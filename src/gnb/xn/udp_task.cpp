//
// Xn UDP transport task.
//

#include "udp_task.hpp"

#include <lib/udp/server_task.hpp>
#include <utils/libc_error.hpp>

static constexpr int BUFFER_SIZE = 4096;
static constexpr int RECEIVE_TIMEOUT_MS = 100;

namespace nr::gnb
{

XnUdpTask::XnUdpTask(TaskBase *base, NtsTask *targetTask)
    : m_base(base), m_targetTask(targetTask), m_server(nullptr)
{
    m_logger = m_base->logBase->makeUniqueLogger("xn-udp");

    try
    {
        m_server = new udp::UdpServer(m_base->config->handover.xn.bindAddress, m_base->config->handover.xn.bindPort);
    }
    catch (const LibError &e)
    {
        m_logger->err("Xn UDP bind failure [%s]", e.what());
        quit();
    }
}

void XnUdpTask::onStart()
{
}

void XnUdpTask::onLoop()
{
    if (m_server == nullptr)
        return;

    uint8_t buffer[BUFFER_SIZE];
    InetAddress peerAddress{};

    int size = m_server->Receive(buffer, BUFFER_SIZE, RECEIVE_TIMEOUT_MS, peerAddress);
    if (size <= 0)
        return;

    auto packet = OctetString::FromArray(buffer, static_cast<size_t>(size));
    m_targetTask->push(std::make_unique<udp::NwUdpServerReceive>(std::move(packet), peerAddress));
}

void XnUdpTask::onQuit()
{
    delete m_server;
    m_server = nullptr;
}

void XnUdpTask::sendPacket(const InetAddress &address, const OctetString &packet) const
{
    if (m_server == nullptr)
        return;

    m_server->Send(address, packet.data(), static_cast<size_t>(packet.length()));
}

} // namespace nr::gnb
