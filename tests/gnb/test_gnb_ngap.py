"""
Tests for gNB NGAP-layer behaviour.

Verifies that the gNB correctly performs NG Setup, sends InitialUEMessage
when a UE attaches, and generates HandoverRequired / PathSwitchRequest
messages via the NGAP interface.

All tests use FakeAmf (SCTP) + FakeUe (RLS) against a real ``nr-gnb``
binary.
"""

from __future__ import annotations

import time

import pytest

import sys
from pathlib import Path
_TESTS_DIR = Path(__file__).resolve().parent.parent
if str(_TESTS_DIR) not in sys.path:
    sys.path.insert(0, str(_TESTS_DIR))

from gnb_harness.marks import gnb_binary_exists, needs_pysctp
from gnb_harness.fake_amf import FakeAmf
from gnb_harness.fake_ue import FakeUe
from gnb_harness.gnb_process import GnbProcess
from gnb_harness import ngap_codec as ngap


# =====================================================================
#  NG Setup
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestNgSetup:
    """Verify that the gNB sends NGSetupRequest on startup and handles
    the NGSetupResponse from the AMF."""

    def test_ng_setup_request_sent(self, fake_amf: FakeAmf, gnb_process: GnbProcess):
        """gNB should send an NGSetupRequest immediately after SCTP
        association is established."""
        gnb_process.generate_config()
        gnb_process.start()

        msg = fake_amf.wait_for_message(ngap.PROC_NG_SETUP, timeout_s=15)
        assert msg is not None, "gNB did not send NGSetupRequest"
        assert msg.procedure_code == ngap.PROC_NG_SETUP

    def test_ng_setup_successful(self, started_gnb: GnbProcess):
        """After receiving NGSetupResponse, gNB should log success."""
        assert started_gnb.has_log("NG Setup procedure is successful")


# =====================================================================
#  Initial UE Message
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestInitialUeMessage:
    """Verify that the gNB sends InitialUEMessage to the AMF when a UE
    completes RRC Setup."""

    def test_initial_ue_message_sent(
        self,
        fake_amf: FakeAmf,
        started_gnb: GnbProcess,
        fake_ue: FakeUe,
    ):
        """After RRC Setup Complete, gNB should forward the NAS PDU to
        the AMF as an InitialUEMessage."""
        # Wait for heartbeat
        assert fake_ue.wait_for_heartbeat_ack(timeout_s=10), "No heartbeat ack"

        # RRC connection establishment
        fake_ue.send_rrc_setup_request()
        time.sleep(0.5)

        from harness.rls_protocol import RrcChannel
        dl = fake_ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
        assert dl is not None, "Did not receive RRCSetup (DL-CCCH)"

        # Send RRC Setup Complete with NAS (default includes NSSAI for AMF selection)
        fake_ue.send_rrc_setup_complete()
        time.sleep(1.0)

        # Verify AMF received InitialUEMessage
        ium = fake_amf.wait_for_initial_ue_message(timeout_s=10)
        assert ium is not None, "AMF did not receive InitialUEMessage"
        assert ium.procedure_code == ngap.PROC_INITIAL_UE_MESSAGE


# =====================================================================
#  HandoverRequired generation
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestHandoverRequired:
    """Verify the gNB generates HandoverRequired after receiving a
    measurement report that triggers a handover decision."""

    def test_handover_required_after_meas_report(
        self,
        fake_amf: FakeAmf,
        connected_ue: FakeUe,
        started_gnb: GnbProcess,
    ):
        """After receiving a MeasurementReport with a stronger neighbour,
        the gNB should send HandoverRequired to the AMF."""
        # Wait for MeasConfig to be sent
        assert started_gnb.wait_for_meas_config(timeout_s=10), \
            "gNB did not send MeasConfig"

        # Allow some time for MeasConfig to reach the UE
        time.sleep(1.0)

        # Send a MeasurementReport showing a neighbour much stronger than serving
        connected_ue.send_measurement_report(
            meas_id=1,
            serving_rsrp=20,   # weak serving
            serving_pci=0,
            neighbor_pci=1,
            neighbor_rsrp=60,  # strong neighbour
        )

        # The gNB should log a handover decision and send HandoverRequired
        assert started_gnb.wait_for_handover_decision(timeout_s=10), \
            "gNB did not make a handover decision"

        ho = fake_amf.wait_for_handover_required(timeout_s=10)
        assert ho is not None, "AMF did not receive HandoverRequired"


# =====================================================================
#  PathSwitchRequest
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestPathSwitch:
    """Verify that the gNB sends PathSwitchRequest after handover
    completion."""

    def test_path_switch_after_handover(
        self,
        fake_amf: FakeAmf,
        connected_ue: FakeUe,
        started_gnb: GnbProcess,
    ):
        """Full handover flow ending with PathSwitchRequest."""
        # Wait for the full handover flow to start
        assert started_gnb.wait_for_meas_config(timeout_s=10)
        time.sleep(1.0)

        # Trigger handover via measurement report
        connected_ue.send_measurement_report(
            meas_id=1,
            serving_rsrp=20,
            serving_pci=0,
            neighbor_pci=1,
            neighbor_rsrp=60,
        )

        # Wait for HandoverCommand to be forwarded to UE
        assert started_gnb.wait_for_handover_command(timeout_s=15), \
            "gNB did not receive HandoverCommand from AMF"

        # UE sends RRCReconfigurationComplete
        time.sleep(0.5)
        connected_ue.send_rrc_reconfiguration_complete()

        # Check that PathSwitchRequest is sent
        ps = fake_amf.wait_for_path_switch_request(timeout_s=10)
        assert ps is not None, "AMF did not receive PathSwitchRequest"
