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

#include <gnb/ngap/task.hpp>
#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
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
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_SpCellConfig.h>
#include <asn/rrc/ASN_RRC_ReconfigurationWithSync.h>
#include <asn/rrc/ASN_RRC_ServingCellConfigCommon.h>
#include <asn/rrc/ASN_RRC_ConditionalReconfiguration.h>
#include <asn/rrc/ASN_RRC_CondReconfigToAddMod.h>
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
#include <utility>

const int MIN_RSRP = cons::MIN_RSRP; // minimum RSRP value (in dBm) to use when no measurement is available
const int HANDOVER_TIMEOUT_MS = 5000; // time to wait for handover completion before considering it failed

namespace nr::gnb
{

/* ================================================================== */
/*  Helper: RSRP_Range → dBm                                         */
/* ================================================================== */

static int rsrpRangeToDbm(long val)
{
    return static_cast<int>(val) - 156; // TS 38.133 Table 10.1.6.1-1
}

static long rsrpDbmToRange(int dbm)
{
    int val = dbm + 156;
    if (val < 0)
        val = 0;
    if (val > 127)
        val = 127;
    return static_cast<long>(val);
}

static long dbToHalfDbSteps(int db)
{
    int val = db * 2;
    if (val < 0)
        val = 0;
    if (val > 30)
        val = 30;
    return static_cast<long>(val);
}

static long tttMsToEnum(int tttMs)
{
    static const int kTttValuesMs[] = {
        0, 40, 64, 80, 100, 128, 160, 256,
        320, 480, 512, 640, 1024, 1280, 2560, 5120,
    };

    if (tttMs <= kTttValuesMs[0])
        return 0;

    for (size_t i = 1; i < sizeof(kTttValuesMs) / sizeof(kTttValuesMs[0]); i++)
    {
        if (tttMs <= kTttValuesMs[i])
            return static_cast<long>(i);
    }

    return 15;
}

/* ================================================================== */
/*  Helper: T304 milliseconds → ASN enum value                        */
/* ================================================================== */

static long t304MsToEnum(int ms)
{
    switch (ms)
    {
    case 50:    return 0;
    case 100:   return 1;
    case 150:   return 2;
    case 200:   return 3;
    case 500:   return 4;
    case 1000:  return 5;
    case 2000:  return 6;
    case 10000: return 7;
    default:    return 5; // default ms1000
    }
}

static int normalizeCrntiForRrc(int crnti)
{
    int normalized = crnti & 0xFFFF;
    if (normalized == 0)
        normalized = 1;
    return normalized;
}

static bool isChoOnlyEventType(const std::string &eventType)
{
    return eventType == "D1";
}

static bool isRrcMeasEventType(const std::string &eventType)
{
    return eventType == "A2" || eventType == "A3" || eventType == "A5";
}

static bool isSupportedChoEventType(const std::string &eventType)
{
    return eventType == "A2" || eventType == "A3" || eventType == "A5" || eventType == "D1";
}

static std::vector<const GnbHandoverConfig::GnbHandoverEventConfig *> selectRrcMeasEvents(
    const GnbHandoverConfig &config, Logger *logger)
{
    std::vector<const GnbHandoverConfig::GnbHandoverEventConfig *> selected{};

    for (const auto &event : config.events)
    {
        if (isRrcMeasEventType(event.eventType))
        {
            selected.push_back(&event);
            continue;
        }

        if (isChoOnlyEventType(event.eventType))
        {
            logger->debug("handover.events: ignoring CHO-only event %s in classic MeasConfig",
                          event.eventType.c_str());
            continue;
        }

        logger->warn("handover.events: unsupported event type %s, skipping", event.eventType.c_str());
    }

    return selected;
}

static std::vector<const GnbHandoverConfig::GnbChoEventConfig *> selectChoEventGroups(
    const GnbHandoverConfig &config, Logger *logger)
{
    std::vector<const GnbHandoverConfig::GnbChoEventConfig *> selected{};

    if (!config.choEnabled)
        return selected;

    for (const auto &group : config.choEvents)
    {
        if (group.events.empty())
            continue;

        bool valid = true;
        for (const auto &event : group.events)
        {
            if (!isSupportedChoEventType(event.eventType))
            {
                logger->warn("handover.choEvents: unsupported event type %s, skipping group",
                             event.eventType.c_str());
                valid = false;
                break;
            }
        }

        if (valid)
            selected.push_back(&group);
    }

    return selected;
}

static const GnbHandoverConfig::GnbHandoverEventConfig *findEventConfigByType(
    const GnbHandoverConfig &config, const std::string &eventType)
{
    for (const auto &event : config.events)
    {
        if (event.eventType == eventType)
            return &event;
    }
    return nullptr;
}

static int getServingPciFromNci(int64_t nci)
{
    return static_cast<int>(nci & 0x3FF);
}

using ScoredNeighbor = std::pair<const GnbNeighborConfig *, int>;

/**
 * @brief creates a vector of candidate neighbor cells to use for handover.  Criteria
 * can be defined here to generate a score for each candidate (lower is better)
 * 
 * @param config 
 * @param servingPci 
 * @return std::vector<ScoredNeighbor> 
 */
static std::vector<ScoredNeighbor> prioritizeNeighbors(const GnbConfig &config, int servingPci)
{
    std::vector<ScoredNeighbor> prioritized{};

    // Default logic, just prioritize the first non-serving neighbor in the list (if any).  
    // This can be enhanced with more complex criteria.
    const GnbNeighborConfig *firstNonServing = nullptr;
    for (const auto &neighbor : config.neighborList)
    {
        if (neighbor.getPci() != servingPci)
        {
            firstNonServing = &neighbor;
            break;
        }
    }

    if (firstNonServing)
    {
        prioritized.emplace_back(firstNonServing, 1);
        return prioritized;
    }

    if (!config.neighborList.empty())
        prioritized.emplace_back(&config.neighborList.front(), 1);

    // sort the vector by the score value
    std::sort(prioritized.begin(), prioritized.end(),              [](const ScoredNeighbor &a, const ScoredNeighbor &b) {
                  return a.second < b.second; // sort in ascending order of score
              });

    return prioritized;
}

static std::vector<long> resolveChoMeasIdsForGroup(const RrcUeContext &ue,
                                                   const GnbHandoverConfig::GnbChoEventConfig &group)
{
    std::vector<long> measIds{};
    std::set<long> seen{};

    for (const auto &event : group.events)
    {
        bool found = false;
        for (const auto &identity : ue.sentMeasIdentities)
        {
            if (identity.eventType != event.eventType)
                continue;

            if (seen.insert(identity.measId).second)
                measIds.push_back(identity.measId);

            found = true;
            break;
        }

        if (!found)
            return {};
    }

    return measIds;
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
            m_ueCtx.erase(resolvedUeId);

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
                servingRsrp = rsrpRangeToDbm(*ssbCell->rsrp);
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
                        rsrp = rsrpRangeToDbm(*ssbCell->rsrp);

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

/* ================================================================== */
/*  Send MeasConfig to UE (A2/A3/A5, configurable, multi-measId)      */
/* ================================================================== */

void GnbRrcTask::sendMeasConfig(int ueId, bool forceResend)
{
    auto *ue = tryFindUeByUeId(ueId);
    if (!ue)
        return;

    if (!forceResend && !ue->sentMeasIdentities.empty())
        return; // already configured

    if (forceResend && !ue->sentMeasIdentities.empty())
    {
        m_logger->info("UE[%d] Forcing MeasConfig resend after handover", ue->ueId);
        ue->sentMeasIdentities.clear();
    }

    auto selectedEvents = selectRrcMeasEvents(m_config->handover, m_logger.get());
    if (selectedEvents.empty())
    {
        m_logger->warn("UE[%d] No non-CHO handover events configured; skipping MeasConfig", ue->ueId);
        return;
    }

    std::string eventList{};
    for (size_t i = 0; i < selectedEvents.size(); i++)
    {
        if (i != 0)
            eventList += ",";
        eventList += selectedEvents[i]->eventType;
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

    // Build MeasConfig
    ies->measConfig = asn::New<ASN_RRC_MeasConfig>();
    auto *mc = ies->measConfig;

    // MeasObject: measObjectId=1, NR measurement object
    mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
    {
        auto *measObj = asn::New<ASN_RRC_MeasObjectToAddMod>();
        measObj->measObjectId = 1;
        measObj->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
        measObj->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
        measObj->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
        *measObj->measObject.choice.measObjectNR->ssbFrequency = 632628; // typical n78 SSB freq
        measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
        *measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing =
            ASN_RRC_SubcarrierSpacing_kHz30;
        // smtc1 (SSB MTC periodicity/offset/duration) - required for NR measObject
        measObj->measObject.choice.measObjectNR->smtc1 =
            asn::New<ASN_RRC_SSB_MTC>();
        measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
            ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
        measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
        measObj->measObject.choice.measObjectNR->smtc1->duration =
            ASN_RRC_SSB_MTC__duration_sf1;
        // quantityConfigIndex is mandatory INTEGER (1..maxNrofQuantityConfig); must be >= 1
        measObj->measObject.choice.measObjectNR->quantityConfigIndex = 1;
        asn::SequenceAdd(*mc->measObjectToAddModList, measObj);
    }

    // ReportConfig list: one ReportConfig per configured handover event type.
    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();
    ue->sentMeasIdentities.clear();

    for (size_t i = 0; i < selectedEvents.size(); i++)
    {
        const auto &event = *selectedEvents[i];
        const auto &eventType = event.eventType;
        const long reportConfigId = static_cast<long>(i + 1);
        const long measId = static_cast<long>(i + 1);

        auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
        rcMod->reportConfigId = reportConfigId;
        rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
        rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

        auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;
        rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
        rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();

        auto *et = rcNR->reportType.choice.eventTriggered;
        if (eventType == "A2")
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA2;
            asn::MakeNew(et->eventId.choice.eventA2);

            auto *a2 = et->eventId.choice.eventA2;
            a2->a2_Threshold.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a2->a2_Threshold.choice.rsrp = rsrpDbmToRange(event.a2ThresholdDbm);
            a2->hysteresis = dbToHalfDbSteps(event.hysteresisDb);
            a2->timeToTrigger = tttMsToEnum(event.tttMs);
            a2->reportOnLeave = false;

            // Ask UE to include neighbor measurements in A2 reports where possible.
            et->reportAddNeighMeas = asn::New<long>();
            *et->reportAddNeighMeas = ASN_RRC_EventTriggerConfig__reportAddNeighMeas_setup;

            m_logger->debug("UE[%d] MeasConfig measId=%ld event=A2 threshold=%ddBm hysteresis=%ddB ttt=%dms",
                            ue->ueId, measId, event.a2ThresholdDbm, event.hysteresisDb, event.tttMs);
        }
        else if (eventType == "A5")
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA5;
            asn::MakeNew(et->eventId.choice.eventA5);

            auto *a5 = et->eventId.choice.eventA5;
            a5->a5_Threshold1.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a5->a5_Threshold1.choice.rsrp = rsrpDbmToRange(event.a5Threshold1Dbm);
            a5->a5_Threshold2.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a5->a5_Threshold2.choice.rsrp = rsrpDbmToRange(event.a5Threshold2Dbm);
            a5->hysteresis = dbToHalfDbSteps(event.hysteresisDb);
            a5->timeToTrigger = tttMsToEnum(event.tttMs);
            a5->reportOnLeave = false;
            a5->useWhiteCellList = false;

            m_logger->debug("UE[%d] MeasConfig measId=%ld event=A5 threshold1=%ddBm threshold2=%ddBm "
                            "hysteresis=%ddB ttt=%dms",
                            ue->ueId, measId, event.a5Threshold1Dbm,
                            event.a5Threshold2Dbm, event.hysteresisDb, event.tttMs);
        }
        else
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
            asn::MakeNew(et->eventId.choice.eventA3);

            auto *a3 = et->eventId.choice.eventA3;
            a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
            a3->a3_Offset.choice.rsrp = event.a3OffsetDb * 2;
            a3->hysteresis = dbToHalfDbSteps(event.hysteresisDb);
            a3->timeToTrigger = tttMsToEnum(event.tttMs);
            a3->reportOnLeave = false;
            a3->useWhiteCellList = false;

            m_logger->debug("UE[%d] MeasConfig measId=%ld event=A3 offset=%ddB hysteresis=%ddB ttt=%dms",
                            ue->ueId, measId, event.a3OffsetDb, event.hysteresisDb, event.tttMs);
        }

        et->rsType = ASN_RRC_NR_RS_Type_ssb;
        et->reportInterval = ASN_RRC_ReportInterval_ms480;
        et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
        et->reportQuantityCell.rsrp = true;
        et->reportQuantityCell.rsrq = false;
        et->reportQuantityCell.sinr = false;
        et->maxReportCells = 4;

        asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);

        auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
        measIdMod->measId = measId;
        measIdMod->measObjectId = 1;
        measIdMod->reportConfigId = reportConfigId;
        asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);

        ue->sentMeasIdentities.push_back({
            measId,
            measIdMod->measObjectId,
            reportConfigId,
            eventType,
        });
    }

    sendRrcMessage(ue->ueId, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

    m_logger->info("UE[%d] MeasConfig (%zu measId entries) sent, txId=%ld",
                   ue->ueId, ue->sentMeasIdentities.size(), txId);

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

    const auto *eventConfig = findEventConfigByType(m_config->handover, eventType);
    if (!eventConfig)
    {
        m_logger->warn("UE[%d] HandoverEval: no config found for event=%s", ue->ueId, eventType.c_str());
        return;
    }

    if (isChoOnlyEventType(eventType))
        return;

    bool shouldHandover = false;
    const int hysteresisDb = eventConfig->hysteresisDb;

    if (eventType == "A2")
    {
        // A2: serving RSRP < threshold - hysteresis
        shouldHandover = servingRsrp < (eventConfig->a2ThresholdDbm - hysteresisDb);
        m_logger->debug("UE[%d] HandoverEval: event=A2 measId serving=%ddBm threshold=%ddBm hysteresis=%ddB "
                        "condition=(%d < %d) result=%s",
                        ue->ueId, servingRsrp, eventConfig->a2ThresholdDbm, hysteresisDb,
                        servingRsrp, eventConfig->a2ThresholdDbm - hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (eventType == "A3")
    {
        // A3: neighbor RSRP > serving RSRP + offset + hysteresis
        shouldHandover = bestNeighPci >= 0 &&
                         bestNeighRsrp > (servingRsrp + eventConfig->a3OffsetDb + hysteresisDb);
        m_logger->debug("UE[%d] HandoverEval: event=A3 serving=%ddBm bestNeighPci=%d bestNeigh=%ddBm "
                        "offset=%ddB hysteresis=%ddB condition=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighPci, bestNeighRsrp,
                        eventConfig->a3OffsetDb, hysteresisDb,
                        bestNeighRsrp, servingRsrp + eventConfig->a3OffsetDb + hysteresisDb,
                        shouldHandover ? "true" : "false");
    }
    else if (eventType == "A5")
    {
        // A5: serving RSRP < threshold1 - hysteresis AND neighbor RSRP > threshold2 + hysteresis
        shouldHandover = bestNeighPci >= 0 &&
                         servingRsrp < (eventConfig->a5Threshold1Dbm - hysteresisDb) &&
                         bestNeighRsrp > (eventConfig->a5Threshold2Dbm + hysteresisDb);
        m_logger->debug("UE[%d] HandoverEval: event=A5 serving=%ddBm bestNeighPci=%d bestNeigh=%ddBm "
                        "thr1=%ddBm thr2=%ddBm hysteresis=%ddB cond1=(%d < %d) cond2=(%d > %d) result=%s",
                        ue->ueId, servingRsrp, bestNeighPci, bestNeighRsrp,
                        eventConfig->a5Threshold1Dbm, eventConfig->a5Threshold2Dbm,
                        hysteresisDb,
                        servingRsrp, eventConfig->a5Threshold1Dbm - hysteresisDb,
                        bestNeighRsrp, eventConfig->a5Threshold2Dbm + hysteresisDb,
                        shouldHandover ? "true" : "false");
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

    // cho event groups from the config
    auto choGroups = selectChoEventGroups(m_config->handover, m_logger.get());
    if (choGroups.empty())
    {
        m_logger->warn("UE[%d] CHO prepare skipped (%s): no CHO event groups configured", ueId, eventType.c_str()); 
        return;
    }

    if (ue->sentMeasIdentities.empty())
    {
        m_logger->warn("UE[%d] CHO prepare skipped (%s): no active MeasConfig identities",
                       ueId,
                       eventType.c_str());
        return;
    }

    // Step 1 - identify neighbor cells that are possible handover targets.
    // For NTN operation, we want targets that are prioritized by a matching criteria, such as
    // duration of time in serving area when we expect a handover will happen.
    //  For now, we just select a neighbor that is different from the serving cell.

    const int servingPci = getServingPciFromNci(m_config->nci);
    // prioritizedNeighbors is sorted vector of (neighborCell, score) pairs,
    //   where score is an integer representing the priority of that neighbor as a handover target (lower is better).
    auto prioritizedNeighbors = prioritizeNeighbors(*m_config, servingPci);
    if (prioritizedNeighbors.empty())
    {
        m_logger->warn("UE[%d] CHO prepare skipped (%s): neighbor list is empty", ueId, eventType.c_str());
        return;
    }

    // Step 2 - map the cho event groups to stored MeasIds

    const GnbHandoverConfig::GnbChoEventConfig *selectedGroup = nullptr;
    std::vector<long> selectedMeasIds{};

    for (const auto *group : choGroups)
    {
        auto measIds = resolveChoMeasIdsForGroup(*ue, *group);
        if (!measIds.empty())
        {
            selectedGroup = group;
            selectedMeasIds = std::move(measIds);
            break;
        }
    }

    if (!selectedGroup)
    {
        m_logger->warn("UE[%d] CHO prepare skipped (%s): no CHO group maps to configured MeasId set",
                       ueId,
                       eventType.c_str());
        return;
    }

    // go for CHO message preparation - set the pending flag
    
    ue->choPreparationPending = true;
    
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
        m_logger->warn("UE[%d] CHO prepare skipped (%s): prioritized neighbors have no valid score", ueId,
                       eventType.c_str());
        return;
    }

    ue->choPreparationMeasIds = std::move(selectedMeasIds);
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

    m_logger->info("UE[%d] CHO prepare started (%s): measIds=%zu candidates=%zu targetPCI_1=%d  priority_1=%d ",
                   ue->ueId,
                   eventType.c_str(),
                   ue->choPreparationMeasIds.size(),
                   ue->choPreparationCandidatePcis.size(),
                   firstCandidatePci,
                   firstCandidatePriority);
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
        // sanity check - cho prep must be pending
        if (!ue->choPreparationPending)
        {
            m_logger->warn("UE[%d] CHO prepare response arrived with no pending CHO state; ignoring", ueId);
            return;
        }

        if (ue->choPreparationCandidatePcis.empty())
        {
            ue->choPreparationPending = false;
            m_logger->warn("UE[%d] CHO prepare response arrived with no pending CHO candidates; ignoring", ueId);
            return;
        }

        // extract the nested RRCReconfiguration container from the NGAP message
        
        OctetString nestedRrcReconfig{};
        if (!extractNestedRrcReconfiguration(rrcContainer, nestedRrcReconfig))
        {
            m_logger->err("UE[%d] Failed to extract nested RRCReconfiguration for CHO candidate", ueId);

            if (ue->choPreparationCandidatePcis.empty())
            {
                ue->choPreparationPending = false;
                ue->choPreparationMeasIds.clear();
                ue->choPreparationCandidateScores.clear();
            }
            return;
        }

        // determine the target gnb PCI from the nested RRCReconfiguration

        int candidatePci = -1;
        if (!extractTargetPciFromNestedRrcReconfiguration(nestedRrcReconfig, candidatePci))
        {
            m_logger->err("UE[%d] Failed to extract target PCI from nested RRCReconfiguration", ueId);
            return;
        }

        // locate the candidate PCI in the pending CHO list

        auto itPendingPci = std::find(ue->choPreparationCandidatePcis.begin(),
                                      ue->choPreparationCandidatePcis.end(),
                                      candidatePci);
        if (itPendingPci == ue->choPreparationCandidatePcis.end())
        {
            m_logger->warn("UE[%d] CHO prepare response targetPCI=%d not found in pending candidate list; ignoring",
                           ueId,
                           candidatePci);
            return;
        }

        // remove it from the pending list
        ue->choPreparationCandidatePcis.erase(itPendingPci);

        // get its priority from the priority map
        
        int candidatePriority = 1;
        auto itScore = ue->choPreparationCandidateScores.find(candidatePci);
        if (itScore != ue->choPreparationCandidateScores.end())
            candidatePriority = itScore->second;


        // Create RRCReconfiguration message with the nested RRCReconfiguration from the target gNB, 
        //  and include the CHO conditional reconfiguration IEs with the candidate PCI and the MeasIds that 
        //  should trigger execution of this candidate's CHO config.
        
        auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
        pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
        pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
        pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

        auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
            asn::New<ASN_RRC_RRCReconfiguration>();
        reconfig->rrc_TransactionIdentifier = getNextTid(ueId);
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

        int condReconfigId = candidatePriority;
        if (condReconfigId < 1)
            condReconfigId = 1;
        if (condReconfigId > 8)
            condReconfigId = 8;

        addMod->condReconfigId = condReconfigId;
        addMod->condExecutionCond =
            asn::New<ASN_RRC_CondReconfigToAddMod::ASN_RRC_CondReconfigToAddMod__condExecutionCond>();
        for (long measId : ue->choPreparationMeasIds)
        {
            auto *entry = asn::New<ASN_RRC_MeasId_t>();
            *entry = measId;
            asn::SequenceAdd(*addMod->condExecutionCond, entry);
        }

        addMod->condRRCReconfig = asn::New<OCTET_STRING_t>();
        asn::SetOctetString(*addMod->condRRCReconfig, nestedRrcReconfig);
        asn::SequenceAdd(*v1610->conditionalReconfiguration->condReconfigToAddModList, addMod);

        sendRrcMessage(ueId, pdu);
        asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);

        m_logger->info("UE[%d] CHO RRCReconfiguration sent with 1 candidate targetPCI=%d condReconfigId=%d "
                       "measIds=%zu pendingCandidates=%zu",
                       ueId,
                       candidatePci,
                       condReconfigId,
                       ue->choPreparationMeasIds.size(),
                       ue->choPreparationCandidatePcis.size());

        ue->choPreparationCandidateScores.erase(candidatePci);
        if (ue->choPreparationCandidatePcis.empty())
        {
            ue->choPreparationPending = false;
            ue->choPreparationCandidateScores.clear();
            ue->choPreparationMeasIds.clear();
        }

        return;
    }  // end of CHO preparation response handling

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

void GnbRrcTask::handleNgapHandoverFailure(int ueId)
{
    bool cleared = false;

    auto itPending = m_handoversPending.find(ueId);
    if (itPending != m_handoversPending.end() && itPending->second != nullptr)
    {
        if (itPending->second->ctx != nullptr)
            delete itPending->second->ctx;

        delete itPending->second;
        m_handoversPending.erase(itPending);
        cleared = true;
    }

    auto *ue = findCtxByUeId(ueId);
    if (ue)
    {
        ue->handoverDecisionPending = false;
        ue->choPreparationPending = false;
        ue->choPreparationCandidatePcis.clear();
        ue->choPreparationCandidateScores.clear();
        ue->choPreparationMeasIds.clear();
        cleared = true;
    }

    if (cleared)
        m_logger->info("UE[%d] Cleared RRC pending handover state after NGAP handover failure", ueId);
    else
        m_logger->warn("UE[%d] NGAP handover failure received with no RRC pending handover state", ueId);
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
    identities.reserve(ctx->sentMeasIdentities.size());
    for (const auto &item : ctx->sentMeasIdentities)
    {
        identities.push_back({item.measId, item.measObjectId, item.reportConfigId, item.eventType});
    }
    return identities;
}

OctetString GnbRrcTask::getHandoverMeasConfigRrcReconfiguration(int ueId) const
{
    if (ueId <= 0)
        return OctetString{};

    auto it = m_ueCtx.find(ueId);
    const RrcUeContext *ctx = it != m_ueCtx.end() ? it->second : nullptr;

    if (!ctx || ctx->sentMeasIdentities.empty())
        return OctetString{};

    auto *pdu = asn::New<ASN_RRC_DL_DCCH_Message>();
    pdu->message.present = ASN_RRC_DL_DCCH_MessageType_PR_c1;
    pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
    pdu->message.choice.c1->present = ASN_RRC_DL_DCCH_MessageType__c1_PR_rrcReconfiguration;

    auto &reconfig = pdu->message.choice.c1->choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration>();
    reconfig->rrc_TransactionIdentifier = 0;
    reconfig->criticalExtensions.present = ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration;

    auto &ies = reconfig->criticalExtensions.choice.rrcReconfiguration =
        asn::New<ASN_RRC_RRCReconfiguration_IEs>();

    // Mark MeasConfig as full replacement to avoid stale measurement state
    // when this configuration is transferred across handover contexts.
    ies->nonCriticalExtension = asn::New<ASN_RRC_RRCReconfiguration_v1530_IEs>();
    ies->nonCriticalExtension->fullConfig = asn::New<long>();
    *ies->nonCriticalExtension->fullConfig =
        ASN_RRC_RRCReconfiguration_v1530_IEs__fullConfig_true;

    ies->measConfig = asn::New<ASN_RRC_MeasConfig>();
    auto *mc = ies->measConfig;

    mc->measObjectToAddModList = asn::New<ASN_RRC_MeasObjectToAddModList>();
    auto *measObj = asn::New<ASN_RRC_MeasObjectToAddMod>();
    measObj->measObjectId = 1;
    measObj->measObject.present = ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR;
    measObj->measObject.choice.measObjectNR = asn::New<ASN_RRC_MeasObjectNR>();
    measObj->measObject.choice.measObjectNR->ssbFrequency = asn::New<long>();
    *measObj->measObject.choice.measObjectNR->ssbFrequency = 632628;
    measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = asn::New<long>();
    *measObj->measObject.choice.measObjectNR->ssbSubcarrierSpacing = ASN_RRC_SubcarrierSpacing_kHz30;
    measObj->measObject.choice.measObjectNR->smtc1 = asn::New<ASN_RRC_SSB_MTC>();
    measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.present =
        ASN_RRC_SSB_MTC__periodicityAndOffset_PR_sf20;
    measObj->measObject.choice.measObjectNR->smtc1->periodicityAndOffset.choice.sf20 = 0;
    measObj->measObject.choice.measObjectNR->smtc1->duration = ASN_RRC_SSB_MTC__duration_sf1;
    measObj->measObject.choice.measObjectNR->quantityConfigIndex = 1;
    asn::SequenceAdd(*mc->measObjectToAddModList, measObj);

    mc->reportConfigToAddModList = asn::New<ASN_RRC_ReportConfigToAddModList>();
    mc->measIdToAddModList = asn::New<ASN_RRC_MeasIdToAddModList>();

    auto selectedEvents = selectRrcMeasEvents(m_config->handover, m_logger.get());

    for (const auto &item : ctx->sentMeasIdentities)
    {
        const GnbHandoverConfig::GnbHandoverEventConfig *eventConfig = nullptr;
        if (item.reportConfigId > 0)
        {
            size_t eventIndex = static_cast<size_t>(item.reportConfigId - 1);
            if (eventIndex < selectedEvents.size())
                eventConfig = selectedEvents[eventIndex];
        }
        if (!eventConfig)
            eventConfig = findEventConfigByType(m_config->handover, item.eventType);
        if (!eventConfig || !isRrcMeasEventType(eventConfig->eventType))
            continue;

        auto *rcMod = asn::New<ASN_RRC_ReportConfigToAddMod>();
        rcMod->reportConfigId = item.reportConfigId;
        rcMod->reportConfig.present = ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR;
        rcMod->reportConfig.choice.reportConfigNR = asn::New<ASN_RRC_ReportConfigNR>();

        auto *rcNR = rcMod->reportConfig.choice.reportConfigNR;
        rcNR->reportType.present = ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered;
        rcNR->reportType.choice.eventTriggered = asn::New<ASN_RRC_EventTriggerConfig>();
        auto *et = rcNR->reportType.choice.eventTriggered;

        if (item.eventType == "A2")
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA2;
            asn::MakeNew(et->eventId.choice.eventA2);
            auto *a2 = et->eventId.choice.eventA2;
            a2->a2_Threshold.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a2->a2_Threshold.choice.rsrp = rsrpDbmToRange(eventConfig->a2ThresholdDbm);
            a2->hysteresis = dbToHalfDbSteps(eventConfig->hysteresisDb);
            a2->timeToTrigger = tttMsToEnum(eventConfig->tttMs);
            a2->reportOnLeave = false;
            et->reportAddNeighMeas = asn::New<long>();
            *et->reportAddNeighMeas = ASN_RRC_EventTriggerConfig__reportAddNeighMeas_setup;
        }
        else if (item.eventType == "A5")
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA5;
            asn::MakeNew(et->eventId.choice.eventA5);
            auto *a5 = et->eventId.choice.eventA5;
            a5->a5_Threshold1.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a5->a5_Threshold1.choice.rsrp = rsrpDbmToRange(eventConfig->a5Threshold1Dbm);
            a5->a5_Threshold2.present = ASN_RRC_MeasTriggerQuantity_PR_rsrp;
            a5->a5_Threshold2.choice.rsrp = rsrpDbmToRange(eventConfig->a5Threshold2Dbm);
            a5->hysteresis = dbToHalfDbSteps(eventConfig->hysteresisDb);
            a5->timeToTrigger = tttMsToEnum(eventConfig->tttMs);
            a5->reportOnLeave = false;
            a5->useWhiteCellList = false;
        }
        else
        {
            et->eventId.present = ASN_RRC_EventTriggerConfig__eventId_PR_eventA3;
            asn::MakeNew(et->eventId.choice.eventA3);
            auto *a3 = et->eventId.choice.eventA3;
            a3->a3_Offset.present = ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp;
            a3->a3_Offset.choice.rsrp = eventConfig->a3OffsetDb * 2;
            a3->hysteresis = dbToHalfDbSteps(eventConfig->hysteresisDb);
            a3->timeToTrigger = tttMsToEnum(eventConfig->tttMs);
            a3->reportOnLeave = false;
            a3->useWhiteCellList = false;
        }

        et->rsType = ASN_RRC_NR_RS_Type_ssb;
        et->reportInterval = ASN_RRC_ReportInterval_ms480;
        et->reportAmount = ASN_RRC_EventTriggerConfig__reportAmount_r1;
        et->reportQuantityCell.rsrp = true;
        et->reportQuantityCell.rsrq = false;
        et->reportQuantityCell.sinr = false;
        et->maxReportCells = 4;

        asn::SequenceAdd(*mc->reportConfigToAddModList, rcMod);

        auto *measIdMod = asn::New<ASN_RRC_MeasIdToAddMod>();
        measIdMod->measId = item.measId;
        measIdMod->measObjectId = item.measObjectId;
        measIdMod->reportConfigId = item.reportConfigId;
        asn::SequenceAdd(*mc->measIdToAddModList, measIdMod);
    }

    OctetString encoded = rrc::encode::EncodeS(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
    asn::Free(asn_DEF_ASN_RRC_DL_DCCH_Message, pdu);
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
        ctx->sentMeasIdentities.push_back({item.measId, item.measObjectId, item.reportConfigId,
                                           item.eventType});
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