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

enum class EReqestingTask
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
    } present;

    // SIGNAL_DETECTED
    // UPLINK_RRC
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
    } present;

    // RRC_PDU_DELIVERY
    int64_t ueId{};
    int cRnti{};
    rrc::RrcChannel channel{};
    OctetString pdu{};

    // RADIO_BEARER_UPDATE
    std::unique_ptr<RadioBearerUpdate> rbUpdate{};
    std::unique_ptr<SdapUpdate> sdapUpdate{};
    
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
        HANDOVER_NOTIFY_SEND,
        HANDOVER_REQUIRED,
        PATH_SWITCH_REQUEST,
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
    } present;

    // UE_CONTEXT_UPDATE
    std::unique_ptr<GtpUeContextUpdate> update{};

    // SESSION_CREATE
    PduSessionResource *resource{};

    // UE_CONTEXT_RELEASE_RECEIVED
    // SESSION_RELEASE
    int64_t ueId{};
    int cRnti{};

    // UE_CONTEXT_RELEASE_RECEIVED
    NgapCause cause{NgapCause::RadioNetwork_unspecified};

    // SESSION_RELEASE
    int psi{};

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
    } present;

    uint32_t xnTxId{};
    int64_t ueId{};
    int64_t targetNci{};
    uint32_t sourceGnbId{};
    bool isCho{};
    std::unique_ptr<OctetString> rrcContainer{};
    std::unique_ptr<std::vector<PduSessionResource>> sessionList{};
    int reason{};

    explicit NmGnbXnToRrc(PR present) : NtsMessage(NtsMessageType::GNB_XN_TO_RRC), present(present)
    {
    }
};


struct NmGnbSctp : NtsMessage
{
    enum PR
    {
        CONNECTION_REQUEST,
        CONNECTION_CLOSE,
        ASSOCIATION_SETUP,
        ASSOCIATION_SHUTDOWN,
        RECEIVE_MESSAGE,
        SEND_MESSAGE,
        UNHANDLED_NOTIFICATION,
    } present;

    // CONNECTION_REQUEST
    // CONNECTION_CLOSE
    // ASSOCIATION_SETUP
    // ASSOCIATION_SHUTDOWN
    // RECEIVE_MESSAGE
    // SEND_MESSAGE
    // UNHANDLED_NOTIFICATION
    int clientId{};

    // CONNECTION_REQUEST
    std::string localAddress{};
    uint16_t localPort{};
    std::string remoteAddress{};
    uint16_t remotePort{};
    sctp::PayloadProtocolId ppid{};
    NtsTask *associatedTask{};
    uint16_t maxTxStreams{10};
    uint16_t maxRxStreams{10};

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
