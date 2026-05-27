//
// UE RRC Measurement Framework
// Supports NR measurement events A2, A3, and A5.
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
};


/**
 * @brief Enum to identify the source of measurements for the UE in the UERANSIM
 *  simulation environment.  Because there are no RF layers, these need to be simulated.
 *  Options:
 * - NONE: use internally simulated values only, no external meas source
 * - UDP: receive measurements as JSON over a UDP socket
 * - UNIX_SOCK: receive measurements as JSON over a Unix datagram socket
 * - FILE: periodically read measurements from a JSON file
 * 
 */
enum class EMeasSourceType
{
    NONE,       // Use internal RLS-simulated dBm values only
    UDP,        // Receive JSON measurements over UDP
    UNIX_SOCK,  // Receive JSON measurements over a Unix datagram socket
    FILE,       // Periodically read a JSON file
};

/**
 * @brief Struct that configures the measurement source for the UE.  
 *  Includes parameters for each source type, which can be set if that source type 
 *  is selected for use.
 * 
 */
struct MeasSourceConfig
{
    EMeasSourceType type{EMeasSourceType::NONE};  // Identify source type in use

    // UDP
    std::string udpAddress{"127.0.0.1"};  // UE IP Address
    uint16_t    udpPort{7200};  // UE's UDP Receive Port

    // UNIX_SOCK
    std::string unixSocketPath{};  // path to Unix datagram socket to receive measurements

    // FILE
    std::string filePath{};  // path to JSON file to read measurements from
    int         filePollIntervalMs{1000};
};

/**
 *  Struct to represent an individual cell measurement (from OOB provider or RLS)
 */
struct CellMeasurement
{
    int     cellId{};           // Internal UERANSIM cell ID (0 = use nci to resolve)
    int64_t nci{};              // NR Cell Identity (from SIB1); used to match when cellId==0
    int     rsrp{-140};         // dBm, range ~ -156 .. -44
    int     rsrq{-20};          // dB  (optional, default -20)
    int     sinr{-23};          // dB  (optional, default -23)
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
 *  event trigger conditions for a MeasurementReport.
 * 
 */
struct UeReportConfig
{
    int           reportConfigId{}; // ID assigned to this report config
    EMeasEvent    event{EMeasEvent::A3};  // event type

    int           a2Threshold{-110};   // for A2: serving < threshold (in dBm)

    int           a3Offset{6};         // for A3: neighbor > serving + this offset (dB)

    int           a5Threshold1{-110};  // for A5: serving < this threshold1 (dBm)
    int           a5Threshold2{-100};  // for A5: neighbor > this threshold2 (dBm)

    // common to all events:
    int           hysteresis{2};        // hysteresis value to apply (dB)
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
    std::unordered_map<int, UeMeasObject>    measObjects;   // key = measObjectId
    std::unordered_map<int, UeReportConfig>  reportConfigs; // key = reportConfigId
    std::unordered_map<int, UeMeasId>        measIds;       // key = measId

    // Per-measId runtime state
    std::unordered_map<int, MeasIdState>     measIdStates;  // key = measId
};

/* ------------------------------------------------------------------ */
/*  JSON helpers                                                      */
/* ------------------------------------------------------------------ */

Json ToJson(const EMeasEvent &v);
Json ToJson(const EMeasSourceType &v);
Json ToJson(const CellMeasurement &v);
Json ToJson(const UeReportConfig &v);
Json ToJson(const UeMeasConfig &v);

} // namespace nr::ue
