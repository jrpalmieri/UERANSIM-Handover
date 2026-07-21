# UE Call Flow Map — NTS Task Method Invocation Reference

This document maps out how methods and functions are called by each NTS (Network Task Service) task within the UE (User Equipment) of UERANSIM. It reveals the 5G control signaling functions supported by the UE and how they are implemented in the NTS framework.

---

## Table of Contents

0. [Modifications from Upstream UERANSIM](#0-modifications-from-upstream-ueransim)
1. [UE Architecture Overview](#1-ue-architecture-overview)
2. [NTS Tasks and Message Flow](#2-nts-tasks-and-message-flow)
3. [RRC Task (`UeRrcTask`)](#3-rrc-task-uerectask)
   - 3.1 [Message Dispatch (onLoop)](#31-message-dispatch-onloop)
   - 3.2 [RRC Connection Setup / Release](#32-rrc-connection-setup--release)
   - 3.3 [Cell Selection (Idle Mode)](#33-cell-selection-idle-mode)
   - 3.4 [Paging](#34-paging)
   - 3.5 [NAS Transport (UL/DL)](#35-nas-transport-uldl)
   - 3.6 [RRC Reconfiguration & MeasConfig](#36-rrc-reconfiguration--measconfig)
   - 3.7 [Measurement Framework](#37-measurement-framework)
   - 3.8 [Handover Execution](#38-handover-execution)
   - 3.9 [Radio Link Failure](#39-radio-link-failure)
   - 3.10 [Access Control (UAC)](#310-access-control-uac)
   - 3.11 [System Information (MIB/SIB1)](#311-system-information-mibsib1)
   - 3.12 [SIB19 Reception (NTN Configuration)](#312-sib19-reception-ntn-configuration)
   - 3.13 [Position Framework (D1 Events)](#313-position-framework-d1-events)
   - 3.14 [Conditional Handover (CHO)](#314-conditional-handover-cho)
4. [NAS Task (`NasTask`)](#4-nas-task-nastask)
   - 4.1 [Message Dispatch (onLoop)](#41-message-dispatch-onloop)
   - 4.2 [NAS MM — Registration](#42-nas-mm--registration)
   - 4.3 [NAS MM — Deregistration](#43-nas-mm--deregistration)
   - 4.4 [NAS MM — Service Request](#44-nas-mm--service-request)
   - 4.5 [NAS MM — Authentication](#45-nas-mm--authentication)
   - 4.6 [NAS MM — Security Mode](#46-nas-mm--security-mode)
   - 4.7 [NAS MM — Paging Response](#47-nas-mm--paging-response)
   - 4.8 [NAS SM — PDU Session Establishment](#48-nas-sm--pdu-session-establishment)
   - 4.9 [NAS SM — PDU Session Release](#49-nas-sm--pdu-session-release)
5. [RLS Task (`UeRlsTask`)](#5-rls-task-uerlstask)
   - 5.1 [Message Dispatch (onLoop)](#51-message-dispatch-onloop)
6. [APP Task (`UeAppTask`)](#6-app-task-ueapptask)
   - 6.1 [Message Dispatch (onLoop)](#61-message-dispatch-onloop)
7. [Inter-Task Message Type Summary](#7-inter-task-message-type-summary)
8. [End-to-End Signaling Flows](#8-end-to-end-signaling-flows)

---

## 0. Modifications from Upstream UERANSIM

This fork adds **handover execution**, **measurement reporting**, and **out-of-band (OOB) measurement injection** capabilities to the UE that are not present in the upstream [aligungr/UERANSIM](https://github.com/aligungr/UERANSIM) `master` branch. The upstream RRC task had an empty `RRC_CONNECTED` branch in `performCycle()` and no support for `RRCReconfiguration`, measurement events, or handover.

### New source files

| File | Purpose |
|------|---------|
| `src/ue/rrc/measurement.hpp` | Measurement types: `EMeasEvent` (A2/A3/A5), `UeMeasConfig`, `UeReportConfig`, `UeMeasObject`, `UeMeasId`, `MeasIdState`, `CellMeasurement`, `MeasSourceConfig`, `EMeasSourceType`; CHO types: `EChoEventType` (T1/A2/A3/A5/D1/D1_SIB19), `ChoCondition`, `ChoCandidate` |
| `src/ue/rrc/measurement.cpp` | Measurement evaluation engine: `evaluateMeasurements()`, `collectMeasurements()`, `sendMeasurementReport()`, `applyMeasConfig()`, `resetMeasurements()`, A2/A3/A5 event condition checks, RSRP-to-ASN.1 conversion |
| `src/ue/rrc/meas_provider.hpp` | `MeasurementProvider` class — OOB measurement injection interface |
| `src/ue/rrc/meas_provider.cpp` | OOB provider implementation: UDP listener (`runUdp`), Unix datagram socket listener (`runUnixSocket`), file poller (`runFilePoller`), JSON parsing (`parseMeasurements`) |
| `src/ue/rrc/reconfig.cpp` | `receiveRrcReconfiguration()` — handles incoming RRCReconfiguration: ASN.1 MeasConfig parsing (`parseMeasConfig`), ReconfigurationWithSync detection for handover, dedicated NAS message delivery, RRCReconfigurationComplete for normal reconfiguration |
| `src/ue/rrc/handover.cpp` | Handover execution (Phase 2+3): `performHandover()`, `findCellByPci()` (3 strategies), `suspendMeasurements()`, `resumeMeasurements()`, `refreshSecurityKeys()`, `handleT304Expiry()` |
| `src/ue/rrc/cho.cpp` | Conditional Handover (CHO) — Release 16/17: `parseConditionalReconfiguration()` (ASN.1 path), `handleChoConfiguration()` (binary DL_CHO path), `evaluateChoCandidates()`, `evaluateCondition{Raw,WithTTT}()`, `selectBestCandidate()` (priority + margin + RSRP), `executeChoCandidate()`, `cancelAllChoCandidates()`, `getUePosition()` |
| `src/ue/rrc/sib19.hpp` | SIB19-r17 NTN types: `EEphemerisType`, `SatPositionVelocity`, `SatOrbitalParameters`, `EphemerisInfo`, `TaInfo`, `NtnConfig`, `Sib19Info`, `extrapolateSatellitePosition()`, `isSib19EphemerisValid()` |
| `src/ue/rrc/sib19.cpp` | SIB19 binary protocol parser: `receiveSib19()` (104-byte little-endian PDU), JSON serialization for all SIB19 types |
| `src/ue/rrc/position.hpp` | UE position types & coordinate conversions: `GeoPosition`, `EcefPosition`, `UePosition`, WGS-84 constants, `geoToEcef()`, `ecefDistance()`, `elevationAngle()`, `computeNadir()` |
| `src/ue/rrc/position.cpp` | JSON serialization for `GeoPosition`, `EcefPosition`, `UePosition` |

### Modified source files

| File | Change summary |
|------|----------------|
| `src/ue/rrc/task.hpp` | Added measurement members (`m_measConfig`, `m_measProvider`), handover state (`m_handoverInProgress`, `m_hoTxId`, `m_hoTargetPci`, `m_measurementsSuspended`), CHO state (`m_choCandidates`), and method declarations for measurement/handover/reconfiguration/CHO/SIB19/position |
| `src/ue/rrc/task.cpp` | Added `TIMER_ID_T304` constant, `MeasurementProvider` lifecycle in `onStart()`/`onQuit()`, T304 expiry dispatch in `onLoop()`, explicit destructor |
| `src/ue/rrc/state.cpp` | Added `evaluateMeasurements()` and `evaluateChoCandidates()` calls inside the previously-empty `RRC_CONNECTED` branch of `performCycle()` |
| `src/ue/rrc/channel.cpp` | Added `rrcReconfiguration` case in `DL_DCCH` dispatch → `receiveRrcReconfiguration()`; added `DL_CHO` channel → `handleChoConfiguration()`; added `DL_SIB19` channel → `receiveSib19()` |
| `src/lib/rrc/rrc.hpp` | Added `DL_CHO` and `DL_SIB19` values to `RrcChannel` enum |
| `src/ue/rrc/sap.cpp` | Added Doxygen comments to `handleRlsSapMessage()` and `handleNasSapMessage()` |
| `src/ue/rrc/nas.cpp` | Added Doxygen comments to `deliverUplinkNas()` |
| `src/ue/types.hpp` | Added `MeasSourceConfig measSourceConfig` field to `UeConfig`; added `std::optional<UePosition> initialPosition` to `UeConfig`; added `Sib19Info sib19` to `UeCellDesc`; added `#include <ue/rrc/measurement.hpp>`, `#include <ue/rrc/position.hpp>`, `#include <ue/rrc/sib19.hpp>` |
| `src/ue.cpp` | Added YAML parsing for optional `measurementSource` config block (type: `udp`/`unix`/`file`/`none`), propagated `measSourceConfig` in `GetConfigByUE()` |

---

## 1. UE Architecture Overview

The UE is composed of four NTS tasks that run as independent threads, communicating via message passing. Each task has a message queue; producers create `NtsMessage` subclass instances and call `push()` on the target task.

```
┌──────────────────────────────────────────────────────────┐
│                     UserEquipment                        │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │
│  │ UeAppTask│  │ NasTask  │  │UeRrcTask │  │UeRlsTask │ │
│  │  (APP)   │  │ (NAS)    │  │  (RRC)   │  │  (RLS)   │ │
│  │          │  │ ┌──────┐ │  │          │  │ ┌──────┐  │ │
│  │ TunTask[]│  │ │NasMm │ │  │ MeasProv │  │ │CtlTask│ │ │
│  │          │  │ │NasSm │ │  │          │  │ │UdpTask│ │ │
│  │          │  │ │Usim  │ │  │          │  │ └──────┘  │ │
│  └──────────┘  │ └──────┘ │  └──────────┘  └──────────┘ │
│                └──────────┘                              │
│                                                          │
│                    TaskBase (shared context)              │
└──────────────────────────────────────────────────────────┘
```

**Source files:**
- Task creation & startup: `src/ue/ue.cpp`
- NTS message definitions: `src/ue/nts.hpp`
- UE types & state enums: `src/ue/types.hpp`

---

## 2. NTS Tasks and Message Flow

| Task | Class | Role |
|------|-------|------|
| **APP** | `UeAppTask` | CLI commands, TUN interface management, data plane bridge |
| **NAS** | `NasTask` | NAS-layer signaling: MM (registration, auth, security) and SM (PDU sessions) |
| **RRC** | `UeRrcTask` | RRC-layer signaling: cell selection, connection control, measurement, handover |
| **RLS** | `UeRlsTask` | Radio Link Simulation: UDP transport to/from gNB, signal management |

### Inter-task message flow directions

```
           ┌──────────┐
     ┌────>│  APP     │<────┐
     │     └──────────┘     │
     │       │     ▲        │
     │  TunToApp  NasToApp  │  StatusUpdate
     │  AppToNas            │  CliCommand
     ▼       ▼     │        │
   ┌──────────┐    │   ┌──────────┐
   │  NAS     │<───┘   │  TUN[]   │
   │ (MM+SM)  │        └──────────┘
   └──────────┘
     │     ▲
 NasToRrc  RrcToNas
     ▼     │
   ┌──────────┐
   │  RRC     │
   └──────────┘
     │     ▲
 RrcToRls  RlsToRrc
     ▼     │
   ┌──────────┐
   │  RLS     │──── UDP ──── gNB
   │(Ctl+Udp) │
   └──────────┘
     │     ▲
 NasToRls  RlsToNas    (user-plane data bypass)
     ▼     │
   ┌──────────┐
   │  NAS     │
   └──────────┘
```

---

## 3. RRC Task (`UeRrcTask`)

**Source:** `src/ue/rrc/task.cpp`, `src/ue/rrc/task.hpp`

### RRC Task Startup & Shutdown

```
UeRrcTask::onStart()                                  [task.cpp]
├── triggerCycle()                                     [state.cpp]
├── setTimer(TIMER_ID_MACHINE_CYCLE, 2500ms)
├── [if initialPosition configured]
│   └── Log UE position (lat, lon, alt, ECEF) for D1 events
└── [if measSourceConfig.type != NONE]
    ├── Create MeasurementProvider(measSourceConfig)   [meas_provider.cpp]
    ├── m_measProvider->start()
    │   └── Spawn background thread:
    │       ├── [UDP]       → runUdp()       — bind udpAddress:udpPort, recv JSON
    │       ├── [UNIX_SOCK] → runUnixSocket() — bind unixSocketPath, recv JSON
    │       └── [FILE]      → runFilePoller() — poll filePath every N ms
    └── Log "OOB measurement provider started"

UeRrcTask::onQuit()                                   [task.cpp]
└── [if m_measProvider] → m_measProvider->stop()
    └── m_running = false; join background thread
```

### 3.1 Message Dispatch (onLoop)

The RRC task's `onLoop()` receives messages and dispatches them:

```
UeRrcTask::onLoop()
├── NtsMessageType::UE_NAS_TO_RRC
│   └── handleNasSapMessage(NmUeNasToRrc)             [sap.cpp]
├── NtsMessageType::UE_RLS_TO_RRC
│   └── handleRlsSapMessage(NmUeRlsToRrc)             [sap.cpp]
├── NtsMessageType::UE_RRC_TO_RRC
│   └── TRIGGER_CYCLE → performCycle()                 [state.cpp]
└── NtsMessageType::TIMER_EXPIRED
    ├── TIMER_ID_MACHINE_CYCLE → performCycle()        [state.cpp]
    └── TIMER_ID_T304 → handleT304Expiry()             [handover.cpp]
```

**SAP handler detail — `handleRlsSapMessage()`** (from RLS):

```
handleRlsSapMessage(NmUeRlsToRrc)                     [sap.cpp]
├── SIGNAL_CHANGED → handleCellSignalChange()          [cells.cpp]
├── DOWNLINK_RRC_DELIVERY → handleDownlinkRrc()        [channel.cpp]
└── RADIO_LINK_FAILURE → handleRadioLinkFailure()      [failures.cpp]
```

**SAP handler detail — `handleNasSapMessage()`** (from NAS):

```
handleNasSapMessage(NmUeNasToRrc)                      [sap.cpp]
├── UPLINK_NAS_DELIVERY → deliverUplinkNas()           [nas.cpp]
├── LOCAL_RELEASE_CONNECTION
│   ├── switchState(RRC_IDLE)                          [state.cpp]
│   ├── push NmUeRrcToRls(RESET_STI) → RLS task
│   └── push NmUeRrcToNas(RRC_CONNECTION_RELEASE) → NAS task
├── RRC_NOTIFY → triggerCycle()                        [state.cpp]
└── PERFORM_UAC → performUac()                         [access.cpp]
```

**State cycle — `performCycle()`:**

```
performCycle()                                         [state.cpp]
├── [RRC_CONNECTED] → evaluateMeasurements()           [measurement.cpp]
│                   → evaluateChoCandidates()           [cho.cpp]
├── [RRC_IDLE]      → performCellSelection()           [idle.cpp]
└── [RRC_INACTIVE]  → performCellSelection()           [idle.cpp]
```

---

### 3.2 RRC Connection Setup / Release

**Connection establishment (UE-initiated):**

```
deliverUplinkNas(pduId, nasPdu)                        [nas.cpp]
├── [RRC_IDLE] → startConnectionEstablishment(nasPdu)  [connection.cpp]
│   ├── ConstructSetupRequest(initialUeId, cause)
│   ├── sendRrcMessage(cellId, UL_CCCH_Message)        [channel.cpp]
│   │   └── push NmUeRrcToRls(RRC_PDU_DELIVERY) → RLS task
│   └── [on failure] handleEstablishmentFailure()      [connection.cpp]
│       └── push NmUeRrcToNas(RRC_ESTABLISHMENT_FAILURE) → NAS task
│
└── [RRC_CONNECTED] → build ULInformationTransfer
    └── sendRrcMessage(UL_DCCH_Message)                [channel.cpp]
        └── push NmUeRrcToRls(RRC_PDU_DELIVERY) → RLS task
```

**Receiving RRC Setup from gNB:**

```
handleDownlinkRrc(cellId, DL_CCCH, pdu)                [channel.cpp]
└── receiveRrcMessage(cellId, DL_CCCH_Message)
    └── rrcSetup → receiveRrcSetup(cellId, msg)        [connection.cpp]
        ├── Build RRCSetupComplete (with initial NAS PDU)
        ├── sendRrcMessage(UL_DCCH_Message)            [channel.cpp]
        ├── switchState(RRC_CONNECTED)                 [state.cpp]
        └── push NmUeRrcToNas(RRC_CONNECTION_SETUP) → NAS task
```

**Receiving RRC Reject:**

```
receiveRrcReject(cellId, msg)                          [connection.cpp]
└── handleEstablishmentFailure()
    └── push NmUeRrcToNas(RRC_ESTABLISHMENT_FAILURE) → NAS task
```

**Receiving RRC Release:**

```
receiveRrcRelease(msg)                                 [connection.cpp]
├── m_state = RRC_IDLE
└── push NmUeRrcToNas(RRC_CONNECTION_RELEASE) → NAS task
```

---

### 3.3 Cell Selection (Idle Mode)

```
performCellSelection()                                 [idle.cpp]
├── lookForSuitableCell(cellInfo, report)
│   ├── Filter: hasSib1, hasMib, PLMN match, not barred/reserved, TAI not forbidden
│   └── Sort by signal strength (dbm)
├── [if not found] lookForAcceptableCell(cellInfo, report)
│   ├── Filter: hasSib1, hasMib, not barred/reserved, TAI not forbidden
│   └── Sort by signal strength, then PLMN priority (stable sort)
├── Update shCtx.currentCell
├── [cell changed] push NmUeRrcToRls(ASSIGN_CURRENT_CELL) → RLS task
└── [cell changed] push NmUeRrcToNas(ACTIVE_CELL_CHANGED) → NAS task
```

---

### 3.4 Paging

```
handleDownlinkRrc(cellId, PCCH, pdu)                   [channel.cpp]
└── receiveRrcMessage(PCCH_Message)
    └── paging → receivePaging(msg)                    [handler.cpp]
        ├── Parse PagingRecordList → extract 5G-S-TMSI
        └── push NmUeRrcToNas(PAGING, tmsiIds) → NAS task
```

---

### 3.5 NAS Transport (UL/DL)

**Uplink NAS transport (NAS → RRC → RLS → gNB):**

```
deliverUplinkNas(pduId, nasPdu)                        [nas.cpp]
├── [RRC_IDLE] → startConnectionEstablishment()        [connection.cpp]
│   (triggers RRC Setup procedure, carries NAS PDU)
└── [RRC_CONNECTED] → build ULInformationTransfer
    └── sendRrcMessage(UL_DCCH)                        [channel.cpp]
        └── push NmUeRrcToRls(RRC_PDU_DELIVERY) → RLS task
```

**Downlink NAS transport (gNB → RLS → RRC → NAS):**

```
handleDownlinkRrc(cellId, DL_DCCH, pdu)                [channel.cpp]
└── receiveRrcMessage(DL_DCCH_Message)
    └── dlInformationTransfer
        → receiveDownlinkInformationTransfer(msg)      [nas.cpp]
            └── push NmUeRrcToNas(NAS_DELIVERY, nasPdu) → NAS task
```

---

### 3.6 RRC Reconfiguration & MeasConfig

> **Added in this fork.** Upstream UERANSIM did not handle
> `RRCReconfiguration` on the DL_DCCH channel. The `channel.cpp` dispatch
> was extended to route `rrcReconfiguration` to `receiveRrcReconfiguration()`.

```
handleDownlinkRrc(cellId, DL_DCCH, pdu)                [channel.cpp]
└── receiveRrcMessage(DL_DCCH_Message)
    └── rrcReconfiguration
        → receiveRrcReconfiguration(msg)               [reconfig.cpp]
            ├── [measConfig present] parseMeasConfig()
            │   ├── Parse MeasObjects (NR frequencies)
            │   ├── Parse ReportConfigs (A2/A3/A5 events)
            │   └── Parse MeasIds (bind object + report)
            │   └── applyMeasConfig(cfg)               [measurement.cpp]
            │
            ├── [v1530 dedicatedNAS_MessageList]
            │   └── for each NAS PDU:
            │       push NmUeRrcToNas(NAS_DELIVERY) → NAS task
            │
            ├── [v1530 masterCellGroup → ReconfigurationWithSync]
            │   └── isHandover = true
            │       → performHandover(txId, PCI, C-RNTI, t304) [handover.cpp]
            │
            └── [normal reconfiguration]
                ├── Build RRCReconfigurationComplete
                └── sendRrcMessage(UL_DCCH)            [channel.cpp]
```

---

### 3.7 Measurement Framework

> **Added in this fork.** Upstream UERANSIM has no measurement evaluation —
> the `RRC_CONNECTED` branch of `performCycle()` was empty.

**Periodic evaluation (during `performCycle()` in RRC_CONNECTED):**

```
evaluateMeasurements()                                 [measurement.cpp]
├── [skip if measurements suspended or no measIds]
├── collectMeasurements()
│   ├── Populate from RLS-simulated dBm (m_cellDesc[].dbm)
│   └── Overlay with OOB provider data (MeasurementProvider)
│       └── m_measProvider->getLatestMeasurements()
│           └── resolveCellId(cm) — match by cellId or NCI
├── getServingCellRsrp(allMeas)
├── For each measId:
│   ├── Look up reportConfig (A2 / A3 / A5)
│   ├── Evaluate event condition:
│   │   ├── A2: evaluateA2(servingRsrp, threshold, hyst)
│   │   │   "Serving cell becomes worse than threshold"
│   │   ├── A3: evaluateA3_cell(serving, neighbor, offset, hyst)
│   │   │   "Neighbor becomes offset better than serving"
│   │   └── A5: evaluateA5_serving() + evaluateA5_neighbor()
│   │       "Serving worse than T1 AND neighbor better than T2"
│   ├── Time-to-trigger check (enteringTimestamp + timeToTriggerMs)
│   └── [triggered] sendMeasurementReport()
│       ├── Build MeasurementReport ASN.1 structure
│       │   ├── Serving cell: measResultServingMOList (RSRP)
│       │   └── Neighbor cells: measResultListNR (top N by RSRP)
│       └── sendRrcMessage(UL_DCCH)                    [channel.cpp]
│           └── push NmUeRrcToRls(RRC_PDU_DELIVERY) → RLS task
```

**Measurement config reset (on connection release / radio failure):**

```
resetMeasurements()                                    [measurement.cpp]
└── m_measConfig = {}  (clear all objects, reports, measIds, states)
```

> `resetMeasurements()` is available for use on RRC release or radio link
> failure to clear stale measurement configuration.

---

### 3.8 Handover Execution

> **Added in this fork (Phase 2+3).** Upstream UERANSIM has no handover
> support — `RRCReconfiguration` messages were not handled.

```
performHandover(txId, targetPCI, newCRNTI, t304Ms, hasRachConfig) [handover.cpp]
├── suspendMeasurements()
│   └── m_measurementsSuspended = true
├── m_handoverInProgress = true
├── setTimer(TIMER_ID_T304, t304Ms)
├── refreshSecurityKeys()     (KgNB* derivation — simulated)
├── findCellByPci(physCellId)
│   ├── Strategy 1: direct PCI → cellId match
│   ├── Strategy 2: NCI lower bits match
│   └── Strategy 3: strongest non-serving detected cell
├── [target not found] → declareRadioLinkFailure()
├── Save previousCell from shCtx.currentCell
├── MAC reset (simulated)
├── Switch serving cell:
│   ├── Update shCtx.currentCell
│   └── push NmUeRrcToRls(ASSIGN_CURRENT_CELL) → RLS task
├── RACH towards target cell (logged, not simulated in detail)
├── Build RRCReconfigurationComplete
│   └── sendRrcMessage(UL_DCCH)                        [channel.cpp]
├── m_handoverInProgress = false
├── resumeMeasurements()
│   └── Reset all MeasIdState timestamps + reported flags
└── push NmUeRrcToNas(ACTIVE_CELL_CHANGED) → NAS task

handleT304Expiry()                                     [handover.cpp]
├── [if handoverInProgress]
│   ├── m_handoverInProgress = false
│   ├── resumeMeasurements()
│   └── declareRadioLinkFailure(SIGNAL_LOST_TO_CONNECTED_CELL)
```

---

### 3.9 Radio Link Failure

```
handleRadioLinkFailure(cause)                          [failures.cpp]
├── m_state = RRC_IDLE
└── push NmUeRrcToNas(RADIO_LINK_FAILURE) → NAS task

declareRadioLinkFailure(cause)                         [failures.cpp]
└── handleRadioLinkFailure(cause)
```

---

### 3.10 Access Control (UAC)

```
performUac(uacCtl)                                     [access.cpp]
├── Read establishment cause from input
├── Check cell barring (MIB/SIB1)
├── Check Access Identity barring (AI 1,2,11-15)
└── Notify result (ALLOWED / BARRED) via LightSync
```

---

### 3.11 System Information (MIB/SIB1)

```
handleDownlinkRrc(cellId, BCCH_BCH, pdu)               [channel.cpp]
└── receiveRrcMessage(cellId, BCCH_BCH_Message)
    └── receiveMib(cellId, msg)                        [sysinfo.cpp]
        ├── Parse cellBarred, intraFreqReselection
        ├── desc.mib.hasMib = true
        └── updateAvailablePlmns()                     [cells.cpp]
            └── push NmUeRrcToNas(NAS_NOTIFY) → NAS task

handleDownlinkRrc(cellId, BCCH_DL_SCH, pdu)            [channel.cpp]
└── receiveRrcMessage(cellId, BCCH_DL_SCH_Message)
    └── receiveSib1(cellId, msg)                       [sysinfo.cpp]
        ├── Parse NCI, TAC, PLMN, cellReserved, UAC barring
        ├── desc.sib1.hasSib1 = true
        └── updateAvailablePlmns()                     [cells.cpp]
```

**Cell signal change (from RLS → RRC):**

```
handleCellSignalChange(cellId, dbm)                    [cells.cpp]
├── [new cell detected] → notifyCellDetected(cellId, dbm)
│   ├── Create cellDesc entry
│   └── updateAvailablePlmns()
│       └── push NmUeRrcToNas(NAS_NOTIFY) → NAS task
│
└── [signal lost < -120 dBm] → notifyCellLost(cellId)
    ├── Clear shCtx.currentCell if active
    ├── Erase cellDesc entry
    ├── [was active + RRC_CONNECTED] → declareRadioLinkFailure()
    ├── [was active + RRC_IDLE] → push NmUeRrcToNas(ACTIVE_CELL_CHANGED)
    └── updateAvailablePlmns()
```

---

### 3.12 SIB19 Reception (NTN Configuration)

> **Added in this fork.** SIB19 carries Non-Terrestrial Network (NTN)
> configuration per 3GPP Release 17. Because UERANSIM's ASN.1 library is
> Release 15 and has no native SIB19 type, this fork uses a custom binary
> channel (`DL_SIB19`) and a 104-byte little-endian PDU format.

**Channel dispatch:**

```
handleDownlinkRrc(cellId, DL_SIB19, pdu)               [channel.cpp]
└── receiveSib19(cellId, pdu)                          [sib19.cpp]
```

**SIB19 binary PDU layout (104 bytes, little-endian):**

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | `ephemerisType` (0 = POSITION_VELOCITY, 1 = ORBITAL_PARAMETERS) |
| 4–51 | 48 | Ephemeris block: `SatPositionVelocity` (6 doubles: posX/Y/Z + velX/Y/Z) or `SatOrbitalParameters` (6 doubles: semiMajorAxis, eccentricity, inclination, omega, bigOmega, meanAnomaly) |
| 52 | 8 | `epochTime` (int64, ms since sim start) |
| 60 | 8 | `kOffset` (double) |
| 68 | 8 | `taCommon` (double, TA in ms) |
| 76 | 8 | `distanceThresh` (double, meters; ≤ 0 = absent) |
| 84 | 4 | `polarization` (int32: 0=RHCP, 1=LHCP, 2=LINEAR) |
| 88 | 4 | `ulSyncValidityDuration` (int32, seconds: 5–900) |
| 92 | 8 | `cellSelectionMinRxLevel` (double, dBm) |
| 100 | 4 | `deriveSSB` (int32: 0/1) |

**Call flow — `receiveSib19()`:**

```
receiveSib19(cellId, pdu)                              [sib19.cpp]
├── Validate length (≥ 104 bytes)
├── Read ephemerisType → EEphemerisType
├── [POSITION_VELOCITY] Parse 6 doubles → SatPositionVelocity
├── [ORBITAL_PARAMETERS] Parse 6 doubles → SatOrbitalParameters
├── Read common fields: epochTime, kOffset, taCommon, distanceThresh,
│   polarization, ulSyncValidityDuration, cellSelectionMinRxLevel, deriveSSB
├── Populate Sib19Info:
│   ├── hasSib19 = true
│   ├── ntnConfig = { ephemerisInfo, epochTime, kOffset, taInfo, syncValidity }
│   ├── distanceThresh = (> 0 ? value : nullopt)
│   └── receivedTime = current sim time
└── Store → m_cellDesc[cellId].sib19
```

**Key SIB19 types** (defined in `sib19.hpp`):

```
Sib19Info
├── hasSib19: bool
├── ntnConfig: NtnConfig
│   ├── ephemerisInfo: EphemerisInfo
│   │   ├── type: EEphemerisType {POSITION_VELOCITY, ORBITAL_PARAMETERS}
│   │   ├── posVel: SatPositionVelocity {posX/Y/Z, velX/Y/Z}   [ECEF meters & m/s]
│   │   └── orbital: SatOrbitalParameters {semiMajorAxis, e, i, ω, Ω, M}
│   ├── epochTime: int64_t (ms)
│   ├── kOffset: double
│   ├── taInfo: TaInfo {taCommon: double ms}
│   └── ulSyncValidityDuration: ENtnUlSyncValidityDuration {S5..S900}
├── distanceThresh: optional<double> (meters, for D1 events)
└── receivedTime: int64_t (ms, when SIB19 was received)
```

**Satellite position extrapolation** (inline in `sib19.hpp`):

```
extrapolateSatellitePosition(posVel, dtSec, &x, &y, &z)
└── Linear extrapolation: x = posX + velX * dt,  y = posY + velY * dt,  z = posZ + velZ * dt
```

**SIB19 validity check** (inline in `sib19.hpp`):

```
isSib19EphemerisValid(sib19, relativeNow)
└── (relativeNow − receivedTime) / 1000.0 < ulSyncValidityDuration (seconds)
```

---

### 3.13 Position Framework (D1 Events)

> **Added in this fork.** The position framework provides WGS-84
> coordinate types and conversions needed by D1 distance-based CHO
> conditions. All functions are `inline` in `position.hpp`.

**Types:**

| Type | Fields | Description |
|------|--------|-------------|
| `GeoPosition` | `latitude`, `longitude`, `altitude` | WGS-84 geodetic (degrees, meters) |
| `EcefPosition` | `x`, `y`, `z` | Earth-Centered Earth-Fixed (meters) |
| `UePosition` | `geo`, `ecef` | Combined geodetic + ECEF representation |

**Conversions & computations** (inline, `position.hpp`):

```
geoToEcef(geo) → EcefPosition
├── N = A / sqrt(1 − E² sin²φ)
├── x = (N + alt) cosφ cosλ
├── y = (N + alt) cosφ sinλ
└── z = (N(1−E²) + alt) sinφ

ecefDistance(a, b) → double
└── Euclidean: sqrt( (ax−bx)² + (ay−by)² + (az−bz)² )

elevationAngle(geo, ueEcef, targetEcef) → double (degrees)
├── Compute local UP vector from geodetic → ECEF normal
├── Compute direction vector UE → target
└── elevation = 90° − acos(dot(up, dir))

computeNadir(satX, satY, satZ) → EcefPosition
├── Compute geodetic (lat, lon) from ECEF
└── Project back to surface: geoToEcef({lat, lon, alt=0})
```

**UE position retrieval** (`cho.cpp`):

```
getUePosition()                                        [cho.cpp]
├── [initialPosition configured] → return *m_base->config->initialPosition
└── [else] → return default (0, 0, 0) converted to ECEF
```

**Configuration** (in UE YAML + `src/ue.cpp`):

```yaml
position:
  latitude: 33.7756
  longitude: -84.3963
  altitude: 320.0
```

Parsed in `GetConfigByUE()` → `UeConfig::initialPosition` (`std::optional<UePosition>`).
ECEF coordinates are computed automatically via `geoToEcef()`.

---

### 3.14 Conditional Handover (CHO)

> **Added in this fork.** Implements Conditional Handover per 3GPP TS 38.331
> §5.3.5.8 (Release 16/17). The gNB pre-configures one or more CHO
> candidates, each with a condition group. Conditions within a candidate
> use AND logic; multiple candidates use OR logic. The first (or
> highest-priority) fully-satisfied candidate triggers handover execution.

**RrcChannel additions** (`src/lib/rrc/rrc.hpp`):

| Channel | Purpose |
|---------|---------|
| `DL_CHO` | Custom channel for injecting CHO candidates via binary protocol |
| `DL_SIB19` | Custom channel for SIB19 NTN configuration (Rel-17) |

**CHO event types** (`EChoEventType` in `measurement.hpp`):

| Event | Trigger Condition |
|-------|-------------------|
| `T1` | Timer expiry: fires after `t1DurationMs` elapsed |
| `A2` | Serving RSRP < threshold + hysteresis |
| `A3` | Neighbor RSRP > serving + offset − hysteresis |
| `A5` | Serving < threshold1 AND neighbor > threshold2 |
| `D1` | Distance from UE to static ECEF reference > threshold (meters) |
| `D1_SIB19` | Distance from UE to SIB19-derived satellite reference > threshold |

**CHO type hierarchy** (`measurement.hpp`):

```
ChoCandidate
├── candidateId: int               (condReconfigId)
├── targetPci: int                 (target cell PCI)
├── newCRNTI: int                  (C-RNTI assigned by target)
├── t304Ms: int                    (T304 supervision timer)
├── executionPriority: int         (lower = higher priority; INT_MAX = unset)
├── conditions: vector<ChoCondition>   ── AND logic within group
│   ├── eventType: EChoEventType
│   ├── [T1]  t1DurationMs
│   ├── [A2]  a2Threshold, hysteresis
│   ├── [A3]  a3Offset, a3Hysteresis
│   ├── [A5]  a5Threshold1, a5Threshold2, a5Hysteresis
│   ├── [D1]  d1RefX/Y/Z (ECEF), d1ThresholdM
│   ├── [D1_SIB19] d1sib19ThresholdM, d1sib19ElevationMinDeg, d1sib19UseNadir
│   ├── timeToTriggerMs, hysteresis (common)
│   └── Runtime: enteringTimestamp, t1StartTime, satisfied, d1sib19ResolvedThreshM
├── executed: bool
└── triggerMargin: double
```

#### 3.14.1 CHO Configuration — ASN.1 Path

```
receiveRrcReconfiguration(msg)                         [reconfig.cpp]
└── [ConditionalReconfiguration present]
    └── parseConditionalReconfiguration(condReconfig)  [cho.cpp]
        └── For each CondReconfigToAddMod in addModList:
            ├── Extract candidateId from condReconfigId
            ├── Parse condExecutionCond → MeasId list (AND logic):
            │   ├── Look up MeasId → ReportConfig in m_measConfig
            │   └── Map EMeasEvent → EChoEventType:
            │       ├── A2 → ChoCondition{A2, threshold, hyst, ttt}
            │       ├── A3 → ChoCondition{A3, offset, hyst, ttt}
            │       └── A5 → ChoCondition{A5, thresh1, thresh2, hyst, ttt}
            ├── [no conditions parsed] → T1 fallback (DEFAULT_T1_DURATION_MS)
            ├── Decode condRRCReconfig (UPER → RRCReconfiguration)
            │   └── Extract ReconfigurationWithSync:
            │       ├── targetPci (physCellId)
            │       ├── newCRNTI (newUE_Identity)
            │       └── t304Ms   (t304 enum → ms)
            └── Append to m_choCandidates
```

#### 3.14.2 CHO Configuration — Binary DL_CHO Path

> The DL_CHO binary path is used by the test harness to inject CHO
> candidates with arbitrary condition groups (including D1 and D1_SIB19)
> that cannot be expressed via the Rel-15 ASN.1 library.

```
handleDownlinkRrc(cellId, DL_CHO, pdu)                 [channel.cpp]
└── [isActiveCell] handleChoConfiguration(pdu)         [cho.cpp]
    ├── Read numCandidates (uint32, offset 0)
    └── Per candidate (variable size):
        ├── Read candidateId, targetPci, newCRNTI, t304Ms, executionPriority
        │   (5 × int32, 24-byte header)
        ├── Read numConditions (uint32)
        └── Per condition (56 bytes fixed):
            ├── Read eventType (int32: 0=T1, 1=A2, 2=A3, 3=A5, 4=D1, 5=D1_SIB19)
            ├── Read intParam1..3 (3 × int32), timeToTriggerMs, reserved
            ├── Read floatParam1..4 (4 × double)
            └── Map to ChoCondition:
                ├── T1:      t1DurationMs = intParam1
                ├── A2:      a2Threshold = ip1, hysteresis = ip2
                ├── A3:      a3Offset = ip1, a3Hysteresis = ip2
                ├── A5:      a5Threshold1 = ip1, thresh2 = ip2, hyst = ip3
                ├── D1:      d1Ref{X,Y,Z} = fp1..3, d1ThresholdM = fp4
                └── D1_SIB19: useNadir = (ip1 & 1), threshold = fp1, elevMin = fp2
```

#### 3.14.3 CHO Candidate Evaluation (Per-Cycle)

Called from `performCycle()` every machine cycle (2500 ms) when `RRC_CONNECTED`:

```
evaluateChoCandidates()                                [cho.cpp]
├── [skip if m_choCandidates empty or m_handoverInProgress]
├── collectMeasurements()                              [measurement.cpp]
├── getServingCellRsrp(allMeas)                        [measurement.cpp]
├── getUePosition()                                    [cho.cpp]
│
├── Phase 1: Evaluate conditions for all candidates
│   └── For each non-executed candidate:
│       ├── Resolve target cell RSRP from measurements
│       │   (match by PCI or NCI lower bits)
│       ├── For each condition in the candidate's group:
│       │   ├── [D1] Compute ecefDistance(uePos, refPoint)
│       │   ├── [D1_SIB19] Derive reference from SIB19:
│       │   │   ├── Get serving cell's Sib19Info
│       │   │   ├── Check isSib19EphemerisValid()
│       │   │   ├── extrapolateSatellitePosition(posVel, dt)
│       │   │   ├── [useNadir] ref = computeNadir(sat)
│       │   │   ├── [else]     ref = satellite ECEF position
│       │   │   ├── d1Dist = ecefDistance(uePos, ref)
│       │   │   └── Resolve threshold: config value, or SIB19 distanceThresh
│       │   └── evaluateConditionWithTTT(cond, ...)
│       │       └── evaluateConditionRaw(cond, ...)
│       │           ├── T1:  elapsed ≥ t1DurationMs → 1.0
│       │           ├── A2:  (threshold + hyst) − servingRsrp → margin
│       │           ├── A3:  targetRsrp − (serving + offset − hyst) → margin
│       │           ├── A5:  min(margin1, margin2) (both > 0 required)
│       │           ├── D1:  ueDistance − threshold → margin
│       │           └── D1_SIB19: ueDistance − resolvedThresh → margin
│       │       Then apply TTT check (except T1 which has built-in timer)
│       ├── [ALL conditions satisfied] → add to triggered list
│       └── [else] continue (keeps TTT timers ticking)
│
├── Phase 2: Select best candidate (selectBestCandidate)
│   └── Sort triggered candidates by:
│       1. executionPriority ASC (lowest value = highest priority)
│       2. triggerMargin DESC (greatest excess over threshold)
│       3. targetRsrp DESC (strongest neighbor signal)
│       4. config order ASC (earliest in list)
│
└── Execute winning candidate → executeChoCandidate()
```

#### 3.14.4 CHO Execution

```
executeChoCandidate(candidate)                         [cho.cpp]
├── candidate.executed = true
├── Cancel all other CHO candidates
│   └── Mark remaining as executed = true
│       (per TS 38.331 §5.3.5.8.6)
└── performHandover(txId=0, targetPci, newCRNTI, t304Ms, hasRachConfig=false)
    └── (see §3.8 Handover Execution)
```

**Cancel all candidates:**

```
cancelAllChoCandidates()                               [cho.cpp]
└── m_choCandidates.clear()
```

---

## 4. NAS Task (`NasTask`)

**Source:** `src/ue/nas/task.cpp`, `src/ue/nas/task.hpp`

The NAS task owns three sub-components:
- **NasMm** — Mobility Management (registration, auth, security, idle mode)
- **NasSm** — Session Management (PDU session lifecycle)
- **Usim** — USIM storage (keys, security context)

### 4.1 Message Dispatch (onLoop)

```
NasTask::onLoop()
├── NtsMessageType::UE_RRC_TO_NAS
│   └── mm->handleRrcEvent(NmUeRrcToNas)              [mm/sap.cpp]
│
├── NtsMessageType::UE_NAS_TO_NAS
│   ├── PERFORM_MM_CYCLE → mm->handleNasEvent()        [mm/sap.cpp]
│   └── NAS_TIMER_EXPIRE
│       ├── [MM timer] → mm->handleNasEvent()          [mm/sap.cpp]
│       └── [SM timer] → sm->handleNasEvent()          [sm/sap.cpp]
│
├── NtsMessageType::UE_APP_TO_NAS
│   └── UPLINK_DATA_DELIVERY
│       → sm->handleUplinkDataRequest(psi, data)       [sm/sap.cpp]
│
├── NtsMessageType::UE_RLS_TO_NAS
│   └── DATA_PDU_DELIVERY
│       → sm->handleDownlinkDataRequest(psi, data)     [sm/sap.cpp]
│
└── NtsMessageType::TIMER_EXPIRED
    ├── NAS_TIMER_CYCLE → performTick()
    │   └── Check all NAS timers (t3346..t3585)
    │       └── [expired] push NmUeNasToNas(NAS_TIMER_EXPIRE)
    └── MM_CYCLE → mm->handleNasEvent(PERFORM_MM_CYCLE)
```

**NAS MM SAP — RRC event handler:**

```
NasMm::handleRrcEvent(NmUeRrcToNas)                   [mm/sap.cpp]
├── RRC_CONNECTION_SETUP → handleRrcConnectionSetup()  [mm/radio.cpp]
│   └── switchCmState(CM_CONNECTED)                    [mm/base.cpp]
│
├── NAS_DELIVERY → DecodeNasMessage → receiveNasMessage() [mm/messaging.cpp]
│
├── RRC_CONNECTION_RELEASE → handleRrcConnectionRelease() [mm/radio.cpp]
│   └── switchCmState(CM_IDLE)                         [mm/base.cpp]
│
├── RADIO_LINK_FAILURE → handleRadioLinkFailure()      [mm/radio.cpp]
│   ├── handleRrcConnectionRelease()
│   └── Switch to appropriate MM substate
│
├── PAGING → handlePaging(tmsiIds)                     [mm/radio.cpp]
│
├── NAS_NOTIFY → triggerMmCycle()                      [mm/base.cpp]
│
├── ACTIVE_CELL_CHANGED → handleActiveCellChange(prevTai) [mm/radio.cpp]
│
├── RRC_ESTABLISHMENT_FAILURE → handleRrcEstablishmentFailure() [mm/radio.cpp]
│
└── RRC_FALLBACK_INDICATION → handleRrcFallbackIndication() [mm/radio.cpp]
```

**NAS MM Cycle — `performMmCycle()`:**

```
NasMm::performMmCycle()                                [mm/base.cpp]
├── [MM_DEREGISTERED_PS substate] → select substate based on cell/USIM
│   ├── NORMAL_SERVICE, LIMITED_SERVICE, NO_SUPI, PLMN_SEARCH, ...
├── [MM_REGISTERED_PS substate] → select substate based on cell
│   ├── NORMAL_SERVICE, LIMITED_SERVICE, PLMN_SEARCH, ...
├── [PLMN_SEARCH / NO_CELL_AVAILABLE] → performPlmnSelection() [mm/radio.cpp]
├── [eCall handling]
├── invokeProcedures()                                 [mm/proc.cpp]
│   ├── sendDeregistration()     (if pending)
│   ├── sendInitialRegistration() (if pending)
│   ├── sendMobilityRegistration() (if pending)
│   ├── sendServiceRequest()     (if pending)
│   └── sm->establishRequiredSessions()
└── [TAI change check] → mobilityUpdatingRequired()
```

---

### 4.2 NAS MM — Registration

**Initial Registration:**

```
sendInitialRegistration(regCause)                      [mm/register.cpp]
├── Check RM state (must be RM_DEREGISTERED)
├── Check t3346 timer, priority, UAC
├── performUac() → push NmUeNasToRrc(PERFORM_UAC) → RRC task
├── Build RegistrationRequest
│   ├── registrationType = INITIAL_REGISTRATION
│   ├── mobileIdentity = getOrGeneratePreferredId()    [mm/identity.cpp]
│   │   └── getOrGenerateSuci() / generateSuci()
│   ├── ueSecurityCapability
│   ├── requestedNSSAI = makeRequestedNssai()          [mm/slice.cpp]
│   └── lastVisitedRegisteredTai
├── switchMmState(MM_REGISTERED_INITIATED_PS)          [mm/base.cpp]
└── sendNasMessage(RegistrationRequest)                [mm/messaging.cpp]
    └── push NmUeNasToRrc(UPLINK_NAS_DELIVERY) → RRC task
```

**Receive Registration Accept:**

```
receiveRegistrationAccept(msg)                         [mm/register.cpp]
├── receiveInitialRegistrationAccept(msg) or
│   receiveMobilityRegistrationAccept(msg)
│   ├── Store 5G-GUTI
│   ├── Store TAI list
│   ├── Store allowed/configured NSSAI
│   ├── Store network feature support
│   ├── switchMmState(MM_REGISTERED_PS)
│   ├── switchUState(U1_UPDATED)
│   └── Send RegistrationComplete (if needed)
│       └── sendNasMessage() → RRC → RLS → gNB
```

**Receive Registration Reject:**

```
receiveRegistrationReject(msg)                         [mm/register.cpp]
├── receiveInitialRegistrationReject(msg) or
│   receiveMobilityRegistrationReject(msg)
│   ├── Process reject cause
│   ├── Update reg attempt counter
│   └── switchMmState() per cause code
```

**Mobility Registration (TAU):**

```
sendMobilityRegistration(updateCause)                  [mm/register.cpp]
├── Check RM state (must be RM_REGISTERED)
├── Check timers, UAC
├── Build RegistrationRequest
│   ├── registrationType = MOBILITY_UPDATING
│   ├── pduSessionStatus, uplinkDataStatus
│   └── mobileIdentity, requestedNSSAI
├── switchMmState(MM_REGISTERED_INITIATED_PS)
└── sendNasMessage(RegistrationRequest)                [mm/messaging.cpp]
```

---

### 4.3 NAS MM — Deregistration

**UE-initiated deregistration:**

```
sendDeregistration(deregCause)                         [mm/dereg.cpp]
├── Check RM state (RM_REGISTERED), active cell
├── performUac()
├── Build DeRegistrationRequestUeOriginating
│   ├── switchOff / normalDeregistration
│   └── mobileIdentity
├── switchMmState(MM_DEREGISTERED_INITIATED_PS)
├── Start t3521
└── sendNasMessage()                                   [mm/messaging.cpp]
```

**Receive Deregistration Accept:**

```
receiveDeregistrationAccept(msg)                       [mm/dereg.cpp]
├── Stop t3521
├── performLocalDeregistration()
└── [switchOff] push NmUeNasToApp(PERFORM_SWITCH_OFF) → APP task
```

**Network-initiated deregistration:**

```
receiveDeregistrationRequest(msg)                      [mm/dereg.cpp]
├── Process cause, re-registration flag
├── performLocalDeregistration()
└── [reRegistration required] initialRegistrationRequired()
```

---

### 4.4 NAS MM — Service Request

```
sendServiceRequest(reqCause)                           [mm/service.cpp]
├── Check MM state, U-state, TAI list
├── performUac()
├── Build ServiceRequest
│   ├── serviceType based on cause
│   ├── uplinkDataStatus, pduSessionStatus
│   └── mobileIdentity (tmsi)
├── switchMmState(MM_SERVICE_REQUEST_INITIATED_PS)
└── sendNasMessage()                                   [mm/messaging.cpp]
```

**Receive Service Accept / Reject:**

```
receiveServiceAccept(msg)                              [mm/service.cpp]
├── Process PDU session reactivation result
├── switchMmState(MM_REGISTERED_PS)
└── sm->establishRequiredSessions() (if reactivation)

receiveServiceReject(msg)                              [mm/service.cpp]
├── Process reject cause
├── switchMmState() per cause
└── [cause=RESTRICTED_SERVICE_AREA] mobilityUpdatingRequired()
```

---

### 4.5 NAS MM — Authentication

```
receiveAuthenticationRequest(msg)                      [mm/auth.cpp]
├── [EAP-AKA'] → receiveAuthenticationRequestEap(msg)
│   ├── Extract EAP-AKA' challenge
│   ├── calculateMilenage(sqn, rand)
│   │   └── crypto::milenage::Calculate()
│   ├── validateAutn(rand, autn)
│   ├── Derive RES, CK', IK', kAusf
│   └── Send AuthenticationResponse (EAP)
│       └── sendNasMessage()
│
└── [5G-AKA] → receiveAuthenticationRequest5gAka(msg)
    ├── calculateMilenage(sqn, rand)
    ├── validateAutn(rand, autn)
    ├── Derive RES*, kAusf, kSeaf, kAmf
    └── Send AuthenticationResponse (RES*)
        └── sendNasMessage()

receiveAuthenticationReject(msg)                       [mm/auth.cpp]
├── Delete security context
├── Delete GUTI, TAI list, etc.
└── switchMmState(MM_DEREGISTERED_PS)

receiveAuthenticationResult(msg)                       [mm/auth.cpp]
├── Process EAP Success/Failure
└── receiveEapSuccessMessage() / receiveEapFailureMessage()
```

---

### 4.6 NAS MM — Security Mode

```
receiveSecurityModeCommand(msg)                        [mm/security.cpp]
├── Validate ngKSI
├── Find security context (current / non-current)
├── Select NAS security algorithms (integrity + ciphering)
│   ├── Verify UE capability matches
│   └── Derive kNasInt, kNasEnc
│       └── keys::DeriveNasKeys()                      [nas/keys.cpp]
├── Verify integrity of the SMC message
├── Activate new NAS security context
├── Build SecurityModeComplete
│   ├── [embeds IMEISV if requested]
│   └── [embeds complete Registration Request in NAS container]
└── sendNasMessage(SecurityModeComplete)               [mm/messaging.cpp]
    └── Encrypted & integrity-protected with new context
```

---

### 4.7 NAS MM — Paging Response

```
handlePaging(tmsiIds)                                  [mm/radio.cpp]
├── Match received 5G-S-TMSI against stored GUTI
├── [no match] → ignore
├── [match] Stop t3346
├── [CM_CONNECTED] → mobilityUpdatingRequired(PAGING_OR_NOTIFICATION)
└── [CM_IDLE] → serviceRequestRequired(IDLE_PAGING)
    └── (triggers Service Request procedure)
```

---

### 4.8 NAS SM — PDU Session Establishment

```
sendEstablishmentRequest(config)                       [sm/establishment.cpp]
├── Check RM state (must be RM_REGISTERED)
├── allocatePduSessionId(config)                       [sm/allocation.cpp]
├── allocateProcedureTransactionId()                   [sm/allocation.cpp]
├── Build PduSessionEstablishmentRequest
│   ├── pduSessionType = IPV4
│   ├── sscMode = SSC_MODE_1
│   ├── extendedProtocolConfigurationOptions (DNS etc.)
│   └── smCapability
├── Start procedure transaction timer (t3580)
└── sendSmMessage(psi, req)                            [sm/transport.cpp]
    ├── Build UlNasTransport (N1_SM_INFORMATION container)
    └── mm->deliverUlTransport(ulNas, hint)            [mm/transport.cpp]
        └── sendNasMessage()                           [mm/messaging.cpp]
            └── push NmUeNasToRrc(UPLINK_NAS_DELIVERY) → RRC task
```

**Receive PDU Session Establishment Accept:**

```
receiveEstablishmentAccept(msg)                        [sm/establishment.cpp]
├── freeProcedureTransactionId(pti)
├── Update pduSession state → ACTIVE
│   ├── Store QoS rules, AMBR, session type
│   ├── Store PDU address
│   └── Store QoS flow descriptions
└── push NmUeStatusUpdate(SESSION_ESTABLISHMENT) → APP task
    └── (APP task) receiveStatusUpdate()               [app/task.cpp]
        └── setupTunInterface(pduSession)
            ├── tun::TunAllocate()
            ├── tun::TunConfigure(ipAddress)
            └── Start TunTask(psi, fd)
```

**Receive PDU Session Establishment Reject:**

```
receiveEstablishmentReject(msg)                        [sm/establishment.cpp]
├── freeProcedureTransactionId(pti)
└── pduSession->psState = INACTIVE
```

---

### 4.9 NAS SM — PDU Session Release

**UE-initiated release:**

```
sendReleaseRequest(psi)                                [sm/release.cpp]
├── allocateProcedureTransactionId()
├── Build PduSessionReleaseRequest (cause=REGULAR_DEACTIVATION)
├── Start procedure transaction timer (t3582)
└── sendSmMessage(psi, req)                            [sm/transport.cpp]
```

**Network-initiated release:**

```
receiveReleaseCommand(msg)                             [sm/release.cpp]
├── localReleaseSession(psi)                           [sm/resource.cpp]
│   ├── pduSession->psState = INACTIVE
│   └── push NmUeStatusUpdate(SESSION_RELEASE) → APP task
│       └── (APP task) Stop & delete TunTask[psi]
├── freeProcedureTransactionId(pti)
├── Send PduSessionReleaseComplete
│   └── sendSmMessage()
└── [cause=REACTIVATION_REQUESTED]
    └── sendEstablishmentRequest() (re-establish session)
```

---

## 5. RLS Task (`UeRlsTask`)

**Source:** `src/ue/rls/task.cpp`, `src/ue/rls/task.hpp`

The RLS task bridges RRC/NAS with the simulated radio layer. It owns two sub-tasks:
- **RlsUdpTask** — sends/receives UDP packets to/from gNB
- **RlsControlTask** — manages cell association, heartbeats, PDU routing

### 5.1 Message Dispatch (onLoop)

```
UeRlsTask::onLoop()
├── NtsMessageType::UE_RLS_TO_RLS (from CtlTask/UdpTask)
│   ├── SIGNAL_CHANGED
│   │   └── push NmUeRlsToRrc(SIGNAL_CHANGED, cellId, dbm) → RRC task
│   ├── DOWNLINK_DATA
│   │   └── push NmUeRlsToNas(DATA_PDU_DELIVERY, psi, pdu) → NAS task
│   ├── DOWNLINK_RRC
│   │   └── push NmUeRlsToRrc(DOWNLINK_RRC_DELIVERY, cellId, channel, pdu) → RRC task
│   ├── RADIO_LINK_FAILURE
│   │   └── push NmUeRlsToRrc(RADIO_LINK_FAILURE, rlfCause) → RRC task
│   └── TRANSMISSION_FAILURE → log
│
├── NtsMessageType::UE_RRC_TO_RLS (from RRC task)
│   ├── ASSIGN_CURRENT_CELL
│   │   └── push NmUeRlsToRls(ASSIGN_CURRENT_CELL) → CtlTask
│   ├── RRC_PDU_DELIVERY
│   │   └── push NmUeRlsToRls(UPLINK_RRC, cellId, channel, pdu, pduId) → CtlTask
│   └── RESET_STI → m_shCtx->sti = random
│
└── NtsMessageType::UE_NAS_TO_RLS (from NAS task)
    └── DATA_PDU_DELIVERY
        └── push NmUeRlsToRls(UPLINK_DATA, psi, pdu) → CtlTask
```

---

## 6. APP Task (`UeAppTask`)

**Source:** `src/ue/app/task.cpp`, `src/ue/app/task.hpp`

### 6.1 Message Dispatch (onLoop)

```
UeAppTask::onLoop()
├── NtsMessageType::UE_TUN_TO_APP (from TUN interface)
│   ├── DATA_PDU_DELIVERY
│   │   └── push NmUeAppToNas(UPLINK_DATA_DELIVERY, psi, data) → NAS task
│   └── TUN_ERROR → log error
│
├── NtsMessageType::UE_NAS_TO_APP (from NAS task)
│   ├── PERFORM_SWITCH_OFF
│   │   └── setTimer(SWITCH_OFF_TIMER_ID) → delayed switchOff
│   └── DOWNLINK_DATA_DELIVERY
│       └── push NmAppToTun(DATA_PDU_DELIVERY, psi, data) → TunTask[psi]
│
├── NtsMessageType::UE_STATUS_UPDATE (from NAS SM)
│   └── receiveStatusUpdate()
│       ├── SESSION_ESTABLISHMENT → setupTunInterface()
│       │   ├── tun::TunAllocate() → create TUN device
│       │   ├── tun::TunConfigure() → assign IP, set MTU
│       │   └── Create & start TunTask
│       ├── SESSION_RELEASE → quit & delete TunTask[psi]
│       └── CM_STATE → update m_cmState
│
├── NtsMessageType::UE_CLI_COMMAND
│   └── UeCmdHandler::handleCmd()                      [app/cmd_handler.cpp]
│
└── NtsMessageType::TIMER_EXPIRED
    └── SWITCH_OFF_TIMER_ID → ueController->performSwitchOff()
```

---

## 7. Inter-Task Message Type Summary

| Message Struct | Direction | Key Payloads |
|---|---|---|
| `NmUeRrcToNas` | RRC → NAS | RRC_CONNECTION_SETUP, NAS_DELIVERY, RRC_CONNECTION_RELEASE, RADIO_LINK_FAILURE, PAGING, ACTIVE_CELL_CHANGED, RRC_ESTABLISHMENT_FAILURE, RRC_FALLBACK_INDICATION, NAS_NOTIFY |
| `NmUeNasToRrc` | NAS → RRC | UPLINK_NAS_DELIVERY, LOCAL_RELEASE_CONNECTION, RRC_NOTIFY, PERFORM_UAC |
| `NmUeRrcToRls` | RRC → RLS | ASSIGN_CURRENT_CELL, RRC_PDU_DELIVERY, RESET_STI |
| `NmUeRlsToRrc` | RLS → RRC | DOWNLINK_RRC_DELIVERY, SIGNAL_CHANGED, RADIO_LINK_FAILURE |
| `NmUeNasToRls` | NAS → RLS | DATA_PDU_DELIVERY (user-plane uplink) |
| `NmUeRlsToNas` | RLS → NAS | DATA_PDU_DELIVERY (user-plane downlink) |
| `NmUeNasToApp` | NAS → APP | PERFORM_SWITCH_OFF, DOWNLINK_DATA_DELIVERY |
| `NmUeAppToNas` | APP → NAS | UPLINK_DATA_DELIVERY |
| `NmUeStatusUpdate` | NAS → APP | SESSION_ESTABLISHMENT, SESSION_RELEASE, CM_STATE |
| `NmUeRrcToRrc` | RRC → RRC | TRIGGER_CYCLE |
| `NmUeNasToNas` | NAS → NAS | PERFORM_MM_CYCLE, NAS_TIMER_EXPIRE |
| `NmUeRlsToRls` | RLS → RLS | RECEIVE_RLS_MESSAGE, SIGNAL_CHANGED, UPLINK_DATA/RRC, DOWNLINK_DATA/RRC, RADIO_LINK_FAILURE, TRANSMISSION_FAILURE, ASSIGN_CURRENT_CELL |
| `NmAppToTun` | APP → TUN | DATA_PDU_DELIVERY |
| `NmUeTunToApp` | TUN → APP | DATA_PDU_DELIVERY, TUN_ERROR |
| `NmUeCliCommand` | CLI → APP | CLI command + callback address |

---

## 8. End-to-End Signaling Flows

### 8.1 Initial Registration (Power-on to Registered)

```
1. UeRrcTask::performCellSelection()
   └── lookForSuitableCell() / lookForAcceptableCell()
   └── NmUeRrcToNas(ACTIVE_CELL_CHANGED) ──────────────────────────→ NAS

2. NasMm::handleActiveCellChange()
   └── switchMmState(MM_DEREGISTERED_NORMAL_SERVICE)
   └── performMmCycle() → invokeProcedures()
   └── sendInitialRegistration()
   └── sendNasMessage(RegistrationRequest)
   └── NmUeNasToRrc(UPLINK_NAS_DELIVERY) ──────────────────────────→ RRC

3. UeRrcTask::deliverUplinkNas() [RRC_IDLE]
   └── startConnectionEstablishment()
   └── sendRrcMessage(RRCSetupRequest via UL_CCCH)
   └── NmUeRrcToRls(RRC_PDU_DELIVERY) ─────────────────────────────→ RLS → gNB

4. gNB → RLS → RRC: RRCSetup
   └── receiveRrcSetup()
   └── sendRrcMessage(RRCSetupComplete + NAS PDU)
   └── switchState(RRC_CONNECTED)
   └── NmUeRrcToNas(RRC_CONNECTION_SETUP) ──────────────────────────→ NAS
       └── NasMm::switchCmState(CM_CONNECTED)

5. gNB → RLS → RRC → NAS: AuthenticationRequest
   └── receiveAuthenticationRequest()
   └── calculateMilenage() → validateAutn()
   └── sendNasMessage(AuthenticationResponse) → RRC → RLS → gNB

6. gNB → NAS: SecurityModeCommand
   └── receiveSecurityModeCommand()
   └── Derive kNasInt, kNasEnc
   └── sendNasMessage(SecurityModeComplete) → RRC → RLS → gNB

7. gNB → NAS: RegistrationAccept
   └── receiveRegistrationAccept()
   └── Store GUTI, TAI list, NSSAI
   └── switchMmState(MM_REGISTERED_NORMAL_SERVICE)
   └── switchUState(U1_UPDATED)
```

### 8.2 PDU Session Establishment

```
1. NasMm::performMmCycle() [MM_REGISTERED_NORMAL_SERVICE]
   └── invokeProcedures()
   └── sm->establishRequiredSessions()
   └── sendEstablishmentRequest(config)

2. NasSm::sendEstablishmentRequest()
   └── sendSmMessage(PduSessionEstablishmentRequest)
   └── mm->deliverUlTransport(UlNasTransport)
   └── sendNasMessage() → RRC → RLS → gNB

3. gNB → NAS: PduSessionEstablishmentAccept (in DlNasTransport)
   └── NasMm::receiveDlNasTransport()
   └── sm->receiveSmMessage()
   └── receiveEstablishmentAccept()
   └── NmUeStatusUpdate(SESSION_ESTABLISHMENT) ────────────────────→ APP
       └── UeAppTask::setupTunInterface()
```

### 8.3 Handover (Measurement-triggered)

```
1. UeRrcTask::performCycle() [RRC_CONNECTED]
   └── evaluateMeasurements()
   └── [A3 event triggered] sendMeasurementReport()
   └── sendRrcMessage(MeasurementReport) → RLS → gNB

2. gNB decision → sends RRCReconfiguration (with ReconfigurationWithSync)

3. gNB → RLS → RRC: RRCReconfiguration
   └── receiveRrcReconfiguration()                     [reconfig.cpp]
       ├── parseMeasConfig() → applyMeasConfig()       (if present)
       └── performHandover(txId, PCI, C-RNTI, t304)   [handover.cpp]
           ├── suspendMeasurements()
           ├── setTimer(T304)
           ├── refreshSecurityKeys()
           ├── findCellByPci() → resolve target cell
           ├── Update shCtx.currentCell → target
           ├── NmUeRrcToRls(ASSIGN_CURRENT_CELL) ─────────────────→ RLS
           ├── sendRrcMessage(RRCReconfigurationComplete) → RLS → target gNB
           ├── resumeMeasurements()
           └── NmUeRrcToNas(ACTIVE_CELL_CHANGED) ─────────────────→ NAS
```

### 8.4 Paging & Service Request

```
1. gNB → RLS → RRC: Paging (PCCH)
   └── receivePaging()
   └── NmUeRrcToNas(PAGING, tmsiIds) ─────────────────────────────→ NAS

2. NasMm::handlePaging()
   └── Match TMSI → serviceRequestRequired(IDLE_PAGING)
   └── sendServiceRequest()
   └── sendNasMessage(ServiceRequest) → RRC → RLS → gNB
       (RRC establishes connection if in IDLE)

3. gNB → NAS: ServiceAccept
   └── receiveServiceAccept()
   └── switchMmState(MM_REGISTERED_PS)
```

### 8.5 Deregistration (UE-initiated switch-off)

```
1. CLI command → UeAppTask → NasMm
   └── deregistrationRequired(SWITCH_OFF)

2. NasMm::sendDeregistration()
   └── sendNasMessage(DeRegistrationRequest) → RRC → RLS → gNB

3. gNB → NAS: DeregistrationAccept
   └── receiveDeregistrationAccept()
   └── performLocalDeregistration()
   └── NmUeNasToApp(PERFORM_SWITCH_OFF) ──────────────────────────→ APP
       └── UeAppTask → delayed timer → ueController->performSwitchOff()
```

---

### 8.6 OOB Measurement Injection (Configuration)

The `measurementSource` block in the UE YAML config file configures the
OOB measurement provider. This is parsed at startup in `src/ue.cpp`:

```
ReadConfigFile()                                       [src/ue.cpp]
├── [measurementSource present]
│   ├── type="udp"  → measSourceConfig.type = UDP
│   │   ├── address (default "127.0.0.1")
│   │   └── port    (default 7200)
│   ├── type="unix" → measSourceConfig.type = UNIX_SOCK
│   │   └── path
│   ├── type="file" → measSourceConfig.type = FILE
│   │   ├── path
│   │   └── pollInterval (default 1000 ms)
│   └── type="none" → measSourceConfig.type = NONE
│
└── GetConfigByUE(ueIndex)                             [src/ue.cpp]
    └── c->measSourceConfig = g_refConfig->measSourceConfig
```

Expected JSON format from UDP/Unix/File sources:
```json
{
  "measurements": [
    { "cellId": 1, "rsrp": -85, "rsrq": -10, "sinr": 15 },
    { "cellId": 2, "rsrp": -95 },
    { "nci": 36,   "rsrp": -78, "rsrq": -8,  "sinr": 20 }
  ]
}
```

Fields: `cellId` (internal UERANSIM cell ID) or `nci` (NR Cell Identity
from SIB1) for cell matching, `rsrp` (dBm, mandatory), `rsrq`/`sinr`
(optional).

---

### 8.7 Conditional Handover (End-to-End)

> **Added in this fork.** This shows the complete lifecycle of a
> Conditional Handover from SIB19 reception through condition evaluation
> to handover execution.

**Phase A — SIB19 Provisioning (NTN scenarios):**

```
1. gNB → RLS → RRC: DL_SIB19 (104-byte binary PDU)
   └── handleDownlinkRrc(cellId, DL_SIB19, pdu)       [channel.cpp]
   └── receiveSib19(cellId, pdu)                       [sib19.cpp]
       ├── Parse ephemeris (position/velocity or Keplerian)
       ├── Parse NTN config (epoch, kOffset, TA, distance threshold, ...)
       └── Store → m_cellDesc[cellId].sib19
```

**Phase B — CHO Configuration (via DL_CHO from test harness):**

```
2. Test harness → gNB → RLS → RRC: DL_CHO (binary PDU)
   └── handleDownlinkRrc(cellId, DL_CHO, pdu)         [channel.cpp]
   └── handleChoConfiguration(pdu)                     [cho.cpp]
       ├── Parse N candidates, each with M conditions
       │   (T1, A2, A3, A5, D1, or D1_SIB19)
       └── Append to m_choCandidates
```

**Phase B' — CHO Configuration (via ASN.1 RRCReconfiguration):**

```
2'. gNB → RLS → RRC: RRCReconfiguration (with ConditionalReconfiguration)
    └── receiveRrcReconfiguration()                    [reconfig.cpp]
    └── parseConditionalReconfiguration()              [cho.cpp]
        ├── Parse condExecutionCond MeasIds → conditions
        ├── Decode nested condRRCReconfig → target cell params
        └── Append to m_choCandidates
```

**Phase C — Per-Cycle Evaluation (every 2500 ms while RRC_CONNECTED):**

```
3. performCycle()                                      [state.cpp]
   └── evaluateChoCandidates()                         [cho.cpp]
       ├── collectMeasurements() (RSRP from RLS + OOB overlay)
       ├── getUePosition() (from config or default)
       ├── For each candidate → evaluate all conditions (AND logic):
       │   ├── T1:       timer check (built-in, no TTT)
       │   ├── A2/A3/A5: RSRP comparison with TTT
       │   ├── D1:       ecefDistance(UE, staticRef) > threshold
       │   └── D1_SIB19: ecefDistance(UE, satRef) > threshold
       │       ├── extrapolateSatellitePosition() from SIB19
       │       ├── [useNadir] computeNadir() → ground projection
       │       └── Resolve threshold from config or SIB19 distanceThresh
       ├── Select best candidate (priority → margin → RSRP → order)
       └── executeChoCandidate()                       [cho.cpp]
```

**Phase D — Handover Execution:**

```
4. executeChoCandidate(winner)                         [cho.cpp]
   ├── Cancel all other CHO candidates
   └── performHandover(0, targetPci, newCRNTI, t304Ms) [handover.cpp]
       ├── suspendMeasurements()
       ├── setTimer(T304)
       ├── findCellByPci() → resolve target
       ├── Switch serving cell → NmUeRrcToRls(ASSIGN_CURRENT_CELL) → RLS
       ├── sendRrcMessage(RRCReconfigurationComplete) → RLS → target gNB
       ├── resumeMeasurements()
       └── NmUeRrcToNas(ACTIVE_CELL_CHANGED) ─────────────────────→ NAS
```

---

## Source File Index

| File | Content |
|------|---------|
| `src/ue/ue.cpp` | UE task creation, startup, shutdown |
| `src/ue/nts.hpp` | All NTS message struct definitions |
| `src/ue/types.hpp` | State enums, config, PDU session, keys |
| `src/ue/rrc/task.cpp` | RRC task: onLoop, dispatch |
| `src/ue/rrc/task.hpp` | RRC task: class declaration, all methods |
| `src/ue/rrc/sap.cpp` | RRC SAP: handle messages from RLS and NAS |
| `src/ue/rrc/state.cpp` | RRC state: triggerCycle, performCycle, switchState |
| `src/ue/rrc/channel.cpp` | RRC channel: encode/decode/send/receive all RRC channels |
| `src/ue/rrc/connection.cpp` | RRC connection: setup request, setup complete, reject, release |
| `src/ue/rrc/idle.cpp` | RRC idle: cell selection (suitable/acceptable) |
| `src/ue/rrc/cells.cpp` | RRC cells: signal change, detect, lost, PLMN update |
| `src/ue/rrc/sysinfo.cpp` | RRC sysinfo: receive MIB, receive SIB1 |
| `src/ue/rrc/handler.cpp` | RRC handler: paging |
| `src/ue/rrc/nas.cpp` | RRC NAS transport: UL/DL information transfer |
| `src/ue/rrc/access.cpp` | RRC access control: UAC |
| `src/ue/rrc/failures.cpp` | RRC failures: radio link failure |
| `src/ue/rrc/reconfig.cpp` | RRC reconfiguration: MeasConfig parsing, handover trigger |
| `src/ue/rrc/measurement.hpp` | Measurement types: events, configs, objects |
| `src/ue/rrc/measurement.cpp` | Measurement logic: evaluate A2/A3/A5, send report |
| `src/ue/rrc/handover.cpp` | Handover execution: cell switch, T304, RACH, complete |
| `src/ue/rrc/cho.cpp` | Conditional Handover: parse, evaluate, select, execute, cancel |
| `src/ue/rrc/sib19.hpp` | SIB19-r17 NTN types: ephemeris, NTN config, extrapolation |
| `src/ue/rrc/sib19.cpp` | SIB19 binary protocol parser, JSON serialization |
| `src/ue/rrc/position.hpp` | UE position types: GeoPosition, EcefPosition, WGS-84 conversions |
| `src/ue/rrc/position.cpp` | Position JSON serialization |
| `src/lib/rrc/rrc.hpp` | RRC channel enum (incl. `DL_CHO`, `DL_SIB19` additions) |
| `src/ue/rrc/meas_provider.hpp` | OOB measurement provider interface |
| `src/ue/rrc/meas_provider.cpp` | OOB measurement provider (UDP/Unix/File) |
| `src/ue/nas/task.cpp` | NAS task: onLoop, timer tick |
| `src/ue/nas/task.hpp` | NAS task: class with MM, SM, USIM |
| `src/ue/nas/mm/mm.hpp` | NAS MM: full class declaration, all methods |
| `src/ue/nas/mm/base.cpp` | NAS MM: init, performMmCycle, state switching |
| `src/ue/nas/mm/sap.cpp` | NAS MM: handleRrcEvent, handleNasEvent |
| `src/ue/nas/mm/proc.cpp` | NAS MM: procedure control, invokeProcedures |
| `src/ue/nas/mm/messaging.cpp` | NAS MM: sendNasMessage, receiveNasMessage, dispatch |
| `src/ue/nas/mm/transport.cpp` | NAS MM: UL/DL NAS transport (SM container) |
| `src/ue/nas/mm/register.cpp` | NAS MM: initial & mobility registration |
| `src/ue/nas/mm/dereg.cpp` | NAS MM: deregistration (UE & network) |
| `src/ue/nas/mm/service.cpp` | NAS MM: service request, accept, reject |
| `src/ue/nas/mm/auth.cpp` | NAS MM: authentication (5G-AKA, EAP-AKA') |
| `src/ue/nas/mm/security.cpp` | NAS MM: security mode command/complete |
| `src/ue/nas/mm/radio.cpp` | NAS MM: PLMN selection, cell change, RRC events |
| `src/ue/nas/mm/identity.cpp` | NAS MM: SUCI generation, identity handling |
| `src/ue/nas/mm/slice.cpp` | NAS MM: NSSAI request construction |
| `src/ue/nas/mm/config.cpp` | NAS MM: configuration update command |
| `src/ue/nas/mm/timer.cpp` | NAS MM: timer expiry handling |
| `src/ue/nas/sm/sm.hpp` | NAS SM: full class declaration, all methods |
| `src/ue/nas/sm/base.cpp` | NAS SM: init |
| `src/ue/nas/sm/sap.cpp` | NAS SM: event handler, data UL/DL |
| `src/ue/nas/sm/transport.cpp` | NAS SM: sendSmMessage, receiveSmMessage |
| `src/ue/nas/sm/establishment.cpp` | NAS SM: PDU session establishment |
| `src/ue/nas/sm/release.cpp` | NAS SM: PDU session release |
| `src/ue/nas/sm/allocation.cpp` | NAS SM: PSI/PTI allocation |
| `src/ue/nas/sm/resource.cpp` | NAS SM: local session release, status |
| `src/ue/nas/sm/timer.cpp` | NAS SM: transaction timer handling |
| `src/ue/rls/task.cpp` | RLS task: onLoop, message routing |
| `src/ue/rls/ctl_task.hpp` | RLS control sub-task |
| `src/ue/rls/udp_task.hpp` | RLS UDP sub-task |
| `src/ue/app/task.cpp` | APP task: onLoop, TUN setup, status |
| `src/ue/app/cmd_handler.cpp` | APP CLI command handler |
| `src/ue/tun/task.hpp` | TUN sub-task per PDU session |
| `src/ue.cpp` | UE main: YAML config parsing (incl. `measurementSource`, `position`), `GetConfigByUE()` |
