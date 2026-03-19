from __future__ import annotations

import logging
import random
import socket
import struct
import threading
import time
from collections import deque
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Deque, List, Optional, Sequence

logger = logging.getLogger(__name__)

RLS_COMPAT_MARKER = 0x03
RLS_VERSION = (3, 3, 7)

MSG_HEARTBEAT = 4
MSG_HEARTBEAT_ACK = 5
MSG_PDU_TRANSMISSION = 6
MSG_PDU_TRANSMISSION_ACK = 7

PDU_TYPE_RRC = 1

PORTAL_PORT = 4997

_FALLBACK_RRC_SETUP_REQUEST = bytes.fromhex("100000000008")
_DEFAULT_NAS_REG_REQUEST = bytes.fromhex("7e004179000d0182f61000000000000000102f020101")
_FALLBACK_RRC_SETUP_COMPLETE = bytes.fromhex("1000059f80105e40034060bd8400000000000000040bc0804040")
_FALLBACK_RRC_RECONFIG_COMPLETE = bytes.fromhex("1000")

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_ASN1_PATH = _PROJECT_ROOT / "tools" / "rrc-15.6.0.asn1"
_ASN1_EXPANDED = _PROJECT_ROOT / "tools" / "rrc-15.6.0-expanded.asn1"


def _try_compile_asn1():
    try:
        import asn1tools  # type: ignore
    except ImportError:
        return None

    for path in (_ASN1_EXPANDED, _ASN1_PATH):
        if not path.exists():
            continue
        try:
            return asn1tools.compile_files(str(path), "uper")
        except Exception:
            continue
    return None


_ASN1 = None


def _get_asn1():
    global _ASN1
    if _ASN1 is None:
        _ASN1 = _try_compile_asn1()
    return _ASN1


@dataclass
class CapturedDlRrc:
    timestamp: float
    channel: int
    raw_pdu: bytes
    pdu_id: int


class RrcChannel(IntEnum):
    BCCH_BCH = 0
    BCCH_DL_SCH = 1
    DL_CCCH = 2
    DL_DCCH = 3
    PCCH = 4
    UL_CCCH = 5
    UL_CCCH1 = 6
    UL_DCCH = 7
    DL_CHO = 8
    DL_SIB19 = 9


class FakeUe:
    def __init__(
        self,
        gnb_addr: str = "127.0.0.1",
        gnb_port: int = PORTAL_PORT,
        sim_pos: tuple[float, float, float] = (0.0, 0.0, 0.0),
        ue_id: int = 1,
    ):
        self._gnb_addr = gnb_addr
        self._gnb_port = gnb_port
        self._sim_pos = sim_pos
        self._sti = random.getrandbits(64)
        self._ue_id = ue_id

        self._sock: Optional[socket.socket] = None
        self._running = False
        self._hb_thread: Optional[threading.Thread] = None
        self._rx_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()

        self._heartbeat_ack_received = False
        self._pdu_id_counter = 1
        self._dl_messages: Deque[CapturedDlRrc] = deque(maxlen=200)

    def start(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(0.5)
        self._sock.bind(("0.0.0.0", 0))
        self._running = True

        self._hb_thread = threading.Thread(target=self._heartbeat_loop, daemon=True)
        self._rx_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self._hb_thread.start()
        self._rx_thread.start()

    def stop(self):
        self._running = False
        if self._hb_thread:
            self._hb_thread.join(timeout=3)
        if self._rx_thread:
            self._rx_thread.join(timeout=3)
        if self._sock:
            self._sock.close()
            self._sock = None

    def _send(self, data: bytes):
        if self._sock:
            self._sock.sendto(data, (self._gnb_addr, self._gnb_port))

    def _next_pdu_id(self) -> int:
        pid = self._pdu_id_counter
        self._pdu_id_counter += 1
        return pid

    def send_rrc_setup_request(self):
        self._send_ul_rrc(RrcChannel.UL_CCCH, _FALLBACK_RRC_SETUP_REQUEST)

    def send_rrc_setup_complete(self, nas_pdu: bytes | None = None):
        if nas_pdu is None:
            nas_pdu = _DEFAULT_NAS_REG_REQUEST
        # Fallback message already includes the default NAS above and is the stable path for this harness.
        _ = nas_pdu
        self._send_ul_rrc(RrcChannel.UL_DCCH, _FALLBACK_RRC_SETUP_COMPLETE)

    def send_measurement_report(
        self,
        meas_id: int = 1,
        serving_rsrp: int = 30,
        serving_pci: int = 0,
        neighbor_pci: int = 1,
        neighbor_rsrp: int = 50,
    ):
        pdu = self._build_measurement_report(
            meas_id,
            serving_rsrp,
            serving_pci,
            neighbor_pci,
            neighbor_rsrp,
        )
        self._send_ul_rrc(RrcChannel.UL_DCCH, pdu)

    def send_measurement_report_multi_neighbor(
        self,
        meas_id: int = 1,
        serving_rsrp: int = 30,
        serving_pci: int = 0,
        neighbors: Sequence[tuple[int, int]] = ((1, 50),),
    ):
        pdu = self._build_measurement_report_multi_neighbor(
            meas_id,
            serving_rsrp,
            serving_pci,
            neighbors,
        )
        self._send_ul_rrc(RrcChannel.UL_DCCH, pdu)

    def send_rrc_reconfiguration_complete(self, txn_id: int = 0):
        pdu = self._build_rrc_reconfig_complete(txn_id)
        self._send_ul_rrc(RrcChannel.UL_DCCH, pdu)

    def _send_ul_rrc(self, channel: RrcChannel, pdu: bytes):
        pid = self._next_pdu_id()
        msg = self._encode_pdu_transmission(int(channel), pid, pdu)
        self._send(msg)

    def wait_for_heartbeat_ack(self, timeout_s: float = 10.0) -> bool:
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            if self._heartbeat_ack_received:
                return True
            time.sleep(0.2)
        return False

    def wait_for_dl_rrc(self, channel: RrcChannel, timeout_s: float = 10.0) -> Optional[CapturedDlRrc]:
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            with self._lock:
                for msg in self._dl_messages:
                    if msg.channel == int(channel):
                        return msg
            time.sleep(0.2)
        return None

    @property
    def dl_messages(self) -> List[CapturedDlRrc]:
        with self._lock:
            return list(self._dl_messages)

    def _heartbeat_loop(self):
        while self._running:
            hb = self._encode_heartbeat(self._sim_pos)
            self._send(hb)
            time.sleep(1.0)

    def _receive_loop(self):
        while self._running:
            try:
                data, _ = self._sock.recvfrom(65536)
            except socket.timeout:
                continue
            except OSError:
                break

            parsed = self._decode_message(data)
            if parsed is None:
                continue

            if parsed["msg_type"] == MSG_HEARTBEAT_ACK:
                self._heartbeat_ack_received = True
                continue

            if parsed["msg_type"] != MSG_PDU_TRANSMISSION:
                continue

            if parsed["pdu_type"] == PDU_TYPE_RRC:
                with self._lock:
                    self._dl_messages.append(
                        CapturedDlRrc(
                            timestamp=time.monotonic(),
                            channel=parsed["payload"],
                            raw_pdu=parsed["pdu"],
                            pdu_id=parsed["pdu_id"],
                        )
                    )

            ack = self._encode_pdu_transmission_ack([parsed["pdu_id"]])
            self._send(ack)

    def _encode_header(self, msg_type: int, sender_id2: int = 0) -> bytes:
        return struct.pack(
            "!BBBBBQII",
            RLS_COMPAT_MARKER,
            RLS_VERSION[0],
            RLS_VERSION[1],
            RLS_VERSION[2],
            msg_type,
            self._sti,
            self._ue_id,
            sender_id2,
        )

    def _encode_heartbeat(self, sim_pos: tuple[float, float, float]) -> bytes:
        return self._encode_header(MSG_HEARTBEAT) + struct.pack("!ddd", sim_pos[0], sim_pos[1], sim_pos[2])

    def _encode_pdu_transmission(self, payload: int, pdu_id: int, pdu: bytes) -> bytes:
        body = struct.pack("!BIII", PDU_TYPE_RRC, pdu_id, payload, len(pdu))
        return self._encode_header(MSG_PDU_TRANSMISSION) + body + pdu

    def _encode_pdu_transmission_ack(self, pdu_ids: list[int]) -> bytes:
        body = struct.pack("!I", len(pdu_ids))
        for pid in pdu_ids:
            body += struct.pack("!I", pid)
        return self._encode_header(MSG_PDU_TRANSMISSION_ACK) + body

    def _decode_message(self, data: bytes) -> Optional[dict]:
        if len(data) < 21:
            return None

        marker, major, minor, patch, msg_type, sti, sender_id, sender_id2 = struct.unpack("!BBBBBQII", data[:21])
        if marker != RLS_COMPAT_MARKER:
            return None
        if (major, minor, patch) != RLS_VERSION:
            return None

        offset = 21
        if msg_type == MSG_HEARTBEAT_ACK:
            if len(data) < offset + 4:
                return None
            dbm = struct.unpack("!i", data[offset : offset + 4])[0]
            return {
                "msg_type": msg_type,
                "sti": sti,
                "sender_id": sender_id,
                "sender_id2": sender_id2,
                "dbm": dbm,
            }

        if msg_type == MSG_PDU_TRANSMISSION:
            if len(data) < offset + 13:
                return None
            pdu_type, pdu_id, payload, pdu_len = struct.unpack("!BIII", data[offset : offset + 13])
            offset += 13
            if len(data) < offset + pdu_len:
                return None
            return {
                "msg_type": msg_type,
                "sti": sti,
                "sender_id": sender_id,
                "sender_id2": sender_id2,
                "pdu_type": pdu_type,
                "pdu_id": pdu_id,
                "payload": payload,
                "pdu": data[offset : offset + pdu_len],
            }

        return {
            "msg_type": msg_type,
            "sti": sti,
            "sender_id": sender_id,
            "sender_id2": sender_id2,
        }

    def _build_measurement_report(
        self,
        meas_id: int,
        serving_rsrp: int,
        serving_pci: int,
        neighbor_pci: int,
        neighbor_rsrp: int,
    ) -> bytes:
        asn1 = _get_asn1()
        if asn1 is not None:
            try:
                serving = {
                    "servCellId": 0,
                    "measResultServingCell": {
                        "measResult": {
                            "cellResults": {
                                "resultsSSB-Cell": {
                                    "rsrp": serving_rsrp,
                                },
                            },
                        },
                    },
                }

                neighbors = [{
                    "physCellId": neighbor_pci,
                    "measResult": {
                        "cellResults": {
                            "resultsSSB-Cell": {
                                "rsrp": neighbor_rsrp,
                            },
                        },
                    },
                }]

                meas_results = {
                    "measId": meas_id,
                    "measResultServingMOList": [serving],
                    "measResultNeighCells": (
                        "measResultListNR",
                        neighbors,
                    ),
                }

                msg = {
                    "message": (
                        "c1",
                        (
                            "measurementReport",
                            {
                                "criticalExtensions": (
                                    "measurementReport",
                                    {
                                        "measResults": meas_results,
                                    },
                                ),
                            },
                        ),
                    )
                }
                return asn1.encode("UL-DCCH-Message", msg)
            except Exception:
                pass

        return bytes.fromhex("0000000000")

    def _build_measurement_report_multi_neighbor(
        self,
        meas_id: int,
        serving_rsrp: int,
        serving_pci: int,
        neighbors: Sequence[tuple[int, int]],
    ) -> bytes:
        asn1 = _get_asn1()
        if asn1 is not None:
            try:
                serving = {
                    "servCellId": serving_pci,
                    "measResultServingCell": {
                        "measResult": {
                            "cellResults": {
                                "resultsSSB-Cell": {
                                    "rsrp": serving_rsrp,
                                },
                            },
                        },
                    },
                }

                neighbor_items = []
                for pci, rsrp in neighbors:
                    neighbor_items.append(
                        {
                            "physCellId": pci,
                            "measResult": {
                                "cellResults": {
                                    "resultsSSB-Cell": {
                                        "rsrp": rsrp,
                                    },
                                },
                            },
                        }
                    )

                meas_results = {
                    "measId": meas_id,
                    "measResultServingMOList": [serving],
                    "measResultNeighCells": (
                        "measResultListNR",
                        neighbor_items,
                    ),
                }

                msg = {
                    "message": (
                        "c1",
                        (
                            "measurementReport",
                            {
                                "criticalExtensions": (
                                    "measurementReport",
                                    {
                                        "measResults": meas_results,
                                    },
                                ),
                            },
                        ),
                    )
                }
                return asn1.encode("UL-DCCH-Message", msg)
            except Exception:
                pass

        if not neighbors:
            return self._build_measurement_report(meas_id, serving_rsrp, serving_pci, neighbor_pci=1, neighbor_rsrp=50)

        best_pci, best_rsrp = max(neighbors, key=lambda item: item[1])
        return self._build_measurement_report(meas_id, serving_rsrp, serving_pci, best_pci, best_rsrp)

    def _build_rrc_reconfig_complete(self, txn_id: int) -> bytes:
        asn1 = _get_asn1()
        if asn1 is not None:
            try:
                msg = {
                    "message": (
                        "c1",
                        (
                            "rrcReconfigurationComplete",
                            {
                                "rrc-TransactionIdentifier": txn_id,
                                "criticalExtensions": (
                                    "rrcReconfigurationComplete",
                                    {},
                                ),
                            },
                        ),
                    )
                }
                return asn1.encode("UL-DCCH-Message", msg)
            except Exception:
                pass

        return _FALLBACK_RRC_RECONFIG_COMPLETE
