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
#include <vector>

namespace sctp
{

class SctpClient
{
  private:
    int sd;
    const PayloadProtocolId ppid;

  public:
    explicit SctpClient(PayloadProtocolId ppid, const std::string &address);
    explicit SctpClient(PayloadProtocolId ppid, const std::string &address, int maxTxStreams, int maxRxStreams);
    // Adopt an already-connected socket (e.g. from SctpServer::acceptClient()).
    // Takes ownership of the fd; the destructor closes it.
    SctpClient(PayloadProtocolId ppid, int connectedFd);
    ~SctpClient();

    void bind(const std::string &address, uint16_t port);
    void connect(const std::string &address, uint16_t port) const;

    void send(uint16_t stream, const uint8_t *buffer, size_t offset, size_t length);
    void send(uint16_t stream, const uint8_t *buffer, size_t length);
    void send(uint16_t stream, const std::vector<uint8_t> &data);

    void receive(ISctpHandler *handler);

    // SCTP_STATUS of the established association (for accepted connections,
    // where no COMM_UP notification will be delivered to synthesize it from).
    void queryStatus(int &assocId, int &inStreams, int &outStreams) const;
};

} // namespace sctp