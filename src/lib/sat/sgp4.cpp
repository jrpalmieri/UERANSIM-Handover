#include "sgp4.hpp"

#include <libsgp4/DateTime.h>
#include <libsgp4/Eci.h>
#include <libsgp4/SGP4.h>
#include <libsgp4/Tle.h>
#include <cmath>


namespace nr::sat
{

int64_t DateTimeToUnixMillis(const libsgp4::DateTime &dateTime)
{
    const libsgp4::DateTime unixEpoch(1970, 1, 1, 0, 0, 0);
    auto delta = dateTime - unixEpoch;
    return static_cast<int64_t>(std::llround(delta.TotalMilliseconds()));
}

libsgp4::DateTime UnixMillisToDateTime(int64_t unixMillis)
{
    // DateTime ticks are microseconds since 0001-01-01 (not UNIX epoch).
    // UnixEpoch = 62135596800000000 µs is the offset from year-1 to 1970-01-01.
    static constexpr int64_t UNIX_EPOCH_TICKS = 62135596800000000LL;
    return libsgp4::DateTime(UNIX_EPOCH_TICKS + unixMillis * 1000LL);
}

Propagator::Propagator(const SatTleEntry &tleEntry)
    : m_tle(tleEntry.line1, tleEntry.line2), m_sgp4(m_tle)
{
}

// propagates the satellite location based on its stored TLE and provided timestamp.
// returns ECEF coordinates (in meters)
EcefPosition Propagator::FindPositionEcef(const int64_t unixMillis) const
{
    libsgp4::DateTime dt = UnixMillisToDateTime(unixMillis);
    auto eci =  m_sgp4.FindPosition(dt);
    return EciToEcef(eci);
}

EcefPosition Propagator::EciToEcef(const libsgp4::Eci& eci) const {

    // 1. Get the GMST angle (in radians) directly from the library's DateTime object
    double theta = eci.GetDateTime().ToGreenwichSiderealTime();

    // 2. Extract the raw ECI (TEME) position vector (already in kilometers)
    double x_eci = eci.Position().x;
    double y_eci = eci.Position().y;
    double z_eci = eci.Position().z;
    
    // 3. Apply the 2D rotation for the Earth's spin
    EcefPosition ecef;
    ecef.x = (x_eci * std::cos(theta)) + (y_eci * std::sin(theta));
    ecef.y = (-x_eci * std::sin(theta)) + (y_eci * std::cos(theta));
    ecef.z = z_eci; // The Z-axis remains identical
    ecef.timestampMs = DateTimeToUnixMillis(eci.GetDateTime());

    // scale the units from kilometers to meters
    ecef.x *= 1000.0;
    ecef.y *= 1000.0;
    ecef.z *= 1000.0;

    return ecef;
}


/**
 * @brief Extracts the Keplerian Orbital Parameters from the TLE
 * stored in the Propagator, and returns them in a fixed-point scaled format 
 * suitable for SIB19 broadcasting.
 * 
 * NEED TO CONFIRM whether the math is correct
 * 
 * @param unixMillis 
 * @return KeplerianElementsRaw 
 */
KeplerianElementsRaw Propagator::GetKeplerianElements(const int64_t unixMillis) const
{
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

    libsgp4::DateTime now = UnixMillisToDateTime(unixMillis);
    
    // extract orbital elements using the library's built-in function
    auto k = libsgp4::OrbitalElements(m_tle);

    // OrbitalElements::MeanMotion() returns rad/min; convert to rad/s
    double n_rad_s = k.MeanMotion() / 60.0;
    double a = std::cbrt(GM / (n_rad_s * n_rad_s));

    // Propagate mean anomaly from TLE epoch to broadcast time
    double dtSec = (now - k.Epoch()).TotalSeconds();
    double M_now = std::fmod(k.MeanAnomoly() + n_rad_s * dtSec, TWO_PI);
    if (M_now < 0.0) M_now += TWO_PI;

    KeplerianElementsRaw elements{};
    elements.semiMajorAxis = static_cast<int64_t>(std::round(a));           // meters
    elements.eccentricity  = static_cast<int64_t>(k.Eccentricity() * (1 << 20));
    elements.periapsis     = encAngle(k.ArgumentPerigee());
    elements.longitude     = encAngle(k.AscendingNode());
    elements.inclination   = encAngle(k.Inclination());
    elements.meanAnomaly   = encAngle(M_now);

    return elements;
}

// parses a TLE string to obtain the epoch value, and converts it to a libsgp4::DateTime object.
libsgp4::DateTime ParseTleEpochDateTime(const std::string &tleEpoch, const std::string &fieldPath)
{
    if (tleEpoch.size() < 5)
        throw std::runtime_error(fieldPath + " must be in TLE format YYDDD.DDD...");

    if (!std::isdigit(static_cast<unsigned char>(tleEpoch[0])) ||
        !std::isdigit(static_cast<unsigned char>(tleEpoch[1])))
    {
        throw std::runtime_error(fieldPath + " must start with a 2-digit year (YY)");
    }

    int year2 = (tleEpoch[0] - '0') * 10 + (tleEpoch[1] - '0');
    int fullYear = year2 >= 57 ? (1900 + year2) : (2000 + year2);

    double dayOfYear = 0.0;
    try
    {
        dayOfYear = std::stod(tleEpoch.substr(2));
    }
    catch (const std::exception &)
    {
        throw std::runtime_error(fieldPath + " has invalid day-of-year value");
    }

    if (!(dayOfYear >= 1.0 && dayOfYear < 367.0))
        throw std::runtime_error(fieldPath + " day-of-year must be in [1, 367)");

    return libsgp4::DateTime(static_cast<unsigned int>(fullYear), dayOfYear);
}

} // namespace nr::sat