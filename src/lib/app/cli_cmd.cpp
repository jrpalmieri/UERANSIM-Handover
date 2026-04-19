//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "cli_cmd.hpp"

#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/options.hpp>
#include <utils/ordered_map.hpp>

#define CMD_ERR(x)                                                                                                     \
    {                                                                                                                  \
        error = x;                                                                                                     \
        return nullptr;                                                                                                \
    }

class OptionsHandler : public opt::IOptionsHandler
{
  public:
    std::stringstream m_output{};
    std::stringstream m_err{};

  public:
    std::ostream &ostream(bool isError) override
    {
        return isError ? m_err : m_output;
    }

    void status(int code) override
    {
        // nothing to do
    }
};

struct CmdEntry
{
    using DescType = opt::OptionsDescription (*)(const std::string &, const CmdEntry &);

    std::string descriptionText;
    std::string usageText;
    DescType descriptionFunc;
    bool helpIfEmpty;

    CmdEntry() = delete;

    CmdEntry(std::string descriptionText, std::string usageText, DescType descriptionFunc, bool helpIfEmpty)
        : descriptionText(std::move(descriptionText)), usageText(std::move(usageText)),
          descriptionFunc(descriptionFunc), helpIfEmpty(helpIfEmpty)
    {
    }
};

static std::string DumpCommands(const OrderedMap<std::string, CmdEntry> &entryTable)
{
    size_t maxLength = 0;
    for (auto &item : entryTable)
        maxLength = std::max(maxLength, item.size());

    std::stringstream ss{};
    for (auto &item : entryTable)
        ss << item << std::string(maxLength - item.size(), ' ') << " | " << entryTable[item].descriptionText << "\n";
    std::string output = ss.str();

    utils::Trim(output);
    return output;
}

static std::optional<opt::OptionsResult> ParseCliCommandCommon(OrderedMap<std::string, CmdEntry> &cmdEntries,
                                                               std::vector<std::string> &&tokens, std::string &error,
                                                               std::string &output, std::string &subCmd)
{
    if (tokens.empty())
    {
        error = "Empty command";
        return std::nullopt;
    }

    subCmd = tokens[0];

    if (subCmd == "commands")
    {
        output = DumpCommands(cmdEntries);
        return std::nullopt;
    }

    if (cmdEntries.count(subCmd) == 0)
    {
        error = "Command not recognized: " + subCmd;
        return std::nullopt;
    }

    opt::OptionsDescription desc = cmdEntries[subCmd].descriptionFunc(subCmd, cmdEntries[subCmd]);

    OptionsHandler handler{};

    opt::OptionsResult options{tokens, desc, &handler};

    error = handler.m_err.str();
    output = handler.m_output.str();
    utils::Trim(error);
    utils::Trim(output);

    if (!error.empty() || !output.empty())
        return {};

    return options;
}

//======================================================================================================
//                                      IMPLEMENTATION
//======================================================================================================

static opt::OptionsDescription DefaultDesc(const std::string &subCommand, const CmdEntry &entry)
{
    return {{}, {}, entry.descriptionText, {}, subCommand, {entry.usageText}, {}, entry.helpIfEmpty, true};
}

static opt::OptionsDescription DescForPsEstablish(const std::string &subCommand, const CmdEntry &entry)
{
    std::string example1 = "IPv4 --sst 1 --sd 1 --dnn internet";
    std::string example2 = "IPv4 --emergency";

    auto res = opt::OptionsDescription{
        {},  {}, entry.descriptionText, {}, subCommand, {entry.usageText}, {example1, example2}, entry.helpIfEmpty,
        true};

    res.items.emplace_back(std::nullopt, "sst", "SST value of the PDU session", "value");
    res.items.emplace_back(std::nullopt, "sd", "SD value of the PDU session", "value");
    res.items.emplace_back('n', "dnn", "DNN/APN value of the PDU session", "apn");
    res.items.emplace_back('e', "emergency", "Request as an emergency session", std::nullopt);

    return res;
}

static bool ParseLocPvArgument(const std::string &arg, app::GnbCliCommand &cmd)
{
    std::vector<std::string> tokens;
    std::stringstream ss(arg);
    std::string token;
    while (std::getline(ss, token, ':'))
        tokens.push_back(token);

    if (tokens.size() != 7)
        return false;

    try
    {
        cmd.locX = std::stod(tokens[0]);
        cmd.locY = std::stod(tokens[1]);
        cmd.locZ = std::stod(tokens[2]);
        cmd.velX = std::stod(tokens[3]);
        cmd.velY = std::stod(tokens[4]);
        cmd.velZ = std::stod(tokens[5]);
        cmd.epochMs = std::stoll(tokens[6]);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool ParseLocWgs84Argument(const std::string &arg, app::GnbCliCommand &cmd)
{
    std::vector<std::string> tokens;
    std::stringstream ss(arg);
    std::string token;
    while (std::getline(ss, token, ':'))
        tokens.push_back(token);

    if (tokens.size() != 3)
        return false;

    try
    {
        cmd.geoLat = std::stod(tokens[0]);
        cmd.geoLon = std::stod(tokens[1]);
        cmd.geoAlt = std::stod(tokens[2]);
    }
    catch (...)
    {
        return false;
    }

    if (cmd.geoLat < -90.0 || cmd.geoLat > 90.0)
        return false;
    if (cmd.geoLon < -180.0 || cmd.geoLon > 180.0)
        return false;

    return true;
}

static bool StartsWith(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static bool ParseSatTimeArg(const std::string &arg, app::SatTimeCliControl &control)
{
    if (arg == "pause")
    {
        control.action = app::SatTimeCliControl::EAction::Pause;
        return true;
    }
    if (arg == "run")
    {
        control.action = app::SatTimeCliControl::EAction::Run;
        return true;
    }
    if (StartsWith(arg, "tickscale="))
    {
        auto valueText = arg.substr(std::string("tickscale=").size());
        if (valueText.empty())
            return false;

        try
        {
            control.tickScale = std::stod(valueText);
        }
        catch (...)
        {
            return false;
        }

        control.action = app::SatTimeCliControl::EAction::TickScale;
        return true;
    }
    if (StartsWith(arg, "start-epoch="))
    {
        auto valueText = arg.substr(std::string("start-epoch=").size());
        utils::Trim(valueText);
        if (valueText.empty())
            return false;

        control.startEpoch = std::move(valueText);
        control.action = app::SatTimeCliControl::EAction::StartEpoch;
        return true;
    }
    if (StartsWith(arg, "pause-at-wallclock="))
    {
        auto valueText = arg.substr(std::string("pause-at-wallclock=").size());
        if (valueText.empty())
            return false;

        try
        {
            control.pauseAtWallclock = std::stoll(valueText);
        }
        catch (...)
        {
            return false;
        }

        control.action = app::SatTimeCliControl::EAction::PauseAtWallclock;
        return true;
    }

    return false;
}

namespace app
{

static OrderedMap<std::string, CmdEntry> g_gnbCmdEntries = {
    {"info", {"Show some information about the gNB", "", DefaultDesc, false}},
    {"status", {"Show some status information about the gNB", "", DefaultDesc, false}},
    {"ui-status", {"Show compact status information for UI polling", "", DefaultDesc, false}},
    {"amf-list", {"List all AMFs associated with the gNB", "", DefaultDesc, false}},
    {"amf-info", {"Show some status information about the given AMF", "<amf-id>", DefaultDesc, true}},
    {"ue-list", {"List all UEs associated with the gNB", "", DefaultDesc, false}},
    {"ue-count", {"Print the total number of UEs connected the this gNB", "", DefaultDesc, false}},
    {"ue-release", {"Request a UE context release for the given UE", "<ue-id>", DefaultDesc, false}},
    {"set-loc-wgs84", {"Set true gNB location as WGS84 coordinates. Format: lat:lon:alt",
                         "<lat:lon:alt>", DefaultDesc, true}},
    {"set-loc-pv", {"Set true gNB location as ECEF position/velocity. Format: x:y:z:vx:vy:vz:epoch-ms",
                      "<x:y:z:vx:vy:vz:epoch-ms>", DefaultDesc, true}},
    {"get-loc-wgs84", {"Get true gNB location as WGS84 JSON", "", DefaultDesc, false}},
    {"get-loc-pv", {"Get true gNB location as ECEF position/velocity JSON", "", DefaultDesc, false}},
    {"sat-loc-pv", {"Upsert one satellite SIB19 position/velocity entry from JSON payload",
                     "<json-payload>", DefaultDesc, true}},
    {"sat-tle", {"Upsert TLE orbital elements for one or more satellite gNBs from JSON payload",
                  "<json-payload>", DefaultDesc, true}},
    {"sat-time", {"Control satellite time: pause|run|tickscale=<v>|start-epoch=<YYDDD.DDD>|"
                    "pause-at-wallclock=<unix-ms>",
                    "[pause|run|tickscale=<v>|start-epoch=<YYDDD.DDD>|pause-at-wallclock=<unix-ms>]",
                    DefaultDesc,
                    false}},
    {"neighbors", {"Update gNB neighbor list from JSON payload", "<json-payload>", DefaultDesc, true}},
    {"version", {"Show gNB version information", "", DefaultDesc, false}},
};

static OrderedMap<std::string, CmdEntry> g_ueCmdEntries = {
    {"info", {"Show some information about the UE", "", DefaultDesc, false}},
    {"status", {"Show some status information about the UE", "", DefaultDesc, false}},
    {"ui-status", {"Show compact status information for UI polling", "", DefaultDesc, false}},
    {"timers", {"Dump current status of the timers in the UE", "", DefaultDesc, false}},
    {"rls-state", {"Show status information about RLS", "", DefaultDesc, false}},
    {"coverage", {"Dump available cells and PLMNs in the coverage", "", DefaultDesc, false}},
    {"ps-establish",
     {"Trigger a PDU session establishment procedure", "<session-type> [options]", DescForPsEstablish, true}},
    {"ps-list", {"List all PDU sessions", "", DefaultDesc, false}},
    {"ps-release", {"Trigger a PDU session release procedure", "<pdu-session-id>...", DefaultDesc, true}},
    {"ps-release-all", {"Trigger PDU session release procedures for all active sessions", "", DefaultDesc, false}},
    {"deregister",
     {"Perform a de-registration by the UE", "<normal|disable-5g|switch-off|remove-sim>", DefaultDesc, true}},
    {"sat-time", {"Control satellite time: pause|run|tickscale=<v>|start-epoch=<YYDDD.DDD>|"
                    "pause-at-wallclock=<unix-ms>",
                    "[pause|run|tickscale=<v>|start-epoch=<YYDDD.DDD>|pause-at-wallclock=<unix-ms>]",
                    DefaultDesc,
                    false}},
    {"version", {"Show UE version information", "", DefaultDesc, false}},
};

static std::unique_ptr<GnbCliCommand> GnbCliParseImpl(const std::string &subCmd, const opt::OptionsResult &options,
                                                      std::string &error)
{
    if (subCmd == "info")
    {
        return std::make_unique<GnbCliCommand>(GnbCliCommand::INFO);
    }
    if (subCmd == "status")
    {
        return std::make_unique<GnbCliCommand>(GnbCliCommand::STATUS);
    }
    else if (subCmd == "ui-status")
    {
        return std::make_unique<GnbCliCommand>(GnbCliCommand::UI_STATUS);
    }
    else if (subCmd == "amf-list")
    {
        return std::make_unique<GnbCliCommand>(GnbCliCommand::AMF_LIST);
    }
    else if (subCmd == "amf-info")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::AMF_INFO);
        if (options.positionalCount() == 0)
            CMD_ERR("AMF ID is expected")
        if (options.positionalCount() > 1)
            CMD_ERR("Only one AMF ID is expected")
        cmd->amfId = utils::ParseInt(options.getPositional(0));
        if (cmd->amfId <= 0)
            CMD_ERR("Invalid AMF ID")
        return cmd;
    }
    else if (subCmd == "ue-list")
    {
        return std::make_unique<GnbCliCommand>(GnbCliCommand::UE_LIST);
    }
    else if (subCmd == "ue-count")
    {
        return std::make_unique<GnbCliCommand>(GnbCliCommand::UE_COUNT);
    }
    else if (subCmd == "ue-release")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::UE_RELEASE_REQ);
        if (options.positionalCount() == 0)
            CMD_ERR("UE ID is expected")
        if (options.positionalCount() > 1)
            CMD_ERR("Only one UE ID is expected")
        cmd->ueId = utils::ParseInt(options.getPositional(0));
        if (cmd->ueId <= 0)
            CMD_ERR("Invalid UE ID")
        return cmd;
    }
    else if (subCmd == "version")
    {
        return std::make_unique<GnbCliCommand>(GnbCliCommand::VERSION);
    }
    else if (subCmd == "set-loc-wgs84")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::SET_LOC_WGS84);
        if (options.positionalCount() == 0)
            CMD_ERR("WGS84 position argument is expected")
        if (options.positionalCount() > 1)
            CMD_ERR("Only one WGS84 position argument is expected")

        if (!ParseLocWgs84Argument(options.getPositional(0), *cmd))
            CMD_ERR("Invalid format. Expected lat:lon:alt with valid WGS84 bounds")

        return cmd;
    }
    else if (subCmd == "set-loc-pv")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::SET_LOC_PV);
        if (options.positionalCount() == 0)
            CMD_ERR("Position/velocity argument is expected")
        if (options.positionalCount() > 1)
            CMD_ERR("Only one position/velocity argument is expected")

        if (!ParseLocPvArgument(options.getPositional(0), *cmd))
            CMD_ERR("Invalid format. Expected x:y:z:vx:vy:vz:epoch-ms")

        return cmd;
    }
    else if (subCmd == "get-loc-wgs84")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::GET_LOC_WGS84);
        if (options.positionalCount() > 0)
            CMD_ERR("No argument is expected")
        return cmd;
    }
    else if (subCmd == "get-loc-pv")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::GET_LOC_PV);
        if (options.positionalCount() > 0)
            CMD_ERR("No argument is expected")
        return cmd;
    }
    else if (subCmd == "neighbors")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::NEIGHBORS);
        if (options.positionalCount() == 0)
            CMD_ERR("JSON payload is expected")
        if (options.positionalCount() > 1)
            CMD_ERR("Only one JSON payload argument is expected")

        cmd->neighborsJson = options.getPositional(0);
        return cmd;
    }
    else if (subCmd == "sat-loc-pv")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::SAT_LOC_PV);
        if (options.positionalCount() == 0)
            CMD_ERR("JSON payload is expected")
        if (options.positionalCount() > 1)
            CMD_ERR("Only one JSON payload argument is expected")

        cmd->satLocPvJson = options.getPositional(0);
        return cmd;
    }
    else if (subCmd == "sat-tle")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::SAT_TLE);
        if (options.positionalCount() == 0)
            CMD_ERR("JSON payload is expected")
        if (options.positionalCount() > 1)
            CMD_ERR("Only one JSON payload argument is expected")

        cmd->satTleJson = options.getPositional(0);
        return cmd;
    }
    else if (subCmd == "sat-time")
    {
        auto cmd = std::make_unique<GnbCliCommand>(GnbCliCommand::SAT_TIME);
        if (options.positionalCount() == 0)
        {
            cmd->satTime.action = SatTimeCliControl::EAction::Status;
            return cmd;
        }
        if (options.positionalCount() > 1)
            CMD_ERR("Only one sat-time argument is expected")

        if (!ParseSatTimeArg(options.getPositional(0), cmd->satTime))
        {
            CMD_ERR("Invalid sat-time argument. Expected pause|run|tickscale=<v>|start-epoch=<YYDDD.DDD>|"
                    "pause-at-wallclock=<unix-ms>")
        }

        return cmd;
    }

    return nullptr;
}

static std::unique_ptr<UeCliCommand> UeCliParseImpl(const std::string &subCmd, const opt::OptionsResult &options,
                                                    std::string &error)
{
    if (subCmd == "info")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::INFO);
    }
    else if (subCmd == "status")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::STATUS);
    }
    else if (subCmd == "ui-status")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::UI_STATUS);
    }
    else if (subCmd == "timers")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::TIMERS);
    }
    else if (subCmd == "deregister")
    {
        auto cmd = std::make_unique<UeCliCommand>(UeCliCommand::DE_REGISTER);
        if (options.positionalCount() == 0)
            CMD_ERR("De-registration type is expected")
        if (options.positionalCount() > 1)
            CMD_ERR("Only one de-registration type is expected")
        auto type = options.getPositional(0);
        if (type == "normal")
            cmd->deregCause = EDeregCause::NORMAL;
        else if (type == "switch-off")
            cmd->deregCause = EDeregCause::SWITCH_OFF;
        else if (type == "disable-5g")
            cmd->deregCause = EDeregCause::DISABLE_5G;
        else if (type == "remove-sim")
            cmd->deregCause = EDeregCause::USIM_REMOVAL;
        else
            CMD_ERR("Invalid de-registration type, possible values are: \"normal\", \"disable-5g\", \"switch-off\", "
                    "\"remove-sim\"")
        return cmd;
    }
    else if (subCmd == "sat-time")
    {
        auto cmd = std::make_unique<UeCliCommand>(UeCliCommand::SAT_TIME);
        if (options.positionalCount() == 0)
        {
            cmd->satTime.action = SatTimeCliControl::EAction::Status;
            return cmd;
        }
        if (options.positionalCount() > 1)
            CMD_ERR("Only one sat-time argument is expected")

        if (!ParseSatTimeArg(options.getPositional(0), cmd->satTime))
        {
            CMD_ERR("Invalid sat-time argument. Expected pause|run|tickscale=<v>|start-epoch=<YYDDD.DDD>|"
                    "pause-at-wallclock=<unix-ms>")
        }

        return cmd;
    }
    else if (subCmd == "ps-release")
    {
        auto cmd = std::make_unique<UeCliCommand>(UeCliCommand::PS_RELEASE);
        if (options.positionalCount() == 0)
            CMD_ERR("At least one PDU session ID is expected")
        if (options.positionalCount() > 15)
            CMD_ERR("Too many PDU session IDs")
        cmd->psCount = options.positionalCount();
        for (int i = 0; i < cmd->psCount; i++)
        {
            int n = 0;
            if (!utils::TryParseInt(options.getPositional(i), n))
                CMD_ERR("Invalid PDU session ID value")
            if (n <= 0)
                CMD_ERR("PDU session IDs must be positive integer")
            if (n > 15)
                CMD_ERR("PDU session IDs cannot be greater than 15")
            cmd->psIds[i] = static_cast<int8_t>(n);
        }
        return cmd;
    }
    else if (subCmd == "ps-release-all")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::PS_RELEASE_ALL);
    }
    else if (subCmd == "ps-establish")
    {
        auto cmd = std::make_unique<UeCliCommand>(UeCliCommand::PS_ESTABLISH);
        if (options.positionalCount() == 0)
            CMD_ERR("PDU session type is expected")
        if (options.positionalCount() > 15)
            CMD_ERR("Only one PDU session type is expected")
        std::string type = options.getPositional(0);
        if (type != "IPv4" && type != "ipv4" && type != "IPV4" && type != "Ipv4" && type != "IpV4")
            CMD_ERR("Only IPv4 is supported for now")
        cmd->isEmergency = options.hasFlag('e', "emergency");
        if (cmd->isEmergency)
        {
            if (options.hasFlag(std::nullopt, "sst") || options.hasFlag(std::nullopt, "sd") ||
                options.hasFlag('n', "dnn"))
                CMD_ERR("SST, SD, and DNN parameters cannot be used for emergency PDU sessions")
        }
        if (options.hasFlag('n', "dnn"))
            cmd->apn = options.getOption('n', "dnn");
        if (options.hasFlag(std::nullopt, "sd") && !options.hasFlag(std::nullopt, "sst"))
            CMD_ERR("SST is also required in case of an SD is provided")
        if (options.hasFlag(std::nullopt, "sst"))
        {
            int n = 0;
            if (!utils::TryParseInt(options.getOption(std::nullopt, "sst"), n) || n <= 0 || n >= 256)
                CMD_ERR("Invalid SST value")
            cmd->sNssai = SingleSlice{};
            cmd->sNssai->sst = static_cast<uint8_t>(n);

            if (options.hasFlag(std::nullopt, "sd"))
            {
                if (!utils::TryParseInt(options.getOption(std::nullopt, "sd"), n) || n <= 0 || n > 0xFFFFFF)
                    CMD_ERR("Invalid SD value")
                cmd->sNssai->sd = octet3{n};
            }
        }
        return cmd;
    }
    else if (subCmd == "ps-list")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::PS_LIST);
    }
    else if (subCmd == "rls-state")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::RLS_STATE);
    }
    else if (subCmd == "coverage")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::COVERAGE);
    }
    else if (subCmd == "version")
    {
        return std::make_unique<UeCliCommand>(UeCliCommand::VERSION);
    }

    return nullptr;
}

std::unique_ptr<GnbCliCommand> ParseGnbCliCommand(std::vector<std::string> &&tokens, std::string &error,
                                                  std::string &output)
{
    std::string subCmd{};
    auto options = ParseCliCommandCommon(g_gnbCmdEntries, std::move(tokens), error, output, subCmd);
    if (options.has_value())
        return GnbCliParseImpl(subCmd, *options, error);
    return nullptr;
}

std::unique_ptr<UeCliCommand> ParseUeCliCommand(std::vector<std::string> &&tokens, std::string &error,
                                                std::string &output)
{
    std::string subCmd{};
    auto options = ParseCliCommandCommon(g_ueCmdEntries, std::move(tokens), error, output, subCmd);
    if (options.has_value())
        return UeCliParseImpl(subCmd, *options, error);
    return nullptr;
}

} // namespace app
