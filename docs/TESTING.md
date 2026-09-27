# TESTING.md — tiers, oracle setup, verdict, fuzzers

Gates run in order: `selftest` → unit tests → `smoke` → `full`.
Each gate must pass before the next counts.

## Battery tiers

| Tier | Command | Corpus | Needs |
|------|---------|--------|-------|
| `selftest` | `make selftest` | framework sanity via loopback adapters | python3 only |
| unit | (lanes add `make unit`) | fast content checks; suggested first: container block-walk | lane sources |
| `smoke` | `make smoke` | fast gate: 9 sizes × 2 patterns | `port_cli` + `oracle_probe` |
| `full` | `make full` | ~90 sizes (0–64 dense, 2^k±1, large to 256 KiB) × rotating patterns (run/period/alphabet/random/textlike/sparse), 77 seeds, selectors `e00,e01,e05,e09` | `port_cli` + `oracle_probe` + `ORACLE_LIB` |

Corpus is deterministic from seed: same seed → same bytes on any
machine. Per `--out` dir outputs: `findings.jsonl` (one row per
divergence), `summary.json` (cell count, bucket histogram, seconds,
verdict), `vectors/` (failing input bytes only — Apple/port outputs are
reproducible by re-run and are not stored).

Battery framework is a vendored, pinned copy (`tests/battery/` +
`VENDORED.md` pin row). Never edit it in place; resync is wholesale
copy + re-pin, and any resync invalidates prior campaigns — rerun the
whole tier. Battery operator rules: black-box only (execute the CLIs,
compare bytes, never read lane source); delete `tmp-selftest-*` after
selftest.

## Oracle setup

- The oracle is the macOS 27 Apple build. `oracle_probe` (built
  explicitly from the pinned in-tree `tests/battery/oracle_probe.c`)
  loads the Apple build
  selected by the `ORACLE_LIB` environment variable (default
  `/usr/lib/libcompression.dylib`).
- This repo never links an Apple binary and has no target that builds
  `oracle_probe` (DECISIONS.md D13). The port side meets the same CLI
  contract: `<cli> enc|dec <selector-hex>` over stdio, exit 0 on
  success, exit 10 on clean codec refusal, `DECODE_SIZE` env sizing the
  decode destination, deterministic bytes, no chatter — see
  `tests/battery/README.md`.
- Verdict needs two Apple builds: run each tier once per build (same
  command, different `ORACLE_LIB`, separate `--out` dirs).
- Multi-build decoder proof is planned, not yet evidenced in this
  tree (a 2026-09-19 candidacy run covered FOUR distinct Apple builds
  by loaded UUID — host macOS 27.0, iOS-sim 26.5, iOS-sim 27.0,
  tvOS-sim 27.0 — but that evidence is not shipped here). Planned
  method: sim builds run directly on the host via
  `DYLD_ROOT_PATH=<RuntimeRoot>` (no simulator daemon needed); a macOS
  process cannot dlopen a sim libcompression, so the sim oracles run
  as sim-platform probe processes behind a `--oracle` argv wrapper
  (the battery Side takes argv — in-contract).
- Caller-buffer quirk (recorded, gates unaffected): Apple decoder
  refusal is destination-size sensitive (e01 blob `000108000075ff`:
  exact cap decodes, +1024 cap refuses, deterministic). All gates use
  cap olen+1024 on ALL sides per the `DECODE_SIZE` contract.

## Decoder gap gates (planned — no evidence in this tree yet)

Beyond the battery tiers, the decoder is slated for dedicated
mutant-scan gates (lane tooling; evidence locations will ship with the
release record per `docs/RELEASING.md` §4). Planned gates, with the
2026-09-19 candidacy figures as the bar to re-prove:

- Full neighborhood vs oracle: 116,612 mutants, gaps=0,
  over_accepts=0.
- Suspect-seed sweep: divs=0/4608; valid corpus 384/384 identical.
- 5-way cross-build scan (port + all 4 Apple builds) × 116,612:
  unanimous agree5; all 8 agree/diverge pairs 0 — Apple-Apple decoder
  variance zero everywhere probed (status + bytes incl refusals).

## Verdict criteria

Zero-tolerance default (threshold confirmation OPEN, DECISIONS.md O7).
Ship is blocked by any of:

- any divergence cell (`findings.jsonl` non-empty),
- any crash (non-0/10 exit, signal, stderr traceback on either side),
- any timeout,
- any skipped cell,
- single-build-only evidence (a tier run against only one Apple build).

Full verdict procedure: verdict record not shipped in this tree;
per-tier verdicts are the `summary.json` files in each `--out` dir.

## Fuzzers (planned — none wired yet)

Intended harnesses, to be built on the `port_cli` contract:

- Roundtrip fuzz: random inputs × levels → port encode → both sides
  decode → byte-compare; plus Apple encode → both sides decode.
- Decode fuzz: mutated/truncated frames → decode must return 0 or a
  correct prefix, never crash or hang; exit 0/10 discipline holds.
- Determinism fuzz: same input + selector twice → identical bytes.

Placeholders: harness sources, seed corpus location, time/iteration
budgets, and where fuzz findings merge into the verdict — all TBD once
lanes land code. Fuzzing never replaces the `smoke`/`full` gates.

## OPEN

- Verdict-threshold confirmation (O7).
- Final spec path + freeze hash lanes may read (O2) — nothing starts
  without it; log the sha256 in `docs/CLEANROOM-LOG.md`.
- `make unit` hook and unit sources (D12).
