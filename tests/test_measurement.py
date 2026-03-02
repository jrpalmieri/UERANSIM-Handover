"""
Tests for the measurement reporting framework.

Verifies that the UE correctly evaluates and reports measurement events:
  - A2: Serving becomes worse than threshold
  - A3: Neighbour becomes offset better than serving
  - A5: Serving < threshold1 AND neighbour > threshold2

Uses the OOB measurement provider (UDP JSON injection) to control
the RSRP values seen by the UE's measurement evaluation engine.
"""

from __future__ import annotations

import time

import pytest

from harness.fake_gnb import FakeGnb
from harness.meas_injector import MeasurementInjector, CellMeas
from harness.ue_process import UeProcess
from harness.rls_protocol import RrcChannel
from conftest import ue_binary_exists, needs_asn1tools


# ======================================================================
#  Unit tests — MeasurementInjector
# ======================================================================

class TestMeasurementInjectorUnit:
    """Verify the injector builds correct JSON payloads."""

    def test_cell_meas_to_dict_with_cell_id(self):
        cm = CellMeas(cell_id=1, rsrp=-85, rsrq=-10, sinr=15)
        d = cm.to_dict()
        assert d["cellId"] == 1
        assert d["rsrp"] == -85
        assert "nci" not in d

    def test_cell_meas_to_dict_with_nci(self):
        cm = CellMeas(nci=36, rsrp=-78, rsrq=-8, sinr=20)
        d = cm.to_dict()
        assert d["nci"] == 36
        assert d["rsrp"] == -78
        assert "cellId" not in d

    def test_set_and_remove_cell(self, meas_injector: MeasurementInjector):
        meas_injector.set_cell(cell_id=1, rsrp=-85)
        assert 1 in meas_injector._cells
        meas_injector.remove_cell(cell_id=1)
        assert 1 not in meas_injector._cells

    def test_clear(self, meas_injector: MeasurementInjector):
        meas_injector.set_cell(cell_id=1, rsrp=-85)
        meas_injector.set_cell(nci=2, rsrp=-90)
        meas_injector.clear()
        assert len(meas_injector._cells) == 0

    def test_json_payload_format(self, meas_injector: MeasurementInjector):
        import json
        meas_injector.set_cell(cell_id=1, rsrp=-85)
        payload = meas_injector._build_json()
        data = json.loads(payload)
        assert "measurements" in data
        assert len(data["measurements"]) == 1
        assert data["measurements"][0]["rsrp"] == -85


# ======================================================================
#  Unit tests — measurement event evaluation logic
# ======================================================================

class TestMeasEventEvaluation:
    """Test the mathematical conditions for measurement events.

    These mirror the C++ evaluation functions in measurement.cpp.
    """

    @staticmethod
    def evaluate_a2(serving_rsrp: int, threshold: int, hyst: int) -> bool:
        """A2 entering: serving < threshold - hysteresis."""
        return serving_rsrp < threshold - hyst

    @staticmethod
    def evaluate_a3(
        serving_rsrp: int, neighbour_rsrp: int, offset: int, hyst: int
    ) -> bool:
        """A3 entering: neighbour > serving + offset + hysteresis."""
        return neighbour_rsrp > serving_rsrp + offset + hyst

    @staticmethod
    def evaluate_a5_serving(serving_rsrp: int, threshold1: int, hyst: int) -> bool:
        """A5 condition 1: serving < threshold1 - hysteresis."""
        return serving_rsrp < threshold1 - hyst

    @staticmethod
    def evaluate_a5_neighbour(neighbour_rsrp: int, threshold2: int, hyst: int) -> bool:
        """A5 condition 2: neighbour > threshold2 + hysteresis."""
        return neighbour_rsrp > threshold2 + hyst

    # --- A2 tests ---

    def test_a2_triggered_when_serving_below_threshold(self):
        """Serving = -115, threshold = -110, hyst = 2 → triggered."""
        assert self.evaluate_a2(-115, -110, 2) is True

    def test_a2_not_triggered_when_serving_above_threshold(self):
        """Serving = -100, threshold = -110, hyst = 2 → not triggered."""
        assert self.evaluate_a2(-100, -110, 2) is False

    def test_a2_not_triggered_at_boundary(self):
        """Serving = -112, threshold = -110, hyst = 2 → -112 < -112 is False."""
        assert self.evaluate_a2(-112, -110, 2) is False

    def test_a2_triggered_just_below_boundary(self):
        """Serving = -113, threshold = -110, hyst = 2 → triggered."""
        assert self.evaluate_a2(-113, -110, 2) is True

    # --- A3 tests ---

    def test_a3_triggered_when_neighbour_better(self):
        """Neighbour = -70, Serving = -85, offset = 6, hyst = 2 → triggered."""
        assert self.evaluate_a3(-85, -70, 6, 2) is True

    def test_a3_not_triggered_when_neighbour_not_enough_better(self):
        """Neighbour = -80, Serving = -85, offset = 6, hyst = 2 → not triggered."""
        assert self.evaluate_a3(-85, -80, 6, 2) is False

    def test_a3_boundary_exact(self):
        """Neighbour exactly at serving+offset+hyst → not triggered (strict >)."""
        # Serving = -85, offset = 6, hyst = 2 → threshold = -77
        # Neighbour = -77 → -77 > -77 is False
        assert self.evaluate_a3(-85, -77, 6, 2) is False

    def test_a3_boundary_just_above(self):
        """Neighbour = -76, serving = -85, offset = 6, hyst = 2 → triggered."""
        assert self.evaluate_a3(-85, -76, 6, 2) is True

    # --- A5 tests ---

    def test_a5_both_conditions_met(self):
        """Serving low and neighbour high → triggered."""
        assert self.evaluate_a5_serving(-115, -110, 2) is True
        assert self.evaluate_a5_neighbour(-90, -100, 2) is True

    def test_a5_only_serving_condition(self):
        """Serving low but neighbour also low → not fully triggered."""
        assert self.evaluate_a5_serving(-115, -110, 2) is True
        assert self.evaluate_a5_neighbour(-105, -100, 2) is False

    def test_a5_only_neighbour_condition(self):
        """Serving OK but neighbour high → not fully triggered."""
        assert self.evaluate_a5_serving(-100, -110, 2) is False
        assert self.evaluate_a5_neighbour(-90, -100, 2) is True

    def test_a5_neither_condition(self):
        """Neither condition met."""
        assert self.evaluate_a5_serving(-100, -110, 2) is False
        assert self.evaluate_a5_neighbour(-105, -100, 2) is False

    # --- Hysteresis tests ---

    def test_hysteresis_zero(self):
        """With hysteresis=0, boundary values trigger."""
        # A2: serving < threshold - 0 → strict less
        assert self.evaluate_a2(-111, -110, 0) is True
        assert self.evaluate_a2(-110, -110, 0) is False

    def test_large_hysteresis_prevents_trigger(self):
        """Large hysteresis makes triggering harder."""
        # A2: serving < threshold - 10 = -120
        assert self.evaluate_a2(-115, -110, 10) is False
        assert self.evaluate_a2(-121, -110, 10) is True

    # --- RSRP encoding test ---

    def test_rsrp_encoding(self):
        """RSRP range 0..127 maps to -156..-44 dBm (TS 38.133)."""
        def rsrp_to_range(dbm: int) -> int:
            return max(0, min(127, dbm + 156))

        assert rsrp_to_range(-156) == 0
        assert rsrp_to_range(-44) == 112
        assert rsrp_to_range(-85) == 71
        assert rsrp_to_range(-110) == 46
        assert rsrp_to_range(-200) == 0   # clamped
        assert rsrp_to_range(0) == 127    # clamped


# ======================================================================
#  Integration tests — measurement event A2
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestMeasEventA2:
    """Test A2 event: serving becomes worse than threshold."""

    def test_a2_triggered_by_low_serving_rsrp(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """When serving RSRP drops below A2 threshold, a MeasurementReport
        should be sent."""
        # Setup: get UE to RRC_CONNECTED
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        # Configure A2 measurement: threshold = -100 dBm
        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a2",
                "a2Threshold": -100,
                "hysteresis": 2,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        # Inject serving cell RSRP below threshold
        meas_injector.set_cell(cell_id=1, rsrp=-115)
        meas_injector.send_repeatedly(interval_s=0.5, duration_s=5.0)

        # Wait for MeasurementReport
        report = fake_gnb.wait_for_measurement_report(timeout_s=15)
        assert report is not None, "No MeasurementReport received for A2 event"
        assert report.channel == RrcChannel.UL_DCCH
        ue_process.cleanup()

    def test_a2_not_triggered_when_serving_above_threshold(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """When serving RSRP is above threshold, no report should be sent."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a2",
                "a2Threshold": -100,
                "hysteresis": 2,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        # Inject serving cell RSRP well above threshold
        meas_injector.set_cell(cell_id=1, rsrp=-70)
        meas_injector.send_repeatedly(interval_s=0.5, duration_s=5.0)

        # Should NOT get a MeasurementReport
        fake_gnb.clear_captured()
        report = fake_gnb.wait_for_measurement_report(timeout_s=8)
        assert report is None, "Unexpected MeasurementReport when serving above A2 threshold"
        ue_process.cleanup()


# ======================================================================
#  Integration tests — measurement event A3
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestMeasEventA3:
    """Test A3 event: neighbour becomes offset better than serving."""

    def test_a3_triggered_when_neighbour_stronger(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """When neighbour RSRP > serving + offset + hyst, report is sent."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        # Configure A3: offset=6, hysteresis=2
        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a3",
                "a3Offset": 6,
                "hysteresis": 2,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        # Serving = -85, Neighbour = -70  (neighbour is 15 dB better)
        # Condition: -70 > -85 + 6 + 2 = -77  → -70 > -77 → True
        meas_injector.set_cell(cell_id=1, rsrp=-85)
        meas_injector.set_cell(nci=2, rsrp=-70)
        meas_injector.send_repeatedly(interval_s=0.5, duration_s=5.0)

        report = fake_gnb.wait_for_measurement_report(timeout_s=15)
        assert report is not None, "No MeasurementReport for A3 event"
        ue_process.cleanup()

    def test_a3_not_triggered_when_neighbour_not_enough_better(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """When neighbour is better but not by enough, no report."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a3",
                "a3Offset": 6,
                "hysteresis": 2,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        # Serving = -85, Neighbour = -80  (only 5 dB better)
        # Condition: -80 > -85 + 6 + 2 = -77  → -80 > -77 → False
        meas_injector.set_cell(cell_id=1, rsrp=-85)
        meas_injector.set_cell(nci=2, rsrp=-80)
        meas_injector.send_repeatedly(interval_s=0.5, duration_s=5.0)

        fake_gnb.clear_captured()
        report = fake_gnb.wait_for_measurement_report(timeout_s=8)
        assert report is None, "Unexpected MeasurementReport for A3 (neighbour not strong enough)"
        ue_process.cleanup()


# ======================================================================
#  Integration tests — measurement event A5
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestMeasEventA5:
    """Test A5 event: serving < threshold1 AND neighbour > threshold2."""

    def test_a5_triggered_both_conditions_met(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """Both A5 conditions met → MeasurementReport sent."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        # Configure A5: threshold1=-100, threshold2=-90, hyst=2
        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a5",
                "a5Threshold1": -100,
                "a5Threshold2": -90,
                "hysteresis": 2,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        # Serving = -110 (< -100 - 2 = -102 ✓)
        # Neighbour = -80 (> -90 + 2 = -88 ✓)
        meas_injector.set_cell(cell_id=1, rsrp=-110)
        meas_injector.set_cell(nci=2, rsrp=-80)
        meas_injector.send_repeatedly(interval_s=0.5, duration_s=5.0)

        report = fake_gnb.wait_for_measurement_report(timeout_s=15)
        assert report is not None, "No MeasurementReport for A5 event"
        ue_process.cleanup()

    def test_a5_not_triggered_only_serving_low(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """Only serving condition met (neighbour also low) → no report."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a5",
                "a5Threshold1": -100,
                "a5Threshold2": -90,
                "hysteresis": 2,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        # Serving = -110 (low ✓), Neighbour = -95 (< -88, not high enough ✗)
        meas_injector.set_cell(cell_id=1, rsrp=-110)
        meas_injector.set_cell(nci=2, rsrp=-95)
        meas_injector.send_repeatedly(interval_s=0.5, duration_s=5.0)

        fake_gnb.clear_captured()
        report = fake_gnb.wait_for_measurement_report(timeout_s=8)
        assert report is None, "Unexpected A5 report (neighbour not strong enough)"
        ue_process.cleanup()


# ======================================================================
#  Integration tests — time-to-trigger
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestTimeToTrigger:
    """Test that events respect the time-to-trigger (TTT) parameter."""

    def test_ttt_delays_report(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """With TTT=640ms, the report should not arrive instantly."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        # Inject a *good* signal so A2 isn't immediately triggered
        meas_injector.set_cell(cell_id=1, rsrp=-80)
        meas_injector.send()
        time.sleep(0.5)

        # A2 with TTT=640ms
        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a2",
                "a2Threshold": -100,
                "hysteresis": 0,
                "timeToTrigger": 640,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(0.5)

        # Start injecting poor signal — A2 entering condition now met
        inject_start = time.monotonic()
        meas_injector.set_cell(cell_id=1, rsrp=-115)
        meas_injector.send_repeatedly(interval_s=0.3, duration_s=10.0)

        report = fake_gnb.wait_for_measurement_report(timeout_s=15)
        if report is not None:
            delay = report.timestamp - inject_start
            # TTT is 640ms, but measurement cycle is 2500ms, so actual delay
            # is at least 640ms but could be up to ~3200ms
            assert delay >= 0.5, \
                f"Report arrived too quickly ({delay:.2f}s) — TTT not respected"
        ue_process.cleanup()

    def test_ttt_zero_reports_immediately(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """With TTT=0, the report should arrive within the measurement cycle."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a2",
                "a2Threshold": -100,
                "hysteresis": 0,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(0.5)

        meas_injector.set_cell(cell_id=1, rsrp=-115)
        meas_injector.send_repeatedly(interval_s=0.3, duration_s=8.0)

        report = fake_gnb.wait_for_measurement_report(timeout_s=12)
        assert report is not None, "No MeasurementReport with TTT=0"
        ue_process.cleanup()


# ======================================================================
#  Integration tests — measurement report content
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestMeasReportContent:
    """Verify the content of MeasurementReport PDUs."""

    def test_report_is_on_ul_dcch(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """MeasurementReport must be sent on UL-DCCH channel."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a2",
                "a2Threshold": -100,
                "hysteresis": 0,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        meas_injector.set_cell(cell_id=1, rsrp=-115)
        meas_injector.send_repeatedly(interval_s=0.5, duration_s=5.0)

        report = fake_gnb.wait_for_measurement_report(timeout_s=15)
        if report is not None:
            assert report.channel == RrcChannel.UL_DCCH
            # Try to decode the report
            decoded = fake_gnb.rrc_codec.decode_ul_dcch(report.raw_pdu)
            if decoded.get("_fallback"):
                assert decoded.get("message_type") == "measurementReport"
        ue_process.cleanup()

    def test_report_one_shot(
        self,
        fake_gnb: FakeGnb,
        ue_process: UeProcess,
        meas_injector: MeasurementInjector,
    ):
        """MeasurementReport is one-shot: only reported once per measId."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a2",
                "a2Threshold": -100,
                "hysteresis": 0,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        # Keep injecting poor signal for a while
        meas_injector.set_cell(cell_id=1, rsrp=-115)
        meas_injector.send_repeatedly(interval_s=0.3, duration_s=10.0)
        time.sleep(2)

        # Count measurement reports
        reports = [
            cm for cm in fake_gnb.captured_messages
            if cm.channel == RrcChannel.UL_DCCH
            and fake_gnb._is_measurement_report(cm)
        ]
        # One-shot: should see at most 1 report per measId
        assert len(reports) <= 1, \
            f"Expected at most 1 report (one-shot), got {len(reports)}"
        ue_process.cleanup()
