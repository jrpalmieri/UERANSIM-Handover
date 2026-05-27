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


ASN_RRC_RadioBearerConfig_t* GnbRrcTask::createRadioBearerConfig(int64_t ueId, std::unique_ptr<std::vector<PduSessionResource>> &sessionList)
{

    m_logger->debug("UE[%ld]: Creating Radio Bearer Config. PDU session list [count=%d]", ueId, sessionList ? sessionList->size() : 0);

    // get UE context
    auto *ue = findCtxByUeId(ueId);
    if (!ue) {
        m_logger->err("UE[%ld]:createRadioBearerConfig: UE not found, exiting.", ueId);
        return nullptr;
    }

    // if sessionList is empty, we can't create any radio bearers
    if (!sessionList || sessionList->empty()) {
        m_logger->warn("UE[%ld]:createRadioBearerConfig: No PDU sessions to create radio bearers for, exiting.", ueId);
        return nullptr;
    }

    // return value for the radio bearer config
    ASN_RRC_RadioBearerConfig_t* radioBearerConfig = nullptr;

    // SDAP: map each PDU session to a bearer based on the QoS Flows
    // Note: for now, we map all QoS Flows for each PSI onto a single DRB

    auto rbUpdate = std::make_unique<RadioBearerUpdate>();
    auto sdapUpdate = std::make_unique<SdapUpdate>();

    // do SDAP mappings (if sessionList is not empty)
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

    m_logger->debug("UE[%ld]: creating %d DRBs and %d SDAP mappings", ueId, drbs_used, sdapUpdate->upsertSdapMappings.size());

    // copy bearer/SDAP data before moving into the RLS message
    auto bearers = rbUpdate->upsertBearers;
    auto sdapMappings = sdapUpdate->upsertSdapMappings;

    // send to RLS to setup the bearers
    {
        auto m = std::make_unique<NmGnbRrcToRls>(NmGnbRrcToRls::RADIO_BEARER_UPDATE);
        m->rbUpdate = std::move(rbUpdate);
        m->sdapUpdate = std::move(sdapUpdate);
        m->ueId = ueId;
        m_base->rlsTask->push(std::move(m));
        m_logger->debug("UE[%ld]: Sent radio bearer and SDAP update to RLS", ueId);
    }

    // radioBearer lists
    auto drbList = asn::New<ASN_RRC_DRB_ToAddModList_t>();

    // iterate the radio bearers in the update list.  DRBs must have bearerID bit 6 == 1
    int drb_idx = 0;
    for (const auto &bearer : bearers)
    {
        if ((bearer.bearerId & 0x40) != 0)
        {
            // DRB
            auto drb = asn::New<ASN_RRC_DRB_ToAddMod_t>();
            drb->drb_Identity = bearer.bearerId & 0x3F;

            auto sdapConfig = asn::New<ASN_RRC_SDAP_Config_t>();
            sdapConfig->defaultDRB = true;  // works for now, as we only use one DRB per session
            // Note - we don't use the SDAP header UL/DL fields, because RLS includes a QoS byte in the PDU payload

            // loop through each SDAP mapping, and if the mapping is for this DRB, add it to the SDAP config
            for (const auto &mapping : sdapMappings)
            {
                if (mapping.radioBearer == (bearer.bearerId & 0x7F))
                {
                    sdapConfig->pdu_Session = mapping.psi;

                    if (!sdapConfig->mappedQoS_FlowsToAdd)
                        sdapConfig->mappedQoS_FlowsToAdd =
                            asn::New<ASN_RRC_SDAP_Config::ASN_RRC_SDAP_Config__mappedQoS_FlowsToAdd>();

                    auto *sdapQosFlow = asn::New<ASN_RRC_QFI_t>();
                    *sdapQosFlow = mapping.qfi;
                    asn_sequence_add(&sdapConfig->mappedQoS_FlowsToAdd->list, sdapQosFlow);
                }
            }

            // Set the SDAP config in the DRB
            drb->cnAssociation = asn::New<ASN_RRC_DRB_ToAddMod::ASN_RRC_DRB_ToAddMod__cnAssociation>();
            drb->cnAssociation->present = ASN_RRC_DRB_ToAddMod__cnAssociation_PR_sdap_Config;
            drb->cnAssociation->choice.sdap_Config = sdapConfig;

            asn_sequence_add(&drbList->list, drb);
            ++drb_idx;
        }
    }

    if (drb_idx > 0)
    {
        m_logger->debug("UE[%ld]: Adding %d DRBs to the RRC Reconfiguration", ueId, drb_idx);
        radioBearerConfig = asn::New<ASN_RRC_RadioBearerConfig_t>();
        radioBearerConfig->drb_ToAddModList = drbList;
    }

    return radioBearerConfig;

}



void GnbRrcTask::handleDownlinkNasAccept(int64_t ueId, const OctetString &nasPdu, std::unique_ptr<std::vector<PduSessionResource>> sessionList)
{

    m_logger->debug("UE[%ld] Downlink NAS Accept received.  PDU session list [count=%d]", ueId, sessionList ? sessionList->size() : 0);

    // get UE context
    auto *ue = findCtxByUeId(ueId);
    if (!ue) {
        m_logger->err("UE[%ld]:handleDownlinkNasAccept: UE not found", ueId);
        return;
    }

    // If there are PDU sessions, create the radio bearer configuration
    ASN_RRC_RadioBearerConfig_t *radioBearerConfig = nullptr;
    if (sessionList && !sessionList->empty())
        radioBearerConfig = createRadioBearerConfig(ueId, sessionList);

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

    m_logger->debug("UE[%ld]: Building RRC Reconfiguration with NAS Accept, radio bearer and SDAP updates.  TxId=%ld", ueId, txId);

    // add the NAS message to the dedicated NAS message list (in the nonCriticalExtension)
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->dedicatedNAS_MessageList =
        asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs::ASN_RRC_RRCReconfiguration_v1530_IEs__dedicatedNAS_MessageList>();

    auto *nasMsg = asn::New<ASN_RRC_DedicatedNAS_Message_t>();
    asn::SetOctetString(*nasMsg, nasPdu);
    asn_sequence_add(&ies->nonCriticalExtension->dedicatedNAS_MessageList->list, nasMsg);
    m_logger->debug("UE[%ld]: Added NAS message to the dedicated NAS message list", ueId);

    if (radioBearerConfig)
    {
        ies->radioBearerConfig = radioBearerConfig;
        m_logger->debug("UE[%ld]: Added Radio Bearer Config to the RRC Reconfiguration", ueId);
    }

    // send the message
    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    m_logger->debug("UE[%ld]: Sent RRC Reconfiguration with NAS Accept, radio bearer and SDAP updates", ueId);

}


void GnbRrcTask::handleNgapPduSessionUpdate(int64_t ueId, std::unique_ptr<std::vector<PduSessionResource>> sessionList)
{

    m_logger->debug("UE[%ld] NGAP PDU Session Update received.  PDU session list [count=%d]", ueId, sessionList ? sessionList->size() : 0);

    // get UE context
    auto *ue = findCtxByUeId(ueId);
    if (!ue) {
        m_logger->err("UE[%ld]:handleNgapPduSessionUpdate: UE not found", ueId);
        return;
    }

    // If there are PDU sessions, create the radio bearer configuration
    ASN_RRC_RadioBearerConfig_t *radioBearerConfig = nullptr;
    if (sessionList && !sessionList->empty())
        radioBearerConfig = createRadioBearerConfig(ueId, sessionList);

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

    m_logger->debug("UE[%ld]: Building RRC Reconfiguration for PDU session update.  TxId=%ld", ueId, txId);

    if (radioBearerConfig)
    {
        ies->radioBearerConfig = radioBearerConfig;
        m_logger->debug("UE[%ld]: Added Radio Bearer Config to the RRC Reconfiguration", ueId);

        // send the message
        sendRrcMessage(ueId, pdu);
        asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
        m_logger->debug("UE[%ld]: Sent RRC Reconfiguration with Radio Bearer Config, SDAP updates", ueId);

    }
    else
    {
        m_logger->debug("UE[%ld]: No radio bearer config to add to the RRC Reconfiguration, aborting RRC Reconfiguration", ueId);
    }
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