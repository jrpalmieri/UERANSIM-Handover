"""
Tests for UE RRC state transitions.

Verifies that the UE enters each of its RRC states (IDLE, CONNECTED)
and transitions correctly in response to gNB signaling.

State machine (simplified):
    RRC_IDLE  → (RRCSetup)       → RRC_CONNECTED
    RRC_CONNECTED → (RRCRelease) → RRC_IDLE
"""

from __future__ import annotations

import time

import pytest

from harness.fake_gnb import FakeGnb
from harness.ue_process import UeProcess
from harness.rls_protocol import RrcChannel
from conftest import ue_binary_exists, needs_asn1tools


# ======================================================================
#  Unit-level tests (no UE process — verify harness components)
# ======================================================================

class TestRrcStateConstants:
    """Verify that the UE state parser recognises all RRC state strings."""

    def test_parse_idle_from_log(self, ue_process: UeProcess):
        ue_process._log_lines = ["[2025-01-01] RRC state: RRC-IDLE"]
        state = ue_process.parse_state()
        assert state.rrc_state == "RRC_IDLE"

    def test_parse_connected_from_log(self, ue_process: UeProcess):
        ue_process._log_lines = ["[2025-01-01] switched to RRC-CONNECTED"]
        state = ue_process.parse_state()
        assert state.rrc_state == "RRC_CONNECTED"
        assert state.connected is True

    def test_parse_inactive_from_log(self, ue_process: UeProcess):
        ue_process._log_lines = ["[2025-01-01] RRC-INACTIVE mode"]
        state = ue_process.parse_state()
        assert state.rrc_state == "RRC_INACTIVE"


# ======================================================================
#  Integration tests (require nr-ue binary)
# ======================================================================

@ue_binary_exists
class TestUeStartsInIdle:
    """The UE must start in RRC_IDLE when launched."""

    def test_initial_rrc_state_is_idle(self, fake_gnb: FakeGnb, ue_process: UeProcess):
        """After startup, before any gNB interaction, the UE is RRC_IDLE."""
        ue_process.generate_config()
        ue_process.start()
        time.sleep(2)

        # The UE should be sending heartbeats but hasn't done RRC setup yet
        state = ue_process.parse_state()
        # Initial state is IDLE (no log line explicitly says so, so we
        # check that it's NOT connected)
        assert state.rrc_state != "RRC_CONNECTED"
        ue_process.cleanup()

    def test_ue_sends_heartbeats(self, fake_gnb: FakeGnb, ue_process: UeProcess):
        """The UE must send RLS HeartBeat messages to discover cells."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10), \
            "UE did not send a HeartBeat within 10 s"
        ue_process.cleanup()


@ue_binary_exists
class TestRrcIdleToConnected:
    """Test the RRC_IDLE → RRC_CONNECTED transition via RRCSetup."""

    @needs_asn1tools
    def test_rrc_setup_request_sent(self, fake_gnb: FakeGnb, ue_process: UeProcess):
        """After cell attach, UE sends RRCSetupRequest on UL-CCCH."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        # Send MIB + SIB1 to trigger cell selection
        fake_gnb.perform_cell_attach()
        time.sleep(1)

        # UE should send RRCSetupRequest
        req = fake_gnb.wait_for_rrc_setup_request(timeout_s=10)
        assert req is not None, "UE did not send RRCSetupRequest"
        assert req.channel == RrcChannel.UL_CCCH
        ue_process.cleanup()

    @needs_asn1tools
    def test_rrc_setup_transitions_to_connected(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """Completing RRCSetup should move UE to RRC_CONNECTED."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        success = fake_gnb.perform_rrc_setup()
        assert success, "RRC setup flow did not complete"

        # Give the UE time to process
        time.sleep(2)
        ue_process.collect_output(timeout_s=1)

        # Verify via log parsing
        state = ue_process.parse_state()
        assert state.rrc_state == "RRC_CONNECTED" or state.connected, \
            f"Expected RRC_CONNECTED, got {state.rrc_state}"
        ue_process.cleanup()


@ue_binary_exists
class TestRrcConnectedToIdle:
    """Test the RRC_CONNECTED → RRC_IDLE transition via RRCRelease."""

    @needs_asn1tools
    def test_rrc_release_returns_to_idle(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """Sending RRCRelease should return UE to RRC_IDLE."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        # Get to CONNECTED state first
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(2)

        # Now release
        fake_gnb.send_rrc_release()
        time.sleep(3)
        ue_process.collect_output(timeout_s=1)

        state = ue_process.parse_state()
        assert state.rrc_state == "RRC_IDLE" or not state.connected, \
            f"Expected RRC_IDLE after release, got {state.rrc_state}"
        ue_process.cleanup()


@ue_binary_exists
class TestRadioLinkFailure:
    """Test that the UE returns to RRC_IDLE on radio link failure."""

    @needs_asn1tools
    def test_signal_loss_causes_rlf(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """Stopping heartbeat ACKs should trigger RLF → RRC_IDLE."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        # Get to CONNECTED state
        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(2)

        # Stop the fake gNB → UE loses HeartBeatAcks → signal lost
        fake_gnb.stop()
        time.sleep(5)  # wait for HEARTBEAT_THRESHOLD (2s) + processing

        ue_process.collect_output(timeout_s=1)
        # UE should detect RLF and go back to IDLE
        has_rlf = ue_process.has_log(r"(?i)radio.link.failure|signal.lost|RRC-IDLE")
        assert has_rlf, "UE did not detect radio link failure"
        ue_process.cleanup()
