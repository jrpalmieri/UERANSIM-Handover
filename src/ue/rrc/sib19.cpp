//
// SIB19 (SystemInformationBlockType19-r17) — receive, parse, and serialise.
//
// This file implements:
//   - receiveSib19(): parses the custom DL_SIB19 binary PDU and stores
//     the decoded NTN configuration in the per-cell UeCellDesc.
//   - JSON serialisation for all SIB19-related types.
//
// Binary protocol (DL_SIB19), little-endian:
//
//   Offset  Size   Field
//   ------  -----  -------------------------------------------------------
//   0       1      ephemerisType     (0 = position/velocity, 1 = orbital)
//   1       3      reserved
//
//   -- If ephemerisType == 0 (position/velocity): 48 bytes --
//   4       8      positionX         (double, meters ECEF)
//   12      8      positionY         (double, meters ECEF)
//   20      8      positionZ         (double, meters ECEF)
//   28      8      velocityVX        (double, m/s — already scaled)
//   36      8      velocityVY        (double, m/s)
//   44      8      velocityVZ        (double, m/s)
//   [total ephemeris block = 48 bytes, starts at offset 4]
//
//   -- If ephemerisType == 1 (orbital): 48 bytes --
//   4       8      semiMajorAxis     (int64, meters)
//   12      4      eccentricity      (int32, scaled 2^-20)
//   16      4      periapsis         (int32, scaled)
//   20      4      longitude         (int32, scaled)
//   24      4      inclination       (int32, scaled)
//   28      4      meanAnomaly       (int32, scaled)
//   32      20     reserved (pad to 48 bytes)
//   [total ephemeris block = 48 bytes, starts at offset 4]
//
//   -- Common fields (start at offset 52) --
//   52      8      epochTime          (int64, 10-ms steps)
//   60      4      kOffset            (int32, ms)
//   64      8      taCommon           (int64, T_c units)
//   72      4      taCommonDrift      (int32, T_c/s)
//   76      4      taCommonDriftVar   (int32, T_c/s²; -1 = not present)
//   80      4      ulSyncValidity     (int32, seconds; -1 = not present)
//   84      4      cellSpecificKoff   (int32; -1 = not present)
//   88      4      ntnPolarization    (int32; 0=RHCP, 1=LHCP, 2=LINEAR, -1=absent)
//   92      4      taDrift            (int32, T_c/s; INT32_MIN = not present)
//   ------  -----  -------------------------------------------------------
//   Total: 96 bytes (fixed size)
//

#include "task.hpp"
#include "sib19.hpp"

#include <cinttypes>
#include <cstring>
#include <climits>
#include <cstdio>
#include <utils/common.hpp>

namespace nr::ue
{

/* ================================================================== */
/*  Binary protocol constants                                          */
/* ================================================================== */

// Total fixed size of the legacy single-entry DL_SIB19 PDU.
static constexpr size_t SIB19_LEGACY_PDU_SIZE = 96;

// Multi-entry payload version and layout constants.
static constexpr uint8_t SIB19_MULTI_VERSION = 2;
static constexpr size_t SIB19_MULTI_HEADER_SIZE = 8;
static constexpr size_t SIB19_MULTI_ENTRY_SIZE = 96;   // PosVel / Orbital
static constexpr size_t SIB19_TLE_ENTRY_SIZE   = 216;  // TLE

// Common-field start offset within a standard (PosVel/Orbital) entry.
static constexpr size_t COMMON_OFFSET     = 52;
// Common-field start offset within a TLE entry.
// Layout: 4 (PCI) + 25 (name) + 70 (line1) + 70 (line2) + 3 (pad) = 172
static constexpr size_t TLE_COMMON_OFFSET = 172;

/* ================================================================== */
/*  receiveSib19 — parse DL_SIB19 PDU and store in cell descriptor    */
/*                                                                    */
/*  Note - this is a custom implementation of SIB19 messages that does
not use the standard ASN.1 parsing */
/* ================================================================== */
void UeRrcTask::receiveSib19(int cellId, const OctetString &pdu)
{
    const uint8_t *p = pdu.data();
    size_t len = static_cast<size_t>(pdu.length());

    // Lambda helpers for reading little-endian values
    auto readI32 = [&](size_t off) -> int32_t {
        if (off + 4 > len)
            return 0;
        int32_t v;
        std::memcpy(&v, p + off, 4);
        return v;
    };

    auto readI64 = [&](size_t off) -> int64_t {
        if (off + 8 > len)
            return 0;
        int64_t v;
        std::memcpy(&v, p + off, 8);
        return v;
    };

    auto readF64 = [&](size_t off) -> double {
        if (off + 8 > len)
            return 0.0;
        double v;
        std::memcpy(&v, p + off, 8);
        return v;
    };

    auto readU8 = [&](size_t off) -> uint8_t {
        if (off >= len)
            return 0;
        return p[off];
    };

    auto readU32 = [&](size_t off) -> uint32_t {
        if (off + 4 > len)
            return 0;
        uint32_t v;
        std::memcpy(&v, p + off, 4);
        return v;
    };

    // commonOff: byte offset of the first common field within the entry
    //   (COMMON_OFFSET=52 for PosVel/Orbital, TLE_COMMON_OFFSET=172 for TLE)
    auto fillCommonFields = [&](size_t base,
                                size_t commonOff,
                                NtnConfig &ntnConfig,
                                std::optional<int32_t> &cellSpecificKoffset,
                                std::optional<ENtnPolarization> &ntnPolarization,
                                std::optional<int32_t> &taDrift) {
        ntnConfig.epochTime = readI64(base + commonOff);
        ntnConfig.kOffset   = readI32(base + commonOff + 8);

        int64_t taCommon     = readI64(base + commonOff + 12);
        int32_t taCommonDrift = readI32(base + commonOff + 20);
        int32_t taDriftVar   = readI32(base + commonOff + 24);

        TaInfo ta{};
        ta.taCommon = taCommon;
        ta.taCommonDrift = taCommonDrift;
        if (taDriftVar != -1)
            ta.taCommonDriftVariation = taDriftVar;
        ntnConfig.taInfo = ta;

        int32_t syncVal = readI32(base + commonOff + 28);
        if (syncVal > 0)
            ntnConfig.ulSyncValidityDuration = static_cast<ENtnUlSyncValidityDuration>(syncVal);

        int32_t csKoff = readI32(base + commonOff + 32);
        if (csKoff >= 0)
            cellSpecificKoffset = csKoff;

        int32_t pol = readI32(base + commonOff + 36);
        if (pol >= 0 && pol <= 2)
            ntnPolarization = static_cast<ENtnPolarization>(pol);

        int32_t taDriftTop = readI32(base + commonOff + 40);
        if (taDriftTop != INT32_MIN)
            taDrift = taDriftTop;
    };

    int64_t receivedTime = utils::CurrentTimeMillis() - m_startedTime;
    Sib19Info sib19{};

    if (len >= SIB19_MULTI_HEADER_SIZE && readU8(0) == SIB19_MULTI_VERSION)
    {
        uint8_t ephType = readU8(1);
        if (ephType > 2)
        {
            m_logger->err("DL_SIB19: unsupported multi-entry ephemeris type %u", ephType);
            return;
        }

        const bool isTle = (ephType == 2);
        const size_t entrySize = isTle ? SIB19_TLE_ENTRY_SIZE : SIB19_MULTI_ENTRY_SIZE;
        const size_t commonOff = isTle ? TLE_COMMON_OFFSET : COMMON_OFFSET;

        uint32_t entryCount = readU32(4);
        size_t expectedSize = SIB19_MULTI_HEADER_SIZE + static_cast<size_t>(entryCount) * entrySize;
        if (len < expectedSize)
        {
            m_logger->err("DL_SIB19: multi-entry PDU too short (%zu bytes, expected at least %zu)",
                          len, expectedSize);
            return;
        }

        std::vector<nr::sat::SatTleEntry> foundTles;

        for (uint32_t i = 0; i < entryCount; i++)
        {
            size_t base = SIB19_MULTI_HEADER_SIZE + static_cast<size_t>(i) * entrySize;
            int32_t pci = readI32(base);
            if (pci < 0 || pci > 1007)
                continue;

            Sib19Info::PciEntry entry{};

            if (ephType == 0)
            {
                entry.ntnConfig.ephemerisInfo.type = EEphemerisType::POSITION_VELOCITY;
                entry.ntnConfig.ephemerisInfo.posVel.positionX  = readF64(base + 4);
                entry.ntnConfig.ephemerisInfo.posVel.positionY  = readF64(base + 12);
                entry.ntnConfig.ephemerisInfo.posVel.positionZ  = readF64(base + 20);
                entry.ntnConfig.ephemerisInfo.posVel.velocityVX = readF64(base + 28);
                entry.ntnConfig.ephemerisInfo.posVel.velocityVY = readF64(base + 36);
                entry.ntnConfig.ephemerisInfo.posVel.velocityVZ = readF64(base + 44);
            }
            else if (ephType == 1)
            {
                entry.ntnConfig.ephemerisInfo.type = EEphemerisType::ORBITAL_PARAMETERS;
                entry.ntnConfig.ephemerisInfo.orbital.semiMajorAxis = readI64(base + 4);
                entry.ntnConfig.ephemerisInfo.orbital.eccentricity  = readI32(base + 12);
                entry.ntnConfig.ephemerisInfo.orbital.periapsis     = readI32(base + 16);
                entry.ntnConfig.ephemerisInfo.orbital.longitude     = readI32(base + 20);
                entry.ntnConfig.ephemerisInfo.orbital.inclination   = readI32(base + 24);
                entry.ntnConfig.ephemerisInfo.orbital.meanAnomaly   = readI32(base + 28);
            }
            else  // ephType == 2  (TLE)
            {
                // Wire layout within the entry (offsets from entry start):
                //   +4   25 bytes  name  (null-padded, up to 24 chars)
                //   +29  70 bytes  line1 (null-padded, 69-char TLE line 1)
                //   +99  70 bytes  line2 (null-padded, 69-char TLE line 2)
                entry.ntnConfig.ephemerisInfo.type = EEphemerisType::TLE;
                auto &tle = entry.ntnConfig.ephemerisInfo.tle;

                if (base + 4 + 25 <= len)
                    std::memcpy(tle.name, p + base + 4, 25);
                tle.name[24] = '\0';

                if (base + 29 + 70 <= len)
                    std::memcpy(tle.line1, p + base + 29, 70);
                tle.line1[69] = '\0';

                if (base + 99 + 70 <= len)
                    std::memcpy(tle.line2, p + base + 99, 70);
                tle.line2[69] = '\0';
                
                foundTles.emplace_back(nr::sat::SatTleEntry{
                    pci,
                    tle.name,
                    tle.line1,
                    tle.line2,
                    receivedTime
                });
            }

            fillCommonFields(base, commonOff, entry.ntnConfig, entry.cellSpecificKoffset,
                             entry.ntnPolarization, entry.taDrift);
            entry.receivedTime = receivedTime;

            sib19.entriesByPci[pci] = std::move(entry);
        }

        if (sib19.entriesByPci.empty())
        {
            m_logger->err("DL_SIB19: no valid entries in multi-entry payload");
            return;
        }

        sib19.hasSib19 = true;

        auto selected = sib19.entriesByPci.find(cellId);
        if (selected == sib19.entriesByPci.end())
            selected = sib19.entriesByPci.begin();

        sib19.ntnConfig = selected->second.ntnConfig;
        sib19.cellSpecificKoffset = selected->second.cellSpecificKoffset;
        sib19.ntnPolarization = selected->second.ntnPolarization;
        sib19.taDrift = selected->second.taDrift;
        sib19.receivedTime = selected->second.receivedTime;

        auto &desc = m_cellDesc[cellId];
        desc.sib19 = sib19;

        // update the SatStates store with the new ephemeris info for this cell's PCI
        if (m_base->satState && foundTles.size() > 0)
        {
            m_base->satState->upsertAll(foundTles);
        }

        m_logger->info("SIB19 received for cell %d: multi-entry count=%zu selectedPci=%d",
                       cellId,
                       sib19.entriesByPci.size(),
                       selected->first);
        return;
    }

    if (len < SIB19_LEGACY_PDU_SIZE)
    {
        m_logger->err("DL_SIB19: PDU too short (%zu bytes, expected >= %zu)", len, SIB19_LEGACY_PDU_SIZE);
        return;
    }

    // --- Header ---
    uint8_t ephType = readU8(0);

    // --- Ephemeris ---
    if (ephType == 0)
    {
        // Position/Velocity state vectors
        sib19.ntnConfig.ephemerisInfo.type = EEphemerisType::POSITION_VELOCITY;
        auto &pv = sib19.ntnConfig.ephemerisInfo.posVel;
        pv.positionX  = readF64(4);
        pv.positionY  = readF64(12);
        pv.positionZ  = readF64(20);
        pv.velocityVX = readF64(28);
        pv.velocityVY = readF64(36);
        pv.velocityVZ = readF64(44);

        m_logger->info("SIB19 cell %d: ephemeris posVel X=%.1f Y=%.1f Z=%.1f "
                       "VX=%.4f VY=%.4f VZ=%.4f",
                       cellId, pv.positionX, pv.positionY, pv.positionZ,
                       pv.velocityVX, pv.velocityVY, pv.velocityVZ);
    }
    else if (ephType == 1)
    {
        // Orbital parameters (Keplerian)
        sib19.ntnConfig.ephemerisInfo.type = EEphemerisType::ORBITAL_PARAMETERS;
        auto &orb = sib19.ntnConfig.ephemerisInfo.orbital;
        orb.semiMajorAxis = readI64(4);
        orb.eccentricity  = readI32(12);
        orb.periapsis     = readI32(16);
        orb.longitude     = readI32(20);
        orb.inclination   = readI32(24);
        orb.meanAnomaly   = readI32(28);

        m_logger->info("SIB19 cell %d: ephemeris orbital sma=%" PRId64 " ecc=%d",
                       cellId, orb.semiMajorAxis, orb.eccentricity);
    }
    else
    {
        m_logger->err("DL_SIB19: unknown ephemeris type %u", ephType);
        return;
    }

    // --- Common fields ---
    sib19.ntnConfig.epochTime = readI64(COMMON_OFFSET + 0);
    sib19.ntnConfig.kOffset = readI32(COMMON_OFFSET + 8);

    // Timing Advance
    int64_t taCommon = readI64(COMMON_OFFSET + 12);
    int32_t taDrift = readI32(COMMON_OFFSET + 20);
    int32_t taDriftVar = readI32(COMMON_OFFSET + 24);

    TaInfo ta{};
    ta.taCommon      = taCommon;
    ta.taCommonDrift = taDrift;
    if (taDriftVar != -1)
        ta.taCommonDriftVariation = taDriftVar;
    sib19.ntnConfig.taInfo = ta;

    // UL sync validity duration
    int32_t syncVal = readI32(COMMON_OFFSET + 28);
    if (syncVal > 0)
        sib19.ntnConfig.ulSyncValidityDuration =
            static_cast<ENtnUlSyncValidityDuration>(syncVal);

    // Cell-specific K_offset
    int32_t csKoff = readI32(COMMON_OFFSET + 32);
    if (csKoff >= 0)
        sib19.cellSpecificKoffset = csKoff;

    // NTN Polarization
    int32_t pol = readI32(COMMON_OFFSET + 36);
    if (pol >= 0 && pol <= 2)
        sib19.ntnPolarization = static_cast<ENtnPolarization>(pol);

    // TA drift (top-level shortcut)
    int32_t taDriftTop = readI32(COMMON_OFFSET + 40);
    if (taDriftTop != INT32_MIN)
        sib19.taDrift = taDriftTop;

    // Stamp reception time
    sib19.receivedTime = receivedTime;
    sib19.hasSib19 = true;

    Sib19Info::PciEntry legacyEntry{};
    legacyEntry.ntnConfig = sib19.ntnConfig;
    legacyEntry.cellSpecificKoffset = sib19.cellSpecificKoffset;
    legacyEntry.ntnPolarization = sib19.ntnPolarization;
    legacyEntry.taDrift = sib19.taDrift;
    legacyEntry.receivedTime = sib19.receivedTime;
    sib19.entriesByPci[cellId] = std::move(legacyEntry);

    // Store in cell descriptor
    auto &desc = m_cellDesc[cellId];
    desc.sib19 = sib19;


    m_logger->info("SIB19 received for cell %d: legacy format epochTime=%" PRId64
                   " kOffset=%d syncValidity=%s",
                   cellId,
                   sib19.ntnConfig.epochTime,
                   sib19.ntnConfig.kOffset,
                   sib19.ntnConfig.ulSyncValidityDuration.has_value()
                       ? std::to_string(static_cast<int>(
                             sib19.ntnConfig.ulSyncValidityDuration.value())).c_str()
                       : "N/A");
}

/* ================================================================== */
/*  JSON serialisation                                                 */
/* ================================================================== */

Json ToJson(EEphemerisType v)
{
    switch (v)
    {
    case EEphemerisType::POSITION_VELOCITY:  return "positionVelocity";
    case EEphemerisType::ORBITAL_PARAMETERS: return "orbitalParameters";
    case EEphemerisType::TLE:                return "tle";
    default: return "unknown";
    }
}

Json ToJson(const SatPositionVelocity &v)
{
    char buf[64];
    auto fmtD = [&](double d) -> std::string {
        snprintf(buf, sizeof(buf), "%.6f", d);
        return std::string(buf);
    };
    return Json::Obj({
        {"positionX",  fmtD(v.positionX)},
        {"positionY",  fmtD(v.positionY)},
        {"positionZ",  fmtD(v.positionZ)},
        {"velocityVX", fmtD(v.velocityVX)},
        {"velocityVY", fmtD(v.velocityVY)},
        {"velocityVZ", fmtD(v.velocityVZ)},
    });
}

Json ToJson(const SatOrbitalParameters &v)
{
    return Json::Obj({
        {"semiMajorAxis", static_cast<int64_t>(v.semiMajorAxis)},
        {"eccentricity",  v.eccentricity},
        {"periapsis",     v.periapsis},
        {"longitude",     v.longitude},
        {"inclination",   v.inclination},
        {"meanAnomaly",   v.meanAnomaly},
    });
}

Json ToJson(const SatTleEphemeris &v)
{
    return Json::Obj({
        {"name",  std::string(v.name)},
        {"line1", std::string(v.line1)},
        {"line2", std::string(v.line2)},
    });
}

Json ToJson(const EphemerisInfo &v)
{
    Json j = Json::Obj({
        {"type", ToJson(v.type)},
    });

    if (v.type == EEphemerisType::POSITION_VELOCITY)
        j.put("positionVelocity", ToJson(v.posVel));
    else if (v.type == EEphemerisType::ORBITAL_PARAMETERS)
        j.put("orbitalParameters", ToJson(v.orbital));
    else if (v.type == EEphemerisType::TLE)
        j.put("tle", ToJson(v.tle));

    return j;
}

Json ToJson(const TaInfo &v)
{
    auto j = Json::Obj({
        {"taCommon",      static_cast<int64_t>(v.taCommon)},
        {"taCommonDrift", v.taCommonDrift},
    });
    if (v.taCommonDriftVariation.has_value())
        j.put("taCommonDriftVariation", v.taCommonDriftVariation.value());
    return j;
}

Json ToJson(const NtnConfig &v)
{
    auto j = Json::Obj({
        {"ephemerisInfo", ToJson(v.ephemerisInfo)},
        {"epochTime",     static_cast<int64_t>(v.epochTime)},
        {"kOffset",       v.kOffset},
    });

    if (v.taInfo.has_value())
        j.put("taInfo", ToJson(v.taInfo.value()));
    if (v.ulSyncValidityDuration.has_value())
        j.put("ulSyncValidityDuration",
              static_cast<int>(v.ulSyncValidityDuration.value()));
    return j;
}

Json ToJson(ENtnPolarization v)
{
    switch (v)
    {
    case ENtnPolarization::RHCP:   return "RHCP";
    case ENtnPolarization::LHCP:   return "LHCP";
    case ENtnPolarization::LINEAR: return "LINEAR";
    default: return "unknown";
    }
}

Json ToJson(const Sib19Info::PciEntry &v)
{
    auto j = Json::Obj({
        {"ntnConfig", ToJson(v.ntnConfig)},
        {"receivedTime", static_cast<int64_t>(v.receivedTime)},
    });

    if (v.cellSpecificKoffset.has_value())
        j.put("cellSpecificKoffset", v.cellSpecificKoffset.value());
    if (v.ntnPolarization.has_value())
        j.put("ntnPolarization", ToJson(v.ntnPolarization.value()));
    if (v.taDrift.has_value())
        j.put("taDrift", v.taDrift.value());
    return j;
}

Json ToJson(const Sib19Info &v)
{
    if (!v.hasSib19)
        return Json{};

    Json entries = Json::Arr({});
    for (const auto &item : v.entriesByPci)
    {
        entries.push(Json::Obj({
            {"pci", item.first},
            {"entry", ToJson(item.second)},
        }));
    }

    auto j = Json::Obj({
        {"hasSib19",    true},
        {"ntnConfig",   ToJson(v.ntnConfig)},
        {"receivedTime", static_cast<int64_t>(v.receivedTime)},
        {"entriesByPci", std::move(entries)},
    });

    if (v.cellSpecificKoffset.has_value())
        j.put("cellSpecificKoffset", v.cellSpecificKoffset.value());
    if (v.ntnPolarization.has_value())
        j.put("ntnPolarization", ToJson(v.ntnPolarization.value()));
    if (v.taDrift.has_value())
        j.put("taDrift", v.taDrift.value());
    return j;
}

} // namespace nr::ue
