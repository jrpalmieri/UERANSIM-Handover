#!/usr/bin/env python3
"""
Per-UE RRC state machine analysis of a UERANSIM PCAP file.

Tracks registration, normal handover (HO), conditional handover (CHO), and
disconnect/reconnect (D/R) cycles for every UE discovered in the trace.

Usage:
  Single file:    rrc_state_machine.py ran_trace_1.pcap
  Directory:      rrc_state_machine.py -d /path/to/log/dir
                  Analyses every ran_trace*.pcap in the directory, accumulates
                  results, and presents per-file event tables plus aggregate totals.

State machine:
  RRC_IDLE
    ─ RRC Setup Request (UE→gNB)     → REGISTRATION_STARTED
  REGISTRATION_STARTED
    ─ RRC Setup (gNB→UE)             → REGISTRATION_PENDING
  REGISTRATION_PENDING
    ─ RRC Setup Complete (UE→gNB)    → RRC_CONNECTED  [emit D/R if prior disconnect]
  RRC_CONNECTED
    ─ Measurement Report (UE→gNB)    → POTENTIAL_HO_START
    ─ RRC Reconfig (gNB→UE, no sync) → stay (measurement config update)
    ─ RRC Reconfig Complete to new gNB → stay [emit CHO]
    ─ RRC Release (gNB→UE)           → RRC_IDLE
    ─ Heartbeat ACK dBm < -140       → set prior_disconnect_ts
  POTENTIAL_HO_START
    ─ Measurement Report (UE→gNB)    → stay, count unmatched MR, update timestamp
    ─ RRC Reconfig w/ sync (gNB→UE)  → HO_PENDING
    ─ RRC Reconfig no sync (gNB→UE)  → stay (measurement config, ignore)
  HO_PENDING
    ─ RRC Reconfig Complete (UE→gNB) → RRC_CONNECTED [emit HO]
  Any connected state
    ─ RRC Setup Request (UE→gNB)     → RRC_IDLE (disconnect)
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
from typing import Optional, Union


DEFAULT_GNB_PORT = 4997
DISCONNECT_DBM_THRESHOLD = -140


# ---------------------------------------------------------------------------
# Packet representation
# ---------------------------------------------------------------------------

@dataclass
class Packet:
    ts: float
    src_ip: str
    dst_ip: str
    src_port: int
    dst_port: int
    info: str
    dbm: Optional[int]       # rls.dbm — only present on Heartbeat ACK
    has_sync: bool           # True when RRCReconfiguration contains reconfigurationWithSync IE


def _msg_type(info: str) -> str:
    if "RRC Setup Request" in info:
        return "setup_request"
    if "RRC Setup Complete" in info:
        return "setup_complete"
    if "RRC Setup" in info:
        return "setup"
    if "RRC Reconfiguration Complete" in info:
        return "rrc_complete"
    if "RRC Reconfiguration" in info:
        return "rrc_reconfig"
    if "Measurement Report" in info:
        return "meas_report"
    if "RRC Release" in info:
        return "rrc_release"
    if "Heartbeat ACK" in info:
        return "heartbeat_ack"
    return "other"


# ---------------------------------------------------------------------------
# Events
# ---------------------------------------------------------------------------

@dataclass
class HOEvent:
    ue_port: int
    index: int
    ts: float            # time of Measurement Report
    mr_ts: float
    rws_ts: float
    complete_ts: float

    @property
    def t1_ms(self) -> float:
        return (self.rws_ts - self.mr_ts) * 1000

    @property
    def t2_ms(self) -> float:
        return (self.complete_ts - self.rws_ts) * 1000


@dataclass
class CHOEvent:
    ue_port: int
    index: int
    ts: float
    from_gnb: str
    to_gnb: str


@dataclass
class DREvent:
    ue_port: int
    index: int
    ts: float            # time of reconnect (RRC Setup Complete)
    disconnect_ts: float
    reconnect_ts: float

    @property
    def delta_ms(self) -> float:
        return (self.reconnect_ts - self.disconnect_ts) * 1000


Event = Union[HOEvent, CHOEvent, DREvent]


# ---------------------------------------------------------------------------
# Per-UE state machine
# ---------------------------------------------------------------------------

_IDLE         = "RRC_IDLE"
_REG_STARTED  = "REGISTRATION_STARTED"
_REG_PENDING  = "REGISTRATION_PENDING"
_CONNECTED    = "RRC_CONNECTED"
_POTENTIAL_HO = "POTENTIAL_HO_START"
_HO_PENDING   = "HO_PENDING"

_ACTIVE_STATES = {_CONNECTED, _POTENTIAL_HO, _HO_PENDING}


@dataclass
class UEStateMachine:
    port: int
    gnb_port: int
    state: str = field(default=_IDLE, repr=False)
    serving_gnb: str = field(default="", repr=False)
    prior_disconnect_ts: float = field(default=0.0, repr=False)

    _reg_start_ts: Optional[float] = field(default=None, repr=False)
    _mr_ts: Optional[float] = field(default=None, repr=False)
    _rws_ts: Optional[float] = field(default=None, repr=False)

    ho_count: int = 0
    cho_count: int = 0
    dr_count: int = 0
    unmatched_mr_count: int = 0

    events: list[Event] = field(default_factory=list)

    def process(self, pkt: Packet) -> None:
        ts = pkt.ts
        msg = _msg_type(pkt.info)
        uplink   = pkt.dst_port == self.gnb_port
        downlink = pkt.src_port == self.gnb_port

        # --- Heartbeat ACK: monitor dBm from serving gNB ---
        if msg == "heartbeat_ack" and downlink:
            if (self.state in _ACTIVE_STATES
                    and pkt.src_ip == self.serving_gnb
                    and pkt.dbm is not None
                    and pkt.dbm < DISCONNECT_DBM_THRESHOLD):
                self.prior_disconnect_ts = ts
            return

        if msg == "other":
            return

        # --- Disconnect: Setup Request while in any active state ---
        if msg == "setup_request" and uplink and self.state in _ACTIVE_STATES:
            self._reset_ho()
            self.state = _IDLE
            # Fall through to IDLE handling to start new registration.

        # --- State transitions ---
        if self.state == _IDLE:
            if msg == "setup_request" and uplink:
                self.state = _REG_STARTED
                self._reg_start_ts = ts
                self.serving_gnb = pkt.dst_ip

        elif self.state == _REG_STARTED:
            if msg == "setup" and downlink:
                self.state = _REG_PENDING

        elif self.state == _REG_PENDING:
            if msg == "setup_complete" and uplink:
                self.state = _CONNECTED
                if self.prior_disconnect_ts != 0.0:
                    self.dr_count += 1
                    self.events.append(DREvent(
                        ue_port=self.port,
                        index=self.dr_count,
                        ts=ts,
                        disconnect_ts=self.prior_disconnect_ts,
                        reconnect_ts=ts,
                    ))
                    self.prior_disconnect_ts = 0.0
                self._reg_start_ts = None

        elif self.state == _CONNECTED:
            if msg == "meas_report" and uplink:
                self.state = _POTENTIAL_HO
                self._mr_ts = ts

            elif msg == "rrc_complete" and uplink:
                # CHO: Reconfig Complete sent to a gNB different from the serving one.
                if pkt.dst_ip != self.serving_gnb:
                    self.cho_count += 1
                    self.events.append(CHOEvent(
                        ue_port=self.port,
                        index=self.cho_count,
                        ts=ts,
                        from_gnb=self.serving_gnb,
                        to_gnb=pkt.dst_ip,
                    ))
                    self.serving_gnb = pkt.dst_ip
                # same-gNB Reconfig Complete in CONNECTED = orphan from attach, ignore

            elif msg == "rrc_reconfig" and downlink:
                pass  # measurement config update — no state change

            elif msg == "rrc_release" and downlink and pkt.src_ip == self.serving_gnb:
                self.state = _IDLE

        elif self.state == _POTENTIAL_HO:
            if msg == "meas_report" and uplink:
                self.unmatched_mr_count += 1
                self._mr_ts = ts  # restart timer

            elif msg == "rrc_reconfig" and downlink:
                if pkt.has_sync:
                    self.state = _HO_PENDING
                    self._rws_ts = ts
                # else: plain reconfig (measurement update), ignore

        elif self.state == _HO_PENDING:
            if msg == "rrc_complete" and uplink:
                self.ho_count += 1
                self.events.append(HOEvent(
                    ue_port=self.port,
                    index=self.ho_count,
                    ts=self._mr_ts,    # type: ignore[arg-type]
                    mr_ts=self._mr_ts,  # type: ignore[arg-type]
                    rws_ts=self._rws_ts,  # type: ignore[arg-type]
                    complete_ts=ts,
                ))
                self.serving_gnb = pkt.dst_ip
                self.state = _CONNECTED
                self._reset_ho()

    def _reset_ho(self) -> None:
        self._mr_ts = None
        self._rws_ts = None


# ---------------------------------------------------------------------------
# PCAP extraction
# ---------------------------------------------------------------------------

def extract_packets(pcap_path: str, gnb_port: int) -> list[Packet]:
    cmd = [
        "tshark", "-r", pcap_path,
        "-Y", "rls",
        "-T", "fields",
        "-e", "frame.time_epoch",
        "-e", "ip.src",
        "-e", "ip.dst",
        "-e", "udp.srcport",
        "-e", "udp.dstport",
        "-e", "_ws.col.Info",
        "-e", "rls.dbm",
        "-e", "nr-rrc.reconfigurationWithSync_element",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0 and not proc.stdout.strip():
        print(f"tshark failed:\n{proc.stderr}", file=sys.stderr)
        sys.exit(1)

    packets: list[Packet] = []
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) < 6:
            continue
        ts_s, src_ip, dst_ip, src_s, dst_s, info = parts[:6]
        dbm_s      = parts[6] if len(parts) > 6 else ""
        sync_s     = parts[7] if len(parts) > 7 else ""
        try:
            ts       = float(ts_s)
            src_port = int(src_s)
            dst_port = int(dst_s)
        except ValueError:
            continue

        dbm: Optional[int] = None
        if dbm_s.strip():
            try:
                dbm = int(dbm_s.strip())
            except ValueError:
                pass

        packets.append(Packet(
            ts=ts,
            src_ip=src_ip,
            dst_ip=dst_ip,
            src_port=src_port,
            dst_port=dst_port,
            info=info,
            dbm=dbm,
            has_sync=bool(sync_s.strip()),
        ))
    return packets


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

def analyze(pcap_path: str, gnb_port: int) -> dict[int, UEStateMachine]:
    packets = extract_packets(pcap_path, gnb_port)
    ues: dict[int, UEStateMachine] = {}

    for pkt in packets:
        # Discover UEs: any port that talks TO gnb_port or receives FROM gnb_port,
        # excluding gnb_port itself.
        ue_port: Optional[int] = None
        if pkt.dst_port == gnb_port and pkt.src_port != gnb_port:
            ue_port = pkt.src_port
        elif pkt.src_port == gnb_port and pkt.dst_port != gnb_port:
            ue_port = pkt.dst_port

        if ue_port is None:
            continue

        if ue_port not in ues:
            ues[ue_port] = UEStateMachine(port=ue_port, gnb_port=gnb_port)

        ues[ue_port].process(pkt)

    return ues


# ---------------------------------------------------------------------------
# Directory analysis
# ---------------------------------------------------------------------------

# (filename, ues) pairs — one entry per ran_trace*.pcap found.
FileResult = tuple[str, dict[int, UEStateMachine]]


def analyze_directory(dir_path: str, gnb_port: int) -> list[FileResult]:
    """Find every ran_trace*.pcap in dir_path (recursively) and run analyze() on each."""
    pcaps = sorted(Path(dir_path).rglob("ran_trace*.pcap"))
    if not pcaps:
        print(f"No ran_trace*.pcap files found in {dir_path}", file=sys.stderr)
        sys.exit(1)
    base = Path(dir_path)
    results: list[FileResult] = []
    for p in pcaps:
        label = str(p.relative_to(base))
        print(f"  Analysing {label} ...", file=sys.stderr)
        results.append((label, analyze(str(p), gnb_port)))
    return results


@dataclass
class TaggedEvent:
    """An event annotated with the source file it came from."""
    source_file: str
    event: Event

    @property
    def ts(self) -> float:
        return self.event.ts

    @property
    def ue_port(self) -> int:
        return self.event.ue_port


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def _all_events_sorted(ues: dict[int, UEStateMachine]) -> list[Event]:
    events: list[Event] = []
    for sm in sorted(ues.values(), key=lambda s: s.port):
        events.extend(sorted(sm.events, key=lambda e: e.ts))
    return events


def _fmt(val: Optional[float], decimals: int = 3) -> str:
    return f"{val:.{decimals}f}" if val is not None else "-"


def output_stdout(ues: dict[int, UEStateMachine]) -> None:
    events = _all_events_sorted(ues)

    # Column widths
    cw = {"port": 10, "dr": 5, "ho": 5, "cho": 5, "t1": 14, "t2": 14, "drms": 10}
    sep = "  "

    hdr = (
        f"{'UE Port':<{cw['port']}}{sep}{'D/R#':>{cw['dr']}}{sep}{'HO#':>{cw['ho']}}{sep}"
        f"{'CHO#':>{cw['cho']}}{sep}{'HO-MRtoRWS':>{cw['t1']}}{sep}"
        f"{'HO-RWStoRC':>{cw['t2']}}{sep}{'D/Rms':>{cw['drms']}}"
    )
    print(hdr)
    print("-" * len(hdr))

    for ev in events:
        port = ev.ue_port
        dr_n = ho_n = cho_n = ""
        t1 = t2 = drms = "-"

        if isinstance(ev, HOEvent):
            ho_n = str(ev.index)
            t1   = _fmt(ev.t1_ms)
            t2   = _fmt(ev.t2_ms)
        elif isinstance(ev, CHOEvent):
            cho_n = str(ev.index)
        elif isinstance(ev, DREvent):
            dr_n  = str(ev.index)
            drms  = _fmt(ev.delta_ms)

        print(
            f"{port:<{cw['port']}}{sep}{dr_n:>{cw['dr']}}{sep}{ho_n:>{cw['ho']}}{sep}"
            f"{cho_n:>{cw['cho']}}{sep}{t1:>{cw['t1']}}{sep}"
            f"{t2:>{cw['t2']}}{sep}{drms:>{cw['drms']}}"
        )

    if not events:
        print("No events recorded.")

    # Summary
    print()
    total_ho  = sum(sm.ho_count  for sm in ues.values())
    total_cho = sum(sm.cho_count for sm in ues.values())
    total_dr  = sum(sm.dr_count  for sm in ues.values())
    total_unmatched = sum(sm.unmatched_mr_count for sm in ues.values())

    ho_events  = [e for e in events if isinstance(e, HOEvent)]
    dr_events  = [e for e in events if isinstance(e, DREvent)]
    t1s  = [e.t1_ms     for e in ho_events]
    t2s  = [e.t2_ms     for e in ho_events]
    drms = [e.delta_ms  for e in dr_events]

    print(f"Total UEs:              {len(ues)}")
    print(f"Total HO events:        {total_ho}")
    print(f"Total CHO events:       {total_cho}")
    print(f"Total D/R cycles:       {total_dr}")
    if t1s or drms:
        print(f"                        {'avg':>10}  {'min':>10}  {'max':>10}")
    if t1s:
        print(f"  HO-MRtoRWS (ms):     {sum(t1s)/len(t1s):>10.3f}  {min(t1s):>10.3f}  {max(t1s):>10.3f}")
        print(f"  HO-RWStoRC (ms):     {sum(t2s)/len(t2s):>10.3f}  {min(t2s):>10.3f}  {max(t2s):>10.3f}")
    if drms:
        print(f"  D/R latency (ms):    {sum(drms)/len(drms):>10.3f}  {min(drms):>10.3f}  {max(drms):>10.3f}")

    # Separate unmatched MR report
    if total_unmatched:
        print()
        print("Unmatched Measurement Reports (no ReconfigWithSync received):")
        for port in sorted(ues):
            sm = ues[port]
            if sm.unmatched_mr_count:
                print(f"  UE {port}: {sm.unmatched_mr_count} unmatched")


def output_csv(ues: dict[int, UEStateMachine], path: Optional[str]) -> None:
    events = _all_events_sorted(ues)

    dest: io.IOBase
    dest = open(path, "w", newline="") if path else sys.stdout  # type: ignore[assignment]

    writer = csv.writer(dest)
    writer.writerow([
        "ue_port", "event_type", "event_index", "timestamp_s",
        "ho_mr_to_rws_ms", "ho_rws_to_rc_ms",
        "dr_delta_ms",
        "cho_from_gnb", "cho_to_gnb",
    ])

    for ev in events:
        if isinstance(ev, HOEvent):
            writer.writerow([
                ev.ue_port, "HO", ev.index, f"{ev.mr_ts:.6f}",
                f"{ev.t1_ms:.3f}", f"{ev.t2_ms:.3f}",
                "", "", "",
            ])
        elif isinstance(ev, CHOEvent):
            writer.writerow([
                ev.ue_port, "CHO", ev.index, f"{ev.ts:.6f}",
                "", "",
                "", ev.from_gnb, ev.to_gnb,
            ])
        elif isinstance(ev, DREvent):
            writer.writerow([
                ev.ue_port, "DR", ev.index, f"{ev.reconnect_ts:.6f}",
                "", "",
                f"{ev.delta_ms:.3f}", "", "",
            ])

    # Unmatched MRs as trailer rows.
    for port in sorted(ues):
        sm = ues[port]
        if sm.unmatched_mr_count:
            writer.writerow([port, "UNMATCHED_MR", sm.unmatched_mr_count, "", "", "", "", "", ""])

    if path:
        dest.close()  # type: ignore[union-attr]
        print(f"CSV written to {path}")


def output_json(ues: dict[int, UEStateMachine], path: Optional[str]) -> None:
    events = _all_events_sorted(ues)

    ho_events = [e for e in events if isinstance(e, HOEvent)]
    dr_events = [e for e in events if isinstance(e, DREvent)]
    t1s  = [e.t1_ms    for e in ho_events]
    t2s  = [e.t2_ms    for e in ho_events]
    drms = [e.delta_ms for e in dr_events]

    def _stats(vals: list[float]) -> dict:
        if not vals:
            return {}
        return {"avg_ms": round(sum(vals)/len(vals), 3),
                "min_ms": round(min(vals), 3),
                "max_ms": round(max(vals), 3)}

    summary = {
        "total_ues":       len(ues),
        "total_ho":        sum(sm.ho_count  for sm in ues.values()),
        "total_cho":       sum(sm.cho_count for sm in ues.values()),
        "total_dr":        sum(sm.dr_count  for sm in ues.values()),
        "total_unmatched_mr": sum(sm.unmatched_mr_count for sm in ues.values()),
        "ho_mr_to_rws":    _stats(t1s),
        "ho_rws_to_rc":    _stats(t2s),
        "dr_latency":      _stats(drms),
    }

    ue_data: dict = {}
    for port in sorted(ues):
        sm = ues[port]
        ev_list = []
        for ev in sorted(sm.events, key=lambda e: e.ts):
            if isinstance(ev, HOEvent):
                ev_list.append({
                    "type": "HO", "index": ev.index,
                    "mr_ts": ev.mr_ts, "rws_ts": ev.rws_ts, "complete_ts": ev.complete_ts,
                    "t1_ms": round(ev.t1_ms, 3), "t2_ms": round(ev.t2_ms, 3),
                })
            elif isinstance(ev, CHOEvent):
                ev_list.append({
                    "type": "CHO", "index": ev.index,
                    "ts": ev.ts, "from_gnb": ev.from_gnb, "to_gnb": ev.to_gnb,
                })
            elif isinstance(ev, DREvent):
                ev_list.append({
                    "type": "DR", "index": ev.index,
                    "disconnect_ts": ev.disconnect_ts, "reconnect_ts": ev.reconnect_ts,
                    "delta_ms": round(ev.delta_ms, 3),
                })
        ue_data[str(port)] = {
            "events": ev_list,
            "unmatched_mr_count": sm.unmatched_mr_count,
        }

    text = json.dumps({"summary": summary, "ues": ue_data}, indent=2)

    if path:
        Path(path).write_text(text)
        print(f"JSON written to {path}")
    else:
        print(text)


# ---------------------------------------------------------------------------
# Directory output helpers
# ---------------------------------------------------------------------------

def _tagged_events(results: list[FileResult]) -> list[TaggedEvent]:
    """Flatten all events across files, preserving file order then UE port order."""
    tagged: list[TaggedEvent] = []
    for fname, ues in results:
        for ev in _all_events_sorted(ues):
            tagged.append(TaggedEvent(source_file=fname, event=ev))
    return tagged


def _aggregate_stats(results: list[FileResult]) -> dict:
    all_ues_count   = sum(len(ues) for _, ues in results)
    total_ho        = sum(sm.ho_count        for _, ues in results for sm in ues.values())
    total_cho       = sum(sm.cho_count       for _, ues in results for sm in ues.values())
    total_dr        = sum(sm.dr_count        for _, ues in results for sm in ues.values())
    total_unmatched = sum(sm.unmatched_mr_count for _, ues in results for sm in ues.values())
    t1s  = [e.t1_ms    for _, ues in results for sm in ues.values()
            for e in sm.events if isinstance(e, HOEvent)]
    t2s  = [e.t2_ms    for _, ues in results for sm in ues.values()
            for e in sm.events if isinstance(e, HOEvent)]
    drms = [e.delta_ms for _, ues in results for sm in ues.values()
            for e in sm.events if isinstance(e, DREvent)]
    return dict(files=len(results), ue_sessions=all_ues_count,
                total_ho=total_ho, total_cho=total_cho,
                total_dr=total_dr, total_unmatched=total_unmatched,
                t1s=t1s, t2s=t2s, drms=drms)


def output_stdout_dir(results: list[FileResult]) -> None:
    FILE_W = 28
    cw = {"port": 10, "dr": 5, "ho": 5, "cho": 5, "t1": 14, "t2": 14, "drms": 10}
    sep = "  "

    hdr_base = (
        f"{'UE Port':<{cw['port']}}{sep}{'D/R#':>{cw['dr']}}{sep}{'HO#':>{cw['ho']}}{sep}"
        f"{'CHO#':>{cw['cho']}}{sep}{'HO-MRtoRWS':>{cw['t1']}}{sep}"
        f"{'HO-RWStoRC':>{cw['t2']}}{sep}{'D/Rms':>{cw['drms']}}"
    )
    hdr_dir = f"{'File':<{FILE_W}}{sep}" + hdr_base

    def _event_row(ev: Event, file_prefix: str = "") -> str:
        dr_n = ho_n = cho_n = ""
        t1 = t2 = drms = "-"
        if isinstance(ev, HOEvent):
            ho_n = str(ev.index); t1 = _fmt(ev.t1_ms); t2 = _fmt(ev.t2_ms)
        elif isinstance(ev, CHOEvent):
            cho_n = str(ev.index)
        elif isinstance(ev, DREvent):
            dr_n = str(ev.index); drms = _fmt(ev.delta_ms)
        row = (
            f"{ev.ue_port:<{cw['port']}}{sep}{dr_n:>{cw['dr']}}{sep}{ho_n:>{cw['ho']}}{sep}"
            f"{cho_n:>{cw['cho']}}{sep}{t1:>{cw['t1']}}{sep}"
            f"{t2:>{cw['t2']}}{sep}{drms:>{cw['drms']}}"
        )
        if file_prefix:
            trunc = file_prefix[:FILE_W]
            return f"{trunc:<{FILE_W}}{sep}{row}"
        return row

    # --- Per-file sections ---
    for fname, ues in results:
        print(f"\n=== {fname} ===")
        print(hdr_base)
        print("-" * len(hdr_base))
        events = _all_events_sorted(ues)
        if events:
            for ev in events:
                print(_event_row(ev))
        else:
            print("  (no events)")

    # --- Aggregate event table ---
    tagged = _tagged_events(results)
    if tagged:
        print(f"\n=== AGGREGATE ({len(results)} files) ===")
        print(hdr_dir)
        print("-" * len(hdr_dir))
        for te in tagged:
            print(_event_row(te.event, te.source_file))

    # --- Grand totals ---
    st = _aggregate_stats(results)
    print(f"\n{'='*60}")
    print(f"TOTALS  ({st['files']} files, {st['ue_sessions']} UE sessions)")
    print(f"{'='*60}")
    print(f"Total HO events:        {st['total_ho']}")
    print(f"Total CHO events:       {st['total_cho']}")
    print(f"Total D/R cycles:       {st['total_dr']}")
    t1s, t2s, drms = st['t1s'], st['t2s'], st['drms']
    if t1s or drms:
        print(f"                        {'avg':>10}  {'min':>10}  {'max':>10}")
    if t1s:
        print(f"  HO-MRtoRWS (ms):     "
              f"{sum(t1s)/len(t1s):>10.3f}  {min(t1s):>10.3f}  {max(t1s):>10.3f}")
        print(f"  HO-RWStoRC (ms):     "
              f"{sum(t2s)/len(t2s):>10.3f}  {min(t2s):>10.3f}  {max(t2s):>10.3f}")
    if drms:
        print(f"  D/R latency (ms):    "
              f"{sum(drms)/len(drms):>10.3f}  {min(drms):>10.3f}  {max(drms):>10.3f}")
    if st['total_unmatched']:
        print(f"Unmatched MRs:          {st['total_unmatched']}")


def output_csv_dir(results: list[FileResult], path: Optional[str]) -> None:
    dest: io.IOBase
    dest = open(path, "w", newline="") if path else sys.stdout  # type: ignore[assignment]
    writer = csv.writer(dest)
    writer.writerow([
        "source_file", "ue_port", "event_type", "event_index", "timestamp_s",
        "ho_mr_to_rws_ms", "ho_rws_to_rc_ms",
        "dr_delta_ms", "cho_from_gnb", "cho_to_gnb",
    ])
    for te in _tagged_events(results):
        ev = te.event
        f  = te.source_file
        if isinstance(ev, HOEvent):
            writer.writerow([f, ev.ue_port, "HO", ev.index, f"{ev.mr_ts:.6f}",
                             f"{ev.t1_ms:.3f}", f"{ev.t2_ms:.3f}", "", "", ""])
        elif isinstance(ev, CHOEvent):
            writer.writerow([f, ev.ue_port, "CHO", ev.index, f"{ev.ts:.6f}",
                             "", "", "", ev.from_gnb, ev.to_gnb])
        elif isinstance(ev, DREvent):
            writer.writerow([f, ev.ue_port, "DR", ev.index, f"{ev.reconnect_ts:.6f}",
                             "", "", f"{ev.delta_ms:.3f}", "", ""])
    # Unmatched MR trailer
    for fname, ues in results:
        for port in sorted(ues):
            sm = ues[port]
            if sm.unmatched_mr_count:
                writer.writerow([fname, port, "UNMATCHED_MR",
                                 sm.unmatched_mr_count, "", "", "", "", "", ""])
    if path:
        dest.close()  # type: ignore[union-attr]
        print(f"CSV written to {path}")


def output_json_dir(results: list[FileResult], path: Optional[str]) -> None:
    st = _aggregate_stats(results)

    def _stats(vals: list[float]) -> dict:
        if not vals:
            return {}
        return {"avg_ms": round(sum(vals)/len(vals), 3),
                "min_ms": round(min(vals), 3),
                "max_ms": round(max(vals), 3)}

    aggregate = {
        "files":           st['files'],
        "ue_sessions":     st['ue_sessions'],
        "total_ho":        st['total_ho'],
        "total_cho":       st['total_cho'],
        "total_dr":        st['total_dr'],
        "total_unmatched_mr": st['total_unmatched'],
        "ho_mr_to_rws":    _stats(st['t1s']),
        "ho_rws_to_rc":    _stats(st['t2s']),
        "dr_latency":      _stats(st['drms']),
    }

    files_data: dict = {}
    for fname, ues in results:
        ue_data: dict = {}
        for port in sorted(ues):
            sm = ues[port]
            ev_list = []
            for ev in sorted(sm.events, key=lambda e: e.ts):
                if isinstance(ev, HOEvent):
                    ev_list.append({"type": "HO", "index": ev.index,
                                    "mr_ts": ev.mr_ts, "rws_ts": ev.rws_ts,
                                    "complete_ts": ev.complete_ts,
                                    "t1_ms": round(ev.t1_ms, 3),
                                    "t2_ms": round(ev.t2_ms, 3)})
                elif isinstance(ev, CHOEvent):
                    ev_list.append({"type": "CHO", "index": ev.index,
                                    "ts": ev.ts, "from_gnb": ev.from_gnb,
                                    "to_gnb": ev.to_gnb})
                elif isinstance(ev, DREvent):
                    ev_list.append({"type": "DR", "index": ev.index,
                                    "disconnect_ts": ev.disconnect_ts,
                                    "reconnect_ts": ev.reconnect_ts,
                                    "delta_ms": round(ev.delta_ms, 3)})
            ue_data[str(port)] = {"events": ev_list,
                                  "unmatched_mr_count": sm.unmatched_mr_count}
        files_data[fname] = ue_data

    text = json.dumps({"aggregate": aggregate, "files": files_data}, indent=2)
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
            "Build per-UE RRC state machines from a UERANSIM PCAP file and report "
            "handover (HO), conditional handover (CHO), and disconnect/reconnect (D/R) events."
        )
    )
    # Mutually exclusive: single file OR directory.
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument(
        "pcap", nargs="?",
        help="Path to a single ran_trace PCAP file",
    )
    src.add_argument(
        "-d", "--directory", metavar="DIR",
        help="Directory to search for ran_trace*.pcap files (results accumulated)",
    )
    parser.add_argument(
        "--format", choices=["json", "csv", "stdout"], default="stdout",
        metavar="FORMAT", help="Output format: stdout (default), csv, or json",
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

    if args.directory:
        d = Path(args.directory)
        if not d.is_dir():
            print(f"Error: not a directory: {args.directory}", file=sys.stderr)
            sys.exit(1)
        results = analyze_directory(str(d), args.gnb_port)
        if args.format == "stdout":
            output_stdout_dir(results)
        elif args.format == "csv":
            output_csv_dir(results, args.output)
        elif args.format == "json":
            output_json_dir(results, args.output)
    else:
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
