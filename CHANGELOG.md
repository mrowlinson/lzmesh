# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning scheme: OPEN — no versioning directive found in the allowed
reads (KICKOFF-CLEANROOM.md names no scheme; DECISIONS.md records none).
No version number or date is claimed below until the coordinator decides.

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
