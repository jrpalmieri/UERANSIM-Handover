//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "sat_time.hpp"

#include <utils/common.hpp>

namespace nr::gnb::sat_time
{

libsgp4::DateTime Now()
{
    const int64_t offsetMillis = utils::GetTimeWarpOffsetMillis();
    const double offsetSeconds = static_cast<double>(offsetMillis) / 1000.0;
    return libsgp4::DateTime::Now().AddSeconds(offsetSeconds);
}

} // namespace nr::gnb::sat_time
