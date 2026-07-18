//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <cstdint>
#include <string>
#include <cstdint>

namespace sctp
{

class SctpServer
{
  private:
    int sd;

  public:
    SctpServer(const std::string &address, uint16_t port);
    // Variant with explicit stream limits (advertised to connecting peers).
    SctpServer(const std::string &address, uint16_t port, int maxRxStreams, int maxTxStreams);
    ~SctpServer();

    void start();

    // Blocking accept.  Returns the connected socket fd (SCTP event
    // notifications already enabled) for the caller to wrap in an SctpClient.
    // Throws SctpError on failure (including when the listen socket is closed).
    int acceptClient();
};

} // namespace sctp