//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "udp_task.hpp"
#include "pos_sim.hpp"

#include <algorithm>
#include <cmath>

#include <libsgp4/DateTime.h>
#include <libsgp4/Eci.h>
#include <libsgp4/SGP4.h>
#include <libsgp4/Tle.h>
#include <cstdint>
#include <cstring>
#include <set>

#include <gnb/nts.hpp>

#include <lib/sat/sat_time.hpp>
#include <lib/sat/sat_state.hpp>
#include <lib/sat/sat_calc.hpp>

#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/libc_error.hpp>

static constexpr const int BUFFER_SIZE = 16384;

static constexpr const int MIN_ALLOWED_DBM = -120;

namespace nr::gnb
{

RlsUdpTask::RlsUdpTask(TaskBase *base, uint64_t sti,
                        Vector3 phyLocation)
    : m_base{base}, m_server{}, m_ctlTask{}, m_sti{sti},
      m_cellId{static_cast<uint32_t>(base->config->getCellId())},
      m_phyLocation{phyLocation}, m_lastLoop{},
    m_stiToUe{}, m_ueMap{}, m_newIdCounter{},
                m_fixedRsrp{base->config->rfLink.rsrpDbValue},
        m_loopCounter{base->config->rls.loopCounter},
        m_receiveTimeout{base->config->rls.receiveTimeout},
        m_heartbeatThreshold{base->config->rls.getHeartbeatThreshold()}
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
    double fsplBasePtr;
    double uMaPathLossBasePtr;
    double uMaPathLossFarPtr;
    double linkBudgetBasePtr;

    pre_calculations(m_base->config->rfLink.carrFrequencyHz, m_base->config->rfLink.txPowerDbm, 
        m_base->config->rfLink.txGainDbi, m_base->config->rfLink.ueRxGainDbi, fsplBasePtr, uMaPathLossBasePtr, uMaPathLossFarPtr, linkBudgetBasePtr);
    
    m_logger->debug("Storing pre-calculations of RSRP values: fsplBase=%f, umaBase=%f, umaFar=%f, linkBudgetBase=%f", fsplBasePtr, uMaPathLossBasePtr, uMaPathLossFarPtr, linkBudgetBasePtr);
}

void RlsUdpTask::onLoop()
{
    auto current = utils::CurrentTimeMillis();
    if (current - m_lastLoop > m_loopCounter)
    {
        m_lastLoop = current;
        heartbeatCycle(current);
    }

    uint8_t buffer[BUFFER_SIZE];
    InetAddress peerAddress;

    int size = m_server->Receive(buffer, BUFFER_SIZE, m_receiveTimeout, peerAddress);
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

    int64_t currentTime = utils::CurrentTimeMillis();

    // UEs provide heartbeat messages to on-net GNBs with their STI and simulated position.
    //   The gNB responds with a simulated RSRP value. The gNB also tracks the UE's address 
    //   and last seen time.
    // Note that in this implementation, heartbeats do NOT cause registration.  UEs must send
    //   an RRCSetupRequest to cause a registration.  Instead, heartbeats allow for simulation of RF conditions
    //   experienced by UEs, and allow for setup of the UDP connections between UEs and GNBs.
    if (msg->msgType == rls::EMessageType::HEARTBEAT)
    {
        auto &hb = static_cast<const rls::RlsHeartBeat &>(*msg);

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
                m_ueMap[ueId].lastSeen    = currentTime;
                m_ueMap[ueId].lastPos     = hb.simPos;
                m_ueMap[ueId].hasPosData  = true;
            }
            // If the STI is not in the stiToUE Map, add it and assign it a UE ID.
            else
            {
                // although this ternary allows for a gnb-generated UEID, in practice this should
                // never happen as the RLS packet format always includes the senderID.
                ueId = msg->senderId != 0 ? static_cast<int>(msg->senderId) : ++m_newIdCounter;

                m_stiToUe[msg->sti] = ueId;
                m_ueMap[ueId].sti        = msg->sti;
                m_ueMap[ueId].address    = addr;
                m_ueMap[ueId].cRnti      = static_cast<int>(msg->senderId2);
                m_ueMap[ueId].lastSeen   = currentTime;
                m_ueMap[ueId].lastPos    = hb.simPos;
                m_ueMap[ueId].hasPosData = true;
                isNewUe = true;
            }
        }

        // For newly seen UEs, push a SIGNAL_DETECTED message to the control task with the new UE ID, which will
        //    trigger sending of the MIB/SIB1 to the UE.
        //    Note that in real networks, the MIB/SIB1 are sent frequently (every ~50-200ms) to handle UE movement and RF channel variability.
        //      In this implementation, we send them upon initial detection and then a long intervals to simplify the implementation and 
        //      reduce unnecessary message sending, since the simulated RF channel is stable.
        if (isNewUe)
        {
            auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::SIGNAL_DETECTED);
            w->ueId = ueId;
            w->cRnti = static_cast<int>(msg->senderId2);
            m_ctlTask->push(std::move(w));
        }

        // Send a HEARTBEAT_ACK back to the sender with the simulated signal strength in dBm
        rls::RlsHeartBeatAck ack{m_sti, m_cellId};

        // compute the RSRP in dBm
        ack.dbm = computeDbm(hb.simPos, ueId);

        sendRlsPdu(addr, ack);

        // update the global measurement data with the reported UE position
        m_base->setUePosition(ueId, hb.simPos, currentTime);
        return;
    }

    // Derive the UeID and C-RNTI from the message's STI and senderId2, respectively, and update the UE info with the new C-RNTI if provided.
    int ueId = 0;
    int cRnti = static_cast<int>(msg->senderId2);
    GeoPosition uePos{};
    bool hasPosData = false;
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

        // snapshot the last known UE position for this message
        uePos      = m_ueMap[ueId].lastPos;
        hasPosData = m_ueMap[ueId].hasPosData;
    }

    // If we get here, this is a non-heartbeat message from a known UE.

    // update the global measurement data with the reported UE position
    if (hasPosData)
        m_base->setUePosition(ueId, uePos, currentTime);

    //  Forward it to the control task for processing, including the UE ID, C-RNTI, and last known UE position.
    auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::RECEIVE_RLS_MESSAGE);
    w->ueId       = ueId;
    w->msg        = std::move(msg);
    w->cRnti      = cRnti;
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
            if (time - item.second.lastSeen > m_heartbeatThreshold)
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

/**
 * @brief Calculates the RSRP value in dBm for a given UE position based on the gNB's configuration.
 * For non-NTN configurations, this is the path loss based on the locations of the UE and gnb using 
 * terrestrial network path loss models.
 * For NTN configurations, this is the path loss based on the locations of the UE and the GNB's satellite using
 * satellite path loss models.
 * 
 * @param uePos - UE's current position in lat/lon/alt, which is provided by the UE in the heartbeat message
 * @return int - RSRP in dbm
 */
int RlsUdpTask::computeDbm(const GeoPosition &uePos, int ueId)
{
    auto *cfg = m_base->config;
    int dbm = 0;

    // if configured for "fixed" mode, just return the fixed RSRP value
    if (cfg->rfLink.updateMode == EGnbRsrpMode::Fixed)
        return m_base->fixedRsrp;

    // get current gnb position
    GeoPosition gnbPos = m_base->getGnbPosition();;

    // if we are in NTN mode, use the satellite model to calculate the path loss.
    if (cfg->ntn.ntnEnabled)
    {
        dbm = SatelliteSimulatedDbm(nr::sat::GeoToEcef(gnbPos), uePos, cfg->rfLink);
    }
    // Terrestrial mode path loss
    else 
    {
        dbm = TerrestrialSimulatedDbm(gnbPos, uePos, cfg->rfLink);
    }

    // if returned power value is below minimum, this signals that UE is below horizon.
    //   debug log this to help with testing and verification
    if (dbm < cons::MIN_RSRP) 
    {
        m_logger->debug("UE[%d} - below horizon, clamping to min RSRP %d dBm.", ueId, cons::MIN_RSRP);
        dbm = cons::MIN_RSRP;
    }
    return dbm;
}



} // namespace nr::gnb
