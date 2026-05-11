//
// Thread-safe store for satellite TLE entries, keyed by NCI.
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

    /// Return the entry for the given NCI, or nullopt if not present.
    std::optional<SatTleEntry> getTle(int64_t nci) const;
    
    /// Return a snapshot of all entries.
    std::vector<SatTleEntry> getAllTles() const;

    /// Number of stored entries.
    size_t size() const;

    /// Get reference to the SGP4 object for the given NCI, or nullptr if not present.
    std::shared_ptr<Propagator> getSgp4(int64_t nci) const;

    /// clear the store
    void clear();

    /// Prioritizes target satellites based on their transit times.
    std::vector<SatPriorityScore> PrioritizeTargetSats(const std::vector<int64_t> &candidateNcis, const EcefPosition &observerEcef, int64_t tStartSec,
          int elevationMinDeg, int maxLookaheadSec);

  private:
    mutable std::mutex m_mutex;
    /// TLEs keyed by NCI.
    std::unordered_map<int64_t, SatTleEntry> m_store;
    // SGP4 propagation objects keyed by NCI.
    std::unordered_map<int64_t, std::shared_ptr<Propagator>> m_sgp4;
};

} // namespace nr::sat
