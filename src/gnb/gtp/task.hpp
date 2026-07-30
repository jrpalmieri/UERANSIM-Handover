//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include "utils.hpp"

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gnb/nts.hpp>
#include <lib/udp/server_task.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>

namespace nr::gnb
{

class GtpTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;

    udp::UdpServerTask *m_udpServer;
    std::unordered_map<int64_t, std::unique_ptr<GtpUeContext>> m_ueContexts;
    std::unique_ptr<IRateLimiter> m_rateLimiter;
    //std::unordered_map<uint64_t, std::unique_ptr<PduSessionResource>> m_pduSessions;
    PduSessionTree m_sessionTree;
    std::unordered_map<UeSessionId, GtpTunnel, UeSessionIdHash> m_forwardingTunnels;

    // Target side of Xn DL data forwarding: TEIDs this gNB advertised in a
    // HandoverRequestAcknowledge, mapped to the UE session the forwarded
    // packets belong to.  Checked before the session tree on GTP-U receive.
    std::unordered_map<uint32_t, UeSessionId> m_incomingForwardingTeids;

    friend class GnbCmdHandler;

  public:
    explicit GtpTask(TaskBase *base);
    ~GtpTask() override = default;

    bool getUeContext(int64_t ueId, std::optional<GtpUeContext> &out);
    PduSessionResource *getPduSession(int64_t ueId, int psi);
    bool getPduSessions(int64_t ueId, std::vector<PduSessionResource> &out);

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    void handleUdpReceive(const udp::NwUdpServerReceive &msg);
    void handleUeContextUpdate(const GtpUeContextUpdate &msg);
    void handleSessionCreate(std::unique_ptr<PduSessionResource> session);
    void handleSessionRelease(int64_t ueId, int psi);
    void handleUeContextDelete(int64_t ueId);
    void handleUplinkData(int64_t ueId, int psi, int qfi, OctetString &&data);
    void handleForwardingTunnelSetup(int64_t ueId, int psi, GtpTunnel &&tunnel);
    void handleForwardingTeidRegister(int64_t ueId, int psi, uint32_t teid);
    void handleUlTunnelUpdate(int64_t ueId, int psi, GtpTunnel &&tunnel);

    void updateAmbrForUe(int64_t ueId);
    void updateAmbrForSession(int64_t ueId, int psi);
};

} // namespace nr::gnb
