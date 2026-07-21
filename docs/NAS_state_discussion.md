# NAS Layer State Machine Discussion

This document describes the state and substate variables tracked by
the NAS (Non-Access Stratum) Mobility Management (MM) layer in
UERANSIM, how they relate to one another, and how two important
procedures — **initial registration** and **handover** — drive
transitions through the state model.

All state enumerations live in `src/ue/types.hpp`; the transition
logic lives under `src/ue/nas/mm/`.

---

## 1  State Variables Overview

The NAS MM layer maintains four independent state variables. Each
captures a different dimension of the UE's relationship with the
5G core network.

| Variable | Enum | Values | Purpose |
|----------|------|--------|---------|
| **RM state** | `ERmState` | `RM_DEREGISTERED`, `RM_REGISTERED` | Registration Management — whether the UE has an active registration context with the AMF. |
| **CM state** | `ECmState` | `CM_IDLE`, `CM_CONNECTED` | Connection Management — whether a NAS signalling connection exists between UE and AMF (mirrors 5GMM-IDLE / 5GMM-CONNECTED in TS 24.501). |
| **MM state / substate** | `EMmState` + `EMmSubState` | See §2 below | The primary Mobility Management state machine as defined in TS 24.501; the substate gives finer granularity. |
| **5GS update status** | `E5UState` | `U1_UPDATED`, `U2_NOT_UPDATED`, `U3_ROAMING_NOT_ALLOWED` | Whether the UE's registration information held  by the network is considered up-to-date. |

### 1.1  Relationship Between RM and MM

RM state is **derived** from MM state inside `switchMmState()`. The
mapping is:

| MM main state | Implied RM state |
|---------------|------------------|
| `MM_DEREGISTERED` | `RM_DEREGISTERED` |
| `MM_REGISTERED_INITIATED` | `RM_DEREGISTERED` |
| `MM_REGISTERED` | `RM_REGISTERED` |
| `MM_SERVICE_REQUEST_INITIATED` | `RM_REGISTERED` |
| `MM_DEREGISTERED_INITIATED` | `RM_REGISTERED` |

Therefore RM state is never set independently — it always follows
from a substate transition.

---

## 2  MM States and Substates

The MM main state (`EMmState`) has six values. Most of them have
one or more **substates** (`EMmSubState`), which provide the detail
that drives procedure selection in the MM cycle.

### 2.1  `MM_NULL`

Substate: `MM_NULL_PS`

The UE is powered off or hasn't started the NAS layer yet. No
procedures run in this state.

### 2.2  `MM_DEREGISTERED` (RM = DEREGISTERED)

The UE does not have an active registration with the network.

| Substate | Meaning |
|----------|---------|
| `MM_DEREGISTERED_PS` | Primary (transient) substate used for initial selection — the MM cycle evaluates the cell and refines to one of the substates below. |
| `MM_DEREGISTERED_NORMAL_SERVICE` | Suitable cell is available. Triggers initial registration. |
| `MM_DEREGISTERED_LIMITED_SERVICE` | Only an acceptable cell is available (emergency only). |
| `MM_DEREGISTERED_ATTEMPTING_REGISTRATION` | Previous registration attempt failed; waiting for retry (T3502/T3511). |
| `MM_DEREGISTERED_PLMN_SEARCH` | No suitable cell; searching for a PLMN. |
| `MM_DEREGISTERED_NO_SUPI` | USIM is invalid or missing. |
| `MM_DEREGISTERED_NO_CELL_AVAILABLE` | No cell at all (no coverage). |
| `MM_DEREGISTERED_ECALL_INACTIVE` | eCall inactivity (special automotive use-case). |
| `MM_DEREGISTERED_INITIAL_REGISTRATION_NEEDED` | Explicit trigger to start initial registration. |

### 2.3  `MM_REGISTERED_INITIATED` (RM = DEREGISTERED)

Substate: `MM_REGISTERED_INITIATED_PS`

A Registration Request has been sent and the UE is waiting for a
Registration Accept/Reject. Timer **T3510** guards this state.

### 2.4  `MM_REGISTERED` (RM = REGISTERED)

Registration was successful. The UE can use network services.

| Substate | Meaning |
|----------|---------|
| `MM_REGISTERED_PS` | Primary (transient) substate — the MM cycle refines to a specific substate below. |
| `MM_REGISTERED_NORMAL_SERVICE` | Registered and camped on a suitable cell in the registered TAI list. This is the steady-state. |
| `MM_REGISTERED_NON_ALLOWED_SERVICE` | Cell is in a non-allowed area. |
| `MM_REGISTERED_ATTEMPTING_REGISTRATION_UPDATE` | A mobility registration (TAU) failed; retrying. |
| `MM_REGISTERED_LIMITED_SERVICE` | Only an acceptable cell is available. |
| `MM_REGISTERED_PLMN_SEARCH` | Searching for a better PLMN or cell. |
| `MM_REGISTERED_NO_CELL_AVAILABLE` | Coverage lost while registered. |
| `MM_REGISTERED_UPDATE_NEEDED` | The UE entered a new TAI not in its registered TAI list — a mobility registration (TAU) is needed. Key for handover. |

### 2.5  `MM_DEREGISTERED_INITIATED` (RM = REGISTERED)

Substate: `MM_DEREGISTERED_INITIATED_PS`

A Deregistration Request has been sent. Timer **T3521** guards
this state.

### 2.6  `MM_SERVICE_REQUEST_INITIATED` (RM = REGISTERED)

Substate: `MM_SERVICE_REQUEST_INITIATED_PS`

A Service Request has been sent while in CM-IDLE to transition
to CM-CONNECTED. Timer **T3517** guards this state.

---

## 3  State Transition Side-Effects

The `switchMmState()`, `switchCmState()`, and `switchUState()`
functions in `src/ue/nas/mm/base.cpp` perform the actual
transitions and invoke callback handlers that apply 3GPP-mandated
side-effects:

### `onSwitchMmState()`

* Deletes the NAS security context when *leaving*
  `MM_DEREGISTERED` (except to `MM_NULL`).
* Clears RAND/RES\* and stops T3516 when *entering*
  `MM_DEREGISTERED` or `MM_NULL`.
* Releases the CM connection if the new substate is a PLMN-search
  or no-cell substate while in `CM_CONNECTED`.
* Stops T3512 when entering `MM_DEREGISTERED`.

### `onSwitchCmState()`

Handles abnormal cases when `CM_CONNECTED → CM_IDLE`:

* If in `MM_REGISTERED_INITIATED`: aborts the in-progress
  registration — the UE falls back to
  `MM_DEREGISTERED_PS` / `U2_NOT_UPDATED` for initial
  registration, or handles the mobility case.
* If in `MM_DEREGISTERED_INITIATED`: aborts deregistration.
* Clears RAND/RES\* and stops T3516.
* Restarts T3512 from its initial value.

### Node / Test Listener Notifications

Every state change is reported via `m_base->nodeListener`, which
emits structured `onSwitch(StateType, ...)` events for RM, MM,
MM\_SUB, CM, and U5 states. This is the mechanism used by the
test harness to observe NAS state transitions from outside the
UE process.

---

## 4  The MM Cycle

The MM cycle (`performMmCycle()` in `src/ue/nas/mm/base.cpp`) is
the central scheduling loop that is triggered after every state
change and timer tick. Its job is to:

1. **Resolve primary ("PS") substates** — if MM is in a transient
   `_PS` substate, evaluate the current cell to pick the correct
   detailed substate (e.g. `NORMAL_SERVICE`, `PLMN_SEARCH`, etc.).
2. **Perform PLMN selection** if in a PLMN\_SEARCH or
   NO\_CELL\_AVAILABLE substate.
3. **Invoke pending procedures** via `invokeProcedures()` —
   this is where registration, mobility registration, service
   request, and deregistration procedures are actually started.
4. **Check for TAI changes** — if the current TAI is not in the
   registered TAI list, trigger mobility registration.

### Procedure Priority

`invokeProcedures()` (`src/ue/nas/mm/proc.cpp`) checks procedure
control flags in strict priority order:

1. **Deregistration**
2. **Initial registration**
3. **Mobility registration (TAU)**
4. **Service request**
5. **SM session establishment** (if in NORMAL\_SERVICE)

Once a procedure returns `OK`, lower-priority procedures are
skipped for that cycle.

---

## 5  Initial Registration Model

Initial registration takes the UE from `RM_DEREGISTERED` to
`RM_REGISTERED`. This is the first procedure a UE runs after
powering on and finding a suitable cell.

### 5.1  Trigger

The MM cycle detects `MM_DEREGISTERED_NORMAL_SERVICE` and calls
`initialRegistrationRequired(MM_DEREG_NORMAL_SERVICE)`, which sets
the `m_procCtl.initialRegistration` flag and re-triggers the MM
cycle.

### 5.2  State Sequence

```
Power-on
  │
  ▼
MM_NULL_PS  (RM_DEREGISTERED, CM_IDLE)
  │  NAS layer starts
  ▼
MM_DEREGISTERED_PS
  │  MM cycle evaluates cell → suitable cell found
  ▼
MM_DEREGISTERED_NORMAL_SERVICE
  │  MM cycle calls invokeProcedures()
  │  → sendInitialRegistration()
  │     • Clears NAS security context
  │     • Builds RegistrationRequest (type=INITIAL)
  │     • Sends NAS message  ──── (RRC establishes connection → CM_CONNECTED)
  ▼
MM_REGISTERED_INITIATED_PS  (RM_DEREGISTERED, CM_CONNECTED)
  │  T3510 started
  │
  │── Registration Accept received
  │     • Stores TAI list, equivalent PLMNs, GUTI
  │     • Resets registration attempt counter
  │     • Sets U1_UPDATED
  │     • Starts T3512 (periodic registration timer)
  ▼
MM_REGISTERED_NORMAL_SERVICE  (RM_REGISTERED, CM_CONNECTED)
```

### 5.3  Failure Paths

| Event | Resulting State |
|-------|-----------------|
| Registration Reject | Depends on cause; typically `MM_DEREGISTERED_*` + `U2_NOT_UPDATED` |
| T3510 expiry (initial reg) | `MM_DEREGISTERED_PS` + `U2_NOT_UPDATED` |
| T3510 expiry (mobility reg) | Release connection, handle as abnormal |
| CM-CONNECTED → CM-IDLE during REGISTERED\_INITIATED | Abort procedure, `MM_DEREGISTERED_PS` + `U2_NOT_UPDATED` |

### 5.4  Key Timers

| Timer | Duration | Purpose |
|-------|----------|---------|
| T3510 | Network-configured | Guards the Registration Request; expires → registration failure |
| T3502 | Network-configured | Retry timer for re-attempting after failure |
| T3511 | Network-configured | Retry timer for re-attempting if still required |
| T3512 | Network-configured | Periodic registration update (started on accept) |
| T3519 | Fixed | SUCI freshness timer |

---

## 6  Handover Model

Handover is initiated by the **RRC layer** upon receiving an
`RRCReconfiguration` message containing `reconfigurationWithSync`.
The NAS layer does **not** drive the handover itself — instead it
*reacts* to the cell change that the RRC layer performed.

### 6.1  RRC-Level Execution (`src/ue/rrc/handover.cpp`)

When the gNB orders a handover:

1. **Suspend measurements** (TS 38.331 §5.5.6.1).
2. **Start T304** — handover supervision timer.
3. **Security key refresh** — KgNB\* derivation placeholder.
4. **Resolve target cell** by PCI among detected cells.
5. **Save previous serving-cell info** (PLMN + TAC for TAI).
6. **MAC reset** indication (logged, not physically modeled).
7. **Switch serving cell** — update `shCtx.currentCell` and tell
   the RLS layer to route UL via the new cell.
8. **RACH** to the target cell (simulated via RLS link switch).
9. **Send RRCReconfigurationComplete** to the target cell.
10. **Clear handover-in-progress flag**, stop T304 supervision.
11. **Resume measurements** on the new serving cell.
12. **Notify NAS** — push `ACTIVE_CELL_CHANGED` with the
    *previous* TAI to the NAS task.

If T304 expires before step 10, the UE declares radio link failure
and initiates RRC re-establishment.

### 6.2  NAS Reaction (`src/ue/nas/mm/radio.cpp`)

`handleActiveCellChange()` is called when the NAS task receives
the `ACTIVE_CELL_CHANGED` notification. The critical point is that
during handover the UE is **still CM_CONNECTED** — the original
UERANSIM code rejected this case, but the handover branch handles
it:

```
handleActiveCellChange(previousTai)
  │
  ├─ CM_CONNECTED path (handover):
  │    │
  │    ├─ TAI unchanged → no state change (seamless HO)
  │    │
  │    └─ TAI changed:
  │         ├─ New TAI in registered list
  │         │    → update lastVisitedRegisteredTai, no procedure
  │         │
  │         └─ New TAI NOT in registered list
  │              → mobilityUpdatingRequired(ENTER_UNLISTED_TRACKING_AREA)
  │              → switch to MM_REGISTERED_UPDATE_NEEDED
  │              → MM cycle invokes sendMobilityRegistration()
  │              → (TAU with the new AMF)
  │
  └─ CM_IDLE path (cell reselection):
       → normal cell-change handling (TAI check,
         registration attempt counter reset, etc.)
```

### 6.3  State Sequence — Seamless Handover (Same TA)

When the target cell belongs to a TAI already in the registered
TAI list, no NAS procedure is needed:

```
MM_REGISTERED_NORMAL_SERVICE  (RM_REGISTERED, CM_CONNECTED)
  │
  │  RRC handover → cell switch → ACTIVE_CELL_CHANGED
  │  TAI unchanged or new TAI in registered TAI list
  │
  ▼
MM_REGISTERED_NORMAL_SERVICE  (RM_REGISTERED, CM_CONNECTED)
  (no state change; lastVisitedRegisteredTai updated)
```

### 6.4  State Sequence — Handover Requiring TAU

When the target cell has a TAI not in the registered list, a
mobility registration update (Tracking Area Update) is triggered:

```
MM_REGISTERED_NORMAL_SERVICE  (RM_REGISTERED, CM_CONNECTED)
  │
  │  RRC handover → cell switch → ACTIVE_CELL_CHANGED
  │  New TAI NOT in registered TAI list
  ▼
MM_REGISTERED_UPDATE_NEEDED  (RM_REGISTERED, CM_CONNECTED)
  │  mobilityUpdatingRequired(ENTER_UNLISTED_TRACKING_AREA)
  │  MM cycle → invokeProcedures()
  │  → sendMobilityRegistration()
  │     • Builds RegistrationRequest
  │       (type=MOBILITY_REGISTRATION_UPDATING)
  │     • Sends NAS message over the new cell
  ▼
MM_REGISTERED_INITIATED_PS  (RM_REGISTERED → RM_DEREGISTERED, CM_CONNECTED)
  │  T3510 started
  │
  │── Registration Accept received
  │     • Updates TAI list (now includes new TAI)
  │     • Updates equivalent PLMNs, service areas
  │     • Resets registration + service-request counters
  │     • Sets U1_UPDATED
  ▼
MM_REGISTERED_NORMAL_SERVICE  (RM_REGISTERED, CM_CONNECTED)
```

> **Note:** During the `MM_REGISTERED_INITIATED_PS` phase the RM
> state briefly becomes `RM_DEREGISTERED` (because
> `MM_REGISTERED_INITIATED` maps to `RM_DEREGISTERED`). This is
> per the 3GPP state machine — the UE's registration is considered
> pending until the network accepts it. Once the accept arrives,
> RM returns to `RM_REGISTERED`.

### 6.5  Handover Failure

If the RRC-level handover fails (T304 expiry, target cell not
found), the RRC layer declares radio link failure. The NAS layer
sees `CM_CONNECTED → CM_IDLE`, which triggers the abnormal-case
handling in `onSwitchCmState()`. The UE then attempts RRC
re-establishment or falls back to cell selection.

---

## 7  Timer Summary

| Timer | Guards | Started When | Expiry Action |
|-------|--------|-------------|---------------|
| T304 | RRC handover | Handover command received | Declare radio link failure |
| T3346 | Access barring | Network-indicated barring | Retry registration |
| T3502 | Registration retry | Registration failure | Re-attempt initial/mobility registration |
| T3510 | Registration request | Registration request sent | Abort registration, `MM_DEREGISTERED_PS` |
| T3511 | Registration retry | Registration failure (alternate) | Re-attempt registration |
| T3512 | Periodic reg update | Registration accept | Trigger mobility registration |
| T3516 | RAND/RES\* storage | Authentication procedure | Clear stored authentication vectors |
| T3517 | Service request | Service request sent | Abort service request |
| T3519 | SUCI freshness | GUTI allocation | Clear stored SUCI |
| T3521 | Deregistration | Deregistration sent | Retransmit (up to 5×), then local dereg |

---

## 8  References

* 3GPP TS 24.501 — Non-Access-Stratum (NAS) protocol for 5GS
* 3GPP TS 38.331 — RRC protocol specification (handover
  procedures, T304)
* Source files:
  - `src/ue/types.hpp` — state enumerations
  - `src/ue/nas/mm/base.cpp` — MM cycle and state-switch logic
  - `src/ue/nas/mm/register.cpp` — registration procedures
  - `src/ue/nas/mm/radio.cpp` — cell-change / handover NAS
    handling
  - `src/ue/nas/mm/proc.cpp` — procedure invocation and priority
  - `src/ue/nas/mm/timer.cpp` — timer expiry handlers
  - `src/ue/rrc/handover.cpp` — RRC handover execution
