//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
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

GnbNeighborConfig ReadNeighborConfig(const YAML::Node &neighborNode,
                                     bool fullRecordRequired,
                                     const std::string &entryPath);

void ValidateUniqueNeighborPci(const std::vector<GnbNeighborConfig> &neighbors, const std::string &listPath);



class GnbNeighbors
{
  public:

    /// Number of stored entries.
    size_t size() const;

    /// Return the entry for the given PCI, or nullopt if not present.
    std::optional<GnbNeighborConfig> findByPci(int pci) const;

    /// Return the entry for the given NCI, or nullopt if not present.
    std::optional<GnbNeighborConfig> findByNci(int64_t nci) const;

    /// Insert or update the entry matching entry.getPci().
    void upsert(const GnbNeighborConfig &entry);

    /// Bulk upsert — upsert() for each entry under a single lock.
    void upsertAll(const std::vector<GnbNeighborConfig> &entries);

    /// Replace the entire neighbor list atomically.
    void replaceAll(const std::vector<GnbNeighborConfig> &entries);

    /// Remove the entry with the given PCI (no-op if not present).
    void remove(int pci);

    /// Return a snapshot of all entries.
    std::vector<GnbNeighborConfig> getAll() const;

  private:
    // Must be called with m_mutex already held.
    void upsertLocked(const GnbNeighborConfig &entry);

    mutable std::mutex m_mutex;
    std::vector<GnbNeighborConfig> m_neighbors;
};




} // namespace nr::gnb
