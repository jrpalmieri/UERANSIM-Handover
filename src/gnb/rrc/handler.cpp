//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <algorithm>

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
#include <asn/rrc/ASN_RRC_DRB-ToReleaseList.h>
#include <asn/rrc/ASN_RRC_SDAP-Config.h>
#include <asn/rrc/ASN_RRC_SRB-ToAddMod.h>
#include <asn/rrc/ASN_RRC_RadioBearerConfig.h>
#include <asn/rrc/ASN_RRC_DedicatedNAS-Message.h>



namespace nr::gnb
{


// Assigns (or reuses) a DRB id for a PSI, drawing from the UE's bearerMap as the
// authoritative pool.  Re-setup of an already-mapped PSI returns its existing DRB
// (idempotent modify); otherwise the lowest free id in the spec's 1..32 range is
// chosen.  Returns the RLS-encoded bearer id (0x40 | id), or 0 if exhausted.
uint8_t GnbRrcTask::allocateDrbId(RrcUeContext *ue, int psi)
{
    auto existing = ue->bearerMap.find(psi);
    if (existing != ue->bearerMap.end())
        return existing->second.drbId;

    std::array<bool, 33> used{}; // index by plain DRB identity 1..32
    for (const auto &[mappedPsi, info] : ue->bearerMap)
    {
        (void)mappedPsi;
        int id = info.drbId & 0x3F;
        if (id >= 1 && id <= 32)
            used[id] = true;
    }

    for (int id = 1; id <= 32; ++id)
        if (!used[id])
            return static_cast<uint8_t>(0x40 | id);

    m_logger->err("UE[%ld]: no free DRB id (1..32) to allocate for PSI=%d", ue->ueId, psi);
    return 0;
}

// Assigns one DRB per session (reusing existing DRBs for known PSIs), records the
// result in ue->bearerMap, and appends the upsert entries to the RLS update lists.
void GnbRrcTask::assignSessionBearers(RrcUeContext *ue, const std::vector<PduSessionResource> &sessions,
                                      RadioBearerUpdate &rbUpdate, SdapUpdate &sdapUpdate)
{
    for (const auto &session : sessions)
    {
        uint8_t bearerId = allocateDrbId(ue, session.psi);
        if (bearerId == 0)
        {
            m_logger->warn("UE[%ld]: skipping bearer setup for PSI=%d (no free DRB id)", ue->ueId, session.psi);
            continue;
        }

        rbUpdate.upsertBearers.emplace_back(RadioBearer{bearerId, 0, 0});

        // Record/refresh the PSI→DRB mapping in RRC's authoritative bearer map so a
        // later session release can find the DRB/QFIs to tear down.
        RrcDrbInfo info;
        info.drbId = bearerId;
        for (const auto &flow : session.qosFlows)
        {
            info.qfis.push_back(flow.qfi);
            sdapUpdate.upsertSdapMappings.emplace_back(SdapMapping{session.psi, flow.qfi, bearerId});
        }
        ue->bearerMap[session.psi] = std::move(info);

        m_logger->debug("UE[%ld]: mapped PSI=%d (QFI count=%zu) onto DRB=%d", ue->ueId, session.psi,
                        session.qosFlows.size(), bearerId & 0x3F);
    }
}

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

    // Assign/reuse DRBs, populate the RRC bearer map, and fill the RLS update lists.
    assignSessionBearers(ue, *sessionList, *rbUpdate, *sdapUpdate);

    m_logger->debug("UE[%ld]: creating %zu DRBs and %zu SDAP mappings", ueId,
                    rbUpdate->upsertBearers.size(), sdapUpdate->upsertSdapMappings.size());

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
        asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    }
}


// Tears down the radio bearers serving a set of 5GC-released PDU sessions.
// Driven from NGAP when the core drops sessions at path switch
// (PDUSessionResourceReleasedListPSAck) or via a PDUSessionResourceReleaseCommand.
// RRC is the source of truth for the PSI→QFI→DRB mapping, so it: (1) collects the
// DRB(s) and SDAP mappings serving the released PSIs from its bearer map, (2) tells
// RLS to delete them (RADIO_BEARER_UPDATE with delete lists), (3) tells the UE to
// release the DRBs (RRCReconfiguration with a drb-ToReleaseList), and (4) prunes its
// own bearer map.
void GnbRrcTask::handleNgapPduSessionRelease(int64_t ueId, const std::vector<int> &releasedPsis)
{
    m_logger->debug("UE[%ld] PDU session release received.  PSI count=%zu", ueId, releasedPsis.size());

    auto *ue = findCtxByUeId(ueId);
    if (!ue) {
        m_logger->err("UE[%ld]:handleNgapPduSessionRelease: UE not found", ueId);
        return;
    }

    auto rbUpdate = std::make_unique<RadioBearerUpdate>();
    auto sdapUpdate = std::make_unique<SdapUpdate>();
    std::vector<uint8_t> releasedDrbIds; // RLS-encoded bearer ids (bit 6 set)

    for (int psi : releasedPsis)
    {
        auto psiIt = ue->bearerMap.find(psi);
        if (psiIt == ue->bearerMap.end())
        {
            m_logger->warn("UE[%ld]: PSI[%d] release requested but no bearer mapping is known; skipping", ueId, psi);
            continue;
        }

        const uint8_t bearerId = psiIt->second.drbId;

        // Never touch SRBs or a non-data bearer.
        if ((bearerId & 0x40) != 0)
        {
            for (int qfi : psiIt->second.qfis)
                sdapUpdate->deleteSdapMappings.emplace_back(SdapMapping{psi, qfi, bearerId});

            releasedDrbIds.emplace_back(bearerId);
            rbUpdate->deleteBearers.emplace_back(bearerId);
        }

        // Release the DRB id back into the pool.
        ue->bearerMap.erase(psiIt);
    }

    if (releasedDrbIds.empty())
    {
        m_logger->debug("UE[%ld]: PDU session release found no DRBs to tear down", ueId);
        return;
    }

    // (2) Tell RLS to delete the bearers and SDAP mappings.
    {
        auto m = std::make_unique<NmGnbRrcToRls>(NmGnbRrcToRls::RADIO_BEARER_UPDATE);
        m->ueId = ueId;
        m->rbUpdate = std::move(rbUpdate);
        m->sdapUpdate = std::move(sdapUpdate);
        m_base->rlsTask->push(std::move(m));
        m_logger->debug("UE[%ld]: Sent radio bearer/SDAP delete to RLS (%zu DRBs)", ueId, releasedDrbIds.size());
    }

    // (3) Tell the UE to release the DRBs via RRCReconfiguration (drb-ToReleaseList).
    auto *drbReleaseList = asn::New<ASN_RRC_DRB_ToReleaseList_t>();
    for (uint8_t bearerId : releasedDrbIds)
    {
        auto *drbId = asn::New<ASN_RRC_DRB_Identity_t>();
        *drbId = bearerId & 0x3F; // plain DRB identity (1..32)
        asn_sequence_add(&drbReleaseList->list, drbId);
    }

    auto *radioBearerConfig = asn::New<ASN_RRC_RadioBearerConfig_t>();
    radioBearerConfig->drb_ToReleaseList = drbReleaseList;

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
    ies->radioBearerConfig = radioBearerConfig;

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    m_logger->debug("UE[%ld]: Sent RRC Reconfiguration releasing %zu DRBs.  TxId=%ld", ueId, releasedDrbIds.size(), txId);
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



/**
 * @brief handler for NGAP UE Context Release messages received from the AMF.
 * Called by the RRC task when it receives a UE_CONTEXT_RELEASE_RECEIVED message from NGAP.
 * 
 * @param ueId 
 */
void GnbRrcTask::handleUeContextRelease(int64_t ueId, NgapCause cause)
{
    bool isHandover = (cause == NgapCause::RadioNetwork_successful_handover);

    m_logger->debug("UE[%ld] handleUeContextRelease: cause=%d, isHandover=%s",
        ueId, static_cast<int>(cause), isHandover ? "true" : "false");

    if (!isHandover)
    {
        // Send RRC Release to the UE only for non-handover releases
        auto *releaseUe = findCtxByUeId(ueId);
        if (releaseUe)
        {
            auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
            pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
            pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
            pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcRelease;
            auto &rrcRelease = pdu->message.choice.c1->choice.rrcRelease = asn::New<ASN_RRC_RRCRelease>();
            rrcRelease->rrc_TransactionIdentifier = releaseUe->getNextTid();
            rrcRelease->criticalExtensions.present = ASN_RRC_RRCRelease__criticalExtensions_PR_rrcRelease;
            rrcRelease->criticalExtensions.choice.rrcRelease = asn::New<ASN_RRC_RRCRelease_IEs>();

            m_logger->info("UE[%ld] Sending RRC Release message to UE", ueId);

            sendRrcMessage(ueId, pdu);
            asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
        }
    }
    else
    {
        m_logger->info("UE[%ld] Skipping RRC Release - context release is handover-related", ueId);
    }

    ueContextRelease(ueId);
}


void GnbRrcTask::handleRadioLinkFailure(int64_t ueId)
{

    // TODO: Implement more advanced RLF handling logic.  Right now we simply clear all UE contexts.
    //   A more advanced implementation would set a timer to allow the UE to reconnect or await a 
    //   handover notification from a peer gNB.

    // Notify NGAP task of the RLF event so it can inform the AMF and clear its context
    auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::RADIO_LINK_FAILURE);
    w->ueId = ueId;
    m_base->ngapTask->push(std::move(w));

    // Notify RLS task of the RLF event so it can clear its context (radio bearers, SDAP mappings, etc.)
    auto m = std::make_unique<NmGnbRrcToRls>(NmGnbRrcToRls::REMOVE_UE_CONTEXT);
    m->ueId = ueId;
    m_base->rlsTask->push(std::move(m));

    // erase UE RRC Context
    ueContextRelease(ueId);
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

    int txId = ue->getNextTid();

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

    // attach the RRC TransactionId
    secModeCmd->rrc_TransactionIdentifier = static_cast<ASN_RRC_RRC_TransactionIdentifier_t>(txId);
    secModeCmd->criticalExtensions.present = ASN_RRC_SecurityModeCommand__criticalExtensions_PR_securityModeCommand;
    secModeCmd->criticalExtensions.choice.securityModeCommand = asn::New<ASN_RRC_SecurityModeCommand_IEs>();

    auto secConfig = asn::New<ASN_RRC_SecurityConfigSMC_t>();
    secModeCmd->criticalExtensions.choice.securityModeCommand->securityConfigSMC = *secConfig;

    secConfig->securityAlgorithmConfig = *asn::New<ASN_RRC_SecurityAlgorithmConfig_t>();
    secConfig->securityAlgorithmConfig.cipheringAlgorithm = cipheringAlgorithm;
    secConfig->securityAlgorithmConfig.integrityProtAlgorithm = integrityAlgorithm;

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->debug("UE[%ld]: Sent RRC Security Mode Command to UE, TxId=%d", ueId, txId);

}

} // namespace nr::gnb