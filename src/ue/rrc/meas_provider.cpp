//
// Out-of-band measurement provider implementation.
//

#include "meas_provider.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <yaml-cpp/yaml.h>

#include <utils/common.hpp>

static constexpr int RECV_BUFFER_SIZE = 8192;

namespace nr::ue
{

MeasurementProvider::MeasurementProvider(const MeasSourceConfig &config, LogBase *logBase)
    : m_config{config}
{
    m_logger = logBase->makeUniqueLogger("meas-prov");
}

MeasurementProvider::~MeasurementProvider()
{
    stop();
}

void MeasurementProvider::start()
{
    if (m_config.type == EMeasSourceType::NONE)
        return;

    m_running = true;

    switch (m_config.type)
    {
    case EMeasSourceType::UDP:
        m_thread = std::thread([this]() { runUdp(); });
        break;
    case EMeasSourceType::UNIX_SOCK:
        m_thread = std::thread([this]() { runUnixSocket(); });
        break;
    case EMeasSourceType::FILE:
        m_thread = std::thread([this]() { runFilePoller(); });
        break;
    default:
        break;
    }
}

void MeasurementProvider::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

std::vector<CellMeasurement> MeasurementProvider::getLatestMeasurements()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestMeasurements;
}

/* -------------------------------------------------------------------------- */
/*  JSON / YAML parsing (yaml-cpp can parse JSON since JSON ⊂ YAML)          */
/* -------------------------------------------------------------------------- */

void MeasurementProvider::parseMeasurements(const std::string &jsonStr)
{
    try
    {
        YAML::Node root = YAML::Load(jsonStr);

        YAML::Node measArray;
        if (root.IsMap() && root["measurements"])
            measArray = root["measurements"];
        else if (root.IsSequence())
            measArray = root;
        else
        {
            m_logger->warn("OOB measurement JSON: expected object with 'measurements' array or a bare array");
            return;
        }

        std::vector<CellMeasurement> result;
        result.reserve(measArray.size());

        for (std::size_t i = 0; i < measArray.size(); i++)
        {
            YAML::Node entry = measArray[i];
            CellMeasurement cm{};

            if (entry["cellId"])
                cm.cellId = entry["cellId"].as<int>();
            if (entry["nci"])
                cm.nci = entry["nci"].as<int64_t>();
            if (entry["rsrp"])
                cm.rsrp = entry["rsrp"].as<int>();
            if (entry["rsrq"])
                cm.rsrq = entry["rsrq"].as<int>();
            if (entry["sinr"])
                cm.sinr = entry["sinr"].as<int>();

            result.push_back(cm);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latestMeasurements = std::move(result);
        }
    }
    catch (const std::exception &e)
    {
        m_logger->warn("Failed to parse OOB measurement data: %s", e.what());
    }
}

/* -------------------------------------------------------------------------- */
/*  UDP receiver                                                              */
/* -------------------------------------------------------------------------- */

void MeasurementProvider::runUdp()
{
    m_logger->info("Starting OOB measurement UDP listener on %s:%d", m_config.udpAddress.c_str(), m_config.udpPort);

    try
    {
        InetAddress bindAddr(m_config.udpAddress, m_config.udpPort);
        Socket sock = Socket::CreateAndBindUdp(bindAddr);
        sock.setReuseAddress();

        uint8_t buffer[RECV_BUFFER_SIZE];

        while (m_running)
        {
            InetAddress peerAddr;
            int n = sock.receive(buffer, RECV_BUFFER_SIZE, 500 /* timeout ms */, peerAddr);
            if (n > 0)
            {
                std::string jsonStr(reinterpret_cast<char *>(buffer), static_cast<size_t>(n));
                parseMeasurements(jsonStr);
            }
        }

        sock.close();
    }
    catch (const std::exception &e)
    {
        m_logger->err("OOB measurement UDP error: %s", e.what());
    }
}

/* -------------------------------------------------------------------------- */
/*  Unix datagram socket receiver                                             */
/* -------------------------------------------------------------------------- */

void MeasurementProvider::runUnixSocket()
{
    m_logger->info("Starting OOB measurement Unix socket listener on %s", m_config.unixSocketPath.c_str());

    int fd = -1;
    try
    {
        fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd < 0)
        {
            m_logger->err("Failed to create Unix socket: %s", strerror(errno));
            return;
        }

        // Remove stale socket file
        ::unlink(m_config.unixSocketPath.c_str());

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, m_config.unixSocketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            m_logger->err("Failed to bind Unix socket: %s", strerror(errno));
            ::close(fd);
            return;
        }

        // Set receive timeout so we can check m_running periodically
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500 ms
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint8_t buffer[RECV_BUFFER_SIZE];

        while (m_running)
        {
            ssize_t n = ::recv(fd, buffer, RECV_BUFFER_SIZE, 0);
            if (n > 0)
            {
                std::string jsonStr(reinterpret_cast<char *>(buffer), static_cast<size_t>(n));
                parseMeasurements(jsonStr);
            }
        }

        ::close(fd);
        ::unlink(m_config.unixSocketPath.c_str());
    }
    catch (const std::exception &e)
    {
        m_logger->err("OOB measurement Unix socket error: %s", e.what());
        if (fd >= 0)
            ::close(fd);
    }
}

/* -------------------------------------------------------------------------- */
/*  File poller                                                               */
/* -------------------------------------------------------------------------- */

void MeasurementProvider::runFilePoller()
{
    m_logger->info("Starting OOB measurement file poller: %s (interval %d ms)",
                   m_config.filePath.c_str(), m_config.filePollIntervalMs);

    int64_t lastModTime = 0;

    while (m_running)
    {
        try
        {
            // Simple approach: re-read if file exists
            std::ifstream ifs(m_config.filePath);
            if (ifs.good())
            {
                std::ostringstream oss;
                oss << ifs.rdbuf();
                std::string content = oss.str();
                if (!content.empty())
                    parseMeasurements(content);
            }
        }
        catch (const std::exception &e)
        {
            m_logger->warn("OOB measurement file read error: %s", e.what());
        }

        // Sleep in small increments so we can break out quickly
        int sleptMs = 0;
        while (m_running && sleptMs < m_config.filePollIntervalMs)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sleptMs += 100;
        }
    }
}

} // namespace nr::ue
