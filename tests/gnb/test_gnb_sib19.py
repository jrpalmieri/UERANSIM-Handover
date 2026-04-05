from __future__ import annotations

import statistics
import struct
import subprocess
import time
from pathlib import Path

import pytest
import yaml

from .harness.fake_ue import FakeUe, RrcChannel
from .harness.gnb_process import GnbProcess
from .harness.marks import gnb_binary_exists, needs_pysctp

PROJECT_ROOT = Path(__file__).resolve().parents[2]
pytestmark = [gnb_binary_exists, needs_pysctp]


def _compute_node_name(config: dict) -> str:
    nci = int(str(config["nci"]), 0)
    id_length = int(config["idLength"])
    gnb_id = nci >> (36 - id_length)
    mcc = int(str(config["mcc"]))
    mnc = int(str(config["mnc"]))
    return f"UERANSIM-gnb-{mcc}-{mnc}-{gnb_id}"


def _run_loc_pv(node_name: str, x: float, y: float, z: float, vx: float, vy: float, vz: float, epoch_ms: int):
    cli_bin = PROJECT_ROOT / "build" / "nr-cli"
    cmd = f"loc-pv {x}:{y}:{z}:{vx}:{vy}:{vz}:{epoch_ms}"

    proc = subprocess.run(
        [str(cli_bin), node_name, "-e", cmd],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
    )

    assert proc.returncode == 0, (
        "nr-cli loc-pv failed:\n"
        f"stdout: {proc.stdout}\n"
        f"stderr: {proc.stderr}"
    )


def _extract_sib19_messages(fake_ue: FakeUe):
    return [m for m in fake_ue.dl_messages if m.channel == int(RrcChannel.DL_SIB19)]


def _wait_for_sib19_count(fake_ue: FakeUe, count: int, timeout_s: float) -> bool:
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        if len(_extract_sib19_messages(fake_ue)) >= count:
            return True
        time.sleep(0.05)
    return False


def _parse_sib19_payload(pdu: bytes) -> dict:
    assert len(pdu) == 96, f"Expected 96-byte SIB19 payload, got {len(pdu)}"
    return {
        "ephemerisType": pdu[0],
        "x": struct.unpack_from("<d", pdu, 4)[0],
        "y": struct.unpack_from("<d", pdu, 12)[0],
        "z": struct.unpack_from("<d", pdu, 20)[0],
        "vx": struct.unpack_from("<d", pdu, 28)[0],
        "vy": struct.unpack_from("<d", pdu, 36)[0],
        "vz": struct.unpack_from("<d", pdu, 44)[0],
        "epoch10ms": struct.unpack_from("<q", pdu, 52)[0],
        "kOffset": struct.unpack_from("<i", pdu, 60)[0],
        "taCommon": struct.unpack_from("<q", pdu, 64)[0],
        "taCommonDrift": struct.unpack_from("<i", pdu, 72)[0],
        "taCommonDriftVariation": struct.unpack_from("<i", pdu, 76)[0],
        "ulSyncValidity": struct.unpack_from("<i", pdu, 80)[0],
        "cellSpecificKoffset": struct.unpack_from("<i", pdu, 84)[0],
        "polarization": struct.unpack_from("<i", pdu, 88)[0],
        "taDrift": struct.unpack_from("<i", pdu, 92)[0],
    }


@pytest.fixture
def started_gnb_with_sib19(fake_amf) -> tuple[GnbProcess, dict, str]:
    gnb = GnbProcess()
    cfg_path = gnb.generate_config()

    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    sib19_cfg = {
        "sib19on": True,
        "sib19timing": 200,
        "kOffset": 160,
        "taCommon": 321,
        "taCommonDrift": 7,
        "taCommonDriftVariation": 11,
        "ulSyncValidityDuration": 900,
        "cellSpecificKoffset": 44,
        "polarization": 2,
        "taDrift": 19,
    }
    cfg["ntn"] = {"sib19": sib19_cfg}

    with open(cfg_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(cfg, f, sort_keys=False)

    node_name = _compute_node_name(cfg)

    gnb.start(cfg_path)
    if not gnb.wait_for_ng_setup(timeout_s=20):
        gnb.cleanup()
        pytest.skip("gNB did not complete NG setup for SIB19 test")

    yield gnb, sib19_cfg, node_name
    gnb.cleanup()


def test_gnb_loc_pv_updates_true_state_and_sib19_payload(started_gnb_with_sib19, fake_ue: FakeUe):
    _gnb, _sib19_cfg, node_name = started_gnb_with_sib19

    if not fake_ue.wait_for_heartbeat_ack(timeout_s=8):
        pytest.skip("Fake UE did not receive heartbeat ack in SIB19 test")

    # SIB19 should not be emitted before true position/velocity is set via loc-pv.
    time.sleep(0.6)
    assert len(_extract_sib19_messages(fake_ue)) == 0

    first_epoch = int(time.time() * 1000)
    _run_loc_pv(node_name, 1111.0, 2222.0, 3333.0, 0.0, 0.0, 0.0, first_epoch)

    assert _wait_for_sib19_count(fake_ue, 1, timeout_s=5.0), "No SIB19 received after first loc-pv"
    first_msg = _extract_sib19_messages(fake_ue)[-1]
    first_payload = _parse_sib19_payload(first_msg.raw_pdu)

    assert first_payload["ephemerisType"] == 0
    assert first_payload["x"] == pytest.approx(1111.0, abs=1e-6)
    assert first_payload["y"] == pytest.approx(2222.0, abs=1e-6)
    assert first_payload["z"] == pytest.approx(3333.0, abs=1e-6)
    assert first_payload["vx"] == pytest.approx(0.0, abs=1e-9)
    assert first_payload["vy"] == pytest.approx(0.0, abs=1e-9)
    assert first_payload["vz"] == pytest.approx(0.0, abs=1e-9)

    baseline_count = len(_extract_sib19_messages(fake_ue))
    second_epoch = int(time.time() * 1000)
    _run_loc_pv(node_name, 7777.0, 8888.0, 9999.0, 0.0, 0.0, 0.0, second_epoch)

    assert _wait_for_sib19_count(fake_ue, baseline_count + 1, timeout_s=5.0), "No SIB19 after second loc-pv"
    second_msg = _extract_sib19_messages(fake_ue)[-1]
    second_payload = _parse_sib19_payload(second_msg.raw_pdu)

    assert second_payload["x"] == pytest.approx(7777.0, abs=1e-6)
    assert second_payload["y"] == pytest.approx(8888.0, abs=1e-6)
    assert second_payload["z"] == pytest.approx(9999.0, abs=1e-6)


def test_gnb_sib19_format_and_periodicity(started_gnb_with_sib19, fake_ue: FakeUe):
    _gnb, sib19_cfg, node_name = started_gnb_with_sib19

    if not fake_ue.wait_for_heartbeat_ack(timeout_s=8):
        pytest.skip("Fake UE did not receive heartbeat ack in SIB19 periodicity test")

    _run_loc_pv(node_name, 1000.0, 2000.0, 3000.0, 1.5, -2.0, 0.5, int(time.time() * 1000))

    assert _wait_for_sib19_count(fake_ue, 6, timeout_s=8.0), "Did not receive enough SIB19 messages"
    sib19_msgs = _extract_sib19_messages(fake_ue)
    recent = sib19_msgs[-5:]

    parsed = [_parse_sib19_payload(m.raw_pdu) for m in recent]
    for p in parsed:
        assert p["ephemerisType"] == 0
        assert p["kOffset"] == sib19_cfg["kOffset"]
        assert p["taCommon"] == sib19_cfg["taCommon"]
        assert p["taCommonDrift"] == sib19_cfg["taCommonDrift"]
        assert p["taCommonDriftVariation"] == sib19_cfg["taCommonDriftVariation"]
        assert p["ulSyncValidity"] == sib19_cfg["ulSyncValidityDuration"]
        assert p["cellSpecificKoffset"] == sib19_cfg["cellSpecificKoffset"]
        assert p["polarization"] == sib19_cfg["polarization"]
        assert p["taDrift"] == sib19_cfg["taDrift"]

    intervals_ms = [
        (recent[i].timestamp - recent[i - 1].timestamp) * 1000.0
        for i in range(1, len(recent))
    ]
    expected_ms = float(sib19_cfg["sib19timing"])
    median_ms = statistics.median(intervals_ms)

    # Timer-driven dispatch can jitter under CI load; keep bounds broad but meaningful.
    assert expected_ms * 0.6 <= median_ms <= expected_ms * 1.8, (
        f"Unexpected SIB19 periodicity. intervals={intervals_ms}, median={median_ms}ms"
    )
    for dt in intervals_ms:
        assert expected_ms * 0.4 <= dt <= expected_ms * 2.5, (
            f"SIB19 interval out of bounds: {dt}ms, intervals={intervals_ms}"
        )
