from __future__ import annotations

from dataclasses import dataclass
import math
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List

import pytest
import yaml

from .harness.gnb_process import GnbProcess

PROJECT_ROOT = Path(__file__).resolve().parents[2]
TESTS_ROOT = Path(__file__).resolve().parents[1]
if str(TESTS_ROOT) not in sys.path:
    sys.path.insert(0, str(TESTS_ROOT))

from ue.harness.ue_process import UeProcess

NR_CLI = PROJECT_ROOT / "build" / "nr-cli"
GNB_CONFIG = PROJECT_ROOT / "config" / "custom-gnb.yaml"
UE_CONFIG = PROJECT_ROOT / "config" / "custom-ue.yaml"


def _config_exists(path: Path) -> bool:
    return path.exists()


sat_time_runtime_ready = pytest.mark.skipif(
    not NR_CLI.exists() or not _config_exists(GNB_CONFIG) or not _config_exists(UE_CONFIG),
    reason="sat-time runtime test prerequisites are missing (nr-cli, open5gs-gnb.yaml, custom-ue.yaml)",
)


@dataclass
class RunningPair:
    gnb_node: str
    ue_node: str
    gnb: GnbProcess
    ue: UeProcess


def _log_tail(lines: List[str], tail: int = 40) -> List[str]:
    if len(lines) <= tail:
        return lines
    return lines[-tail:]


def _print_runtime_logs(pair: RunningPair, label: str, tail: int = 40) -> None:
    pair.gnb.collect_output(timeout_s=0.3)
    pair.ue.collect_output(timeout_s=0.3)

    print(f"\n==== {label}: gNB stdout tail ====")
    for line in _log_tail(pair.gnb.log_lines, tail=tail):
        print(f"[gNB] {line}")

    print(f"==== {label}: UE stdout tail ====")
    for line in _log_tail(pair.ue.log_lines, tail=tail):
        print(f"[UE] {line}")


def _print_cli_exchange(cmd: List[str], proc: subprocess.CompletedProcess[str]) -> None:
    print(f"[nr-cli] cmd: {' '.join(cmd)}")
    print(f"[nr-cli] exit={proc.returncode}")

    stdout = proc.stdout.strip()
    stderr = proc.stderr.strip()

    if stdout:
        print("[nr-cli] stdout:")
        for line in stdout.splitlines():
            print(f"  {line}")

    if stderr:
        print("[nr-cli] stderr:")
        for line in stderr.splitlines():
            print(f"  {line}")


def _assert_cli_audit_entries(
    pair: RunningPair,
    node: str,
    command_markers: List[str],
    response_marker: str,
) -> None:
    process = pair.gnb if node == pair.gnb_node else pair.ue
    process.collect_output(timeout_s=0.3)

    lines = process.log_lines
    for marker in command_markers:
        received = any("CLI command received" in line and marker in line for line in lines)
        assert received, f"Missing CLI received audit log for node={node}, marker='{marker}'"

        responded = any(
            ("CLI result" in line or "CLI error" in line)
            and marker in line
            and response_marker in line
            for line in lines
        )
        assert responded, f"Missing CLI response audit log for node={node}, marker='{marker}'"


def _check_gnb_startup_health(gnb: GnbProcess) -> None:
    gnb.collect_output(timeout_s=0.2)
    lines = gnb.log_lines
    log_text = "\n".join(lines)
    log_text_lc = log_text.lower()

    known_markers = [
        "ntn.tle lines must be at least 69 characters",
        "field 'a3offsetdb' is too small",
        "error:",
    ]
    for marker in known_markers:
        if marker in log_text_lc:
            tail = "\n".join(_log_tail(lines, tail=20))
            raise AssertionError(
                "gNB startup failed due to configuration/runtime error. "
                "Check config/custom-gnb.yaml.\n"
                f"Detected marker: {marker}\n"
                f"gNB log tail:\n{tail}"
            )

    if not gnb.is_running():
        tail = "\n".join(_log_tail(lines, tail=20))
        raise AssertionError(
            "gNB process exited before node discovery completed.\n"
            f"gNB log tail:\n{tail}"
        )


@pytest.fixture
def running_gnb_ue_pair() -> RunningPair:
    gnb = GnbProcess(config_path=GNB_CONFIG)
    ue = UeProcess(config_path=UE_CONFIG, extra_args=["-r"])

    try:
        gnb.start()
        ue.start()

        with open(UE_CONFIG, "r", encoding="utf-8") as f:
            ue_cfg = yaml.safe_load(f)
        ue_node = ue_cfg["supi"]

        gnb_node = _wait_for_node(prefix="UERANSIM-gnb-", timeout_s=20.0, gnb=gnb)
        _wait_for_node(name=ue_node, timeout_s=20.0)

        pair = RunningPair(
            gnb_node=gnb_node,
            ue_node=ue_node,
            gnb=gnb,
            ue=ue,
        )

        _print_runtime_logs(pair, "startup")

        yield pair
    finally:
        try:
            pair = RunningPair(
                gnb_node="",
                ue_node="",
                gnb=gnb,
                ue=ue,
            )
            _print_runtime_logs(pair, "teardown")
        except Exception:
            pass
        ue.cleanup()
        gnb.cleanup()


def _cli_dump() -> str:
    cmd = [str(NR_CLI), "--dump"]
    proc = subprocess.run(
        cmd,
        check=False,
        capture_output=True,
        text=True,
    )
    _print_cli_exchange(cmd, proc)
    if proc.returncode != 0:
        raise AssertionError(f"nr-cli --dump failed: {proc.stderr.strip()}")
    return proc.stdout


def _wait_for_node(
    prefix: str | None = None,
    name: str | None = None,
    timeout_s: float = 15.0,
    gnb: GnbProcess | None = None,
) -> str:
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        if gnb is not None:
            _check_gnb_startup_health(gnb)

        dump = _cli_dump()
        nodes = [line.strip() for line in dump.splitlines() if line.strip()]

        if name is not None and name in nodes:
            return name

        if prefix is not None:
            matches = [n for n in nodes if n.startswith(prefix)]
            if matches:
                return matches[0]

        time.sleep(0.3)

    target = name if name is not None else prefix
    raise AssertionError(f"Node '{target}' did not appear in nr-cli --dump within {timeout_s:.1f}s")


def _exec_sat_time(node: str, command: str) -> str:
    cmd = [str(NR_CLI), node, "--exec", command]
    proc = subprocess.run(
        cmd,
        check=False,
        capture_output=True,
        text=True,
    )
    _print_cli_exchange(cmd, proc)
    if proc.returncode != 0:
        stderr = proc.stderr.strip()
        stdout = proc.stdout.strip()
        msg = (
            f"nr-cli command failed: node={node}, cmd='{command}', "
            f"stdout='{stdout}', stderr='{stderr}'"
        )
        raise AssertionError(msg)
    return proc.stdout


def _parse_status(raw: str) -> Dict[str, str]:
    status: Dict[str, str] = {}
    for line in raw.splitlines():
        line = line.strip()
        if not line or ":" not in line:
            continue
        key, value = line.split(":", 1)
        status[key.strip()] = value.strip()
    return status


def _get_status(node: str) -> Dict[str, str]:
    return _parse_status(_exec_sat_time(node, "sat-time"))


def _get_status_quiet(node: str) -> Dict[str, str]:
    return _parse_status(_exec_sat_time(node, "sat-time"))


def _to_int(status: Dict[str, str], key: str) -> int:
    assert key in status, f"Missing status field: {key}, status={status}"
    return int(status[key])


def _to_float(status: Dict[str, str], key: str) -> float:
    assert key in status, f"Missing status field: {key}, status={status}"
    return float(status[key])


def _assert_progress_matches(
    earlier: Dict[str, str],
    later: Dict[str, str],
    tolerance_ms: int = 35,
) -> None:
    sat_delta = _to_int(later, "sat-time-ms") - _to_int(earlier, "sat-time-ms")
    wall_delta = _to_int(later, "wallclock-ms") - _to_int(earlier, "wallclock-ms")
    scale = _to_float(earlier, "tick-scaling")
    expected = int(round(wall_delta * scale))
    delta = abs(sat_delta - expected)
    assert delta <= tolerance_ms, (
        "sat-time progression mismatch: "
        f"sat_delta={sat_delta}, wall_delta={wall_delta}, scale={scale}, expected={expected}, "
        f"tolerance={tolerance_ms}"
    )


def _assert_paused_freezes(earlier: Dict[str, str], later: Dict[str, str]) -> None:
    sat_delta = abs(_to_int(later, "sat-time-ms") - _to_int(earlier, "sat-time-ms"))
    wall_delta = _to_int(later, "wallclock-ms") - _to_int(earlier, "wallclock-ms")
    assert wall_delta >= 100, f"Wallclock did not advance enough while paused: wall_delta={wall_delta}"
    assert sat_delta <= 3, f"Paused sat-time should be stable, sat_delta={sat_delta}, wall_delta={wall_delta}"


@sat_time_runtime_ready
class TestSatTimeRuntime:
    def test_sat_time_controls_on_gnb_and_ue(self, running_gnb_ue_pair: RunningPair):
        nodes = [running_gnb_ue_pair.gnb_node, running_gnb_ue_pair.ue_node]

        for node in nodes:
            command_markers = [
                "sat-time run",
                "sat-time",
                "sat-time pause",
                "sat-time tickscale=0.500000",
                "sat-time start-epoch=23162.59097222",
            ]

            _exec_sat_time(node, "sat-time run")
            run_a = _get_status(node)
            time.sleep(0.25)
            run_b = _get_status(node)
            _assert_progress_matches(run_a, run_b)

            _exec_sat_time(node, "sat-time pause")
            paused = _get_status(node)
            assert paused.get("paused") == "true"
            time.sleep(0.2)
            paused_later = _get_status(node)
            _assert_paused_freezes(paused, paused_later)

            _exec_sat_time(node, "sat-time run")
            running = _get_status(node)
            assert running.get("paused") == "false"

            _exec_sat_time(node, "sat-time tickscale=0.5")
            scaled = _get_status(node)
            assert scaled.get("tick-scaling") == "0.500000"
            time.sleep(0.3)
            scaled_later = _get_status(node)
            _assert_progress_matches(scaled, scaled_later)

            _exec_sat_time(node, "sat-time start-epoch=23162.59097222")
            reset = _get_status(node)
            assert reset.get("start-epoch-ms") == "1686492660000"
            assert int(reset.get("sat-time-ms", "0")) >= 1686492660000

            _assert_cli_audit_entries(running_gnb_ue_pair, node, command_markers, "sat-time-ms")

        _print_runtime_logs(running_gnb_ue_pair, "post-control-test")

    def test_sat_time_pause_at_wallclock_on_gnb_and_ue(self, running_gnb_ue_pair: RunningPair):
        nodes = [running_gnb_ue_pair.gnb_node, running_gnb_ue_pair.ue_node]

        for node in nodes:
            _exec_sat_time(node, "sat-time run")
            pause_at = int(time.time() * 1000) + 1200
            _exec_sat_time(node, f"sat-time pause-at-wallclock={pause_at}")

            after_set = _get_status(node)
            assert after_set.get("pause-at-wallclock-ms") == str(pause_at)

            end = time.monotonic() + 4.0
            final = after_set
            while time.monotonic() < end:
                final = _get_status_quiet(node)
                if final.get("paused") == "true":
                    break
                time.sleep(0.2)

            print(f"\n---- CLI: node={node} cmd='sat-time (final after schedule)' ----")
            for key in [
                "sat-time-ms",
                "wallclock-ms",
                "start-epoch-ms",
                "tick-scaling",
                "paused",
                "pause-at-wallclock-ms",
            ]:
                if key in final:
                    print(f"{key}: {final[key]}")

            assert final.get("paused") == "true"

            command_markers = [
                "sat-time run",
                "sat-time pause-at-wallclock=",
                "sat-time",
            ]
            _assert_cli_audit_entries(running_gnb_ue_pair, node, command_markers, "sat-time-ms")

        _print_runtime_logs(running_gnb_ue_pair, "post-scheduled-pause-test")
