//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

namespace nr::rrc::common
{

enum class RrcChannel
{
    BCCH_BCH,
    BCCH_DL_SCH,
    DL_CCCH,
    DL_DCCH,
    PCCH,
    UL_CCCH,
    UL_CCCH1,
    UL_DCCH,
    DL_CHO,   // Custom channel for Conditional Handover configuration
    DL_SIB19, // Custom channel for SIB19 NTN configuration (Rel-17)
};

} // namespace nr::rrc::common
