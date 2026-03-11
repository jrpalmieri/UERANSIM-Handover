//
//

#include "meas_task.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>

#include <utils/common.hpp>

// threshold for considering a measurement stale and removing it from the global map if not updated by the provider
static constexpr const int MEASUREMENT_STALE_THRESHOLD = 1000; 

namespace nr::ue
{

MeasTask::MeasTask(TaskBase *base) : m_base{base}
{

    m_logger = base->logBase->makeUniqueLogger(base->config->getLoggerPrefix() + "meas");
    allMeas = base->g_allCellMeasurements;
    measConfig = new MeasSourceConfig;
    measConfig->type = EMeasSourceType::UDP;
    measConfig->udpPort = base->config->handoverServerConfig.port;
}

void MeasTask::onStart()
{
    // Start OOB measurement provider
    m_measProvider = std::make_unique<MeasurementProvider>(
            *measConfig, m_base->logBase);
    m_measProvider->start();
    m_logger->info("OOB measurement provider started (type=%s)",
                       ToJson(measConfig->type).str().c_str());
    

    // Empty the measurement set
    std::unique_lock lock(allMeas->cellMeasurementsMutex);
    allMeas->cellMeasurements.clear();
    lock.unlock();
}

void MeasTask::onQuit()
{
}

void MeasTask::onLoop()
{

    // current time
    auto now = utils::CurrentTimeMillis();


    auto latestMeas = m_measProvider->getLatestMeasurements();
    if (latestMeas.empty())
    {
        m_logger->debug("collectMeasurements: no OOB measurements received");
        return;
    }
    m_logger->info("collectMeasurements: got %zu OOB measurements", latestMeas.size());

    // lock while updating the global measurement map in shared context, since it is also read by RRC for measurement reporting
    std::unique_lock lock(allMeas->cellMeasurementsMutex);

    for (auto &cm : latestMeas)
    {
  
        // in order to use the comparison to keep the set ordered, we always to an erase+insert 
        //  to update the measurement for a cellId, instead of modifying in-place

        auto it = std::find_if(allMeas->cellMeasurements.begin(), allMeas->cellMeasurements.end(),
            [&](const CellMeasurement &m) { return m.cellId == cm.cellId; });

        if (it != allMeas->cellMeasurements.end())
            allMeas->cellMeasurements.erase(it);
        allMeas->cellMeasurements.insert(cm);
        
    }
    
    // remove stale entries
    auto it = allMeas->cellMeasurements.begin();
    while (it != allMeas->cellMeasurements.end()) {

        if (now - it->last_report_time > MEASUREMENT_STALE_THRESHOLD)
            it = allMeas->cellMeasurements.erase(it);
        else
            ++it;
    }
    // unlock for readers
    lock.unlock();

}


} // namespace nr::ue

