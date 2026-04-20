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
#include <lib/rrc/common/sat_calc.hpp>
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
using nr::rrc::common::ReportConfigEvent;
using nr::rrc::common::SatEcefState;
using nr::rrc::common::EventReferenceLocation;
using nr::rrc::common::NeighborEndpoint;
using nr::rrc::common::mtqFromASNValue;
using nr::rrc::common::mtqToASNValue;
using nr::rrc::common::hysteresisFromASNValue;
using nr::rrc::common::hysteresisToASNValue;
using nr::rrc::common::referenceLocationToAsnValue;
using nr::rrc::common::tttMsToASNValue;
using nr::rrc::common::distanceThresholdToASNValue;
using nr::rrc::common::t304MsToEnum;




/* ====================================== */
/*      Helper functions                  */
/* ====================================== */


static int calcLatLongDistance(const EventReferenceLocation &pos1,
                        const EventReferenceLocation &pos2)
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

static int64_t DateTimeToUnixMillis(const libsgp4::DateTime &dateTime)
{
    const libsgp4::DateTime unixEpoch(1970, 1, 1, 0, 0, 0);
    auto delta = dateTime - unixEpoch;
    return static_cast<int64_t>(std::llround(delta.TotalMilliseconds()));
}



/**
 * @brief Compute the conditional-handover trigger conditions for the serving gNB.
 *
 */
void GnbRrcTask::calculateTriggerConditions(nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams, const int ownPci, RrcUeContext *ue)
{

    // if in NTN mode, we use the TLE of our own satellite to calculate the trigger conditions.
    if (m_config->ntn.ntnEnabled)
    {

        auto ownTleOpt   = m_base->satTleStore->find(ownPci);
        const int theta  = m_config->ntn.elevationMinDeg;

        if (!ownTleOpt.has_value() || !ue->uePosition.isValid)
        {
            m_logger->warn("UE[%d] Dynamic trigger: own TLE or UE position unavailable, "
                           "using defaults (t=%ds d=%dm)",
                           ue->ueId, dynTriggerParams.condT1_thresholdSec, dynTriggerParams.condD1_distanceThresholdFromReference1);
            return;
        }

        // convert ue position from lat/long/alt to ECEF for the calculation
        EcefPosition ueEcef = GeoToEcef(ue->uePosition);
        const SatTleEntry &ownTle = m_config->ntn.ownTle.value();
        m_logger->debug("UE[%d]: Calculating Dynamic trigger conditions for NTN using TLE: %s / %s",
                         ue->ueId, ownTle.line1.c_str(), ownTle.line2.c_str());


        static constexpr int MAX_LOOKAHEAD_SEC = 7200; // 2-hour horizon

        // get current sat time
        libsgp4::DateTime now = sat_time::Now();
        const int satNowSec = static_cast<int>(DateTimeToUnixMillis(now) / 1000);

        // get the Ecef of the current location
        SatEcefState curState{};
        if (!nr::rrc::common::PropagateTleToEcef(ownTle.line1, ownTle.line2, now, curState))
            return;

        // If already below the threshold right now, set trigger for right now
        if (ElevationAngleDeg(ueEcef, curState.pos) < static_cast<double>(theta))
        {
            dynTriggerParams.condT1_thresholdSec = satNowSec;
            dynTriggerParams.condT1_durationSec = 100;
            {
                auto refGeo = EcefToGeo(curState.pos);
                dynTriggerParams.condD1_referenceLocation1 = {refGeo.latitude, refGeo.longitude};
            }
            dynTriggerParams.condD1_distanceThresholdFromReference1 = static_cast<int>(EcefDistance(ueEcef, curState.nadir));
            dynTriggerParams.condD1_referenceLocation2 = {ue->uePosition.latitude, ue->uePosition.longitude};
            dynTriggerParams.condD1_distanceThresholdFromReference2 = 0;
            dynTriggerParams.condD1_hysteresisLocation = 10;

            dynTriggerParams.d1_distanceThresholdFromReference1 = dynTriggerParams.condD1_distanceThresholdFromReference1;
            dynTriggerParams.d1_referenceLocation1 = dynTriggerParams.condD1_referenceLocation1;
            dynTriggerParams.d1_distanceThresholdFromReference2 = dynTriggerParams.condD1_distanceThresholdFromReference2;
            dynTriggerParams.d1_referenceLocation2 = dynTriggerParams.condD1_referenceLocation2;
            dynTriggerParams.d1_hysteresisLocation = dynTriggerParams.condD1_hysteresisLocation;

            return;
        }

        // find the exit time (t_exit) and the nadir distance at exit (nadirAtExitM) by propagating the 
        //  TLE forward in time until the elevation drops below the threshold
        EcefPosition nadirAtExitM{};
        int tExit = nr::rrc::common::FindExitTimeSec(ownTle.line1,
                                                    ownTle.line2,
                                                    ueEcef,
                                                    now,
                                                    0,
                                                    theta,
                                                    MAX_LOOKAHEAD_SEC,
                                                    nadirAtExitM);

        dynTriggerParams.condT1_thresholdSec = satNowSec + tExit;
        dynTriggerParams.condT1_durationSec = 100;

        // d1 is comparing UE pos to nadir pos at exit, and if over threhold, that means teh sat is beyond the elevation angle threshold
        {
            auto refGeo = EcefToGeo(nadirAtExitM);
            dynTriggerParams.condD1_referenceLocation1 = {refGeo.latitude, refGeo.longitude};
        }
        dynTriggerParams.condD1_distanceThresholdFromReference1 = static_cast<int>(EcefDistance(ueEcef, nadirAtExitM));
        // d2 we set to dummy vals that will always be true, since we just case about triggering the handover once the sat is beyond the elevation threshold
        dynTriggerParams.condD1_referenceLocation2 = {ue->uePosition.latitude, ue->uePosition.longitude};
        dynTriggerParams.condD1_distanceThresholdFromReference2 = 1;
        dynTriggerParams.condD1_hysteresisLocation = 10;

        dynTriggerParams.d1_distanceThresholdFromReference1 = dynTriggerParams.condD1_distanceThresholdFromReference1;
        dynTriggerParams.d1_referenceLocation1 = dynTriggerParams.condD1_referenceLocation1;
        dynTriggerParams.d1_distanceThresholdFromReference2 = dynTriggerParams.condD1_distanceThresholdFromReference2;
        dynTriggerParams.d1_referenceLocation2 = dynTriggerParams.condD1_referenceLocation2;
        dynTriggerParams.d1_hysteresisLocation = dynTriggerParams.condD1_hysteresisLocation;

        return;
    }
    // for non-NTN mode, we use gnb location and a fixed distance threshold to calculate the trigger conditions.
    else
    {
        // obtain gnb location
        EventReferenceLocation gnbPos{};
        {
            // lock mutex to safely read gNB position
            std::lock_guard<std::mutex> lock(m_base->gnbPositionMutex);
            gnbPos.latitudeDeg = m_base->gnbPosition.latitude;
            gnbPos.longitudeDeg = m_base->gnbPosition.longitude;
        }
        
        m_logger->debug("UE[%d]: Calculating Dynamic trigger conditions for non-NTN using gNB location (lat=%.4f, long=%.4f)",
                         ue->ueId, gnbPos.latitudeDeg, gnbPos.longitudeDeg);
        
        // for timer, use config values as deltas, add to current time
        dynTriggerParams.condT1_thresholdSec +=  utils::CurrentTimeMillis() / 1000;

        // d1 is comparing UE pos to gNB pos, and if over threshold, that means the UE is far enough from the gNB to trigger the handover
        dynTriggerParams.condD1_referenceLocation1 = gnbPos;
        // distance threshold is default set in config
        dynTriggerParams.condD1_referenceLocation2 = {ue->uePosition.latitude, ue->uePosition.longitude};
        dynTriggerParams.condD1_distanceThresholdFromReference2 = 1;
        dynTriggerParams.condD1_hysteresisLocation = 10;

        dynTriggerParams.d1_referenceLocation1 = dynTriggerParams.condD1_referenceLocation1;
        // distance threshold is default set in config
        dynTriggerParams.d1_distanceThresholdFromReference2 = dynTriggerParams.condD1_distanceThresholdFromReference2;
        dynTriggerParams.d1_referenceLocation2 = dynTriggerParams.condD1_referenceLocation2;
        dynTriggerParams.d1_hysteresisLocation = dynTriggerParams.condD1_hysteresisLocation;

        return;
    }

}


static int selectChoCandidateProfile(
    const GnbHandoverConfig &config, Logger *logger)
{
    if (!config.choEnabled)
        return -1;

    // Find the profile whose candidateProfileId matches choDefaultProfileId
    for (size_t i = 0; i < config.candidateProfiles.size(); ++i)
    {
        if (config.candidateProfiles[i].candidateProfileId == config.choDefaultProfileId)
            return static_cast<int>(i);
    }
    if (!config.candidateProfiles.empty()) {
        if (logger)
            logger->warn("CHO profile with candidateProfileId=%d not found; using first profile.", config.choDefaultProfileId);
        return 0;
    }
    if (logger)
        logger->warn("No CHO candidate profiles defined.");
    return -1;
}




static const ReportConfigEvent *findEventConfigByKind(
    const GnbHandoverConfig &config, HandoverEventType eventType)
{
    for (const auto &event : config.events)
    {
        if (event.eventKind == eventType)
            return &event;
    }
    return nullptr;
}

[[maybe_unused]] static const ReportConfigEvent *findEventConfigByType(
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
 *      transit time = t_exit_new - T_exit.  Longer transit → higher (better) score.
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
    // Helper: return first non-serving neighbor as a fallback.
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

    const bool hasUePos = (ueEcef.x != 0.0 || ueEcef.y != 0.0 || ueEcef.z != 0.0);
    if (!hasUePos || neighborhoodCache.empty())
        return fallback();

    static constexpr int MAX_LOOKAHEAD_SEC = 7200;
    const libsgp4::DateTime now = sat_time::Now();

    auto stateResolver = [&](int pci, int offsetSec, SatEcefState &state) -> bool {
        auto tleOpt = tleStore.find(pci);
        if (!tleOpt.has_value())
            return false;
        return nr::rrc::common::PropagateTleToEcef(tleOpt->line1, tleOpt->line2, now.AddSeconds(offsetSec), state);
    };

    auto endpointResolver = [&](int pci, NeighborEndpoint &endpoint) -> bool {
        for (const auto &nb : neighborList)
        {
            if (nb.getPci() != pci)
                continue;

            if (nb.ipAddress.empty())
                return false;

            endpoint.ipAddress = nb.ipAddress;
            endpoint.port = nb.xnPort.value_or(0);
            return true;
        }
        return false;
    };

    const auto ranked = nr::rrc::common::PrioritizeTargetSats(servingPci,
                                                               neighborhoodCache,
                                                               ueEcef,
                                                               tExitSec,
                                                               elevationMinDeg,
                                                               MAX_LOOKAHEAD_SEC,
                                                               stateResolver,
                                                               endpointResolver);
    if (ranked.empty())
        return fallback();

    std::vector<ScoredNeighbor> prioritized{};
    prioritized.reserve(ranked.size());

    for (const auto &[pci, score] : ranked)
    {
        for (const auto &nb : neighborList)
        {
            if (nb.getPci() == pci)
            {
                prioritized.emplace_back(&nb, score);
                break;
            }
        }
    }

    if (prioritized.empty())
        return fallback();

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
                servingRsrp = mtqFromASNValue(*ssbCell->rsrp);
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
                        rsrp = mtqFromASNValue(*ssbCell->rsrp);

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
    rws.t304 = t304MsToEnum(t304Ms);

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
 * @param ue The UE RRC context
 * @param selectedEvents The list of selected handover events
 * @param dynTriggerParams allows the caller to override certain event trigger parameters when building the event.
 * @param isChoRequest if true, creates a MeasConfig suitable for a Conditional Handover procedure, using the CHO-specific ASN types.  If false, creates a default MeasConfig using normal "eventTriggered" reporting events.
 * @param choProfileId if isChoRequest is true, the CHO candidate profile ID to use when building the CHO-specific MeasConfig.  Ignored if isChoRequest is false.
 * @return ASN_RRC_MeasConfig* 
 */
std::vector<long> GnbRrcTask::createMeasConfig(
    ASN_RRC_MeasConfig *&mc,
    RrcUeContext *ue,
    std::vector<ReportConfigEvent> selectedEvents,
    const nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams,
    int choProfileId
)
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
        bool cho_event = !isEventTriggerEventType(event.eventKind);
        const auto eventKind = event.eventKind;
        const auto eventType = nr::rrc::common::HandoverEventTypeToString(eventKind);
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
        if (!cho_event)
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
                a2->a2_Threshold.choice.rsrp = mtqToASNValue(event.a2_thresholdDbm);
                a2->hysteresis = hysteresisToASNValue(event.a2_hysteresisDb);
                a2->timeToTrigger = tttMsToASNValue(event.ttt);
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
                a3->hysteresis = hysteresisToASNValue(event.a3_hysteresisDb);
                a3->timeToTrigger = tttMsToASNValue(event.ttt);
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
                a5->a5_Threshold1.choice.rsrp = mtqToASNValue(event.a5_threshold1Dbm);
                a5->a5_Threshold2.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
                a5->a5_Threshold2.choice.rsrp = mtqToASNValue(event.a5_threshold2Dbm);
                a5->hysteresis = hysteresisToASNValue(event.a5_hysteresisDb);
                a5->timeToTrigger = tttMsToASNValue(event.ttt);
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

                // use dynamic trigger params for D1 since distance values can change with movement
                d1->distanceThreshFromReference1_r17 =
                    distanceThresholdToASNValue(dynTriggerParams.d1_distanceThresholdFromReference1);
                d1->distanceThreshFromReference2_r17 =
                    distanceThresholdToASNValue(dynTriggerParams.d1_distanceThresholdFromReference2);

                referenceLocationToAsnValue(dynTriggerParams.d1_referenceLocation1, d1->referenceLocation1_r17);
                referenceLocationToAsnValue(dynTriggerParams.d1_referenceLocation2, d1->referenceLocation2_r17);
                d1->hysteresisLocation_r17 = nr::rrc::common::hysteresisLocationToASNValue(dynTriggerParams.d1_hysteresisLocation);

                d1->reportOnLeave_r17 = true;
                d1->timeToTrigger = tttMsToASNValue(event.ttt);

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=D1 distThresh1=%dm distThresh2=%dm "
                                "hysteresis=%dm "
                                "ttt=%s",
                                ue->ueId,
                                measId,
                                dynTriggerParams.d1_distanceThresholdFromReference1,
                                dynTriggerParams.d1_distanceThresholdFromReference2,
                                dynTriggerParams.d1_hysteresisLocation,
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
                a3->hysteresis = hysteresisToASNValue(event.a3_hysteresisDb);
                a3->timeToTrigger = tttMsToASNValue(event.ttt);

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventA3 offset=%ddB hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.a3_offsetDb, event.a3_hysteresisDb, nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }

            else if (eventKind == HandoverEventType::CondD1)
            {
                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventD1_r17;
                asn::MakeNew(ctc->condEventId.choice.condEventD1_r17);

                auto *d1 = ctc->condEventId.choice.condEventD1_r17;

                // set trigger params from provided Trigger object
                d1->distanceThreshFromReference1_r17 = distanceThresholdToASNValue(dynTriggerParams.condD1_distanceThresholdFromReference1);
                d1->distanceThreshFromReference2_r17 = distanceThresholdToASNValue(dynTriggerParams.condD1_distanceThresholdFromReference2);
                referenceLocationToAsnValue(dynTriggerParams.condD1_referenceLocation1, d1->referenceLocation1_r17);
                referenceLocationToAsnValue(dynTriggerParams.condD1_referenceLocation2, d1->referenceLocation2_r17);
                d1->hysteresisLocation_r17 = nr::rrc::common::hysteresisLocationToASNValue(dynTriggerParams.condD1_hysteresisLocation);

                // use config for ttt
                d1->timeToTrigger_r17 = tttMsToASNValue(event.ttt);

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventD1 distThresh1=%dm distThresh2=%dm "
                                "hysteresis=%dm "
                                "ttt=%s",
                                ue->ueId,
                                measId,
                                dynTriggerParams.condD1_distanceThresholdFromReference1,
                                dynTriggerParams.condD1_distanceThresholdFromReference2,
                                dynTriggerParams.condD1_hysteresisLocation,
                                nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::CondT1)
            {
                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventT1_r17;
                asn::MakeNew(ctc->condEventId.choice.condEventT1_r17);

                auto *t1 = ctc->condEventId.choice.condEventT1_r17;
                // set trigger params from provided Trigger object
                t1->t1_Threshold_r17 = nr::rrc::common::t1ThresholdToASNValue(dynTriggerParams.condT1_thresholdSec);
                t1->duration_r17 = nr::rrc::common::durationToASNValue(dynTriggerParams.condT1_durationSec);

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventT1 threshold=%dsec duration=%dsec",
                                ue->ueId, measId, dynTriggerParams.condT1_thresholdSec, dynTriggerParams.condT1_durationSec);
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
            cho_event ? choProfileId : 0   // used to mark events as associated with a CHO candidiate
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

    // find all the events that need to be included in the reportConfig

    auto selectedEvents = m_config->handover.events;
    if (selectedEvents.empty())
    {
        m_logger->warn("UE[%d] No non-CHO handover events configured", ue->ueId);
       
    }

    // check for CHO enabled, and add CHO events to the event list
    bool do_cho = m_config->handover.choEnabled;
    int choProfileIdx = -1;
    if (do_cho)
    {
        choProfileIdx = selectChoCandidateProfile(m_config->handover, m_logger.get());
        if (choProfileIdx < 0)
        {
            m_logger->warn("UE[%d] CHO enabled but no CHO candidate profiles index found; skipping", 
                        ueId);
            do_cho = false;
        }

        // get the conditiuons
        auto choProfile = m_config->handover.candidateProfiles[choProfileIdx];
        if (choProfile.conditions.empty())
        {
            m_logger->warn("UE[%d] CHO enabled but selected profile has no events; skipping CHO", 
                        ueId);
            do_cho = false;
        }
        else
        {
            // add the CHO events to the list of events to configure in the MeasConfig
            selectedEvents.insert(selectedEvents.end(), choProfile.conditions.begin(), choProfile.conditions.end());
        }
    }

    if (selectedEvents.empty())
    {
        m_logger->warn("UE[%d] No handover events to configure after checking CHO conditions, exiting MeasConfig",
             ue->ueId);
        return;
    }

    bool need_location = false;
    bool need_time = false;
    std::string eventList{};
    for (size_t i = 0; i < selectedEvents.size(); i++)
    {
        if (selectedEvents[i].eventKind == HandoverEventType::CondD1 || selectedEvents[i].eventKind == HandoverEventType::D1)
            need_location = true;

        if (selectedEvents[i].eventKind == HandoverEventType::CondT1)
            need_time = true;

        if (i != 0)
            eventList += ",";
        eventList += nr::rrc::common::HandoverEventTypeToString(selectedEvents[i].eventKind);
    }

    m_logger->info("UE[%d] Sending MeasConfig with eventType(s)=%s", ue->ueId, eventList.c_str());

    // Determine handover trigger conditions

    // Here is where we call the satellite position calculations to determine the
    // time and distance when this gnb will be out of range of teh UE, and use that to set the tServiceSec and the distance threshold for D1.
    // We need this now to determine the time to use for evaluating which gnbs are in range

    const int ownPci = cons::getPciFromNci(m_config->nci);

    // create the dynamic trigger parameters object and load with config values
    //   as defaults
    nr::rrc::common::DynamicEventTriggerParams dynTriggerParams{};
    for (const auto &event : selectedEvents)
    {
        if (event.eventKind == HandoverEventType::D1)
        {
            dynTriggerParams.d1_distanceThresholdFromReference1 = event.d1_distanceThreshFromReference1;
            dynTriggerParams.d1_distanceThresholdFromReference2 = event.d1_distanceThreshFromReference2;
            dynTriggerParams.d1_referenceLocation1 = event.d1_referenceLocation1;
            dynTriggerParams.d1_referenceLocation2 = event.d1_referenceLocation2;
            dynTriggerParams.d1_hysteresisLocation = event.d1_hysteresisLocation;
        }
        else if (event.eventKind == HandoverEventType::CondD1)
        {
            dynTriggerParams.condD1_distanceThresholdFromReference1 = event.condD1_distanceThreshFromReference1;
            dynTriggerParams.condD1_distanceThresholdFromReference2 = event.condD1_distanceThreshFromReference2;
            dynTriggerParams.condD1_referenceLocation1 = event.condD1_referenceLocation1;
            dynTriggerParams.condD1_referenceLocation2 = event.condD1_referenceLocation2;
            dynTriggerParams.condD1_hysteresisLocation = event.condD1_hysteresisLocation;
        }
        else if (event.eventKind == HandoverEventType::CondT1)
        {
            dynTriggerParams.condT1_thresholdSec = event.condT1_thresholdSecTS;
            dynTriggerParams.condT1_durationSec = event.condT1_durationSec;
        }
    }
    
    // only run the dynamic parameters if we need location of time triggers
    if (need_location || need_time)
        calculateTriggerConditions(dynTriggerParams, ownPci, ue);

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

    auto usedMeasIds = createMeasConfig(mc, ue, selectedEvents, dynTriggerParams, choProfileIdx);
    ies->measConfig = mc;
    
    if (!asn::DeepCopy(asn_DEF_ASN_RRC_MeasConfig, *mc, mc_saved))
    {
        asn::Free(asn_DEF_ASN_RRC_MeasConfig, mc_saved);
        mc_saved = nullptr;
        m_logger->err("UE[%d] Failed to deep-copy MeasConfig for local storage", ue->ueId);
    }

    // send the measConfig now, don't wait for CHO conditionals since they come from other GNBs
    sendRrcMessage(ue->ueId, pdu);

    // store the sent MeasConfig in UE context for potential future reference (e.g. handovers)
    if (mc_saved)
        ue->sentMeasConfigs.emplace_back(mc_saved, usedMeasIds);


    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("UE[%d] MeasConfig (%zu measId entries) sent, txId=%ld",
                   ue->ueId, usedMeasIds.size(), txId);

    // if conditional handover enabled, see if wee need to generate a CHO RRC message
    if (do_cho)
        processConditionalHandover(ue->ueId, dynTriggerParams, choProfileIdx);
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
        int hysteresisDb = hysteresisFromASNValue(eventConfig->a2_hysteresisDb);
        int thresholdDbm = mtqFromASNValue(eventConfig->a2_thresholdDbm);
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
        int hysteresisDb = hysteresisFromASNValue(eventConfig->a3_hysteresisDb);
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
        int threshold1Dbm = mtqFromASNValue(eventConfig->a5_threshold1Dbm);
        int threshold2Dbm = mtqFromASNValue(eventConfig->a5_threshold2Dbm);
        int hysteresisDb = hysteresisFromASNValue(eventConfig->a5_hysteresisDb);
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

        EventReferenceLocation rl1 = eventConfig->d1_referenceLocation1;
        EventReferenceLocation rl2 = eventConfig->d1_referenceLocation2;

        EventReferenceLocation ue_pos = {
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
void GnbRrcTask::processConditionalHandover(int ueId, const nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams, int choProfileIdx)
{
    // ue RRC context
    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
        return;

    // if we're still waiting to complete a prior CHO process, don't start another one
    if (ue->choPreparationPending)
        return;
    
    const int ownPci = cons::getPciFromNci(m_config->nci);
    
    // identify neighbor cells that are possible handover targets.

    // Take a snapshot of the runtime neighbor store once; all pointer lookups below
    // remain valid for the lifetime of this stack frame.
    const auto neighborSnapshot = m_base->neighbors->getAll();

    // prioritizeNeighbors returns a sorted vector of (neighborCell, score) pairs
    //   (higher score = higher priority = longer transit time above theta_e)
    auto prioritizedNeighbors = prioritizeNeighbors(
        neighborSnapshot, ownPci,
        m_satNeighborhoodCache,
        *m_base->satTleStore,
        ue->uePosition.isValid
            ? EcefPosition{ue->uePosition}
            : EcefPosition{},
        dynTriggerParams.condT1_thresholdSec,
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
            m_logger->warn("UE[%d] CHO prepare skipped: targetCellId=0x%llx not in neighborList",
                           ueId,
                           static_cast<long long>(*explicitTargetPci));
            return;
        }

        prioritizedNeighbors.clear();
        prioritizedNeighbors.emplace_back(targetNeighbor, 1);
    }

    if (prioritizedNeighbors.empty())
    {
        m_logger->warn("UE[%d] CHO prepare skipped: neighbor list is empty", ueId);
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
        m_logger->warn("UE[%d] CHO prepare skipped: prioritized neighbors have no valid score", ueId);
        return;
    }

    // loop through MeadIds to extract ones associated with the CHO events, and store those in the UE context for reference when we get the CHO response from NGAP
    std::vector<long> usedMeasIds{};
    for (const auto &measIdEntry : ue->usedMeasIdentities)
    {
        if (measIdEntry.choProfileId == choProfileIdx)
        {
            usedMeasIds.push_back(measIdEntry.measId);
        }
    }
    ue->choPreparationMeasIds = usedMeasIds;

    // Send handover required to NGAP with CHO preparation flag for each candidate,
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

    std::string measIdsStr;
    for (size_t i = 0; i < ue->choPreparationMeasIds.size(); ++i) {
        if (i > 0) measIdsStr += ", ";
        measIdsStr += std::to_string(ue->choPreparationMeasIds[i]);
    }
    std::string pcisStr;
    for (size_t i = 0; i < ue->choPreparationCandidatePcis.size(); ++i) {
        if (i > 0) pcisStr += ", ";
        pcisStr += std::to_string(ue->choPreparationCandidatePcis[i]);
    }
    m_logger->info("UE[%d] CHO prepare send to NGAP: measIds=[%s] total_candidates=%zu targetPCIs=[%s]",
                   ue->ueId,
                   measIdsStr.c_str(),
                   ue->choPreparationCandidatePcis.size(),
                   pcisStr.c_str()
    );
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

    
    // Create RRCReconfiguration message with the nested RRCReconfiguration from the target gNB, 
    //  and include the CHO conditional reconfiguration IEs with the candidate PCI and the MeasIds that 
    //  should trigger execution of this candidate's CHO config.
    
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();
    int txId = getNextTid(ue->ueId);
    reconfig->rrc_TransactionIdentifier = txId;
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

    // // if CHO measConfig has not be sent to UE yet, send it in this RRCreconfig.
    // //   and copy it to the MeasConfig list
    // if (ue->choPreparationMeasConfig)
    // {
    //     auto *mc_saved = asn::New<ASN_RRC_MeasConfig>();
    
    //     if (!asn::DeepCopy(asn_DEF_ASN_RRC_MeasConfig, *ue->choPreparationMeasConfig, mc_saved))
    //     {
    //         asn::Free(asn_DEF_ASN_RRC_MeasConfig, mc_saved);
    //         mc_saved = nullptr;
    //         m_logger->err("UE[%d] Failed to deep-copy MeasConfig for local storage", ue->ueId);
    //     }

    //     ies->measConfig = ue->choPreparationMeasConfig;
    //     ue->choPreparationMeasConfig = nullptr; // transfer ownership to the message
    //     if (mc_saved)
    //         ue->sentMeasConfigs.push_back({mc_saved, ue->choPreparationMeasIds});
    // }

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

    // assign MeasId(s) to this condReconfig
    //  It will be the MeasId(s) created as part of the earlier MeasConfig
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

    std::string measIdsStr;
    for (size_t i = 0; i < ue->choPreparationMeasIds.size(); ++i) {
        if (i > 0) measIdsStr += ", ";
        measIdsStr += std::to_string(ue->choPreparationMeasIds[i]);
    }


    m_logger->info("UE[%d] CHO RRCReconfiguration sent to UE with 1 candidate targetPCI=%d condReconfigId=%d "
                    "measIds=[%s] pendingCandidates=%zu txId=%d",
                    ue->ueId,
                    candidatePci,
                    condReconfigId,
                    measIdsStr.c_str(),
                    ue->choPreparationCandidatePcis.size(),
                    txId);

    // remove this candidate from the pending list; if empty, clear the pending state
    ue->choPreparationCandidateScores.erase(candidatePci);
    if (ue->choPreparationCandidatePcis.empty())
    {
        ue->choPreparationPending = false;
        ue->choPreparationCandidateScores.clear();
        ue->choPreparationMeasIds.clear();
        ue->choPreparationCandidateProfileId.reset();
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
    rws.t304 = t304MsToEnum(t304Ms);

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

    m_logger->info("UE[%d] Target generated handover command with txId=%ld targetPCI=%d newCRNTI=%d", ueId,
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
    ctx->handoverTargetPci = cons::getPciFromNci(m_config->nci);
    ;
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