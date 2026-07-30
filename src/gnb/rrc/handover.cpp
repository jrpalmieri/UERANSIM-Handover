//
// gNB-side handover procedures (source and target roles), including
// conditional handover (CHO) and the custom transparent containers.
//
// Implements:
//   evaluateHandoverDecision()        – re-check a reported event and decide whether to hand over
//   executeBasicHandover()            – build the source-to-target container, start Xn/N2 preparation
//   handleHandoverRequest()           – target side: admit the UE, build pending context + command
//   rejectHandoverRequest() / discardHandoverUeContext() – failure-path responses and cleanup
//   handleHandoverAckOrCommand()      – source side: forward the target's RRCReconfiguration to the UE
//   handleHandoverPreparationFailure() – clear source-side preparation state
//   sendUeHandoverMessage()           – build a ReconfigurationWithSync RRCReconfiguration locally
//   processConditionalHandover() / completeConditionalHandover() / clearChoPendingState() – CHO preparation
//   prioritizeNeighbors()             – rank CHO target candidates (satellite-aware)
//   makeTargetToSourceTransparentContainer() / makeSourceToTargetTransparentContainerSimulated()
//   EncodeCustomRrcContext() / DecodeCustomRrcContext() – custom transparent-container payload
//   createHandoverPreparationInformation() – standards-based HandoverPreparationInformation encode
//
// (MeasConfig construction and MeasurementReport reception live in measurement.cpp;
//  RRCReconfigurationComplete handling lives in reconfiguration.cpp.)
//

#include "task.hpp"

#include <gnb/neighbors.hpp>
#include <gnb/ngap/task.hpp>
#include <gnb/xn/task.hpp>
#include <gnb/rls/task.hpp>
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
#include <asn/rrc/ASN_RRC_ConditionalReconfiguration-r16.h>
#include <asn/rrc/ASN_RRC_CondReconfigToAddMod-r16.h>
#include <asn/rrc/ASN_RRC_CondReconfigToAddModList-r16.h>
#include <asn/rrc/ASN_RRC_CondReconfigToRemoveList-r16.h>
#include <asn/rrc/ASN_RRC_CondTriggerConfig-r16.h>
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



#include <asn/xnap/ASN_XNAP_Cause.h>

#include <cmath>
#include <cstring>
#include <utility>

#include <libsgp4/DateTime.h>

const int HANDOVER_TIMEOUT_MS = 5000; // time to wait for handover completion before considering it failed
const int COND_HANDOVER_TIMEOUT_MS = 100000; // time to wait for conditional handover completion before considering it failed

static constexpr int MIN_COND_RECONFIG_ID = 1;
static constexpr int MAX_COND_RECONFIG_ID = 8;


namespace nr::gnb
{

static RrcUeContext *DecodeCustomRrcContext(const OctetString &data);
static bool UnwrapSourceToTargetContainer(const OctetString &container, OctetString &rrcContextOut,
                                          bool *choIndicationOut = nullptr);

using HandoverEventType = nr::rrc::common::HandoverEventType;
using nr::sat::EcefPosition;
using nr::rrc::common::t304MsToEnum;
using nr::rrc::common::IsMeasurementEvent;



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
 * @brief Evaluates whether to trigger a handover based on the latest measurement report from the UE.
 * 
 * @param ueId UE ID of UE providing measurement report 
 * @param measId Measurement ID of the measurement report
 */
void GnbRrcTask::evaluateHandoverDecision(int64_t ueId, int measId)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
        return;

    // Don't trigger if handover is already in progress
    if (ue->handoverInProgress || ue->handoverDecisionPending)
        return;

    int64_t bestNeighNci = ue->lastMeasReportNci;
    int bestNeighRsrp = ue->lastMeasReportRsrp;
    int servingRsrp = ue->lastServingRsrp;

    auto miIt = ue->measIdentities.find(measId);
    if (miIt == ue->measIdentities.end())
    {
        m_logger->warn("UE[%ld] HandoverEval: unknown measId=%d, skipping", ue->ueId, measId);
        return;
    }
    const auto &mi = miIt->second;

    auto rcIt = ue->reportConfigEvents.find(mi.reportConfigId);
    if (rcIt == ue->reportConfigEvents.end())
    {
        m_logger->warn("UE[%ld] HandoverEval: measId=%d references unknown reportConfigId=%ld, skipping",
                       ue->ueId, measId, mi.reportConfigId);
        return;
    }
    auto rc = rcIt->second;

    // only evaluate events that use measurement reports
    if (!IsMeasurementEvent(rc.eventKind))
        return;

    bool shouldHandover = false;

    if (rc.eventKind == HandoverEventType::A2)
    {
        // A2: serving RSRP < threshold - hysteresis
        shouldHandover = rc.evaluateA2(servingRsrp);
        m_logger->debug("UE[%ld] HandoverEval: event=A2 measId serving=%ddBm threshold=%ddBm hysteresis=%ddB "
                        "condition=(%d < %d) result=%s",
                        ue->ueId, servingRsrp, rc.a2_thresholdDbm, rc.a2_hysteresisDb,
                        servingRsrp, rc.a2_thresholdDbm - rc.a2_hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (rc.eventKind == HandoverEventType::A3)
    {
        // A3: neighbor RSRP > serving RSRP + offset + hysteresis
        //   we just check against the best neighbor
        shouldHandover = rc.evaluateA3Cell(servingRsrp, bestNeighRsrp);
        m_logger->debug("UE[%ld] HandoverEval: event=A3 serving=%ddBm bestNeighNci=%ld bestNeigh=%ddBm "
                        "offset=%ddB hysteresis=%ddB condition=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighNci, bestNeighRsrp,
                        rc.a3_offsetDb, rc.a3_hysteresisDb,
                        bestNeighRsrp, servingRsrp + rc.a3_offsetDb + rc.a3_hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (rc.eventKind == HandoverEventType::A5)
    {
        // A5: serving RSRP < threshold1 - hysteresis AND neighbor RSRP > threshold2 + hysteresis
        //   we just check against the best neighbor
        shouldHandover = rc.evaluateA5Serving(servingRsrp) && rc.evaluateA5Neighbor(bestNeighRsrp);
        m_logger->debug("UE[%ld] HandoverEval: event=A5 serving=%ddBm bestNeighNci=%ld bestNeigh=%ddBm "
                        "thr1=%ddBm thr2=%ddBm hysteresis=%ddB cond1=(%d < %d) cond2=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighNci, bestNeighRsrp,
                        rc.a5_threshold1Dbm, rc.a5_threshold2Dbm,
                        rc.a5_hysteresisDb,
                        servingRsrp, rc.a5_threshold1Dbm - rc.a5_hysteresisDb,
                        bestNeighRsrp, rc.a5_threshold2Dbm + rc.a5_hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (rc.eventKind == HandoverEventType::D1)
    {

        // D1: distance to serving cell (d1) > threshold1 - hysteresis AND distance to neighbor cell (d2) < threshold2 + hysteresis
        auto uePos = m_base->getUePosition(ue->ueId);
        if (!uePos.has_value() || !uePos->isValid)
        {
            m_logger->warn("UE[%ld] HandoverEval: event=D1 but UE position is invalid, skipping evaluation",
                           ue->ueId);
            return;
        }
        
        shouldHandover = rc.evaluateD1(uePos.value());
        
        m_logger->debug("UE[%ld] HandoverEval: event=D1 "
                        "distThresh1=%dm distThresh2=%dm hysteresis=%dm "
                        "result=%s",
                        ue->ueId, rc.d1_distanceThreshFromReference1, rc.d1_distanceThreshFromReference2,
                        rc.d1_hysteresisLocation,
                        shouldHandover ? "true" : "false");
    }
    else
    {
        m_logger->warn("UE[%ld] HandoverEval: unsupported event type %s", ue->ueId, rc.eventStr());
        return;
    }

    if (!shouldHandover)
        return;

    // For A2, a target may not be present in this report; use last known neighbor if available.
    if (bestNeighNci < 0)
    {
        m_logger->warn("UE[%ld] Handover decision met event=%s but no neighbor NCI available",
                       ue->ueId, rc.eventStr());
        return;
    }

    m_logger->info("UE[%ld] Handover decision (%s): targetNCI=%ld (serving=%ddBm, target=%ddBm)",
                   ue->ueId, rc.eventStr(), bestNeighNci, servingRsrp, bestNeighRsrp);

    // initiate handover procedure
    executeBasicHandover(ue, bestNeighNci, servingRsrp, bestNeighRsrp);

}


void GnbRrcTask::executeBasicHandover(RrcUeContext *ue, long targetNci, int servingRsrp, int bestNeighRsrp)
{

    ue->handoverDecisionPending = true;

    // create handover preparation information (RRC Container) to pass to NGAP/Xn

    //std::unique_ptr<OctetString> rrcContainer = createHandoverPreparationInfo(ue, targetNci, servingRsrp, bestNeighRsrp);
    std::unique_ptr<OctetString> rrcContainer = makeSourceToTargetTransparentContainerSimulated(*ue, 0, false);
    
    // Determine whether to use Xn or N2 for handover

    auto neighborOpt = m_base->neighbors->findByNci(targetNci);
    if (!neighborOpt)
    {
        m_logger->err("sendHandoverRequired: target NCI=%ld not found in neighborList", targetNci);
        return;
    }
    const auto &neighbor = *neighborOpt;

    m_logger->info("Resolved target neighbor NCI=%ld -> NCGI(plmn=%03d-%02d nci=0x%09llx gnbId=%u cellId=%d) "
                   "tac=%d interface=%s",
                   targetNci,
                   m_base->config->plmn.mcc,
                   m_base->config->plmn.mnc,
                   static_cast<unsigned long long>(neighbor.getNrCellIdentity()),
                   neighbor.getGnbId(),
                   neighbor.getCellId(),
                   neighbor.tac,
                   neighbor.handoverInterface == EHandoverInterface::N2 ? "N2" : "Xn");


    if (neighbor.handoverInterface == EHandoverInterface::Xn)
    {

        // send Xn handover request

        auto w = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::HANDOVER_REQUEST_SEND);
        w->ueId = ue->ueId;
        w->targetNci = targetNci;
        w->rrcContainer = std::move(rrcContainer);
        w->reason = ASN_XNAP_Cause_PR::ASN_XNAP_Cause_PR_radioNetwork;
        w->choParams = nullptr; 
        m_base->xnTask->push(std::move(w));

        m_logger->info("UE[%ld] Xn handover started - handoverRequired sent to NCI. targetNci=%ld", ue->ueId, targetNci);
    }
    else
    {
        // Initiate N2 handover via NGAP (no Xn interface available)
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
        w->ueId = ue->ueId;
        w->hoTargetNci = targetNci;
        w->hoCause = NgapCause::RadioNetwork_handover_desirable_for_radio_reason;
        w->rrcContainer = std::move(rrcContainer);
        w->choParams = nullptr;
        m_base->ngapTask->push(std::move(w));

        m_logger->info("UE[%ld] N2 Handover started - HandoverRequired sent to NGAP. targetNCI=%ld", ue->ueId, targetNci);
    }

}


/**
 * @brief Frees a provisional (pending-handover) RRC UE context: returns its
 * C-RNTI (if one was allocated) to the pool and deletes the context object.
 * Used by the handleHandoverRequest failure paths and the duplicate-request
 * replacement; intended to be reused by the future pending-handover expiry sweep.
 */
void GnbRrcTask::discardHandoverUeContext(RrcUeContext *ue)
{
    if (ue == nullptr)
        return;

    if (ue->cRnti > 0)
        releaseCrnti(ue->cRnti);

    delete ue;
}

/**
 * @brief Periodic garbage collection of m_handoversPending (driven by the
 * TIMER_ID_HO_PENDING_SWEEP timer in the RRC task, every 1 s).
 *
 * A pending target-side preparation whose UE never sent
 * RRCReconfigurationComplete is discarded once its expireTime passes
 * (5 s basic / 100 s CHO, stamped in handleHandoverRequest).  Cleanup mirrors
 * the Xn HANDOVER_CANCEL_RECEIVED rollback — every receiver is idempotent:
 *  - the provisional RRC context and its C-RNTI are freed,
 *  - Xn only: the provisional NGAP/GTP state made via XN_HANDOVER_PREPARE is
 *    cancelled (N2's equivalent lives in NGAP's own pending map, keyed by the
 *    NGAP transaction id which RRC does not hold — NGAP owns that expiry),
 *  - the pre-programmed RLS bearer context is removed, unless this ueId also
 *    has an *active* RRC context here (never destroy a live UE's radio state).
 *
 * No failure message is sent to anyone: the preparation was already ACKed
 * toward the source, so expiry is purely local cleanup.  A completion that
 * arrives after expiry is treated as coming from an unknown UE.  Note the
 * expiry clock is CurrentTimeMillis(), matching the stamp in
 * handleHandoverRequest (TODO there: switch both to SatTime in NTN mode).
 */
void GnbRrcTask::sweepPendingHandovers()
{
    if (m_handoversPending.empty())
        return;

    const uint64_t now = utils::CurrentTimeMillis();

    for (auto it = m_handoversPending.begin(); it != m_handoversPending.end();)
    {
        auto &pending = it->second;
        if (pending.expireTime > now)
        {
            ++it;
            continue;
        }

        const int64_t ueId = it->first;
        m_logger->warn("UE[%ld] pending %s handover expired (txId=%ld); discarding provisional context",
                       ueId, pending.isXn ? "Xn" : "N2", pending.rrcReconfigurationTxId);

        if (pending.isXn)
        {
            auto ngapCancel = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::XN_TARGET_PREPARATION_CANCEL);
            ngapCancel->ueId = ueId;
            m_base->ngapTask->push(std::move(ngapCancel));
        }

        if (findCtxByUeId(ueId) == nullptr)
        {
            auto rlsCancel = std::make_unique<NmGnbRrcToRls>(NmGnbRrcToRls::REMOVE_UE_CONTEXT);
            rlsCancel->ueId = ueId;
            m_base->rlsTask->push(std::move(rlsCancel));
        }

        discardHandoverUeContext(pending.ctx);
        it = m_handoversPending.erase(it);
    }
}

/**
 * @brief Rejects an incoming Handover Request: frees any partially-built
 * target-side state (via discardHandoverUeContext) and sends a failure
 * response back to the requesting interface so the source side does not
 * wait for an ACK that will never come.
 *   - Xn:   HANDOVER_PREPARATION_FAILURE_SEND → XnAP HandoverPreparationFailure
 *           to the source gNB (existing plumbing in the Xn task).
 *   - NGAP: HANDOVER_FAILURE_SEND → NGAP HandoverFailure to the AMF, which
 *           converts it to HandoverPreparationFailure toward the source.
 *
 * @param requestingTask which interface delivered the Handover Request
 * @param transactionId  the requester's transaction id (xnTxId / ngapTxId)
 * @param ngapCause      cause to report on the NGAP path
 * @param xnCauseGroup   cause group (Cause CHOICE discriminant) for the Xn path
 * @param ue             decoded provisional context to discard (may be nullptr)
 */
void GnbRrcTask::rejectHandoverRequest(ERequestingTask requestingTask, uint32_t transactionId,
                                       NgapCause ngapCause, ASN_XNAP_Cause_PR xnCauseGroup,
                                       RrcUeContext *ue)
{
    discardHandoverUeContext(ue);

    if (requestingTask == ERequestingTask::XN)
    {
        auto w = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::HANDOVER_PREPARATION_FAILURE_SEND);
        w->xnTxId = static_cast<int>(transactionId);
        w->reason = xnCauseGroup;
        m_base->xnTask->push(std::move(w));
    }
    else // ERequestingTask::NGAP
    {
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_FAILURE_SEND);
        w->ngapTxId = transactionId;
        w->hoCause = ngapCause;
        m_base->ngapTask->push(std::move(w));
    }
}

/**
 * @brief Handles a Handover Request msg from XN or NGAP.  Triggered by receiving a HANDOVER_REQUEST_RECEIVED message
 *  from the Xn or NGAP task.
 * 
 *  The rrcContainer is provided by the source gNB (as a Source-to-Target Transparent Container (NGAP) or rrc-Context (Xn) ).
 *  This function decodes the container, creates a provisional RRC UE context, stores it in the pending handover map,
 *  and creates a targetToSourceTransparentContainer containing the RRCReconfiguration message to send to the UE.
 * 
 * @param sourceGnbId    ID of the source gNB
 * @param transactionId  the requester's transaction id (xnTxId / ngapTxId)
 * @param rrcContainer   the source-to-target transparent container
 * @param sessionList    list of PDU session resources
 * @param isCho          flag indicating if this is a Conditional Handover (CHO) request
 * @param requestingTask which interface delivered the Handover Request
 * @param xnCoreContext  core context for Xn
 * @param xnChoRequest   CHO request for Xn
 */
void GnbRrcTask::handleHandoverRequest(int sourceGnbId, uint32_t transactionId,
        std::unique_ptr<OctetString> rrcContainer,
        std::unique_ptr<std::vector<PduSessionResource>> sessionList,
        bool isCho, ERequestingTask requestingTask,
        std::unique_ptr<XnHandoverCoreContext> xnCoreContext,
        std::unique_ptr<XnChoRequest> xnChoRequest)
{

    // The new (provisional) RRC context for this UE
    RrcUeContext *ue = nullptr;

    (void)xnChoRequest;

    // Decode the source-to-target transparent container
    //   Note: need to strip the transparent-container wrapper before decoding: the payload
    //      expected by DecodeCustomRrcContext starts 16 bytes in (see UnwrapSourceToTargetContainer).
    OctetString innerContext{};
    if (UnwrapSourceToTargetContainer(*rrcContainer, innerContext))
        ue = DecodeCustomRrcContext(innerContext);
    else
        m_logger->err("handleHandoverRequest: malformed source-to-target container (transactionId=%u)",
                      transactionId);

    // Check for decode failure
    if (!ue)
    {
        m_logger->err("handleHandoverRequest: Failed to decode RRC Container for transactionId=%u", transactionId);
        // TODO: correct cause for decode failure? Universal for NGAP and XNAP?
        rejectHandoverRequest(requestingTask, transactionId,
                              NgapCause::Protocol_abstract_syntax_error_falsely_constructed_message,
                              ASN_XNAP_Cause_PR_protocol, nullptr);
        return;
    }

    // Check for invalid ueId
    if (ue->ueId <= 0)
    {
        m_logger->err("handleHandoverRequest: decoded RRC context has invalid ueId=%ld (transactionId=%u)",
                      ue->ueId, transactionId);
        // TODO: correct cause for decode failure? Universal for NGAP and XNAP?
        rejectHandoverRequest(requestingTask, transactionId,
                              NgapCause::Protocol_semantic_error,
                              ASN_XNAP_Cause_PR_protocol, ue);
        return;
    }

    // Check for an existing handover pending in progress for this UE.
    //  Could happen from a source retry after timeout, or a CHO re-preparation.
    //  Policy is latest wins:
    //    free the old entry's context and C-RNTI, then proceed with the new
    //    request.
    auto itDup = m_handoversPending.find(ue->ueId);
    if (itDup != m_handoversPending.end())
    {
        m_logger->warn("UE[%ld] handleHandoverRequest: replacing stale pending handover (oldTxId=%ld)",
                       ue->ueId, itDup->second.rrcReconfigurationTxId);
        discardHandoverUeContext(itDup->second.ctx);
        m_handoversPending.erase(itDup);
    }

    // generate new cRNTI for the UE in the target cell
    int newCrnti = m_crntiMgr.allocate();

    // Check for C-RNTI pool exhausted (allocate() returns 0, which is an invalid C-RNTI).
    //   Reject handover request, cause is no radio resources.
    if (newCrnti == 0)
    {
        m_logger->err("UE[%ld] handleHandoverRequest: C-RNTI pool exhausted, rejecting handover", ue->ueId);
        // TODO: Universal cause for NGAP and XN?
        rejectHandoverRequest(requestingTask, transactionId,
                              NgapCause::RadioNetwork_no_radio_resources_available_in_target_cell,
                              ASN_XNAP_Cause_PR_radioNetwork, ue);
        return;
    }

    // assign new CRNTI to UE context
    ue->cRnti = newCrnti;

    if (requestingTask == ERequestingTask::XN)
    {
        // NGAP/GTP preparation is asynchronous, so reject structurally invalid
        // transferred core state here, before the Xn ACK can be queued.  These
        // are the same invariants setupPduSessionResource() enforces.
        bool invalidCore = !xnCoreContext;
        if (sessionList)
        {
            invalidCore = invalidCore || std::any_of(sessionList->begin(), sessionList->end(), [](const auto &session) {
                return session.sessionType != PduSessionType::IPv4 || session.upTunnel.address.length() == 0 ||
                       session.qosFlows.empty();
            });
        }
        if (invalidCore)
        {
            m_logger->err("UE[%ld] invalid Xn core/session context; rejecting handover before ACK", ue->ueId);
            rejectHandoverRequest(requestingTask, transactionId, NgapCause::Protocol_semantic_error,
                                  ASN_XNAP_Cause_PR_protocol, ue);
            return;
        }
    }

    // calculate expiration time for handover completion
    // depends on whether this is a CHO or not
    int timeoutMs = isCho ? COND_HANDOVER_TIMEOUT_MS : HANDOVER_TIMEOUT_MS;

    // TODO: if NTN enabled, use SatTime instead
    uint64_t expireTime = utils::CurrentTimeMillis() + timeoutMs;

    // Here we would do some checking for admission of the PDU sessions.
    // For now, we just admit them all.
    //  (When admission control is added: rejecting *all* sessions becomes a
    //   rejectHandoverRequest() with an unable-to-admit cause; partial admission
    //   populates the rejectedSessions field of the ACK messages below.)

    auto admittedSessions = std::make_unique<std::vector<PduSessionResource>>();
    if (sessionList)
    {
        for (const auto &session : *sessionList)
        {
            admittedSessions->push_back(session);
        }
    }

    // Pre-program the target's RLS with the same one-DRB-per-session bearer/SDAP
    // layout used by normal RRC bearer setup (createRadioBearerConfig).  This is
    // interface-independent: user-plane traffic needs the DRB/SDAP mappings in
    // place when the UE arrives regardless of whether Xn or N2 prepared the
    // handover (previously Xn-only, leaving the N2 target with just the SRB0-only
    // default context).  On Xn it additionally guarantees the transferred SN
    // status has bearers to land on.
    if (!admittedSessions->empty())
    {
        // Reuse the shared allocator so DRB ids stay unique across the UE's active
        // bearers and RRC's bearerMap is populated on the handover path too (needed
        // for any later session release at this gNB).
        auto rb = std::make_unique<RadioBearerUpdate>();
        auto sdap = std::make_unique<SdapUpdate>();
        assignSessionBearers(ue, *admittedSessions, *rb, *sdap);

        auto bearerSetup = std::make_unique<NmGnbRrcToRls>(NmGnbRrcToRls::RADIO_BEARER_UPDATE);
        bearerSetup->ueId = ue->ueId;
        bearerSetup->rbUpdate = std::move(rb);
        bearerSetup->sdapUpdate = std::move(sdap);
        m_base->rlsTask->push(std::move(bearerSetup));

        m_logger->debug("UE[%ld] target RLS pre-programmed with DRB(s) for %zu admitted session(s)",
                        ue->ueId, admittedSessions->size());
    }

    // create the RRCReconfiguration message to send to the UE

    long rrcTxId = ue->getNextTid();
    int t304Ms = 1000; // default T304 value to include in the RRCReconfiguration
    auto targetContainer = makeTargetToSourceTransparentContainer(ue->ueId, ue->cRnti, t304Ms, rrcTxId);

    // check for container build failure
    if (!targetContainer)
    {
        m_logger->err("handleHandoverRequest: Failed to create target-to-source RRC Container for UE[%ld]", ue->ueId);
        // TODO: correct cause for container build failure? Universal for NGAP and XNAP?
        rejectHandoverRequest(requestingTask, transactionId,
                              NgapCause::RadioNetwork_unspecified,
                              ASN_XNAP_Cause_PR_radioNetwork, ue);
        return;
    }

    // store in pending handover map keyed by ueId

    m_handoversPending[ue->ueId] = RRCHandoverPending{
        ue->ueId,
        ue,
        expireTime,
        rrcTxId,
        requestingTask == ERequestingTask::XN
    };


    if (requestingTask == ERequestingTask::XN)
    {

        // For XN handovers, we need to create an NGAP provisional context for the UE, as well as
        //  create user plane context.  The XN_HANDOVER_PREPARE message to NGAP will cause NGAP task to handle both.
        //  Note that we need to copy the admitted list because Xn still
        //    owns the original list for construction of HandoverRequestAck.

        // Note that this assumes that NGAP and GTP will be successful in creating the context (there is no error checking).
        //  TODO: modify this logic so that we can handle NGAP/GTP failures and send a HandoverPreparationFailure back to the source gNB.
        //     which may require a change from using NTP message to a direct function call to the NGAP/GTP tasks.
        auto core = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::XN_HANDOVER_PREPARE);
        core->ueId = ue->ueId;
        core->xnCoreContext = std::move(xnCoreContext);
        core->admittedSessions = std::make_unique<std::vector<PduSessionResource>>(*admittedSessions);
        m_base->ngapTask->push(std::move(core));


        // Msg to Xn to send Handover Request Ack to source gNB

        auto w = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::HANDOVER_REQUEST_ACK_SEND);
        w->ueId = ue->ueId;
        w->rrcContainer = std::move(targetContainer);
        w->xnTxId = transactionId;
        w->admittedSessions = std::move(admittedSessions);
        w->rejectedSessions = nullptr;
        m_base->xnTask->push(std::move(w));
    }
    else if (requestingTask == ERequestingTask::NGAP)
    {
        // Msg to NGAP to send Handover Request Ack to AMF
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUEST_ACK_SEND);
        w->ueId = ue->ueId;
        w->ngapTxId = transactionId;
        w->rrcContainer = std::move(targetContainer);
        w->admittedSessions = std::move(admittedSessions);
        w->rejectedSessions = nullptr;
        m_base->ngapTask->push(std::move(w));
    }   
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

    // Build RRCReconfiguration message
    auto *pdu = makeRrcReconfiguration(ueId);

    if (!pdu)
    {
        m_logger->err("UE[%ld] Failed to create RRCReconfiguration for handover", ueId);
        return;
    }

    long txId = pdu->message.choice.c1->choice.rrcReconfiguration->rrc_TransactionIdentifier;

    // Set nonCriticalExtension (v1530-IEs) with masterCellGroup
    auto ies = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();

    ies->masterCellGroup = asn::New<OCTET_STRING_t>();
    asn::SetOctetString(*ies->masterCellGroup, masterCellGroupOctet);
    pdu->message.choice.c1->choice.rrcReconfiguration->criticalExtensions.choice.rrcReconfiguration->nonCriticalExtension = ies;


    // Record handover state in UE context
    ue->handoverInProgress = true;
    ue->handoverTargetNci = targetNci;
    ue->handoverNewCrnti = hoCrnti;
    ue->handoverTxId = txId;

    sendRrcMessage(ueId, pdu);

    m_logger->info("UE[%ld] RRCReconfiguration (basic handover) sent to UE, txId=%ld", ueId, txId);
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
    auto *ue = findCtxByUeId(ueId);
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

    // Send handover required to NGAP with CHO preparation for each candidate,
    // so NGAP can prepare each target gNB and reply with CHO commands independently.

    for (int64_t targetNci : prepState.candidateNcis)
    {
        auto choReq = std::make_unique<GnbCondHandoverRequest>();
        // TODO: setup the CHO request parameters based on score and T1 conditions
        choReq->choTrigger = 0;
        choReq->choArrivalProbabilityPercent = static_cast<int>(prepState.candidateScores[targetNci]);
        choReq->targetNGRANnodeUeXnapId = 0;
        choReq->maxNumCondReconfigsToPrepare = 0;

        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
        w->ueId = ue->ueId;
        w->hoTargetNci = targetNci;
        w->hoCause = NgapCause::RadioNetwork_handover_desirable_for_radio_reason;
        w->choParams = std::move(choReq);
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
 * @brief Handles a handover command from NGAP or Handover Request Ack from Xn.
 * rrCContainer must carry the RRCReconfiguration message from target gNB for handover.
 * 
 * If isCho is false, basic handover is performed.  The RRCReconfiguration is sent to the UE as-is, 
 * and User Plane downlink forwarding is set up.
 * 
 * If isCho is true, this is the response to a CHO preparation. Instead of basic handover,
 * we call completeConditionlaHandover() to complete the CHO process.
 * 
 * @param ueId 
 * @param rrcContainer 
 */
void GnbRrcTask::handleHandoverAckOrCommand(int64_t ueId,
                                           std::unique_ptr<OctetString> rrcContainer,
                                           bool isCho,
                                           ERequestingTask requestingTask,
                                           int64_t targetNci)
{
    auto cancelInvalidXnAck = [&]() {
        if (requestingTask != ERequestingTask::XN)
            return;
        auto cancel = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::HANDOVER_CANCEL_SEND);
        cancel->ueId = ueId;
        cancel->targetNci = targetNci;
        m_base->xnTask->push(std::move(cancel));
    };
    auto *ue = findCtxByUeId(ueId);
    if (!ue)
    {
        m_logger->warn("UE[%ld]: handleHandoverAckOrCommand - cannot find UE ctx. Aborting.", ueId);
        cancelInvalidXnAck();
        return;
    }

    m_logger->info("UE[%ld]: handleHandoverAckOrCommand - Received Handover Approval from %s (mode=%s)",
                   ueId,
                   requestingTask == ERequestingTask::NGAP ? "NGAP" : "XN",
                   isCho ? "cho-prepare" : "classic");

    // If this handover command is from a CHO preparation request, complete and send
    // a CHO RRCReconfiguration for one candidate as soon as its response arrives.
    if (isCho)
    {
        completeConditionalHandover(ue, std::move(rrcContainer));
        return;
    }

    auto *pdu = rrc::encode::Decode<ASN_RRC_DL_DCCH_Message>(asn_DEF_ASN_RRC_DL_DCCH_Message, *rrcContainer);
    if (!pdu)
    {
        m_logger->err(" UE[%ld] Failed to decode handover RRC container as DL-DCCH message", ueId);
        cancelInvalidXnAck();
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
        cancelInvalidXnAck();
        return;
    }

    // Send Target's RRCReconfiguration message to UE

    sendRrcMessage(ueId, pdu);

    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("UE[%ld]: Target RRCReconfiguration sent to UE", ueId);

    if (requestingTask == ERequestingTask::XN)
    {
        // Status transfer is an execution-phase Xn procedure.  Queue it only
        // after the handover command has been validated and delivered, never
        // for a malformed ACK or for N2 handover.
        auto status = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::SN_STATUS_TRANSFER_SEND);
        status->ueId = ueId;
        status->targetNci = targetNci;
        m_base->xnTask->push(std::move(status));
    }

}



void GnbRrcTask::handleHandoverPreparationFailure(int64_t ueId, int64_t targetNci, bool fromChoPreparation, ERequestingTask requestingTask)
{
    auto *ue = findCtxByUeId(ueId);
    if (!ue) {
        m_logger->warn("UE[%ld]: handleHandoverPreparationFailure - cannot find UE ctx. Aborting.", ueId);
        return;
    }

    // failure from a CHO preparation request
    if (fromChoPreparation)
    {
        m_logger->info("UE[%ld]: handleHandoverPreparationFailure - failure for CHO preparation targetNCI=%ld", ueId, targetNci);

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

    m_logger->info("UE[%ld]: handleHandoverPreparationFailure - basic handover targetNCI=%ld", ueId, targetNci);

    // Clear handover state in UE ctx
    ue->handoverInProgress = false;
    ue->handoverDecisionPending = false;
    ue->handoverTargetNci = -1;
    ue->handoverNewCrnti = -1;
    ue->handoverTxId = -1;

    m_logger->debug("UE[%ld]: handleHandoverPreparationFailure - Cleared UE handover state due to preparation failure", ueId);
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
void GnbRrcTask::completeConditionalHandover(RrcUeContext *ue, std::unique_ptr<OctetString> rrcContainer)
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
    if (!extractNestedRrcReconfiguration(*rrcContainer, nestedRrcReconfig))
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

    v1610->conditionalReconfiguration_r16 = asn::New<ASN_RRC_ConditionalReconfiguration_r16>();
    v1610->conditionalReconfiguration_r16->condReconfigToAddModList_r16 =
        asn::New<ASN_RRC_CondReconfigToAddModList_r16>();

    auto *addMod = asn::New<ASN_RRC_CondReconfigToAddMod_r16>();

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

    addMod->condReconfigId_r16 = condReconfigId;

    // assign MeasId(s) to this condReconfig
    //  It will be the MeasId(s) created as part of the earlier MeasConfig
    addMod->condExecutionCond_r16 =
        asn::New<ASN_RRC_CondReconfigToAddMod_r16::ASN_RRC_CondReconfigToAddMod_r16__condExecutionCond_r16>();
    for (long measId : prepState->measIds)
    {
        auto *entry = asn::New<ASN_RRC_MeasId_t>();
        *entry = measId;
        asn::SequenceAdd(*addMod->condExecutionCond_r16, entry);
    }

    // add the transparent container with the nested RRCReconfiguration from the target gNB

    addMod->condRRCReconfig_r16 = asn::New<OCTET_STRING_t>();
    asn::SetOctetString(*addMod->condRRCReconfig_r16, nestedRrcReconfig);

    // add this candidate to the message's list of conditional reconfigurations
    asn::SequenceAdd(*v1610->conditionalReconfiguration_r16->condReconfigToAddModList_r16, addMod);

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
 * @param rrcTxId output parameter for the transaction id used in the RRCReconfiguration
 * @return true for success, false for failure
 */
std::unique_ptr<OctetString> GnbRrcTask::makeTargetToSourceTransparentContainer(int64_t ueId, int newCrnti,
                                                    int t304Ms, long rrcTxId)
{
    int hoCrnti = normalizeCrntiForRrc(newCrnti);
    if (hoCrnti != newCrnti)
    {
            m_logger->warn("UE[%ld] Normalizing target handover C-RNTI %d -> %d", ueId, newCrnti, hoCrnti);
    }

    // Reconfiguration With Sync in the Master Cell Group will trigger handover to the target cell with the specified T304 and new C-RNTI.
    ASN_RRC_ReconfigurationWithSync rws{};
    rws.newUE_Identity = static_cast<long>(hoCrnti);
    rws.t304 = t304MsToEnum(t304Ms);

    ASN_RRC_ServingCellConfigCommon scc{};
    long nci = static_cast<long>(m_base->config->nci);
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
        return nullptr;
    }

    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration = asn::New<ASN_RRC_RRCReconfiguration>();

    reconfig->rrc_TransactionIdentifier = rrcTxId;
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
        return nullptr;
    }

    m_logger->info("UE[%ld] Target generated handover command with txId=%ld targetNCI=%ld newCRNTI=%d", ueId,
                   rrcTxId, nci, hoCrnti);

                   m_logger->debug("UE[%ld] buildHandoverCommandForTransfer: encoded RRCReconfiguration size=%dB txId=%ld",
                    ueId, encoded.length(), rrcTxId);

    return std::make_unique<OctetString>(std::move(encoded));
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

static constexpr uint32_t CUSTOM_S2T_MAGIC = 0x53325443; // "S2TC"
static constexpr uint8_t CUSTOM_S2T_VERSION = 1;
static constexpr uint32_t CUSTOM_S2T_DEFAULT_BLOB_SIZE = 0;
static constexpr uint32_t CUSTOM_T2S_MAGIC = 0x54325343; // "T2SC"
static constexpr uint8_t CUSTOM_T2S_VERSION = 1;
static constexpr uint32_t CUSTOM_T2S_DEFAULT_BLOB_SIZE = 0;
static constexpr uint8_t CUSTOM_S2T_FLAG_CHO_INDICATION = 0x01;

static uint64_t doubleToU64(double d)
{
    uint64_t u;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}

static double u64ToDouble(uint64_t u)
{
    double d;
    std::memcpy(&d, &u, sizeof(d));
    return d;
}

static void appendString(OctetString &out, const std::string &s)
{
    out.appendOctet2(static_cast<uint16_t>(s.size()));
    for (char c : s)
        out.appendOctet(static_cast<uint8_t>(c));
}

static void appendRefLoc(OctetString &out, const nr::rrc::common::EventReferenceLocation &loc)
{
    out.appendOctet8(doubleToU64(loc.latitudeDeg));
    out.appendOctet8(doubleToU64(loc.longitudeDeg));
}

// Encodes a RrcUeContext into a flat binary OctetString.
// Layout: fixed header | measIdentities | reportConfigEvents | measObjects
static OctetString EncodeCustomRrcContext(RrcUeContext &ue)
{
    OctetString out{};

    // Fixed header (88 bytes)
    out.appendOctet8(static_cast<int64_t>(ue.ueId));
    out.appendOctet4(static_cast<int32_t>(ue.cRnti));
    out.appendOctet4(static_cast<int32_t>(ue.nextHopChainingCount));
    for (uint8_t b : ue.nextHopParameter)
        out.appendOctet(b);
    out.appendOctet2(ue.ueSecInfo.nRencryptionAlgorithmsBitmap);
    out.appendOctet2(ue.ueSecInfo.eUTRAencryptionAlgorithmsBitmap);
    out.appendOctet2(ue.ueSecInfo.nRintegrityProtectionAlgorithmsBitmap);
    out.appendOctet2(ue.ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap);
    for (uint8_t b : ue.ueSecInfo.k_gnb)
        out.appendOctet(b);

    // measIdentities
    out.appendOctet4(static_cast<uint32_t>(ue.measIdentities.size()));
    for (const auto &[key, mi] : ue.measIdentities)
    {
        out.appendOctet8(static_cast<int64_t>(mi.measId));
        out.appendOctet8(static_cast<int64_t>(mi.measObjectId));
        out.appendOctet8(static_cast<int64_t>(mi.reportConfigId));
        out.appendOctet4(static_cast<int32_t>(mi.eventKind));
        appendString(out, mi.eventType);
        out.appendOctet8(static_cast<int64_t>(mi.choProfileId));
    }

    // reportConfigEvents
    out.appendOctet4(static_cast<uint32_t>(ue.reportConfigEvents.size()));
    for (const auto &[key, rc] : ue.reportConfigEvents)
    {
        out.appendOctet8(static_cast<int64_t>(key));
        out.appendOctet4(static_cast<int32_t>(rc.eventId));
        out.appendOctet4(static_cast<int32_t>(rc.reportConfigId));
        out.appendOctet4(static_cast<int32_t>(rc.eventKind));
        appendString(out, rc.eventType);
        out.appendOctet4(static_cast<int32_t>(rc.ttt));
        out.appendOctet4(static_cast<int32_t>(rc.maxReportCells));
        out.appendOctet(rc.reportOnLeave ? 1 : 0);
        out.appendOctet(rc.useAllowedCellList ? 1 : 0);
        out.appendOctet4(static_cast<int32_t>(rc.a2_thresholdDbm));
        out.appendOctet4(static_cast<int32_t>(rc.a2_hysteresisDb));
        out.appendOctet4(static_cast<int32_t>(rc.a3_offsetDb));
        out.appendOctet4(static_cast<int32_t>(rc.a3_hysteresisDb));
        out.appendOctet4(static_cast<int32_t>(rc.a5_threshold1Dbm));
        out.appendOctet4(static_cast<int32_t>(rc.a5_threshold2Dbm));
        out.appendOctet4(static_cast<int32_t>(rc.a5_hysteresisDb));
        out.appendOctet4(static_cast<int32_t>(rc.d1_distanceThreshFromReference1));
        out.appendOctet4(static_cast<int32_t>(rc.d1_distanceThreshFromReference2));
        appendRefLoc(out, rc.d1_referenceLocation1);
        appendRefLoc(out, rc.d1_referenceLocation2);
        out.appendOctet4(static_cast<int32_t>(rc.d1_hysteresisLocation));
        out.appendOctet8(static_cast<int64_t>(rc.condT1_thresholdSecTS));
        out.appendOctet4(static_cast<int32_t>(rc.condT1_durationSec));
        out.appendOctet4(static_cast<int32_t>(rc.condD1_distanceThreshFromReference1));
        out.appendOctet4(static_cast<int32_t>(rc.condD1_distanceThreshFromReference2));
        appendRefLoc(out, rc.condD1_referenceLocation1);
        appendRefLoc(out, rc.condD1_referenceLocation2);
        out.appendOctet4(static_cast<int32_t>(rc.condD1_hysteresisLocation));
        out.appendOctet4(static_cast<int32_t>(rc.condA3_offsetDb));
        out.appendOctet4(static_cast<int32_t>(rc.condA3_hysteresisDb));
    }

    // measObjects
    out.appendOctet4(static_cast<uint32_t>(ue.measObjects.size()));
    for (const auto &[key, mo] : ue.measObjects)
    {
        out.appendOctet8(static_cast<int64_t>(key));
        out.appendOctet4(static_cast<int32_t>(mo.measObjectId));
        out.appendOctet4(static_cast<int32_t>(mo.ssbFrequency));
    }

    return out;
}

// Decodes a flat binary OctetString produced by EncodeCustomRrcContext into a heap-allocated
// RrcUeContext.  Returns nullptr if the buffer is too short or otherwise malformed.
// Caller owns the returned pointer.
static RrcUeContext *DecodeCustomRrcContext(const OctetString &data)
{
    const int total = data.length();
    int off = 0;

    auto avail = [&](int n) { return (off + n) <= total; };

    // fixed header: 8+4+4+32+2+2+2+2+32 = 88 bytes
    if (!avail(88))
        return nullptr;

    int64_t ueId = data.get8L(off); off += 8;
    int cRnti = data.get4I(off); off += 4;
    int nextHopCC = data.get4I(off); off += 4;
    std::array<uint8_t, 32> nhp{};
    for (int i = 0; i < 32; ++i)
        nhp[i] = static_cast<uint8_t>(data.getI(off++));
    uint16_t nrEnc = static_cast<uint16_t>(data.get2I(off)); off += 2;
    uint16_t euEnc = static_cast<uint16_t>(data.get2I(off)); off += 2;
    uint16_t nrInt = static_cast<uint16_t>(data.get2I(off)); off += 2;
    uint16_t euInt = static_cast<uint16_t>(data.get2I(off)); off += 2;
    std::array<uint8_t, 32> kgnb{};
    for (int i = 0; i < 32; ++i)
        kgnb[i] = static_cast<uint8_t>(data.getI(off++));

    auto *ue = new RrcUeContext(ueId);
    ue->cRnti = cRnti;
    ue->nextHopChainingCount = nextHopCC;
    ue->nextHopParameter = nhp;
    ue->ueSecInfo.nRencryptionAlgorithmsBitmap = nrEnc;
    ue->ueSecInfo.eUTRAencryptionAlgorithmsBitmap = euEnc;
    ue->ueSecInfo.nRintegrityProtectionAlgorithmsBitmap = nrInt;
    ue->ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap = euInt;
    ue->ueSecInfo.k_gnb = kgnb;

    // measIdentities
    if (!avail(4)) { delete ue; return nullptr; }
    uint32_t miCount = data.get4UI(off); off += 4;
    for (uint32_t i = 0; i < miCount; ++i)
    {
        if (!avail(28 + 2)) { delete ue; return nullptr; }
        RrcUeContext::MeasIdentityMappings mi{};
        mi.measId = static_cast<long>(data.get8L(off)); off += 8;
        mi.measObjectId = static_cast<long>(data.get8L(off)); off += 8;
        mi.reportConfigId = static_cast<long>(data.get8L(off)); off += 8;
        mi.eventKind = static_cast<nr::rrc::common::HandoverEventType>(data.get4I(off)); off += 4;
        if (!avail(2)) { delete ue; return nullptr; }
        uint16_t slen = static_cast<uint16_t>(data.get2I(off)); off += 2;
        if (!avail(slen + 8)) { delete ue; return nullptr; }
        mi.eventType.resize(slen);
        for (int j = 0; j < slen; ++j)
            mi.eventType[j] = static_cast<char>(data.getI(off++));
        mi.choProfileId = static_cast<long>(data.get8L(off)); off += 8;
        ue->measIdentities[mi.measId] = mi;
    }

    // reportConfigEvents
    if (!avail(4)) { delete ue; return nullptr; }
    uint32_t rcCount = data.get4UI(off); off += 4;
    for (uint32_t i = 0; i < rcCount; ++i)
    {
        if (!avail(20)) { delete ue; return nullptr; }
        long key = static_cast<long>(data.get8L(off)); off += 8;
        nr::rrc::common::ReportConfigEvent rc{};
        rc.eventId = data.get4I(off); off += 4;
        rc.reportConfigId = data.get4I(off); off += 4;
        rc.eventKind = static_cast<nr::rrc::common::HandoverEventType>(data.get4I(off)); off += 4;
        if (!avail(2)) { delete ue; return nullptr; }
        uint16_t slen = static_cast<uint16_t>(data.get2I(off)); off += 2;
        if (!avail(slen)) { delete ue; return nullptr; }
        rc.eventType.resize(slen);
        for (int j = 0; j < slen; ++j)
            rc.eventType[j] = static_cast<char>(data.getI(off++));
        // fixed remainder of ReportConfigEvent: 4+4+1+1+28+8+32+4+8+4+8+32+4+4+4 = 146 bytes
        if (!avail(146)) { delete ue; return nullptr; }
        rc.ttt = static_cast<nr::rrc::common::E_TTT_ms>(data.get4I(off)); off += 4;
        rc.maxReportCells = data.get4I(off); off += 4;
        rc.reportOnLeave = (data.getI(off++) != 0);
        rc.useAllowedCellList = (data.getI(off++) != 0);
        rc.a2_thresholdDbm = data.get4I(off); off += 4;
        rc.a2_hysteresisDb = data.get4I(off); off += 4;
        rc.a3_offsetDb = data.get4I(off); off += 4;
        rc.a3_hysteresisDb = data.get4I(off); off += 4;
        rc.a5_threshold1Dbm = data.get4I(off); off += 4;
        rc.a5_threshold2Dbm = data.get4I(off); off += 4;
        rc.a5_hysteresisDb = data.get4I(off); off += 4;
        rc.d1_distanceThreshFromReference1 = data.get4I(off); off += 4;
        rc.d1_distanceThreshFromReference2 = data.get4I(off); off += 4;
        rc.d1_referenceLocation1.latitudeDeg = u64ToDouble(data.get8UL(off)); off += 8;
        rc.d1_referenceLocation1.longitudeDeg = u64ToDouble(data.get8UL(off)); off += 8;
        rc.d1_referenceLocation2.latitudeDeg = u64ToDouble(data.get8UL(off)); off += 8;
        rc.d1_referenceLocation2.longitudeDeg = u64ToDouble(data.get8UL(off)); off += 8;
        rc.d1_hysteresisLocation = data.get4I(off); off += 4;
        rc.condT1_thresholdSecTS = static_cast<long>(data.get8L(off)); off += 8;
        rc.condT1_durationSec = data.get4I(off); off += 4;
        rc.condD1_distanceThreshFromReference1 = data.get4I(off); off += 4;
        rc.condD1_distanceThreshFromReference2 = data.get4I(off); off += 4;
        rc.condD1_referenceLocation1.latitudeDeg = u64ToDouble(data.get8UL(off)); off += 8;
        rc.condD1_referenceLocation1.longitudeDeg = u64ToDouble(data.get8UL(off)); off += 8;
        rc.condD1_referenceLocation2.latitudeDeg = u64ToDouble(data.get8UL(off)); off += 8;
        rc.condD1_referenceLocation2.longitudeDeg = u64ToDouble(data.get8UL(off)); off += 8;
        rc.condD1_hysteresisLocation = data.get4I(off); off += 4;
        rc.condA3_offsetDb = data.get4I(off); off += 4;
        rc.condA3_hysteresisDb = data.get4I(off); off += 4;
        ue->reportConfigEvents[key] = rc;
    }

    // measObjects
    if (!avail(4)) { delete ue; return nullptr; }
    uint32_t moCount = data.get4UI(off); off += 4;
    for (uint32_t i = 0; i < moCount; ++i)
    {
        if (!avail(16)) { delete ue; return nullptr; }
        long key = static_cast<long>(data.get8L(off)); off += 8;
        nr::rrc::common::MeasObject mo{};
        mo.measObjectId = data.get4I(off); off += 4;
        mo.ssbFrequency = data.get4I(off); off += 4;
        ue->measObjects[key] = mo;
    }

    return ue;
}

/**
 * @brief Constructs a custom SourceToTarget transparent container for use in handover operations.
 * 
 * This is not standards-compliant.  However, since the TC is opaque to all elements except the gNBs, we can use
 * a custom format that only includes the information needed by the non-radio layers for handover.
 * The blobSize value is used to pad the size of the container, which can be used to simulate larger real payloads. 
 *
 * @param ue The UE context
 * @param blobSize The size of the spare data blob to include
 * @param choIndication Whether this is a ConditionalHandover (CHO) request
 */
std::unique_ptr<OctetString> GnbRrcTask::makeSourceToTargetTransparentContainerSimulated(RrcUeContext &ue, uint32_t blobSize, bool choIndication)
{
    auto rrcContext = EncodeCustomRrcContext(ue);
    if (rrcContext.length() == 0)
        return nullptr;

    auto blob = OctetString::FromSpare(static_cast<int>(blobSize));

    OctetString encoded{};
    encoded.appendOctet4(CUSTOM_S2T_MAGIC);
    encoded.appendOctet(CUSTOM_S2T_VERSION);
    // indicator that this a ConditionalHandover (CHO) request
    uint8_t flags = choIndication ? CUSTOM_S2T_FLAG_CHO_INDICATION : 0;
    encoded.appendOctet(flags);
    encoded.appendOctet2(0); // reserved

    // relevant UE context information
    encoded.appendOctet4(static_cast<uint32_t>(rrcContext.length()));
    encoded.appendOctet4(static_cast<uint32_t>(blob.length()));
    encoded.append(rrcContext);
    encoded.append(blob);

    return std::make_unique<OctetString>(std::move(encoded));
}

/**
 * Inverse of makeSourceToTargetTransparentContainerSimulated(): strips the
 * 16-byte wrapper (magic, version, flags, reserved, rrcContext length, blob
 * length) and yields just the EncodeCustomRrcContext() payload.
 *
 * Without this the target fed the whole wrapped container straight into
 * DecodeCustomRrcContext(), which reads a ueId from offset 0 and so parsed the
 * ASCII magic "S2TC" as the UE identity. Nothing caught it because the
 * target-side container decode had never actually executed.
 */
static bool UnwrapSourceToTargetContainer(const OctetString &container, OctetString &rrcContextOut,
                                          bool *choIndicationOut)
{
    static constexpr int HEADER_SIZE = 16;
    if (container.length() < HEADER_SIZE)
        return false;

    if (static_cast<uint32_t>(container.get4I(0)) != CUSTOM_S2T_MAGIC)
        return false;
    if (static_cast<uint8_t>(container.getI(4)) != CUSTOM_S2T_VERSION)
        return false;

    const uint8_t flags = static_cast<uint8_t>(container.getI(5));
    // octets 6..7 are reserved
    const int contextLen = container.get4I(8);
    // octets 12..15 hold the trailing blob length, which the target ignores.

    if (contextLen < 0 || HEADER_SIZE + contextLen > container.length())
        return false;

    if (choIndicationOut)
        *choIndicationOut = (flags & CUSTOM_S2T_FLAG_CHO_INDICATION) != 0;

    rrcContextOut = container.subCopy(HEADER_SIZE, contextLen);
    return true;
}



} // namespace gnb
