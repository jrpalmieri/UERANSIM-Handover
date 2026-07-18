#include "task.hpp"

// NGAP/Xn task definitions are needed because receiveRrcReconfigurationComplete
// pushes completion messages to both tasks
#include <gnb/ngap/task.hpp>
#include <gnb/xn/task.hpp>

#include <asn/rrc/ASN_RRC_DL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1530-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete-IEs.h>


namespace nr::gnb
{

// creates the outer message IE for the RRC Reconfiguration message
ASN_RRC_DL_DCCH_Message* GnbRrcTask::makeRrcReconfiguration(int64_t ueId)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->err("UE[%ld] not found for RRCReconfiguration", ueId);
        return nullptr;
    }

    // Build RRCReconfiguration
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present =
        ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();

    long txId = ue->getNextTid();
    reconfig->rrc_TransactionIdentifier = txId;
    reconfig->criticalExtensions.present =
        ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();


    return pdu;
}

/**
 * @brief Receives an RRCReconfigurationComplete message from a UE, which may indicate the 
 * completion of a handover.  If this is a handover completion, triggers post-handover 
 * processing such as NGAP notification. Otherwise, just logs the completion of a normal 
 * reconfiguration.
 * 
 * @param ueId 
 * @param msg 
 */
void GnbRrcTask::receiveRrcReconfigurationComplete(int64_t ueId, int cRnti,
    const ASN_RRC_RRCReconfigurationComplete &msg)
{
    int64_t txId = msg.rrc_TransactionIdentifier;

    int64_t resolvedUeId = ueId;

    // A UE can share txId values with other UEs (txId is tiny), so match by UE ID first,
    // then verify txId for that UE's pending handover.
    auto itPending = m_handoversPending.find(resolvedUeId);
    bool matchedPending =
        itPending != m_handoversPending.end() &&
        itPending->second.ctx != nullptr &&
        itPending->second.rrcReconfigurationTxId == txId &&
        (cRnti <= 0 || itPending->second.ctx->cRnti == cRnti);

    // if matchedPending is False, this either isn't associated with a pending handover, or its got a bad UEID
    //   We check the cRNTI and txId against the pending handovers to see if we can find a match 
    //   and resolve the correct UE ID
    if (!matchedPending && cRnti > 0)
    {
        // If UE ID was mis-associated on UL delivery, remap using (txId, cRnti).
        for (auto it = m_handoversPending.begin(); it != m_handoversPending.end(); ++it)
        {
            auto &pending = it->second;
            if (!pending.ctx)
                continue;

            if (pending.rrcReconfigurationTxId == txId && pending.ctx->cRnti == cRnti)
            {
                resolvedUeId = it->first;
                itPending = it;
                matchedPending = true;

                if (resolvedUeId != ueId)
                {
                    m_logger->warn(
                        "RRCReconfigurationComplete UE remap: incomingUeId=%ld resolvedUeId=%ld txId=%ld cRnti=%d",
                        ueId, resolvedUeId, txId, cRnti);
                }
                break;
            }
        }
    }

    m_logger->debug("UE[%ld]: RRCReconfigurationComplete received with txId=%ld cRnti=%d matchedPendingHandover=%s",
                    ueId, txId, cRnti, matchedPending ? "true" : "false");

    // matchedPending is True if there is pending handover, so complete it by moving the pending
    // context to the main UE context map.
    if (matchedPending)
    {

        /* move the ctx from pending handover to UE context */

        // get ptr to rrc context in the pending handover map (indexed by UE ID)
        auto *handoverCtx = itPending->second.ctx;
        const bool completedViaXn = itPending->second.isXn;

        // check for old UE context with the same UE ID, if exists, remove it 
        // (since after handover completion, the old UE context is no longer valid)
        auto *ue = findCtxByUeId(resolvedUeId);
        if (ue)
        {
            releaseCrnti(ue->cRnti);
            delete ue;
            m_ueCtx.erase(resolvedUeId);
        }

        // move the UE context from pending handover to UE context map and erase the pending handover
        m_ueCtx[resolvedUeId] = handoverCtx;
        m_handoversPending.erase(itPending);

        // not sure if this is still needed, but clean it up anyway
        handoverCtx->handoverInProgress = false;

        // Send measurement config to UE to restart measurement reporting
        // after handover.
        sendMeasConfig(resolvedUeId, true);

        // N2 completion is reported with HandoverNotify.  Xn completion instead
        // activates the provisional target context and asks the AMF to switch
        // the N3 path; sending both would run two incompatible procedures.
        auto w = std::make_unique<NmGnbRrcToNgap>(completedViaXn
            ? NmGnbRrcToNgap::PATH_SWITCH_REQUEST
            : NmGnbRrcToNgap::HANDOVER_NOTIFY_SEND);
        w->ueId = resolvedUeId;
        m_base->ngapTask->push(std::move(w));

        if (completedViaXn)
        {
            // This is an early execution indication only.  Source resource
            // deletion remains gated by UEContextRelease after Path Switch.
            auto success = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::HANDOVER_SUCCESS_SEND);
            success->ueId = resolvedUeId;
            m_base->xnTask->push(std::move(success));
        }

        m_logger->info("UE[%ld] %s handover radio execution completed; %s requested",
                       resolvedUeId, completedViaXn ? "Xn" : "N2",
                       completedViaXn ? "Path Switch" : "Handover Notify");
        return;

    }

    // other RRCReconfigComplete msgs

    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->warn("UE[%ld] RRCReconfigurationComplete received from unknown UE, ignoring", ueId);
        return;
    }

    // no gnb action needed for non-handover RRCReconfigurationComplete, just log it

    m_logger->info("UE[%ld] RRCReconfigurationComplete received txId=%ld", ueId, txId);

}

} // namespace nr::gnb