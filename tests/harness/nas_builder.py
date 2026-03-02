"""
Minimal 5G NAS (TS 24.501) message builder / parser.

Builds only the specific NAS messages needed for UE test flows:
  - AuthenticationRequest   (DL, 0x56)
  - SecurityModeCommand     (DL, 0x5D)
  - RegistrationAccept      (DL, 0x42)
  - ConfigurationUpdateCommand (DL, 0x54)

Parses:
  - RegistrationRequest     (UL, 0x41)
  - AuthenticationResponse  (UL, 0x57)
  - SecurityModeComplete    (UL, 0x5E)
  - DeRegistrationRequestUeOriginating (UL, 0x45)
  - ServiceRequest          (UL, 0x4C)
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional

from . import milenage


# ======================================================================
#  Constants
# ======================================================================

# Extended Protocol Discriminator
EPD_5GMM = 0x7E
EPD_5GSM = 0x2E

# Security Header Types
SEC_NONE = 0x00
SEC_INTEGRITY = 0x01
SEC_INTEGRITY_CIPHERED = 0x02
SEC_INTEGRITY_NEW_CTX = 0x03
SEC_INTEGRITY_CIPHERED_NEW_CTX = 0x04

# 5GMM Message Types
MT_REGISTRATION_REQUEST = 0x41
MT_REGISTRATION_ACCEPT = 0x42
MT_REGISTRATION_COMPLETE = 0x43
MT_REGISTRATION_REJECT = 0x44
MT_DEREGISTRATION_REQUEST_UE_ORIG = 0x45
MT_DEREGISTRATION_ACCEPT_UE_ORIG = 0x46
MT_DEREGISTRATION_REQUEST_UE_TERM = 0x47
MT_AUTHENTICATION_REQUEST = 0x56
MT_AUTHENTICATION_RESPONSE = 0x57
MT_AUTHENTICATION_REJECT = 0x58
MT_AUTHENTICATION_FAILURE = 0x59
MT_AUTHENTICATION_RESULT = 0x5A
MT_IDENTITY_REQUEST = 0x5B
MT_IDENTITY_RESPONSE = 0x5C
MT_SECURITY_MODE_COMMAND = 0x5D
MT_SECURITY_MODE_COMPLETE = 0x5E
MT_SECURITY_MODE_REJECT = 0x5F
MT_SERVICE_REQUEST = 0x4C
MT_SERVICE_ACCEPT = 0x4E
MT_SERVICE_REJECT = 0x4F
MT_CONFIGURATION_UPDATE_COMMAND = 0x54
MT_CONFIGURATION_UPDATE_COMPLETE = 0x55
MT_5GMM_STATUS = 0x64
MT_DL_NAS_TRANSPORT = 0x68
MT_UL_NAS_TRANSPORT = 0x67

# NAS security algorithm identifiers
class NasIntAlgo(IntEnum):
    IA0 = 0   # null
    IA1 = 1   # 128-5G-IA1 (SNOW 3G)
    IA2 = 2   # 128-5G-IA2 (AES)
    IA3 = 3   # 128-5G-IA3 (ZUC)

class NasCiphAlgo(IntEnum):
    EA0 = 0   # null
    EA1 = 1
    EA2 = 2
    EA3 = 3

# NAS bearer
NAS_BEARER = 0x01


# ======================================================================
#  NAS Security context
# ======================================================================

@dataclass
class NasSecurityContext:
    """Tracks NAS security keys and counters for a session."""
    k_nas_int: bytes = b""
    k_nas_enc: bytes = b""
    int_algo: NasIntAlgo = NasIntAlgo.IA2
    ciph_algo: NasCiphAlgo = NasCiphAlgo.EA2
    dl_count: int = 0   # 32-bit DL NAS COUNT
    ul_count: int = 0   # 32-bit UL NAS COUNT

    def integrity_protect(self, plain_nas: bytes, direction: int = 0) -> bytes:
        """Wrap *plain_nas* in an integrity-protected NAS container.

        Parameters
        ----------
        direction : 0 = downlink (gNB→UE), 1 = uplink (UE→gNB).
            Note: 3GPP MAC direction bit is the *inverse* of this convention
            (3GPP: 0=UL, 1=DL), so we flip before passing to NIA2.

        Returns the full protected NAS message:
          EPD(1) | SecHeader(1) | MAC(4) | SQN(1) | plain_nas(N)
        """
        count = self.dl_count if direction == 0 else self.ul_count
        sqn = count & 0xFF
        # 3GPP direction bit: 0=uplink, 1=downlink (opposite of our parameter)
        mac_dir = 1 - direction

        # The message fed to the integrity algorithm starts at SQN octet
        m = struct.pack("!B", sqn) + plain_nas
        mac = milenage.nas_integrity_nia2(
            self.k_nas_int, count, NAS_BEARER, mac_dir, m
        )

        header = struct.pack("!BB", EPD_5GMM, SEC_INTEGRITY_NEW_CTX)
        result = header + mac + m

        if direction == 0:
            self.dl_count += 1
        return result

    def integrity_protect_and_cipher(self, plain_nas: bytes, direction: int = 0) -> bytes:
        """Wrap *plain_nas* with integrity + ciphering.

        Parameters
        ----------
        direction : 0 = downlink, 1 = uplink (same convention as integrity_protect).
        """
        count = self.dl_count if direction == 0 else self.ul_count
        sqn = count & 0xFF
        mac_dir = 1 - direction

        # Cipher the NAS message (not the SQN)
        ciphered = milenage.nas_encrypt_nea2(
            self.k_nas_enc, count, NAS_BEARER, mac_dir, plain_nas
        )

        m = struct.pack("!B", sqn) + ciphered
        mac = milenage.nas_integrity_nia2(
            self.k_nas_int, count, NAS_BEARER, mac_dir, m
        )

        header = struct.pack("!BB", EPD_5GMM, SEC_INTEGRITY_CIPHERED)
        result = header + mac + m

        if direction == 0:
            self.dl_count += 1
        return result


# ======================================================================
#  Message builders  (downlink — gNB → UE)
# ======================================================================

def build_authentication_request(
    rand: bytes,
    autn: bytes,
    ngksi: int = 0,
    abba: bytes = b"\x00\x00",
) -> bytes:
    """Plain (unprotected) AuthenticationRequest (0x56).

    5G-AKA variant.
    """
    buf = bytearray()
    buf += struct.pack("!BB", EPD_5GMM, SEC_NONE)
    buf += struct.pack("!B", MT_AUTHENTICATION_REQUEST)

    # ngKSI (upper 4 bits) | spare (lower 4 bits)
    buf += struct.pack("!B", (ngksi & 0x07) << 4)

    # ABBA: LV
    buf += struct.pack("!B", len(abba))
    buf += abba

    # Authentication parameter RAND — type TV, IEI=0x21, fixed 16 bytes
    buf += struct.pack("!B", 0x21)
    buf += rand

    # Authentication parameter AUTN — type TLV, IEI=0x20
    buf += struct.pack("!B", 0x20)
    buf += struct.pack("!B", len(autn))
    buf += autn

    return bytes(buf)


def build_security_mode_command(
    int_algo: NasIntAlgo = NasIntAlgo.IA2,
    ciph_algo: NasCiphAlgo = NasCiphAlgo.EA2,
    ngksi: int = 0,
    ue_security_capabilities: bytes | None = None,
    imeisv_request: bool = False,
) -> bytes:
    """Plain SecurityModeCommand (0x5D) — caller must integrity-protect.

    Parameters
    ----------
    ue_security_capabilities : replayed UE security capabilities (LV).
        If None, a default indicating support for EA1-3 / IA1-3 is used.
    """
    buf = bytearray()
    buf += struct.pack("!BB", EPD_5GMM, SEC_NONE)
    buf += struct.pack("!B", MT_SECURITY_MODE_COMMAND)

    # Selected NAS security algorithms  (1 byte)
    #   upper nibble = type of integrity (0=IA0 .. 3=IA3)
    #   lower nibble = type of ciphering (0=EA0 .. 3=EA3)
    buf += struct.pack("!B", (int(int_algo) << 4) | int(ciph_algo))

    # ngKSI | spare
    buf += struct.pack("!B", (ngksi & 0x07) << 4)

    # Replayed UE security capabilities (LV)
    if ue_security_capabilities is None:
        # Default: must match what the UE sends in RegistrationRequest.
        # 5G-EA: EA0(always 1)+EA1+EA2+EA3 = 0xF0
        # 5G-IA: IA0(always 1)+IA1+IA2+IA3 = 0xF0
        # EEA:   EEA0(always 1)+EEA1+EEA2+EEA3 = 0xF0
        # EIA:   EIA0(always 1)+EIA1+EIA2+EIA3 = 0xF0
        ue_security_capabilities = bytes([0xF0, 0xF0, 0xF0, 0xF0])
    buf += struct.pack("!B", len(ue_security_capabilities))
    buf += ue_security_capabilities

    # Optional IMEISV request (tag 0xE-)
    if imeisv_request:
        buf += struct.pack("!B", 0xE1)   # IEI=0xE with value 0x01

    return bytes(buf)


def build_registration_accept(
    reg_result: int = 0x09,
    guti: bytes | None = None,
    tai_list: bytes | None = None,
    nssai: bytes | None = None,
) -> bytes:
    """Plain RegistrationAccept (0x42).

    Parameters
    ----------
    reg_result : 5GS registration result value (0x09 = 3GPP + allowed).
    guti : 5G-GUTI as TLV (with IEI 0x77).
    """
    buf = bytearray()
    buf += struct.pack("!BB", EPD_5GMM, SEC_NONE)
    buf += struct.pack("!B", MT_REGISTRATION_ACCEPT)

    # 5GS registration result (LV — length 1)
    buf += struct.pack("!B", 0x01)
    buf += struct.pack("!B", reg_result)

    # Optional: 5G-GUTI  (IEI = 0x77, TLV-E)
    if guti is not None:
        buf += b"\x77"
        buf += struct.pack("!H", len(guti))
        buf += guti

    # Optional: TAI list  (IEI = 0x54, TLV)
    if tai_list is not None:
        buf += b"\x54"
        buf += struct.pack("!B", len(tai_list))
        buf += tai_list

    # Optional: Allowed NSSAI (IEI = 0x15, TLV)
    if nssai is not None:
        buf += b"\x15"
        buf += struct.pack("!B", len(nssai))
        buf += nssai

    return bytes(buf)


def build_configuration_update_command() -> bytes:
    """Minimal ConfigurationUpdateCommand (0x54) — no optional IEs."""
    buf = bytearray()
    buf += struct.pack("!BB", EPD_5GMM, SEC_NONE)
    buf += struct.pack("!B", MT_CONFIGURATION_UPDATE_COMMAND)
    return bytes(buf)


def build_deregistration_accept_ue_orig() -> bytes:
    """DeRegistrationAcceptUeOriginating (0x46) — no optional IEs."""
    buf = bytearray()
    buf += struct.pack("!BB", EPD_5GMM, SEC_NONE)
    buf += struct.pack("!B", MT_DEREGISTRATION_ACCEPT_UE_ORIG)
    return bytes(buf)


def build_5g_guti(
    mcc: str = "286",
    mnc: str = "93",
    amf_region_id: int = 0x01,
    amf_set_id: int = 0x01,
    amf_pointer: int = 0x00,
    tmsi: int = 0x00000001,
) -> bytes:
    """Build a 5G-GUTI identity value (without IEI/length)."""
    buf = bytearray()
    # Identity type = GUTI (0x02), odd/even (0)
    # First octet: type of identity (3 bits) = 010, odd/even = 0, supi format = 000
    # Actually the 5G-GUTI mobile identity is:
    # Octet 1 lower 3 bits = type (0x02), upper bits = odd/even indicator
    buf += struct.pack("!B", 0xF2)  # 1111 | 0010  (type=GUTI, spare filler)

    # MCC/MNC encoding (BCD)
    d1, d2, d3 = int(mcc[0]), int(mcc[1]), int(mcc[2])
    if len(mnc) == 2:
        d4, d5, d6 = 0xF, int(mnc[0]), int(mnc[1])
    else:
        d4, d5, d6 = int(mnc[2]), int(mnc[0]), int(mnc[1])
    buf += struct.pack("!B", (d2 << 4) | d1)
    buf += struct.pack("!B", (d4 << 4) | d3)
    buf += struct.pack("!B", (d6 << 4) | d5)

    # AMF Region ID
    buf += struct.pack("!B", amf_region_id)
    # AMF Set ID (10 bits) + AMF Pointer (6 bits) = 2 bytes
    amf_set_pointer = (amf_set_id << 6) | (amf_pointer & 0x3F)
    buf += struct.pack("!H", amf_set_pointer)
    # 5G-TMSI
    buf += struct.pack("!I", tmsi)

    return bytes(buf)


# ======================================================================
#  Message parsers  (uplink — UE → gNB)
# ======================================================================

@dataclass
class ParsedNasMessage:
    epd: int = 0
    security_header: int = 0
    message_type: int = 0
    mac: bytes = b""
    sqn: int = 0
    payload: bytes = b""          # raw remaining bytes
    is_security_protected: bool = False


def parse_nas_header(data: bytes) -> ParsedNasMessage:
    """Parse the common NAS header and return a ParsedNasMessage.

    Works for both plain and security-protected messages.
    """
    msg = ParsedNasMessage()
    if len(data) < 3:
        return msg

    msg.epd = data[0]
    msg.security_header = data[1] & 0x0F

    if msg.security_header == SEC_NONE:
        msg.message_type = data[2]
        msg.payload = data[3:]
    else:
        # Security protected: MAC(4) + SQN(1) + inner NAS
        msg.is_security_protected = True
        if len(data) < 7:
            return msg
        msg.mac = data[2:6]
        msg.sqn = data[6]
        # The inner NAS message starts at offset 7
        if len(data) > 9:
            msg.message_type = data[9]   # inner: EPD(1) + secheader(1) + msgtype(1)
            msg.payload = data[10:]
        inner = data[7:]
        if len(inner) >= 3:
            msg.message_type = inner[2]
            msg.payload = inner[3:]

    return msg


def identify_nas_message(data: bytes) -> str:
    """Return a human-readable name for the NAS message."""
    msg = parse_nas_header(data)
    names = {
        MT_REGISTRATION_REQUEST: "RegistrationRequest",
        MT_REGISTRATION_ACCEPT: "RegistrationAccept",
        MT_REGISTRATION_COMPLETE: "RegistrationComplete",
        MT_REGISTRATION_REJECT: "RegistrationReject",
        MT_DEREGISTRATION_REQUEST_UE_ORIG: "DeRegistrationRequest",
        MT_DEREGISTRATION_ACCEPT_UE_ORIG: "DeRegistrationAccept",
        MT_AUTHENTICATION_REQUEST: "AuthenticationRequest",
        MT_AUTHENTICATION_RESPONSE: "AuthenticationResponse",
        MT_AUTHENTICATION_REJECT: "AuthenticationReject",
        MT_AUTHENTICATION_FAILURE: "AuthenticationFailure",
        MT_SECURITY_MODE_COMMAND: "SecurityModeCommand",
        MT_SECURITY_MODE_COMPLETE: "SecurityModeComplete",
        MT_SECURITY_MODE_REJECT: "SecurityModeReject",
        MT_SERVICE_REQUEST: "ServiceRequest",
        MT_SERVICE_ACCEPT: "ServiceAccept",
        MT_SERVICE_REJECT: "ServiceReject",
        MT_CONFIGURATION_UPDATE_COMMAND: "ConfigurationUpdateCommand",
        MT_IDENTITY_REQUEST: "IdentityRequest",
        MT_IDENTITY_RESPONSE: "IdentityResponse",
        MT_5GMM_STATUS: "5GmmStatus",
        MT_DL_NAS_TRANSPORT: "DlNasTransport",
        MT_UL_NAS_TRANSPORT: "UlNasTransport",
    }
    return names.get(msg.message_type, f"Unknown(0x{msg.message_type:02x})")
