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

#include <lib/rrc/common/event_types.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <utils/json.hpp>
#include <ue/types.hpp>

namespace nr::ue
{

/* ------------------------------------------------------------------ */
/*  Measurement object – represents a frequency layer to measure      */
/* ------------------------------------------------------------------ */

// struct UeMeasObject
// {
//     int measObjectId{};
//     int ssbFrequency{};     // SSB ARFCN (simplified — we use cellId matching instead)
// };

// /**
//  * @brief Struct to represent a Measurement Identity, which binds together:
//  * - measId: the identifier used in MeasurementReport messages
//  * - measObjectId: the measurement object (frequency) to which this measId applies
//  * - reportConfigId: the report configuration (event trigger) that uses this measId
//  *
//  */
// struct UeMeasId
// {
//     int measId{};           // maps to the measId in MeasurementReport messages
//     int measObjectId{};     // maps to the measObjectId in UeMeasObject, 
//                             //  which defines the frequency to measure
//     int reportConfigId{};   // maps to the reportConfigId in UeReportConfig, 
//                             //  which defines the event trigger conditions for this measId
// };

// /**
//  * @brief Struct to track the state of a specific measurement Id event trigger,
//  *  including:
//  * - enteringTimestamp: when the event condition was first satisfied 
//  *  (for time-to-trigger tracking)
//  * - reported: whether the event has already been reported (for one-shot reporting)
//  * 
//  */
// struct MeasIdState
// {
//     // Time (ms) when the entering condition was first satisfied,
//     // or 0 if not currently satisfied.
//     int64_t enteringTimestamp{};
    
//     // True once the condition has been satisfied for time-to-trigger duration.
//     bool   isSatisfied{};         

//     // True once the event has been reported (for one-shot reporting).
//     bool   isReported{};

//     std::vector<TriggeredNeighbor> triggeredNeighbors; // List of neighbors that triggered the event (for reporting)
// };


// /**
//  * @brief Struct used to store measurement configurations and state inside the RRC task.
//  *  Includes:
//  * - Measurement objects (frequencies to measure)
//  * - Report configurations (event triggers)
//  * - Measurement identities (binding objects to triggers)
//  * - Runtime state for each measId (time-to-trigger tracking)
//  * 
//  */
// struct UeMeasConfig
// {
//     // RRCReconfiguration-v1530 fullConfig=true indicates full configuration
//     // replacement semantics for measurement-related state.
//     bool fullConfig{};

//     // Delta signaling remove lists (if present) from ASN MeasConfig.
//     std::vector<int> measObjectsToRemove;
//     std::vector<int> reportConfigsToRemove;
//     std::vector<int> measIdsToRemove;

//     std::unordered_map<int, UeMeasObject>    measObjects;   // key = measObjectId
//     std::unordered_map<int, nr::rrc::common::ReportConfigEvent>  reportConfigs; // key = reportConfigId
//     std::unordered_map<int, UeMeasId>        measIds;       // key = measId

//     // Per-measId runtime state
//     std::unordered_map<int, MeasIdState>     measIdStates;  // key = measId
// };



// /**
//  * @brief A single Conditional Handover candidate (CondReconfigToAddMod).
//  *
//  * Per 3GPP TS 38.331 §5.3.5.8.6 (Release 16/17), the gNB pre-configures
//  * one or more CHO candidates.  Each candidate carries:
//  *  - Target cell parameters (technically this is a ReconfigWithSync message, but
//  *      for simulation we only need to store the PCI, C-RNTI, T304).
//  *  - A condition group: one or more ChoCondition entries evaluated with
//  *    AND logic.  All conditions in the group must be simultaneously
//  *    satisfied for the candidate to trigger.
//  *  - An optional execution priority (lower value = higher priority).
//  *
//  * Multiple candidates in the CHO list are treated as OR – the first
//  * (or highest-priority) candidate whose condition group is fully
//  * satisfied triggers handover.
//  *
//  * When multiple candidates trigger in the same evaluation cycle, the
//  * UE selects based on:
//  *   1. condExecutionPriority (lowest value wins)
//  *   2. Greatest trigger margin (how much the conditions are exceeded)
//  *   3. Highest neighbor RSRP
//  *   4. Configuration order (earliest in the list)
//  */
// struct ChoCandidate
// {
//     int candidateId{};          // condReconfigId
//     int targetPci{};            // Target cell PCI
//     int newCRNTI{};             // C-RNTI assigned by target cell
//     int t304Ms{1000};           // T304 supervision timer (ms)
//     int txId{0};              // ASN.1 transactionId for the ReconfigurationWithSync message to apply on trigger
//     int executionPriority{0x7FFFFFFF}; // Lower = higher priority; max = unset

//     // List of all reportConfigIds whose conditions apply to this candidate (from condExecutionCond).
//     std::vector<int> reportConfigIds;

//     /* Runtime state */
//     bool executed{};            // Whether this candidate has been executed
//     double triggerMargin{};     // Computed when all conditions met (for tie-breaking)
// };

// /* ------------------------------------------------------------------ */
// /*  JSON helpers                                                      */
// /* ------------------------------------------------------------------ */

// //Json ToJson(const EMeasEvent &v);
// //Json ToJson(const UeReportConfig &v);
// Json ToJson(const UeMeasConfig &v);
// //Json ToJson(const EChoEventType &v);
// //Json ToJson(const ChoCondition &v);
// Json ToJson(const ChoCandidate &v);

} // namespace nr::ue
