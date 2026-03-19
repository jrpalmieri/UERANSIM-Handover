//
// Periodic satellite position propagation task.
// Reads orbital elements from the shared SatelliteState,
// propagates forward, and writes the updated ECEF position.
//

#pragma once

#include "satellite_state.hpp"

#include <utils/logger.hpp>
#include <utils/nts.hpp>

namespace nr::gnb
{

class SatPositionTask : public NtsTask
{
  private:
    std::unique_ptr<Logger> m_logger;
    SatelliteState *m_satState;
    int64_t m_lastPropMs{};

  public:
    explicit SatPositionTask(LogBase *logBase,
                             SatelliteState *satState);
    ~SatPositionTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;
};

} // namespace nr::gnb
