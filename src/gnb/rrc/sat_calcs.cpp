//
// Satellite TLE store and neighborhood cache calculations for the gNB RRC task.
//

#include "task.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <libsgp4/DateTime.h>
#include <libsgp4/Eci.h>
#include <libsgp4/SGP4.h>
#include <libsgp4/Tle.h>

#include <gnb/sat_tle_store.hpp>
#include <gnb/sat_time.hpp>
#include <utils/common.hpp>
#include <utils/common_types.hpp>
#include <utils/position_calcs.hpp>

namespace nr::gnb
{

// Maximum number of satellites to keep in the neighborhood (ECEF-distance) cache.
static constexpr size_t SAT_NEIGHBORHOOD_MAX = 50;

// Maximum number of satellites to keep in the SIB19 nadir-proximity cache.
static constexpr size_t SAT_SIB19_MAX = 7;

// ---------------------------------------------------------------------------
// Propagated state: ECEF position and sub-satellite nadir point (altitude=0).
// Both in meters.
// ---------------------------------------------------------------------------
struct SatEcefState
{
    EcefPosition pos{};    ///< Satellite ECEF position (altitude included)
    EcefPosition nadir{};  ///< Ground-track nadir (altitude = 0)
};

// ---------------------------------------------------------------------------
// Propagate a single TLE entry at the given DateTime.
// Fills both the satellite ECEF position and its nadir point.
// Returns false (silently) on any libsgp4 exception.
// ---------------------------------------------------------------------------
static bool PropagateFull(const SatTleEntry &entry,
                          const libsgp4::DateTime &dt,
                          SatEcefState &out)
{
    try
    {
        libsgp4::Tle tle(entry.line1, entry.line2);
        libsgp4::SGP4 sgp4(tle);
        libsgp4::Eci eci = sgp4.FindPosition(dt);
        libsgp4::CoordGeodetic geo = eci.ToGeodetic();

        double latDeg = geo.latitude  * (180.0 / M_PI);
        double lonDeg = geo.longitude * (180.0 / M_PI);
        double altM   = geo.altitude  * 1000.0;

        out.pos = GeoToEcef(GeoPosition{latDeg, lonDeg, altM});
        out.nadir = ComputeNadir(out.pos);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Public: upsert TLE entries by PCI.
// ---------------------------------------------------------------------------
void GnbRrcTask::upsertSatTles(const std::vector<SatTleEntry> &entries)
{
    int64_t now = static_cast<int64_t>(utils::CurrentTimeMillis());
    std::vector<SatTleEntry> stamped;
    stamped.reserve(entries.size());
    for (const auto &entry : entries)
    {
        SatTleEntry stored = entry;
        stored.lastUpdatedMs = now;
        stamped.push_back(std::move(stored));
    }
    m_base->satTleStore->upsertAll(stamped);
    m_logger->info("TLE store upserted %zu entries, total %zu satellites",
                   entries.size(), m_base->satTleStore->size());
}

// ---------------------------------------------------------------------------
// Private: compute the nearest satellite caches and refresh both
// m_satNeighborhoodCache (50 nearest by 3-D ECEF distance) and
// m_sib19RangeCache (7 nearest to the gNB's own nadir, selected from the 50).
//
// Step 1 – The gNB's own TLE (keyed by own PCI) is propagated to obtain the
//           gNB's current ECEF position and nadir (sub-satellite ground point).
// Step 2 – All other TLEs are propagated at the same instant.  For each, both
//           the 3-D ECEF distance (used for the 50-neighbor cache) and the
//           nadir-to-nadir distance (used for the SIB19 coverage cache) are
//           computed.
// Step 3 – Partial-sort by ECEF distance → m_satNeighborhoodCache (≤ 50 PCIs).
// Step 4 – Within those 50, partial-sort by nadir distance → m_sib19RangeCache
//           (≤ 7 PCIs).  These are the satellites most likely to have overlapping
//           coverage footprints with the gNB, making them relevant for SIB19.
// ---------------------------------------------------------------------------
void GnbRrcTask::roughNeighborhoodSats()
{
    std::vector<SatTleEntry> allEntries = m_base->satTleStore->getAll();
    if (allEntries.empty())
        return;

    // Determine this gNB's own PCI
    int ownPci = cons::getPciFromNci(m_config->nci);

    auto ownTle = m_base->satTleStore->find(ownPci);
    if (!ownTle.has_value())
    {
        m_logger->warn("roughNeighborhoodSats: own TLE not loaded (pci=%d), skipping", ownPci);
        return;
    }

    // Snapshot current time once so all propagations are coherent.
    libsgp4::DateTime now = sat_time::Now();

    // Propagate own TLE → gNB ECEF position + nadir
    SatEcefState gnbState{};
    if (!PropagateFull(*ownTle, now, gnbState))
    {
        m_logger->warn("roughNeighborhoodSats: failed to propagate own TLE (pci=%d), skipping", ownPci);
        return;
    }

    // Compute per-satellite ECEF and nadir distances
    struct SatInfo
    {
        int pci{};
        double ecefDistM{};   // 3-D distance to gNB (for neighborhood cache)
        double nadirDistM{};  // ground-track nadir distance (for SIB19 cache)
    };
    std::vector<SatInfo> infos;
    infos.reserve(allEntries.size());

    for (const auto &entry : allEntries)
    {
        if (entry.pci == ownPci)
            continue;

        SatEcefState state{};
        if (!PropagateFull(entry, now, state))
        {
            m_logger->debug("roughNeighborhoodSats: skipping pci=%d (propagation failed)", entry.pci);
            continue;
        }

        SatInfo si{};
        si.pci       = entry.pci;
        si.ecefDistM  = EcefDistance(gnbState.pos,   state.pos);
        si.nadirDistM = EcefDistance(gnbState.nadir, state.nadir);
        infos.push_back(si);
    }

    // --- Step 3: top-50 by ECEF distance → m_satNeighborhoodCache ---
    size_t neighborCount = std::min(infos.size(), SAT_NEIGHBORHOOD_MAX);
    std::partial_sort(infos.begin(),
                      infos.begin() + static_cast<ptrdiff_t>(neighborCount),
                      infos.end(),
                      [](const SatInfo &a, const SatInfo &b) {
                          return a.ecefDistM < b.ecefDistM;
                      });

    m_satNeighborhoodCache.clear();
    m_satNeighborhoodCache.reserve(neighborCount);
    for (size_t i = 0; i < neighborCount; ++i)
        m_satNeighborhoodCache.push_back(infos[i].pci);

    // --- Step 4: top-7 by nadir distance (within the 50) → m_sib19RangeCache ---
    size_t sib19Count = std::min(neighborCount, SAT_SIB19_MAX);
    std::partial_sort(infos.begin(),
                      infos.begin() + static_cast<ptrdiff_t>(sib19Count),
                      infos.begin() + static_cast<ptrdiff_t>(neighborCount),
                      [](const SatInfo &a, const SatInfo &b) {
                          return a.nadirDistM < b.nadirDistM;
                      });

    m_sib19RangeCache.clear();
    m_sib19RangeCache.reserve(sib19Count);
    for (size_t i = 0; i < sib19Count; ++i)
        m_sib19RangeCache.push_back(infos[i].pci);

    m_logger->debug("roughNeighborhoodSats: %zu neighbors cached, %zu sib19-range sats "
                    "(own pci=%d, total tles=%zu)",
                    neighborCount, sib19Count, ownPci, allEntries.size());
}

} // namespace nr::gnb
