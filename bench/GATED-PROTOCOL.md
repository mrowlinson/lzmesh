# Gated bench protocol (lane p8-benchharden)

Why: the P7 re-profile waves (I–O) burned on host noise — a shared box
with load spikes turns `make bench` numbers into fiction. This protocol
makes every comparison load-gated, pinned, interleaved, and
provenance-stamped, with an exact rule for when bands overlap.

No codec changes. All machinery lives in `bench/` + Makefile targets.

## 1. Load gate (refuse-and-log)

- Before EVERY run (each of the N outer runs, each side of A/B), the
  driver reads the 1-minute loadavg and refuses when
  `load1 >= BENCH_MAXLOAD_MULT x ncpu` (default MULT=2).
- Refusal: one line to `<outdir>/GATE.log` + stderr, partial results
  kept, exit code **3 (EGATED)** — distinct from bench `1` (failure)
  and `2` (usage). Exit 3 always means "box too hot, no claim made".
- ncpu: `sysctl -n hw.ncpu`, else `nproc`, else `getconf`. loadavg:
  `/proc/loadavg`, else `uptime` parse, else `BENCH_FAKE_LOAD` triple
  (test injection only).
- `BENCH_FORCE=1` overrides a trip but logs `OVERRIDE (schema smoke,
  not a measurement)`. For CI/schema smoke only — a forced run is
  never quoted as a measurement.

## 2. P-core pinning (best effort)

- When `taskpolicy` exists and `BENCH_NOPIN` is unset, bench runs under
  `taskpolicy -t 0 -l 0` (top throughput + latency tiers), which
  prefers P-cores on Apple Silicon. This is a scheduler hint, not an
  affinity mask: the OS may still migrate threads, hence "best
  effort". Placement is not verifiable without root-only tools
  (`powermetrics`), so the pin state is a record of intent, not proof.
- Gotcha (verified on this box): `taskpolicy -c` only accepts
  `utility`/`background`/`maintenance` — the E-core direction. The
  P-core direction is the tier flags `-t 0 -l 0` (see `man
  taskpolicy`). The driver probes the exact form at startup and
  records `none(taskpolicy-failed)` if the probe fails.
- The effective pin state is recorded per run
  (`taskpolicy-t0l0` / `none(<reason>)`) in the TSV `#` header and
  GATE.log. If a comparison's sides show different pin states, the
  comparison is void — re-run.
- Linux: no pinning wired yet (`none(no-taskpolicy)`); same schema,
  `taskset` left for a later lane if needed.

## 3. Interleaved A/B order

- `run_gated.sh --ab <out> <basebin> <newbin> <corpus...>` runs
  base,new,base,new… (run pairs), so a slow drift or a single hot
  spell hits both sides instead of biasing one.
- Never run all of A then all of B on a shared box. Single-side mode
  (`run_gated.sh <out> <bin> …`) is for baselines only.

## 4. Provenance: TSV `#` comments + cmp header

- Every `run<N>.tsv` starts with a `# gated-bench v1` header:
  UTC stamp, bench binary path, `loadavg <1> <5> <15> ncpu <n>
  pin <state>`, reps/levels/run index. Plain `make bench` TSVs have
  no headers; `cmp.py` prints `load1 n/a (… ungated run?)` for those.
- `cmp.py <basedir> <optdir>` echoes a two-line header per side
  (runs, load1 range, ncpu, pin, bin) before the cell table, so a
  pasted comparison always carries its load context.

## 5. Outlier / n-separation policy (exact rule)

Definitions: a cell = (corpus, level, op). Band = p10–p90 over all
samples in the cell (default n=35 = 5 runs × 7 reps). Medians compared
as usual; the band decides the claim.

- **SEPARATED** iff the bands are disjoint: `opt_p10 > base_p90`
  (opt faster) or `base_p10 > opt_p90` (opt slower). Claim allowed.
- **OVERLAP** otherwise: no claim. Two-step policy, in order:
  1. **More reps, once**: double the outer runs (5→10, n=35→70),
     interleaved, gated. If the new bands separate, claim on the
     n=70 result (quote n=70, keep the n=35 table as appendix).
  2. **Else declare NOISE**: the effect is below box resolution.
     Record `NOISE (n=70, bands overlap)` — never a median-only
     uplift, never a silent re-run until green.
- **Outliers**: a single run whose samples sit >2× off the other
  runs' cell median is host noise until proven otherwise (cf. PERF.md
  baseline: text-256k L9-enc run 3, ~2× slow, 4 of 5 runs agree).
  `cmp.py` flags `suspect-<side>:run<N>.tsv` when dropping exactly
  one run's samples would separate an OVERLAP cell. Suspect runs
  stay in the data (bands keep the excursion visible); exclusion
  needs a logged reason + a re-run, never silent deletion.
- Re-running a gated comparison until the verdict flips is p-hacking.
  One doubling, then NOISE. No exceptions.

## 6. Standard comparison recipe

```
# from port/
make bench-gated BENCH_RUNS=5 BENCH_REPS=7   # baseline n=35 (or reuse one)
make bench-ab BASE=<base-bench> NEW=<new-bench>   # interleaved n=35/side
python3 bench/cmp.py results/gated-ab/bbase results/gated-ab/bnew
```

- Both sides same box, same gate, same pin state, interleaved.
- Quote: medians + bands + n + verdict + load1 range (all in cmp
  output). Paste the header lines with the table.

## 7. Exit codes

| Code | Meaning |
|------|---------|
| 0 | all runs passed the gate, bench exit 0 throughout |
| 1 | a bench run failed (correctness/roundtrip — investigate) |
| 2 | usage error |
| 3 | EGATED: box too hot, refused, partial results kept |
