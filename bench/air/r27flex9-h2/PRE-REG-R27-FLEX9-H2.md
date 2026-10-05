# PRE-REG-R27-FLEX9-H2 (BINDING via SLA-default: Q1 unanswered 16min at run time)

lane: r27-flex9 (memo-assist-beds) | base 2b7238fd4 | question: H2 of
DROP-r27-memo-r28road1 — do ephemeral (FRESH-slice-only) train inputs move
h3 codegen under fitted PGO training? bed-only: REPORTS deltas to memo,
makes NO ship claim.

## arms (pre-data: no H2-arm timing bytes exist)

- A (control, DONE stage1): T0-verbatim train (3-file corpus -n 3 + L0 boost
  -n 200 -l 0), profdata 98cfccfe, census 1455/165/120/109/347/33.
- B (fresh): SAME recipe, corpus = FRESH-slice battery inputs only
  (per-input files via mk_fresh.py). BOUND: seeds 21-24 + H5-verbatim
  (-n 3, boost kept). deviation arm owed if ANSWER-r27-flex9-1 deviates.
- C (supplemental, post-hoc, non-binding): volume-matched control (3-file,
  -n 17 + boost -n 1144, total-bytes ~= B). interprets B-A only.

## gates (bind B validity, not a prize)

- G1 PGO-IDENT B 24/24 (enc 12/12 + dec 12/12, std-vs-pgo per stage1 method).
- G2 B census reported as 6-tuple + deltas vs A (lines/ld/st/cbr/mov/bl).
- G3 whole-file enc -S op totals (ld/st/cbr/mov) reported A-vs-B (ephemeral
  sites may live outside h3_split).
- verdict rule: |lines-delta| >= 30 (union-price scale) => EPHEMERAL-SENSITIVE
  (memo prices); < 30 => EPHEMERAL-INERT at -S scale. either way B ships nothing.

## falsifier

- B census == A census EXACT + G3 totals IDENTICAL => fitted fresh training
  does not move codegen; H2 closed negative, no R28 timing arm owed.
