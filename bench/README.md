# bench/ — throughput harness + pinned corpus

Settles DECISIONS.md O5 as **in-process timing** against `liblzmesh.a`
(`bench.c` links the library and times `lzmesh_encode`/`lzmesh_decode`
directly with `CLOCK_MONOTONIC`). Rationale: black-box timing via
`port_cli` folds process-spawn + stdio noise into every sample, which
dominates on sub-megabyte inputs; in-process samples isolate codec cost.
(`time port_cli` stays available for end-to-end sanity; it is not the
recorded baseline.)

## Files

- `bench.c` — harness. Usage: `./bench [-n reps] [-l 0159] file...`. One TSV row
  per rep sample on stdout: `file level op in_bytes out_bytes ns`.
  Verifies roundtrip bytes + encode determinism every rep; exit nonzero
  on any failure. Levels are 0/1/5/9 (full-form `0xE00/0xE01/0xE05/0xE09`
  at the API boundary, per `lzmesh.h`).
- `mkcorpus.py` — deterministic corpus generator (`python3 mkcorpus.py`
  regenerates; `--check` verifies byte-identity). Fixed seeds, no
  platform dependence.
- `corpus/` — pinned corpus, committed (458,752 bytes total):
  - `text-256k.bin` (262,144 B) — word-list pseudo-English, seed 11.
  - `mixed-128k.bin` (131,072 B) — text runs + byte runs + structured
    binary + incompressible tails, seed 22.
  - `zeros-64k.bin` (65,536 B) — all zeros, seed 33.
  - sha256 pins live in `../docs/PERF.md` (provenance row).

## Targets (from `port/`)

- `make bench` — build lib + harness, run 7 reps/file/level/op.
- `make bench BENCH_REPS=1` — quick smoke (still verifies roundtrips).
- `make bench-corpus` — regenerate + `--check` (fails on drift).

Throughput convention: encode MiB/s over input bytes, decode MiB/s over
decoded (output) bytes. Aggregation (median/p10-p90 over samples) is
done by the run driver, not the harness; see `../docs/PERF.md`.
