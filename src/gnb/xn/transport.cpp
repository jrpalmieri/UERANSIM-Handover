#include "task.hpp"
#include "encode.hpp"

#include <lib/asn/utils.hpp>

extern "C"
{
#include <ASN_XNAP_XnAP-PDU.h>
}

namespace nr::gnb
{

// Procedure codes from 3GPP XnAP-CommonDataTypes (xnap-rel18-v18_8.asn1)
static constexpr long XN_PROC_HANDOVER_PREPARATION    = 0;
static constexpr long XN_PROC_SN_STATUS_TRANSFER       = 1;
static constexpr long XN_PROC_HANDOVER_CANCEL          = 2;
static constexpr long XN_PROC_UE_CONTEXT_RELEASE       = 6;
static constexpr long XN_PROC_XN_SETUP                 = 17;
static constexpr long XN_PROC_HANDOVER_SUCCESS         = 29;
static constexpr long XN_PROC_CONDITIONAL_HO_CANCEL    = 30;

// InitiatingMessage, SuccessfulOutcome, and UnsuccessfulOutcome all begin with
// `long procedureCode` as their first field, so we can read it via this cast
// without pulling in the full xnap subtype headers (which conflict with asn1c headers).
static inline long xnProcCode(const void *msg)
{
    return *reinterpret_cast<const long *>(msg);
}

// ---------------------------------------------------------------------------
// SCTP receive: decode outer XnAP-PDU and dispatch by procedure code
// ---------------------------------------------------------------------------

void XnTask::xnHandleSctpMessage(int gnbId, uint16_t stream, const UniqueBuffer &buffer)
{
    auto *pdu = xnap_encode::Decode<ASN_XNAP_XnAP_PDU_t>(
        asn_DEF_ASN_XNAP_XnAP_PDU, buffer.data(), buffer.size());

    if (pdu == nullptr)
    {
        m_logger->err("XnAP APER decoding failed for SCTP message from gnbId=%d", gnbId);
        return;
    }

    if (pdu->present == ASN_XNAP_XnAP_PDU_PR_initiatingMessage)
    {
        if (!pdu->choice.initiatingMessage)
        {
            m_logger->err("XnAP initiatingMessage is null from gnbId=%d", gnbId);
            asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, pdu);
            return;
        }

        switch (xnProcCode(pdu->choice.initiatingMessage))
        {
        case XN_PROC_HANDOVER_PREPARATION:
            receiveHandoverRequest(gnbId, pdu);
            break;
        case XN_PROC_SN_STATUS_TRANSFER:
            receiveSnStatusTransfer(gnbId, pdu);
            break;
        case XN_PROC_HANDOVER_CANCEL:
            xnHandoverCancelTarget(gnbId, pdu);
            break;
        case XN_PROC_UE_CONTEXT_RELEASE:
            receiveUeContextRelease(gnbId, pdu);
            break;
        case XN_PROC_XN_SETUP:
            xnSetupRequestReceive(gnbId, pdu);
            break;
        case XN_PROC_HANDOVER_SUCCESS:
            receiveHandoverSuccess(gnbId, pdu);
            break;
        case XN_PROC_CONDITIONAL_HO_CANCEL:
            xnHandoverCancelTarget(gnbId, pdu);
            break;
        default:
            m_logger->warn("Unhandled XnAP initiating procedureCode=%ld from gnbId=%d",
                           xnProcCode(pdu->choice.initiatingMessage), gnbId);
            break;
        }
    }
    else if (pdu->present == ASN_XNAP_XnAP_PDU_PR_successfulOutcome)
    {
        if (!pdu->choice.successfulOutcome)
        {
            m_logger->err("XnAP successfulOutcome is null from gnbId=%d", gnbId);
            asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, pdu);
            return;
        }

        switch (xnProcCode(pdu->choice.successfulOutcome))
        {
        case XN_PROC_HANDOVER_PREPARATION:
            receiveHandoverRequestAck(gnbId, pdu);
            break;
        case XN_PROC_XN_SETUP:
            xnSetupResponseReceive(gnbId, pdu);
            break;
        default:
            m_logger->warn("Unhandled XnAP successful outcome procedureCode=%ld from gnbId=%d",
                           xnProcCode(pdu->choice.successfulOutcome), gnbId);
            break;
        }
    }
    else if (pdu->present == ASN_XNAP_XnAP_PDU_PR_unsuccessfulOutcome)
    {
        if (!pdu->choice.unsuccessfulOutcome)
        {
            m_logger->err("XnAP unsuccessfulOutcome is null from gnbId=%d", gnbId);
            asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, pdu);
            return;
        }

        switch (xnProcCode(pdu->choice.unsuccessfulOutcome))
        {
        case XN_PROC_HANDOVER_PREPARATION:
            receiveHandoverPreparationFailure(gnbId, pdu);
            break;
        case XN_PROC_XN_SETUP:
            xnSetupFailureReceive(gnbId, pdu);
            break;
        default:
            m_logger->warn("Unhandled XnAP unsuccessful outcome procedureCode=%ld from gnbId=%d",
                           xnProcCode(pdu->choice.unsuccessfulOutcome), gnbId);
            break;
        }
    }
    else
    {
        m_logger->err("XnAP PDU has unknown present type from gnbId=%d", gnbId);
    }

    asn::Free(asn_DEF_ASN_XNAP_XnAP_PDU, pdu);
}


} // namespace nr::gnb