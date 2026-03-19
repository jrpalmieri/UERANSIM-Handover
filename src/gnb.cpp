//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <unordered_map>

#include <unistd.h>

#include <gnb/gnb.hpp>
#include <lib/app/base_app.hpp>
#include <lib/app/cli_base.hpp>
#include <lib/app/cli_cmd.hpp>
#include <lib/app/proc_table.hpp>
#include <utils/constants.hpp>
#include <utils/io.hpp>
#include <utils/options.hpp>
#include <utils/yaml_utils.hpp>
#include <yaml-cpp/yaml.h>

static app::CliServer *g_cliServer = nullptr;
static nr::gnb::GnbConfig *g_refConfig = nullptr;
static std::unordered_map<std::string, nr::gnb::GNodeB *> g_gnbMap{};
static app::CliResponseTask *g_cliRespTask = nullptr;

static struct Options
{
    std::string configFile{};
    bool disableCmd{};
} g_options{};

static nr::gnb::EGnbRsrpMode ReadRsrpMode(const YAML::Node &rsrpNode)
{
    auto mode = yaml::GetString(rsrpNode, "updateMode");
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (mode == "calculated")
        return nr::gnb::EGnbRsrpMode::Calculated;
    if (mode == "fixed")
        return nr::gnb::EGnbRsrpMode::Fixed;

    throw std::runtime_error(
        "Field rsrp.updateMode has invalid value, expected 'Calculated' or 'Fixed'");
}

static std::string ReadHandoverEventType(const YAML::Node &node)
{
    auto value = node.as<std::string>();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    if (value == "A2" || value == "A3" || value == "A5")
        return value;

    throw std::runtime_error(
        "Field handover.eventType has invalid value, expected A2, A3 or A5");
}

static std::vector<std::string> ReadHandoverEventTypes(const YAML::Node &handoverNode)
{
    std::vector<std::string> eventTypes{};
    auto eventTypeNode = handoverNode["eventType"];
    if (!eventTypeNode)
        return eventTypes;

    if (eventTypeNode.IsSequence())
    {
        for (const auto &entry : eventTypeNode)
        {
            eventTypes.push_back(ReadHandoverEventType(entry));
        }
    }
    else
    {
        eventTypes.push_back(ReadHandoverEventType(eventTypeNode));
    }

    std::sort(eventTypes.begin(), eventTypes.end());
    eventTypes.erase(std::unique(eventTypes.begin(), eventTypes.end()), eventTypes.end());
    return eventTypes;
}

static nr::gnb::EHandoverInterface ReadHandoverInterface(const YAML::Node &node)
{
    auto value = node.as<std::string>();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    if (value == "N2")
        return nr::gnb::EHandoverInterface::N2;
    if (value == "XN")
        return nr::gnb::EHandoverInterface::Xn;

    throw std::runtime_error(
        "Field neighborList[].handoverInterface has invalid value, expected N2 or Xn");
}

static nr::gnb::GnbConfig *ReadConfigYaml()
{
    auto *result = new nr::gnb::GnbConfig();
    auto config = YAML::LoadFile(g_options.configFile);

    result->plmn.mcc = yaml::GetInt32(config, "mcc", 1, 999);
    yaml::GetString(config, "mcc", 3, 3);
    result->plmn.mnc = yaml::GetInt32(config, "mnc", 0, 999);
    result->plmn.isLongMnc = yaml::GetString(config, "mnc", 2, 3).size() != 2;

    result->nci = yaml::GetInt64(config, "nci", 0, 0xFFFFFFFFFll);
    result->gnbIdLength = yaml::GetInt32(config, "idLength", 22, 32);
    result->tac = yaml::GetInt32(config, "tac", 0, 0xFFFFFF);

    result->linkIp = yaml::GetIpAddress(config, "linkIp");
    result->ngapIp = yaml::GetIpAddress(config, "ngapIp");
    result->gtpIp = yaml::GetIpAddress(config, "gtpIp");

    if (yaml::HasField(config, "gtpAdvertiseIp"))
        result->gtpAdvertiseIp = yaml::GetIpAddress(config, "gtpAdvertiseIp");

    result->ignoreStreamIds = yaml::GetBool(config, "ignoreStreamIds");

    if (yaml::HasField(config, "rsrp"))
    {
        auto rsrp = config["rsrp"];
        result->rsrp.dbValue = yaml::GetInt32(rsrp, "dbValue", cons::MIN_RSRP, cons::MAX_RSRP);
        result->rsrp.updateMode = ReadRsrpMode(rsrp);
    }

    if (yaml::HasField(config, "handover"))
    {
        auto handover = config["handover"];

        auto eventTypes = ReadHandoverEventTypes(handover);
        if (!eventTypes.empty())
            result->handover.eventTypes = std::move(eventTypes);

        if (yaml::HasField(handover, "a2ThresholdDbm"))
            result->handover.a2ThresholdDbm = yaml::GetInt32(handover, "a2ThresholdDbm", -156, -31);
        if (yaml::HasField(handover, "a3OffsetDb"))
            result->handover.a3OffsetDb = yaml::GetInt32(handover, "a3OffsetDb", -15, 15);
        if (yaml::HasField(handover, "a5Threshold1Dbm"))
            result->handover.a5Threshold1Dbm = yaml::GetInt32(handover, "a5Threshold1Dbm", -156, -31);
        if (yaml::HasField(handover, "a5Threshold2Dbm"))
            result->handover.a5Threshold2Dbm = yaml::GetInt32(handover, "a5Threshold2Dbm", -156, -31);
        if (yaml::HasField(handover, "hysteresisDb"))
            result->handover.hysteresisDb = yaml::GetInt32(handover, "hysteresisDb", 0, 30);

        if (yaml::HasField(handover, "xn"))
        {
            auto xn = handover["xn"];

            if (yaml::HasField(xn, "enabled"))
                result->handover.xn.enabled = yaml::GetBool(xn, "enabled");

            if (yaml::HasField(xn, "bindAddress"))
                result->handover.xn.bindAddress = yaml::GetIpAddress(xn, "bindAddress");

            if (yaml::HasField(xn, "bindPort"))
                result->handover.xn.bindPort = static_cast<uint16_t>(yaml::GetInt32(xn, "bindPort", 1, 65535));

            if (yaml::HasField(xn, "requestTimeoutMs"))
                result->handover.xn.requestTimeoutMs =
                    yaml::GetInt32(xn, "requestTimeoutMs", 100, 60 * 1000);

            if (yaml::HasField(xn, "contextTtlMs"))
                result->handover.xn.contextTtlMs =
                    yaml::GetInt32(xn, "contextTtlMs", 500, 5 * 60 * 1000);

            if (yaml::HasField(xn, "fallbackToN2"))
                result->handover.xn.fallbackToN2 = yaml::GetBool(xn, "fallbackToN2");
        }
    }

    if (yaml::HasField(config, "neighborList"))
    {
        std::unordered_set<int> seenNeighborPci{};

        for (const auto &neighborNode : yaml::GetSequence(config, "neighborList"))
        {
            nr::gnb::GnbNeighborConfig neighbor{};
            neighbor.nci = yaml::GetInt64(neighborNode, "nci", 0, 0xFFFFFFFFFll);
            neighbor.idLength = yaml::GetInt32(neighborNode, "idLength", 22, 32);
            neighbor.tac = yaml::GetInt32(neighborNode, "tac", 0, 0xFFFFFF);
            neighbor.ipAddress = yaml::GetIpAddress(neighborNode, "ipAddress");

            if (yaml::HasField(neighborNode, "handoverInterface"))
                neighbor.handoverInterface = ReadHandoverInterface(neighborNode["handoverInterface"]);

            if (yaml::HasField(neighborNode, "xnAddress"))
                neighbor.xnAddress = yaml::GetIpAddress(neighborNode, "xnAddress");

            if (yaml::HasField(neighborNode, "xnPort"))
                neighbor.xnPort = static_cast<uint16_t>(yaml::GetInt32(neighborNode, "xnPort", 1, 65535));

            auto pci = neighbor.getPci();
            if (seenNeighborPci.count(pci) != 0)
                throw std::runtime_error(
                    "neighborList contains duplicate PCI=" + std::to_string(pci));

            seenNeighborPci.insert(pci);

            result->neighborList.push_back(neighbor);
        }
    }

    result->pagingDrx = EPagingDrx::V128;
    result->name = "UERANSIM-gnb-" + std::to_string(result->plmn.mcc) + "-" + std::to_string(result->plmn.mnc) + "-" +
                   std::to_string(result->getGnbId()); // NOTE: Avoid using "/" dir separator character.

    for (auto &amfConfig : yaml::GetSequence(config, "amfConfigs"))
    {
        nr::gnb::GnbAmfConfig c{};
        c.address = yaml::GetIpAddress(amfConfig, "address");
        c.port = static_cast<uint16_t>(yaml::GetInt32(amfConfig, "port", 1024, 65535));
        result->amfConfigs.push_back(c);
    }

    for (auto &nssai : yaml::GetSequence(config, "slices"))
    {
        SingleSlice s{};
        s.sst = yaml::GetInt32(nssai, "sst", 0, 0xFF);
        if (yaml::HasField(nssai, "sd"))
            s.sd = octet3{yaml::GetInt32(nssai, "sd", 0, 0xFFFFFF)};
        result->nssai.slices.push_back(s);
    }

    /* Satellite simulation config */
    if (yaml::HasField(config, "satSim"))
        result->satSim = yaml::GetBool(config, "satSim");

    if (result->satSim && yaml::HasField(config, "satLink"))
    {
        auto sl = config["satLink"];
        if (yaml::HasField(sl, "frequencyHz"))
            result->satLink.frequencyHz =
                yaml::GetDouble(sl, "frequencyHz");
        if (yaml::HasField(sl, "txPowerDbW"))
            result->satLink.txPowerDbW =
                yaml::GetDouble(sl, "txPowerDbW");
        if (yaml::HasField(sl, "txGainDbi"))
            result->satLink.txGainDbi =
                yaml::GetDouble(sl, "txGainDbi");
        if (yaml::HasField(sl, "rxGainDbi"))
            result->satLink.rxGainDbi =
                yaml::GetDouble(sl, "rxGainDbi");
    }

    /* gNB geographic location (lat/lon/alt) */
    if (yaml::HasField(config, "position"))
    {
        auto pos = config["position"];
        result->geoLocation.latitude =
            yaml::GetDouble(pos, "latitude", -90.0, 90.0);
        result->geoLocation.longitude =
            yaml::GetDouble(pos, "longitude", -180.0, 180.0);
        result->geoLocation.altitude =
            yaml::GetDouble(pos, "altitude",
                            std::nullopt, std::nullopt);
    }

    return result;
}

static void ReadOptions(int argc, char **argv)
{
    std::string versionStr = std::string(cons::Tag) + " (gNB " + nr::gnb::GNB_VERSION + ")";
    opt::OptionsDescription desc{cons::Project,
                                 versionStr,
                                 "5G-SA gNB implementation",
                                 cons::Owner,
                                 "nr-gnb",
                                 {"-c <config-file> [option...]"},
                                 {},
                                 true,
                                 false};

    opt::OptionItem itemConfigFile = {'c', "config", "Use specified configuration file for gNB", "config-file"};
    opt::OptionItem itemDisableCmd = {'l', "disable-cmd", "Disable command line functionality for this instance",
                                      std::nullopt};

    desc.items.push_back(itemConfigFile);
    desc.items.push_back(itemDisableCmd);

    opt::OptionsResult opt{argc, argv, desc, false, nullptr};

    if (opt.hasFlag(itemDisableCmd))
        g_options.disableCmd = true;
    g_options.configFile = opt.getOption(itemConfigFile);

    try
    {
        g_refConfig = ReadConfigYaml();
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        exit(1);
    }
}

static void ReceiveCommand(app::CliMessage &msg)
{
    if (msg.value.empty())
    {
        g_cliServer->sendMessage(app::CliMessage::Result(msg.clientAddr, ""));
        return;
    }

    std::vector<std::string> tokens{};

    auto exp = opt::PerformExpansion(msg.value, tokens);
    if (exp != opt::ExpansionResult::SUCCESS)
    {
        g_cliServer->sendMessage(app::CliMessage::Error(msg.clientAddr, "Invalid command: " + msg.value));
        return;
    }

    if (tokens.empty())
    {
        g_cliServer->sendMessage(app::CliMessage::Error(msg.clientAddr, "Empty command"));
        return;
    }

    std::string error{}, output{};
    auto cmd = app::ParseGnbCliCommand(std::move(tokens), error, output);
    if (!error.empty())
    {
        g_cliServer->sendMessage(app::CliMessage::Error(msg.clientAddr, error));
        return;
    }
    if (!output.empty())
    {
        g_cliServer->sendMessage(app::CliMessage::Result(msg.clientAddr, output));
        return;
    }
    if (cmd == nullptr)
    {
        g_cliServer->sendMessage(app::CliMessage::Error(msg.clientAddr, ""));
        return;
    }

    if (g_gnbMap.count(msg.nodeName) == 0)
    {
        g_cliServer->sendMessage(app::CliMessage::Error(msg.clientAddr, "Node not found: " + msg.nodeName));
        return;
    }

    auto *gnb = g_gnbMap[msg.nodeName];
    gnb->pushCommand(std::move(cmd), msg.clientAddr);
}

static void Loop()
{
    if (!g_cliServer)
    {
        ::pause();
        return;
    }

    auto msg = g_cliServer->receiveMessage();
    if (msg.type == app::CliMessage::Type::ECHO)
    {
        g_cliServer->sendMessage(msg);
        return;
    }

    if (msg.type != app::CliMessage::Type::COMMAND)
        return;

    if (msg.value.size() > 0xFFFF)
    {
        g_cliServer->sendMessage(app::CliMessage::Error(msg.clientAddr, "Command is too large"));
        return;
    }

    if (msg.nodeName.size() > 0xFFFF)
    {
        g_cliServer->sendMessage(app::CliMessage::Error(msg.clientAddr, "Node name is too large"));
        return;
    }

    ReceiveCommand(msg);
}

int main(int argc, char **argv)
{
    app::Initialize();
    ReadOptions(argc, argv);

    std::cout << cons::Name << std::endl;

    if (!g_options.disableCmd)
    {
        g_cliServer = new app::CliServer{};
        g_cliRespTask = new app::CliResponseTask(g_cliServer);
    }

    auto *gnb = new nr::gnb::GNodeB(g_refConfig, nullptr, g_cliRespTask);
    g_gnbMap[g_refConfig->name] = gnb;

    if (!g_options.disableCmd)
    {
        app::CreateProcTable(g_gnbMap, g_cliServer->assignedAddress().getPort());
        g_cliRespTask->start();
    }

    gnb->start();

    while (true)
        Loop();
}
