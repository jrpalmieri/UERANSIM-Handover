from __future__ import annotations

import time
from pathlib import Path
from typing import Generator

import pytest

from .harness.fake_gnb import FakeGnb
from .harness.ue_process import UeProcess

PROJECT_ROOT = Path(__file__).resolve().parents[2]
UE_BINARY = PROJECT_ROOT / "build" / "nr-ue"

ue_binary_exists = pytest.mark.skipif(
    not UE_BINARY.exists(),
    reason=f"nr-ue binary not found at {UE_BINARY}",
)

try:
    import asn1tools  # noqa: F401

    _HAS_ASN1 = True
except ImportError:
    _HAS_ASN1 = False

needs_asn1tools = pytest.mark.skipif(
    not _HAS_ASN1,
    reason="asn1tools not installed; install with: pip install asn1tools",
)


@pytest.fixture
def fake_gnb() -> Generator[FakeGnb, None, None]:
    gnb = FakeGnb(listen_addr="127.0.0.1", cell_dbm=-60, nci=1)
    gnb.start()
    yield gnb
    gnb.stop()


@pytest.fixture
def ue_process() -> Generator[UeProcess, None, None]:
    ue = UeProcess(gnb_search_list=["127.0.0.1"])
    ue.generate_config(enable_handover_sim=True)
    yield ue
    ue.cleanup()


@pytest.fixture
def connected_ue(fake_gnb: FakeGnb, ue_process: UeProcess) -> Generator[tuple[UeProcess, FakeGnb], None, None]:
    ue_process.start()
    assert fake_gnb.wait_for_heartbeat(timeout_s=10)
    fake_gnb.perform_cell_attach()
    assert fake_gnb.perform_rrc_setup()
    fake_gnb.wait_for_ul_dcch(timeout_s=10)
    time.sleep(1)
    yield ue_process, fake_gnb


@pytest.fixture
def source_gnb() -> Generator[FakeGnb, None, None]:
    gnb = FakeGnb(listen_addr="127.0.0.1", cell_dbm=-60, nci=1)
    gnb.start()
    yield gnb
    gnb.stop()


@pytest.fixture
def target_gnb() -> Generator[FakeGnb, None, None]:
    gnb = FakeGnb(listen_addr="127.0.0.2", cell_dbm=-75, nci=2)
    gnb.start()
    yield gnb
    gnb.stop()


@pytest.fixture
def two_gnb_ue(source_gnb: FakeGnb, target_gnb: FakeGnb) -> Generator[UeProcess, None, None]:
    ue = UeProcess(gnb_search_list=["127.0.0.1", "127.0.0.2"])
    ue.generate_config(enable_handover_sim=True)
    ue.start()

    assert source_gnb.wait_for_heartbeat(timeout_s=10)
    assert target_gnb.wait_for_heartbeat(timeout_s=10)

    yield ue
    ue.cleanup()
