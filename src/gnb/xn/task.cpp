//
// Xn control-plane task.
//

#include "task.hpp"

#include <gnb/nts.hpp>
#include <lib/udp/server_task.hpp>

namespace nr::gnb
{

XnTask::XnTask(TaskBase *base)
    : m_base(base),
      m_config(base->config),
      m_udpTask(nullptr),
      m_transactionCounter(1)
{
    m_logger = m_base->logBase->makeUniqueLogger("xn");

    if (m_config->handover.xn.enabled)
        m_udpTask = new XnUdpTask(base, this);
}

void XnTask::onStart()
{
    if (m_udpTask)
    {
        m_logger->info("Xn enabled. Binding UDP %s:%u",
                       m_config->handover.xn.bindAddress.c_str(),
                       static_cast<unsigned>(m_config->handover.xn.bindPort));
        m_udpTask->start();
    }
    else
    {
        m_logger->info("Xn is disabled");
    }
}

void XnTask::onLoop()
{
    auto message = take();
    if (!message)
        return;

    switch (message->msgType)
    {
    case NtsMessageType::GNB_RRC_TO_XN:
        handleRrcMessage(dynamic_cast<NmGnbRrcToXn &>(*message));
        break;
    case NtsMessageType::GNB_NGAP_TO_XN:
        handleNgapMessage(dynamic_cast<NmGnbNgapToXn &>(*message));
        break;
    case NtsMessageType::UDP_SERVER_RECEIVE:
        handleUdpPacket(dynamic_cast<udp::NwUdpServerReceive &>(*message));
        break;
    default:
        m_logger->unhandledNts(*message);
        break;
    }
}

void XnTask::onQuit()
{
    if (m_udpTask)
    {
        m_udpTask->quit();
        delete m_udpTask;
        m_udpTask = nullptr;
    }
}

std::optional<InetAddress> XnTask::resolveNeighborXnEndpoint(int targetPci) const
{
    const auto *neighbor = m_config->findNeighborByPci(targetPci);
    if (neighbor == nullptr)
        return std::nullopt;

    auto address = neighbor->xnAddress ? *neighbor->xnAddress : neighbor->ipAddress;
    auto port = neighbor->xnPort ? *neighbor->xnPort : m_config->handover.xn.bindPort;

    return InetAddress{address, port};
}

void XnTask::handleRrcMessage(NmGnbRrcToXn &message)
{
    switch (message.present)
    {
    case NmGnbRrcToXn::HANDOVER_REQUIRED_XN: {
        auto endpoint = resolveNeighborXnEndpoint(message.hoTargetPci);
        if (!endpoint)
        {
            m_logger->warn("UE[%d] Xn handover request dropped, targetPCI=%d not found",
                           message.ueId, message.hoTargetPci);
            return;
        }

        xn::XnMessage xnMessage{};
        xnMessage.type = xn::XnMessageType::HandoverRequest;
        xnMessage.transactionId = m_transactionCounter++;
        xnMessage.ueId = message.ueId;
        xnMessage.sourcePci = m_base->config->getCellId();
        xnMessage.targetPci = message.hoTargetPci;
        xnMessage.causeCode = static_cast<int>(message.hoCause);

        if (m_udpTask)
        {
            auto packet = xn::EncodeXnMessage(xnMessage);
            m_udpTask->sendPacket(*endpoint, packet);
        }

        m_logger->info("Xn HandoverRequest sent UE[%d] -> ipV%d:%u targetPCI=%d tx=%u",
                       message.ueId,
                       endpoint->getIpVersion(),
                       static_cast<unsigned>(endpoint->getPort()),
                       message.hoTargetPci,
                       xnMessage.transactionId);
        break;
    }
    case NmGnbRrcToXn::HANDOVER_COMPLETE_XN:
        m_logger->info("UE[%d] Xn HandoverComplete notification received", message.ueId);
        break;
    }
}

void XnTask::handleNgapMessage(NmGnbNgapToXn &message)
{
    switch (message.present)
    {
    case NmGnbNgapToXn::PATH_SWITCH_ACK:
        m_logger->info("UE[%d] Xn observed PathSwitch ACK success=%s",
                       message.ueId, message.success ? "true" : "false");
        break;
    }
}

void XnTask::handleUdpPacket(udp::NwUdpServerReceive &packetMsg)
{
    auto decoded = xn::DecodeXnMessage(packetMsg.packet);
    if (!decoded)
    {
        m_logger->warn("Dropping malformed Xn packet from ipV%d:%u",
                       packetMsg.fromAddress.getIpVersion(),
                       static_cast<unsigned>(packetMsg.fromAddress.getPort()));
        return;
    }

    m_logger->info("Received Xn %s from ipV%d:%u (ue=%d tx=%u)",
                   xn::ToString(decoded->type),
                   packetMsg.fromAddress.getIpVersion(),
                   static_cast<unsigned>(packetMsg.fromAddress.getPort()),
                   decoded->ueId,
                   decoded->transactionId);
}

} // namespace nr::gnb
