//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "cmd_handler.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <libsgp4/DateTime.h>
#include <gnb/app/task.hpp>
#include <gnb/gnb.hpp>
#include <gnb/gtp/task.hpp>
#include <gnb/neighbors.hpp>
#include <gnb/ngap/task.hpp>
#include <gnb/rls/task.hpp>
#include <gnb/rrc/task.hpp>
#include <gnb/sctp/task.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/printer.hpp>
#include <utils/sat_time.hpp>
#include <utils/yaml_utils.hpp>
#include <yaml-cpp/yaml.h>

#define PAUSE_CONFIRM_TIMEOUT 3000
#define PAUSE_POLLING 10

namespace nr::gnb
{

enum class ENeighborsUpdateMode
{
    Replace,
    Add,
    Remove,
};

struct NeighborsUpdateRequest
{
    ENeighborsUpdateMode mode{};
    std::string modeText{};
    std::vector<GnbNeighborConfig> neighbors{};
};

struct NeighborsUpdateResult
{
    int beforeCount{};
    int afterCount{};
    int addedCount{};
    int removedCount{};
    std::vector<std::string> warnings{};
};

struct SatLocPvRequest
{
    int pci{};
    double x{};
    double y{};
    double z{};
    double vx{};
    double vy{};
    double vz{};
    int64_t epochMs{};
};

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

static Json ToJsonSatTimeStatus(const utils::SatTime::Status &status)
{
    Json json = Json::Obj({
        {"sat-time-ms", status.satTimeMillis},
        {"wallclock-ms", status.wallclockMillis},
        {"start-epoch-ms", status.startEpochMillis},
        {"tick-scaling", std::to_string(status.tickScaling)},
        {"paused", status.paused},
    });

    if (status.scheduledPauseWallclockMillis.has_value())
        json.put("pause-at-wallclock-ms", *status.scheduledPauseWallclockMillis);

    return json;
}

static std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static std::string ToString(ENeighborsUpdateMode mode)
{
    switch (mode)
    {
    case ENeighborsUpdateMode::Replace:
        return "replace";
    case ENeighborsUpdateMode::Add:
        return "add";
    case ENeighborsUpdateMode::Remove:
        return "remove";
    default:
        return "?";
    }
}

static std::string EscapeForLog(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value)
    {
        if (c == '\n')
            escaped += "\\n";
        else if (c == '\r')
            escaped += "\\r";
        else
            escaped += c;
    }
    return escaped;
}

static std::string FormatSource(const InetAddress &address)
{
    return "ipV" + std::to_string(address.getIpVersion()) + ":" + std::to_string(address.getPort());
}

static std::string FormatSatTimeAction(const app::SatTimeCliControl &satTime)
{
    switch (satTime.action)
    {
    case app::SatTimeCliControl::EAction::Status:
        return "sat-time";
    case app::SatTimeCliControl::EAction::Pause:
        return "sat-time pause";
    case app::SatTimeCliControl::EAction::Run:
        return "sat-time run";
    case app::SatTimeCliControl::EAction::TickScale:
        return "sat-time tickscale=" + std::to_string(satTime.tickScale);
    case app::SatTimeCliControl::EAction::StartEpoch:
        return "sat-time start-epoch=" + satTime.startEpoch;
    case app::SatTimeCliControl::EAction::PauseAtWallclock:
        return "sat-time pause-at-wallclock=" + std::to_string(satTime.pauseAtWallclock);
    }
    return "sat-time";
}

static std::string ToAuditCommand(const app::GnbCliCommand &cmd)
{
    std::ostringstream stream;
    switch (cmd.present)
    {
    case app::GnbCliCommand::STATUS:
        return "status";
    case app::GnbCliCommand::UI_STATUS:
        return "ui-status";
    case app::GnbCliCommand::INFO:
        return "info";
    case app::GnbCliCommand::AMF_LIST:
        return "amf-list";
    case app::GnbCliCommand::AMF_INFO:
        return "amf-info " + std::to_string(cmd.amfId);
    case app::GnbCliCommand::UE_LIST:
        return "ue-list";
    case app::GnbCliCommand::UE_COUNT:
        return "ue-count";
    case app::GnbCliCommand::UE_RELEASE_REQ:
        return "ue-release " + std::to_string(cmd.ueId);
    case app::GnbCliCommand::LOC_PV:
        stream << "loc-pv x=" << cmd.locX << " y=" << cmd.locY << " z=" << cmd.locZ << " vx=" << cmd.velX
               << " vy=" << cmd.velY << " vz=" << cmd.velZ << " epoch-ms=" << cmd.epochMs;
        return stream.str();
    case app::GnbCliCommand::SAT_LOC_PV:
        return "sat-loc-pv payload=" + cmd.satLocPvJson;
    case app::GnbCliCommand::SAT_TLE:
        return "sat-tle payload=" + cmd.satTleJson;
    case app::GnbCliCommand::SAT_TIME:
        return FormatSatTimeAction(cmd.satTime);
    case app::GnbCliCommand::NEIGHBORS:
        return "neighbors payload=" + cmd.neighborsJson;
    case app::GnbCliCommand::VERSION:
        return "version";
    }

    return "unknown";
}

static NeighborsUpdateRequest ParseNeighborsRequest(const std::string &jsonPayload)
{
    YAML::Node root;
    try
    {
        root = YAML::Load(jsonPayload);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(std::string("Invalid JSON payload: ") + e.what());
    }

    if (!root.IsMap())
        throw std::runtime_error("Payload must be a JSON object.");

    auto modeText = ToLower(yaml::GetString(root, "mode"));
    ENeighborsUpdateMode mode{};
    if (modeText == "replace")
        mode = ENeighborsUpdateMode::Replace;
    else if (modeText == "add")
        mode = ENeighborsUpdateMode::Add;
    else if (modeText == "remove")
        mode = ENeighborsUpdateMode::Remove;
    else
        throw std::runtime_error("Field 'mode' must be one of: replace, add, remove.");

    auto neighborsNode = root["neighbors"];
    if (!neighborsNode || !neighborsNode.IsSequence())
        throw std::runtime_error("Field 'neighbors' must be an array.");

    NeighborsUpdateRequest request{};
    request.mode = mode;
    request.modeText = ToString(mode);

    size_t i = 0;
    for (const auto &neighborNode : neighborsNode)
    {
        bool fullRecordRequired = mode != ENeighborsUpdateMode::Remove;
        auto entryPath = "neighbors[" + std::to_string(i) + "]";
        auto neighbor = ReadNeighborConfig(neighborNode, fullRecordRequired, entryPath);
        request.neighbors.push_back(std::move(neighbor));
        i++;
    }

    ValidateUniqueNeighborPci(request.neighbors, "neighbors");

    return request;
}

static std::vector<SatTleEntry> ParseSatTleRequest(const std::string &jsonPayload)
{
    YAML::Node root;
    try
    {
        root = YAML::Load(jsonPayload);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(std::string("Invalid JSON payload: ") + e.what());
    }

    if (!root.IsMap())
        throw std::runtime_error("Payload must be a JSON object.");

    auto satellitesNode = root["satellites"];
    if (!satellitesNode || !satellitesNode.IsSequence())
        throw std::runtime_error("Field 'satellites' must be an array.");

    std::vector<SatTleEntry> entries;
    entries.reserve(satellitesNode.size());

    size_t i = 0;
    for (const auto &node : satellitesNode)
    {
        auto path = "satellites[" + std::to_string(i) + "]";
        if (!node.IsMap())
            throw std::runtime_error(path + " must be an object.");

        SatTleEntry entry{};
        entry.pci = yaml::GetInt32(node, "pci", 0, 1007);

        if (!node["line1"] || !node["line1"].IsScalar())
            throw std::runtime_error(path + ".line1 is required.");
        if (!node["line2"] || !node["line2"].IsScalar())
            throw std::runtime_error(path + ".line2 is required.");

        entry.line1 = node["line1"].as<std::string>();
        entry.line2 = node["line2"].as<std::string>();

        entries.push_back(std::move(entry));
        ++i;
    }

    return entries;
}

static SatLocPvRequest ParseSatLocPvRequest(const std::string &jsonPayload)
{
    YAML::Node root;
    try
    {
        root = YAML::Load(jsonPayload);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(std::string("Invalid JSON payload: ") + e.what());
    }

    if (!root.IsMap())
        throw std::runtime_error("Payload must be a JSON object.");

    SatLocPvRequest request{};
    request.pci = yaml::GetInt32(root, "pci", 0, 1007);
    request.x = root["x"].as<double>();
    request.y = root["y"].as<double>();
    request.z = root["z"].as<double>();
    request.vx = root["vx"].as<double>();
    request.vy = root["vy"].as<double>();
    request.vz = root["vz"].as<double>();
    request.epochMs = yaml::GetInt64(root, "epochMs", std::nullopt, std::nullopt);
    return request;
}

static std::vector<std::string> CollectStaleTargetWarnings(const GnbConfig &config,
                                                           const std::vector<GnbNeighborConfig> &neighbors)
{
    std::vector<std::string> warnings{};
    auto hasNeighborNci = [&neighbors](int64_t nci) {
        return std::any_of(neighbors.begin(), neighbors.end(), [nci](const GnbNeighborConfig &neighbor) {
            return neighbor.nci == nci;
        });
    };

    auto checkEventList = [&warnings, &hasNeighborNci](
                              const std::vector<GnbHandoverConfig::GnbHandoverEventConfig> &events,
                              const std::string &basePath) {
        for (size_t i = 0; i < events.size(); i++)
        {
            const auto &event = events[i];
            if (event.targetCellCalculated || !event.targetCellId.has_value())
                continue;
            if (hasNeighborNci(*event.targetCellId))
                continue;

            warnings.push_back(basePath + "[" + std::to_string(i) + "] targetCellId=" +
                               std::to_string(*event.targetCellId) + " does not exist in neighbor list");
        }
    };

    checkEventList(config.handover.events, "handover.events");
    for (size_t i = 0; i < config.handover.candidateProfiles.size(); i++)
    {
        const auto &profile = config.handover.candidateProfiles[i];
        if (!profile.targetCellCalculated && profile.targetCellId.has_value() && !hasNeighborNci(*profile.targetCellId))
        {
            warnings.push_back("handover.choCandidateProfiles[" + std::to_string(i) + "] targetCellId=" +
                               std::to_string(*profile.targetCellId) + " does not exist in neighbor list");
        }

        checkEventList(profile.conditions,
                       "handover.choCandidateProfiles[" + std::to_string(i) + "].conditions");
    }

    return warnings;
}

static Json ToJsonNeighborList(const std::vector<GnbNeighborConfig> &neighbors)
{
    Json entries = Json::Arr({});
    for (const auto &neighbor : neighbors)
    {
        Json entry = Json::Obj({
            {"nci", neighbor.nci},
            {"idLength", neighbor.idLength},
            {"tac", neighbor.tac},
            {"ipAddress", neighbor.ipAddress},
            {"handoverInterface", neighbor.handoverInterface == EHandoverInterface::Xn ? "Xn" : "N2"},
            {"pci", neighbor.getPci()},
        });
        if (neighbor.xnAddress.has_value())
            entry.put("xnAddress", *neighbor.xnAddress);
        if (neighbor.xnPort.has_value())
            entry.put("xnPort", static_cast<int>(*neighbor.xnPort));
        entries.push(std::move(entry));
    }
    return entries;
}

static NeighborsUpdateResult ApplyNeighborsUpdate(GnbNeighbors &neighbors,
                                                   const GnbConfig &config,
                                                   const NeighborsUpdateRequest &request)
{
    NeighborsUpdateResult result{};

    // Work on a local candidate list so we can validate before committing.
    auto candidate = neighbors.getAll();
    result.beforeCount = static_cast<int>(candidate.size());

    if (request.mode == ENeighborsUpdateMode::Replace)
    {
        candidate = request.neighbors;
    }
    else if (request.mode == ENeighborsUpdateMode::Add)
    {
        std::unordered_set<int> seenPci{};
        for (const auto &neighbor : candidate)
            seenPci.insert(neighbor.getPci());

        for (const auto &neighbor : request.neighbors)
        {
            int pci = neighbor.getPci();
            if (seenPci.count(pci) != 0)
                throw std::runtime_error("Cannot add duplicate PCI=" + std::to_string(pci));

            seenPci.insert(pci);
            candidate.push_back(neighbor);
        }

        result.addedCount = static_cast<int>(request.neighbors.size());
    }
    else
    {
        std::unordered_set<int> removePci{};
        for (const auto &neighbor : request.neighbors)
            removePci.insert(neighbor.getPci());

        std::vector<GnbNeighborConfig> filtered{};
        filtered.reserve(candidate.size());
        for (const auto &neighbor : candidate)
        {
            if (removePci.count(neighbor.getPci()) == 0)
                filtered.push_back(neighbor);
        }

        result.removedCount = static_cast<int>(candidate.size() - filtered.size());
        candidate = std::move(filtered);
    }

    ValidateUniqueNeighborPci(candidate, "neighborList");

    if (request.mode == ENeighborsUpdateMode::Replace)
    {
        result.addedCount = static_cast<int>(candidate.size());
        result.removedCount = result.beforeCount;
    }

    // Commit the validated candidate list to the runtime neighbor store.
    neighbors.replaceAll(candidate);
    result.afterCount = static_cast<int>(candidate.size());
    result.warnings = CollectStaleTargetWarnings(config, candidate);
    return result;
}

void GnbCmdHandler::sendResult(const InetAddress &address, const std::string &output)
{
    m_base->cliCallbackTask->push(std::make_unique<app::NwCliSendResponse>(address, output, false));
    logResponse(false, output);
}

void GnbCmdHandler::sendError(const InetAddress &address, const std::string &output)
{
    m_base->cliCallbackTask->push(std::make_unique<app::NwCliSendResponse>(address, output, true));
    logResponse(true, output);
}

void GnbCmdHandler::ensureCliLogger()
{
    if (m_cliLogger != nullptr)
        return;

    m_cliLogger = m_base->logBase->makeUniqueLogger(m_base->config->name + "-cli");
}

void GnbCmdHandler::logCommandReceived(const NmGnbCliCommand &msg)
{
    ensureCliLogger();
    m_currentCommand = ToAuditCommand(*msg.cmd);
    m_currentSource = FormatSource(msg.address);
    m_cliLogger->info("CLI command received from %s: %s", m_currentSource.c_str(),
                      EscapeForLog(m_currentCommand).c_str());
}

void GnbCmdHandler::logResponse(bool isError, const std::string &output)
{
    ensureCliLogger();
    auto responseText = output.empty() ? std::string{"<empty>"} : EscapeForLog(output);
    auto responseType = isError ? "error" : "result";

    if (!m_currentCommand.empty())
    {
        m_cliLogger->info("CLI %s to %s for command '%s': %s", responseType, m_currentSource.c_str(),
                          EscapeForLog(m_currentCommand).c_str(), responseText.c_str());
    }
    else
    {
        m_cliLogger->info("CLI %s: %s", responseType, responseText.c_str());
    }
}

void GnbCmdHandler::pauseTasks()
{
    m_base->gtpTask->requestPause();
    m_base->rlsTask->requestPause();
    m_base->ngapTask->requestPause();
    m_base->rrcTask->requestPause();
    m_base->sctpTask->requestPause();
}

void GnbCmdHandler::unpauseTasks()
{
    m_base->gtpTask->requestUnpause();
    m_base->rlsTask->requestUnpause();
    m_base->ngapTask->requestUnpause();
    m_base->rrcTask->requestUnpause();
    m_base->sctpTask->requestUnpause();
}

bool GnbCmdHandler::isAllPaused()
{
    if (!m_base->gtpTask->isPauseConfirmed())
        return false;
    if (!m_base->rlsTask->isPauseConfirmed())
        return false;
    if (!m_base->ngapTask->isPauseConfirmed())
        return false;
    if (!m_base->rrcTask->isPauseConfirmed())
        return false;
    if (!m_base->sctpTask->isPauseConfirmed())
        return false;
    return true;
}

void GnbCmdHandler::handleCmd(NmGnbCliCommand &msg)
{
    logCommandReceived(msg);
    pauseTasks();

    uint64_t currentTime = utils::CurrentTimeMillis();
    uint64_t endTime = currentTime + PAUSE_CONFIRM_TIMEOUT;

    bool isPaused = false;
    while (currentTime < endTime)
    {
        currentTime = utils::CurrentTimeMillis();
        if (isAllPaused())
        {
            isPaused = true;
            break;
        }
        utils::Sleep(PAUSE_POLLING);
    }

    if (!isPaused)
    {
        sendError(msg.address, "gNB is unable process command due to pausing timeout");
    }
    else
    {
        handleCmdImpl(msg);
    }

    unpauseTasks();
    m_currentCommand.clear();
    m_currentSource.clear();
}

void GnbCmdHandler::handleCmdImpl(NmGnbCliCommand &msg)
{
    switch (msg.cmd->present)
    {
    case app::GnbCliCommand::STATUS: {
        sendResult(msg.address, ToJson(m_base->appTask->m_statusInfo).dumpYaml());
        break;
    }
    case app::GnbCliCommand::UI_STATUS: {
        Json json = Json::Obj({
            {"nci", m_base->config->nci},
            {"pci", m_base->config->getCellId()},
            {"rrc-ue-count", static_cast<int>(m_base->rrcTask->m_ueCtx.size())},
            {"ngap-ue-count", static_cast<int>(m_base->ngapTask->m_ueCtx.size())},
            {"ngap-up", m_base->appTask->m_statusInfo.isNgapUp},
        });
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::GnbCliCommand::INFO: {
        sendResult(msg.address, ToJson(*m_base->config).dumpYaml());
        break;
    }
    case app::GnbCliCommand::AMF_LIST: {
        Json json = Json::Arr({});
        for (auto &amf : m_base->ngapTask->m_amfCtx)
            json.push(Json::Obj({{"id", amf.first}}));
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::GnbCliCommand::AMF_INFO: {
        if (m_base->ngapTask->m_amfCtx.count(msg.cmd->amfId) == 0)
            sendError(msg.address, "AMF not found with given ID");
        else
        {
            auto amf = m_base->ngapTask->m_amfCtx[msg.cmd->amfId];
            sendResult(msg.address, ToJson(*amf).dumpYaml());
        }
        break;
    }
    case app::GnbCliCommand::UE_LIST: {
        Json json = Json::Arr({});
        for (auto &ue : m_base->ngapTask->m_ueCtx)
        {
            json.push(Json::Obj({
                {"ue-id", ue.first},
                {"ran-ngap-id", ue.second->ranUeNgapId},
                {"amf-ngap-id", ue.second->amfUeNgapId},
            }));
        }
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::GnbCliCommand::UE_COUNT: {
        sendResult(msg.address, std::to_string(m_base->ngapTask->m_ueCtx.size()));
        break;
    }
    case app::GnbCliCommand::UE_RELEASE_REQ: {
        if (m_base->ngapTask->m_ueCtx.count(msg.cmd->ueId) == 0)
            sendError(msg.address, "UE not found with given ID");
        else
        {
            auto ue = m_base->ngapTask->m_ueCtx[msg.cmd->ueId];
            m_base->ngapTask->sendContextRelease(ue->ctxId, NgapCause::RadioNetwork_unspecified);
            sendResult(msg.address, "Requesting UE context release");
        }
        break;
    }
    case app::GnbCliCommand::LOC_PV: {
        PositionVelocity position{};
        position.isValid = true;
        position.x = msg.cmd->locX;
        position.y = msg.cmd->locY;
        position.z = msg.cmd->locZ;
        position.vx = msg.cmd->velX;
        position.vy = msg.cmd->velY;
        position.vz = msg.cmd->velZ;
        position.epochMs = msg.cmd->epochMs;

        m_base->rrcTask->setTruePositionVelocity(position);
        sendResult(msg.address, "Updated true gNB position/velocity for SIB19 generation");
        break;
    }
    case app::GnbCliCommand::SAT_LOC_PV: {
        SatLocPvRequest request{};
        try
        {
            request = ParseSatLocPvRequest(msg.cmd->satLocPvJson);
        }
        catch (const std::exception &e)
        {
            sendError(msg.address, std::string("sat-loc-pv payload error: ") + e.what());
            break;
        }

        SatellitePositionVelocityEntry entry{};
        entry.pci = request.pci;
        entry.x = request.x;
        entry.y = request.y;
        entry.z = request.z;
        entry.vx = request.vx;
        entry.vy = request.vy;
        entry.vz = request.vz;
        entry.epochMs = request.epochMs;
        entry.lastUpdatedMs = utils::CurrentTimeMillis();

        m_base->rrcTask->upsertSatellitePositionVelocity(entry);

        Json response = Json::Obj({
            {"result", "ok"},
            {"pci", request.pci},
            {"updatedAtMs", static_cast<int64_t>(entry.lastUpdatedMs)},
        });
        sendResult(msg.address, response.dumpYaml());
        break;
    }
    case app::GnbCliCommand::SAT_TLE: {
        std::vector<SatTleEntry> entries;
        try
        {
            entries = ParseSatTleRequest(msg.cmd->satTleJson);
        }
        catch (const std::exception &e)
        {
            sendError(msg.address, std::string("sat-tle payload error: ") + e.what());
            break;
        }

        m_base->rrcTask->upsertSatTles(entries);

        Json response = Json::Obj({
            {"result", "ok"},
            {"upsertedCount", static_cast<int>(entries.size())},
        });
        sendResult(msg.address, response.dumpYaml());
        break;
    }
    case app::GnbCliCommand::SAT_TIME: {
        if (m_base->satTime == nullptr)
        {
            sendError(msg.address, "sat-time is not available");
            break;
        }

        try
        {
            switch (msg.cmd->satTime.action)
            {
            case app::SatTimeCliControl::EAction::Status:
                break;
            case app::SatTimeCliControl::EAction::Pause:
                m_base->satTime->SetPaused(true);
                break;
            case app::SatTimeCliControl::EAction::Run:
                m_base->satTime->SetPaused(false);
                break;
            case app::SatTimeCliControl::EAction::TickScale:
                m_base->satTime->SetTickScaling(msg.cmd->satTime.tickScale);
                break;
            case app::SatTimeCliControl::EAction::StartEpoch: {
                auto dt = ParseTleEpochDateTime(msg.cmd->satTime.startEpoch, "sat-time start-epoch");
                m_base->satTime->SetStartEpochMillis(DateTimeToUnixMillis(dt));
                break;
            }
            case app::SatTimeCliControl::EAction::PauseAtWallclock:
                m_base->satTime->SchedulePauseAtWallclockMillis(msg.cmd->satTime.pauseAtWallclock);
                break;
            }
        }
        catch (const std::exception &e)
        {
            sendError(msg.address, std::string("sat-time command failed: ") + e.what());
            break;
        }

        sendResult(msg.address, ToJsonSatTimeStatus(m_base->satTime->GetStatus()).dumpYaml());
        break;
    }
    case app::GnbCliCommand::NEIGHBORS: {
        NeighborsUpdateRequest request{};
        try
        {
            request = ParseNeighborsRequest(msg.cmd->neighborsJson);
        }
        catch (const std::exception &e)
        {
            sendError(msg.address, std::string("neighbors payload error: ") + e.what());
            break;
        }

        NeighborsUpdateResult result{};
        try
        {
            result = ApplyNeighborsUpdate(*m_base->neighbors, *m_base->config, request);
        }
        catch (const std::exception &e)
        {
            sendError(msg.address, std::string("neighbors update failed: ") + e.what());
            break;
        }

        Json warnings = Json::Arr({});
        for (const auto &warning : result.warnings)
            warnings.push(warning);

        Json response = Json::Obj({
            {"result", "ok"},
            {"mode", request.modeText},
            {"beforeCount", result.beforeCount},
            {"afterCount", result.afterCount},
            {"addedCount", result.addedCount},
            {"removedCount", result.removedCount},
            {"warnings", std::move(warnings)},
            {"neighbors", ToJsonNeighborList(m_base->neighbors->getAll())},
        });
        sendResult(msg.address, response.dumpYaml());
        break;
    }
    case app::GnbCliCommand::VERSION: {
        Json json = Json::Obj({
            {"gnb-version", std::string(GNB_VERSION)},
            {"base-version", std::string(cons::Tag)},
        });
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    }
}

} // namespace nr::gnb