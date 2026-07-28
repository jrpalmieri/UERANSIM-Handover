#!/usr/bin/env python3
"""
NG Setup between a gNB and an independent AMF implementation.

The fake AMF in tests/gnb/harness parses NGAP with its own Python codec
(ngap_codec.py), so this is a cross-implementation check rather than a
round trip through our own encoder: the gNB builds NGSetupRequest with the
generated Release-18 descriptors, and something that does not share a line of
code with them has to be able to read it -- and its NGSetupResponse has to be
readable back.

Usage:  .venv/bin/python3 tests/ngap_ngsetup.py [-v]
Exit code 0 on success.
"""
from __future__ import annotations

import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from gnb.harness.fake_amf import FakeAmf  # noqa: E402
from gnb.harness import ngap_codec as ngap  # noqa: E402

GNB_BIN = ROOT / "build" / "nr-gnb"


def run(verbose: bool) -> int:
    if not GNB_BIN.exists():
        print(f"FAIL: {GNB_BIN} not built")
        return 2

    work = Path(tempfile.mkdtemp(prefix="ngsetup_"))
    cfg = yaml.safe_load(open(ROOT / "config" / "gnb1.yaml"))
    cfg.update({
        "nci": "0x000000011", "linkIp": "127.0.0.2",
        "ngapIp": "127.0.0.1", "gtpIp": "127.0.0.2",
        "amfConfigs": [{"address": "127.0.0.5", "port": 38412}],
    })
    cfg["xn"] = {"enabled": False}
    cfg_path = work / "gnb.yaml"
    yaml.dump(cfg, open(cfg_path, "w"), default_flow_style=False)

    amf = FakeAmf()
    proc = None
    log_path = work / "gnb.log"
    try:
        amf.start()
        log = open(log_path, "w")
        proc = subprocess.Popen([str(GNB_BIN), "-c", str(cfg_path)],
                                stdout=log, stderr=subprocess.STDOUT)
        ok = amf.wait_for_ng_setup(timeout_s=20.0)
        time.sleep(1.0)
        captured = amf.captured_messages   # property, not a call
    finally:
        if proc is not None:
            proc.send_signal(signal.SIGINT)
            time.sleep(1.0)
            if proc.poll() is None:
                proc.kill()
        try:
            log.close()
        except Exception:
            pass
        amf.stop()

    text = log_path.read_text()
    if verbose:
        for line in text.splitlines():
            if "ngap" in line or "sctp" in line:
                print("   ", line)
        for cm in captured:
            print("    AMF saw:", cm)

    failures = []
    if not ok:
        failures.append("AMF did not complete NG Setup")
    if not any(cm.procedure_code == ngap.PROC_NG_SETUP for cm in captured):
        failures.append("AMF never parsed an NGSetupRequest")
    # the gNB has to accept the response the AMF sent back
    if not re.search(r"NG Setup procedure with AMF \d+ is successful", text):
        failures.append("gNB did not report NG Setup success")
    for line in text.splitlines():
        if "[ngap]" in line and "[error]" in line:
            failures.append(line.strip())

    if failures:
        print("FAIL: NG Setup")
        for f in failures:
            print("   ", f)
        print(f"logs kept in {work}")
        return 1

    shutil.rmtree(work, ignore_errors=True)
    print("PASS: NG Setup — NGSetupRequest parsed by an independent AMF codec, "
          "NGSetupResponse accepted by the gNB")
    return 0


if __name__ == "__main__":
    sys.exit(run("-v" in sys.argv))
