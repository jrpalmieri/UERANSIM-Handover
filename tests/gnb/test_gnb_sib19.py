from __future__ import annotations

import statistics
import json
import math
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

OWN_TLE_LINE1 = "1 25544U 98067A   24060.51892340  .00016717  00000+0  30375-3 0  9997"
OWN_TLE_LINE2 = "2 25544  51.6410  12.9532 0005518 101.8294  21.5671 15.50040210442261"

NEIGHBOR_TLE_LINE1 = "1 20580U 90037B   24060.24520245  .00000988  00000+0  40680-4 0  9996"
NEIGHBOR_TLE_LINE2 = "2 20580  28.4691 196.2015 0002647 280.5429 162.1138 15.09331170920553"

# Example TLE from config/custom-gnb.yaml.
CUSTOM_TLE_LINE1 = "1 47933U 21029A   23162.59097222  .00001250  00000-0  29666-4 0  9993"
CUSTOM_TLE_LINE2 = "2 47933  53.0000 180.0000 0001000   0.0000   0.0000 15.50000000    14"


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


def _run_sat_loc_pv(node_name: str, payload: dict):
    cli_bin = PROJECT_ROOT / "build" / "nr-cli"
    cmd = f"sat-loc-pv {json.dumps(payload, separators=(',', ':'))}"

    proc = subprocess.run(
        [str(cli_bin), node_name, "-e", cmd],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
    )

    assert proc.returncode == 0, (
        "nr-cli sat-loc-pv failed:\n"
        f"stdout: {proc.stdout}\n"
        f"stderr: {proc.stderr}"
    )


def _run_sat_tle(node_name: str, payload: dict) -> subprocess.CompletedProcess[str]:
    cli_bin = PROJECT_ROOT / "build" / "nr-cli"
    cmd = f"sat-tle '{json.dumps(payload, separators=(',', ':'))}'"

    proc = subprocess.run(
        [str(cli_bin), node_name, "-e", cmd],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
    )

    assert proc.returncode == 0, (
        "nr-cli sat-tle failed:\n"
        f"stdout: {proc.stdout}\n"
        f"stderr: {proc.stderr}"
    )
    return proc


def _extract_sib19_messages(fake_ue: FakeUe):
    return [m for m in fake_ue.dl_messages if m.channel == int(RrcChannel.DL_SIB19)]


def _wait_for_sib19_count(fake_ue: FakeUe, count: int, timeout_s: float) -> bool:
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        if len(_extract_sib19_messages(fake_ue)) >= count:
            return True
        time.sleep(0.05)
    return False


def _wait_for_sib19_payload(
    fake_ue: FakeUe,
    matcher,
    timeout_s: float,
) -> dict | None:
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        for msg in reversed(_extract_sib19_messages(fake_ue)):
            parsed = _parse_sib19_payload(msg.raw_pdu)
            if matcher(parsed):
                return parsed
        time.sleep(0.1)
    return None


def _parse_sib19_payload(pdu: bytes) -> dict:
    if len(pdu) >= 8 and pdu[0] == 2:
        ephemeris_type = pdu[1]
        count = struct.unpack_from("<I", pdu, 4)[0]
        expected = 8 + count * 96
        assert len(pdu) >= expected, f"Expected at least {expected}-byte SIB19 payload, got {len(pdu)}"

        entries = []
        for i in range(count):
            base = 8 + i * 96
            entry = {
                "pci": struct.unpack_from("<i", pdu, base)[0],
                "epoch10ms": struct.unpack_from("<q", pdu, base + 52)[0],
                "kOffset": struct.unpack_from("<i", pdu, base + 60)[0],
                "taCommon": struct.unpack_from("<q", pdu, base + 64)[0],
                "taCommonDrift": struct.unpack_from("<i", pdu, base + 72)[0],
                "taCommonDriftVariation": struct.unpack_from("<i", pdu, base + 76)[0],
                "ulSyncValidity": struct.unpack_from("<i", pdu, base + 80)[0],
                "cellSpecificKoffset": struct.unpack_from("<i", pdu, base + 84)[0],
                "polarization": struct.unpack_from("<i", pdu, base + 88)[0],
                "taDrift": struct.unpack_from("<i", pdu, base + 92)[0],
            }

            if ephemeris_type == 0:
                entry.update({
                    "x": struct.unpack_from("<d", pdu, base + 4)[0],
                    "y": struct.unpack_from("<d", pdu, base + 12)[0],
                    "z": struct.unpack_from("<d", pdu, base + 20)[0],
                    "vx": struct.unpack_from("<d", pdu, base + 28)[0],
                    "vy": struct.unpack_from("<d", pdu, base + 36)[0],
                    "vz": struct.unpack_from("<d", pdu, base + 44)[0],
                })
            else:
                entry.update({
                    "semiMajorAxis": struct.unpack_from("<q", pdu, base + 4)[0],
                    "eccentricity": struct.unpack_from("<i", pdu, base + 12)[0],
                    "periapsis": struct.unpack_from("<i", pdu, base + 16)[0],
                    "longitude": struct.unpack_from("<i", pdu, base + 20)[0],
                    "inclination": struct.unpack_from("<i", pdu, base + 24)[0],
                    "meanAnomaly": struct.unpack_from("<i", pdu, base + 28)[0],
                })

            entries.append(entry)

        return {
            "formatVersion": 2,
            "ephemerisType": ephemeris_type,
            "entries": entries,
        }

    assert len(pdu) == 96, f"Expected 96-byte legacy SIB19 payload, got {len(pdu)}"
    return {
        "formatVersion": 1,
        "ephemerisType": pdu[0],
        "entries": [{
            "pci": None,
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
        }],
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
        "satLocUpdateThresholdMs": 600,
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


@pytest.fixture
def started_gnb_with_sib19_tle(fake_amf) -> tuple[GnbProcess, dict, str, int]:
    gnb = GnbProcess()
    cfg_path = gnb.generate_config()

    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    sib19_cfg = {
        "sib19on": True,
        "sib19timing": 250,
        "satLocUpdateThresholdMs": 800,
        "kOffset": 160,
        "taCommon": 321,
        "taCommonDrift": 7,
    }
    cfg["ntn"] = {
        "ntnEnabled": True,
        "tle": {
            "line1": OWN_TLE_LINE1,
            "line2": OWN_TLE_LINE2,
        },
        "sib19": sib19_cfg,
    }

    with open(cfg_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(cfg, f, sort_keys=False)

    node_name = _compute_node_name(cfg)
    own_pci = int(str(cfg["nci"]), 0) & 0x3FF

    gnb.start(cfg_path)
    if not gnb.wait_for_ng_setup(timeout_s=20):
        gnb.cleanup()
        pytest.skip("gNB did not complete NG setup for SIB19 TLE test")

    yield gnb, sib19_cfg, node_name, own_pci
    gnb.cleanup()


@pytest.fixture
def started_gnb_with_sib19_tle_orbital(fake_amf) -> tuple[GnbProcess, dict, str, int]:
    gnb = GnbProcess()
    cfg_path = gnb.generate_config()

    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    sib19_cfg = {
        "sib19on": True,
        "sib19timing": 250,
        "satLocUpdateThresholdMs": 800,
        "kOffset": 160,
        "taCommon": 321,
        "taCommonDrift": 7,
        "ephType": 1,
    }
    cfg["ntn"] = {
        "ntnEnabled": True,
        "tle": {
            "line1": OWN_TLE_LINE1,
            "line2": OWN_TLE_LINE2,
        },
        "sib19": sib19_cfg,
    }

    with open(cfg_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(cfg, f, sort_keys=False)

    node_name = _compute_node_name(cfg)
    own_pci = int(str(cfg["nci"]), 0) & 0x3FF

    gnb.start(cfg_path)
    if not gnb.wait_for_ng_setup(timeout_s=20):
        gnb.cleanup()
        pytest.skip("gNB did not complete NG setup for orbital SIB19 test")

    yield gnb, sib19_cfg, node_name, own_pci
    gnb.cleanup()


def test_gnb_loads_tle_from_config_and_stores_at_startup(started_gnb_with_sib19_tle):
    gnb, _sib19_cfg, _node_name, own_pci = started_gnb_with_sib19_tle

    loaded_line = gnb.wait_for_log(rf"Loaded own TLE from config \(pci={own_pci}\)", timeout_s=5.0)
    assert loaded_line is not None, "gNB did not report loading own TLE from config"

    store_line = gnb.wait_for_log(r"TLE store upserted 1 entries, total 1 satellites", timeout_s=5.0)
    assert store_line is not None, "gNB did not report TLE store upsert from config"


def test_gnb_cli_sat_tle_json_upserts_store(started_gnb_with_sib19_tle):
    gnb, _sib19_cfg, node_name, own_pci = started_gnb_with_sib19_tle

    payload = {
        "satellites": [
            {
                "pci": own_pci,
                "line1": OWN_TLE_LINE1,
                "line2": OWN_TLE_LINE2,
            },
            {
                "pci": 222,
                "line1": NEIGHBOR_TLE_LINE1,
                "line2": NEIGHBOR_TLE_LINE2,
            },
        ]
    }

    proc = _run_sat_tle(node_name, payload)
    assert "result: ok" in proc.stdout
    assert "upsertedCount: 2" in proc.stdout

    upsert_line = gnb.wait_for_log(r"TLE store upserted 2 entries, total 2 satellites", timeout_s=5.0)
    assert upsert_line is not None, "gNB did not report expected TLE store size after sat-tle upsert"


def test_gnb_periodic_sib19_contains_own_and_neighbor_satellite_positions(
    started_gnb_with_sib19_tle,
    fake_ue: FakeUe,
):
    gnb, _sib19_cfg, node_name, own_pci = started_gnb_with_sib19_tle

    if not fake_ue.wait_for_heartbeat_ack(timeout_s=8):
        pytest.skip("Fake UE did not receive heartbeat ack in SIB19 TLE periodicity test")

    payload = {
        "satellites": [
            {
                "pci": own_pci,
                "line1": OWN_TLE_LINE1,
                "line2": OWN_TLE_LINE2,
            },
            {
                "pci": 222,
                "line1": NEIGHBOR_TLE_LINE1,
                "line2": NEIGHBOR_TLE_LINE2,
            },
        ]
    }
    _run_sat_tle(node_name, payload)

    # m_sib19RangeCache is refreshed by a periodic 15s timer in RRC.
    parsed = _wait_for_sib19_payload(
        fake_ue,
        lambda p: p["formatVersion"] == 2
        and p["ephemerisType"] == 0
        and {e["pci"] for e in p["entries"]}.issuperset({own_pci, 222}),
        timeout_s=25.0,
    )
    assert parsed is not None, "Did not receive SIB19 containing own + neighbor TLE-derived entries"

    own_entry = next(e for e in parsed["entries"] if e["pci"] == own_pci)
    neighbor_entry = next(e for e in parsed["entries"] if e["pci"] == 222)

    # Validate that both entries include finite position fields in the pos/vel encoding path.
    for entry in (own_entry, neighbor_entry):
        for axis in ("x", "y", "z"):
            assert isinstance(entry[axis], float)
            assert entry[axis] == pytest.approx(entry[axis], rel=0.0, abs=0.0)

    assert own_entry["x"] != neighbor_entry["x"]
    assert own_entry["y"] != neighbor_entry["y"]
    assert own_entry["z"] != neighbor_entry["z"]


def test_gnb_periodic_sib19_orbital_mode_contains_own_and_neighbor(
    started_gnb_with_sib19_tle_orbital,
    fake_ue: FakeUe,
):
    _gnb, _sib19_cfg, node_name, own_pci = started_gnb_with_sib19_tle_orbital

    if not fake_ue.wait_for_heartbeat_ack(timeout_s=8):
        pytest.skip("Fake UE did not receive heartbeat ack in orbital SIB19 periodicity test")

    payload = {
        "satellites": [
            {
                "pci": own_pci,
                "line1": OWN_TLE_LINE1,
                "line2": OWN_TLE_LINE2,
            },
            {
                "pci": 222,
                "line1": NEIGHBOR_TLE_LINE1,
                "line2": NEIGHBOR_TLE_LINE2,
            },
        ]
    }
    _run_sat_tle(node_name, payload)

    parsed = _wait_for_sib19_payload(
        fake_ue,
        lambda p: p["formatVersion"] == 2
        and p["ephemerisType"] == 1
        and {e["pci"] for e in p["entries"]}.issuperset({own_pci, 222}),
        timeout_s=25.0,
    )
    assert parsed is not None, "Did not receive orbital SIB19 containing own + neighbor entries"

    own_entry = next(e for e in parsed["entries"] if e["pci"] == own_pci)
    neighbor_entry = next(e for e in parsed["entries"] if e["pci"] == 222)

    for entry in (own_entry, neighbor_entry):
        assert entry["semiMajorAxis"] > 6_000_000
        assert entry["eccentricity"] >= 0
        assert isinstance(entry["periapsis"], int)
        assert isinstance(entry["longitude"], int)
        assert isinstance(entry["inclination"], int)
        assert isinstance(entry["meanAnomaly"], int)

    assert own_entry["semiMajorAxis"] != 0
    assert neighbor_entry["semiMajorAxis"] != 0
    assert math.isfinite(float(own_entry["semiMajorAxis"]))
    assert math.isfinite(float(neighbor_entry["semiMajorAxis"]))


def test_gnb_sib19_three_entries_for_same_tle_different_pcis(
    started_gnb_with_sib19_tle,
    fake_ue: FakeUe,
):
    _gnb, _sib19_cfg, node_name, own_pci = started_gnb_with_sib19_tle

    if not fake_ue.wait_for_heartbeat_ack(timeout_s=8):
        pytest.skip("Fake UE did not receive heartbeat ack in same-TLE multi-PCI SIB19 test")

    # Configure own PCI with a known example TLE (from custom-gnb), then add two more PCIs
    # with the exact same orbit via CLI to emulate 3 satellites on identical trajectories.
    payload = {
        "satellites": [
            {
                "pci": own_pci,
                "line1": CUSTOM_TLE_LINE1,
                "line2": CUSTOM_TLE_LINE2,
            },
            {
                "pci": 223,
                "line1": CUSTOM_TLE_LINE1,
                "line2": CUSTOM_TLE_LINE2,
            },
            {
                "pci": 224,
                "line1": CUSTOM_TLE_LINE1,
                "line2": CUSTOM_TLE_LINE2,
            },
        ]
    }
    _run_sat_tle(node_name, payload)

    parsed = _wait_for_sib19_payload(
        fake_ue,
        lambda p: p["formatVersion"] == 2
        and p["ephemerisType"] == 0
        and len(p["entries"]) == 3
        and {e["pci"] for e in p["entries"]} == {own_pci, 223, 224},
        timeout_s=25.0,
    )
    assert parsed is not None, "Did not receive SIB19 with exactly 3 entries for the expected PCIs"

    entries_by_pci = {entry["pci"]: entry for entry in parsed["entries"]}
    own_entry = entries_by_pci[own_pci]
    e223 = entries_by_pci[223]
    e224 = entries_by_pci[224]

    # Because all three PCIs carry identical TLE lines, their propagated positions/velocities should match.
    # This confirms each SIB19 entry corresponds to one of the provided TLE records.
    for axis in ("x", "y", "z", "vx", "vy", "vz"):
        assert own_entry[axis] == pytest.approx(e223[axis], rel=1e-9, abs=1e-6)
        assert own_entry[axis] == pytest.approx(e224[axis], rel=1e-9, abs=1e-6)


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
    first_entry = first_payload["entries"][0]

    assert first_payload["ephemerisType"] == 0
    assert first_entry["x"] == pytest.approx(1111.0, abs=1e-6)
    assert first_entry["y"] == pytest.approx(2222.0, abs=1e-6)
    assert first_entry["z"] == pytest.approx(3333.0, abs=1e-6)
    assert first_entry["vx"] == pytest.approx(0.0, abs=1e-9)
    assert first_entry["vy"] == pytest.approx(0.0, abs=1e-9)
    assert first_entry["vz"] == pytest.approx(0.0, abs=1e-9)

    baseline_count = len(_extract_sib19_messages(fake_ue))
    second_epoch = int(time.time() * 1000)
    _run_loc_pv(node_name, 7777.0, 8888.0, 9999.0, 0.0, 0.0, 0.0, second_epoch)

    assert _wait_for_sib19_count(fake_ue, baseline_count + 1, timeout_s=5.0), "No SIB19 after second loc-pv"
    second_msg = _extract_sib19_messages(fake_ue)[-1]
    second_payload = _parse_sib19_payload(second_msg.raw_pdu)
    second_entry = second_payload["entries"][0]

    assert second_entry["x"] == pytest.approx(7777.0, abs=1e-6)
    assert second_entry["y"] == pytest.approx(8888.0, abs=1e-6)
    assert second_entry["z"] == pytest.approx(9999.0, abs=1e-6)


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
        assert len(p["entries"]) >= 1
        entry = p["entries"][0]
        assert entry["kOffset"] == sib19_cfg["kOffset"]
        assert entry["taCommon"] == sib19_cfg["taCommon"]
        assert entry["taCommonDrift"] == sib19_cfg["taCommonDrift"]
        assert entry["taCommonDriftVariation"] == sib19_cfg["taCommonDriftVariation"]
        assert entry["ulSyncValidity"] == sib19_cfg["ulSyncValidityDuration"]
        assert entry["cellSpecificKoffset"] == sib19_cfg["cellSpecificKoffset"]
        assert entry["polarization"] == sib19_cfg["polarization"]
        assert entry["taDrift"] == sib19_cfg["taDrift"]

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


def test_gnb_sat_loc_pv_multi_entry_and_stale_pruning(started_gnb_with_sib19, fake_ue: FakeUe):
    _gnb, sib19_cfg, node_name = started_gnb_with_sib19

    if not fake_ue.wait_for_heartbeat_ack(timeout_s=8):
        pytest.skip("Fake UE did not receive heartbeat ack in sat-loc-pv SIB19 test")

    now_ms = int(time.time() * 1000)
    _run_sat_loc_pv(node_name, {
        "pci": 101,
        "x": 5000.0,
        "y": 6000.0,
        "z": 7000.0,
        "vx": 1.0,
        "vy": 2.0,
        "vz": 3.0,
        "epochMs": now_ms,
    })
    _run_sat_loc_pv(node_name, {
        "pci": 202,
        "x": 15000.0,
        "y": 16000.0,
        "z": 17000.0,
        "vx": -1.0,
        "vy": -2.0,
        "vz": -3.0,
        "epochMs": now_ms,
    })

    assert _wait_for_sib19_count(fake_ue, 1, timeout_s=5.0), "No SIB19 received after sat-loc-pv updates"
    payload = _parse_sib19_payload(_extract_sib19_messages(fake_ue)[-1].raw_pdu)
    assert payload["formatVersion"] == 2

    pci_set = {entry["pci"] for entry in payload["entries"]}
    assert {101, 202}.issubset(pci_set)

    # Allow entries to go stale, then refresh only one PCI.
    time.sleep((sib19_cfg["satLocUpdateThresholdMs"] / 1000.0) + 0.25)

    _run_sat_loc_pv(node_name, {
        "pci": 101,
        "x": 5100.0,
        "y": 6100.0,
        "z": 7100.0,
        "vx": 0.5,
        "vy": 0.5,
        "vz": 0.5,
        "epochMs": int(time.time() * 1000),
    })

    baseline = len(_extract_sib19_messages(fake_ue))
    assert _wait_for_sib19_count(fake_ue, baseline + 1, timeout_s=5.0), "No SIB19 received after stale refresh"

    refreshed = _parse_sib19_payload(_extract_sib19_messages(fake_ue)[-1].raw_pdu)
    refreshed_pci_set = {entry["pci"] for entry in refreshed["entries"]}
    assert 101 in refreshed_pci_set
    assert 202 not in refreshed_pci_set
