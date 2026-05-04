# Satellite Simulation Tool

This tool is used to control satellite simulations using UERANSIM-Handover and Open5GS.  It provides a launcher to configure and execute multiple UE and gNB processes, inject commands to control their operations, and log the results.  It also provides a visualization UI to show satellite motion during simulation runs.

## Features

- starts UE and gNB entities as separate executables
- polls status information from UE and GNB through command interface (e.g., `nr-cli`)
- tails stdout/stderr of each spawned process
- writes UE and gNB process logs to files (overwritten on each run)
- monitors AMF reachability with configurable `sctp` or `tcp` connect checks
- optionally tails AMF logs from `amf.log_file` into the AMF pane

## Prerequisites

- Build UERANSIM first so `nr-ue`, `nr-gnb`, and `nr-cli` exist.
- See requirements.txt for python dependencies

- Running instances of Open5GS network functions (config files should be edited to point to the correct IP addresses)

## Run


```bash
python3 tools/UI/dashboard.py --config tools/UI/config.example.json
```

Menu bar:

- `File` -> `Exit` performs a graceful shutdown of spawned processes.
- `Handover` -> `Send gNB Position/Velocity` sends ECEF position/velocity to selected gNB via `set-loc-pv`.
- `Handover` -> `Program` opens a dialog to run periodic signal injection.
- `User Plane` -> `Open Demo` opens a split window for user-plane payload demo.
- `User Plane` -> `Send Text Message` opens a dialog to send demo text traffic.
- `User Plane` -> `Send Repeated Message` opens a dialog for message + cycle period (ms).
- `User Plane` -> `Stop Repeated Message` stops active repeated sending loop.

User Plane demo window:

- Top pane shows printable ASCII extracted from payload bytes seen as inbound traffic on UE #1 TUN.
- Bottom pane shows the source generator send output from dashboard actions.
- Sender action transmits plain UDP payload to UE #1 TUN IP on `user_plane.upf_port`.
- If raw-socket capture is denied, dashboard falls back to `tcpdump` capture automatically.
- Optional: move UE #1 TUN into a Linux network namespace after creation.
- Dashboard verifies host routing to UE #1 TUN IP via `172.22.0.1` (configurable).
- If route is missing, dashboard attempts to add `UE_TUN_IP/32 via gateway` automatically.

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
- For multi-UE runs, UE node names are derived from `ue.node` and `ue.count`.
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
- `user_plane.upf_ip` is retained for compatibility and not used as sender destination.
- `user_plane.upf_port` configures the destination UDP port for demo sends.
- `user_plane.capture_enabled` enables or disables UE #1 TUN packet capture for the top pane.
- `user_plane.capture_use_sudo` enables `sudo` for tcpdump fallback capture.
- `user_plane.capture_sudo_non_interactive` uses `sudo -n` for tcpdump fallback when true.
- `user_plane.move_tun_to_netns` moves UE #1 TUN interface into a namespace after setup.
- `user_plane.netns_name` sets namespace name used for moved UE TUN interface.
- `user_plane.netns_prefix_len` sets prefix length applied in namespace (e.g., 16 for /16).
- `user_plane.ensure_host_route_to_ue` enables route check/add for UE #1 TUN IP.
- `user_plane.host_route_gateway` sets the gateway used for UE route injection.
- `user_plane.host_route_subnet` sets the expected host subnet route (for warning visibility).
- `user_plane.max_log_lines` controls retained lines for each user-plane demo pane.
- **Packet Capture (pcap)** has two independent sections: `core` (AMF interface) and `ran` (loopback RLS)
  - **Core section** (`pcap.core`):
    - `enabled`: enables core (AMF) packet capture
    - `interface`: interface to capture on; blank auto-discovers from AMF host route ("amf" keyword also works)
    - `output_file`: output pcap filename
    - `use_sudo`: whether to use sudo for tcpdump
    - `sudo_non_interactive`: whether to use non-interactive sudo (`-n` flag)
  - **RAN section** (`pcap.ran`):
    - `enabled`: enables RAN (loopback RLS) packet capture
    - `interface`: interface to capture on (typically "lo"); RLS packets identified via `udp port 4997 and net 127.0.0.0/24`
    - `output_file`: output pcap filename
    - `use_sudo`: whether to use sudo for tcpdump
    - `sudo_non_interactive`: whether to use non-interactive sudo (`-n` flag)
  - Core and RAN captures can run simultaneously with independent output files and sudo settings.
