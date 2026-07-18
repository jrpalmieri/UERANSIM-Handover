//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <cstring>
#include <thread>
#include <utility>

// #define MOCKED_PACKETS

#ifdef MOCKED_PACKETS
static std::string MOCK_LIST[] = {
    std::string(
        "201500320000040001000e05806f70656e3567732d616d663000600008000009f10702004000564001ff005000080009f10700000008"),

    std::string("0004403e000003000a000200060055000200010026002b2a7e005600020000215660aed893ca993801ad60ba50575cb020101c"
                "016820d4f980"
                "004ff8987b0bfcbbb8"),

    std::string("00044029000003000a0002000600550002000100260016157e03cbc05c7b007e005d020004f0f0f0f0e1360102"),

    std::string("000e00809e000009000a00020006005500020001006e000a0c3e800000303e800000001c00070009f107020040000000020001"
                "007700091c00"
                "0e000700038000005e002091f9908eefe559d08b9f958b4d7c1a94094918dedcf40bb57c55428a00d4dee90022400835693803"
                "56fffff00026"
                "402f2e7e02687a0bd3017e0042010177000bf209f107020040e700acbe54072009f10700000115020101210201005e0129"),

    std::string("0004403c000003000a0002000600550002000100260029287e02f41c3d78027e0054430f10004f00700065006e003500470053"
                "462147121062"
                "41547021490100"),
};
static int MOCK_INDEX = -1;
#endif

namespace nr::gnb
{

class SctpHandler : public sctp::ISctpHandler
{
  private:
    SctpTask *const sctpTask;
    int clientId;

  public:
    SctpHandler(SctpTask *const sctpTask, int clientId) : sctpTask(sctpTask), clientId(clientId)
    {
    }

  private:
    void onAssociationSetup(int associationId, int inStreams, int outStreams) override
    {
        auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::ASSOCIATION_SETUP);
        w->clientId = clientId;
        w->associationId = associationId;
        w->inStreams = inStreams;
        w->outStreams = outStreams;
        sctpTask->push(std::move(w));
    }

    void onAssociationShutdown() override
    {
        auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::ASSOCIATION_SHUTDOWN);
        w->clientId = clientId;
        sctpTask->push(std::move(w));
    }

    void onMessage(const uint8_t *buffer, size_t length, uint16_t stream) override
    {
        auto *data = new uint8_t[length];
        std::memcpy(data, buffer, length);

        auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::RECEIVE_MESSAGE);
        w->clientId = clientId;
        w->buffer = UniqueBuffer{data, length};
        w->stream = stream;
        sctpTask->push(std::move(w));
    }

    void onUnhandledNotification() override
    {
        auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::UNHANDLED_NOTIFICATION);
        w->clientId = clientId;
        sctpTask->push(std::move(w));
    }

    void onConnectionReset() override
    {
        auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::UNHANDLED_NOTIFICATION);
        w->clientId = clientId;
        sctpTask->push(std::move(w));
    }
};

[[noreturn]] static void ReceiverThread(std::pair<sctp::SctpClient *, sctp::ISctpHandler *> *args)
{
    sctp::SctpClient *client = args->first;
    sctp::ISctpHandler *handler = args->second;

    delete args;

    while (true)
        client->receive(handler);
}

// Blocks in accept() on a listening endpoint; every accepted association is
// handed to the SCTP task as a CONNECTION_ACCEPTED message.  The thread ends
// via pthread_cancel from ScopedThread's destructor (accept and sleep are
// cancellation points); an exception must never escape (std::terminate), so
// accept failures back off and retry instead of throwing.
[[noreturn]] static void AcceptThread(std::pair<sctp::SctpServer *, SctpTask *> *args)
{
    sctp::SctpServer *server = args->first;
    SctpTask *task = args->second;

    delete args;

    while (true)
    {
        int fd;
        try
        {
            fd = server->acceptClient();
        }
        catch (const sctp::SctpError &)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        auto w = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_ACCEPTED);
        w->acceptedFd = fd;
        task->push(std::move(w));
    }
}

SctpTask::SctpTask(TaskBase *base, const char *loggerName) : m_base{base}, m_clients{}
{
    m_logger = base->logBase->makeUniqueLogger(loggerName);
}

void SctpTask::onStart()
{
    m_logger->debug("Starting SCTP task.");
}

void SctpTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::GNB_SCTP: {
        auto& w = dynamic_cast<NmGnbSctp &>(*msg);
        switch (w.present)
        {
        case NmGnbSctp::CONNECTION_REQUEST: {
            receiveSctpConnectionSetupRequest(w.clientId, w.localAddress, w.localPort, w.remoteAddress,
                                              w.remotePort, w.ppid, w.associatedTask, w.maxTxStreams, w.maxRxStreams);
            break;
        }
        case NmGnbSctp::LISTEN_REQUEST: {
            receiveListenRequest(w.localAddress, w.localPort, w.ppid, w.associatedTask, w.maxTxStreams,
                                 w.maxRxStreams);
            break;
        }
        case NmGnbSctp::CONNECTION_ACCEPTED: {
            receiveConnectionAccepted(w.acceptedFd);
            break;
        }
        case NmGnbSctp::CONNECTION_CLOSE: {
            receiveConnectionClose(w.clientId);
            break;
        }
        case NmGnbSctp::ASSOCIATION_SETUP: {
            receiveAssociationSetup(w.clientId, w.associationId, w.inStreams, w.outStreams);
            break;
        }
        case NmGnbSctp::ASSOCIATION_SHUTDOWN: {
            receiveAssociationShutdown(w.clientId);
            break;
        }
        case NmGnbSctp::RECEIVE_MESSAGE: {
            receiveClientReceive(w.clientId, w.stream, std::move(w.buffer));
            break;
        }
        case NmGnbSctp::SEND_MESSAGE: {
            receiveSendMessage(w.clientId, w.stream, std::move(w.buffer));
            break;
        }
        case NmGnbSctp::UNHANDLED_NOTIFICATION: {
            receiveUnhandledNotification(w.clientId);
            break;
        }
        default:
            m_logger->unhandledNts(*msg);
            break;
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void SctpTask::onQuit()
{
    if (m_listener != nullptr)
    {
        // Cancel the accept thread before closing the listen socket it blocks on.
        delete m_listener->acceptThread;
        delete m_listener->server;
        delete m_listener;
        m_listener = nullptr;
    }

    for (auto &client : m_clients)
    {
        ClientEntry *entry = client.second;
        DeleteClientEntry(entry);
    }
    m_clients.clear();
}

void SctpTask::DeleteClientEntry(ClientEntry *entry)
{
    entry->associatedTask = nullptr;
    delete entry->receiverThread;
    delete entry->client;
    delete entry->handler;
    delete entry;
}

void SctpTask::receiveSctpConnectionSetupRequest(int clientId, const std::string &localAddress, uint16_t localPort,
                                                 const std::string &remoteAddress, uint16_t remotePort,
                                                 sctp::PayloadProtocolId ppid, NtsTask *associatedTask, uint16_t maxTxStreams, uint16_t maxRxStreams)
{
    m_logger->info("Trying to establish SCTP connection... (%s:%d)", remoteAddress.c_str(), remotePort);

    auto *client = new sctp::SctpClient(ppid, localAddress, maxTxStreams, maxRxStreams);

    // On bind/connect failure the requesting task is notified with
    // CONNECTION_FAILED so it can apply its own retry policy (e.g. the Xn
    // task retries failed neighbor connections on its periodic check).
    auto notifyFailure = [this, clientId, associatedTask]() {
        if (associatedTask == nullptr)
            return;
        auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::CONNECTION_FAILED);
        msg->clientId = clientId;
        associatedTask->push(std::move(msg));
    };

    try
    {
        client->bind(localAddress, localPort);
    }
    catch (const sctp::SctpError &exc)
    {
        m_logger->err("Binding to local address %s:%d failed. %s", localAddress.c_str(), localPort, exc.what());
        delete client;
        notifyFailure();
        return;
    }

    try
    {
        client->connect(remoteAddress, remotePort);
    }
    catch (const sctp::SctpError &exc)
    {
        m_logger->err("Connecting to remote address %s:%d failed. %s", remoteAddress.c_str(), remotePort, exc.what());
        delete client;
        notifyFailure();
        return;
    }

    m_logger->info("SCTP connection established (%s:%d)", remoteAddress.c_str(), remotePort);

    sctp::ISctpHandler *handler = new SctpHandler(this, clientId);

    auto *entry = new ClientEntry;
    m_clients[clientId] = entry;

    entry->id = clientId;
    entry->client = client;
    entry->handler = handler;
    entry->associatedTask = associatedTask;
    entry->receiverThread = new ScopedThread(
        [](void *arg) { ReceiverThread(reinterpret_cast<std::pair<sctp::SctpClient *, sctp::ISctpHandler *> *>(arg)); },
        new std::pair<sctp::SctpClient *, sctp::ISctpHandler *>(client, handler));
}

void SctpTask::receiveListenRequest(const std::string &localAddress, uint16_t localPort, sctp::PayloadProtocolId ppid,
                                    NtsTask *associatedTask, uint16_t maxTxStreams, uint16_t maxRxStreams)
{
    if (m_listener != nullptr)
    {
        m_logger->err("SCTP listener already active, ignoring LISTEN_REQUEST for %s:%d", localAddress.c_str(),
                      static_cast<int>(localPort));
        return;
    }

    sctp::SctpServer *server;
    try
    {
        server = new sctp::SctpServer(localAddress, localPort, maxRxStreams, maxTxStreams);
    }
    catch (const sctp::SctpError &exc)
    {
        m_logger->err("SCTP listen on %s:%d failed. %s", localAddress.c_str(), static_cast<int>(localPort),
                      exc.what());
        return;
    }

    auto *entry = new ListenerEntry;
    entry->server = server;
    entry->ppid = ppid;
    entry->associatedTask = associatedTask;
    entry->acceptThread = new ScopedThread(
        [](void *arg) { AcceptThread(reinterpret_cast<std::pair<sctp::SctpServer *, SctpTask *> *>(arg)); },
        new std::pair<sctp::SctpServer *, SctpTask *>(server, this));
    m_listener = entry;

    m_logger->info("SCTP listening on %s:%d", localAddress.c_str(), static_cast<int>(localPort));
}

void SctpTask::receiveConnectionAccepted(int acceptedFd)
{
    if (m_listener == nullptr)
    {
        // Listener torn down while the message was in flight; close the stray
        // fd via a throwaway wrapper (ppid is irrelevant for closing).
        sctp::SctpClient stray{sctp::PayloadProtocolId::XNAP, acceptedFd};
        return;
    }

    auto *client = new sctp::SctpClient(m_listener->ppid, acceptedFd);

    // The association is already established, so no COMM_UP notification will
    // be delivered; query its parameters and synthesize ASSOCIATION_SETUP.
    int assocId, inStreams, outStreams;
    try
    {
        client->queryStatus(assocId, inStreams, outStreams);
    }
    catch (const sctp::SctpError &exc)
    {
        m_logger->err("Accepted SCTP association unusable: %s", exc.what());
        delete client;
        return;
    }

    int clientId = m_nextInboundClientId--;

    sctp::ISctpHandler *handler = new SctpHandler(this, clientId);

    auto *entry = new ClientEntry;
    m_clients[clientId] = entry;
    entry->id = clientId;
    entry->client = client;
    entry->handler = handler;
    entry->associatedTask = m_listener->associatedTask;

    m_logger->info("SCTP association accepted (clientId=%d, streams in=%d out=%d)", clientId, inStreams, outStreams);

    // Deliver ASSOCIATION_SETUP before starting the receiver thread so it is
    // guaranteed to precede any RECEIVE_MESSAGE for this clientId.
    auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::ASSOCIATION_SETUP);
    msg->clientId = clientId;
    msg->associationId = assocId;
    msg->inStreams = inStreams;
    msg->outStreams = outStreams;
    entry->associatedTask->push(std::move(msg));

    entry->receiverThread = new ScopedThread(
        [](void *arg) { ReceiverThread(reinterpret_cast<std::pair<sctp::SctpClient *, sctp::ISctpHandler *> *>(arg)); },
        new std::pair<sctp::SctpClient *, sctp::ISctpHandler *>(client, handler));
}

void SctpTask::receiveAssociationSetup(int clientId, int associationId, int inStreams, int outStreams)
{
    m_logger->debug("SCTP association setup ascId[%d]", associationId);

    ClientEntry *entry = m_clients[clientId];
    if (entry == nullptr)
    {
        m_logger->warn("Client entry not found for id: %d", clientId);
        return;
    }

    // Notify the relevant task
    auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::ASSOCIATION_SETUP);
    msg->clientId = clientId;
    msg->associationId = associationId;
    msg->inStreams = inStreams;
    msg->outStreams = outStreams;
    entry->associatedTask->push(std::move(msg));
}

void SctpTask::receiveAssociationShutdown(int clientId)
{
    m_logger->debug("SCTP association shutdown (clientId: %d)", clientId);

    ClientEntry *entry = m_clients[clientId];
    if (entry == nullptr)
    {
        m_logger->warn("Client entry not found for id: %d", clientId);
        return;
    }

    // Notify the relevant task
    auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::ASSOCIATION_SHUTDOWN);
    msg->clientId = clientId;
    entry->associatedTask->push(std::move(msg));
}

void SctpTask::receiveClientReceive(int clientId, uint16_t stream, UniqueBuffer &&buffer)
{
    ClientEntry *entry = m_clients[clientId];
    if (entry == nullptr)
    {
        m_logger->warn("Client entry not found for id: %d", clientId);
        return;
    }

    // Notify the relevant task
    auto msg = std::make_unique<NmGnbSctp>(NmGnbSctp::RECEIVE_MESSAGE);
    msg->clientId = clientId;
    msg->stream = stream;
    msg->buffer = std::move(buffer);
    entry->associatedTask->push(std::move(msg));
}

void SctpTask::receiveUnhandledNotification(int clientId)
{
    // NOTE: For unhandled notifications, "clientId" may be invalid for some notifications.
    // Because some notification may be received after shutdown.

    // Print warning
    m_logger->warn("Unhandled SCTP notification received");
}

void SctpTask::receiveConnectionClose(int clientId)
{
    ClientEntry *entry = m_clients[clientId];
    if (entry == nullptr)
    {
        m_logger->warn("Client entry not found for id: %d", clientId);
        return;
    }

    DeleteClientEntry(entry);
    // Erase the map entry, otherwise it keeps a dangling pointer that a late
    // receiver-thread message (or onQuit's cleanup loop) would dereference.
    m_clients.erase(clientId);
}

void SctpTask::receiveSendMessage(int clientId, uint16_t stream, UniqueBuffer &&buffer)
{
    ClientEntry *entry = m_clients[clientId];
    if (entry == nullptr)
    {
        m_logger->warn("Client entry not found for id: %d", clientId);
        return;
    }

#ifdef MOCKED_PACKETS
    {
        std::string ss = MOCK_LIST[++MOCK_INDEX];
        OctetString data = OctetString::FromHex(ss);
        auto *copy = new uint8_t[data.length()];
        std::memcpy(copy, data.data(), data.length());
        receiveClientReceive(clientId, 0, copy, data.length());
    }
#else
    entry->client->send(stream, buffer.data(), 0, buffer.size());
#endif
}

} // namespace nr::gnb
