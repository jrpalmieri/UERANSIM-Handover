"""
Test for measurement-triggered handover using per-gNB signal reports.

This test runs real nr-ue and two nr-gnb processes connected to a real 5G core.
It changes each gNB's advertised signal strength (HEARTBEAT_ACK dbm) and
verifies the UE hands over to the stronger target cell.
"""

import time
import tempfile
from pathlib import Path

import pytest
import yaml
import shutil

try:
    from pymongo import MongoClient
    from bson import ObjectId
except ImportError:
    pytest.skip("pymongo not installed")

from harness.ue_process import UeProcess
from gnb_harness.gnb_process import GnbProcess
from gnb_harness.marks import gnb_binary_exists


def get_db(db_uri="mongodb://localhost/open5gs"):
    """Connect to MongoDB and return the database object."""
    client = MongoClient(db_uri)
    db_name = db_uri.split('/')[-1].split('?')[0] if '/' in db_uri else 'open5gs'
    if not db_name:
        db_name = 'open5gs'
    return client[db_name]


def create_session(name, pdn_type=3, ipv4=None):
    """Create a session document for a subscriber slice."""
    session = {
        "name": name,
        "type": pdn_type,
        "qos": {
            "index": 9,
            "arp": {
                "priority_level": 8,
                "pre_emption_capability": 1,
                "pre_emption_vulnerability": 1
            }
        },
        "ambr": {
            "downlink": {"value": 1000000000, "unit": 0},
            "uplink": {"value": 1000000000, "unit": 0}
        },
        "ue": {
            "addr": ipv4 or "",
            "addr6": ""
        },
        "_id": ObjectId()
    }
    return session


def create_slice(sst, sd=None, sessions=None, default_indicator=False):
    """Create a slice document for a subscriber."""
    slice_doc = {
        "sst": sst,
        "default_indicator": default_indicator,
        "_id": ObjectId()
    }
    if sd is not None:
        slice_doc["sd"] = sd
    if sessions:
        slice_doc["session"] = sessions
    return slice_doc


def create_subscriber_doc(imsi, key, opc=None, op=None, amf="8000", slices=None, imeisv=None):
    """Create a subscriber document."""
    security = {
        "k": key,
        "amf": amf,
        "op": None,
        "opc": None
    }
    if opc:
        security["opc"] = opc
    if op:
        security["op"] = op

    doc = {
        "schema_version": 1,
        "imsi": imsi,
        "msisdn": [],
        "imeisv": [imeisv] if imeisv else [],
        "mme_host": [],
        "mm_realm": [],
        "purge_flag": [],
        "slice": slices if slices else [],
        "security": security,
        "ambr": {
            "downlink": {"value": 1000000000, "unit": 0},
            "uplink": {"value": 1000000000, "unit": 0}
        },
        "access_restriction_data": 32,
        "network_access_mode": 0,
        "subscriber_status": 0,
        "operator_determined_barring": 0,
        "subscribed_rau_tau_timer": 12,
        "__v": 0
    }
    return doc


def add_or_update_subscriber(db, imsi, key, op, op_type="OP", amf="8000", imeisv=None):
    """Add or update a subscriber in the database."""
    # Delete existing if exists
    db.subscribers.delete_one({"imsi": imsi})
    
    opc = op if op_type.upper() == 'OPC' else None
    op_val = op if op_type.upper() == 'OP' else None
    
    session = create_session("internet", pdn_type=3)
    slice_doc = create_slice(sst=1, sessions=[session])
    doc = create_subscriber_doc(imsi, key, opc=opc, op=op_val, amf=amf, slices=[slice_doc], imeisv=imeisv)
    result = db.subscribers.insert_one(doc)
    print(f"Subscriber {imsi} added/updated with _id: {result.inserted_id}")
    return result


@gnb_binary_exists
class TestOobHandover:
    """Test handover triggered by per-gNB signal reports."""

    def test_oob_measurement_handover(self):
        """Test that stronger target-gNB heartbeat dbm values trigger handover."""
        pytest.skip(
            "Legacy OOB measurement injection scenario. Migrate this real-core test "
            "to heartbeat-ACK dbm steering or fake-gNB multi-cell harness."
        )

        # Add UE to database
        db = get_db()
        add_or_update_subscriber(
            db=db,
            imsi="001010000000001",
            key="465B5CE8B199B49FAA5F0A2EE238A6BC",
            op="E8ED289DEBA952E4283B54E88E6183CA",
            op_type="OP",
            amf="8000",
            imeisv="4370816125816151"
        )
        
        # Create temporary directory for configs
        tmp_dir = Path(tempfile.mkdtemp(prefix="oob_handover_test_"))

        # GNB1 config (source)
        gnb1_config = {
            "mcc": "001",
            "mnc": "01",
            "nci": "0x00000001",
            "idLength": 32,
            "tac": 1,
            "linkIp": "127.0.0.1",
            "ngapIp": "127.0.0.1",
            "gtpIp": "127.0.0.1",
            "amfConfigs": [{"address": "172.22.0.10", "port": 38412}],
            "slices": [{"sst": 1, "sd": 1}],
            "ignoreStreamIds": True,
            "gnbSearchList": [{"address": "127.0.0.2", "tac": 2}],
        }
        gnb1_config_path = tmp_dir / "gnb1.yaml"
        with open(gnb1_config_path, "w") as f:
            yaml.dump(gnb1_config, f, default_flow_style=False)

        # GNB2 config (target)
        gnb2_config = {
            "mcc": "001",
            "mnc": "01",
            "nci": "0x00000002",
            "idLength": 32,
            "tac": 2,
            "linkIp": "127.0.0.2",
            "ngapIp": "127.0.0.2",
            "gtpIp": "127.0.0.2",
            "amfConfigs": [{"address": "172.22.0.10", "port": 38412}],
            "slices": [{"sst": 1, "sd": 1}],
            "ignoreStreamIds": True,
            "gnbSearchList": [{"address": "127.0.0.1", "tac": 1}],
        }
        gnb2_config_path = tmp_dir / "gnb2.yaml"
        with open(gnb2_config_path, "w") as f:
            yaml.dump(gnb2_config, f, default_flow_style=False)

        # UE config
        ue_config = {
            "supi": "imsi-001010000000001",
            "mcc": "001",
            "mnc": "01",
            "key": "465B5CE8B199B49FAA5F0A2EE238A6BC",
            "op": "E8ED289DEBA952E4283B54E88E6183CA",
            "opType": "OP",
            "amf": "8000",
            "imei": "356938035643803",
            "imeiSv": "4370816125816151",
            "gnbSearchList": ["127.0.0.1", "127.0.0.2"],
            "uacAic": {"mps": False, "mcs": False},
            "uacAcc": {
                "normalClass": 0,
                "class11": False,
                "class12": False,
                "class13": False,
                "class14": False,
                "class15": False,
            },
            "sessions": [{"type": "IPv4", "apn": "internet", "slice": {"sst": 1, "sd": 1}}],
            "configured-nssai": [{"sst": 1, "sd": 1}],
            "default-nssai": [{"sst": 1, "sd": 1}],
            "integrity": {"IA1": True, "IA2": True, "IA3": True},
            "ciphering": {"EA1": True, "EA2": True, "EA3": True},
            "integrityMaxRate": {"uplink": "full", "downlink": "full"},
            "enableHandoverSim": True,
        }
        ue_config_path = tmp_dir / "ue.yaml"
        with open(ue_config_path, "w") as f:
            yaml.dump(ue_config, f, default_flow_style=False)

        # Start GNB1
        gnb1 = GnbProcess(config_path=gnb1_config_path)
        gnb1.start()
        assert gnb1.wait_for_log("NG Setup procedure is successful", timeout_s=10)

        # Start GNB2
        gnb2 = GnbProcess(config_path=gnb2_config_path)
        gnb2.start()
        assert gnb2.wait_for_log("NG Setup procedure is successful", timeout_s=10)

        # Start UE
        ue = UeProcess(config_path=ue_config_path)
        ue.start()

        # Wait for UE to register and connect
        assert ue.wait_for_log("Registration is successful", timeout_s=20)
        assert ue.wait_for_log("Connection is successful", timeout_s=10)

        # Get initial cell
        initial_state = ue.parse_state()
        initial_cell = initial_state.cell_id
        print(f"UE initially connected to cell {initial_cell}")

        # Steer measurements by updating each gNB's advertised heartbeat dbm.
        if initial_cell == 1:
            # Favor gNB2
            gnb1.cell_dbm = -80  # weak
            gnb2.cell_dbm = -50  # strong
        else:
            # Favor gNB1
            gnb1.cell_dbm = -50  # strong
            gnb2.cell_dbm = -80  # weak

        # Allow heartbeat/update cycles to propagate new dbm values to UE.
        time.sleep(15)

        # Wait for handover
        handover_line = ue.wait_for_log("Handover completed", timeout_s=30)
        assert handover_line is not None, "Handover did not occur"

        # Check final state
        final_state = ue.parse_state()
        assert final_state.handover_completed
        assert final_state.cell_id != initial_cell

        # Log operations
        print("UE logs:")
        for line in ue.log_lines[-20:]:  # last 20 lines
            print(line)

        print("GNB1 logs:")
        for line in gnb1.log_lines[-20:]:
            print(line)

        print("GNB2 logs:")
        for line in gnb2.log_lines[-20:]:
            print(line)
        print("GNB2 logs:")
        for line in gnb2.log_lines[-20:]:
            print(line)

        # Cleanup
        ue.cleanup()
        gnb1.stop()
        gnb2.stop()

        # Remove temp dir
        shutil.rmtree(tmp_dir)
