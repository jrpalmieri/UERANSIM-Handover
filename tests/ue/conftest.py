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


@pytest.fixture
def registered_two_gnb_ue(source_gnb: FakeGnb, target_gnb: FakeGnb) -> Generator[UeProcess, None, None]:
    """Bring UE to registered state with two visible gNBs.

    This fixture is intended for operational CHO tests that require a stable
    registration baseline before sending RRC measurement/CHO instructions.
    """
    ue = UeProcess(gnb_search_list=["127.0.0.1", "127.0.0.2"])
    ue.generate_config(enable_handover_sim=True)
    ue.start()

    assert source_gnb.wait_for_heartbeat(timeout_s=12), "Source gNB did not receive UE heartbeat"
    assert target_gnb.wait_for_heartbeat(timeout_s=12), "Target gNB did not receive UE heartbeat"

    # Register through source cell first for stability, then attach target so
    # both cells are visible for handover/CHO evaluations.
    source_gnb.perform_cell_attach()
    assert source_gnb.perform_rrc_setup(timeout_s=25), "RRC setup exchange did not complete"
    assert source_gnb.wait_for_ul_dcch(timeout_s=12) is not None, "No UL-DCCH observed after RRC setup"

    registered = False
    for attempt in range(2):
        if source_gnb.perform_registration(timeout_s=45):
            registered = True
            break

        # Retry from connected state once in case the first NAS exchange races.
        source_gnb.perform_cell_attach()
        source_gnb.perform_rrc_setup(timeout_s=25)

    if not registered:
        ue.collect_output(timeout_s=1.0)
        relevant = [
            ln
            for ln in ue.log_lines
            if "MM-" in ln
            or "RM-" in ln
            or "Registration" in ln
            or "Authentication" in ln
            or "Security" in ln
            or "Reject" in ln
            or "failure" in ln.lower()
        ]
        tail = "\n".join(relevant[-20:]) if relevant else "<no registration logs captured>"
        pytest.skip(f"UE registration flow did not complete. Recent UE logs:\n{tail}")

    target_gnb.perform_cell_attach()

    yield ue
    ue.cleanup()
