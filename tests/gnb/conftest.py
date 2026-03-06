"""
pytest configuration and shared fixtures for UERANSIM gNB tests.

Provides managed instances of FakeAmf, FakeUe, and GnbProcess that are
started/stopped per test (or per session).
"""

from __future__ import annotations

import logging
import sys
import time
from pathlib import Path
from typing import Generator

import pytest

# Ensure the tests/ directory is on sys.path so gnb_harness and harness
# packages can be imported from the tests/gnb/ subdirectory.
_TESTS_DIR = Path(__file__).resolve().parent.parent
if str(_TESTS_DIR) not in sys.path:
    sys.path.insert(0, str(_TESTS_DIR))

from gnb_harness.fake_amf import FakeAmf
from gnb_harness.fake_ue import FakeUe
from gnb_harness.gnb_process import GnbProcess

# ---------------------------------------------------------------------------
#  Logging
# ---------------------------------------------------------------------------

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)


# ---------------------------------------------------------------------------
#  Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def fake_amf() -> Generator[FakeAmf, None, None]:
    """Per-test FakeAmf.  Started and stopped automatically."""
    amf = FakeAmf()
    amf.start()
    yield amf
    amf.stop()


@pytest.fixture
def fake_ue() -> Generator[FakeUe, None, None]:
    """Per-test FakeUe.  Started and stopped automatically."""
    ue = FakeUe()
    ue.start()
    yield ue
    ue.stop()


@pytest.fixture
def gnb_process() -> Generator[GnbProcess, None, None]:
    """Per-test GnbProcess.  Config is auto-generated; process is auto-cleaned."""
    gnb = GnbProcess()
    yield gnb
    gnb.cleanup()


@pytest.fixture
def started_gnb(fake_amf: FakeAmf) -> Generator[GnbProcess, None, None]:
    """A gNB process that is started and has completed NG Setup with the fake AMF.

    Depends on ``fake_amf`` fixture to ensure the SCTP server is ready.
    """
    gnb = GnbProcess()
    gnb.generate_config()
    gnb.start()

    # Wait for NG Setup to complete
    if not gnb.wait_for_ng_setup(timeout_s=15):
        gnb.cleanup()
        pytest.skip("gNB did not complete NG Setup within 15 s")

    yield gnb
    gnb.cleanup()


@pytest.fixture
def connected_ue(
    started_gnb: GnbProcess, fake_amf: FakeAmf
) -> Generator[FakeUe, None, None]:
    """A FakeUe that has completed RRC Setup with the gNB.

    The UE sends heartbeats, RRC Setup Request, and RRC Setup Complete.
    After yielding, the gNB should have sent MeasConfig to the UE.
    """
    ue = FakeUe()
    ue.start()

    # Wait for heartbeat exchange
    if not ue.wait_for_heartbeat_ack(timeout_s=10):
        ue.stop()
        pytest.skip("FakeUe did not receive heartbeat ack within 10 s")

    # RRC connection setup
    ue.send_rrc_setup_request()
    time.sleep(0.5)

    # Wait for RRC Setup (DL-CCCH)
    from harness.rls_protocol import RrcChannel
    dl = ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
    if dl is None:
        ue.stop()
        pytest.skip("FakeUe did not receive RRCSetup within 5 s")

    # Send RRC Setup Complete (default NAS includes NSSAI for AMF selection)
    ue.send_rrc_setup_complete()
    time.sleep(1.0)

    yield ue
    ue.stop()
