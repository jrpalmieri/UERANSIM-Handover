"""
Tests for the measurement reporting framework.

Verifies that the UE correctly evaluates and reports measurement events:
  - A2: Serving becomes worse than threshold
  - A3: Neighbour becomes offset better than serving
  - A5: Serving < threshold1 AND neighbour > threshold2

Technique:
- create two fake GnBs providing different RSRP values
- serving gNB provides an A2 or A3 measurement-event configuration
- change RSRP values in heartbeat ACKs to cause measurement events to trigger
- expect a measurement report RRC message to be sent

"""

from __future__ import annotations

import time

from harness.fake_gnb import FakeGnb
from harness.ue_process import UeProcess
from harness.rls_protocol import RrcChannel
from .conftest import ue_binary_exists, needs_asn1tools


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
#  Integration tests — UE MeasurementReport transmission
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestMeasurementReportTransmission:
    @staticmethod
    def _sustain_measurements(
        source_gnb: FakeGnb,
        target_gnb: FakeGnb,
        duration_s: float = 3,
    ) -> None:
        """Keep both cell measurements fresh while the UE evaluates an event."""
        end = time.monotonic() + duration_s
        while time.monotonic() < end:
            source_gnb.send_heartbeat_ack()
            target_gnb.send_heartbeat_ack()
            time.sleep(0.1)

    @staticmethod
    def _connect_to_source(
        source_gnb: FakeGnb,
        target_gnb: FakeGnb,
    ) -> None:
        """Connect through the source, retrying the timing-sensitive setup once."""
        for _attempt in range(2):
            source_gnb.perform_cell_attach()
            target_gnb.perform_cell_attach()
            # The UE's default heartbeat expiry is only 300 ms. Refresh both
            # cells after the two serialized SI broadcasts before waiting for
            # the setup request/response exchange.
            source_gnb.send_heartbeat_ack()
            target_gnb.send_heartbeat_ack()
            assert source_gnb.perform_rrc_setup(timeout_s=20), (
                "RRC setup exchange did not complete"
            )
            if source_gnb.wait_for_ul_dcch(timeout_s=10) is not None:
                return

        raise AssertionError("No UL-DCCH observed after two RRC setup attempts")

    def test_ue_sends_report_when_a2_event_occurs(
        self,
        source_gnb: FakeGnb,
        target_gnb: FakeGnb,
        two_gnb_ue: UeProcess,
    ):
        """An A2 event produces a parseable MeasurementReport on UL-DCCH."""
        # Both cells must be known to the UE, with the source selected as serving.
        source_gnb.cell_dbm = -60
        target_gnb.cell_dbm = -90
        self._connect_to_source(source_gnb, target_gnb)

        report_config = {
            "id": 1,
            "event": "a2",
            "a2Threshold": -100,
            "hysteresis": 2,
            "timeToTrigger": 0,
            "maxReportCells": 4,
        }
        # Ensure A2 is false when the configuration becomes active. Otherwise a
        # briefly aged-out serving measurement (-156 dBm) can consume the
        # one-shot report before event_start is recorded.
        source_gnb.send_heartbeat_ack()
        target_gnb.send_heartbeat_ack()
        source_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[report_config],
            meas_ids=[{
                "measId": 1,
                "measObjectId": 1,
                "reportConfigId": report_config["id"],
            }],
        )
        assert source_gnb.wait_for_rrc_reconfiguration_complete(timeout_s=10), (
            "UE did not acknowledge the measurement configuration"
        )

        # Record the boundary after configuration, then change the values carried
        # by subsequent heartbeat ACKs.  -115 dBm is below the A2 entry boundary:
        # threshold - hysteresis = -102 dBm.
        event_start = time.monotonic()
        source_gnb.cell_dbm = -115
        target_gnb.cell_dbm = -90
        self._sustain_measurements(source_gnb, target_gnb)

        report = source_gnb.wait_for_measurement_report_since(
            start_ts=event_start,
            timeout_s=20,
        )
        if report is None:
            two_gnb_ue.collect_output(timeout_s=1)
            relevant_logs = "\n".join(
                line for line in two_gnb_ue.log_lines
                if "meas" in line.lower()
                or "cell" in line.lower()
                or "rrc" in line.lower()
                or "heartbeat" in line.lower()
            )
            raise AssertionError(
                "UE sent no MeasurementReport after the A2 condition occurred\n"
                f"Relevant UE logs:\n{relevant_logs}"
            )
        assert report.channel == RrcChannel.UL_DCCH
        assert source_gnb.get_ul_dcch_message_type(report.raw_pdu) == (
            "measurementReport"
        )

    def test_ue_sends_report_when_a3_event_occurs(
        self,
        source_gnb: FakeGnb,
        target_gnb: FakeGnb,
        two_gnb_ue: UeProcess,
    ):
        """An A3 neighbour-better event produces a MeasurementReport."""
        source_gnb.cell_dbm = -60
        target_gnb.cell_dbm = -90
        self._connect_to_source(source_gnb, target_gnb)

        source_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[{
                "id": 1,
                "event": "a3",
                "a3Offset": 6,
                "hysteresis": 2,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{
                "measId": 1,
                "measObjectId": 1,
                "reportConfigId": 1,
            }],
        )
        assert source_gnb.wait_for_rrc_reconfiguration_complete(timeout_s=10), (
            "UE did not acknowledge the A3 measurement configuration"
        )

        # A3 entry condition:
        # neighbour > serving + offset + hysteresis
        # -70 > -85 + 6 + 2 = -77
        event_start = time.monotonic()
        source_gnb.cell_dbm = -85
        target_gnb.cell_dbm = -70
        self._sustain_measurements(source_gnb, target_gnb)

        report = source_gnb.wait_for_measurement_report_since(
            start_ts=event_start,
            timeout_s=20,
        )
        if report is None:
            two_gnb_ue.collect_output(timeout_s=1)
            relevant_logs = "\n".join(
                line for line in two_gnb_ue.log_lines
                if "meas" in line.lower()
                or "cell" in line.lower()
                or "rrc" in line.lower()
            )
            raise AssertionError(
                "UE sent no MeasurementReport after the A3 condition occurred\n"
                f"Relevant UE logs:\n{relevant_logs}"
            )

        assert report.channel == RrcChannel.UL_DCCH
        assert source_gnb.get_ul_dcch_message_type(report.raw_pdu) == (
            "measurementReport"
        )
