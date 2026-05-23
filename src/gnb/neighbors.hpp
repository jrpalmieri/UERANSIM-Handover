//
//  Neighbors class - manages gNB neighbor information
//
// Provides methods to read initial neighbor info from YAML config, and dynamically update info
// during runtime in a thread-safe manner.
//
// Used in handover procedures.
//

#pragma once

#include <string>
#include <vector>

#include <gnb/types.hpp>

namespace YAML
{
class Node;
}

namespace nr::gnb
{

EHandoverInterface ReadNeighborHandoverInterface(const YAML::Node &node, const std::string &fieldPath);

GnbNeighborState ReadNeighborConfig(const YAML::Node &neighborNode,
                                     bool fullRecordRequired,
                                     const std::string &entryPath);

void ValidateUniqueNeighborNci(const std::vector<GnbNeighborState> &neighbors, const std::string &listPath);



class GnbNeighbors
{
  public:

    /// Number of stored entries.
    size_t size() const;

    /// Return the entry for the given NCI, or nullopt if not present.
    std::optional<GnbNeighborState> findByNci(int64_t nci) const;

    /// Insert or update the entry matching entry.getNci().
    void upsert(const GnbNeighborState &entry);

    /// Bulk upsert — upsert() for each entry under a single lock.
    void upsertAll(const std::vector<GnbNeighborState> &entries);

    /// Replace the entire neighbor list atomically.
    void replaceAll(const std::vector<GnbNeighborState> &entries);

    /// Remove the entry with the given NCI (no-op if not present).
    void remove(int64_t nci);

    /// Return a snapshot of all entries.
    std::vector<GnbNeighborState> getAll() const;

  private:
    // Must be called with m_mutex already held.
    void upsertLocked(const GnbNeighborState &entry);

    mutable std::mutex m_mutex;
    std::vector<GnbNeighborState> m_neighbors;
};




} // namespace nr::gnb
