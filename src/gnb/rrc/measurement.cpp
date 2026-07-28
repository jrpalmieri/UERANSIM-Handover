/*
*  gNB measurement configuration and reporting
*
* Implements:
*   receiveMeasurementReport()  – parse a UE MeasurementReport, update per-UE state,
*                                 and kick evaluateHandoverDecision() (which lives in handover.cpp)
*   sendMeasConfig()            – build and send the RRCReconfiguration carrying the MeasConfig
*   createMeasConfig()          – construct the MeasConfig IE (basic + conditional events)
*   plus static helpers: clearMeasConfig(), getNewReportConfigId(), getNewMeasId(),
*                        getActiveChoProfileIndices()
*
* (RRCReconfigurationComplete handling lives in reconfiguration.cpp.)
*/


#include "task.hpp"
#include <gnb/xn/task.hpp>

#include <gnb/neighbors.hpp>
#include <gnb/ngap/task.hpp>
#include <gnb/sat_time.hpp>

#include <lib/sat/sat_calc.hpp>
#include <lib/sat/sat_state.hpp>
#include <lib/sat/sat_time.hpp>

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <utils/common.hpp>

#include <algorithm>
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
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1540-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1560-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1610-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1700-IEs.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_CellsToAddMod.h>
#include <asn/rrc/ASN_RRC_CellsToAddModList.h>
#include <asn/rrc/ASN_RRC_SpCellConfig.h>
#include <asn/rrc/ASN_RRC_ReconfigurationWithSync.h>
#include <asn/rrc/ASN_RRC_ServingCellConfigCommon.h>
#include <asn/rrc/ASN_RRC_ConditionalReconfiguration-r16.h>
#include <asn/rrc/ASN_RRC_CondReconfigToAddMod-r16.h>
#include <asn/rrc/ASN_RRC_CondTriggerConfig-r16.h>
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
#include <cmath>
#include <utility>

#include <libsgp4/DateTime.h>

const int MIN_RSRP = cons::MIN_RSRP; // minimum RSRP value (in dBm) to use when no measurement is available
const long DUMMY_MEAS_OBJECT_ID = 1; // dummy MeasObjectId for handover measurement configuration


namespace nr::gnb
{

using HandoverEventType = nr::rrc::common::HandoverEventType;
using nr::rrc::common::ReportConfigEvent;
using nr::rrc::common::MeasObject;
using nr::rrc::common::mtqFromASNValue;
using nr::rrc::common::mtqToASNValue;
using nr::rrc::common::hysteresisToASNValue;
using nr::rrc::common::referenceLocationToAsnValue;
using nr::rrc::common::tttMsToASNValue;
using nr::rrc::common::distanceThresholdToASNValue;
using nr::rrc::common::IsMeasurementEvent;
using nr::rrc::common::IsConditionalEvent;


static long getNewReportConfigId(RrcUeContext *ue)
{
    long id = 1;
    while (true)
    {
        // search vector for id, if not found, break and use it.  
        //  If found, increment and keep searching
        if (std::find(ue->usedReportConfigIds.begin(), ue->usedReportConfigIds.end(), id)            == ue->usedReportConfigIds.end())
        {
            break;
        }
        id++;
    }
    return id;
}


static long getNewMeasId(RrcUeContext *ue)
{
    long id = 1;
    while (true)
    {
        // search vector for id in measId field of MeasIdentityMappings structs, if not found, break and use it.  
        //  If found, increment and keep searching
        auto it = ue->measIdentities.find(id);

        if (it == ue->measIdentities.end())
        {
            break;
        }
        id++;

    }
    return id;
}


static std::vector<int> getActiveChoProfileIndices(
    const GnbHandoverConfig &config, Logger *logger)
{
    std::vector<int> result{};
    if (!config.choEnabled)
        return result;

    for (int id : config.choActiveProfileIds)
    {
        bool found = false;
        for (size_t i = 0; i < config.candidateProfiles.size(); ++i)
        {
            if (config.candidateProfiles[i].candidateProfileId == id)
            {
                result.push_back(static_cast<int>(i));
                found = true;
                break;
            }
        }
        if (!found && logger)
            logger->warn("CHO active profile id=%d not found in candidateProfiles; skipping.", id);
    }

    if (result.empty() && logger)
        logger->warn("No active CHO profiles resolved from choActiveProfileIds.");

    return result;
}

// clear all MeasConfig-related state from the UE context
static void clearMeasConfig(RrcUeContext *ue)
{
    ue->measObjects.clear();
    ue->reportConfigEvents.clear();
    ue->measIdentities.clear();

    // clear all the ID trackers
    ue->usedMeasObjectIds.clear();
    ue->usedReportConfigIds.clear();
    //ue->usedMeasIdentities.clear();

}


/**
 * @brief handles a Measurement Report from a UE
 * 
 */
void GnbRrcTask::receiveMeasurementReport(int64_t ueId, int cRnti,
    const ASN_RRC_MeasurementReport &msg)
{
    int64_t resolvedUeId = ueId;

    auto *ue = findCtxByUeId(resolvedUeId);

    if (!ue)
    {
        m_logger->warn("UE[%ld] MeasurementReport from unknown UE, discarding.", resolvedUeId);
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

    int measId = static_cast<int>(results.measId);

    auto sentMeasIdentityPair = ue->measIdentities.find(measId);
    if (sentMeasIdentityPair == ue->measIdentities.end())
    {
        m_logger->warn("UE[%ld] MeasurementReport unknown measId=%d", resolvedUeId, measId);
        return;
    }
    auto sentMeasIdentity = sentMeasIdentityPair->second;

    auto sentReportConfigPair = ue->reportConfigEvents.find(sentMeasIdentity.reportConfigId);
    if (sentReportConfigPair == ue->reportConfigEvents.end())
    {
        m_logger->warn("UE[%ld] MeasurementReport measId=%d references unknown reportConfigId=%ld, discarding.",
                       resolvedUeId, measId, sentMeasIdentity.reportConfigId);
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
                servingRsrp = mtqFromASNValue(*ssbCell->rsrp);
        }
    }

    auto event_str = nr::rrc::common::HandoverEventTypeToString(sentReportConfigPair->second.eventKind);
    m_logger->info("UE[%ld] MeasurementReport measId=%d event=%s servingRSRP=%ddBm",
                   resolvedUeId, measId, event_str.c_str(), servingRsrp);

    // Extract neighbor cell measurements
    if (results.measResultNeighCells)
    {
        if (results.measResultNeighCells->present ==
            ASN_RRC_MeasResults__measResultNeighCells_PR_measResultListNR)
        {
            auto *nrList = results.measResultNeighCells->choice.measResultListNR;
            if (nrList)
            {
                int64_t bestNeighNci = -1;
                int bestNeighRsrp = MIN_RSRP;

                for (int i = 0; i < nrList->list.count; i++)
                {
                    auto *nr = nrList->list.array[i];
                    if (!nr)
                        continue;

                    int64_t nci = nr->physCellId ? static_cast<int64_t>(*nr->physCellId) : -1;
                    int rsrp = MIN_RSRP;
                    auto *ssbCell = nr->measResult.cellResults.resultsSSB_Cell;
                    if (ssbCell && ssbCell->rsrp)
                        rsrp = mtqFromASNValue(*ssbCell->rsrp);

                    m_logger->debug("  Neighbor NCI=%ld RSRP=%ddBm", nci, rsrp);

                    if (rsrp > bestNeighRsrp)
                    {
                        bestNeighRsrp = rsrp;
                        bestNeighNci = nci;
                    }
                }

                // Store best neighbor info in UE context for potential handover decision
                if (bestNeighNci >= 0)
                {
                    ue->lastMeasReportNci = bestNeighNci;
                    ue->lastMeasReportRsrp = bestNeighRsrp;
                    m_logger->info("Best neighbor: NCI=%ld RSRP=%ddBm (serving=%ddBm)",
                                   bestNeighNci, bestNeighRsrp, servingRsrp);
                }
            }
        }
    }

    // Update serving RSRP and evaluate handover decision
    ue->lastServingRsrp = servingRsrp;
    evaluateHandoverDecision(resolvedUeId, measId);
}





/**
 * @brief Creates a MeasConfig IE for inclusion in an RRCReconfiguration message.  Supports both
 * basic measurement events and conditional handover events.
 * 
 * @param[in/out] mc pointer to the created MeasConfig.
 * @param[in] ue The UE RRC context
 * @param[in] taggedEvents The list of events to include in the MeasConfig
 * @return vector of MeasId values used in the created MeasConfig, for tracking in UE context 
 */
std::vector<long> GnbRrcTask::createMeasConfig(
    ASN_RRC_MeasConfig *&mc,
    RrcUeContext *ue,
    std::vector<std::pair<ReportConfigEvent, int>> taggedEvents
)
{
    std::vector<long> usedMeasIds{};

    // Build MeasConfig
    mc = asn::New<ASN_RRC_MeasConfig>();

    // Build MeasObject
    //    Since RF layers not implemented, this is a dummy object
    //   only include this if we haven't already sent it to the UE
    if (ue->usedMeasObjectIds.empty())
    {
        MeasObject measObj;
        measObj.measObjectId = DUMMY_MEAS_OBJECT_ID;

        mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
        auto *measObjAsn = asn::New<ASN_RRC_MeasObjectToAddMod>();
        measObjAsn->measObjectId = DUMMY_MEAS_OBJECT_ID;
        measObjAsn->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
        measObjAsn->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
        measObjAsn->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
        *measObjAsn->measObject.choice.measObjectNR->ssbFrequency = 632628; // typical n78 SSB freq
        measObjAsn->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
        *measObjAsn->measObject.choice.measObjectNR->ssbSubcarrierSpacing = ASN_RRC_SubcarrierSpacing_kHz30;

        // smtc1 (SSB MTC periodicity/offset/duration) - required for NR measObject.
        measObjAsn->measObject.choice.measObjectNR->smtc1 = asn::New<ASN_RRC_SSB_MTC>();
        measObjAsn->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
            ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
        measObjAsn->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
        measObjAsn->measObject.choice.measObjectNR->smtc1->duration = ASN_RRC_SSB_MTC__duration_sf1;
        // quantityConfigIndex is mandatory INTEGER (1..maxNrofQuantityConfig); must be >= 1.
        measObjAsn->measObject.choice.measObjectNR->quantityConfigIndex = 1;
        
        asn::SequenceAdd(*mc->measObjectToAddModList, measObjAsn);

        // store in UE context
        ue->measObjects.emplace(DUMMY_MEAS_OBJECT_ID, measObj);
    }

    // ReportConfig list: one ReportConfig per configured handover event type.
    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();
    
    for (size_t i = 0; i < taggedEvents.size(); i++)
    {
        const auto &event = taggedEvents[i].first;
        const int eventChoProfileId = taggedEvents[i].second;
        bool cho_event = !IsMeasurementEvent(event.eventKind);
        const auto eventKind = event.eventKind;
        const auto eventType = nr::rrc::common::HandoverEventTypeToString(eventKind);
        // pull a ReportConfigId value that does not conflict with existing active ReportConfigIds
        const long reportConfigId = getNewReportConfigId(ue);
        // pull a MeasId value that does not conflict with existing active MeasIds 
        const long measId = getNewMeasId(ue);

        auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
        rcMod->reportConfigId = reportConfigId;
        rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
        rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

        auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;

        ReportConfigEvent reportConfig;
        reportConfig.reportConfigId = reportConfigId;
        reportConfig.eventKind = eventKind;

        // normal "eventTriggered" events
        if (!cho_event)
        {
            rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
            rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();
            auto *et = rcNR->reportType.choice.eventTriggered;

            if (eventKind == HandoverEventType::A2)
            {
                et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA2;
                asn::MakeNew(et->eventId.choice.eventA2);

                auto *a2 = et->eventId.choice.eventA2;
                a2->a2_Threshold.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
                a2->a2_Threshold.choice.rsrp = mtqToASNValue(event.a2_thresholdDbm);
                a2->hysteresis = hysteresisToASNValue(event.a2_hysteresisDb);
                a2->timeToTrigger = tttMsToASNValue(event.ttt);
                a2->reportOnLeave = true;

                // Ask UE to include neighbor measurements in A2 reports where possible.
                et->reportAddNeighMeas = asn::New<long>();
                *et->reportAddNeighMeas = ASN_RRC_EventTriggerConfig__reportAddNeighMeas_setup;

                reportConfig.a2_hysteresisDb = event.a2_hysteresisDb;
                reportConfig.a2_thresholdDbm = event.a2_thresholdDbm;
                reportConfig.ttt = event.ttt;

                m_logger->debug("UE[%ld] MeasConfig measId=%ld event=A2 threshold=%ddBm hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.a2_thresholdDbm, event.a2_hysteresisDb, E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::A3)
            {
                et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
                asn::MakeNew(et->eventId.choice.eventA3);

                auto *a3 = et->eventId.choice.eventA3;
                a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
                a3->a3_Offset.choice.rsrp = nr::rrc::common::mtqOffsetToASNValue(event.a3_offsetDb);
                a3->hysteresis = hysteresisToASNValue(event.a3_hysteresisDb);
                a3->timeToTrigger = tttMsToASNValue(event.ttt);
                a3->reportOnLeave = true;
                a3->useAllowedCellList = false;

                reportConfig.a3_hysteresisDb = event.a3_hysteresisDb;
                reportConfig.a3_offsetDb = event.a3_offsetDb;
                reportConfig.ttt = event.ttt;

                m_logger->debug("UE[%ld] MeasConfig measId=%ld event=A3 offset=%ddB hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.a3_offsetDb, event.a3_hysteresisDb, nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::A5)
            {
                et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA5;
                asn::MakeNew(et->eventId.choice.eventA5);

                auto *a5 = et->eventId.choice.eventA5;
                a5->a5_Threshold1.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
                a5->a5_Threshold1.choice.rsrp = mtqToASNValue(event.a5_threshold1Dbm);
                a5->a5_Threshold2.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
                a5->a5_Threshold2.choice.rsrp = mtqToASNValue(event.a5_threshold2Dbm);
                a5->hysteresis = hysteresisToASNValue(event.a5_hysteresisDb);
                a5->timeToTrigger = tttMsToASNValue(event.ttt);
                a5->reportOnLeave = true;
                a5->useAllowedCellList = false;

                reportConfig.a5_threshold1Dbm = event.a5_threshold1Dbm;
                reportConfig.a5_threshold2Dbm = event.a5_threshold2Dbm;
                reportConfig.a5_hysteresisDb = event.a5_hysteresisDb;
                reportConfig.ttt = event.ttt;

                m_logger->debug("UE[%ld] MeasConfig measId=%ld event=A5 threshold1=%ddBm threshold2=%ddBm "
                                "hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.a5_threshold1Dbm,
                                event.a5_threshold2Dbm, event.a5_hysteresisDb, nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::D1)
            {
                et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17;
                asn::MakeNew(et->eventId.choice.eventD1_r17);

                auto *d1 = et->eventId.choice.eventD1_r17;

                d1->distanceThreshFromReference1_r17 =
                    distanceThresholdToASNValue(event.d1_distanceThreshFromReference1);
                d1->distanceThreshFromReference2_r17 =
                    distanceThresholdToASNValue(event.d1_distanceThreshFromReference2);

                referenceLocationToAsnValue(event.d1_referenceLocation1, d1->referenceLocation1_r17);
                referenceLocationToAsnValue(event.d1_referenceLocation2, d1->referenceLocation2_r17);
                d1->hysteresisLocation_r17 = nr::rrc::common::hysteresisLocationToASNValue(event.d1_hysteresisLocation);

                d1->reportOnLeave_r17 = true;
                d1->timeToTrigger_r17 = tttMsToASNValue(event.ttt);

                reportConfig.d1_distanceThreshFromReference1 = event.d1_distanceThreshFromReference1;
                reportConfig.d1_distanceThreshFromReference2 = event.d1_distanceThreshFromReference2;
                reportConfig.d1_referenceLocation1 = event.d1_referenceLocation1;
                reportConfig.d1_referenceLocation2 = event.d1_referenceLocation2;
                reportConfig.d1_hysteresisLocation = event.d1_hysteresisLocation;
                reportConfig.ttt = event.ttt;

                m_logger->debug("UE[%ld] MeasConfig measId=%ld event=D1 distThresh1=%dm distThresh2=%dm "
                                "hysteresis=%dm "
                                "ttt=%s",
                                ue->ueId,
                                measId,
                                reportConfig.d1_distanceThreshFromReference1,
                                reportConfig.d1_distanceThreshFromReference2,
                                reportConfig.d1_hysteresisLocation,
                                nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else 
            {
                m_logger->warn("UE[%ld] Unsupported event type %s, skipping", ue->ueId, eventType.c_str());
                continue;
            }
        
        
            et->rsType = ASN_RRC_NR_RS_Type_ssb;
            et->reportInterval = ASN_RRC_ReportInterval_ms480;
            et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;

            // only support RSRP since this just simulated
            et->reportQuantityCell.rsrp = true;
            et->reportQuantityCell.rsrq = false;
            et->reportQuantityCell.sinr = false;
            et->maxReportCells = 4;
        }

        // conditional events
        else
        {
            rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_condTriggerConfig_r16;
            rcNR->reportType.choice.condTriggerConfig_r16 = asn::New<ASN_RRC_CondTriggerConfig_r16>();
            auto *ctc = rcNR->reportType.choice.condTriggerConfig_r16;

            if (eventKind == HandoverEventType::CondA3)
            {

                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventA3;
                asn::MakeNew(ctc->condEventId.choice.condEventA3);

                auto *a3 = ctc->condEventId.choice.condEventA3;
                a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
                a3->a3_Offset.choice.rsrp = nr::rrc::common::mtqOffsetToASNValue(event.a3_offsetDb);
                a3->hysteresis = hysteresisToASNValue(event.a3_hysteresisDb);
                a3->timeToTrigger = tttMsToASNValue(event.ttt);

                reportConfig.condA3_hysteresisDb = event.condA3_hysteresisDb;
                reportConfig.condA3_offsetDb = event.condA3_offsetDb;
                reportConfig.ttt = event.ttt;

                m_logger->debug("UE[%ld] MeasConfig measId=%ld event=condEventA3 offset=%ddB hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.condA3_offsetDb, event.condA3_hysteresisDb, nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }

            else if (eventKind == HandoverEventType::CondD1)
            {
                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventD1_r17;
                asn::MakeNew(ctc->condEventId.choice.condEventD1_r17);

                auto *d1 = ctc->condEventId.choice.condEventD1_r17;

                // set trigger params from provided Trigger object
                d1->distanceThreshFromReference1_r17 = distanceThresholdToASNValue(event.condD1_distanceThreshFromReference1);
                d1->distanceThreshFromReference2_r17 = distanceThresholdToASNValue(event.condD1_distanceThreshFromReference2);
                referenceLocationToAsnValue(event.condD1_referenceLocation1, d1->referenceLocation1_r17);
                referenceLocationToAsnValue(event.condD1_referenceLocation2, d1->referenceLocation2_r17);
                d1->hysteresisLocation_r17 = nr::rrc::common::hysteresisLocationToASNValue(event.condD1_hysteresisLocation);

                // use config for ttt
                d1->timeToTrigger_r17 = tttMsToASNValue(event.ttt);

                reportConfig.condD1_distanceThreshFromReference1 = event.condD1_distanceThreshFromReference1;
                reportConfig.condD1_distanceThreshFromReference2 = event.condD1_distanceThreshFromReference2;
                reportConfig.condD1_referenceLocation1 = event.condD1_referenceLocation1;
                reportConfig.condD1_referenceLocation2 = event.condD1_referenceLocation2;
                reportConfig.condD1_hysteresisLocation = event.condD1_hysteresisLocation;
                reportConfig.ttt = event.ttt;
                
                m_logger->debug("UE[%ld] MeasConfig measId=%ld event=condEventD1 distThresh1=%dm distThresh2=%dm "
                                "hysteresis=%dm "
                                "ttt=%s",
                                ue->ueId,
                                measId,
                                reportConfig.condD1_distanceThreshFromReference1,
                                reportConfig.condD1_distanceThreshFromReference2,
                                reportConfig.condD1_hysteresisLocation,
                                nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::CondT1)
            {
                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventT1_r17;
                asn::MakeNew(ctc->condEventId.choice.condEventT1_r17);

                auto *t1 = ctc->condEventId.choice.condEventT1_r17;
                // set trigger params from provided Trigger object
                // t1-Threshold-r17 is INTEGER (0..549755813887), which asn1c represents as an
                // arbitrary-precision INTEGER_t rather than a native long.
                asn::SetUnsigned64(
                    static_cast<uint64_t>(nr::rrc::common::t1ThresholdToASNValue(event.condT1_thresholdSecTS)),
                    t1->t1_Threshold_r17);
                t1->duration_r17 = nr::rrc::common::durationToASNValue(event.condT1_durationSec);

                reportConfig.condT1_thresholdSecTS = event.condT1_thresholdSecTS;
                reportConfig.condT1_durationSec = event.condT1_durationSec;

                m_logger->debug("UE[%ld] MeasConfig measId=%ld event=condEventT1 threshold=%dsec duration=%dsec",
                                ue->ueId, measId, reportConfig.condT1_thresholdSecTS, reportConfig.condT1_durationSec);
            }
            else
            {
                m_logger->warn("UE[%ld] Unsupported conditional event type %s, skipping", ue->ueId, eventType.c_str());
                continue;
            }

            
        }

        asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);

        ue->reportConfigEvents.emplace(reportConfigId, reportConfig);

        // Create the MeasId

        auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
        measIdMod->measId = measId;
        measIdMod->measObjectId = DUMMY_MEAS_OBJECT_ID; // the dummy NR measObject we defined above
        measIdMod->reportConfigId = reportConfigId;
        asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);

        // update ue's MeasConfig trackers
        ue->measIdentities[measId] = {measId, DUMMY_MEAS_OBJECT_ID, reportConfigId, eventKind, eventType,
                                      cho_event ? static_cast<long>(eventChoProfileId) : -1L};

        usedMeasIds.push_back(measId);

        ue->usedReportConfigIds.push_back(reportConfigId);
        ue->usedMeasObjectIds.push_back(measIdMod->measObjectId);
    }

    return usedMeasIds;

}


/**
 * @brief Sends measurement config to UE using RRCReconfiguration message.
 * If forceResend is false, will only send if UE doesn't already have measurement identities configured;
 * if true, will resend even if UE already has measurements configured 
 * (e.g., to restart measurements after handover).
 * 
 * @param ueId 
 * @param forceResend 
 */
void GnbRrcTask::sendMeasConfig(int64_t ueId, bool forceResend)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
        return;

    if (!forceResend && !ue->measIdentities.empty())
        return; // already configured

    if (forceResend && !ue->measIdentities.empty())
    {
        m_logger->info("UE[%ld] Forcing MeasConfig resend after handover", ue->ueId);
        clearMeasConfig(ue);
    }

    // Resolve basic (non-CHO) measurement events from basicHandoverMeasIdentities
    std::vector<ReportConfigEvent> basicEvents{};
    for (int evId : m_config->handover.basicHandoverMeasIdentities)
    {
        auto it = m_config->handover.eventsById.find(evId);
        if (it == m_config->handover.eventsById.end())
        {
            m_logger->warn("UE[%ld] basicHandoverMeasIdentities: eventId=%d not found in events; skipping", ue->ueId, evId);
            continue;
        }
        if (!IsMeasurementEvent(it->second.eventKind))
        {
            m_logger->warn("UE[%ld] basicHandoverMeasIdentities: eventId=%d type=%s is not a measurement event; skipping",
                           ue->ueId, evId, it->second.eventStr());
            continue;
        }
        basicEvents.push_back(it->second);
    }
    if (basicEvents.empty())
        m_logger->warn("UE[%ld] No basic handover measurement events configured", ue->ueId);

    // Resolve CHO condition events from active profile conditionEventIds
    bool do_cho = m_config->handover.choEnabled;
    std::vector<int> activeChoProfileIndices{};
    if (do_cho)
    {
        activeChoProfileIndices = getActiveChoProfileIndices(m_config->handover, m_logger.get());
        if (activeChoProfileIndices.empty())
        {
            m_logger->warn("UE[%ld] CHO enabled but no active CHO profiles resolved; skipping CHO", ue->ueId);
            do_cho = false;
        }
    }

    // Build tagged event list: basic events use profile index -1; each CHO profile's
    // conditions carry that profile's index so measIdentities can be keyed per-profile.
    std::vector<std::pair<ReportConfigEvent, int>> taggedEvents{};
    for (const auto &ev : basicEvents)
        taggedEvents.emplace_back(ev, -1);
    if (do_cho)
    {
        for (int profileIdx : activeChoProfileIndices)
        {
            const auto &choProfile = m_config->handover.candidateProfiles[profileIdx];
            if (choProfile.conditionEventIds.empty())
            {
                m_logger->warn("UE[%ld] CHO profile idx=%d has no condition event IDs; skipping it", ue->ueId, profileIdx);
                continue;
            }
            for (int evId : choProfile.conditionEventIds)
            {
                auto it = m_config->handover.eventsById.find(evId);
                if (it == m_config->handover.eventsById.end())
                {
                    m_logger->warn("UE[%ld] CHO profile idx=%d: conditionEventId=%d not found in events; skipping",
                                   ue->ueId, profileIdx, evId);
                    continue;
                }
                if (!IsConditionalEvent(it->second.eventKind))
                {
                    m_logger->warn("UE[%ld] CHO profile idx=%d: conditionEventId=%d type=%s is not a conditional event; skipping",
                                   ue->ueId, profileIdx, evId, it->second.eventStr());
                    continue;
                }
                taggedEvents.emplace_back(it->second, profileIdx);
            }
        }
    }

    if (taggedEvents.empty())
    {
        m_logger->warn("UE[%ld] No handover events to configure after resolving IDs, exiting MeasConfig", ue->ueId);
        return;
    }

    bool need_location = false;
    bool need_time = false;
    std::string eventList{};
    for (size_t i = 0; i < taggedEvents.size(); i++)
    {
        const auto &ev = taggedEvents[i].first;
        if (ev.eventKind == HandoverEventType::CondD1 || ev.eventKind == HandoverEventType::D1)
            need_location = true;

        if (ev.eventKind == HandoverEventType::CondT1)
            need_time = true;

        if (i != 0)
            eventList += ",";
        eventList += std::to_string(ev.eventId) + "-" + nr::rrc::common::HandoverEventTypeToString(ev.eventKind);
    }

    m_logger->info("UE[%ld] Creating MeasConfig, eventType(s)=%s", ue->ueId, eventList.c_str());

    // Determine handover trigger conditions

    // Here is where we call the satellite position calculations to determine the
    // time and distance when this gnb will be out of range of teh UE, and use that to set the tServiceSec and the distance threshold for D1.
    // We need this now to determine the time to use for evaluating which gnbs are in range

    const int64_t ownNci = m_config->nci;

    nr::rrc::common::DynamicEventTriggerParams dynTriggerParams{};
    // only run the dynamic parameters if we need location of time triggers
    //   and in NTN mode
    if ((need_location || need_time) && m_config->ntn.ntnEnabled)
    {
        satHandoverTriggerCalc(dynTriggerParams, ownNci, ue);
    }

    // update each event with calculated values as needed
    for (auto &[event, profileIdx] : taggedEvents)
    {
        switch (event.eventKind)
        {
            case HandoverEventType::A2:{
                break;
            }
            case HandoverEventType::A3: {
                break;
            }
            case HandoverEventType::A5: {
                break;
            }
            case HandoverEventType::D1: {
                // in satellite mode, use the calculated distance values
                if (m_config->ntn.ntnEnabled) {
                    event.d1_distanceThreshFromReference1 = dynTriggerParams.d1_distanceThresholdFromReference1;
                    event.d1_distanceThreshFromReference2 = dynTriggerParams.d1_distanceThresholdFromReference2;
                    event.d1_referenceLocation1 = dynTriggerParams.d1_referenceLocation1;
                    event.d1_referenceLocation2 = dynTriggerParams.d1_referenceLocation2;
                    event.d1_hysteresisLocation = dynTriggerParams.d1_hysteresisLocation;
                }
                // in terrestrial mode, just use event's provided values
                break;
            }
            case HandoverEventType::CondA3:{
                break;
            }
            case HandoverEventType::CondD1: {
                // in NTN-mode, use calculated values
                if (m_config->ntn.ntnEnabled) {
                    event.condD1_distanceThreshFromReference1 = dynTriggerParams.condD1_distanceThresholdFromReference1;
                    event.condD1_distanceThreshFromReference2 = dynTriggerParams.condD1_distanceThresholdFromReference2;
                    event.condD1_referenceLocation1 = dynTriggerParams.condD1_referenceLocation1;
                    event.condD1_referenceLocation2 = dynTriggerParams.condD1_referenceLocation2;
                    event.condD1_hysteresisLocation = dynTriggerParams.condD1_hysteresisLocation;
                }
                // in terrestrial mode, just use event's provided values
                break;
            }
            case HandoverEventType::CondT1:{
                // in NTN-mode, use calculated values
                if (m_config->ntn.ntnEnabled) {
                    event.condT1_thresholdSecTS = dynTriggerParams.condT1_thresholdSec;
                    event.condT1_durationSec = dynTriggerParams.condT1_durationSec;
                }
                // in terrestrial mode, use event value as offset to current time
                else {
                    // for timer, use config values as deltas, add to current time
                    event.condT1_thresholdSecTS += utils::CurrentTimeMillis() / 1000;
                }
                break;
            }

        }
    }

    // Build RRCReconfiguration
    auto *pdu = makeRrcReconfiguration(ueId);

    // error check
    if (!pdu) {
        m_logger->err("UE[%ld] Failed to create RRCReconfiguration", ueId);
        return;
    }

    long txId = pdu->message.choice.c1->choice.rrcReconfiguration->rrc_TransactionIdentifier;
    auto *ies = pdu->message.choice.c1->choice.rrcReconfiguration->criticalExtensions.choice.rrcReconfiguration;

    // Force clean-slate behavior for simulation stability: UE replaces prior
    // measurement configuration instead of relying on delta persistence.
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->fullConfig = asn::New<long>();
    *ies->nonCriticalExtension->fullConfig =
        ASN_RRC_RRCReconfiguration_v1530_IEs__fullConfig_true;

    clearMeasConfig(ue);

    // Build MeasConfig
    ASN_RRC_MeasConfig *mc = nullptr;
    auto usedMeasIds = createMeasConfig(mc, ue, taggedEvents);
    ies->measConfig = mc;
    
    // send the measConfig now, don't wait for CHO conditionals since they come from other GNBs
    sendRrcMessage(ue->ueId, pdu);

    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("UE[%ld] RRCReconfiguration sent with MeasConfig (%zu measId entries), txId=%ld",
                   ue->ueId, usedMeasIds.size(), txId);

    // if conditional handover enabled, kick off CHO preparation for each active profile
    if (do_cho)
    {
        for (int profileIdx : activeChoProfileIndices)
            processConditionalHandover(ue->ueId, dynTriggerParams, profileIdx);
    }
}




} // namespace nr::gnb
