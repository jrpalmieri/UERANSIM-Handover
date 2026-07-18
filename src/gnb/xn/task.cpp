#include "task.hpp"

#include <gnb/nts.hpp>
#include <utils/common.hpp>
#include <utils/nts.hpp>
#include <gnb/neighbors.hpp>

namespace nr::gnb
{

XnTask::XnTask(TaskBase *base) : m_base{base}
{
    m_logger = base->logBase->makeUniqueLogger("xn");
}

void XnTask::onStart()
{
    m_logger->info("XnTask started");

    // Listen for inbound Xn associations from peer gNBs.  Per the initiator
    // rule (lower gnbId connects), the higher-gnbId side of every pair only
    // ever learns its peer through this listener.
    {
        auto listen = std::make_unique<NmGnbSctp>(NmGnbSctp::LISTEN_REQUEST);
        listen->localAddress = m_base->config->xn.xnIp;
        listen->localPort = m_base->config->xn.xnPort;
        listen->ppid = sctp::PayloadProtocolId::XNAP;
        listen->associatedTask = this;
        listen->maxTxStreams = 10000;
        listen->maxRxStreams = 10000;
        m_base->xnSctpTask->push(std::move(listen));
    }

    // set up initial connections
    updateXnConnections();

    // timer used to check for new neighbors and update SCTP connections
    setTimer(TIMER_NEIGHBOR_CHECK, TIMER_NEIGHBOR_CHECK_INTERVAL_MS);

    // timer used to check the deferred queue for requests that were deferred due to missing UE context or other information.
    setTimer(TIMER_DEFERRED_QUEUE, DEFERRED_QUEUE_INTERVAL_MS);
    
    // time used to check for handovers that have been pending for too long, and abort them if necessary
    setTimer(TIMER_HANDOVERS_PENDING, HANDOVERS_PENDING_INTERVAL_MS);

}

void XnTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::GNB_RRC_TO_XN: {
        auto &w = dynamic_cast<NmGnbRrcToXn &>(*msg);
        switch (w.present)
        {
        case NmGnbRrcToXn::HANDOVER_REQUEST_SEND:
        {
            m_logger->info("UE[%ld] HandoverRequest received from RRC, targetNCI=%ld", w.ueId, w.targetNci);
            // must first check that contexts are available for the UE
            // if not, we move the request into the deferred queue for retry
            auto contexts = std::make_unique<GnbHandoverUeContexts>();
            if (!GetUeContexts(w.ueId, *contexts))
            {
                m_logger->warn("UE[%ld] Core Network resources not yet assigned, deferring HandoverRequired", w.ueId);
                auto deferred = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::HANDOVER_REQUEST_SEND);
                deferred->ueId = w.ueId;
                deferred->targetNci = w.targetNci;
                deferred->reason = w.reason;
                deferred->rrcContainer = std::move(w.rrcContainer);
                deferred->choParams = std::move(w.choParams);
                deferred->retries = w.retries;
                enqueueDeferred(std::move(deferred));
                break;
            }
            // if contexts exist, we are good to send the HandoverRequest msg
            sendHandoverRequest(w.ueId, w.targetNci, std::move(contexts), std::move(w.rrcContainer), std::move(w.choParams));
            break;
        }
        case NmGnbRrcToXn::HANDOVER_REQUEST_ACK_SEND:
            // sends the HandoverRequestAck msg back to the source gNB
            sendHandoverRequestAck(w.xnTxId, w.ueId, std::move(w.rrcContainer), std::move(w.admittedSessions), std::move(w.rejectedSessions));
            break;
        case NmGnbRrcToXn::HANDOVER_PREPARATION_FAILURE_SEND:
            // sends the HandoverPreparationFailure msg back to the source gNB
            sendHandoverPreparationFailure(w.xnTxId, w.reason);
            break;
         case NmGnbRrcToXn::HANDOVER_CANCEL_SEND:
            sendHandoverCancel(w.ueId, w.targetNci, w.choParams != nullptr);
            break;
       case NmGnbRrcToXn::UE_CONTEXT_RELEASE_SEND:
           // sends the UeContextRelease msg to the source gNB
            sendUeContextRelease(w.ueId, w.targetNci);
            break;
        case NmGnbRrcToXn::SN_STATUS_TRANSFER_SEND:
            // sends the SnStatusTransfer msg to the source gNB
            sendSnStatusTransfer(w.ueId, w.targetNci, w.choParams != nullptr);
            break;
        case NmGnbRrcToXn::HANDOVER_SUCCESS_SEND:
            sendHandoverSuccess(w.ueId, w.targetNci);
            break;
        case NmGnbRrcToXn::CONDITION_HANDOVER_CANCEL_SEND:
            sendHandoverCancel(w.ueId, w.targetNci, w.choParams != nullptr);
            break;
        }
        break;
    }
    case NtsMessageType::GNB_SCTP: {
        auto &w = dynamic_cast<NmGnbSctp &>(*msg);
        if (w.present == NmGnbSctp::RECEIVE_MESSAGE)
            xnHandleSctpMessage(w.clientId, w.stream, w.buffer);
        if (w.present == NmGnbSctp::ASSOCIATION_SETUP)
            handleAssociationSetup(w.clientId, w.associationId, w.inStreams, w.outStreams);
        if (w.present == NmGnbSctp::ASSOCIATION_SHUTDOWN)
            handleAssociationShutdown(w.clientId);
        if (w.present == NmGnbSctp::CONNECTION_FAILED)
            handleConnectionFailure(w.clientId);
        break;
    }
    case NtsMessageType::TIMER_EXPIRED: {
        auto &w = dynamic_cast<NmTimerExpired &>(*msg);
        if (w.timerId == TIMER_NEIGHBOR_CHECK)
        {
            m_logger->debug("Xn neighbor check timer fired");
            updateXnConnections();
            setTimer(TIMER_NEIGHBOR_CHECK, TIMER_NEIGHBOR_CHECK_INTERVAL_MS);
        }
        else if (w.timerId == TIMER_DEFERRED_QUEUE)
        {
            processDeferredQueue();
            setTimer(TIMER_DEFERRED_QUEUE, DEFERRED_QUEUE_INTERVAL_MS);
        }
        else if (w.timerId == TIMER_HANDOVERS_PENDING)
        {
            processHandoverTimeouts(TIMER_HANDOVERS_PENDING);
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void XnTask::onQuit()
{
    m_logger->info("XnTask stopped");
}

// Update the Xn Connections based on the current neighbor list
void XnTask::updateXnConnections()
{

    // (gnbId, clientId) pairs — closing needs the clientId, removal the gnbId
    std::vector<std::pair<int, int>> connectionsToRemove;

    const int myGnbId = static_cast<int>(m_base->config->getGnbId());

    // get current neighbor list
    auto neighborList = m_base->neighbors->getAll();

    // add connection to remove list if not in current neighbor list
    auto &pt = m_xnPeerTable.getAllPeers();
    for (const auto &peer : pt)
    {
        auto it = std::find_if(neighborList.begin(), neighborList.end(), [&peer](const GnbNeighborState &neighbor) {
            return neighbor.getNci() == peer.nci;
        });

        if (it == neighborList.end())
        {
            connectionsToRemove.emplace_back(peer.gnbId, peer.clientId);
        }
    }

    // remove connections that are no longer in neighbor list
    for (auto [gnbId, clientId] : connectionsToRemove)
    {
        // SCTP teardown
        auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
        msg->clientId = clientId;
        msg->associatedTask = this;
        m_base->xnSctpTask->push(std::move(msg));

        // remove from peer table
        m_xnPeerTable.removePeerInfo(gnbId);
    }

    // Close accepted inbound associations that never produced an XnSetupRequest
    const uint64_t now = static_cast<uint64_t>(utils::CurrentTimeMillis());
    for (auto it = m_pendingInbound.begin(); it != m_pendingInbound.end();)
    {
        if (now - it->second.timestamp > PENDING_INBOUND_TIMEOUT_MS)
        {
            m_logger->warn("Closing inbound Xn association (clientId=%d): no XnSetupRequest received", it->first);
            auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
            msg->clientId = it->first;
            msg->associatedTask = this;
            m_base->xnSctpTask->push(std::move(msg));
            it = m_pendingInbound.erase(it);
        }
        else
            ++it;
    }

    // Initiate connections from the neighbor list.
    // Collision-avoidance rule: only the gNB with the LOWER gnbId initiates the
    // association; the higher one waits for the inbound connection on its
    // listener (its peer entry is created when the XnSetupRequest arrives).
    for (auto &neighborState : neighborList)
    {
        // only consider neighbors with Xn interface and valid address/port
        if (neighborState.handoverInterface == EHandoverInterface::Xn && neighborState.xnAddress && neighborState.xnPort)
        {
            const int neighborGnbId = static_cast<int>(neighborState.getGnbId());

            if (myGnbId == neighborGnbId)
            {
                m_logger->err("Xn neighbor NCI 0x%09lx has the same gnbId (%d) as this gNB; check configuration",
                              neighborState.getNci(), myGnbId);
                continue;
            }
            if (myGnbId > neighborGnbId)
                continue; // we are the responder for this pair — wait for inbound

            // find in current peer table by gnbId
            auto peer = m_xnPeerTable.getPeerInfo(neighborGnbId);

            if (!peer)
            {
                // not found, add new connection

                // add to peer table
                XnPeerInfo peerInfo;
                peerInfo.gnbId = neighborGnbId;
                peerInfo.clientId = neighborGnbId; // outbound connections use the gnbId as SCTP clientId
                peerInfo.nci = neighborState.getNci();
                peerInfo.addr = InetAddress(neighborState.xnAddress.value(), *neighborState.xnPort);
                peerInfo.connectionState = EXnConnectionState::CONNECTION_REQUESTED;
                m_xnPeerTable.addPeerInfo(peerInfo);

                // SCTP setup
                requestSctpConnection(neighborGnbId, neighborState.xnAddress.value(),
                                      neighborState.xnPort.value());

                m_logger->info("Adding Xn connection to gNB %d (NCI 0x%09lx)", neighborGnbId,
                               neighborState.getNci());
            }
            else if (peer->connectionState == EXnConnectionState::CONNECTION_FAILED)
            {
                // A previous attempt failed (SCTP connect refused, or the peer
                // answered XnSetupFailure) — retry on this periodic check.
                // Close first: in the XnSetupFailure case a live association/
                // ClientEntry still exists under this clientId, and re-requesting
                // without closing would leak it in the SCTP task.  Close on a
                // nonexistent entry is a harmless warning.
                peer->connectionState = EXnConnectionState::CONNECTION_REQUESTED;

                auto close = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
                close->clientId = peer->clientId;
                close->associatedTask = this;
                m_base->xnSctpTask->push(std::move(close));

                peer->clientId = peer->gnbId; // fresh outbound attempt
                requestSctpConnection(peer->gnbId, neighborState.xnAddress.value(),
                                      neighborState.xnPort.value());

                m_logger->info("Retrying Xn connection to gNB %d", peer->gnbId);
            }
            else
            {
                // already exists
                //  TODO: Update connection parameters if needed
            }
        }
    }

} // updateXnConnections

// Issue an SCTP CONNECTION_REQUEST toward a neighbor's Xn endpoint (on the
// dedicated Xn SCTP task; clientId = the neighbor's gnbId).
void XnTask::requestSctpConnection(int gnbId, const std::string &remoteAddress, uint16_t remotePort)
{
    auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_REQUEST);
    msg->clientId = gnbId;
    msg->localAddress = m_base->config->xn.xnIp;
    msg->localPort = 0;
    msg->remoteAddress = remoteAddress;
    msg->remotePort = remotePort;
    msg->ppid = sctp::PayloadProtocolId::XNAP;
    msg->associatedTask = this;
    msg->maxTxStreams = 10000;
    msg->maxRxStreams = 10000;
    m_base->xnSctpTask->push(std::move(msg));
}

// The SCTP task could not bind/connect toward this peer.  Mark the peer
// CONNECTION_FAILED so the periodic neighbor check retries it.  Retry matters
// especially at startup: whichever gNB boots first has its INIT ABORTed by the
// not-yet-bound peer, and only a later attempt brings the interface up.
void XnTask::handleConnectionFailure(int clientId)
{
    auto *peer = m_xnPeerTable.findByClientId(clientId);
    if (peer == nullptr)
        return;

    m_logger->warn("Xn SCTP connection to gNB %d failed; retrying in %d ms", peer->gnbId,
                   TIMER_NEIGHBOR_CHECK_INTERVAL_MS);
    peer->connectionState = EXnConnectionState::CONNECTION_FAILED;
}

// get copies of all UE Contexts for the given UE ID.
// Returns true if all contexts were found and copied, false if any did not exist.
bool XnTask::GetUeContexts(int64_t ueId, GnbHandoverUeContexts &out)
{
    if (m_base->ngapTask->getUeContext(ueId, out.ngapUeContext) &&
        m_base->rrcTask->getUeContext(ueId, out.rrcUeContext) &&
        m_base->gtpTask->getUeContext(ueId, out.gtpUeContext) &&
        m_base->gtpTask->getPduSessions(ueId, out.pduSessions))
    {
        return true;
    }
    return false;
}

void XnTask::enqueueDeferred(std::unique_ptr<NmGnbRrcToXn> msg)
{
    m_deferredQueue.push_back(std::move(msg));
}


// pull msg from the deferred queue and process if information is ready
void XnTask::processDeferredQueue()
{
    int count = static_cast<int>(m_deferredQueue.size());
    for (int i = 0; i < count; ++i)
    {
        // remove from queue
        auto msg = std::move(m_deferredQueue.front());
        m_deferredQueue.pop_front();

        // copy contexts for handover request
        auto contexts = std::make_unique<GnbHandoverUeContexts>();
        if (GetUeContexts(msg->ueId, *contexts))
        {
            sendHandoverRequest(msg->ueId, msg->targetNci, std::move(contexts), std::move(msg->rrcContainer), std::move(msg->choParams));
            return;
        }
        // if still not ready after the retry threshold, drop the request
        else if (msg->retries >= DEFERRED_MAX_RETRIES)
        {
            m_logger->err("UE[%ld] Dropping deferred HandoverRequired after %d retries", msg->ueId, msg->retries);
            // A local dependency failure must resolve the RRC procedure just
            // like a remote preparation failure; otherwise handoverDecisionPending
            // remains set indefinitely and blocks future mobility decisions.
            auto failure = std::make_unique<NmGnbXnToRrc>(NmGnbXnToRrc::HANDOVER_PREPARATION_FAILURE_RECEIVED);
            failure->ueId = msg->ueId;
            failure->targetNci = msg->targetNci;
            failure->isCho = msg->choParams != nullptr;
            failure->reason = ASN_XNAP_Cause_PR_transport;
            m_base->rrcTask->push(std::move(failure));
        }
        // otherwise, re-enqueue for another retry after some delay
        else
        {
            msg->retries++;
            m_deferredQueue.push_back(std::move(msg));
        }
    }
}

} // namespace nr::gnb
