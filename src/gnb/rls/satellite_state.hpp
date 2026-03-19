//
// Shared satellite state for the gNB satellite simulation.
// Thread-safe: accessed by the UDP receive path (writes on
// SATELLITE_POSITION_UPDATE) and by the sat-position task
// (periodic propagation) and the heartbeat handler (reads).
//

#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <utils/common_types.hpp>

namespace nr::gnb
{

/// Orbital elements extracted from a TLE (simplified).
struct OrbitalElements
{
    double inclination{};      // radians
    double raan{};             // right ascension of ascending node
    double eccentricity{};
    double argPerigee{};       // argument of perigee (rad)
    double meanAnomaly{};      // at epoch (rad)
    double meanMotion{};       // revolutions per day
    int64_t epochMs{};         // Unix epoch millis of TLE
};

/// Parse Two-Line Element set into OrbitalElements.
/// Returns false on malformed input.
bool ParseTle(const std::string &line1,
              const std::string &line2,
              OrbitalElements &out);

/// Propagate orbital elements forward by `dtMs` milliseconds
/// using a simplified Keplerian model (no drag/J2).
/// Returns the satellite ECEF position.
EcefPosition PropagateSatellite(const OrbitalElements &elems,
                                int64_t dtMs);

/// Thread-safe container holding the latest satellite state.
class SatelliteState
{
  public:
    /// Update orbital elements (called on SPU receipt).
    void updateTle(const OrbitalElements &elems);

    /// Read the current orbital elements and whether we have
    /// a valid TLE.
    bool getOrbitalElements(OrbitalElements &out) const;

    /// Set the latest computed ECEF position.
    void setPosition(const EcefPosition &pos, int64_t timeMs);

    /// Get the latest ECEF position + its timestamp.
    /// Returns false if no position has been computed yet.
    bool getPosition(EcefPosition &pos, int64_t &timeMs) const;

  private:
    mutable std::mutex m_mutex;
    bool m_hasTle{false};
    OrbitalElements m_elems{};
    bool m_hasPosition{false};
    EcefPosition m_position{};
    int64_t m_posTimeMs{};
};

} // namespace nr::gnb
