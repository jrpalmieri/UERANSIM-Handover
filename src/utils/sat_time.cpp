#include "sat_time.hpp"

#include <cmath>
#include <stdexcept>

#include <utils/common.hpp>

namespace utils
{

SatTime::SatTime(int64_t startEpochMillis,
                 EStartCondition startCondition,
                 double tickScaling,
                 std::function<int64_t()> wallclockNow)
    : m_wallclockNow(std::move(wallclockNow)),
      m_startEpochMillis(startEpochMillis),
      m_anchorSatMillis(startEpochMillis),
      m_pausedSatMillis(startEpochMillis),
      m_tickScaling(tickScaling),
      m_paused(startCondition == EStartCondition::Paused)
{
    if (!m_wallclockNow)
    {
        m_wallclockNow = []() {
            return utils::CurrentTimeMillis();
        };
    }

    if (!std::isfinite(tickScaling) || tickScaling <= 0.0)
        throw std::invalid_argument("SatTime tickScaling must be a finite value greater than zero");

    int64_t now = m_wallclockNow();
    m_anchorWallclockMillis = now;
}

int64_t SatTime::satTimeAtWallclockLocked(int64_t wallclockMillis) const
{
    if (m_paused)
        return m_pausedSatMillis;

    const int64_t deltaWallclockMillis = wallclockMillis - m_anchorWallclockMillis;
    const double scaledDelta = static_cast<double>(deltaWallclockMillis) * m_tickScaling;
    return m_anchorSatMillis + static_cast<int64_t>(std::llround(scaledDelta));
}

void SatTime::applyScheduledPauseIfDueLocked(int64_t wallclockMillis)
{
    if (!m_scheduledPauseWallclockMillis.has_value() || m_paused)
        return;

    if (wallclockMillis < *m_scheduledPauseWallclockMillis)
        return;

    m_pausedSatMillis = satTimeAtWallclockLocked(wallclockMillis);
    m_paused = true;
    m_scheduledPauseWallclockMillis.reset();
}

int64_t SatTime::CurrentSatTimeMillis()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    int64_t now = m_wallclockNow();
    applyScheduledPauseIfDueLocked(now);
    return satTimeAtWallclockLocked(now);
}

void SatTime::SetPaused(bool paused)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    int64_t now = m_wallclockNow();
    applyScheduledPauseIfDueLocked(now);

    if (paused == m_paused)
        return;

    if (paused)
    {
        m_pausedSatMillis = satTimeAtWallclockLocked(now);
        m_paused = true;
        return;
    }

    m_anchorSatMillis = m_pausedSatMillis;
    m_anchorWallclockMillis = now;
    m_paused = false;
}

void SatTime::SetTickScaling(double tickScaling)
{
    if (!std::isfinite(tickScaling) || tickScaling <= 0.0)
        throw std::invalid_argument("SatTime tickScaling must be a finite value greater than zero");

    std::lock_guard<std::mutex> guard(m_mutex);
    int64_t now = m_wallclockNow();
    applyScheduledPauseIfDueLocked(now);

    if (m_paused)
    {
        m_tickScaling = tickScaling;
        return;
    }

    m_anchorSatMillis = satTimeAtWallclockLocked(now);
    m_anchorWallclockMillis = now;
    m_tickScaling = tickScaling;
}

void SatTime::SetStartEpochMillis(int64_t startEpochMillis)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    int64_t now = m_wallclockNow();
    applyScheduledPauseIfDueLocked(now);

    m_startEpochMillis = startEpochMillis;
    m_anchorSatMillis = startEpochMillis;
    m_pausedSatMillis = startEpochMillis;
    m_anchorWallclockMillis = now;
}

void SatTime::SchedulePauseAtWallclockMillis(int64_t wallclockMillis)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    int64_t now = m_wallclockNow();
    applyScheduledPauseIfDueLocked(now);

    if (m_paused)
    {
        m_scheduledPauseWallclockMillis.reset();
        return;
    }

    if (wallclockMillis <= now)
    {
        m_pausedSatMillis = satTimeAtWallclockLocked(now);
        m_paused = true;
        m_scheduledPauseWallclockMillis.reset();
        return;
    }

    m_scheduledPauseWallclockMillis = wallclockMillis;
}

void SatTime::ClearScheduledPause()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_scheduledPauseWallclockMillis.reset();
}

SatTime::Status SatTime::GetStatus()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    int64_t now = m_wallclockNow();
    applyScheduledPauseIfDueLocked(now);

    Status status{};
    status.satTimeMillis = satTimeAtWallclockLocked(now);
    status.wallclockMillis = now;
    status.startEpochMillis = m_startEpochMillis;
    status.tickScaling = m_tickScaling;
    status.paused = m_paused;
    status.scheduledPauseWallclockMillis = m_scheduledPauseWallclockMillis;
    return status;
}

} // namespace utils
