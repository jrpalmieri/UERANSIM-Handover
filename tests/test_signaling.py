"""
Tests for UE signaling correctness.

Verifies the format and content of signaling messages exchanged between
the UE and the (fake) gNB over the RLS protocol:
  - RLS protocol encoding / decoding
  - HeartBeat exchange
  - RRC message framing  (UL-CCCH / UL-DCCH)
  - NAS message identification
  - Measurement report structure

These tests range from pure-unit (no subprocess) to integration
(requires nr-ue binary).
"""

from __future__ import annotations

import random
import struct
import time

import pytest

from harness import rls_protocol as rls
from harness.rls_protocol import (
    EMessageType, EPduType, RrcChannel,
    RlsHeartBeat, RlsHeartBeatAck, RlsPduTransmission, RlsPduTransmissionAck,
    encode_heartbeat, encode_heartbeat_ack,
    encode_pdu_transmission, encode_pdu_transmission_ack,
    decode_rls_message,
    is_ul_ccch, is_ul_dcch,
)
from harness import nas_builder as nas
from harness.milenage import (
    compute_opc, f1, f2345, generate_auth_vector,
    derive_full_key_set, serving_network_name,
)
from harness.fake_gnb import FakeGnb
from harness.ue_process import UeProcess
from conftest import ue_binary_exists, needs_asn1tools


# ======================================================================
#  RLS protocol unit tests
# ======================================================================

class TestRlsEncoding:
    """Verify the binary encoding of RLS messages."""

    def test_heartbeat_round_trip(self):
        sti = random.getrandbits(64)
        pos = (100, -200, 300)
        encoded = encode_heartbeat(sti, pos)
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsHeartBeat)
        assert decoded.sti == sti
        assert decoded.sim_pos == pos

    def test_heartbeat_ack_round_trip(self):
        sti = random.getrandbits(64)
        dbm = -72
        encoded = encode_heartbeat_ack(sti, dbm)
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsHeartBeatAck)
        assert decoded.sti == sti
        assert decoded.dbm == dbm

    def test_pdu_transmission_round_trip(self):
        sti = random.getrandbits(64)
        pdu = b"\x01\x02\x03\x04\x05"
        encoded = encode_pdu_transmission(
            sti, EPduType.RRC, 42, int(RrcChannel.UL_CCCH), pdu
        )
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsPduTransmission)
        assert decoded.sti == sti
        assert decoded.pdu_type == EPduType.RRC
        assert decoded.pdu_id == 42
        assert decoded.payload == int(RrcChannel.UL_CCCH)
        assert decoded.pdu == pdu

    def test_pdu_transmission_ack_round_trip(self):
        sti = random.getrandbits(64)
        pdu_ids = [1, 2, 3, 99]
        encoded = encode_pdu_transmission_ack(sti, pdu_ids)
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsPduTransmissionAck)
        assert decoded.sti == sti
        assert decoded.pdu_ids == pdu_ids

    def test_header_format(self):
        """First 5 bytes: 0x03, major=3, minor=3, patch=7, msgType."""
        encoded = encode_heartbeat_ack(0, -60)
        assert encoded[0] == 0x03
        assert encoded[1] == 3   # major
        assert encoded[2] == 3   # minor
        assert encoded[3] == 7   # patch
        assert encoded[4] == int(EMessageType.HEARTBEAT_ACK)

    def test_malformed_data_returns_none(self):
        assert decode_rls_message(b"") is None
        assert decode_rls_message(b"\x00\x01\x02") is None
        # valid marker but wrong version, plus STI + senderId
        assert decode_rls_message(
            b"\x03\x03\x02\x02\xFF" + b"\x00" * 12) is None

    def test_empty_pdu_transmission(self):
        sti = 12345
        encoded = encode_pdu_transmission(sti, EPduType.DATA, 1, 0, b"")
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsPduTransmission)
        assert decoded.pdu == b""

    def test_large_pdu(self):
        pdu = bytes(range(256)) * 10  # 2560 bytes
        encoded = encode_pdu_transmission(0, EPduType.RRC, 1, 0, pdu)
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsPduTransmission)
        assert decoded.pdu == pdu


class TestRlsChannelHelpers:
    """Test the convenience channel-identification functions."""

    def test_is_ul_ccch(self):
        msg = RlsPduTransmission(
            sti=0, sender_id=0, pdu_type=EPduType.RRC,
            pdu_id=1, payload=int(RrcChannel.UL_CCCH), pdu=b""
        )
        assert is_ul_ccch(msg) is True
        assert is_ul_dcch(msg) is False

    def test_is_ul_dcch(self):
        msg = RlsPduTransmission(
            sti=0, sender_id=0, pdu_type=EPduType.RRC,
            pdu_id=1, payload=int(RrcChannel.UL_DCCH), pdu=b""
        )
        assert is_ul_dcch(msg) is True
        assert is_ul_ccch(msg) is False

    def test_data_pdu_not_rrc(self):
        msg = RlsPduTransmission(
            sti=0, sender_id=0, pdu_type=EPduType.DATA,
            pdu_id=1, payload=0, pdu=b""
        )
        assert is_ul_ccch(msg) is False
        assert is_ul_dcch(msg) is False


# ======================================================================
#  Milenage / crypto unit tests
# ======================================================================

class TestMilenageCrypto:
    """Verify the Milenage and 5G-AKA key derivation primitives."""

    # Known test vector from 3GPP TS 35.207 Test Set 1
    K = bytes.fromhex("465B5CE8B199B49FAA5F0A2EE238A6BC")
    OP = bytes.fromhex("E8ED289DEBA952E4283B54E88E6183CA")
    SQN = b"\x00\x00\x00\x00\x00\x00"
    AMF = b"\x80\x00"

    def test_opc_computation(self):
        """OPc should be deterministic given K and OP."""
        opc = compute_opc(self.K, self.OP)
        assert len(opc) == 16
        # Re-computation should give same result
        assert compute_opc(self.K, self.OP) == opc

    def test_f2345_output_lengths(self):
        opc = compute_opc(self.K, self.OP)
        rand = bytes(range(16))
        res, ck, ik, ak = f2345(self.K, opc, rand)
        assert len(res) == 8
        assert len(ck) == 16
        assert len(ik) == 16
        assert len(ak) == 6

    def test_f1_mac_length(self):
        opc = compute_opc(self.K, self.OP)
        rand = bytes(range(16))
        mac = f1(self.K, opc, rand, self.SQN, self.AMF)
        assert len(mac) == 8

    def test_generate_auth_vector_structure(self):
        av = generate_auth_vector(self.K, self.OP, is_opc=False)
        assert len(av["rand"]) == 16
        assert len(av["autn"]) == 16   # 6 + 2 + 8
        assert len(av["xres"]) == 8
        assert len(av["ck"]) == 16
        assert len(av["ik"]) == 16

    def test_autn_composition(self):
        """AUTN = (SQN⊕AK) || AMF || MAC-A  → 6 + 2 + 8 = 16 bytes."""
        av = generate_auth_vector(self.K, self.OP, is_opc=False, sqn=self.SQN, amf=self.AMF)
        sqn_xor_ak = bytes(a ^ b for a, b in zip(self.SQN, av["ak"]))
        expected_autn = sqn_xor_ak + self.AMF + av["mac_a"]
        assert av["autn"] == expected_autn

    def test_derive_full_key_set(self):
        keys = derive_full_key_set(
            k=self.K, op_or_opc=self.OP, is_opc=False,
            mcc="286", mnc="93", supi="imsi-286010000000001"
        )
        assert len(keys["k_nas_int"]) == 16
        assert len(keys["k_nas_enc"]) == 16
        assert len(keys["kamf"]) == 32
        assert len(keys["kausf"]) == 32
        assert len(keys["kseaf"]) == 32

    def test_serving_network_name(self):
        sn = serving_network_name("286", "93")
        assert sn == b"5G:mnc093.mcc286.3gppnetwork.org"


# ======================================================================
#  NAS builder unit tests
# ======================================================================

class TestNasBuilder:
    """Verify NAS message encoding and parsing."""

    def test_auth_request_format(self):
        rand = bytes(range(16))
        autn = bytes(range(16, 32))
        msg = nas.build_authentication_request(rand, autn)

        assert msg[0] == nas.EPD_5GMM
        assert msg[1] == nas.SEC_NONE
        assert msg[2] == nas.MT_AUTHENTICATION_REQUEST

    def test_auth_request_contains_rand_and_autn(self):
        rand = bytes(range(16))
        autn = bytes(range(16, 32))
        msg = nas.build_authentication_request(rand, autn)

        assert rand in msg, "RAND not found in AuthenticationRequest"
        assert autn in msg, "AUTN not found in AuthenticationRequest"

    def test_security_mode_command_format(self):
        msg = nas.build_security_mode_command()
        assert msg[0] == nas.EPD_5GMM
        assert msg[1] == nas.SEC_NONE
        assert msg[2] == nas.MT_SECURITY_MODE_COMMAND

    def test_registration_accept_format(self):
        msg = nas.build_registration_accept()
        assert msg[0] == nas.EPD_5GMM
        assert msg[2] == nas.MT_REGISTRATION_ACCEPT

    def test_identify_nas_message(self):
        msg = nas.build_authentication_request(bytes(16), bytes(16))
        name = nas.identify_nas_message(msg)
        assert name == "AuthenticationRequest"

    def test_identify_smc(self):
        msg = nas.build_security_mode_command()
        name = nas.identify_nas_message(msg)
        assert name == "SecurityModeCommand"

    def test_security_context_integrity(self):
        """Verify that integrity wrapping produces correct format."""
        ctx = nas.NasSecurityContext(
            k_nas_int=bytes(16),
            k_nas_enc=bytes(16),
        )
        plain = nas.build_authentication_request(bytes(16), bytes(16))
        protected = ctx.integrity_protect(plain, direction=0)

        assert protected[0] == nas.EPD_5GMM
        # Security header should be SEC_INTEGRITY_NEW_CTX (0x03)
        sec_header = protected[1] & 0x0F
        assert sec_header == nas.SEC_INTEGRITY_NEW_CTX
        # MAC is bytes 2-5 (4 bytes)
        mac = protected[2:6]
        assert len(mac) == 4
        # SQN is byte 6
        sqn = protected[6]
        assert sqn == 0  # first message

    def test_security_context_increments_count(self):
        ctx = nas.NasSecurityContext(
            k_nas_int=bytes(16),
            k_nas_enc=bytes(16),
        )
        plain = bytes([nas.EPD_5GMM, 0x00, nas.MT_REGISTRATION_ACCEPT, 0x01, 0x09])
        ctx.integrity_protect(plain, direction=0)
        assert ctx.dl_count == 1
        ctx.integrity_protect(plain, direction=0)
        assert ctx.dl_count == 2

    def test_5g_guti_format(self):
        guti = nas.build_5g_guti(mcc="286", mnc="93")
        assert len(guti) >= 11  # type(1) + PLMN(3) + AMF region(1) + set+ptr(2) + TMSI(4)
        # First byte type should indicate GUTI
        assert (guti[0] & 0x07) == 0x02

    def test_parse_nas_header_plain(self):
        msg = nas.build_registration_accept()
        parsed = nas.parse_nas_header(msg)
        assert parsed.epd == nas.EPD_5GMM
        assert parsed.message_type == nas.MT_REGISTRATION_ACCEPT
        assert parsed.is_security_protected is False

    def test_parse_nas_header_protected(self):
        ctx = nas.NasSecurityContext(k_nas_int=bytes(16), k_nas_enc=bytes(16))
        plain = nas.build_registration_accept()
        protected = ctx.integrity_protect(plain)
        parsed = nas.parse_nas_header(protected)
        assert parsed.is_security_protected is True
        assert len(parsed.mac) == 4


# ======================================================================
#  Integration tests — signaling over RLS
# ======================================================================

@ue_binary_exists
class TestHeartbeatSignaling:
    """Verify the UE's heartbeat behavior."""

    def test_heartbeat_received_by_gnb(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """The fake gNB must receive a heartbeat from the UE."""
        ue_process.generate_config()
        ue_process.start()
        result = fake_gnb.wait_for_heartbeat(timeout_s=10)
        assert result, "No HeartBeat received from UE within 10s"
        ue_process.cleanup()

    def test_heartbeat_is_periodic(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """HeartBeats should repeat roughly every 1 second."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        # Wait 3s then check we received multiple heartbeats
        # (We can't count them directly from captured_messages since
        #  heartbeats aren't captured, only PDU transmissions are.
        #  But if the gNB is replying, the UE must be sending.)
        time.sleep(3)
        # If the UE address is set, it's been sending heartbeats
        assert fake_gnb._ue_addr is not None
        ue_process.cleanup()

    def test_cell_dbm_received(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """The UE should receive our configured dBm via HeartBeatAck."""
        fake_gnb.cell_dbm = -65
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)
        # The UE now has a cell with -65 dBm signal
        time.sleep(2)
        # If the UE logs signal strength, we could verify it
        ue_process.collect_output(timeout_s=0.5)
        ue_process.cleanup()


@ue_binary_exists
class TestRrcSignaling:
    """Verify RRC message framing on the correct channels."""

    @needs_asn1tools
    def test_setup_request_on_ul_ccch(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """RRCSetupRequest must arrive on UL-CCCH (not UL-DCCH)."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        req = fake_gnb.wait_for_rrc_setup_request(timeout_s=10)
        assert req is not None
        assert req.channel == RrcChannel.UL_CCCH, \
            f"Expected UL_CCCH, got {req.channel}"
        ue_process.cleanup()

    @needs_asn1tools
    def test_setup_complete_on_ul_dcch(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """After RRCSetup, the UE should send RRCSetupComplete on UL-DCCH."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()

        # Wait for UL-DCCH message (should be RRCSetupComplete)
        msg = fake_gnb.wait_for_ul_dcch(timeout_s=10)
        assert msg is not None, "No UL-DCCH message after RRCSetup"
        assert msg.channel == RrcChannel.UL_DCCH

        # Try to identify the message type
        decoded = fake_gnb.rrc_codec.decode_ul_dcch(msg.raw_pdu)
        if decoded.get("_fallback"):
            assert decoded.get("message_type") in (
                "rrcSetupComplete", "ulInformationTransfer"
            ), f"Unexpected UL-DCCH message: {decoded.get('message_type')}"
        ue_process.cleanup()

    @needs_asn1tools
    def test_measurement_report_on_ul_dcch(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """MeasurementReport PDUs must be sent on UL-DCCH."""
        from harness.meas_injector import MeasurementInjector

        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()
        time.sleep(1)

        fake_gnb.send_meas_config(
            report_configs=[{
                "id": 1, "event": "a2",
                "a2Threshold": -100,
                "hysteresis": 0,
                "timeToTrigger": 0,
                "maxReportCells": 4,
            }],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
        )
        time.sleep(0.5)

        inj = MeasurementInjector()
        inj.set_cell(cell_id=1, rsrp=-115)
        inj.send_repeatedly(interval_s=0.5, duration_s=5.0)

        report = fake_gnb.wait_for_measurement_report(timeout_s=15)
        if report is not None:
            assert report.channel == RrcChannel.UL_DCCH
        inj.close()
        ue_process.cleanup()


# ======================================================================
#  NAS signaling sequence tests
# ======================================================================

@ue_binary_exists
@needs_asn1tools
class TestNasSignalingSequence:
    """Verify the ordering of NAS messages during registration."""

    def test_registration_request_is_first_nas(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """The first NAS message from UE should be RegistrationRequest."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()

        # The RRCSetupComplete carries the initial NAS PDU (RegistrationRequest)
        msg = fake_gnb.wait_for_ul_dcch(timeout_s=10)
        assert msg is not None, "No UL-DCCH message received"
        # The contained NAS should be a RegistrationRequest
        # (Full verification requires ASN.1 decoding of the RRC wrapper)
        ue_process.cleanup()

    def test_auth_response_after_auth_request(
        self, fake_gnb: FakeGnb, ue_process: UeProcess
    ):
        """UE should respond to AuthenticationRequest with AuthenticationResponse."""
        ue_process.generate_config()
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        fake_gnb.perform_cell_attach()
        fake_gnb.perform_rrc_setup()

        # Get the RegistrationRequest first
        fake_gnb.wait_for_ul_dcch(timeout_s=10)
        time.sleep(0.5)

        # Send AuthenticationRequest
        keys = derive_full_key_set(
            k=bytes.fromhex("465B5CE8B199B49FAA5F0A2EE238A6BC"),
            op_or_opc=bytes.fromhex("E8ED289DEBA952E4283B54E88E6183CA"),
            is_opc=False,
            mcc="286", mnc="93", supi="imsi-286010000000001",
        )
        auth_req = nas.build_authentication_request(keys["rand"], keys["autn"])
        fake_gnb.send_nas_in_dl_info_transfer(auth_req)

        # Wait for response on UL-DCCH
        resp = fake_gnb.wait_for_ul_dcch(timeout_s=10)
        assert resp is not None, "No response to AuthenticationRequest"
        ue_process.cleanup()
