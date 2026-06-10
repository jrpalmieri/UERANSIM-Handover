#pragma once

#include <memory>
#include <unordered_map>

#include <gnb/nts.hpp>
#include <gnb/types.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>
#include <utils/unique_buffer.hpp>
#include <gnb/gtp/task.hpp>
#include <gnb/sctp/task.hpp>
#include <gnb/rrc/task.hpp>
#include <gnb/ngap/task.hpp>

#include "peertable.hpp"

extern "C"
{
    struct ASN_XNAP_XnAP_PDU;
}

namespace nr::gnb
{

// struct XnConnection
// {
//     int64_t nci;
//     InetAddress addr;
// };

// tracking of pending handover requests from source gNBs
struct XnPendingHandover
{
    // The XnTxId is used to correlate the handover request with the response
    uint32_t xnTxId;
    int64_t ueId;              // target UE ID; populated when RRC ACK arrives
    int64_t sourceNci;
    int     sourceGnbId{-1};   // gnbId to route the Ack back to
    int64_t sourceUeXnApId{0}; // source's UE ID echoed in the Ack

    // indicates if this is a conditional handover (CHO) request
    bool isCho;

    // start timestamp for timer comparisons
    uint64_t timestamp;
};

class XnTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;

    // Peer gNB info table, populated when an XnSetupRequest is received.
    XnPeerTable m_xnPeerTable;

    // queue for requests that are deferred due to missing UE context or other information.
    std::deque<std::unique_ptr<NmGnbRrcToXn>> m_deferredQueue;

    // Next available XnTxId for correlating handover requests and responses
    uint32_t m_nextXnTxId{1};

    // TEID counter for DL Xn-U forwarding tunnels allocated at the target gNB
    uint32_t m_xnDownlinkTeidCounter{0};

    std::vector<XnPendingHandover> m_pendingRequests;

    static constexpr int TIMER_NEIGHBOR_CHECK = 2001;
    static constexpr int TIMER_NEIGHBOR_CHECK_INTERVAL_MS = 10000;

    static constexpr int TIMER_DEFERRED_QUEUE = 1001;
    static constexpr int DEFERRED_QUEUE_INTERVAL_MS = 200;
    static constexpr int DEFERRED_MAX_RETRIES = 5;

  public:
    explicit XnTask(TaskBase *base);
    ~XnTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:

    // update SCTP connections with neighbors
    void updateXnConnections();
    void handleAssociationSetup(int gnbId, int ascId, int inCount, int outCount);
    void handleAssociationShutdown(int gnbId);

    // Interface Setup handlers
    void xnSetupRequestSend(int gnbId);
    void xnSetupRequestReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnSetupResponseSend(int gnbId);
    void xnSetupResponseReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnSetupFailureSend(int gnbId);
    void xnSetupFailureReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu);

    // Decode and dispatch incoming SCTP messages
    void xnHandleSctpMessage(int gnbId, uint16_t stream, const UniqueBuffer &buffer);

    /* Handover - Phase 1 (Preparation) */
    
    void sendHandoverRequest(int64_t ueId, int64_t targetNci, std::unique_ptr<GnbHandoverUeContexts> contexts, std::unique_ptr<OctetString> rrcContainer, std::unique_ptr<GnbCondHandoverRequest> choParams);
    void receiveHandoverRequest(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void sendHandoverRequestAck(uint32_t xnTxId, uint64_t ueId, std::unique_ptr<OctetString> rrcContainer, 
        std::unique_ptr<std::vector<PduSessionResource>> admittedSessions, std::unique_ptr<std::vector<PduSessionResource>> rejectedSessions);
    void receiveHandoverRequestAck(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void sendHandoverPreparationFailure(uint32_t xnTxId, int reason);
    void receiveHandoverPreparationFailure(int gnbId, ASN_XNAP_XnAP_PDU *pdu);

      /* Handover - Phase 2 (execution) */

    void sendSnStatusTransfer(int64_t ueId, int64_t targetNci, bool isCho);
    void receiveSnStatusTransfer(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void sendHandoverSuccess(int64_t ueId, int64_t targetNci);
    void receiveHandoverSuccess(int gnbId, ASN_XNAP_XnAP_PDU *pdu);

    void sendUeContextRelease(int64_t ueId, int64_t targetNci);
    void receiveUeContextRelease(int gnbId, ASN_XNAP_XnAP_PDU *pdu);


    // Outgoing — source gNB (triggered by RrcToXn messages)
    void xnHandoverCancelSource(int64_t ueId, int64_t targetNci, bool isCho);

    void xnHandoverCancelTarget(int gnbId, ASN_XNAP_XnAP_PDU *pdu);

    // Timer handlers
    void xnHandleTimerPrep(int timerId);
    void xnHandleTimerOverall(int timerId);

    // Utility
    bool GetUeContexts(int64_t ueId, GnbHandoverUeContexts &out);
    void enqueueDeferred(std::unique_ptr<NmGnbRrcToXn> msg);
    void processDeferredQueue();
};

} // namespace nr::gnb
