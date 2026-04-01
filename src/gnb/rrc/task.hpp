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

#include <gnb/nts.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>

extern "C"
{
    struct ASN_RRC_BCCH_BCH_Message;
    struct ASN_RRC_BCCH_DL_SCH_Message;
    struct ASN_RRC_DL_CCCH_Message;
    struct ASN_RRC_DL_DCCH_Message;
    struct ASN_RRC_PCCH_Message;
    struct ASN_RRC_UL_CCCH_Message;
    struct ASN_RRC_UL_CCCH1_Message;
    struct ASN_RRC_UL_DCCH_Message;

    struct ASN_RRC_RRCSetupRequest;
    struct ASN_RRC_RRCSetupComplete;
    struct ASN_RRC_RRCReconfigurationComplete;
    struct ASN_RRC_MeasurementReport;
    struct ASN_RRC_ULInformationTransfer;
}

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

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    
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
    void sendRrcMessage(int ueId, ASN_RRC_DL_CCCH_Message *msg);
    void sendRrcMessage(int ueId, ASN_RRC_DL_DCCH_Message *msg);
    void sendRrcMessage(ASN_RRC_PCCH_Message *msg);

    /* RRC channel receive message from UE - channel.cpp */

    void receiveRrcMessage(int ueId, ASN_RRC_BCCH_BCH_Message *msg);
    void receiveRrcMessage(int ueId, ASN_RRC_UL_CCCH_Message *msg);
    void receiveRrcMessage(int ueId, ASN_RRC_UL_CCCH1_Message *msg);
    void receiveRrcMessage(int ueId, int cRnti, ASN_RRC_UL_DCCH_Message *msg);

    /* System Information Broadcast related - broadcast.cpp */
    
    void onBroadcastTimerExpired();
    void triggerSysInfoBroadcast();

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
    void sendHandoverCommand(int ueId, int targetPci, int newCrnti, int t304Ms);
    void handleHandoverComplete(int ueId);
    void sendMeasConfig(int ueId, bool forceResend = false);
    void evaluateHandoverDecision(int ueId, const std::string &eventType);
    void handleNgapHandoverCommand(int ueId, const OctetString &rrcContainer);
    void handoverContextRelease(int ueId);

};

} // namespace nr::gnb
