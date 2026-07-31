# RRC (Radio Resource Control) Layer — Summary

RRC is the control-plane protocol between the UE and the gNB (3GPP TS 38.331). In UERANSIM it is implemented as one NTS task on each side. The RRC tasks ride on top of the RLS layer (see RLS_summary.md): every RRC PDU is ASN.1 UPER-encoded and handed to RLS as an `RRC_PDU_DELIVERY` tagged with a logical RRC channel (BCCH-BCH, BCCH-DL-SCH, CCCH, DCCH, PCCH, plus the custom DL_SIB19 channel), and every received RLS RRC PDU is decoded and dispatched by channel and message type.

The RRC layer owns:
- **System information broadcast** (MIB/SIB1, and a custom SIB19 for NTN satellite ephemeris)
- **Cell tracking and idle-mode cell selection** (UE side)
- **Connection establishment/release** (RRCSetupRequest/RRCSetup/RRCSetupComplete, RRCRelease, RRCReject, SecurityModeCommand/Complete)
- **NAS transport** (UL/DLInformationTransfer, Paging)
- **Measurement configuration and reporting** (A2/A3/A5/D1 events, plus conditional CondA3/CondD1/CondT1)
- **Handover** — currently being extended to a dual Xn/NGAP (N2) handover stack, plus Conditional Handover (CHO), with special support for NTN/satellite scenarios (TLE propagation, dynamic trigger calculation)

Identification note: since the ueID/NCI refactor, the "cellId" carried in UE-side APIs *is* the NCI (NR Cell Identity), and UEs are identified gNB-side by a 64-bit `ueId` derived from the IMSI (delivered by RLS from the STI). The C-RNTI is allocated and tracked gNB-side (`CrntiManager`) to enforce the 65529-UE standards limit, but is not used for identification on the wire (vestigial in RLS).

## Relevant Code Locations

### UE side ([src/ue/rrc/](src/ue/rrc/))

| Location | Role |
|---|---|
| [task.hpp](src/ue/rrc/task.hpp)/[task.cpp](src/ue/rrc/task.cpp) | `UeRrcTask` — NTS message loop, timers (500 ms machine cycle, T304) |
| [state.cpp](src/ue/rrc/state.cpp) | RRC state machine (IDLE/CONNECTED/INACTIVE) and the periodic `performCycle()` |
| [sap.cpp](src/ue/rrc/sap.cpp) | Service access points: dispatch of RLS→RRC and NAS→RRC messages |
| [channel.cpp](src/ue/rrc/channel.cpp) | Per-channel encode/decode and message-type routing (both directions) |
| [cells.cpp](src/ue/rrc/cells.cpp) | Cell detection/loss from RLS signal changes, PLMN list, radio-link-failure handling |
| [idle.cpp](src/ue/rrc/idle.cpp) | Idle-mode cell selection (suitable/acceptable cell search) |
| [sysinfo.cpp](src/ue/rrc/sysinfo.cpp) | MIB / SIB1 reception into the per-cell descriptor |
| [sib19.cpp](src/ue/rrc/sib19.cpp)/[.hpp](src/ue/rrc/sib19.hpp) | Custom binary SIB19 (NTN ephemeris) parsing; feeds the UE TLE store |
| [connection.cpp](src/ue/rrc/connection.cpp) | RRC Setup / Reject / Release / SecurityModeCommand procedures |
| [nas.cpp](src/ue/rrc/nas.cpp) | NAS transport (UL/DLInformationTransfer), triggers establishment from IDLE |
| [handler.cpp](src/ue/rrc/handler.cpp) | Paging reception |
| [access.cpp](src/ue/rrc/access.cpp) | Unified Access Control (UAC) evaluation for NAS |
| [reconfig.cpp](src/ue/rrc/reconfig.cpp) | RRCReconfiguration reception: MeasConfig parsing, radio bearers, handover detection, CHO extension chain |
| [measurement.cpp](src/ue/rrc/measurement.cpp)/[measurement.hpp](src/ue/rrc/measurement.hpp) | Event evaluation (A2/A3/A5/D1/CondD1/CondT1), MeasurementReport generation |
| [handover.cpp](src/ue/rrc/handover.cpp) | Handover execution (`performHandover`), T304, measurement suspend/resume |
| [cho.cpp](src/ue/rrc/cho.cpp) | Conditional handover: candidate parsing, per-cycle evaluation, tie-breaking, execution |
| [meas_provider.cpp](src/ue/rrc/meas_provider.cpp)/[.hpp](src/ue/rrc/meas_provider.hpp) | Out-of-band measurement injection (UDP/Unix socket/file) — **deprecated, never instantiated; candidate for removal** |

### gNB side ([src/gnb/rrc/](src/gnb/rrc/))

| Location | Role |
|---|---|
| [task.hpp](src/gnb/rrc/task.hpp)/[task.cpp](src/gnb/rrc/task.cpp) | `GnbRrcTask` — NTS message loop, timers (SI broadcast, SIB19, satellite caches, location, status) |
| [sap.cpp](src/gnb/rrc/sap.cpp) | RLS→RRC dispatch (SIGNAL_DETECTED, UPLINK_RRC) |
| [channel.cpp](src/gnb/rrc/channel.cpp) | Per-channel encode/decode and message-type routing (both directions) |
| [ues.cpp](src/gnb/rrc/ues.cpp) / [management.cpp](src/gnb/rrc/management.cpp) | `RrcUeContext` creation/snapshot/release (ues.cpp) and lookups + C-RNTI wrappers (management.cpp) |
| [crnti_manager.hpp](src/gnb/rrc/crnti_manager.hpp) | Stack-based O(1) C-RNTI allocator (range 1–65529) |
| [connection.cpp](src/gnb/rrc/connection.cpp) | RRCSetupRequest/SetupComplete/SecurityModeComplete handling |
| [handler.cpp](src/gnb/rrc/handler.cpp) | NAS delivery (both directions), NAS-Accept with RadioBearerConfig, Paging, SecurityModeCommand, UE Context Release, RLF-to-NGAP |
| [broadcast.cpp](src/gnb/rrc/broadcast.cpp) | MIB/SIB1 construction + broadcast; custom binary SIB19 serialization |
| [reconfiguration.cpp](src/gnb/rrc/reconfiguration.cpp) | `makeRrcReconfiguration()` — shared outer-message builder (mostly a stub file) |
| [measurement.cpp](src/gnb/rrc/measurement.cpp) | MeasConfig construction (`sendMeasConfig`/`createMeasConfig`), MeasurementReport reception, RRCReconfigurationComplete / handover-completion matching |
| [handover.cpp](src/gnb/rrc/handover.cpp) | Handover decision + execution (source & target roles), CHO preparation/completion, custom transparent-container encode/decode |
| [sat_calcs.cpp](src/gnb/rrc/sat_calcs.cpp) | Satellite neighborhood caches (50-nearest, 7 SIB19-range) and dynamic NTN trigger calculation |

### Shared support

- `src/lib/rrc/common/` — shared event types (`HandoverEventType`, `ReportConfigEvent`, `DynamicEventTriggerParams`) and ASN value converters (RSRP/hysteresis/TTT/T304/distance encodings).
- [src/gnb/types.hpp](src/gnb/types.hpp) — `RrcUeContext`, `RRCHandoverPending`, `ChoPreparationState`, `GnbCondHandoverRequest`.
- [src/ue/types.hpp](src/ue/types.hpp) — `UeCellDesc` (MIB/SIB1/SIB19 per cell), `UeMeasConfig`, `MeasIdState`, `ChoCandidate`, `ERrcState`.

Unlike RLS (three tasks per node), RRC is a **single NTS task** on each side. All I/O with other layers is via NTS message queues (RLS, NAS, NGAP, Xn, GTP); two exceptions are direct cross-thread reads noted in the issues section.

## UE-side flow

`UeRrcTask` starts in `RRC_IDLE` and runs a **machine cycle** every 500 ms (plus on-demand triggers from NAS). Each cycle (`performCycle()`, state.cpp):

- In **RRC_IDLE / RRC_INACTIVE**: run idle-mode cell selection.
- In **RRC_CONNECTED**: pull the current cell measurements from the node-wide measurement store (`m_base->cellDbMeas`, populated by RLS heartbeat ACKs), then
  1. `evaluateMeasurements()` — update the per-measId trigger state,
  2. `evaluateChoCandidates()` — CHO first; if a CHO executes this cycle, suppress normal reporting (per 38.331),
  3. `measurementReporting()` — send MeasurementReports for satisfied, reportable measIds.

### Cell tracking and selection

RLS pushes `SIGNAL_CHANGED(cellId, dbm)` whenever a cell appears, disappears, or crosses thresholds. `handleCellSignalChange()` (cells.cpp) maintains `m_cellDesc` (per-cell MIB/SIB1/SIB19 + last dbm):
- New cell above `cons::MIN_RSRP` → added; MIB/SIB1 broadcasts from the gNB then fill in its descriptor (sysinfo.cpp), and the available-PLMN set shared with NAS is refreshed.
- Cell below `MIN_RSRP` → removed; if it was the active cell this is treated as radio link failure.
- Signal in the band `[MIN_RSRP, RLF_RSRP)` on a known cell → radio-link-failure handling while still "in coverage".

`handleRadioLinkFailure()` on the **active** cell in connected mode cancels all CHO candidates, drops to `RRC_IDLE`, and notifies NAS (`RADIO_LINK_FAILURE`); in idle mode it notifies NAS of the cell change instead.

Idle-mode selection (`performCellSelection()`, idle.cpp) waits ≥1 s after startup (≥4 s if no PLMN selected yet), then searches `m_cellDesc` for a **suitable** cell (has MIB+SIB1, matches selected PLMN, not barred/reserved, TAC not forbidden, RSRP ≥ RLF threshold), choosing the strongest by dbm; failing that it searches for an **acceptable** cell (same minus the PLMN requirement, for emergency service). The winner is written into the shared `currentCell`, RLS is told via `ASSIGN_CURRENT_CELL`, and NAS gets `ACTIVE_CELL_CHANGED`.

### Connection establishment and NAS transport

NAS sends `UPLINK_NAS_DELIVERY`; if the UE is in `RRC_IDLE` this triggers `startConnectionEstablishment()` (connection.cpp): the initial NAS PDU is stashed, an `RRCSetupRequest` (S-TMSI part 1 if a GUTI/TMSI is available, otherwise a 39-bit random value) goes out on UL-CCCH to the active cell. On `RRCSetup` the UE replies `RRCSetupComplete` (carrying the initial NAS PDU, and S-TMSI/GUAMI when known), switches to `RRC_CONNECTED`, and notifies NAS. `RRCReject` and internal failures surface to NAS as `RRC_ESTABLISHMENT_FAILURE`.

Once connected, uplink NAS rides in `ULInformationTransfer` and downlink NAS arrives via `DLInformationTransfer` (nas.cpp) or inside the `dedicatedNAS-MessageList` of an RRCReconfiguration (reconfig.cpp). `Paging` on PCCH is parsed and the TMSI list forwarded to NAS. `SecurityModeCommand` is always answered with `SecurityModeComplete` (no actual ciphering — simulated RF). `RRCRelease` cancels CHO candidates, drops to `RRC_IDLE`, and notifies NAS; a NAS-initiated `LOCAL_RELEASE_CONNECTION` additionally tells RLS to `RESET_STI`.

### RRCReconfiguration processing (reconfig.cpp)

`receiveRrcReconfiguration()` is the workhorse; in order it:
1. Applies `radioBearerConfig` if present (`setupRadioBearers()`): SRB/DRB add/mod/release and SDAP QoS-flow mappings are translated into a `RADIO_BEARER_UPDATE` for the RLS control task.
2. Parses `measConfig` (delta signaling: remove lists first, then add/mod; `fullConfig=true` wipes state first) into `m_measConfig` via `applyMeasConfig()`. MeasIds referencing conditional events (CondA3/CondD1/CondT1) are marked non-reportable — they gate CHO instead of generating reports.
3. Delivers any dedicated NAS messages to NAS.
4. Decodes `masterCellGroup`; if it contains `ReconfigurationWithSync`, this is a **handover command** — target NCI (carried in `physCellId`), new C-RNTI, and T304 are extracted.
5. Walks the extension chain v1530→v1540→v1560→v1610; a `ConditionalReconfiguration` there is parsed into CHO candidates (`parseConditionalReconfiguration()`).
6. If a handover was detected, `performHandover()` runs; otherwise an `RRCReconfigurationComplete` is sent as the ACK.

### Measurements and reporting (measurement.cpp)

`evaluateMeasurements()` walks every configured measId and evaluates its event against the current measurement snapshot (RSRP events A2/A3/A5), the UE's simulated position (D1/CondD1), or the current time — satellite time in NTN mode (CondT1). A satisfied condition starts/continues a time-to-trigger clock; once TTT elapses the measId's `isSatisfied` flag is set. `measurementReporting()` then builds one `MeasurementReport` per newly-satisfied reportable measId (serving cell result + triggered neighbors sorted by RSRP, capped at `maxReportCells`) — one-shot until the condition clears.

### Handover execution (handover.cpp)

`performHandover(txId, targetNci, newCrnti, t304Ms, hasRachConfig)`:
suspends measurement evaluation, starts T304, logs the (simulated) key refresh / MAC reset / RACH steps, switches `currentCell` to the target, points RLS at the target via `ASSIGN_CURRENT_CELL`, sends `RRCReconfigurationComplete` **to the target cell** with the transaction ID from the handover command, then immediately clears `m_handoverInProgress`, resumes measurements (resetting TTT state and CHO runtime state), and notifies NAS of the cell change. T304 expiry (`handleT304Expiry()`) currently only logs the failure — re-establishment is commented out (see issues).

### Conditional handover (cho.cpp)

`parseConditionalReconfiguration()` applies the remove list then the add/mod list. Each `CondReconfigToAddMod` becomes a `ChoCandidate`: its `condExecutionCond` measIds (AND semantics, max 2) must already exist in `m_measConfig`; its `condRRCReconfig` octet string is decoded to extract target NCI / new C-RNTI / T304 from the nested `ReconfigurationWithSync`. Each cycle, `evaluateChoCandidates()` checks whether all of a candidate's measIds are satisfied; if exactly one candidate triggers it executes immediately, and if several trigger, tie-breaking classifies targets as satellite (present in any cell's SIB19 entries) vs terrestrial — best satellite wins over best terrestrial; satellite scoring (`selectBestSatellite()`) is currently a stub returning dummy scores. Executing a candidate cancels all others (per 38.331 §5.3.5.8) and calls `performHandover()` with the stored txId. RLF, RRCRelease, and `fullConfig` reconfigurations cancel all candidates.

### SIB19 (sib19.cpp)

`receiveSib19()` parses the custom multi-entry binary payload (version 2: 8-byte header, then N entries of 96 bytes for PosVel/Orbital or 216 bytes for TLE) into the cell descriptor's `Sib19Info::entriesByNci`, and upserts any TLE entries into the UE's satellite-state store so it can propagate neighbor satellites itself. This is a simulator-custom encoding, not ASN.1 (the payload is opaque to RLS; only the two RRC layers understand it). Since the Release-18 migration (2026-07-28) the standard `ASN_RRC_SIB19-r17` / `EphemerisInfo-r17` / `NTN-Config-r17` types *do* exist in `src/asn/rrc`, but nothing encodes them — the custom path stays active, because the TLE entries it carries have no standard counterpart. See `docs/ASN1_R18_Migration_Plan.md` §9.3.

## gNB-side flow

`GnbRrcTask` serves every UE from one task. State per UE lives in `RrcUeContext` (`m_ueCtx`, keyed by ueId): C-RNTI, initial identity, RRC connection state, GUAMI/S-TMSI, security info from the AMF, active measurement configuration (measObjects/reportConfigs/measIdentities and their ID trackers), last measurement-report data, handover-in-progress state, and per-CHO-profile preparation state. Handovers in progress at the *target* side live separately in `m_handoversPending` (ueId → `RRCHandoverPending{ueId, ctx, expireTime, rrcTxId}`).

Timers started in `onStart()`: SI broadcast every 10 s and a 1 s pending-handover expiry sweep (`sweepPendingHandovers()`); when NTN is enabled, also gNB location re-propagation from its TLE every 1 s, satellite neighborhood cache recomputation every 15 s, SIB19 broadcast at the configured period, and a 500 ms status publisher (connected-UE count).

### Broadcast (broadcast.cpp)

`triggerSysInfoBroadcast()` constructs MIB (barred/intra-freq flags) and SIB1 (TAC, NCI, PLMN, UAC barring sets) and sends them with `ueId=0`, which RLS fans out to every known UE. It runs on the periodic timer, on `RADIO_POWER_ON` from NGAP (which also clears `m_isBarred`), and immediately on each RLS `SIGNAL_DETECTED` so a newly-arrived UE learns the cell without waiting. `triggerSib19Broadcast()` serializes the gNB's own ephemeris plus the ≤7 neighbors in `m_sib19RangeCache` in the configured mode (PosVel / Orbital / raw TLE) into the custom SIB19 payload.

### Connection (connection.cpp, handler.cpp)

- `receiveRrcSetupRequest`: rejects if a context already exists for the ueId (TODO: better recovery); otherwise allocates a C-RNTI, creates the context (state `RRC_CONNECTION_PENDING`), records the initial identity/establishment cause, and replies `RRCSetup` with a minimal `masterCellGroup`.
- `receiveRrcSetupComplete`: state → `RRC_CONNECTED`; extracts GUAMI/S-TMSI when present; forwards the dedicated NAS message to NGAP as `INITIAL_NAS_DELIVERY`; then immediately `sendMeasConfig(ueId)` to arm handover measurements.
- `handleNgapSecurityInfo` (AMF security context via NGAP): stores it in the context and sends `SecurityModeCommand` with null algorithms (NEA0/NIA0); `SecurityModeComplete` is just logged.
- Downlink NAS → `DLInformationTransfer`; **NAS-Accept** messages that establish PDU sessions instead ride an `RRCReconfiguration` that also carries the generated `RadioBearerConfig` (one DRB per PDU session, all QoS flows of a session mapped to that DRB); the same bearer/SDAP data is pushed to the gNB RLS control task via `RADIO_BEARER_UPDATE`. `handleNgapPduSessionUpdate` does the bearer-only variant.
- **DRB-id ownership and lifecycle (added 2026-07-17).** RRC is the authoritative owner of the `psi → {drbId, qfis}` mapping, held per-UE in `RrcUeContext::bearerMap`. All bearer *creation* funnels through `assignSessionBearers()` ([handler.cpp](src/gnb/rrc/handler.cpp)), which assigns one DRB per session via `allocateDrbId()` — the **lowest free id in the spec's 1..32 range**, derived from `bearerMap` so ids stay unique across a UE's active bearers and are reclaimed on release; re-setup of an already-mapped PSI reuses its existing DRB (idempotent modify). Both `createRadioBearerConfig` (normal setup) and the target-side handover pre-programming path call it, so `bearerMap` is populated identically on N2/Xn handover. This replaced two independent per-call `drbs_used` counters that restarted at 1 each call and could re-assign a live DRB id.
- **Session release / bearer teardown (added 2026-07-17).** `handleNgapPduSessionRelease()` is the inverse: NGAP forwards 5GC-released PSIs (path-switch-ack `PDUSessionResourceReleasedListPSAck` or a `PDUSessionResourceReleaseCommand`) over `NmGnbNgapToRrc::PDU_SESSION_UPDATE` (`releasedPsis`); RRC looks up each PSI's DRB/QFIs in `bearerMap`, tells RLS to delete them (`RADIO_BEARER_UPDATE` with `deleteBearers`/`deleteSdapMappings` — the previously-unexercised delete half of that message), tells the UE to release the DRBs (`RRCReconfiguration` with `drb-ToReleaseList`), and prunes `bearerMap` (freeing the id). Build-clean; not yet exercised live (Xn path blocked by Xn_summary issue 30).
- `handleUeContextRelease` (from NGAP): sends `RRCRelease` to the UE unless the cause is `RadioNetwork_successful_handover`, then deletes the context and returns the C-RNTI to the pool.

### Measurement configuration (measurement.cpp)

`sendMeasConfig(ueId, forceResend)` builds one `RRCReconfiguration` with `fullConfig=true` and a `MeasConfig` containing:
- one dummy `MeasObjectNR` (id 1 — there is no real RF layer),
- one ReportConfig per configured event: basic events from `handover.basicHandoverMeasIdentities` (A2/A3/A5/D1 as `eventTriggered`) and, if CHO is enabled, each active CHO profile's condition events (CondA3/CondD1/CondT1 as `condTriggerConfig_r16`),
- a MeasId per ReportConfig, all recorded in the UE context (`measIdentities`, tagged with the owning CHO profile index or −1).

In NTN mode, before building, `satHandoverTriggerCalc()` (sat_calcs.cpp) propagates the gNB's own TLE to find **t_exit** (when its elevation from the UE drops below the configured minimum) and the nadir point at exit, and rewrites the D1/CondD1 distance thresholds/reference points and CondT1 threshold time accordingly — i.e. the trigger parameters are *computed from orbital mechanics*, not statically configured. After sending, `processConditionalHandover()` is kicked off for each active CHO profile.

### Measurement reports and the handover decision (measurement.cpp, handover.cpp)

`receiveMeasurementReport()` extracts serving RSRP and the best neighbor (NCI is carried in `physCellId`), stores them in the UE context, and calls `evaluateHandoverDecision()`, which re-checks the triggering event's condition (A2/A3/A5 against RSRPs; D1 against the UE's reported position) and, if met and no handover is already pending, runs `executeBasicHandover()`:

1. Builds the **source-to-target transparent container**: a simulator-custom binary blob (`makeSourceToTargetTransparentContainerSimulated` → `ho_container::EncodeRrcContext`, framed by `ho_container::WrapSourceToTarget`) carrying ueId, C-RNTI, NH/NCC and security bitmaps/K_gNB, and the full measurement configuration (measIdentities, reportConfigEvents, measObjects), plus an optional padding blob to simulate realistic container sizes. It is opaque to NGAP/Xn/AMF, as in the real architecture.
2. Looks the target up in the runtime neighbor store; the neighbor's `handoverInterface` selects the path:
   - **Xn**: `HANDOVER_REQUEST_SEND` to the Xn task,
   - **N2**: `HANDOVER_REQUIRED` to the NGAP task.

### Target-side handover (handover.cpp)

`handleHandoverRequest()` (invoked from either `NmGnbNgapToRrc::HANDOVER_REQUEST_RECEIVED` or `NmGnbXnToRrc::HANDOVER_REQUEST_RECEIVED`; the Xn variant additionally carries an `XnHandoverCoreContext` with the transferred AMF association/security/AMBR) decodes the custom container back into a provisional `RrcUeContext` and allocates a fresh C-RNTI. Every failure path (container decode, invalid ueId, C-RNTI exhaustion, invalid Xn core/session state, container build) funnels through `rejectHandoverRequest()`, which frees partial state and answers the requester (XnAP HandoverPreparationFailure / NGAP HandoverFailure); a duplicate pending entry for the same ueId is replaced (latest wins). All PDU sessions are admitted (admission control TODO). The target then pre-programs its RLS with the one-DRB-per-session bearer/SDAP layout (`RADIO_BEARER_UPDATE`, via the shared `assignSessionBearers()` allocator so `bearerMap` is populated and DRB ids stay unique — see the DRB-id lifecycle note above) on **both** interfaces — user-plane traffic needs the DRB/SDAP mappings in place when the UE arrives, and on Xn it additionally guarantees the transferred SN status has bearers to land on (RLS creates the UE's context on demand if no heartbeat has arrived yet). On the Xn path it also sends `XN_HANDOVER_PREPARE` to NGAP to build the provisional target-side NGAP/GTP state (the N2 equivalent happens in `sendHandoverRequestAcknowledge`). It then builds the **target-to-source container** — a `DL-DCCH/RRCReconfiguration` with `ReconfigurationWithSync` (target NCI in `physCellId`, new C-RNTI, T304=1 s) — stores the provisional context in `m_handoversPending` (with a 5 s basic / 100 s CHO expiry and an `isXn` flag that steers the completion procedure), and answers `HANDOVER_REQUEST_ACK_SEND` back to whichever task asked.

### Source-side command and completion

`handleHandoverAckOrCommand()` (NGAP Handover Command / Xn Handover Request Ack, now carrying the target NCI): for a basic handover it decodes the target's container and forwards the `RRCReconfiguration` verbatim to the UE; for CHO it calls `completeConditionalHandover()` instead. On the Xn path, a malformed/unmatchable ACK triggers `HANDOVER_CANCEL_SEND` back to the target so its provisional state can be rolled back, and a successfully delivered command is followed by `SN_STATUS_TRANSFER_SEND` (the Xn task snapshots the source's per-bearer sequence numbers via the RLS `copyUeContext()` accessor and ships them to the target, which applies them through `APPLY_DRB_SN_STATUS`). `handleHandoverPreparationFailure()` clears the pending state (per-profile candidate for CHO, or the whole handover state for basic).

When the UE arrives at the target and its `RRCReconfigurationComplete` comes up through RLS, `receiveRrcReconfigurationComplete()` matches it against `m_handoversPending` by ueId + txId (with a cRnti-based remap fallback that is currently inert since RLS always passes cRnti=0). On a match: any stale old context for that ueId is deleted, the pending context is promoted into `m_ueCtx`, and `sendMeasConfig(..., forceResend=true)` re-arms measurements. Completion then splits by interface (the pending entry's `isXn` flag): **N2** notifies NGAP with `HANDOVER_NOTIFY_SEND`; **Xn** instead sends NGAP a `PATH_SWITCH_REQUEST` (activating the provisional target NGAP context and switching the N3 path) plus `HANDOVER_SUCCESS_SEND` to the source over Xn — the source keeps its RRC/NGAP/GTP state until the post-Path-Switch `UE_CONTEXT_RELEASE` arrives. `PATH_SWITCH_REQUEST_ACK` from NGAP now triggers `UE_CONTEXT_RELEASE_SEND` toward the source; `PATH_SWITCH_REQUEST_FAILURE` deliberately keeps both sides' contexts intact for a future retry/cancel policy.

### Conditional handover, gNB side (handover.cpp)

`processConditionalHandover(ueId, dynTriggerParams, profileIdx)` selects candidate targets — either the profile's explicit `targetCellIds` or a computed ranking (`prioritizeNeighbors()`, which asks the satellite store to score neighbors by how long they will remain above the minimum elevation after the serving satellite's t_exit) — caps them at `maxTargets`, records the profile's `ChoPreparationState` (candidates, scores, associated measIds), and sends one `HANDOVER_REQUIRED`-with-`choParams` per candidate to NGAP. (CHO preparation is currently NGAP-only; there is no Xn CHO path yet.)

As each target's CHO response arrives, `completeConditionalHandover()` extracts the nested RRCReconfiguration, recovers the candidate NCI from it, allocates a `condReconfigId` (1–8), and sends the UE an `RRCReconfiguration` whose v1610 extension carries one `CondReconfigToAddMod` (execution conditions = the profile's measIds, `condRRCReconfig` = the target's container). Profile state clears once all candidates have responded or failed.

### Xn message handling status (task.cpp)

As of 2026-07-15 all `GNB_XN_TO_RRC` handlers are implemented (the earlier log-only stubs are gone):
- `HANDOVER_REQUEST_RECEIVED` → `handleHandoverRequest()` with the transferred `XnHandoverCoreContext`.
- `HANDOVER_REQUEST_ACK_RECEIVED` → shared `handleHandoverAckOrCommand()` (Xn has already correlated targetNci/isCho).
- `HANDOVER_PREPARATION_FAILURE_RECEIVED` → `handleHandoverPreparationFailure()`.
- `UE_CONTEXT_RELEASE_RECEIVED` (source, after target Path Switch) → local `handleUeContextRelease(successful_handover)` (no RRCRelease over the old radio path) + `XN_SOURCE_CONTEXT_RELEASE` to NGAP to tear down source N2/N3.
- `HANDOVER_CANCEL_RECEIVED` (target) → discards the pending provisional RRC context, `XN_TARGET_PREPARATION_CANCEL` to NGAP, and `REMOVE_UE_CONTEXT` to RLS — each receiver idempotent so late/duplicate cancels are no-ops.
- `SN_STATUS_TRANSFER_RECEIVED` (target) → `APPLY_DRB_SN_STATUS` to RLS (applied immediately or deferred until the bearer exists).
- `HANDOVER_SUCCESS_RECEIVED` (source) → marks radio execution succeeded but retains all source state until UEContextRelease.

## Sequence summary (typical lifecycle)

1. gNB broadcasts MIB/SIB1 every 10 s and on each new UE signal; UE builds `m_cellDesc` from `SIGNAL_CHANGED` + MIB/SIB1 (+ SIB19 in NTN mode).
2. UE idle cycle selects the strongest suitable cell → `ASSIGN_CURRENT_CELL` to RLS, `ACTIVE_CELL_CHANGED` to NAS.
3. NAS registration triggers `RRCSetupRequest` → gNB creates context + C-RNTI → `RRCSetup` → `RRCSetupComplete` (initial NAS inside) → gNB forwards to NGAP and immediately sends the measurement configuration (and starts CHO preparation if enabled).
4. AMF security context → `SecurityModeCommand`/`Complete`; NAS Accept + PDU sessions → `RRCReconfiguration` with `RadioBearerConfig` → both sides program RLS bearers/SDAP mappings.
5. UE evaluates measurement events every 500 ms; on trigger (after TTT) sends `MeasurementReport`.
6. **Basic handover**: source gNB decides from the report → custom UE-context container → Xn Handover Request *or* NGAP Handover Required → target builds `ReconfigurationWithSync` + pending context → command returns to source → forwarded to UE → UE switches cells and sends `RRCReconfigurationComplete` to the target → target promotes the pending context, re-arms measurements, notifies NGAP (Handover Notify → Path Switch).
7. **Conditional handover**: gNB pre-arms candidates via per-target CHO preparation; UE stores candidates and executes autonomously when the condition group (e.g. CondT1 AND CondD1 timed to the satellite's coverage exit) is satisfied; remaining candidates are cancelled.
8. Loss of the serving signal below the RLF threshold → UE drops to RRC_IDLE, informs NAS, and re-runs cell selection.

---

## Issues found during this review

### Bugs

3. ~~**Cross-thread unsynchronized access to `m_ueCtx`**~~ **[FIXED 2026-07-15]**: the Xn task calls `GnbRrcTask::getUeContext()` directly ([xn/task.cpp:217](src/gnb/xn/task.cpp#L217)), which walked `m_ueCtx` while the RRC thread could be mutating it. Fixed with the same `shared_mutex` structure RLS uses for `copyUeContext()`: the RRC task thread takes a `unique_lock` for the duration of each message dispatch in `onLoop()` ([task.cpp](src/gnb/rrc/task.cpp)), and `getUeContext()` takes a `shared_lock` while copying ([ues.cpp](src/gnb/rrc/ues.cpp)). Note `getUeContext()` must never be called from the RRC task thread itself (deadlock against the dispatch lock — documented at the member in [task.hpp](src/gnb/rrc/task.hpp)). **Still open elsewhere**: the sibling calls on the same Xn code path — `NgapTask::getUeContext()` and `GtpTask::getUeContext()` ([xn/task.cpp:216-218](src/gnb/xn/task.cpp#L216-L218)) — are still unsynchronized, and `GnbCmdHandler` reads `ngapTask->m_ueCtx` directly from the app task thread ([cmd_handler.cpp:687](src/gnb/app/cmd_handler.cpp#L687)); those tasks need the same treatment.

4. **MUST FIX — NCI carried in `physCellId`.** Handover commands, measurement reports, and CHO containers all transport the target NCI in `ASN_RRC_PhysCellId`, which TS 38.331 constrains to 0..1007. **[PARTIALLY MITIGATED 2026-07-28]**: the Release-18 migration removes that constraint in the ASN.1 input ([rrc-rel18-v18_9.asn1](asn1-definitions/rrc-rel18-v18_9.asn1), marked with a comment at the definition; see `docs/ASN1_R18_Migration_Plan.md` §9.2), so UPER now encodes the field as a length-prefixed integer and a 36-bit NCI survives the round trip — `testLargeNciSurvivesPhysCellId` in [rrc_reference_location_tests.cpp](tests/rrc_reference_location_tests.cpp) pins that. This unblocks testing; it does not fix the design. `physCellId` is now wire-incompatible with any real UE or gNB, and the NCI still needs to move to an IE that can carry a 36-bit value (or a custom container field). Related narrowing to fix at the same time: `ChoCandidate::targetNci` and `extractTargetNciFromNestedRrcReconfiguration`'s out-param are `int` while NCIs are `int64_t` elsewhere.

5. **gNB never reacts to losing a UE at the radio level.** The RLS main task only logs `SIGNAL_LOST` and `TRANSMISSION_FAILURE` ([gnb/rls/task.cpp:66](src/gnb/rls/task.cpp#L66)), and `GnbRrcTask::handleRadioLinkFailure()` ([handler.cpp:381](src/gnb/rrc/handler.cpp#L381)) has no caller — so a UE that silently disappears leaves its RRC context, C-RNTI, and NGAP context allocated forever unless the AMF releases it. (Same gap noted in RLS_summary "Failure modes".) *Note: this is related to, but distinct from, the in-progress UE-context-release/handover work — it needs its own inactivity timer(s) rather than the handover-completion path.*

6. ~~**`m_handoversPending` entries never expire.**~~ **[FIXED 2026-07-15]**: a dedicated 1 s RRC-task timer now drives `sweepPendingHandovers()` ([handover.cpp](src/gnb/rrc/handover.cpp)), which garbage-collects entries past their `expireTime` (5 s basic / 100 s CHO): the provisional context and C-RNTI are freed via `discardHandoverUeContext()`, Xn preparations additionally roll back the provisional NGAP/GTP state (`XN_TARGET_PREPARATION_CANCEL` — idempotent, same as the cancel path), and the pre-programmed RLS bearer context is removed unless the ueId also has an active RRC context here. No failure message is sent (the preparation was already ACKed; expiry is local cleanup), so a completion arriving after expiry is treated as from an unknown UE. The NGAP counterpart is also in place (2026-07-15): `NgapTask::sweepPendingHandovers()` ([handover_n2.cpp](src/gnb/ngap/handover_n2.cpp), own 1 s timer) garbage-collects the N2 target-side `m_handoversPending` — post-ACK entries (adopted `ueId > 0`) additionally release the provisional GTP UE context/sessions, guarded against an active NGAP context for the same ueId; pre-ACK entries (ueId still 0) created no GTP state; `m_xnHandoversPending` is deliberately not swept there since RRC's sweep cancels it via `XN_TARGET_PREPARATION_CANCEL`; no AMF message is sent (the AMF's own relocation supervision covers a UE that never arrives). While wiring this, NGAP's `CHO_CANDIDATE_TIMEOUT_MS` was raised 60 s → 100 s to match RRC's `COND_HANDOVER_TIMEOUT_MS` — with enforcement active, NGAP expiring first would have stranded still-valid RRC CHO candidates (HandoverNotify would find no NGAP context to promote); a comment at each constant demands they stay aligned. Remaining note: the expiry clock everywhere is `CurrentTimeMillis()`, matching the stamps (existing TODO to switch to SatTime in NTN mode).

7. ~~**`handleHandoverRequest` failure paths leak and stay silent**~~ **[FIXED 2026-07-15]**: failure paths leaked the decoded `RrcUeContext`/C-RNTI and never answered the requester, leaving the source waiting. Now every failure funnels through `rejectHandoverRequest()` ([handover.cpp](src/gnb/rrc/handover.cpp)), which frees partial state via `discardHandoverUeContext()` (also intended for reuse by the future issue-6 expiry sweep) and answers the requester — Xn via the existing `HANDOVER_PREPARATION_FAILURE_SEND` plumbing, NGAP via a new `HANDOVER_FAILURE_SEND` message ([nts.hpp](src/gnb/nts.hpp)) handled by `NgapTask::handleRrcHandoverFailure()` ([handover_n2.cpp](src/gnb/ngap/handover_n2.cpp)), which sends NGAP HandoverFailure to the AMF (AMF-UE-NGAP-ID filled from the pending entry, sent non-UE-associated since no established context exists) and erases NGAP's pending entry. Guards added: container decode failure, invalid decoded ueId, C-RNTI pool exhaustion (previously unchecked — cRnti=0 would later normalize to 1 and collide), duplicate pending handover for the same ueId (replace policy: stale entry's context/C-RNTI freed, latest request wins), and target-container build failure. Admission control remains admit-all, with the reject/partial-admission hooks noted in-line for when it lands.


### Incomplete / in-progress functionality (expected given the Xn/NGAP dual-stack work)

11. ~~**Xn RRC stubs — the active in-progress work**~~ **[LARGELY RESOLVED 2026-07-15]**: all five formerly log-only `GNB_XN_TO_RRC` handlers (ACK, UE Context Release, Cancel, SN Status Transfer, Success) are now implemented (see "Xn message handling status" above), and the "Msg to NGAP to create pending handover CTX and reserve GTP tunnels — TODO" is replaced by the `XN_HANDOVER_PREPARE` message. The Xn execution phase is wired end-to-end: source sends SN Status Transfer after delivering the handover command (from an RLS `copyUeContext()` snapshot — that accessor now has its intended caller, [xn/handover.cpp:1553](src/gnb/xn/handover.cpp#L1553)), completion drives `PATH_SWITCH_REQUEST` + `HANDOVER_SUCCESS_SEND` instead of HandoverNotify, and Path Switch Ack triggers the source UE Context Release. *History (all resolved earlier the same day):* NGAP's dispatch was missing the `HANDOVER_REQUEST_ACK_SEND` case (added); `sendHandoverRequestAcknowledge()` ignored its `ueId` (fixed by making `NgapUeContext::ctxId` non-const and adopting RRC's ueId at ACK time, stamping the admitted `PduSessionResource`s); and the admitted resources were passed to GTP as pointers into a function-local vector — a use-after-free, fixed by heap-copying per the normal session-setup convention. *Known accepted wart:* that convention leaks the heap resource on both paths (GTP copies into its session tree, nobody frees the original) — a `unique_ptr`-through-the-message cleanup is a candidate for the failure-modes work. *Still open:* `PATH_SWITCH_REQUEST_FAILURE` intentionally has no retry/cancel policy yet, and CHO preparation remains NGAP-only (no Xn CHO path).

13. **UE T304 expiry does nothing** ([ue/rrc/handover.cpp:181-199](src/ue/rrc/handover.cpp#L181-L199)): re-establishment/RLF on handover failure is commented out. Moreover `performHandover` clears `m_handoverInProgress` immediately after sending RRCReconfigurationComplete, so T304 can never observe a failure — the UE currently cannot fail a handover.


18. **Duplicate-context lockout**: a UE whose registration failed (context left behind) can never re-attach — `receiveRrcSetupRequest` discards when a context exists ([connection.cpp:48-54](src/gnb/rrc/connection.cpp#L48-L54)), with no recovery/timeout. Marked TODO in code.


### Future development areas

- Add support for RRC_INACTIVE state

- Implement a cRNTI functionality that is moved to the RLS layer, but accessible from RRC and integrated into the RRC Setup process.

- Implement a satellite tiebreaker at the UE for multiple satellites in range.