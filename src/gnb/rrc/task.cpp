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
#include <lib/sat/sat_calc.hpp>
#include <lib/sat/sat_state.hpp>
#include <lib/sat/sat_time.hpp>

#include <asn/rrc/ASN_RRC_DLInformationTransfer-IEs.h>
#include <asn/rrc/ASN_RRC_DLInformationTransfer.h>

#include <cmath>

static constexpr const int TIMER_ID_SI_BROADCAST = 1;
static constexpr const int TIMER_PERIOD_SI_BROADCAST = 10'000;
static constexpr const int TIMER_ID_SIB19_BROADCAST = 2;
static constexpr const int TIMER_ID_SAT_CACHE_CALC = 3;
static constexpr const int TIMER_PERIOD_SAT_CACHE_CALC = 15'000;

static constexpr const int TIMER_ID_UPDATE_LOC = 4;
static constexpr const int TIMER_PERIOD_UPDATE_LOC = 1000;  //ms

static constexpr const int TIMER_ID_UPDATE_STATUS = 1001;
static constexpr const int TIMER_PERIOD_UPDATE_STATUS = 500;  //ms


namespace nr::gnb
{

GnbRrcTask::GnbRrcTask(TaskBase *base) : m_base{base}, m_ueCtx{}
{
    m_logger = base->logBase->makeUniqueLogger("rrc");
    m_config = m_base->config;
}

void GnbRrcTask::onStart()
{
    setTimer(TIMER_ID_SI_BROADCAST, TIMER_PERIOD_SI_BROADCAST);

    if (m_config->ntn.ntnEnabled) {

        m_logger->info("NTN enabled: recalculating location from TLE and satTime");
        onUpdateLocationTimerExpired();
        auto gnbGeo = m_base->getGnbPosition();
        m_logger->info("NTN enabled: initial gNB location set to lat=%.6f, lon=%.6f, alt=%.1f",
             gnbGeo.latitude, gnbGeo.longitude, gnbGeo.altitude);

        m_logger->info("NTN enabled: starting timers for location update (%dms) and satellite cache calculation (%dms)",
             TIMER_PERIOD_UPDATE_LOC, TIMER_PERIOD_SAT_CACHE_CALC);

        setTimer(TIMER_ID_UPDATE_LOC, TIMER_PERIOD_UPDATE_LOC);
        setTimer(TIMER_ID_SAT_CACHE_CALC, TIMER_PERIOD_SAT_CACHE_CALC);
        setTimer(TIMER_ID_UPDATE_STATUS, TIMER_PERIOD_UPDATE_STATUS);

        if (m_config->ntn.sib19.sib19On)
        {
            m_logger->info("NTN enabled: SIB19 enabled with timing %dms", m_config->ntn.sib19.sib19TimingMs);
            setTimer(TIMER_ID_SIB19_BROADCAST, m_config->ntn.sib19.sib19TimingMs);
        }
    }
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
            if (m_config->ntn.ntnEnabled && m_config->ntn.sib19.sib19On)
                triggerSib19Broadcast();
    
            break;
        }
        case NmGnbNgapToRrc::NAS_DELIVERY: {
            handleDownlinkNasDelivery(w.ueId, w.pdu);
            break;
        }
        case NmGnbNgapToRrc::NAS_ACCEPT: {
            handleDownlinkNasAccept(w.ueId, w.pdu, std::move(w.sessionList));
            break;
        }
        case NmGnbNgapToRrc::UE_CONTEXT_RELEASE_RECEIVED: {
            handleUeContextRelease(w.ueId, w.cause);
            break;
        }
        case NmGnbNgapToRrc::PAGING: {
            handlePaging(w.uePagingTmsi, w.taiListForPaging);
            break;
        }
        // Target gNB received Handover Request from AMF
        case NmGnbNgapToRrc::HANDOVER_REQUEST_RECEIVED: {
            handleHandoverRequest(0, w.ngapTxId, std::move(w.rrcContainer), std::move(w.sessionList), w.isCho, EReqestingTask::NGAP);
            break;
        }
        // Source gNB received Handover Command from AMF.
        case NmGnbNgapToRrc::HANDOVER_COMMAND_RECEIVED: {
            handleHandoverAckOrCommand(w.ueId, std::move(w.rrcContainer), w.isCho, EReqestingTask::NGAP);
            break;
        }
        // Source gNB received Handover Preparation Failure from AMF.
        case NmGnbNgapToRrc::HANDOVER_PREPARATION_FAILURE_RECEIVED: {
            handleHandoverPreparationFailure(w.ueId, w.hoTargetNci, w.isCho, EReqestingTask::NGAP);
            break;
        }
        case NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK: {
            m_logger->info("UE[%ld] PathSwitchRequestAck received, handover fully complete", w.ueId);
            break;
        }
        case NmGnbNgapToRrc::SECURITY_INFO: {
            handleNgapSecurityInfo(w.ueId, std::move(w.ueSecInfo));
            break;
        }
        case NmGnbNgapToRrc::PDU_SESSION_UPDATE: {
            handleNgapPduSessionUpdate(w.ueId, std::move(w.sessionList));
            break;
        }
        default:
            m_logger->unhandledNts(*msg);
            break;
        }
        break;
    }
    case NtsMessageType::GNB_XN_TO_RRC: {
        auto &w = dynamic_cast<NmGnbXnToRrc &>(*msg);
        switch (w.present)
        {
        // Target gNB received Handover Request from Source gNB
        case NmGnbXnToRrc::HANDOVER_REQUEST_RECEIVED:
            handleHandoverRequest(0, w.xnTxId, std::move(w.rrcContainer), std::move(w.sessionList), w.isCho, EReqestingTask::XN);
            break;
        // Source gNB received Handover Request Ack from Target gNB
        case NmGnbXnToRrc::HANDOVER_REQUEST_ACK_RECEIVED:
            m_logger->debug("UE[%ld] Xn handover command ready", w.ueId);
            break;
        // Source gNB received Handover Preparation Failure from Target gNB
        case NmGnbXnToRrc::HANDOVER_PREPARATION_FAILURE_RECEIVED:
            handleHandoverPreparationFailure(w.ueId, w.targetNci, w.isCho, EReqestingTask::XN);
            break;
        // Source gNB received UE Context Release from Target gNB
        case NmGnbXnToRrc::UE_CONTEXT_RELEASE_RECEIVED:
            m_logger->debug("UE[%ld] Xn source context release requested", w.ueId);
            break;
        // Target gNB received Handover Cancel from Source gNB
        case NmGnbXnToRrc::HANDOVER_CANCEL_RECEIVED:
            m_logger->debug("UE[%ld] Xn handover cancel received", w.ueId);
            break;
        // Target gNB received SN Status Transfer from Source gNB
        case NmGnbXnToRrc::SN_STATUS_TRANSFER_RECEIVED:
            m_logger->debug("UE[%ld] Xn SN status transfer received", w.ueId);
            break;
        // Source gNB received Handover Success from Target gNB
        case NmGnbXnToRrc::HANDOVER_SUCCESS_RECEIVED:
            m_logger->debug("UE[%ld] Xn handover success received", w.ueId);
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
        else if (w.timerId == TIMER_ID_UPDATE_LOC)
        {
            setTimer(TIMER_ID_UPDATE_LOC, TIMER_PERIOD_UPDATE_LOC);
            onUpdateLocationTimerExpired();
        }
        else if (w.timerId == TIMER_ID_UPDATE_STATUS)
        {
            setTimer(TIMER_ID_UPDATE_STATUS, TIMER_PERIOD_UPDATE_STATUS);
            onUpdateGnbStatusTimerExpired();
        }
        break;
    }
    default:
    {
        m_logger->unhandledNts(*msg);
        break;
    }
    
    }

}



GeoPosition GnbRrcTask::getTrueGeoPosition() const
{
    return m_base->getGnbPosition();
}

void GnbRrcTask::upsertSatellitePositionVelocity(const SatellitePositionVelocityEntry &value)
{
    m_satellitePvByNci[value.nci] = value;
}

void GnbRrcTask::onUpdateLocationTimerExpired()
{

    m_logger->debug("Update Location Timer Triggered - Updating gNB location by propagating TLE to current time");

    if (!m_config->ntn.ntnEnabled)
    {
        m_logger->warn("NTN disabled; cannot update location");
        return;
    }

    // pull the current TLE from the TLE store
    int64_t ownNci = m_config->nci;
    auto ownTle = m_base->satStates->getTle(ownNci);
    if (!ownTle.has_value()){
        m_logger->warn("Own TLE not found for NCI %ld; cannot update location", ownNci);
        return;
    }

    GeoPosition gnbGeo;
    auto satNow = m_base->satTime->CurrentSatTimeMillis();

    gnbGeo = EcefToGeo(m_base->satStates->getSgp4(ownNci)->FindPositionEcef(satNow));

    // write location update to global state
    m_base->setGnbPosition(gnbGeo, satNow);
    m_logger->info("Updated gNB location (satTime=%llu): lat=%.6f, lon=%.6f, alt=%.1f", satNow, gnbGeo.latitude, gnbGeo.longitude, gnbGeo.altitude);   

}

void GnbRrcTask::onUpdateGnbStatusTimerExpired()
{
    GnbStatusInfoUpdate update;
    update.nci = m_config->nci;
    update.rrcConnectedUesIsPresent = true;
    update.rrcConnectedUes = static_cast<int>(m_ueCtx.size());
    
    m_base->setGnbStatusInfo(update);
}



} // namespace nr::gnb
