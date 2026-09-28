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

Throughput in MiB/s, medians over 35 samples (5 runs × 7 reps,
interleaved base/opt, pinned corpus), text-256k input, Apple M1 Max,
Apple clang 21 `-O2 -std=c11`. Method: `docs/PERF.md`.

| op | level | P1 baseline | P6 | cumulative |
|----|-------|-------------|----|------------|
| enc | L0 | 70.92 | 120.31 | +69.6% |
| enc | L1 | 3.77 | 14.16 | +275.6% |
| enc | L5 | 4.90 | 10.97 | +123.9% |
| enc | L9 | 0.29 | 4.25 | +1365.5% |
| dec | L0 | 87.35 | 451.26 | +416.6% |
| dec | L1 | 118.15 | 224.01 | +89.6% |
| dec | L5 | 125.06 | 222.02 | +77.5% |
| dec | L9 | 124.63 | 224.62 | +80.2% |

Sources: P1 column = `docs/PERF.md` baseline 2026-09-28; P6 column
= P6 merge opt medians (n=35/35 every cell). Per-round chain behind
the cumulative (same-host back-to-back uplifts, text-256k): P2 dec
L0 +298.8%, enc L9 +1143.8%; P3 enc L5 +15.2%; P4 enc L1 +24.2% /
L5 +21.7%; P5 enc L9 +28.4%; P6 dec L0 +30.3% / L1 +10.8% /
L5 +5.3% / L9 +7.5%, enc L0 +27.6%. Caveat: the P1 L9-enc median
(0.29) carries a documented host-noise excursion (`docs/PERF.md`
provenance note), so that row's ×14.7 is partly noise-floor
artifact — the per-round chain is the honest unit.

PGO is opt-in and build-only (`make pgo`; the default build tree is
untouched, no source change). Measured text-256k: enc L9 +13.5%,
dec L1 +36.0% / L5 +34.7% / L9 +39.2% (n=35). Byte-identity holds
under PGO: PGO-binary FULL battery 12784/12784 PASS.

Apple gap, stated plainly: no oracle timing has ever been run
(`docs/PERF.md` oracle column reads "not run" — no oracle-timing
harness exists), so no port-vs-Apple speed comparison is claimed.
Every number above is port-vs-port. Matching or beating Apple
throughput is in-progress future work, not a result.

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
