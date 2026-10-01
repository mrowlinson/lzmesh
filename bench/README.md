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
- `oracle-bench.py` — Apple-oracle timing harness. Same CLI/TSV
  schema as `bench.c` (`ORACLE_PROBE=<bin> ./oracle-bench.py [-n
  reps] [-l 0159] file...`), one `oracle_probe enc|dec` spawn per
  sample. Samples include process-spawn + stdio overhead, so Apple
  in-process ≥ quoted; see the header comment for the asymmetry +
  quoting direction before comparing against `bench.c` numbers.
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
- `make bench-gated` — load-gated baseline via `run_gated.sh`
  (refuse-and-log at load1 ≥ 2×ncpu, exit 3; P-core pin; `#`
  provenance headers). `make bench-ab BASE= NEW=` for interleaved
  A/B compares, `make bench-gated-selftest` for the hermetic
  self-tests. Full policy: `GATED-PROTOCOL.md`; comparison tool:
  `cmp.py` (median + bands + SEPARATED/OVERLAP verdicts).

Throughput convention: encode MiB/s over input bytes, decode MiB/s over
decoded (output) bytes. Aggregation (median/p10-p90 over samples) is
done by the run driver, not the harness; see `../docs/PERF.md`.

## Gap matrix (lane matrix-harness)

Full 24-cell port-vs-Apple comparison (3 corpus x 0/1/5/9 x enc/dec)
with BOTH sides in-process — the `oracle-bench.py` pipe is not used.

- `abench.c` — Apple in-process mirror of `bench.c`: same CLI/TSV/exit
  codes, `compression_encode/decode_buffer` via dlopen (`ORACLE_LIB`
  selects the build, default system lib). Build: `make matrix-bins`.
- `encdump.c` — in-process port-vs-Apple byte comparator + both-way
  cross-decode; one TSV row per (file, level), `IDENT`/`DIV@off`/`X-OK`.
- `matrix_gated.sh` — interleaved driver: per (file, level) cell, port
  and Apple run back-to-back through `run_gated.sh` (per-cell load gate
  + P-core pin record), side order flipped by run parity. Stitches
  `port/runN.tsv` + `apple/runN.tsv` with `# matrix v2` headers
  (box/load/pin/build-sha) that `cmp.py` reads unchanged, plus
  `bytes/runN.tsv` and a `raw/` audit trail. `make matrix-gated`
  (n = `MATRIX_RUNS` x `MATRIX_REPS` = 70).
- `air/` — gate-ready Air job: `air_matrix.sh` (phase B submits
  untouched after `easy-ssh push`), `oraclegate.py` +
  `oracle-air.pinned` (24-vector pinned Air oracle, macOS 27; the gate
  halts on drift — re-pin deliberately, never silently).

Zeros/dec-floor hazard: zeros-64k and L0-dec cells sit at the timer
floor (quantized bands, e.g. flat 31250.00) — record, never quote.
Verdicts that matter are the slow cells (text/mixed enc L5/L9).
