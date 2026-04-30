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
#include <cmath>
#include <optional>
#include <unordered_map>

#include <unistd.h>

#include <gnb/gnb.hpp>
#include <gnb/neighbors.hpp>
#include <lib/app/base_app.hpp>
#include <lib/app/cli_base.hpp>
#include <lib/app/cli_cmd.hpp>
#include <lib/app/proc_table.hpp>
#include <libsgp4/DateTime.h>
#include <utils/constants.hpp>
#include <utils/common.hpp>
#include <utils/io.hpp>
#include <utils/options.hpp>
#include <utils/yaml_utils.hpp>
#include <yaml-cpp/yaml.h>
#include <lib/sat/sat_calc.hpp>

static app::CliServer *g_cliServer = nullptr;
static nr::gnb::GnbConfig *g_refConfig = nullptr;
static std::unordered_map<std::string, nr::gnb::GNodeB *> g_gnbMap{};
static app::CliResponseTask *g_cliRespTask = nullptr;

static struct Options
{
    std::string configFile{};
    bool disableCmd{};
    std::optional<int64_t> nciOverride{};
    std::optional<std::string> nameOverride{};
} g_options{};

static int64_t ParseNciHex(const std::string &s)
{
    std::string hex = s;
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
        hex = hex.substr(2);

    if (hex.empty() || hex.size() > 9)
        throw std::runtime_error("NCI must be a 36-bit hex value (1–9 hex digits, max 0xFFFFFFFFF)");

    for (char c : hex)
    {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            throw std::runtime_error("NCI contains non-hex character: " + std::string(1, c));
    }

    int64_t val = std::stoll(hex, nullptr, 16);
    if (val < 0 || val > 0xFFFFFFFFFll)
        throw std::runtime_error("NCI value exceeds 36-bit maximum (0xFFFFFFFFF)");

    return val;
}

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




static std::string ResolveGnbNodeNameTemplateToken(const std::string &token, const nr::gnb::GnbConfig &config)
{
    if (token == "pci")
        return std::to_string(cons::getPciFromNci(config.nci));

    throw std::runtime_error("Unsupported token in nodeNameTemplate: {" + token + "}");
}

static std::string RenderGnbNodeNameTemplatePreview(const std::string &pattern, const nr::gnb::GnbConfig &config)
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

        output += ResolveGnbNodeNameTemplateToken(token, config);
        i = close + 1;
    }

    return output;
}

static nr::gnb::EGnbRsrpMode ReadRsrpMode(const YAML::Node &node, const std::string &fieldPath)
{
    auto mode = yaml::GetString(node, "updateMode");
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (mode == "calculated")
        return nr::gnb::EGnbRsrpMode::Calculated;
    if (mode == "fixed")
        return nr::gnb::EGnbRsrpMode::Fixed;

    throw std::runtime_error("Field " + fieldPath + " has invalid value, expected 'Calculated' or 'Fixed'");
}

static nr::gnb::ESib19EphemerisMode ReadSib19EphemerisMode(const YAML::Node &node, const std::string &fieldPath)
{
    auto valueNode = node["ephType"];
    if (!valueNode.IsScalar())
        throw std::runtime_error("Field " + fieldPath + " must be a scalar value");

    auto mode = valueNode.as<std::string>();
    utils::Trim(mode);
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (mode == "pos-vel" || mode == "posvel" || mode == "position-velocity" || mode == "0")
        return nr::gnb::ESib19EphemerisMode::PosVel;
    if (mode == "orbital" || mode == "1")
        return nr::gnb::ESib19EphemerisMode::Orbital;
    if (mode == "tle" || mode == "2")
        return nr::gnb::ESib19EphemerisMode::Tle;

    throw std::runtime_error(
        "Field " + fieldPath + " has invalid value, expected 'pos-vel', 'orbital', or 'tle'");
}

static void ReadTargetCellSelector(
    const YAML::Node &entry,
    nr::gnb::GnbChoCandidateProfileConfig &item,
    const std::string &pathForErrors)
{
    if (!yaml::HasField(entry, "targetCellId"))
        return;

    auto node = entry["targetCellId"];
    if (!node.IsScalar())
        throw std::runtime_error(pathForErrors + ".targetCellId must be a scalar value");

    auto textValue = node.as<std::string>();
    auto lowered = textValue;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lowered == "calculated")
    {
        item.targetCellCalculated = true;
        item.targetCellId.reset();
        return;
    }

    item.targetCellCalculated = false;
    item.targetCellId = yaml::GetInt64(entry, "targetCellId", 0, 0xFFFFFFFFFll);
}

static std::string ReadHandoverEventType(const YAML::Node &node)
{
    auto parsed = nr::rrc::common::ParseHandoverEventType(node.as<std::string>());
    if (parsed.has_value())
        return nr::rrc::common::HandoverEventTypeToString(*parsed);

    throw std::runtime_error(
        "Field handover.events[].eventType has invalid value, expected A2, A3, A5, D1, condA3, condD1 or condT1");
}

static nr::rrc::common::E_TTT_ms ReadTtt(const YAML::Node &node)
{
    auto value = node.as<std::string>();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "ms0")
        return nr::rrc::common::E_TTT_ms::ms0;
    if (value == "ms40")
        return nr::rrc::common::E_TTT_ms::ms40;
    if (value == "ms64")
        return nr::rrc::common::E_TTT_ms::ms64;
    if (value == "ms80")
        return nr::rrc::common::E_TTT_ms::ms80;
    if (value == "ms100")
        return nr::rrc::common::E_TTT_ms::ms100;
    if (value == "ms128")
        return nr::rrc::common::E_TTT_ms::ms128;
    if (value == "ms160")
        return nr::rrc::common::E_TTT_ms::ms160;
    if (value == "ms256")
        return nr::rrc::common::E_TTT_ms::ms256;
    if (value == "ms320")
        return nr::rrc::common::E_TTT_ms::ms320;
    if (value == "ms480")
        return nr::rrc::common::E_TTT_ms::ms480;
    if (value == "ms512")
        return nr::rrc::common::E_TTT_ms::ms512;
    if (value == "ms640")
        return nr::rrc::common::E_TTT_ms::ms640;
    if (value == "ms1024")
        return nr::rrc::common::E_TTT_ms::ms1024;
    if (value == "ms1280")
        return nr::rrc::common::E_TTT_ms::ms1280;
    if (value == "ms2560")
        return nr::rrc::common::E_TTT_ms::ms2560;
    if (value == "ms5120")
        return nr::rrc::common::E_TTT_ms::ms5120;

    throw std::runtime_error(
        "Field TTT has invalid value, expected one of: ms0, ms40, ms64, ms80, ms100, ms128, ms160, ms256, ms320, ms480, ms512, ms640, ms1024, ms1280, ms2560, ms5120");
}

static nr::rrc::common::EventReferenceLocation ReadReferencePosition(const YAML::Node &node, const std::string &pathForErrors)
{
    nr::rrc::common::EventReferenceLocation pos{};

    if (yaml::HasField(node, "latitude"))
        pos.latitudeDeg = yaml::GetDouble(node, "latitude", -90.0, 90.0);
    else
        throw std::runtime_error(pathForErrors + " must have latitude field");

    if (yaml::HasField(node, "longitude"))
        pos.longitudeDeg = yaml::GetDouble(node, "longitude", -180.0, 180.0);
    else
        throw std::runtime_error(pathForErrors + " must have longitude field");

    return pos;
}

static nr::rrc::common::ReportConfigEvent ReadHandoverEvent(
    const YAML::Node &entry,
    const nr::rrc::common::ReportConfigEvent &defaults,
    const std::string &pathForErrors)
{
    if (!entry.IsMap())
        throw std::runtime_error("Each " + pathForErrors + " entry must be a map");

    nr::rrc::common::ReportConfigEvent item = defaults;
    if (!yaml::HasField(entry, "eventType"))
        throw std::runtime_error("Each " + pathForErrors + " entry must define eventType");
    item.eventType = ReadHandoverEventType(entry["eventType"]);
    auto parsedEvent = nr::rrc::common::ParseHandoverEventType(item.eventType);
    if (!parsedEvent.has_value())
        throw std::runtime_error(pathForErrors + ".eventType is invalid after normalization");
    item.eventKind = *parsedEvent;

    // A2
    if (yaml::HasField(entry, "a2ThresholdDbm"))
        item.a2_thresholdDbm = yaml::GetInt32(entry, "a2ThresholdDbm", -156, -31);
    if (yaml::HasField(entry, "a2HysteresisDb"))
        item.a2_hysteresisDb = yaml::GetInt32(entry, "a2HysteresisDb", 0, 30);
    
    // A3
    if (yaml::HasField(entry, "a3OffsetDb"))
        item.a3_offsetDb = yaml::GetInt32(entry, "a3OffsetDb", std::nullopt, std::nullopt);
    if (yaml::HasField(entry, "a3HysteresisDb"))
        item.a3_hysteresisDb = yaml::GetInt32(entry, "a3HysteresisDb", 0, 30);
    else if (yaml::HasField(entry, "a3hysteresisDb"))
        item.a3_hysteresisDb = yaml::GetInt32(entry, "a3hysteresisDb", 0, 30);

    // A5
    if (yaml::HasField(entry, "a5Threshold1Dbm"))
        item.a5_threshold1Dbm = yaml::GetInt32(entry, "a5Threshold1Dbm", -156, -31);
    if (yaml::HasField(entry, "a5Threshold2Dbm"))
        item.a5_threshold2Dbm = yaml::GetInt32(entry, "a5Threshold2Dbm", -156, -31);
    if (yaml::HasField(entry, "a5HysteresisDb"))
        item.a5_hysteresisDb = yaml::GetInt32(entry, "a5HysteresisDb", 0, 30);

        // D1
    if (yaml::HasField(entry, "d1DistanceThreshold1"))
        item.d1_distanceThreshFromReference1 = yaml::GetInt32(entry, "d1DistanceThreshold1", 0, 10000000);
    if (yaml::HasField(entry, "d1DistanceThreshold2"))
        item.d1_distanceThreshFromReference2 = yaml::GetInt32(entry, "d1DistanceThreshold2", 0, 10000000);
    if (yaml::HasField(entry, "d1HysteresisLocation"))
        item.d1_hysteresisLocation = yaml::GetInt32(entry, "d1HysteresisLocation", 0, 10000000);
    if (yaml::HasField(entry, "d1ReferenceLocation1"))
    {
        auto ref = entry["d1ReferenceLocation1"];
        if (!ref.IsMap())
            throw std::runtime_error(pathForErrors + ".d1ReferenceLocation1 must be a map");

        item.d1_referenceLocation1 = ReadReferencePosition(ref, pathForErrors + ".d1ReferenceLocation1");
    }
    if (yaml::HasField(entry, "d1ReferenceLocation2"))
    {
        auto ref = entry["d1ReferenceLocation2"];
        if (!ref.IsMap())
            throw std::runtime_error(pathForErrors + ".d1ReferenceLocation2 must be a map");

        item.d1_referenceLocation2 = ReadReferencePosition(ref, pathForErrors + ".d1ReferenceLocation2");
    }
    
    // condT1
    if (yaml::HasField(entry, "thresholdSecTS"))
        item.condT1_thresholdSecTS = yaml::GetInt32(entry, "thresholdSecTS", 0, 5400);
    else if (yaml::HasField(entry, "threhsoldSecTS"))
        item.condT1_thresholdSecTS = yaml::GetInt32(entry, "threhsoldSecTS", 0, 5400);
    if (yaml::HasField(entry, "durationSec"))
        item.condT1_durationSec = yaml::GetInt32(entry, "durationSec", 0, 6000);        


    // condD1
    if (yaml::HasField(entry, "condD1DistanceThreshold1"))
        item.condD1_distanceThreshFromReference1 = yaml::GetInt32(entry, "condD1DistanceThreshold1", 0, 10000000);
    if (yaml::HasField(entry, "condD1DistanceThreshold2"))
        item.condD1_distanceThreshFromReference2 = yaml::GetInt32(entry, "condD1DistanceThreshold2", 0, 10000000);
    if (yaml::HasField(entry, "condD1HysteresisM"))
        item.condD1_hysteresisLocation = yaml::GetInt32(entry, "condD1HysteresisM", 0, 10000000);
    else if (yaml::HasField(entry, "condD1HysteresisLocation"))
        item.condD1_hysteresisLocation = yaml::GetInt32(entry, "condD1HysteresisLocation", 0, 10000000);
    if (yaml::HasField(entry, "condD1ReferenceLocation1"))
    {
        auto ref = entry["condD1ReferenceLocation1"];
        if (!ref.IsMap())
            throw std::runtime_error(pathForErrors + ".condD1ReferenceLocation1 must be a map");

        item.condD1_referenceLocation1 = ReadReferencePosition(ref, pathForErrors + ".condD1ReferenceLocation1");
    }
    if (yaml::HasField(entry, "condD1ReferenceLocation2"))
    {
        auto ref = entry["condD1ReferenceLocation2"];
        if (!ref.IsMap())
            throw std::runtime_error(pathForErrors + ".condD1ReferenceLocation2 must be a map");

        item.condD1_referenceLocation2 = ReadReferencePosition(ref, pathForErrors + ".condD1ReferenceLocation2");
    }
    if (yaml::HasField(entry, "ttt"))
       item.ttt = ReadTtt(entry["ttt"]);
    
    return item;
}


static std::vector<nr::rrc::common::ReportConfigEvent>
ReadHandoverEvents(const YAML::Node &handoverNode,
    const nr::rrc::common::ReportConfigEvent &defaults,
    const std::string &basePath)
{
    std::vector<nr::rrc::common::ReportConfigEvent> events{};
    auto eventsNode = handoverNode["events"];
    if (!eventsNode)
        return events;
        
    if (!eventsNode.IsSequence())
        throw std::runtime_error("Field " + basePath + ".events must be a YAML sequence");

    for (const auto &entry : eventsNode)
    {
        events.push_back(ReadHandoverEvent(entry, defaults, basePath + ".events[]"));
    }

    return events;
}

static std::vector<nr::gnb::GnbChoCandidateProfileConfig>
ReadChoCandidateProfiles(const YAML::Node &handoverNode,
    const nr::rrc::common::ReportConfigEvent &defaults,
    const std::string &basePath)
{
    std::vector<nr::gnb::GnbChoCandidateProfileConfig> out{};

    YAML::Node profilesNode{};
    std::string profilesPath{};
    if (yaml::HasField(handoverNode, "choCandidateProfiles"))
    {
        profilesNode = handoverNode["choCandidateProfiles"];
        profilesPath = basePath + ".choCandidateProfiles";
    }

    if (!profilesNode)
        return out;

    if (!profilesNode.IsSequence())
        throw std::runtime_error("Field " + profilesPath + " must be a YAML sequence");

    for (const auto &entry : profilesNode)
    {
        if (!entry.IsMap())
            throw std::runtime_error("Each " + profilesPath + "[] entry must be a map");

        nr::gnb::GnbChoCandidateProfileConfig profile{};

        if (yaml::HasField(entry, "candidateProfileId"))
            profile.candidateProfileId = yaml::GetInt32(entry, "candidateProfileId", 0, std::nullopt);

        ReadTargetCellSelector(entry, profile, profilesPath + "[]");

        YAML::Node conditionsNode{};
        std::string conditionsPath{};
        if (yaml::HasField(entry, "conditions"))
        {
            conditionsNode = entry["conditions"];
            conditionsPath = profilesPath + "[].conditions";
        }
        else if (yaml::HasField(entry, "events"))
        {
            // Backward-compatible alias for older configs.
            conditionsNode = entry["events"];
            conditionsPath = profilesPath + "[].events";
        }

        if (!conditionsNode)
            throw std::runtime_error("Each " + profilesPath + "[] entry must define a conditions list");
        if (!conditionsNode.IsSequence())
            throw std::runtime_error("Field " + conditionsPath + " must be a YAML sequence");

        for (const auto &conditionEvent : conditionsNode)
        {
            profile.conditions.push_back(
                ReadHandoverEvent(conditionEvent, defaults, conditionsPath + "[]"));
        }

        if (profile.conditions.empty())
            throw std::runtime_error("Each " + profilesPath + "[] entry must define one or more conditions");

        out.push_back(std::move(profile));
    }

    return out;
}

static void ReadXnConfig(const YAML::Node &xn, nr::gnb::GnbXnConfig &out)
{
    if (yaml::HasField(xn, "enabled"))
        out.enabled = yaml::GetBool(xn, "enabled");

    if (yaml::HasField(xn, "bindAddress"))
        out.bindAddress = yaml::GetIpAddress(xn, "bindAddress");

    if (yaml::HasField(xn, "bindPort"))
        out.bindPort = static_cast<uint16_t>(yaml::GetInt32(xn, "bindPort", 1, 65535));

    if (yaml::HasField(xn, "requestTimeoutMs"))
        out.requestTimeoutMs = yaml::GetInt32(xn, "requestTimeoutMs", 100, 60 * 1000);

    if (yaml::HasField(xn, "contextTtlMs"))
        out.contextTtlMs = yaml::GetInt32(xn, "contextTtlMs", 500, 5 * 60 * 1000);

    if (yaml::HasField(xn, "fallbackToN2"))
        out.fallbackToN2 = yaml::GetBool(xn, "fallbackToN2");
}

static void ReadHandoverConfigSection(
    const YAML::Node &handover,
    const std::string &basePath,
    nr::gnb::GnbHandoverConfig &out)
{
    nr::rrc::common::ReportConfigEvent defaults{};

    if (yaml::HasField(handover, "choEnabled"))
        out.choEnabled = yaml::GetBool(handover, "choEnabled");

    if (yaml::HasField(handover, "choDefaultProfileId"))
        out.choDefaultProfileId = yaml::GetInt32(handover, "choDefaultProfileId", 0, std::nullopt);

    auto events = ReadHandoverEvents(handover, defaults, basePath);
    if (!events.empty())
        out.events = std::move(events);

    auto candidateProfiles = ReadChoCandidateProfiles(handover, defaults, basePath);
    if (!candidateProfiles.empty())
        out.candidateProfiles = std::move(candidateProfiles);
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

    if (yaml::HasField(config, "rfLink"))
    {
        auto rfLink = config["rfLink"];
        result->rfLink.updateMode = ReadRsrpMode(rfLink, "rfLink.updateMode");

        if (yaml::HasField(rfLink, "rsrpDbValue"))
            result->rfLink.rsrpDbValue = yaml::GetInt32(rfLink, "rsrpDbValue", cons::MIN_RSRP, cons::MAX_RSRP);

        if (result->rfLink.updateMode == nr::gnb::EGnbRsrpMode::Fixed && !yaml::HasField(rfLink, "rsrpDbValue"))
            throw std::runtime_error("Field rfLink.rsrpDbValue is required when rfLink.updateMode is Fixed");

        if (yaml::HasField(rfLink, "carrFrequencyHz"))
            result->rfLink.carrFrequencyHz = yaml::GetDouble(rfLink, "carrFrequencyHz");
        if (yaml::HasField(rfLink, "txPowerDbm"))
            result->rfLink.txPowerDbm = yaml::GetDouble(rfLink, "txPowerDbm");
        if (yaml::HasField(rfLink, "txGainDbi"))
            result->rfLink.txGainDbi = yaml::GetDouble(rfLink, "txGainDbi");
        if (yaml::HasField(rfLink, "ueRxGainDbi"))
            result->rfLink.ueRxGainDbi = yaml::GetDouble(rfLink, "ueRxGainDbi");
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

    if (yaml::HasField(config, "position"))
    {
        auto pos = config["position"];
        result->geoLocation.isValid = true;
        result->geoLocation.latitude = yaml::GetDouble(pos, "latitude", -90.0, 90.0);
        result->geoLocation.longitude = yaml::GetDouble(pos, "longitude", -180.0, 180.0);
        result->geoLocation.altitude = yaml::GetDouble(pos, "altitude", std::nullopt, std::nullopt);
    }

    if (yaml::HasField(config, "neighborList"))
    {
        auto sequence = yaml::GetSequence(config, "neighborList");
        for (size_t i = 0; i < sequence.size(); i++)
        {
            auto entryPath = "neighborList[" + std::to_string(i) + "]";
            auto neighbor = nr::gnb::ReadNeighborConfig(sequence[i], true, entryPath);
            result->neighborList.push_back(std::move(neighbor));
        }

        nr::gnb::ValidateUniqueNeighborPci(result->neighborList, "neighborList");
    }

    if (yaml::HasField(config, "handover"))
    {
        ReadHandoverConfigSection(config["handover"], "handover", result->handover);
    }

    if (yaml::HasField(config, "xn"))
        ReadXnConfig(config["xn"], result->handover.xn);

    if (yaml::HasField(config, "ntn"))
    {
        auto ntn = config["ntn"];

        if (yaml::HasField(ntn, "ntnEnabled"))
            result->ntn.ntnEnabled = yaml::GetBool(ntn, "ntnEnabled");


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
                        nr::gnb::NtnConfig::TimeWarpConfig::EStartCondition::Paused;
                }
                else if (startCondition == "moving")
                {
                    result->ntn.timeWarp.startCondition =
                        nr::gnb::NtnConfig::TimeWarpConfig::EStartCondition::Moving;
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
                result->ntn.timeWarp.startEpochMillis = nr::sat::DateTimeToUnixMillis(startEpochDt);
                result->ntn.timeWarp.startEpochText = std::move(startEpochText);
            }
        }

        if (yaml::HasField(ntn, "tle"))
        {
            auto tle = ntn["tle"];
            if (!tle.IsMap())
                throw std::runtime_error("Field ntn.tle must be a map");
            if (!yaml::HasField(tle, "line1") || !yaml::HasField(tle, "line2"))
                throw std::runtime_error("Fields ntn.tle.line1 and ntn.tle.line2 are required");

            nr::sat::SatTleEntry entry{};
            entry.pci = cons::getPciFromNci(result->nci);
            if (yaml::HasField(tle, "name"))
                entry.name = tle["name"].as<std::string>();
            entry.line1 = tle["line1"].as<std::string>();
            entry.line2 = tle["line2"].as<std::string>();
            entry.lastUpdatedMs = 0; // 0 = loaded from config, not a CLI upsert

            if (entry.line1.size() < 69 || entry.line2.size() < 69)
                throw std::runtime_error("ntn.tle lines must be at least 69 characters (standard TLE format)");

            result->ntn.ownTle = entry;
        }

        if (yaml::HasField(ntn, "elevationMinDeg"))
            result->ntn.elevationMinDeg = yaml::GetInt32(ntn, "elevationMinDeg", 0, 90);

        if (yaml::HasField(ntn, "sib19"))
        {
            auto sib19 = ntn["sib19"];

            if (yaml::HasField(sib19, "sib19on"))
                result->ntn.sib19.sib19On = yaml::GetBool(sib19, "sib19on");

            if (result->ntn.sib19.sib19On)
            {
                if (yaml::HasField(sib19, "sib19timing"))
                    result->ntn.sib19.sib19TimingMs =
                        yaml::GetInt32(sib19, "sib19timing", 50, std::nullopt);

                if (yaml::HasField(sib19, "satLocUpdateThresholdMs"))
                {
                    result->ntn.sib19.satLocUpdateThresholdMs =
                        yaml::GetInt32(sib19, "satLocUpdateThresholdMs", 1, std::nullopt);
                }

                if (yaml::HasField(sib19, "kOffset"))
                    result->ntn.sib19.kOffset =
                        yaml::GetInt32(sib19, "kOffset", std::nullopt, std::nullopt);

                if (yaml::HasField(sib19, "taCommon"))
                    result->ntn.sib19.taCommon =
                        yaml::GetInt64(sib19, "taCommon", std::nullopt, std::nullopt);

                if (yaml::HasField(sib19, "taCommonDrift"))
                    result->ntn.sib19.taCommonDrift =
                        yaml::GetInt32(sib19, "taCommonDrift", std::nullopt, std::nullopt);

                if (yaml::HasField(sib19, "taCommonDriftVariation"))
                {
                    result->ntn.sib19.taCommonDriftVariation =
                        yaml::GetInt32(sib19, "taCommonDriftVariation", std::nullopt, std::nullopt);
                }

                if (yaml::HasField(sib19, "ulSyncValidityDuration"))
                {
                    result->ntn.sib19.ulSyncValidityDuration =
                        yaml::GetInt32(sib19, "ulSyncValidityDuration", 1, std::nullopt);
                }

                if (yaml::HasField(sib19, "cellSpecificKoffset"))
                {
                    result->ntn.sib19.cellSpecificKoffset =
                        yaml::GetInt32(sib19, "cellSpecificKoffset", std::nullopt, std::nullopt);
                }

                if (yaml::HasField(sib19, "polarization"))
                {
                    result->ntn.sib19.polarization =
                        yaml::GetInt32(sib19, "polarization", 0, 2);
                }

                if (yaml::HasField(sib19, "taDrift"))
                {
                    result->ntn.sib19.taDrift =
                        yaml::GetInt32(sib19, "taDrift", std::nullopt, std::nullopt);
                }

                if (yaml::HasField(sib19, "ephType"))
                {
                    result->ntn.sib19.ephType = ReadSib19EphemerisMode(sib19, "ntn.sib19.ephType");
                }
            }
        }
    }

    // Consolidated hierarchy: NTN uses the same handover parameter set.
    result->ntn.ntnHandover = result->handover;

    result->pagingDrx = EPagingDrx::V128;
    result->name = "UERANSIM-gnb-" + std::to_string(result->plmn.mcc) + "-" + std::to_string(result->plmn.mnc) + "-" +
                   std::to_string(result->getGnbId()); // NOTE: Avoid using "/" dir separator character.

    if (yaml::HasField(config, "nodeNameTemplate"))
    {
        result->nodeNameTemplate = yaml::GetString(config, "nodeNameTemplate", cons::MinNodeName, cons::MaxNodeName);
        result->nodeNameTemplatePreview = RenderGnbNodeNameTemplatePreview(*result->nodeNameTemplate, *result);
        utils::AssertNodeName(*result->nodeNameTemplatePreview);
    }

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
    opt::OptionItem itemNci = {'i', "nci", "Override NCI from config file (36-bit hex, e.g. 0x1A2B3C4D5)", "nci"};
    opt::OptionItem itemName = {'n', "name", "Override gNB name generated by application", "name"};

    desc.items.push_back(itemConfigFile);
    desc.items.push_back(itemDisableCmd);
    desc.items.push_back(itemNci);
    desc.items.push_back(itemName);

    opt::OptionsResult opt{argc, argv, desc, false, nullptr};

    if (opt.hasFlag(itemDisableCmd))
        g_options.disableCmd = true;
    g_options.configFile = opt.getOption(itemConfigFile);

    if (opt.hasFlag(itemNci))
    {
        try
        {
            g_options.nciOverride = ParseNciHex(opt.getOption(itemNci));
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "ERROR: --nci: " << e.what() << std::endl;
            exit(1);
        }
    }

    if (opt.hasFlag(itemName))
    {
        auto nameVal = opt.getOption(itemName);
        try
        {
            utils::AssertNodeName(nameVal);
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "ERROR: --name: " << e.what() << std::endl;
            exit(1);
        }
        g_options.nameOverride = std::move(nameVal);
    }

    try
    {
        g_refConfig = ReadConfigYaml();
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        exit(1);
    }

    if (g_options.nciOverride.has_value())
    {
        g_refConfig->nci = *g_options.nciOverride;

        if (!g_options.nameOverride.has_value())
        {
            if (g_refConfig->nodeNameTemplate.has_value())
            {
                g_refConfig->nodeNameTemplatePreview =
                    RenderGnbNodeNameTemplatePreview(*g_refConfig->nodeNameTemplate, *g_refConfig);
                g_refConfig->name = *g_refConfig->nodeNameTemplatePreview;
            }
            else
            {
                g_refConfig->name = "UERANSIM-gnb-" + std::to_string(g_refConfig->plmn.mcc) + "-" +
                                    std::to_string(g_refConfig->plmn.mnc) + "-" +
                                    std::to_string(g_refConfig->getGnbId());
            }
        }
    }

    if (g_options.nameOverride.has_value())
        g_refConfig->name = *g_options.nameOverride;
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
