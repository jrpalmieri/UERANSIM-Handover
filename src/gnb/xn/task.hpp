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


class XnTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;

    // Peer gNB info table, populated when an XnSetupRequest is received.
    XnPeerTable m_xnPeerTable;

    // queue for requests that are deferred due to missing UE context or other information.
    std::deque<std::unique_ptr<NmGnbRrcToXn>> m_deferredQueue;

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

    // Incoming from network — target gNB handlers
    void xnHandoverRequestTarget(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnHandoverCancelTarget(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnSnStatusTransferTarget(int gnbId, ASN_XNAP_XnAP_PDU *pdu);

    // Incoming from network — source gNB handlers
    void xnHandoverRequestAckSource(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnHandoverPreparationFailureSource(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnUeContextReleaseSource(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnHandoverSuccessSource(int gnbId, ASN_XNAP_XnAP_PDU *pdu);
    void xnConditionalHandoverCancelTarget(int gnbId, ASN_XNAP_XnAP_PDU *pdu);

    // Outgoing — source gNB (triggered by RrcToXn messages)
    void xnHandoverRequestSource(int64_t ueId, int64_t targetNci, bool isCho, std::unique_ptr<GnbHandoverUeContexts> contexts);
    void xnHandoverCancelSource(int64_t ueId, int64_t targetNci, bool isCho);
    void xnSnStatusTransferSource(int64_t ueId, int64_t targetNci, bool isCho);
    void xnConditionalHandoverCancelSource(int64_t ueId, int64_t targetNci);

    // Outgoing — target gNB (triggered by RrcToXn messages)
    void xnHandoverRequestAckTarget(int64_t ueId, int64_t targetNci, bool isCho,
                                    OctetString rrcReconfigIe);
    void xnHandoverPreparationFailureTarget(int64_t ueId, int64_t targetNci, bool isCho,
                                            int reason);
    void xnUeContextReleaseTarget(int64_t ueId, int64_t targetNci);
    void xnHandoverSuccessTarget(int64_t ueId, int64_t targetNci);

    // Timer handlers
    void xnHandleTimerPrep(int timerId);
    void xnHandleTimerOverall(int timerId);

    // Utility
    bool GetUeContexts(int64_t ueId, GnbHandoverUeContexts &out);
    void enqueueDeferred(std::unique_ptr<NmGnbRrcToXn> msg);
    void processDeferredQueue();
};

} // namespace nr::gnb
