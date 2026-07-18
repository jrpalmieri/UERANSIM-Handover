//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "server.hpp"
#include "internal.hpp"

sctp::SctpServer::SctpServer(const std::string &address, uint16_t port) : SctpServer(address, port, 10, 10)
{
}

sctp::SctpServer::SctpServer(const std::string &address, uint16_t port, int maxRxStreams, int maxTxStreams) : sd(0)
{
    try
    {
        sd = CreateSocket(address);
        SetReuseAddr(sd);
        BindSocket(sd, address, port);
        SetInitOptions(sd, maxRxStreams, maxTxStreams, 10, 10 * 1000);
        SetEventOptions(sd);
        StartListening(sd);
    }
    catch (const SctpError &e)
    {
        CloseSocket(sd);
        throw;
    }
}

sctp::SctpServer::~SctpServer()
{
    CloseSocket(sd);
}

void sctp::SctpServer::start()
{
    Accept(sd);
}

int sctp::SctpServer::acceptClient()
{
    return AcceptConnection(sd);
}
