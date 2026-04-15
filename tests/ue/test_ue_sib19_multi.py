from __future__ import annotations

import struct
import time

from .conftest import ue_binary_exists
from .harness.rls_protocol import RrcChannel


def _build_sib19_multi_payload(entries: list[dict]) -> bytes:
    # Versioned multi-entry format used by gNB SIB19 broadcaster.
    # Header: [0]=version(2), [1]=ephemerisType(0), [2..3]=reserved, [4..7]=entryCount(u32)
    payload = bytearray(8 + len(entries) * 96)
    payload[0] = 2
    payload[1] = 0
    struct.pack_into("<I", payload, 4, len(entries))

    for i, entry in enumerate(entries):
        base = 8 + i * 96
        struct.pack_into("<i", payload, base, int(entry["pci"]))
        struct.pack_into("<d", payload, base + 4, float(entry["x"]))
        struct.pack_into("<d", payload, base + 12, float(entry["y"]))
        struct.pack_into("<d", payload, base + 20, float(entry["z"]))
        struct.pack_into("<d", payload, base + 28, float(entry["vx"]))
        struct.pack_into("<d", payload, base + 36, float(entry["vy"]))
        struct.pack_into("<d", payload, base + 44, float(entry["vz"]))
        struct.pack_into("<q", payload, base + 52, int(entry["epoch10ms"]))
        struct.pack_into("<i", payload, base + 60, int(entry["kOffset"]))
        struct.pack_into("<q", payload, base + 64, int(entry["taCommon"]))
        struct.pack_into("<i", payload, base + 72, int(entry["taCommonDrift"]))
        struct.pack_into("<i", payload, base + 76, int(entry["taCommonDriftVariation"]))
        struct.pack_into("<i", payload, base + 80, int(entry["ulSyncValidity"]))
        struct.pack_into("<i", payload, base + 84, int(entry["cellSpecificKoffset"]))
        struct.pack_into("<i", payload, base + 88, int(entry["polarization"]))
        struct.pack_into("<i", payload, base + 92, int(entry["taDrift"]))

    return bytes(payload)


@ue_binary_exists
class TestUeSib19Multi:
    def test_ue_parses_multi_entry_sib19_and_keeps_pci_map(self, ue_process, fake_gnb):
        ue_process.start()
        assert fake_gnb.wait_for_heartbeat(timeout_s=10)

        # Ensure the UE has discovered the serving cell first.
        fake_gnb.broadcast_system_information()
        assert ue_process.wait_for_log(r"cellId=1", timeout_s=8) is not None

        epoch10ms = int(time.time() * 100)
        pdu = _build_sib19_multi_payload([
            {
                "pci": 1,
                "x": 1000.0,
                "y": 2000.0,
                "z": 3000.0,
                "vx": 1.0,
                "vy": 2.0,
                "vz": 3.0,
                "epoch10ms": epoch10ms,
                "kOffset": 160,
                "taCommon": 321,
                "taCommonDrift": 7,
                "taCommonDriftVariation": 11,
                "ulSyncValidity": 900,
                "cellSpecificKoffset": 44,
                "polarization": 2,
                "taDrift": 19,
            },
            {
                "pci": 222,
                "x": 1100.0,
                "y": 2200.0,
                "z": 3300.0,
                "vx": -1.0,
                "vy": -2.0,
                "vz": -3.0,
                "epoch10ms": epoch10ms,
                "kOffset": 160,
                "taCommon": 321,
                "taCommonDrift": 7,
                "taCommonDriftVariation": 11,
                "ulSyncValidity": 900,
                "cellSpecificKoffset": 44,
                "polarization": 2,
                "taDrift": 19,
            },
        ])

        fake_gnb.send_rrc(RrcChannel.DL_SIB19, pdu)

        # Parser log includes the count from entriesByPci, proving multi-entry map storage.
        assert ue_process.wait_for_log(r"SIB19 received for cell 1: multi-entry count=2 selectedPci=1", timeout_s=8)
