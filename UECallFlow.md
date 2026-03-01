# UE Call Flow Map — NTS Task Method Invocation Reference

This document maps out how methods and functions are called by each NTS (Network Task Service) task within the UE (User Equipment) of UERANSIM. It reveals the 5G control signaling functions supported by the UE and how they are implemented in the NTS framework.

---

## Table of Contents

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

---

### 3.8 Handover Execution

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
