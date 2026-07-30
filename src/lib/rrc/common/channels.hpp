//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <string>

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

inline std::string RrcChannelToString(RrcChannel value)
{
    switch (value)
    {
    case RrcChannel::BCCH_BCH:
        return "BCCH_BCH";
    case RrcChannel::BCCH_DL_SCH:
        return "BCCH_DL_SCH";
    case RrcChannel::DL_CCCH:
        return "DL_CCCH";
    case RrcChannel::DL_DCCH:
        return "DL_DCCH";
    case RrcChannel::PCCH:
        return "PCCH";
    case RrcChannel::UL_CCCH:
        return "UL_CCCH";
    case RrcChannel::UL_CCCH1:
        return "UL_CCCH1";
    case RrcChannel::UL_DCCH:
        return "UL_DCCH";
    case RrcChannel::DL_CHO:
        return "DL_CHO";
    case RrcChannel::DL_SIB19:
        return "DL_SIB19";
    }

    return "UNKNOWN";
}

} // namespace nr::rrc::common
