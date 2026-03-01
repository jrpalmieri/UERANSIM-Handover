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

    // Strategy 3: strongest non-serving detected cell
    //   (UERANSIM does not store PCI per cell, so we fall back to the
    //    best-signal neighbour – the one that likely triggered A3/A5.)
    int bestCellId = 0;
    int bestDbm = -200;
    for (auto &[id, desc] : m_cellDesc)
    {
        if (id == currentCellId)
            continue;
        if (desc.dbm > bestDbm)
        {
            bestDbm = desc.dbm;
            bestCellId = id;
        }
    }

    if (bestCellId != 0)
    {
        m_logger->info("Handover: PCI %d resolved to best neighbour cell[%d] (dbm=%d)",
                       physCellId, bestCellId, bestDbm);
    }

    return bestCellId;
}

/* ================================================================== */
/*  Suspend / resume measurement evaluation during handover           */
/* ================================================================== */

void UeRrcTask::suspendMeasurements()
{
    m_measurementsSuspended = true;
    m_logger->debug("Measurements suspended for handover");
}

void UeRrcTask::resumeMeasurements()
{
    m_measurementsSuspended = false;
    // Reset all entering-condition timestamps so TTT re-evaluates cleanly.
    for (auto &[id, state] : m_measConfig.measIdStates)
    {
        state.enteringTimestamp = 0;
        state.reported = false;
    }
    m_logger->debug("Measurements resumed after handover");
}

/* ================================================================== */
/*  Security key refresh  (Phase 3 placeholder)                       */
/* ================================================================== */

void UeRrcTask::refreshSecurityKeys()
{
    // Per TS 38.331 §5.3.5.4: During handover the UE derives a new
    // KgNB* from the current KgNB and the target PCI / DL-ARFCN.
    // In UERANSIM's simplified security model we log the event but
    // do not actually recompute keys (integrity/ciphering are simulated).
    m_logger->debug("Security key refresh (KgNB* derivation) – simulated");
}

/* ================================================================== */
/*  Execute the intra-system handover                                 */
/* ================================================================== */

void UeRrcTask::performHandover(long txId, int targetPhysCellId, int newCRNTI,
                                 int t304Ms, bool hasRachConfig)
{
    m_logger->info("Handover command: targetPCI=%d newC-RNTI=%d t304=%dms",
                   targetPhysCellId, newCRNTI, t304Ms);

    if (m_state != ERrcState::RRC_CONNECTED)
    {
        m_logger->err("Handover ignored – UE not in RRC_CONNECTED (state=%s)",
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
        m_logger->err("Handover failure: target PCI %d not found among %d detected cells",
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
    m_logger->debug("MAC reset (simulated) for handover to cell[%d]", targetCellId);

    // ---- 6. Switch serving cell ----
    auto &targetDesc = m_cellDesc[targetCellId];
    ActiveCellInfo newCellInfo{};
    newCellInfo.cellId = targetCellId;
    newCellInfo.plmn = targetDesc.sib1.plmn;
    newCellInfo.tac = targetDesc.sib1.tac;
    newCellInfo.category = ECellCategory::SUITABLE_CELL;
    m_base->shCtx.currentCell.set(newCellInfo);

    // Tell RLS to route UL via the new cell
    auto *w1 = new NmUeRrcToRls(NmUeRrcToRls::ASSIGN_CURRENT_CELL);
    w1->cellId = targetCellId;
    m_base->rlsTask->push(w1);

    m_logger->info("Serving cell switched: cell[%d] → cell[%d]",
                   previousCell.cellId, targetCellId);

    // ---- 7. RACH towards target cell (TS 38.331 §5.3.5.4) ----
    //  If rach-ConfigDedicated was provided the UE performs a contention-free
    //  RACH; otherwise it uses the common RACH config.  UERANSIM's RLS layer
    //  establishes the link implicitly via ASSIGN_CURRENT_CELL, so we only
    //  log the RACH type for protocol fidelity.
    if (hasRachConfig)
        m_logger->debug("Contention-free RACH to target cell (dedicated RACH config present)");
    else
        m_logger->debug("Contention-based RACH to target cell (no dedicated RACH config)");

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

    // ---- 9. Handover complete – stop supervision ----
    m_handoverInProgress = false;
    // (T304 will fire later and be silently ignored since the flag is cleared.)

    // ---- 10. Resume measurements on the new serving cell ----
    resumeMeasurements();

    // ---- 11. Notify NAS of the cell change ----
    auto *w2 = new NmUeRrcToNas(NmUeRrcToNas::ACTIVE_CELL_CHANGED);
    w2->previousTai = Tai{previousCell.plmn, previousCell.tac};
    m_base->nasTask->push(w2);

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
