//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <shared_mutex>
#include <algorithm>
#include <cctype>
#include <cmath>

#include <unistd.h>

#include <lib/app/base_app.hpp>
#include <lib/app/cli_base.hpp>
#include <lib/app/cli_cmd.hpp>
#include <lib/app/proc_table.hpp>
#include <lib/app/ue_ctl.hpp>
#include <ue/ue.hpp>
#include <ue/types.hpp>
#include <libsgp4/DateTime.h>
#include <utils/common.hpp>
#include <utils/concurrent_map.hpp>
#include <utils/constants.hpp>
#include <utils/options.hpp>
#include <utils/yaml_utils.hpp>
#include <yaml-cpp/yaml.h>

static app::CliServer *g_cliServer = nullptr;
static nr::ue::UeConfig *g_refConfig = nullptr;
static ConcurrentMap<std::string, nr::ue::UserEquipment *> g_ueMap{};
static app::CliResponseTask *g_cliRespTask = nullptr;


nr::ue::AllCellMeasurements *g_allCellMeasurements{};

static struct Options
{
    std::string configFile{};
    bool noRoutingConfigs{};
    bool disableCmd{};
    std::string imsi{};
    int count{};
    int tempo{};
} g_options{};

static libsgp4::DateTime ParseTleEpochDateTime(const std::string &tleEpoch, const std::string &fieldPath)
{
    if (tleEpoch.size() < 5)
        throw std::runtime_error(fieldPath + " must be in TLE format YYDDD.DDD...");

    if (!std::isdigit(static_cast<unsigned char>(tleEpoch[0])) ||
        !std::isdigit(static_cast<unsigned char>(tleEpoch[1])))
    {
        throw std::runtime_error(fieldPath + " must start with a 2-digit year (YY)");
    }

    int year2 = (tleEpoch[0] - '0') * 10 + (tleEpoch[1] - '0');
    int fullYear = year2 >= 57 ? (1900 + year2) : (2000 + year2);

    double dayOfYear = 0.0;
    try
    {
        dayOfYear = std::stod(tleEpoch.substr(2));
    }
    catch (const std::exception &)
    {
        throw std::runtime_error(fieldPath + " has invalid day-of-year value");
    }

    if (!(dayOfYear >= 1.0 && dayOfYear < 367.0))
        throw std::runtime_error(fieldPath + " day-of-year must be in [1, 367)");

    return libsgp4::DateTime(static_cast<unsigned int>(fullYear), dayOfYear);
}

static int64_t DateTimeToUnixMillis(const libsgp4::DateTime &dateTime)
{
    const libsgp4::DateTime unixEpoch(1970, 1, 1, 0, 0, 0);
    auto delta = dateTime - unixEpoch;
    return static_cast<int64_t>(std::llround(delta.TotalMilliseconds()));
}

static std::optional<std::string> TryGetImsiDigits(const nr::ue::UeConfig &config)
{
    if (config.supi.has_value())
        return config.supi->value;
    return std::nullopt;
}

static std::string ResolveUeNodeNameTemplateToken(const std::string &token, const nr::ue::UeConfig &config)
{
    if (token == "imsi")
    {
        auto imsi = TryGetImsiDigits(config);
        if (!imsi.has_value())
            throw std::runtime_error("nodeNameTemplate token {imsi} requires SUPI/IMSI in UE config");
        return *imsi;
    }

    constexpr const char *kPrefix = "imsi[-";
    constexpr const char *kSuffix = ":]";
    const size_t prefixLen = 6;
    const size_t suffixLen = 2;

    if (token.size() > prefixLen + suffixLen && token.compare(0, prefixLen, kPrefix) == 0 &&
        token.compare(token.size() - suffixLen, suffixLen, kSuffix) == 0)
    {
        auto imsi = TryGetImsiDigits(config);
        if (!imsi.has_value())
            throw std::runtime_error("nodeNameTemplate token {" + token + "} requires SUPI/IMSI in UE config");

        auto countText = token.substr(prefixLen, token.size() - prefixLen - suffixLen);
        int count = 0;
        try
        {
            count = std::stoi(countText);
        }
        catch (const std::exception &)
        {
            throw std::runtime_error("Invalid nodeNameTemplate slice width in token {" + token + "}");
        }

        if (count <= 0)
            throw std::runtime_error("nodeNameTemplate slice width must be positive in token {" + token + "}");

        auto takeCount = static_cast<size_t>(count);
        if (takeCount >= imsi->size())
            return *imsi;

        return imsi->substr(imsi->size() - takeCount);
    }

    throw std::runtime_error("Unsupported token in nodeNameTemplate: {" + token + "}");
}

static std::string RenderUeNodeNameTemplatePreview(const std::string &pattern, const nr::ue::UeConfig &config)
{
    std::string output;
    output.reserve(pattern.size() + 16);

    size_t i = 0;
    while (i < pattern.size())
    {
        if (pattern[i] != '{')
        {
            output.push_back(pattern[i]);
            i++;
            continue;
        }

        size_t close = pattern.find('}', i + 1);
        if (close == std::string::npos)
            throw std::runtime_error("nodeNameTemplate has unmatched '{'");

        auto token = pattern.substr(i + 1, close - i - 1);
        if (token.empty())
            throw std::runtime_error("nodeNameTemplate has empty token: {}");

        output += ResolveUeNodeNameTemplateToken(token, config);
        i = close + 1;
    }

    return output;
}

struct NwUeControllerCmd : NtsMessage
{
    enum PR
    {
        PERFORM_SWITCH_OFF,
    } present;

    // PERFORM_SWITCH_OFF
    nr::ue::UserEquipment *ue{};

    explicit NwUeControllerCmd(PR present) : NtsMessage(NtsMessageType::UE_CTL_COMMAND), present(present)
    {
    }
};

class UeControllerTask : public NtsTask
{
  protected:
    void onStart() override
    {
    }

    void onLoop() override
    {
        auto msg = take();
        if (msg == nullptr)
            return;
        if (msg->msgType == NtsMessageType::UE_CTL_COMMAND)
        {
            auto &w = dynamic_cast<NwUeControllerCmd &>(*msg);
            switch (w.present)
            {
            case NwUeControllerCmd::PERFORM_SWITCH_OFF: {
                std::string key{};
                g_ueMap.invokeForeach([&key, &w](auto &item) {
                    if (item.second == w.ue)
                        key = item.first;
                });

                if (key.empty())
                    return;

                if (g_ueMap.removeAndGetSize(key) == 0)
                    exit(0);

                delete w.ue;
                break;
            }
            }
        }
    }

    void onQuit() override
    {
    }
};

static UeControllerTask *g_controllerTask;

static nr::ue::UeConfig *ReadConfigYaml()
{
    auto *result = new nr::ue::UeConfig();
    auto config = YAML::LoadFile(g_options.configFile);

    result->hplmn.mcc = yaml::GetInt32(config, "mcc", 1, 999);
    yaml::GetString(config, "mcc", 3, 3);
    result->hplmn.mnc = yaml::GetInt32(config, "mnc", 0, 999);
    result->hplmn.isLongMnc = yaml::GetString(config, "mnc", 2, 3).size() == 3;
    if (yaml::HasField(config, "routingIndicator"))
        result->routingIndicator = yaml::GetString(config, "routingIndicator", 1, 4);

    for (auto &gnbSearchItem : yaml::GetSequence(config, "gnbSearchList"))
    {
        result->gnbSearchList.push_back(gnbSearchItem.as<std::string>());
    }

    if (yaml::HasField(config, "rls"))
    {
        auto rls = config["rls"];
        if (yaml::HasField(rls, "LOOP_COUNTER"))
            result->rls.loopCounter = yaml::GetInt32(rls, "LOOP_COUNTER", 1, 60000);
        if (yaml::HasField(rls, "RECEIVE_TIMEOUT"))
            result->rls.receiveTimeout = yaml::GetInt32(rls, "RECEIVE_TIMEOUT", 1, 60000);
        if (yaml::HasField(rls, "TIMER_PERIOD_ACK_SEND"))
            result->rls.timerPeriodAckSend = yaml::GetInt32(rls, "TIMER_PERIOD_ACK_SEND", 1, 60000);
        if (yaml::HasField(rls, "TIMER_PERIOD_ACK_CONTROL"))
            result->rls.timerPeriodAckControl = yaml::GetInt32(rls, "TIMER_PERIOD_ACK_CONTROL", 1, 60000);
    }

    if (yaml::HasField(config, "default-nssai"))
    {
        for (auto &sNssai : yaml::GetSequence(config, "default-nssai"))
        {
            SingleSlice s{};
            s.sst = yaml::GetInt32(sNssai, "sst", 0, 0xFF);
            if (yaml::HasField(sNssai, "sd"))
                s.sd = octet3{yaml::GetInt32(sNssai, "sd", 0, 0xFFFFFF)};
            result->defaultConfiguredNssai.slices.push_back(s);
        }
    }

    if (yaml::HasField(config, "configured-nssai"))
    {
        for (auto &sNssai : yaml::GetSequence(config, "configured-nssai"))
        {
            SingleSlice s{};
            s.sst = yaml::GetInt32(sNssai, "sst", 0, 0xFF);
            if (yaml::HasField(sNssai, "sd"))
                s.sd = octet3{yaml::GetInt32(sNssai, "sd", 0, 0xFFFFFF)};
            result->configuredNssai.slices.push_back(s);
        }
    }

    result->key = OctetString::FromHex(yaml::GetString(config, "key", 32, 32));
    result->opC = OctetString::FromHex(yaml::GetString(config, "op", 32, 32));
    result->amf = OctetString::FromHex(yaml::GetString(config, "amf", 4, 4));

    result->configureRouting = !g_options.noRoutingConfigs;

    // If we have multiple UEs in the same process, then log names should be separated.
    result->prefixLogger = g_options.count > 1;

    if (yaml::HasField(config, "supi"))
        result->supi = Supi::Parse(yaml::GetString(config, "supi"));
    if (yaml::HasField(config, "protectionScheme"))
        result->protectionScheme = yaml::GetInt32(config, "protectionScheme", 0, 255);
    if (yaml::HasField(config, "homeNetworkPublicKeyId"))
        result->homeNetworkPublicKeyId = yaml::GetInt32(config, "homeNetworkPublicKeyId", 0, 255);
    if (yaml::HasField(config, "homeNetworkPublicKey"))        
        result->homeNetworkPublicKey = OctetString::FromHex(yaml::GetString(config, "homeNetworkPublicKey", 64, 64)); 
    if (yaml::HasField(config, "imei"))
        result->imei = yaml::GetString(config, "imei", 15, 15);
    if (yaml::HasField(config, "imeiSv"))
        result->imeiSv = yaml::GetString(config, "imeiSv", 16, 16);
    if (yaml::HasField(config, "tunName"))
        result->tunName = yaml::GetString(config, "tunName", 1, 12);
    if (yaml::HasField(config, "tunNetmask"))
        result->tunNetmask = yaml::GetString(config, "tunNetmask", 9, 15);
    if (yaml::HasField(config, "nodeNameTemplate"))
    {
        result->nodeNameTemplate = yaml::GetString(config, "nodeNameTemplate", cons::MinNodeName, cons::MaxNodeName);
        result->nodeNameTemplatePreview = RenderUeNodeNameTemplatePreview(*result->nodeNameTemplate, *result);
        utils::AssertNodeName(*result->nodeNameTemplatePreview);
    }

    yaml::AssertHasField(config, "integrity");
    yaml::AssertHasField(config, "ciphering");

    result->supportedAlgs.nia1 = yaml::GetBool(config["integrity"], "IA1");
    result->supportedAlgs.nia2 = yaml::GetBool(config["integrity"], "IA2");
    result->supportedAlgs.nia3 = yaml::GetBool(config["integrity"], "IA3");
    result->supportedAlgs.nea1 = yaml::GetBool(config["ciphering"], "EA1");
    result->supportedAlgs.nea2 = yaml::GetBool(config["ciphering"], "EA2");
    result->supportedAlgs.nea3 = yaml::GetBool(config["ciphering"], "EA3");

    std::string opType = yaml::GetString(config, "opType");
    if (opType == "OP")
        result->opType = nr::ue::OpType::OP;
    else if (opType == "OPC")
        result->opType = nr::ue::OpType::OPC;
    else
        throw std::runtime_error("Invalid OP type: " + opType);

    if (yaml::HasField(config, "sessions"))
    {
        for (auto &sess : yaml::GetSequence(config, "sessions"))
        {
            nr::ue::SessionConfig s{};

            if (yaml::HasField(sess, "apn"))
                s.apn = yaml::GetString(sess, "apn");
            if (yaml::HasField(sess, "slice"))
            {
                auto slice = sess["slice"];
                s.sNssai = SingleSlice{};
                s.sNssai->sst = yaml::GetInt32(slice, "sst", 0, 0xFF);
                if (yaml::HasField(slice, "sd"))
                    s.sNssai->sd = octet3{yaml::GetInt32(slice, "sd", 0, 0xFFFFFF)};
            }

            std::string type = yaml::GetString(sess, "type");
            if (type == "IPv4")
                s.type = nas::EPduSessionType::IPV4;
            else if (type == "IPv6")
                s.type = nas::EPduSessionType::IPV6;
            else if (type == "IPv4v6")
                s.type = nas::EPduSessionType::IPV4V6;
            else if (type == "Ethernet")
                s.type = nas::EPduSessionType::ETHERNET;
            else if (type == "Unstructured")
                s.type = nas::EPduSessionType::UNSTRUCTURED;
            else
                throw std::runtime_error("Invalid PDU session type: " + type);

            s.isEmergency = false;

            result->defaultSessions.push_back(s);
        }
    }

    yaml::AssertHasField(config, "integrityMaxRate");
    {
        auto uplink = yaml::GetString(config["integrityMaxRate"], "uplink");
        auto downlink = yaml::GetString(config["integrityMaxRate"], "downlink");
        if (uplink != "full" && uplink != "64kbps")
            throw std::runtime_error("Invalid integrity protection maximum uplink data rate: " + uplink);
        if (downlink != "full" && downlink != "64kbps")
            throw std::runtime_error("Invalid integrity protection maximum downlink data rate: " + downlink);
        result->integrityMaxRate.uplinkFull = uplink == "full";
        result->integrityMaxRate.downlinkFull = downlink == "full";
    }

    yaml::AssertHasField(config, "uacAic");
    {
        result->uacAic.mps = yaml::GetBool(config["uacAic"], "mps");
        result->uacAic.mcs = yaml::GetBool(config["uacAic"], "mcs");
    }

    yaml::AssertHasField(config, "uacAcc");
    {
        result->uacAcc.normalCls = yaml::GetInt32(config["uacAcc"], "normalClass", 0, 9);
        result->uacAcc.cls11 = yaml::GetBool(config["uacAcc"], "class11");
        result->uacAcc.cls12 = yaml::GetBool(config["uacAcc"], "class12");
        result->uacAcc.cls13 = yaml::GetBool(config["uacAcc"], "class13");
        result->uacAcc.cls14 = yaml::GetBool(config["uacAcc"], "class14");
        result->uacAcc.cls15 = yaml::GetBool(config["uacAcc"], "class15");
    }

    // UE initial position
    if (yaml::HasField(config, "position"))
    {
        auto posNode = config["position"];
        GeoPosition geo{};
        geo.latitude  = posNode["latitude"].as<double>(0.0);
        geo.longitude = posNode["longitude"].as<double>(0.0);
        geo.altitude  = posNode["altitude"].as<double>(0.0);
        geo.isValid = true;
        result->initialPosition = geo;
    }

    if (yaml::HasField(config, "ntn"))
    {
        auto ntn = config["ntn"];

        if (yaml::HasField(ntn, "ntnEnabled"))
            result->ntn.ntnEnabled = yaml::GetBool(ntn, "ntnEnabled");

        if (yaml::HasField(ntn, "timewarp"))
            throw std::runtime_error("Field ntn.timewarp is invalid; use ntn.timeWarp");

        if (yaml::HasField(ntn, "timeWarp"))
        {
            auto timeWarp = ntn["timeWarp"];
            if (!timeWarp.IsMap())
                throw std::runtime_error("Field ntn.timeWarp must be a map");

            if (yaml::HasField(timeWarp, "startCondition"))
            {
                auto startCondition = yaml::GetString(timeWarp, "startCondition");
                std::transform(startCondition.begin(), startCondition.end(), startCondition.begin(),
                               [](unsigned char ch) {
                                   return static_cast<char>(std::tolower(ch));
                               });

                if (startCondition == "paused")
                {
                    result->ntn.timeWarp.startCondition =
                        nr::ue::UeConfig::NtnConfig::TimeWarpConfig::EStartCondition::Paused;
                }
                else if (startCondition == "moving")
                {
                    result->ntn.timeWarp.startCondition =
                        nr::ue::UeConfig::NtnConfig::TimeWarpConfig::EStartCondition::Moving;
                }
                else
                {
                    throw std::runtime_error(
                        "Field ntn.timeWarp.startCondition has invalid value, expected 'paused' or 'moving'");
                }
            }

            if (yaml::HasField(timeWarp, "tickScaling"))
            {
                result->ntn.timeWarp.tickScaling = yaml::GetDouble(timeWarp, "tickScaling");
                if (!std::isfinite(result->ntn.timeWarp.tickScaling) || result->ntn.timeWarp.tickScaling <= 0.0)
                {
                    throw std::runtime_error("Field ntn.timeWarp.tickScaling must be a finite value > 0");
                }
            }

            if (yaml::HasField(timeWarp, "startEpoch"))
            {
                auto startEpochText = yaml::GetString(timeWarp, "startEpoch");
                utils::Trim(startEpochText);
                if (startEpochText.empty())
                    throw std::runtime_error("ntn.timeWarp.startEpoch cannot be empty");

                auto startEpochDt = ParseTleEpochDateTime(startEpochText, "ntn.timeWarp.startEpoch");
                result->ntn.timeWarp.startEpochMillis = DateTimeToUnixMillis(startEpochDt);
            }
        }

        if (yaml::HasField(ntn, "tle"))
        {
            auto tle = ntn["tle"];
            if (!tle.IsMap())
                throw std::runtime_error("Field ntn.tle must be a map");
            if (!yaml::HasField(tle, "line1") || !yaml::HasField(tle, "line2"))
                throw std::runtime_error("Fields ntn.tle.line1 and ntn.tle.line2 are required");

            nr::ue::UeConfig::NtnConfig::TleEntry entry{};
            entry.line1 = tle["line1"].as<std::string>();
            entry.line2 = tle["line2"].as<std::string>();

            if (entry.line1.size() < 69 || entry.line2.size() < 69)
                throw std::runtime_error("ntn.tle lines must be at least 69 characters (standard TLE format)");

            result->ntn.tle = entry;
        }

        if (yaml::HasField(ntn, "elevationMinDeg"))
            result->ntn.elevationMinDeg = yaml::GetInt32(ntn, "elevationMinDeg", 0, 90);
    }

    return result;
}

static void ReadOptions(int argc, char **argv)
{
    std::string versionStr = std::string(cons::Tag) + " (UE " + nr::ue::UE_VERSION + ")";
    opt::OptionsDescription desc{
        cons::Project, versionStr, "5G-SA UE implementation", cons::Owner, "nr-ue", {"-c <config-file> [option...]"}, {},
        true,          false};

    opt::OptionItem itemConfigFile = {'c', "config", "Use specified configuration file for UE", "config-file"};
    opt::OptionItem itemImsi = {'i', "imsi", "Use specified IMSI number instead of provided one", "imsi"};
    opt::OptionItem itemCount = {'n', "num-of-UE", "Generate specified number of UEs starting from the given IMSI (Max 512)",
                                 "num"};
    opt::OptionItem itemTempo = {'t', "tempo", "Starting delay in milliseconds for each of the UEs", "tempo"};
    opt::OptionItem itemDisableCmd = {'l', "disable-cmd", "Disable command line functionality for this instance",
                                      std::nullopt};
    opt::OptionItem itemDisableRouting = {'r', "no-routing-config",
                                          "Do not auto configure routing for UE TUN interface", std::nullopt};

    desc.items.push_back(itemConfigFile);
    desc.items.push_back(itemImsi);
    desc.items.push_back(itemCount);
    desc.items.push_back(itemTempo);
    desc.items.push_back(itemDisableCmd);
    desc.items.push_back(itemDisableRouting);

    opt::OptionsResult opt{argc, argv, desc, false, nullptr};

    g_options.configFile = opt.getOption(itemConfigFile);
    g_options.noRoutingConfigs = opt.hasFlag(itemDisableRouting);
    if (opt.hasFlag(itemCount))
    {
        g_options.count = utils::ParseInt(opt.getOption(itemCount));
        if (g_options.count <= 0)
            throw std::runtime_error("Invalid number of UEs");
        if (g_options.count > 512)
            throw std::runtime_error("Number of UEs is too big");
    }
    else
    {
        g_options.count = 1;
    }

    if (opt.hasFlag(itemTempo))
        g_options.tempo = utils::ParseInt(opt.getOption(itemTempo));
    else
        g_options.tempo = 0;

    g_options.imsi = {};
    if (opt.hasFlag(itemImsi))
    {
        g_options.imsi = opt.getOption(itemImsi);
        if (g_options.imsi.length() > 5 && g_options.imsi[0] == 'i' && g_options.imsi[1] == 'm' &&
            g_options.imsi[2] == 's' && g_options.imsi[3] == 'i' && g_options.imsi[4] == '-')
        {
            g_options.imsi = g_options.imsi.substr(5);
        }

        Supi::Parse("imsi-" + g_options.imsi); // validate the string by parsing
    }

    g_options.disableCmd = opt.hasFlag(itemDisableCmd);
}

static std::string LargeSum(std::string a, std::string b)
{
    if (a.length() > b.length())
        std::swap(a, b);

    std::string str;
    size_t n1 = a.length(), n2 = b.length();

    reverse(a.begin(), a.end());
    reverse(b.begin(), b.end());

    int carry = 0;
    for (size_t i = 0; i < n1; i++)
    {
        int sum = ((a[i] - '0') + (b[i] - '0') + carry);
        str.push_back(static_cast<char>((sum % 10) + '0'));
        carry = sum / 10;
    }
    for (size_t i = n1; i < n2; i++)
    {
        int sum = ((b[i] - '0') + carry);
        str.push_back(static_cast<char>((sum % 10) + '0'));
        carry = sum / 10;
    }
    if (carry)
        throw std::runtime_error("UE serial number overflow");
    reverse(str.begin(), str.end());
    return str;
}

static void IncrementNumber(std::string &s, int delta)
{
    s = LargeSum(s, std::to_string(delta));
}

static nr::ue::UeConfig *GetConfigByUe(int ueIndex)
{
    auto *c = new nr::ue::UeConfig();
    c->key = g_refConfig->key.copy();
    c->opC = g_refConfig->opC.copy();
    c->opType = g_refConfig->opType;
    c->amf = g_refConfig->amf.copy();
    c->imei = g_refConfig->imei;
    c->imeiSv = g_refConfig->imeiSv;
    c->supi = g_refConfig->supi;
    c->protectionScheme = g_refConfig->protectionScheme;
    c->homeNetworkPublicKey = g_refConfig->homeNetworkPublicKey.copy();
    c->homeNetworkPublicKeyId = g_refConfig->homeNetworkPublicKeyId;
    c->routingIndicator = g_refConfig->routingIndicator;
    c->tunName = g_refConfig->tunName;
    c->tunNetmask = g_refConfig->tunNetmask;
    c->hplmn = g_refConfig->hplmn;
    c->configuredNssai = g_refConfig->configuredNssai;
    c->defaultConfiguredNssai = g_refConfig->defaultConfiguredNssai;
    c->supportedAlgs = g_refConfig->supportedAlgs;
    c->gnbSearchList = g_refConfig->gnbSearchList;
    c->defaultSessions = g_refConfig->defaultSessions;
    c->configureRouting = g_refConfig->configureRouting;
    c->prefixLogger = g_refConfig->prefixLogger;
    c->integrityMaxRate = g_refConfig->integrityMaxRate;
    c->uacAic = g_refConfig->uacAic;
    c->uacAcc = g_refConfig->uacAcc;
    c->rls = g_refConfig->rls;
    c->initialPosition = g_refConfig->initialPosition;
    c->ntn = g_refConfig->ntn;
    c->nodeNameTemplate = g_refConfig->nodeNameTemplate;
    c->nodeNameTemplatePreview = g_refConfig->nodeNameTemplatePreview;

    if (c->supi.has_value())
        IncrementNumber(c->supi->value, ueIndex);
    if (c->imei.has_value())
        IncrementNumber(*c->imei, ueIndex);
    if (c->imeiSv.has_value())
        IncrementNumber(*c->imeiSv, ueIndex);

    if (c->nodeNameTemplate.has_value())
    {
        c->nodeNameTemplatePreview = RenderUeNodeNameTemplatePreview(*c->nodeNameTemplate, *c);
        utils::AssertNodeName(*c->nodeNameTemplatePreview);
    }

    return c;
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
    auto cmd = app::ParseUeCliCommand(std::move(tokens), error, output);
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

    auto *ue = g_ueMap.getOrDefault(msg.nodeName);
    if (ue == nullptr)
    {
        g_cliServer->sendMessage(app::CliMessage::Error(msg.clientAddr, "Node not found: " + msg.nodeName));
        return;
    }

    ue->pushCommand(std::move(cmd), msg.clientAddr);
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

static class UeController : public app::IUeController
{
  public:
    void performSwitchOff(nr::ue::UserEquipment *ue) override
    {
        auto w = std::make_unique<NwUeControllerCmd>(NwUeControllerCmd::PERFORM_SWITCH_OFF);
        w->ue = ue;
        g_controllerTask->push(std::move(w));
    }
} g_ueController;

int main(int argc, char **argv)
{
    app::Initialize();

    try
    {
        ReadOptions(argc, argv);
        g_refConfig = ReadConfigYaml();
        if (g_options.imsi.length() > 0)
            g_refConfig->supi = Supi::Parse("imsi-" + g_options.imsi);
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    std::cout << cons::Name << std::endl;

    g_controllerTask = new UeControllerTask();
    g_controllerTask->start();

    if (!g_options.disableCmd)
    {
        g_cliServer = new app::CliServer{};
        g_cliRespTask = new app::CliResponseTask(g_cliServer);
    }

    // initialize the global set of all received cell RF measurements
    //   This is disabled
    g_allCellMeasurements = new nr::ue::AllCellMeasurements();
    g_allCellMeasurements->cellMeasurements =
        std::set<nr::ue::CellMeasurement, nr::ue::CompareBySignalStrength>();

    // if (g_refConfig->useHandoverMeasFramework)
    // {
    //     // launch the handover measurement framework server if enabled in config
    //     auto &hoConfig = g_refConfig->handoverServerConfig;
    //     std::cout << "Starting handover measurement server at " << hoConfig.address << ":" << hoConfig.port
    //               << " using transport " << hoConfig.transport << std::endl;
    //     if (hoConfig.transport == "UDP")
    //     {
    //         g_allCellMeasurements->handoverMeasServer = std::make_unique<HandoverMeasurementServerUDP>(hoConfig.address, hoConfig.port, g_allCellMeasurements);
    //     }
    //     else if (hoConfig.transport == "TCP")
    //     {
    //         g_allCellMeasurements->handoverMeasServer = std::make_unique<HandoverMeasurementServerTCP>(hoConfig.address, hoConfig.port, g_allCellMeasurements);
    //     }
    //     else        {
    //         std::cerr << "ERROR: Invalid transport for handover measurement framework server: " << hoConfig.transport << std::endl;
    //         return 1;

    // }

    for (int i = 0; i < g_options.count; i++)
    {
        auto *config = GetConfigByUe(i);
        auto *ue = new nr::ue::UserEquipment(config, &g_ueController, nullptr, g_cliRespTask, g_allCellMeasurements);
        g_ueMap.put(config->getNodeName(), ue);
    }

    if (!g_options.disableCmd)
    {
        app::CreateProcTable(g_ueMap, g_cliServer->assignedAddress().getPort());
        g_cliRespTask->start();
    }

    if (g_options.tempo != 0)
    {
        g_ueMap.invokeForeach([](const auto &ue) {
            ue.second->start();
            std::this_thread::sleep_for(std::chrono::milliseconds(g_options.tempo));
        });
    }
    else
    {
        g_ueMap.invokeForeach([](const auto &ue) { ue.second->start(); });
    }

    while (true)
        Loop();
}
