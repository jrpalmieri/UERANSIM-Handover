//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <ue/app/task.hpp>
#include <ue/nas/task.hpp>
#include <ue/rrc/task.hpp>
#include <utils/common.hpp>
#include <utils/random.hpp>

namespace nr::ue
{


// make an STI value for a UE.
//  convention is the STI = IMSI value in the high bit locations (starting at bit 10) + random value in the low 10 bits. 
//  This allows the gnb to identify the UE by its IMSI from the STI, while also ensuring that the STI is unique across 
//  multiple runs of the program (since the random value will be different each time).
static uint64_t makeSti(const Supi supi)
{
    const auto &imsi =supi.value;
    // IMSI is a 15-char decimal string; convert to uint64
    uint64_t sti = static_cast<uint64_t>(std::stoul(imsi));

    // shift left by 10 bits
    sti <<= 10;

    // append random 10 bits
    sti += Random::Mixed(utils::CurrentTimeMillis()).nextI(0, 1024);

    return sti;
}

UeRlsTask::UeRlsTask(TaskBase *base) : m_base{base}
{
    m_logger = m_base->logBase->makeUniqueLogger(m_base->config->getLoggerPrefix() + "rls");

    m_shCtx = new RlsSharedContext();

    // create a unique STI for this UE based on its SUPI
    if (base->config->supi.has_value())
    {
        m_shCtx->sti = makeSti(base->config->supi.value());
    }
    else
    {
        // old STI assignment logic
        //m_shCtx->sti = Random::Mixed(base->config->supi.value()).nextL();
        m_shCtx->sti = Random::Mixed(base->config->getNodeName()).nextL();
    }

    m_shCtx->cRnti = 0;

    m_udpTask = new RlsUdpTask(base, m_shCtx, base->config->gnbSearchList);
    m_ctlTask = new RlsControlTask(base, m_shCtx);

    m_udpTask->initialize(m_ctlTask);
    m_ctlTask->initialize(this, m_udpTask);
}

void UeRlsTask::onStart()
{
    m_udpTask->start();
    m_ctlTask->start();
}

void UeRlsTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::UE_RLS_TO_RLS: {
        auto &w = dynamic_cast<NmUeRlsToRls &>(*msg);
        switch (w.present)
        {
        case NmUeRlsToRls::SIGNAL_CHANGED: {
            auto m = std::make_unique<NmUeRlsToRrc>(NmUeRlsToRrc::SIGNAL_CHANGED);
            m->cellId = w.cellId;
            m->dbm = w.dbm;
            m_base->rrcTask->push(std::move(m));
            break;
        }
        case NmUeRlsToRls::DOWNLINK_DATA: {
            auto m = std::make_unique<NmUeRlsToNas>(NmUeRlsToNas::DATA_PDU_DELIVERY);
            m->psi = w.psi;
            m->pdu = std::move(w.data);
            m_base->nasTask->push(std::move(m));
            break;
        }
        case NmUeRlsToRls::DOWNLINK_RRC: {
            auto m = std::make_unique<NmUeRlsToRrc>(NmUeRlsToRrc::DOWNLINK_RRC_DELIVERY);
            m->cellId = w.cellId;
            m->channel = w.rrcChannel;
            m->pdu = std::move(w.data);
            m_base->rrcTask->push(std::move(m));
            break;
        }
        case NmUeRlsToRls::RADIO_LINK_FAILURE: {
            auto m = std::make_unique<NmUeRlsToRrc>(NmUeRlsToRrc::RADIO_LINK_FAILURE);
            m->rlfCause = w.rlfCause;
            m->cellId = w.cellId;
            m_base->rrcTask->push(std::move(m));
            break;
        }
        case NmUeRlsToRls::TRANSMISSION_FAILURE: {
            m_logger->debug("transmission failure [%d]", w.pduList.size());
            break;
        }
        default: {
            m_logger->unhandledNts(*msg);
            break;
        }
        }
        break;
    }
    case NtsMessageType::UE_RRC_TO_RLS: {
        auto &w = dynamic_cast<NmUeRrcToRls &>(*msg);
        switch (w.present)
        {
        case NmUeRrcToRls::ASSIGN_CURRENT_CELL: {
            auto m = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::ASSIGN_CURRENT_CELL);
            m->cellId = w.cellId;
            m_ctlTask->push(std::move(m));
            break;
        }
        case NmUeRrcToRls::RRC_PDU_DELIVERY: {
            auto m = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::UPLINK_RRC);
            m->cellId = w.cellId;
            m->rrcChannel = w.channel;
            m->pduId = w.pduId;
            m->data = std::move(w.pdu);
            m_ctlTask->push(std::move(m));
            break;
        }
        case NmUeRrcToRls::RESET_STI: {
            m_shCtx->sti = Random::Mixed(m_base->config->getNodeName()).nextL();
            break;
        }
        }
        break;
    }
    case NtsMessageType::UE_NAS_TO_RLS: {
        auto &w = dynamic_cast<NmUeNasToRls &>(*msg);
        switch (w.present)
        {
        case NmUeNasToRls::DATA_PDU_DELIVERY: {
            auto m = std::make_unique<NmUeRlsToRls>(NmUeRlsToRls::UPLINK_DATA);
            m->psi = w.psi;
            m->data = std::move(w.pdu);
            m_ctlTask->push(std::move(m));
            break;
        }
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void UeRlsTask::onQuit()
{
    m_udpTask->quit();
    m_ctlTask->quit();

    delete m_udpTask;
    delete m_ctlTask;
    delete m_shCtx;
}

void UeRlsTask::setCurrentCrnti(uint32_t cRnti)
{
    m_shCtx->cRnti = cRnti;
}

uint32_t UeRlsTask::getCurrentCrnti() const
{
    return m_shCtx->cRnti.load();
}

} // namespace nr::ue
