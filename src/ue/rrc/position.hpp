//
// UE Position types and conversions for distance-based (D1) events.
//
// Supports:
//   - Geographic coordinates (latitude, longitude, altitude)
//   - Earth-Centered Earth-Fixed (ECEF) Cartesian coordinates
//   - Conversion between geographic and ECEF
//   - Euclidean distance computation in ECEF
//   - Elevation angle computation from UE to a target point
//

#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

#include <utils/json.hpp>

namespace nr::ue
{

/**
 * @brief Geographic position (WGS-84 ellipsoid).
 *  - latitude:  degrees, [-90, +90], positive = North
 *  - longitude: degrees, [-180, +180], positive = East
 *  - altitude:  meters above WGS-84 ellipsoid
 */
struct GeoPosition
{
    double latitude{};   // degrees
    double longitude{};  // degrees
    double altitude{};   // meters above ellipsoid
};

/**
 * @brief Earth-Centered Earth-Fixed (ECEF) Cartesian position, in meters.
 */
struct EcefPosition
{
    double x{};  // meters
    double y{};  // meters
    double z{};  // meters
};

/**
 * @brief Complete UE position, stored in both geographic and ECEF forms.
 *
 *  The geographic form is human-readable (config files, logs).
 *  The ECEF form is used for distance and elevation-angle computations,
 *  particularly for LEO satellite scenarios.
 */
struct UePosition
{
    GeoPosition geo{};
    EcefPosition ecef{};
};

/* ------------------------------------------------------------------ */
/*  WGS-84 constants                                                   */
/* ------------------------------------------------------------------ */

namespace wgs84
{
    static constexpr double A  = 6378137.0;             // Semi-major axis (m)
    static constexpr double F  = 1.0 / 298.257223563;   // Flattening
    static constexpr double B  = A * (1.0 - F);         // Semi-minor axis (m)
    static constexpr double E2 = 1.0 - (B * B) / (A * A); // First eccentricity squared
} // namespace wgs84

/* ------------------------------------------------------------------ */
/*  Conversion helpers                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Convert geographic (lat, lon, alt) to ECEF (x, y, z).
 *
 * Uses WGS-84 ellipsoid parameters.
 */
inline EcefPosition geoToEcef(const GeoPosition &geo)
{
    constexpr double DEG2RAD = M_PI / 180.0;
    double lat = geo.latitude  * DEG2RAD;
    double lon = geo.longitude * DEG2RAD;

    double sinLat = std::sin(lat);
    double cosLat = std::cos(lat);
    double sinLon = std::sin(lon);
    double cosLon = std::cos(lon);

    // Radius of curvature in the prime vertical
    double N = wgs84::A / std::sqrt(1.0 - wgs84::E2 * sinLat * sinLat);

    EcefPosition ecef{};
    ecef.x = (N + geo.altitude) * cosLat * cosLon;
    ecef.y = (N + geo.altitude) * cosLat * sinLon;
    ecef.z = (N * (1.0 - wgs84::E2) + geo.altitude) * sinLat;
    return ecef;
}

/**
 * @brief Compute Euclidean distance (meters) between two ECEF positions.
 */
inline double ecefDistance(const EcefPosition &a, const EcefPosition &b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * @brief Compute the elevation angle (degrees) from a ground position
 *  to a target (e.g., LEO satellite) in ECEF coordinates.
 *
 *  Elevation angle is measured from the local horizontal plane at the
 *  ground position.  Returns a value in [-90, +90].
 *  Positive means the target is above the horizon.
 */
inline double elevationAngle(const GeoPosition &groundGeo,
                             const EcefPosition &groundEcef,
                             const EcefPosition &target)
{
    constexpr double DEG2RAD = M_PI / 180.0;
    constexpr double RAD2DEG = 180.0 / M_PI;

    // Vector from ground to target
    double dx = target.x - groundEcef.x;
    double dy = target.y - groundEcef.y;
    double dz = target.z - groundEcef.z;
    double range = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (range < 1e-9)
        return 90.0;

    // Unit "up" vector at the ground position (normal to WGS-84 ellipsoid)
    double lat = groundGeo.latitude  * DEG2RAD;
    double lon = groundGeo.longitude * DEG2RAD;
    double upX = std::cos(lat) * std::cos(lon);
    double upY = std::cos(lat) * std::sin(lon);
    double upZ = std::sin(lat);

    // Dot product of (ground→target) with up vector gives sine of elevation
    double sinElev = (dx * upX + dy * upY + dz * upZ) / range;

    // Clamp for numerical safety
    if (sinElev > 1.0)  sinElev = 1.0;
    if (sinElev < -1.0) sinElev = -1.0;

    return std::asin(sinElev) * RAD2DEG;
}

/**
 * @brief Compute the sub-satellite (nadir) point on the WGS-84 ellipsoid.
 *
 *  Given a satellite's ECEF position (x, y, z), the nadir is the point
 *  on the Earth's surface directly below the satellite.  This is found
 *  by converting the satellite ECEF to geodetic lat/lon, setting the
 *  altitude to zero, and converting back to ECEF.
 *
 *  Used for NTN CHO Event D1 distance calculations where the reference
 *  point is the satellite beam centre (nadir).
 */
inline EcefPosition computeNadir(double satX, double satY, double satZ)
{
    constexpr double RAD2DEG = 180.0 / M_PI;

    // Longitude is straightforward from ECEF
    double lon = std::atan2(satY, satX);
    double p   = std::sqrt(satX * satX + satY * satY);

    // Iterative latitude computation (Bowring's method)
    double lat = std::atan2(satZ, p * (1.0 - wgs84::E2));  // initial estimate
    for (int i = 0; i < 5; i++)
    {
        double sinLat = std::sin(lat);
        double N = wgs84::A / std::sqrt(1.0 - wgs84::E2 * sinLat * sinLat);
        lat = std::atan2(satZ + wgs84::E2 * N * sinLat, p);
    }

    // Convert (lat, lon, h=0) back to ECEF
    GeoPosition nadirGeo{};
    nadirGeo.latitude  = lat * RAD2DEG;
    nadirGeo.longitude = lon * RAD2DEG;
    nadirGeo.altitude  = 0.0;

    return geoToEcef(nadirGeo);
}

/* ------------------------------------------------------------------ */
/*  JSON helpers                                                       */
/* ------------------------------------------------------------------ */

Json ToJson(const GeoPosition &v);
Json ToJson(const EcefPosition &v);
Json ToJson(const UePosition &v);

} // namespace nr::ue
