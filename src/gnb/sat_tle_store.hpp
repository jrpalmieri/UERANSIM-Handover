//
// Thread-safe store for satellite TLE entries, keyed by PCI.
// Shared across gNB tasks (RRC writes, RLS reads).
//

#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <gnb/types.hpp>

namespace nr::gnb
{

class SatTleStore
{
  public:
    /// Insert or update the entry for entry.pci.
    void upsert(const SatTleEntry &entry);

    /// Bulk upsert — calls upsert() for each entry.
    void upsertAll(const std::vector<SatTleEntry> &entries);

    /// Return the entry for the given PCI, or nullopt if not present.
    std::optional<SatTleEntry> find(int pci) const;

    /// Return a snapshot of all entries.
    std::vector<SatTleEntry> getAll() const;

    /// Number of stored entries.
    size_t size() const;

  private:
    mutable std::mutex m_mutex;
    std::unordered_map<int, SatTleEntry> m_store;
};

} // namespace nr::gnb
