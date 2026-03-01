# UERANSIM UE Test Harness

A Python-based test harness for integration- and unit-testing the
**UERANSIM UE** (User Equipment emulator).  The harness simulates a
gNB (base station) entirely in Python, injects Out-of-Band (OOB)
measurements, and verifies UE state transitions and signaling.

---

## Architecture

```
┌────────────┐  UDP :4997 (RLS)  ┌──────────────┐
│  nr-ue     │ ◄────────────────►│  FakeGnb     │
│ (C++ bin)  │                   │  (Python)     │
└────────────┘                   └──────┬───────┘
      ▲                                 │
      │ UDP :7200 (OOB)         orchestrates
      │                                 │
┌─────┴──────┐        ┌────────────────┴──────────────┐
│ MeasInject  │        │  rls_protocol / rrc_builder / │
│ (Python)    │        │  nas_builder / milenage       │
└────────────┘        └───────────────────────────────┘
```

| Component | Description |
|---|---|
| **FakeGnb** | Listens on UDP 4997, speaks the RLS binary protocol, drives RRC/NAS flows |
| **MeasurementInjector** | Sends JSON cell measurements to the UE's OOB UDP port (7200) |
| **UeProcess** | Starts/stops `nr-ue`, manages config YAML, captures stdout logs |
| **rls_protocol** | Encode / decode RLS HeartBeat, HeartBeatAck, PduTransmission, … |
| **rrc_builder** | RRC message encoding (MIB, SIB1, RRCSetup, Reconfiguration, …) via `asn1tools` with fallback |
| **nas_builder** | 5GMM NAS message encoding (AuthReq, SecModeCmd, RegAccept, …) |
| **milenage** | Milenage (TS 35.206) + full 5G-AKA key derivation chain |

---

## Prerequisites

| Requirement | Notes |
|---|---|
| Python ≥ 3.9 | f-strings, `dataclasses` |
| `nr-ue` binary | Pre-built in `build/nr-ue` (run `make` in the project root first) |
| Network access | Tests bind UDP ports 4997 and 7200 on localhost |
| **No root required** | Tests avoid PDU sessions / TUN; measurement-only flows |

### Python packages

```bash
cd tests
pip install -r requirements.txt
```

Contents of `requirements.txt`:
- pytest ≥ 7.0
- pytest-timeout ≥ 2.1
- pyyaml ≥ 6.0
- cryptography ≥ 41.0
- asn1tools ≥ 0.166 *(optional – enables full ASN.1 encoding/decoding)*

---

## Directory layout

```
tests/
├── conftest.py                # pytest fixtures (fake_gnb, ue_process, …)
├── requirements.txt
├── README.md                  # ← you are here
├── configs/
│   └── test-ue.yaml           # UE config with OOB measurement provider
├── harness/
│   ├── __init__.py
│   ├── rls_protocol.py        # RLS binary protocol
│   ├── fake_gnb.py            # Fake gNB orchestrator
│   ├── meas_injector.py       # OOB measurement injector
│   ├── ue_process.py          # nr-ue process manager
│   ├── rrc_builder.py         # RRC message codec
│   ├── nas_builder.py         # NAS message builder
│   └── milenage.py            # Milenage + 5G-AKA crypto
├── test_ue_rrc_states.py      # RRC state transition tests
├── test_ue_nas_states.py      # NAS / RM / CM / MM state tests
├── test_measurement.py        # Measurement event A2 / A3 / A5 tests
└── test_signaling.py          # Signaling correctness tests
```

---

## Running the tests

### Run all tests (including integration)

```bash
cd tests
pytest -v --timeout=120
```

### Run only unit tests (no `nr-ue` binary needed)

```bash
pytest -v -k "not integration" --timeout=60
```

The harness marks integration tests with `@ue_binary_exists` so they
are automatically **skipped** when the binary is not present.

### Run a single test file

```bash
pytest -v test_measurement.py
```

### Run with extra debug output

```bash
pytest -v -s --timeout=180
```

---

## Test categories

### 1. RRC State Tests (`test_ue_rrc_states.py`)

| Test | What it verifies |
|---|---|
| Parses RRC_IDLE / CONNECTED / INACTIVE from log | Unit — no subprocess |
| UE starts in RRC_IDLE | Integration — verifies initial state |
| UE transitions to RRC_CONNECTED | Integration — full RRC setup |
| UE returns to RRC_IDLE on release | Integration — RRCRelease flow |
| Radio link failure on signal loss | Integration — catastrophic dBm drop |

### 2. NAS State Tests (`test_ue_nas_states.py`)

| Test | What it verifies |
|---|---|
| Parses RM / CM / MM states from log | Unit |
| Initial RM_DEREGISTERED | Integration |
| RM_REGISTERED after registration | Integration |
| CM state follows RRC | Integration |
| MM substates during registration | Integration |

### 3. Measurement Tests (`test_measurement.py`)

| Test | What it verifies |
|---|---|
| A2 event fired when serving ≤ threshold | Integration |
| A2 *not* fired above threshold | Integration |
| A3 event (neighbour better by offset) | Integration |
| A5 dual-threshold | Integration |
| Time-to-trigger delay | Integration |
| One-shot reporting per measId | Integration |
| RSRP encoding math | Unit |
| Hysteresis boundary analysis | Unit |

### 4. Signaling Tests (`test_signaling.py`)

| Test | What it verifies |
|---|---|
| RLS encode/decode round trips | Unit |
| RLS header format (magic, version) | Unit |
| Milenage key derivation | Unit |
| NAS message format | Unit |
| HeartBeat exchange | Integration |
| RRCSetupRequest on UL-CCCH | Integration |
| RRCSetupComplete on UL-DCCH | Integration |
| MeasurementReport on UL-DCCH | Integration |
| NAS registration message sequence | Integration |

---

## How the fake gNB works

### Registration flow

```
UE                              FakeGnb
 │── HeartBeat ──────────────────►│
 │◄─────────── HeartBeatAck ──────│  (with cell dBm)
 │◄─────────── MIB (BCCH_BCH) ───│
 │◄─────────── SIB1 (BCCH_DL_SCH)│
 │                                │  UE performs cell selection
 │── RRCSetupRequest (UL_CCCH) ──►│
 │◄── RRCSetup (DL_CCCH) ────────│
 │── RRCSetupComplete (UL_DCCH) ─►│  contains NAS RegistrationRequest
 │◄── DLInfoTransfer ────────────│  NAS AuthenticationRequest
 │── ULInfoTransfer ─────────────►│  NAS AuthenticationResponse
 │◄── DLInfoTransfer ────────────│  NAS SecurityModeCommand
 │── ULInfoTransfer ─────────────►│  NAS SecurityModeComplete
 │◄── DLInfoTransfer ────────────│  NAS RegistrationAccept
 │                                │
 │  UE is now RRC_CONNECTED,      │
 │  RM_REGISTERED, CM_CONNECTED   │
```

### Measurement config flow

```
FakeGnb                              UE
 │── RRCReconfiguration ──────────►│  (carries measConfig with A2/A3/A5)
 │                                  │
 │  [OOB measurement injection]     │  (MeasurementInjector sends JSON)
 │                                  │
 │◄── MeasurementReport (UL_DCCH) ─│  (when event condition met)
```

---

## UE configuration

The test UE config ([`configs/test-ue.yaml`](configs/test-ue.yaml))
uses these credentials:

| Parameter | Value |
|---|---|
| SUPI | `imsi-286010000000001` |
| MCC / MNC | 286 / 93 |
| Key (K) | `465B5CE8B199B49FAA5F0A2EE238A6BC` |
| OP | `E8ED289DEBA952E4283B54E88E6183CA` |
| OP type | OP (not OPc) |
| OOB meas port | UDP 7200 |

These match the default UERANSIM test credentials.  No real 5G core
is required — the fake gNB handles all NAS messages.

---

## Measurement injection

The `MeasurementInjector` sends JSON over UDP to port 7200:

```python
from harness.meas_injector import MeasurementInjector, CellMeas

inj = MeasurementInjector(port=7200)
inj.set_cell(cell_id=1, rsrp=-85)   # Serving cell
inj.set_cell(cell_id=2, rsrp=-75)   # Neighbour
inj.send()                           # One-shot
inj.send_repeatedly(interval_s=0.5, duration_s=5)  # Sustained
inj.close()
```

JSON format:
```json
{
  "measurements": [
    {"cellId": 1, "rsrp": -85},
    {"cellId": 2, "rsrp": -75}
  ]
}
```

RSRP values are in dBm.  The UERANSIM UE converts them to the
3GPP-encoded 0–127 range internally (`value = rsrp_dBm + 156`).

---

## Extending the harness

### Adding a new measurement event test

1. Add a report config dict in the test with the event type:
   ```python
   report_configs=[{
       "id": 1, "event": "a3",
       "a3Offset": 6,   # dB offset
       "hysteresis": 2,
       "timeToTrigger": 640,  # ms
       "maxReportCells": 4,
   }]
   ```
2. Inject appropriate serving and neighbour RSRP values.
3. Use `fake_gnb.wait_for_measurement_report()`.

### Adding a new NAS message

Edit `harness/nas_builder.py` and add a `build_<message_name>()` function
following the TLV encoding patterns in TS 24.501.

### Adding a new RRC message

Edit `harness/rrc_builder.py`.  If `asn1tools` is available, add an
encoder using the compiled schema.  Otherwise, add a fallback pre-computed
byte constant.

---

## Troubleshooting

| Problem | Solution |
|---|---|
| Tests skip with "nr-ue binary not found" | Run `make` in the project root to build `build/nr-ue` |
| Port 4997 / 7200 in use | Kill other UERANSIM instances: `pkill nr-ue; pkill nr-gnb` |
| `asn1tools` compilation slow | First run compiles the ASN.1 schema (~30s).  Subsequent runs use cached schema. |
| TUN errors | Tests are configured without PDU sessions to avoid TUN.  If you add session tests, run with `sudo`. |
| UE doesn't detect cell | Ensure `gnbSearchList: ["127.0.0.1"]` in the test config |

---

## Protocol references

- **RLS Protocol**: UERANSIM link-simulation protocol (`src/lib/rls/`)
- **NR RRC**: 3GPP TS 38.331 v15.6.0 (`tools/rrc-15.6.0.asn1`)
- **5G NAS**: 3GPP TS 24.501
- **Milenage**: 3GPP TS 35.206
- **5G-AKA**: 3GPP TS 33.501
- **NIA2 / NEA2**: 3GPP TS 33.401 (EIA2 / EEA2, AES-based)
- **OOB measurement provider**: UERANSIM custom extension (`src/ue/rls/measurement.cpp`)
