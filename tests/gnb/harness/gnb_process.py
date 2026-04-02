"""
gNB process lifecycle manager.

Starts ``nr-gnb`` as a subprocess, captures its log output, and provides
helpers to wait for specific log patterns or parse the current gNB state.

Usage::

    gnb = GnbProcess()
    gnb.generate_config(amf_addr="127.0.0.5", amf_port=38412)
    gnb.start()
    gnb.wait_for_log("NG Setup procedure is successful")
    # … interact …
    gnb.cleanup()
"""

from __future__ import annotations

import fcntl
import logging
import os
import re
import select
import shutil
import signal
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

import yaml

logger = logging.getLogger(__name__)

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
# Use cmake-build-release first, fall back to build/
_DEFAULT_BINARY = (
    _PROJECT_ROOT / "cmake-build-release" / "nr-gnb"
    if (_PROJECT_ROOT / "cmake-build-release" / "nr-gnb").exists()
    else _PROJECT_ROOT / "build" / "nr-gnb"
)


@dataclass
class GnbState:
    """Parsed gNB state from log output."""
    ng_setup_done: bool = False
    amf_connected: bool = False
    ue_count: int = 0
    meas_config_sent: bool = False
    handover_required_sent: bool = False
    handover_command_received: bool = False
    handover_completed: bool = False
    path_switch_sent: bool = False
    path_switch_ack_received: bool = False


class GnbProcess:
    """Manage the lifecycle of an ``nr-gnb`` process."""

    def __init__(
        self,
        binary: str | Path | None = None,
        config_path: str | Path | None = None,
        extra_args: List[str] | None = None,
    ):
        self._binary = Path(binary) if binary else _DEFAULT_BINARY
        self._config_path = Path(config_path) if config_path else None
        self._extra_args = extra_args or []
        self._proc: Optional[subprocess.Popen] = None
        self._log_lines: List[str] = []
        self._tmp_dir: Optional[str] = None

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
        mcc: str = "286",
        mnc: str = "01",
        nci: str = "0x000000010",
        id_length: int = 32,
        tac: int = 1,
        link_ip: str = "127.0.0.1",
        ngap_ip: str = "127.0.0.1",
        gtp_ip: str = "127.0.0.1",
        amf_addr: str = "127.0.0.5",
        amf_port: int = 38412,
        slices: Optional[list] = None,
        ignore_stream_ids: bool = True,
    ) -> Path:
        """Create a temporary gNB config YAML and return its path."""
        if slices is None:
            slices = [{"sst": 1, "sd": 1}]

        cfg = {
            "mcc": mcc,
            "mnc": mnc,
            "nci": nci,
            "idLength": id_length,
            "tac": tac,
            "linkIp": link_ip,
            "ngapIp": ngap_ip,
            "gtpIp": gtp_ip,
            "amfConfigs": [{"address": amf_addr, "port": amf_port}],
            "slices": slices,
            "ignoreStreamIds": ignore_stream_ids,
        }

        self._tmp_dir = tempfile.mkdtemp(prefix="ueransim_gnb_test_")
        config_path = Path(self._tmp_dir) / "test-gnb.yaml"
        with open(config_path, "w") as f:
            yaml.dump(cfg, f, default_flow_style=False)

        self._config_path = config_path
        logger.info("Generated gNB config at %s", config_path)
        return config_path

    # ------------------------------------------------------------------
    #  Lifecycle
    # ------------------------------------------------------------------

    def start(self, config_path: Optional[str | Path] = None):
        """Start the nr-gnb process."""
        if self._proc is not None:
            raise RuntimeError("gNB process already running")

        cfg = Path(config_path) if config_path else self._config_path
        if cfg is None:
            cfg = self.generate_config()

        if not self._binary.exists():
            raise FileNotFoundError(f"nr-gnb binary not found at {self._binary}")
        if not cfg.exists():
            raise FileNotFoundError(f"gNB config not found at {cfg}")

        cmd = [str(self._binary), "-c", str(cfg)] + self._extra_args
        self._proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        logger.info("Started nr-gnb (pid=%d) with config %s", self._proc.pid, cfg)

    def stop(self, timeout: float = 5.0):
        """Stop the nr-gnb process gracefully (SIGTERM), then force-kill."""
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
        logger.info("Stopped nr-gnb")

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
        """Read available stdout lines (non-blocking) and append to log."""
        if self._proc is None or self._proc.stdout is None:
            return

        fd = self._proc.stdout.fileno()
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
                    while b"\n" in partial:
                        line_bytes, partial = partial.split(b"\n", 1)
                        line = line_bytes.decode("utf-8", errors="replace")
                        clean = re.sub(r'\x1b\[[0-9;]*m', '', line)
                        if clean:
                            self._log_lines.append(clean)
        finally:
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
        """Wait until the gNB process has started (prints its name)."""
        return self.wait_for_log(r"UERANSIM|nr-gnb|started", timeout_s) is not None

    def wait_for_ng_setup(self, timeout_s: float = 15.0) -> bool:
        """Wait until NG Setup succeeds."""
        return self.wait_for_log(r"NG Setup procedure is successful", timeout_s) is not None

    def has_log(self, pattern: str) -> bool:
        """Check if any log line matches *pattern*."""
        self.collect_output(timeout_s=0.3)
        regex = re.compile(pattern)
        return any(regex.search(line) for line in self._log_lines)

    # ------------------------------------------------------------------
    #  State introspection from logs
    # ------------------------------------------------------------------

    def parse_state(self) -> GnbState:
        """Parse the latest gNB state from collected log lines."""
        self.collect_output(timeout_s=0.3)
        state = GnbState()

        for line in self._log_lines:
            if "NG Setup procedure is successful" in line:
                state.ng_setup_done = True
                state.amf_connected = True

            if "Sending MeasConfig to UE" in line:
                state.meas_config_sent = True

            if "Sending HandoverRequired for UE" in line:
                state.handover_required_sent = True

            if "HandoverCommand received from AMF" in line:
                state.handover_command_received = True

            if "Handover completed for UE" in line:
                state.handover_completed = True

            if "Sending PathSwitchRequest for UE" in line:
                state.path_switch_sent = True

            if "PathSwitchRequestAcknowledge received from AMF" in line:
                state.path_switch_ack_received = True

        return state

    # ------------------------------------------------------------------
    #  Handover-specific wait helpers
    # ------------------------------------------------------------------

    def wait_for_meas_config(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until gNB logs sending MeasConfig."""
        return self.wait_for_log(r"Sending MeasConfig to UE", timeout_s)

    def wait_for_meas_report(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until gNB logs receiving a MeasurementReport."""
        return self.wait_for_log(r"MeasurementReport from UE", timeout_s)

    def wait_for_handover_decision(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until gNB logs a handover decision."""
        return self.wait_for_log(r"Handover decision", timeout_s)

    def wait_for_handover_required(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until gNB logs sending HandoverRequired."""
        return self.wait_for_log(r"HandoverRequired sent to AMF", timeout_s)

    def wait_for_handover_command(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until gNB logs receiving HandoverCommand from AMF."""
        return self.wait_for_log(r"HandoverCommand received from AMF", timeout_s)

    def wait_for_handover_complete(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until gNB logs handover completion."""
        return self.wait_for_log(r"Handover completed for UE", timeout_s)

    def wait_for_path_switch(self, timeout_s: float = 15.0) -> Optional[str]:
        """Wait until gNB logs sending PathSwitchRequest."""
        return self.wait_for_log(r"PathSwitchRequest sent to AMF", timeout_s)
