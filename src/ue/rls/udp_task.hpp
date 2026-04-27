//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <lib/rls/rls_pdu.hpp>
#include <lib/udp/server.hpp>
#include <ue/types.hpp>
#include <utils/nts.hpp>

namespace nr::ue
{

class RlsUdpTask : public NtsTask
{
  private:
    struct CellInfo
    {
        InetAddress address;
        int64_t lastSeen{};
        int dbm{};
        int cellId{};
    };

  private:

    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;
    // UDP Socket server used to send/receive RLS messages
    udp::UdpServer *m_server;
    NtsTask *m_ctlTask;
    RlsSharedContext* m_shCtx;
    std::vector<InetAddress> m_searchSpace;
    // map of all known gnb cells, indexed by STI value
    std::unordered_map<uint64_t, CellInfo> m_cells;
    // map of STI values, indexed by cellId (which is UE-local)
    std::unordered_map<int, uint64_t> m_cellIdToSti;
    int64_t m_lastLoop;
    // used to generate a unique UE-local cellId for each new cell that sends a heartbeat ACK
    int m_cellIdCounter;
    int m_loopCounter;
    int m_receiveTimeout;
    int m_heartbeatThreshold;

    std::unordered_map<u_int64_t, int> cellIdBySti;  // map of cellId indexed by sti value

    bool handoverEnabled;

    friend class UeCmdHandler;

  public:
    explicit RlsUdpTask(TaskBase *base, RlsSharedContext* shCtx, const std::vector<std::string> &searchSpace);
    ~RlsUdpTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    void sendRlsPdu(const InetAddress &addr, const rls::RlsMessage &msg);
    void receiveRlsPdu(const InetAddress &addr, std::unique_ptr<rls::RlsMessage> &&msg);
    //void onSignalChangeOrLost(int cellId);
    void heartbeatCycle(uint64_t time);
    void updateMeasurements(const int dbm, const int cellId);
    void onHeartbeatFailure(int cellId);

  public:
    void initialize(NtsTask *ctlTask);
    void send(int cellId, const rls::RlsMessage &msg);
    int addSearchSpaceIps(const std::vector<std::string> &ipAddresses);
    int removeSearchSpaceIps(const std::vector<std::string> &ipAddresses);
    std::vector<std::string> listSearchSpaceIps() const;
};

} // namespace nr::ue
