//
// NGAP Handover Procedures (Xn / gNB-gNB)
//
// Implements:
//   - sendPathSwitchRequest()              → target gNB → AMF
//   - receivePathSwitchRequestAcknowledge() ← AMF → target gNB
//   - receivePathSwitchRequestFailure()     ← AMF → target gNB
//

#include "encode.hpp"
#include "task.hpp"
#include "utils.hpp"

#include <gnb/rrc/task.hpp>
#include <gnb/gtp/task.hpp>
#include <utils/common.hpp>

#include <cstring>

#include <asn/ngap/ASN_NGAP_GTPTunnel.h>
#include <asn/ngap/ASN_NGAP_NR-CGI.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleasedItemPSAck.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSwitchedItem.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceToBeSwitchedDLItem.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequest.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestAcknowledge.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestAcknowledgeTransfer.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestFailure.h>
#include <asn/ngap/ASN_NGAP_PathSwitchRequestTransfer.h>
#include <asn/ngap/ASN_NGAP_ProtocolIE-Field.h>
#include <asn/ngap/ASN_NGAP_QosFlowAcceptedItem.h>
#include <asn/ngap/ASN_NGAP_SecurityContext.h>
#include <asn/ngap/ASN_NGAP_TAI.h>
#include <asn/ngap/ASN_NGAP_UESecurityCapabilities.h>
#include <asn/ngap/ASN_NGAP_UPTransportLayerInformation.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformation.h>
#include <asn/ngap/ASN_NGAP_UserLocationInformationNR.h>

const int HANDOVER_TIMEOUT_MS = 5000;

namespace nr::gnb
{

void NgapTask::prepareXnHandover(int64_t ueId,
                                 std::unique_ptr<XnHandoverCoreContext> core,
                                 std::unique_ptr<std::vector<PduSessionResource>> sessions)
{
    if (!core)
    {
        m_logger->err("UE[%ld] Xn handover preparation missing core context", ueId);
        return;
    }

    // Resolve the transferred GUAMI against the AMFs connected to this target.
    // The AMF-UE-NGAP-ID is remote scope and must not be mistaken for amfId.
    NgapAmfContext *selectedAmf = nullptr;
    for (auto &[id, amf] : m_amfCtx)
    {
        if (!amf || amf->state != EAmfState::CONNECTED)
            continue;
        for (const auto *served : amf->servedGuamiList)
        {
            if (served && served->guami.plmn == core->guami.plmn &&
                served->guami.amfRegionId == core->guami.amfRegionId &&
                served->guami.amfSetId == core->guami.amfSetId &&
                served->guami.amfPointer == core->guami.amfPointer)
            {
                selectedAmf = amf;
                break;
            }
        }
        if (selectedAmf)
            break;
    }
    if (!selectedAmf)
    {
        m_logger->err("UE[%ld] no connected AMF serves transferred Xn GUAMI", ueId);
        return;
    }

    XnNgapHandoverPending pending{};
    pending.ctx = std::make_unique<NgapUeContext>(ueId);
    pending.ctx->connectionState = UE_NGAP_CONNECTION_STATE::NGAP_CONNECTION_PENDING;
    pending.ctx->ranUeNgapId = generateRanUeNgapId(ueId);
    pending.ctx->amfUeNgapId = core->amfUeNgapId;
    pending.ctx->associatedAmfId = selectedAmf->ctxId;
    pending.ctx->ueAmbr = core->ueAmbr;
    pending.ctx->ueSecInfo = core->ueSecInfo;
    if (sessions)
        pending.sessions = std::move(*sessions);

    // Replace only provisional state; an active context with this ueId belongs
    // to another procedure and must never be silently overwritten.
    if (m_ueCtx.count(ueId))
    {
        m_logger->err("UE[%ld] active NGAP context already exists during Xn preparation", ueId);
        return;
    }
    m_xnHandoversPending[ueId] = std::move(pending);
    auto &stored = m_xnHandoversPending.at(ueId);

    auto update = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_UPDATE);
    update->update = std::make_unique<GtpUeContextUpdate>(true, ueId, stored.ctx->ueAmbr);
    m_base->gtpTask->push(std::move(update));

    for (auto &resource : stored.sessions)
    {
        // Xn decoded these with the source ID; normalize before GTP indexes the
        // session.  The vector remains in the pending map, keeping the borrowed
        // pointer valid until GTP consumes its queued message.
        resource.ueId = ueId;
        auto failure = setupPduSessionResource(stored.ctx.get(), &resource);
        if (failure)
            m_logger->warn("UE[%ld] Xn target rejected PSI[%d], cause=%d", ueId, resource.psi,
                           static_cast<int>(*failure));
    }

    m_logger->info("UE[%ld] provisional Xn NGAP/GTP context prepared with %zu sessions",
                   ueId, stored.ctx->pduSessions.size());
}

bool NgapTask::activateXnHandover(int64_t ueId)
{
    auto pending = m_xnHandoversPending.find(ueId);
    if (pending == m_xnHandoversPending.end() || !pending->second.ctx)
    {
        m_logger->err("UE[%ld] cannot activate missing provisional Xn NGAP context", ueId);
        return false;
    }
    if (m_ueCtx.count(ueId))
    {
        m_logger->err("UE[%ld] refusing to replace active NGAP context during Xn activation", ueId);
        return false;
    }

    pending->second.ctx->connectionState = UE_NGAP_CONNECTION_STATE::NGAP_CONNECTED;
    m_ueCtx[ueId] = pending->second.ctx.release();
    m_xnHandoversPending.erase(pending);
    m_logger->info("UE[%ld] provisional Xn NGAP context activated", ueId);
    return true;
}

void NgapTask::releaseXnSourceContext(int64_t ueId)
{
    auto active = m_ueCtx.find(ueId);
    if (active == m_ueCtx.end())
    {
        // UE Context Release is idempotent; a retransmission after successful
        // cleanup must not recreate state or trigger an RRC release.
        m_logger->warn("UE[%ld] Xn source NGAP context already released", ueId);
        return;
    }

    auto gtp = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_RELEASE_RECEIVED);
    gtp->ueId = ueId;
    gtp->cause = NgapCause::RadioNetwork_successful_handover;
    m_base->gtpTask->push(std::move(gtp));
    delete active->second;
    m_ueCtx.erase(active);
    m_logger->info("UE[%ld] released source NGAP/GTP context after Xn path switch", ueId);
}

void NgapTask::cancelXnTargetPreparation(int64_t ueId)
{
    auto pending = m_xnHandoversPending.find(ueId);
    if (pending == m_xnHandoversPending.end())
    {
        m_logger->debug("UE[%ld] provisional Xn NGAP context already absent", ueId);
        return;
    }
    m_xnHandoversPending.erase(pending);
    auto gtp = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::UE_CONTEXT_RELEASE_RECEIVED);
    gtp->ueId = ueId;
    gtp->cause = NgapCause::RadioNetwork_handover_cancelled;
    m_base->gtpTask->push(std::move(gtp));
    m_logger->info("UE[%ld] cancelled provisional Xn NGAP/GTP preparation", ueId);
}


// Build the per-session PathSwitchRequestTransfer (TS 38.413 §9.3.4.9):
// the target's DL NG-U endpoint (this gNB's tunnel toward the UPF) and the
// QoS flows accepted at the target.  Returned as the APER-encoded blob the
// PDUSessionResourceToBeSwitchedDLItem carries.
static OctetString MakePathSwitchRequestTransfer(const PduSessionResource &resource)
{
    auto *tr = asn::New<ASN_NGAP_PathSwitchRequestTransfer>();

    auto &upInfo = tr->dL_NGU_UP_TNLInformation;
    upInfo.present = ASN_NGAP_UPTransportLayerInformation_PR_gTPTunnel;
    upInfo.choice.gTPTunnel = asn::New<ASN_NGAP_GTPTunnel>();
    asn::SetBitString(upInfo.choice.gTPTunnel->transportLayerAddress, resource.downTunnel.address);
    asn::SetOctetString4(upInfo.choice.gTPTunnel->gTP_TEID, (octet4)resource.downTunnel.teid);

    for (const auto &flow : resource.qosFlows)
    {
        auto *item = asn::New<ASN_NGAP_QosFlowAcceptedItem>();
        item->qosFlowIdentifier = flow.qfi;
        asn::SequenceAdd(tr->qosFlowAcceptedList, item);
    }

    OctetString encoded = ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PathSwitchRequestTransfer, tr);
    asn::Free(asn_DEF_ASN_NGAP_PathSwitchRequestTransfer, tr);
    return encoded;
}

/**
 * @brief Target GNB sends a PathSwitchRequest to the AMF to request switching the UE's path
 * to the new target gNB after handover completion.  Only used in Xn handover, and must NOT
 * be sent in N2 handover as the path switch is implicitly triggered by the HandoverNotify.
 *
 * Carries the mandatory IEs per TS 38.413 §9.2.3.21: RAN-UE-NGAP-ID and
 * UserLocationInformation (auto-inserted by sendNgapUeAssociated),
 * SourceAMF-UE-NGAP-ID (id 100 — added here; the generic id-10 auto-insert is
 * suppressed for this message), UESecurityCapabilities, and
 * PDUSessionResourceToBeSwitchedDLList with a PathSwitchRequestTransfer per
 * session so the 5GC re-points the UPF's DL path at this gNB's tunnels.
 *
 * @param ueId
 */
void NgapTask::sendPathSwitchRequest(int64_t ueId)
{
    m_logger->info("UE[%ld] Sending PathSwitchRequest", ueId);

    // RRC arrival is the activation boundary for Xn target state.  Promote it
    // immediately before constructing the first UE-associated NGAP message.
    if (!m_ueCtx.count(ueId) && !activateXnHandover(ueId))
        return;
    auto *ue = findUeContext(ueId);
    if (!ue)
    {
        m_logger->err("sendPathSwitchRequest: UE context not found UE[%ld] ", ueId);
        return;
    }

    // The per-session data (target DL tunnels, accepted QoS flows) lives in the
    // GTP task's session tree, populated during prepareXnHandover().  Direct
    // cross-thread read — same unsynchronized-accessor pattern the Xn task uses
    // (see Xn_summary issue 19).
    std::vector<PduSessionResource *> sessions;
    if (!m_base->gtpTask->getPduSessions(ueId, sessions) || sessions.empty())
    {
        m_logger->err("UE[%ld] PathSwitchRequest aborted: no PDU sessions found in GTP", ueId);
        return;
    }

    // Encode all transfers before allocating any IEs so a failure aborts cleanly.
    std::vector<std::pair<int, OctetString>> transfers;
    for (auto *resource : sessions)
    {
        OctetString transfer = MakePathSwitchRequestTransfer(*resource);
        if (transfer.length() == 0)
        {
            m_logger->err("UE[%ld] PathSwitchRequest aborted: transfer encode failed for PSI[%d]", ueId,
                          resource->psi);
            return;
        }
        transfers.emplace_back(resource->psi, std::move(transfer));
    }

    std::vector<ASN_NGAP_ProtocolIE_Field_13561P108 *> ies;

    // IE: SourceAMF-UE-NGAP-ID (id 100, mandatory, reject).  PathSwitchRequest
    // does not use the generic AMF-UE-NGAP-ID (id 10) IE.
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P108>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_SourceAMF_UE_NGAP_ID;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P108__value_PR_AMF_UE_NGAP_ID;
        asn::SetSigned64(ue->amfUeNgapId, ie->value.choice.AMF_UE_NGAP_ID);
        ies.push_back(ie);
    }

    // IE: UserLocationInformation
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P108>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_UserLocationInformation;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P108__value_PR_UserLocationInformation;

        ie->value.choice.UserLocationInformation.present =
            ASN_NGAP_UserLocationInformation_PR_userLocationInformationNR;
        auto *nrLoc = asn::New<ASN_NGAP_UserLocationInformationNR>();

        asn::SetOctetString3(nrLoc->nR_CGI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetBitStringLong<36>(m_base->config->nci, nrLoc->nR_CGI.nRCellIdentity);
        asn::SetOctetString3(nrLoc->tAI.pLMNIdentity,
                             ngap_utils::PlmnToOctet3(m_base->config->plmn));
        asn::SetOctetString3(nrLoc->tAI.tAC, octet3{m_base->config->tac});

        ie->value.choice.UserLocationInformation.choice.userLocationInformationNR = nrLoc;
        ies.push_back(ie);
    }

    // IE: UESecurityCapabilities (mandatory, ignore) — transferred from the
    // source gNB over Xn and stored in the provisional context.
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P108>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_UESecurityCapabilities;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P108__value_PR_UESecurityCapabilities;

        auto &sc = ie->value.choice.UESecurityCapabilities;
        asn::SetBitStringInt<16>(ue->ueSecInfo.nRencryptionAlgorithmsBitmap, sc.nRencryptionAlgorithms);
        asn::SetBitStringInt<16>(ue->ueSecInfo.nRintegrityProtectionAlgorithmsBitmap,
                                 sc.nRintegrityProtectionAlgorithms);
        asn::SetBitStringInt<16>(ue->ueSecInfo.eUTRAencryptionAlgorithmsBitmap, sc.eUTRAencryptionAlgorithms);
        asn::SetBitStringInt<16>(ue->ueSecInfo.eUTRAintegrityProtectionAlgorithmsBitmap,
                                 sc.eUTRAintegrityProtectionAlgorithms);
        ies.push_back(ie);
    }

    // IE: PDUSessionResourceToBeSwitchedDLList (mandatory, reject)
    {
        auto *ie = asn::New<ASN_NGAP_ProtocolIE_Field_13561P108>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceToBeSwitchedDLList;
        ie->criticality = ASN_NGAP_Criticality_reject;
        ie->value.present = ASN_NGAP_ProtocolIE_Field_13561P108__value_PR_PDUSessionResourceToBeSwitchedDLList;

        for (auto &[psi, transfer] : transfers)
        {
            auto *item = asn::New<ASN_NGAP_PDUSessionResourceToBeSwitchedDLItem>();
            item->pDUSessionID = static_cast<ASN_NGAP_PDUSessionID_t>(psi);
            asn::SetOctetString(item->pathSwitchRequestTransfer, transfer);
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceToBeSwitchedDLList, item);
        }
        ies.push_back(ie);
    }

    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_PathSwitchRequest>(ies);
    sendNgapUeAssociated(ue->ctxId, pdu);

    m_logger->info("UE[%ld] PathSwitchRequest sent to AMF (%zu PDU sessions)", ueId, transfers.size());
}

/**
 * @brief Target GNB receives the PathSwitchRequestAcknowledge from the AMF to indicate that
 * the path switch has been completed.  Only used for Xn handover.
 *
 * Processes the Ack's content per TS 38.413 §9.2.3.22 before notifying RRC:
 *  - SecurityContext (mandatory): the fresh {NCC, NH} pair, forwarded to RRC
 *    for storage in the UE context (used by the next handover this gNB sources).
 *  - PDUSessionResourceSwitchedList: per-session AcknowledgeTransfer; when the
 *    5GC re-allocated the UL NG-U endpoint at path switch, the new UPF tunnel
 *    is applied to the GTP session so uplink follows the switched path.
 *  - PDUSessionResourceReleasedListPSAck: sessions the 5GC dropped at path
 *    switch are released in GTP (RRC/RLS bearer teardown for these is TODO).
 *
 * @param amfId
 * @param msg
 */
void NgapTask::receivePathSwitchRequestAcknowledge(int amfId, ASN_NGAP_PathSwitchRequestAcknowledge *msg)
{
    m_logger->info("PathSwitchRequestAcknowledge received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receivePathSwitchRequestAcknowledge: UE not found");
        return;
    }

    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK);
    w->ueId = ue->ctxId;

    // --- SecurityContext (mandatory): fresh {NCC, NH} for the next handover ---
    auto *ieSec = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_SecurityContext);
    if (ieSec != nullptr)
    {
        const auto &sec = ieSec->SecurityContext;
        w->hasSecurityContext = true;
        w->nextHopChainingCount = static_cast<int>(sec.nextHopChainingCount);
        // nextHopNH is a 256-bit SecurityKey BIT STRING
        if (sec.nextHopNH.buf != nullptr && sec.nextHopNH.size >= 32)
            std::memcpy(w->nextHopParameter.data(), sec.nextHopNH.buf, 32);
        else
            m_logger->warn("UE[%ld] path switch SecurityContext has malformed NH (size=%d)", ue->ctxId,
                           (int)sec.nextHopNH.size);
    }
    else
    {
        // Mandatory per spec; tolerate its absence (e.g. simplified test cores)
        m_logger->warn("UE[%ld] PathSwitchRequestAcknowledge without SecurityContext IE", ue->ctxId);
    }

    // --- PDUSessionResourceSwitchedList: apply re-allocated UL NG-U endpoints ---
    auto *ieSwitched = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSwitchedList);
    if (ieSwitched != nullptr)
    {
        auto &list = ieSwitched->PDUSessionResourceSwitchedList.list;
        for (int i = 0; i < list.count; i++)
        {
            auto *item = list.array[i];
            if (item == nullptr)
                continue;

            const int psi = static_cast<int>(item->pDUSessionID);
            const auto &blob = item->pathSwitchRequestAcknowledgeTransfer;
            if (blob.buf == nullptr || blob.size <= 0)
                continue;

            auto *transfer = ngap_encode::Decode<ASN_NGAP_PathSwitchRequestAcknowledgeTransfer>(
                asn_DEF_ASN_NGAP_PathSwitchRequestAcknowledgeTransfer, blob.buf, static_cast<size_t>(blob.size));
            if (transfer == nullptr)
            {
                m_logger->warn("UE[%ld] PSI[%d]: cannot decode PathSwitchRequestAcknowledgeTransfer", ue->ctxId,
                               psi);
                continue;
            }

            if (transfer->uL_NGU_UP_TNLInformation != nullptr &&
                transfer->uL_NGU_UP_TNLInformation->present ==
                    ASN_NGAP_UPTransportLayerInformation_PR_gTPTunnel &&
                transfer->uL_NGU_UP_TNLInformation->choice.gTPTunnel != nullptr)
            {
                auto *t = transfer->uL_NGU_UP_TNLInformation->choice.gTPTunnel;
                GtpTunnel ulTunnel;
                ulTunnel.address = asn::GetOctetString(t->transportLayerAddress);
                ulTunnel.teid = static_cast<uint32_t>(asn::GetOctet4(t->gTP_TEID));

                m_logger->info("UE[%ld] PSI[%d]: 5GC re-allocated UL NG-U endpoint (teid=0x%08x); updating GTP",
                               ue->ctxId, psi, ulTunnel.teid);

                auto gm = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::SESSION_UL_TUNNEL_UPDATE);
                gm->ueId = ue->ctxId;
                gm->psi = psi;
                gm->ulTunnel = std::move(ulTunnel);
                m_base->gtpTask->push(std::move(gm));
            }

            asn::Free(asn_DEF_ASN_NGAP_PathSwitchRequestAcknowledgeTransfer, transfer);
        }
    }

    // --- PDUSessionResourceReleasedListPSAck: sessions dropped by the 5GC ---
    std::vector<int> releasedPsis;
    auto *ieReleased =
        asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceReleasedListPSAck);
    if (ieReleased != nullptr)
    {
        auto &list = ieReleased->PDUSessionResourceReleasedListPSAck.list;
        for (int i = 0; i < list.count; i++)
        {
            auto *item = list.array[i];
            if (item == nullptr)
                continue;

            const int psi = static_cast<int>(item->pDUSessionID);
            m_logger->warn("UE[%ld] PSI[%d] released by 5GC at path switch; releasing GTP session", ue->ctxId,
                           psi);

            ue->pduSessions.erase(psi);
            releasedPsis.push_back(psi);
            auto gm = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::SESSION_RELEASE);
            gm->ueId = ue->ctxId;
            gm->psi = psi;
            m_base->gtpTask->push(std::move(gm));
        }
    }

    m_logger->info("UE[%ld] Path switch complete. AMF path updated.", ue->ctxId);

    // Have RRC tear down the radio bearers (RLS + UE) for the released sessions.
    if (!releasedPsis.empty())
    {
        auto rm = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::PDU_SESSION_UPDATE);
        rm->ueId = ue->ctxId;
        rm->releasedPsis = std::move(releasedPsis);
        m_base->rrcTask->push(std::move(rm));
    }

    // Notify RRC that path switch is acknowledged (carries the security context)
    m_base->rrcTask->push(std::move(w));
}

/**
 * @brief Target GNB receives the PathSwitchRequestFailure from the AMF to indicate that 
 * the path switch has failed.  Only used for Xn handover. The target gNB should keep the 
 * UE on the source cell and may choose to retry the path switch or trigger a new handover 
 * if necessary.
 * 
 * @param amfId 
 * @param msg 
 */
void NgapTask::receivePathSwitchRequestFailure(int amfId, ASN_NGAP_PathSwitchRequestFailure *msg)
{
    m_logger->warn("PathSwitchRequestFailure received from AMF");

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (!ue)
    {
        m_logger->err("receivePathSwitchRequestFailure: UE not found");
        return;
    }

    m_logger->warn("UE[%ld] Path switch failed.", ue->ctxId);

    // Notify RRC
    auto w = std::make_unique<NmGnbNgapToRrc>(NmGnbNgapToRrc::PATH_SWITCH_REQUEST_FAILURE);
    w->ueId = ue->ctxId;
    m_base->rrcTask->push(std::move(w));

}

} // namespace nr::gnb
