//
// Wire format of the simulator's custom source-to-target handover container.
//
// RRC-internal: this header belongs to the gNB RRC task and nothing outside it may
// include it.  The container is opaque to every node between the two gNBs (the AMF
// on N2, nothing on Xn), and it is equally opaque to this gNB's own NGAP and Xn
// tasks — they carry the byte string from the source RRC to the target RRC without
// looking inside it, exactly as the real interfaces do.
//
// Instead of a standards-compliant HandoverPreparationInformation the simulator
// carries a flat serialization of the source's RrcUeContext.  The payload codec
// (EncodeRrcContext / DecodeRrcContext) is defined in gnb/rrc/handover.cpp; the
// framing around it lives here so the two halves of the format stay together.
//
// Layout:
//   0..3    magic "S2TC"
//   4       version
//   5       flags (bit 0: CHO indication)
//   6..7    reserved
//   8..11   RRC context payload length
//   12..15  trailing padding-blob length (used to simulate realistic container sizes)
//   16..    payload, then padding blob
//

#pragma once

#include <utils/octet_string.hpp>

#include <cstdint>

namespace nr::gnb
{
struct RrcUeContext;
}

namespace nr::gnb::ho_container
{

/**
 * @brief Serializes the parts of a UE's RRC context that the target needs into a flat
 * binary payload: identity, C-RNTI, the security context (NH/NCC, algorithm bitmaps,
 * K_gNB) and the full measurement configuration.
 *
 * Defined in gnb/rrc/handover.cpp, next to the decoder it mirrors.
 */
OctetString EncodeRrcContext(const RrcUeContext &ue);

/**
 * @brief Inverse of EncodeRrcContext(): rebuilds a heap-allocated RrcUeContext.
 *
 * @return nullptr if the payload is truncated or otherwise malformed; the caller owns
 *         the returned pointer otherwise.
 */
RrcUeContext *DecodeRrcContext(const OctetString &data);

inline constexpr uint32_t S2T_MAGIC = 0x53325443; // "S2TC"
inline constexpr uint8_t S2T_VERSION = 1;
inline constexpr uint8_t S2T_FLAG_CHO_INDICATION = 0x01;
inline constexpr int S2T_HEADER_SIZE = 16;

/**
 * @brief Frames an encoded RRC context payload as a source-to-target container.
 *
 * @param rrcContext    the RRC context payload (EncodeRrcContext output)
 * @param blobSize      bytes of zero padding to append, to simulate a larger real payload
 * @param choIndication true if this container prepares a conditional handover
 */
inline OctetString WrapSourceToTarget(const OctetString &rrcContext, uint32_t blobSize, bool choIndication)
{
    OctetString out{};
    out.appendOctet4(S2T_MAGIC);
    out.appendOctet(S2T_VERSION);
    out.appendOctet(static_cast<uint8_t>(choIndication ? S2T_FLAG_CHO_INDICATION : 0));
    out.appendOctet2(static_cast<uint16_t>(0)); // reserved
    out.appendOctet4(static_cast<uint32_t>(rrcContext.length()));
    out.appendOctet4(blobSize);
    out.append(rrcContext);
    out.append(OctetString::FromSpare(static_cast<int>(blobSize)));
    return out;
}

/**
 * @brief Inverse of WrapSourceToTarget(): validates the frame and yields the payload.
 *
 * Both outputs are optional; a caller that only needs one of them passes nullptr
 * for the other.
 *
 * @param container        the received container
 * @param rrcContextOut    receives the RRC context payload, may be nullptr
 * @param choIndicationOut receives the CHO indication flag, may be nullptr
 * @return false if the container is truncated or does not carry this format
 */
inline bool UnwrapSourceToTarget(const OctetString &container, OctetString *rrcContextOut,
                                 bool *choIndicationOut)
{
    if (container.length() < S2T_HEADER_SIZE)
        return false;
    if (static_cast<uint32_t>(container.get4I(0)) != S2T_MAGIC)
        return false;
    if (static_cast<uint8_t>(container.getI(4)) != S2T_VERSION)
        return false;

    const uint8_t flags = static_cast<uint8_t>(container.getI(5));
    // octets 6..7 are reserved
    const int contextLen = container.get4I(8);
    // octets 12..15 hold the trailing blob length, which the receiver ignores.

    if (contextLen < 0 || S2T_HEADER_SIZE + contextLen > container.length())
        return false;

    if (choIndicationOut)
        *choIndicationOut = (flags & S2T_FLAG_CHO_INDICATION) != 0;
    if (rrcContextOut)
        *rrcContextOut = container.subCopy(S2T_HEADER_SIZE, contextLen);

    return true;
}

} // namespace nr::gnb::ho_container
