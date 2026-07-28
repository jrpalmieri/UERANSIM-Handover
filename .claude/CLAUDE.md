## Repo Layout

- asn1-definitions: ASN1 files for IEs of 3GPP protocol messages
- build: build results from building of UE, gNB, CLI, tests
- config: config yaml files for gNB and UE
- src: source code for UE and gNB
    - asn: asn1 C files for each supported protocol, generated using asn1c
    - ext: external libraries
    - gnb: gNB source code
    - lib: library code specific to 3gpp protocols, available to gNB, UE and CLI
    - ue: UE should code
    - utils: utility functions, avaialble to gNB, UE and CLI
- tests: test harnesses and unit tests for UE and gNB
- tle_data: two-line element data for testing satellite (NTN) operations
- tools: utilities for testing and development

## ASN.1 code is generated — never edit it

Everything in `src/asn` is asn1c output from `asn1-definitions/`. Do not hand-edit it, and
do not add files to it: regenerate with `tools/regen_asn1.sh` (all three protocols, or one
by name). Running it without changing an input leaves the tree byte-identical, so
`git status src/asn` after a run is how you check the code really matches the definitions.
The compiler is a fork pinned by commit in that script. See `asn1-definitions/README.md`
for the toolchain and `docs/ASN1_R18_Migration_Plan.md` for why the tree is the way it is.