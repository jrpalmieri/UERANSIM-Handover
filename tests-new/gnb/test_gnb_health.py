from __future__ import annotations

import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
LEGACY_TESTS_ROOT = PROJECT_ROOT / "tests"

if str(LEGACY_TESTS_ROOT) not in sys.path:
    sys.path.insert(0, str(LEGACY_TESTS_ROOT))

from gnb_harness.marks import gnb_binary_exists, needs_pysctp
from gnb_harness import ngap_codec as ngap


@gnb_binary_exists
@needs_pysctp
class TestGnbHealth:
    def test_gnb_process_starts_and_runs(self, fake_amf, gnb_process):
        gnb_process.generate_config()
        gnb_process.start()

        assert gnb_process.wait_for_startup(timeout_s=10), "gNB did not report startup"
        assert gnb_process.is_running(), "gNB exited unexpectedly after startup"

    def test_gnb_sends_ng_setup_request(self, fake_amf, gnb_process):
        gnb_process.generate_config()
        gnb_process.start()

        msg = fake_amf.wait_for_message(ngap.PROC_NG_SETUP, timeout_s=15)
        assert msg is not None, "AMF did not receive NGSetupRequest"
        assert msg.procedure_code == ngap.PROC_NG_SETUP

    def test_gnb_completes_ng_setup(self, started_gnb):
        assert started_gnb.wait_for_ng_setup(timeout_s=3)
        assert started_gnb.has_log("NG Setup procedure is successful")
