# demo/ — gate protocol exhibits (lane p8-benchharden, 2026-09-28)

Box: Apple M1 Max, 10 cores. What the box allowed was the refusal
path — load1 ~805-815 vs gate threshold 2×10=20 — so there is no
quiet-box PASS here. Honest either way, per the lane brief.

## Files

- `refusal-GATE.log` + `refusal-uptime.txt` — real refusal: exit 3
  (EGATED), `REFUSED load1=806.49 >= 2x10`, pin probe
  `taskpolicy-t0l0`. Command:
  `sh bench/run_gated.sh <out> ./bench/bench bench/corpus/*.bin`
- `forced-ab/` — schema demo ONLY (`BENCH_FORCE=1`, loudly logged as
  OVERRIDE, never a measurement): interleaved --ab, 2 runs × 1 rep,
  L0, zeros-64k, same binary both sides. Shows the `# gated-bench v1`
  header (bin, loadavg triple, ncpu, pin, reps/levels/run).
- `forced-ab-cmp.txt` — `cmp.py` over the forced run: provenance
  header lines (load1 range, pin) + cell verdicts.

## Cautionary exhibit (why the gate exists)

The forced run compares the binary against ITSELF at n=2 under
load-800 noise, and one zeros cell still reads `SEPARATED(opt
slower)` at −33.3% — a phantom "regression" from pure host noise.
Under the protocol this run is void twice over (forced override +
n=2); it is kept to show exactly what quoting ungated numbers buys.
