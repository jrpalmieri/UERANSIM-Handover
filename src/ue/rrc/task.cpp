//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"
//#include "meas_provider.hpp"

#include <asn/rrc/ASN_RRC_RRCSetupRequest-IEs.h>
#include <asn/rrc/ASN_RRC_RRCSetupRequest.h>
#include <asn/rrc/ASN_RRC_ULInformationTransfer-IEs.h>
#include <asn/rrc/ASN_RRC_ULInformationTransfer.h>
#include <lib/rrc/encode.hpp>
#include <ue/app/task.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <utils/common.hpp>

static constexpr const int TIMER_ID_MACHINE_CYCLE = 1;
static constexpr const int TIMER_PERIOD_MACHINE_CYCLE = 500;
static constexpr const int TIMER_ID_T304 = 2;

namespace nr::ue
{

UeRrcTask::UeRrcTask(TaskBase *base) : m_base{base}, m_timers{}
{
    m_logger = base->logBase->makeUniqueLogger(base->config->getLoggerPrefix() + "rrc");

    m_startedTime = utils::CurrentTimeMillis();
    m_state = ERrcState::RRC_IDLE;
    m_establishmentCause = ASN_RRC_EstablishmentCause_mt_Access;
}

UeRrcTask::~UeRrcTask() = default;

void UeRrcTask::onStart()
{
    triggerCycle();

    setTimer(TIMER_ID_MACHINE_CYCLE, TIMER_PERIOD_MACHINE_CYCLE);

    // Log initial UE position if configured (for D1 distance-based events)
    if (m_base->config->initialPosition.has_value())
    {
        auto &pos = *m_base->config->initialPosition;
        nr::sat::EcefPosition ecef = nr::sat::GeoToEcef(pos);
        m_logger->info("UE position configured: lat=%.6f lon=%.6f alt=%.1fm "
                       "ECEF=(%.1f, %.1f, %.1f)",
                       pos.latitude, pos.longitude, pos.altitude,
                       ecef.x, ecef.y, ecef.z);
    }


}

void UeRrcTask::onQuit()
{
}

void UeRrcTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::UE_NAS_TO_RRC: {
        handleNasSapMessage(dynamic_cast<NmUeNasToRrc &>(*msg));
        break;
    }
    case NtsMessageType::UE_RLS_TO_RRC: {
        handleRlsSapMessage(dynamic_cast<NmUeRlsToRrc &>(*msg));
        break;
    }
    case NtsMessageType::UE_RRC_TO_RRC: {
        auto &w = dynamic_cast<NmUeRrcToRrc &>(*msg);
        switch (w.present)
        {
        case NmUeRrcToRrc::TRIGGER_CYCLE:
            performCycle();
            break;
        }
        break;
    }
    case NtsMessageType::TIMER_EXPIRED: {
        auto &w = dynamic_cast<NmTimerExpired &>(*msg);
        // if the machine cycle timer expires, trigger an RRC cycle
        if (w.timerId == TIMER_ID_MACHINE_CYCLE)
        {
            setTimer(TIMER_ID_MACHINE_CYCLE, TIMER_PERIOD_MACHINE_CYCLE);
            performCycle();
        }
        // if the T304 timer expires, handle handover failure if handover is in progress
        else if (w.timerId == TIMER_ID_T304)
        {
            handleT304Expiry();
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

} // namespace nr::ue