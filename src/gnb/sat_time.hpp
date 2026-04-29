//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <libsgp4/DateTime.h>

namespace nr::sat
{
class SatTime;
}

namespace nr::gnb::sat_time
{

void SetSatTimeSource(nr::sat::SatTime *satTime);
libsgp4::DateTime Now();

} // namespace nr::gnb::sat_time
