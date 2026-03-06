//
// NGAP Handover Procedures (N2 / AMF-mediated)
//
// Implements:
//   - sendHandoverRequired()           → source gNB → AMF
//   - receiveHandoverCommand()         ← AMF → source gNB (SuccessfulOutcome)
//   - receiveHandoverPreparationFailure() ← AMF → source gNB (UnsuccessfulOutcome)
//   - sendHandoverNotify()             → target gNB → AMF
//   - sendPathSwitchRequest()          → target gNB → AMF
//   - receivePathSwitchRequestAcknowledge() ← AMF → target gNB
//   - receivePathSwitchRequestFailure()     ← AMF → target gNB
//   - handleHandoverNotifyFromRrc()    (orchestrator called from task.cpp)
//

#include "encode.hpp"
#include "task.hpp"
#include "utils.hpp"

#include <gnb/rrc/task.hpp>

#include <asn/ngap/ASN_NGAP_Cause.h>
#include <asn/ngap/ASN_NGAP_GlobalGNB-ID.h>
#include <asn/ngap/ASN_NGAP_GlobalRANNodeID.h>
#include <asn/ngap/ASN_NGAP_GNB-ID.h>
#include <asn/ngap/ASN_NGAP_HandoverCommand.h>
#include <asn/ngap/ASN_NGAP_HandoverNotify.h>
#include <asn/ngap/ASN_NGAP_HandoverPreparationFailure.h>
#include <asn/ngap/ASN_NGAP_HandoverRequired.h>
#include <asn/ngap/ASN_NGAP_HandoverType.h>
#include <asn/ngap/ASN_NGAP_NGAP-PDU.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequest.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestAcknowledge.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestFailure.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceItemHORqd.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceListHORqd.h>
#include <asn/ngap/ASN_NGAP_ProtocolIE-Field.h>
#include <asn/ngap/ASN_NGAP_SourceToTarget-TransparentContainer.h>
#include <asn/ngap/ASN_NGAP_SuccessfulOutcome.h>
#include <asn/ngap/ASN_NGAP_TAI.h>
#include <asn/ngap/ASN_NGAP_TargetID.h>
#include <asn/ngap/ASN_NGAP_TargetRANNodeID.h>
#include <asn/ngap/ASN_NGAP_TargetToSource-TransparentContainer.h>
#include <asn/ngap/ASN_NGAP_UnsuccessfulOutcome.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformation.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformationNR.h>
#include <asn/ngap/ASN_NGAP_NR-CGI.h>

namespace nr::gnb
{

/* ================================================================== */
/*  Source gNB → AMF: HandoverRequired                                */
/* ================================================================== */

void NgapTask::sendHandoverRequired(int ueId, int targetPci, NgapCause cause)
{
    m_logger->info("Sending HandoverRequired for UE[%d] targetPCI=%d", ueId, targetPci);

    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("sendHandoverRequired: UE context not found for UE[%d]", ueId);
        return;
    }

    std::vector<ASN_NGAP_HandoverRequiredIEs *> ies;

    // IE: HandoverType = intra5gs
    {
        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_HandoverType;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_HandoverRequiredIEs__value_PR_HandoverType;
        ie->value.choice.HandoverType = ASN_NGAP_HandoverType_intra5gs;
        ies.push_back(ie);
    }

    // IE: Cause
    {
        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_Cause;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_HandoverRequiredIEs__value_PR_Cause;
        ngap_utils::ToCauseAsn_Ref(cause, ie->value.choice.Cause);
        ies.push_back(ie);
    }

    // IE: TargetID — use the target gNB's identity
    // For the simulator, we use the same PLMN and TAC but with the target PCI
    // encoded as the gNB-ID (since we identify cells by PCI).
    {
        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_TargetID;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_HandoverRequiredIEs__value_PR_TargetID;

        ie->value.choice.TargetID.present = ASN_NGAP_TargetID_PR_targetRANNodeID;
        auto *targetRanNode = asn::New<ASN_NGAP_TargetRANNodeID>();

        // GlobalRANNodeID → GlobalGNB-ID
        auto *globalGnbId = asn::New<ASN_NGAP_GlobalGNB_ID>();
        asn::SetOctetString3(globalGnbId->pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        globalGnbId->gNB_ID.present = ASN_NGAP_GNB_ID_PR_gNB_ID;
        // Encode target PCI as gNB-ID
        auto gnbIdLength = m_base->config->gnbIdLength;
        auto bitsToShift = 32 - gnbIdLength;
        asn::SetBitString(globalGnbId->gNB_ID.choice.gNB_ID,
                          octet4{static_cast<uint32_t>(targetPci) << bitsToShift},
                          static_cast<size_t>(gnbIdLength));

        targetRanNode->globalRANNodeID.present = ASN_NGAP_GlobalRANNodeID_PR_globalGNB_ID;
        targetRanNode->globalRANNodeID.choice.globalGNB_ID = globalGnbId;

        // selectedTAI
        asn::SetOctetString3(targetRanNode->selectedTAI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetOctetString3(targetRanNode->selectedTAI.tAC, octet3{m_base->config->tac});

        ie->value.choice.TargetID.choice.targetRANNodeID = targetRanNode;
        ies.push_back(ie);
    }

    // IE: SourceToTarget-TransparentContainer (minimal / opaque)
    // In a real implementation this would contain the RRC context from the source.
    // For the simulator, we provide a minimal placeholder.
    {
        auto *ie = asn::New<ASN_NGAP_HandoverRequiredIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_SourceToTarget_TransparentContainer;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present =
            ASN_NGAP_HandoverRequiredIEs__value_PR_SourceToTarget_TransparentContainer;
        // Provide a minimal octet string as placeholder
        OctetString placeholder(std::vector<uint8_t>{0x00});
        asn::SetOctetString(ie->value.choice.SourceToTarget_TransparentContainer,
                            placeholder);
        ies.push_back(ie);
    }

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_HandoverRequired>(ies);
    sendNgapUeAssociated(ue->ctxId, pdu);

    m_logger->info("HandoverRequired sent to AMF for UE[%d]", ueId);
}

/* ================================================================== */
/*  AMF → Source gNB: HandoverCommand (SuccessfulOutcome)             */
/* ================================================================== */

void NgapTask::receiveHandoverCommand(int amfId, ASN_NGAP_HandoverCommand *msg)
{
    m_logger->info("HandoverCommand received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receiveHandoverCommand: UE not found");
        return;
    }

    // Extract TargetToSource-TransparentContainer (contains the RRC container)
    auto *ieContainer = asn::ngap::GetProtocolIe(msg,
        ASN_NGAP_ProtocolIE_ID_id_TargetToSource_TransparentContainer);

    OctetString rrcContainer{};
    if (ieContainer)
    {
        rrcContainer = asn::GetOctetString(ieContainer->TargetToSource_TransparentContainer);
    }

    // Forward to RRC task
    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::HANDOVER_COMMAND_DELIVERY);
    w->ueId = ue->ctxId;
    w->rrcContainer = std::move(rrcContainer);
    m_base->rrcTask->push(std::move(w));

    m_logger->info("HandoverCommand forwarded to RRC for UE[%d]", ue->ctxId);
}

/* ================================================================== */
/*  AMF → Source gNB: HandoverPreparationFailure (UnsuccessfulOutcome)*/
/* ================================================================== */

void NgapTask::receiveHandoverPreparationFailure(int amfId, ASN_NGAP_HandoverPreparationFailure *msg)
{
    m_logger->warn("HandoverPreparationFailure received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receiveHandoverPreparationFailure: UE not found");
        return;
    }

    // Extract cause for logging
    auto *ieCause = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_Cause);
    if (ieCause)
    {
        m_logger->warn("Handover preparation failed for UE[%d], cause present=%d",
                       ue->ctxId, ieCause->Cause.present);
    }

    // Notify RRC that handover failed so it can reset the pending state
    // (We reuse PATH_SWITCH_REQUEST_ACK with a special signal, or simply
    // let RRC time out. For now, just log.)
    m_logger->warn("Handover preparation failed for UE[%d]. UE remains on source cell.", ue->ctxId);
}

/* ================================================================== */
/*  Target gNB → AMF: HandoverNotify                                  */
/* ================================================================== */

void NgapTask::sendHandoverNotify(int ueId)
{
    m_logger->info("Sending HandoverNotify for UE[%d]", ueId);

    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("sendHandoverNotify: UE context not found for UE[%d]", ueId);
        return;
    }

    std::vector<ASN_NGAP_HandoverNotifyIEs *> ies;

    // IE: UserLocationInformation (required)
    {
        auto *ie = asn::New<ASN_NGAP_HandoverNotifyIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_UserLocationInformation;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_HandoverNotifyIEs__value_PR_UserLocationInformation;

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

    m_logger->info("HandoverNotify sent to AMF for UE[%d]", ueId);
}

/* ================================================================== */
/*  Target gNB → AMF: PathSwitchRequest                               */
/* ================================================================== */

void NgapTask::sendPathSwitchRequest(int ueId)
{
    m_logger->info("Sending PathSwitchRequest for UE[%d]", ueId);

    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("sendPathSwitchRequest: UE context not found for UE[%d]", ueId);
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

    m_logger->info("PathSwitchRequest sent to AMF for UE[%d]", ueId);
}

/* ================================================================== */
/*  AMF → Target gNB: PathSwitchRequestAcknowledge                    */
/* ================================================================== */

void NgapTask::receivePathSwitchRequestAcknowledge(int amfId, ASN_NGAP_PathSwitchRequestAcknowledge *msg)
{
    m_logger->info("PathSwitchRequestAcknowledge received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receivePathSwitchRequestAcknowledge: UE not found");
        return;
    }

    m_logger->info("Path switch complete for UE[%d]. AMF path updated.", ue->ctxId);

    // Notify RRC that path switch is acknowledged
    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK);
    w->ueId = ue->ctxId;
    m_base->rrcTask->push(std::move(w));
}

/* ================================================================== */
/*  AMF → Target gNB: PathSwitchRequestFailure                        */
/* ================================================================== */

void NgapTask::receivePathSwitchRequestFailure(int amfId, ASN_NGAP_PathSwitchRequestFailure *msg)
{
    m_logger->warn("PathSwitchRequestFailure received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receivePathSwitchRequestFailure: UE not found");
        return;
    }

    m_logger->warn("Path switch failed for UE[%d].", ue->ctxId);
}

/* ================================================================== */
/*  RRC → NGAP: Handle HANDOVER_NOTIFY from RRC task                  */
/* ================================================================== */

void NgapTask::handleHandoverNotifyFromRrc(int ueId)
{
    m_logger->info("Processing handover complete for UE[%d], sending HandoverNotify + PathSwitchRequest", ueId);

    // Step 1: Send HandoverNotify to AMF
    sendHandoverNotify(ueId);

    // Step 2: Send PathSwitchRequest to update the AMF's downlink path
    sendPathSwitchRequest(ueId);
}

} // namespace nr::gnb
