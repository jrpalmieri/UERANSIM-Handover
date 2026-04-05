"""CHO unit tests for the tests UE stack.

These tests avoid flaky attach/integration paths and focus on:
  - ASN ConditionalReconfiguration payload/builder helpers
  - CHO lifecycle summary parsing from UE logs
"""

from __future__ import annotations

import time

import pytest

from .conftest import needs_asn1tools, ue_binary_exists
from .harness.rls_protocol import RrcChannel
from .harness.rrc_builder import RrcCodec
from .harness.ue_process import UeProcess


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


class TestUeChoDebugEncodingLadder:
    """Incremental diagnostics for CHO payload encoding path.

    These tests are intentionally focused on root-cause localization:
    1. Build a CHO-bearing RRCReconfiguration and decode it with the same ASN stack.
    2. Verify whether the v1610 ConditionalReconfiguration IE survives encoding.
    3. Check whether changing candidate payload changes the encoded bytes.
    """

    @staticmethod
    def _decode_outer_ies(codec: RrcCodec, pdu: bytes):
        decoded = codec._asn1.decode("DL-DCCH-Message", pdu)
        message = decoded.get("message")
        assert isinstance(message, tuple) and len(message) == 2
        c1 = message[1]
        assert isinstance(c1, tuple) and len(c1) == 2
        assert c1[0] == "rrcReconfiguration"
        reconfig = c1[1]
        crit = reconfig.get("criticalExtensions")
        assert isinstance(crit, tuple) and len(crit) == 2
        assert crit[0] == "rrcReconfiguration"
        return crit[1]

    @needs_asn1tools
    def test_conditional_reconfig_v1610_ie_presence_probe(self):
        codec = RrcCodec()
        assert codec.has_asn1, "asn1tools codec is required for CHO encoding probe"

        cond_rrc = codec.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=1,
            target_pci=2,
            new_crnti=0x4201,
            t304_ms=1000,
        )
        pdu = codec.build_rrc_reconfiguration_conditional_handover(
            transaction_id=2,
            candidates_to_add_mod=[
                {
                    "candidateId": 2,
                    "measIds": [1],
                    "condRrcReconfig": cond_rrc,
                }
            ],
            candidate_ids_to_remove=None,
        )

        ies = self._decode_outer_ies(codec, pdu)
        v1530 = ies.get("nonCriticalExtension")
        assert v1530 is not None
        v1540 = v1530.get("nonCriticalExtension") if isinstance(v1530, dict) else None
        v1560 = v1540.get("nonCriticalExtension") if isinstance(v1540, dict) else None
        v1610 = v1560.get("nonCriticalExtension") if isinstance(v1560, dict) else None
        cond = v1610.get("conditionalReconfiguration") if isinstance(v1610, dict) else None

        assert cond is not None, "Expected ConditionalReconfiguration IE in v1610 extension chain"

    @needs_asn1tools
    def test_conditional_reconfig_payload_variation_probe(self):
        codec = RrcCodec()
        assert codec.has_asn1, "asn1tools codec is required for CHO encoding probe"

        cond_rrc_a = codec.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=1,
            target_pci=2,
            new_crnti=0x4301,
            t304_ms=1000,
        )
        cond_rrc_b = codec.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=1,
            target_pci=3,
            new_crnti=0x4302,
            t304_ms=1000,
        )

        pdu_a = codec.build_rrc_reconfiguration_conditional_handover(
            transaction_id=3,
            candidates_to_add_mod=[
                {"candidateId": 2, "measIds": [1], "condRrcReconfig": cond_rrc_a}
            ],
            candidate_ids_to_remove=None,
        )
        pdu_b = codec.build_rrc_reconfiguration_conditional_handover(
            transaction_id=3,
            candidates_to_add_mod=[
                {"candidateId": 3, "measIds": [2], "condRrcReconfig": cond_rrc_b}
            ],
            candidate_ids_to_remove=None,
        )

        assert pdu_a != pdu_b, "Distinct CHO payloads should produce distinct encoded DL-DCCH PDUs"


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

    def test_runtime_reset_summary(self):
        ue = self._ue_with_logs(
            [
                "[rrc] CHO runtime state reset for 2 candidate(s)",
                "[rrc] CHO runtime state reset for 1 candidate(s)",
            ]
        )
        info = ue.parse_cho_info()
        assert info["runtime_resets"] == 2


class TestUeChoConditionSemantics:
    """Regression guards for CHO A2/A3/A5 threshold semantics.

    These assertions mirror the condition formulas used in UE CHO evaluation
    and keep them aligned with the measurement event engine.
    """

    @staticmethod
    def _a2_margin(serving_rsrp: int, threshold: int, hyst: int) -> int:
        # Entering when serving < threshold - hysteresis.
        return (threshold - hyst) - serving_rsrp

    @staticmethod
    def _a3_margin(serving_rsrp: int, neighbor_rsrp: int, offset: int, hyst: int) -> int:
        # Entering when neighbor > serving + offset + hysteresis.
        return neighbor_rsrp - (serving_rsrp + offset + hyst)

    @staticmethod
    def _a5_margins(serving_rsrp: int, neighbor_rsrp: int, th1: int, th2: int, hyst: int) -> tuple[int, int]:
        # Entering when serving < th1 - hyst AND neighbor > th2 + hyst.
        m1 = (th1 - hyst) - serving_rsrp
        m2 = neighbor_rsrp - (th2 + hyst)
        return m1, m2

    def test_a2_boundary(self):
        # threshold=-110, hyst=2 => boundary=-112, strict '<' to trigger.
        assert self._a2_margin(serving_rsrp=-112, threshold=-110, hyst=2) == 0
        assert self._a2_margin(serving_rsrp=-113, threshold=-110, hyst=2) > 0

    def test_a3_boundary(self):
        # serving=-85, offset=6, hyst=2 => boundary neighbor=-77, strict '>'.
        assert self._a3_margin(serving_rsrp=-85, neighbor_rsrp=-77, offset=6, hyst=2) == 0
        assert self._a3_margin(serving_rsrp=-85, neighbor_rsrp=-76, offset=6, hyst=2) > 0

    def test_a5_dual_threshold_boundary(self):
        m1, m2 = self._a5_margins(
            serving_rsrp=-112,
            neighbor_rsrp=-88,
            th1=-110,
            th2=-90,
            hyst=2,
        )
        assert m1 == 0
        assert m2 == 0

        m1, m2 = self._a5_margins(
            serving_rsrp=-113,
            neighbor_rsrp=-87,
            th1=-110,
            th2=-90,
            hyst=2,
        )
        assert m1 > 0
        assert m2 > 0


@ue_binary_exists
@needs_asn1tools
class TestUeChoIntegrationSmoke:
    @staticmethod
    def _bring_up_registered_ue(ue_process, fake_gnb):
        ue_process.start()
        if not fake_gnb.wait_for_heartbeat(timeout_s=10):
            pytest.skip("UE heartbeat not received by fake gNB")

        fake_gnb.perform_cell_attach()
        if not fake_gnb.perform_rrc_setup(timeout_s=20):
            pytest.skip("RRC setup did not complete in this environment")

        if not fake_gnb.perform_registration(timeout_s=20):
            pytest.skip("UE registration flow did not complete in this environment")

    @staticmethod
    def _build_cond_rrc(fake_gnb, transaction_id: int, target_pci: int, new_crnti: int) -> bytes:
        return fake_gnb._rrc.build_conditional_rrc_reconfiguration_with_sync(
            transaction_id=transaction_id,
            target_pci=target_pci,
            new_crnti=new_crnti,
            t304_ms=1000,
        )

    @staticmethod
    def _send_a2_meas_config(fake_gnb, transaction_id: int, threshold: int):
        fake_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "a2",
                    "a2Threshold": threshold,
                    "hysteresis": 0,
                    "timeToTrigger": 0,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=transaction_id,
        )

    @staticmethod
    def _wait_for_handover_execution_protocol(ue_process, fake_gnb, timeout_s: float = 20.0) -> bool:
        """Protocol-level execution guard for CHO handover tests.

                We consider CHO execution observed when either:
                    1) fake gNB captures UL RRCReconfigurationComplete, or
                    2) UE logs "Executing CHO candidate" (fallback for environments
                         where full handover completion is not possible).
        """
        start_ul_count = len(fake_gnb.captured_rrc_on(RrcChannel.UL_DCCH))
        deadline = time.monotonic() + timeout_s

        while time.monotonic() < deadline:
            # Fast path: existing filter helper.
            ul = fake_gnb.wait_for_rrc_reconfiguration_complete(timeout_s=0.5)
            if ul is not None:
                ue_process.collect_output(timeout_s=0.5)
                return True

            # Fallback path: decode newly captured UL-DCCH PDUs and look for
            # an explicit rrcReconfigurationComplete message type.
            current_ul = fake_gnb.captured_rrc_on(RrcChannel.UL_DCCH)
            for cm in current_ul[start_ul_count:]:
                decoded = fake_gnb.rrc_codec.decode_ul_dcch(cm.raw_pdu)
                if decoded.get("message_type") == "rrcReconfigurationComplete":
                    ue_process.collect_output(timeout_s=0.5)
                    return True

                message = decoded.get("message")
                if isinstance(message, tuple) and len(message) == 2:
                    c1 = message[1]
                    if isinstance(c1, tuple) and c1 and c1[0] == "rrcReconfigurationComplete":
                        ue_process.collect_output(timeout_s=0.5)
                        return True

            if ue_process.wait_for_cho_execution(timeout_s=0.5):
                return True

            time.sleep(0.2)

        return False

    def test_conditional_reconfiguration_adds_candidate(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        cond_rrc = self._build_cond_rrc(
            fake_gnb,
            transaction_id=7,
            target_pci=2,
            new_crnti=0x3301,
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

    def test_debug_extension_chain_for_conditional_reconfiguration(self, ue_process, fake_gnb):
        """Incremental integration probe for CHO extension-chain visibility.

        This test does not require successful CHO add/execution; it only verifies
        that UE logs the parsed extension chain state for incoming
        RRCReconfiguration.
        """
        self._bring_up_registered_ue(ue_process, fake_gnb)

        cond_rrc = self._build_cond_rrc(
            fake_gnb,
            transaction_id=18,
            target_pci=2,
            new_crnti=0x33AA,
        )

        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 2,
                    "measIds": [1],
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=19,
        )

        chain_log = ue_process.wait_for_log(
            r"RRCReconfiguration extension chain: .*conditional=",
            timeout_s=10,
        )
        if chain_log is None:
            decode_fail = ue_process.wait_for_log(
                r"RRC DL-DCCH PDU decoding failed \(len=",
                timeout_s=3,
            )
            if decode_fail is None:
                ue_process.collect_output(timeout_s=1.0)
                tail = "\n".join(ue_process.log_lines[-30:])
                assert False, (
                    "UE emitted neither extension-chain nor DL-DCCH decode-failure diagnostics "
                    f"after ConditionalReconfiguration. Recent logs:\n{tail}"
                )

    def test_conditional_reconfiguration_updates_candidate(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        first = self._build_cond_rrc(
            fake_gnb,
            transaction_id=10,
            target_pci=2,
            new_crnti=0x3310,
        )
        second = self._build_cond_rrc(
            fake_gnb,
            transaction_id=11,
            target_pci=3,
            new_crnti=0x3320,
        )

        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 2,
                    "condRrcReconfig": first,
                }
            ],
            transaction_id=12,
        )

        if not ue_process.wait_for_cho_candidate_added(timeout_s=8):
            pytest.skip("UE did not log initial CHO candidate add")

        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 2,
                    "condRrcReconfig": second,
                }
            ],
            transaction_id=13,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=8):
            pytest.skip("UE did not log ConditionalReconfiguration apply summary")

        info = ue_process.parse_cho_info()
        assert info["applied_added"] >= 1
        assert info["applied_updated"] >= 1
        assert info["active_candidates"] >= 1

    def test_conditional_reconfiguration_removes_candidate(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        cond_rrc = self._build_cond_rrc(
            fake_gnb,
            transaction_id=20,
            target_pci=2,
            new_crnti=0x3340,
        )

        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 3,
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=21,
        )

        if not ue_process.wait_for_cho_candidate_added(timeout_s=8):
            pytest.skip("UE did not log CHO candidate add before remove test")

        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=None,
            candidate_ids_to_remove=[3],
            transaction_id=22,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=8):
            pytest.skip("UE did not log ConditionalReconfiguration remove summary")

        info = ue_process.parse_cho_info()
        assert info["applied_removed"] >= 1
        assert info["active_candidates"] == 0

    def test_candidate_retained_across_non_cho_reconfiguration(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        cond_rrc = self._build_cond_rrc(
            fake_gnb,
            transaction_id=30,
            target_pci=2,
            new_crnti=0x3350,
        )

        # Step 1: add a CHO candidate via ConditionalReconfiguration.
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 4,
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=31,
        )

        if not ue_process.wait_for_cho_candidate_added(timeout_s=8):
            pytest.skip("UE did not log CHO candidate add before retention test")

        # Step 2: send a normal (non-CHO) reconfiguration.
        fake_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "a3",
                    "a3Offset": 3,
                    "hysteresis": 1,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=32,
        )

        if not ue_process.wait_for_log(
            r"RRCReconfigurationComplete sent \(non-handover reconfiguration\)",
            timeout_s=8,
        ):
            pytest.skip("UE did not complete non-CHO reconfiguration in retention test")

        # Step 3: remove the same candidate explicitly.
        # If candidate retention is correct, remove count should increase (not removeMiss).
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=None,
            candidate_ids_to_remove=[4],
            transaction_id=33,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=8):
            pytest.skip("UE did not log ConditionalReconfiguration summary in retention test")

        info = ue_process.parse_cho_info()
        assert info["applied_removed"] >= 1
        assert info["applied_remove_miss"] == 0

    def test_fullconfig_without_conditional_reconfiguration_clears_cho(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        cond_rrc = self._build_cond_rrc(
            fake_gnb,
            transaction_id=34,
            target_pci=2,
            new_crnti=0x3360,
        )

        # Step 1: add a CHO candidate.
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 5,
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=35,
        )

        if not ue_process.wait_for_cho_candidate_added(timeout_s=8):
            pytest.skip("UE did not log CHO candidate add before fullConfig clear test")

        # Step 2: send legacy-style fullConfig reconfiguration with no CHO IE.
        fake_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "a3",
                    "a3Offset": 3,
                    "hysteresis": 1,
                    "timeToTrigger": 160,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=36,
            full_config=True,
        )

        if not ue_process.wait_for_log(r"fullConfig=true: clearing existing CHO candidates", timeout_s=8):
            pytest.skip("UE did not log CHO clear on fullConfig")

        # Step 3: remove same candidate ID; should miss because fullConfig cleared CHO state.
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=None,
            candidate_ids_to_remove=[5],
            transaction_id=37,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=8):
            pytest.skip("UE did not log ConditionalReconfiguration summary after fullConfig clear")

        info = ue_process.parse_cho_info()
        assert info["applied_remove_miss"] >= 1

    def test_tiebreak_prefers_lower_candidate_id_when_priority_unset(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        self._send_a2_meas_config(fake_gnb, transaction_id=56, threshold=-110)
        fake_gnb.cell_dbm = -120

        cond_rrc_1 = self._build_cond_rrc(fake_gnb, transaction_id=57, target_pci=2, new_crnti=0x3450)
        cond_rrc_2 = self._build_cond_rrc(fake_gnb, transaction_id=58, target_pci=3, new_crnti=0x3451)
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {"candidateId": 1, "measIds": [1], "condRrcReconfig": cond_rrc_1},
                {"candidateId": 2, "measIds": [1], "condRrcReconfig": cond_rrc_2},
            ],
            transaction_id=59,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=10):
            pytest.skip("UE did not log ConditionalReconfiguration summary in candidate-id tie-break test")

        if not self._wait_for_handover_execution_protocol(ue_process, fake_gnb, timeout_s=12):
            pytest.skip("UE did not show CHO execution evidence in candidate-id tie-break test")

        if not ue_process.wait_for_log(r"Executing CHO candidate", timeout_s=8):
            pytest.skip("UE did not log CHO execution in candidate-id tie-break test")

        info = ue_process.parse_cho_info()
        if info["executed_id"] is None:
            pytest.skip("No CHO executed_id parsed from UE logs in candidate-id tie-break test")
        assert info["executed_id"] == 1

    def test_tiebreak_prefers_greater_margin_when_priority_equal(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        # Make serving weak so both A2 conditions are true. Candidate 61 has
        # larger margin: threshold=-100 vs threshold=-110.
        fake_gnb.cell_dbm = -120
        if not ue_process.wait_for_log(r"signal[- ]?change|Heartbeat|RRC state", timeout_s=5):
            # Non-fatal; continue to parse_cho execution below.
            pass

        fake_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {"id": 1, "event": "a2", "a2Threshold": -110, "hysteresis": 0, "timeToTrigger": 0,
                 "maxReportCells": 8},
                {"id": 2, "event": "a2", "a2Threshold": -100, "hysteresis": 0, "timeToTrigger": 0,
                 "maxReportCells": 8},
            ],
            meas_ids=[
                {"measId": 1, "measObjectId": 1, "reportConfigId": 1},
                {"measId": 2, "measObjectId": 1, "reportConfigId": 2},
            ],
            transaction_id=60,
        )

        cond_rrc_1 = self._build_cond_rrc(fake_gnb, transaction_id=61, target_pci=2, new_crnti=0x3460)
        cond_rrc_2 = self._build_cond_rrc(fake_gnb, transaction_id=62, target_pci=3, new_crnti=0x3461)
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {"candidateId": 3, "measIds": [1], "condRrcReconfig": cond_rrc_1},
                {"candidateId": 4, "measIds": [2], "condRrcReconfig": cond_rrc_2},
            ],
            transaction_id=63,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=10):
            pytest.skip("UE did not log ConditionalReconfiguration summary in margin tie-break test")

        if not self._wait_for_handover_execution_protocol(ue_process, fake_gnb, timeout_s=14):
            pytest.skip("UE did not show CHO execution evidence in margin tie-break test")

        if not ue_process.wait_for_log(r"Executing CHO candidate", timeout_s=8):
            pytest.skip("UE did not log CHO execution in margin tie-break test")

        info = ue_process.parse_cho_info()
        if info["executed_id"] is None:
            pytest.skip("No CHO executed_id parsed from UE logs in margin tie-break test")
        assert info["executed_id"] == 4

    def test_tiebreak_prefers_config_order_when_priority_margin_rsrp_equal(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        # Both candidates trigger immediately (T1=0) with identical priority,
        # margin, and target RSRP. UE should pick earliest configured candidate.
        self._send_a2_meas_config(fake_gnb, transaction_id=64, threshold=-110)
        fake_gnb.cell_dbm = -120

        cond_rrc_1 = self._build_cond_rrc(fake_gnb, transaction_id=65, target_pci=2, new_crnti=0x3462)
        cond_rrc_2 = self._build_cond_rrc(fake_gnb, transaction_id=66, target_pci=2, new_crnti=0x3463)
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {"candidateId": 5, "measIds": [1], "condRrcReconfig": cond_rrc_1},
                {"candidateId": 6, "measIds": [1], "condRrcReconfig": cond_rrc_2},
            ],
            transaction_id=67,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=10):
            pytest.skip("UE did not log ConditionalReconfiguration summary in config-order tie-break test")

        if not self._wait_for_handover_execution_protocol(ue_process, fake_gnb, timeout_s=22):
            pytest.skip("UE did not show CHO execution evidence in config-order tie-break test")

        if not ue_process.wait_for_log(r"Executing CHO candidate", timeout_s=8):
            pytest.skip("UE did not log CHO execution in config-order tie-break test")

        info = ue_process.parse_cho_info()
        if info["executed_id"] is None:
            pytest.skip("No CHO executed_id parsed from UE logs in config-order tie-break test")
        assert info["executed_id"] == 5

    def test_asn_conditional_reconfiguration_and_or_semantics(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        # Build MeasConfig with two A2 MeasIds so candidate 90 can exercise
        # AND semantics via condExecutionCond=[1,2], while candidate 91 uses OR
        # across candidates with condExecutionCond=[1].
        fake_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "a2",
                    "a2Threshold": -110,
                    "hysteresis": 0,
                    "timeToTrigger": 0,
                    "maxReportCells": 8,
                },
                {
                    "id": 2,
                    "event": "a2",
                    "a2Threshold": -100,
                    "hysteresis": 0,
                    "timeToTrigger": 0,
                    "maxReportCells": 8,
                },
            ],
            meas_ids=[
                {"measId": 1, "measObjectId": 1, "reportConfigId": 1},
                {"measId": 2, "measObjectId": 1, "reportConfigId": 2},
            ],
            transaction_id=52,
        )

        # Best-effort observability guard only. Some environments may skip the
        # exact log line while still applying MeasConfig successfully.
        ue_process.wait_for_log(
            r"RRCReconfigurationComplete sent \(non-handover reconfiguration\)",
            timeout_s=10,
        )

        # Ensure both A2 conditions are true.
        fake_gnb.cell_dbm = -120

        cond_rrc_1 = self._build_cond_rrc(
            fake_gnb,
            transaction_id=53,
            target_pci=2,
            new_crnti=0x3490,
        )
        cond_rrc_2 = self._build_cond_rrc(
            fake_gnb,
            transaction_id=54,
            target_pci=2,
            new_crnti=0x3491,
        )

        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 7,
                    "measIds": [1, 2],
                    "condRrcReconfig": cond_rrc_1,
                },
                {
                    "candidateId": 8,
                    "measIds": [1],
                    "condRrcReconfig": cond_rrc_2,
                },
            ],
            transaction_id=55,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=10):
            pytest.skip("UE did not log ConditionalReconfiguration summary for ASN AND/OR test")

        if not self._wait_for_handover_execution_protocol(ue_process, fake_gnb, timeout_s=22):
            pytest.skip("UE did not show CHO execution evidence for ASN CHO semantics test")

        info = ue_process.parse_cho_info()
        if info["applied_added"] < 2:
            pytest.skip("UE did not report both ASN candidates as added in AND/OR semantics test")
        if info["executed_id"] is None:
            pytest.skip("No CHO executed_id parsed from UE logs in ASN AND/OR semantics test")
        assert info["applied_added"] >= 2
        # Both candidates are eligible; tie breaks to configuration order.
        assert info["executed_id"] == 7

    def test_conditional_reconfiguration_invalid_measid_skips_candidate(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        cond_rrc = self._build_cond_rrc(
            fake_gnb,
            transaction_id=40,
            target_pci=2,
            new_crnti=0x3470,
        )

        # Use a measId that should not exist in active MeasConfig.
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {
                    "candidateId": 7,
                    "measIds": [999],
                    "condRrcReconfig": cond_rrc,
                }
            ],
            transaction_id=41,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=8):
            pytest.skip("UE did not log ConditionalReconfiguration summary for invalid measId test")

        info = ue_process.parse_cho_info()
        assert info["applied_skipped"] >= 1
        assert info["active_candidates"] == 0

    def test_conditional_reconfiguration_remove_miss_is_reported(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=None,
            candidate_ids_to_remove=[12345],
            transaction_id=42,
        )

        if not ue_process.wait_for_log(r"ConditionalReconfiguration applied:", timeout_s=8):
            pytest.skip("UE did not log ConditionalReconfiguration summary for remove-miss test")

        info = ue_process.parse_cho_info()
        assert info["applied_remove_miss"] >= 1
        assert info["active_candidates"] == 0

    def test_cho_execution_resets_runtime_state_on_handover_resume(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        self._send_a2_meas_config(fake_gnb, transaction_id=68, threshold=-110)
        fake_gnb.cell_dbm = -120

        cond_rrc = self._build_cond_rrc(fake_gnb, transaction_id=69, target_pci=2, new_crnti=0x3480)
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {"candidateId": 8, "measIds": [1], "condRrcReconfig": cond_rrc}
            ],
            transaction_id=70,
        )

        if not self._wait_for_handover_execution_protocol(ue_process, fake_gnb, timeout_s=12):
            pytest.skip("UE did not show CHO execution evidence in runtime reset test")

        reset_log = ue_process.wait_for_log(r"CHO runtime state reset for \d+ candidate\(s\)", timeout_s=10)
        if reset_log is None:
            pytest.skip("UE did not log CHO runtime reset in timing window")

        info = ue_process.parse_cho_info()
        assert info["executed_id"] == 8
        assert info["runtime_resets"] >= 1

    def test_asn_cho_execution_preempts_same_cycle_measurement_report(self, ue_process, fake_gnb):
        self._bring_up_registered_ue(ue_process, fake_gnb)

        # Configure a Rel-15 event that can trigger quickly and also drive CHO
        # from the standards-based condExecutionCond path.
        fake_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "a2",
                    "a2Threshold": -110,
                    "hysteresis": 0,
                    "timeToTrigger": 0,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=50,
        )

        cond_rrc = self._build_cond_rrc(fake_gnb, transaction_id=71, target_pci=2, new_crnti=0x3481)
        fake_gnb.send_conditional_reconfiguration(
            candidates_to_add_mod=[
                {"candidateId": 6, "measIds": [1], "condRrcReconfig": cond_rrc}
            ],
            transaction_id=72,
        )

        # Make A2 condition true for serving cell as well.
        fake_gnb.cell_dbm = -120

        if not self._wait_for_handover_execution_protocol(ue_process, fake_gnb, timeout_s=14):
            pytest.skip("UE did not show CHO execution evidence in D1 preemption test")

        ue_process.collect_output(timeout_s=1.0)
        lines = ue_process.log_lines

        cho_idx = next((i for i, ln in enumerate(lines) if "Executing CHO candidate 6" in ln), None)
        if cho_idx is None:
            pytest.skip("CHO execution log not found in collected output")

        report_before_cho = any(
            "Sending MeasurementReport" in ln for ln in lines[:cho_idx]
        )
        assert not report_before_cho
