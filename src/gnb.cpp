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
#include <chrono>
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

static app::CliServer *g_cliServer = nullptr;
static nr::gnb::GnbConfig *g_refConfig = nullptr;
static std::unordered_map<std::string, nr::gnb::GNodeB *> g_gnbMap{};
static app::CliResponseTask *g_cliRespTask = nullptr;

static struct Options
{
    std::string configFile{};
    bool disableCmd{};
    std::optional<int64_t> timeWarpOffsetMsOverride{};
} g_options{};

static int64_t CurrentWallTimeMillis()
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

static libsgp4::DateTime ParseTleEpochDateTime(const std::string &tleEpoch)
{
    if (tleEpoch.size() < 5)
        throw std::runtime_error("ntn.timeWarp.targetTimeEpoch must be in TLE format YYDDD.DDD...");

    if (!std::isdigit(static_cast<unsigned char>(tleEpoch[0])) ||
        !std::isdigit(static_cast<unsigned char>(tleEpoch[1])))
    {
        throw std::runtime_error("ntn.timeWarp.targetTimeEpoch must start with a 2-digit year (YY)");
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
        throw std::runtime_error("ntn.timeWarp.targetTimeEpoch has invalid day-of-year value");
    }

    if (!(dayOfYear >= 1.0 && dayOfYear < 367.0))
        throw std::runtime_error("ntn.timeWarp.targetTimeEpoch day-of-year must be in [1, 367)");

    return libsgp4::DateTime(static_cast<unsigned int>(fullYear), dayOfYear);
}

static int64_t DateTimeToUnixMillis(const libsgp4::DateTime &dateTime)
{
    const libsgp4::DateTime unixEpoch(1970, 1, 1, 0, 0, 0);
    auto delta = dateTime - unixEpoch;
    return static_cast<int64_t>(std::llround(delta.TotalMilliseconds()));
}

static int64_t ResolveConfiguredTimeWarpOffsetMs(const nr::gnb::GnbConfig &config)
{
    const auto &tw = config.ntn.timeWarp;

    if (tw.offsetMs.has_value() && tw.targetTimeEpoch.has_value())
    {
        throw std::runtime_error(
            "ntn.timeWarp must define only one of offsetMs or targetTimeEpoch");
    }

    if (tw.offsetMs.has_value())
        return *tw.offsetMs;

    if (tw.targetTimeEpoch.has_value())
    {
        const libsgp4::DateTime target = ParseTleEpochDateTime(*tw.targetTimeEpoch);
        const int64_t targetMs = DateTimeToUnixMillis(target);
        return targetMs - CurrentWallTimeMillis();
    }

    return 0;
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

static void ReadTargetCellSelector(
    const YAML::Node &entry,
    nr::gnb::GnbHandoverConfig::GnbChoCandidateProfileConfig &item,
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
    auto value = node.as<std::string>();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    if (value == "A2" || value == "A3" || value == "A5" || value == "D1")
        return value;

    throw std::runtime_error(
        "Field handover.events[].eventType has invalid value, expected A2, A3, A5 or D1");
}

static std::string ReadDistanceType(const YAML::Node &node)
{
    auto value = node.as<std::string>();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "fixed" || value == "nadir")
        return value;

    throw std::runtime_error(
        "Field handover.*.distanceType has invalid value, expected 'fixed' or 'nadir'");
}

static nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig ReadHandoverEvent(
    const YAML::Node &entry,
    const nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig &defaults,
    const std::string &pathForErrors)
{
    if (!entry.IsMap())
        throw std::runtime_error("Each " + pathForErrors + " entry must be a map");

    nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig item = defaults;
    if (!yaml::HasField(entry, "eventType"))
        throw std::runtime_error("Each " + pathForErrors + " entry must define eventType");
    item.eventType = ReadHandoverEventType(entry["eventType"]);

    if (yaml::HasField(entry, "a2ThresholdDbm"))
        item.a2ThresholdDbm = yaml::GetInt32(entry, "a2ThresholdDbm", -156, -31);
    if (yaml::HasField(entry, "a3OffsetDb"))
        item.a3OffsetDb = yaml::GetInt32(entry, "a3OffsetDb", -15, 15);
    if (yaml::HasField(entry, "a5Threshold1Dbm"))
        item.a5Threshold1Dbm = yaml::GetInt32(entry, "a5Threshold1Dbm", -156, -31);
    if (yaml::HasField(entry, "a5Threshold2Dbm"))
        item.a5Threshold2Dbm = yaml::GetInt32(entry, "a5Threshold2Dbm", -156, -31);
    if (yaml::HasField(entry, "distanceThreshold"))
        item.distanceThreshold = yaml::GetInt32(entry, "distanceThreshold", 0, 10000000);
    if (yaml::HasField(entry, "hysteresisDb"))
        item.hysteresisDb = yaml::GetInt32(entry, "hysteresisDb", 0, 30);
    if (yaml::HasField(entry, "hysteresisM"))
        item.hysteresisM = yaml::GetInt32(entry, "hysteresisM", 0, 10000000);
    if (yaml::HasField(entry, "tttMs"))
        item.tttMs = yaml::GetInt32(entry, "tttMs", 0, 60000);
    if (yaml::HasField(entry, "ntnTriggerEnabled"))
        item.ntnTriggerEnabled = yaml::GetBool(entry, "ntnTriggerEnabled");
    if (yaml::HasField(entry, "useTimer"))
        item.timerSec = yaml::GetBool(entry, "useTimer");
    if (yaml::HasField(entry, "timerSec"))
        item.timerSec = yaml::GetInt32(entry, "timerSec", 0, 5400);
    if (yaml::HasField(entry, "distanceType"))
        item.distanceType = ReadDistanceType(entry["distanceType"]);

    if (yaml::HasField(entry, "referencePosition"))
    {
        auto ref = entry["referencePosition"];
        if (!ref.IsMap())
            throw std::runtime_error(pathForErrors + ".referencePosition must be a map");

        nr::gnb::GnbHandoverConfig::GnbHandoverReferencePosition pos{};
        if (yaml::HasField(ref, "useCurrPosition"))
            pos.useCurrPosition = yaml::GetBool(ref, "useCurrPosition");

        if (yaml::HasField(ref, "latitude"))
            pos.latitude = yaml::GetDouble(ref, "latitude", -90.0, 90.0);
        if (yaml::HasField(ref, "longitude"))
            pos.longitude = yaml::GetDouble(ref, "longitude", -180.0, 180.0);
        if (yaml::HasField(ref, "altitude"))
            pos.altitude = yaml::GetDouble(ref, "altitude", -10000.0, 10000000.0);

        item.referencePosition = pos;
        if (!pos.useCurrPosition)
            item.referencePositionEcef = GeoToEcef(GeoPosition{pos.latitude, pos.longitude, pos.altitude});
    }

    if (item.referencePosition.has_value() && !item.referencePositionEcef.has_value())
    {
        const auto &pos = *item.referencePosition;
        item.referencePositionEcef = GeoToEcef(GeoPosition{pos.latitude, pos.longitude, pos.altitude});
    }

    return item;
}

static std::vector<nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig>
ReadLegacyHandoverEvents(const YAML::Node &handoverNode,
    const nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig &defaults)
{
    std::vector<nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig> events{};
    auto eventTypeNode = handoverNode["eventType"];
    if (!eventTypeNode)
        return events;

    if (eventTypeNode.IsSequence())
    {
        for (const auto &entry : eventTypeNode)
        {
            auto item = defaults;
            item.eventType = ReadHandoverEventType(entry);
            events.push_back(item);
        }
    }
    else
    {
        auto item = defaults;
        item.eventType = ReadHandoverEventType(eventTypeNode);
        events.push_back(item);
    }

    return events;
}

static std::vector<nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig>
ReadHandoverEvents(const YAML::Node &handoverNode,
    const nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig &defaults,
    const std::string &basePath)
{
    std::vector<nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig> events{};
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

static std::vector<nr::gnb::GnbHandoverConfig::GnbChoCandidateProfileConfig>
ReadChoCandidateProfiles(const YAML::Node &handoverNode,
    const nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig &defaults,
    const std::string &basePath)
{
    std::vector<nr::gnb::GnbHandoverConfig::GnbChoCandidateProfileConfig> out{};

    YAML::Node profilesNode{};
    std::string profilesPath{};
    if (yaml::HasField(handoverNode, "choCandidateProfiles"))
    {
        profilesNode = handoverNode["choCandidateProfiles"];
        profilesPath = basePath + ".choCandidateProfiles";
    }
    else if (yaml::HasField(handoverNode, "ChoCandidateteProfiles"))
    {
        // Keep compatibility for transitional naming/typo variants.
        profilesNode = handoverNode["ChoCandidateteProfiles"];
        profilesPath = basePath + ".ChoCandidateteProfiles";
    }
    else if (yaml::HasField(handoverNode, "candidateProfiles"))
    {
        // Backward-compatible alias for prior schema.
        profilesNode = handoverNode["candidateProfiles"];
        profilesPath = basePath + ".candidateProfiles";
    }
    else if (yaml::HasField(handoverNode, "choEvents"))
    {
        // Backward-compatible alias for older configs.
        profilesNode = handoverNode["choEvents"];
        profilesPath = basePath + ".choEvents";
    }

    if (!profilesNode)
        return out;

    if (!profilesNode.IsSequence())
        throw std::runtime_error("Field " + profilesPath + " must be a YAML sequence");

    for (const auto &entry : profilesNode)
    {
        if (!entry.IsMap())
            throw std::runtime_error("Each " + profilesPath + "[] entry must be a map");

        nr::gnb::GnbHandoverConfig::GnbChoCandidateProfileConfig profile{};

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

static void ReadXnConfig(const YAML::Node &xn, nr::gnb::GnbHandoverConfig::GnbXnConfig &out)
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
    nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig defaults{};
    if (yaml::HasField(handover, "a2ThresholdDbm"))
        defaults.a2ThresholdDbm = yaml::GetInt32(handover, "a2ThresholdDbm", -156, -31);
    if (yaml::HasField(handover, "a3OffsetDb"))
        defaults.a3OffsetDb = yaml::GetInt32(handover, "a3OffsetDb", -15, 15);
    if (yaml::HasField(handover, "a5Threshold1Dbm"))
        defaults.a5Threshold1Dbm = yaml::GetInt32(handover, "a5Threshold1Dbm", -156, -31);
    if (yaml::HasField(handover, "a5Threshold2Dbm"))
        defaults.a5Threshold2Dbm = yaml::GetInt32(handover, "a5Threshold2Dbm", -156, -31);
    if (yaml::HasField(handover, "hysteresisDb"))
        defaults.hysteresisDb = yaml::GetInt32(handover, "hysteresisDb", 0, 30);
    if (yaml::HasField(handover, "distanceThreshold"))
        defaults.distanceThreshold = yaml::GetInt32(handover, "distanceThreshold", 0, 10000000);
    if (yaml::HasField(handover, "hysteresisM"))
        defaults.hysteresisM = yaml::GetInt32(handover, "hysteresisM", 0, 10000000);
    if (yaml::HasField(handover, "tttMs"))
        defaults.tttMs = yaml::GetInt32(handover, "tttMs", 0, 60000);
    if (yaml::HasField(handover, "ntnTriggerEnabled"))
        defaults.ntnTriggerEnabled = yaml::GetBool(handover, "ntnTriggerEnabled");
    if (yaml::HasField(handover, "timerSec"))
        defaults.timerSec = yaml::GetInt32(handover, "timerSec", 0, 5400);
    if (yaml::HasField(handover, "useTimer"))
        defaults.useTimer = yaml::GetBool(handover, "useTimer");
    if (yaml::HasField(handover, "distanceType"))
        defaults.distanceType = ReadDistanceType(handover["distanceType"]);

    if (yaml::HasField(handover, "choEnabled"))
        out.choEnabled = yaml::GetBool(handover, "choEnabled");

    if (yaml::HasField(handover, "choDefaultProfileId"))
        out.choDefaultProfileId = yaml::GetInt32(handover, "choDefaultProfileId", 0, std::nullopt);

    auto events = ReadHandoverEvents(handover, defaults, basePath);
    if (events.empty() && basePath == "handover")
        events = ReadLegacyHandoverEvents(handover, defaults);
    if (!events.empty())
        out.events = std::move(events);

    auto candidateProfiles = ReadChoCandidateProfiles(handover, defaults, basePath);
    if (!candidateProfiles.empty())
        out.candidateProfiles = std::move(candidateProfiles);
}

static void ResolveEventReferencePosition(
    nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig &event,
    const GeoPosition &geoLocation,
    const std::string &pathForErrors)
{
    if (!event.referencePosition.has_value())
        return;

    auto &ref = *event.referencePosition;
    if (ref.useCurrPosition)
    {
        ref.latitude = geoLocation.latitude;
        ref.longitude = geoLocation.longitude;
        ref.altitude = geoLocation.altitude;
    }

    event.referencePositionEcef = GeoToEcef(GeoPosition{ref.latitude, ref.longitude, ref.altitude});

    if (event.distanceType == "fixed" && !event.referencePositionEcef.has_value())
    {
        throw std::runtime_error(pathForErrors + " requires referencePosition for distanceType=fixed");
    }
}

static void ValidateHandoverTargetCellId(
    bool targetCellCalculated,
    const std::optional<int64_t> &targetCellId,
    const std::vector<nr::gnb::GnbNeighborConfig> &neighbors,
    const std::string &pathForErrors)
{
    if (targetCellCalculated || !targetCellId.has_value())
        return;

    for (const auto &neighbor : neighbors)
    {
        if (neighbor.nci == *targetCellId)
            return;
    }

    throw std::runtime_error(pathForErrors + ".targetCellId must exist in neighborList");
}

static void NormalizeHandoverConfig(
    nr::gnb::GnbHandoverConfig &cfg,
    const GeoPosition &geoLocation,
    const std::vector<nr::gnb::GnbNeighborConfig> &neighbors,
    const std::string &basePath)
{
    for (size_t i = 0; i < cfg.events.size(); i++)
    {
        auto eventPath = basePath + ".events[" + std::to_string(i) + "]";
        ResolveEventReferencePosition(cfg.events[i], geoLocation, eventPath);
        ValidateHandoverTargetCellId(cfg.events[i].targetCellCalculated, cfg.events[i].targetCellId,
                                     neighbors, eventPath);
    }

    for (size_t i = 0; i < cfg.candidateProfiles.size(); i++)
    {
        auto profilePath = basePath + ".choCandidateProfiles[" + std::to_string(i) + "]";
        ValidateHandoverTargetCellId(cfg.candidateProfiles[i].targetCellCalculated,
                                     cfg.candidateProfiles[i].targetCellId, neighbors, profilePath);

        for (size_t j = 0; j < cfg.candidateProfiles[i].conditions.size(); j++)
        {
            auto eventPath = profilePath + ".conditions[" + std::to_string(j) + "]";
            ResolveEventReferencePosition(cfg.candidateProfiles[i].conditions[j], geoLocation, eventPath);
        }
    }
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

            if (yaml::HasField(timeWarp, "offsetMs"))
            {
                result->ntn.timeWarp.offsetMs =
                    yaml::GetInt64(timeWarp, "offsetMs", std::nullopt, std::nullopt);
            }

            if (yaml::HasField(timeWarp, "targetTimeEpoch"))
            {
                auto targetTimeEpoch = yaml::GetString(timeWarp, "targetTimeEpoch");
                utils::Trim(targetTimeEpoch);
                if (targetTimeEpoch.empty())
                    throw std::runtime_error("ntn.timeWarp.targetTimeEpoch cannot be empty");
                result->ntn.timeWarp.targetTimeEpoch = std::move(targetTimeEpoch);
            }
        }

        if (yaml::HasField(ntn, "tle"))
        {
            auto tle = ntn["tle"];
            if (!tle.IsMap())
                throw std::runtime_error("Field ntn.tle must be a map");
            if (!yaml::HasField(tle, "line1") || !yaml::HasField(tle, "line2"))
                throw std::runtime_error("Fields ntn.tle.line1 and ntn.tle.line2 are required");

            nr::gnb::SatTleEntry entry{};
            entry.pci = cons::getPciFromNci(result->nci);
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
                    result->ntn.sib19.ephType =
                        yaml::GetInt32(sib19, "ephType", 0, 1);
                }
            }
        }
    }

    NormalizeHandoverConfig(result->handover, result->geoLocation, result->neighborList, "handover");

    // Consolidated hierarchy: NTN uses the same handover parameter set.
    result->ntn.ntnHandover = result->handover;

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
                                 {"-c <config-file> [option...]",
                                  "-c <config-file> --time-warp-ms <offset-ms> [option...]"},
                                 {},
                                 true,
                                 false};

    opt::OptionItem itemConfigFile = {'c', "config", "Use specified configuration file for gNB", "config-file"};
    opt::OptionItem itemDisableCmd = {'l', "disable-cmd", "Disable command line functionality for this instance",
                                      std::nullopt};
    opt::OptionItem itemTimeWarpMs = {
        'w',
        "time-warp-ms",
        "Override NTN time warp offset in milliseconds (negative shifts time backwards)",
        "offset-ms"
    };

    desc.items.push_back(itemConfigFile);
    desc.items.push_back(itemDisableCmd);
    desc.items.push_back(itemTimeWarpMs);

    opt::OptionsResult opt{argc, argv, desc, false, nullptr};

    if (opt.hasFlag(itemDisableCmd))
        g_options.disableCmd = true;
    g_options.configFile = opt.getOption(itemConfigFile);

    if (opt.hasFlag(itemTimeWarpMs))
    {
        try
        {
            g_options.timeWarpOffsetMsOverride = std::stoll(opt.getOption(itemTimeWarpMs));
        }
        catch (const std::exception &)
        {
            std::cerr << "ERROR: invalid --time-warp-ms value" << std::endl;
            exit(1);
        }
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

    // adjust the base time to align with a desired starting epoch if configured, to enable deterministic time warping for NTN testing

    const int64_t effectiveTimeWarpOffsetMs = g_options.timeWarpOffsetMsOverride.has_value()
                                                  ? *g_options.timeWarpOffsetMsOverride
                                                  : ResolveConfiguredTimeWarpOffsetMs(*g_refConfig);
    utils::SetTimeWarpOffsetMillis(effectiveTimeWarpOffsetMs);

    std::cout << cons::Name << std::endl;
    if (effectiveTimeWarpOffsetMs != 0)
    {
        std::cout << "NTN time warp offset (ms): " << effectiveTimeWarpOffsetMs << std::endl;
    }

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
