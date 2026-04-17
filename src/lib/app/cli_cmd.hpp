//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <utils/common_types.hpp>

namespace app
{

struct SatTimeCliControl
{
    enum class EAction
    {
        Status,
        Pause,
        Run,
        TickScale,
        StartEpoch,
        PauseAtWallclock,
    };

    EAction action{EAction::Status};
    double tickScale{};
    std::string startEpoch{};
    int64_t pauseAtWallclock{};
};

struct GnbCliCommand
{
    enum PR
    {
        STATUS,
        UI_STATUS,
        INFO,
        AMF_LIST,
        AMF_INFO,
        UE_LIST,
        UE_COUNT,
        UE_RELEASE_REQ,
        LOC_PV,
        SAT_LOC_PV,
        SAT_TLE,
        SAT_TIME,
        NEIGHBORS,
        VERSION,
    } present;

    // AMF_INFO
    int amfId{};

    // UE_RELEASE_REQ
    int ueId{};

    // LOC_PV
    double locX{};
    double locY{};
    double locZ{};
    double velX{};
    double velY{};
    double velZ{};
    int64_t epochMs{};

    // SAT_LOC_PV
    std::string satLocPvJson{};

    // SAT_TLE
    std::string satTleJson{};

    // SAT_TIME
    SatTimeCliControl satTime{};

    // NEIGHBORS
    std::string neighborsJson{};

    explicit GnbCliCommand(PR present) : present(present)
    {
    }
};

struct UeCliCommand
{
    enum PR
    {
        INFO,
        STATUS,
        UI_STATUS,
        TIMERS,
        PS_ESTABLISH,
        PS_RELEASE,
        PS_RELEASE_ALL,
        PS_LIST,
        DE_REGISTER,
        SAT_TIME,
        RLS_STATE,
        COVERAGE,
        VERSION,
    } present;

    // DE_REGISTER
    EDeregCause deregCause{};

    // PS_RELEASE
    std::array<int8_t, 16> psIds{};
    int psCount{};

    // PS_ESTABLISH
    std::optional<SingleSlice> sNssai{};
    std::optional<std::string> apn{};
    bool isEmergency{};

    // SAT_TIME
    SatTimeCliControl satTime{};

    explicit UeCliCommand(PR present) : present(present)
    {
    }
};

std::unique_ptr<GnbCliCommand> ParseGnbCliCommand(std::vector<std::string> &&tokens, std::string &error,
                                                  std::string &output);

std::unique_ptr<UeCliCommand> ParseUeCliCommand(std::vector<std::string> &&tokens, std::string &error,
                                                std::string &output);

} // namespace app