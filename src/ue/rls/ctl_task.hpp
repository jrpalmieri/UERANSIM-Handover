//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include "udp_task.hpp"

#include <unordered_map>
#include <vector>

#include <lib/rrc/rrc.hpp>
#include <ue/nts.hpp>
#include <ue/types.hpp>
#include <utils/nts.hpp>

namespace nr::ue
{

  class RlsControlTask : public NtsTask
{
  private:
    std::unique_ptr<Logger> m_logger;
    // Shared RLS context among the main, control and UDP tasks
    RlsSharedContext *m_shCtx;
    // NCI of current serving cell
    int64_t m_servingCell;
    // ptr to the main RLS task
    NtsTask *m_mainTask;
    // ptr to the UDP task, used to send messages to the gnb via UDP
    RlsUdpTask *m_udpTask;

    // Bearer Sequence Number Trackers
    std::vector<RadioBearer> radioBearers{};

    // map of all sent PDUs that are being tracked for acknowledgment, indexed by a unit64: (radioBearer<<32)|pduId
    std::unordered_map<uint64_t, rls::PduInfo> m_pduMap;

    // vector of all pending ACKs.  This is used to trigger retransmissions when ACK control timer expires.
    std::vector<int64_t> m_pendingAck;

    // SDAP mappings
    std::vector<SdapMapping> m_sdapMappings;

    int m_timerPeriodAckControl;
    int m_timerPeriodAckSend;

  public:
    explicit RlsControlTask(TaskBase *base, RlsSharedContext *shCtx);
    ~RlsControlTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  public:
    void initialize(NtsTask *mainTask, RlsUdpTask *udpTask);

  private:
    void handleRlsMessage(int64_t cellId, rls::RlsMessage &msg);
    void handleSignalChange(int64_t cellId, int dbm);
    void handleUplinkRrcDelivery(int64_t cellId, rrc::RrcChannel channel, OctetString &&data);
    void handleUplinkDataDelivery(int pduSessionId, OctetString &&data);
    void onAckControlTimerExpired();
    void onAckSendTimerExpired();

    void sdapMapping(int pduSessionId, OctetString &data, uint8_t *radioBearer, uint32_t *pduId, int *qfi);
    void selectSignalingRadioBearer(rrc::RrcChannel channel, uint8_t &radioBearer, uint32_t &pduId);
    void createRadioBearer(uint8_t bearerId);
    void handleRadioBearerUpdate(std::unique_ptr<RadioBearerUpdate> rbUpdate, std::unique_ptr<SdapUpdate> sdapUpdate);
};

} // namespace nr::ue