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

#include <ue/nts.hpp>
#include <ue/types.hpp>
#include <ue/rrc/measurement.hpp>
#include <lib/rrc/common/asn_fwd.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>

#include <asn/rrc/ASN_RRC_InitialUE-Identity.h>

namespace nr::ue
{

class MeasurementProvider;

/**
 * @brief Task that performs all RRC-related operations, including:
 * - Cell selection and management
 * - RRC connection establishment, maintenance, and release
 * - NAS transport
 * - Measurement reporting
 * - Access control
 *  
 */
class UeRrcTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;

    int64_t m_startedTime;
    // current RRC state (IDLE, CONNECTED, INACTIVE)
    ERrcState m_state;
    RrcTimers m_timers;

    // Cell information from MIB/SIBs (and measurements for legacy framework), indexed by cellId
    std::unordered_map<int, UeCellDesc> m_cellDesc{};

    int64_t m_lastTimePlmnSearchFailureLogged{};

    /* Procedure related */
    ERrcLastSetupRequest m_lastSetupReq{};

    /* Handover state */

    UeMeasConfig m_measConfig{};  // Current list of measurement identities from network
    bool m_handoverInProgress{};  // flag to indicate a handover is in progress
    long m_hoTxId{};             // RRC transaction ID of the pending handover
    int  m_hoTargetPci{};        // Target cell PCI from ReconfigurationWithSync
    bool m_measurementEvalSuspended{};  // Phase 3: measurements paused during handover

    /* Conditional Handover (CHO) state */
    std::vector<ChoCandidate> m_choCandidates{};  // Active CHO candidates

    /* Establishment procedure related */
    int m_establishmentCause{};
    ASN_RRC_InitialUE_Identity_t m_initialId{};
    OctetString m_initialNasPdu{};

    friend class UeCmdHandler;

  public:
    explicit UeRrcTask(TaskBase *base);
    ~UeRrcTask() override;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    /* Handlers */
    void receivePaging(const ASN_RRC_Paging &msg);

    /* RRC Message Transmission and Receive */
    void handleDownlinkRrc(int cellId, rrc::RrcChannel channel, const OctetString &pdu);
    void sendRrcMessage(int cellId, ASN_RRC_UL_CCCH_Message *msg);
    void sendRrcMessage(int cellId, ASN_RRC_UL_CCCH1_Message *msg);
    void sendRrcMessage(ASN_RRC_UL_DCCH_Message *msg);
    void receiveRrcMessage(int cellId, ASN_RRC_BCCH_BCH_Message *msg);
    void receiveRrcMessage(int cellId, ASN_RRC_BCCH_DL_SCH_Message *msg);
    void receiveRrcMessage(int cellId, ASN_RRC_DL_CCCH_Message *msg);
    void receiveRrcMessage(ASN_RRC_DL_DCCH_Message *msg);
    void receiveRrcMessage(ASN_RRC_PCCH_Message *msg);

    /* Service Access Point */
    void handleRlsSapMessage(NmUeRlsToRrc &msg);
    void handleNasSapMessage(NmUeNasToRrc &msg);

    /* State Management */
    void triggerCycle();
    void performCycle();
    void switchState(ERrcState state);
    void onSwitchState(ERrcState oldState, ERrcState newState);

    /* Idle Mode Operations */
    void performCellSelection();
    bool lookForSuitableCell(ActiveCellInfo &cellInfo, CellSelectionReport &report);
    bool lookForAcceptableCell(ActiveCellInfo &cellInfo, CellSelectionReport &report);

    /* Cell Management */
    void handleCellSignalChange(int cellId, int dbm);
    void notifyCellDetected(int cellId, int dbm);
    void notifyCellLost(int cellId);
    bool hasSignalToCell(int cellId);
    bool isActiveCell(int cellId);
    void updateAvailablePlmns();

    /* System Information and Broadcast */
    void receiveMib(int cellId, const ASN_RRC_MIB &msg);
    void receiveSib1(int cellId, const ASN_RRC_SIB1 &msg);
    void receiveSib19(int cellId, const OctetString &pdu);

    /* NAS Transport */
    void deliverUplinkNas(uint32_t pduId, OctetString &&nasPdu);
    void receiveDownlinkInformationTransfer(const ASN_RRC_DLInformationTransfer &msg);

    /* Connection Control */
    void startConnectionEstablishment(OctetString &&nasPdu);
    void handleEstablishmentFailure();
    void receiveRrcSetup(int cellId, const ASN_RRC_RRCSetup &msg);
    void receiveRrcReject(int cellId, const ASN_RRC_RRCReject &msg);
    void receiveRrcRelease(const ASN_RRC_RRCRelease &msg);

    /* Failures */
    void declareRadioLinkFailure(rls::ERlfCause cause);
    void handleRadioLinkFailure(rls::ERlfCause cause);

    /* Access Control */
    void performUac(std::shared_ptr<LightSync<UacInput, UacOutput>> &uacCtl);

    /* Measurement framework */
    void evaluateMeasurements(int servingCellId, const std::map<int, int> &allMeas);
    void measurementReporting(int servingCellId, int servingRsrp);
    void applyMeasConfig(const UeMeasConfig &cfg);
    void resetMeasurements();
    void sendMeasurementReport(int measId, int servingCellId, int servingRsrp,
                               const std::vector<struct TriggeredNeighbor> &neighbors);
    int getServingCellRsrp(int servingCellId, const std::map<int, int> &allMeas) const;
    void receiveRrcReconfiguration(const ASN_RRC_RRCReconfiguration &msg);

    /* Handover execution */
    void performHandover(long txId, int targetPhysCellId, int newCRNTI,
                         int t304Ms, bool hasRachConfig = false);
    int  findCellByPci(int physCellId);
    void handleT304Expiry();
    void suspendMeasurements();
    void resumeMeasurements();
    void refreshSecurityKeys();

    /* Conditional Handover (CHO) */
    void handleChoConfiguration(const OctetString &pdu);
    void parseConditionalReconfiguration(const ASN_RRC_ConditionalReconfiguration *condReconfig);
    bool evaluateChoCandidates(int servingCellId, const std::map<int, int> &allMeas);
    void executeChoCandidate(ChoCandidate &candidate);
    void cancelAllChoCandidates();
    void resetChoRuntimeState();

};

} // namespace nr::ue
