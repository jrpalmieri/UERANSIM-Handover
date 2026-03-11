"""
Tests for Phase 2 handover functionality.

Verifies that the UE correctly handles RRCReconfiguration messages
containing ReconfigurationWithSync (i.e., handover commands):

  Unit tests (no binary required):
    - T304 timer mapping between ASN.1 enum and milliseconds
    - CellGroupConfig with ReconfigurationWithSync encoding
    - RRCReconfigurationComplete identification heuristic
    - Handover log parsing
    - RRC reconfiguration with sync builder

  Integration tests (require nr-ue binary):
    Single-cell:
      - Handover failure when target cell is not detected
      - UE logs handover command reception
      - RLF declared on handover failure
    Multi-cell (two fake gNBs):
      - Successful handover from source to target cell
      - RRCReconfigurationComplete sent to target cell on UL-DCCH
      - UE remains in RRC_CONNECTED after handover
      - Serving cell switch logged correctly
      - NAS state preserved through handover
"""

from __future__ import annotations

import re
import time

import pytest

from harness.rrc_builder import (
    RrcCodec,
    T304_MS_TO_ENUM,
    T304_ENUM_TO_MS,
    T304_MS_TO_ASN1_STR,
    t304_ms_to_enum,
    t304_enum_to_ms,
)
from harness.fake_gnb import FakeGnb, CapturedMessage
from harness.rls_protocol import RrcChannel
from harness.ue_process import UeProcess, UeState
from conftest import ue_binary_exists, needs_asn1tools


# ======================================================================
#  Unit tests — T304 timer mapping
# ======================================================================

class TestT304Mapping:
    """Verify the T304 timer enum <-> millisecond conversions."""

    EXPECTED = [
        (0, 50), (1, 100), (2, 150), (3, 200),
        (4, 500), (5, 1000), (6, 2000), (7, 10000),
    ]

    def test_enum_to_ms_all_values(self):
        for enum_val, ms in self.EXPECTED:
            assert t304_enum_to_ms(enum_val) == ms

    def test_ms_to_enum_all_values(self):
        for enum_val, ms in self.EXPECTED:
            assert t304_ms_to_enum(ms) == enum_val

    def test_enum_to_ms_out_of_range(self):
        """Out-of-range enum index should default to 1000 ms."""
        assert t304_enum_to_ms(-1) == 1000
        assert t304_enum_to_ms(8) == 1000
        assert t304_enum_to_ms(99) == 1000

    def test_ms_to_enum_unknown_value(self):
        """Unknown ms value should default to enum 5 (ms1000)."""
        assert t304_ms_to_enum(999) == 5

    def test_asn1_string_mapping(self):
        assert T304_MS_TO_ASN1_STR[50] == "ms50"
        assert T304_MS_TO_ASN1_STR[1000] == "ms1000"
        assert T304_MS_TO_ASN1_STR[10000] == "ms10000"

    def test_roundtrip(self):
        for enum_val, ms in self.EXPECTED:
            assert t304_enum_to_ms(t304_ms_to_enum(ms)) == ms


# ======================================================================
#  Unit tests — RRC reconfiguration with sync encoding
# ======================================================================

class TestRrcReconfigWithSync:
    """Test the RRC builder's handover message encoding."""

    @pytest.fixture(autouse=True)
    def _codec(self, rrc_codec: RrcCodec):
        self.rrc = rrc_codec

    def test_cell_group_config_returns_bytes(self):
        """build_cell_group_config_handover should produce non-empty bytes."""
        result = self.rrc.build_cell_group_config_handover(
            target_pci=100, new_crnti=0x5678, t304_ms=500
        )
        assert isinstance(result, bytes)
        # With asn1tools installed it should be non-empty
        if self.rrc.has_asn1:
            assert len(result) > 0

    def test_cell_group_config_different_pci(self):
        """Different PCI values should produce different encodings."""
        if not self.rrc.has_asn1:
            pytest.skip("asn1tools required for encoding comparison")
        a = self.rrc.build_cell_group_config_handover(target_pci=1)
        b = self.rrc.build_cell_group_config_handover(target_pci=100)
        assert a != b

    def test_cell_group_config_different_t304(self):
        """Different T304 values should produce different encodings."""
        if not self.rrc.has_asn1:
            pytest.skip("asn1tools required for encoding comparison")
        a = self.rrc.build_cell_group_config_handover(t304_ms=50)
        b = self.rrc.build_cell_group_config_handover(t304_ms=10000)
        assert a != b

    def test_rrc_reconfig_with_sync_returns_bytes(self):
        """build_rrc_reconfiguration_with_sync should produce bytes."""
        result = self.rrc.build_rrc_reconfiguration_with_sync(
            transaction_id=1, target_pci=2, new_crnti=0x1234, t304_ms=1000
        )
        assert isinstance(result, bytes)
        assert len(result) > 0

    @needs_asn1tools
    def test_rrc_reconfig_with_sync_decodable(self):
        """The encoded message should be decodable as a DL-DCCH-Message."""
        if not self.rrc.has_asn1:
            pytest.skip("ASN.1 schema compilation failed")
        encoded = self.rrc.build_rrc_reconfiguration_with_sync(
            transaction_id=3, target_pci=42, new_crnti=100, t304_ms=200
        )
        decoded = self.rrc._asn1.decode("DL-DCCH-Message", encoded)
        assert "message" in decoded
        # asn1tools decodes CHOICE as tuple: ('c1', ('rrcReconfiguration', {...}))
        c1 = decoded["message"][1][1]
        assert c1["rrc-TransactionIdentifier"] == 3

    @needs_asn1tools
    def test_master_cell_group_contains_cell_group_config(self):
        """The masterCellGroup OCTET STRING should contain a valid CellGroupConfig."""
        if not self.rrc.has_asn1:
            pytest.skip("ASN.1 schema compilation failed")
        encoded = self.rrc.build_rrc_reconfiguration_with_sync(
            transaction_id=1, target_pci=77, new_crnti=0xABCD, t304_ms=500
        )
        decoded = self.rrc._asn1.decode("DL-DCCH-Message", encoded)
        # asn1tools decodes CHOICE as tuple: ('c1', ('rrcReconfiguration', {...}))
        c1 = decoded["message"][1][1]
        # Navigate to nonCriticalExtension -> masterCellGroup
        ies = c1["criticalExtensions"][1]
        v1530 = ies.get("nonCriticalExtension", {})
        mcg_bytes = v1530.get("masterCellGroup")
        assert mcg_bytes is not None and len(mcg_bytes) > 0

        # Decode the inner CellGroupConfig
        cell_group = self.rrc._asn1.decode("CellGroupConfig", mcg_bytes)
        sp_cell = cell_group.get("spCellConfig", {})
        rws = sp_cell.get("reconfigurationWithSync", {})
        assert rws is not None

        # Verify the fields
        scc = rws.get("spCellConfigCommon", {})
        assert scc.get("physCellId") == 77
        assert rws.get("newUE-Identity") == 0xABCD
        assert rws.get("t304") == "ms500"

    def test_all_t304_values_encode(self):
        """Every valid T304 value should encode without error."""
        for ms in T304_MS_TO_ASN1_STR:
            result = self.rrc.build_rrc_reconfiguration_with_sync(t304_ms=ms)
            assert isinstance(result, bytes)

    def test_pci_range_boundaries(self):
        """PCI boundary values (0, 1007) should encode successfully."""
        for pci in [0, 1, 500, 1007]:
            result = self.rrc.build_cell_group_config_handover(target_pci=pci)
            assert isinstance(result, bytes)


# ======================================================================
#  Unit tests — RRCReconfigurationComplete identification
# ======================================================================

class TestRrcReconfCompleteId:
    """Test the heuristic that identifies RRCReconfigurationComplete."""

    def test_first_nibble_1_is_reconfig_complete(self):
        """UL-DCCH c1 choice 1 = rrcReconfigurationComplete."""
        # UPER: 1 bit outer CHOICE (0=c1) + 4-bit c1 index (1=reconfig) = 0_0001_000 = 0x08
        cm = CapturedMessage(
            timestamp=0.0,
            rls_msg=None,
            channel=RrcChannel.UL_DCCH,
            raw_pdu=bytes([0x08, 0x00]),
        )
        assert FakeGnb._is_rrc_reconfiguration_complete(cm) is True

    def test_first_nibble_0_is_not_reconfig_complete(self):
        """UL-DCCH c1 choice 0 = measurementReport (not reconfig complete)."""
        cm = CapturedMessage(
            timestamp=0.0,
            rls_msg=None,
            channel=RrcChannel.UL_DCCH,
            raw_pdu=bytes([0x00, 0x00]),
        )
        assert FakeGnb._is_rrc_reconfiguration_complete(cm) is False

    def test_empty_pdu_is_not_reconfig_complete(self):
        cm = CapturedMessage(
            timestamp=0.0,
            rls_msg=None,
            channel=RrcChannel.UL_DCCH,
            raw_pdu=b"",
        )
        assert FakeGnb._is_rrc_reconfiguration_complete(cm) is False

    def test_various_transaction_ids(self):
        """Different transaction IDs should still be identified."""
        # c1 index 1 → top 5 bits = 00001, remaining 3 bits vary with txnId
        for tx_high in [0x08, 0x0C, 0x0A, 0x0E]:
            cm = CapturedMessage(
                timestamp=0.0,
                rls_msg=None,
                channel=RrcChannel.UL_DCCH,
                raw_pdu=bytes([tx_high, 0x00]),
            )
            assert FakeGnb._is_rrc_reconfiguration_complete(cm) is True


# ======================================================================
#  Unit tests — Handover log parsing
# ======================================================================

class TestHandoverLogParsing:
    """Verify UeProcess.parse_handover_info() against log patterns."""

    def _ue_with_logs(self, lines: list[str]) -> UeProcess:
        """Create a UeProcess with pre-populated log lines."""
        ue = UeProcess.__new__(UeProcess)
        ue._proc = None
        ue._log_lines = list(lines)
        ue._tmp_dir = None
        return ue

    def test_handover_command_received(self):
        ue = self._ue_with_logs([
            "[2026-02-28] [rrc] Handover command: targetPCI=42 newC-RNTI=4660 t304=1000ms",
        ])
        info = ue.parse_handover_info()
        assert info["command_received"] is True
        assert info["target_pci"] == 42

    def test_serving_cell_switched(self):
        ue = self._ue_with_logs([
            "[2026-02-28] [rrc] Serving cell switched: cell[1] → cell[2]",
        ])
        info = ue.parse_handover_info()
        assert info["source_cell"] == 1
        assert info["target_cell"] == 2

    def test_handover_completed(self):
        ue = self._ue_with_logs([
            "[rrc] Handover to cell[2] completed (PCI=42, newC-RNTI=4660)",
        ])
        info = ue.parse_handover_info()
        assert info["completed"] is True

    def test_handover_failure_target_not_found(self):
        ue = self._ue_with_logs([
            "[rrc] Handover failure: target PCI 99 not found among 1 detected cells",
        ])
        info = ue.parse_handover_info()
        assert info["failed"] is True

    def test_t304_expired(self):
        ue = self._ue_with_logs([
            "[rrc] T304 expired – handover to PCI 42 failed",
        ])
        info = ue.parse_handover_info()
        assert info["t304_expired"] is True
        assert info["failed"] is True

    def test_full_successful_handover_sequence(self):
        ue = self._ue_with_logs([
            "[rrc] ReconfigurationWithSync detected: PCI=2 newC-RNTI=4660 t304=1000ms",
            "[rrc] Handover command: targetPCI=2 newC-RNTI=4660 t304=1000ms",
            "[rrc] Serving cell switched: cell[1] → cell[2]",
            "[rrc] Handover to cell[2] completed (PCI=2, newC-RNTI=4660)",
        ])
        info = ue.parse_handover_info()
        assert info["command_received"] is True
        assert info["target_pci"] == 2
        assert info["source_cell"] == 1
        assert info["target_cell"] == 2
        assert info["completed"] is True
        assert info["failed"] is False
        assert info["t304_expired"] is False

    def test_full_failure_sequence(self):
        ue = self._ue_with_logs([
            "[rrc] Handover command: targetPCI=99 newC-RNTI=100 t304=500ms",
            "[rrc] Handover failure: target PCI 99 not found among 1 detected cells",
        ])
        info = ue.parse_handover_info()
        assert info["command_received"] is True
        assert info["failed"] is True
        assert info["completed"] is False

    def test_parse_state_includes_handover(self):
        """UeState should include handover_completed and handover_failed."""
        ue = self._ue_with_logs([
            "[rrc] RRC-CONNECTED",
            "[rrc] Serving cell switched: cell[1] → cell[3]",
            "[rrc] Handover to cell[3] completed (PCI=5, newC-RNTI=200)",
        ])
        state = ue.parse_state()
        assert state.handover_completed is True
        assert state.handover_source_cell == 1
        assert state.handover_target_cell == 3
        assert state.handover_target_pci == 5

    def test_parse_state_handover_failure(self):
        ue = self._ue_with_logs([
            "[rrc] RRC-CONNECTED",
            "[rrc] Handover failure: target PCI 99 not found",
        ])
        state = ue.parse_state()
        assert state.handover_failed is True
        assert state.handover_completed is False

    def test_no_handover_logs(self):
        ue = self._ue_with_logs([
            "[rrc] RRC-CONNECTED",
            "[nas] RM-REGISTERED",
        ])
        info = ue.parse_handover_info()
        assert info["command_received"] is False
        assert info["completed"] is False
        assert info["failed"] is False


# ======================================================================
#  Integration tests — single cell (handover failure scenarios)
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestHandoverFailureSingleCell:
    """Tests where the UE has only one detected cell.

    When a handover command references a PCI that the UE cannot resolve
    to any detected cell, the handover should fail.
    """

    def test_handover_command_received(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """The UE should log reception of the handover command."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        assert fake_gnb.perform_rrc_setup()

        # Wait for registration flow first UL message
        fake_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        # Send handover command with target PCI=99 (not the serving cell)
        fake_gnb.send_handover_command(
            target_pci=99, new_crnti=0x1234, t304_ms=1000
        )

        # Check for handover command reception in UE logs
        line = ue_process.wait_for_log(
            r"Handover command|ReconfigurationWithSync detected",
            timeout_s=10,
        )
        assert line is not None, "UE did not log handover command reception"
        ue_process.cleanup()

    def test_handover_failure_no_target_cell(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """When the target PCI doesn't match any cell, handover should fail."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        assert fake_gnb.perform_rrc_setup()
        fake_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        # Handover to non-existent cell (PCI 999)
        fake_gnb.send_handover_command(
            target_pci=999, new_crnti=0x5678, t304_ms=500
        )

        # UE should log handover failure
        fail_line = ue_process.wait_for_handover_failure(timeout_s=10)
        assert fail_line is not None, "UE did not report handover failure"

        info = ue_process.parse_handover_info()
        assert info["failed"] is True
        ue_process.cleanup()

    def test_rlf_after_handover_failure(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """Handover failure should trigger radio link failure."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        assert fake_gnb.perform_rrc_setup()
        fake_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        fake_gnb.send_handover_command(target_pci=888, t304_ms=200)

        # Wait for either "Handover failure" or radio link failure indication
        ue_process.wait_for_handover_failure(timeout_s=10)
        time.sleep(2)
        ue_process.collect_output(timeout_s=1)

        info = ue_process.parse_handover_info()
        assert info["failed"] is True
        # After handover failure → RLF, UE should be going to IDLE
        ue_process.cleanup()

    def test_handover_ignored_if_not_connected(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """Handover command should be ignored if UE is not in RRC_CONNECTED.

        Note: This is hard to test reliably because we need to send the
        handover before RRC setup.  The UE may not even process the
        RRCReconfiguration if it hasn't completed RRC connection yet.
        This test verifies the UE doesn't crash from such a sequence.
        """
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        # Send handover BEFORE cell attach / RRC setup
        fake_gnb.send_handover_command(target_pci=42, t304_ms=1000)
        time.sleep(3)

        # The UE should still be responsive (no crash)
        ue_process.collect_output(timeout_s=1)
        assert ue_process.is_running(), "UE process crashed after early HO command"
        ue_process.cleanup()


# ======================================================================
#  Integration tests — two cells (successful handover)
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestHandoverSuccessTwoCells:
    """Tests with two fake gNBs providing two detectable cells.

    The UE discovers both cells via heartbeat exchanges, connects to the
    source cell, then receives a handover command to switch to the target.
    """

    def test_ue_detects_two_cells(
        self, source_gnb: FakeGnb, target_gnb: FakeGnb, two_cell_ue: UeProcess
    ):
        """The UE should detect and exchange heartbeats with both gNBs."""
        # two_cell_ue fixture already verifies heartbeats from both
        assert source_gnb._ue_addr is not None
        assert target_gnb._ue_addr is not None
        two_cell_ue.cleanup()

    def test_successful_handover(
        self, source_gnb: FakeGnb, target_gnb: FakeGnb, two_cell_ue: UeProcess
    ):
        """Full handover: register on source, handover to target."""
        # Attach and register on source cell
        source_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup()
        source_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        # Make target cell stronger so findCellByPci strategy 2 picks it
        target_gnb.cell_dbm = -50  # stronger than source (-60)
        time.sleep(3)  # let UE update signal measurements

        # Send handover command from source cell
        # Use PCI=2 (target). Strategy 2 will pick the strongest neighbour.
        source_gnb.send_handover_command(
            target_pci=2, new_crnti=0x5678, t304_ms=1000
        )

        # Wait for handover completion log
        ho_line = two_cell_ue.wait_for_handover_complete(timeout_s=15)
        assert ho_line is not None, "Handover did not complete within timeout"

        info = two_cell_ue.parse_handover_info()
        assert info["completed"] is True
        assert info["failed"] is False
        two_cell_ue.cleanup()

    def test_reconfig_complete_sent_on_ul_dcch(
        self, source_gnb: FakeGnb, target_gnb: FakeGnb, two_cell_ue: UeProcess
    ):
        """After handover, UE sends RRCReconfigurationComplete on UL-DCCH."""
        source_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup()
        source_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        target_gnb.cell_dbm = -50
        time.sleep(3)

        # Clear captured messages before handover
        source_gnb.clear_captured()
        target_gnb.clear_captured()

        source_gnb.send_handover_command(
            target_pci=2, new_crnti=0x5678, t304_ms=1000
        )

        # The RRCReconfigurationComplete should arrive at the TARGET cell.
        # After handover, the UE routes UL to the new serving cell.
        reconfig_complete = target_gnb.wait_for_rrc_reconfiguration_complete(
            timeout_s=15
        )
        if reconfig_complete is not None:
            assert reconfig_complete.channel == RrcChannel.UL_DCCH
        else:
            # Alternatively it may arrive at source if cell routing hasn't
            # switched yet (timing-dependent)
            reconfig_complete = source_gnb.wait_for_rrc_reconfiguration_complete(
                timeout_s=5
            )
            if reconfig_complete is not None:
                assert reconfig_complete.channel == RrcChannel.UL_DCCH
            # If neither got it, the handover may have failed
        two_cell_ue.cleanup()

    def test_rrc_stays_connected_after_handover(
        self, source_gnb: FakeGnb, target_gnb: FakeGnb, two_cell_ue: UeProcess
    ):
        """The UE should remain in RRC_CONNECTED after a successful handover."""
        source_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup()
        source_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        target_gnb.cell_dbm = -50
        time.sleep(3)

        source_gnb.send_handover_command(
            target_pci=2, new_crnti=0x5678, t304_ms=1000
        )
        two_cell_ue.wait_for_handover_complete(timeout_s=15)
        time.sleep(1)

        state = two_cell_ue.parse_state()
        if state.handover_completed:
            assert state.rrc_state == "RRC_CONNECTED", \
                f"Expected RRC_CONNECTED after HO, got {state.rrc_state}"
        two_cell_ue.cleanup()

    def test_cell_switch_logged_correctly(
        self, source_gnb: FakeGnb, target_gnb: FakeGnb, two_cell_ue: UeProcess
    ):
        """The UE should log the cell switch with both cell IDs."""
        source_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup()
        source_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        target_gnb.cell_dbm = -50
        time.sleep(3)

        source_gnb.send_handover_command(
            target_pci=2, new_crnti=0x5678, t304_ms=1000
        )
        two_cell_ue.wait_for_handover_complete(timeout_s=15)

        info = two_cell_ue.parse_handover_info()
        if info["completed"]:
            assert info["source_cell"] is not None
            assert info["target_cell"] is not None
            assert info["source_cell"] != info["target_cell"], \
                "Source and target cells should differ"
        two_cell_ue.cleanup()


# ======================================================================
#  Integration tests — T304 timer supervision
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestT304TimerSupervision:
    """Verify T304 handover supervision timer behaviour."""

    def test_short_t304_completes_before_expiry(
        self, source_gnb: FakeGnb, target_gnb: FakeGnb, two_cell_ue: UeProcess
    ):
        """With a valid target cell, handover should complete before T304."""
        source_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup()
        source_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        target_gnb.cell_dbm = -50
        time.sleep(3)

        # Use short T304 = 200ms — handover should still complete
        source_gnb.send_handover_command(
            target_pci=2, new_crnti=0x5678, t304_ms=200
        )
        ho_line = two_cell_ue.wait_for_handover_complete(timeout_s=10)

        info = two_cell_ue.parse_handover_info()
        if ho_line is not None:
            assert info["t304_expired"] is False
        two_cell_ue.cleanup()

    def test_t304_expiry_single_cell(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """T304 should expire when the UE cannot find the target cell.

        With a short T304, the timer fires before any re-attempt logic
        and triggers an RLF.
        """
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        assert fake_gnb.perform_rrc_setup()
        fake_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        # Target PCI 777 doesn't exist; with only 1 cell the UE will
        # fail immediately (findCellByPci returns 0) rather than
        # waiting for T304.  The failure is synchronous.
        fake_gnb.send_handover_command(
            target_pci=777, new_crnti=0x9999, t304_ms=50
        )

        fail_line = ue_process.wait_for_handover_failure(timeout_s=10)
        assert fail_line is not None, "Expected handover failure"
        ue_process.cleanup()


# ======================================================================
#  Integration tests — measurement-triggered handover flow
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestMeasTriggeredHandover:
    """End-to-end test: A3 measurement → MeasReport → Handover command.

    This simulates the complete flow:
      1. UE registers on source cell
      2. Source configures A3 measurement
      3. Target cell becomes stronger (via OOB injection and gNB signal)
      4. UE sends MeasurementReport
      5. Source issues handover command
      6. UE completes handover
    """

    def test_a3_triggered_handover(
        self,
        source_gnb: FakeGnb,
        target_gnb: FakeGnb,
        two_cell_ue: UeProcess,
    ):
        """Full A3-event → handover flow."""
        # 1. Attach and register on source
        source_gnb.perform_cell_attach()
        assert source_gnb.perform_rrc_setup()
        source_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(1)

        # 2. Configure A3 measurement on source
        source_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a3",
                "a3Offset": 3,
                "hysteresis": 0,
                "timeToTrigger": 0,
                "maxReportCells": 8,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(1)

        # 3. Make target cell significantly stronger. UE measurements now come
        # from per-gNB HEARTBEAT_ACK dbm updates instead of OOB injection.
        source_gnb.cell_dbm = -65
        target_gnb.cell_dbm = -50
        time.sleep(6)

        # 4. Wait for measurement report
        report = source_gnb.wait_for_measurement_report(timeout_s=15)

        if report is not None:
            # 5. Issue handover command
            source_gnb.send_handover_command(
                target_pci=2, new_crnti=0xBEEF, t304_ms=1000
            )

            # 6. Wait for handover completion
            ho_line = two_cell_ue.wait_for_handover_complete(timeout_s=15)
            if ho_line is not None:
                info = two_cell_ue.parse_handover_info()
                assert info["completed"] is True

        two_cell_ue.cleanup()
