"""Unit tests for UE signaling helpers in tests.

Covers RLS framing helpers, Milenage key derivation helpers, and NAS builder
utilities without requiring external processes.
"""

from __future__ import annotations

import random

from .harness import nas_builder as nas
from .harness.milenage import (
    compute_opc,
    derive_full_key_set,
    f1,
    f2345,
    generate_auth_vector,
    serving_network_name,
)
from .harness.rls_protocol import (
    EMessageType,
    EPduType,
    RlsHeartBeat,
    RlsHeartBeatAck,
    RlsPduTransmission,
    RlsPduTransmissionAck,
    RrcChannel,
    decode_rls_message,
    encode_heartbeat,
    encode_heartbeat_ack,
    encode_pdu_transmission,
    encode_pdu_transmission_ack,
    is_ul_ccch,
    is_ul_dcch,
)


class TestRlsEncoding:
    def test_heartbeat_round_trip(self):
        sti = random.getrandbits(64)
        pos = (100.0, -200.0, 300.0)
        encoded = encode_heartbeat(sti, pos, sender_id=1, sender_id2=2)
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsHeartBeat)
        assert decoded.sti == sti
        assert decoded.sender_id == 1
        assert decoded.sender_id2 == 2
        assert decoded.sim_pos == pos

    def test_heartbeat_ack_round_trip(self):
        sti = random.getrandbits(64)
        encoded = encode_heartbeat_ack(sti, -72, sender_id=9, sender_id2=11)
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsHeartBeatAck)
        assert decoded.sti == sti
        assert decoded.sender_id == 9
        assert decoded.sender_id2 == 11
        assert decoded.dbm == -72

    def test_pdu_transmission_round_trip(self):
        sti = random.getrandbits(64)
        pdu = b"\x01\x02\x03\x04\x05"
        encoded = encode_pdu_transmission(
            sti,
            EPduType.RRC,
            42,
            int(RrcChannel.UL_CCCH),
            pdu,
            sender_id=3,
            sender_id2=4,
        )
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsPduTransmission)
        assert decoded.sti == sti
        assert decoded.sender_id == 3
        assert decoded.sender_id2 == 4
        assert decoded.pdu_type == EPduType.RRC
        assert decoded.pdu_id == 42
        assert decoded.payload == int(RrcChannel.UL_CCCH)
        assert decoded.pdu == pdu

    def test_pdu_transmission_ack_round_trip(self):
        sti = random.getrandbits(64)
        pdu_ids = [1, 2, 3, 99]
        encoded = encode_pdu_transmission_ack(sti, pdu_ids, sender_id=3, sender_id2=4)
        decoded = decode_rls_message(encoded)
        assert isinstance(decoded, RlsPduTransmissionAck)
        assert decoded.sti == sti
        assert decoded.sender_id == 3
        assert decoded.sender_id2 == 4
        assert decoded.pdu_ids == pdu_ids

    def test_header_format(self):
        encoded = encode_heartbeat_ack(0, -60)
        assert encoded[0] == 0x03
        assert encoded[1] == 3
        assert encoded[2] == 3
        assert encoded[3] == 7
        assert encoded[4] == int(EMessageType.HEARTBEAT_ACK)


class TestRlsChannelHelpers:
    def test_is_ul_ccch(self):
        msg = RlsPduTransmission(
            sti=0,
            sender_id=0,
            sender_id2=0,
            pdu_type=EPduType.RRC,
            pdu_id=1,
            payload=int(RrcChannel.UL_CCCH),
            pdu=b"",
        )
        assert is_ul_ccch(msg) is True
        assert is_ul_dcch(msg) is False

    def test_is_ul_dcch(self):
        msg = RlsPduTransmission(
            sti=0,
            sender_id=0,
            sender_id2=0,
            pdu_type=EPduType.RRC,
            pdu_id=1,
            payload=int(RrcChannel.UL_DCCH),
            pdu=b"",
        )
        assert is_ul_dcch(msg) is True
        assert is_ul_ccch(msg) is False


class TestMilenageCrypto:
    K = bytes.fromhex("465B5CE8B199B49FAA5F0A2EE238A6BC")
    OP = bytes.fromhex("E8ED289DEBA952E4283B54E88E6183CA")
    SQN = b"\x00\x00\x00\x00\x00\x00"
    AMF = b"\x80\x00"

    def test_opc_computation(self):
        opc = compute_opc(self.K, self.OP)
        assert len(opc) == 16
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
        assert len(av["autn"]) == 16
        assert len(av["xres"]) == 8

    def test_derive_full_key_set(self):
        keys = derive_full_key_set(
            k=self.K,
            op_or_opc=self.OP,
            is_opc=False,
            mcc="286",
            mnc="93",
            supi="imsi-286010000000001",
        )
        assert len(keys["k_nas_int"]) == 16
        assert len(keys["k_nas_enc"]) == 16

    def test_serving_network_name(self):
        sn = serving_network_name("286", "93")
        assert sn == b"5G:mnc093.mcc286.3gppnetwork.org"


class TestNasBuilder:
    def test_auth_request_format(self):
        msg = nas.build_authentication_request(bytes(range(16)), bytes(range(16, 32)))
        assert msg[0] == nas.EPD_5GMM
        assert msg[1] == nas.SEC_NONE
        assert msg[2] == nas.MT_AUTHENTICATION_REQUEST

    def test_security_mode_command_format(self):
        msg = nas.build_security_mode_command()
        assert msg[0] == nas.EPD_5GMM
        assert msg[1] == nas.SEC_NONE
        assert msg[2] == nas.MT_SECURITY_MODE_COMMAND

    def test_registration_accept_format(self):
        msg = nas.build_registration_accept()
        assert msg[0] == nas.EPD_5GMM
        assert msg[2] == nas.MT_REGISTRATION_ACCEPT

    def test_security_context_counts_increment(self):
        ctx = nas.NasSecurityContext(k_nas_int=bytes(16), k_nas_enc=bytes(16))
        plain = nas.build_registration_accept()
        ctx.integrity_protect(plain, direction=0)
        ctx.integrity_protect(plain, direction=0)
        assert ctx.dl_count == 2
