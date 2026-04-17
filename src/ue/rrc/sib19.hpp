//
// SIB19 (SystemInformationBlockType19-r17) — NTN Configuration
//
// Implements the UE-side data structures for 3GPP TS 38.331 §6.3.1 SIB19-r17,
// which carries Non-Terrestrial Network (NTN) satellite configuration.
//
// SIB19 provides the UE with:
//   - Satellite ephemeris (position/velocity state vectors or Keplerian elements)
//   - Epoch time reference for position extrapolation
//   - Timing advance and drift information
//   - Cell-specific offsets for Doppler/delay compensation
//   - Distance threshold for CHO event D1 triggering
//   - Synchronisation validity duration
//
// Since UERANSIM's ASN.1 library is Rel-15-based and does not include native
// SIB19 types, these structures are defined at the simulation level and
// populated via a custom binary protocol (DL_SIB19 channel), mirroring the
// approach used for CHO configuration (DL_CHO).
//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <utils/json.hpp>

namespace nr::ue
{

/* ------------------------------------------------------------------ */
/*  Ephemeris formats per TS 38.331 EphemerisInfo-r17                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Identifies whether the satellite ephemeris is provided as
 *  position/velocity state vectors (ECEF) or Keplerian orbital parameters.
 */
enum class EEphemerisType
{
    POSITION_VELOCITY,   ///< X,Y,Z + VX,VY,VZ state vectors
    ORBITAL_PARAMETERS,  ///< Keplerian elements
};

/**
 * @brief Position/Velocity state vectors in ECEF (Earth-Centered Earth-Fixed).
 *
 * Per TS 38.331:
 *   positionX/Y/Z : INTEGER (-536870912..536870911), unit = meters
 *   velocityVX/VY/VZ : INTEGER (-131072..131071), unit = 0.0625 m/s
 *
 * We store the decoded (scaled) values as doubles for direct use in
 * position extrapolation and distance computation.
 */
struct SatPositionVelocity
{
    double positionX{};    ///< meters (ECEF X)
    double positionY{};    ///< meters (ECEF Y)
    double positionZ{};    ///< meters (ECEF Z)
    double velocityVX{};   ///< m/s   (ECEF VX, already scaled from 0.0625 m/s units)
    double velocityVY{};   ///< m/s   (ECEF VY)
    double velocityVZ{};   ///< m/s   (ECEF VZ)
};

/**
 * @brief Keplerian orbital elements per TS 38.331 orbitalParameters-r17.
 *
 * These define a satellite orbit analytically and are primarily used for
 * LEO satellites with predictable paths.
 *
 * Stored as raw integer values from the ASN.1 encoding; scaling is left
 * to the consumer (e.g. semi-major axis is in meters, eccentricity is
 * scaled by 2^-20, etc.).
 */
struct SatOrbitalParameters
{
    int64_t semiMajorAxis{};      ///< meters (0..8589934591)
    int32_t eccentricity{};       ///< scaled 2^-20 (0..1048575)
    int32_t periapsis{};          ///< scaled 2^-28 × 2π rad (0..268435455)
    int32_t longitude{};          ///< scaled 2^-28 × 2π rad
    int32_t inclination{};        ///< scaled 2^-28 × 2π rad
    int32_t meanAnomaly{};        ///< scaled 2^-28 × 2π rad
};

/**
 * @brief EphemerisInfo-r17 CHOICE — either state vectors or Keplerian elements.
 */
struct EphemerisInfo
{
    EEphemerisType type{EEphemerisType::POSITION_VELOCITY};
    SatPositionVelocity posVel{};
    SatOrbitalParameters orbital{};
};

/* ------------------------------------------------------------------ */
/*  Timing Advance information                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Common Timing Advance information from ntn-Config-r17.
 *
 *   ta-Common-r17 : INTEGER (0..66485757), unit = T_c (basic NR time unit)
 *   ta-CommonDrift-r17 : INTEGER (-257303..257303), unit = T_c/s
 *   ta-CommonDriftVariation-r17 : INTEGER (0..28949), unit = T_c/s² (optional)
 */
struct TaInfo
{
    int64_t taCommon{};                               ///< Common TA in T_c units
    int32_t taCommonDrift{};                          ///< TA drift rate (T_c/s)
    std::optional<int32_t> taCommonDriftVariation{};  ///< TA drift variation (T_c/s²)
};

/* ------------------------------------------------------------------ */
/*  Cell-selection NTN parameters                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Cell-selection parameters specific to NTN from SIB19.
 *
 * Per TS 38.304, the UE uses these to determine if an NTN cell is
 * suitable for camping.
 */
struct CellSelectionNtn
{
    std::optional<int32_t> qRxLevMinOffset{};    ///< dB offset for NTN cell selection
    std::optional<int32_t> qQualMinOffset{};     ///< dB offset for NTN quality
};

/* ------------------------------------------------------------------ */
/*  NTN Polarisation type                                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Polarisation type for the NTN cell's antenna.
 */
enum class ENtnPolarization
{
    RHCP,    ///< Right-Hand Circular Polarisation
    LHCP,    ///< Left-Hand Circular Polarisation
    LINEAR,  ///< Linear polarisation
};

/* ------------------------------------------------------------------ */
/*  UL sync validity duration enum                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Enumerated values for ntn-UlSyncValidityDuration-r17.
 *
 * Per TS 38.331, indicates how long the current synchronisation data
 * (ephemeris + TA) is valid before the UE must re-read SIB19.
 * Values in seconds: {5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60,
 *                     120, 180, 240, 900}.
 */
enum class ENtnUlSyncValidityDuration
{
    S5 = 5,
    S10 = 10,
    S15 = 15,
    S20 = 20,
    S25 = 25,
    S30 = 30,
    S35 = 35,
    S40 = 40,
    S45 = 45,
    S50 = 50,
    S55 = 55,
    S60 = 60,
    S120 = 120,
    S180 = 180,
    S240 = 240,
    S900 = 900,
};

/* ------------------------------------------------------------------ */
/*  ntn-Config-r17 container                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief The top-level ntn-Config-r17 container from SIB19-r17.
 *
 * Wraps the ephemeris data, epoch time, timing advance information,
 * and scheduling offset.
 *
 * epochTime-r17 is represented as a 64-bit integer in units of 10 ms
 * steps. Note that in 3gpp this is related to the System Frame Number (SFN)
 * and subfame, but for the simulation we are simply using the Unix epoch as a 10ms value.
 *
 * kOffset-r17 is a scheduling offset in milliseconds that compensates
 * for the satellite round-trip delay.
 */
struct NtnConfig
{
    /* Ephemeris */
    EphemerisInfo ephemerisInfo{};

    /* Time reference */
    int64_t epochTime{};               ///< 10-ms steps

    /* Scheduling offset */
    int32_t kOffset{};                 ///< ms — compensates round-trip delay

    /* Timing Advance */
    std::optional<TaInfo> taInfo{};

    /* UL synchronisation validity */
    std::optional<ENtnUlSyncValidityDuration> ulSyncValidityDuration{};
};

/* ------------------------------------------------------------------ */
/*  SIB19 top-level structure                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Complete SIB19-r17 (SystemInformationBlockType19-r17) data.
 *
 * This is the primary structure stored per cell in UeCellDesc.
 * It is populated by the receiveSib19() handler when the UE receives
 * a DL_SIB19 PDU from the gNB.
 *
 * Key fields for CHO integration:
 *   - ntnConfig.ephemerisInfo: satellite position/velocity for extrapolation
 *   - ntnConfig.epochTime: "T_epoch" for the math X_now = X + VX × Δt
 *   - ntnConfig.ulSyncValidityDuration: expiration of the ephemeris data
 *
 * The receivedTime field records the UE's local clock (ms since epoch)
 * at which this SIB19 was received, allowing the UE to determine how
 * stale the data is.
 */
struct Sib19Info
{
    bool hasSib19{false};

    /* ntn-Config-r17 */
    NtnConfig ntnConfig{};

    /* Cell-specific K_offset (additional to the one in ntn-Config) */
    std::optional<int32_t> cellSpecificKoffset{};

    /* Antenna polarisation */
    std::optional<ENtnPolarization> ntnPolarization{};

    /* Cell-selection NTN parameters */
    std::optional<CellSelectionNtn> cellSelectionNtn{};

    /* Timing Advance drift rate (T_c/s) — top-level field for quick access */
    std::optional<int32_t> taDrift{};

    /* UE-local reception timestamp (ms since UE start) */
    int64_t receivedTime{};

    /* All decoded SIB19 records in the last received payload, keyed by PCI. */
    struct PciEntry
    {
        NtnConfig ntnConfig{};
        std::optional<int32_t> cellSpecificKoffset{};
        std::optional<ENtnPolarization> ntnPolarization{};
        std::optional<int32_t> taDrift{};
        int64_t receivedTime{};
    };

    std::unordered_map<int, PciEntry> entriesByPci{};
};

/* ------------------------------------------------------------------ */
/*  JSON serialisation                                                 */
/* ------------------------------------------------------------------ */

Json ToJson(EEphemerisType v);
Json ToJson(const SatPositionVelocity &v);
Json ToJson(const SatOrbitalParameters &v);
Json ToJson(const EphemerisInfo &v);
Json ToJson(const TaInfo &v);
Json ToJson(const NtnConfig &v);
Json ToJson(ENtnPolarization v);
Json ToJson(const Sib19Info::PciEntry &v);
Json ToJson(const Sib19Info &v);

/* ------------------------------------------------------------------ */
/*  Satellite position extrapolation                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Extrapolate the satellite's current ECEF position from the
 *  epoch state vectors and elapsed time.
 *
 *  X_now = X_epoch + VX × Δt
 *  Y_now = Y_epoch + VY × Δt
 *  Z_now = Z_epoch + VZ × Δt
 *
 * @param posVel   The position/velocity at epoch time.
 * @param dtSec    Elapsed time since epoch, in seconds.
 * @param[out] x   Extrapolated X position (meters).
 * @param[out] y   Extrapolated Y position (meters).
 * @param[out] z   Extrapolated Z position (meters).
 */
inline void extrapolateSatelliteEcefPosition(const SatPositionVelocity &posVel,
                                             double dtSec,
                                             double &x, double &y, double &z)
{
    x = posVel.positionX + posVel.velocityVX * dtSec;
    y = posVel.positionY + posVel.velocityVY * dtSec;
    z = posVel.positionZ + posVel.velocityVZ * dtSec;
}

/**
 * @brief Solve Kepler's equation  M = E - e·sin(E)  for eccentric anomaly E.
 *
 * Uses Newton-Raphson iteration; converges in 3-5 steps for typical LEO eccentricities.
 *
 * @param M  Mean anomaly (radians).
 * @param e  Eccentricity (0 ≤ e < 1).
 * @returns  Eccentric anomaly E (radians).
 */
inline double solveKepler(double M, double e)
{
    double E = M;
    for (int i = 0; i < 20; ++i)
    {
        double dE = (M - E + e * std::sin(E)) / (1.0 - e * std::cos(E));
        E += dE;
        if (std::fabs(dE) < 1e-12)
            break;
    }
    return E;
}

/**
 * @brief Propagate satellite ECEF position from Keplerian orbital elements.
 *
 * Decodes the raw SIB19 fixed-point values, propagates the mean anomaly to
 * @p tMs, solves for the eccentric/true anomaly, rotates to ECI, then
 * converts to ECEF using GMST derived from the Unix epoch.
 *
 * Encoding convention (matching SIB19 binary protocol):
 *   semiMajorAxis : raw meters  (int64)
 *   eccentricity  : raw / 2^20  (dimensionless)
 *   periapsis, longitude, inclination, meanAnomaly : raw × (2π / 2^28)  (radians)
 *
 * @param orb       Raw orbital parameters from SIB19.
 * @param epochMs   Epoch timestamp in Unix milliseconds (epochTime10ms × 10).
 * @param tMs       Target Unix timestamp in milliseconds.
 * @param[out] x    ECEF X position (meters).
 * @param[out] y    ECEF Y position (meters).
 * @param[out] z    ECEF Z position (meters).
 */
inline void propagateKeplerian(const SatOrbitalParameters &orb,
                                int64_t epochMs, int64_t tMs,
                                double &x, double &y, double &z)
{
    constexpr double TWO_PI  = 2.0 * M_PI;
    constexpr double GM      = 3.986004418e14;  // m³/s²
    constexpr double OMEGA_E = 7.2921150e-5;    // Earth rotation rate (rad/s)
    constexpr double SCALE   = TWO_PI / (1LL << 28);

    // Decode fixed-point integers → physical values
    const double a     = static_cast<double>(orb.semiMajorAxis);
    const double e     = static_cast<double>(orb.eccentricity) / static_cast<double>(1LL << 20);
    const double omega = orb.periapsis   * SCALE;   // argument of perigee (rad)
    const double Omega = orb.longitude   * SCALE;   // RAAN (rad)
    const double inc   = orb.inclination * SCALE;   // inclination (rad)
    const double M0    = orb.meanAnomaly * SCALE;   // mean anomaly at epoch (rad)

    // Mean motion (rad/s)
    const double n = std::sqrt(GM / (a * a * a));

    // Propagate mean anomaly to target time
    const double dtSec = static_cast<double>(tMs - epochMs) / 1000.0;
    double M = std::fmod(M0 + n * dtSec, TWO_PI);
    if (M < 0.0) M += TWO_PI;

    // Eccentric anomaly via Newton-Raphson
    const double E = solveKepler(M, e);

    // True anomaly
    const double nu = 2.0 * std::atan2(
        std::sqrt(1.0 + e) * std::sin(E / 2.0),
        std::sqrt(1.0 - e) * std::cos(E / 2.0));

    // Orbital radius and position in perifocal frame
    const double r    = a * (1.0 - e * std::cos(E));
    const double xOrb = r * std::cos(nu);
    const double yOrb = r * std::sin(nu);

    // Rotation matrix: perifocal → ECI
    const double cO = std::cos(Omega), sO = std::sin(Omega);
    const double co = std::cos(omega), so = std::sin(omega);
    const double ci = std::cos(inc),   si = std::sin(inc);

    const double xEci = (cO*co - sO*so*ci) * xOrb + (-cO*so - sO*co*ci) * yOrb;
    const double yEci = (sO*co + cO*so*ci) * xOrb + (-sO*so + cO*co*ci) * yOrb;
    const double zEci = (so*si)            * xOrb + ( co*si)             * yOrb;

    // ECI → ECEF: rotate by GMST at target time
    // GMST (radians) from Unix epoch using the standard IAU formula
    const double tSec = static_cast<double>(tMs) / 1000.0;
    const double jd   = 2440587.5 + tSec / 86400.0;         // Julian date
    const double d    = jd - 2451545.0;                      // days since J2000
    double gmst = std::fmod((280.46061837 + 360.98564736629 * d) * (M_PI / 180.0), TWO_PI);
    if (gmst < 0.0) gmst += TWO_PI;

    x =  xEci * std::cos(gmst) + yEci * std::sin(gmst);
    y = -xEci * std::sin(gmst) + yEci * std::cos(gmst);
    z =  zEci;

    (void)OMEGA_E;  // retained for reference; GMST formula subsumes it
}

/**
 * @brief Check whether the SIB19 ephemeris data is still valid based on
 *  the ulSyncValidityDuration and the time elapsed since reception.
 *
 * @param sib19         The SIB19 info to check.
 * @param currentTimeMs The current UE-local time in ms.
 * @return true if the data is still within the validity window (or no
 *         validity duration was specified — assumed always valid).
 */
inline bool isSib19EphemerisValid(const Sib19Info &sib19, int64_t currentTimeMs)
{
    if (!sib19.hasSib19)
        return false;

    if (!sib19.ntnConfig.ulSyncValidityDuration.has_value())
        return true;  // no expiration specified — always valid

    int64_t validityMs = static_cast<int64_t>(
        static_cast<int>(sib19.ntnConfig.ulSyncValidityDuration.value())) * 1000;

    return (currentTimeMs - sib19.receivedTime) < validityMs;
}

} // namespace nr::ue
