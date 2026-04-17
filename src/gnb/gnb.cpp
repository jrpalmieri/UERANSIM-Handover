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
#include "sat_tle_store.hpp"
#include "sctp/task.hpp"
#include "xn/task.hpp"

#include <lib/app/cli_base.hpp>
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

    base->satTleStore = new SatTleStore();

    base->neighbors = new GnbNeighbors();
    base->neighbors->upsertAll(config->neighborList);

    base->appTask = new GnbAppTask(base);
    base->sctpTask = new SctpTask(base);
    base->ngapTask = new NgapTask(base);
    base->rrcTask = new GnbRrcTask(base);
    base->gtpTask = new GtpTask(base);
    base->rlsTask = new GnbRlsTask(base);
    base->xnTask = new XnTask(base);

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

    delete taskBase->satTleStore;

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
