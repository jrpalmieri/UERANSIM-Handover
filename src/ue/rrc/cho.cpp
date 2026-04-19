//
// Conditional Handover (CHO) – Release 16/17 with Condition Groups.
//
// Per 3GPP TS 38.331 §5.3.5.8 (Release 17), the gNB configures one or more
// CondReconfigToAddMod entries, each carrying:
//   - condExecutionCond: a list of MeasId values.  Multiple MeasIds within
//     one entry are evaluated with AND logic (all must be satisfied).
//   - condRRCReconfig: the nested RRCReconfiguration to apply on trigger.
//   - condExecutionPriority (optional): lower value = higher priority.
//
// Multiple CondReconfigToAddMod entries (candidates) are treated as OR:
// the first (or highest-priority) candidate whose condition group is fully
// satisfied triggers handover.
//
// Includes implementation of the NTN-specific timing IE "ntn-TriggerConfig-r17", 
//  which allows the gNB to specify a timing-based criteria in a reportConfig.
//  The UE uses this value to set a timer that is evaluates as an AND condition
//   with the base reportConfig event (e.g. A3) for CHO triggering.
//
// When multiple candidates trigger in the same evaluation cycle, the UE
// selects based on:
//   1. condExecutionPriority (lowest wins)
//   2. Greatest trigger margin
//   3. Highest neighbor RSRP
//   4. Configuration order (earliest in the list)
//
// Implements:
//   - parseConditionalReconfiguration(): ASN.1 path.
//   - handleChoConfiguration(): Binary DL_CHO test/fallback path.
//   - evaluateChoCandidates(): Per-cycle condition group evaluation.
//   - evaluateCondition(): Single atomic condition evaluation.
//   - selectBestCandidate(): Priority + tie-breaking.
//   - executeChoCandidate(): Triggers performHandover().
//   - cancelAllChoCandidates(): Clears all pending candidates.
//

#include "task.hpp"
#include "measurement.hpp"
#include "position.hpp"
#include "sib19.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <limits>
#include <unordered_set>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/sat_time.hpp>

#include <asn/rrc/ASN_RRC_ConditionalReconfiguration.h>
#include <asn/rrc/ASN_RRC_CondReconfigToAddMod.h>
#include <asn/rrc/ASN_RRC_CondTriggerConfig-r16.h>
#include <asn/rrc/ASN_RRC_NTN-TriggerConfig-r17.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1530-IEs.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_SpCellConfig.h>
#include <asn/rrc/ASN_RRC_ReconfigurationWithSync.h>
#include <asn/rrc/ASN_RRC_ServingCellConfigCommon.h>

static constexpr int MIN_COND_RECONFIG_ID = 1;
static constexpr int MAX_COND_RECONFIG_ID = 8;
static constexpr int MIN_COND_EXEC_PRIORITY = 1;
static constexpr int MAX_COND_EXEC_PRIORITY = 128;
static constexpr int MIN_MEAS_ID = 1;
static constexpr int MAX_MEAS_ID = 64;
static constexpr int MAX_COND_EXEC_MEAS_IDS = 2;
static constexpr int MAX_COND_RECONFIG_REMOVE_ENTRIES = 64;
static constexpr int MAX_COND_RECONFIG_ADDMOD_ENTRIES = 64;
static constexpr int MAX_COND_RRC_RECONFIG_BYTES = 16384;



namespace nr::ue
{

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */


int selectBestTerrestrial(const std::unordered_set<int> &nonSatPcis, const std::map<int, int> &allMeas)
{
    // select the target candidate with the strongest signal (highest RSRP).
    // In the future, we can add more sophisticated tie-breaking logic here if needed.
    int bestPci = -1;
    int bestRsrp = std::numeric_limits<int>::min();

    for (int pci : nonSatPcis)
    {
        auto it = allMeas.find(pci);
        if (it == allMeas.end())
            continue;
        int rsrp = it->second;

        if (rsrp > bestRsrp)
        {
            bestRsrp = rsrp;
            bestPci = pci;
        }
    }

    return bestPci;
}

int selectBestSatellite(const std::unordered_set<int> &satPcis)
{
    return -1;
    // loop through SIB19 data for all satellite candidates and calculate the transit time

    // for (int pci : satPcis)
    // {
    //     auto it = m_cellDesc.find(pci);
    //     if (it == m_cellDesc.end())
    //         continue;

    //     auto &sib19 = it->second.sib19;
    //     if (!sib19.hasSib19)
    //         continue;

    //     // only consider candidates with valid SIB19 ephemeris
    //     if (!isSib19EphemerisValid(sib19, utils::CurrentTimeMillis()))
    //         continue;

    //     // calculate transit time based on current SIB19 ephemeris and satellite position extrapolation
    //     //  (similar to the serving cell position calculation in evaluateConditionD1)


    // // calculate reference position for satellite, if serving cell is a sat
    // //   used in all D1 condition checks that use nadir
    // EcefPosition servingPosNadir{0, 0, 0};

    // bool servingPosValid = false;
   
    // // check if the serving cell ID is in the SIB19 map.  if it is, the serving cell is a sat
    // auto servingIt = m_cellDesc.find(servingCellId);
    // if (servingIt != m_cellDesc.end() && servingIt->second.sib19.hasSib19)
    // {
    //     auto &sib19 = servingIt->second.sib19;
    //     //int64_t relativeNow = now - m_startedTime;

    //     // only use if the SIB19 data is current (within the validity threshold)
    //     if (isSib19EphemerisValid(sib19, now))
    //     {
            
    //         // get satellite's current position using current SIB19 ephemeris and
    //         //   motion extrapolation based on time delta from SIB19 epoch to now

    //         double dtSec = (now - (sib19.ntnConfig.epochTime)*10) / 1000.0;  // sib19 epoch is counted in 10ms units
    //         double satX, satY, satZ;
    //         // if position is stored in SIB19 as ECEF position + velocity, apply delta based on time 
    //         if (sib19.ntnConfig.ephemerisInfo.type == EEphemerisType::POSITION_VELOCITY)
    //         {
    //             extrapolateSatelliteEcefPosition(
    //                 sib19.ntnConfig.ephemerisInfo.posVel,
    //                 dtSec, satX, satY, satZ);

    //             // convert position to nadir for distance calculations
    //             servingPosNadir = computeNadir(satX, satY, satZ);
    //             servingPosValid = true;

    //         }
    //         // otherwise it is stored as orbital parameters that need to be converted to ECEF position
    //         //   using the SIB19 epoch as reference time
    //         else  // ORBITAL_PARAMETERS
    //         {
    //             int64_t epochMs = static_cast<int64_t>(sib19.ntnConfig.epochTime) * 10LL;
    //             propagateKeplerian(
    //                 sib19.ntnConfig.ephemerisInfo.orbital,
    //                 epochMs, now, satX, satY, satZ);
    //             servingPosNadir = computeNadir(satX, satY, satZ);
    //             servingPosValid = true;
    //         }
    //     }
    // }


}

// Build a human-readable condition group string, e.g. "(A3 AND D1)".
static std::string conditionGroupStr(const std::vector<int> &measIds, const UeMeasConfig &cfg)
{
    if (measIds.empty())
        return "(empty)";
    std::string s = "(";
    for (size_t i = 0; i < measIds.size(); i++)
    {
        if (i > 0) s += " AND ";
        auto measIt = cfg.measIds.find(measIds[i]);
        if (measIt == cfg.measIds.end())
        {
            s += "UNKNOWN";
            continue;
        }

        int rid = measIt->second.reportConfigId;
        auto reportIt = cfg.reportConfigs.find(rid);
        if (reportIt == cfg.reportConfigs.end())
        {
            s += "UNKNOWN";
            continue;
        }

        s += reportIt->second.eventStr();
    }
    s += ")";
    return s;
}

// Upsert a CHO candidate by candidateId and return true if updated, false if added.
static bool upsertChoCandidate(std::vector<ChoCandidate> &candidates, ChoCandidate &&cand)
{
    auto existing = std::find_if(
        candidates.begin(), candidates.end(),
        [&](const ChoCandidate &c) { return c.candidateId == cand.candidateId; });

    if (existing != candidates.end())
    {
        *existing = std::move(cand);
        return true;
    }

    candidates.push_back(std::move(cand));
    return false;
}

/**
 * @brief Parse ConditionalReconfiguration from ASN.1 extension chain
 * 
 */
void UeRrcTask::parseConditionalReconfiguration(
    const ASN_RRC_ConditionalReconfiguration *condReconfig)
{
    if (!condReconfig)
    {
        m_logger->warn("ConditionalReconfiguration: null message");
        return;
    }

    int removedCount = 0;
    int removeMissCount = 0;

    // Apply remove list first, then add/modify list.
    if (condReconfig->condReconfigToRemoveList)
    {
        auto &removeList = condReconfig->condReconfigToRemoveList->list;
        std::unordered_set<int> seenRemoveIds;
        if (removeList.count > MAX_COND_RECONFIG_REMOVE_ENTRIES)
        {
            m_logger->warn("ConditionalReconfiguration remove list has %d entries; processing first %d",
                           removeList.count, MAX_COND_RECONFIG_REMOVE_ENTRIES);
        }

        int removeLimit = std::min(removeList.count, MAX_COND_RECONFIG_REMOVE_ENTRIES);
        for (int i = 0; i < removeLimit; i++)
        {
            long *pId = removeList.array[i];
            if (!pId)
            {
                removeMissCount++;
                continue;
            }

            int candidateId = static_cast<int>(*pId);
            if (candidateId < MIN_COND_RECONFIG_ID || candidateId > MAX_COND_RECONFIG_ID)
            {
                m_logger->warn("ConditionalReconfiguration remove: invalid condReconfigId=%d (valid range=%d..%d)",
                               candidateId, MIN_COND_RECONFIG_ID, MAX_COND_RECONFIG_ID);
                removeMissCount++;
                continue;
            }

            if (!seenRemoveIds.insert(candidateId).second)
            {
                m_logger->warn("ConditionalReconfiguration remove: duplicate condReconfigId=%d ignored",
                               candidateId);
                continue;
            }

            size_t before = m_choCandidates.size();
            m_choCandidates.erase(
                std::remove_if(
                    m_choCandidates.begin(), m_choCandidates.end(),
                    [&](const ChoCandidate &c) { return c.candidateId == candidateId; }),
                m_choCandidates.end());

            if (m_choCandidates.size() < before)
                removedCount++;
            else
                removeMissCount++;
        }
    }

    int addModCount = condReconfig->condReconfigToAddModList ?
        condReconfig->condReconfigToAddModList->list.count : 0;
    m_logger->info("ConditionalReconfiguration received: addMod=%d remove=%d",
                   addModCount, removedCount);

    // if addMod list is not present, then we are done after applying the remove list
    if (!condReconfig->condReconfigToAddModList)
    {
        m_logger->info("ConditionalReconfiguration applied: removed=%d removeMiss=%d activeCandidates=%zu",
                       removedCount, removeMissCount, m_choCandidates.size());
        return;
    }

    auto &addList = condReconfig->condReconfigToAddModList->list;
    if (addList.count > MAX_COND_RECONFIG_ADDMOD_ENTRIES)
    {
        m_logger->warn("ConditionalReconfiguration add/mod list has %d entries; processing first %d",
                       addList.count, MAX_COND_RECONFIG_ADDMOD_ENTRIES);
    }
    int addLimit = std::min(addList.count, MAX_COND_RECONFIG_ADDMOD_ENTRIES);

    int addedCount = 0;
    int updatedCount = 0;
    int skippedCount = 0;
    std::unordered_set<int> seenAddIds;

    for (int i = 0; i < addLimit; i++)
    {
        auto *item = addList.array[i];
        if (!item)
        {
            skippedCount++;
            continue;
        }

        ChoCandidate cand{};
        cand.candidateId = static_cast<int>(item->condReconfigId);
        if (cand.candidateId < MIN_COND_RECONFIG_ID || cand.candidateId > MAX_COND_RECONFIG_ID)
        {
            m_logger->warn("CHO candidate skipped: invalid condReconfigId=%d (valid range=%d..%d)",
                           cand.candidateId, MIN_COND_RECONFIG_ID, MAX_COND_RECONFIG_ID);
            skippedCount++;
            continue;
        }

        if (!seenAddIds.insert(cand.candidateId).second)
        {
            m_logger->warn("CHO candidate %d appears multiple times in add/mod list; using first occurrence",
                           cand.candidateId);
            skippedCount++;
            continue;
        }


        // --- Build condition group from condExecutionCond MeasIds ---
        // Per TS 38.331: multiple MeasIds in one condExecutionCond = AND logic.
        if (item->condExecutionCond)
        {
            auto &measIdList = item->condExecutionCond->list;
            std::unordered_set<int> seenMeasIds;
            if (measIdList.count > MAX_COND_EXEC_MEAS_IDS)
            {
                m_logger->warn("CHO candidate %d: condExecutionCond has %d MeasIds; processing first %d",
                               cand.candidateId, measIdList.count, MAX_COND_EXEC_MEAS_IDS);
            }
            for (int j = 0; j < measIdList.count; j++)
            {
                if (j >= MAX_COND_EXEC_MEAS_IDS)
                    break;

                long *pMeasId = measIdList.array[j];
                if (!pMeasId)
                    continue;
                int measId = static_cast<int>(*pMeasId);

                if (measId < MIN_MEAS_ID || measId > MAX_MEAS_ID)
                {
                    m_logger->warn("CHO candidate %d: MeasId %d out of supported range (%d..%d); skipping condition",
                                   cand.candidateId, measId, MIN_MEAS_ID, MAX_MEAS_ID);
                    continue;
                }

                if (!seenMeasIds.insert(measId).second)
                {
                    m_logger->warn("CHO candidate %d: duplicate MeasId %d in condExecutionCond; skipping duplicate",
                                   cand.candidateId, measId);
                    continue;
                }

                // Note - measConfigs should have already been stored from processing the main RRCReconfiguration measConfig IE
                //  so we should be able to look up the MeasId in the measId map
                auto itMid = m_measConfig.measIds.find(measId);
                if (itMid == m_measConfig.measIds.end())
                {
                    m_logger->warn("CHO candidate %d: condExecutionCond MeasId %d not found "
                                   "in active MeasConfig - skipping this condition",
                                   cand.candidateId, measId);
                    continue;
                }

                auto itRc = m_measConfig.reportConfigs.find(itMid->second.reportConfigId);
                if (itRc == m_measConfig.reportConfigs.end())
                {
                    m_logger->warn("CHO candidate %d: reportConfig %d for MeasId %d not found; skipping condition",
                                   cand.candidateId, itMid->second.reportConfigId, measId);
                    continue;
                }

                // add MeasId to the candidate's condition group
                cand.measIds.push_back(measId);
            }
        }

        // If condExecutionCond was present but did not resolve to usable conditions,
        // reject the candidate
        if (cand.measIds.empty())
        {
            m_logger->warn("CHO candidate %d: condExecutionCond has no usable MeasIds; skipping candidate",
                            cand.candidateId);
            skippedCount++;
            continue;

            // A candidate without usable measurable conditions is invalid.
        }


        // Decode the nested RRCReconfiguration from condRRCReconfig to extract the target PCI, C-RNTI, and T304 for 
        //  the candidate target cell.

        if (!item->condRRCReconfig || item->condRRCReconfig->size == 0)
        {
            m_logger->warn("CHO candidate %d: missing condRRCReconfig - skipping",
                           cand.candidateId);
            skippedCount++;
            continue;
        }

        // if (item->condRRCReconfig->size > MAX_COND_RRC_RECONFIG_BYTES)
        // {
        //     m_logger->warn("CHO candidate %d: condRRCReconfig too large (%zu bytes) – skipping",
        //                    cand.candidateId, static_cast<size_t>(item->condRRCReconfig->size));
        //     skippedCount++;
        //     continue;
        // }

        auto *innerReconfig = rrc::encode::Decode<ASN_RRC_RRCReconfiguration>(
            asn_DEF_ASN_RRC_RRCReconfiguration, *item->condRRCReconfig);

        if (!innerReconfig)
        {
            m_logger->err("CHO candidate %d: failed to UPER-decode condRRCReconfig",
                          cand.candidateId);
            skippedCount++;
            continue;
        }

        // save the txId from the target's RRCReconfiguration for use when triggering the candidate
        cand.txId = static_cast<int>(innerReconfig->rrc_TransactionIdentifier);

        bool foundRWS = false;
        if (innerReconfig->criticalExtensions.present ==
            ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration)
        {
            auto *innerIes = innerReconfig->criticalExtensions.choice.rrcReconfiguration;
            if (innerIes && innerIes->nonCriticalExtension)
            {
                auto *v1530 = innerIes->nonCriticalExtension;
                if (v1530->masterCellGroup)
                {
                    auto *cellGroupConfig = rrc::encode::Decode<ASN_RRC_CellGroupConfig>(
                        asn_DEF_ASN_RRC_CellGroupConfig, *v1530->masterCellGroup);
                    if (cellGroupConfig)
                    {
                        if (cellGroupConfig->spCellConfig &&
                            cellGroupConfig->spCellConfig->reconfigurationWithSync)
                        {
                            auto *rws = cellGroupConfig->spCellConfig->reconfigurationWithSync;

                            // found the PCI, cRNTI and t304 timers for the candidate's target cell
                            cand.targetPci =
                                (rws->spCellConfigCommon && rws->spCellConfigCommon->physCellId)
                                    ? static_cast<int>(*rws->spCellConfigCommon->physCellId)
                                    : -1;
                            cand.newCRNTI = static_cast<int>(rws->newUE_Identity);
                            cand.t304Ms = nr::rrc::common::t304EnumToMs(rws->t304);
                            foundRWS = true;
                        }
                        asn::Free(asn_DEF_ASN_RRC_CellGroupConfig, cellGroupConfig);
                        m_logger->debug("CHO candidate %d: decoded cRNTI=%d PCI=%d T304=%dms from nested RRCReconfiguration",
                                        cand.candidateId, cand.newCRNTI, cand.targetPci, cand.t304Ms);
                            
                    }
                }
            }
        }

        asn::Free(asn_DEF_ASN_RRC_RRCReconfiguration, innerReconfig);

        if (!foundRWS)
        {
            m_logger->warn("CHO candidate %d: no ReconfigurationWithSync in nested reconfig "
                           "- skipping", cand.candidateId);
            skippedCount++;
            continue;
        }

        // Enable candidate for evaluation
        cand.executed = false;

        m_logger->info("CHO candidate %d added: targetPCI=%d newC-RNTI=%d t304=%dms "
                       "conditions=%s priority=%d",
                       cand.candidateId, cand.targetPci, cand.newCRNTI, cand.t304Ms,
                       conditionGroupStr(cand.measIds, m_measConfig).c_str(),
                       cand.executionPriority);

        // add to the candidate list, or update if candidateId already exists
        bool wasUpdated = upsertChoCandidate(m_choCandidates, std::move(cand));
        if (wasUpdated)
        {
            updatedCount++;
        }
        else
        {
            addedCount++;
        }
    }

    m_logger->info(
        "ConditionalReconfiguration applied: removed=%d removeMiss=%d "
        "added=%d updated=%d skipped=%d activeCandidates=%zu",
        removedCount, removeMissCount, addedCount,
        updatedCount, skippedCount, m_choCandidates.size());
}





// // Evaluate a single condition with time-to-trigger.
// // Returns the margin (>0 = satisfied after TTT) and updates cond.satisfied.
// static double evaluateConditionWithTTT(
//     ChoCondition &cond,
//     int64_t now,
//     int servingRsrp,
//     int targetRsrp,
//     double ueDistance,
//     Logger *logger,
//     int candidateId)
// {

//     // if there is a timer criterion, check if it is met.
//     bool timerMet = isTimerCriterionMet(cond, now, logger, candidateId);
//     if (!timerMet)
//         return -1.0;


//     // evaluate the base condition first to see if it's currently met, and to get the margin for logging and tie-breaking.
//     double margin = evaluateConditionRaw(cond, now, servingRsrp, targetRsrp,
//                                           ueDistance, logger, candidateId);
//     bool measurementMet = (margin > 0);

//     if (measurementMet)
//     {
//         // check time-to-trigger

//         // if this is teh first time condition is met, start the ttt timer
//         if (cond.tttEnteringTimestamp == 0)
//             cond.tttEnteringTimestamp = now;

//         // check for condition held for duration of timer
//         int64_t held = now - cond.tttEnteringTimestamp;
//         if (held >= cond.timeToTriggerMs)
//         {
//             cond.satisfied = true;
//             return margin;
//         }
//         else
//         {
//             cond.satisfied = false;
//             return -1.0; // Not yet held long enough
//         }
//     }
//     else
//     {
//         // reset the TTT timer if condition is not met
//         if (cond.tttEnteringTimestamp != 0)
//         {
//             logger->debug("CHO candidate %d: %s condition no longer satisfied, resetting TTT",
//                           candidateId, eventTypeName(cond.eventType));
//             cond.tttEnteringTimestamp = 0;
//         }
//         cond.satisfied = false;
//         return -1.0;
//     }
// }

/**
 * @brief Evaluates each CHO candidate for condition satisfaction.  For candidates whose conditions 
 *  are all satisfied, triggers handover to the candidate.  Applies tie-breaking logic if more than 
 *  one candidate is satisfied.
 * 
 *  This is called periodically, just after the measurements check and before measurement reporting,
 *  so that CHO candidates get evaluated before the normal event logic.
 * 
 * @return true 
 * @return false 
 */
bool UeRrcTask::evaluateChoCandidates(int servingCellId, const std::map<int, int> &allMeas)
{
    // if no candidates, or if handover is already in progress, or if measurement evaluation is suspended, then skip
    if (m_choCandidates.empty() || m_handoverInProgress || m_measurementEvalSuspended)
        return false;

    std::vector<int> triggeredCandidateIndices;  // indices of candidates whose conditions are currently satisfied
    for (const auto &cand : m_choCandidates)
    {
        m_logger->debug("Evaluating CHO candidate %d: targetPCI=%d conditions=%s priority=%d executed=%s",
                        cand.candidateId, cand.targetPci, conditionGroupStr(cand.measIds, m_measConfig).c_str(),
                        cand.executionPriority, cand.executed ? "yes" : "no");

        // check each condition for satisfaction
        std::size_t total_satisfied = 0;
        for (int measId : cand.measIds)
        {
            auto &mId = m_measConfig.measIdStates[measId];
            if (mId.isSatisfied)
            {
                total_satisfied++;
            }
        }
        if ((total_satisfied != 0U) && (total_satisfied == cand.measIds.size()))
        {
            m_logger->debug("CHO candidate %d: triggered by event group %s",
                            cand.candidateId, conditionGroupStr(cand.measIds, m_measConfig).c_str());

            triggeredCandidateIndices.push_back(cand.candidateId);
        }
    }

    // nothing triggered, just exit
    if (triggeredCandidateIndices.empty())
    {
        m_logger->debug("No CHO candidates triggered at this time");
        return false;
    }

    // One triggered candidate – no need for tie-breaking, just execute it.
    if (triggeredCandidateIndices.size() == 1)
    {
        int ci = triggeredCandidateIndices[0];
        auto &cand = m_choCandidates[ci];
        m_logger->info("CHO candidate %d is only triggered candidate; executing handover to PCI %d",
                       cand.candidateId, cand.targetPci);
        executeChoCandidate(cand);
        return true;
    }

    // More than one - need to apply tie-breaking logic to select the best candidate to execute.

    // if these targets are satellites, then we need to calculate their transit times. Sat with the longest
    //  transit time will be in range teh longest and will be highest in sky
    
    // generate list of PCIs that are sats
    std::unordered_set<int> satPcis;
    std::map<int, int> PciToChoId;
    std::unordered_set<int> nonSatPcis;
    for (int ci : triggeredCandidateIndices)
    {
        auto &cand = m_choCandidates[ci];
        PciToChoId[cand.targetPci] = ci;
        auto it = m_cellDesc.find(cand.targetPci);
        if (it != m_cellDesc.end() && it->second.sib19.hasSib19)
        {
            satPcis.insert(cand.targetPci);
        }
        else
        {
            nonSatPcis.insert(cand.targetPci);
        }
    }

    // select best sat
    int bestSatPci = selectBestSatellite(satPcis);

    // for non-SATs, we can apply the normal RSRP-based tie-breaking logic (highest RSRP wins)
    int bestNonSatPci = selectBestTerrestrial(nonSatPcis, allMeas);

    // for now, we pick the best SAT candidate if one exists, otherwise we pick the best non-SAT candidate.
    int bestPci = bestSatPci > 0 ? bestSatPci : bestNonSatPci;

    auto &foundCand = m_choCandidates[PciToChoId[bestPci]];
    m_logger->info("CHO candidate %d selected as best candidate; executing handover to PCI %d",
                    foundCand.candidateId, foundCand.targetPci);
    executeChoCandidate(foundCand);
    return true;

    // current satellite-domain time in milliseconds
    // int64_t now = m_base->satTime != nullptr
    //                   ? m_base->satTime->CurrentSatTimeMillis()
    //                   : utils::CurrentTimeMillis();

    // copy the current cell power measurements under lock to avoid holding the lock while evaluating conditions
    // std::map<int, int> allMeas;
    // {
    //     std::shared_lock lock(m_base->cellDbMeasMutex);
    //     allMeas = m_base->cellDbMeas;
    // }

    // current cellId
    // int servingCellId = m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });
    // current cell RSRP
    // int servingRsrp = getServingCellRsrp(servingCellId, allMeas);



    // // Phase 1: Evaluate all conditions for all candidates.
    // // Collect candidates whose entire condition group is satisfied.
    // struct TriggeredCandidate
    // {
    //     int index;           // index into m_choCandidates
    //     double minMargin;    // minimum margin across all conditions (bottleneck)
    //     int targetRsrp;
    // };
    // std::vector<TriggeredCandidate> triggered;

    // for (size_t ci = 0; ci < m_choCandidates.size(); ci++)
    // {
    //     auto &cand = m_choCandidates[ci];
    //     // if this candidate is marked as executed, or if it has no conditions (invalid), then skip evaluating it
    //     if (cand.executed || cand.conditions.empty())
    //         continue;

    //     // Find target cell RSRP for this candidate (used by A3, A5 conditions).
    //     int targetRsrp = cons::MIN_RSRP;
    //     for (auto &[cellId, meas] : allMeas)
    //     {
    //         // match by PCI value
    //         if (cellId == cand.targetPci)
    //         {
    //             targetRsrp = meas;
    //             break;
    //         }
    //         // if PCI match didn;t work, try matching by NCI
    //         if (m_cellDesc.count(cellId))
    //         {
    //             int nciLow = cons::getPciFromNci(m_cellDesc[cellId].sib1.nci);
    //             if (nciLow == cand.targetPci)
    //             {
    //                 targetRsrp = meas;
    //                 break;
    //             }
    //         }
    //     }

    //     bool allSatisfied = true;
    //     double minMargin = 1e9;

    //     // loop through each condition in the candidate's condition group
    //     for (auto &cond : cand.conditions)
    //     {
    //         // if condition is event D1 - calculate distance from UE to reference point
    //         double servingD1DistanceM = 0.0;
    //         if (cond.eventType == EChoEventType::D1)
    //         {
    //             // for fixed reference, calculate distance from UE to fixed point
    //             if (cond.d1ReferenceType == ED1ReferenceType::Fixed)
    //             {
    //                 EcefPosition ref{cond.d1RefX, cond.d1RefY, cond.d1RefZ};
    //                 servingD1DistanceM = ecefDistance(uePos.ecef, ref);
    //                 servingPosValid = true;
    //             }
    //             // for nadir reference, calculate distance from UE to satellite nadir position (if available)
    //             else if (cond.d1ReferenceType == ED1ReferenceType::Nadir && servingPosValid)
    //             {
    //                 servingD1DistanceM = ecefDistance(uePos.ecef, servingPosNadir);
    //             }
    //             // if not fixed and no valid distance found for serving cell — cannot evaluate D1 conditions for this candidate
    //             //  so leave distance a 0, which will cause teh condition to be not satisfied and the TTT timer to reset
    //             else {

    //                 m_logger->debug("CHO candidate %d: D1 skipped – serving position unavailable",
    //                                     cand.candidateId);
    //             }

    //             // D1 entering condition: distance > threshold + hysteresis.
    //             cond.d1ResolvedThreshM = cond.d1ThresholdM + static_cast<double>(cond.hysteresis);
    //         }


}


/**
 * @brief Triggers the actual handover from a CHO candidate.
 * Any CHO candidates that are not executed are cancelled, per TS 38.331 §5.3.5.8.6.
 * 
 * @param candidate 
 */
void UeRrcTask::executeChoCandidate(ChoCandidate &candidate)
{
    candidate.executed = true;

    m_logger->info("Executing CHO candidate %d: targetPCI=%d newC-RNTI=%d t304=%dms",
                   candidate.candidateId, candidate.targetPci,
                   candidate.newCRNTI, candidate.t304Ms);

    // Cancel remaining CHO candidates before starting the handover.
    // Per TS 38.331 §5.3.5.8.6, the UE cancels all other CHO candidates
    // when one is executed.
    for (auto &c : m_choCandidates)
    {
        if (c.candidateId != candidate.candidateId)
            c.executed = true;
    }

    performHandover(candidate.txId, candidate.targetPci, candidate.newCRNTI,
                    candidate.t304Ms, /*hasRachConfig=*/false);
}

/**
 * @brief Cancels all CHO candidates, e.g. when a handover is triggered by other means, or when the RRC connection is released.
 * 
 * @return * void 
 */
void UeRrcTask::cancelAllChoCandidates()
{
    if (m_choCandidates.empty())
        return;

    m_logger->info("Cancelling %d CHO candidate(s)", static_cast<int>(m_choCandidates.size()));
    m_choCandidates.clear();
}

/**
 * @brief Resets the runtime state of all CHO candidates, e.g. when a handover is triggered by other means, or when the RRC connection is released.
 * 
 * @return * void 
 */
void UeRrcTask::resetChoRuntimeState()
{
    if (m_choCandidates.empty())
        return;

    for (auto &cand : m_choCandidates)
    {
        cand.triggerMargin = 0.0;

    }

    m_logger->debug("CHO runtime state reset for %zu candidate(s)",
                    m_choCandidates.size());
}


} // namespace nr::ue