#!/usr/bin/env python3
"""
Measure NR handover latency from a PCAP file, broken into three phases:

  Phase 1 (T1): Measurement Report (UE → serving gNB)
                  → RRC Reconfiguration (serving gNB → UE)
  Phase 2 (T2): RRC Reconfiguration (serving gNB → UE)
                  → RRC Reconfiguration Complete (UE → target gNB)
  Total:        Measurement Report → RRC Reconfiguration Complete

UEs are identified by source UDP port.  All handover triplets found in the
trace are reported.  Measurement Reports with no complete triplet are counted
as failed/incomplete handovers.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


DEFAULT_GNB_PORT = 4997


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

@dataclass
class HandoverEvent:
    ue_port: int
    index: int                   # 1-based count within this UE
    meas_report_time: float      # epoch seconds — UE → serving gNB
    rrc_reconfig_time: float     # epoch seconds — serving gNB → UE
    rrc_complete_time: float     # epoch seconds — UE → target gNB

    @property
    def t1_ms(self) -> float:
        """MR → Reconfig: gNB decision + handover command delivery."""
        return (self.rrc_reconfig_time - self.meas_report_time) * 1000

    @property
    def t2_ms(self) -> float:
        """Reconfig → Complete: UE processing + random access to target cell."""
        return (self.rrc_complete_time - self.rrc_reconfig_time) * 1000

    @property
    def total_ms(self) -> float:
        return (self.rrc_complete_time - self.meas_report_time) * 1000


# States for per-UE handover state machine.
_IDLE = "IDLE"
_WAIT_RECONFIG = "WAIT_RECONFIG"
_WAIT_COMPLETE = "WAIT_COMPLETE"


@dataclass
class UEState:
    port: int
    handovers: list[HandoverEvent] = field(default_factory=list)
    failed_reports: int = 0
    _state: str = field(default=_IDLE, repr=False)
    _mr_time: Optional[float] = field(default=None, repr=False)
    _reconfig_time: Optional[float] = field(default=None, repr=False)

    def on_meas_report(self, ts: float) -> None:
        if self._state != _IDLE:
            # Previous attempt never completed.
            self.failed_reports += 1
        self._mr_time = ts
        self._reconfig_time = None
        self._state = _WAIT_RECONFIG

    def on_rrc_reconfig(self, ts: float) -> None:
        if self._state == _WAIT_RECONFIG:
            self._reconfig_time = ts
            self._state = _WAIT_COMPLETE
        # In any other state this is an orphan (e.g., initial-attach reconfig) — ignore.

    def on_rrc_complete(self, ts: float) -> None:
        if self._state == _WAIT_COMPLETE:
            self.handovers.append(HandoverEvent(
                ue_port=self.port,
                index=len(self.handovers) + 1,
                meas_report_time=self._mr_time,        # type: ignore[arg-type]
                rrc_reconfig_time=self._reconfig_time,  # type: ignore[arg-type]
                rrc_complete_time=ts,
            ))
            self._state = _IDLE
            self._mr_time = None
            self._reconfig_time = None
        elif self._state == _WAIT_RECONFIG:
            # Complete arrived without seeing a Reconfig — packet loss or out-of-order.
            self.failed_reports += 1
            self._state = _IDLE
            self._mr_time = None
        # In IDLE state this is an orphan (initial attach) — ignore.

    def finalize(self) -> None:
        if self._state != _IDLE:
            self.failed_reports += 1
            self._state = _IDLE
            self._mr_time = None
            self._reconfig_time = None


# ---------------------------------------------------------------------------
# PCAP extraction
# ---------------------------------------------------------------------------

def extract_packets(
    pcap_path: str,
    gnb_port: int,
) -> list[tuple[float, int, str]]:
    """
    Return (timestamp, ue_port, msg_type) for all relevant RRC packets.

    msg_type is one of: 'meas_report', 'rrc_reconfig', 'rrc_complete'.
    ue_port is always the UE's port regardless of direction.

    Uplink   (UE→gNB): srcport=UE_port, dstport=gnb_port
    Downlink (gNB→UE): srcport=gnb_port, dstport=UE_port
    """
    cmd = [
        "tshark",
        "-r", pcap_path,
        "-Y", "nr-rrc",
        "-T", "fields",
        "-e", "frame.time_epoch",
        "-e", "udp.srcport",
        "-e", "udp.dstport",
        "-e", "_ws.col.Info",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0 and not proc.stdout.strip():
        print(f"tshark failed:\n{proc.stderr}", file=sys.stderr)
        sys.exit(1)

    packets: list[tuple[float, int, str]] = []
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        ts_s, src_s, dst_s, info = parts[0], parts[1], parts[2], parts[3]
        try:
            ts = float(ts_s)
            src_port = int(src_s)
            dst_port = int(dst_s)
        except ValueError:
            continue

        if dst_port == gnb_port:
            # Uplink: UE → gNB
            ue_port = src_port
            if "Measurement Report" in info:
                packets.append((ts, ue_port, "meas_report"))
            elif "RRC Reconfiguration Complete" in info:
                packets.append((ts, ue_port, "rrc_complete"))

        elif src_port == gnb_port:
            # Downlink: gNB → UE
            ue_port = dst_port
            if "RRC Reconfiguration" in info and "Complete" not in info:
                packets.append((ts, ue_port, "rrc_reconfig"))

    return packets


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

def analyze(pcap_path: str, gnb_port: int) -> dict[int, UEState]:
    packets = extract_packets(pcap_path, gnb_port)
    ues: dict[int, UEState] = {}

    for ts, ue_port, msg_type in packets:
        if ue_port not in ues:
            ues[ue_port] = UEState(port=ue_port)
        state = ues[ue_port]
        if msg_type == "meas_report":
            state.on_meas_report(ts)
        elif msg_type == "rrc_reconfig":
            state.on_rrc_reconfig(ts)
        else:
            state.on_rrc_complete(ts)

    for state in ues.values():
        state.finalize()

    return ues


# ---------------------------------------------------------------------------
# Output formatters
# ---------------------------------------------------------------------------

def _sorted_handovers(ues: dict[int, UEState]) -> list[HandoverEvent]:
    events = [h for s in ues.values() for h in s.handovers]
    events.sort(key=lambda h: h.meas_report_time)
    return events


def _summary_stats(values: list[float]) -> dict:
    if not values:
        return {}
    return {
        "avg": sum(values) / len(values),
        "min": min(values),
        "max": max(values),
    }


def output_stdout(ues: dict[int, UEState]) -> None:
    events = _sorted_handovers(ues)
    total_failed = sum(s.failed_reports for s in ues.values())

    # Column widths: UE Port, HO#, T1, T2, Total
    c = [10, 5, 12, 12, 12]
    sep = "  "
    hdr = (
        f"{'UE Port':<{c[0]}}{sep}{'HO#':<{c[1]}}{sep}"
        f"{'T1 MR→Reconfig':>{c[2]}}{sep}{'T2 Reconfig→Cmp':>{c[3]}}{sep}"
        f"{'Total (ms)':>{c[4]}}"
    )
    print(hdr)
    print("-" * len(hdr))

    for h in events:
        print(
            f"{h.ue_port:<{c[0]}}{sep}{h.index:<{c[1]}}{sep}"
            f"{h.t1_ms:>{c[2]}.3f}{sep}{h.t2_ms:>{c[3]}.3f}{sep}"
            f"{h.total_ms:>{c[4]}.3f}"
        )

    if not events:
        print("No complete handover triplets found.")

    print()
    t1s = [h.t1_ms for h in events]
    t2s = [h.t2_ms for h in events]
    tots = [h.total_ms for h in events]

    print(f"Total UEs:              {len(ues)}")
    print(f"Total handovers:        {len(events)}")
    if events:
        print(f"                        {'avg':>10}  {'min':>10}  {'max':>10}")
        print(f"  T1 MR→Reconfig (ms): {sum(t1s)/len(t1s):>10.3f}  {min(t1s):>10.3f}  {max(t1s):>10.3f}")
        print(f"  T2 Reconfig→Cmp(ms): {sum(t2s)/len(t2s):>10.3f}  {min(t2s):>10.3f}  {max(t2s):>10.3f}")
        print(f"  Total (ms):          {sum(tots)/len(tots):>10.3f}  {min(tots):>10.3f}  {max(tots):>10.3f}")
    print(f"Failed/unmatched MRs:   {total_failed}")

    if total_failed:
        print()
        print("UEs with failed/unmatched Measurement Reports:")
        for port in sorted(ues):
            s = ues[port]
            if s.failed_reports:
                print(f"  port {port}: {s.failed_reports} unmatched")


def output_csv(ues: dict[int, UEState], path: Optional[str]) -> None:
    events = _sorted_handovers(ues)

    dest: io.IOBase
    if path:
        dest = open(path, "w", newline="")
    else:
        dest = sys.stdout  # type: ignore[assignment]

    writer = csv.writer(dest)
    writer.writerow([
        "ue_port", "handover_index",
        "meas_report_time_s", "rrc_reconfig_time_s", "rrc_complete_time_s",
        "t1_mr_to_reconfig_ms", "t2_reconfig_to_complete_ms", "total_ms",
    ])
    for h in events:
        writer.writerow([
            h.ue_port, h.index,
            f"{h.meas_report_time:.6f}",
            f"{h.rrc_reconfig_time:.6f}",
            f"{h.rrc_complete_time:.6f}",
            f"{h.t1_ms:.3f}",
            f"{h.t2_ms:.3f}",
            f"{h.total_ms:.3f}",
        ])

    for port in sorted(ues):
        s = ues[port]
        if s.failed_reports:
            writer.writerow([port, "FAILED", "", "", "", "", "", s.failed_reports])

    if path:
        dest.close()  # type: ignore[union-attr]
        print(f"CSV written to {path}")


def output_json(ues: dict[int, UEState], path: Optional[str]) -> None:
    t1s = [h.t1_ms for s in ues.values() for h in s.handovers]
    t2s = [h.t2_ms for s in ues.values() for h in s.handovers]
    tots = [h.total_ms for s in ues.values() for h in s.handovers]

    def _stats(vals: list[float]) -> dict:
        if not vals:
            return {}
        return {
            "avg_ms": round(sum(vals) / len(vals), 3),
            "min_ms": round(min(vals), 3),
            "max_ms": round(max(vals), 3),
        }

    summary: dict = {
        "total_ues": len(ues),
        "total_handovers": len(tots),
        "total_failed_reports": sum(s.failed_reports for s in ues.values()),
        "t1_mr_to_reconfig": _stats(t1s),
        "t2_reconfig_to_complete": _stats(t2s),
        "total": _stats(tots),
    }

    ue_data = {}
    for port in sorted(ues):
        s = ues[port]
        ue_data[str(port)] = {
            "handovers": [
                {
                    "index": h.index,
                    "meas_report_time_s": h.meas_report_time,
                    "rrc_reconfig_time_s": h.rrc_reconfig_time,
                    "rrc_complete_time_s": h.rrc_complete_time,
                    "t1_ms": round(h.t1_ms, 3),
                    "t2_ms": round(h.t2_ms, 3),
                    "total_ms": round(h.total_ms, 3),
                }
                for h in s.handovers
            ],
            "failed_reports": s.failed_reports,
        }

    result = {"summary": summary, "ues": ue_data}
    text = json.dumps(result, indent=2)

    if path:
        Path(path).write_text(text)
        print(f"JSON written to {path}")
    else:
        print(text)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Measure NR handover latency from a PCAP file, broken into three phases: "
            "Measurement Report → RRC Reconfiguration (T1), "
            "RRC Reconfiguration → RRC Reconfiguration Complete (T2), "
            "and total.  UEs are identified by source UDP port."
        )
    )
    parser.add_argument("pcap", help="Path to the PCAP file")
    parser.add_argument(
        "--format", choices=["json", "csv", "stdout"], default="stdout",
        metavar="FORMAT",
        help="Output format: stdout (default), csv, or json",
    )
    parser.add_argument(
        "--output", "-o", metavar="FILE",
        help="Write csv/json output to FILE instead of stdout",
    )
    parser.add_argument(
        "--gnb-port", type=int, default=DEFAULT_GNB_PORT, metavar="PORT",
        help=f"gNB RLS UDP port (default: {DEFAULT_GNB_PORT})",
    )
    args = parser.parse_args()

    if not Path(args.pcap).exists():
        print(f"Error: file not found: {args.pcap}", file=sys.stderr)
        sys.exit(1)

    ues = analyze(args.pcap, args.gnb_port)

    if args.format == "stdout":
        output_stdout(ues)
    elif args.format == "csv":
        output_csv(ues, args.output)
    elif args.format == "json":
        output_json(ues, args.output)


if __name__ == "__main__":
    main()
