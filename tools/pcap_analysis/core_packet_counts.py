#!/usr/bin/env python3
"""
Per-pair packet-count analysis of UERANSIM core_trace PCAP files.

Counts packets exchanged between each labelled IP address pair in the 5G core
network trace.  Packets whose source or destination is unlabelled are silently
ignored so the output stays focused on inter-NF traffic.

IP labels (last octet of 172.22.0.x):
  AMF=10, SMF=7, UPF=8, NRF=12, AUSF=11, UDM=13, UDR=14, PCF=27
  GNBn = last octet 202+ → GNB1=202, GNB2=203, GNB3=204, …

Usage:
  Single file:    core_packet_counts.py core_trace_1.pcap
  Directory:      core_packet_counts.py -d /path/to/log/dir
                  Analyses every core_trace*.pcap in the directory, accumulates
                  results, and presents per-file tables plus aggregate totals.
"""

from __future__ import annotations

import argparse
import collections
import csv
import io
import json
import subprocess
import sys
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# IP label mapping
# ---------------------------------------------------------------------------

_NF_LABELS: dict[int, str] = {
    10: "AMF",
    7:  "SMF",
    8:  "UPF",
    12: "NRF",
    11: "AUSF",
    13: "UDM",
    14: "UDR",
    27: "PCF",
}

_GNB_FIRST_OCTET = 202   # 202 → GNB1, 203 → GNB2, …


def _label_ip(ip: str) -> Optional[str]:
    """Map an IP string to a node label, or None if not recognised."""
    # tshark sometimes emits comma-separated IPs for tunnelled frames; take first.
    ip = ip.split(",")[0].strip()
    parts = ip.split(".")
    if len(parts) != 4:
        return None
    try:
        last = int(parts[-1])
    except ValueError:
        return None
    if last in _NF_LABELS:
        return _NF_LABELS[last]
    if last >= _GNB_FIRST_OCTET:
        return f"GNB{last - _GNB_FIRST_OCTET + 1}"
    return None


# ---------------------------------------------------------------------------
# PCAP extraction
# ---------------------------------------------------------------------------

PairCounts = dict[tuple[str, str], int]   # canonical (labelA, labelB) → count


def extract_pair_counts(pcap_path: str) -> PairCounts:
    cmd = [
        "tshark", "-r", pcap_path,
        "-T", "fields",
        "-e", "ip.src",
        "-e", "ip.dst",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0 and not proc.stdout.strip():
        print(f"tshark failed for {pcap_path}:\n{proc.stderr}", file=sys.stderr)
        sys.exit(1)

    counts: PairCounts = collections.defaultdict(int)
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        src_label = _label_ip(parts[0])
        dst_label = _label_ip(parts[1])
        if src_label is None or dst_label is None or src_label == dst_label:
            continue
        pair = (min(src_label, dst_label), max(src_label, dst_label))
        counts[pair] += 1

    return dict(counts)


# ---------------------------------------------------------------------------
# Directory analysis
# ---------------------------------------------------------------------------

FileResult = tuple[str, PairCounts]


def analyze_directory(dir_path: str) -> list[FileResult]:
    """Find every core_trace*.pcap in dir_path (recursively) and count pairs."""
    pcaps = sorted(Path(dir_path).rglob("core_trace*.pcap"))
    if not pcaps:
        print(f"No core_trace*.pcap files found in {dir_path}", file=sys.stderr)
        sys.exit(1)
    base = Path(dir_path)
    results: list[FileResult] = []
    for p in pcaps:
        label = str(p.relative_to(base))
        print(f"  Analysing {label} ...", file=sys.stderr)
        results.append((label, extract_pair_counts(str(p))))
    return results


def _aggregate(results: list[FileResult]) -> PairCounts:
    totals: PairCounts = collections.defaultdict(int)
    for _, counts in results:
        for pair, n in counts.items():
            totals[pair] += n
    return dict(totals)


# ---------------------------------------------------------------------------
# Derived aggregates
# ---------------------------------------------------------------------------

# Pairs explicitly excluded from Other_Relevant (canonical min/max order).
_DERIVED_EXCLUDE_PAIRS = frozenset({("AMF", "SMF"), ("SMF", "UPF")})


def compute_derived(counts: PairCounts) -> dict[str, int]:
    """Return AMF_GNB, Other_Relevant, and Total_Avg aggregates from raw pair counts."""
    amf_gnb = sum(
        n for (a, b), n in counts.items()
        if "AMF" in (a, b) and (a.startswith("GNB") or b.startswith("GNB"))
    )
    other_relevant = sum(
        n for (a, b), n in counts.items()
        if "NRF" not in (a, b)
        and not a.startswith("GNB") and not b.startswith("GNB")
        and (a, b) not in _DERIVED_EXCLUDE_PAIRS
    )
    amf_smf = counts.get(("AMF", "SMF"), 0)
    smf_upf = counts.get(("SMF", "UPF"), 0)
    return {
        "AMF_GNB":       amf_gnb,
        "Other_Relevant": other_relevant,
        "Total_Avg":     amf_gnb + other_relevant + amf_smf + smf_upf,
    }


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def _pair_label(pair: tuple[str, str]) -> str:
    return f"{pair[0]}_{pair[1]}"


def _sorted_pairs(counts: PairCounts) -> list[tuple[tuple[str, str], int]]:
    return sorted(counts.items(), key=lambda kv: _pair_label(kv[0]))


def _print_table(counts: PairCounts, n_files: Optional[int] = None) -> None:
    PAIR_W  = 20
    COUNT_W = 10
    AVG_W   = 10
    show_avg = n_files is not None and n_files > 0
    hdr = f"{'Pair':<{PAIR_W}}  {'Packets':>{COUNT_W}}"
    if show_avg:
        hdr += f"  {'Avg/file':>{AVG_W}}"
    print(hdr)
    print("-" * len(hdr))
    if not counts:
        print("  (no labelled pairs found)")
    else:
        for pair, n in _sorted_pairs(counts):
            row = f"{_pair_label(pair):<{PAIR_W}}  {n:>{COUNT_W}}"
            if show_avg:
                row += f"  {n / n_files:>{AVG_W}.1f}"
            print(row)
    print()
    total = sum(counts.values())
    summary = f"Total labelled packets: {total}"
    if show_avg:
        summary += f"   avg/file: {total / n_files:.1f}"
    print(summary)


def _print_derived(counts: PairCounts, n_files: Optional[int] = None) -> None:
    PAIR_W  = 20
    COUNT_W = 10
    AVG_W   = 10
    show_avg = n_files is not None and n_files > 0
    derived = compute_derived(counts)
    print()
    hdr = f"{'Derived aggregate':<{PAIR_W}}  {'Packets':>{COUNT_W}}"
    if show_avg:
        hdr += f"  {'Avg/file':>{AVG_W}}"
    print(hdr)
    print("-" * len(hdr))
    for name, n in derived.items():
        row = f"{name:<{PAIR_W}}  {n:>{COUNT_W}}"
        if show_avg:
            row += f"  {n / n_files:>{AVG_W}.1f}"
        print(row)


# ---------------------------------------------------------------------------
# Output — single file
# ---------------------------------------------------------------------------

def output_stdout(counts: PairCounts) -> None:
    _print_table(counts)
    _print_derived(counts)


def output_csv(counts: PairCounts, path: Optional[str]) -> None:
    dest: io.IOBase
    dest = open(path, "w", newline="") if path else sys.stdout  # type: ignore[assignment]
    writer = csv.writer(dest)
    writer.writerow(["pair", "node_a", "node_b", "packet_count"])
    for pair, n in _sorted_pairs(counts):
        writer.writerow([_pair_label(pair), pair[0], pair[1], n])
    for name, n in compute_derived(counts).items():
        writer.writerow([name, "", "", n])
    if path:
        dest.close()  # type: ignore[union-attr]
        print(f"CSV written to {path}")


def output_json(counts: PairCounts, path: Optional[str]) -> None:
    data = {
        "total_labelled_packets": sum(counts.values()),
        "pair_counts": {_pair_label(p): n for p, n in _sorted_pairs(counts)},
        "derived": compute_derived(counts),
    }
    text = json.dumps(data, indent=2)
    if path:
        Path(path).write_text(text)
        print(f"JSON written to {path}")
    else:
        print(text)


# ---------------------------------------------------------------------------
# Output — directory mode
# ---------------------------------------------------------------------------

def output_stdout_dir(results: list[FileResult]) -> None:
    for fname, counts in results:
        print(f"\n=== {fname} ===")
        _print_table(counts)
        _print_derived(counts)

    n = len(results)
    agg = _aggregate(results)
    print(f"\n{'='*60}")
    print(f"AGGREGATE  ({n} files)")
    print(f"{'='*60}")
    _print_table(agg, n_files=n)
    _print_derived(agg, n_files=n)


def output_csv_dir(results: list[FileResult], path: Optional[str]) -> None:
    dest: io.IOBase
    dest = open(path, "w", newline="") if path else sys.stdout  # type: ignore[assignment]
    writer = csv.writer(dest)
    writer.writerow(["source_file", "pair", "node_a", "node_b", "packet_count", "avg_per_file"])
    for fname, counts in results:
        for pair, n in _sorted_pairs(counts):
            writer.writerow([fname, _pair_label(pair), pair[0], pair[1], n, ""])
        for name, n in compute_derived(counts).items():
            writer.writerow([fname, name, "", "", n, ""])
    n_files = len(results)
    agg = _aggregate(results)
    for pair, n in _sorted_pairs(agg):
        writer.writerow(["AGGREGATE", _pair_label(pair), pair[0], pair[1], n,
                         f"{n / n_files:.1f}"])
    for name, n in compute_derived(agg).items():
        writer.writerow(["AGGREGATE", name, "", "", n, f"{n / n_files:.1f}"])
    if path:
        dest.close()  # type: ignore[union-attr]
        print(f"CSV written to {path}")


def output_json_dir(results: list[FileResult], path: Optional[str]) -> None:
    n_files = len(results)
    agg = _aggregate(results)
    agg_derived = compute_derived(agg)
    data: dict = {
        "aggregate": {
            "n_files": n_files,
            "total_labelled_packets": sum(agg.values()),
            "pair_counts": {_pair_label(p): {"total": n, "avg_per_file": round(n / n_files, 1)}
                            for p, n in _sorted_pairs(agg)},
            "derived": {name: {"total": n, "avg_per_file": round(n / n_files, 1)}
                        for name, n in agg_derived.items()},
        },
        "files": {},
    }
    for fname, counts in results:
        data["files"][fname] = {
            "total_labelled_packets": sum(counts.values()),
            "pair_counts": {_pair_label(p): n for p, n in _sorted_pairs(counts)},
            "derived": compute_derived(counts),
        }
    text = json.dumps(data, indent=2)
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
            "Count packets exchanged between labelled IP pairs in UERANSIM "
            "core_trace PCAP files.  "
            "Labels (last octet): AMF=10, SMF=7, UPF=8, NRF=12, AUSF=11, "
            "UDM=13, UDR=14, PCF=27, GNBn=202+."
        )
    )
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument(
        "pcap", nargs="?",
        help="Path to a single core_trace PCAP file",
    )
    src.add_argument(
        "-d", "--directory", metavar="DIR",
        help="Directory to search for core_trace*.pcap files (results accumulated)",
    )
    parser.add_argument(
        "--format", choices=["json", "csv", "stdout"], default="stdout",
        metavar="FORMAT", help="Output format: stdout (default), csv, or json",
    )
    parser.add_argument(
        "--output", "-o", metavar="FILE",
        help="Write csv/json output to FILE instead of stdout",
    )
    args = parser.parse_args()

    if args.directory:
        d = Path(args.directory)
        if not d.is_dir():
            print(f"Error: not a directory: {args.directory}", file=sys.stderr)
            sys.exit(1)
        results = analyze_directory(str(d))
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
        counts = extract_pair_counts(args.pcap)
        if args.format == "stdout":
            output_stdout(counts)
        elif args.format == "csv":
            output_csv(counts, args.output)
        elif args.format == "json":
            output_json(counts, args.output)


if __name__ == "__main__":
    main()
