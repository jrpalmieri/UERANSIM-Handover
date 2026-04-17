//
// SatTleStore implementation.
//

#include "sat_tle_store.hpp"

namespace nr::gnb
{

void SatTleStore::upsert(const SatTleEntry &entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_store[entry.pci] = entry;
}

void SatTleStore::upsertAll(const std::vector<SatTleEntry> &entries)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &entry : entries)
        m_store[entry.pci] = entry;
}

std::optional<SatTleEntry> SatTleStore::find(int pci) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_store.find(pci);
    if (it == m_store.end())
        return std::nullopt;
    return it->second;
}

std::vector<SatTleEntry> SatTleStore::getAll() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SatTleEntry> result;
    result.reserve(m_store.size());
    for (const auto &[pci, entry] : m_store)
        result.push_back(entry);
    return result;
}

size_t SatTleStore::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_store.size();
}

} // namespace nr::gnb
