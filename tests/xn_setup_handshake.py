#!/usr/bin/env python3
"""
XnSetup handshake between two gNB instances.

XnAP messages only ever travel between gNBs, so there is no golden source to
check them against and the pre-Release-18 implementation is not authoritative
either. This test therefore verifies the exchange behaviourally: bring up two
gNB processes against a fake AMF, point them at each other over Xn, and require
that

  * the initiator sends XnSetupRequest and the responder decodes it,
  * the responder sends XnSetupResponse and the initiator decodes it,
  * the values each side reports match what the *other* side was configured
    with -- NCI, TAC count, PLMN count and AMF region count.

The last point is what makes this more than a smoke test: the two gNBs are
separate processes, so a field only reads back correctly if it survived
encode -> APER -> SCTP -> decode intact.

Usage:  .venv/bin/python3 tests/xn_setup_handshake.py [-v]
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

GNB_BIN = ROOT / "build" / "nr-gnb"
SETUP_WAIT_S = 15.0

# gNB1 initiates, gNB2 accepts. NCIs are chosen so the low byte is easy to read
# in the logs: 0x11 -> 17, 0x22 -> 34.
NODES = {
    "gnb1": dict(nci="0x000000011", nci_int=17, ip="127.0.0.2"),
    "gnb2": dict(nci="0x000000022", nci_int=34, ip="127.0.0.3"),
}


def write_config(work: Path, name: str, me: dict, peer: dict) -> Path:
    cfg = yaml.safe_load(open(ROOT / "config" / "gnb1.yaml"))
    cfg["nci"] = me["nci"]
    cfg["linkIp"] = me["ip"]
    cfg["ngapIp"] = "127.0.0.1"
    cfg["gtpIp"] = me["ip"]  # distinct per gNB; 2152 would otherwise clash
    cfg["amfConfigs"] = [{"address": "127.0.0.5", "port": 38412}]
    cfg["xn"] = {"enabled": True, "xnIp": me["ip"], "xnPort": 38422}
    cfg["neighborList"] = [{
        "nci": peer["nci"], "idLength": 32, "mcc": cfg["mcc"], "mnc": cfg["mnc"],
        "tac": 1, "xnAddress": peer["ip"], "xnPort": 38422,
        "handoverInterface": "Xn",
    }]
    path = work / f"{name}.yaml"
    yaml.dump(cfg, open(path, "w"), default_flow_style=False)
    return path


def run(verbose: bool) -> int:
    if not GNB_BIN.exists():
        print(f"FAIL: {GNB_BIN} not built")
        return 2

    work = Path(tempfile.mkdtemp(prefix="xn_setup_"))
    procs = []
    amf = FakeAmf()
    try:
        amf.start()
        for name, me in NODES.items():
            peer = NODES["gnb2" if name == "gnb1" else "gnb1"]
            cfg = write_config(work, name, me, peer)
            log = open(work / f"{name}.log", "w")
            procs.append((name, subprocess.Popen([str(GNB_BIN), "-c", str(cfg)],
                                                 stdout=log, stderr=subprocess.STDOUT), log))
            time.sleep(2.0)
        # gNB1 retries its outbound association every 10 s, so allow for one retry
        time.sleep(SETUP_WAIT_S)
    finally:
        for _, p, _ in procs:
            p.send_signal(signal.SIGINT)
        time.sleep(1.5)
        for _, p, log in procs:
            if p.poll() is None:
                p.kill()
            log.close()
        amf.stop()

    logs = {name: (work / f"{name}.log").read_text() for name, _, _ in procs}
    if verbose:
        for name, text in logs.items():
            print(f"--- {name}")
            for line in text.splitlines():
                if "[xn]" in line or "[sctp-xn]" in line:
                    print("   ", line)

    failures = []

    def want(name, pattern, what):
        m = re.search(pattern, logs[name])
        if not m:
            failures.append(f"{name}: {what} not seen")
        return m

    want("gnb1", r"XnSetupRequest sent to gnbId=2", "XnSetupRequest sent")
    want("gnb2", r"XnSetupResponse sent to clientId", "XnSetupResponse sent")

    # each side must read the other's identity back off the wire
    m = want("gnb2", r"XnSetupRequest from gNB \d+ \(clientId=-?\d+\): "
                     r"nci=(\d+) pci=\d+ tacs=(\d+) plmns=(\d+) amfRegions=(\d+)",
             "XnSetupRequest decoded")
    if m:
        nci, tacs, plmns, regions = (int(g) for g in m.groups())
        if nci != NODES["gnb1"]["nci_int"]:
            failures.append(f"gnb2 decoded nci={nci}, expected {NODES['gnb1']['nci_int']}")
        for got, exp, what in ((tacs, 1, "tacs"), (plmns, 1, "plmns"), (regions, 1, "amfRegions")):
            if got != exp:
                failures.append(f"gnb2 decoded {what}={got}, expected {exp}")

    m = want("gnb1", r"XnSetupResponse from gnbId=\d+: "
                     r"nci=(\d+) pci=\d+ tacs=(\d+) plmns=(\d+) amfRegions=(\d+)",
             "XnSetupResponse decoded")
    if m:
        nci, tacs, plmns, regions = (int(g) for g in m.groups())
        if nci != NODES["gnb2"]["nci_int"]:
            failures.append(f"gnb1 decoded nci={nci}, expected {NODES['gnb2']['nci_int']}")
        for got, exp, what in ((tacs, 1, "tacs"), (plmns, 1, "plmns"), (regions, 1, "amfRegions")):
            if got != exp:
                failures.append(f"gnb1 decoded {what}={got}, expected {exp}")

    for name, text in logs.items():
        for line in text.splitlines():
            if "[xn]" in line and "[error]" in line:
                failures.append(f"{name}: {line.strip()}")

    if failures:
        print("FAIL: XnSetup handshake")
        for f in failures:
            print("   ", f)
        print(f"logs kept in {work}")
        return 1

    shutil.rmtree(work, ignore_errors=True)
    print("PASS: XnSetup handshake — request and response exchanged, "
          "identities decoded correctly in both directions")
    return 0


if __name__ == "__main__":
    sys.exit(run("-v" in sys.argv))
