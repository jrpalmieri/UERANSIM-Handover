//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <lib/rrc/encode.hpp>
#include <ue/rls/task.hpp>

#include <asn/rrc/ASN_RRC_RRCReject.h>
#include <asn/rrc/ASN_RRC_RRCSetup.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_UL-CCCH-Message.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-Message.h>
#include <sstream>
#include <iomanip>

namespace nr::ue
{

static const char *decodeResultCodeName(asn_dec_rval_code_e code)
{
    switch (code)
    {
    case RC_OK:
        return "RC_OK";
    case RC_WMORE:
        return "RC_WMORE";
    case RC_FAIL:
        return "RC_FAIL";
    default:
        return "RC_UNKNOWN";
    }
}

static std::string pduHexPreview(const OctetString &pdu, size_t maxBytes = 16)
{
    const uint8_t *buf = pdu.data();
    size_t n = static_cast<size_t>(pdu.length());
    size_t limit = std::min(n, maxBytes);

    std::ostringstream oss;
    for (size_t i = 0; i < limit; i++)
    {
        if (i > 0)
            oss << ' ';
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(buf[i]);
    }

    if (n > limit)
        oss << " ...";

    return oss.str();
}

void UeRrcTask::handleDownlinkRrc(int cellId, rrc::RrcChannel channel, const OctetString &rrcPdu)
{
    if (!hasSignalToCell(cellId))
        return;

    switch (channel)
    {
    // MIB Broadcast
    case rrc::RrcChannel::BCCH_BCH: {
        auto *pdu = rrc::encode::Decode<ASN_RRC_BCCH_BCH_Message>(asn_DEF_ASN_RRC_BCCH_BCH_Message, rrcPdu);
        if (pdu == nullptr)
            m_logger->err("RRC BCCH-BCH PDU decoding failed.");
        else
            receiveRrcMessage(cellId, pdu);
        asn::Free(asn_DEF_ASN_RRC_BCCH_BCH_Message, pdu);
        break;
    }
    // SIB1 Broadcast
    case rrc::RrcChannel::BCCH_DL_SCH: {
        auto *pdu = rrc::encode::Decode<ASN_RRC_BCCH_DL_SCH_Message>(asn_DEF_ASN_RRC_BCCH_DL_SCH_Message, rrcPdu);
        if (pdu == nullptr)
            m_logger->err("RRC BCCH-DL-SCH PDU decoding failed.");
        else
            receiveRrcMessage(cellId, pdu);
        asn::Free(asn_DEF_ASN_RRC_BCCH_DL_SCH_Message, pdu);
        break;
    }
    // RRCSetup, RRCReject
    case rrc::RrcChannel::DL_CCCH: {
        auto *pdu = rrc::encode::Decode<ASN_RRC_DL_CCCH_Message>(asn_DEF_ASN_RRC_DL_CCCH_Message, rrcPdu);
        if (pdu == nullptr)
            m_logger->err("RRC DL-CCCH PDU decoding failed.");
        else
            receiveRrcMessage(cellId, pdu);
        asn::Free(asn_DEF_ASN_RRC_DL_CCCH_Message, pdu);
        break;
    }
    // RRCRelease, RRCReconfiguration, DLInformationTransfer
    case rrc::RrcChannel::DL_DCCH: {
        if (isActiveCell(cellId))
        {
            asn_dec_rval_t decodeRes{};
            auto *pdu = rrc::encode::DecodeWithResult<ASN_RRC_DL_DCCH_Message>(
                asn_DEF_ASN_RRC_DL_DCCH_Message, rrcPdu.data(), static_cast<size_t>(rrcPdu.length()), decodeRes);
            if (pdu == nullptr)
                m_logger->err(
                    "RRC DL-DCCH PDU decoding failed (code=%s(%d), consumed=%zu, len=%d, preview=%s)",
                    decodeResultCodeName(decodeRes.code), static_cast<int>(decodeRes.code), decodeRes.consumed,
                    rrcPdu.length(), pduHexPreview(rrcPdu).c_str());
            else
                receiveRrcMessage(pdu);
            asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
        }
        break;
    };
    // Paging
    case rrc::RrcChannel::PCCH: {
        if (isActiveCell(cellId))
        {
            auto *pdu = rrc::encode::Decode<ASN_RRC_PCCH_Message>(asn_DEF_ASN_RRC_PCCH_Message, rrcPdu);
            if (pdu == nullptr)
                m_logger->err("RRC PCCH PDU decoding failed.");
            else
                receiveRrcMessage(pdu);
            asn::Free(asn_DEF_ASN_RRC_PCCH_Message, pdu);
        }
        break;
    }
    case rrc::RrcChannel::UL_CCCH:
    case rrc::RrcChannel::UL_CCCH1:
    case rrc::RrcChannel::UL_DCCH:
        break;
    case rrc::RrcChannel::DL_CHO: {
        if (isActiveCell(cellId))
        {
            m_logger->warn("Ignoring deprecated DL_CHO channel; use RRCReconfiguration/ConditionalReconfiguration ASN path");
        }
        break;
    }
    case rrc::RrcChannel::DL_SIB19: {
        receiveSib19(cellId, rrcPdu);
        break;
    }
    }
}

// RRCSetupRequest
void UeRrcTask::sendRrcMessage(int cellId, ASN_RRC_UL_CCCH_Message *msg)
{
    OctetString pdu = rrc::encode::EncodeS(asn_DEF_ASN_RRC_UL_CCCH_Message, msg);
    if (pdu.length() == 0)
    {
        m_logger->err("RRC UL-CCCH encoding failed.");
        return;
    }

    auto m = std::make_unique<NmUeRrcToRls>(NmUeRrcToRls::RRC_PDU_DELIVERY);
    m->cellId = cellId;
    m->channel = rrc::RrcChannel::UL_CCCH;
    m->pdu = std::move(pdu);
    m_base->rlsTask->push(std::move(m));
}

void UeRrcTask::sendRrcMessage(int cellId, ASN_RRC_UL_CCCH1_Message *msg)
{
    OctetString pdu = rrc::encode::EncodeS(asn_DEF_ASN_RRC_UL_CCCH1_Message, msg);
    if (pdu.length() == 0)
    {
        m_logger->err("RRC UL-CCCH1 encoding failed.");
        return;
    }

    auto m = std::make_unique<NmUeRrcToRls>(NmUeRrcToRls::RRC_PDU_DELIVERY);
    m->cellId = cellId;
    m->channel = rrc::RrcChannel::UL_CCCH1;
    m->pdu = std::move(pdu);
    m_base->rlsTask->push(std::move(m));
}

// RRCSetupComplete, MeasurementReport, RRCReconfigurationComplete, UplinkInformationTransfer
void UeRrcTask::sendRrcMessage(ASN_RRC_UL_DCCH_Message *msg)
{
    OctetString pdu = rrc::encode::EncodeS(asn_DEF_ASN_RRC_UL_DCCH_Message, msg);
    if (pdu.length() == 0)
    {
        m_logger->err("RRC UL-DCCH encoding failed.");
        return;
    }

    auto m = std::make_unique<NmUeRrcToRls>(NmUeRrcToRls::RRC_PDU_DELIVERY);
    m->cellId = m_base->shCtx.currentCell.get<int>([](auto &value) { return value.cellId; });
    m->channel = rrc::RrcChannel::UL_DCCH;
    m->pdu = std::move(pdu);
    m_base->rlsTask->push(std::move(m));
}

void UeRrcTask::receiveRrcMessage(int cellId, ASN_RRC_BCCH_BCH_Message *msg)
{
    if (msg->message.present == ASN_RRC_BCCH_BCH_MessageType_PR_mib)
        receiveMib(cellId, *msg->message.choice.mib);
}

void UeRrcTask::receiveRrcMessage(int cellId, ASN_RRC_BCCH_DL_SCH_Message *msg)
{
    if (msg->message.present != ASN_RRC_BCCH_DL_SCH_MessageType_PR_c1)
        return;

    auto &c1 = msg->message.choice.c1;
    switch (c1->present)
    {
    case ASN_RRC_BCCH_DL_SCH_MessageType__c1_PR_systemInformationBlockType1:
        receiveSib1(cellId, *c1->choice.systemInformationBlockType1);
        break;
    default:
        break;
    }
}

void UeRrcTask::receiveRrcMessage(int cellId, ASN_RRC_DL_CCCH_Message *msg)
{
    if (msg->message.present != ASN_RRC_DL_CCCH_MessageType_PR_c1)
        return;

    auto &c1 = msg->message.choice.c1;
    switch (c1->present)
    {
    case ASN_RRC_DL_CCCH_MessageType__c1_PR_rrcReject:
        receiveRrcReject(cellId, *c1->choice.rrcReject);
        break;
    case ASN_RRC_DL_CCCH_MessageType__c1_PR_rrcSetup:
        receiveRrcSetup(cellId, *c1->choice.rrcSetup);
        break;
    default:
        break;
    }
}

void UeRrcTask::receiveRrcMessage(ASN_RRC_DL_DCCH_Message *msg)
{
    if (msg->message.present != ASN_RRC_DL_DCCH_MessageType_PR_c1)
        return;

    auto &c1 = msg->message.choice.c1;
    switch (c1->present)
    {
    case ASN_RRC_DL_DCCH_MessageType__c1_PR_dlInformationTransfer:
        receiveDownlinkInformationTransfer(*c1->choice.dlInformationTransfer);
        break;
    case ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcRelease:
        receiveRrcRelease(*c1->choice.rrcRelease);
        break;
    case ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration:
        receiveRrcReconfiguration(*c1->choice.rrcReconfiguration);
        break;
    default:
        break;
    }
}

void UeRrcTask::receiveRrcMessage(ASN_RRC_PCCH_Message *msg)
{
    if (msg->message.present != ASN_RRC_PCCH_MessageType_PR_c1)
        return;

    auto &c1 = msg->message.choice.c1;
    switch (c1->present)
    {
    case ASN_RRC_PCCH_MessageType__c1_PR_paging:
        receivePaging(*c1->choice.paging);
        break;
    default:
        break;
    }
}

} // namespace nr::ue
