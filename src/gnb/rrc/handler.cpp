//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <gnb/rls/task.hpp>
#include <gnb/ngap/task.hpp>
#include <lib/rrc/encode.hpp>

#include <asn/ngap/ASN_NGAP_FiveG-S-TMSI.h>
#include <asn/rrc/ASN_RRC_BCCH-BCH-Message.h>
#include <asn/rrc/ASN_RRC_BCCH-DL-SCH-Message.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_DL-CCCH-Message.h>
#include <asn/rrc/ASN_RRC_DL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_DLInformationTransfer-IEs.h>
#include <asn/rrc/ASN_RRC_DLInformationTransfer.h>
#include <asn/rrc/ASN_RRC_PCCH-Message.h>
#include <asn/rrc/ASN_RRC_Paging.h>
#include <asn/rrc/ASN_RRC_PagingRecord.h>
#include <asn/rrc/ASN_RRC_PagingRecordList.h>
#include <asn/rrc/ASN_RRC_RRCRelease-IEs.h>
#include <asn/rrc/ASN_RRC_RRCRelease.h>
#include <asn/rrc/ASN_RRC_RRCSetup-IEs.h>
#include <asn/rrc/ASN_RRC_RRCSetup.h>
#include <asn/rrc/ASN_RRC_RRCSetupComplete-IEs.h>
#include <asn/rrc/ASN_RRC_RRCSetupComplete.h>
#include <asn/rrc/ASN_RRC_RRCSetupRequest.h>
#include <asn/rrc/ASN_RRC_UL-CCCH-Message.h>
#include <asn/rrc/ASN_RRC_UL-CCCH1-Message.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_ULInformationTransfer-IEs.h>
#include <asn/rrc/ASN_RRC_ULInformationTransfer.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1530-IEs.h>
#include <asn/rrc/ASN_RRC_SecurityModeCommand.h>
#include <asn/rrc/ASN_RRC_SecurityModeCommand-IEs.h>
#include <asn/rrc/ASN_RRC_IntegrityProtAlgorithm.h>
#include <asn/rrc/ASN_RRC_SecurityConfigSMC.h>
#include <asn/rrc/ASN_RRC_SecurityAlgorithmConfig.h>
#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestItem.h>
#include <asn/rrc/ASN_RRC_SRB-ToAddModList.h>
#include <asn/rrc/ASN_RRC_DRB-ToAddModList.h>
#include <asn/rrc/ASN_RRC_DRB-ToAddMod.h>
#include <asn/rrc/ASN_RRC_SDAP-Config.h>
#include <asn/rrc/ASN_RRC_SRB-ToAddMod.h>
#include <asn/rrc/ASN_RRC_RadioBearerConfig.h>
#include <asn/rrc/ASN_RRC_DedicatedNAS-Message.h>



namespace nr::gnb
{

void GnbRrcTask::handleDownlinkNasAccept(int64_t ueId, const OctetString &nasPdu, std::unique_ptr<std::vector<PduSessionResource>> sessionList)
{

    // get UE context
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
        return;

    // SDAP: map each PDU session to a bearer based on the QoS Flows
    // Note: for now, we map all QoS Flows for each PSI onto a single DRB

    auto rbUpdate = std::make_unique<RadioBearerUpdate>();
    auto sdapUpdate = std::make_unique<SdapUpdate>();

    // add default SRB to the update
    RadioBearer bearer;
    bearer.bearerId = 0x0; // SRB0
    rbUpdate->upsertBearers.emplace_back(bearer);

    // do SDAP mappings
    int drbs_used = 0;
    for (const auto &session : *sessionList)
    {
        ++drbs_used;

        // add a DRB for this session
        RadioBearer bearer;
        bearer.bearerId = 0x40 | (drbs_used); // DRB IDs encoded using bit 6
        rbUpdate->upsertBearers.emplace_back(bearer);

        auto list = session.qosFlows->list;
        for (int i = 0; i < list.count; ++i)
        {
            SdapMapping mapping;
            mapping.psi = session.psi;

            const auto &flow = list.array[i];
            mapping.qfi = flow->qosFlowIdentifier;
            mapping.radioBearer = bearer.bearerId; // The DRB that was just created
            sdapUpdate->upsertSdapMappings.emplace_back(mapping);
        }

        m_logger->debug("UE[%ld]: adding SDAP mapping for PSI=%d, QFI=%d to DRBearer=%d",
            ueId, session.psi, session.qosFlows->list.count, drbs_used);
    }

    // send to RLS to setup the bearers
    {
        auto m = std::make_unique<NmGnbRrcToRls>(NmGnbRrcToRls::RADIO_BEARER_UPDATE);
        m->rbUpdate = std::move(rbUpdate);
        m->sdapUpdate = std::move(sdapUpdate);
        m->ueId = ueId;
        m_base->rlsTask->push(std::move(m));
    }

    // ---- Build RRCReconfiguration DL-DCCH message ----
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present =
        ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();

    long txId = ue->getNextTid();
    reconfig->rrc_TransactionIdentifier = txId;
    reconfig->criticalExtensions.present =
        ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();


    // add the NAS message to the dedicated NAS message list (in the nonCriticalExtension)
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();

    ies->nonCriticalExtension->dedicatedNAS_MessageList->list.array = asn::New<ASN_RRC_DedicatedNAS_Message_t*>();
    ies->nonCriticalExtension->dedicatedNAS_MessageList->list.count = 1;
    asn::SetOctetString(*ies->nonCriticalExtension->dedicatedNAS_MessageList->list.array[0], nasPdu);

    // add radioBearer Config
    ies->radioBearerConfig = asn::New<ASN_RRC_RadioBearerConfig_t>();
    ies->radioBearerConfig->drb_ToAddModList = asn::New<ASN_RRC_DRB_ToAddModList_t>();
    ies->radioBearerConfig->srb_ToAddModList = asn::New<ASN_RRC_SRB_ToAddModList_t>();

    // iterate the radio bearers in the update list.  If the bearerID bit 6 is 0, it is an SRB, otherwise it's a DRB
    int srb_idx = 0;
    int drb_idx = 0;
    for (const auto &bearer : rbUpdate->upsertBearers)
    {
        if ((bearer.bearerId & 0x40) == 0)
        {
            // SRB
            auto srb = asn::New<ASN_RRC_SRB_ToAddMod_t>();
            srb->srb_Identity = bearer.bearerId & 0x3F; // ID is in bits 0-5
            ies->radioBearerConfig->srb_ToAddModList->list.array[srb_idx++] = srb;
            ies->radioBearerConfig->srb_ToAddModList->list.count = srb_idx;
        }
        else
        {
            // DRB
            auto drb = asn::New<ASN_RRC_DRB_ToAddMod_t>();
            drb->drb_Identity = bearer.bearerId & 0x3F; // ID is in bits 0-5

            auto sdapConfig = asn::New<ASN_RRC_SDAP_Config_t>();
            sdapConfig->defaultDRB = true;  // works for now, as we only use one DRB per session
            // Note - we don't use the SDAP header UL/DL fields, because RLS includes a QoS byte in the PDU payload

            // loop through each SDAP mapping, and if the mapping is for this DRB, add it to the SDAP config
            int qfi_count = 0;
            for (const auto &mapping : sdapUpdate->upsertSdapMappings)
            {
                if (mapping.radioBearer == (bearer.bearerId & 0x7F))
                {
                    // Add the SDAP mapping to the config
                    sdapConfig->pdu_Session = mapping.psi;

                    auto sdapQosFlow = asn::New<ASN_RRC_QFI_t>();

                    *sdapQosFlow = mapping.qfi;
                    sdapConfig->mappedQoS_FlowsToAdd->list.array[qfi_count++] = sdapQosFlow;
                    sdapConfig->mappedQoS_FlowsToAdd->list.count = qfi_count;
                }
            }

            // Set the SDAP config in the DRB
            drb->cnAssociation->choice.sdap_Config = sdapConfig;

            // Add the DRB to the list
            ies->radioBearerConfig->drb_ToAddModList->list.array[drb_idx++] = drb;
            ies->radioBearerConfig->drb_ToAddModList->list.count = drb_idx;
        }
    }

    // send the message
    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

}


    void GnbRrcTask::handleDownlinkNasDelivery(int64_t ueId, const OctetString &nasPdu)
{
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 =
        asn::New<ASN_RRC_DL_DCCH_MessageType_t::ASN_RRC_DL_DCCH_MessageType_u::ASN_RRC_DL_DCCH_MessageType__c1>();
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_dlInformationTransfer;
    pdu->message.choice.c1->choice.dlInformationTransfer = asn::New<ASN_RRC_DLInformationTransfer>();

    auto &c1 = pdu->message.choice.c1->choice.dlInformationTransfer->criticalExtensions;
    c1.present = ASN_RRC_DLInformationTransfer__criticalExtensions_PR_dlInformationTransfer;
    c1.choice.dlInformationTransfer = asn::New<ASN_RRC_DLInformationTransfer_IEs>();
    c1.choice.dlInformationTransfer->dedicatedNAS_Message = asn::New<ASN_RRC_DedicatedNAS_Message_t>();
    asn::SetOctetString(*c1.choice.dlInformationTransfer->dedicatedNAS_Message, nasPdu);

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
}

void GnbRrcTask::deliverUplinkNas(int64_t ueId, OctetString &&nasPdu)
{
    auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::UPLINK_NAS_DELIVERY);
    w->ueId = ueId;
    w->pdu = std::move(nasPdu);
    m_base->ngapTask->push(std::move(w));
}

void GnbRrcTask::receiveUplinkInformationTransfer(int64_t ueId, const ASN_RRC_ULInformationTransfer &msg)
{
    if (msg.criticalExtensions.present == ASN_RRC_ULInformationTransfer__criticalExtensions_PR_ulInformationTransfer)
        deliverUplinkNas(
            ueId, asn::GetOctetString(*msg.criticalExtensions.choice.ulInformationTransfer->dedicatedNAS_Message));
}

void GnbRrcTask::releaseConnection(int64_t ueId)
{
 
    // Send RRC Release message
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcRelease;
    auto &rrcRelease = pdu->message.choice.c1->choice.rrcRelease = asn::New<ASN_RRC_RRCRelease>();
    auto *releaseUe = findCtxByUeId(ueId);
    rrcRelease->rrc_TransactionIdentifier = releaseUe ? releaseUe->getNextTid() : 0;
    rrcRelease->criticalExtensions.present = ASN_RRC_RRCRelease__criticalExtensions_PR_rrcRelease;
    rrcRelease->criticalExtensions.choice.rrcRelease = asn::New<ASN_RRC_RRCRelease_IEs>();

    m_logger->info("UE[%ld] Sending RRC Release message to UE", ueId);

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    handoverContextRelease(ueId);
}

void GnbRrcTask::handleRadioLinkFailure(int64_t ueId)
{
    // Notify NGAP task
    auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::RADIO_LINK_FAILURE);
    w->ueId = ueId;
    m_base->ngapTask->push(std::move(w));

    handoverContextRelease(ueId);
}

void GnbRrcTask::handlePaging(const asn::Unique<ASN_NGAP_FiveG_S_TMSI> &tmsi,
                              const asn::Unique<ASN_NGAP_TAIListForPaging> &taiList)
{
    // Construct and send a Paging message
    auto *pdu = asn::New<ASN_RRC_PCCH_Message>();
    pdu->message.present = ASN_RRC_PCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_PCCH_MessageType__c1_PR_paging;
    auto &paging = pdu->message.choice.c1->choice.paging = asn::New<ASN_RRC_Paging>();

    auto *record = asn::New<ASN_RRC_PagingRecord>();
    record->ue_Identity.present = ASN_RRC_PagingUE_Identity_PR_ng_5G_S_TMSI;

    OctetString tmsiOctets{};
    tmsiOctets.appendOctet2(bits::Ranged16({
        {10, asn::GetBitStringInt<10>(tmsi->aMFSetID)},
        {6, asn::GetBitStringInt<10>(tmsi->aMFPointer)},
    }));
    tmsiOctets.append(asn::GetOctetString(tmsi->fiveG_TMSI));

    asn::SetBitString(record->ue_Identity.choice.ng_5G_S_TMSI, tmsiOctets);

    paging->pagingRecordList = asn::NewFor(paging->pagingRecordList);
    asn::SequenceAdd(*paging->pagingRecordList, record);

    sendRrcMessage(pdu);
    asn::Free(asn_DEF_ASN_RRC_PCCH_Message, pdu);
}

// Receives the security information from NGAP and sends RRC Security Mode Command to UE
void GnbRrcTask::handleNgapSecurityInfo(int64_t ueId, std::unique_ptr<UeSecurityInfo> secInfo)
{
    m_logger->debug("UE[%ld]: Security information received from NGAP", ueId);
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
        return;

    // set UE context with the received security information
    ue->ueSecInfo = *secInfo;

    // set the security algorithms

    // In a real implementation, this would involve selecting the appropriate algorithms based on the UE's capabilities
    //  and teh GNB capabilities.  Here, we simply use the default algorithms for ciphering and integrity protection
    long cipheringAlgorithm = 0;  // (NEA0)

    auto integrityAlgorithm = asn::New<ASN_RRC_IntegrityProtAlgorithm_t>();
    *integrityAlgorithm = 0;  // (NIA0)

    // Send RRC Security Mode Command to UE immediately after receiving security info from NGAP
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_securityModeCommand;
    auto &secModeCmd = pdu->message.choice.c1->choice.securityModeCommand = asn::New<ASN_RRC_SecurityModeCommand>();
    secModeCmd->rrc_TransactionIdentifier = ue->getNextTid();
    secModeCmd->criticalExtensions.present = ASN_RRC_SecurityModeCommand__criticalExtensions_PR_securityModeCommand;
    secModeCmd->criticalExtensions.choice.securityModeCommand = asn::New<ASN_RRC_SecurityModeCommand_IEs>();

    auto secConfig = asn::New<ASN_RRC_SecurityConfigSMC_t>();
    secModeCmd->criticalExtensions.choice.securityModeCommand->securityConfigSMC = *secConfig;

    secConfig->securityAlgorithmConfig = *asn::New<ASN_RRC_SecurityAlgorithmConfig_t>();
    secConfig->securityAlgorithmConfig.cipheringAlgorithm = cipheringAlgorithm;
    secConfig->securityAlgorithmConfig.integrityProtAlgorithm = integrityAlgorithm;

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);


}

} // namespace nr::gnb