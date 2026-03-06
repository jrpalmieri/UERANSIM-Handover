"""
Tests for Conditional Handover (CHO) functionality — Release 16/17.

Verifies that the UE correctly handles Conditional Handover (CHO)
instructions from the gNB, including:

  Unit tests (no binary required):
    - V2 binary CHO protocol encoding/decoding (condition groups)
    - Legacy conditionType mapping to v2 conditions
    - Arbitrary condition group encoding (AND/OR)
    - Execution priority encoding
    - CHO candidate log parsing
    - RRC builder helpers for CHO

  Integration tests (require nr-ue binary):
    - CHO candidate configuration via DL_CHO channel
    - T1 timer expiry triggers handover (single T1 condition)
    - Candidate cancellation on handover execution
    - Multiple candidates with different T1 durations (OR logic)
    - CHO does not execute before T1 expiry
"""

from __future__ import annotations

import re
import struct
import time

import pytest

from harness.rrc_builder import RrcCodec
from harness.fake_gnb import (
    FakeGnb, CapturedMessage, _build_cho_binary, _build_condition,
    CONDITION_SIZE, CANDIDATE_HEADER_SIZE,
    CHO_EVENT_T1, CHO_EVENT_A2, CHO_EVENT_A3, CHO_EVENT_A5, CHO_EVENT_D1,
    CHO_EVENT_D1_SIB19,
)
from harness.rls_protocol import RrcChannel
from harness.ue_process import UeProcess, UeState
from conftest import ue_binary_exists, needs_asn1tools


# ======================================================================
#  Unit tests — V2 CHO binary protocol (condition groups)
# ======================================================================

class TestChoBinaryProtocolV2:
    """Verify the v2 binary CHO configuration encoding with condition groups."""

    def test_single_condition_encoding_size(self):
        """A single condition should be exactly 56 bytes."""
        cond = _build_condition({"event": "T1", "t1DurationMs": 500})
        assert len(cond) == CONDITION_SIZE

    def test_t1_condition_encoding(self):
        """T1 condition should encode eventType=0 and t1DurationMs in intParam1."""
        cond_bytes = _build_condition({"event": "T1", "t1DurationMs": 2000})
        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[0] == CHO_EVENT_T1  # eventType
        assert fields[1] == 2000          # intParam1 = t1DurationMs

    def test_a2_condition_encoding(self):
        """A2 condition should encode threshold and hysteresis."""
        cond_bytes = _build_condition(
            {"event": "A2", "threshold": -100, "hysteresis": 3}
        )
        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[0] == CHO_EVENT_A2  # eventType
        assert fields[1] == -100          # threshold
        assert fields[2] == 3             # hysteresis

    def test_a3_condition_encoding(self):
        """A3 condition should encode offset and hysteresis."""
        cond_bytes = _build_condition(
            {"event": "A3", "offset": 5, "hysteresis": 1, "timeToTriggerMs": 640}
        )
        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[0] == CHO_EVENT_A3
        assert fields[1] == 5      # offset
        assert fields[2] == 1      # hysteresis
        assert fields[4] == 640    # timeToTriggerMs

    def test_a5_condition_encoding(self):
        """A5 condition should encode threshold1, threshold2, hysteresis."""
        cond_bytes = _build_condition(
            {"event": "A5", "threshold1": -110, "threshold2": -90, "hysteresis": 2}
        )
        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[0] == CHO_EVENT_A5
        assert fields[1] == -110   # threshold1
        assert fields[2] == -90    # threshold2
        assert fields[3] == 2      # hysteresis

    def test_d1_condition_encoding(self):
        """D1 condition should encode ECEF ref point and threshold in floats."""
        cond_bytes = _build_condition(
            {"event": "D1", "refX": 4e6, "refY": 5e5, "refZ": 4.9e6, "thresholdM": 2000.0}
        )
        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[0] == CHO_EVENT_D1

        fp = struct.unpack_from("<dddd", cond_bytes, 24)
        assert fp[0] == pytest.approx(4e6)
        assert fp[1] == pytest.approx(5e5)
        assert fp[2] == pytest.approx(4.9e6)
        assert fp[3] == pytest.approx(2000.0)

    def test_single_candidate_t1_only_legacy(self):
        """Legacy T1_ONLY candidate maps to 1 T1 condition."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "targetPci": 5, "newCRNTI": 100,
             "t304Ms": 500, "t1DurationMs": 2000, "conditionType": "T1_ONLY"}
        ])
        # Header: 4 bytes, candidate header: 24 bytes, 1 condition: 56 bytes
        assert len(pdu) == 4 + CANDIDATE_HEADER_SIZE + 1 * CONDITION_SIZE

        num = struct.unpack_from("<I", pdu, 0)[0]
        assert num == 1

        # Candidate header
        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[0] == 1     # candidateId
        assert hdr[1] == 5     # targetPci
        assert hdr[2] == 100   # newCRNTI
        assert hdr[3] == 500   # t304Ms
        assert hdr[5] == 1     # numConditions

        # T1 condition
        coff = 4 + CANDIDATE_HEADER_SIZE
        cond = struct.unpack_from("<iiiiii", pdu, coff)
        assert cond[0] == CHO_EVENT_T1
        assert cond[1] == 2000  # t1DurationMs

    def test_single_candidate_t1_and_a3_legacy(self):
        """Legacy T1_AND_A3 maps to 2 conditions: T1 + A3."""
        pdu = _build_cho_binary([
            {"candidateId": 2, "targetPci": 10, "newCRNTI": 200,
             "t304Ms": 1000, "t1DurationMs": 500,
             "conditionType": "T1_AND_A3",
             "a3Offset": 3, "a3Hysteresis": 1, "a3TimeToTriggerMs": 640}
        ])
        assert len(pdu) == 4 + CANDIDATE_HEADER_SIZE + 2 * CONDITION_SIZE

        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[5] == 2  # numConditions

        # First condition: T1
        coff1 = 4 + CANDIDATE_HEADER_SIZE
        c1 = struct.unpack_from("<iiiiii", pdu, coff1)
        assert c1[0] == CHO_EVENT_T1
        assert c1[1] == 500  # t1DurationMs

        # Second condition: A3
        coff2 = coff1 + CONDITION_SIZE
        c2 = struct.unpack_from("<iiiiii", pdu, coff2)
        assert c2[0] == CHO_EVENT_A3
        assert c2[1] == 3    # offset
        assert c2[2] == 1    # hysteresis
        assert c2[4] == 640  # timeToTriggerMs

    def test_d1_only_legacy(self):
        """Legacy D1_ONLY maps to 1 D1 condition."""
        pdu = _build_cho_binary([
            {"candidateId": 3, "targetPci": 7, "conditionType": "D1_ONLY",
             "d1RefX": 4e6, "d1RefY": 5e5, "d1RefZ": 4.9e6, "d1ThresholdM": 2000.0}
        ])
        assert len(pdu) == 4 + CANDIDATE_HEADER_SIZE + 1 * CONDITION_SIZE

        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[5] == 1  # numConditions

        coff = 4 + CANDIDATE_HEADER_SIZE
        c = struct.unpack_from("<iiiiii", pdu, coff)
        assert c[0] == CHO_EVENT_D1

        fp = struct.unpack_from("<dddd", pdu, coff + 24)
        assert fp[0] == pytest.approx(4e6)
        assert fp[3] == pytest.approx(2000.0)

    def test_explicit_condition_group(self):
        """New-style candidate with explicit condition group."""
        pdu = _build_cho_binary([
            {"candidateId": 10, "targetPci": 42,
             "executionPriority": 3,
             "conditions": [
                 {"event": "D1", "refX": 1e6, "refY": 2e6, "refZ": 3e6, "thresholdM": 500.0},
                 {"event": "A3", "offset": 4, "hysteresis": 1, "timeToTriggerMs": 100},
             ]}
        ])
        assert len(pdu) == 4 + CANDIDATE_HEADER_SIZE + 2 * CONDITION_SIZE

        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[0] == 10   # candidateId
        assert hdr[1] == 42   # targetPci
        assert hdr[4] == 3    # executionPriority
        assert hdr[5] == 2    # numConditions

        # D1 condition
        coff1 = 4 + CANDIDATE_HEADER_SIZE
        c1 = struct.unpack_from("<iiiiii", pdu, coff1)
        assert c1[0] == CHO_EVENT_D1
        fp = struct.unpack_from("<dddd", pdu, coff1 + 24)
        assert fp[0] == pytest.approx(1e6)

        # A3 condition
        coff2 = coff1 + CONDITION_SIZE
        c2 = struct.unpack_from("<iiiiii", pdu, coff2)
        assert c2[0] == CHO_EVENT_A3
        assert c2[1] == 4     # offset
        assert c2[4] == 100   # timeToTriggerMs

    def test_multiple_candidates_variable_conditions(self):
        """Multiple candidates with different numbers of conditions."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "conditions": [
                {"event": "T1", "t1DurationMs": 100},
            ]},
            {"candidateId": 2, "conditions": [
                {"event": "T1", "t1DurationMs": 200},
                {"event": "A2", "threshold": -105},
            ]},
            {"candidateId": 3, "conditions": [
                {"event": "D1", "thresholdM": 300.0},
                {"event": "A3", "offset": 2},
                {"event": "A5", "threshold1": -110, "threshold2": -90},
            ]},
        ])
        # 4 header + cand1(24+56) + cand2(24+2*56) + cand3(24+3*56)
        expected = 4 + (24 + 56) + (24 + 112) + (24 + 168)
        assert len(pdu) == expected

        num = struct.unpack_from("<I", pdu, 0)[0]
        assert num == 3

    def test_empty_candidates(self):
        """Zero candidates should produce 4 bytes with count=0."""
        pdu = _build_cho_binary([])
        assert len(pdu) == 4
        assert struct.unpack_from("<I", pdu, 0)[0] == 0

    def test_default_values_legacy(self):
        """Missing keys should use defaults via legacy mapping."""
        pdu = _build_cho_binary([{"candidateId": 1}])
        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[1] == 2      # default targetPci
        assert hdr[2] == 0x1234 # default newCRNTI
        assert hdr[3] == 1000   # default t304Ms
        assert hdr[5] == 1      # 1 condition (T1 default)

    def test_execution_priority_default(self):
        """executionPriority should default to -1 (unset) when not specified."""
        pdu = _build_cho_binary([{"candidateId": 1}])
        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        prio = struct.unpack_from("<i", pdu, 4 + 16)[0]
        assert prio == -1  # default unset

    def test_execution_priority_explicit(self):
        """executionPriority should be encoded when specified."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "executionPriority": 5, "conditions": [
                {"event": "T1", "t1DurationMs": 100}
            ]}
        ])
        prio = struct.unpack_from("<i", pdu, 4 + 16)[0]
        assert prio == 5


# ======================================================================
#  Unit tests — condition group AND/OR logic validation
# ======================================================================

class TestConditionGroupLogic:
    """Verify condition group patterns used in CHO configurations."""

    def test_and_group_d1_and_a3(self):
        """D1 AND A3 condition group for a single candidate."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "targetPci": 101,
             "conditions": [
                 {"event": "D1", "refX": 1.0, "refY": 2.0, "refZ": 3.0, "thresholdM": 50000.0},
                 {"event": "A3", "offset": 3, "hysteresis": 1, "timeToTriggerMs": 100},
             ]}
        ])
        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[0] == 1    # candidateId
        assert hdr[5] == 2    # 2 conditions (AND)
        coff = 4 + CANDIDATE_HEADER_SIZE
        c1_evt = struct.unpack_from("<i", pdu, coff)[0]
        c2_evt = struct.unpack_from("<i", pdu, coff + CONDITION_SIZE)[0]
        assert c1_evt == CHO_EVENT_D1
        assert c2_evt == CHO_EVENT_A3

    def test_or_candidates_d1_vs_a3(self):
        """Two candidates (OR): one D1-only, one A3-only."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "targetPci": 101,
             "conditions": [{"event": "D1", "thresholdM": 50000.0}]},
            {"candidateId": 2, "targetPci": 101,
             "conditions": [{"event": "A3", "offset": 3}]},
        ])
        num = struct.unpack_from("<I", pdu, 0)[0]
        assert num == 2

    def test_and_group_t1_and_a2(self):
        """T1 AND A2 condition group: timer + serving cell degradation."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "conditions": [
                {"event": "T1", "t1DurationMs": 500},
                {"event": "A2", "threshold": -105, "hysteresis": 2},
            ]}
        ])
        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[5] == 2  # 2 conditions

    def test_triple_and_group(self):
        """Three conditions AND: D1 AND A3 AND A2."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "conditions": [
                {"event": "D1", "thresholdM": 50000.0},
                {"event": "A3", "offset": 3},
                {"event": "A2", "threshold": -105},
            ]}
        ])
        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[5] == 3

    def test_priority_ordering_among_or_candidates(self):
        """Multiple OR candidates with different priorities."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "targetPci": 101, "executionPriority": 5,
             "conditions": [{"event": "T1", "t1DurationMs": 100}]},
            {"candidateId": 2, "targetPci": 102, "executionPriority": 1,
             "conditions": [{"event": "T1", "t1DurationMs": 100}]},
        ])
        # Candidate 1 priority
        p1 = struct.unpack_from("<i", pdu, 4 + 16)[0]
        assert p1 == 5
        # Candidate 2 priority
        off2 = 4 + CANDIDATE_HEADER_SIZE + CONDITION_SIZE
        p2 = struct.unpack_from("<i", pdu, off2 + 16)[0]
        assert p2 == 1


# ======================================================================
#  Unit tests — CHO log parsing
# ======================================================================

class TestChoLogParsing:
    """Verify UeProcess.parse_cho_info() against log patterns."""

    def _ue_with_logs(self, lines: list) -> UeProcess:
        """Create a UeProcess with pre-populated log lines."""
        ue = UeProcess.__new__(UeProcess)
        ue._proc = None
        ue._log_lines = list(lines)
        ue._tmp_dir = None
        return ue

    def test_candidate_added_via_asn1(self):
        ue = self._ue_with_logs([
            "[rrc] CHO candidate 1 added: targetPCI=5 newC-RNTI=100 t304=500ms "
            "conditions=(T1) priority=2147483647",
        ])
        info = ue.parse_cho_info()
        assert info["candidates_configured"] == 1

    def test_candidate_added_via_dl_cho(self):
        ue = self._ue_with_logs([
            "[rrc] DL_CHO candidate 1: targetPCI=5 conditions=(T1) priority=-1",
        ])
        info = ue.parse_cho_info()
        assert info["candidates_configured"] == 1

    def test_t1_expired(self):
        ue = self._ue_with_logs([
            "[rrc] CHO candidate 1: T1 expired (elapsed=2001ms)",
        ])
        info = ue.parse_cho_info()
        assert 1 in info["t1_expired_ids"]

    def test_candidate_executed(self):
        ue = self._ue_with_logs([
            "[rrc] Executing CHO candidate 2: targetPCI=5 newC-RNTI=100 t304=500ms",
        ])
        info = ue.parse_cho_info()
        assert info["executed_id"] == 2

    def test_candidates_cancelled(self):
        ue = self._ue_with_logs([
            "[rrc] Cancelling 3 CHO candidate(s)",
        ])
        info = ue.parse_cho_info()
        assert info["cancelled"] is True

    def test_full_cho_sequence(self):
        ue = self._ue_with_logs([
            "[rrc] CHO candidate 1 added: targetPCI=5 conditions=(T1) priority=2147483647",
            "[rrc] CHO candidate 2 added: targetPCI=10 conditions=(T1) priority=2147483647",
            "[rrc] CHO candidate 1: T1 started (500ms)",
            "[rrc] CHO candidate 2: T1 started (1000ms)",
            "[rrc] CHO candidate 1: T1 expired (elapsed=501ms)",
            "[rrc] CHO candidate 1: all conditions met (T1) – eligible for execution",
            "[rrc] Executing CHO candidate 1: targetPCI=5 newC-RNTI=100 t304=500ms",
        ])
        info = ue.parse_cho_info()
        assert info["candidates_configured"] == 2
        assert 1 in info["t1_expired_ids"]
        assert info["executed_id"] == 1

    def test_no_cho_logs(self):
        ue = self._ue_with_logs([
            "[rrc] RRC-CONNECTED",
            "[nas] RM-REGISTERED",
        ])
        info = ue.parse_cho_info()
        assert info["candidates_configured"] == 0
        assert info["executed_id"] is None
        assert info["cancelled"] is False

    def test_multiple_t1_expired(self):
        ue = self._ue_with_logs([
            "[rrc] CHO candidate 1: T1 expired (elapsed=500ms)",
            "[rrc] CHO candidate 2: T1 expired (elapsed=1000ms)",
        ])
        info = ue.parse_cho_info()
        assert sorted(info["t1_expired_ids"]) == [1, 2]


# ======================================================================
#  Unit tests — D1 log parsing
# ======================================================================

class TestD1LogParsing:
    """Verify D1-related log messages are parsed correctly."""

    def _ue_with_logs(self, lines: list) -> UeProcess:
        ue = UeProcess.__new__(UeProcess)
        ue._proc = None
        ue._log_lines = list(lines)
        ue._tmp_dir = None
        return ue

    def test_d1_candidate_added(self):
        ue = self._ue_with_logs([
            "[rrc] DL_CHO candidate 1: targetPCI=7 conditions=(D1) priority=2147483647",
        ])
        info = ue.parse_cho_info()
        assert info["candidates_configured"] == 1

    def test_d1_condition_met_logged(self):
        ue = self._ue_with_logs([
            "[rrc] DL_CHO candidate 1: targetPCI=7 conditions=(D1) priority=2147483647",
            "[rrc] CHO candidate 1: all conditions met (D1) – eligible for execution",
            "[rrc] Executing CHO candidate 1: targetPCI=7 newC-RNTI=300 t304=500ms",
        ])
        info = ue.parse_cho_info()
        assert info["executed_id"] == 1


# ======================================================================
#  Integration tests — require nr-ue binary and root
# ======================================================================

def _connect_ue(fake_gnb: FakeGnb, ue_process: UeProcess):
    """Start the UE and bring it to RRC_CONNECTED via the fake gNB."""
    ue_process.generate_config()
    ue_process.start()
    assert fake_gnb.wait_for_heartbeat(timeout_s=10), "No heartbeat from UE"

    fake_gnb.perform_cell_attach()
    assert fake_gnb.perform_rrc_setup(), "RRC setup failed"

    fake_gnb.wait_for_ul_dcch(timeout_s=10)
    time.sleep(1.0)


@ue_binary_exists
class TestChoIntegrationSingleCell:
    """Integration tests for CHO with a single fake gNB."""

    def test_cho_candidate_configured_via_dl_cho(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """UE should log CHO candidate configuration when receiving DL_CHO."""
        _connect_ue(fake_gnb, ue_process)

        fake_gnb.send_cho_configuration([
            {"candidateId": 1, "targetPci": 2, "newCRNTI": 100,
             "t304Ms": 1000, "t1DurationMs": 5000, "conditionType": "T1_ONLY"}
        ])

        log = ue_process.wait_for_cho_candidate_added(timeout_s=5)
        assert log is not None, "UE did not log CHO candidate configuration"

    def test_cho_t1_only_triggers_handover(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """T1 condition CHO should trigger handover when T1 expires."""
        _connect_ue(fake_gnb, ue_process)

        fake_gnb.send_cho_configuration([
            {"candidateId": 1, "targetPci": 99, "newCRNTI": 100,
             "t304Ms": 1000, "t1DurationMs": 500, "conditionType": "T1_ONLY"}
        ])

        log = ue_process.wait_for_cho_execution(timeout_s=15)
        assert log is not None, "CHO candidate was not executed after T1 expiry"

    def test_cho_does_not_execute_before_t1(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """CHO should NOT trigger before T1 expires."""
        _connect_ue(fake_gnb, ue_process)

        fake_gnb.send_cho_configuration([
            {"candidateId": 1, "targetPci": 99, "newCRNTI": 100,
             "t304Ms": 1000, "t1DurationMs": 30000,
             "conditionType": "T1_ONLY"}
        ])

        time.sleep(3)
        log = ue_process.wait_for_cho_execution(timeout_s=2)
        assert log is None, "CHO candidate executed before T1 expired"

    def test_cho_multiple_candidates(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """Multiple CHO candidates (OR): shortest T1 should execute first."""
        _connect_ue(fake_gnb, ue_process)

        fake_gnb.send_cho_configuration([
            {"candidateId": 1, "targetPci": 10, "newCRNTI": 100,
             "t1DurationMs": 500, "conditionType": "T1_ONLY"},
            {"candidateId": 2, "targetPci": 20, "newCRNTI": 200,
             "t1DurationMs": 10000, "conditionType": "T1_ONLY"},
        ])

        log = ue_process.wait_for_cho_execution(timeout_s=15)
        assert log is not None, "No CHO candidate executed"

        cho_info = ue_process.parse_cho_info()
        assert cho_info["executed_id"] == 1

    def test_cho_t1_expired_logged(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """UE should log T1 expiry for CHO candidates."""
        _connect_ue(fake_gnb, ue_process)

        fake_gnb.send_cho_configuration([
            {"candidateId": 1, "targetPci": 99, "newCRNTI": 100,
             "t304Ms": 1000, "t1DurationMs": 500, "conditionType": "T1_ONLY"}
        ])

        log = ue_process.wait_for_cho_t1_expired(timeout_s=15)
        assert log is not None, "T1 expiry was not logged"

        cho_info = ue_process.parse_cho_info()
        assert 1 in cho_info["t1_expired_ids"]

    def test_cho_condition_group_via_dl_cho(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """UE should accept a candidate with explicit condition group."""
        _connect_ue(fake_gnb, ue_process)

        fake_gnb.send_cho_configuration([
            {"candidateId": 1, "targetPci": 99,
             "executionPriority": 2,
             "conditions": [
                 {"event": "T1", "t1DurationMs": 500},
             ]}
        ])

        log = ue_process.wait_for_cho_candidate_added(timeout_s=5)
        assert log is not None, "UE did not log condition-group CHO candidate"

        # The T1 should expire and trigger execution
        log = ue_process.wait_for_cho_execution(timeout_s=15)
        assert log is not None, "Condition-group CHO was not executed"

    def test_cho_priority_selection(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """When two T1 candidates trigger simultaneously, higher priority wins."""
        _connect_ue(fake_gnb, ue_process)

        # Both have same T1 duration, but candidate 2 has higher priority (lower value)
        fake_gnb.send_cho_configuration([
            {"candidateId": 1, "targetPci": 10,
             "executionPriority": 5,
             "conditions": [{"event": "T1", "t1DurationMs": 500}]},
            {"candidateId": 2, "targetPci": 20,
             "executionPriority": 1,
             "conditions": [{"event": "T1", "t1DurationMs": 500}]},
        ])

        log = ue_process.wait_for_cho_execution(timeout_s=15)
        assert log is not None, "No CHO candidate executed"

        cho_info = ue_process.parse_cho_info()
        assert cho_info["executed_id"] == 2, \
            "Higher-priority candidate (lower value) should have been selected"


# ======================================================================
#  Unit tests — D1_SIB19 binary protocol encoding
# ======================================================================

class TestD1Sib19BinaryProtocol:
    """Verify D1_SIB19 event type encoding in the v2 binary CHO protocol."""

    def test_d1_sib19_condition_encoding(self):
        """D1_SIB19 condition should encode flags, threshold, and elevMin."""
        cond_bytes = _build_condition(
            {"event": "D1_SIB19", "useNadir": True,
             "thresholdM": 500000.0, "elevationMinDeg": 10.0}
        )
        assert len(cond_bytes) == CONDITION_SIZE

        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[0] == CHO_EVENT_D1_SIB19  # eventType = 5
        assert fields[1] == 1  # flags: bit0 = useNadir

        fp = struct.unpack_from("<dddd", cond_bytes, 24)
        assert fp[0] == pytest.approx(500000.0)  # thresholdM
        assert fp[1] == pytest.approx(10.0)       # elevationMinDeg

    def test_d1_sib19_no_nadir(self):
        """D1_SIB19 with useNadir=False should have flags bit0 = 0."""
        cond_bytes = _build_condition(
            {"event": "D1_SIB19", "useNadir": False, "thresholdM": 1000.0}
        )
        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[0] == CHO_EVENT_D1_SIB19
        assert fields[1] == 0  # no nadir flag

    def test_d1_sib19_use_sib19_threshold(self):
        """D1_SIB19 with negative thresholdM should signal 'use SIB19 distanceThresh'."""
        cond_bytes = _build_condition(
            {"event": "D1_SIB19", "thresholdM": -1.0}
        )
        fp = struct.unpack_from("<dddd", cond_bytes, 24)
        assert fp[0] < 0  # negative = use SIB19's distanceThresh

    def test_d1_sib19_default_values(self):
        """D1_SIB19 with defaults: useNadir=True, thresholdM=-1.0, elevMin=-1.0."""
        cond_bytes = _build_condition({"event": "D1_SIB19"})
        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[0] == CHO_EVENT_D1_SIB19
        assert fields[1] == 1  # useNadir default is True

        fp = struct.unpack_from("<dddd", cond_bytes, 24)
        assert fp[0] == pytest.approx(-1.0)  # default thresholdM
        assert fp[1] == pytest.approx(-1.0)  # default elevMinDeg

    def test_d1_sib19_only_legacy_mapping(self):
        """Legacy D1_SIB19_ONLY conditionType maps to 1 D1_SIB19 condition."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "targetPci": 7,
             "conditionType": "D1_SIB19_ONLY",
             "d1sib19UseNadir": True,
             "d1sib19ThresholdM": 300000.0,
             "d1sib19ElevationMinDeg": 5.0}
        ])
        assert len(pdu) == 4 + CANDIDATE_HEADER_SIZE + 1 * CONDITION_SIZE

        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[5] == 1  # numConditions

        coff = 4 + CANDIDATE_HEADER_SIZE
        c = struct.unpack_from("<iiiiii", pdu, coff)
        assert c[0] == CHO_EVENT_D1_SIB19

        fp = struct.unpack_from("<dddd", pdu, coff + 24)
        assert fp[0] == pytest.approx(300000.0)
        assert fp[1] == pytest.approx(5.0)

    def test_d1_sib19_with_time_to_trigger(self):
        """D1_SIB19 condition should support timeToTriggerMs."""
        cond_bytes = _build_condition(
            {"event": "D1_SIB19", "thresholdM": 100000.0,
             "timeToTriggerMs": 320}
        )
        fields = struct.unpack_from("<iiiiii", cond_bytes, 0)
        assert fields[4] == 320  # timeToTriggerMs

    def test_d1_sib19_and_a3_condition_group(self):
        """D1_SIB19 AND A3 condition group for a single candidate."""
        pdu = _build_cho_binary([
            {"candidateId": 1, "targetPci": 101,
             "conditions": [
                 {"event": "D1_SIB19", "useNadir": True, "thresholdM": 200000.0},
                 {"event": "A3", "offset": 3, "hysteresis": 1},
             ]}
        ])
        hdr = struct.unpack_from("<iiiiiI", pdu, 4)
        assert hdr[5] == 2  # 2 conditions (AND)

        coff = 4 + CANDIDATE_HEADER_SIZE
        c1_evt = struct.unpack_from("<i", pdu, coff)[0]
        c2_evt = struct.unpack_from("<i", pdu, coff + CONDITION_SIZE)[0]
        assert c1_evt == CHO_EVENT_D1_SIB19
        assert c2_evt == CHO_EVENT_A3


# ======================================================================
#  Unit tests — D1_SIB19 log parsing
# ======================================================================

class TestD1Sib19LogParsing:
    """Verify D1_SIB19-related log messages are parsed correctly."""

    def _ue_with_logs(self, lines: list) -> UeProcess:
        ue = UeProcess.__new__(UeProcess)
        ue._proc = None
        ue._log_lines = list(lines)
        ue._tmp_dir = None
        return ue

    def test_d1_sib19_candidate_added(self):
        ue = self._ue_with_logs([
            "[rrc] DL_CHO candidate 1: targetPCI=7 conditions=(D1_SIB19) priority=2147483647",
        ])
        info = ue.parse_cho_info()
        assert info["candidates_configured"] == 1

    def test_d1_sib19_condition_met_logged(self):
        ue = self._ue_with_logs([
            "[rrc] DL_CHO candidate 1: targetPCI=7 conditions=(D1_SIB19) priority=2147483647",
            "[rrc] CHO candidate 1: all conditions met (D1_SIB19) – eligible for execution",
            "[rrc] Executing CHO candidate 1: targetPCI=7 newC-RNTI=300 t304=500ms",
        ])
        info = ue.parse_cho_info()
        assert info["executed_id"] == 1

    def test_d1_sib19_and_t1_condition_group(self):
        ue = self._ue_with_logs([
            "[rrc] DL_CHO candidate 1: targetPCI=7 conditions=(D1_SIB19 AND T1) priority=2147483647",
            "[rrc] CHO candidate 1: all conditions met (D1_SIB19 AND T1) – eligible for execution",
            "[rrc] Executing CHO candidate 1: targetPCI=7 newC-RNTI=300 t304=500ms",
        ])
        info = ue.parse_cho_info()
        assert info["candidates_configured"] == 1
        assert info["executed_id"] == 1


# ======================================================================
#  Unit tests — SIB19 + CHO geometry calculations
# ======================================================================

class TestSib19ChoGeometry:
    """Verify the orbital geometry calculations used for D1_SIB19 evaluation.

    These are pure-Python equivalents of the C++ computeNadir, ecefDistance,
    and elevationAngle functions, used to validate that the binary protocol
    parameters will produce correct trigger behaviour.
    """

    @staticmethod
    def _geo_to_ecef(lat_deg, lon_deg, alt_m=0.0):
        """WGS-84 geodetic to ECEF conversion."""
        import math
        A = 6378137.0
        E2 = 0.00669437999014
        lat = math.radians(lat_deg)
        lon = math.radians(lon_deg)
        sin_lat = math.sin(lat)
        cos_lat = math.cos(lat)
        N = A / math.sqrt(1.0 - E2 * sin_lat * sin_lat)
        x = (N + alt_m) * cos_lat * math.cos(lon)
        y = (N + alt_m) * cos_lat * math.sin(lon)
        z = (N * (1 - E2) + alt_m) * sin_lat
        return x, y, z

    @staticmethod
    def _ecef_distance(a, b):
        import math
        return math.sqrt(sum((ai - bi) ** 2 for ai, bi in zip(a, b)))

    @staticmethod
    def _compute_nadir(sat_x, sat_y, sat_z):
        """Reproduce the C++ computeNadir logic in Python."""
        import math
        A = 6378137.0
        E2 = 0.00669437999014
        lon = math.atan2(sat_y, sat_x)
        p = math.sqrt(sat_x ** 2 + sat_y ** 2)
        lat = math.atan2(sat_z, p * (1.0 - E2))
        for _ in range(5):
            sin_lat = math.sin(lat)
            N = A / math.sqrt(1.0 - E2 * sin_lat * sin_lat)
            lat = math.atan2(sat_z + E2 * N * sin_lat, p)
        # Convert back with h=0
        sin_lat = math.sin(lat)
        cos_lat = math.cos(lat)
        N = A / math.sqrt(1.0 - E2 * sin_lat * sin_lat)
        nx = N * cos_lat * math.cos(lon)
        ny = N * cos_lat * math.sin(lon)
        nz = N * (1 - E2) * sin_lat
        return nx, ny, nz

    @staticmethod
    def _elevation_angle(ue_geo, ue_ecef, sat_ecef):
        """Reproduce the C++ elevationAngle logic in Python."""
        import math
        dx = sat_ecef[0] - ue_ecef[0]
        dy = sat_ecef[1] - ue_ecef[1]
        dz = sat_ecef[2] - ue_ecef[2]
        rng = math.sqrt(dx * dx + dy * dy + dz * dz)
        if rng < 1e-9:
            return 90.0
        lat = math.radians(ue_geo[0])
        lon = math.radians(ue_geo[1])
        upX = math.cos(lat) * math.cos(lon)
        upY = math.cos(lat) * math.sin(lon)
        upZ = math.sin(lat)
        sin_elev = (dx * upX + dy * upY + dz * upZ) / rng
        sin_elev = max(-1.0, min(1.0, sin_elev))
        return math.degrees(math.asin(sin_elev))

    def test_nadir_directly_above_equator(self):
        """Satellite directly above equator (0, 0) should have nadir at (0, 0)."""
        # Satellite at 600km above equator on prime meridian
        import math
        A = 6378137.0
        sat_alt = 600e3
        sat_ecef = (A + sat_alt, 0.0, 0.0)
        nadir = self._compute_nadir(*sat_ecef)
        # nadir should be close to (A, 0, 0)
        assert abs(nadir[0] - A) < 1.0  # within 1 meter
        assert abs(nadir[1]) < 1.0
        assert abs(nadir[2]) < 1.0

    def test_nadir_over_north_pole(self):
        """Satellite above the North Pole should have nadir near (0, 0, B)."""
        import math
        B = 6356752.314245  # WGS-84 semi-minor axis
        sat_ecef = (0.0, 0.0, B + 600e3)
        nadir = self._compute_nadir(*sat_ecef)
        assert abs(nadir[0]) < 1.0
        assert abs(nadir[1]) < 1.0
        assert abs(nadir[2] - B) < 1.0

    def test_distance_ue_to_nadir_vs_satellite(self):
        """Distance to nadir should be less than slant range to satellite."""
        # UE at Atlanta, GA (33.749, -84.388)
        ue_ecef = self._geo_to_ecef(33.749, -84.388, 300.0)
        # Satellite 600km above (33.0, -84.0)
        sat_ecef = self._geo_to_ecef(33.0, -84.0, 600e3)
        nadir = self._compute_nadir(*sat_ecef)

        dist_to_nadir = self._ecef_distance(ue_ecef, nadir)
        dist_to_sat = self._ecef_distance(ue_ecef, sat_ecef)

        assert dist_to_nadir < dist_to_sat

    def test_extrapolation_changes_distance(self):
        """After extrapolation, satellite moves and distance to UE changes."""
        # Satellite initially overhead
        ue_ecef = self._geo_to_ecef(33.749, -84.388, 300.0)
        sat_x, sat_y, sat_z = self._geo_to_ecef(33.749, -84.388, 600e3)

        # Velocity: ~7.5 km/s eastward (typical LEO)
        vx, vy, vz = -5000.0, 5000.0, 2000.0

        dt = 60.0  # 60 seconds
        new_x = sat_x + vx * dt
        new_y = sat_y + vy * dt
        new_z = sat_z + vz * dt

        dist_before = self._ecef_distance(ue_ecef, (sat_x, sat_y, sat_z))
        dist_after = self._ecef_distance(ue_ecef, (new_x, new_y, new_z))

        # Distance should have changed significantly
        assert abs(dist_after - dist_before) > 10000  # at least 10km change

    def test_elevation_angle_overhead(self):
        """Satellite directly overhead should have ~90° elevation."""
        ue_geo = (33.749, -84.388, 300.0)
        ue_ecef = self._geo_to_ecef(*ue_geo)
        sat_ecef = self._geo_to_ecef(33.749, -84.388, 600e3)

        elev = self._elevation_angle(ue_geo, ue_ecef, sat_ecef)
        assert elev > 85.0  # nearly overhead

    def test_elevation_angle_near_horizon(self):
        """Satellite far away should have low elevation angle."""
        ue_geo = (33.749, -84.388, 300.0)
        ue_ecef = self._geo_to_ecef(*ue_geo)
        # Satellite 600km high but far away laterally
        sat_ecef = self._geo_to_ecef(10.0, -50.0, 600e3)

        elev = self._elevation_angle(ue_geo, ue_ecef, sat_ecef)
        assert elev < 30.0  # well below overhead

    def test_d1_trigger_logic_distance_exceeds_threshold(self):
        """D1_SIB19 should trigger when distance from UE to nadir exceeds threshold."""
        ue_ecef = self._geo_to_ecef(33.749, -84.388, 300.0)
        # Satellite far from the UE
        sat_ecef = self._geo_to_ecef(10.0, -50.0, 600e3)
        nadir = self._compute_nadir(*sat_ecef)

        dist = self._ecef_distance(ue_ecef, nadir)
        threshold = 500000.0  # 500km

        # Margin per 3GPP D1: distance - threshold > 0 means triggered
        margin = dist - threshold
        assert margin > 0, f"Expected D1 trigger: dist={dist:.0f}m > thresh={threshold:.0f}m"

    def test_d1_no_trigger_when_close(self):
        """D1_SIB19 should NOT trigger when UE is close to nadir."""
        ue_ecef = self._geo_to_ecef(33.749, -84.388, 300.0)
        # Satellite nearly overhead
        sat_ecef = self._geo_to_ecef(33.75, -84.39, 600e3)
        nadir = self._compute_nadir(*sat_ecef)

        dist = self._ecef_distance(ue_ecef, nadir)
        threshold = 500000.0

        margin = dist - threshold
        assert margin < 0, f"Expected no trigger: dist={dist:.0f}m < thresh={threshold:.0f}m"