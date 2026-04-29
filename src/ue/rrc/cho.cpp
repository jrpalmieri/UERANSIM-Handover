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
#include "sib19.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <limits>
#include <unordered_set>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <lib/sat/sat_calc.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <lib/sat/sat_time.hpp>

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

using nr::sat::EcefPosition;
using nr::sat::ExtrapolateEcefPosition;
using nr::sat::ComputeNadir;

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

/**
 * @brief Selects the best satellite based on priority scores.
 * Only used if multiple candidate target gnbs are satellites.
 * 
 * @param servingCellId 
 * @param satPcis 
 * @return std::vector<nr::sat::SatPriorityScore> 
 */
std::vector<nr::sat::SatPriorityScore> UeRrcTask::selectBestSatellite(
    int servingCellId,
    const std::unordered_set<int> &satPcis)
{

    std::vector<nr::sat::SatPriorityScore> priorities{};

    // sanity check
    if (satPcis.empty())
        return priorities;

    // get current sat time
    const int64_t satNowMs = m_base->satTime != nullptr ? m_base->satTime->CurrentSatTimeMillis()
                                                         : utils::CurrentTimeMillis();

    // failsafe to determine PCI
    int servingPci = servingCellId;
    auto servingIt = m_cellDesc.find(servingCellId);
    if (servingIt != m_cellDesc.end() && servingIt->second.sib1.hasSib1)
        servingPci = cons::getPciFromNci(servingIt->second.sib1.nci);

    // dummy routing until actual propagators are built:
    for (auto it = satPcis.begin(); it != satPcis.end(); ++it)
    {
        int pci = *it;
        priorities.emplace_back(pci, 0); // dummy score of 0 for now
    }

    return priorities;

    // auto stateResolver = [&](int pci, int offsetSec, nr::sat::SatEcefState &state) -> bool {
    //     const int64_t targetSatMs = satNowMs + static_cast<int64_t>(offsetSec) * 1000LL;

    //     const UeCellDesc *descForPci = nullptr;
    //     std::optional<Sib19Info::PciEntry> entryForPci{};

    //     for (const auto &[cellId, desc] : m_cellDesc)
    //     {
    //         if (!desc.sib19.hasSib19)
    //             continue;

    //         bool matches = false;
    //         if (cellId == pci)
    //             matches = true;
    //         else if (desc.sib1.hasSib1 && cons::getPciFromNci(desc.sib1.nci) == pci)
    //             matches = true;
    //         if (desc.sib19.entriesByPci.count(pci) != 0)
    //             matches = true;

    //         if (!matches)
    //             continue;

    //         descForPci = &desc;
    //         auto itEntry = desc.sib19.entriesByPci.find(pci);
    //         if (itEntry != desc.sib19.entriesByPci.end())
    //             entryForPci = itEntry->second;
    //         break;
    //     }

    //     if (descForPci == nullptr)
    //         return false;

    //     const Sib19Info &sib19 = descForPci->sib19;
    //     if (!isSib19EphemerisValid(sib19, satNowMs))
    //         return false;

    //     const NtnConfig &ntnCfg = entryForPci.has_value() ? entryForPci->ntnConfig : sib19.ntnConfig;
    //     const int64_t epochMs = static_cast<int64_t>(ntnCfg.epochTime) * 10LL;

    //     if (ntnCfg.ephemerisInfo.type == EEphemerisType::POSITION_VELOCITY)
    //     {
    //         const auto &pv = ntnCfg.ephemerisInfo.posVel;
    //         const double dtSec = static_cast<double>(targetSatMs - epochMs) / 1000.0;
    //         state.pos = ExtrapolateEcefPosition(EcefPosition{pv.positionX, pv.positionY, pv.positionZ},
    //                                             EcefPosition{pv.velocityVX, pv.velocityVY, pv.velocityVZ},
    //                                             dtSec);
    //     }
    //     else if (ntnCfg.ephemerisInfo.type == EEphemerisType::ORBITAL_PARAMETERS)
    //     {
    //         const auto &orb = ntnCfg.ephemerisInfo.orbital;
    //         nr::sat::KeplerianElementsRaw raw{};
    //         raw.semiMajorAxis = orb.semiMajorAxis;
    //         raw.eccentricity = orb.eccentricity;
    //         raw.periapsis = orb.periapsis;
    //         raw.longitude = orb.longitude;
    //         raw.inclination = orb.inclination;
    //         raw.meanAnomaly = orb.meanAnomaly;
    //         state.pos = nr::sat::PropagateKeplerianToEcef(raw, epochMs, targetSatMs);
    //     }
    //     else
    //     {
    //         return false;
    //     }

    //     state.nadir = ComputeNadir(state.pos);
    //     return true;
    // };

    // auto endpointResolver = [&](int pci, nr::sat::NeighborEndpoint &endpoint) -> bool {
    //     if (m_base == nullptr || m_base->g_allCellMeasurements == nullptr)
    //         return false;

    //     int resolvedCellId = -1;
    //     if (m_cellDesc.count(pci) != 0)
    //     {
    //         resolvedCellId = pci;
    //     }
    //     else
    //     {
    //         for (const auto &[cellId, desc] : m_cellDesc)
    //         {
    //             if (desc.sib1.hasSib1 && cons::getPciFromNci(desc.sib1.nci) == pci)
    //             {
    //                 resolvedCellId = cellId;
    //                 break;
    //             }
    //         }
    //     }

    //     std::shared_lock lock(m_base->g_allCellMeasurements->cellMeasurementsMutex);
    //     for (const auto &cm : m_base->g_allCellMeasurements->cellMeasurements)
    //     {
    //         if (cm.cellId != pci && cm.cellId != resolvedCellId)
    //             continue;
    //         if (cm.ip.empty())
    //             continue;

    //         endpoint.ipAddress = cm.ip;
    //         endpoint.port = cons::RadioLinkPort;
    //         return true;
    //     }
    //     return false;
    // };

    // static constexpr int MAX_LOOKAHEAD_SEC = 7200;
    // static constexpr int UE_ELEVATION_MIN_DEG = 5;

    // const EcefPosition ueEcef = EcefPosition{m_base->UeLocation};
    // return nr::sat::PrioritizeTargetSats(servingPci,
    //                                              satPciList,
    //                                              ueEcef,
    //                                              0,
    //                                              UE_ELEVATION_MIN_DEG,
    //                                              MAX_LOOKAHEAD_SEC);



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

    std::vector<int> triggeredCandidateIndices;  // vector indices of candidates whose conditions are currently satisfied
    for (int i = 0; i < static_cast<int>(m_choCandidates.size()); i++)
    {
        const auto &cand = m_choCandidates[i];
        m_logger->debug("Evaluating CHO candidate %d: targetPCI=%d conditions=%s executed=%s",
                        cand.candidateId, cand.targetPci, conditionGroupStr(cand.measIds, m_measConfig).c_str(),
                        cand.executed ? "yes" : "no");

        if (cand.executed)
        {
            m_logger->debug("CHO candidate %d already executed or cancelled; skipping",
                            cand.candidateId);
            continue;
        }
        
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

            triggeredCandidateIndices.push_back(i);
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
        auto ci = triggeredCandidateIndices[0];
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
    std::map<int, int> PciToVecIdx;
    std::unordered_set<int> nonSatPcis;
    for (int ci : triggeredCandidateIndices)
    {
        auto &cand = m_choCandidates[ci];
        PciToVecIdx[cand.targetPci] = ci;
        bool isSatCandidate = false;
        for (const auto &[cellId, desc] : m_cellDesc)
        {
            (void)cellId;
            if (!desc.sib19.hasSib19)
                continue;
            if (desc.sib19.entriesByPci.count(cand.targetPci) != 0)
            {
                isSatCandidate = true;
                break;
            }
            if (desc.sib1.hasSib1 && cons::getPciFromNci(desc.sib1.nci) == cand.targetPci)
            {
                isSatCandidate = true;
                break;
            }
        }

        if (isSatCandidate)
        {
            satPcis.insert(cand.targetPci);
        }
        else
        {
            nonSatPcis.insert(cand.targetPci);
        }
    }

    int bestSatPci = -1;
    const auto prioritizedSat = selectBestSatellite(servingCellId, satPcis);
    if (!prioritizedSat.empty())
        bestSatPci = prioritizedSat.front().pci;

    // for non-SATs, we can apply the normal RSRP-based tie-breaking logic (highest RSRP wins)
    int bestNonSatPci = selectBestTerrestrial(nonSatPcis, allMeas);

    // for now, we pick the best SAT candidate if one exists, otherwise we pick the best non-SAT candidate.
    int bestPci = bestSatPci > 0 ? bestSatPci : bestNonSatPci;
    if (bestPci <= 0 || PciToVecIdx.count(bestPci) == 0)
    {
        m_logger->warn("CHO tie-break could not choose a valid candidate");
        return false;
    }

    auto &foundCand = m_choCandidates[PciToVecIdx[bestPci]];
    m_logger->info("CHO candidate %d selected as best candidate; executing handover to PCI %d",
                    foundCand.candidateId, foundCand.targetPci);
    executeChoCandidate(foundCand);
    return true;

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