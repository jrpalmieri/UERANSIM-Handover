# gNB Call Flow Map

This document maps the method/function call chains for each NTS (Non-blocking Task Signaling) task
in the UERANSIM gNB.  It reveals the 5G control-plane and data-plane signaling functions supported
by the gNB and how each is implemented within the NTS framework.

> **Reading convention** — Indentation shows caller → callee relationships.  
> An entry like `├─ handleFoo()` means the parent called `handleFoo()`.  
> A `→ TaskName` annotation means the function pushes an NTS message to that task.

---

## Architecture Overview

```
┌─────────────┐
│  GnbAppTask │  status display, CLI commands
└──────┬──────┘
       │ NmGnbStatusUpdate, NmGnbCliCommand
       │
┌──────┴──────┐  NmGnbRrcToNgap /   ┌─────────────┐  NmGnbSctp   ┌───────────┐
│  NgapTask   │◄────────────────────►│  SctpTask    │◄────────────►│  AMF      │
│  (NGAP)     │  NmGnbNgapToRrc      │  (SCTP)      │              │  (N2)     │
└──────┬──────┘                      └──────────────┘              └───────────┘
       │ NmGnbNgapToRrc / NmGnbNgapToGtp
       │
┌──────┴──────┐  NmGnbRrcToRls /    ┌──────────────┐
│  GnbRrcTask │◄────────────────────►│  GnbRlsTask  │  (Radio Link Simulation)
│  (RRC)      │  NmGnbRlsToRrc      │  ├─ RlsControlTask
└─────────────┘                      │  └─ RlsUdpTask
                                     └──────┬───────┘
       ┌─────────────┐  NmGnbRlsToGtp /     │ NmGnbGtpToRls
       │  GtpTask    │◄─────────────────────┘
       │  (GTP-U)    │◄──── UDP (N3 tunnel to UPF)
       └─────────────┘
```

### NTS Message Types (inter-task)

| Message Type | Direction | Purpose |
|---|---|---|
| `NmGnbRlsToRrc` | RLS → RRC | SIGNAL_DETECTED, UPLINK_RRC |
| `NmGnbRlsToGtp` | RLS → GTP | DATA_PDU_DELIVERY (uplink user data) |
| `NmGnbGtpToRls` | GTP → RLS | DATA_PDU_DELIVERY (downlink user data) |
| `NmGnbRrcToRls` | RRC → RLS | RRC_PDU_DELIVERY (downlink RRC) |
| `NmGnbRrcToNgap` | RRC → NGAP | INITIAL_NAS_DELIVERY, UPLINK_NAS_DELIVERY, RADIO_LINK_FAILURE, HANDOVER_NOTIFY, HANDOVER_REQUIRED |
| `NmGnbNgapToRrc` | NGAP → RRC | RADIO_POWER_ON, NAS_DELIVERY, AN_RELEASE, PAGING, HANDOVER_COMMAND_DELIVERY, PATH_SWITCH_REQUEST_ACK |
| `NmGnbNgapToGtp` | NGAP → GTP | UE_CONTEXT_UPDATE, UE_CONTEXT_RELEASE, SESSION_CREATE, SESSION_RELEASE |
| `NmGnbSctp` | NGAP ↔ SCTP | CONNECTION_REQUEST/CLOSE, ASSOCIATION_SETUP/SHUTDOWN, RECEIVE/SEND_MESSAGE |
| `NmGnbRlsToRls` | Internal RLS | SIGNAL_DETECTED/LOST, RECEIVE_RLS_MESSAGE, DOWNLINK/UPLINK_RRC, DOWNLINK/UPLINK_DATA, RADIO_LINK_FAILURE, TRANSMISSION_FAILURE |
| `NmGnbStatusUpdate` | NGAP → App | NGAP_IS_UP |
| `NmGnbCliCommand` | CLI → App | CLI command dispatch |

---

## 1. NgapTask — NGAP Signaling (N2 Interface)

**Source:** `src/gnb/ngap/`  
**Class:** `NgapTask` (extends `NtsTask`)

### 1.1 `onStart()`  — AMF Connection Bootstrap

```
onStart()
├─ createAmfContext()                       — for each configured AMF
└─ → SctpTask : NmGnbSctp::CONNECTION_REQUEST   — initiate SCTP to each AMF
```

### 1.2 `onLoop()` — Message Dispatch

The main loop dispatches two message types:

#### 1.2.1 From RRC (`NmGnbRrcToNgap`)

```
onLoop() ── NtsMessageType::GNB_RRC_TO_NGAP
├─ INITIAL_NAS_DELIVERY
│  └─ handleInitialNasTransport()                   [nas.cpp]
│     ├─ extractSliceInfoAndModifyPdu()              — decode NAS to find requested NSSAI
│     ├─ createUeContext()                           [management.cpp]
│     │  ├─ selectAmf()                              [nnsf.cpp]  — NSSF-like AMF selection
│     │  │  └─ (match requested SST to AMF slice support; fallback to first AMF)
│     │  └─ assign ranUeNgapId
│     ├─ Build ASN_NGAP_InitialUEMessage (NAS-PDU, RRC Establishment Cause, 5G-S-TMSI)
│     └─ sendNgapUeAssociated()                      [transport.cpp]
│        ├─ Attach AMF-UE-NGAP-ID, RAN-UE-NGAP-ID, UserLocationInformation IEs
│        ├─ ngap_encode::Encode()                    — APER encode
│        └─ → SctpTask : NmGnbSctp::SEND_MESSAGE
│
├─ UPLINK_NAS_DELIVERY
│  └─ handleUplinkNasTransport()                     [nas.cpp]
│     ├─ findUeContext()
│     ├─ Build ASN_NGAP_UplinkNASTransport
│     └─ sendNgapUeAssociated()
│
├─ RADIO_LINK_FAILURE
│  └─ handleRadioLinkFailure()                       [radio.cpp]
│     ├─ → GtpTask : NmGnbNgapToGtp::UE_CONTEXT_RELEASE
│     └─ sendContextRelease()                        [context.cpp]
│        ├─ Build ASN_NGAP_UEContextReleaseRequest (cause, PDU session list)
│        └─ sendNgapUeAssociated()
│
├─ HANDOVER_REQUIRED
│  └─ sendHandoverRequired()                         [handover.cpp]
│     ├─ findUeContext()
│     ├─ Build ASN_NGAP_HandoverRequired
│     │  ├─ IE: HandoverType = intra5gs
│     │  ├─ IE: Cause
│     │  ├─ IE: TargetID (TargetRANNodeID with target PCI)
│     │  └─ IE: SourceToTarget-TransparentContainer
│     └─ sendNgapUeAssociated()
│
└─ HANDOVER_NOTIFY
   └─ handleHandoverNotifyFromRrc()                  [handover.cpp]
      ├─ sendHandoverNotify()
      │  ├─ Build ASN_NGAP_HandoverNotify (UserLocationInformation)
      │  └─ sendNgapUeAssociated()
      └─ sendPathSwitchRequest()
         ├─ Build ASN_NGAP_PathSwitchRequest (UserLocationInformation)
         └─ sendNgapUeAssociated()
```

#### 1.2.2 From SCTP (`NmGnbSctp`)

```
onLoop() ── NtsMessageType::GNB_SCTP
├─ ASSOCIATION_SETUP
│  └─ handleAssociationSetup()                       [interface.cpp]
│     └─ sendNgSetupRequest()
│        ├─ Build ASN_NGAP_NGSetupRequest
│        │  ├─ IE: GlobalRANNodeID (gNB-ID)
│        │  ├─ IE: RANNodeName
│        │  ├─ IE: SupportedTAList (PLMN, TAC, S-NSSAI slices)
│        │  └─ IE: DefaultPagingDRX
│        └─ sendNgapNonUe()                          [transport.cpp]
│           ├─ ngap_encode::Encode()
│           └─ → SctpTask : NmGnbSctp::SEND_MESSAGE
│
├─ RECEIVE_MESSAGE
│  └─ handleSctpMessage()                            [transport.cpp]
│     ├─ ngap_encode::Decode()                       — APER decode NGAP PDU
│     ├─ handleSctpStreamId()                        — validate stream assignment
│     │
│     ├── InitiatingMessage dispatch:
│     │   ├─ ErrorIndication        → receiveErrorIndication()               [interface.cpp]
│     │   ├─ InitialContextSetupRequest → receiveInitialContextSetup()       [context.cpp]
│     │   │  ├─ → GtpTask : NmGnbNgapToGtp::UE_CONTEXT_UPDATE
│     │   │  ├─ Parse PDUSessionResourceSetupListCxtReq
│     │   │  │  └─ setupPduSessionResource()                                [session.cpp]
│     │   │  │     ├─ Allocate downlink TEID
│     │   │  │     ├─ → GtpTask : NmGnbNgapToGtp::SESSION_CREATE
│     │   │  │     └─ Track PDU session in UE context
│     │   │  ├─ deliverDownlinkNas() → RRC                                  [nas.cpp]
│     │   │  └─ Send ASN_NGAP_InitialContextSetupResponse
│     │   │
│     │   ├─ DownlinkNASTransport → receiveDownlinkNasTransport()           [nas.cpp]
│     │   │  └─ deliverDownlinkNas()
│     │   │     └─ → GnbRrcTask : NmGnbNgapToRrc::NAS_DELIVERY
│     │   │
│     │   ├─ UEContextReleaseCommand → receiveContextRelease()              [context.cpp]
│     │   │  ├─ → GnbRrcTask : NmGnbNgapToRrc::AN_RELEASE
│     │   │  ├─ → GtpTask : NmGnbNgapToGtp::UE_CONTEXT_RELEASE
│     │   │  ├─ Send ASN_NGAP_UEContextReleaseComplete
│     │   │  └─ deleteUeContext()
│     │   │
│     │   ├─ UEContextModificationRequest → receiveContextModification()    [context.cpp]
│     │   │  ├─ Update UE AMBR / AMF-UE-NGAP-ID
│     │   │  ├─ Send ASN_NGAP_UEContextModificationResponse
│     │   │  └─ → GtpTask : NmGnbNgapToGtp::UE_CONTEXT_UPDATE
│     │   │
│     │   ├─ PDUSessionResourceSetupRequest
│     │   │  └─ receiveSessionResourceSetupRequest()                        [session.cpp]
│     │   │     ├─ For each PDU session:
│     │   │     │  └─ setupPduSessionResource()
│     │   │     │     ├─ Allocate downlink TEID
│     │   │     │     └─ → GtpTask : NmGnbNgapToGtp::SESSION_CREATE
│     │   │     ├─ deliverDownlinkNas() (if NAS PDU present)
│     │   │     └─ Send ASN_NGAP_PDUSessionResourceSetupResponse
│     │   │
│     │   ├─ PDUSessionResourceReleaseCommand
│     │   │  └─ receiveSessionResourceReleaseCommand()                      [session.cpp]
│     │   │     ├─ → GtpTask : NmGnbNgapToGtp::SESSION_RELEASE (per PSI)
│     │   │     ├─ deliverDownlinkNas() (if NAS PDU present)
│     │   │     └─ Send ASN_NGAP_PDUSessionResourceReleaseResponse
│     │   │
│     │   ├─ RerouteNASRequest → receiveRerouteNasRequest()                 [nas.cpp]
│     │   │  ├─ Decode enclosed InitialUEMessage
│     │   │  ├─ selectNewAmfForReAllocation()                               [nnsf.cpp]
│     │   │  └─ sendNgapUeAssociated() (re-route to new AMF)
│     │   │
│     │   ├─ AMFConfigurationUpdate → receiveAmfConfigurationUpdate()       [interface.cpp]
│     │   │  └─ Send Ack or Failure
│     │   │
│     │   ├─ OverloadStart → receiveOverloadStart()                         [interface.cpp]
│     │   ├─ OverloadStop  → receiveOverloadStop()                          [interface.cpp]
│     │   │
│     │   ├─ Paging → receivePaging()                                       [radio.cpp]
│     │   │  └─ → GnbRrcTask : NmGnbNgapToRrc::PAGING
│     │   │
│     │   └─ HandoverRequest → (logged, not implemented — target gNB role)
│     │
│     ├── SuccessfulOutcome dispatch:
│     │   ├─ NGSetupResponse → receiveNgSetupResponse()                     [interface.cpp]
│     │   │  ├─ AssignDefaultAmfConfigs() (name, capacity, GUAMI, PLMN support)
│     │   │  ├─ Mark AMF CONNECTED
│     │   │  ├─ → GnbAppTask : NmGnbStatusUpdate::NGAP_IS_UP
│     │   │  └─ → GnbRrcTask : NmGnbNgapToRrc::RADIO_POWER_ON
│     │   │
│     │   ├─ HandoverCommand → receiveHandoverCommand()                     [handover.cpp]
│     │   │  ├─ Extract TargetToSource-TransparentContainer (RRC container)
│     │   │  └─ → GnbRrcTask : NmGnbNgapToRrc::HANDOVER_COMMAND_DELIVERY
│     │   │
│     │   └─ PathSwitchRequestAcknowledge
│     │      └─ receivePathSwitchRequestAcknowledge()                       [handover.cpp]
│     │         └─ → GnbRrcTask : NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK
│     │
│     └── UnsuccessfulOutcome dispatch:
│         ├─ NGSetupFailure → receiveNgSetupFailure()                       [interface.cpp]
│         │
│         ├─ HandoverPreparationFailure
│         │  └─ receiveHandoverPreparationFailure()                         [handover.cpp]
│         │     └─ Log warning, UE remains on source cell
│         │
│         └─ PathSwitchRequestFailure
│            └─ receivePathSwitchRequestFailure()                           [handover.cpp]
│
└─ ASSOCIATION_SHUTDOWN
   └─ handleAssociationShutdown()                    [interface.cpp]
      ├─ Mark AMF NOT_CONNECTED
      ├─ → SctpTask : NmGnbSctp::CONNECTION_CLOSE
      └─ deleteAmfContext()
```

### 1.3 NGAP Transport Helpers

```
sendNgapNonUe(amfId, pdu)                            [transport.cpp]
├─ ASN constraint check
├─ ngap_encode::Encode() — APER
├─ → SctpTask : NmGnbSctp::SEND_MESSAGE
└─ nodeListener->onSend() (if monitoring)

sendNgapUeAssociated(ueId, pdu)                       [transport.cpp]
├─ Auto-attach AMF-UE-NGAP-ID, RAN-UE-NGAP-ID, UserLocationInformation IEs
├─ ASN constraint check
├─ ngap_encode::Encode() — APER
├─ → SctpTask : NmGnbSctp::SEND_MESSAGE
└─ nodeListener->onSend() (if monitoring)
```

---

## 2. GnbRrcTask — RRC Signaling (Uu Interface)

**Source:** `src/gnb/rrc/`  
**Class:** `GnbRrcTask` (extends `NtsTask`)

### 2.1 `onStart()` — System Info Timer

```
onStart()
└─ setTimer(TIMER_ID_SI_BROADCAST, 10 s)
```

### 2.2 `onLoop()` — Message Dispatch

#### 2.2.1 From RLS (`NmGnbRlsToRrc`)

```
onLoop() ── NtsMessageType::GNB_RLS_TO_RRC
└─ handleRlsSapMessage()                              [sap.cpp]
   ├─ SIGNAL_DETECTED
   │  └─ triggerSysInfoBroadcast()                    [broadcast.cpp]
   │     ├─ ConstructMibMessage() → sendRrcMessage(BCCH_BCH)
   │     │  └─ → GnbRlsTask : NmGnbRrcToRls::RRC_PDU_DELIVERY (ueId=0, broadcast)
   │     └─ ConstructSib1Message() → sendRrcMessage(BCCH_DL_SCH)
   │        └─ → GnbRlsTask : NmGnbRrcToRls::RRC_PDU_DELIVERY (ueId=0, broadcast)
   │
   └─ UPLINK_RRC
      └─ handleUplinkRrc()                            [channel.cpp]
         ├─ Decode PDU per channel:
         │  ├─ BCCH_BCH  → receiveRrcMessage(BCCH_BCH)     — (stub)
         │  ├─ UL_CCCH   → receiveRrcMessage(UL_CCCH)
         │  ├─ UL_CCCH1  → receiveRrcMessage(UL_CCCH1)     — (stub)
         │  └─ UL_DCCH   → receiveRrcMessage(UL_DCCH)
         │
         ├─ receiveRrcMessage(UL_CCCH)                [channel.cpp]
         │  └─ rrcSetupRequest
         │     └─ receiveRrcSetupRequest()            [connection.cpp]
         │        ├─ createUe()                       [ues.cpp]
         │        ├─ Build ASN_RRC_RRCSetup (CellGroupConfig)
         │        └─ sendRrcMessage(DL_CCCH)
         │           └─ → GnbRlsTask : NmGnbRrcToRls::RRC_PDU_DELIVERY
         │
         └─ receiveRrcMessage(UL_DCCH)                [channel.cpp]
            ├─ rrcSetupComplete
            │  └─ receiveRrcSetupComplete()           [connection.cpp]
            │     ├─ Parse 5G-S-TMSI if present
            │     ├─ → NgapTask : NmGnbRrcToNgap::INITIAL_NAS_DELIVERY
            │     └─ sendMeasConfig()                 [handover.cpp]
            │        ├─ Build RRCReconfiguration with MeasConfig
            │        │  ├─ MeasObject NR (SSB freq, smtc1)
            │        │  ├─ ReportConfig A3 event (3 dB offset, 1 dB hysteresis, 100ms TTT)
            │        │  └─ MeasId linking object→report
            │        └─ sendRrcMessage(DL_DCCH)
            │
            ├─ measurementReport
            │  └─ receiveMeasurementReport()          [handover.cpp]
            │     ├─ Parse serving cell RSRP
            │     ├─ Parse neighbour cell list (PCI, RSRP)
            │     ├─ Select best neighbour
            │     └─ evaluateHandoverDecision()
            │        └─ If best neighbour > serving + 3 dB:
            │           └─ → NgapTask : NmGnbRrcToNgap::HANDOVER_REQUIRED
            │
            ├─ rrcReconfigurationComplete
            │  └─ receiveRrcReconfigurationComplete() [handover.cpp]
            │     └─ If handoverInProgress:
            │        └─ handleHandoverComplete()
            │           └─ → NgapTask : NmGnbRrcToNgap::HANDOVER_NOTIFY
            │
            └─ ulInformationTransfer
               └─ receiveUplinkInformationTransfer()  [handler.cpp]
                  └─ deliverUplinkNas()
                     └─ → NgapTask : NmGnbRrcToNgap::UPLINK_NAS_DELIVERY
```

#### 2.2.2 From NGAP (`NmGnbNgapToRrc`)

```
onLoop() ── NtsMessageType::GNB_NGAP_TO_RRC
├─ RADIO_POWER_ON
│  ├─ m_isBarred = false
│  └─ triggerSysInfoBroadcast()
│
├─ NAS_DELIVERY
│  └─ handleDownlinkNasDelivery()                     [handler.cpp]
│     ├─ Build ASN_RRC_DLInformationTransfer (DedicatedNAS-Message)
│     └─ sendRrcMessage(DL_DCCH)
│        └─ → GnbRlsTask : NmGnbRrcToRls::RRC_PDU_DELIVERY
│
├─ AN_RELEASE
│  └─ releaseConnection()                             [handler.cpp]
│     ├─ Build ASN_RRC_RRCRelease
│     ├─ sendRrcMessage(DL_DCCH)
│     └─ Delete UE RRC context
│
├─ PAGING
│  └─ handlePaging()                                  [handler.cpp]
│     ├─ Build ASN_RRC_Paging (PagingRecord with 5G-S-TMSI)
│     └─ sendRrcMessage(PCCH)
│        └─ → GnbRlsTask : NmGnbRrcToRls::RRC_PDU_DELIVERY (broadcast)
│
├─ HANDOVER_COMMAND_DELIVERY
│  └─ handleNgapHandoverCommand()                     [handover.cpp]
│     └─ sendHandoverCommand()
│        ├─ Build RRCReconfiguration with ReconfigurationWithSync
│        │  ├─ newUE-Identity (C-RNTI)
│        │  ├─ t304 timer value
│        │  └─ spCellConfigCommon with target physCellId (PCI)
│        ├─ Record handover state in UE context
│        └─ sendRrcMessage(DL_DCCH)
│
└─ PATH_SWITCH_REQUEST_ACK
   └─ Log: handover fully complete
```

#### 2.2.3 Timer Expired

```
onLoop() ── NtsMessageType::TIMER_EXPIRED
└─ TIMER_ID_SI_BROADCAST (every 10s)
   ├─ Re-arm timer
   └─ onBroadcastTimerExpired()
      └─ triggerSysInfoBroadcast()
```

### 2.3 RRC Channel Send Helpers

```
sendRrcMessage(BCCH_BCH)       → encode → RLS (ueId=0, BCCH_BCH broadcast)
sendRrcMessage(BCCH_DL_SCH)    → encode → RLS (ueId=0, BCCH_DL_SCH broadcast)
sendRrcMessage(ueId, DL_CCCH)  → encode → RLS (unicast, DL_CCCH)
sendRrcMessage(ueId, DL_DCCH)  → encode → RLS (unicast, DL_DCCH)
sendRrcMessage(PCCH)           → encode → RLS (ueId=0, PCCH broadcast)
```

---

## 3. SctpTask — SCTP Transport

**Source:** `src/gnb/sctp/`  
**Class:** `SctpTask` (extends `NtsTask`)

### 3.1 `onLoop()` — Message Dispatch

```
onLoop() ── NtsMessageType::GNB_SCTP
├─ CONNECTION_REQUEST
│  └─ receiveSctpConnectionSetupRequest()
│     ├─ Create sctp::SctpClient
│     ├─ Bind & connect
│     ├─ Create SctpHandler callback → pushes messages back to SctpTask
│     └─ Spawn ReceiverThread (blocking receive loop)
│
├─ CONNECTION_CLOSE
│  └─ receiveConnectionClose()
│     └─ DeleteClientEntry() (cleanup client, handler, thread)
│
├─ ASSOCIATION_SETUP
│  └─ receiveAssociationSetup()
│     └─ → associatedTask (NgapTask) : NmGnbSctp::ASSOCIATION_SETUP
│
├─ ASSOCIATION_SHUTDOWN
│  └─ receiveAssociationShutdown()
│     └─ → associatedTask (NgapTask) : NmGnbSctp::ASSOCIATION_SHUTDOWN
│
├─ RECEIVE_MESSAGE
│  └─ receiveClientReceive()
│     └─ → associatedTask (NgapTask) : NmGnbSctp::RECEIVE_MESSAGE
│
├─ SEND_MESSAGE
│  └─ receiveSendMessage()
│     └─ sctp::SctpClient::send()          — write to SCTP socket
│
└─ UNHANDLED_NOTIFICATION
   └─ receiveUnhandledNotification()        — log warning
```

### 3.2 SctpHandler Callbacks (from ReceiverThread)

```
SctpHandler (ISctpHandler implementation)
├─ onAssociationSetup()       → push NmGnbSctp::ASSOCIATION_SETUP to SctpTask
├─ onAssociationShutdown()    → push NmGnbSctp::ASSOCIATION_SHUTDOWN
├─ onMessage()                → push NmGnbSctp::RECEIVE_MESSAGE
├─ onUnhandledNotification()  → push NmGnbSctp::UNHANDLED_NOTIFICATION
└─ onConnectionReset()        → push NmGnbSctp::UNHANDLED_NOTIFICATION
```

---

## 4. GtpTask — GTP-U Data Plane (N3 Interface)

**Source:** `src/gnb/gtp/`  
**Class:** `GtpTask` (extends `NtsTask`)

### 4.1 `onStart()`

```
onStart()
└─ Create and start UdpServerTask on GTP IP:2152
```

### 4.2 `onLoop()` — Message Dispatch

#### 4.2.1 From NGAP (`NmGnbNgapToGtp`)

```
onLoop() ── NtsMessageType::GNB_NGAP_TO_GTP
├─ UE_CONTEXT_UPDATE
│  └─ handleUeContextUpdate()
│     ├─ Create/update GtpUeContext (AMBR)
│     └─ updateAmbrForUe()
│        └─ RateLimiter::updateUeUplinkLimit/DownlinkLimit()
│
├─ UE_CONTEXT_RELEASE
│  └─ handleUeContextDelete()
│     ├─ Find all PDU sessions for UE
│     ├─ Remove from rate limiter, session table, session tree
│     └─ Remove UE context
│
├─ SESSION_CREATE
│  └─ handleSessionCreate()
│     ├─ Store PduSessionResource (uplink/downlink tunnel, QoS flows)
│     ├─ m_sessionTree.insert() — index by TEID
│     ├─ updateAmbrForUe()
│     └─ updateAmbrForSession()
│
└─ SESSION_RELEASE
   └─ handleSessionRelease()
      ├─ Remove session from rate limiter
      ├─ Remove from PDU session table
      └─ m_sessionTree.remove()
```

#### 4.2.2 From RLS — Uplink Data (`NmGnbRlsToGtp`)

```
onLoop() ── NtsMessageType::GNB_RLS_TO_GTP
└─ DATA_PDU_DELIVERY
   └─ handleUplinkData()
      ├─ Find PDU session by (ueId, psi)
      ├─ Rate limit check
      ├─ Build GTP-U G-PDU (TEID, QFI extension header)
      ├─ gtp::EncodeGtpMessage()
      └─ m_udpServer->send() to UPF address:2152
```

#### 4.2.3 From N3 — Downlink Data (UDP)

```
onLoop() ── NtsMessageType::UDP_SERVER_RECEIVE
└─ handleUdpReceive()
   ├─ gtp::DecodeGtpMessage()
   ├─ MT_G_PDU:
   │  ├─ Lookup session by downlink TEID
   │  ├─ Rate limit check
   │  └─ → GnbRlsTask : NmGnbGtpToRls::DATA_PDU_DELIVERY
   │
   └─ MT_ECHO_REQUEST:
      └─ Send MT_ECHO_RESPONSE back
```

---

## 5. GnbRlsTask — Radio Link Simulation (Main Coordinator)

**Source:** `src/gnb/rls/task.cpp`  
**Class:** `GnbRlsTask` (extends `NtsTask`)  
**Sub-tasks:** `RlsControlTask`, `RlsUdpTask`

### 5.1 `onStart()`

```
onStart()
├─ m_udpTask->start()   — begin RLS UDP receive loop
└─ m_ctlTask->start()   — begin RLS control (ACK management)
```

### 5.2 `onLoop()` — Message Dispatch

#### 5.2.1 Internal RLS messages (`NmGnbRlsToRls`) — bubbled up from sub-tasks

```
onLoop() ── NtsMessageType::GNB_RLS_TO_RLS
├─ SIGNAL_DETECTED
│  └─ → GnbRrcTask : NmGnbRlsToRrc::SIGNAL_DETECTED
│
├─ SIGNAL_LOST
│  └─ Log signal lost
│
├─ UPLINK_DATA
│  └─ → GtpTask : NmGnbRlsToGtp::DATA_PDU_DELIVERY
│
├─ UPLINK_RRC
│  └─ → GnbRrcTask : NmGnbRlsToRrc::UPLINK_RRC
│
├─ RADIO_LINK_FAILURE
│  └─ Log RLF cause
│
└─ TRANSMISSION_FAILURE
   └─ Log transmission failure
```

#### 5.2.2 From RRC — Downlink RRC (`NmGnbRrcToRls`)

```
onLoop() ── NtsMessageType::GNB_RRC_TO_RLS
└─ RRC_PDU_DELIVERY
   └─ → RlsControlTask : NmGnbRlsToRls::DOWNLINK_RRC
```

#### 5.2.3 From GTP — Downlink Data (`NmGnbGtpToRls`)

```
onLoop() ── NtsMessageType::GNB_GTP_TO_RLS
└─ DATA_PDU_DELIVERY
   └─ → RlsControlTask : NmGnbRlsToRls::DOWNLINK_DATA
```

---

## 6. RlsControlTask — RLS ACK and PDU Management

**Source:** `src/gnb/rls/ctl_task.cpp`  
**Class:** `RlsControlTask` (extends `NtsTask`)

### 6.1 `onLoop()`

```
onLoop() ── NtsMessageType::GNB_RLS_TO_RLS
├─ SIGNAL_DETECTED
│  └─ handleSignalDetected()
│     └─ → GnbRlsTask (main) : NmGnbRlsToRls::SIGNAL_DETECTED
│
├─ SIGNAL_LOST
│  └─ handleSignalLost()
│     └─ → GnbRlsTask (main) : NmGnbRlsToRls::SIGNAL_LOST
│
├─ RECEIVE_RLS_MESSAGE
│  └─ handleRlsMessage()
│     ├─ PDU_TRANSMISSION_ACK → clear PDU from m_pduMap
│     └─ PDU_TRANSMISSION
│        ├─ Track pduId for ACK
│        ├─ EPduType::DATA → → GnbRlsTask : NmGnbRlsToRls::UPLINK_DATA
│        └─ EPduType::RRC  → → GnbRlsTask : NmGnbRlsToRls::UPLINK_RRC
│
├─ DOWNLINK_RRC
│  └─ handleDownlinkRrcDelivery()
│     ├─ Store PDU in m_pduMap (for retransmission / ACK tracking)
│     ├─ Build rls::RlsPduTransmission (EPduType::RRC)
│     └─ m_udpTask->send()
│
└─ DOWNLINK_DATA
   └─ handleDownlinkDataDelivery()
      ├─ Build rls::RlsPduTransmission (EPduType::DATA)
      └─ m_udpTask->send()

onLoop() ── NtsMessageType::TIMER_EXPIRED
├─ TIMER_ID_ACK_CONTROL (1.5s)
│  └─ onAckControlTimerExpired()
│     └─ Expire stale PDUs (>3s) → NmGnbRlsToRls::TRANSMISSION_FAILURE
│
└─ TIMER_ID_ACK_SEND (2.25s)
   └─ onAckSendTimerExpired()
      └─ Send batch RlsPduTransmissionAck to UEs via m_udpTask->send()
```

---

## 7. RlsUdpTask — RLS UDP Transport

**Source:** `src/gnb/rls/udp_task.cpp`  
**Class:** `RlsUdpTask` (extends `NtsTask`)

### 7.1 `onLoop()` — Polling Loop

```
onLoop()
├─ heartbeatCycle() (every 1s)
│  ├─ Detect UEs not seen for >2s
│  └─ For lost UEs:
│     └─ → RlsControlTask : NmGnbRlsToRls::SIGNAL_LOST
│
└─ m_server->Receive() (200ms timeout)
   └─ receiveRlsPdu()
      ├─ HEARTBEAT
      │  ├─ EstimateSimulatedDbm() — distance-based signal model
      │  ├─ If new UE STI: assign ueId, → RlsControlTask : NmGnbRlsToRls::SIGNAL_DETECTED
      │  ├─ Update lastSeen timestamp
      │  └─ Send RlsHeartBeatAck (with dBm) back to UE
      │
      └─ Other message types
         └─ → RlsControlTask : NmGnbRlsToRls::RECEIVE_RLS_MESSAGE
```

### 7.2 `send(ueId, msg)` — Outbound RLS

```
send(ueId, msg)
├─ ueId == 0: broadcast to all known UEs
└─ ueId > 0: sendRlsPdu()
   ├─ rls::EncodeRlsMessage()
   └─ m_server->Send() — UDP
```

---

## 8. GnbAppTask — Application Management & CLI

**Source:** `src/gnb/app/`  
**Class:** `GnbAppTask` (extends `NtsTask`)

### 8.1 `onLoop()`

```
onLoop()
├─ NtsMessageType::GNB_STATUS_UPDATE
│  └─ NGAP_IS_UP
│     └─ Update m_statusInfo.isNgapUp
│
└─ NtsMessageType::GNB_CLI_COMMAND
   └─ GnbCmdHandler::handleCmd()           [cmd_handler.cpp]
      └─ Dispatch CLI commands (status, info, amf-list, ue-list, etc.)
```

---

## 5G Procedure Summary

The following table maps high-level 5G procedures to the gNB tasks and key methods involved.

| 5G Procedure | Tasks Involved | Key Methods |
|---|---|---|
| **NG Setup** | SCTP → NGAP | `sendNgSetupRequest()`, `receiveNgSetupResponse/Failure()` |
| **Initial Registration** | RLS → RRC → NGAP → SCTP | `receiveRrcSetupRequest()`, `receiveRrcSetupComplete()`, `handleInitialNasTransport()` |
| **NAS Transport (UL)** | RRC → NGAP | `receiveUplinkInformationTransfer()`, `deliverUplinkNas()`, `handleUplinkNasTransport()` |
| **NAS Transport (DL)** | NGAP → RRC → RLS | `receiveDownlinkNasTransport()`, `deliverDownlinkNas()`, `handleDownlinkNasDelivery()` |
| **Initial Context Setup** | NGAP → GTP, NGAP → RRC | `receiveInitialContextSetup()`, `setupPduSessionResource()` |
| **PDU Session Setup** | NGAP → GTP | `receiveSessionResourceSetupRequest()`, `setupPduSessionResource()` |
| **PDU Session Release** | NGAP → GTP | `receiveSessionResourceReleaseCommand()` |
| **UE Context Release** | NGAP → RRC, NGAP → GTP | `receiveContextRelease()`, `releaseConnection()` |
| **UE Context Modification** | NGAP → GTP | `receiveContextModification()` |
| **Paging** | NGAP → RRC → RLS | `receivePaging()`, `handlePaging()`, `sendRrcMessage(PCCH)` |
| **System Information Broadcast** | RRC → RLS | `triggerSysInfoBroadcast()`, `ConstructMibMessage()`, `ConstructSib1Message()` |
| **Radio Link Failure** | RRC → NGAP → GTP | `handleRadioLinkFailure()`, `sendContextRelease()` |
| **Measurement Config** | RRC → RLS | `sendMeasConfig()` (A3 event, RRCReconfiguration) |
| **Measurement Report** | RLS → RRC | `receiveMeasurementReport()`, `evaluateHandoverDecision()` |
| **Handover (source gNB)** | RRC → NGAP → SCTP, then NGAP → RRC → RLS | `evaluateHandoverDecision()` → `sendHandoverRequired()` → `receiveHandoverCommand()` → `sendHandoverCommand()` |
| **Handover (target gNB)** | RRC → NGAP → SCTP | `receiveRrcReconfigurationComplete()` → `handleHandoverComplete()` → `sendHandoverNotify()` + `sendPathSwitchRequest()` |
| **Path Switch** | NGAP → RRC | `receivePathSwitchRequestAcknowledge/Failure()` |
| **Uplink User Data** | RLS → GTP → N3 | `handleUplinkData()`, GTP-U encode, UDP send |
| **Downlink User Data** | N3 → GTP → RLS | `handleUdpReceive()`, `handleDownlinkDataDelivery()` |
| **AMF Config Update** | SCTP → NGAP | `receiveAmfConfigurationUpdate()` |
| **Overload Control** | SCTP → NGAP | `receiveOverloadStart/Stop()` |
| **NAS Reroute** | NGAP | `receiveRerouteNasRequest()`, `selectNewAmfForReAllocation()` |
| **Error Indication** | NGAP ↔ AMF | `sendErrorIndication()`, `receiveErrorIndication()` |

---

## Source File Index

| File | Task | Responsibility |
|---|---|---|
| `src/gnb/ngap/task.cpp` | NgapTask | Main loop, message dispatch |
| `src/gnb/ngap/interface.cpp` | NgapTask | NG Setup, AMF association, overload, AMF config update |
| `src/gnb/ngap/transport.cpp` | NgapTask | NGAP PDU encode/decode, SCTP message routing |
| `src/gnb/ngap/nas.cpp` | NgapTask | Initial/Uplink/Downlink NAS transport, Reroute |
| `src/gnb/ngap/context.cpp` | NgapTask | Initial Context Setup, UE Context Release/Modification |
| `src/gnb/ngap/session.cpp` | NgapTask | PDU Session Setup/Release |
| `src/gnb/ngap/radio.cpp` | NgapTask | Radio link failure, Paging |
| `src/gnb/ngap/handover.cpp` | NgapTask | Handover Required/Command/Notify, PathSwitch |
| `src/gnb/ngap/management.cpp` | NgapTask | AMF/UE context CRUD |
| `src/gnb/ngap/nnsf.cpp` | NgapTask | AMF selection (NAS Node Selection Function) |
| `src/gnb/ngap/encode.cpp/.hpp` | NgapTask | APER encode/decode wrappers |
| `src/gnb/ngap/utils.cpp/.hpp` | NgapTask | ASN↔domain type helpers |
| `src/gnb/rrc/task.cpp` | GnbRrcTask | Main loop, message dispatch |
| `src/gnb/rrc/broadcast.cpp` | GnbRrcTask | MIB/SIB1 construction and broadcast |
| `src/gnb/rrc/channel.cpp` | GnbRrcTask | RRC PDU encode/decode per channel, message routing |
| `src/gnb/rrc/connection.cpp` | GnbRrcTask | RRC Setup Request/Complete |
| `src/gnb/rrc/handler.cpp` | GnbRrcTask | DL NAS delivery, UL info transfer, RRC Release, RLF, Paging |
| `src/gnb/rrc/handover.cpp` | GnbRrcTask | MeasConfig, MeasReport, HO command, HO complete |
| `src/gnb/rrc/sap.cpp` | GnbRrcTask | RLS service access point handler |
| `src/gnb/rrc/ues.cpp` | GnbRrcTask | UE context CRUD |
| `src/gnb/rrc/management.cpp` | GnbRrcTask | Transaction ID counter |
| `src/gnb/gtp/task.cpp` | GtpTask | GTP-U data plane, session/context management |
| `src/gnb/rls/task.cpp` | GnbRlsTask | RLS coordinator, routes between RRC/GTP and sub-tasks |
| `src/gnb/rls/ctl_task.cpp` | RlsControlTask | PDU ACK tracking, retransmission, RLS message handling |
| `src/gnb/rls/udp_task.cpp` | RlsUdpTask | Heartbeat, signal detection, UDP I/O |
| `src/gnb/sctp/task.cpp` | SctpTask | SCTP connection lifecycle, send/receive |
| `src/gnb/app/task.cpp` | GnbAppTask | Status updates, CLI commands |
