//
// RRC Reconfiguration reception and MeasConfig parsing.
//

#include "task.hpp"

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <lib/rrc/common/asn_converters.hpp>
#include <ue/nas/task.hpp>

#include <asn/rrc/ASN_RRC_RRCReconfiguration.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1530-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1540-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1560-IEs.h>
#include <asn/rrc/ASN_RRC_RRCReconfiguration-v1610-IEs.h>
#include <asn/rrc/ASN_RRC_ConditionalReconfiguration.h>
#include <asn/rrc/ASN_RRC_CondReconfigToAddMod.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete-IEs.h>
#include <asn/rrc/ASN_RRC_CellGroupConfig.h>
#include <asn/rrc/ASN_RRC_SpCellConfig.h>
#include <asn/rrc/ASN_RRC_ReconfigurationWithSync.h>
#include <asn/rrc/ASN_RRC_ServingCellConfigCommon.h>
#include <asn/rrc/ASN_RRC_MeasConfig.h>
#include <asn/rrc/ASN_RRC_MeasObjectToRemoveList.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddModList.h>
#include <asn/rrc/ASN_RRC_MeasIdToRemoveList.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddModList.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasObjectNR.h>
#include <asn/rrc/ASN_RRC_ReportConfigToRemoveList.h>
#include <asn/rrc/ASN_RRC_ReportConfigToAddModList.h>
#include <asn/rrc/ASN_RRC_ReportConfigToAddMod.h>
#include <asn/rrc/ASN_RRC_ReportConfigNR.h>
#include <asn/rrc/ASN_RRC_EventTriggerConfig.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-MessageType.h>
#include <asn/rrc/ASN_RRC_DedicatedNAS-Message.h>

namespace nr::ue
{

using nr::rrc::common::E_TTT_ms;
using nr::rrc::common::HandoverEventType;
using nr::rrc::common::ReportConfigEvent;
using nr::rrc::common::hysteresisFromASNValue;
using nr::rrc::common::mtqFromASNValue;
using nr::rrc::common::mtqOffsetFromASNValue;
using nr::rrc::common::referenceLocationFromAsnValue;
using nr::rrc::common::t304EnumToMs;
using nr::rrc::common::t1ThresholdFromASNValue;
using nr::rrc::common::durationFromASNValue;
using nr::rrc::common::distanceThresholdFromASNValue;
using nr::rrc::common::hysteresisLocationFromASNValue;



namespace
{

long measTriggerQuantityValue(const ASN_RRC_MeasTriggerQuantity_t &q)
{
    switch (q.present)
    {
    case ASN_RRC_MeasTriggerQuantity_PR_rsrp:
        return q.choice.rsrp;
    case ASN_RRC_MeasTriggerQuantity_PR_rsrq:
        return q.choice.rsrq;
    case ASN_RRC_MeasTriggerQuantity_PR_sinr:
        return q.choice.sinr;
    default:
        return 0;
    }
}

long measTriggerQuantityOffsetValue(const ASN_RRC_MeasTriggerQuantityOffset_t &q)
{
    switch (q.present)
    {
    case ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp:
        return q.choice.rsrp;
    case ASN_RRC_MeasTriggerQuantityOffset_PR_rsrq:
        return q.choice.rsrq;
    case ASN_RRC_MeasTriggerQuantityOffset_PR_sinr:
        return q.choice.sinr;
    default:
        return 0;
    }
}

} // namespace


/* ================================================================== */
/*  Parse ASN MeasConfig → UeMeasConfig                               */
/* ================================================================== */

static UeMeasConfig parseMeasConfig(const ASN_RRC_MeasConfig &mc)
{
    UeMeasConfig cfg{};

    // --- Remove lists (delta signaling) ---
    if (mc.measObjectToRemoveList)
    {
        auto &list = mc.measObjectToRemoveList->list;
        cfg.measObjectsToRemove.reserve(list.count);
        for (int i = 0; i < list.count; i++)
        {
            if (list.array[i])
                cfg.measObjectsToRemove.push_back(static_cast<int>(*list.array[i]));
        }
    }

    if (mc.reportConfigToRemoveList)
    {
        auto &list = mc.reportConfigToRemoveList->list;
        cfg.reportConfigsToRemove.reserve(list.count);
        for (int i = 0; i < list.count; i++)
        {
            if (list.array[i])
                cfg.reportConfigsToRemove.push_back(static_cast<int>(*list.array[i]));
        }
    }

    if (mc.measIdToRemoveList)
    {
        auto &list = mc.measIdToRemoveList->list;
        cfg.measIdsToRemove.reserve(list.count);
        for (int i = 0; i < list.count; i++)
        {
            if (list.array[i])
                cfg.measIdsToRemove.push_back(static_cast<int>(*list.array[i]));
        }
    }

    // --- MeasObjects ---
    if (mc.measObjectToAddModList)
    {
        auto &list = mc.measObjectToAddModList->list;
        for (int i = 0; i < list.count; i++)
        {
            auto *item = list.array[i];
            nr::rrc::common::MeasObject obj{};
            obj.measObjectId = static_cast<int>(item->measObjectId);
            if (item->measObject.present == ASN_RRC_MeasObjectToAddMod__measObject_PR_measObjectNR)
            {
                auto *nr = item->measObject.choice.measObjectNR;
                if (nr)
                    obj.ssbFrequency = static_cast<int>(nr->ssbFrequency ? *nr->ssbFrequency : 0);
            }
            cfg.measObjects[obj.measObjectId] = obj;
        }
    }

    // --- ReportConfigs ---
    if (mc.reportConfigToAddModList)
    {
        auto &list = mc.reportConfigToAddModList->list;
        for (int i = 0; i < list.count; i++)
        {
            auto *item = list.array[i];
            if (item->reportConfig.present != ASN_RRC_ReportConfigToAddMod__reportConfig_PR_reportConfigNR)
                continue;
            auto *rcNR = item->reportConfig.choice.reportConfigNR;
            if (!rcNR)
                continue;

            // rc is the new UeReportConfig we will populate based on the 
            //  ASN ReportConfig and add to cfg.reportConfigs
            ReportConfigEvent rc{};
            rc.reportConfigId = static_cast<int>(item->reportConfigId);

            // eventTriggered reports
            if (rcNR->reportType.present == ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered)
            {
                auto *eTrig = rcNR->reportType.choice.eventTriggered;
                if (!eTrig)
                    continue;
            
                rc.maxReportCells = static_cast<int>(eTrig->maxReportCells);

                switch (eTrig->eventId.present)
                {
                case ASN_RRC_EventTriggerConfig__eventId_PR_eventA2:
                {
                    auto *a2 = eTrig->eventId.choice.eventA2;
                    if (!a2) break;
                    rc.eventKind = HandoverEventType::A2;
                    rc.a2_thresholdDbm = mtqFromASNValue(measTriggerQuantityValue(a2->a2_Threshold));
                    rc.a2_hysteresisDb = hysteresisFromASNValue(a2->hysteresis);
                    rc.ttt = static_cast<E_TTT_ms>(a2->timeToTrigger);
                    break;
                }
                case ASN_RRC_EventTriggerConfig__eventId_PR_eventA3:
                {
                    auto *a3 = eTrig->eventId.choice.eventA3;
                    if (!a3) break;
                    rc.eventKind = HandoverEventType::A3;
                    rc.a3_offsetDb =
                        mtqOffsetFromASNValue(measTriggerQuantityOffsetValue(a3->a3_Offset));
                    rc.a3_hysteresisDb = hysteresisFromASNValue(a3->hysteresis);
                    rc.ttt = static_cast<E_TTT_ms>(a3->timeToTrigger);
                    break;
                }
                case ASN_RRC_EventTriggerConfig__eventId_PR_eventA5:
                {
                    auto *a5 = eTrig->eventId.choice.eventA5;
                    if (!a5) break;
                    rc.eventKind = HandoverEventType::A5;
                    rc.a5_threshold1Dbm =
                        mtqFromASNValue(measTriggerQuantityValue(a5->a5_Threshold1));
                    rc.a5_threshold2Dbm =
                        mtqFromASNValue(measTriggerQuantityValue(a5->a5_Threshold2));
                    rc.a5_hysteresisDb = hysteresisFromASNValue(a5->hysteresis);
                    rc.ttt = static_cast<E_TTT_ms>(a5->timeToTrigger);
                    break;
                }
                // D1 event
                case ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17:
                {
                    auto *d1 = eTrig->eventId.choice.eventD1_r17;
                    if (!d1)
                        break;

                    rc.eventKind = HandoverEventType::D1;
                    rc.d1_hysteresisLocation = hysteresisLocationFromASNValue(d1->hysteresisLocation_r17);
                    rc.d1_distanceThreshFromReference1 = distanceThresholdFromASNValue(d1->distanceThreshFromReference1_r17);
                    rc.d1_distanceThreshFromReference2 = distanceThresholdFromASNValue(d1->distanceThreshFromReference2_r17);

                    referenceLocationFromAsnValue(d1->referenceLocation1_r17,
                        rc.d1_referenceLocation1);
                    referenceLocationFromAsnValue(d1->referenceLocation2_r17,
                        rc.d1_referenceLocation2);
                    rc.ttt = static_cast<E_TTT_ms>(d1->timeToTrigger);

                    break;
                }
                default:
                    break;
                }
            }
            // conditional triggered reports (e.g. for conditional handover)
            else if (rcNR->reportType.present == ASN_RRC_ReportConfigNR__reportType_PR_condTriggerConfig_r16)
            {
                auto *ctc = rcNR->reportType.choice.condTriggerConfig_r16;
                if (!ctc)
                    continue;

                switch (ctc->condEventId.present)
                {

                case ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventT1_r17:
                {
                    auto *t1 = ctc->condEventId.choice.condEventT1_r17;
                    if (!t1)
                        break;

                    rc.eventKind = HandoverEventType::CondT1;
                    rc.condT1_durationSec = durationFromASNValue(t1->duration_r17);
                    rc.condT1_thresholdSecTS = t1ThresholdFromASNValue(t1->t1_Threshold_r17);
                    rc.ttt = static_cast<E_TTT_ms>(E_TTT_ms::ms0);   // CondT1 doesn't have a TTT, it is satisfied immediately
                    break;
                }
                case ASN_RRC_CondTriggerConfig_r16__condEventId_PR_condEventD1_r17:
                {
                    auto *d1 = ctc->condEventId.choice.condEventD1_r17;
                    if (!d1)
                        break;

                    rc.eventKind = HandoverEventType::CondD1;
                    rc.condD1_hysteresisLocation = hysteresisLocationFromASNValue(d1->hysteresisLocation_r17);
                    rc.condD1_distanceThreshFromReference1 = distanceThresholdFromASNValue(d1->distanceThreshFromReference1_r17);
                    rc.condD1_distanceThreshFromReference2 = distanceThresholdFromASNValue(d1->distanceThreshFromReference2_r17);

                    referenceLocationFromAsnValue(d1->referenceLocation1_r17,
                        rc.condD1_referenceLocation1);
                    referenceLocationFromAsnValue(d1->referenceLocation2_r17,
                        rc.condD1_referenceLocation2);
                    break;
                }
                default:
                    continue; // skip unsupported events
                }
            }
            else
            {
                continue; // skip unsupported report types
            }

            // store the parsed ReportConfig in the cfg to be applied to UE state
            cfg.reportConfigs[rc.reportConfigId] = rc;
        }
    }

    // --- MeasIds ---
    if (mc.measIdToAddModList)
    {
        auto &list = mc.measIdToAddModList->list;
        for (int i = 0; i < list.count; i++)
        {
            auto *item = list.array[i];
            nr::rrc::common::MeasIdentity mid{};
            mid.measId = static_cast<int>(item->measId);
            mid.measObjectId = static_cast<int>(item->measObjectId);
            mid.reportConfigId = static_cast<int>(item->reportConfigId);
            cfg.measIds[mid.measId] = mid;
        }
    }

    return cfg;
}



/**
 * @brief Handles incoming RRCReconfiguration message.  Extracts the MeasConfig and any
 * relevant handover parameters, and applies the MeasConfig to the UE state.  
 * If a handover is being commanded, the extracted parameters will be used to manage the 
 * handover execution in subsequent steps.
 * 
 * @param msg 
 */
void UeRrcTask::receiveRrcReconfiguration(const ASN_RRC_RRCReconfiguration &msg)
{
    if (msg.criticalExtensions.present !=
        ASN_RRC_RRCReconfiguration__criticalExtensions_PR_rrcReconfiguration)
    {
        m_logger->err("RRCReconfiguration: unrecognised criticalExtensions");
        return;
    }

    // save the txId for Ack matching
    int txId = static_cast<int>(msg.rrc_TransactionIdentifier);

    // RRCReconfiguration-IEs are under the criticalExtensions IE
    auto *ies = msg.criticalExtensions.choice.rrcReconfiguration;
    if (!ies)
    {
        m_logger->err("RRCReconfiguration: null IEs");
        return;
    }

    m_logger->info("RRCReconfiguration received (txId=%ld)", msg.rrc_TransactionIdentifier);

    // RRCReconfiguration takes the format:
    // RRCReconfiguration-IEs ::= SEQUENCE {
    //   radioBearerConfig                   RadioBearerConfig    (Not used by simulation)
    //   secondaryCellGroup                  OCTET STRING (CONTAINING CellGroupConfig) (Not used by simulation)
    //   measConfig                          MeasConfig                     (array of measConfigs)
    //   lateNonCriticalExtension            OCTET STRING                   (not used by simulation)
    //   nonCriticalExtension                RRCReconfiguration-v1530-IEs     (list of version-specific added IEs, including CHO)
    // }

    // check for a fullConfig indicator in the nonCriticalExtention->[v1530] IEs. 
    //  If fullConfig is true, then the MeasConfig is a full replacement of the existing config
    bool fullConfig = false;
    if (ies->nonCriticalExtension && ies->nonCriticalExtension->fullConfig)
        fullConfig = true;

    // add/remove MeasConfigs if present
    if (ies->measConfig)
    {
        auto cfg = parseMeasConfig(*ies->measConfig);
        cfg.fullConfig = fullConfig;
        applyMeasConfig(cfg);
    }
    // if no measConfigs provided but fullConfig=true, just clear measurement state
    else if (fullConfig)
    {
        m_logger->info("RRCReconfiguration fullConfig=true without MeasConfig: clearing measurement state");
        resetMeasurements();
    }

    bool isHandover = false;
    int hoPhysCellId = -1;
    int hoNewCRNTI = 0;
    int hoT304Ms = 0;
    bool hoHasRachConfig = false;
    const ASN_RRC_ConditionalReconfiguration *pendingConditionalReconfig = nullptr;

    // Walk the nonCriticalExtension chain: v1530 → v1540 → v1560 → v1610 for CHO
    if (ies->nonCriticalExtension)
    {
        // v1530:
        //   RRCReconfiguration-v1530-IEs ::= SEQUENCE {
        //     masterCellGroup                     OCTET STRING (CONTAINING CellGroupConfig)           OPTIONAL, -- Need M
        //     fullConfig                          ENUMERATED {true}                                   OPTIONAL, -- Cond FullConfig
        //     dedicatedNAS-MessageList            SEQUENCE (SIZE(1..maxDRB)) OF DedicatedNAS-Message  OPTIONAL, -- Cond exec
        //     masterKeyUpdate                     MasterKeyUpdate                                     OPTIONAL, -- Cond MasterKey
        //     sk-Counter                          SK-Counter                                          OPTIONAL, -- Cond SN-Term
        //     nonCriticalExtension                RRCReconfiguration-v1610-IEs                        OPTIONAL
        // }
        auto *v1530 = ies->nonCriticalExtension;

        // Deliver any dedicated NAS messages to the NAS layer
        //  (None of these are used by the simulator)
        // if (v1530->dedicatedNAS_MessageList)
        // {
        //     auto &nasList = v1530->dedicatedNAS_MessageList->list;
        //     for (int i = 0; i < nasList.count; i++)
        //     {
        //         auto *nasPdu = nasList.array[i];
        //         if (nasPdu && nasPdu->buf && nasPdu->size > 0)
        //         {
        //             auto w = std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::NAS_DELIVERY);
        //             w->nasPdu = OctetString::FromArray(nasPdu->buf, static_cast<size_t>(nasPdu->size));
        //             m_base->nasTask->push(std::move(w));
        //         }
        //     }
        // }

        // masterCellGroup -> check for ReconfigurationWithSync to detect handover scenarios and extract relevant parameters 
        if (v1530->masterCellGroup)
        {
            auto *cellGroupConfig = rrc::encode::Decode<ASN_RRC_CellGroupConfig>(
                asn_DEF_ASN_RRC_CellGroupConfig, *v1530->masterCellGroup);

            if (cellGroupConfig)
            {
                // check for the presence of ReconfigurationWithSync in the spCellConfig, 
                //  which indicates a handover is being commanded.
                if (cellGroupConfig->spCellConfig &&
                    cellGroupConfig->spCellConfig->reconfigurationWithSync)
                {
                    // extract the target cell handover parameters
                    //  and set the handover flag
                    auto *rws = cellGroupConfig->spCellConfig->reconfigurationWithSync;

                    hoPhysCellId = (rws->spCellConfigCommon && rws->spCellConfigCommon->physCellId)
                                       ? static_cast<int>(*rws->spCellConfigCommon->physCellId)
                                       : -1;
                    hoNewCRNTI = static_cast<int>(rws->newUE_Identity);
                    hoT304Ms = t304EnumToMs(rws->t304);
                    hoHasRachConfig = (rws->rach_ConfigDedicated != nullptr);
                    isHandover = true;

                    m_logger->info("ReconfigurationWithSync detected: PCI=%d newC-RNTI=%d t304=%dms rachConfig=%s",
                                   hoPhysCellId, hoNewCRNTI, hoT304Ms,
                                   hoHasRachConfig ? "dedicated" : "common");
                }

                asn::Free(asn_DEF_ASN_RRC_CellGroupConfig, cellGroupConfig);
            }
            else
            {
                m_logger->err("Failed to UPER-decode masterCellGroup as CellGroupConfig");
            }
        }

        // --- Walk extension chain: v1530 → v1540 → v1560 → v1610 for CHO ---
        auto *v1540 = v1530->nonCriticalExtension;
        auto *v1560 = v1540 ? v1540->nonCriticalExtension : nullptr;
        auto *v1610 = v1560 ? v1560->nonCriticalExtension : nullptr;

        m_logger->debug(
            "RRCReconfiguration extension chain: v1530=%s v1540=%s v1560=%s v1610=%s conditional=%s",
            "yes",
            v1540 ? "yes" : "no",
            v1560 ? "yes" : "no",
            v1610 ? "yes" : "no",
            (v1610 && v1610->conditionalReconfiguration) ? "yes" : "no");

        // check for a ConditionalReconfiguration IE in the v1610 extension, which indicates 
        //   a CHO is pending and we should parse the CHO IEs 
        if (v1610 && v1610->conditionalReconfiguration)
        {
            pendingConditionalReconfig = v1610->conditionalReconfiguration;
        }
        else if (v1610)
        {
            // if v1610 IEs are present but ConditionalReconfiguration is omitted or not populated by the sender,
            //  just log it for debugging
            m_logger->debug("RRCReconfiguration v1610 present without ConditionalReconfiguration; ignoring CHO update");
        }
    }

    // Compatibility rule: fullConfig means replace-all configuration state.
    // If ConditionalReconfiguration is omitted, CHO state is cleared and stays empty
    // (Rel-15 fallback path with no CHO IE).
    if (fullConfig)
    {
        m_logger->info("RRCReconfiguration fullConfig=true: clearing existing CHO candidates");
        cancelAllChoCandidates();
    }

    // set the conditional handover events (if present)
    if (pendingConditionalReconfig)
        parseConditionalReconfiguration(pendingConditionalReconfig);

    // if this RRC reconfig was to execute a handover, then trigger the handover procedure with the extracted parameters.
    //   Note - we do this last, because the RRC can include new measurement configurations from the new  GNB
    //      that we want to apply before executing the handover
    if (isHandover)
    {
        // handovers require special processing
        performHandover(txId, hoPhysCellId, hoNewCRNTI, hoT304Ms, hoHasRachConfig);
    }

    // Non-handover reconfiguration: just send RRCReconfigurationComplete as ACK
    else
    {
        auto *pdu = asn::New<ASN_RRC_UL_DCCH_Message>();
        pdu->message.present = ASN_RRC_UL_DCCH_MessageType_PR_c1;
        pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
        pdu->message.choice.c1->present =
            ASN_RRC_UL_DCCH_MessageType__c1_PR_rrcReconfigurationComplete;

        auto &rrc = pdu->message.choice.c1->choice.rrcReconfigurationComplete =
            asn::New<ASN_RRC_RRCReconfigurationComplete>();
        rrc->rrc_TransactionIdentifier = txId;
        rrc->criticalExtensions.present =
            ASN_RRC_RRCReconfigurationComplete__criticalExtensions_PR_rrcReconfigurationComplete;
        rrc->criticalExtensions.choice.rrcReconfigurationComplete =
            asn::New<ASN_RRC_RRCReconfigurationComplete_IEs>();

        sendRrcMessage(pdu);
        asn::Free(asn_DEF_ASN_RRC_UL_DCCH_Message, pdu);

        m_logger->info("RRCReconfigurationComplete sent (non-handover reconfiguration)");
    }
}

} // namespace nr::ue
