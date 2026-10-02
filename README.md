# lzmesh — bit-exact clean-room C port of Apple LZMESH

Independent C11 implementation of Apple's LZMESH codec
(`COMPRESSION_LZMESH`), written from specification prose only and proven
by encode byte-identity against Apple's shipping codec: port encode ==
Apple encode on 15792 gated cells, and the port self-roundtrips its own
bytes in unit tests. (Identical bytes decode identically under any
correct decoder, so encode byte-identity is the operative proof; no
cross-decoder battery has been run.)

Stock `cc`, macOS-tested (Linux build unrun, expected-trivial).
Zero dependencies, zero Apple libraries
linked. Buffer API only — no streaming API provided.

## Byte-exactness claim

Gated: 15792/15792 cells 100%, 0 ENC_DIFF vs Apple oracle.

As of 2026-09-28, source lanes/lane-4 @ e582954c (codec-identical
to the gated P6 merge 88b5a27b — e582954c touches scratch only) plus
comment-only SPDX headers. `make unit`: 44/44 suites green, fail=0.
Vector fixtures: 25 .bin + manifest (26 files, unchanged).

- Full corpus: 12784/12784. Seeds 0–16, selectors
  e00/e01/e05/e09, tier full.
- Holdout: 3008/3008. Seeds 17–20 (disjoint from full).

Method: black-box byte-identity battery, port vs Apple oracle:

```
tests/battery/battery.py --oracle ./oracle_probe --port ./port_cli \
  --out <dir> --seeds <n> --seed-offset <off> \
  --selectors e00,e01,e05,e09 --tier full
```

Runs: full `--seeds 17 --seed-offset 0`;
holdout `--seeds 4 --seed-offset 17`.

NOT covered — fresh slice seeds 21–24 (3008 cells, disjoint from the
gated range), 5 pre-existing diffs, next frontier:

- s21-n262144-alphabet e05 + e09 (lengths differ: 234023 vs 234032)
- s22-n255-alphabet e01 (same length, bytes differ)
- s24-n49-alphabet e09
- s24-n65536-alphabet e09

The 5 are byte-identical before and after the gating merge
(pre-existing, merge-inert); 0 new diffs introduced.

## Performance

Throughput in MiB/s, medians over n=70 (10 gated runs × 7 reps,
interleaved port-vs-Apple, same box back-to-back), MacBookAir M1,
Apple clang 21 `-O2 -std=c11`, R9-ship tree
(`port/src/lzmesh_enc.c` md5 41ec58b7,
`port/src/lzmesh_dec.c` md5 5fa2dea5; tables + PGO below read
the R9 matrix on the R8 SHIP — R9 union absolutes refresh next
matrix; R9 deltas in the paragraph below). Both
sides in-process: port `bench/bench.c` vs Apple `bench/abench`
(same harness, no fork+exec+pipe floor — R4's piped-Apple
methodology is superseded; its tables are history below).

Encode, port / Apple:

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 346.42 / 874.13 | 103.52 / 324.68 | 95.71 / 246.06 | 77.08 / 226.65 |
| mixed-128k | 257.73 / 862.07 | 135.28 / 461.25 | 117.48 / 348.68 | 96.08 / 296.91 |
| zeros-64k | 1524.39 / 2016.13 | 2976.19 / 5208.33 | 2976.19 / 5208.33 | 2976.19 / 919.12 |

Decode, port / Apple (each side decodes its own bytes):

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 1204.09 / 2109.74 | 417.36 / 1408.46 | 428.08 / 1515.15 | 430.29 / 1543.21 |
| mixed-128k | 1237.62 / 2314.81 | 822.37 / 2450.98 | 929.38 / 2659.57 | 892.86 / 2403.85 |
| zeros-64k | 62500.00 / 31250.00 | 62500.00 / 5681.82 | 62500.00 / 5681.82 | 31250.00 / 5681.82 |

R4 ships decode + a sliver of encode: decres lifts text dec
L1/L5/L9 to 417.71/428.82/432.53 (Air-gated +15.3/+16.3/+16.0
pp vs R3 base, all SEPARATED, 0 slower anywhere) and enc5-C3
lifts text L5 enc to 85.79 (+2.8 SEPARATED). Mixed L5 dec reads
932.84 (+11.9 median, OVERLAP — run2 outlier, NO-CLAIM).
enc9text-F2 and enc0-C-ZERO carried (bytes-green, 0 SEP,
prizeless at gate n) — mixed L9 enc was the open frontier
at R4 (port 67.04 vs piped Apple 28.80, NO-CLAIM); R6 ships
it (next paragraph).

R6 ships L9 encode (T9+QBR: peek-r1 legs + flood/span/
direct splits + gate snapshot over the QBR query razor;
enc md5 ac44f320): text L9 enc +10.6% SEPARATED (n=25 gated
A/B base-vs-new, 55.51→61.36) and mixed L9 enc +14.3%
SEPARATED (67.02→76.64), 0 slower across 18 L1/L5/L9 cells.
Gap-share vs R5 ranking: text +2.57pp (−75.6→−73.0), mixed
+3.24pp (−77.4→−74.2).

R7 folds a fresh 24-cell in-process matrix onto this tree
(bench-only, code-identical to the R6 SHIP): tables above now
read R7 absolutes (n=70, MacBookAir, load1 1.06–1.44,
23 SEPARATED / 1 OVERLAP), re-confirming both L9 ships — text
L9 enc 61.27 and mixed L9 enc 76.31, bands disjoint vs the R5
ranking, gaps −73.0/−74.0 — and PGO is re-measured below
(9SEP/15OVER). Worst deficits: mixed L1 enc −74.8%,
mixed L9 enc −74.0%, text L9 enc −73.0% (all SEPARATED Apple).
Full R7 tables: `tmp/matrix-r7/RANKING.md`.

R8 ships the union of l9new (IRA floor + C1 flood-guard hoist +
C2 store load-fusion + C3 u27 tail-gate + R riders) and
mix1neutral (E2 stock-order redispatch + D1; enc md5 155d258a):
all 6 text/mixed enc cells SEPARATED (n=25 gated A/B base-vs-new,
24/24 cells incl L0, 0 slower) — text L1 +9.7% (93.91→103.01),
L5 +11.1% (86.09→95.68), L9 +25.9% (61.15→76.99); mixed L1
+15.8% (116.06→134.41), L5 +8.8% (107.67→117.15), L9 +25.2%
(76.22→95.42). Gap-share vs R7 ranking: text L9 +6.94pp
(−73.0→−66.0), mixed L9 +6.51pp (−74.0→−67.5), mixed L1 +4.01pp
(−74.8→−70.8), text L5 +3.88pp, text L1 +2.74pp, mixed L5 +2.66pp.
Mixed-L1 stacks superadditively (+15.8 vs +7.4/+7.1 solo);
per-side prizes re-verified first-hand (L9s +25.2/+25.7 x2 runs,
mixed-L1 +7.4/+7.8 incl mirror, L5s OV-or-better x3). S23/S24
closed. Evidence: `tmp/r8land/` (union) + `tmp/r8l9new/` +
`tmp/r8mix1neutral/`.

R9 folds a fresh 24-cell in-process matrix onto the R8 SHIP
(bench-only, code-identical): tables above now read R9 absolutes
(n=70, MacBookAir, load1 1.15–1.42, 23 SEPARATED / 1 OVERLAP),
re-confirming all 6 R8 ships — bands disjoint vs the R7 ranking
(mixed L1 enc 135.28, text L1 enc 103.52, mixed L5 enc 117.48,
text L5 enc 95.71, mixed L9 enc 96.08, text L9 enc 77.08; gaps
−70.7/−68.1/−66.3/−61.1/−67.6/−66.0) — and PGO is re-measured
below (9SEP/15OVER, same 9 cells). Worst deficits now: text L9
dec −72.1%, text L5 dec −71.7%, mixed L1 enc −70.7% (all
SEPARATED Apple). Full R9 tables: `tmp/matrix-r9/RANKING.md`.

R9 ships the union of mix1 (F6-PRE fresh6 stack prefilter;
+23/−0) and text1 (E-stack: nowin-inline + L1-extend precheck +
u35 put slow-split/inline; +109/−30; enc md5 41ec58b7): mixed L1
enc +17.4% SEPARATED (n=25 gated A/B base-vs-union, 135.28→
158.83) and text L1 enc +17.8% OVERLAP (102.88→121.24, run1-cold
both sides; narrowed text1 mechanism is +8.0% SEPARATED clean on
ab2, 103.18→111.46), 0 slower across 18 L1/L5/L9 cells.
Gap-share vs R9 ranking: mixed L1 +5.1pp (−70.7→−65.6), text L1
+5.4pp (−68.1→−62.7). L1s stack independently again (time-add
+17.2 predicted vs +17.4 observed, res 0.2pp). L5-neutrality
holds (L5s OV-or-better, mixed-L5 SEP-faster x2 narrowed). S25
closed. Evidence: `tmp/r9land/` (union) + `tmp/r9mix1/` +
`tmp/r9text1/` + `tmp/matrix-r9/`.

Reading the gap (both sides in-process, no pipe floor):

- Apple ahead on all 16 text/mixed codec cells (all SEPARATED,
  gaps −42.9% to −72.1%): real port deficits, the optimization
  frontier. Text L0 cells carry wide bands (medians 346.42 /
  1204.09 — sprawl, not signal; text L0 dec −5.0pp vs R7 is
  wide-band overlap both runs, medians inside each other's bands).
- Zeros cells are timer-floor artifacts (NO-CLAIM): enc medians
  flip between 2976/3125 quanta run to run; zeros L0 dec reads
  n=67/70 (ns==0 samples dropped by the cmp filter) OVERLAP;
  L1/L5/L9 dec +1000.0/+1000.0/+450.0% SEPARATED-port is pure
  quantum artifact, not a speed claim (L1/L5 = median flip
  31250→62500 inside the same band).

Byte note: port enc == Apple enc on 9/12 cells; mixed-128k
L1/L5/L9 differ (e01 34181@1152, same length; e05 32799v32796@2;
e09 32708v32707@8; every output self-roundtrips and cross-decodes
OK, 12/12 both directions; not ship-introduced — R4 union
FULL 57904 NEW=0 base-vs-union direct; R6 ship FULL/HOLD/
FRESH 18800 DIV=0 base-vs-ship direct, land-rerun; R7 matrix
re-verifies the same 9/12 IDENT + 3 mixed DIVs, DIV lines
byte-identical x10 and == R5; R8 union FULL/HOLD/FRESH 18800
DIV=0 + NOWIN0 12784/0 base-vs-union direct, land-rerun, pins
12/12 on Air too; R9 matrix re-verifies the same 9/12 IDENT +
3 mixed DIVs (DIV lines byte-identical x10 and == R7) and R9
union FULL/HOLD/FRESH 18800 DIV=0 base-vs-union direct,
land-rerun, pins 18/18 on Air too). Sizes within 3 B,
so dec-timing inputs are size-matched.

PGO is opt-in and build-only (`make pgo`; the default build tree
is untouched, no source change). Re-measured on this tree (Air,
interleaved --ab, n=70, 9SEP/15OVER, same 9 cells as R7),
text-256k: enc L1/L5/L9 +10.4/+7.7/+6.4%, dec L1
+7.5% (mixed-enc L1/L5/L9
+11.0/+11.5/+9.1%, see `tmp/matrix-r9/pgo/cmp-pgo.txt`; quoted cells
SEPARATED; text-dec L5/L9 +3.0/+2.6% and mixed-dec
L1/L5/L9 +0.6/+0.0/−1.0% OVERLAP are NO-CLAIM).
Caveats: 2 cells SEPARATED slower — text enc L0 −16.0%,
mixed enc L0 −12.9% (same caveat as R4: −16.2/−13.1); text/mixed
dec L0 −0.5/−1.0% OVERLAP
(NO-CLAIM); zeros all OVERLAP (timer floor).
Byte-identity holds
under PGO: PGO-binary enc == normal enc 12/12.

Sources: every cell traces to
`tmp/matrix-r9/ev/cmp-matrix.txt` (bands + n +
verdicts), reproducible byte-identically from `ev/matrix/`
run TSVs; PGO from `tmp/matrix-r9/pgo/cmp-pgo.txt` + `gated-pgoab/`.
Gate: load1 < 16, 0 refusals on all runs (matrix 1.15–1.42,
pgoab 1.91–2.15), pin `taskpolicy-t0l0`.
Method: `docs/PERF.md` + `bench/GATED-PROTOCOL.md`; Apple
harness: in-process `bench/abench` (R4 piped-oracle tables
superseded).

## Clean-room methodology

No decompiler output, no Apple source, and no third-party LZMESH code
was read or used at any point. The derivation chain:

- Hint-firewall. Implementers read the final spec Part A only, plus
  the battery CLI contract and their own lane's divergence reports —
  never the Apple binary, never other lanes, never spec-side
  transcripts. A hard read barrier splits what each side may open;
  blocked on a clause, a lane files a one-way spec query instead of
  guessing.
- Black-box oracle batteries. Every behavior claim is measured by
  executing the port CLI and the Apple oracle CLI and comparing bytes
  — operators never read implementation source. The battery framework
  is a vendored, pinned copy; any resync invalidates prior campaigns.
- Zero Apple code. This repo never links an Apple binary: the oracle
  adapter source ships pinned (`tests/battery/oracle_probe.c`, see
  `tests/battery/VENDORED.md`), no default target builds or links it,
  and the library never touches Apple code. No Apple code or Apple
  library bytes are
  committed: the fixtures under `tests/unit/vectors/` are black-box
  functional data (test inputs plus recorded oracle output bytes,
  traced per-file in `vectors/manifest.txt`), and the tree contains
  no Mach-O.
- Neutral identifiers. Public prefix is `lzmesh_`; upstream
  `mesh_*`/`MESH_*` and Apple-private `Msh*`/`msh_*` identifiers are
  forbidden. No spec prose or constants tables are reproduced in the
  repo — per-file provenance lives in `docs/DERIVATION.md`, and every
  session logs its exhaustive inputs (including the sha256 of the
  exact spec version read) in `docs/CLEANROOM-LOG.md`.

## Build

```
make            # liblzmesh.a + port_cli
make selftest   # battery framework sanity — python3 only, no codec
make unit       # fast C unit tests over the public API
make smoke      # fast battery tier (needs port_cli + oracle_probe)
make full       # full battery tier (needs port_cli + ORACLE_LIB)
```

Flags: `-O2 -std=c11 -Wall -Wextra`, zero warnings required.
Details, variables, and platform notes: `docs/BUILD.md`.

## Use

```c
#include "lzmesh.h"

/* level selects the encoding level (0xE00/0xE01/0xE05/0xE09). */
size_t n = lzmesh_encode(dst, dst_cap, src, src_len, NULL, 0xE09);

/* one decoder handles all levels; size the destination first — an
   undersized buffer truncates with no error signal. */
size_t m = lzmesh_decode(out, lzmesh_decoded_size(dst, n), dst, n, NULL);
```

`scratch` may be `NULL` (the library allocates) or point to at least
`lzmesh_encode_scratch_size(level)` / `lzmesh_decode_scratch_size()`
bytes. Encode returns bytes written, or 0 on failure. Deterministic:
same input + level yields identical bytes on every run.

Integrity warning: the format carries no checksum; decoding never
validates. Applications needing integrity layer their own digest above
the codec.

## Layout

```
README.md            this file
BUILD.md             build + test gates, pinned to this tree
CHANGELOG.md         release history (Keep a Changelog)
LICENSE              license text (0BSD; injected at export)
LICENSE-CHOICE.md    decided license + pre-decision memo history
RELEASE-CHECKLIST.md ordered gates before publish
.gitignore           ignores (injected at export)
Makefile             all / selftest / unit / smoke / full / bench / pgo / clean
DECISIONS.md         every skeleton choice + its source directive
include/lzmesh.h     public API: encode, decode, scratch sizes,
                     decoded-size framing walker
src/                 port sources + port_cli.c (battery CLI:
                     enc|dec <selector-hex>, stdio byte pipe)
tests/battery/       vendored divergence-battery framework (pinned)
tests/unit/          fast unit tests + vector fixtures
bench/               benchmarks + pinned corpus + generator
release/             deterministic public-tree exporter (export.sh)
docs/                BUILD / TESTING / API / PERF / RELEASING /
                     SPEC-S9CR + CLEANROOM-LOG + DERIVATION + README
```

What does NOT ship: research notes, agent scratch, battery result
dirs, build artifacts, the oracle adapter binary, or any Apple library
— the tree builds with Apple libraries absent.

## License

License: 0BSD — see LICENSE.
