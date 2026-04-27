#include "sat_calc.hpp"

#include <algorithm>
#include <cmath>

#include <libsgp4/Eci.h>
#include <libsgp4/SGP4.h>
#include <libsgp4/Tle.h>

#include <utils/position_calcs.hpp>

namespace nr::rrc::common
{


int64_t DateTimeToUnixMillis(const libsgp4::DateTime &dateTime)
{
    const libsgp4::DateTime unixEpoch(1970, 1, 1, 0, 0, 0);
    auto delta = dateTime - unixEpoch;
    return static_cast<int64_t>(std::llround(delta.TotalMilliseconds()));
}

libsgp4::DateTime UnixMillisToDateTime(int64_t unixMillis)
{
    // Note libsgp4 does unix epoch conversion internally, but require microseconds as the input value
    return libsgp4::DateTime(unixMillis * 1000);
}

/**
 * @brief Propagates TLE data to ECEF coordinates based on provided time
 * 
 * @param line1 TLE line 1
 * @param line2 TLE line 2
 * @param dt Time to propagate to
 * @param out Output ECEF position
 * @return true if successful, false otherwise
 */
bool PropagateTleToEcef(const std::string &line1,
                        const std::string &line2,
                        const libsgp4::DateTime &dt,
                        SatEcefState &out)
{
    GeoPosition geo{};
    if (!PropagateTleToGeo(line1, line2, dt, geo))
        return false;

    out.pos = GeoToEcef(geo);
    out.nadir = ComputeNadir(out.pos);
    return true;
}

/**
 * @brief Propagates TLE data to geodetic coordinates based on provided time
 * 
 * @param line1 TLE line 1
 * @param line2 TLE line 2
 * @param dt Time to propagate to
 * @param out Output geodetic position
 * @return true if successful, false otherwise
 */
bool PropagateTleToGeo(const std::string &line1,
                        const std::string &line2,
                        const libsgp4::DateTime &dt,
                        GeoPosition &out)
{
    try
    {
        libsgp4::Tle tle(line1, line2);
        libsgp4::SGP4 sgp4(tle);
        libsgp4::Eci eci = sgp4.FindPosition(dt);
        libsgp4::CoordGeodetic geo = eci.ToGeodetic();

        const double latDeg = geo.latitude * (180.0 / M_PI);
        const double lonDeg = geo.longitude * (180.0 / M_PI);
        const double altM = geo.altitude * 1000.0;

        out = GeoPosition{latDeg, lonDeg, altM};
        return true;
    }
    catch (...)
    {
        return false;
    }
}

int FindExitTimeSec(const std::string &line1,
                    const std::string &line2,
                    const EcefPosition &observer,
                    const libsgp4::DateTime &epoch,
                    int startSec,
                    int thetaDeg,
                    int maxLookaheadSec,
                    EcefPosition &nadirAtExitM)
{
    static constexpr int STEP_SEC = 10;

    nadirAtExitM = EcefPosition{0, 0, 0};
    int tLow = startSec;
    int tHigh = startSec;
    bool found = false;

    for (int t = startSec + STEP_SEC; t <= startSec + maxLookaheadSec; t += STEP_SEC)
    {
        SatEcefState state{};
        if (!PropagateTleToEcef(line1, line2, epoch.AddSeconds(t), state))
            continue;

        if (ElevationAngleDeg(observer, state.pos) < static_cast<double>(thetaDeg))
        {
            tLow = t - STEP_SEC;
            tHigh = t;
            found = true;
            break;
        }
    }

    if (!found)
    {
        const int tEnd = startSec + maxLookaheadSec;
        SatEcefState state{};
        if (PropagateTleToEcef(line1, line2, epoch.AddSeconds(tEnd), state))
            nadirAtExitM = state.nadir;
        return tEnd;
    }

    while (tHigh - tLow > 1)
    {
        const int tMid = (tLow + tHigh) / 2;
        SatEcefState state{};
        if (!PropagateTleToEcef(line1, line2, epoch.AddSeconds(tMid), state))
        {
            tLow = tMid;
            continue;
        }

        if (ElevationAngleDeg(observer, state.pos) >= static_cast<double>(thetaDeg))
            tLow = tMid;
        else
            tHigh = tMid;
    }

    SatEcefState exitState{};
    if (PropagateTleToEcef(line1, line2, epoch.AddSeconds(tHigh), exitState))
        nadirAtExitM = exitState.nadir;

    return tHigh;
}

double SolveKepler(double meanAnomalyRad, double eccentricity)
{
    double eccentricAnomaly = meanAnomalyRad;
    for (int i = 0; i < 20; ++i)
    {
        const double dE = (meanAnomalyRad - eccentricAnomaly + eccentricity * std::sin(eccentricAnomaly)) /
                          (1.0 - eccentricity * std::cos(eccentricAnomaly));
        eccentricAnomaly += dE;
        if (std::fabs(dE) < 1e-12)
            break;
    }
    return eccentricAnomaly;
}

EcefPosition PropagateKeplerianToEcef(const KeplerianElementsRaw &orb,
                                      int64_t epochMs,
                                      int64_t tMs)
{
    constexpr double TWO_PI = 2.0 * M_PI;
    constexpr double GM = 3.986004418e14;
    constexpr double SCALE = TWO_PI / (1LL << 28);

    const double semiMajorAxis = static_cast<double>(orb.semiMajorAxis);
    const double eccentricity = static_cast<double>(orb.eccentricity) / static_cast<double>(1LL << 20);
    const double omega = static_cast<double>(orb.periapsis) * SCALE;
    const double raan = static_cast<double>(orb.longitude) * SCALE;
    const double inclination = static_cast<double>(orb.inclination) * SCALE;
    const double meanAtEpoch = static_cast<double>(orb.meanAnomaly) * SCALE;

    const double meanMotion = std::sqrt(GM / (semiMajorAxis * semiMajorAxis * semiMajorAxis));

    const double dtSec = static_cast<double>(tMs - epochMs) / 1000.0;
    double meanAnomaly = std::fmod(meanAtEpoch + meanMotion * dtSec, TWO_PI);
    if (meanAnomaly < 0.0)
        meanAnomaly += TWO_PI;

    const double eccentricAnomaly = SolveKepler(meanAnomaly, eccentricity);
    const double trueAnomaly = 2.0 * std::atan2(std::sqrt(1.0 + eccentricity) * std::sin(eccentricAnomaly / 2.0),
                                                std::sqrt(1.0 - eccentricity) * std::cos(eccentricAnomaly / 2.0));

    const double radius = semiMajorAxis * (1.0 - eccentricity * std::cos(eccentricAnomaly));
    const double xOrb = radius * std::cos(trueAnomaly);
    const double yOrb = radius * std::sin(trueAnomaly);

    const double cRaan = std::cos(raan);
    const double sRaan = std::sin(raan);
    const double cOmega = std::cos(omega);
    const double sOmega = std::sin(omega);
    const double cInc = std::cos(inclination);
    const double sInc = std::sin(inclination);

    const double xEci = (cRaan * cOmega - sRaan * sOmega * cInc) * xOrb +
                        (-cRaan * sOmega - sRaan * cOmega * cInc) * yOrb;
    const double yEci = (sRaan * cOmega + cRaan * sOmega * cInc) * xOrb +
                        (-sRaan * sOmega + cRaan * cOmega * cInc) * yOrb;
    const double zEci = (sOmega * sInc) * xOrb + (cOmega * sInc) * yOrb;

    const double tSec = static_cast<double>(tMs) / 1000.0;
    const double julianDate = 2440587.5 + tSec / 86400.0;
    const double daysSinceJ2000 = julianDate - 2451545.0;
    double gmst = std::fmod((280.46061837 + 360.98564736629 * daysSinceJ2000) * (M_PI / 180.0), TWO_PI);
    if (gmst < 0.0)
        gmst += TWO_PI;

    EcefPosition ecef{};
    ecef.x = xEci * std::cos(gmst) + yEci * std::sin(gmst);
    ecef.y = -xEci * std::sin(gmst) + yEci * std::cos(gmst);
    ecef.z = zEci;
    return ecef;
}

namespace
{

/**
 * @brief Finds the exit time for a satellite based on its state and an observer's position.
 * Caller provides a resolver function to obtain the satellite's ECEF state at a given time offset, allowing
 * flexibility in how satellite states are computed (e.g., from TLEs, Keplerian elements, or other sources).
 * 
 * @param pci - satellite's gnb PCI value
 * @param observerEcef - observer's location in ECEF coordinates
 * @param startSec - starting time in seconds
 * @param thetaDeg - elevation angle threshold in degrees
 * @param maxLookaheadSec - farthest time to look ahead for an exit event (if no exit occurs before this, function returns this value)
 * @param stateResolver - function used to obtain the satellite's ECEF state at a given time offset
 * @param nadirAtExitM - output parameter to receive the satellite's nadir position at the exit time (or at max lookahead if no exit occurs)
 * @return int - time value representing when the satellite exits the elevation threshold, or max lookahead if it never exits within that time
 */
int FindExitTimeSecWithResolver(int pci,
                                const EcefPosition &observerEcef,
                                int startSec,
                                int thetaDeg,
                                int maxLookaheadSec,
                                const SatStateResolver &stateResolver,
                                EcefPosition &nadirAtExitM)
{
    // step units for the progagation search.  a smaller step will yield a more accurate exit time but require more state resolutions (and thus more CPU time).
    //  10 seconds is a reasonable default that balances accuracy with performance.
    static constexpr int STEP_SEC = 10;

    nadirAtExitM = EcefPosition{0, 0, 0};
    int tLow = startSec;
    int tHigh = startSec;
    bool found = false;

    for (int t = startSec + STEP_SEC; t <= startSec + maxLookaheadSec; t += STEP_SEC)
    {
        SatEcefState state{};
        if (!stateResolver(pci, t, state))
            continue;

        if (ElevationAngleDeg(observerEcef, state.pos) < static_cast<double>(thetaDeg))
        {
            tLow = t - STEP_SEC;
            tHigh = t;
            found = true;
            break;
        }
    }

    if (!found)
    {
        const int tEnd = startSec + maxLookaheadSec;
        SatEcefState endState{};
        if (stateResolver(pci, tEnd, endState))
            nadirAtExitM = endState.nadir;
        return tEnd;
    }

    while (tHigh - tLow > 1)
    {
        const int tMid = (tLow + tHigh) / 2;
        SatEcefState state{};
        if (!stateResolver(pci, tMid, state))
        {
            tLow = tMid;
            continue;
        }

        if (ElevationAngleDeg(observerEcef, state.pos) >= static_cast<double>(thetaDeg))
            tLow = tMid;
        else
            tHigh = tMid;
    }

    SatEcefState exitState{};
    if (stateResolver(pci, tHigh, exitState))
        nadirAtExitM = exitState.nadir;

    return tHigh;
}

} // namespace

/**
 * @brief Prioritizes target satellites based on their transit times.
 * Caller provides a start time (tExitSec) that represents when the serving Sat will be going below
 * the elevation angle threshold relative to an observer (observerEcef), thus requiring a handover 
 * to a target satellite.  The available satellites to target is provided by candidatePcis.
 * 
 * Caller provides a state resolver function that can return the ECEF state of a target satellite at a 
 * given time offset, allowing flexibility in how satellite states are computed (e.g., from TLEs, 
 * Keplerian elements, or other sources).  Caller may also provide an endoiint resolver function
 * to check whether a signaling endpoint exists for a given satellite PCI, allowing satellites without
 * a signaling path to be filtered out.  If this resolver is not needed, caller can simply provide a 
 * dummy function that always returns true.
 * 
 * Returns a vector of (PCI, score) pairs, sorted by score with the best candidate first.
 * The score is the transit time in seconds (higher is better), which is a proxy for the highest reached
 * elevation angle.
 * Other scoring methods may also be used where constellations with different altitudes are involved, 
 * but transit time is a simple and effective method for constellations of similar altitudes. 
 * 
 * @param servingPci 
 * @param candidatePcis 
 * @param observerEcef 
 * @param tExitSec 
 * @param elevationMinDeg 
 * @param maxLookaheadSec 
 * @param stateResolver 
 * @param endpointResolver 
 * @return std::vector<PciScore> 
 */
std::vector<PciScore> PrioritizeTargetSats(int servingPci,
                                           const std::vector<int> &candidatePcis,
                                           const EcefPosition &observerEcef,
                                           int tExitSec,
                                           int elevationMinDeg,
                                           int maxLookaheadSec,
                                           const SatStateResolver &stateResolver,
                                           const EndpointResolver &endpointResolver)
{
    std::vector<PciScore> prioritized{};

    const bool hasUePos = (observerEcef.x != 0.0 || observerEcef.y != 0.0 || observerEcef.z != 0.0);
    if (!hasUePos)
        return prioritized;

    for (int pci : candidatePcis)
    {
        if (pci == servingPci)
            continue;

        NeighborEndpoint endpoint{};
        if (!endpointResolver(pci, endpoint))
            continue;

        SatEcefState stateAtExit{};
        if (!stateResolver(pci, tExitSec, stateAtExit))
            continue;

        if (ElevationAngleDeg(observerEcef, stateAtExit.pos) < static_cast<double>(elevationMinDeg))
            continue;

        EcefPosition nadirDummy{};
        const int tNeighborExit = FindExitTimeSecWithResolver(pci,
                                                              observerEcef,
                                                              tExitSec,
                                                              elevationMinDeg,
                                                              maxLookaheadSec,
                                                              stateResolver,
                                                              nadirDummy);
        const int transitSec = tNeighborExit - tExitSec;
        prioritized.emplace_back(pci, transitSec);
    }

    std::sort(prioritized.begin(), prioritized.end(), [](const PciScore &a, const PciScore &b) {
        if (a.second == b.second)
            return a.first < b.first;
        return a.second > b.second;
    });

    return prioritized;
}
} // namespace nr::rrc::common
