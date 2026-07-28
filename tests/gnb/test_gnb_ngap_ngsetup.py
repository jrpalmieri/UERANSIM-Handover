"""
NG Setup cross-implementation test.

The fake AMF in tests/harness parses NGAP with its own Python codec
(ngap_codec.py), so this is a cross-implementation check rather than a
round trip through our own encoder: the gNB builds NGSetupRequest with the
generated Release-18 descriptors, and something that does not share a line of
code with them has to be able to read it -- and its NGSetupResponse has to be
readable back.
"""
from __future__ import annotations

import re
import signal
import subprocess
import time
from pathlib import Path

import pytest
import yaml

from harness.fake_amf import FakeAmf
from harness import ngap_codec as ngap
from harness.marks import gnb_binary_exists, needs_pysctp

PROJECT_ROOT = Path(__file__).resolve().parents[2]
GNB_BIN = PROJECT_ROOT / "build" / "nr-gnb"


@gnb_binary_exists
@needs_pysctp
def test_ng_setup_cross_codec(tmp_path):
    """gNB NGSetupRequest must be parseable by an independent NGAP codec."""
    cfg = yaml.safe_load(open(PROJECT_ROOT / "config" / "gnb1.yaml"))
    cfg.update({
        "nci": "0x000000011", "linkIp": "127.0.0.2",
        "ngapIp": "127.0.0.1", "gtpIp": "127.0.0.2",
        "amfConfigs": [{"address": "127.0.0.5", "port": 38412}],
    })
    cfg["xn"] = {"enabled": False}
    cfg_path = tmp_path / "gnb.yaml"
    yaml.dump(cfg, open(cfg_path, "w"), default_flow_style=False)

    amf = FakeAmf()
    proc = None
    log_path = tmp_path / "gnb.log"
    try:
        amf.start()
        with open(log_path, "w") as log:
            proc = subprocess.Popen(
                [str(GNB_BIN), "-c", str(cfg_path)],
                stdout=log, stderr=subprocess.STDOUT,
            )
        ok = amf.wait_for_ng_setup(timeout_s=20.0)
        time.sleep(1.0)
        captured = amf.captured_messages
    finally:
        if proc is not None:
            proc.send_signal(signal.SIGINT)
            time.sleep(1.0)
            if proc.poll() is None:
                proc.kill()
        amf.stop()

    text = log_path.read_text()

    assert ok, "AMF did not complete NG Setup within timeout"
    assert any(
        cm.procedure_code == ngap.PROC_NG_SETUP for cm in captured
    ), "AMF never parsed an NGSetupRequest from the gNB"
    assert re.search(
        r"NG Setup procedure with AMF \d+ is successful", text
    ), "gNB did not report NG Setup success in its log"

    ngap_errors = [
        line.strip() for line in text.splitlines()
        if "[ngap]" in line and "[error]" in line
    ]
    assert not ngap_errors, f"gNB logged NGAP errors: {ngap_errors}"
