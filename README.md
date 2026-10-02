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
Apple clang 21 `-O2 -std=c11`, R7 fold on the R6-ship tree
(`port/src/lzmesh_enc.c` md5 ac44f320,
`port/src/lzmesh_dec.c` md5 5fa2dea5; port/src byte-identical to
the R6 SHIP c05555f30; tables + PGO below read THIS tree). Both
sides in-process: port `bench/bench.c` vs Apple `bench/abench`
(same harness, no fork+exec+pipe floor — R4's piped-Apple
methodology is superseded; its tables are history below).

Encode, port / Apple:

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 365.44 / 874.13 | 94.11 / 324.25 | 86.13 / 245.94 | 61.27 / 226.55 |
| mixed-128k | 258.27 / 862.07 | 115.96 / 459.56 | 107.85 / 350.14 | 76.31 / 293.43 |
| zeros-64k | 1524.39 / 1953.12 | 2976.19 / 5208.33 | 2976.19 / 5208.33 | 2976.19 / 892.86 |

Decode, port / Apple (each side decodes its own bytes):

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 1278.78 / 2059.32 | 417.01 / 1404.49 | 428.08 / 1524.39 | 430.29 / 1533.74 |
| mixed-128k | 1237.62 / 2314.81 | 822.37 / 2450.98 | 925.93 / 2659.57 | 892.86 / 2403.85 |
| zeros-64k | 62500.00 / 31250.00 | 31250.00 / 5681.82 | 31250.00 / 5681.82 | 31250.00 / 5681.82 |

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

Reading the gap (both sides in-process, no pipe floor):

- Apple ahead on all 16 text/mixed codec cells (all SEPARATED,
  gaps −46.5% to −74.8%): real port deficits, the optimization
  frontier. Text L0 cells carry wide bands (medians 365.44 /
  1278.78 vs R5 within 1.2pp — sprawl, not signal).
- Zeros cells are timer-floor artifacts (NO-CLAIM): enc medians
  flip between 2976/3125 quanta run to run; zeros L0 dec reads
  n=68/70 (ns==0 samples dropped by the cmp filter) OVERLAP;
  L1/L5/L9 dec +450.0% SEPARATED-port is pure quantum
  artifact, not a speed claim.

Byte note: port enc == Apple enc on 9/12 cells; mixed-128k
L1/L5/L9 differ (e01 34181@1152, same length; e05 32799v32796@2;
e09 32708v32707@8; every output self-roundtrips and cross-decodes
OK, 12/12 both directions; not ship-introduced — R4 union
FULL 57904 NEW=0 base-vs-union direct; R6 ship FULL/HOLD/
FRESH 18800 DIV=0 base-vs-ship direct, land-rerun; R7 matrix
re-verifies the same 9/12 IDENT + 3 mixed DIVs, DIV lines
byte-identical x10 and == R5). Sizes within 3 B,
so dec-timing inputs are size-matched.

PGO is opt-in and build-only (`make pgo`; the default build tree
is untouched, no source change). Re-measured on this tree (Air,
interleaved --ab, n=70, 9SEP/15OVER),
text-256k: enc L1/L5/L9 +5.6/+3.0/+6.5%, dec L1
+7.9% (mixed-enc L1/L5/L9
+10.2/+3.0/+5.7%, see `tmp/matrix-r7/pgo/cmp-pgo.txt`; quoted cells
SEPARATED; text-dec L5/L9 +2.6/+3.6% and mixed-dec
L1/L5/L9 +2.7/+2.3/+0.4% OVERLAP are NO-CLAIM).
Caveats: 2 cells SEPARATED slower — text enc L0 −16.0%,
mixed enc L0 −12.5% (same caveat as R4: −16.2/−13.1); text/mixed
dec L0 +0.0/+0.0% OVERLAP
(NO-CLAIM); zeros all OVERLAP (timer floor).
Byte-identity holds
under PGO: PGO-binary enc == normal enc 12/12.

Sources: every cell traces to
`tmp/matrix-r7/ev/cmp-matrix.txt` (bands + n +
verdicts), reproducible byte-identically from `ev/matrix/`
run TSVs; PGO from `tmp/matrix-r7/pgo/cmp-pgo.txt` + `gated-pgoab/`.
Gate: load1 < 16, 0 refusals on all runs (matrix 1.06–1.44,
pgoab 1.41–1.54), pin `taskpolicy-t0l0`.
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
