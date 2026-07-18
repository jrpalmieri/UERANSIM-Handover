//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gnb/nts.hpp>
#include <lib/sctp/sctp.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>
#include <utils/scoped_thread.hpp>

namespace nr::gnb
{

class SctpTask : public NtsTask
{
  private:
    struct ClientEntry
    {
        int id;
        sctp::SctpClient *client;
        ScopedThread *receiverThread;
        sctp::ISctpHandler *handler;
        NtsTask *associatedTask;
    };

    // A listening endpoint (LISTEN_REQUEST).  Accepted associations become
    // ordinary ClientEntry records with negative, task-assigned clientIds.
    struct ListenerEntry
    {
        sctp::SctpServer *server;
        ScopedThread *acceptThread;
        sctp::PayloadProtocolId ppid;
        NtsTask *associatedTask;
    };

  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;
    std::unordered_map<int, ClientEntry *> m_clients;
    ListenerEntry *m_listener{nullptr};
    // Inbound (accepted) associations get negative clientIds so they can never
    // collide with requester-chosen ids.  -1 is reserved as "unset" by users.
    int m_nextInboundClientId{-2};

    friend class GnbCmdHandler;

  public:
    // loggerName distinguishes multiple instances (e.g. "sctp" for the
    // NGAP/AMF connections, "sctp-xn" for the Xn instance).
    explicit SctpTask(TaskBase *base, const char *loggerName = "sctp");
    ~SctpTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    static void DeleteClientEntry(ClientEntry *entry);

  private:
    void receiveSctpConnectionSetupRequest(int clientId, const std::string &localAddress, uint16_t localPort,
                                           const std::string &remoteAddress, uint16_t remotePort,
                                           sctp::PayloadProtocolId ppid, NtsTask *associatedTask, uint16_t maxTxStreams, uint16_t maxRxStreams);
    void receiveListenRequest(const std::string &localAddress, uint16_t localPort, sctp::PayloadProtocolId ppid,
                              NtsTask *associatedTask, uint16_t maxTxStreams, uint16_t maxRxStreams);
    void receiveConnectionAccepted(int acceptedFd);
    void receiveAssociationSetup(int clientId, int associationId, int inStreams, int outStreams);
    void receiveAssociationShutdown(int clientId);
    void receiveClientReceive(int clientId, uint16_t stream, UniqueBuffer &&buffer);
    void receiveUnhandledNotification(int clientId);
    void receiveConnectionClose(int clientId);
    void receiveSendMessage(int clientId, uint16_t stream, UniqueBuffer &&buffer);
};

} // namespace nr::gnb