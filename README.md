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

Throughput in MiB/s, medians over n=70 (10 gated runs × 7 reps, same
box back-to-back), MacBookAir M1, Apple clang 21 `-O2 -std=c11`,
tree lanes/lane-4 @ d927ce0e (P30 SHIP: W9 walk+qlink
chain-delete; `port/src/lzmesh_enc.c` md5 ea439a15;
this README folded on top, code-identical). Port = in-process
(`bench/bench.c`); Apple = stdio-pipe floor
(`bench/oracle-bench.py` over `oracle_probe`): every Apple sample
includes fork+exec+dlopen+pipes, so Apple in-process ≥ quoted,
always.

Encode, port / Apple:

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 122.82 / 59.19 | 91.79 / 51.52 | 83.03 / 48.86 | 51.93 / 41.23 |
| mixed-128k | 77.59 / 38.79 | 114.10 / 36.57 | 104.21 / 35.37 | 63.28 / 28.53 |
| zeros-64k | 1524.39 / 24.04 | 2976.19 / 23.72 | 2976.19 / 23.68 | 2976.19 / 19.97 |

Decode, port / Apple (each side decodes its own bytes):

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 564.33 / 75.00 | 249.75 / 82.36 | 254.58 / 83.32 | 256.67 / 83.56 |
| mixed-128k | 606.80 / 44.00 | 581.40 / 50.15 | 600.96 / 50.33 | 573.39 / 50.03 |
| zeros-64k | 62500.00 / 27.94 | 62500.00 / 27.90 | 62500.00 / 27.90 | 62500.00 / 27.90 |

Reading the gap (which direction each claim favors):

- No cell shows Apple ahead: the port number is higher in all
  24 cells (all SEPARATED), so by the asymmetry below there is
  NO CLAIM on any cell. Text enc L9 reads port 51.93 vs piped
  Apple 41.23 (+26.0% margin) — this is NOT a port-faster
  claim: Apple in-process could still be faster, and the ~2–4 ms
  spawn+pipe overhead dominates the comparison on slow cells.
- No claim where the port number is higher: the port is timed
  in-process while Apple pays ~2–4 ms spawn+pipe per sample
  (visible as the ~20–84 MiB/s floor on sub-0.1 ms-codec
  cells), so every gap favors the port by construction. Zeros
  cells carry no Apple codec information (pure overhead floor);
  port zeros-dec 62500.00 is the timer-quantum floor, same
  artifact class (zeros-enc port medians flip between
  2976/3050/3125 quanta run to run — same class; one
  zeros-dec-L0 sample read ns==0 and was dropped by the cmp
  filter, n=69 on that cell).

Byte note: port enc == Apple enc on 9/12 cells; mixed-128k
L1/L5/L9 differ (e01 34181@1152, same length; e05 32799v32796@2;
e09 32708v32707@8; every output self-roundtrips and cross-decodes
OK, 12/12 both directions; not ship-introduced — same 3 divs,
same offsets, as the prior public fold). Sizes within 3 B, so
dec-timing inputs are size-matched.

PGO is opt-in and build-only (`make pgo`; the default build tree
is untouched, no source change). Re-measured on this tree (Air,
interleaved --ab, n=70, 15SEP/9OVER),
text-256k: enc L1/L5/L9 +7.8/+4.8/+9.5%, dec L0/L1/L5/L9
+8.0/+47.9/+46.7/+46.5% (mixed-enc L1/L5/L9
+9.6/+5.6/+10.2%, mixed-dec L0/L1/L5/L9
+6.7/+31.9/+35.5/+32.9%, see cmp-pgo.txt; quoted cells
SEPARATED).
Caveats: 1 cell SEPARATED slower — text enc L0 −1.3%;
mixed enc L0 +0.3% OVERLAP (NO-CLAIM); zeros all OVERLAP
(timer floor).
Byte-identity holds
under PGO: PGO-binary enc == normal enc 12/12.

Sources: every cell traces to
`tmp/p30push/air/cmp-port-vs-apple.txt` (bands + n +
verdicts), reproducible byte-identically from `gated-port/` +
`gated-apple/` run TSVs; PGO from `cmp-pgo.txt` + `gated-pgoab/`.
Gate: load1 < 16, 0 refusals on all runs (fold port 2.73–2.73,
apple 2.62–2.73, pgoab 2.37–2.49), pin `taskpolicy-t0l0`.
Method: `docs/PERF.md` + `bench/GATED-PROTOCOL.md`; Apple
harness: `bench/oracle-bench.py`.

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
