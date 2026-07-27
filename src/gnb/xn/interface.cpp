#include "task.hpp"
#include "encode.hpp"

#include <gnb/neighbors.hpp>
#include <gnb/ngap/utils.hpp>
#include <lib/asn/utils.hpp>
#include <utils/common.hpp>

extern "C"
{
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


// Build a 3-byte BCD-encoded PLMN octet string into dst (same layout as NGAP).
static void setXnPlmn(ASN_XNAP_PLMN_Identity_t &dst, const Plmn &plmn)
{
    asn::SetOctetString3(dst, ngap_utils::PlmnToOctet3(plmn));
}


// called when an SCTP association comes up — either our outbound connection to
// a neighbor (positive clientId == gnbId; we initiate the XnSetup procedure) or
// an accepted inbound association (negative clientId; peer identity unknown
// until its XnSetupRequest arrives, so it is parked in m_pendingInbound).
void XnTask::handleAssociationSetup(int clientId, int ascId, int inCount, int outCount)
{
    if (clientId < 0)
    {
        m_logger->info("Inbound Xn association up (clientId=%d), awaiting XnSetupRequest", clientId);
        m_pendingInbound[clientId] =
            PendingInboundAssoc{ascId, inCount, outCount, static_cast<uint64_t>(utils::CurrentTimeMillis())};
        return;
    }

    auto *gnb = m_xnPeerTable.getPeerInfo(clientId);

    if (gnb != nullptr)
    {
        // update SCTP association info
        auto assoc = SctpAssociation();
        assoc.associationId = ascId;
        assoc.inStreams = inCount;
        assoc.outStreams = outCount;
        gnb->sctpAssoc = assoc;

        // reset stream ID manager, partitioning the UE-associated stream space by
        // gnbId (lower gnbId → EVEN, higher → ODD) so opposing handovers never
        // collide on a stream ID.
        const int myGnbId = static_cast<int>(m_base->config->getGnbId());
        const StreamParity parity = (myGnbId < gnb->gnbId) ? StreamParity::Even : StreamParity::Odd;
        gnb->streamIdManager.resetStreams(inCount, outCount, parity);

        // trigger XNAP setup procedure by sending an XnSetupRequest on the new association
        xnSetupRequestSend(clientId);
    }
    else
    {
        m_logger->err("Failed to find XnPeerNode for gnbId=%d, SCTP setup failed", clientId);
    }
}

void XnTask::handleAssociationShutdown(int clientId)
{
    // An inbound association that never completed Xn Setup just gets dropped.
    if (m_pendingInbound.erase(clientId) > 0)
    {
        auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
        w->clientId = clientId;
        m_base->xnSctpTask->push(std::move(w));
        return;
    }

    auto *gnb = m_xnPeerTable.findByClientId(clientId);
    if (gnb == nullptr)
        return;

    const int gnbId = gnb->gnbId;

    m_logger->info("Association terminated for gNB[%d]", gnbId);
    // Resolve handovers before removing peer metadata; cleanup messages use
    // only local task queues and remain safe after the SCTP association is gone.
    handlePeerHandoverLoss(gnbId);
    m_logger->debug("Removing gNB[%d] from peer table", gnbId);

    m_xnPeerTable.removePeerInfo(gnbId);

    auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
    w->clientId = clientId;
    m_base->xnSctpTask->push(std::move(w));

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

    auto *ieGlobalId = asn::New<ASN_XNAP_ProtocolIE_Field_14202P118_t>();
    ieGlobalId->id          = XNAP_IE_GlobalNG_RAN_Node_ID;
    ieGlobalId->criticality = ASN_XNAP_Criticality_reject;
    ieGlobalId->value.present = ASN_XNAP_ProtocolIE_Field_14202P118__value_PR_GlobalNG_RANNode_ID;
    ieGlobalId->value.choice.GlobalNG_RANNode_ID = *globalNodeId;
    free(globalNodeId);   // contents moved into the IE


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

    auto *ieTaiList = asn::New<ASN_XNAP_ProtocolIE_Field_14202P118_t>();
    ieTaiList->id          = XNAP_IE_TAISupport_List;
    ieTaiList->criticality = ASN_XNAP_Criticality_reject;
    ieTaiList->value.present = ASN_XNAP_ProtocolIE_Field_14202P118__value_PR_TAISupport_List;
    ieTaiList->value.choice.TAISupport_List = *taiList;
    free(taiList);   // contents moved into the IE


    // -----------------------------------------------------------------------
    // IE 3 — AMF-Region-Information  (mandatory, criticality=reject)
    //   Identifies the AMF region(s) reachable via this gNB.
    //   Uses the PLMN from config and the AMF region ID advertised by the
    //   connected AMF in its served GUAMI list.
    // -----------------------------------------------------------------------

    auto *amfRegionEntry = asn::New<ASN_XNAP_GlobalAMF_Region_Information_t>();
    setXnPlmn(amfRegionEntry->plmn_ID, cfg->plmn);

    // amf_region_id is a BIT_STRING of exactly 8 bits (TS 38.413 clause 9.3.3.1).
    auto *amf = m_base->ngapTask->getConnectedAmfContextForXn();
    if (amf == nullptr)
    {
        m_logger->err("xnSetupRequestSend: no connected AMF with a served GUAMI");
        asn::Free(asn_DEF_ASN_XNAP_GlobalAMF_Region_Information, amfRegionEntry);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P118, ieTaiList);
        asn::Free(asn_DEF_ASN_XNAP_ProtocolIE_Field_14202P118, ieGlobalId);
        return;
    }
    asn::SetBitStringInt<8>(amf->servedGuamiList.front()->guami.amfRegionId,
                            amfRegionEntry->amf_region_id);

    auto *amfRegionInfo = asn::New<ASN_XNAP_AMF_Region_Information_t>();
    asn::SequenceAdd(*amfRegionInfo, amfRegionEntry);

    auto *ieAmfRegion = asn::New<ASN_XNAP_ProtocolIE_Field_14202P118_t>();
    ieAmfRegion->id          = XNAP_IE_AMF_Region_Information;
    ieAmfRegion->criticality = ASN_XNAP_Criticality_reject;
    ieAmfRegion->value.present = ASN_XNAP_ProtocolIE_Field_14202P118__value_PR_AMF_Region_Information;
    ieAmfRegion->value.choice.AMF_Region_Information = *amfRegionInfo;
    free(amfRegionInfo);   // contents moved into the IE


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

    auto *ieServedCells = asn::New<ASN_XNAP_ProtocolIE_Field_14202P118_t>();
    ieServedCells->id          = XNAP_IE_List_of_served_cells_NR;
    ieServedCells->criticality = ASN_XNAP_Criticality_reject;
    ieServedCells->value.present = ASN_XNAP_ProtocolIE_Field_14202P118__value_PR_ServedCells_NR;
    ieServedCells->value.choice.ServedCells_NR = *servedCells;
    free(servedCells);   // contents moved into the IE


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
    initMsg->value.present = ASN_XNAP_InitiatingMessage__value_PR_XnSetupRequest;
    initMsg->value.choice.XnSetupRequest = *xnSetupReq;
    free(xnSetupReq);   // contents moved into the message


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
    sctpMsg->clientId = gnbId;  // initiator-only path: outbound clientId == neighbor gnbId
    sctpMsg->stream   = NON_UE_ASSOCIATED_STREAM_ID;
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->xnSctpTask->push(std::move(sctpMsg));

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

void XnTask::xnSetupRequestReceive(int clientId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("xnSetupRequestReceive clientId=%d", clientId);

    auto *initMsg = pdu->choice.initiatingMessage;
    if (!initMsg || initMsg->value.present != ASN_XNAP_InitiatingMessage__value_PR_XnSetupRequest)
    {
        m_logger->err("xnSetupRequestReceive: not an XnSetupRequest (clientId=%d)", clientId);
        xnSetupFailureSend(clientId);
        return;
    }

    // The open type is decoded in place along with the enclosing PDU, so the
    // value already is an XnSetupRequest -- no second decode. It belongs to the
    // PDU, so it must not be freed here.
    auto *xnReq = &initMsg->value.choice.XnSetupRequest;

    XnPeerInfo peer;
    // The peer's identity comes from the message (GlobalNG-RANNode-ID), not
    // from the transport: inbound associations have provisional negative
    // clientIds until this binding is made.
    int decodedGnbId = -1;

    bool hasGlobalId = false;
    bool hasTaiList  = false;
    bool hasAmfRegion = false;

    for (int i = 0; i < xnReq->protocolIEs.list.count; ++i)
    {
        auto *ie = xnReq->protocolIEs.list.array[i];
        if (!ie)
            continue;


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
            if (ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P118__value_PR_GlobalNG_RANNode_ID)
            {
                m_logger->warn("xnSetupRequestReceive: cannot decode GlobalNG-RANNode-ID (clientId=%d)", clientId);
                break;
            }
            // Decoded in place with the PDU; borrowed, not owned.
            auto *nodeId = &ie->value.choice.GlobalNG_RANNode_ID;

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
                // The bit-string value IS the gNB ID — same derivation as
                // GnbConfig/GnbNeighborState::getGnbId() (nci >> (36 - idLength)).
                decodedGnbId = static_cast<int>(gnbIdVal);
                hasGlobalId = true;
            }
            break;
        }

        // -------------------------------------------------------------------
        // IE 75 — TAISupport-List  (mandatory, criticality=reject)
        //   For each TAISupport-Item: record the TAC (3-byte int) and
        //   the set of broadcast PLMNs.  Duplicate PLMNs across TAIs are
        //   stored only once (checked by mcc/mnc/isLongMnc equality).
        // -------------------------------------------------------------------
        case XNAP_IE_TAISupport_List: {
            if (ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P118__value_PR_TAISupport_List)
            {
                m_logger->warn("xnSetupRequestReceive: cannot decode TAISupport-List (clientId=%d)", clientId);
                break;
            }
            // Decoded in place with the PDU; borrowed, not owned.
            auto *taiList = &ie->value.choice.TAISupport_List;

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
            break;
        }

        // -------------------------------------------------------------------
        // IE 4 — AMF-Region-Information  (mandatory, criticality=reject)
        //   Each GlobalAMF-Region-Information entry carries a PLMN and an
        //   8-bit AMF region ID.  We store only the region IDs; PLMN info
        //   is already captured from TAISupport-List.
        // -------------------------------------------------------------------
        case XNAP_IE_AMF_Region_Information: {
            if (ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P118__value_PR_AMF_Region_Information)
            {
                m_logger->warn("xnSetupRequestReceive: cannot decode AMF-Region-Information (clientId=%d)", clientId);
                break;
            }
            // Decoded in place with the PDU; borrowed, not owned.
            auto *amfInfo = &ie->value.choice.AMF_Region_Information;

            for (int j = 0; j < amfInfo->list.count; ++j)
            {
                auto *entry = amfInfo->list.array[j];
                if (!entry) continue;
                // amf_region_id is a BIT_STRING of exactly 8 bits (TS 38.413 §9.3.3.1)
                if (entry->amf_region_id.size > 0)
                    peer.amfRegionList.push_back(asn::GetBitStringInt<8>(entry->amf_region_id));
            }
            hasAmfRegion = true;
            break;
        }

        // -------------------------------------------------------------------
        // IE 19 — List-of-served-cells-NR  (optional, criticality=reject)
        //   First cell gives us the authoritative NCI (from NR-CGI) and the
        //   physical cell ID (nrPCI).  Remaining cells are ignored for now —
        //   the peer table is keyed per gNB, not per cell.
        // -------------------------------------------------------------------
        case XNAP_IE_List_of_served_cells_NR: {
            if (ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P118__value_PR_ServedCells_NR) break;
            // Decoded in place with the PDU; borrowed, not owned.
            auto *servedCells = &ie->value.choice.ServedCells_NR;

            if (servedCells->list.count > 0 && servedCells->list.array[0])
            {
                const auto &info = servedCells->list.array[0]->served_cell_info_NR;
                peer.nrPCI = static_cast<int>(info.nrPCI);
                // NR-Cell-Identity is a 36-bit BIT_STRING; overrides the NCI base
                // derived from GlobalNG-RANNode-ID above.
                peer.nci = asn::GetBitStringLong<36>(info.cellID.nr_CI);
            }
            break;
        }

        default:
            break;
        }
    }


    // ------------------------------------------------------------------
    // Validate that all three mandatory IEs were present and decoded.
    // ------------------------------------------------------------------
    if (!hasGlobalId || !hasTaiList || !hasAmfRegion)
    {
        m_logger->err("xnSetupRequestReceive: missing mandatory IE(s) "
                      "(globalId=%d taiList=%d amfRegion=%d) clientId=%d",
                      hasGlobalId, hasTaiList, hasAmfRegion, clientId);
        xnSetupFailureSend(clientId);
        return;
    }

    // ------------------------------------------------------------------
    // The neighbor store is authoritative in this simulator (it also drives
    // the RRC handover decisions): a setup request from a gNB that is not a
    // configured Xn neighbor is rejected and its association closed.
    // ------------------------------------------------------------------
    const GnbNeighborState *neighbor = nullptr;
    auto neighborList = m_base->neighbors->getAll();
    for (const auto &n : neighborList)
    {
        if (static_cast<int>(n.getGnbId()) == decodedGnbId && n.handoverInterface == EHandoverInterface::Xn)
        {
            neighbor = &n;
            break;
        }
    }
    if (neighbor == nullptr)
    {
        m_logger->err("XnSetupRequest from unknown gNB %d (clientId=%d) rejected: not a configured Xn neighbor",
                      decodedGnbId, clientId);
        xnSetupFailureSend(clientId);
        m_pendingInbound.erase(clientId);
        auto close = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
        close->clientId = clientId;
        close->associatedTask = this;
        m_base->xnSctpTask->push(std::move(close));
        return;
    }

    peer.gnbId = decodedGnbId;

    // ------------------------------------------------------------------
    // Collision guard: if this gNB already holds an association to the same
    // peer (e.g. our own outbound attempt, racing the peer's inbound one),
    // keep an established interface and reject the newcomer; otherwise the
    // newer association wins and the stale attempt is closed.
    // ------------------------------------------------------------------
    auto *existing = m_xnPeerTable.getPeerInfo(decodedGnbId);
    if (existing != nullptr && existing->clientId != clientId)
    {
        if (existing->connectionState == EXnConnectionState::CONNECTED)
        {
            m_logger->warn("XnSetupRequest from gNB %d on clientId=%d rejected: interface already CONNECTED "
                           "(clientId=%d)",
                           decodedGnbId, clientId, existing->clientId);
            xnSetupFailureSend(clientId);
            m_pendingInbound.erase(clientId);
            auto close = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
            close->clientId = clientId;
            close->associatedTask = this;
            m_base->xnSctpTask->push(std::move(close));
            return;
        }

        m_logger->warn("Replacing stale Xn association to gNB %d (old clientId=%d, new clientId=%d)",
                       decodedGnbId, existing->clientId, clientId);
        auto close = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
        close->clientId = existing->clientId;
        close->associatedTask = this;
        m_base->xnSctpTask->push(std::move(close));
    }

    // ------------------------------------------------------------------
    // Bind the association to the peer entry (creating it if needed — the
    // responder side does not pre-create entries), apply the association
    // parameters stashed at accept time, and send the success response.
    // Per TS 38.423 the responder's Xn Setup completes when the
    // XnSetupResponse is sent, so only then is the peer marked CONNECTED
    // (gating every handover send path).
    // ------------------------------------------------------------------
    auto *stored = m_xnPeerTable.applySetupInfo(peer);
    stored->clientId = clientId;
    if (neighbor->xnAddress && neighbor->xnPort)
        stored->addr = InetAddress(neighbor->xnAddress.value(), *neighbor->xnPort);

    auto pending = m_pendingInbound.find(clientId);
    if (pending != m_pendingInbound.end())
    {
        auto assoc = SctpAssociation();
        assoc.associationId = pending->second.associationId;
        assoc.inStreams = pending->second.inStreams;
        assoc.outStreams = pending->second.outStreams;
        stored->sctpAssoc = assoc;
        // Partition the UE-associated stream space by gnbId (lower → EVEN, higher
        // → ODD) so opposing handovers never collide on a stream ID.
        const int myGnbId = static_cast<int>(m_base->config->getGnbId());
        const StreamParity parity = (myGnbId < decodedGnbId) ? StreamParity::Even : StreamParity::Odd;
        stored->streamIdManager.resetStreams(pending->second.inStreams, pending->second.outStreams, parity);
        m_pendingInbound.erase(pending);
    }

    m_logger->info("XnSetupRequest from gNB %d (clientId=%d): nci=%ld pci=%d tacs=%zu plmns=%zu amfRegions=%zu",
                   decodedGnbId, clientId, peer.nci, peer.nrPCI,
                   peer.tacList.size(), peer.plmnList.size(), peer.amfRegionList.size());

    if (xnSetupResponseSend(clientId))
    {
        auto *bound = m_xnPeerTable.getPeerInfo(decodedGnbId);
        if (bound != nullptr)
            bound->connectionState = EXnConnectionState::CONNECTED;
    }
}

// Returns true if the XnSetupResponse was handed to the SCTP task (the caller
// then marks the peer CONNECTED); false if any mandatory IE or the PDU failed
// to encode.
bool XnTask::xnSetupResponseSend(int clientId)
{
    m_logger->debug("xnSetupResponseSend clientId=%d", clientId);

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

    auto *ieGlobalId = asn::New<ASN_XNAP_ProtocolIE_Field_14202P119_t>();
    ieGlobalId->id          = XNAP_IE_GlobalNG_RAN_Node_ID;
    ieGlobalId->criticality = ASN_XNAP_Criticality_reject;
    ieGlobalId->value.present = ASN_XNAP_ProtocolIE_Field_14202P119__value_PR_GlobalNG_RANNode_ID;
    ieGlobalId->value.choice.GlobalNG_RANNode_ID = *globalNodeId;
    free(globalNodeId);   // contents moved into the IE


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

    auto *ieTaiList = asn::New<ASN_XNAP_ProtocolIE_Field_14202P119_t>();
    ieTaiList->id          = XNAP_IE_TAISupport_List;
    ieTaiList->criticality = ASN_XNAP_Criticality_reject;
    ieTaiList->value.present = ASN_XNAP_ProtocolIE_Field_14202P119__value_PR_TAISupport_List;
    ieTaiList->value.choice.TAISupport_List = *taiList;
    free(taiList);   // contents moved into the IE


    // -----------------------------------------------------------------------
    // IE 3 — AMF-Region-Information  (optional in response, criticality=reject)
    //   Include it when we can encode it; skip silently on failure rather than
    //   aborting the whole response (the spec marks it OPTIONAL here).
    // -----------------------------------------------------------------------

    ASN_XNAP_ProtocolIE_Field_14202P119_t *ieAmfRegion = nullptr;

    auto *amfRegionEntry = asn::New<ASN_XNAP_GlobalAMF_Region_Information_t>();
    setXnPlmn(amfRegionEntry->plmn_ID, cfg->plmn);
    asn::SetBitStringInt<8>(0, amfRegionEntry->amf_region_id); // Assumption: placeholder 0

    auto *amfRegionInfo = asn::New<ASN_XNAP_AMF_Region_Information_t>();
    asn::SequenceAdd(*amfRegionInfo, amfRegionEntry);

    ieAmfRegion = asn::New<ASN_XNAP_ProtocolIE_Field_14202P119_t>();
    ieAmfRegion->id          = XNAP_IE_AMF_Region_Information;
    ieAmfRegion->criticality = ASN_XNAP_Criticality_reject;
    ieAmfRegion->value.present = ASN_XNAP_ProtocolIE_Field_14202P119__value_PR_AMF_Region_Information;
    ieAmfRegion->value.choice.AMF_Region_Information = *amfRegionInfo;
    free(amfRegionInfo);   // contents moved into the IE

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

    ASN_XNAP_ProtocolIE_Field_14202P119_t *ieServedCells = nullptr;
    ieServedCells = asn::New<ASN_XNAP_ProtocolIE_Field_14202P119_t>();
    ieServedCells->id          = XNAP_IE_List_of_served_cells_NR;
    ieServedCells->criticality = ASN_XNAP_Criticality_reject;
    ieServedCells->value.present = ASN_XNAP_ProtocolIE_Field_14202P119__value_PR_ServedCells_NR;
    ieServedCells->value.choice.ServedCells_NR = *servedCells;
    free(servedCells);   // contents moved into the IE

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
    succMsg->value.present = ASN_XNAP_SuccessfulOutcome__value_PR_XnSetupResponse;
    succMsg->value.choice.XnSetupResponse = *xnSetupResp;
    free(xnSetupResp);   // contents moved into the message


    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present                   = ASN_XNAP_XnAP_PDU_PR_successfulOutcome;
    outerPdu->choice.successfulOutcome  = succMsg;

    // -----------------------------------------------------------------------
    // APER-encode and send via SCTP back to the requesting gNB (clientId = clientId)
    // -----------------------------------------------------------------------

    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        m_logger->err("xnSetupResponseSend: APER encoding failed for clientId=%d", clientId);
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return false;
    }

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = clientId;
    sctpMsg->stream   = NON_UE_ASSOCIATED_STREAM_ID;
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->xnSctpTask->push(std::move(sctpMsg));

    m_logger->info("XnSetupResponse sent to clientId=%d", clientId);

    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
    return true;
}

void XnTask::xnSetupResponseReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("xnSetupResponseReceive gnbId=%d", gnbId);

    auto *succMsg = pdu->choice.successfulOutcome;
    if (!succMsg || succMsg->value.present != ASN_XNAP_SuccessfulOutcome__value_PR_XnSetupResponse)
    {
        m_logger->err("xnSetupResponseReceive: not an XnSetupResponse from gnbId=%d", gnbId);
        return;
    }

    // Decoded in place with the enclosing PDU; owned by it, so not freed here.
    auto *xnResp = &succMsg->value.choice.XnSetupResponse;

    XnPeerInfo peer;
    peer.gnbId = gnbId;

    bool hasGlobalId = false;
    bool hasTaiList  = false;

    for (int i = 0; i < xnResp->protocolIEs.list.count; ++i)
    {
        auto *ie = xnResp->protocolIEs.list.array[i];
        if (!ie)
            continue;


        switch (ie->id)
        {
        case XNAP_IE_GlobalNG_RAN_Node_ID: {
            if (ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P119__value_PR_GlobalNG_RANNode_ID)
            {
                m_logger->warn("xnSetupResponseReceive: cannot decode GlobalNG-RANNode-ID from gnbId=%d", gnbId);
                break;
            }
            // Decoded in place with the PDU; borrowed, not owned.
            auto *nodeId = &ie->value.choice.GlobalNG_RANNode_ID;

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
            break;
        }

        case XNAP_IE_TAISupport_List: {
            if (ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P119__value_PR_TAISupport_List)
            {
                m_logger->warn("xnSetupResponseReceive: cannot decode TAISupport-List from gnbId=%d", gnbId);
                break;
            }
            // Decoded in place with the PDU; borrowed, not owned.
            auto *taiList = &ie->value.choice.TAISupport_List;

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
            break;
        }

        // AMF-Region-Information is optional in XnSetupResponse (per TS 38.423 Table 9.1.2.2-1)
        case XNAP_IE_AMF_Region_Information: {
            if (ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P119__value_PR_AMF_Region_Information)
            {
                m_logger->warn("xnSetupResponseReceive: cannot decode AMF-Region-Information from gnbId=%d", gnbId);
                break;
            }
            // Decoded in place with the PDU; borrowed, not owned.
            auto *amfInfo = &ie->value.choice.AMF_Region_Information;

            for (int j = 0; j < amfInfo->list.count; ++j)
            {
                auto *entry = amfInfo->list.array[j];
                if (!entry) continue;
                if (entry->amf_region_id.size > 0)
                    peer.amfRegionList.push_back(asn::GetBitStringInt<8>(entry->amf_region_id));
            }
            break;
        }

        case XNAP_IE_List_of_served_cells_NR: {
            if (ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P119__value_PR_ServedCells_NR) break;
            // Decoded in place with the PDU; borrowed, not owned.
            auto *servedCells = &ie->value.choice.ServedCells_NR;

            if (servedCells->list.count > 0 && servedCells->list.array[0])
            {
                const auto &info = servedCells->list.array[0]->served_cell_info_NR;
                peer.nrPCI = static_cast<int>(info.nrPCI);
                peer.nci = asn::GetBitStringLong<36>(info.cellID.nr_CI);
            }
            break;
        }

        default:
            break;
        }
    }


    if (!hasGlobalId || !hasTaiList)
    {
        m_logger->err("xnSetupResponseReceive: missing mandatory IE(s) "
                      "(globalId=%d taiList=%d) from gnbId=%d",
                      hasGlobalId, hasTaiList, gnbId);
        return;
    }

    // Merge the learned info into the existing peer entry (created by
    // updateXnConnections when this side initiated the connection).  Receiving
    // the XnSetupResponse completes the initiator's Xn Setup, so the peer is
    // now CONNECTED and eligible for handover signalling.
    auto *stored = m_xnPeerTable.applySetupInfo(peer);
    stored->connectionState = EXnConnectionState::CONNECTED;

    m_logger->info("XnSetupResponse from gnbId=%d: nci=%ld pci=%d tacs=%zu plmns=%zu amfRegions=%zu",
                   gnbId, stored->nci, stored->nrPCI,
                   stored->tacList.size(), stored->plmnList.size(), stored->amfRegionList.size());
}

void XnTask::xnSetupFailureSend(int clientId)
{
    m_logger->debug("xnSetupFailureSend clientId=%d", clientId);

    // Cause: mandatory IEs were absent — protocol / abstract_syntax_error_reject
    ASN_XNAP_Cause_t cause{};
    cause.present          = ASN_XNAP_Cause_PR_protocol;
    cause.choice.protocol  = ASN_XNAP_CauseProtocol_abstract_syntax_error_reject;

    auto *ieCause = asn::New<ASN_XNAP_ProtocolIE_Field_14202P120_t>();
    ieCause->id          = XNAP_IE_Cause;
    ieCause->criticality = ASN_XNAP_Criticality_ignore;
    ieCause->value.present = ASN_XNAP_ProtocolIE_Field_14202P120__value_PR_Cause;
    ieCause->value.choice.Cause = cause;


    auto *xnSetupFail = asn::New<ASN_XNAP_XnSetupFailure_t>();
    asn::SequenceAdd(xnSetupFail->protocolIEs, ieCause);

    auto *unsuccMsg = asn::New<ASN_XNAP_UnsuccessfulOutcome_t>();
    unsuccMsg->procedureCode = XN_PROC_XN_SETUP;
    unsuccMsg->criticality   = ASN_XNAP_Criticality_reject;
    unsuccMsg->value.present = ASN_XNAP_UnsuccessfulOutcome__value_PR_XnSetupFailure;
    unsuccMsg->value.choice.XnSetupFailure = *xnSetupFail;
    free(xnSetupFail);   // contents moved into the message


    auto *outerPdu = asn::New<ASN_XNAP_XnAP_PDU_t>();
    outerPdu->present                    = ASN_XNAP_XnAP_PDU_PR_unsuccessfulOutcome;
    outerPdu->choice.unsuccessfulOutcome = unsuccMsg;

    ssize_t encoded;
    uint8_t *buffer;
    if (!xnap_encode::Encode(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu, encoded, buffer))
    {
        m_logger->err("xnSetupFailureSend: APER encoding failed for clientId=%d", clientId);
        asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
        return;
    }

    auto sctpMsg = std::make_unique<NmGnbSctp>(NmGnbSctp::SEND_MESSAGE);
    sctpMsg->clientId = clientId;
    sctpMsg->stream   = NON_UE_ASSOCIATED_STREAM_ID;
    sctpMsg->buffer   = UniqueBuffer{buffer, static_cast<size_t>(encoded)};
    m_base->xnSctpTask->push(std::move(sctpMsg));

    m_logger->info("XnSetupFailure sent to clientId=%d", clientId);

    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, outerPdu);
}

void XnTask::xnSetupFailureReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu)
{
    m_logger->debug("xnSetupFailureReceive gnbId=%d", gnbId);

    // Any XnSetupFailure on this association means setup did not complete —
    // mark the peer failed regardless of whether the Cause decodes below, so
    // it can never be treated as handover-eligible.
    {
        auto *peer = m_xnPeerTable.getPeerInfo(gnbId);
        if (peer != nullptr)
            peer->connectionState = EXnConnectionState::CONNECTION_FAILED;
    }

    auto *unsuccMsg = pdu->choice.unsuccessfulOutcome;
    if (!unsuccMsg || unsuccMsg->value.present != ASN_XNAP_UnsuccessfulOutcome__value_PR_XnSetupFailure)
    {
        m_logger->err("XnSetupFailure from gnbId=%d: malformed UnsuccessfulOutcome; Xn setup rejected", gnbId);
        return;
    }

    // Decoded in place with the enclosing PDU; owned by it, so not freed here.
    auto *xnFail = &unsuccMsg->value.choice.XnSetupFailure;

    // Decode the mandatory Cause IE and log it.  Peer is NOT added to m_xnPeerTable.
    for (int i = 0; i < xnFail->protocolIEs.list.count; ++i)
    {
        auto *ie = xnFail->protocolIEs.list.array[i];
        if (!ie || ie->id != XNAP_IE_Cause ||
            ie->value.present != ASN_XNAP_ProtocolIE_Field_14202P120__value_PR_Cause)
            continue;

        // Decoded in place with the PDU; borrowed, not owned.
        auto *cause = &ie->value.choice.Cause;

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
        break;
    }

}


void XnTask::handlePeerHandoverLoss(int gnbId)
{
    std::vector<std::pair<int64_t, int64_t>> sourceLost;
    std::vector<std::pair<uint32_t, int64_t>> targetLost;

    for (const auto &[ueId_gnb_pair, pending] : m_pendingHandoversSourceByUeId)
    {
        if (pending.targetGnbId == gnbId && !pending.executionSucceeded) 
            sourceLost.push_back(ueId_gnb_pair);
    }
    for (const auto &[txId, pending] : m_pendingHandoversTargetByTxId)
    {   
        if (pending.sourceGnbId == gnbId && !pending.executionSucceeded) 
            targetLost.emplace_back(txId, pending.ueId);
    }

    for (auto ueId_gnb_pair : sourceLost)
    {
        auto it = m_pendingHandoversSourceByUeId.find(ueId_gnb_pair); 
        if (it == m_pendingHandoversSourceByUeId.end()) continue;

        auto failure = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_PREPARATION_FAILURE_RECEIVED);
        failure->ueId = ueId_gnb_pair.first; 
        failure->targetNci = it->second.targetNci; 
        failure->isCho = it->second.isCho;
        failure->reason = ASN_XNAP_Cause_PR_transport; 
        m_base->rrcTask->push(std::move(failure));
        
        removeSourcePendingHandover(ueId_gnb_pair.first, gnbId);
    }

    for (auto [txId, ueId] : targetLost)
    {
        auto cleanup = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_CANCEL_RECEIVED);
        cleanup->ueId = ueId; 
        m_base->rrcTask->push(std::move(cleanup));
        removeTargetPendingHandover(txId);
    }
}

} // namespace nr::gnb
