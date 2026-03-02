"""
UE process lifecycle manager.

Starts ``nr-ue`` as a subprocess, captures its log output, and provides
helpers to wait for specific log lines or parse the current UE state.
"""

from __future__ import annotations

import os
import re
import signal
import subprocess
import tempfile
import time
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

import yaml

# Default binary path (relative to project root)
_PROJECT_ROOT = Path(__file__).resolve().parents[2]
_DEFAULT_BINARY = _PROJECT_ROOT / "build" / "nr-ue"
_TEMPLATE_CONFIG = _PROJECT_ROOT / "config" / "custom-ue.yaml"


@dataclass
class UeState:
    """Parsed UE state from log output."""
    rrc_state: str = "RRC_IDLE"
    rm_state: str = "RM_DEREGISTERED"
    cm_state: str = "CM_IDLE"
    mm_state: str = "MM_DEREGISTERED"
    mm_sub_state: str = ""
    registered: bool = False
    connected: bool = False
    cell_id: int = 0
    # Handover-related
    handover_completed: bool = False
    handover_failed: bool = False
    handover_target_pci: int = 0
    handover_source_cell: int = 0
    handover_target_cell: int = 0


class UeProcess:
    """Manage the lifecycle of an ``nr-ue`` process."""

    def __init__(
        self,
        binary: str | Path | None = None,
        config_path: str | Path | None = None,
        extra_args: List[str] | None = None,
        oob_meas_port: int = 7200,
        gnb_search_addr: str = "127.0.0.1",
        gnb_search_list: List[str] | None = None,
    ):
        self._binary = Path(binary) if binary else _DEFAULT_BINARY
        self._config_path = Path(config_path) if config_path else None
        self._extra_args = extra_args or []
        self._oob_meas_port = oob_meas_port
        self._gnb_search_list = gnb_search_list or [gnb_search_addr]
        self._proc: Optional[subprocess.Popen] = None
        self._log_lines: List[str] = []
        self._tmp_dir: Optional[str] = None
        self._master_fd: Optional[int] = None
        self._pty_file = None

    @property
    def pid(self) -> Optional[int]:
        return self._proc.pid if self._proc else None

    @property
    def log_output(self) -> str:
        return "\n".join(self._log_lines)

    @property
    def log_lines(self) -> List[str]:
        return list(self._log_lines)

    # ------------------------------------------------------------------
    #  Config generation
    # ------------------------------------------------------------------

    def generate_config(
        self,
        supi: str = "imsi-286010000000001",
        mcc: str = "286",
        mnc: str = "93",
        key: str = "465B5CE8B199B49FAA5F0A2EE238A6BC",
        op: str = "E8ED289DEBA952E4283B54E88E6183CA",
        op_type: str = "OP",
        sessions: Optional[list] = None,
        meas_source_type: str = "UDP",
        meas_udp_port: int = 7200,
    ) -> Path:
        """Create a temporary UE config YAML and return its path."""
        cfg = {
            "supi": supi,
            "mcc": mcc,
            "mnc": mnc,
            "key": key,
            "op": op,
            "opType": op_type,
            "amf": "8000",
            "imei": "356938035643803",
            "imeiSv": "4370816125816151",
            "gnbSearchList": self._gnb_search_list,
            "uacAic": {"mps": False, "mcs": False},
            "uacAcc": {
                "normalClass": 0,
                "class11": False,
                "class12": False,
                "class13": False,
                "class14": False,
                "class15": False,
            },
            "integrity": {"IA1": True, "IA2": True, "IA3": True},
            "ciphering": {"EA1": True, "EA2": True, "EA3": True},
            "integrityMaxRate": {"uplink": "full", "downlink": "full"},
        }

        # Sessions — empty by default to avoid TUN creation
        if sessions is not None:
            cfg["sessions"] = sessions

        # NSSAI
        cfg["configured-nssai"] = [{"sst": 1, "sd": 1}]
        cfg["default-nssai"] = [{"sst": 1, "sd": 1}]

        # OOB measurement source
        if meas_source_type.upper() != "NONE":
            cfg["measurementSource"] = {
                "type": meas_source_type.lower(),
                "port": meas_udp_port,
            }

        self._tmp_dir = tempfile.mkdtemp(prefix="ueransim_test_")
        config_path = Path(self._tmp_dir) / "test-ue.yaml"
        with open(config_path, "w") as f:
            yaml.dump(cfg, f, default_flow_style=False)

        self._config_path = config_path
        return config_path

    # ------------------------------------------------------------------
    #  Lifecycle
    # ------------------------------------------------------------------

    def start(self, config_path: Optional[str | Path] = None):
        """Start the nr-ue process."""
        if self._proc is not None:
            raise RuntimeError("UE process already running")

        cfg = Path(config_path) if config_path else self._config_path
        if cfg is None:
            cfg = self.generate_config()

        if not self._binary.exists():
            raise FileNotFoundError(f"nr-ue binary not found at {self._binary}")
        if not cfg.exists():
            raise FileNotFoundError(f"UE config not found at {cfg}")

        cmd = [str(self._binary), "-c", str(cfg)] + self._extra_args
        self._proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

    def stop(self, timeout: float = 5.0):
        """Stop the nr-ue process gracefully (SIGTERM), then force-kill."""
        if self._proc is None:
            return
        try:
            self._proc.send_signal(signal.SIGTERM)
            self._proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait(timeout=2)
        finally:
            self._proc = None
            if self._pty_file is not None:
                try:
                    self._pty_file.close()
                except OSError:
                    pass
                self._pty_file = None
                self._master_fd = None

    def cleanup(self):
        """Stop process and remove temporary files."""
        self.stop()
        if self._tmp_dir and os.path.isdir(self._tmp_dir):
            shutil.rmtree(self._tmp_dir, ignore_errors=True)

    def is_running(self) -> bool:
        if self._proc is None:
            return False
        return self._proc.poll() is None

    # ------------------------------------------------------------------
    #  Log collection
    # ------------------------------------------------------------------

    def collect_output(self, timeout_s: float = 0.5):
        """Read available stdout lines (non-blocking) and append to log.

        Uses raw non-blocking reads on the underlying file descriptor to
        avoid a subtle issue where ``select`` + ``readline()`` on a
        ``TextIOWrapper`` can miss data that was already buffered inside
        Python's I/O stack (select reports the OS pipe as not-ready while
        the BufferedReader already consumed the bytes).
        """
        if self._proc is None or self._proc.stdout is None:
            return
        import select
        import fcntl

        fd = self._proc.stdout.fileno()

        # Set non-blocking on the raw FD
        fl = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

        partial = b""
        end = time.monotonic() + timeout_s
        try:
            while time.monotonic() < end:
                remaining = end - time.monotonic()
                if remaining <= 0:
                    break
                wait = min(remaining, 0.1)
                ready, _, _ = select.select([fd], [], [], wait)
                if ready:
                    try:
                        data = os.read(fd, 65536)
                    except BlockingIOError:
                        continue
                    if not data:
                        break  # EOF
                    partial += data
                    # Process complete lines
                    while b"\n" in partial:
                        line_bytes, partial = partial.split(b"\n", 1)
                        line = line_bytes.decode("utf-8", errors="replace")
                        clean = re.sub(r'\x1b\[[0-9;]*m', '', line)
                        if clean:
                            self._log_lines.append(clean)
        finally:
            # Restore blocking mode
            fcntl.fcntl(fd, fcntl.F_SETFL, fl)

    def wait_for_log(self, pattern: str, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until a log line matching *pattern* appears.

        Returns the matching line, or None on timeout.
        """
        regex = re.compile(pattern)
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            self.collect_output(timeout_s=0.3)
            for line in self._log_lines:
                if regex.search(line):
                    return line
            time.sleep(0.2)
        return None

    def wait_for_startup(self, timeout_s: float = 10.0) -> bool:
        """Wait until the UE process has printed its startup banner."""
        return self.wait_for_log(r"UERANSIM|nr-ue|started|searching", timeout_s) is not None

    # ------------------------------------------------------------------
    #  State introspection from logs
    # ------------------------------------------------------------------

    def parse_state(self) -> UeState:
        """Parse the latest UE state from collected log lines."""
        self.collect_output(timeout_s=0.3)
        state = UeState()

        for line in self._log_lines:
            # RRC state changes
            if "RRC-IDLE" in line or "switched to RRC-IDLE" in line.upper():
                state.rrc_state = "RRC_IDLE"
                state.connected = False
            elif "RRC Release received" in line:
                # UE doesn't log "RRC-IDLE" explicitly, but RRC Release → IDLE
                state.rrc_state = "RRC_IDLE"
                state.connected = False
            elif "RRC-CONNECTED" in line or "switched to RRC-CONNECTED" in line.upper():
                state.rrc_state = "RRC_CONNECTED"
                state.connected = True
            elif "RRC-INACTIVE" in line:
                state.rrc_state = "RRC_INACTIVE"

            # RM state — the UE doesn't log "RM-REGISTERED" directly,
            # but RM state is implied by MM state:
            #   MM_REGISTERED  → RM_REGISTERED
            #   MM_DEREGISTERED → RM_DEREGISTERED
            if "RM-REGISTERED" in line:
                state.rm_state = "RM_REGISTERED"
                state.registered = True
            elif "RM-DEREGISTERED" in line:
                state.rm_state = "RM_DEREGISTERED"

            # CM state
            if "CM-CONNECTED" in line:
                state.cm_state = "CM_CONNECTED"
            elif "CM-IDLE" in line:
                state.cm_state = "CM_IDLE"

            # MM state
            if "MM-DEREGISTERED" in line:
                state.mm_state = "MM_DEREGISTERED"
                state.rm_state = "RM_DEREGISTERED"
                state.registered = False
            elif "MM-REGISTERED-INITIATED" in line or "MM-REGISTER-INITIATED" in line:
                state.mm_state = "MM_REGISTERED_INITIATED"
            elif "MM-REGISTERED" in line and "INITIATED" not in line:
                state.mm_state = "MM_REGISTERED"
                state.rm_state = "RM_REGISTERED"
                state.registered = True

            # MM sub-state
            m = re.search(r"MM state is (.+?)(?:\.|$)", line)
            if m:
                state.mm_sub_state = m.group(1).strip()

            # Handover
            if "Handover to cell" in line and "completed" in line:
                state.handover_completed = True
                m = re.search(r"PCI=(\d+)", line)
                if m:
                    state.handover_target_pci = int(m.group(1))
            if "Handover failure" in line or "T304 expired" in line:
                state.handover_failed = True
            m = re.search(r"Serving cell switched: cell\[(\d+)\].*cell\[(\d+)\]", line)
            if m:
                state.handover_source_cell = int(m.group(1))
                state.handover_target_cell = int(m.group(2))

        return state

    def has_log(self, pattern: str) -> bool:
        """Check if any log line matches *pattern*."""
        regex = re.compile(pattern)
        return any(regex.search(line) for line in self._log_lines)

    # ------------------------------------------------------------------
    #  Handover-specific helpers
    # ------------------------------------------------------------------

    def wait_for_handover_complete(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until the UE logs a successful handover completion."""
        return self.wait_for_log(r"Handover to cell\[\d+\] completed", timeout_s)

    def wait_for_handover_failure(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until the UE logs a handover failure (target not found or T304 expiry)."""
        return self.wait_for_log(
            r"Handover failure|T304 expired", timeout_s
        )

    def wait_for_handover_command(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until the UE logs reception of a handover command."""
        return self.wait_for_log(r"Handover command: targetPCI=", timeout_s)

    def wait_for_cell_switch(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until the UE logs a cell switch."""
        return self.wait_for_log(r"Serving cell switched", timeout_s)

    def wait_for_reconfig_with_sync(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until the UE logs detection of ReconfigurationWithSync."""
        return self.wait_for_log(r"ReconfigurationWithSync detected", timeout_s)

    def parse_handover_info(self) -> dict:
        """Extract handover info from log lines.

        Returns a dict with keys:
          - command_received (bool)
          - completed (bool)
          - failed (bool)
          - target_pci (int or None)
          - source_cell (int or None)
          - target_cell (int or None)
          - t304_expired (bool)
          - rlf_triggered (bool)
        """
        self.collect_output(timeout_s=0.3)
        info: dict = {
            "command_received": False,
            "completed": False,
            "failed": False,
            "target_pci": None,
            "source_cell": None,
            "target_cell": None,
            "t304_expired": False,
            "rlf_triggered": False,
        }

        for line in self._log_lines:
            # "Handover command: targetPCI=X newC-RNTI=Y t304=Zms"
            m = re.search(r"Handover command: targetPCI=(\d+)", line)
            if m:
                info["command_received"] = True
                info["target_pci"] = int(m.group(1))

            # "Serving cell switched: cell[X] → cell[Y]"
            m = re.search(r"Serving cell switched: cell\[(\d+)\].*cell\[(\d+)\]", line)
            if m:
                info["source_cell"] = int(m.group(1))
                info["target_cell"] = int(m.group(2))

            # "Handover to cell[X] completed (PCI=Y, newC-RNTI=Z)"
            if "Handover to cell" in line and "completed" in line:
                info["completed"] = True

            # "Handover failure: target PCI X not found"
            if "Handover failure" in line:
                info["failed"] = True

            # "T304 expired – handover to PCI X failed"
            if "T304 expired" in line:
                info["t304_expired"] = True
                info["failed"] = True

            # Radio link failure (triggered by HO failure or T304 expiry)
            if "Radio link failure" in line or "radio-link-failure" in line.lower():
                info["rlf_triggered"] = True

        return info
