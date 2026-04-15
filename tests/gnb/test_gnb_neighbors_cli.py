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
    return subprocess.run(
        [str(cli_bin), node_name, "-e", command],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
    )


def _run_neighbors(node_name: str, payload: dict) -> subprocess.CompletedProcess[str]:
    payload_str = json.dumps(payload, separators=(",", ":"))
    return _run_cli(node_name, f"neighbors {payload_str}")


def _run_info(node_name: str) -> dict:
    proc = _run_cli(node_name, "info")
    assert proc.returncode == 0, (
        "nr-cli info failed:\n"
        f"stdout: {proc.stdout}\n"
        f"stderr: {proc.stderr}"
    )
    return yaml.safe_load(proc.stdout)


def _pci_set(info_json: dict) -> set[int]:
    return {int(entry["pci"]) for entry in info_json.get("neighbor-list", [])}


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

    info_json = _run_info(node_name)
    assert len(info_json.get("neighbor-list", [])) == 2
    assert _pci_set(info_json) == {2, 3}


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

    info_json = _run_info(node_name)
    assert _pci_set(info_json) == {3}


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

    info_json = _run_info(node_name)
    assert _pci_set(info_json) == {2}
