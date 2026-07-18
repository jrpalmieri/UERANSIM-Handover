//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "ctl_task.hpp"

#include <algorithm>
#include <shared_mutex>
#include <stdexcept>
#include <utils/common.hpp>

static constexpr const size_t MAX_PDU_COUNT = 4096;
static constexpr const int MAX_PDU_TTL = 3000;

// Packs (radioBearer, pduId) into a single uint64 key for the per-UE PDU map.
static inline uint64_t pduKey(uint32_t pduId, uint8_t radioBearer)
{
    return static_cast<uint64_t>(radioBearer) << 32 | pduId;
}

static constexpr const int TIMER_ID_ACK_CONTROL = 1;
static constexpr const int TIMER_ID_ACK_SEND = 2;

// determines whether to require an ACK on an RRC packet
static bool requireRrcAck(rrc::RrcChannel /*channel*/)
{
    return false;
}

static bool requireDataAck(int /*psi*/)
{
    return false;
}

namespace nr::gnb
{

// ── UE context helpers ──

void RlsControlTask::createRlsUeContext(int64_t ueId)
{
    // emplace avoids operator[] which requires a default constructor
    auto [it, inserted] = m_ueCtx.emplace(ueId, RlsUeContext(ueId));
    if (inserted)
    {
        // Only SRB0 exists until a PDU session is established.  DRBs are created
        // on demand by RRC (createRadioBearerConfig / assignSessionBearers) via
        // RADIO_BEARER_UPDATE; RRC owns DRB-id allocation, so there is no default
        // DRB to pre-seed here.
        it->second.radioBearers.push_back(RadioBearer{0, 0, 0}); // SRB0
    }
}

void RlsControlTask::deleteRlsUeContext(int64_t ueId)
{
    m_ueCtx.erase(ueId);
}

RlsUeContext *RlsControlTask::getRlsUeContext(int64_t ueId)
{
    // use find to avoid operator[] which requires a default constructor
    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end())
        return nullptr;
    return &it->second;
}

// void RlsControlTask::updateUeBearer(int64_t ueId, uint8_t radioBearer, uint32_t pduId)
// {
//     auto ctx = getRlsUeContext(ueId);
//     auto &bearers = ctx->radioBearers;
//     auto it = std::find_if(bearers.begin(), bearers.end(),
//         [&](const RadioBearer &b) { return b.bearerId == (radioBearer & 0x7f); });
//     if (it != bearers.end())
//         it->ulSn = pduId;
//     else
//         m_logger->warn("UE[%ld]: Received PDU for unknown radio bearer %d", ueId, radioBearer & 0x7f);
// }

// ── Constructor / lifecycle ───────────────────────────────────────────────────

RlsControlTask::RlsControlTask(TaskBase *base, uint64_t sti)
    : m_sti{sti}, m_cellId{base->config->nci}, m_mainTask{}, m_udpTask{},
        m_timerPeriodAckControl{base->config->rls.timerPeriodAckControl},
        m_timerPeriodAckSend{base->config->rls.timerPeriodAckSend}
{
    m_logger = base->logBase->makeUniqueLogger("rls-ctl");
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
    case NtsMessageType::GNB_RLS_TO_RLS: {
        auto &w = dynamic_cast<NmGnbRlsToRls &>(*msg);
        switch (w.present)
        {
        case NmGnbRlsToRls::SIGNAL_DETECTED:
            handleSignalDetected(w.ueId);
            break;
        case NmGnbRlsToRls::SIGNAL_LOST:
            handleSignalLost(w.ueId);
            break;
        case NmGnbRlsToRls::RECEIVE_RLS_MESSAGE:
            handleRlsMessage(w);
            break;
        case NmGnbRlsToRls::DOWNLINK_DATA:
            handleDownlinkDataDelivery(w.ueId, w.psi, w.qfi, std::move(w.data));
            break;
        case NmGnbRlsToRls::DOWNLINK_RRC:
            handleDownlinkRrcDelivery(w.ueId, w.rrcChannel, std::move(w.data));
            break;
        case NmGnbRlsToRls::RADIO_BEARER_UPDATE:
            handleRadioBearerUpdate(w.ueId, std::move(w.rbUpdate), std::move(w.sdapUpdate));
            break;
        case NmGnbRlsToRls::APPLY_DRB_SN_STATUS:
            handleApplyDrbSnStatus(w.ueId, w.drbSnStatus);
            break;
        case NmGnbRlsToRls::REMOVE_UE_CONTEXT:
            handleRemoveUeContext(w.ueId);
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

// ── Signal handlers ───────────────────────────────────────────────────────────

void RlsControlTask::handleSignalDetected(int64_t ueId)
{
    std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);
    if (!m_ueCtx.count(ueId))
        createRlsUeContext(ueId);

    auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::SIGNAL_DETECTED);
    w->ueId = ueId;
    m_mainTask->push(std::move(w));
}

void RlsControlTask::handleSignalLost(int64_t ueId)
{
    // Note: do not delete RLS context until UE Context Release is provided from RRC
    //  as this may be used in reconnect or handover scenarios.
    auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::SIGNAL_LOST);
    w->ueId = ueId;
    m_mainTask->push(std::move(w));
}

// ── RLS message handler ───────────────────────────────────────────────────────

void RlsControlTask::handleRlsMessage(NmGnbRlsToRls &w)
{
    std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);
    int64_t ueId = w.ueId;
    auto &msg    = *w.msg;

    if (msg.msgType == rls::EMessageType::PDU_TRANSMISSION_ACK)
    {
        auto &m = (rls::RlsPduTransmissionAck &)msg;
        auto ctx = getRlsUeContext(ueId);
        for (size_t i = 0; i < m.pduIds.size(); i++)
            ctx->m_pduMap.erase(pduKey(m.pduIds[i], m.radioBearers[i] & 0x7f));
    }
    else if (msg.msgType == rls::EMessageType::PDU_TRANSMISSION)
    {
        auto &m = (rls::RlsPduTransmission &)msg;
        auto ctx = getRlsUeContext(ueId);

        if (m.ackPdu)
            ctx->m_pendingAck.push_back(pduKey(m.pduId, m.radioBearer));

        if (m.pduType == rls::EPduType::DATA)
        {
            // On the source gNB this is the simulated PDCP UL receive COUNT.
            // Keep the next expected value so Xn SN Status Transfer can align
            // the target with the UE's continuing uplink sequence.
            auto bearer = std::find_if(ctx->radioBearers.begin(), ctx->radioBearers.end(),
                [&m](const RadioBearer &b) { return b.bearerId == (m.radioBearer & 0x7f); });
            if (bearer != ctx->radioBearers.end())
            {
                const uint32_t nextCount = NextPdcpCount(m.pduId);
                // UINT32_MAX is the final COUNT in the cycle; its successor is
                // zero and must not be rejected by the normal monotonic check.
                bearer->ulSn = nextCount == 0 ? 0 : std::max(bearer->ulSn, nextCount);
            }
            else
                m_logger->warn("UE[%ld] uplink PDU received for unknown DRB %d", ueId, m.radioBearer & 0x3f);

            auto out = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::UPLINK_DATA);
            out->ueId = ueId;
            out->cRnti = ctx->cRnti;
            out->qfi  = static_cast<int>(m.sdapByte & 0x3f);
            out->psi  = static_cast<int>(m.payloadType);
            out->data = std::move(m.pdu);
            m_mainTask->push(std::move(out));
            m_logger->debug("UE[%ld]: received uplink data. Psi=%d, PduId=%u, RB=%02x, QFI=%d, AckPdu=%s", ueId, m.payloadType, m.pduId, m.radioBearer & 0x7f, m.sdapByte & 0x3f, m.ackPdu ? "true" : "false");
        }
        else if (m.pduType == rls::EPduType::RRC)
        {
            //updateUeBearer(ueId, m.radioBearer, m.pduId);
            auto out = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::UPLINK_RRC);
            out->ueId       = ueId;
            out->cRnti      = ctx->cRnti;
            out->rrcChannel = static_cast<rrc::RrcChannel>(m.payloadType);
            out->data       = std::move(m.pdu);
            m_mainTask->push(std::move(out));
            m_logger->debug("UE[%ld]: received uplink RRC. Channel=%d, PduId=%u, RB=%02x, QFI=%d, AckPdu=%s", ueId, m.payloadType, m.pduId, m.radioBearer & 0x7f, m.sdapByte & 0x3f, m.ackPdu ? "true" : "false");

        }
        else
        {
            m_logger->debug("UE[%ld]: received uplink UNKNOWN PDU. PayloadType=%d, PduId=%u, RB=%02x, QFI=%d, AckPdu=%s", ueId, m.payloadType, m.pduId, m.radioBearer & 0x7f, m.sdapByte & 0x3f, m.ackPdu ? "true" : "false");
        }
    }
    else
    {
        m_logger->err("Unhandled RLS message type. Packet dropped.");
    }
}

// ── Downlink delivery ─────────────────────────────────────────────────────────

void RlsControlTask::handleDownlinkRrcDelivery(int64_t ueId, rrc::RrcChannel channel, OctetString &&data)
{
    uint32_t pduId      = 0;
    bool     ackPdu     = requireRrcAck(channel);
    uint8_t  radioBearer = 0; // SRB0

    if (ueId != 0)
    // critical section
    {
        std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);
        auto ctx = getRlsUeContext(ueId);
        if (!ctx)
        {
            m_logger->warn("Received RRC for unknown UE ID %ld. Message dropped.", ueId);
            return;
        }

        // this does nothing right now.  Would need to add a mapping of RRC channels to radio bearers to support more than just SRB0.
        auto it = std::find_if(ctx->radioBearers.begin(), ctx->radioBearers.end(),
            [radioBearer](const RadioBearer &b) { return b.bearerId == radioBearer; });
        if (it == ctx->radioBearers.end())
        {
            m_logger->warn("UE[%ld]: No bearer %d for downlink RRC. Message dropped.", ueId, radioBearer);
            return;
        }
        pduId = it->dlSn;
        // Allocate the current COUNT, then explicitly prepare the next one.
        it->dlSn = NextPdcpCount(it->dlSn);

        if (ackPdu)
        {
            const uint64_t key = pduKey(pduId, radioBearer);

            if (ctx->m_pduMap.count(key))
            {
                auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::RADIO_LINK_FAILURE);
                w->rlfCause = rls::ERlfCause::PDU_ID_EXISTS;
                m_mainTask->push(std::move(w));
                return;
            }

            if (ctx->m_pduMap.size() > MAX_PDU_COUNT)
            {
                auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::RADIO_LINK_FAILURE);
                w->rlfCause = rls::ERlfCause::PDU_ID_FULL;
                m_mainTask->push(std::move(w));
                return;
            }

            auto &info = ctx->m_pduMap[key];
            info.id          = pduId;
            info.radioBearer = radioBearer;
            info.pdu         = data.copy();
            info.rrcChannel  = channel;
            info.sentTime    = utils::CurrentTimeMillis();
        }
    }

    rls::RlsPduTransmission msg{m_sti};
    msg.pduType     = rls::EPduType::RRC;
    msg.radioBearer = radioBearer;
    msg.sdapByte    = 0;
    msg.ackPdu      = ackPdu;
    msg.pdu         = std::move(data);
    msg.payloadType = static_cast<uint32_t>(channel);
    msg.pduId       = pduId;
    m_udpTask->send(ueId, msg);
}


void RlsControlTask::getBearerFromSdap(RlsUeContext &ctx, int psi, int qfi, uint8_t *radioBearer, uint32_t *pduId)
{

    *radioBearer = 0x41; // default to DRB1 if no SDAP mapping found

    // match PSI and QFI to find the corresponding bearer ID and DL SN
    auto it = std::find_if(ctx.sdapMappings.begin(), ctx.sdapMappings.end(),
            [psi, qfi](const SdapMapping &m) { return m.qfi == qfi && m.psi == psi; });

    if (it != ctx.sdapMappings.end())
    {
        *radioBearer = it->radioBearer;
    }
    
    // find bearer for the given radioBearer ID to get the current DL SN for PDU ID assignment
    auto bearerIt = std::find_if(ctx.radioBearers.begin(), ctx.radioBearers.end(),
            [radioBearer](const RadioBearer &b) { return b.bearerId == (*radioBearer & 0x7f); });

    if (bearerIt != ctx.radioBearers.end())
    {
        *pduId = bearerIt->dlSn;
        // The combined simulated PDCP COUNT restarts after UINT32_MAX.
        bearerIt->dlSn = NextPdcpCount(bearerIt->dlSn);
    }
    else
    {
        m_logger->warn("UE[%ld]: No bearer %d for SDAP mapping. Defaulting to DRB1.", ctx.ueId, *radioBearer & 0x7f);
        *radioBearer = 0x41; // default to DRB1
        auto defaultBearerIt = std::find_if(ctx.radioBearers.begin(), ctx.radioBearers.end(),
            [](const RadioBearer &b) { return b.bearerId == 0x41; });
        if (defaultBearerIt != ctx.radioBearers.end())
        {
            *pduId = defaultBearerIt->dlSn;
            // Apply the same rollover rule on the fallback DRB path.
            defaultBearerIt->dlSn = NextPdcpCount(defaultBearerIt->dlSn);
        }
        else
        {
            m_logger->warn("UE[%ld]: No default bearer DRB1 found. PDU ID assignment failed.", ctx.ueId);
            *pduId = 0; // fallback to 0, but this is a logic error - there should always be a default data bearer.
        }
    }

}
    

void RlsControlTask::handleDownlinkDataDelivery(int64_t ueId, int psi, int qfi, OctetString &&data)
{
    uint32_t pduId      = 0;
    bool     ackPdu     = requireDataAck(psi);
    uint8_t radioBearer = 0x41; // DRB1

    m_logger->debug("UE[%ld]: Handling downlink data delivery for PSI=%d, QFI=%d", ueId, psi, qfi);

    // critical section
    {
        std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);

        auto ctx = getRlsUeContext(ueId);
        if (!ctx)
        {
            m_logger->warn("Received data for unknown UE ID %ld. Message dropped.", ueId);
            return;
        }

        getBearerFromSdap(*ctx, psi, qfi, &radioBearer, &pduId);

        if (ackPdu)
        {
            const uint64_t key = pduKey(pduId, radioBearer);

            if (ctx->m_pduMap.count(key))
            {
                auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::RADIO_LINK_FAILURE);
                w->rlfCause = rls::ERlfCause::PDU_ID_EXISTS;
                m_mainTask->push(std::move(w));
                return;
            }

            if (ctx->m_pduMap.size() > MAX_PDU_COUNT)
            {
                auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::RADIO_LINK_FAILURE);
                w->rlfCause = rls::ERlfCause::PDU_ID_FULL;
                m_mainTask->push(std::move(w));
                return;
            }

            auto &info = ctx->m_pduMap[key];
            info.id          = pduId;
            info.radioBearer = radioBearer;
            info.pdu         = data.copy();
            info.sentTime    = utils::CurrentTimeMillis();
        }
    }

    m_logger->debug("UE[%ld]: Sending downlink data to UE. PDU ID=%u, QFI=%d, RB=%02x, AckPDU=%s", ueId, pduId, qfi, radioBearer, ackPdu ? "true" : "false");

    rls::RlsPduTransmission msg{m_sti};
    msg.pduType     = rls::EPduType::DATA;
    msg.radioBearer = radioBearer;
    msg.ackPdu      = ackPdu;
    msg.pdu         = std::move(data);
    msg.payloadType = static_cast<uint32_t>(psi);
    msg.pduId       = pduId;
    msg.sdapByte    = static_cast<uint8_t>(qfi);
    m_udpTask->send(ueId, msg);
}


// Handles a message from RRC to update the Radio Bearer and SDAP mappings for a UE.
void RlsControlTask::handleRadioBearerUpdate(int64_t ueId, std::unique_ptr<RadioBearerUpdate> rbUpdate, std::unique_ptr<SdapUpdate> sdapUpdate)
{
    m_logger->info("UE[%ld]: Handling RadioBearer/SDAP update", ueId);

    std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);
    auto ctx = getRlsUeContext(ueId);
    if (!ctx)
    {
        // Target bearer preparation normally precedes the UE's first heartbeat
        // at this cell.  Create the RLS context now instead of dropping the
        // configuration and leaving later SN Status permanently unresolved.
        createRlsUeContext(ueId);
        ctx = getRlsUeContext(ueId);
    }

    if (rbUpdate)
    {
        // Delete old radio bearers
        for (const auto &bearerId : rbUpdate->deleteBearers)
        {
            ctx->radioBearers.erase(std::remove_if(ctx->radioBearers.begin(), ctx->radioBearers.end(),
                                                    [bearerId](const RadioBearer &b) { return b.bearerId == bearerId; }),
                                     ctx->radioBearers.end());

            m_logger->debug("UE[%ld]: Deleting radio bearer %02x", ueId, bearerId);
        }

        // Add/update new bearers
        for (const auto &bearer : rbUpdate->upsertBearers)
        {
            // Update the radio bearer in the context
            auto it = std::find_if(ctx->radioBearers.begin(), ctx->radioBearers.end(),
                                    [bearer](const RadioBearer &b) { return b.bearerId == bearer.bearerId; });
            if (it != ctx->radioBearers.end())
            {
                *it = bearer;
                m_logger->debug("UE[%ld]: Updating radio bearer %02x", ueId, bearer.bearerId);
            }
            else
            {
                ctx->radioBearers.push_back(bearer);
                m_logger->debug("UE[%ld]: Adding radio bearer %02x", ueId, bearer.bearerId);
            }
        }
    }

    if (sdapUpdate)
    {
        // Delete old SDAP mappings
        for (const auto &mapping : sdapUpdate->deleteSdapMappings)
        {
            ctx->sdapMappings.erase(std::remove_if(ctx->sdapMappings.begin(), ctx->sdapMappings.end(),
                                                    [mapping](const SdapMapping &m) { return m.qfi == mapping.qfi && m.psi == mapping.psi; }),
                                     ctx->sdapMappings.end());
            m_logger->debug("UE[%ld]: Deleting SDAP mapping (qfi=%d, psi=%d)", ueId, mapping.qfi, mapping.psi);
        }
        // Process SDAP update
        for (const auto &mapping : sdapUpdate->upsertSdapMappings)
        {
            // Update the SDAP mapping in the context
            auto it = std::find_if(ctx->sdapMappings.begin(), ctx->sdapMappings.end(),
                                    [mapping](const SdapMapping &m) { return m.qfi == mapping.qfi && m.psi == mapping.psi; });
            if (it != ctx->sdapMappings.end())
            {
                *it = mapping;
                m_logger->debug("UE[%ld]: Updating SDAP mapping (qfi=%d, psi=%d)", ueId, mapping.qfi, mapping.psi);
            }
            else
            {
                ctx->sdapMappings.push_back(mapping);
                m_logger->debug("UE[%ld]: Adding SDAP mapping (qfi=%d, psi=%d)", ueId, mapping.qfi, mapping.psi);
            }
        }
    }

    auto deferred = m_deferredSnStatus.find(ueId);
    if (deferred != m_deferredSnStatus.end())
    {
        auto pending = std::move(deferred->second);
        m_deferredSnStatus.erase(deferred);
        applyOrDeferDrbSnStatus(ueId, *ctx, pending);
    }
}

void RlsControlTask::applyOrDeferDrbSnStatus(int64_t ueId, RlsUeContext &ctx,
                                             const std::vector<DrbSnStatus> &status)
{
    std::vector<DrbSnStatus> unresolved;
    for (const auto &item : status)
    {
        const uint8_t encodedBearer = static_cast<uint8_t>(0x40 | item.drbId);
        auto bearer = std::find_if(ctx.radioBearers.begin(), ctx.radioBearers.end(),
            [encodedBearer](const RadioBearer &b) { return b.bearerId == encodedBearer; });
        if (bearer == ctx.radioBearers.end())
        {
            unresolved.push_back(item);
            continue;
        }
        // A late/retransmitted transfer must not move a counter backwards if
        // target traffic has already advanced it.
        bearer->ulSn = std::max(bearer->ulSn, item.nextUlCount);
        bearer->dlSn = std::max(bearer->dlSn, item.nextDlCount);
        m_logger->info("UE[%ld] applied Xn SN status DRB[%d] UL=%u DL=%u",
                       ueId, item.drbId, item.nextUlCount, item.nextDlCount);
    }
    if (!unresolved.empty())
    {
        m_deferredSnStatus[ueId] = std::move(unresolved);
        m_logger->debug("UE[%ld] deferred SN status for %zu not-yet-configured DRBs",
                        ueId, m_deferredSnStatus[ueId].size());
    }
}

void RlsControlTask::handleApplyDrbSnStatus(int64_t ueId, const std::vector<DrbSnStatus> &status)
{
    std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);
    auto *ctx = getRlsUeContext(ueId);
    if (!ctx)
    {
        m_deferredSnStatus[ueId] = status;
        return;
    }
    applyOrDeferDrbSnStatus(ueId, *ctx, status);
}

void RlsControlTask::handleRemoveUeContext(int64_t ueId)
{
    std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);
    deleteRlsUeContext(ueId);
    m_deferredSnStatus.erase(ueId);
    m_logger->info("UE[%ld] provisional target RLS context removed", ueId);
}


// ── Timer handlers ────────────────────────────────────────────────────────────

void RlsControlTask::onAckControlTimerExpired()
{
    std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);
    int64_t current = utils::CurrentTimeMillis();

    // Iterate through each UE context
    for (auto &entry : m_ueCtx)
    {
        int64_t ueId  = entry.first;
        auto   &ctx   = entry.second;

        std::vector<uint64_t> expiredKeys;
        std::vector<rls::PduInfo> failures;

        for (auto &kv : ctx.m_pduMap)
        {
            if (current - kv.second.sentTime > MAX_PDU_TTL)
            {
                expiredKeys.push_back(kv.first);
                failures.push_back(std::move(kv.second));
            }
        }

        for (auto key : expiredKeys)
            ctx.m_pduMap.erase(key);

        if (!failures.empty())
        {
            auto w = std::make_unique<NmGnbRlsToRls>(NmGnbRlsToRls::TRANSMISSION_FAILURE);
            w->ueId    = ueId;
            w->pduList = std::move(failures);
            m_mainTask->push(std::move(w));
        }
    }
}

void RlsControlTask::onAckSendTimerExpired()
{
    std::unique_lock<std::shared_mutex> lock(m_ueCtxMutex);
    for (auto &entry : m_ueCtx)
    {
        int64_t ueId = entry.first;
        auto   &ctx  = entry.second;

        if (ctx.m_pendingAck.empty())
            continue;

        rls::RlsPduTransmissionAck msg{m_sti};
        msg.pduIds.reserve(ctx.m_pendingAck.size());
        msg.radioBearers.reserve(ctx.m_pendingAck.size());

        for (uint64_t packed : ctx.m_pendingAck)
        {
            msg.pduIds.push_back(static_cast<uint32_t>(packed & 0xFFFFFFFF));
            msg.radioBearers.push_back(static_cast<uint8_t>((packed >> 32) & 0xFF));
        }
        ctx.m_pendingAck.clear();

        m_udpTask->send(ueId, msg);
    }
}

// ── Cross-task context access ─────────────────────────────────────────────────

std::optional<RlsUeContext> RlsControlTask::copyUeContext(int64_t ueId) const
{
    std::shared_lock<std::shared_mutex> lock(m_ueCtxMutex);
    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end())
        return std::nullopt;

    // PduInfo contains a non-copyable OctetString, so we manually copy only the
    // fields that callers (RRC, Xn SN Status Transfer) actually need.
    const auto &src = it->second;
    RlsUeContext snap(src.ueId);
    snap.sti          = src.sti;
    snap.cRnti        = src.cRnti;
    snap.radioBearers = src.radioBearers;   // RadioBearer is trivially copyable
    snap.m_pendingAck = src.m_pendingAck;   // vector<uint64_t> is copyable
    snap.sdapMappings = src.sdapMappings;
    // m_pduMap omitted: rebuilding it would require OctetString::copy() per entry
    return snap;
}

} // namespace nr::gnb
