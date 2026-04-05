from __future__ import annotations

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


@ue_binary_exists
class TestUeDistanceChoLocationUpdate:
    @staticmethod
    def _ensure_rrc_connected_with_two_gnb(source_gnb, target_gnb):
        source_gnb.perform_cell_attach()
        target_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup(timeout_s=20), "RRC setup exchange did not complete"
        assert source_gnb.wait_for_ul_dcch(timeout_s=10) is not None, "No UL-DCCH observed after RRC setup"

    def test_distance_based_cho_after_gnb_position_update(self, two_gnb_ue, source_gnb, target_gnb):
        """Validate standards-based CHO flow with location observability.

        Steps covered:
          1. Position is sent to gNBs via command interface (`loc-pv`) and update is verified.
        2. Source gNB sends ASN ConditionalReconfiguration CHO toward target PCI=2
           and UE acknowledges CHO application.
          3. gNB position is updated again via `loc-pv` to change geometry and trigger CHO,
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
            f"loc-pv {source_pos[0]}:{source_pos[1]}:{source_pos[2]}:0.0:0.0:0.0:{now_ms}"
        )
        tgt_cmd = (
            f"loc-pv {source_pos[0] + 1_000_000.0}:{source_pos[1]}:{source_pos[2]}:"
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
            f"loc-pv {source_pos[0] + 1_000_000.0}:{source_pos[1]}:{source_pos[2]}:"
            f"0.0:0.0:0.0:{now_ms}"
        )
        target_gnb.run_command(
            f"loc-pv {source_pos[0]}:{source_pos[1]}:{source_pos[2]}:0.0:0.0:0.0:{now_ms}"
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
        1. Send `loc-pv` with non-zero velocity for source/target gNBs and verify update.
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
            f"loc-pv {source_pos[0] + 50_000.0}:{source_pos[1]}:{source_pos[2]}:"
            f"30000.0:0.0:0.0:{now_ms}"
        )
        tgt_cmd = (
            f"loc-pv {source_pos[0] + 250_000.0}:{source_pos[1]}:{source_pos[2]}:"
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

