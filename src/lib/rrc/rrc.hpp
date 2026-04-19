//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <lib/rrc/common/channels.hpp>

namespace nr::rrc
{

using RrcChannel = common::RrcChannel;

} // namespace nr::rrc

namespace rrc
{

using RrcChannel = nr::rrc::RrcChannel;

} // namespace rrc
