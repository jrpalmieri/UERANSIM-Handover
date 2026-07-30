//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include "types.hpp"

#include <utility>

#include <lib/app/cli_base.hpp>
#include <lib/app/cli_cmd.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rls/rls_base.hpp>
#include <lib/rrc/rrc.hpp>
#include <lib/sctp/sctp.hpp>
#include <utils/network.hpp>
#include <utils/nts.hpp>
#include <utils/octet_string.hpp>
#include <utils/unique_buffer.hpp>

#include <asn/xnap/ASN_XNAP_Cause.h>

extern "C"
{
    struct ASN_NGAP_FiveG_S_TMSI;
    struct ASN_NGAP_TAIListForPaging;
}

namespace nr::gnb
{

enum class ERequestingTask
{
    NGAP,
    RRC,
    XN,
    GTP,
    RLS,
};

struct NmGnbRlsToRrc : NtsMessage
{
    enum PR
    {
        SIGNAL_DETECTED,
        UPLINK_RRC,
        RADIO_LINK_FAILURE,
    } present;

    // SIGNAL_DETECTED
    // UPLINK_RRC
    // RADIO_LINK_FAILURE
    int64_t ueId{};

    // SIGNAL_DETECTED
    // UPLINK_RRC
    int cRnti{};

    // UPLINK_RRC
    OctetString data;
    rrc::RrcChannel rrcChannel{};

    // UPLINK_RRC
    // Last known UE geographic position, carried from the RLS heartbeat.
    GeoPosition uePos{};
    bool        hasPosData{false};

    explicit NmGnbRlsToRrc(PR present) : NtsMessage(NtsMessageType::GNB_RLS_TO_RRC), present(present)
    {
    }
};

struct NmGnbRlsToGtp : NtsMessage
{
    enum PR
    {
        DATA_PDU_DELIVERY,
    } present;

    // DATA_PDU_DELIVERY
    int64_t ueId{};
    int cRnti{};
    int psi{};
    int qfi;
    OctetString pdu;

    explicit NmGnbRlsToGtp(PR present) : NtsMessage(NtsMessageType::GNB_RLS_TO_GTP), present(present)
    {
    }
};

struct NmGnbGtpToRls : NtsMessage
{
    enum PR
    {
        DATA_PDU_DELIVERY,
        SESSION_UPDATE,
    } present;

    // DATA_PDU_DELIVERY
    // SESSION_UPDATE
    int64_t ueId{};
    int psi{};
    int qfi{};

    // SESSION_UPDATE
    std::unique_ptr<PduSessionSdapUpdate> sdap{};

    // DATA_PDU_DELIVERY
    OctetString pdu{};

    explicit NmGnbGtpToRls(PR present) : NtsMessage(NtsMessageType::GNB_GTP_TO_RLS), present(present)
    {
    }
};

struct NmGnbRlsToRls : NtsMessage
{
    enum PR
    {
        SIGNAL_DETECTED,
        SIGNAL_LOST,
        RECEIVE_RLS_MESSAGE,
        DOWNLINK_RRC,
        DOWNLINK_DATA,
        UPLINK_RRC,
        UPLINK_DATA,
        RADIO_LINK_FAILURE,
        TRANSMISSION_FAILURE,
        RADIO_BEARER_UPDATE,
        APPLY_DRB_SN_STATUS,
        REMOVE_UE_CONTEXT,
    } present;

    // SIGNAL_DETECTED
    // SIGNAL_LOST
    // DOWNLINK_RRC
    // DOWNLINK_DATA
    // UPLINK_DATA
    // UPLINK_RRC
    int64_t ueId{};
    int cRnti{};

    // RECEIVE_RLS_MESSAGE
    std::unique_ptr<rls::RlsMessage> msg{};

    // DOWNLINK_DATA
    // UPLINK_DATA
    int psi{};
    int qfi{};

    // DOWNLINK_DATA
    // DOWNLINK_RRC
    // UPLINK_DATA
    // UPLINK_RRC
    OctetString data;
    uint8_t radioBearer{};

    // DOWNLINK_RRC
    uint32_t pduId{};

    // DOWNLINK_RRC
    // UPLINK_RRC
    rrc::RrcChannel rrcChannel{};

    // RADIO_LINK_FAILURE
    rls::ERlfCause rlfCause{};

    // TRANSMISSION_FAILURE
    std::vector<rls::PduInfo> pduList;

    // RADIO_BEARER_UPDATE
    std::unique_ptr<RadioBearerUpdate> rbUpdate{};
    std::unique_ptr<SdapUpdate> sdapUpdate{};
    std::vector<DrbSnStatus> drbSnStatus{};


    explicit NmGnbRlsToRls(PR present) : NtsMessage(NtsMessageType::GNB_RLS_TO_RLS), present(present)
    {
    }
};

struct NmGnbRrcToRls : NtsMessage
{
    enum PR
    {
        RRC_PDU_DELIVERY,
        RADIO_BEARER_UPDATE,
        APPLY_DRB_SN_STATUS,
        REMOVE_UE_CONTEXT,
    } present;

    // RRC_PDU_DELIVERY
    int64_t ueId{};
    int cRnti{};
    rrc::RrcChannel channel{};
    OctetString pdu{};

    // RADIO_BEARER_UPDATE
    std::unique_ptr<RadioBearerUpdate> rbUpdate{};
    std::unique_ptr<SdapUpdate> sdapUpdate{};
    std::vector<DrbSnStatus> drbSnStatus{};
    
    explicit NmGnbRrcToRls(PR present) : NtsMessage(NtsMessageType::GNB_RRC_TO_RLS), present(present)
    {
    }
};

struct NmGnbNgapToRrc : NtsMessage
{
    enum PR
    {
        RADIO_POWER_ON,
        NAS_DELIVERY,
        NAS_ACCEPT,
        UE_CONTEXT_RELEASE_RECEIVED,
        PAGING,
        HANDOVER_REQUEST_RECEIVED,
        HANDOVER_COMMAND_RECEIVED,
        HANDOVER_PREPARATION_FAILURE_RECEIVED,
        PATH_SWITCH_REQUEST_ACK,
        PATH_SWITCH_REQUEST_FAILURE,
        SECURITY_INFO,
        PDU_SESSION_UPDATE,
    } present;

    uint32_t ngapTxId{};

    // HANDOVER_COMMAND_DELIVERY
    std::unique_ptr<OctetString> rrcContainer{};
    int64_t hoTargetNci{};
    int hoNewCrnti{};
    bool isCho{};

    // NAS_DELIVERY
    // UE_CONTEXT_RELEASE_RECEIVED
    int64_t ueId{};
    int cRnti{};

    // UE_CONTEXT_RELEASE_RECEIVED
    NgapCause cause{NgapCause::RadioNetwork_unspecified};

    // NAS_ACCEPT
    std::unique_ptr<std::vector<PduSessionResource>> sessionList{};

    // NAS_DELIVERY
    // NAS_ACCEPT
    OctetString pdu{};

    // PAGING
    asn::Unique<ASN_NGAP_FiveG_S_TMSI> uePagingTmsi{};
    asn::Unique<ASN_NGAP_TAIListForPaging> taiListForPaging{};

    // Security Info
    std::unique_ptr<UeSecurityInfo> ueSecInfo{};

    // PDU_SESSION_UPDATE
    std::unique_ptr<PduSessionSdapUpdate> sdapUpdate{};

    // PDU_SESSION_UPDATE — PSIs the 5GC has released (path-switch ack or
    // PDUSessionResourceReleaseCommand); RRC tears down their DRB/SDAP state.
    std::vector<int> releasedPsis{};

    // PATH_SWITCH_REQUEST_ACK — fresh {NCC, NH} pair from the AMF's mandatory
    // SecurityContext IE, stored by RRC for the next handover's key derivation
    bool hasSecurityContext{};
    int nextHopChainingCount{};
    std::array<uint8_t, 32> nextHopParameter{};

    explicit NmGnbNgapToRrc(PR present) : NtsMessage(NtsMessageType::GNB_NGAP_TO_RRC), present(present)
    {
    }
};

struct NmGnbRrcToNgap : NtsMessage
{
    enum PR
    {
        INITIAL_NAS_DELIVERY,
        UPLINK_NAS_DELIVERY,
        RADIO_LINK_FAILURE,
        HANDOVER_REQUEST_ACK_SEND,
        HANDOVER_FAILURE_SEND,            // ngapTxId, hoCause — target RRC rejected a HandoverRequest;
                                          // NGAP sends HandoverFailure to the AMF and drops its pending entry
        HANDOVER_NOTIFY_SEND,
        HANDOVER_REQUIRED,
        PATH_SWITCH_REQUEST,
        XN_HANDOVER_PREPARE,              // target-side provisional NGAP/GTP setup
        XN_SOURCE_CONTEXT_RELEASE,        // source cleanup after target path switch
        XN_TARGET_PREPARATION_CANCEL,     // rollback provisional target NGAP/GTP state
    } present;

    uint32_t ngapTxId{};

    // HANDOVER_REQUIRED
    int64_t hoTargetNci{};
    NgapCause hoCause{};
    std::unique_ptr<GnbCondHandoverRequest> choParams{nullptr};
    int retries=0;
    std::unique_ptr<OctetString> rrcContainer{};
    std::unique_ptr<std::vector<PduSessionResource>> admittedSessions{};
    std::unique_ptr<std::vector<PduSessionResource>> rejectedSessions{};

    // INITIAL_NAS_DELIVERY
    // UPLINK_NAS_DELIVERY
    // RADIO_LINK_FAILURE
    int64_t ueId{};
    int cRnti{};

    // XN_HANDOVER_PREPARE.  admittedSessions remains owned by this message;
    // NGAP moves it into its pending record before queuing pointers to GTP.
    std::unique_ptr<XnHandoverCoreContext> xnCoreContext{};

    // INITIAL_NAS_DELIVERY
    // UPLINK_NAS_DELIVERY
    OctetString pdu{};

    // INITIAL_NAS_DELIVERY
    int64_t rrcEstablishmentCause{};
    std::optional<GutiMobileIdentity> sTmsi{};

    explicit NmGnbRrcToNgap(PR present) : NtsMessage(NtsMessageType::GNB_RRC_TO_NGAP), present(present)
    {
    }
};

struct NmGnbNgapToGtp : NtsMessage
{
    enum PR
    {
        UE_CONTEXT_UPDATE,
        UE_CONTEXT_RELEASE_RECEIVED,
        SESSION_CREATE,
        SESSION_RELEASE,
        FORWARDING_TUNNEL_SETUP,
        SESSION_UL_TUNNEL_UPDATE, // 5GC re-allocated the UL NG-U endpoint at path switch
    } present;

    // UE_CONTEXT_UPDATE
    std::unique_ptr<GtpUeContextUpdate> update{};

    // SESSION_CREATE
    // The message owns the resource. The GTP task runs on its own thread and keeps the
    // session past the end of the request that created it, so it is given an independent
    // copy rather than a borrowed pointer into the sender's storage.
    std::unique_ptr<PduSessionResource> resource{};

    // UE_CONTEXT_RELEASE_RECEIVED
    // SESSION_RELEASE
    int64_t ueId{};
    int cRnti{};

    // UE_CONTEXT_RELEASE_RECEIVED
    NgapCause cause{NgapCause::RadioNetwork_unspecified};

    // SESSION_RELEASE
    int psi{};

    // FORWARDING_TUNNEL_SETUP
    GtpTunnel forwardingTunnel{};

    // SESSION_UL_TUNNEL_UPDATE
    GtpTunnel ulTunnel{};

    explicit NmGnbNgapToGtp(PR present) : NtsMessage(NtsMessageType::GNB_NGAP_TO_GTP), present(present)
    {
    }
};

struct NmGnbRrcToXn : NtsMessage
{
    enum PR
    {
        HANDOVER_REQUEST_SEND,            // ueId, targetNci, isCho
        HANDOVER_REQUEST_ACK_SEND,        // ueId, targetNci, isCho, rrcReconfigIe
        HANDOVER_CANCEL_SEND,             // ueId, targetNci, isCho
        HANDOVER_PREPARATION_FAILURE_SEND,// ueId, targetNci, isCho, reason
        UE_CONTEXT_RELEASE_SEND,          // ueId, targetNci
        SN_STATUS_TRANSFER_SEND,          // ueId, targetNci, isCho
        HANDOVER_SUCCESS_SEND,            // ueId, targetNci
        CONDITION_HANDOVER_CANCEL_SEND,   // ueId, targetNci
    } present;

    int xnTxId{};
    int64_t ueId{};
    int64_t targetNci{};
    std::unique_ptr<GnbCondHandoverRequest> choParams{nullptr};
    ASN_XNAP_Cause_PR reason{};
    int retries=0;
    std::unique_ptr<OctetString> rrcContainer{};
    std::unique_ptr<std::vector<PduSessionResource>> admittedSessions{};
    std::unique_ptr<std::vector<PduSessionResource>> rejectedSessions{};

    explicit NmGnbRrcToXn(PR present) : NtsMessage(NtsMessageType::GNB_RRC_TO_XN), present(present)
    {
    }
};

struct NmGnbXnToRrc : NtsMessage
{
    enum PR
    {
        HANDOVER_REQUEST_RECEIVED,             // ueId, gnbId, pdu
        HANDOVER_PREPARATION_FAILURE_RECEIVED, // ueId, targetNci, isCho, reason
        HANDOVER_REQUEST_ACK_RECEIVED,         // ueId, targetNci, isCho, rrcContainer
        HANDOVER_CANCEL_RECEIVED,              // ueId, targetNci, isCho
        UE_CONTEXT_RELEASE_RECEIVED,           // ueId, targetNci
        SN_STATUS_TRANSFER_RECEIVED,           // ueId, targetNci, isCho
        HANDOVER_SUCCESS_RECEIVED,             // ueId, targetNci
        HANDOVER_ABORT,
    } present;

    uint32_t xnTxId{};
    int64_t ueId{};
    int64_t targetNci{};
    uint32_t sourceGnbId{};
    bool isCho{};
    std::unique_ptr<OctetString> rrcContainer{};
    std::unique_ptr<std::vector<PduSessionResource>> sessionList{};
    std::unique_ptr<XnHandoverCoreContext> xnCoreContext{};
    std::unique_ptr<XnChoRequest> xnChoRequest{};
    std::vector<DrbSnStatus> drbSnStatus{};
    int reason{};
    // HANDOVER_REQUEST_RECEIVED extras
    int64_t amfUeNgapId{};
    Guami guami{};
    UeSecurityInfo ueSecInfo{};
    uint64_t dlAmbr{};
    uint64_t ulAmbr{};
    std::string ngapSourceIpAddr{};

    enum ABORT_REASON
    {
        ABORT_REASON_NONE,
        ABORT_REASON_SOURCE_PREPARATION_TIMEOUT,
        ABORT_REASON_SOURCE_OVERALL_TIMEOUT,
        ABORT_REASON_TARGET_PREPARATION_TIMEOUT,
        ABORT_REASON_TARGET_OVERALL_TIMEOUT,
        ABORT_REASON_XN_FAILURE,
    } abortReason{ABORT_REASON_NONE};
    
    explicit NmGnbXnToRrc(PR present) : NtsMessage(NtsMessageType::GNB_XN_TO_RRC), present(present)
    {
    }
};

struct NmGnbXnToGtp : NtsMessage
{
    enum PR
    {
        FORWARDING_TUNNEL_SETUP,
        FORWARDING_TEID_REGISTER,
    } present;

    // FORWARDING_TUNNEL_SETUP / FORWARDING_TEID_REGISTER
    int64_t ueId{};
    int psi{};

    // FORWARDING_TUNNEL_SETUP
    GtpTunnel forwardingTunnel{};

    // FORWARDING_TEID_REGISTER (target side: DL Xn-U TEID advertised in the
    // HandoverRequestAcknowledge, on which forwarded packets will arrive)
    uint32_t teid{};

    explicit NmGnbXnToGtp(PR present) : NtsMessage(NtsMessageType::GNB_XN_TO_GTP), present(present)
    {
    }
};

struct NmGnbSctp : NtsMessage
{
    enum PR
    {
        CONNECTION_REQUEST,
        CONNECTION_CLOSE,
        CONNECTION_FAILED, // sent to the associatedTask when a CONNECTION_REQUEST's bind/connect fails
        LISTEN_REQUEST,    // ask the SCTP task to accept inbound associations on localAddress:localPort
        CONNECTION_ACCEPTED, // internal: accept thread -> SCTP task, carries the accepted fd
        ASSOCIATION_SETUP,
        ASSOCIATION_SHUTDOWN,
        RECEIVE_MESSAGE,
        SEND_MESSAGE,
        UNHANDLED_NOTIFICATION,
    } present;

    // CONNECTION_REQUEST
    // CONNECTION_CLOSE
    // CONNECTION_FAILED
    // ASSOCIATION_SETUP
    // ASSOCIATION_SHUTDOWN
    // RECEIVE_MESSAGE
    // SEND_MESSAGE
    // UNHANDLED_NOTIFICATION
    int clientId{};

    // CONNECTION_REQUEST
    // LISTEN_REQUEST (localAddress/localPort/ppid/associatedTask/max*Streams)
    std::string localAddress{};
    uint16_t localPort{};
    std::string remoteAddress{};
    uint16_t remotePort{};
    sctp::PayloadProtocolId ppid{};
    NtsTask *associatedTask{};
    uint16_t maxTxStreams{10};
    uint16_t maxRxStreams{10};

    // CONNECTION_ACCEPTED
    int acceptedFd{-1};

    // ASSOCIATION_SETUP
    int associationId{};
    int inStreams{};
    int outStreams{};

    // RECEIVE_MESSAGE
    // SEND_MESSAGE
    UniqueBuffer buffer{};
    uint16_t stream{};

    explicit NmGnbSctp(PR present) : NtsMessage(NtsMessageType::GNB_SCTP), present(present)
    {
    }
};

struct NmGnbStatusUpdate : NtsMessage
{
    static constexpr const int NGAP_IS_UP = 1;

    const int what;

    // NGAP_IS_UP
    bool isNgapUp{};

    explicit NmGnbStatusUpdate(const int what) : NtsMessage(NtsMessageType::GNB_STATUS_UPDATE), what(what)
    {
    }
};

struct NmGnbCliCommand : NtsMessage
{
    std::unique_ptr<app::GnbCliCommand> cmd;
    InetAddress address;

    NmGnbCliCommand(std::unique_ptr<app::GnbCliCommand> cmd, InetAddress address)
        : NtsMessage(NtsMessageType::GNB_CLI_COMMAND), cmd(std::move(cmd)), address(address)
    {
    }
};

} // namespace nr::gnb
