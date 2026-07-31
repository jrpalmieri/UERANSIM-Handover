//
// NGAP Handover Procedures (N2 / AMF-mediated)
//
// Implements:
//   - sendHandoverRequired()           → source gNB → AMF
//   - receiveHandoverCommand()         ← AMF → source gNB (SuccessfulOutcome)
//   - receiveHandoverPreparationFailure() ← AMF → source gNB (UnsuccessfulOutcome)
//   - sendHandoverNotify()             → target gNB → AMF
//   - handleHandoverNotifyFromRrc()    (orchestrator called from task.cpp)
//

#include "encode.hpp"
#include "task.hpp"
#include "utils.hpp"

#include <gnb/gtp/task.hpp>
#include <gnb/neighbors.hpp>
#include <gnb/rrc/task.hpp>
#include <utils/common.hpp>

#include <asn/ngap/ASN_NGAP_Cause.h>
#include <asn/ngap/ASN_NGAP_GlobalGNB-ID.h>
#include <asn/ngap/ASN_NGAP_GlobalRANNodeID.h>
#include <asn/ngap/ASN_NGAP_GNB-ID.h>
#include <asn/ngap/ASN_NGAP_HandoverCommand.h>
#include <asn/ngap/ASN_NGAP_HandoverCommandTransfer.h>
#include <asn/ngap/ASN_NGAP_HandoverFailure.h>
#include <asn/ngap/ASN_NGAP_HandoverRequiredTransfer.h>
#include <asn/ngap/ASN_NGAP_HandoverRequest.h>
#include <asn/ngap/ASN_NGAP_HandoverRequestAcknowledge.h>
#include <asn/ngap/ASN_NGAP_HandoverRequestAcknowledgeTransfer.h>
#include <asn/ngap/ASN_NGAP_HandoverResourceAllocationUnsuccessfulTransfer.h>
#include <asn/ngap/ASN_NGAP_HandoverNotify.h>
#include <asn/ngap/ASN_NGAP_HandoverPreparationFailure.h>
#include <asn/ngap/ASN_NGAP_HandoverRequired.h>
#include <asn/ngap/ASN_NGAP_HandoverType.h>
#include <asn/ngap/ASN_NGAP_NGAP-PDU.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceAdmittedItem.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceAdmittedList.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceFailedToSetupListHOAck.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceHandoverItem.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceHandoverList.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceItemHORqd.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceListHORqd.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemHOReq.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupRequestTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceToReleaseItemHOCmd.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceToReleaseListHOCmd.h>
#include <asn/ngap/ASN_NGAP_ProtocolIE-Field.h>
#include <asn/ngap/ASN_NGAP_QosFlowItemWithDataForwarding.h>
#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestItem.h>
#include <asn/ngap/ASN_NGAP_SourceToTarget-TransparentContainer.h>
#include <asn/ngap/ASN_NGAP_SourceNGRANNode-ToTargetNGRANNode-TransparentContainer.h>
#include <asn/ngap/ASN_NGAP_SuccessfulOutcome.h>
#include <asn/ngap/ASN_NGAP_TAI.h>
#include <asn/ngap/ASN_NGAP_TargetID.h>
#include <asn/ngap/ASN_NGAP_TargetRANNodeID.h>
#include <asn/ngap/ASN_NGAP_TargetToSource-TransparentContainer.h>
#include <asn/ngap/ASN_NGAP_TargetNGRANNode-ToSourceNGRANNode-TransparentContainer.h>
#include <asn/ngap/ASN_NGAP_UPTransportLayerInformation.h>
#include <asn/ngap/ASN_NGAP_GTPTunnel.h>
#include <asn/ngap/ASN_NGAP_UnsuccessfulOutcome.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformation.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformationNR.h>
#include <asn/ngap/ASN_NGAP_NR-CGI.h>

// No asn/rrc includes: NGAP does not build, decode or inspect RRC messages.  The RRC
// containers it carries are opaque byte strings handed to and from the RRC task.

#include <optional>

const int HANDOVER_TIMEOUT_MS = 5000;

namespace nr::gnb
{

// The layout of the transparent containers (magic, version, flags, lengths) is not
// declared here on purpose: those containers belong to the two gNBs' RRC layers and are
// opaque byte strings to NGAP.  See gnb/rrc/handover_container.hpp.

// Must stay aligned with (or exceed) the RRC layer's COND_HANDOVER_TIMEOUT_MS
// (rrc/handover.cpp, 100 s): both pending maps guard the same CHO preparation,
// and NGAP expiring first would strand a still-valid RRC candidate — its
// eventual HandoverNotify would find no NGAP context to promote.
static constexpr int CHO_CANDIDATE_TIMEOUT_MS = 100000;


static OctetString MakeHoSetupUnsuccessfulTransfer(NgapCause cause)
{
    auto *tr = asn::New<ASN_NGAP_HandoverResourceAllocationUnsuccessfulTransfer>();
    ngap_utils::ToCauseAsn_Ref(cause, tr->cause);

    OctetString encoded =
        ngap_encode::EncodeS(asn_DEF_ASN_NGAP_HandoverResourceAllocationUnsuccessfulTransfer, tr);
    asn::Free(asn_DEF_ASN_NGAP_HandoverResourceAllocationUnsuccessfulTransfer, tr);

    return encoded;
}

static OctetString MakeHoAcknowledgeTransfer(const PduSessionResource &resource)
{
    auto *tr = asn::New<ASN_NGAP_HandoverRequestAcknowledgeTransfer>();

    auto &upInfo = tr->dL_NGU_UP_TNLInformation;
    upInfo.present = ASN_NGAP_UPTransportLayerInformation_PR_gTPTunnel;
    upInfo.choice.gTPTunnel = asn::New<ASN_NGAP_GTPTunnel>();
    asn::SetBitString(upInfo.choice.gTPTunnel->transportLayerAddress, resource.downTunnel.address);
    asn::SetOctetString4(upInfo.choice.gTPTunnel->gTP_TEID, (octet4)resource.downTunnel.teid);

    for (const auto &flow : resource.qosFlows)
    {
        auto *qosItem = asn::New<ASN_NGAP_QosFlowItemWithDataForwarding>();
        qosItem->qosFlowIdentifier = flow.qfi;
        asn::SequenceAdd(tr->qosFlowSetupResponseList, qosItem);
    }

    OctetString encoded = ngap_encode::EncodeS(asn_DEF_ASN_NGAP_HandoverRequestAcknowledgeTransfer, tr);
    asn::Free(asn_DEF_ASN_NGAP_HandoverRequestAcknowledgeTransfer, tr);

    return encoded;
}

static OctetString MakeHoRequiredTransfer()
{
    // Open5GS expects a present and decodable transfer blob per PDU session item.
    auto *tr = asn::New<ASN_NGAP_HandoverRequiredTransfer>();
    OctetString encoded = ngap_encode::EncodeS(asn_DEF_ASN_NGAP_HandoverRequiredTransfer, tr);
    asn::Free(asn_DEF_ASN_NGAP_HandoverRequiredTransfer, tr);
    return encoded;
}




void NgapTask::sendHandoverFailure(NgapCause cause, int64_t ueId) 
{
    auto *causeIe = asn::New<ASN_NGAP_ProtocolIE_Field_13561P106>();
    causeIe->id = ASN_NGAP_ProtocolIE_ID_id_Cause;
    causeIe->criticality = ASN_NGAP_Criticality_ignore;
    causeIe->value.present = ASN_NGAP_ProtocolIE_Field_13561P106__value_PR_Cause;
    ngap_utils::ToCauseAsn_Ref(cause, causeIe->value.choice.Cause);

    auto *failurePdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverFailure>({causeIe});
    sendNgapUeAssociated(ueId, failurePdu);

};

/**
 * @brief Handles HANDOVER_FAILURE_SEND from RRC: the target-side RRC could not
 * prepare the requested handover (container decode failure, C-RNTI exhaustion, ...).
 * Sends an NGAP HandoverFailure to the AMF for this transaction and drops the
 * pending target-side context, so the AMF can convert the failure into a
 * HandoverPreparationFailure toward the source gNB instead of waiting for an
 * acknowledge that will never come.
 *
 * Note: no established UE context exists at this point — the provisional context
 * lives only in m_handoversPending (its ueId may not be resolved yet), so the
 * mandatory AMF-UE-NGAP-ID IE is filled explicitly from the pending entry and the
 * PDU is sent with sendNgapNonUe() rather than sendNgapUeAssociated().
 *
 * @param transactionId the NGAP transaction id assigned in receiveHandoverRequest()
 * @param cause         failure cause reported by RRC
 */
void NgapTask::handleRrcHandoverFailure(uint32_t transactionId, NgapCause cause)
{
    auto it = m_handoversPending.find(transactionId);
    if (it == m_handoversPending.end())
    {
        m_logger->err("handleRrcHandoverFailure: no pending handover found for transaction ID %u", transactionId);
        return;
    }
    auto &pending = it->second;

    m_logger->info("NGTxId[%u]: RRC rejected HandoverRequest (cause=%d), sending HandoverFailure to AMF[%d]",
                   transactionId, static_cast<int>(cause), pending.amfId);

    // AMF-UE-NGAP-ID is mandatory in HandoverFailure; take it from the provisional context.
    auto *idIe = asn::New<ASN_NGAP_ProtocolIE_Field_13561P106>();
    idIe->id = ASN_NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID;
    idIe->criticality = ASN_NGAP_Criticality_ignore;
    idIe->value.present = ASN_NGAP_ProtocolIE_Field_13561P106__value_PR_AMF_UE_NGAP_ID;
    asn::SetSigned64(pending.ctx ? pending.ctx->amfUeNgapId : 0, idIe->value.choice.AMF_UE_NGAP_ID);

    auto *causeIe = asn::New<ASN_NGAP_ProtocolIE_Field_13561P106>();
    causeIe->id = ASN_NGAP_ProtocolIE_ID_id_Cause;
    causeIe->criticality = ASN_NGAP_Criticality_ignore;
    causeIe->value.present = ASN_NGAP_ProtocolIE_Field_13561P106__value_PR_Cause;
    ngap_utils::ToCauseAsn_Ref(cause, causeIe->value.choice.Cause);

    auto *failurePdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverFailure>({idIe, causeIe});
    sendNgapNonUe(pending.amfId, failurePdu);

    // Drop the pending target-side context (the unique_ptr ctx is freed by the erase).
    m_handoversPending.erase(it);
}

/**
 * @brief Periodic garbage collection of the N2 target-side pending-handover map
 * (TIMER_HO_PENDING_SWEEP, every 1 s) — the NGAP counterpart of
 * GnbRrcTask::sweepPendingHandovers().
 *
 * A pending entry whose UE never completed the handover (no HandoverNotify
 * promotion) is dropped once its expireTime passes (5 s basic /
 * CHO_CANDIDATE_TIMEOUT_MS for CHO, stamped in receiveHandoverRequest).
 * If the preparation was already ACKed, the UE identity was adopted
 * (pending.ueId > 0) and GTP holds a provisional UE context plus sessions —
 * release them, unless an *active* NGAP context exists for that ueId (never
 * destroy a live UE's user plane).  Pre-ACK entries still carry ueId 0 and
 * have created no GTP state.
 *
 * Deliberately out of scope here:
 *  - m_xnHandoversPending is not swept — RRC's own sweep cancels it via
 *    XN_TARGET_PREPARATION_CANCEL (cancelXnTargetPreparation is idempotent).
 *  - No message is sent to the AMF: the AMF already holds our
 *    HandoverRequestAcknowledge (or HandoverFailure), and its own relocation
 *    supervision timer covers a UE that never arrives.
 */
void NgapTask::sweepPendingHandovers()
{
    if (m_handoversPending.empty())
        return;

    const int64_t now = utils::CurrentTimeMillis();

    for (auto it = m_handoversPending.begin(); it != m_handoversPending.end();)
    {
        auto &pending = it->second;
        if (pending.expireTime > now)
        {
            ++it;
            continue;
        }

        m_logger->warn("NGTxId[%u]: pending %s handover expired (ueId=%ld); discarding provisional NGAP context",
                       it->first, pending.choCandidate ? "CHO" : "N2", pending.ueId);

        // Roll back the provisional GTP state created at ACK time.  ueId == 0
        // means the ACK never happened (identity not adopted), so GTP holds
        // nothing for this preparation.
        if (pending.ueId > 0 && m_ueCtx.count(pending.ueId) == 0)
        {
            auto gtp = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_RELEASE_RECEIVED);
            gtp->ueId = pending.ueId;
            gtp->cause = NgapCause::RadioNetwork_tngrelocoverall_expiry;
            m_base->gtpTask->push(std::move(gtp));
        }

        // The provisional NgapUeContext (unique_ptr) is freed by the erase.
        it = m_handoversPending.erase(it);
    }
}

// TODO: failure mode without UE context
// void NgapTask::sendHandoverFailure(int64_t amfUeNgapId, uint16_t stream, NgapCause cause) 
// {
//     auto *causeIe = asn::New<ASN_NGAP_ProtocolIE_Field_13561P106>();
//     causeIe->id = ASN_NGAP_ProtocolIE_ID_id_Cause;
//     causeIe->criticality = ASN_NGAP_Criticality_ignore;
//     causeIe->value.present = ASN_NGAP_ProtocolIE_Field_13561P106__value_PR_Cause;
//     ngap_utils::ToCauseAsn_Ref(cause, causeIe->value.choice.Cause);

//     auto *failurePdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverFailure>({causeIe});
//     sendNgapUeAssociatedError(amfUeNgapId, stream, failurePdu);

// };

/**
 * @brief Source GNB sends a HandoverRequired message to the AMF to trigger handover for the specified UE 
 * to the target NCI. The cause parameter indicates the reason for the handover and is included in the 
 * message to assist the AMF in making informed decisions during the handover process.
 * 
 * Note: in this simulator, the HandoverRequired message includes a custom SourceToTarget-TransparentContainer 
 * that encapsulates the RRC handover preparation information.  This is done to avoid modification of the ASN.1
 * definitions and to simplify the implementation by reusing the existing NGAP message structure. The custom container
 * is identified by a specific magic number in the beginning of the container payload, allowing the target
 * gNB to recognize and decode the RRC handover preparation information.
 * 
 * @param ueId 
 * @param targetNci 
 * @param cause 
 * @param rrcContainer The RRC handover preparation information to be included in the HandoverRequired message
 */
void NgapTask::sendHandoverRequired(int64_t ueId, int64_t targetNci, NgapCause cause, bool hoForChoPreparation, std::unique_ptr<OctetString> rrcContainer)
{
    m_logger->info("UE[%ld] Sending HandoverRequired to AMF targetNCI=%ld", ueId, targetNci);

    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("UE[%ld]: sendHandoverRequired: UE context not found. Aborting.", ueId);
        return;
    }

    // RRC owns the source-to-target container (it holds the UE's RRC context); NGAP only
    // relays it.  Without it the target has nothing to build a UE context from, so there is
    // no point in starting the procedure.
    if (!rrcContainer || rrcContainer->length() == 0)
    {
        m_logger->err("UE[%ld]: sendHandoverRequired: no source-to-target container provided by RRC. Aborting.", ueId);
        return;
    }

    // get target cell info
    auto neighborOpt = m_base->neighbors->findByNci(targetNci);
    if (!neighborOpt)
    {
        m_logger->err("UE[%ld]: sendHandoverRequired - target NCI=%ld not found in neighborList. Aborting.", ueId, targetNci);
        return;
    }
    const auto &neighbor = *neighborOpt;

    m_logger->info("UE[%ld]: sendHandoverRequired - resolved target neighbor NCI=%ld -> NCGI(plmn=%03d-%02d nci=0x%09lx gnbId=%u cellId=%d "
                   "tac=%d interface=%s",
                   ueId,
                   targetNci,
                   neighbor.plmn.mcc,
                   neighbor.plmn.mnc,
                   neighbor.getNrCellIdentity(),
                   neighbor.getGnbId(),
                   neighbor.getCellId(),
                   neighbor.tac,
                   neighbor.handoverInterface == EHandoverInterface::N2 ? "N2" : "Xn");

    std::vector<ASN_NGAP_ProtocolIE_Field_13561P101 *> ies;

    // IE: HandoverType = intra5gs
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P101>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_HandoverType;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P101__value_PR_HandoverType;
        ie->value.choice.HandoverType = ASN_NGAP_HandoverType_intra5gs;
        ies.push_back(ie);
    }

    // IE: Cause
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P101>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_Cause;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P101__value_PR_Cause;
        ngap_utils::ToCauseAsn_Ref(cause, ie->value.choice.Cause);
        ies.push_back(ie);
    }

    // IE: TargetID
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P101>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_TargetID;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P101__value_PR_TargetID;

        ie->value.choice.TargetID.present = ASN_NGAP_TargetID_PR_targetRANNodeID;
        auto *targetRanNode = asn::New<ASN_NGAP_TargetRANNodeID>();

        // GlobalRANNodeID → GlobalGNB-ID
        auto *globalGnbId = asn::New<ASN_NGAP_GlobalGNB_ID>();
        asn::SetOctetString3(globalGnbId->pLMNIdentity,
                             ngap_utils::PlmnToOctet3(neighbor.plmn));
        globalGnbId->gNB_ID.present = ASN_NGAP_GNB_ID_PR_gNB_ID;

        auto gnbIdLength = neighbor.idLength;
        auto bitsToShift = 32 - gnbIdLength;

        asn::SetBitString(globalGnbId->gNB_ID.choice.gNB_ID,
                  octet4{neighbor.getGnbId() << bitsToShift},
                          static_cast<size_t>(gnbIdLength));

        targetRanNode->globalRANNodeID.present = ASN_NGAP_GlobalRANNodeID_PR_globalGNB_ID;
        targetRanNode->globalRANNodeID.choice.globalGNB_ID = globalGnbId;

        // selectedTAI
        asn::SetOctetString3(targetRanNode->selectedTAI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(neighbor.plmn));
        asn::SetOctetString3(targetRanNode->selectedTAI.tAC, octet3{neighbor.tac});

        ie->value.choice.TargetID.choice.targetRANNodeID = targetRanNode;
        ies.push_back(ie);
    }

    // IE: SourceToTarget-TransparentContainer
    // TODO: need to add SessionInformationList to enable DL Forwarding
    {
        auto sttc = makeSourceTargetNgranTransparentContainer(targetNci, neighbor.plmn, *rrcContainer);
        if (!sttc)
        {
            m_logger->err("UE[%ld]: sendHandoverRequired: failed to encode SourceToTarget-TransparentContainer", ueId);
            for (auto *ie : ies) asn::Free(asn_DEF_ASN_NGAP_ProtocolIE_Field_13561P101, ie);
            return;
        }
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P101>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_SourceToTarget_TransparentContainer;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present =
            ASN_NGAP_ProtocolIE_Field_13561P101__value_PR_SourceToTarget_TransparentContainer;
        asn::SetOctetString(ie->value.choice.SourceToTarget_TransparentContainer,
                            *sttc);
        ies.push_back(ie);

    }

    // IE: PDUSessionResourceListHORqd
    // include the list of PDU sessions
    {
        m_logger->info("UE[%ld] Building PDUSessionResourceListHORqd", ueId);
        OctetString hoRequiredTransfer = MakeHoRequiredTransfer();
        if (hoRequiredTransfer.length() == 0)
        {
            m_logger->err("sendHandoverRequired: failed to encode HandoverRequiredTransfer");
            for (auto *ie : ies) asn::Free(asn_DEF_ASN_NGAP_ProtocolIE_Field_13561P101, ie);
            return;
        }

        std::vector<int> pduSessionIds{};
        if (!ue->pduSessions.empty())
        {
            pduSessionIds.assign(ue->pduSessions.begin(), ue->pduSessions.end());
        }
        else
        {
            m_logger->warn("UE[%ld]: send Handoverrequired: no active PDU sessions found", ueId);
            for (auto *ie : ies) asn::Free(asn_DEF_ASN_NGAP_ProtocolIE_Field_13561P101, ie);
            return;
        }

        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P101>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceListHORqd;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P101__value_PR_PDUSessionResourceListHORqd;

        for (int psi : pduSessionIds)
        {
            auto *item = asn::New<ASN_NGAP_PDUSessionResourceItemHORqd>();
            item->pDUSessionID = static_cast<ASN_NGAP_PDUSessionID_t>(psi);
            asn::SetOctetString(item->handoverRequiredTransfer, hoRequiredTransfer);
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceListHORqd, item);
        }

        ies.push_back(ie);

        m_logger->info("UE[%ld]: HandoverRequired - including %d PDU session item(s) in IE",
                       ueId,
                       static_cast<int>(pduSessionIds.size()));
    }

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverRequired>(ies);
    sendNgapUeAssociated(ue->ctxId, pdu);

    // Store handover pending info in UE ctx.
    ue->handoverInProgress = true;
    ue->handoverTargetNci = targetNci;
    ue->handoverIsChoPreparation = hoForChoPreparation;

    m_logger->info("UE[%ld]: HandoverRequired sent to AMF (targetNCI=%ld mode=%s)",
                   ueId,
                   targetNci,
                   hoForChoPreparation ? "cho" : "basic");
}


/**
 * @brief Target GNB receives the HandoverRequest from the AMF to initiate the handover procedure
 * for a specific UE. The HandoverRequest includes the AMF-UE-NGAP-ID to identify the UE context, 
 * the HandoverType to indicate the type of handover (e.g., intra-5GS), a SourceToTarget-TransparentContainer
 * that carries the RRC container from the source gNB, which contains necessary information for 
 * the target gNB to prepare for the handover (e.g., the source cell ID, UE capabilities, etc.), 
 * and optionally a list of PDU sessions to be set up during the handover. Upon receiving the 
 * HandoverRequest, the target gNB should process the information, prepare UE context at the target cell
 * and respond with a HandoverRequestAcknowledge if the preparation is successful 
 * or a HandoverPreparationFailure if it fails.
 * 
 * @param amfId 
 * @param msg 
 * @param stream SCTP stream ID of the message
 */
void NgapTask::receiveHandoverRequest(int amfId, ASN_NGAP_HandoverRequest *msg, uint16_t stream)
{

    m_logger->info("HandoverRequest received from AMF[%d]", amfId);

    /* Error Checking */

    auto *amf = findAmfContext(amfId);
    if (!amf)
    {
        m_logger->err("receiveHandoverRequest: AMF %d context not found", amfId);
        return;
    }

    auto *ieAmfUeNgapId = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_AMF_UE_NGAP_ID);
    auto *ieType = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_HandoverType);
    auto *ieCause = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_Cause);
    auto *ieContainer =
        asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_SourceToTarget_TransparentContainer);
    auto *iePsList = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListHOReq);
    auto *ieAmbr = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_UEAggregateMaximumBitRate);
    auto *ieSecurityCapabilities = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_UESecurityCapabilities);
    auto *ieSecurityContext = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_SecurityContext);
    auto ieAllowedNssai = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_AllowedNSSAI);
    auto *ieGuami = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_GUAMI);

    // General NGAP error responses
    
    if (!ieAmfUeNgapId)
    {
        m_logger->err("receiveHandoverRequest: mandatory AMF_UE_NGAP_ID missing");
        sendErrorIndication(amfId, NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message);
        return;
    }

    int64_t amfUeNgapId = asn::GetSigned64(ieAmfUeNgapId->AMF_UE_NGAP_ID);
    if (amfUeNgapId <= 0)
    {
        m_logger->err("receiveHandoverRequest: invalid AMF_UE_NGAP_ID=%ld", amfUeNgapId);
        sendErrorIndication(amfId, NgapCause::Protocol_semantic_error, amfUeNgapId);
        return;
    }

    // Handover Failure responses for unsupported handover types or missing mandatory IEs
    // TODO:  Need to modify the sendHandoverFailure to deal with the lack of a UE context in these cases
    if (!ieType)
    {
        m_logger->err("receiveHandoverRequest: mandatory HandoverType missing");
        sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
        return;
    }

    if (ieType->HandoverType != ASN_NGAP_HandoverType_intra5gs)
    {
        m_logger->warn("receiveHandoverRequest: unsupported HandoverType=%d", (int)ieType->HandoverType);
        sendHandoverFailure(NgapCause::Protocol_message_not_compatible_with_receiver_state, amfUeNgapId);
        return;
    }

    if (!ieContainer)
    {
        m_logger->err("receiveHandoverRequest: mandatory SourceToTarget_TransparentContainer missing");
        sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
        return;
    }
    if (!iePsList)
    {
        m_logger->err("receiveHandoverRequest: mandatory PDUSessionResourceSetupListHOReq missing");
        sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
        return;
    }
    if (!ieAmbr)
    {
        m_logger->err("receiveHandoverRequest: mandatory UEAggregateMaximumBitRate missing");
        sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
        return;
    }
    if (!ieSecurityCapabilities)
    {
        m_logger->err("receiveHandoverRequest: mandatory UESecurityCapabilities missing");
        sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
        return;
    }
    if (!ieSecurityContext)
    {
        m_logger->err("receiveHandoverRequest: mandatory SecurityContext missing");
        sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
        return;
    }
    if (!ieAllowedNssai)
    {
        m_logger->err("receiveHandoverRequest: mandatory AllowedNSSAI missing");
        sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
        return;
    }
    if (!ieGuami)
    {
        m_logger->err("receiveHandoverRequest: mandatory GUAMI missing");
        sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
        return;
    }

    // pull next NgapTransactionId
    uint32_t transactionId = m_transactionIdCounter++;

    int hoType = static_cast<int>(ieType->HandoverType);
    size_t sourceToTargetSize = ieContainer->SourceToTarget_TransparentContainer.size;
    int requestedPsCount = iePsList->PDUSessionResourceSetupListHOReq.list.count;

    m_logger->info("HandoverRequest NGTxId[%d] from AMF[%d] contents: AMF-UE-NGAP-ID=%ld hoType=%d "
                   "s2tContainer=%zuB pduSessionCount=%d", transactionId,
                   amfId, amfUeNgapId, hoType, sourceToTargetSize, requestedPsCount);

    // Peel off the NGAP layer of the source-to-target container.  The AMF relays the source's
    // SourceToTarget-TransparentContainer verbatim; it is an APER-encoded
    // SourceNGRANNode-ToTargetNGRANNode-TransparentContainer, an NGAP IE that NGAP owns and so
    // decodes here.  Its rRCContainer field is an opaque byte string belonging to the peer
    // gNB's RRC layer: it is passed to the RRC task untouched and is never inspected here.
    // (Handing on the whole APER container instead left the target unable to recover the
    // source's RRC context.)
    OctetString sourceRrcContext{};
    {
        auto *s2tContainer = ngap_encode::Decode<ASN_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer>(
            asn_DEF_ASN_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer,
            ieContainer->SourceToTarget_TransparentContainer);

        if (s2tContainer == nullptr)
        {
            m_logger->err("receiveHandoverRequest: NGTxId[%d]: cannot decode "
                          "SourceNGRANNode-ToTargetNGRANNode-TransparentContainer (%zuB)",
                          transactionId, sourceToTargetSize);
            sendHandoverFailure(NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message, amfUeNgapId);
            return;
        }

        sourceRrcContext = asn::GetOctetString(s2tContainer->rRCContainer);
        asn::Free(asn_DEF_ASN_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer, s2tContainer);
    }

    if (sourceRrcContext.length() == 0)
    {
        m_logger->err("receiveHandoverRequest: NGTxId[%d]: source-to-target container carries an empty rRCContainer",
                      transactionId);
        sendHandoverFailure(NgapCause::Protocol_semantic_error, amfUeNgapId);
        return;
    }

    // NGAP has no CHO IE of its own in Release 18, so the conditional-handover indication is
    // always false: conditional handover is not supported on the N2 path, and every N2
    // HandoverRequest is prepared here as a classic handover.  The peer RRC's container is
    // not an alternative source for this - it is not NGAP's to open.
    bool isCho = false;

    /* create a new provisional NGAP UE context */

    auto ue = std::make_unique<NgapUeContext>(0);
    ue->associatedAmfId = amfId;
    ue->amfUeNgapId = amfUeNgapId;
    ue->ranUeNgapId = 0;
    ue->uplinkStream = stream;
    ue->downlinkStream = stream;

    // Extract AMBR from HandoverRequest
    if (auto *ie = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_UEAggregateMaximumBitRate))
    {
        ue->ueAmbr.dlAmbr = asn::GetUnsigned64(ie->UEAggregateMaximumBitRate.uEAggregateMaximumBitRateDL) / 8ull;
        ue->ueAmbr.ulAmbr = asn::GetUnsigned64(ie->UEAggregateMaximumBitRate.uEAggregateMaximumBitRateUL) / 8ull;
    }

    // Extract UE Security Capabilities from HandoverRequest
    if (auto *ie = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_UESecurityCapabilities))
    {
        ue->ueSecInfo.nRencryptionAlgorithmsBitmap =
            static_cast<uint16_t>(asn::GetOctetString(ie->UESecurityCapabilities.nRencryptionAlgorithms).get4UI(0));
        ue->ueSecInfo.eUTRAencryptionAlgorithmsBitmap =
            static_cast<uint16_t>(asn::GetOctetString(ie->UESecurityCapabilities.eUTRAencryptionAlgorithms).get4UI(0));
        ue->ueSecInfo.nRintegrityProtectionAlgorithmsBitmap =
            static_cast<uint16_t>(asn::GetOctetString(ie->UESecurityCapabilities.nRintegrityProtectionAlgorithms).get4UI(0));
        ue->ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap =
            static_cast<uint16_t>(asn::GetOctetString(ie->UESecurityCapabilities.eUTRAintegrityProtectionAlgorithms).get4UI(0));
    }

    // Create a list to store the PDU session resources for RRC message
    auto sessionList = std::make_unique<std::vector<PduSessionResource>>();

    // add session context items if provided
    if (iePsList && iePsList->PDUSessionResourceSetupListHOReq.list.count > 0)
    {
        for (int i = 0; i < iePsList->PDUSessionResourceSetupListHOReq.list.count; i++)
        {
            auto &item = iePsList->PDUSessionResourceSetupListHOReq.list.array[i];
            // Decode the PDU session resource setup request transfer
            auto *transfer = ngap_encode::Decode<ASN_NGAP_PDUSessionResourceSetupRequestTransfer>(
                asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, item->handoverRequestTransfer);
            if (transfer == nullptr)
            {
                m_logger->err("NGTxId[%d]: Unable to decode a PDU session resource setup request transfer. Ignoring the relevant item", transactionId);
                continue;
            }

            sessionList->emplace_back(ue->ctxId, static_cast<int>(item->pDUSessionID));
            sessionList->back().sNssai = ngap_utils::SnssaiFromAsn(item->s_NSSAI);
            makeNgapPduSessionItems(&sessionList->back(), transfer);

            asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, transfer);
        }
    }

    // add to NGAP pending handover map

    auto &hoPending = m_handoversPending[transactionId];
    hoPending.ctx = std::move(ue);
    hoPending.transactionId = transactionId;
    hoPending.amfId = amfId;
    hoPending.choCandidate = isCho;

    // Note: expireTime is different for Conditional Handover (CHO), which is expected to happen much later than a
    //  classical RSRP based handover
    hoPending.expireTime =
        utils::CurrentTimeMillis() + (isCho ? CHO_CANDIDATE_TIMEOUT_MS : HANDOVER_TIMEOUT_MS);

    if (hoPending.choCandidate)
    {
        m_logger->info("NGTxId[%d]: Target side candidate context stored for CHO (extended timeout=%dms)",
                       transactionId,
                       CHO_CANDIDATE_TIMEOUT_MS);
    }


    // send rrc container to RRC for processing

    auto rrcMsg = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::HANDOVER_REQUEST_RECEIVED);
    rrcMsg->rrcContainer = std::make_unique<OctetString>(std::move(sourceRrcContext));
    rrcMsg->ngapTxId = transactionId;
    rrcMsg->isCho = isCho;
    rrcMsg->sessionList = std::move(sessionList);
    m_base->rrcTask->push(std::move(rrcMsg));

    return;
}



/**
 * @brief Send a HandoverRequestAcknowledge message to the AMF
 * Called when a HANDOVER_REQUEST_ACK msg is received from the RRC task.
 * The message includes the list of admitted and failed PDU session resources, as well as the target RRC container.
 * Maps the transactionID to a pending handover object in the handoversPending map.
 * 
 * @param transactionId The transaction ID for this handover operation
 * @param ueId The UE identifier
 * @param admittedList The list of PDU session resources that were successfully admitted by RRC
 * @param failedList The list of PDU session resources that failed to be set up by RRC
 * @param targetRrcContainer The RRC container for the target side
 */
void NgapTask::sendHandoverRequestAcknowledge(uint32_t transactionId, int64_t ueId, 
    std::unique_ptr<std::vector<PduSessionResource>> rrcAdmittedSessions, 
    std::unique_ptr<std::vector<PduSessionResource>> rrcFailedSessions, 
    std::unique_ptr<OctetString> targetRrcContainer)
{

    // obtain handover pending object using the transaction ID
    auto it = m_handoversPending.find(transactionId);
    if (it == m_handoversPending.end())
    {
        m_logger->err("sendHandoverRequestAcknowledge: no pending handover found for transaction ID %u", transactionId);
        return;
    }
    auto &handover = it->second;

    auto *amf = findAmfContext(handover.amfId);
    if (!amf)
    {
        m_logger->err("sendHandoverRequestAcknowledge: AMF %d context not found", handover.amfId);
        return;
    }

    auto *ue = handover.ctx.get();
    if (!ue)
    {
        m_logger->err("sendHandoverRequestAcknowledge: provisional UE context not found for transaction ID %u", transactionId);
        return;
    }

    // Adopt the UE identity resolved by RRC from the source's transparent container.
    //  The provisional context was created in receiveHandoverRequest() with ctxId=0
    //  (the identity is unknown until RRC decodes the container).  Everything below
    //  keys off this value: the RAN-UE-NGAP-ID, the GTP UE context, the pending-map
    //  ueId used by sendNgapUeAssociated()'s fallback lookup, and the
    //  sendHandoverNotify() lookup that promotes the context on handover completion.
    handover.ueId = ueId;
    ue->ctxId = ueId;

    // assign RAN-UE-NGAP-ID
    ue->ranUeNgapId = ue->ctxId;

    /* User Plane Setup */

    // send msg to GTP to create new UE context.
    auto update = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_UPDATE);
    update->update = std::make_unique<GtpUeContextUpdate>(true, ue->ctxId, ue->ueAmbr);
    m_base->gtpTask->push(std::move(update));

    // send msgs to GTP to create new PDU session contexts for admitted sessions

    std::vector<ASN_NGAP_PDUSessionResourceAdmittedItem *> admittedList;
    std::vector<ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck *> failedList;

    if (rrcFailedSessions)
    {
        for (const auto &resource : *rrcFailedSessions)
        {
            OctetString encodedFail = MakeHoSetupUnsuccessfulTransfer(NgapCause::Protocol_semantic_error);
            auto *failed = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck>();
            failed->pDUSessionID = resource.psi;
            asn::SetOctetString(failed->handoverResourceAllocationUnsuccessfulTransfer, encodedFail);
            failedList.push_back(failed);

            m_logger->warn("UE[%ld] handover PDU session[%d]: setup failed at RRC", ue->ctxId, resource.psi);
        }
    }

    if (rrcAdmittedSessions) {
        for (auto &resource : *rrcAdmittedSessions)
        {
            // Stamp the adopted UE identity: the resources were built in
            //  receiveHandoverRequest() with the placeholder ueId=0, and GTP keys
            //  its session tree (and UE-context check) by resource.ueId.
            resource.ueId = ueId;

            // Send the session to GTP to setup. GTP is given its own copy, so this
            //  element of rrcAdmittedSessions can stay where it is.
            auto setupError = setupPduSessionResource(ue, resource);

            // check if the setup failed
            if (setupError.has_value())
            {
                m_logger->warn("UE[%ld] handover PDU session[%d]: setup failed cause=%d", ue->ctxId, resource.psi,
                            (int)setupError.value());

                OctetString encodedFail = MakeHoSetupUnsuccessfulTransfer(setupError.value());

                auto *failed = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck>();
                failed->pDUSessionID = resource.psi;
                asn::SetOctetString(failed->handoverResourceAllocationUnsuccessfulTransfer, encodedFail);
                failedList.push_back(failed);
            }
            else
            {
                // setupPduSessionResource() wrote the downlink tunnel (address/TEID) into
                //  `resource` itself, so the acknowledge transfer encodes it directly
                OctetString encodedAck = MakeHoAcknowledgeTransfer(resource);
                if (encodedAck.length() == 0)
                {
                    m_logger->warn("UE[%ld] handover PDU session %d: failed to encode acknowledge transfer", ue->ctxId,
                                resource.psi);

                    OctetString encodedFail = MakeHoSetupUnsuccessfulTransfer(NgapCause::Protocol_semantic_error);

                    auto *failed = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck>();
                    failed->pDUSessionID = resource.psi;
                    asn::SetOctetString(failed->handoverResourceAllocationUnsuccessfulTransfer, encodedFail);
                    failedList.push_back(failed);
                }
                else
                {
                    auto *admitted = asn::New<ASN_NGAP_PDUSessionResourceAdmittedItem>();
                    admitted->pDUSessionID = resource.psi;
                    asn::SetOctetString(admitted->handoverRequestAcknowledgeTransfer, encodedAck);
                    admittedList.push_back(admitted);

                    m_logger->debug("UE[%ld] handover PDU session[%d]: admitted teid=%u", ue->ctxId, resource.psi,
                                    resource.downTunnel.teid);
                }
            }

        }
    }

    if (admittedList.empty())
    {
        for (auto *item : failedList)
            asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceFailedToSetupItemHOAck, item);
        sendHandoverFailure(NgapCause::Misc_not_enough_user_plane_processing_resources, ue->amfUeNgapId);
        return;
    }

    std::vector<ASN_NGAP_ProtocolIE_Field_13561P105 *> ackIes;

    auto *admittedIe = asn::New<ASN_NGAP_ProtocolIE_Field_13561P105>();
    admittedIe->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceAdmittedList;
    admittedIe->criticality = ASN_NGAP_Criticality_ignore;
    admittedIe->value.present = ASN_NGAP_ProtocolIE_Field_13561P105__value_PR_PDUSessionResourceAdmittedList;
    for (auto *item : admittedList)
        asn::SequenceAdd(admittedIe->value.choice.PDUSessionResourceAdmittedList, item);
    ackIes.push_back(admittedIe);

    if (!failedList.empty())
    {
        auto *failedIe = asn::New<ASN_NGAP_ProtocolIE_Field_13561P105>();
        failedIe->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceFailedToSetupListHOAck;
        failedIe->criticality = ASN_NGAP_Criticality_ignore;
        failedIe->value.present =
            ASN_NGAP_ProtocolIE_Field_13561P105__value_PR_PDUSessionResourceFailedToSetupListHOAck;
        for (auto *item : failedList)
            asn::SequenceAdd(failedIe->value.choice.PDUSessionResourceFailedToSetupListHOAck, item);
        ackIes.push_back(failedIe);
    }


    /* Send Handover Request Acknowledge to AMF*/

    auto *t2sIe = asn::New<ASN_NGAP_ProtocolIE_Field_13561P105>();
    t2sIe->id = ASN_NGAP_ProtocolIE_ID_id_TargetToSource_TransparentContainer;
    t2sIe->criticality = ASN_NGAP_Criticality_reject;
    t2sIe->value.present =
        ASN_NGAP_ProtocolIE_Field_13561P105__value_PR_TargetToSource_TransparentContainer;
    asn::SetOctetString(t2sIe->value.choice.TargetToSource_TransparentContainer, *targetRrcContainer);
    ackIes.push_back(t2sIe);

    auto *ackPdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverRequestAcknowledge>(ackIes);
    sendNgapUeAssociated(ue->ctxId, ackPdu);

    m_logger->info("UE[%ld] HandoverRequestAcknowledge sent admitted=%d failed=%d", ue->ctxId,
                   (int)admittedList.size(), (int)failedList.size());
}


/**
 * @brief AMF sends a HandoverCommand to the source gNB to instruct it to proceed with the handover 
 * for the specified UE.  The HandoverCommand includes a TargetToSource-TransparentContainer that 
 * carries the RRC container from the Target gNB, which contains necessary information for the 
 * UE to connect to the target gNB (e.g., the C-RNTI).
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receiveHandoverCommand(int amfId, ASN_NGAP_HandoverCommand *msg)
{
    m_logger->info("HandoverCommand received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receiveHandoverCommand: UE not found");
        return;
    }

    // check for handover in process in the UE's context
    if (!ue->handoverInProgress)
    {
        m_logger->err("UE[%ld]:receiveHandoverCommand: UE has no pending NGAP handovers. Ignoring command.", ue->ctxId);
        return;
    }

    // get target NCI from context
    int64_t targetNci = ue->handoverTargetNci;

    // Extract the HandoverType IE
    auto *ieHandoverType = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_HandoverType);
    if (!ieHandoverType)
    {
        m_logger->err("UE[%ld]: receiveHandoverCommand - missing HandoverType IE", ue->ctxId);
        return;
    }
    if (ieHandoverType->HandoverType != ASN_NGAP_HandoverType_intra5gs)
    {
        m_logger->warn("UE[%ld]: receiveHandoverCommand - unsupported HandoverType=%d", ue->ctxId, (int)ieHandoverType->HandoverType);
        return;
    }

    // Extract TargetToSource-TransparentContainer (NG-RAN version)
    auto *ieContainer = asn::ngap::GetProtocolIe(msg,
        ASN_NGAP_ProtocolIE_ID_id_TargetToSource_TransparentContainer);

    std::unique_ptr<OctetString> rrcContainer;
    if (ieContainer)
    {
        // Decode the tstc as an NGRAN TSTC
        auto *tstc = ngap_encode::Decode<ASN_NGAP_TargetNGRANNode_ToSourceNGRANNode_TransparentContainer>(
            asn_DEF_ASN_NGAP_TargetNGRANNode_ToSourceNGRANNode_TransparentContainer, ieContainer->TargetToSource_TransparentContainer);
        
        if (tstc == nullptr)
        {
            m_logger->err("UE[%ld]: receiveHandoverCommand - failed to decode TargetToSource_TransparentContainer as NGRAN TSTC", ue->ctxId);
            return;
        }

        // extract the rrcContainer from the tstc
        rrcContainer = std::make_unique<OctetString>(asn::GetOctetString(tstc->rRCContainer));
        asn::Free(asn_DEF_ASN_NGAP_TargetNGRANNode_ToSourceNGRANNode_TransparentContainer, tstc);
    }
    else {
        m_logger->err("UE[%ld]: receiveHandoverCommand - missing TargetToSource_TransparentContainer", ue->ctxId);
        return;
    }

    // Here we would find an IE in the message that indicates this is a response to a CHO preparation
    //  Since NGAP does not support CHO, we'll need a custom IE or report from the RRC task that this is CHO-related

    // As a workaround, we store the isCHO flag in teh context and map this response to the original request

    bool isCho = ue->handoverIsChoPreparation;

    // Parse accepted PDU sessions: extract DL forwarding tunnel per session from HandoverCommandTransfer
    auto *ieHoList = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceHandoverList);
    if (ieHoList)
    {
        auto &hoList = ieHoList->PDUSessionResourceHandoverList.list;
        for (int i = 0; i < hoList.count; i++)
        {
            auto *item = hoList.array[i];
            if (!item)
                continue;

            auto *transfer = ngap_encode::Decode<ASN_NGAP_HandoverCommandTransfer>(
                asn_DEF_ASN_NGAP_HandoverCommandTransfer, item->handoverCommandTransfer);
            if (!transfer)
            {
                m_logger->warn("UE[%ld]: receiveHandoverCommand - failed to decode HandoverCommandTransfer for PDU session %ld",
                               ue->ctxId, (long)item->pDUSessionID);
                continue;
            }

            if (transfer->dLForwardingUP_TNLInformation &&
                transfer->dLForwardingUP_TNLInformation->present == ASN_NGAP_UPTransportLayerInformation_PR_gTPTunnel)
            {
                auto *gtpTunnel = transfer->dLForwardingUP_TNLInformation->choice.gTPTunnel;

                GtpTunnel fwdTunnel;
                fwdTunnel.teid    = (uint32_t)asn::GetOctet4(gtpTunnel->gTP_TEID);
                fwdTunnel.address = asn::GetOctetString(gtpTunnel->transportLayerAddress);

                m_logger->info("UE[%ld]: receiveHandoverCommand - PDU session %ld DL forwarding tunnel: addr=%s teid=0x%08x",
                               ue->ctxId, (long)item->pDUSessionID,
                               utils::OctetStringToIp(fwdTunnel.address).c_str(), fwdTunnel.teid);

                auto gm = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::FORWARDING_TUNNEL_SETUP);
                gm->ueId             = ue->ctxId;
                gm->psi              = (int)item->pDUSessionID;
                gm->forwardingTunnel = std::move(fwdTunnel);
                m_base->gtpTask->push(std::move(gm));
            }
            else
            {
                m_logger->info("UE[%ld]: receiveHandoverCommand - PDU session %ld: no DL forwarding tunnel",
                               ue->ctxId, (long)item->pDUSessionID);
            }

            asn::Free(asn_DEF_ASN_NGAP_HandoverCommandTransfer, transfer);
        }
    }
    else
    {
        m_logger->info("UE[%ld]: receiveHandoverCommand - no PDUSessionResourceHandoverList in message", ue->ctxId);
    }

    // Parse PDU sessions the AMF wants released at the source
    auto *ieRelList = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceToReleaseListHOCmd);
    if (ieRelList)
    {
        auto &relList = ieRelList->PDUSessionResourceToReleaseListHOCmd.list;
        for (int i = 0; i < relList.count; i++)
        {
            auto *item = relList.array[i];
            if (item)
                m_logger->info("UE[%ld]: receiveHandoverCommand - PDU session %ld flagged for release",
                               ue->ctxId, (long)item->pDUSessionID);
        }
    }

    // Forward to RRC task

    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::HANDOVER_COMMAND_RECEIVED);
    w->ueId = ue->ctxId;
    w->rrcContainer = std::move(rrcContainer);
    w->hoTargetNci = targetNci;
    w->isCho = isCho;
    m_base->rrcTask->push(std::move(w));

    m_logger->info("UE[%ld]: HandoverCommandReceived forwarded to RRC targetNCI=%ld mode=%s",
                   ue->ctxId,
                   targetNci,
                   isCho ? "cho" : "basic");
}

/**
 * @brief AMF sends a HandoverPreparationFailure to the source gNB to indicate that the 
 * handover preparation has failed.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receiveHandoverPreparationFailure(int amfId, ASN_NGAP_HandoverPreparationFailure *msg)
{
    // FIX - need to figure out how to determine which targetNCI this message came from
    int hoTargetNci = -1;
    m_logger->warn("HandoverPreparationFailure received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));

    if (!ue)
    {
        m_logger->err("receiveHandoverPreparationFailure: UE not found");
        return;
    }

    // remove from pending CHO list if present, so that subsequent handover attempts to the same target can be properly correlated.
    
    auto itByUe = m_hoReqChoPendingByTargetNci.find(ue->ctxId);
    if (itByUe != m_hoReqChoPendingByTargetNci.end())
    {
        // targetNCIs are tracked as a map in the second value
        auto &targetMap = itByUe->second;
        // if the targetNCI is in the map, remove it
        if (targetMap.count(hoTargetNci) != 0)
        {
            targetMap.erase(hoTargetNci);

            // if the map is now empty, remove the UE entry as well
            if (targetMap.empty())
                m_hoReqChoPendingByTargetNci.erase(itByUe);
        }
        else
        {
            m_logger->warn("UE[%ld] HO prep failure cannot be target-correlated; keeping %zu pending target(s)",
                           ue->ctxId,
                           targetMap.size());
        }
    }

    // Extract cause for logging
    auto *ieCause = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_Cause);
    if (ieCause)
    {
        m_logger->warn("UE[%ld] Handover preparation failed cause present=%d",
                       ue->ctxId, ieCause->Cause.present);
    }

    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::HANDOVER_PREPARATION_FAILURE_RECEIVED);
    w->ueId = ue->ctxId;
    w->hoTargetNci = hoTargetNci;
    m_base->rrcTask->push(std::move(w));

    m_logger->warn("UE[%ld] Handover preparation failed. UE remains on source cell.", ue->ctxId);
}

/**
 * @brief target GNB sends a HandoverNotify to the AMF to indicate that the handover has 
 * been completed and the UE is now being served by the target gNB. This allows the AMF 
 * to update its context for the UE and notify the source gNB to release resources associated with the UE. 
 * Triggered by a message from the RRC task indicating that the UE has successfully connected to the target 
 * and completed the RRC handover procedure.
 * 
 * @param ueId 
 */
void NgapTask::sendHandoverNotify(int64_t ueId)
{

    m_logger->debug("UE[%ld]: sendHandoverNotify - HANDOVER_NOTIFY_SEND received from RRC", ueId);

    // move the UE context from handover pending map to ueCtx map
    // since the handover pending map is indexed by transaction ID, we need to find the corresponding entry for this UE ID
    auto it = std::find_if(m_handoversPending.begin(), m_handoversPending.end(),
                           [ueId](const auto &entry) { return entry.second.ctx && entry.second.ctx->ctxId == ueId; });
    if (it == m_handoversPending.end())
    {
        m_logger->warn("UE[%ld] Ignoring handover complete: no pending NGAP handover context", ueId);
        return;
    }

    m_logger->info("UE[%ld]: sendHandoverNotify - activating UE NGAP context, sending HandoverNotify", ueId);

    m_ueCtx[ueId] = it->second.ctx.release();
    m_handoversPending.erase(it);

    // sanity check - the UE context should now be in the main UE context map
    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("UE[%ld]: sendHandoverNotify - UE context activation failed", ueId);
        return;
    }

    std::vector<ASN_NGAP_ProtocolIE_Field_13561P107 *> ies;

    // IE: UserLocationInformation (required) - use the current cell's PLMN and TAC
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P107>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_UserLocationInformation;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P107__value_PR_UserLocationInformation;

        ie->value.choice.UserLocationInformation.present =
            ASN_NGAP_UserLocationInformation_PR_userLocationInformationNR;
        auto *nrLoc = asn::New<ASN_NGAP_UserLocationInformationNR>();

        // NR-CGI
        asn::SetOctetString3(nrLoc->nR_CGI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetBitStringLong<36>(m_base->config->nci, nrLoc->nR_CGI.nRCellIdentity);

        // TAI
        asn::SetOctetString3(nrLoc->tAI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetOctetString3(nrLoc->tAI.tAC, octet3{m_base->config->tac});

        ie->value.choice.UserLocationInformation.choice.userLocationInformationNR = nrLoc;
        ies.push_back(ie);
    }

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverNotify>(ies);
    sendNgapUeAssociated(ue->ctxId, pdu);

    m_logger->info("UE[%ld]: HandoverNotify sent to AMF", ueId);
}


// Creates the SourceToTarget-TransparentContainer used for Intra5GS handovers.
//
//   The provided RRC container is inserted into the rrcContainer IE.
//   The target NCI and PLMN are used to build the targetCell_ID IE.
//   The UEHistoryInformation IE is left empty in this implementation.
//
//   The target side undoes this in receiveHandoverRequest(): the rRCContainer IE is
//   extracted and handed to RRC untouched, since only the two gNBs understand it.
std::unique_ptr<OctetString> NgapTask::makeSourceTargetNgranTransparentContainer(int64_t targetNCI, const Plmn &targetPlmn, const OctetString &rrcContainer)
{
    auto *container = asn::New<ASN_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer>();

    // rRCContainer (mandatory)
    asn::SetOctetString(container->rRCContainer, rrcContainer);

    // targetCell_ID (mandatory) — build NR-CGI from the neighbor entry
    container->targetCell_ID.present = ASN_NGAP_NGRAN_CGI_PR_nR_CGI;
    container->targetCell_ID.choice.nR_CGI = asn::New<ASN_NGAP_NR_CGI>();
    asn::SetOctetString3(container->targetCell_ID.choice.nR_CGI->pLMNIdentity,
                         ngap_utils::PlmnToOctet3(targetPlmn));
    asn::SetBitStringLong<36>(targetNCI, container->targetCell_ID.choice.nR_CGI->nRCellIdentity);

    // uEHistoryInformation (mandatory SEQUENCE-OF, empty list is valid)

    OctetString encoded = ngap_encode::EncodeS(
        asn_DEF_ASN_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer, container);
    asn::Free(asn_DEF_ASN_NGAP_SourceNGRANNode_ToTargetNGRANNode_TransparentContainer, container);

    if (encoded.length() == 0)
        return nullptr;

    return std::make_unique<OctetString>(std::move(encoded));
}


} // namespace nr::gnb
