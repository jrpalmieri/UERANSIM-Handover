# gNB Operations Reference

This document describes the operations that the UERANSIM **gNB (gNodeB)** performs when communicating with **UEs** (over the Uu/RLS interface) and with the **AMF** (over the N2/NGAP interface). For each operation it lists the **triggering event**, the **5G procedure context**, and the **functions** (with source file locations) that implement the behaviour.

> **Scope:** This covers the UERANSIM-Handover fork of UERANSIM, which extends the upstream project with measurement configuration, measurement reporting, and N2-based intra-5G-SA handover.

> **Depth convention:** For each operation the *top-level handler* is shown, along with the key *sub-functions* it invokes (two levels). Lower-level utility calls (ASN.1 encode/decode wrappers, NTS push helpers) are described once in the Architecture section and not repeated.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [gNB Startup](#2-gnb-startup)
3. [NG Setup — Establishing the AMF Connection](#3-ng-setup--establishing-the-amf-connection)
4. [System Information Broadcast (MIB / SIB1)](#4-system-information-broadcast-mib--sib1)
5. [Receiving a Registration Request from a UE](#5-receiving-a-registration-request-from-a-ue)
6. [Initial Context Setup (AMF → gNB)](#6-initial-context-setup-amf--gnb)
7. [NAS Transport (Uplink and Downlink)](#7-nas-transport-uplink-and-downlink)
8. [PDU Session Resource Setup](#8-pdu-session-resource-setup)
9. [PDU Session Resource Release](#9-pdu-session-resource-release)
10. [UE Context Release](#10-ue-context-release)
11. [UE Context Modification](#11-ue-context-modification)
12. [Paging](#12-paging)
13. [Measurement Configuration](#13-measurement-configuration)
14. [Measurement Report Handling](#14-measurement-report-handling)
15. [Receiving a Handover Request from a UE (Measurement-Triggered Handover)](#15-receiving-a-handover-request-from-a-ue-measurement-triggered-handover)
16. [Handover Command Delivery (AMF → Source gNB → UE)](#16-handover-command-delivery-amf--source-gnb--ue)
17. [Handover Completion and Path Switch](#17-handover-completion-and-path-switch)
18. [Radio Link Failure](#18-radio-link-failure)
19. [User-Plane Data Forwarding (GTP-U)](#19-user-plane-data-forwarding-gtp-u)
20. [AMF Configuration Update](#20-amf-configuration-update)
21. [Overload Control](#21-overload-control)
22. [NAS Reroute](#22-nas-reroute)
23. [Source File Index](#23-source-file-index)

---

## 1. Architecture Overview

The gNB is built as a set of cooperating **NTS (Non-blocking Task Signaling) tasks**, each running in its own thread. Inter-task communication uses typed NTS messages pushed to per-task queues.

```
┌─────────────┐
│  GnbAppTask │  Status display, CLI commands
└──────┬──────┘
       │
┌──────┴──────┐                         ┌──────────────┐              ┌───────────┐
│  NgapTask   │◄───────────────────────►│  SctpTask    │◄────────────►│   AMF     │
│  (NGAP/N2)  │                         │  (SCTP)      │              │   (N2)    │
└──────┬──────┘                         └──────────────┘              └───────────┘
       │
┌──────┴──────┐                         ┌──────────────┐
│  GnbRrcTask │◄───────────────────────►│  GnbRlsTask  │  (Radio Link Simulation)
│  (RRC / Uu) │                         │  ├─ RlsControlTask
└─────────────┘                         │  └─ RlsUdpTask
                                        └──────┬───────┘
       ┌─────────────┐                         │
       │  GtpTask    │◄────────────────────────┘
       │  (GTP-U/N3) │◄──── UDP to/from UPF
       └─────────────┘
```

### Key NTS Message Types

| Message | Direction | Purpose |
|---------|-----------|---------|
| `NmGnbRlsToRrc` | RLS → RRC | SIGNAL_DETECTED, UPLINK_RRC |
| `NmGnbRrcToRls` | RRC → RLS | RRC_PDU_DELIVERY (downlink RRC) |
| `NmGnbRrcToNgap` | RRC → NGAP | INITIAL_NAS_DELIVERY, UPLINK_NAS_DELIVERY, RADIO_LINK_FAILURE, HANDOVER_REQUIRED, HANDOVER_NOTIFY |
| `NmGnbNgapToRrc` | NGAP → RRC | RADIO_POWER_ON, NAS_DELIVERY, AN_RELEASE, PAGING, HANDOVER_COMMAND_DELIVERY, PATH_SWITCH_REQUEST_ACK |
| `NmGnbNgapToGtp` | NGAP → GTP | UE_CONTEXT_UPDATE, UE_CONTEXT_RELEASE, SESSION_CREATE, SESSION_RELEASE |
| `NmGnbSctp` | NGAP ↔ SCTP | CONNECTION_REQUEST, ASSOCIATION_SETUP/SHUTDOWN, SEND/RECEIVE_MESSAGE |
| `NmGnbRlsToGtp` | RLS → GTP | DATA_PDU_DELIVERY (uplink user data) |
| `NmGnbGtpToRls` | GTP → RLS | DATA_PDU_DELIVERY (downlink user data) |

### Common Helper Functions

| Helper | File | Purpose |
|--------|------|---------|
| `sendNgapNonUe()` | `src/gnb/ngap/transport.cpp` | APER-encode an NGAP PDU and push to SCTP (non-UE-associated) |
| `sendNgapUeAssociated()` | `src/gnb/ngap/transport.cpp` | Same as above but auto-attaches RAN-UE-NGAP-ID, AMF-UE-NGAP-ID, and UserLocationInformation IEs |
| `sendRrcMessage()` | `src/gnb/rrc/channel.cpp` | UPER-encode an RRC PDU and push to RLS for radio delivery |
| `ngap_encode::Encode()` / `Decode()` | `src/gnb/ngap/encode.cpp` | APER encode/decode wrappers for NGAP ASN.1 |
| `rrc::encode::EncodeS()` | `src/lib/rrc/encode.cpp` | UPER encode wrapper for RRC ASN.1 |
| `findUeContext()` | `src/gnb/ngap/management.cpp` | Look up NGAP UE context by ID |
| `findUe()` | `src/gnb/rrc/ues.cpp` | Look up RRC UE context by ID |
| `selectAmf()` | `src/gnb/ngap/nnsf.cpp` | Select an AMF based on slice support (NSSF-like) |

---

## 2. gNB Startup

### Triggering Event
**User launches the `nr-gnb` binary** with a YAML configuration file (e.g. `./nr-gnb -c config/free5gc-gnb.yaml`).

### 5G Context
Before the gNB can serve any UE it must: read its configuration, instantiate all internal tasks, and begin the NG Setup procedure toward the AMF.

### Sequence of Operations

| Step | Function / Action | Source File |
|------|-------------------|-------------|
| 1 | `main()` is entered; `app::Initialize()` sets up signal handlers | `src/gnb.cpp` |
| 2 | `ReadOptions()` parses CLI flags (`-c`, `-l`) | `src/gnb.cpp` |
| 3 | `ReadConfigYaml()` loads YAML → `GnbConfig` (PLMN, NCI, TAC, slice list, AMF addresses, IPs) | `src/gnb.cpp` |
| 4 | `GNodeB` constructor creates a `TaskBase` and instantiates all six NTS tasks: `GnbAppTask`, `SctpTask`, `NgapTask`, `GnbRrcTask`, `GtpTask`, `GnbRlsTask` | `src/gnb/gnb.cpp` |
| 5 | `GNodeB::start()` calls `.start()` on each task in order: App → SCTP → NGAP → RRC → RLS → GTP | `src/gnb/gnb.cpp` |
| 6 | Each task's `onStart()` fires (see below) | Various |
| 7 | CLI server begins accepting commands (unless `--disable-cmd`) | `src/gnb.cpp` |
| 8 | `main()` enters infinite `Loop()` dispatching CLI messages | `src/gnb.cpp` |

### Runtime CLI Example: Neighbor Updates

The gNB supports runtime neighbor changes through:

`neighbors <json-payload>`

`mode` must be one of `replace`, `add`, or `remove`.

- `replace`: replace all current neighbors with the provided list.
- `add`: add neighbors from the provided list.
- `remove`: remove neighbors that match by PCI (PCI is derived from `nci` and `idLength`).

JSON fields for each neighbor entry align with config file fields:

- Required for `replace` and `add`: `nci`, `idLength`, `tac`, `ipAddress`
- Optional: `handoverInterface` (`N2` or `Xn`), `xnAddress`, `xnPort`
- For `remove`, only `nci` and `idLength` are required.

Examples with `nr-cli`:

```bash
# Replace full neighbor list
nr-cli UERANSIM-gnb-286-1-1 -e \
       'neighbors {"mode":"replace","neighbors":[{"nci":2,"idLength":32,"tac":1,"ipAddress":"127.0.0.2","handoverInterface":"N2"}]}'

# Add one neighbor
nr-cli UERANSIM-gnb-286-1-1 -e \
       'neighbors {"mode":"add","neighbors":[{"nci":3,"idLength":32,"tac":1,"ipAddress":"127.0.0.3","handoverInterface":"Xn","xnAddress":"127.0.0.3","xnPort":9487}]}'

# Remove one neighbor by PCI identity derived from nci/idLength
nr-cli UERANSIM-gnb-286-1-1 -e 'neighbors {"mode":"remove","neighbors":[{"nci":2,"idLength":32}]}'
```

Quoting guidance:

- Prefer wrapping the full CLI expression in single quotes on Linux/macOS shells.
- Keep JSON compact (no unescaped inner single quotes).
- If generating commands from Python, pass the full `-e` argument string directly via `subprocess.run`.

### Runtime CLI Example: Multi-Satellite SIB19 Updates

The gNB supports runtime satellite ephemeris updates for SIB19 through:

`sat-loc-pv <json-payload>`

`sat-loc-pv` semantics:

- Upsert by PCI: if `pci` exists, overwrite it; otherwise insert a new record.
- Fields in payload are required: `pci`, `x`, `y`, `z`, `vx`, `vy`, `vz`, `epochMs`.
- Position is ECEF meters, velocity is ECEF m/s, and `epochMs` is Unix time in milliseconds.

SIB19 broadcast behavior:

- On each SIB19 timer tick, all satellite entries updated within the configured threshold are broadcast.
- Entries older than threshold are removed before encoding.
- Threshold is configured in YAML under `ntn.sib19.satLocUpdateThresholdMs`.

Example YAML:

```yaml
ntn:
       sib19:
              sib19on: true
              sib19timing: 200
              satLocUpdateThresholdMs: 1000
```

Time warp configuration for NTN simulations:

```yaml
ntn:
       timeWarp:
              # Choose exactly one:
              # offsetMs: -3600000
              targetTimeEpoch: "23162.59097222"  # TLE epoch format YYDDD.DDDDDDDD (UTC)
```

Notes:

- `targetTimeEpoch` is resolved once at startup into an offset from the current wall-clock time.
- The computed offset is then used throughout the simulation to shift the internal time base.

Examples with `nr-cli`:

```bash
# Insert or update PCI 101
nr-cli UERANSIM-gnb-286-1-1 -e \
       'sat-loc-pv {"pci":101,"x":6871000,"y":100,"z":50,"vx":0,"vy":7450,"vz":0,"epochMs":1713091200000}'

# Insert or update PCI 202
nr-cli UERANSIM-gnb-286-1-1 -e \
       'sat-loc-pv {"pci":202,"x":6871500,"y":120,"z":60,"vx":0,"vy":7445,"vz":0,"epochMs":1713091200100}'

# Alternative: read payload from a file
nr-cli UERANSIM-gnb-286-1-1 -e "sat-loc-pv $(cat sat-entry.json)"
```

Quoting guidance:

- Prefer single quotes around the whole `-e` expression in Linux/macOS shells.
- Keep JSON keys and string values in double quotes.

### Per-Task `onStart()` Actions

| Task | `onStart()` Action |
|------|--------------------|
| **NgapTask** | For each AMF in the config, calls `createAmfContext()` then pushes `NmGnbSctp::CONNECTION_REQUEST` to SctpTask — initiating SCTP connections to all AMFs |
| **SctpTask** | (No special onStart; waits for CONNECTION_REQUEST messages) |
| **GnbRrcTask** | Arms `TIMER_ID_SI_BROADCAST` (10-second periodic timer) for system information broadcast; sets `m_isBarred = true` (cell barred until NGAP is up) |
| **GnbRlsTask** | Starts sub-tasks `RlsUdpTask` (UDP receive loop + heartbeat) and `RlsControlTask` (ACK management, PDU retransmission) |
| **GtpTask** | Creates and starts a `UdpServerTask` listening on the gNB's GTP IP, port 2152 |
| **GnbAppTask** | (No special onStart; waits for status updates and CLI commands) |

### Key Data Structures Created

- **`GnbConfig`** — holds PLMN, NCI, TAC, slice list, AMF addresses, link/NGAP/GTP IPs
- **`TaskBase`** — shared struct holding pointers to all tasks and config; passed to every task constructor
- **`NgapAmfContext`** — one per AMF, tracks SCTP association state, AMF name, GUAMI list, PLMN support

---

## 3. NG Setup — Establishing the AMF Connection

### Triggering Event
**SCTP association completes** — `SctpTask` receives an association-setup callback from the SCTP layer and pushes `NmGnbSctp::ASSOCIATION_SETUP` to `NgapTask`.

### Functions Involved

| Step | Function | File | Description |
|------|----------|------|-------------|
| 1 | `NgapTask::handleAssociationSetup()` | `src/gnb/ngap/interface.cpp` | Records the SCTP association (streams, association ID) in the `NgapAmfContext` |
| 2 | `NgapTask::sendNgSetupRequest()` | `src/gnb/ngap/interface.cpp` | Builds `ASN_NGAP_NGSetupRequest` with GlobalRANNodeID (gNB-ID), RANNodeName, SupportedTAList (PLMN + TAC + S-NSSAI slices), DefaultPagingDRX |
| 3 | `sendNgapNonUe()` | `src/gnb/ngap/transport.cpp` | APER-encodes and pushes to SCTP |
| 4 | *(AMF responds)* | — | — |
| 5a | `NgapTask::receiveNgSetupResponse()` | `src/gnb/ngap/interface.cpp` | Parses AMF name, relative capacity, GUAMI list, PLMN support; marks AMF **CONNECTED**; pushes `NmGnbStatusUpdate::NGAP_IS_UP` to AppTask and `NmGnbNgapToRrc::RADIO_POWER_ON` to RrcTask |
| 5b | `NgapTask::receiveNgSetupFailure()` | `src/gnb/ngap/interface.cpp` | Logs error; AMF remains NOT_CONNECTED |

### Effect of RADIO_POWER_ON
When `GnbRrcTask` receives `RADIO_POWER_ON`, it sets `m_isBarred = false` and calls `triggerSysInfoBroadcast()`, allowing UEs to detect and camp on the cell.

---

## 4. System Information Broadcast (MIB / SIB1)

### Triggering Events
1. **`RADIO_POWER_ON`** received from NGAP (cell unbarred after NG Setup success)
2. **`TIMER_ID_SI_BROADCAST` expires** (every 10 seconds)
3. **`SIGNAL_DETECTED`** from RLS (a new UE heartbeat is detected)

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `GnbRrcTask::triggerSysInfoBroadcast()` | `src/gnb/rrc/broadcast.cpp` | Entry point — constructs and sends MIB and SIB1 |
| `ConstructMibMessage()` | `src/gnb/rrc/broadcast.cpp` | Builds `ASN_RRC_BCCH_BCH_Message` with MIB (cell barred status, SCS, DMRS position) |
| `ConstructSib1Message()` | `src/gnb/rrc/broadcast.cpp` | Builds `ASN_RRC_BCCH_DL_SCH_Message` with SIB1 (PLMN ID, TAC, NCI, cell reservation status, UAC barring sets) |
| `sendRrcMessage(BCCH_BCH)` | `src/gnb/rrc/channel.cpp` | UPER-encodes MIB and pushes to RLS as a broadcast (ueId=0) |
| `sendRrcMessage(BCCH_DL_SCH)` | `src/gnb/rrc/channel.cpp` | UPER-encodes SIB1 and pushes to RLS as a broadcast (ueId=0) |

---

## 5. Receiving a Registration Request from a UE

### Triggering Event
**UE sends an RRCSetupRequest** over UL-CCCH → received by `GnbRlsTask` over the RLS UDP link → forwarded as `NmGnbRlsToRrc::UPLINK_RRC` to `GnbRrcTask`.

### 5G Context
Registration is the first control-plane procedure a UE performs after cell selection. The gNB acts as a relay between the UE's NAS layer and the AMF, while also managing the RRC connection and measurement configuration.

### End-to-End Flow

```
UE                        gNB (RRC)              gNB (NGAP)              AMF
 │                           │                       │                    │
 │── RRCSetupRequest ───────►│                       │                    │
 │                           │ createUe()            │                    │
 │◄── RRCSetup ─────────────│                       │                    │
 │                           │                       │                    │
 │── RRCSetupComplete ──────►│                       │                    │
 │   (+ embedded NAS PDU)    │── INITIAL_NAS_DELIVERY►│                   │
 │                           │                       │── InitialUEMessage─►│
 │                           │                       │                    │
 │                           │◄─ NAS_DELIVERY ───────│◄─ DL NAS Transport─│
 │◄── DLInformationTransfer─│                       │                    │
 │   (NAS Auth Request)      │                       │                    │
 │                           │                       │                    │
 │── ULInformationTransfer──►│                       │                    │
 │   (NAS Auth Response)     │── UPLINK_NAS_DELIVERY─►│                   │
 │                           │                       │── UL NAS Transport─►│
 │                           │                       │                    │
 │                           │                       │◄─InitialCtxSetup──│
 │                           │◄── NAS_DELIVERY ──────│                    │
 │◄── DLInformationTransfer─│                       │                    │
 │   (NAS Registration Accept)                       │                    │
 │                           │                       │                    │
 │── RRCReconfiguration ───►│ (MeasConfig response)  │                    │
 │   Complete                │                       │                    │
```

### Functions Involved (Step-by-Step)

#### Step 1: RRC Setup Request → RRC Setup Response

| Function | File | Description |
|----------|------|-------------|
| `GnbRrcTask::handleRlsSapMessage()` | `src/gnb/rrc/sap.cpp` | Dispatches SIGNAL_DETECTED and UPLINK_RRC from RLS |
| `GnbRrcTask::handleUplinkRrc()` | `src/gnb/rrc/channel.cpp` | Decodes the uplink RRC PDU per channel type (UL_CCCH in this case) |
| `GnbRrcTask::receiveRrcSetupRequest()` | `src/gnb/rrc/connection.cpp` | Creates a new `RrcUeContext` via `createUe()`; extracts the UE identity (ng-5G-S-TMSI-Part1 or random value) and establishment cause; builds and sends `ASN_RRC_RRCSetup` with a `CellGroupConfig` on DL-CCCH |
| `createUe()` | `src/gnb/rrc/ues.cpp` | Allocates a new `RrcUeContext` and adds it to the UE map |

#### Step 2: RRC Setup Complete → Initial NAS Transport to AMF

| Function | File | Description |
|----------|------|-------------|
| `GnbRrcTask::receiveRrcSetupComplete()` | `src/gnb/rrc/connection.cpp` | Extracts the dedicated NAS message and optional 5G-S-TMSI from the `RRCSetupComplete`; pushes `NmGnbRrcToNgap::INITIAL_NAS_DELIVERY` to NgapTask; then calls `sendMeasConfig()` to configure measurements for handover |
| `NgapTask::handleInitialNasTransport()` | `src/gnb/ngap/nas.cpp` | Calls `extractSliceInfoAndModifyPdu()` to decode the NAS Registration Request and extract the requested S-NSSAI; calls `createUeContext()` which invokes `selectAmf()` to pick the best AMF for the UE's slice; builds `ASN_NGAP_InitialUEMessage` (NAS-PDU, RRC Establishment Cause, 5G-S-TMSI); sends via `sendNgapUeAssociated()` |
| `extractSliceInfoAndModifyPdu()` | `src/gnb/ngap/nas.cpp` | NAS-decodes the Registration Request, extracts the first requested SST, then strips NSSAI from the NAS PDU (to avoid duplication with the NGAP IE) |
| `createUeContext()` | `src/gnb/ngap/management.cpp` | Creates `NgapUeContext`, assigns `ranUeNgapId`, links to selected AMF |
| `selectAmf()` | `src/gnb/ngap/nnsf.cpp` | NSSF-like AMF selection: matches the UE's requested SST against each AMF's PLMN support list; falls back to first connected AMF |

#### Step 3: Subsequent NAS Exchanges (Authentication, Security, Registration Accept)

After the initial UE message, the AMF drives the NAS procedure. The gNB transparently relays NAS PDUs:

- **Downlink:** AMF sends `DownlinkNASTransport` → `receiveDownlinkNasTransport()` → `deliverDownlinkNas()` → RRC `handleDownlinkNasDelivery()` → `DLInformationTransfer` to UE
- **Uplink:** UE sends `ULInformationTransfer` → `receiveUplinkInformationTransfer()` → `deliverUplinkNas()` → NGAP `handleUplinkNasTransport()` → `UplinkNASTransport` to AMF

See [Section 7](#7-nas-transport-uplink-and-downlink) for details.

---

## 6. Initial Context Setup (AMF → gNB)

### Triggering Event
**AMF sends `InitialContextSetupRequest`** via SCTP → decoded in `NgapTask::handleSctpMessage()`.

### 5G Context
After authentication and security are complete, the AMF instructs the gNB to establish the UE's signaling context (and optionally one or more PDU sessions).

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receiveInitialContextSetup()` | `src/gnb/ngap/context.cpp` | Parses the message; pushes `NmGnbNgapToGtp::UE_CONTEXT_UPDATE` to GtpTask (creates GTP UE context with AMBR); processes any embedded `PDUSessionResourceSetupListCxtReq` by calling `setupPduSessionResource()` for each; delivers piggybacked NAS PDU via `deliverDownlinkNas()`; responds with `InitialContextSetupResponse` |
| `setupPduSessionResource()` | `src/gnb/ngap/session.cpp` | Allocates a downlink TEID; creates a `PduSessionResource` record; pushes `NmGnbNgapToGtp::SESSION_CREATE` to GtpTask |
| `deliverDownlinkNas()` | `src/gnb/ngap/nas.cpp` | Pushes `NmGnbNgapToRrc::NAS_DELIVERY` → RRC delivers via `DLInformationTransfer` |

---

## 7. NAS Transport (Uplink and Downlink)

### 7a. Uplink NAS Delivery

| Triggering Event | UE sends `ULInformationTransfer` (RRC UL-DCCH) |
|---|---|
| `receiveUplinkInformationTransfer()` | `src/gnb/rrc/handler.cpp` — extracts dedicatedNAS-Message |
| `deliverUplinkNas()` | `src/gnb/rrc/handler.cpp` — pushes `NmGnbRrcToNgap::UPLINK_NAS_DELIVERY` |
| `handleUplinkNasTransport()` | `src/gnb/ngap/nas.cpp` — builds `ASN_NGAP_UplinkNASTransport`, sends to AMF |

### 7b. Downlink NAS Delivery

| Triggering Event | AMF sends `DownlinkNASTransport` via SCTP |
|---|---|
| `receiveDownlinkNasTransport()` | `src/gnb/ngap/nas.cpp` — extracts NAS-PDU from NGAP message |
| `deliverDownlinkNas()` | `src/gnb/ngap/nas.cpp` — pushes `NmGnbNgapToRrc::NAS_DELIVERY` |
| `handleDownlinkNasDelivery()` | `src/gnb/rrc/handler.cpp` — builds `DLInformationTransfer` and sends via `sendRrcMessage(DL_DCCH)` |

---

## 8. PDU Session Resource Setup

### Triggering Event
**AMF sends `PDUSessionResourceSetupRequest`** via SCTP (separate from Initial Context Setup).

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `receiveSessionResourceSetupRequest()` | `src/gnb/ngap/session.cpp` | Iterates over `PDUSessionResourceSetupListSUReq`; for each session calls `setupPduSessionResource()` |
| `setupPduSessionResource()` | `src/gnb/ngap/session.cpp` | Allocates downlink TEID, creates `PduSessionResource`, pushes `SESSION_CREATE` to GtpTask |
| `GtpTask::handleSessionCreate()` | `src/gnb/gtp/task.cpp` | Stores the session (up/down tunnel, QoS flows), indexes by TEID in `m_sessionTree`, updates AMBR rate limiters |
| Responds with `PDUSessionResourceSetupResponse` | `src/gnb/ngap/session.cpp` | Encodes the response with per-session transfer addresses and sends to AMF |

---

## 9. PDU Session Resource Release

### Triggering Event
**AMF sends `PDUSessionResourceReleaseCommand`** via SCTP.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `receiveSessionResourceReleaseCommand()` | `src/gnb/ngap/session.cpp` | For each PSI: pushes `SESSION_RELEASE` to GtpTask; delivers piggybacked NAS PDU if present; responds with `PDUSessionResourceReleaseResponse` |
| `GtpTask::handleSessionRelease()` | `src/gnb/gtp/task.cpp` | Removes session from rate limiter, PDU session table, and TEID tree |

---

## 10. UE Context Release

### Triggering Event
**AMF sends `UEContextReleaseCommand`** via SCTP.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receiveContextRelease()` | `src/gnb/ngap/context.cpp` | Pushes `AN_RELEASE` to RrcTask and `UE_CONTEXT_RELEASE` to GtpTask; sends `UEContextReleaseComplete` to AMF; calls `deleteUeContext()` |
| `GnbRrcTask::releaseConnection()` | `src/gnb/rrc/handler.cpp` | Builds `ASN_RRC_RRCRelease`, sends to UE via DL-DCCH; erases UE from the RRC context map |
| `GtpTask::handleUeContextDelete()` | `src/gnb/gtp/task.cpp` | Removes all PDU sessions and the GTP UE context |

---

## 11. UE Context Modification

### Triggering Event
**AMF sends `UEContextModificationRequest`** via SCTP.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receiveContextModification()` | `src/gnb/ngap/context.cpp` | Updates UE AMBR and/or AMF-UE-NGAP-ID; responds with `UEContextModificationResponse`; pushes `UE_CONTEXT_UPDATE` to GtpTask to update rate limiters |

---

## 12. Paging

### Triggering Event
**AMF sends `Paging`** via SCTP (for a UE in RRC Idle).

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receivePaging()` | `src/gnb/ngap/radio.cpp` | Extracts 5G-S-TMSI and TAI list; pushes `NmGnbNgapToRrc::PAGING` to RrcTask |
| `GnbRrcTask::handlePaging()` | `src/gnb/rrc/handler.cpp` | Builds `ASN_RRC_Paging` with a `PagingRecord` containing the 5G-S-TMSI; sends via `sendRrcMessage(PCCH)` as a broadcast |

---

## 13. Measurement Configuration

### Triggering Event
**RRC Setup Complete received** — immediately after the UE completes RRC setup, `receiveRrcSetupComplete()` calls `sendMeasConfig()`.

### 5G Context
The gNB configures the UE to perform neighbour cell measurements so that it can trigger handover when a better cell is detected. This uses the 3GPP **A3 event** (neighbour cell becomes better than serving cell by a configurable offset).

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `GnbRrcTask::sendMeasConfig()` | `src/gnb/rrc/handover.cpp` | Builds an `RRCReconfiguration` with `MeasConfig` containing: |
| | | • **MeasObjectNR** (measObjectId=1): SSB frequency 632628 (n78 band), SCS=30kHz, SMTC periodicity=20ms |
| | | • **ReportConfigNR** (reportConfigId=1): A3 event with a3-Offset=6 (3 dB in 0.5 dB steps), hysteresis=2 (1 dB), TTT=100ms, report rsrp, maxReportCells=4 |
| | | • **MeasId** (measId=1): links measObjectId=1 to reportConfigId=1 |
| | | Sets `ue->measConfigSent = true` to avoid resending. Sends via `sendRrcMessage(DL_DCCH)`. |

---

## 14. Measurement Report Handling

### Triggering Event
**UE sends `MeasurementReport`** over UL-DCCH → decoded and dispatched as `measurementReport` within `receiveRrcMessage(UL_DCCH)`.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `GnbRrcTask::receiveMeasurementReport()` | `src/gnb/rrc/handover.cpp` | Extracts the serving cell RSRP from `measResultServingMOList` (converted from RSRP range to dBm); iterates `measResultListNR` neighbour cells to find the one with the highest RSRP; stores `lastMeasReportPci`, `lastMeasReportRsrp`, and `lastServingRsrp` in the UE context; then calls `evaluateHandoverDecision()` |
| `rsrpRangeToDbm()` | `src/gnb/rrc/handover.cpp` | Converts 3GPP RSRP range integer (0–127) to dBm value |

---

## 15. Receiving a Handover Request from a UE (Measurement-Triggered Handover)

### Triggering Event
**`evaluateHandoverDecision()` is called** after a Measurement Report is processed. If the best neighbour's RSRP exceeds the serving cell's RSRP by more than the A3 offset (3 dB), the gNB decides to initiate handover.

> **Note:** In UERANSIM, this is an N2-based handover (no Xn interface). The "handover request" originates from the UE's measurement report, not from an explicit handover request message. The gNB autonomously decides to hand over based on measurement data.

### 5G Context
This is the **source gNB side of an N2 handover**. The flow is:

```
UE                    Source gNB (RRC)         Source gNB (NGAP)           AMF
 │                         │                        │                      │
 │─ MeasurementReport ───►│                        │                      │
 │                         │ evaluateHandoverDecision()                    │
 │                         │── HANDOVER_REQUIRED ──►│                      │
 │                         │                        │── HandoverRequired ─►│
 │                         │                        │                      │
 │                         │                        │◄─ HandoverCommand ──│
 │                         │◄─ HO_COMMAND_DELIVERY──│                      │
 │◄─ RRCReconfiguration ──│                        │                      │
 │   (with ReconfigWithSync)                        │                      │
```

### Functions Involved (Step-by-Step)

#### Step 1: Handover Decision (RRC Layer)

| Function | File | Description |
|----------|------|-------------|
| `GnbRrcTask::evaluateHandoverDecision()` | `src/gnb/rrc/handover.cpp` | Checks guards: handover not already in progress, not already pending, valid neighbour PCI. Compares `bestNeighRsrp > servingRsrp + 3 dB`. If true: sets `handoverDecisionPending = true`; pushes `NmGnbRrcToNgap::HANDOVER_REQUIRED` with `ueId`, `hoTargetPci`, and cause `RadioNetwork_handover_desirable_for_radio_reason`. |

#### Step 2: Handover Required (NGAP Layer → AMF)

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::sendHandoverRequired()` | `src/gnb/ngap/handover.cpp` | Finds the UE's NGAP context; builds `ASN_NGAP_HandoverRequired` with IEs: |
| | | • **HandoverType** = intra5gs |
| | | • **Cause** = handover_desirable_for_radio_reason |
| | | • **TargetID** = `TargetRANNodeID` encoding the target PCI as the gNB-ID (same PLMN, TAC) |
| | | • **SourceToTarget-TransparentContainer** = minimal placeholder (simulator limitation) |
| | | Sends via `sendNgapUeAssociated()` |

#### Step 3: Handover Command Reception (AMF → Source gNB)

See [Section 16](#16-handover-command-delivery-amf--source-gnb--ue).

### Key Data in UE Context During Handover

| Field | Type | Purpose |
|-------|------|---------|
| `handoverInProgress` | `bool` | Set to `true` when HandoverCommand is sent to UE |
| `handoverDecisionPending` | `bool` | Set to `true` before HANDOVER_REQUIRED sent; cleared when command received |
| `handoverTargetPci` | `int` | PCI of the target cell |
| `handoverNewCrnti` | `int` | New C-RNTI assigned for the target cell |
| `handoverTxId` | `long` | RRC transaction ID of the RRCReconfiguration |
| `lastMeasReportPci` | `int` | PCI of best neighbour from last measurement report |
| `lastMeasReportRsrp` | `int` | RSRP (dBm) of best neighbour |
| `lastServingRsrp` | `int` | RSRP (dBm) of serving cell |

---

## 16. Handover Command Delivery (AMF → Source gNB → UE)

### Triggering Event
**AMF sends `HandoverCommand`** (SuccessfulOutcome to HandoverRequired) via SCTP.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receiveHandoverCommand()` | `src/gnb/ngap/handover.cpp` | Extracts `TargetToSource-TransparentContainer` (RRC container); pushes `NmGnbNgapToRrc::HANDOVER_COMMAND_DELIVERY` with `ueId` and `rrcContainer` to RrcTask |
| `GnbRrcTask::handleNgapHandoverCommand()` | `src/gnb/rrc/handover.cpp` | Clears `handoverDecisionPending`; determines `targetPci` and `newCrnti` from UE context; calls `sendHandoverCommand()` |
| `GnbRrcTask::sendHandoverCommand()` | `src/gnb/rrc/handover.cpp` | Builds `RRCReconfiguration` with `ReconfigurationWithSync` containing: |
| | | • `newUE-Identity` (C-RNTI for target cell) |
| | | • `t304` timer value (1000 ms) |
| | | • `spCellConfigCommon` with target `physCellId` (PCI) |
| | | UPER-encodes `CellGroupConfig` as `masterCellGroup`; sets `handoverInProgress = true`; sends via `sendRrcMessage(DL_DCCH)` |

### Handover Preparation Failure

If the AMF responds with `HandoverPreparationFailure` instead:

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receiveHandoverPreparationFailure()` | `src/gnb/ngap/handover.cpp` | Logs a warning; UE remains on the source cell |

---

## 17. Handover Completion and Path Switch

### Triggering Event
**UE sends `RRCReconfigurationComplete`** after it has synchronized with the target cell.

### 5G Context
After the UE successfully reconfigures onto the target cell, the gNB sends `HandoverNotify` and `PathSwitchRequest` to the AMF so the core network can update the downlink data path.

### Functions Involved

| Step | Function | File | Description |
|------|----------|------|-------------|
| 1 | `GnbRrcTask::receiveRrcReconfigurationComplete()` | `src/gnb/rrc/handover.cpp` | Checks `handoverInProgress`; if true, calls `handleHandoverComplete()` |
| 2 | `GnbRrcTask::handleHandoverComplete()` | `src/gnb/rrc/handover.cpp` | Pushes `NmGnbRrcToNgap::HANDOVER_NOTIFY` to NgapTask |
| 3 | `NgapTask::handleHandoverNotifyFromRrc()` | `src/gnb/ngap/handover.cpp` | Calls `sendHandoverNotify()` then `sendPathSwitchRequest()` |
| 4 | `NgapTask::sendHandoverNotify()` | `src/gnb/ngap/handover.cpp` | Builds `ASN_NGAP_HandoverNotify` with `UserLocationInformation` (NR-CGI + TAI); sends to AMF |
| 5 | `NgapTask::sendPathSwitchRequest()` | `src/gnb/ngap/handover.cpp` | Builds `ASN_NGAP_PathSwitchRequest` with `UserLocationInformation`; sends to AMF to redirect downlink data |

### Path Switch Acknowledgement

| Function | File | Description |
|----------|------|-------------|
| `receivePathSwitchRequestAcknowledge()` | `src/gnb/ngap/handover.cpp` | Logs success; pushes `NmGnbNgapToRrc::PATH_SWITCH_REQUEST_ACK` to RrcTask — handover is fully complete |
| `receivePathSwitchRequestFailure()` | `src/gnb/ngap/handover.cpp` | Logs warning |

---

## 18. Radio Link Failure

### Triggering Event
**RLS detects radio link failure** — `RlsControlTask` reports `RADIO_LINK_FAILURE` or a prolonged signal loss is detected in `RlsUdpTask`.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `GnbRrcTask::handleRadioLinkFailure()` | `src/gnb/rrc/handler.cpp` | Pushes `NmGnbRrcToNgap::RADIO_LINK_FAILURE` to NgapTask |
| `NgapTask::handleRadioLinkFailure()` | `src/gnb/ngap/radio.cpp` | Pushes `UE_CONTEXT_RELEASE` to GtpTask; calls `sendContextRelease()` |
| `sendContextRelease()` | `src/gnb/ngap/context.cpp` | Builds `UEContextReleaseRequest` with cause and PDU session list; sends to AMF |

---

## 19. User-Plane Data Forwarding (GTP-U)

### 19a. Uplink (UE → UPF)

| Triggering Event | RLS delivers uplink data PDU → `NmGnbRlsToGtp::DATA_PDU_DELIVERY` |
|---|---|
| `GtpTask::handleUplinkData()` | `src/gnb/gtp/task.cpp` — looks up PDU session by (ueId, psi); checks rate limiter; builds GTP-U G-PDU (TEID + QFI extension header); encodes and sends to UPF via UDP |

### 19b. Downlink (UPF → UE)

| Triggering Event | UDP packet received on port 2152 from UPF |
|---|---|
| `GtpTask::handleUdpReceive()` | `src/gnb/gtp/task.cpp` — decodes GTP-U message; for `G-PDU`: looks up session by downlink TEID, checks rate limiter, pushes `NmGnbGtpToRls::DATA_PDU_DELIVERY` to RLS |
| `GtpTask` (echo) | Responds to `ECHO_REQUEST` with `ECHO_RESPONSE` |

---

## 20. AMF Configuration Update

### Triggering Event
**AMF sends `AMFConfigurationUpdate`** via SCTP.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receiveAmfConfigurationUpdate()` | `src/gnb/ngap/interface.cpp` | Processes updated AMF configuration; responds with Ack or Failure |

---

## 21. Overload Control

### Triggering Events
**AMF sends `OverloadStart` or `OverloadStop`** via SCTP.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receiveOverloadStart()` | `src/gnb/ngap/interface.cpp` | Records overload indication (reduction percentage or action) in `NgapAmfContext::overloadInfo` |
| `NgapTask::receiveOverloadStop()` | `src/gnb/ngap/interface.cpp` | Clears overload status |

---

## 22. NAS Reroute

### Triggering Event
**AMF sends `RerouteNASRequest`** — instructs the gNB to resend an Initial UE Message to a different AMF.

### Functions Involved

| Function | File | Description |
|----------|------|-------------|
| `NgapTask::receiveRerouteNasRequest()` | `src/gnb/ngap/nas.cpp` | Decodes the enclosed `InitialUEMessage`; calls `selectNewAmfForReAllocation()` to pick an alternative AMF; resends via `sendNgapUeAssociated()` to the new AMF |
| `selectNewAmfForReAllocation()` | `src/gnb/ngap/nnsf.cpp` | AMF selection excluding the original AMF |

---

## 23. Source File Index

| File | Task | Responsibility |
|------|------|----------------|
| `src/gnb.cpp` | — | Entry point: config parsing, GNodeB creation, CLI server |
| `src/gnb/gnb.cpp` | — | `GNodeB` class: task creation, start, shutdown |
| `src/gnb/gnb.hpp` | — | `GNodeB` class declaration |
| `src/gnb/types.hpp` | — | Domain types: `GnbConfig`, `NgapAmfContext`, `NgapUeContext`, `RrcUeContext`, `PduSessionResource`, `NgapCause`, `TaskBase` |
| `src/gnb/nts.hpp` | — | All NTS inter-task message types (`NmGnb*`) |
| `src/gnb/nts.cpp` | — | NTS message type implementations |
| `src/gnb/ngap/task.cpp` | NgapTask | Main loop, message dispatch |
| `src/gnb/ngap/interface.cpp` | NgapTask | NG Setup, AMF association, overload, AMF config update |
| `src/gnb/ngap/transport.cpp` | NgapTask | NGAP PDU encode/decode, SCTP routing |
| `src/gnb/ngap/nas.cpp` | NgapTask | Initial/Uplink/Downlink NAS transport, Reroute |
| `src/gnb/ngap/context.cpp` | NgapTask | Initial Context Setup, UE Context Release/Modification |
| `src/gnb/ngap/session.cpp` | NgapTask | PDU Session Setup/Release |
| `src/gnb/ngap/radio.cpp` | NgapTask | Radio link failure, Paging |
| `src/gnb/ngap/handover.cpp` | NgapTask | HandoverRequired/Command/Notify, PathSwitch |
| `src/gnb/ngap/management.cpp` | NgapTask | AMF/UE context CRUD |
| `src/gnb/ngap/nnsf.cpp` | NgapTask | AMF selection (NAS Node Selection Function) |
| `src/gnb/ngap/encode.cpp` | NgapTask | APER encode/decode wrappers |
| `src/gnb/ngap/utils.cpp` | NgapTask | ASN↔domain type helpers |
| `src/gnb/rrc/task.cpp` | GnbRrcTask | Main loop, message dispatch |
| `src/gnb/rrc/broadcast.cpp` | GnbRrcTask | MIB/SIB1 construction and broadcast |
| `src/gnb/rrc/channel.cpp` | GnbRrcTask | RRC PDU encode/decode per channel |
| `src/gnb/rrc/connection.cpp` | GnbRrcTask | RRC Setup Request/Complete |
| `src/gnb/rrc/handler.cpp` | GnbRrcTask | DL NAS delivery, UL info transfer, RRC Release, RLF, Paging |
| `src/gnb/rrc/handover.cpp` | GnbRrcTask | MeasConfig, MeasReport, HO decision, HO command, HO complete |
| `src/gnb/rrc/sap.cpp` | GnbRrcTask | RLS service access point handler |
| `src/gnb/rrc/ues.cpp` | GnbRrcTask | UE context CRUD |
| `src/gnb/rrc/management.cpp` | GnbRrcTask | Transaction ID counter |
| `src/gnb/gtp/task.cpp` | GtpTask | GTP-U data plane, session/context management |
| `src/gnb/rls/task.cpp` | GnbRlsTask | RLS coordinator (routes between RRC/GTP and sub-tasks) |
| `src/gnb/rls/ctl_task.cpp` | RlsControlTask | PDU ACK tracking, retransmission, RLS message handling |
| `src/gnb/rls/udp_task.cpp` | RlsUdpTask | Heartbeat, signal detection/loss, UDP I/O |
| `src/gnb/sctp/task.cpp` | SctpTask | SCTP connection lifecycle, send/receive |
| `src/gnb/app/task.cpp` | GnbAppTask | Status updates, CLI commands |
| `src/gnb/app/cmd_handler.cpp` | GnbAppTask | CLI command dispatch (status, info, ue-list, etc.) |
