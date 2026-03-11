//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include "types.hpp"

#include <lib/app/cli_cmd.hpp>
#include <lib/app/monitor.hpp>
#include <string>
#include <utils/logger.hpp>
#include <utils/network.hpp>
#include <utils/nts.hpp>

namespace nr::gnb
{

// gNB-specific version tracking (independent of UERANSIM base version).
// Bump this when adding gNB-level features such as handover, measurement, etc.
static constexpr const char *GNB_VERSION = "2.0.1";

class GNodeB
{
  private:
    TaskBase *taskBase;

  public:
    GNodeB(GnbConfig *config, app::INodeListener *nodeListener, NtsTask *cliCallbackTask);
    virtual ~GNodeB();

  public:
    void start();
    void pushCommand(std::unique_ptr<app::GnbCliCommand> cmd, const InetAddress &address);
};

} // namespace nr::gnb