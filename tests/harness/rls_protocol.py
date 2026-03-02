"""
RLS (Radio Link Simulation) binary protocol encoder / decoder.

Wire format (all fields big-endian):

  Offset  Size  Field
  ------  ----  -----
  0       1     0x03  (compat marker)
  1       1     major version (3)
  2       1     minor version (2)
  3       1     patch version (2)
  4       1     message type  (EMessageType)
  5       8     STI  – Session Temporary Identifier (uint64)
  13+     var   payload (depends on message type)

Payload per message type:

  HEARTBEAT (4):      simPos.x (4B int32), .y (4B), .z (4B)
  HEARTBEAT_ACK (5):  dbm (4B int32)
  PDU_TRANSMISSION (6):
      pduType (1B), pduId (4B uint32), payload (4B uint32),
      pduLen (4B uint32), pdu (pduLen bytes)
  PDU_TRANSMISSION_ACK (7):
      count (4B uint32), then count × pduId (4B each)
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from enum import IntEnum
from typing import List, Optional


# ---------------------------------------------------------------------------
#  Constants
# ---------------------------------------------------------------------------

RLS_COMPAT_MARKER = 0x03
RLS_VERSION = (3, 2, 7)
RLS_HEADER_FMT = "!BBBBB"          # marker, major, minor, patch, msgType
RLS_HEADER_SIZE = struct.calcsize(RLS_HEADER_FMT)  # 5
RLS_STI_FMT = "!Q"                 # uint64
RLS_STI_SIZE = struct.calcsize(RLS_STI_FMT)        # 8

# Portal port used by UERANSIM gNB RLS
PORTAL_PORT = 4997


class EMessageType(IntEnum):
    RESERVED = 0
    HEARTBEAT = 4
    HEARTBEAT_ACK = 5
    PDU_TRANSMISSION = 6
    PDU_TRANSMISSION_ACK = 7


class EPduType(IntEnum):
    RESERVED = 0
    RRC = 1
    DATA = 2


class RrcChannel(IntEnum):
    """Mirrors rrc::RrcChannel – numeric values must match the C++ enum."""
    BCCH_BCH = 0
    BCCH_DL_SCH = 1
    DL_CCCH = 2
    DL_DCCH = 3
    PCCH = 4
    UL_CCCH = 5
    UL_CCCH1 = 6
    UL_DCCH = 7


# ---------------------------------------------------------------------------
#  Data classes for decoded messages
# ---------------------------------------------------------------------------

@dataclass
class RlsHeartBeat:
    sti: int
    sim_pos: tuple  # (x, y, z) as ints


@dataclass
class RlsHeartBeatAck:
    sti: int
    dbm: int


@dataclass
class RlsPduTransmission:
    sti: int
    pdu_type: EPduType
    pdu_id: int
    payload: int         # RrcChannel for RRC PDUs, PSI for DATA
    pdu: bytes


@dataclass
class RlsPduTransmissionAck:
    sti: int
    pdu_ids: List[int]


RlsMessage = RlsHeartBeat | RlsHeartBeatAck | RlsPduTransmission | RlsPduTransmissionAck


# ---------------------------------------------------------------------------
#  Encoding helpers
# ---------------------------------------------------------------------------

def _encode_header(msg_type: EMessageType, sti: int) -> bytes:
    hdr = struct.pack(
        RLS_HEADER_FMT,
        RLS_COMPAT_MARKER,
        RLS_VERSION[0],
        RLS_VERSION[1],
        RLS_VERSION[2],
        int(msg_type),
    )
    return hdr + struct.pack(RLS_STI_FMT, sti)


def encode_heartbeat(sti: int, sim_pos: tuple = (0, 0, 0)) -> bytes:
    """Encode an RLS HeartBeat message."""
    return _encode_header(EMessageType.HEARTBEAT, sti) + struct.pack(
        "!iii", sim_pos[0], sim_pos[1], sim_pos[2]
    )


def encode_heartbeat_ack(sti: int, dbm: int) -> bytes:
    """Encode an RLS HeartBeatAck message."""
    return _encode_header(EMessageType.HEARTBEAT_ACK, sti) + struct.pack("!i", dbm)


def encode_pdu_transmission(
    sti: int,
    pdu_type: EPduType,
    pdu_id: int,
    payload: int,
    pdu: bytes,
) -> bytes:
    """Encode an RLS PduTransmission message.

    Parameters
    ----------
    sti : session temporary identifier
    pdu_type : RRC or DATA
    pdu_id : monotonically increasing PDU identifier
    payload : RrcChannel (for RRC) or PSI (for DATA)
    pdu : raw PDU bytes (ASN.1 encoded RRC or IP packet)
    """
    body = struct.pack("!BIII", int(pdu_type), pdu_id, payload, len(pdu))
    return _encode_header(EMessageType.PDU_TRANSMISSION, sti) + body + pdu


def encode_pdu_transmission_ack(sti: int, pdu_ids: List[int]) -> bytes:
    """Encode an RLS PduTransmissionAck message."""
    body = struct.pack("!I", len(pdu_ids))
    for pid in pdu_ids:
        body += struct.pack("!I", pid)
    return _encode_header(EMessageType.PDU_TRANSMISSION_ACK, sti) + body


# ---------------------------------------------------------------------------
#  Decoding
# ---------------------------------------------------------------------------

def decode_rls_message(data: bytes) -> Optional[RlsMessage]:
    """Decode a raw UDP datagram into an RLS message object.

    Returns None if the datagram is malformed or has an unknown type.
    """
    if len(data) < RLS_HEADER_SIZE + RLS_STI_SIZE:
        return None

    marker, major, minor, patch, msg_type_raw = struct.unpack_from(RLS_HEADER_FMT, data, 0)
    if marker != RLS_COMPAT_MARKER:
        return None

    sti = struct.unpack_from(RLS_STI_FMT, data, RLS_HEADER_SIZE)[0]
    offset = RLS_HEADER_SIZE + RLS_STI_SIZE          # 13

    try:
        msg_type = EMessageType(msg_type_raw)
    except ValueError:
        return None

    if msg_type == EMessageType.HEARTBEAT:
        if len(data) < offset + 12:
            return None
        x, y, z = struct.unpack_from("!iii", data, offset)
        return RlsHeartBeat(sti=sti, sim_pos=(x, y, z))

    if msg_type == EMessageType.HEARTBEAT_ACK:
        if len(data) < offset + 4:
            return None
        dbm = struct.unpack_from("!i", data, offset)[0]
        return RlsHeartBeatAck(sti=sti, dbm=dbm)

    if msg_type == EMessageType.PDU_TRANSMISSION:
        if len(data) < offset + 13:
            return None
        pdu_type_raw, pdu_id, payload, pdu_len = struct.unpack_from("!BIII", data, offset)
        offset += 13
        if len(data) < offset + pdu_len:
            return None
        pdu = data[offset : offset + pdu_len]
        try:
            pdu_type = EPduType(pdu_type_raw)
        except ValueError:
            pdu_type = EPduType.RESERVED
        return RlsPduTransmission(sti=sti, pdu_type=pdu_type, pdu_id=pdu_id, payload=payload, pdu=pdu)

    if msg_type == EMessageType.PDU_TRANSMISSION_ACK:
        if len(data) < offset + 4:
            return None
        count = struct.unpack_from("!I", data, offset)[0]
        offset += 4
        pdu_ids = []
        for _ in range(count):
            if len(data) < offset + 4:
                break
            pdu_ids.append(struct.unpack_from("!I", data, offset)[0])
            offset += 4
        return RlsPduTransmissionAck(sti=sti, pdu_ids=pdu_ids)

    return None


# ---------------------------------------------------------------------------
#  Convenience helpers
# ---------------------------------------------------------------------------

def is_rrc_channel(pdu_tx: RlsPduTransmission, channel: RrcChannel) -> bool:
    """Check if a PDU_TRANSMISSION carries an RRC PDU on the given channel."""
    return pdu_tx.pdu_type == EPduType.RRC and pdu_tx.payload == int(channel)


def is_ul_ccch(pdu_tx: RlsPduTransmission) -> bool:
    return is_rrc_channel(pdu_tx, RrcChannel.UL_CCCH)


def is_ul_dcch(pdu_tx: RlsPduTransmission) -> bool:
    return is_rrc_channel(pdu_tx, RrcChannel.UL_DCCH)
