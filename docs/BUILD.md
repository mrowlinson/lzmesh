# BUILD.md — toolchain, targets, platform notes

## Toolchain

Stock compiler only. No configure step, no third-party dependencies.

- Compiler: `cc` (override with `CC=`), flags `CFLAGS` default
  `-O2 -std=c11 -Wall -Wextra -Iinclude`.
- Archiver: `ar` (override with `AR=`), used as `ar rcs`.
- Python 3: needed only to run the battery (`selftest`/`smoke`/`full`),
  not to build the library or CLI.
- Single `Makefile`, direct `cc` invocation. There is no CMake or
  second build system (DECISIONS.md D2).

Library sources live in `src/` (added by implementer lanes); the public
API is `include/lzmesh.h`. Until lanes land, `make` fails loudly with
`no lane sources yet`.

## Make targets

| Target     | Builds / runs | Needs |
|------------|---------------|-------|
| `all`      | `liblzmesh.a` + `port_cli` (default) | lane sources in `src/` |
| `port_cli` | battery CLI from `src/*.c` incl. `src/port_cli.c` | lane sources |
| `selftest` | battery framework sanity (`battery.py --selftest`) | python3 only — always runnable, no codec |
| `smoke`    | fast battery tier vs `port_cli` | built `port_cli` (+ `oracle_probe` from pinned in-tree source) |
| `full`     | full 77-seed tier, selectors `e00,e01,e05,e09` | built `port_cli`, `ORACLE_LIB`, `oracle_probe` |
| `pgo`      | PGO rebuild: lib + `port_cli` + `bench` under `build-pgo/` | pinned corpus + `llvm-profdata` (via `xcrun`) |
| `pgo-unit` | unit tests built against the PGO lib, then run | `pgo` outputs |
| `clean`    | removes objects, lib, CLI, `results/`, `tmp-selftest-*`, `build-pgo/` | — |

Variables: `CC`, `CFLAGS`, `AR` (toolchain); `BATTERY` (default
`tests/battery/battery.py`); `ORACLE` (default
`../tmp/portrepo/oracle_probe`, the scratch prebuilt — override with `ORACLE=`);
`PORT` (default `./port_cli`); `ORACLE_LIB` (Apple build under test,
consumed by `oracle_probe` at run time — see `docs/TESTING.md`);
`PGODIR` (default `build-pgo`), `PGO_TRAIN_REPS` (default 3),
`PGO_L0_BOOST_REPS` (default 200),
`PROFDATA` (default: `xcrun --find llvm-profdata`).

There is deliberately no `oracle_probe` target here: build the adapter
explicitly from `tests/battery/oracle_probe.c` when needed, and this
repo must build with Apple libraries absent (DECISIONS.md D13).

`port_cli` CLI contract (`enc|dec <selector-hex>`, stdio byte pipe,
exit 0/10, `DECODE_SIZE`, determinism, no chatter): defined in
`tests/battery/README.md`.

## macOS notes

- Stock Xcode CLT `cc` (clang) is sufficient; no extra packages.
- `make selftest` runs anywhere (loopback adapters, no codec).
- `smoke`/`full` need `oracle_probe` (built from the pinned in-tree
  source) plus the macOS 27 Apple build under test via `ORACLE_LIB`
  (default `/usr/lib/libcompression.dylib`).

## Linux notes

- Stock `cc` (gcc or clang) + `ar` + python3. No Apple libraries exist
  here, so only `all`/`port_cli`/`selftest`/`clean` run locally; the
  oracle-backed tiers (`smoke`/`full`) require a macOS host (build
  `oracle_probe` there from the pinned in-tree source).
- Not yet built here (Docker daemon unavailable at candidacy time);
  expected-trivial: pure C11 + libc, zero Apple API.

## Proven platform builds (decoder-complete candidate 2026-09-19)

Candidate record (evidence not shipped in this tree; re-run on this
tree planned). At candidacy, all slices built rc=0 with zero warnings
and link libc-only (no
forbidden undefined symbols). Built with Xcode 27.0 SDKs; outputs went
to lane scratch, never into this tree.

| Slice | minos stamp | Run status |
|-------|-------------|------------|
| macOS arm64 (host) | — | units 35/35, 950+6+13+0 |
| macOS x86_64 | 13.0 | build-only (no Rosetta to run) |
| iOS device arm64 | honest 7.0 | link-only (no device shell) |
| iOS sim arm64 | 14.0 (ld-enforced bump) | RUNS under iOS 26.5 + 27.0 roots; units 34/35 |
| iOS sim x86_64 | 7.0 | build-only (no Rosetta) |
| tvOS device / sim-arm64 / sim-x86_64 | 9.0 / 14.0-bump / 9.0 | sim-arm64 RUNS under tvOS 27 root; units 34/35 |
| watchOS device arm64 / sim-arm64 | 26.0-bump / 9.0 | build-only (no watch sim runtime) |
| watchOS device arm64_32 | honest 2.0 | build-only (oldest device floor) |

The single unit gap on sim/device slices (34 not 35) is TEST-ONLY:
`test_m19_cli_cap` Part B uses `system()`, unavailable on iOS/tvOS
SDKs. The library is unaffected. iOS notes:
`../tmp/portrepo/ios/NOTES.md` (stayed in tmp scratch).

## OPEN

- `make unit` hook: LANDED (35 tests, `make unit` wired).
- Bench build wiring: pending bench-scope decision (DECISIONS.md O5).
- Linux build: unrun, expected-trivial (see above).
