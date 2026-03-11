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
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <utils/common.hpp>

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
    case EChoEventType::D1_SIB19: return "D1_SIB19";
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

/* ================================================================== */
/*  Parse ConditionalReconfiguration from ASN.1 extension chain       */
/* ================================================================== */

void UeRrcTask::parseConditionalReconfiguration(
    const ASN_RRC_ConditionalReconfiguration *condReconfig)
{
    if (!condReconfig || !condReconfig->condReconfigToAddModList)
    {
        m_logger->warn("ConditionalReconfiguration: empty or missing addModList");
        return;
    }

    auto &addList = condReconfig->condReconfigToAddModList->list;
    m_logger->info("ConditionalReconfiguration received: %d candidate(s)", addList.count);

    for (int i = 0; i < addList.count; i++)
    {
        auto *item = addList.array[i];
        if (!item)
            continue;

        ChoCandidate cand{};
        cand.candidateId = static_cast<int>(item->condReconfigId);

        // --- Build condition group from condExecutionCond MeasIds ---
        // Per TS 38.331: multiple MeasIds in one condExecutionCond = AND logic.
        if (item->condExecutionCond)
        {
            auto &measIdList = item->condExecutionCond->list;
            for (int j = 0; j < measIdList.count; j++)
            {
                long *pMeasId = measIdList.array[j];
                if (!pMeasId)
                    continue;
                int measId = static_cast<int>(*pMeasId);

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
                    continue;

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
                default:
                    m_logger->warn("CHO candidate %d: MeasId %d has unsupported event type – "
                                   "skipping condition",
                                   cand.candidateId, measId);
                    continue;
                }

                m_logger->info("CHO candidate %d: %s condition from MeasId %d (ttt=%dms)",
                               cand.candidateId, eventTypeName(cond.eventType),
                               measId, cond.timeToTriggerMs);

                cand.conditions.push_back(std::move(cond));
            }
        }

        // If no conditions were parsed from condExecutionCond, add a T1 fallback.
        if (cand.conditions.empty())
        {
            ChoCondition t1Cond{};
            t1Cond.eventType = EChoEventType::T1;
            t1Cond.t1DurationMs = DEFAULT_T1_DURATION_MS;
            cand.conditions.push_back(std::move(t1Cond));
            m_logger->info("CHO candidate %d: no condExecutionCond – using T1 fallback (%dms)",
                           cand.candidateId, DEFAULT_T1_DURATION_MS);
        }

        // --- Decode the nested RRCReconfiguration from condRRCReconfig ---
        if (!item->condRRCReconfig || item->condRRCReconfig->size == 0)
        {
            m_logger->warn("CHO candidate %d: missing condRRCReconfig – skipping",
                           cand.candidateId);
            continue;
        }

        auto *innerReconfig = rrc::encode::Decode<ASN_RRC_RRCReconfiguration>(
            asn_DEF_ASN_RRC_RRCReconfiguration, *item->condRRCReconfig);

        if (!innerReconfig)
        {
            m_logger->err("CHO candidate %d: failed to UPER-decode condRRCReconfig",
                          cand.candidateId);
            continue;
        }

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
            continue;
        }

        // --- Initialise runtime state ---
        cand.executed = false;

        m_logger->info("CHO candidate %d added: targetPCI=%d newC-RNTI=%d t304=%dms "
                       "conditions=%s priority=%d",
                       cand.candidateId, cand.targetPci, cand.newCRNTI, cand.t304Ms,
                       conditionGroupStr(cand.conditions).c_str(),
                       cand.executionPriority);

        m_choCandidates.push_back(std::move(cand));
    }
}

/* ================================================================== */
/*  DL_CHO custom channel handler (test / fallback path) – v2        */
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
    //       [0..3]   eventType        (int32)  0=T1,1=A2,2=A3,3=A5,4=D1
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
                cond.d1RefX = fp1;
                cond.d1RefY = fp2;
                cond.d1RefZ = fp3;
                cond.d1ThresholdM = fp4;
                break;
            case 5: // D1_SIB19
                cond.eventType = EChoEventType::D1_SIB19;
                // intParam1 = flags: bit 0 = useNadir
                cond.d1sib19UseNadir = (ip1 & 0x01) != 0;
                // floatParam1 = threshold (< 0 means use SIB19's distanceThresh)
                cond.d1sib19ThresholdM = fp1;
                // floatParam2 = elevation minimum (< 0 = disabled)
                cond.d1sib19ElevationMinDeg = fp2;
                break;
            default:
                m_logger->warn("DL_CHO candidate %d: unknown eventType %d – skipping condition",
                               cand.candidateId, evtType);
                continue;
            }

            cand.conditions.push_back(std::move(cond));
        }

        offset += numConditions * CONDITION_SIZE;

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
                m_logger->info("DL_CHO candidate %d: D1 ref=(%.1f, %.1f, %.1f) threshold=%.1fm",
                               cand.candidateId, c.d1RefX, c.d1RefY, c.d1RefZ, c.d1ThresholdM);
            else if (c.eventType == EChoEventType::D1_SIB19)
                m_logger->info("DL_CHO candidate %d: D1_SIB19 nadir=%s threshold=%.1fm elevMin=%.1f°",
                               cand.candidateId,
                               c.d1sib19UseNadir ? "true" : "false",
                               c.d1sib19ThresholdM,
                               c.d1sib19ElevationMinDeg);
        }

        m_choCandidates.push_back(std::move(cand));
    }

    m_logger->info("DL_CHO: %d candidate(s) configured", numCandidates);
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
        // A2: serving < threshold + hysteresis  (entering condition)
        double margin = (cond.a2Threshold + cond.hysteresis) - servingRsrp;
        return margin; // >0 means serving is bad enough
    }

    case EChoEventType::A3:
    {
        // A3: neighbor > serving + offset − hysteresis
        double margin = targetRsrp - (servingRsrp + cond.a3Offset - cond.a3Hysteresis);
        return margin; // >0 means neighbor is good enough
    }

    case EChoEventType::A5:
    {
        // A5: serving < threshold1 + hysteresis AND neighbor > threshold2 − hysteresis
        double margin1 = (cond.a5Threshold1 + cond.a5Hysteresis) - servingRsrp;
        double margin2 = targetRsrp - (cond.a5Threshold2 - cond.a5Hysteresis);
        // Both must be positive; return the lesser (bottleneck)
        if (margin1 > 0 && margin2 > 0)
            return std::min(margin1, margin2);
        return std::min(margin1, margin2); // will be <=0
    }

    case EChoEventType::D1:
    {
        // D1 (3GPP Event D1): distance to reference point exceeds threshold.
        // Margin = distance − threshold.  Positive means UE is beyond threshold.
        return ueDistance - cond.d1ThresholdM;
    }

    case EChoEventType::D1_SIB19:
    {
        // D1_SIB19: like D1, but reference point and threshold are derived
        // from SIB19 ephemeris data.  The resolved threshold is set by
        // evaluateChoCandidates() each cycle based on the condition config
        // or SIB19's distanceThresh.
        double thresh = cond.d1sib19ResolvedThreshM;
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

/* ================================================================== */
/*  Evaluate CHO candidates (called every machine-cycle)              */
/* ================================================================== */

void UeRrcTask::evaluateChoCandidates()
{
    if (m_choCandidates.empty() || m_handoverInProgress)
        return;

    int64_t now = utils::CurrentTimeMillis();
    std::map<int, int> allMeas;
    {
        std::shared_lock lock(m_base->cellDbMeasMutex);
        allMeas = m_base->cellDbMeas;
    }

    int servingCellId = m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });
    int servingRsrp = getServingCellRsrp(servingCellId, allMeas);

    // Pre-compute UE position (needed for D1 conditions)
    UePosition uePos = getUePosition();

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
        int targetRsrp = -200;
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

        // Compute distance for D1 conditions.
        // We compute it once per candidate; if multiple D1 conditions exist
        // with different reference points, each uses its own.
        bool allSatisfied = true;
        double minMargin = 1e9;

        for (auto &cond : cand.conditions)
        {
            // For D1 conditions, compute distance to this condition's reference point.
            double d1Dist = 0.0;
            if (cond.eventType == EChoEventType::D1)
            {
                EcefPosition ref{cond.d1RefX, cond.d1RefY, cond.d1RefZ};
                d1Dist = ecefDistance(uePos.ecef, ref);
            }
            else if (cond.eventType == EChoEventType::D1_SIB19)
            {
                // D1_SIB19: derive reference point from serving cell's SIB19 ephemeris.
                int servCellId = m_base->shCtx.currentCell.get<int>(
                    [](auto &v) { return v.cellId; });
                auto cellIt = m_cellDesc.find(servCellId);

                if (cellIt == m_cellDesc.end() || !cellIt->second.sib19.hasSib19)
                {
                    m_logger->debug("CHO candidate %d: D1_SIB19 skipped – no SIB19 data for cell %d",
                                    cand.candidateId, servCellId);
                    cond.d1sib19ResolvedThreshM = 0.0;
                    // Fall through with d1Dist=0 → will produce very negative margin
                }
                else
                {
                    auto &sib19 = cellIt->second.sib19;
                    int64_t relativeNow = now - m_startedTime;

                    if (!isSib19EphemerisValid(sib19, relativeNow))
                    {
                        m_logger->debug("CHO candidate %d: D1_SIB19 skipped – stale SIB19 (cell %d)",
                                        cand.candidateId, servCellId);
                        cond.d1sib19ResolvedThreshM = 0.0;
                    }
                    else
                    {
                        // Extrapolate satellite position since SIB19 was received
                        double dtSec = (relativeNow - sib19.receivedTime) / 1000.0;
                        double satX, satY, satZ;
                        extrapolateSatellitePosition(
                            sib19.ntnConfig.ephemerisInfo.posVel,
                            dtSec, satX, satY, satZ);

                        // Compute reference point
                        EcefPosition ref;
                        if (cond.d1sib19UseNadir)
                            ref = computeNadir(satX, satY, satZ);
                        else
                            ref = {satX, satY, satZ};

                        d1Dist = ecefDistance(uePos.ecef, ref);

                        // Resolve threshold: use condition's value, or fall back to SIB19's
                        double thresh = cond.d1sib19ThresholdM;
                        if (thresh < 0 && sib19.distanceThresh.has_value())
                            thresh = sib19.distanceThresh.value();
                        cond.d1sib19ResolvedThreshM = thresh;

                        // Compute elevation angle for logging
                        EcefPosition satPos{satX, satY, satZ};
                        double elev = elevationAngle(uePos.geo, uePos.ecef, satPos);

                        m_logger->debug("CHO candidate %d: D1_SIB19 dist=%.0fm thresh=%.0fm "
                                        "elev=%.1fdeg nadir=%s dt=%.1fs",
                                        cand.candidateId, d1Dist, thresh, elev,
                                        cond.d1sib19UseNadir ? "true" : "false",
                                        dtSec);
                    }
                }
            }

            double margin = evaluateConditionWithTTT(
                cond, now, servingRsrp, targetRsrp, d1Dist,
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

    if (triggered.empty())
        return;

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
        m_logger->info("CHO: %zu candidates triggered simultaneously – selecting candidate %d "
                       "(priority=%d margin=%.1f)",
                       triggered.size(), winner.candidateId,
                       winner.executionPriority, triggered[0].minMargin);
    }

    std::string condStr = conditionGroupStr(winner.conditions);
    m_logger->info("CHO candidate %d: condition group %s met – executing handover",
                   winner.candidateId, condStr.c_str());
    executeChoCandidate(winner);
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

    performHandover(/*txId=*/0, candidate.targetPci, candidate.newCRNTI,
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