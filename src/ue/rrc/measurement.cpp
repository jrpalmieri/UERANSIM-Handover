//
// UE RRC Measurement evaluation (A2, A3, A5) and MeasurementReport generation.
//

#include "task.hpp"
#include "measurement.hpp"
#include "meas_provider.hpp"

#include <algorithm>
#include <cmath>

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <utils/common.hpp>

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

Json ToJson(const EMeasSourceType &v)
{
    switch (v)
    {
    case EMeasSourceType::NONE: return "none";
    case EMeasSourceType::UDP:  return "udp";
    case EMeasSourceType::UNIX_SOCK: return "unix";
    case EMeasSourceType::FILE: return "file";
    default: return "?";
    }
}

Json ToJson(const CellMeasurement &v)
{
    return Json::Obj({
        {"cellId", v.cellId},
        {"nci",    static_cast<int32_t>(v.nci)},
        {"rsrp",   v.rsrp},
        {"rsrq",   v.rsrq},
        {"sinr",   v.sinr},
    });
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

/**
 * @brief Determine the internal cellId for a given CellMeasurement.  
 *      If the measurement has a UERANSIM cellId, return it. 
 *      Otherwise, try to match by NR CellId (NCI) against what we have in SIB1.
 * 
 * @param cm  The CellMeasurement to resolve.
 * @return (int) The resolved cellId, or 0 if it cannot be resolved.
 */
int UeRrcTask::resolveCellId(const CellMeasurement &cm) const
{
    // check for UERANSIM cellId first
    if (cm.cellId != 0)
        return cm.cellId;

    // If no UERANSIM cellId, then try to resolve by NR CellId (NCI) from SIB1
    if (cm.nci != 0)
    {
        for (auto &[cid, desc] : m_cellDesc)
        {
            if (desc.sib1.hasSib1 && desc.sib1.nci == cm.nci)
                return cid;
        }
    }

    return 0; // unresolved
}

/**
 * @brief Collects effective RSRP per cell.  The default is to use the default
 *    fake RLS measurement values.  If an Out-Of-Band provider is available,
 *    then it overlays/replaces the RLS values with the OOB measurements.
 *
 * @return (std::unordered_map<int, CellMeasurement>) A map of cell measurements
 *      by cellId
 */
std::unordered_map<int, CellMeasurement> UeRrcTask::collectMeasurements()
{
    std::unordered_map<int, CellMeasurement> result;

    // 1. Start with RLS-simulated dBm values
    for (auto &[cellId, desc] : m_cellDesc)
    {
        CellMeasurement cm{};
        cm.cellId = cellId;
        cm.rsrp = desc.dbm;
        // RLS only provides a single dBm, use defaults for rsrq/sinr
        cm.rsrq = -10;
        cm.sinr = 0;

        if (desc.sib1.hasSib1)
            cm.nci = desc.sib1.nci;

        result[cellId] = cm;
    }

    // 2. Overlay / replace with OOB provider data
    if (m_measProvider)
    {
        auto oobMeas = m_measProvider->getLatestMeasurements();
        for (auto &cm : oobMeas)
        {
            // get cellId of meas
            int cid = resolveCellId(cm);

            if (cid != 0)
            // if a valid cellId was found, save/overlay the measurement by cellId
            {
                result[cid] = cm;
                result[cid].cellId = cid; // normalise
            }
            else
            // if cellId is 0 (cellId couldn't be determined)
            //  save it anyway, but in a way that will not conflict
            //  with real cellIds (negative synthetic cellId derived from nci)
            {
                // Unknown cell from OOB — store by nr cellId-derived synthetic key
                // (negative to avoid collision with real cellIds)
                if (cm.nci != 0)
                {
                    int syntheticId = -static_cast<int>(cm.nci & 0x7FFFFFFF);
                    result[syntheticId] = cm;
                    result[syntheticId].cellId = syntheticId;
                }
            }
        }
    }

    return result;
}

/**
 * @brief Gets the RSRP of the serving cell from a map of all measurements.
 * 
 * @param allMeas The map of all cell measurements.
 * @return (int) The RSRP of the serving cell, or -140 if no serving cell is found.
 */
int UeRrcTask::getServingCellRsrp(const std::unordered_map<int, CellMeasurement> &allMeas) const
{
    int servingCellId = m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });
    if (servingCellId != 0 && allMeas.count(servingCellId))
        return allMeas.at(servingCellId).rsrp;
    return -140; // no serving cell
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
 *  and configured thresholds.
 * 
 */
void UeRrcTask::evaluateMeasurements()
{
    // only do measurement evaluation if we're in RRC_CONNECTED 
    //  and have some measIds configured 
    if (m_state != ERrcState::RRC_CONNECTED)
        return;

    // Phase 3: skip evaluation while measurements are suspended (during handover)
    if (m_measurementsSuspended)
        return;

    if (m_measConfig.measIds.empty())
        return;

    // get the current measurements (RLS + OOB) and serving cell RSRP
    auto allMeas = collectMeasurements();
    int servingRsrp = getServingCellRsrp(allMeas);

    // get the current serving cellId for easier reference in loops below
    int servingCellId = m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });

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
            eventSatisfied = evaluateA2(servingRsrp, rc.a2Threshold, rc.hysteresis);
            break;
        }
        case EMeasEvent::A3:
        {
            for (auto &[cid, cm] : allMeas)
            {
                if (cid == servingCellId)
                    continue;
                if (evaluateA3_cell(servingRsrp, cm.rsrp, rc.a3Offset, rc.hysteresis))
                {
                    triggered.push_back({cid, cm.rsrp});
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
                for (auto &[cid, cm] : allMeas)
                {
                    if (cid == servingCellId)
                        continue;
                    if (evaluateA5_neighbor(cm.rsrp, rc.a5Threshold2, rc.hysteresis))
                    {
                        triggered.push_back({cid, cm.rsrp});
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

                sendMeasurementReport(measId, servingRsrp, triggered, allMeas);
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
 * @param allMeas 
 */
void UeRrcTask::sendMeasurementReport(int measId, int servingRsrp,
                                       const std::vector<TriggeredNeighbor> &neighbors,
                                       const std::unordered_map<int, CellMeasurement> &allMeas)
{
    (void)allMeas; // available for future extensions

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
        servMo->measResultServingCell.physCellId = nullptr; // optional
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

            // If we know the PCI we could fill physCellId -- for now it's optional
            // measResultNR->physCellId = ...;

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
