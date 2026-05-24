//
// gNB-side handover support additions.
//
// Implements:
//   receiveRrcReconfigurationComplete()  – handle UE's RRCReconfigurationComplete on target gNB
//   receiveMeasurementReport()           – receive UE MeasurementReport and determine whether to initiate handover
//   sendUeHandoverMessage()                – build RRCReconfiguration with
//                                          ReconfigurationWithSync and send to UE
//   handleHandoverComplete()             – post-handover processing (logging, NGAP notify)
//   sendMeasConfig()                     – send measurement configuration to UE in RRCReconfiguration message
//   evaluateHandoverDecision()           – decide whether to initiate handover
//   handleNgapHandoverCommand()          – receive notification of NGAP Handover Command, send 
//                                          embedded RRCReconfiguration container to UE
//

#include "task.hpp"

#include <gnb/neighbors.hpp>
#include <gnb/ngap/task.hpp>
#include <gnb/sat_time.hpp>

#include <lib/sat/sat_calc.hpp>
#include <lib/sat/sat_state.hpp>
#include <lib/sat/sat_time.hpp>

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <utils/common.hpp>

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
#include <asn/rrc/ASN_RRC_CellsToAddMod.h>
#include <asn/rrc/ASN_RRC_CellsToAddModList.h>
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
#include <asn/rrc/ASN_RRC_HandoverPreparationInformation.h>
#include <asn/rrc/ASN_RRC_HandoverPreparationInformation-IEs.h>
#include <cmath>
#include <utility>

#include <libsgp4/DateTime.h>

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
using nr::rrc::common::MeasObject;
using nr::sat::SatEcefState;
using nr::sat::EcefPosition;
using nr::sat::ComputeNadir;
using nr::rrc::common::EventReferenceLocation;
using nr::sat::NeighborEndpoint;
using nr::rrc::common::mtqFromASNValue;
using nr::rrc::common::mtqToASNValue;
using nr::rrc::common::hysteresisFromASNValue;
using nr::rrc::common::hysteresisToASNValue;
using nr::rrc::common::referenceLocationToAsnValue;
using nr::rrc::common::tttMsToASNValue;
using nr::rrc::common::distanceThresholdToASNValue;
using nr::rrc::common::t304MsToEnum;
using nr::rrc::common::IsMeasurementEvent;
using nr::rrc::common::IsConditionalEvent;



/* ====================================== */
/*      Helper functions                  */
/* ====================================== */


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





static const GnbNeighborState *findNeighborByNci(const std::vector<GnbNeighborState> &neighborList, int64_t nci)
{
    for (const auto &neighbor : neighborList)
    {
        if (neighbor.nci == nci)
            return &neighbor;
    }

    return nullptr;
}

/**
 * @brief Identify and rank neighbor cells as conditional-handover candidates.
 *
 * For each NCI in the 50-satellite neighborhood cache, this function:
 *   1. Skips NCIs not present in the runtime neighbor store (no signaling path).
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
 * @param servingNci        NCI of the serving (own) satellite gNB
 * @param ueEcef            UE ECEF position (meters); zero vector if unknown
 * @param tExitSec          T_exit: seconds from now when serving gNB exits coverage
 * @param elevationMinDeg   Minimum elevation angle threshold (integer degrees)
 * @return Sorted vector of (GnbNeighborState*, score), best candidate first
 */
std::vector<ScoredNeighbor> GnbRrcTask::prioritizeNeighbors(
    const std::vector<GnbNeighborState> &neighborList,
    int64_t servingNci,
    const EcefPosition &ueEcef,
    int tExitSec)
{
    // Helper: return first non-serving neighbor as a fallback.
    auto fallback = [&]() -> std::vector<ScoredNeighbor> {
        std::vector<ScoredNeighbor> fb{};
        for (const auto &nb : neighborList)
        {
            if (nb.getNci() != servingNci)
            {
                fb.emplace_back(&nb, 1);
                break;
            }
        }
        return fb;
    };

    const bool hasUePos = (ueEcef.x != 0.0 || ueEcef.y != 0.0 || ueEcef.z != 0.0);
    if (!hasUePos || m_satNeighborhoodCache.empty())
        return fallback();

    static constexpr int MAX_LOOKAHEAD_SEC = 7200;

    const auto ranked = m_base->satStates->PrioritizeTargetSats(m_satNeighborhoodCache,
                                                               ueEcef,
                                                               tExitSec,
                                                               m_config->ntn.elevationMinDeg,
                                                               MAX_LOOKAHEAD_SEC);
                                                               
    if (ranked.empty())
        return fallback();

    std::vector<ScoredNeighbor> prioritized{};
    prioritized.reserve(ranked.size());

    for (const auto &[nci, score] : ranked)
    {
        for (const auto &nb : neighborList)
        {
            if (nb.getNci() == nci)
            {
                prioritized.emplace_back(ScoredNeighbor(&nb, score));
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

static bool extractTargetNciFromNestedRrcReconfiguration(const OctetString &nestedRrcReconfiguration,
                                                         int &targetNci)
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
                    targetNci = static_cast<int64_t>(
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
void GnbRrcTask::receiveRrcReconfigurationComplete(int64_t ueId, int cRnti,
    const ASN_RRC_RRCReconfigurationComplete &msg)
{
    int64_t txId = msg.rrc_TransactionIdentifier;

    int64_t resolvedUeId = ueId;

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
                        "RRCReconfigurationComplete UE remap: incomingUeId=%ld resolvedUeId=%ld txId=%ld cRnti=%d",
                        ueId, resolvedUeId, txId, cRnti);
                }
                break;
            }
        }
    }

    m_logger->debug("UE[%ld]: RRCReconfigurationComplete received with txId=%ld cRnti=%d matchedPendingHandover=%s",
                    ueId, txId, cRnti, matchedPending ? "true" : "false");

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
            releaseCrnti(ue->cRnti);
            delete ue;
            m_ueCtx.erase(resolvedUeId);
        }

        // move the UE context from pending handover to UE context map and erase the pending handover
        m_ueCtx[resolvedUeId] = handoverCtx;
        m_handoversPending.erase(itPending);

        // not sure if this is still needed, but clean it up anyway
        handoverCtx->handoverInProgress = false;

        // Send measurement config to UE to restart measurement reporting
        // after handover.
        sendMeasConfig(resolvedUeId, true);

        // Notify NGAP of handover completion.
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_NOTIFY);
        w->ueId = resolvedUeId;
        m_base->ngapTask->push(std::move(w));

        m_logger->info("UE[%ld] Handover completed. NGAP layer notification sent.", resolvedUeId);
        return;

    }

    // other RRCReconfigComplete msgs

    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
    {
        m_logger->warn("UE[%ld] RRCReconfigurationComplete received from unknown UE, ignoring", ueId);
        return;
    }

    // no gnb action needed for non-handover RRCReconfigurationComplete, just log it

    m_logger->info("UE[%ld] RRCReconfigurationComplete received txId=%ld", ueId, txId);

}



/**
 * @brief Creates and sends an RRCReconfiguration message with ReconfigurationWithSync IE to UE, which
 * instructs UE to handover to the target NCI, using the new C-RNTI and T304 timer specified.
 * Note - in real implementations this would be forwarding a RWS message created by the targetGNB.  Since 
 * the only parameters we need to pass over are the targetNCI, cRNTI and T304 value, we just
 * make it here from the values provided in the target's transparent container.
 * 
 * @param ueId 
 * @param targetNci 
 * @param newCrnti 
 * @param t304Ms 
 */
void GnbRrcTask::sendUeHandoverMessage(int64_t ueId, int64_t targetNci, int newCrnti, int t304Ms)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->err("Cannot send handover command: UE[%ld] not found", ueId);
        return;
    }

    m_logger->info("UE[%ld] Sending handover command targetNCI=%ld newC-RNTI=%d t304=%dms",
                   ueId, targetNci, newCrnti, t304Ms);

    int hoCrnti = normalizeCrntiForRrc(newCrnti);
    if (hoCrnti != newCrnti)
    {
        m_logger->warn("UE[%ld] Normalizing handover C-RNTI %d -> %d", ueId, newCrnti, hoCrnti);
    }

    // ---- Build CellGroupConfig with ReconfigurationWithSync ----
    ASN_RRC_ReconfigurationWithSync rws{};
    rws.newUE_Identity = static_cast<long>(hoCrnti);
    rws.t304 = t304MsToEnum(t304Ms);

    // Set target cell NCI via spCellConfigCommon
    ASN_RRC_ServingCellConfigCommon scc{};
    long nci = static_cast<long>(targetNci);
    scc.physCellId = &nci;
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
        m_logger->err("UE[%ld] Failed to encode CellGroupConfig for handover command", ueId);
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

    long txId = ue->getNextTid();
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
    ue->handoverTargetNci = targetNci;
    ue->handoverNewCrnti = hoCrnti;
    ue->handoverTxId = txId;

    sendRrcMessage(ueId, pdu);

    m_logger->info("UE[%ld] RRCReconfiguration (handover) sent to UE, txId=%ld", ueId, txId);
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
void GnbRrcTask::processConditionalHandover(int64_t ueId, const nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams, int choProfileIdx)
{
    // ue RRC context
    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
        return;

    // per-profile pending guard: don't restart preparation for a profile already in flight
    auto &prepState = ue->choPreparations[choProfileIdx];
    if (prepState.pending)
        return;

    const int64_t ownNci = m_config->nci;
    const auto &profile = m_config->handover.candidateProfiles[choProfileIdx];

    // Take a snapshot of the runtime neighbor store once; all pointer lookups below
    // remain valid for the lifetime of this stack frame.
    const auto neighborSnapshot = m_base->neighbors->getAll();

    // Get UE current position from global storage
    auto uePos = m_base->getUePosition(ue->ueId);
    EcefPosition ueEcefPos{};
    if (uePos.has_value())
    {
        ueEcefPos = nr::sat::GeoToEcef(uePos.value());
    }
    else
    {
        m_logger->warn("UE[%ld] -- UE position is invalid, using trigger defaults", ue->ueId);
    }

    // prioritizeNeighbors returns a sorted vector of (neighborCell, score) pairs
    //   (higher score = higher priority = longer transit time above theta_e)
    auto prioritizedNeighbors = prioritizeNeighbors(neighborSnapshot, ownNci, ueEcefPos, dynTriggerParams.condT1_thresholdSec);

    // Build the candidate list from explicit targetCellIds or from the calculated priority list.
    if (!profile.targetCellCalculated && !profile.targetCellIds.empty())
    {
        // Explicit mode: use the listed NCIs in order, capped to maxTargets.
        prioritizedNeighbors.clear();
        int added = 0;
        for (int64_t nci : profile.targetCellIds)
        {
            if (added >= profile.maxTargets)
                break;
            const auto *nbr = findNeighborByNci(neighborSnapshot, nci);
            if (!nbr)
            {
                m_logger->warn("UE[%ld] CHO profile %d: targetCellId=0x%llx not in neighborList; skipping",
                               ueId, choProfileIdx, nci);
                continue;
            }
            prioritizedNeighbors.emplace_back(nbr, 1);
            ++added;
        }
    }
    else
    {
        // Calculated mode: cap the prioritized list to maxTargets.
        if (profile.maxTargets > 0 && static_cast<int>(prioritizedNeighbors.size()) > profile.maxTargets)
            prioritizedNeighbors.erase(
                prioritizedNeighbors.begin() + profile.maxTargets, prioritizedNeighbors.end());
    }

    if (prioritizedNeighbors.empty())
    {
        m_logger->warn("UE[%ld] CHO profile %d: no valid target neighbors; skipping preparation",
                       ueId, choProfileIdx);
        return;
    }

    // mark this profile's preparation as pending and store candidates
    prepState.pending = true;
    prepState.candidateNcis.clear();
    prepState.candidateScores.clear();

    for (const auto &n : prioritizedNeighbors)
    {
        const int64_t nci = n.neighbor->getNci();
        prepState.candidateNcis.push_back(nci);
        prepState.candidateScores[nci] = n.score;
    }

    if (prepState.candidateNcis.empty())
    {
        prepState.pending = false;
        m_logger->warn("UE[%ld] CHO profile %d: candidate list empty after filtering; skipping", ueId, choProfileIdx);
        return;
    }

    // collect the measIds associated with this profile's CHO conditions
    std::vector<long> usedMeasIds{};
    for (const auto &measIdEntry : ue->measIdentities)
    {
        if (measIdEntry.second.choProfileId == static_cast<long>(choProfileIdx))
            usedMeasIds.push_back(measIdEntry.first);
    }
    prepState.measIds = usedMeasIds;

    // Send handover required to NGAP with CHO preparation flag for each candidate,
    // so NGAP can prepare each target gNB and reply with CHO commands independently.

    for (int64_t targetNci : prepState.candidateNcis)
    {
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
        w->ueId = ue->ueId;
        w->hoTargetNci = targetNci;
        w->hoCause = NgapCause::RadioNetwork_handover_desirable_for_radio_reason;
        w->hoForChoPreparation = true;
        m_base->ngapTask->push(std::move(w));
    }

    std::string measIdsStr;
    for (size_t i = 0; i < prepState.measIds.size(); ++i) {
        if (i > 0) measIdsStr += ", ";
        measIdsStr += std::to_string(prepState.measIds[i]);
    }
    std::string ncisStr;
    for (size_t i = 0; i < prepState.candidateNcis.size(); ++i) {
        if (i > 0) ncisStr += ", ";
        ncisStr += std::to_string(prepState.candidateNcis[i]);
    }
    m_logger->info("UE[%ld] CHO profile %d: prepare sent to NGAP measIds=[%s] total_candidates=%zu targetNCIs=[%s]",
                   ue->ueId,
                   choProfileIdx,
                   measIdsStr.c_str(),
                   prepState.candidateNcis.size(),
                   ncisStr.c_str()
    );
}

/**
 * @brief Handles a handover command from NGAP, including an RRC container
 * that carries the RRCReconfiguration message from target gNB for handover.
 * 
 * If the handover command is for a CHO preparation, this function completes the 
 * CHO process by generating and sending the CHO RRCReconfiguration message to the UE.
 * 
 * @param ueId 
 * @param rrcContainer 
 */
void GnbRrcTask::handleNgapHandoverCommand(int64_t ueId,
                                           const OctetString &rrcContainer,
                                           bool hoForChoPreparation)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->warn("UE[%ld] Cannot find UE for handleNgapHandoverCommand", ueId);
        return;
    }

    m_logger->info("UE[%ld] Received NGAP Handover Command with RRC container of %zu bytes (mode=%s)",
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
        m_logger->err(" UE[%ld] Failed to decode handover RRC container as DL-DCCH message", ueId);
        return;
    }

    bool isReconfig = pdu->message.present == ASN_RRC_DL_DCCH_MessageType_PR_c1 &&
                      pdu->message.choice.c1 != nullptr &&
                      pdu->message.choice.c1->present == ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration &&
                      pdu->message.choice.c1->choice.rrcReconfiguration != nullptr;

    if (!isReconfig)
    {
        m_logger->err("UE[%ld] Decoded handover RRC container is not an RRCReconfiguration", ueId);
        asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
        return;
    }

    sendRrcMessage(ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("UE[%ld] RRCReconfiguration from NGAP forwarded to UE", ueId);

}

void GnbRrcTask::handleNgapHandoverFailure(int64_t ueId, int64_t targetNci, bool fromChoPreparation)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue) {
        m_logger->warn("UE[%ld] Cannot find UE for handleNgapHandoverFailure", ueId);
        return;
    }

    // failure from a CHO preparation request
    if (fromChoPreparation)
    {
        m_logger->info("UE[%ld] Received NGAP Handover Failure for CHO preparation targetNCI=%ld", ueId, targetNci);

        // find which profile owns this targetNci
        int owningProfile = -1;
        for (auto &[profileIdx, prepState] : ue->choPreparations)
        {
            auto it = std::find(prepState.candidateNcis.begin(), prepState.candidateNcis.end(), targetNci);
            if (it != prepState.candidateNcis.end())
            {
                prepState.candidateNcis.erase(it);
                prepState.candidateScores.erase(targetNci);
                owningProfile = profileIdx;
                m_logger->debug("UE[%ld] Removed targetNCI=%ld from CHO profile %d candidates", ueId, targetNci, profileIdx);
                if (prepState.candidateNcis.empty())
                {
                    clearChoPendingState(ue, profileIdx);
                    m_logger->debug("UE[%ld] CHO profile %d: no further candidates; cleared state", ueId, profileIdx);
                }
                break;
            }
        }
        if (owningProfile < 0)
            m_logger->warn("UE[%ld] CHO failure for targetNCI=%ld not found in any pending profile", ueId, targetNci);

        return;
    }

    // failure from normal handover request

    m_logger->info("UE[%ld] Received NGAP Handover Failure for classic handover targetNCI=%ld", ueId, targetNci);

    auto itPending = m_handoversPending.find(ueId);
    if (itPending != m_handoversPending.end() && itPending->second != nullptr)
    {
        if (itPending->second->ctx != nullptr)
        {
            releaseCrnti(itPending->second->ctx->cRnti);
            delete itPending->second->ctx;
        }

        delete itPending->second;
        m_handoversPending.erase(itPending);
    }

        m_logger->warn("UE[%ld] NGAP handover failure received with no RRC pending handover state", ueId);
}

/**
 * @brief Fully clears the CHO pending state in the UE context, including the MeasConfig, candidate lists, and timers.
 * 
 * @param ue The ue Rrc context
 * @param profileIdx The index of the profile to clear
 */
void GnbRrcTask::clearChoPendingState(RrcUeContext *ue, int profileIdx)
{
    auto it = ue->choPreparations.find(profileIdx);
    if (it != ue->choPreparations.end())
        ue->choPreparations.erase(it);
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
    // find the profile whose candidateNcis contains the responding target NCI —
    // we don't yet know candidateNci, so we defer the lookup until after extraction.
    // First confirm at least one profile has pending state.
    if (ue->choPreparations.empty())
    {
        m_logger->warn("UE[%ld] CHO prepare response arrived with no pending CHO state; ignoring", ue->ueId);
        return;
    }

    // extract the nested RRCReconfiguration container from the NGAP message
    
    OctetString nestedRrcReconfig{};
    if (!extractNestedRrcReconfiguration(rrcContainer, nestedRrcReconfig))
    {
        m_logger->err("UE[%ld] Failed to extract nested RRCReconfiguration for CHO candidate", ue->ueId);
        return;
    }

    // determine the target gnb NCI from the nested RRCReconfiguration

    int candidateNci = -1;
    if (!extractTargetNciFromNestedRrcReconfiguration(nestedRrcReconfig, candidateNci))
    {
        m_logger->err("UE[%ld] Failed to extract target NCI from nested RRCReconfiguration", ue->ueId);
        return;
    }

    // locate the candidate NCI across all pending profiles
    //   NOTE: key limitation - if the same candidate NCI is present in multiple profiles, we will match it to the first profile and ignore the others.
    //   So currently we cannot send multiple CHO preparations with the same candidate NCI per UE.  In practice this should not happen,
    //   but relevant for testing.
    int owningProfileIdx = -1;
    ChoPreparationState *prepState = nullptr;
    for (auto &[profileIdx, state] : ue->choPreparations)
    {
        auto it = std::find(state.candidateNcis.begin(), state.candidateNcis.end(), candidateNci);
        if (it != state.candidateNcis.end())
        {
            state.candidateNcis.erase(it);
            owningProfileIdx = profileIdx;
            prepState = &state;
            break;
        }
    }
    if (!prepState)
    {
        m_logger->warn("UE[%ld] CHO prepare response targetNCI=%d not found in any pending profile; ignoring",
                        ue->ueId, candidateNci);
        return;
    }

    
    // Create RRCReconfiguration message with the nested RRCReconfiguration from the target gNB, 
    //  and include the CHO conditional reconfiguration IEs with the candidate NCI and the MeasIds that 
    //  should trigger execution of this candidate's CHO config.
    
    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();
    int txId = ue->getNextTid();
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

    auto *addMod = asn::New<ASN_RRC_CondReconfigToAddMod>();

    // select a unique condReconfigId
    int condReconfigId = -1;
    if (!allocateUniqueCondReconfigId(ue, condReconfigId))
    {
        m_logger->err("UE[%ld] CHO candidate skipped: no free condReconfigId in range %d..%d",
                      ue->ueId,
                      MIN_COND_RECONFIG_ID,
                      MAX_COND_RECONFIG_ID);

        prepState->candidateScores.erase(candidateNci);
        if (prepState->candidateNcis.empty())
            clearChoPendingState(ue, owningProfileIdx);
        return;
    }

    addMod->condReconfigId = condReconfigId;

    // assign MeasId(s) to this condReconfig
    //  It will be the MeasId(s) created as part of the earlier MeasConfig
    addMod->condExecutionCond =
        asn::New<ASN_RRC_CondReconfigToAddMod::ASN_RRC_CondReconfigToAddMod__condExecutionCond>();
    for (long measId : prepState->measIds)
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
    for (size_t i = 0; i < prepState->measIds.size(); ++i) {
        if (i > 0) measIdsStr += ", ";
        measIdsStr += std::to_string(prepState->measIds[i]);
    }

    m_logger->info("UE[%ld] CHO profile %d: RRCReconfiguration sent targetNCI=%ld condReconfigId=%d "
                    "measIds=[%s] pendingCandidates=%zu txId=%d",
                    ue->ueId,
                    owningProfileIdx,
                    candidateNci,
                    condReconfigId,
                    measIdsStr.c_str(),
                    prepState->candidateNcis.size(),
                    txId);

    // if this profile has no more candidates, clear its state
    prepState->candidateScores.erase(candidateNci);
    if (prepState->candidateNcis.empty())
        clearChoPendingState(ue, owningProfileIdx);

    return;

}

std::vector<HandoverMeasurementIdentity> GnbRrcTask::getHandoverMeasurementIdentities(int64_t ueId) const
{
    std::vector<HandoverMeasurementIdentity> identities{};

    if (ueId <= 0)
        return identities;

    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end() || !it->second)
        return identities;

    auto *ctx = it->second;
    identities.reserve(ctx->measIdentities.size());
    for (const auto &item : ctx->measIdentities)
    {
        auto mi = item.second;
        identities.push_back({mi.measId, mi.measObjectId, mi.reportConfigId, mi.eventKind, mi.eventType});
    }
    return identities;
}

/**
 * @brief Used by NGAP to collect MeasConfig Information for a handover Command to AMF
 * 
 * @param ueId 
 * @return OctetString 
 */
OctetString GnbRrcTask::getHandoverMeasConfigRrcReconfiguration(int64_t ueId) const
{
    if (ueId <= 0)
        return OctetString{};

    auto it = m_ueCtx.find(ueId);
    const RrcUeContext *ctx = it != m_ueCtx.end() ? it->second : nullptr;

    if (!ctx || ctx->measIdentities.empty())
        return OctetString{};

    // In a real implementation, here we would insert the actual MeasConfig IEs based on the UE context and measurement identities.
    // Since we don;t use them, we just create a dummy payload of 1024 bytes.

    // create dummy payload
    std::vector<uint8_t> buffer(1024);
    for (int i = 0; i < 1024; i++) {
        buffer[i] = static_cast<uint8_t>(i & 0xFF);
    }

    OctetString encoded = OctetString(std::move(buffer));


    return encoded;
}

/**
 * @brief Used by target gNB to create an RRCReconfiguration message to be sent to the UE as part of
 * handover preparation.
 * Returns the transaction id so it can be inserted into UE's pending handover
 * context at target and matched when the UE completes handover.
 * 
 * @param ueId 
 * @param targetNci 
 * @param newCrnti 
 * @param t304Ms 
 * @param rrcContainer 
 * @return int64_t 
 */
int64_t GnbRrcTask::buildHandoverCommandForTransfer(int64_t ueId, int64_t targetNci, int newCrnti,
                                                    int t304Ms, OctetString &rrcContainer)
{
    int hoCrnti = normalizeCrntiForRrc(newCrnti);
    if (hoCrnti != newCrnti)
    {
            m_logger->warn("UE[%ld] Normalizing target handover C-RNTI %d -> %d", ueId, newCrnti, hoCrnti);
    }

    ASN_RRC_ReconfigurationWithSync rws{};
    rws.newUE_Identity = static_cast<long>(hoCrnti);
    rws.t304 = t304MsToEnum(t304Ms);

    ASN_RRC_ServingCellConfigCommon scc{};
    long nci = static_cast<long>(targetNci);
    scc.physCellId = &nci;
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
        m_logger->err("UE[%ld] buildHandoverCommandForTransfer: failed CellGroupConfig encode", ueId);
        return -1;
    }

    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration>();

    auto *txUe = findCtxByUeId(ueId);
    long txId = txUe ? txUe->getNextTid() : 0;

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
        m_logger->err("UE[%ld] buildHandoverCommandForTransfer: failed RRCReconfiguration encode", ueId);
        return -1;
    }

    m_logger->info("UE[%ld] Target generated handover command with txId=%ld targetNCI=%ld newCRNTI=%d", ueId,
                   txId, targetNci, hoCrnti);
    m_logger->debug("UE[%ld] buildHandoverCommandForTransfer: encoded RRCReconfiguration size=%dB txId=%ld",
                    ueId, encoded.length(), txId);

    rrcContainer = std::move(encoded);

    return txId;
}

/**
 * @brief Adds a pending handover context transfer for the specified UE, to be completed when the UE connects
 * after handover. Handover preparation info includes the measurement
 * identities configured for this UE.
 * Also builds the RRCReconfiguration message to be included in the Target2Source
 * TransparentContainer, which source gNB sends to the UE.
 * 
 * @param ueId 
 * @param handoverPrep
 * @param rrcContainer output parameter for the RRCReconfiguration message to be sent to the UE 
 * @return true - success
 * @return false - failure
 */
bool GnbRrcTask::addPendingHandover(int64_t ueId, const HandoverPreparationInfo &handoverPrep,
                                    OctetString &rrcContainer)
{
    if (ueId <= 0)
        return false;

    // create the new UE RRC context
    auto *ctx = new RrcUeContext(ueId);
    ctx->ueId = ueId;
    ctx->handoverInProgress = true;
    ctx->handoverTargetNci = m_config->nci;
    ;
    ctx->cRnti = allocateCrnti();

    for (const auto &item : handoverPrep.measIdentities)
    {
        ctx->measIdentities[item.measId] = {item.measId, item.measObjectId, item.reportConfigId,
                                            item.eventKind, item.eventType, 0};
    }

    auto it = m_handoversPending.find(ueId);
    if (it != m_handoversPending.end() && it->second)
    {
        if (it->second->ctx)
            releaseCrnti(it->second->ctx->cRnti);
        delete it->second->ctx;
        delete it->second;
    }

    int64_t txId = buildHandoverCommandForTransfer(ueId, ctx->handoverTargetNci, ctx->cRnti, 1000, rrcContainer);
    // the command build fails, no handover context should be added
    if (txId < 0) {
        releaseCrnti(ctx->cRnti);
        delete ctx;
        return false;
    }

    // add pending handover context, to be completed when the UE connects after handover
    auto *pending = new RRCHandoverPending();
    pending->ueId = ueId;
    pending->ctx = ctx;
    pending->txId = txId;
    pending->expireTime = utils::CurrentTimeMillis() + HANDOVER_TIMEOUT_MS;
    m_handoversPending[ueId] = pending;

    m_logger->info("UE[%d] Added pending handover transfer with %zu measurement identities", ueId,
                   handoverPrep.measIdentities.size());
    return true;
}

/**
 * @brief Builds an APER-encoded HandoverPreparationInformation message for the specified UE,
 * per TS 38.331 §11.2.2.  The encoded OctetString is suitable for placement in the XnAP
 * rrc-Context field or the NGAP source-to-target transparent container.
 *
 * Only the mandatory ue_CapabilityRAT_List IE is included (as an empty list, because UE
 * capability containers are not stored in the current RRC context).  The optional sourceConfig,
 * rrm_Config, and as_Context IEs are absent — their ASN.1 types are not generated in this build.
 *
 * @param ueId  The UE identifier
 * @return APER-encoded OctetString, or empty on error
 */
OctetString GnbRrcTask::createHandoverPreparationInformation(int64_t ueId)
{
    auto it = m_ueCtx.find(ueId);
    if (it == m_ueCtx.end() || !it->second)
    {
        m_logger->err("UE[%ld] createHandoverPreparationInformation: UE context not found", ueId);
        return OctetString{};
    }

    auto *pdu = asn::New<ASN_RRC_HandoverPreparationInformation>();
    pdu->criticalExtensions.present =
        ASN_RRC_HandoverPreparationInformation__criticalExtensions_PR_c1;
    pdu->criticalExtensions.choice.c1 = asn::NewFor(pdu->criticalExtensions.choice.c1);
    pdu->criticalExtensions.choice.c1->present =
        ASN_RRC_HandoverPreparationInformation__criticalExtensions__c1_PR_handoverPreparationInformation;

    auto *ies = asn::New<ASN_RRC_HandoverPreparationInformation_IEs>();
    // ue_CapabilityRAT_List is mandatory per spec; left empty because UE capability
    // containers are not stored in the RRC context in this implementation.
    // sourceConfig, rrm_Config, as_Context: optional; left absent (ASN.1 types not generated).
    pdu->criticalExtensions.choice.c1->choice.handoverPreparationInformation = ies;

    m_logger->warn("UE[%ld] createHandoverPreparationInformation: ue_CapabilityRAT_List is empty "
                   "(UE capabilities not stored in RRC context)", ueId);

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_HandoverPreparationInformation, pdu);
    asn::Free(asn_DEF_ASN_RRC_HandoverPreparationInformation, pdu);

    if (encoded.length() == 0)
        m_logger->err("UE[%ld] createHandoverPreparationInformation: APER encoding failed", ueId);
    else
        m_logger->debug("UE[%ld] createHandoverPreparationInformation: encoded %zu bytes", ueId, encoded.length());

    return encoded;
}

/**
 * @brief Deletes the UE's context due to a successful handover to the target gNB.
 *
 * @param ueId
 */
void GnbRrcTask::handoverContextRelease(int64_t ueId)
{
    auto *ctx = findCtxByUeId(ueId);
    if (ctx)
    {
        releaseCrnti(ctx->cRnti);
        delete ctx;
        m_ueCtx.erase(ueId);
        m_logger->info("UE[%ld] RRC context released", ueId);
        return;
    }

    m_logger->warn("UE[%ld] handoverContextRelease: context not found", ueId);
}

} // namespace gnb