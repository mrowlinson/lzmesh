# lzmesh — byte-exact clean-room C port of Apple LZMESH

Independent C11 implementation of Apple's LZMESH codec
(`COMPRESSION_LZMESH`), written from behavior specification only and
proven by encode byte-identity against Apple's shipping codec: port
encode == Apple encode on 15792 gated cells at the current tip
(land6 `6b7ff34dd`, 2026-10-08), and the port self-roundtrips its own
bytes in unit tests. Identical bytes decode identically under any
correct decoder, so encode byte-identity is the operative proof.

Why: byte-exact Apple LZMESH in portable C11 with zero dependencies
— no Apple libraries linked, so it builds anywhere with a stock `cc`.
Deterministic bytes (same input + level yields identical output on
every run), buffer API, no hidden state. Embed it, audit it, fuzz it.

## Quickstart

```
make            # liblzmesh.a + port_cli
printf 'hello lzmesh' | ./port_cli enc e09 > hello.lz
./port_cli dec e09 < hello.lz   # prints: hello lzmesh
make unit       # fast C unit tests over the public API
```

`port_cli enc|dec <level-hex>` is a stdio byte pipe (`e09` == `0xE09`;
levels `e00/e01/e05/e09`). The decoder takes no selector — one decoder
handles all levels (the `dec` level argument is accepted and ignored).

## API sketch

```c
#include "lzmesh.h"

size_t lzmesh_encode(uint8_t *dst, size_t dst_cap,
                     const uint8_t *src, size_t src_len,
                     void *scratch, int level);   /* level 0xE00/0xE01/0xE05/0xE09 */
size_t lzmesh_decode(uint8_t *dst, size_t dst_cap,
                     const uint8_t *src, size_t src_len,
                     void *scratch);
size_t lzmesh_encode_scratch_size(int level);
size_t lzmesh_decode_scratch_size(void);
size_t lzmesh_decoded_size(const uint8_t *src, size_t src_len);
```

Buffer API only — no streaming API exists (honest: the format has no
streaming path, and neither does this port). `scratch` may be `NULL`
(the library allocates) or point to the matching scratch-size bytes.
Encode returns bytes written, or 0 on failure. Decode returns bytes
written, or 0 on truncated/invalid input; an undersized destination
truncates with no error signal, so size it with `lzmesh_decoded_size`
first. The format carries no checksum: decoding never validates, so
applications needing integrity layer their own digest above the codec.
Full contract: `docs/API.md`, `include/lzmesh.h`.

## Current numbers

Byte-exactness at land6 `6b7ff34dd` (2026-10-08):

- Gated: 15792/15792 cells, 0 `ENC_DIFF` vs the Apple oracle — full
  12784 (seeds 0–16) + holdout 3008 (seeds 17–20, disjoint), selectors
  `e00/e01/e05/e09`, tier full.
- Parent-verify batteries at land6, all green: smoke 4928/0, full
  12784/0, holdout 3008/0, units (stock suites + 20 kraft-edge
  boundary tests) PASS, veh-ident 24/24, fuzz 11349/0/0, PGO-binary
  byte-identical to the normal build. NEW=0 everywhere.
- NOT covered — fresh slice seeds 21–24 (3008 cells), 5 pre-existing
  diffs, byte-identical before and after every land (merge-inert):
  s21-n262144-alphabet `e05` + `e09` (234023 vs 234032),
  s22-n255-alphabet `e01` (same length, bytes differ),
  s24-n49-alphabet `e09`, s24-n65536-alphabet `e09`.
- Source pin: `src/lzmesh.c` md5 `96854130` (unify-1.0 single-source
  merge of land2 enc `b11690c5` + land6 dec `b775ba42`; pre/post
  CLI md5 IDENT on hello + 256KB fixture, enc+dec).
- Method: black-box byte-identity battery, port CLI vs Apple oracle
  CLI (`tests/battery/battery.py --oracle … --port ./port_cli …`).

Performance (decode gaps, encode status): see
[docs/PERF-RESULTS.md](docs/PERF-RESULTS.md).

## Layout (public tree)

```
README.md            this file
CLEANROOM.md         wall method, one page
DERIVATION-CLEANROOM.md  RE-track derivation log (hashes, not bytes)
CHANGELOG.md         release history + moved gate history (R4–R28, lands 1–6)
WAVES.md             per-wave performance history (newest first)
BUILD.md             build + test gates, pinned to this tree
LICENSE              license text (0BSD; injected at export)
LICENSE-CHOICE.md    decided license + pre-decision memo history
RELEASE-CHECKLIST.md ordered gates before publish
.gitignore           ignores (injected at export)
Makefile             all / selftest / unit / smoke / full / bench / pgo / clean
DECISIONS.md         every skeleton choice + its source directive
include/lzmesh.h     public API: encode, decode, scratch sizes,
                     decoded-size framing walker
src/                 lzmesh.c (single-source codec) + port_cli.c
                     (battery CLI: enc|dec <level-hex>, stdio byte pipe)
tests/battery/       vendored divergence-battery framework (pinned)
tests/unit/          fast unit tests + vector fixtures
bench/               benchmarks + pinned corpus + generator
release/             deterministic public-tree exporter (export.sh)
docs/                BUILD / TESTING / API / PERF / PERF-RESULTS /
                     RELEASING / SPEC-S9CR + CLEANROOM-LOG +
                     DERIVATION + README
```

What does NOT ship: research notes, agent scratch, battery result
dirs, build artifacts, the oracle adapter binary, or any Apple library
— the tree builds with Apple libraries absent.

## Cleanroom note

No decompiler output, no Apple source, and no third-party LZMESH code
was read or used: implementers worked from behavior-spec prose only,
every behavior claim was measured by black-box oracle batteries, and
the tree links zero Apple code under neutral `lzmesh_` identifiers.
See [CLEANROOM.md](CLEANROOM.md) for the one-page method,
[DERIVATION-CLEANROOM.md](DERIVATION-CLEANROOM.md) for the RE-track
log, and `docs/CLEANROOM-LOG.md` + `docs/DERIVATION.md` for per-session
and per-file provenance.

## License

License: 0BSD — see LICENSE.
