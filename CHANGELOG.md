# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning scheme: OPEN — no versioning directive found in the allowed
reads (KICKOFF-CLEANROOM.md names no scheme; DECISIONS.md records none).
No version number or date is claimed below until the coordinator decides.

## Gate history (moved from README.md, 2026-10-09)

Round narrative and stale tables, preserved verbatim from the
pre-product README. Current numbers live in README.md; per-wave detail
in WAVES.md.

### Duet lands 1–6 (Oct 2026, lanes/lane-4)

- land1 `90947a6c8` + `3dd76decf` (2026-10-06): m9e V-spanflush +
  t9e L9-BACKFILL vehicles (encode).
- land2 `0902aee1e` (2026-10-07): t5e5 V-TIGHT-L5 vehicle (encode;
  re-gate tL5e +12.0% SEP vehicle-vs-stock on the merged tip).
- land3 `d58157de9` (2026-10-07): t9d5 TRUE3-X8 vehicle (decode).
- land4 `0ab3110bc` (2026-10-08): tspec U16 vehicle (decode).
- land5 `10e4ee30d` (2026-10-08): mcopy2 D27DBL vehicle (decode).
- land6 `6b7ff34dd` (2026-10-08): fetch2 dead-check-del vehicle
  (`b775ba42`) + kraft-edge boundary test (decode). Parent-verify
  green: smoke 4928/0, full 12784/0, holdout 3008/0, fresh 3008/5
  identical pre-existing, units stock + 20 kraft PASS, NEW=0.
  Public export + tag `duet-land6` pushed.
- REGAP-6 (measured 2026-10-09): direct Apple-vs-land6 decode gaps
  at `6b7ff34dd`, n=70 in-process both sides — tL0d +35.9 SEP,
  mL0d +48.3 pooled / core +35–39, mL1d +38.9 SEP, tL9d +33.2 OV,
  tL5d +30.2 OV, mL9d +18.9 / core +13.5–16.7 OV, mL5d +15.2 /
  core +12–18 OV, tL1d +22.2 OV mush. See README.md (current).

### R28 matrix (stale; last full enc+dec absolute matrix)

Throughput in MiB/s, medians over n=70 (10 gated runs × 7 reps,
interleaved port-vs-Apple, same box back-to-back), MacBookAir M1,
Apple clang 21 `-O2 -std=c11`. Tables read the R28 matrix on the
R27 ship tip (`port/src/lzmesh_enc.c` md5 3fec8aa3,
`port/src/lzmesh_dec.c` md5 5a147a81; R27 SHIPPED tL5d-d1,
text+mixed L0 dec rel +20.1/+17.9, gap-share +31.8/+21.4pp
gate-primary; R28 re-measured +22.0/+19.3 on the ship tip (window
note: different nulls — gate null + R27 anchors vs R28 ship-tip null;
R28 null is the R28 primary; gap-share stays gate-primary per memo
FORMAL, no R28-anchor recompute); ship numbers now in tables below).
R28 HELD (no ship: H1N1 micro banked bytes-only, SFX/stack/v2d
CLOSED, canon/hist/layenc staged for R29; detail in history). Both
sides in-process: port `bench/bench.c` vs Apple
`bench/abench` (same harness, no fork+exec+pipe floor).

Encode, port / Apple:

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 481.70 / 1018.44 | 137.36 / 323.21 | 105.78 / 245.82 | 87.90 / 226.86 |
| mixed-128k | 401.29 / 862.07 | 186.71 / 461.25 | 140.21 / 350.14 | 112.31 / 296.21 |
| zeros-64k | 1524.39 / 1953.12 | 2976.19 / 5208.33 | 2976.19 / 5208.33 | 2976.19 / 919.12 |

Decode, port / Apple (each side decodes its own bytes):

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 1552.80 / 2304.20 | 875.66 / 1404.49 | 865.05 / 1506.02 | 865.05 / 1543.21 |
| mixed-128k | 1506.02 / 2314.81 | 1269.07 / 2450.98 | 1666.67 / 2659.57 | 1533.80 / 2403.85 |
| zeros-64k | 62500.00 / 31250.00 | 31250.00 / 5681.82 | 31250.00 / 5681.82 | 31250.00 / 5681.82 |

Standings (R28 gaps, port vs Apple): the worst six cells are all
encode — mixed L9 −62.1%, text L9 −61.3%, mixed L5 −60.0%,
mixed L1 −59.5%, text L1 −57.5%, text L5 −57.0% (all SEPARATED).
Decode trails worst on mixed L1 −48.2% and text L9 −43.9%.

Ships to date (per-wave detail in WAVES.md):

| wave | ship | cells moved |
|------|------|-------------|
| R4 | decres + enc5-C3 | text dec L1/L5/L9, text enc L5 |
| R6 | T9+QBR | L9 enc text+mixed |
| R8 | l9new + mix1neutral | all 6 text/mixed enc |
| R9 | mix1 + text1 | mixed L1, text L1 enc |
| R10 | textdec | all 6 text/mixed dec |
| R11 | lastblock | exact-shape text dec L1/L5/L9 |
| R12 | l0 + h123 | L0 enc, mixed L1/L5 enc |
| R13 | l0res | mixed L0 enc |
| R15 | tl1 stack+T2+S4 | text L1/L5/L9 enc |
| R17 | D20 | text L9 enc |
| R18 | H9-sink | mixed L9/L1/L5 enc |
| R23 | tL9d D1P4+PGO | text L9 dec |
| R24 | tL5d pure-T0-PGO | text L5 dec (build-only, zero src delta) |
| R27 | tL5d-d1 L0-ship | text+mixed L0 dec (rel +20.1/+17.9, gap-share +31.8/+21.4pp) |

No-ship waves (holds/kills, detail in history): R7 (matrix fold
only), R14, R16, R19, R20, R21, R22, R25, R26, R28.

Full per-wave history (R28–R4, newest first): WAVES.md.
Future land folds append new waves there.

Reading the gap (both sides in-process, no pipe floor):

- Apple ahead on all 16 text/mixed codec cells (all SEPARATED,
  gaps −32.6% to −62.1%): real port deficits, the optimization
  frontier. Text L0 enc carries wide bands (median 481.70,
  band 396.07–489.24, Apple wide too 871.08–1082.25 — sprawl, not
  signal; holds cross-run (485.44→481.70 R27→R28, bands
  overlap)).
- Zeros cells are timer-floor artifacts (NO-CLAIM): enc medians
  flip between 2976/3125 quanta run to run; zeros L0 dec reads
  n=61/70 (ns==0 samples dropped by the cmp filter) OVERLAP;
  L1/L5/L9 dec +450.0% x3 SEPARATED-port is pure
  quantum artifact, not a speed claim (median flip back
  62500→31250 inside the same band).

Byte note (R28 era): port enc == Apple enc on 9/12 cells;
mixed-128k L1/L5/L9 differ (e01 34181@1152, same length; e05
32799v32796@2; e09 32708v32707@8; every output self-roundtrips and
cross-decodes OK, 12/12 both directions; not ship-introduced). Every
wave re-verifies the same 9/12 IDENT + 3 mixed DIVs on its ship and
HOLD stacks (FULL/HOLD/FRESH 0 NEW base-vs-ship direct, lane-gated +
land-verified); DIV lines byte-identical every round, md5 5d68b09c
R12–R28. Sizes within 3 B, so dec-timing inputs are size-matched.

PGO (R28 era, opt-in build-only `make pgo`; default tree untouched, no
source change; re-measured on the R28 tree, Air, interleaved --ab,
n=70, 6SEP/18OVER, composition flipped): the 0-slower streak ENDS at
18 — text L0 enc −2.3% SEPARATED-slower is a PRIMARY-veto-class lean
(FORMAL BINDING, deepening R24 −1.0 → R28 −2.3; no PGO-stack variant
with tL0e SEP-slower ships). Text-256k enc L1/L5/L9 +10.2/+4.7/+3.8%
SEPARATED; mixed-enc L1/L5 +5.1/+5.3% SEPARATED (L1 OV→SEP reprice, L5
STANDS T3-clean gap 2.61) with L9 +4.4% OVERLAP; text-dec L1/L5/L9
+1.8/+5.9/+6.2% OVERLAP (L5/L9 SEP→OV ship-explained); mixed-dec
L1/L5/L9 −1.0/+7.1/+4.5% OVERLAP (NO-CLAIM). Caveats: mixed L0 enc
−3.4% OVERLAP carries a rule-1 flagged limitation (drop-1-only −3.3
SEP-slower, pack-level; watch R29); text/mixed dec L0 −0.6/−1.2%
OVERLAP (NO-CLAIM); zeros all OVERLAP (timer floor); drop-1 secondary
9/15. Byte-identity holds under PGO: PGO-binary enc == normal enc
12/12.

Sources (R28): every cell traces to `tmp/matrix-r28/matrix-ev/
cmp-matrix.txt` (bands + n + verdicts), reproducible byte-identically
from `tmp/matrix-r28/matrix-ev/matrix/` run TSVs; PGO from
`tmp/matrix-r28/pgo/cmp-pgo.txt` + `tmp/matrix-r28/pgo/gated-pgoab/`;
exact-cap from `tmp/matrix-r12/exactcap/cmp-exactcap.txt` (not re-run
R28; R12 cite retained). Gate: load1 < 16, 0 refusals on all runs
(matrix 1.12–2.03, pgoab 1.44–1.48), pin `taskpolicy-t0l0`. Method:
`docs/PERF.md` + `bench/GATED-PROTOCOL.md`; Apple harness: in-process
`bench/abench`.

### Byte-exactness record, 2026-09-28 (stale pin; claim re-proven at land6)

As of 2026-09-28, source lanes/lane-4 @ e582954c (codec-identical to
the gated P6 merge 88b5a27b — e582954c touches scratch only) plus
comment-only SPDX headers. `make unit`: 44/44 suites green, fail=0.
Vector fixtures: 25 .bin + manifest (26 files, unchanged). Full corpus
12784/12784 (seeds 0–16, e00/e01/e05/e09, tier full); holdout 3008/3008
(seeds 17–20). Battery: `tests/battery/battery.py --oracle
./oracle_probe --port ./port_cli --out <dir> --seeds <n> --seed-offset
<off> --selectors e00,e01,e05,e09 --tier full`; full `--seeds 17
--seed-offset 0`, holdout `--seeds 4 --seed-offset 17`. Fresh-slice 5
(pre-existing, merge-inert) match the land6 fresh-5 in README.md.

## [Unreleased]

### Decoder-complete candidate 2026-09-19 (NOT a versioned release)

No version number is claimed: the versioning scheme, tag, host, and
license are coordinator-OPEN (`docs/RELEASING.md` §1, D10/O1 — "do not
invent values"). This entry freezes the proven decoder state plus the
candidate snapshot in `release/` for review.

Proven at HEAD (evidence: `release/evidence/`, `release/MANIFEST.md`):

- Decoder COMPLETE: agrees with Apple on all 116,612 neighborhood
  mutants (gaps=0, over_accepts=0), suspect-seed gate divs=0/4608,
  valid corpus 384/384 identical; fixes: input-cap rule (#1) and
  lit-run bound `lit_used+run <= tokc+round32up(litc)-1` (#2).
- Battery: `make smoke` PASS 1232 cells buckets EMPTY (200.5s);
  4-selector smoke e00,e01,e05,e09 PASS 4928 cells buckets EMPTY
  (841.1s); `make unit` RC=0 35/35, 950 PASS + 6 XPASS + 13 XFAIL;
  `make selftest` OK. Build: `make all` RC=0, zero warnings.
- Cross-Apple-build proof (4 distinct libcompression builds —
  host macOS 27.0, iOS-sim 26.5/27.0, tvOS-sim 27.0): 5-way × 116,612
  unanimous, all 8 agree/diverge pairs 0, Apple-Apple variance zero.
- Platform builds, all rc=0 zero warnings libc-only: iOS
  device/sim-arm64/sim-x86_64, tvOS 3 slices, watchOS arm64/sim-arm64/
  arm64_32, macOS-x86_64; sim slices run (units 34/35, gap is
  TEST-ONLY `system()` unavailable on iOS/tvOS SDKs). Linux unbuilt
  (Docker daemon unavailable; expected-trivial pure C11+libc).
- Encoder OPEN (encbatt lane owns it; ~3,957 ENC_DIFF): full-tier
  verdict FAILs on encoder buckets only. Zero decoder buckets anywhere.
- Provisional `LZMESH_VERSION_*` (0.0.0-dev) marker added to
  `include/lzmesh.h` for downstream feature-gating; replaced by the
  real version at release-prep time. Not a version claim.

### Skeleton stage (prior)

First public release of the standalone clean-room LZMESH port (skeleton
stage; codec sources land via implementer lanes). Facts below are drawn
from `README.md`, `DECISIONS.md`, `docs/`, `Makefile`, and
`include/lzmesh.h` only.

### Added

- Public buffer-only C API in `include/lzmesh.h` (declarations only;
  lanes provide definitions): `lzmesh_encode` with level 0/1/5/9 (parse
  effort only, no grammar change), `lzmesh_decode` with no selector (one
  decoder handles all levels), `lzmesh_encode_scratch_size`,
  `lzmesh_decode_scratch_size`, and the `lzmesh_decoded_size` framing
  walker for sizing decode destinations. Deterministic encode; no
  checksum anywhere in the format; undersized decode destinations
  truncate with no error signal (DECISIONS.md D4–D6).
- Single-`Makefile` build: stock `cc`, C11, no dependencies, macOS +
  Linux. Targets `all` (`liblzmesh.a` + `port_cli`), `port_cli`,
  `selftest`, `smoke`, `full`, `unit`, `clean` (DECISIONS.md D1–D2).
- Battery CLI contract: `port_cli enc|dec <selector-hex>` stdio byte
  pipe, exit 0/10 discipline, `DECODE_SIZE` sizing, determinism, no
  chatter (`tests/battery/README.md`; DECISIONS.md D9, D14).
- Vendored, pinned divergence-battery framework under `tests/battery/`
  with `VENDORED.md` pin (file shas, date, resync rule); resync is
  wholesale copy plus re-pin (DECISIONS.md D7).
- `tests/unit/` shell for fast unit tests plus `make unit` wiring
  (DECISIONS.md D12).
- Documentation shells: `docs/BUILD.md`, `docs/TESTING.md` (tiers,
  oracle setup, zero-tolerance verdict, planned fuzzers), `docs/API.md`,
  `docs/PERF.md` (method plus empty results tables).
- Clean-room shells: `docs/CLEANROOM-LOG.md`, `docs/DERIVATION.md`,
  `spec-queries/` one-way channel; `.gitignore` covers `research/` +
  `tmp/` from the first commit plus build artifacts (DECISIONS.md D3,
  D8).
- No Apple binary linkage: no Makefile target builds `oracle_probe`
  here; the oracle builds only in the battery lane and the port must
  build with Apple libraries absent (DECISIONS.md D13).

### OPEN (undecided at release-prep time)

- License choice (DECISIONS.md D10/O1) — no `LICENSE`/`NOTICE` text and
  no file headers until the coordinator names the license.
- Version number and versioning scheme for this release.
- API shape confirmation: simplified (level on encode only) versus
  Apple-exact 6-arg mirror (O3).
- Level representation: bare int versus header enum (O4).
- Bench scope: black-box via `port_cli` versus in-process timing (O5);
  all `docs/PERF.md` tables empty until settled.
- Lane fill model: fork-per-lane versus fill-in-place (O6).
- Verdict-threshold confirmation for `smoke`/`full` wiring;
  zero-tolerance default assumed (O7).
- Final spec path plus freeze hash lanes may read (O2).
- Repository host, name, and publication location: no directive found.
