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
    GNB_RF_DATA = 21,   // added for advanced measurement framework with external measurement sources; 
                        //   not used in legacy RLS-simulated measurements
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
    const uint32_t cellId{};

    explicit RlsMessage(EMessageType msgType, uint64_t sti, uint32_t cellId = 0)
        : msgType(msgType), sti(sti), cellId(cellId)
    {
    }
};

struct RlsHeartBeat : RlsMessage
{
    Vector3 simPos;

    explicit RlsHeartBeat(uint64_t sti, uint32_t cellId = 0) : RlsMessage(EMessageType::HEARTBEAT, sti, cellId)
    {
    }
};

struct RlsHeartBeatAck : RlsMessage
{
    int dbm{};

    explicit RlsHeartBeatAck(uint64_t sti, uint32_t cellId = 0) : RlsMessage(EMessageType::HEARTBEAT_ACK, sti, cellId)
    {
    }
};

struct RlsPduTransmission : RlsMessage
{
    EPduType pduType{};
    uint32_t pduId{};
    uint32_t payload{};
    OctetString pdu{};

    explicit RlsPduTransmission(uint64_t sti, uint32_t cellId = 0)
        : RlsMessage(EMessageType::PDU_TRANSMISSION, sti, cellId)
    {
    }
};

struct RlsPduTransmissionAck : RlsMessage
{
    std::vector<uint32_t> pduIds;

    explicit RlsPduTransmissionAck(uint64_t sti, uint32_t cellId = 0)
        : RlsMessage(EMessageType::PDU_TRANSMISSION_ACK, sti, cellId)
    {
    }
};

struct RlsGnbRfData : RlsMessage
{
    int pci{0};
    OctetString ip{};
    int rsrp{0};
    int rsrq{0};
    int sinr{0};

    explicit RlsGnbRfData(uint64_t sti, uint32_t cellId = 0) : RlsMessage(EMessageType::GNB_RF_DATA, sti, cellId)
    {
    }
};

void EncodeRlsMessage(const RlsMessage &msg, OctetString &stream, bool includeCellId = false);
std::unique_ptr<RlsMessage> DecodeRlsMessage(const OctetView &stream, bool hasCellId = false);

} // namespace rls