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
#include <unordered_set>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>

#include <asn/rrc/ASN_RRC_ConditionalReconfiguration.h>
#include <asn/rrc/ASN_RRC_CondReconfigToAddMod.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1530-IEs.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_SpCellConfig.h>
#include <asn/rrc/ASN_RRC_ReconfigurationWithSync.h>
#include <asn/rrc/ASN_RRC_ServingCellConfigCommon.h>

// Default T1 duration (ms) when not derivable from the message.
static constexpr int DEFAULT_T1_DURATION_MS = 1000;
static constexpr int MIN_COND_RECONFIG_ID = 1;
static constexpr int MAX_COND_RECONFIG_ID = 8;
static constexpr int MIN_MEAS_ID = 1;
static constexpr int MAX_MEAS_ID = 64;
static constexpr int MAX_COND_EXEC_MEAS_IDS = 2;
static constexpr int MAX_COND_RECONFIG_REMOVE_ENTRIES = 64;
static constexpr int MAX_COND_RECONFIG_ADDMOD_ENTRIES = 64;
static constexpr int MAX_COND_RRC_RECONFIG_BYTES = 16384;

// T304 enum → milliseconds (mirrors reconfig.cpp helper).
static int t304EnumToMs(long t304)
{
    static const int table[] = {50, 100, 150, 200, 500, 1000, 2000, 10000};
    if (t304 >= 0 && t304 < 8)
        return table[t304];
    return 1000;
}

namespace nr::ue
{

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static const char *eventTypeName(EChoEventType t)
{
    switch (t)
    {
    case EChoEventType::T1: return "T1";
    case EChoEventType::A2: return "A2";
    case EChoEventType::A3: return "A3";
    case EChoEventType::A5: return "A5";
    case EChoEventType::D1: return "D1";
    default: return "?";
    }
}

static const char *d1RefTypeName(ED1ReferenceType t)
{
    switch (t)
    {
    case ED1ReferenceType::Fixed: return "fixed";
    case ED1ReferenceType::Nadir: return "nadir";
    default: return "?";
    }
}

// Build a human-readable condition group string, e.g. "(T1 AND A3)".
static std::string conditionGroupStr(const std::vector<ChoCondition> &conds)
{
    if (conds.empty())
        return "(empty)";
    std::string s = "(";
    for (size_t i = 0; i < conds.size(); i++)
    {
        if (i > 0) s += " AND ";
        s += eventTypeName(conds[i].eventType);
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

/* ================================================================== */
/*  Parse ConditionalReconfiguration from ASN.1 extension chain       */
/* ================================================================== */

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
                                   "in active MeasConfig – skipping this condition",
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

                // build condition from reportConfig parameters
                auto &rc = itRc->second;
                ChoCondition cond{};

                switch (rc.event)
                {
                case EMeasEvent::A2:
                    cond.eventType = EChoEventType::A2;
                    cond.a2Threshold = rc.a2Threshold;
                    cond.hysteresis = rc.hysteresis;
                    cond.timeToTriggerMs = rc.timeToTriggerMs;
                    break;
                case EMeasEvent::A3:
                    cond.eventType = EChoEventType::A3;
                    cond.a3Offset = rc.a3Offset;
                    cond.a3Hysteresis = rc.hysteresis;
                    cond.hysteresis = rc.hysteresis;
                    cond.timeToTriggerMs = rc.timeToTriggerMs;
                    break;
                case EMeasEvent::A5:
                    cond.eventType = EChoEventType::A5;
                    cond.a5Threshold1 = rc.a5Threshold1;
                    cond.a5Threshold2 = rc.a5Threshold2;
                    cond.a5Hysteresis = rc.hysteresis;
                    cond.hysteresis = rc.hysteresis;
                    cond.timeToTriggerMs = rc.timeToTriggerMs;
                    break;
                case EMeasEvent::D1:
                    cond.eventType = EChoEventType::D1;
                    cond.d1ReferenceType = rc.d1ReferenceType;
                    cond.d1RefX = rc.d1RefX;
                    cond.d1RefY = rc.d1RefY;
                    cond.d1RefZ = rc.d1RefZ;
                    cond.d1ThresholdM = rc.d1ThresholdM;
                    cond.hysteresis = rc.hysteresis;
                    cond.timeToTriggerMs = rc.timeToTriggerMs;
                    break;
                default:
                    m_logger->warn("CHO candidate %d: MeasId %d has unsupported event type – "
                                   "skipping condition",
                                   cand.candidateId, measId);
                    continue;
                }

                m_logger->info("CHO candidate %d: %s condition from MeasId %d (ttt=%dms)",
                               cand.candidateId, eventTypeName(cond.eventType),
                               measId, cond.timeToTriggerMs);

                // add condition to candidate's condition group
                cand.conditions.push_back(std::move(cond));
            }
        }

        // If condExecutionCond was present but did not resolve to usable conditions,
        // reject the candidate
        if (cand.conditions.empty())
        {
            m_logger->warn("CHO candidate %d: condExecutionCond has no usable MeasIds; skipping candidate",
                            cand.candidateId);
            skippedCount++;
            continue;

            // Alternate treatment - always add a default timer condition if no valid conditions were found
            // ChoCondition t1Cond{};
            // t1Cond.eventType = EChoEventType::T1;
            // t1Cond.t1DurationMs = DEFAULT_T1_DURATION_MS;
            // cand.conditions.push_back(std::move(t1Cond));
            // m_logger->info("CHO candidate %d: no condExecutionCond – using T1 fallback (%dms)",
            //                cand.candidateId, DEFAULT_T1_DURATION_MS);
        }

        // Decode the nested RRCReconfiguration from condRRCReconfig to extract the target PCI, C-RNTI, and T304 for 
        //  the candidate target cell.
        if (!item->condRRCReconfig || item->condRRCReconfig->size == 0)
        {
            m_logger->warn("CHO candidate %d: missing condRRCReconfig – skipping",
                           cand.candidateId);
            skippedCount++;
            continue;
        }

        if (item->condRRCReconfig->size > MAX_COND_RRC_RECONFIG_BYTES)
        {
            m_logger->warn("CHO candidate %d: condRRCReconfig too large (%zu bytes) – skipping",
                           cand.candidateId, static_cast<size_t>(item->condRRCReconfig->size));
            skippedCount++;
            continue;
        }

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
                            cand.targetPci =
                                (rws->spCellConfigCommon && rws->spCellConfigCommon->physCellId)
                                    ? static_cast<int>(*rws->spCellConfigCommon->physCellId)
                                    : -1;
                            cand.newCRNTI = static_cast<int>(rws->newUE_Identity);
                            cand.t304Ms = t304EnumToMs(rws->t304);
                            foundRWS = true;
                        }
                        asn::Free(asn_DEF_ASN_RRC_CellGroupConfig, cellGroupConfig);
                    }
                }
            }
        }

        asn::Free(asn_DEF_ASN_RRC_RRCReconfiguration, innerReconfig);

        if (!foundRWS)
        {
            m_logger->warn("CHO candidate %d: no ReconfigurationWithSync in nested reconfig "
                           "– skipping", cand.candidateId);
            skippedCount++;
            continue;
        }

        // --- Initialise runtime state ---
        cand.executed = false;

        m_logger->info("CHO candidate %d added: targetPCI=%d newC-RNTI=%d t304=%dms "
                       "conditions=%s priority=%d",
                       cand.candidateId, cand.targetPci, cand.newCRNTI, cand.t304Ms,
                       conditionGroupStr(cand.conditions).c_str(),
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

/* ================================================================== */
/* [Legacy - now Deprecated]                                          */
/*  DL_CHO custom channel handler (test / fallback path) – v2         */
/* ================================================================== */

void UeRrcTask::handleChoConfiguration(const OctetString &pdu)
{
    // V2 binary protocol for injecting CHO candidates with arbitrary
    // condition groups from the test harness.
    //
    // Wire format (little-endian):
    //   [0..3]   numCandidates (uint32)
    //   Per candidate (variable size):
    //     [0..3]   candidateId        (int32)
    //     [4..7]   targetPci          (int32)
    //     [8..11]  newCRNTI           (int32)
    //     [12..15] t304Ms             (int32)
    //     [16..19] executionPriority  (int32)   -1 = unset → INT_MAX
    //     [20..23] numConditions      (uint32)
    //     Per condition (56 bytes):
    //       [0..3]   eventType        (int32)  0=T1,1=A2,2=A3,3=A5,4=D1-fixed,5=D1-nadir
    //       [4..7]   intParam1        (int32)
    //       [8..11]  intParam2        (int32)
    //       [12..15] intParam3        (int32)
    //       [16..19] timeToTriggerMs  (int32)
    //       [20..23] reserved         (int32)
    //       [24..31] floatParam1      (double)
    //       [32..39] floatParam2      (double)
    //       [40..47] floatParam3      (double)
    //       [48..55] floatParam4      (double)

    static constexpr size_t CONDITION_SIZE = 56;
    static constexpr size_t CANDIDATE_HEADER_SIZE = 24;

    const uint8_t *p = pdu.data();
    size_t len = static_cast<size_t>(pdu.length());

    if (len < 4)
    {
        m_logger->err("DL_CHO: PDU too short (%zu bytes)", len);
        return;
    }

    auto readI32 = [&](size_t off) -> int32_t {
        int32_t v;
        memcpy(&v, p + off, 4);
        return v;
    };

    auto readU32 = [&](size_t off) -> uint32_t {
        uint32_t v;
        memcpy(&v, p + off, 4);
        return v;
    };

    auto readF64 = [&](size_t off) -> double {
        double v;
        memcpy(&v, p + off, 8);
        return v;
    };

    uint32_t numCandidates = readU32(0);
    size_t offset = 4;
    int addedCount = 0;
    int updatedCount = 0;
    int skippedCount = 0;

    for (uint32_t i = 0; i < numCandidates; i++)
    {
        if (offset + CANDIDATE_HEADER_SIZE > len)
        {
            m_logger->err("DL_CHO: truncated candidate header at offset %zu", offset);
            return;
        }

        ChoCandidate cand{};
        cand.candidateId       = readI32(offset + 0);
        cand.targetPci         = readI32(offset + 4);
        cand.newCRNTI          = readI32(offset + 8);
        cand.t304Ms            = readI32(offset + 12);
        int32_t prio           = readI32(offset + 16);
        cand.executionPriority = (prio < 0) ? 0x7FFFFFFF : prio;
        uint32_t numConditions = readU32(offset + 20);
        offset += CANDIDATE_HEADER_SIZE;

        if (offset + numConditions * CONDITION_SIZE > len)
        {
            m_logger->err("DL_CHO: truncated conditions for candidate %d", cand.candidateId);
            return;
        }

        for (uint32_t j = 0; j < numConditions; j++)
        {
            size_t coff = offset + j * CONDITION_SIZE;

            ChoCondition cond{};
            int32_t evtType     = readI32(coff + 0);
            int32_t ip1         = readI32(coff + 4);
            int32_t ip2         = readI32(coff + 8);
            int32_t ip3         = readI32(coff + 12);
            cond.timeToTriggerMs = readI32(coff + 16);
            // [20..23] reserved
            double fp1          = readF64(coff + 24);
            double fp2          = readF64(coff + 32);
            double fp3          = readF64(coff + 40);
            double fp4          = readF64(coff + 48);

            switch (evtType)
            {
            case 0: // T1
                cond.eventType = EChoEventType::T1;
                cond.t1DurationMs = ip1;
                break;
            case 1: // A2
                cond.eventType = EChoEventType::A2;
                cond.a2Threshold = ip1;
                cond.hysteresis = ip2;
                break;
            case 2: // A3
                cond.eventType = EChoEventType::A3;
                cond.a3Offset = ip1;
                cond.a3Hysteresis = ip2;
                break;
            case 3: // A5
                cond.eventType = EChoEventType::A5;
                cond.a5Threshold1 = ip1;
                cond.a5Threshold2 = ip2;
                cond.a5Hysteresis = ip3;
                break;
            case 4: // D1
                cond.eventType = EChoEventType::D1;
                cond.d1ReferenceType = ED1ReferenceType::Fixed;
                cond.d1RefX = fp1;
                cond.d1RefY = fp2;
                cond.d1RefZ = fp3;
                cond.d1ThresholdM = fp4;
                break;
            case 5: // D1 legacy alias, interpreted as nadir-reference D1
                cond.eventType = EChoEventType::D1;
                cond.d1ReferenceType = ED1ReferenceType::Nadir;
                cond.d1ThresholdM = fp1;
                cond.d1ElevationMinDeg = fp2;
                break;
            default:
                m_logger->warn("DL_CHO candidate %d: unknown eventType %d – skipping condition",
                               cand.candidateId, evtType);
                continue;
            }

            cand.conditions.push_back(std::move(cond));
        }

        offset += numConditions * CONDITION_SIZE;

        if (cand.conditions.empty())
        {
            m_logger->warn("DL_CHO candidate %d: no usable conditions after decoding; skipping candidate",
                           cand.candidateId);
            skippedCount++;
            continue;
        }

        // Initialise runtime state
        cand.executed = false;

        m_logger->info("DL_CHO candidate %d: targetPCI=%d conditions=%s priority=%d",
                       cand.candidateId, cand.targetPci,
                       conditionGroupStr(cand.conditions).c_str(),
                       cand.executionPriority);

        if (cand.conditions.size() == 1)
        {
            auto &c = cand.conditions[0];
            if (c.eventType == EChoEventType::T1)
                m_logger->info("DL_CHO candidate %d: T1 duration=%dms",
                               cand.candidateId, c.t1DurationMs);
            else if (c.eventType == EChoEventType::D1)
                m_logger->info("DL_CHO candidate %d: D1 refType=%s ref=(%.1f, %.1f, %.1f) "
                               "threshold=%.1fm",
                               cand.candidateId, d1RefTypeName(c.d1ReferenceType),
                               c.d1RefX, c.d1RefY, c.d1RefZ, c.d1ThresholdM);
        }

        bool wasUpdated = upsertChoCandidate(m_choCandidates, std::move(cand));
        if (wasUpdated)
            updatedCount++;
        else
            addedCount++;
    }

    m_logger->info("DL_CHO applied: total=%u added=%d updated=%d skipped=%d activeCandidates=%zu",
                   numCandidates, addedCount, updatedCount, skippedCount, m_choCandidates.size());
}

/* ================================================================== */
/*  Evaluate a single atomic condition                                */
/* ================================================================== */

// Returns the "margin" by which the condition is exceeded (>0 means exceeded).
// For conditions that are simply met/not-met, returns 1.0 on met, -1.0 on not.
static double evaluateConditionRaw(
    ChoCondition &cond,
    int64_t now,
    int servingRsrp,
    int targetRsrp,
    double ueDistance, // Distance from UE to the condition's D1 reference point
    Logger *logger,
    int candidateId)
{
    switch (cond.eventType)
    {
    case EChoEventType::T1:
    {
        if (cond.t1StartTime == 0)
        {
            cond.t1StartTime = now;
            logger->debug("CHO candidate %d: T1 started (%dms)", candidateId, cond.t1DurationMs);
        }
        int64_t elapsed = now - cond.t1StartTime;
        if (elapsed >= cond.t1DurationMs)
        {
            if (!cond.satisfied)
                logger->info("CHO candidate %d: T1 expired (elapsed=%ldms)",
                             candidateId, static_cast<long>(elapsed));
            return 1.0;
        }
        return -1.0;
    }

    case EChoEventType::A2:
    {
        // Align with measurement.cpp evaluateA2():
        // entering when serving < threshold - hysteresis.
        double margin = (cond.a2Threshold - cond.hysteresis) - servingRsrp;
        return margin; // >0 means serving is bad enough
    }

    case EChoEventType::A3:
    {
        // Align with measurement.cpp evaluateA3_cell():
        // entering when neighbor > serving + offset + hysteresis.
        double margin = targetRsrp - (servingRsrp + cond.a3Offset + cond.a3Hysteresis);
        return margin; // >0 means neighbor is good enough
    }

    case EChoEventType::A5:
    {
        // Align with measurement.cpp evaluateA5_*():
        // serving < threshold1 - hysteresis AND neighbor > threshold2 + hysteresis.
        double margin1 = (cond.a5Threshold1 - cond.a5Hysteresis) - servingRsrp;
        double margin2 = targetRsrp - (cond.a5Threshold2 + cond.a5Hysteresis);
        // Both must be positive; return the lesser (bottleneck)
        if (margin1 > 0 && margin2 > 0)
            return std::min(margin1, margin2);
        return std::min(margin1, margin2); // will be <=0
    }

    case EChoEventType::D1:
    {
        // D1 (3GPP Event D1): distance to configured reference exceeds threshold.
        // Margin = distance − threshold.  Positive means UE is beyond threshold.
        double thresh = cond.d1ResolvedThreshM;
        if (thresh <= 0)
            return -1e6;  // No valid threshold — cannot evaluate
        return ueDistance - thresh;  // positive when distance exceeds threshold
    }

    default:
        return -1.0;
    }
}

// Evaluate a single condition with time-to-trigger.
// Returns the margin (>0 = satisfied after TTT) and updates cond.satisfied.
static double evaluateConditionWithTTT(
    ChoCondition &cond,
    int64_t now,
    int servingRsrp,
    int targetRsrp,
    double ueDistance,
    Logger *logger,
    int candidateId)
{
    double margin = evaluateConditionRaw(cond, now, servingRsrp, targetRsrp,
                                          ueDistance, logger, candidateId);
    bool rawMet = (margin > 0);

    // T1 conditions don't use TTT — they have their own timer built in.
    if (cond.eventType == EChoEventType::T1)
    {
        cond.satisfied = rawMet;
        return rawMet ? margin : -1.0;
    }

    if (rawMet)
    {
        if (cond.enteringTimestamp == 0)
            cond.enteringTimestamp = now;

        int64_t held = now - cond.enteringTimestamp;
        if (held >= cond.timeToTriggerMs)
        {
            cond.satisfied = true;
            return margin;
        }
        else
        {
            cond.satisfied = false;
            return -1.0; // Not yet held long enough
        }
    }
    else
    {
        if (cond.enteringTimestamp != 0)
        {
            logger->debug("CHO candidate %d: %s condition lost, resetting TTT",
                          candidateId, eventTypeName(cond.eventType));
            cond.enteringTimestamp = 0;
        }
        cond.satisfied = false;
        return -1.0;
    }
}

/**
 * @brief Evaluates each CHO candidate for condition satisfaction.  For candidates whose conditions 
 *  are all satisfied, triggers handover to the candidate.  Applies tie-breaking logic if more than 
 *  one candidate is satisfied.
 * 
 *  This should get called periodically, just before the measurements check for normal measurement reporting,
 *  so that CHO candidates get evaluated before the normal event logic.
 * 
 * @return true 
 * @return false 
 */
bool UeRrcTask::evaluateChoCandidates()
{
    // if no candidates, or if handover is already in progress, or if measurement evaluation is suspended, then skip
    if (m_choCandidates.empty() || m_handoverInProgress || m_measurementEvalSuspended)
        return false;

    // current time in milliseconds
    int64_t now = utils::CurrentTimeMillis();

    // copy the current cell power measurements under lock to avoid holding the lock while evaluating conditions
    std::map<int, int> allMeas;
    {
        std::shared_lock lock(m_base->cellDbMeasMutex);
        allMeas = m_base->cellDbMeas;
    }

    int servingCellId = m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });
    int servingRsrp = getServingCellRsrp(servingCellId, allMeas);

    // Pre-compute UE position (needed for D1 conditions)
    UePosition uePos = getUePosition();

    // for D1 - reference position for satellites
    //   used in all D1 condition checks that use nadir
    EcefPosition servingPosNadir{0, 0, 0};

    bool servingPosValid = false;
   
    auto servingIt = m_cellDesc.find(servingCellId);
    if (servingIt != m_cellDesc.end() && servingIt->second.sib19.hasSib19)
    {
        auto &sib19 = servingIt->second.sib19;
        //int64_t relativeNow = now - m_startedTime;

        if (isSib19EphemerisValid(sib19, now))
        {
            
            // get satellite's current position using current SIB19 ephemeris and
            //   motion extrapolation based on time delta from SIB19 epoch to now

            double dtSec = (now - (sib19.ntnConfig.epochTime)*10) / 1000.0;  // sib19 epoch is counted in 10ms units
            double satX, satY, satZ;
            // if position is stored in SIB19 as ECEF position + velocity, apply delta based on time 
            if (sib19.ntnConfig.ephemerisInfo.type == EEphemerisType::POSITION_VELOCITY)
            {
                extrapolateSatelliteEcefPosition(
                    sib19.ntnConfig.ephemerisInfo.posVel,
                    dtSec, satX, satY, satZ);

                // convert position to nadir for distance calculations
                servingPosNadir = computeNadir(satX, satY, satZ);
                servingPosValid = true;

            }
            // otherwise it is stored as orbital parameters that need to be converted to ECEF position 
            //   using the SIB19 epoch as reference time
            else 
            {
                // not used in the simulator
            }
        }
    }

    // Phase 1: Evaluate all conditions for all candidates.
    // Collect candidates whose entire condition group is satisfied.
    struct TriggeredCandidate
    {
        int index;           // index into m_choCandidates
        double minMargin;    // minimum margin across all conditions (bottleneck)
        int targetRsrp;
    };
    std::vector<TriggeredCandidate> triggered;

    for (size_t ci = 0; ci < m_choCandidates.size(); ci++)
    {
        auto &cand = m_choCandidates[ci];
        if (cand.executed || cand.conditions.empty())
            continue;

        // Find target cell RSRP for this candidate (used by A3, A5 conditions).
        int targetRsrp = cons::MIN_RSRP;
        for (auto &[cellId, meas] : allMeas)
        {
            if (cellId == cand.targetPci)
            {
                targetRsrp = meas;
                break;
            }
            if (m_cellDesc.count(cellId))
            {
                int nciLow = static_cast<int>(m_cellDesc[cellId].sib1.nci & 0x3FF);
                if (nciLow == cand.targetPci)
                {
                    targetRsrp = meas;
                    break;
                }
            }
        }

        bool allSatisfied = true;
        double minMargin = 1e9;

        for (auto &cond : cand.conditions)
        {
            // For D1 conditions, calculate the serving cell distance
            double servingD1DistanceM = 0.0;
            if (cond.eventType == EChoEventType::D1)
            {
                // for fixed reference, calculate distance from UE to fixed point
                if (cond.d1ReferenceType == ED1ReferenceType::Fixed)
                {
                    EcefPosition ref{cond.d1RefX, cond.d1RefY, cond.d1RefZ};
                    servingD1DistanceM = ecefDistance(uePos.ecef, ref);
                    servingPosValid = true;
                }
                // for nadir reference, calculate distance from UE to satellite nadir position (if available)
                else if (cond.d1ReferenceType == ED1ReferenceType::Nadir && servingPosValid)
                {
                    servingD1DistanceM = ecefDistance(uePos.ecef, servingPosNadir);
                }
                // if not fixed and no valid distance found for serving cell — cannot evaluate D1 conditions for this candidate
                //  so leave distance a 0, which will cause teh condition to be not satisfied and the TTT timer to reset
                else {

                    m_logger->debug("CHO candidate %d: D1 skipped – serving position unavailable",
                                        cand.candidateId);
                }

                // D1 entering condition: distance > threshold + hysteresis.
                cond.d1ResolvedThreshM = cond.d1ThresholdM + static_cast<double>(cond.hysteresis);
            }

            // evaluate the condition, taking into account TTT
            //   will set the cond.satisfied flag for the condition
            //   Will return the margin by which the condition is satisfied (positive means satisfied after TTT),
            double margin = evaluateConditionWithTTT(
                cond, now, servingRsrp, targetRsrp, servingD1DistanceM,
                m_logger.get(), cand.candidateId);

            if (!cond.satisfied)
            {
                allSatisfied = false;
                // Don't break — continue evaluating remaining conditions
                // so their TTT timers keep ticking.
            }
            else
            {
                minMargin = std::min(minMargin, margin);
            }
        }

        if (allSatisfied)
        {
            // Build log message showing which conditions were met.
            std::string condStr = conditionGroupStr(cand.conditions);
            m_logger->info("CHO candidate %d: all conditions met %s – eligible for execution",
                           cand.candidateId, condStr.c_str());

            cand.triggerMargin = minMargin;
            triggered.push_back({static_cast<int>(ci), minMargin, targetRsrp});
        }
    }

    // if nothing triggered, we're done
    if (triggered.empty())
        return false;

    // Phase 2: Select best candidate using priority + tie-breaking.
    // Sort by: (1) executionPriority ASC, (2) triggerMargin DESC,
    //          (3) targetRsrp DESC, (4) config order ASC.
    std::sort(triggered.begin(), triggered.end(),
              [this](const TriggeredCandidate &a, const TriggeredCandidate &b)
    {
        auto &candA = m_choCandidates[a.index];
        auto &candB = m_choCandidates[b.index];

        // 1. Priority (lower value = higher priority)
        if (candA.executionPriority != candB.executionPriority)
            return candA.executionPriority < candB.executionPriority;

        // 2. Greatest trigger margin
        if (std::abs(a.minMargin - b.minMargin) > 0.01)
            return a.minMargin > b.minMargin;

        // 3. Highest target RSRP
        if (a.targetRsrp != b.targetRsrp)
            return a.targetRsrp > b.targetRsrp;

        // 4. Configuration order (earliest index)
        return a.index < b.index;
    });

    // Execute the winning candidate.
    auto &winner = m_choCandidates[triggered[0].index];

    if (triggered.size() > 1)
    {
        std::string ranking;
        ranking.reserve(triggered.size() * 32);
        for (size_t i = 0; i < triggered.size(); i++)
        {
            auto &cand = m_choCandidates[triggered[i].index];
            if (i > 0)
                ranking += ", ";
            ranking += "#" + std::to_string(cand.candidateId);
            ranking += "(p=" + std::to_string(cand.executionPriority);
            ranking += " m=" + std::to_string(static_cast<int>(triggered[i].minMargin));
            ranking += " rsrp=" + std::to_string(triggered[i].targetRsrp) + ")";
        }

        m_logger->info("CHO tie-break: %zu candidates triggered, ranking=%s",
                       triggered.size(), ranking.c_str());
    }

    std::string condStr = conditionGroupStr(winner.conditions);
    m_logger->info("CHO selection: winner=%d priority=%d margin=%.2f targetRsrp=%d conditions=%s",
                   winner.candidateId,
                   winner.executionPriority,
                   triggered[0].minMargin,
                   triggered[0].targetRsrp,
                   condStr.c_str());

    executeChoCandidate(winner);

    return true;
}

/* ================================================================== */
/*  Execute a CHO candidate – trigger the actual handover              */
/* ================================================================== */

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

/* ================================================================== */
/*  Cancel all pending CHO candidates                                 */
/* ================================================================== */

void UeRrcTask::cancelAllChoCandidates()
{
    if (m_choCandidates.empty())
        return;

    m_logger->info("Cancelling %d CHO candidate(s)", static_cast<int>(m_choCandidates.size()));
    m_choCandidates.clear();
}

/* ================================================================== */
/*  Reset CHO runtime state (keep configured candidates)              */
/* ================================================================== */

void UeRrcTask::resetChoRuntimeState()
{
    if (m_choCandidates.empty())
        return;

    for (auto &cand : m_choCandidates)
    {
        cand.triggerMargin = 0.0;

        for (auto &cond : cand.conditions)
        {
            cond.enteringTimestamp = 0;
            cond.t1StartTime = 0;
            cond.satisfied = false;
            cond.d1ResolvedThreshM = 0.0;
        }
    }

    m_logger->debug("CHO runtime state reset for %zu candidate(s)",
                    m_choCandidates.size());
}

/* ================================================================== */
/*  UE Position retrieval (for D1 distance-based events)              */
/* ================================================================== */

UePosition UeRrcTask::getUePosition() const
{
    if (m_base->config->initialPosition.has_value())
        return *m_base->config->initialPosition;

    UePosition pos{};
    pos.geo = {0.0, 0.0, 0.0};
    pos.ecef = geoToEcef(pos.geo);
    return pos;
}

} // namespace nr::ue