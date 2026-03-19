//
// Satellite state implementation: TLE parsing, simplified
// Keplerian propagation, and thread-safe position storage.
//

#include "satellite_state.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

namespace nr::gnb
{

// --------------- TLE Parsing ---------------

static constexpr double DEG2RAD = M_PI / 180.0;
// Earth gravitational parameter (m^3/s^2)
static constexpr double GM = 3.986004418e14;
// Seconds per day
static constexpr double SEC_PER_DAY = 86400.0;
// Earth rotation rate (rad/s)
static constexpr double OMEGA_E = 7.2921159e-5;
// WGS-84 semi-major axis
static constexpr double WGS84_A = 6378137.0;

bool ParseTle(const std::string &line1,
              const std::string &line2,
              OrbitalElements &out)
{
    // TLE line 1 must be at least 69 chars, line 2 as well
    if (line1.size() < 69 || line2.size() < 69)
        return false;

    try
    {
        // ----- Line 2 fields (fixed-width columns) -----
        // cols 9-16:  inclination (deg)
        out.inclination =
            std::stod(line2.substr(8, 8)) * DEG2RAD;
        // cols 18-25: RAAN (deg)
        out.raan =
            std::stod(line2.substr(17, 8)) * DEG2RAD;
        // cols 27-33: eccentricity (leading decimal assumed)
        std::string eccStr = "0." + line2.substr(26, 7);
        out.eccentricity = std::stod(eccStr);
        // cols 35-42: argument of perigee (deg)
        out.argPerigee =
            std::stod(line2.substr(34, 8)) * DEG2RAD;
        // cols 44-51: mean anomaly (deg)
        out.meanAnomaly =
            std::stod(line2.substr(43, 8)) * DEG2RAD;
        // cols 53-63: mean motion (rev/day)
        out.meanMotion =
            std::stod(line2.substr(52, 11));

        // ----- Epoch from Line 1 -----
        // cols 19-20: 2-digit year, cols 21-32: day-of-year
        int year2 = std::stoi(line1.substr(18, 2));
        double dayOfYear = std::stod(line1.substr(20, 12));
        int year = (year2 < 57) ? (2000 + year2)
                                : (1900 + year2);

        // Convert to rough Unix epoch millis
        // (days from 1970-01-01 to Jan 1 of `year`)
        int64_t y = year;
        int64_t days = 365 * (y - 1970)
                       + (y - 1969) / 4
                       - (y - 1901) / 100
                       + (y - 1601) / 400;
        double totalDays = static_cast<double>(days)
                           + (dayOfYear - 1.0);
        out.epochMs =
            static_cast<int64_t>(totalDays * 86400.0 * 1000.0);

        return true;
    }
    catch (...)
    {
        return false;
    }
}

// --------------- Simplified Keplerian propagation --------

/// Solve Kepler's equation M = E - e*sin(E) via Newton-Raphson
static double SolveKepler(double M, double e, int maxIter = 10)
{
    double E = M;
    for (int i = 0; i < maxIter; i++)
    {
        double dE = (E - e * std::sin(E) - M)
                    / (1.0 - e * std::cos(E));
        E -= dE;
        if (std::fabs(dE) < 1e-12)
            break;
    }
    return E;
}

EcefPosition PropagateSatellite(const OrbitalElements &elems,
                                int64_t dtMs)
{
    double dtSec = static_cast<double>(dtMs) / 1000.0;

    // Mean motion in rad/s
    double n = elems.meanMotion * 2.0 * M_PI / SEC_PER_DAY;

    // Semi-major axis from mean motion: a = (GM / n^2)^(1/3)
    double a = std::cbrt(GM / (n * n));

    // Mean anomaly at current time
    double M = elems.meanAnomaly + n * dtSec;
    // Normalize to [0, 2*pi)
    M = std::fmod(M, 2.0 * M_PI);
    if (M < 0.0)
        M += 2.0 * M_PI;

    // Eccentric anomaly
    double E = SolveKepler(M, elems.eccentricity);

    // True anomaly
    double sinNu = std::sqrt(1.0 - elems.eccentricity
                                        * elems.eccentricity)
                   * std::sin(E)
                   / (1.0 - elems.eccentricity * std::cos(E));
    double cosNu =
        (std::cos(E) - elems.eccentricity)
        / (1.0 - elems.eccentricity * std::cos(E));
    double nu = std::atan2(sinNu, cosNu);

    // Radius
    double r =
        a * (1.0 - elems.eccentricity * std::cos(E));

    // Position in orbital plane
    double xOrb = r * std::cos(nu);
    double yOrb = r * std::sin(nu);

    // Argument of latitude
    double u = elems.argPerigee + nu;

    // RAAN corrected for Earth rotation
    double raanCorr =
        elems.raan - OMEGA_E * dtSec;

    // ECI -> ECEF (simplified: ignoring J2 RAAN drift)
    double cosU = std::cos(u);
    double sinU = std::sin(u);
    double cosR = std::cos(raanCorr);
    double sinR = std::sin(raanCorr);
    double cosI = std::cos(elems.inclination);
    double sinI = std::sin(elems.inclination);

    double x = (cosR * cosU - sinR * sinU * cosI) * xOrb
             + (-cosR * sinU - sinR * cosU * cosI) * yOrb;
    double y = (sinR * cosU + cosR * sinU * cosI) * xOrb
             + (-sinR * sinU + cosR * cosU * cosI) * yOrb;
    double z = (sinU * sinI) * xOrb
             + (cosU * sinI) * yOrb;

    return {x, y, z};
}

// --------------- SatelliteState methods ------------------

void SatelliteState::updateTle(const OrbitalElements &elems)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_elems = elems;
    m_hasTle = true;
}

bool SatelliteState::getOrbitalElements(
    OrbitalElements &out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasTle)
        return false;
    out = m_elems;
    return true;
}

void SatelliteState::setPosition(const EcefPosition &pos,
                                 int64_t timeMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_position = pos;
    m_posTimeMs = timeMs;
    m_hasPosition = true;
}

bool SatelliteState::getPosition(EcefPosition &pos,
                                 int64_t &timeMs) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasPosition)
        return false;
    pos = m_position;
    timeMs = m_posTimeMs;
    return true;
}

} // namespace nr::gnb
