#include "task.hpp"
#include "encode.hpp"

#include <gnb/ngap/utils.hpp>
#include <lib/asn/utils.hpp>

extern "C"
{
// asn1c/ constr_TYPE.h (found first in the include path) doesn't pull in
// compare.h, so type_compare_f is undefined by the time xnap sub-headers
// that declare it are processed.  Include the xnap-specific helpers first.
#include <asn_compare.h>   // defines asn_comp_rval_t
#include <compare.h>       // defines type_compare_f
#include <ANY.h>
#include <ASN_XNAP_XnAP-PDU.h>
#include <ASN_XNAP_InitiatingMessage.h>
#include <ASN_XNAP_SuccessfulOutcome.h>
#include <ASN_XNAP_UnsuccessfulOutcome.h>
#include <ASN_XNAP_XnSetupRequest.h>
#include <ASN_XNAP_XnSetupResponse.h>
#include <ASN_XNAP_XnSetupFailure.h>
#include <ASN_XNAP_Cause.h>
#include <ASN_XNAP_ProtocolIE-Field.h>
#include <ASN_XNAP_ProtocolIE-Container.h>

// GlobalNG-RANNode-ID and sub-types
#include <ASN_XNAP_GlobalNG-RANNode-ID.h>
#include <ASN_XNAP_GlobalgNB-ID.h>
#include <ASN_XNAP_GNB-ID-Choice.h>
#include <ASN_XNAP_PLMN-Identity.h>

// TAISupport-List and sub-types
#include <ASN_XNAP_TAISupport-List.h>
#include <ASN_XNAP_TAISupport-Item.h>
#include <ASN_XNAP_BroadcastPLMNinTAISupport-Item.h>
#include <ASN_XNAP_SliceSupport-List.h>
#include <ASN_XNAP_S-NSSAI.h>

// AMF-Region-Information and sub-types
#include <ASN_XNAP_AMF-Region-Information.h>
#include <ASN_XNAP_GlobalAMF-Region-Information.h>

// TAC (needed for TAISupport-Item decoding)
#include <ASN_XNAP_TAC.h>

// ServedCells-NR and sub-types
#include <ASN_XNAP_ServedCells-NR.h>
#include <ASN_XNAP_ServedCells-NR-Item.h>
#include <ASN_XNAP_ServedCellInformation-NR.h>
#include <ASN_XNAP_NR-CGI.h>
#include <ASN_XNAP_BroadcastPLMNs.h>
#include <ASN_XNAP_NRModeInfo.h>
#include <ASN_XNAP_NRModeInfoTDD.h>
#include <ASN_XNAP_NRFrequencyInfo.h>
#include <ASN_XNAP_NRFrequencyBand-List.h>
#include <ASN_XNAP_NRFrequencyBandItem.h>
#include <ASN_XNAP_NRTransmissionBandwidth.h>
#include <ASN_XNAP_Connectivity-Support.h>
}

namespace nr::gnb
{

static constexpr uint16_t NON_UE_ASSOCIATED_STREAM_ID = 0; // SCTP stream for non-UE-associated messages (e.g. XnSetup)


// XnSetup procedure code (TS 38.423 Table 9.1-1); mirrors the constant in transport.cpp.
static constexpr long XN_PROC_XN_SETUP = 17;

// XnSetupRequest mandatory IE IDs from 3GPP TS 38.423 / XnAP-PDU-Contents ASN.1 (rel-18)
static constexpr long XNAP_IE_GlobalNG_RAN_Node_ID    = 14;   // id-GlobalNG-RAN-node-ID
static constexpr long XNAP_IE_TAISupport_List         = 75;   // id-TAISupport-list
static constexpr long XNAP_IE_AMF_Region_Information  = 4;    // id-AMF-Region-Information
static constexpr long XNAP_IE_List_of_served_cells_NR = 19;   // id-List-of-served-cells-NR
static constexpr long XNAP_IE_Cause                   = 7;    // id-Cause

// Pack a typed ASN.1 value into the ANY_t value field of a ProtocolIE-Field using APER.
// Returns false on encoding failure (caller should abort and free).
static bool setIeValue(ASN_XNAP_ProtocolIE_Field_14202P0_t *ie,
                       asn_TYPE_descriptor_t *td, void *val)
{
    return ANY_fromType_aper(&ie->value, td, val) == 0;
}

// Build a 3-byte BCD-encoded PLMN octet string into dst (same layout as NGAP).
static void setXnPlmn(ASN_XNAP_PLMN_Identity_t &dst, const Plmn &plmn)
{
    asn::SetOctetString3(dst, ngap_utils::PlmnToOctet3(plmn));
}


// called when SCTP client has established a new association.
// triggers XnSetup procedure by sending an XnSetupRequest on the new association
void XnTask::handleAssociationSetup(int gnbId, int ascId, int inCount, int outCount)
{
    auto *gnb = m_xnPeerTable.getPeerInfo(gnbId);

    if (gnb != nullptr)
    {
        // update SCTP association info
        auto assoc = SctpAssociation();
        assoc.associationId = ascId;
        assoc.inStreams = inCount;
        assoc.outStreams = outCount;
        gnb->sctpAssoc = assoc;

        // reset stream ID manager
        gnb->streamIdManager.resetStreams(inCount, outCount);

        // trigger XNAP setup procedure by sending an XnSetupRequest on the new association
        xnSetupRequestSend(gnbId);
    }
    else
    {
        m_logger->err("Failed to find XnPeerNode for gnbId=%d, SCTP setup failed", gnbId);
    }
}

void XnTask::handleAssociationShutdown(int gnbId)
{
    auto *gnb = m_xnPeerTable.getPeerInfo(gnbId);
    if (gnb == nullptr)
        return;

    m_logger->info("Association terminated for gNB[%d]", gnbId);
    m_logger->debug("Removing gNB[%d] from peer table", gnbId);

    m_xnPeerTable.removePeerInfo(gnbId);

    auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
    w->clientId = gnbId;
    m_base->sctpTask->push(std::move(w));

}

void XnTask::xnSetupRequestSend(int gnbId)
{
    m_logger->debug("xnSetupRequestSend gnbId=%d", gnbId);

    const GnbConfig *cfg = m_base->config;

    // -----------------------------------------------------------------------
    // IE 1 — GlobalNG-RANNode-ID  (mandatory, criticality=reject)
    //   Identifies this gNB to the peer.  We use the gnb_ID bit-string variant
    //   of GNB-ID-Choice, length = gnbIdLength bits (22..32), value =
    //   the upper gnbIdLength bits of the NCI.
    // -----------------------------------------------------------------------

    auto *gnbGlobalId = asn::New<ASN_XNAP_GlobalgNB_ID_t>();
    setXnPlmn(gnbGlobalId->plmn_id, cfg->plmn);

    gnbGlobalId->gnb_id.present = ASN_XNAP_GNB_ID_Choice_PR_gnb_ID;
    // Left-align the gNB ID within a 32-bit octet block then truncate to gnbIdLength bits,
    // matching the NGAP encoding pattern in ngap/interface.cpp.
    asn::SetBitString(gnbGlobalId->gnb_id.choice.gnb_ID,
                      octet4{cfg->getGnbId() << (32 - cfg->gnbIdLength)},
                      static_cast<size_t>(cfg->gnbIdLength));

    auto *globalNodeId = asn::New<ASN_XNAP_GlobalNG_RANNode_ID_t>();
    globalNodeId->present = ASN_XNAP_GlobalNG_RANNode_ID_PR_gNB;
    globalNodeId->choice.gNB = gnbGlobalId;

    auto *ieGlobalId = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieGlobalId->id          = XNAP_IE_GlobalNG_RAN_Node_ID;
    ieGlobalId->criticality = ASN_XNAP_Criticality_reject;
    if (!setIeValue(ieGlobalId, &asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, globalNodeId))
    {
        m_logger->err("xnSetupRequestSend: failed to encode GlobalNG-RANNode-ID");
        asn::Free(asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, globalNodeId);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGlobalId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, globalNodeId);

    // -----------------------------------------------------------------------
    // IE 2 — TAISupport-List  (mandatory, criticality=reject)
    //   Advertises the TAC(s) and PLMN(s)/slices served by this gNB.
    //   Assumption: one TAI entry derived from cfg->tac and cfg->plmn.
    //   Slice list mirrors the NSSAI from cfg (same as NG Setup Request).
    // -----------------------------------------------------------------------

    auto *snssai = asn::New<ASN_XNAP_S_NSSAI_t>();
    // Assumption: use the first configured slice; a real implementation would
    // iterate cfg->nssai.slices and add one S-NSSAI per slice entry.
    if (!cfg->nssai.slices.empty())
    {
        asn::SetOctetString1(snssai->sst, static_cast<uint8_t>(cfg->nssai.slices[0].sst));
        if (cfg->nssai.slices[0].sd.has_value())
        {
            snssai->sd = asn::New<OCTET_STRING_t>();
            asn::SetOctetString3(*snssai->sd, octet3{cfg->nssai.slices[0].sd.value()});
        }
    }
    else
    {
        // Fallback: SST=1 (eMBB) with no SD — the most common default slice.
        asn::SetOctetString1(snssai->sst, 1);
    }

    auto *broadcastPlmnTai = asn::New<ASN_XNAP_BroadcastPLMNinTAISupport_Item_t>();
    setXnPlmn(broadcastPlmnTai->plmn_id, cfg->plmn);
    asn::SequenceAdd(broadcastPlmnTai->tAISliceSupport_List, snssai);

    auto *taiItem = asn::New<ASN_XNAP_TAISupport_Item_t>();
    // TAC is a 3-octet value in XnAP (same as NGAP).
    asn::SetOctetString3(taiItem->tac, octet3{cfg->tac});
    asn::SequenceAdd(taiItem->broadcastPLMNs, broadcastPlmnTai);

    auto *taiList = asn::New<ASN_XNAP_TAISupport_List_t>();
    asn::SequenceAdd(*taiList, taiItem);

    auto *ieTaiList = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieTaiList->id          = XNAP_IE_TAISupport_List;
    ieTaiList->criticality = ASN_XNAP_Criticality_reject;
    if (!setIeValue(ieTaiList, &asn_DEF_ASN_XNAP_TAISupport_List, taiList))
    {
        m_logger->err("xnSetupRequestSend: failed to encode TAISupport-List");
        asn::Free(asn_DEF_ASN_XNAP_TAISupport_List, taiList);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTaiList);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGlobalId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_TAISupport_List, taiList);

    // -----------------------------------------------------------------------
    // IE 3 — AMF-Region-Information  (mandatory, criticality=reject)
    //   Identifies the AMF region(s) reachable via this gNB.
    //   Assumption: use PLMN from config and AMF region ID = 0.  A real
    //   implementation should read the amfRegionId from the connected AMF's
    //   GUAMI (available in NgapAmfContext via the app/NGAP task).  Setting
    //   region 0 is harmless for setup — the peer uses it only for AMF
    //   selection hints, not for authentication.
    // -----------------------------------------------------------------------

    auto *amfRegionEntry = asn::New<ASN_XNAP_GlobalAMF_Region_Information_t>();
    setXnPlmn(amfRegionEntry->plmn_ID, cfg->plmn);
    // amf_region_id is a BIT_STRING of exactly 8 bits (TS 38.413 clause 9.3.3.1).
    asn::SetBitStringInt<8>(0, amfRegionEntry->amf_region_id); // Assumption: placeholder 0

    auto *amfRegionInfo = asn::New<ASN_XNAP_AMF_Region_Information_t>();
    asn::SequenceAdd(*amfRegionInfo, amfRegionEntry);

    auto *ieAmfRegion = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieAmfRegion->id          = XNAP_IE_AMF_Region_Information;
    ieAmfRegion->criticality = ASN_XNAP_Criticality_reject;
    if (!setIeValue(ieAmfRegion, &asn_DEF_ASN_XNAP_AMF_Region_Information, amfRegionInfo))
    {
        m_logger->err("xnSetupRequestSend: failed to encode AMF-Region-Information");
        asn::Free(asn_DEF_ASN_XNAP_AMF_Region_Information, amfRegionInfo);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieAmfRegion);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTaiList);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGlobalId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_AMF_Region_Information, amfRegionInfo);

    // -----------------------------------------------------------------------
    // IE 4 — List-of-served-cells-NR  (optional, criticality=reject)
    //   Describes this gNB's NR cells so the peer can build its neighbour
    //   table without an OAM lookup.
    //
    //   Sub-IE assumptions:
    //   - nrPCI = 1  (arbitrary; GnbConfig has no PCI field)
    //   - cellID = NR-CGI built from cfg->plmn and cfg->nci (36-bit)
    //   - tac = cfg->tac (3 octets)
    //   - broadcastPLMN = [ cfg->plmn ]
    //   - nrModeInfo = TDD, ARFCN=632628 (n78 / 3.5 GHz), SCS=30 kHz, NRB=106
    //     (20 MHz channel — the most common indoor/urban 5G NR deployment)
    //   - measurementTimingConfiguration = 1 dummy zero-byte (OCTET STRING,
    //     content defined by TS 38.331 MeasurementTimingConfiguration; not
    //     validated on this interface for setup purposes)
    //   - connectivitySupport.eNDC_Support = not_supported (standalone NR gNB)
    // -----------------------------------------------------------------------

    // --- NR-CGI ---
    auto *nrCgi = asn::New<ASN_XNAP_NR_CGI_t>();
    setXnPlmn(nrCgi->plmn_id, cfg->plmn);
    asn::SetBitStringLong<36>(cfg->nci, nrCgi->nr_CI);  // NR Cell Identity is 36 bits

    // --- BroadcastPLMNs ---
    auto *bcastPlmn = asn::New<ASN_XNAP_PLMN_Identity_t>();
    asn::SetOctetString3(*bcastPlmn, ngap_utils::PlmnToOctet3(cfg->plmn));

    auto *broadcastPlmns = asn::New<ASN_XNAP_BroadcastPLMNs_t>();
    asn::SequenceAdd(*broadcastPlmns, bcastPlmn);

    // --- NRModeInfo (TDD) ---
    auto *freqBandItem = asn::New<ASN_XNAP_NRFrequencyBandItem_t>();
    freqBandItem->nr_frequency_band = 78; // Assumption: n78 (3300–3800 MHz), most common 5G TDD band

    auto *freqBandList = asn::New<ASN_XNAP_NRFrequencyBand_List_t>();
    asn::SequenceAdd(*freqBandList, freqBandItem);

    auto *freqInfo = asn::New<ASN_XNAP_NRFrequencyInfo_t>();
    freqInfo->nrARFCN = 632628; // Assumption: ARFCN for 3.5 GHz (n78 centre)
    freqInfo->frequencyBand_List = *freqBandList;
    free(freqBandList); // freqBandList contents moved into freqInfo

    auto *txBw = asn::New<ASN_XNAP_NRTransmissionBandwidth_t>();
    txBw->nRSCS = ASN_XNAP_NRSCS_scs30;       // Assumption: 30 kHz SCS (FR1 standard)
    txBw->nRNRB = ASN_XNAP_NRNRB_nrb106;      // Assumption: 106 RBs = 20 MHz @ 30 kHz SCS

    auto *tddInfo = asn::New<ASN_XNAP_NRModeInfoTDD_t>();
    tddInfo->nrFrequencyInfo      = *freqInfo;
    free(freqInfo);
    tddInfo->nrTransmissonBandwidth = *txBw;
    free(txBw);

    auto *modeInfo = asn::New<ASN_XNAP_NRModeInfo_t>();
    modeInfo->present       = ASN_XNAP_NRModeInfo_PR_tdd;
    modeInfo->choice.tdd    = tddInfo;

    // --- measurementTimingConfiguration: 1 dummy zero-byte ---
    static const uint8_t dummyMtc = 0x00;
    OCTET_STRING_t mtcOctet{};
    OCTET_STRING_fromBuf(&mtcOctet, reinterpret_cast<const char *>(&dummyMtc), 1);

    // --- connectivitySupport ---
    ASN_XNAP_Connectivity_Support_t connSupport{};
    connSupport.eNDC_Support = ASN_XNAP_Connectivity_Support__eNDC_Support_not_supported;

    // --- Assemble ServedCellInformation-NR ---
    auto *cellInfo = asn::New<ASN_XNAP_ServedCellInformation_NR_t>();
    cellInfo->nrPCI             = cfg->nci;         // For simulation, we use NCI as PCI
    cellInfo->cellID            = *nrCgi;
    free(nrCgi);
    asn::SetOctetString3(cellInfo->tac, octet3{cfg->tac});
    cellInfo->broadcastPLMN     = *broadcastPlmns;
    free(broadcastPlmns);
    cellInfo->nrModeInfo        = *modeInfo;
    free(modeInfo);
    cellInfo->measurementTimingConfiguration = mtcOctet;
    cellInfo->connectivitySupport = connSupport;

    auto *servedCellItem = asn::New<ASN_XNAP_ServedCells_NR_Item_t>();
    servedCellItem->served_cell_info_NR = *cellInfo;
    free(cellInfo);

    auto *servedCells = asn::New<ASN_XNAP_ServedCells_NR_t>();
    asn::SequenceAdd(*servedCells, servedCellItem);

    auto *ieServedCells = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieServedCells->id          = XNAP_IE_List_of_served_cells_NR;
    ieServedCells->criticality = ASN_XNAP_Criticality_reject;
    if (!setIeValue(ieServedCells, &asn_DEF_ASN_XNAP_ServedCells_NR, servedCells))
    {
        m_logger->err("xnSetupRequestSend: failed to encode ServedCells-NR");
        asn::Free(asn_DEF_ASN_XNAP_ServedCells_NR, servedCells);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieServedCells);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieAmfRegion);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTaiList);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGlobalId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_ServedCells_NR, servedCells);

    // -----------------------------------------------------------------------
    // Assemble XnSetupRequest ProtocolIE container
    // -----------------------------------------------------------------------

    auto *xnSetupReq = asn::New<ASN_XNAP_XnSetupRequest_t>();
    asn::SequenceAdd(xnSetupReq->protocolIEs, ieGlobalId);
    asn::SequenceAdd(xnSetupReq->protocolIEs, ieTaiList);
    asn::SequenceAdd(xnSetupReq->protocolIEs, ieAmfRegion);
    asn::SequenceAdd(xnSetupReq->protocolIEs, ieServedCells);

    // -----------------------------------------------------------------------
    // Wrap in InitiatingMessage with procedure code 17 (XnSetup)
    // -----------------------------------------------------------------------

    auto *initMsg = asn::New<ASN_XNAP_InitiatingMessage_t>();
    initMsg->procedureCode = XN_PROC_XN_SETUP;
    initMsg->criticality   = ASN_XNAP_Criticality_reject;
    if (ANY_fromType_aper(&initMsg->value, &asn_DEF_ASN_XNAP_XnSetupRequest, xnSetupReq) != 0)
    {
        m_logger->err("xnSetupRequestSend: failed to encode XnSetupRequest into InitiatingMessage");
        asn::Free(asn_DEF_ASN_XNAP_XnSetupRequest, xnSetupReq);
        asn::Free(asn_DEF_ASN_XNAP_InitiatingMessage, initMsg);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_XnSetupRequest, xnSetupReq);

    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present                    = ASN_XNAP_XnAP_PDU_PR_initiatingMessage;
    outerPdu->choice.initiatingMessage   = initMsg;

    // -----------------------------------------------------------------------
    // APER-encode and send via SCTP to the target gNB (clientId = gnbId)
    // -----------------------------------------------------------------------

    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        m_logger->err("xnSetupRequestSend: APER encoding failed for gnbId=%d", gnbId);
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return;
    }

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = gnbId;  // clientId == target NCI, as set in updateXnConnections()
    sctpMsg->stream   = NON_UE_ASSOCIATED_STREAM_ID;
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->sctpTask->push(std::move(sctpMsg));

    m_logger->info("XnSetupRequest sent to gnbId=%d", gnbId);

    auto *gnb = m_xnPeerTable.getPeerInfo(gnbId);
    if (gnb != nullptr)
    {
        gnb->connectionState = EXnConnectionState::CONNECTION_REQUESTED;
    }

    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
}

// ---------------------------------------------------------------------------
// Decode a 3-byte XNAP PLMN-Identity octet string into a Plmn struct.
// Bit layout is identical to NGAP (TS 24.008 10.5.1.3).
// ---------------------------------------------------------------------------
static void xnPlmnDecode(const ASN_XNAP_PLMN_Identity_t &src, Plmn &out)
{
    if (src.size != 3)
        return;
    const uint8_t *b = src.buf;
    int mcc3 = b[1] & 0x0f;
    out.mcc = ((b[0] & 0xf0) >> 4) * 10    // MCC digit 2
            + (b[0] & 0x0f) * 100           // MCC digit 1
            + mcc3;                          // MCC digit 3

    int mnc3hi = (b[1] & 0xf0) >> 4;        // third digit of MNC (0xF → 2-digit MNC)
    if (mnc3hi == 0xf)
    {
        out.isLongMnc = false;
        out.mnc = ((b[2] & 0xf0) >> 4)      // MNC digit 2
                + (b[2] & 0x0f) * 10;       // MNC digit 1
    }
    else
    {
        out.isLongMnc = true;
        out.mnc = mnc3hi * 100              // MNC digit 3
                + ((b[2] & 0xf0) >> 4)      // MNC digit 2
                + (b[2] & 0x0f) * 10;       // MNC digit 1
    }
}

void XnTask::xnSetupRequestReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("xnSetupRequestReceive gnbId=%d", gnbId);

    auto *initMsg = pdu->choice.initiatingMessage;
    if (!initMsg || !initMsg->value.buf)
    {
        m_logger->err("xnSetupRequestReceive: null initiatingMessage from gnbId=%d", gnbId);
        xnSetupFailureSend(gnbId);
        return;
    }

    // Decode the APER-encoded XnSetupRequest from the InitiatingMessage OPEN TYPE value.
    auto *xnReq = xnap_encode::Decode<ASN_XNAP_XnSetupRequest_t>(
        asn_DEF_ASN_XNAP_XnSetupRequest,
        reinterpret_cast<const uint8_t *>(initMsg->value.buf),
        static_cast<size_t>(initMsg->value.size));
    if (!xnReq)
    {
        m_logger->err("xnSetupRequestReceive: APER decode failed for gnbId=%d", gnbId);
        xnSetupFailureSend(gnbId);
        return;
    }

    XnPeerInfo peer;
    peer.gnbId = gnbId;

    bool hasGlobalId = false;
    bool hasTaiList  = false;
    bool hasAmfRegion = false;

    for (int i = 0; i < xnReq->protocolIEs.list.count; ++i)
    {
        auto *ie = xnReq->protocolIEs.list.array[i];
        if (!ie || !ie->value.buf)
            continue;

        const uint8_t *vbuf  = reinterpret_cast<const uint8_t *>(ie->value.buf);
        const size_t   vsize = static_cast<size_t>(ie->value.size);

        switch (ie->id)
        {
        // -------------------------------------------------------------------
        // IE 14 — GlobalNG-RANNode-ID  (mandatory, criticality=reject)
        //   Extract the gNB-ID bit-string.  The bit-string length encodes
        //   gnbIdLength; we left-shift to produce the 36-bit NCI base
        //   (cell bits are zero).  If ServedCells-NR also provides an NCI
        //   we'll overwrite this with the real value there.
        // -------------------------------------------------------------------
        case XNAP_IE_GlobalNG_RAN_Node_ID: {
            auto *nodeId = xnap_encode::Decode<ASN_XNAP_GlobalNG_RANNode_ID_t>(
                asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, vbuf, vsize);
            if (!nodeId)
            {
                m_logger->warn("xnSetupRequestReceive: cannot decode GlobalNG-RANNode-ID from gnbId=%d", gnbId);
                break;
            }

            if (nodeId->present == ASN_XNAP_GlobalNG_RANNode_ID_PR_gNB &&
                nodeId->choice.gNB &&
                nodeId->choice.gNB->gnb_id.present == ASN_XNAP_GNB_ID_Choice_PR_gnb_ID)
            {
                const BIT_STRING_t &bs = nodeId->choice.gNB->gnb_id.choice.gnb_ID;
                // gnbIdLength = total bits in the bit-string
                int gnbIdLength = static_cast<int>(bs.size * 8) - bs.bits_unused;
                // Left-align within 36 bits so the value can serve as an NCI base
                int64_t gnbIdVal = asn::GetBitStringLong<32>(bs); // read up to 32 bits (max gnbIdLength=32)
                peer.nci = gnbIdVal << (36 - gnbIdLength);
                hasGlobalId = true;
            }
            asn::Free(asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, nodeId);
            break;
        }

        // -------------------------------------------------------------------
        // IE 75 — TAISupport-List  (mandatory, criticality=reject)
        //   For each TAISupport-Item: record the TAC (3-byte int) and
        //   the set of broadcast PLMNs.  Duplicate PLMNs across TAIs are
        //   stored only once (checked by mcc/mnc/isLongMnc equality).
        // -------------------------------------------------------------------
        case XNAP_IE_TAISupport_List: {
            auto *taiList = xnap_encode::Decode<ASN_XNAP_TAISupport_List_t>(
                asn_DEF_ASN_XNAP_TAISupport_List, vbuf, vsize);
            if (!taiList)
            {
                m_logger->warn("xnSetupRequestReceive: cannot decode TAISupport-List from gnbId=%d", gnbId);
                break;
            }

            for (int j = 0; j < taiList->list.count; ++j)
            {
                auto *item = taiList->list.array[j];
                if (!item) continue;

                // TAC is a 3-byte OCTET_STRING (same encoding as NGAP SupportedTA)
                if (item->tac.size == 3)
                    peer.tacList.push_back(static_cast<int32_t>(asn::GetOctet3(item->tac)));

                for (int k = 0; k < item->broadcastPLMNs.list.count; ++k)
                {
                    auto *bcastItem = item->broadcastPLMNs.list.array[k];
                    if (!bcastItem) continue;
                    Plmn plmn;
                    xnPlmnDecode(bcastItem->plmn_id, plmn);
                    // Avoid duplicates when the same PLMN appears across multiple TAIs
                    bool found = false;
                    for (auto &p : peer.plmnList)
                        if (p.mcc == plmn.mcc && p.mnc == plmn.mnc) { found = true; break; }
                    if (!found)
                        peer.plmnList.push_back(plmn);
                }
            }
            hasTaiList = true;
            asn::Free(asn_DEF_ASN_XNAP_TAISupport_List, taiList);
            break;
        }

        // -------------------------------------------------------------------
        // IE 4 — AMF-Region-Information  (mandatory, criticality=reject)
        //   Each GlobalAMF-Region-Information entry carries a PLMN and an
        //   8-bit AMF region ID.  We store only the region IDs; PLMN info
        //   is already captured from TAISupport-List.
        // -------------------------------------------------------------------
        case XNAP_IE_AMF_Region_Information: {
            auto *amfInfo = xnap_encode::Decode<ASN_XNAP_AMF_Region_Information_t>(
                asn_DEF_ASN_XNAP_AMF_Region_Information, vbuf, vsize);
            if (!amfInfo)
            {
                m_logger->warn("xnSetupRequestReceive: cannot decode AMF-Region-Information from gnbId=%d", gnbId);
                break;
            }

            for (int j = 0; j < amfInfo->list.count; ++j)
            {
                auto *entry = amfInfo->list.array[j];
                if (!entry) continue;
                // amf_region_id is a BIT_STRING of exactly 8 bits (TS 38.413 §9.3.3.1)
                if (entry->amf_region_id.size > 0)
                    peer.amfRegionList.push_back(asn::GetBitStringInt<8>(entry->amf_region_id));
            }
            hasAmfRegion = true;
            asn::Free(asn_DEF_ASN_XNAP_AMF_Region_Information, amfInfo);
            break;
        }

        // -------------------------------------------------------------------
        // IE 19 — List-of-served-cells-NR  (optional, criticality=reject)
        //   First cell gives us the authoritative NCI (from NR-CGI) and the
        //   physical cell ID (nrPCI).  Remaining cells are ignored for now —
        //   the peer table is keyed per gNB, not per cell.
        // -------------------------------------------------------------------
        case XNAP_IE_List_of_served_cells_NR: {
            auto *servedCells = xnap_encode::Decode<ASN_XNAP_ServedCells_NR_t>(
                asn_DEF_ASN_XNAP_ServedCells_NR, vbuf, vsize);
            if (!servedCells) break;

            if (servedCells->list.count > 0 && servedCells->list.array[0])
            {
                const auto &info = servedCells->list.array[0]->served_cell_info_NR;
                peer.nrPCI = static_cast<int>(info.nrPCI);
                // NR-Cell-Identity is a 36-bit BIT_STRING; overrides the NCI base
                // derived from GlobalNG-RANNode-ID above.
                peer.nci = asn::GetBitStringLong<36>(info.cellID.nr_CI);
            }
            asn::Free(asn_DEF_ASN_XNAP_ServedCells_NR, servedCells);
            break;
        }

        default:
            break;
        }
    }

    asn::Free(asn_DEF_ASN_XNAP_XnSetupRequest, xnReq);

    // ------------------------------------------------------------------
    // Validate that all three mandatory IEs were present and decoded.
    // ------------------------------------------------------------------
    if (!hasGlobalId || !hasTaiList || !hasAmfRegion)
    {
        m_logger->err("xnSetupRequestReceive: missing mandatory IE(s) "
                      "(globalId=%d taiList=%d amfRegion=%d) from gnbId=%d",
                      hasGlobalId, hasTaiList, hasAmfRegion, gnbId);
        xnSetupFailureSend(gnbId);
        return;
    }

    // ------------------------------------------------------------------
    // Store peer info and send success response.
    // ------------------------------------------------------------------
    m_xnPeerTable.addPeerInfo(peer);

    m_logger->info("XnSetupRequest from gnbId=%d: nci=%ld pci=%d tacs=%zu plmns=%zu amfRegions=%zu",
                   gnbId, peer.nci, peer.nrPCI,
                   peer.tacList.size(), peer.plmnList.size(), peer.amfRegionList.size());

    xnSetupResponseSend(gnbId);
}

void XnTask::xnSetupResponseSend(int gnbId)
{
    m_logger->debug("xnSetupResponseSend gnbId=%d", gnbId);

    const GnbConfig *cfg = m_base->config;

    // -----------------------------------------------------------------------
    // IE 1 — GlobalNG-RANNode-ID  (mandatory, criticality=reject)
    // -----------------------------------------------------------------------

    auto *gnbGlobalId = asn::New<ASN_XNAP_GlobalgNB_ID_t>();
    setXnPlmn(gnbGlobalId->plmn_id, cfg->plmn);

    gnbGlobalId->gnb_id.present = ASN_XNAP_GNB_ID_Choice_PR_gnb_ID;
    asn::SetBitString(gnbGlobalId->gnb_id.choice.gnb_ID,
                      octet4{cfg->getGnbId() << (32 - cfg->gnbIdLength)},
                      static_cast<size_t>(cfg->gnbIdLength));

    auto *globalNodeId = asn::New<ASN_XNAP_GlobalNG_RANNode_ID_t>();
    globalNodeId->present = ASN_XNAP_GlobalNG_RANNode_ID_PR_gNB;
    globalNodeId->choice.gNB = gnbGlobalId;

    auto *ieGlobalId = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieGlobalId->id          = XNAP_IE_GlobalNG_RAN_Node_ID;
    ieGlobalId->criticality = ASN_XNAP_Criticality_reject;
    if (!setIeValue(ieGlobalId, &asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, globalNodeId))
    {
        m_logger->err("xnSetupResponseSend: failed to encode GlobalNG-RANNode-ID");
        asn::Free(asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, globalNodeId);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGlobalId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, globalNodeId);

    // -----------------------------------------------------------------------
    // IE 2 — TAISupport-List  (mandatory, criticality=reject)
    // -----------------------------------------------------------------------

    auto *snssai = asn::New<ASN_XNAP_S_NSSAI_t>();
    if (!cfg->nssai.slices.empty())
    {
        asn::SetOctetString1(snssai->sst, static_cast<uint8_t>(cfg->nssai.slices[0].sst));
        if (cfg->nssai.slices[0].sd.has_value())
        {
            snssai->sd = asn::New<OCTET_STRING_t>();
            asn::SetOctetString3(*snssai->sd, octet3{cfg->nssai.slices[0].sd.value()});
        }
    }
    else
    {
        asn::SetOctetString1(snssai->sst, 1);
    }

    auto *broadcastPlmnTai = asn::New<ASN_XNAP_BroadcastPLMNinTAISupport_Item_t>();
    setXnPlmn(broadcastPlmnTai->plmn_id, cfg->plmn);
    asn::SequenceAdd(broadcastPlmnTai->tAISliceSupport_List, snssai);

    auto *taiItem = asn::New<ASN_XNAP_TAISupport_Item_t>();
    asn::SetOctetString3(taiItem->tac, octet3{cfg->tac});
    asn::SequenceAdd(taiItem->broadcastPLMNs, broadcastPlmnTai);

    auto *taiList = asn::New<ASN_XNAP_TAISupport_List_t>();
    asn::SequenceAdd(*taiList, taiItem);

    auto *ieTaiList = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieTaiList->id          = XNAP_IE_TAISupport_List;
    ieTaiList->criticality = ASN_XNAP_Criticality_reject;
    if (!setIeValue(ieTaiList, &asn_DEF_ASN_XNAP_TAISupport_List, taiList))
    {
        m_logger->err("xnSetupResponseSend: failed to encode TAISupport-List");
        asn::Free(asn_DEF_ASN_XNAP_TAISupport_List, taiList);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieTaiList);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieGlobalId);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_TAISupport_List, taiList);

    // -----------------------------------------------------------------------
    // IE 3 — AMF-Region-Information  (optional in response, criticality=reject)
    //   Include it when we can encode it; skip silently on failure rather than
    //   aborting the whole response (the spec marks it OPTIONAL here).
    // -----------------------------------------------------------------------

    ASN_XNAP_ProtocolIE_Field_14202P0_t *ieAmfRegion = nullptr;

    auto *amfRegionEntry = asn::New<ASN_XNAP_GlobalAMF_Region_Information_t>();
    setXnPlmn(amfRegionEntry->plmn_ID, cfg->plmn);
    asn::SetBitStringInt<8>(0, amfRegionEntry->amf_region_id); // Assumption: placeholder 0

    auto *amfRegionInfo = asn::New<ASN_XNAP_AMF_Region_Information_t>();
    asn::SequenceAdd(*amfRegionInfo, amfRegionEntry);

    ieAmfRegion = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieAmfRegion->id          = XNAP_IE_AMF_Region_Information;
    ieAmfRegion->criticality = ASN_XNAP_Criticality_reject;
    if (!setIeValue(ieAmfRegion, &asn_DEF_ASN_XNAP_AMF_Region_Information, amfRegionInfo))
    {
        m_logger->warn("xnSetupResponseSend: failed to encode AMF-Region-Information (optional); skipping");
        asn::Free(asn_DEF_ASN_XNAP_AMF_Region_Information, amfRegionInfo);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieAmfRegion);
        ieAmfRegion = nullptr;
    }
    else
    {
        asn::Free(asn_DEF_ASN_XNAP_AMF_Region_Information, amfRegionInfo);
    }

    // -----------------------------------------------------------------------
    // IE 4 — List-of-served-cells-NR  (optional, criticality=reject)
    // -----------------------------------------------------------------------

    auto *nrCgi = asn::New<ASN_XNAP_NR_CGI_t>();
    setXnPlmn(nrCgi->plmn_id, cfg->plmn);
    asn::SetBitStringLong<36>(cfg->nci, nrCgi->nr_CI);

    auto *bcastPlmn = asn::New<ASN_XNAP_PLMN_Identity_t>();
    asn::SetOctetString3(*bcastPlmn, ngap_utils::PlmnToOctet3(cfg->plmn));

    auto *broadcastPlmns = asn::New<ASN_XNAP_BroadcastPLMNs_t>();
    asn::SequenceAdd(*broadcastPlmns, bcastPlmn);

    auto *freqBandItem = asn::New<ASN_XNAP_NRFrequencyBandItem_t>();
    freqBandItem->nr_frequency_band = 78;

    auto *freqBandList = asn::New<ASN_XNAP_NRFrequencyBand_List_t>();
    asn::SequenceAdd(*freqBandList, freqBandItem);

    auto *freqInfo = asn::New<ASN_XNAP_NRFrequencyInfo_t>();
    freqInfo->nrARFCN = 632628;
    freqInfo->frequencyBand_List = *freqBandList;
    free(freqBandList);

    auto *txBw = asn::New<ASN_XNAP_NRTransmissionBandwidth_t>();
    txBw->nRSCS = ASN_XNAP_NRSCS_scs30;
    txBw->nRNRB = ASN_XNAP_NRNRB_nrb106;

    auto *tddInfo = asn::New<ASN_XNAP_NRModeInfoTDD_t>();
    tddInfo->nrFrequencyInfo      = *freqInfo;
    free(freqInfo);
    tddInfo->nrTransmissonBandwidth = *txBw;
    free(txBw);

    auto *modeInfo = asn::New<ASN_XNAP_NRModeInfo_t>();
    modeInfo->present       = ASN_XNAP_NRModeInfo_PR_tdd;
    modeInfo->choice.tdd    = tddInfo;

    static const uint8_t dummyMtc = 0x00;
    OCTET_STRING_t mtcOctet{};
    OCTET_STRING_fromBuf(&mtcOctet, reinterpret_cast<const char *>(&dummyMtc), 1);

    ASN_XNAP_Connectivity_Support_t connSupport{};
    connSupport.eNDC_Support = ASN_XNAP_Connectivity_Support__eNDC_Support_not_supported;

    auto *cellInfo = asn::New<ASN_XNAP_ServedCellInformation_NR_t>();
    cellInfo->nrPCI             = cfg->nci;
    cellInfo->cellID            = *nrCgi;
    free(nrCgi);
    asn::SetOctetString3(cellInfo->tac, octet3{cfg->tac});
    cellInfo->broadcastPLMN     = *broadcastPlmns;
    free(broadcastPlmns);
    cellInfo->nrModeInfo        = *modeInfo;
    free(modeInfo);
    cellInfo->measurementTimingConfiguration = mtcOctet;
    cellInfo->connectivitySupport = connSupport;

    auto *servedCellItem = asn::New<ASN_XNAP_ServedCells_NR_Item_t>();
    servedCellItem->served_cell_info_NR = *cellInfo;
    free(cellInfo);

    auto *servedCells = asn::New<ASN_XNAP_ServedCells_NR_t>();
    asn::SequenceAdd(*servedCells, servedCellItem);

    ASN_XNAP_ProtocolIE_Field_14202P0_t *ieServedCells = nullptr;
    ieServedCells = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieServedCells->id          = XNAP_IE_List_of_served_cells_NR;
    ieServedCells->criticality = ASN_XNAP_Criticality_reject;
    if (!setIeValue(ieServedCells, &asn_DEF_ASN_XNAP_ServedCells_NR, servedCells))
    {
        m_logger->warn("xnSetupResponseSend: failed to encode ServedCells-NR (optional); skipping");
        asn::Free(asn_DEF_ASN_XNAP_ServedCells_NR, servedCells);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieServedCells);
        ieServedCells = nullptr;
    }
    else
    {
        asn::Free(asn_DEF_ASN_XNAP_ServedCells_NR, servedCells);
    }

    // -----------------------------------------------------------------------
    // Assemble XnSetupResponse ProtocolIE container
    // -----------------------------------------------------------------------

    auto *xnSetupResp = asn::New<ASN_XNAP_XnSetupResponse_t>();
    asn::SequenceAdd(xnSetupResp->protocolIEs, ieGlobalId);
    asn::SequenceAdd(xnSetupResp->protocolIEs, ieTaiList);
    if (ieAmfRegion)
        asn::SequenceAdd(xnSetupResp->protocolIEs, ieAmfRegion);
    if (ieServedCells)
        asn::SequenceAdd(xnSetupResp->protocolIEs, ieServedCells);

    // -----------------------------------------------------------------------
    // Wrap in SuccessfulOutcome with procedure code 17 (XnSetup)
    // -----------------------------------------------------------------------

    auto *succMsg = asn::New<ASN_XNAP_SuccessfulOutcome_t>();
    succMsg->procedureCode = XN_PROC_XN_SETUP;
    succMsg->criticality   = ASN_XNAP_Criticality_reject;
    if (ANY_fromType_aper(&succMsg->value, &asn_DEF_ASN_XNAP_XnSetupResponse, xnSetupResp) != 0)
    {
        m_logger->err("xnSetupResponseSend: failed to encode XnSetupResponse into SuccessfulOutcome");
        asn::Free(asn_DEF_ASN_XNAP_XnSetupResponse, xnSetupResp);
        asn::Free(asn_DEF_ASN_XNAP_SuccessfulOutcome, succMsg);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_XnSetupResponse, xnSetupResp);

    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present                   = ASN_XNAP_XnAP_PDU_PR_successfulOutcome;
    outerPdu->choice.successfulOutcome  = succMsg;

    // -----------------------------------------------------------------------
    // APER-encode and send via SCTP back to the requesting gNB (clientId = gnbId)
    // -----------------------------------------------------------------------

    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        m_logger->err("xnSetupResponseSend: APER encoding failed for gnbId=%d", gnbId);
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return;
    }

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = gnbId;
    sctpMsg->stream   = NON_UE_ASSOCIATED_STREAM_ID;
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->sctpTask->push(std::move(sctpMsg));

    m_logger->info("XnSetupResponse sent to gnbId=%d", gnbId);

    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
}

void XnTask::xnSetupResponseReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("xnSetupResponseReceive gnbId=%d", gnbId);

    auto *succMsg = pdu->choice.successfulOutcome;
    if (!succMsg || !succMsg->value.buf)
    {
        m_logger->err("xnSetupResponseReceive: null successfulOutcome from gnbId=%d", gnbId);
        return;
    }

    auto *xnResp = xnap_encode::Decode<ASN_XNAP_XnSetupResponse_t>(
        asn_DEF_ASN_XNAP_XnSetupResponse,
        reinterpret_cast<const uint8_t *>(succMsg->value.buf),
        static_cast<size_t>(succMsg->value.size));
    if (!xnResp)
    {
        m_logger->err("xnSetupResponseReceive: APER decode failed for gnbId=%d", gnbId);
        return;
    }

    XnPeerInfo peer;
    peer.gnbId = gnbId;

    bool hasGlobalId = false;
    bool hasTaiList  = false;

    for (int i = 0; i < xnResp->protocolIEs.list.count; ++i)
    {
        auto *ie = xnResp->protocolIEs.list.array[i];
        if (!ie || !ie->value.buf)
            continue;

        const uint8_t *vbuf  = reinterpret_cast<const uint8_t *>(ie->value.buf);
        const size_t   vsize = static_cast<size_t>(ie->value.size);

        switch (ie->id)
        {
        case XNAP_IE_GlobalNG_RAN_Node_ID: {
            auto *nodeId = xnap_encode::Decode<ASN_XNAP_GlobalNG_RANNode_ID_t>(
                asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, vbuf, vsize);
            if (!nodeId)
            {
                m_logger->warn("xnSetupResponseReceive: cannot decode GlobalNG-RANNode-ID from gnbId=%d", gnbId);
                break;
            }

            if (nodeId->present == ASN_XNAP_GlobalNG_RANNode_ID_PR_gNB &&
                nodeId->choice.gNB &&
                nodeId->choice.gNB->gnb_id.present == ASN_XNAP_GNB_ID_Choice_PR_gnb_ID)
            {
                const BIT_STRING_t &bs = nodeId->choice.gNB->gnb_id.choice.gnb_ID;
                int gnbIdLength = static_cast<int>(bs.size * 8) - bs.bits_unused;
                int64_t gnbIdVal = asn::GetBitStringLong<32>(bs);
                peer.nci = gnbIdVal << (36 - gnbIdLength);
                hasGlobalId = true;
            }
            asn::Free(asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, nodeId);
            break;
        }

        case XNAP_IE_TAISupport_List: {
            auto *taiList = xnap_encode::Decode<ASN_XNAP_TAISupport_List_t>(
                asn_DEF_ASN_XNAP_TAISupport_List, vbuf, vsize);
            if (!taiList)
            {
                m_logger->warn("xnSetupResponseReceive: cannot decode TAISupport-List from gnbId=%d", gnbId);
                break;
            }

            for (int j = 0; j < taiList->list.count; ++j)
            {
                auto *item = taiList->list.array[j];
                if (!item) continue;

                if (item->tac.size == 3)
                    peer.tacList.push_back(static_cast<int32_t>(asn::GetOctet3(item->tac)));

                for (int k = 0; k < item->broadcastPLMNs.list.count; ++k)
                {
                    auto *bcastItem = item->broadcastPLMNs.list.array[k];
                    if (!bcastItem) continue;
                    Plmn plmn;
                    xnPlmnDecode(bcastItem->plmn_id, plmn);
                    bool found = false;
                    for (auto &p : peer.plmnList)
                        if (p.mcc == plmn.mcc && p.mnc == plmn.mnc) { found = true; break; }
                    if (!found)
                        peer.plmnList.push_back(plmn);
                }
            }
            hasTaiList = true;
            asn::Free(asn_DEF_ASN_XNAP_TAISupport_List, taiList);
            break;
        }

        // AMF-Region-Information is optional in XnSetupResponse (per TS 38.423 Table 9.1.2.2-1)
        case XNAP_IE_AMF_Region_Information: {
            auto *amfInfo = xnap_encode::Decode<ASN_XNAP_AMF_Region_Information_t>(
                asn_DEF_ASN_XNAP_AMF_Region_Information, vbuf, vsize);
            if (!amfInfo)
            {
                m_logger->warn("xnSetupResponseReceive: cannot decode AMF-Region-Information from gnbId=%d", gnbId);
                break;
            }

            for (int j = 0; j < amfInfo->list.count; ++j)
            {
                auto *entry = amfInfo->list.array[j];
                if (!entry) continue;
                if (entry->amf_region_id.size > 0)
                    peer.amfRegionList.push_back(asn::GetBitStringInt<8>(entry->amf_region_id));
            }
            asn::Free(asn_DEF_ASN_XNAP_AMF_Region_Information, amfInfo);
            break;
        }

        case XNAP_IE_List_of_served_cells_NR: {
            auto *servedCells = xnap_encode::Decode<ASN_XNAP_ServedCells_NR_t>(
                asn_DEF_ASN_XNAP_ServedCells_NR, vbuf, vsize);
            if (!servedCells) break;

            if (servedCells->list.count > 0 && servedCells->list.array[0])
            {
                const auto &info = servedCells->list.array[0]->served_cell_info_NR;
                peer.nrPCI = static_cast<int>(info.nrPCI);
                peer.nci = asn::GetBitStringLong<36>(info.cellID.nr_CI);
            }
            asn::Free(asn_DEF_ASN_XNAP_ServedCells_NR, servedCells);
            break;
        }

        default:
            break;
        }
    }

    asn::Free(asn_DEF_ASN_XNAP_XnSetupResponse, xnResp);

    if (!hasGlobalId || !hasTaiList)
    {
        m_logger->err("xnSetupResponseReceive: missing mandatory IE(s) "
                      "(globalId=%d taiList=%d) from gnbId=%d",
                      hasGlobalId, hasTaiList, gnbId);
        return;
    }

    if (!m_xnPeerTable.addPeerInfo(peer))
    {
        m_logger->err("xnSetupResponseReceive: failed to add peer info for gnbId=%d", gnbId);
        return;
    }

    m_logger->info("XnSetupResponse from gnbId=%d: nci=%ld pci=%d tacs=%zu plmns=%zu amfRegions=%zu",
                   gnbId, peer.nci, peer.nrPCI,
                   peer.tacList.size(), peer.plmnList.size(), peer.amfRegionList.size());
}

void XnTask::xnSetupFailureSend(int gnbId)
{
    m_logger->debug("xnSetupFailureSend gnbId=%d", gnbId);

    // Cause: mandatory IEs were absent — protocol / abstract_syntax_error_reject
    ASN_XNAP_Cause_t cause{};
    cause.present          = ASN_XNAP_Cause_PR_protocol;
    cause.choice.protocol  = ASN_XNAP_CauseProtocol_abstract_syntax_error_reject;

    auto *ieCause = asn::New<ASN_XNAP_ProtocolIE_Field_14202P0_t>();
    ieCause->id          = XNAP_IE_Cause;
    ieCause->criticality = ASN_XNAP_Criticality_ignore;
    if (!setIeValue(ieCause, &asn_DEF_ASN_XNAP_Cause, &cause))
    {
        m_logger->err("xnSetupFailureSend: failed to encode Cause");
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P0, ieCause);
        return;
    }

    auto *xnSetupFail = asn::New<ASN_XNAP_XnSetupFailure_t>();
    asn::SequenceAdd(xnSetupFail->protocolIEs, ieCause);

    auto *unsuccMsg = asn::New<ASN_XNAP_UnsuccessfulOutcome_t>();
    unsuccMsg->procedureCode = XN_PROC_XN_SETUP;
    unsuccMsg->criticality   = ASN_XNAP_Criticality_reject;
    if (ANY_fromType_aper(&unsuccMsg->value, &asn_DEF_ASN_XNAP_XnSetupFailure, xnSetupFail) != 0)
    {
        m_logger->err("xnSetupFailureSend: failed to encode XnSetupFailure into UnsuccessfulOutcome");
        asn::Free(asn_DEF_ASN_XNAP_XnSetupFailure, xnSetupFail);
        asn::Free(asn_DEF_ASN_XNAP_UnsuccessfulOutcome, unsuccMsg);
        return;
    }
    asn::Free(asn_DEF_ASN_XNAP_XnSetupFailure, xnSetupFail);

    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present                    = ASN_XNAP_XnAP_PDU_PR_unsuccessfulOutcome;
    outerPdu->choice.unsuccessfulOutcome = unsuccMsg;

    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        m_logger->err("xnSetupFailureSend: APER encoding failed for gnbId=%d", gnbId);
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return;
    }

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = gnbId;
    sctpMsg->stream   = NON_UE_ASSOCIATED_STREAM_ID;
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->sctpTask->push(std::move(sctpMsg));

    m_logger->info("XnSetupFailure sent to gnbId=%d", gnbId);

    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
}

void XnTask::xnSetupFailureReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("xnSetupFailureReceive gnbId=%d", gnbId);

    auto *unsuccMsg = pdu->choice.unsuccessfulOutcome;
    if (!unsuccMsg || !unsuccMsg->value.buf)
    {
        m_logger->err("XnSetupFailure from gnbId=%d: malformed UnsuccessfulOutcome; Xn setup rejected", gnbId);
        return;
    }

    auto *xnFail = xnap_encode::Decode<ASN_XNAP_XnSetupFailure_t>(
        asn_DEF_ASN_XNAP_XnSetupFailure,
        reinterpret_cast<const uint8_t *>(unsuccMsg->value.buf),
        static_cast<size_t>(unsuccMsg->value.size));
    if (!xnFail)
    {
        m_logger->err("XnSetupFailure from gnbId=%d: APER decode failed; Xn setup rejected", gnbId);
        return;
    }

    // Decode the mandatory Cause IE and log it.  Peer is NOT added to m_xnPeerTable.
    for (int i = 0; i < xnFail->protocolIEs.list.count; ++i)
    {
        auto *ie = xnFail->protocolIEs.list.array[i];
        if (!ie || ie->id != XNAP_IE_Cause || !ie->value.buf)
            continue;

        auto *cause = xnap_encode::Decode<ASN_XNAP_Cause_t>(
            asn_DEF_ASN_XNAP_Cause,
            reinterpret_cast<const uint8_t *>(ie->value.buf),
            static_cast<size_t>(ie->value.size));
        if (!cause)
            break;

        switch (cause->present)
        {
        case ASN_XNAP_Cause_PR_radioNetwork:
            m_logger->err("XnSetupFailure from gnbId=%d: cause=radioNetwork(%ld)", gnbId, cause->choice.radioNetwork);
            break;
        case ASN_XNAP_Cause_PR_transport:
            m_logger->err("XnSetupFailure from gnbId=%d: cause=transport(%ld)", gnbId, cause->choice.transport);
            break;
        case ASN_XNAP_Cause_PR_protocol:
            m_logger->err("XnSetupFailure from gnbId=%d: cause=protocol(%ld)", gnbId, cause->choice.protocol);
            break;
        case ASN_XNAP_Cause_PR_misc:
            m_logger->err("XnSetupFailure from gnbId=%d: cause=misc(%ld)", gnbId, cause->choice.misc);
            break;
        default:
            m_logger->err("XnSetupFailure from gnbId=%d: cause=unknown", gnbId);
            break;
        }
        asn::Free(asn_DEF_ASN_XNAP_Cause, cause);
        break;
    }

    asn::Free(asn_DEF_ASN_XNAP_XnSetupFailure, xnFail);
}


} // namespace nr::gnb