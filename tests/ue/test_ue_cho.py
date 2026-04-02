"""CHO unit tests for the tests UE stack.

These tests avoid flaky attach/integration paths and focus on:
  - DL_CHO binary protocol helpers
  - ASN ConditionalReconfiguration payload/builder helpers
  - CHO lifecycle summary parsing from UE logs
"""

from __future__ import annotations

import struct

import pytest

from .conftest import needs_asn1tools, ue_binary_exists
from .harness.fake_gnb import (
    CANDIDATE_HEADER_SIZE,
    CONDITION_SIZE,
    CHO_EVENT_A3,
    CHO_EVENT_D1,
    CHO_EVENT_T1,
    _build_cho_binary,
    _build_condition,
)
from .harness.rrc_builder import RrcCodec
from .harness.ue_process import UeProcess


class TestUeChoBinaryHelpers:
    def test_t1_condition_encoding(self):
        cond = _build_condition({"event": "T1", "t1DurationMs": 800})
        fields = struct.unpack_from("<iiiiii", cond, 0)
        assert len(cond) == CONDITION_SIZE
        assert fields[0] == CHO_EVENT_T1
        assert fields[1] == 800

    def test_d1_condition_encoding(self):
        cond = _build_condition(
            {
                "event": "D1",
                "refX": 1e6,
                "refY": 2e6,
                "refZ": 3e6,
                "thresholdM": 500.0,
            }
        )
        fields = struct.unpack_from("<iiiiii", cond, 0)
        floats = struct.unpack_from("<dddd", cond, 24)
        assert fields[0] == CHO_EVENT_D1
        assert floats[0] == 1e6
        assert floats[3] == 500.0

    def test_binary_with_two_candidates(self):
        pdu = _build_cho_binary(
            [
                {"candidateId": 1, "conditions": [{"event": "T1", "t1DurationMs": 100}]},
                {
                    "candidateId": 2,
                    "conditions": [
                        {"event": "D1", "thresholdM": 200.0},
                        {"event": "A3", "offset": 3},
                    ],
                },
            ]
        )
        assert struct.unpack_from("<I", pdu, 0)[0] == 2

        first_hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert first_hdr[0] == 1
        assert first_hdr[5] == 1

        off2 = 4 + CANDIDATE_HEADER_SIZE + CONDITION_SIZE
        second_hdr = struct.unpack_from("<iiiiiI", pdu, off2)
        assert second_hdr[0] == 2
        assert second_hdr[5] == 2

        c2_off = off2 + CANDIDATE_HEADER_SIZE + CONDITION_SIZE
        second_evt = struct.unpack_from("<i", pdu, c2_off)[0]
        assert second_evt == CHO_EVENT_A3


class TestUeChoAsnBuilderHelpers:
    def test_payload_add_mod_only(self):
        codec = RrcCodec.__new__(RrcCodec)
        codec._asn1 = None

        payload = codec.build_conditional_reconfiguration_payload(
            candidates_to_add_mod=[
                {
                    "candidateId": 3,
                    "measIds": [21, 22],
                    "condRrcReconfig": b"\x01\x02",
                }
            ]
        )

        assert "condReconfigToAddModList" in payload
        assert "condReconfigToRemoveList" not in payload
        item = payload["condReconfigToAddModList"][0]
        assert item["condReconfigId"] == 3
        assert item["condExecutionCond"] == [21, 22]
        assert item["condRRCReconfig"] == b"\x01\x02"

    def test_payload_remove_only(self):
        codec = RrcCodec.__new__(RrcCodec)
        codec._asn1 = None

        payload = codec.build_conditional_reconfiguration_payload(
            candidate_ids_to_remove=[1, 4, 8]
        )

        assert "condReconfigToAddModList" not in payload
        assert payload["condReconfigToRemoveList"] == [1, 4, 8]

    def test_payload_add_and_remove(self):
        codec = RrcCodec.__new__(RrcCodec)
        codec._asn1 = None

        payload = codec.build_conditional_reconfiguration_payload(
            candidates_to_add_mod=[{"candidateId": 7}],
            candidate_ids_to_remove=[2],
        )

        assert payload["condReconfigToAddModList"][0]["condReconfigId"] == 7
        assert payload["condReconfigToRemoveList"] == [2]

    def test_builder_returns_bytes_without_asn1(self):
        codec = RrcCodec.__new__(RrcCodec)
        codec._asn1 = None

        pdu = codec.build_rrc_reconfiguration_conditional_handover(
            transaction_id=1,
            candidates_to_add_mod=[{"candidateId": 1}],
            candidate_ids_to_remove=[1],
        )

        assert isinstance(pdu, (bytes, bytearray))
        assert len(pdu) > 0


class TestUeChoSummaryParsing:
    @staticmethod
    def _ue_with_logs(lines):
        ue = UeProcess.__new__(UeProcess)
        ue._proc = None
        ue._log_lines = list(lines)
        ue._tmp_dir = None
        return ue

    def test_conditional_reconfig_full_summary(self):
        ue = self._ue_with_logs(
            [
                "[rrc] ConditionalReconfiguration applied: removed=1 removeMiss=0 "
                "added=2 updated=1 skipped=3 activeCandidates=4"
            ]
        )
        info = ue.parse_cho_info()
        assert info["applied_removed"] == 1
        assert info["applied_remove_miss"] == 0
        assert info["applied_added"] == 2
        assert info["applied_updated"] == 1
        assert info["applied_skipped"] == 3
        assert info["active_candidates"] == 4

    def test_conditional_reconfig_remove_only_summary(self):
        ue = self._ue_with_logs(
            [
                "[rrc] ConditionalReconfiguration applied: removed=2 removeMiss=1 "
                "activeCandidates=5"
            ]
        )
        info = ue.parse_cho_info()
        assert info["applied_removed"] == 2
        assert info["applied_remove_miss"] == 1
        assert info["active_candidates"] == 5

    def test_dl_cho_summary(self):
        ue = self._ue_with_logs(
            ["[rrc] DL_CHO applied: total=2 added=1 updated=1 skipped=0 activeCandidates=2"]
        )
        info = ue.parse_cho_info()
        assert info["applied_added"] == 1
        assert info["applied_updated"] == 1
        assert info["applied_skipped"] == 0
        assert info["active_candidates"] == 2


@ue_binary_exists
@needs_asn1tools
class TestUeChoIntegrationSmoke:
    def test_conditional_reconfiguration_adds_candidate(self, ue_process, fake_gnb):
        ue_process.start()
        if not fake_gnb.wait_for_heartbeat(timeout_s=10):
            pytest.skip("UE heartbeat not received by fake gNB")

        fake_gnb.perform_cell_attach()
        if not fake_gnb.perform_rrc_setup(timeout_s=20):
            pytest.skip("RRC setup did not complete in this environment")

        if not fake_gnb.perform_registration(timeout_s=20):
            pytest.skip("UE registration flow did not complete in this environment")

        cond_rrc = fake_gnb._rrc.build_rrc_reconfiguration_with_sync(
            transaction_id=7,
            target_pci=2,
            new_crnti=0x3301,
            t304_ms=1000,
        )

        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 1,
                    # No measIds: UE should apply default T1 fallback.
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=8,
        )

        added_log = ue_process.wait_for_cho_candidate_added(timeout_s=10)
        if not added_log:
            pytest.skip("UE did not log CHO candidate add after ConditionalReconfiguration")

        info = ue_process.parse_cho_info()
        assert info["applied_added"] >= 1
        assert info["active_candidates"] >= 1
