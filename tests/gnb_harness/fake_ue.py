"""
Fake UE — RLS-protocol client that drives the UE side of a real nr-gnb.

Sends heartbeats, RRC Setup Request, RRC Setup Complete, Measurement
Reports and RRC Reconfiguration Complete over the RLS UDP protocol
and captures all DL messages the gNB sends back.

Usage::

    ue = FakeUe()
    ue.start()                         # binds UDP, starts heartbeat thread
    ue.wait_for_heartbeat_ack()        # wait for gNB reply
    ue.send_rrc_setup_request()        # begin RRC connection
    dl = ue.wait_for_dl_rrc(RrcChannel.DL_CCCH)   # receive RRCSetup
    ue.send_rrc_setup_complete(nas=b'\\x7e\\x00\\x41...')
    dl = ue.wait_for_dl_rrc(RrcChannel.DL_DCCH)   # receive MeasConfig
"""

from __future__ import annotations

import logging
import os
import random
import socket
import struct
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Deque, List, Optional

# Add sibling harness to path so we can reuse the existing protocol module
_TESTS_DIR = Path(__file__).resolve().parent.parent
if str(_TESTS_DIR) not in sys.path:
    sys.path.insert(0, str(_TESTS_DIR))

from harness.rls_protocol import (
    EPduType,
    EMessageType,
    RlsHeartBeatAck,
    RlsPduTransmission,
    RlsMessage,
    RrcChannel,
    decode_rls_message,
    encode_heartbeat,
    encode_pdu_transmission,
    encode_pdu_transmission_ack,
)

logger = logging.getLogger(__name__)

# -----------------------------------------------------------------------
#  ASN.1 support for building UL RRC messages
# -----------------------------------------------------------------------
_ASN1_PATH = Path(__file__).resolve().parents[2] / "tools" / "rrc-15.6.0.asn1"
_ASN1_EXPANDED = Path(__file__).resolve().parents[2] / "tools" / "rrc-15.6.0-expanded.asn1"


def _try_compile():
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
            pass
    # Try generating expanded version
    from harness.rrc_builder import _expand_setup_release
    if _ASN1_PATH.exists():
        try:
            _expand_setup_release(_ASN1_PATH)
            if _ASN1_EXPANDED.exists():
                return asn1tools.compile_files(str(_ASN1_EXPANDED), "uper")
        except Exception:
            pass
    return None


_asn1_mod = None


def _get_asn1():
    global _asn1_mod
    if _asn1_mod is None:
        _asn1_mod = _try_compile()
    return _asn1_mod


# -----------------------------------------------------------------------
#  Pre-computed fallback UPER bytes
# -----------------------------------------------------------------------

# Minimal RRCSetupRequest: randomValue=0, estCause=mo-Data(4), spare=0
_FALLBACK_RRC_SETUP_REQUEST = bytes.fromhex("100000000008")

# Default NAS: 5GMM Registration Request with requestedNSSAI(SST=1)
# so the gNB's AMF selection can match on slice type.
#   7e       EPD = 5GMM
#   00       Security header = plain
#   41       Message type = Registration Request
#   79       regType(initial+follow-on) + ngKSI(7=no key)
#   000d     Mobile identity length = 13
#   01       SUCI, SUPI format=IMSI
#   82f610   PLMN = MCC 286, MNC 01
#   0000     Routing indicator = 0000
#   00       Protection scheme = null
#   00       Home network public key ID = 0
#   0000000010  MSIN = 0000000001 (BCD)
#   2f       IEI = Requested NSSAI
#   02       Length = 2
#   0101     S-NSSAI: length=1, SST=1
_DEFAULT_NAS_REG_REQUEST = bytes.fromhex(
    "7e004179000d0182f61000000000000000102f020101"
)

# Minimal RRCSetupComplete: txnId=0, selectedPLMN=1, NAS with NSSAI
# UPER-encoded via asn1tools with the default NAS Registration Request above
_FALLBACK_RRC_SETUP_COMPLETE = bytes.fromhex(
    "1000059f80105e40034060bd8400000000000000040bc0804040"
)

# Minimal RRCReconfigurationComplete: txnId=0
_FALLBACK_RRC_RECONFIG_COMPLETE = bytes.fromhex("1000")


# -----------------------------------------------------------------------
#  Captured DL message
# -----------------------------------------------------------------------

@dataclass
class CapturedDlRrc:
    """A DL RRC PDU captured from the gNB."""
    timestamp: float
    channel: int        # RrcChannel enum value
    raw_pdu: bytes      # raw UPER bytes
    pdu_id: int = 0


@dataclass
class CapturedDlRrcDecoded(CapturedDlRrc):
    """CapturedDlRrc with optional ASN.1 decode result."""
    decoded: dict = None  # type: ignore


# -----------------------------------------------------------------------
#  FakeUe
# -----------------------------------------------------------------------

class FakeUe:
    """RLS-protocol UDP client pretending to be a UE."""

    def __init__(
        self,
        gnb_addr: str = "127.0.0.1",
        gnb_port: int = 4997,
        sim_pos: tuple = (0, 0, 0),
    ):
        self._gnb_addr = gnb_addr
        self._gnb_port = gnb_port
        self._sim_pos = sim_pos
        self._sti = random.getrandbits(64)

        self._sock: Optional[socket.socket] = None
        self._running = False
        self._hb_thread: Optional[threading.Thread] = None
        self._rx_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()

        self._heartbeat_ack_received = False
        self._pdu_id_counter = 1

        # Captured DL messages
        self._dl_messages: Deque[CapturedDlRrc] = deque(maxlen=200)

    # ----------------------------------------------------------------
    #  Lifecycle
    # ----------------------------------------------------------------

    def start(self):
        """Bind UDP socket and start heartbeat + receive threads."""
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(0.5)
        # Bind to ephemeral port
        self._sock.bind(("0.0.0.0", 0))
        self._running = True

        self._hb_thread = threading.Thread(target=self._heartbeat_loop, daemon=True)
        self._rx_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self._hb_thread.start()
        self._rx_thread.start()
        logger.info("FakeUe started (STI=%016x), targeting %s:%d",
                     self._sti, self._gnb_addr, self._gnb_port)

    def stop(self):
        """Stop threads and close socket."""
        self._running = False
        if self._hb_thread:
            self._hb_thread.join(timeout=3)
        if self._rx_thread:
            self._rx_thread.join(timeout=3)
        if self._sock:
            self._sock.close()
            self._sock = None
        logger.info("FakeUe stopped")

    # ----------------------------------------------------------------
    #  Send helpers
    # ----------------------------------------------------------------

    def _send(self, data: bytes):
        if self._sock:
            self._sock.sendto(data, (self._gnb_addr, self._gnb_port))

    def _next_pdu_id(self) -> int:
        pid = self._pdu_id_counter
        self._pdu_id_counter += 1
        return pid

    def send_ul_rrc(self, channel: RrcChannel, pdu: bytes):
        """Send a UL RRC PDU on the given channel."""
        pid = self._next_pdu_id()
        msg = encode_pdu_transmission(
            sti=self._sti,
            pdu_type=EPduType.RRC,
            pdu_id=pid,
            payload=int(channel),
            pdu=pdu,
        )
        self._send(msg)
        logger.debug("Sent UL RRC on channel %s, %d bytes, pdu_id=%d",
                      channel.name, len(pdu), pid)

    # ----------------------------------------------------------------
    #  High-level UL message senders
    # ----------------------------------------------------------------

    def send_rrc_setup_request(
        self,
        random_value: int = 0,
        est_cause: str = "mo-Data",
    ):
        """Send RRCSetupRequest on UL-CCCH."""
        pdu = self._build_rrc_setup_request(random_value, est_cause)
        self.send_ul_rrc(RrcChannel.UL_CCCH, pdu)
        logger.info("Sent RRCSetupRequest")

    def send_rrc_setup_complete(
        self,
        txn_id: int = 0,
        selected_plmn: int = 1,
        nas_pdu: bytes = None,
    ):
        """Send RRCSetupComplete on UL-DCCH.

        Default NAS is a Registration Request with requestedNSSAI(SST=1)
        so that the gNB's AMF selection succeeds.
        """
        if nas_pdu is None:
            nas_pdu = _DEFAULT_NAS_REG_REQUEST
        pdu = self._build_rrc_setup_complete(txn_id, selected_plmn, nas_pdu)
        self.send_ul_rrc(RrcChannel.UL_DCCH, pdu)
        logger.info("Sent RRCSetupComplete (NAS %d bytes)", len(nas_pdu))

    def send_measurement_report(
        self,
        meas_id: int = 1,
        serving_rsrp: int = 30,
        serving_pci: int = 0,
        neighbor_pci: int = 1,
        neighbor_rsrp: int = 50,
    ):
        """Send MeasurementReport on UL-DCCH."""
        pdu = self._build_measurement_report(
            meas_id, serving_rsrp, serving_pci, neighbor_pci, neighbor_rsrp,
        )
        self.send_ul_rrc(RrcChannel.UL_DCCH, pdu)
        logger.info("Sent MeasurementReport (meas_id=%d, neigh PCI=%d RSRP=%d)",
                      meas_id, neighbor_pci, neighbor_rsrp)

    def send_rrc_reconfiguration_complete(self, txn_id: int = 0):
        """Send RRCReconfigurationComplete on UL-DCCH."""
        pdu = self._build_rrc_reconfig_complete(txn_id)
        self.send_ul_rrc(RrcChannel.UL_DCCH, pdu)
        logger.info("Sent RRCReconfigurationComplete")

    def send_ul_information_transfer(self, nas_pdu: bytes):
        """Send ULInformationTransfer on UL-DCCH."""
        pdu = self._build_ul_information_transfer(nas_pdu)
        self.send_ul_rrc(RrcChannel.UL_DCCH, pdu)
        logger.info("Sent ULInformationTransfer")

    # ----------------------------------------------------------------
    #  Wait helpers
    # ----------------------------------------------------------------

    def wait_for_heartbeat_ack(self, timeout_s: float = 10.0) -> bool:
        """Wait until first HeartBeatAck from gNB."""
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            if self._heartbeat_ack_received:
                return True
            time.sleep(0.2)
        return False

    def wait_for_dl_rrc(
        self,
        channel: Optional[RrcChannel] = None,
        timeout_s: float = 10.0,
        start_from: int = 0,
    ) -> Optional[CapturedDlRrc]:
        """Wait for a DL RRC PDU, optionally filtering by channel.

        ``start_from`` — skip the first N captured messages (for sequential waits).
        """
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            with self._lock:
                for i in range(start_from, len(self._dl_messages)):
                    msg = self._dl_messages[i]
                    if channel is None or msg.channel == int(channel):
                        return msg
            time.sleep(0.2)
        return None

    def wait_for_meas_config(self, timeout_s: float = 10.0) -> Optional[CapturedDlRrc]:
        """Wait for any DL-DCCH message (likely MeasConfig from gNB)."""
        return self.wait_for_dl_rrc(RrcChannel.DL_DCCH, timeout_s)

    @property
    def dl_messages(self) -> List[CapturedDlRrc]:
        with self._lock:
            return list(self._dl_messages)

    @property
    def dl_dcch_count(self) -> int:
        with self._lock:
            return sum(1 for m in self._dl_messages if m.channel == int(RrcChannel.DL_DCCH))

    # ----------------------------------------------------------------
    #  Internal: heartbeat loop
    # ----------------------------------------------------------------

    def _heartbeat_loop(self):
        while self._running:
            msg = encode_heartbeat(self._sti, self._sim_pos)
            self._send(msg)
            time.sleep(1.0)

    # ----------------------------------------------------------------
    #  Internal: receive loop
    # ----------------------------------------------------------------

    def _receive_loop(self):
        while self._running:
            try:
                data, addr = self._sock.recvfrom(65536)
            except socket.timeout:
                continue
            except OSError:
                break

            parsed = decode_rls_message(data)
            if parsed is None:
                continue

            if isinstance(parsed, RlsHeartBeatAck):
                self._heartbeat_ack_received = True

            elif isinstance(parsed, RlsPduTransmission):
                if parsed.pdu_type == EPduType.RRC:
                    cm = CapturedDlRrc(
                        timestamp=time.monotonic(),
                        channel=parsed.payload,
                        raw_pdu=parsed.pdu,
                        pdu_id=parsed.pdu_id,
                    )
                    with self._lock:
                        self._dl_messages.append(cm)
                    logger.debug("Captured DL RRC on channel %d, %d bytes",
                                  parsed.payload, len(parsed.pdu))

                # ACK every PDU
                ack = encode_pdu_transmission_ack(self._sti, [parsed.pdu_id])
                self._send(ack)

    # ----------------------------------------------------------------
    #  Internal: RRC message builders
    # ----------------------------------------------------------------

    def _build_rrc_setup_request(self, random_value: int, est_cause: str) -> bytes:
        asn1 = _get_asn1()
        if asn1 is not None:
            try:
                msg = {
                    "message": (
                        "c1", (
                            "rrcSetupRequest", {
                                "rrcSetupRequest": {
                                    "ue-Identity": ("randomValue", (random_value.to_bytes(5, 'big') if isinstance(random_value, int) else random_value, 39)),
                                    "establishmentCause": est_cause,
                                    "spare": (b'\x00', 1),
                                }
                            }
                        )
                    )
                }
                return asn1.encode("UL-CCCH-Message", msg)
            except Exception as e:
                logger.debug("asn1tools RRCSetupRequest encode failed: %s", e)

        return _FALLBACK_RRC_SETUP_REQUEST

    def _build_rrc_setup_complete(
        self, txn_id: int, selected_plmn: int, nas_pdu: bytes,
    ) -> bytes:
        asn1 = _get_asn1()
        if asn1 is not None:
            try:
                msg = {
                    "message": (
                        "c1", (
                            "rrcSetupComplete", {
                                "rrc-TransactionIdentifier": txn_id,
                                "criticalExtensions": (
                                    "rrcSetupComplete", {
                                        "selectedPLMN-Identity": selected_plmn,
                                        "dedicatedNAS-Message": nas_pdu,
                                    }
                                ),
                            }
                        )
                    )
                }
                return asn1.encode("UL-DCCH-Message", msg)
            except Exception as e:
                logger.debug("asn1tools RRCSetupComplete encode failed: %s", e)

        return _FALLBACK_RRC_SETUP_COMPLETE

    def _build_measurement_report(
        self,
        meas_id: int,
        serving_rsrp: int,
        serving_pci: int,
        neighbor_pci: int,
        neighbor_rsrp: int,
    ) -> bytes:
        """Build MeasurementReport with serving + one neighbor cell."""
        asn1 = _get_asn1()
        if asn1 is not None:
            try:
                # MeasResultServMO — serving cell
                serv_mo = {
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

                # Neighbor results
                neigh_results = [{
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
                    "measResultServingMOList": [serv_mo],
                }
                if neigh_results:
                    meas_results["measResultNeighCells"] = (
                        "measResultListNR", neigh_results
                    )

                msg = {
                    "message": (
                        "c1", (
                            "measurementReport", {
                                "criticalExtensions": (
                                    "measurementReport", {
                                        "measResults": meas_results,
                                    }
                                ),
                            }
                        )
                    )
                }
                return asn1.encode("UL-DCCH-Message", msg)
            except Exception as e:
                logger.debug("asn1tools MeasurementReport encode failed: %s", e)

        # Fallback: minimal stub (won't parse properly in gNB)
        logger.warning("Using fallback MeasurementReport — install asn1tools")
        return bytes.fromhex("0000000000")

    def _build_rrc_reconfig_complete(self, txn_id: int) -> bytes:
        asn1 = _get_asn1()
        if asn1 is not None:
            try:
                msg = {
                    "message": (
                        "c1", (
                            "rrcReconfigurationComplete", {
                                "rrc-TransactionIdentifier": txn_id,
                                "criticalExtensions": (
                                    "rrcReconfigurationComplete", {}
                                ),
                            }
                        )
                    )
                }
                return asn1.encode("UL-DCCH-Message", msg)
            except Exception as e:
                logger.debug("asn1tools RRCReconfigComplete encode failed: %s", e)

        return _FALLBACK_RRC_RECONFIG_COMPLETE

    def _build_ul_information_transfer(self, nas_pdu: bytes) -> bytes:
        asn1 = _get_asn1()
        if asn1 is not None:
            try:
                msg = {
                    "message": (
                        "c1", (
                            "ulInformationTransfer", {
                                "criticalExtensions": (
                                    "ulInformationTransfer", {
                                        "dedicatedNAS-Message": nas_pdu,
                                    }
                                ),
                            }
                        )
                    )
                }
                return asn1.encode("UL-DCCH-Message", msg)
            except Exception as e:
                logger.debug("asn1tools ULInfoTransfer encode failed: %s", e)

        # Fallback stub
        return b'\x50' + bytes([len(nas_pdu)]) + nas_pdu
