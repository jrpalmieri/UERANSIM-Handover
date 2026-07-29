# UERANSIM Test Harness

A Python-based test harness for integration- and unit-testing the
**UERANSIM UE and gNB** (User Equipment and base station emulators).
The harness simulates peer nodes entirely in Python — a fake gNB for UE tests
and a fake AMF / fake UE for gNB tests — and drives the full 5G NR signaling
stack through real subprocess binaries.

---

## Architecture

```
┌────────────┐  UDP :4997 (RLS)  ┌──────────────┐
│  nr-ue     │ ◄────────────────►│  FakeGnb     │
│ (C++ bin)  │                   │  (Python)    │
└────────────┘                   └──────┬───────┘
                                        │
                               orchestrates / inspects
                                        │
               ┌────────────────────────┴──────────────────────┐
               │  harness/                                      │
               │  ├── fake_gnb.py      (RLS + RRC + NAS peer)  │
               │  ├── rls_protocol.py  (RLS encode / decode)    │
               │  ├── rrc_builder.py   (RRC ASN.1 encoding)     │
               │  ├── nas_builder.py   (5GMM NAS encoding)      │
               │  ├── milenage.py      (5G-AKA key derivation)  │
               │  └── ue_process.py    (nr-ue lifecycle + logs) │
               └───────────────────────────────────────────────┘

┌────────────┐  SCTP NGAP  ┌──────────────┐  RLS  ┌──────────────┐
│  nr-gnb    │◄───────────►│  FakeAmf     │       │  FakeUe      │
│ (C++ bin)  │             │  (Python)    │       │  (Python)    │
└────────────┘             └──────────────┘       └──────┬───────┘
                                                         │ UDP (RLS)
                                                   ◄─────┘
               ┌────────────────────────────────────────────────┐
               │  harness/                                       │
               │  ├── fake_amf.py      (NGAP SCTP server)       │
               │  ├── fake_ue.py       (RLS UE simulator)       │
               │  ├── gnb_process.py   (nr-gnb lifecycle + logs)│
               │  ├── ngap_codec.py    (minimal NGAP APER codec) │
               │  └── marks.py         (shared pytest marks)    │
               └────────────────────────────────────────────────┘
```

---

## Directory layout

```
tests/
├── README.md
├── requirements.txt
├── conftest.py                    # shared fixtures (fake_gnb, ue_process, meas_injector)
├── configs/
│   ├── test-ue.yaml
│   ├── test_ue1.yaml
│   └── test_gnb2.yaml
├── data/
│   ├── asn1_specs/                # RRC ASN.1 schema used by rrc_builder
│   └── asn1_vectors/              # NGAP golden encode/decode vectors
├── harness/                       # shared test harness (UE + gNB tests)
│   ├── fake_gnb.py                # FakeGnb: RLS + RRC + NAS peer for UE tests
│   ├── fake_amf.py                # FakeAmf: NGAP SCTP server for gNB tests
│   ├── fake_ue.py                 # FakeUe: RLS UE simulator for gNB tests
│   ├── gnb_process.py             # GnbProcess: nr-gnb lifecycle manager
│   ├── meas_injector.py           # MeasurementInjector: OOB measurement stub (unused)
│   ├── marks.py                   # shared pytest marks (gnb_binary_exists, etc.)
│   ├── milenage.py                # Milenage / 5G-AKA key derivation
│   ├── nas_builder.py             # 5GMM NAS message encoder
│   ├── ngap_codec.py              # Minimal NGAP APER IE-level codec
│   ├── rls_protocol.py            # RLS binary protocol encode / decode
│   ├── rrc_builder.py             # RRC message encoder (via asn1tools + fallbacks)
│   └── ue_process.py              # UeProcess: nr-ue lifecycle + log capture
├── ue/                            # UE integration and unit tests
│   ├── conftest.py                # UE-specific fixtures
│   ├── test_ue_cho.py             # Conditional Handover (CHO) evaluation logic
│   ├── test_ue_execution.py       # UE binary smoke tests (startup, config)
│   ├── test_ue_handover.py        # Signal- and distance-based handover flows
│   ├── test_ue_handover_rrc.py    # RRC handover message handling (Phase 2)
│   ├── test_ue_measurement.py     # Measurement events A2 / A3 / A5 / TTT
│   ├── test_ue_nas_states.py      # NAS state machine (RM / CM / MM / PDU)
│   ├── test_ue_rls.py             # RLS heartbeat and signal-strength tracking
│   ├── test_ue_rrc_states.py      # RRC state transitions (IDLE / CONNECTED / INACTIVE)
│   ├── test_ue_sib19_multi.py     # SIB19 multi-entry NCI map parsing
│   ├── test_ue_signaling.py       # End-to-end RRC + NAS signaling sequences
│   └── test_ue_signaling_units.py # Harness unit tests (RLS codec, crypto, NAS builder)
└── gnb/
    ├── conftest.py                # gNB-specific fixtures
    ├── test_gnb_handover.py       # gNB-side handover (NGAP HandoverRequired)
    ├── test_gnb_health.py         # gNB startup and CLI health checks
    ├── test_gnb_neighbors_cli.py  # Xn neighbor list CLI operations
    ├── test_gnb_ngap.py           # NGAP procedures (NGSetup, InitialUE, PathSwitch, …)
    ├── test_gnb_ngap_ngsetup.py   # Cross-codec NGSetup round-trip test
    ├── test_gnb_registration.py   # End-to-end UE registration through gNB + FakeAmf
    ├── test_gnb_rrc.py            # gNB-side RRC setup and measurement config
    ├── test_gnb_rsrp_models.py    # Distance/NTN RSRP model arithmetic
    ├── test_gnb_sib19.py          # SIB19 NTN broadcast and TLE loading
    ├── test_gnb_xn_handshake.py   # Two-gNB XnSetup handshake (XnAP)
    └── test_sat_time_runtime.py   # Satellite time controls (pause / resume / offset)
```


---

## Prerequisites

| Requirement | Notes |
|---|---|
| Python ≥ 3.9 | f-strings, `dataclasses` |
| `nr-ue` binary | Pre-built in `build/nr-ue` — run `make` in the project root |
| `nr-gnb` binary | Pre-built in `build/nr-gnb` — required for gNB tests |
| Network access | Tests bind UDP 4997 (RLS) and SCTP on localhost |
| **No root required** | Tests avoid PDU sessions / TUN |

### Python packages

```bash
cd tests
pip install -r requirements.txt
```

`requirements.txt` includes: `pytest`, `pytest-timeout`, `pyyaml`,
`cryptography`, and `asn1tools` (optional — enables full ASN.1 RRC encoding).

---

## Running the tests

### Run all tests

```bash
cd tests
pytest -v --timeout=120
# or from project root:
python3 -m pytest tests/ue/ tests/gnb/ -v
```

### Run only UE tests

```bash
python3 -m pytest tests/ue/ -v
```

### Run only gNB tests

```bash
python3 -m pytest tests/gnb/ -v
```

### Run a single test file

```bash
python3 -m pytest tests/ue/test_ue_rrc_states.py -v
```

### Skip binary-dependent tests

Tests that require `nr-ue` or `nr-gnb` are decorated with
`@ue_binary_exists` / `@gnb_binary_exists` and are **automatically
skipped** when the binary is absent.  Pure harness unit tests always run.

### Run with extra debug output

```bash
python3 -m pytest tests/ue/ -v -s --timeout=180
```

---

## Test suites

### UE tests (`tests/ue/`)

| File | Scope | What it covers |
|---|---|---|
| `test_ue_execution.py` | Integration | Binary startup, config generation, process lifecycle |
| `test_ue_rrc_states.py` | Unit + Integration | RRC state transitions: IDLE → CONNECTED → IDLE; RLF |
| `test_ue_nas_states.py` | Unit + Integration | NAS state machine: RM / CM / MM substates; PDU sessions |
| `test_ue_rls.py` | Integration | RLS heartbeat exchange; signal-strength tracking via `cellDbMeas` |
| `test_ue_signaling_units.py` | Unit | RLS codec round trips; Milenage / 5G-AKA; NAS builder output |
| `test_ue_signaling.py` | Integration | HeartBeat; RRCSetupRequest / Complete; NAS registration sequence |
| `test_ue_measurement.py` | Integration | A2 / A3 / A5 event evaluation and MeasurementReport generation |
| `test_ue_sib19_multi.py` | Integration | SIB19 multi-entry binary PDU parsing; NCI map storage |
| `test_ue_cho.py` | Unit + Integration | CHO candidate lifecycle; condition evaluation; arbitration |
| `test_ue_handover_rrc.py` | Unit + Integration | RRCReconfiguration-with-sync decoding; T304 timer; handover success / failure |
| `test_ue_handover.py` | Integration | Signal-based (A3/A5) and distance-based (D1/CHO) handover flows |

### gNB tests (`tests/gnb/`)

| File | Scope | What it covers |
|---|---|---|
| `test_gnb_health.py` | Integration | Binary startup, CLI responsiveness |
| `test_gnb_rsrp_models.py` | Unit | Distance-based and NTN RSRP model arithmetic |
| `test_gnb_ngap.py` | Integration | NGAP procedures: NGSetup, InitialUEMessage, HandoverRequired, PathSwitch |
| `test_gnb_ngap_ngsetup.py` | Integration | Cross-codec NGSetup round-trip (gNB ASN.1 ↔ Python NGAP codec) |
| `test_gnb_registration.py` | Integration | End-to-end UE registration: RLS → RRC → NAS → NGAP |
| `test_gnb_rrc.py` | Integration | RRCSetup flow; MeasConfig delivery; MeasurementReport reception; handover command |
| `test_gnb_handover.py` | Integration | gNB-side handover: NGAP HandoverRequired triggered by MeasurementReport |
| `test_gnb_neighbors_cli.py` | Integration | Xn neighbor list: CLI add / remove / replace / deduplication |
| `test_gnb_xn_handshake.py` | Integration | Two-gNB XnSetup handshake; field integrity across APER encode → decode |
| `test_gnb_sib19.py` | Integration | SIB19 NTN broadcast; TLE loading from config; periodic position updates |
| `test_sat_time_runtime.py` | Integration | Satellite time offset, pause/resume controls on gNB and UE |

---

## Key harness components

| Module | Description |
|---|---|
| **`fake_gnb.py`** | Listens on UDP 4997, speaks RLS, drives full RRC + NAS flows for UE tests |
| **`fake_amf.py`** | SCTP NGAP server; handles NGSetup, InitialUEMessage, and handover procedures for gNB tests |
| **`fake_ue.py`** | RLS UE simulator; sends HeartBeats to the gNB and receives downlink RRC PDUs |
| **`gnb_process.py`** | Starts/stops `nr-gnb`, captures log output, provides wait-for-log helpers |
| **`ue_process.py`** | Starts/stops `nr-ue`, manages config YAML, captures stdout, parses RRC/NAS/CM states |
| **`rls_protocol.py`** | Encode / decode RLS HeartBeat, HeartBeatAck, PduTransmission |
| **`rrc_builder.py`** | RRC message encoding (MIB, SIB1, RRCSetup, RRCReconfiguration, SIB19, …) via `asn1tools` with pre-compiled fallbacks |
| **`nas_builder.py`** | 5GMM NAS message encoding (AuthReq, SecModeCmd, RegAccept, …) |
| **`milenage.py`** | Milenage (TS 35.206) and full 5G-AKA / NAS security key derivation chain |
| **`ngap_codec.py`** | Minimal APER IE-level NGAP codec (no external ASN.1 dependency) |
| **`marks.py`** | `gnb_binary_exists` / `ue_binary_exists` skip decorators |

---

## Signaling flows

### UE registration

```
UE (nr-ue)                       FakeGnb (Python)
 │── HeartBeat ─────────────────►│
 │◄──────────── HeartBeatAck ────│  (with cell dBm)
 │◄──────────── MIB (BCCH_BCH) ─│
 │◄──────────── SIB1 (BCCH_DL_SCH)│
 │                                │  UE performs cell selection
 │── RRCSetupRequest (UL_CCCH) ─►│
 │◄── RRCSetup (DL_CCCH) ────────│
 │── RRCSetupComplete (UL_DCCH) ►│  NAS RegistrationRequest
 │◄── DLInfoTransfer ────────────│  NAS AuthenticationRequest
 │── ULInfoTransfer ─────────────►│  NAS AuthenticationResponse
 │◄── DLInfoTransfer ────────────│  NAS SecurityModeCommand
 │── ULInfoTransfer ─────────────►│  NAS SecurityModeComplete
 │◄── DLInfoTransfer ────────────│  NAS RegistrationAccept
 │  UE: RRC_CONNECTED, RM_REGISTERED, CM_CONNECTED
```

### gNB registration (with FakeAmf)

```
nr-gnb                   FakeAmf (Python)
 │── NGSetupRequest ─────────────►│
 │◄── NGSetupResponse ────────────│
 │── InitialUEMessage ────────────►│  (triggered by FakeUe attach)
 │◄── DownlinkNASTransport ───────│  (NAS auth / security / reg accept)
 │── UplinkNASTransport ──────────►│
```

### Measurement reporting

```
FakeGnb                          UE (nr-ue)
 │── RRCReconfiguration ─────────►│  (measConfig: A2/A3/A5 event)
 │                                │
 │  UE evaluates measurements     │  (from RLS HeartBeatAck dBm values)
 │                                │
 │◄── MeasurementReport (UL_DCCH)─│  (when event + TTT satisfied)
```

---

## UE configuration

Test configs live in [`configs/`](configs/).  The default UE
(`configs/test-ue.yaml`) uses these credentials:

| Parameter | Value |
|---|---|
| SUPI | `imsi-286010000000001` |
| MCC / MNC | 286 / 93 |
| Key (K) | `465B5CE8B199B49FAA5F0A2EE238A6BC` |
| OP | `E8ED289DEBA952E4283B54E88E6183CA` |
| OP type | OP (not OPc) |

No real 5G core is required — the fake gNB / fake AMF handle all NAS messages.

---

## Troubleshooting

| Problem | Solution |
|---|---|
| UE tests skip with "nr-ue binary not found" | Run `make nr-ue` in the project root |
| gNB tests skip with "nr-gnb binary not found" | Run `make nr-gnb` in the project root |
| Port 4997 in use | Kill stale processes: `pkill nr-ue; pkill nr-gnb` |
| `asn1tools` compilation slow | First run compiles the RRC ASN.1 schema (~30 s); subsequent runs use the cached result |
| TUN errors | Tests avoid PDU sessions.  If you add session tests, run with `sudo` |

---

## Protocol references

- **RLS Protocol**: UERANSIM link-simulation protocol (`src/lib/rls/`)
- **NR RRC**: harness and simulator encode using 3GPP TS 38.331
  **Release 18.9** (`tests/data/asn1_specs/rrc-rel18-v18_9.asn1`) via
  `asn1tools`.
- **5G NAS**: 3GPP TS 24.501
- **NGAP**: 3GPP TS 38.413
- **Milenage / 5G-AKA**: 3GPP TS 35.206, TS 33.501
- **XnAP**: 3GPP TS 38.423
