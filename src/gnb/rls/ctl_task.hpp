//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include "udp_task.hpp"

#include <map>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

#include <gnb/nts.hpp>
#include <gnb/types.hpp>
#include <utils/nts.hpp>

namespace nr::gnb
{

class RlsControlTask : public NtsTask
{
  private:
    std::unique_ptr<Logger> m_logger;
    uint64_t m_sti;
    int64_t m_cellId;
    NtsTask *m_mainTask;
    RlsUdpTask *m_udpTask;

    // keyed by (ueId, pduId, radioBearer) to avoid collisions across UEs
    //std::map<std::tuple<int64_t, uint32_t, uint8_t>, rls::PduInfo> m_pduMap;
    //std::unordered_map<int64_t, std::vector<std::pair<uint32_t, uint8_t>>> m_pendingAck;

    // UE Contexts, keyed by UE ID
    std::map<int64_t, RlsUeContext> m_ueCtx;
    // SN Status may race target bearer configuration.  Unknown DRBs are held
    // here and retried after every RADIO_BEARER_UPDATE for the UE.
    std::unordered_map<int64_t, std::vector<DrbSnStatus>> m_deferredSnStatus;
    // Handlers (RLS task thread) hold unique_lock; copyUeContext (caller thread) holds shared_lock.
    mutable std::shared_mutex m_ueCtxMutex;

    int m_timerPeriodAckControl;
    int m_timerPeriodAckSend;

  public:
    explicit RlsControlTask(TaskBase *base, uint64_t sti);
    ~RlsControlTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  public:
    void initialize(NtsTask *mainTask, RlsUdpTask *udpTask);

    // Thread-safe copy of a single UE's RLS context for use by other tasks (RRC, Xn).
    // Returns nullopt if no context exists for ueId.
    std::optional<RlsUeContext> copyUeContext(int64_t ueId) const;

  private:
    void createRlsUeContext(int64_t ueId);
    void deleteRlsUeContext(int64_t ueId);
    RlsUeContext *getRlsUeContext(int64_t ueId);
    //void updateUeBearer(int64_t ueId, uint8_t radioBearer, uint32_t pduId);
    void getBearerFromSdap(RlsUeContext &ctx, int psi, int qfi, uint8_t *radioBearer, uint32_t *pduId);

    void handleSignalDetected(int64_t ueId);
    void handleSignalLost(int64_t ueId);
    void handleRlsMessage(NmGnbRlsToRls &w);
    void handleDownlinkRrcDelivery(int64_t ueId, rrc::RrcChannel channel, OctetString &&data);
    void handleDownlinkDataDelivery(int64_t ueId, int psi, int qfi, OctetString &&data);
    void handleRadioBearerUpdate(int64_t ueId, std::unique_ptr<RadioBearerUpdate> rbUpdate, std::unique_ptr<SdapUpdate> sdapUpdate);
    void handleApplyDrbSnStatus(int64_t ueId, const std::vector<DrbSnStatus> &status);
    void handleRemoveUeContext(int64_t ueId);
    void applyOrDeferDrbSnStatus(int64_t ueId, RlsUeContext &ctx, const std::vector<DrbSnStatus> &status);
    void onAckControlTimerExpired();
    void onAckSendTimerExpired();
};

} // namespace nr::gnb
