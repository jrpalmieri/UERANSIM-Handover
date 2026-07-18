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
#include <gnb/ngap/task.hpp>
#include <gnb/xn/task.hpp>
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

// Expiry sweep for m_handoversPending (see sweepPendingHandovers()).  1 s
// granularity is ample: the entries themselves expire after 5 s (basic) or
// 100 s (CHO).
static constexpr const int TIMER_ID_HO_PENDING_SWEEP = 5;
static constexpr const int TIMER_PERIOD_HO_PENDING_SWEEP = 1000;  //ms


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
    // garbage-collect abandoned target-side handover preparations (issue 6)
    setTimer(TIMER_ID_HO_PENDING_SWEEP, TIMER_PERIOD_HO_PENDING_SWEEP);

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

    // Every UE-context read/mutation happens on this thread inside the
    // dispatch below, so one writer lock per message suffices; getUeContext()
    // on other threads (Xn) takes the shared lock.  See m_ueCtxMutex.
    std::unique_lock<std::shared_mutex> ueCtxLock(m_ueCtxMutex);

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
            handleHandoverRequest(0, w.ngapTxId, std::move(w.rrcContainer), std::move(w.sessionList), w.isCho, ERequestingTask::NGAP);
            break;
        }
        // Source gNB received Handover Command from AMF.
        case NmGnbNgapToRrc::HANDOVER_COMMAND_RECEIVED: {
            handleHandoverAckOrCommand(w.ueId, std::move(w.rrcContainer), w.isCho,
                                       ERequestingTask::NGAP, w.hoTargetNci);
            break;
        }
        // Source gNB received Handover Preparation Failure from AMF.
        case NmGnbNgapToRrc::HANDOVER_PREPARATION_FAILURE_RECEIVED: {
            handleHandoverPreparationFailure(w.ueId, w.hoTargetNci, w.isCho, ERequestingTask::NGAP);
            break;
        }
        case NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK: {
            m_logger->info("UE[%ld] PathSwitchRequestAck received; requesting Xn source release", w.ueId);

            // Store the fresh {NCC, NH} pair from the AMF's SecurityContext:
            // the next handover this gNB sources uses it for key derivation
            // (simulated security: carried in the handover context, no KDF run).
            if (w.hasSecurityContext)
            {
                auto *ue = findCtxByUeId(w.ueId);
                if (ue != nullptr)
                {
                    ue->nextHopChainingCount = w.nextHopChainingCount;
                    ue->nextHopParameter = w.nextHopParameter;
                    m_logger->debug("UE[%ld] security context refreshed by path switch (NCC=%d)", w.ueId,
                                    w.nextHopChainingCount);
                }
            }

            auto release = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::UE_CONTEXT_RELEASE_SEND);
            release->ueId = w.ueId;
            // Xn resolves the source peer and both XnAP UE IDs from its retained
            // target-side correlation; no NCI inference is needed here.
            m_base->xnTask->push(std::move(release));
            break;
        }
        case NmGnbNgapToRrc::PATH_SWITCH_REQUEST_FAILURE: {
            // Keep both sides' contexts intact so a bounded retry/cancel policy
            // can be added without destroying the still-recoverable source path.
            m_logger->err("UE[%ld] PathSwitchRequest failed; Xn source context retained", w.ueId);
            break;
        }
        case NmGnbNgapToRrc::SECURITY_INFO: {
            handleNgapSecurityInfo(w.ueId, std::move(w.ueSecInfo));
            break;
        }
        case NmGnbNgapToRrc::PDU_SESSION_UPDATE: {
            if (!w.releasedPsis.empty())
                handleNgapPduSessionRelease(w.ueId, w.releasedPsis);
            if (w.sessionList && !w.sessionList->empty())
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
            handleHandoverRequest(w.sourceGnbId, w.xnTxId, std::move(w.rrcContainer),
                                  std::move(w.sessionList), w.isCho, ERequestingTask::XN,
                                  std::move(w.xnCoreContext), std::move(w.xnChoRequest));
            break;
        // Source gNB received Handover Request Ack from Target gNB
        case NmGnbXnToRrc::HANDOVER_REQUEST_ACK_RECEIVED:
            // Xn has already correlated the ACK with the outgoing request, so
            // targetNci/isCho are reliable here and the shared N2/Xn command
            // handler can perform the actual RRC transition.
            handleHandoverAckOrCommand(w.ueId, std::move(w.rrcContainer), w.isCho,
                                       ERequestingTask::XN, w.targetNci);
            break;
        // Source gNB received Handover Preparation Failure from Target gNB
        case NmGnbXnToRrc::HANDOVER_PREPARATION_FAILURE_RECEIVED:
            handleHandoverPreparationFailure(w.ueId, w.targetNci, w.isCho, ERequestingTask::XN);
            break;
        // Source gNB received UE Context Release from Target gNB
        case NmGnbXnToRrc::UE_CONTEXT_RELEASE_RECEIVED:
        {
            // Successful handover release never sends RRCRelease over the old
            // radio path.  Clean RRC locally and ask NGAP to remove source N2/N3.
            handleUeContextRelease(w.ueId, NgapCause::RadioNetwork_successful_handover);
            auto coreRelease = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::XN_SOURCE_CONTEXT_RELEASE);
            coreRelease->ueId = w.ueId;
            m_base->ngapTask->push(std::move(coreRelease));
            break;
        }
        // Target gNB received Handover Cancel from Source gNB
        case NmGnbXnToRrc::HANDOVER_CANCEL_RECEIVED:
        {
            auto pending = m_handoversPending.find(w.ueId);
            if (pending != m_handoversPending.end())
            {
                discardHandoverUeContext(pending->second.ctx);
                m_handoversPending.erase(pending);
            }
            // Roll back every provisional layer.  Each receiver is idempotent,
            // which makes a duplicate or late cancel a safe no-op.
            auto ngapCancel = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::XN_TARGET_PREPARATION_CANCEL);
            ngapCancel->ueId = w.ueId;
            m_base->ngapTask->push(std::move(ngapCancel));
            auto rlsCancel = std::make_unique<NmGnbRrcToRls>(NmGnbRrcToRls::REMOVE_UE_CONTEXT);
            rlsCancel->ueId = w.ueId;
            m_base->rlsTask->push(std::move(rlsCancel));
            m_logger->info("UE[%ld] Xn target preparation cancelled", w.ueId);
            break;
        }
        // Target gNB received SN Status Transfer from Source gNB
        case NmGnbXnToRrc::SN_STATUS_TRANSFER_RECEIVED:
        {
            auto apply = std::make_unique<NmGnbRrcToRls>(NmGnbRrcToRls::APPLY_DRB_SN_STATUS);
            apply->ueId = w.ueId;
            apply->drbSnStatus = std::move(w.drbSnStatus);
            m_base->rlsTask->push(std::move(apply));
            break;
        }
        // Source gNB received Handover Success from Target gNB
        case NmGnbXnToRrc::HANDOVER_SUCCESS_RECEIVED:
        {
            auto *ue = findCtxByUeId(w.ueId);
            if (!ue || (ue->handoverTargetNci > 0 && ue->handoverTargetNci != w.targetNci))
            {
                m_logger->warn("UE[%ld] unmatched Xn HandoverSuccess ignored", w.ueId);
                break;
            }
            // Success confirms radio execution only.  Keep source RRC/NGAP/GTP
            // state until UEContextRelease arrives after target Path Switch.
            ue->handoverDecisionPending = false;
            ue->handoverInProgress = true;
            ue->handoverTargetNci = w.targetNci;
            m_logger->info("UE[%ld] Xn radio execution succeeded; source context retained", w.ueId);
            break;
        }
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
        else if (w.timerId == TIMER_ID_HO_PENDING_SWEEP)
        {
            setTimer(TIMER_ID_HO_PENDING_SWEEP, TIMER_PERIOD_HO_PENDING_SWEEP);
            sweepPendingHandovers();
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
