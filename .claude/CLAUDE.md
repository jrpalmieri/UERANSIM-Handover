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