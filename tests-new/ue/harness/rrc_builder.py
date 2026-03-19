"""
NR RRC (TS 38.331) message encoder / decoder.

Primary approach: use ``asn1tools`` to compile the 3GPP RRC ASN.1 schema
and produce UPER-encoded PDUs at test time.

Fallback: pre-computed UPER byte sequences for the handful of DL messages
needed by the test harness (MIB, SIB1, RRCSetup, DLInformationTransfer,
RRCReconfiguration, RRCRelease).

Usage::

    rrc = RrcCodec()          # compiles ASN.1 on first call
    pdu = rrc.build_rrc_setup(transaction_id=0)
    info = rrc.decode_ul_ccch(raw_bytes)
"""

from __future__ import annotations

import logging
import os
from pathlib import Path
from typing import Any, Dict, List, Optional

logger = logging.getLogger(__name__)

# Path to the RRC ASN.1 schema bundled with UERANSIM
_ASN1_PATH = Path(__file__).resolve().parents[3] / "tools" / "rrc-15.6.0.asn1"
_ASN1_EXPANDED_PATH = Path(__file__).resolve().parents[3] / "tools" / "rrc-15.6.0-expanded.asn1"


def _try_compile_asn1():
    """Attempt to compile the NR RRC ASN.1 module via asn1tools.

    Returns the compiled module or *None* on failure.
    """
    try:
        import asn1tools  # type: ignore
    except ImportError:
        logger.info("asn1tools not installed — using pre-computed RRC bytes")
        return None

    # Try expanded file first (parameterized types pre-expanded)
    for path in (_ASN1_EXPANDED_PATH, _ASN1_PATH):
        if not path.exists():
            continue
        try:
            compiled = asn1tools.compile_files(str(path), "uper")
            logger.info("Compiled RRC ASN.1 from %s (%d types)", path.name, len(compiled.types))
            return compiled
        except Exception as exc:
            logger.debug("Failed to compile %s: %s", path.name, exc)

    # If the expanded file doesn't exist, try generating it
    if _ASN1_PATH.exists() and not _ASN1_EXPANDED_PATH.exists():
        try:
            expanded = _expand_setup_release(_ASN1_PATH)
            if expanded is not None:
                compiled = asn1tools.compile_files(str(_ASN1_EXPANDED_PATH), "uper")
                logger.info("Compiled expanded RRC ASN.1 (%d types)", len(compiled.types))
                return compiled
        except Exception as exc:
            logger.debug("Failed to compile expanded ASN.1: %s", exc)

    logger.warning("Could not compile any RRC ASN.1 file")
    return None


def _expand_setup_release(asn1_path: Path) -> Optional[Path]:
    """Preprocess ASN.1 to expand SetupRelease parameterized type inline."""
    import re
    try:
        content = asn1_path.read_text()
        # Remove the parameterized type definition
        content = re.sub(
            r'-- TAG-SETUPRELEASE-START.*?-- TAG-SETUPRELEASE-STOP',
            '-- SetupRelease expanded inline',
            content,
            flags=re.DOTALL,
        )
        # Replace all usages with inline CHOICE
        content = re.sub(
            r'SetupRelease\s*\{\s*([^}]+?)\s*\}',
            r'CHOICE { release NULL, setup \1 }',
            content,
        )
        out_path = asn1_path.parent / "rrc-15.6.0-expanded.asn1"
        out_path.write_text(content)
        logger.info("Generated expanded ASN.1 at %s", out_path)
        return out_path
    except Exception as exc:
        logger.warning("Failed to expand SetupRelease: %s", exc)
        return None
        return None


# T304 timer mapping (TS 38.331 §6.3.2)
T304_MS_TO_ENUM = {50: 0, 100: 1, 150: 2, 200: 3, 500: 4, 1000: 5, 2000: 6, 10000: 7}
T304_ENUM_TO_MS = {v: k for k, v in T304_MS_TO_ENUM.items()}
T304_MS_TO_ASN1_STR = {50: "ms50", 100: "ms100", 150: "ms150", 200: "ms200",
                       500: "ms500", 1000: "ms1000", 2000: "ms2000", 10000: "ms10000"}


def t304_ms_to_enum(ms: int) -> int:
    """Convert T304 milliseconds to the ASN.1 enum index."""
    return T304_MS_TO_ENUM.get(ms, 5)  # default ms1000


def t304_enum_to_ms(enum_val: int) -> int:
    """Convert T304 ASN.1 enum index to milliseconds."""
    table = [50, 100, 150, 200, 500, 1000, 2000, 10000]
    if 0 <= enum_val < len(table):
        return table[enum_val]
    return 1000


# ------------------------------------------------------------------
#  UPER bit-level helpers for fallback encoding
# ------------------------------------------------------------------

def _bits_to_bytes(bits: list) -> bytes:
    """Convert a list of 0/1 bits to bytes, padding with trailing zeros."""
    while len(bits) % 8 != 0:
        bits.append(0)
    result = bytearray()
    for i in range(0, len(bits), 8):
        val = 0
        for j in range(8):
            val = (val << 1) | bits[i + j]
        result.append(val)
    return bytes(result)


def _push_int(bits: list, value: int, width: int):
    """Append *width* bits of *value* (MSB first) to *bits*."""
    for i in range(width - 1, -1, -1):
        bits.append((value >> i) & 1)


def _push_bytes(bits: list, data: bytes):
    """Append all bytes of *data* to *bits*."""
    for b in data:
        _push_int(bits, b, 8)


def _push_length_determinant(bits: list, length: int):
    """Append a UPER unconstrained-length determinant."""
    if length < 128:
        _push_int(bits, length, 8)
    elif length < 16384:
        _push_int(bits, 0b10, 2)
        _push_int(bits, length, 14)
    else:
        raise ValueError(f"Length {length} too large for fallback encoder")


def _uper_dl_info_transfer(tid: int, nas_pdu: bytes) -> bytes:
    """UPER-encode DL-DCCH-Message → c1 → dlInformationTransfer."""
    bits: list = []
    _push_int(bits, 0, 1)       # c1 (not messageClassExtension)
    _push_int(bits, 5, 4)       # dlInformationTransfer = index 5
    _push_int(bits, tid & 3, 2) # rrc-TransactionIdentifier
    _push_int(bits, 0, 1)       # criticalExtensions = IEs
    _push_int(bits, 1, 1)       # dedicatedNAS-Message PRESENT
    _push_int(bits, 0, 1)       # lateNonCriticalExtension ABSENT
    _push_int(bits, 0, 1)       # nonCriticalExtension ABSENT
    _push_length_determinant(bits, len(nas_pdu))
    _push_bytes(bits, nas_pdu)
    return _bits_to_bytes(bits)


def _uper_rrc_release(tid: int) -> bytes:
    """UPER-encode DL-DCCH-Message → c1 → rrcRelease (empty IEs)."""
    bits: list = []
    _push_int(bits, 0, 1)       # c1
    _push_int(bits, 2, 4)       # rrcRelease = index 2
    _push_int(bits, tid & 3, 2) # rrc-TransactionIdentifier
    _push_int(bits, 0, 1)       # criticalExtensions = IEs
    _push_int(bits, 0, 1)       # extension marker (no extensions)
    _push_int(bits, 0, 6)       # 6 OPTIONAL fields all ABSENT
    return _bits_to_bytes(bits)


class RrcCodec:
    """Encode & decode NR RRC PDUs.

    Tries ``asn1tools`` first; falls back to pre-computed constants.
    """

    def __init__(self):
        self._asn1 = _try_compile_asn1()

    @property
    def has_asn1(self) -> bool:
        return self._asn1 is not None

    # ------------------------------------------------------------------
    #  DL message builders
    # ------------------------------------------------------------------

    def build_mib(
        self,
        sfn: int = 0,
        scs_common: str = "scs15or60",
        ssb_offset: int = 0,
        dmrs_pos: str = "pos2",
        cell_barred: str = "notBarred",
        intra_freq: str = "allowed",
    ) -> bytes:
        """Encode a MIB (BCCH-BCH-Message)."""
        if self._asn1 is not None:
            mib_msg = {
                "message": (
                    "mib", {
                        "systemFrameNumber": (sfn, 6),
                        "subCarrierSpacingCommon": scs_common,
                        "ssb-SubcarrierOffset": ssb_offset,
                        "dmrs-TypeA-Position": dmrs_pos,
                        "pdcch-ConfigSIB1": {
                            "controlResourceSetZero": 0,
                            "searchSpaceZero": 0,
                        },
                        "cellBarred": cell_barred,
                        "intraFreqReselection": intra_freq,
                        "spare": (0, 1),
                    }
                )
            }
            try:
                return self._asn1.encode("BCCH-BCH-Message", mib_msg)
            except Exception as exc:
                logger.debug("asn1tools MIB encode failed: %s", exc)

        # Fallback: minimal pre-computed MIB bytes (cell not barred, SCS 15kHz)
        # Generated by tools/gen_sib1_hex.cpp using UERANSIM's ASN.1 encoder
        return bytes.fromhex("000004")

    def build_sib1(
        self,
        mcc: str = "286",
        mnc: str = "93",
        tac: int = 1,
        cell_identity: int = 1,
    ) -> bytes:
        """Encode a SIB1 (BCCH-DL-SCH-Message → SIB1)."""
        if self._asn1 is not None:
            # Build PLMN identity
            mcc_digits = [int(d) for d in mcc]
            mnc_digits = [int(d) for d in mnc]
            plmn = {"mcc": mcc_digits, "mnc": mnc_digits}

            sib1_val = {
                "cellSelectionInfo": {
                    "q-RxLevMin": -70,
                },
                "cellAccessRelatedInfo": {
                    "plmn-IdentityList": [
                        {
                            "plmn-IdentityList": [plmn],
                            "trackingAreaCode": (tac, 24),
                            "cellIdentity": (cell_identity, 36),
                            "cellReservedForOperatorUse": "notReserved",
                        }
                    ]
                },
            }
            sib1_msg = {"message": ("c1", ("systemInformationBlockType1", sib1_val))}
            try:
                return self._asn1.encode("BCCH-DL-SCH-Message", sib1_msg)
            except Exception as exc:
                logger.debug("asn1tools SIB1 encode failed: %s", exc)

        # Fallback: minimal pre-computed SIB1 bytes for PLMN 286/93, TAC=1, NCI=1
        # Generated by tools/gen_sib1_hex.cpp using UERANSIM's ASN.1 encoder
        return bytes.fromhex("400008250c930000010000000018")

    @staticmethod
    def build_sib19(
        *,
        ephemeris_type: int = 0,
        position_x: float = 0.0,
        position_y: float = 0.0,
        position_z: float = 0.0,
        velocity_vx: float = 0.0,
        velocity_vy: float = 0.0,
        velocity_vz: float = 0.0,
        semi_major_axis: int = 0,
        eccentricity: int = 0,
        periapsis: int = 0,
        longitude: int = 0,
        inclination: int = 0,
        mean_anomaly: int = 0,
        epoch_time: int = 0,
        k_offset: int = 0,
        ta_common: int = 0,
        ta_common_drift: int = 0,
        ta_common_drift_variation: int = -1,
        ul_sync_validity: int = -1,
        cell_specific_koffset: int = -1,
        distance_thresh: float = -1.0,
        ntn_polarization: int = -1,
        ta_drift: int = -(2**31),
    ) -> bytes:
        """Build a binary SIB19 PDU for the DL_SIB19 custom channel.

        Binary format (little-endian, 104 bytes total):
          [0]     ephemeris_type (uint8: 0=posVel, 1=orbital)
          [1..3]  reserved
          [4..51] ephemeris block (48 bytes)
          [52..103] common fields

        Parameters
        ----------
        ephemeris_type : 0 = position/velocity, 1 = orbital parameters.
        position_x/y/z : ECEF meters (for ephemeris_type=0).
        velocity_vx/vy/vz : m/s (for ephemeris_type=0).
        semi_major_axis .. mean_anomaly : Keplerian (for ephemeris_type=1).
        epoch_time : 10-ms steps (SFN-based).
        k_offset : ms — scheduling offset.
        ta_common : T_c units.
        ta_common_drift : T_c/s.
        ta_common_drift_variation : T_c/s²; -1 = not present.
        ul_sync_validity : seconds; -1 = not present.
        cell_specific_koffset : -1 = not present.
        distance_thresh : meters; <0 = not present.
        ntn_polarization : 0=RHCP, 1=LHCP, 2=LINEAR, -1=absent.
        ta_drift : T_c/s; -(2**31) = not present.
        """
        import struct
        buf = bytearray(104)

        # Header
        struct.pack_into("<B3x", buf, 0, ephemeris_type)

        # Ephemeris block (48 bytes at offset 4)
        if ephemeris_type == 0:
            struct.pack_into("<6d", buf, 4,
                             position_x, position_y, position_z,
                             velocity_vx, velocity_vy, velocity_vz)
        else:
            struct.pack_into("<q5i", buf, 4,
                             semi_major_axis, eccentricity, periapsis,
                             longitude, inclination, mean_anomaly)
            # remaining 20 bytes of the 48-byte block stay zero (padding)

        # Common fields (offset 52)
        struct.pack_into("<q", buf, 52, epoch_time)
        struct.pack_into("<i", buf, 60, k_offset)
        struct.pack_into("<q", buf, 64, ta_common)
        struct.pack_into("<i", buf, 72, ta_common_drift)
        struct.pack_into("<i", buf, 76, ta_common_drift_variation)
        struct.pack_into("<i", buf, 80, ul_sync_validity)
        struct.pack_into("<i", buf, 84, cell_specific_koffset)
        struct.pack_into("<d", buf, 88, distance_thresh)
        struct.pack_into("<i", buf, 96, ntn_polarization)
        struct.pack_into("<i", buf, 100, ta_drift)

        return bytes(buf)

    def build_rrc_setup(self, transaction_id: int = 0) -> bytes:
        """Encode an RRCSetup (DL-CCCH-Message)."""
        if self._asn1 is not None:
            rrc_setup = {
                "message": (
                    "c1", (
                        "rrcSetup",
                        {
                            "rrc-TransactionIdentifier": transaction_id,
                            "criticalExtensions": (
                                "rrcSetup",
                                {
                                    "radioBearerConfig": {
                                        # Minimal — no SRBs or DRBs to add
                                    },
                                    "masterCellGroup": b"",
                                },
                            ),
                        },
                    )
                )
            }
            try:
                return self._asn1.encode("DL-CCCH-Message", rrc_setup)
            except Exception as exc:
                logger.debug("asn1tools RRCSetup encode failed: %s", exc)

        # Fallback: minimal pre-computed RRCSetup (transaction_id=0)
        # Generated by tools/gen_sib1_hex.cpp using UERANSIM's ASN.1 encoder
        return bytes.fromhex("2000080000")

    def build_dl_information_transfer(
        self,
        nas_pdu: bytes,
        transaction_id: int = 0,
    ) -> bytes:
        """Encode a DLInformationTransfer (DL-DCCH-Message) wrapping a NAS PDU."""
        if self._asn1 is not None:
            msg = {
                "message": (
                    "c1", (
                        "dlInformationTransfer",
                        {
                            "rrc-TransactionIdentifier": transaction_id,
                            "criticalExtensions": (
                                "dlInformationTransfer",
                                {"dedicatedNAS-Message": nas_pdu},
                            ),
                        },
                    )
                )
            }
            try:
                return self._asn1.encode("DL-DCCH-Message", msg)
            except Exception as exc:
                logger.debug("asn1tools DLInformationTransfer encode failed: %s", exc)

        # Fallback: UPER bit-level encoding of DL-DCCH-Message
        # Structure: c1(0) | index=5(0101) | tid(2b) | critExt=IEs(0)
        #            | NAS-present(1) | late-absent(0) | nonCrit-absent(0)
        #            | length-determinant | NAS bytes | zero-padding
        return _uper_dl_info_transfer(transaction_id, nas_pdu)

    def build_rrc_reconfiguration(
        self,
        transaction_id: int = 0,
        meas_objects: Optional[List[Dict]] = None,
        report_configs: Optional[List[Dict]] = None,
        meas_ids: Optional[List[Dict]] = None,
    ) -> bytes:
        """Encode an RRCReconfiguration (DL-DCCH-Message) with a measConfig.

        Each element in *meas_objects* is ``{"id": int, "ssbFreq": int}``.
        Each element in *report_configs* is::

            {"id": int, "event": "a2"|"a3"|"a5",
             "a2Threshold": int, "a3Offset": int,
             "a5Threshold1": int, "a5Threshold2": int,
             "hysteresis": int, "timeToTrigger": int,
             "maxReportCells": int}

        Each element in *meas_ids* is ``{"measId": int, "measObjectId": int,
        "reportConfigId": int}``.
        """
        if meas_objects is None:
            meas_objects = [{"id": 1, "ssbFreq": 632628}]
        if report_configs is None:
            report_configs = [
                {"id": 1, "event": "a3", "a3Offset": 6,
                 "hysteresis": 2, "timeToTrigger": 640, "maxReportCells": 8}
            ]
        if meas_ids is None:
            meas_ids = [{"measId": 1, "measObjectId": 1, "reportConfigId": 1}]

        if self._asn1 is not None:
            try:
                return self._asn1_rrc_reconfig(
                    transaction_id, meas_objects, report_configs, meas_ids
                )
            except Exception as exc:
                logger.debug("asn1tools RRCReconfiguration encode failed: %s", exc)

        # Fallback: return a bytes-level stub
        return self._fallback_rrc_reconfig(
            transaction_id, meas_objects, report_configs, meas_ids
        )

    # ------------------------------------------------------------------
    #  Handover: CellGroupConfig + RRCReconfiguration with sync
    # ------------------------------------------------------------------

    def build_cell_group_config_handover(
        self,
        target_pci: int = 2,
        new_crnti: int = 0x1234,
        t304_ms: int = 1000,
    ) -> bytes:
        """UPER-encode a CellGroupConfig containing ReconfigurationWithSync.

        This is the content of the ``masterCellGroup`` OCTET STRING inside
        RRCReconfiguration-v1530-IEs that triggers a handover in the UE.

        Parameters
        ----------
        target_pci : int
            Physical Cell Identity of the target cell (0-1007).
        new_crnti : int
            The C-RNTI assigned by the target cell (0-65535).
        t304_ms : int
            Handover supervision timer in milliseconds.
        """
        t304_str = T304_MS_TO_ASN1_STR.get(t304_ms, "ms1000")

        if self._asn1 is not None:
            cell_group = {
                "cellGroupId": 0,
                "spCellConfig": {
                    "reconfigurationWithSync": {
                        "spCellConfigCommon": {
                            "physCellId": target_pci,
                            "dmrs-TypeA-Position": "pos2",
                            "ss-PBCH-BlockPower": 0,
                        },
                        "newUE-Identity": new_crnti,
                        "t304": t304_str,
                    },
                },
            }
            try:
                return self._asn1.encode("CellGroupConfig", cell_group)
            except Exception as exc:
                logger.debug("asn1tools CellGroupConfig encode failed: %s", exc)

        # Fallback: return empty bytes (the UE will fail to decode but we
        # can still test the signaling chain)
        logger.warning(
            "Using fallback CellGroupConfig — install asn1tools for proper encoding"
        )
        return b""

    def build_rrc_reconfiguration_with_sync(
        self,
        transaction_id: int = 0,
        target_pci: int = 2,
        new_crnti: int = 0x1234,
        t304_ms: int = 1000,
    ) -> bytes:
        """Build an RRCReconfiguration (DL-DCCH) that triggers a handover.

        The message carries masterCellGroup in the v1530 extension,
        containing a CellGroupConfig with ReconfigurationWithSync.
        """
        mcg_bytes = self.build_cell_group_config_handover(
            target_pci, new_crnti, t304_ms
        )

        if self._asn1 is not None:
            msg = {
                "message": (
                    "c1", (
                        "rrcReconfiguration",
                        {
                            "rrc-TransactionIdentifier": transaction_id,
                            "criticalExtensions": (
                                "rrcReconfiguration",
                                {
                                    "nonCriticalExtension": {
                                        "masterCellGroup": mcg_bytes,
                                    }
                                },
                            ),
                        },
                    )
                )
            }
            try:
                return self._asn1.encode("DL-DCCH-Message", msg)
            except Exception as exc:
                logger.debug(
                    "asn1tools RRCReconfiguration (handover) encode failed: %s", exc
                )

        # Fallback stub
        logger.warning(
            "Using fallback RRCReconfiguration (handover) — install asn1tools"
        )
        return b"\x04\x00\x00"

    def build_rrc_release(self, transaction_id: int = 0) -> bytes:
        """Encode an RRCRelease (DL-DCCH-Message)."""
        if self._asn1 is not None:
            msg = {
                "message": (
                    "c1", (
                        "rrcRelease",
                        {
                            "rrc-TransactionIdentifier": transaction_id,
                            "criticalExtensions": ("rrcRelease", {}),
                        },
                    )
                )
            }
            try:
                return self._asn1.encode("DL-DCCH-Message", msg)
            except Exception as exc:
                logger.debug("asn1tools RRCRelease failed: %s", exc)

        return _uper_rrc_release(transaction_id)

    # ------------------------------------------------------------------
    #  UL message decoders
    # ------------------------------------------------------------------

    def decode_ul_ccch(self, data: bytes) -> Dict[str, Any]:
        """Decode a UL-CCCH-Message (e.g. RRCSetupRequest)."""
        if self._asn1 is not None:
            try:
                return self._asn1.decode("UL-CCCH-Message", data)
            except Exception as exc:
                logger.debug("asn1tools UL-CCCH decode failed: %s", exc)

        return self._fallback_decode_ul_ccch(data)

    def decode_ul_dcch(self, data: bytes) -> Dict[str, Any]:
        """Decode a UL-DCCH-Message (e.g. RRCSetupComplete, MeasurementReport)."""
        if self._asn1 is not None:
            try:
                return self._asn1.decode("UL-DCCH-Message", data)
            except Exception as exc:
                logger.debug("asn1tools UL-DCCH decode failed: %s", exc)

        return self._fallback_decode_ul_dcch(data)

    # ------------------------------------------------------------------
    #  asn1tools helpers
    # ------------------------------------------------------------------

    _TTT_TABLE = [0, 40, 64, 80, 100, 128, 160, 256, 320, 480, 512, 640, 1024, 1280, 2560, 5120]

    def _ttt_to_enum(self, ms: int) -> str:
        """Map timeToTrigger ms value to the ASN.1 enum string."""
        idx = 0
        for i, v in enumerate(self._TTT_TABLE):
            if v <= ms:
                idx = i
        return f"ms{self._TTT_TABLE[idx]}"

    def _event_to_asn1(self, cfg: Dict) -> Dict:
        """Convert a report config dict to ASN.1 EventTriggerConfig structure.

        In the 3GPP ASN.1 (TS 38.331), hysteresis and timeToTrigger are
        per-event fields inside each eventXxx SEQUENCE, not at the
        EventTriggerConfig level.
        """
        event = cfg.get("event", "a3")
        hyst = cfg.get("hysteresis", 2)
        ttt = cfg.get("timeToTrigger", 640)
        ttt_str = self._ttt_to_enum(ttt)
        hyst_val = hyst * 2  # ASN.1 uses 0.5 dB steps

        if event == "a2":
            threshold = cfg.get("a2Threshold", -110)
            rsrp_range = max(0, min(127, threshold + 156))
            event_id = ("eventA2", {
                "a2-Threshold": ("rsrp", rsrp_range),
                "reportOnLeave": False,
                "hysteresis": hyst_val,
                "timeToTrigger": ttt_str,
            })
        elif event == "a5":
            t1 = cfg.get("a5Threshold1", -110)
            t2 = cfg.get("a5Threshold2", -100)
            r1 = max(0, min(127, t1 + 156))
            r2 = max(0, min(127, t2 + 156))
            event_id = ("eventA5", {
                "a5-Threshold1": ("rsrp", r1),
                "a5-Threshold2": ("rsrp", r2),
                "reportOnLeave": False,
                "hysteresis": hyst_val,
                "timeToTrigger": ttt_str,
                "useWhiteCellList": False,
            })
        else:  # a3
            a3off = cfg.get("a3Offset", 6)
            event_id = ("eventA3", {
                "a3-Offset": ("rsrp", a3off * 2),
                "reportOnLeave": False,
                "hysteresis": hyst_val,
                "timeToTrigger": ttt_str,
                "useWhiteCellList": False,
            })

        return {
            "eventId": event_id,
            "rsType": "ssb",
            "reportInterval": "ms1024",
            "reportAmount": "r1",
            "reportQuantityCell": {"rsrp": True, "rsrq": False, "sinr": False},
            "maxReportCells": cfg.get("maxReportCells", 8),
            "includeBeamMeasurements": False,
        }

    def _asn1_rrc_reconfig(self, txn_id, meas_objs, report_cfgs, meas_ids_list):
        meas_obj_list = []
        for mo in meas_objs:
            meas_obj_list.append({
                "measObjectId": mo["id"],
                "measObject": ("measObjectNR", {
                    "ssbFrequency": mo.get("ssbFreq", 632628),
                    "ssbSubcarrierSpacing": "kHz15",
                    "smtc1": {
                        "periodicityAndOffset": ("sf20", 0),
                        "duration": "sf1",
                    },
                    "referenceSignalConfig": {},
                    "offsetMO": {},
                    "quantityConfigIndex": 1,
                }),
            })

        rc_list = []
        for rc in report_cfgs:
            rc_list.append({
                "reportConfigId": rc["id"],
                "reportConfig": (
                    "reportConfigNR",
                    {
                        "reportType": (
                            "eventTriggered",
                            self._event_to_asn1(rc),
                        ),
                    },
                ),
            })

        mid_list = []
        for mi in meas_ids_list:
            mid_list.append({
                "measId": mi["measId"],
                "measObjectId": mi["measObjectId"],
                "reportConfigId": mi["reportConfigId"],
            })

        meas_config = {}
        if meas_obj_list:
            meas_config["measObjectToAddModList"] = meas_obj_list
        if rc_list:
            meas_config["reportConfigToAddModList"] = rc_list
        if mid_list:
            meas_config["measIdToAddModList"] = mid_list

        msg = {
            "message": (
                "c1", (
                    "rrcReconfiguration",
                    {
                        "rrc-TransactionIdentifier": txn_id,
                        "criticalExtensions": (
                            "rrcReconfiguration",
                            {"measConfig": meas_config},
                        ),
                    },
                )
            )
        }
        return self._asn1.encode("DL-DCCH-Message", msg)

    # ------------------------------------------------------------------
    #  Fallback (no asn1tools) helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _fallback_decode_ul_ccch(data: bytes) -> Dict[str, Any]:
        """Best-effort decode of UL-CCCH without asn1tools."""
        result: Dict[str, Any] = {"raw": data.hex(), "_fallback": True}

        if len(data) >= 1:
            # First bits distinguish message type
            # Bit 0 of first byte: 0 = c1 branch
            first_byte = data[0]
            # In UPER, UL-CCCH-Message has c1 as first choice
            # c1 choices: rrcSetupRequest (0), rrcResumeRequest (1),
            #             rrcReestablishmentRequest (2), rrcSystemInfoRequest (3)
            choice_bits = (first_byte >> 6) & 0x03
            choices = {
                0: "rrcSetupRequest",
                1: "rrcResumeRequest",
                2: "rrcReestablishmentRequest",
                3: "rrcSystemInfoRequest",
            }
            result["message_type"] = choices.get(choice_bits, f"unknown({choice_bits})")

        return result

    @staticmethod
    def _fallback_decode_ul_dcch(data: bytes) -> Dict[str, Any]:
        """Best-effort decode of UL-DCCH without asn1tools."""
        result: Dict[str, Any] = {"raw": data.hex(), "_fallback": True}

        if len(data) >= 1:
            first_byte = data[0]
            # UL-DCCH c1 choices (4 bits):
            # 0=measurementReport, 1=rrcReconfigurationComplete,
            # 2=rrcSetupComplete, ...
            choice_bits = (first_byte >> 4) & 0x0F
            choices = {
                0: "measurementReport",
                1: "rrcReconfigurationComplete",
                2: "rrcSetupComplete",
                3: "securityModeComplete_stub",
                4: "securityModeFailure_stub",
                5: "ulInformationTransfer",
                # additional entries abbreviated
            }
            result["message_type"] = choices.get(choice_bits, f"unknown({choice_bits})")

            # For measurementReport, try to extract measId
            if choice_bits == 0 and len(data) >= 3:
                result["likely_meas_report"] = True

            # For ulInformationTransfer, try to extract NAS PDU
            if choice_bits == 5 and len(data) > 4:
                result["likely_nas_pdu"] = data[2:]

        return result

    @staticmethod
    def _fallback_rrc_reconfig(txn_id, meas_objs, report_cfgs, meas_ids_list) -> bytes:
        """Produce a stub RRCReconfiguration for fallback mode.

        NOTE: Without proper ASN.1 UPER encoding, this stub may not be
        parseable by the real UE.  Install ``asn1tools`` for full support.
        """
        logger.warning(
            "Using fallback RRCReconfiguration — install asn1tools for proper encoding"
        )
        return b"\x04\x00\x00"   # placeholder
