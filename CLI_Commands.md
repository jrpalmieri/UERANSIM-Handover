
# UERANSIM CLI Commands (UE and gNB)

This document describes how the `nr-cli` process works, how command handlers are implemented
inside each UE/gNB node, and the full set of runtime CLI commands available.

---

## How nr-cli Works

### Binary and invocation

`nr-cli` is the command-line client (`src/cli.cpp`). It runs as a separate process from the
simulator nodes and communicates with them via UDP.

```
Usage:
  nr-cli <node-name> [option...]
  nr-cli --dump
  nr-cli --mass-dereg <cause> [--nodes <file>]

Options:
  -d, --dump               List all UE and gNBs currently running in the environment
  -e, --exec <command>     Execute one command without entering an interactive shell
      --mass-dereg <cause> Deregister all (or a listed set of) UEs in parallel
      --nodes <file>       File with one UE node name per line (used with --mass-dereg)
```

### Node discovery

Each running UE/gNB process writes a **ProcTable** entry — a small file in
`cons::PROC_TABLE_DIR` — containing its PID, version, node names, and the UDP port its CLI
server is listening on.  When `nr-cli` starts it:

1. Reads the ProcTable directory and collects all entries.
2. Cross-checks each entry's PID against the live process list; stale entries (dead PIDs) are
   removed automatically.
3. Locates the entry whose node-name list contains the requested node name.
4. Verifies the version triple (`major.minor.patch`) matches the CLI binary; if not, it warns
   and skips the node.
5. Extracts the UDP port from the matching entry and opens a `CliServer` socket.

### Interactive vs. one-shot mode

| Mode | How to invoke | Behaviour |
|---|---|---|
| Interactive shell | `nr-cli <node>` | Reads commands from stdin in a loop; prints results until EOF or Ctrl-D |
| One-shot (`-e`) | `nr-cli <node> -e "cmd args"` | Sends one command, prints the result, then exits |
| Multi-node deregister | `nr-cli --mass-dereg <cause>` | Discovers all running UEs (or reads `--nodes` file), fires `deregister <cause>` on each in parallel threads, prints per-node OK/FAIL |

In interactive mode, typing `commands` prints the list of available subcommands for that node.

### UDP transport

`nr-cli` wraps the command text in a `CliMessage::Command` envelope and sends it to
`cons::CMD_SERVER_IP:<port>`.  The node returns one of three message types:

| Type | Meaning |
|---|---|
| `RESULT` | Command succeeded; value is YAML/text output |
| `ERROR` | Command or execution failed; value is the error string |
| `ECHO` | Informational echo printed to stdout without exiting |

---

## How Command Handlers Work Inside Each Node

### Per-node CLI server

Every UE and gNB process starts an `app::CliServer` UDP listener on startup.  When a
`CliMessage::Command` arrives the server dispatches it to the node's **App task** as either a
`NmUeCliCommand` or `NmGnbCliCommand` message.

### Command parsing

Before execution, `ParseUeCliCommand()` / `ParseGnbCliCommand()` (`src/lib/app/cli_cmd.cpp`)
tokenizes the raw command string and validates it against the node's command entry table
(`g_ueCmdEntries` / `g_gnbCmdEntries`):

1. Empty input → error `"Empty command"`.
2. Subcommand `commands` → dump the command table and return (no handler called).
3. Unknown subcommand → error `"Command not recognized: <name>"`.
4. Known subcommand → run the subcommand-specific parser to build a typed command object
   (e.g. `GnbCliCommand::NEIGHBORS`); any argument errors are returned as CLI errors before
   the handler is ever called.

### Task-pause execution model

Both `UeCmdHandler::handleCmd()` and `GnbCmdHandler::handleCmd()` follow the same pattern:

```
handleCmd(msg):
  1. Log the command and source.
  2. Pause all sibling tasks:
       UE  — NAS task, RRC task, RLS task
       gNB — App task, RRC task, NGAP task, RLS task, SCTP task, GTP task
  3. Poll every 10 ms for up to 3000 ms until all tasks acknowledge pause.
  4a. If timeout:  sendError("... pausing timeout")
  4b. If paused:   handleCmdImpl(msg)  ← executes the command, sends result
  5. Unpause all tasks.
```

`handleCmdImpl()` is a large `switch` on `cmd->present` that reads node state and serialises a
JSON/YAML response, or triggers a procedure and sends a plain-text acknowledgement.

### Implementation files

| File | Role |
|---|---|
| `src/cli.cpp` | `nr-cli` binary: options, node discovery, UDP send/receive |
| `src/lib/app/cli_base.hpp/.cpp` | `CliServer` UDP socket; `CliMessage` types |
| `src/lib/app/cli_cmd.hpp/.cpp` | Command entry tables, parsers, typed command structs |
| `src/ue/app/cmd_handler.hpp/.cpp` | UE command execution |
| `src/gnb/app/cmd_handler.hpp/.cpp` | gNB command execution |

---

## Common Parser Behaviour (UE and gNB)

- `commands` — prints a formatted table of available subcommands and their descriptions.
- Unknown subcommand → `Command not recognized: <name>`
- Empty input → `Empty command`
- Argument validation errors are returned as CLI errors (prefixed by the subcommand name
  where noted below).

---

## gNB Commands

### config-info
- Syntax: `config-info`
- Args/options: none
- Output: YAML object with the full gNB configuration.

### status
- Syntax: `status`
- Args/options: none
- Output: YAML object with gNB runtime status.

### ui-status
- Syntax: `ui-status`
- Args/options: none
- Output: YAML object with compact fields for UI polling:
  - `nci` — NR Cell Identity
  - `pci` — Physical Cell ID
  - `rrc-ue-count` — number of UEs in RRC context table
  - `ngap-ue-count` — number of UEs in NGAP context table
  - `ngap-up` — whether the NGAP link to the AMF is up

### amf-list
- Syntax: `amf-list`
- Args/options: none
- Output: YAML array of AMF context entries (each with `id`).

### amf-info
- Syntax: `amf-info <amf-id>`
- Args/options:
  - positional `amf-id` (positive integer)
- Output:
  - Success: YAML object with AMF details.
  - Error: `AMF not found with given ID`

### ue-list
- Syntax: `ue-list`
- Args/options: none
- Output: YAML array of UE context entries associated with this gNB.

### ue-count
- Syntax: `ue-count`
- Args/options: none
- Output: Plain integer — total UE context count.

### ue-release
- Syntax: `ue-release <ue-id>`
- Args/options:
  - positional `ue-id` (positive integer)
- Output:
  - Success: `Requesting UE context release`
  - Error: `UE not found with given ID`

### set-loc-wgs84
- Syntax: `set-loc-wgs84 <lat:lon:alt>`
- Args/options:
  - one positional string in colon-separated `lat:lon:alt` format
  - `lat` ∈ [−90, 90], `lon` ∈ [−180, 180], `alt` in metres
- Output:
  - Success: `Updated true gNB WGS84 position`
  - Error: `Invalid format. Expected lat:lon:alt with valid WGS84 bounds`

### set-loc-pv
- Syntax: `set-loc-pv <x:y:z:vx:vy:vz:epoch-ms>`
- Args/options:
  - one positional string in colon-separated ECEF position/velocity format
  - `x`, `y`, `z` in metres; `vx`, `vy`, `vz` in m/s; `epoch-ms` Unix timestamp in ms
- Output:
  - Success: `Updated true gNB position/velocity for SIB19 generation`
  - Error: `Invalid format. Expected x:y:z:vx:vy:vz:epoch-ms`

### get-loc-wgs84
- Syntax: `get-loc-wgs84`
- Args/options: none
- Output:
  - Success: YAML object with `latitude`, `longitude`, `altitude`
  - Error: `gNB location is not set`

### get-loc-pv
- Syntax: `get-loc-pv`
- Args/options: none
- Output:
  - Success: YAML object with `x`, `y`, `z`, `vx`, `vy`, `vz`, `epochMs`
  - Error: `gNB location is not set`

### sat-loc-pv
- Syntax: `sat-loc-pv <json-payload>`
- Args/options:
  - one positional JSON payload string with satellite position/velocity fields
- Output:
  - Success: YAML object — `result: ok`, `pci`, `updatedAtMs`
  - Error: `sat-loc-pv payload error: <reason>`

### sat-tle
- Syntax: `sat-tle <json-payload>`
- Args/options:
  - one positional JSON payload string with a `satellites` array of TLE records
- Output:
  - Success: YAML object — `result: ok`, `upsertedCount`
  - Error: `sat-tle payload error: <reason>`

### sat-time
- Syntax:
  - `sat-time` — query current clock status
  - `sat-time pause` — pause the satellite clock
  - `sat-time run` — resume the satellite clock
  - `sat-time tickscale=<v>` — set simulation time scaling factor
  - `sat-time start-epoch=<YYDDD.DDD>` — set the TLE-epoch start time
  - `sat-time pause-at-wallclock=<unix-ms>` — schedule a pause at a wall-clock instant
  - `sat-time run-at-wallclock=<unix-ms>` — schedule a resume at a wall-clock instant
- Args/options: at most one positional action argument
- Output:
  - Success: YAML object — `sat-time-ms`, `wallclock-ms`, `start-epoch-ms`, `tick-scaling`,
    `paused`, and optionally `pause-at-wallclock-ms`, `run-at-wallclock-ms`
  - Errors: `sat-time is not available`, `sat-time command failed: <reason>`, or parse error

### neighbors
- Syntax: `neighbors <json-payload>`
- Args/options:
  - one positional JSON payload
  - required fields: `mode` (`"replace"` | `"add"` | `"remove"`), `neighbors` (array)
- Output:
  - Success: YAML object — `result: ok`, `mode`, `beforeCount`, `afterCount`, `addedCount`,
    `removedCount`, `warnings`, `neighbors`
  - Errors: `neighbors payload error: <reason>`, `neighbors update failed: <reason>`

### set-rsrp
- Syntax: `set-rsrp <rsrp>`
- Args/options:
  - one positional integer value in dBm (e.g. `-90`)
  - valid range: −140 to −40 dBm
- Output:
  - Success: `Updated fixed RSRP to <value> dBm`
  - Error: `RSRP value out of range (must be between -140 and -40 dBm)`

### version
- Syntax: `version`
- Args/options: none
- Output: YAML object — `gnb-version`, `base-version`

---

## UE Commands

### config-info
- Syntax: `config-info`
- Args/options: none
- Output: YAML object with UE identity/config — `supi`, `hplmn`, `imei`, `imeisv`,
  `ecall-only`, `uac-aic`, `uac-acc`, `is-high-priority`.

### status
- Syntax: `status`
- Args/options: none
- Output: YAML object with mobility/registration/session status:
  - `cm-state`, `rm-state`, `mm-state`, `5u-state`
  - `sim-inserted`, `selected-plmn`, `current-cell`, `current-plmn`, `current-tac`
  - `last-tai`, `stored-suci`, `stored-guti`, `has-emergency`

### ui-status
- Syntax: `ui-status`
- Args/options: none
- Output: YAML object for compact UI polling:
  - `rrc-state`, `nas-state`, `connected-pci`, `connected-dbm`
  - Per-cell signal measurements keyed by PCI

### set-loc-wgs84
- Syntax: `set-loc-wgs84 <lat:lon:alt>`
- Args/options:
  - one positional string in colon-separated `lat:lon:alt` format
  - `lat` ∈ [−90, 90], `lon` ∈ [−180, 180], `alt` in metres
- Output:
  - Success: `Updated true UE WGS84 position`
  - Error: `Invalid format. Expected lat:lon:alt with valid WGS84 bounds`

### get-loc-wgs84
- Syntax: `get-loc-wgs84`
- Args/options: none
- Output:
  - Success: YAML object — `latitude`, `longitude`, `altitude`
  - Error: `UE location is not set`

### timers
- Syntax: `timers`
- Args/options: none
- Output: YAML object dump of all current UE NAS timer states.

### rls-state
- Syntax: `rls-state`
- Args/options: none
- Output: YAML object — `sti`, `gnb-search-space`

### gnb-ip-add
- Syntax: `gnb-ip-add <json-payload>`
- Args/options:
  - one positional JSON payload: `{"ipAddresses": ["<ip>", ...]}`
- Output:
  - Success: YAML object — `result: ok`, `requestedCount`, `addedCount`, `gnb-search-space`
  - Error: `gnb-ip-add payload error: <reason>`

### gnb-ip-remove
- Syntax: `gnb-ip-remove <json-payload>`
- Args/options:
  - one positional JSON payload: `{"ipAddresses": ["<ip>", ...]}`
- Output:
  - Success: YAML object — `result: ok`, `requestedCount`, `removedCount`, `gnb-search-space`
  - Error: `gnb-ip-remove payload error: <reason>`

### gnb-ip-list
- Syntax: `gnb-ip-list`
- Args/options: none
- Output: YAML object — `gnb-search-space`

### coverage
- Syntax: `coverage`
- Args/options: none
- Output:
  - Cells present: YAML object keyed by `[pci]`, each entry includes signal strength and
    decoded MIB/SIB1 fields.
  - No cells: `No cell available`

### ps-establish
- Syntax: `ps-establish <session-type> [options]`
- Supported session type: `IPv4` (case-insensitive variants accepted)
- Options:
  - `--sst <value>` — SST value (1..255)
  - `--sd <value>` — SD value (1..16777215); requires `--sst`
  - `-n, --dnn <apn>` — DNN/APN name
  - `-e, --emergency` — request as an emergency session (mutually exclusive with `--sst`,
    `--sd`, `--dnn`)
- Output:
  - Success: `PDU session establishment procedure triggered`
  - Validation/parse errors printed by the argument parser.

### ps-list
- Syntax: `ps-list`
- Args/options: none
- Output: YAML object listing active PDU sessions (`PDU Session<id>` keys), each with:
  `state`, `session-type`, `apn`, `s-nssai`, `emergency`, `address`, `ambr`, `data-pending`

### ps-release
- Syntax: `ps-release <pdu-session-id>...`
- Args/options:
  - one or more PDU session IDs (1..15); max 15 per command
- Output:
  - Success: `PDU session release procedure(s) triggered`
  - Validation errors on out-of-range or non-integer IDs.

### ps-release-all
- Syntax: `ps-release-all`
- Args/options: none
- Output: `PDU session release procedure(s) triggered`

### deregister
- Syntax: `deregister <normal|disable-5g|switch-off|remove-sim>`
- Args/options: exactly one de-registration cause
- Output:
  - `normal`, `disable-5g`, `remove-sim`: `De-registration procedure triggered`
  - `switch-off`: `De-registration procedure triggered. UE device will be switched off.`

### sat-time
- Syntax:
  - `sat-time` — query current clock status
  - `sat-time pause` — pause the satellite clock
  - `sat-time run` — resume the satellite clock
  - `sat-time tickscale=<v>` — set simulation time scaling factor
  - `sat-time start-epoch=<YYDDD.DDD>` — set the TLE-epoch start time
  - `sat-time pause-at-wallclock=<unix-ms>` — schedule a pause at a wall-clock instant
  - `sat-time run-at-wallclock=<unix-ms>` — schedule a resume at a wall-clock instant
- Args/options: at most one positional action argument
- Output:
  - Success: YAML object — `sat-time-ms`, `wallclock-ms`, `start-epoch-ms`, `tick-scaling`,
    `paused`, and optionally `pause-at-wallclock-ms`, `run-at-wallclock-ms`
  - Errors: `sat-time is not available`, `sat-time command failed: <reason>`, or parse error

### version
- Syntax: `version`
- Args/options: none
- Output: YAML object — `ue-version`, `base-version`

---

## Quick Examples

```bash
# List all running nodes
nr-cli --dump

# gNB status
nr-cli UERANSIM-gnb-286-1-1 -e "status"

# gNB compact UI status
nr-cli UERANSIM-gnb-286-1-1 -e "ui-status"

# gNB neighbor replace
nr-cli UERANSIM-gnb-286-1-1 -e \
  'neighbors {"mode":"replace","neighbors":[
    {"nci":2,"idLength":32,"tac":1,"ipAddress":"127.0.0.2","handoverInterface":"N2"}
  ]}'

# gNB set fixed RSRP
nr-cli UERANSIM-gnb-286-1-1 -e "set-rsrp -95"

# gNB satellite clock status
nr-cli UERANSIM-gnb-286-1-1 -e "sat-time"

# gNB satellite clock run at future wall-clock time
nr-cli UERANSIM-gnb-286-1-1 -e "sat-time run-at-wallclock=1746230400000"

# UE compact status
nr-cli UERANSIM-ue-1 -e "ui-status"

# UE full status
nr-cli UERANSIM-ue-1 -e "status"

# UE PDU session establish
nr-cli UERANSIM-ue-1 -e "ps-establish IPv4 --sst 1 --sd 1 --dnn internet"

# UE PDU session list
nr-cli UERANSIM-ue-1 -e "ps-list"

# UE deregister (normal)
nr-cli UERANSIM-ue-1 -e "deregister normal"

# Mass deregister all UEs (switch-off)
nr-cli --mass-dereg switch-off

# Mass deregister a specific set of UEs
nr-cli --mass-dereg normal --nodes ue_list.txt

# UE set location
nr-cli UERANSIM-ue-1 -e "set-loc-wgs84 37.42:122.08:100"

# UE satellite clock status
nr-cli UERANSIM-ue-1 -e "sat-time"
```
