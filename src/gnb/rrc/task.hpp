//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gnb/types.hpp>
#include <gnb/nts.hpp>
#include "crnti_manager.hpp"
#include <lib/sat/sat_calc.hpp>
#include <lib/rrc/common/asn_fwd.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>

#include <asn/rrc/ASN_RRC_MeasConfig.h>
#include <asn/rrc/ASN_RRC_RadioBearerConfig.h>
#include <asn/rrc/ASN_RRC_SecurityModeComplete.h>

namespace nr::gnb
{

class NgapTask;

class GnbRrcTask : public NtsTask
{
  private:
    TaskBase *m_base;
    GnbConfig *m_config;
    std::unique_ptr<Logger> m_logger;

    // C-RNTI allocator
    CrntiManager m_crntiMgr;

    // UE RRC Contexts, indexed by UE ID
    std::unordered_map<int64_t, RrcUeContext *> m_ueCtx;

    // Guards m_ueCtx and the contexts it owns for cross-thread reads
    // (same pattern as RlsControlTask::copyUeContext).  The RRC task thread
    // holds a unique_lock for the duration of each message dispatch in
    // onLoop(); getUeContext() (called from the Xn task thread) holds a
    // shared_lock while copying.  Never call getUeContext() from the RRC
    // task thread itself — it would deadlock against the dispatch lock.
    mutable std::shared_mutex m_ueCtxMutex;

    // Pending Handover Contexts, indexed by UE ID
    std::unordered_map<int64_t, RRCHandoverPending> m_handoversPending;

    //std::unordered_map<int64_t, int> m_tidCountersByUe - moved to UE ctx;

    bool m_isBarred = true;
    bool m_cellReserved = false;
    UacAiBarringSet m_aiBarringSet = {};
    bool m_intraFreqReselectAllowed = true;

    // gnb location as PV in ECEF coordinates
    //PositionVelocity m_truePositionVelocity{};

    std::unordered_map<int64_t, SatellitePositionVelocityEntry> m_satellitePvByNci{};

    // Cache of NCI values for neighboring satellites, used for coarse filtering of nearby satellites.
    // TODO(satellite-neighbor-integration): When satellite tracking discovers a new neighbor gNB
    //   (i.e., a satellite with a valid TLE that is within range), call
    //   m_base->neighbors->upsert(entry) to add it to the runtime neighbor store so it becomes
    //   available for handover decisions without a manual CLI update.
    std::vector<int64_t> m_satNeighborhoodCache{};

    // Cache of NCIs for gNBs that are within communication range of the UE,
    //   used for deciding which neighbors to include in SIB19.
    // TODO(satellite-neighbor-integration): Entries added here that are not yet in the runtime
    //   neighbor store should trigger a neighbor upsert (see m_satNeighborhoodCache note above).
    std::vector<int64_t> m_sib19RangeCache{};

    friend class GnbCmdHandler;

  public:
    explicit GnbRrcTask(TaskBase *base);
    ~GnbRrcTask() override = default;

    // Dead declarations (no implementation — calling any of these would be a
    // link error).  Commented out rather than deleted in case the planned
    // functionality returns; see RRC_summary.md issue 14.
    //std::vector<HandoverMeasurementIdentity> getHandoverMeasurementIdentities(int64_t ueId) const;
    //OctetString getHandoverMeasConfigRrcReconfiguration(int64_t ueId) const;
    //void setTrueGeoPosition(const GeoPosition &value);
    //void setTruePositionVelocity(const PositionVelocity &value);
    //PositionVelocity getTruePositionVelocity() const;
    //void upsertSatTles(const std::vector<nr::sat::SatTleEntry> &entries);

    GeoPosition getTrueGeoPosition() const;
    void upsertSatellitePositionVelocity(const SatellitePositionVelocityEntry &value);
    bool getUeContext(int64_t ueId, std::optional<RrcUeContext> &out);

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    
    void onUpdateGnbStatusTimerExpired();

  /* Management - management.cpp */

    int allocateCrnti();
    void releaseCrnti(int crnti);
    RrcUeContext* findCtxByCrnti(int cRnti);
    RrcUeContext* findCtxByUeId(int64_t ueId);

    /* Handlers for NAS and NGAP messages - handlers.cpp */

    void handleDownlinkNasDelivery(int64_t ueId, const OctetString &nasPdu);
    void handleDownlinkNasAccept(int64_t ueId, const OctetString &nasPdu, std::unique_ptr<std::vector<PduSessionResource>> sessionList);
    void deliverUplinkNas(int64_t ueId, OctetString &&nasPdu);
    void handleRadioLinkFailure(int64_t ueId);
    void handlePaging(const asn::Unique<ASN_NGAP_FiveG_S_TMSI> &tmsi,
                      const asn::Unique<ASN_NGAP_TAIListForPaging> &taiList);
    void handleNgapSecurityInfo(int64_t ueId, std::unique_ptr<UeSecurityInfo> secInfo);
    void handleNgapPduSessionUpdate(int64_t ueId, std::unique_ptr<std::vector<PduSessionResource>> sessionList);
    void handleNgapPduSessionRelease(int64_t ueId, const std::vector<int> &releasedPsis);
    ASN_RRC_RadioBearerConfig_t* createRadioBearerConfig(int64_t ueId, std::unique_ptr<std::vector<PduSessionResource>> &sessionList);

    // Assigns (or, for an already-mapped PSI, reuses) a DRB id in the range 1..32,
    // using the UE's bearerMap as the authoritative pool.  Returns the RLS-encoded
    // bearer id (bit 6 set), or 0 if the UE has no free DRB id left.
    uint8_t allocateDrbId(RrcUeContext *ue, int psi);

    // Assigns one DRB per session (reusing existing DRBs for known PSIs), records
    // the result in ue->bearerMap, and appends the corresponding upsert entries to
    // the RLS radio-bearer / SDAP update lists.  Shared by the normal setup path
    // and the target-side handover pre-programming path.
    void assignSessionBearers(RrcUeContext *ue, const std::vector<PduSessionResource> &sessions,
                              RadioBearerUpdate &rbUpdate, SdapUpdate &sdapUpdate);
    void handleUeContextRelease(int64_t ueId, NgapCause cause);



    void receiveUplinkInformationTransfer(int64_t ueId, const ASN_RRC_ULInformationTransfer &msg);

    /* RRC channel send message to UE - channel.cpp */

    void handleUplinkRrc(int64_t ueId, int cRnti, rrc::RrcChannel channel, const OctetString &rrcPdu);
    void sendRrcMessage(ASN_RRC_BCCH_BCH_Message *msg);
    void sendRrcMessage(ASN_RRC_BCCH_DL_SCH_Message *msg);
    void sendRrcMessage(rrc::RrcChannel channel, OctetString &&pdu);
    void sendRrcMessage(int64_t ueId, ASN_RRC_DL_CCCH_Message *msg);
    void sendRrcMessage(int64_t ueId, ASN_RRC_DL_DCCH_Message *msg);
    void sendRrcMessage(ASN_RRC_PCCH_Message *msg);

    /* RRC channel receive message from UE - channel.cpp */

    void receiveRrcMessage(int64_t ueId, ASN_RRC_BCCH_BCH_Message *msg);
    void receiveRrcMessage(int64_t ueId, ASN_RRC_UL_CCCH_Message *msg);
    void receiveRrcMessage(int64_t ueId, ASN_RRC_UL_CCCH1_Message *msg);
    void receiveRrcMessage(int64_t ueId, int cRnti, ASN_RRC_UL_DCCH_Message *msg);

    /* Satellite TLE tracking and neighborhood cache - sat_calcs.cpp */

    void roughNeighborhoodSats();
    void satHandoverTriggerCalc(nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams, int64_t ownNci, RrcUeContext *ue);

    /* System Information Broadcast related - broadcast.cpp */

    void onBroadcastTimerExpired();
    void triggerSysInfoBroadcast();
    void triggerSib19Broadcast();
    void onUpdateLocationTimerExpired();

    /* Service Access Point - sap.cpp */

    void handleRlsSapMessage(NmGnbRlsToRrc &msg);

    /* UE Management - ues.cpp */

    RrcUeContext *createUe(int64_t ueId, int crnti);
    void ueContextRelease(int64_t ueId);


    /* Connection Control - connection.cpp */

    void receiveRrcSetupRequest(int64_t ueId, const ASN_RRC_RRCSetupRequest &msg);
    void receiveRrcSetupComplete(int64_t ueId, const ASN_RRC_RRCSetupComplete &msg);
    void receiveSecurityModeComplete(int64_t ueId, int cRnti, const ASN_RRC_SecurityModeComplete &msg);

    /* Measurement - measurement.cpp */
    void receiveMeasurementReport(int64_t ueId, int cRnti, const ASN_RRC_MeasurementReport &msg);
    void sendMeasConfig(int64_t ueId, bool forceResend = false);
    std::vector<long> createMeasConfig(ASN_RRC_MeasConfig *&mc, RrcUeContext *ue,
                  std::vector<std::pair<nr::rrc::common::ReportConfigEvent, int>> taggedEvents
                  );

    /* Reconfiguration - reconfiguration.cpp */
    ASN_RRC_DL_DCCH_Message* makeRrcReconfiguration(int64_t ueId);
    void receiveRrcReconfigurationComplete(int64_t ueId, int cRnti, const ASN_RRC_RRCReconfigurationComplete &msg);
    
    /* Handover - handover.cpp */
    void evaluateHandoverDecision(int64_t ueId, int measId);
    void executeBasicHandover(RrcUeContext *ue, long bestNeighNci, int servingRsrp, int bestNeighRsrp);
    void handleHandoverRequest(int sourceGnbId, uint32_t transactionId, std::unique_ptr<OctetString> rrcContainer,
                    std::unique_ptr<std::vector<PduSessionResource>> sessionList,
                    bool isCho, ERequestingTask requestingTask,
                    std::unique_ptr<XnHandoverCoreContext> xnCoreContext = nullptr,
                    std::unique_ptr<XnChoRequest> xnChoRequest = nullptr);
    // Frees a provisional (pending-handover) UE context and returns its C-RNTI
    // to the pool.  Shared by the handleHandoverRequest failure paths, the
    // Xn handover-cancel handler, and the pending-handover expiry sweep.
    void discardHandoverUeContext(RrcUeContext *ue);
    // Periodic (1 s) garbage collection of m_handoversPending entries whose
    // expireTime has passed — i.e. the UE never completed the handover.
    void sweepPendingHandovers();
    // Cleans up any partially-built target-side state and answers the handover
    // requester with a failure: XnAP HandoverPreparationFailure (Xn) or
    // NGAP HandoverFailure via HANDOVER_FAILURE_SEND (NGAP).
    void rejectHandoverRequest(ERequestingTask requestingTask, uint32_t transactionId,
                    NgapCause ngapCause, ASN_XNAP_Cause_PR xnCauseGroup, RrcUeContext *ue);
    void handleHandoverAckOrCommand(int64_t ueId, std::unique_ptr<OctetString> rrcContainer, bool isCho,
                                    ERequestingTask requestingTask, int64_t targetNci = -1);

    void sendUeHandoverMessage(int64_t ueId, int64_t targetNci, int newCrnti, int t304Ms);
    //void handleHandoverComplete(int64_t ueId);  // dead declaration — no implementation (see RRC_summary.md issue 14)
    void processConditionalHandover(int64_t ueId,
                    const nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams,
                    int choProfileIdx);
    
    void handleHandoverPreparationFailure(int64_t ueId, int64_t targetNci, bool fromChoPreparation, ERequestingTask requestingTask);
    //void handoverContextRelease(int64_t ueId);  // dead declaration — no implementation (see RRC_summary.md issue 14)
    void completeConditionalHandover(RrcUeContext *ue, std::unique_ptr<OctetString> rrcContainer);
    std::vector<ScoredNeighbor> prioritizeNeighbors(const std::vector<GnbNeighborState> &neighborList, int64_t servingNci,
                    const nr::sat::EcefPosition &ueEcef,
                    int tExitSec);
    void clearChoPendingState(RrcUeContext *ue, int profileIdx);


    OctetString createHandoverPreparationInformation(int64_t ueId);
    std::unique_ptr<OctetString> makeSourceToTargetTransparentContainerSimulated(RrcUeContext &ue, uint32_t blobSize, bool choIndication);
    std::unique_ptr<OctetString> makeTargetToSourceTransparentContainer(int64_t ueId, int newCrnti,
                                                    int t304Ms, long rrcTxId);

};

} // namespace nr::gnb
