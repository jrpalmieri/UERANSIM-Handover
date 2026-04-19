//
// Phase 4 – gNB-side handover support.
//
// Implements:
//   1. receiveRrcReconfigurationComplete()  – handle UE's HO completion on target gNB
//   2. receiveMeasurementReport()           – parse and act on UE measurement reports
//   3. sendHandoverCommand()                – build RRCReconfiguration with
//                                             ReconfigurationWithSync and send to UE
//   4. handleHandoverComplete()             – post-handover processing (logging, NGAP notify)
//   5. sendMeasConfig()                     – send measurement configuration to UE
//   6. evaluateHandoverDecision()           – decide whether to initiate handover
//   7. handleNgapHandoverCommand()          – forward NGAP Handover Command (RRC container) to UE
//

#include "task.hpp"

#include <gnb/neighbors.hpp>
#include <gnb/ngap/task.hpp>
#include <gnb/sat_time.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <utils/common.hpp>
#include <utils/position_calcs.hpp>
#include <algorithm>
#include <asn/rrc/ASN_RRC_MeasurementReport.h>
#include <asn/rrc/ASN_RRC_MeasurementReport-IEs.h>
#include <asn/rrc/ASN_RRC_MeasResults.h>
#include <asn/rrc/ASN_RRC_MeasResultNR.h>
#include <asn/rrc/ASN_RRC_MeasResultListNR.h>
#include <asn/rrc/ASN_RRC_MeasResultServMO.h>
#include <asn/rrc/ASN_RRC_MeasResultServMOList.h>
#include <asn/rrc/ASN_RRC_MeasQuantityResults.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1530-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1540-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1560-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1610-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1700-IEs.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_SpCellConfig.h>
#include <asn/rrc/ASN_RRC_ReconfigurationWithSync.h>
#include <asn/rrc/ASN_RRC_ServingCellConfigCommon.h>
#include <asn/rrc/ASN_RRC_ConditionalReconfiguration.h>
#include <asn/rrc/ASN_RRC_CondReconfigToAddMod.h>
#include <asn/rrc/ASN_RRC_CondTriggerConfig-r16.h>
#include <asn/rrc/ASN_RRC_NTN-TriggerConfig-r17.h>
#include <asn/rrc/ASN_RRC_DL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_DL-DCCH-MessageType.h>
#include <asn/rrc/ASN_RRC_MeasConfig.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddModList.h>
#include <asn/rrc/ASN_RRC_MeasObjectNR.h>
#include <asn/rrc/ASN_RRC_ReportConfigToAddMod.h>
#include <asn/rrc/ASN_RRC_ReportConfigToAddModList.h>
#include <asn/rrc/ASN_RRC_ReportConfigNR.h>
#include <asn/rrc/ASN_RRC_EventTriggerConfig.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddModList.h>
#include <asn/rrc/ASN_RRC_SSB-MTC.h>
#include <asn/rrc/ASN_RRC_SubcarrierSpacing.h>
#include <asn/rrc/ASN_RRC_MeasTriggerQuantityOffset.h>
#include <cmath>
#include <utility>

#include <libsgp4/DateTime.h>
#include <libsgp4/Eci.h>
#include <libsgp4/SGP4.h>
#include <libsgp4/Tle.h>
#include <gnb/sat_tle_store.hpp>

const int MIN_RSRP = cons::MIN_RSRP; // minimum RSRP value (in dBm) to use when no measurement is available
const int HANDOVER_TIMEOUT_MS = 5000; // time to wait for handover completion before considering it failed
const long DUMMY_MEAS_OBJECT_ID = 1; // dummy MeasObjectId for handover measurement configuration

static constexpr int NTN_DEFAULT_T_SERVICE_SEC = 300;
static constexpr int MIN_COND_RECONFIG_ID = 1;
static constexpr int MAX_COND_RECONFIG_ID = 8;


namespace nr::gnb
{

using HandoverEventType = nr::rrc::common::HandoverEventType;

/* ================================================================== */
/*  ASN value conversion helpers                                      */
/* ================================================================== */

// // convert from dB to the ASN MeasTriggerQuantityOffset
// //      which is an integer representing 0.5 dB steps, 
// //      with valid range of 0..30 (i.e. 0..15 dB)
// static long mtqOffsetToASNValue(int offsetDb)
// {

//     int val = offsetDb * 2;
//     if (val < 0)
//         val = 0;
//     if (val > 30)
//         val = 30;
//     return static_cast<long>(val);
// }

// // convert from ASN MeasTriggerQuantityOffset back to dB
// static int mtqOffsetFromASNValue(long val)
// {
//     return static_cast<int>(val) / 2;
// }

// // // convert a Measure Trigger Quantity (generally an RSRP) in dBm to the ASN MTQ
// // //      which is an integer representing 1 dBm starting at -156dBm
// // //      with valid range of 0..127  (i.e. -156..-29 dBm)
// // static long mtqToASNValue(int dbm)
// // {
// //     int val = dbm + 156;
// //     if (val < 0)
// //         val = 0;
// //     if (val > 127)
// //         val = 127;
// //     return static_cast<long>(val);
// // }

// // // convert an ASN MTQ value back to dBm
// // static int mtqFromASNValue(long val)
// // {
// //     return static_cast<int>(val) - 156;
    
// // }

// // convert a hysteresis value to an ASN Hysteresis 
// //      which is an integer representing 0.5 dB steps, 
// //      with valid range of 0..30 (i.e. 0..15 dB)
// static long hysteresisToASNValue(int db)
// {
//     int val = db * 2;
//     if (val < 0)
//         val = 0;
//     if (val > 30)
//         val = 30;
//     return static_cast<long>(val);
// }

// // convert an ASN Hysteresis value back to dB
// static int hysteresisFromASNValue(long val)
// {
//     return static_cast<int>(val) / 2;
// }


// // Convert an E_TTT_ms enum to the corresponding ASN_RRC_TimeToTrigger enum value
// static long tttMsToASNValue(nr::rrc::common::E_TTT_ms tttMs)
// {

//     // the enum now matches the ASN values directly, so we can just static_cast it
//     return static_cast<long>(tttMs);
// }


// // convert a hysteresis location in meters to an ASN HysteresisLocation
// //      which is an integer representing 10m steps, 
// //      with valid range of 0..32768 (i.e. 0..327680 m)
// static long hysteresisLocationToASNValue(int hysteresisLocation)
// {
//     int val = hysteresisLocation / 10;
//     if (val < 0)
//         val = 0;
//     if (val > 32768)
//         val = 32768;
//     return static_cast<long>(val);
// }

// // convert an ASN HysteresisLocation back to meters
// static int hysteresisLocationFromASNValue(long val)
// {
//     return static_cast<int>(val) * 10;
// }

// // converts a distanceThreshold in meters to the ASN Value,
// //      which is an integer representing 50m steps,
// //      with valid range of 0..65525 (i.e. 0..3,276,250 m)
// static long distanceThresholdToASNValue(int distanceM)
// {
//     int val = distanceM / 50;
//     if (val < 0)
//         val = 0;
//     if (val > 65525)
//         val = 65525;
//     return static_cast<long>(val);
// }

// // convert an ASN distanceThreshold value back to meters
// static int distanceThresholdFromASNValue(long val)
// {
//     return static_cast<int>(val) * 50;
// }

// // converts a T1 duration in second to the ASN Value, 
// //      which is an integer representing 100ms steps, 
// //      with valid range of 0..6000 (i.e. 0..600s)
// static long durationToASNValue(int durationSec)
// {
//     int val = durationSec * 10; // convert from seconds to 100ms steps
//     if (val < 1)
//         val = 1;
//     if (val > 6000)
//         val = 6000;
//     return static_cast<long>(val);
// }

// // converts an ASN t1 duration value back to seconds
// static int durationFromASNValue(long val)
// {
//     return static_cast<int>(val) / 10;
// }

// // converts a T1 threshold in ms to the ASN Value
// //      which is an integer representing 10ms steps,
// //      with valid range of 0..549755813887 (i.e. 0..5497558138870 ms)
// //      Note: starting point is January 1, 1900, 00:00:00 UTC (NTP Epoch),
// //          so this is an absolute timestamp rather than a relative duration.
// //
// //      This function will convert the unix epoch-based value to the 
// //          NTP Epoch value, then apply the unit scaling.
// static long t1ThresholdToASNValue(uint64_t thresholdSec)
// {
//     uint64_t val = thresholdSec + NTP_EPOCH_TO_UNIX_EPOCH; // convert from unix epoch to NTP epoch
//     val = val * 100; // convert from seconds to 10ms steps
//     if (val > 549755813887)
//         val = 549755813887;
//     return static_cast<long>(val);
// }

// // convert an ASN T1 threshold value back to seconds.
// //    also converts from NTP epoch back to unix epoch by subtracting the offset
// static uint64_t t1ThresholdFromASNValue(long val)
// {
//     return (static_cast<uint64_t>(val) / 100) - NTP_EPOCH_TO_UNIX_EPOCH;
// }

// static long t304MsToEnum(int ms)
// {
//     switch (ms)
//     {
//     case 50:    return 0;
//     case 100:   return 1;
//     case 150:   return 2;
//     case 200:   return 3;
//     case 500:   return 4;
//     case 1000:  return 5;
//     case 2000:  return 6;
//     case 10000: return 7;
//     default:    return 5; // default ms1000
//     }
// }



/* ====================================== */
/*      Helper functions                  */
/* ====================================== */


static int calcLatLongDistance(const nr::rrc::common::EventReferenceLocation &pos1,
                        const nr::rrc::common::EventReferenceLocation &pos2)
{
    GeoPosition a{pos1.latitudeDeg, pos1.longitudeDeg, 0.0};
    GeoPosition b{pos2.latitudeDeg, pos2.longitudeDeg, 0.0};
    return static_cast<int>(std::round(HaversineDistanceMeters(a, b)));
}


static bool allocateUniqueCondReconfigId(RrcUeContext *ue, int &outId)
{
    if (ue == nullptr)
        return false;

    for (int id = MIN_COND_RECONFIG_ID; id <= MAX_COND_RECONFIG_ID; id++)
    {
        if (ue->usedCondReconfigIds.count(id) == 0)
        {
            ue->usedCondReconfigIds.insert(id);
            outId = id;
            return true;
        }
    }

    return false;
}

// Normalize the C-RNTI to fit within 16 bits for RRC message encoding.
static int normalizeCrntiForRrc(int crnti)
{
    int normalized = crnti & 0xFFFF;
    if (normalized == 0)
        normalized = 1;
    return normalized;
}

// returns true for event types that are stored under the "eventTrigger" category in the ASN structures
//   and are enabled for use
bool isEventTriggerEventType(HandoverEventType eventType)
{
    return nr::rrc::common::IsMeasurementEvent(eventType);
}

// returns true for event types that are stored under the "condEventTrigger" category in the ASN structures
//   and are enabled for use
bool isCondTriggerConfigEventType(HandoverEventType eventType)
{
    return nr::rrc::common::IsConditionalEvent(eventType);
}



/* ================================================================== */
/*  Satellite geometry helpers for conditional-handover trigger calcs */
/* ================================================================== */

/// Satellite ECEF position and its ground-track nadir (altitude = 0), both in meters.
struct SatPvEcef
{
    EcefPosition pos{};
    EcefPosition nadir{};
};

/// Propagate a TLE at a given DateTime and fill pos/nadir.
/// Returns false silently on any libsgp4 exception.
static bool PropagateToEcef(const SatTleEntry &entry,
                             const libsgp4::DateTime &dt,
                             SatPvEcef &out)
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
    catch (...)
    {
        return false;
    }
}

/// Scan forward from `startSec` seconds past `epoch` until the satellite's
/// elevation from `obs` drops below `thetaDeg`.  Uses a 10-second coarse step
/// followed by a 1-second binary search.
///
/// @param tleEntry      TLE of the satellite to track
/// @param obs           Observer ECEF position (meters)
/// @param epoch         Reference DateTime (typically "now")
/// @param startSec      Start offset in seconds from epoch (0 = now)
/// @param thetaDeg      Exit-angle threshold (degrees)
/// @param maxLookaheadSec  Search window cap (seconds past startSec)
/// @param[out] nadirDistM  ECEF distance from obs to satellite nadir at exit
/// @return              Seconds from epoch at which elevation falls below thetaDeg
static int FindExitTimeSec(const SatTleEntry &tleEntry,
                            const EcefPosition &obs,
                            const libsgp4::DateTime &epoch,
                            int startSec,
                            int thetaDeg,
                            int maxLookaheadSec,
                            double &nadirDistM)
{
    static constexpr int STEP_SEC = 10;

    nadirDistM = 0.0;
    int tLow   = startSec;
    int tHigh  = startSec;
    bool found = false;

    for (int t = startSec + STEP_SEC; t <= startSec + maxLookaheadSec; t += STEP_SEC)
    {
        SatPvEcef st{};
        if (!PropagateToEcef(tleEntry, epoch.AddSeconds(t), st))
            continue;
        if (ElevationAngleDeg(obs, st.pos) < static_cast<double>(thetaDeg))
        {
            tLow  = t - STEP_SEC;
            tHigh = t;
            found = true;
            break;
        }
    }

    if (!found)
    {
        // Satellite remains above threshold throughout the lookahead window
        int tEnd = startSec + maxLookaheadSec;
        SatPvEcef st{};
        if (PropagateToEcef(tleEntry, epoch.AddSeconds(tEnd), st))
            nadirDistM = EcefDistance(obs, st.nadir);
        return tEnd;
    }

    // Binary-search to narrow down to ±1 second
    while (tHigh - tLow > 1)
    {
        int tMid = (tLow + tHigh) / 2;
        SatPvEcef st{};
        if (!PropagateToEcef(tleEntry, epoch.AddSeconds(tMid), st))
        {
            tLow = tMid;
            continue;
        }
        if (ElevationAngleDeg(obs, st.pos) >= static_cast<double>(thetaDeg))
            tLow = tMid;
        else
            tHigh = tMid;
    }

    // Record nadir distance at the exit instant
    SatPvEcef exitSt{};
    if (PropagateToEcef(tleEntry, epoch.AddSeconds(tHigh), exitSt))
        nadirDistM = EcefDistance(obs, exitSt.nadir);

    return tHigh;
}

/**
 * @brief Compute the conditional-handover trigger conditions for the serving gNB.
 *
 * Propagates the gNB's own TLE forward in time until its elevation angle
 * (as seen from the UE) drops below thetaExitDeg.  The crossing time
 * (T_exit) is returned as triggerTimerSec; the UE-to-nadir distance at
 * that instant is returned as distanceThresholdM (useful as a D1 trigger
 * distance threshold).
 *
 * @param ownTle           TLE of the serving (own) satellite gNB
 * @param ueEcef           ECEF position of the UE (meters)
 * @param thetaExitDeg     Minimum elevation angle threshold (integer degrees)
 * @param[out] triggerTimerSec      Seconds until the gNB exits coverage (T_exit)
 * @param[out] distanceThresholdM   Nadir distance (meters) from UE at T_exit
 */
static void calculateTriggerConditions(const SatTleEntry &ownTle,
                                        const EcefPosition &ueEcef,
                                        int thetaExitDeg,
                                        int &triggerTimerSec,
                                        int &distanceThresholdM)
{
    static constexpr int MAX_LOOKAHEAD_SEC = 7200; // 2-hour horizon

    triggerTimerSec    = NTN_DEFAULT_T_SERVICE_SEC;
    distanceThresholdM = 0;

    libsgp4::DateTime now = sat_time::Now();

    // If already below the threshold right now, exit immediately
    SatPvEcef curState{};
    if (!PropagateToEcef(ownTle, now, curState))
        return;

    if (ElevationAngleDeg(ueEcef, curState.pos) < static_cast<double>(thetaExitDeg))
    {
        triggerTimerSec    = 0;
        distanceThresholdM = static_cast<int>(EcefDistance(ueEcef, curState.nadir));
        return;
    }

    double nadirAtExitM = 0.0;
    int tExit = FindExitTimeSec(ownTle, ueEcef, now, 0, thetaExitDeg, MAX_LOOKAHEAD_SEC, nadirAtExitM);

    triggerTimerSec    = tExit;
    distanceThresholdM = static_cast<int>(nadirAtExitM);
}


static int selectChoCandidateProfile(
    const GnbHandoverConfig &config, Logger *logger)
{
    std::vector<const GnbChoCandidateProfileConfig *> selected{};

    if (!config.choEnabled)
        return -1;

    return config.choDefaultProfileId;
}




static const nr::rrc::common::ReportConfigEvent *findEventConfigByKind(
    const GnbHandoverConfig &config, HandoverEventType eventType)
{
    for (const auto &event : config.events)
    {
        if (event.eventKind == eventType)
            return &event;
    }
    return nullptr;
}

[[maybe_unused]] static const nr::rrc::common::ReportConfigEvent *findEventConfigByType(
    const GnbHandoverConfig &config, const std::string &eventType)
{
    auto parsed = nr::rrc::common::ParseHandoverEventType(eventType);
    if (!parsed.has_value())
        return nullptr;

    return findEventConfigByKind(config, *parsed);
}


static const GnbNeighborConfig *findNeighborByNci(const std::vector<GnbNeighborConfig> &neighborList, int64_t nci)
{
    for (const auto &neighbor : neighborList)
    {
        if (neighbor.nci == nci)
            return &neighbor;
    }

    return nullptr;
}

using ScoredNeighbor = std::pair<const GnbNeighborConfig *, int>;

/**
 * @brief Identify and rank neighbor cells as conditional-handover candidates.
 *
 * For each PCI in the 50-satellite neighborhood cache, this function:
 *   1. Skips PCIs not present in the runtime neighbor store (no signaling path).
 *   2. Propagates the neighbor's TLE at T_exit and checks whether its elevation
 *      angle from the UE is >= thetaExitDeg (i.e., the satellite will be in
 *      coverage when the serving gNB exits coverage).
 *   3. Computes the candidate's own exit time (t_exit_new) and scores it by
 *      transit time = t_exit_new - T_exit.  Longer transit → lower (better) score.
 *
 * Falls back to the first non-serving neighbor when the UE position is
 * unknown or no elevation-qualified candidates are found.
 *
 * @param neighborList      Snapshot of the runtime neighbor store
 * @param servingPci        PCI of the serving (own) satellite gNB
 * @param neighborhoodCache Ordered list of PCIs in the 50-satellite neighborhood
 * @param tleStore          TLE store (shared across tasks)
 * @param ueEcef            UE ECEF position (meters); zero vector if unknown
 * @param tExitSec          T_exit: seconds from now when serving gNB exits coverage
 * @param elevationMinDeg   Minimum elevation angle threshold (integer degrees)
 * @return Sorted vector of (GnbNeighborConfig*, score), best candidate first
 */
static std::vector<ScoredNeighbor> prioritizeNeighbors(
    const std::vector<GnbNeighborConfig> &neighborList,
    int servingPci,
    const std::vector<int> &neighborhoodCache,
    const SatTleStore &tleStore,
    const EcefPosition &ueEcef,
    int tExitSec,
    int elevationMinDeg)
{
    std::vector<ScoredNeighbor> prioritized{};

    // Helper: return first non-serving neighbor as a fallback
    auto fallback = [&]() -> std::vector<ScoredNeighbor> {
        std::vector<ScoredNeighbor> fb{};
        for (const auto &nb : neighborList)
        {
            if (nb.getPci() != servingPci)
            {
                fb.emplace_back(&nb, 1);
                break;
            }
        }
        return fb;
    };

    // If UE position is unavailable or cache is empty, use config-order fallback
    bool hasUePos = (ueEcef.x != 0.0 || ueEcef.y != 0.0 || ueEcef.z != 0.0);
    if (!hasUePos || neighborhoodCache.empty())
        return fallback();

    static constexpr int MAX_LOOKAHEAD_SEC = 7200; // 2-hour horizon

    libsgp4::DateTime now    = sat_time::Now();
    libsgp4::DateTime tExitDt = now.AddSeconds(tExitSec);

    for (int pci : neighborhoodCache)
    {
        if (pci == servingPci)
            continue;

        // Must be in the runtime neighbor store to be a usable CHO target
        const GnbNeighborConfig *nbCfg = nullptr;
        for (const auto &nb : neighborList)
        {
            if (nb.getPci() == pci)
            {
                nbCfg = &nb;
                break;
            }
        }
        if (!nbCfg)
            continue;

        // Need a TLE to propagate the satellite
        auto tleOpt = tleStore.find(pci);
        if (!tleOpt.has_value())
            continue;

        // (2) Check that this satellite is above theta_e from the UE at T_exit
        SatPvEcef stateAtExit{};
        if (!PropagateToEcef(*tleOpt, tExitDt, stateAtExit))
            continue;

        if (ElevationAngleDeg(ueEcef, stateAtExit.pos) < static_cast<double>(elevationMinDeg))
            continue;

        // (3) Find this satellite's own exit time starting from T_exit
        double nadirDummy = 0.0;
        int tNeighborExit = FindExitTimeSec(
            *tleOpt, ueEcef, now,
            tExitSec, elevationMinDeg,
            MAX_LOOKAHEAD_SEC, nadirDummy);

        // Transit time = how long the candidate stays above elevationMinDeg after T_exit
        int transitSec = tNeighborExit - tExitSec;

        // Lower score = higher priority; negate so longest transit sorts first
        prioritized.emplace_back(nbCfg, -transitSec);
    }

    if (prioritized.empty())
        return fallback();

    std::sort(prioritized.begin(), prioritized.end(),
              [](const ScoredNeighbor &a, const ScoredNeighbor &b) {
                  return a.second < b.second;
              });

    return prioritized;
}



static bool extractNestedRrcReconfiguration(const OctetString &rrcContainer,
                                            OctetString &nestedRrcReconfiguration)
{
    auto *decoded = rrc::encode::Decode<ASN_RRC_DL_DCCH_Message>(asn_DEF_ASN_RRC_DL_DCCH_Message, rrcContainer);
    if (!decoded)
        return false;

    bool isReconfig = decoded->message.present == ASN_RRC_DL_DCCH_MessageType_PR_c1 &&
                      decoded->message.choice.c1 != nullptr &&
                      decoded->message.choice.c1->present ==
                          ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration &&
                      decoded->message.choice.c1->choice.rrcReconfiguration != nullptr;

    if (!isReconfig)
    {
        asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, decoded);
        return false;
    }

    auto *innerReconfig = decoded->message.choice.c1->choice.rrcReconfiguration;
    nestedRrcReconfiguration = rrc::encode::EncodeS(asn_DEF_ASN_RRC_RRCReconfiguration, innerReconfig);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, decoded);

    return nestedRrcReconfiguration.length() > 0;
}

static bool extractTargetPciFromNestedRrcReconfiguration(const OctetString &nestedRrcReconfiguration,
                                                         int &targetPci)
{
    auto *reconfig = rrc::encode::Decode<ASN_RRC_RRCReconfiguration>(asn_DEF_ASN_RRC_RRCReconfiguration,
                                                                      nestedRrcReconfiguration);
    if (!reconfig)
        return false;

    bool ok = false;

    if (reconfig->criticalExtensions.present ==
            ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration &&
        reconfig->criticalExtensions.choice.rrcReconfiguration != nullptr)
    {
        auto *ies = reconfig->criticalExtensions.choice.rrcReconfiguration;
        if (ies->nonCriticalExtension != nullptr && ies->nonCriticalExtension->masterCellGroup != nullptr)
        {
            OctetString masterCellGroup = asn::GetOctetString(*ies->nonCriticalExtension->masterCellGroup);
            auto *cellGroup = rrc::encode::Decode<ASN_RRC_CellGroupConfig>(asn_DEF_ASN_RRC_CellGroupConfig,
                                                                            masterCellGroup);
            if (cellGroup)
            {
                if (cellGroup->spCellConfig != nullptr &&
                    cellGroup->spCellConfig->reconfigurationWithSync != nullptr &&
                    cellGroup->spCellConfig->reconfigurationWithSync->spCellConfigCommon != nullptr &&
                    cellGroup->spCellConfig->reconfigurationWithSync->spCellConfigCommon->physCellId != nullptr)
                {
                    targetPci = static_cast<int>(
                        *cellGroup->spCellConfig->reconfigurationWithSync->spCellConfigCommon->physCellId);
                    ok = true;
                }

                asn::Free(asn_DEF_ASN_RRC_CellGroupConfig, cellGroup);
            }
        }
    }

    asn::Free(asn_DEF_ASN_RRC_RRCReconfiguration, reconfig);
    return ok;
}

/**
 * @brief Receives an RRCReconfigurationComplete message from a UE, which may indicate the 
 * completion of a handover.  If this is a handover completion, triggers post-handover 
 * processing such as NGAP notification. Otherwise, just logs the completion of a normal 
 * reconfiguration.
 * 
 * @param ueId 
 * @param msg 
 */
void GnbRrcTask::receiveRrcReconfigurationComplete(int ueId, int cRnti,
    const ASN_RRC_RRCReconfigurationComplete &msg)
{
    int64_t txId = msg.rrc_TransactionIdentifier;

    int resolvedUeId = ueId;

    // A UE can share txId values with other UEs (txId is tiny), so match by UE ID first,
    // then verify txId for that UE's pending handover.
    auto itPending = m_handoversPending.find(resolvedUeId);
    bool matchedPending =
        itPending != m_handoversPending.end() &&
        itPending->second != nullptr &&
        itPending->second->ctx != nullptr &&
        itPending->second->txId == txId &&
        (cRnti <= 0 || itPending->second->ctx->cRnti == cRnti);

    // if matchedPending is False, this either isn't associated with a pending handover, or its got a bad UEID
    //   We check the cRNTI and txId against the pending handovers to see if we can find a match 
    //   and resolve the correct UE ID
    if (!matchedPending && cRnti > 0)
    {
        // If UE ID was mis-associated on UL delivery, remap using (txId, cRnti).
        for (auto it = m_handoversPending.begin(); it != m_handoversPending.end(); ++it)
        {
            auto *pending = it->second;
            if (!pending || !pending->ctx)
                continue;

            if (pending->txId == txId && pending->ctx->cRnti == cRnti)
            {
                resolvedUeId = it->first;
                itPending = it;
                matchedPending = true;

                if (resolvedUeId != ueId)
                {
                    m_logger->warn(
                        "RRCReconfigurationComplete UE remap: incomingUeId=%d resolvedUeId=%d txId=%ld cRnti=%d",
                        ueId, resolvedUeId, txId, cRnti);
                }
                break;
            }
        }
    }

    // matchedPending is True if there is pending handover, so complete it by moving the pending
    // context to the main UE context map.
    if (matchedPending)
    {

        /* move the ctx from pending handover to UE context */

        // get ptr to rrc context in the pending handover map (indexed by UE ID)
        auto *handoverCtx = itPending->second->ctx;

        // check for old UE context with the same UE ID, if exists, remove it 
        // (since after handover completion, the old UE context is no longer valid)
        auto *ue = findCtxByUeId(resolvedUeId);
        if (ue)
        {
            delete ue;
            m_ueCtx.erase(resolvedUeId);
        }

        // move the UE context from pending handover to UE context map and erase the pending handover
        m_ueCtx[resolvedUeId] = handoverCtx;
        m_handoversPending.erase(itPending);

        // not sure if this is still needed, but clean it up anyway
        handoverCtx->handoverInProgress = false;

        // Re-arm measurement reporting on target gNB after handover.
        sendMeasConfig(resolvedUeId, true);

        // Notify NGAP of handover completion.
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_NOTIFY);
        w->ueId = resolvedUeId;
        m_base->ngapTask->push(std::move(w));

        m_logger->info("UE[%d] Handover completed. NGAP notification sent.", resolvedUeId);
        return;

    }

    // other RRCReconfigComplete msgs

    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
    {
        m_logger->warn("UE[%d] RRCReconfigurationComplete received from unknown UE, ignoring", ueId);
        return;
    }

    // no gnb action needed for non-handover RRCReconfigurationComplete, just log it

    m_logger->info("UE[%d] RRCReconfigurationComplete received txId=%ld", ueId, txId);

}


/**
 * @brief handles a Measurement Report from a UE
 * 
 */
void GnbRrcTask::receiveMeasurementReport(int ueId, int cRnti,
    const ASN_RRC_MeasurementReport &msg)
{
    int resolvedUeId = ueId;

    auto *ue = tryFindUeByUeId(resolvedUeId);
    if (!ue && cRnti > 0)
    {
        ue = tryFindUeByCrnti(cRnti);
        if (ue)
        {
            resolvedUeId = ue->ueId;
            if (resolvedUeId != ueId)
            {
                m_logger->warn(
                    "MeasurementReport UE remap: incomingUeId=%d resolvedUeId=%d cRnti=%d",
                    ueId, resolvedUeId, cRnti);
            }
        }
    }

    if (!ue)
    {
        m_logger->warn("UE[%d] MeasurementReport from unknown (cRnti=%d)", ueId, cRnti);
        return;
    }

    if (msg.criticalExtensions.present !=
        ASN_RRC_MeasurementReport__criticalExtensions_PR_measurementReport)
    {
        m_logger->err("MeasurementReport: unrecognised criticalExtensions");
        return;
    }

    auto *ies = msg.criticalExtensions.choice.measurementReport;
    if (!ies)
    {
        m_logger->err("MeasurementReport: null IEs");
        return;
    }

    auto &results = ies->measResults;
    long measId = results.measId;
    const auto *sentMeasIdentity = ue->findSentMeasIdentity(measId);
    if (!sentMeasIdentity)
    {
        m_logger->warn("UE[%d] MeasurementReport unknown measId=%ld", resolvedUeId, measId);
        return;
    }

    // Extract serving cell measurement
    int servingRsrp = MIN_RSRP;
    if (results.measResultServingMOList.list.count > 0)
    {
        auto *servMO = results.measResultServingMOList.list.array[0];
        if (servMO)
        {
            auto *ssbCell = servMO->measResultServingCell.measResult.cellResults.resultsSSB_Cell;
            if (ssbCell && ssbCell->rsrp)
                servingRsrp = nr::rrc::common::mtqFromASNValue(*ssbCell->rsrp);
        }
    }

    m_logger->info("UE[%d] MeasurementReport measId=%ld event=%s servingRSRP=%ddBm",
                   resolvedUeId, measId, sentMeasIdentity->eventType.c_str(), servingRsrp);

    // Extract neighbor cell measurements
    if (results.measResultNeighCells)
    {
        if (results.measResultNeighCells->present ==
            ASN_RRC_MeasResults__measResultNeighCells_PR_measResultListNR)
        {
            auto *nrList = results.measResultNeighCells->choice.measResultListNR;
            if (nrList)
            {
                int bestNeighPci = -1;
                int bestNeighRsrp = MIN_RSRP;

                for (int i = 0; i < nrList->list.count; i++)
                {
                    auto *nr = nrList->list.array[i];
                    if (!nr)
                        continue;

                    int pci = nr->physCellId ? static_cast<int>(*nr->physCellId) : -1;
                    int rsrp = MIN_RSRP;
                    auto *ssbCell = nr->measResult.cellResults.resultsSSB_Cell;
                    if (ssbCell && ssbCell->rsrp)
                        rsrp = nr::rrc::common::mtqFromASNValue(*ssbCell->rsrp);

                    m_logger->debug("  Neighbor PCI=%d RSRP=%ddBm", pci, rsrp);

                    if (rsrp > bestNeighRsrp)
                    {
                        bestNeighRsrp = rsrp;
                        bestNeighPci = pci;
                    }
                }

                // Store best neighbor info in UE context for potential handover decision
                if (bestNeighPci >= 0)
                {
                    ue->lastMeasReportPci = bestNeighPci;
                    ue->lastMeasReportRsrp = bestNeighRsrp;
                    m_logger->info("Best neighbor: PCI=%d RSRP=%ddBm (serving=%ddBm)",
                                   bestNeighPci, bestNeighRsrp, servingRsrp);
                }
            }
        }
    }

    // Update serving RSRP and evaluate handover decision
    ue->lastServingRsrp = servingRsrp;
    evaluateHandoverDecision(resolvedUeId, sentMeasIdentity->eventType);
}

/* ================================================================== */
/*  Send RRCReconfiguration with ReconfigurationWithSync (Handover)   */
/* ================================================================== */

void GnbRrcTask::sendHandoverCommand(int ueId, int targetPci, int newCrnti, int t304Ms)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->err("Cannot send handover command: UE[%d] not found", ueId);
        return;
    }

    m_logger->info("UE[%d] Sending handover command targetPCI=%d newC-RNTI=%d t304=%dms",
                   ueId, targetPci, newCrnti, t304Ms);

    int hoCrnti = normalizeCrntiForRrc(newCrnti);
    if (hoCrnti != newCrnti)
    {
        m_logger->warn("UE[%d] Normalizing handover C-RNTI %d -> %d", ueId, newCrnti, hoCrnti);
    }

    // ---- Build CellGroupConfig with ReconfigurationWithSync ----
    ASN_RRC_ReconfigurationWithSync rws{};
    rws.newUE_Identity = static_cast<long>(hoCrnti);
    rws.t304 = nr::rrc::common::t304MsToEnum(t304Ms);

    // Set target cell PCI via spCellConfigCommon
    ASN_RRC_ServingCellConfigCommon scc{};
    long pci = static_cast<long>(targetPci);
    scc.physCellId = &pci;
    scc.dmrs_TypeA_Position = ASN_RRC_ServingCellConfigCommon__dmrs_TypeA_Position_pos2;
    scc.ss_PBCH_BlockPower = 0;
    rws.spCellConfigCommon = &scc;

    // Wrap in SpCellConfig
    ASN_RRC_SpCellConfig spCellConfig{};
    spCellConfig.reconfigurationWithSync = &rws;

    // Wrap in CellGroupConfig
    ASN_RRC_CellGroupConfig cellGroupConfig{};
    cellGroupConfig.cellGroupId = 0;
    cellGroupConfig.spCellConfig = &spCellConfig;

    // UPER-encode CellGroupConfig to OCTET STRING (masterCellGroup)
    OctetString masterCellGroupOctet =
        rrc::encode::EncodeS(asn_DEF_ASN_RRC_CellGroupConfig, &cellGroupConfig);

    if (masterCellGroupOctet.length() == 0)
    {
        m_logger->err("UE[%d] Failed to encode CellGroupConfig for handover command", ueId);
        return;
    }

    // ---- Build RRCReconfiguration DL-DCCH message ----
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present =
        ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();

    long txId = getNextTid(ueId);
    reconfig->rrc_TransactionIdentifier = txId;
    reconfig->criticalExtensions.present =
        ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    // Set nonCriticalExtension (v1530-IEs) with masterCellGroup
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->masterCellGroup = asn::New<OCTET_STRING_t>();
    asn::SetOctetString(*ies->nonCriticalExtension->masterCellGroup, masterCellGroupOctet);

    // Record handover state in UE context
    ue->handoverInProgress = true;
    ue->handoverTargetPci = targetPci;
    ue->handoverNewCrnti = hoCrnti;
    ue->handoverTxId = txId;

    sendRrcMessage(ueId, pdu);

    m_logger->info("UE[%d] RRCReconfiguration (handover) sent to UE, txId=%ld", ueId, txId);
}

long getNewReportConfigId(RrcUeContext *ue)
{
    long id = 1;
    while (true)
    {
        // search vector for id, if not found, break and use it.  
        //  If found, increment and keep searching
        if (std::find(ue->usedReportConfigIds.begin(), ue->usedReportConfigIds.end(), id)            == ue->usedReportConfigIds.end())
        {
            break;
        }
        id++;
    }
    return id;
}


long getNewMeasId(RrcUeContext *ue)
{
    long id = 1;
    while (true)
    {
        // search vector for id in measId field of SentMeasIdentities structs, if not found, break and use it.  
        //  If found, increment and keep searching
        auto it = std::find_if(
            ue->usedMeasIdentities.begin(), ue->usedMeasIdentities.end(),
            [id](const RrcUeContext::MeasIdentityMappings &measIdStruct) {
                return measIdStruct.measId == id;
            });

        if (it == ue->usedMeasIdentities.end())
        {
            break;
        }
        id++;

    }
    return id;
}

void clearMeasConfig(RrcUeContext *ue)
{
    for (auto &sentMeasConfig : ue->sentMeasConfigs)
    {
        auto *measConfig = std::get<0>(sentMeasConfig);
        if (measConfig)
            asn::Free(asn_DEF_ASN_RRC_MeasConfig, measConfig);
    }


    // clear all the ID trackers
    ue->usedMeasObjectIds.clear();
    ue->usedReportConfigIds.clear();
    ue->usedMeasIdentities.clear();

    // clears the MeasConfig pointers
    ue->sentMeasConfigs.clear();
}


/**
 * @brief Creates a MeasConfig IE.  If isChoEnbaled is true, the MeasConfig will be built using
 * the Conditional Handover ASN types.  Otherwise, a default MeasConfig will be created 
 * using normal "eventTriggered" reporting events.
 * 
 * @param ue 
 * @param selectedEvents 
 * @return ASN_RRC_MeasConfig* 
 */
std::vector<long> GnbRrcTask::createMeasConfig(
    ASN_RRC_MeasConfig *&mc,
    RrcUeContext *ue,
    std::vector<nr::rrc::common::ReportConfigEvent> selectedEvents,
    bool isChoRequest,
    int trigger_timer_sec,
    int distance_threshold_m,
    int choProfileId)
{
    std::vector<long> usedMeasIds{};

    // Build MeasConfig
    mc = asn::New<ASN_RRC_MeasConfig>();

    // Build MeasObject
    //    Since RF layers not implemented, this is a dummy object
    //   only include this if we haven't already sent it to the UE
    if (ue->usedMeasObjectIds.empty())
    {
        mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
        auto *measObj = asn::New<ASN_RRC_MeasObjectToAddMod>();
        measObj->measObjectId = DUMMY_MEAS_OBJECT_ID;
        measObj->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
        measObj->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
        measObj->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
        *measObj->measObject.choice.measObjectNR->ssbFrequency = 632628; // typical n78 SSB freq
        measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
        *measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = ASN_RRC_SubcarrierSpacing_kHz30;

        // smtc1 (SSB MTC periodicity/offset/duration) - required for NR measObject.
        measObj->measObject.choice.measObjectNR->smtc1 = asn::New<ASN_RRC_SSB_MTC>();
        measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
            ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
        measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
        measObj->measObject.choice.measObjectNR->smtc1->duration = ASN_RRC_SSB_MTC__duration_sf1;
        // quantityConfigIndex is mandatory INTEGER (1..maxNrofQuantityConfig); must be >= 1.
        measObj->measObject.choice.measObjectNR->quantityConfigIndex = 1;
        asn::SequenceAdd(*mc->measObjectToAddModList, measObj);
    }

    // ReportConfig list: one ReportConfig per configured handover event type.
    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();
    
    for (size_t i = 0; i < selectedEvents.size(); i++)
    {
        const auto &event = selectedEvents[i];
        const auto eventKind = event.eventKind;
        const auto eventType = nr::rrc::common::ToString(eventKind);
        // pull a ReportConfigId value that does not conflict with existing active ReportConfigIds
        const long reportConfigId = getNewReportConfigId(ue);
        // pull a MeasId value that does not conflict with existing active MeasIds 
        const long measId = getNewMeasId(ue);

        auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
        rcMod->reportConfigId = reportConfigId;
        rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
        rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

        auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;

        // normal "eventTriggered" events
        if (!isChoRequest)
        {
            rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
            rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();
            auto *et = rcNR->reportType.choice.eventTriggered;

            if (eventKind == HandoverEventType::A2)
            {
                et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA2;
                asn::MakeNew(et->eventId.choice.eventA2);

                auto *a2 = et->eventId.choice.eventA2;
                a2->a2_Threshold.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
                a2->a2_Threshold.choice.rsrp = nr::rrc::common::mtqToASNValue(event.a2_thresholdDbm);
                a2->hysteresis = nr::rrc::common::hysteresisToASNValue(event.a2_hysteresisDb);
                a2->timeToTrigger = nr::rrc::common::tttMsToASNValue(event.ttt);
                a2->reportOnLeave = true;

                // Ask UE to include neighbor measurements in A2 reports where possible.
                et->reportAddNeighMeas = asn::New<long>();
                *et->reportAddNeighMeas = ASN_RRC_EventTriggerConfig__reportAddNeighMeas_setup;

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=A2 threshold=%ddBm hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.a2_thresholdDbm, event.a2_hysteresisDb, E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::A3)
            {
                et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
                asn::MakeNew(et->eventId.choice.eventA3);

                auto *a3 = et->eventId.choice.eventA3;
                a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
                a3->a3_Offset.choice.rsrp = nr::rrc::common::mtqOffsetToASNValue(event.a3_offsetDb);
                a3->hysteresis = nr::rrc::common::hysteresisToASNValue(event.a3_hysteresisDb);
                a3->timeToTrigger = nr::rrc::common::tttMsToASNValue(event.ttt);
                a3->reportOnLeave = true;
                a3->useAllowedCellList = false;

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=A3 offset=%ddB hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.a3_offsetDb, event.a3_hysteresisDb, nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::A5)
            {
                et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA5;
                asn::MakeNew(et->eventId.choice.eventA5);

                auto *a5 = et->eventId.choice.eventA5;
                a5->a5_Threshold1.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
                a5->a5_Threshold1.choice.rsrp = nr::rrc::common::mtqToASNValue(event.a5_threshold1Dbm);
                a5->a5_Threshold2.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
                a5->a5_Threshold2.choice.rsrp = nr::rrc::common::mtqToASNValue(event.a5_threshold2Dbm);
                a5->hysteresis = nr::rrc::common::hysteresisToASNValue(event.a5_hysteresisDb);
                a5->timeToTrigger = nr::rrc::common::tttMsToASNValue(event.ttt);
                a5->reportOnLeave = true;
                a5->useAllowedCellList = false;

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=A5 threshold1=%ddBm threshold2=%ddBm "
                                "hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.a5_threshold1Dbm,
                                event.a5_threshold2Dbm, event.a5_hysteresisDb, nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::D1)
            {
                et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17;
                asn::MakeNew(et->eventId.choice.eventD1_r17);

                auto *d1 = et->eventId.choice.eventD1_r17;
                d1->distanceThreshFromReference1_r17 = nr::rrc::common::distanceThresholdToASNValue(event.d1_distanceThreshFromReference1);
                d1->distanceThreshFromReference2_r17 = nr::rrc::common::distanceThresholdToASNValue(event.d1_distanceThreshFromReference2);

                //d1->referenceLocation1_r17.present


                d1->reportOnLeave_r17 = true;
                d1->hysteresisLocation_r17 = nr::rrc::common::hysteresisLocationToASNValue(event.d1_hysteresisLocation);
                d1->timeToTrigger = nr::rrc::common::tttMsToASNValue(event.ttt);

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=D1 distThresh1=%dm distThresh2=%dm "
                                "hysteresis=%dm "
                                "ttt=%s",
                                ue->ueId,
                                measId,
                                event.d1_distanceThreshFromReference1,
                                event.d1_distanceThreshFromReference2,
                                event.d1_hysteresisLocation,
                                nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else 
            {
                m_logger->warn("UE[%d] Unsupported event type %s, skipping", ue->ueId, eventType.c_str());
                continue;
            }
        
        
            et->rsType = ASN_RRC_NR_RS_Type_ssb;
            et->reportInterval = ASN_RRC_ReportInterval_ms480;
            et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;

            // only support RSRP since this just simulated
            et->reportQuantityCell.rsrp = true;
            et->reportQuantityCell.rsrq = false;
            et->reportQuantityCell.sinr = false;
            et->maxReportCells = 4;
        }

        // conditional events
        else
        {
            rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_condTriggerConfig_r16;
            rcNR->reportType.choice.condTriggerConfig_r16 = asn::New<ASN_RRC_CondTriggerConfig_r16>();
            auto *ctc = rcNR->reportType.choice.condTriggerConfig_r16;

            if (eventKind == HandoverEventType::CondA3)
            {

                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventA3;
                asn::MakeNew(ctc->condEventId.choice.condEventA3);

                auto *a3 = ctc->condEventId.choice.condEventA3;
                a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
                a3->a3_Offset.choice.rsrp = nr::rrc::common::mtqOffsetToASNValue(event.a3_offsetDb);
                a3->hysteresis = nr::rrc::common::hysteresisToASNValue(event.a3_hysteresisDb);
                a3->timeToTrigger = nr::rrc::common::tttMsToASNValue(event.ttt);

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventA3 offset=%ddB hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.a3_offsetDb, event.a3_hysteresisDb, nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }

            else if (eventKind == HandoverEventType::CondD1)
            {
                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventD1_r17;
                asn::MakeNew(ctc->condEventId.choice.condEventD1_r17);

                auto *d1 = ctc->condEventId.choice.condEventD1_r17;
                d1->distanceThreshFromReference1_r17 = nr::rrc::common::distanceThresholdToASNValue(event.condD1_distanceThreshFromReference1);
                d1->distanceThreshFromReference2_r17 = nr::rrc::common::distanceThresholdToASNValue(event.condD1_distanceThreshFromReference2);
                // d1->referenceLocation1_r17.choice
                // auto *rl1 = asn::New<ASN_RRC_CondTriggerConfig_r16__condEventId__condEventD1_r17__referenceLocation_r17>();
                
                // d1->referenceLocation1_r17 = referenceLocationToASNValue(event.condD1_referenceLocation1);
                // d1->referenceLocation2_r17 = referenceLocationToASNValue(event.condD1_referenceLocation2);

                d1->hysteresisLocation_r17 = nr::rrc::common::hysteresisLocationToASNValue(event.condD1_hysteresisLocation);
                d1->timeToTrigger_r17 = nr::rrc::common::tttMsToASNValue(event.ttt);

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventD1 distThresh1=%dm distThresh2=%dm "
                                "hysteresis=%dm "
                                "ttt=%s",
                                ue->ueId,
                                measId,
                                event.condD1_distanceThreshFromReference1,
                                event.condD1_distanceThreshFromReference2,
                                event.condD1_hysteresisLocation,
                                nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::CondT1)
            {
                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventT1_r17;
                asn::MakeNew(ctc->condEventId.choice.condEventT1_r17);

                auto *t1 = ctc->condEventId.choice.condEventT1_r17;
                t1->t1_Threshold_r17 = nr::rrc::common::t1ThresholdToASNValue(event.condT1_thresholdSecTS);
                t1->duration_r17 = nr::rrc::common::durationToASNValue(event.condT1_durationSec);

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventT1 threshold=%dsec duration=%dsec",
                                ue->ueId, measId, event.condT1_thresholdSecTS, event.condT1_durationSec);
            }
            else
            {
                m_logger->warn("UE[%d] Unsupported conditional event type %s, skipping", ue->ueId, eventType.c_str());
                continue;
            }

            
        }

        asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);

        // Create the MeasId

        auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
        measIdMod->measId = measId;
        measIdMod->measObjectId = DUMMY_MEAS_OBJECT_ID; // the dummy NR measObject we defined above
        measIdMod->reportConfigId = reportConfigId;
        asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);

        // update ue's MeasConfig trackers
        ue->usedMeasIdentities.push_back({
            measId,
            DUMMY_MEAS_OBJECT_ID,
            reportConfigId,
            eventKind,
            eventType,
            choProfileId
        });

        usedMeasIds.push_back(measId);

        ue->usedReportConfigIds.push_back(reportConfigId);
        ue->usedMeasObjectIds.push_back(measIdMod->measObjectId);
    }

    return usedMeasIds;

}



/**
 * @brief Sends measurement config to UE
 * 
 * @param ueId 
 * @param forceResend 
 */
void GnbRrcTask::sendMeasConfig(int ueId, bool forceResend)
{
    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
        return;

    if (!forceResend && !ue->usedMeasIdentities.empty())
        return; // already configured

    if (forceResend && !ue->usedMeasIdentities.empty())
    {
        m_logger->info("UE[%d] Forcing MeasConfig resend after handover", ue->ueId);
        ue->usedMeasIdentities.clear();
    }

    auto selectedEvents = m_config->handover.events;
    if (selectedEvents.empty())
    {
        m_logger->warn("UE[%d] No non-CHO handover events configured; skipping MeasConfig", ue->ueId);

        // if conditional handover enabled, generate a CHO RRC message
        if (m_config->handover.choEnabled)
            processConditionalHandover(ue->ueId, "no-measconfig");
        
        return;
    }

    std::string eventList{};
    for (size_t i = 0; i < selectedEvents.size(); i++)
    {
        if (i != 0)
            eventList += ",";
        eventList += nr::rrc::common::ToString(selectedEvents[i].eventKind);
    }

    m_logger->info("UE[%d] Sending MeasConfig with eventType(s)=%s", ue->ueId, eventList.c_str());

    // Build RRCReconfiguration with measConfig
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present =
        ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();

    long txId = getNextTid(ue->ueId);
    reconfig->rrc_TransactionIdentifier = txId;
    reconfig->criticalExtensions.present =
        ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    // Force clean-slate behavior for simulation stability: UE replaces prior
    // measurement configuration instead of relying on delta persistence.
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->fullConfig = asn::New<long>();
    *ies->nonCriticalExtension->fullConfig =
        ASN_RRC_RRCReconfiguration_v1530_IEs__fullConfig_true;
    clearMeasConfig(ue);
    
    // Build MeasConfig
    ASN_RRC_MeasConfig *mc = nullptr;
    auto *mc_saved = asn::New<ASN_RRC_MeasConfig>();

    auto usedMeasIds = createMeasConfig(mc, ue, selectedEvents, false, 0, 0, 0);
    ies->measConfig = mc;
    
    if (!asn::DeepCopy(asn_DEF_ASN_RRC_MeasConfig, *mc, mc_saved))
    {
        asn::Free(asn_DEF_ASN_RRC_MeasConfig, mc_saved);
        mc_saved = nullptr;
        m_logger->err("UE[%d] Failed to deep-copy MeasConfig for local storage", ue->ueId);
    }

    sendRrcMessage(ue->ueId, pdu);

    // store the sent MeasConfig in UE context for potential future reference (e.g. handovers)
    if (mc_saved)
        ue->sentMeasConfigs.push_back({mc_saved, usedMeasIds});


    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("UE[%d] MeasConfig (%zu measId entries) sent, txId=%ld",
                   ue->ueId, usedMeasIds.size(), txId);

    // if conditional handover enabled, generate a CHO RRC message
    if (m_config->handover.choEnabled)
        processConditionalHandover(ue->ueId, "post-measconfig");
}

/**
 * @brief Evaluates whether to trigger a handover based on the latest measurement report from the UE.
 * 
 * @param ueId UE ID of UE providing measurement report 
 */
void GnbRrcTask::evaluateHandoverDecision(int ueId, const std::string &eventType)
{
    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
        return;

    // Don't trigger if handover is already in progress
    if (ue->handoverInProgress || ue->handoverDecisionPending)
        return;

    int bestNeighPci = ue->lastMeasReportPci;
    int bestNeighRsrp = ue->lastMeasReportRsrp;
    int servingRsrp = ue->lastServingRsrp;

    const auto parsedEvent = nr::rrc::common::ParseHandoverEventType(eventType);
    if (!parsedEvent.has_value())
    {
        m_logger->warn("UE[%d] HandoverEval: unsupported event type %s", ue->ueId, eventType.c_str());
        return;
    }

    const auto eventKind = *parsedEvent;
    const auto *eventConfig = findEventConfigByKind(m_config->handover, eventKind);
    if (!eventConfig)
    {
        m_logger->warn("UE[%d] HandoverEval: no config found for event=%s", ue->ueId, eventType.c_str());
        return;
    }

    // only evaluate events that use measurement reports
    if (!isEventTriggerEventType(eventKind))
        return;

    bool shouldHandover = false;

    if (eventKind == HandoverEventType::A2)
    {
        // A2: serving RSRP < threshold - hysteresis
        int hysteresisDb = nr::rrc::common::hysteresisFromASNValue(eventConfig->a2_hysteresisDb);
        int thresholdDbm = nr::rrc::common::mtqFromASNValue(eventConfig->a2_thresholdDbm);
        shouldHandover = servingRsrp < (thresholdDbm - hysteresisDb);
        m_logger->debug("UE[%d] HandoverEval: event=A2 measId serving=%ddBm threshold=%ddBm hysteresis=%ddB "
                        "condition=(%d < %d) result=%s",
                        ue->ueId, servingRsrp, thresholdDbm, hysteresisDb,
                        servingRsrp, thresholdDbm - hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (eventKind == HandoverEventType::A3)
    {
        // A3: neighbor RSRP > serving RSRP + offset + hysteresis
        int offsetDb = nr::rrc::common::mtqOffsetFromASNValue(eventConfig->a3_offsetDb);
        int hysteresisDb = nr::rrc::common::hysteresisFromASNValue(eventConfig->a3_hysteresisDb);
        shouldHandover = bestNeighPci >= 0 &&
                         bestNeighRsrp > (servingRsrp + offsetDb + hysteresisDb);
        m_logger->debug("UE[%d] HandoverEval: event=A3 serving=%ddBm bestNeighPci=%d bestNeigh=%ddBm "
                        "offset=%ddB hysteresis=%ddB condition=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighPci, bestNeighRsrp,
                        offsetDb, hysteresisDb,
                        bestNeighRsrp, servingRsrp + offsetDb + hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (eventKind == HandoverEventType::A5)
    {
        // A5: serving RSRP < threshold1 - hysteresis AND neighbor RSRP > threshold2 + hysteresis
        int threshold1Dbm = nr::rrc::common::mtqFromASNValue(eventConfig->a5_threshold1Dbm);
        int threshold2Dbm = nr::rrc::common::mtqFromASNValue(eventConfig->a5_threshold2Dbm);
        int hysteresisDb = nr::rrc::common::hysteresisFromASNValue(eventConfig->a5_hysteresisDb);
        shouldHandover = bestNeighPci >= 0 &&
                         servingRsrp < (threshold1Dbm - hysteresisDb) &&
                         bestNeighRsrp > (threshold2Dbm + hysteresisDb);
        m_logger->debug("UE[%d] HandoverEval: event=A5 serving=%ddBm bestNeighPci=%d bestNeigh=%ddBm "
                        "thr1=%ddBm thr2=%ddBm hysteresis=%ddB cond1=(%d < %d) cond2=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighPci, bestNeighRsrp,
                        threshold1Dbm, threshold2Dbm,
                        hysteresisDb,
                        servingRsrp, threshold1Dbm - hysteresisDb,
                        bestNeighRsrp, threshold2Dbm + hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (eventKind == HandoverEventType::D1)
    {
        // D1: distance to serving cell (d1) > threshold1 - hysteresis AND distance to neighbor cell (d2) < threshold2 + hysteresis
        int distThresh1 = nr::rrc::common::distanceThresholdFromASNValue(eventConfig->d1_distanceThreshFromReference1);
        int distThresh2 = nr::rrc::common::distanceThresholdFromASNValue(eventConfig->d1_distanceThreshFromReference2);
        int hysteresisM = nr::rrc::common::hysteresisLocationFromASNValue(eventConfig->d1_hysteresisLocation);

        nr::rrc::common::EventReferenceLocation rl1 = eventConfig->d1_referenceLocation1;
        nr::rrc::common::EventReferenceLocation rl2 = eventConfig->d1_referenceLocation2;

        nr::rrc::common::EventReferenceLocation ue_pos = {
            ue->uePosition.latitude,
            ue->uePosition.longitude
        };

        // calculate distance from gNB to UE based on stored UE position and gNB position;
        //   Note that altitude is not used
        int distance1 = calcLatLongDistance(ue_pos, rl1);
        int distance2 = calcLatLongDistance(ue_pos, rl2);
        
        shouldHandover = bestNeighPci >= 0 &&
                            distance1 > (distThresh1 - hysteresisM) &&
                            distance2 < (distThresh2 + hysteresisM);

        m_logger->debug("UE[%d] HandoverEval: event=D1 distance1=%dm distance2=%dm "
                        "distThresh1=%dm distThresh2=%dm hysteresis=%dm "
                        "cond1=(%d > %d) cond2=(%d < %d) result=%s",
                        ue->ueId, distance1, distance2,
                        distThresh1, distThresh2,
                        hysteresisM,
                        distance1, distThresh1 - hysteresisM,
                        distance2, distThresh2 + hysteresisM,
                        shouldHandover ? "true" : "false");
    }
    else
    {
        m_logger->warn("UE[%d] HandoverEval: unsupported event type %s", ue->ueId, eventType.c_str());
        return;
    }

    if (!shouldHandover)
        return;

    // For A2, a target may not be present in this report; use last known neighbor if available.
    if (bestNeighPci < 0)
    {
        m_logger->warn("UE[%d] Handover decision met event=%s but no neighbor PCI available",
                       ue->ueId, eventType.c_str());
        return;
    }

    m_logger->info("UE[%d] Handover decision (%s): targetPCI=%d (serving=%ddBm, target=%ddBm)",
                   ue->ueId, eventType.c_str(), bestNeighPci, servingRsrp, bestNeighRsrp);

    ue->handoverDecisionPending = true;

    // Initiate N2 handover via NGAP (no Xn interface available)
    auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
    w->ueId = ue->ueId;
    w->hoTargetPci = bestNeighPci;
    w->hoCause = NgapCause::RadioNetwork_handover_desirable_for_radio_reason;
    m_base->ngapTask->push(std::move(w));

    m_logger->info("UE[%d] HandoverRequired sent to NGAP targetPCI=%d", ue->ueId, bestNeighPci);
}

/**
 * @brief initiates creation of conditional handover (CHO) RRC reconfiguration messages to UE.
 * Uses the configured CHO event groups to determine which CHO event(s) to prepare for.
 * Selects target cell(s) based on selection criteria, and requests handover preparation
 * to prepare the target gNB(s) in advance of a handover trigger event.  Because completion of the
 * CHO message required a response form the target gnb (the transparent container with a ReconfigWithSync message)
 * this function only starts the process and sets the CHO preparation flag.  When the AMF responds with the 
 * transparent container, the CHO RRC Reconfiguration message is generated and sent to the UE 
 * (see the RRCTask handleNgapHandoverCommand function).
 * 
 * @param ueId 
 * @param eventType 
 */
void GnbRrcTask::processConditionalHandover(int ueId, const std::string &eventType)
{
    // ue RRC context
    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
        return;

    // if we're still waiting to complete a prior CHO process, don't start another one
    if (ue->choPreparationPending)
        return;

    // Step 1 - Determine handover trigger conditions

    // Here is where we call the satellite position calculations to determine the
    // time and distance when this gnb will be out of range of teh UE, and use that to set the tServiceSec and the distance threshold for D1.
    // We need this now to determine the time to use for evaluating which gnbs are in range

    int trigger_timer_sec = NTN_DEFAULT_T_SERVICE_SEC;
    int distance_threshold_m = 0;

    const int ownPci = cons::getPciFromNci(m_config->nci);
    {
        auto ownTleOpt   = m_base->satTleStore->find(ownPci);
        const int theta  = m_config->ntn.elevationMinDeg;

        if (ownTleOpt.has_value() && ue->uePosition.isValid)
        {
            // convert ue position from lat/long/alt to ECEF for the calculation
            EcefPosition ueEcef = GeoToEcef(ue->uePosition);

            calculateTriggerConditions(*ownTleOpt, ueEcef, theta,
                                       trigger_timer_sec, distance_threshold_m);
            m_logger->info("UE[%d] CHO trigger: T_exit=%ds d_exit=%dm theta=%ddeg",
                           ueId, trigger_timer_sec, distance_threshold_m, theta);
        }
        else
        {
            m_logger->warn("UE[%d] CHO trigger: own TLE or UE position unavailable, "
                           "using defaults (t=%ds d=%dm)",
                           ueId, trigger_timer_sec, distance_threshold_m);
        }
    }

    // store these so they can be used when the targets respond
    ue->choPreparationTriggerTimerSec = trigger_timer_sec;
    ue->choPreparationDistanceThreshold = distance_threshold_m;

    // Step 2 - generate MeasConfig with MeasIds.
    //   Note  - we could reuse existing MeasIds from non-CHO MeasConfigs, but
    //   that gets complex.  Instead, we just create new ones that are CHO-specific


    // select a CHO candidate profile from the active profile config.
    auto choProfileIdx = selectChoCandidateProfile(m_config->handover, m_logger.get());
    if (choProfileIdx < 0)
    {
        m_logger->warn("UE[%d] CHO prepare skipped: no CHO candidate profiles found for index %d", 
                       ueId,
                       m_config->handover.choDefaultProfileId);
        return;
    }

    // create the MeasConfig
    ASN_RRC_MeasConfig *mc = nullptr;
    auto usedMeasIds = createMeasConfig(mc, ue, m_config->handover.candidateProfiles[choProfileIdx].conditions, 
        true, trigger_timer_sec, distance_threshold_m, choProfileIdx);

    if (!mc)
    {
        m_logger->warn("UE[%d] CHO prepare skipped (%s): no active MeasConfig identities",
                       ueId,
                       eventType.c_str());
        return;
    }

    // store the MeasConfig in the UE context waiting for the responses from the targets
    ue->choPreparationMeasConfig = std::move(mc);


    // Step 3 - identify neighbor cells that are possible handover targets.

    // Take a snapshot of the runtime neighbor store once; all pointer lookups below
    // remain valid for the lifetime of this stack frame.
    const auto neighborSnapshot = m_base->neighbors->getAll();

    // prioritizeNeighbors returns a sorted vector of (neighborCell, score) pairs
    //   (lower score = higher priority = longer transit time above theta_e)
    auto prioritizedNeighbors = prioritizeNeighbors(
        neighborSnapshot, ownPci,
        m_satNeighborhoodCache,
        *m_base->satTleStore,
        ue->uePosition.isValid
            ? EcefPosition{ue->uePosition}
            : EcefPosition{},
        trigger_timer_sec,
        m_config->ntn.elevationMinDeg);

    // If the profile has a specific targetCellId, filter to that cell.
    std::optional<int64_t> explicitTargetPci{};
    if (m_config->handover.candidateProfiles[choProfileIdx].targetCellId.has_value())
        explicitTargetPci = cons::getPciFromNci(m_config->handover.candidateProfiles[choProfileIdx].targetCellId.value());

    if (explicitTargetPci.has_value())
    {
        const auto *targetNeighbor = findNeighborByNci(neighborSnapshot, *explicitTargetPci);
        if (!targetNeighbor)
        {
            m_logger->warn("UE[%d] CHO prepare skipped (%s): targetCellId=0x%llx not in neighborList",
                           ueId,
                           eventType.c_str(),
                           static_cast<long long>(*explicitTargetPci));
            return;
        }

        prioritizedNeighbors.clear();
        prioritizedNeighbors.emplace_back(targetNeighbor, 1);
    }

    if (prioritizedNeighbors.empty())
    {
        m_logger->warn("UE[%d] CHO prepare skipped (%s): neighbor list is empty", ueId, eventType.c_str());
        return;
    }

    // go for CHO message preparation - set the pending flag
    
    ue->choPreparationPending = true;
    ue->choPreparationCandidateProfileId = choProfileIdx;
    
    // Step 3 - store the CHO target PCIs and their priority scores in UE RRC context

    ue->choPreparationCandidatePcis.clear();
    ue->choPreparationCandidateScores.clear();
    for (const auto &[neighbor, score] : prioritizedNeighbors)
    {
        const int pci = neighbor->getPci();
        ue->choPreparationCandidatePcis.push_back(pci);
        ue->choPreparationCandidateScores[pci] = score;
    }

    if (ue->choPreparationCandidatePcis.empty())
    {
        ue->choPreparationPending = false;
        ue->choPreparationMeasIds.clear();
        ue->choPreparationCandidateScores.clear();
        ue->choPreparationCandidateProfileId.reset();
        ue->choPreparationDistanceThreshold = 0;
        ue->choPreparationTriggerTimerSec = 0;
        m_logger->warn("UE[%d] CHO prepare skipped (%s): prioritized neighbors have no valid score", ueId,
                       eventType.c_str());
        return;
    }

    ue->choPreparationMeasIds = usedMeasIds;

    int firstCandidatePci = ue->choPreparationCandidatePcis.front();
    int firstCandidatePriority = ue->choPreparationCandidateScores[firstCandidatePci];

    // Step 4 - send handover required to NGAP with CHO preparation flag for each candidate,
    // so NGAP can prepare each target gNB and reply with CHO commands independently.

    for (int targetPci : ue->choPreparationCandidatePcis)
    {
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
        w->ueId = ue->ueId;
        w->hoTargetPci = targetPci;
        w->hoCause = NgapCause::RadioNetwork_handover_desirable_for_radio_reason;
        w->hoForChoPreparation = true;
        m_base->ngapTask->push(std::move(w));
    }

    m_logger->info("UE[%d] CHO prepare started (%s): measIds=%zu candidates=%zu targetPCI_1=%d "
                   "priority_1=%d t-Service=%d",
                   ue->ueId,
                   eventType.c_str(),
                   ue->choPreparationMeasIds.size(),
                   ue->choPreparationCandidatePcis.size(),
                   firstCandidatePci,
                   firstCandidatePriority,
                   trigger_timer_sec);
}

/**
 * @brief Handles a handover command from NGAP, including an RRC container
 * that carries the RRCReconfiguration for handover.
 * 
 * @param ueId 
 * @param rrcContainer 
 */
void GnbRrcTask::handleNgapHandoverCommand(int ueId,
                                           const OctetString &rrcContainer,
                                           bool hoForChoPreparation)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->warn("UE[%d] Cannot find UE for handleNgapHandoverCommand", ueId);
        return;
    }

    m_logger->info("UE[%d] Received NGAP Handover Command with RRC container of %zu bytes (mode=%s)",
                   ueId,
                   rrcContainer.length(),
                   hoForChoPreparation ? "cho-prepare" : "classic");

    // If this handover command is from a CHO preparation request, complete and send
    // a CHO RRCReconfiguration for one candidate as soon as its response arrives.
    if (hoForChoPreparation)
    {
        completeConditionalHandover(ue, rrcContainer);
        return;
    }

    auto *pdu = rrc::encode::Decode<ASN_RRC_DL_DCCH_Message>(asn_DEF_ASN_RRC_DL_DCCH_Message, rrcContainer);
    if (!pdu)
    {
        m_logger->err("Failed to decode handover RRC container as DL-DCCH UE[%d] ", ueId);
        return;
    }

    bool isReconfig = pdu->message.present == ASN_RRC_DL_DCCH_MessageType_PR_c1 &&
                      pdu->message.choice.c1 != nullptr &&
                      pdu->message.choice.c1->present == ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration &&
                      pdu->message.choice.c1->choice.rrcReconfiguration != nullptr;

    if (!isReconfig)
    {
        m_logger->err("UE[%d] Decoded handover RRC container is not an RRCReconfiguration", ueId);
        asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
        return;
    }

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("Forwarded decoded handover RRCReconfiguration UE[%d]", ueId);

}

void GnbRrcTask::handleNgapHandoverFailure(int ueId, int targetPci, bool fromChoPreparation)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue) {
        m_logger->warn("UE[%d] Cannot find UE for handleNgapHandoverFailure", ueId);
        return;
    }

    // failure from a CHO preparation request
    if (fromChoPreparation)
    {
        m_logger->info("UE[%d] Received NGAP Handover Failure for CHO preparation targetPCI=%d", ueId, targetPci);

        // remove targetPci from pending candidate list and if it was the last one, clear the CHO pending state
        auto itPci = std::find(ue->choPreparationCandidatePcis.begin(),
                                ue->choPreparationCandidatePcis.end(),
                                targetPci);
        if (itPci != ue->choPreparationCandidatePcis.end())
        {
            ue->choPreparationCandidatePcis.erase(itPci);
            m_logger->debug("UE[%d] Removed target PCI %d from CHO preparation candidates", ueId, targetPci);
        }
        else {
            m_logger->warn("UE[%d] Received CHO preparation failure for targetPCI=%d which is not in pending candidate list",
                            ueId, targetPci);
        }

        if (ue->choPreparationCandidatePcis.empty())
        {
            clearChoPendingState(ue);
            m_logger->debug("UE[%d] No further CHO preparation candidates; cleared CHO state", ueId);

        }

        return;
    }

    // failure from normal handover request

    m_logger->info("UE[%d] Received NGAP Handover Failure for classic handover targetPCI=%d", ueId, targetPci);

    auto itPending = m_handoversPending.find(ueId);
    if (itPending != m_handoversPending.end() && itPending->second != nullptr)
    {
        if (itPending->second->ctx != nullptr)
            delete itPending->second->ctx;

        delete itPending->second;
        m_handoversPending.erase(itPending);
    }

        m_logger->warn("UE[%d] NGAP handover failure received with no RRC pending handover state", ueId);
}

/**
 * @brief Fully clears the CHO pending state in the UE context, including the MeasConfig, candidate lists, and timers.
 * 
 * @param ue The ue Rrc context
 */
void GnbRrcTask::clearChoPendingState(RrcUeContext *ue)
{
    ue->choPreparationPending = false;
    ue->choPreparationMeasIds.clear();
    ue->choPreparationCandidateScores.clear();
    ue->choPreparationCandidateProfileId.reset();
    ue->choPreparationDistanceThreshold = 0;
    ue->choPreparationTriggerTimerSec = 0;

    ue->choPreparationCandidatePcis.clear();

    // free measconfig if not sent and clear
    if (ue->choPreparationMeasConfig)
    {
        asn::Free(asn_DEF_ASN_RRC_MeasConfig, ue->choPreparationMeasConfig);
        ue->choPreparationMeasConfig = nullptr;
    }
}

/**
 * @brief Called when the NGAP has received a successful response from a target gNB for a conditional handover.
 * Sends the CHO RRCReconfiguration to the UE with the target gNB as the target.
 * 
 * @param ue - the ue Rrc context
 * @param rrcContainer - the transparent container provided by the target gNB, which should contain the nested 
 * RRCReconfiguration for the CHO candidate.
 */
void GnbRrcTask::completeConditionalHandover(RrcUeContext *ue, const OctetString &rrcContainer)
{
    // sanity check - cho prep must be pending
    if (!ue->choPreparationPending)
    {
        m_logger->warn("UE[%d] CHO prepare response arrived with no pending CHO state; ignoring", ue->ueId);
        return;
    }

    if (ue->choPreparationCandidatePcis.empty())
    {
        clearChoPendingState(ue);
        m_logger->warn("UE[%d] CHO prepare response arrived with no pending CHO candidates; ignoring", ue->ueId);
        return;
    }

    // extract the nested RRCReconfiguration container from the NGAP message
    
    OctetString nestedRrcReconfig{};
    if (!extractNestedRrcReconfiguration(rrcContainer, nestedRrcReconfig))
    {
        m_logger->err("UE[%d] Failed to extract nested RRCReconfiguration for CHO candidate", ue->ueId);
        return;
    }

    // determine the target gnb PCI from the nested RRCReconfiguration

    int candidatePci = -1;
    if (!extractTargetPciFromNestedRrcReconfiguration(nestedRrcReconfig, candidatePci))
    {
        m_logger->err("UE[%d] Failed to extract target PCI from nested RRCReconfiguration", ue->ueId);
        return;
    }

    // locate the candidate PCI in the pending CHO list

    auto itPendingPci = std::find(ue->choPreparationCandidatePcis.begin(),
                                    ue->choPreparationCandidatePcis.end(),
                                    candidatePci);
    if (itPendingPci == ue->choPreparationCandidatePcis.end())
    {
        m_logger->warn("UE[%d] CHO prepare response targetPCI=%d not found in pending candidate list; ignoring",
                        ue->ueId,
                        candidatePci);
        return;
    }

    // remove it from the pending list
    ue->choPreparationCandidatePcis.erase(itPendingPci);

    // get its priority from the priority map
    
    // Create RRCReconfiguration message with the nested RRCReconfiguration from the target gNB, 
    //  and include the CHO conditional reconfiguration IEs with the candidate PCI and the MeasIds that 
    //  should trigger execution of this candidate's CHO config.
    
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();
    reconfig->rrc_TransactionIdentifier = getNextTid(ue->ueId);
    reconfig->criticalExtensions.present =
        ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;

    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1540_IEs>();
    ies->nonCriticalExtension->nonCriticalExtension->nonCriticalExtension =
        asn::New<ASN_RRC_RRCReconfiguration_v1560_IEs>();
    auto *v1610 = asn::New<ASN_RRC_RRCReconfiguration_v1610_IEs>();
    ies->nonCriticalExtension->nonCriticalExtension->nonCriticalExtension->nonCriticalExtension = v1610;

    v1610->conditionalReconfiguration = asn::New<ASN_RRC_ConditionalReconfiguration>();
    v1610->conditionalReconfiguration->condReconfigToAddModList =
        asn::New<ASN_RRC_ConditionalReconfiguration::
                    ASN_RRC_ConditionalReconfiguration__condReconfigToAddModList>();

    // if CHO measConfig has not be sent to UE yet, send it in this RRCreconfig.
    //   and copy it to the MeasConfig list
    if (ue->choPreparationMeasConfig)
    {
        auto *mc_saved = asn::New<ASN_RRC_MeasConfig>();
    
        if (!asn::DeepCopy(asn_DEF_ASN_RRC_MeasConfig, *ue->choPreparationMeasConfig, mc_saved))
        {
            asn::Free(asn_DEF_ASN_RRC_MeasConfig, mc_saved);
            mc_saved = nullptr;
            m_logger->err("UE[%d] Failed to deep-copy MeasConfig for local storage", ue->ueId);
        }

        ies->measConfig = ue->choPreparationMeasConfig;
        ue->choPreparationMeasConfig = nullptr; // transfer ownership to the message
        if (mc_saved)
            ue->sentMeasConfigs.push_back({mc_saved, ue->choPreparationMeasIds});
    }

    auto *addMod = asn::New<ASN_RRC_CondReconfigToAddMod>();

    // select a unique condReconfigId
    int condReconfigId = -1;
    if (!allocateUniqueCondReconfigId(ue, condReconfigId))
    {
        m_logger->err("UE[%d] CHO candidate skipped: no free condReconfigId in range %d..%d",
                      ue->ueId,
                      MIN_COND_RECONFIG_ID,
                      MAX_COND_RECONFIG_ID);

        ue->choPreparationCandidateScores.erase(candidatePci);
        if (ue->choPreparationCandidatePcis.empty())
            clearChoPendingState(ue);
        return;
    }

    addMod->condReconfigId = condReconfigId;

    // assign a MeasId to this condReconfig
    //  It will be the MeasId created as part of the 
    addMod->condExecutionCond =
        asn::New<ASN_RRC_CondReconfigToAddMod::ASN_RRC_CondReconfigToAddMod__condExecutionCond>();
    for (long measId : ue->choPreparationMeasIds)
    {
        auto *entry = asn::New<ASN_RRC_MeasId_t>();
        *entry = measId;
        asn::SequenceAdd(*addMod->condExecutionCond, entry);
    }

    // add the transparent container with the nested RRCReconfiguration from the target gNB

    addMod->condRRCReconfig = asn::New<OCTET_STRING_t>();
    asn::SetOctetString(*addMod->condRRCReconfig, nestedRrcReconfig);

    // add this candidate to the message's list of conditional reconfigurations
    asn::SequenceAdd(*v1610->conditionalReconfiguration->condReconfigToAddModList, addMod);

    // send to the UE
    sendRrcMessage(ue->ueId, pdu);

    // free all the ASN structures we created (the nested RRCReconfig is now owned by the message and will be freed with it)
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("UE[%d] CHO RRCReconfiguration sent with 1 candidate targetPCI=%d condReconfigId=%d "
                    "measIds=%zu pendingCandidates=%zu",
                    ue->ueId,
                    candidatePci,
                    condReconfigId,
                    ue->choPreparationMeasIds.size(),
                    ue->choPreparationCandidatePcis.size());

    // remove this candidate from the pending list; if empty, clear the pending state
    ue->choPreparationCandidateScores.erase(candidatePci);
    if (ue->choPreparationCandidatePcis.empty())
    {
        ue->choPreparationPending = false;
        ue->choPreparationCandidateScores.clear();
        ue->choPreparationMeasIds.clear();
        ue->choPreparationCandidateProfileId.reset();
        ue->choPreparationDistanceThreshold = 0;
        ue->choPreparationTriggerTimerSec = 0;
    }

    return;

}

std::vector<HandoverMeasurementIdentity> GnbRrcTask::getHandoverMeasurementIdentities(int ueId) const
{
    std::vector<HandoverMeasurementIdentity> identities{};

    if (ueId <= 0)
        return identities;

    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end() || !it->second)
        return identities;

    auto *ctx = it->second;
    identities.reserve(ctx->usedMeasIdentities.size());
    for (const auto &item : ctx->usedMeasIdentities)
    {
        identities.push_back({item.measId, item.measObjectId, item.reportConfigId, item.eventKind, item.eventType});
    }
    return identities;
}

/**
 * @brief Used by NGAP to collect MeasConfig Information for a handover Command to AMF
 * 
 * @param ueId 
 * @return OctetString 
 */
OctetString GnbRrcTask::getHandoverMeasConfigRrcReconfiguration(int ueId) const
{
    if (ueId <= 0)
        return OctetString{};

    auto it = m_ueCtx.find(ueId);
    const RrcUeContext *ctx = it != m_ueCtx.end() ? it->second : nullptr;

    if (!ctx || ctx->usedMeasIdentities.empty())
        return OctetString{};

    // create dummy payload
    std::vector<uint8_t> buffer(1024);
    for (int i = 0; i < 1024; i++) {
        buffer[i] = static_cast<uint8_t>(i & 0xFF);
    }

    OctetString encoded = OctetString(std::move(buffer));

    // auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    // pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    // pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    // pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    // auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
    //     asn::New<ASN_RRC_RRCReconfiguration>();
    // reconfig->rrc_TransactionIdentifier = 0;
    // reconfig->criticalExtensions.present = ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;

    // auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
    //     asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    // // Mark MeasConfig as full replacement to avoid stale measurement state
    // // when this configuration is transferred across handover contexts.
    // ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    // ies->nonCriticalExtension->fullConfig = asn::New<long>();
    // *ies->nonCriticalExtension->fullConfig =
    //     ASN_RRC_RRCReconfiguration_v1530_IEs__fullConfig_true;

    // ies->measConfig = asn::New<ASN_RRC_MeasConfig>();
    // auto *mc = ies->measConfig;

    // mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
    // auto *measObj = asn::New<ASN_RRC_MeasObjectToAddMod>();
    // measObj->measObjectId = 1;
    // measObj->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
    // measObj->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
    // measObj->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
    // *measObj->measObject.choice.measObjectNR->ssbFrequency = 632628;
    // measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
    // *measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = ASN_RRC_SubcarrierSpacing_kHz30;
    // measObj->measObject.choice.measObjectNR->smtc1 = asn::New<ASN_RRC_SSB_MTC>();
    // measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
    //     ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
    // measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
    // measObj->measObject.choice.measObjectNR->smtc1->duration = ASN_RRC_SSB_MTC__duration_sf1;
    // measObj->measObject.choice.measObjectNR->quantityConfigIndex = 1;
    // asn::SequenceAdd(*mc->measObjectToAddModList, measObj);

    // mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    // mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();

    // auto selectedEvents = m_config->handover.candidateProfiles[0].conditions;

    // for (const auto &item : ctx->usedMeasIdentities)
    // {
    //     const GnbHandoverConfig::GnbHandoverEventConfig *eventConfig = nullptr;
    //     if (item.reportConfigId > 0)
    //     {
    //         size_t eventIndex = static_cast<size_t>(item.reportConfigId - 1);
    //         if (eventIndex < selectedEvents.size())
    //             eventConfig = &selectedEvents[eventIndex];
    //     }
    //     if (!eventConfig)
    //         eventConfig = findEventConfigByType(m_config->handover, item.eventType);
    //     if (!eventConfig || !isRrcMeasEventType(eventConfig->eventType))
    //         continue;

    //     auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
    //     rcMod->reportConfigId = item.reportConfigId;
    //     rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
    //     rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

    //     auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;
    //     rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
    //     rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();
    //     auto *et = rcNR->reportType.choice.eventTriggered;

    //     if (item.eventType == "A2")
    //     {
    //         et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA2;
    //         asn::MakeNew(et->eventId.choice.eventA2);
    //         auto *a2 = et->eventId.choice.eventA2;
    //         a2->a2_Threshold.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
    //         a2->a2_Threshold.choice.rsrp = mtqToASNValue(eventConfig->a2ThresholdDbm);
    //         a2->hysteresis = hysteresisToASNValue(eventConfig->hysteresisDb);
    //         a2->timeToTrigger = tttMsToASNValue(eventConfig->tttMs);
    //         a2->reportOnLeave = false;
    //         et->reportAddNeighMeas = asn::New<long>();
    //         *et->reportAddNeighMeas = ASN_RRC_EventTriggerConfig__reportAddNeighMeas_setup;
    //     }
    //     else if (item.eventType == "A5")
    //     {
    //         et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA5;
    //         asn::MakeNew(et->eventId.choice.eventA5);
    //         auto *a5 = et->eventId.choice.eventA5;
    //         a5->a5_Threshold1.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
    //         a5->a5_Threshold1.choice.rsrp = mtqToASNValue(eventConfig->a5Threshold1Dbm);
    //         a5->a5_Threshold2.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
    //         a5->a5_Threshold2.choice.rsrp = mtqToASNValue(eventConfig->a5Threshold2Dbm);
    //         a5->hysteresis = hysteresisToASNValue(eventConfig->hysteresisDb);
    //         a5->timeToTrigger = tttMsToASNValue(eventConfig->tttMs);
    //         a5->reportOnLeave = false;
    //         a5->useAllowedCellList = false;
    //     }
    //     else if (item.eventType == "D1")
    //     {
    //         et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17;
    //         asn::MakeNew(et->eventId.choice.eventD1_r17);
    //         auto *d1 = et->eventId.choice.eventD1_r17;
    //         d1->distanceThreshFromReference1_r17 = std::max(0, eventConfig->distanceThreshold);
    //         d1->distanceThreshFromReference2_r17 = eventConfig->distanceThreshold2;
    //         d1->reportOnLeave_r17 = false;
    //         d1->hysteresisLocation_r17 = static_cast<long>(std::max(0, eventConfig->hysteresisM));
    //         d1->timeToTrigger = tttMsToASNValue(eventConfig->tttMs);

    //         if (eventConfig->distanceType == "fixed" && eventConfig->referencePosition.has_value())
    //         {
    //             d1->referenceLocation1_r17.present =
    //                 ASN_RRC_EventTriggerConfig__eventId__eventD1_r17__referenceLocation_r17_PR_fixedReferenceLocation_r17;
    //             asn::MakeNew(d1->referenceLocation1_r17.choice.fixedReferenceLocation_r17);
    //             auto *fixed = d1->referenceLocation1_r17.choice.fixedReferenceLocation_r17;
    //             const auto &ref = *eventConfig->referencePosition;
    //             fixed->latitudeSign = ref.latitude >= 0
    //                 ? ASN_RRC_EventTriggerConfig__latitudeSign_north
    //                 : ASN_RRC_EventTriggerConfig__latitudeSign_south;
    //             fixed->degreesLatitude = static_cast<long>(std::fabs(ref.latitude) * 8388608.0 / 90.0);
    //             fixed->degreesLongitude = static_cast<long>(ref.longitude * 16777216.0 / 360.0);
    //         }
    //         else
    //         {
    //             d1->referenceLocation1_r17.present =
    //                 ASN_RRC_EventTriggerConfig__eventId__eventD1_r17__referenceLocation_r17_PR_nadirReferenceLocation_r17;
    //             d1->referenceLocation1_r17.choice.nadirReferenceLocation_r17 = asn::New<NULL_t>();
    //         }

    //         if (eventConfig->distanceType2 == "fixed" && eventConfig->referencePosition2.has_value())
    //         {
    //             d1->referenceLocation2_r17.present =
    //                 ASN_RRC_EventTriggerConfig__eventId__eventD1_r17__referenceLocation_r17_PR_fixedReferenceLocation_r17;
    //             asn::MakeNew(d1->referenceLocation2_r17.choice.fixedReferenceLocation_r17);
    //             auto *fixed2 = d1->referenceLocation2_r17.choice.fixedReferenceLocation_r17;
    //             const auto &ref2 = *eventConfig->referencePosition2;
    //             fixed2->latitudeSign = ref2.latitude >= 0
    //                 ? ASN_RRC_EventTriggerConfig__latitudeSign_north
    //                 : ASN_RRC_EventTriggerConfig__latitudeSign_south;
    //             fixed2->degreesLatitude = static_cast<long>(std::fabs(ref2.latitude) * 8388608.0 / 90.0);
    //             fixed2->degreesLongitude = static_cast<long>(ref2.longitude * 16777216.0 / 360.0);
    //         }
    //         else
    //         {
    //             d1->referenceLocation2_r17.present =
    //                 ASN_RRC_EventTriggerConfig__eventId__eventD1_r17__referenceLocation_r17_PR_nadirReferenceLocation_r17;
    //             d1->referenceLocation2_r17.choice.nadirReferenceLocation_r17 = asn::New<NULL_t>();
    //         }
    //     }
    //     else if (item.eventType == "A3")
    //     {
    //         et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
    //         asn::MakeNew(et->eventId.choice.eventA3);
    //         auto *a3 = et->eventId.choice.eventA3;
    //         a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
    //         a3->a3_Offset.choice.rsrp = eventConfig->a3OffsetDb * 2;
    //         a3->hysteresis = hysteresisToASNValue(eventConfig->hysteresisDb);
    //         a3->timeToTrigger = tttMsToASNValue(eventConfig->tttMs);
    //         a3->reportOnLeave = false;
    //         a3->useAllowedCellList = false;
    //     }
    //     else 
    //     {
    //         m_logger->warn("UE[%d] Unsupported event type '%s' for MeasId %ld; skipping report config",
    //                        ueId,
    //                        item.eventType.c_str(),
    //                        item.measId);
    //         continue;
    //     }

    //     et->rsType = ASN_RRC_NR_RS_Type_ssb;
    //     et->reportInterval = ASN_RRC_ReportInterval_ms480;
    //     et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
    //     et->reportQuantityCell.rsrp = true;
    //     et->reportQuantityCell.rsrq = false;
    //     et->reportQuantityCell.sinr = false;
    //     et->maxReportCells = 4;

    //     asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);

    //     auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
    //     measIdMod->measId = item.measId;
    //     measIdMod->measObjectId = item.measObjectId;
    //     measIdMod->reportConfigId = item.reportConfigId;
    //     asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);
    // }

    // OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    // asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    return encoded;
}

/**
 * @brief Creates an RRCReconfiguration message sent to the UE as part of
 * handover preparation.
 * Returns the transaction id so it can be inserted into pending handover
 * context and matched when the UE completes handover.
 * 
 * @param ueId 
 * @param targetPci 
 * @param newCrnti 
 * @param t304Ms 
 * @param rrcContainer 
 * @return int64_t 
 */
int64_t GnbRrcTask::buildHandoverCommandForTransfer(int ueId, int targetPci, int newCrnti,
                                                    int t304Ms, OctetString &rrcContainer)
{
    int hoCrnti = normalizeCrntiForRrc(newCrnti);
    if (hoCrnti != newCrnti)
    {
            m_logger->warn("UE[%d] Normalizing target handover C-RNTI %d -> %d", ueId, newCrnti, hoCrnti);
    }

    ASN_RRC_ReconfigurationWithSync rws{};
    rws.newUE_Identity = static_cast<long>(hoCrnti);
    rws.t304 = nr::rrc::common::t304MsToEnum(t304Ms);

    ASN_RRC_ServingCellConfigCommon scc{};
    long pci = static_cast<long>(targetPci);
    scc.physCellId = &pci;
    scc.dmrs_TypeA_Position = ASN_RRC_ServingCellConfigCommon__dmrs_TypeA_Position_pos2;
    scc.ss_PBCH_BlockPower = 0;
    rws.spCellConfigCommon = &scc;

    ASN_RRC_SpCellConfig spCellConfig{};
    spCellConfig.reconfigurationWithSync = &rws;

    ASN_RRC_CellGroupConfig cellGroupConfig{};
    cellGroupConfig.cellGroupId = 0;
    cellGroupConfig.spCellConfig = &spCellConfig;

    OctetString masterCellGroupOctet = rrc::encode::EncodeS(asn_DEF_ASN_RRC_CellGroupConfig, &cellGroupConfig);
    if (masterCellGroupOctet.length() == 0)
    {
        m_logger->err("UE[%d] buildHandoverCommandForTransfer: failed CellGroupConfig encode", ueId);
        return -1;
    }

    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration>();

    long txId = getNextTid(ueId);

    reconfig->rrc_TransactionIdentifier = txId;
    reconfig->criticalExtensions.present = ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;
    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->masterCellGroup = asn::New<OCTET_STRING_t>();
    asn::SetOctetString(*ies->nonCriticalExtension->masterCellGroup, masterCellGroupOctet);

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    if (encoded.length() == 0)
    {
        m_logger->err("UE[%d] buildHandoverCommandForTransfer: failed RRCReconfiguration encode", ueId);
        return -1;
    }

    m_logger->info("UE[%d] Target generated handover command with txId=%ld targetPCI=%d newC-RNTI=%d", ueId,
                   txId, targetPci, hoCrnti);
    m_logger->debug("UE[%d] buildHandoverCommandForTransfer: encoded RRCReconfiguration size=%dB txId=%ld",
                    ueId, encoded.length(), txId);

    rrcContainer = std::move(encoded);

    return txId;
}

/**
 * @brief Adds a pending handover context transfer for the specified UE, to be completed when the UE connects
 * after handover. Handover preparation info includes the measurement
 * identities configured for this UE.
 * Also builds the RRCReconfiguration message to be included in the T2S
 * TransparentContainer, which source gNB sends to the UE.
 * 
 * @param ueId 
 * @param handoverPrep
 * @param rrcContainer output parameter for the RRCReconfiguration message to be sent to the UE 
 * @return true - success
 * @return false - failure
 */
bool GnbRrcTask::addPendingHandover(int ueId, const HandoverPreparationInfo &handoverPrep,
                                    OctetString &rrcContainer)
{
    if (ueId <= 0)
        return false;

    // create the new UE RRC context
    auto *ctx = new RrcUeContext(ueId);
    ctx->ueId = ueId;
    ctx->handoverInProgress = true;
    ctx->handoverTargetPci = m_base->config->getCellId();
    ctx->cRnti = allocateCrnti();

    for (const auto &item : handoverPrep.measIdentities)
    {
        ctx->usedMeasIdentities.push_back({item.measId, item.measObjectId, item.reportConfigId,
                                           item.eventKind, item.eventType});
    }

    auto it = m_handoversPending.find(ueId);
    if (it != m_handoversPending.end() && it->second)
    {
        delete it->second->ctx;
        delete it->second;
    }

    int64_t txId = buildHandoverCommandForTransfer(ueId, ctx->handoverTargetPci, ctx->cRnti, 1000, rrcContainer);
    // the command build fails, no handover context should be added
    if (txId < 0) {
        delete ctx;
        return false;
    }

    // add pending handover context, to be completed when the UE connects after handover
    auto *pending = new RRCHandoverPending();
    pending->id = ueId;
    pending->ctx = ctx;
    pending->txId = txId;
    pending->expireTime = utils::CurrentTimeMillis() + HANDOVER_TIMEOUT_MS;
    m_handoversPending[ueId] = pending;

    m_logger->info("UE[%d] Added pending handover transfer with %zu measurement identities", ueId,
                   handoverPrep.measIdentities.size());
    return true;
}

/**
 * @brief Deletes the UE's context due to a successful handover to the target gNB.
 * 
 * @param ueId 
 */
void GnbRrcTask::handoverContextRelease(int ueId)
{
    auto *ctx = findCtxByUeId(ueId);
    if (ctx)
    {
        delete ctx;
        m_ueCtx.erase(ueId);
        m_tidCountersByUe.erase(ueId);
        m_logger->info("UE[%d] RRC context released", ueId);
        return;
    }

    m_logger->warn("UE[%d] handoverContextRelease: context not found", ueId);
}

} // namespace gnb