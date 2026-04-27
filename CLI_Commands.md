
# UERANSIM CLI Commands (UE and gNB)

This document lists the runtime CLI commands supported by UERANSIM UE and gNB nodes, including arguments, options, and expected output.

The command parser and handlers are implemented in:
- `src/lib/app/cli_cmd.cpp`
- `src/ue/app/cmd_handler.cpp`
- `src/gnb/app/cmd_handler.cpp`

## How to run commands

Use `nr-cli` against a running node.

```bash
# List discoverable nodes
nr-cli --dump

# Interactive shell for a node
nr-cli <node-name>

# One-shot command
nr-cli <node-name> -e "<command>"
```

In interactive mode, type `commands` to list subcommands available for that node.

## Notes on output format

- Most commands return YAML (serialized from internal JSON objects/arrays).
- Some commands return plain text (for example, `ue-count` or trigger acknowledgements).
- Validation failures return CLI errors with explicit messages.

---

## gNB Commands

### config-info
- Syntax: `config-info`
- Args/options: none
- Expected output:
  - YAML object with full gNB config.

### status
- Syntax: `status`
- Args/options: none
- Expected output:
  - YAML object with gNB runtime status.

### ui-status
- Syntax: `ui-status`
- Args/options: none
- Expected output:
  - YAML object with compact UI polling fields.

### amf-list
- Syntax: `amf-list`
- Args/options: none
- Expected output:
  - YAML array of AMFs.

### amf-info
- Syntax: `amf-info <amf-id>`
- Args/options:
  - positional `amf-id` (positive integer)
- Expected output:
  - Success: YAML object with AMF details.
  - Error: `AMF not found with given ID`.

### ue-list
- Syntax: `ue-list`
- Args/options: none
- Expected output:
  - YAML array of UE context entries.

### ue-count
- Syntax: `ue-count`
- Args/options: none
- Expected output:
  - Plain integer string with total UE context count.

### ue-release
- Syntax: `ue-release <ue-id>`
- Args/options:
  - positional `ue-id` (positive integer)
- Expected output:
  - Success: `Requesting UE context release`
  - Error: `UE not found with given ID`

### set-loc-wgs84
- Syntax: `set-loc-wgs84 <lat:lon:alt>`
- Args/options:
  - one positional string in exact colon-separated format above
- Expected output:
  - Success: `Updated true gNB WGS84 position`
  - Parse error when malformed:
    - `Invalid format. Expected lat:lon:alt with valid WGS84 bounds`

### set-loc-pv
- Syntax: `set-loc-pv <x:y:z:vx:vy:vz:epoch-ms>`
- Args/options:
  - one positional string in exact colon-separated format above
- Expected output:
  - Success: `Updated true gNB position/velocity for SIB19 generation`
  - Parse error when malformed:
    - `Invalid format. Expected x:y:z:vx:vy:vz:epoch-ms`

### get-loc-wgs84
- Syntax: `get-loc-wgs84`
- Args/options: none
- Expected output:
  - Success: JSON/YAML object with fields: `latitude`, `longitude`, `altitude`
  - Error: `gNB location is not set`

### get-loc-pv
- Syntax: `get-loc-pv`
- Args/options: none
- Expected output:
  - Success: JSON/YAML object with fields: `x`, `y`, `z`, `vx`, `vy`, `vz`, `epochMs`
  - Error: `gNB location is not set`

### sat-loc-pv
- Syntax: `sat-loc-pv <json-payload>`
- Args/options:
  - one positional JSON payload string with satellite position/velocity fields
- Expected output:
  - Success YAML object: `result: ok`, `pci: <int>`, `updatedAtMs: <unix-ms>`
  - Payload errors are returned as: `sat-loc-pv payload error: <reason>`

### sat-tle
- Syntax: `sat-tle <json-payload>`
- Args/options:
  - one positional JSON payload string with `satellites` array
- Expected output:
  - Success YAML object: `result: ok`, `upsertedCount: <int>`
  - Payload errors are returned as: `sat-tle payload error: <reason>`

### sat-time
- Syntax:
  - `sat-time`
  - `sat-time pause`
  - `sat-time run`
  - `sat-time tickscale=<v>`
  - `sat-time start-epoch=<YYDDD.DDD>`
  - `sat-time pause-at-wallclock=<unix-ms>`
  - `sat-time run-at-wallclock=<unix-ms>`
- Args/options:
  - at most one positional action argument
- Expected output:
  - Success YAML object with satellite clock status: `sat-time-ms`, `wallclock-ms`, `start-epoch-ms`, `tick-scaling`, `paused`, optional `pause-at-wallclock-ms`, optional `run-at-wallclock-ms`
  - Errors: `sat-time is not available`, `sat-time command failed: <reason>`, parse error if argument invalid

### neighbors
- Syntax: `neighbors <json-payload>`
- Args/options:
  - one positional JSON payload
  - payload must contain: `mode` (replace, add, remove), `neighbors` (array)
- Expected output:
  - Success YAML object: `result: ok`, `mode`, `beforeCount`, `afterCount`, `addedCount`, `removedCount`, `warnings`, `neighbors`
  - Errors: `neighbors payload error: <reason>`, `neighbors update failed: <reason>`

### set-rsrp
- Syntax: `set-rsrp <rsrp>`
- Args/options:
  - one positional integer value (dBm, e.g. -90)
- Expected output:
  - Success: `Updated fixed RSRP to <value> dBm`
  - Error: `RSRP value out of range (must be between -140 and -40 dBm)`

### version
- Syntax: `version`
- Args/options: none
- Expected output:
  - YAML object: `gnb-version`, `base-version`

---

## UE Commands

### config-info
- Syntax: `config-info`
- Args/options: none
- Expected output:
  - YAML object with UE identity/config summary, including fields like: `supi`, `hplmn`, `imei`, `imeisv`, `ecall-only`, `uac-aic`, `uac-acc`, `is-high-priority`

### status
- Syntax: `status`
- Args/options: none
- Expected output:
  - YAML object with mobility/registration/session status fields: `cm-state`, `rm-state`, `mm-state`, `5u-state`, `sim-inserted`, `selected-plmn`, `current-cell`, `current-plmn`, `current-tac`, `last-tai`, `stored-suci`, `stored-guti`, `has-emergency`

### ui-status
- Syntax: `ui-status`
- Args/options: none
- Expected output:
  - YAML object for compact UI polling: `rrc-state`, `nas-state`, `connected-pci`, `connected-dbm`

### set-loc-wgs84
- Syntax: `set-loc-wgs84 <lat:lon:alt>`
- Args/options:
  - one positional string in exact colon-separated format above
- Expected output:
  - Success: `Updated true UE WGS84 position`
  - Parse error when malformed: `Invalid format. Expected lat:lon:alt with valid WGS84 bounds`

### get-loc-wgs84
- Syntax: `get-loc-wgs84`
- Args/options: none
- Expected output:
  - Success: JSON/YAML object with fields: `latitude`, `longitude`, `altitude`
  - Error: `UE location is not set`

### timers
- Syntax: `timers`
- Args/options: none
- Expected output:
  - YAML object dump of UE NAS timers.

### rls-state
- Syntax: `rls-state`
- Args/options: none
- Expected output:
  - YAML object: `sti`, `gnb-search-space`

### gnb-ip-add
- Syntax: `gnb-ip-add <json-payload>`
- Args/options:
  - one positional JSON payload string with `ipAddresses` array
- Expected output:
  - Success YAML object: `result: ok`, `requestedCount`, `addedCount`, `gnb-search-space`
  - Payload errors: `gnb-ip-add payload error: <reason>`

### gnb-ip-remove
- Syntax: `gnb-ip-remove <json-payload>`
- Args/options:
  - one positional JSON payload string with `ipAddresses` array
- Expected output:
  - Success YAML object: `result: ok`, `requestedCount`, `removedCount`, `gnb-search-space`
  - Payload errors: `gnb-ip-remove payload error: <reason>`

### gnb-ip-list
- Syntax: `gnb-ip-list`
- Args/options: none
- Expected output:
  - YAML object: `gnb-search-space`

### coverage
- Syntax: `coverage`
- Args/options: none
- Expected output:
  - If cells exist: YAML object keyed by `[pci]`, each entry includes signal and decoded MIB/SIB1 fields.
  - If no cells: `No cell available`

### ps-establish
- Syntax: `ps-establish <session-type> [options]`
- Supported session type: `IPv4` only (case variants accepted)
- Options:
  - `--sst <value>`
  - `--sd <value>`
  - `-n, --dnn <apn>`
  - `-e, --emergency`
- Validation rules:
  - `--sd` requires `--sst`.
  - `--sst` range: 1..255.
  - `--sd` range: 1..16777215.
  - With `--emergency`, `--sst`, `--sd`, and `--dnn` are not allowed.
- Expected output:
  - Success: `PDU session establishment procedure triggered`
  - Parse/validation errors on invalid input.

### ps-list
- Syntax: `ps-list`
- Args/options: none
- Expected output:
  - YAML object listing active PDU sessions (`PDU Session<id>` keys), each with: `state`, `session-type`, `apn`, `s-nssai`, `emergency`, `address`, `ambr`, `data-pending`

### ps-release
- Syntax: `ps-release <pdu-session-id>...`
- Args/options:
  - one or more IDs
- Validation rules:
  - max 15 IDs in one command
  - each ID must be integer in range 1..15
- Expected output:
  - Success: `PDU session release procedure(s) triggered`
  - Parse/validation errors on invalid input.

### ps-release-all
- Syntax: `ps-release-all`
- Args/options: none
- Expected output:
  - Success: `PDU session release procedure(s) triggered`

### deregister
- Syntax: `deregister <normal|disable-5g|switch-off|remove-sim>`
- Args/options:
  - exactly one de-registration cause
- Expected output:
  - `normal`, `disable-5g`, `remove-sim`: `De-registration procedure triggered`
  - `switch-off`: `De-registration procedure triggered. UE device will be switched off.`

### sat-time
- Syntax:
  - `sat-time`
  - `sat-time pause`
  - `sat-time run`
  - `sat-time tickscale=<v>`
  - `sat-time start-epoch=<YYDDD.DDD>`
  - `sat-time pause-at-wallclock=<unix-ms>`
  - `sat-time run-at-wallclock=<unix-ms>`
- Args/options:
  - at most one positional action argument
- Expected output:
  - Success YAML object with satellite clock status: `sat-time-ms`, `wallclock-ms`, `start-epoch-ms`, `tick-scaling`, `paused`, optional `pause-at-wallclock-ms`, optional `run-at-wallclock-ms`
  - Errors: `sat-time is not available`, `sat-time command failed: <reason>`, parse error if argument invalid

### version
- Syntax: `version`
- Args/options: none
- Expected output:
  - YAML object: `ue-version`, `base-version`
### rls-state
- Syntax: `rls-state`
- Args/options: none
- Expected output:
  - YAML object:
    - `sti`
    - `gnb-search-space`

### coverage
- Syntax: `coverage`
- Args/options: none
- Expected output:
  - If cells exist:
    - YAML object keyed by `[pci]`, each entry includes signal and decoded MIB/SIB1 fields.
  - If no cells:
    - `No cell available`

### ps-establish
- Syntax: `ps-establish <session-type> [options]`
- Supported session type:
  - `IPv4` only (case variants accepted)
- Options:
  - `--sst <value>`
  - `--sd <value>`
  - `-n, --dnn <apn>`
  - `-e, --emergency`
- Validation rules:
  - `--sd` requires `--sst`.
  - `--sst` range: 1..255.
  - `--sd` range: 1..16777215.
  - With `--emergency`, `--sst`, `--sd`, and `--dnn` are not allowed.
- Expected output:
  - Success: `PDU session establishment procedure triggered`
  - Parse/validation errors on invalid input.

### ps-list
- Syntax: `ps-list`
- Args/options: none
- Expected output:
  - YAML object listing active PDU sessions (`PDU Session<id>` keys), each with:
    - `state`
    - `session-type`
    - `apn`
    - `s-nssai`
    - `emergency`
    - `address`
    - `ambr`
    - `data-pending`

### ps-release
- Syntax: `ps-release <pdu-session-id>...`
- Args/options:
  - one or more IDs
- Validation rules:
  - max 15 IDs in one command
  - each ID must be integer in range 1..15
- Expected output:
  - Success: `PDU session release procedure(s) triggered`
  - Parse/validation errors on invalid input.

### ps-release-all
- Syntax: `ps-release-all`
- Args/options: none
- Expected output:
  - Success: `PDU session release procedure(s) triggered`

### deregister
- Syntax: `deregister <normal|disable-5g|switch-off|remove-sim>`
- Args/options:
  - exactly one de-registration cause
- Expected output:
  - `normal`, `disable-5g`, `remove-sim`:
    - `De-registration procedure triggered`
  - `switch-off`:
    - `De-registration procedure triggered. UE device will be switched off.`

### sat-time
- Syntax:
  - `sat-time`
  - `sat-time pause`
  - `sat-time run`
  - `sat-time tickscale=<v>`
  - `sat-time start-epoch=<YYDDD.DDD>`
  - `sat-time pause-at-wallclock=<unix-ms>`
  - `sat-time run-at-wallclock=<unix-ms>`
- Args/options:
  - at most one positional action argument
- Expected output:
  - Success YAML object with satellite clock status:
    - `sat-time-ms`
    - `wallclock-ms`
    - `start-epoch-ms`
    - `tick-scaling`
    - `paused`
    - optional `pause-at-wallclock-ms`
    - optional `run-at-wallclock-ms`
  - Errors:
    - `sat-time is not available`
    - `sat-time command failed: <reason>`
    - parse error if argument invalid

### version
- Syntax: `version`
- Args/options: none
- Expected output:
  - YAML object:
    - `ue-version`
    - `base-version`

---

## Common parser behavior (UE and gNB)

- `commands`: prints a formatted list of available subcommands and short descriptions.
- Unknown subcommand: `Command not recognized: <name>`
- Empty input: `Empty command`
- If command parser prints help/usage for invalid argument combinations, `nr-cli` shows that output.

## Quick examples

```bash
# gNB status
nr-cli UERANSIM-gnb-286-1-1 -e "status"

# gNB neighbor replace
nr-cli UERANSIM-gnb-286-1-1 -e \
  'neighbors {"mode":"replace","neighbors":[
    {"nci":2,"idLength":32,"tac":1,"ipAddress":"127.0.0.2","handoverInterface":"N2"}
  ]}'

# UE compact status
nr-cli UERANSIM-ue-1 -e "ui-status"

# UE PDU session establish
nr-cli UERANSIM-ue-1 -e "ps-establish IPv4 --sst 1 --sd 1 --dnn internet"

# UE sat-time status
nr-cli UERANSIM-ue-1 -e "sat-time"
```
