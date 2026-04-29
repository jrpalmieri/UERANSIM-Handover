//
// Thread-safe store for satellite TLE entries, keyed by PCI.
// Shared across gNB tasks (RRC writes, RLS reads).
//

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>

#include "sat_base.hpp"
#include "sgp4.hpp"

namespace nr::sat
{

class SatStates
{
  public:

    /// Upserts add one or more TLE entries and create corresponding SGP4 objects for propagation.

    void upsertAll(const std::vector<SatTleEntry> &entries);
    void upsertAll(const std::vector<SatTleEntry> &entries, int64_t timestampMs);
    void upsert(const SatTleEntry &entry);
    void upsert(const SatTleEntry &entry, int64_t timestampMs);

  private:
    void upsert(const SatTleEntry &entry, std::shared_ptr<Propagator> sgp4);

  public:

    /// Return the entry for the given PCI, or nullopt if not present.
    std::optional<SatTleEntry> getTle(int pci) const;
    
    /// Return a snapshot of all entries.
    std::vector<SatTleEntry> getAllTles() const;

    /// Number of stored entries.
    size_t size() const;

    /// Get reference to the SGP4 object for the given PCI, or nullptr if not present.
    std::shared_ptr<Propagator> getSgp4(int pci) const;

    /// clear the store
    void clear();

    /// Prioritizes target satellites based on their transit times.
    std::vector<SatPriorityScore> PrioritizeTargetSats(const std::vector<int> &candidatePcis, const EcefPosition &observerEcef, int64_t tStartSec,
          int elevationMinDeg, int maxLookaheadSec);

  private:
    mutable std::mutex m_mutex;
    // TLEs keyed by PCI.
    std::unordered_map<int, SatTleEntry> m_store;
    // SGP4 propagation objects keyed by PCI.
    std::unordered_map<int, std::shared_ptr<Propagator>> m_sgp4;
};

} // namespace nr::sat
