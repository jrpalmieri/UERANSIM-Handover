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
#  CHO binary protocol helper – v2 (condition groups)
# ======================================================================

# Event type constants for the v2 binary protocol
CHO_EVENT_T1 = 0
CHO_EVENT_A2 = 1
CHO_EVENT_A3 = 2
CHO_EVENT_A5 = 3
CHO_EVENT_D1 = 4
CHO_EVENT_D1_SIB19 = 5

_EVENT_TYPE_MAP = {
    "T1": CHO_EVENT_T1,
    "A2": CHO_EVENT_A2,
    "A3": CHO_EVENT_A3,
    "A5": CHO_EVENT_A5,
    "D1": CHO_EVENT_D1,
    "D1_SIB19": CHO_EVENT_D1_SIB19,
}

# Condition size = 56 bytes:
#   6 × int32 (24 bytes) + 4 × double (32 bytes)
CONDITION_SIZE = 56

# Candidate header = 24 bytes:
#   candidateId + targetPci + newCRNTI + t304Ms + executionPriority + numConditions
CANDIDATE_HEADER_SIZE = 24


def _build_condition(cond: Dict) -> bytes:
    """Encode a single condition into 56 bytes.

    Condition wire format (little-endian, 56 bytes):
      [0..3]   eventType      (int32)  0=T1, 1=A2, 2=A3, 3=A5, 4=D1, 5=D1_SIB19
      [4..7]   intParam1      (int32)
      [8..11]  intParam2      (int32)
      [12..15] intParam3      (int32)
      [16..19] timeToTriggerMs (int32)
      [20..23] reserved       (int32)
      [24..31] floatParam1    (double)
      [32..39] floatParam2    (double)
      [40..47] floatParam3    (double)
      [48..55] floatParam4    (double)

    Parameter mapping by event type:
      T1:       intParam1 = t1DurationMs
      A2:       intParam1 = threshold, intParam2 = hysteresis
      A3:       intParam1 = offset, intParam2 = hysteresis
      A5:       intParam1 = threshold1, intParam2 = threshold2, intParam3 = hysteresis
      D1:       floatParam1..4 = refX, refY, refZ, thresholdM
      D1_SIB19: intParam1 = flags (bit0=useNadir), floatParam1 = thresholdM, floatParam2 = elevMinDeg
    """
    evt_str = cond.get("event", "T1")
    evt = _EVENT_TYPE_MAP.get(evt_str, CHO_EVENT_T1)
    ttt = cond.get("timeToTriggerMs", 0)

    ip1, ip2, ip3 = 0, 0, 0
    fp1, fp2, fp3, fp4 = 0.0, 0.0, 0.0, 0.0

    if evt == CHO_EVENT_T1:
        ip1 = cond.get("t1DurationMs", 1000)
    elif evt == CHO_EVENT_A2:
        ip1 = cond.get("threshold", -110)
        ip2 = cond.get("hysteresis", 2)
    elif evt == CHO_EVENT_A3:
        ip1 = cond.get("offset", 6)
        ip2 = cond.get("hysteresis", 2)
    elif evt == CHO_EVENT_A5:
        ip1 = cond.get("threshold1", -110)
        ip2 = cond.get("threshold2", -100)
        ip3 = cond.get("hysteresis", 2)
    elif evt == CHO_EVENT_D1:
        fp1 = cond.get("refX", 0.0)
        fp2 = cond.get("refY", 0.0)
        fp3 = cond.get("refZ", 0.0)
        fp4 = cond.get("thresholdM", 1000.0)
    elif evt == CHO_EVENT_D1_SIB19:
        # flags: bit0 = useNadir (default true)
        flags = 0
        if cond.get("useNadir", True):
            flags |= 0x01
        ip1 = flags
        fp1 = cond.get("thresholdM", -1.0)           # <0 = use SIB19's distanceThresh
        fp2 = cond.get("elevationMinDeg", -1.0)       # <0 = disabled

    return struct.pack("<iiiiii", evt, ip1, ip2, ip3, ttt, 0) + \
           struct.pack("<dddd", fp1, fp2, fp3, fp4)


def _build_cho_binary(candidates: List[Dict]) -> bytes:
    """Build a v2 binary CHO configuration PDU for the DL_CHO channel.

    V2 wire format (little-endian):
      [0..3]   numCandidates (uint32)
      Per candidate (variable size):
        [0..3]   candidateId        (int32)
        [4..7]   targetPci          (int32)
        [8..11]  newCRNTI           (int32)
        [12..15] t304Ms             (int32)
        [16..19] executionPriority  (int32)  -1 = unset
        [20..23] numConditions      (uint32)
        Per condition (56 bytes each):
          (see _build_condition)

    Each candidate dict can use the LEGACY keys for single-condition
    shorthand:
      - conditionType: "T1_ONLY" | "T1_AND_A3" | "D1_ONLY"  →  mapped to
        conditions list automatically.
      - t1DurationMs, a3Offset, a3Hysteresis, a3TimeToTriggerMs,
        d1RefX, d1RefY, d1RefZ, d1ThresholdM  →  used for the single cond.

    Or the NEW key for arbitrary condition groups:
      - conditions: list of dicts, each with:
          event: "T1"|"A2"|"A3"|"A5"|"D1"
          (plus event-specific params)
    """
    buf = struct.pack("<I", len(candidates))
    for c in candidates:
        cid = c.get("candidateId", 1)
        tpci = c.get("targetPci", 2)
        crnti = c.get("newCRNTI", 0x1234)
        t304 = c.get("t304Ms", 1000)
        prio = c.get("executionPriority", -1)

        # Build condition list
        if "conditions" in c:
            # New-style: explicit condition list
            cond_list = c["conditions"]
        else:
            # Legacy-style: single conditionType → map to condition list
            cond_str = c.get("conditionType", "T1_ONLY")
            cond_list = _legacy_to_conditions(c, cond_str)

        buf += struct.pack("<iiiiiI", cid, tpci, crnti, t304, prio, len(cond_list))
        for cond in cond_list:
            buf += _build_condition(cond)

    return buf


def _legacy_to_conditions(c: Dict, cond_str: str) -> List[Dict]:
    """Convert legacy single-conditionType to a list of condition dicts."""
    if cond_str == "T1_ONLY":
        return [{"event": "T1", "t1DurationMs": c.get("t1DurationMs", 1000)}]
    elif cond_str == "T1_AND_A3":
        return [
            {"event": "T1", "t1DurationMs": c.get("t1DurationMs", 1000)},
            {"event": "A3",
             "offset": c.get("a3Offset", 6),
             "hysteresis": c.get("a3Hysteresis", 2),
             "timeToTriggerMs": c.get("a3TimeToTriggerMs", 0)},
        ]
    elif cond_str == "D1_ONLY":
        return [{"event": "D1",
                 "refX": c.get("d1RefX", 0.0),
                 "refY": c.get("d1RefY", 0.0),
                 "refZ": c.get("d1RefZ", 0.0),
                 "thresholdM": c.get("d1ThresholdM", 1000.0)}]
    elif cond_str == "D1_SIB19_ONLY":
        return [{"event": "D1_SIB19",
                 "useNadir": c.get("d1sib19UseNadir", True),
                 "thresholdM": c.get("d1sib19ThresholdM", -1.0),
                 "elevationMinDeg": c.get("d1sib19ElevationMinDeg", -1.0)}]
    else:
        return [{"event": "T1", "t1DurationMs": c.get("t1DurationMs", 1000)}]


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
        self._cell_id = int(nci & 0x3FF)
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
        self._consumed_idx: int = 0
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
            self._gnb_sti, EPduType.RRC, self._next_pdu_id(),
            int(channel), pdu,
            sender_id=self._cell_id,
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

    def send_cho_configuration(
        self,
        candidates: List[Dict],
    ):
        """Send CHO configuration via the DL_CHO custom channel (v2 protocol).

        Each candidate dict supports LEGACY keys (single condition shorthand):
          - candidateId, targetPci, newCRNTI, t304Ms
          - conditionType: "T1_ONLY" | "T1_AND_A3" | "D1_ONLY"
          - t1DurationMs, a3Offset, a3Hysteresis, a3TimeToTriggerMs
          - d1RefX, d1RefY, d1RefZ, d1ThresholdM

        Or NEW keys for arbitrary condition groups:
          - candidateId, targetPci, newCRNTI, t304Ms
          - executionPriority (int, lower = higher priority, -1 = unset)
          - conditions: list of dicts, each with:
              event: "T1"|"A2"|"A3"|"A5"|"D1"
              (plus event-specific params, see _build_condition)
        """
        pdu = _build_cho_binary(candidates)
        self.send_rrc(RrcChannel.DL_CHO, pdu)
        logger.info(
            "Sent CHO configuration: %d candidate(s)", len(candidates)
        )

    def send_conditional_reconfiguration(
        self,
        candidates_to_add_mod: Optional[List[Dict]] = None,
        candidate_ids_to_remove: Optional[List[int]] = None,
        transaction_id: int = 3,
    ):
        """Send ASN-based ConditionalReconfiguration in DL-DCCH.

        candidates_to_add_mod entries:
          {
            "candidateId": int,
            "measIds": [int, ...],
            "condRrcReconfig": bytes,
          }

        candidate_ids_to_remove is a list of CondReconfigId values.
        """
        reconfig = self._rrc.build_rrc_reconfiguration_conditional_handover(
            transaction_id=transaction_id,
            candidates_to_add_mod=candidates_to_add_mod,
            candidate_ids_to_remove=candidate_ids_to_remove,
        )
        self.send_dl_dcch(reconfig)
        logger.info(
            "Sent ConditionalReconfiguration: addMod=%d remove=%d",
            len(candidates_to_add_mod or []),
            len(candidate_ids_to_remove or []),
        )

    def send_sib19(self, **kwargs):
        """Send a SIB19 NTN configuration via the DL_SIB19 custom channel.

        All keyword arguments are forwarded to ``RrcCodec.build_sib19()``.
        Common parameters:

        - position_x/y/z : float — satellite ECEF position in meters.
        - velocity_vx/vy/vz : float — satellite velocity in m/s.
        - epoch_time : int — reference time in 10-ms steps.
        - k_offset : int — scheduling offset in ms.
        - distance_thresh : float — CHO D1 threshold in meters (< 0 = absent).
        - ta_common : int — common Timing Advance in T_c units.
        - ta_common_drift : int — TA drift rate (T_c/s).
        - ul_sync_validity : int — seconds (-1 = absent).
        - ntn_polarization : int — 0=RHCP, 1=LHCP, 2=LINEAR, -1=absent.
        """
        pdu = RrcCodec.build_sib19(**kwargs)
        self.send_rrc(RrcChannel.DL_SIB19, pdu)
        logger.info("Sent SIB19 (%d bytes)", len(pdu))

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
        ack = encode_heartbeat_ack(
            self._gnb_sti,
            self._cell_dbm,
            sender_id=self._cell_id,
        )
        self._send_raw(ack, addr)

    def _handle_pdu_transmission(self, pdu_tx: RlsPduTransmission, addr: Tuple[str, int]):
        """Process an uplink PDU_TRANSMISSION: ACK it and capture."""
        # Send ACK
        ack = encode_pdu_transmission_ack(
            self._gnb_sti,
            [pdu_tx.pdu_id],
            sender_id=self._cell_id,
        )
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
        """Wait for a captured UL message on *channel*, optionally filtered.

        Searches from the internal consumption index so that each call
        returns the *next* matching message (not one already returned).
        """
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            with self._lock:
                for i in range(self._consumed_idx, len(self._captured)):
                    cm = self._captured[i]
                    if cm.channel == channel:
                        if filter_fn is None or filter_fn(cm):
                            self._consumed_idx = i + 1
                            return cm
            time.sleep(0.2)
        return None

    @staticmethod
    def _is_measurement_report(cm: CapturedMessage) -> bool:
        """Heuristic: check if a UL-DCCH PDU is a MeasurementReport."""
        if len(cm.raw_pdu) < 1:
            return False
        # In UPER, UL-DCCH: 1 bit outer CHOICE (0=c1) + 4-bit c1 index.
        # measurementReport = c1 index 0 → top 5 bits = 00000.
        return (cm.raw_pdu[0] >> 3) == 0

    @staticmethod
    def _is_rrc_reconfiguration_complete(cm: CapturedMessage) -> bool:
        """Heuristic: check if a UL-DCCH PDU is RRCReconfigurationComplete.

        In UPER, UL-DCCH-MessageType → c1 → choice index 1 =
        rrcReconfigurationComplete.  Top 5 bits = 00001.
        """
        if len(cm.raw_pdu) < 1:
            return False
        return (cm.raw_pdu[0] >> 3) == 1
