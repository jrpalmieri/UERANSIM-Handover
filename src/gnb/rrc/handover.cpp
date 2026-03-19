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
#include <utils/common.hpp>
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

const int MIN_RSRP = cons::MIN_RSRP; // minimum RSRP value (in dBm) to use when no measurement is available
const int HANDOVER_TIMEOUT_MS = 5000; // time to wait for handover completion before considering it failed

namespace nr::gnb
{

/* ================================================================== */
/*  Helper: RSRP_Range → dBm                                         */
/* ================================================================== */

static int rsrpRangeToDbm(long val)
{
    return static_cast<int>(val) - 156; // TS 38.133 Table 10.1.6.1-1
}

static long rsrpDbmToRange(int dbm)
{
    int val = dbm + 156;
    if (val < 0)
        val = 0;
    if (val > 127)
        val = 127;
    return static_cast<long>(val);
}

static long dbToHalfDbSteps(int db)
{
    int val = db * 2;
    if (val < 0)
        val = 0;
    if (val > 30)
        val = 30;
    return static_cast<long>(val);
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

static int normalizeCrntiForRrc(int crnti)
{
    int normalized = crnti & 0xFFFF;
    if (normalized == 0)
        normalized = 1;
    return normalized;
}

/**
 * @brief Receives an RRCReconfigurationComplete message from a UE, which may indicate the 
 * completion of a handover.  If this is a handover completion, triggers post-handover 
 * processing such as NGAP notification. Otherwise, just logs the completion of a normal 
 * reconfiguration.
 * 
 * @param ueId 
 * @param msg 
 */
void GnbRrcTask::receiveRrcReconfigurationComplete(int ueId,
    const ASN_RRC_RRCReconfigurationComplete &msg)
{
    int64_t txId = msg.rrc_TransactionIdentifier;
    
    // check to see if this is a handover completion by looking up the pending handover context using the TxId
    if (m_handoversPending.count(txId)) {

        /* move the ctx from pending handover to UE context */

        // get ptr to rrc context in the pending handover map (indexed by txId)
        auto *handoverCtx = m_handoversPending[txId]->ctx;

        // check for old UE context with the same UE ID, if exists, remove it 
        // (since after handover completion, the old UE context is no longer valid)
        auto *ue = findCtxByUeId(ueId);
        if (ue)
            m_ueCtx.erase(ueId);

        // move the UE context from pending handover to UE context map and erase the pending handover
        m_ueCtx[ueId] = handoverCtx;
        m_handoversPending.erase(txId);

        // not sure if this is still needed, but clean it up anyway
        handoverCtx->handoverInProgress = false;

        // Re-arm measurement reporting on target gNB after handover.
        sendMeasConfig(ueId, true);

        // Notify NGAP of handover completion.
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_NOTIFY);
        w->ueId = ueId;
        m_base->ngapTask->push(std::move(w));

        m_logger->info("Handover completed for UE[%d]. NGAP notification sent.", ueId);
            return;

    }

    // other RRCReconfigComplete msgs

    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
    {
        m_logger->warn("Received RRCReconfigurationComplete from unknown UE[%d], ignoring", ueId);
        return;
    }

    // no gnb action needed for non-handover RRCReconfigurationComplete, just log it

    m_logger->info("RRCReconfigurationComplete received from UE[%d] (txId=%ld)", ueId, txId);

}


/**
 * @brief handles a Measurement Report from a UE
 * 
 */
void GnbRrcTask::receiveMeasurementReport(int ueId,
    const ASN_RRC_MeasurementReport &msg)
{
    auto *ue = findCtxByUeId(ueId);
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
    const auto *sentMeasIdentity = ue->findSentMeasIdentity(measId);
    if (!sentMeasIdentity)
    {
        m_logger->warn("MeasurementReport from UE[%d]: unknown measId=%ld", ueId, measId);
        return;
    }

    // Extract serving cell measurement
    int servingRsrp = MIN_RSRP;
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

    m_logger->info("MeasurementReport from UE[%d]: measId=%ld event=%s servingRSRP=%ddBm",
                   ueId, measId, sentMeasIdentity->eventType.c_str(), servingRsrp);

    // Extract neighbor cell measurements
    if (results.measResultNeighCells)
    {
        if (results.measResultNeighCells->present ==
            ASN_RRC_MeasResults__measResultNeighCells_PR_measResultListNR)
        {
            auto *nrList = results.measResultNeighCells->choice.measResultListNR;
            if (nrList)
            {
                int bestNeighPci = -1;
                int bestNeighRsrp = MIN_RSRP;

                for (int i = 0; i < nrList->list.count; i++)
                {
                    auto *nr = nrList->list.array[i];
                    if (!nr)
                        continue;

                    int pci = nr->physCellId ? static_cast<int>(*nr->physCellId) : -1;
                    int rsrp = MIN_RSRP;
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

                // Store best neighbor info in UE context for potential handover decision
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
    evaluateHandoverDecision(ueId, sentMeasIdentity->eventType);
}

/* ================================================================== */
/*  Send RRCReconfiguration with ReconfigurationWithSync (Handover)   */
/* ================================================================== */

void GnbRrcTask::sendHandoverCommand(int ueId, int targetPci, int newCrnti, int t304Ms)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->err("Cannot send handover command: UE[%d] not found", ueId);
        return;
    }

    m_logger->info("Sending handover command to UE[%d]: targetPCI=%d newC-RNTI=%d t304=%dms",
                   ueId, targetPci, newCrnti, t304Ms);

    int hoCrnti = normalizeCrntiForRrc(newCrnti);
    if (hoCrnti != newCrnti)
    {
        m_logger->warn("Normalizing handover C-RNTI for UE[%d]: %d -> %d", ueId, newCrnti, hoCrnti);
    }

    // ---- Build CellGroupConfig with ReconfigurationWithSync ----
    ASN_RRC_ReconfigurationWithSync rws{};
    rws.newUE_Identity = static_cast<long>(hoCrnti);
    rws.t304 = t304MsToEnum(t304Ms);

    // Set target cell PCI via spCellConfigCommon
    ASN_RRC_ServingCellConfigCommon scc{};
    long pci = static_cast<long>(targetPci);
    scc.physCellId = &pci;
    scc.dmrs_TypeA_Position = ASN_RRC_ServingCellConfigCommon__dmrs_TypeA_Position_pos2;
    scc.ss_PBCH_BlockPower = 0;
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
    ue->handoverNewCrnti = hoCrnti;
    ue->handoverTxId = txId;

    sendRrcMessage(ueId, pdu);

    m_logger->info("RRCReconfiguration (handover) sent to UE[%d] txId=%ld", ueId, txId);
}

/* ================================================================== */
/*  Send MeasConfig to UE (A2/A3/A5, configurable, multi-measId)      */
/* ================================================================== */

void GnbRrcTask::sendMeasConfig(int ueId, bool forceResend)
{
    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
        return;

    if (!forceResend && !ue->sentMeasIdentities.empty())
        return; // already configured

    if (forceResend && !ue->sentMeasIdentities.empty())
    {
        m_logger->info("Forcing MeasConfig resend to UE[%d] after handover", ue->ueId);
        ue->sentMeasIdentities.clear();
    }

    if (m_config->handover.eventTypes.empty())
    {
        m_logger->warn("No handover.eventType configured; skipping MeasConfig for UE[%d]", ue->ueId);
        return;
    }

    std::string eventList{};
    for (size_t i = 0; i < m_config->handover.eventTypes.size(); i++)
    {
        if (i != 0)
            eventList += ",";
        eventList += m_config->handover.eventTypes[i];
    }

    m_logger->info("Sending MeasConfig to UE[%d] with eventType(s)=%s", ue->ueId, eventList.c_str());

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

    // Force clean-slate behavior for simulation stability: UE replaces prior
    // measurement configuration instead of relying on delta persistence.
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->fullConfig = asn::New<long>();
    *ies->nonCriticalExtension->fullConfig =
        ASN_RRC_RRCReconfiguration_v1530_IEs__fullConfig_true;

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

    // ReportConfig list: one ReportConfig per configured handover event type.
    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();
    ue->sentMeasIdentities.clear();

    for (size_t i = 0; i < m_config->handover.eventTypes.size(); i++)
    {
        const auto &eventType = m_config->handover.eventTypes[i];
        const long reportConfigId = static_cast<long>(i + 1);
        const long measId = static_cast<long>(i + 1);

        auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
        rcMod->reportConfigId = reportConfigId;
        rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
        rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

        auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;
        rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
        rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();

        auto *et = rcNR->reportType.choice.eventTriggered;
        if (eventType == "A2")
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA2;
            asn::MakeNew(et->eventId.choice.eventA2);

            auto *a2 = et->eventId.choice.eventA2;
            a2->a2_Threshold.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a2->a2_Threshold.choice.rsrp = rsrpDbmToRange(m_config->handover.a2ThresholdDbm);
            a2->hysteresis = dbToHalfDbSteps(m_config->handover.hysteresisDb);
            a2->timeToTrigger = 4; // TTT enum 4 = 100ms
            a2->reportOnLeave = false;

            // Ask UE to include neighbor measurements in A2 reports where possible.
            et->reportAddNeighMeas = asn::New<long>();
            *et->reportAddNeighMeas = ASN_RRC_EventTriggerConfig__reportAddNeighMeas_setup;

            m_logger->debug("MeasConfig UE[%d] measId=%ld event=A2 threshold=%ddBm hysteresis=%ddB ttt=100ms",
                            ue->ueId, measId, m_config->handover.a2ThresholdDbm,
                            m_config->handover.hysteresisDb);
        }
        else if (eventType == "A5")
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA5;
            asn::MakeNew(et->eventId.choice.eventA5);

            auto *a5 = et->eventId.choice.eventA5;
            a5->a5_Threshold1.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a5->a5_Threshold1.choice.rsrp = rsrpDbmToRange(m_config->handover.a5Threshold1Dbm);
            a5->a5_Threshold2.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a5->a5_Threshold2.choice.rsrp = rsrpDbmToRange(m_config->handover.a5Threshold2Dbm);
            a5->hysteresis = dbToHalfDbSteps(m_config->handover.hysteresisDb);
            a5->timeToTrigger = 4; // TTT enum 4 = 100ms
            a5->reportOnLeave = false;
            a5->useWhiteCellList = false;

            m_logger->debug("MeasConfig UE[%d] measId=%ld event=A5 threshold1=%ddBm threshold2=%ddBm "
                            "hysteresis=%ddB ttt=100ms",
                            ue->ueId, measId, m_config->handover.a5Threshold1Dbm,
                            m_config->handover.a5Threshold2Dbm, m_config->handover.hysteresisDb);
        }
        else
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
            asn::MakeNew(et->eventId.choice.eventA3);

            auto *a3 = et->eventId.choice.eventA3;
            a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
            a3->a3_Offset.choice.rsrp = m_config->handover.a3OffsetDb * 2;
            a3->hysteresis = dbToHalfDbSteps(m_config->handover.hysteresisDb);
            a3->timeToTrigger = 4; // TTT enum 4 = 100ms
            a3->reportOnLeave = false;
            a3->useWhiteCellList = false;

            m_logger->debug("MeasConfig UE[%d] measId=%ld event=A3 offset=%ddB hysteresis=%ddB ttt=100ms",
                            ue->ueId, measId, m_config->handover.a3OffsetDb,
                            m_config->handover.hysteresisDb);
        }

        et->rsType = ASN_RRC_NR_RS_Type_ssb;
        et->reportInterval = ASN_RRC_ReportInterval_ms480;
        et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
        et->reportQuantityCell.rsrp = true;
        et->reportQuantityCell.rsrq = false;
        et->reportQuantityCell.sinr = false;
        et->maxReportCells = 4;

        asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);

        auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
        measIdMod->measId = measId;
        measIdMod->measObjectId = 1;
        measIdMod->reportConfigId = reportConfigId;
        asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);

        ue->sentMeasIdentities.push_back({
            measId,
            measIdMod->measObjectId,
            reportConfigId,
            eventType,
        });
    }

    sendRrcMessage(ue->ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("MeasConfig (%zu measId entries) sent to UE[%d] txId=%ld",
                   ue->sentMeasIdentities.size(), ue->ueId, txId);
}

/**
 * @brief Evaluates whether to trigger a handover based on the latest measurement report from the UE.
 * 
 * @param ueId UE ID of UE providing measurement report 
 */
void GnbRrcTask::evaluateHandoverDecision(int ueId, const std::string &eventType)
{
    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
        return;

    // Don't trigger if handover is already in progress
    if (ue->handoverInProgress || ue->handoverDecisionPending)
        return;

    int bestNeighPci = ue->lastMeasReportPci;
    int bestNeighRsrp = ue->lastMeasReportRsrp;
    int servingRsrp = ue->lastServingRsrp;

    bool shouldHandover = false;
    const int hysteresisDb = m_config->handover.hysteresisDb;

    if (eventType == "A2")
    {
        // A2: serving RSRP < threshold - hysteresis
        shouldHandover = servingRsrp < (m_config->handover.a2ThresholdDbm - hysteresisDb);
        m_logger->debug("HandoverEval UE[%d] event=A2 measId serving=%ddBm threshold=%ddBm hysteresis=%ddB "
                        "condition=(%d < %d) result=%s",
                        ue->ueId, servingRsrp, m_config->handover.a2ThresholdDbm, hysteresisDb,
                        servingRsrp, m_config->handover.a2ThresholdDbm - hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (eventType == "A3")
    {
        // A3: neighbor RSRP > serving RSRP + offset + hysteresis
        shouldHandover = bestNeighPci >= 0 &&
                         bestNeighRsrp > (servingRsrp + m_config->handover.a3OffsetDb + hysteresisDb);
        m_logger->debug("HandoverEval UE[%d] event=A3 serving=%ddBm bestNeighPci=%d bestNeigh=%ddBm "
                        "offset=%ddB hysteresis=%ddB condition=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighPci, bestNeighRsrp,
                        m_config->handover.a3OffsetDb, hysteresisDb,
                        bestNeighRsrp, servingRsrp + m_config->handover.a3OffsetDb + hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (eventType == "A5")
    {
        // A5: serving RSRP < threshold1 - hysteresis AND neighbor RSRP > threshold2 + hysteresis
        shouldHandover = bestNeighPci >= 0 &&
                         servingRsrp < (m_config->handover.a5Threshold1Dbm - hysteresisDb) &&
                         bestNeighRsrp > (m_config->handover.a5Threshold2Dbm + hysteresisDb);
        m_logger->debug("HandoverEval UE[%d] event=A5 serving=%ddBm bestNeighPci=%d bestNeigh=%ddBm "
                        "thr1=%ddBm thr2=%ddBm hysteresis=%ddB cond1=(%d < %d) cond2=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighPci, bestNeighRsrp,
                        m_config->handover.a5Threshold1Dbm, m_config->handover.a5Threshold2Dbm,
                        hysteresisDb,
                        servingRsrp, m_config->handover.a5Threshold1Dbm - hysteresisDb,
                        bestNeighRsrp, m_config->handover.a5Threshold2Dbm + hysteresisDb,
                        shouldHandover ? "true" : "false");
    }

    if (!shouldHandover)
        return;

    // For A2, a target may not be present in this report; use last known neighbor if available.
    if (bestNeighPci < 0)
    {
        m_logger->warn("Handover decision met for UE[%d] event=%s but no neighbor PCI available",
                       ue->ueId, eventType.c_str());
        return;
    }

    m_logger->info("Handover decision (%s): UE[%d] -> targetPCI=%d (serving=%ddBm, target=%ddBm)",
                   eventType.c_str(), ue->ueId, bestNeighPci, servingRsrp, bestNeighRsrp);

    ue->handoverDecisionPending = true;

    // Initiate N2 handover via NGAP (no Xn interface available)
    auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
    w->ueId = ue->ueId;
    w->hoTargetPci = bestNeighPci;
    w->hoCause = NgapCause::RadioNetwork_handover_desirable_for_radio_reason;
    m_base->ngapTask->push(std::move(w));

    m_logger->info("HandoverRequired sent to NGAP for UE[%d] targetPCI=%d", ue->ueId, bestNeighPci);
}

/**
 * @brief Handles a handover command from NGAP, including an RRC container
 * that carries the RRCReconfiguration for handover.
 * 
 * @param ueId 
 * @param rrcContainer 
 */
void GnbRrcTask::handleNgapHandoverCommand(int ueId, const OctetString &rrcContainer)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->warn("handleNgapHandoverCommand: UE[%d] not found", ueId);
        return;
    }

    m_logger->info("Received NGAP Handover Command for UE[%d] rrcContainer=%dB", ueId, rrcContainer.length());

    auto *pdu = rrc::encode::Decode<ASN_RRC_DL_DCCH_Message>(asn_DEF_ASN_RRC_DL_DCCH_Message, rrcContainer);
    if (!pdu)
    {
        m_logger->err("Failed to decode handover RRC container as DL-DCCH for UE[%d]", ueId);
        return;
    }

    bool isReconfig = pdu->message.present == ASN_RRC_DL_DCCH_MessageType_PR_c1 &&
                      pdu->message.choice.c1 != nullptr &&
                      pdu->message.choice.c1->present == ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration &&
                      pdu->message.choice.c1->choice.rrcReconfiguration != nullptr;

    if (!isReconfig)
    {
        m_logger->err("Decoded handover RRC container is not an RRCReconfiguration for UE[%d]", ueId);
        asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
        return;
    }

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("Forwarded decoded handover RRCReconfiguration to UE[%d]", ueId);

}


std::vector<HandoverMeasurementIdentity> GnbRrcTask::getHandoverMeasurementIdentities(int ueId) const
{
    std::vector<HandoverMeasurementIdentity> identities{};

    if (ueId <= 0)
        return identities;

    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end() || !it->second)
        return identities;

    auto *ctx = it->second;
    identities.reserve(ctx->sentMeasIdentities.size());
    for (const auto &item : ctx->sentMeasIdentities)
    {
        identities.push_back({item.measId, item.measObjectId, item.reportConfigId, item.eventType});
    }
    return identities;
}

OctetString GnbRrcTask::getHandoverMeasConfigRrcReconfiguration(int ueId) const
{
    if (ueId <= 0)
        return OctetString{};

    auto it = m_ueCtx.find(ueId);
    const RrcUeContext *ctx = it != m_ueCtx.end() ? it->second : nullptr;

    if (!ctx || ctx->sentMeasIdentities.empty())
        return OctetString{};

    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();
    reconfig->rrc_TransactionIdentifier = 0;
    reconfig->criticalExtensions.present = ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;

    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    // Mark MeasConfig as full replacement to avoid stale measurement state
    // when this configuration is transferred across handover contexts.
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->fullConfig = asn::New<long>();
    *ies->nonCriticalExtension->fullConfig =
        ASN_RRC_RRCReconfiguration_v1530_IEs__fullConfig_true;

    ies->measConfig = asn::New<ASN_RRC_MeasConfig>();
    auto *mc = ies->measConfig;

    mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
    auto *measObj = asn::New<ASN_RRC_MeasObjectToAddMod>();
    measObj->measObjectId = 1;
    measObj->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
    measObj->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
    measObj->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
    *measObj->measObject.choice.measObjectNR->ssbFrequency = 632628;
    measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
    *measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = ASN_RRC_SubcarrierSpacing_kHz30;
    measObj->measObject.choice.measObjectNR->smtc1 = asn::New<ASN_RRC_SSB_MTC>();
    measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
        ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
    measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
    measObj->measObject.choice.measObjectNR->smtc1->duration = ASN_RRC_SSB_MTC__duration_sf1;
    measObj->measObject.choice.measObjectNR->quantityConfigIndex = 1;
    asn::SequenceAdd(*mc->measObjectToAddModList, measObj);

    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();

    for (const auto &item : ctx->sentMeasIdentities)
    {
        auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
        rcMod->reportConfigId = item.reportConfigId;
        rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
        rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

        auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;
        rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
        rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();
        auto *et = rcNR->reportType.choice.eventTriggered;

        if (item.eventType == "A2")
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA2;
            asn::MakeNew(et->eventId.choice.eventA2);
            auto *a2 = et->eventId.choice.eventA2;
            a2->a2_Threshold.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a2->a2_Threshold.choice.rsrp = rsrpDbmToRange(m_config->handover.a2ThresholdDbm);
            a2->hysteresis = dbToHalfDbSteps(m_config->handover.hysteresisDb);
            a2->timeToTrigger = 4;
            a2->reportOnLeave = false;
            et->reportAddNeighMeas = asn::New<long>();
            *et->reportAddNeighMeas = ASN_RRC_EventTriggerConfig__reportAddNeighMeas_setup;
        }
        else if (item.eventType == "A5")
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA5;
            asn::MakeNew(et->eventId.choice.eventA5);
            auto *a5 = et->eventId.choice.eventA5;
            a5->a5_Threshold1.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a5->a5_Threshold1.choice.rsrp = rsrpDbmToRange(m_config->handover.a5Threshold1Dbm);
            a5->a5_Threshold2.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a5->a5_Threshold2.choice.rsrp = rsrpDbmToRange(m_config->handover.a5Threshold2Dbm);
            a5->hysteresis = dbToHalfDbSteps(m_config->handover.hysteresisDb);
            a5->timeToTrigger = 4;
            a5->reportOnLeave = false;
            a5->useWhiteCellList = false;
        }
        else
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
            asn::MakeNew(et->eventId.choice.eventA3);
            auto *a3 = et->eventId.choice.eventA3;
            a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
            a3->a3_Offset.choice.rsrp = m_config->handover.a3OffsetDb * 2;
            a3->hysteresis = dbToHalfDbSteps(m_config->handover.hysteresisDb);
            a3->timeToTrigger = 4;
            a3->reportOnLeave = false;
            a3->useWhiteCellList = false;
        }

        et->rsType = ASN_RRC_NR_RS_Type_ssb;
        et->reportInterval = ASN_RRC_ReportInterval_ms480;
        et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
        et->reportQuantityCell.rsrp = true;
        et->reportQuantityCell.rsrq = false;
        et->reportQuantityCell.sinr = false;
        et->maxReportCells = 4;

        asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);

        auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
        measIdMod->measId = item.measId;
        measIdMod->measObjectId = item.measObjectId;
        measIdMod->reportConfigId = item.reportConfigId;
        asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);
    }

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    return encoded;
}

/**
 * @brief Creates an RRCReconfiguration message sent to the UE as part of
 * handover preparation.
 * Returns the transaction id so it can be inserted into pending handover
 * context and matched when the UE completes handover.
 * 
 * @param ueId 
 * @param targetPci 
 * @param newCrnti 
 * @param t304Ms 
 * @param rrcContainer 
 * @return int64_t 
 */
int64_t GnbRrcTask::buildHandoverCommandForTransfer(int ueId, int targetPci, int newCrnti,
                                                    int t304Ms, OctetString &rrcContainer)
{
    int hoCrnti = normalizeCrntiForRrc(newCrnti);
    if (hoCrnti != newCrnti)
    {
        m_logger->warn("Normalizing target handover C-RNTI for UE[%d]: %d -> %d", ueId, newCrnti, hoCrnti);
    }

    ASN_RRC_ReconfigurationWithSync rws{};
    rws.newUE_Identity = static_cast<long>(hoCrnti);
    rws.t304 = t304MsToEnum(t304Ms);

    ASN_RRC_ServingCellConfigCommon scc{};
    long pci = static_cast<long>(targetPci);
    scc.physCellId = &pci;
    scc.dmrs_TypeA_Position = ASN_RRC_ServingCellConfigCommon__dmrs_TypeA_Position_pos2;
    scc.ss_PBCH_BlockPower = 0;
    rws.spCellConfigCommon = &scc;

    ASN_RRC_SpCellConfig spCellConfig{};
    spCellConfig.reconfigurationWithSync = &rws;

    ASN_RRC_CellGroupConfig cellGroupConfig{};
    cellGroupConfig.cellGroupId = 0;
    cellGroupConfig.spCellConfig = &spCellConfig;

    OctetString masterCellGroupOctet = rrc::encode::EncodeS(asn_DEF_ASN_RRC_CellGroupConfig, &cellGroupConfig);
    if (masterCellGroupOctet.length() == 0)
    {
        m_logger->err("buildHandoverCommandForTransfer: failed CellGroupConfig encode for UE[%d]", ueId);
        return -1;
    }

    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration>();

    long txId = getNextTid();
    for (int attempt = 0; attempt < 4 && m_handoversPending.count(txId); ++attempt)
        txId = getNextTid();

    reconfig->rrc_TransactionIdentifier = txId;
    reconfig->criticalExtensions.present = ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->masterCellGroup = asn::New<OCTET_STRING_t>();
    asn::SetOctetString(*ies->nonCriticalExtension->masterCellGroup, masterCellGroupOctet);

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    if (encoded.length() == 0)
    {
        m_logger->err("buildHandoverCommandForTransfer: failed RRCReconfiguration encode for UE[%d]", ueId);
        return -1;
    }

    m_logger->info("Target generated handover command for UE[%d] txId=%ld targetPCI=%d newC-RNTI=%d", ueId,
                   txId, targetPci, hoCrnti);
    m_logger->debug("buildHandoverCommandForTransfer: encoded RRCReconfiguration size=%dB for UE[%d] txId=%ld",
                    encoded.length(), ueId, txId);

    rrcContainer = std::move(encoded);

    return txId;
}

/**
 * @brief Adds a pending handover context transfer for the specified UE, to be completed when the UE connects
 * after handover. Handover preparation info includes the measurement
 * identities configured for this UE.
 * Also builds the RRCReconfiguration message to be included in the T2S
 * TransparentContainer, which source gNB sends to the UE.
 * 
 * @param ueId 
 * @param handoverPrep
 * @param rrcContainer output parameter for the RRCReconfiguration message to be sent to the UE 
 * @return true - success
 * @return false - failure
 */
bool GnbRrcTask::addPendingHandover(int ueId, const HandoverPreparationInfo &handoverPrep,
                                    OctetString &rrcContainer)
{
    if (ueId <= 0)
        return false;

    // create the new UE RRC context
    auto *ctx = new RrcUeContext(ueId);
    ctx->ueId = ueId;
    ctx->handoverInProgress = true;
    ctx->handoverTargetPci = m_base->config->getCellId();
    ctx->cRnti = allocateCrnti();

    for (const auto &item : handoverPrep.measIdentities)
    {
        ctx->sentMeasIdentities.push_back({item.measId, item.measObjectId, item.reportConfigId,
                                           item.eventType});
    }

    auto it = m_handoversPending.find(static_cast<long>(ueId));
    if (it != m_handoversPending.end() && it->second)
    {
        delete it->second->ctx;
        delete it->second;
    }

    int64_t txId = buildHandoverCommandForTransfer(ueId, ctx->handoverTargetPci, ctx->cRnti, 1000, rrcContainer);
    // the command build fails, no handover context should be added
    if (txId < 0) {
        delete ctx;
        return false;
    }

    // add pending handover context, to be completed when the UE connects after handover
    auto *pending = new RRCHandoverPending();
    pending->id = ueId;
    pending->ctx = ctx;
    pending->txId = txId;
    pending->expireTime = utils::CurrentTimeMillis() + HANDOVER_TIMEOUT_MS;
    m_handoversPending[txId] = pending;

    m_logger->info("Added pending handover transfer for UE[%d] with %zu measurement identities", ueId,
                   handoverPrep.measIdentities.size());
    return true;
}

/**
 * @brief Deletes the UE's context due to a successful handover to the target gNB.
 * 
 * @param ueId 
 */
void GnbRrcTask::handoverContextRelease(int ueId)
{
    auto *ctx = findCtxByUeId(ueId);
    if (ctx)
    {
        delete ctx;
        m_ueCtx.erase(ueId);
        m_logger->info("Released context for UE[%d]", ueId);
        return;
    }

    m_logger->warn("handoverContextRelease: UE[%d] context not found", ueId);
}

} // namespace gnb