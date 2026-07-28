"""
Milenage algorithm (3GPP TS 35.206) and 5G-AKA key derivation (TS 33.501).

Provides:
  - Milenage f1..f5  (authentication functions)
  - 5G-AKA key hierarchy:  KAUSF → KSEAF → KAMF → KNASint / KNASenc
  - NAS integrity (128-NIA2 / AES-CMAC) and ciphering (128-NEA2 / AES-CTR)
"""

from __future__ import annotations

import hashlib
import hmac
import os
import struct
from typing import Tuple

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.cmac import CMAC
from cryptography.hazmat.primitives.ciphers.algorithms import AES


# ======================================================================
#  Low-level AES helpers
# ======================================================================

def _aes128_ecb(key: bytes, block: bytes) -> bytes:
    """AES-128-ECB encrypt a single 16-byte block."""
    assert len(key) == 16 and len(block) == 16
    cipher = Cipher(algorithms.AES(key), modes.ECB())
    enc = cipher.encryptor()
    return enc.update(block) + enc.finalize()


def _xor(a: bytes, b: bytes) -> bytes:
    """XOR two equal-length byte strings."""
    return bytes(x ^ y for x, y in zip(a, b))


def _rotate_left_bytes(data: bytes, n_bits: int) -> bytes:
    """Circular left rotation of a 128-bit block by *n_bits* (multiple of 8)."""
    assert n_bits % 8 == 0
    n = (n_bits // 8) % 16
    return data[n:] + data[:n]


# ======================================================================
#  Milenage constants (TS 35.206 §4.1)
# ======================================================================

_C1 = b"\x00" * 16
_C2 = b"\x00" * 15 + b"\x01"
_C3 = b"\x00" * 15 + b"\x02"
_C4 = b"\x00" * 15 + b"\x04"
_C5 = b"\x00" * 15 + b"\x08"

_R1 = 64   # bits
_R2 = 0
_R3 = 32
_R4 = 64
_R5 = 96


# ======================================================================
#  Milenage functions
# ======================================================================

def compute_opc(k: bytes, op: bytes) -> bytes:
    """OPc = E_K(OP) ⊕ OP."""
    return _xor(_aes128_ecb(k, op), op)


def _milenage_temp(k: bytes, opc: bytes, rand: bytes) -> bytes:
    """temp = E_K(RAND ⊕ OPc)."""
    return _aes128_ecb(k, _xor(rand, opc))


def f1(k: bytes, opc: bytes, rand: bytes, sqn: bytes, amf: bytes) -> bytes:
    """f1 → MAC-A (8 bytes).

    Used to build AUTN.
    """
    temp = _milenage_temp(k, opc, rand)
    # IN1 = SQN(48) || AMF(16) || SQN(48) || AMF(16) = 16 bytes
    in1 = sqn + amf + sqn + amf
    inner = _xor(_rotate_left_bytes(_xor(in1, opc), _R1), _C1)
    out1 = _xor(_aes128_ecb(k, _xor(temp, inner)), opc)
    return out1[:8]


def f2345(k: bytes, opc: bytes, rand: bytes) -> Tuple[bytes, bytes, bytes, bytes]:
    """f2 → RES (8 B), f3 → CK (16 B), f4 → IK (16 B), f5 → AK (6 B)."""
    temp = _milenage_temp(k, opc, rand)

    out2 = _xor(_aes128_ecb(k, _xor(_rotate_left_bytes(_xor(temp, opc), _R2), _C2)), opc)
    out3 = _xor(_aes128_ecb(k, _xor(_rotate_left_bytes(_xor(temp, opc), _R3), _C3)), opc)
    out4 = _xor(_aes128_ecb(k, _xor(_rotate_left_bytes(_xor(temp, opc), _R4), _C4)), opc)
    out5 = _xor(_aes128_ecb(k, _xor(_rotate_left_bytes(_xor(temp, opc), _R5), _C5)), opc)

    res = out2[8:16]   # 8 bytes  (f2)
    ck = out3           # 16 bytes (f3)
    ik = out4           # 16 bytes (f4)
    ak = out2[:6]       # 6 bytes  (f5 — shares out2 with f2)
    return res, ck, ik, ak


def generate_auth_vector(
    k: bytes,
    op_or_opc: bytes,
    is_opc: bool,
    sqn: bytes = b"\x00\x00\x00\x00\x00\x00",
    amf: bytes = b"\x80\x00",
    rand: bytes | None = None,
) -> dict:
    """Generate a full 5G authentication vector.

    Returns a dict with keys: rand, autn, xres, ck, ik, ak, mac_a, opc.
    """
    if rand is None:
        rand = os.urandom(16)

    opc = op_or_opc if is_opc else compute_opc(k, op_or_opc)
    res, ck, ik, ak = f2345(k, opc, rand)
    mac_a = f1(k, opc, rand, sqn, amf)

    # AUTN = (SQN ⊕ AK) || AMF || MAC-A
    sqn_xor_ak = _xor(sqn, ak)
    autn = sqn_xor_ak + amf + mac_a

    return dict(
        rand=rand, autn=autn, xres=res, ck=ck, ik=ik, ak=ak,
        mac_a=mac_a, opc=opc, sqn=sqn, amf=amf,
    )


# ======================================================================
#  5G-AKA key derivation  (TS 33.501 Annex A)
# ======================================================================

def _kdf(key: bytes, fc: int, *params: bytes) -> bytes:
    """HMAC-SHA-256 based KDF.  S = FC || P0 || L0 || P1 || L1 || …"""
    s = struct.pack("!B", fc)
    for p in params:
        s += p + struct.pack("!H", len(p))
    return hmac.new(key, s, hashlib.sha256).digest()


def derive_kausf(ck: bytes, ik: bytes, sn_name: bytes, sqn_xor_ak: bytes) -> bytes:
    """KAUSF = KDF(CK||IK, 0x6A, SN_name, SQN⊕AK)."""
    return _kdf(ck + ik, 0x6A, sn_name, sqn_xor_ak)


def derive_kseaf(kausf: bytes, sn_name: bytes) -> bytes:
    return _kdf(kausf, 0x6C, sn_name)


def derive_kamf(kseaf: bytes, supi: bytes, abba: bytes) -> bytes:
    return _kdf(kseaf, 0x6D, supi, abba)


def derive_knas_int(kamf: bytes, algo_id: int = 2) -> bytes:
    """KNASint – algorithm type 0x02 (integrity)."""
    key = _kdf(kamf, 0x69, struct.pack("!B", 0x02), struct.pack("!B", algo_id))
    return key[16:]   # last 128 bits


def derive_knas_enc(kamf: bytes, algo_id: int = 2) -> bytes:
    """KNASenc – algorithm type 0x01 (ciphering)."""
    key = _kdf(kamf, 0x69, struct.pack("!B", 0x01), struct.pack("!B", algo_id))
    return key[16:]


def derive_res_star(ck: bytes, ik: bytes, sn_name: bytes, rand: bytes, res: bytes) -> bytes:
    """RES* = KDF(CK||IK, 0x6B, SN_name, RAND, RES) → last 16 bytes."""
    key = _kdf(ck + ik, 0x6B, sn_name, rand, res)
    return key[16:]


def serving_network_name(mcc: str, mnc: str) -> bytes:
    """Build the Serving Network Name string used in 5G-AKA."""
    mnc_padded = mnc.zfill(3)
    return f"5G:mnc{mnc_padded}.mcc{mcc}.3gppnetwork.org".encode("ascii")


# ======================================================================
#  5G NAS Security algorithms  (NIA2 / NEA2 — AES-based)
# ======================================================================

def _build_eia2_input(count: int, bearer: int, direction: int) -> bytes:
    """Build the 8-byte 'initial block' for 128-EIA2.

    Layout (64 bits): COUNT(32) || BEARER(5) || DIRECTION(1) || 0…0(26)
    """
    first32 = count
    second32 = (bearer << 27) | (direction << 26)
    return struct.pack("!II", first32, second32)


def nas_integrity_nia2(
    key: bytes,
    count: int,
    bearer: int,
    direction: int,
    message: bytes,
) -> bytes:
    """Compute 32-bit NAS MAC using 128-NIA2 (AES-CMAC).

    *message* is the NAS PDU starting from the sequence number octet.
    """
    m = _build_eia2_input(count, bearer, direction) + message
    c = CMAC(AES(key))
    c.update(m)
    mac = c.finalize()
    return mac[:4]  # leftmost 32 bits


def nas_encrypt_nea2(
    key: bytes,
    count: int,
    bearer: int,
    direction: int,
    plaintext: bytes,
) -> bytes:
    """Encrypt (or decrypt) a NAS PDU using 128-NEA2 (AES-CTR).

    Counter block: COUNT(32) || BEARER(5) || DIRECTION(1) || 0…0(26) || 0(64)
    """
    iv_upper = _build_eia2_input(count, bearer, direction)
    iv = iv_upper + b"\x00\x00\x00\x00\x00\x00\x00\x00"   # 16 bytes total
    cipher = Cipher(algorithms.AES(key), modes.CTR(iv))
    enc = cipher.encryptor()
    return enc.update(plaintext) + enc.finalize()


def derive_full_key_set(
    k: bytes,
    op_or_opc: bytes,
    is_opc: bool,
    mcc: str,
    mnc: str,
    supi: str,
    sqn: bytes = b"\x00\x00\x00\x00\x00\x20",  # seq=1, ind=0 (indBitLen=5)
    amf_val: bytes = b"\x80\x00",
    rand: bytes | None = None,
    abba: bytes = b"\x00\x00",
) -> dict:
    """One-shot helper: generate auth vector + derive all NAS keys.

    Returns a dict with: rand, autn, xres, res_star, kausf, kseaf, kamf,
    k_nas_int, k_nas_enc, opc, ak, sqn, sn_name, ck, ik.
    """
    av = generate_auth_vector(k, op_or_opc, is_opc, sqn, amf_val, rand)
    sn_name = serving_network_name(mcc, mnc)

    sqn_xor_ak = _xor(av["sqn"], av["ak"])
    kausf = derive_kausf(av["ck"], av["ik"], sn_name, sqn_xor_ak)
    kseaf = derive_kseaf(kausf, sn_name)
    # The UERANSIM C++ Supi::Parse strips the "imsi-" prefix, so the KDF
    # input for KAMF uses only the numeric IMSI value.
    supi_value = supi.split("-", 1)[-1] if "-" in supi else supi
    kamf = derive_kamf(kseaf, supi_value.encode("utf-8"), abba)
    k_nas_int = derive_knas_int(kamf, algo_id=2)  # NIA2
    k_nas_enc = derive_knas_enc(kamf, algo_id=2)  # NEA2
    res_star = derive_res_star(av["ck"], av["ik"], sn_name, av["rand"], av["xres"])

    return dict(
        rand=av["rand"],
        autn=av["autn"],
        xres=av["xres"],
        res_star=res_star,
        sn_name=sn_name,
        kausf=kausf,
        kseaf=kseaf,
        kamf=kamf,
        k_nas_int=k_nas_int,
        k_nas_enc=k_nas_enc,
        opc=av["opc"],
        ak=av["ak"],
        sqn=av["sqn"],
        ck=av["ck"],
        ik=av["ik"],
    )
