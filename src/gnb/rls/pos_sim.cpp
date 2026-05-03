#include "pos_sim.hpp"
#include <lib/sat/sat_calc.hpp>

#include <cmath>
#include <algorithm>

using nr::sat::EcefPosition;
using nr::sat::EcefToGeo;
using nr::sat::GeoToEcef;

namespace nr::gnb
{

double freespacePathLoss_base = 0.0;
double uMaPathLoss_base = 0.0;
double uMaPathLoss_far = 0.0;
double uMaPathLoss_really_far = 0.0;
double linkBudget_base = 0.0;

/// Free-space path loss in dB.
/// distance: meters,  frequencyHz: GHz.
static inline double FastFreeSpacePathLossDb(double distanceM)
{
    // min distance is 10m
    if (distanceM <= 10.0)
        distanceM = 10.0;

    // assumes freeSpacePathLoss_base has already been calculated
    return freespacePathLoss_base + 20.0 * std::log10(distanceM);

}

/// Free-space path loss in dB.
/// distance: meters,  frequencyHz: GHz.
static double FreeSpacePathLossDb(double distanceM,
                                  double frequencyHz)
{
    // min distance is 10m
    if (distanceM <= 10.0)
        distanceM = 10.0;

    return 20.0 * std::log10(distanceM)
         + 20.0 * std::log10(frequencyHz)
         - 147.55; // FSPL at 1m and 1GHz in dB

}


/// Urban Macro path loss in dB based on 3GPP TS 38.901, for frequencies 0.5-6 GHz.
///  PL = 22*log10(d) + 28 + 20*log10(f)
/// distance: meters,  frequencyHz: Hz.
static inline double FastUrbanMacroPathLossDb(double distanceM)
{
    // min distance is 10m
    if (distanceM <= 10.0)
        distanceM = 10.0;
    
    if (distanceM < 1600.0)
       return uMaPathLoss_base + (22.0 * std::log10(distanceM));
    if (distanceM <= 5000.0)
        return uMaPathLoss_far + (40.0 * std::log10(distanceM));

    // This is not 3GPP, but ensures that by 8km, path loss makes signal effectively unusable for typical
    //  link budgets (15dbm TxPower, 15dBi TxGain, 0dBi RxGain)
    return uMaPathLoss_really_far + (100.0 * std::log10(distanceM));

}

/// Urban Macro path loss in dB based on 3GPP TS 38.901, for frequencies 0.5-6 GHz.
///  PL = 22*log10(d) + 28 + 20*log10(f)
/// distance: meters,  frequencyHz: Hz.
static double UrbanMacroPathLossDb(double distanceM,
                                  double frequencyHz)
{
    // min distance is 10m
    if (distanceM <= 10.0)
        distanceM = 10.0;

    // convert to GHz
    double freqGHz = frequencyHz / 1e9;
    
    if (distanceM < 1600.0)
       return 28.0 + (22.0 * std::log10(distanceM)) + (20.0 * std::log10(freqGHz));
    if (distanceM <= 5000.0)
        return 28.0 + (40.0 * std::log10(distanceM)) + (20.0 * std::log10(freqGHz)) - 32.04;

    // This is not 3GPP, but ensures that by 8km, path loss makes signal effectively unusable for typical
    //  link budgets (15dbm TxPower, 15dBi TxGain, 0dBi RxGain)
    return 28.0 + (100.0 * std::log10(distanceM)) + (20.0 * std::log10(freqGHz)) - 254.0;
}


// Inline dot product calculator for ECEF positions, used for the fast horizon check.
inline double dotProduct(const EcefPosition& a, const EcefPosition& b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

inline EcefPosition subtractEcef(const EcefPosition& a, const EcefPosition& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

/**
 * @brief Calculates a penalty for RSRP when the satellite is near the horizon.
 *  - below horizon = MIN_RSRP (unreachable)
 *  - above horizon but below 10 degrees elevation = MIN_RSRP (also unreachable)
 *  - above horizon between 10 and 15 degrees - 3 db penalty
 *  - above horizon between 15 and 20 degrees - 2 db penalty
 *  - above horizon between 20 and 30 degrees - 1 db penalty
 * @param posA The ECEF coordinates of the reference object (e.g., ground station).
 * @param posB The ECEF coordinates of the target object (e.g., LEO satellite).
 * @return True if posB is above the local tangent plane of posA.
 */
double inline horizonPenalty(const EcefPosition& posA, const EcefPosition& posB) {
    
    EcefPosition diff = subtractEcef(posB, posA);

    double dotDiff = dotProduct(diff, posA);
    
    // below horizon
    if (dotDiff <=0.0)
        return cons::MIN_RSRP;

    // if below 10 degrees elevation, also treat as unreachable due to atmospheric effects
    //   optimized to avoid trig and sqrt

    // Square vDotA
    double vDotASq = dotDiff * dotDiff;
    
    // Calculate the squared magnitudes
    double magASq = dotProduct(posA, posA);
    double magVSq = dotProduct(diff, diff);
    
    // 10deg check: sin^2(10 degrees) is approx 0.0301536
    constexpr double SIN_SQ_10_DEG = 0.0301536896; 

    // 15deg check: sin^2(15 degrees) is approx 0.0669873
    constexpr double SIN_SQ_15_DEG = 0.0669872981;

    // 20deg check: sin^2(20 degrees) is approx 0.1169778
    constexpr double SIN_SQ_20_DEG = 0.1169777784;

    // 30deg check: sin^2(30 degrees) is 0.25
    constexpr double SIN_SQ_30_DEG = 0.25;

    // Is (vDotA^2) / (magA^2 * magV^2) > sin^2(10) ?
    // Rewritten to avoid division:
    if (vDotASq < SIN_SQ_10_DEG * magASq * magVSq)
        return cons::MIN_RSRP;
    if (vDotASq < SIN_SQ_15_DEG * magASq * magVSq)
        return -3.0;
    if (vDotASq < SIN_SQ_20_DEG * magASq * magVSq)
        return -2.0;
    if (vDotASq < SIN_SQ_30_DEG * magASq * magVSq)
        return -1.0;

    return 0.0;

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
    // convert to an ECEF position coordinate
    EcefPosition ueEcef = GeoToEcef(ueGeo);

    // if the satellite is near the horizon, add a power penalty to model atmospheric effects
    double penalty = horizonPenalty(ueEcef, gnbEcef);

    // if teh penalty is at the minimum RSRP, this means the satellite is unreachable
    if (penalty == cons::MIN_RSRP)
    {
        return cons::MIN_RSRP - 1;
    }

    double dist = EcefDistance(gnbEcef, ueEcef);

    // calculate free space path loss at this distance
    double fspl = FastFreeSpacePathLossDb(dist);

    // Link budget: Prx(dBm) = Ptx(dBm) + Gtx + Grx - FSPL + Penalty (wil be a -dBm value)
    double rxDbm = linkBudget_base - fspl + penalty;

    // clamp to min/max values
    rxDbm = std::max(rxDbm, static_cast<double>(cons::MIN_RSRP));
    rxDbm = std::min(rxDbm, static_cast<double>(cons::MAX_RSRP));

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

    double uma_pl = FastUrbanMacroPathLossDb(dist);

    double rxDbm = linkBudget_base - uma_pl;

    // clamp to min/max values
    rxDbm = std::max(rxDbm, static_cast<double>(cons::MIN_RSRP));
    rxDbm = std::min(rxDbm, static_cast<double>(cons::MAX_RSRP));

    return static_cast<int>(std::round(rxDbm));
}


// pre-calculate certain values in RSRP calculations, since they do not change over time.
//  avoids repeating expensive calculations
void pre_calculations(double carrierFrequencyHz, double txPowerDbm, double txGainDbi, double ueRxGainDbi,
    double &fsplBase, double &umaBase, double &umaFar, double &lBudget_base)
{
    freespacePathLoss_base =  20.0 * std::log10(carrierFrequencyHz)- 147.55;
    fsplBase = freespacePathLoss_base;

    uMaPathLoss_base = 28.0 + (20.0 * std::log10(carrierFrequencyHz / 1e9));
    umaBase = uMaPathLoss_base;

    uMaPathLoss_far = uMaPathLoss_base - 32.04;
    umaFar = uMaPathLoss_far;

    uMaPathLoss_really_far = uMaPathLoss_base - 254.0;

    linkBudget_base = txPowerDbm + txGainDbi + ueRxGainDbi;
    lBudget_base = linkBudget_base;

}

} // namespace nr::gnb
