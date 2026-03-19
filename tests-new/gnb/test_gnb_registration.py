from __future__ import annotations

import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
LEGACY_TESTS_ROOT = PROJECT_ROOT / "tests"

if str(LEGACY_TESTS_ROOT) not in sys.path:
    sys.path.insert(0, str(LEGACY_TESTS_ROOT))

from gnb_harness.marks import gnb_binary_exists, needs_pysctp
from gnb_harness import ngap_codec as ngap
from .harness.fake_ue import RrcChannel


@gnb_binary_exists
@needs_pysctp
class TestInitialUeRegistration:
    def test_gnb_replies_with_rrc_setup(self, started_gnb, fake_ue):
        assert fake_ue.wait_for_heartbeat_ack(timeout_s=10), "No heartbeat ack from gNB"

        fake_ue.send_rrc_setup_request()

        dl = fake_ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
        assert dl is not None, "Did not receive RRCSetup on DL-CCCH"
        assert len(dl.raw_pdu) > 0, "RRCSetup PDU is empty"

    def test_gnb_sends_initial_ue_message_after_setup_complete(self, fake_amf, started_gnb, fake_ue):
        assert fake_ue.wait_for_heartbeat_ack(timeout_s=10), "No heartbeat ack from gNB"

        fake_ue.send_rrc_setup_request()
        dl = fake_ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
        assert dl is not None, "Did not receive RRCSetup on DL-CCCH"

        fake_ue.send_rrc_setup_complete()
        time.sleep(1.0)

        ium = fake_amf.wait_for_initial_ue_message(timeout_s=10)
        assert ium is not None, "AMF did not receive InitialUEMessage"
        assert ium.procedure_code == ngap.PROC_INITIAL_UE_MESSAGE

    def test_gnb_creates_amf_ue_context_after_initial_registration(self, fake_amf, rrc_connected_ue):
        ium = fake_amf.wait_for_initial_ue_message(timeout_s=10)
        assert ium is not None, "AMF did not receive InitialUEMessage"

        ue_ctx = fake_amf.get_ue_context(ran_ue_id=1)
        assert ue_ctx is not None, "AMF did not create UE context for RAN-UE-NGAP-ID=1"
        assert ue_ctx["amf_ue_id"] > 0
