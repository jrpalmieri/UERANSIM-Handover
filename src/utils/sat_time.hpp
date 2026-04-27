//
// Shared satellite-time domain clock with pause/run and tick scaling controls.
//

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace utils
{

class SatTime
{
  public:
    enum class EStartCondition
    {
        Moving,
        Paused,
    };

    struct Status
    {
        int64_t satTimeMillis{};
      int64_t wallclockMillis{};
        int64_t startEpochMillis{};
        double tickScaling{1.0};
        bool paused{false};
        std::optional<int64_t> scheduledPauseWallclockMillis{};
    std::optional<int64_t> scheduledRunWallclockMillis{};
    };

    explicit SatTime(int64_t startEpochMillis,
                     EStartCondition startCondition = EStartCondition::Moving,
                     double tickScaling = 1.0,
                     std::function<int64_t()> wallclockNow = {});

    int64_t CurrentSatTimeMillis();

    void SetPaused(bool paused);
    void SetTickScaling(double tickScaling);
    void SetStartEpochMillis(int64_t startEpochMillis);
    void SchedulePauseAtWallclockMillis(int64_t wallclockMillis);
    void ScheduleRunAtWallclockMillis(int64_t wallclockMillis);
    void ClearScheduledPause();

    [[nodiscard]] Status GetStatus();

  private:
    std::function<int64_t()> m_wallclockNow;
    std::mutex m_mutex;

    int64_t m_startEpochMillis{};
    int64_t m_anchorWallclockMillis{};
    int64_t m_anchorSatMillis{};
    int64_t m_pausedSatMillis{};
    double m_tickScaling{1.0};
    bool m_paused{false};
    std::optional<int64_t> m_scheduledPauseWallclockMillis{};
    std::optional<int64_t> m_scheduledRunWallclockMillis{};

    int64_t satTimeAtWallclockLocked(int64_t wallclockMillis) const;
    void applyScheduledPauseIfDueLocked(int64_t wallclockMillis);
    void applyScheduledRunIfDueLocked(int64_t wallclockMillis);
};

} // namespace utils
