"""
Fake gNB — speaks the RLS protocol over UDP and drives the UE through
registration using hand-crafted NAS and (optionally ASN.1-encoded) RRC
messages.

Typical usage inside a pytest test::

    gnb = FakeGnb()
    gnb.start()
    # … start UE process …
    gnb.wait_for_heartbeat()
    gnb.perform_cell_attach()
    gnb.perform_rrc_setup()
    gnb.perform_registration()     # full auth + security + accept
    # UE is now RRC_CONNECTED + RM_REGISTERED
    gnb.send_meas_config(...)
    msg = gnb.wait_for_measurement_report()
    gnb.stop()
"""

from __future__ import annotations

import logging
import os
import random
import select
import socket
import struct
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Deque, Dict, List, Optional, Tuple

from . import rls_protocol as rls
from .rls_protocol import (
    EMessageType, EPduType, RrcChannel,
    RlsHeartBeat, RlsHeartBeatAck, RlsPduTransmission,
    encode_heartbeat_ack, encode_pdu_transmission, encode_pdu_transmission_ack,
    decode_rls_message,
)
from .rrc_builder import RrcCodec
from . import nas_builder as nas
from . import milenage

logger = logging.getLogger(__name__)


# ======================================================================
#  Data types
# ======================================================================

@dataclass
class CapturedMessage:
    """A message captured from the UE on the uplink."""
    timestamp: float
    rls_msg: rls.RlsMessage
    channel: Optional[RrcChannel] = None
    nas_name: str = ""
    raw_pdu: bytes = b""


# ======================================================================
#  FakeGnb
# ======================================================================

class FakeGnb:
    """Fake gNB that communicates with a real nr-ue process over UDP/RLS."""

    def __init__(
        self,
        listen_addr: str = "0.0.0.0",
        listen_port: int = rls.PORTAL_PORT,
        cell_dbm: int = -60,
        mcc: str = "286",
        mnc: str = "93",
        tac: int = 1,
        nci: int = 1,
        ue_supi: str = "imsi-286010000000001",
        ue_key: str = "465B5CE8B199B49FAA5F0A2EE238A6BC",
        ue_op: str = "E8ED289DEBA952E4283B54E88E6183CA",
        ue_op_type: str = "OP",
    ):
        self._addr = listen_addr
        self._port = listen_port
        self._cell_dbm = cell_dbm
        self._mcc = mcc
        self._mnc = mnc
        self._tac = tac
        self._nci = nci
        self._supi = ue_supi
        self._ue_key = bytes.fromhex(ue_key)
        self._ue_op = bytes.fromhex(ue_op)
        self._is_opc = ue_op_type.upper() == "OPC"

        # RLS state
        self._sock: Optional[socket.socket] = None
        self._ue_addr: Optional[Tuple[str, int]] = None
        self._ue_sti: int = 0
        self._gnb_sti: int = random.getrandbits(64)
        self._pdu_id_counter: int = 1

        # Captured messages
        self._captured: Deque[CapturedMessage] = deque(maxlen=1000)
        self._lock = threading.Lock()

        # RRC codec
        self._rrc = RrcCodec()

        # NAS security context
        self._nas_ctx: Optional[nas.NasSecurityContext] = None
        self._keys: Optional[dict] = None  # output of derive_full_key_set

        # Listener thread
        self._running = False
        self._thread: Optional[threading.Thread] = None

    # ------------------------------------------------------------------
    #  Lifecycle
    # ------------------------------------------------------------------

    def start(self):
        """Bind the UDP socket and start the listener thread."""
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self._addr, self._port))
        self._sock.settimeout(0.5)

        self._running = True
        self._thread = threading.Thread(target=self._listen_loop, daemon=True)
        self._thread.start()
        logger.info("FakeGnb listening on %s:%d", self._addr, self._port)

    def stop(self):
        """Stop the listener and close the socket."""
        self._running = False
        if self._thread:
            self._thread.join(timeout=3)
        if self._sock:
            self._sock.close()
        logger.info("FakeGnb stopped")

    # ------------------------------------------------------------------
    #  Low-level send helpers
    # ------------------------------------------------------------------

    def _send_raw(self, data: bytes, addr: Optional[Tuple[str, int]] = None):
        """Send raw bytes to the UE (or a specific address)."""
        dest = addr or self._ue_addr
        if dest and self._sock:
            self._sock.sendto(data, dest)

    def _next_pdu_id(self) -> int:
        pid = self._pdu_id_counter
        self._pdu_id_counter += 1
        return pid

    def send_rrc(self, channel: RrcChannel, pdu: bytes):
        """Send an RRC PDU to the UE on the given channel."""
        msg = encode_pdu_transmission(
            self._ue_sti, EPduType.RRC, self._next_pdu_id(),
            int(channel), pdu,
        )
        self._send_raw(msg)
        logger.debug("Sent RRC PDU on %s (%d bytes)", channel.name, len(pdu))

    def send_dl_ccch(self, pdu: bytes):
        self.send_rrc(RrcChannel.DL_CCCH, pdu)

    def send_dl_dcch(self, pdu: bytes):
        self.send_rrc(RrcChannel.DL_DCCH, pdu)

    def send_nas_in_dl_info_transfer(self, nas_pdu: bytes, transaction_id: int = 0):
        """Wrap NAS PDU in DLInformationTransfer and send on DL-DCCH."""
        rrc_pdu = self._rrc.build_dl_information_transfer(nas_pdu, transaction_id)
        self.send_dl_dcch(rrc_pdu)

    # ------------------------------------------------------------------
    #  High-level protocol flows
    # ------------------------------------------------------------------

    def wait_for_heartbeat(self, timeout_s: float = 10.0) -> bool:
        """Wait until we receive a HeartBeat from the UE."""
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            if self._ue_addr is not None:
                return True
            time.sleep(0.2)
        return False

    def perform_cell_attach(self):
        """Send MIB + SIB1 to trigger cell selection at the UE.

        Must be called after wait_for_heartbeat() succeeds.
        """
        mib = self._rrc.build_mib()
        sib1 = self._rrc.build_sib1(self._mcc, self._mnc, self._tac, self._nci)
        self.send_rrc(RrcChannel.BCCH_BCH, mib)
        time.sleep(0.1)
        self.send_rrc(RrcChannel.BCCH_DL_SCH, sib1)
        logger.info("Sent MIB + SIB1 for cell attach")

    def wait_for_rrc_setup_request(self, timeout_s: float = 10.0) -> Optional[CapturedMessage]:
        """Wait for the UE to send an RRCSetupRequest on UL-CCCH."""
        return self._wait_for_ul(RrcChannel.UL_CCCH, timeout_s=timeout_s)

    def perform_rrc_setup(self, transaction_id: int = 0) -> bool:
        """Wait for RRCSetupRequest and respond with RRCSetup.

        Returns True if the setup request was received.
        """
        req = self.wait_for_rrc_setup_request()
        if req is None:
            logger.warning("No RRCSetupRequest received")
            return False

        rrc_setup = self._rrc.build_rrc_setup(transaction_id)
        self.send_dl_ccch(rrc_setup)
        logger.info("Sent RRCSetup (txn=%d)", transaction_id)
        return True

    def perform_registration(self, timeout_s: float = 15.0) -> bool:
        """Drive the full NAS registration flow.

        Sequence:
          1. Receive RegistrationRequest (in RRCSetupComplete or ULInfoTransfer)
          2. Send AuthenticationRequest
          3. Receive AuthenticationResponse
          4. Send SecurityModeCommand (integrity-protected)
          5. Receive SecurityModeComplete
          6. Send RegistrationAccept (integrity + ciphered)

        Returns True on success.
        """
        # Step 1: Wait for NAS PDU (RegistrationRequest) from UE
        reg_req = self._wait_for_ul(RrcChannel.UL_DCCH, timeout_s=timeout_s)
        if reg_req is None:
            logger.warning("No uplink DCCH message received for registration")
            return False
        logger.info("Received UL-DCCH message (likely Registration flow)")

        # Step 2: Generate auth vector and send AuthenticationRequest
        self._keys = milenage.derive_full_key_set(
            k=self._ue_key,
            op_or_opc=self._ue_op,
            is_opc=self._is_opc,
            mcc=self._mcc,
            mnc=self._mnc,
            supi=self._supi,
        )

        auth_req = nas.build_authentication_request(
            rand=self._keys["rand"],
            autn=self._keys["autn"],
        )
        self.send_nas_in_dl_info_transfer(auth_req)
        logger.info("Sent AuthenticationRequest")

        # Step 3: Wait for AuthenticationResponse
        auth_resp = self._wait_for_ul(RrcChannel.UL_DCCH, timeout_s=timeout_s)
        if auth_resp is None:
            logger.warning("No AuthenticationResponse received")
            return False
        logger.info("Received AuthenticationResponse")

        # Step 4: Send SecurityModeCommand (integrity-protected with new context)
        self._nas_ctx = nas.NasSecurityContext(
            k_nas_int=self._keys["k_nas_int"],
            k_nas_enc=self._keys["k_nas_enc"],
            int_algo=nas.NasIntAlgo.IA2,
            ciph_algo=nas.NasCiphAlgo.EA2,
        )

        smc_plain = nas.build_security_mode_command(
            int_algo=nas.NasIntAlgo.IA2,
            ciph_algo=nas.NasCiphAlgo.EA2,
            imeisv_request=False,
        )
        smc_protected = self._nas_ctx.integrity_protect(smc_plain, direction=0)
        self.send_nas_in_dl_info_transfer(smc_protected)
        logger.info("Sent SecurityModeCommand (integrity-protected)")

        # Step 5: Wait for SecurityModeComplete
        smc_complete = self._wait_for_ul(RrcChannel.UL_DCCH, timeout_s=timeout_s)
        if smc_complete is None:
            logger.warning("No SecurityModeComplete received")
            return False
        logger.info("Received SecurityModeComplete")

        # Step 6: Send RegistrationAccept (integrity + ciphered)
        guti = nas.build_5g_guti(mcc=self._mcc, mnc=self._mnc)
        reg_accept = nas.build_registration_accept(guti=guti)
        reg_accept_protected = self._nas_ctx.integrity_protect_and_cipher(
            reg_accept, direction=0
        )
        self.send_nas_in_dl_info_transfer(reg_accept_protected)
        logger.info("Sent RegistrationAccept (secured)")

        return True

    def send_meas_config(
        self,
        meas_objects: Optional[List[Dict]] = None,
        report_configs: Optional[List[Dict]] = None,
        meas_ids: Optional[List[Dict]] = None,
        transaction_id: int = 1,
    ):
        """Send RRCReconfiguration with measurement configuration."""
        reconfig = self._rrc.build_rrc_reconfiguration(
            transaction_id=transaction_id,
            meas_objects=meas_objects,
            report_configs=report_configs,
            meas_ids=meas_ids,
        )
        self.send_dl_dcch(reconfig)
        logger.info("Sent RRCReconfiguration with measConfig")

    def send_handover_command(
        self,
        target_pci: int = 2,
        new_crnti: int = 0x1234,
        t304_ms: int = 1000,
        transaction_id: int = 2,
    ):
        """Send an RRCReconfiguration containing ReconfigurationWithSync.

        This triggers the UE to perform a handover to the cell identified
        by *target_pci*.
        """
        reconfig = self._rrc.build_rrc_reconfiguration_with_sync(
            transaction_id=transaction_id,
            target_pci=target_pci,
            new_crnti=new_crnti,
            t304_ms=t304_ms,
        )
        self.send_dl_dcch(reconfig)
        logger.info(
            "Sent handover command: targetPCI=%d newC-RNTI=%d t304=%dms",
            target_pci, new_crnti, t304_ms,
        )

    def send_rrc_release(self, transaction_id: int = 0):
        """Send RRCRelease to move UE back to IDLE."""
        release = self._rrc.build_rrc_release(transaction_id)
        self.send_dl_dcch(release)
        logger.info("Sent RRCRelease")

    def wait_for_measurement_report(self, timeout_s: float = 15.0) -> Optional[CapturedMessage]:
        """Wait for a MeasurementReport on UL-DCCH."""
        return self._wait_for_ul(
            RrcChannel.UL_DCCH, timeout_s=timeout_s,
            filter_fn=lambda cm: self._is_measurement_report(cm),
        )

    def wait_for_rrc_reconfiguration_complete(
        self, timeout_s: float = 10.0
    ) -> Optional[CapturedMessage]:
        """Wait for an RRCReconfigurationComplete on UL-DCCH."""
        return self._wait_for_ul(
            RrcChannel.UL_DCCH, timeout_s=timeout_s,
            filter_fn=lambda cm: self._is_rrc_reconfiguration_complete(cm),
        )

    def wait_for_ul_dcch(self, timeout_s: float = 10.0) -> Optional[CapturedMessage]:
        """Wait for any UL-DCCH PDU."""
        return self._wait_for_ul(RrcChannel.UL_DCCH, timeout_s=timeout_s)

    # ------------------------------------------------------------------
    #  Captured message access
    # ------------------------------------------------------------------

    @property
    def captured_messages(self) -> List[CapturedMessage]:
        with self._lock:
            return list(self._captured)

    def captured_rrc_on(self, channel: RrcChannel) -> List[CapturedMessage]:
        """Return all captured RRC messages on a specific channel."""
        return [
            cm for cm in self.captured_messages
            if cm.channel == channel
        ]

    def clear_captured(self):
        with self._lock:
            self._captured.clear()

    # ------------------------------------------------------------------
    #  Properties
    # ------------------------------------------------------------------

    @property
    def cell_dbm(self) -> int:
        return self._cell_dbm

    @cell_dbm.setter
    def cell_dbm(self, value: int):
        self._cell_dbm = value

    @property
    def rrc_codec(self) -> RrcCodec:
        return self._rrc

    @property
    def nas_security(self) -> Optional[nas.NasSecurityContext]:
        return self._nas_ctx

    # ------------------------------------------------------------------
    #  Internal: listener loop
    # ------------------------------------------------------------------

    def _listen_loop(self):
        """Background thread: receive RLS messages and auto-reply to heartbeats."""
        buf_size = 16384
        while self._running:
            try:
                ready, _, _ = select.select([self._sock], [], [], 0.3)
                if not ready:
                    continue
                data, addr = self._sock.recvfrom(buf_size)
            except (OSError, socket.timeout):
                continue

            msg = decode_rls_message(data)
            if msg is None:
                continue

            if isinstance(msg, RlsHeartBeat):
                self._handle_heartbeat(msg, addr)
            elif isinstance(msg, RlsPduTransmission):
                self._handle_pdu_transmission(msg, addr)
            elif isinstance(msg, rls.RlsPduTransmissionAck):
                pass  # ack from UE — nothing to do

    def _handle_heartbeat(self, hb: RlsHeartBeat, addr: Tuple[str, int]):
        """Reply with HeartBeatAck and record UE address/STI."""
        self._ue_addr = addr
        self._ue_sti = hb.sti
        ack = encode_heartbeat_ack(self._gnb_sti, self._cell_dbm)
        self._send_raw(ack, addr)

    def _handle_pdu_transmission(self, pdu_tx: RlsPduTransmission, addr: Tuple[str, int]):
        """Process an uplink PDU_TRANSMISSION: ACK it and capture."""
        # Send ACK
        ack = encode_pdu_transmission_ack(self._gnb_sti, [pdu_tx.pdu_id])
        self._send_raw(ack, addr)

        # Determine channel
        channel = None
        if pdu_tx.pdu_type == EPduType.RRC:
            try:
                channel = RrcChannel(pdu_tx.payload)
            except ValueError:
                pass

        # Identify NAS if this is UL-DCCH containing ULInfoTransfer
        nas_name = ""
        raw_pdu = pdu_tx.pdu
        if channel in (RrcChannel.UL_DCCH,):
            # Try to identify NAS by decoding the RRC/NAS
            decoded = self._rrc.decode_ul_dcch(raw_pdu)
            if decoded.get("message_type") == "ulInformationTransfer":
                nas_bytes = decoded.get("likely_nas_pdu", b"")
                if nas_bytes:
                    nas_name = nas.identify_nas_message(nas_bytes)

        cm = CapturedMessage(
            timestamp=time.monotonic(),
            rls_msg=pdu_tx,
            channel=channel,
            nas_name=nas_name,
            raw_pdu=raw_pdu,
        )
        with self._lock:
            self._captured.append(cm)

        logger.debug(
            "Captured UL PDU: channel=%s pdu_type=%s len=%d nas=%s",
            channel, pdu_tx.pdu_type, len(raw_pdu), nas_name or "N/A",
        )

    # ------------------------------------------------------------------
    #  Internal: wait helpers
    # ------------------------------------------------------------------

    def _wait_for_ul(
        self,
        channel: RrcChannel,
        timeout_s: float = 10.0,
        filter_fn: Optional[Callable[[CapturedMessage], bool]] = None,
    ) -> Optional[CapturedMessage]:
        """Wait for a captured UL message on *channel*, optionally filtered."""
        start_idx = len(self._captured)
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            with self._lock:
                for i in range(start_idx, len(self._captured)):
                    cm = self._captured[i]
                    if cm.channel == channel:
                        if filter_fn is None or filter_fn(cm):
                            return cm
            time.sleep(0.2)
        return None

    @staticmethod
    def _is_measurement_report(cm: CapturedMessage) -> bool:
        """Heuristic: check if a UL-DCCH PDU is a MeasurementReport."""
        if len(cm.raw_pdu) < 1:
            return False
        # In UPER, UL-DCCH c1 choice 0 = measurementReport
        first_nibble = (cm.raw_pdu[0] >> 4) & 0x0F
        return first_nibble == 0

    @staticmethod
    def _is_rrc_reconfiguration_complete(cm: CapturedMessage) -> bool:
        """Heuristic: check if a UL-DCCH PDU is RRCReconfigurationComplete.

        In UPER, UL-DCCH-MessageType → c1 → choice index 1 =
        rrcReconfigurationComplete.  The first 4 bits of the encoded
        PDU represent the c1 choice index.
        """
        if len(cm.raw_pdu) < 1:
            return False
        first_nibble = (cm.raw_pdu[0] >> 4) & 0x0F
        return first_nibble == 1
