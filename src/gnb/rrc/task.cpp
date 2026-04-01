//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <gnb/nts.hpp>
#include <gnb/rls/task.hpp>
#include <lib/rrc/encode.hpp>

#include <asn/rrc/ASN_RRC_DLInformationTransfer-IEs.h>
#include <asn/rrc/ASN_RRC_DLInformationTransfer.h>

static constexpr const int TIMER_ID_SI_BROADCAST = 1;
static constexpr const int TIMER_PERIOD_SI_BROADCAST = 10'000;

namespace nr::gnb
{

GnbRrcTask::GnbRrcTask(TaskBase *base) : m_base{base}, m_ueCtx{}, m_tidCountersByUe{}
{
    m_logger = base->logBase->makeUniqueLogger("rrc");
    m_config = m_base->config;
}

void GnbRrcTask::onStart()
{
    setTimer(TIMER_ID_SI_BROADCAST, TIMER_PERIOD_SI_BROADCAST);
}

void GnbRrcTask::onQuit()
{
    // todo
}

void GnbRrcTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::GNB_RLS_TO_RRC: {
        handleRlsSapMessage(dynamic_cast<NmGnbRlsToRrc &>(*msg));
        break;
    }
    case NtsMessageType::GNB_NGAP_TO_RRC: {
        auto &w = dynamic_cast<NmGnbNgapToRrc &>(*msg);
        switch (w.present)
        {
        case NmGnbNgapToRrc::RADIO_POWER_ON: {
            m_isBarred = false;
            triggerSysInfoBroadcast();
            break;
        }
        case NmGnbNgapToRrc::NAS_DELIVERY: {
            handleDownlinkNasDelivery(w.ueId, w.pdu);
            break;
        }
        case NmGnbNgapToRrc::AN_RELEASE: {
            releaseConnection(w.ueId);
            break;
        }
        case NmGnbNgapToRrc::PAGING: {
            handlePaging(w.uePagingTmsi, w.taiListForPaging);
            break;
        }
        // UE context release after handover completion.
        case NmGnbNgapToRrc::UE_CONTEXT_RELEASE: {
            handoverContextRelease(w.ueId);
            break;
        }
        // AMF approval of handover command (sent to source gNB). Forward to RRC to complete the handover.
        case NmGnbNgapToRrc::HANDOVER_COMMAND_DELIVERY: {
            handleNgapHandoverCommand(w.ueId, w.rrcContainer);
            break;
        }
        case NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK: {
            m_logger->info("UE[%d] PathSwitchRequestAck received, handover fully complete", w.ueId);
            break;
        }
        }
        break;
    }
    case NtsMessageType::GNB_XN_TO_RRC: {
        auto &w = dynamic_cast<NmGnbXnToRrc &>(*msg);
        switch (w.present)
        {
        case NmGnbXnToRrc::HANDOVER_COMMAND_READY:
            m_logger->debug("UE[%d] Xn handover command ready", w.ueId);
            break;
        case NmGnbXnToRrc::HANDOVER_PREP_FAILURE:
            m_logger->warn("UE[%d] Xn handover preparation failed cause=%d", w.ueId, w.causeCode);
            break;
        case NmGnbXnToRrc::SOURCE_CONTEXT_RELEASE:
            m_logger->debug("UE[%d] Xn source context release requested", w.ueId);
            break;
        }
        break;
    }
    case NtsMessageType::TIMER_EXPIRED: {
        auto w = dynamic_cast<NmTimerExpired &>(*msg);
        if (w.timerId == TIMER_ID_SI_BROADCAST)
        {
            setTimer(TIMER_ID_SI_BROADCAST, TIMER_PERIOD_SI_BROADCAST);
            onBroadcastTimerExpired();
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

} // namespace nr::gnb
