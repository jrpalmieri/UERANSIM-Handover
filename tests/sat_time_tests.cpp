#include <lib/sat/sat_time.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

struct FakeClock
{
    int64_t nowMs{0};

    int64_t now() const
    {
        return nowMs;
    }

    void advance(int64_t deltaMs)
    {
        nowMs += deltaMs;
    }
};

void assertEq(int64_t actual, int64_t expected, const std::string &msg)
{
    if (actual != expected)
    {
        throw std::runtime_error(msg + " expected=" + std::to_string(expected) +
                                 " actual=" + std::to_string(actual));
    }
}

void assertNear(int64_t actual, int64_t expected, int64_t tolerance, const std::string &msg)
{
    int64_t delta = std::llabs(actual - expected);
    if (delta > tolerance)
    {
        throw std::runtime_error(msg + " expected~=" + std::to_string(expected) +
                                 " actual=" + std::to_string(actual) +
                                 " tolerance=" + std::to_string(tolerance));
    }
}

void testMovingClockAtScaleOne()
{
    FakeClock clock{1000};
    nr::sat::SatTime satTime(5000,
                             nr::sat::ESatTimeState::Moving,
                             1.0,
                             [&clock]() {
                                 return clock.now();
                             });

    assertEq(satTime.CurrentSatTimeMillis(), 5000, "start epoch mismatch");
    clock.advance(250);
    assertEq(satTime.CurrentSatTimeMillis(), 5250, "moving scale=1 mismatch");
}

void testPausedStartAndRun()
{
    FakeClock clock{500};
    nr::sat::SatTime satTime(12345,
                             nr::sat::ESatTimeState::Paused,
                             1.0,
                             [&clock]() {
                                 return clock.now();
                           });

    clock.advance(1000);
    assertEq(satTime.CurrentSatTimeMillis(), 12345, "paused clock should not move");

    satTime.SetPaused(false);
    clock.advance(200);
    assertEq(satTime.CurrentSatTimeMillis(), 12545, "clock should resume from paused point");
}

void testScaleChangeKeepsContinuity()
{
    FakeClock clock{0};
    nr::sat::SatTime satTime(0,
                              nr::sat::ESatTimeState::Moving,
                              1.0,
                              [&clock]() {
                                  return clock.now();
                              });

    clock.advance(1000);
    assertEq(satTime.CurrentSatTimeMillis(), 1000, "pre-scale continuity mismatch");

    satTime.SetTickScaling(0.5);
    clock.advance(1000);
    assertNear(satTime.CurrentSatTimeMillis(), 1500, 1, "post-scale continuity mismatch");
}

void testScheduledPause()
{
    FakeClock clock{100};
    nr::sat::SatTime satTime(1000,
                              nr::sat::ESatTimeState::Moving,
                              2.0,
                              [&clock]() {
                                  return clock.now();
                              });

    satTime.SchedulePauseAtWallclockMillis(400);

    clock.advance(200);
    assertNear(satTime.CurrentSatTimeMillis(), 1400, 1, "scheduled pause pre-threshold mismatch");

    clock.advance(100);
    int64_t pausedSat = satTime.CurrentSatTimeMillis();
    clock.advance(500);
    assertEq(satTime.CurrentSatTimeMillis(), pausedSat, "scheduled pause did not freeze clock");
}

void testSetStartEpochResetsDomainClock()
{
    FakeClock clock{0};
    nr::sat::SatTime satTime(10,
                              nr::sat::ESatTimeState::Moving,
                              1.0,
                              [&clock]() {
                                  return clock.now();
                              });

    clock.advance(100);
    assertEq(satTime.CurrentSatTimeMillis(), 110, "pre-reset mismatch");

    satTime.SetStartEpochMillis(5000);
    assertEq(satTime.CurrentSatTimeMillis(), 5000, "reset mismatch at set time");

    clock.advance(20);
    assertEq(satTime.CurrentSatTimeMillis(), 5020, "reset mismatch after run");
}

} // namespace

int main()
{
    try
    {
        testMovingClockAtScaleOne();
        testPausedStartAndRun();
        testScaleChangeKeepsContinuity();
        testScheduledPause();
        testSetStartEpochResetsDomainClock();
    }
    catch (const std::exception &e)
    {
        std::cerr << "sat_time_tests: FAILED: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "sat_time_tests: OK" << std::endl;
    return 0;
}
