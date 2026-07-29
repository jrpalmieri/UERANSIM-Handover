"""
Minimal NGAP APER IE-level codec.

Parses and builds NGAP PDUs at the protocol-IE level using raw APER
byte manipulation.  Does NOT require asn1tools or a compiled ASN.1
schema — only handles the outer NGAP-PDU structure and individual IE
value construction for the specific types used by the gNB test harness.

Exception: build_pdu_session_resource_setup_request() below hand-rolls the
two protocol-IE containers (the outer message and its Transfer) exactly as
everywhere else in this module, but leans on asn1tools + the purpose-built
tests/data/asn1_specs/ngap-pdu-session-setup.asn1 module for the "leaf" IE
values (S-NSSAI, the UL tunnel endpoint, the QoS flow list) since those are
too deeply nested to hand-roll safely. asn1tools cannot compile the real
ngap-rel18-v18_9.asn1 at all: besides `(CONTAINING X)` syntax it doesn't
parse, its PER compiler has a genuine bug in resolving the information-
object-class open-type pattern that NGAP's own ProtocolIE-Container /
ProtocolIE-Field rely on (`NGAP-PROTOCOL-IES.&Value ({IEsSetParam}{@id})` —
a plain TypeError in
asn1tools.codecs.compiler.Compiler.pre_process_parameterization_step_1_dummy_to_actual_type,
independent of which types populate the object set). That's exactly why
this module hand-rolls containers instead of using asn1tools for them, and
why the trimmed .asn1 file retypes every iE-Extensions/choice-Extensions
field to NULL rather than defining ProtocolExtensionContainer for real.
Any future NGAP message built via asn1tools needs the same two workarounds.

Wire format reference (APER encoding of NGAP-PDU):

  Outer PDU:
    Byte 0:    CHOICE byte (0x00=initiating, 0x20=successful, 0x40=unsuccessful)
    Byte 1:    ProcedureCode (uint8)
    Byte 2:    Criticality   (0x00=reject, 0x40=ignore, 0x80=notify)
    Byte 3+:   Length-determinant for the value field
               Then value bytes

  Value (message body):
    Byte 0:    Extension preamble (0x00 = no extension)
    Byte 1-2:  Number of protocol IEs (uint16 big-endian)
    Then repeated IEs

  Each IE:
    Byte 0-1:  IE ID (uint16 big-endian)
    Byte 2:    Criticality
    Byte 3+:   Length-determinant for open-type value, then value bytes
"""

from __future__ import annotations

import socket
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ======================================================================
#  Constants
# ======================================================================

# -- PDU types (CHOICE index encoded in top 3 bits of byte 0) ---------
PDU_INITIATING_MESSAGE   = 0x00
PDU_SUCCESSFUL_OUTCOME   = 0x20
PDU_UNSUCCESSFUL_OUTCOME = 0x40

# -- Criticality -------------------------------------------------------
CRIT_REJECT = 0x00
CRIT_IGNORE = 0x40
CRIT_NOTIFY = 0x80

# -- Procedure codes (TS 38.413 §9.4) ---------------------------------
PROC_HANDOVER_PREPARATION    = 12
PROC_HANDOVER_CANCEL         = 10
PROC_ERROR_INDICATION        = 9
PROC_DOWNLINK_NAS_TRANSPORT  = 4
PROC_HANDOVER_NOTIFICATION   = 11
PROC_INITIAL_CONTEXT_SETUP   = 14
PROC_INITIAL_UE_MESSAGE      = 15
PROC_NG_SETUP                = 21
PROC_PATH_SWITCH_REQUEST     = 25
PROC_PDU_SESSION_RESOURCE_SETUP = 29
PROC_UE_CONTEXT_RELEASE_COMMAND = 41
PROC_UPLINK_NAS_TRANSPORT    = 46

# -- Protocol IE IDs (TS 38.413 §9.3) ---------------------------------
IE_ALLOWED_NSSAI              = 0
IE_AMF_NAME                   = 1
IE_AMF_OVERLOAD_RESPONSE      = 2
IE_AMF_UE_NGAP_ID             = 10
IE_CAUSE                      = 15
IE_CORE_NETWORK_ASSISTANCE_INFO = 18
IE_GUAMI                      = 28
IE_HANDOVER_TYPE               = 29
IE_NAS_PDU                    = 38
IE_PDU_SESSION_RESOURCE_SETUP_LIST_SU_REQ = 74
IE_PDU_SESSION_TYPE           = 134
IE_PLMN_SUPPORT_LIST          = 80
IE_QOS_FLOW_SETUP_REQUEST_LIST = 136
IE_RAN_UE_NGAP_ID             = 85
IE_RELATIVE_AMF_CAPACITY      = 86
IE_UL_NGU_UP_TNL_INFORMATION  = 139
IE_RRC_ESTABLISHMENT_CAUSE    = 90
IE_SECURITY_KEY                = 94
IE_SERVED_GUAMI_LIST           = 96
IE_SOURCE_TO_TARGET_CONTAINER  = 101
IE_TARGET_ID                   = 105
IE_TARGET_TO_SOURCE_CONTAINER  = 106
IE_UE_AGGREGATE_MAX_BITRATE   = 110
IE_UE_CONTEXT_REQUEST          = 112
IE_UE_SECURITY_CAPABILITIES   = 119
IE_USER_LOCATION_INFO          = 121

# SCTP Payload Protocol Identifier for NGAP
NGAP_PPID = 60


# ======================================================================
#  Data classes
# ======================================================================

@dataclass
class NgapIE:
    """One protocol IE (decoded at byte level)."""
    ie_id: int
    criticality: int       # raw byte: 0x00, 0x40, or 0x80
    value: bytes           # raw open-type bytes


@dataclass
class NgapPdu:
    """Decoded NGAP PDU at the IE level."""
    pdu_type: int          # PDU_INITIATING_MESSAGE, etc.
    procedure_code: int
    criticality: int       # raw byte
    ies: List[NgapIE] = field(default_factory=list)

    # Convenience helpers
    @property
    def is_initiating(self) -> bool:
        return self.pdu_type == PDU_INITIATING_MESSAGE

    @property
    def is_successful(self) -> bool:
        return self.pdu_type == PDU_SUCCESSFUL_OUTCOME

    @property
    def is_unsuccessful(self) -> bool:
        return self.pdu_type == PDU_UNSUCCESSFUL_OUTCOME

    def find_ie(self, ie_id: int) -> Optional[NgapIE]:
        """Return the first IE with the given ID, or None."""
        for ie in self.ies:
            if ie.ie_id == ie_id:
                return ie
        return None


# ======================================================================
#  Length-determinant helpers
# ======================================================================

def _encode_length(length: int) -> bytes:
    """Encode an APER length-determinant (unconstrained)."""
    if length < 128:
        return bytes([length])
    elif length < 16384:
        return bytes([0x80 | (length >> 8), length & 0xFF])
    else:
        raise ValueError(f"Length {length} too large for simple encoding")


def _decode_length(data: bytes, offset: int) -> Tuple[int, int]:
    """Decode an APER length-determinant.  Returns (length, new_offset)."""
    b = data[offset]
    if b & 0x80 == 0:
        return b, offset + 1
    else:
        length = ((b & 0x7F) << 8) | data[offset + 1]
        return length, offset + 2


# ======================================================================
#  Decoder
# ======================================================================

def decode_ngap_pdu(data: bytes) -> Optional[NgapPdu]:
    """Decode an APER-encoded NGAP PDU to IE-level representation.

    Returns None if the data is too short or obviously malformed.
    """
    if len(data) < 4:
        return None

    pdu_type = data[0] & 0xE0    # top 3 bits
    proc_code = data[1]
    criticality = data[2]

    # Length of the value field
    val_len, val_start = _decode_length(data, 3)

    if val_start + val_len > len(data):
        return None

    value = data[val_start : val_start + val_len]

    # Parse IEs from the value
    ies = _parse_ies(value)

    return NgapPdu(
        pdu_type=pdu_type,
        procedure_code=proc_code,
        criticality=criticality,
        ies=ies,
    )


def _parse_ies(value: bytes) -> List[NgapIE]:
    """Parse the protocolIEs from an NGAP message value field."""
    if len(value) < 3:
        return []

    # Byte 0: extension preamble (skip)
    # Byte 1-2: number of IEs
    num_ies = (value[1] << 8) | value[2]
    offset = 3

    ies: List[NgapIE] = []
    for _ in range(num_ies):
        if offset + 3 > len(value):
            break

        ie_id = (value[offset] << 8) | value[offset + 1]
        crit = value[offset + 2]
        offset += 3

        ie_len, offset = _decode_length(value, offset)
        if offset + ie_len > len(value):
            break

        ie_value = value[offset : offset + ie_len]
        offset += ie_len

        ies.append(NgapIE(ie_id=ie_id, criticality=crit, value=ie_value))

    return ies


# ======================================================================
#  Encoder
# ======================================================================

def encode_ngap_pdu(pdu: NgapPdu) -> bytes:
    """Encode an NgapPdu into APER bytes."""
    value = _encode_ies(pdu.ies)
    length_bytes = _encode_length(len(value))
    return bytes([pdu.pdu_type, pdu.procedure_code, pdu.criticality]) + length_bytes + value


def _encode_ies(ies: List[NgapIE]) -> bytes:
    """Encode a list of protocol IEs into the value field."""
    # Extension preamble (1 byte, 0x00 = not extended)
    buf = b'\x00'
    # Number of IEs (2 bytes, big-endian)
    buf += struct.pack(">H", len(ies))
    for ie in ies:
        buf += struct.pack(">H", ie.ie_id)        # IE ID
        buf += bytes([ie.criticality])              # Criticality
        buf += _encode_length(len(ie.value))        # Open-type length
        buf += ie.value                             # Open-type value
    return buf


# ======================================================================
#  IE value encoders for specific types
# ======================================================================

def _encode_aper_integer(value: int) -> bytes:
    """Encode a semi-constrained/extensible non-negative INTEGER (e.g. the
    BitRate fields of UEAggregateMaximumBitRate) using the APER "normally
    small length" + minimum-octets form.

    Not to be confused with _encode_aper_wide_constrained_integer below,
    which is for fully-constrained non-extensible ranges like the
    UE-NGAP-ID types and uses a different, empirically-verified wire form.
    This generic form is unverified against a live decode (unlike the
    UE-NGAP-ID path) -- treat it as best-effort should it ever misbehave.
    """
    if value < 0:
        raise ValueError("Only non-negative integers supported")
    if value == 0:
        n_bytes = 1
        val_bytes = b'\x00'
    else:
        n_bytes = (value.bit_length() + 7) // 8
        val_bytes = value.to_bytes(n_bytes, 'big')
    # Normally small length: (n_bytes - 1) << 1  (7-bit field, byte-aligned)
    nsl = (n_bytes - 1) << 1
    return bytes([nsl]) + val_bytes


def _encode_aper_wide_constrained_integer(value: int, max_octets: int) -> bytes:
    """Encode a non-negative INTEGER whose fully-constrained range needs more
    than 2 octets to represent (e.g. RAN-UE-NGAP-ID 0..2^32-1, max_octets=4;
    AMF-UE-NGAP-ID 0..2^40-1, max_octets=5).

    Empirically reverse-engineered from asn1c's own aper_encode_to_buffer()
    output (see the ngap_codec module docstring): aligned PER does NOT emit
    these as a raw fixed-width field, nor with the "normally small length"
    form. Instead the octet-count needed for the *value* (1..max_octets) is
    itself packed as a small constrained whole number into the leftmost bits
    of a single prefix octet -- using exactly ceil(log2(max_octets)) bits,
    left-aligned and zero-padded to the octet boundary -- followed by that
    many big-endian value octets. E.g. for RAN-UE-NGAP-ID (max_octets=4, so
    a 2-bit count field), value 1049576 needs 3 octets, encoding as
    prefix=(3-1)<<6=0x80 followed by 10 03 e8 -- confirmed byte-for-byte
    against a live gNB's InitialUEMessage and against aper_encode_to_buffer()
    called directly on ASN_NGAP_RAN_UE_NGAP_ID_t/ASN_NGAP_AMF_UE_NGAP_ID_t.
    """
    if value < 0 or value >= (1 << (8 * max_octets)):
        raise ValueError(f"value {value} does not fit in {max_octets} octets")
    count_bits = (max_octets - 1).bit_length()
    n = max(1, (value.bit_length() + 7) // 8)
    prefix = (n - 1) << (8 - count_bits)
    return bytes([prefix]) + value.to_bytes(n, 'big')


def _decode_aper_wide_constrained_integer(data: bytes, max_octets: int) -> int:
    """Inverse of _encode_aper_wide_constrained_integer (see its docstring)."""
    count_bits = (max_octets - 1).bit_length()
    shift = 8 - count_bits
    n = (data[0] >> shift) + 1
    return int.from_bytes(data[1:1 + n], 'big')


def _encode_aper_integer32(value: int) -> bytes:
    """Encode RAN-UE-NGAP-ID (INTEGER 0..2^32-1, max 4 value octets)."""
    return _encode_aper_wide_constrained_integer(value, 4)


def _encode_aper_integer40(value: int) -> bytes:
    """Encode AMF-UE-NGAP-ID (INTEGER 0..2^40-1, max 5 value octets)."""
    return _encode_aper_wide_constrained_integer(value, 5)


def _encode_octet_string(data: bytes) -> bytes:
    """Encode an unconstrained OCTET STRING: length-det + bytes."""
    return _encode_length(len(data)) + data


def _encode_printable_string_1_150(s: str) -> bytes:
    """Encode PrintableString(SIZE(1..150,...)) as seen in AMFName.

    APER bit-stream: 1 bit extension (0) + 8-bit constrained length
    (actual_length - 1, since lb=1) → 9 bits total, then byte-align,
    then 8-bit ASCII chars.
    """
    encoded_chars = s.encode('ascii')
    length_val = len(encoded_chars) - 1  # constrained: value - lb
    # Pack 9 bits (1 ext + 8 length) into a byte stream, then pad to boundary
    bits = (0 << 8) | (length_val & 0xFF)  # ext=0 in bit 8, length in bits 7-0
    # These 9 bits pack into 2 bytes: top 7 bits of `bits` in byte0, last 2 bits
    # shifted to top of byte1 (with 6 bits padding).
    byte0 = (bits >> 1) & 0xFF
    byte1 = (bits & 1) << 7  # remaining 1 bit shifted to MSB, rest is padding
    return bytes([byte0, byte1]) + encoded_chars


# ======================================================================
#  PLMN encoding  (BCD format, 3 bytes)
# ======================================================================

def encode_plmn(mcc: str, mnc: str) -> bytes:
    """Encode PLMN Identity as 3-byte BCD (TS 38.413 / TS 24.501).

    MCC = 3 digits, MNC = 2 or 3 digits.
    Byte 0: MCC digit 2 << 4 | MCC digit 1
    Byte 1: MNC digit 3 << 4 | MCC digit 3    (MNC digit 3 = 0xF if 2-digit MNC)
    Byte 2: MNC digit 2 << 4 | MNC digit 1
    """
    m1, m2, m3 = int(mcc[0]), int(mcc[1]), int(mcc[2])
    if len(mnc) == 2:
        n1, n2, n3 = int(mnc[0]), int(mnc[1]), 0xF
    else:
        n1, n2, n3 = int(mnc[0]), int(mnc[1]), int(mnc[2])
    return bytes([
        (m2 << 4) | m1,
        (n3 << 4) | m3,
        (n2 << 4) | n1,
    ])


# ======================================================================
#  Known-good NGAP mock templates (from UERANSIM's asn1c encoder)
# ======================================================================

# NGSetupResponse — verified working with the gNB's APER decoder.
# PLMN in template: MCC=901, MNC=70 (09 f1 07).
# PLMN bytes appear at byte offsets 31-33 and 47-49 (0-indexed).
_MOCK_NG_SETUP_RESPONSE_HEX = (
    "2015003200000400"
    "01000e05806f70656e3567732d616d6630"  # AMFName = "open5gs-amf0"
    "00600008000009f10702004000564001ff"  # ServedGUAMI + RelativeAMFCapacity
    "005000080009f10700000008"             # PLMNSupportList
)
_PLMN_OFFSETS_IN_NG_SETUP_RESP = [31, 47]  # byte positions of 3-byte PLMN

# InitialContextSetupRequest (mock #4 from sctp/task.cpp)
# AMF-UE-NGAP-ID=6 at value bytes 0-1 (IE value [0:2] = 00 06)
# RAN-UE-NGAP-ID=1 at IE value [0:2] = 00 01
_MOCK_INITIAL_CTX_SETUP_HEX = (
    "000e00809e000009"
    "000a00020006"                       # AMF-UE-NGAP-ID = 6
    "005500020001"                       # RAN-UE-NGAP-ID = 1
    "006e000a0c3e800000303e80000000"     # UEAggMaxBitRate
    "1c00070009f107020040"               # GUAMI
    "0000000200010077"                   # AllowedNSSAI
    "00091c000e0007000380"               # UESecurityCapabilities
    "00005e002091f9908eefe559d08b9f958b4d7c1a94094918dedcf40bb57c55428a00d4dee9"  # SecurityKey
    "0022400835693803056fffff0"          # (cont.)
    "0026402f2e7e02687a0bd3017e0042010177000bf209f107020040e700acbe54072009f107"
    "00000115020101210201005e0129"       # NAS-PDU (embedded)
)


def build_ng_setup_response(
    amf_name: str = "open5gs-amf0",
    mcc: str = "901",
    mnc: str = "70",
    amf_region_id: int = 0x02,
    amf_set_id: int = 0x0040,
    amf_pointer: int = 0x00,
    relative_capacity: int = 255,
) -> bytes:
    """Build an NGSetupResponse NGAP PDU.

    Uses a verified mock template from asn1c with PLMN substitution.
    Note: amf_name parameter is ignored (template uses 'open5gs-amf0').
    """
    template = bytearray.fromhex(_MOCK_NG_SETUP_RESPONSE_HEX)
    new_plmn = encode_plmn(mcc, mnc)
    for offset in _PLMN_OFFSETS_IN_NG_SETUP_RESP:
        template[offset : offset + 3] = new_plmn
    return bytes(template)


def build_downlink_nas_transport(
    amf_ue_ngap_id: int,
    ran_ue_ngap_id: int,
    nas_pdu: bytes,
) -> bytes:
    """Build a DownlinkNASTransport NGAP PDU."""
    ies = [
        NgapIE(IE_AMF_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer40(amf_ue_ngap_id)),
        NgapIE(IE_RAN_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer32(ran_ue_ngap_id)),
        NgapIE(IE_NAS_PDU, CRIT_REJECT, _encode_octet_string(nas_pdu)),
    ]
    return encode_ngap_pdu(NgapPdu(
        pdu_type=PDU_INITIATING_MESSAGE,
        procedure_code=PROC_DOWNLINK_NAS_TRANSPORT,
        criticality=CRIT_IGNORE,
        ies=ies,
    ))


def build_initial_context_setup_request(
    amf_ue_ngap_id: int,
    ran_ue_ngap_id: int,
    nas_pdu: bytes,
    mcc: str = "286",
    mnc: str = "01",
    security_key: bytes = b'\x00' * 32,
) -> bytes:
    """Build an InitialContextSetupRequest NGAP PDU.

    Contains the mandatory IEs (GUAMI, AllowedNSSAI, UESecurityCapabilities,
    SecurityKey) plus the NAS PDU needed to establish the UE context in the
    gNB. UEAggregateMaximumBitRate is omitted: it is CONDITIONAL only on
    PDUSessionResourceSetupListCxtReq being present (which this harness
    never sends here), and NgapTask::receiveInitialContextSetup has its
    read of that IE commented out regardless (src/gnb/ngap/context.cpp) --
    it unconditionally pushes UE_CONTEXT_UPDATE to GTP with whatever AMBR
    the UE context already has.

    GUAMI/AllowedNSSAI/UESecurityCapabilities/SecurityKey are built via
    asn1tools + the trimmed ngap-pdu-session-setup.asn1 module (see that
    module's docstring and _get_ngap_leaf_asn1 above) rather than hand-
    rolled bytes: an earlier hand-rolled version of this function produced
    bytes the real gNB's asn1c decoder rejected outright (APER decoding
    failed), the same class of bug fixed for AMF/RAN-UE-NGAP-ID above.
    """
    asn1 = _get_ngap_leaf_asn1()
    plmn = encode_plmn(mcc, mnc)

    guami_enc = asn1.encode("GUAMI", {
        "pLMNIdentity": plmn,
        "aMFRegionID": (bytes([2]), 8),
        "aMFSetID": (bytes([0x40, 0x00]), 10),
        "aMFPointer": (bytes([0x00]), 6),
    })

    allowed_nssai_enc = asn1.encode("AllowedNSSAI", [{"s-NSSAI": {"sST": bytes([1])}}])

    ue_sec_cap_enc = asn1.encode("UESecurityCapabilities", {
        "nRencryptionAlgorithms": (bytes([0xf0, 0x00]), 16),
        "nRintegrityProtectionAlgorithms": (bytes([0xf0, 0x00]), 16),
        "eUTRAencryptionAlgorithms": (bytes([0xf0, 0x00]), 16),
        "eUTRAintegrityProtectionAlgorithms": (bytes([0xf0, 0x00]), 16),
    })

    sec_key_enc = asn1.encode("SecurityKey", (security_key, 256))

    ies = [
        NgapIE(IE_AMF_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer40(amf_ue_ngap_id)),
        NgapIE(IE_RAN_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer32(ran_ue_ngap_id)),
        NgapIE(IE_GUAMI, CRIT_REJECT, guami_enc),
        NgapIE(IE_ALLOWED_NSSAI, CRIT_REJECT, allowed_nssai_enc),
        NgapIE(IE_NAS_PDU, CRIT_IGNORE, _encode_octet_string(nas_pdu)),
        NgapIE(IE_UE_SECURITY_CAPABILITIES, CRIT_REJECT, ue_sec_cap_enc),
        NgapIE(IE_SECURITY_KEY, CRIT_REJECT, sec_key_enc),
    ]
    return encode_ngap_pdu(NgapPdu(
        pdu_type=PDU_INITIATING_MESSAGE,
        procedure_code=PROC_INITIAL_CONTEXT_SETUP,
        criticality=CRIT_REJECT,
        ies=ies,
    ))


def build_handover_command(
    amf_ue_ngap_id: int,
    ran_ue_ngap_id: int,
    handover_type: int = 0,  # 0 = intra5gs
    target_to_source_container: bytes = b'\x00',
) -> bytes:
    """Build a HandoverCommand (successfulOutcome of HandoverPreparation).

    Sent by AMF to source gNB to execute the handover.
    """
    # HandoverType: ENUMERATED {intra5gs, ...} — single byte
    ho_type_enc = bytes([handover_type << 5])  # enum in APER: value in top bits

    ies = [
        NgapIE(IE_AMF_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer40(amf_ue_ngap_id)),
        NgapIE(IE_RAN_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer32(ran_ue_ngap_id)),
        NgapIE(IE_HANDOVER_TYPE, CRIT_REJECT, ho_type_enc),
        NgapIE(IE_TARGET_TO_SOURCE_CONTAINER, CRIT_REJECT,
               _encode_octet_string(target_to_source_container)),
    ]
    return encode_ngap_pdu(NgapPdu(
        pdu_type=PDU_SUCCESSFUL_OUTCOME,
        procedure_code=PROC_HANDOVER_PREPARATION,
        criticality=CRIT_REJECT,
        ies=ies,
    ))


_PDU_SESSION_ASN1_PATH = (
    Path(__file__).resolve().parent.parent
    / "data" / "asn1_specs" / "ngap-pdu-session-setup.asn1"
)
_pdu_session_asn1 = None


def _get_ngap_leaf_asn1():
    """Compile (once) the purpose-built ASN.1 module for the "leaf" IE
    values used by build_pdu_session_resource_setup_request() and
    build_initial_context_setup_request() (S-NSSAI, GUAMI, security
    capabilities, QoS structures, ...). See the module docstring for why
    this is a trimmed module rather than the real spec."""
    global _pdu_session_asn1
    if _pdu_session_asn1 is None:
        import asn1tools
        _pdu_session_asn1 = asn1tools.compile_files(str(_PDU_SESSION_ASN1_PATH), "per")
    return _pdu_session_asn1


def build_pdu_session_resource_setup_request(
    amf_ue_ngap_id: int,
    ran_ue_ngap_id: int,
    psi: int,
    upf_ip: str,
    upf_teid: int,
    sst: int = 1,
    five_qi: int = 9,
) -> bytes:
    """Build a PDUSessionResourceSetupRequest (TS 38.413 §9.2.1.4/§9.3.4.2).

    Establishes a single PDU session with one non-GBR QoS flow and a dummy
    UPF-side DL tunnel endpoint, with no NAS-PDU IE. Deliberately omitting
    the NAS-PDU IE routes the real gNB straight to
    NgapTask::deliverPDUSessionSetupRequest() (src/gnb/ngap/session.cpp)
    instead of requiring a real 5GSM PDU Session Establishment Accept: the
    gNB creates the GTP session and adds the PSI to the UE's NGAP context
    synchronously either way, with no NAS content inspected and no RRC
    round-trip to the UE required for the session to become visible to
    GtpTask::getPduSessions() — see gNB/xn/task.cpp's GetUeContexts().
    """
    asn1 = _get_ngap_leaf_asn1()

    transfer_value = _encode_ies([
        NgapIE(IE_UL_NGU_UP_TNL_INFORMATION, CRIT_REJECT, asn1.encode(
            "UPTransportLayerInformation",
            ("gTPTunnel", {
                "transportLayerAddress": (socket.inet_aton(upf_ip), 32),
                "gTP-TEID": upf_teid.to_bytes(4, "big"),
            }),
        )),
        NgapIE(IE_PDU_SESSION_TYPE, CRIT_REJECT, asn1.encode("PDUSessionType", "ipv4")),
        NgapIE(IE_QOS_FLOW_SETUP_REQUEST_LIST, CRIT_REJECT, asn1.encode(
            "QosFlowSetupRequestList",
            [{
                "qosFlowIdentifier": 1,
                "qosFlowLevelQosParameters": {
                    "qosCharacteristics": ("nonDynamic5QI", {"fiveQI": five_qi}),
                    "allocationAndRetentionPriority": {
                        "priorityLevelARP": 8,
                        "pre-emptionCapability": "shall-not-trigger-pre-emption",
                        "pre-emptionVulnerability": "not-pre-emptable",
                    },
                },
            }],
        )),
    ])

    session_list = asn1.encode("PDUSessionResourceSetupListSUReq", [{
        "pDUSessionID": psi,
        "s-NSSAI": {"sST": bytes([sst])},
        "pDUSessionResourceSetupRequestTransfer": transfer_value,
    }])

    ies = [
        NgapIE(IE_AMF_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer40(amf_ue_ngap_id)),
        NgapIE(IE_RAN_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer32(ran_ue_ngap_id)),
        NgapIE(IE_PDU_SESSION_RESOURCE_SETUP_LIST_SU_REQ, CRIT_REJECT, session_list),
    ]
    return encode_ngap_pdu(NgapPdu(
        pdu_type=PDU_INITIATING_MESSAGE,
        procedure_code=PROC_PDU_SESSION_RESOURCE_SETUP,
        criticality=CRIT_REJECT,
        ies=ies,
    ))


def build_handover_request(
    amf_ue_ngap_id: int,
    handover_type: int = 0,  # 0 = intra5gs
    source_to_target_container: bytes = b"\x00",
) -> bytes:
    """Build a HandoverRequest (initiatingMessage of HandoverPreparation).

    This minimal variant intentionally omits the
    PDUSessionResourceSetupListHOReq IE so target gNB can exercise the
    HandoverFailure path.
    """
    # HandoverType: ENUMERATED {intra5gs, ...} — single byte
    ho_type_enc = bytes([handover_type << 5])

    ies = [
        NgapIE(IE_AMF_UE_NGAP_ID, CRIT_REJECT, _encode_aper_integer40(amf_ue_ngap_id)),
        NgapIE(IE_HANDOVER_TYPE, CRIT_REJECT, ho_type_enc),
        NgapIE(IE_SOURCE_TO_TARGET_CONTAINER, CRIT_REJECT,
               _encode_octet_string(source_to_target_container)),
    ]
    return encode_ngap_pdu(NgapPdu(
        pdu_type=PDU_INITIATING_MESSAGE,
        procedure_code=PROC_HANDOVER_PREPARATION,
        criticality=CRIT_REJECT,
        ies=ies,
    ))


def build_path_switch_request_ack(
    amf_ue_ngap_id: int,
    ran_ue_ngap_id: int,
) -> bytes:
    """Build a PathSwitchRequestAcknowledge (successfulOutcome)."""
    # UESecurityCapabilities: placeholder
    ue_sec_cap = b'\xf0\x00\xf0\x00\xf0\x00\xf0\x00'

    # SecurityContext: NextHopNH(256 bits) + NextHopChainingCount(3 bits)
    sec_ctx = b'\x00' * 32 + b'\x00'  # 32-byte NH + 1-byte NCC

    ies = [
        NgapIE(IE_AMF_UE_NGAP_ID, CRIT_IGNORE, _encode_aper_integer40(amf_ue_ngap_id)),
        NgapIE(IE_RAN_UE_NGAP_ID, CRIT_IGNORE, _encode_aper_integer32(ran_ue_ngap_id)),
        NgapIE(IE_UE_SECURITY_CAPABILITIES, CRIT_REJECT, ue_sec_cap),
    ]
    return encode_ngap_pdu(NgapPdu(
        pdu_type=PDU_SUCCESSFUL_OUTCOME,
        procedure_code=PROC_PATH_SWITCH_REQUEST,
        criticality=CRIT_REJECT,
        ies=ies,
    ))


def build_handover_preparation_failure(
    amf_ue_ngap_id: int,
    ran_ue_ngap_id: int,
    cause_group: int = 0,   # 0 = radioNetwork
    cause_value: int = 0,   # 0 = unspecified
) -> bytes:
    """Build a HandoverPreparationFailure (unsuccessfulOutcome)."""
    # Cause: CHOICE + enum value.  Simplified encoding.
    cause_enc = bytes([cause_group << 5, cause_value << 1])

    ies = [
        NgapIE(IE_AMF_UE_NGAP_ID, CRIT_IGNORE, _encode_aper_integer40(amf_ue_ngap_id)),
        NgapIE(IE_RAN_UE_NGAP_ID, CRIT_IGNORE, _encode_aper_integer32(ran_ue_ngap_id)),
        NgapIE(IE_CAUSE, CRIT_IGNORE, cause_enc),
    ]
    return encode_ngap_pdu(NgapPdu(
        pdu_type=PDU_UNSUCCESSFUL_OUTCOME,
        procedure_code=PROC_HANDOVER_PREPARATION,
        criticality=CRIT_REJECT,
        ies=ies,
    ))


# ======================================================================
#  Extraction helpers (for parsing gNB → AMF messages)
# ======================================================================

def extract_ran_ue_ngap_id(pdu: NgapPdu) -> Optional[int]:
    """Extract RAN-UE-NGAP-ID from a decoded PDU.

    See _encode_aper_wide_constrained_integer's docstring for the wire
    format (a bit-packed octet-count prefix, not a plain fixed-width value
    or a "normally small length" byte).
    """
    ie = pdu.find_ie(IE_RAN_UE_NGAP_ID)
    if ie is None or len(ie.value) == 0:
        return None
    return _decode_aper_wide_constrained_integer(ie.value, 4)


def extract_amf_ue_ngap_id(pdu: NgapPdu) -> Optional[int]:
    """Extract AMF-UE-NGAP-ID from a decoded PDU (INTEGER(0..2^40-1), same
    encoding scheme as RAN-UE-NGAP-ID but with a 5-octet max width -- see
    extract_ran_ue_ngap_id / _encode_aper_wide_constrained_integer)."""
    ie = pdu.find_ie(IE_AMF_UE_NGAP_ID)
    if ie is None or len(ie.value) == 0:
        return None
    return _decode_aper_wide_constrained_integer(ie.value, 5)


def extract_nas_pdu(pdu: NgapPdu) -> Optional[bytes]:
    """Extract the NAS-PDU from a decoded PDU (unconstrained OCTET STRING)."""
    ie = pdu.find_ie(IE_NAS_PDU)
    if ie is None:
        return None
    # Unconstrained OCTET STRING: length-det + bytes
    if len(ie.value) < 1:
        return None
    length, offset = _decode_length(ie.value, 0)
    return ie.value[offset : offset + length]
