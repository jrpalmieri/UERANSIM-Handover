"""
Simplified UDP measurement generator for manual handover testing.

Rather than launching any processes, this script assumes the UE and gNB
instances are already running (configured via their respective YAML files).
It simply drives the ``MeasurementInjector`` to send periodic measurements to
whatever UE is listening.  No assertions are performed.
"""
from pathlib import Path
import time

import pytest
import yaml


from .harness.meas_injector import MeasurementInjector

# def get_db(db_uri="mongodb://localhost:27017/open5gs"):
#     """Connect to MongoDB and return the database object."""
#     client = MongoClient(db_uri)
#     db_name = db_uri.split('/')[-1].split('?')[0] if '/' in db_uri else 'open5gs'
#     if not db_name:
#         db_name = 'open5gs'
#     return client[db_name]


# def create_session(name, pdn_type=3, ipv4=None):
#     """Create a session document for a subscriber slice."""
#     session = {
#         "name": name,
#         "type": pdn_type,
#         "qos": {
#             "index": 9,
#             "arp": {
#                 "priority_level": 8,
#                 "pre_emption_capability": 1,
#                 "pre_emption_vulnerability": 1
#             }
#         },
#         "ambr": {
#             "downlink": {"value": 1000000000, "unit": 0},
#             "uplink": {"value": 1000000000, "unit": 0}
#         },
#         "ue": {
#             "addr": ipv4 or "",
#             "addr6": ""
#         },
#         "_id": ObjectId()
#     }
#     return session


# def create_slice(sst, sd=None, sessions=None, default_indicator=False):
#     """Create a slice document for a subscriber."""
#     slice_doc = {
#         "sst": sst,
#         "default_indicator": default_indicator,
#         "_id": ObjectId()
#     }
#     if sd is not None:
#         slice_doc["sd"] = sd
#     if sessions:
#         slice_doc["session"] = sessions
#     return slice_doc


# def create_subscriber_doc(imsi, key, opc=None, op=None, amf="8000", slices=None, imeisv=None):
#     """Create a subscriber document."""
#     security = {
#         "k": key,
#         "amf": amf,
#         "op": None,
#         "opc": None
#     }
#     if opc:
#         security["opc"] = opc
#     if op:
#         security["op"] = op

#     doc = {
#         "schema_version": 1,
#         "imsi": imsi,
#         "msisdn": [],
#         "imeisv": [imeisv] if imeisv else [],
#         "mme_host": [],
#         "mm_realm": [],
#         "purge_flag": [],
#         "slice": slices if slices else [],
#         "security": security,
#         "ambr": {
#             "downlink": {"value": 1000000000, "unit": 0},
#             "uplink": {"value": 1000000000, "unit": 0}
#         },
#         "access_restriction_data": 32,
#         "network_access_mode": 0,
#         "subscriber_status": 0,
#         "operator_determined_barring": 0,
#         "subscribed_rau_tau_timer": 12,
#         "__v": 0
#     }
#     return doc


# def add_or_update_subscriber(db, imsi, key, op, op_type="OP", amf="8000", imeisv=None):
#     """Add or update a subscriber in the database."""
#     # Delete existing if exists
#     db.subscribers.delete_one({"imsi": imsi})
    
#     opc = op if op_type.upper() == 'OPC' else None
#     op_val = op if op_type.upper() == 'OP' else None
    
#     session = create_session("internet", pdn_type=3)
#     slice_doc = create_slice(sst=1, sessions=[session])
#     doc = create_subscriber_doc(imsi, key, opc=opc, op=op_val, amf=amf, slices=[slice_doc], imeisv=imeisv)
#     result = db.subscribers.insert_one(doc)
#     print(f"Subscriber {imsi} added/updated with _id: {result.inserted_id}")
#     return result


class TestOobHandover:
    """Simplified test that merely generates measurement traffic.

    Instead of launching GNB/UE processes, we assume the entities are already
    running and configured using the YAML files in ``tests/configs``.  The
    purpose of this test is simply to drive the ``MeasurementInjector`` so that
    the UE receives out‑of‑band reports periodically.  All log assertions and
    process management have been removed.
    """

    def test_oob_measurement_handover(self):
        """Generate periodic UDP measurements without touching UE/GNB processes."""

        print("Generating UDP measurements (no GNB/UE control)")

        # load the NCI values from the YAML configs so we know which cells to
        # reference.  (This is purely for demonstration; hard‑coding 1/2 would
        # also work.)
        config_dir = Path("./configs")
        gnb1_config_path = config_dir / "test_gnb1.yaml"
        gnb2_config_path = config_dir / "test_gnb2.yaml"

        def _read_nci(path: Path) -> int:
            try:
                with open(path) as f:
                    data = yaml.safe_load(f)
            except FileNotFoundError:
                # fall back to numeric literal if the file is missing
                return 1 if "gnb1" in str(path) else 2
            nci = data.get("nci")
            if isinstance(nci, str):
                return int(nci, 0)
            return int(nci)

        nci1 = _read_nci(gnb1_config_path)
        nci2 = _read_nci(gnb2_config_path)

        # create the injector and push two sample cells repeatedly
        inj = MeasurementInjector()
        # weak for first, strong for second – the UE under test should be
        # configured externally to interpret these reports however is desired.
        rsrp_values= [-50, -80]
        change_count = 0
        inj.set_cell(nci=nci1, rsrp=rsrp_values[0])
        inj.set_cell(nci=nci2, rsrp=rsrp_values[1])

        # print and send repeatedly so user can see what's being transmitted
        update_interval = 1.0
        change_interval = 10.0
        duration = 35.0
        end_time = time.monotonic() + duration

        count = 0
        while time.monotonic() < end_time:
            count += 1
            # build a JSON-like representation of the measurements
            meas_list = [c.to_dict() for c in inj._cells.values()]
            ts = time.strftime("%H:%M:%S", time.localtime())
            print(f"{ts} [{count}] sending measurements: {meas_list}")
            inj.send()
            time.sleep(update_interval)
            # every change_interval seconds, swap the signal strengths to
            # encourage the UE to handover to the other cell
            if time.monotonic() + update_interval >= end_time:
                break
            if time.monotonic() % change_interval < update_interval:
                change_count += 1
                print(f"{ts} *** changing signal strengths (change #{change_count}) ***")
                inj.set_cell(nci=nci1, rsrp=rsrp_values[change_count % 2])
                inj.set_cell(nci=nci2, rsrp=rsrp_values[(change_count + 1) % 2])
        inj.close()


if __name__ == "__main__":
    test = TestOobHandover()
    test.test_oob_measurement_handover()
