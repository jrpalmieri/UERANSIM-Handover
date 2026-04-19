#!/usr/bin/env python3
"""Windowed runtime dashboard for multi-UE status, two gNBs, and AMF with manual RSRP injection."""

from __future__ import annotations

import argparse
import ipaddress
import json
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
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import messagebox, scrolledtext, ttk
from typing import Dict, List, Optional, TextIO

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
    def __init__(self, config: Dict[str, object], workspace: Path) -> None:
        self.workspace = workspace
        self.config = config

        ui_cfg = config.get("ui", {})
        self.poll_interval = float(ui_cfg.get("poll_interval_sec", 1.0))
        self.log_limit = int(ui_cfg.get("max_log_lines", 400))
        self.command_timeout = float(ui_cfg.get("command_timeout_sec", 3.0))
        self.process_log_dir = Path(self._resolve(str(ui_cfg.get("process_log_dir", "./tools/UI/logs"))))
        self.process_log_dir.mkdir(parents=True, exist_ok=True)
        ue_cfg = config.get("ue", {})
        self.ue_count = max(1, int(ue_cfg.get("count", 1)))
        self.ue_keys = [f"ue{idx + 1}" for idx in range(self.ue_count)]
        self.primary_ue_key = self.ue_keys[0]
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

        self.panes = {
            "gnb1": EntityPane("gnb1", "GNB1"),
            "gnb2": EntityPane("gnb2", "GNB2"),
            "amf": EntityPane("amf", "AMF"),
        }
        for idx, key in enumerate(self.ue_keys):
            title = "UE" if idx == 0 else f"UE{idx + 1}"
            self.panes[key] = EntityPane(key, title)

        for pane in self.panes.values():
            pane.logs = deque(maxlen=self.log_limit)

        self.processes: Dict[str, ManagedProcess] = {}
        self.stop_event = threading.Event()
        self.node_names: Dict[str, str] = {
            "gnb1": str(self.config["gnbs"][0]["node"]),
            "gnb2": str(self.config["gnbs"][1]["node"]),
        }
        self.node_names.update(self._build_ue_node_names())
        self.last_cli_error: Dict[str, str] = {}
        self.ue_db_status: Dict[str, str] = {key: "UNKNOWN" for key in self.ue_keys}

        self.gnb_link_ips = {
            "gnb1": self._extract_gnb_link_ip(0),
            "gnb2": self._extract_gnb_link_ip(1),
        }

        self.root = tk.Tk()
        self.root.title("UERANSIM Windowed Dashboard")
        self.root.geometry("1500x900")
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        self.log_widgets: Dict[str, scrolledtext.ScrolledText] = {}
        self.summary_window: Optional[tk.Toplevel] = None
        self.summary_widget: Optional[scrolledtext.ScrolledText] = None
        self.inject_target_var = tk.StringVar(value="gnb1")
        self.inject_dbm_var = tk.StringVar(value="-80")
        self.inject_status_var = tk.StringVar(value="Ready")
        self.ue_count_var = tk.StringVar(
            value=f"UEs: total={self.ue_count}, log-pane=1"
        )
        self.program_thread: Optional[threading.Thread] = None
        self.program_stop_event = threading.Event()
        self.program_running = False
        self.repeat_message_thread: Optional[threading.Thread] = None
        self.repeat_message_stop_event = threading.Event()
        self.repeat_message_running = False

        self._build_ui()

    def _resolve(self, value: str) -> str:
        p = Path(value)
        if p.is_absolute():
            return str(p)
        return str((self.workspace / p).resolve())

    def _process_log_path(self, key: str) -> Path:
        return self.process_log_dir / f"{key}-windowed.log"

    def _build_ue_node_names(self) -> Dict[str, str]:
        ue_cfg = self.config["ue"]
        base_node = str(ue_cfg["node"])
        match = re.search(r"^(.*?)(\d+)$", base_node)
        if match:
            prefix = match.group(1)
            start = int(match.group(2))
            return {
                self.ue_keys[idx]: f"{prefix}{start + idx}"
                for idx in range(self.ue_count)
            }

        return {
            self.ue_keys[idx]: f"{base_node}{idx + 1}"
            for idx in range(self.ue_count)
        }

    def _extract_gnb_link_ip(self, idx: int) -> str:
        path = Path(self._resolve(str(self.config["gnbs"][idx]["config"])))
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

    def _build_ui(self) -> None:
        self._build_menu()

        controls = ttk.Frame(self.root, padding=8)
        controls.pack(side=tk.TOP, fill=tk.X)

        ttk.Label(controls, text="Inject RSRP (dBm) into gNB:").pack(side=tk.LEFT)

        target_box = ttk.Combobox(
            controls,
            width=10,
            state="readonly",
            textvariable=self.inject_target_var,
            values=["gnb1", "gnb2"],
        )
        target_box.pack(side=tk.LEFT, padx=(8, 4))

        dbm_entry = ttk.Entry(controls, width=8, textvariable=self.inject_dbm_var)
        dbm_entry.pack(side=tk.LEFT, padx=4)

        inject_button = ttk.Button(controls, text="Inject", command=self._inject_rsrp)
        inject_button.pack(side=tk.LEFT, padx=(4, 12))

        ttk.Label(controls, textvariable=self.inject_status_var).pack(side=tk.LEFT)
        ttk.Label(controls, text="|").pack(side=tk.LEFT, padx=(12, 6))
        ttk.Label(controls, textvariable=self.ue_count_var).pack(side=tk.LEFT)

        panes_frame = ttk.Frame(self.root, padding=8)
        panes_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        self._build_summary_window()

        keys = [self.primary_ue_key, "gnb1", "gnb2", "amf"]
        for i, key in enumerate(keys):
            row = i // 2
            col = i % 2

            pane_frame = ttk.LabelFrame(panes_frame, text=self.panes[key].title, padding=8)
            pane_frame.grid(row=row, column=col, sticky="nsew", padx=6, pady=6)

            panes_frame.grid_columnconfigure(col, weight=1)
            panes_frame.grid_rowconfigure(row, weight=1)

            self._create_pane_widgets(pane_frame, key, log_height=22)

    def _build_summary_window(self) -> None:
        win = tk.Toplevel(self.root)
        win.title("UERANSIM Summary")
        win.geometry("650x420")
        win.resizable(True, True)

        frame = ttk.LabelFrame(win, text="Summary", padding=8)
        frame.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        widget = scrolledtext.ScrolledText(frame, wrap=tk.WORD, height=20)
        widget.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        widget.configure(state=tk.DISABLED, font=("TkFixedFont", 10))

        self.summary_window = win
        self.summary_widget = widget

    def _create_pane_widgets(self, parent: ttk.Widget, key: str, log_height: int) -> None:
        log_widget = scrolledtext.ScrolledText(parent, wrap=tk.WORD, height=log_height)
        log_widget.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        log_widget.configure(state=tk.DISABLED, font=("TkFixedFont", 10))
        self.log_widgets[key] = log_widget

    def _build_summary_text(self) -> str:
        blocks: List[str] = []

        for key in self.ue_keys:
            scalars, _ = self.panes[key].snapshot()
            title = self.panes[key].title
            lines = [f"{k}: {v}" for k, v in scalars.items()]
            blocks.append(title + "\n" + ("\n".join(lines) if lines else "No data"))

        for key in ("gnb1", "gnb2", "amf"):
            scalars, _ = self.panes[key].snapshot()
            title = self.panes[key].title
            lines = [f"{k}: {v}" for k, v in scalars.items()]
            blocks.append(title + "\n" + ("\n".join(lines) if lines else "No data"))

        return "\n\n".join(blocks)

    def _build_menu(self) -> None:
        menubar = tk.Menu(self.root)

        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="Exit", command=self._on_close)
        menubar.add_cascade(label="File", menu=file_menu)

        handover_menu = tk.Menu(menubar, tearoff=0)
        handover_menu.add_command(label="Send gNB Position/Velocity", command=self._open_gnb_loc_pv_dialog)
        handover_menu.add_command(label="Program", command=self._open_handover_program_dialog)
        handover_menu.add_command(label="Stop Program", command=self._stop_handover_program)
        menubar.add_cascade(label="Handover", menu=handover_menu)

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
        exe_cfg = self.config["executables"]
        nr_cli = self._resolve(str(exe_cfg["nr_cli"]))
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
        exe_cfg = self.config["executables"]
        nr_cli = self._resolve(str(exe_cfg["nr_cli"]))
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

        if current["gnb1"] not in nodes and len(probed_gnbs) >= 1:
            resolved["gnb1"] = probed_gnbs[0]

        if current["gnb2"] not in nodes and len(probed_gnbs) >= 2:
            resolved["gnb2"] = probed_gnbs[1]

        for key in [*self.ue_keys, "gnb1", "gnb2"]:
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

    def _user_plane_capture_loop(self) -> None:
        while not self.user_plane_capture_stop.is_set():
            if not self.primary_ue_tun_name:
                self._set_user_plane_capture_status("waiting for UE #1 TUN")
                self.user_plane_capture_stop.wait(0.5)
                continue

            if self.user_plane_tun_moved_to_netns and self.user_plane_move_tun_to_netns:
                self._set_user_plane_capture_status("running (netns tcpdump)")
                self._start_user_plane_tcpdump_capture()
                return

            try:
                sock = socket.socket(
                    socket.AF_PACKET,
                    socket.SOCK_RAW,
                    socket.ntohs(0x0003),
                )
                sock.bind((self.primary_ue_tun_name, 0))
                sock.settimeout(0.5)
            except PermissionError:
                self._set_user_plane_capture_status("permission denied, trying tcpdump fallback")
                self._append_user_plane_rx("capture error: permission denied for raw socket")
                self._start_user_plane_tcpdump_capture()
                return
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

        self._start_user_plane_capture()

        def _on_close() -> None:
            self.user_plane_window = None
            self.user_plane_rx_widget = None
            self.user_plane_tx_widget = None
            self.user_plane_header_var = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", _on_close)

    def _send_user_plane_text(self, text: str) -> None:
        self._discover_primary_ue_tun_info()
        ue_ip = self.primary_ue_tun_ip

        if not ue_ip:
            self._append_user_plane_tx("send failed: UE #1 TUN IP is unknown")
            return

        payload = text.encode("utf-8", errors="replace")

        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                udp.sendto(payload, (ue_ip, self.user_plane_upf_port))
        except OSError as ex:
            self._append_user_plane_tx(
                f"send failed to {ue_ip}:{self.user_plane_upf_port}: {ex}"
            )
            return

        self._append_user_plane_tx(
            f"sent {len(payload)}B to UE {ue_ip}:{self.user_plane_upf_port} "
            f"(route via {self.user_plane_host_route_gateway}) text={text!r}"
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

    def _start_user_plane_repeated_message(self, message: str, cycle_ms: int) -> None:
        self._stop_user_plane_repeated_message()

        period_sec = cycle_ms / 1000.0
        self.repeat_message_stop_event.clear()
        self.repeat_message_running = True
        self._append_user_plane_tx(
            f"repeat sender started: cycle={cycle_ms}ms message={message!r}"
        )

        def _loop() -> None:
            while not self.repeat_message_stop_event.is_set():
                self._send_user_plane_text(message)
                if self.repeat_message_stop_event.wait(period_sec):
                    break

            self.repeat_message_running = False
            self.root.after(0, lambda: self._append_user_plane_tx("repeat sender stopped"))

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

    def _poll_gnb_status(self, idx: int) -> Dict[str, str]:
        key = "gnb1" if idx == 0 else "gnb2"
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
        gnb1_scalars, _ = self.panes["gnb1"].snapshot()
        gnb2_scalars, _ = self.panes["gnb2"].snapshot()

        g1 = gnb1_scalars.get("NGAP Up", "N/A")
        g2 = gnb2_scalars.get("NGAP Up", "N/A")

        if g1 == "TRUE" or g2 == "TRUE":
            return True, None
        if g1 == "FALSE" and g2 == "FALSE":
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
            self._discover_primary_ue_tun_info()
            self._ensure_user_plane_tun_namespace()
            self._ensure_host_route_to_primary_ue()
            for key in self.ue_keys:
                try:
                    ue_scalars = self._poll_ue_status(key)
                    ue_scalars["DB"] = self.ue_db_status.get(key, "UNKNOWN")
                    self.panes[key].set_scalars(ue_scalars)
                except Exception as ex:  # pylint: disable=broad-except
                    self.panes[key].append_log(f"status poll failed: {ex}")

            try:
                self.panes["gnb1"].set_scalars(self._poll_gnb_status(0))
            except Exception as ex:  # pylint: disable=broad-except
                self.panes["gnb1"].append_log(f"status poll failed: {ex}")

            try:
                self.panes["gnb2"].set_scalars(self._poll_gnb_status(1))
            except Exception as ex:  # pylint: disable=broad-except
                self.panes["gnb2"].append_log(f"status poll failed: {ex}")

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
        raw = amf.get("log_file")
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

            try:
                with log_path.open("r", encoding="utf-8", errors="replace") as f:
                    size = log_path.stat().st_size
                    if position > size:
                        position = 0
                    f.seek(position)
                    for line in f:
                        self.panes["amf"].append_log("[AMF] " + line.rstrip("\n"))
                    position = f.tell()
            except OSError as ex:
                self.panes["amf"].append_log(f"AMF log read error: {ex}")

            self.stop_event.wait(self.poll_interval)

    def _start_processes(self) -> None:
        exe_cfg = self.config["executables"]

        ue_cfg = self.config["ue"]
        nr_ue_path = self._resolve(str(exe_cfg["nr_ue"]))

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

        for idx in range(2):
            key = f"gnb{idx + 1}"
            gnb_cfg = self.config["gnbs"][idx]
            gnb_cmd = [
                self._resolve(str(exe_cfg["nr_gnb"])),
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
    def _format_cli_float(value: float) -> str:
        return f"{value:.12g}"

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
            values=["gnb1", "gnb2"],
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
            values=["gnb1", "gnb2"],
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

        if self.summary_window is not None and self.summary_window.winfo_exists() and self.summary_widget is not None:
            summary_text = self._build_summary_text()
            self.summary_widget.configure(state=tk.NORMAL)
            self.summary_widget.delete("1.0", tk.END)
            self.summary_widget.insert(tk.END, summary_text)
            self.summary_widget.see("1.0")
            self.summary_widget.configure(state=tk.DISABLED)

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

        if not self.stop_event.is_set():
            self.root.after(500, self._refresh_ui)

    def _on_close(self) -> None:
        self._stop_handover_program()
        self._stop_user_plane_repeated_message()
        self._stop_user_plane_capture()
        self.stop_event.set()
        for proc in self.processes.values():
            proc.stop()
        self.root.destroy()

    def run(self) -> None:
        self._start_processes()

        status_thread = threading.Thread(target=self._status_loop, daemon=True)
        amf_thread = threading.Thread(target=self._amf_loop, daemon=True)
        amf_log_thread = threading.Thread(target=self._amf_log_loop, daemon=True)
        status_thread.start()
        amf_thread.start()
        amf_log_thread.start()

        self._refresh_ui()
        self.root.mainloop()


def validate_config(config: Dict[str, object]) -> None:
    for key in ["executables", "amf", "ue", "gnbs"]:
        if key not in config:
            raise ValueError(f"Missing config section: {key}")

    gnbs = config["gnbs"]
    if not isinstance(gnbs, list) or len(gnbs) != 2:
        raise ValueError("Config must contain exactly two gNB definitions in 'gnbs'.")

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
    parser.add_argument("--config", required=True, help="Path to dashboard JSON config")
    parser.add_argument(
        "--ue-count",
        type=int,
        default=None,
        help="Number of UEs to run in nr-ue (overrides ue.count from config)",
    )
    args = parser.parse_args()

    cfg_path = Path(args.config).resolve()
    workspace = cfg_path.parent.parent.parent.resolve()

    with cfg_path.open("r", encoding="utf-8") as f:
        config = json.load(f)

    if args.ue_count is not None:
        if args.ue_count < 1:
            raise ValueError("--ue-count must be at least 1")
        config.setdefault("ue", {})["count"] = args.ue_count

    validate_config(config)
    app = WindowedDashboard(config, workspace)
    app.run()


if __name__ == "__main__":
    main()
