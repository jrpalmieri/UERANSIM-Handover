//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "cmd_handler.hpp"

#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <libsgp4/DateTime.h>
#include <ue/app/task.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <ue/rrc/task.hpp>
#include <ue/tun/task.hpp>
#include <ue/ue.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/printer.hpp>
#include <utils/sat_time.hpp>
#include <yaml-cpp/yaml.h>

#include <shared_mutex>

#define PAUSE_CONFIRM_TIMEOUT 3000
#define PAUSE_POLLING 10

// todo add coverage again to cli
static std::string SignalDescription(int dbm)
{
    if (dbm > -90)
        return "Excellent";
    if (dbm > -105)
        return "Good";
    if (dbm > -120)
        return "Fair";
    return "Poor";
}

namespace nr::ue
{

static std::vector<std::string> ParseGnbIpListPayload(const std::string &jsonPayload)
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

    auto ipsNode = root["ipAddresses"];
    if (!ipsNode || !ipsNode.IsSequence())
        throw std::runtime_error("Field 'ipAddresses' must be an array.");

    if (ipsNode.size() == 0)
        throw std::runtime_error("Field 'ipAddresses' must contain at least one IP address.");

    std::unordered_set<std::string> seen;
    std::vector<std::string> ips;
    ips.reserve(ipsNode.size());

    size_t i = 0;
    for (const auto &ipNode : ipsNode)
    {
        if (!ipNode.IsScalar())
            throw std::runtime_error("ipAddresses[" + std::to_string(i) + "] must be a string.");

        auto ip = ipNode.as<std::string>();
        utils::Trim(ip);
        if (ip.empty())
            throw std::runtime_error("ipAddresses[" + std::to_string(i) + "] cannot be empty.");

        InetAddress parsed(ip, cons::RadioLinkPort);
        if (parsed.getIpVersion() != 4)
            throw std::runtime_error("ipAddresses[" + std::to_string(i) + "] must be an IPv4 address.");

        if (seen.insert(ip).second)
            ips.push_back(std::move(ip));
        i++;
    }

    return ips;
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
    if (status.scheduledRunWallclockMillis.has_value())
        json.put("run-at-wallclock-ms", *status.scheduledRunWallclockMillis);

    return json;
}

static Json ToJsonLocWgs84(const GeoPosition &position)
{
    return Json::Obj({
        {"latitude", std::to_string(position.latitude)},
        {"longitude", std::to_string(position.longitude)},
        {"altitude", std::to_string(position.altitude)},
    });
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
    case app::SatTimeCliControl::EAction::RunAtWallclock:
        return "sat-time run-at-wallclock=" + std::to_string(satTime.runAtWallclock);
    }
    return "sat-time";
}

static std::string ToAuditCommand(const app::UeCliCommand &cmd)
{
    std::ostringstream stream;
    switch (cmd.present)
    {
    case app::UeCliCommand::CONFIG_INFO:
        return "config-info";
    case app::UeCliCommand::STATUS:
        return "status";
    case app::UeCliCommand::UI_STATUS:
        return "ui-status";
    case app::UeCliCommand::SET_LOC_WGS84:
        stream << "set-loc-wgs84 lat=" << cmd.geoLat << " lon=" << cmd.geoLon << " alt=" << cmd.geoAlt;
        return stream.str();
    case app::UeCliCommand::GET_LOC_WGS84:
        return "get-loc-wgs84";
    case app::UeCliCommand::TIMERS:
        return "timers";
    case app::UeCliCommand::PS_ESTABLISH: {
        stream << "ps-establish";
        if (cmd.apn.has_value())
            stream << " apn=" << *cmd.apn;
        if (cmd.sNssai.has_value())
            stream << " s-nssai=" << ToJson(*cmd.sNssai).str();
        stream << " emergency=" << (cmd.isEmergency ? "true" : "false");
        return stream.str();
    }
    case app::UeCliCommand::PS_RELEASE: {
        stream << "ps-release";
        for (int i = 0; i < cmd.psCount; i++)
            stream << " " << static_cast<int>(cmd.psIds[i]);
        return stream.str();
    }
    case app::UeCliCommand::PS_RELEASE_ALL:
        return "ps-release-all";
    case app::UeCliCommand::PS_LIST:
        return "ps-list";
    case app::UeCliCommand::DE_REGISTER:
        return "deregister cause=" + std::to_string(static_cast<int>(cmd.deregCause));
    case app::UeCliCommand::SAT_TIME:
        return FormatSatTimeAction(cmd.satTime);
    case app::UeCliCommand::RLS_STATE:
        return "rls-state";
    case app::UeCliCommand::GNB_IP_ADD:
        return "gnb-ip-add payload=" + cmd.gnbIpJson;
    case app::UeCliCommand::GNB_IP_REMOVE:
        return "gnb-ip-remove payload=" + cmd.gnbIpJson;
    case app::UeCliCommand::GNB_IP_LIST:
        return "gnb-ip-list";
    case app::UeCliCommand::COVERAGE:
        return "coverage";
    case app::UeCliCommand::VERSION:
        return "version";
    }

    return "unknown";
}

void UeCmdHandler::sendResult(const InetAddress &address, const std::string &output)
{
    m_base->cliCallbackTask->push(std::make_unique<app::NwCliSendResponse>(address, output, false));
    logResponse(false, output);
}

void UeCmdHandler::sendError(const InetAddress &address, const std::string &output)
{
    m_base->cliCallbackTask->push(std::make_unique<app::NwCliSendResponse>(address, output, true));
    logResponse(true, output);
}

void UeCmdHandler::ensureCliLogger()
{
    if (m_cliLogger != nullptr)
        return;

    m_cliLogger = m_base->logBase->makeUniqueLogger(m_base->config->getNodeName() + "-cli");
}

void UeCmdHandler::logCommandReceived(const NmUeCliCommand &msg)
{

    // suppress logging of specific command types
    if (msg.cmd && msg.cmd->present == app::UeCliCommand::UI_STATUS)
    {
        m_suppressCurrentCommandLogs = true;
        return;
    }

    ensureCliLogger();
    m_currentCommand = ToAuditCommand(*msg.cmd);
    m_currentSource = FormatSource(msg.address);
    m_cliLogger->info("CLI command received from %s: %s", m_currentSource.c_str(),
                      EscapeForLog(m_currentCommand).c_str());
}

void UeCmdHandler::logResponse(bool isError, const std::string &output)
{

    // suupressed the command from logging, also suppress the response log
    if (m_suppressCurrentCommandLogs)
    {
        m_suppressCurrentCommandLogs = false;
        return;
    }


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

void UeCmdHandler::pauseTasks()
{
    m_base->nasTask->requestPause();
    m_base->rrcTask->requestPause();
    m_base->rlsTask->requestPause();
}

void UeCmdHandler::unpauseTasks()
{
    m_base->nasTask->requestUnpause();
    m_base->rrcTask->requestUnpause();
    m_base->rlsTask->requestUnpause();
}

bool UeCmdHandler::isAllPaused()
{
    if (!m_base->nasTask->isPauseConfirmed())
        return false;
    if (!m_base->rrcTask->isPauseConfirmed())
        return false;
    if (!m_base->rlsTask->isPauseConfirmed())
        return false;
    return true;
}

void UeCmdHandler::handleCmd(NmUeCliCommand &msg)
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
        sendError(msg.address, "UE is unable process command due to pausing timeout");
    }
    else
    {
        handleCmdImpl(msg);
    }

    unpauseTasks();
    m_currentCommand.clear();
    m_currentSource.clear();
}

void UeCmdHandler::handleCmdImpl(NmUeCliCommand &msg)
{
    switch (msg.cmd->present)
    {
    case app::UeCliCommand::STATUS: {
        std::optional<int> currentCellId = std::nullopt;
        std::optional<Plmn> currentPlmn = std::nullopt;
        std::optional<int> currentTac = std::nullopt;

        auto currentCell = m_base->shCtx.currentCell.get();
        if (currentCell.hasValue())
        {
            currentCellId = currentCell.cellId;
            currentPlmn = currentCell.plmn;
            currentTac = currentCell.tac;
        }

        Json json = Json::Obj({
            {"cm-state", ToJson(m_base->nasTask->mm->m_cmState)},
            {"rm-state", ToJson(m_base->nasTask->mm->m_rmState)},
            {"mm-state", ToJson(m_base->nasTask->mm->m_mmSubState)},
            {"5u-state", ToJson(m_base->nasTask->mm->m_storage->uState->get())},
            {"sim-inserted", m_base->nasTask->mm->m_usim->isValid()},
            {"selected-plmn", ::ToJson(m_base->shCtx.selectedPlmn.get())},
            {"current-cell", ::ToJson(currentCellId)},
            {"current-plmn", ::ToJson(currentPlmn)},
            {"current-tac", ::ToJson(currentTac)},
            {"last-tai", ToJson(m_base->nasTask->mm->m_storage->lastVisitedRegisteredTai)},
            {"stored-suci", ToJson(m_base->nasTask->mm->m_storage->storedSuci->get())},
            {"stored-guti", ToJson(m_base->nasTask->mm->m_storage->storedGuti->get())},
            {"has-emergency", ::ToJson(m_base->nasTask->mm->hasEmergency())},
        });
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::UeCliCommand::UI_STATUS: {
        std::optional<int> currentCellId = std::nullopt;
        auto currentCell = m_base->shCtx.currentCell.get();
        if (currentCell.hasValue())
            currentCellId = currentCell.cellId;

        std::string connectedPci = "NONE";
        std::string connectedDbm = "NONE";
        if (currentCellId.has_value())
        {
            connectedPci = std::to_string(*currentCellId);

            std::shared_lock lock(m_base->cellDbMeasMutex);
            auto it = m_base->cellDbMeas.find(*currentCellId);
            if (it != m_base->cellDbMeas.end())
                connectedDbm = std::to_string(it->second);
        }

        Json json = Json::Obj({
            {"rrc-state", ToJson(m_base->rrcTask->m_state)},
            {"nas-state", ToJson(m_base->nasTask->mm->m_mmSubState)},
            {"connected-pci", connectedPci},
            {"connected-dbm", connectedDbm},
        });

        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::UeCliCommand::SET_LOC_WGS84: {
        GeoPosition geo{};
        geo.isValid = true;
        geo.latitude = msg.cmd->geoLat;
        geo.longitude = msg.cmd->geoLon;
        geo.altitude = msg.cmd->geoAlt;

        // update position without changing timestamp
        m_base->UeLocation.latitude = geo.latitude;
        m_base->UeLocation.longitude = geo.longitude;
        m_base->UeLocation.altitude = geo.altitude;
        m_base->UeLocation.isValid = geo.isValid;

        sendResult(msg.address, "Updated UE WGS84 position");
        break;
    }
    case app::UeCliCommand::GET_LOC_WGS84: {
        auto uePosition = m_base->UeLocation;
        if (!uePosition.isValid)
        {
            sendError(msg.address, "UE location is not set");
            break;
        }

        sendResult(msg.address, ToJsonLocWgs84(uePosition).dumpYaml());
        break;
    }
    case app::UeCliCommand::CONFIG_INFO: {
        auto json = Json::Obj({
            {"node-name-active", m_base->config->getNodeName()},
            {"supi", ToJson(m_base->config->supi)},
            {"hplmn", ToJson(m_base->config->hplmn)},
            {"imei", ::ToJson(m_base->config->imei)},
            {"imeisv", ::ToJson(m_base->config->imeiSv)},
            {"ecall-only", ::ToJson(m_base->nasTask->usim->m_isECallOnly)},
            {"uac-aic", Json::Obj({
                            {"mps", m_base->config->uacAic.mps},
                            {"mcs", m_base->config->uacAic.mcs},
                        })},
            {"uac-acc", Json::Obj({
                            {"normal-class", m_base->config->uacAcc.normalCls},
                            {"class-11", m_base->config->uacAcc.cls11},
                            {"class-12", m_base->config->uacAcc.cls12},
                            {"class-13", m_base->config->uacAcc.cls13},
                            {"class-14", m_base->config->uacAcc.cls14},
                            {"class-15", m_base->config->uacAcc.cls15},
                        })},
            {"is-high-priority", m_base->nasTask->mm->isHighPriority()},
        });

        if (m_base->config->nodeNameTemplate.has_value())
            json.put("node-name-template", *m_base->config->nodeNameTemplate);
        if (m_base->config->nodeNameTemplatePreview.has_value())
            json.put("node-name-template-preview", *m_base->config->nodeNameTemplatePreview);

        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::UeCliCommand::TIMERS: {
        sendResult(msg.address, ToJson(m_base->nasTask->timers).dumpYaml());
        break;
    }
    case app::UeCliCommand::DE_REGISTER: {
        m_base->nasTask->mm->deregistrationRequired(msg.cmd->deregCause);

        if (msg.cmd->deregCause != EDeregCause::SWITCH_OFF)
            sendResult(msg.address, "De-registration procedure triggered");
        else
            sendResult(msg.address, "De-registration procedure triggered. UE device will be switched off.");
        break;
    }
    case app::UeCliCommand::SAT_TIME: {
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
            case app::SatTimeCliControl::EAction::RunAtWallclock:
                m_base->satTime->ScheduleRunAtWallclockMillis(msg.cmd->satTime.runAtWallclock);
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
    case app::UeCliCommand::PS_RELEASE: {
        for (int i = 0; i < msg.cmd->psCount; i++)
            m_base->nasTask->sm->sendReleaseRequest(static_cast<int>(msg.cmd->psIds[i]) % 16);
        sendResult(msg.address, "PDU session release procedure(s) triggered");
        break;
    }
    case app::UeCliCommand::PS_RELEASE_ALL: {
        m_base->nasTask->sm->sendReleaseRequestForAll();
        sendResult(msg.address, "PDU session release procedure(s) triggered");
        break;
    }
    case app::UeCliCommand::PS_ESTABLISH: {
        SessionConfig config;
        config.type = nas::EPduSessionType::IPV4;
        config.isEmergency = msg.cmd->isEmergency;
        config.apn = msg.cmd->apn;
        config.sNssai = msg.cmd->sNssai;
        m_base->nasTask->sm->sendEstablishmentRequest(config);
        sendResult(msg.address, "PDU session establishment procedure triggered");
        break;
    }
    case app::UeCliCommand::PS_LIST: {
        Json json = Json::Obj({});
        for (auto *pduSession : m_base->nasTask->sm->m_pduSessions)
        {
            if (pduSession->psi == 0 || pduSession->psState == EPsState::INACTIVE)
                continue;

            auto obj = Json::Obj({
                {"state", ToJson(pduSession->psState)},
                {"session-type", ToJson(pduSession->sessionType)},
                {"apn", ::ToJson(pduSession->apn)},
                {"s-nssai", ToJson(pduSession->sNssai)},
                {"emergency", pduSession->isEmergency},
                {"address", ::ToJson(pduSession->pduAddress)},
                {"ambr", ::ToJson(pduSession->sessionAmbr)},
                {"data-pending", pduSession->uplinkPending},
            });

            json.put("PDU Session" + std::to_string(pduSession->psi), obj);
        }
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::UeCliCommand::RLS_STATE: {
        Json json = Json::Obj({
            {"sti", OctetString::FromOctet8(m_base->rlsTask->m_shCtx->sti).toHexString()},
            {"gnb-search-space", ::ToJson(m_base->config->gnbSearchList)},
        });
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::UeCliCommand::GNB_IP_ADD: {
        std::vector<std::string> ipAddresses;
        try
        {
            ipAddresses = ParseGnbIpListPayload(msg.cmd->gnbIpJson);
        }
        catch (const std::exception &e)
        {
            sendError(msg.address, std::string("gnb-ip-add payload error: ") + e.what());
            break;
        }

        auto addedCount = m_base->rlsTask->m_udpTask->addSearchSpaceIps(ipAddresses);

        for (const auto &ip : ipAddresses)
        {
            bool exists = std::find(m_base->config->gnbSearchList.begin(), m_base->config->gnbSearchList.end(), ip) !=
                          m_base->config->gnbSearchList.end();
            if (!exists)
                m_base->config->gnbSearchList.push_back(ip);
        }

        Json response = Json::Obj({
            {"result", "ok"},
            {"requestedCount", static_cast<int>(ipAddresses.size())},
            {"addedCount", addedCount},
            {"gnb-search-space", ::ToJson(m_base->rlsTask->m_udpTask->listSearchSpaceIps())},
        });
        sendResult(msg.address, response.dumpYaml());
        break;
    }
    case app::UeCliCommand::GNB_IP_REMOVE: {
        std::vector<std::string> ipAddresses;
        try
        {
            ipAddresses = ParseGnbIpListPayload(msg.cmd->gnbIpJson);
        }
        catch (const std::exception &e)
        {
            sendError(msg.address, std::string("gnb-ip-remove payload error: ") + e.what());
            break;
        }

        auto removedCount = m_base->rlsTask->m_udpTask->removeSearchSpaceIps(ipAddresses);

        std::unordered_set<std::string> removeSet(ipAddresses.begin(), ipAddresses.end());
        std::vector<std::string> kept;
        kept.reserve(m_base->config->gnbSearchList.size());
        for (const auto &ip : m_base->config->gnbSearchList)
        {
            if (removeSet.count(ip) == 0)
                kept.push_back(ip);
        }
        m_base->config->gnbSearchList = std::move(kept);

        Json response = Json::Obj({
            {"result", "ok"},
            {"requestedCount", static_cast<int>(ipAddresses.size())},
            {"removedCount", removedCount},
            {"gnb-search-space", ::ToJson(m_base->rlsTask->m_udpTask->listSearchSpaceIps())},
        });
        sendResult(msg.address, response.dumpYaml());
        break;
    }
    case app::UeCliCommand::GNB_IP_LIST: {
        Json response = Json::Obj({
            {"gnb-search-space", ::ToJson(m_base->rlsTask->m_udpTask->listSearchSpaceIps())},
        });
        sendResult(msg.address, response.dumpYaml());
        break;
    }
    case app::UeCliCommand::COVERAGE: {
        Json json = Json::Obj({});

        const auto &cells = m_base->rrcTask->m_cellDesc;
        for (auto &item : cells)
        {
            auto &cell = item.second;

            auto mib = Json{};
            auto sib1 = Json{};

            if (cell.mib.hasMib)
            {
                mib = Json::Obj({
                    {"barred", cell.mib.isBarred},
                    {"intra-freq-reselection",
                     std::string{cell.mib.isIntraFreqReselectAllowed ? "allowed" : "not-allowed"}},
                });
            }
            if (cell.sib1.hasSib1)
            {
                sib1 = Json::Obj({
                    {"nr-cell-id", utils::IntToHex(cell.sib1.nci)},
                    {"plmn", ToJson(cell.sib1.plmn)},
                    {"tac", cell.sib1.tac},
                    {"operator-reserved", cell.sib1.isReserved},
                });
            }

            auto obj = Json::Obj({{"signal", std::to_string(cell.dbm) + " dBm (" + SignalDescription(cell.dbm) + ")"},
                                  {"mib", mib},
                                  {"sib1", sib1}});

            json.put("[" + std::to_string(item.first) + "]", obj);
        }

        if (cells.empty())
            json = "No cell available";

        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::UeCliCommand::VERSION: {
        Json json = Json::Obj({
            {"ue-version", std::string(UE_VERSION)},
            {"base-version", std::string(cons::Tag)},
        });
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    }
}

} // namespace nr::ue
