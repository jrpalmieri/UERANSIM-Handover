from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest
import yaml

from .harness.gnb_process import GnbProcess
from .harness.marks import gnb_binary_exists, needs_pysctp

PROJECT_ROOT = Path(__file__).resolve().parents[2]
pytestmark = [gnb_binary_exists, needs_pysctp]


def _compute_node_name_from_config(config: dict) -> str:
    nci = int(str(config["nci"]), 0)
    id_length = int(config["idLength"])
    gnb_id = nci >> (36 - id_length)
    mcc = int(str(config["mcc"]))
    mnc = int(str(config["mnc"]))
    return f"UERANSIM-gnb-{mcc}-{mnc}-{gnb_id}"


def _get_node_name(gnb: GnbProcess) -> str:
    cfg_path = gnb._config_path
    assert cfg_path is not None
    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    return _compute_node_name_from_config(cfg)


def _run_cli(node_name: str, command: str) -> subprocess.CompletedProcess[str]:
    cli_bin = PROJECT_ROOT / "build" / "nr-cli"
    proc = subprocess.run(
        [str(cli_bin), node_name, "-e", command],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
    )
    _print_cli_exchange(cli_bin, node_name, command, proc)
    return proc


def _print_cli_exchange(
    cli_bin: Path,
    node_name: str,
    command: str,
    proc: subprocess.CompletedProcess[str],
) -> None:
    print(f"[nr-cli] cmd: {cli_bin} {node_name} -e {command}", flush=True)
    print(f"[nr-cli] exit={proc.returncode}", flush=True)

    stdout = proc.stdout.strip()
    stderr = proc.stderr.strip()

    if stdout:
        print("[nr-cli] stdout:", flush=True)
        for line in stdout.splitlines():
            print(f"  {line}", flush=True)

    if stderr:
        print("[nr-cli] stderr:", flush=True)
        for line in stderr.splitlines():
            print(f"  {line}", flush=True)


def _run_neighbors(node_name: str, payload: dict) -> subprocess.CompletedProcess[str]:
    payload_str = json.dumps(payload, separators=(",", ":"))
    return _run_cli(node_name, f"neighbors '{payload_str}'")


def _run_info(node_name: str) -> dict:
    proc = _run_cli(node_name, "info")
    assert proc.returncode == 0, (
        "nr-cli info failed:\n"
        f"stdout: {proc.stdout}\n"
        f"stderr: {proc.stderr}"
    )
    return yaml.safe_load(proc.stdout)


def _pci_set(info_json: dict) -> set[int]:
    entries = info_json.get("neighbor-list")
    if entries is None:
        entries = info_json.get("neighborList")
    if entries is None:
        entries = []
    return {int(entry["pci"]) for entry in entries}


def _neighbors_from_update_response(response: dict) -> set[int]:
    entries = response.get("neighbors")
    assert isinstance(entries, list), f"neighbors field missing or not a list: {response}"
    return {int(entry["pci"]) for entry in entries}


def _runtime_neighbors_pci_set(node_name: str) -> set[int]:
    # No-op request used to fetch current runtime neighbor store from command response.
    proc = _run_neighbors(node_name, {"mode": "add", "neighbors": []})
    assert proc.returncode == 0, (
        "nr-cli neighbors runtime snapshot failed:\n"
        f"stdout: {proc.stdout}\n"
        f"stderr: {proc.stderr}"
    )
    response = yaml.safe_load(proc.stdout)
    assert response["result"] == "ok"
    return _neighbors_from_update_response(response)


def test_neighbors_replace_updates_neighbor_list(started_gnb: GnbProcess):
    node_name = _get_node_name(started_gnb)

    replace_payload = {
        "mode": "replace",
        "neighbors": [
            {
                "nci": 0x000000002,
                "idLength": 32,
                "tac": 1,
                "ipAddress": "127.0.0.2",
                "handoverInterface": "N2",
            },
            {
                "nci": 0x000000003,
                "idLength": 32,
                "tac": 1,
                "ipAddress": "127.0.0.3",
                "handoverInterface": "Xn",
                "xnAddress": "127.0.0.3",
                "xnPort": 9487,
            },
        ],
    }

    proc = _run_neighbors(node_name, replace_payload)
    assert proc.returncode == 0, (
        "nr-cli neighbors replace failed:\n"
        f"stdout: {proc.stdout}\n"
        f"stderr: {proc.stderr}"
    )

    response = yaml.safe_load(proc.stdout)
    assert response["result"] == "ok"
    assert response["mode"] == "replace"
    assert response["afterCount"] == 2

    assert _neighbors_from_update_response(response) == {2, 3}
    assert _runtime_neighbors_pci_set(node_name) == {2, 3}


def test_neighbors_add_and_remove_by_pci(started_gnb: GnbProcess):
    node_name = _get_node_name(started_gnb)

    initial = {
        "mode": "replace",
        "neighbors": [
            {
                "nci": 0x000000002,
                "idLength": 32,
                "tac": 1,
                "ipAddress": "127.0.0.2",
                "handoverInterface": "N2",
            }
        ],
    }
    assert _run_neighbors(node_name, initial).returncode == 0

    add_payload = {
        "mode": "add",
        "neighbors": [
            {
                "nci": 0x000000003,
                "idLength": 32,
                "tac": 1,
                "ipAddress": "127.0.0.3",
                "handoverInterface": "N2",
            }
        ],
    }
    add_proc = _run_neighbors(node_name, add_payload)
    assert add_proc.returncode == 0, (
        "nr-cli neighbors add failed:\n"
        f"stdout: {add_proc.stdout}\n"
        f"stderr: {add_proc.stderr}"
    )

    add_response = yaml.safe_load(add_proc.stdout)
    assert add_response["mode"] == "add"
    assert add_response["addedCount"] == 1

    remove_payload = {
        "mode": "remove",
        "neighbors": [
            {
                "nci": 0x000000002,
                "idLength": 32,
            }
        ],
    }
    remove_proc = _run_neighbors(node_name, remove_payload)
    assert remove_proc.returncode == 0, (
        "nr-cli neighbors remove failed:\n"
        f"stdout: {remove_proc.stdout}\n"
        f"stderr: {remove_proc.stderr}"
    )

    remove_response = yaml.safe_load(remove_proc.stdout)
    assert remove_response["mode"] == "remove"
    assert remove_response["removedCount"] == 1

    assert _neighbors_from_update_response(remove_response) == {3}
    assert _runtime_neighbors_pci_set(node_name) == {3}


def test_neighbors_rejects_duplicate_pci_and_preserves_state(started_gnb: GnbProcess):
    node_name = _get_node_name(started_gnb)

    initial = {
        "mode": "replace",
        "neighbors": [
            {
                "nci": 0x000000002,
                "idLength": 32,
                "tac": 1,
                "ipAddress": "127.0.0.2",
                "handoverInterface": "N2",
            }
        ],
    }
    assert _run_neighbors(node_name, initial).returncode == 0

    invalid_payload = {
        "mode": "add",
        "neighbors": [
            {
                "nci": 0x000000012,
                "idLength": 32,
                "tac": 1,
                "ipAddress": "127.0.0.12",
                "handoverInterface": "N2",
            }
        ],
    }

    proc = _run_neighbors(node_name, invalid_payload)
    combined = (proc.stdout + "\n" + proc.stderr).lower()
    assert proc.returncode != 0 or "duplicate pci" in combined

    assert _runtime_neighbors_pci_set(node_name) == {2}


def test_neighbors_config_load_add_two_and_delete_one_preserves_others(
    started_gnb_with_neighbor: GnbProcess,
):
    node_name = _get_node_name(started_gnb_with_neighbor)

    # 1) Verify neighbors from config are present in the global neighbor store.
    assert _runtime_neighbors_pci_set(node_name) == {2}

    # 2) Add at least two neighbors via CLI and verify all are present.
    add_payload = {
        "mode": "add",
        "neighbors": [
            {
                "nci": 0x000000004,
                "idLength": 32,
                "tac": 1,
                "ipAddress": "127.0.0.4",
                "handoverInterface": "N2",
            },
            {
                "nci": 0x000000005,
                "idLength": 32,
                "tac": 1,
                "ipAddress": "127.0.0.5",
                "handoverInterface": "Xn",
                "xnAddress": "127.0.0.5",
                "xnPort": 9488,
            },
        ],
    }
    add_proc = _run_neighbors(node_name, add_payload)
    assert add_proc.returncode == 0, (
        "nr-cli neighbors add failed:\n"
        f"stdout: {add_proc.stdout}\n"
        f"stderr: {add_proc.stderr}"
    )

    add_response = yaml.safe_load(add_proc.stdout)
    assert add_response["result"] == "ok"
    assert add_response["mode"] == "add"
    assert add_response["addedCount"] == 2
    assert add_response["afterCount"] == 3

    assert _neighbors_from_update_response(add_response) == {2, 4, 5}
    assert _runtime_neighbors_pci_set(node_name) == {2, 4, 5}

    # 3) Delete one added neighbor and verify the others remain.
    remove_payload = {
        "mode": "remove",
        "neighbors": [
            {
                "nci": 0x000000004,
                "idLength": 32,
            }
        ],
    }
    remove_proc = _run_neighbors(node_name, remove_payload)
    assert remove_proc.returncode == 0, (
        "nr-cli neighbors remove failed:\n"
        f"stdout: {remove_proc.stdout}\n"
        f"stderr: {remove_proc.stderr}"
    )

    remove_response = yaml.safe_load(remove_proc.stdout)
    assert remove_response["result"] == "ok"
    assert remove_response["mode"] == "remove"
    assert remove_response["removedCount"] == 1
    assert remove_response["afterCount"] == 2

    assert _neighbors_from_update_response(remove_response) == {2, 5}
    assert _runtime_neighbors_pci_set(node_name) == {2, 5}
