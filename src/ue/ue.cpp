//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "ue.hpp"

#include "app/task.hpp"
#include "nas/task.hpp"
#include "rls/task.hpp"
#include "rrc/task.hpp"

#include <utils/common.hpp>
#include <utils/constants.hpp>

#include <lib/sat/sat_time.hpp>

namespace nr::ue
{

UserEquipment::UserEquipment(UeConfig *config, app::IUeController *ueController, app::INodeListener *nodeListener,
                             NtsTask *cliCallbackTask, AllCellMeasurements *g_allCellMeasurements)
{
    auto *base = new TaskBase();
    base->ue = this;
    base->config = config;
    base->logBase = new LogBase("logs/ue-" + config->getNodeName() + ".log");
    base->ueController = ueController;
    base->nodeListener = nodeListener;
    base->cliCallbackTask = cliCallbackTask;

    int64_t startEpochMillis = config->ntn.timeWarp.startEpochMillis.value_or(utils::CurrentTimeMillis());
    auto startCondition = nr::sat::SatTime::EStartCondition::Moving;
    if (config->ntn.timeWarp.startCondition == nr::ue::UeConfig::NtnConfig::TimeWarpConfig::EStartCondition::Paused)
        startCondition = nr::sat::SatTime::EStartCondition::Paused;

    base->satTime = new nr::sat::SatTime(startEpochMillis, startCondition, config->ntn.timeWarp.tickScaling);

    base->nasTask = new NasTask(base);
    base->rrcTask = new UeRrcTask(base);
    base->appTask = new UeAppTask(base);
    base->rlsTask = new UeRlsTask(base);

    base->g_allCellMeasurements = g_allCellMeasurements;

    // initialize UE Location from config if provided, otherwise default to (0, 0, 0)
    base->UeLocation = config->initialPosition.value_or(GeoPosition{});
    
    // Satellite states store
    base->satState = new nr::sat::SatStates();

    taskBase = base;
}

UserEquipment::~UserEquipment()
{
    taskBase->nasTask->quit();
    taskBase->rrcTask->quit();
    taskBase->rlsTask->quit();
    taskBase->appTask->quit();

    delete taskBase->nasTask;
    delete taskBase->rrcTask;
    delete taskBase->rlsTask;
    delete taskBase->appTask;
    delete taskBase->satTime;

    delete taskBase->logBase;

    delete taskBase;
}

void UserEquipment::start()
{
    auto logger = taskBase->logBase->makeUniqueLogger(taskBase->config->getLoggerPrefix() + "ue");
    logger->info("UE version %s (base %s) starting", UE_VERSION, cons::Tag);

    taskBase->nasTask->start();
    taskBase->rrcTask->start();
    taskBase->rlsTask->start();
    taskBase->appTask->start();
}

void UserEquipment::pushCommand(std::unique_ptr<app::UeCliCommand> cmd, const InetAddress &address)
{
    taskBase->appTask->push(std::make_unique<NmUeCliCommand>(std::move(cmd), address));
}

} // namespace nr::ue
