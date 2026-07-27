# ASN.1 Release-18 Migration Plan (RRC / NGAP / XnAP)

Status: **stage 0 complete** (2026-07-27). Stages 1–4 not started.
Author: analysis of `UERANSIM-Handover` @ `xn` (a68a5fad) and `/home/joe/repos/asn1c-r18` @ e835ff87, 2026-07-27.

Stage 0 results are recorded in [§6](#6-stage-0-as-built).

---

## 1. Where we are today (verified, not assumed)

### 1.1 Generated code

| Dir | Source spec | Compiler | Files | Notes |
|---|---|---|---|---|
| `src/asn/rrc` | `nr-rrc-15.6.0.asn1` (756 hdrs) **+** `/tmp/nr-rrc-17.3.0-patched.asn1` (35 hdrs) | asn1c-0.9.29 | 1600 | Hybrid. Also hand-edited across ≥7 commits. |
| `src/asn/ngap` | split `NGAP-*.asn` (Rel-15.8 lineage, cf. `tools/ngap-15.8.0.asn1`) | asn1c-0.9.29 | 2144 | Clean, no hand edits found. |
| `src/asn/xnap` | `xnap-rel18-v18_8.asn1` | **asn1c-0.9.24** fork | 2765 | Already R18, but see §1.3. Ships its own private skeleton copy. |
| `src/asn/asn1c` | — | — | 79 | Shared runtime: an *older* 0.9.29-era snapshot **+ APER**. |

RRC files carrying hand edits (these are the ones a regeneration destroys):
`ASN_RRC_CondTriggerConfig-r16`, `ASN_RRC_EventTriggerConfig`, `ASN_RRC_CondReconfigToAddMod`,
`ASN_RRC_ConditionalReconfiguration`, `ASN_RRC_ReportConfigNR`, `ASN_RRC_RRCReconfiguration-v1610-IEs`,
`ASN_RRC_RRCReconfiguration-v1700-IEs`, `ASN_RRC_NTN-TriggerConfig-r17`, plus unprefixed strays
(`OnDemandSIB-Request-r16.c`, `T316-r16.c`).

### 1.2 Identifiers are prefixed today; asn1c-r18 will not prefix them

`nm` over the three built archives: `libasn-ngap.a` 7053 symbols, `libasn-xnap.a` 5844, `libasn-rrc.a` 2667,
**0 collisions** — because every generated C identifier carries `ASN_RRC_` / `ASN_NGAP_` / `ASN_XNAP_`
(`ASN_XNAP_A2XPC5FlowBitRates_t`, `asn_DEF_ASN_NGAP_AMF_TNLAssociationSetupItem`, …).

`asn1c-r18`'s `-fprefix` renames **only file names and include guards** (README-R18 says so explicitly).
Unprefixed R18 output cannot work here:

- `nr-gnb` links `asn-rrc` + `asn-ngap` + `asn-xnap` into one binary → thousands of duplicate
  `asn_DEF_*` / `*_constraint` definitions;
- three TUs include RRC **and** NGAP headers in the same translation unit and would hit hard
  C redefinition errors: [handler.cpp](src/gnb/rrc/handler.cpp), [connection.cpp](src/gnb/rrc/connection.cpp),
  [handover_n2.cpp](src/gnb/ngap/handover_n2.cpp);
- ~896 distinct prefixed identifiers appear in hand-written C++ (RRC 240, NGAP 455, XnAP 201).

**Decision (agreed): patch `asn1c-r18` so `-fprefix` also prefixes C identifiers.**

### 1.3 XnAP has a live ABI bug that this migration fixes

`src/asn/xnap` carries a private 0.9.24-era skeleton copy, and the target's include path puts it *first*:

```
gcc -I src -I src/asn/xnap -I src/asn/asn1c ... ASN_XNAP_BitRate.c
```

So XnAP TUs compile against a **0.9.24 `asn_TYPE_descriptor_t`** (flat `ber_decoder`/`uper_decoder`/
`aper_decoder` function pointers, `char *name`, `int tags_count`, `per_constraints`) while the runtime
implementations and every C++ caller see the **0.9.29 layout** (`asn_TYPE_operation_t *op`,
`encoding_constraints`, `unsigned` counts). The private `.c` skeletons are not even compiled — the
CMake glob is `ASN_XNAP_*.c` — so the linked implementations are the 0.9.29 ones.

Net effect: every XnAP type descriptor is written with one struct layout and read with another by
`aper_decode()` in [src/gnb/xn/encode.hpp:43](src/gnb/xn/encode.hpp#L43). This is undefined behaviour,
not a style problem, and it is consistent with `docs/Xn_summary.md`'s finding that the Xn path has never
run end-to-end. Regenerating XnAP with the same compiler and runtime as everything else removes it.
The `type_compare_f` compatibility shim in [constr_TYPE.h](src/asn/asn1c/constr_TYPE.h) exists only to
placate this old code and gets deleted with it.

### 1.4 APER: the runtime carries it, the fork does not

- `asn1c-r18/skeletons` is **byte-identical** to upstream 0.9.29 skeletons (`/home/joe/repos/asn1c`)
  and has **zero** APER support.
- `src/asn/asn1c` is an older 0.9.29-era base **plus** an APER port: `aper_decoder`/`aper_encoder`
  appended to `asn_TYPE_operation_s`, `aper_*` functions in ~24 `.c` files, `aper_decode()`/`aper_encode()`
  entry points, aligned open-type handling.
- NGAP, XnAP and one RRC path all call `aper_decode()`
  ([ngap/encode.hpp:66](src/gnb/ngap/encode.hpp#L66), [xn/encode.hpp:43](src/gnb/xn/encode.hpp#L43),
  [xn/handover.cpp:2236](src/gnb/xn/handover.cpp#L2236), [rrc/encode.hpp:88](src/lib/rrc/encode.hpp#L88)).

Crucially: **no generated file contains APER code.** Zero files in `src/asn/{rrc,ngap}` define an
`asn_TYPE_operation_t`; constrained primitives just point at `&asn_OP_INTEGER` etc. APER is a *pure
runtime* concern, so R18-generated code will use it unchanged. (Old XnAP code is the exception — it
emits per-type `_decode_aper` wrappers and assigns `td->aper_decoder`; that vanishes on regeneration.)

The two struct layouts generated code actually depends on — `asn_TYPE_member_s` and
`asn_encoding_constraints_s` — are **identical** between the repo runtime and the fork. The only
`constr_TYPE.h` deltas are the two APER fields and the XnAP shim.

**Decision (agreed): port APER onto the `asn1c-r18` skeletons**, so one command emits a complete runtime.
Note the port is *not* a clean patch application: the repo runtime is an older base with its own drift
(e.g. `INTEGER.c` has `field_unsigned`/`ULONG_MAX` handling upstream lacks; `NULL.c` is implemented in
terms of `BOOLEAN`; 2014-era copyright headers). It must be a deliberate function-by-function port onto
the newer base, not a bulk file copy.

### 1.5 Inputs and toolchain readiness

- `asn1-definitions/` now holds `rrc-rel18-v18_9.asn1`, `ngap-rel18-v18_9.asn1`, `xnap-rel18-v18_8.asn1`.
  (Two WSL `*:Zone.Identifier` junk files are untracked — delete and `.gitignore`.)
- `asn1c-r18/asn1c/asn1c` is built; `config.h` has `HAVE_128_BIT_INT 1`, which NGAP/XnAP require.
- Expected output volume: RRC 2674 TUs, XnAP 1394, NGAP 1266 — the repo gains roughly 3–4k files
  over today's 6.5k.

---

## 2. The shared-runtime question, answered

> *"The new XnAP files will likely need new common files that differ from the existing ones in
> `src/asn/asn1c` — how do we avoid conflicts while the layers change one at a time?"*

**Never let two runtimes exist.** A parallel `src/asn/asn1c_r18` cannot be made safe: both copies define
`asn_DEF_NativeInteger`, `aper_decode`, `uper_decode`, `asn_OP_SEQUENCE`, … under identical names. With
static archives the linker does not error — it silently binds each reference to whichever archive it
scans first. That is *exactly* the failure mode already live in XnAP (§1.3), reproduced three times over
and much harder to spot.

**Instead, make the runtime Stage 0 and swap it in place, before any protocol is regenerated.** This is
safe precisely because of §1.4: the new runtime is a superset in behaviour and layout-compatible in the
structs generated code touches. Concretely:

- **Stage 0** replaces `src/asn/asn1c` with `upstream-0.9.29 + APER`. The *existing* Rel-15 RRC and NGAP
  code recompiles against it untouched, and the golden-vector harness proves encodings are unchanged.
  XnAP is not rebuilt against it — it keeps compiling against its own private headers, i.e. it stays
  exactly as (un)broken as today. No regression, no second runtime.
- **Stage 1** regenerates XnAP and *deletes* the private skeleton copy, so XnAP joins the single shared
  runtime. From here on there is one runtime, one compiler, one ABI.
- **Stages 2–3** regenerate NGAP and RRC against that same already-proven runtime. Each stage is a
  self-contained, buildable, testable commit.

The one thing to watch: `asn1c_run.sh -c` writes the skeleton set as a *sibling* of `-D`. Generate into a
scratch tree, then **diff** the emitted `asn1c/` against the Stage-0 runtime and reconcile deliberately —
never let a later generation silently overwrite the APER-patched runtime. A CI/pre-commit check that
`src/asn/asn1c` differs from `asn1c-r18/skeletons` by exactly the APER delta is cheap insurance.

---

## 3. Stages

### Stage 0 — toolchain and runtime (no protocol code changes)

**0.1 Extend `-fprefix` to C identifiers in `asn1c-r18`.**
- Choke point: `construct_base_name()` / `c_name_impl()` in `libasn1compiler/asn1c_naming.c` — type
  names, `asn_DEF_*`, `asn_MBR_*`, `asn_SPC_*`, compound member names and `_PR_` enumerators all derive
  from `base_name`.
- Must **not** prefix builtin/skeleton type names produced by `asn1c_type_name(..., TNF_CTYPE)`
  (`NativeInteger`, `OCTET_STRING`, `asn_DEF_NativeInteger`, `asn_OP_SEQUENCE`, …). Getting this
  boundary wrong is the main risk in this step.
- Acceptance oracle: regenerate XnAP, `nm` the archive, and diff the symbol set against today's
  `libasn-xnap.a` symbol list. Every generated symbol must carry `ASN_XNAP_`; no skeleton symbol may.
- Update `README-R18.md` (its current text documents `-fprefix` as filename-only) and add a compiler
  test under `tests/`.

**0.2 Port APER onto `asn1c-r18/skeletons`.**
- Add `aper_decoder` / `aper_encoder` to `asn_TYPE_operation_s`, populate every `asn_OP_*` table.
- Port `aper_*` implementations for: `INTEGER`, `NativeInteger`, `NativeEnumerated`, `BOOLEAN`, `NULL`,
  `OCTET_STRING`, `BIT_STRING`, `ANY`, `OPEN_TYPE`, `UTF8String`/`VisibleString`/`PrintableString`,
  `OBJECT_IDENTIFIER`, `constr_SEQUENCE`, `constr_CHOICE`, `constr_SET_OF`, `constr_SEQUENCE_OF`,
  plus `per_decoder.c`/`per_encoder.c` entry points, `per_opentype.c`, `per_support.c` alignment helpers.
- Source of truth is `src/asn/asn1c`, but port functions onto the newer base; do not bulk-copy
  (§1.4). Where the repo base diverges for non-APER reasons, keep the newer upstream code unless a
  golden vector says otherwise.
- Keep `-DASN_DISABLE_OER_SUPPORT` and generate with `--no-oer`; OER skeletons stay out of the build.

**0.3 Build the golden-vector harness *before* touching anything.**
- Extract real NGAP/RRC/XnAP PDUs from the existing captures under `tools/dashboard/logs/*/trace_*.pcap`,
  plus synthetic round-trip fixtures for the message types the code actually builds.
- New test target (suggest `tests/asn1_golden/`) that decodes each vector, dumps a canonical text form,
  and re-encodes; expected outputs generated from **today's** build and committed.
- This harness is the acceptance gate for every subsequent stage. Everything else in this plan is
  mechanical; this is what makes the mechanics verifiable.

**0.4 Swap the runtime.** Replace `src/asn/asn1c` contents (same file subset), retain the
`type_compare_f` shim until Stage 1. Rebuild the *unchanged* RRC/NGAP/XnAP trees.

**Gate:** full build clean; golden vectors byte-identical; `tests/gnb` and `tests/ue` suites at parity.

---

### Stage 1 — XnAP (smallest C++ surface: ~201 identifiers; validates the whole toolchain)

1. Generate:
   `asn1c_run.sh -p ASN_XNAP_ -c -D <scratch>/xnap --no-oer asn1-definitions/xnap-rel18-v18_8.asn1`
2. Diff the emitted sibling `asn1c/` against the Stage-0 runtime; reconcile explicitly (expect: identical
   except the APER delta).
3. Replace `src/asn/xnap`; **delete** the private skeleton copies, `converter-sample.c`, `pdu_collection.c`.
4. Simplify `src/asn/xnap/CMakeLists.txt` — with no private headers left, the shadowing hazard is gone;
   keep the glob restricted to `ASN_XNAP_*.c`.
5. Remove the `type_compare_f` shim from the runtime.
6. Fix call sites, principally `src/gnb/xn/*` and `src/lib/asn/*`. Expect churn from 0.9.24→0.9.29 shape
   changes (`asn_OP_*` tables, `const` qualifiers, `unsigned` counts) on top of any R18 IE drift.

**Gate:** `-Wall -Wextra -pedantic` clean; XnAP golden vectors pass; Xn handover tests exercise a real
encode/decode round trip for the first time.

---

### Stage 2 — NGAP (~455 identifiers, 1266 TUs, Rel-15.8 → Rel-18.9)

Watch for:
- `value_PR_*` enumerator drift. The fork now *uniquifies* duplicate CHOICE presence enumerators as
  `X_2`, `X_3` rather than dropping them, and `presence_index` is derived by counting alternatives —
  any call site that switches on presence must be re-checked, not just recompiled.
- IE-container / `ProtocolIE-Field` naming changes across releases.
- Interop: open5gs's AMF is not Rel-18. R18 ASN.1 stays wire-compatible for the IEs we already send,
  but do not start populating new-in-R18 IEs during this stage — keep the change purely mechanical.

**Gate:** NGAP golden vectors; registration + N2 handover integration tests against open5gs.

---

### Stage 3 — RRC (~240 identifiers, 2674 TUs, Rel-15.6 → Rel-18.9 — the largest semantic jump)

1. Regenerate; delete `src/asn/rrc_backup_r15` and the unprefixed strays.
2. **Retire the hand-edited and Rel-17-spliced files** (§1.1) and re-derive their behaviour from stock
   R18 definitions. These are CHO and NTN critical paths — `CondTriggerConfig-r16`,
   `CondReconfigToAddMod`, `ConditionalReconfiguration`, `EventTriggerConfig`, `NTN-TriggerConfig-r17`,
   `RRCReconfiguration-v1610/v1700-IEs` — so budget real time here, not just compile-error chasing.
   Each one needs a behavioural test, not just a green build.
3. Cross-check the open items already recorded in `docs/RRC_summary.md`: `physCellId`-as-NCI, and the
   SIB19 NCI layout fix (wire-format change) — regeneration is the natural moment to settle them.
4. `-findirect-choice` is used both before and after, so CHOICE members stay pointers; still verify,
   since member pointer-ness changes ripple through every call site.

**Gate:** RRC golden vectors; `rrc_reference_location_tests`, `rrc_d1_probe`, SIB19/CHO/handover suites.

---

### Stage 4 — consolidation

- Check in `tools/regen_asn1.sh` recording the exact flags per protocol, so regeneration is one command
  and never again archaeology from file headers.
- Update `asn1-definitions/README.md` with spec versions, compiler commit, and commands.
- Move or delete stale inputs in `tools/` (`ngap-15.8.0.asn1`, `ngap-17.9.asn`, `rrc-15.6.0*.asn1`).
- Delete the `*:Zone.Identifier` files; add a `.gitignore` rule.
- Commit the `asn1c-r18` changes in their own repo and record the commit hash here.

---

## 4. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| APER port subtly wrong (alignment, open types, extension markers) | **High** — silent wire corruption | Golden vectors from real pcaps, established in Stage 0 *before* any change |
| `-fprefix` identifier patch over-reaches into skeleton names | High | `nm` symbol-set diff against today's archives as a hard gate |
| RRC hand-edits encode behaviour not present in stock R18 defs | High | Treat Stage 3 item 2 as feature work with tests, not a merge conflict |
| Two runtimes coexisting by accident | High | Stage 0 ordering; CI check that `src/asn/asn1c` = fork skeletons + APER delta |
| `value_PR_*` renumbering breaks presence-index logic | Medium | Audit every `_PR_` switch during Stages 2–3 |
| open5gs AMF rejects R18-shaped PDUs | Medium | Keep Stages 1–3 mechanical; no new IEs until after |
| Build time / repo size (+3–4k files) | Low | Expected; revisit globs if it bites |

## 6. Stage 0 as built

Completed 2026-07-27. The build is green, and every behavioural baseline captured
before the swap is byte-identical after it.

### 6.1 `-fprefix` now prefixes C identifiers (`asn1c-r18`)

The fork already prefixed *references* to user types but not their *definitions*,
so `-fprefix` output did not even compile. Fixed by inverting the default in
`asn1c_make_identifier()`: with an expression in hand it prefixes unless
`AMI_NO_PREFIX` is passed. Opt-outs are exactly the places a prefix must not
appear — C structure member names (`MKID_member()`, new), the trailing components
of a compound name (the leading one carries it), and output file names (already
prefixed by `asn1c_prefixed_filename()`). `construct_base_name()` and
`out_name_chain()` emit the prefix from their innermost recursion only.

Verified against the one input that produces today's tree, `tools/rrc-15.6.0.asn1`:

| Check | Result |
| ----- | ------ |
| Identifiers matching today's `src/asn/rrc` exactly | 4165 (of 4648 generated / 4772 present today; the remainder differ for the two reasons below, not for prefixing) |
| Generated symbols missing the prefix | **0** |
| Translation units compiling (`gcc -fsyntax-only`) | **766 of 766** |
| Exported symbols in the linked archive lacking `ASN_RRC_` | **0 of 2563** |
| External references | 23, all skeleton/libc (`asn_OP_SEQUENCE`, `calloc`, …) — none prefixed |
| `make check` in `asn1c-r18` | 89 pass, 0 fail, 1 expected xfail |

Remaining old-vs-new naming differences are compiler-version drift, not the
patch: this asn1c drops the `__ext1__` component from extension-group compound
names (0 occurrences with *and* without `-fprefix`, 245 in the current tree). No
C++ call site uses those names. The rest of the difference is the tree itself:
today's `src/asn/rrc` carries the hand-spliced Rel-16/17 CHO and NTN types
(§1.1), which a stock Rel-15.6 regeneration naturally does not reproduce.

### 6.2 APER ported onto the fork's skeletons

The reference runtime's entire delta over upstream turned out to be **43 APER
functions plus `asn_int642INTEGER`/`asn_uint642INTEGER`** (the latter called from
[handover.cpp](../src/gnb/xn/handover.cpp)); everything else was drift where the
fork is newer. All of it was ported function-by-function onto the newer base,
plus: the two `asn_TYPE_operation_s` slots, `ATS_ALIGNED_{BASIC,CANONICAL}_PER`,
the APER cases in `asn_application.c`, the header prototypes and
`X_decode_aper → OCTET_STRING_decode_aper` macros, and the PER-constraint bounds
widened to `long long`.

Two defects were caught during the port, both by the fork's own test suite:

- **Op-table slot shift.** `asn_TYPE_operation_t` initialisers are positional, so
  every table needs the two new APER slots — including the 18 skeleton files the
  repo runtime does not carry (`NativeReal.c`, `REAL.c`, the string types,
  `constr_SET.c`). Missing them made `random_fill` land in the `aper_decoder`
  slot; ASan caught it as a stack-buffer-overflow inside `NativeReal_random_fill`
  reached from `SET_OF_decode_aper`.
- **PER-less link sets.** The appended APER blocks needed upstream's
  `#ifndef ASN_DISABLE_PER_SUPPORT` guard, or tools that link a subset of the
  skeletons (`unber`) fail to link.

One cosmetic defect came across with the code: `aper_decode()` had a brace-less
`if(!td->op->aper_decoder) ASN__DECODE_FAILED;` followed by a call indented as
though it were guarded. Behaviour was already correct — the macro returns — but
gcc warned on every build, so the indentation was corrected. No behavioural
change; the golden vectors were re-verified afterwards.

### 6.3 Golden-vector harness

[tests/asn1_golden.cpp](../tests/asn1_golden.cpp), target `asn1_golden`, plus a
corpus of 1156 real NGAP PDUs in
[tests/data/asn1_vectors/](../tests/data/asn1_vectors/) — drawn from 29786 unique
PDUs in the tracked captures, covering all 17 `(pduType, procedureCode)` pairs
present, selected with a fixed seed. Every vector decodes and re-encodes
**byte-identically**, so any diff is a real behavioural change.

RRC/UPER has no captured vectors (the pcaps are core-side only; no RLS traffic),
so it is covered by the deterministic output of `rrc_d1_probe`,
`tools/gen_sib1_hex.cpp` and `rrc_reference_location_tests`.

### 6.4 The swap

`src/asn/asn1c` is now `asn1c-r18/skeletons` + APER + exactly two documented
shims, both removable as their tree is regenerated:

| Shim | Why | Remove at |
| ---- | --- | --------- |
| `type_compare_f` in `constr_TYPE.h` | XnAP headers declare prototypes with it | stage 1 |
| `aioc__undefined = 0` in `asn_ioc.h` | current NGAP tables mark typeless IOC rows with it and test for it; asn1c-r18 instead checks `type_descriptor` for NULL | stage 2 |

[tools/check_asn1_runtime.sh](../tools/check_asn1_runtime.sh) enforces the
invariant: the runtime may differ from the fork's skeletons only by documented
shims, and no protocol tree may carry a private skeleton copy (XnAP is
grandfathered until stage 1).

### 6.5 Acceptance results

| Baseline | Before → after |
| -------- | -------------- |
| NGAP APER, 1156 vectors | identical (all byte-identical round-trips) |
| `rrc_d1_probe` (RRC UPER, CHO/D1 encode+decode) | identical |
| `gen_sib1_hex` (MIB/SIB1/RRCSetup/DLInfoTransfer/RRCRelease) | identical |
| `rrc_reference_location_tests`, `sat_lib_tests`, `sat_time_tests` | identical, all pass |
| Full build incl. `nr-gnb`, `nr-ue` | clean, no new warnings |
| `pytest tests/ue` (A/B, old runtime vs new) | identical: 45 passed / 16 failed / 14 skipped / 10 errors **both ways**, same 26 test IDs — **zero regressions** |

The UE suite's 26 pre-existing failures are unrelated to ASN.1 (the first one,
`test_ue_starts_and_reaches_fake_gnb`, is an RLS heartbeat that never arrives)
and reproduce exactly on the pre-swap runtime; they were not investigated
further here.

`pytest tests/gnb` — the suite with the fake AMF, which would exercise the
ported APER end-to-end — **cannot run in this environment**: `pysctp` is not
installed (it needs `libsctp-dev`), so all 38 tests skip. Installing it would
make the strongest available APER regression test runnable and is worth doing
before stage 2.

---

## 7. Open items for you

1. **Rel-15 fallback** — is there any reason to keep `src/asn/rrc_backup_r15`, or is deletion in Stage 3 fine?
2. **RRC hand-edits** — do you want them reconstructed feature-for-feature on R18, or is the intent that
   stock R18 definitions supersede them and the C++ adapts?
3. **`asn1c-r18` ownership** — should the `-fprefix` and APER work land as commits in that repo (and be
   pinned by hash here), or be carried as patches inside this repo?
4. **Interop target** — which AMF/gNB versions must stay interoperable after the jump? That decides how
   conservative Stage 2 has to be.
