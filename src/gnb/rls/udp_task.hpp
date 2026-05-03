//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <gnb/types.hpp>
#include <lib/rls/rls_pdu.hpp>
#include <lib/udp/server.hpp>
#include <utils/nts.hpp>

namespace nr::gnb
{

class RlsUdpTask : public NtsTask
{
  private:
    struct UeInfo
    {
      uint64_t sti{};
      int cRnti{};
      InetAddress address;
      int64_t lastSeen{};
      GeoPosition lastPos{};       // last UE position reported in a heartbeat
      bool        hasPosData{false}; // true once at least one heartbeat with position has been received
    };

  private:
    std::unique_ptr<Logger> m_logger;
    TaskBase *m_base;
    udp::UdpServer *m_server;
    NtsTask *m_ctlTask;
    uint64_t m_sti;
    uint32_t m_cellId;
    Vector3 m_phyLocation;
    int64_t m_lastLoop;
    std::mutex m_ueMutex;
    std::unordered_map<uint64_t, int> m_stiToUe;
    std::unordered_map<int, UeInfo> m_ueMap;

    // map from C-RNTI to UE ID, used for quickly finding UE context by C-RNTI 
    //  when receiving RRC messages from UEs
    //std::unordered_map<int, int> m_crntiToUeId;

    // map from UE ID to C-RNTI, used for quickly finding UE context by UE ID
    //  when receiving messages from other tasks (e.g. RLS) that only include UE ID but not C-RNTI
    //std::unordered_map<int, int> m_ueIdToCrnti;

    int m_newIdCounter;
    //int m_fixedRsrp;
    int m_loopCounter;
    int m_receiveTimeout;
    int m_heartbeatThreshold;

  public:
    explicit RlsUdpTask(TaskBase *base, uint64_t sti,
                        Vector3 phyLocation);
    ~RlsUdpTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    void receiveRlsPdu(const InetAddress &addr,
                       std::unique_ptr<rls::RlsMessage> &&msg);
    void sendRlsPdu(const InetAddress &addr,
                    const rls::RlsMessage &msg);
    void heartbeatCycle(int64_t time);
    int computeDbm(const GeoPosition &uePos, int ueId);
//    void handleSatPositionUpdate(const rls::RlsMessage &msg);
//    void handleRsrpUpdate(const rls::RlsMessage &msg);

  public:
    void initialize(NtsTask *ctlTask);
    void send(int ueId, const rls::RlsMessage &msg);
};

} // namespace nr::gnb
