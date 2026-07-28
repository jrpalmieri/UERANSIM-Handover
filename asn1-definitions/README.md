
ASN.1 definitions for Message formats.  Inputs to asn1c compiler to generate the C files in /src/asn

XnAP - the Xn Application Protocol for the XN interface (gNB-gNB)
NGAP - the Next Generate Application Protocol for the N2 interface (gNB-AMF)
RRC - the Radio Resource Control Protocol for the Uu interface (gNB-UE)

## Regenerating

The exact command is recorded in the preamble of every generated file, with a relative
`-D` path, so it can be re-run from the repository root. The compiler is
`/home/joe/repos/asn1c-r18` on branch `prefix-identifiers-and-aper` (see
docs/ASN1_R18_Migration_Plan.md). Generation writes the shared runtime into
`src/asn/asn1c` as a side effect of `-fcommon=asn1c`; that is a no-op, because the
runtime is byte-identical to the fork's skeletons.

## Local deviations from the published specifications

These files are otherwise the 3GPP releases named in their filenames. One line is not:

- **`rrc-rel18-v18_9.asn1`, `PhysCellId`** — the `(0..1007)` constraint is removed so the
  36-bit NCI this project transports in `physCellId` can be encoded. Marked with a comment
  at the definition; rationale and consequences in docs/ASN1_R18_Migration_Plan.md §9.2.
