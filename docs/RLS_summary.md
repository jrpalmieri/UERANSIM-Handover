# RLS (Radio Link Simulation) Layer — Summary

RLS is UERANSIM's own simulation layer that stands in for the entire Uu air interface (PHY/MAC/RLC) between a UE process and a gNB process. Since UERANSIM runs UE and gNB as separate OS processes (often on separate hosts), RLS carries RRC PDUs and user-plane (SDAP/PDCP) PDUs over a UDP socket on port 4997 (cons::RadioLinkPort, constants.cpp), and additionally simulates radio-link discovery, signal strength (RSRP), and link-failure detection.
Layers above RLS (RRC, NAS, GTP), communicate with RLS to receive incoming messages or send outgoing messages.  RLS also exposes some control operations that upper layers can use to query/change RLS state.


## Relevant Code Locations

| Location | Role |
|---|---|
| [src/lib/rls/rls_pdu.hpp](src/lib/rls/rls_pdu.hpp) / [.cpp](src/lib/rls/rls_pdu.cpp) | Wire protocol: message types and encode/decode, shared by UE and gNB |
| [src/lib/rls/rls_base.hpp](src/lib/rls/rls_base.hpp) / [.cpp](src/lib/rls/rls_base.cpp) | Shared `PduInfo` (per-PDU ACK-tracking record) and `ERlfCause` (radio-link-failure reasons) |
| [src/ue/rls/task.hpp](src/ue/rls/task.hpp)/[.cpp](src/ue/rls/task.cpp) | UE RLS task — the entry point/coordinator, wires the two sub-tasks below to NAS/RRC |
| [src/ue/rls/udp_task.hpp](src/ue/rls/udp_task.hpp)/[.cpp](src/ue/rls/udp_task.cpp) | UE side: UDP socket I/O, heartbeats, cell discovery/tracking |
| [src/ue/rls/ctl_task.hpp](src/ue/rls/ctl_task.hpp)/[.cpp](src/ue/rls/ctl_task.cpp) | UE side: radio bearers, SDAP mapping, PDU-ID assignment, ACK bookkeeping |
| [src/gnb/rls/task.hpp](src/gnb/rls/task.hpp)/[.cpp](src/gnb/rls/task.cpp) | gNB RLS task — coordinator, wires sub-tasks to RRC/GTP |
| [src/gnb/rls/udp_task.hpp](src/gnb/rls/udp_task.hpp)/[.cpp](src/gnb/rls/udp_task.cpp) | gNB side: UDP socket I/O, per-UE heartbeat tracking, RSRP simulation |
| [src/gnb/rls/ctl_task.hpp](src/gnb/rls/ctl_task.hpp)/[.cpp](src/gnb/rls/ctl_task.cpp) | gNB side: per-UE radio bearers/SDAP/ACK state (`RlsUeContext`) |
| [src/gnb/rls/pos_sim.hpp](src/gnb/rls/pos_sim.hpp)/[.cpp](src/gnb/rls/pos_sim.cpp) | RSRP/path-loss math (terrestrial and NTN/satellite models) used by the gNB udp task |

Both sides use the same architecture to implement the RLS layer: three separate NTS tasks:
•	A **main task** (task.cpp) that acts as a message router for the other RLS tasks.  Upper layers send messages to this task’s message queue, and the main task routes to the other tasks according to message type.
•	A **udp task** (udp_task.cpp) that handles socket I/O and link discovery simulation.  This task does not implement the NTS message queue.  Instead, it creates a separate UDP socket task and performs immediate reads/writes using that task. 
•	A **control task** (ctl_task.cpp) that implements a message handling state machine.  It is responsible for responding to upper layer control messages, sending upper later data messages to the udp task for transmission and determining handling of incoming (downlink) messages received from the udp task.


## The wire protocol (`rls_pdu.hpp`/`.cpp`)

Every RLS message starts with a fixed header, then type-specific fields:

```
byte 0      : 0x03                        (legacy/compat marker, must be 3)
byte 1-3    : Major.Minor.Patch            (from utils::constants, currently 3.4.7)
byte 4      : EMessageType                (Message type enum, see below)
byte 5-12   : sti (uint64, 8 bytes)        — "Simulation Temp Identifier" (see below)
... type-specific payload ...
```

`EncodeRlsMessage`/`DecodeRlsMessage` ([rls_pdu.cpp:52-174](src/lib/rls/rls_pdu.cpp#L52-L174))
are symmetric and used identically by both UE and gNB. `DecodeRlsMessage`
rejects anything that doesn't match the compat byte and the current
Major/Minor/Patch, so a version mismatch between UE and gNB binaries causes
messages to be silently dropped (logged as "Unable to decode RLS message").

### Message types (`EMessageType`)

| Type | Direction | Purpose |
|---|---|---|
| `HEARTBEAT` | UE → gNB | "I'm here, at this position" — periodic, drives discovery and RSRP simulation |
| `HEARTBEAT_ACK` | gNB → UE | "I hear you at N dBm" — reply to every heartbeat |
| `PDU_TRANSMISSION` | either direction | Carries one RRC or Data PDU |
| `PDU_TRANSMISSION_ACK` | either direction | Batched ACK for one or more previously-received `PDU_TRANSMISSION`s |
| `DEPRECATED1/2/3`, `RESERVED` | — | Reserved values kept for wire compatibility with older RLS versions; unused |


### STI identifier scheme

Both sides construct a 64-bit "Simulation Temp Identifier" (STI) to identify themselves, using the same logic: a 36+ bit system-unique identifying value in the high bits, plus a random value in the low 10 bits.  This replaces all standards-based radio layer identifications (e.g., RNTIs) and allows for simple transmission of system-unique IDs in the simulated environment (where there are no security concerns and the universe of UEs and gNBs is known).

-	UE: high bits = UE's IMSI (from SUPI); low 10 bits = random (ue/rls/task.cpp). The gNB recovers the UE ID via sti >> 10 (gnd/rls/udp_task.cpp).
-	gNB: high bits = the gNB's NCI (NR cell identity); low 10 bits = random (gnb/rls/task.cpp). The UE recovers the NCI the same way (ue/rls/udp_task.hpp).

The random low bits exist so that if a UE or gNB restarts/re-simulates, stale messages from a prior run (or a prior cell instance with the same ID) can be distinguished from the current run — the UE's RlsCellMap and the gNB's m_stiToUe/m_ueMap key off the full STI, not just the ID, and messages with an unrecognized STI are dropped rather than misattributed (e.g. ue/rls/udp_task.cpp:252-259).

The UE and gNB set their STIs on power up, and retain them for the duration of their existence, with some exceptions:
-	The UE/gNB can be instructed to reset its STI through the command interface;
-	A UE can reset its STI on a message from RRC to RESET_STI, which is sent by RRC when the RRC connection is locally released (ue/rrc/sap.cpp:67).

### Message fields for `PDU_TRANSMISSION` message type

- `pduType` (1-byte uint): enum indicating `RRC` or `DATA`.
- `radioBearer` and `ackPdu` (1-byte packed bit field): a byte where:
  - bit 6 distinguishes signaling (SRB) vs data (DRB) bearers,
  - bits 5-0 are the bearer ID (e.g. `0x00` = SRB0, `0x41` = DRB1), and
  - bit 7 (`ackPdu`) indicates whether a PDU_TRANSMISSION_ACK should be sent for this message
    (simulates RLC AM (acknowledged mode) vs UM (unacknowledged mode)).
- `pduId` (4-byte uint): a per-bearer sequence number (see Radio Bearers below).
- `sdapByte` (1-byte bit field): the SDAP header stand-in — unused for RRC, carries the QFI for
  DATA PDUs.
- `payloadType` (4-byte uint): overloaded field — for RRC PDUs this is the `rrc::RrcChannel`
  enum value (the logical RRC channel, e.g. CCCH/DCCH), for DATA PDUs this
  is the PDU Session ID (PSI).
- `payloadLength` (4-byte uint): length of the carried PDU payload.
- `pdu` (variable-length encoded raw bytes): the raw payload bytes (max 16 KiB, enforced on decode).

### Message fields for PDU_TRANSMISSION_ACK message types
- `count` (4-byte uint): the number of ACKs being carried by this message (multiple PDU ACKs can be batched into the same transmission).
- An array of length `count`, containing individual ACKs for received PDU_TRANSMISSION message.  Each ACK element contains:
  - `radioBearer` (1-byte packed bit field): same contents as the radioBearer from the PDU_TRANSMISSION message (identified which bearer was used to send the PDU) 
  - `pduId` (4-byte uint): the per-bearer sequence number of the PDU_TRANSMISSION being ACKed.

### Message fields for HEARTBEAT and HEARTBEAT_ACK message types

HEARTBEAT (only sent from UE to gNB) carries the UE's simulated geographic position (GeoPosition{latitude, longitude, altitude} as three double floats (8 bytes each). 

HEARTBEAT_ACK (only set from gNB to UE) carries the gNB’s simulated RSRP experienced by the UE (HEARTBEAT_ACK.dbm , int, 4 bytes).  Depending on its operating mode, the gNB generates the RSRP value based on the UE’s reported location (from prior HEARTBEATs) and its own location to compute a simulated RSRP using propagation formulas.

## UE-side flow

UeRlsTask (ue/rls/task.cpp) is constructed once per UE on startup.  It creates a shared RlsSharedContext for data that is shared among the RLS tasks (e.g., the STI), which implements concurrency management logic.  It then creates the udp task and the control task, initializes them and starts them.

**RlsUdpTask** — owns the UDP socket and the radio layer simulated link status. On startup it is passed the following configuration parameters:
- `searchSpace` – a list of IP addresses that are gNBs that should receive HEARTBEAT messages
- `base` – pointer to the base configuration structure common to all UE tasks
- `shCtx` – pointer to shared RLS context object created by main task.

`base` is used to obtain the timing for the HEARTBEAT cycle:
- `m_loopCounter`: a timer value (in ms) for starting the HEARTBEAT cycle
- `m_receiveTimeout`: a time threshold (in ms) for determining when a HEARTBEAT_ACK has not been received. 

The UDP task has three primary operations – (1) run a receive loop for incoming UDP datagrams, (2) execute the heartbeat cycle when the loopCounter is tripped, and (3) expose a send() function to allow sending of outgoing UDP datagrams.  Operations (1) and (2) are performed inside the NTS onLoop() method, while the send() function can be called directly (exposed public) by other tasks (e.g., the RLS control task).

On every onLoop() iteration, it:

1.	Checks if the heartbeat timer (m_loopCounter) has been tripped and if so triggers a heartbeat cycle. 

2.	Waits on a call to UdpServer::Receive() (using the m_receiveTimeout value to terminate).  If any datagram is returned, it processes the message (receiveRlsPdu()) by decoding the wire format and dispatching to a handler.
The send() function takes as parameters the target cell’s NCI and the msg to send, encodes the message into the RLS wire format and sends to the target cell.  The target cell IP is determined by looking up the peer address for that NCI in the known cells map maintained by the udp task; if the NCI isn't known, it just logs a warning and drops the message.

#### The HEARTBEAT cycle:

The udp task first checks for missing ACKs to prior HEARTBEAT messages.  It iterates the known cells map to check whether any cell’s stored “last seen” timestamp is older than the threshold m_loopCounter + m_receiveTimeout.  If so, the cell is removed from the known cells map, and the RRC layer is informed that the cell is no longer available (RRC SIGNAL_CHANGED).
The udp task then sends new HEARTBEAT messages with UE’s current simulated position (sourced from TaskBase::UeLocation) to every gNB IP in the searchSpace list.

#### The Message Processing logic:
1.	First check if this is a HEARTBEAK_ACK message.  If so, we determine the NCI of the sending cell from the STI value, and update the known cells map with the NCI value and the current time as the last seen timestamp.  We also store the reported RSRP value in the global cell measurements structure so it is available to other UE tasks.  If this is a new cell (not already in the known cells map) or the reported RSRP has fallen below the threshold for a working radio link, we send a SIGNAL_CHANGED message to the RRC layer.
2.	Next is a sanity check on the message STI value.  If the message contains an STI value that is not in the known cells map, it is logged and dropped.  
3.	PDU_TRANSMISSIONS and PDU_TRANSMISSION_ACKs are forwarded to the control task using the RECEIVE_RLS_MESSAGE message with the NCI resolved from the STI.
4.	Anything else is invalid, and simply dropped.

**RlsControlTask** — owns bearer state and ACK bookkeeping. On startup it is passed the following configuration parameters:
- `base` – pointer to the base configuration structure common to all UE tasks
- `shCtx` – pointer to shared RLS context object created by main task.

`base` is used to obtain the timing for the PDU_TRANSMISSION_ACK cycle:
- `m_timerPeriodAckControl`: a timer value (in ms) for triggering ACK received checking
- `m_timerPeriodAckSend`: a time threshold (in ms) for triggering ACK sends for received PDUs. 

The control task has three primary operations – (1) run a dispatch loop as part of its onLoop() method for handling messages on its NTS message queue, (2) execute the PDU ACK cycles when the ACK timers are tripped, and (3) managing the radio bearer simulation operations.

Radio bearer tracking information is populated from the RRC layer (from RADIO_BEARER_UPDATE messages).  The information tracked is:
- `radioBearers` (vector): the available radio bearers and their associated uplink/downlink sequence numbers (appended to messages to ensure in-order delivery);
- `m_sdapMapping` (vector): the mappings of a session ID and QoS flow ID to a radio bearer.

By default, only SRB0 (the signaling bearer) is created; DRBs are added on demand when RRC sets up a PDU session (via `RADIO_BEARER_UPDATE`). *(Changed 2026-07-17: the gNB side previously also pre-seeded a default `DRB1 (0x41)`; that was removed so RRC is the sole DRB-id authority — see the gNB-side note below.)*  

On every onLoop() iteration, it:
1.	Check its NTS message queue for a message (if not, it exits)
2.	Decodes the message and routes to a handling function.
Handled messages are either from other RLS tasks, or an NTS TIMER_EXPIRED.

From main task:

- `UPLINK_RRC`: sent from main task based on an RRC_PDU_DELIVERY message from the RRC layer.  This represents an RRC message being sent to the target gNB.  The handler handleUplinkRrcDelivery()) will: pick SRB0 as the radio bearer and assign the next ulSn value; if an ACK is required, it will register the PDU in m_pduMap for ACK-tracking, and call the udp task send() function to send a PDU_TRANSMISSION to the target cell. Note that currently the requireRrcAck() function that determines whether to require an ACK is hardwired for FALSE, so no ACKs are required for uplink RRC messages.
- `UPLINK_DATA`: sent from the main task based on a DATA_PDU_DELIVERY message from NAS layer.  This represents a user plane message to be sent to the target gNB.  The handler (handleUplinkDataDelivery()) will: look up the SDAP mapping for the PDU session to get the assigned radio bearer and QFI; assign the next ulSn for that bearer; if an ACK is required, it will register the PDU in m_pduMap for ACK-tracking; and call the udp task send() function to send a PDU_TRANSMISSION to the target cell.  Note that currently the requireDataAck() function that determines whether to require an ACK is hardwired for FALSE, so no ACKs are required for uplink data messages.
- `ASSIGN_CURRENT_CELL`: sent from the main task based on an ASSIGN_CURRENT_CELL message from the RRC layer.  Simply sets the control task’s m_servingCell to the provided NCI.
- `RADIO_BEARER_UPDATE`: sent from the main task based on a message from the RRC layer.  This represents an indication from the RRC layer that the gNB has sent radio bearer information. The handler (handleRadioBearerUpdate()) will: update the radio bearer map according to the provided rbUpdate list; and update the SDAP mappings according to the provided sdapUpdate list.

From the udp task:
- `RECEIVE_RLS_MESSAGE`: sent from the udp task based on receiving a PDU_TRANSMISSION.  This represents a downlink message from the gNB.  The handler (handleRlsMessage()) will decode the message type.  If the message is a PDU ACK, it will update the m_pduAck structure to remove the pending ACK record.  If the message is a PDU, it will: check the message’s ackPdu bit (in the SDAP byte), and if set, add an ACK to the m_pendingAck queue so an ACK will be sent on the next ACK send time expiration; check the message’s pduType byte for a DATA message, and if so, sends a DOWNLINK_DATA message to the main task for routing to the NAS layer; check the message’s pduType byte for an RRC message, and if so, sends a DOWNLINK_RRC message to the main task fro routing to the RRC layer.
- `SIGNAL_CHANGED`: sent from the udp task when a cell fails to ACK a HEARTBEAT within the time threshold or a cell’s reported RSRP falls below the threshold for an active radio link. The handler (handleSignalChange()) sends SIGNAL_CHANGED message to the main task for forwarding to the RRC layer.

TIMER_EXPIRED:

Checks which of the timers (ACK control timer, ACK send timer) has tripped and calls the handler.

- ACK control timer: Handled by onAckControlTimerExpired).  Scans m_pduMap for entries older than MAX_PDU_TTL (3000 ms) and reports them as TRANSMISSION_FAILURE message to the main task.

- ACK send timer: Handled by onAckSendTimerExpired().  Flushes m_pendingAck as one batched PDU_TRANSMISSION_ACK message, which is sent to the serving cell using the udp task send() function.

## gNB-side flow

GnbRlsTask (gnb/rls/task.cpp) is constructed once per gNB on startup (unlike the UE side, a single instance serves every UE the cell is in contact with). It generates its own STI directly from the gNB's NCI (generateSti(nci)) — there is no shared-context struct analogous to the UE's RlsSharedContext, since the gNB's STI is fixed at construction and never reset. It then creates the udp task and the control task, initializes them, and starts them.

**RlsUdpTask** — owns the UDP socket and per-UE last-seen/address tracking. On startup it is passed:
- `base` – pointer to the base configuration structure common to all gNB tasks
- `sti` – the gNB's own STI (generated by the main task, passed by value, not shared)

`base` is used the same way as on the UE side, to obtain the HEARTBEAT/heartbeat-timeout timing (`m_loopCounter`, `m_receiveTimeout`), and additionally supplies `rfLink`/`ntn` configuration used for RSRP simulation.

The gNB udp task keeps two maps, both protected by a single `std::mutex` (`m_ueMutex`), rather than the single-cell-map-per-UE structure on the UE side, since it must track many UEs at once:
- `m_stiToUe` (STI → ueId), used to recognize a returning UE and to detect brand-new UEs;
- `m_ueMap` (ueId → UeInfo), storing each UE's current address, last-seen time, and last-reported position.

Like the UE side, the udp task has the same three primary operations — (1) a receive loop for incoming datagrams, (2) a periodic cycle gated by `m_loopCounter`, and (3) a `send()` function exposed to the control task — but the periodic cycle's role is different from the UE side's: the gNB never initiates HEARTBEATs itself (it is the UE that pings the gNB), so the gNB's periodic cycle is purely a **pruning pass** — it does not send anything. On every onLoop() iteration, it:

1. Checks if the prune timer (`m_loopCounter`) has tripped and if so runs `heartbeatCycle()`: iterates `m_ueMap` for any UE whose `lastSeen` is older than `m_loopCounter + m_receiveTimeout`, removes it from both `m_ueMap` and `m_stiToUe`, and pushes a `SIGNAL_LOST` message to the control task for each one removed.
2. Waits on `UdpServer::Receive()` (bounded by `m_receiveTimeout`). Any datagram is decoded and dispatched via `receiveRlsPdu()`.

The `send(ueId, msg)` function has a broadcast case not present on the UE side: **`ueId == 0` sends the message to every currently-known UE's address** (used for cell-wide messages such as MIB/SIB1). For a specific `ueId`, it looks up that UE's last-known address in `m_ueMap`; if the UE isn't known, the message is silently dropped (no warning is logged here, unlike the UE side's udp `send()`).

#### The Message Processing logic (`receiveRlsPdu()`):

1. First check if this is a `HEARTBEAT`. If so, the sending UE's id is derived from the STI (`sti >> 10`). The UE's entry in `m_ueMap`/`m_stiToUe` is upserted (address, last-seen time, reported position). If this is the **first** heartbeat ever seen for this UE (i.e. it wasn't already in `m_stiToUe`), a `SIGNAL_DETECTED` message is pushed to the control task — this is what causes the gNB to begin sending MIB/SIB1 to a newly-discovered UE. In all cases, a `HEARTBEAT_ACK` is sent back immediately (not gated by the prune-cycle timer) containing a simulated RSRP computed by `computeDbm()`. The UE's reported position is also recorded in the node-wide position store (`m_base->setUePosition()`).
2. If not a `HEARTBEAT`, and the message's STI is not already known in `m_stiToUe` (i.e. no heartbeat has ever been received from this sender), the message is dropped — the gNB will not process PDU traffic from a UE it has never heard a heartbeat from.
3. Otherwise, the message (`PDU_TRANSMISSION` or `PDU_TRANSMISSION_ACK`) is forwarded to the control task as a `RECEIVE_RLS_MESSAGE`, tagged with the resolved `ueId`.

`computeDbm()` picks between two path-loss models depending on configuration: for a `Fixed` RSRP mode it just returns the configured fixed value; otherwise it calls either `SatelliteSimulatedDbm()` (NTN mode, using the gNB's ECEF satellite position) or `TerrestrialSimulatedDbm()` (using the gNB's geographic position), both implemented in `pos_sim.cpp`. The result is clamped to `cons::MIN_RSRP` if the UE would be below the horizon.

**RlsControlTask** — owns per-UE bearer state and ACK bookkeeping, in a `map<int64_t, RlsUeContext>` (`m_ueCtx`) keyed by `ueId`. Because RRC and Xn code on other task threads may need to read this state (see below), access to `m_ueCtx` is guarded by a `std::shared_mutex` (`m_ueCtxMutex`): the RLS task thread itself takes a `unique_lock` for every read or mutation performed inside its own handlers, while the one external accessor (`copyUeContext()`) takes a `shared_lock`. On startup it is passed:
- `base` – pointer to the base configuration structure common to all gNB tasks
- `sti` – the gNB's own STI (used to stamp every RLS message the gNB sends)

`base` supplies the same ACK-cycle timing fields as the UE side (`m_timerPeriodAckControl`, `m_timerPeriodAckSend`).

Each UE's `RlsUeContext` is created lazily, on first `SIGNAL_DETECTED`, with **only SRB0** pre-populated; DRBs are added on demand via `RADIO_BEARER_UPDATE` when RRC establishes a PDU session (this now matches the UE side, which also pre-creates only SRB0). *(Before 2026-07-17 the gNB side additionally pre-seeded `DRB1 (0x41)`; that crutch was removed so RRC's `allocateDrbId` is the sole DRB-id authority.)* A UE's context is **not** deleted when its signal is lost — the comment in the code notes this is deliberate, so that bearer/ACK state can survive a reconnect or a handover; actual deletion is expected to be driven from elsewhere (e.g. RRC/NGAP UE Context Release), not from this layer.

On every onLoop() iteration, messages are handled the same way as the UE side — from the main task (based on upper-layer RRC/GTP messages), from the udp task, or `TIMER_EXPIRED`:

From the main task:

- `DOWNLINK_RRC`: sent from the main task based on an `RRC_PDU_DELIVERY` message from the RRC layer. The handler (`handleDownlinkRrcDelivery()`) behaves differently depending on `ueId`: for a specific UE (`ueId != 0`), it looks up that UE's context, finds SRB0, assigns the next `dlSn`, and (if an ACK were required — currently always false via `requireRrcAck()`) registers the PDU in that UE's `m_pduMap`. For the **broadcast** case (`ueId == 0`, used for MIB/SIB1), this per-UE bookkeeping is skipped entirely — `pduId` stays 0 and `radioBearer` stays SRB0 — and the message is simply handed to the udp task's `send(0, msg)`, which broadcasts it to every currently-known UE.
- `DOWNLINK_DATA`: sent from the main task based on a `DATA_PDU_DELIVERY` message from the GTP layer. The handler (`handleDownlinkDataDelivery()`) looks up the UE's context, resolves the radio bearer and next `dlSn` via `getBearerFromSdap()` (matching the UE's PSI+QFI against that UE's SDAP mappings, falling back to DRB1 with a warning if no mapping is found — a legacy crutch that only resolves if a DRB1 actually exists; since the DRB1 pre-seed was removed (2026-07-17) an unmapped flow now warns and drops rather than riding a phantom bearer, which is expected only in the "logic error" case the code already flags), registers the PDU for ACK-tracking if required (currently always false via `requireDataAck()`), and sends a `PDU_TRANSMISSION` to that UE via the udp task.
- `RADIO_BEARER_UPDATE`: sent from the main task based on a message from the RRC layer, carrying a target `ueId`. The handler (`handleRadioBearerUpdate()`) updates that specific UE's `radioBearers`/`sdapMappings` (delete/upsert), the same logic as the UE side but scoped to one UE's context instead of the whole task.

From the udp task:

- `SIGNAL_DETECTED`: sent when the udp task sees a UE's first-ever heartbeat. The handler (`handleSignalDetected()`) creates the UE's `RlsUeContext` if one doesn't already exist, then forwards `SIGNAL_DETECTED` to the main task, which relays it to RRC (triggering the initial MIB/SIB1 send to that UE).
- `SIGNAL_LOST`: sent when the udp task's prune cycle has not seen a heartbeat from a UE within the timeout. The handler (`handleSignalLost()`) does **not** touch the UE's `RlsUeContext` (per the note above) — it simply forwards `SIGNAL_LOST` to the main task, which (unlike the UE side's equivalent `SIGNAL_CHANGED` path) currently only logs it rather than relaying it further to RRC — see Further development areas.
- `RECEIVE_RLS_MESSAGE`: sent when the udp task receives a `PDU_TRANSMISSION` or `PDU_TRANSMISSION_ACK` from a known UE. The handler (`handleRlsMessage()`) looks up that UE's context and: for an ACK, removes the acknowledged entries from `m_pduMap`; for a PDU, queues a pending ACK if the message's `ackPdu` bit is set, then forwards a `DATA` PDU to the main task as `UPLINK_DATA` (destined for GTP) or an `RRC` PDU as `UPLINK_RRC` (destined for RRC) — both tagged with the UE's `cRnti` from its context (see Further development areas — this is currently always 0 and is a vestigial field, not something to be relied on).

TIMER_EXPIRED:

Handled the same way as the UE side, but each timer handler now iterates every entry in `m_ueCtx` (taking the `unique_lock` for the duration) rather than acting on one bearer/ACK state:

- ACK control timer: `onAckControlTimerExpired()` scans every UE's `m_pduMap` for entries older than `MAX_PDU_TTL` (3000 ms) and reports them as a `TRANSMISSION_FAILURE` (per UE) to the main task — which currently only logs it, the same as `SIGNAL_LOST` above.
- ACK send timer: `onAckSendTimerExpired()` flushes each UE's `m_pendingAck` (if non-empty) as one batched `PDU_TRANSMISSION_ACK`, sent to that UE via the udp task's `send()`.

Finally, `copyUeContext(ueId)` is a public method (not reachable via the NTS message queue — it's called directly, under the `shared_lock` described above) that returns a thread-safe snapshot of one UE's radio bearers, `cRnti`, and pending-ACK list (the PDU map itself is deliberately omitted from the snapshot, since `OctetString` isn't cheaply copyable). Per its own header comment it exists for RRC/Xn to read a UE's bearer state — for example during handover — but as of this review nothing in RRC or Xn actually calls it yet; see Further development areas.

## Sequence summary (typical lifecycle)

1. UE starts sending `HEARTBEAT`s to every gNB in its search space, every
   ~200 ms.
2. First heartbeat reaching a given gNB → gNB creates an `RlsUeContext`,
   tells RRC (`SIGNAL_DETECTED`) → RRC starts "broadcasting" MIB/SIB1.
   (In simulator, these are unicast to each UE, but sent periodically).
3. gNB ACKs every heartbeat with a simulated dBm; UE's RRC uses these
   `SIGNAL_CHANGED` events (new cell, or low signal) to build its cell list
   and to decide on cell (re)selection / trigger RLF.
4. Once UE picks a cell, RRC sends `RRCSetupRequest` etc. as ordinary
   `PDU_TRANSMISSION` (RRC, SRB0) messages; the gNB assigns bearer/PDU-ID and
   passes payload up to its own RRC.
5. Data-plane PDUs (NAS PDU sessions / GTP) flow the same way over DRBs once
   SDAP mappings are configured by RRC (`RADIO_BEARER_UPDATE`).
6. If configured to require ACKs, receivers queue and periodically flush
   `PDU_TRANSMISSION_ACK`s; senders time out un-ACKed PDUs and report
   transmission failures upward.
7. Loss of heartbeats past the threshold (either direction) surfaces as
   `SIGNAL_LOST` (gNB) or a synthetic "signal below minimum" `SIGNAL_CHANGED`
   (UE), which is how radio link failure is simulated without any real RF.

---

## Further development areas

### PDU ACK logic

Currently the functions for determining whether to require ACKs for PDUs (requireDataAck(), requireRrcAck() ) are functionally disabled (always return False).  A mechanism for enabling these functions needs to be developed.  Options: a configuration parameter to cause all data and/or RRC PDUs to be ACKed; a way to enable ACKs for messages with certain QFIs; a way to enable ACKs during certain operating environments (e.g. low RSRP).

### cRNTI usage

The Radio Network Temporary Identifiers (RNTIs) are not needed in the simulator to identify UEs, since the current implementation uses the STI value to embed the UE’s system-unique identifier.  However, the C-RNTI is a limiter on the number UEs that a gNB can serve at one time – the C-RNTI is a 16-bit value that by standard is limited to 1-65529.  Tracking C-RNTI assignments in the gNB is useful to enforce this limitation, even if the C-RNTI is not communicated to the UE.

Prior development used the cRNTI in the RLS wire protocol to provide identification information.  These code paths can be removed.

### Failure modes

The failure mode operations are only minimally specified.  Further development would add hardening and reporting for error conditions.  For example, the gNB currently does nothing when RLS produces a SIGNAL_LOST, TRANSMISSION_FAILURE or RADIO_LINK_FAILURE message for a UE.  There is also no mechanism for deleting a UE RLS context, which should occur at a minimum after a certain period of inactivity after a failure event.

### m_cellDesc bug

 m_cellDesc uses int (32-bit) keys, but NCIs are int64_t

   - src/ue/rrc/task.hpp:53: std::unordered_map<int, UeCellDesc> m_cellDesc{}
   - nciFromSti() returns int64_t (static_cast<int64_t>(sti >> 10))
   - NmUeRlsToRls::cellId (line 128 in nts.hpp) is also int, so the NCI is already truncated to 32 bits before it reaches the RRC task and gets stored as a m_cellDesc key
   - cellDbMeas.upsertMeasurement() takes int64_t, so it stores the full 64-bit NCI
   - lookForSuitableCell calls cellDbMeas.getMeasurement(item.first) where item.first is the truncated int, sign-extended to int64_t at the call site — this produces a different 64-bit value than the one stored, so the lookup returns MIN_RSRP - 1 and the cell silently fails the signal check

   Consequence: Any STI whose upper 54 bits encode an NCI that doesn't fit in int32_t causes cell selection to always fail — even when the measurement is present and healthy.

   Current workaround in the test harness: fake_gnb.py sets self._gnb_sti = (nci << 10) | random.getrandbits(10), which forces nciFromSti(sti) = nci to be a small value (e.g., 1) that is identical whether stored as int or int64_t.

   The real fix would be to change m_cellDesc's key type to int64_t and update NmUeRlsToRls::cellId similarly — but that's a broader C++ refactor touching the RRC task, the message types, and all call sites.


