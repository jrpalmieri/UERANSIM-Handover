from __future__ import annotations

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
NR_CLI = PROJECT_ROOT / "build" / "nr-cli"

pytestmark = [gnb_binary_exists, needs_pysctp]

WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_E2 = 2.0 * WGS84_F - WGS84_F * WGS84_F

UE_GEO_POS = (33.7490, -84.3880, 0.0)
TERRESTRIAL_GNB_GEO_CASES = [
    (33.7490, -84.3880, 500.0),
    (33.8000, -84.4000, 500.0),
    (34.2000, -84.6000, 500.0),
]

NTN_TLE_LINE1 = "1 25544U 98067A   24060.51892340  .00016717  00000+0  30375-3 0  9997"
NTN_TLE_LINE2 = "2 25544  51.6410  12.9532 0005518 101.8294  21.5671 15.50040210442261"

# Synthetic TLE whose sampled scenarios are documented in config comments.
# UE at lat=45, lon=-90 aligns with this orbit plane; MA=30 starts below horizon,
# MA=90 reaches zenith, and later descending pass reaches the opposite horizon.
SKYTRACK_TLE_LINE1 = "1 99994U 26106D   26106.97180556  .00000000  00000-0  00000-0 0  9999"
SKYTRACK_TLE_LINE2 = "2 99994  45.0000 270.0000 0001000   0.0000  30.0000 15.00000000    18"
SKYTRACK_UE_GEO_POS = (45.0000, -90.0000, 0.0)


class TestGnbRsrpModel:
    @pytest.mark.parametrize("gnb_geo", TERRESTRIAL_GNB_GEO_CASES)
    def test_terrestrial_heartbeat_ack_rsrp_matches_uma_model(self, fake_amf, gnb_geo):
        gnb = GnbProcess()
        fake_ue = FakeUe(sim_pos=UE_GEO_POS)
        cfg_path = gnb.generate_config()

        with open(cfg_path, "r", encoding="utf-8") as f:
            cfg = yaml.safe_load(f)

        cfg["position"] = {
            "latitude": gnb_geo[0],
            "longitude": gnb_geo[1],
            "altitude": gnb_geo[2],
        }
        cfg["rfLink"] = {
            "updateMode": "Calculated",
            "carrFrequencyHz": 3.5e9,
            "txPowerDbm": 15.0,
            "txGainDbi": 15.0,
            "ueRxGainDbi": 0.0,
        }
        cfg["ntn"] = {"ntnEnabled": False}

        with open(cfg_path, "w", encoding="utf-8") as f:
            yaml.safe_dump(cfg, f, sort_keys=False)

        try:
            gnb.start(cfg_path)
            if not gnb.wait_for_ng_setup(timeout_s=15):
                pytest.skip("gNB did not complete NG setup")

            fake_ue.start()
            ack = fake_ue.wait_for_heartbeat_ack_dbm(timeout_s=10.0, min_count=2)
            assert ack is not None, "Fake UE did not receive heartbeat ACK with RSRP"

            distance_m = _ecef_distance_m(_geo_to_ecef(gnb_geo), _geo_to_ecef(UE_GEO_POS))
            model_path_loss_db = _uma_path_loss_db(distance_m, float(cfg["rfLink"]["carrFrequencyHz"]))
            expected_dbm = _expected_terrestrial_dbm(
                gnb_geo=gnb_geo,
                ue_geo=UE_GEO_POS,
                frequency_hz=float(cfg["rfLink"]["carrFrequencyHz"]),
                tx_power_dbm=float(cfg["rfLink"]["txPowerDbm"]),
                tx_gain_dbi=float(cfg["rfLink"]["txGainDbi"]),
                ue_rx_gain_dbi=float(cfg["rfLink"]["ueRxGainDbi"]),
            )

            _print_rsrp_case(
                mode="terrestrial",
                gnb_geo=gnb_geo,
                ue_geo=UE_GEO_POS,
                frequency_hz=float(cfg["rfLink"]["carrFrequencyHz"]),
                tx_power_dbm=float(cfg["rfLink"]["txPowerDbm"]),
                tx_gain_dbi=float(cfg["rfLink"]["txGainDbi"]),
                ue_rx_gain_dbi=float(cfg["rfLink"]["ueRxGainDbi"]),
                distance_m=distance_m,
                model_path_loss_db=model_path_loss_db,
                expected_dbm=expected_dbm,
                observed_dbm=ack.dbm,
            )
            assert ack.dbm == expected_dbm
        finally:
            fake_ue.stop()
            gnb.cleanup()

    @pytest.mark.skipif(not NR_CLI.exists(), reason="nr-cli is required for sat-time pause in NTN test")
    def test_ntn_heartbeat_ack_rsrp_matches_fspl_model(self, fake_amf):
        gnb = GnbProcess()
        fake_ue = FakeUe(sim_pos=UE_GEO_POS)
        cfg_path = gnb.generate_config()

        with open(cfg_path, "r", encoding="utf-8") as f:
            cfg = yaml.safe_load(f)

        cfg["rfLink"] = {
            "updateMode": "Calculated",
            "carrFrequencyHz": 10.7e9,
            # Use a stronger link budget so ACKs continue near/below horizon
            # and the test can observe the full sky-track evolution.
            "txPowerDbm": 80.0,
            "txGainDbi": 50.0,
            "ueRxGainDbi": 50.0,
        }
        cfg["ntn"] = {
            "ntnEnabled": True,
            "tle": {
                "line1": NTN_TLE_LINE1,
                "line2": NTN_TLE_LINE2,
            },
            "sib19": {
                "sib19on": True,
                "sib19timing": 250,
                "ephType": "pos-vel",
            },
        }

        with open(cfg_path, "w", encoding="utf-8") as f:
            yaml.safe_dump(cfg, f, sort_keys=False)

        try:
            gnb.start(cfg_path)
            if not gnb.wait_for_ng_setup(timeout_s=20):
                pytest.skip("gNB did not complete NG setup for NTN RSRP test")

            fake_ue.start()
            if not fake_ue.wait_for_heartbeat_ack(timeout_s=10):
                pytest.skip("Fake UE did not receive heartbeat ACK in NTN mode")

            _exec_cli(_compute_node_name(cfg), "sat-time pause")

            sib19_entry = _wait_for_pos_vel_sib19_entry(fake_ue, timeout_s=8.0)
            assert sib19_entry is not None, "Did not receive SIB19 pos-vel payload for NTN RSRP check"

            seen = len(fake_ue.heartbeat_acks)
            ack = fake_ue.wait_for_heartbeat_ack_dbm(timeout_s=8.0, min_count=seen + 1)
            assert ack is not None, "Did not receive heartbeat ACK after sat-time pause"

            sat_ecef = (sib19_entry["x"], sib19_entry["y"], sib19_entry["z"])
            distance_m = _ecef_distance_m(sat_ecef, _geo_to_ecef(UE_GEO_POS))
            model_path_loss_db = _fspl_db(distance_m, float(cfg["rfLink"]["carrFrequencyHz"]))
            expected_dbm = _expected_ntn_dbm(
                sat_ecef=sat_ecef,
                ue_geo=UE_GEO_POS,
                frequency_hz=float(cfg["rfLink"]["carrFrequencyHz"]),
                tx_power_dbm=float(cfg["rfLink"]["txPowerDbm"]),
                tx_gain_dbi=float(cfg["rfLink"]["txGainDbi"]),
                ue_rx_gain_dbi=float(cfg["rfLink"]["ueRxGainDbi"]),
            )

            _print_rsrp_case(
                mode="ntn",
                gnb_geo=None,
                ue_geo=UE_GEO_POS,
                frequency_hz=float(cfg["rfLink"]["carrFrequencyHz"]),
                tx_power_dbm=float(cfg["rfLink"]["txPowerDbm"]),
                tx_gain_dbi=float(cfg["rfLink"]["txGainDbi"]),
                ue_rx_gain_dbi=float(cfg["rfLink"]["ueRxGainDbi"]),
                distance_m=distance_m,
                model_path_loss_db=model_path_loss_db,
                expected_dbm=expected_dbm,
                observed_dbm=ack.dbm,
                sat_ecef=sat_ecef,
                sib19_epoch_10ms=int(sib19_entry["epoch10ms"]),
            )
            assert ack.dbm == expected_dbm
        finally:
            fake_ue.stop()
            gnb.cleanup()

    @pytest.mark.skipif(not NR_CLI.exists(), reason="nr-cli is required for sat-time control in NTN test")
    def test_ntn_moving_satellite_three_rsrp_reports_match_fspl_model(self, fake_amf):
        gnb = GnbProcess()
        fake_ue = FakeUe(sim_pos=UE_GEO_POS)
        cfg_path = gnb.generate_config()

        with open(cfg_path, "r", encoding="utf-8") as f:
            cfg = yaml.safe_load(f)

        cfg["rfLink"] = {
            "updateMode": "Calculated",
            "carrFrequencyHz": 10.7e9,
            # Stronger budget keeps ACK reporting active for low-elevation tracking.
            "txPowerDbm": 80.0,
            "txGainDbi": 50.0,
            "ueRxGainDbi": 50.0,
        }
        cfg["ntn"] = {
            "ntnEnabled": True,
            "tle": {
                "line1": NTN_TLE_LINE1,
                "line2": NTN_TLE_LINE2,
            },
            "sib19": {
                "sib19on": True,
                "sib19timing": 250,
                "ephType": "pos-vel",
            },
        }

        with open(cfg_path, "w", encoding="utf-8") as f:
            yaml.safe_dump(cfg, f, sort_keys=False)

        try:
            gnb.start(cfg_path)
            if not gnb.wait_for_ng_setup(timeout_s=20):
                pytest.skip("gNB did not complete NG setup for moving NTN RSRP test")

            fake_ue.start()
            if not fake_ue.wait_for_heartbeat_ack(timeout_s=10):
                pytest.skip("Fake UE did not receive heartbeat ACK in moving NTN mode")

            node_name = _compute_node_name(cfg)
            _exec_cli(node_name, "sat-time run")

            # Align each ACK with the closest SIB19 sample while satellite time is moving.
            validated = 0
            initial_seen = len(fake_ue.heartbeat_acks)
            next_min_count = max(initial_seen + 1, 2)
            start = time.monotonic()
            while validated < 3 and (time.monotonic() - start) < 15.0:
                ack = fake_ue.wait_for_heartbeat_ack_dbm(timeout_s=5.0, min_count=next_min_count)
                assert ack is not None, "Timed out waiting for heartbeat ACK sample in moving NTN test"

                sib19_entry = _closest_pos_vel_sib19_entry(fake_ue, ack.timestamp, window_s=2.0)
                assert sib19_entry is not None, "No nearby SIB19 sample found to model moving NTN ACK"

                sat_ecef = (sib19_entry["x"], sib19_entry["y"], sib19_entry["z"])
                distance_m = _ecef_distance_m(sat_ecef, _geo_to_ecef(UE_GEO_POS))
                model_path_loss_db = _fspl_db(distance_m, float(cfg["rfLink"]["carrFrequencyHz"]))
                expected_dbm = _expected_ntn_dbm(
                    sat_ecef=sat_ecef,
                    ue_geo=UE_GEO_POS,
                    frequency_hz=float(cfg["rfLink"]["carrFrequencyHz"]),
                    tx_power_dbm=float(cfg["rfLink"]["txPowerDbm"]),
                    tx_gain_dbi=float(cfg["rfLink"]["txGainDbi"]),
                    ue_rx_gain_dbi=float(cfg["rfLink"]["ueRxGainDbi"]),
                )

                _print_rsrp_case(
                    mode=f"ntn-moving-sample-{validated + 1}",
                    gnb_geo=None,
                    ue_geo=UE_GEO_POS,
                    frequency_hz=float(cfg["rfLink"]["carrFrequencyHz"]),
                    tx_power_dbm=float(cfg["rfLink"]["txPowerDbm"]),
                    tx_gain_dbi=float(cfg["rfLink"]["txGainDbi"]),
                    ue_rx_gain_dbi=float(cfg["rfLink"]["ueRxGainDbi"]),
                    distance_m=distance_m,
                    model_path_loss_db=model_path_loss_db,
                    expected_dbm=expected_dbm,
                    observed_dbm=ack.dbm,
                    sat_ecef=sat_ecef,
                    sib19_epoch_10ms=int(sib19_entry["epoch10ms"]),
                )

                assert ack.dbm == expected_dbm
                validated += 1
                next_min_count += 1

            assert validated == 3, "Did not validate three moving NTN RSRP samples"
        finally:
            fake_ue.stop()
            gnb.cleanup()

    @pytest.mark.skipif(not NR_CLI.exists(), reason="nr-cli is required for sat-time control in NTN test")
    def test_ntn_skytrack_horizon_to_horizon_with_30s_samples(self, fake_amf):
        gnb = GnbProcess()
        fake_ue = FakeUe(sim_pos=SKYTRACK_UE_GEO_POS, heartbeat_interval_s=0.1)
        cfg_path = gnb.generate_config()

        with open(cfg_path, "r", encoding="utf-8") as f:
            cfg = yaml.safe_load(f)

        cfg["rfLink"] = {
            "updateMode": "Calculated",
            "carrFrequencyHz": 10.7e9,
            # Strong link budget keeps ACK reports available across low elevations.
            "txPowerDbm": 80.0,
            "txGainDbi": 50.0,
            "ueRxGainDbi": 50.0,
        }
        cfg["ntn"] = {
            "ntnEnabled": True,
            "tle": {
                "line1": SKYTRACK_TLE_LINE1,
                "line2": SKYTRACK_TLE_LINE2,
            },
            "timeWarp": {
                "startEpoch": "26106.97180556",
                "startCondition": "paused",
                "tickScaling": 100.0,
            },
            "sib19": {
                "sib19on": True,
                "sib19timing": 200,
                "ephType": "pos-vel",
            },
        }

        with open(cfg_path, "w", encoding="utf-8") as f:
            yaml.safe_dump(cfg, f, sort_keys=False)

        try:
            gnb.start(cfg_path)
            if not gnb.wait_for_ng_setup(timeout_s=20):
                pytest.skip("gNB did not complete NG setup for NTN sky-track test")

            fake_ue.start()
            if not fake_ue.wait_for_heartbeat_ack(timeout_s=10):
                pytest.skip("Fake UE did not receive heartbeat ACK in NTN sky-track test")

            start_epoch_sib19 = _wait_for_pos_vel_sib19_entry(fake_ue, timeout_s=8.0)
            assert start_epoch_sib19 is not None, "Failed to capture start-epoch SIB19 sample"
            sat_start_epoch_ecef = (
                start_epoch_sib19["x"],
                start_epoch_sib19["y"],
                start_epoch_sib19["z"],
            )

            node_name = _compute_node_name(cfg)

            # -----------------------------------------------------------------
            # Calibration phase: pick a UE location that will be directly under
            # the satellite at a future sat-time. This guarantees an overhead pass
            # in the main observation window after we reset the start epoch.
            # -----------------------------------------------------------------
            _exec_cli(node_name, "sat-time run")
            _exec_cli(node_name, "sat-time tickscale=100")
            sat_tickscale = 100.0

            sat_check_a = _get_sat_time_ms(node_name)
            time.sleep(0.2)
            sat_check_b = _get_sat_time_ms(node_name)
            assert sat_check_b - sat_check_a >= 5_000, (
                "sat-time scaling did not accelerate as expected: "
                f"delta_ms={sat_check_b - sat_check_a}"
            )

            calibration_target_s = 2000.0
            calibration_start_wall = time.monotonic()
            calibration_best_delta_s = float("inf")
            calibration_ue_geo: tuple[float, float, float] | None = None

            calibration_deadline = time.monotonic() + 20.0
            while time.monotonic() < calibration_deadline:
                sat_elapsed_s = (time.monotonic() - calibration_start_wall) * sat_tickscale

                sib19_pair = _latest_pos_vel_sib19_with_timestamp(fake_ue)
                if sib19_pair is None:
                    time.sleep(0.2)
                    continue

                sib19, _ = sib19_pair
                sat_ecef = (sib19["x"], sib19["y"], sib19["z"])
                nadir_lat, nadir_lon = _ecef_to_lat_lon_deg(sat_ecef)
                candidate_ue_geo = (nadir_lat, nadir_lon, 0.0)
                initial_elev_deg = _elevation_angle_deg(candidate_ue_geo, sat_start_epoch_ecef)

                # Enforce requested scenario: start below/at horizon, then rise.
                if initial_elev_deg > 0.0:
                    time.sleep(0.2)
                    continue

                delta_s = abs(sat_elapsed_s - calibration_target_s)
                if delta_s < calibration_best_delta_s:
                    calibration_best_delta_s = delta_s
                    calibration_ue_geo = candidate_ue_geo

                if sat_elapsed_s >= calibration_target_s + 120.0:
                    break

                time.sleep(0.2)

            assert calibration_ue_geo is not None, "Failed to calibrate UE sky-track location from SIB19"
            assert calibration_best_delta_s <= 120.0, (
                "Calibration did not observe target sat-time neighborhood; "
                f"best_delta_s={calibration_best_delta_s:.3f}"
            )

            print(
                "[NTN-SKYTRACK] calibrated_ue_geo="
                f"({calibration_ue_geo[0]:.6f}, {calibration_ue_geo[1]:.6f}, {calibration_ue_geo[2]:.1f}) "
                f"target_sat_s={calibration_target_s:.1f} best_delta_s={calibration_best_delta_s:.3f}"
            )

            # Update Fake UE heartbeat position to the calibrated ground-track point.
            fake_ue._sim_pos = calibration_ue_geo

            # Reset sat-time back to configured epoch and run main observation pass.
            _exec_cli(node_name, "sat-time start-epoch=26106.97180556")
            _exec_cli(node_name, "sat-time run")

            reset_wall = time.monotonic()
            main_start_wall = time.monotonic()
            next_sample_sat_s = 0.0

            initial_elevation_deg: float | None = None
            reached_overhead = False
            opposite_horizon_elapsed_s: float | None = None
            max_elevation_deg = -90.0
            sample_count = 0
            sampled_with_rsrp = 0

            deadline = time.monotonic() + 90.0
            while time.monotonic() < deadline:
                sat_elapsed_s = (time.monotonic() - main_start_wall) * sat_tickscale

                sib19_pair = _latest_pos_vel_sib19_with_timestamp(fake_ue)
                if sib19_pair is None:
                    time.sleep(0.2)
                    continue

                sib19, sib19_timestamp = sib19_pair
                if sib19_timestamp < reset_wall:
                    time.sleep(0.05)
                    continue

                sat_ecef = (sib19["x"], sib19["y"], sib19["z"])
                elevation_deg = _elevation_angle_deg(calibration_ue_geo, sat_ecef)
                nadir_distance_m = _nadir_distance_m(calibration_ue_geo, sat_ecef)

                if initial_elevation_deg is None:
                    initial_elevation_deg = elevation_deg

                max_elevation_deg = max(max_elevation_deg, elevation_deg)
                if elevation_deg >= 85.0:
                    reached_overhead = True

                if sat_elapsed_s >= next_sample_sat_s:
                    ack = _closest_heartbeat_ack(fake_ue, sib19_timestamp, window_s=3.0)
                    rsrp_text = "NA"
                    if ack is not None:
                        rsrp_text = str(ack.dbm)
                        sampled_with_rsrp += 1

                    sample_count += 1
                    print(
                        "[NTN-SKYTRACK] sample="
                        f"{sample_count} sat_elapsed_s={sat_elapsed_s:.1f} "
                        f"rsrp_dbm={rsrp_text} nadir_distance_m={nadir_distance_m:.1f} "
                        f"elevation_deg={elevation_deg:.3f}"
                    )
                    next_sample_sat_s += 30.0

                if reached_overhead and elevation_deg <= 0.0:
                    opposite_horizon_elapsed_s = sat_elapsed_s
                    break

                if sat_elapsed_s > 5_200.0:
                    break

                time.sleep(0.2)

            assert initial_elevation_deg is not None
            assert initial_elevation_deg <= 0.0, (
                "Expected start below/at horizon but got "
                f"initial_elevation_deg={initial_elevation_deg:.3f}"
            )
            assert reached_overhead, (
                "Satellite never reached near-overhead geometry "
                f"(max_elevation_deg={max_elevation_deg:.3f})"
            )
            assert opposite_horizon_elapsed_s is not None, (
                "Satellite did not reach opposite horizon after overhead "
                f"within test window (max_elevation_deg={max_elevation_deg:.3f})"
            )
            assert 10.0 <= opposite_horizon_elapsed_s <= 5000.0, (
                "Opposite-horizon crossing sat-time out of expected range: "
                f"{opposite_horizon_elapsed_s:.3f}s"
            )
            assert sample_count >= 3, "Expected multiple 30s sat-time samples for sky-track trace"
            assert sampled_with_rsrp >= 3, "Expected at least three sampled points with reported RSRP"
        finally:
            fake_ue.stop()
            gnb.cleanup()


def _compute_node_name(cfg: dict) -> str:
    nci = int(str(cfg["nci"]), 0)
    id_length = int(cfg["idLength"])
    gnb_id = nci >> (36 - id_length)
    mcc = int(str(cfg["mcc"]))
    mnc = int(str(cfg["mnc"]))
    return f"UERANSIM-gnb-{mcc}-{mnc}-{gnb_id}"


def _exec_cli(node_name: str, command: str, timeout_s: int = 8) -> str:
    proc = subprocess.run(
        [str(NR_CLI), node_name, "--exec", command],
        capture_output=True,
        text=True,
        timeout=timeout_s,
        check=False,
    )
    assert proc.returncode == 0, (
        f"nr-cli failed for command '{command}' on node '{node_name}'. "
        f"stdout='{proc.stdout.strip()}' stderr='{proc.stderr.strip()}'"
    )
    return proc.stdout


def _geo_to_ecef(geo: tuple[float, float, float]) -> tuple[float, float, float]:
    lat_rad = math.radians(geo[0])
    lon_rad = math.radians(geo[1])
    alt = geo[2]

    sin_lat = math.sin(lat_rad)
    cos_lat = math.cos(lat_rad)
    sin_lon = math.sin(lon_rad)
    cos_lon = math.cos(lon_rad)

    n = WGS84_A / math.sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat)
    x = (n + alt) * cos_lat * cos_lon
    y = (n + alt) * cos_lat * sin_lon
    z = (n * (1.0 - WGS84_E2) + alt) * sin_lat
    return x, y, z


def _ecef_distance_m(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    dz = a[2] - b[2]
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def _expected_terrestrial_dbm(
    gnb_geo: tuple[float, float, float],
    ue_geo: tuple[float, float, float],
    frequency_hz: float,
    tx_power_dbm: float,
    tx_gain_dbi: float,
    ue_rx_gain_dbi: float,
) -> int:
    distance_m = _ecef_distance_m(_geo_to_ecef(gnb_geo), _geo_to_ecef(ue_geo))
    uma_path_loss_db = _uma_path_loss_db(distance_m, frequency_hz)
    rx_dbm = tx_power_dbm + tx_gain_dbi + ue_rx_gain_dbi - uma_path_loss_db
    return int(round(rx_dbm))


def _expected_ntn_dbm(
    sat_ecef: tuple[float, float, float],
    ue_geo: tuple[float, float, float],
    frequency_hz: float,
    tx_power_dbm: float,
    tx_gain_dbi: float,
    ue_rx_gain_dbi: float,
) -> int:
    distance_m = _ecef_distance_m(sat_ecef, _geo_to_ecef(ue_geo))
    fspl_db = _fspl_db(distance_m, frequency_hz)
    rx_dbm = tx_power_dbm + tx_gain_dbi + ue_rx_gain_dbi - fspl_db
    return int(round(rx_dbm))


def _uma_path_loss_db(distance_m: float, frequency_hz: float) -> float:
    distance_m = max(distance_m, 1.0)
    freq_ghz = frequency_hz / 1e9
    # Matches src/gnb/rls/pos_sim.cpp UrbanMacroPathLossDb.
    if distance_m <= 1600.0:
        return 28.0 + (22.0 * math.log10(distance_m)) + (20.0 * math.log10(freq_ghz))
    else:
        return 28.0 + (40.0 * math.log10(distance_m)) + (20.0 * math.log10(freq_ghz)) - 9.0 * math.log10(1600.0**2)


def _fspl_db(distance_m: float, frequency_hz: float) -> float:
    distance_m = max(distance_m, 1.0)
    # Matches src/gnb/rls/pos_sim.cpp FreeSpacePathLossDb.
    return 20.0 * math.log10(distance_m) + 20.0 * math.log10(frequency_hz) - 147.55


def _print_rsrp_case(
    mode: str,
    gnb_geo: tuple[float, float, float] | None,
    ue_geo: tuple[float, float, float],
    frequency_hz: float,
    tx_power_dbm: float,
    tx_gain_dbi: float,
    ue_rx_gain_dbi: float,
    distance_m: float,
    model_path_loss_db: float,
    expected_dbm: int,
    observed_dbm: int,
    sat_ecef: tuple[float, float, float] | None = None,
    sib19_epoch_10ms: int | None = None,
) -> None:
    print("\n[RSRP-TRACE] mode=", mode, sep="")
    if gnb_geo is not None:
        print("[RSRP-TRACE] gnb_geo(lat,lon,alt)=", gnb_geo, sep="")
    if sat_ecef is not None:
        print("[RSRP-TRACE] sat_ecef(x,y,z)=", tuple(round(v, 3) for v in sat_ecef), sep="")
    print("[RSRP-TRACE] ue_geo(lat,lon,alt)=", ue_geo, sep="")
    print(
        "[RSRP-TRACE] rf(f_Hz,tx_dBm,gtx_dBi,grx_dBi)=",
        (frequency_hz, tx_power_dbm, tx_gain_dbi, ue_rx_gain_dbi),
        sep="",
    )
    print("[RSRP-TRACE] distance_m=", round(distance_m, 3), sep="")
    print("[RSRP-TRACE] model_path_loss_db=", round(model_path_loss_db, 6), sep="")
    if sib19_epoch_10ms is not None:
        print("[RSRP-TRACE] sib19_epoch_10ms=", sib19_epoch_10ms, sep="")
    print("[RSRP-TRACE] expected_dbm=", expected_dbm, sep="")
    print("[RSRP-TRACE] observed_ack_dbm=", observed_dbm, sep="")


def _wait_for_pos_vel_sib19_entry(fake_ue: FakeUe, timeout_s: float) -> dict | None:
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        for msg in reversed(fake_ue.dl_messages):
            if msg.channel != int(RrcChannel.DL_SIB19):
                continue
            parsed = _parse_sib19_payload(msg.raw_pdu)
            if parsed is not None:
                return parsed
        time.sleep(0.1)
    return None


def _closest_pos_vel_sib19_entry(
    fake_ue: FakeUe,
    target_time_s: float,
    window_s: float,
) -> dict | None:
    closest: dict | None = None
    smallest_delta = float("inf")

    for msg in fake_ue.dl_messages:
        if msg.channel != int(RrcChannel.DL_SIB19):
            continue

        delta = abs(msg.timestamp - target_time_s)
        if delta > window_s:
            continue

        parsed = _parse_sib19_payload(msg.raw_pdu)
        if parsed is None:
            continue

        if delta < smallest_delta:
            smallest_delta = delta
            closest = parsed

    return closest


def _latest_pos_vel_sib19_with_timestamp(fake_ue: FakeUe) -> tuple[dict, float] | None:
    for msg in reversed(fake_ue.dl_messages):
        if msg.channel != int(RrcChannel.DL_SIB19):
            continue
        parsed = _parse_sib19_payload(msg.raw_pdu)
        if parsed is None:
            continue
        return parsed, msg.timestamp
    return None


def _closest_heartbeat_ack(fake_ue: FakeUe, target_time_s: float, window_s: float) -> object | None:
    closest = None
    smallest_delta = float("inf")
    for ack in fake_ue.heartbeat_acks:
        delta = abs(ack.timestamp - target_time_s)
        if delta > window_s:
            continue
        if delta < smallest_delta:
            smallest_delta = delta
            closest = ack
    return closest


def _parse_sib19_payload(pdu: bytes) -> dict | None:
    # Format v2 header: [version:u8][ephType:u8][reserved:u16][count:u32]
    if len(pdu) < 8 or pdu[0] != 2:
        return None

    ephemeris_type = pdu[1]
    if ephemeris_type != 0:
        return None

    count = struct.unpack_from("<I", pdu, 4)[0]
    if count < 1:
        return None

    expected = 8 + count * 96
    if len(pdu) < expected:
        return None

    base = 8
    return {
        "pci": struct.unpack_from("<i", pdu, base)[0],
        "x": struct.unpack_from("<d", pdu, base + 4)[0],
        "y": struct.unpack_from("<d", pdu, base + 12)[0],
        "z": struct.unpack_from("<d", pdu, base + 20)[0],
        "vx": struct.unpack_from("<d", pdu, base + 28)[0],
        "vy": struct.unpack_from("<d", pdu, base + 36)[0],
        "vz": struct.unpack_from("<d", pdu, base + 44)[0],
        "epoch10ms": struct.unpack_from("<q", pdu, base + 52)[0],
    }


def _get_sat_time_ms(node_name: str) -> int:
    last_error: Exception | None = None
    for _ in range(3):
        try:
            raw = _exec_cli(node_name, "sat-time", timeout_s=15)
            status = yaml.safe_load(raw)
            assert isinstance(status, dict), f"Unexpected sat-time output: {raw}"
            assert "sat-time-ms" in status, f"Missing sat-time-ms in sat-time output: {status}"
            return int(status["sat-time-ms"])
        except Exception as exc:
            last_error = exc
            time.sleep(0.1)

    assert last_error is not None
    raise last_error


def _ecef_to_lat_lon_deg(ecef: tuple[float, float, float]) -> tuple[float, float]:
    x, y, z = ecef
    lon = math.degrees(math.atan2(y, x))
    hyp = math.sqrt(x * x + y * y)
    lat = math.degrees(math.atan2(z, hyp))
    return lat, lon


def _haversine_distance_m(
    a_geo: tuple[float, float, float],
    b_lat_lon_deg: tuple[float, float],
) -> float:
    # Spherical Earth distance for ground-track (nadir) reporting.
    earth_radius_m = 6371000.0
    lat1 = math.radians(a_geo[0])
    lon1 = math.radians(a_geo[1])
    lat2 = math.radians(b_lat_lon_deg[0])
    lon2 = math.radians(b_lat_lon_deg[1])

    dlat = lat2 - lat1
    dlon = lon2 - lon1
    hav = math.sin(dlat / 2.0) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2.0) ** 2
    return 2.0 * earth_radius_m * math.asin(math.sqrt(hav))


def _nadir_distance_m(ue_geo: tuple[float, float, float], sat_ecef: tuple[float, float, float]) -> float:
    nadir_lat_lon = _ecef_to_lat_lon_deg(sat_ecef)
    return _haversine_distance_m(ue_geo, nadir_lat_lon)


def _elevation_angle_deg(ue_geo: tuple[float, float, float], sat_ecef: tuple[float, float, float]) -> float:
    ue_ecef = _geo_to_ecef(ue_geo)
    rx = sat_ecef[0] - ue_ecef[0]
    ry = sat_ecef[1] - ue_ecef[1]
    rz = sat_ecef[2] - ue_ecef[2]

    lat = math.radians(ue_geo[0])
    lon = math.radians(ue_geo[1])

    east_x = -math.sin(lon)
    east_y = math.cos(lon)
    east_z = 0.0

    north_x = -math.sin(lat) * math.cos(lon)
    north_y = -math.sin(lat) * math.sin(lon)
    north_z = math.cos(lat)

    up_x = math.cos(lat) * math.cos(lon)
    up_y = math.cos(lat) * math.sin(lon)
    up_z = math.sin(lat)

    e = rx * east_x + ry * east_y + rz * east_z
    n = rx * north_x + ry * north_y + rz * north_z
    u = rx * up_x + ry * up_y + rz * up_z

    horizontal = math.sqrt(e * e + n * n)
    return math.degrees(math.atan2(u, horizontal))
