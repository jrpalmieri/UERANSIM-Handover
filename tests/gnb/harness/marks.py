"""
Shared pytest marks for gNB tests.
"""

import os
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[3]
GNB_BINARY = PROJECT_ROOT / "build" / "nr-gnb"

gnb_binary_exists = pytest.mark.skipif(
    not GNB_BINARY.exists(),
    reason=f"nr-gnb binary not found at {GNB_BINARY}",
)

try:
    import sctp  # type: ignore
    _has_pysctp = True
except ImportError:
    _has_pysctp = False

needs_pysctp = pytest.mark.skipif(
    not _has_pysctp,
    reason="pysctp not installed — pip install pysctp",
)

try:
    import asn1tools  # type: ignore
    _has_asn1 = True
except ImportError:
    _has_asn1 = False

needs_asn1tools = pytest.mark.skipif(
    not _has_asn1,
    reason="asn1tools not installed — pip install asn1tools",
)
