//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "encode.hpp"
#include "task.hpp"
#include "utils.hpp"

#include <gnb/gtp/task.hpp>
#include <gnb/rrc/task.hpp>

#include <asn/ngap/ASN_NGAP_AllowedNSSAI-Item.h>
#include <asn/ngap/ASN_NGAP_AMF-UE-NGAP-ID.h>
#include <asn/ngap/ASN_NGAP_AssociatedQosFlowItem.h>
#include <asn/ngap/ASN_NGAP_AssociatedQosFlowList.h>
#include <asn/ngap/ASN_NGAP_GTPTunnel.h>
#include <asn/ngap/ASN_NGAP_InitialContextSetupRequest.h>
#include <asn/ngap/ASN_NGAP_InitialContextSetupResponse.h>
#include <asn/ngap/ASN_NGAP_NGAP-PDU.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceFailedToSetupItemCxtRes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceFailedToSetupItemSURes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceItemCxtRelReq.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseCommand.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseResponse.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseResponseTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleasedItemRelRes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemCxtReq.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemCxtRes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemSUReq.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemSURes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupListCxtReq.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupRequest.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupRequestTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupResponse.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupResponseTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceToReleaseItemRelCmd.h>
#include <asn/ngap/ASN_NGAP_ProtocolIE-Field.h>
#include <asn/ngap/ASN_NGAP_QosFlowPerTNLInformationItem.h>
#include <asn/ngap/ASN_NGAP_QosFlowPerTNLInformationList.h>
#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestItem.h>
#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestList.h>
#include <asn/ngap/ASN_NGAP_SuccessfulOutcome.h>
#include <asn/ngap/ASN_NGAP_UE-NGAP-ID-pair.h>
#include <asn/ngap/ASN_NGAP_UE-NGAP-IDs.h>
#include <asn/ngap/ASN_NGAP_UEAggregateMaximumBitRate.h>
#include <asn/ngap/ASN_NGAP_UEContextModificationRequest.h>
#include <asn/ngap/ASN_NGAP_UEContextModificationResponse.h>
#include <asn/ngap/ASN_NGAP_UEContextReleaseCommand.h>
#include <asn/ngap/ASN_NGAP_UEContextReleaseComplete.h>
#include <asn/ngap/ASN_NGAP_UEContextReleaseRequest.h>

namespace nr::gnb
{

/**
 * @brief Handles the AMF's Initial Context Setup Request message. This message is sent by the AMF in response to an INITIAL UE message.
 * This msg means the UE has been authenticated and the AMF is requesting the setup of the user plane session contexts for the UE.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receiveInitialContextSetup(int amfId, ASN_NGAP_InitialContextSetupRequest *msg)
{
    m_logger->debug("Initial Context Setup Request received from AMF[%d]", amfId);

    // the AMF msg must contain the NGAP IDs of the UE, which we use to find the relevant UE NGAP context
    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (ue == nullptr) 
    {
        m_logger->err("receiveInitialContextSetup: UE not found");
        return;
    }

    m_logger->debug("UE[%ld] Initial Context Setup Request mapped to UE", ue->ctxId);

    // User plane - Initial context setup
    {
        auto w = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_UPDATE);
        w->update = std::make_unique<GtpUeContextUpdate>(true, ue->ctxId, ue->ueAmbr);
        w->ueId = ue->ctxId;
        m_logger->debug("UE[%ld] Initial Context Setup: sending UE_CONTEXT_UPDATE to GTP with AMBR UL=%lu bps, DL=%lu bps", 
            w->update->ueId, w->update->ueAmbr.ulAmbr, w->update->ueAmbr.dlAmbr);
        m_base->gtpTask->push(std::move(w));
    }

    // Extract Security Information (Capabilities, Key)
    {
        auto *secIe = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_UESecurityCapabilities);
        if (secIe)
        {
            // These are al 16-bit bit strings, convert them to uint16_t
            ue->ueSecInfo.nRencryptionAlgorithmsBitmap = asn::GetOctetString(secIe->UESecurityCapabilities.nRencryptionAlgorithms).get4UI(0);
            ue->ueSecInfo.eUTRAencryptionAlgorithmsBitmap = asn::GetOctetString(secIe->UESecurityCapabilities.eUTRAencryptionAlgorithms).get4UI(0);
            ue->ueSecInfo.nRintegrityProtectionAlgorithmsBitmap = asn::GetOctetString(secIe->UESecurityCapabilities.nRintegrityProtectionAlgorithms).get4UI(0);
            ue->ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap = asn::GetOctetString(secIe->UESecurityCapabilities.eUTRAintegrityProtectionAlgorithms).get4UI(0);

            m_logger->debug("UE[%ld] Initial Context Setup: sending SECURITY_INFO to RRC - nRencryptionAlgorithms=0x%02x, eUTRAencryptionAlgorithms=0x%02x, nRintegrityProtectionAlgorithms=0x%02x, eUTRAintegrityProtectionAlgorithms=0x%02x",
                ue->ctxId, ue->ueSecInfo.nRencryptionAlgorithmsBitmap, ue->ueSecInfo.eUTRAencryptionAlgorithmsBitmap,
                ue->ueSecInfo.nRintegrityProtectionAlgorithmsBitmap, ue->ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap);
        }
    }

    {
        auto *secIe = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_SecurityKey);
        if (secIe)
        {
            auto sk = asn::GetOctetString(secIe->SecurityKey);
            std::copy(sk.data(), sk.data() + sk.length(), ue->ueSecInfo.k_gnb.begin());
            m_logger->debug("UE[%ld] Initial Context Setup: Security Key received, length=%d bytes",
                ue->ctxId, ue->ueSecInfo.k_gnb.size());
        }
    }

    // Send to RRC now (so it can send Security Mode Command to UE)
    {
        auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::SECURITY_INFO);
        w->ueId = ue->ctxId;
        w->ueSecInfo = std::make_unique<UeSecurityInfo>(ue->ueSecInfo);
        m_base->rrcTask->push(std::move(w));
    }

    // Extract Allowed NSSAIs
    auto *nssaiIe = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_AllowedNSSAI);
    if (nssaiIe)    {
        auto &list = nssaiIe->AllowedNSSAI.list;
        for (int i = 0; i < list.count; i++)
        {            auto &item = list.array[i];
            
            SingleSlice slice{};
            slice.sst = item->s_NSSAI.sST.buf[0];
            slice.sd = (item->s_NSSAI.sD && item->s_NSSAI.sD->size > 0) ? std::optional<octet3>{asn::GetOctet3(*item->s_NSSAI.sD)} : std::nullopt;
            ue->allowedNssais.emplace_back(slice);

            m_logger->debug("UE[%ld] Initial Context Setup: Allowed NSSAI received - SST=%d, SD=%s",
                ue->ctxId, slice.sst, slice.sd ? std::to_string(static_cast<uint32_t>(*slice.sd)).c_str() : "None");
        }
    }

    // Extract User Plane information.
    auto *reqIe = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_UEAggregateMaximumBitRate);
    if (reqIe)
    {
        ue->ueAmbr.dlAmbr = asn::GetUnsigned64(reqIe->UEAggregateMaximumBitRate.uEAggregateMaximumBitRateDL) / 8ull;
        ue->ueAmbr.ulAmbr = asn::GetUnsigned64(reqIe->UEAggregateMaximumBitRate.uEAggregateMaximumBitRateUL) / 8ull;
    }


    // Extract PDU Session Resource Setup List
    //  and send to GTP and RRC to setup the PDU sessions and tunnels

    std::vector<ASN_NGAP_PDUSessionResourceSetupItemCxtRes *> successList;
    std::vector<ASN_NGAP_PDUSessionResourceFailedToSetupItemCxtRes *> failedList;

    // Create a list to store the PDU session resources for RRC message
    auto sessionList = std::make_unique<std::vector<PduSessionResource>>();

    reqIe = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListCxtReq);
    if (reqIe)
    {
        auto &list = reqIe->PDUSessionResourceSetupListCxtReq.list;
        for (int i = 0; i < list.count; i++)
        {
            auto &item = list.array[i];
            auto *transfer = ngap_encode::Decode<ASN_NGAP_PDUSessionResourceSetupRequestTransfer>(
                asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, item->pDUSessionResourceSetupRequestTransfer);
            if (transfer == nullptr)
            {
                m_logger->err(
                    "Unable to decode a PDU session resource setup request transfer. Ignoring the relevant item");
                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, transfer);
                continue;
            }

            auto *resource = new PduSessionResource(ue->ctxId, static_cast<int>(item->pDUSessionID));

            auto *ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_PDUSessionAggregateMaximumBitRate);
            if (ie)
            {
                resource->sessionAmbr.dlAmbr =
                    asn::GetUnsigned64(ie->PDUSessionAggregateMaximumBitRate.pDUSessionAggregateMaximumBitRateDL) /
                    8ull;
                resource->sessionAmbr.ulAmbr =
                    asn::GetUnsigned64(ie->PDUSessionAggregateMaximumBitRate.pDUSessionAggregateMaximumBitRateUL) /
                    8ull;
            }

            ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_DataForwardingNotPossible);
            if (ie)
                resource->dataForwardingNotPossible = true;

            ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_PDUSessionType);
            if (ie)
                resource->sessionType = ngap_utils::PduSessionTypeFromAsn(ie->PDUSessionType);

            ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_UL_NGU_UP_TNLInformation);
            if (ie)
            {
                resource->upTunnel.teid =
                    (uint32_t)asn::GetOctet4(ie->UPTransportLayerInformation.choice.gTPTunnel->gTP_TEID);

                resource->upTunnel.address =
                    asn::GetOctetString(ie->UPTransportLayerInformation.choice.gTPTunnel->transportLayerAddress);
            }

            ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_QosFlowSetupRequestList);
            if (ie)
            {
                auto *ptr = asn::New<ASN_NGAP_QosFlowSetupRequestList>();
                asn::DeepCopy(asn_DEF_ASN_NGAP_QosFlowSetupRequestList, ie->QosFlowSetupRequestList, ptr);

                resource->qosFlows = asn::WrapUnique(ptr, asn_DEF_ASN_NGAP_QosFlowSetupRequestList);
            }

            // Instructs GTP to setup the UP Tunnel
            m_logger->debug("UE[%ld] PDU Session Resource Setup sent to GTP psi=%d", ue->ctxId, resource->psi);
            auto error = setupPduSessionResource(ue, resource);

            if (error.has_value())
            {
                // Create the unsuccessful response transfer
                auto *tr = asn::New<ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer>();
                ngap_utils::ToCauseAsn_Ref(error.value(), tr->cause);

                OctetString encodedTr =
                    ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer, tr);

                if (encodedTr.length() == 0)
                    throw std::runtime_error("PDUSessionResourceSetupUnsuccessfulTransfer encoding failed");

                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer, tr);

                auto *res = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemCxtRes>();
                res->pDUSessionID = resource->psi;
                asn::SetOctetString(res->pDUSessionResourceSetupUnsuccessfulTransfer, encodedTr);

                failedList.push_back(res);
            }
            else
            {

                // add to the session list to be sent to RRC
                sessionList->emplace_back(*resource);

                // Create the response transfer for the successful PDU session setup
                auto *tr = asn::New<ASN_NGAP_PDUSessionResourceSetupResponseTransfer>();

                auto &qosList = resource->qosFlows->list;
                for (int iQos = 0; iQos < qosList.count; iQos++)
                {
                    auto *associatedQosFlowItem = asn::New<ASN_NGAP_AssociatedQosFlowItem>();
                    associatedQosFlowItem->qosFlowIdentifier = qosList.array[iQos]->qosFlowIdentifier;
                    asn::SequenceAdd(tr->dLQosFlowPerTNLInformation.associatedQosFlowList, associatedQosFlowItem);
                }

                auto &upInfo = tr->dLQosFlowPerTNLInformation.uPTransportLayerInformation;
                upInfo.present = ASN_NGAP_UPTransportLayerInformation_PR_gTPTunnel;
                upInfo.choice.gTPTunnel = asn::New<ASN_NGAP_GTPTunnel>();
                asn::SetBitString(upInfo.choice.gTPTunnel->transportLayerAddress, resource->downTunnel.address);
                asn::SetOctetString4(upInfo.choice.gTPTunnel->gTP_TEID, (octet4)resource->downTunnel.teid);

                OctetString encodedTr =
                    ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PDUSessionResourceSetupResponseTransfer, tr);

                if (encodedTr.length() == 0)
                    throw std::runtime_error("PDUSessionResourceSetupResponseTransfer encoding failed");

                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupResponseTransfer, tr);

                auto *res = asn::New<ASN_NGAP_PDUSessionResourceSetupItemCxtRes>();
                res->pDUSessionID = resource->psi;
                asn::SetOctetString(res->pDUSessionResourceSetupResponseTransfer, encodedTr);

                successList.push_back(res);
            }

            asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, transfer);
        }
    }

    // Send to RRC - NAS Registration Accept to UE, Security Context, and the list of allowed PDU sessions (PSIs)
    reqIe = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_NAS_PDU);
    if (reqIe)
    {
       
        deliverDownlinkNasAccept(ue->ctxId, asn::GetOctetString(reqIe->NAS_PDU), std::move(sessionList));

        m_logger->debug("UE[%ld]: Initial Context Setup Request - NAS Accept and PDU Session List [count=%d] sent to RRC", ue->ctxId, sessionList ? sessionList->size() : 0);
    }

    // Send Response to AMF
    
    std::vector<ASN_NGAP_InitialContextSetupResponseIEs *> responseIes;

    if (!successList.empty())
    {
        auto *ie = asn::New<ASN_NGAP_InitialContextSetupResponseIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListCxtRes;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_InitialContextSetupResponseIEs__value_PR_PDUSessionResourceSetupListCxtRes;

        for (auto &item : successList)
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceSetupListCxtRes, item);

        responseIes.push_back(ie);
    }

    if (!failedList.empty())
    {
        auto *ie = asn::New<ASN_NGAP_InitialContextSetupResponseIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceFailedToSetupListCxtRes;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_InitialContextSetupResponseIEs__value_PR_PDUSessionResourceFailedToSetupListCxtRes;

        for (auto &item : failedList)
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceFailedToSetupListCxtRes, item);

        responseIes.push_back(ie);
    }

    // Send response to AMF- InitialContextSetupResponse
    auto *response = asn::ngap::NewMessagePdu<ASN_NGAP_InitialContextSetupResponse>(responseIes);
    sendNgapUeAssociated(ue->ctxId, response);

    m_logger->debug("UE[%ld] Initial Context Setup Response sent to AMF", ue->ctxId);
}

void NgapTask::receiveContextRelease(int amfId, ASN_NGAP_UEContextReleaseCommand *msg)
{
    m_logger->debug("UE Context Release Command received from AMF[%d]", amfId);

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPairFromUeNgapIds(msg));
    if (ue == nullptr)
    {
        m_logger->err("receiveContextRelease: UE not found");
        return;
    }
    
    m_logger->debug("UE[%ld] Context Release Command mapped to UE", ue->ctxId);

    // Notify RRC task
    auto w1 = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::AN_RELEASE);
    w1->ueId = ue->ctxId;
    m_base->rrcTask->push(std::move(w1));

    // Notify GTP task
    auto w2 = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_RELEASE);
    w2->ueId = ue->ctxId;
    m_base->gtpTask->push(std::move(w2));

    auto *response = asn::ngap::NewMessagePdu<ASN_NGAP_UEContextReleaseComplete>({});
    sendNgapUeAssociated(ue->ctxId, response);

    m_logger->info("UE[%ld] NGAP Context released.", ue->ctxId);

    deleteUeContext(ue->ctxId);

}

void NgapTask::receiveContextModification(int amfId, ASN_NGAP_UEContextModificationRequest *msg)
{
    m_logger->debug("UE Context Modification Request received from AMF[%d]", amfId);

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (ue == nullptr)
        return;

    m_logger->debug("UE[%ld] Context Modification Request mapped to UE", ue->ctxId);

    auto *ie = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_UEAggregateMaximumBitRate);
    if (ie)
    {
        ue->ueAmbr.dlAmbr = asn::GetUnsigned64(ie->UEAggregateMaximumBitRate.uEAggregateMaximumBitRateDL);
        ue->ueAmbr.ulAmbr = asn::GetUnsigned64(ie->UEAggregateMaximumBitRate.uEAggregateMaximumBitRateUL);
    }

    ie = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_NewAMF_UE_NGAP_ID);
    if (ie)
    {
        int64_t old = ue->amfUeNgapId;
        ue->amfUeNgapId = asn::GetSigned64(ie->AMF_UE_NGAP_ID_1);
        m_logger->debug("UE[%ld} AMF-UE-NGAP-ID changed from %ld to %ld", ue->ctxId, old, ue->amfUeNgapId);
    }

    auto *response = asn::ngap::NewMessagePdu<ASN_NGAP_UEContextModificationResponse>({});
    sendNgapUeAssociated(ue->ctxId, response);

    auto w = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_UPDATE);
    w->update = std::make_unique<GtpUeContextUpdate>(false, ue->ctxId, ue->ueAmbr);
    m_base->gtpTask->push(std::move(w));
}

void NgapTask::sendContextRelease(int64_t ueId, NgapCause cause)
{
    m_logger->debug("UE[%ld] Sending UE Context release request to AMF (NG-RAN node initiated)", ueId);

    auto *ue = findUeContext(ueId);
    if (ue == nullptr)
        return;

    std::vector<ASN_NGAP_UEContextReleaseRequest_IEs *> ies;

    if (!ue->pduSessions.empty())
    {
        auto *ieSessionList = asn::New<ASN_NGAP_UEContextReleaseRequest_IEs>();
        ieSessionList->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceListCxtRelReq;
        ieSessionList->criticality = ASN_NGAP_Criticality_reject;
        ieSessionList->value.present = ASN_NGAP_UEContextReleaseRequest_IEs__value_PR_PDUSessionResourceListCxtRelReq;

        for (int psi : ue->pduSessions)
        {
            auto *sessionItem = asn::New<ASN_NGAP_PDUSessionResourceItemCxtRelReq>();
            sessionItem->pDUSessionID = static_cast<ASN_NGAP_PDUSessionID_t>(psi);
            asn::SequenceAdd(ieSessionList->value.choice.PDUSessionResourceListCxtRelReq, sessionItem);
        }

        ies.push_back(ieSessionList);
    }

    auto *ieCause = asn::New<ASN_NGAP_UEContextReleaseRequest_IEs>();
    ieCause->id = ASN_NGAP_ProtocolIE_ID_id_Cause;
    ieCause->criticality = ASN_NGAP_Criticality_ignore;
    ieCause->value.present = ASN_NGAP_UEContextReleaseRequest_IEs__value_PR_Cause;
    ngap_utils::ToCauseAsn_Ref(cause, ieCause->value.choice.Cause);
    ies.push_back(ieCause);

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_UEContextReleaseRequest>(ies);
    sendNgapUeAssociated(ueId, pdu);
}

} // namespace nr::gnb