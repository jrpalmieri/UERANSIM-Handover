//
// Satellite position propagation task implementation.
//

#include "sat_position_task.hpp"

#include <utils/common.hpp>

// Propagation interval in milliseconds
static constexpr int PROPAGATION_INTERVAL_MS = 100;
// Timeout waiting for NTS messages
static constexpr int RECEIVE_TIMEOUT_MS = 50;

namespace nr::gnb
{

SatPositionTask::SatPositionTask(LogBase *logBase,
                                 SatelliteState *satState)
    : m_satState{satState}, m_lastPropMs{}
{
    m_logger = logBase->makeUniqueLogger("sat-pos");
}

void SatPositionTask::onStart()
{
    m_logger->info("Satellite position task started");
}

void SatPositionTask::onLoop()
{
    // Consume any queued NTS messages (timer, etc.)
    auto msg = poll(RECEIVE_TIMEOUT_MS);
    (void)msg;

    int64_t now = utils::CurrentTimeMillis();
    if (now - m_lastPropMs < PROPAGATION_INTERVAL_MS)
        return;
    m_lastPropMs = now;

    OrbitalElements elems{};
    if (!m_satState->getOrbitalElements(elems))
        return;  // no TLE received yet — nothing to propagate

    int64_t dt = now - elems.epochMs;
    EcefPosition pos = PropagateSatellite(elems, dt);
    m_satState->setPosition(pos, now);
}

void SatPositionTask::onQuit()
{
    m_logger->info("Satellite position task stopped");
}

} // namespace nr::gnb
