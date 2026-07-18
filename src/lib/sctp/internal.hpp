//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include "types.hpp"

#include <string>

namespace sctp
{

int CreateSocket(const std::string &address);
void BindSocket(int sd, const std::string &address, uint16_t port);
// SO_REUSEADDR — used on listening sockets so a restart is not blocked by a
// lingering endpoint from a previous instance.
void SetReuseAddr(int sd);
void SetInitOptions(int sd, int maxRxStreams, int maxTxStreams, int maxAttempts, int initTimeoutMs);
void SetEventOptions(int sd);
void StartListening(int sd);
void CloseSocket(int sd);
void Accept(int sd);
// Blocking accept that returns the connected fd (with SCTP event
// notifications enabled on it) instead of discarding it like Accept().
int AcceptConnection(int sd);
// SCTP_STATUS query for an established one-to-one association.
void QueryStatus(int sd, int &assocId, int &inStreams, int &outStreams);
void Connect(int sd, const std::string &address, uint16_t port);
void SendMessage(int sd, const uint8_t *buffer, size_t length, int ppid, uint16_t stream);
void ReceiveMessage(int sd, uint32_t ppid, ISctpHandler *handler);

} // namespace sctp