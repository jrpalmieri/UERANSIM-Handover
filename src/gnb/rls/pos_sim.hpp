#pragma once

#include <gnb/types.hpp>
#include <utils/common_types.hpp>
#include <lib/sat/sat_calc.hpp>

namespace nr::gnb
{

/// Compute simulated RSRP (dBm) for the satellite link.
/// gnbEcef: current satellite ECEF position (meters).
/// ueGeo:   UE position in lat/lon/alt.
/// link:    satellite link budget parameters.
int SatelliteSimulatedDbm(const nr::sat::EcefPosition &gnbEcef,
                          const GeoPosition &ueGeo,
                          const GnbRfLinkConfig &link);

/// Compute simulated RSRP (dBm) for a terrestrial gNB.
/// gnbGeo: gNB position in lat/lon/alt.
/// ueGeo:  UE position in lat/lon/alt.
/// link:   link budget parameters (reused for consistency).
int TerrestrialSimulatedDbm(const GeoPosition &gnbGeo,
                            const GeoPosition &ueGeo,
                            const GnbRfLinkConfig &link);

} // namespace nr::gnb