#include "task.hpp"
#include "encode.hpp"

#include <gnb/ngap/utils.hpp>
#include <gnb/gtp/task.hpp>
#include <gnb/rrc/task.hpp>
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


// ---------------------------------------------------------------------------
// Outgoing — source gNB (triggered by RrcToXn::HANDOVER_REQUEST_SEND)
//
// Builds and sends an XnAP HandoverRequest to the target gNB identified by
// targetNci.  The target gNB must already be present in the Xn peer table
// with an CONNECTED association state, otherwise the function logs an error
// and returns without sending.
// ---------------------------------------------------------------------------

void XnTask::sendHandoverRequest(int64_t ueId, int64_t targetNci, 
    std::unique_ptr<GnbHandoverUeContexts> contexts, 
    std::unique_ptr<OctetString> rrcContainer, 
    std::unique_ptr<GnbCondHandoverRequest> choRequest)
{
    bool isCho = choRequest != nullptr;
    m_logger->debug("xnHandoverRequestSource ueId=%ld targetNci=0x%09lx isCho=%s",
                    ueId, targetNci, isCho ? "true" : "false");

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
        auto *pduItem = asn::New<ASN_XNAP_PDUSessionResourcesToBeSetup_Item_t>();

        // pdu Session ID
        pduItem->pduSessionId = static_cast<ASN_XNAP_PDUSession_ID_t>(res->psi);

        // S-NSSAI
        asn::SetOctetString1(pduItem->s_NSSAI.sst, res->sNssai.sst);
        if (res->sNssai.sd.has_value() )
            asn::SetOctetString3(*pduItem->s_NSSAI.sd, res->sNssai.sd.value());


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


        if (res->qosFlows.empty())
        {
            m_logger->err("sendHandoverRequest: PSI=%d has no QoS flows; aborting", res->psi);
            asn::Free(asn_DEF_ASN_XNAP_PDUSessionResourcesToBeSetup_Item, pduItem);
            return;
        }

        // Data forwarding IE - add flows to the DataforwardingandOffloadingInfofromSource IE as well
        // qosFlowsToBeForwarded is an embedded list — add directly, no separate allocation needed.
        auto *dfInfo = asn::New<ASN_XNAP_DataforwardingandOffloadingInfofromSource>();

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

        // add the Data Forwarding Info IE
        pduItem->dataforwardinginfofromSource = dfInfo;
        
        asn::SequenceAdd(ueCtxInfo->pduSessionResourcesToBeSetup_List, pduItem);
    }

    // --- 5g. rrc-Context: RRC handover container ---
    // Opaque container for the UE's RRC context.  Provided by RRC task.
    {
        OCTET_STRING_fromBuf(&ueCtxInfo->rrc_Context,
                             reinterpret_cast<const char *>(rrcContainer->data()),
                             static_cast<int>(rrcContainer->length()));
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

    // --- 5h. CHOinformation-Req IE (conditional: isCho only) ---
    ASN_XNAP_ProtocolIE_Field_14202P0_t *ieCho = nullptr;
    if (isCho)
    {
        auto *choReq = asn::New<ASN_XNAP_CHOinformation_Req_t>();

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

        auto *extContainer = asn::New<ASN_XNAP_ProtocolExtensionContainer_14246P0_t>();
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
            choReq->iE_Extensions = extContainer;
        else
            asn::Free(asn_DEF_ASN_XNAP_ProtocolExtensionContainer_14246P0, extContainer);

        ieCho = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
        ieCho->id          = XNAP_IE_CHOinformation_Req;
        ieCho->criticality = ASN_XNAP_Criticality_reject;
        if (!setHoIeValue(ieCho, &asn_DEF_ASN_XNAP_CHOinformation_Req, choReq))
        {
            m_logger->err("xnHandoverRequestSource: failed to encode CHOinformation-Req");
            asn::Free(asn_DEF_ASN_XNAP_CHOinformation_Req, choReq);
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieCho);
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieUeHist);
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieUeCtxInfo);
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGuami);
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTgtCell);
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieCause);
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
            return;
        }
        asn::Free(asn_DEF_ASN_XNAP_CHOinformation_Req, choReq);
    }



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
    if (isCho && ieCho)
        asn::SequenceAdd(hoReq->protocolIEs, ieCho);
    
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
    // pull the next transaction ID 
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

    // Iterate IEs
    int64_t sourceUeXnApId = 0;
    std::unique_ptr<OctetString> rrcContainer;
    Guami guami{};

    int64_t amfId = 0;  // ng-c-UE-reference: AMF-UE-NGAP-ID
    std::string ngapSourceIpAddr;  // cp-TNL-info-source
    UeSecurityInfo ueSecInfo; // ueSecurityCapabilities and securityInformation
    uint64_t dlAmbr = 0, ulAmbr = 0;  // ueAmbr
    std::vector<PduSessionResource> pduSessions; // pduSessionResourcesToBeSetup_List


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
        //  (not used, just loggedt)
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
                // TODO: convert ASN PLMN to Plmn type
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
    }

    asn::Free(asn_DEF_ASN_XNAP_HandoverRequest, hoReq);

    // Sanity checks on mandatory IEs

    if (!rrcContainer)
    {
        m_logger->err("receiveHandoverRequest: missing or empty rrc-Context from gnbId=%d", gnbId);
        return;
    }

    if (sourceUeXnApId == 0)
    {
        m_logger->err("receiveHandoverRequest: missing sourceNG-RANnodeUEXnAPID (IE id=73) from gnbId=%d; dropping",
                      gnbId);
        return;
    }

    // Record this pending request to use for response.
    {
        XnPendingHandover pending{};
        pending.xnTxId       = xnTxId;
        pending.ueId         = 0; // filled when RRC sends the Ack back
        pending.sourceGnbId  = gnbId;
        pending.sourceUeXnApId = sourceUeXnApId;
        pending.isCho        = false;
        pending.timestamp    = static_cast<uint64_t>(utils::CurrentTimeMillis());
        m_pendingRequests.push_back(pending);
    }

    // Send XnToRrc msg to RRC task. xnTxId correlates when the response is returned.
    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_REQUEST_RECEIVED);
    msg->xnTxId          = xnTxId;
    msg->sourceGnbId     = gnbId;
    msg->rrcContainer    = std::move(rrcContainer);
    msg->amfUeNgapId     = amfId;
    msg->guami           = guami;
    msg->ueSecInfo       = ueSecInfo;
    msg->dlAmbr          = dlAmbr;
    msg->ulAmbr          = ulAmbr;
    msg->ngapSourceIpAddr = ngapSourceIpAddr;
    if (!pduSessions.empty())
        msg->sessionList = std::make_unique<std::vector<PduSessionResource>>(std::move(pduSessions));
    m_base->rrcTask->push(std::move(msg));
}


// called from an RrcToXn msg to acknowledge the HandoverRequest.
//  Msg passes the RRC Reconfiguration container with the HO command for the UE.
void XnTask::sendHandoverRequestAck(uint32_t xnTxId, uint64_t ueId,
    std::unique_ptr<OctetString> rrcContainer,
    std::unique_ptr<std::vector<PduSessionResource>> admittedSessions,
    std::unique_ptr<std::vector<PduSessionResource>> rejectedSessions)
{
    // -----------------------------------------------------------------------
    // 1. Look up pending handover entry by xnTxId to get routing info.
    // -----------------------------------------------------------------------
    XnPendingHandover *pending = nullptr;
    for (auto &p : m_pendingRequests)
    {
        if (p.xnTxId == xnTxId)
        {
            pending = &p;
            break;
        }
    }

    if (!pending)
    {
        m_logger->err("sendHandoverRequestAck: no pending entry for xnTxId=%u", xnTxId);
        return;
    }

    int sourceGnbId      = pending->sourceGnbId;
    int64_t targetUeId   = ueId;
    int64_t srcUeXnApId  = pending->sourceUeXnApId;

    // confirm the source gNB is still connected by xN
    XnPeerInfo *sourcePeer = m_xnPeerTable.getPeerInfo(sourceGnbId);
    if (!sourcePeer)
    {
        m_logger->err("sendHandoverRequestAck: no peer for sourceGnbId=%d xnTxId=%u",
                      sourceGnbId, xnTxId);
        return;
    }

    if (sourcePeer->connectionState != EXnConnectionState::CONNECTED)
    {
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

    auto *ieSrcUeId = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieSrcUeId->id          = XNAP_IE_sourceNG_RAN_node_UE_XnAP_ID;
    ieSrcUeId->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieSrcUeId, &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &srcUeId))
    {
        m_logger->err("sendHandoverRequestAck: failed to encode sourceNG-RANnodeUEXnAPID");
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
        return;
    }

    // -----------------------------------------------------------------------
    // IE 2 — targetNG-RANnodeUEXnAPID  (id=79, mandatory, criticality=ignore)
    //   The target gNB's handle for this UE on the Xn interface.
    // -----------------------------------------------------------------------
    auto tgtUeId = static_cast<ASN_XNAP_NG_RANnodeUEXnAPID_t>(targetUeId);

    auto *ieTgtUeId = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieTgtUeId->id          = XNAP_IE_targetNG_RAN_node_UE_XnAP_ID;
    ieTgtUeId->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieTgtUeId, &asn_DEF_ASN_XNAP_NG_RANnodeUEXnAPID, &tgtUeId))
    {
        m_logger->err("sendHandoverRequestAck: failed to encode targetNG-RANnodeUEXnAPID");
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTgtUeId);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
        return;
    }

    // -----------------------------------------------------------------------
    // IE 3 — PDUSessionResourcesAdmitted-List  (id=42, mandatory, criticality=ignore)
    //   For each admitted PDU session, allocate a DL Xn-U forwarding tunnel
    //   and list the admitted QoS flows.
    // -----------------------------------------------------------------------
    auto *admittedList = asn::New<ASN_XNAP_PDUSessionResourcesAdmitted_List_t>();

    if (admittedSessions)
    {
        for (auto &resource : *admittedSessions)
        {
            // Allocate a DL forwarding TEID for the Xn-U interface.
            // TODO: register this tunnel with the GTP task once XN→GTP messaging is in place.
            uint32_t dlTeid = ++m_xnDownlinkTeidCounter;

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
            else
            {
                // Fallback: a single default QFI=1 when no flows are present.
                auto *admittedItem = asn::New<ASN_XNAP_QoSFlowsAdmitted_Item_t>();
                admittedItem->qfi = 1;
                asn::SequenceAdd(pduItem->pduSessionResourceAdmittedInfo.qosFlowsAdmitted_List,
                                 admittedItem);

                auto *fwdItem = asn::New<ASN_XNAP_QoSFLowsAcceptedToBeForwarded_Item_t>();
                fwdItem->qosFlowIdentifier = 1;
                asn::SequenceAdd(fwdInfo->qosFlowsAcceptedForDataForwarding_List, fwdItem);
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

    auto *ieAdmitted = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieAdmitted->id          = XNAP_IE_PDUSessionResourcesAdmitted_List;
    ieAdmitted->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieAdmitted, &asn_DEF_ASN_XNAP_PDUSessionResourcesAdmitted_List, admittedList))
    {
        m_logger->err("sendHandoverRequestAck: failed to encode PDUSessionResourcesAdmitted-List");
        asn::Free(asn_DEF_ASN_XNAP_PDUSessionResourcesAdmitted_List, admittedList);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieAdmitted);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTgtUeId);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_PDUSessionResourcesAdmitted_List, admittedList);

    // -----------------------------------------------------------------------
    // IE 4 — PDUSessionResourcesNotAdmitted-List  (id=43, optional, criticality=ignore)
    // -----------------------------------------------------------------------
    ASN_XNAP_ProtocolIE_Field_14202P0_t *ieNotAdmitted = nullptr;

    if (rejectedSessions && !rejectedSessions->empty())
    {
        auto *notAdmittedList = asn::New<ASN_XNAP_PDUSessionResourcesNotAdmitted_List_t>();

        for (const auto &resource : *rejectedSessions)
        {
            auto *pduItem = asn::New<ASN_XNAP_PDUSessionResourcesNotAdmitted_Item_t>();
            pduItem->pduSessionId = static_cast<ASN_XNAP_PDUSession_ID_t>(resource.psi);
            asn::SequenceAdd(*notAdmittedList, pduItem);
        }

        ieNotAdmitted = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
        ieNotAdmitted->id          = XNAP_IE_PDUSessionResourcesNotAdmitted;
        ieNotAdmitted->criticality = ASN_XNAP_Criticality_ignore;
        if (!setHoIeValue(ieNotAdmitted, &asn_DEF_ASN_XNAP_PDUSessionResourcesNotAdmitted_List, notAdmittedList))
        {
            m_logger->err("sendHandoverRequestAck: failed to encode PDUSessionResourcesNotAdmitted-List");
            asn::Free(asn_DEF_ASN_XNAP_PDUSessionResourcesNotAdmitted_List, notAdmittedList);
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieNotAdmitted);
            ieNotAdmitted = nullptr;
        }
        else
        {
            asn::Free(asn_DEF_ASN_XNAP_PDUSessionResourcesNotAdmitted_List, notAdmittedList);
        }
    }

    // -----------------------------------------------------------------------
    // IE 5 — Target2SourceNG-RANnodeTranspContainer  (id=77, mandatory, criticality=ignore)
    //   OCTET STRING carrying the RRCReconfiguration (handover command) for the UE.
    // -----------------------------------------------------------------------
    OCTET_STRING_t rrcOs{};
    OCTET_STRING_fromBuf(&rrcOs,
                         reinterpret_cast<const char *>(rrcContainer->data()),
                         static_cast<int>(rrcContainer->length()));

    auto *ieRrcContainer = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieRrcContainer->id          = XNAP_IE_Target2SourceTranspContainer;
    ieRrcContainer->criticality = ASN_XNAP_Criticality_ignore;
    if (!setHoIeValue(ieRrcContainer, &asn_DEF_OCTET_STRING, &rrcOs))
    {
        m_logger->err("sendHandoverRequestAck: failed to encode Target2SourceNG-RANnodeTranspContainer");
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_OCTET_STRING, &rrcOs);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieRrcContainer);
        if (ieNotAdmitted)
            asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieNotAdmitted);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieAdmitted);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTgtUeId);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieSrcUeId);
        return;
    }
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_OCTET_STRING, &rrcOs);

    // -----------------------------------------------------------------------
    // Assemble HandoverRequestAcknowledge and wrap in SuccessfulOutcome
    // -----------------------------------------------------------------------
    auto *hoAck = asn::New<ASN_XNAP_HandoverRequestAcknowledge_t>();
    asn::SequenceAdd(hoAck->protocolIEs, ieSrcUeId);
    asn::SequenceAdd(hoAck->protocolIEs, ieTgtUeId);
    asn::SequenceAdd(hoAck->protocolIEs, ieAdmitted);
    if (ieNotAdmitted)
        asn::SequenceAdd(hoAck->protocolIEs, ieNotAdmitted);
    asn::SequenceAdd(hoAck->protocolIEs, ieRrcContainer);

    auto *succMsg = asn::New<ASN_XNAP_SuccessfulOutcome_t>();
    succMsg->procedureCode = XN_PROC_HANDOVER_PREPARATION;
    succMsg->criticality   = ASN_XNAP_Criticality_reject;
    if (ANY_fromType_aper(&succMsg->value, &asn_DEF_ASN_XNAP_HandoverRequestAcknowledge, hoAck) != 0)
    {
        m_logger->err("sendHandoverRequestAck: failed to encode HandoverRequestAcknowledge into SuccessfulOutcome");
        asn::Free(asn_DEF_ASN_XNAP_HandoverRequestAcknowledge, hoAck);
        asn::Free(asn_DEF_ASN_XNAP_SuccessfulOutcome, succMsg);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_HandoverRequestAcknowledge, hoAck);

    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present                  = ASN_XNAP_XnAP_PDU_PR_successfulOutcome;
    outerPdu->choice.successfulOutcome = succMsg;

    // -----------------------------------------------------------------------
    // APER-encode and send via SCTP
    // -----------------------------------------------------------------------
    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        m_logger->err("sendHandoverRequestAck: APER encoding failed for xnTxId=%u gnbId=%d",
                      xnTxId, sourceGnbId);
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return;
    }

    auto streamId = sourcePeer->streamIdManager.allocate();
    if (!streamId.has_value())
    {
        m_logger->err("sendHandoverRequestAck: no free SCTP stream for gnbId=%d xnTxId=%u",
                      sourceGnbId, xnTxId);
        delete[] buffer;
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return;
    }

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = sourceGnbId;
    sctpMsg->stream   = streamId.value();
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->sctpTask->push(std::move(sctpMsg));

    m_logger->info("XnAP HandoverRequestAcknowledge sent to gnbId=%d for xnTxId=%u ueId=%ld",
                   sourceGnbId, xnTxId, targetUeId);

    // Remove the pending entry now that the Ack has been sent.
    m_pendingRequests.erase(
        std::remove_if(m_pendingRequests.begin(), m_pendingRequests.end(),
                       [xnTxId](const XnPendingHandover &p){ return p.xnTxId == xnTxId; }),
        m_pendingRequests.end());

    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
}







void XnTask::sendHandoverPreparationFailure(uint32_t xnTxId, int reason)
{
    m_logger->debug("xnHandoverPreparationFailureTarget xnTxId=%ld reason=%d",
                    xnTxId, reason);
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
    std::unique_ptr<OctetString> rrcContainer;

    for (int i = 0; i < hoAck->protocolIEs.list.count; ++i)
    {
        auto *ie = hoAck->protocolIEs.list.array[i];
        if (!ie || !ie->value.buf)
            continue;

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

                if (sourceUeId >= 0)
                {
                    auto gm = std::make_unique<NmGnbXnToGtp>(NmGnbXnToGtp::FORWARDING_TUNNEL_SETUP);
                    gm->ueId             = sourceUeId;
                    gm->psi              = static_cast<int>(item->pduSessionId);
                    gm->forwardingTunnel = std::move(fwdTunnel);
                    m_base->gtpTask->push(std::move(gm));
                }
            }

            asn::Free(asn_DEF_ASN_XNAP_PDUSessionResourcesAdmitted_List, admittedList);
        }
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
        return;
    }

    if (!rrcContainer)
    {
        m_logger->warn("UE[%ld]: receiveHandoverRequestAck - no RRC container from gnbId=%d", sourceUeId, gnbId);
        return;
    }

    auto msg = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_REQUEST_ACK_RECEIVED);
    msg->ueId         = sourceUeId;
    msg->targetNci    = -1;  // not carried in ack; needs source-side outgoing HO tracking
    msg->isCho        = false;
    msg->rrcContainer = std::move(rrcContainer);
    m_base->rrcTask->push(std::move(msg));

    m_logger->info("UE[%ld]: HandoverRequestAck processed from gnbId=%d, forwarded to RRC", sourceUeId, gnbId);
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


// ---------------------------------------------------------------------------
// Outgoing — target gNB (triggered by RrcToXn messages)
// ---------------------------------------------------------------------------



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
