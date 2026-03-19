#include "sat_pos_sim.hpp"

#include <cmath>
#include <algorithm>

namespace nr::gnb
{

/// Free-space path loss in dB.
/// distance: meters,  frequencyHz: Hz.
/// FSPL = 20*log10(d) + 20*log10(f) - 147.55
static double FreeSpacePathLossDb(double distanceM,
                                  double frequencyHz)
{
    if (distanceM <= 0.0)
        distanceM = 1.0;
    return 20.0 * std::log10(distanceM)
         + 20.0 * std::log10(frequencyHz)
         - 147.55;
}

int SatelliteSimulatedDbm(const EcefPosition &gnbEcef,
                          const GeoPosition &ueGeo,
                          const SatelliteLinkConfig &link)
{
    EcefPosition ueEcef = GeoToEcef(ueGeo);
    double dist = EcefDistance(gnbEcef, ueEcef);

    double fspl = FreeSpacePathLossDb(dist, link.frequencyHz);

    // Link budget: Prx = Ptx(dBW) + Gtx + Grx - FSPL
    // Convert result to dBm (dBW + 30 = dBm)
    double rxDbm = (link.txPowerDbW + 30.0)
                 + link.txGainDbi
                 + link.rxGainDbi
                 - fspl;

    return static_cast<int>(std::round(rxDbm));
}

int TerrestrialSimulatedDbm(const GeoPosition &gnbGeo,
                            const GeoPosition &ueGeo,
                            const SatelliteLinkConfig &link)
{
    EcefPosition gnbEcef = GeoToEcef(gnbGeo);
    EcefPosition ueEcef  = GeoToEcef(ueGeo);
    double dist = EcefDistance(gnbEcef, ueEcef);

    double fspl = FreeSpacePathLossDb(dist, link.frequencyHz);

    double rxDbm = (link.txPowerDbW + 30.0)
                 + link.txGainDbi
                 + link.rxGainDbi
                 - fspl;

    return static_cast<int>(std::round(rxDbm));
}

} // namespace nr::gnb
