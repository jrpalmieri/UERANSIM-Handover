//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"
#include "cmd_handler.hpp"
#include <lib/nas/utils.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <ue/tun/tun.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>

#include <cstdio>
#include <string>

namespace {

struct TunPacketInfo
{
    bool valid{false};
    std::string srcIp;
    std::string dstIp;
    std::string proto;
    uint16_t srcPort{0};
    uint16_t dstPort{0};
};

static TunPacketInfo ParseTunPacket(const OctetString &data)
{
    TunPacketInfo info;
    const uint8_t *p = data.data();
    int len = data.length();

    if (len < 20 || (p[0] >> 4) != 4)
        return info;

    int ihl = (p[0] & 0x0F) * 4;
    if (len < ihl)
        return info;

    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", p[12], p[13], p[14], p[15]);
    info.srcIp = buf;
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", p[16], p[17], p[18], p[19]);
    info.dstIp = buf;

    uint8_t proto = p[9];
    switch (proto)
    {
    case 1:  info.proto = "ICMP"; break;
    case 6:  info.proto = "TCP";  break;
    case 17: info.proto = "UDP";  break;
    default: { char pb[12]; snprintf(pb, sizeof(pb), "IP(%u)", proto); info.proto = pb; break; }
    }

    if ((proto == 6 || proto == 17) && len >= ihl + 4)
    {
        info.srcPort = static_cast<uint16_t>((p[ihl] << 8) | p[ihl + 1]);
        info.dstPort = static_cast<uint16_t>((p[ihl + 2] << 8) | p[ihl + 3]);
    }

    info.valid = true;
    return info;
}

static std::string FormatPacketDesc(const TunPacketInfo &pkt)
{
    if (!pkt.valid)
        return "?";
    char buf[72];
    if (pkt.srcPort == 0 && pkt.dstPort == 0)
        snprintf(buf, sizeof(buf), "%s %s->%s", pkt.proto.c_str(), pkt.srcIp.c_str(), pkt.dstIp.c_str());
    else
        snprintf(buf, sizeof(buf), "%s %s:%u->%s:%u", pkt.proto.c_str(), pkt.srcIp.c_str(), pkt.srcPort,
                 pkt.dstIp.c_str(), pkt.dstPort);
    return buf;
}

} // namespace

static constexpr const int SWITCH_OFF_TIMER_ID = 1;
static constexpr const int SWITCH_OFF_DELAY = 500;

namespace nr::ue
{

UeAppTask::UeAppTask(TaskBase *base) : m_base{base}
{
    m_logger = m_base->logBase->makeUniqueLogger(m_base->config->getLoggerPrefix() + "app");
}

void UeAppTask::onStart()
{
}

void UeAppTask::onQuit()
{
    for (auto &tunTask : m_tunTasks)
    {
        if (tunTask != nullptr)
        {
            tunTask->quit();
            delete tunTask;
            tunTask = nullptr;
        }
    }
}

void UeAppTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::UE_TUN_TO_APP: {
        auto &w = dynamic_cast<NmUeTunToApp &>(*msg);
        switch (w.present)
        {
        case NmUeTunToApp::DATA_PDU_DELIVERY: {
            auto pktInfo = ParseTunPacket(w.data);
            m_logger->debug("Uplink PDU[%d] %s size=%zu", w.psi, FormatPacketDesc(pktInfo).c_str(), w.data.length());
            auto m = std::make_unique<NmUeAppToNas>(NmUeAppToNas::UPLINK_DATA_DELIVERY);
            m->psi = w.psi;
            m->data = std::move(w.data);
            m_base->nasTask->push(std::move(m));
            break;
        }
        case NmUeTunToApp::TUN_ERROR: {
            m_logger->err("TUN failure [%s]", w.error.c_str());
            break;
        }
        }
        break;
    }
    case NtsMessageType::UE_NAS_TO_APP: {
        auto &w = dynamic_cast<NmUeNasToApp &>(*msg);
        switch (w.present)
        {
        case NmUeNasToApp::PERFORM_SWITCH_OFF: {
            setTimer(SWITCH_OFF_TIMER_ID, SWITCH_OFF_DELAY);
            break;
        }
        case NmUeNasToApp::DOWNLINK_DATA_DELIVERY: {
            auto *tunTask = m_tunTasks[w.psi];
            if (tunTask)
            {
                auto pktInfo = ParseTunPacket(w.data);
                m_logger->debug("Downlink PDU[%d] %s size=%zu", w.psi, FormatPacketDesc(pktInfo).c_str(), w.data.length());
                auto m = std::make_unique<NmAppToTun>(NmAppToTun::DATA_PDU_DELIVERY);
                m->psi = w.psi;
                m->data = std::move(w.data);
                tunTask->push(std::move(m));
            }
            break;
        }
        }
        break;
    }
    case NtsMessageType::UE_STATUS_UPDATE: {
        receiveStatusUpdate(dynamic_cast<NmUeStatusUpdate &>(*msg));
        break;
    }
    case NtsMessageType::UE_CLI_COMMAND: {
        auto &w = dynamic_cast<NmUeCliCommand &>(*msg);
        UeCmdHandler handler{m_base};
        handler.handleCmd(w);
        break;
    }
    case NtsMessageType::TIMER_EXPIRED: {
        auto &w = dynamic_cast<NmTimerExpired &>(*msg);
        if (w.timerId == SWITCH_OFF_TIMER_ID)
        {
            m_logger->info("UE device is switching off");
            m_base->ueController->performSwitchOff(m_base->ue);
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void UeAppTask::receiveStatusUpdate(NmUeStatusUpdate &msg)
{
    if (msg.what == NmUeStatusUpdate::SESSION_ESTABLISHMENT)
    {
        auto *session = msg.pduSession;

        setupTunInterface(session);
        return;
    }

    if (msg.what == NmUeStatusUpdate::SESSION_RELEASE)
    {
        if (m_tunTasks[msg.psi] != nullptr)
        {
            m_tunTasks[msg.psi]->quit();
            delete m_tunTasks[msg.psi];
            m_tunTasks[msg.psi] = nullptr;
        }

        return;
    }

    if (msg.what == NmUeStatusUpdate::CM_STATE)
    {
        m_cmState = msg.cmState;
        return;
    }
}

void UeAppTask::setupTunInterface(const PduSession *pduSession)
{
    if (!utils::IsRoot())
    {
        m_logger->err("TUN interface could not be setup. Permission denied. Please run the UE with 'sudo'");
        return;
    }

    if (!pduSession->pduAddress.has_value())
    {
        m_logger->err("Connection could not setup. PDU address is missing.");
        return;
    }

    if (pduSession->pduAddress->sessionType != nas::EPduSessionType::IPV4 ||
        pduSession->sessionType != nas::EPduSessionType::IPV4)
    {
        m_logger->err("Connection could not setup. PDU session type is not supported.");
        return;
    }

    int psi = pduSession->psi;
    if (psi == 0 || psi > 15)
    {
        m_logger->err("Connection could not setup. Invalid PSI.");
        return;
    }

    if (m_tunTasks[psi] != nullptr)
    {
        m_logger->err("Connection could not setup. TUN task for specified PSI is non-null.");
        return;
    }

    std::string error{}, allocatedName{};
    std::string requestedName = cons::TunNamePrefix;
    std::string requestedNetmask = cons::TunNetmask;
    if (m_base->config->tunName.has_value())
        requestedName = *m_base->config->tunName;
    if (m_base->config->tunNetmask.has_value())
        requestedNetmask = *m_base->config->tunNetmask;
    
    int fd = tun::TunAllocate(requestedName.c_str(), allocatedName, error);
    if (fd == 0 || error.length() > 0)
    {
        m_logger->err("TUN allocation failure [%s]", error.c_str());
        return;
    }

    // set the TUN ip address to the PDU address assigned by the network
    std::string ipAddress = utils::OctetStringToIp(pduSession->pduAddress->pduAddressInformation);

    bool r = tun::TunConfigure(allocatedName, ipAddress, requestedNetmask, cons::TunMtu, m_base->config->configureRouting, error);
    if (!r || error.length() > 0)
    {
        m_logger->err("TUN configuration failure [%s]", error.c_str());
        return;
    }

    auto *task = new TunTask(m_base, psi, fd, allocatedName);
    m_tunTasks[psi] = task;
    task->start();

    m_logger->info("Connection setup for PDU session[%d] is successful, TUN interface[%s, %s] is up.", pduSession->psi,
                   allocatedName.c_str(), ipAddress.c_str());
}

} // namespace nr::ue
