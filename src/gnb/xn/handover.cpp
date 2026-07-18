#include "task.hpp"
#include "encode.hpp"

#include <gnb/ngap/utils.hpp>
#include <gnb/gtp/task.hpp>
#include <gnb/rrc/task.hpp>
#include <gnb/rls/task.hpp>
#include <gnb/ngap/task.hpp>
#include <gnb/gtp/utils.hpp>
#include <lib/asn/utils.hpp>
#include <utils/common.hpp>

#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestItem.h>
#include <asn/ngap/ASN_NGAP_QosCharacteristics.h>
#include <asn/ngap/ASN_NGAP_NonDynamic5QIDescriptor.h>
#include <asn/ngap/ASN_NGAP_AllocationAndRetentionPriority.h>

extern "C"
{
#include <asn_compare.h>
#include <compare.h>
#include <ANY.h>
#include <ASN_XNAP_XnAP-PDU.h>
#include <ASN_XNAP_InitiatingMessage.h>
#include <ASN_XNAP_ProtocolIE-Field.h>
#include <ASN_XNAP_ProtocolIE-Container.h>

// IE type headers for HandoverRequest and HandoverRequestAcknowledge
#include <ASN_XNAP_HandoverRequest.h>
#include <ASN_XNAP_HandoverRequestAcknowledge.h>
#include <ASN_XNAP_UEContextRelease.h>
#include <ASN_XNAP_SNStatusTransfer.h>
#include <ASN_XNAP_HandoverCancel.h>
#include <ASN_XNAP_ConditionalHandoverCancel.h>
#include <ASN_XNAP_HandoverSuccess.h>
#include <ASN_XNAP_HandoverPreparationFailure.h>
#include <ASN_XNAP_UnsuccessfulOutcome.h>
#include <ASN_XNAP_DRBsSubjectToStatusTransfer-List.h>
#include <ASN_XNAP_DRBsSubjectToStatusTransfer-Item.h>
#include <ASN_XNAP_DRBBStatusTransfer18bitsSN.h>
#include <ASN_XNAP_SuccessfulOutcome.h>
#include <ASN_XNAP_NG-RANnodeUEXnAPID.h>
#include <ASN_XNAP_Cause.h>
#include <ASN_XNAP_Target-CGI.h>
#include <ASN_XNAP_NR-CGI.h>
#include <ASN_XNAP_PLMN-Identity.h>
#include <ASN_XNAP_GUAMI.h>
#include <ASN_XNAP_UEContextInfoHORequest.h>
#include <ASN_XNAP_AMF-UE-NGAP-ID.h>
#include <ASN_XNAP_CPTransportLayerInformation.h>
#include <ASN_XNAP_TransportLayerAddress.h>
#include <ASN_XNAP_UESecurityCapabilities.h>
#include <ASN_XNAP_AS-SecurityInformation.h>
#include <ASN_XNAP_UEAggregateMaximumBitRate.h>
#include <ASN_XNAP_BitRate.h>
#include <ASN_XNAP_PDUSessionResourcesToBeSetup-List.h>
#include <ASN_XNAP_PDUSessionResourcesToBeSetup-Item.h>
#include <ASN_XNAP_PDUSessionAggregateMaximumBitRate.h>
#include <ASN_XNAP_S-NSSAI.h>
#include <ASN_XNAP_UPTransportLayerInformation.h>
#include <ASN_XNAP_GTPtunnelTransportLayerInformation.h>
#include <ASN_XNAP_GTP-TEID.h>
#include <ASN_XNAP_PDUSessionType.h>
#include <ASN_XNAP_QoSFlowsToBeSetup-List.h>
#include <ASN_XNAP_QoSFlowsToBeSetup-Item.h>
#include <ASN_XNAP_QoSFlowLevelQoSParameters.h>
#include <ASN_XNAP_QoSCharacteristics.h>
#include <ASN_XNAP_NonDynamic5QIDescriptor.h>
#include <ASN_XNAP_AllocationandRetentionPriority.h>
#include <ASN_XNAP_UEHistoryInformation.h>
#include <ASN_XNAP_LastVisitedCell-Item.h>
#include <ASN_XNAP_LastVisitedNGRANCellInformation.h>
#include <ASN_XNAP_CHOinformation-Req.h>
#include <ASN_XNAP_CHOtrigger.h>
#include <ASN_XNAP_CHO-Probability.h>
#include <ASN_XNAP_ProtocolExtensionContainer.h>
#include <ASN_XNAP_ProtocolExtensionField.h>
#include <ASN_XNAP_CHO-Maxnoof-CondReconfig.h>
#include <ASN_XNAP_CHOTimeBasedInformation.h>

// IE type headers for HandoverRequestAcknowledge
#include <ASN_XNAP_PDUSessionResourcesAdmitted-List.h>
#include <ASN_XNAP_PDUSessionResourcesAdmitted-Item.h>
#include <ASN_XNAP_PDUSessionResourceAdmittedInfo.h>
#include <ASN_XNAP_QoSFlowsAdmitted-List.h>
#include <ASN_XNAP_QoSFlowsAdmitted-Item.h>
#include <ASN_XNAP_DataForwardingInfoFromTargetNGRANnode.h>
#include <ASN_XNAP_DataforwardingandOffloadingInfofromSource.h>
#include <ASN_XNAP_QoSFLowsToBeForwarded-List.h>
#include <ASN_XNAP_QoSFLowsToBeForwarded-Item.h>
#include <ASN_XNAP_QoSFLowsAcceptedToBeForwarded-List.h>
#include <ASN_XNAP_QoSFLowsAcceptedToBeForwarded-Item.h>
#include <ASN_XNAP_PDUSessionResourcesNotAdmitted-List.h>
#include <ASN_XNAP_PDUSessionResourcesNotAdmitted-Item.h>
}

namespace nr::gnb
{

// -----------------------------------------------------------------------
// Procedure code for Handover Preparation (TS 38.423 Table 9.1-1)
// -----------------------------------------------------------------------
static constexpr long XN_PROC_HANDOVER_PREPARATION = 0;
static constexpr long XN_PROC_UE_CONTEXT_RELEASE = 6;
static constexpr long XN_PROC_SN_STATUS_TRANSFER = 1;
static constexpr long XNAP_IE_DRBS_SUBJECT_TO_STATUS_TRANSFER = 12;
static constexpr long XN_PROC_HANDOVER_CANCEL = 2;
static constexpr long XN_PROC_HANDOVER_SUCCESS = 29;
static constexpr long XN_PROC_CONDITIONAL_HANDOVER_CANCEL = 30;
static constexpr long XNAP_IE_REQUESTED_TARGET_CELL_GLOBAL_ID = 161;

// -----------------------------------------------------------------------
// IE IDs for HandoverRequest (TS 38.423 / XnAP-PDU-Contents ASN.1 rel-18)
// -----------------------------------------------------------------------
static constexpr long XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID = 73; // id-sourceNG-RANnodeUEXnAPID
static constexpr long XNAP_IE_Cause_HO                     = 7;  // id-Cause
static constexpr long XNAP_IE_targetCellGlobalID            = 78; // id-targetCellGlobalID
static constexpr long XNAP_IE_GUAMI                         = 15; // id-GUAMI
static constexpr long XNAP_IE_UEContextInfoHORequest        = 83; // id-UEContextInfoHORequest
static constexpr long XNAP_IE_UEHistoryInformation          = 88; // id-UEHistoryInformation
static constexpr long XNAP_IE_CHOinformation_Req            = 158; // id-CHOinformation-Req (TS 38.423 Table 9.1.3.1-1)
static constexpr long XNAP_EXT_CHOTimeBasedInformation      = 382; // id-CHOTimeBasedInformation (CHOinformation-Req-ExtIEs)
static constexpr long XNAP_EXT_CHO_Maxnoof_CondReconfig     = 443; // id-CHO-Maxnoof-CondReconfig (CHOinformation-Req-ExtIEs)

// -----------------------------------------------------------------------
// IE IDs for HandoverRequestAcknowledge (TS 38.423 / XnAP rel-18)
// -----------------------------------------------------------------------
static constexpr long XNAP_IE_targetNG_RAN_node_UE_XnAP_ID      = 79; // id-targetNG-RANnodeUEXnAPID
static constexpr long XNAP_IE_PDUSessionResourcesAdmitted_List  = 42; // id-PDUSessionResourcesAdmitted-List
static constexpr long XNAP_IE_PDUSessionResourcesNotAdmitted    = 43; // id-PDUSessionResourcesNotAdmitted-List
static constexpr long XNAP_IE_Target2SourceTranspContainer      = 77; // id-Target2SourceNG-RANnodeTranspContainer

// Helper: pack a typed ASN.1 value into the open ANY field of a ProtocolIE-Field via APER.
static bool setHoIeValue(ASN_XNAP_ProtocolIE_Field_14202P0_t *ie,
                         asn_TYPE_descriptor_t *td, void *val)
{
    return ANY_fromType_aper(&ie->value, td, val) == 0;
}

// Helper: build a 3-byte BCD-encoded PLMN octet string (same layout as NGAP).
static void setXnPlmn(ASN_XNAP_PLMN_Identity_t &dst, const Plmn &plmn)
{
    asn::SetOctetString3(dst, ngap_utils::PlmnToOctet3(plmn));
}

// Helper: build and send an RRC handover abort message to the RRC task.
static void sendRRCAbort(int64_t ueId, int64_t targetNci, bool isCho, 
    NmGnbXnToRrc::ABORT_REASON reason, TaskBase *base)
{
    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_ABORT);
    msg->ueId = ueId;
    msg->targetNci = targetNci;
    msg->isCho = isCho;
    msg->reason = reason;
    base->rrcTask->push(std::move(msg));

}

void XnTask::removeTargetPendingHandover(int txId)
{

    // get the pending handover by txId

    auto found = std::find_if(m_pendingHandoversTargetByTxId.begin(), m_pendingHandoversTargetByTxId.end(),
        [txId](const auto &pair) { return pair.second.xnTxId == txId; });


    if (found == m_pendingHandoversTargetByTxId.end())
    {
        m_logger->warn("No pending handover found for XnTxId=%d to remove", txId);
        return;
    }

    // reclaim SCTP stream ID for future use (streamId 0 means none was allocated)
    uint16_t streamId = found->second.streamId;
    if (streamId != 0)
    {
        m_logger->debug("Reclaiming SCTP streamId=%d for XnTxId=%d", streamId, txId);

        auto *sourcePeer = m_xnPeerTable.getPeerInfo(found->second.sourceGnbId);
        if (sourcePeer)
        {
            sourcePeer->streamIdManager.release(streamId);
        }
    }

    // remove the pending handover from map
    m_pendingHandoversTargetByTxId.erase(found);  

}

void XnTask::removeSourcePendingHandover(int64_t ueId, int targetGnbId)
{

    auto found = m_pendingHandoversSourceByUeId.find(std::make_pair(ueId, static_cast<int64_t>(targetGnbId)));

    if (found == m_pendingHandoversSourceByUeId.end())
    {
        m_logger->warn("No pending handover found for ueId=%ld and targetGnbId=%d to remove", ueId, targetGnbId);
        return;
    }

    // reclaim SCTP stream ID for future use (streamId 0 means none was allocated)
    uint16_t streamId = found->second.streamId;
    if (streamId != 0)
    {
        m_logger->debug("Reclaiming SCTP streamId=%d for ueId=%ld and targetGnbId=%d", streamId, ueId, targetGnbId);

        auto *targetPeer = m_xnPeerTable.getPeerInfo(targetGnbId);
        if (targetPeer)
        {
            targetPeer->streamIdManager.release(streamId);
        }
    }

    // remove the pending handover from map
    m_pendingHandoversSourceByUeId.erase(found);

}

XnTask::SourcePendingMap::iterator XnTask::findSourcePendingByNci(int64_t ueId, int64_t targetNci)
{
    return std::find_if(m_pendingHandoversSourceByUeId.begin(), m_pendingHandoversSourceByUeId.end(),
        [ueId, targetNci](const auto &pair) { return pair.second.ueId == ueId && pair.second.targetNci == targetNci; });
}

/**
 * sendHandoverRequest (called from task.cpp - RrcToXn::HANDOVER_REQUEST_SEND)
 *
 * Builds and sends an XnAP HandoverRequest to the target gNB identified by
 * targetNci.  The target gNB must already be present in the Xn peer table
 * with an CONNECTED association state, otherwise the function logs an error
 * and returns without sending.
 *
 * @param ueId The UE identifier
 * @param targetNci The target gNB NCI
 * @param contexts The handover UE contexts
 * @param rrcContainer The RRC container for handover
 * @param choRequest The conditional handover request (nullptr if regular handover)
 */
void XnTask::sendHandoverRequest(int64_t ueId, int64_t targetNci, 
    std::unique_ptr<GnbHandoverUeContexts> contexts, 
    std::unique_ptr<OctetString> rrcContainer, 
    std::unique_ptr<GnbCondHandoverRequest> choRequest)
{
    bool isCho = choRequest != nullptr;
    m_logger->debug("xnHandoverRequestSource ueId=%ld targetNci=0x%09lx isCho=%s",
                    ueId, targetNci, isCho ? "true" : "false");

    // Find target peer in Xn peer table by NCI.

    XnPeerInfo *targetPeer = nullptr;
    for (auto &peer : m_xnPeerTable.getAllPeers())
    {
        if (peer.nci == targetNci)
        {
            targetPeer = &peer;
            break;
        }
    }

    if (targetPeer == nullptr)
    {
        // TODO: need to handle this failure better.  RRC should be notified that the handover cannot proceed, 
        //   so it can either retry or abandon and release reserved resources.  For now, just log an error and return.
        m_logger->err("xnHandoverRequestSource: no Xn peer for targetNci=0x%09lx ueId=%ld",
                      targetNci, ueId);

        // send message to RRC layer to report failure
        sendRRCAbort(ueId, targetNci, isCho, NmGnbXnToRrc::ABORT_REASON_XN_FAILURE, m_base);
        return;
    }

    if (targetPeer->connectionState != EXnConnectionState::CONNECTED)
    {
        m_logger->err("xnHandoverRequestSource: peer gnbId=%d (targetNci=0x%09lx) not CONNECTED "
                      "(state=%d), dropping HandoverRequest for ueId=%ld",
                      targetPeer->gnbId, targetNci,
                      static_cast<int>(targetPeer->connectionState), ueId);
        sendRRCAbort(ueId, targetNci, isCho, NmGnbXnToRrc::ABORT_REASON_XN_FAILURE, m_base);
        return;
    }

    // Retrieve UE context from NGAP, GTP, and RRC tasks.
    //   In prior versions this section retreived contexts from the tasks directly.
    //   Now we pass them in as parameters to ensure they are present, so this section just
    //   assigns each context to a local pointer.

    auto *ngapUe = &*contexts->ngapUeContext;
    auto *rrcUe = &*contexts->rrcUeContext;
    auto *gtpUe = &*contexts->gtpUeContext;
    auto &pduSessions = contexts->pduSessions;

    const GnbConfig *cfg = m_base->config;


    // -----------------------------------------------------------------------
    // IE 1 — sourceNG-RANnodeUEXnAPID  (id=73, mandatory, criticality=reject)
    //   Opaque 32-bit UE identifier at the source gNB on the Xn interface.
    //   For the simulation, we use the full 64-bit UE ID to simplify tracking across 
    //   multiple gNBs (the IE allows for long values).  This is not standard-compliant, 
    //   but it is convenient for the simulation (avoids lookup operations).
    // -----------------------------------------------------------------------
    ASN_XNAP_NG_RANnodeUEXnAPID_t srcUeXnId =
        static_cast<ASN_XNAP_NG_RANnodeUEXnAPID_t>(ueId);

    // Ownership note: every top-level allocation below is held in an asn::Unique
    // guard, so any early return frees everything built so far.  Ownership is
    // transferred with .release() only at the point a struct is linked into its
    // parent (SequenceAdd / choice assignment).

    auto ieSrcUeId = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                     asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieSrcUeId->id          = XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID;
    ieSrcUeId->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieSrcUeId.get(), &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &srcUeXnId))
    {
        m_logger->err("UE[%ld]: xnHandoverRequestSource: failed to encode sourceNG-RANnodeUEXnAPID", ueId);
        return;
    }

    // -----------------------------------------------------------------------
    // IE 2 — Cause  (id=7, mandatory, criticality=reject)
    //   Assumption: use handover-desirable-for-radio-reason for normal handover
    //   and xn-handover-triggered for CHO.  These are the most typical causes
    //   for Xn-based handover per TS 38.423 Section 9.2.3.
    // -----------------------------------------------------------------------
    ASN_XNAP_Cause_t cause{};
    cause.present = ASN_XNAP_Cause_PR_radioNetwork;
    // Assumption: CHO uses cho_cpc_resources_tobechanged (55) as the closest available
    // XnAP cause for conditional handover initiation.  Non-CHO uses the standard
    // handover_desirable_for_radio_reasons (1) cause value.
    cause.choice.radioNetwork = isCho
        ? ASN_XNAP_CauseRadioNetworkLayer_cho_cpc_resources_tobechanged
        : ASN_XNAP_CauseRadioNetworkLayer_handover_desirable_for_radio_reasons;

    auto ieCause = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                   asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieCause->id          = XNAP_IE_Cause_HO;
    ieCause->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieCause.get(), &asn_DEF_ASN_XNAP_Cause, &cause))
    {
        m_logger->err("UE[%ld]: xnHandoverRequestSource: failed to encode Cause", ueId);
        return;
    }

    // -----------------------------------------------------------------------
    // IE 3 — targetCellGlobalID  (id=78, mandatory, criticality=reject)
    //   Build an NR-CGI for the target cell from the 36-bit targetNci and
    //   the PLMN learned from the Xn peer's setup exchange.
    //   Assumption: use the first PLMN in targetPeer->plmnList.  If the list is
    //   empty (peer table not fully populated), fall back to the local cfg->plmn.
    // -----------------------------------------------------------------------
    Plmn targetPlmn = (!targetPeer->plmnList.empty()) ? targetPeer->plmnList[0] : cfg->plmn;

    auto targetCgi = asn::WrapUnique(asn::New<ASN_XNAP_Target_CGI_t>(),
                                     asn_DEF_ASN_XNAP_Target_CGI);
    targetCgi->present    = ASN_XNAP_Target_CGI_PR_nr;
    targetCgi->choice.nr  = asn::New<ASN_XNAP_NR_CGI_t>();
    setXnPlmn(targetCgi->choice.nr->plmn_id, targetPlmn);
    asn::SetBitStringLong<36>(targetNci, targetCgi->choice.nr->nr_CI);

    auto ieTgtCell = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                     asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieTgtCell->id          = XNAP_IE_targetCellGlobalID;
    ieTgtCell->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieTgtCell.get(), &asn_DEF_ASN_XNAP_Target_CGI, targetCgi.get()))
    {
        m_logger->err("UE[%ld]: xnHandoverRequestSource: failed to encode targetCellGlobalID", ueId);
        return;
    }
    targetCgi.reset();

    // -----------------------------------------------------------------------
    // IE 4 — GUAMI  (id=15, mandatory, criticality=reject)
    //   Identifies the AMF that manages NAS for this UE so the target can route
    //   path-switch requests to the same AMF.
    //  Pull from UE's RRC Context
    // -----------------------------------------------------------------------
    auto guami = asn::WrapUnique(asn::New<ASN_XNAP_GUAMI_t>(), asn_DEF_ASN_XNAP_GUAMI);
    const Guami &g = rrcUe->guami;

    setXnPlmn(guami->plmn_ID, g.plmn);
    asn::SetBitStringInt<8> (g.amfRegionId, guami->amf_region_id);
    asn::SetBitStringInt<10>(g.amfSetId,    guami->amf_set_id);
    asn::SetBitStringInt<6> (g.amfPointer,  guami->amf_pointer);

    auto ieGuami = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                   asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieGuami->id          = XNAP_IE_GUAMI;
    ieGuami->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieGuami.get(), &asn_DEF_ASN_XNAP_GUAMI, guami.get()))
    {
        m_logger->err("xnHandoverRequestSource: failed to encode GUAMI");
        sendRRCAbort(ueId, targetNci, isCho, NmGnbXnToRrc::ABORT_REASON_XN_FAILURE, m_base);
        return;
    }
    guami.reset();

    // -----------------------------------------------------------------------
    // IE 5 — UEContextInfoHORequest  (id=83, mandatory, criticality=reject)
    //   The nested structure carrying the UE's session context to the target.
    // -----------------------------------------------------------------------
    auto ueCtxInfo = asn::WrapUnique(asn::New<ASN_XNAP_UEContextInfoHORequest_t>(),
                                     asn_DEF_ASN_XNAP_UEContextInfoHORequest);

    // --- 5a. ng-c-UE-reference: AMF-UE-NGAP-ID ---
    // The AMF-assigned identifier for this UE.  The target uses it to correlate
    // the incoming path-switch request after the UE connects.
    //  Pull from NGAP context
    // AMF_UE_NGAP_ID_t is INTEGER_t (opaque BER struct) — must use asn_int642INTEGER.
    {
        int64_t amfId = ngapUe->amfUeNgapId;
        asn_int642INTEGER(&ueCtxInfo->ng_c_UE_reference, amfId);
    }

    // --- 5b. cp-TNL-info-source: control-plane transport layer address ---
    // The IP address of the source gNB's N2 (AMF-side) interface, so the target
    // gNB can update the AMF with the new RAN endpoint via Path Switch.
    // Pulled from source gNB configuration.
    {
        ueCtxInfo->cp_TNL_info_source.present =
            ASN_XNAP_CPTransportLayerInformation_PR_endpointIPAddress;
        auto &epAddr = ueCtxInfo->cp_TNL_info_source.choice.endpointIPAddress;

        // Parse the ngapIp string into a 4-byte big-endian representation.
        uint32_t ipBe = 0;
        {
            unsigned a, b, c, d;
            if (sscanf(cfg->ngapIp.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
                ipBe = (a << 24) | (b << 16) | (c << 8) | d;
            // Assumption: IPv4 only; IPv6 addresses need 128-bit BIT_STRING.
        }
        asn::SetBitString(epAddr, octet4{ipBe}, 32);
    }

    // --- 5c. ueSecurityCapabilities ---
    // Pulled from RRC context
    {
        auto &sc = ueCtxInfo->ueSecurityCapabilities;
        if (rrcUe->ueSecurityInfoValid)
        {
            asn::SetBitStringInt<16>(rrcUe->ueSecInfo.nRencryptionAlgorithmsBitmap, sc.nr_EncyptionAlgorithms);
            asn::SetBitStringInt<16>(rrcUe->ueSecInfo.nRintegrityProtectionAlgorithmsBitmap, sc.nr_IntegrityProtectionAlgorithms);
            asn::SetBitStringInt<16>(rrcUe->ueSecInfo.eUTRAencryptionAlgorithmsBitmap, sc.e_utra_EncyptionAlgorithms);
            asn::SetBitStringInt<16>(rrcUe->ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap, sc.e_utra_IntegrityProtectionAlgorithms);
        }
    }

    // --- 5d. securityInformation ---
    // AS security context: KgNB* (256-bit key derived from current KgNB and NH)
    // and the Next-Hop Chaining Counter (NCC, 0-7).
    // Pull from RRC context
    {
        auto &si = ueCtxInfo->securityInformation;
        si.key_NG_RAN_Star.size = 32;
        si.key_NG_RAN_Star.buf  = static_cast<uint8_t *>(malloc(32));
        if (rrcUe->ueSecurityInfoValid)
            std::memcpy(si.key_NG_RAN_Star.buf, rrcUe->ueSecInfo.k_gnb.data(), 32);
        else
            std::memset(si.key_NG_RAN_Star.buf, 0, 32);
        si.key_NG_RAN_Star.bits_unused = 0;
        si.ncc = rrcUe->ueSecurityInfoValid ? rrcUe->nextHopChainingCount : 0;
    }

    // --- 5e. ue-AMBR ---
    // Aggregate maximum bit rate for the UE.
    // Pulled from GTP UE context AMBR
    {
        uint64_t dlAmbr = 0, ulAmbr = 0;
        dlAmbr = gtpUe->ueAmbr.dlAmbr;
        ulAmbr = gtpUe->ueAmbr.ulAmbr;

        // BitRate_t is a plain long — assign directly (no INTEGER_t boxing needed).
        ueCtxInfo->ue_AMBR.dl_UE_AMBR = static_cast<long>(dlAmbr);
        ueCtxInfo->ue_AMBR.ul_UE_AMBR = static_cast<long>(ulAmbr);
    }

    // --- 5f. pduSessionResourcesToBeSetup-List ---
    // Enumerate active PDU sessions from GTP PDU Sessions
    for (auto *res : pduSessions)
    {
        // A session with no QoS flows cannot be encoded (qosFlowsToBeSetup-List
        // has a lower bound of 1) — abort the whole request up front.
        if (res->qosFlows.empty())
        {
            m_logger->err("sendHandoverRequest: PSI=%d has no QoS flows; aborting", res->psi);
            sendRRCAbort(ueId, targetNci, isCho, NmGnbXnToRrc::ABORT_REASON_XN_FAILURE, m_base);
            return;
        }

        auto pduItem = asn::WrapUnique(asn::New<ASN_XNAP_PDUSessionResourcesToBeSetup_Item_t>(),
                                       asn_DEF_ASN_XNAP_PDUSessionResourcesToBeSetup_Item);

        // pdu Session ID
        pduItem->pduSessionId = static_cast<ASN_XNAP_PDUSession_ID_t>(res->psi);

        // S-NSSAI
        asn::SetOctetString1(pduItem->s_NSSAI.sst, res->sNssai.sst);
        if (res->sNssai.sd.has_value() )
        {
            pduItem->s_NSSAI.sd = asn::New<OCTET_STRING_t>();
            asn::SetOctetString3(*pduItem->s_NSSAI.sd, res->sNssai.sd.value());
        }

        // pduSession AMBR — from GTP PDU Session struct.
        {
            uint64_t dlAmbr = 0, ulAmbr = 0;
            dlAmbr = res->sessionAmbr.dlAmbr;
            ulAmbr = res->sessionAmbr.ulAmbr;

            auto *ambr = asn::New<ASN_XNAP_PDUSessionAggregateMaximumBitRate>();
            ambr->downlink_session_AMBR = static_cast<ASN_XNAP_BitRate_t>(dlAmbr);
            ambr->uplink_session_AMBR = static_cast<ASN_XNAP_BitRate_t>(ulAmbr);

            pduItem->pduSessionAMBR = ambr;
        }


        // UL GTP-U tunnel at UPF — from the GTP task's upTunnel record.
        {
            uint32_t upfIpBe = 0;
            if (res->upTunnel.address.length() >= 4)
            {
                const uint8_t *b = res->upTunnel.address.data();
                upfIpBe = (static_cast<uint32_t>(b[0]) << 24) |
                          (static_cast<uint32_t>(b[1]) << 16) |
                          (static_cast<uint32_t>(b[2]) << 8)  |
                          static_cast<uint32_t>(b[3]);
            }
            auto *gtpTunnel = asn::New<ASN_XNAP_GTPtunnelTransportLayerInformation_t>();
            asn::SetBitString(gtpTunnel->tnl_address, octet4{upfIpBe}, 32);
            asn::SetOctetString4(gtpTunnel->gtp_teid, octet4{res->upTunnel.teid});
            pduItem->uL_NG_U_TNLatUPF.present =
                ASN_XNAP_UPTransportLayerInformation_PR_gtpTunnel;
            pduItem->uL_NG_U_TNLatUPF.choice.gtpTunnel = gtpTunnel;
        }

        // Source-DL-NG-U-TNL-Information — optional transport info for the downlink path from source gNB to UPF.
        {
            uint32_t upfIpBe = 0;
            if (res->downTunnel.address.length() >= 4)
            {
                const uint8_t *b = res->downTunnel.address.data();
                upfIpBe = (static_cast<uint32_t>(b[0]) << 24) |
                          (static_cast<uint32_t>(b[1]) << 16) |
                          (static_cast<uint32_t>(b[2]) << 8)  |
                          static_cast<uint32_t>(b[3]);
            }
            auto *gtpTunnel = asn::New<ASN_XNAP_GTPtunnelTransportLayerInformation_t>();
            asn::SetBitString(gtpTunnel->tnl_address, octet4{upfIpBe}, 32);
            asn::SetOctetString4(gtpTunnel->gtp_teid, octet4{res->downTunnel.teid});

            auto *upTnlInfo = asn::New<ASN_XNAP_UPTransportLayerInformation_t>();
            upTnlInfo->present = ASN_XNAP_UPTransportLayerInformation_PR_gtpTunnel;
            upTnlInfo->choice.gtpTunnel = gtpTunnel;

            pduItem->source_DL_NG_U_TNL_Information = upTnlInfo;

        }

        // PDU session type — from GTP resource (set from NGAP SessionResourceSetup).
        {
            int xnType = 1;
            switch (res->sessionType)
            {
            case PduSessionType::IPv4:     xnType = 1; break;
            case PduSessionType::IPv6:     xnType = 2; break;
            case PduSessionType::IPv4v6:   xnType = 3; break;
            case PduSessionType::ETHERNET: xnType = 4; break;
            default:                       xnType = 6; break; // unstructured
            }
            pduItem->pduSessionType = static_cast<ASN_XNAP_PDUSessionType_t>(xnType);
        }

        // QoS flows — translate each NGAP QosFlowSetupRequestItem stored on the
        // resource into its XnAP equivalent.  For non-dynamic 5QI, the fiveQI
        // value is carried directly.  Dynamic 5QI falls back to 5QI=9 since XnAP
        // requires a NonDynamic5QIDescriptor for the non-dynamic path.


        // Data forwarding IE - add flows to the DataforwardingandOffloadingInfofromSource IE as well
        // qosFlowsToBeForwarded is an embedded list — add directly, no separate allocation needed.
        // Attach to pduItem immediately so pduItem owns it from the start.
        auto *dfInfo = asn::New<ASN_XNAP_DataforwardingandOffloadingInfofromSource>();
        pduItem->dataforwardinginfofromSource = dfInfo;

        for (const auto &flow : res->qosFlows)
        {
            auto *xnNonDyn = asn::New<ASN_XNAP_NonDynamic5QIDescriptor_t>();
            xnNonDyn->fiveQI = flow.fiveQi;

            auto *qosFlowItem = asn::New<ASN_XNAP_QoSFlowsToBeSetup_Item_t>();
            qosFlowItem->qfi = static_cast<long>(flow.qfi);
            qosFlowItem->qosFlowLevelQoSParameters.qos_characteristics.present
                = ASN_XNAP_QoSCharacteristics_PR_non_dynamic;
            qosFlowItem->qosFlowLevelQoSParameters.qos_characteristics.choice.non_dynamic
                = xnNonDyn;
            qosFlowItem->qosFlowLevelQoSParameters.allocationAndRetentionPrio.priorityLevel
                = flow.arpPriorityLevel;
            qosFlowItem->qosFlowLevelQoSParameters.allocationAndRetentionPrio.pre_emption_capability
                = flow.arpPreemptCapability;
            qosFlowItem->qosFlowLevelQoSParameters.allocationAndRetentionPrio.pre_emption_vulnerability
                = flow.arpPreemptVulnerability;

            asn::SequenceAdd(pduItem->qosFlowsToBeSetup_List, qosFlowItem);

            auto *dfQosItem = asn::New<ASN_XNAP_QoSFLowsToBeForwarded_Item_t>();
            dfQosItem->qosFlowIdentifier = qosFlowItem->qfi;
            dfQosItem->dl_dataforwarding = ASN_XNAP_DLForwarding_dl_forwarding_proposed;
            asn::SequenceAdd(dfInfo->qosFlowsToBeForwarded, dfQosItem);
        }

        asn::SequenceAdd(ueCtxInfo->pduSessionResourcesToBeSetup_List, pduItem.release());
    }

    // --- 5g. rrc-Context: RRC handover container ---
    // Opaque container for the UE's RRC context.  Provided as parameter by RRC task.
    {
        OCTET_STRING_fromBuf(&ueCtxInfo->rrc_Context,
                             reinterpret_cast<const char *>(rrcContainer->data()),
                             static_cast<int>(rrcContainer->length()));
    }

    // Wrap UEContextInfoHORequest as IE 5
    auto ieUeCtxInfo = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                       asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieUeCtxInfo->id          = XNAP_IE_UEContextInfoHORequest;
    ieUeCtxInfo->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieUeCtxInfo.get(), &asn_DEF_ASN_XNAP_UEContextInfoHORequest, ueCtxInfo.get()))
    {
        m_logger->err("xnHandoverRequestSource: failed to encode UEContextInfoHORequest");
        return;
    }
    ueCtxInfo.reset();

    // -----------------------------------------------------------------------
    // IE 6 — UEHistoryInformation  (id=88, mandatory, criticality=ignore)
    //   List of cells the UE has visited recently.  Provides context for the
    //   target to optimize radio configuration.
    //   Assumption: one entry for the current source cell (serving this gNB).
    //   The entry is an opaque OCTET STRING per TS 38.413 §9.3.3.10; a zero-
    //   byte dummy is used here as a placeholder.  A real implementation would
    //   serialize the LastVisitedNR-CellInformation structure defined in TS 38.413.
    // -----------------------------------------------------------------------
    auto *ueHistItem = asn::New<ASN_XNAP_LastVisitedCell_Item_t>();
    ueHistItem->present = ASN_XNAP_LastVisitedCell_Item_PR_nG_RAN_Cell;
    static const uint8_t dummyHistBuf = 0x00; // Assumption: opaque placeholder
    OCTET_STRING_fromBuf(&ueHistItem->choice.nG_RAN_Cell,
                         reinterpret_cast<const char *>(&dummyHistBuf), 1);

    auto ueHistInfo = asn::WrapUnique(asn::New<ASN_XNAP_UEHistoryInformation_t>(),
                                      asn_DEF_ASN_XNAP_UEHistoryInformation);
    asn::SequenceAdd(*ueHistInfo, ueHistItem);

    auto ieUeHist = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                    asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieUeHist->id          = XNAP_IE_UEHistoryInformation;
    ieUeHist->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieUeHist.get(), &asn_DEF_ASN_XNAP_UEHistoryInformation, ueHistInfo.get()))
    {
        m_logger->err("xnHandoverRequestSource: failed to encode UEHistoryInformation");
        return;
    }
    ueHistInfo.reset();

    // --- 5h. CHOinformation-Req IE (conditional: isCho only) ---
    asn::Unique<ASN_XNAP_ProtocolIE_Field_14202P0_t> ieCho{};
    if (isCho)
    {
        auto choReq = asn::WrapUnique(asn::New<ASN_XNAP_CHOinformation_Req_t>(),
                                      asn_DEF_ASN_XNAP_CHOinformation_Req);

        // CHO trigger
        // Can either be CHO-initiation or CHO-replace
        if (choRequest->choTrigger == 0)
             choReq->cho_trigger = ASN_XNAP_CHOtrigger_cho_initiation;
        else 
             choReq->cho_trigger = ASN_XNAP_CHOtrigger_cho_replace;

        // targetNG_RANnodeUEXnAPID
        //  Only include for CHO-replace and if the RRC has provided a target UE XnAP ID
        if (choRequest->choTrigger != 0 && choRequest->targetNGRANnodeUeXnapId > 0)
        {
            auto *targetUeId = asn::New<ASN_XNAP_NG_RANnodeUEXnAPID_t>();
            *targetUeId = static_cast<ASN_XNAP_NG_RANnodeUEXnAPID_t>(choRequest->targetNGRANnodeUeXnapId);
            choReq->targetNG_RANnodeUEXnAPID = targetUeId;
        }

        // estimated arrival probability (1-100 percent, constrained by TS 38.423)
        auto *prob = asn::New<ASN_XNAP_CHO_Probability_t>();
        *prob = static_cast<ASN_XNAP_CHO_Probability_t>(
            std::max(1, std::min(100, choRequest->choArrivalProbabilityPercent)));
        choReq->cHO_EstimatedArrivalProbability = prob;

        // --- Extension IEs (both optional) ---
        // Each extension field is a ProtocolExtensionField_14249P0 wrapper:
        //   id / criticality / extensionValue (APER-packed ANY).
        // On encoding failure the field is skipped (logged) rather than aborting
        // the whole IE, because both extensions are OPTIONAL per TS 38.423.

        auto extContainer = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolExtensionContainer_14246P0_t>(),
                                            asn_DEF_ASN_XNAP_ProtocolExtensionContainer_14246P0);
        bool extUsed = false;

        // Extension 1: CHO-Maxnoof-CondReconfig (id=443, criticality=reject)
        // Maximum number of conditional RRCReconfigurations to prepare.
        // Omit when choRequest->maxNumCondReconfigsToPrepare == 0.
        if (choRequest->maxNumCondReconfigsToPrepare > 0)
        {
            auto *extField = asn::New<ASN_XNAP_ProtocolExtensionField_14249P0_t>();
            extField->id          = XNAP_EXT_CHO_Maxnoof_CondReconfig;
            extField->criticality = ASN_XNAP_Criticality_reject;

            ASN_XNAP_CHO_Maxnoof_CondReconfig_t maxReconfig =
                static_cast<ASN_XNAP_CHO_Maxnoof_CondReconfig_t>(choRequest->maxNumCondReconfigsToPrepare);
            if (ANY_fromType_aper(&extField->extensionValue,
                                  &asn_DEF_ASN_XNAP_CHO_Maxnoof_CondReconfig,
                                  &maxReconfig) == 0)
            {
                asn::SequenceAdd(*extContainer, extField);
                extUsed = true;
            }
            else
            {
                m_logger->warn("xnHandoverRequestSource: failed to encode CHO-Maxnoof-CondReconfig; skipping extension");
                asn::Free(asn_DEF_ASN_XNAP_ProtocolExtensionField_14249P0, extField);
            }
        }

        // Extension 2: CHOTimeBasedInformation (id=382, criticality=reject)
        // T1 window start and duration for time-based CHO triggering.
        // Omit when choRequest->tbiProvided == false.
        if (choRequest->tbiProvided)
        {
            auto *extField = asn::New<ASN_XNAP_ProtocolExtensionField_14249P0_t>();
            extField->id          = XNAP_EXT_CHOTimeBasedInformation;
            extField->criticality = ASN_XNAP_Criticality_reject;

            ASN_XNAP_CHOTimeBasedInformation_t tbi{};
            // cHO_HOWindowStart is INTEGER_t (big-integer struct) — use asn_int642INTEGER
            asn_int642INTEGER(&tbi.cHO_HOWindowStart, choRequest->tbiWindowStart);
            tbi.cHO_HOWindowDuration = static_cast<ASN_XNAP_CHO_HandoverWindowDuration_t>(choRequest->tbiDuration);
            bool tbiOk = ANY_fromType_aper(&extField->extensionValue,
                                           &asn_DEF_ASN_XNAP_CHOTimeBasedInformation,
                                           &tbi) == 0;
            // cHO_HOWindowStart.buf was heap-allocated by asn_int642INTEGER — free it
            // regardless of encode success so the stack tbi does not leak.
            ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_ASN_XNAP_CHO_HandoverWindowStart,
                                          &tbi.cHO_HOWindowStart);
            if (tbiOk)
            {
                asn::SequenceAdd(*extContainer, extField);
                extUsed = true;
            }
            else
            {
                m_logger->warn("xnHandoverRequestSource: failed to encode CHOTimeBasedInformation; skipping extension");
                asn::Free(asn_DEF_ASN_XNAP_ProtocolExtensionField_14249P0, extField);
            }
        }

        if (extUsed)
            choReq->iE_Extensions = extContainer.release();

        ieCho = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
        ieCho->id          = XNAP_IE_CHOinformation_Req;
        ieCho->criticality = ASN_XNAP_Criticality_reject;
        if (!setHoIeValue(ieCho.get(), &asn_DEF_ASN_XNAP_CHOinformation_Req, choReq.get()))
        {
            m_logger->err("xnHandoverRequestSource: failed to encode CHOinformation-Req");
            return;
        }
    }



    // -----------------------------------------------------------------------
    // Assemble HandoverRequest ProtocolIE container and wrap in InitiatingMessage
    // -----------------------------------------------------------------------
    auto hoReq = asn::WrapUnique(asn::New<ASN_XNAP_HandoverRequest_t>(),
                                 asn_DEF_ASN_XNAP_HandoverRequest);
    asn::SequenceAdd(hoReq->protocolIEs, ieSrcUeId.release());
    asn::SequenceAdd(hoReq->protocolIEs, ieCause.release());
    asn::SequenceAdd(hoReq->protocolIEs, ieTgtCell.release());
    asn::SequenceAdd(hoReq->protocolIEs, ieGuami.release());
    asn::SequenceAdd(hoReq->protocolIEs, ieUeCtxInfo.release());
    asn::SequenceAdd(hoReq->protocolIEs, ieUeHist.release());
    if (ieCho)
        asn::SequenceAdd(hoReq->protocolIEs, ieCho.release());

    auto initMsg = asn::WrapUnique(asn::New<ASN_XNAP_InitiatingMessage_t>(),
                                   asn_DEF_ASN_XNAP_InitiatingMessage);
    initMsg->procedureCode = XN_PROC_HANDOVER_PREPARATION;
    initMsg->criticality   = ASN_XNAP_Criticality_reject;
    if (ANY_fromType_aper(&initMsg->value, &asn_DEF_ASN_XNAP_HandoverRequest, hoReq.get()) != 0)
    {
        m_logger->err("xnHandoverRequestSource: failed to encode HandoverRequest into InitiatingMessage");
        sendRRCAbort(ueId, targetNci, isCho, NmGnbXnToRrc::ABORT_REASON_XN_FAILURE, m_base);
        return;
    }
    hoReq.reset();

    auto outerPdu = asn::WrapUnique(asn::New<ASN_XNAP_XnAP_PDU_t>(), asn_DEF_ASN_XNAP_XnAP_PDU);
    outerPdu->present                  = ASN_XNAP_XnAP_PDU_PR_initiatingMessage;
    outerPdu->choice.initiatingMessage = initMsg.release();

    // -----------------------------------------------------------------------
    // APER-encode
    // -----------------------------------------------------------------------
    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu.get(), encoded, buffer))
    {
        m_logger->err("xnHandoverRequestSource: APER encoding failed for ueId=%ld targetGnbId=%d",
                      ueId, targetPeer->gnbId);
        sendRRCAbort(ueId, targetNci, isCho, NmGnbXnToRrc::ABORT_REASON_XN_FAILURE, m_base);
        return;
    }
    outerPdu.reset();

    // -----------------------------------------------------------------------
    // Allocate a UE-associated SCTP stream for this handover from the target's
    // stream manager, then send via the SCTP task.
    //
    // Note - per standard, the stream ID should be reused for all messages associated with
    //   the handover, so we save the streamID in the handover state struct.
    // -----------------------------------------------------------------------
    auto streamId = targetPeer->streamIdManager.allocate();
    if (!streamId.has_value())
    {
        m_logger->err("xnHandoverRequestSource: no free SCTP stream for gnbId=%d ueId=%ld",
                      targetPeer->gnbId, ueId);
        delete[] buffer;
        sendRRCAbort(ueId, targetNci, isCho, NmGnbXnToRrc::ABORT_REASON_XN_FAILURE, m_base);
        return;
    }

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = targetPeer->clientId;
    sctpMsg->stream   = streamId.value();
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->xnSctpTask->push(std::move(sctpMsg));

    // Create the Xn pending handover state  

    XnPendingHandover outgoing{};
    outgoing.ueId = ueId;
    outgoing.sourceUeXnApId = ueId;
    outgoing.targetNci = targetNci;
    outgoing.targetGnbId = targetPeer->gnbId;
    outgoing.isCho = isCho;
    outgoing.role = XnPendingHandover::Role::SOURCE;
    outgoing.timestamp = static_cast<uint64_t>(utils::CurrentTimeMillis());
    outgoing.streamId = streamId.value();

    // One Xn UE association per UE per peer node: if a stale entry already exists
    // for this UE/target pair, release its resources before overwriting it.
    const auto pendingKey = std::make_pair(ueId, static_cast<int64_t>(targetPeer->gnbId));
    if (m_pendingHandoversSourceByUeId.count(pendingKey))
        removeSourcePendingHandover(ueId, targetPeer->gnbId);
    m_pendingHandoversSourceByUeId[pendingKey] = outgoing;

    m_logger->info("XnAP HandoverRequest sent to gnbId=%d (targetNci=0x%09lx) "
                   "for ueId=%ld on stream=%u isCho=%d",
                   targetPeer->gnbId, targetNci, ueId, streamId.value(), isCho);
}


/**
 * receiveHandoverRequest (called from SCTP msg handler)
 *
 * Handles an incoming XnAP HandoverRequest from a source gNB.  Validates the
 * PDU structure, decodes the HandoverRequest content, extracts UE and session
 * information, and stores pending handover state for subsequent acknowledgement
 * processing.  On error, logs the failure and returns without further action.
 *
 * @param gnbId The source gNB identifier
 * @param pdu The XnAP-PDU containing the HandoverRequest
 */
void XnTask::receiveHandoverRequest(int gnbId, uint16_t stream, ASN_XNAP_XnAP_PDU *pdu)
{

    // Validate outer PDU structure.
    if (pdu->present != ASN_XNAP_XnAP_PDU_PR_initiatingMessage ||
        !pdu->choice.initiatingMessage ||
        !pdu->choice.initiatingMessage->value.buf)
    {
        // TODO: send XnAP error indication to source gNB
        m_logger->err("receiveHandoverRequest: malformed PDU from gnbId=%d", gnbId);
        return;
    }

    auto *initMsg = pdu->choice.initiatingMessage;

    // Decode the HandoverRequest from the InitiatingMessage OPEN TYPE value.
    auto *hoReq = xnap_encode::Decode<ASN_XNAP_HandoverRequest_t>(
        asn_DEF_ASN_XNAP_HandoverRequest,
        reinterpret_cast<const uint8_t *>(initMsg->value.buf),
        static_cast<size_t>(initMsg->value.size));
    if (!hoReq)
    {
        // TODO: send XnAP error indication to source gNB
        m_logger->err("receiveHandoverRequest: failed to decode HandoverRequest from gnbId=%d", gnbId);
        return;
    }

    // pull the next transaction ID 
    uint32_t xnTxId = m_nextXnTxId++;

    // Iterate IEs and store locally

    int64_t sourceUeXnApId = 0;
    std::unique_ptr<OctetString> rrcContainer;
    Guami guami{};

    int64_t amfId = 0;  // ng-c-UE-reference: AMF-UE-NGAP-ID
    std::string ngapSourceIpAddr;  // cp-TNL-info-source
    UeSecurityInfo ueSecInfo; // ueSecurityCapabilities and securityInformation
    uint64_t dlAmbr = 0, ulAmbr = 0;  // ueAmbr
    std::vector<PduSessionResource> pduSessions; // pduSessionResourcesToBeSetup_List
    bool isCho = false;
    std::unique_ptr<XnChoRequest> xnChoRequest;


    for (int i = 0; i < hoReq->protocolIEs.list.count; ++i)
    {
        auto *ie = hoReq->protocolIEs.list.array[i];
        if (!ie || !ie->value.buf)
            continue;

        // extract sourceNG-RANnodeUEXnAPID (id=73)

        if (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID)
        {
            auto *srcId = xnap_encode::Decode<ASN_XNAP_NG_RANnodeUEXnAPID_t>(
                asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (srcId)
            {
                sourceUeXnApId = static_cast<int64_t>(*srcId);
                asn::Free(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, srcId);
            }
        }
        // extract HO cause (id=7)
        // (Not used, but logged)
        
        else if (ie->id == XNAP_IE_Cause_HO)
        {
            auto *cause = xnap_encode::Decode<ASN_XNAP_Cause_t>(
                asn_DEF_ASN_XNAP_Cause,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (cause)
            {
                m_logger->info("receiveHandoverRequest: HO cause %ld from gnbId=%d",
                               cause->present, gnbId);
                asn::Free(asn_DEF_ASN_XNAP_Cause, cause);
            }
            else 
            {
                m_logger->err("receiveHandoverRequest: failed to decode Cause from gnbId=%d", gnbId);
            }
        }
        
        // Extract Target cell Global ID (id=78)
        //  (not used, just logged - should match this cell's CGI)

        else if (ie->id == XNAP_IE_targetCellGlobalID)
        {
            auto *tgtCell = xnap_encode::Decode<ASN_XNAP_Target_CGI_t>(
                asn_DEF_ASN_XNAP_Target_CGI,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (tgtCell)
            {
                // Log the target cell's NR CGI (PLMN + NR Cell ID) for debugging.
                Plmn plmn;
                ngap_utils::PlmnFromAsn_Ref(
                    reinterpret_cast<const ASN_NGAP_PLMNIdentity_t &>(tgtCell->choice.nr->plmn_id),
                    plmn);
                m_logger->info("receiveHandoverRequest: target CGI PLMN=%d/%d NR Cell ID=0x%09lx from gnbId=%d",
                               plmn.mcc, plmn.mnc, tgtCell->choice.nr->nr_CI, gnbId);

                asn::Free(asn_DEF_ASN_XNAP_Target_CGI, tgtCell);
            }
            else
            {
                m_logger->err("receiveHandoverRequest: failed to decode TargetID_CGI from gnbId=%d", gnbId);
            }
        }

        // extract GUAMI (id=15) into guami

        else if (ie->id == XNAP_IE_GUAMI)
        {
            auto *recGuami = xnap_encode::Decode<ASN_XNAP_GUAMI_t>(
                asn_DEF_ASN_XNAP_GUAMI,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (recGuami)
            {
                // save GUAMI for later use
                guami.amfRegionId = asn::GetBitStringInt<8>(recGuami->amf_region_id);
                guami.amfSetId    = asn::GetBitStringInt<10>(recGuami->amf_set_id);
                guami.amfPointer  = asn::GetBitStringInt<6>(recGuami->amf_pointer);
                ngap_utils::PlmnFromAsn_Ref(
                    reinterpret_cast<const ASN_NGAP_PLMNIdentity_t &>(recGuami->plmn_ID),
                    guami.plmn);

                // Log the GUAMI (AMF region + AMF set + AMF ID) for debugging.
                m_logger->info("receiveHandoverRequest: GUAMI AMF Region ID=%d AMF Set ID=%d AMF ID=0x%06x from gnbId=%d",
                               guami.amfRegionId, guami.amfSetId, guami.amfPointer, gnbId);

                asn::Free(asn_DEF_ASN_XNAP_GUAMI, recGuami);
            }
            else
            {
                m_logger->err("receiveHandoverRequest: failed to decode GUAMI from gnbId=%d", gnbId);
            }
        }

        // extract UEContextInfoHORequest (id=83)

        else if (ie->id == XNAP_IE_UEContextInfoHORequest)
        {
            auto *ueCtxInfo = xnap_encode::Decode<ASN_XNAP_UEContextInfoHORequest_t>(
                asn_DEF_ASN_XNAP_UEContextInfoHORequest,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (!ueCtxInfo)
            {
                m_logger->err("receiveHandoverRequest: failed to decode UEContextInfoHORequest from gnbId=%d", gnbId);
                continue;
            }

            // extract ng-c-UE-reference: AMF-UE-NGAP-ID into amfId

            amfId = asn::GetSigned64(ueCtxInfo->ng_c_UE_reference);

            // extract cp-TNL-info-source: control-plane transport layer address into ngapSourceIpAddr string

            if (ueCtxInfo->cp_TNL_info_source.present ==
                ASN_XNAP_CPTransportLayerInformation_PR_endpointIPAddress)
            {
                const auto &bs = ueCtxInfo->cp_TNL_info_source.choice.endpointIPAddress;
                if (bs.buf && bs.size >= 4)
                {
                    char ipBuf[20]{};
                    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u",
                             bs.buf[0], bs.buf[1], bs.buf[2], bs.buf[3]);
                    ngapSourceIpAddr = ipBuf;
                }
            }

            // extract ueSecurityCapabilities into ueSecInfo

            {
                const auto &sc = ueCtxInfo->ueSecurityCapabilities;
                ueSecInfo.nRencryptionAlgorithmsBitmap =
                    asn::GetBitStringInt<16>(sc.nr_EncyptionAlgorithms);
                ueSecInfo.nRintegrityProtectionAlgorithmsBitmap =
                    asn::GetBitStringInt<16>(sc.nr_IntegrityProtectionAlgorithms);
                ueSecInfo.eUTRAencryptionAlgorithmsBitmap =
                    asn::GetBitStringInt<16>(sc.e_utra_EncyptionAlgorithms);
                ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap =
                    asn::GetBitStringInt<16>(sc.e_utra_IntegrityProtectionAlgorithms);
            }

            // extract securityInformation into ueSecInfo

            {
                const auto &si = ueCtxInfo->securityInformation;
                if (si.key_NG_RAN_Star.buf && si.key_NG_RAN_Star.size >= 32)
                    std::memcpy(ueSecInfo.k_gnb.data(), si.key_NG_RAN_Star.buf, 32);
            }

            // extract ueAMBR into dlAmbr and ulAmbr
            dlAmbr = static_cast<uint64_t>(ueCtxInfo->ue_AMBR.dl_UE_AMBR);
            ulAmbr = static_cast<uint64_t>(ueCtxInfo->ue_AMBR.ul_UE_AMBR);

            // extract pduSessionResourcesToBeSetup-List into pduSessions vector

            {
                auto &list = ueCtxInfo->pduSessionResourcesToBeSetup_List.list;
                for (int iPdu = 0; iPdu < list.count; iPdu++)
                {
                    auto *item = list.array[iPdu];
                    if (!item)
                        continue;

                    PduSessionResource res(sourceUeXnApId, static_cast<int>(item->pduSessionId));

                    // S-NSSAI
                    if (item->s_NSSAI.sst.buf && item->s_NSSAI.sst.size > 0)
                        res.sNssai.sst = item->s_NSSAI.sst.buf[0];
                    if (item->s_NSSAI.sd && item->s_NSSAI.sd->buf && item->s_NSSAI.sd->size >= 3)
                        res.sNssai.sd = asn::GetOctet3(*item->s_NSSAI.sd);

                    // session AMBR (optional)
                    if (item->pduSessionAMBR)
                    {
                        res.sessionAmbr.dlAmbr =
                            static_cast<uint64_t>(item->pduSessionAMBR->downlink_session_AMBR);
                        res.sessionAmbr.ulAmbr =
                            static_cast<uint64_t>(item->pduSessionAMBR->uplink_session_AMBR);
                    }

                    // UL GTP tunnel at UPF
                    if (item->uL_NG_U_TNLatUPF.present ==
                        ASN_XNAP_UPTransportLayerInformation_PR_gtpTunnel &&
                        item->uL_NG_U_TNLatUPF.choice.gtpTunnel)
                    {
                        auto *t = item->uL_NG_U_TNLatUPF.choice.gtpTunnel;
                        res.upTunnel.address = asn::GetOctetString(t->tnl_address);
                        res.upTunnel.teid    = static_cast<uint32_t>(asn::GetOctet4(t->gtp_teid));
                    }

                    // source DL GTP tunnel (optional)
                    if (item->source_DL_NG_U_TNL_Information &&
                        item->source_DL_NG_U_TNL_Information->present ==
                            ASN_XNAP_UPTransportLayerInformation_PR_gtpTunnel &&
                        item->source_DL_NG_U_TNL_Information->choice.gtpTunnel)
                    {
                        auto *t = item->source_DL_NG_U_TNL_Information->choice.gtpTunnel;
                        res.downTunnel.address = asn::GetOctetString(t->tnl_address);
                        res.downTunnel.teid    = static_cast<uint32_t>(asn::GetOctet4(t->gtp_teid));
                    }

                    // PDU session type
                    switch (item->pduSessionType)
                    {
                    case 1: res.sessionType = PduSessionType::IPv4;     break;
                    case 2: res.sessionType = PduSessionType::IPv6;     break;
                    case 3: res.sessionType = PduSessionType::IPv4v6;   break;
                    case 4: res.sessionType = PduSessionType::ETHERNET; break;
                    default: res.sessionType = PduSessionType::UNSTRUCTURED; break;
                    }

                    // QoS flows
                    auto &qosList = item->qosFlowsToBeSetup_List.list;
                    for (int iFlow = 0; iFlow < qosList.count; iFlow++)
                    {
                        auto *xnFlow = qosList.array[iFlow];
                        if (!xnFlow)
                            continue;
                        QosFlowInfo flow{};
                        flow.qfi = static_cast<int>(xnFlow->qfi);
                        const auto &qosChars = xnFlow->qosFlowLevelQoSParameters.qos_characteristics;
                        if (qosChars.present == ASN_XNAP_QoSCharacteristics_PR_non_dynamic &&
                            qosChars.choice.non_dynamic)
                            flow.fiveQi = qosChars.choice.non_dynamic->fiveQI;
                        else
                            flow.fiveQi = 9;
                        const auto &arp = xnFlow->qosFlowLevelQoSParameters.allocationAndRetentionPrio;
                        flow.arpPriorityLevel        = arp.priorityLevel;
                        flow.arpPreemptCapability    = arp.pre_emption_capability;
                        flow.arpPreemptVulnerability = arp.pre_emption_vulnerability;
                        res.qosFlows.push_back(flow);
                    }

                    pduSessions.push_back(std::move(res));
                }
            }

            // extract rrc-Context into rrcContainer (as opaque byte array)

            if (ueCtxInfo->rrc_Context.buf && ueCtxInfo->rrc_Context.size > 0)
                rrcContainer = std::make_unique<OctetString>(
                    OctetString::FromArray(ueCtxInfo->rrc_Context.buf,
                                           static_cast<size_t>(ueCtxInfo->rrc_Context.size)));

            asn::Free(asn_DEF_ASN_XNAP_UEContextInfoHORequest, ueCtxInfo);
        }

        // Check for CHO IE

        else if (ie->id == XNAP_IE_CHOinformation_Req)
        {
            auto *cho = xnap_encode::Decode<ASN_XNAP_CHOinformation_Req_t>(
                asn_DEF_ASN_XNAP_CHOinformation_Req,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (!cho)
            {
                m_logger->err("receiveHandoverRequest: failed to decode CHOinformation-Req from gnbId=%d", gnbId);
                continue;
            }

            auto request = std::make_unique<XnChoRequest>();
            request->choTrigger = cho->cho_trigger == ASN_XNAP_CHOtrigger_cho_replace ? 1 : 0;
            if (cho->targetNG_RANnodeUEXnAPID)
                request->targetNGRANnodeUeXnapId = *cho->targetNG_RANnodeUEXnAPID;
            if (cho->cHO_EstimatedArrivalProbability)
                request->choArrivalProbabilityPercent = *cho->cHO_EstimatedArrivalProbability;

            if (cho->iE_Extensions)
            {
                auto &extensions = cho->iE_Extensions->list;
                for (int j = 0; j < extensions.count; ++j)
                {
                    auto *extension = extensions.array[j];
                    if (!extension || !extension->extensionValue.buf)
                        continue;
                    if (extension->id == XNAP_EXT_CHO_Maxnoof_CondReconfig)
                    {
                        auto *maximum = xnap_encode::Decode<ASN_XNAP_CHO_Maxnoof_CondReconfig_t>(
                            asn_DEF_ASN_XNAP_CHO_Maxnoof_CondReconfig,
                            extension->extensionValue.buf, extension->extensionValue.size);
                        if (maximum)
                        {
                            request->maxNumCondReconfigsToPrepare = *maximum;
                            asn::Free(asn_DEF_ASN_XNAP_CHO_Maxnoof_CondReconfig, maximum);
                        }
                    }
                    else if (extension->id == XNAP_EXT_CHOTimeBasedInformation)
                    {
                        auto *tbi = xnap_encode::Decode<ASN_XNAP_CHOTimeBasedInformation_t>(
                            asn_DEF_ASN_XNAP_CHOTimeBasedInformation,
                            extension->extensionValue.buf, extension->extensionValue.size);
                        if (tbi)
                        {
                            intmax_t windowStart{};
                            if (asn_INTEGER2imax(&tbi->cHO_HOWindowStart, &windowStart) == 0)
                            {
                                request->tbiProvided = true;
                                request->tbiWindowStart = static_cast<int64_t>(windowStart);
                                request->tbiDuration = static_cast<int32_t>(tbi->cHO_HOWindowDuration);
                            }
                            asn::Free(asn_DEF_ASN_XNAP_CHOTimeBasedInformation, tbi);
                        }
                    }
                }
            }

            asn::Free(asn_DEF_ASN_XNAP_CHOinformation_Req, cho);
            isCho = true;
            xnChoRequest = std::move(request);
        }
    }

    asn::Free(asn_DEF_ASN_XNAP_HandoverRequest, hoReq);

    // Sanity checks on mandatory IEs

    if (!rrcContainer)
    {
        // TODO: send XnAP error indication to source gNB
        m_logger->err("receiveHandoverRequest: missing or empty rrc-Context from gnbId=%d", gnbId);
        return;
    }

    if (sourceUeXnApId == 0)
    {
        // TODO: send XnAP error indication to source gNB
        m_logger->err("receiveHandoverRequest: missing sourceNG-RANnodeUEXnAPID (IE id=73) from gnbId=%d; dropping",
                      gnbId);
        return;
    }

    // Store a pending request to use for response.

    {
        XnPendingHandover pending{};
        pending.xnTxId       = xnTxId;
        // The simulator uses the stable ueId as the source XnAP UE ID, so it is
        // available for timeout rollback even if RRC never produces an ACK.
        pending.ueId         = sourceUeXnApId;
        pending.sourceGnbId  = gnbId;
        pending.sourceUeXnApId = sourceUeXnApId;
        pending.isCho        = isCho;
        pending.role         = XnPendingHandover::Role::TARGET;
        // Timestamp used for timeout cleanup of pending requests that never get an ACK.
        pending.timestamp    = static_cast<uint64_t>(utils::CurrentTimeMillis());

        // Reuse the SCTP stream the source chose for the HandoverRequest for every
        // subsequent message of this handover (per TS 38.423 UE-associated
        // signalling): record it so sendHandoverRequestAck() (and later
        // target→source messages) reply on it.  No reservation in this peer's
        // stream allocator is needed — the source's stream is of the opposite
        // parity to what this (target) side allocates (see StreamParity), so this
        // side's allocator can never independently hand out the same ID.
        pending.streamId     = stream;

        // store in map by transaction ID for later correlation when the ACK is received
        m_pendingHandoversTargetByTxId[xnTxId] = pending;
    }

    // Send XnToRrc msg to RRC task. xnTxId correlates when the response is returned.
    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_REQUEST_RECEIVED);
    msg->xnTxId          = xnTxId;
    msg->sourceGnbId     = gnbId;
    msg->isCho           = isCho;
    msg->rrcContainer    = std::move(rrcContainer);
    msg->amfUeNgapId     = amfId;
    msg->guami           = guami;
    msg->ueSecInfo       = ueSecInfo;
    msg->dlAmbr          = dlAmbr;
    msg->ulAmbr          = ulAmbr;
    msg->ngapSourceIpAddr = ngapSourceIpAddr;
    msg->xnCoreContext = std::make_unique<XnHandoverCoreContext>();
    msg->xnCoreContext->amfUeNgapId = amfId;
    msg->xnCoreContext->guami = guami;
    msg->xnCoreContext->ueSecInfo = ueSecInfo;
    msg->xnCoreContext->ueAmbr.dlAmbr = dlAmbr;
    msg->xnCoreContext->ueAmbr.ulAmbr = ulAmbr;
    msg->xnCoreContext->ngapSourceIpAddr = ngapSourceIpAddr;
    msg->xnChoRequest = std::move(xnChoRequest);
    if (!pduSessions.empty())
        msg->sessionList = std::make_unique<std::vector<PduSessionResource>>(std::move(pduSessions));
    m_base->rrcTask->push(std::move(msg));
}


/**
 * sendHandoverRequestAck (called from task.cpp - RRCtoXn::HANDOVER_REQUEST_ACK_SEND)
 *
 * Called from an RrcToXn message to acknowledge the HandoverRequest. Processes
 * the RRC Reconfiguration container with the HO command and updates pending
 * handover state before sending the XnAP response to the source gNB.
 *
 * @param xnTxId Transaction ID correlating the request/response pair
 * @param ueId Target UE identifier
 * @param rrcContainer RRC Reconfiguration container with HO command
 * @param admittedSessions PDU sessions admitted at target gNB
 * @param rejectedSessions PDU sessions rejected at target gNB
 */
void XnTask::sendHandoverRequestAck(uint32_t xnTxId, uint64_t ueId,
    std::unique_ptr<OctetString> rrcContainer,
    std::unique_ptr<std::vector<PduSessionResource>> admittedSessions,
    std::unique_ptr<std::vector<PduSessionResource>> rejectedSessions)
{
    // Look up pending handover entry by xnTxId to get routing info.
    
    auto pendingIt = m_pendingHandoversTargetByTxId.find(xnTxId);
    XnPendingHandover *pending = pendingIt == m_pendingHandoversTargetByTxId.end() ? nullptr : &pendingIt->second;

    if (!pending)
    {
        // TODO: error handling - remove Xn state, inform RRC to abort handover, 
        //  send XnAP error indication to source gNB
        m_logger->err("sendHandoverRequestAck: no pending entry for xnTxId=%u", xnTxId);
        return;
    }

    int sourceGnbId      = pending->sourceGnbId;
    int64_t targetUeId   = ueId;
    int64_t srcUeXnApId  = pending->sourceUeXnApId;
    pending->ueId = ueId;
    pending->targetUeXnApId = ueId;

    // set ackSent flag to true so that the pending entry is not cleaned up by timeout
    pending->ackSent = true;

    // reset the timestamp to now for future timeout measurements
    pending->timestamp = static_cast<uint64_t>(utils::CurrentTimeMillis());

    // confirm the source gNB is still connected by xN

    XnPeerInfo *sourcePeer = m_xnPeerTable.getPeerInfo(sourceGnbId);
    if (!sourcePeer)
    {
        // TODO: error handling - remove Xn state, inform RRC to abort handover, 
        //  (can't send XnAP error indication to source gNB, since connection is gone)
        m_logger->err("sendHandoverRequestAck: no peer for sourceGnbId=%d xnTxId=%u",
                      sourceGnbId, xnTxId);
        return;
    }

    if (sourcePeer->connectionState != EXnConnectionState::CONNECTED)
    {
        // TODO: error handling - remove Xn state, inform RRC to abort handover, 
        //  (can't send XnAP error indication to source gNB, since connection is gone)
        m_logger->err("sendHandoverRequestAck: peer gnbId=%d not CONNECTED, dropping Ack for xnTxId=%u",
                      sourceGnbId, xnTxId);
        return;
    }

    const GnbConfig *cfg = m_base->config;
    std::string gtpIp = cfg->gtpAdvertiseIp.value_or(cfg->gtpIp);

    // -----------------------------------------------------------------------
    // IE 1 — sourceNG-RANnodeUEXnAPID  (id=73, mandatory, criticality=ignore)
    //   Echo the source gNB's UE XnAP ID back so it can correlate the Ack.
    // -----------------------------------------------------------------------
    auto srcUeId = static_cast<ASN_XNAP_NG_RANnodeUEXnAPID_t>(srcUeXnApId);

    auto ieSrcUeId = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                     asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieSrcUeId->id          = XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID;
    ieSrcUeId->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieSrcUeId.get(), &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &srcUeId))
    {
        m_logger->err("sendHandoverRequestAck: failed to encode sourceNG-RANnodeUEXnAPID");
        return;
    }

    // -----------------------------------------------------------------------
    // IE 2 — targetNG-RANnodeUEXnAPID  (id=79, mandatory, criticality=ignore)
    //   The target gNB's handle for this UE on the Xn interface.
    //   For simulator, we use the UeID.
    // -----------------------------------------------------------------------
    auto tgtUeId = static_cast<ASN_XNAP_NG_RANnodeUEXnAPID_t>(targetUeId);

    auto ieTgtUeId = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                     asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieTgtUeId->id          = XNAP_IE_targetNG_RAN_node_UE_XnAP_ID;
    ieTgtUeId->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieTgtUeId.get(), &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &tgtUeId))
    {
        m_logger->err("sendHandoverRequestAck: failed to encode targetNG-RANnodeUEXnAPID");
        return;
    }

    // -----------------------------------------------------------------------
    // IE 3 — PDUSessionResourcesAdmitted-List  (id=42, mandatory, criticality=ignore)
    //   For each admitted PDU session, allocate a DL Xn-U forwarding tunnel
    //   and list the admitted QoS flows.
    // -----------------------------------------------------------------------
    auto admittedList = asn::WrapUnique(asn::New<ASN_XNAP_PDUSessionResourcesAdmitted_List_t>(),
                                        asn_DEF_ASN_XNAP_PDUSessionResourcesAdmitted_List);

    if (admittedSessions)
    {
        for (auto &resource : *admittedSessions)
        {
            // Allocate a DL forwarding TEID for the Xn-U interface and register
            // it with the GTP task so packets the source relays on this TEID are
            // matched to the UE session and delivered to the UE via RLS.  The
            // registration is torn down with the UE's GTP context on handover
            // cancel/expiry (UE_CONTEXT_RELEASE_RECEIVED) or on the End Marker.
            uint32_t dlTeid = ++m_xnDownlinkTeidCounter;

            {
                auto reg = std::make_unique<NmGnbXnToGtp>(NmGnbXnToGtp::FORWARDING_TEID_REGISTER);
                reg->ueId = targetUeId;
                reg->psi  = resource.psi;
                reg->teid = dlTeid;
                m_base->gtpTask->push(std::move(reg));
            }

            // Build pduItem directly — avoids a value-copy + raw free() of an
            // intermediate admittedInfo struct that shares heap pointers with the copy.
            auto *pduItem = asn::New<ASN_XNAP_PDUSessionResourcesAdmitted_Item_t>();
            pduItem->pduSessionId = static_cast<ASN_XNAP_PDUSession_ID_t>(resource.psi);

            auto *fwdInfo = asn::New<ASN_XNAP_DataForwardingInfoFromTargetNGRANnode_t>();

            if (!resource.qosFlows.empty())
            {
                for (const auto &flow : resource.qosFlows)
                {
                    long qfi = static_cast<long>(flow.qfi);

                    auto *admittedItem = asn::New<ASN_XNAP_QoSFlowsAdmitted_Item_t>();
                    admittedItem->qfi = qfi;
                    asn::SequenceAdd(pduItem->pduSessionResourceAdmittedInfo.qosFlowsAdmitted_List,
                                     admittedItem);

                    auto *fwdItem = asn::New<ASN_XNAP_QoSFLowsAcceptedToBeForwarded_Item_t>();
                    fwdItem->qosFlowIdentifier = qfi;
                    asn::SequenceAdd(fwdInfo->qosFlowsAcceptedForDataForwarding_List, fwdItem);
                }
            }

            // Fallback: a single default QFI=1 when no flows are present.
            //    This shouldn't happen, but rather than error out we create it and log a warning.
            else
            {
                auto *admittedItem = asn::New<ASN_XNAP_QoSFlowsAdmitted_Item_t>();
                admittedItem->qfi = 1;
                asn::SequenceAdd(pduItem->pduSessionResourceAdmittedInfo.qosFlowsAdmitted_List,
                                 admittedItem);

                auto *fwdItem = asn::New<ASN_XNAP_QoSFLowsAcceptedToBeForwarded_Item_t>();
                fwdItem->qosFlowIdentifier = 1;
                asn::SequenceAdd(fwdInfo->qosFlowsAcceptedForDataForwarding_List, fwdItem);
                m_logger->warn("sendHandoverRequestAck: no QoS flows for admitted PDU session %d, using default QFI=1",
                               resource.psi);
            }

            // DL data forwarding tunnel endpoint at this (target) gNB.
            {
                auto *gtpTunnel = asn::New<ASN_XNAP_GTPtunnelTransportLayerInformation_t>();
                uint32_t ipBe = 0;
                {
                    unsigned a, b, c, d;
                    if (sscanf(gtpIp.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
                        ipBe = (a << 24) | (b << 16) | (c << 8) | d;
                }
                asn::SetBitString(gtpTunnel->tnl_address, octet4{ipBe}, 32);
                asn::SetOctetString4(gtpTunnel->gtp_teid, octet4{dlTeid});

                auto *dlFwdTnl = asn::New<ASN_XNAP_UPTransportLayerInformation_t>();
                dlFwdTnl->present = ASN_XNAP_UPTransportLayerInformation_PR_gtpTunnel;
                dlFwdTnl->choice.gtpTunnel = gtpTunnel;

                fwdInfo->pduSessionLevelDLDataForwardingInfo = dlFwdTnl;
            }

            pduItem->pduSessionResourceAdmittedInfo.dataForwardingInfoFromTarget = fwdInfo;

            asn::SequenceAdd(*admittedList, pduItem);
        }
    }

    auto ieAdmitted = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                      asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieAdmitted->id          = XNAP_IE_PDUSessionResourcesAdmitted_List;
    ieAdmitted->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieAdmitted.get(), &asn_DEF_ASN_XNAP_PDUSessionResourcesAdmitted_List, admittedList.get()))
    {
        m_logger->err("sendHandoverRequestAck: failed to encode PDUSessionResourcesAdmitted-List");
        return;
    }
    admittedList.reset();

    // -----------------------------------------------------------------------
    // IE 4 — PDUSessionResourcesNotAdmitted-List  (id=43, optional, criticality=ignore)
    // -----------------------------------------------------------------------
    asn::Unique<ASN_XNAP_ProtocolIE_Field_14202P0_t> ieNotAdmitted{};

    if (rejectedSessions && !rejectedSessions->empty())
    {
        auto notAdmittedList = asn::WrapUnique(asn::New<ASN_XNAP_PDUSessionResourcesNotAdmitted_List_t>(),
                                               asn_DEF_ASN_XNAP_PDUSessionResourcesNotAdmitted_List);

        for (const auto &resource : *rejectedSessions)
        {
            auto *pduItem = asn::New<ASN_XNAP_PDUSessionResourcesNotAdmitted_Item_t>();
            pduItem->pduSessionId = static_cast<ASN_XNAP_PDUSession_ID_t>(resource.psi);
            asn::SequenceAdd(*notAdmittedList, pduItem);
        }

        ieNotAdmitted = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                        asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
        ieNotAdmitted->id          = XNAP_IE_PDUSessionResourcesNotAdmitted;
        ieNotAdmitted->criticality = ASN_XNAP_Criticality_ignore;
        if (!setHoIeValue(ieNotAdmitted.get(), &asn_DEF_ASN_XNAP_PDUSessionResourcesNotAdmitted_List,
                          notAdmittedList.get()))
        {
            m_logger->err("sendHandoverRequestAck: failed to encode PDUSessionResourcesNotAdmitted-List");
            ieNotAdmitted.reset();
        }
    }

    // -----------------------------------------------------------------------
    // IE 5 — Target2SourceNG-RANnodeTranspContainer  (id=77, mandatory, criticality=ignore)
    //   OCTET STRING carrying the RRCReconfiguration (handover command) for the UE.
    //   Provided as a passed paarmeter from RRC.  This just packages it into an OCTET STRING for XnAP IE.
    // -----------------------------------------------------------------------
    OCTET_STRING_t rrcOs{};
    OCTET_STRING_fromBuf(&rrcOs,
                         reinterpret_cast<const char *>(rrcContainer->data()),
                         static_cast<int>(rrcContainer->length()));

    auto ieRrcContainer = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                          asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    ieRrcContainer->id          = XNAP_IE_Target2SourceTranspContainer;
    ieRrcContainer->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieRrcContainer.get(), &asn_DEF_OCTET_STRING, &rrcOs))
    {
        m_logger->err("sendHandoverRequestAck: failed to encode Target2SourceNG-RANnodeTranspContainer");
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_OCTET_STRING, &rrcOs);
        return;
    }
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_OCTET_STRING, &rrcOs);

    // -----------------------------------------------------------------------
    // Assemble HandoverRequestAcknowledge and wrap in SuccessfulOutcome
    // -----------------------------------------------------------------------
    auto hoAck = asn::WrapUnique(asn::New<ASN_XNAP_HandoverRequestAcknowledge_t>(),
                                 asn_DEF_ASN_XNAP_HandoverRequestAcknowledge);
    asn::SequenceAdd(hoAck->protocolIEs, ieSrcUeId.release());
    asn::SequenceAdd(hoAck->protocolIEs, ieTgtUeId.release());
    asn::SequenceAdd(hoAck->protocolIEs, ieAdmitted.release());
    if (ieNotAdmitted)
        asn::SequenceAdd(hoAck->protocolIEs, ieNotAdmitted.release());
    asn::SequenceAdd(hoAck->protocolIEs, ieRrcContainer.release());

    auto succMsg = asn::WrapUnique(asn::New<ASN_XNAP_SuccessfulOutcome_t>(),
                                   asn_DEF_ASN_XNAP_SuccessfulOutcome);
    succMsg->procedureCode = XN_PROC_HANDOVER_PREPARATION;
    succMsg->criticality   = ASN_XNAP_Criticality_reject;
    if (ANY_fromType_aper(&succMsg->value, &asn_DEF_ASN_XNAP_HandoverRequestAcknowledge, hoAck.get()) != 0)
    {
        // TODO: error handling - remove Xn state, inform RRC to abort handover,
        //  send XnAP error indication to source gNB
        m_logger->err("sendHandoverRequestAck: failed to encode HandoverRequestAcknowledge into SuccessfulOutcome");
        return;
    }
    hoAck.reset();

    auto outerPdu = asn::WrapUnique(asn::New<ASN_XNAP_XnAP_PDU_t>(), asn_DEF_ASN_XNAP_XnAP_PDU);
    outerPdu->present                  = ASN_XNAP_XnAP_PDU_PR_successfulOutcome;
    outerPdu->choice.successfulOutcome = succMsg.release();

    // -----------------------------------------------------------------------
    // APER-encode and send via SCTP
    // -----------------------------------------------------------------------
    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu.get(), encoded, buffer))
    {
        m_logger->err("sendHandoverRequestAck: APER encoding failed for xnTxId=%u gnbId=%d",
                      xnTxId, sourceGnbId);
        return;
    }

    // StreamID - reply on the stream the source used for the HandoverRequest,
    // which receiveHandoverRequest() recorded and reserved.  The fallback below
    // only triggers if that stream was somehow not captured (e.g. a malformed
    // request arriving on stream 0), in which case we allocate a fresh one.

    if (pending->streamId == 0)
    {
        // allocate a new SCTP stream for the source gNB
        pending->streamId = sourcePeer->streamIdManager.allocate().value_or(0);
        // failure check
        if (pending->streamId == 0)
        {
            // TODO: error handling - remove Xn state, inform RRC to abort handover,
            //  (cannot send XnAP error indication to source gNB - no SCTP stream avaiable)
            m_logger->err("sendHandoverRequestAck: no free SCTP stream for gnbId=%d xnTxId=%u",
                        sourceGnbId, xnTxId);
            delete[] buffer;
            return;
        }
    }

    // Send msg using SCTP

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = sourcePeer->clientId;
    sctpMsg->stream   = pending->streamId;
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->xnSctpTask->push(std::move(sctpMsg));

    m_logger->info("XnAP HandoverRequestAcknowledge sent to gnbId=%d for xnTxId=%u ueId=%ld",
                   sourceGnbId, xnTxId, targetUeId);

    // Retain the target-side correlation after preparation: status transfer,
    // cancel, and context-release procedures use the XnAP UE ID pair later.

}


/**
 * sendHandoverPreparationFailure (called from task.cpp - RRCtoXn::HANDOVER_PREPARATION_FAILURE_SEND)
 *
 * Sends a HandoverPreparationFailure message to the source gNB in response to a
 * pending HandoverRequest.  The pending request is removed from the pending map.
 * 
 * Error handling note: It is expected that any saved handover state for the other layers (e.g., RRC, NGAP)
 * has already been cleaned up by the time this is called, so this function does not attempt to inform 
 * then of the failure.  Also, if the message preparation fails, the error is logged.  
 * The source gNB will have to time out the handover request to recover from the failure.
 * 
 * @param xnTxId Transaction ID of the pending handover preparation request.
 * @param reason Cause value used to build the handover preparation failure.
 * @return void
 */
void XnTask::sendHandoverPreparationFailure(uint32_t xnTxId, int reason)
{
    auto pending = m_pendingHandoversTargetByTxId.find(xnTxId);
    if (pending == m_pendingHandoversTargetByTxId.end())
    {
        m_logger->warn("sendHandoverPreparationFailure: no pending entry for xnTxId=%u, aborting.", xnTxId);
        return;
    }

    auto *pendingHo = &pending->second; 

    // get peer info for the source gNB to send the failure message

    auto *sourcePeer = m_xnPeerTable.getPeerInfo(pendingHo->sourceGnbId);
    if (!sourcePeer || sourcePeer->connectionState != EXnConnectionState::CONNECTED) 
    { 
        m_logger->warn("sendHandoverPreparationFailure: source gNB %d not in peer table, cannot send failure for xnTxId=%u, aborting.",
                       pendingHo->sourceGnbId, xnTxId);
        
        removeTargetPendingHandover(xnTxId);
        return; 
    }

    // IE sourceNG-RANnodeUEXnAPID (id=73, mandatory, criticality=ignore)

    ASN_XNAP_NG_RANnodeUEXnAPID_t sourceId = pendingHo->sourceUeXnApId;
    auto sourceIe = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                    asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    sourceIe->id = XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID;
    sourceIe->criticality = ASN_XNAP_Criticality_ignore;
    
    // IE Cause (id=19, mandatory, criticality=ignore)

    ASN_XNAP_Cause_t cause{};
    cause.present = static_cast<ASN_XNAP_Cause_PR>(reason);
    if (cause.present == ASN_XNAP_Cause_PR_protocol)
        cause.choice.protocol = ASN_XNAP_CauseProtocol_semantic_error;
    else if (cause.present == ASN_XNAP_Cause_PR_transport)
        cause.choice.transport = ASN_XNAP_CauseTransportLayer_unspecified;
    else
    { 
        cause.present = ASN_XNAP_Cause_PR_radioNetwork; 
        cause.choice.radioNetwork = ASN_XNAP_CauseRadioNetworkLayer_unspecified; 
    }
    
    auto causeIe = asn::WrapUnique(asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(),
                                   asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0);
    causeIe->id = XNAP_IE_Cause_HO;
    causeIe->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(sourceIe.get(), &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &sourceId) ||
        !setHoIeValue(causeIe.get(), &asn_DEF_ASN_XNAP_Cause, &cause))
    { 
        m_logger->err("sendHandoverPreparationFailure: failed to encode Cause for xnTxId=%u sourceGnbId=%d, aborting.", xnTxId, pendingHo->sourceGnbId);

        // clear pending handover Xn state for this transaction ID
        removeTargetPendingHandover(xnTxId);

        return; 
    }

    // Create IE for HandoverPreparationFailure and wrap in UnsuccessfulOutcome

    auto body = asn::WrapUnique(asn::New<ASN_XNAP_HandoverPreparationFailure_t>(),
                                asn_DEF_ASN_XNAP_HandoverPreparationFailure);
    asn::SequenceAdd(body->protocolIEs, sourceIe.release());
    asn::SequenceAdd(body->protocolIEs, causeIe.release());
    
    auto outcome = asn::WrapUnique(asn::New<ASN_XNAP_UnsuccessfulOutcome_t>(),
                                   asn_DEF_ASN_XNAP_UnsuccessfulOutcome);
    outcome->procedureCode = XN_PROC_HANDOVER_PREPARATION;
    outcome->criticality = ASN_XNAP_Criticality_reject;
    
    if (ANY_fromType_aper(&outcome->value, &asn_DEF_ASN_XNAP_HandoverPreparationFailure, body.get()) != 0)
    { 
        m_logger->err("sendHandoverPreparationFailure: failed to encode HandoverPreparationFailure into UnsuccessfulOutcome for xnTxId=%u sourceGnbId=%d, aborting.", xnTxId, pendingHo->sourceGnbId);

        // clear pending handover Xn state for this transaction ID
        removeTargetPendingHandover(xnTxId);

        return; 
    }
    body.reset();
    
    // create outer PDU and encode for SCTP send

    auto outerPdu = asn::WrapUnique(asn::New<ASN_XNAP_XnAP_PDU_t>(), asn_DEF_ASN_XNAP_XnAP_PDU);
    outerPdu->present = ASN_XNAP_XnAP_PDU_PR_unsuccessfulOutcome;
    outerPdu->choice.unsuccessfulOutcome = outcome.release();
    ssize_t encoded{}; uint8_t *buffer{};
    
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu.get(), encoded, buffer)) 
    { 
        m_logger->err("sendHandoverPreparationFailure: failed to APER encode XnAP PDU for xnTxId=%u sourceGnbId=%d, aborting.", xnTxId, pendingHo->sourceGnbId);

        // clear pending handover Xn state for this transaction ID
        removeTargetPendingHandover(xnTxId);

        return; 
    }

    // StreamID - if we have one assigned, use it.  Otherwise, allocate a new one

    if (pendingHo->streamId == 0)
    {
        // allocate a new SCTP stream for the source gNB
        pendingHo->streamId = sourcePeer->streamIdManager.allocate().value_or(0);
        // failure check
        if (pendingHo->streamId == 0)
        {
            // TODO: error handling - remove Xn state, inform RRC to abort handover,
            //  (cannot send XnAP error indication to source gNB - no SCTP stream avaiable)
            m_logger->err("sendHandoverRequestAck: no free SCTP stream for gnbId=%d xnTxId=%u",
                        pendingHo->sourceGnbId, xnTxId);
            delete[] buffer;
            return;
        }
    }

    // send using SCTP
    
    auto sctp = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE); 
    sctp->clientId = sourcePeer->clientId; 
    sctp->stream = pendingHo->streamId;
    sctp->buffer = UniqueBuffer{buffer, static_cast<size_t>(encoded)}; 
    m_base->xnSctpTask->push(std::move(sctp));

    // clear pending handover Xn state for this transaction ID
    removeTargetPendingHandover(xnTxId);
}

/**
 * sendHandoverCancel (called from task.cpp - RRCtoXn::HANDOVER_CANCEL_SEND)
 * 
 * Sends a HandoverCancel message to the target gNB related to a pending handover request.
 *
 * Error handling note: It is expected that any saved handover state for the other layers (e.g., RRC, NGAP)
 * has already been cleaned up by the time this is called, so this function does not attempt
 * to inform them of the failure.  Also, if the message preparation fails, the error is logged.  
 * The target gNB will have to time out the handover request to recover from the failure.
 * 
 * @param ueId UE identifier for the pending handover request.
 * @param targetNci NCI of the target gNB.
 * @param isCho Whether this is for a conditional handover.
 * @return void
 */
void XnTask::sendHandoverCancel(int64_t ueId, int64_t targetNci, bool isCho)
{
    // get entry for UE in pending handover map (RRC addresses us by NCI)
    auto pending = findSourcePendingByNci(ueId, targetNci);
    if (pending == m_pendingHandoversSourceByUeId.end())
    {
        // If we can't find it, just log and return.
        m_logger->warn("UE[%ld] HandoverCancel ignored: no outgoing Xn preparation", ueId);
        return;
    }

    // pointer to the pending handover entry
    auto *pendingHo = &pending->second;
    const int targetGnbId = pendingHo->targetGnbId;

    // TODO: tear down UP GTP tunnels for any admitted PDU sessions, if any were created.

    // get peer info for the target gNB to send the cancel message

    auto *targetPeer = m_xnPeerTable.getPeerInfo(targetGnbId);
    if (!targetPeer || targetPeer->connectionState != EXnConnectionState::CONNECTED)
    {
        m_logger->warn("UE[%ld] HandoverCancel ignored: target gNB %d not in peer table or not connected",
                       ueId, targetGnbId);

        // Remove pending handover resources
        removeSourcePendingHandover(ueId, targetGnbId);

        return;
    }

    // check for valid source and target UE XnAP IDs in the pending handover entry
    //   We check this up front to avoid having to check when the ASN structs are allocated.
    //   Note: the target UE XnAP ID is optional in the normal HandoverCancel message, 
    //     but is required in the CHO Handover Cancel message, so we include isCho in the boolean logic.
    if (pendingHo->sourceUeXnApId <= 0)
    {
        m_logger->warn("UE[%ld] HandoverCancel ignored: invalid source UE XnAP ID %ld in pending handover entry for target gNB %d",
                       ueId, pendingHo->sourceUeXnApId, targetGnbId);
        // Remove pending handover resources
        removeSourcePendingHandover(ueId, targetGnbId);
        return;
    }
    if (isCho && pendingHo->targetUeXnApId <= 0)
    {
        m_logger->warn("UE[%ld] HandoverCancel ignored: invalid target UE XnAP ID %ld in pending handover entry for target gNB %d (CHO)",
                       ueId, pendingHo->targetUeXnApId, targetGnbId);
        // Remove pending handover resources
        removeSourcePendingHandover(ueId, targetGnbId);
        return;
    }

    // IE - sourceNG-RANnodeUEXnAPID (id=73, mandatory, criticality=reject)

    // create source NG-RAN node UE XnAP ID IE
    auto *sourceIe = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    if (!sourceIe)
    {
        m_logger->err("UE[%ld] HandoverCancel: failed to allocate source IE", ueId);

        // Remove pending handover from the outgoing requests map
        removeSourcePendingHandover(ueId, targetGnbId);

        return;
    }
    sourceIe->id = XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID;
    sourceIe->criticality = ASN_XNAP_Criticality_reject;
    {
        ASN_XNAP_NG_RANnodeUEXnAPID_t id = pendingHo->sourceUeXnApId;
        if (!setHoIeValue(sourceIe, &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &id))
        {
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe);
            m_logger->err("UE[%ld] HandoverCancel: failed to set source IE value", ueId);

            // Remove pending handover resources
            removeSourcePendingHandover(ueId, targetGnbId);

            return;
        }
    }

    // IE - target NG-RAN node UE XnAP ID IE
    //  Note - the target NG-RAN node UE XnAP ID is optional in the normal HandoverCancel message, 
    //    but is required in the CHO Handover Cancel message.

    ASN_XNAP_ProtocolIE_Field_14202P0_t *targetIe = nullptr;

    // only create the target IE if this is a CHO cancel or if the target UE XnAP ID is valid (non-zero)
    if (isCho || pendingHo->targetUeXnApId > 0)
    {
        targetIe = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
        if (!targetIe)
        {
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe);
            m_logger->err("UE[%ld] HandoverCancel: failed to allocate target IE", ueId);
            // Remove pending handover resources
            removeSourcePendingHandover(ueId, targetGnbId);
            return;
        }
        targetIe->id = XNAP_IE_targetNG_RAN_node_UE_XnAP_ID;
        targetIe->criticality = isCho ? ASN_XNAP_Criticality_reject : ASN_XNAP_Criticality_ignore;
        {
            ASN_XNAP_NG_RANnodeUEXnAPID_t id = pendingHo->targetUeXnApId;
            if (!setHoIeValue(targetIe, &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &id))
            {
                asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe);
                asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, targetIe);
                m_logger->err("UE[%ld] HandoverCancel: failed to set target IE value", ueId);
                // Remove pending handover resources
                removeSourcePendingHandover(ueId, targetGnbId);
                return;
            }
        }
    }

    // IE - Cause (id=19, mandatory, criticality=ignore)

    ASN_XNAP_Cause_t cause{};
    cause.present = ASN_XNAP_Cause_PR_radioNetwork;
    cause.choice.radioNetwork = ASN_XNAP_CauseRadioNetworkLayer_procedure_cancelled;
    auto *causeIe = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    causeIe->id = XNAP_IE_Cause_HO; 
    causeIe->criticality = ASN_XNAP_Criticality_ignore;
    
    if (!setHoIeValue(causeIe, &asn_DEF_ASN_XNAP_Cause, &cause))
    {
        if (sourceIe) asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe);
        if (targetIe) asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, targetIe);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, causeIe);
        m_logger->err("UE[%ld] HandoverCancel: failed to set cause IE value", ueId);
        
        // Remove pending handover resources
        removeSourcePendingHandover(ueId, targetGnbId);
        
        return;
    }

    // Select message body type based on whether this is a conditional handover or not

    void *body{}; asn_TYPE_descriptor_t *bodyType{};
    if (isCho)
    {
        auto *v = asn::New<ASN_XNAP_ConditionalHandoverCancel_t>();
        asn::SequenceAdd(v->protocolIEs, sourceIe); 
        asn::SequenceAdd(v->protocolIEs, targetIe);
        asn::SequenceAdd(v->protocolIEs, causeIe); 
        body = v; 
        bodyType = &asn_DEF_ASN_XNAP_ConditionalHandoverCancel;
    }
    else
    {
        auto *v = asn::New<ASN_XNAP_HandoverCancel_t>();
        asn::SequenceAdd(v->protocolIEs, sourceIe);
        // the target IE is optional for normal handover cancel, so only add it if it exists 
        if (targetIe) asn::SequenceAdd(v->protocolIEs, targetIe);
        asn::SequenceAdd(v->protocolIEs, causeIe); 
        body = v; 
        bodyType = &asn_DEF_ASN_XNAP_HandoverCancel;
    }
    
    auto *init = asn::New<ASN_XNAP_InitiatingMessage_t>();
    init->procedureCode = isCho ? XN_PROC_CONDITIONAL_HANDOVER_CANCEL : XN_PROC_HANDOVER_CANCEL;
    init->criticality = ASN_XNAP_Criticality_ignore;
    if (ANY_fromType_aper(&init->value, bodyType, body) != 0) 
    { 
        ASN_STRUCT_FREE(*bodyType, body); 
        asn::Free(asn_DEF_ASN_XNAP_InitiatingMessage, init); 
        m_logger->err("UE[%ld] HandoverCancel: failed to encode %s into InitiatingMessage", ueId, isCho ? "ConditionalHandoverCancel" : "HandoverCancel");
        
        // Remove pending handover resources
        removeSourcePendingHandover(ueId, targetGnbId);
        
        return; 
    }
    
    // Create outer PDU and encode for SCTP send

    ASN_STRUCT_FREE(*bodyType, body);
    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>(); 
    outerPdu->present = ASN_XNAP_XnAP_PDU_PR_initiatingMessage; 
    outerPdu->choice.initiatingMessage = init;
    
    ssize_t encoded{}; 
    uint8_t *buffer{};
    
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer)) 
    { 
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu); 
        m_logger->err("UE[%ld] HandoverCancel: failed to APER encode XnAP PDU for target gNB %d, aborting.", ueId, pendingHo->targetGnbId);
        // Remove pending handover resources
        removeSourcePendingHandover(ueId, targetGnbId);
        return; 
    }
    
    // Send message using SCTP

    auto sctp = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE); 
    sctp->clientId = targetPeer->clientId; 
    sctp->stream = pendingHo->streamId;
    sctp->buffer = UniqueBuffer{buffer, static_cast<size_t>(encoded)}; 
    m_base->xnSctpTask->push(std::move(sctp));
    
    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
    
    // Remove pending handover from the outgoing requests map
    removeSourcePendingHandover(ueId, targetGnbId);
    
    m_logger->info("UE[%ld] XnAP %s sent", ueId, isCho ? "ConditionalHandoverCancel" : "HandoverCancel");
}

/**
 * sendSnStatusTransfer (called from task.cpp - RRCtoXn::SN_STATUS_TRANSFER_SEND)
 * 
 * Sends a SNStatusTransfer message to the target gNB for a given UE.  This is used to transfer 
 * the PDCP SN status of DRBs during handover.  It will cause the target gNB to update its PDCP state 
 * for the UE's DRBs.
 * 
 * @param ueId UE identifier for which to send the SNStatusTransfer.
 * @param targetNci NCI of the target gNB.
 * @param isCho Whether this is for a conditional handover.
 * @return void
 */
void XnTask::sendSnStatusTransfer(int64_t ueId, int64_t targetNci, bool isCho)
{
    
    // locate the pending handover request for this UE (RRC addresses us by NCI)
    auto correlation = findSourcePendingByNci(ueId, targetNci);
    if (correlation == m_pendingHandoversSourceByUeId.end() || correlation->second.targetUeXnApId <= 0)
    {
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: ACK correlation incomplete", ueId);
        // TODO: error handling
        return;
    }

    auto *pendingHo = &correlation->second;
    const int targetGnbId = pendingHo->targetGnbId;

    // find target gNB in peer table

    auto *targetPeer = m_xnPeerTable.getPeerInfo(targetGnbId);
    if (!targetPeer || targetPeer->connectionState != EXnConnectionState::CONNECTED)
    {
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: target gNB [%d] not Xn Connected", ueId, targetGnbId);
        // TODO: error handling
        return;
    }
    
    // grab copy of current RLS context to copy radio bearers

    auto snapshot = m_base->rlsTask->copyUeContext(ueId);
    if (!snapshot)
    {
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: RLS context unavailable", ueId);
        // TODO: error handling
        return;
    }

    // IE - Create source and target NG_RAN_node_UE_XnAP_ID IEs (id=73 and 74, mandatory, criticality=reject) 

    auto makeIdIe = [](long ieId, int64_t value) {
        auto *ie = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
        ie->id = ieId;
        ie->criticality = ASN_XNAP_Criticality_reject;
        ASN_XNAP_NG_RANnodeUEXnAPID_t id = static_cast<ASN_XNAP_NG_RANnodeUEXnAPID_t>(value);
        if (!setHoIeValue(ie, &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &id))
        {
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ie);
            return static_cast<ASN_XNAP_ProtocolIE_Field_14202P0_t *>(nullptr);
        }
        return ie;
    };
    
    auto *sourceIe = makeIdIe(XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID, pendingHo->sourceUeXnApId);
    if (!sourceIe)
    {
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: failed to create source IE", ueId);
        // TODO: error handling
        return;
    }
    auto *targetIe = makeIdIe(XNAP_IE_targetNG_RAN_node_UE_XnAP_ID, pendingHo->targetUeXnApId);
    if (!targetIe)
    {
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: failed to create target IE", ueId);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe);
        // TODO: error handling
        return;
    }

    // Create IE for Data Radio Bearers
    int drb_count = 0;
    auto *list = asn::New<ASN_XNAP_DRBsSubjectToStatusTransfer_List_t>();

    for (const auto &bearer : snapshot->radioBearers)
    {
        // skip non-data radio bearers
        if ((bearer.bearerId & 0x40) == 0)
            continue;

        // DRB value is low 6 bits
        auto *item = asn::New<ASN_XNAP_DRBsSubjectToStatusTransfer_Item_t>();
        item->drbID = bearer.bearerId & 0x3f;

        auto setCount = [](ASN_XNAP_DRBBStatusTransferChoice_t &choice, uint32_t count) {
            choice.present = ASN_XNAP_DRBBStatusTransferChoice_PR_pdcp_sn_18bits;
            choice.choice.pdcp_sn_18bits = asn::New<ASN_XNAP_DRBBStatusTransfer18bitsSN_t>();
            // RLS stores one combined uint32 count.  Split it into the PDCP standard
            // 18-bit SN and 14-bit HFN values.
            choice.choice.pdcp_sn_18bits->cOUNTValue.pdcp_SN18 = count & ((1u << 18) - 1);
            choice.choice.pdcp_sn_18bits->cOUNTValue.hfn_PDCP_SN18 = count >> 18;
        };

        // Generate IEs for the uplink and downlink Sequence Number counts
        setCount(item->pdcpStatusTransfer_UL, bearer.ulSn);
        setCount(item->pdcpStatusTransfer_DL, bearer.dlSn);

        asn::SequenceAdd(*list, item);
        drb_count++;
    }

    // if no data bearers found, log error and return
    if (list->list.count == 0)
    {
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, targetIe);
        asn::Free(asn_DEF_ASN_XNAP_DRBsSubjectToStatusTransfer_List, list);
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: no transferable DRBs", ueId);
        // TODO: error handling
        return;
    }

    auto *listIe = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    listIe->id = XNAP_IE_DRBS_SUBJECT_TO_STATUS_TRANSFER;
    listIe->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(listIe, &asn_DEF_ASN_XNAP_DRBsSubjectToStatusTransfer_List, list))
    {
        asn::Free(asn_DEF_ASN_XNAP_DRBsSubjectToStatusTransfer_List, list);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, targetIe);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, listIe);
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: failed to encode DRB list IE", ueId);
        // TODO: error handling
        return;
    }

    asn::Free(asn_DEF_ASN_XNAP_DRBsSubjectToStatusTransfer_List, list);

    // Create Status Transfer body IE

    auto *body = asn::New<ASN_XNAP_SNStatusTransfer_t>();
    asn::SequenceAdd(body->protocolIEs, sourceIe);
    asn::SequenceAdd(body->protocolIEs, targetIe);
    asn::SequenceAdd(body->protocolIEs, listIe);
    
    // Create Xn Initiating message IE

    auto *init = asn::New<ASN_XNAP_InitiatingMessage_t>();
    init->procedureCode = XN_PROC_SN_STATUS_TRANSFER;
    init->criticality = ASN_XNAP_Criticality_ignore;
    if (ANY_fromType_aper(&init->value, &asn_DEF_ASN_XNAP_SNStatusTransfer, body) != 0)
    {
        asn::Free(asn_DEF_ASN_XNAP_SNStatusTransfer, body);
        asn::Free(asn_DEF_ASN_XNAP_InitiatingMessage, init);
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: failed to encode Status Transfer body", ueId);
        // TODO: error handling
        return;
    }

    asn::Free(asn_DEF_ASN_XNAP_SNStatusTransfer, body);
    
    // Create Outer PDU and encode for SCTP send

    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present = ASN_XNAP_XnAP_PDU_PR_initiatingMessage;
    outerPdu->choice.initiatingMessage = init;
    
    ssize_t encoded{}; 
    uint8_t *buffer{};
    
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        m_logger->err("UE[%ld] cannot send SNStatusTransfer: failed to encode Outer PDU", ueId);
        // TODO: error handling
        return;
    }
    
    // Send message using SCTP

    auto sctp = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctp->clientId = targetPeer->clientId; 
    sctp->buffer = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    sctp->stream = pendingHo->streamId;  // use the same stream as the handover request/ack
    m_base->xnSctpTask->push(std::move(sctp));
    
    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
    
    m_logger->info("UE[%ld] XnAP SNStatusTransfer sent with %zu DRBs", ueId,
                   drb_count);
}


/**
 * receiveHandoverCancel -  (called from transport.cpp)
 *  
 * Process an incoming HandoverCancel or ConditionalHandoverCancel message from a gNB.  If the message matches
 * a pending handover request, it will be forwarded to the RRC task for further processing.  If no match is found,
 * a warning is logged and the message is ignored.
 * 
 * @param gnbId Source gNB ID
 * @param pdu XnAP PDU containing the HandoverCancel message
 * @return void
 */
void XnTask::receiveHandoverCancel(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    if (pdu->present != ASN_XNAP_XnAP_PDU_PR_initiatingMessage || !pdu->choice.initiatingMessage) 
    {
        m_logger->err("receive HandoverCancel: cannot process msg from gnbId=%d - invalid PDU format", gnbId);
        return;
    }

    // Check for ConditionalHandoverCancel vs HandoverCancel based on procedure code
    const bool isCho = pdu->choice.initiatingMessage->procedureCode == XN_PROC_CONDITIONAL_HANDOVER_CANCEL;
    
    asn_TYPE_descriptor_t *td = isCho ? &asn_DEF_ASN_XNAP_ConditionalHandoverCancel : &asn_DEF_ASN_XNAP_HandoverCancel;

    void *decoded = nullptr;
    auto decodeResult = aper_decode(nullptr, td, &decoded,
        reinterpret_cast<const uint8_t *>(pdu->choice.initiatingMessage->value.buf),
        static_cast<size_t>(pdu->choice.initiatingMessage->value.size), 0, 0);

    if (decodeResult.code != RC_OK || !decoded) 
    { 
        if (decoded) ASN_STRUCT_FREE(*td, decoded); 
        return; 
    }
    
    auto &ies = isCho ? reinterpret_cast<ASN_XNAP_ConditionalHandoverCancel_t *>(decoded)->protocolIEs
                      : reinterpret_cast<ASN_XNAP_HandoverCancel_t *>(decoded)->protocolIEs;
    
    int64_t sourceId = -1, targetId = -1;
    
    for (int i = 0; i < ies.list.count; ++i)
    {
        auto *ie = ies.list.array[i]; 
        if (!ie || !ie->value.buf) 
            continue;
        if (ie->id != XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID && ie->id != XNAP_IE_targetNG_RAN_node_UE_XnAP_ID) 
            continue;
        auto *id = xnap_encode::Decode<ASN_XNAP_NG_RANnodeUEXnAPID_t>(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID,
            reinterpret_cast<const uint8_t *>(ie->value.buf), static_cast<size_t>(ie->value.size));
        if (id) 
        { 
            (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID ? sourceId : targetId) = *id; asn::Free(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, id); 
        }
    }

    ASN_STRUCT_FREE(*td, decoded);
    
    // retrieve the pending handover request for this transaction ID

    auto found = m_pendingHandoversTargetByTxId.end();
    
    for (auto it = m_pendingHandoversTargetByTxId.begin(); it != m_pendingHandoversTargetByTxId.end(); ++it)
    {
        if (it->second.sourceGnbId == gnbId && it->second.sourceUeXnApId == sourceId &&
            (targetId < 0 || it->second.targetUeXnApId == targetId)) 
            { 
                found = it; 
                break; 
            }
    }


    if (found == m_pendingHandoversTargetByTxId.end()) 
    { 
        m_logger->warn("Unmatched Xn HandoverCancel from gnbId=%d", gnbId); 
        return; 
    }
    
    // Check if the handover execution has already succeeded.  If so, we cannot cancel it, and must ignore the cancel message.
    if (found->second.executionSucceeded)
    {
        m_logger->warn("UE[%ld] late HandoverCancel ignored after execution success", found->second.ueId);
        return;
    }

    // Notify RRC layer of the handover cancel.  The RRC layer will handle any necessary cleanup and notify NGAP if needed.
    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_CANCEL_RECEIVED);
    msg->ueId = found->second.ueId; msg->isCho = isCho; 
    m_base->rrcTask->push(std::move(msg));
    
    // remove the pending handover request (and release its SCTP stream), as it is now cancelled
    removeTargetPendingHandover(found->second.xnTxId);
}


/**
 * receiveSnStatusTransfer -  (called from transport.cpp)
 * 
 * Process an incoming SNStatusTransfer message from a gNB.  If the message matches a pending handover request,
 * it will be forwarded to the RRC task for further processing.  If no match is found, a warning is logged and the message is ignored.
 * 
 * @param gnbId Source gNB ID
 * @param pdu XnAP PDU containing the SNStatusTransfer message
 * @return void
 */
void XnTask::receiveSnStatusTransfer(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    // Decode the message PDU

    if (pdu->present != ASN_XNAP_XnAP_PDU_PR_initiatingMessage ||
        !pdu->choice.initiatingMessage || !pdu->choice.initiatingMessage->value.buf)
    {
        m_logger->err("receiveSnStatusTransfer: malformed PDU from gnbId=%d", gnbId);
        return;
    }

    auto *body = xnap_encode::Decode<ASN_XNAP_SNStatusTransfer_t>(
        asn_DEF_ASN_XNAP_SNStatusTransfer,
        reinterpret_cast<const uint8_t *>(pdu->choice.initiatingMessage->value.buf),
        static_cast<size_t>(pdu->choice.initiatingMessage->value.size));
    
    if (!body)
    {
        m_logger->err("receiveSnStatusTransfer: failed to decode SNStatusTransfer from gnbId=%d", gnbId);
        return;
    }

    // Extract the source and target UE XnAP IDs and the DRB status list from the message

    int64_t sourceUeId = -1, targetUeId = -1;
    std::vector<DrbSnStatus> status;

    for (int i = 0; i < body->protocolIEs.list.count; ++i)
    {
        auto *ie = body->protocolIEs.list.array[i];
        if (!ie || !ie->value.buf) 
            continue;
    
        if (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID || ie->id == XNAP_IE_targetNG_RAN_node_UE_XnAP_ID)
        {
            auto *id = xnap_encode::Decode<ASN_XNAP_NG_RANnodeUEXnAPID_t>(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID,
                reinterpret_cast<const uint8_t *>(ie->value.buf), static_cast<size_t>(ie->value.size));
            if (id)
            {
                (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID ? sourceUeId : targetUeId) = *id;
                asn::Free(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, id);
            }
        }
        else if (ie->id == XNAP_IE_DRBS_SUBJECT_TO_STATUS_TRANSFER)
        {
            auto *list = xnap_encode::Decode<ASN_XNAP_DRBsSubjectToStatusTransfer_List_t>(
                asn_DEF_ASN_XNAP_DRBsSubjectToStatusTransfer_List,
                reinterpret_cast<const uint8_t *>(ie->value.buf), static_cast<size_t>(ie->value.size));
            if (!list) 
                continue;
            
            for (int j = 0; j < list->list.count; ++j)
            {
                auto *item = list->list.array[j];
                if (!item || item->pdcpStatusTransfer_UL.present != ASN_XNAP_DRBBStatusTransferChoice_PR_pdcp_sn_18bits ||
                    item->pdcpStatusTransfer_DL.present != ASN_XNAP_DRBBStatusTransferChoice_PR_pdcp_sn_18bits)
                    continue;
                auto combine = [](const ASN_XNAP_DRBBStatusTransfer18bitsSN_t *v) -> uint32_t {
                    return (static_cast<uint32_t>(v->cOUNTValue.hfn_PDCP_SN18) << 18) |
                           static_cast<uint32_t>(v->cOUNTValue.pdcp_SN18);
                };
                status.push_back(DrbSnStatus{static_cast<uint8_t>(item->drbID),
                    combine(item->pdcpStatusTransfer_UL.choice.pdcp_sn_18bits),
                    combine(item->pdcpStatusTransfer_DL.choice.pdcp_sn_18bits)});
            }

            asn::Free(asn_DEF_ASN_XNAP_DRBsSubjectToStatusTransfer_List, list);
        }
    }

    asn::Free(asn_DEF_ASN_XNAP_SNStatusTransfer, body);

    XnPendingHandover *pending = nullptr;
    for (auto &[txId, candidate] : m_pendingHandoversTargetByTxId)
        if (candidate.sourceGnbId == gnbId && candidate.sourceUeXnApId == sourceUeId &&
            candidate.targetUeXnApId == targetUeId) { pending = &candidate; break; }
    if (!pending || status.empty())
    {
        m_logger->err("receiveSnStatusTransfer: unable to match pending handover request (gnbId=%d, ueid=%ld)", gnbId, sourceUeId);
        return;
    }

    // send msg to RRC layer

    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::SN_STATUS_TRANSFER_RECEIVED);
    msg->ueId = pending->ueId;
    msg->drbSnStatus = std::move(status);
    m_base->rrcTask->push(std::move(msg));
}


/**
 * receiveHandoverRequestAck -  (called from transport.cpp)
 * 
 * Process an incoming HandoverRequestAcknowledge message from a gNB.  If the message is valid, 
 * it will be decoded and forwarded to the RRC task for further processing.
 * 
 * @param gnbId Source gNB ID
 * @param pdu XnAP PDU containing the HandoverRequestAcknowledge message
 * @return void
 */
void XnTask::receiveHandoverRequestAck(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("receiveHandoverRequestAck: received from gnbId=%d", gnbId);

    if (pdu->present != ASN_XNAP_XnAP_PDU_PR_successfulOutcome ||
        !pdu->choice.successfulOutcome ||
        !pdu->choice.successfulOutcome->value.buf)
    {
        m_logger->err("receiveHandoverRequestAck: malformed PDU from gnbId=%d", gnbId);
        return;
    }

    auto *succMsg = pdu->choice.successfulOutcome;
    auto *hoAck = xnap_encode::Decode<ASN_XNAP_HandoverRequestAcknowledge_t>(
        asn_DEF_ASN_XNAP_HandoverRequestAcknowledge,
        reinterpret_cast<const uint8_t *>(succMsg->value.buf),
        static_cast<size_t>(succMsg->value.size));

    if (!hoAck)
    {
        m_logger->err("receiveHandoverRequestAck: failed to decode HandoverRequestAcknowledge from gnbId=%d", gnbId);
        return;
    }

    int64_t sourceUeId = -1;
    int64_t targetUeXnApId = -1;
    std::unique_ptr<OctetString> rrcContainer;
    std::vector<std::pair<int, GtpTunnel>> forwardingTunnels;

    // Extract the relevant IEs from the HandoverRequestAcknowledge message

    for (int i = 0; i < hoAck->protocolIEs.list.count; ++i)
    {
        auto *ie = hoAck->protocolIEs.list.array[i];
        if (!ie || !ie->value.buf)
            continue;

        // IE - source NG-RAN node UE XnAP ID (id=73, mandatory, criticality=reject)

        if (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID)
        {
            auto *srcId = xnap_encode::Decode<ASN_XNAP_NG_RANnodeUEXnAPID_t>(
                asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (srcId)
            {
                sourceUeId = static_cast<int64_t>(*srcId);
                asn::Free(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, srcId);
            }
        }

        // IE - target NG-RAN node UE XnAP ID (id=74, mandatory, criticality=reject)

        else if (ie->id == XNAP_IE_targetNG_RAN_node_UE_XnAP_ID)
        {
            auto *targetId = xnap_encode::Decode<ASN_XNAP_NG_RANnodeUEXnAPID_t>(
                asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (targetId)
            {
                targetUeXnApId = static_cast<int64_t>(*targetId);
                asn::Free(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, targetId);
            }
        }

        // IE - PDU Session Resources Admitted List (id=142, optional, criticality=ignore)

        else if (ie->id == XNAP_IE_PDUSessionResourcesAdmitted_List)
        {
            auto *admittedList = xnap_encode::Decode<ASN_XNAP_PDUSessionResourcesAdmitted_List_t>(
                asn_DEF_ASN_XNAP_PDUSessionResourcesAdmitted_List,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (!admittedList)
            {
                m_logger->warn("receiveHandoverRequestAck: failed to decode admitted list from gnbId=%d", gnbId);
                continue;
            }

            for (int j = 0; j < admittedList->list.count; ++j)
            {
                auto *item = admittedList->list.array[j];
                if (!item || !item->pduSessionResourceAdmittedInfo.dataForwardingInfoFromTarget)
                    continue;

                auto *dlFwd = item->pduSessionResourceAdmittedInfo
                                  .dataForwardingInfoFromTarget->pduSessionLevelDLDataForwardingInfo;
                if (!dlFwd ||
                    dlFwd->present != ASN_XNAP_UPTransportLayerInformation_PR_gtpTunnel ||
                    !dlFwd->choice.gtpTunnel)
                    continue;

                auto *t = dlFwd->choice.gtpTunnel;
                GtpTunnel fwdTunnel;
                fwdTunnel.address = asn::GetOctetString(t->tnl_address);
                fwdTunnel.teid    = static_cast<uint32_t>(asn::GetOctet4(t->gtp_teid));

                m_logger->info("UE[%ld]: receiveHandoverRequestAck - PSI=%ld DL forwarding tunnel: addr=%s teid=0x%08x",
                               sourceUeId, (long)item->pduSessionId,
                               utils::OctetStringToIp(fwdTunnel.address).c_str(), fwdTunnel.teid);

                // Do not mutate GTP while the ACK is only partially decoded.
                // Apply tunnels after both UE IDs and peer correlation pass.
                forwardingTunnels.emplace_back(static_cast<int>(item->pduSessionId), std::move(fwdTunnel));
            }

            asn::Free(asn_DEF_ASN_XNAP_PDUSessionResourcesAdmitted_List, admittedList);
        }

        // IE - Target to Source Transparent Container (id=143, optional, criticality=ignore)

        else if (ie->id == XNAP_IE_Target2SourceTranspContainer)
        {
            auto *rrcOs = xnap_encode::Decode<OCTET_STRING_t>(
                asn_DEF_OCTET_STRING,
                reinterpret_cast<const uint8_t *>(ie->value.buf),
                static_cast<size_t>(ie->value.size));
            if (rrcOs)
            {
                rrcContainer = std::make_unique<OctetString>(asn::GetOctetString(*rrcOs));
                asn::Free(asn_DEF_OCTET_STRING, rrcOs);
            }
        }
    }

    asn::Free(asn_DEF_ASN_XNAP_HandoverRequestAcknowledge, hoAck);

    if (sourceUeId < 0)
    {
        m_logger->err("receiveHandoverRequestAck: missing sourceNG-RANnodeUEXnAPID from gnbId=%d", gnbId);
        // TODO: error handling
        return;
    }

    if (!rrcContainer)
    {
        m_logger->warn("UE[%ld]: receiveHandoverRequestAck - no RRC container from gnbId=%d", sourceUeId, gnbId);
        // TODO: error handing
        return;
    }

    if (targetUeXnApId < 0)
    {
        m_logger->err("UE[%ld]: HandoverRequestAck missing targetNG-RANnodeUEXnAPID", sourceUeId);
        // TODO: error handling
        return;
    }

    // retrieve pending handover state for this UE and peer gNB (map is keyed by (ueId, targetGnbId))

    auto pendingIt = m_pendingHandoversSourceByUeId.find(std::make_pair(sourceUeId, static_cast<int64_t>(gnbId)));
    if (pendingIt == m_pendingHandoversSourceByUeId.end())
    {
        m_logger->err("UE[%ld]: unmatched HandoverRequestAck from gnbId=%d", sourceUeId, gnbId);
        return;
    }

    auto *pendingHo = &pendingIt->second;
    
    // store handover ack information in the pending handover state for this UE.

    pendingHo->targetUeXnApId = targetUeXnApId;
    pendingHo->ackReceived = true;

    // store new timestamp.  Used to determine trip of overall timer.
    pendingHo->timestamp = static_cast<uint64_t>(utils::CurrentTimeMillis());

    // setup forwarding tunnels for each admitted PDU session.  This will be sent to the GTP task for processing.

    for (auto &[psi, tunnel] : forwardingTunnels)
    {
        auto gm = std::make_unique<NmGnbXnToGtp>(NmGnbXnToGtp::FORWARDING_TUNNEL_SETUP);
        gm->ueId = sourceUeId;
        gm->psi = psi;
        gm->forwardingTunnel = std::move(tunnel);
        m_base->gtpTask->push(std::move(gm));
    }

    // send msg to RRC layer to indicate that the handover request has been acknowledged and the RRC container is available for processing.
    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_REQUEST_ACK_RECEIVED);
    msg->ueId         = sourceUeId;
    msg->targetNci    = pendingHo->targetNci;
    msg->isCho        = pendingHo->isCho;
    msg->rrcContainer = std::move(rrcContainer);
    m_base->rrcTask->push(std::move(msg));


    m_logger->info("UE[%ld]: HandoverRequestAck processed from gnbId=%d, forwarded to RRC", sourceUeId, gnbId);
}

/**
 * receiveHandoverPreparationFailure -  (called from transport.cpp)
 * 
 * Process an incoming HandoverPreparationFailure message from a gNB.  If the message matches a pending handover request,
 * it will be forwarded to the RRC task for further processing.  If no match is found, a warning is logged and the message is ignored.
 * 
 * @param gnbId Source gNB ID
 * @param pdu XnAP PDU containing the HandoverPreparationFailure message
 * @return void
 */
void XnTask::receiveHandoverPreparationFailure(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    if (pdu->present != ASN_XNAP_XnAP_PDU_PR_unsuccessfulOutcome || !pdu->choice.unsuccessfulOutcome) return;

    auto *body = xnap_encode::Decode<ASN_XNAP_HandoverPreparationFailure_t>(asn_DEF_ASN_XNAP_HandoverPreparationFailure,
        reinterpret_cast<const uint8_t *>(pdu->choice.unsuccessfulOutcome->value.buf),
        static_cast<size_t>(pdu->choice.unsuccessfulOutcome->value.size));
    if (!body) return;

    int64_t sourceId = -1; 
    int reason = ASN_XNAP_Cause_PR_NOTHING;
    
    // extract the source UE XnAP ID and cause from the HandoverPreparationFailure message

    for (int i = 0; i < body->protocolIEs.list.count; ++i)
    {
        auto *ie = body->protocolIEs.list.array[i]; 
        if (!ie || !ie->value.buf) continue;
    
        // IE - source NG-RAN node UE XnAP ID (id=73, mandatory, criticality=reject)

        if (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID)
        { 
            auto *id = xnap_encode::Decode<ASN_XNAP_NG_RANnodeUEXnAPID_t>(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, 
                reinterpret_cast<const uint8_t *>(ie->value.buf), 
                ie->value.size); 
            if (id) 
            { 
                sourceId = *id; 
                asn::Free(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, id); 
            } 
        }

        // IE - cause (id=142, mandatory, criticality=reject)

        else if (ie->id == XNAP_IE_Cause_HO)
        { 
            auto *c = xnap_encode::Decode<ASN_XNAP_Cause_t>(asn_DEF_ASN_XNAP_Cause, 
                reinterpret_cast<const uint8_t *>(ie->value.buf), 
                ie->value.size); 
            if (c) 
            { 
                reason = c->present; 
                asn::Free(asn_DEF_ASN_XNAP_Cause, c); 
            } 
        }
    }

    asn::Free(asn_DEF_ASN_XNAP_HandoverPreparationFailure, body);
    
    auto pending = m_pendingHandoversSourceByUeId.find(std::make_pair(sourceId, static_cast<int64_t>(gnbId)));
    if (pending == m_pendingHandoversSourceByUeId.end())
    {
        m_logger->warn("UE[%ld]: unmatched HandoverPreparationFailure from gnbId=%d", sourceId, gnbId);
        return;
    }

    // Send msg to RRC layer to indicate that the handover preparation has failed.  
    //  The RRC layer will handle any necessary cleanup and notify NGAP if needed.
    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_PREPARATION_FAILURE_RECEIVED);
    msg->ueId = sourceId; 
    msg->targetNci = pending->second.targetNci; 
    msg->isCho = pending->second.isCho; 
    msg->reason = reason;
    m_base->rrcTask->push(std::move(msg));
    
    // remove the pending handover request (and release its SCTP stream), as it has
    // failed and is no longer valid
    removeSourcePendingHandover(sourceId, gnbId);
}

/**
 * receiveUeContextRelease -  (called from transport.cpp)
 * 
 * Process an incoming UEContextRelease message from a gNB.  Send by the target gNB when the
 * AMF has completed the path switch.  If the message matches a pending handover request,
 * it will be forwarded to the RRC task for further processing.  If no match is found, a warning is logged and the message is ignored.
 * 
 * @param gnbId Source gNB ID
 * @param pdu XnAP PDU containing the UEContextRelease message
 * @return void
 */
void XnTask::receiveUeContextRelease(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    if (pdu->present != ASN_XNAP_XnAP_PDU_PR_initiatingMessage ||
        !pdu->choice.initiatingMessage || !pdu->choice.initiatingMessage->value.buf)
    {
        m_logger->err("receiveUeContextRelease: malformed PDU from gnbId=%d", gnbId);
        return;
    }

    auto *release = xnap_encode::Decode<ASN_XNAP_UEContextRelease_t>(
        asn_DEF_ASN_XNAP_UEContextRelease,
        reinterpret_cast<const uint8_t *>(pdu->choice.initiatingMessage->value.buf),
        static_cast<size_t>(pdu->choice.initiatingMessage->value.size));
    if (!release)
    {
        m_logger->err("receiveUeContextRelease: decode failed from gnbId=%d", gnbId);
        return;
    }

    int64_t sourceUeId = -1;
    int64_t targetUeId = -1;
    
    // Extract the source and target UE XnAP IDs from the UEContextRelease message

    for (int i = 0; i < release->protocolIEs.list.count; ++i)
    {
        auto *ie = release->protocolIEs.list.array[i];
        if (!ie || !ie->value.buf)
            continue;
        if (ie->id != XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID &&
            ie->id != XNAP_IE_targetNG_RAN_node_UE_XnAP_ID)
            continue;

        auto *id = xnap_encode::Decode<ASN_XNAP_NG_RANnodeUEXnAPID_t>(
            asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID,
            reinterpret_cast<const uint8_t *>(ie->value.buf), static_cast<size_t>(ie->value.size));
        if (id)
        {
            if (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID)
                sourceUeId = static_cast<int64_t>(*id);
            else
                targetUeId = static_cast<int64_t>(*id);
            asn::Free(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, id);
        }
    }
    asn::Free(asn_DEF_ASN_XNAP_UEContextRelease, release);

    // Validate the complete UE-ID pair and peer before 
    //    deleting source RLS/RRC/NGAP/GTP contexts.
    
    auto outgoing = m_pendingHandoversSourceByUeId.find(std::make_pair(sourceUeId, static_cast<int64_t>(gnbId)));
    if (outgoing == m_pendingHandoversSourceByUeId.end() && m_completedSourceReleases.count(sourceUeId))
    {
        // only log a warning - possible that source has timed out the pending handover
        m_logger->warn("receiveUeContextRelease: no pending handover found for UE[%ld], Xn UEContextRelease ignored", sourceUeId);
        return;
    }

    if (outgoing == m_pendingHandoversSourceByUeId.end() || outgoing->second.targetUeXnApId != targetUeId)
    {
        m_logger->err("receiveUeContextRelease: unmatched IDs source=%ld target=%ld gnbId=%d",
                      sourceUeId, targetUeId, gnbId);
        return;
    }

    // send msg to RRC layer to indicate that the UEContextRelease has been received and validated.

    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::UE_CONTEXT_RELEASE_RECEIVED);
    msg->ueId = sourceUeId;
    msg->targetNci = outgoing->second.targetNci;
    m_base->rrcTask->push(std::move(msg));

    // remove the pending handover request (and release its SCTP stream), as it has
    // been completed
    removeSourcePendingHandover(sourceUeId, gnbId);

    // TODO: not sure what this is doing, but it seems to be tracking completed releases?
    m_completedSourceReleases.insert(sourceUeId);

    m_logger->info("UE[%ld] validated Xn UEContextRelease forwarded to source RRC", sourceUeId);
}


/**
 * receiveHandoverSuccess -  (called from transport.cpp)
 * 
 * Process an incoming HandoverSuccess message from a gNB.  Send by the target gNB when the
 * UE has connected to the target gNB.  If the message matches a pending handover request,
 * it will be forwarded to the RRC task for further processing.  If no match is found, a warning is logged and the message is ignored.
 * 
 * @param gnbId Source gNB ID
 * @param pdu XnAP PDU containing the HandoverSuccess message
 * @return void
 */
void XnTask::receiveHandoverSuccess(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    if (pdu->present != ASN_XNAP_XnAP_PDU_PR_initiatingMessage || !pdu->choice.initiatingMessage) 
    {
        m_logger->err("receiveHandoverSuccess: Invalid PDU format");
        return;
    }
    
    auto *body = xnap_encode::Decode<ASN_XNAP_HandoverSuccess_t>(asn_DEF_ASN_XNAP_HandoverSuccess,
        reinterpret_cast<const uint8_t *>(pdu->choice.initiatingMessage->value.buf),
        static_cast<size_t>(pdu->choice.initiatingMessage->value.size));
    if (!body)
    {
        m_logger->err("receiveHandoverSuccess: Failed to decode HandoverSuccess message");
        return;
    }

    int64_t sourceUeId = -1, targetUEId = -1, requestedNci = -1;
    
    // Extract the source and target UE XnAP IDs and the requested target cell global ID from the HandoverSuccess message

    for (int i = 0; i < body->protocolIEs.list.count; ++i)
    {
        auto *ie = body->protocolIEs.list.array[i]; 
        if (!ie || !ie->value.buf) continue;
        
        // IE - source NG-RAN node UE XnAP ID (id=73, mandatory, criticality=reject)
        // IE - target NG-RAN node UE XnAP ID (id=74, mandatory, criticality=reject)

        if (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID || ie->id == XNAP_IE_targetNG_RAN_node_UE_XnAP_ID)
        {
            auto *id = xnap_encode::Decode<ASN_XNAP_NG_RANnodeUEXnAPID_t>(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID,
                reinterpret_cast<const uint8_t *>(ie->value.buf), 
                static_cast<size_t>(ie->value.size));
            if (id) 
            { 
                (ie->id == XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID ? sourceUeId : targetUEId) = *id; 
                asn::Free(asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, id); 
            }
        }
        
        // IE - target cell global ID (id=75, optional, criticality=ignore)

        else if (ie->id == XNAP_IE_REQUESTED_TARGET_CELL_GLOBAL_ID)
        {
            auto *cgi = xnap_encode::Decode<ASN_XNAP_Target_CGI_t>(asn_DEF_ASN_XNAP_Target_CGI,
                reinterpret_cast<const uint8_t *>(ie->value.buf), 
                static_cast<size_t>(ie->value.size));
            if (cgi && cgi->present == ASN_XNAP_Target_CGI_PR_nr && cgi->choice.nr)
                requestedNci = asn::GetBitStringLong<36>(cgi->choice.nr->nr_CI);
            if (cgi) 
            {
                asn::Free(asn_DEF_ASN_XNAP_Target_CGI, cgi);
            }
        }
    }
    asn::Free(asn_DEF_ASN_XNAP_HandoverSuccess, body);
    
    // Find the pending handover state

    auto pending = m_pendingHandoversSourceByUeId.find(std::make_pair(sourceUeId, static_cast<int64_t>(gnbId)));
    if (pending == m_pendingHandoversSourceByUeId.end() ||
        pending->second.targetUeXnApId != targetUEId || pending->second.targetNci != requestedNci)
    { 
        // TODO: is this a warn or error?  If the UE has moved, should we inform RRC even if the Xn pending state is gone?
        m_logger->warn("Unmatched Xn HandoverSuccess from gnbId=%d", gnbId); 
        return; 
    }
    
    // send msg to RRC layer to indicate that the handover has succeeded.
    
    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_SUCCESS_RECEIVED);
    msg->ueId = sourceUeId; 
    msg->targetNci = requestedNci; 
    m_base->rrcTask->push(std::move(msg));
    
    // set Xn handover state to indicate that the handover has succeeded.
    pending->second.executionSucceeded = true;
}


/**
 * sendUeContextRelease -  (called from task.cpp - RrcToXn::UE_CONTEXT_RELEASE)
 * 
 * Send an XnAP UEContextRelease message to the source gNB for a given UE.  
 * This is typically called after the handover has completed and the AMF has notified the 
 * target gNB that the path switch has been completed.
 * 
 * @param ueId The UE ID for which the UEContextRelease is to be sent
 * @param targetNci The source cell global ID (NCI)
 * @return void
 */
void XnTask::sendUeContextRelease(int64_t ueId, int64_t targetNci)
{
    // retrieve the pending handover state for this UE.  If no pending handover is found, log an error and return.

    XnPendingHandover *pending = nullptr;
    for (auto &[txId, candidate] : m_pendingHandoversTargetByTxId)
    {
        if (candidate.role == XnPendingHandover::Role::TARGET && candidate.targetUeXnApId == ueId)
        {
            pending = &candidate;
            break;
        }
    }
    if (!pending)
    {
        m_logger->err("sendUeContextRelease: UE[%ld] cannot send UEContextRelease - pending state missing", ueId);
        return;
    }

    auto *sourcePeer = m_xnPeerTable.getPeerInfo(pending->sourceGnbId);
    if (!sourcePeer || sourcePeer->connectionState != EXnConnectionState::CONNECTED)
    {
        m_logger->err("sendUeContextRelease: UE[%ld] cannot send UEContextRelease - source peer unavailable", ueId);
        return;
    }

    // IE - source NG-RAN node UE XnAP ID (id=73, mandatory, criticality=reject)
    // IE - target NG-RAN node UE XnAP ID (id=74, mandatory, criticality=reject)

    auto makeIdIe = [](long ieId, int64_t value) {
        auto *ie = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
        ie->id = ieId;
        ie->criticality = ASN_XNAP_Criticality_reject;
        ASN_XNAP_NG_RANnodeUEXnAPID_t id = static_cast<ASN_XNAP_NG_RANnodeUEXnAPID_t>(value);
        if (!setHoIeValue(ie, &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &id))
        {
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ie);
            return static_cast<ASN_XNAP_ProtocolIE_Field_14202P0_t *>(nullptr);
        }
        return ie;
    };

    auto *sourceIe = makeIdIe(XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID, pending->sourceUeXnApId);
    auto *targetIe = makeIdIe(XNAP_IE_targetNG_RAN_node_UE_XnAP_ID, pending->targetUeXnApId);
    if (!sourceIe || !targetIe)
    {
        if (sourceIe) asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe);
        if (targetIe) asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, targetIe);
        m_logger->err("sendUeContextRelease: UE[%ld] failed to encode UEContextRelease IDs", ueId);
        return;
    }

    // create UE ContextRelease message and encode it into an XnAP PDU for sending to the source gNB.
    auto *body = asn::New<ASN_XNAP_UEContextRelease_t>();
    asn::SequenceAdd(body->protocolIEs, sourceIe);
    asn::SequenceAdd(body->protocolIEs, targetIe);
    
    auto *init = asn::New<ASN_XNAP_InitiatingMessage_t>();
    init->procedureCode = XN_PROC_UE_CONTEXT_RELEASE;
    init->criticality = ASN_XNAP_Criticality_reject;
    if (ANY_fromType_aper(&init->value, &asn_DEF_ASN_XNAP_UEContextRelease, body) != 0)
    {
        asn::Free(asn_DEF_ASN_XNAP_UEContextRelease, body);
        asn::Free(asn_DEF_ASN_XNAP_InitiatingMessage, init);
        m_logger->err("sendUeContextRelease: UE[%ld] failed to encode UEContextRelease message", ueId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_UEContextRelease, body);

    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present = ASN_XNAP_XnAP_PDU_PR_initiatingMessage;
    outerPdu->choice.initiatingMessage = init;
    
    ssize_t encoded{};
    uint8_t *buffer{};
    
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        m_logger->err("sendUeContextRelease: UE[%ld] failed to encode XnAP PDU", ueId);
        return;
    }

    // Send the encoded XnAP PDU to the source gNB via SCTP.
    auto sctp = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctp->clientId = sourcePeer->clientId;
    sctp->stream = pending->streamId;
    sctp->buffer = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    const int sourceGnbId = pending->sourceGnbId;
    const uint32_t completedTxId = pending->xnTxId;
    m_base->xnSctpTask->push(std::move(sctp));
    
    
    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);

    // UEContextRelease is the end of the handover process.
    // We can remove the pending handover state (and release its SCTP stream).
    removeTargetPendingHandover(completedTxId);

    m_logger->info("UE[%ld] XnAP UEContextRelease sent to source gNB %d", ueId, sourceGnbId);

}

/**
 * sendHandoverSuccess -  (called from task.cpp - RrcToXn::HANDOVER_SUCCESS)
 * 
 * Send an XnAP HandoverSuccess message to the source gNB for a given UE.  
 * This is typically called after the UE has successfully connected to the target gNB.
 * 
 * @param ueId The UE ID for which the HandoverSuccess is to be sent.
 * @param targetNci The source cell global ID (NCI).
 * @return void
 */
void XnTask::sendHandoverSuccess(int64_t ueId, int64_t targetNci)
{
    
    // retrieve the pending handover state for this UE.  If no pending handover is found, log a warning and return.
    XnPendingHandover *pending = nullptr;
    for (auto &[txId, candidate] : m_pendingHandoversTargetByTxId)
        if (candidate.role == XnPendingHandover::Role::TARGET && candidate.targetUeXnApId == ueId) 
        { 
            pending = &candidate; 
            break;
        }
    if (!pending) 
    { 
        m_logger->warn("sendHandoverSuccess: UE[%ld] cannot send HandoverSuccess - missing pending handover state", ueId); 
        return; 
    }

    // set the successful handover flag
    pending->executionSucceeded = true;
    
    // find source gNB in peer table

    auto *peer = m_xnPeerTable.getPeerInfo(pending->sourceGnbId);
    if (!peer || peer->connectionState != EXnConnectionState::CONNECTED)
    {
        m_logger->warn("sendHandoverSuccess: UE[%ld] cannot send HandoverSuccess - source gNB not connected", ueId);
        return;
    }

    // IE - source NG-RAN node UE XnAP ID (id=73, mandatory, criticality=reject)
    // IE - target NG-RAN node UE XnAP ID (id=74, mandatory, criticality=reject)

    auto makeId = [](long ieId, int64_t value) {
        auto *ie = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(); 
        ie->id = ieId; 
        ie->criticality = ASN_XNAP_Criticality_reject;
        ASN_XNAP_NG_RANnodeUEXnAPID_t id = value;
        if (!setHoIeValue(ie, &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &id)) 
        { 
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ie); 
            return static_cast<ASN_XNAP_ProtocolIE_Field_14202P0_t *>(nullptr); 
        }
        return ie;
    };

    auto *sourceIe = makeId(XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID, pending->sourceUeXnApId);
    auto *targetIe = makeId(XNAP_IE_targetNG_RAN_node_UE_XnAP_ID, pending->targetUeXnApId);

    // IE - requested target cell global ID (id=75, optional, criticality=ignore)

    auto *cgiValue = asn::New<ASN_XNAP_Target_CGI_t>(); 
    cgiValue->present = ASN_XNAP_Target_CGI_PR_nr;
    cgiValue->choice.nr = asn::New<ASN_XNAP_NR_CGI_t>(); 
    setXnPlmn(cgiValue->choice.nr->plmn_id, m_base->config->plmn);
    asn::SetBitStringLong<36>(m_base->config->nci, cgiValue->choice.nr->nr_CI);
    
    auto *cgiIe = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>(); 
    cgiIe->id = XNAP_IE_REQUESTED_TARGET_CELL_GLOBAL_ID;
    cgiIe->criticality = ASN_XNAP_Criticality_reject;
    if (!sourceIe || !targetIe || !setHoIeValue(cgiIe, &asn_DEF_ASN_XNAP_Target_CGI, cgiValue))
    { 
        if (sourceIe) 
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, sourceIe); 
        if (targetIe) 
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, targetIe); 
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, cgiIe); 
        asn::Free(asn_DEF_ASN_XNAP_Target_CGI, cgiValue); 
        return; 
    }
    
    asn::Free(asn_DEF_ASN_XNAP_Target_CGI, cgiValue);
    
    auto *body = asn::New<ASN_XNAP_HandoverSuccess_t>(); 
    asn::SequenceAdd(body->protocolIEs, sourceIe);
    asn::SequenceAdd(body->protocolIEs, targetIe); 
    asn::SequenceAdd(body->protocolIEs, cgiIe);
    auto *init = asn::New<ASN_XNAP_InitiatingMessage_t>(); 
    init->procedureCode = XN_PROC_HANDOVER_SUCCESS; 
    init->criticality = ASN_XNAP_Criticality_ignore;
    if (ANY_fromType_aper(&init->value, &asn_DEF_ASN_XNAP_HandoverSuccess, body) != 0) 
    { 
        asn::Free(asn_DEF_ASN_XNAP_HandoverSuccess, body); 
        asn::Free(asn_DEF_ASN_XNAP_InitiatingMessage, init); 
        return; 
    }
    
    asn::Free(asn_DEF_ASN_XNAP_HandoverSuccess, body);
    
    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>(); 
    outerPdu->present = ASN_XNAP_XnAP_PDU_PR_initiatingMessage; 
    outerPdu->choice.initiatingMessage = init;
    
    ssize_t encoded{}; 
    uint8_t *buffer{};
    
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer)) 
    { 
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu); 
        return; 
    }
    
    auto sctp = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE); 
    sctp->clientId = peer->clientId; 
    sctp->stream = pending->streamId;
    sctp->buffer = UniqueBuffer{buffer, static_cast<size_t>(encoded)}; 
    m_base->xnSctpTask->push(std::move(sctp));
    
    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);

    m_logger->info("UE[%ld] XnAP HandoverSuccess sent", ueId);
}

// ---------------------------------------------------------------------------
// Handover Timer handlers
// ---------------------------------------------------------------------------


void XnTask::processHandoverTimeouts(int timerId)
{
    // grab current time in milliseconds for timeout calculations
    const uint64_t now = static_cast<uint64_t>(utils::CurrentTimeMillis());

    // timeout lists

    std::vector<std::pair<int64_t, int>> sourcePreparationExpired;         // (ueId, targetGnbId)
    std::vector<std::tuple<int64_t, int64_t, bool>> sourceOverallExpired;  // (ueId, targetNci, isCho)

    std::vector<std::pair<uint32_t, int64_t>> targetPreparationExpired;   // (XnTxId, ueId)
    std::vector<std::pair<uint32_t, int64_t>> targetOverallExpired;   // (XnTxId, ueId)

    // loop through the pending handover requests and check for timeouts.  
    //  If a timeout is detected, the UE ID is added to the appropriate list for further processing.
    
    // gNB acting as source (outgoing requests)
    // Note: xnAP only defines two timers: preparation and overall, both maintained by the source gNB.
    //   The preparation timer measures from handoverRequest to HandoverRequestAck or Handoverprepatation Failure
    //   The overall timer measures from HandoverRequestAck to UEContextRelease.

    for (auto &[ueid_gnbid_pair, pending] : m_pendingHandoversSourceByUeId)
    {
        uint64_t duration = now - pending.timestamp;

        // Preparation timer - no handover request ack received within the preparation timeout period
        if (!pending.ackReceived)
        {
            if (duration > HANDOVER_PREPARATION_TIMEOUT_MS && !pending.timeoutReported)
            {
                pending.timeoutReported = true;

                // send msg to RRC layer to indicate that the handover preparation has failed.
                auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_ABORT);
                msg->ueId = pending.ueId; 
                msg->targetNci = pending.targetNci; 
                msg->isCho = pending.isCho;
                msg->reason = NmGnbXnToRrc::ABORT_REASON_SOURCE_PREPARATION_TIMEOUT;
                m_base->rrcTask->push(std::move(msg));

                m_logger->warn("UE[%ld]: Xn source preparation timer expired", pending.ueId);

                // add to the list of expired preparation timers for further processing
                sourcePreparationExpired.push_back(std::make_pair(pending.ueId, pending.targetGnbId));
            }
        }

        // Overall timer - handover request ack received, but no UEContextRelease received within the overall timeout period
        if (pending.ackReceived && !pending.executionSucceeded)
        {
            if (duration > (pending.isCho ? HANDOVER_OVERALL_TIMEOUT_CHO_MS : HANDOVER_OVERALL_TIMEOUT_MS) && !pending.timeoutReported)
            {
                pending.timeoutReported = true;

                // send msg to RRC layer to indicate that the handover has failed due to overall timeout
                auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_ABORT);
                msg->ueId = pending.ueId; 
                msg->targetNci = pending.targetNci; 
                msg->isCho = pending.isCho;
                msg->reason = NmGnbXnToRrc::ABORT_REASON_SOURCE_OVERALL_TIMEOUT;
                m_base->rrcTask->push(std::move(msg));

                m_logger->warn("UE[%ld]: Xn source overall timer expired", pending.ueId);


                sourceOverallExpired.push_back(std::make_tuple(pending.ueId, pending.targetNci, pending.isCho));
            }
        }
    }

    // gNB acting as target (incoming requests)
    // XnAP does not define any timers for the target gNB, so these timers are implementation specific

    // RRC ACK Received - timer for receipt of HANDOVER_REQUEST_ACK from RRC layer
    //   If the timer expires, the target gNB will send a HandoverPreparationFailure message 
    //   to the source gNB and terminate the handover process.
    //   Should mirror the source gNB's preparation timer, but is maintained independently by the target gNB.

    // Xn Overall timer - failsafe timer to ensure cleanup of stale handover state in case of unexpected failures.
    //   Sends notification to RRC - RRC's timer should have expired already, so RRC can use this to ensure correct operation.
    //      Important that Xn-RRC are in sync, in case a UE tries to handover outside the timeout window - 
    //          example:if RRC accepts the handover, but Xn times out, the UE will have no userplane connectivity.
    //   No notification to source gNB is sent, as the source gNB should have already timed out and cleaned up its state.
    //   Should mirror the source gNB's overall timer, but is maintained independently by the target gNB.    
    
    for (auto &[txId, pending] : m_pendingHandoversTargetByTxId)
    {
        uint64_t duration = now - pending.timestamp;

        // insert timers here

        // RRC Ack timer - no send handover request ack received from RRC within the timeout period
        if (!pending.ackSent)
        {
            if (duration > HANDOVER_RRC_ACK_TIMEOUT_MS && !pending.timeoutReported)
            {
                // set this flag to avoid sending multiple timeout messages for the same pending handover
                pending.timeoutReported = true;

                // send msg to RRC layer to indicate that the handover preparation shoudl terminate
                auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_ABORT);
                msg->ueId = pending.ueId; 
                msg->targetNci = pending.targetNci; 
                msg->isCho = pending.isCho;
                msg->reason = NmGnbXnToRrc::ABORT_REASON_TARGET_PREPARATION_TIMEOUT;
                m_base->rrcTask->push(std::move(msg));

                m_logger->warn("UE[%ld]: Xn target preparation timer expired", pending.ueId);

                // add to the list of expired target timers for further processing
                targetPreparationExpired.push_back({txId, pending.ueId});
            }
        }

        // Target Overall Timer
        if (pending.ackSent && !pending.executionSucceeded)
        {
            if (duration > (pending.isCho ? HANDOVER_OVERALL_TARGET_TIMEOUT_CHO_MS : HANDOVER_OVERALL_TARGET_TIMEOUT_MS) && !pending.timeoutReported)
            {
                pending.timeoutReported = true;

                // send msg to RRC layer to indicate that the handover has failed due to overall timeout
                auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_ABORT);
                msg->ueId = pending.ueId; 
                msg->targetNci = pending.targetNci; 
                msg->isCho = pending.isCho;
                msg->reason = NmGnbXnToRrc::ABORT_REASON_TARGET_OVERALL_TIMEOUT;
                m_base->rrcTask->push(std::move(msg));

                m_logger->warn("UE[%ld]: Xn target overall timer expired", pending.ueId);
            
                targetOverallExpired.push_back({txId, pending.ueId});
            
            }
        }

    }


    for (const auto &[ueId, targetGnbId] : sourcePreparationExpired)
    {
        // removes the pending entry and releases its SCTP stream
        removeSourcePendingHandover(ueId, targetGnbId);
    }
    for (const auto &[ueId, targetNci, isCho] : sourceOverallExpired)
    {
        // send a HandoverCancel msg to the target gNB to cancel handover processing
        // sendHandoverCancel() will delete the pending handover state (releasing its
        // SCTP stream), so don't need to do it here
        //  (also why we call this here instead of in the loop above).
        // sendHandoverCancel() should also tear down the UP GTP forwarding tunnels

        sendHandoverCancel(ueId, targetNci, isCho);

    }

    for (const auto &[txId, ueId] : targetPreparationExpired)
    {

        m_logger->warn("UE[%ld]: Xn RRC preparation timer expired", ueId);

        // send msg to source gNB to indicate that the handover preparation has failed.

        // Note: this function will remove the pending handover state, so we don't need to do it here.
        //   (also why it has to happen here rather than above)

        // TODO: check this cause - is it correct for target-side timeout?
        sendHandoverPreparationFailure(txId, ASN_XNAP_Cause_PR_transport);
    }

    for (const auto &[txId, ueId] : targetOverallExpired)
    {
        m_logger->warn("UE[%ld]: Xn target overall timer expired", ueId);

        // No msg to source gNB to indicate that the handover has failed due to overall timeout.

        // remove the pending handover state
        removeTargetPendingHandover(txId);
    }

    // restart timer
    setTimer(TIMER_HANDOVERS_PENDING, HANDOVERS_PENDING_INTERVAL_MS);
}



} // namespace nr::gnb
