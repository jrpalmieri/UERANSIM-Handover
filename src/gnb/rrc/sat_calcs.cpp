//
// Satellite TLE store and neighborhood cache calculations for the gNB RRC task.
//

#include "task.hpp"

#include <algorithm>
#include <cmath>

//#include <libsgp4/DateTime.h>

#include <lib/sat/sat_state.hpp>
#include <lib/sat/sat_time.hpp>
#include <lib/sat/sat_calc.hpp>
#include <utils/common.hpp>
#include <utils/common_types.hpp>

using nr::sat::SatEcefState;
using nr::sat::EcefPosition;
using nr::sat::SatTleEntry;
using nr::sat::EcefDistance;
using nr::sat::GeoToEcef;
using nr::sat::EcefToGeo;
using nr::sat::ComputeNadir;
using nr::sat::ElevationAngleDeg;


namespace nr::gnb
{

// Maximum number of satellites to keep in the neighborhood (ECEF-distance) cache.
static constexpr size_t SAT_NEIGHBORHOOD_MAX = 50;

// Maximum number of satellites to keep in the SIB19 nadir-proximity cache.
static constexpr size_t SAT_SIB19_MAX = 7;

static constexpr int MAX_LOOKAHEAD_SEC = 7200; // 2-hour horizon


/**
 * @brief Determines trigger values for dynamic handover events (e.g. Event D1) based on the satellite's 
 * propagation and UE's position.  Allows for proactive handover decisions based on satellite location predictions.
 *  * 
 * @param dynTriggerParams 
 * @param ownPci 
 * @param ue 
 */
void GnbRrcTask::satHandoverTriggerCalc(nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams, const int ownPci, RrcUeContext *ue)
{

    // min elevation angle to trigger handover, in degrees (set in config)
    const int theta  = m_config->ntn.elevationMinDeg;

    auto ownTleOpt   = m_base->satStates->getTle(ownPci);

    if (!ownTleOpt.has_value())
    {
        m_logger->warn("UE[%d] Dynamic trigger: own TLE unavailable, using trigger defaults (t=%ds d=%dm)",
                        ue->ueId, dynTriggerParams.condT1_thresholdSec, dynTriggerParams.condD1_distanceThresholdFromReference1);
        return;
    }

    // get current UE position from global store
    std::optional<GeoPosition> uePos = m_base->getUePosition(ue->ueId);

    // validity checks
    //   (could also add a timestamp check to ensure freshness, but not a concern the way this is currently implemented
    //      since all UEs report their positions to all gNBs in heartbeats.))
    if (!uePos.has_value() || !uePos->isValid)
    {
        m_logger->warn("UE[%d] Dynamic trigger: UE position unavailable, using trigger defaults (t=%ds d=%dm)",
                        ue->ueId, dynTriggerParams.condT1_thresholdSec, dynTriggerParams.condD1_distanceThresholdFromReference1);
        return;
    }

    const SatTleEntry &ownTle = ownTleOpt.value();
    
    m_logger->debug("UE[%d]: Calculating Dynamic trigger conditions for NTN using TLE: %s / %s",
                        ue->ueId, ownTle.line1.c_str(), ownTle.line2.c_str());

    // convert ue position from lat/long/alt to ECEF for the calculation
    EcefPosition ueEcef = GeoToEcef(uePos.value());

    // get current sat time
    const int64_t satNowMs = m_base->satTime->CurrentSatTimeMillis();
    const int satNowSec = static_cast<int>(satNowMs / 1000);

    // get the Ecef of the current gnb location
    auto sgp4 = m_base->satStates->getSgp4(ownPci);
    SatEcefState curState{};
    curState.pos = sgp4->FindPositionEcef(satNowMs);
    curState.nadir = ComputeNadir(curState.pos);

    // compute current elevation angle to satellite from UE location
    auto elevAngle = ElevationAngleDeg(ueEcef, curState.pos);
    m_logger->debug("UE[%d]: Current elevation angle to satellite is %.2f deg (threshold is %d deg)",
                    ue->ueId, elevAngle, theta);

    EcefPosition nadirAtExitM{};
    long tExit = 0;

    // If already below the threshold right now, set triggers for right now
    if (elevAngle < static_cast<double>(theta))
    {
        tExit = 0;
        nadirAtExitM = curState.nadir;
    }
    else 
    {
        // find the exit time (t_exit) and the nadir distance at exit (nadirAtExitM) by propagating the 
        //  TLE forward in time until the elevation drops below the threshold
        tExit = nr::sat::FindExitTimeSec(*sgp4, ueEcef, satNowMs, 0, theta, MAX_LOOKAHEAD_SEC, nadirAtExitM);

    }

    // update the Dynamic triggers with the calculated values

    // condT1
    dynTriggerParams.condT1_thresholdSec = satNowSec + tExit;
    dynTriggerParams.condT1_durationSec = 100;

    // condD1 is comparing UE pos to nadir pos at exit, and if over threshold, that means the sat is beyond the elevation angle threshold
    {
        auto refGeo = EcefToGeo(nadirAtExitM);
        dynTriggerParams.condD1_referenceLocation1 = {refGeo.latitude, refGeo.longitude};
    }
    dynTriggerParams.condD1_distanceThresholdFromReference1 = static_cast<int>(EcefDistance(ueEcef, nadirAtExitM));
    // D2 we set to dummy vals that will always be true, since we just care about triggering the handover once the sat is beyond the elevation threshold
    dynTriggerParams.condD1_referenceLocation2 = {uePos->latitude, uePos->longitude};
    dynTriggerParams.condD1_distanceThresholdFromReference2 = 1;
    dynTriggerParams.condD1_hysteresisLocation = 10;
    // copy condD1 values to D1 - caller will choose between them
    dynTriggerParams.d1_distanceThresholdFromReference1 = dynTriggerParams.condD1_distanceThresholdFromReference1;
    dynTriggerParams.d1_referenceLocation1 = dynTriggerParams.condD1_referenceLocation1;
    dynTriggerParams.d1_distanceThresholdFromReference2 = dynTriggerParams.condD1_distanceThresholdFromReference2;
    dynTriggerParams.d1_referenceLocation2 = dynTriggerParams.condD1_referenceLocation2;
    dynTriggerParams.d1_hysteresisLocation = dynTriggerParams.condD1_hysteresisLocation;

    m_logger->debug("UE[%d]: NTN trigger conditions: t_thresh=%d sec (%d sec from sat-now), "
                "d1_ref1=[%.4f,%.4f], d1_thresh1=%d m, d1_ref2=[%.4f,%.4f], d1_thresh2=%d m",
                        ue->ueId, dynTriggerParams.condT1_thresholdSec, tExit,
                        dynTriggerParams.condD1_referenceLocation1.latitudeDeg, dynTriggerParams.condD1_referenceLocation1.longitudeDeg, dynTriggerParams.condD1_distanceThresholdFromReference1,
                        dynTriggerParams.condD1_referenceLocation2.latitudeDeg, dynTriggerParams.condD1_referenceLocation2.longitudeDeg, dynTriggerParams.condD1_distanceThresholdFromReference2);

    return;

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

    // check that there is sat data to process
    std::vector<SatTleEntry> allEntries = m_base->satStates->getAllTles();
    if (allEntries.empty())
        return;

    // Determine this gNB's own PCI
    int ownPci = cons::getPciFromNci(m_config->nci);

    // check t we have our own TLE data to propagate
    auto ownTle = m_base->satStates->getTle(ownPci);
    if (!ownTle.has_value())
    {
        m_logger->warn("roughNeighborhoodSats: own TLE not loaded (pci=%d), skipping", ownPci);
        return;
    }

    // Snapshot current time once so all propagations are coherent.
    const int64_t satNowMs = m_base->satTime->CurrentSatTimeMillis();

    // Propagate own TLE → gNB ECEF position + nadir
    SatEcefState gnbState{};
    gnbState.pos = m_base->satStates->getSgp4(ownPci)->FindPositionEcef(satNowMs);
    gnbState.nadir = ComputeNadir(gnbState.pos);

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
        // skip ourselves
        if (entry.pci == ownPci)
            continue;

        SatEcefState state{};
        state.pos = m_base->satStates->getSgp4(entry.pci)->FindPositionEcef(satNowMs);
        state.nadir = ComputeNadir(state.pos);

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
