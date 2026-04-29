#include <lib/sat/sat_calc.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace nr::sat;

namespace
{

void assertNear(double actual, double expected, double tolerance, const std::string &msg)
{
    if (std::fabs(actual - expected) > tolerance)
    {
        throw std::runtime_error(msg + " expected=" + std::to_string(expected) +
                                 " actual=" + std::to_string(actual) +
                                 " tol=" + std::to_string(tolerance));
    }
}

void assertTrue(bool condition, const std::string &msg)
{
    if (!condition)
        throw std::runtime_error(msg);
}

void testGeoEcefRoundTrip()
{
    GeoPosition geo{33.7756, -84.3963, 320.0};
    EcefPosition ecef = GeoToEcef(geo);
    GeoPosition rt = EcefToGeo(ecef);

    assertNear(rt.latitude, geo.latitude, 1e-6, "latitude round-trip mismatch");
    assertNear(rt.longitude, geo.longitude, 1e-6, "longitude round-trip mismatch");
    assertNear(rt.altitude, geo.altitude, 0.2, "altitude round-trip mismatch");
}

void testEcefDistance()
{
    EcefPosition a{1000.0, 2000.0, -3000.0};
    EcefPosition b{1000.0, 2000.0, -3000.0};
    EcefPosition c{4000.0, 6000.0, -3000.0};

    assertNear(EcefDistance(a, b), 0.0, 1e-9, "zero ECEF distance mismatch");
    assertNear(EcefDistance(a, c), 5000.0, 1e-9, "ECEF distance mismatch");
}

void testHaversineAndBearing()
{
    GeoPosition a{0.0, 0.0, 0.0};
    GeoPosition b{0.0, 1.0, 0.0};

    double dist = HaversineDistanceMeters(a, b);
    assertNear(dist, 111194.9266, 20.0, "haversine distance mismatch");

    double bearing = InitialBearingDeg(a, b);
    assertNear(bearing, 90.0, 1e-6, "initial bearing mismatch");
}

void testComputeNadirAndElevation()
{
    GeoPosition groundGeo{10.0, 20.0, 0.0};
    EcefPosition ground = GeoToEcef(groundGeo);

    GeoPosition satGeo{10.0, 20.0, 550000.0};
    EcefPosition sat = GeoToEcef(satGeo);

    EcefPosition nadir = ComputeNadir(sat);
    GeoPosition nadirGeo = EcefToGeo(nadir);

    assertNear(nadirGeo.latitude, groundGeo.latitude, 1e-6, "nadir latitude mismatch");
    assertNear(nadirGeo.longitude, groundGeo.longitude, 1e-6, "nadir longitude mismatch");
    assertNear(nadirGeo.altitude, 0.0, 0.2, "nadir altitude mismatch");

    double elev = ElevationAngleDeg(ground, sat);
    assertTrue(elev > 89.0 && elev <= 90.0, "elevation should be near zenith");
}

void testExtrapolation()
{
    EcefPosition p{100.0, 200.0, 300.0};
    EcefPosition v{1.5, -2.0, 0.25};

    EcefPosition e = ExtrapolateEcefPosition(p, v, 20.0);
    assertNear(e.x, 130.0, 1e-9, "vector extrapolation X mismatch");
    assertNear(e.y, 160.0, 1e-9, "vector extrapolation Y mismatch");
    assertNear(e.z, 305.0, 1e-9, "vector extrapolation Z mismatch");

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    ExtrapolateEcefPosition(100.0, 200.0, 300.0, 1.5, -2.0, 0.25, 20.0, x, y, z);
    assertNear(x, e.x, 1e-9, "scalar extrapolation X mismatch");
    assertNear(y, e.y, 1e-9, "scalar extrapolation Y mismatch");
    assertNear(z, e.z, 1e-9, "scalar extrapolation Z mismatch");
}

} // namespace

int main()
{
    try
    {
        testGeoEcefRoundTrip();
        testEcefDistance();
        testHaversineAndBearing();
        testComputeNadirAndElevation();
        testExtrapolation();
    }
    catch (const std::exception &e)
    {
        std::cerr << "position_calcs_tests: FAILED: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "position_calcs_tests: OK" << std::endl;
    return 0;
}
