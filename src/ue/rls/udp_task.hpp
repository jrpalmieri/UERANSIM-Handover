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

inline static int64_t nciFromSti(uint64_t sti)
{
    // for gnb transmissions, ST = NCI (36 bits) + Randomizer (10 bits)
    // to get NCI, just shift the STI right by 10 bits
    return static_cast<int64_t>(sti >> 10);
}

class RlsCellMap
{
  public:
    RlsCellMap();
    RlsCellMap(int64_t last_seen_threshold_ms);

  private:
    std::vector<uint64_t> stis;
    std::vector<int64_t> ncis;
    std::vector<int64_t> last_seen;
    std::vector<InetAddress> addresses;

    int64_t m_lastSeenThreshold;

  public:
    void upsertCell(const uint64_t sti, const int64_t last_seen, const InetAddress &address);
    bool removeCell(const uint64_t sti);
    bool isCellInRange(const uint64_t sti) const;
    std::vector<uint64_t> checkLastSeen(int64_t time) const;
    void setLastSeenThreshold(int64_t threshold) { m_lastSeenThreshold = threshold; }
    bool getAddressNci(int64_t nci, InetAddress &addr);

};

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
    int64_t m_lastLoop;
    int m_loopCounter;
    int m_heartbeatThreshold;
    int m_receiveTimeout;


    // map of all known gnb cells, indexed by STI value
    //std::unordered_map<uint64_t, CellInfo> m_cells;
    // map of STI values, indexed by cellId (which is UE-local)
    //std::unordered_map<int, uint64_t> m_cellIdToSti;
    //std::unordered_map<u_int64_t, int> cellIdBySti;  // map of cellId indexed by sti value

    // tracker for all cells that have sent a heartbeat ACK, used to determine which cells are in range and to trigger heartbeat timeouts
    RlsCellMap m_cellMap;

    // list of all gNB IPs to send heartbeats
    //  (can be altered dynamically through CLI commands)
    std::vector<InetAddress> m_searchSpace;

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
    void receiveRlsPdu(const InetAddress &addr, std::unique_ptr<rls::RlsMessage> &&msg, int64_t time);
    //void onSignalChangeOrLost(int cellId);
    void heartbeatCycle(uint64_t time);
    //void updateMeasurements(const int dbm, const int cellId);
    //void onHeartbeatFailure(int cellId);


  public:
    void initialize(NtsTask *ctlTask);
    void send(int64_t nci, const rls::RlsMessage &msg);
    int addSearchSpaceIps(const std::vector<std::string> &ipAddresses);
    int removeSearchSpaceIps(const std::vector<std::string> &ipAddresses);
    std::vector<std::string> listSearchSpaceIps() const;
};

} // namespace nr::ue
