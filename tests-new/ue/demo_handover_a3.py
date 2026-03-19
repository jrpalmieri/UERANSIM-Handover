from __future__ import annotations

import argparse
import logging
import sys
import time
from pathlib import Path

# Allow running this file directly from the repo root.
SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from harness.fake_gnb import FakeGnb
from harness.ue_process import UeProcess


def print_banner(title: str) -> None:
    print("\n" + "=" * 80)
    print(title)
    print("=" * 80)


def flush_new_ue_logs(ue: UeProcess, last_index: int) -> int:
    ue.collect_output(timeout_s=0.3)
    lines = ue.log_lines
    if last_index >= len(lines):
        return last_index

    for line in lines[last_index:]:
        print(f"[UE] {line}")

    return len(lines)


def wait_with_ue_logs(ue: UeProcess, seconds: float, last_index: int) -> int:
    end = time.monotonic() + seconds
    cursor = last_index
    while time.monotonic() < end:
        cursor = flush_new_ue_logs(ue, cursor)
        time.sleep(0.2)
    return cursor


def run_demo(a3_offset: int, a3_hysteresis: int, a3_ttt: int) -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="[%(asctime)s] [%(name)s] [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    src_gnb = FakeGnb(listen_addr="127.0.0.1", cell_dbm=-55, nci=1)
    tgt_gnb = FakeGnb(listen_addr="127.0.0.2", cell_dbm=-90, nci=2)
    ue = UeProcess(gnb_search_list=["127.0.0.1", "127.0.0.2"])

    log_cursor = 0

    try:
        print_banner("Starting Demo Components")
        print("[SRC-gNB] Start on 127.0.0.1:4997 (PCI/NCI=1)")
        print("[TGT-gNB] Start on 127.0.0.2:4997 (PCI/NCI=2)")
        src_gnb.start()
        tgt_gnb.start()

        ue.generate_config(enable_handover_sim=True)
        ue.start()
        print("[UE] Started nr-ue with gnbSearchList=[127.0.0.1, 127.0.0.2]")

        print_banner("RLS Discovery")
        if not src_gnb.wait_for_heartbeat(timeout_s=10):
            print("[SRC-gNB] ERROR: did not receive heartbeat from UE")
            return 1
        if not tgt_gnb.wait_for_heartbeat(timeout_s=10):
            print("[TGT-gNB] ERROR: did not receive heartbeat from UE")
            return 1
        print("[SRC-gNB] Heartbeat received from UE")
        print("[TGT-gNB] Heartbeat received from UE")
        log_cursor = wait_with_ue_logs(ue, 1.0, log_cursor)

        print_banner("Initial Attach and RRC Setup on Source gNB")
        print("[SRC-gNB] Send MIB/SIB1")
        src_gnb.perform_cell_attach()

        print("[SRC-gNB] Wait for RRCSetupRequest and send RRCSetup")
        if not src_gnb.perform_rrc_setup(timeout_s=15):
            print("[SRC-gNB] ERROR: UE did not send RRCSetupRequest")
            return 1

        print("[SRC-gNB] Completing NAS registration")
        if not src_gnb.perform_registration(timeout_s=20):
            print("[SRC-gNB] ERROR: registration did not complete")
            return 1

        log_cursor = wait_with_ue_logs(ue, 2.0, log_cursor)

        print_banner("Configure MeasConfig (A3) on Source gNB")
        print(
            "[SRC-gNB] Send RRCReconfiguration with A3 "
            f"(offset={a3_offset}, hysteresis={a3_hysteresis}, ttt={a3_ttt}ms)"
        )
        src_gnb.send_meas_config(
            meas_objects=[{"id": 1, "ssbFreq": 632628}],
            report_configs=[
                {
                    "id": 1,
                    "event": "a3",
                    "a3Offset": a3_offset,
                    "hysteresis": a3_hysteresis,
                    "timeToTrigger": a3_ttt,
                    "maxReportCells": 8,
                }
            ],
            meas_ids=[{"measId": 1, "measObjectId": 1, "reportConfigId": 1}],
            transaction_id=1,
        )

        log_cursor = wait_with_ue_logs(ue, 1.0, log_cursor)

        print_banner("Trigger A3 Condition")
        print("[SRC-gNB] Lower source signal to -95 dBm")
        print("[TGT-gNB] Raise target signal to -50 dBm")
        src_gnb.cell_dbm = -95
        tgt_gnb.cell_dbm = -50

        print("[SRC-gNB] Waiting for UL MeasurementReport from UE")
        meas_report = src_gnb.wait_for_measurement_report(timeout_s=20)
        if meas_report is None:
            print("[SRC-gNB] ERROR: did not receive MeasurementReport")
            log_cursor = wait_with_ue_logs(ue, 3.0, log_cursor)
            return 1
        print("[SRC-gNB] MeasurementReport received")

        print_banner("Issue Handover Command")
        print("[SRC-gNB] Send RRCReconfigurationWithSync targeting PCI=2")
        src_gnb.send_handover_command(target_pci=2, new_crnti=0x1234, t304_ms=1000, transaction_id=2)

        print("[SRC-gNB/TGT-gNB] Wait for RRCReconfigurationComplete")
        reconfig_complete_src = src_gnb.wait_for_rrc_reconfiguration_complete(timeout_s=5)
        reconfig_complete_tgt = tgt_gnb.wait_for_rrc_reconfiguration_complete(timeout_s=10)
        if reconfig_complete_src is not None:
            print("[SRC-gNB] RRCReconfigurationComplete received")
        if reconfig_complete_tgt is not None:
            print("[TGT-gNB] RRCReconfigurationComplete received")
        if reconfig_complete_src is None and reconfig_complete_tgt is None:
            print("[SRC-gNB/TGT-gNB] WARNING: no RRCReconfigurationComplete observed")

        log_cursor = wait_with_ue_logs(ue, 5.0, log_cursor)

        print_banner("UE Handover Summary")
        info = ue.parse_handover_info()
        print(f"[UE] command_received={info['command_received']}")
        print(f"[UE] completed={info['completed']}")
        print(f"[UE] failed={info['failed']}")
        print(f"[UE] target_pci={info['target_pci']}")
        print(f"[UE] source_cell={info['source_cell']}")
        print(f"[UE] target_cell={info['target_cell']}")
        print(f"[UE] t304_expired={info['t304_expired']}")
        print(f"[UE] rlf_triggered={info['rlf_triggered']}")

        if info["completed"]:
            print("\nDemo result: handover completed.")
            return 0

        print("\nDemo result: handover command path ran, but completion was not observed.")
        return 2
    finally:
        print_banner("Stopping Demo Components")
        ue.cleanup()
        src_gnb.stop()
        tgt_gnb.stop()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Demo: UE handover using FakeGnb with A3 MeasConfig.",
    )
    parser.add_argument("--a3-offset", type=int, default=3, help="A3 offset in dB (default: 3)")
    parser.add_argument("--a3-hysteresis", type=int, default=1, help="A3 hysteresis in dB (default: 1)")
    parser.add_argument("--a3-ttt", type=int, default=160, help="A3 timeToTrigger in ms (default: 160)")
    args = parser.parse_args()

    return run_demo(args.a3_offset, args.a3_hysteresis, args.a3_ttt)


if __name__ == "__main__":
    raise SystemExit(main())
