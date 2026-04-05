//
// Phase 2+3 – Handover execution upon ReconfigurationWithSync reception.
//
// Phase 2: Core handover – cell switch, RRCReconfigurationComplete, T304.
// Phase 3: Enhanced handover – RACH config, measurement suspension,
//           MAC reset indication, improved PCI resolution, security key
//           refresh placeholder.
//

#include "task.hpp"

#include <lib/asn/utils.hpp>
#include <lib/rrc/encode.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>

#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete.h>
#include <asn/rrc/ASN_RRC_RRCReconfigurationComplete-IEs.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-Message.h>
#include <asn/rrc/ASN_RRC_UL-DCCH-MessageType.h>

static constexpr int TIMER_ID_T304 = 2;

namespace nr::ue
{

/* ================================================================== */
/*  Find a detected cell that matches the target PCI                  */
/* ================================================================== */

int UeRrcTask::findCellByPci(int physCellId)
{
    int currentCellId =
        m_base->shCtx.currentCell.get<int>([](auto &v) { return v.cellId; });

    // Strategy 1: direct match of PCI value to an RLS cellId
    //   (covers simulation setups where cellId == PCI)
    if (physCellId != currentCellId && m_cellDesc.count(physCellId))
    {
        m_logger->debug("Handover: PCI %d matched directly to cell[%d]", physCellId, physCellId);
        return physCellId;
    }

    // Strategy 2: match by NCI – some cells may expose their NCI via SIB1;
    //   if physCellId happens to match the lower bits, use it.
    for (auto &[id, desc] : m_cellDesc)
    {
        if (id == currentCellId)
            continue;
        int nciLowBits = static_cast<int>(desc.sib1.nci & 0x3FF);  // lower 10 bits ≈ PCI
        if (nciLowBits == physCellId)
        {
            m_logger->debug("Handover: PCI %d matched via NCI lower bits to cell[%d]", physCellId, id);
            return id;
        }
    }

    m_logger->err("Handover: PCI %d could not be resolved to any detected cell", physCellId);
    return 0;
}

/* ================================================================== */
/*  Suspend / resume measurement evaluation during handover           */
/* ================================================================== */

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
        state.reported = false;
    }

    // Keep CHO candidate configuration but clear transient TTT/timer state so
    // post-handover evaluation starts from a clean baseline.
    resetChoRuntimeState();

    m_logger->debug("Handover - Measurement evaluations resumed after handover");
}

/* ================================================================== */
/*  Security key refresh                                              */
/* ================================================================== */

void UeRrcTask::refreshSecurityKeys()
{
    // Per TS 38.331 §5.3.5.4: During handover the UE derives a new
    // KgNB* from the current KgNB and the target PCI / DL-ARFCN.
    // In UERANSIM's simplified security model we log the event but
    // do not actually recompute keys (integrity/ciphering are simulated).
    m_logger->debug("Handover - Security key refresh (KgNB* derivation) - not performed in simulated RF environment");
}

/**
 * @brief Perform a handover between the current (serving) gnb and the target gnb.
 * 
 * @param txId transaction identifier for the RRCReconfiguration message that triggered the handover
 * @param targetPhysCellId the PCI of the target cell as indicated in the RRCReconfiguration message
 * @param newCRNTI the new C-RNTI to be assigned to the UE by the target cell
 * @param t304Ms the duration of the T304 timer (which guards for handover failures)
 * @param hasRachConfig the RACH config information (not used, but passed in the RRCReconfig msg)
*/
void UeRrcTask::performHandover(long txId, int targetPhysCellId, int newCRNTI,
                                 int t304Ms, bool hasRachConfig)
{
    m_logger->info("Handover - received RRCReconfig: targetPCI=%d newC-RNTI=%d t304=%dms",
                   targetPhysCellId, newCRNTI, t304Ms);

    if (m_base->rlsTask)
        m_base->rlsTask->setCurrentCrnti(static_cast<uint32_t>(newCRNTI));

    if (m_state != ERrcState::RRC_CONNECTED)
    {
        m_logger->err("Handover - command ignored because UE not in RRC_CONNECTED (state=%s)",
                      ToJson(m_state).str().c_str());
        return;
    }

    // ---- 0. Suspend measurements during handover (TS 38.331 §5.5.6.1) ----
    suspendMeasurements();

    // ---- 1. Start T304 (handover supervision timer) ----
    m_handoverInProgress = true;
    m_hoTxId = txId;
    m_hoTargetPci = targetPhysCellId;
    setTimer(TIMER_ID_T304, t304Ms);

    // ---- 2. Security key refresh (TS 38.331 §5.3.5.4) ----
    refreshSecurityKeys();

    // ---- 3. Find the target cell among detected cells ----
    int targetCellId = findCellByPci(targetPhysCellId);
    if (targetCellId == 0)
    {
        m_logger->err("Handover - failure: target PCI %d not found among %d detected cells",
                      targetPhysCellId, static_cast<int>(m_cellDesc.size()));
        m_handoverInProgress = false;
        resumeMeasurements();
        // T304 will fire but handleT304Expiry checks the flag
        declareRadioLinkFailure(rls::ERlfCause::SIGNAL_LOST_TO_CONNECTED_CELL);
        return;
    }

    // ---- 4. Save previous serving-cell information ----
    ActiveCellInfo previousCell =
        m_base->shCtx.currentCell.get<ActiveCellInfo>([](auto &v) { return v; });

    // ---- 5. MAC reset indication (TS 38.331 §5.3.5.4) ----
    //  In a real UE the MAC entity is reset here.  UERANSIM does not model
    //  the MAC layer in detail, so we log the event for traceability.
    m_logger->debug("Handover - MAC reset for handover to cell[%d] - not performed in simulated RF environment", targetCellId);

    // ---- 6. Switch serving cell ----
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

    m_logger->info("Handover - Serving cell switched: cell[%d] → cell[%d]",
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

    // In this simulated environment, source and target cells share the same
    // measurement model. Keep existing MeasConfig so A3/A5 can re-trigger for
    // subsequent handovers. Per-measId trigger state is reset in
    // resumeMeasurements().
    m_logger->debug("Handover - Existing measurement configuration preserved");

    // ---- 9. Handover complete – stop supervision ----
    m_handoverInProgress = false;
    // (T304 will fire later and be silently ignored since the flag is cleared.)

    // ---- 10. Resume measurements on the new serving cell ----
    resumeMeasurements();

    // ---- 11. Notify NAS of the cell change ----
    auto w2 = std::make_unique<NmUeRrcToNas>(NmUeRrcToNas::ACTIVE_CELL_CHANGED);
    w2->previousTai = Tai{previousCell.plmn, previousCell.tac};
    m_base->nasTask->push(std::move(w2));

    m_logger->info("Handover to cell[%d] completed (PCI=%d, newC-RNTI=%d)",
                   targetCellId, targetPhysCellId, newCRNTI);
}

/* ================================================================== */
/*  T304 expiry → handover failure                                    */
/* ================================================================== */

void UeRrcTask::handleT304Expiry()
{
    if (!m_handoverInProgress)
    {
        // Handover already completed before T304 fired – nothing to do.
        return;
    }

    m_logger->err("T304 expired – handover to PCI %d failed", m_hoTargetPci);
    m_handoverInProgress = false;

    // Resume measurements so the UE can attempt re-establishment
    resumeMeasurements();

    // Per TS 38.331 §5.3.5.8.3: upon T304 expiry the UE shall
    // initiate the RRC re-establishment procedure.
    declareRadioLinkFailure(rls::ERlfCause::SIGNAL_LOST_TO_CONNECTED_CELL);
}

} // namespace nr::ue
