// //
// // UE Position types and conversions for distance-based (D1) events.
// //
// // Supports:
// //   - Geographic coordinates (latitude, longitude, altitude)
// //   - Earth-Centered Earth-Fixed (ECEF) Cartesian coordinates
// //   - Conversion between geographic and ECEF
// //   - Euclidean distance computation in ECEF
// //   - Elevation angle computation from UE to a target point
// //

// #pragma once

// #include <utils/common_types.hpp>
// #include <utils/json.hpp>
// #include <utils/position_calcs.hpp>

// namespace nr::ue
// {

// /**
//  * @brief Geographic position (WGS-84 ellipsoid).
//  *  - latitude:  degrees, [-90, +90], positive = North
//  *  - longitude: degrees, [-180, +180], positive = East
//  *  - altitude:  meters above WGS-84 ellipsoid
//  */
// using GeoPosition = ::GeoPosition;

// /**
//  * @brief Earth-Centered Earth-Fixed (ECEF) Cartesian position, in meters.
//  */
// using EcefPosition = ::EcefPosition;

// /**
//  * @brief Complete UE position, stored in both geographic and ECEF forms.
//  *
//  *  The geographic form is human-readable (config files, logs).
//  *  The ECEF form is used for distance and elevation-angle computations,
//  *  particularly for LEO satellite scenarios.
//  */
// struct UePosition
// {
//     GeoPosition geo{};
//     EcefPosition ecef{};
// };

// /**
//  * @brief Convert geographic (lat, lon, alt) to ECEF (x, y, z).
//  *
//  * Uses WGS-84 ellipsoid parameters.
//  */
// inline EcefPosition geoToEcef(const GeoPosition &geo)
// {
//     return GeoToEcef(geo);
// }

// /**
//  * @brief Compute Euclidean distance (meters) between two ECEF positions.
//  */
// inline double ecefDistance(const EcefPosition &a, const EcefPosition &b)
// {
//     return EcefDistance(a, b);
// }

// /**
//  * @brief Compute the elevation angle (degrees) from a ground position
//  *  to a target (e.g., LEO satellite) in ECEF coordinates.
//  *
//  *  Elevation angle is measured from the local horizontal plane at the
//  *  ground position.  Returns a value in [-90, +90].
//  *  Positive means the target is above the horizon.
//  */
// inline double elevationAngle(const GeoPosition &groundGeo,
//                              const EcefPosition &groundEcef,
//                              const EcefPosition &target)
// {
//     (void)groundGeo;
//     return ElevationAngleDeg(groundEcef, target);
// }

// /**
//  * @brief Compute the sub-satellite (nadir) point on the WGS-84 ellipsoid.
//  *
//  *  Given a satellite's ECEF position (x, y, z), the nadir is the point
//  *  on the Earth's surface directly below the satellite.  This is found
//  *  by converting the satellite ECEF to geodetic lat/lon, setting the
//  *  altitude to zero, and converting back to ECEF.
//  *
//  *  Used for NTN CHO Event D1 distance calculations where the reference
//  *  point is the satellite beam centre (nadir).
//  */
// inline EcefPosition computeNadir(double satX, double satY, double satZ)
// {
//     return ComputeNadir(EcefPosition{satX, satY, satZ});
// }

// /* ------------------------------------------------------------------ */
// /*  JSON helpers                                                       */
// /* ------------------------------------------------------------------ */

// Json ToJson(const GeoPosition &v);
// Json ToJson(const EcefPosition &v);
// Json ToJson(const UePosition &v);

// } // namespace nr::ue
