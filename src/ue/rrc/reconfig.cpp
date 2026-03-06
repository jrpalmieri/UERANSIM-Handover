//
// RRC Reconfiguration reception and MeasConfig parsing.
//

#include "task.hpp"
#include "measurement.hpp"

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
#include <asn/rrc/ASN_RRC_MeasIdToAddModList.h>
#include <asn/rrc/ASN_RRC_MeasIdToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddModList.h>
#include <asn/rrc/ASN_RRC_MeasObjectToAddMod.h>
#include <asn/rrc/ASN_RRC_MeasObjectNR.h>
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

/* ================================================================== */
/*  Parse ASN MeasConfig → UeMeasConfig                               */
/* ================================================================== */

static UeMeasConfig parseMeasConfig(const ASN_RRC_MeasConfig &mc)
{
    UeMeasConfig cfg{};

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
            default:
                continue; // skip unsupported events (A1, A4, A6)
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

    auto *ies = msg.criticalExtensions.choice.rrcReconfiguration;
    if (!ies)
    {
        m_logger->err("RRCReconfiguration: null IEs");
        return;
    }

    m_logger->info("RRCReconfiguration received (txId=%ld)", msg.rrc_TransactionIdentifier);

    // --- Apply MeasConfig if present ---
    if (ies->measConfig)
    {
        auto cfg = parseMeasConfig(*ies->measConfig);
        applyMeasConfig(cfg);
    }

    // --- Check v1530-IEs for masterCellGroup (handover) & dedicated NAS ---
    bool isHandover = false;
    int hoPhysCellId = -1;
    int hoNewCRNTI = 0;
    int hoT304Ms = 0;
    bool hoHasRachConfig = false;
    bool hasConditionalReconfig = false;
    (void)hasConditionalReconfig; // May be used in future for CHO-specific response

    if (ies->nonCriticalExtension)
    {
        auto *v1530 = ies->nonCriticalExtension;

        // Deliver any dedicated NAS messages to the NAS layer
        if (v1530->dedicatedNAS_MessageList)
        {
            auto &nasList = v1530->dedicatedNAS_MessageList->list;
            for (int i = 0; i < nasList.count; i++)
            {
                auto *nasPdu = nasList.array[i];
                if (nasPdu && nasPdu->buf && nasPdu->size > 0)
                {
                    auto w = std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::NAS_DELIVERY);
                    w->nasPdu = OctetString::FromArray(nasPdu->buf, static_cast<size_t>(nasPdu->size));
                    m_base->nasTask->push(std::move(w));
                }
            }
        }

        // Decode masterCellGroup → CellGroupConfig → SpCellConfig → ReconfigurationWithSync
        if (v1530->masterCellGroup)
        {
            auto *cellGroupConfig = rrc::encode::Decode<ASN_RRC_CellGroupConfig>(
                asn_DEF_ASN_RRC_CellGroupConfig, *v1530->masterCellGroup);

            if (cellGroupConfig)
            {
                if (cellGroupConfig->spCellConfig &&
                    cellGroupConfig->spCellConfig->reconfigurationWithSync)
                {
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

        if (v1610 && v1610->conditionalReconfiguration)
        {
            hasConditionalReconfig = true;
            auto *condReconfig = v1610->conditionalReconfiguration;
            parseConditionalReconfiguration(condReconfig);
        }
    }

    // --- Execute handover or normal reconfiguration ---
    if (isHandover)
    {
        performHandover(msg.rrc_TransactionIdentifier, hoPhysCellId, hoNewCRNTI, hoT304Ms, hoHasRachConfig);
    }
    else
    {
        // Normal reconfiguration: send RRCReconfigurationComplete
        auto *pdu = asn::New<ASN_RRC_UL_DCCH_Message>();
        pdu->message.present = ASN_RRC_UL_DCCH_MessageType_PR_c1;
        pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
        pdu->message.choice.c1->present =
            ASN_RRC_UL_DCCH_MessageType__c1_PR_rrcReconfigurationComplete;

        auto &rrc = pdu->message.choice.c1->choice.rrcReconfigurationComplete =
            asn::New<ASN_RRC_RRCReconfigurationComplete>();
        rrc->rrc_TransactionIdentifier = msg.rrc_TransactionIdentifier;
        rrc->criticalExtensions.present =
            ASN_RRC_RRCReconfigurationComplete__criticalExtensions_PR_rrcReconfigurationComplete;
        rrc->criticalExtensions.choice.rrcReconfigurationComplete =
            asn::New<ASN_RRC_RRCReconfigurationComplete_IEs>();

        sendRrcMessage(pdu);
        asn::Free(asn_DEF_ASN_RRC_UL_DCCH_Message, pdu);

        m_logger->info("RRCReconfigurationComplete sent (normal reconfiguration)");
    }
}

} // namespace nr::ue
