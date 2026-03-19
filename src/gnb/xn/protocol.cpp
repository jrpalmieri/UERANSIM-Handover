//
// Simulator-native Xn protocol codec for gNB-to-gNB handover signaling.
//

#include "protocol.hpp"

namespace nr::gnb::xn
{

static constexpr uint8_t kProtocolVersion = 1;
static constexpr int kEncodedMessageLength = 24;

static constexpr int kVersionOffset = 0;
static constexpr int kTypeOffset = 1;
static constexpr int kTxnOffset = 4;
static constexpr int kUeIdOffset = 8;
static constexpr int kSourcePciOffset = 12;
static constexpr int kTargetPciOffset = 14;
static constexpr int kCrntiOffset = 16;
static constexpr int kT304Offset = 18;
static constexpr int kCauseOffset = 20;

OctetString EncodeXnMessage(const XnMessage &message)
{
    OctetString packet{};
    packet.appendOctet(kProtocolVersion);
    packet.appendOctet(static_cast<uint8_t>(message.type));
    packet.appendOctet2(0); // reserved
    packet.appendOctet4(message.transactionId);
    packet.appendOctet4(message.ueId);
    packet.appendOctet2(message.sourcePci);
    packet.appendOctet2(message.targetPci);
    packet.appendOctet2(message.newCrnti);
    packet.appendOctet2(message.t304Ms);
    packet.appendOctet4(message.causeCode);
    return packet;
}

std::optional<XnMessage> DecodeXnMessage(const OctetString &packet)
{
    if (packet.length() < kEncodedMessageLength)
        return std::nullopt;

    if (packet.getI(kVersionOffset) != static_cast<int>(kProtocolVersion))
        return std::nullopt;

    auto rawType = static_cast<uint8_t>(packet.getI(kTypeOffset));
    if (rawType < static_cast<uint8_t>(XnMessageType::HandoverRequest) ||
        rawType > static_cast<uint8_t>(XnMessageType::UeContextRelease))
        return std::nullopt;

    XnMessage message{};
    message.type = static_cast<XnMessageType>(rawType);
    message.transactionId = packet.get4UI(kTxnOffset);
    message.ueId = packet.get4I(kUeIdOffset);
    message.sourcePci = packet.get2I(kSourcePciOffset);
    message.targetPci = packet.get2I(kTargetPciOffset);
    message.newCrnti = packet.get2I(kCrntiOffset);
    message.t304Ms = packet.get2I(kT304Offset);
    message.causeCode = packet.get4I(kCauseOffset);
    return message;
}

const char *ToString(XnMessageType type)
{
    switch (type)
    {
    case XnMessageType::HandoverRequest:
        return "HandoverRequest";
    case XnMessageType::HandoverRequestAck:
        return "HandoverRequestAck";
    case XnMessageType::HandoverPreparationFailure:
        return "HandoverPreparationFailure";
    case XnMessageType::HandoverComplete:
        return "HandoverComplete";
    case XnMessageType::UeContextRelease:
        return "UeContextRelease";
    default:
        return "Unknown";
    }
}

} // namespace nr::gnb::xn
