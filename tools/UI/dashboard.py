#!/usr/bin/env python3
"""Terminal UI for running and monitoring one UE, two gNBs, and AMF reachability."""

from __future__ import annotations

import argparse
import curses
import json
import os
import shlex
import socket
import subprocess
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, TextIO


def parse_simple_yaml(text: str) -> Dict[str, str]:
    """Parse flat key/value YAML emitted by CLI ui-status command handlers."""
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
            # Truncate on each dashboard run.
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
            if self.log_file is not None:
                self.log_file.close()
                self.log_file = None
            return
        if self.proc.poll() is not None:
            if self.log_file is not None:
                self.log_file.close()
                self.log_file = None
            return
        self.pane.append_log("[stopping process]")
        self._write_log_file("[stopping process]\n")
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.pane.append_log("[terminate timeout, killing process]")
            self._write_log_file("[terminate timeout, killing process]\n")
            self.proc.kill()
        finally:
            if self.log_file is not None:
                self.log_file.close()
                self.log_file = None


class Dashboard:
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

    def _process_log_path(self, key: str) -> Path:
        return self.process_log_dir / f"{key}.log"

    def _tail_amf_log_path(self) -> Optional[Path]:
        amf = self.config["amf"]
        raw = amf.get("log_file")
        if raw is None:
            return None
        value = str(raw).strip()
        if not value:
            return None
        return Path(self._resolve(value))

    def _resolve(self, value: str) -> str:
        p = Path(value)
        if p.is_absolute():
            return str(p)
        return str((self.workspace / p).resolve())

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

    def _resolve_runtime_node_names(self) -> None:
        user_nodes = self._list_runtime_nodes_with_mode(use_sudo=False)
        sudo_nodes: List[str] = []
        if self.ue_cli_with_sudo:
            sudo_nodes = self._list_runtime_nodes_with_mode(use_sudo=True)

        nodes = sorted(set(user_nodes) | set(sudo_nodes))
        if not nodes:
            return

        configured = dict(self.node_names)
        resolved = dict(configured)

        for key, name in configured.items():
            if name in nodes:
                resolved[key] = name

        probed_ue_user, probed_gnbs = self._probe_nodes_by_status_with_mode(user_nodes, use_sudo=False)
        probed_ue_sudo: Optional[str] = None
        if self.ue_cli_with_sudo:
            probed_ue_sudo, _ = self._probe_nodes_by_status_with_mode(sudo_nodes, use_sudo=True)

        probed_ue = probed_ue_user if probed_ue_user is not None else probed_ue_sudo

        if configured["ue"] not in nodes and probed_ue is not None:
            resolved["ue"] = probed_ue

        if configured["gnb1"] not in nodes and len(probed_gnbs) >= 1:
            resolved["gnb1"] = probed_gnbs[0]

        if configured["gnb2"] not in nodes and len(probed_gnbs) >= 2:
            resolved["gnb2"] = probed_gnbs[1]

        for key in ["ue", "gnb1", "gnb2"]:
            if self.node_names.get(key) != resolved[key]:
                self.panes[key].append_log(
                    f"CLI node mapped: '{self.node_names.get(key)}' -> '{resolved[key]}'"
                )

        self.node_names = resolved

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
            return {"NCI": "N/A", "PCI": "N/A", "RRC UEs": "N/A", "NGAP UEs": "N/A", "NGAP Up": "N/A"}
        self.last_cli_error.pop(key, None)
        return {
            "NCI": out.get("nci", "N/A"),
            "PCI": out.get("pci", "N/A"),
            "RRC UEs": out.get("rrc-ue-count", "N/A"),
            "NGAP UEs": out.get("ngap-ue-count", "N/A"),
            "NGAP Up": out.get("ngap-up", "N/A").upper(),
        }

    def _amf_loop(self) -> None:
        amf = self.config["amf"]
        host = str(amf["host"])
        port = int(amf["port"])
        timeout = float(amf.get("connect_timeout_sec", 1.0))
        protocol = str(amf.get("protocol", "sctp")).strip().lower()
        active_probe = bool(amf.get("active_probe", protocol == "tcp"))
        last_state: Optional[bool] = None
        last_error: Optional[str] = None

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

            if last_state is None or last_state != ok:
                self.panes["amf"].append_log(
                    f"AMF reachability changed: {'reachable' if ok else 'unreachable'}"
                )
            if not ok and error and error != last_error:
                self.panes["amf"].append_log(f"connect {protocol} {host}:{port} failed: {error}")

            last_state = ok
            last_error = error
            self.stop_event.wait(self.poll_interval)

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
        self, host: str, port: int, timeout: float, protocol: str
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

    def _amf_log_loop(self) -> None:
        log_path = self._tail_amf_log_path()
        if log_path is None:
            self.panes["amf"].append_log("AMF log tail disabled (amf.log_file is not set)")
            return

        self.panes["amf"].append_log(f"AMF log tail enabled: {log_path}")
        notified_missing = False
        position = 0

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

        for i in range(2):
            gnb_cfg = self.config["gnbs"][i]
            gnb_cmd = [
                self._resolve(str(exe_cfg["nr_gnb"])),
                "-c",
                self._resolve(str(gnb_cfg["config"])),
                *[str(x) for x in gnb_cfg.get("args", [])],
            ]
            key = f"gnb{i + 1}"
            self.processes[key] = ManagedProcess(
                self.panes[key],
                gnb_cmd,
                self.workspace,
                self._process_log_path(key),
            )

        for proc in self.processes.values():
            proc.start()

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

    def _draw_pane(self, stdscr: curses.window, pane: EntityPane, y: int, x: int, h: int, w: int) -> None:
        box = stdscr.derwin(h, w, y, x)
        box.box()
        title = f" {pane.title} "
        if len(title) < (w - 2):
            box.addstr(0, 2, title)

        scalars, logs = pane.snapshot()
        row = 1
        for key, value in scalars.items():
            if row >= h - 2:
                break
            text = f"{key}: {value}"
            box.addnstr(row, 1, text, w - 2)
            row += 1

        if row < h - 2:
            box.hline(row, 1, ord("-"), w - 2)
            row += 1

        max_log_lines = max(0, h - row - 1)
        if max_log_lines > 0:
            tail = logs[-max_log_lines:]
            for line in tail:
                if row >= h - 1:
                    break
                box.addnstr(row, 1, line, w - 2)
                row += 1

    def _ui_loop(self, stdscr: curses.window) -> None:
        curses.curs_set(0)
        stdscr.nodelay(True)
        stdscr.timeout(200)

        while not self.stop_event.is_set():
            stdscr.erase()
            h, w = stdscr.getmaxyx()

            half_h = max(6, h // 2)
            half_w = max(20, w // 2)

            self._draw_pane(stdscr, self.panes["ue"], 0, 0, half_h, half_w)
            self._draw_pane(stdscr, self.panes["gnb1"], 0, half_w, half_h, w - half_w)
            self._draw_pane(stdscr, self.panes["gnb2"], half_h, 0, h - half_h, half_w)
            self._draw_pane(stdscr, self.panes["amf"], half_h, half_w, h - half_h, w - half_w)

            footer = "Press q to quit"
            if len(footer) < w:
                stdscr.addnstr(h - 1, max(0, w - len(footer) - 1), footer, len(footer))

            stdscr.refresh()

            ch = stdscr.getch()
            if ch in (ord("q"), ord("Q")):
                self.stop_event.set()

    def run(self) -> None:
        self._start_processes()

        status_thread = threading.Thread(target=self._status_loop, daemon=True)
        amf_thread = threading.Thread(target=self._amf_loop, daemon=True)
        amf_log_thread = threading.Thread(target=self._amf_log_loop, daemon=True)
        status_thread.start()
        amf_thread.start()
        amf_log_thread.start()

        try:
            curses.wrapper(self._ui_loop)
        finally:
            self.stop_event.set()
            for proc in self.processes.values():
                proc.stop()


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
    parser = argparse.ArgumentParser(description="UERANSIM live dashboard")
    parser.add_argument("--config", required=True, help="Path to dashboard JSON config")
    args = parser.parse_args()

    cfg_path = Path(args.config).resolve()
    workspace = cfg_path.parent.parent.parent.resolve()

    with cfg_path.open("r", encoding="utf-8") as f:
        config = json.load(f)

    validate_config(config)
    dashboard = Dashboard(config, workspace)
    dashboard.run()


if __name__ == "__main__":
    main()
