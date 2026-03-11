//
// Out-of-band measurement provider for UE RRC.
// Accepts cell measurements via UDP, Unix datagram socket, or JSON file.
//
// Expected JSON format (array of cell measurements):
// {
//   "measurements": [
//     { "cellId": 1, "rsrp": -85, "rsrq": -10, "sinr": 15 },
//     { "cellId": 2, "rsrp": -95, "rsrq": -12, "sinr": 8 },
//     { "nci": 36,   "rsrp": -78, "rsrq": -8,  "sinr": 20 }
//   ]
// }
//
// Fields:
//   cellId  – internal UERANSIM cell ID (from RLS layer)
//   nci     – NR Cell Identity (from SIB1), used when cellId is absent
//   rsrp    – Reference Signal Received Power in dBm (mandatory)
//   rsrq    – Reference Signal Received Quality in dB (optional)
//   sinr    – Signal-to-Interference-plus-Noise Ratio in dB (optional)
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <utils/logger.hpp>
#include <utils/network.hpp>
#include <ue/types.hpp>

namespace nr::ue
{

class MeasurementProvider
{
  public:
    explicit MeasurementProvider(const MeasSourceConfig &config, LogBase *logBase);
    ~MeasurementProvider();

    // Start the provider background thread (if applicable).
    void start();

    // Stop and join the background thread.
    void stop();

    // Thread-safe: retrieve the latest set of OOB measurements.
    // Returns an empty vector if no OOB source is configured.
    std::vector<CellMeasurement> getLatestMeasurements();

  private:
    void runUdp();
    void runUnixSocket();
    void runFilePoller();
    void parseMeasurements(const std::string &jsonStr);

    MeasSourceConfig m_config;
    std::unique_ptr<Logger> m_logger;

    std::mutex m_mutex;
    std::vector<CellMeasurement> m_latestMeasurements;

    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace nr::ue
