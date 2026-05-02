//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "gnb.hpp"
#include "app/task.hpp"
#include "gtp/task.hpp"
#include "neighbors.hpp"
#include "ngap/task.hpp"
#include "rls/task.hpp"
#include "rrc/task.hpp"

#include <lib/sat/sat_time.hpp>
#include <lib/sat/sat_state.hpp>
#include "sat_time.hpp"

#include "sctp/task.hpp"
#include "xn/task.hpp"

#include <lib/app/cli_base.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>

namespace nr::gnb
{

GNodeB::GNodeB(GnbConfig *config, app::INodeListener *nodeListener, NtsTask *cliCallbackTask)
{
    auto *base = new TaskBase();
    base->config = config;
    base->logBase = new LogBase("logs/" + config->name + ".log");
    base->nodeListener = nodeListener;
    base->cliCallbackTask = cliCallbackTask;

    auto m_logger = base->logBase->makeUniqueLogger("gnb");

    m_logger->info("GNodeB object created with gNB ID %u, Cell ID %d, TAC %d", config->getGnbId(), config->getCellId(), config->tac);

    // set initial gnb position from config
    // if ntn is enabled, the position will be updated later by satellite tracking 
    //     using TLE data
    base->setGnbPosition(config->geoLocation);
    m_logger->info("Initial gNB location from config: lat=%.6f, lon=%.6f, alt=%.1f", config->geoLocation.latitude, config->geoLocation.longitude, config->geoLocation.altitude);

    // create storage object for Two-Line Elements
    base->satStates = new nr::sat::SatStates();
    // load from config if present
    if (config->ntn.ownTle.has_value()){
        base->satStates->upsert(config->ntn.ownTle.value());
        m_logger->info("Loaded own TLE from config (pci=%d)", config->ntn.ownTle->pci);

    }

    // initialize the sat clock
    int64_t startEpochMillis = config->ntn.timeWarp.startEpochMillis.value_or(utils::CurrentTimeMillis());
    
    auto startCondition = nr::sat::ESatTimeState::Moving;
    if (config->ntn.timeWarp.startState == nr::sat::ESatTimeState::Paused)
        startCondition = nr::sat::ESatTimeState::Paused;

    base->satTime = new nr::sat::SatTime(startEpochMillis, startCondition, config->ntn.timeWarp.tickScaling);
    sat_time::SetSatTimeSource(base->satTime);

    m_logger->info("Initialized satellite clock: startEpoch=%llums (%s), startCondition=%s, tickScaling=%.3f", startEpochMillis,
                   config->ntn.timeWarp.startEpochText.c_str(),
                   startCondition == nr::sat::ESatTimeState::Moving ? "Moving" : "Paused",
                   config->ntn.timeWarp.tickScaling);

    // initialize the neighbor list
    base->neighbors = new GnbNeighbors();
    base->neighbors->upsertAll(config->neighborList);

    base->appTask = new GnbAppTask(base);
    base->sctpTask = new SctpTask(base);
    base->ngapTask = new NgapTask(base);
    base->rrcTask = new GnbRrcTask(base);
    base->gtpTask = new GtpTask(base);
    base->rlsTask = new GnbRlsTask(base);
    base->xnTask = new XnTask(base);

    base->fixedRsrp = config->rfLink.updateMode == EGnbRsrpMode::Fixed ? config->rfLink.rsrpDbValue : 0;
    m_logger->info("gNB RF Link Config: updateMode=%s, rsrpDbValue=%d, carrFrequencyHz=%.1fMHz, txPowerDbm=%.1f, txGainDbi=%.1f, ueRxGainDbi=%.1f",
                   config->rfLink.updateMode == EGnbRsrpMode::Fixed ? "Fixed" : "Calculated",
                   config->rfLink.rsrpDbValue,
                   config->rfLink.carrFrequencyHz / 1e6,
                   config->rfLink.txPowerDbm,
                   config->rfLink.txGainDbi,
                   config->rfLink.ueRxGainDbi);

    taskBase = base;
}

GNodeB::~GNodeB()
{
    taskBase->appTask->quit();
    taskBase->sctpTask->quit();
    taskBase->ngapTask->quit();
    taskBase->rrcTask->quit();
    taskBase->gtpTask->quit();
    taskBase->rlsTask->quit();
    if (taskBase->config->handover.xn.enabled)
        taskBase->xnTask->quit();

    delete taskBase->appTask;
    delete taskBase->sctpTask;
    delete taskBase->ngapTask;
    delete taskBase->rrcTask;
    delete taskBase->gtpTask;
    delete taskBase->rlsTask;
    delete taskBase->xnTask;

    sat_time::SetSatTimeSource(nullptr);
    delete taskBase->satTime;
    delete taskBase->satStates;

    delete taskBase->logBase;

    delete taskBase;
}

void GNodeB::start()
{
    auto logger = taskBase->logBase->makeUniqueLogger(taskBase->config->name + "-gnb");
    logger->info("gNB version %s (base %s) starting", GNB_VERSION, cons::Tag);

    taskBase->appTask->start();
    taskBase->sctpTask->start();
    taskBase->ngapTask->start();
    taskBase->rrcTask->start();
    taskBase->rlsTask->start();
    taskBase->gtpTask->start();

    if (taskBase->config->handover.xn.enabled)
        taskBase->xnTask->start();
}

void GNodeB::pushCommand(std::unique_ptr<app::GnbCliCommand> cmd, const InetAddress &address)
{
    taskBase->appTask->push(std::make_unique<NmGnbCliCommand>(std::move(cmd), address));
}

} // namespace nr::gnb
