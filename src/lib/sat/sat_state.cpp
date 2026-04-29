//
// SatTleStore implementation.
//

#include "sat_state.hpp"
#include "sat_calc.hpp"
#include "sgp4.hpp"
#include <utils/common.hpp>

namespace nr::sat
{

void SatStates::upsert(const SatTleEntry &entry)
{
    int64_t now = static_cast<int64_t>(utils::CurrentTimeMillis());
    upsert(entry, now);
}

void SatStates::upsert(const SatTleEntry &entry, int64_t timestampMs)
{
    SatTleEntry stored = entry;
    stored.lastUpdatedMs = timestampMs;
    auto sgp4 = std::make_shared<Propagator>(stored);

    upsert(stored, std::move(sgp4));
}

void SatStates::upsert(const SatTleEntry &entry, std::shared_ptr<Propagator> sgp4)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_store[entry.pci] = entry;
    // free old SGP4 object if exists (to release memory), then store the new one
    auto it = m_sgp4.find(entry.pci);
    if (it != m_sgp4.end())
        m_sgp4.erase(it);
    m_sgp4[entry.pci] = std::move(sgp4);
}

void SatStates::upsertAll(const std::vector<SatTleEntry> &entries)
{
    int64_t now = static_cast<int64_t>(utils::CurrentTimeMillis());
    upsertAll(entries, now);
}

void SatStates::upsertAll(const std::vector<SatTleEntry> &entries, int64_t timestampMs)
{
    for (const auto &entry : entries)
    {
        SatTleEntry stored = entry;
        stored.lastUpdatedMs = timestampMs;
        auto sgp4 = std::make_shared<Propagator>(stored);

        upsert(stored, std::move(sgp4));
    }
}

std::optional<SatTleEntry> SatStates::getTle(int pci) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_store.find(pci);
    if (it == m_store.end())
        return std::nullopt;
    return it->second;
}

std::vector<SatTleEntry> SatStates::getAllTles() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SatTleEntry> result;
    result.reserve(m_store.size());
    for (const auto &[pci, entry] : m_store)
        result.push_back(entry);
    return result;
}

size_t SatStates::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_store.size();
}

std::shared_ptr<Propagator> SatStates::getSgp4(int pci) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sgp4.find(pci);
    if (it == m_sgp4.end())
        return nullptr;
    return it->second;
}


void SatStates::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_store.clear();
    m_sgp4.clear();
}


/**
 * @brief Prioritizes target satellites based on their transit times.
 * Caller provides a start time (tStartSec) as the start time for running propagations, and list of
 * satellites (candidate PCIs) to propagate.  
 * 
 * Transit times are calculated as the amount of time a satellite spends above an elevation angle threshold
 * (elevationMinDeg).  The angle is relative to a ground observer (observerEcef).  The propagation runs can
 * be time-limited using the maxLookaheadSec parameter, which is the maximum amount of time to look ahead 
 * from tStartSec when calculating transit times (maxLookaheadSec=0 means no limit).
 * 
 * Returns a vector of (PCI, score) pairs, sorted by score with the best candidate first.
 * The score is the transit time in seconds (higher is better), which is a proxy for the highest reached
 * elevation angle.
 * 
 * Other scoring methods may also be used where constellations with different altitudes are involved, 
 * but transit time is a simple and effective method for constellations of similar altitudes. 
 * 
 * @param candidatePcis 
 * @param observerEcef 
 * @param tStartSec 
 * @param elevationMinDeg 
 * @param maxLookaheadSec 
 * @return std::vector<SatPriorityScore> 
 */
std::vector<SatPriorityScore> SatStates::PrioritizeTargetSats(
                                           const std::vector<int> &candidatePcis,
                                           const EcefPosition &observerEcef,
                                           int64_t tStartSec,
                                           int elevationMinDeg,
                                           int maxLookaheadSec)
{
    std::vector<SatPriorityScore> prioritized{};

    // sanity checks on input parameters
    const bool hasUePos = (observerEcef.x != 0.0 || observerEcef.y != 0.0 || observerEcef.z != 0.0);
    if (!hasUePos)
        return prioritized;

    if (candidatePcis.empty())
        return prioritized;

    if (maxLookaheadSec < 0)
        return prioritized;

    for (int pci : candidatePcis)
    {
        auto sgp4 = getSgp4(pci);

        // see if this sat is within the elevation angle threshold at the 
        //   serving sat's exit time.  if not, skip this candidate.
        SatEcefState stateAtExit{};
        stateAtExit.pos = sgp4->FindPositionEcef(tStartSec * 1000);
        if (ElevationAngleDeg(observerEcef, stateAtExit.pos) < static_cast<double>(elevationMinDeg))
            continue;

        // for in-range candidates, find their exit time and compute a score based on transit time (longer is better)
        EcefPosition nadirDummy{};
        const int tNeighborExit = FindExitTimeSec(*sgp4,
                                                    observerEcef,
                                                    tStartSec,
                                                    0,
                                                    elevationMinDeg,
                                                    maxLookaheadSec,
                                                    nadirDummy);

        const int transitSec = tNeighborExit - tStartSec;
        prioritized.emplace_back(pci, transitSec);
    }

    std::sort(prioritized.begin(), prioritized.end(), [](const SatPriorityScore &a, const SatPriorityScore &b) {
        if (a.score == b.score)
            return a.pci < b.pci;
        return a.score > b.score;
    });

    return prioritized;
}


} // namespace nr::sat
