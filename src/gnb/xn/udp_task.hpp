//
// Xn UDP transport task.
//

#pragma once

#include <memory>

#include <gnb/types.hpp>
#include <lib/udp/server.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>
#include <utils/octet_string.hpp>

namespace nr::gnb
{

class XnUdpTask : public NtsTask
{
  private:
    TaskBase *m_base;
    NtsTask *m_targetTask;
    std::unique_ptr<Logger> m_logger;
    udp::UdpServer *m_server;

  public:
    XnUdpTask(TaskBase *base, NtsTask *targetTask);
    ~XnUdpTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  public:
    void sendPacket(const InetAddress &address, const OctetString &packet) const;
};

} // namespace nr::gnb
