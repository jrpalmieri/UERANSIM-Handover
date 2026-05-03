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
    std::unordered_map<int, RrcUeContext *> m_ueCtx;

    // Pending Handover Contexts, indexed by UE ID
    std::unordered_map<int, RRCHandoverPending *> m_handoversPending;

    std::unordered_map<int, int> m_tidCountersByUe;

    bool m_isBarred = true;
    bool m_cellReserved = false;
    UacAiBarringSet m_aiBarringSet = {};
    bool m_intraFreqReselectAllowed = true;

    // gnb location as PV in ECEF coordinates
    //PositionVelocity m_truePositionVelocity{};

    std::unordered_map<int, SatellitePositionVelocityEntry> m_satellitePvByPci{};

    // Cache of PCI values for neighboring satellites, used for coarse filtering of nearby satellites.
    // TODO(satellite-neighbor-integration): When satellite tracking discovers a new neighbor gNB
    //   (i.e., a satellite with a valid TLE that is within range), call
    //   m_base->neighbors->upsert(entry) to add it to the runtime neighbor store so it becomes
    //   available for handover decisions without a manual CLI update.
    std::vector<int> m_satNeighborhoodCache{};

    // Cache of PCIs for gNBs that are within communication range of the UE,
    //   used for deciding which neighbors to include in SIB19.
    // TODO(satellite-neighbor-integration): Entries added here that are not yet in the runtime
    //   neighbor store should trigger a neighbor upsert (see m_satNeighborhoodCache note above).
    std::vector<int> m_sib19RangeCache{};

    friend class GnbCmdHandler;

  public:
    explicit GnbRrcTask(TaskBase *base);
    ~GnbRrcTask() override = default;

    std::vector<HandoverMeasurementIdentity> getHandoverMeasurementIdentities(int ueId) const;
    OctetString getHandoverMeasConfigRrcReconfiguration(int ueId) const;
    int64_t buildHandoverCommandForTransfer(int ueId, int targetPci, int newCrnti, int t304Ms,
                        OctetString &rrcContainer);
    bool addPendingHandover(int ueId, const HandoverPreparationInfo &handoverPrep,
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

    int getNextTid(int ueId);
    int allocateCrnti() const;
    RrcUeContext* findCtxByCrnti(int cRnti);
    RrcUeContext* findCtxByUeId(int ueId);

    /* Handlers for RRC-NAS - handlers.cpp */

    void handleUplinkRrc(int ueId, int cRnti, rrc::RrcChannel channel, const OctetString &rrcPdu);
    void handleDownlinkNasDelivery(int ueId, const OctetString &nasPdu);
    void deliverUplinkNas(int ueId, OctetString &&nasPdu);
    void releaseConnection(int ueId);
    void handleRadioLinkFailure(int ueId);
    void handlePaging(const asn::Unique<ASN_NGAP_FiveG_S_TMSI> &tmsi,
                      const asn::Unique<ASN_NGAP_TAIListForPaging> &taiList);

    void receiveUplinkInformationTransfer(int ueId, const ASN_RRC_ULInformationTransfer &msg);

    /* RRC channel send message to UE - channel.cpp */

    void sendRrcMessage(ASN_RRC_BCCH_BCH_Message *msg);
    void sendRrcMessage(ASN_RRC_BCCH_DL_SCH_Message *msg);
    void sendRrcMessage(rrc::RrcChannel channel, OctetString &&pdu);
    void sendRrcMessage(int ueId, ASN_RRC_DL_CCCH_Message *msg);
    void sendRrcMessage(int ueId, ASN_RRC_DL_DCCH_Message *msg);
    void sendRrcMessage(ASN_RRC_PCCH_Message *msg);

    /* RRC channel receive message from UE - channel.cpp */

    void receiveRrcMessage(int ueId, ASN_RRC_BCCH_BCH_Message *msg);
    void receiveRrcMessage(int ueId, ASN_RRC_UL_CCCH_Message *msg);
    void receiveRrcMessage(int ueId, ASN_RRC_UL_CCCH1_Message *msg);
    void receiveRrcMessage(int ueId, int cRnti, ASN_RRC_UL_DCCH_Message *msg);

    /* Satellite TLE tracking and neighborhood cache - sat_calcs.cpp */

    void roughNeighborhoodSats();
    void satHandoverTriggerCalc(nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams, const int ownPci, RrcUeContext *ue);

    /* System Information Broadcast related - broadcast.cpp */

    void onBroadcastTimerExpired();
    void triggerSysInfoBroadcast();
    void triggerSib19Broadcast();
    void onUpdateLocationTimerExpired();

    /* Service Access Point - sap.cpp */

    void handleRlsSapMessage(NmGnbRlsToRrc &msg);

    /* UE Management - ues.cpp */

    RrcUeContext *createUe(int ueId, int crnti);
    RrcUeContext *tryFindUeByCrnti(int crnti);
    RrcUeContext *tryFindUeByUeId(int ueId);

    /* Connection Control - connection.cpp */

    void receiveRrcSetupRequest(int ueId, const ASN_RRC_RRCSetupRequest &msg);
    void receiveRrcSetupComplete(int ueId, const ASN_RRC_RRCSetupComplete &msg);

    /* Handover - handover.cpp */

    void receiveRrcReconfigurationComplete(int ueId, int cRnti, const ASN_RRC_RRCReconfigurationComplete &msg);
    void receiveMeasurementReport(int ueId, int cRnti, const ASN_RRC_MeasurementReport &msg);
    void sendUeHandoverMessage(int ueId, int targetPci, int newCrnti, int t304Ms);
    void handleHandoverComplete(int ueId);
    void sendMeasConfig(int ueId, bool forceResend = false);
    void evaluateHandoverDecision(int ueId, int measId);
    void processConditionalHandover(int ueId,
                    const nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams,
                    int choProfileIdx);
    void handleNgapHandoverCommand(int ueId, const OctetString &rrcContainer, bool hoForChoPreparation);
    void handleNgapHandoverFailure(int ueId, int targetPci, bool hoForChoPreparation);
    void handoverContextRelease(int ueId);
    void completeConditionalHandover(RrcUeContext *ue, const OctetString &rrcContainer);
    void calculateTriggerConditions(nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams,
                    int ownPci,
                    RrcUeContext *ue);
    std::vector<ScoredNeighbor> prioritizeNeighbors(
      const std::vector<GnbNeighborConfig> &neighborList,
      int servingPci,
      const nr::sat::EcefPosition &ueEcef,
      int tExitSec);
    void clearChoPendingState(RrcUeContext *ue);
    std::vector<long> createMeasConfig(
      ASN_RRC_MeasConfig *&mc,
      RrcUeContext *ue,
      std::vector<nr::rrc::common::ReportConfigEvent> selectedEvents,
      const nr::rrc::common::DynamicEventTriggerParams &dynTriggerParams,
      long choProfileId
      ); 

};

} // namespace nr::gnb
