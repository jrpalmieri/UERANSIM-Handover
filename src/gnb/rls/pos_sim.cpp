#include "pos_sim.hpp"
#include <lib/sat/sat_calc.hpp>

#include <cmath>
#include <algorithm>

using nr::sat::EcefPosition;
using nr::sat::EcefToGeo;
using nr::sat::GeoToEcef;

namespace nr::gnb
{

/// Free-space path loss in dB.
/// distance: meters,  frequencyHz: GHz.
static double FreeSpacePathLossDb(double distanceM,
                                  double frequencyHz)
{
    if (distanceM <= 0.0)
        distanceM = 1.0;
    return 20.0 * std::log10(distanceM)
         + 20.0 * std::log10(frequencyHz)
         - 147.55; // FSPL at 1m and 1GHz in dB
}


/// Urban Macro path loss in dB based on 3GPP TS 38.901, for frequencies 0.5-6 GHz.
///  PL = 22*log10(d) + 28 + 20*log10(f)
/// distance: meters,  frequencyHz: Hz.
static double UrbanMacroPathLossDb(double distanceM,
                                  double frequencyHz)
{
    // min distance is 1m
    if (distanceM <= 0.0)
        distanceM = 1.0;

    // convert to GHz
    double freqGHz = frequencyHz / 1e9;
    
    if (distanceM < 1600.0)
       return 28.0 + (22.0 * std::log10(distanceM)) + (20.0 * std::log10(freqGHz));

    return 28.0 + (40.0 * std::log10(distanceM)) + (20.0 * std::log10(freqGHz)) - (9.0 * std::log10(1600.0 * 1600.0));

}

/**
 * @brief Generates a simulated RSRP value (dBm) for a satellite link based on the positions of the gNB and UE, and the link parameters.
 * The RSRP is calculated using a free-space path loss model.
 * 
 * @param gnbEcef 
 * @param ueGeo 
 * @param link 
 * @return int 
 */
int SatelliteSimulatedDbm(const EcefPosition &gnbEcef,
                          const GeoPosition &ueGeo,
                          const GnbRfLinkConfig &link)
{
    EcefPosition ueEcef = GeoToEcef(ueGeo);
    double dist = EcefDistance(gnbEcef, ueEcef);

    // calculate free space path loss
    double fspl = FreeSpacePathLossDb(dist, link.carrFrequencyHz);

    // Link budget: Prx(dBm) = Ptx(dBm) + Gtx + Grx - FSPL
    double rxDbm = link.txPowerDbm
                 + link.txGainDbi
                 + link.ueRxGainDbi
                 - fspl - 1.0; // add an additional 1 dB to account for atmospheric losses, etc.

    return static_cast<int>(std::round(rxDbm));
}

/**
 * @brief Generated a simulated RSRP value (dBm) for a terrestrial link based on the positions of the gNB and UE, and the link parameters.
 * The RSRP is calculated using the urban macro (Uma) model from TS 38.901.
 * 
 * @param gnbGeo 
 * @param ueGeo 
 * @param link 
 * @return int 
 */
int TerrestrialSimulatedDbm(const GeoPosition &gnbGeo,
                            const GeoPosition &ueGeo,
                            const GnbRfLinkConfig &link)
{
    EcefPosition gnbEcef = GeoToEcef(gnbGeo);
    EcefPosition ueEcef  = GeoToEcef(ueGeo);
    double dist = EcefDistance(gnbEcef, ueEcef);

    double uma_pl = UrbanMacroPathLossDb(dist, link.carrFrequencyHz);

    double rxDbm = link.txPowerDbm
                 + link.txGainDbi
                 + link.ueRxGainDbi
                 - uma_pl;

    return static_cast<int>(std::round(rxDbm));
}

} // namespace nr::gnb
