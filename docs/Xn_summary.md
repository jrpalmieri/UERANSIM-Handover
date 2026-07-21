# Xn (XnAP) Layer — Summary

Xn is the gNB↔gNB interface (3GPP TS 38.423, XnAP). In UERANSIM it exists **only on the gNB side** — there is no UE-side Xn code. The UE participates in Xn handover indirectly through RRC (it receives the `RRCReconfiguration` handover command and answers `RRCReconfigurationComplete` at the target; see RRC_summary.md). This summary covers the gNB `XnTask` plus the NGAP-side Xn support (Path Switch) and the GTP-side data forwarding it drives.

The Xn layer owns:
- **Peer discovery and SCTP connection management** to neighbor gNBs (from the runtime neighbor store), including the **Xn Setup** procedure (XnSetupRequest/Response/Failure)
- **Handover Preparation** (XnAP HandoverRequest / HandoverRequestAcknowledge / HandoverPreparationFailure), carrying the UE's core-network context (AMF reference, security, AMBR, PDU sessions) and the opaque RRC container between source and target
- **Handover Execution** support (SNStatusTransfer, HandoverSuccess, UEContextRelease, HandoverCancel / ConditionalHandoverCancel)
- **Handover supervision timers** (preparation and overall, source side) and a deferred-send queue for requests whose UE contexts are not yet available

Unlike RLS (custom UDP wire format), Xn uses real ASN.1 APER-encoded XnAP PDUs over SCTP (PPID = XNAP), so the messages are interoperable-shaped even though several IEs are filled with simulator assumptions.

Identification note: on the wire, the simulator uses the stable 64-bit `ueId` (IMSI-derived) as **both** the source and target `NG-RANnodeUEXnAPID` (the IE tolerates long values). NCIs identify cells; `gnbId` (from the neighbor store) doubles as the SCTP `clientId` for each peer association.

## Relevant Code Locations

| Location | Role |
|---|---|
| [task.hpp](src/gnb/xn/task.hpp)/[task.cpp](src/gnb/xn/task.cpp) | `XnTask` — NTS message loop, neighbor-driven connection management, deferred queue, timers; `XnPendingHandover` state struct |
| [peertable.hpp](src/gnb/xn/peertable.hpp)/[peertable.cpp](src/gnb/xn/peertable.cpp) | `XnPeerTable` (per-peer info: NCI, PLMN/TAC lists, SCTP association, connection state) and `StreamIdManager` (per-peer SCTP stream allocator) |
| [transport.cpp](src/gnb/xn/transport.cpp) | SCTP receive: outer XnAP-PDU APER decode and dispatch by procedure code |
| [encode.hpp](src/gnb/xn/encode.hpp) | APER/XER encode + APER decode helpers (`xnap_encode::Encode/Decode`) |
| [interface.cpp](src/gnb/xn/interface.cpp) | Xn Setup procedure (send/receive Request, Response, Failure), association up/down handling, peer-loss cleanup |
| [handover.cpp](src/gnb/xn/handover.cpp) | All handover procedures: HandoverRequest/Ack/PreparationFailure, SNStatusTransfer, HandoverCancel (+CHO variant), HandoverSuccess, UEContextRelease, timeout supervision |
| [src/gnb/ngap/handover_xn.cpp](src/gnb/ngap/handover_xn.cpp) | NGAP-side Xn support: provisional target NGAP/GTP context (`prepareXnHandover`), activation, source release, target cancel, PathSwitchRequest/Ack/Failure |
| [src/gnb/gtp/task.cpp](src/gnb/gtp/task.cpp) | DL data forwarding during handover: `FORWARDING_TUNNEL_SETUP`, per-packet relay to the target's forwarding tunnel, GTP-U End Marker handling |
| [src/gnb/nts.hpp](src/gnb/nts.hpp) | Message structs: `NmGnbRrcToXn`, `NmGnbXnToRrc`, `NmGnbXnToGtp`, plus `XN_*` cases in `NmGnbRrcToNgap` |
| [src/gnb/types.hpp](src/gnb/types.hpp) | `XnHandoverCoreContext`, `GnbHandoverUeContexts`, neighbor `xnAddress`/`xnPort`/`handoverInterface`, gNB config `xnIp`/`xnPort` |
| src/asn/xnap | asn1c-generated XnAP rel-18 types (`ASN_XNAP_*`) |

Like RRC (and unlike RLS), Xn is a **single NTS task** per gNB, started only when `config->xn.enabled` is set. It talks to: a **dedicated Xn SCTP task** (`xnSctpTask`, logger `sctp-xn` — a second instance of the same `SctpTask` class used for the AMF link, kept separate so AMF ctxIds and neighbor gnbIds can't collide in the clientId space and Xn connect stalls can't block NGAP), the RRC task (all handover decisions/state transitions), the NGAP task (indirectly, via RRC relaying `XN_HANDOVER_PREPARE` etc.), the GTP task (forwarding-tunnel setup), and — one direct cross-thread call — the RLS control task (`copyUeContext()` snapshot for SN Status Transfer). It also directly calls `getUeContext()` on the RRC, NGAP, and GTP tasks (see issue 19 on synchronization).

## Peer management and Xn Setup

### Connection lifecycle (task.cpp / interface.cpp)

`updateXnConnections()` runs at startup and every 10 s (`TIMER_NEIGHBOR_CHECK`). It diffs the runtime neighbor list against the peer table:
- Neighbors with `handoverInterface == Xn` and a configured `xnAddress:xnPort` that aren't yet peers are added (`XnPeerInfo{gnbId, nci, addr}`) and an SCTP `CONNECTION_REQUEST` is pushed (clientId = neighbor's gnbId, local bind = `config.xn.xnIp`, PPID XNAP, up to 10000 streams requested each way).
- Peers whose NCI has left the neighbor list get an SCTP `CONNECTION_CLOSE` and are removed.

`handleAssociationSetup()` (SCTP association established) records the association, resets the peer's `StreamIdManager` to this side's parity half of `[2, min(inStreams,outStreams))` (streams 0/1 reserved for non-UE-associated signalling; lower gnbId → EVEN, higher → ODD, see SCTP streams below), and sends an **XnSetupRequest**. `handleAssociationShutdown()` first resolves any in-flight handovers with that peer (`handlePeerHandoverLoss()` — source-side entries produce a synthetic `HANDOVER_PREPARATION_FAILURE_RECEIVED` to RRC, target-side entries a synthetic `HANDOVER_CANCEL_RECEIVED`), then removes the peer.

### Xn Setup procedure (interface.cpp)

`xnSetupRequestSend()` builds the four IEs on stream 0:
1. **GlobalNG-RANNode-ID** — gNB-ID bit string, `gnbIdLength` bits of the NCI (same encoding as NG Setup).
2. **TAISupport-List** — one TAI from `cfg->tac`/`cfg->plmn`; slice list from the first configured NSSAI slice (fallback SST=1).
3. **AMF-Region-Information** — placeholder region ID 0 (assumption noted in code; should come from the connected AMF's GUAMI).
4. **ServedCells-NR** — NR-CGI from the 36-bit `cfg->nci`; **`nrPCI` is set to the NCI** (simulator convention, same 0..1007 constraint concern as RRC's physCellId issue); fixed dummy RF parameters (TDD n78, ARFCN 632628, 30 kHz SCS, 106 RB) and a 1-byte dummy MeasurementTimingConfiguration.

`xnSetupRequestReceive()` decodes the same IEs into a fresh `XnPeerInfo` (the ServedCells NCI overrides the NCI base derived from the gNB-ID), validates the three mandatory IEs (else **XnSetupFailure** with cause protocol/abstract-syntax-error-reject), stores the peer, and answers **XnSetupResponse** (same IE set; AMF-Region and ServedCells optional). `xnSetupResponseReceive()` decodes and stores the peer info from the responder. `xnSetupFailureReceive()` logs the cause and does not add the peer.

**Note (see issues 2–3):** both receive paths `addPeerInfo()` a *new* entry instead of updating the entry `updateXnConnections()` already created, and no path ever sets `connectionState = CONNECTED` — which every handover send path requires.

### SCTP streams

`StreamIdManager` is a bitset+pool allocator per peer. Stream 0 is used for non-UE-associated signalling (Xn Setup). Each handover allocates one UE-associated stream at the source (`sendHandoverRequest`) and reuses it for all subsequent messages of that handover (per the standard's intent, stored in `XnPendingHandover::streamId`); the target records the request's arrival stream and replies on it, so both endpoints use one stream per handover (issue 9, fixed). To keep opposing handovers (each gNB initiating toward the other) from independently picking the same ID, the shared `[2, min(inStreams,outStreams))` space is **partitioned by parity**: the lower-gnbId side allocates EVEN streams, the higher-gnbId side ODD (`StreamParity`, set in `resetStreams()` from the gnbId comparison at association setup). A handover thus always runs on a stream of the initiator's parity; the responder never draws it from its own (opposite-parity) allocator. Streams are released back to the pool whenever a pending entry is torn down, via `removeSourcePendingHandover()` / `removeTargetPendingHandover()` (issue 10, fixed) — `release()` is a guarded no-op for a peer-chosen (opposite-parity) stream, so it cannot pollute the local pool.

## Handover Preparation phase

### Source side

RRC decides the handover (see RRC_summary) and sends `NmGnbRrcToXn::HANDOVER_REQUEST_SEND` (ueId, targetNci, opaque RRC container, optional `choParams`). The Xn task first gathers **all four UE context snapshots** — NGAP context, RRC context, GTP context, and the GTP PDU-session list — via `GetUeContexts()` ([task.cpp:220](src/gnb/xn/task.cpp#L220)). If any is missing (e.g. sessions still being established), the request goes to the **deferred queue**, retried every 200 ms up to 5 times; on final failure a synthetic `HANDOVER_PREPARATION_FAILURE_RECEIVED` is sent to RRC so its `handoverDecisionPending` state resolves.

`sendHandoverRequest()` ([handover.cpp:163](src/gnb/xn/handover.cpp#L163)) requires the target peer to be present and `CONNECTED`, then builds the XnAP **HandoverRequest**:
- **sourceNG-RANnodeUEXnAPID** = ueId (64-bit, simulator convention)
- **Cause** = handover-desirable-for-radio-reasons (or cho-cpc-resources-tobechanged for CHO)
- **targetCellGlobalID** = NR-CGI from targetNci + the peer's first PLMN
- **GUAMI** from the RRC context
- **UEContextInfoHORequest**: AMF-UE-NGAP-ID, source NGAP IP (`cfg->ngapIp`, IPv4 only), UE security capabilities + AS security (KgNB\*, NCC) from the RRC context, UE-AMBR from the GTP context, and one `PDUSessionResourcesToBeSetup-Item` per session (S-NSSAI, session AMBR, UL tunnel at UPF, source DL tunnel, session type, QoS flows as non-dynamic 5QI, plus a `DataforwardingandOffloadingInfofromSource` proposing DL forwarding for every QFI); the opaque `rrc-Context` container from RRC
- **UEHistoryInformation** — 1-byte dummy placeholder
- **CHOinformation-Req** (CHO only): trigger (initiation/replace), optional target UE XnAP ID, arrival probability, and optional extensions CHO-Maxnoof-CondReconfig and CHOTimeBasedInformation (T1 window)

It allocates an SCTP stream, sends, and records an `XnPendingHandover` (role SOURCE) in `m_outgoingRequestsByUeId` with targetNci/targetGnbId/isCho/streamId/timestamp.

### Target side

`receiveHandoverRequest()` ([handover.cpp:813](src/gnb/xn/handover.cpp#L813)) assigns a local **xnTxId** (target-only transaction id; the wire correlates by UE XnAP IDs), decodes all the IEs above into local values, requires the rrc-Context and sourceNG-RANnodeUEXnAPID (else logged drop — no error indication yet), stores an `XnPendingHandover` (role TARGET) in `m_pendingRequestsByTxId`, and pushes `NmGnbXnToRrc::HANDOVER_REQUEST_RECEIVED` to RRC carrying the RRC container, the decoded session list, and an `XnHandoverCoreContext` (AMF reference, GUAMI, security, AMBR, source NGAP IP). Note the CHO IE's *contents* are dropped — only `isCho` survives (issue 12).

RRC (see RRC_summary "Target-side handover") decodes the container, admits sessions (admit-all today), pre-programs RLS bearers, has NGAP build the provisional target context (`XN_HANDOVER_PREPARE` → `prepareXnHandover()`), and answers either `HANDOVER_REQUEST_ACK_SEND` or `HANDOVER_PREPARATION_FAILURE_SEND` (both carrying xnTxId back).

`sendHandoverRequestAck()` ([handover.cpp:1192](src/gnb/xn/handover.cpp#L1192)) looks up the pending entry by xnTxId, marks `ackSent`, adopts the RRC-assigned ueId as `targetUeXnApId`, and builds the **HandoverRequestAcknowledge**: both UE XnAP IDs, `PDUSessionResourcesAdmitted-List` (per admitted session: admitted QFIs, accepted-for-forwarding QFIs, and a **DL forwarding tunnel endpoint** at this gNB — gtpIp + a TEID from the private `m_xnDownlinkTeidCounter`; issue 11 — this TEID is now registered with GTP via `FORWARDING_TEID_REGISTER`), optional `PDUSessionResourcesNotAdmitted-List`, and the target→source transparent container (the `RRCReconfiguration` handover command) as an OCTET STRING.

`sendHandoverPreparationFailure()` ([handover.cpp:1526](src/gnb/xn/handover.cpp#L1526)) sends UnsuccessfulOutcome with the echoed source UE XnAP ID and a Cause built from the requested cause group, then erases the pending entry.

### Back at the source

`receiveHandoverRequestAck()` ([handover.cpp:2254](src/gnb/xn/handover.cpp#L2254)) decodes both UE IDs, the admitted list (collecting per-PSI DL forwarding tunnels), and the RRC container; validates them; matches `m_outgoingRequestsByUeId` (ueId + sending gnbId); marks `ackReceived` and resets the timestamp (preparation timer → overall timer); pushes one `NmGnbXnToGtp::FORWARDING_TUNNEL_SETUP` per admitted session to GTP (which flips those sessions into relay mode — see GTP notes below); and forwards `HANDOVER_REQUEST_ACK_RECEIVED` (with targetNci/isCho resolved from the pending entry) to RRC, which delivers the handover command to the UE.

`receiveHandoverPreparationFailure()` matches by source UE ID + gnbId, forwards `HANDOVER_PREPARATION_FAILURE_RECEIVED` (with cause group) to RRC, and erases the pending entry.

## Handover Execution phase

- **SNStatusTransfer** (source → target, after the handover command is delivered to the UE): `sendSnStatusTransfer()` ([handover.cpp:1871](src/gnb/xn/handover.cpp#L1871)) requires a completed ACK correlation, snapshots the UE's radio bearers directly from the RLS control task (`copyUeContext()` — the shared-mutex accessor RLS provides for exactly this), and encodes one `DRBsSubjectToStatusTransfer-Item` per DRB, splitting RLS's combined 32-bit sequence count into the standard 18-bit PDCP SN + HFN. `receiveSnStatusTransfer()` matches the pending target entry by the UE-ID pair and forwards `SN_STATUS_TRANSFER_RECEIVED` (drbId/ul/dl list) to RRC, which applies it to RLS (`APPLY_DRB_SN_STATUS`).
- **HandoverSuccess** (target → source, when the UE's `RRCReconfigurationComplete` arrives at the target): `sendHandoverSuccess()` includes both UE IDs and this cell's CGI; marks the target entry `executionSucceeded`. `receiveHandoverSuccess()` at the source matches IDs **and** NCI, sets `executionSucceeded` (blocking late cancels), and forwards `HANDOVER_SUCCESS_RECEIVED` to RRC (which retains all source state until UEContextRelease).
- **UEContextRelease** (target → source, after the AMF acknowledges the Path Switch): `sendUeContextRelease()` finds the TARGET-role entry by `targetUeXnApId`, sends both IDs, and erases the entry — the end of the handover at the target. `receiveUeContextRelease()` at the source validates the full ID pair + peer, forwards `UE_CONTEXT_RELEASE_RECEIVED` to RRC (which locally releases RRC state and tells NGAP to tear down source N2/N3), erases the outgoing entry, and records the ueId in `m_completedSourceReleases` so a duplicate release is a warn-level no-op.
- **HandoverCancel / ConditionalHandoverCancel** (source → target): `sendHandoverCancel()` picks the body type and procedure code by `isCho`, includes the source (and, if known, target) UE XnAP ID and cause procedure-cancelled, then erases the outgoing entry. `receiveHandoverCancel()` handles both procedure codes, matches the target pending entry by IDs, ignores late cancels after `executionSucceeded`, forwards `HANDOVER_CANCEL_RECEIVED` to RRC (idempotent cleanup of the provisional context) and erases the entry.

### Timers (handover.cpp `processHandoverTimeouts`, 1 s tick)

Source side implements the two XnAP-defined supervision timers over the single `timestamp` field:
- **Preparation timer** (5 s): no ACK since the request → synthetic `HANDOVER_PREPARATION_FAILURE_RECEIVED` (cause transport) to RRC, entry removed.
- **Overall timer** (10 s basic / 100 s CHO, aligned with RRC/NGAP CHO timeouts): ACK received but no UEContextRelease → sends HandoverCancel to the target and removes the entry (with TODOs for tearing down forwarding tunnels and telling RRC).

The target-side timer block is an **empty placeholder** ("insert timers here") — see issue 8.

## NGAP-side Xn support (handover_xn.cpp)

- `prepareXnHandover()` (from RRC's `XN_HANDOVER_PREPARE` during target-side preparation): resolves the transferred GUAMI against connected AMFs, builds a provisional `NgapUeContext` (pending state, fresh RAN-UE-NGAP-ID, transferred AMF-UE-NGAP-ID/AMBR/security) in `m_xnHandoversPending`, pushes the AMBR to GTP, and runs `setupPduSessionResource()` for each transferred session (re-keyed to the local ueId) so GTP tunnels exist before the UE arrives.
- `activateXnHandover()` promotes the provisional context into `m_ueCtx`; called lazily by `sendPathSwitchRequest()` when RRC reports handover completion (`PATH_SWITCH_REQUEST`).
- `sendPathSwitchRequest()` sends the NGAP PathSwitchRequest (UE NGAP IDs and UserLocationInformation are auto-inserted by `sendNgapUeAssociated`); Ack/Failure are relayed to RRC as `PATH_SWITCH_REQUEST_ACK`/`_FAILURE` (Ack triggers the source UE Context Release over Xn). Note the missing mandatory IEs — issue 13.
- `releaseXnSourceContext()` (source, after target's UEContextRelease) and `cancelXnTargetPreparation()` (rollback) release NGAP+GTP state idempotently.

## GTP data forwarding (source side)

Once `FORWARDING_TUNNEL_SETUP` records a forwarding tunnel for a UE/PSI, the source GTP task stops delivering DL packets to RLS for that session and instead re-wraps each GTP-U payload with the target's forwarding TEID and sends it to the target's GTP address; a GTP-U **End Marker** from the UPF is forwarded and closes the forwarding tunnel. (The receiving half at the target is wired up as of issue 11's fix: the target registers each advertised forwarding TEID with its GTP task, matches incoming G-PDUs on it, and delivers them to the UE via RLS.)

## Sequence summary (successful Xn handover)

1. Both gNBs list each other as Xn neighbors → SCTP association → XnSetupRequest/Response exchange populates the peer tables.
2. Source RRC picks an Xn target from a MeasurementReport → `HANDOVER_REQUEST_SEND` → Xn gathers NGAP/RRC/GTP contexts (deferring if needed) → XnAP **HandoverRequest** with the full core context + RRC container.
3. Target Xn stores a pending entry (xnTxId) → RRC decodes the container, admits sessions, pre-programs RLS, NGAP builds the provisional context + GTP tunnels → **HandoverRequestAcknowledge** with admitted sessions, DL forwarding tunnels, and the `RRCReconfiguration` command.
4. Source applies forwarding tunnels to GTP (DL traffic now relays toward the target), delivers the command to the UE, then sends **SNStatusTransfer** (bearer SN snapshot from RLS).
5. UE arrives at the target (`RRCReconfigurationComplete`) → target RRC promotes the pending context → **HandoverSuccess** to the source + NGAP **PathSwitchRequest** to the AMF.
6. PathSwitchRequestAcknowledge → target sends **UEContextRelease** → source releases RRC/NGAP/GTP state; the UPF's End Marker closes the forwarding tunnel.
7. Failures map to **HandoverPreparationFailure** (pre-ACK), **HandoverCancel** (post-ACK, e.g. overall timeout), and synthetic failure/cancel messages on SCTP association loss.

---

## Issues found during this review

*Reviewed against the working tree on 2026-07-16 (branch `xn`).*



### Bugs

2. ~~**MUST FIX — `connectionState` never reaches `CONNECTED`, so every handover send path is dead.**~~ **[FIXED 2026-07-16]**: previously the only assignment anywhere was `CONNECTION_REQUESTED` after sending XnSetupRequest, while every handover send path requires `CONNECTED`. Now, per TS 38.423 completion semantics: the initiator sets `CONNECTED` on receiving XnSetupResponse (`xnSetupResponseReceive()`), and the responder sets it after successfully sending the response — `xnSetupResponseSend()` now returns `bool` and `xnSetupRequestReceive()` gates the transition on it. `xnSetupFailureReceive()` now marks the peer `CONNECTION_FAILED` (previously an unused enum value), giving the future reconnect policy (issue 21) a hook. Note: end-to-end this remains untestable until issue 21a (no SCTP listener) is fixed.





13. ~~**PathSwitchRequest omits mandatory IEs.**~~ **[FIXED 2026-07-17]**: `sendPathSwitchRequest()` ([ngap/handover_xn.cpp](src/gnb/ngap/handover_xn.cpp)) now carries the full TS 38.413 §9.2.3.21 mandatory set: RAN-UE-NGAP-ID + UserLocationInformation (auto-inserted as before), **SourceAMF-UE-NGAP-ID** (id 100 — previously the AMF id went out under the *wrong* IE id 10, because `AddProtocolIeIfUsable` matches by union-member type; `sendNgapUeAssociated` now suppresses the generic id-10 insert when an id-100 IE is present, a guarded no-op for every other message), **UESecurityCapabilities** (from the Xn-transferred security info in the activated context), and **PDUSessionResourceToBeSwitchedDLList** with a per-session APER-encoded `PathSwitchRequestTransfer` (target's DL NG-U tunnel from the GTP session tree + accepted QoS flows) so the 5GC re-points the UPF at this gNB. Transfers are encoded before any IE is allocated so failures abort cleanly; no sessions in GTP aborts the send with an error. Verified by a standalone APER round-trip test: 5 IEs in canonical order (85/100/121/119/76), encode+decode clean. *Residual:* the session list is read via the unsynchronized `GtpTask::getPduSessions()` accessor (issue 19 pattern). **[Ack handling added 2026-07-17]**: `receivePathSwitchRequestAcknowledge()` now processes the Ack's content per TS 38.413 §9.2.3.22 — the mandatory **SecurityContext** {NCC, NH} rides on the `PATH_SWITCH_REQUEST_ACK` message to RRC, which stores it in the UE context (`nextHopChainingCount`/`nextHopParameter`, the values the next sourced handover carries; store-only, no SecurityModeCommand); each **PDUSessionResourceSwitchedList** item's `PathSwitchRequestAcknowledgeTransfer` is decoded and a re-allocated UL NG-U endpoint is applied to GTP via a new `SESSION_UL_TUNNEL_UPDATE` message (`GtpTask::handleUlTunnelUpdate()` re-points `session->upTunnel`, so uplink follows the switched path); **PDUSessionResourceReleasedListPSAck** sessions are released in GTP and removed from the NGAP context. All three paths verified by an APER round-trip test building the Ack as an AMF would and running the same extraction logic. **[RRC/RLS bearer teardown added 2026-07-17]**: RRC is now the authoritative owner of the `psi → {qfi → drbId}` mapping (new `RrcUeContext::bearerMap`, populated in `createRadioBearerConfig` alongside the existing RLS/UE bearer programming). NGAP no longer silently drops radio state on release: both the path-switch-ack release (`receivePathSwitchRequestAcknowledge`) and the normal `PDUSessionResourceReleaseCommand` (`receiveSessionResourceReleaseCommand`) now forward the released PSIs to RRC over the existing `NmGnbNgapToRrc::PDU_SESSION_UPDATE` message (new `releasedPsis` field). RRC's new `handleNgapPduSessionRelease()` looks up the DRB(s)/QFIs for each PSI, tells RLS to delete them (`RADIO_BEARER_UPDATE` with `deleteBearers`/`deleteSdapMappings` — the previously-unexercised delete half of that message and its `ctl_task.cpp` handler), tells the UE to release the DRBs (`RRCReconfiguration` with `drb-ToReleaseList`, already handled UE-side in `reconfig.cpp`), and prunes `bearerMap`. Builds clean; not yet exercised live (Xn path blocked by issue 30; NGAP-release path needs a full core+UE scenario). *Caveat:* `createRadioBearerConfig` restarts its `drbs_used` DRB-id counter at 1 each call, so a session added in a later update can collide with an existing DRB id — `bearerMap` mirrors whatever id was actually programmed, but the underlying counter bug is untouched (separate fix).



30. **MUST FIX — the entire XnAP ASN.1 layer is ABI-broken: every encode/decode crashes at runtime.** (Found 2026-07-17, exposed by the first live association after fixing 21a.) The XnAP code in `src/asn/xnap` was generated by **asn1c-0.9.24** and ships its own old-style skeleton headers (`asn_TYPE_descriptor_t` with inline function pointers and `td->per_constraints`), while NGAP/RRC were generated by **asn1c-0.9.29** whose modern skeleton (`td->op` operation table, `td->encoding_constraints`) is what the linked `asn-asn1c` runtime implements. The `asn-xnap` CMake target compiles only the generated `ASN_XNAP_*.c` files (against the old headers, so all descriptors have the old layout) but links the modern runtime — and a "compatibility typedef for older asn1c-generated code (e.g. XnAP)" in [src/asn/asn1c/constr_TYPE.h](src/asn/asn1c/constr_TYPE.h) masks the mismatch at compile time. Confirmed live: `ANY_fromType_aper(asn_DEF_ASN_XNAP_GlobalNG_RANNode_ID, …)` — the first XnAP encode ever executed — segfaults reading the old-layout descriptor with modern-layout code. **Every** XnAP encode, decode, and `asn::Free` call on `ASN_XNAP_*` types has this defect; none of the XnAP wire code in this summary has ever actually run. Fix: regenerate `asn1-definitions/xnap-rel18-v18_8.asn1` with the same asn1c-0.9.29 (flags per the NGAP headers: `-pdu=all -fcompound-names -findirect-choice -fno-include-deps -no-gen-OER -gen-PER -no-gen-example -D xnap`, prefix `ASN_XNAP_`), delete the old skeleton from `src/asn/xnap`, and adapt the Xn code to the 0.9.29 naming/IE conventions (typed `_IEs` unions like NGAP instead of ANY-packed `ProtocolIE_Field_14202P0` values — the `encode.hpp` ANY helpers largely disappear). Note asn1c is not installed on this machine; the 0.9.29 fork used for NGAP must be obtained/built first.

### Incomplete / in-progress functionality

16. **No XnAP ErrorIndication.** Malformed or unmatchable PDUs are logged and dropped throughout (`receiveHandoverRequest`, `receiveHandoverRequestAck`, `sendHandoverRequestAck` correlation failures, etc. — the "TODO: send XnAP error indication" comments). The peer discovers problems only via its supervision timers.

17. **Overall-timeout cleanup is partial.** On overall-timer expiry the source sends HandoverCancel, but the in-code TODOs remain: GTP forwarding tunnels for the UE are not torn down (DL traffic keeps relaying to a dead handover until the End Marker or session release), and RRC is not told the execution failed (it already processed the ACK, so its handover state resolution is unclear).

18. **Xn Setup content assumptions** (all marked in code): ServedCells-NR advertises fixed dummy RF parameters; `nrPCI` carries the NCI (breaks the 0..1007 PER constraint for large NCIs — same family as RRC_summary issue 4); UEHistoryInformation is a 1-byte dummy. Multi-cell gNBs, multiple slices per TAI, and IPv6 transport addresses are unsupported.

19. **Cross-thread `getUeContext()` reads are only partly synchronized.** `GetUeContexts()` ([task.cpp:220](src/gnb/xn/task.cpp#L220)) calls into RRC (shared-mutex protected since 2026-07-15), but the NGAP and GTP `getUeContext()`/`getPduSessions()` calls on the same line are still unsynchronized reads of those tasks' state from the Xn thread — carried over from RRC_summary issue 3. Note `getPduSessions` returns raw `PduSessionResource*` pointers into GTP's tree, which `sendHandoverRequest` then dereferences at leisure — a session released mid-preparation is a use-after-free.

20. **`m_completedSourceReleases` grows without bound** and its guard is only consulted when the pending entry is already gone ([handover.cpp:2588](src/gnb/xn/handover.cpp#L2588)); the code's own TODO questions its purpose. A time-bounded ledger (or reusing RRC's idempotent release handling) would be cleaner.

21a. ~~**MUST FIX — no Xn SCTP association can establish: nothing listens on xnPort.**~~ **[FIXED 2026-07-17 — accept-style listener implemented]**: the SCTP lib gained `AcceptConnection()`/`QueryStatus()`, `SctpServer::acceptClient()`, an adopt-fd `SctpClient` constructor, and `SO_REUSEADDR` on the listen socket; `SctpTask` gained an additive `LISTEN_REQUEST` path (listen socket + accept thread; each accepted association becomes an ordinary `ClientEntry` with a negative task-assigned clientId, and a synthesized `ASSOCIATION_SETUP` carries the SCTP_STATUS stream counts) — the NGAP client path is untouched. `XnTask` listens on `xn.xnIp:xnPort`, and collisions are avoided by the **initiator rule**: only the gNB with the *lower* gnbId connects outbound; the higher one waits for the inbound association. Inbound peers are identified from the XnSetupRequest's **GlobalNG-RANNode-ID** (the bit-string value is the gnbId, same derivation as `getGnbId()`); unknown gNBs (not configured Xn neighbors) are rejected with XnSetupFailure and their association closed; unbound inbound associations may only carry the XnSetup procedure and are pruned after 30 s without one. `XnPeerInfo::clientId` is now decoupled from gnbId (all send sites use `peer->clientId`; the receive dispatcher resolves clientId→gnbId via `findByClientId()`). Verified live: listener up on both gNBs, gnb1 (lower id) connects, gnb2 accepts with clientId=-2 and 10000 negotiated streams — at which point the first-ever XnAP encode crashed, exposing issue 30 below.

21b. ~~**`XnTask` is never started.**~~ **[FIXED 2026-07-16]**: `start()`/`quit()`/`delete` are now wired in [gnb.cpp](src/gnb/gnb.cpp), with startup gated on the existing `config->xn.enabled` YAML flag (default false; sample configs carry `enabled: false`). The Xn stack also now runs on a **dedicated second `SctpTask` instance** (`m_base->xnSctpTask`, logger `sctp-xn`, started just before `XnTask`): this structurally eliminates the clientId collision between AMF ctxIds and neighbor gnbIds in the shared `m_clients` map (observed live: AMF clientId=2 and gNB gnbId=2 coexisting), and isolates Xn's blocking connects/retry churn from AMF NGAP traffic. `SctpTask` gained a logger-name constructor parameter, and `receiveConnectionClose()` now erases the map entry after deletion (previously left a dangling pointer that a late receiver-thread message or `onQuit()` would dereference — latent for NGAP, which never closes connections, but exercised by Xn's neighbor churn and retry path).

21. **Peer reconnection is coarse.** *Partially addressed 2026-07-16*: `SctpTask` now pushes a `CONNECTION_FAILED` notification to the requesting task when bind/connect fails (NGAP logs it; no AMF retry policy yet), and `XnTask::handleConnectionFailure()` marks the peer `CONNECTION_FAILED` so `updateXnConnections()` retries it on the 10 s neighbor tick (issuing a `CONNECTION_CLOSE` first, so a retry after XnSetupFailure — which also sets `CONNECTION_FAILED` but leaves a live association — cannot leak the old `ClientEntry`). Verified live: connect-refused → failure notification → retry every 10 s. Remaining: a peer stuck in `CONNECTION_REQUESTED` (connect succeeded but the Xn Setup exchange never completes) is never retried; "update connection parameters if needed" is still a TODO; and there is no Xn Setup collision handling when both gNBs initiate simultaneously (both will connect and each side answers the other's setup — harmless today, but it doubles associations).

### Cleanups

22. **Dead code:** `XnPeerTable::updatePeerSctpInfo()` has no callers.


26. `receiveHandoverCancel()` and `receiveHandoverSuccess()` don't null-check `initiatingMessage->value.buf` before decoding (the other receive paths do).


