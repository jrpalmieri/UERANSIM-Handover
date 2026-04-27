//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "rls_pdu.hpp"

#include <cstring>

#include <utils/constants.hpp>

namespace rls
{

static void AppendDouble(OctetString &stream, double v)
{
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    stream.appendOctet8(bits);
}

static double ReadDouble(const OctetView &stream)
{
    uint64_t bits = stream.read8UL();
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

static void AppendString(OctetString &stream, const std::string &s)
{
    stream.appendOctet4(static_cast<uint32_t>(s.size()));
    for (char c : s)
        stream.appendOctet(static_cast<uint8_t>(c));
}

static std::string ReadString(const OctetView &stream)
{
    uint32_t len = stream.read4UI();
    if (len > 4096)
        return {};
    std::string s;
    s.reserve(len);
    for (uint32_t i = 0; i < len; i++)
        s.push_back(static_cast<char>(stream.readI()));
    return s;
}

void EncodeRlsMessage(const RlsMessage &msg, OctetString &stream)
{
    stream.appendOctet(0x03); // (Just for old RLS compatibility)

    stream.appendOctet(cons::Major);
    stream.appendOctet(cons::Minor);
    stream.appendOctet(cons::Patch);
    stream.appendOctet(static_cast<uint8_t>(msg.msgType));
    stream.appendOctet8(msg.sti);
    stream.appendOctet4(msg.senderId);
    stream.appendOctet4(msg.senderId2);

    if (msg.msgType == EMessageType::HEARTBEAT)
    {
        auto &m = (const RlsHeartBeat &)msg;
        AppendDouble(stream, m.simPos.latitude);
        AppendDouble(stream, m.simPos.longitude);
        AppendDouble(stream, m.simPos.altitude);
    }
    else if (msg.msgType == EMessageType::HEARTBEAT_ACK)
    {
        auto &m = (const RlsHeartBeatAck &)msg;
        stream.appendOctet4(m.dbm);
    }
    else if (msg.msgType == EMessageType::PDU_TRANSMISSION)
    {
        auto &m = (const RlsPduTransmission &)msg;
        stream.appendOctet(static_cast<uint8_t>(m.pduType));
        stream.appendOctet4(m.pduId);
        stream.appendOctet4(m.payload);
        stream.appendOctet4(m.pdu.length());
        stream.append(m.pdu);
    }
    else if (msg.msgType == EMessageType::PDU_TRANSMISSION_ACK)
    {
        auto &m = (const RlsPduTransmissionAck &)msg;
        stream.appendOctet4(static_cast<uint32_t>(m.pduIds.size()));
        for (auto pduId : m.pduIds)
            stream.appendOctet4(pduId);
    }
}

std::unique_ptr<RlsMessage> DecodeRlsMessage(const OctetView &stream)
{
    auto first = stream.readI(); // (Just for old RLS compatibility)
    if (first != 3)
        return nullptr;

    // checks for version compatibility (currently set for 327)
    if (stream.read() != cons::Major)
        return nullptr;
    if (stream.read() != cons::Minor)
        return nullptr;
    if (stream.read() != cons::Patch)
        return nullptr;

    // message type: 4: HEARTBEAT, 5: HEARTBEAT_ACK, 6: PDU_TRANSMISSION, 7: PDU_TRANSMISSION_ACK
    auto msgType = static_cast<EMessageType>(stream.readI());
    // sti: simulation temp identifier (randomly generated)
    uint64_t sti = stream.read8UL();
    uint32_t senderId = stream.read4UI();
    uint32_t senderId2 = stream.read4UI();

    // heartbeat messages contain the simulated position of the UE 
    if (msgType == EMessageType::HEARTBEAT)
    {
        auto res = std::make_unique<RlsHeartBeat>(sti, senderId, senderId2);
        res->simPos.latitude = ReadDouble(stream);
        res->simPos.longitude = ReadDouble(stream);
        res->simPos.altitude = ReadDouble(stream);
        return res;
    }
    // heartbeat ack messages contain the signal strength in dBm
    else if (msgType == EMessageType::HEARTBEAT_ACK)
    {
        auto res = std::make_unique<RlsHeartBeatAck>(sti, senderId, senderId2);
        res->dbm = stream.read4I();
        return res;
    }
    // pdu transmission messages contain the type, id, payload type, payload length and payload data
    //  type = 0: reserved, 1: RRC, 2: DATA
    //  payload code = 0: reserved, 1: RRC Reconfiguration, 2: RRC Reconfiguration Complete, 
    //  3: RRC Setup, 4: RRC Setup Complete, 5: RRC Reject, 6: RRC Resume, 7: RRC Resume Complete, 
    //  8: RRC Release, 9: RRC Release Complete
    else if (msgType == EMessageType::PDU_TRANSMISSION)
    {
        auto res = std::make_unique<RlsPduTransmission>(sti, senderId, senderId2);
        res->pduType = static_cast<EPduType>((uint8_t)stream.read());
        res->pduId = stream.read4UI();
        res->payload = stream.read4UI();

        int pduLength = stream.read4I();
        if (pduLength > 16384)
            return nullptr;
        
        res->pdu = stream.readOctetString(pduLength);
        return res;
    }
    // pdu transmission ack messages contain a vector of acknowledged pdu ids
    else if (msgType == EMessageType::PDU_TRANSMISSION_ACK)
    {
        auto res = std::make_unique<RlsPduTransmissionAck>(sti, senderId, senderId2);
        auto count = stream.read4UI();
        res->pduIds.reserve(count);
        for (uint32_t i = 0; i < count; i++)
            res->pduIds.push_back(stream.read4UI());
        return res;
    }

    return nullptr;
}

} // namespace rls
