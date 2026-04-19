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
#include <utils/common.hpp>
#include <utils/position_calcs.hpp>

#include <asn/rrc/ASN_RRC_DLInformationTransfer-IEs.h>
#include <asn/rrc/ASN_RRC_DLInformationTransfer.h>

#include <cmath>

static constexpr const int TIMER_ID_SI_BROADCAST = 1;
static constexpr const int TIMER_PERIOD_SI_BROADCAST = 10'000;
static constexpr const int TIMER_ID_SIB19_BROADCAST = 2;
static constexpr const int TIMER_ID_SAT_CACHE_CALC = 3;
static constexpr const int TIMER_PERIOD_SAT_CACHE_CALC = 15'000;

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
    setTimer(TIMER_ID_SAT_CACHE_CALC, TIMER_PERIOD_SAT_CACHE_CALC);

    if (m_base->gnbPosition.isValid)
        setTrueGeoPosition(m_base->gnbPosition);

    if (m_config->ntn.ownTle.has_value())
    {
        upsertSatTles({*m_config->ntn.ownTle});
        m_logger->info("Loaded own TLE from config (pci=%d)", m_config->ntn.ownTle->pci);
    }

    if (m_config->ntn.sib19.sib19On)
        setTimer(TIMER_ID_SIB19_BROADCAST, m_config->ntn.sib19.sib19TimingMs);
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
            triggerSib19Broadcast();
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
            handleNgapHandoverCommand(w.ueId, w.rrcContainer, w.hoForChoPreparation);
            break;
        }
        case NmGnbNgapToRrc::HANDOVER_FAILURE: {
            handleNgapHandoverFailure(w.ueId, w.hoTargetPci, w.hoForChoPreparation);
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
        else if (w.timerId == TIMER_ID_SIB19_BROADCAST)
        {
            setTimer(TIMER_ID_SIB19_BROADCAST, m_config->ntn.sib19.sib19TimingMs);
            triggerSib19Broadcast();
        }
        else if (w.timerId == TIMER_ID_SAT_CACHE_CALC)
        {
            setTimer(TIMER_ID_SAT_CACHE_CALC, TIMER_PERIOD_SAT_CACHE_CALC);
            roughNeighborhoodSats();
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void GnbRrcTask::setTruePositionVelocity(const PositionVelocity &value)
{
    m_truePositionVelocity = value;

    if (!value.isValid)
        return;

    auto geo = EcefToGeo(EcefPosition{value.x, value.y, value.z});
    geo.isValid = true;
    m_base->gnbPosition = geo;
}

void GnbRrcTask::setTrueGeoPosition(const GeoPosition &value)
{
    if (!std::isfinite(value.latitude) || !std::isfinite(value.longitude) || !std::isfinite(value.altitude))
        return;
    if (value.latitude < -90.0 || value.latitude > 90.0)
        return;
    if (value.longitude < -180.0 || value.longitude > 180.0)
        return;

    GeoPosition normalized = value;
    normalized.isValid = true;
    m_base->gnbPosition = normalized;

    EcefPosition ecef = GeoToEcef(normalized);

    PositionVelocity pv{};
    pv.isValid = true;
    pv.x = ecef.x;
    pv.y = ecef.y;
    pv.z = ecef.z;
    pv.vx = 0.0;
    pv.vy = 0.0;
    pv.vz = 0.0;
    pv.epochMs = static_cast<int64_t>(utils::CurrentTimeMillis());
    m_truePositionVelocity = pv;
}

GeoPosition GnbRrcTask::getTrueGeoPosition() const
{
    return m_base->gnbPosition;
}

void GnbRrcTask::upsertSatellitePositionVelocity(const SatellitePositionVelocityEntry &value)
{
    m_satellitePvByPci[value.pci] = value;
}

} // namespace nr::gnb
