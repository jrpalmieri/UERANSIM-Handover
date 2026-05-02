#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "sat_base.hpp"
#include "sgp4.hpp"
#include <libsgp4/DateTime.h>
#include <utils/common_types.hpp>

namespace nr::sat
{


using EndpointResolver = std::function<bool(int pci, NeighborEndpoint &endpoint)>;
using SatStateResolver = std::function<bool(int pci, int offsetSec, SatEcefState &state)>;

bool PropagateTleToEcef(const std::string &line1,
                        const std::string &line2,
                        const libsgp4::DateTime &dt,
                        SatEcefState &out);

bool PropagateTleToEcef(const std::string &line1,
                        const std::string &line2,
                        const int64_t unixMillis,
                        SatEcefState &out);

bool PropagateTleToGeo(const std::string &line1,
                        const std::string &line2,
                        const libsgp4::DateTime &dt,
                        GeoPosition &out);

bool PropagateTleToGeo(const std::string &line1,
                        const std::string &line2,
                        const int64_t unixMillis,
                        GeoPosition &out);

int FindExitTimeSec(const std::string &line1,
                    const std::string &line2,
                    const EcefPosition &observer,
                    const libsgp4::DateTime &epoch,
                    int startSec,
                    int thetaDeg,
                    int maxLookaheadSec,
                    EcefPosition &nadirAtExitM);

int FindExitTimeSec(const Propagator &sgp4,
                    const EcefPosition &observer,
                    const int64_t unixMillis,
                    int startSec,
                    int thetaDeg,
                    int maxLookaheadSec,
                    EcefPosition &nadirAtExitM);



// int64_t DateTimeToUnixMillis(const libsgp4::DateTime &dateTime);

// libsgp4::DateTime UnixMillisToDateTime(int64_t unixMillis);

double SolveKepler(double meanAnomalyRad, double eccentricity);

EcefPosition PropagateKeplerianToEcef(const KeplerianElementsRaw &orb,
                                      int64_t epochMs,
                                      int64_t tMs);

int64_t TleEpochToUnixMillis(const std::string &epoch);
std::string UnixMillisToTleEpoch(int64_t unixMillis);

} // namespace nr::sat
