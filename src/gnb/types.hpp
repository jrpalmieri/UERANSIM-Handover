//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <lib/app/monitor.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rrc/common/event_types.hpp>
#include <utils/common_types.hpp>
#include <utils/logger.hpp>
#include <utils/network.hpp>
#include <utils/nts.hpp>
#include <utils/octet_string.hpp>
#include <utils/constants.hpp>

#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestList.h>
#include <asn/rrc/ASN_RRC_InitialUE-Identity.h>

#include <asn/rrc/ASN_RRC_MeasConfig.h>

namespace utils
{
class SatTime;
}

namespace nr::gnb
{

class GnbAppTask;
class GtpTask;
class NgapTask;
class GnbRrcTask;
class GnbRlsTask;
class SctpTask;
class XnTask;

struct NgapUeContext;
struct RrcUeContext;

class GnbNeighbors;    // fwd decl (neighbors.hpp)
class SatTleStore;     // fwd decl (sat_tle_store.hpp)

enum class EAmfState
{
    NOT_CONNECTED = 0,
    WAITING_NG_SETUP,
    CONNECTED
};

struct RRCHandoverPending
{
    int id{};
    RrcUeContext* ctx{};
    uint64_t expireTime{};
    int64_t txId{};
};

struct HandoverMeasurementIdentity
{
    long measId{};
    long measObjectId{};
    long reportConfigId{};
    nr::rrc::common::HandoverEventType eventKind{};
    std::string eventType{};
};

struct HandoverPreparationInfo
{
    std::vector<HandoverMeasurementIdentity> measIdentities{};
    OctetString measConfigRrcReconfiguration{};
};

struct NGAPHandoverPending
{
    int id{};
    NgapUeContext* ctx{};
    uint64_t expireTime{};
    bool choCandidate{};
};

struct SctpAssociation
{
    int associationId{};
    int inStreams{};
    int outStreams{};
};

struct Guami
{
    Plmn plmn{};
    int amfRegionId{}; // 8-bit
    int amfSetId{};    // 10-bit
    int amfPointer{};  // 6-bit
};

struct ServedGuami
{
    Guami guami{};
    std::string backupAmfName{};
};

// TODO: update cli and json for overload related types

enum class EOverloadAction
{
    UNSPECIFIED_OVERLOAD,
    REJECT_NON_EMERGENCY_MO_DATA,
    REJECT_SIGNALLING,
    ONLY_EMERGENCY_AND_MT,
    ONLY_HIGH_PRI_AND_MT,
};

enum class EOverloadStatus
{
    NOT_OVERLOADED,
    OVERLOADED
};

struct OverloadInfo
{
    struct Indication
    {
        // Reduce the signalling traffic by the indicated percentage
        int loadReductionPerc{};

        // If reduction percentage is not present, this action shall be used
        EOverloadAction action{};
    };

    EOverloadStatus status{};
    Indication indication{};
};

struct NgapAmfContext
{
    int ctxId{};
    SctpAssociation association{};
    int nextStream{}; // next available SCTP stream for uplink
    std::string address{};
    uint16_t port{};
    std::string amfName{};
    int64_t relativeCapacity{};
    EAmfState state{};
    OverloadInfo overloadInfo{};
    std::vector<ServedGuami *> servedGuamiList{};
    std::vector<PlmnSupport *> plmnSupportList{};
};

struct RlsUeContext
{
    const int ueId;
    uint64_t sti{};
    InetAddress addr{};
    int64_t lastSeen{};

    explicit RlsUeContext(int ueId) : ueId(ueId)
    {
    }
};

struct AggregateMaximumBitRate
{
    uint64_t dlAmbr{};
    uint64_t ulAmbr{};
};

enum UE_NGAP_CONNECTION_STATE {
    NGAP_NOT_CONNECTED = 0,
    NGAP_CONNECTION_PENDING,
    NGAP_CONNECTED
};

// The NGAP context for a UE
struct NgapUeContext
{
    const int ctxId{};

    // State tracker for connection state
    UE_NGAP_CONNECTION_STATE connectionState{UE_NGAP_CONNECTION_STATE::NGAP_NOT_CONNECTED};

    // AMF UE NGAP ID is assigned by the AMF
    int64_t amfUeNgapId = -1; // -1 if not assigned

    // RAN UE NGAP ID is assigned by the gNB
    int64_t ranUeNgapId{};

    // tracks the AMF associated with the UE
    int associatedAmfId{};
    
    // Uplink SCTP stream ID for this UE
    int uplinkStream{};
    // Downlink SCTP stream ID for this UE
    int downlinkStream{};
    // Aggregate Maximimu Bit Rate
    AggregateMaximumBitRate ueAmbr{};
    // All PDU Session IDs associated with this UE
    std::set<int> pduSessions{};

    explicit NgapUeContext(int ctxId) : ctxId(ctxId)
    {
    }
};

struct PositionVelocity
{
    bool isValid{false};
    double x{};
    double y{};
    double z{};
    double vx{};
    double vy{};
    double vz{};
    int64_t epochMs{};
};

// tracks internal state of the RRC connection process for a UE
//   NOT the same as the RRC 3GPP states
enum UE_RRC_CONNECTION_STATE {
    RRC_NOT_CONNECTED = 0,
    RRC_CONNECTION_PENDING,
    RRC_CONNECTED
};

struct RrcUeContext
{


    // UE's unique identifier, provided by UE in RLS messages
    int ueId{};

    // Temporary cell-specific ID for this UE, assigned be serving gNB
    int cRnti{};

    int64_t initialId = -1; // 39-bit value, or -1
    bool isInitialIdSTmsi{}; // TMSI-part-1 or a random value
    int64_t establishmentCause{};

    // RRC connection state of the UE
    UE_RRC_CONNECTION_STATE rrcState{UE_RRC_CONNECTION_STATE::RRC_NOT_CONNECTED};

    // 5G GUTI
    std::optional<GutiMobileIdentity> sTmsi{};

    /* Handover state */

    bool handoverInProgress{};
    int handoverTargetPci{};
    int handoverNewCrnti{};
    int observedRadioUeId{};
    long handoverTxId{};

    // Active Measurement Config IEs

    struct MeasIdentityMappings
    {
        long measId{};
        long measObjectId{};
        long reportConfigId{};
        nr::rrc::common::HandoverEventType eventKind{};
        std::string eventType{};
        int choProfileId{};
    };

    // list of measurement identities currently configured for this UE, along with their associated object and report config IDs and event types
    std::vector<MeasIdentityMappings> usedMeasIdentities{};

    // list of measuObjectIds currently configured for this UE, to avoid conflicting reuse
    std::vector<long> usedMeasObjectIds{};

    // list of reportConfigIds currently configured for this UE, to avoid conflicting reuse
    std::vector<long> usedReportConfigIds{};

    // stores pointers to sent MeasConfig messages along with the measIds used in that config, for potential future reference (e.g. handovers)
    std::vector<std::tuple<ASN_RRC_MeasConfig*, std::vector<long>>> sentMeasConfigs{};


    // UE position (populated from RLS heartbeat data via UPLINK_RRC path)
    GeoPosition uePosition{};

    /* Last measurement report data */

    int lastMeasReportPci{-1};
    int lastMeasReportRsrp{-140};
    int lastServingRsrp{-140};
    bool handoverDecisionPending{};
    bool choPreparationPending{};
    
    /* CHO State Tracking */

    std::vector<int> choPreparationCandidatePcis{};
    std::unordered_map<int, int> choPreparationCandidateScores{};
    std::vector<long> choPreparationMeasIds{};
    std::unordered_set<int> usedCondReconfigIds{};
    std::optional<int> choPreparationCandidateProfileId{};
    int choPreparationTriggerTimerSec{};
    int choPreparationDistanceThreshold{};
    ASN_RRC_MeasConfig* choPreparationMeasConfig{};
    

    [[nodiscard]] const MeasIdentityMappings *findSentMeasIdentity(long measId) const
    {
        for (const auto &entry : usedMeasIdentities)
        {
            if (entry.measId == measId)
                return &entry;
        }
        return nullptr;
    }

    explicit RrcUeContext(const int cRnti) : cRnti(cRnti)
    {
    }
};

struct NgapIdPair
{
    std::optional<int64_t> amfUeNgapId{};
    std::optional<int64_t> ranUeNgapId{};

    NgapIdPair() : amfUeNgapId{}, ranUeNgapId{}
    {
    }

    NgapIdPair(const std::optional<int64_t> &amfUeNgapId, const std::optional<int64_t> &ranUeNgapId)
        : amfUeNgapId(amfUeNgapId), ranUeNgapId(ranUeNgapId)
    {
    }
};

enum class NgapCause
{
    RadioNetwork_unspecified = 0,
    RadioNetwork_txnrelocoverall_expiry,
    RadioNetwork_successful_handover,
    RadioNetwork_release_due_to_ngran_generated_reason,
    RadioNetwork_release_due_to_5gc_generated_reason,
    RadioNetwork_handover_cancelled,
    RadioNetwork_partial_handover,
    RadioNetwork_ho_failure_in_target_5GC_ngran_node_or_target_system,
    RadioNetwork_ho_target_not_allowed,
    RadioNetwork_tngrelocoverall_expiry,
    RadioNetwork_tngrelocprep_expiry,
    RadioNetwork_cell_not_available,
    RadioNetwork_unknown_targetID,
    RadioNetwork_no_radio_resources_available_in_target_cell,
    RadioNetwork_unknown_local_UE_NGAP_ID,
    RadioNetwork_inconsistent_remote_UE_NGAP_ID,
    RadioNetwork_handover_desirable_for_radio_reason,
    RadioNetwork_time_critical_handover,
    RadioNetwork_resource_optimisation_handover,
    RadioNetwork_reduce_load_in_serving_cell,
    RadioNetwork_user_inactivity,
    RadioNetwork_radio_connection_with_ue_lost,
    RadioNetwork_radio_resources_not_available,
    RadioNetwork_invalid_qos_combination,
    RadioNetwork_failure_in_radio_interface_procedure,
    RadioNetwork_interaction_with_other_procedure,
    RadioNetwork_unknown_PDU_session_ID,
    RadioNetwork_unkown_qos_flow_ID,
    RadioNetwork_multiple_PDU_session_ID_instances,
    RadioNetwork_multiple_qos_flow_ID_instances,
    RadioNetwork_encryption_and_or_integrity_protection_algorithms_not_supported,
    RadioNetwork_ng_intra_system_handover_triggered,
    RadioNetwork_ng_inter_system_handover_triggered,
    RadioNetwork_xn_handover_triggered,
    RadioNetwork_not_supported_5QI_value,
    RadioNetwork_ue_context_transfer,
    RadioNetwork_ims_voice_eps_fallback_or_rat_fallback_triggered,
    RadioNetwork_up_integrity_protection_not_possible,
    RadioNetwork_up_confidentiality_protection_not_possible,
    RadioNetwork_slice_not_supported,
    RadioNetwork_ue_in_rrc_inactive_state_not_reachable,
    RadioNetwork_redirection,
    RadioNetwork_resources_not_available_for_the_slice,
    RadioNetwork_ue_max_integrity_protected_data_rate_reason,
    RadioNetwork_release_due_to_cn_detected_mobility,
    RadioNetwork_n26_interface_not_available,
    RadioNetwork_release_due_to_pre_emption,
    RadioNetwork_multiple_location_reporting_reference_ID_instances,

    Transport_transport_resource_unavailable = 100,
    Transport_unspecified,

    Nas_normal_release = 200,
    Nas_authentication_failure,
    Nas_deregister,
    Nas_unspecified,

    Protocol_transfer_syntax_error = 300,
    Protocol_abstract_syntax_error_reject,
    Protocol_abstract_syntax_error_ignore_and_notify,
    Protocol_message_not_compatible_with_receiver_state,
    Protocol_semantic_error,
    Protocol_abstract_syntax_error_falsely_constructed_message,
    Protocol_unspecified,

    Misc_control_processing_overload = 400,
    Misc_not_enough_user_plane_processing_resources,
    Misc_hardware_failure,
    Misc_om_intervention,
    Misc_unknown_PLMN,
};

struct GtpTunnel
{
    uint32_t teid{};
    OctetString address{};
};

struct PduSessionResource
{
    const int ueId;
    const int psi;

    AggregateMaximumBitRate sessionAmbr{};
    bool dataForwardingNotPossible{};
    PduSessionType sessionType = PduSessionType::UNSTRUCTURED;
    GtpTunnel upTunnel{};
    GtpTunnel downTunnel{};
    asn::Unique<ASN_NGAP_QosFlowSetupRequestList> qosFlows{};

    PduSessionResource(const int ueId, const int psi) : ueId(ueId), psi(psi)
    {
    }
};

struct GnbStatusInfo
{
    bool isNgapUp{};
};

struct GtpUeContext
{
    const int ueId;
    AggregateMaximumBitRate ueAmbr{};

    explicit GtpUeContext(const int ueId) : ueId(ueId)
    {
    }
};

struct GtpUeContextUpdate
{
    bool isCreate{};
    int ueId{};
    AggregateMaximumBitRate ueAmbr{};

    GtpUeContextUpdate(bool isCreate, int ueId, const AggregateMaximumBitRate &ueAmbr)
        : isCreate(isCreate), ueId(ueId), ueAmbr(ueAmbr)
    {
    }
};

struct GnbAmfConfig
{
    std::string address{};
    uint16_t port{};
};

struct SatelliteLinkConfig
{
    double frequencyHz{10.7e9};  // carrier frequency in Hz
    double txPowerDbm{43.0};     // radiated power in dBm
    double txGainDbi{35.0};      // transmit antenna gain in dBi
    double ueRxGainDbi{25.0};    // UE receive antenna gain in dBi
    
};

enum class ESib19EphemerisMode : uint8_t
{
    PosVel = 0,
    Orbital = 1,
};

struct SIB19Config
{
    bool sib19On{false};
    int sib19TimingMs{1000};
    int satLocUpdateThresholdMs{5000};

    // Ephemeris mode transmitted in SIB19.
    ESib19EphemerisMode ephType{ESib19EphemerisMode::PosVel};

    int32_t kOffset{0};
    int64_t taCommon{0};
    int32_t taCommonDrift{0};
    std::optional<int32_t> taCommonDriftVariation{};
    std::optional<int32_t> ulSyncValidityDuration{};
    std::optional<int32_t> cellSpecificKoffset{};
    std::optional<int32_t> polarization{};
    std::optional<int32_t> taDrift{};
};


struct SatellitePositionVelocityEntry
{
    int pci{};
    double x{};
    double y{};
    double z{};
    double vx{};
    double vy{};
    double vz{};
    int64_t epochMs{};
    int64_t lastUpdatedMs{};
};

// TLE entry for one satellite gNB.  Keyed by PCI and shared across subsystems.
struct SatTleEntry
{
    int pci{};
    std::string line1{};
    std::string line2{};
    int64_t lastUpdatedMs{};  // 0 = loaded from config, otherwise millisecond timestamp of last CLI upsert
};

enum class EGnbRsrpMode
{
    Calculated,
    Fixed,
};

enum class EHandoverInterface
{
    N2,
    Xn,
};

struct GnbRfLinkConfig
{
    EGnbRsrpMode updateMode{EGnbRsrpMode::Calculated};
    int rsrpDbValue{-120};
    double carrFrequencyHz{10.7e9};
    double txPowerDbm{43.0};
    double txGainDbi{35.0};
    double ueRxGainDbi{25.0};

    [[nodiscard]] SatelliteLinkConfig toSatelliteLinkConfig() const
    {
        SatelliteLinkConfig cfg{};
        cfg.frequencyHz = carrFrequencyHz;
        cfg.txPowerDbm = txPowerDbm;
        cfg.txGainDbi = txGainDbi;
        cfg.ueRxGainDbi = ueRxGainDbi;
        return cfg;
    }
};

struct GnbNeighborConfig
{
    int64_t nci{};     // 36-bit
    int idLength{};    // 22..32 bits
    int tac{};         // 24-bit
    std::string ipAddress{};
    EHandoverInterface handoverInterface{EHandoverInterface::N2};
    std::optional<std::string> xnAddress{};
    std::optional<uint16_t> xnPort{};

    [[nodiscard]] inline uint32_t getGnbId() const
    {
        return static_cast<uint32_t>(
            (nci & 0xFFFFFFFFFLL) >>
            (36LL - static_cast<int64_t>(idLength)));
    }

    [[nodiscard]] inline uint64_t getNrCellIdentity() const
    {
        return static_cast<uint64_t>(nci) & 0xFFFFFFFFFULL;
    }

    [[nodiscard]] inline int getCellId() const
    {
        auto bitCount = 36 - idLength;
        auto mask = (1ULL << bitCount) - 1ULL;
        return static_cast<int>(getNrCellIdentity() & mask);
    }

    [[nodiscard]] inline int getPci() const
    {
        return cons::getPciFromNci(nci);
    }
};

struct GnbRsrpConfig
{
    int dbValue{-120};
    EGnbRsrpMode updateMode{EGnbRsrpMode::Calculated};
};


// // valid TimeToTrigger values in milliseconds
// enum class E_TTT_ms {

//     ms0 = 0,
//     ms40 = 1,
//     ms64 = 2,
//     ms80 = 3,
//     ms100 = 4,
//     ms128 = 5,
//     ms160 = 6,
//     ms256 = 7,
//     ms320 = 8,
//     ms480 = 9,
//     ms512 = 10,
//     ms640 = 11,
//     ms1024 = 12,
//     ms1280 = 13,
//     ms2560 = 14,
//     ms5120 = 15
// };

// inline const char* E_TTT_ms_to_string(E_TTT_ms ttt) {
//     static const char* const names[] = {"ms0", "ms40", "ms64", "ms80", "ms100", "ms128", "ms160", "ms256", "ms320", "ms480", "ms512", "ms640", "ms1024", "ms1280", "ms2560", "ms5120"};
//     return names[static_cast<std::size_t>(ttt)];
// }

// inline E_TTT_ms E_TTT_ms_from_string(const std::string& value) {
//     std::string upperValue = value;
//     std::transform(upperValue.begin(), upperValue.end(), upperValue.begin(), [](unsigned char ch) {
//         return static_cast<char>(std::toupper(ch));
//     });

//     if (upperValue == "MS0") {
//         return E_TTT_ms::ms0;
//     } else if (upperValue == "MS40") {
//         return E_TTT_ms::ms40;
//     } else if (upperValue == "MS64") {
//         return E_TTT_ms::ms64;
//     } else if (upperValue == "MS80") {
//         return E_TTT_ms::ms80;
//     } else if (upperValue == "MS100") {
//         return E_TTT_ms::ms100;
//     } else if (upperValue == "MS128") {
//         return E_TTT_ms::ms128;
//     } else if (upperValue == "MS160") {
//         return E_TTT_ms::ms160;
//     } else if (upperValue == "MS256") {
//         return E_TTT_ms::ms256;
//     } else if (upperValue == "MS320") {
//         return E_TTT_ms::ms320;
//     } else if (upperValue == "MS480") {
//         return E_TTT_ms::ms480;
//     } else if (upperValue == "MS512") {
//         return E_TTT_ms::ms512;
//     } else if (upperValue == "MS640") {
//         return E_TTT_ms::ms640;
//     } else if (upperValue == "MS1024") {
//         return E_TTT_ms::ms1024;
//     } else if (upperValue == "MS1280") {
//         return E_TTT_ms::ms1280;
//     } else if (upperValue == "MS2560") {
//         return E_TTT_ms::ms2560;
//     } else if (upperValue == "MS5120") {
//         return E_TTT_ms::ms5120;
//     }
//     return E_TTT_ms::ms0;
// }


struct GnbChoCandidateProfileConfig
{
    int candidateProfileId{0};
    bool targetCellCalculated{true};
    std::optional<int64_t> targetCellId{};
    std::vector<nr::rrc::common::ReportConfigEvent> conditions{};
};

struct GnbHandoverConfig
{
    // struct GnbHandoverReferencePosition
    // {
    //     double latitude{};
    //     double longitude{};
    // };

    // struct GnbHandoverEventConfig
    // {
    //     nr::rrc::common::HandoverEventType eventKind{nr::rrc::common::HandoverEventType::A3};

    //     // Event type string (e.g. "A3", "A2", "D1", "condT1", etc.)
    //     // Kept for compatibility while migrating callers to eventKind.
    //     std::string eventType{"A3"};

    //     // eventA2 fields

    //     int a2_thresholdDbm{-110};
    //     int a2_hysteresisDb{1};
    //     bool a2_reportOnLeave{true};
    //     E_TTT_ms a2_TTT{E_TTT_ms::ms100};

    //     // event a3 fields

    //     int a3_offsetDb{3};
    //     int a3_hysteresisDb{1};
    //     bool a3_reportOnLeave{true};
    //     E_TTT_ms a3_TTT{E_TTT_ms::ms100};
    //     bool a3_useAllowedCellList{false};
        
    //     // eventA5 fields
        
    //     int a5_threshold1Dbm{-110};
    //     int a5_threshold2Dbm{-95};
    //     int a5_hysteresisDb{1};
    //     bool a5_reportOnLeave{true};
    //     E_TTT_ms a5_TTT{E_TTT_ms::ms100};
    //     bool a5_useAllowedCellList{false};

    //     // eventD1-r17 fields

    //     int d1_distanceThreshFromReference1{1000};
    //     int d1_distanceThreshFromReference2{1000};
    //     GnbHandoverReferencePosition d1_referenceLocation1{};
    //     GnbHandoverReferencePosition d1_referenceLocation2{};
    //     int d1_hysteresisLocation{1};
    //     bool d1_reportOnLeave{true};
    //     E_TTT_ms d1_TTT{E_TTT_ms::ms100};

    //     // condEventT1-r17 fields

    //     int condT1_thresholdSecTS{0};
    //     int condT1_durationSec{100};

    //     // condEventD1-r17 fields

    //     int condD1_distanceThreshFromReference1{1000};
    //     int condD1_distanceThreshFromReference2{1000};
    //     GnbHandoverReferencePosition condD1_referenceLocation1{};
    //     GnbHandoverReferencePosition condD1_referenceLocation2{};
    //     int condD1_hysteresisLocation{1};
    //     bool condD1_reportOnLeave{true};
    //     E_TTT_ms condD1_TTT{E_TTT_ms::ms100};
    // };

    struct GnbXnConfig
    {
        bool enabled{false};
        std::string bindAddress{"127.0.0.1"};
        uint16_t bindPort{9487};
        int requestTimeoutMs{1000};
        int contextTtlMs{5000};
        bool fallbackToN2{true};
    };

    bool choEnabled{false};
    int choDefaultProfileId{0};
    std::vector<nr::rrc::common::ReportConfigEvent> events{{}};
    std::vector<GnbChoCandidateProfileConfig> candidateProfiles{};
    GnbXnConfig xn{};
};

struct NtnConfig
{
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
    std::optional<SatTleEntry> ownTle{};  // set when ntn.tle is present in the config file
    TimeWarpConfig timeWarp{};
    SIB19Config sib19{};
    GnbHandoverConfig ntnHandover{};

    // Minimum elevation angle (degrees, integer) below which the satellite gNB is considered
    // at risk of going out of range from the UE.  Used to compute T_exit and to rank CHO candidates.
    // Configurable via ntn.elevationMinDeg; defaults to 20 degrees.
    int elevationMinDeg{20};
};

struct GnbRlsConfig
{
    int loopCounter{1000};
    int receiveTimeout{200};
    int timerPeriodAckControl{1500};
    int timerPeriodAckSend{2250};

    [[nodiscard]] int getHeartbeatThreshold() const
    {
        return loopCounter + receiveTimeout;
    }
};

struct GnbConfig
{
    /* Read from config file */
    int64_t nci{};     // 36-bit
    int gnbIdLength{}; // 22..32 bit
    Plmn plmn{};
    int tac{};
    NetworkSlice nssai{};
    std::vector<GnbAmfConfig> amfConfigs{};
    std::string linkIp{};
    std::string ngapIp{};
    std::string gtpIp{};
    std::optional<std::string> gtpAdvertiseIp{};
    bool ignoreStreamIds{};
    GnbRfLinkConfig rfLink{};
    GnbRlsConfig rls{};
    GnbHandoverConfig handover{};
    std::vector<GnbNeighborConfig> neighborList{};

    NtnConfig ntn{};

    /* Assigned by program */
    std::string name{};
    EPagingDrx pagingDrx{};
    Vector3 phyLocation{};
    GeoPosition geoLocation{};  // lat/lon/alt for the gNB

    [[nodiscard]] inline uint32_t getGnbId() const
    {
        return static_cast<uint32_t>(
            (nci & 0xFFFFFFFFFLL) >>
            (36LL - static_cast<int64_t>(gnbIdLength)));
    }

    [[nodiscard]] inline int getCellId() const
    {
        return static_cast<int>(
            nci & static_cast<uint64_t>(
                      (1 << (36 - gnbIdLength)) - 1));
    }


};

struct TaskBase
{
    GnbConfig *config{};
    LogBase *logBase{};
    app::INodeListener *nodeListener{};
    NtsTask *cliCallbackTask{};

    GnbAppTask *appTask{};
    GtpTask *gtpTask{};
    NgapTask *ngapTask{};
    GnbRrcTask *rrcTask{};
    SctpTask *sctpTask{};
    GnbRlsTask *rlsTask{};
    XnTask *xnTask{};

    SatTleStore *satTleStore{};  // always non-null after GNodeB construction
    utils::SatTime *satTime{};

    GnbNeighbors *neighbors{};

    GeoPosition gnbPosition{};
};

Json ToJson(const GnbStatusInfo &v);
Json ToJson(const GnbConfig &v);
Json ToJson(const NgapAmfContext &v);
Json ToJson(const EAmfState &v);
Json ToJson(const EPagingDrx &v);
Json ToJson(const SctpAssociation &v);
Json ToJson(const ServedGuami &v);
Json ToJson(const Guami &v);

} // namespace nr::gnb