#include "sat_base.hpp"
#include <math.h>

namespace nr::sat
{

static constexpr double WGS84_A = 6378137.0;
static constexpr double WGS84_F = 1.0 / 298.257223563;
static constexpr double WGS84_B = WGS84_A * (1.0 - WGS84_F);
static constexpr double WGS84_E2 = 2.0 * WGS84_F - WGS84_F * WGS84_F;
static constexpr double WGS84_EP2 = (WGS84_A * WGS84_A - WGS84_B * WGS84_B) / (WGS84_B * WGS84_B);
static constexpr double DEG2RAD = M_PI / 180.0;
static constexpr double RAD2DEG = 180.0 / M_PI;


EcefPosition::EcefPosition(const GeoPosition &geo) : EcefPosition(GeoToEcef(geo))
{
};



/**
 * @brief Converts a WGS84 geodic position to ECEF coordinates.
 * 
 * @param geo 
 * @return EcefPosition 
 */
EcefPosition GeoToEcef(const GeoPosition &geo)
{
    double lat = geo.latitude * DEG2RAD;
    double lon = geo.longitude * DEG2RAD;
    double sinLat = std::sin(lat);
    double cosLat = std::cos(lat);
    double sinLon = std::sin(lon);
    double cosLon = std::cos(lon);

    double n = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sinLat * sinLat);

    double x = (n + geo.altitude) * cosLat * cosLon;
    double y = (n + geo.altitude) * cosLat * sinLon;
    double z = (n * (1.0 - WGS84_E2) + geo.altitude) * sinLat;
    return {x, y, z, geo.timestampMs};
}

/**
 * @brief Converts ECEF coordinates to a WGS84 geodetic position.
 * 
 * @param ecef 
 * @return GeoPosition 
 */
GeoPosition EcefToGeo(const EcefPosition &ecef)
{
    double x = ecef.x;
    double y = ecef.y;
    double z = ecef.z;

    double lon = std::atan2(y, x);
    double p = std::sqrt(x * x + y * y);

    if (p < 1e-9)
    {
        double latPole = z >= 0.0 ? (M_PI / 2.0) : (-M_PI / 2.0);
        double altPole = std::fabs(z) - WGS84_B;
        return {latPole * RAD2DEG, lon * RAD2DEG, altPole, ecef.timestampMs};
    }

    double theta = std::atan2(z * WGS84_A, p * WGS84_B);
    double sinTheta = std::sin(theta);
    double cosTheta = std::cos(theta);

    double lat = std::atan2(z + WGS84_EP2 * WGS84_B * sinTheta * sinTheta * sinTheta,
                            p - WGS84_E2 * WGS84_A * cosTheta * cosTheta * cosTheta);

    for (int i = 0; i < 5; i++)
    {
        double sinLat = std::sin(lat);
        double n = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sinLat * sinLat);
        lat = std::atan2(z + WGS84_E2 * n * sinLat, p);
    }

    double sinLat = std::sin(lat);
    double n = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sinLat * sinLat);
    double alt = p / std::cos(lat) - n;

    return {lat * RAD2DEG, lon * RAD2DEG, alt, ecef.timestampMs};
}

/**
 * @brief Computes the euclidian distance between two ECEF positions.
 * 
 * @param a 
 * @param b 
 * @return distance in meters
 */
double EcefDistance(const EcefPosition &a, const EcefPosition &b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/**
 * @brief Calculates the elevation angle (in degrees) from an observer to a target, given their ECEF coordinates.
 * 
 * @param observer 
 * @param target 
 * @return elevation angle in degrees
 */
double ElevationAngleDeg(const EcefPosition &observer, const EcefPosition &target)
{
    double obsMag = std::sqrt(observer.x * observer.x + observer.y * observer.y + observer.z * observer.z);
    if (obsMag < 1.0)
        return -90.0;

    double ux = observer.x / obsMag;
    double uy = observer.y / obsMag;
    double uz = observer.z / obsMag;

    double rx = target.x - observer.x;
    double ry = target.y - observer.y;
    double rz = target.z - observer.z;
    double range = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (range < 1.0)
        return 90.0;

    double sinElev = (ux * rx + uy * ry + uz * rz) / range;
    sinElev = std::clamp(sinElev, -1.0, 1.0);
    return std::asin(sinElev) * RAD2DEG;
}

double HaversineDistanceMeters(const GeoPosition &a, const GeoPosition &b)
{
    double lat1 = a.latitude * DEG2RAD;
    double lat2 = b.latitude * DEG2RAD;
    double dLat = (b.latitude - a.latitude) * DEG2RAD;
    double dLon = (b.longitude - a.longitude) * DEG2RAD;

    double h = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
               std::cos(lat1) * std::cos(lat2) * std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));

    static constexpr double EARTH_RADIUS_M = 6371000.0;
    return EARTH_RADIUS_M * c;
}

double InitialBearingDeg(const GeoPosition &a, const GeoPosition &b)
{
    double lat1 = a.latitude * DEG2RAD;
    double lat2 = b.latitude * DEG2RAD;
    double dLon = (b.longitude - a.longitude) * DEG2RAD;

    double y = std::sin(dLon) * std::cos(lat2);
    double x = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    double bearing = std::atan2(y, x) * RAD2DEG;

    if (bearing < 0.0)
        bearing += 360.0;
    return bearing;
}

EcefPosition ComputeNadir(const EcefPosition &sat)
{
    EcefPosition nadir{};

    // Old way (naive spherical Earth):
    // GeoPosition satGeo = EcefToGeo(satelliteEcef);
    // satGeo.altitude = 0.0;
    // return GeoToEcef(satGeo);

    // New way - Bowring's method (no trigonometric functions)

    // 1. WGS84 Ellipsoid Constants (in meters, matching ECEF input units)
    constexpr double a = 6378137.0;
    constexpr double f = 1.0 / 298.257223563;
    constexpr double b = a * (1.0 - f);
    
    // Eccentricity constants
    constexpr double e2 = 1.0 - (b * b) / (a * a);
    constexpr double ep2 = (a * a) / (b * b) - 1.0;

    // 2. Distance from the Z-axis
    double p = std::sqrt(sat.x * sat.x + sat.y * sat.y);
    
    // Edge case: Satellite is directly over the North/South pole
    if (p < 1e-6) {
        nadir.x = 0.0;
        nadir.y = 0.0;
        nadir.z = sat.z > 0 ? b : -b;
        return nadir;
    }

    // 3. Parametric latitude components (Algebraic 'sin' and 'cos' of beta)
    double pb = p * b;
    double za = sat.z * a;
    double hyp1 = std::sqrt(pb * pb + za * za);
    double s = za / hyp1; 
    double c = pb / hyp1; 

    // 4. Geodetic latitude components (Bowring's equation)
    double pn = p - e2 * a * (c * c * c);
    double zn = sat.z + ep2 * b * (s * s * s);

    // 5. Normal vector components (Algebraic 'sin' and 'cos' of phi)
    double hyp2 = std::sqrt(pn * pn + zn * zn);
    double s_phi = zn / hyp2; 
    double c_phi = pn / hyp2; 

    // 6. Calculate the ellipsoid's prime vertical radius of curvature (N)
    double N = a / std::sqrt(1.0 - e2 * (s_phi * s_phi));

    // 7. Project back to 3D Cartesian coordinates
    double p_ell = N * c_phi;
    nadir.x = sat.x * (p_ell / p);
    nadir.y = sat.y * (p_ell / p);
    nadir.z = N * (1.0 - e2) * s_phi;

    return nadir;

}

EcefPosition ExtrapolateEcefPosition(const EcefPosition &position,
                                     const EcefPosition &velocity,
                                     double dtSec)
{
    EcefPosition out{};
    out.x = position.x + velocity.x * dtSec;
    out.y = position.y + velocity.y * dtSec;
    out.z = position.z + velocity.z * dtSec;
    out.timestampMs = position.timestampMs + int64_t(dtSec*1000);
    return out;
}

void ExtrapolateEcefPosition(double posX,
                             double posY,
                             double posZ,
                             double velX,
                             double velY,
                             double velZ,
                             double dtSec,
                             double &x,
                             double &y,
                             double &z)
{
    x = posX + velX * dtSec;
    y = posY + velY * dtSec;
    z = posZ + velZ * dtSec;
}




} // namespace nr::sat