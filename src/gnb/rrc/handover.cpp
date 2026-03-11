//
// Phase 4 – gNB-side handover support.
//
// Implements:
//   1. receiveRrcReconfigurationComplete()  – handle UE's HO completion on target gNB
//   2. receiveMeasurementReport()           – parse and act on UE measurement reports
//   3. sendHandoverCommand()                – build RRCReconfiguration with
//                                             ReconfigurationWithSync and send to UE
//   4. handleHandoverComplete()             – post-handover processing (logging, NGAP notify)
//   5. sendMeasConfig()                     – send measurement configuration to UE
//   6. evaluateHandoverDecision()           – decide whether to initiate handover
//   7. handleNgapHandoverCommand()          – forward NGAP Handover Command (RRC container) to UE
//

#include "task.hpp"

#include <gnb/ngap/task.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>

#include <asn/rrc/ASN_RRC_MeasurementReport.h>
#include <asn/rrc/ASN_RRC_MeasurementReport-IEs.h>
#include <asn/rrc/ASN_RRC_MeasResults.h>
#include <asn/rrc/ASN_RRC_MeasResultNR.h>
#include <asn/rrc/ASN_RRC_MeasResultListNR.h>
#include <asn/rrc/ASN_RRC_MeasResultServMO.h>
#include <asn/rrc/ASN_RRC_MeasResultServMOList.h>
#include <asn/rrc/ASN_RRC_MeasQuantityResults.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1530-IEs.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_SpCellConfig.h>
#include <asn/rrc/ASN_RRC_ReconfigurationWithSync.h>
#include <asn/rrc/ASN_RRC_ServingCellConfigCommon.h>
#include <asn/rrc/ASN_RRC_DL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_DL-DCCH-MessageType.h>
#include <asn/rrc/ASN_RRC_MeasConfig.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddModList.h>
#include <asn/rrc/ASN_RRC_MeasObjectNR.h>
#include <asn/rrc/ASN_RRC_ReportConfigToAddMod.h>
#include <asn/rrc/ASN_RRC_ReportConfigToAddModList.h>
#include <asn/rrc/ASN_RRC_ReportConfigNR.h>
#include <asn/rrc/ASN_RRC_EventTriggerConfig.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddModList.h>
#include <asn/rrc/ASN_RRC_SSB-MTC.h>
#include <asn/rrc/ASN_RRC_SubcarrierSpacing.h>
#include <asn/rrc/ASN_RRC_MeasTriggerQuantityOffset.h>

namespace nr::gnb
{

/* ================================================================== */
/*  Helper: RSRP_Range → dBm                                         */
/* ================================================================== */

static int rsrpRangeToDbm(long val)
{
    return static_cast<int>(val) - 156; // TS 38.133 Table 10.1.6.1-1
}

/* ================================================================== */
/*  Helper: T304 milliseconds → ASN enum value                        */
/* ================================================================== */

static long t304MsToEnum(int ms)
{
    switch (ms)
    {
    case 50:    return 0;
    case 100:   return 1;
    case 150:   return 2;
    case 200:   return 3;
    case 500:   return 4;
    case 1000:  return 5;
    case 2000:  return 6;
    case 10000: return 7;
    default:    return 5; // default ms1000
    }
}

/* ================================================================== */
/*  Receive RRCReconfigurationComplete from UE                        */
/* ================================================================== */

void GnbRrcTask::receiveRrcReconfigurationComplete(int ueId,
    const ASN_RRC_RRCReconfigurationComplete &msg)
{
    auto *ue = findUe(ueId);
    if (!ue)
    {
        m_logger->warn("RRCReconfigurationComplete from unknown UE[%d]", ueId);
        return;
    }

    long txId = msg.rrc_TransactionIdentifier;
    m_logger->info("RRCReconfigurationComplete received from UE[%d] (txId=%ld)", ueId, txId);

    // Check if this is a handover completion
    if (ue->handoverInProgress)
    {
        ue->handoverInProgress = false;
        m_logger->info("Handover completed for UE[%d]: targetPCI=%d newC-RNTI=%d",
                       ueId, ue->handoverTargetPci, ue->handoverNewCrnti);
        handleHandoverComplete(ueId);
    }
    else
    {
        m_logger->debug("Normal reconfiguration completed for UE[%d]", ueId);
    }
}

/* ================================================================== */
/*  Post-handover processing                                          */
/* ================================================================== */

void GnbRrcTask::handleHandoverComplete(int ueId)
{
    auto *ue = findUe(ueId);
    if (!ue)
        return;

    // Notify NGAP of handover completion so it can trigger PathSwitchRequest
    // if this is the target gNB receiving the handover.
    auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_NOTIFY);
    w->ueId = ueId;
    m_base->ngapTask->push(std::move(w));

    m_logger->info("Handover complete notification sent to NGAP for UE[%d]", ueId);
}

/* ================================================================== */
/*  Receive MeasurementReport from UE                                 */
/* ================================================================== */

void GnbRrcTask::receiveMeasurementReport(int ueId,
    const ASN_RRC_MeasurementReport &msg)
{
    auto *ue = findUe(ueId);
    if (!ue)
    {
        m_logger->warn("MeasurementReport from unknown UE[%d]", ueId);
        return;
    }

    if (msg.criticalExtensions.present !=
        ASN_RRC_MeasurementReport__criticalExtensions_PR_measurementReport)
    {
        m_logger->err("MeasurementReport: unrecognised criticalExtensions");
        return;
    }

    auto *ies = msg.criticalExtensions.choice.measurementReport;
    if (!ies)
    {
        m_logger->err("MeasurementReport: null IEs");
        return;
    }

    auto &results = ies->measResults;
    long measId = results.measId;

    // Extract serving cell measurement
    int servingRsrp = -140;
    if (results.measResultServingMOList.list.count > 0)
    {
        auto *servMO = results.measResultServingMOList.list.array[0];
        if (servMO)
        {
            auto *ssbCell = servMO->measResultServingCell.measResult.cellResults.resultsSSB_Cell;
            if (ssbCell && ssbCell->rsrp)
                servingRsrp = rsrpRangeToDbm(*ssbCell->rsrp);
        }
    }

    m_logger->info("MeasurementReport from UE[%d]: measId=%ld servingRSRP=%ddBm",
                   ueId, measId, servingRsrp);

    // Extract neighbour cell measurements
    if (results.measResultNeighCells)
    {
        if (results.measResultNeighCells->present ==
            ASN_RRC_MeasResults__measResultNeighCells_PR_measResultListNR)
        {
            auto *nrList = results.measResultNeighCells->choice.measResultListNR;
            if (nrList)
            {
                int bestNeighPci = -1;
                int bestNeighRsrp = -200;

                for (int i = 0; i < nrList->list.count; i++)
                {
                    auto *nr = nrList->list.array[i];
                    if (!nr)
                        continue;

                    int pci = nr->physCellId ? static_cast<int>(*nr->physCellId) : -1;
                    int rsrp = -140;
                    auto *ssbCell = nr->measResult.cellResults.resultsSSB_Cell;
                    if (ssbCell && ssbCell->rsrp)
                        rsrp = rsrpRangeToDbm(*ssbCell->rsrp);

                    m_logger->debug("  Neighbor PCI=%d RSRP=%ddBm", pci, rsrp);

                    if (rsrp > bestNeighRsrp)
                    {
                        bestNeighRsrp = rsrp;
                        bestNeighPci = pci;
                    }
                }

                // Store best neighbour info in UE context for potential handover decision
                if (bestNeighPci >= 0)
                {
                    ue->lastMeasReportPci = bestNeighPci;
                    ue->lastMeasReportRsrp = bestNeighRsrp;
                    m_logger->info("Best neighbor: PCI=%d RSRP=%ddBm (serving=%ddBm)",
                                   bestNeighPci, bestNeighRsrp, servingRsrp);
                }
            }
        }
    }

    // Update serving RSRP and evaluate handover decision
    ue->lastServingRsrp = servingRsrp;
    evaluateHandoverDecision(ueId);
}

/* ================================================================== */
/*  Send RRCReconfiguration with ReconfigurationWithSync (Handover)   */
/* ================================================================== */

void GnbRrcTask::sendHandoverCommand(int ueId, int targetPci, int newCrnti, int t304Ms)
{
    auto *ue = findUe(ueId);
    if (!ue)
    {
        m_logger->err("Cannot send handover command: UE[%d] not found", ueId);
        return;
    }

    m_logger->info("Sending handover command to UE[%d]: targetPCI=%d newC-RNTI=%d t304=%dms",
                   ueId, targetPci, newCrnti, t304Ms);

    // ---- Build CellGroupConfig with ReconfigurationWithSync ----
    ASN_RRC_ReconfigurationWithSync rws{};
    rws.newUE_Identity = static_cast<long>(newCrnti);
    rws.t304 = t304MsToEnum(t304Ms);

    // Set target cell PCI via spCellConfigCommon
    ASN_RRC_ServingCellConfigCommon scc{};
    long pci = static_cast<long>(targetPci);
    scc.physCellId = &pci;
    rws.spCellConfigCommon = &scc;

    // Wrap in SpCellConfig
    ASN_RRC_SpCellConfig spCellConfig{};
    spCellConfig.reconfigurationWithSync = &rws;

    // Wrap in CellGroupConfig
    ASN_RRC_CellGroupConfig cellGroupConfig{};
    cellGroupConfig.cellGroupId = 0;
    cellGroupConfig.spCellConfig = &spCellConfig;

    // UPER-encode CellGroupConfig to OCTET STRING (masterCellGroup)
    OctetString masterCellGroupOctet =
        rrc::encode::EncodeS(asn_DEF_ASN_RRC_CellGroupConfig, &cellGroupConfig);

    if (masterCellGroupOctet.length() == 0)
    {
        m_logger->err("Failed to encode CellGroupConfig for handover command to UE[%d]", ueId);
        return;
    }

    // ---- Build RRCReconfiguration DL-DCCH message ----
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present =
        ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();

    long txId = getNextTid();
    reconfig->rrc_TransactionIdentifier = txId;
    reconfig->criticalExtensions.present =
        ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    // Set nonCriticalExtension (v1530-IEs) with masterCellGroup
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->masterCellGroup = asn::New<OCTET_STRING_t>();
    asn::SetOctetString(*ies->nonCriticalExtension->masterCellGroup, masterCellGroupOctet);

    // Record handover state in UE context
    ue->handoverInProgress = true;
    ue->handoverTargetPci = targetPci;
    ue->handoverNewCrnti = newCrnti;
    ue->handoverTxId = txId;

    sendRrcMessage(ueId, pdu);

    m_logger->info("RRCReconfiguration (handover) sent to UE[%d] txId=%ld", ueId, txId);
}

/* ================================================================== */
/*  Send MeasConfig to UE (A3 event for neighbor-better-than-serving) */
/* ================================================================== */

void GnbRrcTask::sendMeasConfig(int ueId)
{
    auto *ue = findUe(ueId);
    if (!ue)
        return;

    if (ue->measConfigSent)
        return; // already configured

    m_logger->info("Sending MeasConfig to UE[%d] with A3 event", ueId);

    // Build RRCReconfiguration with measConfig
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present =
        ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();

    long txId = getNextTid();
    reconfig->rrc_TransactionIdentifier = txId;
    reconfig->criticalExtensions.present =
        ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    // Build MeasConfig
    ies->measConfig = asn::New<ASN_RRC_MeasConfig>();
    auto *mc = ies->measConfig;

    // MeasObject: measObjectId=1, NR measurement object
    mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
    {
        auto *measObj = asn::New<ASN_RRC_MeasObjectToAddMod>();
        measObj->measObjectId = 1;
        measObj->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
        measObj->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
        measObj->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
        *measObj->measObject.choice.measObjectNR->ssbFrequency = 632628; // typical n78 SSB freq
        measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
        *measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing =
            ASN_RRC_SubcarrierSpacing_kHz30;
        // smtc1 (SSB MTC periodicity/offset/duration) - required for NR measObject
        measObj->measObject.choice.measObjectNR->smtc1 =
            asn::New<ASN_RRC_SSB_MTC>();
        measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
            ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
        measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
        measObj->measObject.choice.measObjectNR->smtc1->duration =
            ASN_RRC_SSB_MTC__duration_sf1;
        // quantityConfigIndex is mandatory INTEGER (1..maxNrofQuantityConfig); must be >= 1
        measObj->measObject.choice.measObjectNR->quantityConfigIndex = 1;
        asn::SequenceAdd(*mc->measObjectToAddModList, measObj);
    }

    // ReportConfig: reportConfigId=1, A3 event (neighbor > serving + 3 dB)
    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    {
        auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
        rcMod->reportConfigId = 1;
        rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
        rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

        auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;
        rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
        rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();

        auto *et = rcNR->reportType.choice.eventTriggered;
        et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
        asn::MakeNew(et->eventId.choice.eventA3);

        auto *a3 = et->eventId.choice.eventA3;
        // a3-Offset: 6 in 0.5 dB units = 3 dB
        a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
        a3->a3_Offset.choice.rsrp = 6; // 3 dB in 0.5 dB steps
        a3->hysteresis = 2;  // 1 dB in 0.5 dB steps
        a3->timeToTrigger = 4; // TTT enum 4 = 100ms
        a3->reportOnLeave = false;
        a3->useWhiteCellList = false;

        et->rsType = ASN_RRC_NR_RS_Type_ssb;
        et->reportInterval = ASN_RRC_ReportInterval_ms480;
        et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
        et->reportQuantityCell.rsrp = true;
        et->reportQuantityCell.rsrq = false;
        et->reportQuantityCell.sinr = false;
        et->maxReportCells = 4;

        asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);
    }

    // MeasId: link measObjectId=1 with reportConfigId=1
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();
    {
        auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
        measIdMod->measId = 1;
        measIdMod->measObjectId = 1;
        measIdMod->reportConfigId = 1;
        asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);
    }

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    ue->measConfigSent = true;
    m_logger->info("MeasConfig (A3 event) sent to UE[%d] txId=%ld", ueId, txId);
}

/* ================================================================== */
/*  Evaluate handover decision after receiving MeasurementReport      */
/* ================================================================== */

void GnbRrcTask::evaluateHandoverDecision(int ueId)
{
    auto *ue = findUe(ueId);
    if (!ue)
        return;

    // Don't trigger if handover is already in progress
    if (ue->handoverInProgress || ue->handoverDecisionPending)
        return;

    // Need a valid best neighbour
    if (ue->lastMeasReportPci < 0)
        return;

    int bestNeighPci = ue->lastMeasReportPci;
    int bestNeighRsrp = ue->lastMeasReportRsrp;
    int servingRsrp = ue->lastServingRsrp;

    // A3-like decision: neighbor is significantly better than serving
    // (this mirrors the A3 config sent to UE, but the gNB makes its own decision)
    int a3Offset = 3; // dB
    if (bestNeighRsrp > servingRsrp + a3Offset)
    {
        m_logger->info("Handover decision: UE[%d] -> targetPCI=%d (serving=%ddBm, target=%ddBm)",
                       ueId, bestNeighPci, servingRsrp, bestNeighRsrp);

        ue->handoverDecisionPending = true;

        // Initiate N2 handover via NGAP (no Xn interface available)
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
        w->ueId = ueId;
        w->hoTargetPci = bestNeighPci;
        w->hoCause = NgapCause::RadioNetwork_handover_desirable_for_radio_reason;
        m_base->ngapTask->push(std::move(w));

        m_logger->info("HandoverRequired sent to NGAP for UE[%d] targetPCI=%d", ueId, bestNeighPci);
    }
}

/* ================================================================== */
/*  Handle NGAP Handover Command: forward RRC container to UE         */
/* ================================================================== */

void GnbRrcTask::handleNgapHandoverCommand(int ueId, const OctetString &rrcContainer)
{
    auto *ue = findUe(ueId);
    if (!ue)
    {
        m_logger->warn("handleNgapHandoverCommand: UE[%d] not found", ueId);
        return;
    }

    m_logger->info("Forwarding NGAP Handover Command RRC container to UE[%d] (%d bytes)",
                   ueId, static_cast<int>(rrcContainer.length()));

    // The RRC container from the AMF (originally from the target gNB via
    // HandoverRequestAcknowledge) is an RRCReconfiguration message.
    // In a full implementation we would decode and potentially augment it.
    // For our simulator, we can build our own RRCReconfiguration with
    // ReconfigurationWithSync based on the handover state.

    // Use a default target PCI and C-RNTI from the UE context
    // (these were set when we initiated the HANDOVER_REQUIRED)
    int targetPci = ue->handoverTargetPci;
    int newCrnti = ue->handoverNewCrnti > 0 ? ue->handoverNewCrnti : (ue->ueId + 0x1000);
    int t304Ms = 1000;

    ue->handoverDecisionPending = false;
    sendHandoverCommand(ueId, targetPci, newCrnti, t304Ms);
}

} // namespace nr::gnb
