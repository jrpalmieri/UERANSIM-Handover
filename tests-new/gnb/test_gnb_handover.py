from __future__ import annotations

import re
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
LEGACY_TESTS_ROOT = PROJECT_ROOT / "tests"

if str(LEGACY_TESTS_ROOT) not in sys.path:
    sys.path.insert(0, str(LEGACY_TESTS_ROOT))

from gnb_harness.marks import gnb_binary_exists, needs_pysctp
from gnb_harness import ngap_codec as ngap


@gnb_binary_exists
@needs_pysctp
class TestGnbHandover:
    def test_gnb_sends_handover_required_after_measurement_report(
        self,
        fake_amf,
        started_gnb_with_neighbor,
        rrc_connected_ue_with_neighbor,
    ):
        assert started_gnb_with_neighbor.wait_for_meas_config(timeout_s=10), "gNB did not send MeasConfig"
        time.sleep(1.0)

        rrc_connected_ue_with_neighbor.send_measurement_report(
            meas_id=1,
            serving_rsrp=20,
            serving_pci=0,
            neighbor_pci=2,
            neighbor_rsrp=60,
        )

        assert started_gnb_with_neighbor.wait_for_handover_decision(timeout_s=10), "gNB did not log handover decision"

        ho = fake_amf.wait_for_handover_required(timeout_s=10)
        assert ho is not None, "AMF did not receive HandoverRequired"
        assert ho.procedure_code == ngap.PROC_HANDOVER_PREPARATION

    def test_gnb_reports_handover_completion_after_reconfig_complete(
        self,
        fake_amf,
        started_gnb_with_neighbor,
        rrc_connected_ue_with_neighbor,
    ):
        assert started_gnb_with_neighbor.wait_for_meas_config(timeout_s=10), "gNB did not send MeasConfig"
        time.sleep(1.0)

        rrc_connected_ue_with_neighbor.send_measurement_report(
            meas_id=1,
            serving_rsrp=20,
            serving_pci=0,
            neighbor_pci=2,
            neighbor_rsrp=60,
        )

        assert started_gnb_with_neighbor.wait_for_handover_command(timeout_s=15), "gNB did not receive HandoverCommand"

        time.sleep(0.5)
        rrc_connected_ue_with_neighbor.send_rrc_reconfiguration_complete()

        notify = fake_amf.wait_for_handover_notify(timeout_s=10)
        if notify is not None:
            assert notify.procedure_code == ngap.PROC_HANDOVER_NOTIFICATION
            return

        ps = fake_amf.wait_for_path_switch_request(timeout_s=3)
        assert ps is not None, "AMF did not receive HandoverNotify or PathSwitchRequest"
        assert ps.procedure_code == ngap.PROC_PATH_SWITCH_REQUEST

    def test_target_handover_request_without_sessions_returns_failure_or_error(
        self,
        fake_amf,
        started_gnb,
    ):
        fake_amf.send_handover_request(amf_ue_ngap_id=9001)

        failure = fake_amf.wait_for_handover_failure(timeout_s=10)
        if failure is not None:
            assert failure.pdu is not None
            assert failure.pdu.is_unsuccessful
            assert failure.procedure_code == ngap.PROC_HANDOVER_PREPARATION
            return

        err = fake_amf.wait_for_error_indication(timeout_s=3)
        assert err is not None, "AMF did not receive HandoverFailure or ErrorIndication"

    def test_handover_selects_strongest_neighbor_from_multi_neighbor_report(
        self,
        fake_amf,
        started_gnb_with_two_neighbors,
        rrc_connected_ue_with_two_neighbors,
    ):
        assert started_gnb_with_two_neighbors.wait_for_meas_config(timeout_s=10), "gNB did not send MeasConfig"
        time.sleep(1.0)

        # Neighbor PCI 3 is intentionally stronger than PCI 2 and should be selected as target.
        rrc_connected_ue_with_two_neighbors.send_measurement_report_multi_neighbor(
            meas_id=1,
            serving_rsrp=20,
            serving_pci=0,
            neighbors=((2, 55), (3, 65)),
        )

        decision_line = started_gnb_with_two_neighbors.wait_for_handover_decision(timeout_s=10)
        assert decision_line is not None, "gNB did not log handover decision"
        assert re.search(r"targetPCI=3\b", decision_line), (
            f"Expected handover target PCI 3, but decision log was: {decision_line}"
        )

        required_line = started_gnb_with_two_neighbors.wait_for_handover_required(timeout_s=10)
        assert required_line is not None, "gNB did not log HandoverRequired dispatch"

        ho = fake_amf.wait_for_handover_required(timeout_s=10)
        assert ho is not None, "AMF did not receive HandoverRequired for strongest neighbor"
        assert ho.procedure_code == ngap.PROC_HANDOVER_PREPARATION

    def test_handover_selects_pci2_when_pci2_is_strongest_in_multi_neighbor_report(
        self,
        fake_amf,
        started_gnb_with_two_neighbors,
        rrc_connected_ue_with_two_neighbors,
    ):
        assert started_gnb_with_two_neighbors.wait_for_meas_config(timeout_s=10), "gNB did not send MeasConfig"
        time.sleep(1.0)

        rrc_connected_ue_with_two_neighbors.send_measurement_report_multi_neighbor(
            meas_id=1,
            serving_rsrp=20,
            serving_pci=0,
            neighbors=((2, 64), (3, 56)),
        )

        decision_line = started_gnb_with_two_neighbors.wait_for_handover_decision(timeout_s=10)
        assert decision_line is not None, "gNB did not log handover decision"
        assert re.search(r"targetPCI=2\b", decision_line), (
            f"Expected handover target PCI 2, but decision log was: {decision_line}"
        )

        required_line = started_gnb_with_two_neighbors.wait_for_handover_required(timeout_s=10)
        assert required_line is not None, "gNB did not log HandoverRequired dispatch"

        ho = fake_amf.wait_for_handover_required(timeout_s=10)
        assert ho is not None, "AMF did not receive HandoverRequired for strongest neighbor"
        assert ho.procedure_code == ngap.PROC_HANDOVER_PREPARATION
