"""
Out-of-band measurement injector.

Sends JSON measurement data to the UE over UDP (or writes to a file)
so the UE's measurement evaluation engine sees configurable RSRP/RSRQ
values without needing real radio.

Expected JSON format (per the OOB provider in meas_provider.cpp)::

    {
      "measurements": [
        {"cellId": 1, "rsrp": -85, "rsrq": -10, "sinr": 15},
        {"nci": 36,   "rsrp": -78}
      ]
    }
"""

from __future__ import annotations

import json
import socket
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional

DEFAULT_UDP_ADDR = "127.0.0.1"
DEFAULT_UDP_PORT = 7200


@dataclass
class CellMeas:
    """A single cell measurement entry."""
    cell_id: int = 0       # Internal UERANSIM cell ID (0 → resolve via nci)
    nci: int = 0           # NR Cell Identity
    rsrp: int = -140       # dBm
    rsrq: int = -20        # dB
    sinr: int = -23        # dB

    def to_dict(self) -> dict:
        d: dict = {}
        if self.cell_id:
            d["cellId"] = self.cell_id
        if self.nci:
            d["nci"] = self.nci
        d["rsrp"] = self.rsrp
        d["rsrq"] = self.rsrq
        d["sinr"] = self.sinr
        return d


class MeasurementInjector:
    """Send out-of-band cell measurements to a running UE over UDP."""

    def __init__(
        self,
        target_addr: str = DEFAULT_UDP_ADDR,
        target_port: int = DEFAULT_UDP_PORT,
    ):
        self._addr = target_addr
        self._port = target_port
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._cells: Dict[int, CellMeas] = {}

    def close(self):
        self._sock.close()

    # ------------------------------------------------------------------
    #  Cell management
    # ------------------------------------------------------------------

    def set_cell(
        self,
        cell_id: int = 0,
        nci: int = 0,
        rsrp: int = -140,
        rsrq: int = -20,
        sinr: int = -23,
    ):
        """Add or update a cell measurement entry."""
        key = cell_id or nci
        self._cells[key] = CellMeas(cell_id=cell_id, nci=nci,
                                     rsrp=rsrp, rsrq=rsrq, sinr=sinr)

    def remove_cell(self, cell_id: int = 0, nci: int = 0):
        key = cell_id or nci
        self._cells.pop(key, None)

    def clear(self):
        self._cells.clear()

    # ------------------------------------------------------------------
    #  Convenience setters
    # ------------------------------------------------------------------

    def set_serving_rsrp(self, cell_id: int, rsrp: int):
        """Shortcut: set RSRP for the serving cell."""
        if cell_id in self._cells:
            self._cells[cell_id].rsrp = rsrp
        else:
            self.set_cell(cell_id=cell_id, rsrp=rsrp)

    def set_neighbour(self, nci: int, rsrp: int, rsrq: int = -10, sinr: int = 15):
        """Shortcut: set measurements for a neighbour cell (by NCI)."""
        self.set_cell(nci=nci, rsrp=rsrp, rsrq=rsrq, sinr=sinr)

    # ------------------------------------------------------------------
    #  Sending
    # ------------------------------------------------------------------

    def send(self):
        """Send current cell measurements to the UE."""
        payload = self._build_json()
        self._sock.sendto(payload, (self._addr, self._port))

    def send_measurements(self, cells: List[CellMeas]):
        """One-shot: send an explicit list of CellMeas without storing them."""
        payload = json.dumps(
            {"measurements": [c.to_dict() for c in cells]}
        ).encode("utf-8")
        self._sock.sendto(payload, (self._addr, self._port))

    def send_raw(self, measurements: List[dict]):
        """Send a raw list of measurement dicts."""
        payload = json.dumps({"measurements": measurements}).encode("utf-8")
        self._sock.sendto(payload, (self._addr, self._port))

    def send_repeatedly(self, interval_s: float = 0.5, duration_s: float = 5.0):
        """Send current measurements periodically for *duration_s* seconds."""
        end_time = time.monotonic() + duration_s
        while time.monotonic() < end_time:
            self.send()
            time.sleep(interval_s)

    # ------------------------------------------------------------------
    #  Ramp helpers (for gradual signal changes)
    # ------------------------------------------------------------------

    def ramp_serving_rsrp(
        self,
        cell_id: int,
        start_rsrp: int,
        end_rsrp: int,
        duration_s: float = 3.0,
        step_interval_s: float = 0.3,
    ):
        """Gradually ramp the serving cell RSRP from *start* to *end*."""
        steps = max(1, int(duration_s / step_interval_s))
        delta = (end_rsrp - start_rsrp) / steps

        for i in range(steps + 1):
            rsrp = int(start_rsrp + delta * i)
            self.set_serving_rsrp(cell_id, rsrp)
            self.send()
            time.sleep(step_interval_s)

    # ------------------------------------------------------------------
    #  Internal
    # ------------------------------------------------------------------

    def _build_json(self) -> bytes:
        measurements = [c.to_dict() for c in self._cells.values()]
        return json.dumps({"measurements": measurements}).encode("utf-8")
