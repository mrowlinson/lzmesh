# PRE-REG-R27-TL5D (BINDING v1 for r27-tL5d L0-ship gate; filed BEFORE any R27 Air bytes)

lane: r27-tL5d | base 2b7238fd4 | territory flagship-A tL5d/L0.
ship target: banked d1 (B1B2a+D1) PGO stack as L0-DECODE ship (tL0d flagship).
bank: R26 G1 n=35 std-stack (cmp-stack.txt): tL0d +20.1 SEP + mL0d +17.9 SEP,
2 SEP / 22 OV / 0 slower; warmed vetoes (tL0e -1.3 SEP-slower).

## rig (T3)

- PRIMARY: std (bench.c aec6dd80, BENCH_INNER_WARMUP unset).
- SECONDARY (non-binding, rig-gap resolution): warmed-k7 (bench-w.c 553c853d,
  BENCH_INNER_WARMUP=7, banked bench-warmup.diff applied verbatim).
- REPS=7 gate-matched on EVERY timing run (prescreen + gate + laypad).
  prescreen n=14 (RUNS=2), gate/laypad n=35 (RUNS=5).
- falsifier (manual, pre-registered): run-parity split (odd/even runs same
  flagship direction) + drop-1 directional agreement on flagship + all SEPs.
  falsifier split => VOID arm, HOLD (no re-run as primary).

## vehicles (md5 pins, verified in worktree 2026-10-05)

- tip dec-tip.c 0dae2df417bd56d06dc30f94fa0ebd31 (R23 ship dec).
- d1 dec-d1.c 5a147a81d4df9269f52e652981dd5776 (B1B2a+D1, D1 fused-clean).
- bench-w.c 553c853dab2dc5d7b6f144449a598d5b.
- tree enc 3fec8aa3274f901b1372be85936a77a2 + bench.c aec6dd8062e180c25531484d33f60ee8.
- bytes pre-gated (banked R26, re-verified R27 in-submit): 12/12 + 3600
  mutants 0 + units identical + warn0 + fusion audit (D1 comp_block fused).
- PGO: T0 recipe (train on STD bench: -n 3 full + -n 200 -l 0 L0), per-vehicle
  own profile. shipped artifact = d1pgo bench binary + d1pgo CLI.

## prescreen (ONE submit, prescreen remote, REPS=7, n=14)

- r27ab-pre.sh: oracle-gate 24/24 pinned (HALT on drift) + builds (warn count
  recorded) + PGO-IDENT 24/24 (tip + d1) + stack-d1 std A/B (tippgo vs d1pgo)
  + drop-1 + cmp.
- GO gate iff: 0 SEP-slower + tL0d SEP >=5 (directional confirm of banked
  +20.1/+17.9). else HOLD-with-proof (no gate burn).

## gate (ONE submit, gate slot AFTER matrix, CLAIM held, n=35)

- r27ab-gate.sh: stack (tippgo vs d1pgo, std PRIMARY) + stackW (warmed
  SECONDARY, expected tL0e SEP-slower reproduce, non-binding) + drop-1s.
- SHIP iff ALL: (a) stack primary 0 SEP-slower anywhere (L5 load-bearing
  incl); (b) flagship tL0d >=5 SEP; (c) mL0d OV-or-better (expected SEP);
  (d) laypad PASS; (e) bytes NEW=0 (FULL/HOLD/FRESH/d77, std-self AND d1pgo
  CLI); (f) units 1083/1076 TRUE-scalar + default; (g) smoke 1232/0 x2
  (default + scalar); (h) profdata pinned + committed + recipe in tree.
- drop-1 SEP-slower: secondary-flag rule (RULING-L0-DROP1-ENCODE): flagged,
  not veto, iff primary clean + vehicle L0-inert for that cell (dec-only
  delta => encode cells inert by construction) + lean == PGO-alone lean
  within noise (inheritance check vs matrix PGO-alone, same rig preferred).

## laypad (ONE submit, gate slot, n=35)

- r27ab-laypad.sh (R24 method): d1-std vs d1-laystd (+ALIGN, std) vs
  d1-laypgo (+ALIGN + own T0 train). dec-only perturb, enc .o shared.
- PASS iff: laystd 0 SEP (layout alone prizeless) AND laypgo flagship tL0d
  >=5 SEP (prize robust, not layout luck). laypgo collapse to OV => HOLD.

## conventions + burns

- quote rel uplift pts + gap-share pp on R27-matrix Apple anchors.
  cross-era pp NOT comparable: if r27 anchors unavailable at verdict, rel-only
  + pp pending land (disclosed, never banked-anchor pp).
- burns: prescreen 1 + gate 1 + laypad 1 + bytes (batch remote, non-timing).
  no re-runs; infra-failed submit may re-run ONCE, disclosed with cause.
