#!/usr/bin/env python3
"""Windowed runtime dashboard for multi-UE status, two gNBs, and AMF with manual RSRP injection."""

from __future__ import annotations

import argparse
import copy
import datetime
import ipaddress
import json
import math
import os
import random
import re
import shlex
import socket
import struct
import subprocess
import threading
import time
import tkinter as tk
import yaml
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import messagebox, scrolledtext, ttk
from typing import Dict, List, Optional, TextIO

try:
    from sgp4.api import Satrec as _Satrec, jday as _sgp4_jday
    _SGP4_AVAILABLE = True
except ImportError:
    # sudo strips PATH so the system python3 may be used instead of the venv.
    # Try adding the local .venv site-packages explicitly before giving up.
    import sys as _sys_sgp4
    _venv_site = str(Path(__file__).resolve().parent.parent.parent
                     / ".venv" / "lib"
                     / f"python{_sys_sgp4.version_info.major}.{_sys_sgp4.version_info.minor}"
                     / "site-packages")
    if _venv_site not in _sys_sgp4.path:
        _sys_sgp4.path.insert(0, _venv_site)
    try:
        from sgp4.api import Satrec as _Satrec, jday as _sgp4_jday
        _SGP4_AVAILABLE = True
    except ImportError:
        print("[TLE] sgp4 library not found — TLE propagation disabled (pip install sgp4)", file=_sys_sgp4.stderr, flush=True)
        _Satrec = None  # type: ignore
        _sgp4_jday = None  # type: ignore
        _SGP4_AVAILABLE = False

RLS_MSG_GNB_RF_DATA = 21
RLS_PORT = 4997
CONS_MAJOR = 3
CONS_MINOR = 3
CONS_PATCH = 7
MIN_RSRP = -156
MAX_RSRP = -44
PRINTABLE_ASCII_MIN = 32
PRINTABLE_ASCII_MAX = 126


def parse_simple_yaml(text: str) -> Dict[str, str]:
    data: Dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        data[key.strip()] = value.strip()
    return data


@dataclass
class EntityPane:
    key: str
    title: str
    scalars: Dict[str, str] = field(default_factory=dict)
    logs: deque[str] = field(default_factory=lambda: deque(maxlen=400))
    lock: threading.Lock = field(default_factory=threading.Lock)

    def append_log(self, line: str) -> None:
        with self.lock:
            self.logs.append(line.rstrip())

    def set_scalars(self, updates: Dict[str, str]) -> None:
        with self.lock:
            self.scalars = updates

    def snapshot(self) -> tuple[Dict[str, str], List[str]]:
        with self.lock:
            return dict(self.scalars), list(self.logs)


class ManagedProcess:
    def __init__(self, pane: EntityPane, command: List[str], cwd: Path, log_path: Optional[Path] = None) -> None:
        self.pane = pane
        self.command = command
        self.cwd = cwd
        self.log_path = log_path
        self.proc: Optional[subprocess.Popen[str]] = None
        self.reader_thread: Optional[threading.Thread] = None
        self.log_file: Optional[TextIO] = None

    def _write_log_file(self, line: str) -> None:
        if self.log_file is None:
            return
        self.log_file.write(line)
        self.log_file.flush()

    def start(self) -> None:
        self.pane.append_log("$ " + " ".join(self.command))
        if self.log_path is not None:
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            self.log_file = self.log_path.open("w", encoding="utf-8")
            self._write_log_file("$ " + " ".join(self.command) + "\n")

        self.proc = subprocess.Popen(
            self.command,
            cwd=str(self.cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        def _reader() -> None:
            assert self.proc is not None
            assert self.proc.stdout is not None
            for line in self.proc.stdout:
                self.pane.append_log(line)
                self._write_log_file(line)
            code = self.proc.wait()
            self.pane.append_log(f"[process exited with code {code}]")
            self._write_log_file(f"[process exited with code {code}]\n")

        self.reader_thread = threading.Thread(target=_reader, daemon=True)
        self.reader_thread.start()

    def stop(self) -> None:
        if self.proc is None:
            self._close_log_file()
            return

        if self.proc.poll() is None:
            self.pane.append_log("[stopping process]")
            self._write_log_file("[stopping process]\n")
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.pane.append_log("[terminate timeout, killing process]")
                self._write_log_file("[terminate timeout, killing process]\n")
                self.proc.kill()

        self._close_log_file()

    def _close_log_file(self) -> None:
        if self.log_file is not None:
            self.log_file.close()
            self.log_file = None


class WindowedDashboard:
    def __init__(self, config: Optional[Dict[str, object]] = None, workspace: Optional[Path] = None, config_path: Optional[Path] = None, auto_run: bool = False, auto_close: bool = False) -> None:
        self.workspace = workspace or Path.cwd()
        self.config: Optional[Dict[str, object]] = None
        self.demo_running = False
        self.auto_run = auto_run
        self.auto_close = auto_close
        self.config_window: Optional[tk.Toplevel] = None
        self.content_frame: Optional[ttk.Frame] = None

        # Config-derived fields with safe defaults (overwritten by _apply_config)
        self.poll_interval = 1.0
        self.log_limit = 400
        self.command_timeout = 3.0
        self.nr_cli = "./build/nr-cli"
        self.nr_ue = "./build/nr-ue"
        self.nr_gnb = "./build/nr-gnb"
        self.ue_count = 1
        self.ue_keys: List[str] = ["ue1"]
        self.primary_ue_key = "ue1"
        self.gnb_count = 0
        self.gnb_keys: List[str] = []
        self.ue_create_db_profiles = False
        self.ue_run_with_sudo = False
        self.ue_cli_with_sudo = False
        self.ue_cli_sudo_non_interactive = True
        self.ue_cli_sudo_disabled_logged = False
        self.user_plane_upf_ip = "127.0.0.1"
        self.user_plane_upf_port = 5000
        self.user_plane_capture_enabled = True
        self.user_plane_capture_use_sudo = True
        self.user_plane_capture_sudo_non_interactive = True
        self.user_plane_move_tun_to_netns = False
        self.user_plane_netns_name = "ue1"
        self.user_plane_netns_prefix_len = 16
        self.user_plane_log_limit = 400
        self.user_plane_auto_ensure_host_route = True
        self.user_plane_host_route_gateway = "172.22.0.1"
        self.user_plane_host_route_subnet = "172.22.0.0/24"
        self.user_plane_rx_logs: deque[str] = deque(maxlen=self.user_plane_log_limit)
        self.user_plane_tx_logs: deque[str] = deque(maxlen=self.user_plane_log_limit)
        self.user_plane_lock = threading.Lock()
        self.user_plane_window: Optional[tk.Toplevel] = None
        self.user_plane_rx_widget: Optional[scrolledtext.ScrolledText] = None
        self.user_plane_tx_widget: Optional[scrolledtext.ScrolledText] = None
        self.user_plane_header_var: Optional[tk.StringVar] = None
        self.user_plane_capture_thread: Optional[threading.Thread] = None
        self.user_plane_tcpdump_thread: Optional[threading.Thread] = None
        self.user_plane_tcpdump_proc: Optional[subprocess.Popen[str]] = None
        self.user_plane_capture_stop = threading.Event()
        self.user_plane_capture_status = "stopped"
        self.user_plane_capture_backend = "none"
        self.user_plane_route_status = "unknown"
        self.user_plane_last_route_log = ""
        self.user_plane_route_last_check_ip: Optional[str] = None
        self.user_plane_route_last_check_time = 0.0
        self.user_plane_netns_status = "disabled"
        self.user_plane_tun_moved_to_netns = False
        self.user_plane_last_netns_log = ""
        self.primary_ue_tun_name: Optional[str] = None
        self.primary_ue_tun_ip: Optional[str] = None

        self.panes: Dict[str, EntityPane] = {}
        self.processes: Dict[str, ManagedProcess] = {}
        self.stop_event = threading.Event()
        self.node_names: Dict[str, str] = {}
        self.last_cli_error: Dict[str, str] = {}
        self.ue_db_status: Dict[str, str] = {}
        self.gnb_link_ips: Dict[str, str] = {}
        self.gnb_tles: Dict[str, tuple] = {}
        self.current_rsrp_dbm: Dict[str, Optional[int]] = {}
        self.rsrp_current_vars: Dict[str, tk.StringVar] = {}

        self.root = tk.Tk()
        self.root.title("UERANSIM Windowed Dashboard")
        self.root.geometry("1500x900")
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        self.log_widgets: Dict[str, scrolledtext.ScrolledText] = {}
        self.scalar_vars: Dict[str, tk.StringVar] = {}
        self.header_config_var = tk.StringVar(value="Demo: None")
        self.header_run_var = tk.StringVar(value="Status: Ready")
        self.config_name: str = "None"
        self.demo_name: str = ""
        self._demo_completed: bool = False
        self.location_window: Optional[tk.Toplevel] = None
        self.rsrp_window: Optional[tk.Toplevel] = None
        self.viz_window: Optional[tk.Toplevel] = None
        self.inject_target_var = tk.StringVar(value="gnb1")
        self.inject_dbm_var = tk.StringVar(value="-80")
        self.inject_status_var = tk.StringVar(value="Ready")
        self.ue_count_var = tk.StringVar(value="UEs: total=0, log-pane=1")
        self.elapsed_var = tk.StringVar(value="Elapsed: 0s")
        self.demo_start_time: float = 0.0
        self.elapsed_timer_id: Optional[str] = None
        self.run_stop_after_sec: float = 0.0
        self.run_iterations: int = 1
        self.run_iteration: int = 0
        self.reset_core: bool = False
        self.reset_command: str = ""
        self.reset_command_args: List[str] = []
        self._reset_proc: Optional[subprocess.Popen[str]] = None
        self._resetting: bool = False
        self.log_dir: Path = Path("./tools/UI/logs")
        self.run_log_dir_name: str = "run-%t"
        self.run_log_dir: Optional[Path] = None
        self.run_log_template: str = "demo.log"
        self.ue_log_file_template: str = "ue_%i.log"
        self.gnb_log_file_templates: Dict[str, str] = {}
        self._run_ts: str = ""
        self._demo_log_file: Optional[TextIO] = None
        self.stop_after_timer_id: Optional[str] = None
        self.program_thread: Optional[threading.Thread] = None
        self.program_stop_event = threading.Event()
        self.program_running = False
        self.repeat_message_thread: Optional[threading.Thread] = None
        self.repeat_message_stop_event = threading.Event()
        self.repeat_message_running = False

        self.upt_enabled = False
        self.upt_ue_source = "UE1"
        self.upt_start_trigger = "tun-up"
        self.upt_start_delay_sec = 0.0
        self.upt_cycle_time_sec = 1.0
        self.upt_end_time_sec = 60.0
        self.upt_end_count = 0
        self.upt_message_text = "Hello from demo"
        self.upt_message_size_bytes = 0
        self.upt_pad_byte_value = 0x20

        self.amf_log_file_template: str = ""
        self.pcap_enabled = False
        self.pcap_interface = ""
        self.pcap_output_file = "trace.pcap"
        self.pcap_use_sudo = True
        self.pcap_sudo_non_interactive = True
        self.pcap_proc: Optional[subprocess.Popen[str]] = None
        self.pcap_thread: Optional[threading.Thread] = None
        self.pcap_stop_event = threading.Event()

        self.script_entries: List[Dict[str, object]] = []
        self.script_start_time: float = 0.0
        self.script_executed_set: set = set()
        self.script_timer_id: Optional[str] = None
        self._script_editor_rows: List[Dict[str, object]] = []

        self._build_skeleton_ui()

        if config is not None:
            if config_path is not None:
                self.config_name = config_path.name
            self._apply_config(config, workspace or Path.cwd())

    def _apply_config(self, config: Dict[str, object], workspace: Path) -> None:
        self.config = config
        self.workspace = workspace
        self.demo_name = str(config.get("demo_name", ""))

        ui_cfg = config.get("ui", {})
        self.poll_interval = float(ui_cfg.get("poll_interval_sec", 1.0))
        self.log_limit = int(ui_cfg.get("max_log_lines", 400))
        self.command_timeout = float(ui_cfg.get("command_timeout_sec", 3.0))

        self.nr_cli = str(config.get("command_cli", "./build/nr-cli"))
        ue_cfg = config.get("ue", {})
        self.nr_ue = str(ue_cfg.get("ue_command", "./build/nr-ue"))
        self.ue_log_file_template = str(ue_cfg.get("ue_log_file", "ue_%i.log"))
        self.ue_create_db_profiles = bool(ue_cfg.get("create_db_profiles", False))
        self.ue_count = max(1, int(ue_cfg.get("count", 1)))
        self.ue_keys = ["ue1"]  # single log pane regardless of count
        self.primary_ue_key = "ue1"
        self.ue_run_with_sudo = bool(ue_cfg.get("run_with_sudo", False))
        self.ue_cli_with_sudo = bool(ue_cfg.get("cli_with_sudo", self.ue_run_with_sudo))
        self.ue_cli_sudo_non_interactive = bool(ue_cfg.get("cli_sudo_non_interactive", True))
        self.ue_cli_sudo_disabled_logged = False

        up_cfg = config.get("user_plane", {})
        self.user_plane_upf_ip = str(up_cfg.get("upf_ip", "127.0.0.1"))
        self.user_plane_upf_port = int(up_cfg.get("upf_port", 5000))
        self.user_plane_capture_enabled = bool(up_cfg.get("capture_enabled", True))
        self.user_plane_capture_use_sudo = bool(up_cfg.get("capture_use_sudo", True))
        self.user_plane_capture_sudo_non_interactive = bool(
            up_cfg.get("capture_sudo_non_interactive", True)
        )
        self.user_plane_move_tun_to_netns = bool(up_cfg.get("move_tun_to_netns", False))
        self.user_plane_netns_name = str(up_cfg.get("netns_name", "ue1"))
        self.user_plane_netns_prefix_len = int(up_cfg.get("netns_prefix_len", 16))
        self.user_plane_log_limit = int(up_cfg.get("max_log_lines", 400))
        self.user_plane_auto_ensure_host_route = bool(up_cfg.get("ensure_host_route_to_ue", True))
        self.user_plane_host_route_gateway = str(up_cfg.get("host_route_gateway", "172.22.0.1"))
        self.user_plane_host_route_subnet = str(up_cfg.get("host_route_subnet", "172.22.0.0/24"))

        upt_cfg = config.get("user_plane_test", {})
        self.upt_enabled = bool(upt_cfg.get("enabled", False))
        self.upt_ue_source = str(upt_cfg.get("ue_source", "UE1"))
        self.upt_start_trigger = str(upt_cfg.get("start_trigger", "tun-up")).strip().lower()
        self.upt_start_delay_sec = float(upt_cfg.get("start_delay_sec", 0.0))
        self.upt_cycle_time_sec = float(upt_cfg.get("cycle_time_sec", 1.0))
        self.upt_end_time_sec = float(upt_cfg.get("end_time_sec", 0.0))
        self.upt_end_count = int(upt_cfg.get("end_count", 0))
        self.upt_message_text = str(upt_cfg.get("message_text", "Hello from demo"))
        self.upt_message_size_bytes = int(upt_cfg.get("message_size_bytes", 0))
        raw_pad = upt_cfg.get("pad_byte_value", 0x20)
        self.upt_pad_byte_value = int(str(raw_pad), 0) if isinstance(raw_pad, str) else int(raw_pad)

        run_cfg = config.get("run", {})
        self.run_stop_after_sec = float(run_cfg.get("stop_after_sec", 0.0))
        self.run_iterations = max(1, int(run_cfg.get("iterations", 1)))
        self.reset_core = bool(run_cfg.get("reset_core", False))
        self.reset_command = str(run_cfg.get("reset_command", ""))
        self.reset_command_args = [str(x) for x in run_cfg.get("reset_command_args", [])]
        self.run_log_template = str(run_cfg.get("run_log", "demo_%i.log"))
        log_cfg = run_cfg.get("logging", {})
        log_dir_str = str(log_cfg.get("log_dir", "./tools/UI/logs"))
        self.log_dir = Path(self._resolve(log_dir_str))
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.run_log_dir_name = str(log_cfg.get("run_log_dir_name", "run-%t"))

        amf_cfg = config.get("amf", {})
        self.amf_log_file_template = str(amf_cfg.get("amf_log_file", ""))

        pcap_cfg = config.get("pcap", {})
        self.pcap_enabled = bool(pcap_cfg.get("enabled", False))
        self.pcap_interface = str(pcap_cfg.get("interface", ""))
        self.pcap_output_file = str(pcap_cfg.get("output_file", "trace.pcap"))
        self.pcap_use_sudo = bool(pcap_cfg.get("use_sudo", True))
        self.pcap_sudo_non_interactive = bool(pcap_cfg.get("sudo_non_interactive", True))

        gnb_section = config.get("gnb", {})
        self.nr_gnb = str(gnb_section.get("gnb_command", "./build/nr-gnb"))
        gnbs = gnb_section.get("gnbs", [])
        self.gnb_count = min(3, len(gnbs))
        self.gnb_keys = [f"gnb{i + 1}" for i in range(self.gnb_count)]
        self.gnb_log_file_templates = {
            f"gnb{i + 1}": str(gnbs[i].get("gnb_log_file", f"gnb{i + 1}_%i.log"))
            for i in range(self.gnb_count)
        }

        self.panes = {
            "demo": EntityPane("demo", "Demo"),
            "amf": EntityPane("amf", "AMF"),
        }
        for i, key in enumerate(self.gnb_keys):
            self.panes[key] = EntityPane(key, f"GNB{i + 1}")
        for idx, key in enumerate(self.ue_keys):
            title = "UE" if idx == 0 else f"UE{idx + 1}"
            self.panes[key] = EntityPane(key, title)
        for pane in self.panes.values():
            pane.logs = deque(maxlen=self.log_limit)

        self.node_names = {self.gnb_keys[i]: str(gnbs[i]["node"]) for i in range(self.gnb_count)}
        self.node_names.update(self._build_ue_node_names())
        self.last_cli_error = {}
        self.ue_db_status = {key: "UNKNOWN" for key in self.ue_keys}
        self.gnb_link_ips = {self.gnb_keys[i]: self._extract_gnb_link_ip(i) for i in range(self.gnb_count)}

        sat_tles_list = config.get("sat_tles", [])
        tle_by_node: Dict[str, tuple] = {}
        for tle_entry in (sat_tles_list if isinstance(sat_tles_list, list) else []):
            if isinstance(tle_entry, dict) and "gnb" in tle_entry and "line1" in tle_entry and "line2" in tle_entry:
                tle_by_node[str(tle_entry["gnb"])] = (str(tle_entry["line1"]), str(tle_entry["line2"]))
        self.gnb_tles = {
            self.gnb_keys[i]: tle_by_node[str(gnbs[i]["node"])]
            for i in range(self.gnb_count)
            if str(gnbs[i]["node"]) in tle_by_node
        }

        self.current_rsrp_dbm = {key: None for key in self.gnb_keys}
        self.rsrp_current_vars = {}
        self.ue_count_var.set(f"UEs: total={self.ue_count}, log-pane=1")
        self.inject_target_var.set(self.gnb_keys[0] if self.gnb_keys else "gnb1")

        self.script_entries = self._parse_script(config.get("script", []))
        self._script_editor_rows = list(self.script_entries)

        self._update_header_config()
        self._rebuild_content_ui()

    def _resolve(self, value: str) -> str:
        p = Path(value)
        if p.is_absolute():
            return str(p)
        return str((self.workspace / p).resolve())

    def _parse_script(self, script_json: object) -> List[Dict[str, object]]:
        rows: List[Dict[str, object]] = []
        if not isinstance(script_json, list):
            return rows
        for entry in script_json:
            if not isinstance(entry, dict):
                continue
            t = float(entry.get("time", 0))
            for cmd in entry.get("commands", []):
                if not isinstance(cmd, dict):
                    continue
                rows.append({
                    "time": t,
                    "node": str(cmd.get("node", "")),
                    "command": str(cmd.get("command", "")),
                    "args": cmd.get("args", []),
                })
        rows.sort(key=lambda r: float(r["time"]))
        return rows

    @staticmethod
    def _script_rows_to_json(rows: List[Dict[str, object]]) -> List[Dict[str, object]]:
        groups: Dict[float, List[Dict[str, object]]] = {}
        for row in rows:
            t = float(row["time"])
            groups.setdefault(t, []).append({
                "node": row["node"],
                "command": row["command"],
                "args": row["args"],
            })
        return [{"time": t, "commands": cmds} for t, cmds in sorted(groups.items())]

    def _expand_log_name(self, template: str, iteration: Optional[int] = None) -> str:
        """Expand %i (iteration), %u (UE count), %t (run timestamp) wildcards in a log name template."""
        result = template
        result = result.replace("%i", str(iteration) if iteration is not None else str(self.run_iteration))
        result = result.replace("%u", str(self.ue_count))
        result = result.replace("%t", self._run_ts or time.strftime("%Y%m%d-%H%M%S"))
        return result

    def _process_log_path(self, key: str) -> Path:
        log_dir = self.run_log_dir if self.run_log_dir is not None else self.log_dir
        if key == "ue1" or key in self.ue_keys:
            template = self.ue_log_file_template
        elif key in self.gnb_log_file_templates:
            template = self.gnb_log_file_templates[key]
        else:
            template = f"{key}_%i.log"
        return log_dir / self._expand_log_name(template)

    def _build_ue_node_names(self) -> Dict[str, str]:
        ue_cfg = self.config["ue"]
        base_node = str(ue_cfg["node"])
        match = re.search(r"^(.*?)(\d+)$", base_node)
        if match:
            prefix = match.group(1)
            start = int(match.group(2))
            return {
                self.ue_keys[idx]: f"{prefix}{start + idx}"
                for idx in range(len(self.ue_keys))
            }

        return {
            self.ue_keys[idx]: f"{base_node}{idx + 1}"
            for idx in range(len(self.ue_keys))
        }

    def _extract_gnb_link_ip(self, idx: int) -> str:
        path = Path(self._resolve(str(self.config["gnb"]["gnbs"][idx]["config"])))
        if not path.exists():
            return "127.0.0.1"

        pattern = re.compile(r"^\s*linkIp:\s*([^\s#]+)")
        for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = pattern.match(raw)
            if not match:
                continue
            value = match.group(1).strip().strip("\"'")
            if value:
                return value

        return "127.0.0.1"

    def _extract_imsi_override(self, ue_args: List[str]) -> Optional[str]:
        idx = 0
        while idx < len(ue_args):
            item = ue_args[idx]
            if item == "-i" or item == "--imsi":
                if idx + 1 < len(ue_args):
                    return ue_args[idx + 1]
                return None
            idx += 1
        return None

    @staticmethod
    def _normalize_imsi_value(value: Optional[str]) -> Optional[str]:
        if value is None:
            return None

        token = value.strip().strip("'\"")
        if token.startswith("imsi-"):
            token = token[5:]

        return token if token.isdigit() else None

    def _resolve_primary_ue_imsi_hint(self) -> Optional[str]:
        ue_cfg = self.config.get("ue", {})
        ue_args = [str(x) for x in ue_cfg.get("args", [])]

        # Highest priority: explicit CLI override for base IMSI.
        imsi_override = self._normalize_imsi_value(self._extract_imsi_override(ue_args))
        if imsi_override:
            return imsi_override

        # Next: supi from UE YAML config.
        cfg_raw = ue_cfg.get("config")
        if cfg_raw is not None:
            cfg_path = Path(self._resolve(str(cfg_raw)))
            if cfg_path.exists():
                try:
                    parsed = parse_simple_yaml(cfg_path.read_text(encoding="utf-8", errors="replace"))
                    supi = parsed.get("supi")
                    supi_imsi = self._normalize_imsi_value(supi)
                    if supi_imsi:
                        return supi_imsi
                except OSError:
                    pass

        # Last chance: runtime CLI node mapping can become imsi-<digits>.
        mapped = self.node_names.get(self.primary_ue_key)
        return self._normalize_imsi_value(mapped)

    def _warn_open5gs_subscribers(self, ue_cfg: Dict[str, object]) -> None:
        db_cfg = self.config.get("core_db", {})
        enabled = bool(db_cfg.get("warn_missing_subscribers", True))
        if not enabled:
            for key in self.ue_keys:
                self.ue_db_status[key] = "DISABLED"
            return

        script_path = Path(self._resolve(str(db_cfg.get("dbctl_script", "./tools/open5gs-dbctl.py"))))
        if not script_path.exists():
            for key in self.ue_keys:
                self.ue_db_status[key] = "CHECK_SKIPPED"
            self.panes[self.primary_ue_key].append_log(
                f"[db-check] skipped: script not found at {script_path}"
            )
            return

        python_exec = str(db_cfg.get("python", "python3"))
        db_uri = str(db_cfg.get("db_uri", "mongodb://localhost/open5gs"))
        timeout = float(db_cfg.get("check_timeout_sec", 10.0))

        cmd = [
            python_exec,
            str(script_path),
            "--db_uri",
            db_uri,
            "check_multi_yaml",
            self._resolve(str(ue_cfg["config"])),
            str(self.ue_count),
        ]

        imsi_override = self._extract_imsi_override([str(x) for x in ue_cfg.get("args", [])])
        if imsi_override:
            cmd.extend(["--imsi_override", imsi_override])

        self.panes[self.primary_ue_key].append_log("[db-check] validating Open5GS subscribers...")
        for key in self.ue_keys:
            self.ue_db_status[key] = "CHECKING"

        try:
            proc = subprocess.run(
                cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=timeout,
                check=False,
            )
        except FileNotFoundError as ex:
            for key in self.ue_keys:
                self.ue_db_status[key] = "CHECK_FAILED"
            self.panes[self.primary_ue_key].append_log(f"[db-check] skipped: {ex}")
            return
        except subprocess.TimeoutExpired:
            for key in self.ue_keys:
                self.ue_db_status[key] = "CHECK_TIMEOUT"
            self.panes[self.primary_ue_key].append_log("[db-check] timed out")
            return

        output_lines: List[str] = []
        if proc.stdout:
            output_lines.extend([line.rstrip() for line in proc.stdout.splitlines() if line.strip()])
        if proc.stderr:
            output_lines.extend([line.rstrip() for line in proc.stderr.splitlines() if line.strip()])

        if not output_lines:
            self.panes[self.primary_ue_key].append_log("[db-check] no output from database checker")
        else:
            for line in output_lines:
                self.panes[self.primary_ue_key].append_log("[db-check] " + line)

        # Parse checker output lines such as: [OK] IMSI 001010000000001
        status_rows: List[tuple[str, str]] = []
        status_re = re.compile(r"^\[(OK|MISSING|INCOMPATIBLE)\]\s+IMSI\s+(\S+)")
        for line in output_lines:
            match = status_re.match(line.strip())
            if not match:
                continue
            status_rows.append((match.group(1), match.group(2)))

        for idx, key in enumerate(self.ue_keys):
            if idx < len(status_rows):
                status, imsi = status_rows[idx]
                self.ue_db_status[key] = f"{status} ({imsi})"
            elif self.ue_db_status.get(key) == "CHECKING":
                self.ue_db_status[key] = "UNKNOWN"

        if proc.returncode == 0:
            self.panes[self.primary_ue_key].append_log("[db-check] subscriber profiles are compatible")
        elif proc.returncode == 3:
            self.panes[self.primary_ue_key].append_log(
                "[db-check] warning: missing/incompatible subscriber profiles detected"
            )
        else:
            for key in self.ue_keys:
                if self.ue_db_status.get(key, "") in {"CHECKING", "UNKNOWN"}:
                    self.ue_db_status[key] = "CHECK_FAILED"
            self.panes[self.primary_ue_key].append_log(
                f"[db-check] checker failed with exit code {proc.returncode}"
            )

    def _build_skeleton_ui(self) -> None:
        self._build_menu()
        self.content_frame = ttk.Frame(self.root)
        self.content_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        ttk.Label(
            self.content_frame,
            text="No config loaded — use File > Load Config",
            font=("TkDefaultFont", 14),
        ).place(relx=0.5, rely=0.5, anchor="center")

    def _rebuild_content_ui(self) -> None:
        for win_attr in ("rsrp_window", "location_window", "user_plane_window", "viz_window"):
            win = getattr(self, win_attr, None)
            if win is not None and win.winfo_exists():
                win.destroy()
            setattr(self, win_attr, None)
        self.log_widgets = {}
        self.scalar_vars = {}
        self.rsrp_current_vars = {}

        if self.content_frame is not None:
            self.content_frame.destroy()

        self.content_frame = ttk.Frame(self.root)
        self.content_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        controls = ttk.Frame(self.content_frame, padding=(8, 4))
        controls.pack(side=tk.TOP, fill=tk.X)

        # Right-aligned RSRP controls (packed before left items)
        inject_button = ttk.Button(controls, text="Inject", command=self._inject_rsrp)
        inject_button.pack(side=tk.RIGHT, padx=(4, 0))
        dbm_entry = ttk.Entry(controls, width=8, textvariable=self.inject_dbm_var)
        dbm_entry.pack(side=tk.RIGHT, padx=4)
        target_box = ttk.Combobox(
            controls,
            width=10,
            state="readonly",
            textvariable=self.inject_target_var,
            values=self.gnb_keys,
        )
        target_box.pack(side=tk.RIGHT, padx=(4, 4))
        ttk.Label(controls, text="Quick RSRP change:").pack(side=tk.RIGHT, padx=(16, 0))

        # Left-aligned info
        ttk.Label(controls, textvariable=self.header_config_var).pack(side=tk.LEFT)
        ttk.Label(controls, textvariable=self.header_run_var, font=("TkDefaultFont", 10, "normal")).pack(side=tk.LEFT, padx=(24, 0))

        panes_frame = ttk.Frame(self.content_frame, padding=8)
        panes_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        panes_frame.grid_columnconfigure(0, weight=1)
        panes_frame.grid_columnconfigure(1, weight=1)

        # Left column: Demo, UE(s), AMF stacked
        left_keys: List[str] = ["demo", *self.ue_keys, "amf"]
        for r, key in enumerate(left_keys):
            pane_frame = ttk.LabelFrame(panes_frame, text=self.panes[key].title, padding=8)
            pane_frame.grid(row=r, column=0, sticky="nsew", padx=(6, 3), pady=6)
            panes_frame.grid_rowconfigure(r, weight=1)
            self._create_pane_widgets(pane_frame, key, log_height=10)

        # Right column: all GNBs in a subframe so they share vertical space evenly
        right_frame = ttk.Frame(panes_frame)
        right_frame.grid(row=0, column=1, rowspan=len(left_keys), sticky="nsew", padx=(3, 6), pady=6)
        for key in self.gnb_keys:
            gnb_frame = ttk.LabelFrame(right_frame, text=self.panes[key].title, padding=8)
            gnb_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True, pady=(0, 6))
            self._create_pane_widgets(gnb_frame, key, log_height=10)

        self.run_menu.entryconfig("Run Demo", state=tk.NORMAL)
        self.run_menu.entryconfig("Stop Demo", state=tk.DISABLED)

    def _create_pane_widgets(self, parent: ttk.Widget, key: str, log_height: int) -> None:
        show_scalars = key in self.gnb_keys or key == "amf" or key == self.primary_ue_key
        if show_scalars:
            var = tk.StringVar(value="")
            self.scalar_vars[key] = var
            box = ttk.LabelFrame(parent, padding=(4, 2))
            box.pack(side=tk.TOP, fill=tk.X, pady=(0, 4))
            ttk.Label(
                box,
                textvariable=var,
                font=("TkFixedFont", 9),
                anchor="w",
                justify="left",
            ).pack(side=tk.TOP, fill=tk.X)
        log_widget = scrolledtext.ScrolledText(parent, wrap=tk.WORD, height=log_height)
        log_widget.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        log_widget.configure(state=tk.DISABLED, font=("TkFixedFont", 10))
        self.log_widgets[key] = log_widget

    def _build_menu(self) -> None:
        menubar = tk.Menu(self.root)

        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="Load Config", command=self._load_config_dialog)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self._on_close)
        menubar.add_cascade(label="File", menu=file_menu)

        self.run_menu = tk.Menu(menubar, tearoff=0)
        self.run_menu.add_command(label="Run Demo", command=self._run_demo, state=tk.DISABLED)
        self.run_menu.add_command(label="Stop Demo", command=self._stop_demo, state=tk.DISABLED)
        self.run_menu.add_separator()
        self.run_menu.add_command(label="Config", command=self._open_config_window)
        menubar.add_cascade(label="Run", menu=self.run_menu)

        handover_menu = tk.Menu(menubar, tearoff=0)
        handover_menu.add_command(label="Send gNB Position/Velocity", command=self._open_gnb_loc_pv_dialog)
        handover_menu.add_command(label="Program", command=self._open_handover_program_dialog)
        handover_menu.add_command(label="Stop Program", command=self._stop_handover_program)
        menubar.add_cascade(label="Handover", menu=handover_menu)

        location_menu = tk.Menu(menubar, tearoff=0)
        location_menu.add_command(label="Open Location Editor", command=self._open_location_window)
        menubar.add_cascade(label="Location", menu=location_menu)

        rsrp_menu = tk.Menu(menubar, tearoff=0)
        rsrp_menu.add_command(label="Open gNB RSRP Editor", command=self._open_rsrp_window)
        menubar.add_cascade(label="RSRP", menu=rsrp_menu)

        viz_menu = tk.Menu(menubar, tearoff=0)
        viz_menu.add_command(label="Open Window", command=self._open_viz_window)
        menubar.add_cascade(label="Visualization", menu=viz_menu)

        user_plane_menu = tk.Menu(menubar, tearoff=0)
        user_plane_menu.add_command(label="Open Demo", command=self._open_user_plane_demo_window)
        user_plane_menu.add_command(label="Send Text Message", command=self._open_user_plane_send_dialog)
        user_plane_menu.add_command(
            label="Send Repeated Message",
            command=self._open_user_plane_repeated_send_dialog,
        )
        user_plane_menu.add_command(
            label="Stop Repeated Message",
            command=self._stop_user_plane_repeated_message,
        )
        menubar.add_cascade(label="User Plane", menu=user_plane_menu)

        self.root.config(menu=menubar)

    def _load_config_dialog(self) -> None:
        from tkinter import filedialog
        if self.demo_running:
            messagebox.showwarning("Demo Running", "Stop the demo before loading a new config.")
            return
        path = filedialog.askopenfilename(
            title="Load Config",
            filetypes=[("YAML files", "*.yaml"), ("JSON files", "*.json"), ("All files", "*.*")],
        )
        if not path:
            return
        cfg_path = Path(path).resolve()
        try:
            config = _load_config_file(cfg_path)
            validate_config(config)
        except Exception as ex:
            messagebox.showerror("Config Error", str(ex))
            return
        self.config_name = cfg_path.name
        workspace = cfg_path.parent.parent.parent.resolve()
        self._apply_config(config, workspace)
        if self.config_window is not None and self.config_window.winfo_exists():
            self._populate_config_window()

    def _run_demo(self) -> None:
        if self.config is None:
            messagebox.showwarning("No Config", "Load a config first via File > Load Config.")
            return
        if self.demo_running:
            return
        self.run_iteration = 1
        self._run_ts = time.strftime("%Y%m%d-%H%M%S")
        run_dir_name = self._expand_log_name(self.run_log_dir_name)
        self.run_log_dir = self.log_dir / run_dir_name
        try:
            self.run_log_dir.mkdir(parents=True, exist_ok=True)
        except OSError as ex:
            self.run_log_dir = self.log_dir
            self.panes["demo"].append_log(f"[demo] could not create run log dir: {ex}; using {self.log_dir}")
        self._open_demo_log_file()
        self._start_demo_iteration()

    def _demo_log(self, msg: str) -> None:
        ts = time.strftime("%Y-%m-%d %H:%M:%S")
        line = f"[{ts}] {msg}"
        self.panes["demo"].append_log(line)
        if self._demo_log_file is not None:
            try:
                self._demo_log_file.write(line + "\n")
                self._demo_log_file.flush()
            except OSError:
                pass

    def _open_demo_log_file(self) -> None:
        try:
            log_dir = self.run_log_dir if self.run_log_dir is not None else self.log_dir
            filename = self._expand_log_name(self.run_log_template)
            path = log_dir / filename
            self._demo_log_file = path.open("w", encoding="utf-8")
        except OSError as ex:
            self.panes["demo"].append_log(f"[demo] could not open log file: {ex}")

    def _close_demo_log_file(self) -> None:
        if self._demo_log_file is not None:
            try:
                self._demo_log_file.close()
            except OSError:
                pass
            self._demo_log_file = None

    def _start_demo_iteration(self) -> None:
        if self.demo_running:
            return
        self.stop_event = threading.Event()
        self.demo_running = True
        self._demo_completed = False
        self._resetting = False
        self.run_menu.entryconfig("Run Demo", state=tk.DISABLED)
        self.run_menu.entryconfig("Stop Demo", state=tk.NORMAL)
        self.primary_ue_tun_name = None
        self.primary_ue_tun_ip = None
        self._demo_log(f"[run] === Iteration {self.run_iteration}/{self.run_iterations} started ===")
        if self.reset_core:
            self._resetting = True
            self._update_run_header()
            self._demo_log(f"[reset] resetting core: {self.reset_command} {' '.join(self.reset_command_args)}")
            threading.Thread(target=self._run_core_reset, daemon=True).start()
        elif self.ue_create_db_profiles:
            threading.Thread(target=self._create_db_profiles, daemon=True).start()
        else:
            self._launch_demo_processes()

    def _run_core_reset(self) -> None:
        cmd = self.reset_command.strip()
        try:
            parts = shlex.split(cmd) if cmd else []
            if not parts:
                raise FileNotFoundError("reset_command is empty")
            args = self.reset_command_args
            # replace %i with iteration number in args if present
            args = [str(arg).replace("%i", str(self.run_iteration)) for arg in args]
            parts.extend(args)
            self._reset_proc = subprocess.Popen(
                parts,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                cwd=str(self.workspace),
            )
            assert self._reset_proc.stdout is not None
            for line in self._reset_proc.stdout:
                self._demo_log("[reset] " + line.rstrip())
            returncode = self._reset_proc.wait()
            self._reset_proc = None
            if self.stop_event.is_set():
                return
            if returncode == 0:
                self.root.after(0, self._on_reset_success)
            else:
                self.root.after(0, lambda: self._on_reset_failed(f"exited with code {returncode}"))
        except FileNotFoundError:
            self._reset_proc = None
            if not self.stop_event.is_set():
                self.root.after(0, lambda: self._on_reset_failed(f"command not found: {cmd!r}"))
        except Exception as ex:
            self._reset_proc = None
            if not self.stop_event.is_set():
                self.root.after(0, lambda: self._on_reset_failed(str(ex)))

    def _on_reset_success(self) -> None:
        if not self.demo_running:
            return
        self._resetting = False
        self._demo_log("[reset] core reset complete")
        if self.ue_create_db_profiles:
            threading.Thread(target=self._create_db_profiles, daemon=True).start()
        else:
            delay = 5
            self._demo_log(f"[reset] waiting {delay}s for core services to come up...")
            self.root.after(delay * 1000, self._launch_demo_processes)

    def _on_reset_failed(self, error: str) -> None:
        self._resetting = False
        self._demo_log(f"[reset] FAILED: {error}")
        self._stop_demo()
        messagebox.showerror("Core Reset Failed", f"Core network reset failed:\n\n{error}")

    def _create_db_profiles(self) -> None:
        """Background thread: reset subscriber DB then add UE profiles via dbctl."""
        db_cfg = self.config.get("core_db", {})
        script_path = Path(self._resolve(str(db_cfg.get("dbctl_script", "./tools/open5gs-dbctl.py"))))
        python_exec = str(db_cfg.get("python", "python3"))
        db_uri = str(db_cfg.get("db_uri", "mongodb://localhost/open5gs"))
        timeout = float(db_cfg.get("check_timeout_sec", 10.0))
        ue_cfg = self.config["ue"]
        ue_config = self._resolve(str(ue_cfg["config"]))

        self.root.after(0, lambda: self._demo_log("[db-setup] creating subscriber profiles..."))

        if not script_path.exists():
            if not self.stop_event.is_set():
                self.root.after(0, lambda: self._on_db_profiles_failed(f"dbctl script not found: {script_path}"))
            return

        base_cmd = [python_exec, str(script_path), "--db_uri", db_uri]
        imsi_override = self._extract_imsi_override([str(x) for x in ue_cfg.get("args", [])])

        def _run_dbctl(step_label: str, args: List[str]) -> bool:
            cmd = base_cmd + args
            if imsi_override:
                cmd = base_cmd + ["--imsi_override", imsi_override] + args
            self.root.after(0, lambda c=cmd: self._demo_log(f"[db-setup] {step_label}: " + " ".join(c)))
            try:
                proc = subprocess.run(
                    cmd,
                    cwd=str(self.workspace),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=timeout,
                    check=False,
                )
                for line in (proc.stdout or "").splitlines():
                    if line.strip():
                        self.root.after(0, lambda l=line: self._demo_log(f"[db-setup] {l.rstrip()}"))
                if proc.returncode != 0:
                    if not self.stop_event.is_set():
                        self.root.after(0, lambda: self._on_db_profiles_failed(
                            f"{step_label} exited with code {proc.returncode}"
                        ))
                    return False
            except subprocess.TimeoutExpired:
                if not self.stop_event.is_set():
                    self.root.after(0, lambda: self._on_db_profiles_failed(f"{step_label} timed out"))
                return False
            except FileNotFoundError:
                if not self.stop_event.is_set():
                    self.root.after(0, lambda: self._on_db_profiles_failed(f"{step_label}: python not found"))
                return False
            return True

        if self.stop_event.is_set():
            return
        if not _run_dbctl("reset", ["reset"]):
            return
        if self.stop_event.is_set():
            return
        if not _run_dbctl("add_multi_yaml", ["add_multi_yaml", ue_config, str(self.ue_count)]):
            return
        if not self.stop_event.is_set():
            self.root.after(0, self._on_db_profiles_success)

    def _on_db_profiles_success(self) -> None:
        if not self.demo_running:
            return
        self._demo_log("[db-setup] subscriber profiles created successfully")
        delay = 2
        self._demo_log(f"[db-setup] waiting {delay}s before starting processes...")
        self.root.after(delay * 1000, self._launch_demo_processes)

    def _on_db_profiles_failed(self, error: str) -> None:
        self._demo_log(f"[db-setup] FAILED: {error}")
        self._stop_demo()
        messagebox.showerror("DB Profile Setup Failed", f"Subscriber profile creation failed:\n\n{error}")

    def _launch_demo_processes(self) -> None:
        self.demo_start_time = time.time()
        self._tick_elapsed()
        self._start_pcap_capture()
        self._start_processes()
        for target in (self._status_loop, self._amf_loop, self._amf_log_loop):
            threading.Thread(target=target, daemon=True).start()
        self._maybe_schedule_auto_user_plane_test()
        if self.script_entries:
            self.script_start_time = time.time()
            self.script_executed_set = set()
            self.script_timer_id = self.root.after(1000, self._script_tick)
        if self.run_stop_after_sec > 0:
            self.stop_after_timer_id = self.root.after(
                int(self.run_stop_after_sec * 1000), self._iteration_complete
            )

    def _iteration_complete(self) -> None:
        """Called when a timed iteration ends naturally; restarts if more iterations remain."""
        more = self.run_iteration < self.run_iterations
        if not more:
            self._demo_completed = True
        self._demo_log(f"[run] === Iteration {self.run_iteration}/{self.run_iterations} stopped ===")
        self._stop_demo(close_log=not more)
        if more:
            self.run_iteration += 1
            self.run_menu.entryconfig("Run Demo", state=tk.DISABLED)
            self.root.after(500, self._start_demo_iteration)
        else:
            self._update_run_header()
            if self.auto_close:
                self.root.after(500, self._on_close)

    def _stop_demo(self, close_log: bool = True) -> None:
        if not self.demo_running:
            return
        self._resetting = False
        if close_log:
            self._demo_log(f"[run] === Iteration {self.run_iteration}/{self.run_iterations} stopped ===")
        if self._reset_proc is not None and self._reset_proc.poll() is None:
            self._reset_proc.terminate()
            self._reset_proc = None
        self._stop_handover_program()
        self._stop_user_plane_repeated_message()
        self._stop_user_plane_capture()
        self._stop_pcap_capture()
        self._copy_amf_log()
        if self.script_timer_id is not None:
            self.root.after_cancel(self.script_timer_id)
            self.script_timer_id = None
        if self.elapsed_timer_id is not None:
            self.root.after_cancel(self.elapsed_timer_id)
            self.elapsed_timer_id = None
        if self.stop_after_timer_id is not None:
            self.root.after_cancel(self.stop_after_timer_id)
            self.stop_after_timer_id = None
        self.stop_event.set()
        for proc in self.processes.values():
            proc.stop()
        self.processes.clear()
        self.demo_running = False
        if close_log:
            self._close_demo_log_file()
        if hasattr(self, "run_menu"):
            self.run_menu.entryconfig("Run Demo", state=tk.NORMAL if self.config is not None else tk.DISABLED)
            self.run_menu.entryconfig("Stop Demo", state=tk.DISABLED)
        self._update_run_header()

    def _maybe_schedule_auto_user_plane_test(self) -> None:
        if not self.upt_enabled:
            return

        trigger = self.upt_start_trigger
        delay_sec = self.upt_start_delay_sec
        message = self.upt_message_text
        cycle_sec = self.upt_cycle_time_sec
        cycle_ms = max(0, int(cycle_sec * 1000))
        end_time_sec = self.upt_end_time_sec
        end_count = self.upt_end_count
        msg_size = self.upt_message_size_bytes
        pad_byte = self.upt_pad_byte_value
        stop_event = self.stop_event

        def _log(msg: str) -> None:
            if self.panes:
                self.panes[self.primary_ue_key].append_log("[upt] " + msg)

        def _launch() -> None:
            if trigger == "tun-up":
                _log("waiting for UE TUN interface...")
                while not stop_event.is_set():
                    if self.primary_ue_tun_ip:
                        break
                    stop_event.wait(0.5)
                else:
                    return
                _log(f"TUN IP={self.primary_ue_tun_ip} detected")
            elif trigger == "immediate":
                _log("immediate start trigger")
            else:
                _log(f"unknown start_trigger '{trigger}', defaulting to tun-up")
                while not stop_event.is_set():
                    if self.primary_ue_tun_ip:
                        break
                    stop_event.wait(0.5)
                else:
                    return

            if delay_sec > 0:
                _log(f"waiting {delay_sec}s start delay...")
                if stop_event.wait(delay_sec):
                    return

            if not self.demo_running:
                return

            _log(
                f"starting: trigger={trigger} tun={self.primary_ue_tun_ip} "
                f"cycle={cycle_sec}s end_time={end_time_sec}s end_count={end_count} "
                f"size={msg_size}B pad=0x{pad_byte:02x} message={message!r}"
            )
            self.root.after(
                0,
                lambda: self._start_user_plane_repeated_message(
                    message, cycle_ms,
                    end_time_sec=end_time_sec,
                    end_count=end_count,
                    msg_size_bytes=msg_size,
                    pad_byte=pad_byte,
                ),
            )

        threading.Thread(target=_launch, daemon=True).start()

    def _script_tick(self) -> None:
        if not self.demo_running:
            self.script_timer_id = None
            return
        elapsed = time.time() - self.script_start_time
        for i, entry in enumerate(self.script_entries):
            if i not in self.script_executed_set and float(entry["time"]) <= elapsed:
                self.script_executed_set.add(i)
                threading.Thread(target=self._execute_script_entry, args=(entry,), daemon=True).start()
        self.script_timer_id = self.root.after(1000, self._script_tick)

    def _update_header_config(self) -> None:
        rt = int(self.run_stop_after_sec) if self.run_stop_after_sec > 0 else 0
        display_name = self.demo_name if self.demo_name else self.config_name
        self.header_config_var.set(
            f"Demo: {display_name}        UEs={self.ue_count}  gNBs={self.gnb_count}"
            f"  Runtime={rt}s  Iterations={self.run_iterations}"
        )

    def _update_run_header(self) -> None:
        iter_part = f"  Iteration={self.run_iteration}" if self.run_iterations > 1 else ""
        if self._resetting:
            self.header_run_var.set(f"Status: Resetting...{iter_part}")
        elif self.demo_running:
            elapsed = int(time.time() - self.demo_start_time)
            self.header_run_var.set(f"Status: Running{iter_part}  Elapsed={elapsed}s")
        elif self._demo_completed:
            self.header_run_var.set(f"Status: Completed{iter_part}")
        else:
            self.header_run_var.set("Status: Ready")

    def _tick_elapsed(self) -> None:
        if not self.demo_running:
            self.elapsed_timer_id = None
            return
        self._update_run_header()
        self.elapsed_timer_id = self.root.after(1000, self._tick_elapsed)

    def _key_for_config_node_name(self, node_name: str) -> Optional[str]:
        """Map a config-level node name (e.g. 'GNB2', 'UE1') to an internal key (e.g. 'gnb2', 'ue1').
        Uses the original config arrays, not the runtime-remapped node_names dict."""
        if not self.config:
            return None
        name_upper = node_name.upper()
        for i, gnb_cfg in enumerate(self.config.get("gnb", {}).get("gnbs", [])):
            if str(gnb_cfg.get("node", "")).upper() == name_upper and i < len(self.gnb_keys):
                return self.gnb_keys[i]
        ue_cfg = self.config.get("ue", {})
        base_node = str(ue_cfg.get("node", ""))
        m = re.search(r"^(.*?)(\d+)$", base_node)
        if m:
            prefix, start = m.group(1), int(m.group(2))
            for idx, key in enumerate(self.ue_keys):
                if f"{prefix}{start + idx}".upper() == name_upper:
                    return key
        else:
            for idx, key in enumerate(self.ue_keys):
                candidate = base_node if idx == 0 else f"{base_node}{idx + 1}"
                if candidate.upper() == name_upper:
                    return key
        return None

    def _execute_script_entry(self, entry: Dict[str, object]) -> None:
        node_name = str(entry["node"])
        command = str(entry["command"])
        args = entry.get("args", [])
        if not isinstance(args, list):
            args = [args]

        key = self._key_for_config_node_name(node_name)

        use_sudo = bool(key in self.ue_keys and self.ue_cli_with_sudo) if key else False
        cmd_str = command
        if args:
            args_strs = [shlex.quote(json.dumps(a, separators=(",", ":"))) if isinstance(a, (dict, list)) else str(a) for a in args]
            separator = " -- " if any(s.startswith("-") for s in args_strs) else " "
            cmd_str += separator + " ".join(args_strs)
        runtime_name = self.node_names.get(key, node_name) if key else node_name
        if not key:
            self.root.after(0, lambda: self._demo_log(f"[script] unknown node '{node_name}' > {cmd_str}: skipped"))
            return
        result = self._cli_exec_with_mode(runtime_name, cmd_str, use_sudo=use_sudo)
        if "error" in result:
            msg = f"[script] {node_name} > {cmd_str}: ERROR: {result['error']}"
        else:
            msg = f"[script] {node_name} > {cmd_str}: ok"
        self.root.after(0, lambda m=msg: self._demo_log(m))

    def _open_config_window(self) -> None:
        from tkinter import filedialog
        if self.config_window is not None and self.config_window.winfo_exists():
            self.config_window.lift()
            self.config_window.focus_set()
            return

        win = tk.Toplevel(self.root)
        win.title("Config Editor")
        win.geometry("720x800")
        win.resizable(True, True)
        self.config_window = win

        def _on_win_close() -> None:
            self.config_window = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", _on_win_close)

        if self.config is None:
            ttk.Label(win, text="No config loaded yet — use File > Load Config", font=("TkDefaultFont", 13)).pack(
                expand=True
            )
            ttk.Button(win, text="Close", command=_on_win_close).pack(pady=8)
            return

        # ── Notebook ──────────────────────────────────────────────────────────
        nb = ttk.Notebook(win)
        nb.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        # Store StringVar / BooleanVar refs by dotted path for Apply
        self._cfg_vars: Dict[str, object] = {}

        def _make_scroll_frame(parent: ttk.Widget) -> ttk.Frame:
            canvas = tk.Canvas(parent, borderwidth=0, highlightthickness=0)
            vsb = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
            canvas.configure(yscrollcommand=vsb.set)
            vsb.pack(side=tk.RIGHT, fill=tk.Y)
            canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            inner = ttk.Frame(canvas)
            inner_id = canvas.create_window((0, 0), window=inner, anchor="nw")

            def _on_resize(event: object) -> None:
                canvas.itemconfig(inner_id, width=canvas.winfo_width())

            def _on_frame_configure(event: object) -> None:
                canvas.configure(scrollregion=canvas.bbox("all"))

            canvas.bind("<Configure>", _on_resize)
            inner.bind("<Configure>", _on_frame_configure)
            return inner

        def _add_field(
            frame: ttk.Frame,
            row: int,
            label: str,
            path: str,
            value: object,
            browse: bool = False,
        ) -> int:
            ttk.Label(frame, text=label + ":").grid(row=row, column=0, sticky="w", padx=(4, 8), pady=3)
            if isinstance(value, bool):
                var: object = tk.BooleanVar(value=value)
                ttk.Checkbutton(frame, variable=var).grid(row=row, column=1, sticky="w", pady=3)
            elif isinstance(value, list):
                var = tk.StringVar(value=json.dumps(value))
                ttk.Entry(frame, textvariable=var, width=52).grid(row=row, column=1, columnspan=2 if browse else 1, sticky="ew", pady=3)
            else:
                var = tk.StringVar(value=str(value) if value is not None else "")
                ttk.Entry(frame, textvariable=var, width=52).grid(row=row, column=1, sticky="ew", pady=3)
                if browse:
                    def _browse(p: str = path, v: tk.StringVar = var) -> None:  # type: ignore[assignment]
                        chosen = filedialog.askopenfilename(title=f"Select {p}")
                        if chosen:
                            v.set(chosen)
                    ttk.Button(frame, text="…", width=3, command=_browse).grid(row=row, column=2, sticky="w", padx=(2, 0), pady=3)
            self._cfg_vars[path] = var
            frame.grid_columnconfigure(1, weight=1)
            return row + 1

        def _build_section_tab(section_label: str, section_key: str, fields: List[tuple]) -> None:
            tab = ttk.Frame(nb)
            nb.add(tab, text=section_label)
            inner = _make_scroll_frame(tab)
            section = self.config.get(section_key, {}) if section_key else self.config
            row = 0
            for field_key, default, browse in fields:
                val = section.get(field_key, default) if isinstance(section, dict) else default
                row = _add_field(inner, row, field_key, f"{section_key}.{field_key}" if section_key else field_key, val, browse=browse)

        def _add_section_header(inner: ttk.Frame, row: int, title: str, first: bool = False) -> int:
            if not first:
                ttk.Separator(inner, orient="horizontal").grid(
                    row=row, column=0, columnspan=3, sticky="ew", pady=(12, 4)
                )
                row += 1
            ttk.Label(inner, text=title, font=("TkDefaultFont", 10, "bold")).grid(
                row=row, column=0, columnspan=3, sticky="w", padx=4, pady=(0, 6)
            )
            return row + 1

        # ── Run tab (first) ───────────────────────────────────────────────────
        run_tab = ttk.Frame(nb)
        nb.add(run_tab, text="Run")
        run_inner = _make_scroll_frame(run_tab)
        row = 0
        row = _add_section_header(run_inner, row, "Demo", first=True)
        row = _add_field(run_inner, row, "demo_name", "demo_name", self.config.get("demo_name", ""))
        ttk.Label(run_inner, text="config_file:").grid(row=row, column=0, sticky="w", padx=(4, 8), pady=3)
        ttk.Label(run_inner, text=self.config_name, foreground="gray").grid(row=row, column=1, sticky="w", pady=3)
        row += 1
        row = _add_field(run_inner, row, "command_cli", "command_cli", self.config.get("command_cli", "./build/nr-cli"), browse=True)
        row = _add_section_header(run_inner, row, "Run")
        run_section = self.config.get("run", {})
        for field_key, default in [
            ("stop_after_sec", 0.0),
            ("iterations", 1),
            ("reset_core", False),
            ("reset_command", ""),
            ("reset_command_args", []),
            ("run_log", "demo_%i.log"),
        ]:
            row = _add_field(run_inner, row, field_key, f"run.{field_key}", run_section.get(field_key, default))
        row = _add_section_header(run_inner, row, "Logging")
        log_section = run_section.get("logging", {})
        for field_key, default in [
            ("log_dir", "./tools/UI/logs"),
            ("run_log_dir_name", "run-%t"),
        ]:
            row = _add_field(run_inner, row, field_key, f"run.logging.{field_key}", log_section.get(field_key, default))

        # ── UE tab ────────────────────────────────────────────────────────────
        _build_section_tab("UE", "ue", [
            ("node", "UE1", False), ("count", 1, False), ("ue_command", "./build/nr-ue", True),
            ("config", "", True), ("ue_log_file", "ue_%i.log", False),
            ("create_db_profiles", False, False),
            ("auto_setcap", True, False), ("run_with_sudo", True, False),
            ("sudo_non_interactive", False, False), ("cli_with_sudo", True, False), ("args", [], False),
        ])

        # ── GNBs tab ──────────────────────────────────────────────────────────
        gnbs_tab = ttk.Frame(nb)
        nb.add(gnbs_tab, text="GNBs")
        gnbs_inner = _make_scroll_frame(gnbs_tab)
        gnb_section = self.config.get("gnb", {})
        gnbs = gnb_section.get("gnbs", []) if isinstance(gnb_section, dict) else []
        row = 0
        row = _add_section_header(gnbs_inner, row, "GNB", first=True)
        row = _add_field(gnbs_inner, row, "gnb_command", "gnb.gnb_command", gnb_section.get("gnb_command", "./build/nr-gnb"), browse=True)
        for i, gnb_cfg in enumerate(gnbs):
            row = _add_section_header(gnbs_inner, row, f"GNB{i + 1}")
            for field_key, default, browse in [
                ("node", f"GNB{i+1}", False),
                ("config", "", True),
                ("gnb_log_file", f"gnb{i+1}_%i.log", False),
                ("args", [], False),
            ]:
                row = _add_field(gnbs_inner, row, field_key, f"gnb.gnbs.{i}.{field_key}", gnb_cfg.get(field_key, default), browse=browse)
            gnb_key = f"gnb{i + 1}"
            tle = self.gnb_tles.get(gnb_key)
            if tle:
                ttk.Separator(gnbs_inner, orient="horizontal").grid(
                    row=row, column=0, columnspan=3, sticky="ew", pady=(8, 4)
                )
                row += 1
                ttk.Label(gnbs_inner, text="Satellite TLE", font=("TkDefaultFont", 9, "bold")).grid(
                    row=row, column=0, columnspan=3, sticky="w", padx=4, pady=(0, 4)
                )
                row += 1
                for tle_label, tle_val in [("line1", tle[0]), ("line2", tle[1])]:
                    ttk.Label(gnbs_inner, text=tle_label + ":").grid(
                        row=row, column=0, sticky="w", padx=(4, 8), pady=2
                    )
                    ttk.Label(
                        gnbs_inner, text=tle_val,
                        font=("TkFixedFont", 8), foreground="#445566", anchor="w",
                    ).grid(row=row, column=1, columnspan=2, sticky="w", pady=2)
                    row += 1

        # ── Core Network tab ──────────────────────────────────────────────────
        core_tab = ttk.Frame(nb)
        nb.add(core_tab, text="Core Network")
        core_inner = _make_scroll_frame(core_tab)
        row = 0
        row = _add_section_header(core_inner, row, "AMF", first=True)
        amf_section = self.config.get("amf", {})
        for field_key, default, browse in [
            ("host", "127.0.0.1", False), ("port", 38412, False), ("protocol", "sctp", False),
            ("active_probe", False, False), ("connect_timeout_sec", 1.0, False),
            ("source_log_file", "", False), ("amf_log_file", "", False),
        ]:
            row = _add_field(core_inner, row, field_key, f"amf.{field_key}", amf_section.get(field_key, default), browse=browse)
        row = _add_section_header(core_inner, row, "Core DB")
        db_section = self.config.get("core_db", {})
        for field_key, default, browse in [
            ("warn_missing_subscribers", True, False), ("dbctl_script", "./tools/open5gs-dbctl.py", False),
            ("db_uri", "mongodb://localhost/open5gs", False), ("check_timeout_sec", 10.0, False), ("python", "python3", False),
        ]:
            row = _add_field(core_inner, row, field_key, f"core_db.{field_key}", db_section.get(field_key, default), browse=browse)

        # ── UI tab ────────────────────────────────────────────────────────────
        _build_section_tab("UI", "ui", [
            ("poll_interval_sec", 1.0, False),
            ("max_log_lines", 400, False),
            ("command_timeout_sec", 3.0, False),
        ])

        # ── User Plane tab ────────────────────────────────────────────────────
        up_tab = ttk.Frame(nb)
        nb.add(up_tab, text="User Plane")
        up_inner = _make_scroll_frame(up_tab)
        up_section = self.config.get("user_plane", {})
        up_row = 0
        for field_key, default, browse in [
            ("upf_ip", "127.0.0.1", False), ("upf_port", 5000, False),
            ("capture_enabled", True, False), ("capture_use_sudo", True, False),
            ("capture_sudo_non_interactive", True, False), ("move_tun_to_netns", False, False),
            ("netns_name", "ue1", False), ("netns_prefix_len", 16, False), ("max_log_lines", 400, False),
            ("ensure_host_route_to_ue", True, False), ("host_route_gateway", "172.22.0.1", False),
            ("host_route_subnet", "172.22.0.0/24", False),
        ]:
            val = up_section.get(field_key, default)
            up_row = _add_field(up_inner, up_row, field_key, f"user_plane.{field_key}", val, browse=browse)
        ttk.Separator(up_inner, orient="horizontal").grid(row=up_row, column=0, columnspan=3, sticky="ew", pady=(10, 4))
        up_row += 1
        ttk.Label(up_inner, text="User Plane Test", font=("TkDefaultFont", 10, "bold")).grid(
            row=up_row, column=0, columnspan=3, sticky="w", padx=4, pady=(0, 6)
        )
        up_row += 1
        upt_section = self.config.get("user_plane_test", {})
        for field_key, label, default in [
            ("enabled", "Enabled", False), ("ue_source", "UE source (name or 'all')", "UE1"),
            ("start_trigger", "Start trigger (tun-up / immediate)", "tun-up"),
            ("start_delay_sec", "Start delay after trigger (sec)", 0.0),
            ("cycle_time_sec", "Cycle time between sends (sec, 0=once)", 1.0),
            ("end_time_sec", "End after time (sec, 0=no limit)", 0.0),
            ("end_count", "End after count (0=no limit)", 0),
            ("message_text", "Message text", "Hello from demo"),
            ("message_size_bytes", "Message size bytes (0=no padding)", 0),
            ("pad_byte_value", "Pad byte value (hex ok, e.g. 0x32)", "0x20"),
        ]:
            up_row = _add_field(up_inner, up_row, label, f"user_plane_test.{field_key}", upt_section.get(field_key, default))


        # ── Packet Capture tab ────────────────────────────────────────────────
        pcap_tab = ttk.Frame(nb)
        nb.add(pcap_tab, text="Packet Capture")
        pcap_inner = _make_scroll_frame(pcap_tab)
        pcap_section = self.config.get("pcap", {})
        pcap_row = 0
        for field_key, label, default in [
            ("enabled", "enabled", False),
            ("interface", "interface (blank = auto-discover from AMF IP)", ""),
            ("output_file", "output_file", "trace.pcap"),
            ("use_sudo", "use_sudo", True),
            ("sudo_non_interactive", "sudo_non_interactive", True),
        ]:
            pcap_row = _add_field(pcap_inner, pcap_row, label, f"pcap.{field_key}", pcap_section.get(field_key, default))
        tk.Label(
            pcap_inner,
            text="Note: leaving 'interface' blank auto-discovers the interface\nby routing to the AMF IP address when the demo starts.",
            fg="gray", justify="left",
        ).grid(row=pcap_row, column=0, columnspan=3, sticky="w", padx=4, pady=(8, 2))

        # ── Script tab ────────────────────────────────────────────────────────
        script_tab = ttk.Frame(nb)
        nb.add(script_tab, text="Script")

        # Toolbar
        script_toolbar = ttk.Frame(script_tab)
        script_toolbar.pack(side=tk.TOP, fill=tk.X, padx=4, pady=(4, 2))

        # Treeview with scrollbar
        tree_frame = ttk.Frame(script_tab)
        tree_frame.pack(fill=tk.BOTH, expand=True, padx=4, pady=(0, 4))

        cols = ("time", "node", "command", "args")
        script_tree = ttk.Treeview(tree_frame, columns=cols, show="headings", selectmode="browse")
        script_tree.heading("time", text="Time (s)")
        script_tree.heading("node", text="Node")
        script_tree.heading("command", text="Command")
        script_tree.heading("args", text="Args")
        script_tree.column("time", width=80, anchor="center")
        script_tree.column("node", width=100, anchor="w")
        script_tree.column("command", width=150, anchor="w")
        script_tree.column("args", width=200, anchor="w")

        vsb_s = ttk.Scrollbar(tree_frame, orient="vertical", command=script_tree.yview)
        script_tree.configure(yscrollcommand=vsb_s.set)
        vsb_s.pack(side=tk.RIGHT, fill=tk.Y)
        script_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # Populate treeview from editor rows
        self._script_editor_rows = [dict(r) for r in self.script_entries]

        def _refresh_script_tree() -> None:
            script_tree.delete(*script_tree.get_children())
            for r in self._script_editor_rows:
                args_str = json.dumps(r["args"]) if r["args"] else "[]"
                script_tree.insert("", "end", values=(r["time"], r["node"], r["command"], args_str))

        _refresh_script_tree()

        def _open_row_dialog(prefill: Optional[Dict[str, object]] = None) -> Optional[Dict[str, object]]:
            dlg = tk.Toplevel(win)
            dlg.title("Edit Script Entry" if prefill else "Add Script Entry")
            dlg.geometry("360x200")
            dlg.resizable(False, False)
            dlg.grab_set()

            fields = [
                ("Time (s)", "time", str(prefill["time"]) if prefill else "1"),
                ("Node", "node", str(prefill["node"]) if prefill else ""),
                ("Command", "command", str(prefill["command"]) if prefill else ""),
                ("Args (JSON)", "args", json.dumps(prefill["args"]) if prefill else "[]"),
            ]
            vars_d: Dict[str, tk.StringVar] = {}
            for r_idx, (lbl, key, default) in enumerate(fields):
                ttk.Label(dlg, text=lbl + ":").grid(row=r_idx, column=0, sticky="w", padx=8, pady=4)
                v = tk.StringVar(value=default)
                ttk.Entry(dlg, textvariable=v, width=28).grid(row=r_idx, column=1, sticky="ew", padx=8, pady=4)
                vars_d[key] = v
            dlg.grid_columnconfigure(1, weight=1)

            result: List[Optional[Dict[str, object]]] = [None]

            def _ok() -> None:
                try:
                    t = float(vars_d["time"].get())
                except ValueError:
                    messagebox.showerror("Invalid", "Time must be a number.", parent=dlg)
                    return
                try:
                    args_val = json.loads(vars_d["args"].get() or "[]")
                    if not isinstance(args_val, list):
                        args_val = [args_val]
                except json.JSONDecodeError:
                    messagebox.showerror("Invalid", "Args must be a JSON array, e.g. [-90]", parent=dlg)
                    return
                result[0] = {
                    "time": t,
                    "node": vars_d["node"].get().strip(),
                    "command": vars_d["command"].get().strip(),
                    "args": args_val,
                }
                dlg.destroy()

            def _cancel() -> None:
                dlg.destroy()

            btn_row = ttk.Frame(dlg)
            btn_row.grid(row=len(fields), column=0, columnspan=2, pady=8)
            ttk.Button(btn_row, text="OK", command=_ok).pack(side=tk.LEFT, padx=6)
            ttk.Button(btn_row, text="Cancel", command=_cancel).pack(side=tk.LEFT, padx=6)
            dlg.wait_window()
            return result[0]

        def _add_row() -> None:
            row_data = _open_row_dialog()
            if row_data:
                self._script_editor_rows.append(row_data)
                self._script_editor_rows.sort(key=lambda r: float(r["time"]))
                _refresh_script_tree()

        def _edit_row() -> None:
            sel = script_tree.selection()
            if not sel:
                return
            idx = script_tree.index(sel[0])
            row_data = _open_row_dialog(prefill=self._script_editor_rows[idx])
            if row_data:
                self._script_editor_rows[idx] = row_data
                self._script_editor_rows.sort(key=lambda r: float(r["time"]))
                _refresh_script_tree()

        def _delete_row() -> None:
            sel = script_tree.selection()
            if not sel:
                return
            idx = script_tree.index(sel[0])
            del self._script_editor_rows[idx]
            _refresh_script_tree()

        def _move_row(delta: int) -> None:
            sel = script_tree.selection()
            if not sel:
                return
            idx = script_tree.index(sel[0])
            new_idx = idx + delta
            if new_idx < 0 or new_idx >= len(self._script_editor_rows):
                return
            rows = self._script_editor_rows
            rows[idx], rows[new_idx] = rows[new_idx], rows[idx]
            _refresh_script_tree()
            children = script_tree.get_children()
            if 0 <= new_idx < len(children):
                script_tree.selection_set(children[new_idx])
                script_tree.focus(children[new_idx])

        script_tree.bind("<Double-1>", lambda *_: _edit_row())

        ttk.Button(script_toolbar, text="Add", command=_add_row).pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(script_toolbar, text="Edit", command=_edit_row).pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(script_toolbar, text="Delete", command=_delete_row).pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(script_toolbar, text="▲ Up", command=lambda: _move_row(-1)).pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(script_toolbar, text="▼ Down", command=lambda: _move_row(1)).pack(side=tk.LEFT)

        # ── Bottom buttons ────────────────────────────────────────────────────
        btn_bar = ttk.Frame(win)
        btn_bar.pack(side=tk.BOTTOM, fill=tk.X, padx=8, pady=(0, 8))
        self._cfg_apply_btn = ttk.Button(btn_bar, text="Apply", command=self._apply_config_from_editor)
        self._cfg_apply_btn.pack(side=tk.LEFT, padx=(0, 6))
        if self.demo_running:
            self._cfg_apply_btn.config(state=tk.DISABLED)
        ttk.Button(btn_bar, text="Save to File", command=self._save_config_to_file).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(btn_bar, text="Close", command=_on_win_close).pack(side=tk.LEFT)

    def _populate_config_window(self) -> None:
        if not hasattr(self, "_cfg_vars") or self.config is None:
            return
        for path, var in self._cfg_vars.items():
            parts = path.split(".")
            try:
                if parts[0] == "gnb" and parts[1] == "gnbs" and len(parts) == 4:
                    val = self.config["gnb"]["gnbs"][int(parts[2])].get(parts[3], "")
                elif len(parts) == 3:
                    val = self.config.get(parts[0], {}).get(parts[1], {}).get(parts[2], "")
                elif len(parts) == 2:
                    val = self.config.get(parts[0], {}).get(parts[1], "")
                elif len(parts) == 1:
                    val = self.config.get(parts[0], "")
                else:
                    continue
                if isinstance(var, tk.BooleanVar):
                    var.set(bool(val))
                elif isinstance(var, tk.StringVar):
                    var.set(json.dumps(val) if isinstance(val, list) else str(val) if val is not None else "")
            except (KeyError, IndexError, TypeError):
                pass

    def _apply_config_from_editor(self) -> None:
        if self.demo_running or not hasattr(self, "_cfg_vars") or self.config is None:
            return
        new_cfg = copy.deepcopy(self.config)
        for path, var in self._cfg_vars.items():
            parts = path.split(".")
            raw = var.get() if isinstance(var, (tk.StringVar, tk.BooleanVar)) else var.get()
            try:
                if isinstance(var, tk.BooleanVar):
                    value: object = bool(raw)
                else:
                    try:
                        parsed = json.loads(raw)
                        value = parsed
                    except (json.JSONDecodeError, TypeError):
                        value = raw
                if parts[0] == "gnb" and parts[1] == "gnbs" and len(parts) == 4:
                    new_cfg["gnb"]["gnbs"][int(parts[2])][parts[3]] = value
                elif len(parts) == 3:
                    new_cfg.setdefault(parts[0], {}).setdefault(parts[1], {})[parts[2]] = value
                elif len(parts) == 2:
                    new_cfg.setdefault(parts[0], {})[parts[1]] = value
                elif len(parts) == 1:
                    new_cfg[parts[0]] = value
            except (IndexError, KeyError, TypeError):
                pass
        new_cfg["script"] = self._script_rows_to_json(self._script_editor_rows)
        try:
            validate_config(new_cfg)
        except ValueError as ex:
            messagebox.showerror("Config Validation Error", str(ex))
            return
        workspace = self.workspace
        self._apply_config(new_cfg, workspace)

    def _save_config_to_file(self) -> None:
        from tkinter import filedialog
        if self.config is None:
            return
        path = filedialog.asksaveasfilename(
            title="Save Config",
            defaultextension=".yaml",
            filetypes=[("YAML files", "*.yaml"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                yaml.safe_dump(self.config, f, default_flow_style=False, sort_keys=False, allow_unicode=True)
        except OSError as ex:
            messagebox.showerror("Save Error", str(ex))

    def _cli_exec(self, node: str, command: str) -> Dict[str, str]:
        return self._cli_exec_with_mode(node, command, use_sudo=False)

    def _disable_ue_cli_sudo(self, reason: str) -> None:
        if not self.ue_cli_with_sudo:
            return

        self.ue_cli_with_sudo = False
        if not self.ue_cli_sudo_disabled_logged:
            self.panes[self.primary_ue_key].append_log(
                "[priv] Disabled sudo nr-cli polling: " + reason + " (falling back to non-sudo polling)"
            )
            self.ue_cli_sudo_disabled_logged = True

    def _cli_exec_with_mode(self, node: str, command: str, use_sudo: bool) -> Dict[str, str]:
        nr_cli = self._resolve(self.nr_cli)
        cmd: List[str] = []
        if use_sudo:
            cmd.append("sudo")
            if self.ue_cli_sudo_non_interactive:
                cmd.append("-n")
        cmd.extend([nr_cli, node, "-e", command])

        try:
            proc = subprocess.run(
                cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=self.command_timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            if use_sudo:
                self._disable_ue_cli_sudo(f"timeout running {' '.join(cmd)}")
            return {"error": "command timed out"}

        if proc.returncode != 0:
            if use_sudo:
                err = proc.stderr.strip() or proc.stdout.strip() or "sudo nr-cli failed"
                if "password is required" in err or "a password is required" in err or "sudo:" in err:
                    self._disable_ue_cli_sudo(err)
            return {"error": proc.stderr.strip() or proc.stdout.strip() or "command failed"}
        return parse_simple_yaml(proc.stdout)

    def _list_runtime_nodes(self) -> List[str]:
        return self._list_runtime_nodes_with_mode(use_sudo=False)

    def _list_runtime_nodes_with_mode(self, use_sudo: bool) -> List[str]:
        nr_cli = self._resolve(self.nr_cli)
        cmd: List[str] = []
        if use_sudo:
            cmd.append("sudo")
            if self.ue_cli_sudo_non_interactive:
                cmd.append("-n")
        cmd.extend([nr_cli, "--dump"])

        try:
            proc = subprocess.run(
                cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=self.command_timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            if use_sudo:
                self._disable_ue_cli_sudo(f"timeout running {' '.join(cmd)}")
            return []

        if proc.returncode != 0:
            if use_sudo:
                err = proc.stderr.strip() or proc.stdout.strip() or "sudo nr-cli --dump failed"
                if "password is required" in err or "a password is required" in err or "sudo:" in err:
                    self._disable_ue_cli_sudo(err)
            return []
        return [line.strip() for line in proc.stdout.splitlines() if line.strip()]

    def _probe_nodes_by_status(self, nodes: List[str]) -> tuple[List[str], List[str]]:
        return self._probe_nodes_by_status_with_mode(nodes, use_sudo=False)

    def _probe_nodes_by_status_with_mode(
        self,
        nodes: List[str],
        use_sudo: bool,
    ) -> tuple[List[str], List[str]]:
        ue_nodes: List[str] = []
        gnb_nodes: List[str] = []

        for node in nodes:
            out = self._cli_exec_with_mode(node, "ui-status", use_sudo=use_sudo)
            if "error" in out:
                continue

            if "rrc-state" in out or "nas-state" in out:
                ue_nodes.append(node)
                continue

            if "nci" in out or "rrc-ue-count" in out or "ngap-ue-count" in out:
                gnb_nodes.append(node)

        ue_nodes.sort()
        gnb_nodes.sort()
        return ue_nodes, gnb_nodes

    def _resolve_runtime_node_names(self) -> None:
        user_nodes = self._list_runtime_nodes_with_mode(use_sudo=False)
        sudo_nodes: List[str] = []
        if self.ue_cli_with_sudo:
            sudo_nodes = self._list_runtime_nodes_with_mode(use_sudo=True)

        nodes = sorted(set(user_nodes) | set(sudo_nodes))
        if not nodes:
            return

        current = dict(self.node_names)
        resolved = dict(current)

        for key, name in current.items():
            if name in nodes:
                resolved[key] = name

        probed_ues_user, probed_gnbs = self._probe_nodes_by_status_with_mode(user_nodes, use_sudo=False)
        probed_ues_sudo: List[str] = []
        if self.ue_cli_with_sudo:
            probed_ues_sudo, _ = self._probe_nodes_by_status_with_mode(sudo_nodes, use_sudo=True)

        probed_ues = probed_ues_user if probed_ues_user else probed_ues_sudo

        for idx, key in enumerate(self.ue_keys):
            if current[key] not in nodes and idx < len(probed_ues):
                resolved[key] = probed_ues[idx]

        for i, gnb_key in enumerate(self.gnb_keys):
            if current.get(gnb_key) not in nodes and i < len(probed_gnbs):
                resolved[gnb_key] = probed_gnbs[i]

        for key in [*self.ue_keys, *self.gnb_keys]:
            if self.node_names.get(key) != resolved[key]:
                target_key = self.primary_ue_key if key in self.ue_keys else key
                self.panes[target_key].append_log(
                    f"CLI node mapped [{key}]: '{self.node_names.get(key)}' -> '{resolved[key]}'"
                )

        self.node_names = resolved

    def _record_cli_error(self, key: str, error: str) -> None:
        # Node names can differ at runtime; remapping may happen one poll later.
        # Suppress this known transient to avoid noisy panes at startup.
        if "No node found with name" in error:
            return

        last = self.last_cli_error.get(key)
        if last != error:
            target_key = self.primary_ue_key if key in self.ue_keys else key
            self.panes[target_key].append_log(f"cli poll error [{key}]: {error}")
            self.last_cli_error[key] = error

    def _poll_ue_status(self, key: str) -> Dict[str, str]:
        node = self.node_names[key]
        out = self._cli_exec_with_mode(node, "ui-status", use_sudo=self.ue_cli_with_sudo)
        if "error" in out:
            self._record_cli_error(key, out["error"])
            return {"RRC": "N/A", "NAS": "N/A", "PCI": "N/A", "dBm": "N/A"}

        self.last_cli_error.pop(key, None)
        return {
            "RRC": out.get("rrc-state", "N/A"),
            "NAS": out.get("nas-state", "N/A"),
            "PCI": out.get("connected-pci", "N/A"),
            "dBm": out.get("connected-dbm", "N/A"),
        }

    def _discover_primary_ue_tun_info(self) -> None:
        _, logs = self.panes[self.primary_ue_key].snapshot()
        imsi_hint = self._resolve_primary_ue_imsi_hint()
        scoped_pattern = re.compile(
            r"\[(\d+)\|app\].*TUN interface\[([^,\]]+),\s*([^\]]+)\] is up"
        )

        if imsi_hint:
            for line in reversed(logs):
                match = scoped_pattern.search(line)
                if not match:
                    continue
                if match.group(1) != imsi_hint:
                    continue

                self.primary_ue_tun_name = match.group(2).strip()
                self.primary_ue_tun_ip = match.group(3).strip()
                return

        # Fallback for logs without per-UE prefixes.
        pattern = re.compile(r"TUN interface\[([^,\]]+),\s*([^\]]+)\] is up")
        for line in reversed(logs):
            match = pattern.search(line)
            if not match:
                continue

            self.primary_ue_tun_name = match.group(1).strip()
            self.primary_ue_tun_ip = match.group(2).strip()
            return

    @staticmethod
    def _extract_printable_ascii(data: bytes) -> str:
        chars = [
            chr(byte)
            for byte in data
            if PRINTABLE_ASCII_MIN <= byte <= PRINTABLE_ASCII_MAX
        ]
        return "".join(chars)

    @staticmethod
    def _decode_transport_payload(packet: bytes) -> tuple[Optional[str], bytes]:
        def decode_from_offset(offset: int) -> tuple[Optional[str], bytes]:
            if len(packet) <= offset:
                return None, b""

            version = (packet[offset] >> 4) & 0x0F

            if version == 4:
                if len(packet) < offset + 20:
                    return None, b""

                ihl = (packet[offset] & 0x0F) * 4
                if ihl < 20 or len(packet) < offset + ihl:
                    return None, b""

                protocol = packet[offset + 9]
                dst_ip = socket.inet_ntoa(packet[offset + 16:offset + 20])
                transport_start = offset + ihl

                if protocol == 17 and len(packet) >= transport_start + 8:
                    return dst_ip, packet[transport_start + 8:]
                if protocol == 6 and len(packet) >= transport_start + 20:
                    tcp_hlen = ((packet[transport_start + 12] >> 4) & 0x0F) * 4
                    if tcp_hlen >= 20 and len(packet) >= transport_start + tcp_hlen:
                        return dst_ip, packet[transport_start + tcp_hlen:]
                return dst_ip, packet[transport_start:]

            if version == 6:
                if len(packet) < offset + 40:
                    return None, b""

                next_header = packet[offset + 6]
                dst_ip = socket.inet_ntop(socket.AF_INET6, packet[offset + 24:offset + 40])
                transport_start = offset + 40

                if next_header == 17 and len(packet) >= transport_start + 8:
                    return dst_ip, packet[transport_start + 8:]
                if next_header == 6 and len(packet) >= transport_start + 20:
                    tcp_hlen = ((packet[transport_start + 12] >> 4) & 0x0F) * 4
                    if tcp_hlen >= 20 and len(packet) >= transport_start + tcp_hlen:
                        return dst_ip, packet[transport_start + tcp_hlen:]
                return dst_ip, packet[transport_start:]

            return None, b""

        # TUN and AF_PACKET layouts vary by host; probe likely offsets.
        for offset in (0, 14, 16):
            dst_ip, payload = decode_from_offset(offset)
            if dst_ip is not None:
                return dst_ip, payload

        return None, b""

    def _append_user_plane_rx(self, line: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        with self.user_plane_lock:
            self.user_plane_rx_logs.append(f"[{timestamp}] {line}")

    def _append_user_plane_tx(self, line: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        with self.user_plane_lock:
            self.user_plane_tx_logs.append(f"[{timestamp}] {line}")

    def _set_user_plane_capture_status(self, status: str) -> None:
        self.user_plane_capture_status = status

    def _run_privileged_command(self, base_cmd: List[str]) -> subprocess.CompletedProcess[str]:
        cmd = list(base_cmd)
        if os.geteuid() != 0:
            cmd = ["sudo"]
            if self.user_plane_capture_sudo_non_interactive:
                cmd.append("-n")
            cmd.extend(base_cmd)

        return subprocess.run(
            cmd,
            cwd=str(self.workspace),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=self.command_timeout,
            check=False,
        )

    def _log_user_plane_netns_status(self, status: str) -> None:
        self.user_plane_netns_status = status
        if status == self.user_plane_last_netns_log:
            return

        self.user_plane_last_netns_log = status
        line = f"[netns] {status}"
        self._append_user_plane_tx(line)
        self.panes[self.primary_ue_key].append_log(line)

    def _ensure_user_plane_tun_namespace(self) -> None:
        if not self.user_plane_move_tun_to_netns:
            self.user_plane_netns_status = "disabled"
            return

        if self.user_plane_tun_moved_to_netns:
            self.user_plane_netns_status = f"moved to {self.user_plane_netns_name}"
            return

        if not self.primary_ue_tun_name or not self.primary_ue_tun_ip:
            self.user_plane_netns_status = "waiting for UE TUN"
            return

        ns_name = self.user_plane_netns_name
        tun_name = self.primary_ue_tun_name
        ue_ip = self.primary_ue_tun_ip
        prefix = self.user_plane_netns_prefix_len

        try:
            add_ns = self._run_privileged_command(["ip", "netns", "add", ns_name])
        except (FileNotFoundError, subprocess.TimeoutExpired) as ex:
            self._log_user_plane_netns_status(f"failed to create netns: {ex}")
            return

        if add_ns.returncode != 0 and "File exists" not in (add_ns.stderr + add_ns.stdout):
            err = add_ns.stderr.strip() or add_ns.stdout.strip() or "unknown"
            self._log_user_plane_netns_status(f"failed to create netns '{ns_name}': {err}")
            return

        try:
            move_if = self._run_privileged_command(["ip", "link", "set", tun_name, "netns", ns_name])
        except (FileNotFoundError, subprocess.TimeoutExpired) as ex:
            self._log_user_plane_netns_status(f"failed to move {tun_name}: {ex}")
            return

        if move_if.returncode != 0:
            err = move_if.stderr.strip() or move_if.stdout.strip() or "unknown"
            self._log_user_plane_netns_status(f"failed to move {tun_name}: {err}")
            return

        for cmd in (
            ["ip", "netns", "exec", ns_name, "ip", "link", "set", tun_name, "up"],
            [
                "ip",
                "netns",
                "exec",
                ns_name,
                "ip",
                "addr",
                "replace",
                f"{ue_ip}/{prefix}",
                "dev",
                tun_name,
            ],
        ):
            try:
                res = self._run_privileged_command(cmd)
            except (FileNotFoundError, subprocess.TimeoutExpired) as ex:
                self._log_user_plane_netns_status(f"failed netns config: {ex}")
                return

            if res.returncode != 0:
                err = res.stderr.strip() or res.stdout.strip() or "unknown"
                self._log_user_plane_netns_status(f"failed netns config: {err}")
                return

        self.user_plane_tun_moved_to_netns = True
        self._log_user_plane_netns_status(
            f"moved {tun_name} to {ns_name} with {ue_ip}/{prefix}"
        )

    def _start_user_plane_tcpdump_capture(self) -> None:
        if self.user_plane_tcpdump_thread is not None and self.user_plane_tcpdump_thread.is_alive():
            return

        if not self.primary_ue_tun_name:
            self._set_user_plane_capture_status("fallback waiting for UE #1 TUN")
            return

        cmd: List[str] = []
        if self.user_plane_capture_use_sudo:
            cmd.append("sudo")
            if self.user_plane_capture_sudo_non_interactive:
                cmd.append("-n")

        if self.user_plane_tun_moved_to_netns and self.user_plane_move_tun_to_netns:
            cmd.extend([
                "ip",
                "netns",
                "exec",
                self.user_plane_netns_name,
                "tcpdump",
                "-l",
                "-n",
                "-A",
                "-s",
                "0",
                "-i",
                self.primary_ue_tun_name,
            ])
        else:
            cmd.extend([
                "tcpdump",
                "-l",
                "-n",
                "-A",
                "-s",
                "0",
                "-i",
                self.primary_ue_tun_name,
            ])

        try:
            self.user_plane_tcpdump_proc = subprocess.Popen(
                cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except FileNotFoundError as ex:
            self._set_user_plane_capture_status("fallback error: tcpdump not found")
            self._append_user_plane_rx(f"capture fallback failed: {ex}")
            return
        except OSError as ex:
            self._set_user_plane_capture_status(f"fallback error: {ex}")
            self._append_user_plane_rx(f"capture fallback failed: {ex}")
            return

        self.user_plane_capture_backend = "tcpdump"
        self._set_user_plane_capture_status("running (tcpdump fallback)")
        self._append_user_plane_rx("capture switched to tcpdump fallback")

        def _reader() -> None:
            proc = self.user_plane_tcpdump_proc
            if proc is None or proc.stdout is None:
                self._set_user_plane_capture_status("fallback error: no stdout")
                return

            header_re = re.compile(r"^\s*\d{2}:\d{2}:\d{2}\.\d+\s+IP6?\s+")

            for line in proc.stdout:
                if self.user_plane_capture_stop.is_set():
                    break

                text = line.rstrip("\n")
                if not text:
                    continue

                if text.startswith("tcpdump:"):
                    self._append_user_plane_rx(text)
                    continue

                if header_re.match(text):
                    continue

                printable = self._extract_printable_ascii(text.encode("utf-8", errors="ignore"))
                if printable:
                    self._append_user_plane_rx(printable)

            code = proc.poll()
            if not self.user_plane_capture_stop.is_set() and code not in (None, 0):
                self._set_user_plane_capture_status(f"fallback exited ({code})")

        self.user_plane_tcpdump_thread = threading.Thread(target=_reader, daemon=True)
        self.user_plane_tcpdump_thread.start()

    def _log_user_plane_route_status(self, status: str) -> None:
        self.user_plane_route_status = status
        if status == self.user_plane_last_route_log:
            return

        self.user_plane_last_route_log = status
        line = f"[route] {status}"
        self._append_user_plane_tx(line)
        self.panes[self.primary_ue_key].append_log(line)

    def _check_host_route_to_ue(self, ue_ip: str) -> tuple[bool, str]:
        main_cmd = ["ip", "route", "show", "table", "main", f"{ue_ip}/32"]
        try:
            main_proc = subprocess.run(
                main_cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=self.command_timeout,
                check=False,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired) as ex:
            return False, f"route check failed: {ex}"

        if main_proc.returncode == 0:
            main_line = main_proc.stdout.splitlines()[0].strip() if main_proc.stdout else ""
            token = f"via {self.user_plane_host_route_gateway}"
            if main_line and token in main_line:
                return True, f"main-table: {main_line}"

        cmd = ["ip", "route", "get", ue_ip]
        try:
            proc = subprocess.run(
                cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=self.command_timeout,
                check=False,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired) as ex:
            return False, f"route check failed: {ex}"

        if proc.returncode != 0:
            err = proc.stderr.strip() or proc.stdout.strip() or "route lookup failed"
            return False, f"route check failed: {err}"

        route_line = proc.stdout.splitlines()[0].strip() if proc.stdout else ""
        token = f"via {self.user_plane_host_route_gateway}"
        if token in route_line:
            return True, route_line

        if main_proc.returncode == 0 and main_proc.stdout.strip():
            return False, f"main-table: {main_proc.stdout.splitlines()[0].strip()}"

        return False, route_line or "no route output"

    def _add_host_route_to_ue(self, ue_ip: str) -> tuple[bool, str]:
        add_cmd = [
            "ip",
            "route",
            "replace",
            f"{ue_ip}/32",
            "via",
            self.user_plane_host_route_gateway,
        ]

        cmd = list(add_cmd)
        if os.geteuid() != 0:
            cmd = ["sudo", "-n", *cmd]

        try:
            proc = subprocess.run(
                cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=self.command_timeout,
                check=False,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired) as ex:
            return False, f"route add failed: {ex}"

        if proc.returncode == 0:
            return True, "ok"

        err = proc.stderr.strip() or proc.stdout.strip() or "unknown error"
        if os.geteuid() != 0 and "password" in err.lower():
            manual_cmd = shlex.join(["sudo", *add_cmd])
            err = f"{err}. Run manually: {manual_cmd}"

        return False, f"route add failed: {err}"

    def _ensure_host_route_to_primary_ue(self) -> None:
        if not self.user_plane_auto_ensure_host_route:
            self.user_plane_route_status = "disabled"
            return

        ue_ip = self.primary_ue_tun_ip
        if not ue_ip:
            self.user_plane_route_status = "waiting for UE TUN IP"
            return

        now = time.monotonic()
        if (
            self.user_plane_route_last_check_ip == ue_ip
            and (now - self.user_plane_route_last_check_time) < 10.0
        ):
            return

        self.user_plane_route_last_check_ip = ue_ip
        self.user_plane_route_last_check_time = now

        subnet_cmd = ["ip", "route", "show", self.user_plane_host_route_subnet]
        try:
            subnet_proc = subprocess.run(
                subnet_cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=self.command_timeout,
                check=False,
            )
            if subnet_proc.returncode != 0 or not subnet_proc.stdout.strip():
                self._log_user_plane_route_status(
                    "warning: host subnet route "
                    f"{self.user_plane_host_route_subnet} not found"
                )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass

        ok, detail = self._check_host_route_to_ue(ue_ip)
        if ok:
            self._log_user_plane_route_status(
                f"ok: {ue_ip}/32 via {self.user_plane_host_route_gateway}"
            )
            return

        self._log_user_plane_route_status(
            f"missing for {ue_ip}; attempting add via {self.user_plane_host_route_gateway}"
        )

        added, add_err = self._add_host_route_to_ue(ue_ip)
        if not added:
            self._log_user_plane_route_status(add_err)
            return

        ok_after, detail_after = self._check_host_route_to_ue(ue_ip)
        if ok_after:
            self._log_user_plane_route_status(
                f"added: {ue_ip}/32 via {self.user_plane_host_route_gateway}"
            )
        else:
            self._log_user_plane_route_status(
                f"route add verification failed for {ue_ip}: {detail_after or detail}"
            )

    @staticmethod
    def _iface_exists(name: str) -> bool:
        return os.path.exists(f"/sys/class/net/{name}")

    def _user_plane_capture_loop(self) -> None:
        use_raw_socket = True  # downgraded to False permanently on first PermissionError

        while not self.user_plane_capture_stop.is_set():
            if not self.primary_ue_tun_name:
                self._set_user_plane_capture_status("waiting for UE #1 TUN")
                self.user_plane_capture_stop.wait(0.5)
                continue

            if not self._iface_exists(self.primary_ue_tun_name):
                self._set_user_plane_capture_status(
                    f"waiting for {self.primary_ue_tun_name} to appear on OS"
                )
                self.user_plane_capture_stop.wait(0.5)
                continue

            if self.user_plane_tun_moved_to_netns and self.user_plane_move_tun_to_netns:
                self._set_user_plane_capture_status("running (netns tcpdump)")
                self._start_user_plane_tcpdump_capture()
                # Block until tcpdump exits, then retry
                if self.user_plane_tcpdump_thread is not None:
                    self.user_plane_tcpdump_thread.join()
                self.user_plane_tcpdump_proc = None
                self.user_plane_tcpdump_thread = None
                if not self.user_plane_capture_stop.is_set():
                    self.user_plane_capture_stop.wait(2.0)
                continue

            if not use_raw_socket:
                self._start_user_plane_tcpdump_capture()
                if self.user_plane_tcpdump_thread is not None:
                    self.user_plane_tcpdump_thread.join()
                self.user_plane_tcpdump_proc = None
                self.user_plane_tcpdump_thread = None
                if not self.user_plane_capture_stop.is_set():
                    self.user_plane_capture_stop.wait(2.0)
                continue

            try:
                sock = socket.socket(
                    socket.AF_PACKET,
                    socket.SOCK_RAW,
                    socket.ntohs(0x0003),
                )
                sock.bind((self.primary_ue_tun_name, 0))
                sock.settimeout(0.5)
            except PermissionError:
                use_raw_socket = False
                self._set_user_plane_capture_status("permission denied, trying tcpdump fallback")
                self._append_user_plane_rx("capture error: permission denied for raw socket")
                continue
            except OSError as ex:
                self._set_user_plane_capture_status(f"error: {ex}")
                self._append_user_plane_rx(f"capture error: {ex}")
                self.user_plane_capture_stop.wait(1.0)
                continue

            self._set_user_plane_capture_status("running")
            self.user_plane_capture_backend = "raw-socket"
            self._append_user_plane_rx(
                f"capture started on {self.primary_ue_tun_name} (UE IP={self.primary_ue_tun_ip or 'unknown'})"
            )

            try:
                while not self.user_plane_capture_stop.is_set():
                    try:
                        packet = sock.recv(8192)
                    except socket.timeout:
                        continue
                    except OSError as ex:
                        self._set_user_plane_capture_status(f"read error: {ex}")
                        self._append_user_plane_rx(f"capture read error: {ex}")
                        break

                    dst_ip, payload = self._decode_transport_payload(packet)
                    if not payload:
                        continue

                    if self.primary_ue_tun_ip and dst_ip and dst_ip != self.primary_ue_tun_ip:
                        continue

                    printable = self._extract_printable_ascii(payload)
                    if printable:
                        self._append_user_plane_rx(printable)
            finally:
                sock.close()

    def _start_user_plane_capture(self) -> None:
        if not self.user_plane_capture_enabled:
            self._set_user_plane_capture_status("disabled")
            self._append_user_plane_rx("capture disabled by config")
            return

        if self.user_plane_capture_thread is not None and self.user_plane_capture_thread.is_alive():
            return

        self._set_user_plane_capture_status("starting")
        self.user_plane_capture_stop.clear()
        self.user_plane_capture_thread = threading.Thread(target=self._user_plane_capture_loop, daemon=True)
        self.user_plane_capture_thread.start()

    def _stop_user_plane_capture(self) -> None:
        self.user_plane_capture_stop.set()
        if self.user_plane_capture_thread is not None:
            self.user_plane_capture_thread.join(timeout=1.0)
            self.user_plane_capture_thread = None

        if self.user_plane_tcpdump_proc is not None:
            if self.user_plane_tcpdump_proc.poll() is None:
                self.user_plane_tcpdump_proc.terminate()
                try:
                    self.user_plane_tcpdump_proc.wait(timeout=1.0)
                except subprocess.TimeoutExpired:
                    self.user_plane_tcpdump_proc.kill()
            self.user_plane_tcpdump_proc = None

        if self.user_plane_tcpdump_thread is not None:
            self.user_plane_tcpdump_thread.join(timeout=1.0)
            self.user_plane_tcpdump_thread = None

        self.user_plane_capture_backend = "none"
        self._set_user_plane_capture_status("stopped")

    def _open_user_plane_demo_window(self) -> None:
        if self.user_plane_window is not None and self.user_plane_window.winfo_exists():
            self.user_plane_window.lift()
            self.user_plane_window.focus_set()
            return

        self._discover_primary_ue_tun_info()

        win = tk.Toplevel(self.root)
        win.title("UERANSIM User Plane Demo")
        win.geometry("900x520")
        win.resizable(True, True)

        header = ttk.Frame(win, padding=8)
        header.pack(side=tk.TOP, fill=tk.X)

        self.user_plane_header_var = tk.StringVar(value="")
        ttk.Label(header, textvariable=self.user_plane_header_var).pack(side=tk.LEFT)

        paned = ttk.Panedwindow(win, orient=tk.VERTICAL)
        paned.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=8, pady=(0, 8))

        top_frame = ttk.LabelFrame(
            paned,
            text="User Plane Payload Received by UE #1 (Printable ASCII)",
            padding=6,
        )
        bottom_frame = ttk.LabelFrame(
            paned,
            text="Source Data Generator Output",
            padding=6,
        )

        top_text = scrolledtext.ScrolledText(top_frame, wrap=tk.WORD, height=12)
        top_text.pack(fill=tk.BOTH, expand=True)
        top_text.configure(state=tk.DISABLED, font=("TkFixedFont", 10))

        bottom_text = scrolledtext.ScrolledText(bottom_frame, wrap=tk.WORD, height=12)
        bottom_text.pack(fill=tk.BOTH, expand=True)
        bottom_text.configure(state=tk.DISABLED, font=("TkFixedFont", 10))

        paned.add(top_frame, weight=1)
        paned.add(bottom_frame, weight=1)

        self.user_plane_window = win
        self.user_plane_rx_widget = top_text
        self.user_plane_tx_widget = bottom_text

        if self.demo_running:
            self._start_user_plane_capture()

        def _on_close() -> None:
            self.user_plane_window = None
            self.user_plane_rx_widget = None
            self.user_plane_tx_widget = None
            self.user_plane_header_var = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", _on_close)

    @staticmethod
    def _build_user_plane_payload(text: str, size_bytes: int, pad_byte: int) -> bytes:
        payload = text.encode("utf-8", errors="replace")
        if size_bytes > 0 and len(payload) < size_bytes:
            payload += bytes([pad_byte & 0xFF] * (size_bytes - len(payload)))
        return payload

    def _send_user_plane_text(self, text: str) -> None:
        payload = self._build_user_plane_payload(text, 0, 0x20)
        self._send_user_plane_payload(payload, text)

    def _send_user_plane_payload(self, payload: bytes, display_text: str) -> None:
        self._discover_primary_ue_tun_info()
        ue_ip = self.primary_ue_tun_ip

        if not ue_ip:
            self._append_user_plane_tx("send failed: UE #1 TUN IP is unknown")
            return

        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                udp.sendto(payload, (ue_ip, self.user_plane_upf_port))
        except OSError as ex:
            self._append_user_plane_tx(
                f"send failed to {ue_ip}:{self.user_plane_upf_port}: {ex}"
            )
            return

        self._append_user_plane_tx(
            f"sent {len(payload)}B to {ue_ip}:{self.user_plane_upf_port} "
            f"(route via {self.user_plane_host_route_gateway}) text={display_text!r}"
        )

    def _open_user_plane_send_dialog(self) -> None:
        self._open_user_plane_demo_window()

        dialog = tk.Toplevel(self.root)
        dialog.title("Send Text Message to UE #1")
        dialog.transient(self.root)
        dialog.grab_set()

        frm = ttk.Frame(dialog, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)

        self._discover_primary_ue_tun_info()
        ue_ip = self.primary_ue_tun_ip or "unknown"
        ttk.Label(
            frm,
            text=(
                f"Destination UE #1: {ue_ip}:{self.user_plane_upf_port} | "
                f"route via {self.user_plane_host_route_gateway}"
            ),
        ).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))

        ttk.Label(frm, text="Message:").grid(row=1, column=0, sticky="nw", pady=4)
        msg_text = tk.Text(frm, height=6, width=56, wrap=tk.WORD)
        msg_text.grid(row=1, column=1, sticky="nsew", pady=4)

        frm.grid_columnconfigure(1, weight=1)
        frm.grid_rowconfigure(1, weight=1)

        btns = ttk.Frame(frm)
        btns.grid(row=2, column=0, columnspan=2, sticky="e", pady=(10, 0))

        def _on_send() -> None:
            text = msg_text.get("1.0", tk.END).strip()
            if not text:
                messagebox.showerror("User Plane Demo", "Message cannot be empty")
                return

            self._send_user_plane_text(text)
            dialog.destroy()

        ttk.Button(btns, text="Send", command=_on_send).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(btns, text="Cancel", command=dialog.destroy).pack(side=tk.LEFT)

    def _open_user_plane_repeated_send_dialog(self) -> None:
        self._open_user_plane_demo_window()

        dialog = tk.Toplevel(self.root)
        dialog.title("Send Repeated Message to UE #1")
        dialog.transient(self.root)
        dialog.grab_set()

        frm = ttk.Frame(dialog, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)

        self._discover_primary_ue_tun_info()
        ue_ip = self.primary_ue_tun_ip or "unknown"
        ttk.Label(
            frm,
            text=(
                f"Destination UE #1: {ue_ip}:{self.user_plane_upf_port} | "
                f"route via {self.user_plane_host_route_gateway}"
            ),
        ).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))

        ttk.Label(frm, text="Message:").grid(row=1, column=0, sticky="nw", pady=4)
        msg_text = tk.Text(frm, height=6, width=56, wrap=tk.WORD)
        msg_text.grid(row=1, column=1, sticky="nsew", pady=4)

        ttk.Label(frm, text="Cycle (ms):").grid(row=2, column=0, sticky="w", pady=4)
        cycle_var = tk.StringVar(value="1000")
        ttk.Entry(frm, textvariable=cycle_var, width=16).grid(row=2, column=1, sticky="w", pady=4)

        frm.grid_columnconfigure(1, weight=1)
        frm.grid_rowconfigure(1, weight=1)

        btns = ttk.Frame(frm)
        btns.grid(row=3, column=0, columnspan=2, sticky="e", pady=(10, 0))

        def _on_start() -> None:
            text = msg_text.get("1.0", tk.END).strip()
            if not text:
                messagebox.showerror("User Plane Demo", "Message cannot be empty")
                return

            try:
                cycle_ms = int(cycle_var.get().strip())
                if cycle_ms <= 0:
                    raise ValueError("cycle")
            except ValueError:
                messagebox.showerror("User Plane Demo", "Cycle must be a positive integer (ms)")
                return

            self._start_user_plane_repeated_message(text, cycle_ms)
            dialog.destroy()

        ttk.Button(btns, text="Start", command=_on_start).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(btns, text="Cancel", command=dialog.destroy).pack(side=tk.LEFT)

    def _start_user_plane_repeated_message(
        self,
        message: str,
        cycle_ms: int,
        end_time_sec: float = 0.0,
        end_count: int = 0,
        msg_size_bytes: int = 0,
        pad_byte: int = 0x20,
    ) -> None:
        self._stop_user_plane_repeated_message()

        period_sec = cycle_ms / 1000.0 if cycle_ms > 0 else 0.0
        payload = self._build_user_plane_payload(message, msg_size_bytes, pad_byte)
        self.repeat_message_stop_event.clear()
        self.repeat_message_running = True
        self._append_user_plane_tx(
            f"sender started: cycle={cycle_ms}ms size={len(payload)}B "
            f"end_time={end_time_sec}s end_count={end_count} message={message!r}"
        )

        def _loop() -> None:
            start_t = time.time()
            count = 0
            while not self.repeat_message_stop_event.is_set():
                self._send_user_plane_payload(payload, message)
                count += 1

                if end_count > 0 and count >= end_count:
                    break
                if end_time_sec > 0 and (time.time() - start_t) >= end_time_sec:
                    break
                if period_sec <= 0:
                    break

                if self.repeat_message_stop_event.wait(period_sec):
                    break

            self.repeat_message_running = False
            self.root.after(0, lambda: self._append_user_plane_tx(
                f"sender stopped after {count} message(s)"
            ))

        self.repeat_message_thread = threading.Thread(target=_loop, daemon=True)
        self.repeat_message_thread.start()

    def _stop_user_plane_repeated_message(self) -> None:
        if not self.repeat_message_running and self.repeat_message_thread is None:
            return

        self.repeat_message_stop_event.set()
        if self.repeat_message_thread is not None:
            self.repeat_message_thread.join(timeout=1.0)
            self.repeat_message_thread = None

        self.repeat_message_running = False

    def _poll_gnb_status(self, key: str) -> Dict[str, str]:
        node = self.node_names[key]
        out = self._cli_exec(node, "ui-status")
        if "error" in out:
            self._record_cli_error(key, out["error"])
            return {
                "NCI": "N/A",
                "PCI": "N/A",
                "RRC UEs": "N/A",
                "NGAP UEs": "N/A",
                "NGAP Up": "N/A",
            }

        self.last_cli_error.pop(key, None)
        return {
            "NCI": out.get("nci", "N/A"),
            "PCI": out.get("pci", "N/A"),
            "RRC UEs": out.get("rrc-ue-count", "N/A"),
            "NGAP UEs": out.get("ngap-ue-count", "N/A"),
            "NGAP Up": out.get("ngap-up", "N/A").upper(),
        }

    def _derive_amf_from_gnb_ngap(self) -> tuple[bool, Optional[str]]:
        ngap_states = []
        for key in self.gnb_keys:
            scalars, _ = self.panes[key].snapshot()
            ngap_states.append(scalars.get("NGAP Up", "N/A"))
        if any(s == "TRUE" for s in ngap_states):
            return True, None
        if ngap_states and all(s == "FALSE" for s in ngap_states):
            return False, "derived from gNB NGAP state"
        return False, "awaiting gNB NGAP state"

    def _check_amf_reachability(
        self,
        host: str,
        port: int,
        timeout: float,
        protocol: str,
    ) -> tuple[bool, Optional[str]]:
        if protocol == "tcp":
            try:
                with socket.create_connection((host, port), timeout=timeout):
                    return True, None
            except OSError as ex:
                return False, str(ex)

        if protocol == "sctp":
            ipproto_sctp = getattr(socket, "IPPROTO_SCTP", None)
            if ipproto_sctp is None:
                return False, "socket.IPPROTO_SCTP not available in this Python build"

            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, ipproto_sctp)
            try:
                sock.settimeout(timeout)
                err = sock.connect_ex((host, port))
                if err == 0:
                    return True, None
                return False, os.strerror(err)
            except OSError as ex:
                return False, str(ex)
            finally:
                sock.close()

        return False, f"unsupported protocol '{protocol}' (use 'sctp' or 'tcp')"

    def _status_loop(self) -> None:
        self._resolve_runtime_node_names()
        while not self.stop_event.is_set():
            self._resolve_runtime_node_names()
            if self.stop_event.is_set():
                return
            self._discover_primary_ue_tun_info()
            self._ensure_user_plane_tun_namespace()
            self._ensure_host_route_to_primary_ue()
            for key in self.ue_keys:
                if self.stop_event.is_set():
                    return
                try:
                    ue_scalars = self._poll_ue_status(key)
                    ue_scalars["DB"] = self.ue_db_status.get(key, "UNKNOWN")
                    self.panes[key].set_scalars(ue_scalars)
                except Exception as ex:  # pylint: disable=broad-except
                    self.panes[key].append_log(f"status poll failed: {ex}")

            for gnb_key in self.gnb_keys:
                if self.stop_event.is_set():
                    return
                try:
                    self.panes[gnb_key].set_scalars(self._poll_gnb_status(gnb_key))
                except Exception as ex:  # pylint: disable=broad-except
                    self.panes[gnb_key].append_log(f"status poll failed: {ex}")

            self.stop_event.wait(self.poll_interval)

    def _amf_loop(self) -> None:
        amf = self.config["amf"]
        host = str(amf["host"])
        port = int(amf["port"])
        timeout = float(amf.get("connect_timeout_sec", 1.0))
        protocol = str(amf.get("protocol", "sctp")).strip().lower()
        active_probe = bool(amf.get("active_probe", protocol == "tcp"))

        if protocol == "sctp" and not active_probe:
            self.panes["amf"].append_log(
                "AMF probe is passive (SCTP active connect disabled to avoid phantom gNB sessions)"
            )

        while not self.stop_event.is_set():
            if active_probe:
                ok, error = self._check_amf_reachability(host, port, timeout, protocol)
            else:
                ok, error = self._derive_amf_from_gnb_ngap()

            if self.stop_event.is_set():
                return
            self.panes["amf"].set_scalars(
                {
                    "Host": host,
                    "Port": str(port),
                    "Proto": protocol.upper(),
                    "Probe": "ACTIVE" if active_probe else "PASSIVE",
                    "Reachable": "YES" if ok else "NO",
                    "Error": "-" if ok else (error or "unknown"),
                }
            )
            self.stop_event.wait(self.poll_interval)

    def _tail_amf_log_path(self) -> Optional[Path]:
        amf = self.config["amf"]
        raw = amf.get("source_log_file") or amf.get("log_file")
        if raw is None:
            return None
        value = str(raw).strip()
        if not value:
            return None
        return Path(self._resolve(value))

    def _amf_log_loop(self) -> None:
        log_path = self._tail_amf_log_path()
        if log_path is None:
            self.panes["amf"].append_log("AMF log tail disabled (amf.log_file is not set)")
            return

        self.panes["amf"].append_log(f"AMF log tail enabled: {log_path}")
        position = 0
        notified_missing = False

        while not self.stop_event.is_set():
            if not log_path.exists():
                if not notified_missing:
                    self.panes["amf"].append_log(f"AMF log file not found: {log_path}")
                    notified_missing = True
                self.stop_event.wait(self.poll_interval)
                continue

            if notified_missing:
                self.panes["amf"].append_log(f"AMF log file found: {log_path}")
                notified_missing = False

            if self.stop_event.is_set():
                return
            try:
                with log_path.open("r", encoding="utf-8", errors="replace") as f:
                    size = log_path.stat().st_size
                    if position > size:
                        position = 0
                    f.seek(position)
                    for line in f:
                        if self.stop_event.is_set():
                            return
                        self.panes["amf"].append_log("[AMF] " + line.rstrip("\n"))
                    position = f.tell()
            except OSError as ex:
                self.panes["amf"].append_log(f"AMF log read error: {ex}")

            self.stop_event.wait(self.poll_interval)

    @staticmethod
    def _discover_iface_for_ip(ip: str) -> Optional[str]:
        """Return the local interface name that routes to *ip*, or None on failure."""
        try:
            result = subprocess.run(
                ["ip", "route", "get", ip],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=3,
            )
            for token, nxt in zip(result.stdout.split(), result.stdout.split()[1:]):
                if token == "dev":
                    return nxt
        except Exception:
            pass
        return None

    def _start_pcap_capture(self) -> None:
        if not self.pcap_enabled:
            return

        iface = self.pcap_interface.strip()
        if not iface or iface.lower() == "amf":
            amf_ip = str(self.config.get("amf", {}).get("host", "")) if self.config else ""
            if amf_ip:
                discovered = self._discover_iface_for_ip(amf_ip)
                if discovered:
                    iface = discovered
                    self._demo_log(f"[pcap] auto-discovered interface {iface} for AMF IP {amf_ip}")
                else:
                    self._demo_log(f"[pcap] could not discover interface for AMF IP {amf_ip}; capture skipped")
                    return
            else:
                self._demo_log("[pcap] no interface configured and no AMF IP to discover from; capture skipped")
                return

        log_dir = self.run_log_dir if self.run_log_dir is not None else self.log_dir
        out_path = str((log_dir / self._expand_log_name(self.pcap_output_file)).resolve())

        cmd: List[str] = []
        if self.pcap_use_sudo:
            cmd.append("sudo")
            if self.pcap_sudo_non_interactive:
                cmd.append("-n")
        cmd.extend(["tcpdump", "-i", iface, "-w", out_path, "-U"])

        self._demo_log(f"[pcap] starting capture on {iface} -> {out_path}")
        self._demo_log("$ " + " ".join(cmd))

        self.pcap_stop_event.clear()
        try:
            self.pcap_proc = subprocess.Popen(
                cmd,
                cwd=str(self.workspace),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except (FileNotFoundError, OSError) as ex:
            self._demo_log(f"[pcap] failed to start tcpdump: {ex}")
            return

        def _reader() -> None:
            proc = self.pcap_proc
            if proc is None or proc.stdout is None:
                return
            for line in proc.stdout:
                self._demo_log("[pcap] " + line.rstrip())
            code = proc.wait()
            if not self.pcap_stop_event.is_set():
                self._demo_log(f"[pcap] tcpdump exited with code {code}")

        self.pcap_thread = threading.Thread(target=_reader, daemon=True)
        self.pcap_thread.start()

    def _stop_pcap_capture(self) -> None:
        self.pcap_stop_event.set()
        if self.pcap_proc is not None:
            if self.pcap_proc.poll() is None:
                self._demo_log("[pcap] stopping capture")
                self.pcap_proc.terminate()
                try:
                    self.pcap_proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.pcap_proc.kill()
            self.pcap_proc = None
        if self.pcap_thread is not None:
            self.pcap_thread.join(timeout=2.0)
            self.pcap_thread = None

    def _copy_amf_log(self) -> None:
        if not self.amf_log_file_template:
            return
        src_path = self._tail_amf_log_path()
        if src_path is None or not src_path.exists():
            if src_path is not None:
                self._demo_log(f"[amf-log] source not found, skipping copy: {src_path}")
            return
        log_dir = self.run_log_dir if self.run_log_dir is not None else self.log_dir
        dest_name = self._expand_log_name(self.amf_log_file_template)
        dest_path = log_dir / dest_name
        try:
            import shutil
            shutil.copy2(str(src_path), str(dest_path))
            self._demo_log(f"[amf-log] copied {src_path} -> {dest_path}")
        except OSError as ex:
            self._demo_log(f"[amf-log] copy failed: {ex}")

    def _start_processes(self) -> None:
        ue_cfg = self.config["ue"]
        nr_ue_path = self._resolve(self.nr_ue)

        self._warn_open5gs_subscribers(ue_cfg)

        self._prepare_ue_privileges(nr_ue_path)

        ue_cmd = [
            nr_ue_path,
            "-c",
            self._resolve(str(ue_cfg["config"])),
            *[str(x) for x in ue_cfg.get("args", [])],
        ]

        if self.ue_count > 1:
            ue_cmd.extend(["-n", str(self.ue_count)])

        if bool(ue_cfg.get("run_with_sudo", False)):
            sudo_non_interactive = bool(ue_cfg.get("sudo_non_interactive", False))
            if not sudo_non_interactive and not os.isatty(0):
                sudo_non_interactive = True
                self.panes[self.primary_ue_key].append_log(
                    "[priv] No TTY for interactive sudo; forcing non-interactive sudo (-n)"
                )

            sudo_cmd = ["sudo"]
            if sudo_non_interactive:
                sudo_cmd.append("-n")
            ue_cmd = [*sudo_cmd, *ue_cmd]

        self.processes["ue"] = ManagedProcess(
            self.panes[self.primary_ue_key],
            ue_cmd,
            self.workspace,
            self._process_log_path("ue"),
        )

        for idx, key in enumerate(self.gnb_keys):
            gnb_cfg = self.config["gnb"]["gnbs"][idx]
            gnb_cmd = [
                self._resolve(self.nr_gnb),
                "-c",
                self._resolve(str(gnb_cfg["config"])),
                *[str(x) for x in gnb_cfg.get("args", [])],
            ]
            self.processes[key] = ManagedProcess(
                self.panes[key],
                gnb_cmd,
                self.workspace,
                self._process_log_path(key),
            )

        for proc in self.processes.values():
            proc.start()

    def _inject_rsrp_value(self, key: str, dbm: int, source: str) -> None:
        dbm = max(MIN_RSRP, min(MAX_RSRP, dbm))

        target_ip = self.gnb_link_ips.get(key, "127.0.0.1")
        sti = random.getrandbits(64)
        packet = struct.pack(
            ">5BQIIi",
            0x03,
            CONS_MAJOR,
            CONS_MINOR,
            CONS_PATCH,
            RLS_MSG_GNB_RF_DATA,
            sti,
            0,
            0,
            dbm,
        )

        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                udp.sendto(packet, (target_ip, RLS_PORT))

            self.current_rsrp_dbm[key] = dbm
            self._sync_rsrp_current_vars()

            msg = f"Injected {dbm} dBm to {key} ({target_ip}:{RLS_PORT})"
            if source == "program":
                self.inject_status_var.set("Program running: " + msg)
                self.panes[key].append_log("[program] " + msg)
            else:
                self.inject_status_var.set(msg)
                self.panes[key].append_log("[inject] " + msg)
        except OSError as ex:
            msg = f"Injection failed: {ex}"
            self.inject_status_var.set(msg)
            tag = "[program] " if source == "program" else "[inject] "
            self.panes[key].append_log(tag + msg)

    def _prepare_ue_privileges(self, nr_ue_path: str) -> None:
        ue_cfg = self.config.get("ue", {})
        auto_setcap = bool(ue_cfg.get("auto_setcap", True))

        if not auto_setcap:
            return

        if os.geteuid() == 0:
            return

        getcap = subprocess.run(
            ["getcap", nr_ue_path],
            cwd=str(self.workspace),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=self.command_timeout,
            check=False,
        )

        if getcap.returncode == 0 and "cap_net_admin" in getcap.stdout:
            return

        setcap_cmd = ["sudo", "-n", "setcap", "cap_net_admin+ep", nr_ue_path]
        setcap = subprocess.run(
            setcap_cmd,
            cwd=str(self.workspace),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=self.command_timeout,
            check=False,
        )

        if setcap.returncode == 0:
            self.panes[self.primary_ue_key].append_log(
                "[priv] Granted cap_net_admin to nr-ue binary for TUN setup"
            )
            return

        err = setcap.stderr.strip() or setcap.stdout.strip() or "setcap failed"
        self.panes[self.primary_ue_key].append_log(
            f"[priv] Unable to grant cap_net_admin automatically: {err}"
        )
        self.panes[self.primary_ue_key].append_log(
            "[priv] Run once: " + shlex.join(["sudo", "setcap", "cap_net_admin+ep", nr_ue_path])
        )

    def _inject_rsrp(self) -> None:
        key = self.inject_target_var.get()
        raw_dbm = self.inject_dbm_var.get().strip()

        try:
            dbm = int(raw_dbm)
        except ValueError:
            self.inject_status_var.set("Invalid dBm value")
            return

        self._inject_rsrp_value(key, dbm, source="manual")

    @staticmethod
    def _format_current_rsrp(value: Optional[int]) -> str:
        if value is None:
            return "Unknown"
        return f"{value} dBm"

    def _sync_rsrp_current_vars(self) -> None:
        for key, var in self.rsrp_current_vars.items():
            var.set(self._format_current_rsrp(self.current_rsrp_dbm.get(key)))

    def _open_rsrp_window(self) -> None:
        if self.rsrp_window is not None and self.rsrp_window.winfo_exists():
            self.rsrp_window.lift()
            self.rsrp_window.focus_set()
            return

        dialog = tk.Toplevel(self.root)
        dialog.title("gNB RSRP Editor")
        dialog.geometry("760x260")
        dialog.resizable(True, False)

        frame = ttk.Frame(dialog, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(
            frame,
            text="View current gNB RSRP and inject new values. Current value is the last successful UI injection.",
        ).grid(row=0, column=0, columnspan=7, sticky="w", pady=(0, 10))

        headers = ["gNB", "Current RSRP", "New RSRP (dBm)", "Range", "Actions"]
        for idx, header in enumerate(headers):
            col = idx if idx < 4 else 6
            ttk.Label(frame, text=header).grid(row=1, column=col, sticky="w", padx=(0, 8), pady=(0, 4))

        row_map = {key: i + 2 for i, key in enumerate(self.gnb_keys)}
        new_rsrp_vars: Dict[str, tk.StringVar] = {}
        self.rsrp_current_vars = {}

        for key, row in row_map.items():
            self.rsrp_current_vars[key] = tk.StringVar(value=self._format_current_rsrp(self.current_rsrp_dbm.get(key)))
            current_var = self.rsrp_current_vars[key]

            default_value = self.current_rsrp_dbm.get(key)
            new_rsrp_vars[key] = tk.StringVar(value=str(default_value if default_value is not None else -80))

            ttk.Label(frame, text=key).grid(row=row, column=0, sticky="w", pady=4)
            ttk.Label(frame, textvariable=current_var, width=16).grid(row=row, column=1, sticky="w", pady=4)
            ttk.Entry(frame, textvariable=new_rsrp_vars[key], width=16).grid(row=row, column=2, sticky="ew", pady=4)
            ttk.Label(frame, text=f"[{MIN_RSRP}, {MAX_RSRP}] dBm").grid(row=row, column=3, sticky="w", pady=4)

        status_row = len(self.gnb_keys) + 2
        footer_row = status_row + 1
        status_var = tk.StringVar(value="Ready")
        ttk.Label(frame, textvariable=status_var).grid(row=status_row, column=0, columnspan=7, sticky="w", pady=(8, 0))

        def _apply_target(target_key: str) -> None:
            try:
                dbm = int(new_rsrp_vars[target_key].get().strip())
            except ValueError:
                status_var.set(f"Invalid numeric RSRP value for {target_key}")
                return

            self._inject_rsrp_value(target_key, dbm, source="manual")
            status_var.set(f"Applied {target_key} RSRP")
            self._sync_rsrp_current_vars()

        def _apply_all() -> None:
            for k in self.gnb_keys:
                _apply_target(k)

        for key, row in row_map.items():
            btn_row = ttk.Frame(frame)
            btn_row.grid(row=row, column=6, sticky="e", pady=4)
            ttk.Button(btn_row, text="Apply", command=lambda k=key: _apply_target(k)).pack(side=tk.LEFT)

        footer = ttk.Frame(frame)
        footer.grid(row=footer_row, column=0, columnspan=7, sticky="e", pady=(12, 0))
        ttk.Button(footer, text="Apply All", command=_apply_all).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(footer, text="Close", command=dialog.destroy).pack(side=tk.LEFT)

        frame.grid_columnconfigure(2, weight=1)

        self.rsrp_window = dialog
        self._sync_rsrp_current_vars()

        def _on_close() -> None:
            self.rsrp_window = None
            self.rsrp_current_vars = {}
            dialog.destroy()

        dialog.protocol("WM_DELETE_WINDOW", _on_close)

    @staticmethod
    def _format_cli_float(value: float) -> str:
        return f"{value:.12g}"

    @staticmethod
    def _ms_to_yyddd(timestamp_ms: float) -> str:
        """Format a Unix millisecond timestamp as YYDDD.ddddddd (day-of-year epoch)."""
        dt = datetime.datetime.fromtimestamp(timestamp_ms / 1000.0, tz=datetime.timezone.utc)
        yy  = dt.year % 100
        doy = dt.timetuple().tm_yday
        frac = (dt.hour * 3600 + dt.minute * 60 + dt.second + dt.microsecond / 1e6) / 86400.0
        frac = min(frac, 0.9999999)
        return f"{yy:02d}{doy:03d}" + f"{frac:.7f}"[1:]  # "0.ddddddd" → ".ddddddd"

    @staticmethod
    def _haversine_2d_distance_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
        radius_m = 6371000.0
        lat1_rad = math.radians(lat1)
        lon1_rad = math.radians(lon1)
        lat2_rad = math.radians(lat2)
        lon2_rad = math.radians(lon2)
        dlat = lat2_rad - lat1_rad
        dlon = lon2_rad - lon1_rad
        a = math.sin(dlat / 2.0) ** 2 + math.cos(lat1_rad) * math.cos(lat2_rad) * math.sin(dlon / 2.0) ** 2
        c = 2.0 * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))
        return radius_m * c

    @staticmethod
    def _wgs84_to_ecef(lat_deg: float, lon_deg: float, alt_m: float) -> tuple[float, float, float]:
        a = 6378137.0
        e2 = 6.69437999014e-3  # WGS-84 first eccentricity squared
        lat = math.radians(lat_deg)
        lon = math.radians(lon_deg)
        N = a / math.sqrt(1.0 - e2 * math.sin(lat) ** 2)
        return (
            (N + alt_m) * math.cos(lat) * math.cos(lon),
            (N + alt_m) * math.cos(lat) * math.sin(lon),
            (N * (1.0 - e2) + alt_m) * math.sin(lat),
        )

    @classmethod
    def _distance_3d_m(
        cls,
        lat1: float, lon1: float, alt1: float,
        lat2: float, lon2: float, alt2: float,
    ) -> float:
        x1, y1, z1 = cls._wgs84_to_ecef(lat1, lon1, alt1)
        x2, y2, z2 = cls._wgs84_to_ecef(lat2, lon2, alt2)
        return math.sqrt((x2 - x1) ** 2 + (y2 - y1) ** 2 + (z2 - z1) ** 2)

    @classmethod
    def _elevation_angle_deg(
        cls,
        ue_lat: float, ue_lon: float, ue_alt: float,
        tgt_lat: float, tgt_lon: float, tgt_alt: float,
    ) -> float:
        """Elevation angle (degrees) of target above the UE's local horizon, using ENU frame."""
        ux, uy, uz = cls._wgs84_to_ecef(ue_lat, ue_lon, ue_alt)
        tx, ty, tz = cls._wgs84_to_ecef(tgt_lat, tgt_lon, tgt_alt)
        dx, dy, dz = tx - ux, ty - uy, tz - uz
        lat = math.radians(ue_lat)
        lon = math.radians(ue_lon)
        east  = -math.sin(lon) * dx + math.cos(lon) * dy
        north = (-math.sin(lat) * math.cos(lon) * dx
                 - math.sin(lat) * math.sin(lon) * dy
                 + math.cos(lat) * dz)
        up    = (math.cos(lat) * math.cos(lon) * dx
                 + math.cos(lat) * math.sin(lon) * dy
                 + math.sin(lat) * dz)
        horiz = math.sqrt(east ** 2 + north ** 2)
        return math.degrees(math.atan2(up, horiz))

    @classmethod
    def _azimuth_deg(
        cls,
        ue_lat: float, ue_lon: float, ue_alt: float,
        tgt_lat: float, tgt_lon: float, tgt_alt: float,
    ) -> float:
        """Bearing (degrees, 0=N 90=E) from UE to target using ENU frame."""
        ux, uy, uz = cls._wgs84_to_ecef(ue_lat, ue_lon, ue_alt)
        tx, ty, tz = cls._wgs84_to_ecef(tgt_lat, tgt_lon, tgt_alt)
        dx, dy, dz = tx - ux, ty - uy, tz - uz
        lat = math.radians(ue_lat)
        lon = math.radians(ue_lon)
        east  = -math.sin(lon) * dx + math.cos(lon) * dy
        north = (-math.sin(lat) * math.cos(lon) * dx
                 - math.sin(lat) * math.sin(lon) * dy
                 + math.cos(lat) * dz)
        return math.degrees(math.atan2(east, north)) % 360.0

    @staticmethod
    def _fspl_db(dist_m: float, freq_hz: float) -> float:
        """Free-space path loss (dB) — mirrors FreeSpacePathLossDb in pos_sim.cpp."""
        if dist_m <= 0:
            dist_m = 1.0
        return 20.0 * math.log10(dist_m) + 20.0 * math.log10(freq_hz) - 147.55

    @staticmethod
    def _uma_pl_db(dist_m: float, freq_hz: float) -> float:
        """Urban-macro path loss (dB) per 3GPP TS 38.901 — mirrors UrbanMacroPathLossDb in pos_sim.cpp."""
        if dist_m <= 0:
            dist_m = 1.0
        freq_ghz = freq_hz / 1e9
        if dist_m < 1600.0:
            return 28.0 + 22.0 * math.log10(dist_m) + 20.0 * math.log10(freq_ghz)
        return 28.0 + 40.0 * math.log10(dist_m) + 20.0 * math.log10(freq_ghz) - 9.0 * math.log10(1600.0 ** 2)

    def _read_gnb_rf_config(self, idx: int) -> Dict[str, object]:
        """Parse rfLink and ntn sections from a gNB YAML config file."""
        defaults: Dict[str, object] = {
            "carrFrequencyHz": 3.5e9,
            "txPowerDbm": 15.0,
            "txGainDbi": 15.0,
            "ueRxGainDbi": 0.0,
            "isSatellite": False,
        }
        try:
            path = Path(self._resolve(str(self.config["gnb"]["gnbs"][idx]["config"])))
            if not path.exists():
                return defaults
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

            def _read_section(section: str) -> Dict[str, str]:
                result: Dict[str, str] = {}
                in_sec = False
                sec_re = re.compile(r"^" + re.escape(section) + r"\s*:")
                kv_re = re.compile(r"^[ \t]{2,}(\w+)\s*:\s*([^#\n]*)")
                for ln in lines:
                    if sec_re.match(ln):
                        in_sec = True
                        continue
                    if in_sec:
                        stripped = ln.strip()
                        if stripped and not ln[0].isspace():
                            break
                        m = kv_re.match(ln)
                        if m:
                            result[m.group(1).strip()] = m.group(2).strip()
                return result

            rf = _read_section("rfLink")
            ntn = _read_section("ntn")
            pos = _read_section("position")

            alt = float(pos.get("altitude", 0.0))
            is_sat = ntn.get("ntnEnabled", "false").lower() == "true" or alt > 10_000.0

            return {
                "carrFrequencyHz": float(rf.get("carrFrequencyHz", defaults["carrFrequencyHz"])),
                "txPowerDbm": float(rf.get("txPowerDbm", defaults["txPowerDbm"])),
                "txGainDbi": float(rf.get("txGainDbi", defaults["txGainDbi"])),
                "ueRxGainDbi": float(rf.get("ueRxGainDbi", defaults["ueRxGainDbi"])),
                "isSatellite": is_sat,
            }
        except Exception:
            return defaults

    def _cli_exec_for_location_target(self, target_key: str, command: str) -> Dict[str, str]:
        node = self.node_names.get(target_key)
        if not node:
            raise RuntimeError(f"Unknown target '{target_key}'")

        if target_key in self.ue_keys:
            return self._cli_exec_with_mode(node, command, use_sudo=self.ue_cli_with_sudo)
        return self._cli_exec(node, command)

    def _propagate_tle_to_wgs84(self, line1: str, line2: str, epoch_ms: Optional[float] = None) -> Optional[Dict[str, float]]:
        """Propagate a TLE to the given Unix epoch (ms) or current UTC; return WGS84 lat/lon/alt (m)."""
        if not _SGP4_AVAILABLE:
            return None

        try:
            sat = _Satrec.twoline2rv(line1, line2)
            if epoch_ms is not None:
                unix_sec = epoch_ms / 1000.0
                jd_total = 2440587.5 + unix_sec / 86400.0
                jd = math.floor(jd_total)
                fr = jd_total - jd
            else:
                now = datetime.datetime.now(datetime.timezone.utc)
                jd, fr = _sgp4_jday(now.year, now.month, now.day, now.hour, now.minute,
                              now.second + now.microsecond / 1e6)
            error_code, r, _ = sat.sgp4(jd, fr)
            if error_code != 0 or r is None:
                return None

            x_teme, y_teme, z_teme = r  # km, TEME frame

            # Rotate TEME → ECEF using Greenwich Mean Sidereal Time
            jd_total = jd + fr
            theta_deg = 280.46061837 + 360.98564736629 * (jd_total - 2451545.0)
            theta = math.radians(theta_deg % 360.0)
            cos_t = math.cos(theta)
            sin_t = math.sin(theta)
            x_ecef = cos_t * x_teme + sin_t * y_teme
            y_ecef = -sin_t * x_teme + cos_t * y_teme
            z_ecef = z_teme  # km

            # ECEF (km) → WGS84 geodetic via iterative Bowring method
            x_m, y_m, z_m = x_ecef * 1000.0, y_ecef * 1000.0, z_ecef * 1000.0
            a = 6378137.0
            f = 1.0 / 298.257223563
            e2 = 2.0 * f - f * f
            lon_rad = math.atan2(y_m, x_m)
            p = math.sqrt(x_m * x_m + y_m * y_m)
            lat_rad = math.atan2(z_m, p * (1.0 - e2))
            for _ in range(10):
                N = a / math.sqrt(1.0 - e2 * math.sin(lat_rad) ** 2)
                lat_new = math.atan2(z_m + e2 * N * math.sin(lat_rad), p)
                if abs(lat_new - lat_rad) < 1e-12:
                    lat_rad = lat_new
                    break
                lat_rad = lat_new
            N = a / math.sqrt(1.0 - e2 * math.sin(lat_rad) ** 2)
            if abs(math.cos(lat_rad)) > 1e-10:
                alt_m = p / math.cos(lat_rad) - N
            else:
                alt_m = abs(z_m) / abs(math.sin(lat_rad)) - N * (1.0 - e2)

            return {
                "latitude": math.degrees(lat_rad),
                "longitude": math.degrees(lon_rad),
                "altitude": alt_m,
                "timestampMs": float(int(time.time() * 1000)),
            }
        except Exception as _tle_ex:
            import traceback as _tb, sys as _sys
            print(f"[TLE propagation error] {_tle_ex}", file=_sys.stderr, flush=True)
            _tb.print_exc(file=_sys.stderr)
            return None

    def _fetch_location_wgs84(self, target_key: str) -> Dict[str, float]:
        node = self.node_names.get(target_key)
        if not node:
            raise RuntimeError(f"Unknown target '{target_key}'")

        out = self._cli_exec_for_location_target(target_key, "get-loc-wgs84")
        if "error" in out:
            raise RuntimeError(f"get-loc-wgs84 failed for {target_key} ({node}): {out['error']}")

        try:
            latitude = float(out["latitude"])
            longitude = float(out["longitude"])
            altitude = float(out["altitude"])
        except KeyError as ex:
            raise RuntimeError(f"Missing field in get-loc-wgs84 output: {ex}") from ex
        except ValueError as ex:
            raise RuntimeError(f"Invalid get-loc-wgs84 numeric value: {ex}") from ex

        result: Dict[str, float] = {"latitude": latitude, "longitude": longitude, "altitude": altitude}
        if "timestampMs" in out:
            try:
                result["timestampMs"] = float(out["timestampMs"])
            except ValueError:
                pass
        return result

    def _set_location_wgs84(
        self,
        target_key: str,
        latitude: float,
        longitude: float,
        altitude: float,
    ) -> None:
        node = self.node_names.get(target_key)
        if not node:
            raise RuntimeError(f"Unknown target '{target_key}'")

        arg = ":".join(
            [
                self._format_cli_float(latitude),
                self._format_cli_float(longitude),
                self._format_cli_float(altitude),
            ]
        )
        out = self._cli_exec_for_location_target(target_key, f"set-loc-wgs84 {arg}")
        if "error" in out:
            raise RuntimeError(f"set-loc-wgs84 failed for {target_key} ({node}): {out['error']}")

        msg = (
            f"Updated {target_key} WGS84 to lat={self._format_cli_float(latitude)}, "
            f"lon={self._format_cli_float(longitude)}, alt={self._format_cli_float(altitude)}"
        )
        self.inject_status_var.set(msg)
        self.panes[target_key].append_log("[set-loc-wgs84] " + msg)

    def _open_location_window(self) -> None:
        if self.location_window is not None and self.location_window.winfo_exists():
            self.location_window.lift()
            self.location_window.focus_set()
            return

        dialog = tk.Toplevel(self.root)
        dialog.title("Node WGS84 Location Editor")
        dialog.geometry("1100x560")
        dialog.resizable(True, True)

        frame = ttk.Frame(dialog, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)

        gnb_list = ", ".join(self.gnb_keys) if self.gnb_keys else "gNBs"
        ttk.Label(
            frame,
            text=(
                f"View/edit UE1, {gnb_list} locations in WGS84. "
                "Shows 3D distance, elevation angle, and predicted Rx power from UE1 to each gNB."
            ),
        ).grid(row=0, column=0, columnspan=8, sticky="w", pady=(0, 10))

        headers = ["Node", "Current Location", "Latitude", "Longitude", "Altitude (m)", "Actions"]
        for idx, header in enumerate(headers):
            col = idx if idx < 5 else 7
            ttk.Label(frame, text=header).grid(row=1, column=col, sticky="w", padx=(0, 8), pady=(0, 4))

        editable_keys = [self.primary_ue_key, *self.gnb_keys]
        row_map = {key: i + 2 for i, key in enumerate(editable_keys)}
        current_vars: Dict[str, tk.StringVar] = {}
        lat_vars: Dict[str, tk.StringVar] = {}
        lon_vars: Dict[str, tk.StringVar] = {}
        alt_vars: Dict[str, tk.StringVar] = {}

        for key in editable_keys:
            row = row_map[key]
            node = self.node_names.get(key, "unknown")
            current_vars[key] = tk.StringVar(value="Not loaded")
            lat_vars[key] = tk.StringVar(value="")
            lon_vars[key] = tk.StringVar(value="")
            alt_vars[key] = tk.StringVar(value="")

            ttk.Label(frame, text=f"{key} ({node})").grid(row=row, column=0, sticky="w", pady=4)
            ttk.Label(frame, textvariable=current_vars[key], width=32).grid(row=row, column=1, sticky="w", pady=4)
            ttk.Entry(frame, textvariable=lat_vars[key], width=16).grid(row=row, column=2, sticky="ew", pady=4)
            ttk.Entry(frame, textvariable=lon_vars[key], width=16).grid(row=row, column=3, sticky="ew", pady=4)
            ttk.Entry(frame, textvariable=alt_vars[key], width=16).grid(row=row, column=4, sticky="ew", pady=4)

        # ── RF parameter section (per gNB, local only — not sent to nodes) ──
        rf_sep_row = len(editable_keys) + 2
        ttk.Separator(frame, orient=tk.HORIZONTAL).grid(
            row=rf_sep_row, column=0, columnspan=8, sticky="ew", pady=(10, 4)
        )
        ttk.Label(frame, text="Signal Prediction Parameters  (local only — not sent to nodes)").grid(
            row=rf_sep_row + 1, column=0, columnspan=8, sticky="w", pady=(0, 4)
        )

        rf_hdr_row = rf_sep_row + 2
        for col_idx, hdr in enumerate(
            ["gNB", "Carrier Freq (Hz)", "Tx Power (dBm)", "Tx Gain (dBi)", "UE Rx Gain (dBi)", "Link Type"]
        ):
            ttk.Label(frame, text=hdr).grid(row=rf_hdr_row, column=col_idx, sticky="w", padx=(0, 6), pady=(0, 2))

        rf_freq_vars: Dict[str, tk.StringVar] = {}
        rf_txpwr_vars: Dict[str, tk.StringVar] = {}
        rf_txgain_vars: Dict[str, tk.StringVar] = {}
        rf_rxgain_vars: Dict[str, tk.StringVar] = {}
        rf_type_vars: Dict[str, tk.StringVar] = {}

        for i, gnb_key in enumerate(self.gnb_keys):
            rf_cfg = self._read_gnb_rf_config(i)
            row = rf_hdr_row + 1 + i
            rf_freq_vars[gnb_key] = tk.StringVar(value=str(rf_cfg["carrFrequencyHz"]))
            rf_txpwr_vars[gnb_key] = tk.StringVar(value=str(rf_cfg["txPowerDbm"]))
            rf_txgain_vars[gnb_key] = tk.StringVar(value=str(rf_cfg["txGainDbi"]))
            rf_rxgain_vars[gnb_key] = tk.StringVar(value=str(rf_cfg["ueRxGainDbi"]))
            rf_type_vars[gnb_key] = tk.StringVar(
                value="Satellite" if rf_cfg["isSatellite"] else "Terrestrial"
            )

            ttk.Label(frame, text=gnb_key).grid(row=row, column=0, sticky="w", pady=2)
            ttk.Entry(frame, textvariable=rf_freq_vars[gnb_key], width=14).grid(
                row=row, column=1, sticky="ew", pady=2, padx=(0, 6)
            )
            ttk.Entry(frame, textvariable=rf_txpwr_vars[gnb_key], width=10).grid(
                row=row, column=2, sticky="ew", pady=2, padx=(0, 6)
            )
            ttk.Entry(frame, textvariable=rf_txgain_vars[gnb_key], width=10).grid(
                row=row, column=3, sticky="ew", pady=2, padx=(0, 6)
            )
            ttk.Entry(frame, textvariable=rf_rxgain_vars[gnb_key], width=10).grid(
                row=row, column=4, sticky="ew", pady=2, padx=(0, 6)
            )
            ttk.Combobox(
                frame,
                textvariable=rf_type_vars[gnb_key],
                values=["Terrestrial", "Satellite"],
                width=12,
                state="readonly",
            ).grid(row=row, column=5, sticky="w", pady=2)

        # ── Distance / elevation / Prx display ──
        dist_row_start = rf_hdr_row + 1 + len(self.gnb_keys) + 1
        dist_vars: Dict[str, tk.StringVar] = {}
        for i, gnb_key in enumerate(self.gnb_keys):
            var = tk.StringVar(value=f"UE1 → {gnb_key}:  2D=N/A  |  3D=N/A  |  elev=N/A  |  Prx=N/A")
            dist_vars[gnb_key] = var
            ttk.Label(frame, textvariable=var).grid(
                row=dist_row_start + i, column=0, columnspan=8, sticky="w", pady=(8 if i == 0 else 2, 0)
            )

        status_row = dist_row_start + len(self.gnb_keys)
        footer_row = status_row + 1
        status_var = tk.StringVar(value="Ready")
        ttk.Label(frame, textvariable=status_var).grid(row=status_row, column=0, columnspan=8, sticky="w", pady=(8, 0))

        def _location_text(latitude: float, longitude: float, altitude: float) -> str:
            return (
                f"lat={self._format_cli_float(latitude)}, "
                f"lon={self._format_cli_float(longitude)}, "
                f"alt={self._format_cli_float(altitude)}"
            )

        def _refresh_target(target_key: str) -> None:
            try:
                location = self._fetch_location_wgs84(target_key)
            except RuntimeError as ex:
                current_vars[target_key].set("<unavailable>")
                status_var.set(str(ex))
                pane_key = self.primary_ue_key if target_key in self.ue_keys else target_key
                self.panes[pane_key].append_log("[get-loc-wgs84] " + str(ex))
                return

            lat = location["latitude"]
            lon = location["longitude"]
            alt = location["altitude"]
            current_vars[target_key].set(_location_text(lat, lon, alt))
            lat_vars[target_key].set(self._format_cli_float(lat))
            lon_vars[target_key].set(self._format_cli_float(lon))
            alt_vars[target_key].set(self._format_cli_float(alt))
            status_var.set(f"Loaded {target_key} location")
            _update_distance_preview()

        def _refresh_all() -> None:
            for key in editable_keys:
                _refresh_target(key)

        def _apply_target(target_key: str) -> None:
            try:
                latitude = float(lat_vars[target_key].get().strip())
                longitude = float(lon_vars[target_key].get().strip())
                altitude = float(alt_vars[target_key].get().strip())
            except ValueError:
                status_var.set(f"Invalid numeric value for {target_key}")
                return

            try:
                self._set_location_wgs84(target_key, latitude, longitude, altitude)
            except RuntimeError as ex:
                status_var.set(str(ex))
                pane_key = self.primary_ue_key if target_key in self.ue_keys else target_key
                self.panes[pane_key].append_log("[set-loc-wgs84] " + str(ex))
                return

            current_vars[target_key].set(_location_text(latitude, longitude, altitude))
            status_var.set(f"Updated {target_key} location")
            _update_distance_preview()

        def _apply_all() -> None:
            for key in editable_keys:
                _apply_target(key)

        def _parse_lat_lon_alt(key: str) -> Optional[tuple[float, float, float]]:
            try:
                return (
                    float(lat_vars[key].get().strip()),
                    float(lon_vars[key].get().strip()),
                    float(alt_vars[key].get().strip()),
                )
            except ValueError:
                return None

        def _format_dist(d: float) -> str:
            return f"{d:,.1f} m" if d < 1_000_000 else f"{d / 1000:,.1f} km"

        def _compute_prx(gnb_key: str, d3: float) -> str:
            try:
                freq_hz = float(rf_freq_vars[gnb_key].get().strip())
                tx_pwr = float(rf_txpwr_vars[gnb_key].get().strip())
                tx_gain = float(rf_txgain_vars[gnb_key].get().strip())
                rx_gain = float(rf_rxgain_vars[gnb_key].get().strip())
                is_sat = rf_type_vars[gnb_key].get() == "Satellite"
                if is_sat:
                    pl = self._fspl_db(d3, freq_hz)
                    prx = tx_pwr + tx_gain + rx_gain - pl - 1.0
                else:
                    pl = self._uma_pl_db(d3, freq_hz)
                    prx = tx_pwr + tx_gain + rx_gain - pl
                return f"{prx:.1f} dBm"
            except (ValueError, ZeroDivisionError):
                return "error"

        def _update_distance_preview(*_args: object) -> None:
            ue = _parse_lat_lon_alt(self.primary_ue_key)
            for gnb_key in self.gnb_keys:
                g = _parse_lat_lon_alt(gnb_key)
                if ue is None or g is None:
                    dist_vars[gnb_key].set(f"UE1 → {gnb_key}:  dist=N/A  |  elev=N/A  |  Prx=N/A")
                else:
                    try:
                        d2 = self._haversine_2d_distance_m(ue[0], ue[1], g[0], g[1])
                        d3 = self._distance_3d_m(ue[0], ue[1], ue[2], g[0], g[1], g[2])
                        el = self._elevation_angle_deg(ue[0], ue[1], ue[2], g[0], g[1], g[2])
                        prx = _compute_prx(gnb_key, d3)
                        dist_vars[gnb_key].set(
                            f"UE1 → {gnb_key}:  "
                            f"2D={_format_dist(d2)}  |  "
                            f"3D={_format_dist(d3)}  |  "
                            f"elev={el:+.2f}°  |  "
                            f"Prx={prx}"
                        )
                    except (ValueError, ZeroDivisionError):
                        dist_vars[gnb_key].set(f"UE1 → {gnb_key}:  2D=error  |  3D=error  |  elev=error  |  Prx=error")

        for key in editable_keys:
            row = row_map[key]
            btn_row = ttk.Frame(frame)
            btn_row.grid(row=row, column=7, sticky="e", pady=4)
            ttk.Button(btn_row, text="Load", command=lambda k=key: _refresh_target(k)).pack(
                side=tk.LEFT, padx=(0, 6)
            )
            ttk.Button(btn_row, text="Apply", command=lambda k=key: _apply_target(k)).pack(side=tk.LEFT)

        footer = ttk.Frame(frame)
        footer.grid(row=footer_row, column=0, columnspan=8, sticky="e", pady=(12, 0))
        ttk.Button(footer, text="Refresh All", command=_refresh_all).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(footer, text="Apply All", command=_apply_all).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(footer, text="Close", command=dialog.destroy).pack(side=tk.LEFT)

        for key in editable_keys:
            lat_vars[key].trace_add("write", _update_distance_preview)
            lon_vars[key].trace_add("write", _update_distance_preview)
            alt_vars[key].trace_add("write", _update_distance_preview)

        for gnb_key in self.gnb_keys:
            rf_freq_vars[gnb_key].trace_add("write", _update_distance_preview)
            rf_txpwr_vars[gnb_key].trace_add("write", _update_distance_preview)
            rf_txgain_vars[gnb_key].trace_add("write", _update_distance_preview)
            rf_rxgain_vars[gnb_key].trace_add("write", _update_distance_preview)
            rf_type_vars[gnb_key].trace_add("write", _update_distance_preview)

        frame.grid_columnconfigure(1, weight=1)
        frame.grid_columnconfigure(2, weight=1)
        frame.grid_columnconfigure(3, weight=1)
        frame.grid_columnconfigure(4, weight=1)

        self.location_window = dialog

        def _on_close() -> None:
            self.location_window = None
            dialog.destroy()

        dialog.protocol("WM_DELETE_WINDOW", _on_close)
        _refresh_all()

    # ─────────────────────────────────────────────────────────────────────────
    def _open_viz_window(self) -> None:
        if self.viz_window is not None and self.viz_window.winfo_exists():
            self.viz_window.lift()
            self.viz_window.focus_set()
            return

        win = tk.Toplevel(self.root)
        win.title("Location Visualization")
        win.geometry("1050x700")
        win.resizable(True, True)

        # ── Top control bar ──────────────────────────────────────────────────
        ctrl = ttk.Frame(win, padding=(6, 4))
        ctrl.pack(fill=tk.X, side=tk.TOP)

        ttk.Label(ctrl, text="View:").pack(side=tk.LEFT)
        view_var = tk.StringVar(value="Elevation / Azimuth")
        view_combo = ttk.Combobox(
            ctrl, textvariable=view_var,
            values=["Elevation / Azimuth", "True Scale (Earth Cross-Section)"],
            state="readonly", width=34,
        )
        view_combo.pack(side=tk.LEFT, padx=(4, 12))

        ttk.Button(ctrl, text="Refresh Now", command=lambda: _fetch_and_draw(force=True)).pack(side=tk.LEFT)
        status_var = tk.StringVar(value="No data — press Refresh Now or start demo")
        ttk.Label(ctrl, textvariable=status_var, foreground="#555").pack(side=tk.LEFT, padx=10)

        # ── Main area ────────────────────────────────────────────────────────
        main = ttk.Frame(win)
        main.pack(fill=tk.BOTH, expand=True)

        canvas = tk.Canvas(main, bg="#080818")
        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # ── Info panel ───────────────────────────────────────────────────────
        info_outer = ttk.Frame(main, padding=(6, 4), width=420)
        info_outer.pack(side=tk.RIGHT, fill=tk.Y)
        info_outer.pack_propagate(False)

        ttk.Label(info_outer, text="Location Data", font=("TkDefaultFont", 9, "bold")).pack(anchor="w")
        info_text = scrolledtext.ScrolledText(
            info_outer, width=50, font=("Courier", 8), state=tk.DISABLED, wrap=tk.NONE,
        )
        info_text.pack(fill=tk.BOTH, expand=True)

        # ── Shared state ─────────────────────────────────────────────────────
        loc_cache: Dict[str, Dict[str, float]] = {}
        tle_cache: Dict[str, Dict[str, float]] = {}
        poll_id: list = [None]

        GNB_COLORS = ["#ff8800", "#00aaff", "#ff44ff"]
        UE_COLOR   = "#44ff44"

        def _update_info_panel() -> None:
            lines: List[str] = []
            for key in [self.primary_ue_key] + self.gnb_keys:
                label = "UE" if key == self.primary_ue_key else key.upper()
                node  = self.node_names.get(key, key)
                loc   = loc_cache.get(key)
                prop  = tle_cache.get(key)
                lines.append(f"{label}  ({node})")
                if loc and prop:
                    lines.append(f"  {'Reported':<20}Propagated")
                    lines.append(f"  lat: {loc['latitude']:+12.6f}°  {prop['latitude']:+12.6f}°")
                    lines.append(f"  lon: {loc['longitude']:+12.6f}°  {prop['longitude']:+12.6f}°")
                    lines.append(f"  alt: {loc['altitude']:>12,.0f} m  {prop['altitude']:>12,.0f} m")
                    if "timestampMs" in loc:
                        lines.append(f"  ts:  {self._ms_to_yyddd(loc['timestampMs'])}")
                elif loc:
                    lines.append(f"  lat: {loc['latitude']:+12.6f}°")
                    lines.append(f"  lon: {loc['longitude']:+12.6f}°")
                    lines.append(f"  alt: {loc['altitude']:>12,.0f} m")
                    if "timestampMs" in loc:
                        lines.append(f"  ts:  {self._ms_to_yyddd(loc['timestampMs'])}")
                else:
                    lines.append("  (no data)")
                lines.append("")
            info_text.config(state=tk.NORMAL)
            info_text.delete("1.0", tk.END)
            info_text.insert(tk.END, "\n".join(lines).rstrip())
            info_text.config(state=tk.DISABLED)

        def _fetch_all() -> int:
            nonlocal loc_cache, tle_cache
            new: Dict[str, Dict[str, float]] = {}
            for key in [self.primary_ue_key] + self.gnb_keys:
                try:
                    new[key] = self._fetch_location_wgs84(key)
                except RuntimeError:
                    pass
            # For gnbs with TLEs, propagate at the reported timestamp as a comparison value
            new_tle: Dict[str, Dict[str, float]] = {}
            for gnb_key in self.gnb_keys:
                tle = self.gnb_tles.get(gnb_key)
                if tle is not None:
                    reported = new.get(gnb_key)
                    epoch_ms = reported.get("timestampMs") if reported else None
                    propagated = self._propagate_tle_to_wgs84(tle[0], tle[1], epoch_ms=epoch_ms)
                    if propagated is not None:
                        new_tle[gnb_key] = propagated
            loc_cache = new
            tle_cache = new_tle
            return len(new)

        def _draw() -> None:
            if view_var.get().startswith("True"):
                _draw_true_scale()
            else:
                _draw_elev_az()

        def _fetch_and_draw(force: bool = False) -> None:
            if not force and not self.demo_running:
                return
            count = _fetch_all()
            _update_info_panel()
            _draw()
            status_var.set(f"Updated {time.strftime('%H:%M:%S')}  ({count} node(s) with data)")

        def _schedule_poll() -> None:
            if not win.winfo_exists():
                return
            if self.demo_running:
                _fetch_and_draw(force=True)
            interval_ms = max(500, int(self.poll_interval * 1000))
            poll_id[0] = win.after(interval_ms, _schedule_poll)

        # ── Elevation / Azimuth view ──────────────────────────────────────────
        def _draw_elev_az() -> None:
            canvas.delete("all")
            W = canvas.winfo_width()  or 800
            H = canvas.winfo_height() or 650

            ML, MR, MT, MB = 58, 16, 20, 44
            pw = W - ML - MR
            ph = H - MT - MB

            def to_xy(az_deg: float, el_deg: float) -> tuple:
                cx = ML + (az_deg / 360.0) * pw
                cy = MT + (1.0 - (el_deg + 90.0) / 180.0) * ph
                return cx, cy

            # Background
            canvas.create_rectangle(ML, MT, ML + pw, MT + ph, fill="#080818", outline="#334455")
            # Above-horizon tinted region
            _, hy = to_xy(0, 0)
            canvas.create_rectangle(ML, MT, ML + pw, hy, fill="#0a0f22", outline="")

            # Elevation grid lines
            for el_g in range(-90, 91, 30):
                _, cy = to_xy(0, el_g)
                if el_g == 0:
                    canvas.create_line(ML, cy, ML + pw, cy, fill="#6688aa", width=2)
                    canvas.create_text(ML - 4, cy, text="0° (horizon)", anchor="e",
                                       fill="#99aacc", font=("Arial", 7))
                else:
                    canvas.create_line(ML, cy, ML + pw, cy, fill="#223344")
                    canvas.create_text(ML - 4, cy, text=f"{el_g:+}°", anchor="e",
                                       fill="#556677", font=("Arial", 7))

            # Azimuth grid / compass labels
            compass = {0: "N", 45: "NE", 90: "E", 135: "SE",
                       180: "S", 225: "SW", 270: "W", 315: "NW"}
            for az_g in range(0, 361, 45):
                cx, _ = to_xy(az_g, 0)
                canvas.create_line(cx, MT, cx, MT + ph, fill="#223344")
                lbl = compass.get(az_g, f"{az_g}°")
                canvas.create_text(cx, MT + ph + 14, text=lbl, anchor="n",
                                   fill="#778899", font=("Arial", 7))

            # Axis labels
            canvas.create_text(ML + pw // 2, H - 5, text="Azimuth",
                                fill="#aabbcc", font=("Arial", 9), anchor="s")

            ue_loc = loc_cache.get(self.primary_ue_key)

            # UE observer marker centred on the horizon line
            ue_cx, ue_cy = to_xy(180.0, 0.0)
            r = 7
            canvas.create_oval(ue_cx - r, ue_cy - r, ue_cx + r, ue_cy + r,
                                fill=UE_COLOR, outline="white", width=2)
            canvas.create_text(ue_cx, ue_cy - r - 3, text="UE (observer)",
                                fill=UE_COLOR, font=("Arial", 8, "bold"), anchor="s")

            if ue_loc is None:
                canvas.create_text(W // 2, H // 2 + 30,
                                    text="No UE location — press Refresh Now",
                                    fill="#888", font=("Arial", 11))
                return

            for i, gnb_key in enumerate(self.gnb_keys):
                gnb_loc = loc_cache.get(gnb_key)
                if gnb_loc is None:
                    continue
                color = GNB_COLORS[i % len(GNB_COLORS)]
                el = self._elevation_angle_deg(
                    ue_loc["latitude"], ue_loc["longitude"], ue_loc["altitude"],
                    gnb_loc["latitude"], gnb_loc["longitude"], gnb_loc["altitude"],
                )
                az = self._azimuth_deg(
                    ue_loc["latitude"], ue_loc["longitude"], ue_loc["altitude"],
                    gnb_loc["latitude"], gnb_loc["longitude"], gnb_loc["altitude"],
                )
                el_c = max(-90.0, min(90.0, el))
                gx, gy = to_xy(az % 360.0, el_c)
                r = 6
                canvas.create_oval(gx - r, gy - r, gx + r, gy + r,
                                   fill=color, outline="white", width=1)
                canvas.create_text(gx, gy - r - 3, text=gnb_key.upper(),
                                   fill=color, font=("Arial", 8, "bold"), anchor="s")
                canvas.create_text(gx, gy + r + 2,
                                   text=f"az={az:.0f}°  el={el:+.1f}°",
                                   fill=color, font=("Arial", 7), anchor="n")

        # ── True Scale (Earth Cross-Section) view ─────────────────────────────
        def _draw_true_scale() -> None:
            canvas.delete("all")
            W = canvas.winfo_width()  or 800
            H = canvas.winfo_height() or 650
            margin = 50

            ue_loc    = loc_cache.get(self.primary_ue_key)
            gnb_pairs = [(k, loc_cache[k]) for k in self.gnb_keys if k in loc_cache]

            if ue_loc is None and not gnb_pairs:
                canvas.create_text(W // 2, H // 2,
                                    text="No location data — press Refresh Now",
                                    fill="#888", font=("Arial", 12))
                return

            # ── Vector helpers ────────────────────────────────────────────────
            def _dot3(a: tuple, b: tuple) -> float:
                return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]

            def _norm3(v: tuple) -> float:
                return math.sqrt(v[0]**2 + v[1]**2 + v[2]**2)

            def _normalize3(v: tuple) -> tuple:
                n = _norm3(v)
                return (v[0]/n, v[1]/n, v[2]/n) if n > 1e-10 else (1.0, 0.0, 0.0)

            def _sub3(a: tuple, b: tuple) -> tuple:
                return (a[0]-b[0], a[1]-b[1], a[2]-b[2])

            def _scale3(v: tuple, s: float) -> tuple:
                return (v[0]*s, v[1]*s, v[2]*s)

            def _ecef(loc: Dict[str, float]) -> tuple:
                return self._wgs84_to_ecef(loc["latitude"], loc["longitude"], loc["altitude"])

            # ── Projection basis ──────────────────────────────────────────────
            # e1 = radial "up" at UE;  UE sits at top of the Earth circle in the view
            if ue_loc:
                ue_ecef = _ecef(ue_loc)
            else:
                ue_ecef = _ecef(gnb_pairs[0][1])

            e1 = _normalize3(ue_ecef)

            # e2 = tangential direction in the orbital plane (perpendicular to e1)
            e2_acc = [0.0, 0.0, 0.0]
            for _, gloc in gnb_pairs:
                g   = _ecef(gloc)
                d   = _dot3(g, e1)
                perp = _sub3(g, _scale3(e1, d))
                n   = _norm3(perp)
                if n > 1.0:
                    e2_acc[0] += perp[0] / n
                    e2_acc[1] += perp[1] / n
                    e2_acc[2] += perp[2] / n

            e2_raw = tuple(e2_acc)
            if _norm3(e2_raw) < 0.1:
                # Fallback: arbitrary perpendicular to e1
                cand = (0.0, 1.0, 0.0) if abs(e1[1]) < 0.9 else (1.0, 0.0, 0.0)
                d    = _dot3(cand, e1)
                perp = _sub3(cand, _scale3(e1, d))
                e2_raw = perp
            e2 = _normalize3(e2_raw)

            # Project ECEF → (horizontal=e2, vertical=e1) in metres
            def _project(ecef: tuple) -> tuple:
                return _dot3(ecef, e2), _dot3(ecef, e1)

            # ── 2-D positions ─────────────────────────────────────────────────
            earth_r = 6371000.0
            ue_2d   = _project(ue_ecef) if ue_loc else None
            gnb_2ds = [(k, _project(_ecef(loc))) for k, loc in gnb_pairs]

            # ── Scale ─────────────────────────────────────────────────────────
            earth_boundary = [(earth_r, 0.0), (-earth_r, 0.0), (0.0, earth_r), (0.0, -earth_r)]
            all_h = [p[0] for _, p in gnb_2ds] + [ep[0] for ep in earth_boundary]
            all_v = [p[1] for _, p in gnb_2ds] + [ep[1] for ep in earth_boundary]
            if ue_2d:
                all_h.append(ue_2d[0])
                all_v.append(ue_2d[1])

            max_extent = max(max(abs(v) for v in all_h), max(abs(v) for v in all_v)) * 1.15
            avail = min(W - 2 * margin, H - 2 * margin)
            scale = avail / (2.0 * max_extent)

            cx0, cy0 = W / 2.0, H / 2.0

            def to_canvas(h: float, v: float) -> tuple:
                return cx0 + h * scale, cy0 - v * scale  # invert v: y grows down on canvas

            # ── Earth ─────────────────────────────────────────────────────────
            er_px = earth_r * scale
            canvas.create_oval(cx0 - er_px, cy0 - er_px, cx0 + er_px, cy0 + er_px,
                                fill="#0d2a47", outline="#3a6ea8", width=2)
            canvas.create_text(cx0, cy0, text="Earth", fill="#4a7faa", font=("Arial", 9))

            # ── Orbit circles (one per unique altitude) ───────────────────────
            drawn_radii: set = set()
            for i, (gnb_key, (gh, gv)) in enumerate(gnb_2ds):
                color     = GNB_COLORS[i % len(GNB_COLORS)]
                orbit_r_m = math.sqrt(gh**2 + gv**2)
                rounded   = round(orbit_r_m / 10000) * 10000  # bucket 10 km
                if rounded not in drawn_radii:
                    op = orbit_r_m * scale
                    canvas.create_oval(cx0 - op, cy0 - op, cx0 + op, cy0 + op,
                                       outline=color, width=1, dash=(4, 10))
                    drawn_radii.add(rounded)

            # ── Horizon line at UE ─────────────────────────────────────────────
            if ue_2d is not None:
                _, uy_c = to_canvas(*ue_2d)
                canvas.create_line(margin, uy_c, W - margin, uy_c,
                                   fill="#556677", width=1, dash=(8, 5))
                canvas.create_text(margin + 4, uy_c - 4, text="horizon", anchor="sw",
                                   fill="#6688aa", font=("Arial", 7))

            # ── Scale bar ─────────────────────────────────────────────────────
            ref_km = 10 ** math.floor(math.log10(max_extent / 1000))
            ref_px = ref_km * 1000 * scale
            if ref_px >= 15:
                bx = W - margin - ref_px
                by = H - margin + 18
                canvas.create_line(bx, by, bx + ref_px, by, fill="#556677", width=2)
                lbl = f"{ref_km:,.0f} km" if ref_km >= 1 else f"{int(ref_km * 1000)} m"
                canvas.create_text(bx + ref_px / 2, by + 4, text=lbl,
                                   anchor="n", fill="#6688aa", font=("Arial", 7))

            # ── UE ────────────────────────────────────────────────────────────
            if ue_2d is not None:
                ux_c, uy_c = to_canvas(*ue_2d)
                r = 6
                canvas.create_oval(ux_c - r, uy_c - r, ux_c + r, uy_c + r,
                                   fill=UE_COLOR, outline="white", width=2)
                canvas.create_text(ux_c, uy_c - r - 3, text="UE",
                                   fill=UE_COLOR, font=("Arial", 8, "bold"), anchor="s")

            # ── GNBs ──────────────────────────────────────────────────────────
            for i, (gnb_key, gnb_2d) in enumerate(gnb_2ds):
                color  = GNB_COLORS[i % len(GNB_COLORS)]
                gx_c, gy_c = to_canvas(*gnb_2d)
                r = 6
                canvas.create_oval(gx_c - r, gy_c - r, gx_c + r, gy_c + r,
                                   fill=color, outline="white", width=1)
                canvas.create_text(gx_c, gy_c - r - 3, text=gnb_key.upper(),
                                   fill=color, font=("Arial", 8, "bold"), anchor="s")
                alt_km = loc_cache[gnb_key]["altitude"] / 1000.0
                canvas.create_text(gx_c, gy_c + r + 2, text=f"{alt_km:,.0f} km",
                                   fill=color, font=("Arial", 7), anchor="n")

        # ── Event wiring ──────────────────────────────────────────────────────
        view_combo.bind("<<ComboboxSelected>>", lambda _: _draw())
        canvas.bind("<Configure>", lambda _: _draw())

        def _on_viz_close() -> None:
            if poll_id[0] is not None:
                try:
                    win.after_cancel(poll_id[0])
                except Exception:
                    pass
            self.viz_window = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", _on_viz_close)
        self.viz_window = win
        _schedule_poll()

    def _send_gnb_loc_pv(
        self,
        target_key: str,
        x: float,
        y: float,
        z: float,
        vx: float,
        vy: float,
        vz: float,
        epoch_ms: int,
    ) -> None:
        node = self.node_names.get(target_key)
        if not node:
            msg = f"Unknown gNB target '{target_key}'"
            self.inject_status_var.set(msg)
            self.panes[target_key].append_log("[set-loc-pv] " + msg)
            return

        arg = ":".join(
            [
                self._format_cli_float(x),
                self._format_cli_float(y),
                self._format_cli_float(z),
                self._format_cli_float(vx),
                self._format_cli_float(vy),
                self._format_cli_float(vz),
                str(epoch_ms),
            ]
        )
        cmd = f"set-loc-pv {arg}"
        out = self._cli_exec(node, cmd)
        if "error" in out:
            msg = f"set-loc-pv failed for {target_key} ({node}): {out['error']}"
            self.inject_status_var.set(msg)
            self.panes[target_key].append_log("[set-loc-pv] " + msg)
            return

        msg = (
            f"Sent set-loc-pv to {target_key} ({node}): "
            f"pos=({self._format_cli_float(x)}, {self._format_cli_float(y)}, {self._format_cli_float(z)}) "
            f"vel=({self._format_cli_float(vx)}, {self._format_cli_float(vy)}, {self._format_cli_float(vz)}) "
            f"epoch={epoch_ms}"
        )
        self.inject_status_var.set(msg)
        self.panes[target_key].append_log("[set-loc-pv] " + msg)

    def _open_gnb_loc_pv_dialog(self) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title("Send gNB Position/Velocity")
        dialog.transient(self.root)
        dialog.grab_set()

        frm = ttk.Frame(dialog, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)

        target_var = tk.StringVar(value=self.inject_target_var.get())
        x_var = tk.StringVar(value="0")
        y_var = tk.StringVar(value="0")
        z_var = tk.StringVar(value="0")
        vx_var = tk.StringVar(value="0")
        vy_var = tk.StringVar(value="0")
        vz_var = tk.StringVar(value="0")
        epoch_var = tk.StringVar(value=str(int(time.time() * 1000)))

        ttk.Label(frm, text="Target gNB:").grid(row=0, column=0, sticky="w", pady=4)
        ttk.Combobox(
            frm,
            width=12,
            state="readonly",
            textvariable=target_var,
            values=self.gnb_keys,
        ).grid(row=0, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="X (m):").grid(row=1, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=x_var, width=24).grid(row=1, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Y (m):").grid(row=2, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=y_var, width=24).grid(row=2, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Z (m):").grid(row=3, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=z_var, width=24).grid(row=3, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Vx (m/s):").grid(row=4, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=vx_var, width=24).grid(row=4, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Vy (m/s):").grid(row=5, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=vy_var, width=24).grid(row=5, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Vz (m/s):").grid(row=6, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=vz_var, width=24).grid(row=6, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Epoch (ms):").grid(row=7, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=epoch_var, width=24).grid(row=7, column=1, sticky="ew", pady=4)

        frm.grid_columnconfigure(1, weight=1)

        btns = ttk.Frame(frm)
        btns.grid(row=8, column=0, columnspan=2, sticky="e", pady=(10, 0))

        def _on_now() -> None:
            epoch_var.set(str(int(time.time() * 1000)))

        def _on_send() -> None:
            try:
                x = float(x_var.get().strip())
                y = float(y_var.get().strip())
                z = float(z_var.get().strip())
                vx = float(vx_var.get().strip())
                vy = float(vy_var.get().strip())
                vz = float(vz_var.get().strip())
                epoch_ms = int(epoch_var.get().strip())
                if epoch_ms < 0:
                    raise ValueError("epoch")
            except ValueError:
                self.inject_status_var.set("Invalid gNB position/velocity inputs")
                return

            self._send_gnb_loc_pv(
                target_key=target_var.get(),
                x=x,
                y=y,
                z=z,
                vx=vx,
                vy=vy,
                vz=vz,
                epoch_ms=epoch_ms,
            )
            dialog.destroy()

        ttk.Button(btns, text="Now", command=_on_now).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(btns, text="Send", command=_on_send).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(btns, text="Cancel", command=dialog.destroy).pack(side=tk.LEFT)

    def _open_handover_program_dialog(self) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title("Handover Program")
        dialog.transient(self.root)
        dialog.grab_set()

        frm = ttk.Frame(dialog, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)

        target_var = tk.StringVar(value=self.inject_target_var.get())
        period_var = tk.StringVar(value="1.0")
        dbm_a_var = tk.StringVar(value="-50")
        dbm_b_var = tk.StringVar(value="-60")
        duration_var = tk.StringVar(value="0")

        ttk.Label(frm, text="Target gNB:").grid(row=0, column=0, sticky="w", pady=4)
        ttk.Combobox(
            frm,
            width=12,
            state="readonly",
            textvariable=target_var,
            values=self.gnb_keys,
        ).grid(row=0, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Period (sec):").grid(row=1, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=period_var, width=16).grid(row=1, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Signal A (dBm):").grid(row=2, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=dbm_a_var, width=16).grid(row=2, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Signal B (dBm):").grid(row=3, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=dbm_b_var, width=16).grid(row=3, column=1, sticky="ew", pady=4)

        ttk.Label(frm, text="Duration (sec, 0=none):").grid(row=4, column=0, sticky="w", pady=4)
        ttk.Entry(frm, textvariable=duration_var, width=16).grid(row=4, column=1, sticky="ew", pady=4)

        frm.grid_columnconfigure(1, weight=1)

        btns = ttk.Frame(frm)
        btns.grid(row=5, column=0, columnspan=2, sticky="e", pady=(10, 0))

        def _on_ok() -> None:
            try:
                period = float(period_var.get().strip())
                if period <= 0:
                    raise ValueError("period")

                dbm_a = int(dbm_a_var.get().strip())
                dbm_b = int(dbm_b_var.get().strip())

                duration_raw = duration_var.get().strip()
                duration = 0.0 if duration_raw == "" else float(duration_raw)
                if duration < 0:
                    raise ValueError("duration")
            except ValueError:
                self.inject_status_var.set("Invalid handover program inputs")
                return

            self._start_handover_program(
                target=target_var.get(),
                period_sec=period,
                signal_a=dbm_a,
                signal_b=dbm_b,
                duration_sec=duration,
            )
            dialog.destroy()

        ttk.Button(btns, text="OK", command=_on_ok).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(btns, text="Cancel", command=dialog.destroy).pack(side=tk.LEFT)

    def _start_handover_program(
        self,
        target: str,
        period_sec: float,
        signal_a: int,
        signal_b: int,
        duration_sec: float,
    ) -> None:
        self._stop_handover_program()

        self.inject_target_var.set(target)
        self.program_stop_event.clear()
        self.program_running = True
        self.panes[target].append_log(
            "[program] Started: period="
            f"{period_sec}s signals=({signal_a},{signal_b}) duration={duration_sec}s"
        )

        def _loop() -> None:
            start = time.monotonic()
            toggle = False
            while not self.program_stop_event.is_set():
                val = signal_a if not toggle else signal_b
                toggle = not toggle

                self.root.after(0, lambda k=target, d=val: self._inject_rsrp_value(k, d, source="program"))

                if duration_sec > 0 and (time.monotonic() - start) >= duration_sec:
                    break

                if self.program_stop_event.wait(period_sec):
                    break

            self.program_running = False
            self.root.after(0, lambda: self.inject_status_var.set("Program stopped"))

        self.program_thread = threading.Thread(target=_loop, daemon=True)
        self.program_thread.start()

    def _stop_handover_program(self) -> None:
        if not self.program_running and self.program_thread is None:
            return

        self.program_stop_event.set()
        if self.program_thread is not None:
            self.program_thread.join(timeout=1.0)
            self.program_thread = None

        self.program_running = False
        self.inject_status_var.set("Program stopped")

    def _refresh_ui(self) -> None:
        for key in self.log_widgets:
            _, logs = self.panes[key].snapshot()

            widget = self.log_widgets[key]
            text = "\n".join(logs)
            widget.configure(state=tk.NORMAL)
            widget.delete("1.0", tk.END)
            widget.insert(tk.END, text)
            widget.see(tk.END)
            widget.configure(state=tk.DISABLED)

        for key, var in self.scalar_vars.items():
            scalars, _ = self.panes[key].snapshot()
            if key in self.ue_keys:
                scalars = {k: v for k, v in scalars.items() if k != "DB"}
            var.set("  ".join(f"{k}:{v}" for k, v in scalars.items()))

        if self.user_plane_window is not None and self.user_plane_window.winfo_exists():
            iface = self.primary_ue_tun_name or "unknown"
            ue_ip = self.primary_ue_tun_ip or "unknown"
            if self.user_plane_header_var is not None:
                self.user_plane_header_var.set(
                    "UE #1 TUN="
                    f"{iface} | UE TUN IP={ue_ip} | "
                    f"send-dst={ue_ip}:{self.user_plane_upf_port} | "
                    f"route-via={self.user_plane_host_route_gateway} | "
                    f"capture={self.user_plane_capture_status} | "
                    f"route={self.user_plane_route_status} | netns={self.user_plane_netns_status}"
                )

            with self.user_plane_lock:
                rx_text = "\n".join(self.user_plane_rx_logs)
                tx_text = "\n".join(self.user_plane_tx_logs)

            if self.user_plane_rx_widget is not None:
                self.user_plane_rx_widget.configure(state=tk.NORMAL)
                self.user_plane_rx_widget.delete("1.0", tk.END)
                self.user_plane_rx_widget.insert(tk.END, rx_text)
                self.user_plane_rx_widget.see(tk.END)
                self.user_plane_rx_widget.configure(state=tk.DISABLED)

            if self.user_plane_tx_widget is not None:
                self.user_plane_tx_widget.configure(state=tk.NORMAL)
                self.user_plane_tx_widget.delete("1.0", tk.END)
                self.user_plane_tx_widget.insert(tk.END, tx_text)
                self.user_plane_tx_widget.see(tk.END)
                self.user_plane_tx_widget.configure(state=tk.DISABLED)

        interval = 500 if self.demo_running else 2000
        self.root.after(interval, self._refresh_ui)

    def _on_close(self) -> None:
        self._stop_demo()
        self.root.destroy()

    def run(self) -> None:
        if self.auto_run or self.auto_close:
            self.root.after(300, self._run_demo)
        self._refresh_ui()
        self.root.mainloop()


def _load_config_file(path: Path) -> Dict[str, object]:
    """Load a dashboard config from a YAML (.yaml/.yml) or legacy JSON (.json) file."""
    with path.open("r", encoding="utf-8") as f:
        if path.suffix.lower() in (".yaml", ".yml"):
            return yaml.safe_load(f)
        return json.load(f)


def validate_config(config: Dict[str, object]) -> None:
    for key in ["amf", "ue", "gnb"]:
        if key not in config:
            raise ValueError(f"Missing config section: {key}")

    if "command_cli" not in config:
        raise ValueError("Missing required field: command_cli")

    gnb_section = config["gnb"]
    gnbs = gnb_section.get("gnbs", []) if isinstance(gnb_section, dict) else []
    if not isinstance(gnbs, list) or not (1 <= len(gnbs) <= 3):
        raise ValueError("Config gnb.gnbs must contain 1 to 3 gNB definitions.")

    for key in ["node", "config"]:
        if key not in config["ue"]:
            raise ValueError(f"UE config missing required key: {key}")

    ue_count = int(config["ue"].get("count", 1))
    if ue_count < 1:
        raise ValueError("UE count must be at least 1")

    for i, gnb in enumerate(gnbs):
        for key in ["node", "config"]:
            if key not in gnb:
                raise ValueError(f"gNB #{i + 1} config missing required key: {key}")

    up_cfg = config.get("user_plane", {})
    upf_port = int(up_cfg.get("upf_port", 5000))
    if upf_port < 1 or upf_port > 65535:
        raise ValueError("user_plane.upf_port must be in range 1..65535")

    route_gateway = str(up_cfg.get("host_route_gateway", "172.22.0.1")).strip()
    route_subnet = str(up_cfg.get("host_route_subnet", "172.22.0.0/24")).strip()
    try:
        ipaddress.ip_address(route_gateway)
    except ValueError as ex:
        raise ValueError(f"user_plane.host_route_gateway is invalid: {ex}") from ex

    try:
        ipaddress.ip_network(route_subnet, strict=False)
    except ValueError as ex:
        raise ValueError(f"user_plane.host_route_subnet is invalid: {ex}") from ex


def main() -> None:
    parser = argparse.ArgumentParser(description="UERANSIM windowed dashboard")
    parser.add_argument("--config", required=False, default=None, help="Path to dashboard YAML config (optional; .json also accepted)")
    parser.add_argument(
        "-u", "--ues", "--ue-count",
        dest="ue_count",
        type=int,
        default=None,
        metavar="N",
        help="Number of UEs (overrides ue.count from config)",
    )
    parser.add_argument(
        "-r", "--run",
        action="store_true",
        default=False,
        help="Automatically start the demo on launch (requires --config)",
    )
    parser.add_argument(
        "-R", "--run-close",
        action="store_true",
        default=False,
        help="Automatically start the demo and close the app when the run completes (requires --config)",
    )
    args = parser.parse_args()

    if (args.run or args.run_close) and args.config is None:
        parser.error("--run / --run-close require --config")

    if args.config is not None:
        cfg_path = Path(args.config).resolve()
        workspace = cfg_path.parent.parent.parent.resolve()

        config = _load_config_file(cfg_path)

        if args.ue_count is not None:
            if args.ue_count < 1:
                raise ValueError("--ue-count must be at least 1")
            config.setdefault("ue", {})["count"] = args.ue_count

        validate_config(config)
        app = WindowedDashboard(
            config, workspace, config_path=cfg_path,
            auto_run=args.run or args.run_close,
            auto_close=args.run_close,
        )
    else:
        app = WindowedDashboard()

    app.run()


if __name__ == "__main__":
    main()
