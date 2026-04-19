//
// Shared geographic and position calculation helpers.
//

#pragma once

#include "common_types.hpp"

#include <cstdint>

/// Convert geodetic lat/lon/alt (WGS-84) to ECEF (meters).
EcefPosition GeoToEcef(const GeoPosition &geo);

/// Convert ECEF (meters) to geodetic lat/lon/alt (WGS-84).
GeoPosition EcefToGeo(const EcefPosition &ecef);

/// Compute Euclidean distance (meters) between two ECEF points.
double EcefDistance(const EcefPosition &a, const EcefPosition &b);

/// Compute the elevation angle in degrees from an observer to a target in ECEF.
double ElevationAngleDeg(const EcefPosition &observer, const EcefPosition &target);

/// Compute surface distance in meters using a Haversine sphere approximation.
double HaversineDistanceMeters(const GeoPosition &a, const GeoPosition &b);

/// Compute initial bearing from point A to B in degrees, normalized to [0, 360).
double InitialBearingDeg(const GeoPosition &a, const GeoPosition &b);

/// Compute the sub-satellite nadir point on the WGS-84 ellipsoid (altitude = 0 m).
EcefPosition ComputeNadir(const EcefPosition &satelliteEcef);

/// Extrapolate an ECEF position linearly from current position and velocity vectors.
EcefPosition ExtrapolateEcefPosition(const EcefPosition &position,
                                     const EcefPosition &velocity,
                                     double dtSec);

/// Extrapolate an ECEF position linearly from scalar position and velocity components.
void ExtrapolateEcefPosition(double posX,
                             double posY,
                             double posZ,
                             double velX,
                             double velY,
                             double velZ,
                             double dtSec,
                             double &x,
                             double &y,
                             double &z);
