#!/usr/bin/env bash
#
# Guard the "one runtime" invariant of the Release-18 ASN.1 migration:
#
#   src/asn/asn1c  ==  <asn1c-r18>/skeletons  +  migration shims
#
# A second, divergent copy of the skeleton runtime is the failure mode this
# migration exists to remove (see docs/ASN1_R18_Migration_Plan.md §2): two
# copies define the same symbols, the linker silently binds to whichever it
# scans first, and descriptors get written with one struct layout and read with
# another. Run this after regenerating any protocol tree.
#
# Usage: tools/check_asn1_runtime.sh [path-to-asn1c-r18]
#
set -u

REPO_RT="$(cd "$(dirname "$0")/.." && pwd)/src/asn/asn1c"
SKEL="${1:-${ASN1C_R18:-$HOME/repos/asn1c-r18}}/skeletons"

if [ ! -d "$SKEL" ]; then
    echo "not a skeletons directory: $SKEL" >&2
    echo "usage: $0 [path-to-asn1c-r18]" >&2
    exit 2
fi

status=0
unexpected=""
shimmed=""

for f in "$REPO_RT"/*.c "$REPO_RT"/*.h; do
    b="$(basename "$f")"
    if [ ! -f "$SKEL/$b" ]; then
        echo "MISSING UPSTREAM: $b is in the runtime but not in $SKEL"
        status=1
        continue
    fi
    if diff -q "$SKEL/$b" "$f" >/dev/null; then
        continue
    fi
    # A difference is allowed only where it is a documented migration shim.
    added="$(diff "$SKEL/$b" "$f" | grep '^>' | grep -vc 'MIGRATION SHIM')"
    if diff "$SKEL/$b" "$f" | grep -q 'MIGRATION SHIM'; then
        shimmed="$shimmed $b"
    else
        unexpected="$unexpected $b"
        status=1
    fi
    : "$added"
done

# Files the compiler would emit that the runtime does not carry are fine (OER
# is deliberately excluded, -DASN_DISABLE_OER_SUPPORT), so we do not check the
# other direction.

if [ -n "$shimmed" ]; then
    echo "shimmed (expected, remove as each tree is regenerated):$shimmed"
fi
if [ -n "$unexpected" ]; then
    echo "DIVERGED (not a documented shim):$unexpected"
fi

# A private skeleton copy inside a protocol tree is the other half of the
# invariant: those headers shadow the shared runtime for that library only.
for d in "$(dirname "$REPO_RT")"/*/; do
    case "$d" in
        */asn1c/) continue ;;
    esac
    priv="$(ls "$d" 2>/dev/null | grep -cE '^(constr_TYPE|asn_application|per_support|OCTET_STRING)\.h$')"
    [ "$priv" -gt 0 ] || continue
    name="$(basename "$d")"
    if [ "$name" = "xnap" ]; then
        # Known and tracked: the XnAP tree predates this migration and compiles
        # against its own 0.9.24-era headers. Stage 1 regenerates it and deletes
        # them; drop this exception then.
        echo "known: src/asn/xnap carries $priv private skeleton header(s) (removed in stage 1)"
    else
        echo "PRIVATE RUNTIME COPY: $d carries $priv skeleton header(s) that shadow src/asn/asn1c"
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "OK: one runtime, shims only"
fi
exit "$status"
