//
// Golden-vector harness for the ASN.1 runtime.
//
// Decodes a corpus of real PDUs captured from this project's own runs, then
// re-encodes each one and reports a digest. The point is not to validate 3GPP
// conformance -- it is to detect any change in codec behaviour when the
// skeleton runtime underneath is replaced (see docs/ASN1_R18_Migration_Plan.md,
// stage 0). Run it before and after the swap; the output must be identical.
//
// Usage: asn1_golden <vector-file> [...]
//   Vector file format: "A1VEC\0", uint32 count, then count records of
//   uint32 length + payload. Little-endian, as produced by the corpus builder.
//

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <asn/ngap/ASN_NGAP_NGAP-PDU.h>

namespace
{

// FNV-1a. A change detector, not a security hash.
uint64_t fnv1a(const uint8_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

struct Vectors
{
    std::vector<std::vector<uint8_t>> items;
};

bool loadVectors(const char *path, Vectors &out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    char magic[6] = {};
    uint32_t count = 0;
    if (fread(magic, 1, 6, f) != 6 || memcmp(magic, "A1VEC\0", 6) != 0 ||
        fread(&count, 4, 1, f) != 1)
    {
        fprintf(stderr, "%s: not a vector file\n", path);
        fclose(f);
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t len = 0;
        if (fread(&len, 4, 1, f) != 1)
            break;
        std::vector<uint8_t> buf(len);
        if (len && fread(buf.data(), 1, len, f) != len)
            break;
        out.items.push_back(std::move(buf));
    }
    fclose(f);
    return out.items.size() == count;
}

int consumeBytes(const void *buffer, size_t size, void *key)
{
    auto *acc = static_cast<std::vector<uint8_t> *>(key);
    auto *p = static_cast<const uint8_t *>(buffer);
    acc->insert(acc->end(), p, p + size);
    return 0;
}

// Decode with APER, re-encode with APER, digest both the structure's
// re-encoding and the round-trip fidelity.
void runNgap(const Vectors &v)
{
    size_t okDecode = 0, okRoundTrip = 0, failDecode = 0, failEncode = 0;

    for (size_t i = 0; i < v.items.size(); i++)
    {
        const auto &in = v.items[i];
        void *pdu = nullptr;

        asn_dec_rval_t rv = aper_decode(nullptr, &asn_DEF_ASN_NGAP_NGAP_PDU, &pdu, in.data(),
                                        in.size(), 0, 0);

        if (rv.code != RC_OK)
        {
            failDecode++;
            printf("%04zu DECODE_FAIL code=%d consumed=%zu len=%zu\n", i, (int)rv.code,
                   rv.consumed, in.size());
            ASN_STRUCT_FREE(asn_DEF_ASN_NGAP_NGAP_PDU, pdu);
            continue;
        }
        okDecode++;

        std::vector<uint8_t> reenc;
        asn_enc_rval_t er =
            aper_encode(&asn_DEF_ASN_NGAP_NGAP_PDU, nullptr, pdu, consumeBytes, &reenc);

        if (er.encoded < 0)
        {
            failEncode++;
            printf("%04zu ENCODE_FAIL len=%zu\n", i, in.size());
        }
        else
        {
            bool same = reenc.size() == in.size() && memcmp(reenc.data(), in.data(), in.size()) == 0;
            if (same)
                okRoundTrip++;
            printf("%04zu OK in=%zu consumed=%zu out=%zu%s hash=%016llx\n", i, in.size(),
                   rv.consumed, reenc.size(), same ? " identical" : " differs",
                   (unsigned long long)fnv1a(reenc.data(), reenc.size()));
        }

        ASN_STRUCT_FREE(asn_DEF_ASN_NGAP_NGAP_PDU, pdu);
    }

    printf("SUMMARY ngap vectors=%zu decoded=%zu decode_failed=%zu encode_failed=%zu "
           "byte_identical=%zu\n",
           v.items.size(), okDecode, failDecode, failEncode, okRoundTrip);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <vector-file> [...]\n", argv[0]);
        return 2;
    }

    for (int i = 1; i < argc; i++)
    {
        Vectors v;
        if (!loadVectors(argv[i], v))
            return 1;

        std::string name = argv[i];
        printf("=== %s (%zu vectors)\n", name.substr(name.find_last_of('/') + 1).c_str(),
               v.items.size());

        if (name.find("ngap") != std::string::npos)
            runNgap(v);
        else
        {
            fprintf(stderr, "%s: no codec bound to this vector file\n", argv[i]);
            return 2;
        }
    }
    return 0;
}
