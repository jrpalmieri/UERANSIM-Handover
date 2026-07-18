#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>

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


// tracking of pending handover requests
struct XnPendingHandover
{
    // The XnTxId is used to correlate the handover request with the response
    uint32_t xnTxId;
    int64_t ueId;              // target UE ID; populated when RRC ACK arrives
    int64_t sourceNci;
    int     sourceGnbId{-1};   // gnbId to route the Ack back to
    int64_t sourceUeXnApId{0}; // source's UE ID echoed in the Ack
    int64_t targetUeXnApId{0}; // assigned by target and learned in the Ack
    int64_t targetNci{0};
    int     targetGnbId{-1};

    enum class Role { SOURCE, TARGET } role{Role::SOURCE};

    // indicates if this is a conditional handover (CHO) request
    bool isCho;
    
    // state flags for tracking the state of the handover
    
    bool executionSucceeded{};
    bool ackSent{};
    
    // HandoverRequestAck received from target gNB (source role only)
    bool ackReceived{};
    
    
    bool timeoutReported{};

    // start timestamp for timer comparisons
    uint64_t timestamp;

    // allocated streamID for all Xn messages in the handover
    uint16_t streamId{0}; 
};

class XnTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;

    // Peer gNB info table, populated when an XnSetupRequest is received.
    XnPeerTable m_xnPeerTable;

    // Accepted inbound associations (negative clientIds) whose peer identity
    // is not yet known — resolved when their XnSetupRequest arrives.  Holds the
    // association parameters delivered with the synthesized ASSOCIATION_SETUP.
    struct PendingInboundAssoc
    {
        int associationId{};
        int inStreams{};
        int outStreams{};
        uint64_t timestamp{}; // for pruning inbound peers that never send a setup
    };
    std::unordered_map<int, PendingInboundAssoc> m_pendingInbound;

    // queue for requests that are deferred due to missing UE context or other information.
    std::deque<std::unique_ptr<NmGnbRrcToXn>> m_deferredQueue;

    // Next available XnTxId for correlating handover requests and responses
    uint32_t m_nextXnTxId{1};

    // TEID counter for DL Xn-U forwarding tunnels allocated at the target gNB.
    // Seeded at a high base so these TEIDs stay disjoint from the NGAP task's
    // real DL NG-U TEIDs (allocated sequentially from 1) — the two share the
    // target GtpTask's receive-side TEID namespace (m_incomingForwardingTeids
    // vs. the session tree), so an overlap would misroute real DL traffic.
    static constexpr uint32_t XN_FORWARDING_TEID_BASE = 0xC0000000u;
    uint32_t m_xnDownlinkTeidCounter{XN_FORWARDING_TEID_BASE};

    // map of pending handover requests at target gNB, keyed by the XnTxId assigned by the target gNB
    std::unordered_map<uint32_t, XnPendingHandover> m_pendingHandoversTargetByTxId;

    // map of pending handover requests at source gNB, keyed by (UE ID, target gnbId).
    // Per TS 38.423 UE-associated signalling there is one Xn UE association per UE
    // per peer node, and incoming replies (Ack/Failure/Release/Success) identify only
    // the peer and the UE XnAP IDs — not the target cell — so the gnbId is the key
    // the receive paths can correlate on.  The requested cell is kept in the entry's
    // targetNci field; RRC-facing sends that are addressed by NCI resolve entries via
    // findSourcePendingByNci().
    // (hash functor needed because std::hash has no std::pair specialization; same
    //  pattern as UeSessionIdHash in gtp/utils.hpp)
    struct UeTargetPairHash
    {
        std::size_t operator()(const std::pair<int64_t, int64_t> &k) const
        {
            std::size_t h1 = std::hash<int64_t>{}(k.first);
            std::size_t h2 = std::hash<int64_t>{}(k.second);
            return h1 ^ (h2 << 1);
        }
    };
    using SourcePendingMap = std::unordered_map<std::pair<int64_t, int64_t>, XnPendingHandover, UeTargetPairHash>;
    SourcePendingMap m_pendingHandoversSourceByUeId;

    // Small idempotency ledger for duplicate UEContextRelease delivery.  The
    // normal pending entry is removed after the first validated release.
    std::unordered_set<int64_t> m_completedSourceReleases;

    static constexpr int TIMER_NEIGHBOR_CHECK = 2001;
    static constexpr int TIMER_NEIGHBOR_CHECK_INTERVAL_MS = 10000;

    // inbound association with no XnSetupRequest within this window is closed
    static constexpr uint64_t PENDING_INBOUND_TIMEOUT_MS = 30000;

    static constexpr int TIMER_DEFERRED_QUEUE = 1001;
    static constexpr int DEFERRED_QUEUE_INTERVAL_MS = 200;
    static constexpr int DEFERRED_MAX_RETRIES = 5;

    // Timer for checking pending handover requests for timeouts.
    static constexpr int TIMER_HANDOVERS_PENDING = 3001;
    static constexpr uint64_t HANDOVERS_PENDING_INTERVAL_MS = 1000;

    // Target's timeout for RRC Ack receipt (measured from HandoverRequest receipt)
    static constexpr uint64_t HANDOVER_RRC_ACK_TIMEOUT_MS = 4000;
    
    // Source's timeout for RRC Ack/Failure receipt (measured from HandoverRequest send)
    static constexpr uint64_t HANDOVER_PREPARATION_TIMEOUT_MS = 5000;

    // Target's timeout for Xn receipt of RRC Handover Success message (measured from RRC Ack receipt).
    static constexpr uint64_t HANDOVER_RRC_SUCCESS_TIMEOUT_MS = 4000;

    // Source's timeout for Xn receipt of UEContextRelease message (measured from Handover Ack receipt).
    //  CHO preparations require longer timeout
    static constexpr uint64_t HANDOVER_OVERALL_TIMEOUT_MS = 10000;
    static constexpr uint64_t HANDOVER_OVERALL_TIMEOUT_CHO_MS = 100000;

    // Target's timeout for Xn receipt of UEContextRelease message (measured from Handover Ack receipt).
    //  CHO preparations require longer timeout
    static constexpr uint64_t HANDOVER_OVERALL_TARGET_TIMEOUT_MS = 10000;
    static constexpr uint64_t HANDOVER_OVERALL_TARGET_TIMEOUT_CHO_MS = 100000;

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
    void requestSctpConnection(int gnbId, const std::string &remoteAddress, uint16_t remotePort);
    void handleAssociationSetup(int gnbId, int ascId, int inCount, int outCount);
    void handleAssociationShutdown(int gnbId);
    void handleConnectionFailure(int clientId);

    // Interface Setup handlers
    // Setup-request *receive* and response/failure *send* operate on SCTP
    // clientIds — an inbound peer's identity is unknown until its
    // XnSetupRequest is decoded.  Response/failure *receive* run on our own
    // outbound association, where the dispatcher already resolved the gnbId.
    void xnSetupRequestSend(int gnbId);
    void xnSetupRequestReceive(int clientId, ASN_XNAP_XnAP_PDU *pdu);
    bool xnSetupResponseSend(int clientId);
    void xnSetupResponseReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnSetupFailureSend(int clientId);
    void xnSetupFailureReceive(int gnbId, ASN_XNAP_XnAP_PDU *pdu);

    // Decode and dispatch incoming SCTP messages (resolves the SCTP clientId
    // to the peer gnbId before dispatching to the procedure handlers)
    void xnHandleSctpMessage(int clientId, uint16_t stream, const UniqueBuffer &buffer);

    /* Handover - Phase 1 (Preparation) */
    
    void sendHandoverRequest(int64_t ueId, int64_t targetNci, std::unique_ptr<GnbHandoverUeContexts> contexts, std::unique_ptr<OctetString> rrcContainer, std::unique_ptr<GnbCondHandoverRequest> choParams);
    void receiveHandoverRequest(int gnbId, uint16_t stream, ASN_XNAP_XnAP_PDU *pdu);
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
    void sendHandoverCancel(int64_t ueId, int64_t targetNci, bool isCho);

    void receiveHandoverCancel(int gnbId, ASN_XNAP_XnAP_PDU *pdu);

    // Timer handlers
    void processHandoverTimeouts(int timerId);

    // Utility
    bool GetUeContexts(int64_t ueId, GnbHandoverUeContexts &out);
    void enqueueDeferred(std::unique_ptr<NmGnbRrcToXn> msg);
    void processDeferredQueue();
    void handlePeerHandoverLoss(int gnbId);
    void removeTargetPendingHandover(int txId);
    void removeSourcePendingHandover(int64_t ueId, int targetGnbId);

    // Locate a source-role pending entry by the requested cell's NCI (RRC addresses
    // the Xn task by NCI; the map itself is keyed by target gnbId).
    SourcePendingMap::iterator findSourcePendingByNci(int64_t ueId, int64_t targetNci);
};

} // namespace nr::gnb
