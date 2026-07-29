# Xn (XnAP) Layer — Summary

Xn is the gNB↔gNB interface (3GPP TS 38.423, XnAP). In UERANSIM it exists **only on the gNB side** — there is no UE-side Xn code. The UE participates in Xn handover indirectly through RRC (it receives the `RRCReconfiguration` handover command and answers `RRCReconfigurationComplete` at the target; see RRC_summary.md). This summary covers the gNB `XnTask` plus the NGAP-side Xn support (Path Switch) and the GTP-side data forwarding it drives.

The Xn layer owns:
- **Peer discovery and SCTP connection management** to neighbor gNBs (from the runtime neighbor store), including the **Xn Setup** procedure (XnSetupRequest/Response/Failure)
- **Handover Preparation** (XnAP HandoverRequest / HandoverRequestAcknowledge / HandoverPreparationFailure), carrying the UE's core-network context (AMF reference, security, AMBR, PDU sessions) and the opaque RRC container between source and target
- **Handover Execution** support (SNStatusTransfer, HandoverSuccess, UEContextRelease, HandoverCancel / ConditionalHandoverCancel)
- **Handover supervision timers** (preparation and overall, source side) and a deferred-send queue for requests whose UE contexts are not yet available

Unlike RLS (custom UDP wire format), Xn uses real ASN.1 APER-encoded XnAP PDUs over SCTP (PPID = XNAP), so the messages are interoperable with Release 18 conforming messages.

Identification note: the simulator identifies a UE internally by a stable 64-bit `ueId` (IMSI-derived), but `NG-RANnodeUEXnAPID` is `INTEGER (0..4294967295)`, so the ueId **cannot** go on the wire unchanged — an earlier version of this note claimed the IE tolerated long values; it does not (see issue 32). `XnTask::xnApIdFromUeId()` derives the 32-bit wire id and is the single place that mapping lives; `findSourcePendingByXnApId()` is the reverse seam. NCIs identify cells; `gnbId` (from the neighbor store) doubles as the SCTP `clientId` for each peer association.

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

Like RRC (and unlike RLS), Xn is a **single NTS task** per gNB, started only when `config->xn.enabled` is set. It sends/receives NTS messages with: 
- a **dedicated Xn SCTP task** (`xnSctpTask`, logger `sctp-xn` — a second instance of the same `SctpTask` class used for the AMF link, kept separate so AMF ctxIds and neighbor gnbIds can't collide in the clientId space and Xn connect stalls can't block NGAP), 
- the RRC task (all handover decisions/state transitions), 
- the GTP task (forwarding-tunnel setup).

Note it never pushes to the NGAP task: NGAP-side Xn work is reached indirectly, via RRC relaying `XN_HANDOVER_PREPARE` and friends.

Separately from NTS messaging, it reads UE context by **calling directly into other tasks' objects** — `getUeContext()` on NGAP, RRC and GTP plus `getPduSessions()` on GTP (all in `GetUeContexts()`), and `copyUeContext()` on the RLS control task for the SN Status Transfer snapshot (see issue 19 on synchronization).

## Peer management and Xn Setup

### Connection lifecycle (task.cpp / interface.cpp)

At startup `XnTask` pushes a `LISTEN_REQUEST` to the Xn SCTP task, so every gNB both listens on `xn.xnIp:xnPort` **and** may dial out.

`updateXnConnections()` runs at startup and every 10s thereafter (`TIMER_NEIGHBOR_CHECK`). It diffs the runtime neighbor list against the peer table:
- **Only the gNB with the lower `gnbId` dials out** (`if (myGnbId > neighborGnbId) continue;`). This is the collision-avoidance rule: without it both sides connect and each answers the other's setup, doubling associations. The higher-gnbId side simply waits for the inbound association, and gets its peer entry when the XnSetupRequest arrives.
- Qualifying neighbors (`handoverInterface == Xn`, a configured `xnAddress:xnPort`, not yet a peer) are added as `XnPeerInfo{gnbId, nci, addr}` in state `CONNECTION_REQUESTED`, and an SCTP `CONNECTION_REQUEST` is pushed (clientId = neighbor's gnbId, local bind = `config.xn.xnIp`, PPID XNAP, up to 10000 streams requested each way).
- Peers in `CONNECTION_FAILED` are retried on this same tick (a `CONNECTION_CLOSE` goes first so a retry after XnSetupFailure — which leaves a live association — cannot leak the old `ClientEntry`).
- Peers whose NCI has left the neighbor list get an SCTP `CONNECTION_CLOSE` and are removed.
- Inbound associations that never produced an XnSetupRequest are closed after `PENDING_INBOUND_TIMEOUT_MS` (30 s).

**Inbound path.** An accepted association arrives with a *negative, task-assigned* clientId and is parked in `m_pendingInbound` — there is no peer entry yet, because the gNB's identity is not known until its XnSetupRequest arrives. That request's **GlobalNG-RANNode-ID** supplies the gnbId; a gNB that is not a configured Xn neighbor is rejected with **XnSetupFailure** and its association closed, as is a second association from a gNB already `CONNECTED`. Consequently `XnPeerInfo::clientId` is **decoupled from gnbId** (equal to it for outbound, negative for inbound): all send sites use `peer->clientId`, and the receive dispatcher resolves clientId → peer via `findByClientId()`.

`handleAssociationSetup()` (SCTP association established) records the association, resets the peer's `StreamIdManager` to this side's parity half of `[2, min(inStreams,outStreams))` (streams 0/1 reserved for non-UE-associated signalling; lower gnbId → EVEN, higher → ODD, see SCTP streams below), and sends an **XnSetupRequest**. `handleAssociationShutdown()` first resolves any in-flight handovers with that peer (`handlePeerHandoverLoss()` — source-side entries produce a synthetic `HANDOVER_PREPARATION_FAILURE_RECEIVED` to RRC, target-side entries a synthetic `HANDOVER_CANCEL_RECEIVED`), then removes the peer.

### SCTP streams allocation logic

`StreamIdManager` is a bitset+pool allocator per peer. Stream 0 is used for non-UE-associated signalling (Xn Setup). Each handover allocates one UE-associated stream at the source (`sendHandoverRequest`) and reuses it for all subsequent messages of that handover (per the standard's intent, stored in `XnPendingHandover::streamId`); the target records the request's arrival stream and replies on it, so both endpoints use one stream per handover. To keep opposing handovers (each gNB initiating toward the other) from independently picking the same ID, the shared `[2, min(inStreams,outStreams))` space is **partitioned by parity**: the lower-gnbId side allocates EVEN streams, the higher-gnbId side ODD (`StreamParity`, set in `resetStreams()` from the gnbId comparison at association setup). A handover thus always runs on a stream of the initiator's parity; the responder never draws it from its own (opposite-parity) allocator. Streams are released back to the pool whenever a pending entry is torn down, via `removeSourcePendingHandover()` / `removeTargetPendingHandover()` — `release()` is a guarded no-op for a peer-chosen (opposite-parity) stream, so it cannot pollute the local pool.

### Xn Setup procedure (interface.cpp)

Handshake to setup an Xn connection between two peer GNBs.

`xnSetupRequestSend()` builds the four IEs on stream 0:
1. **GlobalNG-RANNode-ID** — gNB-ID bit string, `gnbIdLength` bits of the NCI (same encoding as NG Setup).
2. **TAISupport-List** — one TAI from `cfg->tac`/`cfg->plmn`; slice list from the first configured NSSAI slice (fallback SST=1).
3. **AMF-Region-Information** — placeholder region ID 0 (assumption noted in code; should come from the connected AMF's GUAMI).
4. **ServedCells-NR** — NR-CGI from the 36-bit `cfg->nci`; **`nrPCI` is set to the NCI** (simulator convention, same 0..1007 constraint concern as RRC's physCellId issue); fixed dummy RF parameters (TDD n78, ARFCN 632628, 30 kHz SCS, 106 RB) and a 1-byte dummy MeasurementTimingConfiguration.

`xnSetupRequestReceive()` decodes the same IEs into a fresh `XnPeerInfo` (the ServedCells NCI overrides the NCI base derived from the gNB-ID), validates the three mandatory IEs (else **XnSetupFailure** with cause protocol/abstract-syntax-error-reject), stores the peer, and answers **XnSetupResponse** (same IE set; AMF-Region and ServedCells optional). 

`xnSetupResponseReceive()` decodes and stores the peer info from the responder. `xnSetupFailureReceive()` logs the cause and does not add the peer.


## Handover Preparation phase

### Source side

RRC decides to initiate the handover (see RRC_summary) and sends `NmGnbRrcToXn::HANDOVER_REQUEST_SEND` (ueId, targetNci, opaque RRC container, optional `choParams`). The Xn task first gathers **all four UE context snapshots** — NGAP context, RRC context, GTP context, and the GTP PDU-session list — via `GetUeContexts()` ([task.cpp:317](src/gnb/xn/task.cpp#L317)). If any is missing (e.g. sessions still being established), the request goes to the **deferred queue**, retried every 200 ms up to 5 times; on final failure a synthetic `HANDOVER_PREPARATION_FAILURE_RECEIVED` is sent to RRC so its `handoverDecisionPending` state resolves.  This procedure is used to handle handover signaling triggered prior to receiving context information from the AMF (possible in CHO situations).

`sendHandoverRequest()` ([handover.cpp:278](src/gnb/xn/handover.cpp#L278)) requires the target peer to be present and `CONNECTED`, then builds the XnAP **HandoverRequest**:
- **sourceNG-RANnodeUEXnAPID** = `xnApIdFromUeId(ueId)` — the low 32 bits of the ueId, because the IE is only 32 bits wide (issue 32). The same derived value is stored in the pending entry, so later correlation matches what actually went on the wire.
- **Cause** = handover-desirable-for-radio-reasons (or cho-cpc-resources-tobechanged for CHO)
- **targetCellGlobalID** = NR-CGI from targetNci + the peer's first PLMN
- **GUAMI** from the RRC context
- **UEContextInfoHORequest**: AMF-UE-NGAP-ID, source NGAP IP (`cfg->ngapIp`, IPv4 only), UE security capabilities + AS security (KgNB\*, NCC) from the RRC context, UE-AMBR from the GTP context, and one `PDUSessionResourcesToBeSetup-Item` per session (S-NSSAI, session AMBR, UL tunnel at UPF, source DL tunnel, session type, QoS flows as non-dynamic 5QI, plus a `DataforwardingandOffloadingInfofromSource` proposing DL forwarding for every QFI); the opaque `rrc-Context` container from RRC
- **UEHistoryInformation** — 1-byte dummy placeholder
- **CHOinformation-Req** (CHO only): trigger (initiation/replace), optional target UE XnAP ID, arrival probability, and optional extensions CHO-Maxnoof-CondReconfig and CHOTimeBasedInformation (T1 window)

It allocates an SCTP stream, sends, and records an `XnPendingHandover` (role SOURCE) in `m_pendingHandoversSourceByUeId` (keyed by the *full* ueId + targetGnbId) with targetNci/targetGnbId/isCho/streamId/timestamp.

### Target side

`receiveHandoverRequest()` ([handover.cpp:908](src/gnb/xn/handover.cpp#L908)) assigns a local **xnTxId** (target-only transaction id; the wire correlates by UE XnAP IDs), decodes all the IEs above into local values, requires the rrc-Context and sourceNG-RANnodeUEXnAPID (else logged drop — no error indication yet), stores an `XnPendingHandover` (role TARGET) in `m_pendingHandoversTargetByTxId`, and pushes `NmGnbXnToRrc::HANDOVER_REQUEST_RECEIVED` to RRC carrying the RRC container, the decoded session list, and an `XnHandoverCoreContext` (AMF reference, GUAMI, security, AMBR, source NGAP IP).

The RRC layer (see RRC_summary "Target-side handover") decodes the container, admits sessions, pre-programs RLS bearers, has NGAP build the provisional target context (`XN_HANDOVER_PREPARE` → `prepareXnHandover()`), and answers either `HANDOVER_REQUEST_ACK_SEND` or `HANDOVER_PREPARATION_FAILURE_SEND` (both carrying xnTxId back).

`sendHandoverRequestAck()` ([handover.cpp:1345](src/gnb/xn/handover.cpp#L1345)) looks up the pending entry by xnTxId, marks `ackSent`, derives `targetUeXnApId` from the RRC-assigned ueId with the same `xnApIdFromUeId()` seam the source uses, and builds the **HandoverRequestAcknowledge**: 
-   both UE XnAP IDs, 
-   `PDUSessionResourcesAdmitted-List` (per admitted session: admitted QFIs, accepted-for-forwarding QFIs, and a **DL forwarding tunnel endpoint** at this gNB — gtpIp + a TEID from the private `m_xnDownlinkTeidCounter`), 
-   optional `PDUSessionResourcesNotAdmitted-List`, 
-   the target→source transparent container (the `RRCReconfiguration` handover command) as an OCTET STRING.

`sendHandoverPreparationFailure()` ([handover.cpp:1674](src/gnb/xn/handover.cpp#L1674)) sends UnsuccessfulOutcome with the echoed source UE XnAP ID and a Cause built from the requested cause group, then erases the pending entry.

### Back at the source

`receiveHandoverRequestAck()` ([handover.cpp:2376](src/gnb/xn/handover.cpp#L2376)) decodes both UE IDs, the admitted list (collecting per-PSI DL forwarding tunnels), and the RRC container; validates them; correlates via `findSourcePendingByXnApId()` (the 32-bit wire id + sending gnbId — the map key is the full ueId, which the wire value no longer equals); marks `ackReceived` and resets the timestamp (preparation timer → overall timer); pushes one `NmGnbXnToGtp::FORWARDING_TUNNEL_SETUP` per admitted session to GTP (which flips those sessions into relay mode — see GTP notes below); and forwards `HANDOVER_REQUEST_ACK_RECEIVED` (with targetNci/isCho resolved from the pending entry) to RRC, which delivers the handover command to the UE.

`receiveHandoverPreparationFailure()` matches by source UE ID + gnbId, forwards `HANDOVER_PREPARATION_FAILURE_RECEIVED` (with cause group) to RRC, and erases the pending entry.

## Handover Execution phase

- **SNStatusTransfer** (source → target, after the handover command is delivered to the UE): `sendSnStatusTransfer()` ([handover.cpp:2004](src/gnb/xn/handover.cpp#L2004)) requires a completed ACK correlation, snapshots the UE's radio bearers directly from the RLS control task (`copyUeContext()` — the shared-mutex accessor RLS provides for exactly this), and encodes one `DRBsSubjectToStatusTransfer-Item` per DRB, splitting RLS's combined 32-bit sequence count into the standard 18-bit PDCP SN + HFN. `receiveSnStatusTransfer()` matches the pending target entry by the UE-ID pair and forwards `SN_STATUS_TRANSFER_RECEIVED` (drbId/ul/dl list) to RRC, which applies it to RLS (`APPLY_DRB_SN_STATUS`).
- **HandoverSuccess** (target → source, when the UE's `RRCReconfigurationComplete` arrives at the target): `sendHandoverSuccess()` includes both UE IDs and this cell's CGI; marks the target entry `executionSucceeded`. `receiveHandoverSuccess()` at the source matches IDs **and** NCI, sets `executionSucceeded` (blocking late cancels), and forwards `HANDOVER_SUCCESS_RECEIVED` to RRC (which retains all source state until UEContextRelease).
- **UEContextRelease** (target → source, after the AMF acknowledges the Path Switch): `sendUeContextRelease()` finds the TARGET-role entry by `targetUeXnApId`, sends both IDs, and erases the entry — the end of the handover at the target. `receiveUeContextRelease()` at the source validates the full ID pair + peer, forwards `UE_CONTEXT_RELEASE_RECEIVED` to RRC (which locally releases RRC state and tells NGAP to tear down source N2/N3), erases the outgoing entry, and records the **32-bit wire id** in `m_completedSourceReleases` so a duplicate release is a warn-level no-op (the wire id deliberately, since on a duplicate the pending entry holding the full ueId is already gone).
- **HandoverCancel / ConditionalHandoverCancel** (source → target): `sendHandoverCancel()` picks the body type and procedure code by `isCho`, includes the source (and, if known, target) UE XnAP ID and cause procedure-cancelled, then erases the outgoing entry. `receiveHandoverCancel()` handles both procedure codes, matches the target pending entry by IDs, ignores late cancels after `executionSucceeded`, forwards `HANDOVER_CANCEL_RECEIVED` to RRC (idempotent cleanup of the provisional context) and erases the entry.

### Timers (handover.cpp `processHandoverTimeouts`, 1 s tick)

All four timers run off one 1 s tick (`TIMER_HANDOVERS_PENDING`) over each entry's single `timestamp` field, and each fires at most once (`timeoutReported`). Every one of them notifies RRC with `NmGnbXnToRrc::HANDOVER_ABORT` carrying a distinct reason code — **but RRC does not handle that message at all, so all four notifications are currently dropped; see issue 33.**

Source side — the two timers XnAP actually defines:
- **Preparation timer** (`HANDOVER_PREPARATION_TIMEOUT_MS`, 5 s): no ACK since the request → `HANDOVER_ABORT` / `ABORT_REASON_SOURCE_PREPARATION_TIMEOUT`, entry removed (releasing its SCTP stream).
- **Overall timer** (`HANDOVER_OVERALL_TIMEOUT_MS` 10 s / `..._CHO_MS` 100 s, aligned with RRC/NGAP CHO timeouts): ACK received but no UEContextRelease → `HANDOVER_ABORT` / `ABORT_REASON_SOURCE_OVERALL_TIMEOUT`, then `sendHandoverCancel()` to the target (which erases the entry and releases the stream). The in-code TODO for tearing down the GTP forwarding tunnels remains — see issue 17.

Target side — XnAP defines no target timers, so these are implementation-specific failsafes against stale state:
- **RRC-ack timer** (`HANDOVER_RRC_ACK_TIMEOUT_MS`, 4 s): RRC never answered the `HANDOVER_REQUEST_RECEIVED` with an ack/failure → `HANDOVER_ABORT` / `ABORT_REASON_TARGET_PREPARATION_TIMEOUT`, and a **HandoverPreparationFailure** (cause transport — the code flags this cause as unverified) goes back to the source, which also erases the entry. Deliberately shorter than the source's 5 s preparation timer so the target loses the race and the source gets a real failure rather than a timeout.
- **Target overall timer** (`HANDOVER_OVERALL_TARGET_TIMEOUT_MS` 10 s / `..._CHO_MS` 100 s): ack sent but execution never succeeded → `HANDOVER_ABORT` / `ABORT_REASON_TARGET_OVERALL_TIMEOUT`. Nothing is sent to the source, on the assumption its own overall timer has already fired.

## NGAP-side Xn support (handover_xn.cpp)

- `prepareXnHandover()` (from RRC's `XN_HANDOVER_PREPARE` during target-side preparation): resolves the transferred GUAMI against connected AMFs, builds a provisional `NgapUeContext` (pending state, fresh RAN-UE-NGAP-ID, transferred AMF-UE-NGAP-ID/AMBR/security) in `m_xnHandoversPending`, pushes the AMBR to GTP, and runs `setupPduSessionResource()` for each transferred session (re-keyed to the local ueId) so GTP tunnels exist before the UE arrives.
- `activateXnHandover()` promotes the provisional context into `m_ueCtx`; called lazily by `sendPathSwitchRequest()` when RRC reports handover completion (`PATH_SWITCH_REQUEST`).
- `sendPathSwitchRequest()` sends the NGAP PathSwitchRequest (UE NGAP IDs and UserLocationInformation are auto-inserted by `sendNgapUeAssociated`); Ack/Failure are relayed to RRC as `PATH_SWITCH_REQUEST_ACK`/`_FAILURE` (Ack triggers the source UE Context Release over Xn). The mandatory-IE set and Ack processing were completed in issue 13.
- `releaseXnSourceContext()` (source, after target's UEContextRelease) and `cancelXnTargetPreparation()` (rollback) release NGAP+GTP state idempotently.

## GTP data forwarding (source side)

Once `FORWARDING_TUNNEL_SETUP` records a forwarding tunnel for a UE/PSI, the source GTP task stops delivering DL packets to RLS for that session and instead re-wraps each GTP-U payload with the target's forwarding TEID and sends it to the target's GTP address; a GTP-U **End Marker** from the UPF is forwarded and closes the forwarding tunnel. The receiving half at the target registers each advertised forwarding TEID with its GTP task, matches incoming G-PDUs on it, and delivers them to the UE via RLS.

## Sequence summary (successful Xn handover)

1. Both gNBs list each other as Xn neighbors → SCTP association → XnSetupRequest/Response exchange populates the peer tables.
2. Source RRC picks an Xn target from a MeasurementReport → `HANDOVER_REQUEST_SEND` → Xn gathers NGAP/RRC/GTP contexts (deferring if needed) → XnAP **HandoverRequest** with the full core context + RRC container.
3. Target Xn stores a pending entry (xnTxId) → RRC decodes the container, admits sessions, pre-programs RLS, NGAP builds the provisional context + GTP tunnels → **HandoverRequestAcknowledge** with admitted sessions, DL forwarding tunnels, and the `RRCReconfiguration` command.
4. Source applies forwarding tunnels to GTP (DL traffic now relays toward the target), delivers the command to the UE, then sends **SNStatusTransfer** (bearer SN snapshot from RLS).
5. UE arrives at the target (`RRCReconfigurationComplete`) → target RRC promotes the pending context → **HandoverSuccess** to the source + NGAP **PathSwitchRequest** to the AMF.
6. PathSwitchRequestAcknowledge → target sends **UEContextRelease** → source releases RRC/NGAP/GTP state; the UPF's End Marker closes the forwarding tunnel.
7. Failures map to **HandoverPreparationFailure** (pre-ACK), **HandoverCancel** (post-ACK, e.g. overall timeout), and synthetic failure/cancel messages on SCTP association loss.

---

## Open Issues 


### Bugs

2. ~~**MUST FIX — `connectionState` never reaches `CONNECTED`, so every handover send path is dead.**~~ **[FIXED 2026-07-16]**: previously the only assignment anywhere was `CONNECTION_REQUESTED` after sending XnSetupRequest, while every handover send path requires `CONNECTED`. Now, per TS 38.423 completion semantics: the initiator sets `CONNECTED` on receiving XnSetupResponse (`xnSetupResponseReceive()`), and the responder sets it after successfully sending the response — `xnSetupResponseSend()` now returns `bool` and `xnSetupRequestReceive()` gates the transition on it. `xnSetupFailureReceive()` now marks the peer `CONNECTION_FAILED` (previously an unused enum value), giving the future reconnect policy (issue 21) a hook. Note: this was untestable end-to-end until the Xn SCTP listener landed (`LISTEN_REQUEST` / accept path, 2026-07-17); verified live since.





13. ~~**PathSwitchRequest omits mandatory IEs.**~~ **[FIXED 2026-07-17]**: `sendPathSwitchRequest()` ([ngap/handover_xn.cpp](src/gnb/ngap/handover_xn.cpp)) now carries the full TS 38.413 §9.2.3.21 mandatory set: RAN-UE-NGAP-ID + UserLocationInformation (auto-inserted as before), **SourceAMF-UE-NGAP-ID** (id 100 — previously the AMF id went out under the *wrong* IE id 10, because `AddProtocolIeIfUsable` matches by union-member type; `sendNgapUeAssociated` now suppresses the generic id-10 insert when an id-100 IE is present, a guarded no-op for every other message), **UESecurityCapabilities** (from the Xn-transferred security info in the activated context), and **PDUSessionResourceToBeSwitchedDLList** with a per-session APER-encoded `PathSwitchRequestTransfer` (target's DL NG-U tunnel from the GTP session tree + accepted QoS flows) so the 5GC re-points the UPF at this gNB. Transfers are encoded before any IE is allocated so failures abort cleanly; no sessions in GTP aborts the send with an error. Verified by a standalone APER round-trip test: 5 IEs in canonical order (85/100/121/119/76), encode+decode clean. *Residual:* the session list is read via the unsynchronized `GtpTask::getPduSessions()` accessor (issue 19 pattern). **[Ack handling added 2026-07-17]**: `receivePathSwitchRequestAcknowledge()` now processes the Ack's content per TS 38.413 §9.2.3.22 — the mandatory **SecurityContext** {NCC, NH} rides on the `PATH_SWITCH_REQUEST_ACK` message to RRC, which stores it in the UE context (`nextHopChainingCount`/`nextHopParameter`, the values the next sourced handover carries; store-only, no SecurityModeCommand); each **PDUSessionResourceSwitchedList** item's `PathSwitchRequestAcknowledgeTransfer` is decoded and a re-allocated UL NG-U endpoint is applied to GTP via a new `SESSION_UL_TUNNEL_UPDATE` message (`GtpTask::handleUlTunnelUpdate()` re-points `session->upTunnel`, so uplink follows the switched path); **PDUSessionResourceReleasedListPSAck** sessions are released in GTP and removed from the NGAP context. All three paths verified by an APER round-trip test building the Ack as an AMF would and running the same extraction logic. **[RRC/RLS bearer teardown added 2026-07-17]**: RRC is now the authoritative owner of the `psi → {qfi → drbId}` mapping (new `RrcUeContext::bearerMap`, populated in `createRadioBearerConfig` alongside the existing RLS/UE bearer programming). NGAP no longer silently drops radio state on release: both the path-switch-ack release (`receivePathSwitchRequestAcknowledge`) and the normal `PDUSessionResourceReleaseCommand` (`receiveSessionResourceReleaseCommand`) now forward the released PSIs to RRC over the existing `NmGnbNgapToRrc::PDU_SESSION_UPDATE` message (new `releasedPsis` field). RRC's new `handleNgapPduSessionRelease()` looks up the DRB(s)/QFIs for each PSI, tells RLS to delete them (`RADIO_BEARER_UPDATE` with `deleteBearers`/`deleteSdapMappings` — the previously-unexercised delete half of that message and its `ctl_task.cpp` handler), tells the UE to release the DRBs (`RRCReconfiguration` with `drb-ToReleaseList`, already handled UE-side in `reconfig.cpp`), and prunes `bearerMap`. Builds clean; not yet exercised live (the Xn path was blocked at the time by the XnAP ASN.1 ABI break, since resolved by the Release-18 regeneration; the NGAP-release path needs a full core+UE scenario). *Caveat:* `createRadioBearerConfig` restarts its `drbs_used` DRB-id counter at 1 each call, so a session added in a later update can collide with an existing DRB id — `bearerMap` mirrors whatever id was actually programmed, but the underlying counter bug is untouched (separate fix).


31. **First live Xn handover-preparation run (2026-07-29).** With the XnAP ASN.1 ABI break resolved (the Release-18 regeneration, commit `03562ee0`), the handover path was driven end-to-end for the first time: two gNBs over Xn, a `FakeUe` on the source, a PDU session established, and an A3 MeasurementReport naming the peer. This exercised code that had never executed and found the bugs below. **State at end of session: HandoverRequest encodes, transmits and is decoded correctly by the target; the target admits the session and emits HandoverRequestAcknowledge; the source cannot yet decode that Ack (see 31e).**

    Test driver: `xn_handover_live.py` (scratch, not committed). Two config gotchas it encodes, both non-obvious: `handover.basicHandoverMeasIdentities` is the enable-list for classic events and is **empty** in the shipped `config/gnb1.yaml`, so RRC silently sends no MeasConfig at all until an eventId is listed there; and `nr-gnb` must be reachable at `xn.xnIp`, with only the *lower* gnbId initiating outbound (the collision-avoidance rule in `updateXnConnections()`) — the higher one waits for the inbound association.

    31a. ~~**`GtpTask::getPduSessions()` returned pointers to a destroyed local vector.**~~ **[FIXED]** It filled a function-local `std::vector<PduSessionResource>` by value and pushed `&element` into the out-param, so every caller read freed stack memory. Live symptom: `pduSessionId=-2039361879`, `qfi=-145208288`, `fiveQI=3158928232052543145` — garbage that then failed PER range checks and aborted the encode. Now returns copies (`std::vector<PduSessionResource>`), which also closes the use-after-free half of issue 19: no caller holds pointers into GTP's session tree any more. Both callers updated (`XnTask::GetUeContexts`, `NgapTask::sendPathSwitchRequest`).

    31b. ~~**`UEContextInfoHORequest::ueSecurityCapabilities` had no fallback.**~~ **[FIXED]** The four algorithm BIT STRINGs were only populated when `rrcUe->ueSecurityInfoValid` — false in any run without a real AS security procedure — leaving them default-constructed (`buf=nullptr, size=0`), which the APER encoder rejects. This was the *first* cause of "APER encoding failed", before 31a. Now falls back to all-zero, mirroring what the `securityInformation` block immediately below already did.

    31c. ~~**Source-to-target container was never unwrapped at the target.**~~ **[FIXED]** `makeSourceToTargetTransparentContainerSimulated()` prepends a 16-byte wrapper (magic `"S2TC"`, version, flags, reserved, two lengths), but `handleHandoverRequest()` passed the *whole* container to `DecodeCustomRrcContext()`, which reads a ueId from offset 0 — so the target parsed the ASCII magic as the UE identity (`UE[5994946700739870720]` = `0x5332544301000000`). No unwrap function existed at all; the target-side decode had never run, on the N2 path either. Added `UnwrapSourceToTargetContainer()` next to the encoder it mirrors. The target now resolves the source's exact ueId.

    31d. ~~**Double free / use-after-free on the RRC container in `sendHandoverRequestAck`.**~~ **[FIXED]** `ieRrcContainer->value.choice.OCTET_STRING = rrcOs` is a shallow struct copy sharing `buf`, and the next line called `ASN_STRUCT_FREE_CONTENTS_ONLY` on `rrcOs` — freeing the buffer the IE now pointed at. The encoder then read freed memory and the PDU teardown freed it again (`free(): double free detected in tcache 2`, localised by gdb backtrace to the `outerPdu` teardown at the end of `sendHandoverRequestAck`; the offending assignment is now at [handover.cpp:1581](src/gnb/xn/handover.cpp#L1581)). `rrcOs` is a stack shell, so the correct action is simply not to free.

    31e. **OPEN — the source cannot decode HandoverRequestAcknowledge.** The target encodes and sends it, but `aper_decode` rejects the PDU at the source (`XnAP APER decoding failed for SCTP message`). Reproduced offline: the captured 66 bytes fail to decode with the *same* generated library, so the bytes themselves are bad rather than the receive path being at fault. A hand-parse of the octets looks self-consistent (4 IEs: 73, 79, 42, 77, correct ids and lengths; PSI=5, gtp IP `7f000003`, TEID `c0000001` all present), so the defect is likely inside the `PDUSessionResourcesAdmitted-List` item encoding — the optional-presence preamble of `PDUSessionResourceAdmittedInfo` is the prime suspect. Sample failing PDU:
    `2000003e00000400494005c0a788b874004f4005c0a788b874002a4012000005100014000101f07f000003c0000001004d400f0e000c0050204240000911e0000d00`

    31f. **Fixed in passing:** `receiveHandoverRequest` logged `nr_CI` (a `BIT_STRING_t` struct) through a `%lx` varargs slot, printing a pointer and corrupting every following argument — the source of nonsense like `NR Cell ID=0x77af70003c10 from gnbId=5`. Now goes through `asn::GetBitStringLong<36>`. Also, `xnap_encode::Encode` now reports `failed_type` and an XER dump of the offending structure on failure; without it "APER encoding failed" carried no information about which IE was at fault.

32. **`NG-RANnodeUEXnAPID` is 32-bit; the simulator's 64-bit ueId does not fit.** The IE is `INTEGER (0..4294967295)`; the note near the top of this document claiming "the IE tolerates long values" is wrong, and was never caught because the layer had never run. Verified against the generated encoder: `4294967295` encodes, `4294967296` and any real IMSI-derived ueId fail outright. **Interim fix (approved 2026-07-29):** `XnTask::xnApIdFromUeId()` truncates to the low 32 bits, and is deliberately the *single* place the mapping is decided so a proper per-peer allocator can replace it without touching call sites; its docstring states the contract a replacement must satisfy. Because truncation is not invertible, `findSourcePendingByXnApId()` was added as the matching reverse seam: the source recovers the full ueId from the pending entry that recorded what it sent, and every message forwarded to RRC/GTP now carries `->second.ueId` rather than the 32-bit wire value. **Still to do:** truncation is not collision-free (two ueIds differing only above bit 32 collide), so this is simulator-grade, not standard-correct.  Must add a facility that pulls unique 32-bit XNAPIDs on each side of teh transaction and maps those to pending handovers.  The facility must be able to track unused IDs and expose an API to allow users to return their IDs after session completion (obligation of users to avoid ID exhaustion).

33. **MUST FIX — RRC silently drops every `HANDOVER_ABORT`, so no Xn failure or timeout ever reaches RRC.** (Found 2026-07-29 during the doc audit.) `NmGnbXnToRrc::HANDOVER_ABORT` is produced by *six* paths: all four supervision timers in `processHandoverTimeouts()`, and every `sendRRCAbort()` bail-out in `sendHandoverRequest()` (no Xn peer for the target NCI, peer not `CONNECTED`, APER encode failure, no free SCTP stream, a PDU session with no QoS flows). The RRC switch on `NmGnbXnToRrc` in [rrc/task.cpp:201](src/gnb/rrc/task.cpp#L201) handles 7 of the enum's 8 values and has **no `default:` case**, so `HANDOVER_ABORT` falls straight through and is discarded without so much as a log line. Consequence: on any of those failures the source UE keeps `handoverDecisionPending`/`handoverInProgress` set forever — the exact state-resolution problem the abort message was introduced to fix. Note the association-loss path is *not* affected: `handlePeerHandoverLoss()` sends the older `HANDOVER_PREPARATION_FAILURE_RECEIVED`/`HANDOVER_CANCEL_RECEIVED`, both of which RRC does handle, which is why the live test still showed `handleHandoverPreparationFailure` on peer loss.

34. **Dead field: `NmGnbXnToRrc::abortReason`.** [nts.hpp:434-440](src/gnb/nts.hpp#L434) declares an `ABORT_REASON` enum and an `abortReason` member, but nothing anywhere in `src/` ever writes or reads it — `sendRRCAbort()` and all four timeout sites put the reason in the generic `int reason` field instead. Either drop the member or switch the senders to it (the typed field is the better carrier, and would make issue 33's handler easier to write correctly).

### Incomplete / in-progress functionality

16. **No XnAP ErrorIndication.** Malformed or unmatchable PDUs are logged and dropped throughout (`receiveHandoverRequest`, `receiveHandoverRequestAck`, `sendHandoverRequestAck` correlation failures, etc. — the "TODO: send XnAP error indication" comments). The peer discovers problems only via its supervision timers.

17. **Overall-timeout cleanup is partial.** On overall-timer expiry the source sends HandoverCancel, but the in-code TODOs remain: GTP forwarding tunnels for the UE are not torn down (DL traffic keeps relaying to a dead handover until the End Marker or session release), and RRC is not told the execution failed (it already processed the ACK, so its handover state resolution is unclear).

18. **Xn Setup content assumptions** (all marked in code): ServedCells-NR advertises fixed dummy RF parameters; `nrPCI` carries the NCI (breaks the 0..1007 PER constraint for large NCIs — same family as RRC_summary issue 4); UEHistoryInformation is a 1-byte dummy. Multi-cell gNBs, multiple slices per TAI, and IPv6 transport addresses are unsupported.

19. **Cross-thread `getUeContext()` reads are only partly synchronized.** `GetUeContexts()` ([task.cpp:317](src/gnb/xn/task.cpp#L317)) calls into RRC (shared-mutex protected since 2026-07-15), but the NGAP and GTP `getUeContext()`/`getPduSessions()` calls on the same line are still unsynchronized reads of those tasks' state from the Xn thread — carried over from RRC_summary issue 3. `NgapTask::sendPathSwitchRequest()` and `RlsControlTask::copyUeContext()` are the same pattern (the latter is at least mutex-protected). *Narrowed 2026-07-29:* the use-after-free half is gone — `getPduSessions()` now returns copies rather than pointers into GTP's session tree (issue 31a), so a session released mid-preparation is no longer a dangling dereference; what remains is an unsynchronized *read* that can observe a torn or stale snapshot.

20. **`m_completedSourceReleases` grows without bound** and its guard is only consulted when the pending entry is already gone ([handover.cpp:2700](src/gnb/xn/handover.cpp#L2700)); the code's own TODO questions its purpose. A time-bounded ledger (or reusing RRC's idempotent release handling) would be cleaner.

21. **Peer reconnection is coarse.** *Partially addressed 2026-07-16*: `SctpTask` now pushes a `CONNECTION_FAILED` notification to the requesting task when bind/connect fails (NGAP logs it; no AMF retry policy yet), and `XnTask::handleConnectionFailure()` marks the peer `CONNECTION_FAILED` so `updateXnConnections()` retries it on the 10 s neighbor tick (issuing a `CONNECTION_CLOSE` first, so a retry after XnSetupFailure — which also sets `CONNECTION_FAILED` but leaves a live association — cannot leak the old `ClientEntry`). Verified live: connect-refused → failure notification → retry every 10 s. Remaining: a peer stuck in `CONNECTION_REQUESTED` (connect succeeded but the Xn Setup exchange never completes) is never retried — `updateXnConnections()` only re-dials entries in `CONNECTION_FAILED` — and "update connection parameters if needed" is still a TODO. *Corrected 2026-07-29:* this issue previously claimed there was no Xn Setup collision handling. There is: only the lower-gnbId side dials out, and an inbound XnSetupRequest from a gNB already `CONNECTED` on another clientId is rejected and its association closed. Simultaneous-initiation double associations are no longer possible.

### Cleanups





