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
    nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig &item,
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
    if (yaml::HasField(entry, "distanceType"))
        item.distanceType = ReadDistanceType(entry["distanceType"]);

    ReadTargetCellSelector(entry, item, pathForErrors);

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

static std::vector<nr::gnb::GnbHandoverConfig::GnbChoEventConfig>
ReadChoEvents(const YAML::Node &handoverNode,
    const nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig &defaults,
    const std::string &basePath)
{
    std::vector<nr::gnb::GnbHandoverConfig::GnbChoEventConfig> out{};
    auto choEventsNode = handoverNode["choEvents"];
    if (!choEventsNode)
        return out;

    if (!choEventsNode.IsSequence())
        throw std::runtime_error("Field " + basePath + ".choEvents must be a YAML sequence");

    for (const auto &entry : choEventsNode)
    {
        if (!entry.IsMap())
            throw std::runtime_error("Each " + basePath + ".choEvents[] entry must be a map");

        nr::gnb::GnbHandoverConfig::GnbChoEventConfig cho{};

        auto eventsNode = entry["events"];
        if (!eventsNode)
            throw std::runtime_error("Each " + basePath + ".choEvents[] entry must define an events list");
        if (!eventsNode.IsSequence())
            throw std::runtime_error("Field " + basePath + ".choEvents[].events must be a YAML sequence");

        for (const auto &conditionEvent : eventsNode)
        {
            cho.events.push_back(
                ReadHandoverEvent(conditionEvent, defaults, basePath + ".choEvents[].events[]"));
        }

        if (!cho.events.empty())
            out.push_back(std::move(cho));
    }

    return out;
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
    if (yaml::HasField(handover, "distanceType"))
        defaults.distanceType = ReadDistanceType(handover["distanceType"]);

    if (yaml::HasField(handover, "choEnabled"))
        out.choEnabled = yaml::GetBool(handover, "choEnabled");

    auto events = ReadHandoverEvents(handover, defaults, basePath);
    if (events.empty() && basePath == "handover")
        events = ReadLegacyHandoverEvents(handover, defaults);
    if (!events.empty())
        out.events = std::move(events);

    auto choEvents = ReadChoEvents(handover, defaults, basePath);
    if (!choEvents.empty())
        out.choEvents = std::move(choEvents);
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
    const nr::gnb::GnbHandoverConfig::GnbHandoverEventConfig &event,
    const std::vector<nr::gnb::GnbNeighborConfig> &neighbors,
    const std::string &pathForErrors)
{
    if (event.targetCellCalculated || !event.targetCellId.has_value())
        return;

    for (const auto &neighbor : neighbors)
    {
        if (neighbor.nci == *event.targetCellId)
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
        ValidateHandoverTargetCellId(cfg.events[i], neighbors, eventPath);
    }

    for (size_t i = 0; i < cfg.choEvents.size(); i++)
    {
        for (size_t j = 0; j < cfg.choEvents[i].events.size(); j++)
        {
            auto eventPath = basePath + ".choEvents[" + std::to_string(i) + "].events[" + std::to_string(j) + "]";
            ResolveEventReferencePosition(cfg.choEvents[i].events[j], geoLocation, eventPath);
            ValidateHandoverTargetCellId(cfg.choEvents[i].events[j], neighbors, eventPath);
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
                throw std::runtime_error("neighborList contains duplicate PCI=" + std::to_string(pci));

            seenNeighborPci.insert(pci);
            result->neighborList.push_back(neighbor);
        }
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

        if (yaml::HasField(ntn, "ntnHandover"))
            ReadHandoverConfigSection(ntn["ntnHandover"], "ntn.ntnHandover", result->ntn.ntnHandover);

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
            }
        }
    }

    NormalizeHandoverConfig(result->handover, result->geoLocation, result->neighborList, "handover");
    NormalizeHandoverConfig(result->ntn.ntnHandover, result->geoLocation, result->neighborList,
                            "ntn.ntnHandover");

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
