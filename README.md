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
Apple clang 21 `-O2 -std=c11`. Tables read the R27 matrix on the
R26 hold tip (`port/src/lzmesh_enc.c` md5 3fec8aa3,
`port/src/lzmesh_dec.c` md5 0dae2df4; R25–R26 changed no code — all
hunters held). R27 SHIPPED tL5d-d1 (`port/src/lzmesh_dec.c` md5
5a147a81; text+mixed L0 dec rel +20.1/+17.9, gap-share
+31.8/+21.4pp gate-primary); ship numbers land in tables at the R28
re-measure. Both sides in-process: port `bench/bench.c` vs Apple
`bench/abench` (same harness, no fork+exec+pipe floor).

Encode, port / Apple:

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 485.44 / 971.25 | 137.36 / 325.10 | 105.80 / 245.82 | 87.81 / 226.76 |
| mixed-128k | 399.36 / 862.07 | 187.41 / 461.25 | 140.61 / 352.61 | 112.31 / 296.91 |
| zeros-64k | 1524.39 / 1984.63 | 3125.00 / 5208.33 | 2976.19 / 5208.33 | 2976.19 / 919.12 |

Decode, port / Apple (each side decodes its own bytes):

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 1272.27 / 2136.75 | 830.56 / 1412.43 | 827.81 / 1515.15 | 827.81 / 1543.21 |
| mixed-128k | 1262.63 / 2314.81 | 1184.86 / 2450.98 | 1602.56 / 2659.57 | 1462.04 / 2403.85 |
| zeros-64k | 62500.00 / 31250.00 | 31250.00 / 5681.82 | 31250.00 / 5681.82 | 31250.00 / 5681.82 |

Standings (R27 gaps, port vs Apple): the worst six cells are all
encode — mixed L9 −62.2%, text L9 −61.3%, mixed L5 −60.1%,
mixed L1 −59.4%, text L1 −57.7%, text L5 −57.0% (all SEPARATED).
Decode trails worst on mixed L1 −51.7% and text L9 −46.4%.

Ships to date (per-wave detail in [WAVES.md](WAVES.md)):

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
only), R14, R16, R19, R20, R21, R22, R25, R26.

Full per-wave history (R27–R4, newest first): [WAVES.md](WAVES.md).
Future land folds append new waves there; this section keeps only
current standings.

Reading the gap (both sides in-process, no pipe floor):

- Apple ahead on all 16 text/mixed codec cells (all SEPARATED,
  gaps −39.2% to −62.2%): real port deficits, the optimization
  frontier. Text L0 enc carries wide bands (median 485.44,
  band 392.96–488.28, Apple wide too 823.22–1082.25 — sprawl, not
  signal; holds cross-run (482.63→485.44 R26→R27, bands
  overlap)).
- Zeros cells are timer-floor artifacts (NO-CLAIM): enc medians
  flip between 2976/3125 quanta run to run; zeros L0 dec reads
  n=64/70 (ns==0 samples dropped by the cmp filter) OVERLAP;
  L1/L5/L9 dec +450.0% x3 SEPARATED-port is pure
  quantum artifact, not a speed claim (median flip back
  62500→31250 inside the same band).

Byte note: port enc == Apple enc on 9/12 cells; mixed-128k
L1/L5/L9 differ (e01 34181@1152, same length; e05 32799v32796@2;
e09 32708v32707@8; every output self-roundtrips and cross-decodes
OK, 12/12 both directions; not ship-introduced). Every wave
re-verifies the same 9/12 IDENT + 3 mixed DIVs on its ship and
HOLD stacks (FULL/HOLD/FRESH 0 NEW base-vs-ship direct,
lane-gated + land-verified); DIV lines byte-identical every
round, md5 5d68b09c R12–R27. Sizes within 3 B,
so dec-timing inputs are size-matched.

PGO is opt-in and build-only (`make pgo`; the default build tree
is untouched, no source change). Re-measured on this tree (Air,
interleaved --ab, n=70, 6SEP/18OVER, 6 faster, 0 slower, 18th
straight 0-slower round): text-256k enc L1/L5/L9 +10.2/+4.8/+3.8%
SEPARATED; mixed-enc L1/L5/L9 +4.5/+5.4/+3.8% (L5 SEPARATED
flip-back, L1/L9 OVERLAP — mL5e SEP→OV→SEP with run5-recurrence
T3 flag; see `tmp/matrix-r27/pgo/ev/cmp-pgo.txt`); text-dec
L1/L5/L9 +4.5/+9.2/+7.7% (L5/L9 SEPARATED, L1 OVERLAP); mixed-dec
L1/L5/L9 +6.0/+10.0/+7.7% OVERLAP (NO-CLAIM).
Caveats: none SEPARATED-slower on the std rig — L0 PGO headroom
now ~0 on both sides (text L0 enc −1.4% / mixed L0 enc −2.8%
OVERLAP; static diet only, no PGO-stack left on L0); text/mixed
dec L0 −0.5/−1.0% OVERLAP (NO-CLAIM); zeros all OVERLAP (timer
floor); drop-1 secondary tL0e −1.3% OVERLAP — the R26 PGO-inherited
SEP-slower limitation did NOT reproduce (std primary clean both
waves).
Byte-identity holds under PGO: PGO-binary enc == normal enc 12/12.

Sources: every cell traces to
`tmp/matrix-r27/ev/cmp-matrix.txt` (bands + n +
verdicts), reproducible byte-identically from `ev/matrix/`
run TSVs; PGO from `tmp/matrix-r27/pgo/ev/cmp-pgo.txt` + `gated-pgoab/`;
exact-cap from `tmp/matrix-r12/exactcap/cmp-exactcap.txt`.
Gate: load1 < 16, 0 refusals on all runs (matrix 1.01–1.27,
pgoab 1.34–1.37), pin `taskpolicy-t0l0`.
Method: `docs/PERF.md` + `bench/GATED-PROTOCOL.md`; Apple
harness: in-process `bench/abench`.

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
WAVES.md             per-wave performance history (newest first)
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
