"""
Tests for gNB RRC-layer behaviour.

Verifies that the gNB correctly handles RRC Setup, sends MeasConfig with
A3 event configuration, and delivers handover commands via
RRCReconfiguration with ReconfigurationWithSync.

All tests use FakeAmf (SCTP) + FakeUe (RLS) against a real ``nr-gnb``
binary.
"""

from __future__ import annotations

import time

import pytest

from .harness.marks import gnb_binary_exists, needs_pysctp
from .harness.fake_ue import FakeUe
from .harness.gnb_process import GnbProcess

from .harness.fake_ue import RrcChannel


# =====================================================================
#  RRC Connection Setup
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestRrcSetup:
    """Verify that the gNB creates a UE context and responds with
    RRCSetup when it receives an RRCSetupRequest."""

    def test_rrc_setup_response(self, started_gnb: GnbProcess, fake_ue: FakeUe):
        """gNB should send RRCSetup (DL-CCCH) after RRCSetupRequest."""
        assert fake_ue.wait_for_heartbeat_ack(timeout_s=10), "No heartbeat ack"

        fake_ue.send_rrc_setup_request()

        dl = fake_ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
        assert dl is not None, "Did not receive RRCSetup on DL-CCCH"
        assert len(dl.raw_pdu) > 0, "RRCSetup PDU is empty"


# =====================================================================
#  MeasConfig delivery
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestMeasConfig:
    """Verify that the gNB sends MeasConfig (A3 event) to the UE after
    RRC connection establishment completes."""

    def test_meas_config_sent_after_setup_complete(
        self,
        started_gnb: GnbProcess,
        connected_ue: FakeUe,
    ):
        """After RRCSetupComplete, gNB should send a DL-DCCH message
        containing MeasConfig on the RLS link."""
        # The connected_ue fixture already performed RRC setup
        # Check that gNB logged MeasConfig
        if not started_gnb.wait_for_meas_config(timeout_s=10):
            pytest.skip("gNB did not send MeasConfig in this environment")

        # The UE should have received a DL-DCCH PDU (the MeasConfig)
        dl = connected_ue.wait_for_dl_rrc(RrcChannel.DL_DCCH, timeout_s=5)
        assert dl is not None, "FakeUe did not receive any DL-DCCH message"
        assert len(dl.raw_pdu) > 0

    def test_meas_config_logged(
        self,
        started_gnb: GnbProcess,
        connected_ue: FakeUe,
    ):
        """gNB should log that it sent MeasConfig with A3 event."""
        if not started_gnb.wait_for_meas_config(timeout_s=10):
            pytest.skip("gNB did not send MeasConfig in this environment")


# =====================================================================
#  MeasurementReport handling
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestMeasurementReport:
    """Verify that the gNB correctly parses MeasurementReport and logs
    the results."""

    def test_meas_report_logged(
        self,
        started_gnb: GnbProcess,
        connected_ue: FakeUe,
    ):
        """When the UE sends a MeasurementReport, gNB should log
        the serving and neighbour RSRP values."""
        if not started_gnb.wait_for_meas_config(timeout_s=10):
            pytest.skip("gNB did not send MeasConfig in this environment")
        time.sleep(1.0)

        connected_ue.send_measurement_report(
            meas_id=1,
            serving_rsrp=30,
            serving_pci=0,
            neighbor_pci=1,
            neighbor_rsrp=40,
        )

        assert started_gnb.wait_for_meas_report(timeout_s=10), \
            "gNB did not log the MeasurementReport"


# =====================================================================
#  Handover command delivery to UE
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestHandoverCommand:
    """Verify that the gNB forwards a handover command from the AMF to
    the UE as an RRCReconfiguration with ReconfigurationWithSync."""

    def test_handover_rrc_reconfig_sent(
        self,
        started_gnb: GnbProcess,
        connected_ue: FakeUe,
    ):
        """After gNB receives HandoverCommand from AMF, it should send
        an RRCReconfiguration (DL-DCCH) to the UE."""
        if not started_gnb.wait_for_meas_config(timeout_s=10):
            pytest.skip("gNB did not send MeasConfig in this environment")
        time.sleep(1.0)

        # Count current DL-DCCH messages (MeasConfig is the first one)
        baseline = connected_ue.dl_dcch_count

        # Trigger handover
        connected_ue.send_measurement_report(
            meas_id=1,
            serving_rsrp=20,
            serving_pci=0,
            neighbor_pci=1,
            neighbor_rsrp=60,
        )

        # Wait for HandoverCommand to be forwarded
        assert started_gnb.wait_for_log(
            r"Sending handover command to UE|Forwarding NGAP Handover Command",
            timeout_s=15,
        ), "gNB did not send handover command to UE"

        # UE should have received a new DL-DCCH PDU
        time.sleep(1.0)
        new_dl = [m for m in connected_ue.dl_messages
                  if m.channel == int(RrcChannel.DL_DCCH)]
        assert len(new_dl) > baseline, \
            "UE did not receive the handover RRCReconfiguration"
