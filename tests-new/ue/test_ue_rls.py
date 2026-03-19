from __future__ import annotations

import re
import time

from .conftest import ue_binary_exists


def _latest_cell_dbm(log_lines: list[str]) -> dict[int, int]:
    latest: dict[int, int] = {}
    pattern = re.compile(r"cellId=(\d+) dbm=(-?\d+)")
    for line in log_lines:
        m = pattern.search(line)
        if m:
            latest[int(m.group(1))] = int(m.group(2))
    return latest


@ue_binary_exists
class TestUeRlsAndSignals:
    def test_ue_connects_to_fake_gnb(self, ue_process, fake_gnb):
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        ue_process.collect_output(timeout_s=1)
        latest = _latest_cell_dbm(ue_process.log_lines)
        assert 1 in latest

    def test_ue_connects_to_two_gnbs_at_rls_layer(self, two_gnb_ue, source_gnb, target_gnb):
        assert two_gnb_ue.is_running()
        assert source_gnb.wait_for_heartbeat(timeout_s=2)
        assert target_gnb.wait_for_heartbeat(timeout_s=2)

        time.sleep(2)
        two_gnb_ue.collect_output(timeout_s=1)
        latest = _latest_cell_dbm(two_gnb_ue.log_lines)
        assert 1 in latest
        assert 2 in latest

    def test_ue_stores_signal_strength_from_heartbeat_ack(self, ue_process, fake_gnb):
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.cell_dbm = -64
        assert ue_process.wait_for_log(r"cellId=1 dbm=-64", timeout_s=8) is not None

        fake_gnb.cell_dbm = -91
        assert ue_process.wait_for_log(r"cellId=1 dbm=-91", timeout_s=8) is not None
