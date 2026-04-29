//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <gnb/ngap/task.hpp>

#include <lib/sat/sat_time.hpp>
#include <lib/sat/sat_state.hpp>
#include <lib/sat/sat_calc.hpp>

#include <lib/asn/rrc.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <utils/common.hpp>
#include <utils/common_types.hpp>

#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#include <libsgp4/DateTime.h>
#include <libsgp4/Eci.h>
#include <libsgp4/SGP4.h>
#include <libsgp4/Tle.h>

#include <asn/rrc/ASN_RRC_MIB.h>
#include <asn/rrc/ASN_RRC_PLMN-IdentityInfo.h>
#include <asn/rrc/ASN_RRC_PLMN-IdentityInfoList.h>
#include <asn/rrc/ASN_RRC_SIB1.h>
#include <asn/rrc/ASN_RRC_UAC-BarringInfoSet.h>
#include <asn/rrc/ASN_RRC_UAC-BarringInfoSetIndex.h>
#include <asn/rrc/ASN_RRC_UAC-BarringPerCat.h>
#include <asn/rrc/ASN_RRC_UAC-BarringPerCatList.h>

#include <algorithm>

using nr::sat::EcefPosition;

namespace nr::gnb
{

static constexpr uint8_t SIB19_PDU_VERSION = 2;
static constexpr uint8_t SIB19_PDU_EPH_TYPE_POS_VEL = 0;
static constexpr uint8_t SIB19_PDU_EPH_TYPE_ORBITAL  = 1;
static constexpr uint8_t SIB19_PDU_EPH_TYPE_TLE      = 2;
static constexpr size_t SIB19_HEADER_SIZE = 8;
static constexpr size_t SIB19_ENTRY_SIZE = 96;      // PosVel / Orbital entry size
static constexpr size_t SIB19_TLE_ENTRY_SIZE = 216; // TLE entry size
static constexpr uint32_t SIB19_MAX_ENTRIES = 256;

// Offset of the first common field within a standard (PosVel/Orbital) entry.
static constexpr size_t SIB19_STD_COMMON_OFF = 52;
// Offset of the first common field within a TLE entry.
// Layout: 4 (PCI) + 25 (name) + 70 (line1) + 70 (line2) + 3 (pad) = 172
static constexpr size_t SIB19_TLE_COMMON_OFF = 172;

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

/**
 * @brief Sends a SIB19 message conataining satellite ephemeris data.
 * Relies on the TLE store to provide the current TLE for the serving satellite 
 * (identified by own PCI) and nearby satellites.  The top 7 nearby satellites are determined 
 * during the periodic satellite position update and stored in m_sib19RangeCache.
 * 
 */
void GnbRrcTask::triggerSib19Broadcast()
{
    // config must enable SIB19
    if (!m_config->ntn.sib19.sib19On)
        return;

    int ownPci = cons::getPciFromNci(m_config->nci);

    // config sets what type of ephemeris data to include in SIB19, either pos/vel or orbital elements
    const auto ephType = m_config->ntn.sib19.ephType;

    // Own TLE must be loaded — it is always included as the serving satellite.
    auto ownTleOpt = m_base->satStates->getTle(ownPci);
    if (!ownTleOpt.has_value())
    {
        m_logger->debug("SIB19: own TLE not loaded (pci=%d), skipping broadcast", ownPci);
        return;
    }

    // Build the ordered PCI list: own satellite first, then SIB19-range neighbors.
    std::vector<int> pcis;
    pcis.push_back(ownPci);
    for (int pci : m_sib19RangeCache)
    {
        if (pci != ownPci)
            pcis.push_back(pci);
    }

    // Coherent time base for all TLE operations.
    const int64_t nowMs = utils::CurrentTimeMillis();
    const int64_t satNowMs = m_base->satTime->CurrentSatTimeMillis();
    //const libsgp4::DateTime now = nr::rrc::common::UnixMillisToDateTime(satNowMs);

    static constexpr double TWO_PI = 2.0 * M_PI;
    static constexpr double GM     = 3.986004418e14;  // m³/s²

    // Encode a physical angle (radians) to the fixed-point SIB19 format.
    // Scale: 1 LSB = 2π / 2^28 rad.
    auto encAngle = [](double rad) -> int32_t {
        constexpr double TWO_PI = 2.0 * M_PI;
        double norm = std::fmod(rad, TWO_PI);
        if (norm < 0.0) norm += TWO_PI;
        return static_cast<int32_t>(std::round(norm / TWO_PI * static_cast<double>(1 << 28)));
    };

    std::vector<nr::sat::EphEntry> entries;
    entries.reserve(pcis.size());

    // Pre-compute t0+1 s only when needed for velocity (pos/vel path).
    const int64_t satNowPlus1 = satNowMs + 1000;

    // loop through each PCI, calculating ephemeris data based on the TLE and the configured ephType
    for (int pci : pcis)
    {
        auto tle = m_base->satStates->getTle(pci);
        if (!tle.has_value())
        {
            m_logger->debug("SIB19: TLE not found for pci=%d, skipping", pci);
            continue;
        }

        try
        {
            nr::sat::EphEntry e{};
            e.pci = pci;

            if (ephType == ESib19EphemerisMode::PosVel)
            {
                // SGP4 propagate to now + numerical velocity
                auto propagator = m_base->satStates->getSgp4(pci);
                EcefPosition p0 = propagator->FindPositionEcef(satNowMs);
                EcefPosition p1 = propagator->FindPositionEcef(satNowPlus1);

                e.x = p0.x; e.y = p0.y; e.z = p0.z;
                e.vx = p1.x - p0.x;  // dt = 1 s → difference is m/s
                e.vy = p1.y - p0.y;
                e.vz = p1.z - p0.z;
            }
            else if (ephType == ESib19EphemerisMode::Orbital)
            {
                // Extract Keplerian elements from SGP4 state
                auto propagator = m_base->satStates->getSgp4(pci);
                auto k = propagator->GetKeplerianElements(satNowMs);

                e.semiMajorAxis = k.semiMajorAxis;
                e.eccentricity  = k.eccentricity;
                e.periapsis     = k.periapsis;
                e.longitude     = k.longitude;
                e.inclination   = k.inclination;
                e.meanAnomaly   = k.meanAnomaly;
            }
            else  // ESib19EphemerisMode::Tle
            {
                // Copy raw TLE strings directly — no propagation needed
                const auto &t = tle.value();
                std::strncpy(e.tleName,  t.name.c_str(),  sizeof(e.tleName)  - 1);
                std::strncpy(e.tleLine1, t.line1.c_str(), sizeof(e.tleLine1) - 1);
                std::strncpy(e.tleLine2, t.line2.c_str(), sizeof(e.tleLine2) - 1);
            }

            entries.push_back(e);
        }
        catch (const std::exception &ex)
        {
            m_logger->debug("SIB19: TLE processing failed for pci=%d (%s)", pci, ex.what());
        }
    }

    if (entries.empty())
    {
        m_logger->warn("SIB19: no satellite entries to broadcast");
        return;
    }

    // -----------------------------------------------------------------------
    // Serialise payload: version=2, ephType from config.
    //
    // PosVel / Orbital entry (96 bytes): 4 (PCI) + 48 (ephemeris) + 44 (common)
    // TLE entry          (216 bytes): 4 (PCI) + 25 (name) + 70 (line1) +
    //                                 70 (line2) + 3 (pad) + 44 (common)
    // -----------------------------------------------------------------------
    const bool isTle = (ephType == ESib19EphemerisMode::Tle);
    const size_t entrySize = isTle ? SIB19_TLE_ENTRY_SIZE : SIB19_ENTRY_SIZE;
    const size_t commonOff = isTle ? SIB19_TLE_COMMON_OFF : SIB19_STD_COMMON_OFF;

    uint32_t entryCount = static_cast<uint32_t>(entries.size());
    std::vector<uint8_t> payload(SIB19_HEADER_SIZE + entryCount * entrySize, 0);
    payload[0] = SIB19_PDU_VERSION;
    payload[1] = static_cast<uint8_t>(ephType);
    WriteLe(payload, 4, entryCount);

    const auto &cfg = m_config->ntn.sib19;
    const int64_t epoch10ms   = nowMs / 10;
    const int32_t taDriftVar  = cfg.taCommonDriftVariation.has_value() ? *cfg.taCommonDriftVariation : -1;
    const int32_t ulSync      = cfg.ulSyncValidityDuration.has_value()  ? *cfg.ulSyncValidityDuration  : -1;
    const int32_t cellKOffset = cfg.cellSpecificKoffset.has_value()      ? *cfg.cellSpecificKoffset      : -1;
    const int32_t polarization = cfg.polarization.has_value()            ? *cfg.polarization             : -1;
    const int32_t taDriftTop  = cfg.taDrift.has_value()                  ? *cfg.taDrift                  : INT32_MIN;

    for (uint32_t i = 0; i < entryCount; i++)
    {
        const auto &e = entries[i];
        const size_t base = SIB19_HEADER_SIZE + static_cast<size_t>(i) * entrySize;

        WriteLe(payload, base, static_cast<int32_t>(e.pci));

        if (ephType == ESib19EphemerisMode::PosVel)
        {
            WriteLe(payload, base + 4,  e.x);
            WriteLe(payload, base + 12, e.y);
            WriteLe(payload, base + 20, e.z);
            WriteLe(payload, base + 28, e.vx);
            WriteLe(payload, base + 36, e.vy);
            WriteLe(payload, base + 44, e.vz);
        }
        else if (ephType == ESib19EphemerisMode::Orbital)
        {
            WriteLe(payload, base + 4,  e.semiMajorAxis);
            WriteLe(payload, base + 12, e.eccentricity);
            WriteLe(payload, base + 16, e.periapsis);
            WriteLe(payload, base + 20, e.longitude);
            WriteLe(payload, base + 24, e.inclination);
            WriteLe(payload, base + 28, e.meanAnomaly);
            // base+32..base+51 reserved — zeroed by vector initialisation
        }
        else  // Tle
        {
            // name at +4 (25 bytes), line1 at +29 (70 bytes), line2 at +99 (70 bytes)
            // bytes 169..171 are padding — already zeroed by vector initialisation
            std::memcpy(payload.data() + base + 4,  e.tleName,  sizeof(e.tleName));
            std::memcpy(payload.data() + base + 29, e.tleLine1, sizeof(e.tleLine1));
            std::memcpy(payload.data() + base + 99, e.tleLine2, sizeof(e.tleLine2));
        }

        // Common fields at offset `commonOff` within each entry (layout is identical
        // for all modes; only the starting offset differs between TLE and others).
        WriteLe(payload, base + commonOff,      epoch10ms);
        WriteLe(payload, base + commonOff + 8,  cfg.kOffset);
        WriteLe(payload, base + commonOff + 12, cfg.taCommon);
        WriteLe(payload, base + commonOff + 20, cfg.taCommonDrift);
        WriteLe(payload, base + commonOff + 24, taDriftVar);
        WriteLe(payload, base + commonOff + 28, ulSync);
        WriteLe(payload, base + commonOff + 32, cellKOffset);
        WriteLe(payload, base + commonOff + 36, polarization);
        WriteLe(payload, base + commonOff + 40, taDriftTop);
    }

    m_logger->debug("SIB19: broadcasting %u entries ephType=%d (own pci=%d + %zu neighbors)",
                    entryCount, static_cast<int>(ephType), ownPci, entries.size() - 1);

    sendRrcMessage(rrc::RrcChannel::DL_SIB19, OctetString(std::move(payload)));
}

} // namespace nr::gnb