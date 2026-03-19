#!/usr/bin/env python3
"""Windowed runtime dashboard for UE, two gNBs, and AMF with manual RSRP injection."""

from __future__ import annotations

import argparse
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
from tkinter import scrolledtext, ttk
from typing import Dict, List, Optional, TextIO

RLS_MSG_GNB_RF_DATA = 21
RLS_PORT = 4997
CONS_MAJOR = 3
CONS_MINOR = 3
CONS_PATCH = 7
MIN_RSRP = -156
MAX_RSRP = -44


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
        self.ue_run_with_sudo = bool(ue_cfg.get("run_with_sudo", False))
        self.ue_cli_with_sudo = bool(ue_cfg.get("cli_with_sudo", self.ue_run_with_sudo))
        self.ue_cli_sudo_non_interactive = bool(ue_cfg.get("cli_sudo_non_interactive", True))
        self.ue_cli_sudo_disabled_logged = False

        self.panes = {
            "ue": EntityPane("ue", "UE"),
            "gnb1": EntityPane("gnb1", "gNB #1"),
            "gnb2": EntityPane("gnb2", "gNB #2"),
            "amf": EntityPane("amf", "AMF"),
        }
        for pane in self.panes.values():
            pane.logs = deque(maxlen=self.log_limit)

        self.processes: Dict[str, ManagedProcess] = {}
        self.stop_event = threading.Event()
        self.node_names: Dict[str, str] = {
            "ue": str(self.config["ue"]["node"]),
            "gnb1": str(self.config["gnbs"][0]["node"]),
            "gnb2": str(self.config["gnbs"][1]["node"]),
        }
        self.last_cli_error: Dict[str, str] = {}

        self.gnb_link_ips = {
            "gnb1": self._extract_gnb_link_ip(0),
            "gnb2": self._extract_gnb_link_ip(1),
        }

        self.root = tk.Tk()
        self.root.title("UERANSIM Windowed Dashboard")
        self.root.geometry("1500x900")
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        self.scalar_vars: Dict[str, tk.StringVar] = {}
        self.log_widgets: Dict[str, scrolledtext.ScrolledText] = {}
        self.inject_target_var = tk.StringVar(value="gnb1")
        self.inject_dbm_var = tk.StringVar(value="-80")
        self.inject_status_var = tk.StringVar(value="Ready")
        self.program_thread: Optional[threading.Thread] = None
        self.program_stop_event = threading.Event()
        self.program_running = False

        self._build_ui()

    def _resolve(self, value: str) -> str:
        p = Path(value)
        if p.is_absolute():
            return str(p)
        return str((self.workspace / p).resolve())

    def _process_log_path(self, key: str) -> Path:
        return self.process_log_dir / f"{key}-windowed.log"

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

        panes_frame = ttk.Frame(self.root, padding=8)
        panes_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        keys = ["ue", "gnb1", "gnb2", "amf"]
        for i, key in enumerate(keys):
            row = i // 2
            col = i % 2

            pane_frame = ttk.LabelFrame(panes_frame, text=self.panes[key].title, padding=8)
            pane_frame.grid(row=row, column=col, sticky="nsew", padx=6, pady=6)

            panes_frame.grid_columnconfigure(col, weight=1)
            panes_frame.grid_rowconfigure(row, weight=1)

            scalar_var = tk.StringVar(value="")
            self.scalar_vars[key] = scalar_var
            scalar_label = ttk.Label(
                pane_frame,
                textvariable=scalar_var,
                justify=tk.LEFT,
                anchor="w",
                font=("TkFixedFont", 10),
            )
            scalar_label.pack(side=tk.TOP, fill=tk.X)

            log_widget = scrolledtext.ScrolledText(pane_frame, wrap=tk.WORD, height=18)
            log_widget.pack(side=tk.TOP, fill=tk.BOTH, expand=True, pady=(8, 0))
            log_widget.configure(state=tk.DISABLED, font=("TkFixedFont", 10))
            self.log_widgets[key] = log_widget

    def _build_menu(self) -> None:
        menubar = tk.Menu(self.root)

        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="Exit", command=self._on_close)
        menubar.add_cascade(label="File", menu=file_menu)

        handover_menu = tk.Menu(menubar, tearoff=0)
        handover_menu.add_command(label="Program", command=self._open_handover_program_dialog)
        handover_menu.add_command(label="Stop Program", command=self._stop_handover_program)
        menubar.add_cascade(label="Handover", menu=handover_menu)

        self.root.config(menu=menubar)

    def _cli_exec(self, node: str, command: str) -> Dict[str, str]:
        return self._cli_exec_with_mode(node, command, use_sudo=False)

    def _disable_ue_cli_sudo(self, reason: str) -> None:
        if not self.ue_cli_with_sudo:
            return

        self.ue_cli_with_sudo = False
        if not self.ue_cli_sudo_disabled_logged:
            self.panes["ue"].append_log(
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

    def _probe_nodes_by_status(self, nodes: List[str]) -> tuple[Optional[str], List[str]]:
        return self._probe_nodes_by_status_with_mode(nodes, use_sudo=False)

    def _probe_nodes_by_status_with_mode(
        self,
        nodes: List[str],
        use_sudo: bool,
    ) -> tuple[Optional[str], List[str]]:
        ue_node: Optional[str] = None
        gnb_nodes: List[str] = []

        for node in nodes:
            out = self._cli_exec_with_mode(node, "ui-status", use_sudo=use_sudo)
            if "error" in out:
                continue

            if "rrc-state" in out or "nas-state" in out:
                if ue_node is None:
                    ue_node = node
                continue

            if "nci" in out or "rrc-ue-count" in out or "ngap-ue-count" in out:
                gnb_nodes.append(node)

        gnb_nodes.sort()
        return ue_node, gnb_nodes

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

        probed_ue_user, probed_gnbs = self._probe_nodes_by_status_with_mode(user_nodes, use_sudo=False)
        probed_ue_sudo: Optional[str] = None
        if self.ue_cli_with_sudo:
            probed_ue_sudo, _ = self._probe_nodes_by_status_with_mode(sudo_nodes, use_sudo=True)

        probed_ue = probed_ue_user if probed_ue_user is not None else probed_ue_sudo

        if current["ue"] not in nodes and probed_ue is not None:
            resolved["ue"] = probed_ue

        if current["gnb1"] not in nodes and len(probed_gnbs) >= 1:
            resolved["gnb1"] = probed_gnbs[0]

        if current["gnb2"] not in nodes and len(probed_gnbs) >= 2:
            resolved["gnb2"] = probed_gnbs[1]

        for key in ["ue", "gnb1", "gnb2"]:
            if self.node_names.get(key) != resolved[key]:
                self.panes[key].append_log(
                    f"CLI node mapped: '{self.node_names.get(key)}' -> '{resolved[key]}'"
                )

        self.node_names = resolved

    def _record_cli_error(self, key: str, error: str) -> None:
        # Node names can differ at runtime; remapping may happen one poll later.
        # Suppress this known transient to avoid noisy panes at startup.
        if "No node found with name" in error:
            return

        last = self.last_cli_error.get(key)
        if last != error:
            self.panes[key].append_log(f"cli poll error: {error}")
            self.last_cli_error[key] = error

    def _poll_ue_status(self) -> Dict[str, str]:
        node = self.node_names["ue"]
        out = self._cli_exec_with_mode(node, "ui-status", use_sudo=self.ue_cli_with_sudo)
        if "error" in out:
            self._record_cli_error("ue", out["error"])
            return {"RRC": "N/A", "NAS": "N/A", "PCI": "N/A", "dBm": "N/A"}

        self.last_cli_error.pop("ue", None)
        return {
            "RRC": out.get("rrc-state", "N/A"),
            "NAS": out.get("nas-state", "N/A"),
            "PCI": out.get("connected-pci", "N/A"),
            "dBm": out.get("connected-dbm", "N/A"),
        }

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
            try:
                self.panes["ue"].set_scalars(self._poll_ue_status())
            except Exception as ex:  # pylint: disable=broad-except
                self.panes["ue"].append_log(f"status poll failed: {ex}")

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

        self._prepare_ue_privileges(nr_ue_path)

        ue_cmd = [
            nr_ue_path,
            "-c",
            self._resolve(str(ue_cfg["config"])),
            *[str(x) for x in ue_cfg.get("args", [])],
        ]

        if bool(ue_cfg.get("run_with_sudo", False)):
            sudo_non_interactive = bool(ue_cfg.get("sudo_non_interactive", False))
            if not sudo_non_interactive and not os.isatty(0):
                sudo_non_interactive = True
                self.panes["ue"].append_log(
                    "[priv] No TTY for interactive sudo; forcing non-interactive sudo (-n)"
                )

            sudo_cmd = ["sudo"]
            if sudo_non_interactive:
                sudo_cmd.append("-n")
            ue_cmd = [*sudo_cmd, *ue_cmd]

        self.processes["ue"] = ManagedProcess(
            self.panes["ue"],
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
            self.panes["ue"].append_log("[priv] Granted cap_net_admin to nr-ue binary for TUN setup")
            return

        err = setcap.stderr.strip() or setcap.stdout.strip() or "setcap failed"
        self.panes["ue"].append_log(f"[priv] Unable to grant cap_net_admin automatically: {err}")
        self.panes["ue"].append_log(
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
        for key in ["ue", "gnb1", "gnb2", "amf"]:
            scalars, logs = self.panes[key].snapshot()

            scalar_lines = [f"{k}: {v}" for k, v in scalars.items()]
            self.scalar_vars[key].set("\n".join(scalar_lines))

            widget = self.log_widgets[key]
            text = "\n".join(logs)
            widget.configure(state=tk.NORMAL)
            widget.delete("1.0", tk.END)
            widget.insert(tk.END, text)
            widget.see(tk.END)
            widget.configure(state=tk.DISABLED)

        if not self.stop_event.is_set():
            self.root.after(500, self._refresh_ui)

    def _on_close(self) -> None:
        self._stop_handover_program()
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

    for i, gnb in enumerate(gnbs):
        for key in ["node", "config"]:
            if key not in gnb:
                raise ValueError(f"gNB #{i + 1} config missing required key: {key}")


def main() -> None:
    parser = argparse.ArgumentParser(description="UERANSIM windowed dashboard")
    parser.add_argument("--config", required=True, help="Path to dashboard JSON config")
    args = parser.parse_args()

    cfg_path = Path(args.config).resolve()
    workspace = cfg_path.parent.parent.parent.resolve()

    with cfg_path.open("r", encoding="utf-8") as f:
        config = json.load(f)

    validate_config(config)
    app = WindowedDashboard(config, workspace)
    app.run()


if __name__ == "__main__":
    main()
