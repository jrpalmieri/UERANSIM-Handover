#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <utils/common_types.hpp>


namespace nr::sat
{

/// Earth-Centered Earth-Fixed position in meters
struct EcefPosition
{
    double x{};
    double y{};
    double z{};
    int64_t timestampMs{};

    EcefPosition() = default;

    EcefPosition(double x, double y, double z)
        : x(x), y(y), z(z)
    {
    }
    EcefPosition(double x, double y, double z, int64_t timestampMs)
        : x(x), y(y), z(z), timestampMs(timestampMs)
    {
    }
    explicit EcefPosition(const GeoPosition &geo);
};

struct SatEcefState
{
    EcefPosition pos{};
    EcefPosition nadir{};
};

struct KeplerianElementsRaw
{
    int64_t semiMajorAxis{};
    int64_t eccentricity{};
    int64_t periapsis{};
    int64_t longitude{};
    int64_t inclination{};
    int64_t meanAnomaly{};
};

struct EphEntry
{
    int pci{};

    // pos/vel (ephType == 0)
    double x{}, y{}, z{};
    double vx{}, vy{}, vz{};

    // orbital (ephType == 1)
    int64_t semiMajorAxis{};
    int32_t eccentricity{};
    int32_t periapsis{};
    int32_t longitude{};
    int32_t inclination{};
    int32_t meanAnomaly{};

    // TLE (ephType == 2)
    char tleName[25]{};   // satellite name (up to 24 chars + NUL)
    char tleLine1[70]{};  // TLE line 1 (69 chars + NUL)
    char tleLine2[70]{};  // TLE line 2 (69 chars + NUL)
};

struct NeighborEndpoint
{
    std::string ipAddress{};
    uint16_t port{};
};

struct SatPriorityScore {
    int pci{};
    int score{};

    SatPriorityScore() = default;

    SatPriorityScore(int pci, int score) : pci(pci), score(score)
    {
    }
};

struct SatTleEntry
{
    int pci{};
    std::string name{};   // satellite name (optional, up to 24 chars)
    std::string line1{};
    std::string line2{};
    int64_t lastUpdatedMs{};
};



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

} // namespace nr::sat