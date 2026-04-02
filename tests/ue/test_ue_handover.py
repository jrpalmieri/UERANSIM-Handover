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


def _strongest_cell(latest: dict[int, int]) -> int:
    assert latest
    return max(latest.items(), key=lambda kv: kv[1])[0]


@ue_binary_exists
class TestUeSignalBasedHandover:
    def test_handover_from_source_to_target_based_on_signal_strength(self, two_gnb_ue, source_gnb, target_gnb):
        source_gnb.cell_dbm = -52
        target_gnb.cell_dbm = -88
        time.sleep(3)

        two_gnb_ue.collect_output(timeout_s=1)
        latest = _latest_cell_dbm(two_gnb_ue.log_lines)
        assert 1 in latest and 2 in latest
        assert _strongest_cell(latest) == 1

        source_gnb.cell_dbm = -95
        target_gnb.cell_dbm = -46
        time.sleep(3)

        two_gnb_ue.collect_output(timeout_s=1)
        latest = _latest_cell_dbm(two_gnb_ue.log_lines)
        assert 1 in latest and 2 in latest
        assert _strongest_cell(latest) == 2
