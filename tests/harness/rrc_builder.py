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
_ASN1_PATH = Path(__file__).resolve().parents[2] / "tools" / "rrc-15.6.0.asn1"


def _try_compile_asn1():
    """Attempt to compile the NR RRC ASN.1 module via asn1tools.

    Returns the compiled module or *None* on failure.
    """
    try:
        import asn1tools  # type: ignore
    except ImportError:
        logger.info("asn1tools not installed — using pre-computed RRC bytes")
        return None

    if not _ASN1_PATH.exists():
        logger.warning("RRC ASN.1 file not found at %s", _ASN1_PATH)
        return None

    try:
        compiled = asn1tools.compile_files(str(_ASN1_PATH), "uper")
        return compiled
    except Exception as exc:
        logger.warning("Failed to compile RRC ASN.1: %s", exc)
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
                "message": {
                    "mib": {
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
                }
            }
            try:
                return self._asn1.encode("BCCH-BCH-Message", mib_msg)
            except Exception as exc:
                logger.debug("asn1tools MIB encode failed: %s", exc)

        # Fallback: minimal pre-computed MIB bytes (cell not barred, SCS 15kHz)
        return bytes.fromhex("00000000")

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
            sib1_msg = {"message": {"c1": ("systemInformationBlockType1", sib1_val)}}
            try:
                return self._asn1.encode("BCCH-DL-SCH-Message", sib1_msg)
            except Exception as exc:
                logger.debug("asn1tools SIB1 encode failed: %s", exc)

        # Fallback: minimal pre-computed SIB1 bytes
        # This is a simplified encoding — may need updating per ASN.1 schema version
        return bytes.fromhex(
            "40 40 04 08 60 d6 00 80 00 44 02 80 00 00 01 00"
            .replace(" ", "")
        )

    def build_rrc_setup(self, transaction_id: int = 0) -> bytes:
        """Encode an RRCSetup (DL-CCCH-Message)."""
        if self._asn1 is not None:
            rrc_setup = {
                "message": {
                    "c1": (
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
                }
            }
            try:
                return self._asn1.encode("DL-CCCH-Message", rrc_setup)
            except Exception as exc:
                logger.debug("asn1tools RRCSetup encode failed: %s", exc)

        # Fallback: minimal pre-computed RRCSetup
        return bytes.fromhex("2000 0400".replace(" ", ""))

    def build_dl_information_transfer(
        self,
        nas_pdu: bytes,
        transaction_id: int = 0,
    ) -> bytes:
        """Encode a DLInformationTransfer (DL-DCCH-Message) wrapping a NAS PDU."""
        if self._asn1 is not None:
            msg = {
                "message": {
                    "c1": (
                        "dlInformationTransfer",
                        {
                            "rrc-TransactionIdentifier": transaction_id,
                            "criticalExtensions": (
                                "dlInformationTransfer",
                                {"dedicatedNAS-Message": nas_pdu},
                            ),
                        },
                    )
                }
            }
            try:
                return self._asn1.encode("DL-DCCH-Message", msg)
            except Exception as exc:
                logger.debug("asn1tools DLInformationTransfer encode failed: %s", exc)

        # Fallback: manual construction (simplified DL-DCCH frame)
        # DL-DCCH-Message → c1 → dlInformationTransfer
        # This is a simplified byte representation; production use should use asn1tools
        buf = bytearray()
        buf += bytes([0x08])                       # message choice + transaction id
        buf += bytes([transaction_id & 0x03])
        buf += bytes([0x00])                       # criticalExtensions choice
        nas_len = len(nas_pdu)
        buf += nas_len.to_bytes(2, "big")
        buf += nas_pdu
        return bytes(buf)

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
                "message": {
                    "c1": (
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
                }
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
                "message": {
                    "c1": (
                        "rrcRelease",
                        {
                            "rrc-TransactionIdentifier": transaction_id,
                            "criticalExtensions": ("rrcRelease", {}),
                        },
                    )
                }
            }
            try:
                return self._asn1.encode("DL-DCCH-Message", msg)
            except Exception as exc:
                logger.debug("asn1tools RRCRelease failed: %s", exc)

        return bytes.fromhex("1000")

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
        """Convert a report config dict to ASN.1 event trigger structure."""
        event = cfg.get("event", "a3")
        hyst = cfg.get("hysteresis", 2)
        ttt = cfg.get("timeToTrigger", 640)

        if event == "a2":
            threshold = cfg.get("a2Threshold", -110)
            rsrp_range = max(0, min(127, threshold + 156))
            return {
                "eventId": ("eventA2", {
                    "a2-Threshold": ("rsrp", rsrp_range),
                }),
                "rsType": "ssb",
                "reportInterval": "ms1024",
                "reportAmount": "r1",
                "reportQuantityCell": {"rsrp": True, "rsrq": False, "sinr": False},
                "maxReportCells": cfg.get("maxReportCells", 8),
                "hysteresis": hyst * 2,
                "timeToTrigger": self._ttt_to_enum(ttt),
            }
        elif event == "a5":
            t1 = cfg.get("a5Threshold1", -110)
            t2 = cfg.get("a5Threshold2", -100)
            r1 = max(0, min(127, t1 + 156))
            r2 = max(0, min(127, t2 + 156))
            return {
                "eventId": ("eventA5", {
                    "a5-Threshold1": ("rsrp", r1),
                    "a5-Threshold2": ("rsrp", r2),
                }),
                "rsType": "ssb",
                "reportInterval": "ms1024",
                "reportAmount": "r1",
                "reportQuantityCell": {"rsrp": True, "rsrq": False, "sinr": False},
                "maxReportCells": cfg.get("maxReportCells", 8),
                "hysteresis": hyst * 2,
                "timeToTrigger": self._ttt_to_enum(ttt),
            }
        else:  # a3
            a3off = cfg.get("a3Offset", 6)
            return {
                "eventId": ("eventA3", {
                    "a3-Offset": ("rsrp", a3off * 2),
                    "reportOnLeave": False,
                }),
                "rsType": "ssb",
                "reportInterval": "ms1024",
                "reportAmount": "r1",
                "reportQuantityCell": {"rsrp": True, "rsrq": False, "sinr": False},
                "maxReportCells": cfg.get("maxReportCells", 8),
                "hysteresis": hyst * 2,
                "timeToTrigger": self._ttt_to_enum(ttt),
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
                    "quantityConfigIndex": 1,
                }),
            })

        rc_list = []
        for rc in report_cfgs:
            rc_list.append({
                "reportConfigId": rc["id"],
                "reportConfig": (
                    "reportConfigNR",
                    self._event_to_asn1(rc),
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
            "message": {
                "c1": (
                    "rrcReconfiguration",
                    {
                        "rrc-TransactionIdentifier": txn_id,
                        "criticalExtensions": (
                            "rrcReconfiguration",
                            {"measConfig": meas_config},
                        ),
                    },
                )
            }
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
