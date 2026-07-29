#pragma once

#include <cstdio>

#include <asn_application.h>
#include <lib/asn/utils.hpp>
#include <utils/octet_string.hpp>

namespace nr::gnb::xnap_encode
{

template <typename T>
inline std::string EncodeXer(const asn_TYPE_descriptor_t &desc, T *pdu)
{
    auto res = asn_encode_to_new_buffer(nullptr, ATS_CANONICAL_XER, &desc, pdu);

    if (res.buffer == nullptr || res.result.encoded < 0)
        return {};

    std::string s{reinterpret_cast<char *>(res.buffer), reinterpret_cast<char *>(res.buffer) + res.result.encoded};
    free(res.buffer);
    return s;
}

template <typename T>
inline bool Encode(const asn_TYPE_descriptor_t &desc, T *pdu, ssize_t &encoded, uint8_t *&buffer)
{
    auto res = asn_encode_to_new_buffer(nullptr, ATS_ALIGNED_CANONICAL_PER, &desc, pdu);

    if (res.buffer == nullptr || res.result.encoded < 0)
    {
        // asn_encode_to_new_buffer discards asn_enc_rval_t::failed_type on the
        // ATS_ALIGNED_CANONICAL_PER path unless surfaced here -- without this,
        // every caller's "APER encoding failed" log has zero information about
        // which nested IE actually rejected the encode. failed_type is often
        // just the generic "value" open-type field name at whatever depth the
        // recursion stopped, so also dump XER (which has far fewer constraint
        // checks) to see the actual populated structure.
        std::fprintf(stderr, "xnap_encode::Encode failed: type=%s failed_type=%s\n", desc.name,
                     res.result.failed_type ? res.result.failed_type->name : "(null)");
        std::string xer = EncodeXer(desc, pdu);
        std::fprintf(stderr, "xnap_encode::Encode XER dump:\n%s\n", xer.empty() ? "(XER encode also failed)" : xer.c_str());
        return false;
    }

    encoded = res.result.encoded;
    buffer = new uint8_t[encoded];
    std::memcpy(buffer, res.buffer, encoded);
    free(res.buffer);

    return true;
}

template <typename T>
inline T *Decode(asn_TYPE_descriptor_t &desc, const uint8_t *buffer, size_t size)
{
    auto *pdu = asn::New<T>();
    auto res = aper_decode(nullptr, &desc, reinterpret_cast<void **>(&pdu), buffer, size, 0, 0);

    if (res.code != RC_OK)
    {
        asn::Free(desc, pdu);
        return nullptr;
    }

    return pdu;
}

} // namespace nr::gnb::xnap_encode
