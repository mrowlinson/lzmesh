# PRE-REG-R27-FLEX7-LAYENC (union layout-perturb gate; BINDING, pre-data)

lane: r27-r27-flex7 (layout-perturb) | vehicle: union (enc-union.c 3860c43c +
-DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9) | base 2b7238fd4.
executor: post-d77 Air run (dispatcher-routed; flex7 takes ZERO bytes).
method: R24 ALIGN flipped enc-side (tL5d laypad2 mirror, prereg3).
binds: ANSWER-r27-flex7-1-FORMAL (rules + bounds) + ANSWER-r27-flex7-2-FORMAL
(single-T0 + union-baseline). NO perturb-arm timing bytes exist at filing.

## arms (r27ab-layenc.sh, n=35 REPS=7, BENCH_RUNS=5 BENCH_REPS=7)

- builds: tip-std + union-std + union-laystd (enc + ALIGN, dec .o shared) +
  unionpgo (own T0) + laypgo (single-T0 + tripwire, Q2a) + tippgo iff
  BASELINE=tip (corroboration) + bench bins all arms.
- T0 recipe corpus-pinned (3 files + -n 3 + -n 200 -l 0); do NOT relax
  (stage1c + traincorp1: corpus moves h3; zeros-only train breaks bl-flat).
- PGO-IDENT 24/24 x2 binding pairs (union std-vs-pgo, lay std-vs-pgo).
  tripwire: cross-pair (lay-std vs lay-cross) IDENT => single-T0 VALID;
  any DIV => discard cross, own-T0 fallback, continue. both profdata md5s
  recorded either way. (tripwire PASS path validated local 24/24.)
- static: T0+PGO -S h3_split via h3count.py (calibrated EXACT 3/3 vs memo):
  bounds ld<193 / cbr<=117 / st<=180 / mov<=520 / bl==33 (Q1b FORMAL).
- timing (0-SLOWER-ONLY regression rule, Q1a FORMAL; NO prize bar):
  BINDING E2a union-std vs union-laystd; E2b unionpgo vs union-laypgo.
  baseline CONTEMPORANEOUS (same batch/box/contiguous; never stale median).
  OPTIONAL corroboration (BASELINE=tip, NON-BINDING): tip-std vs laystd +
  tippgo vs laypgo, 0-slower-only.

## PASS (binding)

- bytes+static: PGO-IDENT 24/24 x2 + h3 bounds all-MET (mechanism rule carries
  the verdict, Q1b).
- timing: 0 SEP-slower on EITHER E2a/E2b => PASS; any SEP-slower => FAIL
  (layout regression). drop-1 directional agree (falsifier; disagree => HOLD).
- corroboration reading (Q2b): lay OV-vs-union but slower-vs-tip =
  no-prize-confirmed, NOT layout harm. corroboration can never FAIL the gate.
- variants: 1 BINDS if decisive (bytes IDENT + in-bounds + 0-slower); 2nd/3rd
  multi-seed ALIGN staged iff ambiguous (Q1c). local expectation: decisive.

## burns + validity

- 1 submit serial (timing slot, dispatcher call only; post-d77 + post-union-
  prescreen). no re-runs; infra-fail once disclosed (tL5d precedent).
- executor command: `BENCH_RUNS=5 BENCH_REPS=7 BASELINE=union T0MODE=single
  sh port/bench/air/r27flex7-layenc/r27ab-layenc.sh` (tip corroboration:
  BASELINE=tip adds the pair; verdict still from the union pair).
