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



static std::vector<int> getActiveChoProfileIndices(
    const GnbHandoverConfig &config, Logger *logger)
{
    std::vector<int> result{};
    if (!config.choEnabled)
        return result;

    for (int id : config.choActiveProfileIds)
    {
        bool found = false;
        for (size_t i = 0; i < config.candidateProfiles.size(); ++i)
        {
            if (config.candidateProfiles[i].candidateProfileId == id)
            {
                result.push_back(static_cast<int>(i));
                found = true;
                break;
            }
        }
        if (!found && logger)
            logger->warn("CHO active profile id=%d not found in candidateProfiles; skipping.", id);
    }

    if (result.empty() && logger)
        logger->warn("No active CHO profiles resolved from choActiveProfileIds.");

    return result;
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
 * @param ueEcef            UE ECEF position (meters); zero vector if unknown
 * @param tExitSec          T_exit: seconds from now when serving gNB exits coverage
 * @param elevationMinDeg   Minimum elevation angle threshold (integer degrees)
 * @return Sorted vector of (GnbNeighborConfig*, score), best candidate first
 */
std::vector<ScoredNeighbor> GnbRrcTask::prioritizeNeighbors(
    const std::vector<GnbNeighborConfig> &neighborList,
    int servingPci,
    const EcefPosition &ueEcef,
    int tExitSec)
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
    if (!hasUePos || m_satNeighborhoodCache.empty())
        return fallback();

    static constexpr int MAX_LOOKAHEAD_SEC = 7200;
    const int64_t satNowMs = m_base->satTime->CurrentSatTimeMillis();

    const auto ranked = m_base->satStates->PrioritizeTargetSats(m_satNeighborhoodCache,
                                                               ueEcef,
                                                               tExitSec,
                                                               m_config->ntn.elevationMinDeg,
                                                               MAX_LOOKAHEAD_SEC);
                                                               
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

    m_logger->debug("UE[%d] RRCReconfigurationComplete received with txId=%ld cRnti=%d matchedPendingHandover=%s",
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

        m_logger->info("UE[%d] Handover completed. NGAP layer notification sent.", resolvedUeId);
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

    int measId = static_cast<int>(results.measId);

    if (ue->measIdentities.count(measId) == 0)
    {
        m_logger->warn("UE[%d] MeasurementReport unknown measId=%ld", resolvedUeId, measId);
        return;
    }

    
    auto sentMeasIdentityPair = ue->measIdentities.find(measId);
    auto sentMeasIdentity = sentMeasIdentityPair->second;

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

    auto event_str = nr::rrc::common::HandoverEventTypeToString(ue->reportConfigEvents.find(sentMeasIdentity.reportConfigId)->second.eventKind);
    m_logger->info("UE[%d] MeasurementReport measId=%ld event=%s servingRSRP=%ddBm",
                   resolvedUeId, measId, event_str.c_str(), servingRsrp);

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
    evaluateHandoverDecision(resolvedUeId, measId);
}

/**
 * @brief Creates and sends an RRCReconfiguration message with ReconfigurationWithSync IE to UE, which
 * instructs UE to handover to the target PCI, using the new C-RNTI and T304 timer specified.
 * Note - in real implementations this would be forwarding a RWS message created by the targetGNB.  Since 
 * the only parameters we need to pass over are the targetPCI, cRNTI and T304 value, we just
 * make it here from the values provided in the target's transparent container.
 * 
 * @param ueId 
 * @param targetPci 
 * @param newCrnti 
 * @param t304Ms 
 */
void GnbRrcTask::sendUeHandoverMessage(int ueId, int targetPci, int newCrnti, int t304Ms)
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
        // search vector for id in measId field of MeasIdentityMappings structs, if not found, break and use it.  
        //  If found, increment and keep searching
        auto it = ue->measIdentities.find(id);

        if (it == ue->measIdentities.end())
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

    ue->measObjects.clear();
    ue->reportConfigEvents.clear();
    ue->measIdentities.clear();

    // clear all the ID trackers
    ue->usedMeasObjectIds.clear();
    ue->usedReportConfigIds.clear();
    //ue->usedMeasIdentities.clear();

    // clears the MeasConfig pointers
    ue->sentMeasConfigs.clear();
}


/**
 * @brief Creates a MeasConfig IE.  If isChoEnbaled is true, the MeasConfig will be built using
 * the Conditional Handover ASN types.  Otherwise, a default MeasConfig will be created 
 * using normal "eventTriggered" reporting events.
 * 
 * @param[in/out] mc Output pointer to the created MeasConfig.
 * @param[in] ue The UE RRC context
 * @param[in] taggedEvents The list of selected handover events
 * @return ASN_RRC_MeasConfig* 
 */
std::vector<long> GnbRrcTask::createMeasConfig(
    ASN_RRC_MeasConfig *&mc,
    RrcUeContext *ue,
    std::vector<std::pair<ReportConfigEvent, int>> taggedEvents
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
        MeasObject measObj;
        measObj.measObjectId = DUMMY_MEAS_OBJECT_ID;

        mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
        auto *measObjAsn = asn::New<ASN_RRC_MeasObjectToAddMod>();
        measObjAsn->measObjectId = DUMMY_MEAS_OBJECT_ID;
        measObjAsn->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
        measObjAsn->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
        measObjAsn->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
        *measObjAsn->measObject.choice.measObjectNR->ssbFrequency = 632628; // typical n78 SSB freq
        measObjAsn->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
        *measObjAsn->measObject.choice.measObjectNR->ssbSubcarrierSpacing = ASN_RRC_SubcarrierSpacing_kHz30;

        // smtc1 (SSB MTC periodicity/offset/duration) - required for NR measObject.
        measObjAsn->measObject.choice.measObjectNR->smtc1 = asn::New<ASN_RRC_SSB_MTC>();
        measObjAsn->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
            ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
        measObjAsn->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
        measObjAsn->measObject.choice.measObjectNR->smtc1->duration = ASN_RRC_SSB_MTC__duration_sf1;
        // quantityConfigIndex is mandatory INTEGER (1..maxNrofQuantityConfig); must be >= 1.
        measObjAsn->measObject.choice.measObjectNR->quantityConfigIndex = 1;
        
        // add pointer to target gnb in the cellsToAddModdList
        // measObjAsn->measObject.choice.measObjectNR->cellsToAddModList = asn::New<ASN_RRC_CellsToAddModList>();
        // auto *cell = asn::New<ASN_RRC_CellsToAddMod>();
        // cell->physCellId = 0;  // set to PCI of target cell
        // asn::SequenceAdd(*measObjAsn->measObject.choice.measObjectNR->cellsToAddModList, cell);
        
        asn::SequenceAdd(*mc->measObjectToAddModList, measObjAsn);

        // store in UE context
        ue->measObjects.emplace(DUMMY_MEAS_OBJECT_ID, measObj);
    }

    // ReportConfig list: one ReportConfig per configured handover event type.
    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();
    
    for (size_t i = 0; i < taggedEvents.size(); i++)
    {
        const auto &event = taggedEvents[i].first;
        const int eventChoProfileId = taggedEvents[i].second;
        bool cho_event = !IsMeasurementEvent(event.eventKind);
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

        ReportConfigEvent reportConfig;
        reportConfig.reportConfigId = reportConfigId;
        reportConfig.eventKind = eventKind;

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

                reportConfig.a2_hysteresisDb = event.a2_hysteresisDb;
                reportConfig.a2_thresholdDbm = event.a2_thresholdDbm;
                reportConfig.ttt = event.ttt;

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

                reportConfig.a3_hysteresisDb = event.a3_hysteresisDb;
                reportConfig.a3_offsetDb = event.a3_offsetDb;
                reportConfig.ttt = event.ttt;

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

                reportConfig.a5_threshold1Dbm = event.a5_threshold1Dbm;
                reportConfig.a5_threshold2Dbm = event.a5_threshold2Dbm;
                reportConfig.a5_hysteresisDb = event.a5_hysteresisDb;
                reportConfig.ttt = event.ttt;

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

                d1->distanceThreshFromReference1_r17 =
                    distanceThresholdToASNValue(event.d1_distanceThreshFromReference1);
                d1->distanceThreshFromReference2_r17 =
                    distanceThresholdToASNValue(event.d1_distanceThreshFromReference2);

                referenceLocationToAsnValue(event.d1_referenceLocation1, d1->referenceLocation1_r17);
                referenceLocationToAsnValue(event.d1_referenceLocation2, d1->referenceLocation2_r17);
                d1->hysteresisLocation_r17 = nr::rrc::common::hysteresisLocationToASNValue(event.d1_hysteresisLocation);

                d1->reportOnLeave_r17 = true;
                d1->timeToTrigger = tttMsToASNValue(event.ttt);

                reportConfig.d1_distanceThreshFromReference1 = event.d1_distanceThreshFromReference1;
                reportConfig.d1_distanceThreshFromReference2 = event.d1_distanceThreshFromReference2;
                reportConfig.d1_referenceLocation1 = event.d1_referenceLocation1;
                reportConfig.d1_referenceLocation2 = event.d1_referenceLocation2;
                reportConfig.d1_hysteresisLocation = event.d1_hysteresisLocation;
                reportConfig.ttt = event.ttt;

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=D1 distThresh1=%dm distThresh2=%dm "
                                "hysteresis=%dm "
                                "ttt=%s",
                                ue->ueId,
                                measId,
                                reportConfig.d1_distanceThreshFromReference1,
                                reportConfig.d1_distanceThreshFromReference2,
                                reportConfig.d1_hysteresisLocation,
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

                reportConfig.condA3_hysteresisDb = event.condA3_hysteresisDb;
                reportConfig.condA3_offsetDb = event.condA3_offsetDb;
                reportConfig.ttt = event.ttt;

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventA3 offset=%ddB hysteresis=%ddB ttt=%s",
                                ue->ueId, measId, event.condA3_offsetDb, event.condA3_hysteresisDb, nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }

            else if (eventKind == HandoverEventType::CondD1)
            {
                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventD1_r17;
                asn::MakeNew(ctc->condEventId.choice.condEventD1_r17);

                auto *d1 = ctc->condEventId.choice.condEventD1_r17;

                // set trigger params from provided Trigger object
                d1->distanceThreshFromReference1_r17 = distanceThresholdToASNValue(event.condD1_distanceThreshFromReference1);
                d1->distanceThreshFromReference2_r17 = distanceThresholdToASNValue(event.condD1_distanceThreshFromReference2);
                referenceLocationToAsnValue(event.condD1_referenceLocation1, d1->referenceLocation1_r17);
                referenceLocationToAsnValue(event.condD1_referenceLocation2, d1->referenceLocation2_r17);
                d1->hysteresisLocation_r17 = nr::rrc::common::hysteresisLocationToASNValue(event.condD1_hysteresisLocation);

                // use config for ttt
                d1->timeToTrigger_r17 = tttMsToASNValue(event.ttt);

                reportConfig.condD1_distanceThreshFromReference1 = event.condD1_distanceThreshFromReference1;
                reportConfig.condD1_distanceThreshFromReference2 = event.condD1_distanceThreshFromReference2;
                reportConfig.condD1_referenceLocation1 = event.condD1_referenceLocation1;
                reportConfig.condD1_referenceLocation2 = event.condD1_referenceLocation2;
                reportConfig.condD1_hysteresisLocation = event.condD1_hysteresisLocation;
                reportConfig.ttt = event.ttt;
                
                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventD1 distThresh1=%dm distThresh2=%dm "
                                "hysteresis=%dm "
                                "ttt=%s",
                                ue->ueId,
                                measId,
                                reportConfig.condD1_distanceThreshFromReference1,
                                reportConfig.condD1_distanceThreshFromReference2,
                                reportConfig.condD1_hysteresisLocation,
                                nr::rrc::common::E_TTT_ms_to_string(event.ttt));
            }
            else if (eventKind == HandoverEventType::CondT1)
            {
                ctc->condEventId.present = ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventT1_r17;
                asn::MakeNew(ctc->condEventId.choice.condEventT1_r17);

                auto *t1 = ctc->condEventId.choice.condEventT1_r17;
                // set trigger params from provided Trigger object
                t1->t1_Threshold_r17 = nr::rrc::common::t1ThresholdToASNValue(event.condT1_thresholdSecTS);
                t1->duration_r17 = nr::rrc::common::durationToASNValue(event.condT1_durationSec);

                reportConfig.condT1_thresholdSecTS = event.condT1_thresholdSecTS;
                reportConfig.condT1_durationSec = event.condT1_durationSec;

                m_logger->debug("UE[%d] MeasConfig measId=%ld event=condEventT1 threshold=%dsec duration=%dsec",
                                ue->ueId, measId, reportConfig.condT1_thresholdSecTS, reportConfig.condT1_durationSec);
            }
            else
            {
                m_logger->warn("UE[%d] Unsupported conditional event type %s, skipping", ue->ueId, eventType.c_str());
                continue;
            }

            
        }

        asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);

        ue->reportConfigEvents.emplace(reportConfigId, reportConfig);

        // Create the MeasId

        auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
        measIdMod->measId = measId;
        measIdMod->measObjectId = DUMMY_MEAS_OBJECT_ID; // the dummy NR measObject we defined above
        measIdMod->reportConfigId = reportConfigId;
        asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);

        // update ue's MeasConfig trackers
        ue->measIdentities[measId] = {measId, DUMMY_MEAS_OBJECT_ID, reportConfigId, eventKind, eventType,
                                      cho_event ? static_cast<long>(eventChoProfileId) : -1L};

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

    if (!forceResend && !ue->measIdentities.empty())
        return; // already configured

    if (forceResend && !ue->measIdentities.empty())
    {
        m_logger->info("UE[%d] Forcing MeasConfig resend after handover", ue->ueId);
        clearMeasConfig(ue);
    }

    // Resolve basic (non-CHO) measurement events from basicHandoverMeasIdentities
    std::vector<ReportConfigEvent> basicEvents{};
    for (int evId : m_config->handover.basicHandoverMeasIdentities)
    {
        auto it = m_config->handover.eventsById.find(evId);
        if (it == m_config->handover.eventsById.end())
        {
            m_logger->warn("UE[%d] basicHandoverMeasIdentities: eventId=%d not found in events; skipping", ueId, evId);
            continue;
        }
        if (!IsMeasurementEvent(it->second.eventKind))
        {
            m_logger->warn("UE[%d] basicHandoverMeasIdentities: eventId=%d type=%s is not a measurement event; skipping",
                           ueId, evId, it->second.eventStr());
            continue;
        }
        basicEvents.push_back(it->second);
    }
    if (basicEvents.empty())
        m_logger->warn("UE[%d] No basic handover measurement events configured", ue->ueId);

    // Resolve CHO condition events from active profile conditionEventIds
    bool do_cho = m_config->handover.choEnabled;
    std::vector<int> activeChoProfileIndices{};
    if (do_cho)
    {
        activeChoProfileIndices = getActiveChoProfileIndices(m_config->handover, m_logger.get());
        if (activeChoProfileIndices.empty())
        {
            m_logger->warn("UE[%d] CHO enabled but no active CHO profiles resolved; skipping CHO", ueId);
            do_cho = false;
        }
    }

    // Build tagged event list: basic events use profile index -1; each CHO profile's
    // conditions carry that profile's index so measIdentities can be keyed per-profile.
    std::vector<std::pair<ReportConfigEvent, int>> taggedEvents{};
    for (const auto &ev : basicEvents)
        taggedEvents.emplace_back(ev, -1);
    if (do_cho)
    {
        for (int profileIdx : activeChoProfileIndices)
        {
            const auto &choProfile = m_config->handover.candidateProfiles[profileIdx];
            if (choProfile.conditionEventIds.empty())
            {
                m_logger->warn("UE[%d] CHO profile idx=%d has no condition event IDs; skipping it", ueId, profileIdx);
                continue;
            }
            for (int evId : choProfile.conditionEventIds)
            {
                auto it = m_config->handover.eventsById.find(evId);
                if (it == m_config->handover.eventsById.end())
                {
                    m_logger->warn("UE[%d] CHO profile idx=%d: conditionEventId=%d not found in events; skipping",
                                   ueId, profileIdx, evId);
                    continue;
                }
                if (!IsConditionalEvent(it->second.eventKind))
                {
                    m_logger->warn("UE[%d] CHO profile idx=%d: conditionEventId=%d type=%s is not a conditional event; skipping",
                                   ueId, profileIdx, evId, it->second.eventStr());
                    continue;
                }
                taggedEvents.emplace_back(it->second, profileIdx);
            }
        }
    }

    if (taggedEvents.empty())
    {
        m_logger->warn("UE[%d] No handover events to configure after resolving IDs, exiting MeasConfig", ue->ueId);
        return;
    }

    bool need_location = false;
    bool need_time = false;
    std::string eventList{};
    for (size_t i = 0; i < taggedEvents.size(); i++)
    {
        const auto &ev = taggedEvents[i].first;
        if (ev.eventKind == HandoverEventType::CondD1 || ev.eventKind == HandoverEventType::D1)
            need_location = true;

        if (ev.eventKind == HandoverEventType::CondT1)
            need_time = true;

        if (i != 0)
            eventList += ",";
        eventList += std::to_string(ev.eventId) + "-" + nr::rrc::common::HandoverEventTypeToString(ev.eventKind);
    }

    m_logger->info("UE[%d] Creating MeasConfig, eventType(s)=%s", ue->ueId, eventList.c_str());

    // Determine handover trigger conditions

    // Here is where we call the satellite position calculations to determine the
    // time and distance when this gnb will be out of range of teh UE, and use that to set the tServiceSec and the distance threshold for D1.
    // We need this now to determine the time to use for evaluating which gnbs are in range

    const int ownPci = cons::getPciFromNci(m_config->nci);

    nr::rrc::common::DynamicEventTriggerParams dynTriggerParams{};
    // only run the dynamic parameters if we need location of time triggers
    //   and in NTN mode
    if ((need_location || need_time) && m_config->ntn.ntnEnabled)
    {
        satHandoverTriggerCalc(dynTriggerParams, ownPci, ue);
    }

    // update each event with calculated values as needed
    for (auto &[event, profileIdx] : taggedEvents)
    {
        switch (event.eventKind)
        {
            case HandoverEventType::A2:{
                break;
            }
            case HandoverEventType::A3: {
                break;
            }
            case HandoverEventType::A5: {
                break;
            }
            case HandoverEventType::D1: {
                // in satellite mode, use the calculated distance values
                if (m_config->ntn.ntnEnabled) {
                    event.d1_distanceThreshFromReference1 = dynTriggerParams.d1_distanceThresholdFromReference1;
                    event.d1_distanceThreshFromReference2 = dynTriggerParams.d1_distanceThresholdFromReference2;
                    event.d1_referenceLocation1 = dynTriggerParams.d1_referenceLocation1;
                    event.d1_referenceLocation2 = dynTriggerParams.d1_referenceLocation2;
                    event.d1_hysteresisLocation = dynTriggerParams.d1_hysteresisLocation;
                }
                // in terrestrial mode, just use event's provided values
                break;
            }
            case HandoverEventType::CondA3:{
                break;
            }
            case HandoverEventType::CondD1: {
                // in NTN-mode, use calculated values
                if (m_config->ntn.ntnEnabled) {
                    event.condD1_distanceThreshFromReference1 = dynTriggerParams.condD1_distanceThresholdFromReference1;
                    event.condD1_distanceThreshFromReference2 = dynTriggerParams.condD1_distanceThresholdFromReference2;
                    event.condD1_referenceLocation1 = dynTriggerParams.condD1_referenceLocation1;
                    event.condD1_referenceLocation2 = dynTriggerParams.condD1_referenceLocation2;
                    event.condD1_hysteresisLocation = dynTriggerParams.condD1_hysteresisLocation;
                }
                // in terrestrial mode, just use event's provided values
                break;
            }
            case HandoverEventType::CondT1:{
                // in NTN-mode, use calculated values
                if (m_config->ntn.ntnEnabled) {
                    event.condT1_thresholdSecTS = dynTriggerParams.condT1_thresholdSec;
                    event.condT1_durationSec = dynTriggerParams.condT1_durationSec;
                }
                // in terrestrial mode, use event value as offset to current time
                else {
                    // for timer, use config values as deltas, add to current time
                    event.condT1_thresholdSecTS += utils::CurrentTimeMillis() / 1000;
                }
                break;
            }

        }
    }

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

    auto usedMeasIds = createMeasConfig(mc, ue, taggedEvents);
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

    m_logger->info("UE[%d] RRCReconfiguration sent with MeasConfig (%zu measId entries), txId=%ld",
                   ue->ueId, usedMeasIds.size(), txId);

    // if conditional handover enabled, kick off CHO preparation for each active profile
    if (do_cho)
    {
        for (int profileIdx : activeChoProfileIndices)
            processConditionalHandover(ue->ueId, dynTriggerParams, profileIdx);
    }
}

/**
 * @brief Evaluates whether to trigger a handover based on the latest measurement report from the UE.
 * 
 * @param ueId UE ID of UE providing measurement report 
 * @param measId Measurement ID of the measurement report
 */
void GnbRrcTask::evaluateHandoverDecision(int ueId, int measId)
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

    auto mi = ue->measIdentities.find(measId)->second;
    auto rc = ue->reportConfigEvents.find(mi.reportConfigId)->second;

    // only evaluate events that use measurement reports
    if (!IsMeasurementEvent(rc.eventKind))
        return;

    bool shouldHandover = false;

    if (rc.eventKind == HandoverEventType::A2)
    {
        // A2: serving RSRP < threshold - hysteresis
        shouldHandover = rc.evaluateA2(servingRsrp);
        m_logger->debug("UE[%d] HandoverEval: event=A2 measId serving=%ddBm threshold=%ddBm hysteresis=%ddB "
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
        m_logger->debug("UE[%d] HandoverEval: event=A3 serving=%ddBm bestNeighPci=%d bestNeigh=%ddBm "
                        "offset=%ddB hysteresis=%ddB condition=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighPci, bestNeighRsrp,
                        rc.a3_offsetDb, rc.a3_hysteresisDb,
                        bestNeighRsrp, servingRsrp + rc.a3_offsetDb + rc.a3_hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (rc.eventKind == HandoverEventType::A5)
    {
        // A5: serving RSRP < threshold1 - hysteresis AND neighbor RSRP > threshold2 + hysteresis
        //   we just check against the best neighbor
        shouldHandover = rc.evaluateA5Serving(servingRsrp) && rc.evaluateA5Neighbor(bestNeighRsrp);
        m_logger->debug("UE[%d] HandoverEval: event=A5 serving=%ddBm bestNeighPci=%d bestNeigh=%ddBm "
                        "thr1=%ddBm thr2=%ddBm hysteresis=%ddB cond1=(%d < %d) cond2=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighPci, bestNeighRsrp,
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
            m_logger->warn("UE[%d] HandoverEval: event=D1 but UE position is invalid, skipping evaluation",
                           ue->ueId);
            return;
        }
        
        shouldHandover = rc.evaluateD1(uePos.value());
        
        m_logger->debug("UE[%d] HandoverEval: event=D1 "
                        "distThresh1=%dm distThresh2=%dm hysteresis=%dm "
                        "cond1=(%d > %d) cond2=(%d < %d) result=%s",
                        ue->ueId, rc.d1_distanceThreshFromReference1, rc.d1_distanceThreshFromReference2,
                        rc.d1_hysteresisLocation,
                        shouldHandover ? "true" : "false");
    }
    else
    {
        m_logger->warn("UE[%d] HandoverEval: unsupported event type %s", ue->ueId, rc.eventStr());
        return;
    }

    if (!shouldHandover)
        return;

    // For A2, a target may not be present in this report; use last known neighbor if available.
    if (bestNeighPci < 0)
    {
        m_logger->warn("UE[%d] Handover decision met event=%s but no neighbor PCI available",
                       ue->ueId, rc.eventStr());
        return;
    }

    m_logger->info("UE[%d] Handover decision (%s): targetPCI=%d (serving=%ddBm, target=%ddBm)",
                   ue->ueId, rc.eventStr(), bestNeighPci, servingRsrp, bestNeighRsrp);

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

    // per-profile pending guard: don't restart preparation for a profile already in flight
    auto &prepState = ue->choPreparations[choProfileIdx];
    if (prepState.pending)
        return;

    const int ownPci = cons::getPciFromNci(m_config->nci);
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
        m_logger->warn("UE[%d] -- UE position is invalid, using trigger defaults", ue->ueId);
    }

    // prioritizeNeighbors returns a sorted vector of (neighborCell, score) pairs
    //   (higher score = higher priority = longer transit time above theta_e)
    auto prioritizedNeighbors = prioritizeNeighbors(neighborSnapshot, ownPci, ueEcefPos, dynTriggerParams.condT1_thresholdSec);

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
            int64_t pci = cons::getPciFromNci(nci);
            const auto *nbr = findNeighborByNci(neighborSnapshot, pci);
            if (!nbr)
            {
                m_logger->warn("UE[%d] CHO profile %d: targetCellId=0x%llx not in neighborList; skipping",
                               ueId, choProfileIdx, static_cast<long long>(nci));
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
        m_logger->warn("UE[%d] CHO profile %d: no valid target neighbors; skipping preparation",
                       ueId, choProfileIdx);
        return;
    }

    // mark this profile's preparation as pending and store candidates
    prepState.pending = true;
    prepState.candidatePcis.clear();
    prepState.candidateScores.clear();

    for (const auto &n : prioritizedNeighbors)
    {
        const int pci = n.neighbor->getPci();
        prepState.candidatePcis.push_back(pci);
        prepState.candidateScores[pci] = n.score;
    }

    if (prepState.candidatePcis.empty())
    {
        prepState.pending = false;
        m_logger->warn("UE[%d] CHO profile %d: candidate list empty after filtering; skipping", ueId, choProfileIdx);
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

    for (int targetPci : prepState.candidatePcis)
    {
        auto w = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
        w->ueId = ue->ueId;
        w->hoTargetPci = targetPci;
        w->hoCause = NgapCause::RadioNetwork_handover_desirable_for_radio_reason;
        w->hoForChoPreparation = true;
        m_base->ngapTask->push(std::move(w));
    }

    std::string measIdsStr;
    for (size_t i = 0; i < prepState.measIds.size(); ++i) {
        if (i > 0) measIdsStr += ", ";
        measIdsStr += std::to_string(prepState.measIds[i]);
    }
    std::string pcisStr;
    for (size_t i = 0; i < prepState.candidatePcis.size(); ++i) {
        if (i > 0) pcisStr += ", ";
        pcisStr += std::to_string(prepState.candidatePcis[i]);
    }
    m_logger->info("UE[%d] CHO profile %d: prepare sent to NGAP measIds=[%s] total_candidates=%zu targetPCIs=[%s]",
                   ue->ueId,
                   choProfileIdx,
                   measIdsStr.c_str(),
                   prepState.candidatePcis.size(),
                   pcisStr.c_str()
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
        m_logger->err(" UE[%d] Failed to decode handover RRC container as DL-DCCH message", ueId);
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

    m_logger->info("UE[%d] RRCReconfiguration from NGAP forwarded to UE", ueId);

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

        // find which profile owns this targetPci
        int owningProfile = -1;
        for (auto &[profileIdx, prepState] : ue->choPreparations)
        {
            auto it = std::find(prepState.candidatePcis.begin(), prepState.candidatePcis.end(), targetPci);
            if (it != prepState.candidatePcis.end())
            {
                prepState.candidatePcis.erase(it);
                prepState.candidateScores.erase(targetPci);
                owningProfile = profileIdx;
                m_logger->debug("UE[%d] Removed targetPCI=%d from CHO profile %d candidates", ueId, targetPci, profileIdx);
                if (prepState.candidatePcis.empty())
                {
                    clearChoPendingState(ue, profileIdx);
                    m_logger->debug("UE[%d] CHO profile %d: no further candidates; cleared state", ueId, profileIdx);
                }
                break;
            }
        }
        if (owningProfile < 0)
            m_logger->warn("UE[%d] CHO failure for targetPCI=%d not found in any pending profile", ueId, targetPci);

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
    // find the profile whose candidatePcis contains the responding target PCI —
    // we don't yet know candidatePci, so we defer the lookup until after extraction.
    // First confirm at least one profile has pending state.
    if (ue->choPreparations.empty())
    {
        m_logger->warn("UE[%d] CHO prepare response arrived with no pending CHO state; ignoring", ue->ueId);
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

    // locate the candidate PCI across all pending profiles
    //   NOTE: key limitation - if the same candidate PCI is present in multiple profiles, we will match it to the first profile and ignore the others.
    //   So currently we cannot send multiple CHO preparations with the same candidate PCI per UE.  In practice this should not happen,
    //   but relevant for testing.
    int owningProfileIdx = -1;
    ChoPreparationState *prepState = nullptr;
    for (auto &[profileIdx, state] : ue->choPreparations)
    {
        auto it = std::find(state.candidatePcis.begin(), state.candidatePcis.end(), candidatePci);
        if (it != state.candidatePcis.end())
        {
            state.candidatePcis.erase(it);
            owningProfileIdx = profileIdx;
            prepState = &state;
            break;
        }
    }
    if (!prepState)
    {
        m_logger->warn("UE[%d] CHO prepare response targetPCI=%d not found in any pending profile; ignoring",
                        ue->ueId, candidatePci);
        return;
    }

    
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

    auto *addMod = asn::New<ASN_RRC_CondReconfigToAddMod>();

    // select a unique condReconfigId
    int condReconfigId = -1;
    if (!allocateUniqueCondReconfigId(ue, condReconfigId))
    {
        m_logger->err("UE[%d] CHO candidate skipped: no free condReconfigId in range %d..%d",
                      ue->ueId,
                      MIN_COND_RECONFIG_ID,
                      MAX_COND_RECONFIG_ID);

        prepState->candidateScores.erase(candidatePci);
        if (prepState->candidatePcis.empty())
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

    m_logger->info("UE[%d] CHO profile %d: RRCReconfiguration sent targetPCI=%d condReconfigId=%d "
                    "measIds=[%s] pendingCandidates=%zu txId=%d",
                    ue->ueId,
                    owningProfileIdx,
                    candidatePci,
                    condReconfigId,
                    measIdsStr.c_str(),
                    prepState->candidatePcis.size(),
                    txId);

    // if this profile has no more candidates, clear its state
    prepState->candidateScores.erase(candidatePci);
    if (prepState->candidatePcis.empty())
        clearChoPendingState(ue, owningProfileIdx);

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
OctetString GnbRrcTask::getHandoverMeasConfigRrcReconfiguration(int ueId) const
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
 * Also builds the RRCReconfiguration message to be included in the Target2Source
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
        ctx->measIdentities[item.measId] = {item.measId, item.measObjectId, item.reportConfigId,
                                            item.eventKind, item.eventType, 0};
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