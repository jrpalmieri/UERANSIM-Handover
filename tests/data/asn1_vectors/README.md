# ASN.1 golden vectors

Corpus and expected output for `asn1_golden` (built from
[tests/asn1_golden.cpp](../../asn1_golden.cpp)). Its purpose is to detect any
change in ASN.1 **codec behaviour** when the skeleton runtime under
`src/asn/asn1c` is replaced — see [docs/ASN1_R18_Migration_Plan.md](../../../docs/ASN1_R18_Migration_Plan.md),
stage 0.

## Files

| File | Contents |
| ---- | -------- |
| `ngap.vec` | 1156 NGAP PDUs, APER-encoded, extracted from the SCTP N2 traffic in `tools/dashboard/logs/*/*.pcap` |
| `ngap.expected` | Output of `asn1_golden ngap.vec` recorded against the pre-migration runtime |

`ngap.vec` format: `"A1VEC\0"`, `uint32` count, then that many records of
`uint32` length followed by the payload (little-endian).

The corpus was selected from 29786 unique captured PDUs: every distinct
`(pduType, procedureCode)` pair present (17 of them), and for each pair the
shortest PDU, the longest, and up to 80 more drawn with a fixed seed so the
selection is reproducible.

## Running

```sh
./build/asn1_golden tests/data/asn1_vectors/ngap.vec > /tmp/now.txt
diff tests/data/asn1_vectors/ngap.expected /tmp/now.txt && echo "codec behaviour unchanged"
```

Every vector currently decodes and re-encodes **byte-identically**, so any diff
at all is a real behavioural change and must be explained before proceeding.

## Coverage

This corpus exercises APER (NGAP), which is the codec ported by hand during the
migration and therefore the one at risk. The UPER side (RRC) is covered by
`rrc_d1_probe`, `rrc_reference_location_tests` and `tools/gen_sib1_hex.cpp`,
whose outputs are deterministic and should likewise be compared across a runtime
change. XnAP has no vectors: nothing in the captures exercises the Xn interface.
