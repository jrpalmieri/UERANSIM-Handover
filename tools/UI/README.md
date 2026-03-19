# UERANSIM Runtime UI

This tool launches one UE and two gNB processes, then renders a terminal dashboard with separate
windows for:

- UE
- gNB #1
- gNB #2
- AMF

Each entity window shows:

- scalar values at the top
- live log lines below

## Features

- starts UE and gNB entities as separate executables
- polls compact scalar metrics through `nr-cli ... -e "ui-status"`
- tails stdout/stderr of each spawned process
- writes UE and gNB process logs to files (overwritten on each run)
- monitors AMF reachability with configurable `sctp` or `tcp` connect checks
- optionally tails AMF logs from `amf.log_file` into the AMF pane

## Prerequisites

- Build UERANSIM first so `nr-ue`, `nr-gnb`, and `nr-cli` exist.
- Ensure gNB and UE config files contain the node names referenced by this UI config.

## Run

```bash
python3 tools/UI/dashboard.py --config tools/UI/config.example.json
```

Quit with `q`.

## Run (Windowed)

```bash
python3 tools/UI/dashboard_windowed.py --config tools/UI/config.example.json
```

The windowed dashboard keeps the same runtime behavior and adds a control bar to:

- select target gNB (`gnb1` or `gnb2`)
- enter a manual RSRP value (dBm)
- inject that value into gNB RLS over UDP to simulate movement and trigger handover behavior

Menu bar additions:

- `File` -> `Exit` performs a graceful shutdown of spawned processes.
- `Handover` -> `Program` opens a dialog to run periodic signal injection.

Handover Program inputs:

- Period (seconds)
- Signal A (dBm)
- Signal B (dBm)
- Duration (seconds, `0` or empty means no end)

Use `OK` to start the program and `Cancel` to close the dialog without starting.

## Config Notes

- `amf.host` and `amf.port` are used by the AMF pane reachability checks.
- `amf.protocol` selects socket type for AMF checks (`sctp` default, `tcp` optional).
- `amf.active_probe` controls whether socket connects are sent to AMF.
- For `sctp`, keep `amf.active_probe: false` to avoid creating phantom AMF gNB sessions.
- With passive mode (`active_probe: false`), AMF reachability is derived from gNB `NGAP Up` state.
- `amf.log_file` (optional) enables AMF log tailing in the AMF pane.
- `ui.process_log_dir` controls where UE/gNB logs are written (defaults to `./tools/UI/logs`).
- Files are `ue.log`, `gnb1.log`, and `gnb2.log`, and each is truncated when dashboard starts.
- `ue.node` and each `gnbs[i].node` must match runtime node names used by `nr-cli`.
- `ue.config` and each `gnbs[i].config` are passed to `nr-ue -c ...` and `nr-gnb -c ...`.
- `ue.auto_setcap` (default `true`) attempts `sudo -n setcap cap_net_admin+ep <nr-ue>` once at startup.
- `ue.run_with_sudo` enables launching UE with `sudo`.
- `ue.sudo_non_interactive` (default `false`) uses `sudo -n` when true.
- If the dashboard has no interactive TTY, it automatically forces `sudo -n` for UE launch
	to avoid hanging on password prompts.
- `ue.cli_with_sudo` (default follows `ue.run_with_sudo`) runs `nr-cli` polls for UE via sudo.
- `ue.cli_sudo_non_interactive` (default `true`) controls whether UE `nr-cli` polling uses `sudo -n`.
- In environments where `cap_net_admin` alone still cannot create TUN, set
	`ue.run_with_sudo: true` and `ue.sudo_non_interactive: false`.
- For user-plane TUN setup without running full UE as root, grant capability once:
  `sudo setcap cap_net_admin+ep /path/to/nr-ue`
- Windowed process logs are written as `ue-windowed.log`, `gnb1-windowed.log`, and
	`gnb2-windowed.log` under `ui.process_log_dir`.
