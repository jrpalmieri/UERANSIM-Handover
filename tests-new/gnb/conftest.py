from __future__ import annotations

import logging
import sys
import time
from pathlib import Path
from typing import Generator

import pytest
import yaml

PROJECT_ROOT = Path(__file__).resolve().parents[2]
LEGACY_TESTS_ROOT = PROJECT_ROOT / "tests"

if str(LEGACY_TESTS_ROOT) not in sys.path:
    sys.path.insert(0, str(LEGACY_TESTS_ROOT))

from gnb_harness.fake_amf import FakeAmf
from gnb_harness.gnb_process import GnbProcess

from .harness.fake_ue import FakeUe
from .harness.fake_ue import RrcChannel

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)


@pytest.fixture
def fake_amf() -> Generator[FakeAmf, None, None]:
    amf = FakeAmf()
    amf.start()
    yield amf
    amf.stop()


@pytest.fixture
def fake_ue() -> Generator[FakeUe, None, None]:
    ue = FakeUe()
    ue.start()
    yield ue
    ue.stop()


@pytest.fixture
def gnb_process() -> Generator[GnbProcess, None, None]:
    gnb = GnbProcess()
    yield gnb
    gnb.cleanup()


@pytest.fixture
def started_gnb(fake_amf: FakeAmf) -> Generator[GnbProcess, None, None]:
    gnb = GnbProcess()
    gnb.generate_config()
    gnb.start()

    if not gnb.wait_for_ng_setup(timeout_s=15):
        gnb.cleanup()
        pytest.skip("gNB did not complete NG Setup within 15 seconds")

    yield gnb
    gnb.cleanup()


@pytest.fixture
def started_gnb_with_neighbor(fake_amf: FakeAmf) -> Generator[GnbProcess, None, None]:
    gnb = GnbProcess()
    cfg_path = gnb.generate_config()

    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    cfg["neighborList"] = [
        {
            "nci": "0x000000002",
            "idLength": 32,
            "tac": 1,
            "ipAddress": "127.0.0.2",
            "handoverInterface": "N2",
        }
    ]

    with open(cfg_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(cfg, f, sort_keys=False)

    gnb.start(cfg_path)

    if not gnb.wait_for_ng_setup(timeout_s=15):
        gnb.cleanup()
        pytest.skip("gNB with neighbor config did not complete NG Setup within 15 seconds")

    yield gnb
    gnb.cleanup()


@pytest.fixture
def started_gnb_with_two_neighbors(fake_amf: FakeAmf) -> Generator[GnbProcess, None, None]:
    gnb = GnbProcess()
    cfg_path = gnb.generate_config()

    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    cfg["neighborList"] = [
        {
            "nci": "0x000000002",
            "idLength": 32,
            "tac": 1,
            "ipAddress": "127.0.0.2",
            "handoverInterface": "N2",
        },
        {
            "nci": "0x000000003",
            "idLength": 32,
            "tac": 1,
            "ipAddress": "127.0.0.3",
            "handoverInterface": "N2",
        },
    ]
    cfg["handover"] = {
        "eventType": ["A3"],
        "a2ThresholdDbm": -110,
        "a3OffsetDb": 3,
        "a5Threshold1Dbm": -110,
        "a5Threshold2Dbm": -95,
        "hysteresisDb": 1,
    }

    with open(cfg_path, "w", encoding="utf-8") as f:
        yaml.safe_dump(cfg, f, sort_keys=False)

    gnb.start(cfg_path)

    if not gnb.wait_for_ng_setup(timeout_s=15):
        gnb.cleanup()
        pytest.skip("gNB with two-neighbor config did not complete NG Setup within 15 seconds")

    yield gnb
    gnb.cleanup()


@pytest.fixture
def rrc_connected_ue(started_gnb: GnbProcess, fake_ue: FakeUe) -> Generator[FakeUe, None, None]:
    if not fake_ue.wait_for_heartbeat_ack(timeout_s=10):
        pytest.skip("Fake UE did not receive heartbeat ack within 10 seconds")

    fake_ue.send_rrc_setup_request()

    dl_setup = fake_ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
    if dl_setup is None:
        pytest.skip("Fake UE did not receive RRCSetup on DL-CCCH")

    fake_ue.send_rrc_setup_complete()
    time.sleep(1.0)

    yield fake_ue


@pytest.fixture
def rrc_connected_ue_with_neighbor(
    started_gnb_with_neighbor: GnbProcess,
    fake_ue: FakeUe,
) -> Generator[FakeUe, None, None]:
    if not fake_ue.wait_for_heartbeat_ack(timeout_s=10):
        pytest.skip("Fake UE did not receive heartbeat ack within 10 seconds")

    fake_ue.send_rrc_setup_request()

    dl_setup = fake_ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
    if dl_setup is None:
        pytest.skip("Fake UE did not receive RRCSetup on DL-CCCH")

    fake_ue.send_rrc_setup_complete()
    time.sleep(1.0)

    yield fake_ue


@pytest.fixture
def rrc_connected_ue_with_two_neighbors(
    started_gnb_with_two_neighbors: GnbProcess,
    fake_ue: FakeUe,
) -> Generator[FakeUe, None, None]:
    if not fake_ue.wait_for_heartbeat_ack(timeout_s=10):
        pytest.skip("Fake UE did not receive heartbeat ack within 10 seconds")

    fake_ue.send_rrc_setup_request()

    dl_setup = fake_ue.wait_for_dl_rrc(RrcChannel.DL_CCCH, timeout_s=5)
    if dl_setup is None:
        pytest.skip("Fake UE did not receive RRCSetup on DL-CCCH")

    fake_ue.send_rrc_setup_complete()
    time.sleep(1.0)

    yield fake_ue
