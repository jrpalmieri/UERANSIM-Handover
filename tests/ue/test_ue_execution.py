from __future__ import annotations

from .conftest import ue_binary_exists


@ue_binary_exists
class TestUeExecution:
    def test_ue_starts_and_reaches_fake_gnb(self, ue_process, fake_gnb):
        ue_process.start()

        assert ue_process.is_running()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

    def test_ue_sends_rrc_setup_request_after_cell_attach(self, ue_process, fake_gnb):
        """UE should send RRCSetupRequest on UL-CCCH after receiving MIB+SIB1."""
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10), "UE heartbeat not received by fake gNB"

        fake_gnb.perform_cell_attach()

        req = fake_gnb.wait_for_rrc_setup_request(timeout_s=15)
        assert req is not None, "UE did not send RRCSetupRequest after MIB+SIB1"

    def test_ue_rrc_connection_complete(self, ue_process, fake_gnb):
        """Full RRC setup exchange: cell attach → RRCSetupRequest → RRCSetup."""
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10), "UE heartbeat not received by fake gNB"

        fake_gnb.perform_cell_attach()
        assert fake_gnb.perform_rrc_setup(timeout_s=15), "RRC setup exchange did not complete"
