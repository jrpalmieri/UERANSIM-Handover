#include "task.hpp"
#include "encode.hpp"

#include <gnb/ngap/utils.hpp>
#include <gnb/gtp/task.hpp>
#include <gnb/rrc/task.hpp>
#include <gnb/ngap/task.hpp>
#include <gnb/gtp/utils.hpp>
#include <lib/asn/utils.hpp>

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

// IE type headers for HandoverRequest
#include <ASN_XNAP_HandoverRequest.h>
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
}

namespace nr::gnb
{

// ---------------------------------------------------------------------------
// Incoming from network — target gNB handlers
// ---------------------------------------------------------------------------


void XnTask::xnHandoverCancelTarget(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("xnHandoverCancelTarget gnbId=%d", gnbId);
}

void XnTask::receiveSnStatusTransfer(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("receiveSnStatusTransfer gnbId=%d", gnbId);
}


// ---------------------------------------------------------------------------
// Incoming from network — source gNB handlers
// ---------------------------------------------------------------------------

void XnTask::receiveHandoverRequestAck(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("receiveHandoverRequestAck gnbId=%d", gnbId);
}

void XnTask::receiveHandoverPreparationFailure(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("receiveHandoverPreparationFailure gnbId=%d", gnbId);
}

void XnTask::receiveUeContextRelease(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("receiveUeContextRelease gnbId=%d", gnbId);
}

void XnTask::receiveHandoverSuccess(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("receiveHandoverSuccess gnbId=%d", gnbId);
}

// -----------------------------------------------------------------------
// Procedure code for Handover Preparation (TS 38.423 Table 9.1-1)
// -----------------------------------------------------------------------
static constexpr long XN_PROC_HANDOVER_PREPARATION = 0;

// -----------------------------------------------------------------------
// IE IDs for HandoverRequest (TS 38.423 / XnAP-PDU-Contents ASN.1 rel-18)
// -----------------------------------------------------------------------
static constexpr long XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID = 73; // id-sourceNG-RANnodeUEXnAPID
static constexpr long XNAP_IE_Cause_HO                     = 7;  // id-Cause
static constexpr long XNAP_IE_targetCellGlobalID            = 78; // id-targetCellGlobalID
static constexpr long XNAP_IE_GUAMI                         = 15; // id-GUAMI
static constexpr long XNAP_IE_UEContextInfoHORequest        = 83; // id-UEContextInfoHORequest
static constexpr long XNAP_IE_UEHistoryInformation          = 88; // id-UEHistoryInformation

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


// ---------------------------------------------------------------------------
// Outgoing — source gNB (triggered by RrcToXn::HANDOVER_REQUEST_SEND)
//
// Builds and sends an XnAP HandoverRequest to the target gNB identified by
// targetNci.  The target gNB must already be present in the Xn peer table
// with an CONNECTED association state, otherwise the function logs an error
// and returns without sending.
// ---------------------------------------------------------------------------

void XnTask::sendHandoverRequest(int64_t ueId, int64_t targetNci, bool isCho, std::unique_ptr<GnbHandoverUeContexts> contexts)
{
    m_logger->debug("xnHandoverRequestSource ueId=%ld targetNci=0x%09lx isCho=%d",
                    ueId, targetNci, isCho);

    // -----------------------------------------------------------------------
    // 1. Find target peer in Xn peer table by NCI.
    // -----------------------------------------------------------------------
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
        m_logger->err("xnHandoverRequestSource: no Xn peer for targetNci=0x%09lx ueId=%ld",
                      targetNci, ueId);
        return;
    }

    if (targetPeer->connectionState != EXnConnectionState::CONNECTED)
    {
        m_logger->err("xnHandoverRequestSource: peer gnbId=%d (targetNci=0x%09lx) not CONNECTED "
                      "(state=%d), dropping HandoverRequest for ueId=%ld",
                      targetPeer->gnbId, targetNci,
                      static_cast<int>(targetPeer->connectionState), ueId);
        return;
    }

    // -----------------------------------------------------------------------
    // 2. Retrieve UE context from NGAP, GTP, and RRC tasks.
    //   The contexts are needed to build the HandoverRequest message and may be
    //   used by the target gNB to prepare the handover.
    //   Note: to execute this function, all contexts must be present, so no 
    //   error checking needed.
    // -----------------------------------------------------------------------
    auto *ngapUe = &*contexts->ngapUeContext;
    auto *rrcUe = &*contexts->rrcUeContext;
    auto *gtpUe = &*contexts->gtpUeContext;
    auto &pduSessions = contexts->pduSessions;

    const GnbConfig *cfg = m_base->config;

    /* -----------------------------------------------------------------------
     3. Build the RRC container (HandoverPreparationInformation / RRCReconfiguration).
        Either includes the HandoverPreparationInformation message as defined in 
        subclause 10.2.2. of TS 36.331 [14], or the HandoverPreparationInformation-NB message 
        as defined in subclause 10.6.2 of TS 36.331 [14], if the target NG-RAN node is an ng-eNB,
        or the HandoverPreparationInformation message as defined in subclause 11.2.2 
        of TS 38.331 [10], if the target NG-RAN node is a gNB.
       -----------------------------------------------------------------------
     */
    OctetString rrcContainer;

    // -----------------------------------------------------------------------
    // IE 1 — sourceNG-RANnodeUEXnAPID  (id=73, mandatory, criticality=reject)
    //   Opaque 32-bit UE identifier at the source gNB on the Xn interface.
    //   For the simulation, we use the full 64-bit UE ID to simplify tracking across 
    //   multiple gNBs (the IE allows for long values).
    // -----------------------------------------------------------------------
    ASN_XNAP_NG_RANnodeUEXnAPID_t srcUeXnId =
        static_cast<ASN_XNAP_NG_RANnodeUEXnAPID_t>(ueId);

    auto *ieSrcUeId = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieSrcUeId->id          = XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID;
    ieSrcUeId->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieSrcUeId, &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &srcUeXnId))
    {
        m_logger->err("UE[%ld]: xnHandoverRequestSource: failed to encode sourceNG-RANnodeUEXnAPID", ueId);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
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

    auto *ieCause = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieCause->id          = XNAP_IE_Cause_HO;
    ieCause->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieCause, &asn_DEF_ASN_XNAP_Cause, &cause))
    {
        m_logger->err("UE[%ld]: xnHandoverRequestSource: failed to encode Cause", ueId);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieCause);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
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

    auto *tgtNrCgi = asn::New<ASN_XNAP_NR_CGI_t>();
    setXnPlmn(tgtNrCgi->plmn_id, targetPlmn);
    asn::SetBitStringLong<36>(targetNci, tgtNrCgi->nr_CI);

    auto *targetCgi = asn::New<ASN_XNAP_Target_CGI_t>();
    targetCgi->present    = ASN_XNAP_Target_CGI_PR_nr;
    targetCgi->choice.nr  = tgtNrCgi;

    auto *ieTgtCell = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieTgtCell->id          = XNAP_IE_targetCellGlobalID;
    ieTgtCell->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieTgtCell, &asn_DEF_ASN_XNAP_Target_CGI, targetCgi))
    {
        m_logger->err("UE[%ld]: xnHandoverRequestSource: failed to encode targetCellGlobalID", ueId);
        asn::Free(asn_DEF_ASN_XNAP_Target_CGI, targetCgi);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTgtCell);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieCause);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_Target_CGI, targetCgi);

    // -----------------------------------------------------------------------
    // IE 4 — GUAMI  (id=15, mandatory, criticality=reject)
    //   Identifies the AMF that manages NAS for this UE so the target can route
    //   path-switch requests to the same AMF.
    //  Pull from UE's RRC Context
    // -----------------------------------------------------------------------
    auto *guami = asn::New<ASN_XNAP_GUAMI_t>();
    const Guami &g = rrcUe->guami;

    setXnPlmn(guami->plmn_ID, g.plmn);
    asn::SetBitStringInt<8> (g.amfRegionId, guami->amf_region_id);
    asn::SetBitStringInt<10>(g.amfSetId,    guami->amf_set_id);
    asn::SetBitStringInt<6> (g.amfPointer,  guami->amf_pointer);

    auto *ieGuami = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieGuami->id          = XNAP_IE_GUAMI;
    ieGuami->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieGuami, &asn_DEF_ASN_XNAP_GUAMI, guami))
    {
        m_logger->err("xnHandoverRequestSource: failed to encode GUAMI");
        asn::Free(asn_DEF_ASN_XNAP_GUAMI, guami);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGuami);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTgtCell);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieCause);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_GUAMI, guami);

    // -----------------------------------------------------------------------
    // IE 5 — UEContextInfoHORequest  (id=83, mandatory, criticality=reject)
    //   The nested structure carrying the UE's session context to the target.
    // -----------------------------------------------------------------------
    auto *ueCtxInfo = asn::New<ASN_XNAP_UEContextInfoHORequest_t>();

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
    // Assumption: use ngapIp from config.  This is a BIT_STRING of 32 bits (IPv4)
    // per TS 38.423 §9.3.2.1.  IPv6 would require 128 bits.
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
        if (contexts->rrcUeContext->ueSecurityInfoValid)
        {
            asn::SetBitStringInt<16>(contexts->rrcUeContext->ueSecInfo.nRencryptionAlgorithmsBitmap, sc.nr_EncyptionAlgorithms);
            asn::SetBitStringInt<16>(contexts->rrcUeContext->ueSecInfo.nRintegrityProtectionAlgorithmsBitmap, sc.nr_IntegrityProtectionAlgorithms);
            asn::SetBitStringInt<16>(contexts->rrcUeContext->ueSecInfo.eUTRAencryptionAlgorithmsBitmap, sc.e_utra_EncyptionAlgorithms);
            asn::SetBitStringInt<16>(contexts->rrcUeContext->ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap, sc.e_utra_IntegrityProtectionAlgorithms);
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
        if (contexts->rrcUeContext->ueSecurityInfoValid)
            std::memcpy(si.key_NG_RAN_Star.buf, contexts->rrcUeContext->ueSecInfo.k_gnb.data(), 32);
        else
            std::memset(si.key_NG_RAN_Star.buf, 0, 32);
        si.key_NG_RAN_Star.bits_unused = 0;
        si.ncc = contexts->rrcUeContext->ueSecurityInfoValid ? contexts->rrcUeContext->nextHopChainingCount : 0;
    }

    // --- 5e. ue-AMBR ---
    // Aggregate maximum bit rate for the UE.
    // Pulled from GTP UE context AMBR — it matches what the rate limiter is enforcing.
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
        auto *pduItem = asn::New<ASN_XNAP_PDUSessionResourcesToBeSetup_Item_t>();
        pduItem->pduSessionId = static_cast<ASN_XNAP_PDUSession_ID_t>(res->psi);

        // S-NSSAI — PduSessionResource does not cache per-session NSSAI (it is
        // carried in the NGAP setup transfer but not stored on the resource struct).
        // Assumption: use the first configured slice for all sessions.
        if (!cfg->nssai.slices.empty())
        {
            asn::SetOctetString1(pduItem->s_NSSAI.sst,
                                 static_cast<uint8_t>(cfg->nssai.slices[0].sst));
            if (cfg->nssai.slices[0].sd.has_value())
            {
                pduItem->s_NSSAI.sd = asn::New<OCTET_STRING_t>();
                asn::SetOctetString3(*pduItem->s_NSSAI.sd,
                                     octet3{cfg->nssai.slices[0].sd.value()});
            }
        }
        else
        {
            asn::SetOctetString1(pduItem->s_NSSAI.sst, 1); // SST=1 (eMBB) fallback
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
        bool addedFlow = false;
        if (res->qosFlows && res->qosFlows->list.count > 0)
        {
            auto &qosList = res->qosFlows->list;
            for (int iFlow = 0; iFlow < qosList.count; iFlow++)
            {
                auto *ngapFlow = qosList.array[iFlow];
                if (!ngapFlow)
                    continue;

                const auto &ngapQosChars =
                    ngapFlow->qosFlowLevelQosParameters.qosCharacteristics;
                const auto &ngapArp =
                    ngapFlow->qosFlowLevelQosParameters.allocationAndRetentionPriority;

                auto *xnNonDyn = asn::New<ASN_XNAP_NonDynamic5QIDescriptor_t>();
                if (ngapQosChars.present == ASN_NGAP_QosCharacteristics_PR_nonDynamic5QI
                    && ngapQosChars.choice.nonDynamic5QI)
                    xnNonDyn->fiveQI = ngapQosChars.choice.nonDynamic5QI->fiveQI;
                else
                    xnNonDyn->fiveQI = 9; // dynamic / unknown — use eMBB default

                auto *qosChars = asn::New<ASN_XNAP_QoSCharacteristics_t>();
                qosChars->present        = ASN_XNAP_QoSCharacteristics_PR_non_dynamic;
                qosChars->choice.non_dynamic = xnNonDyn;

                auto *arp = asn::New<ASN_XNAP_AllocationandRetentionPriority_t>();
                arp->priorityLevel             = ngapArp.priorityLevelARP;
                arp->pre_emption_capability    = ngapArp.pre_emptionCapability;
                arp->pre_emption_vulnerability = ngapArp.pre_emptionVulnerability;

                auto *qosParams = asn::New<ASN_XNAP_QoSFlowLevelQoSParameters_t>();
                qosParams->qos_characteristics    = *qosChars; free(qosChars);
                qosParams->allocationAndRetentionPrio = *arp;  free(arp);

                auto *qosFlowItem = asn::New<ASN_XNAP_QoSFlowsToBeSetup_Item_t>();
                qosFlowItem->qfi = static_cast<long>(ngapFlow->qosFlowIdentifier);
                qosFlowItem->qosFlowLevelQoSParameters = *qosParams; free(qosParams);

                asn::SequenceAdd(pduItem->qosFlowsToBeSetup_List, qosFlowItem);
                addedFlow = true;
            }
        }

        if (!addedFlow)
        {
            // Fallback: single default QoS flow when no NGAP flows are stored.
            auto *xnNonDyn = asn::New<ASN_XNAP_NonDynamic5QIDescriptor_t>();
            xnNonDyn->fiveQI = 9;
            auto *qosChars = asn::New<ASN_XNAP_QoSCharacteristics_t>();
            qosChars->present        = ASN_XNAP_QoSCharacteristics_PR_non_dynamic;
            qosChars->choice.non_dynamic = xnNonDyn;
            auto *arp = asn::New<ASN_XNAP_AllocationandRetentionPriority_t>();
            arp->priorityLevel = 8; arp->pre_emption_capability = 1; arp->pre_emption_vulnerability = 1;
            auto *qosParams = asn::New<ASN_XNAP_QoSFlowLevelQoSParameters_t>();
            qosParams->qos_characteristics    = *qosChars; free(qosChars);
            qosParams->allocationAndRetentionPrio = *arp;  free(arp);
            auto *qosFlowItem = asn::New<ASN_XNAP_QoSFlowsToBeSetup_Item_t>();
            qosFlowItem->qfi = 1;
            qosFlowItem->qosFlowLevelQoSParameters = *qosParams; free(qosParams);
            asn::SequenceAdd(pduItem->qosFlowsToBeSetup_List, qosFlowItem);
        }

        asn::SequenceAdd(ueCtxInfo->pduSessionResourcesToBeSetup_List, pduItem);
    }

    // --- 5g. rrc-Context: RRC handover container ---
    {
        OCTET_STRING_fromBuf(&ueCtxInfo->rrc_Context,
                             reinterpret_cast<const char *>(rrcContainer.data()),
                             static_cast<int>(rrcContainer.length()));
    }

    // Wrap UEContextInfoHORequest as IE 5
    auto *ieUeCtxInfo = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieUeCtxInfo->id          = XNAP_IE_UEContextInfoHORequest;
    ieUeCtxInfo->criticality = ASN_XNAP_Criticality_reject;
    if (!setHoIeValue(ieUeCtxInfo, &asn_DEF_ASN_XNAP_UEContextInfoHORequest, ueCtxInfo))
    {
        m_logger->err("xnHandoverRequestSource: failed to encode UEContextInfoHORequest");
        asn::Free(asn_DEF_ASN_XNAP_UEContextInfoHORequest, ueCtxInfo);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieUeCtxInfo);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGuami);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTgtCell);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieCause);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_UEContextInfoHORequest, ueCtxInfo);

    // -----------------------------------------------------------------------
    // IE 6 — UEHistoryInformation  (id=88, mandatory, criticality=ignore)
    //   List of cells the UE has visited recently.  Provides context for the
    //   target to optimise radio configuration.
    //   Assumption: one entry for the current source cell (serving this gNB).
    //   The entry is an opaque OCTET STRING per TS 38.413 §9.3.3.10; a zero-
    //   byte dummy is used here as a placeholder.  A real implementation would
    //   serialise the LastVisitedNR-CellInformation structure defined in TS 38.413.
    // -----------------------------------------------------------------------
    auto *ueHistItem = asn::New<ASN_XNAP_LastVisitedCell_Item_t>();
    ueHistItem->present = ASN_XNAP_LastVisitedCell_Item_PR_nG_RAN_Cell;
    static const uint8_t dummyHistBuf = 0x00; // Assumption: opaque placeholder
    OCTET_STRING_fromBuf(&ueHistItem->choice.nG_RAN_Cell,
                         reinterpret_cast<const char *>(&dummyHistBuf), 1);

    auto *ueHistInfo = asn::New<ASN_XNAP_UEHistoryInformation_t>();
    asn::SequenceAdd(*ueHistInfo, ueHistItem);

    auto *ieUeHist = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieUeHist->id          = XNAP_IE_UEHistoryInformation;
    ieUeHist->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieUeHist, &asn_DEF_ASN_XNAP_UEHistoryInformation, ueHistInfo))
    {
        m_logger->err("xnHandoverRequestSource: failed to encode UEHistoryInformation");
        asn::Free(asn_DEF_ASN_XNAP_UEHistoryInformation, ueHistInfo);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieUeHist);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieUeCtxInfo);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGuami);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTgtCell);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieCause);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_UEHistoryInformation, ueHistInfo);

    // -----------------------------------------------------------------------
    // Assemble HandoverRequest ProtocolIE container and wrap in InitiatingMessage
    // -----------------------------------------------------------------------
    auto *hoReq = asn::New<ASN_XNAP_HandoverRequest_t>();
    asn::SequenceAdd(hoReq->protocolIEs, ieSrcUeId);
    asn::SequenceAdd(hoReq->protocolIEs, ieCause);
    asn::SequenceAdd(hoReq->protocolIEs, ieTgtCell);
    asn::SequenceAdd(hoReq->protocolIEs, ieGuami);
    asn::SequenceAdd(hoReq->protocolIEs, ieUeCtxInfo);
    asn::SequenceAdd(hoReq->protocolIEs, ieUeHist);

    auto *initMsg = asn::New<ASN_XNAP_InitiatingMessage_t>();
    initMsg->procedureCode = XN_PROC_HANDOVER_PREPARATION;
    initMsg->criticality   = ASN_XNAP_Criticality_reject;
    if (ANY_fromType_aper(&initMsg->value, &asn_DEF_ASN_XNAP_HandoverRequest, hoReq) != 0)
    {
        m_logger->err("xnHandoverRequestSource: failed to encode HandoverRequest into InitiatingMessage");
        asn::Free(asn_DEF_ASN_XNAP_HandoverRequest, hoReq);
        asn::Free(asn_DEF_ASN_XNAP_InitiatingMessage, initMsg);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_HandoverRequest, hoReq);

    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present                  = ASN_XNAP_XnAP_PDU_PR_initiatingMessage;
    outerPdu->choice.initiatingMessage = initMsg;

    // -----------------------------------------------------------------------
    // APER-encode
    // -----------------------------------------------------------------------
    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        m_logger->err("xnHandoverRequestSource: APER encoding failed for ueId=%ld targetGnbId=%d",
                      ueId, targetPeer->gnbId);
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return;
    }

    // -----------------------------------------------------------------------
    // Allocate a UE-associated SCTP stream for this handover from the target's
    // stream manager, then send via the SCTP task.
    // -----------------------------------------------------------------------
    auto streamId = targetPeer->streamIdManager.allocate();
    if (!streamId.has_value())
    {
        m_logger->err("xnHandoverRequestSource: no free SCTP stream for gnbId=%d ueId=%ld",
                      targetPeer->gnbId, ueId);
        delete[] buffer;
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return;
    }

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = targetPeer->gnbId;
    sctpMsg->stream   = streamId.value();
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->sctpTask->push(std::move(sctpMsg));

    m_logger->info("XnAP HandoverRequest sent to gnbId=%d (targetNci=0x%09lx) "
                   "for ueId=%ld on stream=%u isCho=%d",
                   targetPeer->gnbId, targetNci, ueId, streamId.value(), isCho);

    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
}


// Handle an incoming XnAP HandoverRequest from a source gNB.
// Called from SCTP msg handler.
void XnTask::receiveHandoverRequest(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    uint32_t xnTxId = m_nextXnTxId++;

    // Validate outer PDU structure.
    if (pdu->present != ASN_XNAP_XnAP_PDU_PR_initiatingMessage ||
        !pdu->choice.initiatingMessage ||
        !pdu->choice.initiatingMessage->value.buf)
    {
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
        m_logger->err("receiveHandoverRequest: failed to decode HandoverRequest from gnbId=%d", gnbId);
        return;
    }

    // Iterate IEs to find UEContextInfoHORequest (id=83) and extract rrc-Context.
    std::unique_ptr<OctetString> rrcContainer;

    for (int i = 0; i < hoReq->protocolIEs.list.count; ++i)
    {
        auto *ie = hoReq->protocolIEs.list.array[i];
        if (!ie || ie->id != XNAP_IE_UEContextInfoHORequest || !ie->value.buf)
            continue;

        auto *ueCtxInfo = xnap_encode::Decode<ASN_XNAP_UEContextInfoHORequest_t>(
            asn_DEF_ASN_XNAP_UEContextInfoHORequest,
            reinterpret_cast<const uint8_t *>(ie->value.buf),
            static_cast<size_t>(ie->value.size));
        if (!ueCtxInfo)
        {
            m_logger->err("receiveHandoverRequest: failed to decode UEContextInfoHORequest from gnbId=%d", gnbId);
            break;
        }

        if (ueCtxInfo->rrc_Context.buf && ueCtxInfo->rrc_Context.size > 0)
            rrcContainer = std::make_unique<OctetString>(
                OctetString::FromArray(ueCtxInfo->rrc_Context.buf,
                                       static_cast<size_t>(ueCtxInfo->rrc_Context.size)));

        asn::Free(asn_DEF_ASN_XNAP_UEContextInfoHORequest, ueCtxInfo);
        break;
    }

    asn::Free(asn_DEF_ASN_XNAP_HandoverRequest, hoReq);

    if (!rrcContainer)
    {
        m_logger->err("receiveHandoverRequest: missing or empty rrc-Context from gnbId=%d", gnbId);
        return;
    }

    // Send XnToRrc msg to RRC task. xnTxId correlates when the response is returned.
    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_REQUEST_RECEIVED);
    msg->xnTxId = xnTxId;
    msg->sourceGnbId = gnbId;
    msg->rrcContainer = std::move(rrcContainer);
    m_base->rrcTask->push(std::move(msg));
}





void XnTask::xnHandoverCancelSource(int64_t ueId, int64_t targetNci, bool isCho)
{
    m_logger->debug("xnHandoverCancelSource ueId=%ld targetNci=%ld isCho=%d", ueId, targetNci, isCho);
}

void XnTask::sendSnStatusTransfer(int64_t ueId, int64_t targetNci, bool isCho)
{
    m_logger->debug("sendSnStatusTransfer ueId=%ld targetNci=%ld isCho=%d", ueId, targetNci, isCho);
}


// ---------------------------------------------------------------------------
// Outgoing — target gNB (triggered by RrcToXn messages)
// ---------------------------------------------------------------------------


// called from an RrcToXn msg to acknowledge the HandoverRequest.
//  Msg passes the RRC Reconfiguration container with the HO command for the UE.
void XnTask::sendHandoverRequestAck(int64_t ueId, int64_t targetNci, bool isCho,
                                         std::unique_ptr<OctetString> rrcContainer)
{

    // instruct GTP to setup Xn-U tunnels

    // create XnHandoverRequestAck message with the RRC container

    // send the XnHandoverRequestAck message to the target gNB


    m_logger->debug("xnHandoverRequestAckTarget ueId=%ld targetNci=%ld isCho=%d", ueId, targetNci, isCho);
}

void XnTask::sendHandoverPreparationFailure(int64_t ueId, int64_t targetNci, bool isCho,
                                                 int reason)
{
    m_logger->debug("xnHandoverPreparationFailureTarget ueId=%ld targetNci=%ld reason=%d",
                    ueId, targetNci, reason);
}

void XnTask::sendUeContextRelease(int64_t ueId, int64_t targetNci)
{
    m_logger->debug("xnUeContextReleaseTarget ueId=%ld targetNci=%ld", ueId, targetNci);
}

void XnTask::sendHandoverSuccess(int64_t ueId, int64_t targetNci)
{
    m_logger->debug("xnHandoverSuccessTarget ueId=%ld targetNci=%ld", ueId, targetNci);
}

// ---------------------------------------------------------------------------
// Timer handlers
// ---------------------------------------------------------------------------

void XnTask::xnHandleTimerPrep(int timerId)
{
    m_logger->debug("xnHandleTimerPrep timerId=%d", timerId);
}

void XnTask::xnHandleTimerOverall(int timerId)
{
    m_logger->debug("xnHandleTimerOverall timerId=%d", timerId);
}

} // namespace nr::gnb
