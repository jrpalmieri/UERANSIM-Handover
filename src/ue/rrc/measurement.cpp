//
// UE RRC Measurement evaluation (A2, A3, A5) and MeasurementReport generation.
//

#include "task.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/sat_time.hpp>

#include <asn/rrc/ASN_RRC_MeasurementReport.h>
#include <asn/rrc/ASN_RRC_MeasurementReport-IEs.h>
#include <asn/rrc/ASN_RRC_MeasResults.h>
#include <asn/rrc/ASN_RRC_MeasResultNR.h>
#include <asn/rrc/ASN_RRC_MeasResultListNR.h>
#include <asn/rrc/ASN_RRC_MeasResultServMOList.h>
#include <asn/rrc/ASN_RRC_MeasResultServMO.h>
#include <asn/rrc/ASN_RRC_MeasQuantityResults.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-MessageType.h>

#include <ue/meas/meas_provider.hpp>

const int MIN_RSRP = cons::MIN_RSRP; // minimum RSRP value (in dBm) to use when no measurement is available
const int MAX_RSRP = cons::MAX_RSRP; // maximum RSRP value (in dBm) to use when no measurement is available

namespace nr::ue
{

/* ================================================================== */
/*  JSON serialisation helpers for measurement types                  */
/* ================================================================== */


Json ToJson(const UeMeasConfig &v)
{
    auto arr = Json::Arr({});
    for (auto &[id, rc] : v.reportConfigs)
        arr.push(rc.toJson());
    return Json::Obj({
        {"reportConfigs", std::move(arr)},
        {"numMeasObjects", static_cast<int32_t>(v.measObjects.size())},
        {"numMeasIds", static_cast<int32_t>(v.measIds.size())},
    });
}


Json ToJson(const ChoCandidate &v)
{
    return Json::Obj({
        {"candidateId", v.candidateId},
        {"targetPci", v.targetPci},
        {"newCRNTI", v.newCRNTI},
        {"t304Ms", v.t304Ms},
        {"txId", v.txId},
        {"executionPriority", v.executionPriority},
        {"executed", v.executed},
    });
}



/**
 * @brief Gets the RSRP of the serving cell from a map of all measurements.
 * 
 * @param allMeas The map of all cell measurements.
 * @return (int) The RSRP of the serving cell, or MIN_RSRP if no serving cell is found.
 */
int UeRrcTask::getServingCellRsrp(int servingCellId, const std::map<int, int> &allMeas) const
{
    if (servingCellId != 0) 
    {
        // find serving cell ID in the set
        auto it = std::find_if(allMeas.begin(), allMeas.end(), [servingCellId](const std::pair<int, int>& p) {
            return p.first == servingCellId;
        });

        if (it != allMeas.end())
            return it->second; // return the RSRP value for the serving cell

        return MIN_RSRP; // no serving cell found

    }
    
    return MIN_RSRP; // no serving cell
}


/**
 * @brief Evaluate measurement events (A2, A3, A5) based on the current measurements 
 *  and configured thresholds.  Current measurement are stored in the global task context
 *  (m_base->cellDbMeas) and protected by a mutex. Configured thresholds are stored in the RRC task's
 *  measurement configuration (m_measConfig) which is updated by the RRC Reconfiguration procedure. 
 *  If an event's conditions are satisfied for long enough (time-to-trigger), then a MeasurementReport
 *  is generated and sent to the gnb.
 * 
 */
void UeRrcTask::evaluateMeasurements(int servingCellId, const std::map<int, int> &allMeas)
{
    // only do measurement evaluation if we're in RRC_CONNECTED 
    //  and have some measIds configured 
    if (m_state != ERrcState::RRC_CONNECTED)
        return;

    // skip evaluation while measurements are suspended (during handover)
    if (m_measurementEvalSuspended)
        return;

    // if no measIds are configured, skip evaluation
    if (m_measConfig.measIds.empty())
        return;

    // get the serving cell id and RSRP for easier reference in loops below
    int servingRsrp = getServingCellRsrp(servingCellId, allMeas);

    // current time - depends on whether this in NTN mode or not
    int64_t now;
    if (m_base->config->ntn.ntnEnabled)
    {
        now = m_base->satTime != nullptr
              ? m_base->satTime->CurrentSatTimeMillis()
              : utils::CurrentTimeMillis();
    }
    else {
        now = utils::CurrentTimeMillis();
    }

    m_logger->debug("evaluateMeasurements: servingCell=%d servingRsrp=%d dBm, allMeas.size=%zu sat-time=%lld",
         servingCellId, servingRsrp, allMeas.size(), now);

    // loop through all configured measIds and evaluate their conditions against 
    //  the current measurements
    for (auto &[measId, mid] : m_measConfig.measIds)
    {
        auto rcIt = m_measConfig.reportConfigs.find(mid.reportConfigId);
        if (rcIt == m_measConfig.reportConfigs.end())
            continue;

        auto &rc = rcIt->second;
        auto &state = m_measConfig.measIdStates[measId];

        if (state.isReported)
            continue; // already reported for this config (one-shot)

        bool eventSatisfied = false;


        // check for each event type, and evaluate according to the relevant conditions
        switch (rc.eventKind)
        {
        case nr::rrc::common::HandoverEventType::A2:
        {
            // A2 - just compare serving cell rsrp to threshold
            eventSatisfied = rc.evaluateA2(servingRsrp);
            m_logger->debug("[MeasId=%d] evaluating %s: servingRsrp=%lld, threshold=%d, satisfied=%s",
                measId, rc.eventStr(), servingRsrp, rc.a2_thresholdDbm + rc.a2_hysteresisDb,
                eventSatisfied ? "true" : "false");

            break;
        }
        case nr::rrc::common::HandoverEventType::A3:
        {
            // A3 - compare each neighbor cell's RSRP to the serving cell RSRP + offset
            for (auto cm : allMeas)
            {
                if (cm.first == servingCellId)
                    continue;
                // log each neighbor check
                m_logger->debug("A3 check cid=%d rsrp=%d (serving=%d offset=%d hyst=%d)",
                                cm.first, cm.second, servingRsrp, rc.a3_offsetDb, rc.a3_hysteresisDb);
                if (rc.evaluateA3Cell(servingRsrp, cm.second))
                {
                    state.triggeredNeighbors.push_back({cm.first, cm.second});
                    eventSatisfied = true;
                    m_logger->debug("[MeasId=%d] evaluating %s: servingRsrp=%lld, cell%dRsrp=%d, satisfied=%s",
                        measId, rc.eventStr(), servingRsrp, cm.first, cm.second + rc.a3_offsetDb + rc.a3_hysteresisDb,
                        eventSatisfied ? "true" : "false");
                }
            }
            break;
        }
        case nr::rrc::common::HandoverEventType::A5:
        {
            bool servCond = rc.evaluateA5Serving(servingRsrp);
            if (servCond)
            {
                for (auto cm : allMeas)
                {
                    if (cm.first == servingCellId)
                        continue;
                    if (rc.evaluateA5Neighbor(cm.second))
                    {
                        state.triggeredNeighbors.push_back({cm.first, cm.second});
                        eventSatisfied = true;
                    }
                }
            }
            break;
        }
        case nr::rrc::common::HandoverEventType::D1:
        {
            eventSatisfied = rc.evaluateD1(m_base->UeLocation);

            m_logger->debug("[MeasId=%d] evaluating %s: UE location (lat=%.4f, long=%.4f), ref1 (lat=%.4f, long=%.4f), ref2 (lat=%.4f, long=%.4f), distanceThresh1=%d, distanceThresh2=%d, hysteresisLocation=%d, satisfied=%s",
                measId, rc.eventStr(), m_base->UeLocation.latitude, m_base->UeLocation.longitude,
                rc.d1_referenceLocation1.latitudeDeg, rc.d1_referenceLocation1.longitudeDeg,
                rc.d1_referenceLocation2.latitudeDeg, rc.d1_referenceLocation2.longitudeDeg,
                rc.d1_distanceThreshFromReference1, rc.d1_distanceThreshFromReference2,
                rc.d1_hysteresisLocation,
                eventSatisfied ? "true" : "false");
            break;
        }
        case nr::rrc::common::HandoverEventType::CondD1:
        {
            eventSatisfied = rc.evaluateCondD1(m_base->UeLocation); 
            break;
        }
        case nr::rrc::common::HandoverEventType::CondT1:
        {
            eventSatisfied = rc.evaluateCondT1(now / 1000); // convert ms to sec for this check
            m_logger->debug("[MeasId=%d] Evaluating CondT1: now_time=%llds, threshold=%llds, satisfied=%s",
                            measId, now / 1000, rc.condT1_thresholdSecTS, eventSatisfied ? "true" : "false");
            break;
        }
        case nr::rrc::common::HandoverEventType::CondA3:
        case nr::rrc::common::HandoverEventType::UNKNOWN:
        default:
        {
            eventSatisfied = false;
            break;
        }
        } // switch

        // if an event is satisfied, check time-to-trigger 
        //  and if satisfied for long enough, send report
        if (eventSatisfied)
        {
            // if this is the first time condition is satisfied, record the current timestamp
            if (state.enteringTimestamp == 0)
                state.enteringTimestamp = now;

            int64_t elapsed = now - state.enteringTimestamp;

            // check that condition has been satisfied for at least timeToTrigger,
            //  and if so, send report
            if (elapsed >= rc.timeToTriggerMs())
            {
                m_logger->info("[measId=%d] Measurement event %s triggered",
                               measId, rc.eventStr());

                state.isSatisfied = true;

            }
            else
            {
                m_logger->debug("[MeasId=%d] event %s not triggered, awaiting TTT (elapsed=%d, TTT=%d)",
                    measId, rc.eventStr(), elapsed, rc.timeToTriggerMs());

            }            
        }
        else
        // no condition is satisfied, reset time-to-trigger tracking
        {
            state.enteringTimestamp = 0;
            state.isSatisfied = false;
            state.isReported = false;
        }
    }
        
}

void UeRrcTask::measurementReporting(int servingCellId, int servingRsrp)
{

    // loop through each MeasId and check if its conditions are satisfied, 
    //and if so, generate and send a MeasurementReport message to the gNB

    // int servingCellId = m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });
    // int servingRsrp = getServingCellRsrp(servingCellId, allMeas);

    for (auto &[measId, mid] : m_measConfig.measIds)
    {
        // current state of this MeasId
        auto &state = m_measConfig.measIdStates[measId];
        // ptr to the report config for this MeasId
        auto rc = m_measConfig.reportConfigs[mid.reportConfigId];

        // if conditions are satisfied and not yet reported, send report
        if (state.isSatisfied && !state.isReported) {

            // Sort triggered neighbors by RSRP descending
            std::sort(state.triggeredNeighbors.begin(), state.triggeredNeighbors.end(),
                        [](const auto &a, const auto &b) { return a.rsrp > b.rsrp; });

            // Limit to maxReportCells for this report config
            if (static_cast<int>(state.triggeredNeighbors.size()) > rc.maxReportCells)
                state.triggeredNeighbors.resize(rc.maxReportCells);

            sendMeasurementReport(measId, servingCellId, servingRsrp, state.triggeredNeighbors);

            // its now reported, set flag
            state.isReported = true;

        }
    }

}


/**
 * @brief Build and send a MeasurementReport message for the given measId, 
 *  serving cell RSRP, and triggered neighbors.
 * 
 * @param measId 
 * @param servingRsrp 
 * @param neighbors 
 */
void UeRrcTask::sendMeasurementReport(int measId, int servingCellId, int servingRsrp,
                                       const std::vector<TriggeredNeighbor> &neighbors)
{

    // the new message to send to gNB
    //  an RRC Uplink DCCH Message with a MeasurementReport payload
    auto *pdu = asn::New<ASN_RRC_UL_DCCH_Message>();
    pdu->message.present = ASN_RRC_UL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_UL_DCCH_MessageType__c1_PR_measurementReport;

    auto &mr = pdu->message.choice.c1->choice.measurementReport = asn::New<ASN_RRC_MeasurementReport>();
    mr->criticalExtensions.present =
        ASN_RRC_MeasurementReport__criticalExtensions_PR_measurementReport;

    auto *ies = mr->criticalExtensions.choice.measurementReport =
        asn::New<ASN_RRC_MeasurementReport_IEs>();

    // measId
    ies->measResults.measId = measId;

    // Serving cell result (measResultServingMOList — at least one entry)
    {
        auto *servMo = asn::New<ASN_RRC_MeasResultServMO>();
        servMo->servCellId = 0; // PCell

        // Serving cell measResult
        servMo->measResultServingCell.physCellId = asn::New<long>();
        *servMo->measResultServingCell.physCellId = servingCellId;

        auto *servRsrp = asn::New<long>();
        *servRsrp = nr::rrc::common::mtqToASNValue(servingRsrp);
        servMo->measResultServingCell.measResult.cellResults.resultsSSB_Cell =
            asn::New<ASN_RRC_MeasQuantityResults>();
        servMo->measResultServingCell.measResult.cellResults.resultsSSB_Cell->rsrp = servRsrp;

        asn::SequenceAdd(ies->measResults.measResultServingMOList, servMo);
    }

    // Neighbor cell results
    if (!neighbors.empty())
    {
        ies->measResults.measResultNeighCells =
            asn::New<ASN_RRC_MeasResults::ASN_RRC_MeasResults__measResultNeighCells>();
        ies->measResults.measResultNeighCells->present =
            ASN_RRC_MeasResults__measResultNeighCells_PR_measResultListNR;

        auto *nrList = asn::New<ASN_RRC_MeasResultListNR>();

        for (auto &nb : neighbors)
        {
            auto *measResultNR = asn::New<ASN_RRC_MeasResultNR>();

            // try to compute PCI from the neighbor's cellId (via NCI low bits)
            int pci = -1;
            if (m_cellDesc.count(nb.cellId))
            {
                int64_t nci = m_cellDesc[nb.cellId].sib1.nci;
                pci = cons::getPciFromNci(nci);
                measResultNR->physCellId = asn::New<long>();
                *measResultNR->physCellId = pci;
                m_logger->info("Reporting neighbour cellId=%d PCI=%d", nb.cellId, pci);
            }

            auto *nbRsrp = asn::New<long>();
            *nbRsrp = nr::rrc::common::mtqToASNValue(nb.rsrp);
            measResultNR->measResult.cellResults.resultsSSB_Cell =
                asn::New<ASN_RRC_MeasQuantityResults>();
            measResultNR->measResult.cellResults.resultsSSB_Cell->rsrp = nbRsrp;

            asn::SequenceAdd(*nrList, measResultNR);
        }

        ies->measResults.measResultNeighCells->choice.measResultListNR = nrList;
    }

    m_logger->info("Sending MeasurementReport measId=%d serving=%d dBm neighbors=%d",
                   measId, servingRsrp, static_cast<int>(neighbors.size()));

    sendRrcMessage(pdu);
    asn::Free(asn_DEF_ASN_RRC_UL_DCCH_Message, pdu);
}

/**
 * @brief Applies the given measurement configuration to the current state.
 *  This is called when a new measurement configuration is received from the network, 
 *  e.g. via RRCReconfiguration message.
 * 
 * @param cfg The new measurement configuration to apply.  This will be merged into the existing config,
 */
void UeRrcTask::applyMeasConfig(const UeMeasConfig &cfg)
{
    auto formatIdList = [](const std::vector<int> &ids) {
        if (ids.empty())
            return std::string("-");

        std::string out;
        out.reserve(ids.size() * 6);
        for (size_t i = 0; i < ids.size(); i++)
        {
            if (i != 0)
                out += ",";
            out += std::to_string(ids[i]);
        }
        return out;
    };

    if (cfg.fullConfig)
    {
        m_logger->info("MeasConfig fullConfig=true: replacing existing measurement configuration");
        resetMeasurements();  // this empties the m_measConfig map
    }

    std::vector<int> removedMeasIds{};
    std::vector<int> removedReportConfigs{};
    std::vector<int> removedMeasObjects{};
    std::vector<int> removedDanglingMeasIds{};

    std::vector<int> addedMeasObjects{};
    std::vector<int> modifiedMeasObjects{};
    std::vector<int> addedReportConfigs{};
    std::vector<int> modifiedReportConfigs{};
    std::vector<int> addedMeasIds{};
    std::vector<int> modifiedMeasIds{};

    m_logger->info("MeasConfig delta request: remove objects=%zu reports=%zu measIds=%zu add/mod objects=%zu "
                   "reports=%zu measIds=%zu",
                   cfg.measObjectsToRemove.size(), cfg.reportConfigsToRemove.size(), cfg.measIdsToRemove.size(),
                   cfg.measObjects.size(), cfg.reportConfigs.size(), cfg.measIds.size());

    // 3GPP delta signaling remove lists.
    for (int measId : cfg.measIdsToRemove)
    {
        if (m_measConfig.measIds.erase(measId) > 0)
            removedMeasIds.push_back(measId);
        m_measConfig.measIdStates.erase(measId);
    }

    for (int reportConfigId : cfg.reportConfigsToRemove)
    {
        if (m_measConfig.reportConfigs.erase(reportConfigId) > 0)
            removedReportConfigs.push_back(reportConfigId);
    }

    for (int measObjectId : cfg.measObjectsToRemove)
    {
        if (m_measConfig.measObjects.erase(measObjectId) > 0)
            removedMeasObjects.push_back(measObjectId);
    }

    // Drop measIds that reference removed/non-existent objects or reports.
    for (auto it = m_measConfig.measIds.begin(); it != m_measConfig.measIds.end();)
    {
        const auto &mid = it->second;
        bool missingObject = m_measConfig.measObjects.count(mid.measObjectId) == 0;
        bool missingReport = m_measConfig.reportConfigs.count(mid.reportConfigId) == 0;

        if (missingObject || missingReport)
        {
            m_measConfig.measIdStates.erase(it->first);
            removedDanglingMeasIds.push_back(it->first);
            it = m_measConfig.measIds.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Merge incoming config into current config
    for (auto &[id, obj] : cfg.measObjects)
    {
        if (m_measConfig.measObjects.count(id) > 0)
            modifiedMeasObjects.push_back(id);
        else
            addedMeasObjects.push_back(id);
        m_measConfig.measObjects[id] = obj;
    }

    for (auto &[id, rc] : cfg.reportConfigs)
    {
        if (m_measConfig.reportConfigs.count(id) > 0)
            modifiedReportConfigs.push_back(id);
        else
            addedReportConfigs.push_back(id);
        m_measConfig.reportConfigs[id] = rc;
        m_logger->info("Applied MeasConfig reportConfigId=%d event=%s", id,
                       nr::rrc::common::HandoverEventTypeToString(rc.eventKind).c_str());
    }

    for (auto &[id, mid] : cfg.measIds)
    {
        if (m_measConfig.measIds.count(id) > 0)
            modifiedMeasIds.push_back(id);
        else
            addedMeasIds.push_back(id);
        m_measConfig.measIds[id] = mid;
        // Reset state for new/updated measIds
        m_measConfig.measIdStates[id] = {};
    }

    m_logger->info("MeasConfig delta applied: removed objects=[%s] reports=[%s] measIds=[%s] danglingMeasIds=[%s]",
                   formatIdList(removedMeasObjects).c_str(), formatIdList(removedReportConfigs).c_str(),
                   formatIdList(removedMeasIds).c_str(), formatIdList(removedDanglingMeasIds).c_str());
    m_logger->info("MeasConfig delta applied: added objects=[%s] modified objects=[%s] added reports=[%s] "
                   "modified reports=[%s]",
                   formatIdList(addedMeasObjects).c_str(), formatIdList(modifiedMeasObjects).c_str(),
                   formatIdList(addedReportConfigs).c_str(), formatIdList(modifiedReportConfigs).c_str());
    m_logger->info("MeasConfig delta applied: added measIds=[%s] modified measIds=[%s]",
                   formatIdList(addedMeasIds).c_str(), formatIdList(modifiedMeasIds).c_str());


    m_logger->info("Measurement config state: %d objects, %d reports, %d measIds",
                   static_cast<int>(m_measConfig.measObjects.size()),
                   static_cast<int>(m_measConfig.reportConfigs.size()),
                   static_cast<int>(m_measConfig.measIds.size()));

}

/**
 * @brief Resets the measurement configuration state, 
 *  e.g. on Handover, RRC release or radio failure.
 * 
 */
void UeRrcTask::resetMeasurements()
{
    m_measConfig = {};
    m_logger->debug("Measurement config reset");
}

} // namespace nr::ue
