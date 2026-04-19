# UE Operations Reference

This document describes the operations that the UERANSIM **UE (User Equipment)** performs when communicating with a **gNB (gNodeB)**. For each operation it lists the **triggering event**, the **5G procedure context**, and the **functions** (with source file locations) that implement the behavior.

> **Scope:** This covers the UERANSIM-Handover fork of UERANSIM, which extends the upstream project with measurement event evaluation (A2/A3/A5), MeasurementReport generation, intra-5G-SA handover execution, Conditional Handover (CHO) and Non-Terrestrial Network (NTN) support.

> **Depth convention:** For each operation the *top-level handler* is shown, along with the key *sub-functions* it invokes (two levels). Lower-level utility calls (ASN.1 encode/decode wrappers, NTS push helpers) are described once in the Architecture section and not repeated.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [UE Startup](#2-ue-startup)
3. [Cell Selection (Idle Mode)](#3-cell-selection-idle-mode)
4. [RRC Connection Establishment](#4-rrc-connection-establishment)
5. [NAS Registration with the Network](#5-nas-registration-with-the-network)
6. [NAS Transport (Uplink and Downlink)](#6-nas-transport-uplink-and-downlink)
7. [RRC Reconfiguration — Normal (without Handover)](#7-rrc-reconfiguration--normal-without-handover)
8. [RRC Reconfiguration — Handover (ReconfigurationWithSync)](#8-rrc-reconfiguration--handover-reconfigurationwithsync)
9. [Handover Execution (performHandover)](#9-handover-execution-performhandover)
10. [Measurement Framework — Configuration](#10-measurement-framework--configuration)
11. [Measurement Framework — Collection](#11-measurement-framework--collection)
12. [Measurement Framework — Event Evaluation (A2, A3, A5)](#12-measurement-framework--event-evaluation-a2-a3-a5)
13. [MeasurementReport Generation and Transmission](#13-measurementreport-generation-and-transmission)
14. [Conditional Handover (CHO) — Configuration](#14-conditional-handover-cho--configuration)
15. [Conditional Handover (CHO) — Evaluation and Execution](#15-conditional-handover-cho--evaluation-and-execution)
16. [Handover Failure — T304 Expiry](#16-handover-failure--t304-expiry)
17. [Radio Link Failure](#17-radio-link-failure)
18. [RRC Connection Release](#18-rrc-connection-release)
19. [Paging](#19-paging)
20. [RRC State Machine Cycle](#20-rrc-state-machine-cycle)
21. [NAS Mobility Management Cycle](#21-nas-mobility-management-cycle)
22. [Source File Index](#22-source-file-index)

---

## 1. Architecture Overview

The UE is built as a set of cooperating **NTS (Non-blocking Task Signaling) tasks**, each running in its own thread. Inter-task communication uses typed NTS messages pushed to per-task queues.

```
┌──────────────┐
│  UeAppTask   │  CLI handling, TUN interface, uplink/downlink data
└──────┬───────┘
       │
┌──────┴───────┐
│   NasTask    │  NAS Mobility Management (NasMm) + Session Management (NasSm)
│  ├─ NasMm    │  Registration, authentication, security, mobility updates
│  ├─ NasSm    │  PDU session establishment, QoS
│  └─ Usim     │  USIM credential store
└──────┬───────┘
       │
┌──────┴───────┐                       ┌──────────── ──┐
│  UeRrcTask   │◄─────────────────────►│  UeRlsTask    │  (Radio Link Simulation)
│  (RRC / Uu)  │                       │  ├─ RlsCtlTask│
│              │                       │  └─ RlsUdpTask│
└──────────────┘                       └──────┬────── ─┘
                                              │
                                              |
                                              |      (RF simulated via UDP)
                                              |
                                       ┌──────┴───────┐
                                       │   gNB(s)     │
                                       └──────────────┘
```

Examples of how these components interact are provided below.

### Key NTS Message Types

| Message | Direction | Purposes |
|---------|-----------|----------|
| `NmUeRlsToRrc` | RLS → RRC | `SIGNAL_CHANGED`, `DOWNLINK_RRC_DELIVERY`, `RADIO_LINK_FAILURE` |
| `NmUeRrcToRls` | RRC → RLS | `ASSIGN_CURRENT_CELL`, `RRC_PDU_DELIVERY`, `RESET_STI` |
| `NmUeRrcToNas` | RRC → NAS | `RRC_CONNECTION_SETUP`, `RRC_CONNECTION_RELEASE`, `NAS_DELIVERY`, `ACTIVE_CELL_CHANGED`, `RRC_ESTABLISHMENT_FAILURE`, `RADIO_LINK_FAILURE`, `PAGING` |
| `NmUeNasToRrc` | NAS → RRC | `UPLINK_NAS_DELIVERY`, `LOCAL_RELEASE_CONNECTION`, `RRC_NOTIFY`, `PERFORM_UAC` |
| `NmUeRrcToRrc` | RRC → RRC | `TRIGGER_CYCLE` (self-message to perform state machine cycle) |
| `NmUeNasToNas` | NAS → NAS | `PERFORM_MM_CYCLE`, `NAS_TIMER_EXPIRE` |
| `NmUeAppToNas` | APP → NAS | `UPLINK_DATA_DELIVERY` (user-plane data from TUN) |
| `NmUeRlsToNas` | RLS → NAS | `DATA_PDU_DELIVERY` (downlink user-plane data) |

### Key RRC States

| State | Description |
|-------|-------------|
| `RRC_IDLE` | No active RRC connection. UE performs cell selection. |
| `RRC_CONNECTED` | Active RRC connection with the serving gNB. Measurements and handover are active. |
| `RRC_INACTIVE` | Connection suspended (not fully implemented in UERANSIM). |

### Key NAS States

| State Layer | States | Description |
|-------------|--------|-------------|
| **RM** (Registration Management) | `RM_DEREGISTERED`, `RM_REGISTERED` | Whether UE is registered with the network. |
| **CM** (Connection Management) | `CM_IDLE`, `CM_CONNECTED` | Whether a NAS signaling connection exists. |
| **MM** (Mobility Management) | `MM_NULL`, `MM_DEREGISTERED`, `MM_REGISTERED_INITIATED`, `MM_REGISTERED`, `MM_DEREGISTERED_INITIATED`, `MM_SERVICE_REQUEST_INITIATED` | Current MM procedure state. |

### Common Helper Functions

| Helper | File | Purpose |
|--------|------|---------|
| `sendRrcMessage(cellId, UL_CCCH)` | `src/ue/rrc/channel.cpp` | UPER-encode an RRC UL-CCCH PDU and push to RLS |
| `sendRrcMessage(UL_DCCH)` | `src/ue/rrc/channel.cpp` | UPER-encode an RRC UL-DCCH PDU and push to RLS (uses current serving cell) |
| `handleDownlinkRrc()` | `src/ue/rrc/channel.cpp` | Decode incoming RRC PDU by channel type and dispatch to handler |
| `rrc::encode::Decode<T>()` | `src/lib/rrc/encode.cpp` | UPER decode wrapper for RRC ASN.1 |
| `rrc::encode::EncodeS()` | `src/lib/rrc/encode.cpp` | UPER encode wrapper for RRC ASN.1 |
| `switchState()` | `src/ue/rrc/state.cpp` | RRC state transition with logging and listener notification |
| `switchMmState()` | `src/ue/nas/mm/base.cpp` | NAS MM state transition with logging |
| `triggerCycle()` | `src/ue/rrc/state.cpp` | Post a `TRIGGER_CYCLE` self-message to invoke `performCycle()` |

---

## 2. UE Startup

### Triggering Event
**User launches the `nr-ue` binary** with a YAML configuration file (e.g. `./nr-ue -c config/free5gc-ue.yaml`).

### Initialization
Before the UE can communicate with any gNB it must read its configuration, instantiate all internal NTS tasks, and start the simulated RF network connection to gNB(s).  If any gNBs are detected, it begins RRC_IDLE cell selection.

### Sequence of Operations

| Step | Action | Key Function(s) / File(s) |
|------|--------|-----------------|
| 1 | Parse command-line options and read YAML config | `ReadOptions()`, `ReadConfigYaml()` in `src/ue.cpp` |
| 2 | Create the `UeControllerTask` and start it | `main()` in `src/ue.cpp` |
| 3 | Instantiate `UserEquipment` – creates the task base | `UserEquipment::UserEquipment()` in `src/ue/ue.cpp` |
| 3a | — Create `UeAppTask` (application, CLI, TUN) | `UeAppTask()` in `src/ue/app/task.cpp` |
| 3b | — Create `NasTask` (housing `NasMm`, `NasSm`, `Usim`) | `NasTask::NasTask()` in `src/ue/nas/task.cpp` |
| 3c | — Create `UeRrcTask` (RRC layer) | `UeRrcTask::UeRrcTask()` in `src/ue/rrc/task.cpp` |
| 3d | — Create `UeRlsTask` (radio link simulation) | `UeRlsTask()` in `src/ue/rls/task.cpp` |
| 4 | Start all tasks: `nasTask→start()`, `rrcTask→start()`, `rlsTask→start()`, `appTask→start()` | `UserEquipment::start()` in `src/ue/ue.cpp` |
| 5 | **NasTask::onStart()**: initialize USIM, start SM and MM sub-layers, set periodic timers (`NAS_TIMER_CYCLE` at 1 s, `MM_CYCLE` at 1.1 s) | `NasTask::onStart()` in `src/ue/nas/task.cpp` |
| 6 | **UeRrcTask::onStart()**: post initial `TRIGGER_CYCLE`, set `MACHINE_CYCLE` timer (2500 ms period), log initial UE position if configured, start OOB measurement provider if configured | `UeRrcTask::onStart()` in `src/ue/rrc/task.cpp` |
| 7 | RRC initial state is `RRC_IDLE`; NAS initial state is `RM_DEREGISTERED` / `CM_IDLE` / `MM_DEREGISTERED` | Constructor defaults in `src/ue/rrc/task.cpp`, `src/ue/nas/mm/base.cpp` |
| 8 | Enter the main event loop | `main()` → `Loop()` in `src/ue.cpp` |

## 2. RF Layer Simulation (RLS)

The UE communicates with GNBs over UDP connections.  This avoids having to simulate complex RF parameters.  The RLS layer operates as follows:

1. The UE periodically sends "Heartbeat" messages to all gNBs it is aware of.  Awareness is by including the IP address of each gNB that the UE could communicate with in its config file. The Heartbeat message is a small packet that only contains some UE-unique IDs that are useful to identify it to the gNBs, and the UE's location.  Most importantly, the Heartbeat packet includes the source UDP port on which the gNB can reach the UE.

2. The UE expects that gNBs will provide an acknowledgement to the heartbeat (Heartbeat-Ack) within a period of time of receiving the Heartbeat message.  Failure to receive the Heartbeat-Ack is interpreted as a radio link failure (which triggers a disconnection if teh UE was in RRC_CONNECTED mode and returns the UE to RRC_IDLE mode).  Teh Heartbeat-Ack is also a small packet with information that identifies the sending gNB and, most importantly, a simulated Reference Signal Received Power (RSRP) value that is intended to simulate the signal strength the UE would measure for RF carriers received from this gNB.

3. Besides Heartbeat/Heartbeat-Ack messages, all other communications over the RLS layer are considered encapsulated packets destined for a higher layer endpoint on the UE or gNB. Messages received from the gNB are forwarded using the RlsToRrc message to the UE's RRC Task for processing.  Message received from the RRC Task are sent out over the RLS connection to the gNB to which the UE is connected (or attempting to connect to).


### Key RLS Constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `TIMER_PERIOD_MACHINE_CYCLE` | 2500 ms | RRC periodic cycle interval |

---

## 3. Cell Selection (Idle Mode)

### Triggering Event
**RRC periodic cycle fires** while in `RRC_IDLE` or `RRC_INACTIVE` state (every `TIMER_PERIOD_MACHINE_CYCLE` = 2500 ms), or a `TRIGGER_CYCLE` message is received.

### 5G Context
Per 3GPP TS 38.304, the UE in idle mode must camp on a suitable cell for the selected PLMN. If no suitable cell is found, it attempts to find an acceptable cell.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `performCycle()` detects `m_state == RRC_IDLE` → calls `performCellSelection()` | `src/ue/rrc/state.cpp` |
| 2 | Wait for initial startup delay (1 s for cell discovery, 4 s for PLMN selection) | `performCellSelection()` in `src/ue/rrc/idle.cpp` |
| 3 | Try `lookForSuitableCell()` — check each detected cell in `m_cellDesc` against: PLMN match, SIB1 presence, MIB presence, barring, reserved status, forbidden TAI | `lookForSuitableCell()` in `src/ue/rrc/idle.cpp` |
| 4 | If no suitable cell found, try `lookForAcceptableCell()` — relaxed criteria (no PLMN match required) | `lookForAcceptableCell()` in `src/ue/rrc/idle.cpp` |
| 5 | If cell found: update `shCtx.currentCell`, send `ASSIGN_CURRENT_CELL` to RLS, send `ACTIVE_CELL_CHANGED` to NAS | `performCellSelection()` in `src/ue/rrc/idle.cpp` |
| 6 | If no cell found: log warnings, clear current cell | `performCellSelection()` in `src/ue/rrc/idle.cpp` |

### Cell Detection and Signal Tracking

Cells are detected and tracked via signal change notifications from the RLS layer:

| Event | Handler | File |
|-------|---------|------|
| RLS reports new/changed signal | `handleCellSignalChange()` | `src/ue/rrc/cells.cpp` |
| New cell detected (signal > -120 dBm) | `notifyCellDetected()` — adds to `m_cellDesc`, calls `updateAvailablePlmns()` | `src/ue/rrc/cells.cpp` |
| Cell signal lost (< -120 dBm) | `notifyCellLost()` — removes from `m_cellDesc`, triggers radio link failure if it was the serving cell | `src/ue/rrc/cells.cpp` |
| System information received (MIB) | `receiveMib()` | `src/ue/rrc/sysinfo.cpp` |
| System information received (SIB1) | `receiveSib1()` — extracts PLMN, TAC, NCI, cell barring status | `src/ue/rrc/sysinfo.cpp` |

---

## 4. RRC Connection Establishment

### Triggering Event
**NAS layer sends an uplink NAS PDU** (e.g. Registration Request) while the UE is in `RRC_IDLE` state.

### 5G Context
Per 3GPP TS 38.331 §5.3.3, the UE initiates the RRC connection establishment procedure to move from `RRC_IDLE` to `RRC_CONNECTED`. This involves sending an RRCSetupRequest and receiving an RRCSetup from the gNB.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | NAS pushes `UPLINK_NAS_DELIVERY` message to RRC | `NmUeNasToRrc` in `src/ue/nts.hpp` |
| 2 | RRC SAP handler receives the message → calls `deliverUplinkNas()` | `handleNasSapMessage()` in `src/ue/rrc/sap.cpp` |
| 3 | Since `m_state == RRC_IDLE`, `deliverUplinkNas()` calls `startConnectionEstablishment()` with the NAS PDU | `deliverUplinkNas()` in `src/ue/rrc/nas.cpp` |
| 4 | Verify state is `RRC_IDLE` and an active cell exists | `startConnectionEstablishment()` in `src/ue/rrc/connection.cpp` |
| 5 | Prepare Initial UE Identity: use S-TMSI (from GUTI) if available, otherwise generate a 39-bit random value | `startConnectionEstablishment()` in `src/ue/rrc/connection.cpp` |
| 6 | Store the NAS PDU as `m_initialNasPdu` (to include in RRCSetupComplete) | `startConnectionEstablishment()` in `src/ue/rrc/connection.cpp` |
| 7 | Construct and send **RRCSetupRequest** (UL-CCCH) to the active cell | `ConstructSetupRequest()`, `sendRrcMessage(cellId, UL_CCCH)` in `src/ue/rrc/connection.cpp` |
| 8 | gNB responds with **RRCSetup** (DL-CCCH) | — |
| 9 | `receiveRrcSetup()` handles the response: constructs **RRCSetupComplete** (UL-DCCH) carrying the NAS PDU and S-TMSI | `receiveRrcSetup()` in `src/ue/rrc/connection.cpp` |
| 10 | Switch RRC state to `RRC_CONNECTED` | `switchState(ERrcState::RRC_CONNECTED)` in `src/ue/rrc/state.cpp` |
| 11 | Notify NAS of connection setup → `RRC_CONNECTION_SETUP` | `NmUeRrcToNas(RRC_CONNECTION_SETUP)` pushed to NAS task |
| 12 | NAS `handleRrcConnectionSetup()` switches CM state to `CM_CONNECTED` | `handleRrcConnectionSetup()` in `src/ue/nas/mm/radio.cpp` |

### Failure Path

| Event | Handler | Action |
|-------|---------|--------|
| **RRCReject** received | `receiveRrcReject()` in `src/ue/rrc/connection.cpp` | Calls `handleEstablishmentFailure()` |
| No active cell | `startConnectionEstablishment()` | Calls `handleEstablishmentFailure()` |
| Establishment failure | `handleEstablishmentFailure()` in `src/ue/rrc/connection.cpp` | Pushes `RRC_ESTABLISHMENT_FAILURE` to NAS |

---

## 5. NAS Registration with the Network

### Triggering Event
**NAS MM cycle** detects the UE is in `MM_DEREGISTERED` with an available cell, and transitions to `MM_DEREGISTERED_INITIAL_REGISTRATION_NEEDED`, then triggers the registration procedure.

### 5G Context
Per 3GPP TS 24.501 §5.5.1, the UE performs initial registration to transition from `RM_DEREGISTERED` to `RM_REGISTERED`. This involves sending a Registration Request, completing authentication and security, and receiving a Registration Accept.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `performMmCycle()` evaluates the current MM sub-state | `performMmCycle()` in `src/ue/nas/mm/base.cpp` |
| 2 | If in `MM_DEREGISTERED_PS` with a current cell → switch to `MM_DEREGISTERED_NORMAL_SERVICE` or `MM_DEREGISTERED_INITIAL_REGISTRATION_NEEDED` | `performMmCycle()` in `src/ue/nas/mm/base.cpp` |
| 3 | `sendInitialRegistration()` prepares the NAS **Registration Request** | `sendInitialRegistration()` in `src/ue/nas/mm/register.cpp` |
| 3a | — Check RM state is `RM_DEREGISTERED` | Precondition check |
| 3b | — Check T3346 timer, perform Unified Access Control (UAC) | `performUac()` |
| 3c | — Invalidate current NAS security context | `m_usim->m_currentNsCtx = {}` |
| 3d | — Prepare NSSAI, UE security capability, mobile identity (SUCI/GUTI) | Helper functions in `src/ue/nas/mm/register.cpp` |
| 3e | — Construct `RegistrationRequest` with `registrationType = INITIAL_REGISTRATION` | `src/ue/nas/mm/register.cpp` |
| 4 | Send the NAS message via `sendNasMessage()` → NAS encoding → `UPLINK_NAS_DELIVERY` to RRC | `sendNasMessage()` in `src/ue/nas/mm/transport.cpp` |
| 5 | Switch MM state to `MM_REGISTERED_INITIATED` and start timer **T3510** | `sendInitialRegistration()` in `src/ue/nas/mm/register.cpp` |
| 6 | (This triggers RRC connection establishment if in RRC_IDLE — see Section 4) | — |
| 7 | **Authentication** (if requested by AMF): Authentication Request/Response exchange via NAS transport | `receiveAuthenticationRequest()` / `receiveAuthenticationResponse()` in `src/ue/nas/mm/auth.cpp` |
| 8 | **Security Mode**: Security Mode Command/Complete exchange | `receiveSecurityModeCommand()` in `src/ue/nas/mm/security.cpp` |
| 9 | **Registration Accept** received → `receiveRegistrationAccept()` | `receiveRegistrationAccept()` in `src/ue/nas/mm/register.cpp` |
| 9a | — Switch RM state to `RM_REGISTERED` | `switchRmState(RM_REGISTERED)` |
| 9b | — Switch MM state to `MM_REGISTERED_NORMAL_SERVICE` | `switchMmState()` |
| 9c | — Process assigned GUTI, TAI list, allowed NSSAI | `receiveRegistrationAccept()` |
| 10 | Send **Registration Complete** to the network | `sendRegistrationComplete()` |

### Mobility Registration Update

When already registered, triggered by events like TAI change, T3512 expiry, or configuration update:

| Trigger | Handler | File |
|---------|---------|------|
| TAI changed (cell change) | `handleActiveCellChange()` → `sendMobilityRegistration()` | `src/ue/nas/mm/radio.cpp`, `src/ue/nas/mm/register.cpp` |
| T3512 expiry | Timer callback → `sendMobilityRegistration(T3512_EXPIRY)` | `src/ue/nas/mm/register.cpp` |
| Configuration update | `sendMobilityRegistration(CONFIGURATION_UPDATE)` | `src/ue/nas/mm/register.cpp` |

---

## 6. NAS Transport (Uplink and Downlink)

### Triggering Event
**Uplink:** NAS layer has a PDU to send. **Downlink:** gNB sends a DLInformationTransfer containing a NAS PDU.

### 5G Context
Per 3GPP TS 38.331 §5.7.2/5.7.3, ULInformationTransfer and DLInformationTransfer carry NAS messages transparently through the RRC layer.

### Uplink Path

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | NAS pushes `UPLINK_NAS_DELIVERY` to RRC task | `NmUeNasToRrc` in `src/ue/nts.hpp` |
| 2 | `handleNasSapMessage()` → `deliverUplinkNas()` | `src/ue/rrc/sap.cpp` |
| 3 | If `RRC_CONNECTED`: construct **ULInformationTransfer** (UL-DCCH) wrapping the NAS PDU | `deliverUplinkNas()` in `src/ue/rrc/nas.cpp` |
| 4 | If `RRC_IDLE`: trigger connection establishment (carries NAS PDU in RRCSetupComplete) | `deliverUplinkNas()` → `startConnectionEstablishment()` |

### Downlink Path

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | gNB sends **DLInformationTransfer** (DL-DCCH) | — |
| 2 | `handleDownlinkRrc()` decodes the DL-DCCH message | `handleDownlinkRrc()` in `src/ue/rrc/channel.cpp` |
| 3 | `receiveDownlinkInformationTransfer()` extracts the NAS PDU | `receiveDownlinkInformationTransfer()` in `src/ue/rrc/nas.cpp` |
| 4 | Push `NAS_DELIVERY` to NAS task carrying the NAS PDU | `NmUeRrcToNas(NAS_DELIVERY)` |

---

## 7. RRC Reconfiguration — Normal (without Handover)

### Triggering Event
**gNB sends an RRCReconfiguration** message (DL-DCCH) that does **not** contain a `ReconfigurationWithSync` IE (i.e. no handover).

### 5G Context
Per 3GPP TS 38.331 §5.3.5, the gNB may send RRCReconfiguration to update measurement configuration, deliver NAS messages, or modify radio bearer settings.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `handleDownlinkRrc()` decodes DL-DCCH → detects `rrcReconfiguration` | `src/ue/rrc/channel.cpp` |
| 2 | `receiveRrcReconfiguration()` is called with the decoded message | `receiveRrcReconfiguration()` in `src/ue/rrc/reconfig.cpp` |
| 3 | If `measConfig` is present: parse measurement configuration and apply it | `parseMeasConfig()`, `applyMeasConfig()` in `src/ue/rrc/reconfig.cpp` |
| 4 | If `nonCriticalExtension` (v1530-IEs) has `dedicatedNAS_MessageList`: deliver each NAS PDU to NAS layer via `NAS_DELIVERY` | `receiveRrcReconfiguration()` in `src/ue/rrc/reconfig.cpp` |
| 5 | Check v1610-IEs for `conditionalReconfiguration` → if present, parse CHO candidates (see Section 14) | `parseConditionalReconfiguration()` in `src/ue/rrc/cho.cpp` |
| 6 | Check `masterCellGroup` for `ReconfigurationWithSync` → not present for normal reconfig | `receiveRrcReconfiguration()` in `src/ue/rrc/reconfig.cpp` |
| 7 | Construct and send **RRCReconfigurationComplete** (UL-DCCH) | `receiveRrcReconfiguration()` in `src/ue/rrc/reconfig.cpp` |

---

## 8. RRC Reconfiguration — Handover (ReconfigurationWithSync)

### Triggering Event
**gNB sends an RRCReconfiguration** message (DL-DCCH) that **contains** a `ReconfigurationWithSync` IE inside `masterCellGroup → spCellConfig`.

### 5G Context
Per 3GPP TS 38.331 §5.3.5.4, when the UE receives an RRCReconfiguration with `ReconfigurationWithSync`, it performs an intra-system handover to the target cell identified by the target PCI and new C-RNTI.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `receiveRrcReconfiguration()` processes the incoming RRCReconfiguration | `src/ue/rrc/reconfig.cpp` |
| 2 | Apply `measConfig` if present | `parseMeasConfig()`, `applyMeasConfig()` |
| 3 | Deliver any `dedicatedNAS_Message` entries to NAS | `receiveRrcReconfiguration()` |
| 4 | Decode `masterCellGroup` (UPER) as `CellGroupConfig` | `rrc::encode::Decode<ASN_RRC_CellGroupConfig>()` |
| 5 | Navigate to `spCellConfig → reconfigurationWithSync` | `receiveRrcReconfiguration()` in `src/ue/rrc/reconfig.cpp` |
| 6 | Extract handover parameters: | |
| | — `physCellId` (target PCI) from `spCellConfigCommon` | |
| | — `newUE_Identity` (new C-RNTI) | |
| | — `t304` (supervision timer) → converted via `t304EnumToMs()` | `t304EnumToMs()` in `src/ue/rrc/reconfig.cpp` |
| | — `rach_ConfigDedicated` presence (contention-free RACH flag) | |
| 7 | Set `isHandover = true` → call `performHandover()` (see Section 9) | `receiveRrcReconfiguration()` → `performHandover()` |

### Extension Chain for CHO

The function also walks the non-critical extension chain looking for Conditional Handover:

```
v1530-IEs → v1540-IEs → v1560-IEs → v1610-IEs → conditionalReconfiguration
```

If `conditionalReconfiguration` is found at v1610, it is parsed via `parseConditionalReconfiguration()` (see Section 14).

---

## 9. Handover Execution (performHandover)

### Triggering Event
Called by `receiveRrcReconfiguration()` (network-triggered handover) or `executeChoCandidate()` (conditional handover).

### 5G Context
Per 3GPP TS 38.331 §5.3.5.4, upon receiving a handover command the UE suspends measurements, starts T304, resolves the target cell, switches the serving cell, performs RACH, sends RRCReconfigurationComplete to the target, and resumes measurements.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 0 | **Suspend measurements** during handover | `suspendMeasurements()` in `src/ue/rrc/handover.cpp` — sets `m_measurementsSuspended = true`, resets TTT timestamps |
| 1 | **Start T304** supervision timer | `setTimer(TIMER_ID_T304, t304Ms)` — T304 values: 50, 100, 150, 200, 500, 1000, 2000, 10000 ms |
| 2 | Set `m_handoverInProgress = true`, record `m_hoTxId`, `m_hoTargetPci` | `performHandover()` in `src/ue/rrc/handover.cpp` |
| 3 | **Security key refresh** (simulated in UERANSIM) | `refreshSecurityKeys()` in `src/ue/rrc/handover.cpp` — logs KgNB* derivation event |
| 4 | **Find target cell** among detected cells by PCI | `findCellByPci()` in `src/ue/rrc/handover.cpp` |
| 4a | — Strategy 1: direct match of PCI value to RLS cellId | |
| 4b | — Strategy 2: match by NCI lower 10 bits in SIB1 | |
| 4c | — Strategy 3: strongest non-serving detected cell (fallback) | |
| 5 | Save previous serving cell information | Read from `shCtx.currentCell` |
| 6 | **MAC reset** (simulated — logged for traceability) | `performHandover()` |
| 7 | **Switch serving cell**: update `shCtx.currentCell`, send `ASSIGN_CURRENT_CELL` to RLS | `performHandover()` in `src/ue/rrc/handover.cpp` |
| 8 | **RACH towards target cell**: contention-free if `rach-ConfigDedicated` was provided, contention-based otherwise (simulated via RLS `ASSIGN_CURRENT_CELL`) | `performHandover()` |
| 9 | **Send RRCReconfigurationComplete** to target cell (UL-DCCH) | `sendRrcMessage(pdu)` in `performHandover()` |
| 10 | Clear `m_handoverInProgress` flag (T304 will be silently ignored) | `performHandover()` |
| 11 | **Resume measurements** on the new serving cell | `resumeMeasurements()` in `src/ue/rrc/handover.cpp` — resets all `enteringTimestamp` and `reported` flags |
| 12 | **Notify NAS** of cell change → `ACTIVE_CELL_CHANGED` with previous TAI | `NmUeRrcToNas(ACTIVE_CELL_CHANGED)` pushed to NAS task |

### Handover Failure

If the target cell cannot be found (`findCellByPci()` returns 0):
- Clear `m_handoverInProgress`, resume measurements
- Declare radio link failure via `declareRadioLinkFailure(SIGNAL_LOST_TO_CONNECTED_CELL)`
- UE transitions to `RRC_IDLE`

---

## 10. Measurement Framework — Configuration

### Triggering Event
**RRCReconfiguration** containing a `measConfig` IE is received from the gNB.

### 5G Context
Per 3GPP TS 38.331 §5.5.2, the gNB configures the UE with measurement objects (frequencies), report configurations (event triggers), and measurement identities (binding objects to triggers).

### Parsing Flow

| Component | Parser Action | Data Structure |
|-----------|--------------|----------------|
| `measObjectToAddModList` | Extract `measObjectId`, `ssbFrequency` | `UeMeasObject` → stored in `m_measConfig.measObjects` |
| `reportConfigToAddModList` | Extract event type (A2/A3/A5), thresholds, hysteresis, timeToTrigger, maxReportCells | `UeReportConfig` → stored in `m_measConfig.reportConfigs` |
| `measIdToAddModList` | Extract `measId`, bind `measObjectId` ↔ `reportConfigId` | `UeMeasId` → stored in `m_measConfig.measIds` |

All parsing is performed in `parseMeasConfig()` in `src/ue/rrc/reconfig.cpp`.

### Supported Event Types and Their Parameters

| Event | Condition | Parameters |
|-------|-----------|------------|
| **A2** | Serving cell RSRP < threshold − hysteresis | `a2Threshold` (dBm), `hysteresis` (dB), `timeToTriggerMs` (ms) |
| **A3** | Neighbor RSRP > serving RSRP + offset + hysteresis | `a3Offset` (dB), `hysteresis` (dB), `timeToTriggerMs` (ms) |
| **A5** | Serving RSRP < threshold1 − hysteresis **AND** neighbor RSRP > threshold2 + hysteresis | `a5Threshold1` (dBm), `a5Threshold2` (dBm), `hysteresis` (dB), `timeToTriggerMs` (ms) |

### ASN.1 Conversion Helpers (in `src/ue/rrc/reconfig.cpp`)

| Function | Conversion |
|----------|------------|
| `rsrpRangeToDbm(val)` | RSRP_Range (0..127) → dBm (−156..−44) |
| `timeToTriggerMs(ttt)` | TTT enum → ms: 0, 40, 64, 80, 100, 128, 160, 256, 320, 480, 512, 640, 1024, 1280, 2560, 5120 |
| `hysteresisToDb(h)` | 0.5 dB units → integer dB (rounded down) |
| `triggerQuantityToDbm(tq)` | `MeasTriggerQuantity` → RSRP in dBm |
| `triggerQuantityOffsetToDb(tqo)` | `MeasTriggerQuantityOffset` → dB offset |

---

## 11. Measurement Framework — Collection

### Triggering Event
Called every evaluation cycle (from `evaluateMeasurements()` and `evaluateChoCandidates()`).

### 5G Context
UERANSIM does not model physical RF. Instead, measurements come from two sources: the RLS layer (simulated dBm values per detected cell) and an optional Out-of-Band (OOB) measurement provider.

### Collection Process

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `collectMeasurements()` starts with RLS-simulated values from `m_cellDesc` | `collectMeasurements()` in `src/ue/rrc/measurement.cpp` |
| 2 | For each cell: create `CellMeasurement` with `cellId`, `rsrp = desc.dbm`, default `rsrq = -10`, `sinr = 0` | `collectMeasurements()` |
| 3 | If `m_measProvider` is active: get OOB measurements and overlay/replace by cell identity | `collectMeasurements()` |
| 4 | OOB cell identity resolution: first by `cellId`, then by `nci` (NR Cell Identity from SIB1) | `resolveCellId()` in `src/ue/rrc/measurement.cpp` |
| 5 | Unresolved OOB cells stored with synthetic negative cellId (from NCI) to avoid collisions | `collectMeasurements()` |
| 6 | `getServingCellRsrp()` extracts the serving cell's RSRP from the measurement map | `getServingCellRsrp()` in `src/ue/rrc/measurement.cpp` |

### OOB Measurement Source Types

| Type | Config | Description |
|------|--------|-------------|
| `NONE` | Default | Use only RLS-simulated dBm values |
| `UDP` | `udpAddress`, `udpPort` | Receive JSON measurements over UDP |
| `UNIX_SOCK` | `unixSocketPath` | Receive JSON measurements over a Unix datagram socket |
| `FILE` | `filePath`, `filePollIntervalMs` | Periodically read measurements from a JSON file |

Configuration is in `MeasSourceConfig` struct in `src/ue/rrc/measurement.hpp`.

### OOB Measurement JSON Format

The OOB measurement provider (`src/ue/rrc/meas_provider.cpp`) accepts JSON (or YAML, since the parser uses yaml-cpp which treats JSON as a subset of YAML). Two top-level shapes are accepted:

**Object with `measurements` array (preferred):**
```json
{
  "measurements": [
    { "cellId": 1, "rsrp": -85, "rsrq": -10, "sinr": 15 },
    { "cellId": 2, "rsrp": -95, "rsrq": -12, "sinr": 8 },
    { "nci": 36,   "rsrp": -78, "rsrq": -8,  "sinr": 20 }
  ]
}
```

**Bare array (also accepted):**
```json
[
  { "cellId": 1, "rsrp": -85 },
  { "cellId": 2, "rsrp": -95, "rsrq": -12, "sinr": 8 }
]
```

### Field Reference

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `cellId` | int | No* | 0 | Internal UERANSIM cell ID (matches the RLS-layer cell identifier). If 0, the provider falls back to `nci` for resolution. |
| `nci` | int64 | No* | 0 | NR Cell Identity (as broadcast in SIB1). Used to resolve the cell when `cellId` is absent or 0. Matched against `desc.sib1.nci` in `m_cellDesc`. |
| `rsrp` | int | **Yes** | −140 | Reference Signal Received Power in dBm. Typical range: −156 to −44 dBm. |
| `rsrq` | int | No | −20 | Reference Signal Received Quality in dB. |
| `sinr` | int | No | −23 | Signal-to-Interference-plus-Noise Ratio in dB. |

> \* At least one of `cellId` or `nci` should be provided so the measurement can be associated with a known cell. If neither resolves to a known cell, the measurement is stored under a synthetic negative cell ID derived from `nci` (to avoid collisions with real cell IDs) and will only participate in neighbor-cell evaluation if a later SIB1 reception creates a matching entry.

### Cell Identity Resolution Logic (`resolveCellId()`)

1. If `cellId != 0` → use it directly.
2. Else if `nci != 0` → scan `m_cellDesc` for a cell whose `sib1.nci` matches → return that cell's ID.
3. Otherwise → return 0 (unresolved); measurement stored under synthetic key `−(nci & 0x7FFFFFFF)`.

### Delivery Mechanisms

| Source Type | How JSON is Delivered | Parsing Trigger |
|-------------|----------------------|-----------------|
| **UDP** | Send a UDP datagram containing the JSON string to the UE's configured `udpAddress:udpPort` (default `127.0.0.1:7200`). Max datagram size: 8192 bytes. | Each received datagram triggers `parseMeasurements()`. |
| **UNIX_SOCK** | Send a Unix datagram to the configured `unixSocketPath`. The provider binds a `SOCK_DGRAM` socket at that path. Max datagram size: 8192 bytes. | Each received datagram triggers `parseMeasurements()`. |
| **FILE** | Write the JSON to the configured `filePath`. The provider reads the entire file every `filePollIntervalMs` milliseconds (default 1000 ms). | Each poll cycle reads and parses the file contents. |

Each call to `parseMeasurements()` **replaces** the entire previous measurement set (it is not additive). To update measurements, send a complete snapshot of all cells each time.

---

## 12. Measurement Framework — Event Evaluation (A2, A3, A5)

### Triggering Event
**RRC periodic cycle fires** while in `RRC_CONNECTED` state (every 2500 ms). Called from `performCycle()` → `evaluateMeasurements()`.

### 5G Context
Per 3GPP TS 38.331 §5.5.4, the UE evaluates configured measurement events using the entering conditions. When an event condition is satisfied for at least `timeToTrigger` milliseconds, the UE sends a MeasurementReport to the gNB.

### Preconditions
- `m_state == RRC_CONNECTED`
- `m_measurementsSuspended == false` (not during handover)
- `m_measConfig.measIds` is not empty

### Evaluation Flow

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `evaluateMeasurements()` collects all measurements | `evaluateMeasurements()` in `src/ue/rrc/measurement.cpp` |
| 2 | Get serving cell RSRP and serving cell ID | `getServingCellRsrp()`, `shCtx.currentCell` |
| 3 | For each configured `measId`: | Loop in `evaluateMeasurements()` |
| 3a | — Skip if already reported (one-shot) | `state.reported` check |
| 3b | — Look up the associated `UeReportConfig` | `m_measConfig.reportConfigs[mid.reportConfigId]` |
| 3c | — Evaluate the event condition: | |

### Event-Specific Evaluation Logic

**Event A2** — Serving cell becomes worse than threshold:
```
entering condition: servingRSRP < a2Threshold − hysteresis
```
- Evaluated in `evaluateA2()` in `src/ue/rrc/measurement.cpp`

**Event A3** — Neighbor cell becomes offset better than serving cell:
```
entering condition: neighborRSRP > servingRSRP + a3Offset + hysteresis
```
- Evaluated per non-serving cell in `evaluateA3_cell()` in `src/ue/rrc/measurement.cpp`
- Triggered neighbors collected and sorted by RSRP descending

**Event A5** — Serving cell worse than threshold1 AND neighbor better than threshold2:
```
entering condition 1: servingRSRP < a5Threshold1 − hysteresis
entering condition 2: neighborRSRP > a5Threshold2 + hysteresis
```
- Evaluated in `evaluateA5_serving()` + `evaluateA5_neighbor()` in `src/ue/rrc/measurement.cpp`
- Both conditions must be simultaneously satisfied

### Time-to-Trigger

| Step | Action |
|------|--------|
| 4 | If event is satisfied: record `enteringTimestamp` if first time | `state.enteringTimestamp = now` |
| 5 | Check elapsed time: `elapsed = now − enteringTimestamp` | |
| 6 | If `elapsed >= timeToTriggerMs`: event triggered → send report | `sendMeasurementReport()` |
| 7 | Mark as reported: `state.reported = true` | One-shot reporting |
| 8 | If event is **not** satisfied: reset `enteringTimestamp = 0` | TTT restarts on next satisfaction |

---

## 13. MeasurementReport Generation and Transmission

### Triggering Event
A measurement event (A2, A3, or A5) has been satisfied for the configured `timeToTrigger` duration.

### 5G Context
Per 3GPP TS 38.331 §5.5.5, the UE sends a MeasurementReport to inform the gNB of the measurement event, including serving cell and neighbor cell results.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | Sort triggered neighbors by RSRP descending | `evaluateMeasurements()` in `src/ue/rrc/measurement.cpp` |
| 2 | Limit to `maxReportCells` | `evaluateMeasurements()` |
| 3 | Call `sendMeasurementReport()` | `sendMeasurementReport()` in `src/ue/rrc/measurement.cpp` |
| 4 | Construct ASN.1 `MeasurementReport` (UL-DCCH): | |
| 4a | — Set `measId` | |
| 4b | — Build `measResultServingMOList` with serving cell RSRP (converted via `rsrpToAsn()`) | |
| 4c | — Build `measResultNeighCells` → `measResultListNR` with each neighbor's RSRP | |
| 5 | Send via `sendRrcMessage(pdu)` | Routes UL-DCCH through current serving cell via RLS |

### RSRP Encoding

The function `rsrpToAsn(dBm)` converts dBm to ASN.1 RSRP_Range: `value = dBm + 156` (range 0..127, representing −156..−44 dBm).

---

## 14. Conditional Handover (CHO) — Configuration

### Triggering Event
**RRCReconfiguration** contains `conditionalReconfiguration` in the v1610 non-critical extension chain, **or** a binary CHO injection arrives via the `DL_CHO` custom channel (test harness path).

### 5G Context
Per 3GPP TS 38.331 §5.3.5.8 (Release 16/17), the gNB pre-configures one or more CHO candidates. Each candidate carries a condition group (AND logic) and a nested RRCReconfiguration with target cell parameters. When a candidate's condition group is fully satisfied, the UE autonomously executes the handover.

### ASN.1 Configuration Path (`parseConditionalReconfiguration()`)

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | Walk `condReconfigToAddModList` items | `parseConditionalReconfiguration()` in `src/ue/rrc/cho.cpp` |
| 2 | For each candidate, extract `condReconfigId` → `candidateId` | |
| 3 | Parse `condExecutionCond` MeasId list → resolve each to its `UeReportConfig` event type → build `ChoCondition` entries | Multiple MeasIds = AND logic |
| 4 | If no conditions parsed → add T1 fallback (timer-only, default 1000 ms) | Default `DEFAULT_T1_DURATION_MS = 1000` |
| 5 | Decode `condRRCReconfig` (UPER) as nested `RRCReconfiguration` | `rrc::encode::Decode<ASN_RRC_RRCReconfiguration>()` |
| 6 | Navigate to `masterCellGroup → spCellConfig → reconfigurationWithSync` in the nested reconfig | |
| 7 | Extract target PCI, new C-RNTI, T304 from ReconfigurationWithSync | |
| 8 | Store as `ChoCandidate` in `m_choCandidates` list | Vector `m_choCandidates` in `UeRrcTask` |

### Binary DL_CHO Path (`handleChoConfiguration()`)

Used by the test harness for injecting CHO candidates with arbitrary condition groups:

| Wire Field | Size | Purpose |
|------------|------|---------|
| `numCandidates` | 4 bytes | Number of candidates |
| Per candidate: `candidateId`, `targetPci`, `newCRNTI`, `t304Ms`, `executionPriority` | 24 bytes header | Target cell parameters |
| Per condition: `eventType`, `intParams[3]`, `timeToTriggerMs`, `reserved`, `floatParams[4]` | 56 bytes | Condition specification |

Supported event types: T1 (0), A2 (1), A3 (2), A5 (3), D1 (4), D1_SIB19 (5).

### CHO Condition Types

| Type | Description | Key Parameters |
|------|-------------|----------------|
| **T1** | Timer-only: fires after `t1DurationMs` elapsed | `t1DurationMs` |
| **A2** | Serving cell RSRP < threshold | `a2Threshold`, `hysteresis`, `timeToTriggerMs` |
| **A3** | Neighbor RSRP > serving RSRP + offset − hysteresis | `a3Offset`, `a3Hysteresis`, `timeToTriggerMs` |
| **A5** | Serving < threshold1 AND neighbor > threshold2 | `a5Threshold1`, `a5Threshold2`, `a5Hysteresis`, `timeToTriggerMs` |
| **D1** | Distance to static ECEF reference point exceeds threshold | `d1RefX/Y/Z`, `d1ThresholdM` |
| **D1_SIB19** | Distance to SIB19-derived satellite reference exceeds threshold | `d1sib19ThresholdM`, `d1sib19UseNadir` |

---

## 15. Conditional Handover (CHO) — Evaluation and Execution

### Triggering Event
**RRC periodic cycle fires** while in `RRC_CONNECTED` state and CHO candidates are configured. Called from `performCycle()` → `evaluateChoCandidates()`.

### 5G Context
Per 3GPP TS 38.331 §5.3.5.8.6, when a CHO candidate's condition group is fully satisfied, the UE autonomously executes the handover without waiting for a new gNB command.

### Evaluation Flow

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | Skip if `m_choCandidates` is empty or `m_handoverInProgress` | `evaluateChoCandidates()` in `src/ue/rrc/cho.cpp` |
| 2 | Collect current measurements and serving RSRP | `collectMeasurements()`, `getServingCellRsrp()` |
| 3 | Get UE position (for D1 events) | `getUePosition()` in `src/ue/rrc/cho.cpp` |
| 4 | For each non-executed candidate: | Loop in `evaluateChoCandidates()` |
| 4a | — Find target cell RSRP for A3/A5 conditions | Match by PCI or NCI lower bits |
| 4b | — For D1_SIB19 conditions: extrapolate satellite position from SIB19 ephemeris | `extrapolateSatellitePosition()`, `computeNadir()` in `src/ue/rrc/position.cpp`, `src/ue/rrc/sib19.cpp` |
| 4c | — Evaluate each condition with TTT: `evaluateConditionWithTTT()` → `evaluateConditionRaw()` | `src/ue/rrc/cho.cpp` |
| 4d | — All conditions must be satisfied (AND logic) for candidate to trigger | |
| 5 | Collect all triggered candidates | |
| 6 | **Select best candidate** by priority tie-breaking: | |
| | (1) `executionPriority` (lowest value wins) | |
| | (2) Greatest trigger margin | |
| | (3) Highest target RSRP | |
| | (4) Configuration order (earliest) | |
| 7 | **Execute winning candidate**: `executeChoCandidate()` | `executeChoCandidate()` in `src/ue/rrc/cho.cpp` |
| 7a | — Mark all other candidates as executed (cancel) | Per TS 38.331 §5.3.5.8.6 |
| 7b | — Call `performHandover(txId=0, targetPci, newCRNTI, t304Ms)` | See Section 9 |

### Single Condition Evaluation (`evaluateConditionRaw()`)

Returns a margin value (positive = condition exceeded):

| Event | Margin Formula |
|-------|---------------|
| T1 | 1.0 if elapsed ≥ duration, else −1.0 |
| A2 | (a2Threshold + hysteresis) − servingRSRP |
| A3 | targetRSRP − (servingRSRP + a3Offset − a3Hysteresis) |
| A5 | min(threshold1+hyst − serving, target − threshold2+hyst) |
| D1 | distance − d1ThresholdM |
| D1_SIB19 | distance − resolvedThresholdM |

---

## 16. Handover Failure — T304 Expiry

### Triggering Event
**Timer T304 expires** while `m_handoverInProgress == true`.

### 5G Context
Per 3GPP TS 38.331 §5.3.5.8.3, if T304 expires before the handover completes, the UE declares a handover failure and initiates RRC re-establishment.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | Timer message `TIMER_EXPIRED` with `timerId == TIMER_ID_T304` arrives in `onLoop()` | `UeRrcTask::onLoop()` in `src/ue/rrc/task.cpp` |
| 2 | `handleT304Expiry()` checks `m_handoverInProgress` flag | `handleT304Expiry()` in `src/ue/rrc/handover.cpp` |
| 3 | If handover already completed (flag is false): ignore silently | |
| 4 | If handover still in progress: log error, clear flag | |
| 5 | Resume measurements | `resumeMeasurements()` |
| 6 | Declare radio link failure → triggers re-establishment | `declareRadioLinkFailure(SIGNAL_LOST_TO_CONNECTED_CELL)` |

---

## 17. Radio Link Failure

### Triggering Event
- **RLS reports radio link failure** (signal lost to serving cell)
- **T304 expiry** during handover
- **Handover failure** (target cell not found)

### 5G Context
Per 3GPP TS 38.331 §5.3.10, upon radio link failure the UE transitions to `RRC_IDLE` and notifies the NAS layer.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `declareRadioLinkFailure()` calls `handleRadioLinkFailure()` | `src/ue/rrc/failures.cpp` |
| 2 | Switch RRC state to `RRC_IDLE` | `m_state = ERrcState::RRC_IDLE` |
| 3 | Notify NAS → `RADIO_LINK_FAILURE` | `NmUeRrcToNas(RADIO_LINK_FAILURE)` pushed to NAS task |
| 4 | NAS handles the failure (may attempt re-registration) | `handleRadioLinkFailure()` in NAS layer |

---

## 18. RRC Connection Release

### Triggering Event
**gNB sends an RRCRelease** message (DL-DCCH), **or** NAS requests local release.

### 5G Context
Per 3GPP TS 38.331 §5.3.8, the gNB may release the RRC connection at any time. The UE transitions to `RRC_IDLE`.

### Network-Initiated Release

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `handleDownlinkRrc()` decodes `RRCRelease` | `src/ue/rrc/channel.cpp` |
| 2 | `receiveRrcRelease()` switches state to `RRC_IDLE` | `receiveRrcRelease()` in `src/ue/rrc/connection.cpp` |
| 3 | Notify NAS → `RRC_CONNECTION_RELEASE` | `NmUeRrcToNas(RRC_CONNECTION_RELEASE)` pushed to NAS task |
| 4 | NAS `handleRrcConnectionRelease()` switches CM to `CM_IDLE` | `handleRrcConnectionRelease()` in `src/ue/nas/mm/radio.cpp` |

### NAS-Initiated Local Release

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | NAS pushes `LOCAL_RELEASE_CONNECTION` to RRC | `NmUeNasToRrc(LOCAL_RELEASE_CONNECTION)` |
| 2 | `handleNasSapMessage()` switches state to `RRC_IDLE`, resets STI | `handleNasSapMessage()` in `src/ue/rrc/sap.cpp` |
| 3 | Notify NAS of release → `RRC_CONNECTION_RELEASE` | Pushed back to NAS task |

---

## 19. Paging

### Triggering Event
**gNB broadcasts a Paging message** (PCCH) to the UE.

### 5G Context
Per 3GPP TS 38.331 §5.3.2, the gNB may page the UE using the 5G-S-TMSI to notify it of pending downlink data or signaling.

### Sequence of Operations

| Step | Action | Function / File |
|------|--------|-----------------|
| 1 | `handleDownlinkRrc()` decodes PCCH message | `src/ue/rrc/channel.cpp` |
| 2 | `receivePaging()` extracts list of S-TMSI identities from paging records | `receivePaging()` in `src/ue/rrc/handler.cpp` |
| 3 | Push `PAGING` message to NAS with TMSI list | `NmUeRrcToNas(PAGING)` with `pagingTmsi` |
| 4 | NAS checks if any TMSI matches → triggers service request if so | NAS MM paging handler |

---

## 20. RRC State Machine Cycle

### Triggering Event
**Periodic timer** (`TIMER_ID_MACHINE_CYCLE`, every 2500 ms) **or** explicit `TRIGGER_CYCLE` self-message.

### 5G Context
The RRC cycle is the UE's periodic state-machine evaluation. Actions depend on the current RRC state.

### Cycle Dispatch

| RRC State | Actions Performed | Functions |
|-----------|-------------------|-----------|
| `RRC_CONNECTED` | `evaluateMeasurements()` — check A2/A3/A5 events and send reports | `src/ue/rrc/measurement.cpp` |
| | `evaluateChoCandidates()` — check CHO condition groups and execute if met | `src/ue/rrc/cho.cpp` |
| `RRC_IDLE` | `performCellSelection()` — search for suitable/acceptable cell | `src/ue/rrc/idle.cpp` |
| `RRC_INACTIVE` | `performCellSelection()` — same as idle (simplified) | `src/ue/rrc/idle.cpp` |

All dispatched from `performCycle()` in `src/ue/rrc/state.cpp`.

### Message Processing in `onLoop()`

| Message Type | Handler |
|-------------|---------|
| `UE_NAS_TO_RRC` | `handleNasSapMessage()` in `src/ue/rrc/sap.cpp` |
| `UE_RLS_TO_RRC` | `handleRlsSapMessage()` in `src/ue/rrc/sap.cpp` |
| `UE_RRC_TO_RRC` (`TRIGGER_CYCLE`) | `performCycle()` in `src/ue/rrc/state.cpp` |
| `TIMER_EXPIRED` (MACHINE_CYCLE) | Re-arm timer + `performCycle()` |
| `TIMER_EXPIRED` (T304) | `handleT304Expiry()` in `src/ue/rrc/handover.cpp` |

---

## 21. NAS Mobility Management Cycle

### Triggering Event
**Periodic timer** (`NTS_TIMER_ID_MM_CYCLE`, every 1100 ms) **or** explicit `PERFORM_MM_CYCLE` self-message.

### 5G Context
The NAS MM cycle evaluates the current NAS state and triggers procedures (registration, service request, PLMN selection) as needed.

### Key Actions by State

| MM Sub-State | Action | Function |
|-------------|--------|----------|
| `MM_DEREGISTERED_PS` | If cell available → switch to `NORMAL_SERVICE` or `INITIAL_REGISTRATION_NEEDED` | `performMmCycle()` in `src/ue/nas/mm/base.cpp` |
| `MM_DEREGISTERED_PLMN_SEARCH` | `performPlmnSelection()` — select HPLMN or equivalent | `performPlmnSelection()` in `src/ue/nas/mm/radio.cpp` |
| `MM_DEREGISTERED_NORMAL_SERVICE` | Trigger initial registration procedure | → `sendInitialRegistration()` |
| `MM_DEREGISTERED_INITIAL_REGISTRATION_NEEDED` | Trigger initial registration procedure | → `sendInitialRegistration()` |
| `MM_REGISTERED_PS` | If cell available → `NORMAL_SERVICE` | `performMmCycle()` |
| `MM_REGISTERED_PLMN_SEARCH` | `performPlmnSelection()` | `performPlmnSelection()` |

### RRC Events Handled by NAS MM

| Event | Handler | File |
|-------|---------|------|
| `RRC_CONNECTION_SETUP` | `handleRrcConnectionSetup()` → switch CM to `CM_CONNECTED` | `src/ue/nas/mm/radio.cpp` |
| `RRC_CONNECTION_RELEASE` | `handleRrcConnectionRelease()` → switch CM to `CM_IDLE` | `src/ue/nas/mm/radio.cpp` |
| `RRC_ESTABLISHMENT_FAILURE` | `handleRrcEstablishmentFailure()` → log error, may retry | `src/ue/nas/mm/radio.cpp` |
| `RADIO_LINK_FAILURE` | `handleRadioLinkFailure()` | `src/ue/nas/mm/radio.cpp` |
| `ACTIVE_CELL_CHANGED` | `handleActiveCellChange()` → check TAI, may trigger mobility update | `src/ue/nas/mm/radio.cpp` |
| `NAS_DELIVERY` | `handleNasMessage()` → dispatch by NAS message type (Auth, Security, Registration Accept, etc.) | `src/ue/nas/mm/transport.cpp` |
| `PAGING` | Check S-TMSI match → trigger service request | NAS MM paging handler |

### NAS Timers

| Timer | Purpose | Default |
|-------|---------|---------|
| T3346 | Backoff timer for registration attempts | Network-assigned |
| T3502 | Registration retry after failure | 12 min |
| T3510 | Registration attempt supervision | 15 s |
| T3511 | Registration retry after temporary failure | 10 s |
| T3512 | Periodic registration update | 54 min |
| T3517 | Service request supervision | 15 s |
| T3521 | De-registration supervision | 15 s |

Timer ticking performed in `NasTask::performTick()` (`src/ue/nas/task.cpp`), called every 1000 ms.

---

## 22. Source File Index

### RRC Layer (`src/ue/rrc/`)

| File | Purpose |
|------|---------|
| `task.cpp` / `task.hpp` | RRC task main loop, timer handling, member declarations |
| `state.cpp` | `performCycle()`, `switchState()`, `triggerCycle()` |
| `connection.cpp` | RRC Setup / Reject / Release handling, connection establishment |
| `channel.cpp` | Downlink RRC dispatch, RRC message encoding and sending |
| `sap.cpp` | Service Access Point handlers (NAS→RRC, RLS→RRC message dispatch) |
| `nas.cpp` | UL/DL NAS transport (ULInformationTransfer, DLInformationTransfer) |
| `handler.cpp` | Paging handler |
| `idle.cpp` | Cell selection: `performCellSelection()`, `lookForSuitableCell()`, `lookForAcceptableCell()` |
| `cells.cpp` | Cell signal tracking: `handleCellSignalChange()`, `notifyCellDetected()`, `notifyCellLost()` |
| `sysinfo.cpp` | MIB and SIB1 reception and parsing |
| `sib19.cpp` / `sib19.hpp` | SIB19 (NTN satellite ephemeris) reception and utilities |
| `reconfig.cpp` | RRCReconfiguration handling, MeasConfig parsing |
| `measurement.cpp` | Measurement evaluation (A2/A3/A5), report generation, JSON serialization |
| `measurement.hpp` | Measurement data structures: `UeMeasConfig`, `UeReportConfig`, `UeMeasId`, `CellMeasurement`, `ChoCandidate`, `ChoCondition` |
| `meas_provider.cpp` / `meas_provider.hpp` | Out-of-band measurement provider (UDP, Unix socket, file) |
| `handover.cpp` | Handover execution: `performHandover()`, `findCellByPci()`, `handleT304Expiry()`, `suspendMeasurements()`, `resumeMeasurements()`, `refreshSecurityKeys()` |
| `cho.cpp` | Conditional Handover: `parseConditionalReconfiguration()`, `handleChoConfiguration()`, `evaluateChoCandidates()`, `executeChoCandidate()`, `cancelAllChoCandidates()` |
| `position.cpp` / `position.hpp` | UE position utilities (ECEF, geodetic, distance calculation) |
| `failures.cpp` | Radio link failure declaration and handling |
| `access.cpp` | Unified Access Control (UAC) |

### NAS Layer (`src/ue/nas/`)

| File | Purpose |
|------|---------|
| `task.cpp` / `task.hpp` | NAS task main loop, timer ticking, message dispatch |
| `mm/base.cpp` | NasMm state machine cycle, state transitions |
| `mm/mm.hpp` | NasMm class declaration, member variables |
| `mm/register.cpp` | Initial registration, mobility registration update |
| `mm/radio.cpp` | PLMN selection, RRC event handlers (connection setup/release, cell change) |
| `mm/auth.cpp` | Authentication procedure |
| `mm/security.cpp` | Security mode procedure |
| `mm/transport.cpp` | NAS message encoding and sending |
| `sm/` | Session Management (PDU session procedures) |
| `usim/` | USIM credential management |
| `storage.cpp` | Persistent NAS storage |
| `enc.cpp` / `keys.cpp` | NAS encoding and key management |

### Other UE Components

| File | Purpose |
|------|---------|
| `src/ue.cpp` | UE entry point (`main()`), configuration loading, CLI |
| `src/ue/ue.cpp` / `ue.hpp` | `UserEquipment` class: task creation and lifecycle |
| `src/ue/nts.hpp` / `nts.cpp` | NTS message type definitions for all inter-task messages |
| `src/ue/types.hpp` / `types.cpp` | Enum definitions (RRC states, NAS states, etc.) and JSON serialization |
| `src/ue/timer.cpp` / `timer.hpp` | UE timer infrastructure |
| `src/ue/app/task.cpp` | Application task (CLI commands, TUN interface) |
| `src/ue/rls/task.cpp` | Radio Link Simulation task |
| `src/ue/rls/ctl_task.cpp` | RLS control sub-task |
| `src/ue/rls/udp_task.cpp` | RLS UDP communication sub-task |
