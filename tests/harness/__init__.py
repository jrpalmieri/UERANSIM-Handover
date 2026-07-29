# UERANSIM Test Harness
#
# This package provides a Python-based test harness for verifying the
# UERANSIM UE (User Equipment) and gNB (base station) implementation.  
# It acts as a fake gNB /fake UE / fake AMF to UE / gNB operations.
#
# Sub-modules:
#   rls_protocol  – RLS binary protocol encoder/decoder
#   milenage      – Milenage (TS 35.206) + 5G-AKA key derivation
#   nas_builder   – 5G NAS (TS 24.501) message encoder/decoder
#   rrc_builder   – NR RRC (TS 38.331) message encoder via asn1tools
#   ue_process    – nr-ue process lifecycle management
#   fake_gnb      – fake gNB orchestrator

from .fake_gnb import FakeGnb
from .ue_process import UeProcess
