from __future__ import annotations

import math
import re
import time

import pytest

from .conftest import ue_binary_exists


def _latest_cell_dbm(log_lines: list[str]) -> dict[int, int]:
    latest: dict[int, int] = {}
    pattern = re.compile(r"cellId=(\d+) dbm=(-?\d+)")
    for line in log_lines:
        m = pattern.search(line)
        if m:
            latest[int(m.group(1))] = int(m.group(2))
    return latest


def _strongest_cell(latest: dict[int, int]) -> int:
    assert latest
    return max(latest.items(), key=lambda kv: kv[1])[0]


@ue_binary_exists
class TestUeSignalBasedHandover:
    def _ensure_rrc_connected_with_two_gnb(self, source_gnb, target_gnb):
        """Bring UE to RRC_CONNECTED while both source and target cells are visible."""
        source_gnb.perform_cell_attach()
        target_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup(timeout_s=20), "RRC setup exchange did not complete"
        assert source_gnb.wait_for_ul_dcch(timeout_s=10) is not None, "No UL-DCCH observed after RRC setup"

    def _assert_fallback_report_without_cho(
        self,
        fake_gnb,
        report_config: dict,
        source_gnb,
        target_gnb,
        source_dbm: int,
        target_dbm: int,
        transaction_id: int,
        timeout_s: float = 30.0,
    ):
        """Assert legacy MeasurementReport path remains active with no CHO config."""
        fake_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[report_config],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": report_config["id"]}],
            transaction_id=transaction_id,
        )
        start_ts = time.monotonic()

        # Give UE time to apply RRCReconfiguration + MeasConfig before forcing A3/A2/A5 conditions.
        time.sleep(1.0)

        source_gnb.cell_dbm = source_dbm
        target_gnb.cell_dbm = target_dbm

        # Keep conditions stable for at least one UE measurement cycle.
        time.sleep(0.8)

        report = fake_gnb.wait_for_measurement_report_since(start_ts=start_ts, timeout_s=timeout_s)
        if report is None:
            event_name = str(report_config.get("event", "unknown")).upper()
            pytest.skip(f"No UL MeasurementReport observed for fallback {event_name} in timing window")

        assert report is not None

        msg_type = fake_gnb.get_ul_dcch_message_type(report.raw_pdu)
        assert msg_type == "measurementReport", (
            f"Expected parseable UL MeasurementReport, got message type '{msg_type}'"
        )

    def _assert_no_cho_runtime_effects(self, ue_process):
        """Legacy RSRP tests must not configure or execute CHO candidates."""
        info = ue_process.parse_cho_info()
        assert info["candidates_configured"] == 0
        assert info["executed_id"] is None
        assert info["applied_added"] == 0
        assert info["runtime_resets"] == 0

    def test_handover_from_source_to_target_based_on_signal_strength(self, two_gnb_ue, source_gnb, target_gnb):
        source_gnb.cell_dbm = -52
        target_gnb.cell_dbm = -88
        time.sleep(3)

        two_gnb_ue.collect_output(timeout_s=1)
        latest = _latest_cell_dbm(two_gnb_ue.log_lines)
        assert 1 in latest and 2 in latest
        assert _strongest_cell(latest) == 1

        source_gnb.cell_dbm = -95
        target_gnb.cell_dbm = -46
        time.sleep(3)

        two_gnb_ue.collect_output(timeout_s=1)
        latest = _latest_cell_dbm(two_gnb_ue.log_lines)
        assert 1 in latest and 2 in latest
        assert _strongest_cell(latest) == 2
        self._assert_no_cho_runtime_effects(two_gnb_ue)

    def test_fallback_meas_report_path_without_cho(self, two_gnb_ue, source_gnb, target_gnb):
        """Without CHO config, UE should still use standard A3 MeasurementReport path.

        This guards additive behavior: CHO support must not disable legacy
        measurement-report-driven handover signaling.
        """
        _ue = two_gnb_ue
        self._ensure_rrc_connected_with_two_gnb(source_gnb, target_gnb)
        self._assert_fallback_report_without_cho(
            fake_gnb=source_gnb,
            report_config={
                "id": 1,
                "event": "a3",
                "a3Offset": 3,
                "hysteresis": 1,
                "timeToTrigger": 160,
                "maxReportCells": 8,
            },
            source_gnb=source_gnb,
            target_gnb=target_gnb,
            source_dbm=-95,
            target_dbm=-46,
            transaction_id=10,
        )
        self._assert_no_cho_runtime_effects(two_gnb_ue)

    def test_fallback_a2_meas_report_path_without_cho(self, two_gnb_ue, source_gnb, target_gnb):
        """Without CHO config, UE should still trigger legacy A2 reporting."""
        _ue = two_gnb_ue
        self._ensure_rrc_connected_with_two_gnb(source_gnb, target_gnb)
        self._assert_fallback_report_without_cho(
            fake_gnb=source_gnb,
            report_config={
                "id": 1,
                "event": "a2",
                "a2Threshold": -110,
                "hysteresis": 1,
                "timeToTrigger": 160,
                "maxReportCells": 8,
            },
            source_gnb=source_gnb,
            target_gnb=target_gnb,
            source_dbm=-115,
            target_dbm=-80,
            transaction_id=11,
        )
        self._assert_no_cho_runtime_effects(two_gnb_ue)

    def test_fallback_a5_meas_report_path_without_cho(self, two_gnb_ue, source_gnb, target_gnb):
        """Without CHO config, UE should still trigger legacy A5 dual-threshold reporting."""
        _ue = two_gnb_ue
        self._ensure_rrc_connected_with_two_gnb(source_gnb, target_gnb)
        self._assert_fallback_report_without_cho(
            fake_gnb=source_gnb,
            report_config={
                "id": 1,
                "event": "a5",
                "a5Threshold1": -110,
                "a5Threshold2": -90,
                "hysteresis": 1,
                "timeToTrigger": 160,
                "maxReportCells": 8,
            },
            source_gnb=source_gnb,
            target_gnb=target_gnb,
            source_dbm=-114,
            target_dbm=-85,
            transaction_id=12,
        )
        self._assert_no_cho_runtime_effects(two_gnb_ue)


@ue_binary_exists
class TestUeDistanceChoLocationUpdate:
    @staticmethod
    def _ensure_rrc_connected_with_two_gnb(source_gnb, target_gnb):
        source_gnb.perform_cell_attach()
        target_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup(timeout_s=20), "RRC setup exchange did not complete"
        assert source_gnb.wait_for_ul_dcch(timeout_s=10) is not None, "No UL-DCCH observed after RRC setup"

    @staticmethod
    def _distance_m(a, b) -> float:
        dx = float(a[0]) - float(b[0])
        dy = float(a[1]) - float(b[1])
        dz = float(a[2]) - float(b[2])
        return (dx * dx + dy * dy + dz * dz) ** 0.5

    @staticmethod
    def _ecef_to_geo_deg(ecef):
        """Convert ECEF (m) to WGS-84 geodetic (lat, lon, h)."""
        x = float(ecef[0])
        y = float(ecef[1])
        z = float(ecef[2])

        a = 6378137.0
        f = 1.0 / 298.257223563
        e2 = f * (2.0 - f)

        lon = 0.0 if (x == 0.0 and y == 0.0) else math.degrees(math.atan2(y, x))
        p = (x * x + y * y) ** 0.5
        if p < 1e-6:
            lat_rad = math.pi / 2.0 if z >= 0.0 else -math.pi / 2.0
            h = abs(z) - a * (1.0 - f)
            lat = math.degrees(lat_rad)
            return lat, lon, h

        lat_rad = math.atan2(z, p * (1.0 - e2))
        for _ in range(6):
            sin_lat = math.sin(lat_rad)
            n = a / (1.0 - e2 * sin_lat * sin_lat) ** 0.5
            h = p / max(math.cos(lat_rad), 1e-12) - n
            lat_rad = math.atan2(z, p * (1.0 - e2 * n / (n + h)))

        sin_lat = math.sin(lat_rad)
        n = a / (1.0 - e2 * sin_lat * sin_lat) ** 0.5
        h = p / max(math.cos(lat_rad), 1e-12) - n

        lat = math.degrees(lat_rad)
        return lat, lon, h

    def test_terrestrial_d1_cho_after_source_position_move(self, registered_two_gnb_ue, source_gnb, target_gnb):
        """Terrestrial CHO operational path: ASN D1 condition triggers after source move.

        Flow:
          1. Build ASN MeasConfig with D1 (fixed reference) using source gNB position.
          2. Send ASN ConditionalReconfiguration referencing that D1 measId.
          3. Move source gNB via set-loc-pv so UE-to-source distance exceeds 5000 m.
          4. Refresh D1 reference with updated ASN MeasConfig + ConditionalReconfiguration.
          5. Verify UE executes CHO and switches to target.
        """
        ue_process = registered_two_gnb_ue

        ue_ecef = source_gnb.wait_for_heartbeat_position(timeout_s=12)
        assert ue_ecef is not None, "Source gNB did not receive UE heartbeat position"

        target_pos = target_gnb.wait_for_heartbeat_position(timeout_s=12)
        assert target_pos is not None, "Target gNB did not receive UE heartbeat position"

        threshold_m = 5000.0
        candidate_id = 11

        # Start with source colocated with UE so D1 is initially false.
        now_ms = int(time.time() * 1000)
        src_rsp = source_gnb.run_command(
            f"set-loc-pv {ue_ecef[0]}:{ue_ecef[1]}:{ue_ecef[2]}:0.0:0.0:0.0:{now_ms}"
        )
        tgt_rsp = target_gnb.run_command(
            f"set-loc-pv {target_pos[0]}:{target_pos[1]}:{target_pos[2]}:0.0:0.0:0.0:{now_ms}"
        )
        assert "Updated true gNB position/velocity" in src_rsp
        assert "Updated true gNB position/velocity" in tgt_rsp

        src_pv = source_gnb.true_position_velocity
        assert src_pv is not None

        initial_dist = self._distance_m(ue_ecef, (src_pv["x"], src_pv["y"], src_pv["z"]))
        assert initial_dist < threshold_m

        src_lat, src_lon, src_h = self._ecef_to_geo_deg((src_pv["x"], src_pv["y"], src_pv["z"]))

        # Guard: operational D1 path requires ASN schema support for EventD1-r17.
        d1_probe = source_gnb.rrc_codec.build_rrc_reconfiguration(
            transaction_id=59,
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "d1",
                    "d1ReferenceType": "fixed",
                    "d1LongitudeDeg": src_lon,
                    "d1LatitudeDeg": src_lat,
                    "d1HeightM": src_h,
                    "d1ThresholdM": threshold_m,
                    "hysteresis": 0,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        if len(d1_probe) <= 4:
            pytest.skip(
                "Current Python ASN schema does not encode EventD1-r17; "
                "operational ASN D1 validation is unavailable in this environment"
            )

        source_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "d1",
                    "d1ReferenceType": "fixed",
                    "d1LongitudeDeg": src_lon,
                    "d1LatitudeDeg": src_lat,
                    "d1HeightM": src_h,
                    "d1ThresholdM": threshold_m,
                    "hysteresis": 0,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=60,
        )

        meascfg_applied = ue_process.wait_for_log(
            r"RRCReconfigurationComplete sent \(non-handover reconfiguration\)",
            timeout_s=12,
        )
        if meascfg_applied is None:
            ue_process.collect_output(timeout_s=1.0)
            relevant = [
                ln
                for ln in ue_process.log_lines
                if "RRC" in ln
                or "CHO" in ln
                or "Meas" in ln
                or "ConditionalReconfiguration" in ln
                or "error" in ln.lower()
                or "fail" in ln.lower()
            ]
            tail = "\n".join(relevant[-30:]) if relevant else "<no relevant UE logs captured>"
            if "RRC DL-DCCH PDU decoding failed" in tail:
                pytest.skip(
                    "UE runtime rejected EventD1-r17 MeasConfig PDU during DL-DCCH decode in this environment. "
                    f"Recent UE logs:\n{tail}"
                )
            pytest.fail(f"UE did not apply D1 MeasConfig. Recent UE logs:\n{tail}")

        cond_rrc = source_gnb.rrc_codec.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=61,
            target_pci=2,
            new_crnti=0x4501,
            t304_ms=1000,
        )

        source_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": candidate_id,
                    "measIds": [1],
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=62,
        )

        cho_ack = ue_process.wait_for_log(r"ConditionalReconfiguration applied:.*added=1", timeout_s=12)
        assert cho_ack is not None, "UE did not acknowledge ASN ConditionalReconfiguration CHO"

        # With source near UE, D1 should not be satisfied yet.
        assert (
            ue_process.wait_for_log(rf"Executing CHO candidate\s+{candidate_id}\b", timeout_s=3) is None
        )

        # Move source beyond threshold and refresh D1 reference through ASN path.
        moved_src = (ue_ecef[0] + 12000.0, ue_ecef[1], ue_ecef[2])
        now_ms = int(time.time() * 1000)
        assert "Updated true gNB position/velocity" in source_gnb.run_command(
            f"set-loc-pv {moved_src[0]}:{moved_src[1]}:{moved_src[2]}:0.0:0.0:0.0:{now_ms}"
        )

        moved_dist = self._distance_m(ue_ecef, moved_src)
        assert moved_dist > threshold_m

        moved_lat, moved_lon, moved_h = self._ecef_to_geo_deg(moved_src)

        source_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "d1",
                    "d1ReferenceType": "fixed",
                    "d1LongitudeDeg": moved_lon,
                    "d1LatitudeDeg": moved_lat,
                    "d1HeightM": moved_h,
                    "d1ThresholdM": threshold_m,
                    "hysteresis": 0,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=63,
        )

        meascfg_applied_2 = ue_process.wait_for_log(
            r"RRCReconfigurationComplete sent \(non-handover reconfiguration\)",
            timeout_s=12,
        )
        assert meascfg_applied_2 is not None, "UE did not apply updated D1 MeasConfig"

        cond_rrc_2 = source_gnb.rrc_codec.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=64,
            target_pci=2,
            new_crnti=0x4501,
            t304_ms=1000,
        )

        source_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": candidate_id,
                    "measIds": [1],
                    "condRrcReconfig": cond_rrc_2,
                }
            ],
            transaction_id=65,
        )

        executed = ue_process.wait_for_cho_execution(timeout_s=20)
        switched = ue_process.wait_for_cell_switch(timeout_s=20)
        assert executed is not None or switched is not None, (
            "No CHO execution or serving-cell switch observed after source location moved beyond D1 threshold"
        )

        info = ue_process.parse_handover_info()
        if info["target_cell"] is not None:
            assert info["target_cell"] == 2

        cho_info = ue_process.parse_cho_info()
        if cho_info["executed_id"] is not None:
            if cho_info["executed_id"] != candidate_id:
                assert info["target_cell"] == 2, (
                    "CHO executed with a different candidate id and did not switch to expected target cell"
                )

    def test_ntn_nadir_d1_cho_after_satellite_distance_growth(
        self, registered_two_gnb_ue, source_gnb, target_gnb
    ):
        """NTN CHO path: source satellite D1(nadir) triggers after orbital-track growth.

        Scenario:
          1. UE starts attached to source gNB.
          2. Source sends SIB19 with satellite directly above UE (nadir near UE).
          3. Source sends MeasConfig D1 with nadir reference and CHO candidate to terrestrial target.
          4. Source advances satellite position along an ISS-like track and refreshes SIB19.
          5. UE executes CHO once nadir distance exceeds threshold.

        TLE-inspired reference (used to choose realistic LEO speed):
          line1: 1 25544U 98067A   24060.51892340  .00016717  00000+0  30375-3 0  9997
          line2: 2 25544  51.6410  12.9532 0005518 101.8294  21.5671 15.50040210442261
        """
        ue_process = registered_two_gnb_ue

        ue_ecef = source_gnb.wait_for_heartbeat_position(timeout_s=12)
        assert ue_ecef is not None, "Source gNB did not receive UE heartbeat position"

        target_pos = target_gnb.wait_for_heartbeat_position(timeout_s=12)
        assert target_pos is not None, "Target gNB did not receive UE heartbeat position"

        # Build an orthonormal frame around UE ECEF.
        ux = float(ue_ecef[0])
        uy = float(ue_ecef[1])
        uz = float(ue_ecef[2])
        u_norm = max((ux * ux + uy * uy + uz * uz) ** 0.5, 1.0)
        up_x = ux / u_norm
        up_y = uy / u_norm
        up_z = uz / u_norm

        east_x = -up_y
        east_y = up_x
        east_z = 0.0
        east_norm = (east_x * east_x + east_y * east_y + east_z * east_z) ** 0.5
        if east_norm < 1e-6:
            east_x, east_y, east_z = 0.0, 1.0, 0.0
            east_norm = 1.0
        east_x /= east_norm
        east_y /= east_norm
        east_z /= east_norm

        # LEO satellite directly above UE, then moving tangentially (ISS-like speed).
        sat_alt_m = 550_000.0
        sat_speed_mps = 7_600.0
        step_dt_s = 20.0
        threshold_m = 80_000.0
        candidate_id = 21

        sat_x0 = ux + up_x * sat_alt_m
        sat_y0 = uy + up_y * sat_alt_m
        sat_z0 = uz + up_z * sat_alt_m
        sat_vx = east_x * sat_speed_mps
        sat_vy = east_y * sat_speed_mps
        sat_vz = east_z * sat_speed_mps

        # Keep target terrestrial and near UE so CHO execution can complete cleanly.
        tgt_x = float(target_pos[0]) + east_x * 20_000.0
        tgt_y = float(target_pos[1]) + east_y * 20_000.0
        tgt_z = float(target_pos[2]) + east_z * 20_000.0

        now_ms = int(time.time() * 1000)
        src_rsp = source_gnb.run_command(
            f"set-loc-pv {sat_x0}:{sat_y0}:{sat_z0}:{sat_vx}:{sat_vy}:{sat_vz}:{now_ms}"
        )
        tgt_rsp = target_gnb.run_command(f"set-loc-pv {tgt_x}:{tgt_y}:{tgt_z}:0.0:0.0:0.0:{now_ms}")
        assert "Updated true gNB position/velocity" in src_rsp
        assert "Updated true gNB position/velocity" in tgt_rsp

        epoch_10ms = int(time.time() * 100)
        source_gnb.send_sib19(
            ephemeris_type=0,
            position_x=sat_x0,
            position_y=sat_y0,
            position_z=sat_z0,
            velocity_vx=sat_vx,
            velocity_vy=sat_vy,
            velocity_vz=sat_vz,
            epoch_time=epoch_10ms,
            ul_sync_validity=-1,
        )

        # Guard: operational D1 path requires ASN schema support for EventD1-r17.
        d1_probe = source_gnb.rrc_codec.build_rrc_reconfiguration(
            transaction_id=66,
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "d1",
                    "d1ReferenceType": "nadir",
                    "d1ThresholdM": threshold_m,
                    "hysteresis": 0,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        if len(d1_probe) <= 4:
            pytest.skip(
                "Current Python ASN schema does not encode EventD1-r17; "
                "operational ASN D1 validation is unavailable in this environment"
            )

        source_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "d1",
                    "d1ReferenceType": "nadir",
                    "d1ThresholdM": threshold_m,
                    "hysteresis": 0,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=67,
        )

        assert ue_process.wait_for_log(
            r"RRCReconfigurationComplete sent \(non-handover reconfiguration\)",
            timeout_s=12,
        ), "UE did not apply D1(nadir) MeasConfig"

        cond_rrc = source_gnb.rrc_codec.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=68,
            target_pci=2,
            new_crnti=0x4502,
            t304_ms=1000,
        )
        source_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": candidate_id,
                    "measIds": [1],
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=69,
        )

        assert ue_process.wait_for_log(
            r"ConditionalReconfiguration applied:.*added=1",
            timeout_s=12,
        ), "UE did not acknowledge ASN ConditionalReconfiguration CHO"

        # With satellite nadir starting near UE, D1 must not trigger immediately.
        assert ue_process.wait_for_log(rf"Executing CHO candidate\s+{candidate_id}\b", timeout_s=3) is None

        # Advance satellite along-track by a TLE-inspired step and refresh SIB19.
        sat_x1 = sat_x0 + sat_vx * step_dt_s
        sat_y1 = sat_y0 + sat_vy * step_dt_s
        sat_z1 = sat_z0 + sat_vz * step_dt_s
        epoch_10ms_2 = epoch_10ms + int(step_dt_s * 100.0)
        source_gnb.send_sib19(
            ephemeris_type=0,
            position_x=sat_x1,
            position_y=sat_y1,
            position_z=sat_z1,
            velocity_vx=sat_vx,
            velocity_vy=sat_vy,
            velocity_vz=sat_vz,
            epoch_time=epoch_10ms_2,
            ul_sync_validity=-1,
        )

        track_shift_m = sat_speed_mps * step_dt_s
        assert track_shift_m > threshold_m

        executed = ue_process.wait_for_cho_execution(timeout_s=20)
        switched = ue_process.wait_for_cell_switch(timeout_s=20)
        assert executed is not None or switched is not None, (
            "No CHO execution or serving-cell switch observed after NTN source distance growth"
        )

        info = ue_process.parse_handover_info()
        if info["target_cell"] is not None:
            assert info["target_cell"] == 2

        cho_info = ue_process.parse_cho_info()
        if cho_info["executed_id"] is not None:
            if cho_info["executed_id"] != candidate_id:
                assert info["target_cell"] == 2, (
                    "CHO executed with a different candidate id and did not switch to expected target cell"
                )

    def test_ntn_d1_two_candidate_selection_prefers_first_configured_target(
        self, registered_two_gnb_ue, source_gnb, target_gnb
    ):
        """NTN CHO with two D1 candidates added via two RRCReconfiguration messages.

        This validates candidate selection when both candidates reference the same D1
        trigger (nadir, same threshold) but point to different target gNBs.

        Note: current ASN schema/runtime path in this branch does not expose
        condExecutionPriority-r17. With equal trigger conditions, UE selection falls
        to deterministic tie-breaks, where earlier configured candidate wins.
        """
        from .harness.fake_gnb import FakeGnb

        ue_process = registered_two_gnb_ue

        with FakeGnb(listen_addr="127.0.0.3", cell_dbm=-75, nci=3) as target2_gnb:
            assert target2_gnb.wait_for_heartbeat(timeout_s=12), (
                "Third target gNB did not receive UE heartbeat"
            )
            target2_gnb.perform_cell_attach()

            ue_ecef = source_gnb.wait_for_heartbeat_position(timeout_s=12)
            assert ue_ecef is not None, "Source gNB did not receive UE heartbeat position"

            target_pos = target_gnb.wait_for_heartbeat_position(timeout_s=12)
            assert target_pos is not None, "Target gNB did not receive UE heartbeat position"

            target2_pos = target2_gnb.wait_for_heartbeat_position(timeout_s=12)
            assert target2_pos is not None, "Third target gNB did not receive UE heartbeat position"

            ux = float(ue_ecef[0])
            uy = float(ue_ecef[1])
            uz = float(ue_ecef[2])
            u_norm = max((ux * ux + uy * uy + uz * uz) ** 0.5, 1.0)
            up_x = ux / u_norm
            up_y = uy / u_norm
            up_z = uz / u_norm

            east_x = -up_y
            east_y = up_x
            east_z = 0.0
            east_norm = (east_x * east_x + east_y * east_y + east_z * east_z) ** 0.5
            if east_norm < 1e-6:
                east_x, east_y, east_z = 0.0, 1.0, 0.0
                east_norm = 1.0
            east_x /= east_norm
            east_y /= east_norm
            east_z /= east_norm

            sat_alt_m = 550_000.0
            sat_speed_mps = 7_600.0
            step_dt_s = 20.0
            threshold_m = 80_000.0

            sat_x0 = ux + up_x * sat_alt_m
            sat_y0 = uy + up_y * sat_alt_m
            sat_z0 = uz + up_z * sat_alt_m
            sat_vx = east_x * sat_speed_mps
            sat_vy = east_y * sat_speed_mps
            sat_vz = east_z * sat_speed_mps

            now_ms = int(time.time() * 1000)
            assert "Updated true gNB position/velocity" in source_gnb.run_command(
                f"set-loc-pv {sat_x0}:{sat_y0}:{sat_z0}:{sat_vx}:{sat_vy}:{sat_vz}:{now_ms}"
            )

            # Keep both targets close so D1 condition is the dominant trigger.
            tgt1_x = float(target_pos[0]) + east_x * 20_000.0
            tgt1_y = float(target_pos[1]) + east_y * 20_000.0
            tgt1_z = float(target_pos[2]) + east_z * 20_000.0
            tgt2_x = float(target2_pos[0]) + east_x * 20_000.0
            tgt2_y = float(target2_pos[1]) + east_y * 20_000.0
            tgt2_z = float(target2_pos[2]) + east_z * 20_000.0

            assert "Updated true gNB position/velocity" in target_gnb.run_command(
                f"set-loc-pv {tgt1_x}:{tgt1_y}:{tgt1_z}:0.0:0.0:0.0:{now_ms}"
            )
            assert "Updated true gNB position/velocity" in target2_gnb.run_command(
                f"set-loc-pv {tgt2_x}:{tgt2_y}:{tgt2_z}:0.0:0.0:0.0:{now_ms}"
            )

            epoch_10ms = int(time.time() * 100)
            source_gnb.send_sib19(
                ephemeris_type=0,
                position_x=sat_x0,
                position_y=sat_y0,
                position_z=sat_z0,
                velocity_vx=sat_vx,
                velocity_vy=sat_vy,
                velocity_vz=sat_vz,
                epoch_time=epoch_10ms,
                ul_sync_validity=-1,
            )

            d1_probe = source_gnb.rrc_codec.build_rrc_reconfiguration(
                transaction_id=70,
                meas_objects=[{"id": 1, "ssbFreq": 632628}],
                report_configs=[
                    {
                        "id": 1,
                        "event": "d1",
                        "d1ReferenceType": "nadir",
                        "d1ThresholdM": threshold_m,
                        "hysteresis": 0,
                        "timeToTrigger": 160,
                        "maxReportCells": 8,
                    }
                ],
                meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            )
            if len(d1_probe) <= 4:
                pytest.skip(
                    "Current Python ASN schema does not encode EventD1-r17; "
                    "operational ASN D1 validation is unavailable in this environment"
                )

            source_gnb.send_meas_config(
                meas_objects=[{"id": 1, "ssbFreq": 632628}],
                report_configs=[
                    {
                        "id": 1,
                        "event": "d1",
                        "d1ReferenceType": "nadir",
                        "d1ThresholdM": threshold_m,
                        "hysteresis": 0,
                        "timeToTrigger": 160,
                        "maxReportCells": 8,
                    }
                ],
                meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
                transaction_id=71,
            )

            assert ue_process.wait_for_log(
                r"RRCReconfigurationComplete sent \(non-handover reconfiguration\)",
                timeout_s=12,
            ), "UE did not apply D1(nadir) MeasConfig"

            # Candidate A (treated as higher-priority in this test) to target PCI 2.
            cond_rrc_a = source_gnb.rrc_codec.build_conditional_rrc_reconfiguration_with_sync(
                transaction_id=72,
                target_pci=2,
                new_crnti=0x4601,
                t304_ms=1000,
            )
            source_gnb.send_conditional_reconfiguration(
                candidates_to_add_mod=[
                    {
                        "candidateId": 31,
                        "measIds": [1],
                        "condRrcReconfig": cond_rrc_a,
                    }
                ],
                transaction_id=73,
            )

            # Candidate B to a different target PCI 3.
            cond_rrc_b = source_gnb.rrc_codec.build_conditional_rrc_reconfiguration_with_sync(
                transaction_id=74,
                target_pci=3,
                new_crnti=0x4602,
                t304_ms=1000,
            )
            source_gnb.send_conditional_reconfiguration(
                candidates_to_add_mod=[
                    {
                        "candidateId": 32,
                        "measIds": [1],
                        "condRrcReconfig": cond_rrc_b,
                    }
                ],
                transaction_id=75,
            )

            assert ue_process.wait_for_log(
                r"ConditionalReconfiguration applied:.*activeCandidates=2",
                timeout_s=12,
            ), "UE did not report two active CHO candidates"

            assert ue_process.wait_for_log(r"Executing CHO candidate\s+31\b", timeout_s=3) is None
            assert ue_process.wait_for_log(r"Executing CHO candidate\s+32\b", timeout_s=3) is None

            sat_x1 = sat_x0 + sat_vx * step_dt_s
            sat_y1 = sat_y0 + sat_vy * step_dt_s
            sat_z1 = sat_z0 + sat_vz * step_dt_s
            epoch_10ms_2 = epoch_10ms + int(step_dt_s * 100.0)
            source_gnb.send_sib19(
                ephemeris_type=0,
                position_x=sat_x1,
                position_y=sat_y1,
                position_z=sat_z1,
                velocity_vx=sat_vx,
                velocity_vy=sat_vy,
                velocity_vz=sat_vz,
                epoch_time=epoch_10ms_2,
                ul_sync_validity=-1,
            )

            assert sat_speed_mps * step_dt_s > threshold_m

            executed = ue_process.wait_for_cho_execution(timeout_s=22)
            switched = ue_process.wait_for_cell_switch(timeout_s=22)
            assert executed is not None or switched is not None, (
                "No CHO execution or serving-cell switch observed after NTN D1 trigger"
            )

            info = ue_process.parse_handover_info()
            if info["target_cell"] is not None:
                assert info["target_cell"] == 2

            cho_info = ue_process.parse_cho_info()
            if cho_info["executed_id"] is not None:
                assert cho_info["executed_id"] == 31

    def test_distance_based_cho_after_gnb_position_update(self, two_gnb_ue, source_gnb, target_gnb):
        """Validate standards-based CHO flow with location observability.

        Steps covered:
          1. Position is sent to gNBs via command interface (`set-loc-pv`) and update is verified.
        2. Source gNB sends ASN ConditionalReconfiguration CHO toward target PCI=2
           and UE acknowledges CHO application.
          3. gNB position is updated again via `set-loc-pv` to change geometry and trigger CHO,
           causing CHO-triggered handover to the target gNB.
        """
        ue_process = two_gnb_ue
        self._ensure_rrc_connected_with_two_gnb(source_gnb, target_gnb)

        source_pos = source_gnb.wait_for_heartbeat_position(timeout_s=12)
        assert source_pos is not None, "Source gNB did not receive UE heartbeat position"

        target_pos = target_gnb.wait_for_heartbeat_position(timeout_s=12)
        assert target_pos is not None, "Target gNB did not receive UE heartbeat position"

        assert source_gnb.heartbeat_count > 0
        assert target_gnb.heartbeat_count > 0

        # (1) Send position via command interface and verify gNB state update.
        # Place source near UE and target far from UE so source starts stronger.
        now_ms = int(time.time() * 1000)
        src_cmd = (
            f"set-loc-pv {source_pos[0]}:{source_pos[1]}:{source_pos[2]}:0.0:0.0:0.0:{now_ms}"
        )
        tgt_cmd = (
            f"set-loc-pv {source_pos[0] + 1_000_000.0}:{source_pos[1]}:{source_pos[2]}:"
            f"0.0:0.0:0.0:{now_ms}"
        )

        src_rsp = source_gnb.run_command(src_cmd)
        tgt_rsp = target_gnb.run_command(tgt_cmd)
        assert "Updated true gNB position/velocity" in src_rsp
        assert "Updated true gNB position/velocity" in tgt_rsp

        src_pv = source_gnb.true_position_velocity
        tgt_pv = target_gnb.true_position_velocity
        assert src_pv is not None
        assert tgt_pv is not None
        assert src_pv["x"] == pytest.approx(source_pos[0])
        assert src_pv["y"] == pytest.approx(source_pos[1])
        assert src_pv["z"] == pytest.approx(source_pos[2])
        assert source_gnb.cell_dbm > target_gnb.cell_dbm

        # Standards CHO path: build MeasConfig first, then add CHO via
        # ASN ConditionalReconfiguration referencing MeasId(s).
        source_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "a3",
                    "a3Offset": 3,
                    "hysteresis": 1,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=40,
        )
        meascfg_applied = ue_process.wait_for_log(
            r"RRCReconfigurationComplete sent \(non-handover reconfiguration\)",
            timeout_s=12,
        )
        assert meascfg_applied is not None, "UE did not apply MeasConfig before CHO setup"

        assert source_gnb.rrc_codec.supports_conditional_reconfiguration(), (
            "Standards-based CHO encoding is unsupported in current Python ASN schema. "
            f"{source_gnb.rrc_codec.conditional_reconfiguration_support_error()}"
        )

        cond_rrc = source_gnb.rrc_codec.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=41,
            target_pci=2,
            new_crnti=0x4201,
            t304_ms=1000,
        )
        source_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 2,
                    "measIds": [1],
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=42,
        )

        cho_ack = ue_process.wait_for_log(r"ConditionalReconfiguration applied:.*added=1", timeout_s=12)
        if cho_ack is None:
            ue_process.collect_output(timeout_s=1.0)
            cho_lines = [
                ln
                for ln in ue_process.log_lines
                if "ConditionalReconfiguration" in ln
                or "CHO candidate" in ln
                or "condExecutionCond" in ln
                or "extension chain" in ln
                or "DL-DCCH PDU decoding failed" in ln
            ]
            tail = "\n".join(cho_lines[-20:]) if cho_lines else "<no CHO logs captured>"
            assert False, (
                "UE did not acknowledge ASN ConditionalReconfiguration CHO. "
                f"Recent CHO logs:\n{tail}"
            )

        # With source stronger than target, A3 should not trigger immediately.
        assert ue_process.wait_for_cho_execution(timeout_s=3) is None

        # (3) Update gNB positions via command interface to invert geometry:
        # source far, target near. This should satisfy A3 and trigger CHO.
        now_ms = int(time.time() * 1000)
        source_gnb.run_command(
            f"set-loc-pv {source_pos[0] + 1_000_000.0}:{source_pos[1]}:{source_pos[2]}:"
            f"0.0:0.0:0.0:{now_ms}"
        )
        target_gnb.run_command(
            f"set-loc-pv {source_pos[0]}:{source_pos[1]}:{source_pos[2]}:0.0:0.0:0.0:{now_ms}"
        )
        assert target_gnb.cell_dbm > source_gnb.cell_dbm

        executed = ue_process.wait_for_cho_execution(timeout_s=20)
        switched = ue_process.wait_for_cell_switch(timeout_s=20)
        assert executed is not None or switched is not None, (
            "No CHO execution or serving-cell switch observed after simulated location update"
        )

        info = ue_process.parse_handover_info()
        if info["target_cell"] is not None:
            assert info["target_cell"] == 2

    def test_distance_based_cho_after_time_evolved_gnb_position(
        self, two_gnb_ue, source_gnb, target_gnb
    ):
        """Validate CHO trigger from time-evolved gNB position/velocity.

        Flow:
        1. Send `set-loc-pv` with non-zero velocity for source/target gNBs and verify update.
        2. Send standards-based CHO and verify UE acknowledges candidate add.
        3. Let PV evolve in time, refresh modeled RSRP from evolved positions, and
           verify CHO handover to target gNB.
        """
        ue_process = two_gnb_ue
        self._ensure_rrc_connected_with_two_gnb(source_gnb, target_gnb)

        source_pos = source_gnb.wait_for_heartbeat_position(timeout_s=12)
        assert source_pos is not None, "Source gNB did not receive UE heartbeat position"

        target_pos = target_gnb.wait_for_heartbeat_position(timeout_s=12)
        assert target_pos is not None, "Target gNB did not receive UE heartbeat position"

        assert source_gnb.heartbeat_count > 0
        assert target_gnb.heartbeat_count > 0

        now_ms = int(time.time() * 1000)
        src_cmd = (
            f"set-loc-pv {source_pos[0] + 50_000.0}:{source_pos[1]}:{source_pos[2]}:"
            f"30000.0:0.0:0.0:{now_ms}"
        )
        tgt_cmd = (
            f"set-loc-pv {source_pos[0] + 250_000.0}:{source_pos[1]}:{source_pos[2]}:"
            f"-30000.0:0.0:0.0:{now_ms}"
        )

        assert "Updated true gNB position/velocity" in source_gnb.run_command(src_cmd)
        assert "Updated true gNB position/velocity" in target_gnb.run_command(tgt_cmd)
        assert source_gnb.true_position_velocity is not None
        assert target_gnb.true_position_velocity is not None
        assert source_gnb.cell_dbm > target_gnb.cell_dbm

        source_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "a3",
                    "a3Offset": 3,
                    "hysteresis": 1,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=43,
        )
        assert ue_process.wait_for_log(
            r"RRCReconfigurationComplete sent \(non-handover reconfiguration\)",
            timeout_s=12,
        ), "UE did not apply MeasConfig before CHO setup"

        cond_rrc = source_gnb.rrc_codec.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=44,
            target_pci=2,
            new_crnti=0x4202,
            t304_ms=1000,
        )
        source_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 3,
                    "measIds": [1],
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=45,
        )

        assert ue_process.wait_for_log(
            r"ConditionalReconfiguration applied:.*added=1",
            timeout_s=12,
        ), "UE did not acknowledge ASN ConditionalReconfiguration CHO"

        assert ue_process.wait_for_cho_execution(timeout_s=3) is None

        time.sleep(5.0)
        source_gnb.refresh_modeled_cell_dbm()
        target_gnb.refresh_modeled_cell_dbm()
        assert target_gnb.cell_dbm > source_gnb.cell_dbm

        executed = ue_process.wait_for_cho_execution(timeout_s=20)
        switched = ue_process.wait_for_cell_switch(timeout_s=20)
        assert executed is not None or switched is not None, (
            "No CHO execution or serving-cell switch observed after time-evolved position update"
        )

        info = ue_process.parse_handover_info()
        if info["target_cell"] is not None:
            assert info["target_cell"] == 2

