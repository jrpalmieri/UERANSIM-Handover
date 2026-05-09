//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "udp_task.hpp"

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <set>

#include <ue/nts.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>

static constexpr const int BUFFER_SIZE = 16384;

namespace nr::ue
{


RlsCellMap::RlsCellMap() : stis(), ncis(), last_seen(), addresses(), m_lastSeenThreshold(0)
{
}

RlsCellMap::RlsCellMap(int64_t last_seen_threshold_ms) : m_lastSeenThreshold(last_seen_threshold_ms), stis(), ncis(), last_seen(), addresses()
{
}

void RlsCellMap::upsertCell(const uint64_t sti, const int64_t last_seen_ts, const InetAddress &address)
{
    // update if present
    for (int i = 0; i < stis.size(); i++)
    {
        if (stis[i] == sti)
        {
            ncis[i] = nciFromSti(sti);
            last_seen[i] = last_seen_ts;
            addresses[i] = address;
            return;
        }
    }

    // Add if new
    stis.push_back(sti);
    ncis.push_back(nciFromSti(sti));
    last_seen.push_back(last_seen_ts);
    addresses.push_back(address);
}

bool RlsCellMap::removeCell(const uint64_t sti)
{
    for (size_t i = 0; i < stis.size(); i++)
    {
        if (stis[i] == sti)
        {
            stis[i] = stis.back();
            stis.pop_back();
            
            ncis[i] = ncis.back();
            ncis.pop_back();
            
            last_seen[i] = last_seen.back();
            last_seen.pop_back();
            
            addresses[i] = addresses.back();
            addresses.pop_back();
            
            return true;
        }
    }
    return false;
}

bool RlsCellMap::isCellInRange(const uint64_t sti) const
{
    for (size_t i = 0; i < stis.size(); i++)
    {
        if (stis[i] == sti)
            return true;
    }
    return false;
}

std::vector<uint64_t> RlsCellMap::checkLastSeen(int64_t now) const
{
    std::vector<uint64_t> staleStis;
    for (size_t i = 0; i < stis.size(); i++)
    {
        if (now - last_seen[i] > m_lastSeenThreshold)
            staleStis.push_back(stis[i]);
    }
    return staleStis;
}

bool RlsCellMap::getAddressNci(int64_t nci, InetAddress &addr)
{
    for (size_t i = 0; i < ncis.size(); i++)
    {
        if (ncis[i] == nci)
        {
            addr = addresses[i];
            return true;
        }
    }
    return false;
}


RlsUdpTask::RlsUdpTask(TaskBase *base, RlsSharedContext *shCtx, const std::vector<std::string> &searchSpace)
    : m_base{base}, m_server{}, m_ctlTask{}, m_shCtx{shCtx}, m_searchSpace{}, m_lastLoop{},
    m_loopCounter{base->config->rls.loopCounter},
    m_receiveTimeout{base->config->rls.receiveTimeout},
    m_heartbeatThreshold{base->config->rls.getHeartbeatThreshold()}
{
    m_logger = base->logBase->makeUniqueLogger(base->config->getLoggerPrefix() + "rls-udp");

    m_server = new udp::UdpServer();

    // set up the cell map for heartbeat testing
    m_cellMap.setLastSeenThreshold(m_heartbeatThreshold);

    //m_cellMap = new RlsCellMap(m_heartbeatThreshold);

    // Search space is the list GNB IPs
    for (auto &ip : searchSpace)
        m_searchSpace.emplace_back(ip, cons::RadioLinkPort);
}

void RlsUdpTask::onStart()
{
}

void RlsUdpTask::onLoop()
{
    // Do a heartbeat cycle every LOOP_COUNTER milliseconds,
    //  - send a HEARTBEAT message to all gnb IPs in the search space
    //  - check if known cells haven't sent HEARTBEAT_ACKs for more than HEARTBEAT_THRESHOLD
    //  
    auto current = utils::CurrentTimeMillis();
    if (current - m_lastLoop > m_loopCounter)
    {
        m_lastLoop = current;
        heartbeatCycle(current);
    }
    uint8_t buffer[BUFFER_SIZE];
    InetAddress peerAddress;

    // pull a UDP message from the server, and if one is received, 
    //  decode it as an RLS message and handle it
    int size = m_server->Receive(buffer, BUFFER_SIZE, m_receiveTimeout, peerAddress);
    if (size > 0)
    {
        auto rlsMsg = rls::DecodeRlsMessage(OctetView{buffer, static_cast<size_t>(size)});
        if (rlsMsg == nullptr)
            m_logger->err("Unable to decode RLS message");
        else
            receiveRlsPdu(peerAddress, std::move(rlsMsg), current);
    }
}

void RlsUdpTask::onQuit()
{
    delete m_server;
}

void RlsUdpTask::sendRlsPdu(const InetAddress &addr, const rls::RlsMessage &msg)
{
    OctetString stream;
    rls::EncodeRlsMessage(msg, stream);

    m_server->Send(addr, stream.data(), static_cast<size_t>(stream.length()));
}

void RlsUdpTask::send(int64_t nci, const rls::RlsMessage &msg)
{
    InetAddress addr;
    if(m_cellMap.getAddressNci(nci, addr))
    {
        sendRlsPdu(addr, msg);
        return;
    }
    m_logger->warn("Unable to send RLS message to cell %d because address is unknown", nci);
    return;
}

void RlsUdpTask::receiveRlsPdu(const InetAddress &addr, std::unique_ptr<rls::RlsMessage> &&msg, int64_t time)
{
    
    // HEARTBEAT_ACKS are from gnbs to advertise themselves
    //   they will contain:
    //    - a simulation temp identifier (STI) =  NCI (36 bits) + Randomizer (10 bits) that is randomly generated by the gnb and should be unique across 
    //       runs of the same gnb in different simulations (avoids stale message problem)
    //    - the dbm signal strength being simulated for this UE as estimated by the gnb
    if (msg->msgType == rls::EMessageType::HEARTBEAT_ACK)
    {
        uint64_t reportedSti = msg->sti;
        
        int newDbm = ((const rls::RlsHeartBeatAck &)*msg).dbm;
        // Only enable the following if ACKS with dbms below the RLF should be ignored
        // if (newDbm < cons::RLF_RSRP)
        // {
        //     m_logger->debug("RLS heartbeat ACK ignored (below RLF threshold): sti=%lu dbm=%d threshold=%d",
        //         msg->sti, newDbm, cons::RLF_RSRP);
        //     return;
        // }

        // check if this is a new cell in the cellmap
        bool newCellFound = !m_cellMap.isCellInRange(reportedSti);

        // add/update the cell info in the cell map
        m_cellMap.upsertCell(reportedSti, time, addr);
        m_base->cellDbMeas.upsertMeasurement(nciFromSti(reportedSti), newDbm);

        m_logger->debug("RLS heartbeat ACK received: sti=%lu NCI=%d dbm=%d",
                reportedSti, nciFromSti(reportedSti), newDbm);

        // check if reported dBm is below the Radio Link Failure threshold
        bool low_signal = newDbm < cons::RLF_RSRP;

        // Notify RRC of a newly detected cell so it can add it to the cell table.
        //   or a low signal so it can trigger RLF.
        if (newCellFound || low_signal)
        {
            auto signalChange = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::SIGNAL_CHANGED);
            signalChange->cellId = nciFromSti(reportedSti);
            signalChange->dbm = newDbm;
            m_ctlTask->push(std::move(signalChange));

            if (newCellFound)
            {
                m_logger->debug("RLS heartbeat ACK - new cell found %d, adding to RRC cell map",
                    nciFromSti(reportedSti));
            }
            else
            {
                m_logger->debug("RLS heartbeat ACK - low signal detected for cell %d, dbm=%d",
                    nciFromSti(reportedSti), newDbm);
            }
        }

        return;
    }


    // if not a heartbeat ack, and the STI is not in the cell map, 
    //  then ignore the message (could be a prior failed simulation)
    if (!m_cellMap.isCellInRange(msg->sti))
    {
        m_logger->warn(
            "Received RLS message with unknown STI %lu. Dropping message",
            msg->sti);

        return;
    }

    // for PDU_TRANSMISSION and other messages, forward to control task with cellId info
    auto w = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::RECEIVE_RLS_MESSAGE);
    w->cellId = nciFromSti(msg->sti);

    w->msg = std::move(msg);
    m_ctlTask->push(std::move(w));
}


void RlsUdpTask::heartbeatCycle(uint64_t time)
{
    // check for missing Heartbeat ACKs
    auto staleStis = m_cellMap.checkLastSeen(time);

    // if any missing, remove from the cell trackers
    for (auto &sti : staleStis)
    {
        m_logger->debug("RLS heartbeat timeout for STI %lu, removing cell", sti);
        m_cellMap.removeCell(sti);
        m_base->cellDbMeas.removeMeasurement(nciFromSti(sti));

        int dbm = cons::MIN_RSRP - 1; // set dbm to below the min value to signal total RLS failure
        auto w = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::SIGNAL_CHANGED);
        w->cellId = nciFromSti(sti);
        w->dbm = dbm;
        m_ctlTask->push(std::move(w));
    }

    // Send HEARTBEATs
    //   for all the IP addresses in the "search space", send a HEARTBEAT message 
    //   with the UE position from TaskBase::UeLocation (single source of truth)
    for (auto &addr : m_searchSpace)
    {
        rls::RlsHeartBeat msg{m_shCtx->sti};
        msg.simPos.latitude = m_base->UeLocation.latitude;
        msg.simPos.longitude = m_base->UeLocation.longitude;
        msg.simPos.altitude = m_base->UeLocation.altitude;
        sendRlsPdu(addr, msg);
    }
}


void RlsUdpTask::initialize(NtsTask *ctlTask)
{
    m_ctlTask = ctlTask;
}

int RlsUdpTask::addSearchSpaceIps(const std::vector<std::string> &ipAddresses)
{
    int added = 0;
    for (const auto &ip : ipAddresses)
    {
        bool exists = false;
        for (const auto &addr : m_searchSpace)
        {
            if (addr.getIpAddrString() == ip)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
        {
            m_searchSpace.emplace_back(ip, cons::RadioLinkPort);
            added++;
        }
    }
    return added;
}

int RlsUdpTask::removeSearchSpaceIps(const std::vector<std::string> &ipAddresses)
{
    int removed = 0;
    std::vector<InetAddress> kept;
    kept.reserve(m_searchSpace.size());

    for (const auto &addr : m_searchSpace)
    {
        auto ipAddrStr = addr.getIpAddrString();
        if (ipAddrStr.empty())
            continue;

        bool shouldRemove = false;
        for (const auto &target : ipAddresses)
        {
            if (ipAddrStr == target)
            {
                shouldRemove = true;
                break;
            }
        }

        if (shouldRemove)
            removed++;
        else
            kept.push_back(addr);
    }

    m_searchSpace = std::move(kept);
    return removed;
}

std::vector<std::string> RlsUdpTask::listSearchSpaceIps() const
{
    std::vector<std::string> ips;
    ips.reserve(m_searchSpace.size());
    for (const auto &addr : m_searchSpace)
    {
        auto ip = addr.getIpAddrString();
        if (!ip.empty())
            ips.push_back(std::move(ip));
    }
    return ips;
}



} // namespace nr::ue
