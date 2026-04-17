//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include <sstream>

#include <gnb/types.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>

namespace nr::gnb
{

int getPciFromNci(int64_t nci)
{
    return static_cast<int>(nci & cons::PCI_MASK);
}

static std::string ToString(EGnbRsrpMode mode)
{
    switch (mode)
    {
    case EGnbRsrpMode::Calculated:
        return "Calculated";
    case EGnbRsrpMode::Fixed:
        return "Fixed";
    default:
        return "?";
    }
}

static std::string ToString(ESib19EphemerisMode mode)
{
    switch (mode)
    {
    case ESib19EphemerisMode::PosVel:
        return "pos-vel";
    case ESib19EphemerisMode::Orbital:
        return "orbital";
    }
    return "pos-vel";
}

static std::string ToString(EHandoverInterface intf)
{
    switch (intf)
    {
    case EHandoverInterface::N2:
        return "N2";
    case EHandoverInterface::Xn:
        return "Xn";
    default:
        return "?";
    }
}

Json ToJson(const GnbStatusInfo &v)
{
    return Json::Obj({{"is-ngap-up", v.isNgapUp}});
}

Json ToJson(const GnbConfig &v)
{
    std::string startCondition =
        v.ntn.timeWarp.startCondition == NtnConfig::TimeWarpConfig::EStartCondition::Paused
            ? "paused"
            : "moving";

    auto handoverEvents = Json::Arr({});
    for (const auto &event : v.handover.events)
    {
        Json eventJson = Json::Obj({
            {"event-type", event.eventType},
            {"a2-threshold-dbm", event.a2ThresholdDbm},
            {"a3-offset-db", event.a3OffsetDb},
            {"a5-threshold1-dbm", event.a5Threshold1Dbm},
            {"a5-threshold2-dbm", event.a5Threshold2Dbm},
            {"distance-threshold", event.distanceThreshold},
            {"hysteresis-db", event.hysteresisDb},
            {"hysteresis-m", event.hysteresisM},
            {"ttt-ms", event.tttMs},
            {"ntn-trigger-enabled", event.ntnTriggerEnabled},
            {"timerSec", event.timerSec},
            {"useTimer", event.useTimer},
            {"distance-type", event.distanceType},
            {"target-cell-calculated", event.targetCellCalculated},
        });

        if (event.targetCellId)
            eventJson.put("target-cell-id", *event.targetCellId);

        if (event.referencePosition)
        {
            eventJson.put("reference-position", Json::Obj({
                {"use-current-position", event.referencePosition->useCurrPosition},
                {"latitude", std::to_string(event.referencePosition->latitude)},
                {"longitude", std::to_string(event.referencePosition->longitude)},
                {"altitude", std::to_string(event.referencePosition->altitude)},
            }));
        }
        if (event.referencePositionEcef)
        {
            eventJson.put("reference-position-ecef", Json::Obj({
                {"x", std::to_string(event.referencePositionEcef->x)},
                {"y", std::to_string(event.referencePositionEcef->y)},
                {"z", std::to_string(event.referencePositionEcef->z)},
            }));
        }

        handoverEvents.push(std::move(eventJson));
    }

    auto handoverCandidateProfiles = Json::Arr({});
    for (const auto &profile : v.handover.candidateProfiles)
    {
        auto conditionEntries = Json::Arr({});
        for (const auto &event : profile.conditions)
        {
            Json eventJson = Json::Obj({
                {"event-type", event.eventType},
                {"a2-threshold-dbm", event.a2ThresholdDbm},
                {"a3-offset-db", event.a3OffsetDb},
                {"a5-threshold1-dbm", event.a5Threshold1Dbm},
                {"a5-threshold2-dbm", event.a5Threshold2Dbm},
                {"distance-threshold", event.distanceThreshold},
                {"hysteresis-db", event.hysteresisDb},
                {"hysteresis-m", event.hysteresisM},
                {"ttt-ms", event.tttMs},
                {"ntn-trigger-enabled", event.ntnTriggerEnabled},
                {"timerSec", event.timerSec},
                {"useTimer", event.useTimer},
                {"distance-type", event.distanceType},
                {"target-cell-calculated", event.targetCellCalculated},
            });

            if (event.referencePosition)
            {
                eventJson.put("reference-position", Json::Obj({
                    {"use-current-position", event.referencePosition->useCurrPosition},
                    {"latitude", std::to_string(event.referencePosition->latitude)},
                    {"longitude", std::to_string(event.referencePosition->longitude)},
                    {"altitude", std::to_string(event.referencePosition->altitude)},
                }));
            }
            if (event.referencePositionEcef)
            {
                eventJson.put("reference-position-ecef", Json::Obj({
                    {"x", std::to_string(event.referencePositionEcef->x)},
                    {"y", std::to_string(event.referencePositionEcef->y)},
                    {"z", std::to_string(event.referencePositionEcef->z)},
                }));
            }

            conditionEntries.push(std::move(eventJson));
        }

        Json profileJson = Json::Obj({
            {"candidate-profile-id", profile.candidateProfileId},
            {"target-cell-calculated", profile.targetCellCalculated},
            {"conditions", std::move(conditionEntries)},
        });

        if (profile.targetCellId)
            profileJson.put("target-cell-id", *profile.targetCellId);

        handoverCandidateProfiles.push(std::move(profileJson));
    }

    auto neighborEntries = Json::Arr({});
    for (const auto &neighbor : v.neighborList)
    {
        Json neighborEntry = Json::Obj({
            {"nci", neighbor.nci},
            {"nr-cell-identity", static_cast<int64_t>(neighbor.getNrCellIdentity())},
            {"id-length", neighbor.idLength},
            {"gnb-id", static_cast<int64_t>(neighbor.getGnbId())},
            {"cell-id", neighbor.getCellId()},
            {"tac", neighbor.tac},
            {"ip-address", neighbor.ipAddress},
            {"handover-interface", ToString(neighbor.handoverInterface)},
            {"pci", neighbor.getPci()},
        });

        if (neighbor.xnAddress)
            neighborEntry.put("xn-address", *neighbor.xnAddress);
        if (neighbor.xnPort)
            neighborEntry.put("xn-port", static_cast<int>(*neighbor.xnPort));

        neighborEntries.push(std::move(neighborEntry));
    }

    Json sib19Json = Json::Obj({
        {"sib19-on", v.ntn.sib19.sib19On},
        {"sib19-timing-ms", v.ntn.sib19.sib19TimingMs},
        {"sat-loc-update-threshold-ms", v.ntn.sib19.satLocUpdateThresholdMs},
        {"eph-type", ToString(v.ntn.sib19.ephType)},
        {"k-offset", v.ntn.sib19.kOffset},
        {"ta-common", v.ntn.sib19.taCommon},
        {"ta-common-drift", v.ntn.sib19.taCommonDrift},
    });

    if (v.ntn.sib19.taCommonDriftVariation)
        sib19Json.put("ta-common-drift-variation", *v.ntn.sib19.taCommonDriftVariation);
    if (v.ntn.sib19.ulSyncValidityDuration)
        sib19Json.put("ul-sync-validity-duration", *v.ntn.sib19.ulSyncValidityDuration);
    if (v.ntn.sib19.cellSpecificKoffset)
        sib19Json.put("cell-specific-koffset", *v.ntn.sib19.cellSpecificKoffset);
    if (v.ntn.sib19.polarization)
        sib19Json.put("polarization", *v.ntn.sib19.polarization);
    if (v.ntn.sib19.taDrift)
        sib19Json.put("ta-drift", *v.ntn.sib19.taDrift);

    Json timeWarpJson = Json::Obj({});
    timeWarpJson.put("start-condition", startCondition);
    timeWarpJson.put("tick-scaling", std::to_string(v.ntn.timeWarp.tickScaling));
    if (v.ntn.timeWarp.startEpochMillis)
        timeWarpJson.put("start-epoch-ms", *v.ntn.timeWarp.startEpochMillis);

    return Json::Obj({
        {"name", v.name},
        {"nci", v.nci},
        {"plmn", ToJson(v.plmn)},
        {"tac", v.tac},
        {"nssai", ToJson(v.nssai)},
        {"ngap-ip", v.ngapIp},
        {"gtp-ip", v.gtpIp},
        {"paging-drx", ToJson(v.pagingDrx)},
        {"ignore-sctp-id", v.ignoreStreamIds},
        {"rf-link", Json::Obj({
            {"update-mode", ToString(v.rfLink.updateMode)},
            {"rsrp-db-value", v.rfLink.rsrpDbValue},
            {"carrier-frequency-hz", std::to_string(v.rfLink.carrFrequencyHz)},
            {"tx-power-dbm", std::to_string(v.rfLink.txPowerDbm)},
            {"tx-gain-dbi", std::to_string(v.rfLink.txGainDbi)},
            {"ue-rx-gain-dbi", std::to_string(v.rfLink.ueRxGainDbi)},
        })},
        {"handover", Json::Obj({
            {"cho-enabled", v.handover.choEnabled},
            {"cho-default-profile-id", v.handover.choDefaultProfileId},
            {"events", std::move(handoverEvents)},
            {"cho-candidate-profiles", std::move(handoverCandidateProfiles)},
            {"xn", Json::Obj({
                {"enabled", v.handover.xn.enabled},
                {"bind-address", v.handover.xn.bindAddress},
                {"bind-port", static_cast<int>(v.handover.xn.bindPort)},
                {"request-timeout-ms", v.handover.xn.requestTimeoutMs},
                {"context-ttl-ms", v.handover.xn.contextTtlMs},
                {"fallback-to-n2", v.handover.xn.fallbackToN2},
            })},
        })},
        {"ntn", Json::Obj({
            {"ntn-enabled", v.ntn.ntnEnabled},
            {"own-tle-set", v.ntn.ownTle.has_value()},
            {"time-warp", std::move(timeWarpJson)},
            {"sib19", std::move(sib19Json)},
        })},
        {"neighbor-list", std::move(neighborEntries)},
    });
}

Json ToJson(const NgapAmfContext &v)
{
    auto isIp6 = utils::GetIpVersion(v.address) == 6;
    auto address = isIp6 ? "[" + v.address + "]" : v.address;

    return Json::Obj({
        {"id", v.ctxId},
        {"name", v.amfName},
        {"address", address + ":" + std::to_string(v.port)},
        {"state", ToJson(v.state).str()},
        {"capacity", v.relativeCapacity},
        {"association", ToJson(v.association)},
        {"served-guami", ::ToJson(v.servedGuamiList)},
        {"served-plmn", ::ToJson(v.plmnSupportList)},
    });
}

Json ToJson(const EAmfState &v)
{
    switch (v)
    {
    case EAmfState::NOT_CONNECTED:
        return "NOT_CONNECTED";
    case EAmfState::WAITING_NG_SETUP:
        return "WAITING_NG_SETUP";
    case EAmfState::CONNECTED:
        return "CONNECTED";
    default:
        return "?";
    }
}

Json ToJson(const EPagingDrx &v)
{
    switch (v)
    {
    case EPagingDrx::V32:
        return "v32";
    case EPagingDrx::V64:
        return "v64";
    case EPagingDrx::V128:
        return "v128";
    case EPagingDrx::V256:
        return "v256";
    default:
        return "?";
    }
}

Json ToJson(const SctpAssociation &v)
{
    return Json::Obj({{"id", v.associationId}, {"rx-num", v.inStreams}, {"tx-num", v.outStreams}});
}

Json ToJson(const ServedGuami &v)
{
    return Json::Obj({{"guami", ToJson(v.guami)}, {"backup-amf", v.backupAmfName}});
}

Json ToJson(const Guami &v)
{
    return Json::Obj({
        {"plmn", ToJson(v.plmn)},
        {"region-id", ::ToJson(v.amfRegionId)},
        {"set-id", ::ToJson(v.amfSetId)},
        {"pointer", ::ToJson(v.amfPointer)},
    });
}

} // namespace nr::gnb
