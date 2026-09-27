# BUILD.md — lzmesh-port build + test

Stock toolchain only. macOS-tested (Linux build unrun,
expected-trivial). C11, no dependencies, no Apple libraries linked
(oracle adapter source ships pinned at
`tests/battery/oracle_probe.c`; no Makefile target builds it here).

## Build

```
make            # liblzmesh.a + port_cli (needs lane sources in src/)
```

Flags: `-O2 -std=c11 -Wall -Wextra`, zero warnings required.
Single Makefile + direct `cc` (no CMake; see DECISIONS.md D2).

## Test gates (in order)

```
make selftest   # battery framework sanity — no codec needed
make unit       # fast unit tests (roundtrip, e00-vectors, comp XFAILs)
make smoke      # fast battery tier (needs port_cli + oracle_probe)
make full       # full 77-seed tier, ORACLE_LIB selects Apple build
```

Baselines below are the 2026-09-19 candidate record (evidence not
shipped in this tree; re-run on this tree planned):

- `make unit` green baseline (decoder-complete candidate 2026-09-19):
  35/35 binaries exit 0; 950 PASS + 6 XPASS + 13 XFAIL + 0 FAIL.
  XFAILs are encoder/COMP-emission state (encbatt lane), not breakage.
- `make smoke` green baseline (decoder-complete port): verdict PASS,
  1232 cells, buckets EMPTY — including zero ENC_DIFF on the smoke
  corpus. Any CRASH / TIMEOUT / NONDET / DEC_DIFF / DEC_ZERO_ASYM /
  CROSS_FAIL / ROUNDTRIP_FAIL = breakage, stop.
- 4-selector smoke (`--selectors e00,e01,e05,e09`, same tier): PASS,
  4928 cells, buckets EMPTY — decoder-fix domains at battery level.
- `make full` verdict is FAIL on ENC_DIFF buckets only (encoder OPEN,
  encbatt-owned, ~3,957 diffs); gates publish per
  RELEASE-CHECKLIST.md (two Apple builds) once the encoder lands.
  Zero decoder buckets anywhere is the decoder gate.

## port_cli contract

`port_cli enc|dec <selector-hex>`: stdio byte pipe, exit 0/10,
DECODE_SIZE support, deterministic, no chatter. Full contract:
`tests/battery/README.md`. Selectors: bare 0/1/5/9 accepted everywhere
(see DECISIONS.md MERGE-M1; full-form 0xE00/0xE01/0xE05/0xE09 also
accepted at encode entry pending O3/O4 ruling).

## Layout notes

- `include/lzmesh.h`: public API (encode/decode + scratch sizes +
  framing walker). Declarations + one-line docs only.
- `src/`: port sources + `port_cli.c`.
- `tests/battery/`: vendored framework copy (pinned; see VENDORED.md).
  Resync wholesale + re-pin only.
- `tests/unit/`, `bench/`: lanes own these (bench scope OPEN, O5).
- `spec-queries/`: one-way channel to spec side; blocked on a clause →
  file here, never guess.

## Clean-room rules for contributors

Read the pinned spec Part A only (version in docs/CLEANROOM-LOG.md) +
battery CLI contract + own lane's divergence reports. Never the Apple
binary, third-party sources, other lanes, or spec-side transcripts. Log
every session's inputs in docs/CLEANROOM-LOG.md.
