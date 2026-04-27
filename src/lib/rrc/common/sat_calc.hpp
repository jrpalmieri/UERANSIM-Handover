#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <libsgp4/DateTime.h>

#include <utils/common_types.hpp>

namespace nr::rrc::common
{

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

struct NeighborEndpoint
{
    std::string ipAddress{};
    uint16_t port{};
};

using PciScore = std::pair<int, int>;
using EndpointResolver = std::function<bool(int pci, NeighborEndpoint &endpoint)>;
using SatStateResolver = std::function<bool(int pci, int offsetSec, SatEcefState &state)>;

bool PropagateTleToEcef(const std::string &line1,
                        const std::string &line2,
                        const libsgp4::DateTime &dt,
                        SatEcefState &out);

bool PropagateTleToGeo(const std::string &line1,
                        const std::string &line2,
                        const libsgp4::DateTime &dt,
                        GeoPosition &out);

int FindExitTimeSec(const std::string &line1,
                    const std::string &line2,
                    const EcefPosition &observer,
                    const libsgp4::DateTime &epoch,
                    int startSec,
                    int thetaDeg,
                    int maxLookaheadSec,
                    EcefPosition &nadirAtExitM);

int64_t DateTimeToUnixMillis(const libsgp4::DateTime &dateTime);

libsgp4::DateTime UnixMillisToDateTime(int64_t unixMillis);

double SolveKepler(double meanAnomalyRad, double eccentricity);

EcefPosition PropagateKeplerianToEcef(const KeplerianElementsRaw &orb,
                                      int64_t epochMs,
                                      int64_t tMs);

std::vector<PciScore> PrioritizeTargetSats(int servingPci,
                                           const std::vector<int> &candidatePcis,
                                           const EcefPosition &observerEcef,
                                           int tExitSec,
                                           int elevationMinDeg,
                                           int maxLookaheadSec,
                                           const SatStateResolver &stateResolver,
                                           const EndpointResolver &endpointResolver);

} // namespace nr::rrc::common
