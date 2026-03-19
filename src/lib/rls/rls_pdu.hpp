//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <utils/common_types.hpp>
#include <utils/octet_string.hpp>
#include <utils/octet_view.hpp>

namespace rls
{

enum class EMessageType : uint8_t
{
    RESERVED = 0,

    DEPRECATED1 = 1,
    DEPRECATED2 = 2,
    DEPRECATED3 = 3,

    HEARTBEAT = 4,
    HEARTBEAT_ACK = 5,
    PDU_TRANSMISSION = 6,
    PDU_TRANSMISSION_ACK = 7,
    GNB_RF_DATA = 21,   // added for advanced measurement framework
                        //   with external measurement sources;
                        //   not used in legacy RLS-simulated measurements
    SATELLITE_POSITION_UPDATE = 22,  // TLE-based satellite position
};

enum class EPduType : uint8_t
{
    RESERVED = 0,
    RRC,
    DATA
};

struct RlsMessage
{
    const EMessageType msgType;
    const uint64_t sti{};
    const uint32_t senderId{};
    const uint32_t senderId2{};

    explicit RlsMessage(EMessageType msgType, uint64_t sti, uint32_t senderId = 0, uint32_t senderId2 = 0)
        : msgType(msgType), sti(sti), senderId(senderId), senderId2(senderId2)
    {
    }
};

struct RlsHeartBeat : RlsMessage
{
    GeoPosition simPos;

    explicit RlsHeartBeat(uint64_t sti, uint32_t senderId = 0, uint32_t senderId2 = 0)
        : RlsMessage(EMessageType::HEARTBEAT, sti, senderId, senderId2)
    {
    }
};

struct RlsHeartBeatAck : RlsMessage
{
    int dbm{};

    explicit RlsHeartBeatAck(uint64_t sti, uint32_t senderId = 0, uint32_t senderId2 = 0)
        : RlsMessage(EMessageType::HEARTBEAT_ACK, sti, senderId, senderId2)
    {
    }
};

struct RlsPduTransmission : RlsMessage
{
    EPduType pduType{};
    uint32_t pduId{};
    uint32_t payload{};
    OctetString pdu{};

    explicit RlsPduTransmission(uint64_t sti, uint32_t senderId = 0, uint32_t senderId2 = 0)
        : RlsMessage(EMessageType::PDU_TRANSMISSION, sti, senderId, senderId2)
    {
    }
};

struct RlsPduTransmissionAck : RlsMessage
{
    std::vector<uint32_t> pduIds;

    explicit RlsPduTransmissionAck(uint64_t sti, uint32_t senderId = 0, uint32_t senderId2 = 0)
        : RlsMessage(EMessageType::PDU_TRANSMISSION_ACK, sti, senderId, senderId2)
    {
    }
};

struct RlsGnbRfData : RlsMessage
{
    int rsrp{0};

    explicit RlsGnbRfData(uint64_t sti, uint32_t senderId = 0, uint32_t senderId2 = 0)
        : RlsMessage(EMessageType::GNB_RF_DATA, sti, senderId, senderId2)
    {
    }
};

/// Satellite position update conveying a TLE and epoch timestamp
struct RlsSatellitePositionUpdate : RlsMessage
{
    std::string tleLine1{};
    std::string tleLine2{};
    int64_t epochMs{};  // Unix epoch millis of the TLE observation

    explicit RlsSatellitePositionUpdate(
          uint64_t sti, uint32_t senderId = 0, uint32_t senderId2 = 0)
        : RlsMessage(
              EMessageType::SATELLITE_POSITION_UPDATE,
              sti, senderId, senderId2)
    {
    }
};

void EncodeRlsMessage(const RlsMessage &msg, OctetString &stream);
std::unique_ptr<RlsMessage> DecodeRlsMessage(const OctetView &stream);

} // namespace rls