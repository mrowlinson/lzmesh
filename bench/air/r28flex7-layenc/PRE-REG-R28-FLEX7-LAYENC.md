# PRE-REG-R28-FLEX7-LAYENC (H1N1 layout-perturb gate; BINDING per ANSWER-r28-flex7-1-FORMAL)

lane: r28-flex7 (layenc-gate) | vehicle: H1N1 (mk_h1n1.py @fdd6327ba;
anchors H1 L13591, N1a L13966, N1b L14127; pin 086a5c2a) | base fdd6327ba.
executor: r28ab-layenc.sh PINNED (land-tail): VFLAGS="" (source-only),
VEHICLEQ 086a5c2a76e62dea76452391e5d59644, COUNT_SYM lzmesh_u37_parse,
bounds ld<=2858/cbr<=1242/st<=1011/mov<=4307/bl=209, profdata gate ab47e992.
R28 RUN: SKIPPED (ANSWER-r28-land-2: no ship variant; staged pack banked R29).
method: R27 layenc1/4 mirror (enc-side -falign-functions=64 -falign-loops=64,
dec .o shared). R28 tip-instrument VALIDATED (layenc1: bins differ, nm
64f8->80c0, -S diff 2436, streams 24/24 IDENT, static EXACT).
binds: ANSWER-r28-flex7-1-FORMAL (a: E2a+E2b REQUIRED, n=35, single-T0+tripwire;
b: bounds + profdata gate; c: recipe STAGED; d: multi-seed carries).
NO perturb-arm timing bytes exist (none will R28: SKIP).

## arms (r28ab-layenc.sh, n=35 REPS=7, BENCH_RUNS=5 BENCH_REPS=7)

- builds: tip-std + veh-std + veh-laystd (enc + ALIGN, dec .o shared) +
  vehpgo (own T0) + laypgo (single-T0 + tripwire, Q1a) + tippgo iff
  BASELINE=tip (corroboration) + bench bins all arms.
- T0 recipe corpus-pinned (3 files + -n 3 + -n 200 -l 0); do NOT relax
  (R27 stage1c + traincorp1: corpus moves h3; zeros-only train breaks bl-flat).
- PGO-IDENT 24/24 x2 binding pairs (veh std-vs-pgo, lay std-vs-pgo).
  tripwire: cross-pair (lay-std vs lay-cross) IDENT => single-T0 VALID;
  any DIV => discard cross, own-T0 fallback, continue. both profdata md5s
  recorded either way. (tripwire PASS path: R27 validated local 24/24.)
- static: T0+PGO -S count via fncount.py (R27 h3count.py verbatim, symbol
  TBD Q1b): bounds TBD Q1b (env B_LD/B_ST/B_CBR/B_MOV/B_BL; executor HALTS
  until pinned).
- timing (0-SLOWER-ONLY regression rule, Q1a; NO prize bar):
  BINDING E2a veh-std vs veh-laystd; E2b vehpgo vs veh-laypgo.
  baseline CONTEMPORANEOUS (same batch/box/contiguous; never stale median).
  OPTIONAL corroboration (BASELINE=tip, NON-BINDING): tip-std vs laystd +
  tippgo vs laypgo, 0-slower-only.

## PASS (binding after FORMAL)

- bytes+static: PGO-IDENT 24/24 x2 + bounds all-MET (mechanism rule carries
  the verdict).
- timing: 0 SEP-slower on EITHER E2a/E2b => PASS; any SEP-slower => FAIL
  (layout regression). drop-1 directional agree (falsifier; disagree => HOLD).
- corroboration reading: lay OV-vs-veh but slower-vs-tip =
  no-prize-confirmed, NOT layout harm. corroboration can never FAIL the gate.
- variants: 1 BINDS if decisive (bytes IDENT + in-bounds + 0-slower); 2nd/3rd
  multi-seed ALIGN staged iff ambiguous (Q1d).

## burns + validity

- 1 submit serial (timing slot, dispatcher call only; post-T1-bytes +
  post-H1N1-prescreen). no re-runs; infra-fail once disclosed (tL5d precedent).
- executor command: `BENCH_RUNS=5 BENCH_REPS=7 BASELINE=veh T0MODE=single
  VFLAGS=... VEHICLE_SRC=... VEHICLEQ=... COUNT_SYM=... B_LD=... B_ST=...
  B_CBR=... B_MOV=... B_BL=... sh port/bench/air/r28flex7-layenc/r28ab-layenc.sh`
  (tip corroboration: BASELINE=tip adds the pair; verdict still from veh pair).
