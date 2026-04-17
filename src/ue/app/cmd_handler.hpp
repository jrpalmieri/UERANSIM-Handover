//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <memory>
#include <string>

#include <ue/nts.hpp>
#include <ue/types.hpp>
#include <utils/logger.hpp>

namespace nr::ue
{

class UeCmdHandler
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_cliLogger{};
    std::string m_currentCommand{};
    std::string m_currentSource{};

  private:
    void ensureCliLogger();
    void logCommandReceived(const NmUeCliCommand &msg);
    void logResponse(bool isError, const std::string &output);

  public:
    explicit UeCmdHandler(TaskBase *base) : m_base(base)
    {
    }

    void handleCmd(NmUeCliCommand &msg);

  private:
    void pauseTasks();
    void unpauseTasks();
    bool isAllPaused();

  private:
    void handleCmdImpl(NmUeCliCommand &msg);

  private:
    void sendResult(const InetAddress &address, const std::string &output);
    void sendError(const InetAddress &address, const std::string &output);
};

} // namespace nr::ue
