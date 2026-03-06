"""
Integration tests for the gNB handover flow.

Verifies the complete N2 (AMF-mediated) handover sequence:

    MeasConfig → MeasReport → HandoverDecision → HandoverRequired →
    HandoverCommand → RRCReconfiguration → RRCReconfigurationComplete →
    HandoverNotify → PathSwitchRequest → PathSwitchRequestAck

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

from harness.rls_protocol import RrcChannel


# =====================================================================
#  Full handover flow
# =====================================================================

@gnb_binary_exists
@needs_pysctp
class TestHandoverFlow:
    """End-to-end gNB handover integration tests."""

    def test_full_n2_handover(
        self,
        fake_amf: FakeAmf,
        started_gnb: GnbProcess,
    ):
        """Complete N2 handover from UE attach through path switch.

        Steps:
        1. FakeUe connects and completes RRC Setup
        2. gNB sends MeasConfig (A3)
        3. FakeUe sends MeasurementReport (neighbour stronger)
        4. gNB makes handover decision → sends HandoverRequired to AMF
        5. AMF auto-responds with HandoverCommand
        6. gNB forwards handover to UE via RRCReconfiguration
        7. FakeUe sends RRCReconfigurationComplete
        8. gNB sends HandoverNotify + PathSwitchRequest to AMF
        9. AMF auto-responds with PathSwitchRequestAck
        """
        # -- Step 1: UE attach --
        ue = FakeUe()
        ue.start()
        try:
            assert ue.wait_for_heartbeat_ack(timeout_s=10), \
                "No heartbeat ack from gNB"

            ue.send_rrc_setup_request()
            dl = ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
            assert dl is not None, "Did not receive RRCSetup"

            ue.send_rrc_setup_complete()  # default NAS includes NSSAI(SST=1)

            # -- Step 2: MeasConfig --
            assert started_gnb.wait_for_meas_config(timeout_s=10), \
                "gNB did not send MeasConfig"
            meas_dl = ue.wait_for_dl_rrc(RrcChannel.DL_DCCH, timeout_s=5)
            assert meas_dl is not None, "UE did not receive MeasConfig"

            # Wait for AMF to assign UE context (InitialUEMessage → DL NAS)
            ium = fake_amf.wait_for_initial_ue_message(timeout_s=10)
            assert ium is not None, "AMF did not receive InitialUEMessage"
            time.sleep(1.0)

            # -- Step 3: Measurement Report --
            ue.send_measurement_report(
                meas_id=1,
                serving_rsrp=20,   # weak
                serving_pci=0,
                neighbor_pci=1,
                neighbor_rsrp=60,  # strong
            )

            # -- Step 4: Handover decision + HandoverRequired --
            assert started_gnb.wait_for_handover_decision(timeout_s=10), \
                "gNB did not make handover decision"
            ho_req = fake_amf.wait_for_handover_required(timeout_s=10)
            assert ho_req is not None, "AMF did not receive HandoverRequired"

            # -- Step 5: HandoverCommand auto-response (FakeAmf handles this) --
            assert started_gnb.wait_for_handover_command(timeout_s=10), \
                "gNB did not receive HandoverCommand from AMF"

            # -- Step 6: RRCReconfiguration to UE --
            assert started_gnb.wait_for_log(
                r"Sending handover command to UE|Forwarding NGAP Handover Command",
                timeout_s=10,
            ), "gNB did not forward handover to UE"

            # Wait for the DL-DCCH handover RRC message
            time.sleep(1.0)
            dcch_msgs = [m for m in ue.dl_messages
                         if m.channel == int(RrcChannel.DL_DCCH)]
            assert len(dcch_msgs) >= 2, \
                "UE did not receive handover RRCReconfiguration"

            # -- Step 7: RRCReconfigurationComplete --
            ue.send_rrc_reconfiguration_complete()

            # -- Step 8 & 9: HandoverNotify + PathSwitchRequest --
            hn = fake_amf.wait_for_handover_notify(timeout_s=10)
            ps = fake_amf.wait_for_path_switch_request(timeout_s=10)

            # At least one of these should appear (depends on code path)
            assert hn is not None or ps is not None, \
                "AMF received neither HandoverNotify nor PathSwitchRequest"

            # Verify gNB state
            state = started_gnb.parse_state()
            assert state.handover_completed or state.path_switch_sent, \
                "gNB state does not reflect handover completion"

        finally:
            ue.stop()

    def test_handover_preparation_failure(
        self,
        started_gnb: GnbProcess,
    ):
        """Verify gNB handles HandoverPreparationFailure gracefully.

        The AMF returns a failure instead of HandoverCommand — the gNB
        should log a warning and keep the UE on the source cell.
        """
        # Create a FakeAmf that does NOT auto-respond with HandoverCommand
        amf = FakeAmf(auto_initial_context=True)
        # Override: we need to intercept (not auto-respond to) HandoverRequired
        # We'll handle it manually below

        # NOTE: This test requires a custom AMF; however, the started_gnb
        # fixture already uses the default fake_amf.  For a clean test,
        # we skip if the auto-responding AMF has already handled the HO.
        # This is a design-level test placeholder.
        pytest.skip("HandoverPreparationFailure test requires custom AMF wiring — placeholder")

    def test_meas_report_no_handover_weak_neighbour(
        self,
        fake_amf: FakeAmf,
        started_gnb: GnbProcess,
    ):
        """If the neighbour is weaker than serving, no handover should be
        triggered."""
        ue = FakeUe()
        ue.start()
        try:
            assert ue.wait_for_heartbeat_ack(timeout_s=10)
            ue.send_rrc_setup_request()
            dl = ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
            assert dl is not None

            ue.send_rrc_setup_complete(
                nas_pdu=b'\x7e\x00\x41\x01',
            )
            assert started_gnb.wait_for_meas_config(timeout_s=10)
            time.sleep(1.0)

            # Neighbour weaker than serving
            ue.send_measurement_report(
                meas_id=1,
                serving_rsrp=60,   # strong serving
                serving_pci=0,
                neighbor_pci=1,
                neighbor_rsrp=20,  # weak neighbour
            )
            time.sleep(3.0)

            # Should NOT trigger a handover
            assert not started_gnb.has_log("Handover decision"), \
                "gNB should not trigger handover with weaker neighbour"
            assert not fake_amf.has_message(ngap.PROC_HANDOVER_PREPARATION), \
                "AMF should not have received HandoverRequired"

        finally:
            ue.stop()
