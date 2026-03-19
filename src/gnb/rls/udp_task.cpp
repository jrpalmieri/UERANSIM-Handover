//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "udp_task.hpp"
#include "sat_pos_sim.hpp"
#include "satellite_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>

#include <gnb/nts.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/libc_error.hpp>

static constexpr const int BUFFER_SIZE = 16384;

static constexpr const int LOOP_PERIOD = 1000;
static constexpr const int RECEIVE_TIMEOUT = 200;
static constexpr const int HEARTBEAT_THRESHOLD = 2000; // (LOOP_PERIOD + RECEIVE_TIMEOUT)'dan büyük olmalı

static constexpr const int MIN_ALLOWED_DBM = -120;

namespace nr::gnb
{

RlsUdpTask::RlsUdpTask(TaskBase *base, uint64_t sti,
                        Vector3 phyLocation)
    : m_base{base}, m_server{}, m_ctlTask{}, m_sti{sti},
      m_cellId{static_cast<uint32_t>(base->config->getCellId())},
      m_phyLocation{phyLocation}, m_lastLoop{},
    m_stiToUe{}, m_ueMap{}, m_newIdCounter{},
    m_fixedRsrp{base->config->rsrp.dbValue}
{
    m_logger = base->logBase->makeUniqueLogger("rls-udp");

    try
    {
        m_server = new udp::UdpServer(base->config->linkIp, cons::RadioLinkPort);
    }
    catch (const LibError &e)
    {
        m_logger->err("RLS failure [%s]", e.what());
        quit();
        return;
    }
}

void RlsUdpTask::onStart()
{
}

void RlsUdpTask::onLoop()
{
    auto current = utils::CurrentTimeMillis();
    if (current - m_lastLoop > LOOP_PERIOD)
    {
        m_lastLoop = current;
        heartbeatCycle(current);
    }

    uint8_t buffer[BUFFER_SIZE];
    InetAddress peerAddress;

    int size = m_server->Receive(buffer, BUFFER_SIZE, RECEIVE_TIMEOUT, peerAddress);
    if (size > 0)
    {
        auto rlsMsg = rls::DecodeRlsMessage(OctetView{buffer, static_cast<size_t>(size)});
        if (rlsMsg == nullptr)
            m_logger->err("Unable to decode RLS message");
        else
            receiveRlsPdu(peerAddress, std::move(rlsMsg));
    }
}

void RlsUdpTask::onQuit()
{
    delete m_server;
}

void RlsUdpTask::receiveRlsPdu(
    const InetAddress &addr,
    std::unique_ptr<rls::RlsMessage> &&msg)
{
    // Satellite position updates are handled internally and not forwarded to the control task
    if (msg->msgType == rls::EMessageType::SATELLITE_POSITION_UPDATE)
    {
        handleSatPositionUpdate(*msg);
        return;
    }

    // RF data messages are handled internally and not forwarded to the control task
    if (msg->msgType == rls::EMessageType::GNB_RF_DATA)
    {
        handleRsrpUpdate(*msg);
        return;
    }

    // UEs provide heartbeat messages to on-net gnbs with their STI and simulated position.
    //   The gNB responds with a simulated RSRP value. The gNB also tracks the UE's address 
    //   and last seen time.
    // Note that in this implementation, heartbeats do NOT cause registration.  UEs must send
    //   an RRCSetupRequest to cause a registration.
    if (msg->msgType == rls::EMessageType::HEARTBEAT)
    {
        auto &hb = static_cast<const rls::RlsHeartBeat &>(*msg);
        int dbm = computeDbm(hb.simPos);
        if (dbm < MIN_ALLOWED_DBM)
        {
            // if the simulated signal strength is such low, then ignore this message
            return;
        }

        int ueId = 0;
        bool isNewUe = false;
        {
            // add a lock here to protect against use by the send function while updating
            std::lock_guard<std::mutex> lock(m_ueMutex);

            // if the STI is in the the stiToUE Map, update the UE info in the UE Map with the
            // provided address and set its last seen time to now.
            if (m_stiToUe.count(msg->sti))
            {
                ueId = m_stiToUe[msg->sti];
                m_ueMap[ueId].address = addr;
                if (msg->senderId2 != 0)
                    m_ueMap[ueId].cRnti = static_cast<int>(msg->senderId2);
                m_ueMap[ueId].lastSeen = utils::CurrentTimeMillis();
            }
            // if the STI is not in the stiToUE Map, add it and assign it a UE ID.
            // push a SIGNAL_DETECTED message to the control task with the new UE ID.
            else
            {
                // although this ternary allows for a gnb-generated UEID, in practice this should
                // never happen as the RLS packet format always includes the senderID.
                ueId = msg->senderId != 0 ? static_cast<int>(msg->senderId) : ++m_newIdCounter;

                m_stiToUe[msg->sti] = ueId;
                m_ueMap[ueId].sti = msg->sti;
                m_ueMap[ueId].address = addr;
                m_ueMap[ueId].cRnti = static_cast<int>(msg->senderId2);
                m_ueMap[ueId].lastSeen = utils::CurrentTimeMillis();
                isNewUe = true;
            }
        }

        // only send the SIGNAL_DETECTED message when the UE is newly detected.
        //  SIGNAL_DETECTED only sends the MIB/SIB, so this is OK for now to reduce traffic.
        if (isNewUe)
        {
            auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::SIGNAL_DETECTED);
            w->ueId = ueId;
            w->cRnti = static_cast<int>(msg->senderId2);
            m_ctlTask->push(std::move(w));
        }

        // send a HEARTBEAT_ACK back to the sender with the simulated signal strength in dBm
        rls::RlsHeartBeatAck ack{m_sti, m_cellId};
        ack.dbm = dbm;

        sendRlsPdu(addr, ack);
        return;
    }

    int ueId = 0;
    int cRnti = static_cast<int>(msg->senderId2);
    {
        std::lock_guard<std::mutex> lock(m_ueMutex);
        if (!m_stiToUe.count(msg->sti))
        {
            // if no HB received yet, and the message is not HB, then ignore the message
            return;
        }

        ueId = m_stiToUe[msg->sti];

        // update the cRnti (catches the RRC assignment)
        if (msg->senderId2 != 0)
            m_ueMap[ueId].cRnti = cRnti;
    }

    // forward the message to the control task
    auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::RECEIVE_RLS_MESSAGE);
    w->ueId = ueId;
    w->msg = std::move(msg);
    w->cRnti = cRnti;
    m_ctlTask->push(std::move(w));
}

void RlsUdpTask::sendRlsPdu(const InetAddress &addr, const rls::RlsMessage &msg)
{
    OctetString stream;
    rls::EncodeRlsMessage(msg, stream);

    m_server->Send(addr, stream.data(), static_cast<size_t>(stream.length()));
}

void RlsUdpTask::heartbeatCycle(int64_t time)
{
    std::set<int> lostUeId{};
    std::set<uint64_t> lostSti{};

    {
        std::lock_guard<std::mutex> lock(m_ueMutex);
        for (auto &item : m_ueMap)
        {
            if (time - item.second.lastSeen > HEARTBEAT_THRESHOLD)
            {
                lostUeId.insert(item.first);
                lostSti.insert(item.second.sti);
            }
        }

        for (uint64_t sti : lostSti)
            m_stiToUe.erase(sti);

        for (int lostId : lostUeId)
            m_ueMap.erase(lostId);
    }

    for (int lostId : lostUeId)
    {
        auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::SIGNAL_LOST);
        w->ueId = lostId;
        m_ctlTask->push(std::move(w));
    }
}

void RlsUdpTask::initialize(NtsTask *ctlTask)
{
    m_ctlTask = ctlTask;
}

void RlsUdpTask::send(int ueId, const rls::RlsMessage &msg)
{
    if (ueId == 0)
    {
        std::vector<InetAddress> broadcastPeers;
        {
            std::lock_guard<std::mutex> lock(m_ueMutex);
            broadcastPeers.reserve(m_ueMap.size());
            for (auto &ue : m_ueMap)
                broadcastPeers.push_back(ue.second.address);
        }

        for (const auto &peer : broadcastPeers)
            sendRlsPdu(peer, msg);
        return;
    }

    InetAddress peer;
    {
        std::lock_guard<std::mutex> lock(m_ueMutex);
        if (!m_ueMap.count(ueId))
        {
            // ignore the message
            return;
        }
        peer = m_ueMap[ueId].address;
    }

    sendRlsPdu(peer, msg);
}

int RlsUdpTask::computeDbm(const GeoPosition &uePos)
{
    auto *cfg = m_base->config;

    if (cfg->rsrp.updateMode == EGnbRsrpMode::Fixed)
        return m_fixedRsrp;

    if (cfg->satSim && m_base->satState)
    {
        EcefPosition satEcef{};
        int64_t posTime{};
        if (m_base->satState->getPosition(satEcef, posTime))
        {
            return SatelliteSimulatedDbm(
                satEcef, uePos, cfg->satLink);
        }
        // No TLE received yet - fall back to static position
    }


    // Terrestrial mode (or satellite fallback)
    return TerrestrialSimulatedDbm(
        cfg->geoLocation, uePos, cfg->satLink);
}

void RlsUdpTask::handleSatPositionUpdate(
    const rls::RlsMessage &msg)
{
    if (!m_base->config->satSim || !m_base->satState)
    {
        m_logger->warn(
            "Received SATELLITE_POSITION_UPDATE but "
            "satSim is disabled — ignoring");
        return;
    }

    auto &spu =
        static_cast<const rls::RlsSatellitePositionUpdate &>(
            msg);

    OrbitalElements elems{};
    if (!ParseTle(spu.tleLine1, spu.tleLine2, elems))
    {
        m_logger->err("Failed to parse TLE from SPU message");
        return;
    }
    // Override epoch if the SPU carries one
    if (spu.epochMs != 0)
        elems.epochMs = spu.epochMs;

    m_base->satState->updateTle(elems);
    m_logger->info("Satellite TLE updated from SPU message");
}


void RlsUdpTask::handleRsrpUpdate(
    const rls::RlsMessage &msg)
{
    auto &rfData = static_cast<const rls::RlsGnbRfData &>(msg);
    int clampedRsrp = std::max(cons::MIN_RSRP,
                               std::min(rfData.rsrp, cons::MAX_RSRP));

    if (clampedRsrp != rfData.rsrp)
    {
        m_logger->warn(
            "Received GNB_RF_DATA rsrp=%d outside supported range, clamped to %d",
            rfData.rsrp, clampedRsrp);
    }

    m_fixedRsrp = clampedRsrp;

    if (m_base->config->rsrp.updateMode == EGnbRsrpMode::Fixed)
    {
        m_logger->debug(
            "Updated fixed RSRP to %d dBm from GNB_RF_DATA",
            m_fixedRsrp);
    }
    else
    {
        m_logger->debug(
            "Stored GNB_RF_DATA rsrp=%d dBm but rsrp.updateMode=Calculated keeps path loss active",
            m_fixedRsrp);
    }
}

} // namespace nr::gnb
