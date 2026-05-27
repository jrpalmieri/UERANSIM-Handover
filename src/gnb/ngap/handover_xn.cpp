//
// NGAP Handover Procedures (Xn / gNB-gNB)
//
// Implements:
//   - sendPathSwitchRequest()              → target gNB → AMF
//   - receivePathSwitchRequestAcknowledge() ← AMF → target gNB
//   - receivePathSwitchRequestFailure()     ← AMF → target gNB
//

#include "encode.hpp"
#include "task.hpp"
#include "utils.hpp"

#include <gnb/rrc/task.hpp>
#include <utils/common.hpp>

#include <asn/ngap/ASN_NGAP_NR-CGI.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequest.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestAcknowledge.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestFailure.h>
#include <asn/ngap/ASN_NGAP_ProtocolIE-Field.h>
#include <asn/ngap/ASN_NGAP_TAI.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformation.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformationNR.h>

const int HANDOVER_TIMEOUT_MS = 5000;

namespace nr::gnb
{


/**
 * @brief Target GNB sends a PathSwitchRequest to the AMF to request switching the UE's path 
 * to the new target gNB after handover completion.  Only used in Xn handover, and must NOT 
 * be sent in N2 handover as the path switch is implicitly triggered by the HandoverNotify.
 * 
 * @param ueId 
 */
void NgapTask::sendPathSwitchRequest(int64_t ueId)
{
    m_logger->info("UE[%ld] Sending PathSwitchRequest", ueId);

    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("sendPathSwitchRequest: UE context not found UE[%ld] ", ueId);
        return;
    }

    std::vector<ASN_NGAP_PathSwitchRequestIEs *> ies;

    // IE: UserLocationInformation
    {
        auto *ie = asn::New<ASN_NGAP_PathSwitchRequestIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_UserLocationInformation;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_PathSwitchRequestIEs__value_PR_UserLocationInformation;

        ie->value.choice.UserLocationInformation.present =
            ASN_NGAP_UserLocationInformation_PR_userLocationInformationNR;
        auto *nrLoc = asn::New<ASN_NGAP_UserLocationInformationNR>();

        asn::SetOctetString3(nrLoc->nR_CGI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetBitStringLong<36>(m_base->config->nci, nrLoc->nR_CGI.nRCellIdentity);
        asn::SetOctetString3(nrLoc->tAI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetOctetString3(nrLoc->tAI.tAC, octet3{m_base->config->tac});

        ie->value.choice.UserLocationInformation.choice.userLocationInformationNR = nrLoc;
        ies.push_back(ie);
    }

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_PathSwitchRequest>(ies);
    sendNgapUeAssociated(ue->ctxId, pdu);

    m_logger->info("UE[%ld] PathSwitchRequest sent to AMF", ueId);
}

/**
 * @brief Target GNB receives the PathSwitchRequestAcknowledge from the AMF to indicate that 
 * the path switch has been completed.  Only used for Xn handover.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receivePathSwitchRequestAcknowledge(int amfId, ASN_NGAP_PathSwitchRequestAcknowledge *msg)
{
    m_logger->info("PathSwitchRequestAcknowledge received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receivePathSwitchRequestAcknowledge: UE not found");
        return;
    }

    m_logger->info("UE[%ld] Path switch complete. AMF path updated.", ue->ctxId);

    // Notify RRC that path switch is acknowledged
    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK);
    w->ueId = ue->ctxId;
    m_base->rrcTask->push(std::move(w));

}

/**
 * @brief Target GNB receives the PathSwitchRequestFailure from the AMF to indicate that 
 * the path switch has failed.  Only used for Xn handover. The target gNB should keep the 
 * UE on the source cell and may choose to retry the path switch or trigger a new handover 
 * if necessary.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receivePathSwitchRequestFailure(int amfId, ASN_NGAP_PathSwitchRequestFailure *msg)
{
    m_logger->warn("PathSwitchRequestFailure received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receivePathSwitchRequestFailure: UE not found");
        return;
    }

    m_logger->warn("UE[%ld] Path switch failed.", ue->ctxId);

    // Notify RRC
    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::PATH_SWITCH_REQUEST_FAILURE);
    w->ueId = ue->ctxId;
    m_base->rrcTask->push(std::move(w));

}

} // namespace nr::gnb
