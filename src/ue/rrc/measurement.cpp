//
// UE RRC Measurement evaluation (A2, A3, A5) and MeasurementReport generation.
//

#include "task.hpp"
#include "measurement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>

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

Json ToJson(const EMeasEvent &v)
{
    switch (v)
    {
    case EMeasEvent::A2: return "A2";
    case EMeasEvent::A3: return "A3";
    case EMeasEvent::A5: return "A5";
    default: return "?";
    }
}


Json ToJson(const UeReportConfig &v)
{
    auto j = Json::Obj({
        {"reportConfigId", v.reportConfigId},
        {"event",          ToJson(v.event)},
        {"hysteresis",     v.hysteresis},
        {"timeToTrigger",  v.timeToTriggerMs},
    });
    switch (v.event)
    {
    case EMeasEvent::A2:
        j.put("a2Threshold", v.a2Threshold);
        break;
    case EMeasEvent::A3:
        j.put("a3Offset", v.a3Offset);
        break;
    case EMeasEvent::A5:
        j.put("a5Threshold1", v.a5Threshold1);
        j.put("a5Threshold2", v.a5Threshold2);
        break;
    }
    return j;
}

Json ToJson(const UeMeasConfig &v)
{
    auto arr = Json::Arr({});
    for (auto &[id, rc] : v.reportConfigs)
        arr.push(ToJson(rc));
    return Json::Obj({
        {"reportConfigs", std::move(arr)},
        {"numMeasObjects", static_cast<int32_t>(v.measObjects.size())},
        {"numMeasIds", static_cast<int32_t>(v.measIds.size())},
    });
}

Json ToJson(const EChoEventType &v)
{
    switch (v)
    {
    case EChoEventType::T1: return "T1";
    case EChoEventType::A2: return "A2";
    case EChoEventType::A3: return "A3";
    case EChoEventType::A5: return "A5";
    case EChoEventType::D1: return "D1";
    case EChoEventType::D1_SIB19: return "D1_SIB19";
    default: return "?";
    }
}

Json ToJson(const ChoCondition &v)
{
    auto j = Json::Obj({
        {"eventType", ToJson(v.eventType)},
        {"timeToTriggerMs", v.timeToTriggerMs},
        {"satisfied", v.satisfied},
    });
    switch (v.eventType)
    {
    case EChoEventType::T1:
        j.put("t1DurationMs", v.t1DurationMs);
        break;
    case EChoEventType::A2:
        j.put("a2Threshold", v.a2Threshold);
        j.put("hysteresis", v.hysteresis);
        break;
    case EChoEventType::A3:
        j.put("a3Offset", v.a3Offset);
        j.put("a3Hysteresis", v.a3Hysteresis);
        break;
    case EChoEventType::A5:
        j.put("a5Threshold1", v.a5Threshold1);
        j.put("a5Threshold2", v.a5Threshold2);
        j.put("a5Hysteresis", v.a5Hysteresis);
        break;
    case EChoEventType::D1:
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.3f", v.d1RefX);
        j.put("d1RefX", std::string(buf));
        snprintf(buf, sizeof(buf), "%.3f", v.d1RefY);
        j.put("d1RefY", std::string(buf));
        snprintf(buf, sizeof(buf), "%.3f", v.d1RefZ);
        j.put("d1RefZ", std::string(buf));
        snprintf(buf, sizeof(buf), "%.3f", v.d1ThresholdM);
        j.put("d1ThresholdM", std::string(buf));
        break;
    }
    case EChoEventType::D1_SIB19:
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.3f", v.d1sib19ThresholdM);
        j.put("d1sib19ThresholdM", std::string(buf));
        snprintf(buf, sizeof(buf), "%.3f", v.d1sib19ElevationMinDeg);
        j.put("d1sib19ElevationMinDeg", std::string(buf));
        j.put("d1sib19UseNadir", v.d1sib19UseNadir);
        snprintf(buf, sizeof(buf), "%.3f", v.d1sib19ResolvedThreshM);
        j.put("d1sib19ResolvedThreshM", std::string(buf));
        break;
    }
    }
    return j;
}

Json ToJson(const ChoCandidate &v)
{
    // Serialize condition list
    auto condArr = Json::Arr({});
    for (auto &c : v.conditions)
        condArr.push(ToJson(c));

    // Build a summary string for the condition group
    std::string condSummary;
    for (size_t i = 0; i < v.conditions.size(); i++)
    {
        if (i > 0) condSummary += " AND ";
        condSummary += ToJson(v.conditions[i].eventType).str();
    }

    return Json::Obj({
        {"candidateId", v.candidateId},
        {"targetPci", v.targetPci},
        {"newCRNTI", v.newCRNTI},
        {"t304Ms", v.t304Ms},
        {"executionPriority", v.executionPriority},
        {"conditionGroup", condSummary},
        {"conditions", std::move(condArr)},
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
    if (servingCellId != 0) {
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
 * @brief Struct to store measurement events for a cellId.
 */
struct TriggeredNeighbor
{
    int cellId;
    int rsrp;
};

static bool evaluateA2(int servingRsrp, int threshold, int hyst)
{
    // A2 entering: serving RSRP < threshold - hyst
    return servingRsrp < (threshold - hyst);
}

static bool evaluateA3_cell(int servingRsrp, int neighborRsrp, int offset, int hyst)
{
    // A3 entering: neighbor RSRP > serving RSRP + offset + hyst
    return neighborRsrp > (servingRsrp + offset + hyst);
}

static bool evaluateA5_serving(int servingRsrp, int threshold1, int hyst)
{
    // A5 entering condition 1: serving < threshold1 - hyst
    return servingRsrp < (threshold1 - hyst);
}

static bool evaluateA5_neighbor(int neighborRsrp, int threshold2, int hyst)
{
    // A5 entering condition 2: neighbor > threshold2 + hyst
    return neighborRsrp > (threshold2 + hyst);
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
void UeRrcTask::evaluateMeasurements()
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

    std::map<int, int> allMeas;
    // Get the current measurements to evaluate against
    //   since this is actively updated by the RLS meas task and read here by RRC, 
    //   we make a copy from the shared context which is protected by a mutex
    {
        std::shared_lock lock(m_base->cellDbMeasMutex);
        allMeas = m_base->cellDbMeas;
    }

    // get the serving cell id and RSRP for easier reference in loops below
    int servingCellId = m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });
    int servingRsrp = getServingCellRsrp(servingCellId, allMeas);
    m_logger->debug("evaluateMeasurements: servingCell=%d servingRsrp=%d dBm, allMeas.size=%zu", servingCellId, servingRsrp, allMeas.size());

    // current time for time-to-trigger evaluation
    int64_t now = utils::CurrentTimeMillis();

    // loop through all configured measIds and evaluate their conditions against 
    //  the current measurements
    for (auto &[measId, mid] : m_measConfig.measIds)
    {
        auto rcIt = m_measConfig.reportConfigs.find(mid.reportConfigId);
        if (rcIt == m_measConfig.reportConfigs.end())
            continue;

        auto &rc = rcIt->second;
        auto &state = m_measConfig.measIdStates[measId];

        if (state.reported)
            continue; // already reported for this config (one-shot)

        bool eventSatisfied = false;
        std::vector<TriggeredNeighbor> triggered;

        // check for each event type, and evaluate according to the relevant conditions
        switch (rc.event)
        {
        case EMeasEvent::A2:
        {
            // A2 - just compare serving cell rsrp to threshold
            eventSatisfied = evaluateA2(servingRsrp, rc.a2Threshold, rc.hysteresis);
            break;
        }
        case EMeasEvent::A3:
        {
            // A3 - compare each neighbor cell's RSRP to the serving cell RSRP + offset
            for (auto cm : allMeas)
            {
                if (cm.first == servingCellId)
                    continue;
                // log each neighbor check
                m_logger->debug("A3 check cid=%d rsrp=%d (serving=%d offset=%d hyst=%d)",
                                cm.first, cm.second, servingRsrp, rc.a3Offset, rc.hysteresis);
                if (evaluateA3_cell(servingRsrp, cm.second, rc.a3Offset, rc.hysteresis))
                {
                    m_logger->info("A3 condition satisfied for cid=%d rsrp=%d", cm.first, cm.second);
                    triggered.push_back({cm.first, cm.second});
                    eventSatisfied = true;
                }
            }
            break;
        }
        case EMeasEvent::A5:
        {
            bool servCond = evaluateA5_serving(servingRsrp, rc.a5Threshold1, rc.hysteresis);
            if (servCond)
            {
                for (auto cm : allMeas)
                {
                    if (cm.first == servingCellId)
                        continue;
                    if (evaluateA5_neighbor(cm.second, rc.a5Threshold2, rc.hysteresis))
                    {
                        triggered.push_back({cm.first, cm.second});
                        eventSatisfied = true;
                    }
                }
            }
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
            if (elapsed >= rc.timeToTriggerMs)
            {
                m_logger->info("Measurement event %s triggered for measId %d (serving RSRP=%d dBm)",
                               ToJson(rc.event).str().c_str(), measId, servingRsrp);

                // Sort triggered neighbors by RSRP descending
                std::sort(triggered.begin(), triggered.end(),
                         [](const auto &a, const auto &b) { return a.rsrp > b.rsrp; });

                // Limit to maxReportCells
                if (static_cast<int>(triggered.size()) > rc.maxReportCells)
                    triggered.resize(rc.maxReportCells);

                sendMeasurementReport(measId, servingCellId, servingRsrp, triggered);
                state.reported = true;
            }
        }
        else
        // the condition is not satisfied, reset time-to-trigger tracking
        {
            state.enteringTimestamp = 0;
        }
    }
}

/**
 * @brief Converts an RSRP value in dBm to the ASN.1 encoded long value 
 *  expected by the MeasurementReport message.
 * Per TS 38.133 Table 10.1.6.1-1, the RSRP value in dBm is mapped to 
 *  an integer value from 0 to 127 by adding 156.
 * 
 * @param rsrpDbm 
 * @return (long) The ASN.1 encoded RSRP value, clipped to the range 0..127. 
 */
static long rsrpToAsn(int rsrpDbm)
{
    int val = rsrpDbm + 156;
    if (val < 0) val = 0;
    if (val > 127) val = 127;
    return static_cast<long>(val);
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
        *servRsrp = rsrpToAsn(servingRsrp);
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
                pci = static_cast<int>(nci & 0x3FF);
                measResultNR->physCellId = asn::New<long>();
                *measResultNR->physCellId = pci;
                m_logger->info("Reporting neighbour cellId=%d PCI=%d", nb.cellId, pci);
            }

            auto *nbRsrp = asn::New<long>();
            *nbRsrp = rsrpToAsn(nb.rsrp);
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
    // Merge incoming config into current config
    for (auto &[id, obj] : cfg.measObjects)
        m_measConfig.measObjects[id] = obj;

    for (auto &[id, rc] : cfg.reportConfigs)
        m_measConfig.reportConfigs[id] = rc;

    for (auto &[id, mid] : cfg.measIds)
    {
        m_measConfig.measIds[id] = mid;
        // Reset state for new/updated measIds
        m_measConfig.measIdStates[id] = {};
    }

    m_logger->info("Applied measurement config: %d objects, %d reports, %d measIds",
                   static_cast<int>(m_measConfig.measObjects.size()),
                   static_cast<int>(m_measConfig.reportConfigs.size()),
                   static_cast<int>(m_measConfig.measIds.size()));
}

/**
 * @brief Resets the measurement configuration state, 
 *  e.g. on RRC release or radio failure.
 * 
 */
void UeRrcTask::resetMeasurements()
{
    m_measConfig = {};
    m_logger->debug("Measurement config reset");
}

} // namespace nr::ue
