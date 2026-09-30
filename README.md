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
tree c312cf90. Port = in-process (`bench/bench.c`); Apple =
stdio-pipe floor (`bench/oracle-bench.py` over `oracle_probe`):
every Apple sample includes fork+exec+dlopen+pipes, so Apple
in-process ≥ quoted, always.

Encode, port / Apple:

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 123.27 / 60.00 | 49.50 / 51.87 | 39.40 / 49.14 | 19.05 / 41.40 |
| mixed-128k | 77.78 / 38.99 | 62.47 / 36.73 | 50.33 / 35.86 | 22.70 / 28.72 |
| zeros-64k | 1524.39 / 24.33 | 2976.19 / 23.91 | 2976.19 / 23.90 | 3050.60 / 20.06 |

Decode, port / Apple (each side decodes its own bytes):

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 564.33 / 76.31 | 250.25 / 83.44 | 254.84 / 84.35 | 256.81 / 84.37 |
| mixed-128k | 606.80 / 44.39 | 572.09 / 50.37 | 602.41 / 50.83 | 573.39 / 50.84 |
| zeros-64k | 62500.00 / 28.21 | 62500.00 / 28.29 | 62500.00 / 28.24 | 62500.00 / 28.25 |

Reading the gap (which direction each claim favors):

- Apple leads, conservative claim — pipe overhead can only
  understate Apple, so the true gap is larger: enc L1/L5/L9 on
  text (+4.8/+24.7/+117.3%) and enc L9 on mixed
  (+26.5%). 4 cells, all SEPARATED.
- No claim where the port number is higher (all dec cells, enc
  L0, mixed enc L1/L5, all zeros): the port is timed in-process while Apple pays
  ~2–4 ms spawn+pipe per sample (visible as the ~20–85 MiB/s
  floor on sub-0.1 ms-codec cells), so these gaps favor the port
  by construction. Apple in-process could be faster on any of
  them. Zeros cells carry no Apple codec information (pure
  overhead floor); port zeros-dec 62500.00 is the timer-quantum
  floor, same artifact class. Mixed enc L5 flipped to
  port-higher this wave (S1 fusion, +40.4% SEPARATED) and joins
  the NO-CLAIM bucket per discipline (was Apple-led in P13).

Byte note: port enc == Apple enc on 9/12 cells; mixed-128k
L1/L5/L9 differ (pre-existing — identical sizes+offsets to
P12/P13: e01 34181@1152; e05 32799v32796@2; e09 32708v32707@8;
every output self-roundtrips and cross-decodes OK). Sizes within
3 B, so dec-timing inputs are size-matched.

PGO is opt-in and build-only (`make pgo`; the default build tree
is untouched, no source change). Re-measured on this tree (Air,
interleaved --ab, n=70, quoted cells SEPARATED), text-256k:
enc L9 +15.0%, dec L1 +47.6% / L5 +46.9% / L9 +46.7%.
Caveats: enc L0 −1.6% (text) and enc L5 −1.4% (mixed)
SEPARATED slower. Byte-identity holds
under PGO: PGO-binary enc == normal enc 12/12.

Sources: every cell traces to
`tmp/verify-p14-air/p14-merge/cmp-port-vs-apple.txt` (bands + n +
verdicts), reproducible byte-identically from `gated-port/` +
`gated-apple/` run TSVs; PGO from `cmp-pgo.txt` + `gated-pgoab/`.
Gate: load1 < 16, 0 refusals on all runs, pin `taskpolicy-t0l0`.
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
