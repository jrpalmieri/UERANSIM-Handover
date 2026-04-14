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
#include <vector>

#include <lib/app/monitor.hpp>
#include <lib/asn/utils.hpp>
#include <utils/common_types.hpp>
#include <utils/logger.hpp>
#include <utils/network.hpp>
#include <utils/nts.hpp>
#include <utils/octet_string.hpp>

#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestList.h>
#include <asn/rrc/ASN_RRC_InitialUE-Identity.h>

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

class SatelliteState;  // fwd decl (satellite_state.hpp)

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

// tracks internal state of the RRC connection process for a UE
//   NOT the same as the RRC 3GPP states
enum UE_RRC_CONNECTION_STATE {
    RRC_NOT_CONNECTED = 0,
    RRC_CONNECTION_PENDING,
    RRC_CONNECTED
};

struct RrcUeContext
{
    struct SentMeasIdentity
    {
        long measId{};
        long measObjectId{};
        long reportConfigId{};
        std::string eventType{};
    };

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

    // Active Measurement Identities
    std::vector<SentMeasIdentity> sentMeasIdentities{};

    /* Last measurement report data */
    
    int lastMeasReportPci{-1};
    int lastMeasReportRsrp{-140};
    int lastServingRsrp{-140};
    bool handoverDecisionPending{};
    bool choPreparationPending{};
    std::vector<int> choPreparationCandidatePcis{};
    std::unordered_map<int, int> choPreparationCandidateScores{};
    std::vector<long> choPreparationMeasIds{};

    [[nodiscard]] const SentMeasIdentity *findSentMeasIdentity(long measId) const
    {
        for (const auto &entry : sentMeasIdentities)
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

struct SIB19Config
{
    bool sib19On{false};
    int sib19TimingMs{1000};

    int32_t kOffset{0};
    int64_t taCommon{0};
    int32_t taCommonDrift{0};
    std::optional<int32_t> taCommonDriftVariation{};
    std::optional<int32_t> ulSyncValidityDuration{};
    std::optional<int32_t> cellSpecificKoffset{};
    std::optional<int32_t> polarization{};
    std::optional<int32_t> taDrift{};
};

struct TruePositionVelocity
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
        // In this simulator, PCI is represented by the 10 least significant bits.
        return static_cast<int>(nci & 0x3FF);
    }
};

struct GnbRsrpConfig
{
    int dbValue{-120};
    EGnbRsrpMode updateMode{EGnbRsrpMode::Calculated};
};

struct GnbHandoverConfig
{
    struct GnbHandoverReferencePosition
    {
        bool useCurrPosition{false};
        double latitude{};
        double longitude{};
        double altitude{};
    };

    struct GnbHandoverEventConfig
    {
        std::string eventType{"A3"};
        int a2ThresholdDbm{-110};
        int a3OffsetDb{3};
        int a5Threshold1Dbm{-110};
        int a5Threshold2Dbm{-95};
        int distanceThreshold{1000};
        int hysteresisDb{1};
        int hysteresisM{0};
        int tttMs{100};
        std::string distanceType{"nadir"};
        bool targetCellCalculated{true};
        std::optional<int64_t> targetCellId{};
        std::optional<GnbHandoverReferencePosition> referencePosition{};
        std::optional<EcefPosition> referencePositionEcef{};
    };

    struct GnbChoEventConfig
    {
        std::vector<GnbHandoverEventConfig> events{};
    };

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
    std::vector<GnbHandoverEventConfig> events{{}};
    std::vector<GnbChoEventConfig> choEvents{};
    GnbXnConfig xn{};
};

struct NtnConfig
{
    bool ntnEnabled{false};
    SIB19Config sib19{};
    GnbHandoverConfig ntnHandover{};
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

    [[nodiscard]] inline const GnbNeighborConfig *findNeighborByPci(int pci) const
    {
        for (const auto &neighbor : neighborList)
        {
            if (neighbor.getPci() == pci)
                return &neighbor;
        }
        return nullptr;
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

    SatelliteState *satState{};  // nullptr when NTN satellite mode is disabled
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