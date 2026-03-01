# UERANSIM UE Test Harness
#
# This package provides a Python-based test harness for verifying the
# UERANSIM UE (User Equipment) implementation.  It acts as a fake gNB
# by speaking the RLS (Radio Link Simulation) binary protocol over UDP
# and can inject out-of-band measurements to test the measurement
# reporting framework (A2/A3/A5 events).
#
# Architecture:
#   FakeGnb  ── speaks RLS on UDP 4997 ── real nr-ue process
#                                            │
#   MeasInjector ── JSON on UDP 7200 ────────┘
#
# Sub-modules:
#   rls_protocol  – RLS binary protocol encoder/decoder
#   milenage      – Milenage (TS 35.206) + 5G-AKA key derivation
#   nas_builder   – 5G NAS (TS 24.501) message encoder/decoder
#   rrc_builder   – NR RRC (TS 38.331) message encoder via asn1tools
#   meas_injector – OOB measurement injection over UDP
#   ue_process    – nr-ue process lifecycle management
#   fake_gnb      – fake gNB orchestrator

from .fake_gnb import FakeGnb
from .meas_injector import MeasurementInjector
from .ue_process import UeProcess
