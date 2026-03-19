//
// Simulator-native Xn protocol codec for gNB-to-gNB handover signaling.
//

#pragma once

#include <optional>
#include <string>

#include <utils/octet_string.hpp>

namespace nr::gnb::xn
{

enum class XnMessageType : uint8_t
{
    HandoverRequest = 1,
    HandoverRequestAck = 2,
    HandoverPreparationFailure = 3,
    HandoverComplete = 4,
    UeContextRelease = 5,
};

struct XnMessage
{
    XnMessageType type{XnMessageType::HandoverRequest};
    uint32_t transactionId{};
    int ueId{};
    int sourcePci{};
    int targetPci{};
    int newCrnti{};
    int t304Ms{};
    int causeCode{};
};

OctetString EncodeXnMessage(const XnMessage &message);
std::optional<XnMessage> DecodeXnMessage(const OctetString &packet);
const char *ToString(XnMessageType type);

} // namespace nr::gnb::xn
