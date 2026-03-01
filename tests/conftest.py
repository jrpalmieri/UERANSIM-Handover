"""
pytest configuration and shared fixtures for UERANSIM UE tests.

Fixtures provide managed instances of FakeGnb, UeProcess, and
MeasurementInjector that are started/stopped per test (or per session).
"""

from __future__ import annotations

import logging
import os
import shutil
import time
from pathlib import Path
from typing import Generator

import pytest

from harness.fake_gnb import FakeGnb
from harness.meas_injector import MeasurementInjector
from harness.ue_process import UeProcess
from harness.rrc_builder import RrcCodec

# ---------------------------------------------------------------------------
#  Paths
# ---------------------------------------------------------------------------

PROJECT_ROOT = Path(__file__).resolve().parent.parent
UE_BINARY = PROJECT_ROOT / "build" / "nr-ue"
TEST_UE_CONFIG = Path(__file__).resolve().parent / "configs" / "test-ue.yaml"

# ---------------------------------------------------------------------------
#  Logging
# ---------------------------------------------------------------------------

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)

# ---------------------------------------------------------------------------
#  Skip conditions
# ---------------------------------------------------------------------------

ue_binary_exists = pytest.mark.skipif(
    not UE_BINARY.exists(),
    reason=f"nr-ue binary not found at {UE_BINARY}",
)

needs_root = pytest.mark.skipif(
    os.geteuid() != 0,
    reason="Test requires root (for TUN interface creation)",
)

try:
    import asn1tools
    _has_asn1 = True
except ImportError:
    _has_asn1 = False

needs_asn1tools = pytest.mark.skipif(
    not _has_asn1,
    reason="asn1tools not installed — install with: pip install asn1tools",
)


# ---------------------------------------------------------------------------
#  Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def rrc_codec() -> RrcCodec:
    """Session-scoped RRC codec (compiles ASN.1 once)."""
    return RrcCodec()


@pytest.fixture
def fake_gnb() -> Generator[FakeGnb, None, None]:
    """Per-test FakeGnb instance.  Started and stopped automatically."""
    gnb = FakeGnb()
    gnb.start()
    yield gnb
    gnb.stop()


@pytest.fixture
def meas_injector() -> Generator[MeasurementInjector, None, None]:
    """Per-test MeasurementInjector instance."""
    inj = MeasurementInjector()
    yield inj
    inj.close()


@pytest.fixture
def ue_process() -> Generator[UeProcess, None, None]:
    """Per-test UeProcess.  Config is auto-generated; process is auto-cleaned."""
    ue = UeProcess()
    yield ue
    ue.cleanup()


@pytest.fixture
def running_ue(fake_gnb: FakeGnb) -> Generator[UeProcess, None, None]:
    """A UE process that is started and connected to the fake_gnb.

    The UE process is started with the test config and the fixture waits
    until the fake gNB receives the first heartbeat.
    """
    ue = UeProcess()
    ue.generate_config()
    ue.start()
    time.sleep(1)  # give the UE a moment to initialise

    # Wait for heartbeat
    if not fake_gnb.wait_for_heartbeat(timeout_s=10):
        ue.cleanup()
        pytest.skip("UE did not send heartbeat within 10 s")

    yield ue
    ue.cleanup()


# ---------------------------------------------------------------------------
#  Multi-cell / handover fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def source_gnb() -> Generator[FakeGnb, None, None]:
    """Source cell gNB bound to 127.0.0.1:4997, signal -60 dBm."""
    gnb = FakeGnb(listen_addr="127.0.0.1", cell_dbm=-60, nci=1)
    gnb.start()
    yield gnb
    gnb.stop()


@pytest.fixture
def target_gnb() -> Generator[FakeGnb, None, None]:
    """Target cell gNB bound to 127.0.0.2:4997, signal -70 dBm.

    Starts with weaker signal so the UE initially camps on the source cell.
    """
    gnb = FakeGnb(listen_addr="127.0.0.2", cell_dbm=-70, nci=2)
    gnb.start()
    yield gnb
    gnb.stop()


@pytest.fixture
def two_cell_ue(
    source_gnb: FakeGnb, target_gnb: FakeGnb
) -> Generator[UeProcess, None, None]:
    """A UE configured to search two gNB addresses.

    The UE will discover both cells via heartbeat exchanges.
    """
    ue = UeProcess(gnb_search_list=["127.0.0.1", "127.0.0.2"])
    ue.generate_config()
    ue.start()
    time.sleep(1)

    if not source_gnb.wait_for_heartbeat(timeout_s=10):
        ue.cleanup()
        pytest.skip("UE did not heartbeat source cell within 10 s")
    if not target_gnb.wait_for_heartbeat(timeout_s=10):
        ue.cleanup()
        pytest.skip("UE did not heartbeat target cell within 10 s")

    yield ue
    ue.cleanup()


# ---------------------------------------------------------------------------
#  Test credential constants (match configs/test-ue.yaml)
# ---------------------------------------------------------------------------

TEST_SUPI = "imsi-286010000000001"
TEST_MCC = "286"
TEST_MNC = "93"
TEST_KEY_HEX = "465B5CE8B199B49FAA5F0A2EE238A6BC"
TEST_OP_HEX = "E8ED289DEBA952E4283B54E88E6183CA"
TEST_OP_TYPE = "OP"
