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

#include <set>
#include <stdexcept>

#include <gnb/gtp/task.hpp>
#include <gnb/rrc/task.hpp>

#include <asn/ngap/ASN_NGAP_AssociatedQosFlowItem.h>
#include <asn/ngap/ASN_NGAP_AssociatedQosFlowList.h>
#include <asn/ngap/ASN_NGAP_GTPTunnel.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceFailedToSetupItemSURes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseCommand.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseResponse.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseResponseTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleasedItemRelRes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemSUReq.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemSURes.h>
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

namespace nr::gnb
{

// Sets up PDU session resources for a UE based on the received setup request.
//  Note: used after UE Context setup to add sessions to the UE's context.  The intial context setup may have
//  already created PDU sessions, which case this message may not be received by the gNB.
void NgapTask::receiveSessionResourceSetupRequest(int amfId, ASN_NGAP_PDUSessionResourceSetupRequest *msg)
{
    // find the UE Context based on the AMF UE NGAP ID and RAN UE NGAP ID in the message
    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (ue == nullptr) {
        m_logger->err("Received PDU session resource setup request, but UE not found for AMF ID %d and RAN ID %d.  Aborting PDU session resource setup request processing.", amfId, ngap_utils::FindNgapIdPair(msg).ranUeNgapId);
        return;
    }

    std::vector<ASN_NGAP_PDUSessionResourceSetupItemSURes *> successList;
    std::vector<ASN_NGAP_PDUSessionResourceFailedToSetupItemSURes *> failedList;


    m_logger->info("UE[%ld]: Received PDU session resource setup request with %d items", ue->ctxId, 
        msg->protocolIEs.list.count);

    // Create a list to store the PDU session resources for RRC message
    auto sessionList = std::make_unique<std::vector<PduSessionResource>>();

    auto *ieList = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListSUReq);
    if (ieList)
    {
        m_logger->debug("UE[%ld]: Processing IE PDU session resource setup list", ue->ctxId, 
            ieList->PDUSessionResourceSetupListSUReq.list.count);

        auto &list = ieList->PDUSessionResourceSetupListSUReq.list;

        for (int i = 0; i < list.count; i++)
        {
            auto &item = list.array[i];
            auto *transfer = ngap_encode::Decode<ASN_NGAP_PDUSessionResourceSetupRequestTransfer>(
                asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, item->pDUSessionResourceSetupRequestTransfer);
            if (transfer == nullptr)
            {
                m_logger->err(
                    "UE[%ld]: Unable to decode a PDU session resource setup request transfer. Ignoring the relevant item", ue->ctxId);
                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, transfer);
                continue;
            }

            PduSessionResource resource{ue->ctxId, static_cast<int>(item->pDUSessionID)};
            resource.sNssai = ngap_utils::SnssaiFromAsn(item->s_NSSAI);
            makeNgapPduSessionItems(&resource, transfer);

            // Instruct GTP to setup the UP tunnel
            m_logger->debug("UE[%ld]: Processing PDU session resource setup request item with PSI=%d", ue->ctxId, resource.psi);
            auto error = setupPduSessionResource(ue, resource);

            // GTP failure
            if (error.has_value())
            {
                auto *tr = asn::New<ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer>();
                ngap_utils::ToCauseAsn_Ref(error.value(), tr->cause);

                OctetString encodedTr =
                    ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer, tr);

                if (encodedTr.length() == 0)
                    throw std::runtime_error("PDUSessionResourceSetupUnsuccessfulTransfer encoding failed");

                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer, tr);

                auto *res = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemSURes>();
                res->pDUSessionID = resource.psi;
                asn::SetOctetString(res->pDUSessionResourceSetupUnsuccessfulTransfer, encodedTr);

                failedList.push_back(res);
            }
            // GTP success
            else
            {
                m_logger->debug("UE[%ld]: PDU session resource setup successful with PSI=%d", ue->ctxId, resource.psi);
                if (item->pDUSessionNAS_PDU)
                    deliverDownlinkNas(ue->ctxId, asn::GetOctetString(*item->pDUSessionNAS_PDU));

                auto *tr = asn::New<ASN_NGAP_PDUSessionResourceSetupResponseTransfer>();

                for (const auto &flow : resource.qosFlows)
                {
                    auto *associatedQosFlowItem = asn::New<ASN_NGAP_AssociatedQosFlowItem>();
                    associatedQosFlowItem->qosFlowIdentifier = flow.qfi;
                    asn::SequenceAdd(tr->dLQosFlowPerTNLInformation.associatedQosFlowList, associatedQosFlowItem);
                }

                auto &upInfo = tr->dLQosFlowPerTNLInformation.uPTransportLayerInformation;
                upInfo.present = ASN_NGAP_UPTransportLayerInformation_PR_gTPTunnel;
                upInfo.choice.gTPTunnel = asn::New<ASN_NGAP_GTPTunnel>();
                asn::SetBitString(upInfo.choice.gTPTunnel->transportLayerAddress, resource.downTunnel.address);
                asn::SetOctetString4(upInfo.choice.gTPTunnel->gTP_TEID, (octet4)resource.downTunnel.teid);

                OctetString encodedTr =
                    ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PDUSessionResourceSetupResponseTransfer, tr);

                if (encodedTr.length() == 0)
                    throw std::runtime_error("PDUSessionResourceSetupResponseTransfer encoding failed");

                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupResponseTransfer, tr);

                auto *res = asn::New<ASN_NGAP_PDUSessionResourceSetupItemSURes>();
                res->pDUSessionID = resource.psi;
                asn::SetOctetString(res->pDUSessionResourceSetupResponseTransfer, encodedTr);

                successList.push_back(res);

                // Hand the session on to RRC. This is the last use of the resource, so it
                // is moved rather than copied.
                sessionList->emplace_back(std::move(resource));
            }

            asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, transfer);
        }
    }

    // Get the NAS msg if included
    auto *ieNasPdu = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_NAS_PDU);
    if (ieNasPdu)
    {
        m_logger->debug("UE[%ld]: Processing IE NAS PDU", ue->ctxId);

        if (sessionList->empty())
        {
            deliverDownlinkNas(ue->ctxId, asn::GetOctetString(ieNasPdu->NAS_PDU));
            m_logger->debug("UE[%ld]: PDU Session Setup Request - NAS msg only sent to RRC (no PDU sessions to establish)", ue->ctxId);
        }
        else
        {
            deliverDownlinkNasAccept(ue->ctxId, asn::GetOctetString(ieNasPdu->NAS_PDU), std::move(sessionList));
            m_logger->debug("UE[%ld]: PDU Session Setup Request - NAS msg and PDU Session List [count=%d] sent to RRC", ue->ctxId, sessionList ? sessionList->size() : 0);
        }
    }
    else
    {
        m_logger->debug("UE[%ld]: PDU Session Setup Request - No NAS PDU included in the request", ue->ctxId);
        deliverPDUSessionSetupRequest(ue->ctxId, std::move(sessionList));
    }

    // Send response to AMF

    std::vector<ASN_NGAP_ProtocolIE_Field_13561P74 *> responseIes;

    if (!successList.empty())
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P74>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListSURes;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P74__value_PR_PDUSessionResourceSetupListSURes;

        for (auto &item : successList)
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceSetupListSURes, item);

        responseIes.push_back(ie);
    }

    if (!failedList.empty())
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P74>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceFailedToSetupListSURes;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present =
            ASN_NGAP_ProtocolIE_Field_13561P74__value_PR_PDUSessionResourceFailedToSetupListSURes;

        for (auto &item : failedList)
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceFailedToSetupListSURes, item);

        responseIes.push_back(ie);
    }

    auto *respPdu = asn::ngap::NewMessagePdu<ASN_NGAP_PDUSessionResourceSetupResponse>(responseIes);
    sendNgapUeAssociated(ue->ctxId, respPdu);

    if (failedList.empty())
        m_logger->info("UE[%ld] PDU session resource(s) setup count[%d]", ue->ctxId,
                       static_cast<int>(successList.size()));
    else if (successList.empty())
        m_logger->err("UE[%ld] PDU session resource(s) setup failed count[%d]", ue->ctxId,
                      static_cast<int>(failedList.size()));
    else
        m_logger->err("UE[%ld] PDU session establishment partially successful success[%d] failed[%d]",
                      ue->ctxId,
                      static_cast<int>(successList.size()), static_cast<int>(failedList.size()));
}

/**
 * @brief Assigns the downlink tunnel endpoint to the given PDU session resource and instructs
 * GTP to setup the user plane tunnel. Returns an optional NGAP cause in case of failure.
 *
 * The caller keeps ownership of the resource, which is updated in place with the assigned
 * downlink address and TEID so that the caller can encode them into its NGAP response. The
 * GTP task receives an independent copy, because it runs on another thread and retains the
 * session after this request completes.
 *
 * @param ue the NGAP context of the UE that owns the session
 * @param resource the session resource to set up, updated in place with the downlink tunnel
 * @return std::optional<NgapCause> the failure cause, or an empty optional on success
 */
std::optional<NgapCause> NgapTask::setupPduSessionResource(NgapUeContext *ue, PduSessionResource &resource)
{
    if (resource.sessionType != PduSessionType::IPv4)
    {
        m_logger->err("UE[%ld]: PDU session resource could not setup: Only IPv4 is supported", ue->ctxId);
        return NgapCause::RadioNetwork_unspecified;
    }

    if (resource.upTunnel.address.length() == 0)
    {
        m_logger->err("UE[%ld]: PDU session resource could not setup: Uplink TNL information is missing", ue->ctxId);
        return NgapCause::Protocol_transfer_syntax_error;
    }

    if (resource.qosFlows.empty())
    {
        m_logger->err("UE[%ld]: PDU session resource could not setup: QoS flow list is null or empty", ue->ctxId);
        return NgapCause::Protocol_semantic_error;
    }

    std::string gtpIp = m_base->config->gtpAdvertiseIp.value_or(m_base->config->gtpIp);

    resource.downTunnel.address = utils::IpToOctetString(gtpIp);
    resource.downTunnel.teid = ++m_downlinkTeidCounter;

    // This is the only deep copy of the resource in the session setup path. Everything the
    // caller does afterwards reads or moves its own object, so the two threads never share one.
    auto w = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::SESSION_CREATE);
    w->resource = std::make_unique<PduSessionResource>(resource);
    m_base->gtpTask->push(std::move(w));

    // Add the PDUSessionID to the UE's session list
    ue->pduSessions.insert(resource.psi);
    m_logger->debug("UE[%ld]: PDU session resource setup, added to session list with PSI=%d",
        ue->ctxId, resource.psi);

    return {};
}

void NgapTask::receiveSessionResourceReleaseCommand(int amfId, ASN_NGAP_PDUSessionResourceReleaseCommand *msg)
{
    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (ue == nullptr)
        return;

    std::set<int> psIds{};

    auto *ieReq = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceToReleaseListRelCmd);
    if (ieReq)
    {
        auto &list = ieReq->PDUSessionResourceToReleaseListRelCmd.list;

        for (int i = 0; i < list.count; i++)
        {
            auto &item = list.array[i];
            if (item)
                psIds.insert(static_cast<int>(item->pDUSessionID));
        }
    }

    ieReq = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_NAS_PDU);
    if (ieReq)
        deliverDownlinkNas(ue->ctxId, asn::GetOctetString(ieReq->NAS_PDU));

    auto *ieResp = asn::New<ASN_NGAP_ProtocolIE_Field_13561P76>();
    ieResp->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceReleasedListRelRes;
    ieResp->criticality = ASN_NGAP_Criticality_ignore;
    ieResp->value.present =
        ASN_NGAP_ProtocolIE_Field_13561P76__value_PR_PDUSessionResourceReleasedListRelRes;

    // Perform release
    for (auto &psi : psIds)
    {
        auto w = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::SESSION_RELEASE);
        w->ueId = ue->ctxId;
        w->psi = psi;
        m_base->gtpTask->push(std::move(w));

        ue->pduSessions.erase(psi);

    }

    // Have RRC tear down the radio bearers (RLS + UE) for the released sessions.
    if (!psIds.empty())
    {
        auto rm = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::PDU_SESSION_UPDATE);
        rm->ueId = ue->ctxId;
        rm->releasedPsis.assign(psIds.begin(), psIds.end());
        m_base->rrcTask->push(std::move(rm));
    }

    for (auto &psi : psIds)
    {
        auto *tr = asn::New<ASN_NGAP_PDUSessionResourceReleaseResponseTransfer>();

        OctetString encodedTr = ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PDUSessionResourceReleaseResponseTransfer, tr);

        if (encodedTr.length() == 0)
            throw std::runtime_error("PDUSessionResourceReleaseResponseTransfer encoding failed");

        asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceReleaseResponseTransfer, tr);

        auto *item = asn::New<ASN_NGAP_PDUSessionResourceReleasedItemRelRes>();
        item->pDUSessionID = static_cast<ASN_NGAP_PDUSessionID_t>(psi);
        asn::SetOctetString(item->pDUSessionResourceReleaseResponseTransfer, encodedTr);

        asn::SequenceAdd(ieResp->value.choice.PDUSessionResourceReleasedListRelRes, item);
    }

    auto *respPdu = asn::ngap::NewMessagePdu<ASN_NGAP_PDUSessionResourceReleaseResponse>({ieResp});
    sendNgapUeAssociated(ue->ctxId, respPdu);

    m_logger->info("UE[%ld] PDU session resource(s) released, count[%d]", ue->ctxId, static_cast<int>(psIds.size()));
}

} // namespace nr::gnb