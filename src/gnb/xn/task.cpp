#include "task.hpp"

#include <gnb/nts.hpp>
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

    // set up initial connections
    updateXnConnections();

    setTimer(TIMER_NEIGHBOR_CHECK, TIMER_NEIGHBOR_CHECK_INTERVAL_MS);
    setTimer(TIMER_DEFERRED_QUEUE, DEFERRED_QUEUE_INTERVAL_MS);

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
            // get contexts for handover request
            auto contexts = std::make_unique<GnbHandoverUeContexts>();
            if (!GetUeContexts(w.ueId, *contexts))
            {
                m_logger->warn("UE[%ld] Core Network resources not yet assigned, deferring HandoverRequired", w.ueId);
                auto deferred = std::make_unique<NmGnbRrcToXn>(NmGnbRrcToXn::HANDOVER_REQUEST_SEND);
                deferred->ueId = w.ueId;
                deferred->targetNci = w.targetNci;
                deferred->reason = w.reason;
                deferred->rrcContainer = std::move(w.rrcContainer);
                deferred->isCho = w.isCho;
                deferred->retries = w.retries;
                enqueueDeferred(std::move(deferred));
                break;
            }

            xnHandoverRequestSource(w.ueId, w.targetNci, w.isCho, std::move(contexts));
            break;
        }
        case NmGnbRrcToXn::HANDOVER_REQUEST_ACK_SEND:
            xnHandoverRequestAckTarget(w.ueId, w.targetNci, w.isCho, std::move(w.rrcContainer));
            break;
        case NmGnbRrcToXn::HANDOVER_CANCEL_SEND:
            xnHandoverCancelSource(w.ueId, w.targetNci, w.isCho);
            break;
        case NmGnbRrcToXn::HANDOVER_PREPARATION_FAILURE_SEND:
            xnHandoverPreparationFailureTarget(w.ueId, w.targetNci, w.isCho, w.reason);
            break;
        case NmGnbRrcToXn::UE_CONTEXT_RELEASE_SEND:
            xnUeContextReleaseTarget(w.ueId, w.targetNci);
            break;
        case NmGnbRrcToXn::SN_STATUS_TRANSFER_SEND:
            xnSnStatusTransferSource(w.ueId, w.targetNci, w.isCho);
            break;
        case NmGnbRrcToXn::HANDOVER_SUCCESS_SEND:
            xnHandoverSuccessTarget(w.ueId, w.targetNci);
            break;
        case NmGnbRrcToXn::CONDITION_HANDOVER_CANCEL_SEND:
            xnConditionalHandoverCancelSource(w.ueId, w.targetNci);
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
        else
        {
            xnHandleTimerPrep(w.timerId);
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

    std::vector<int> connectionsToRemove;

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
            connectionsToRemove.push_back(peer.gnbId);
        }
    }

    // remove connections that are no longer in neighbor list
    for (int gnbId : connectionsToRemove)
    {
        // SCTP teardown
        auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_CLOSE);
        msg->clientId = gnbId;
        msg->associatedTask = this;
        m_base->sctpTask->push(std::move(msg));

        // remove from peer table
        m_xnPeerTable.removePeerInfo(gnbId);
    }


    // Add new connections from neighbor list
    for (auto &neighborState : neighborList)
    {
        // only consider neighbors with Xn interface and valid address/port
        if (neighborState.handoverInterface == EHandoverInterface::Xn && neighborState.xnAddress && neighborState.xnPort)
        {
            // find in current peer table by NCI
            auto peer = m_xnPeerTable.getPeerInfo(neighborState.getGnbId());

            if (!peer)
            {
                // not found, add new connection

                // add to peer table
                XnPeerInfo peerInfo;
                peerInfo.gnbId = neighborState.getGnbId();
                peerInfo.nci = neighborState.getNci();
                peerInfo.addr = InetAddress(neighborState.xnAddress.value(), *neighborState.xnPort);
                m_xnPeerTable.addPeerInfo(peerInfo);

                // SCTP setup
                auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_REQUEST);
                msg->clientId = neighborState.getGnbId(); // use gnbId as clientId
                msg->localAddress = m_base->config->xn.xnIp;
                msg->localPort = 0;
                msg->remoteAddress = neighborState.xnAddress.value();
                msg->remotePort = neighborState.xnPort.value();
                msg->ppid = sctp::PayloadProtocolId::XNAP;
                msg->associatedTask = this;
                msg->maxTxStreams = 10000;
                msg->maxRxStreams = 10000;
                m_base->sctpTask->push(std::move(msg));

                m_logger->info("Adding Xn connection to gNB %d (NCI 0x%09x)", peerInfo.gnbId, peerInfo.nci);
            }
            else
            {
                // already exists
                //  TODO: Update connection parameters if needed
            }
        }
    }

} // updateXnConnections

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
            xnHandoverRequestSource(msg->ueId, msg->targetNci, msg->isCho, std::move(contexts));
        }
        // if still not ready after the retry threshold, drop the request
        else if (msg->retries >= DEFERRED_MAX_RETRIES)
        {
            m_logger->err("UE[%ld] Dropping deferred HandoverRequired after %d retries", msg->ueId, msg->retries);
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
