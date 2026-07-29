//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <gnb/gtp/proto.hpp>
#include <gnb/rls/task.hpp>
#include <utils/constants.hpp>
#include <utils/libc_error.hpp>

#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestItem.h>

namespace
{

std::unique_ptr<gtp::GtpExtHeader> cloneExtHeader(const gtp::GtpExtHeader &src)
{
    switch (src.type)
    {
    case gtp::ExtHeaderType::PduSessionContainerExtHeader: {
        auto &s = static_cast<const gtp::PduSessionContainerExtHeader &>(src);
        auto dst = std::make_unique<gtp::PduSessionContainerExtHeader>();
        if (s.pduSessionInformation->pduType == gtp::PduSessionInformation::PDU_TYPE_DL)
        {
            auto &dl = static_cast<const gtp::DlPduSessionInformation &>(*s.pduSessionInformation);
            auto copy = std::make_unique<gtp::DlPduSessionInformation>();
            copy->qmp        = dl.qmp;
            copy->qfi        = dl.qfi;
            copy->rqi        = dl.rqi;
            copy->ppi        = dl.ppi;
            copy->dlSendingTs = dl.dlSendingTs;
            copy->dlQfiSeq   = dl.dlQfiSeq;
            dst->pduSessionInformation = std::move(copy);
        }
        else
        {
            auto &ul = static_cast<const gtp::UlPduSessionInformation &>(*s.pduSessionInformation);
            auto copy = std::make_unique<gtp::UlPduSessionInformation>();
            copy->qmp                 = ul.qmp;
            copy->qfi                 = ul.qfi;
            copy->dlSendingTsRepeated = ul.dlSendingTsRepeated;
            copy->dlReceivedTs        = ul.dlReceivedTs;
            copy->ulSendingTs         = ul.ulSendingTs;
            copy->dlDelayResult       = ul.dlDelayResult;
            copy->ulDelayResult       = ul.ulDelayResult;
            copy->ulQfiSeq            = ul.ulQfiSeq;
            dst->pduSessionInformation = std::move(copy);
        }
        return dst;
    }
    case gtp::ExtHeaderType::LongPdcpPduNumberExtHeader: {
        auto &s = static_cast<const gtp::LongPdcpPduNumberExtHeader &>(src);
        auto copy = std::make_unique<gtp::LongPdcpPduNumberExtHeader>();
        copy->pdcpPduNumber = s.pdcpPduNumber;
        return copy;
    }
    case gtp::ExtHeaderType::PdcpPduNumberExtHeader: {
        auto &s = static_cast<const gtp::PdcpPduNumberExtHeader &>(src);
        auto copy = std::make_unique<gtp::PdcpPduNumberExtHeader>();
        copy->pdcpPduNumber = s.pdcpPduNumber;
        return copy;
    }
    case gtp::ExtHeaderType::UdpPortExtHeader: {
        auto &s = static_cast<const gtp::UdpPortExtHeader &>(src);
        auto copy = std::make_unique<gtp::UdpPortExtHeader>();
        copy->port = s.port;
        return copy;
    }
    case gtp::ExtHeaderType::NrRanContainerExtHeader:
        return std::make_unique<gtp::NrRanContainerExtHeader>();
    }
    return nullptr;
}

} // namespace

namespace nr::gnb
{

GtpTask::GtpTask(TaskBase *base)
    : m_base{base}, m_udpServer{}, m_ueContexts{}, m_rateLimiter(std::make_unique<RateLimiter>()),
      m_sessionTree{}
{
    m_logger = m_base->logBase->makeUniqueLogger("gtp");
}

void GtpTask::onStart()
{
    try
    {
        m_logger->info("Starting GTP/UDP task.  Creating IPv4 UDP socket, local binding: IP=%s, port=%d", m_base->config->gtpIp.c_str(), cons::GtpPort);
        m_udpServer = new udp::UdpServerTask(m_base->config->gtpIp, cons::GtpPort, this);
        m_udpServer->start();
    }
    catch (const LibError &e)
    {
        m_logger->err("GTP/UDP task could not be created.  Error binding to local address IP=%s port=%d. %s", m_base->config->gtpIp.c_str(), cons::GtpPort, e.what());
    }
}

void GtpTask::onQuit()
{
    m_udpServer->quit();
    delete m_udpServer;

    m_ueContexts.clear();
}

void GtpTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::GNB_NGAP_TO_GTP: {
        auto &w = dynamic_cast<NmGnbNgapToGtp &>(*msg);
        switch (w.present)
        {
        case NmGnbNgapToGtp::UE_CONTEXT_UPDATE: {
            handleUeContextUpdate(*w.update);
            break;
        }
        case NmGnbNgapToGtp::UE_CONTEXT_RELEASE_RECEIVED: {
            handleUeContextDelete(w.ueId);
            break;
        }
        case NmGnbNgapToGtp::SESSION_CREATE: {
            handleSessionCreate(w.resource);
            break;
        }
        case NmGnbNgapToGtp::SESSION_RELEASE: {
            handleSessionRelease(w.ueId, w.psi);
            break;
        }
        case NmGnbNgapToGtp::FORWARDING_TUNNEL_SETUP: {
            handleForwardingTunnelSetup(w.ueId, w.psi, std::move(w.forwardingTunnel));
            break;
        }
        case NmGnbNgapToGtp::SESSION_UL_TUNNEL_UPDATE: {
            handleUlTunnelUpdate(w.ueId, w.psi, std::move(w.ulTunnel));
            break;
        }
        }
        break;
    }
    case NtsMessageType::GNB_XN_TO_GTP: {
        auto &w = dynamic_cast<NmGnbXnToGtp &>(*msg);
        switch (w.present)
        {
        case NmGnbXnToGtp::FORWARDING_TUNNEL_SETUP: {
            handleForwardingTunnelSetup(w.ueId, w.psi, std::move(w.forwardingTunnel));
            break;
        }
        case NmGnbXnToGtp::FORWARDING_TEID_REGISTER: {
            handleForwardingTeidRegister(w.ueId, w.psi, w.teid);
            break;
        }
        }
        break;
    }
    // uplink from UE
    case NtsMessageType::GNB_RLS_TO_GTP: {
        auto &w = dynamic_cast<NmGnbRlsToGtp &>(*msg);
        switch (w.present)
        {
        case NmGnbRlsToGtp::DATA_PDU_DELIVERY: {
            handleUplinkData(w.ueId, w.psi, w.qfi, std::move(w.pdu));
            break;
        }
        }
        break;
    }
    // downlink to UE
    case NtsMessageType::UDP_SERVER_RECEIVE:
        handleUdpReceive(dynamic_cast<udp::NwUdpServerReceive &>(*msg));
        break;
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void GtpTask::handleUeContextUpdate(const GtpUeContextUpdate &msg)
{
    m_logger->debug("UE[%ld]: Context update received. isCreate=%d, AMBR: UL=%lu bps, DL=%lu bps",
        msg.ueId, msg.isCreate, msg.ueAmbr.ulAmbr, msg.ueAmbr.dlAmbr);
        
    if (!m_ueContexts.count(msg.ueId))
    {
        m_ueContexts[msg.ueId] = std::make_unique<GtpUeContext>(msg.ueId);
        m_logger->debug("UE[%ld]: New GTP context created.", msg.ueId);
    }

    auto &ue = m_ueContexts[msg.ueId];
    ue->ueAmbr = msg.ueAmbr;

    updateAmbrForUe(ue->ueId);

    m_logger->debug("UE[%ld]: Context updated. AMBR: UL=%lu bps, DL=%lu bps", ue->ueId, ue->ueAmbr.ulAmbr, ue->ueAmbr.dlAmbr);
}

void GtpTask::handleSessionCreate(PduSessionResource *session)
{
    if (!m_ueContexts.count(session->ueId))
    {
        m_logger->err("UE[%ld] PDU session resource could not be created, UE context not found", session->ueId);
        return;
    }
    
    m_sessionTree.insertSession(session->ueId, session->psi, *session);

    updateAmbrForUe(session->ueId);
    updateAmbrForSession(session->ueId, session->psi);

    m_logger->debug("UE[%ld]: PDU session resource created. PSI[%d], sNssai SST=%d SD=%s, DownTunnelId=%d, UpTunnelId=%d",
        session->ueId, session->psi,
        (int)session->sNssai.sst,
        session->sNssai.sd.has_value() ? std::to_string((int)(uint32_t)session->sNssai.sd.value()) : "none",
        session->downTunnel.teid, session->upTunnel.teid);

}

void GtpTask::handleSessionRelease(int64_t ueId, int psi)
{
    if (!m_ueContexts.count(ueId))
    {
        m_logger->err("UE[%ld] PDU session resource could not be released, UE context not found", ueId);
        return;
    }

    // Remove all session information from rate limiter
    m_rateLimiter->updateSessionUplinkLimit(ueId, psi, 0);
    m_rateLimiter->updateSessionDownlinkLimit(ueId, psi, 0);

    // Drop any Xn-U forwarding TEID registered for this session
    for (auto it = m_incomingForwardingTeids.begin(); it != m_incomingForwardingTeids.end();)
    {
        if (it->second == UeSessionId{ueId, psi})
            it = m_incomingForwardingTeids.erase(it);
        else
            ++it;
    }

    // And remove from PDU session tree
    m_sessionTree.removeSession(ueId, psi);

    m_logger->debug("UE[%ld] PDU session resource released. PSI[%d]", ueId, psi);

}

// Applies a UL NG-U endpoint re-allocated by the 5GC at path switch (from the
// PathSwitchRequestAcknowledgeTransfer) — subsequent uplink data goes to the
// new UPF tunnel.
void GtpTask::handleUlTunnelUpdate(int64_t ueId, int psi, GtpTunnel &&tunnel)
{
    PduSessionResource *session;
    if (m_sessionTree.getSession(ueId, psi, session) != 0)
    {
        m_logger->warn("UE[%ld]: UL tunnel update for unknown PSI[%d], ignoring", ueId, psi);
        return;
    }

    m_logger->info("UE[%ld]: PSI[%d] UL NG-U tunnel updated by path switch: teid=0x%08x -> 0x%08x", ueId, psi,
                   session->upTunnel.teid, tunnel.teid);
    session->upTunnel = std::move(tunnel);
}

void GtpTask::handleForwardingTunnelSetup(int64_t ueId, int psi, GtpTunnel &&tunnel)
{
    PduSessionResource *session;
    if (m_sessionTree.getSession(ueId, psi, session) != 0)
    {
        m_logger->warn("UE[%ld]: forwarding tunnel setup for unknown PSI[%d], ignoring", ueId, psi);
        return;
    }

    uint32_t teid = tunnel.teid;
    m_forwardingTunnels[UeSessionId{ueId, psi}] = std::move(tunnel);
    m_logger->info("UE[%ld]: PSI[%d] DL forwarding enabled → teid=0x%08x", ueId, psi, teid);
}

// Target side of Xn DL data forwarding: installs a TEID this gNB advertised in
// a HandoverRequestAcknowledge, so packets relayed by the source gNB can be
// matched to the UE's session.  The registration is not gated on the session
// existing yet — the NGAP SESSION_CREATE from the provisional target context
// and this message race benignly; the session is resolved per packet.
void GtpTask::handleForwardingTeidRegister(int64_t ueId, int psi, uint32_t teid)
{
    auto existing = m_incomingForwardingTeids.find(teid);
    if (existing != m_incomingForwardingTeids.end() && !(existing->second == UeSessionId{ueId, psi}))
    {
        m_logger->warn("UE[%ld]: forwarding TEID 0x%08x already registered to UE[%ld] PSI[%d], overwriting",
                       ueId, teid, existing->second.ueId, existing->second.psi);
    }

    m_incomingForwardingTeids[teid] = UeSessionId{ueId, psi};
    m_logger->info("UE[%ld]: PSI[%d] Xn-U DL forwarding TEID 0x%08x installed", ueId, psi, teid);
}

void GtpTask::handleUeContextDelete(int64_t ueId)
{
    // Find PDU sessions of the UE
    std::vector<PduSessionResource> sessions{};
    m_sessionTree.enumerateByUe(ueId, sessions);

    int count = 0;
    for (auto &session : sessions)
    {
        // Remove all session information from rate limiter
        m_rateLimiter->updateSessionUplinkLimit(session.ueId, session.psi, 0);
        m_rateLimiter->updateSessionDownlinkLimit(session.ueId, session.psi, 0);

        // Remove any active DL forwarding tunnel for this session
        m_forwardingTunnels.erase(UeSessionId{session.ueId, session.psi});

        count++;
    }

    // Drop any Xn-U forwarding TEIDs registered for this UE (this is also the
    // teardown path for handover cancel/expiry — the provisional target context
    // is released via UE_CONTEXT_RELEASE_RECEIVED)
    for (auto it = m_incomingForwardingTeids.begin(); it != m_incomingForwardingTeids.end();)
    {
        if (it->second.ueId == ueId)
            it = m_incomingForwardingTeids.erase(it);
        else
            ++it;
    }

    // Remove all sessions from PDU session tree
    m_sessionTree.removeAllSessions(ueId);

    // Remove all user information from rate limiter
    m_rateLimiter->updateUeUplinkLimit(ueId, 0);
    m_rateLimiter->updateUeDownlinkLimit(ueId, 0);

    // Remove UE context
    m_ueContexts.erase(ueId);

    m_logger->debug("UE[%ld] Context(s) deleted [count=%d]", ueId, count);
}

// Uplink data from UE, delivery to UPF via GTP-U
void GtpTask::handleUplinkData(int64_t ueId, int psi, int qfi, OctetString &&pdu)
{


    m_logger->debug("UE[%ld]: Uplink data received. PSI[%d], QFI[%d], size=%zu", ueId, psi, qfi, pdu.length());
    const uint8_t *data = pdu.data();

    // ignore non IPv4 packets
    if ((data[0] >> 4 & 0xF) != 4)
    {
        m_logger->debug("UE[%ld]: Psi=%d - Non-IPv4 packet received (GTP code=0x%02x) and dropped.", ueId, psi, (data[0] >>4) & 0xF);
        return;
    }


    // find the PDU session for this UE and PDU session ID
    PduSessionResource *pduSession;
    int result_code = m_sessionTree.getSession(ueId, psi, pduSession);
    if (result_code != 0)
    {
        m_logger->err("UE[%ld] Uplink data failure, PDU session not found for PSI[%d]. Reason=%s", ueId, psi,
                      result_code == 1 ? "UE context not found" :
                      result_code == 2 ? "No sessions stored for this UE" :
                      result_code == 3 ? "PDU session not found for this UE and PSI" : "Unknown error");
        return;
    }

    if (m_rateLimiter->allowUplinkPacket(ueId, psi, static_cast<int64_t>(pdu.length())))
    {
        gtp::GtpMessage gtp{};
        gtp.payload = std::move(pdu);
        gtp.msgType = gtp::GtpMessage::MT_G_PDU;
        gtp.teid = pduSession->upTunnel.teid;

        auto ul = std::make_unique<gtp::UlPduSessionInformation>();
        // TODO: currently using first QSI
        ul->qfi = !pduSession->qosFlows.empty() ? pduSession->qosFlows[0].qfi : 1;

        auto cont = std::make_unique<gtp::PduSessionContainerExtHeader>();
        cont->pduSessionInformation = std::move(ul);
        gtp.extHeaders.push_back(std::move(cont));

        OctetString gtpPdu;
        if (!gtp::EncodeGtpMessage(gtp, gtpPdu))
            m_logger->err("UE[%ld] Uplink data failure, GTP encoding failed", ueId);
        else
        {
            auto ip_addr = InetAddress(pduSession->upTunnel.address, cons::GtpPort);
            m_logger->debug("UE[%ld] Uplink GTP data sent. ip=[%s], port=[%d], UpTunnelId=[%u]", ueId, ip_addr.getIpAddrString().c_str(), cons::GtpPort, gtp.teid);

            m_udpServer->send(ip_addr, gtpPdu);
        }
    }
    else 
    {
        m_logger->debug("UE[%ld] Uplink packet for PSI=%d dropped by rate limiter. Payload_size=[%zu]", ueId, psi, pdu.length());
    }
}

// Downlink PDU delivery to UE
void GtpTask::handleUdpReceive(const udp::NwUdpServerReceive &msg)
{
    OctetView buffer{msg.packet};
    auto gtp = gtp::DecodeGtpMessage(buffer);

    switch (gtp->msgType)
    {
    case gtp::GtpMessage::MT_G_PDU: {
        // A packet on a registered Xn-U forwarding TEID is DL data relayed from
        // the source gNB during handover; deliver it straight to the UE via RLS.
        {
            auto fwdIt = m_incomingForwardingTeids.find(gtp->teid);
            if (fwdIt != m_incomingForwardingTeids.end())
            {
                int fwdQfi = -1;
                for (auto &ext : gtp->extHeaders)
                {
                    if (ext->type == gtp::ExtHeaderType::PduSessionContainerExtHeader)
                    {
                        auto &pduCont = dynamic_cast<gtp::PduSessionContainerExtHeader &>(*ext);
                        if (pduCont.pduSessionInformation->pduType == gtp::PduSessionInformation::PDU_TYPE_DL)
                            fwdQfi = dynamic_cast<gtp::DlPduSessionInformation &>(*pduCont.pduSessionInformation).qfi;
                    }
                }

                m_logger->debug("UE[%ld]: Forwarded DL data received for PSI=%d. TEID=[%u], payload_size=[%zu] QFI=%d",
                                fwdIt->second.ueId, fwdIt->second.psi, gtp->teid, gtp->payload.length(), fwdQfi);

                auto w = std::make_unique<NmGnbGtpToRls>(NmGnbGtpToRls::DATA_PDU_DELIVERY);
                w->ueId = fwdIt->second.ueId;
                w->psi = fwdIt->second.psi;
                w->qfi = fwdQfi;
                w->pdu = std::move(gtp->payload);
                m_base->rlsTask->push(std::move(w));
                return;
            }
        }

        // find the session Id using the TEID in the GTP header
        auto ueSessionId = m_sessionTree.findByDownTeid(gtp->teid);
        if (ueSessionId == nullptr)
        {
            m_logger->err("TEID %d not found on GTP-U Downlink", gtp->teid);
            return;
        }

        // get the QFI for this packet from the GTP-U extension header, if it exists
        int qfi = -1;
        for (auto &ext : gtp->extHeaders)
        {
            if (ext->type == gtp::ExtHeaderType::PduSessionContainerExtHeader)
            {
                auto &pduCont = dynamic_cast<gtp::PduSessionContainerExtHeader &>(*ext);
                // this should be a downlink packet
                if (pduCont.pduSessionInformation->pduType == gtp::PduSessionInformation::PDU_TYPE_DL)
                {
                    auto &dlInfo = dynamic_cast<gtp::DlPduSessionInformation &>(*pduCont.pduSessionInformation);
                    qfi = dlInfo.qfi;
                }
            }
        }

        // get the sequence number from the GTP-U header
        uint32_t seq = gtp->seq.has_value() ? gtp->seq.value() : 0;

        m_logger->debug("UE[%ld]: Downlink GTP data received for PSI=%d. TEID=[%u], payload_size=[%zu] seq=%d  QFI=%d", ueSessionId->ueId, ueSessionId->psi, gtp->teid, gtp->payload.length(), seq, qfi);

        // If session is in DL forwarding mode, relay the packet to the target gNB tunnel and skip RLS delivery
        UeSessionId fwdKey{ueSessionId->ueId, ueSessionId->psi};
        auto fwdIt = m_forwardingTunnels.find(fwdKey);
        if (fwdIt != m_forwardingTunnels.end())
        {
            gtp::GtpMessage fwd{};
            fwd.msgType = gtp::GtpMessage::MT_G_PDU;
            fwd.teid    = fwdIt->second.teid;
            fwd.payload = std::move(gtp->payload);
            for (auto &ext : gtp->extHeaders)
            {
                auto clone = cloneExtHeader(*ext);
                if (clone)
                    fwd.extHeaders.push_back(std::move(clone));
            }

            OctetString fwdPdu;
            if (gtp::EncodeGtpMessage(fwd, fwdPdu))
            {
                m_udpServer->send(InetAddress(fwdIt->second.address, cons::GtpPort), fwdPdu);
                m_logger->debug("UE[%ld]: PSI=%d packet forwarded to target teid=0x%08x", ueSessionId->ueId, ueSessionId->psi, fwd.teid);
            }
            else
                m_logger->err("UE[%ld]: DL forwarding encode failed for PSI=%d", ueSessionId->ueId, ueSessionId->psi);
            return;
        }

        // apply the brute-force rate limiter - drops packets if the session or UE has exceeded the AMBR
        if (!m_rateLimiter->allowDownlinkPacket(ueSessionId->ueId, ueSessionId->psi, gtp->payload.length()))
        {
            m_logger->debug("UE[%ld]: Downlink packet for PSI=%d dropped by rate limiter.  Payload_size=[%zu]", ueSessionId->ueId, ueSessionId->psi, gtp->payload.length());
            return;
        }
            
        // send to RLS for transport to UE
        auto w = std::make_unique<NmGnbGtpToRls>(NmGnbGtpToRls::DATA_PDU_DELIVERY);
        w->ueId = ueSessionId->ueId;
        w->psi = ueSessionId->psi;
        w->qfi = qfi;
        w->pdu = std::move(gtp->payload);
        m_base->rlsTask->push(std::move(w));
        return;
    }
    case gtp::GtpMessage::MT_ECHO_REQUEST: {
        gtp::GtpMessage gtpResponse{};
        gtpResponse.msgType = gtp::GtpMessage::MT_ECHO_RESPONSE;
        gtpResponse.seq = gtp->seq;
        gtpResponse.payload = OctetString::FromOctet2({14, 0});

        OctetString gtpPdu;
        if (gtp::EncodeGtpMessage(gtpResponse, gtpPdu))
            m_udpServer->send(msg.fromAddress, gtpPdu);
        else
            m_logger->err("Uplink data failure, GTP encoding failed");
        return;
    }
    case gtp::GtpMessage::MT_END_MARKER: {
        m_logger->debug("Received GTP-U End Marker for TEID %u", gtp->teid);

        // End Marker arriving at the target on a forwarding TEID marks the end
        // of DL data relayed from the source gNB — deregister the tunnel.
        {
            auto fwdIt = m_incomingForwardingTeids.find(gtp->teid);
            if (fwdIt != m_incomingForwardingTeids.end())
            {
                m_logger->info("UE[%ld]: PSI=%d End Marker received on Xn-U forwarding tunnel, DL forwarding complete",
                               fwdIt->second.ueId, fwdIt->second.psi);
                m_incomingForwardingTeids.erase(fwdIt);
                return;
            }
        }

        auto *endSession = m_sessionTree.findByDownTeid(gtp->teid);
        if (!endSession)
        {
            m_logger->warn("End Marker received for unknown TEID %u", gtp->teid);
            return;
        }

        UeSessionId key{endSession->ueId, endSession->psi};
        auto it = m_forwardingTunnels.find(key);
        if (it == m_forwardingTunnels.end())
        {
            m_logger->debug("UE[%ld]: End Marker for PSI=%d, no forwarding tunnel active — ignoring",
                            endSession->ueId, endSession->psi);
            return;
        }

        gtp::GtpMessage em{};
        em.msgType = gtp::GtpMessage::MT_END_MARKER;
        em.teid    = it->second.teid;

        OctetString emPdu;
        if (gtp::EncodeGtpMessage(em, emPdu))
            m_udpServer->send(InetAddress(it->second.address, cons::GtpPort), emPdu);
        else
            m_logger->err("UE[%ld]: End Marker encode failed for PSI=%d", endSession->ueId, endSession->psi);

        m_forwardingTunnels.erase(it);
        m_logger->info("UE[%ld]: PSI=%d End Marker forwarded to target, DL forwarding complete",
                       endSession->ueId, endSession->psi);
        return;
    }
    default: {
        m_logger->err("Unhandled GTP-U message type: %d", gtp->msgType);
        return;
    }
    }
}

void GtpTask::updateAmbrForUe(int64_t ueId)
{
    if (!m_ueContexts.count(ueId))
        return;

    auto &ue = m_ueContexts[ueId];
    m_rateLimiter->updateUeUplinkLimit(ueId, ue->ueAmbr.ulAmbr);
    m_rateLimiter->updateUeDownlinkLimit(ueId, ue->ueAmbr.dlAmbr);
}

void GtpTask::updateAmbrForSession(int64_t ueId, int psi)
{

    PduSessionResource *session;
    if (!m_sessionTree.getSession(ueId, psi, session))
        return;

    m_rateLimiter->updateSessionUplinkLimit(ueId, psi, session->sessionAmbr.ulAmbr);
    m_rateLimiter->updateSessionDownlinkLimit(ueId, psi, session->sessionAmbr.dlAmbr);
}

// returns a copy of the current UE context for the given UE ID.
//  If no context exists for the given UE ID, returns false.
bool GtpTask::getUeContext(int64_t ueId, std::optional<GtpUeContext> &out)
{
    auto it = m_ueContexts.find(ueId);
    if (it != m_ueContexts.end())
    {
        out.emplace(*it->second);
        return true;
    }
    return false;
}

PduSessionResource *GtpTask::getPduSession(int64_t ueId, int psi)
{
    PduSessionResource *session;
    if (m_sessionTree.getSession(ueId, psi, session))
        return session;
    return nullptr;
}

// Returns copies of the UE's PDU sessions. Copies rather than pointers into
// m_sessionTree: the caller is another task's thread (XnTask, gathering the
// handover context) and may hold these well past the point GTP could release
// or reallocate the underlying session.
bool GtpTask::getPduSessions(int64_t ueId, std::vector<PduSessionResource> &out)
{
    m_sessionTree.enumerateByUe(ueId, out);
    return !out.empty();
}



} // namespace nr::gnb
