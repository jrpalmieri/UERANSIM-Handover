//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "ctl_task.hpp"

#include <utils/common.hpp>

static constexpr const size_t MAX_PDU_COUNT = 128;

static inline uint64_t pduKey(uint32_t pduId, uint8_t radioBearer)
{
    return (static_cast<uint64_t>(radioBearer) << 32) | pduId;
}
static constexpr const int MAX_PDU_TTL = 3000;

static constexpr const int TIMER_ID_ACK_CONTROL = 1;
static constexpr const int TIMER_ID_ACK_SEND = 2;

// determines whether to require an ACK on an RRC packet
static bool requireRrcAck(rrc::RrcChannel /*channel*/)
{
    return false;
}

// determines whether to require an ACK on a data packet, based on the PDU session type
static bool requireDataAck(int /*psi*/)
{
    return false;
}

namespace nr::ue
{

void RlsControlTask::createRadioBearer(uint8_t bearerId)
{
    auto it = std::find_if(radioBearers.begin(), radioBearers.end(),
        [&](const RadioBearer &b) { return b.bearerId == bearerId; });
    if (it == radioBearers.end())
        radioBearers.push_back(RadioBearer{bearerId, 0, 0});
}



RlsControlTask::RlsControlTask(TaskBase *base, RlsSharedContext *shCtx)
    : m_shCtx{shCtx}, m_servingCell{}, m_mainTask{}, m_udpTask{}, m_pduMap{}, m_pendingAck{},
      m_timerPeriodAckControl{base->config->rls.timerPeriodAckControl},
      m_timerPeriodAckSend{base->config->rls.timerPeriodAckSend}
{
    m_logger = base->logBase->makeUniqueLogger(base->config->getLoggerPrefix() + "rls-ctl");
}

void RlsControlTask::initialize(NtsTask *mainTask, RlsUdpTask *udpTask)
{
    m_mainTask = mainTask;
    m_udpTask = udpTask;
}

void RlsControlTask::onStart()
{
    setTimer(TIMER_ID_ACK_CONTROL, m_timerPeriodAckControl);
    setTimer(TIMER_ID_ACK_SEND, m_timerPeriodAckSend);
}

void RlsControlTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::UE_RLS_TO_RLS: {
        auto &w = dynamic_cast<NmUeRlsToRls &>(*msg);
        switch (w.present)
        {
        case NmUeRlsToRls::SIGNAL_CHANGED:
            handleSignalChange(w.cellId, w.dbm);
            break;
        case NmUeRlsToRls::RECEIVE_RLS_MESSAGE:
            handleRlsMessage(w.cellId, *w.msg);
            break;
        case NmUeRlsToRls::UPLINK_DATA:
            handleUplinkDataDelivery(w.psi, std::move(w.data));
            break;
        case NmUeRlsToRls::UPLINK_RRC:
            handleUplinkRrcDelivery(w.cellId, w.rrcChannel, std::move(w.data));
            break;
        case NmUeRlsToRls::ASSIGN_CURRENT_CELL:
            m_servingCell = w.cellId;
            break;
        case NmUeRlsToRls::RADIO_BEARER_UPDATE:
            handleRadioBearerUpdate(std::move(w.rbUpdate), std::move(w.sdapUpate));
            break;
        default:
            m_logger->unhandledNts(*msg);
            break;
        }
        break;
    }
    case NtsMessageType::TIMER_EXPIRED: {
        auto &w = dynamic_cast<NmTimerExpired &>(*msg);
        if (w.timerId == TIMER_ID_ACK_CONTROL)
        {
            setTimer(TIMER_ID_ACK_CONTROL, m_timerPeriodAckControl);
            onAckControlTimerExpired();
        }
        else if (w.timerId == TIMER_ID_ACK_SEND)
        {
            setTimer(TIMER_ID_ACK_SEND, m_timerPeriodAckSend);
            onAckSendTimerExpired();
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void RlsControlTask::onQuit()
{
}

/**
 * @brief Handles RLS messages received from the GNB, which are forwarded by the UDP task. 
 * These messages will be either a PDU_TRANSMISSION message containing a downlink RRC or data packet, 
 * or a PDU_TRANSMISSION_ACK message acknowledging the receipt of a previously sent uplink packet.
 * 
 * @param cellId 
 * @param msg 
 */
void RlsControlTask::handleRlsMessage(int64_t cellId, rls::RlsMessage &msg)
{
    // if an ACK, then remove the acknowledged PDU from the m_pduMap tracking map
    if (msg.msgType == rls::EMessageType::PDU_TRANSMISSION_ACK)
    {
        auto &m = (rls::RlsPduTransmissionAck &)msg;
        for (size_t i = 0; i < m.pduIds.size(); i++)
            m_pduMap.erase(pduKey(m.pduIds[i], m.radioBearers[i]));
    }
    // PDU msg
    else if (msg.msgType == rls::EMessageType::PDU_TRANSMISSION)
    {
        auto &m = (rls::RlsPduTransmission &)msg;

        // store a pending ACK for this PDU, indexed by cellId,
        //  to be sent back to the gnb in the next ACK_SEND timer cycle.
        if (m.ackPdu)
            m_pendingAck.push_back(pduKey(m.pduId, m.radioBearer));

        // for data packets, forward to main task as a DOWNLINK_DATA message
        if (m.pduType == rls::EPduType::DATA)
        {
            if (cellId != m_servingCell)
            {
                // NOTE: Data packet may be received from a cell other than serving cell
                //  Ignore the packet if this is the case. Other cell can only send RRC, but not DATA
                return;
            }

            auto w = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::DOWNLINK_DATA);
            w->psi = static_cast<int>(m.payloadType);
            w->data = std::move(m.pdu);
            m_mainTask->push(std::move(w));
        }
        // for RRC messages, forward to main task as a DOWNLINK_RRC message
        else if (m.pduType == rls::EPduType::RRC)
        {
            auto w = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::DOWNLINK_RRC);
            w->cellId = cellId;
            w->rrcChannel = static_cast<rrc::RrcChannel>(m.payloadType);
            w->data = std::move(m.pdu);
            m_mainTask->push(std::move(w));
        }
        else
        {
            m_logger->err("Unhandled RLS PDU type");
        }
    }
    else
    {
        m_logger->err("Unhandled RLS message type");
    }
}

void RlsControlTask::handleSignalChange(int64_t cellId, int dbm)
{
    auto w = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::SIGNAL_CHANGED);
    w->cellId = cellId;
    w->dbm = dbm;
    m_mainTask->push(std::move(w));
}

/**
 * @brief Used to forward uplink RRC messages to the gnb via the UDP task.  
 * Also tracks sent PDUs for acknowledgment, by storing them in m_pduMap indexed by pduId, 
 * and setting a sentTime for each PDU to detect transmission failures.
 * 
 * @param cellId 
 * @param pduId 
 * @param channel 
 * @param data 
 */
void RlsControlTask::handleUplinkRrcDelivery(int64_t nci, rrc::RrcChannel channel, OctetString &&data)
{
    // check if this RRC message requires an acknowledgment
    bool ackPdu = requireRrcAck(channel);

    uint32_t pduId = 0;
    uint8_t radioBearer = 0; // SRB0

    // get the appropriate radio bearer and PDU ID for this RRC message
    selectSignalingRadioBearer(channel, radioBearer, pduId);

    // PDU send tracking: if the message has a non-zero pduId, 
    //  then it is tracked in m_pduMap until acknowledged by the gnb.
    if (ackPdu)
    {
        const uint64_t key = pduKey(pduId, radioBearer);

        // check if a PDU with this key is already being tracked,
        //  which would indicate a bug in the RRC task where it is reusing pduIds.
        if (m_pduMap.count(key))
        {
            m_pduMap.clear();

            auto w = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::RADIO_LINK_FAILURE);
            w->rlfCause = rls::ERlfCause::PDU_ID_EXISTS;
            w->cellId = nci;
            m_mainTask->push(std::move(w));
            return;
        }

        // overflow check
        if (m_pduMap.size() > MAX_PDU_COUNT)
        {
            m_pduMap.clear();

            auto w = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::RADIO_LINK_FAILURE);
            w->rlfCause = rls::ERlfCause::PDU_ID_FULL;
            w->cellId = nci;
            m_mainTask->push(std::move(w));
            return;
        }

        // add PDU to the map for tracking until acknowledgment, with the current time as sentTime
        auto &info = m_pduMap[key];
        info.endPointId = nci;
        info.id = pduId;
        info.radioBearer = radioBearer;
        info.pdu = data.copy();
        info.rrcChannel = channel;
        info.sentTime = utils::CurrentTimeMillis();
    }

    // create a new RlsPduTransmission message with the provided RRC payload and 
    //  send it to the UDP task to be forwarded to the gnb
    rls::RlsPduTransmission msg{m_shCtx->sti};
    msg.pduType = rls::EPduType::RRC;
    msg.ackPdu = ackPdu;
    msg.radioBearer = radioBearer;
    msg.pdu = std::move(data);
    msg.payloadType = static_cast<uint32_t>(channel);
    msg.pduId = pduId;

    m_udpTask->send(nci, msg);
}

/**
 * @brief Used to forward uplink data packets to the gnb 
 * via the UDP task.
 * 
 * @param pduSessionId the session ID for this PDU Session
 * @param data the data to send
 */
void RlsControlTask::handleUplinkDataDelivery(int pduSessionId, OctetString &&data)
{
    uint8_t radioBearer = 0X41; // DRB1
    uint32_t pduId = 0;
    int qfi = 0;

    // get the appropriate radio bearer and PDU ID for this data packet
    sdapMapping(pduSessionId, data, &radioBearer, &pduId, &qfi);
    bool ackPdu = requireDataAck(pduSessionId);

    // create a new RlsPduTransmission message with the provided data payload and
    //  send it to the UDP task to be forwarded to the gnb
    rls::RlsPduTransmission msg{m_shCtx->sti};
    msg.pduType = rls::EPduType::DATA;
    msg.ackPdu = ackPdu;
    msg.radioBearer = radioBearer;
    msg.pdu = std::move(data);
    msg.payloadType = static_cast<uint32_t>(pduSessionId);
    msg.pduId = pduId;
    msg.sdapByte = static_cast<uint8_t>(qfi);

    m_udpTask->send(m_servingCell, msg);
}

/**
 * @brief Used to detect when PDUs that have been sent to the gnb have not been 
 * acknowledged within a reasonable time frame, which may indicate a radio link failure.
 * This function is called when the ACK control timer expires, which is set to a period 
 * of TIMER_PERIOD_ACK_CONTROL milliseconds.  The PDUs being tracked for acknowledgment are 
 * stored in m_pduMap, which is a map of PduInfo indexed by pduId.
 * 
 */
void RlsControlTask::onAckControlTimerExpired()
{
    int64_t current = utils::CurrentTimeMillis();

    std::vector<uint64_t> transmissionFailureIds;
    std::vector<rls::PduInfo> transmissionFailures;

    // loop through each tracked PDU in m_pduMap, and if any of them have been 
    //  unacknowledged for more than MAX_PDU_TTL milliseconds, mark as a transmission failure. 
    for (auto &pdu : m_pduMap)
    {
        auto delta = current - pdu.second.sentTime;
        if (delta > MAX_PDU_TTL)
        {
            transmissionFailureIds.push_back(pdu.first);
            transmissionFailures.push_back(std::move(pdu.second));
        }
    }

    // remove all failed PDUs from the tracking map
    for (auto id : transmissionFailureIds)
        m_pduMap.erase(id);

    // push a TRANSMISSION_FAILURE message to the main task with the vector of failed PDUs
    if (!transmissionFailures.empty())
    {
        auto w = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::TRANSMISSION_FAILURE);
        w->pduList = std::move(transmissionFailures);
        m_mainTask->push(std::move(w));
    }
}

/**
 * @brief used to send PDU_TRANSMISSION_ACK messages to the gnb for all 
 * received PDUs that are pending acknowledgment.  PDUs pending acknowledgment are 
 * tracked in m_pendingAck.
 */
void RlsControlTask::onAckSendTimerExpired()
{
    auto copy = m_pendingAck;
    m_pendingAck.clear();

    rls::RlsPduTransmissionAck msg{m_shCtx->sti};
    msg.pduIds.reserve(copy.size());
    msg.radioBearers.reserve(copy.size());

    for (auto &entry : copy)
    {
        msg.pduIds.push_back(static_cast<uint32_t>(entry & 0xFFFFFFFF));
        msg.radioBearers.push_back(static_cast<uint32_t>(entry >> 32));
    }

    m_udpTask->send(m_servingCell, msg);
}

// Maps a PDU onto a radio data bearer and returns the next PDU ID to use. Uses the sdapMapping vector to do the mapping.
//
//  Currently just maps based on PDU Session Id.  But actual data is passed in so in future can support more complex mapping based on QoS flow, etc.
void RlsControlTask::sdapMapping(int pduSessionId, OctetString &data, uint8_t *radioBearer, uint32_t *pduId, int *qfi)
{

    // if want to map based on QoS flow, would need to insert some logic here
    //  to parse the QoS flow ID from the data, and then find the corresponding SDAP mapping based on both PDU session ID and QoS flow ID.

    // find existing mapping for this PDU session ID
    auto it = std::find_if(m_sdapMappings.begin(), m_sdapMappings.end(),
        [&](const SdapMapping &m) { return m.psi == pduSessionId; });

    if (it != m_sdapMappings.end())
    {
        *radioBearer = it->radioBearer;
        *qfi = it->qfi;
    }

    // use radioBearer to determine the next PDU ID to use for this session, and increment the sequence number for the radio bearer
    auto rbIt = std::find_if(radioBearers.begin(), radioBearers.end(),
        [&](const RadioBearer &b) { return b.bearerId == *radioBearer; });
    if (rbIt == radioBearers.end())
    {
        m_logger->err("No radio bearer found for SDAP mapping");
        return;
    }

    *pduId = rbIt->ulSn++;

    return;
}

void RlsControlTask::selectSignalingRadioBearer(rrc::RrcChannel channel, uint8_t &radioBearer, uint32_t &pduId)
{
    // For now, just find SRB0 and use it for signaling
    auto srb0It = std::find_if(radioBearers.begin(), radioBearers.end(),
        [](const RadioBearer &b) { return b.bearerId == 0; });

    if (srb0It != radioBearers.end())
    {
        radioBearer = srb0It->bearerId;
        pduId = srb0It->ulSn++;
    }
    else
    {
        m_logger->err("SRB0 not found for signaling");
    }
    return;
}

// Handle the radio bearer and SDAP updates from RRC
void RlsControlTask::handleRadioBearerUpdate(std::unique_ptr<RadioBearerUpdate> rbUpdate, std::unique_ptr<SdapUpdate> sdapUpdate)
{
    // Handle the radio bearer update
    if (rbUpdate)
    {
        // Delete any radio bearers that are no longer needed
        for (const auto &bearer : rbUpdate->deleteBearers)
        {
            // Remove the radio bearer from the list
            radioBearers.erase(std::remove_if(radioBearers.begin(), radioBearers.end(),
                [&bearer](const RadioBearer &b) { return b.bearerId == bearer; }), radioBearers.end());
            m_logger->info("Deleting radio bearer with ID %d", bearer);
        }

        // Process the radio bearer updates
        for (const auto &bearer : rbUpdate->upsertBearers)
        {
            // Check if the radio bearer already exists
            auto it = std::find_if(radioBearers.begin(), radioBearers.end(),
                [&bearer](const RadioBearer &b) { return b.bearerId == bearer.bearerId; });
            if (it != radioBearers.end())
            {
                // Update the existing radio bearer
                *it = bearer;
            }
            else
            {
                // Add the new radio bearer
                radioBearers.push_back(bearer);
            }

            // Add or modify the radio bearer
            m_logger->info("Adding/modifying radio bearer with ID %d", bearer.bearerId);
        }

    }

    // Update the SDAP mappings
    if (sdapUpdate)
    {
        for (const auto &mapping : sdapUpdate->deleteSdapMappings)
        {
            // Remove the SDAP mapping from the list
            m_sdapMappings.erase(std::remove_if(m_sdapMappings.begin(), m_sdapMappings.end(),
                [&mapping](const SdapMapping &m) { return m.psi == mapping.psi && m.qfi == mapping.qfi && m.radioBearer == mapping.radioBearer; }), m_sdapMappings.end());

            m_logger->info("Deleting SDAP mapping for PDU session %d, QFI %d, radio bearer %d", mapping.psi, mapping.qfi, mapping.radioBearer);
        }

        // Process the SDAP updates
        for (const auto &mapping : sdapUpdate->upsertSdapMappings)
        {
            // Check if the SDAP mapping already exists
            auto it = std::find_if(m_sdapMappings.begin(), m_sdapMappings.end(),
                [&mapping](const SdapMapping &m) { return m.psi == mapping.psi && m.qfi == mapping.qfi && m.radioBearer == mapping.radioBearer; });
            if (it != m_sdapMappings.end())
            {
                // Update the existing SDAP mapping
                *it = mapping;
            }
            else
            {
                // Add the new SDAP mapping
                m_sdapMappings.push_back(mapping);
            }

            // Add or modify the SDAP mapping
            m_logger->info("Adding/modifying SDAP mapping for PDU session %d, QFI %d, radio bearer %d", mapping.psi, mapping.qfi, mapping.radioBearer);
        }

    }
}

} // namespace nr::ue
