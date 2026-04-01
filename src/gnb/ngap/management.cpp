//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <utils/common.hpp>

namespace nr::gnb
{

NgapAmfContext *NgapTask::findAmfContext(int ctxId)
{
    NgapAmfContext *ctx = nullptr;
    if (m_amfCtx.count(ctxId))
        ctx = m_amfCtx[ctxId];
    if (ctx == nullptr)
        m_logger->err("AMF context not found with id: %d", ctxId);
    return ctx;
}

void NgapTask::createAmfContext(const GnbAmfConfig &conf)
{
    auto *ctx = new NgapAmfContext();
    ctx->ctxId = utils::NextId();
    ctx->state = EAmfState::NOT_CONNECTED;
    ctx->address = conf.address;
    ctx->port = conf.port;
    m_amfCtx[ctx->ctxId] = ctx;
}

void NgapTask::createUeContext(int ueId, int32_t &requestedSliceType)
{
    auto *ctx = new NgapUeContext(ueId);

    // AMF UE NGAP ID is assigned by the AMF, so initialize to -1 to indicate not assigned
    ctx->amfUeNgapId = -1;

    // RAN UE NGAP ID is assigned by the gNB, so assign a new unique ID
    ctx->ranUeNgapId = generateRanUeNgapId(ueId);
    m_logger->debug("UE[%d] NGAP context created: ranUeNgapId=%ld", ueId, ctx->ranUeNgapId);

    m_ueCtx[ctx->ctxId] = ctx;

    // Perform AMF selection
    auto *amf = selectAmf(ueId, requestedSliceType);
    if (amf == nullptr)
        m_logger->err("UE[%d] AMF selection failed. Could not find a suitable AMF.", ueId);
    else
        ctx->associatedAmfId = amf->ctxId;
}

NgapUeContext *NgapTask::findUeContext(int ctxId)
{
    NgapUeContext *ctx = nullptr;
    if (m_ueCtx.count(ctxId))
        ctx = m_ueCtx[ctxId];
    if (ctx == nullptr)
        m_logger->err("UE[%d] NGAP context not found", ctxId);
    return ctx;
}

NgapUeContext *NgapTask::findUeByRanId(int64_t ranUeNgapId)
{
    if (ranUeNgapId <= 0)
        return nullptr;
    // TODO: optimize
    for (auto &ue : m_ueCtx)
        if (ue.second != nullptr && ue.second->ranUeNgapId == ranUeNgapId)
            return ue.second;
    return nullptr;
}

/**
 * @brief Finds the UE's NGAP context in `m_ueCtx` based on the AMF UE NGAP ID.
 *   This can be used when receiving messages from the AMF that only include the AMF UE NGAP ID,
 *   such as HandoverRequest.  If no matching UE context is found, returns nullptr.
 * 
 * @param amfUeNgapId 
 * @return NgapUeContext* 
 */
NgapUeContext *NgapTask::findUeByAmfId(int64_t amfUeNgapId)
{
    if (amfUeNgapId <= 0)
        return nullptr;
    // TODO: optimize
    for (auto &ue : m_ueCtx)
        if (ue.second != nullptr && ue.second->amfUeNgapId == amfUeNgapId)
            return ue.second;
    return nullptr;
}

NgapUeContext *NgapTask::findUeByNgapIdPair(int amfCtxId, const NgapIdPair &idPair)
{
    auto &amfId = idPair.amfUeNgapId;
    auto &ranId = idPair.ranUeNgapId;

    if (!amfId.has_value() && !ranId.has_value())
    {
        sendErrorIndication(amfCtxId, NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message);
        return nullptr;
    }

    if (!amfId.has_value())
    {
        auto ue = findUeByRanId(ranId.value());
        if (ue == nullptr)
        {
            sendErrorIndication(amfCtxId, NgapCause::RadioNetwork_unknown_local_UE_NGAP_ID);
            return nullptr;
        }

        return ue;
    }

    if (!ranId.has_value())
    {
        auto ue = findUeByAmfId(amfId.value());
        if (ue == nullptr)
        {
            sendErrorIndication(amfCtxId, NgapCause::RadioNetwork_inconsistent_remote_UE_NGAP_ID);
            return nullptr;
        }

        return ue;
    }

    auto ue = findUeByRanId(ranId.value());
    if (ue == nullptr)
    {
        sendErrorIndication(amfCtxId, NgapCause::RadioNetwork_unknown_local_UE_NGAP_ID);
        return nullptr;
    }

    if (ue->amfUeNgapId == -1)
        ue->amfUeNgapId = amfId.value();  // TODO: this seems to be a cheat to handle a missing ngapID
    else if (ue->amfUeNgapId != amfId.value())
    {
        sendErrorIndication(amfCtxId, NgapCause::RadioNetwork_inconsistent_remote_UE_NGAP_ID);
        return nullptr;
    }

    return ue;
}

void NgapTask::deleteUeContext(int ueId)
{
    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end())
    {
        m_logger->err("UE[%d] NGAP context not found, no deletion performed", ueId);
        return;
    }

    delete it->second;
    m_ueCtx.erase(it);
    m_logger->debug("UE[%d] NGAP context deleted", ueId);
}

void NgapTask::deleteAmfContext(int amfId)
{
    auto it = m_amfCtx.find(amfId);
    if (it == m_amfCtx.end())
    {
        m_logger->err("AMF context with id %d not found, no deletion performed", amfId);
        return;
    }

    delete it->second;
    m_amfCtx.erase(it);
}

/**
 * @brief Generates the RAN UE NGAP ID for a new UE context. The generation logic is a combination of
 * the physical cell id (PCI) and the UE Id, so that the generated ID is unique across all UEs and gnbs.
 *  
 * @param ueId 
 * @return int64_t 
 */
int64_t NgapTask::generateRanUeNgapId(int ueId) 
{

    int pci = m_base->config->getCellId() & 0x3FF;
    if (pci < 0)
        pci = 0;

    // The RAN UE NGAP ID is constructed as follows:
    // | 10 bits PCI | 22 bits UeId |
    // NGAP constrains RAN_UE_NGAP_ID to 0..4294967295 (32 bits), so keep it strictly within 32 bits.
    uint32_t localUeBits = static_cast<uint32_t>(ueId) & ((1u << 22) - 1u);
    uint32_t ranUeNgapId = (static_cast<uint32_t>(pci) << 22) | localUeBits;
    return static_cast<int64_t>(ranUeNgapId);

}

} // namespace nr::gnb