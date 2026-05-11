//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gnb/types.hpp>
#include <gnb/nts.hpp>
#include <lib/sat/sat_calc.hpp>
#include <lib/rrc/common/asn_fwd.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>

#include <asn/rrc/ASN_RRC_MeasConfig.h>

namespace nr::gnb
{

class NgapTask;

class GnbRrcTask : public NtsTask
{
  private:
    TaskBase *m_base;
    GnbConfig *m_config;
    std::unique_ptr<Logger> m_logger;

    // UE RRC Contexts, indexed by UE ID
    std::unordered_map<int64_t, RrcUeContext *> m_ueCtx;

    // Pending Handover Contexts, indexed by UE ID
    std::unordered_map<int64_t, RRCHandoverPending *> m_handoversPending;

    std::unordered_map<int64_t, int> m_tidCountersByUe;

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

    std::vector<HandoverMeasurementIdentity> getHandoverMeasurementIdentities(int64_t ueId) const;
    OctetString getHandoverMeasConfigRrcReconfiguration(int64_t ueId) const;
    int64_t buildHandoverCommandForTransfer(int64_t ueId, int64_t targetNci, int newCrnti, int t304Ms,
                        OctetString &rrcContainer);
    bool addPendingHandover(int64_t ueId, const HandoverPreparationInfo &handoverPrep,
                OctetString &rrcContainer);
    void setTrueGeoPosition(const GeoPosition &value);
    GeoPosition getTrueGeoPosition() const;
    void setTruePositionVelocity(const PositionVelocity &value);
    PositionVelocity getTruePositionVelocity() const;
    void upsertSatellitePositionVelocity(const SatellitePositionVelocityEntry &value);
    void upsertSatTles(const std::vector<nr::sat::SatTleEntry> &entries);

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    
    void onUpdateGnbStatusTimerExpired();

  /* Management - management.cpp */

    int getNextTid(int64_t ueId);
    int allocateCrnti() const;
    RrcUeContext* findCtxByCrnti(int cRnti);
    RrcUeContext* findCtxByUeId(int64_t ueId);

    /* Handlers for RRC-NAS - handlers.cpp */

    void handleUplinkRrc(int64_t ueId, int cRnti, rrc::RrcChannel channel, const OctetString &rrcPdu);
    void handleDownlinkNasDelivery(int64_t ueId, const OctetString &nasPdu);
    void deliverUplinkNas(int64_t ueId, OctetString &&nasPdu);
    void releaseConnection(int64_t ueId);
    void handleRadioLinkFailure(int64_t ueId);
    void handlePaging(const asn::Unique<ASN_NGAP_FiveG_S_TMSI> &tmsi,
                      const asn::Unique<ASN_NGAP_TAIListForPaging> &taiList);

    void receiveUplinkInformationTransfer(int64_t ueId, const ASN_RRC_ULInformationTransfer &msg);

    /* RRC channel send message to UE - channel.cpp */

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
    RrcUeContext *tryFindUeByCrnti(int crnti);
    RrcUeContext *tryFindUeByUeId(int64_t ueId);

    /* Connection Control - connection.cpp */

    void receiveRrcSetupRequest(int64_t ueId, const ASN_RRC_RRCSetupRequest &msg);
    void receiveRrcSetupComplete(int64_t ueId, const ASN_RRC_RRCSetupComplete &msg);

    /* Handover - handover.cpp */

    void receiveRrcReconfigurationComplete(int64_t ueId, int cRnti, const ASN_RRC_RRCReconfigurationComplete &msg);
    void receiveMeasurementReport(int64_t ueId, int cRnti, const ASN_RRC_MeasurementReport &msg);
    void sendUeHandoverMessage(int64_t ueId, int64_t targetNci, int newCrnti, int t304Ms);
    void handleHandoverComplete(int64_t ueId);
    void sendMeasConfig(int64_t ueId, bool forceResend = false);
    void evaluateHandoverDecision(int64_t ueId, int measId);
    void processConditionalHandover(int64_t ueId,
                    const nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams,
                    int choProfileIdx);
    void handleNgapHandoverCommand(int64_t ueId, const OctetString &rrcContainer, bool hoForChoPreparation);
    void handleNgapHandoverFailure(int64_t ueId, int64_t targetNci, bool hoForChoPreparation);
    void handoverContextRelease(int64_t ueId);
    void completeConditionalHandover(RrcUeContext *ue, const OctetString &rrcContainer);
    std::vector<ScoredNeighbor> prioritizeNeighbors(
      const std::vector<GnbNeighborConfig> &neighborList,
      int64_t servingNci,
      const nr::sat::EcefPosition &ueEcef,
      int tExitSec);
    void clearChoPendingState(RrcUeContext *ue, int profileIdx);
    std::vector<long> createMeasConfig(
      ASN_RRC_MeasConfig *&mc,
      RrcUeContext *ue,
      std::vector<std::pair<nr::rrc::common::ReportConfigEvent, int>> taggedEvents
      );

};

} // namespace nr::gnb
