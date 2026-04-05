//
// RRC Reconfiguration reception and MeasConfig parsing.
//

#include "task.hpp"
#include "measurement.hpp"
#include "position.hpp"

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
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

/* ================================================================== */
/*  ASN helpers for MeasConfig conversion                             */
/* ================================================================== */

// RSRP_Range (0..127) → dBm (-156..-44)
static int rsrpRangeToDbm(long val)
{
    return static_cast<int>(val) - 156;
}

// TimeToTrigger enum → milliseconds
static int timeToTriggerMs(long ttt)
{
    // TS 38.331 enum values
    static const int table[] = {
        0, 40, 64, 80, 100, 128, 160, 256,
        320, 480, 512, 640, 1024, 1280, 2560, 5120};
    if (ttt >= 0 && ttt < 16)
        return table[ttt];
    return 640; // default
}

// Hysteresis (0..30 in 0.5 dB units) → integer dB (round down)
static int hysteresisToDb(long h)
{
    return static_cast<int>(h / 2);
}

// MeasTriggerQuantity → RSRP in dBm (we focus on RSRP; fall back for rsrq/sinr)
static int triggerQuantityToDbm(const ASN_RRC_MeasTriggerQuantity &tq)
{
    if (tq.present == ASN_RRC_MeasTriggerQuantity_PR_rsrp)
        return rsrpRangeToDbm(tq.choice.rsrp);
    return -110; // fallback
}

// MeasTriggerQuantityOffset → dB (0.5 dB units → int)
static int triggerQuantityOffsetToDb(const ASN_RRC_MeasTriggerQuantityOffset &tqo)
{
    if (tqo.present == ASN_RRC_MeasTriggerQuantityOffset_PR_rsrp)
        return static_cast<int>(tqo.choice.rsrp / 2);
    return 0;
}

static double microDegreesToDegrees(long val)
{
    return static_cast<double>(val) / 1000000.0;
}

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
            UeMeasObject obj{};
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
            if (rcNR->reportType.present != ASN_RRC_ReportConfigNR__reportType_PR_eventTriggered)
                continue;
            auto *eTrig = rcNR->reportType.choice.eventTriggered;
            if (!eTrig)
                continue;

            // rc is the new UeReportConfig we will populate based on the 
            //  ASN ReportConfig and add to cfg.reportConfigs
            UeReportConfig rc{};
            rc.reportConfigId = static_cast<int>(item->reportConfigId);
            rc.maxReportCells = static_cast<int>(eTrig->maxReportCells);

            switch (eTrig->eventId.present)
            {
            case ASN_RRC_EventTriggerConfig__eventId_PR_eventA2:
            {
                auto *a2 = eTrig->eventId.choice.eventA2;
                if (!a2) break;
                rc.event = EMeasEvent::A2;
                rc.a2Threshold = triggerQuantityToDbm(a2->a2_Threshold);
                rc.hysteresis = hysteresisToDb(a2->hysteresis);
                rc.timeToTriggerMs = timeToTriggerMs(a2->timeToTrigger);
                break;
            }
            case ASN_RRC_EventTriggerConfig__eventId_PR_eventA3:
            {
                auto *a3 = eTrig->eventId.choice.eventA3;
                if (!a3) break;
                rc.event = EMeasEvent::A3;
                rc.a3Offset = triggerQuantityOffsetToDb(a3->a3_Offset);
                rc.hysteresis = hysteresisToDb(a3->hysteresis);
                rc.timeToTriggerMs = timeToTriggerMs(a3->timeToTrigger);
                break;
            }
            case ASN_RRC_EventTriggerConfig__eventId_PR_eventA5:
            {
                auto *a5 = eTrig->eventId.choice.eventA5;
                if (!a5) break;
                rc.event = EMeasEvent::A5;
                rc.a5Threshold1 = triggerQuantityToDbm(a5->a5_Threshold1);
                rc.a5Threshold2 = triggerQuantityToDbm(a5->a5_Threshold2);
                rc.hysteresis = hysteresisToDb(a5->hysteresis);
                rc.timeToTriggerMs = timeToTriggerMs(a5->timeToTrigger);
                break;
            }
            // D1 event
            case ASN_RRC_EventTriggerConfig__eventId_PR_eventD1_r17:
            {
                auto *d1 = eTrig->eventId.choice.eventD1_r17;
                if (!d1)
                    break;

                rc.event = EMeasEvent::D1;
                rc.hysteresis = hysteresisToDb(d1->hysteresis);
                rc.timeToTriggerMs = timeToTriggerMs(d1->timeToTrigger);
                rc.d1ThresholdM = static_cast<double>(d1->distanceThresh_r17);

                // for fixed references (terrestrial sources)
                if (d1->referenceLocation_r17.present ==
                    ASN_RRC_EventTriggerConfig__eventId__eventD1_r17__referenceLocation_r17_PR_fixedReferenceLocation_r17)
                {
                    auto *fixed = d1->referenceLocation_r17.choice.fixedReferenceLocation_r17;
                    if (!fixed)
                        break;

                    GeoPosition geo{};
                    geo.longitude = microDegreesToDegrees(fixed->longitude_r17);
                    geo.latitude = microDegreesToDegrees(fixed->latitude_r17);
                    geo.altitude = static_cast<double>(fixed->height_r17);
                    EcefPosition ecef = geoToEcef(geo);

                    rc.d1ReferenceType = ED1ReferenceType::Fixed;
                    rc.d1RefX = ecef.x;
                    rc.d1RefY = ecef.y;
                    rc.d1RefZ = ecef.z;
                }
                // for nadir references (SIB19 - satellite ephemeris)
                else if (d1->referenceLocation_r17.present ==
                         ASN_RRC_EventTriggerConfig__eventId__eventD1_r17__referenceLocation_r17_PR_nadirReferenceLocation_r17)
                {
                    rc.d1ReferenceType = ED1ReferenceType::Nadir;
                    rc.d1RefX = 0.0;
                    rc.d1RefY = 0.0;
                    rc.d1RefZ = 0.0;
                }
                // this is an invalid configuration
                else
                {
                    rc.d1ReferenceType = ED1ReferenceType::Unknown;
                    rc.d1RefX = 0.0;
                    rc.d1RefY = 0.0;
                    rc.d1RefZ = 0.0;
                }
                break;
            }
            default:
                continue; // skip unsupported events
            }

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
            UeMeasId mid{};
            mid.measId = static_cast<int>(item->measId);
            mid.measObjectId = static_cast<int>(item->measObjectId);
            mid.reportConfigId = static_cast<int>(item->reportConfigId);
            cfg.measIds[mid.measId] = mid;
        }
    }

    return cfg;
}

/* ================================================================== */
/*  T304 enum → milliseconds (used for ReconfigurationWithSync)       */
/* ================================================================== */

static int t304EnumToMs(long t304)
{
    static const int table[] = {50, 100, 150, 200, 500, 1000, 2000, 10000};
    if (t304 >= 0 && t304 < 8)
        return table[t304];
    return 1000;
}

/* ================================================================== */
/*  Handle incoming RRCReconfiguration                                */
/* ================================================================== */

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

    // RRCReconfiguration-IEs are under teh criticalExtensions IE
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

        m_logger->info(
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
