//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "sat_time.hpp"

#include <atomic>

#include <utils/common.hpp>
#include <utils/sat_time.hpp>

namespace nr::gnb::sat_time
{

static std::atomic<utils::SatTime *> g_satTime{nullptr};

void SetSatTimeSource(utils::SatTime *satTime)
{
    g_satTime.store(satTime, std::memory_order_relaxed);
}

libsgp4::DateTime Now()
{
    int64_t satMillis = utils::CurrentTimeMillis();
    if (auto *satTime = g_satTime.load(std::memory_order_relaxed); satTime != nullptr)
        satMillis = satTime->CurrentSatTimeMillis();

    const int64_t wallclockMillis = utils::CurrentTimeMillis();
    const int64_t offsetMillis = satMillis - wallclockMillis;
    const double offsetSeconds = static_cast<double>(offsetMillis) / 1000.0;
    return libsgp4::DateTime::Now().AddSeconds(offsetSeconds);
}

} // namespace nr::gnb::sat_time
