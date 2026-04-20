//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <sstream>

#include <gnb/app/task.hpp>
#include <gnb/sctp/task.hpp>
#include <utils/nts.hpp>

namespace nr::gnb
{

NgapTask::NgapTask(TaskBase *base) : m_base{base}, m_ueNgapIdCounter{}, m_downlinkTeidCounter{}, m_isInitialized{}
{
    m_logger = base->logBase->makeUniqueLogger("ngap");
}

void NgapTask::onStart()
{
    for (auto &amfConfig : m_base->config->amfConfigs)
        createAmfContext(amfConfig);
    if (m_amfCtx.empty())
        m_logger->warn("No AMF configuration is provided");

    for (auto &amfCtx : m_amfCtx)
    {
        auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_REQUEST);
        msg->clientId = amfCtx.second->ctxId;
        msg->localAddress = m_base->config->ngapIp;
        msg->localPort = 0;
        msg->remoteAddress = amfCtx.second->address;
        msg->remotePort = amfCtx.second->port;
        msg->ppid = sctp::PayloadProtocolId::NGAP;
        msg->associatedTask = this;
        m_base->sctpTask->push(std::move(msg));
    }

    setTimer(TIMER_DEFERRED_QUEUE, DEFERRED_QUEUE_INTERVAL_MS);
}

void NgapTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::GNB_RRC_TO_NGAP: {
        auto &w = dynamic_cast<NmGnbRrcToNgap &>(*msg);
        switch (w.present)
        {
        case NmGnbRrcToNgap::INITIAL_NAS_DELIVERY: {
            handleInitialNasTransport(w.ueId, w.pdu, w.rrcEstablishmentCause, w.sTmsi);
            break;
        }
        case NmGnbRrcToNgap::UPLINK_NAS_DELIVERY: {
            handleUplinkNasTransport(w.ueId, w.pdu);
            break;
        }
        case NmGnbRrcToNgap::RADIO_LINK_FAILURE: {
            handleRadioLinkFailure(w.ueId);
            break;
        }
        // RRC (target gnb) notifies NGAP of handover completion (RRCReconfigComplete). NGAP will notify AMF with HANDOVER_NOTIFY.
        //      AMF will notify source gnb to delete UE context.
        case NmGnbRrcToNgap::HANDOVER_NOTIFY: {
            m_logger->info("UE[%d] Handover complete notification from RRC", w.ueId);
            handleHandoverNotifyFromRrc(w.ueId);
            break;
        }
        // RRC (source gnb) notifies NGAP of handover start. NGAP will notify AMF with HANDOVER_REQUIRED.
        //      AMF will then notify target gNB.
        case NmGnbRrcToNgap::HANDOVER_REQUIRED: {
            m_logger->info("UE[%d] HandoverRequired received from RRC, targetPCI=%d", w.ueId, w.hoTargetPci);
            
            auto *_ue = findUeContext(w.ueId);
            if (!_ue || _ue->amfUeNgapId < 1 || _ue->pduSessions.empty())
            {
                m_logger->warn("UE[%d] Core Network resources not yet assigned, deferring HandoverRequired", w.ueId);
                auto deferred = std::make_unique<NmGnbRrcToNgap>(NmGnbRrcToNgap::HANDOVER_REQUIRED);
                deferred->ueId = w.ueId;
                deferred->hoTargetPci = w.hoTargetPci;
                deferred->hoCause = w.hoCause;
                deferred->hoForChoPreparation = w.hoForChoPreparation;
                deferred->retries = w.retries;
                enqueueDeferred(std::move(deferred));
                break;
            }
            sendHandoverRequired(w.ueId, w.hoTargetPci, w.hoCause, w.hoForChoPreparation);
            break;
        }
        }
        break;
    }
    case NtsMessageType::GNB_SCTP: {
        auto &w = dynamic_cast<NmGnbSctp &>(*msg);
        switch (w.present)
        {
        case NmGnbSctp::ASSOCIATION_SETUP:
            handleAssociationSetup(w.clientId, w.associationId, w.inStreams, w.outStreams);
            break;
        case NmGnbSctp::RECEIVE_MESSAGE:
            handleSctpMessage(w.clientId, w.stream, w.buffer);
            break;
        case NmGnbSctp::ASSOCIATION_SHUTDOWN:
            handleAssociationShutdown(w.clientId);
            break;
        default:
            m_logger->unhandledNts(*msg);
            break;
        }
        break;
    }
    case NtsMessageType::GNB_XN_TO_NGAP: {
        auto &w = dynamic_cast<NmGnbXnToNgap &>(*msg);
        switch (w.present)
        {
        case NmGnbXnToNgap::PATH_SWITCH_REQUEST_REQUIRED: {
            m_logger->info("Xn requested PathSwitchRequest UE[%d] ", w.ueId);
            sendPathSwitchRequest(w.ueId);
            break;
        }
        }
        break;
    }
    case NtsMessageType::TIMER_EXPIRED: {
        auto &w = dynamic_cast<NmTimerExpired &>(*msg);
        if (w.timerId == TIMER_DEFERRED_QUEUE)
        {
            processDeferredQueue();
            setTimer(TIMER_DEFERRED_QUEUE, DEFERRED_QUEUE_INTERVAL_MS);
        }
        break;
    }
    default: {
        m_logger->unhandledNts(*msg);
        break;
    }
    }
}

void NgapTask::enqueueDeferred(std::unique_ptr<NmGnbRrcToNgap> msg)
{
    m_deferredQueue.push_back(std::move(msg));
}

void NgapTask::processDeferredQueue()
{
    int count = static_cast<int>(m_deferredQueue.size());
    for (int i = 0; i < count; ++i)
    {
        auto msg = std::move(m_deferredQueue.front());
        m_deferredQueue.pop_front();

        if (m_ueCtx.count(msg->ueId) && (m_ueCtx[msg->ueId]->amfUeNgapId > 0 && !m_ueCtx[msg->ueId]->pduSessions.empty()))
        {
            sendHandoverRequired(msg->ueId, msg->hoTargetPci, msg->hoCause, msg->hoForChoPreparation);
        }
        else if (msg->retries >= DEFERRED_MAX_RETRIES)
        {
            m_logger->err("UE[%d] Dropping deferred HandoverRequired after %d retries", msg->ueId, msg->retries);
        }
        else
        {
            msg->retries++;
            m_deferredQueue.push_back(std::move(msg));
        }
    }
}

void NgapTask::onQuit()
{
    for (auto &i : m_ueCtx)
        delete i.second;
    for (auto &i : m_amfCtx)
        delete i.second;
    m_ueCtx.clear();
    m_amfCtx.clear();
}

} // namespace nr::gnb
