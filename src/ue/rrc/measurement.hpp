//
// UE RRC Measurement Framework
// Supports NR measurement events A2, A3, A5, and D1.
// Provides out-of-band measurement injection via UDP, Unix socket, or file.
//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <utils/json.hpp>
#include <ue/types.hpp>

namespace nr::ue
{

/**
 * @brief Measurement event types that can trigger a MeasurementReport, 
 *  based on 3GPP TS 38.331 Section 5.5.4
 * 
 */
enum class EMeasEvent
{
    A2, // Serving cell becomes worse than threshold
    A3, // Neighbor cell becomes offset better than serving cell (SpCell)
    A5, // Serving cell becomes worse than threshold1 AND neighbor cell becomes better than threshold2
    D1, // Distance to a configured reference location exceeds threshold
};

/**
 * @brief D1 reference location selector.
 *
 * - Fixed: use a configured ECEF reference point.
 * - Nadir: derive the reference point from serving-cell SIB19 ephemeris.
 * - Unknown: invalid configuration (should be rejected by RRC Reconfiguration procedure).
 */
enum class ED1ReferenceType
{
    Fixed,
    Nadir,
    Unknown,
};

/* ------------------------------------------------------------------ */
/*  Measurement object – represents a frequency layer to measure      */
/* ------------------------------------------------------------------ */

struct UeMeasObject
{
    int measObjectId{};
    int ssbFrequency{};     // SSB ARFCN (simplified — we use cellId matching instead)
};

/**
 * @brief Struct to represent a report configuration, which defines the 
 *  event trigger conditions for a MeasurementReport.  Contains fields for
 *  all supported event types (A2, A3, A5, D1), but only a subset will be relevant
 *  depending on the event type.
 * 
 */
struct UeReportConfig
{
    int           reportConfigId{}; // ID assigned to this report config
    EMeasEvent    event{EMeasEvent::A3};  // event type

    /* A2 */
    int           a2Threshold{-110};   // serving < threshold (in dBm)

    /* A3 */
    int           a3Offset{6};         // neighbor > serving + this offset (dB)

    /* A5 */
    int           a5Threshold1{-110};  // serving < this threshold1 (dBm)
    int           a5Threshold2{-100};  // neighbor > this threshold2 (dBm)

    /* D1: distance to reference location > threshold */
    ED1ReferenceType d1ReferenceType{ED1ReferenceType::Fixed};
    double        d1RefX{};            // ECEF reference X (m), used when d1ReferenceType=Fixed
    double        d1RefY{};            // ECEF reference Y (m), used when d1ReferenceType=Fixed
    double        d1RefZ{};            // ECEF reference Z (m), used when d1ReferenceType=Fixed
    double        d1ThresholdM{1000.0}; // distance threshold in meters

    /* T1: timer  */
    int t1DurationMs{1000};


    // common to all events:
    int           hysteresis{2};        // hysteresis value to apply (db for RSRP, m for distance)
    int           timeToTriggerMs{640}; // how long condition must hold (ms) before triggering report
    int           maxReportCells{8};    // max number of neighbor cells to include in report (for A3/A5)
};


/**
 * @brief Struct to represent a Measurement Identity, which binds together:
 * - measId: the identifier used in MeasurementReport messages
 * - measObjectId: the measurement object (frequency) to which this measId applies
 * - reportConfigId: the report configuration (event trigger) that uses this measId
 *
 */
struct UeMeasId
{
    int measId{};           // maps to the measId in MeasurementReport messages
    int measObjectId{};     // maps to the measObjectId in UeMeasObject, 
                            //  which defines the frequency to measure
    int reportConfigId{};   // maps to the reportConfigId in UeReportConfig, 
                            //  which defines the event trigger conditions for this measId
};

/**
 * @brief Struct to track the state of a specific measurement Id event trigger,
 *  including:
 * - enteringTimestamp: when the event condition was first satisfied 
 *  (for time-to-trigger tracking)
 * - reported: whether the event has already been reported (for one-shot reporting)
 * 
 */
struct MeasIdState
{
    // Time (ms) when the entering condition was first satisfied,
    // or 0 if not currently satisfied.
    int64_t enteringTimestamp{};

    // True once the event has been reported (for one-shot reporting).
    bool    reported{};
};


/**
 * @brief Struct used to store measurement configurations and state inside the RRC task.
 *  Includes:
 * - Measurement objects (frequencies to measure)
 * - Report configurations (event triggers)
 * - Measurement identities (binding objects to triggers)
 * - Runtime state for each measId (time-to-trigger tracking)
 * 
 */
struct UeMeasConfig
{
    // RRCReconfiguration-v1530 fullConfig=true indicates full configuration
    // replacement semantics for measurement-related state.
    bool fullConfig{};

    // Delta signaling remove lists (if present) from ASN MeasConfig.
    std::vector<int> measObjectsToRemove;
    std::vector<int> reportConfigsToRemove;
    std::vector<int> measIdsToRemove;

    std::unordered_map<int, UeMeasObject>    measObjects;   // key = measObjectId
    std::unordered_map<int, UeReportConfig>  reportConfigs; // key = reportConfigId
    std::unordered_map<int, UeMeasId>        measIds;       // key = measId

    // Per-measId runtime state
    std::unordered_map<int, MeasIdState>     measIdStates;  // key = measId
};

/* ------------------------------------------------------------------ */
/*  Conditional Handover (CHO) types – Release 17 condition groups    */
/* ------------------------------------------------------------------ */

/**
 * @brief Event type for a single atomic CHO execution condition.
 *
 * Per 3GPP TS 38.331, each MeasId in condExecutionCond resolves to a
 * ReportConfig whose event type determines the kind of check the UE
 * must perform.  We also support a T1 (timer-only) pseudo-event for
 * UERANSIM-specific timer-based triggers.
 */
enum class EChoEventType
{
    T1,       // Timer-only: fires after t1DurationMs elapsed
    A2,       // Serving RSRP < threshold
    A3,       // Neighbor RSRP > serving RSRP + offset + hysteresis
    A5,       // Serving < threshold1 - hysteresis AND neighbor > threshold2 + hysteresis
    D1,       // Distance to configured reference (fixed or nadir) exceeds threshold
};

/**
 * @brief A single atomic condition within a CHO condition group.
 *
 * Multiple ChoCondition objects within a ChoCandidate's condition list
 * are evaluated with AND logic – all must be simultaneously satisfied
 * for the candidate to trigger.  Different ChoCandidate entries are
 * evaluated with OR logic – the first fully-satisfied candidate wins.
 */
struct ChoCondition
{
    EChoEventType eventType{EChoEventType::T1};

    /* T1 parameters */
    int t1DurationMs{1000};

    /* A2 parameters */
    int a2Threshold{-110};        // dBm

    /* A3 parameters */
    int a3Offset{6};              // dB
    int a3Hysteresis{2};          // dB

    /* A5 parameters */
    int a5Threshold1{-110};       // dBm (serving)
    int a5Threshold2{-100};       // dBm (neighbor)
    int a5Hysteresis{2};          // dB

    /* D1 parameters */
    ED1ReferenceType d1ReferenceType{ED1ReferenceType::Fixed};
    double d1RefX{};
    double d1RefY{};
    double d1RefZ{};
    double d1ThresholdM{1000.0};
    double d1ElevationMinDeg{-1.0};   ///< Optional log-only elevation filter hint

    /* D1 runtime — set by evaluateChoCandidates() each cycle */
    double d1ResolvedThreshM{0.0};    ///< Resolved threshold used for this evaluation cycle

    /* Common */
    int hysteresis{2};            // general hysteresis (used where applicable)
    int timeToTriggerMs{0};       // condition must hold this long (ms)

    /* Runtime state — managed by evaluateChoCandidates() */
    int64_t enteringTimestamp{};  // When condition first became true (0 = not met)
    int64_t t1StartTime{};       // When T1 was started (0 = not started)
    bool satisfied{};             // true once condition has been met for TTT duration
};

/**
 * @brief A single Conditional Handover candidate (CondReconfigToAddMod).
 *
 * Per 3GPP TS 38.331 §5.3.5.8.6 (Release 16/17), the gNB pre-configures
 * one or more CHO candidates.  Each candidate carries:
 *  - Target cell parameters (technically this is a ReconfigWithSync message, but
 *      for simulation we only need to store the PCI, C-RNTI, T304).
 *  - A condition group: one or more ChoCondition entries evaluated with
 *    AND logic.  All conditions in the group must be simultaneously
 *    satisfied for the candidate to trigger.
 *  - An optional execution priority (lower value = higher priority).
 *
 * Multiple candidates in the CHO list are treated as OR – the first
 * (or highest-priority) candidate whose condition group is fully
 * satisfied triggers handover.
 *
 * When multiple candidates trigger in the same evaluation cycle, the
 * UE selects based on:
 *   1. condExecutionPriority (lowest value wins)
 *   2. Greatest trigger margin (how much the conditions are exceeded)
 *   3. Highest neighbor RSRP
 *   4. Configuration order (earliest in the list)
 */
struct ChoCandidate
{
    int candidateId{};          // condReconfigId
    int targetPci{};            // Target cell PCI
    int newCRNTI{};             // C-RNTI assigned by target cell
    int t304Ms{1000};           // T304 supervision timer (ms)
    int txId{0};              // ASN.1 transactionId for the ReconfigurationWithSync message to apply on trigger
    int executionPriority{0x7FFFFFFF}; // Lower = higher priority; max = unset

    /* Condition group – AND logic: ALL must be satisfied */
    std::vector<ChoCondition> conditions;

    /* Runtime state */
    bool executed{};            // Whether this candidate has been executed
    double triggerMargin{};     // Computed when all conditions met (for tie-breaking)
};

/* ------------------------------------------------------------------ */
/*  JSON helpers                                                      */
/* ------------------------------------------------------------------ */

Json ToJson(const EMeasEvent &v);
Json ToJson(const UeReportConfig &v);
Json ToJson(const UeMeasConfig &v);
Json ToJson(const EChoEventType &v);
Json ToJson(const ChoCondition &v);
Json ToJson(const ChoCandidate &v);

} // namespace nr::ue
