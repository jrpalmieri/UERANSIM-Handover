//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <gnb/ngap/task.hpp>
#include <lib/asn/rrc.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <utils/common.hpp>

#include <climits>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <asn/rrc/ASN_RRC_MIB.h>
#include <asn/rrc/ASN_RRC_PLMN-IdentityInfo.h>
#include <asn/rrc/ASN_RRC_PLMN-IdentityInfoList.h>
#include <asn/rrc/ASN_RRC_SIB1.h>
#include <asn/rrc/ASN_RRC_UAC-BarringInfoSet.h>
#include <asn/rrc/ASN_RRC_UAC-BarringInfoSetIndex.h>
#include <asn/rrc/ASN_RRC_UAC-BarringPerCat.h>
#include <asn/rrc/ASN_RRC_UAC-BarringPerCatList.h>

#include <algorithm>

namespace nr::gnb
{

static constexpr uint8_t SIB19_PDU_VERSION = 2;
static constexpr uint8_t SIB19_PDU_EPH_TYPE_POS_VEL = 0;
static constexpr size_t SIB19_HEADER_SIZE = 8;
static constexpr size_t SIB19_ENTRY_SIZE = 96;
static constexpr uint32_t SIB19_MAX_ENTRIES = 256;

template <typename T>
static void WriteLe(std::vector<uint8_t> &buffer, size_t offset, const T &value)
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    std::memcpy(buffer.data() + offset, &value, sizeof(T));
}

static ASN_RRC_BCCH_BCH_Message *ConstructMibMessage(bool barred, bool intraFreqReselectAllowed)
{
    auto *pdu = asn::New<ASN_RRC_BCCH_BCH_Message>();
    pdu->message.present = ASN_RRC_BCCH_BCH_MessageType_PR_mib;
    pdu->message.choice.mib = asn::New<ASN_RRC_MIB>();

    auto &mib = *pdu->message.choice.mib;

    asn::SetBitStringInt<6>(0, mib.systemFrameNumber);
    mib.subCarrierSpacingCommon = ASN_RRC_MIB__subCarrierSpacingCommon_scs15or60;
    mib.ssb_SubcarrierOffset = 0;
    mib.dmrs_TypeA_Position = ASN_RRC_MIB__dmrs_TypeA_Position_pos2;
    mib.cellBarred = barred ? ASN_RRC_MIB__cellBarred_barred : ASN_RRC_MIB__cellBarred_notBarred;
    mib.intraFreqReselection = intraFreqReselectAllowed ? ASN_RRC_MIB__intraFreqReselection_allowed
                                                        : ASN_RRC_MIB__intraFreqReselection_notAllowed;
    asn::SetBitStringInt<1>(0, mib.spare);
    mib.pdcch_ConfigSIB1.controlResourceSetZero = 0;
    mib.pdcch_ConfigSIB1.searchSpaceZero = 0;
    return pdu;
}

static ASN_RRC_BCCH_DL_SCH_Message *ConstructSib1Message(bool cellReserved, int tac, int64_t nci, const Plmn &plmn,
                                                         const UacAiBarringSet &aiBarringSet)
{
    auto *pdu = asn::New<ASN_RRC_BCCH_DL_SCH_Message>();
    pdu->message.present = ASN_RRC_BCCH_DL_SCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_BCCH_DL_SCH_MessageType__c1_PR_systemInformationBlockType1;
    pdu->message.choice.c1->choice.systemInformationBlockType1 = asn::New<ASN_RRC_SIB1>();

    auto &sib1 = *pdu->message.choice.c1->choice.systemInformationBlockType1;

    if (cellReserved)
    {
        asn::MakeNew(sib1.cellAccessRelatedInfo.cellReservedForOtherUse);
        *sib1.cellAccessRelatedInfo.cellReservedForOtherUse =
            ASN_RRC_CellAccessRelatedInfo__cellReservedForOtherUse_true;
    }

    auto *plmnInfo = asn::New<ASN_RRC_PLMN_IdentityInfo>();
    plmnInfo->cellReservedForOperatorUse = cellReserved
                                               ? ASN_RRC_PLMN_IdentityInfo__cellReservedForOperatorUse_reserved
                                               : ASN_RRC_PLMN_IdentityInfo__cellReservedForOperatorUse_notReserved;
    asn::MakeNew(plmnInfo->trackingAreaCode);
    asn::SetBitStringInt<24>(tac, *plmnInfo->trackingAreaCode);
    asn::SetBitStringLong<36>(nci, plmnInfo->cellIdentity);
    asn::SequenceAdd(plmnInfo->plmn_IdentityList, asn::rrc::NewPlmnId(plmn));
    asn::SequenceAdd(sib1.cellAccessRelatedInfo.plmn_IdentityList, plmnInfo);

    asn::MakeNew(sib1.uac_BarringInfo);

    auto *info = asn::New<ASN_RRC_UAC_BarringInfoSet>();
    info->uac_BarringFactor = ASN_RRC_UAC_BarringInfoSet__uac_BarringFactor_p50;
    info->uac_BarringTime = ASN_RRC_UAC_BarringInfoSet__uac_BarringTime_s4;

    asn::SetBitStringInt<7>(bits::Consequential8(false, aiBarringSet.ai1, aiBarringSet.ai2, aiBarringSet.ai11,
                                                 aiBarringSet.ai12, aiBarringSet.ai13, aiBarringSet.ai14,
                                                 aiBarringSet.ai15),
                            info->uac_BarringForAccessIdentity);

    asn::SequenceAdd(sib1.uac_BarringInfo->uac_BarringInfoSetList, info);

    asn::MakeNew(sib1.uac_BarringInfo->uac_BarringForCommon);

    for (size_t i = 0; i < 63; i++)
    {
        auto *item = asn::New<ASN_RRC_UAC_BarringPerCat>();
        item->accessCategory = static_cast<decltype(item->accessCategory)>(i + 1);
        item->uac_barringInfoSetIndex = 1;

        asn::SequenceAdd(*sib1.uac_BarringInfo->uac_BarringForCommon, item);
    }

    return pdu;
}

void GnbRrcTask::onBroadcastTimerExpired()
{
    triggerSysInfoBroadcast();
}

void GnbRrcTask::triggerSysInfoBroadcast()
{
    auto *mib = ConstructMibMessage(m_isBarred, m_intraFreqReselectAllowed);
    auto *sib1 = ConstructSib1Message(m_cellReserved, m_config->tac, m_config->nci, m_config->plmn, m_aiBarringSet);

    sendRrcMessage(mib);
    sendRrcMessage(sib1);

    asn::Free(asn_DEF_ASN_RRC_BCCH_BCH_Message, mib);
    asn::Free(asn_DEF_ASN_RRC_BCCH_DL_SCH_Message, sib1);
}

void GnbRrcTask::triggerSib19Broadcast()
{
    if (!m_config->ntn.sib19.sib19On)
        return;

    const int64_t nowMs = utils::CurrentTimeMillis();
    const int64_t thresholdMs = std::max<int64_t>(1, m_config->ntn.sib19.satLocUpdateThresholdMs);

    for (auto it = m_satellitePvByPci.begin(); it != m_satellitePvByPci.end();)
    {
        int64_t ageMs = nowMs - it->second.lastUpdatedMs;
        if (ageMs > thresholdMs)
            it = m_satellitePvByPci.erase(it);
        else
            ++it;
    }

    std::vector<SatellitePositionVelocityEntry> entries{};
    entries.reserve(m_satellitePvByPci.size() + 1);
    for (const auto &item : m_satellitePvByPci)
        entries.push_back(item.second);

    if (entries.empty())
    {
        if (!m_truePositionVelocity.isValid)
            return;

        SatellitePositionVelocityEntry fallback{};
        fallback.pci = m_config->getCellId();
        fallback.x = m_truePositionVelocity.x;
        fallback.y = m_truePositionVelocity.y;
        fallback.z = m_truePositionVelocity.z;
        fallback.vx = m_truePositionVelocity.vx;
        fallback.vy = m_truePositionVelocity.vy;
        fallback.vz = m_truePositionVelocity.vz;
        fallback.epochMs = m_truePositionVelocity.epochMs;
        fallback.lastUpdatedMs = nowMs;
        entries.push_back(fallback);
    }

    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.pci < b.pci;
    });

    if (entries.size() > SIB19_MAX_ENTRIES)
        entries.resize(SIB19_MAX_ENTRIES);

    uint32_t entryCount = static_cast<uint32_t>(entries.size());
    std::vector<uint8_t> payload(SIB19_HEADER_SIZE + entryCount * SIB19_ENTRY_SIZE, 0);
    payload[0] = SIB19_PDU_VERSION;
    payload[1] = SIB19_PDU_EPH_TYPE_POS_VEL;
    WriteLe(payload, 4, entryCount);

    const auto &cfg = m_config->ntn.sib19;
    const int64_t epoch10ms = nowMs / 10;
    const int32_t taDriftVar = cfg.taCommonDriftVariation.has_value() ? *cfg.taCommonDriftVariation : -1;
    const int32_t ulSync = cfg.ulSyncValidityDuration.has_value() ? *cfg.ulSyncValidityDuration : -1;
    const int32_t cellKOffset = cfg.cellSpecificKoffset.has_value() ? *cfg.cellSpecificKoffset : -1;
    const int32_t polarization = cfg.polarization.has_value() ? *cfg.polarization : -1;
    const int32_t taDriftTop = cfg.taDrift.has_value() ? *cfg.taDrift : INT32_MIN;

    for (uint32_t i = 0; i < entryCount; i++)
    {
        const auto &entry = entries[i];
        const size_t base = SIB19_HEADER_SIZE + static_cast<size_t>(i) * SIB19_ENTRY_SIZE;

        double dtSec = static_cast<double>(nowMs - entry.epochMs) / 1000.0;
        double xNow = entry.x + entry.vx * dtSec;
        double yNow = entry.y + entry.vy * dtSec;
        double zNow = entry.z + entry.vz * dtSec;

        WriteLe(payload, base, static_cast<int32_t>(entry.pci));
        WriteLe(payload, base + 4, xNow);
        WriteLe(payload, base + 12, yNow);
        WriteLe(payload, base + 20, zNow);
        WriteLe(payload, base + 28, entry.vx);
        WriteLe(payload, base + 36, entry.vy);
        WriteLe(payload, base + 44, entry.vz);
        WriteLe(payload, base + 52, epoch10ms);
        WriteLe(payload, base + 60, cfg.kOffset);
        WriteLe(payload, base + 64, cfg.taCommon);
        WriteLe(payload, base + 72, cfg.taCommonDrift);
        WriteLe(payload, base + 76, taDriftVar);
        WriteLe(payload, base + 80, ulSync);
        WriteLe(payload, base + 84, cellKOffset);
        WriteLe(payload, base + 88, polarization);
        WriteLe(payload, base + 92, taDriftTop);
    }

    sendRrcMessage(rrc::RrcChannel::DL_SIB19, OctetString(std::move(payload)));
}

} // namespace nr::gnb