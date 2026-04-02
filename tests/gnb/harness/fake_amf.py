"""
Fake AMF — SCTP server that speaks minimal NGAP for gNB testing.

Accepts a gNB SCTP connection, responds to NGSetupRequest, and provides
a framework for handling/responding to subsequent NGAP procedures
(InitialUEMessage, HandoverRequired, PathSwitchRequest, etc.).

Usage::

    amf = FakeAmf()
    amf.start()         # binds SCTP, spawns accept thread
    # … start nr-gnb …
    amf.wait_for_ng_setup()
    # … interact …
    amf.stop()
"""

from __future__ import annotations

import logging
import select
import socket
import struct
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Deque, Dict, List, Optional, Tuple

try:
    import sctp as _sctp
    HAS_PYSCTP = True
except ImportError:
    HAS_PYSCTP = False

from . import ngap_codec as ngap

logger = logging.getLogger(__name__)


@dataclass
class CapturedNgapMessage:
    """An NGAP message captured from the gNB."""
    timestamp: float
    raw: bytes
    pdu: Optional[ngap.NgapPdu]
    procedure_code: int = 0
    procedure_name: str = ""


_PROC_NAMES = {
    ngap.PROC_ERROR_INDICATION: "ErrorIndication",
    ngap.PROC_HANDOVER_PREPARATION: "HandoverRequired/Command",
    ngap.PROC_DOWNLINK_NAS_TRANSPORT: "DownlinkNASTransport",
    ngap.PROC_HANDOVER_NOTIFICATION: "HandoverNotify",
    ngap.PROC_INITIAL_CONTEXT_SETUP: "InitialContextSetup",
    ngap.PROC_INITIAL_UE_MESSAGE: "InitialUEMessage",
    ngap.PROC_NG_SETUP: "NGSetup",
    ngap.PROC_PATH_SWITCH_REQUEST: "PathSwitchRequest",
    ngap.PROC_UPLINK_NAS_TRANSPORT: "UplinkNASTransport",
}


class FakeAmf:
    """Fake AMF that accepts SCTP from a real nr-gnb process."""

    def __init__(
        self,
        listen_addr: str = "127.0.0.5",
        listen_port: int = 38412,
        mcc: str = "286",
        mnc: str = "01",
        amf_name: str = "test-amf",
        auto_ng_setup: bool = True,
        auto_initial_context: bool = True,
    ):
        if not HAS_PYSCTP:
            raise RuntimeError("pysctp is required for FakeAmf — pip install pysctp")

        self._addr = listen_addr
        self._port = listen_port
        self._mcc = mcc
        self._mnc = mnc
        self._amf_name = amf_name
        self._auto_ng_setup = auto_ng_setup
        self._auto_initial_context = auto_initial_context

        self._server_sock = None
        self._client_sock = None
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()

        # Captured messages
        self._captured: Deque[CapturedNgapMessage] = deque(maxlen=500)

        # State
        self._ng_setup_done = False
        self._amf_ue_id_counter = 1
        self._ue_contexts: Dict[int, dict] = {}  # ran_ue_id → context

    # ------------------------------------------------------------------
    #  Lifecycle
    # ------------------------------------------------------------------

    def start(self):
        """Bind SCTP socket and start accept/receive thread."""
        self._server_sock = _sctp.sctpsocket_tcp(socket.AF_INET)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_sock.bind((self._addr, self._port))
        self._server_sock.listen(1)
        self._server_sock.settimeout(1.0)

        self._running = True
        self._thread = threading.Thread(target=self._accept_and_serve, daemon=True)
        self._thread.start()
        logger.info("FakeAmf listening on %s:%d", self._addr, self._port)

    def stop(self):
        """Stop the server and close sockets."""
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
        if self._client_sock:
            try:
                self._client_sock.close()
            except Exception:
                pass
        if self._server_sock:
            try:
                self._server_sock.close()
            except Exception:
                pass
        logger.info("FakeAmf stopped")

    # ------------------------------------------------------------------
    #  Send
    # ------------------------------------------------------------------

    def send_ngap(self, data: bytes, stream: int = 0):
        """Send raw NGAP bytes over SCTP to the gNB."""
        if self._client_sock is None:
            logger.warning("Cannot send: no gNB connection")
            return
        try:
            self._client_sock.sctp_send(data, ppid=socket.htonl(ngap.NGAP_PPID), stream=stream)
        except Exception as e:
            logger.error("SCTP send error: %s", e)

    def send_ng_setup_response(self):
        """Send a pre-built NGSetupResponse."""
        resp = ngap.build_ng_setup_response(
            amf_name=self._amf_name,
            mcc=self._mcc,
            mnc=self._mnc,
        )
        self.send_ngap(resp)
        self._ng_setup_done = True
        logger.info("Sent NGSetupResponse")

    def send_downlink_nas(self, ran_ue_id: int, nas_pdu: bytes):
        """Send DownlinkNASTransport to a specific UE."""
        ctx = self._ue_contexts.get(ran_ue_id)
        if ctx is None:
            logger.warning("No UE context for RAN-UE-NGAP-ID=%d", ran_ue_id)
            return
        data = ngap.build_downlink_nas_transport(
            amf_ue_ngap_id=ctx["amf_ue_id"],
            ran_ue_ngap_id=ran_ue_id,
            nas_pdu=nas_pdu,
        )
        self.send_ngap(data, stream=ctx.get("stream", 1))

    def send_initial_context_setup(self, ran_ue_id: int, nas_pdu: bytes = b'\x7e\x00\x42\x01\x00'):
        """Send InitialContextSetupRequest for a UE."""
        ctx = self._ue_contexts.get(ran_ue_id)
        if ctx is None:
            logger.warning("No UE context for RAN-UE-NGAP-ID=%d", ran_ue_id)
            return
        data = ngap.build_initial_context_setup_request(
            amf_ue_ngap_id=ctx["amf_ue_id"],
            ran_ue_ngap_id=ran_ue_id,
            nas_pdu=nas_pdu,
            mcc=self._mcc,
            mnc=self._mnc,
        )
        self.send_ngap(data, stream=ctx.get("stream", 1))
        logger.info("Sent InitialContextSetupRequest for RAN-UE=%d", ran_ue_id)

    def send_handover_command(self, ran_ue_id: int, container: bytes = b'\x00'):
        """Send HandoverCommand (successfulOutcome) to the gNB."""
        ctx = self._ue_contexts.get(ran_ue_id)
        if ctx is None:
            logger.warning("No UE context for RAN-UE-NGAP-ID=%d", ran_ue_id)
            return
        data = ngap.build_handover_command(
            amf_ue_ngap_id=ctx["amf_ue_id"],
            ran_ue_ngap_id=ran_ue_id,
            target_to_source_container=container,
        )
        self.send_ngap(data, stream=ctx.get("stream", 1))
        logger.info("Sent HandoverCommand for RAN-UE=%d", ran_ue_id)

    def send_handover_request(self, amf_ue_ngap_id: int, stream: int = 1):
        """Send HandoverRequest (initiatingMessage) to target gNB."""
        data = ngap.build_handover_request(amf_ue_ngap_id=amf_ue_ngap_id)
        self.send_ngap(data, stream=stream)
        logger.info("Sent HandoverRequest for AMF-UE=%d", amf_ue_ngap_id)

    def send_path_switch_request_ack(self, ran_ue_id: int):
        """Send PathSwitchRequestAcknowledge to the gNB."""
        ctx = self._ue_contexts.get(ran_ue_id)
        if ctx is None:
            return
        data = ngap.build_path_switch_request_ack(
            amf_ue_ngap_id=ctx["amf_ue_id"],
            ran_ue_ngap_id=ran_ue_id,
        )
        self.send_ngap(data, stream=ctx.get("stream", 1))
        logger.info("Sent PathSwitchRequestAck for RAN-UE=%d", ran_ue_id)

    # ------------------------------------------------------------------
    #  Wait helpers
    # ------------------------------------------------------------------

    def wait_for_ng_setup(self, timeout_s: float = 15.0) -> bool:
        """Wait until NGSetup is complete."""
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            if self._ng_setup_done:
                return True
            time.sleep(0.2)
        return False

    def wait_for_message(
        self,
        procedure_code: int,
        timeout_s: float = 15.0,
        pdu_type: Optional[int] = None,
    ) -> Optional[CapturedNgapMessage]:
        """Wait for a specific NGAP message from the gNB."""
        end = time.monotonic() + timeout_s
        seen = 0
        while time.monotonic() < end:
            with self._lock:
                for i in range(seen, len(self._captured)):
                    cm = self._captured[i]
                    if cm.procedure_code == procedure_code:
                        if pdu_type is None or (cm.pdu and cm.pdu.pdu_type == pdu_type):
                            return cm
                seen = len(self._captured)
            time.sleep(0.2)
        return None

    def wait_for_initial_ue_message(self, timeout_s: float = 15.0) -> Optional[CapturedNgapMessage]:
        return self.wait_for_message(ngap.PROC_INITIAL_UE_MESSAGE, timeout_s)

    def wait_for_handover_required(self, timeout_s: float = 15.0) -> Optional[CapturedNgapMessage]:
        return self.wait_for_message(ngap.PROC_HANDOVER_PREPARATION, timeout_s,
                                     pdu_type=ngap.PDU_INITIATING_MESSAGE)

    def wait_for_handover_request_ack(self, timeout_s: float = 15.0) -> Optional[CapturedNgapMessage]:
        """Wait for HandoverRequestAcknowledge from target gNB."""
        return self.wait_for_message(ngap.PROC_HANDOVER_PREPARATION, timeout_s,
                                     pdu_type=ngap.PDU_SUCCESSFUL_OUTCOME)

    def wait_for_handover_failure(self, timeout_s: float = 15.0) -> Optional[CapturedNgapMessage]:
        """Wait for HandoverFailure from target gNB."""
        return self.wait_for_message(ngap.PROC_HANDOVER_PREPARATION, timeout_s,
                                     pdu_type=ngap.PDU_UNSUCCESSFUL_OUTCOME)

    def wait_for_error_indication(self, timeout_s: float = 15.0) -> Optional[CapturedNgapMessage]:
        """Wait for ErrorIndication from gNB."""
        return self.wait_for_message(ngap.PROC_ERROR_INDICATION, timeout_s,
                                     pdu_type=ngap.PDU_INITIATING_MESSAGE)

    def wait_for_handover_notify(self, timeout_s: float = 15.0) -> Optional[CapturedNgapMessage]:
        return self.wait_for_message(ngap.PROC_HANDOVER_NOTIFICATION, timeout_s)

    def wait_for_path_switch_request(self, timeout_s: float = 15.0) -> Optional[CapturedNgapMessage]:
        return self.wait_for_message(ngap.PROC_PATH_SWITCH_REQUEST, timeout_s)

    # ------------------------------------------------------------------
    #  Captured messages
    # ------------------------------------------------------------------

    @property
    def captured_messages(self) -> List[CapturedNgapMessage]:
        with self._lock:
            return list(self._captured)

    def has_message(self, procedure_code: int) -> bool:
        with self._lock:
            return any(cm.procedure_code == procedure_code for cm in self._captured)

    # ------------------------------------------------------------------
    #  Properties
    # ------------------------------------------------------------------

    @property
    def ng_setup_done(self) -> bool:
        return self._ng_setup_done

    @property
    def connected(self) -> bool:
        return self._client_sock is not None

    def get_ue_context(self, ran_ue_id: int) -> Optional[dict]:
        return self._ue_contexts.get(ran_ue_id)

    # ------------------------------------------------------------------
    #  Internal: accept thread
    # ------------------------------------------------------------------

    def _accept_and_serve(self):
        """Accept one gNB connection and then serve it."""
        while self._running:
            try:
                conn, addr = self._server_sock.accept()
                self._client_sock = conn
                logger.info("gNB connected from %s", addr)
                self._serve_loop()
            except socket.timeout:
                continue
            except OSError:
                break

    def _serve_loop(self):
        """Read and dispatch NGAP messages from the gNB."""
        sock = self._client_sock
        if sock is None:
            return

        # Some pysctp builds expose MSG_NOTIFICATION; detect it.
        MSG_NOTIFICATION = getattr(socket, 'MSG_NOTIFICATION', 0x8000)

        import errno as _errno

        while self._running:
            # Use select to wait for readable data instead of relying on
            # pysctp's (often broken) timeout support.
            try:
                ready, _, _ = select.select([sock], [], [], 0.5)
            except (ValueError, OSError):
                break
            if not ready:
                continue

            try:
                fromaddr, flags, data, notif = sock.sctp_recv(65536)
            except socket.timeout:
                continue
            except OSError as e:
                if e.errno == _errno.EAGAIN or e.errno == _errno.EWOULDBLOCK:
                    continue
                logger.info("gNB disconnected (%s)", e)
                break
            except ConnectionError as e:
                logger.info("gNB disconnected (%s)", e)
                break

            # Skip SCTP notifications (association events, etc.)
            if flags & MSG_NOTIFICATION:
                logger.debug("SCTP notification (flags=%#x), skipping", flags)
                continue

            if not data:
                # Empty data with no notification flag: peer shut down.
                logger.info("gNB SCTP connection closed (EOF)")
                break

            self._handle_ngap_message(data)

    def _handle_ngap_message(self, data: bytes):
        """Parse and auto-respond to an incoming NGAP message."""
        pdu = ngap.decode_ngap_pdu(data)
        proc_code = pdu.procedure_code if pdu else (data[1] if len(data) > 1 else -1)
        proc_name = _PROC_NAMES.get(proc_code, f"unknown({proc_code})")

        cm = CapturedNgapMessage(
            timestamp=time.monotonic(),
            raw=data,
            pdu=pdu,
            procedure_code=proc_code,
            procedure_name=proc_name,
        )
        with self._lock:
            self._captured.append(cm)

        logger.info("Received NGAP: %s (proc=%d)", proc_name, proc_code)

        if pdu is None:
            return

        # Auto-respond based on procedure
        if proc_code == ngap.PROC_NG_SETUP and self._auto_ng_setup:
            self.send_ng_setup_response()

        elif proc_code == ngap.PROC_INITIAL_UE_MESSAGE:
            self._handle_initial_ue_message(pdu)

        elif proc_code == ngap.PROC_UPLINK_NAS_TRANSPORT:
            # Just capture — no auto-response needed for basic tests
            pass

        elif proc_code == ngap.PROC_HANDOVER_PREPARATION and pdu.is_initiating:
            # HandoverRequired — auto-respond with HandoverCommand if auto mode
            self._handle_handover_required(pdu)

        elif proc_code == ngap.PROC_HANDOVER_NOTIFICATION:
            # HandoverNotify — captured for assertions
            pass

        elif proc_code == ngap.PROC_PATH_SWITCH_REQUEST:
            # Auto-respond with PathSwitchRequestAck
            self._handle_path_switch_request(pdu)

    def _handle_initial_ue_message(self, pdu: ngap.NgapPdu):
        """Process InitialUEMessage: create UE context, optionally auto-respond."""
        ran_ue_id = ngap.extract_ran_ue_ngap_id(pdu)
        if ran_ue_id is None:
            logger.warning("InitialUEMessage without RAN-UE-NGAP-ID")
            return

        amf_ue_id = self._amf_ue_id_counter
        self._amf_ue_id_counter += 1

        self._ue_contexts[ran_ue_id] = {
            "amf_ue_id": amf_ue_id,
            "ran_ue_id": ran_ue_id,
            "stream": 1,
        }
        logger.info("Created UE context: RAN-UE=%d AMF-UE=%d", ran_ue_id, amf_ue_id)

        if self._auto_initial_context:
            # Send a simplified InitialContextSetupRequest with a dummy NAS acceptance
            # Use a minimal RegistrationAccept NAS PDU
            dummy_nas = bytes([
                0x7e,  # EPD = 5GMM
                0x00,  # Security header = plain
                0x42,  # Message type = Registration Accept
                0x01,  # 5GS registration result length
                0x01,  # Registration result = 3GPP access + registered
            ])
            # Give gNB a moment to process its own state
            time.sleep(0.1)
            self.send_downlink_nas(ran_ue_id, dummy_nas)

    def _handle_handover_required(self, pdu: ngap.NgapPdu):
        """Process HandoverRequired: auto-respond with HandoverCommand."""
        ran_ue_id = ngap.extract_ran_ue_ngap_id(pdu)
        if ran_ue_id is None:
            return
        logger.info("Auto-responding to HandoverRequired with HandoverCommand")
        # Small delay to simulate AMF processing
        time.sleep(0.05)
        self.send_handover_command(ran_ue_id, container=b'\x00')

    def _handle_path_switch_request(self, pdu: ngap.NgapPdu):
        """Process PathSwitchRequest: auto-respond with Ack."""
        ran_ue_id = ngap.extract_ran_ue_ngap_id(pdu)
        if ran_ue_id is None:
            return
        time.sleep(0.05)
        self.send_path_switch_request_ack(ran_ue_id)
