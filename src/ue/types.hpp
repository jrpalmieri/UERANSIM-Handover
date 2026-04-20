//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include "timer.hpp"

#include <array>
#include <atomic>

#include <ue/rrc/sib19.hpp>
#include <deque>
#include <memory>
#include <queue>
#include <set>
#include <unordered_set>
#include <map>
#include <shared_mutex>

#include <lib/app/monitor.hpp>
#include <lib/app/ue_ctl.hpp>
#include <lib/nas/nas.hpp>
#include <lib/rrc/common/event_types.hpp>
#include <utils/common_types.hpp>
#include <utils/json.hpp>
#include <utils/locked.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>
#include <utils/octet_string.hpp>

namespace utils
{
class SatTime;
}

namespace nr::ue
{

class UeAppTask;
class NasTask;
class UeRrcTask;
class UeRlsTask;
class UserEquipment;

struct UeCellDesc
{
    int dbm{};

    struct
    {
        bool hasMib = false;
        bool isBarred = true;
        bool isIntraFreqReselectAllowed = true;
    } mib{};

    struct
    {
        bool hasSib1 = false;
        bool isReserved = false;
        int64_t nci = 0;
        int tac = 0;
        Plmn plmn;
        UacAiBarringSet aiBarringSet;
    } sib1{};

    Sib19Info sib19{};
};

struct SupportedAlgs
{
    bool nia1 = true;
    bool nia2 = true;
    bool nia3 = true;
    bool nea1 = true;
    bool nea2 = true;
    bool nea3 = true;
};

enum class OpType
{
    OP,
    OPC
};

struct SessionConfig
{
    nas::EPduSessionType type{};
    std::optional<SingleSlice> sNssai{};
    std::optional<std::string> apn{};
    bool isEmergency{};
};

struct IntegrityMaxDataRateConfig
{
    bool uplinkFull{};
    bool downlinkFull{};
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


struct HandoverServerConfig
{
    std::string address{"127.0.0.1"};
    std::string transport{"UDP"};
    uint16_t port{7200};
};

struct UeRlsConfig
{
    int loopCounter{200};
    int receiveTimeout{100};
    int timerPeriodAckControl{1500};
    int timerPeriodAckSend{2250};

    [[nodiscard]] int getHeartbeatThreshold() const
    {
        return loopCounter + receiveTimeout;
    }
};

struct UeConfig
{
    struct NtnConfig
    {
        struct TleEntry
        {
            std::string line1{};
            std::string line2{};
        };

        struct TimeWarpConfig
        {
            enum class EStartCondition
            {
                Moving,
                Paused,
            };

            EStartCondition startCondition{EStartCondition::Moving};
            double tickScaling{1.0};
            std::optional<int64_t> startEpochMillis{};
        };

        bool ntnEnabled{false};
        std::optional<TleEntry> tle{};
        TimeWarpConfig timeWarp{};
        int elevationMinDeg{20};
    };

    /* Read from config file */
    std::optional<Supi> supi{};
    int protectionScheme;
    int homeNetworkPublicKeyId;
    OctetString homeNetworkPublicKey{};
    std::optional<std::string> routingIndicator{};
    Plmn hplmn{};
    OctetString key{};
    OctetString opC{};
    OpType opType{};
    OctetString amf{};
    std::optional<std::string> imei{};
    std::optional<std::string> imeiSv{};
    SupportedAlgs supportedAlgs{};
    std::vector<std::string> gnbSearchList{};
    std::vector<SessionConfig> defaultSessions{};
    IntegrityMaxDataRateConfig integrityMaxRate{};
    NetworkSlice defaultConfiguredNssai{};
    NetworkSlice configuredNssai{};
    std::optional<std::string> tunName{};
    std::optional<std::string> tunNetmask{};
    std::optional<std::string> nodeNameTemplate{};

    struct
    {
        bool mps{};
        bool mcs{};
    } uacAic;

    struct
    {
        int normalCls{}; // [0..9]
        bool cls11{};
        bool cls12{};
        bool cls13{};
        bool cls14{};
        bool cls15{};
    } uacAcc;

    // Advanced handover measurement framework with external measurement sources
    //  (default is False to use legacy RLS-simulated measurements)
    bool useHandoverMeasFramework{false};
    
    HandoverServerConfig handoverServerConfig{}; 
    
    // If useHandoverMeasFramework is True, then the following measSourceConfig specifies the 
    //  source of measurements for the UE
    MeasSourceConfig measSourceConfig{};
    
    // UE position from config (for D1 handover events)
    std::optional<GeoPosition> initialPosition{};

    NtnConfig ntn{};

    UeRlsConfig rls{};

    /* Assigned by program */
    bool configureRouting{};
    bool prefixLogger{};
    std::optional<std::string> nodeNameTemplatePreview{};

    [[nodiscard]] std::string getNodeName() const
    {
        if (supi.has_value())
            return ToJson(supi).str();
        if (imei.has_value())
            return "imei-" + *imei;
        if (imeiSv.has_value())
            return "imeisv-" + *imeiSv;
        return "unknown-ue";
    }

    [[nodiscard]] std::string getLoggerPrefix() const
    {
        if (!prefixLogger)
            return "";
        if (supi.has_value())
            return supi->value + "|";
        if (imei.has_value())
            return *imei + "|";
        if (imeiSv.has_value())
            return *imeiSv + "|";
        return "unknown-ue|";
    }
};

struct CellSelectionReport
{
    int outOfPlmnCells{};
    int siMissingCells{};
    int reservedCells{};
    int barredCells{};
    int forbiddenTaiCells{};
};

/**
 *  Struct to represent an individual cell measurement (from OOB provider)
 */
struct CellMeasurement
{
    int     cellId{};           // Cell ID
    int64_t nci{};              // NR Cell Identity (from SIB1)
    int     rsrp{-140};         // dBm, range ~ -156 .. -44
    int     rsrq{-20};          // dB  (optional, default -20)
    int     sinr{-23};          // dB  (optional, default -23)
    std::string ip{};           // IP address of the cell
    uint64_t last_report_time{}; // timestamp of when this measurement was reported by the OOB provider (ms)
};

/**
 * @brief Struct to store measurement events for a cellId.
 */
struct TriggeredNeighbor
{
    int cellId;
    int rsrp;
};


/**
 * @brief comparator for the CellMeasurement set, so that it can stay sorted by rsrp in 
 *  descending order (stronger signals first if rsrp is negative)
 * 
 */
struct CompareBySignalStrength {
    bool operator()(const CellMeasurement& a, const CellMeasurement b) const {
        return a.rsrp > b.rsrp;  // sort descending (stronger signals first if rsrp is negative)
    }
};

struct AllCellMeasurements
{
    // Global set to store the most recent measurement for each cellId, updated by RLS meas task 
    //   and read by each UE's RRC for measurement reporting
    std::set<CellMeasurement, CompareBySignalStrength> cellMeasurements;
    // mutex to protect access to allCellMeasurements set, since it is updated by RLS meas task and 
    //  read by each UE's RRC for measurement reporting
    std::shared_mutex cellMeasurementsMutex;
};


struct ActiveCellInfo
{
    int cellId{};
    ECellCategory category{};
    Plmn plmn{};
    int tac{};

    [[nodiscard]] bool hasValue() const;
};

struct UeMeasObject
{
    int measObjectId{};
    int ssbFrequency{};     // SSB ARFCN (simplified — we use cellId matching instead)
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
    
    // True once the condition has been satisfied for time-to-trigger duration.
    bool   isSatisfied{};         

    // True once the event has been reported (for one-shot reporting).
    bool   isReported{};

    std::vector<TriggeredNeighbor> triggeredNeighbors; // List of neighbors that triggered the event (for reporting)
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
    std::unordered_map<int, nr::rrc::common::ReportConfigEvent>  reportConfigs; // key = reportConfigId
    std::unordered_map<int, UeMeasId>        measIds;       // key = measId

    // Per-measId runtime state
    std::unordered_map<int, MeasIdState>     measIdStates;  // key = measId
};

/**
 * @brief A single Conditional Handover candidate (CondReconfigToAddMod).
 *
 * Per 3GPP TS 38.331 §5.3.5.8.6 (Release 16/17), the gNB pre-configures
 * one or more CHO candidates.  Each candidate carries:
 *  - Target cell parameters (technically this is a ReconfigWithSync message, but
 *      for simulation we only need to store the PCI, C-RNTI, T304).
 *  - A condition group: one or more ReportConfigEvents evaluated with
 *    AND logic.  All conditions in the group must be simultaneously
 *    satisfied for the candidate to trigger.
 *
 * Multiple candidates in the CHO list are treated as OR – if any of
 * the candidates have a condition group that is fully satisfied,
 * handover is triggered.  UE must determine which to trigger.
 */
struct ChoCandidate
{
    int candidateId{};          // condReconfigId
    int targetPci{};            // Target cell PCI
    int newCRNTI{};             // C-RNTI assigned by target cell
    int t304Ms{1000};           // T304 supervision timer (ms)
    int txId{0};              // ASN.1 transactionId for the ReconfigurationWithSync message to apply on trigger
    int executionPriority{0x7FFFFFFF}; // Lower = higher priority; max = unset

    // List of all MeasIds whose conditions apply to this candidate (from condExecutionCond).
    std::vector<int> measIds;

    /* Runtime state */
    bool executed{};            // Whether this candidate has been executed
    double triggerMargin{};     // Computed when all conditions met (for tie-breaking)
};


struct UeSharedContext
{
    Locked<std::unordered_set<Plmn>> availablePlmns;
    Locked<Plmn> selectedPlmn;
    Locked<ActiveCellInfo> currentCell;
    Locked<std::vector<Tai>> forbiddenTaiRoaming;
    Locked<std::vector<Tai>> forbiddenTaiRps;
    Locked<std::optional<GutiMobileIdentity>> providedGuti;
    Locked<std::optional<GutiMobileIdentity>> providedTmsi;

    Plmn getCurrentPlmn();
    Tai getCurrentTai();
    bool hasActiveCell();
};

struct RlsSharedContext
{
    std::atomic<uint64_t> sti{};
    uint32_t senderId{};
    std::atomic<uint32_t> cRnti{};
};

struct TaskBase
{
    UserEquipment *ue{};
    UeConfig *config{};
    LogBase *logBase{};
    app::IUeController *ueController{};
    app::INodeListener *nodeListener{};
    NtsTask *cliCallbackTask{};

    UeSharedContext shCtx{};

    AllCellMeasurements *g_allCellMeasurements{};

    // cell RF strength measurements
    std::map<int, int> cellDbMeas;
    std::shared_mutex cellDbMeasMutex;

    UeAppTask *appTask{};
    NasTask *nasTask{};
    UeRrcTask *rrcTask{};
    UeRlsTask *rlsTask{};
    utils::SatTime *satTime{};

    // lat/long/alt position of the UE
    GeoPosition UeLocation{};
};

struct RrcTimers
{
    UeTimer t300;

    RrcTimers();
};

struct NasTimers
{
    UeTimer t3346; /* MM - ... */
    UeTimer t3396; /* SM - ... */

    UeTimer t3444; /* MM - ... */
    UeTimer t3445; /* MM - ... */

    UeTimer t3502; /* MM - Initiation of the registration procedure, if still required */
    UeTimer t3510; /* MM - Registration Request transmission timer */
    UeTimer t3511; /* MM - Retransmission of the REGISTRATION REQUEST, if still required */
    UeTimer t3512; /* MM - Periodic registration update timer */
    UeTimer t3516; /* MM - 5G AKA - RAND and RES* storing timer */
    UeTimer t3517; /* MM - Service Request transmission timer */
    UeTimer t3519; /* MM - Transmission with fresh SUCI timer */
    UeTimer t3520; /* MM - ... */
    UeTimer t3521; /* MM - De-registration transmission timer for not switch off */
    UeTimer t3525; /* MM - ... */
    UeTimer t3540; /* MM - ... */

    UeTimer t3584; /* SM - ... */
    UeTimer t3585; /* SM - ... */

    NasTimers();
};

enum class ERmState
{
    RM_DEREGISTERED,
    RM_REGISTERED
};

enum class ECmState
{
    CM_IDLE, // Exact same thing with 5GMM-IDLE in 24.501
    CM_CONNECTED
};

enum class E5UState
{
    U1_UPDATED = 0,
    U2_NOT_UPDATED,
    U3_ROAMING_NOT_ALLOWED
};

enum class ERrcState
{
    RRC_IDLE,
    RRC_CONNECTED,
    RRC_INACTIVE,
};

enum class ERrcLastSetupRequest
{
    SETUP_REQUEST,
    REESTABLISHMENT_REQUEST,
    RESUME_REQUEST,
    RESUME_REQUEST1,
};

enum class EMmState
{
    MM_NULL,
    MM_DEREGISTERED,
    MM_REGISTERED_INITIATED,
    MM_REGISTERED,
    MM_DEREGISTERED_INITIATED,
    MM_SERVICE_REQUEST_INITIATED,
};

enum class EMmSubState
{
    MM_NULL_PS,

    MM_DEREGISTERED_PS,
    MM_DEREGISTERED_NORMAL_SERVICE,
    MM_DEREGISTERED_LIMITED_SERVICE,
    MM_DEREGISTERED_ATTEMPTING_REGISTRATION,
    MM_DEREGISTERED_PLMN_SEARCH,
    MM_DEREGISTERED_NO_SUPI,
    MM_DEREGISTERED_NO_CELL_AVAILABLE,
    MM_DEREGISTERED_ECALL_INACTIVE,
    MM_DEREGISTERED_INITIAL_REGISTRATION_NEEDED,

    MM_REGISTERED_INITIATED_PS,

    MM_REGISTERED_PS,
    MM_REGISTERED_NORMAL_SERVICE,
    MM_REGISTERED_NON_ALLOWED_SERVICE,
    MM_REGISTERED_ATTEMPTING_REGISTRATION_UPDATE,
    MM_REGISTERED_LIMITED_SERVICE,
    MM_REGISTERED_PLMN_SEARCH,
    MM_REGISTERED_NO_CELL_AVAILABLE,
    MM_REGISTERED_UPDATE_NEEDED,

    MM_DEREGISTERED_INITIATED_PS,

    MM_SERVICE_REQUEST_INITIATED_PS
};

enum class EPsState
{
    INACTIVE,
    ACTIVE_PENDING,
    ACTIVE,
    INACTIVE_PENDING,
    MODIFICATION_PENDING
};

enum class EPtState
{
    INACTIVE,
    PENDING,
};

struct PduSession
{
    static constexpr const int MIN_ID = 1;
    static constexpr const int MAX_ID = 15;

    const int psi;

    EPsState psState{};
    bool uplinkPending{};

    nas::EPduSessionType sessionType{};
    std::optional<std::string> apn{};
    std::optional<SingleSlice> sNssai{};
    bool isEmergency{};

    std::optional<nas::IEQoSRules> authorizedQoSRules{};
    std::optional<nas::IESessionAmbr> sessionAmbr{};
    std::optional<nas::IEQoSFlowDescriptions> authorizedQoSFlowDescriptions{};
    std::optional<nas::IEPduAddress> pduAddress{};

    explicit PduSession(int psi) : psi(psi)
    {
    }
};

struct ProcedureTransaction
{
    static constexpr const int MIN_ID = 1;
    static constexpr const int MAX_ID = 254;

    EPtState state{};
    std::unique_ptr<UeTimer> timer{};
    std::unique_ptr<nas::SmMessage> message{};
    int psi{};
};

enum class EConnectionIdentifier
{
    THREE_3GPP_ACCESS = 0x01,
    NON_THREE_3GPP_ACCESS = 0x02,
};

struct NasCount
{
    octet2 overflow{};
    octet sqn{};

    [[nodiscard]] inline octet4 toOctet4() const
    {
        uint32_t value = 0;
        value |= (uint32_t)overflow;
        value <<= 8;
        value |= (uint32_t)sqn;
        return octet4{value};
    }
};

struct UeKeys
{
    OctetString abba{};

    OctetString kAusf{};
    OctetString kSeaf{};
    OctetString kAmf{};
    OctetString kNasInt{};
    OctetString kNasEnc{};
    OctetString kAkma{};

    [[nodiscard]] UeKeys deepCopy() const
    {
        UeKeys keys;
        keys.kAusf = kAusf.subCopy(0);
        keys.kSeaf = kSeaf.subCopy(0);
        keys.kAmf = kAmf.subCopy(0);
        keys.kNasInt = kNasInt.subCopy(0);
        keys.kNasEnc = kNasEnc.subCopy(0);
        return keys;
    }
};

struct NasSecurityContext
{
    nas::ETypeOfSecurityContext tsc{};
    int ngKsi{}; // 3-bit

    NasCount downlinkCount{};
    NasCount uplinkCount{};

    bool is3gppAccess = true;

    UeKeys keys{};
    nas::ETypeOfIntegrityProtectionAlgorithm integrity{};
    nas::ETypeOfCipheringAlgorithm ciphering{};

    std::deque<int> lastNasSequenceNums{};

    void updateDownlinkCount(const NasCount &validatedCount)
    {
        downlinkCount.overflow = validatedCount.overflow;
        downlinkCount.sqn = validatedCount.sqn;
    }

    [[nodiscard]] NasCount estimatedDownlinkCount(octet sequenceNumber) const
    {
        NasCount count;
        count.sqn = downlinkCount.sqn;
        count.overflow = downlinkCount.overflow;

        if (count.sqn > sequenceNumber)
            count.overflow = octet2(((int)count.overflow + 1) & 0xFFFF);
        count.sqn = sequenceNumber;
        return count;
    }

    void countOnEncrypt()
    {
        uplinkCount.sqn = static_cast<uint8_t>((((int)uplinkCount.sqn + 1) & 0xFF));
        if (uplinkCount.sqn == 0)
            uplinkCount.overflow = octet2(((int)uplinkCount.overflow + 1) & 0xFFFF);
    }

    void rollbackCountOnEncrypt()
    {
        if (uplinkCount.sqn == 0)
        {
            uplinkCount.sqn = 0xFF;

            if ((int)uplinkCount.overflow == 0)
                uplinkCount.overflow = octet2{0xFFFF};
            else
                uplinkCount.overflow = octet2{(int)uplinkCount.overflow - 1};
        }
        else
        {
            uplinkCount.sqn = static_cast<uint8_t>(((int)uplinkCount.sqn - 1) & 0xFF);
        }
    }

    [[nodiscard]] NasSecurityContext deepCopy() const
    {
        NasSecurityContext ctx;
        ctx.tsc = tsc;
        ctx.ngKsi = ngKsi;
        ctx.downlinkCount = downlinkCount;
        ctx.uplinkCount = uplinkCount;
        ctx.is3gppAccess = is3gppAccess;
        ctx.keys = keys.deepCopy();
        ctx.integrity = integrity;
        ctx.ciphering = ciphering;
        ctx.lastNasSequenceNums = lastNasSequenceNums;
        return ctx;
    }
};

enum class EAutnValidationRes
{
    OK,
    MAC_FAILURE,
    AMF_SEPARATION_BIT_FAILURE,
    SYNCHRONISATION_FAILURE,
};

enum class ERegUpdateCause
{
    // when the UE detects entering a tracking area that is not in the list of tracking areas that the UE previously
    // registered in the AMF
    ENTER_UNLISTED_TRACKING_AREA,
    // when the periodic registration updating timer T3512 expires
    T3512_EXPIRY,
    // when the UE receives a CONFIGURATION UPDATE COMMAND message indicating "registration requested" in the
    // Configuration update indication IE as specified in subclauses 5.4.4.3;
    CONFIGURATION_UPDATE,
    // when the UE in state 5GMM-REGISTERED.ATTEMPTING-REGISTRATION-UPDATE either receives a paging or the UE receives a
    // NOTIFICATION message with access type indicating 3GPP access over the non-3GPP access for PDU sessions associated
    // with 3GPP access
    PAGING_OR_NOTIFICATION,
    // upon inter-system change from S1 mode to N1 mode
    INTER_SYSTEM_CHANGE_S1_TO_N1,
    // when the UE receives an indication of "RRC Connection failure" from the lower layers and does not have signalling
    // pending (i.e. when the lower layer requests NAS signalling connection recovery) except for the case specified in
    // subclause 5.3.1.4;
    CONNECTION_RECOVERY,
    // when the UE receives a fallback indication from the lower layers and does not have signalling pending (i.e. when
    // the lower layer requests NAS signalling connection recovery, see subclauses 5.3.1.4 and 5.3.1.2);
    FALLBACK_INDICATION,
    // when the UE changes the 5GMM capability or the S1 UE network capability or both
    MM_OR_S1_CAPABILITY_CHANGE,
    // when the UE's usage setting changes
    USAGE_SETTING_CHANGE,
    // when the UE needs to change the slice(s) it is currently registered to
    SLICE_CHANGE,
    // when the UE changes the UE specific DRX parameters
    DRX_CHANGE,
    // when the UE in state 5GMM-REGISTERED.ATTEMPTING-REGISTRATION-UPDATE receives a request from the upper layers to
    // establish an emergency PDU session or perform emergency services fallback
    EMERGENCY_CASE,
    // when the UE needs to register for SMS over NAS, indicate a change in the requirements to use SMS over NAS, or
    // de-register from SMS over NAS;
    SMS_OVER_NAS_CHANGE,
    // when the UE needs to indicate PDU session status to the network after performing a local release of PDU
    // session(s) as specified in subclauses 6.4.1.5 and 6.4.3.5;
    PS_STATUS_INFORM,
    // when the UE in 5GMM-IDLE mode changes the radio capability for NG-RAN
    RADIO_CAP_CHANGE,
    // when the UE needs to request new LADN information
    NEW_LADN_NEEDED,
    // when the UE needs to request the use of MICO mode or needs to stop the use of MICO mode
    MICO_MODE_CHANGE,
    // when the UE in 5GMM-CONNECTED mode with RRC inactive indication enters a cell in the current registration area
    // belonging to an equivalent PLMN of the registered PLMN and not belonging to the registered PLMN;
    ENTER_EQUIVALENT_PLMN_CELL,
    // when the UE receives a SERVICE REJECT message with the 5GMM cause value set to #28 "Restricted service area".
    RESTRICTED_SERVICE_AREA,
    // ------ following are not specified by 24.501 ------
    TAI_CHANGE_IN_ATT_UPD,
    PLMN_CHANGE_IN_ATT_UPD,
    T3346_EXPIRY_IN_ATT_UPD,
    T3502_EXPIRY_IN_ATT_UPD,
    T3511_EXPIRY_IN_ATT_UPD,
};

enum class EServiceReqCause
{
    // a) the UE, in 5GMM-IDLE mode over 3GPP access, receives a paging request from the network
    IDLE_PAGING,
    // b) the UE, in 5GMM-CONNECTED mode over 3GPP access, receives a notification from the network with access type
    // indicating non-3GPP access
    CONNECTED_3GPP_NOTIFICATION_N3GPP,
    // c) the UE, in 5GMM-IDLE mode over 3GPP access, has uplink signalling pending
    IDLE_UPLINK_SIGNAL_PENDING,
    // d) the UE, in 5GMM-IDLE mode over 3GPP access, has uplink user data pending
    IDLE_UPLINK_DATA_PENDING,
    // e) the UE, in 5GMM-CONNECTED mode or in 5GMM-CONNECTED mode with RRC inactive indication, has user data pending
    // due to no user-plane resources established for PDU session(s) used for user data transport
    CONNECTED_UPLINK_DATA_PENDING,
    // f) the UE in 5GMM-IDLE mode over non-3GPP access, receives an indication from the lower layers of non-3GPP
    // access, that the access stratum connection is established between UE and network
    NON_3GPP_AS_ESTABLISHED,
    // g) the UE, in 5GMM-IDLE mode over 3GPP access, receives a notification from the network with access type
    // indicating 3GPP access when the UE is in 5GMM-CONNECTED mode over non-3GPP access
    IDLE_3GPP_NOTIFICATION_N3GPP,
    // h) the UE, in 5GMM-IDLE, 5GMM-CONNECTED mode over 3GPP access, or 5GMM-CONNECTED mode with RRC inactive
    // indication, receives a request for emergency services fallback from the upper layer and performs emergency
    // services fallback as specified in subclause 4.13.4.2 of 3GPP TS 23.502 [9]
    EMERGENCY_FALLBACK,
    // i) the UE, in 5GMM-CONNECTED mode over 3GPP access or in 5GMM-CONNECTED mode with RRC inactive indication,
    // receives a fallback indication from the lower layers (see subclauses 5.3.1.2 and 5.3.1.4) and or the UE has a
    // pending NAS procedure other than a registration, service request, or de-registration procedure
    FALLBACK_INDICATION
};

enum class EProcRc
{
    OK,
    CANCEL,
    STAY,
};

struct ProcControl
{
    std::optional<EInitialRegCause> initialRegistration{};
    std::optional<ERegUpdateCause> mobilityRegistration{};
    std::optional<EServiceReqCause> serviceRequest{};
    std::optional<EDeregCause> deregistration{};
};

struct UacInput
{
    std::bitset<16> identities;
    int category{};
    int establishmentCause{};
};

enum class EUacResult
{
    ALLOWED,
    BARRED,
    BARRING_APPLICABLE_EXCEPT_0_2,
};

struct UacOutput
{
    EUacResult res{};
};

enum class ENasTransportHint
{
    PDU_SESSION_ESTABLISHMENT_REQUEST,
    PDU_SESSION_ESTABLISHMENT_ACCEPT,
    PDU_SESSION_ESTABLISHMENT_REJECT,

    PDU_SESSION_AUTHENTICATION_COMMAND,
    PDU_SESSION_AUTHENTICATION_COMPLETE,
    PDU_SESSION_AUTHENTICATION_RESULT,

    PDU_SESSION_MODIFICATION_REQUEST,
    PDU_SESSION_MODIFICATION_REJECT,
    PDU_SESSION_MODIFICATION_COMMAND,
    PDU_SESSION_MODIFICATION_COMPLETE,
    PDU_SESSION_MODIFICATION_COMMAND_REJECT,

    PDU_SESSION_RELEASE_REQUEST,
    PDU_SESSION_RELEASE_REJECT,
    PDU_SESSION_RELEASE_COMMAND,
    PDU_SESSION_RELEASE_COMPLETE,

    FIVEG_SM_STATUS
};

Json ToJson(const ECmState &state);
Json ToJson(const ERmState &state);
Json ToJson(const EMmState &state);
Json ToJson(const EMmSubState &state);
Json ToJson(const E5UState &state);
Json ToJson(const NasTimers &v);
Json ToJson(const ERegUpdateCause &v);
Json ToJson(const EPsState &v);
Json ToJson(const EServiceReqCause &v);
Json ToJson(const ERrcState &v);
Json ToJson(const CellMeasurement &v);
Json ToJson(const EMeasSourceType &v);


} // namespace nr::ue
