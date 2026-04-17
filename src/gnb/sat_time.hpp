//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <libsgp4/DateTime.h>

namespace utils
{
class SatTime;
}

namespace nr::gnb::sat_time
{

void SetSatTimeSource(utils::SatTime *satTime);
libsgp4::DateTime Now();

} // namespace nr::gnb::sat_time
