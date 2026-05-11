//
// UE Handover
//
// Implements US-side handover support
//   performHandover        – execute connection to new gNB
//   suspendMeasurements    – suspend measurement evaluation during handover
//   resumeMeasurements     – resume measurement evaluation after handover
//

#include "task.hpp"

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <utils/constants.hpp>

#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete-IEs.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-MessageType.h>

static constexpr int TIMER_ID_T304 = 2;

namespace nr::ue
{


/*  Suspend / resume measurement evaluation during handover           */
void UeRrcTask::suspendMeasurements()
{
    m_measurementEvalSuspended = true;
    m_logger->debug("Handover - Measurement evaluation suspended for handover");
}

void UeRrcTask::resumeMeasurements()
{
    m_measurementEvalSuspended = false;
    // Reset all entering-condition timestamps so TTT re-evaluates cleanly.
    for (auto &[id, state] : m_measConfig.measIdStates)
    {
        state.enteringTimestamp = 0;
        state.isReported = false;
    }

    // Keep CHO candidate configuration but clear transient TTT/timer state so
    // post-handover evaluation starts from a clean baseline.
    resetChoRuntimeState();

    m_logger->debug("Handover - Measurement evaluations resumed after handover");
}


/*  Security key refresh (not implemented)                                              */
void UeRrcTask::refreshSecurityKeys()
{
    // Per TS 38.331 §5.3.5.4: During handover the UE derives a new
    // KgNB* from the current KgNB and the target NCI / DL-ARFCN.
    // In UERANSIM's simplified security model we log the event but
    // do not actually recompute keys (integrity/ciphering are simulated).
    m_logger->debug("Handover - Security key refresh (KgNB* derivation) - not performed in simulated RF environment");
}

/**
 * @brief Perform a handover between the current (serving) gnb and the target gnb.
 * 
 * @param txId transaction identifier for the RRCReconfiguration message that triggered the handover
 * @param targetCellId the NCI of the target cell as indicated in the RRCReconfiguration message
 * @param newCRNTI the new C-RNTI to be assigned to the UE by the target cell
 * @param t304Ms the duration of the T304 timer (which guards for handover failures)
 * @param hasRachConfig the RACH config information (not used, but passed in the RRCReconfig msg)
*/
void UeRrcTask::performHandover(long txId, int64_t targetCellId, int newCRNTI,
                                 int t304Ms, bool hasRachConfig)
{
    m_logger->info("Handover - received RRCReconfig: targetNCI=%ld newC-RNTI=%d t304=%dms",
                   targetCellId, newCRNTI, t304Ms);

    if (m_state != ERrcState::RRC_CONNECTED)
    {
        m_logger->err("Handover - command ignored because UE not in RRC_CONNECTED (state=%s)",
                      ToJson(m_state).str().c_str());
        return;
    }

    if (targetCellId == 0)
    {
        m_logger->err("Handover - failure: target NCI %ld not valid",
                      targetCellId);
        return;
    }

    // Suspend measurements during handover (TS 38.331 §5.5.6.1)
    suspendMeasurements();

    // Start T304 (handover supervision timer)
    m_handoverInProgress = true;
    m_hoTxId = txId;
    m_hoTargetNci = targetCellId;
    setTimer(TIMER_ID_T304, t304Ms);

    // Security key refresh (TS 38.331 §5.3.5.4)
    refreshSecurityKeys();


    // Save previous serving-cell information
    ActiveCellInfo previousCell =
        m_base->shCtx.currentCell.get<ActiveCellInfo>([](auto &v) { return v; });

    // MAC reset indication (TS 38.331 §5.3.5.4)
    //  In a real UE the MAC entity is reset here.  UERANSIM does not model
    //  the MAC layer in detail, so we log the event for traceability.
    m_logger->debug("Handover - MAC reset for handover to cell[%ld] - not performed in simulated RF environment", targetCellId);

    // Switch serving cell
    auto &targetDesc = m_cellDesc[targetCellId];
    ActiveCellInfo newCellInfo{};
    newCellInfo.cellId = targetCellId;
    newCellInfo.plmn = targetDesc.sib1.plmn;
    newCellInfo.tac = targetDesc.sib1.tac;
    newCellInfo.category = ECellCategory::SUITABLE_CELL;
    m_base->shCtx.currentCell.set(newCellInfo);

    // Tell RLS to route UL via the new cell
    auto w1 = std::make_unique<NmUeRrcToRls>(NmUeRrcToRls::ASSIGN_CURRENT_CELL);
    w1->cellId = targetCellId;
    m_base->rlsTask->push(std::move(w1));

    m_logger->info("Handover - Serving cell switched: cell[%ld] → cell[%ld]",
                   previousCell.cellId, targetCellId);

    // ---- 7. RACH towards target cell (TS 38.331 §5.3.5.4) ----
    //  If rach-ConfigDedicated was provided the UE performs a contention-free
    //  RACH; otherwise it uses the common RACH config.  UERANSIM's RLS layer
    //  establishes the link implicitly via ASSIGN_CURRENT_CELL, so we only
    //  log the RACH type for protocol fidelity.
    if (hasRachConfig)
        m_logger->debug("Handover - Contention-free RACH to target cell (dedicated RACH config present) - not performed in simulated RF environment");
    else
        m_logger->debug("Handover - Contention-based RACH to target cell (no dedicated RACH config) - not performed in simulated RF environment");

    // ---- 8. Send RRCReconfigurationComplete to the *target* cell ----
    {
        auto *pdu = asn::New<ASN_RRC_UL_DCCH_Message>();
        pdu->message.present = ASN_RRC_UL_DCCH_MessageType_PR_c1;
        pdu->message.choice.c1 = asn::NewFor(pdu->message.choice.c1);
        pdu->message.choice.c1->present =
            ASN_RRC_UL_DCCH_MessageType__c1_PR_rrcReconfigurationComplete;

        auto &rrc = pdu->message.choice.c1->choice.rrcReconfigurationComplete =
            asn::New<ASN_RRC_RRCReconfigurationComplete>();
        rrc->rrc_TransactionIdentifier = txId;
        rrc->criticalExtensions.present =
            ASN_RRC_RRCReconfigurationComplete__criticalExtensions_PR_rrcReconfigurationComplete;
        rrc->criticalExtensions.choice.rrcReconfigurationComplete =
            asn::New<ASN_RRC_RRCReconfigurationComplete_IEs>();

        sendRrcMessage(pdu); // routed via the newly-assigned serving cell
        asn::Free(asn_DEF_ASN_RRC_UL_DCCH_Message, pdu);
    }

    //  Keep existing MeasConfigs in place.
    m_logger->debug("Handover - Existing measurement configuration preserved");

    // Handover complete – reset flag
    m_handoverInProgress = false;
    // (T304 will fire later and be silently ignored since the flag is cleared.)

    // Resume measurements
    resumeMeasurements();

    // Notify NAS layer of the cell change
    auto w2 = std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::ACTIVE_CELL_CHANGED);
    w2->previousTai = Tai{previousCell.plmn, previousCell.tac};
    m_base->nasTask->push(std::move(w2));

    m_logger->info("Handover to cell[%ld] completed", targetCellId);
}

/*  T304 expiry → handover failure                                    */
void UeRrcTask::handleT304Expiry()
{
    if (!m_handoverInProgress)
    {
        // Handover already completed before T304 fired – nothing to do.
        return;
    }
    
    m_logger->err("T304 expired - handover to cell %ld failed", m_hoTargetNci);
    m_handoverInProgress = false;

    // Resume measurements so the UE can attempt re-establishment
    resumeMeasurements();


    // Per TS 38.331 §5.3.5.8.3: upon T304 expiry the UE shall
    // initiate the RRC re-establishment procedure.
    //declareRadioLinkFailure(rls::ERlfCause::SIGNAL_LOST_TO_CONNECTED_CELL);
}

} // namespace nr::ue
