//
// Xn control-plane task.
//

#pragma once

#include "protocol.hpp"
#include "udp_task.hpp"

#include <memory>
#include <optional>

#include <gnb/nts.hpp>
#include <gnb/types.hpp>
#include <lib/udp/server_task.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>

namespace nr::gnb
{

class XnTask : public NtsTask
{
  private:
    TaskBase *m_base;
    GnbConfig *m_config;
    std::unique_ptr<Logger> m_logger;
    XnUdpTask *m_udpTask;
    uint32_t m_transactionCounter;

  public:
    explicit XnTask(TaskBase *base);
    ~XnTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    std::optional<InetAddress> resolveNeighborXnEndpoint(int targetPci) const;
    void handleRrcMessage(NmGnbRrcToXn &message);
    void handleNgapMessage(NmGnbNgapToXn &message);
    void handleUdpPacket(udp::NwUdpServerReceive &packetMsg);
};

} // namespace nr::gnb
