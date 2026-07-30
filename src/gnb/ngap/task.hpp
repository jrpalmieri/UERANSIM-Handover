//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <deque>
#include <optional>
#include <unordered_map>

#include <gnb/nts.hpp>
#include <gnb/types.hpp>
#include <lib/app/monitor.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>

extern "C"
{
    struct ASN_NGAP_NGAP_PDU;
    struct ASN_NGAP_NGSetupResponse;
    struct ASN_NGAP_NGSetupFailure;
    struct ASN_NGAP_ErrorIndication;
    struct ASN_NGAP_DownlinkNASTransport;
    struct ASN_NGAP_RerouteNASRequest;
    struct ASN_NGAP_PDUSessionResourceSetupRequest;
    struct ASN_NGAP_InitialContextSetupRequest;
    struct ASN_NGAP_UEContextReleaseCommand;
    struct ASN_NGAP_UEContextModificationRequest;
    struct ASN_NGAP_AMFConfigurationUpdate;
    struct ASN_NGAP_OverloadStart;
    struct ASN_NGAP_OverloadStop;
    struct ASN_NGAP_PDUSessionResourceReleaseCommand;
    struct ASN_NGAP_Paging;
    struct ASN_NGAP_HandoverRequest;
    struct ASN_NGAP_HandoverCommand;
    struct ASN_NGAP_HandoverPreparationFailure;
    struct ASN_NGAP_PathSwitchRequestAcknowledge;
    struct ASN_NGAP_PathSwitchRequestFailure;
    struct ASN_NGAP_PDUSessionResourceSetupRequestTransfer;
}

namespace nr::gnb
{

class SctpTask;
class GnbRrcTask;
class GtpTask;
class GnbAppTask;

class NgapTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;

    std::unordered_map<int, NgapAmfContext *> m_amfCtx;
    std::unordered_map<int64_t, NgapUeContext *> m_ueCtx;

    // tracks pending CHO handover preparations, so that responses can be correlated.
    // primary index is UE ctxId, secondary index is target NCI. Third value is count of pending CHO preparations for the same target NCI
    //  (can be >1 but unlikely in practice, since usually only 1 handover preparation per target NCI would be triggered for the same UE)
    std::unordered_map<int64_t, std::unordered_map<int64_t, int>> m_hoReqChoPendingByTargetNci;

    // Pending handover context tracker, indexed by transactionId
    std::unordered_map<uint32_t, NGAPHandoverPending> m_handoversPending;

    struct XnNgapHandoverPending
    {
        std::unique_ptr<NgapUeContext> ctx{};
        std::vector<PduSessionResource> sessions{};
    };
    // Xn has no NGAP transaction ID, so provisional target contexts are keyed
    // by the simulator-stable ueId until RRC confirms UE arrival.
    std::unordered_map<int64_t, XnNgapHandoverPending> m_xnHandoversPending;

    // Transaction Id for correlating responses to requests from other tasks
    uint32_t m_transactionIdCounter;

    int64_t m_ueNgapIdCounter;
    uint32_t m_downlinkTeidCounter;
    bool m_isInitialized;

    static constexpr int TIMER_DEFERRED_QUEUE = 1001;
    static constexpr int DEFERRED_QUEUE_INTERVAL_MS = 200;
    static constexpr int DEFERRED_MAX_RETRIES = 5;

    static constexpr int TIMER_STATUS_UPDATE = 1002;
    static constexpr int TIMER_STATUS_UPDATE_INTERVAL_MS = 500;

    // Expiry sweep for the N2 target-side pending-handover map (m_handoversPending);
    // NGAP counterpart of GnbRrcTask::sweepPendingHandovers().
    static constexpr int TIMER_HO_PENDING_SWEEP = 1003;
    static constexpr int TIMER_HO_PENDING_SWEEP_INTERVAL_MS = 1000;

    std::deque<std::unique_ptr<NmGnbRrcToNgap>> m_deferredQueue;

    void enqueueDeferred(std::unique_ptr<NmGnbRrcToNgap> msg);
    void processDeferredQueue();

    friend class GnbCmdHandler;

  public:
    explicit NgapTask(TaskBase *base);
    ~NgapTask() override = default;

    bool getUeContext(int64_t ueId, std::optional<NgapUeContext> &out);
    NgapAmfContext *getAmfContextForXn(int amfId);
    NgapAmfContext *getConnectedAmfContextForXn();

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:

    /* Utility functions - utils.cpp */
  
    void createAmfContext(const GnbAmfConfig &config);
    NgapAmfContext *findAmfContext(int ctxId);
    void createUeContext(int64_t ueId, int32_t &requestedSliceType);
    NgapUeContext *findUeContext(int64_t ctxId);
    NgapUeContext *findUeByRanId(int64_t ranUeNgapId);
    NgapUeContext *findUeByAmfId(int64_t amfUeNgapId);
    NgapUeContext *findUeByNgapIdPair(int amfCtxId, const NgapIdPair &idPair);
    void deleteUeContext(int64_t ueId);
    void deleteAmfContext(int amfId);
    int64_t generateRanUeNgapId(int64_t ueId);
    void sendGnbStatusUpdate();

    /* Interface management */
    void handleAssociationSetup(int amfId, int ascId, int inCount, int outCount);
    void handleAssociationShutdown(int amfId);
    void sendNgSetupRequest(int amfId);
    void sendErrorIndication(int amfId, NgapCause cause = NgapCause::Protocol_unspecified, int64_t ueId = 0);
    void receiveNgSetupResponse(int amfId, ASN_NGAP_NGSetupResponse *msg);
    void receiveNgSetupFailure(int amfId, ASN_NGAP_NGSetupFailure *msg);
    void receiveErrorIndication(int amfId, ASN_NGAP_ErrorIndication *msg);
    void receiveAmfConfigurationUpdate(int amfId, ASN_NGAP_AMFConfigurationUpdate *msg);
    void receiveOverloadStart(int amfId, ASN_NGAP_OverloadStart *msg);
    void receiveOverloadStop(int amfId, ASN_NGAP_OverloadStop *msg);

    /* Message transport */
    void sendNgapNonUe(int amfId, ASN_NGAP_NGAP_PDU *pdu);
    void sendNgapUeAssociated(int64_t ueId, ASN_NGAP_NGAP_PDU *pdu);
    void handleSctpMessage(int amfId, uint16_t stream, const UniqueBuffer &buffer);
    bool handleSctpStreamId(int amfId, int stream, const ASN_NGAP_NGAP_PDU &pdu);

    /* NAS transport */
    void handleInitialNasTransport(int64_t ueId, OctetString &nasPdu, int64_t rrcEstablishmentCause,
                                   const std::optional<GutiMobileIdentity> &sTmsi);
    void handleUplinkNasTransport(int64_t ueId, const OctetString &nasPdu);
    void receiveDownlinkNasTransport(int amfId, ASN_NGAP_DownlinkNASTransport *msg);
    void deliverDownlinkNas(int64_t ueId, OctetString &&nasPdu);
    void deliverDownlinkNasAccept(int64_t ueId, OctetString &&nasPdu, std::unique_ptr<std::vector<PduSessionResource>> sessionList);
    void sendNasNonDeliveryIndication(int64_t ueId, const OctetString &nasPdu, NgapCause cause);
    void receiveRerouteNasRequest(int amfId, ASN_NGAP_RerouteNASRequest *msg);
    void deliverPDUSessionSetupRequest(int64_t ueId, std::unique_ptr<std::vector<PduSessionResource>> sessionList);

    /* PDU session management */
    void receiveSessionResourceSetupRequest(int amfId, ASN_NGAP_PDUSessionResourceSetupRequest *msg);
    void receiveSessionResourceReleaseCommand(int amfId, ASN_NGAP_PDUSessionResourceReleaseCommand *msg);
    std::optional<NgapCause> setupPduSessionResource(NgapUeContext *ue, PduSessionResource &resource);
    void prepareXnHandover(int64_t ueId, std::unique_ptr<XnHandoverCoreContext> core,
                           std::unique_ptr<std::vector<PduSessionResource>> sessions);
    bool activateXnHandover(int64_t ueId);
    void releaseXnSourceContext(int64_t ueId);
    void cancelXnTargetPreparation(int64_t ueId);

    /* UE context management */

    void receiveInitialContextSetup(int amfId, ASN_NGAP_InitialContextSetupRequest *msg);
    void receiveContextRelease(int amfId, ASN_NGAP_UEContextReleaseCommand *msg);
    void receiveContextModification(int amfId, ASN_NGAP_UEContextModificationRequest *msg);
    void sendContextRelease(int64_t ueId, NgapCause cause);
    void makeNgapContextItems(NgapUeContext *ue, ASN_NGAP_InitialContextSetupRequest *ie);
    void makeNgapPduSessionItems(PduSessionResource *resource,
                                  ASN_NGAP_PDUSessionResourceSetupRequestTransfer *transfer);

    /* NAS Node Selection - nnsf.cpp */
    
    NgapAmfContext *selectAmf(int64_t ueId, int32_t &requestedSliceType);
    NgapAmfContext *selectNewAmfForReAllocation(int64_t ueId, int initiatedAmfId, int amfSetId);

    /* Radio resource control */
    void handleRadioLinkFailure(int64_t ueId);
    void receivePaging(int amfId, ASN_NGAP_Paging *msg);

    /* N2 Handover (AMF-mediated) */

    void sendHandoverRequired(int64_t ueId, int64_t targetNci, NgapCause cause, bool hoForChoPreparation, std::unique_ptr<OctetString> rrcContainer);
    void receiveHandoverRequest(int amfId, ASN_NGAP_HandoverRequest *msg, uint16_t stream);
    void sendHandoverRequestAcknowledge(uint32_t transactionId, int64_t ueId, std::unique_ptr<std::vector<PduSessionResource>> admittedList, 
                                        std::unique_ptr<std::vector<PduSessionResource>> failedList, std::unique_ptr<OctetString> targetRrcContainer);

    void receiveHandoverCommand(int amfId, ASN_NGAP_HandoverCommand *msg);
    void receiveHandoverPreparationFailure(int amfId, ASN_NGAP_HandoverPreparationFailure *msg);
    void sendHandoverNotify(int64_t ueId);
    void handleHandoverNotifyFromRrc(int64_t ueId);
    void sendHandoverFailure(NgapCause cause, int64_t ueId);
    // Target-side RRC rejected a HandoverRequest (HANDOVER_FAILURE_SEND):
    // send NGAP HandoverFailure to the AMF and drop the pending entry.
    void handleRrcHandoverFailure(uint32_t transactionId, NgapCause cause);
    // Periodic (1 s) garbage collection of m_handoversPending entries whose
    // expireTime has passed — i.e. the UE never completed the handover.
    void sweepPendingHandovers();

    std::unique_ptr<OctetString> makeSourceTargetNgranTransparentContainer(int64_t targetNCI, const Plmn &targetPlmn, std::unique_ptr<OctetString> rrcContainer);

    /* Xn Handover (gNB-gNB) */

    void sendPathSwitchRequest(int64_t ueId);
    void receivePathSwitchRequestAcknowledge(int amfId, ASN_NGAP_PathSwitchRequestAcknowledge *msg);
    void receivePathSwitchRequestFailure(int amfId, ASN_NGAP_PathSwitchRequestFailure *msg);

  };

} // namespace nr::gnb
