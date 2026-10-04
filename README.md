# lzmesh — bit-exact clean-room C port of Apple LZMESH

Independent C11 implementation of Apple's LZMESH codec
(`COMPRESSION_LZMESH`), written from specification prose only and proven
by encode byte-identity against Apple's shipping codec: port encode ==
Apple encode on 15792 gated cells, and the port self-roundtrips its own
bytes in unit tests. (Identical bytes decode identically under any
correct decoder, so encode byte-identity is the operative proof; no
cross-decoder battery has been run.)

Stock `cc`, macOS-tested (Linux build unrun, expected-trivial).
Zero dependencies, zero Apple libraries
linked. Buffer API only — no streaming API provided.

## Byte-exactness claim

Gated: 15792/15792 cells 100%, 0 ENC_DIFF vs Apple oracle.

As of 2026-09-28, source lanes/lane-4 @ e582954c (codec-identical
to the gated P6 merge 88b5a27b — e582954c touches scratch only) plus
comment-only SPDX headers. `make unit`: 44/44 suites green, fail=0.
Vector fixtures: 25 .bin + manifest (26 files, unchanged).

- Full corpus: 12784/12784. Seeds 0–16, selectors
  e00/e01/e05/e09, tier full.
- Holdout: 3008/3008. Seeds 17–20 (disjoint from full).

Method: black-box byte-identity battery, port vs Apple oracle:

```
tests/battery/battery.py --oracle ./oracle_probe --port ./port_cli \
  --out <dir> --seeds <n> --seed-offset <off> \
  --selectors e00,e01,e05,e09 --tier full
```

Runs: full `--seeds 17 --seed-offset 0`;
holdout `--seeds 4 --seed-offset 17`.

NOT covered — fresh slice seeds 21–24 (3008 cells, disjoint from the
gated range), 5 pre-existing diffs, next frontier:

- s21-n262144-alphabet e05 + e09 (lengths differ: 234023 vs 234032)
- s22-n255-alphabet e01 (same length, bytes differ)
- s24-n49-alphabet e09
- s24-n65536-alphabet e09

The 5 are byte-identical before and after the gating merge
(pre-existing, merge-inert); 0 new diffs introduced.

## Performance

Throughput in MiB/s, medians over n=70 (10 gated runs × 7 reps,
interleaved port-vs-Apple, same box back-to-back), MacBookAir M1,
Apple clang 21 `-O2 -std=c11`, R13-ship tree
(`port/src/lzmesh_enc.c` md5 992a8804,
`port/src/lzmesh_dec.c` md5 db4ae6b7; tables + PGO below read
the R14 matrix on the R13 SHIP — no R14 code change, both R14
hunters held; R14 deltas in the paragraphs below). Both
sides in-process: port `bench/bench.c` vs Apple `bench/abench`
(same harness, no fork+exec+pipe floor — R4's piped-Apple
methodology is superseded; its tables are history below).

Encode, port / Apple:

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 473.04 / 997.15 | 128.70 / 325.10 | 101.98 / 246.06 | 80.10 / 227.17 |
| mixed-128k | 401.93 / 862.07 | 174.83 / 461.25 | 130.96 / 349.65 | 104.43 / 296.91 |
| zeros-64k | 1524.39 / 1953.12 | 2976.19 / 5208.33 | 2976.19 / 5208.33 | 2976.19 / 946.97 |

Decode, port / Apple (each side decodes its own bytes):

| corpus | L0 | L1 | L5 | L9 |
|--------|----|----|----|----|
| text-256k | 1262.63 / 2293.87 | 746.27 / 1404.49 | 726.74 / 1519.77 | 728.86 / 1543.21 |
| mixed-128k | 1237.62 / 2314.81 | 1096.49 / 2450.98 | 1420.45 / 2659.57 | 1250.00 / 2403.85 |
| zeros-64k | 62500.00 / 31250.00 | 31250.00 / 5681.82 | 31250.00 / 5681.82 | 31250.00 / 5681.82 |

R4 ships decode + a sliver of encode: decres lifts text dec
L1/L5/L9 to 417.71/428.82/432.53 (Air-gated +15.3/+16.3/+16.0
pp vs R3 base, all SEPARATED, 0 slower anywhere) and enc5-C3
lifts text L5 enc to 85.79 (+2.8 SEPARATED). Mixed L5 dec reads
932.84 (+11.9 median, OVERLAP — run2 outlier, NO-CLAIM).
enc9text-F2 and enc0-C-ZERO carried (bytes-green, 0 SEP,
prizeless at gate n) — mixed L9 enc was the open frontier
at R4 (port 67.04 vs piped Apple 28.80, NO-CLAIM); R6 ships
it (next paragraph).

R6 ships L9 encode (T9+QBR: peek-r1 legs + flood/span/
direct splits + gate snapshot over the QBR query razor;
enc md5 ac44f320): text L9 enc +10.6% SEPARATED (n=25 gated
A/B base-vs-new, 55.51→61.36) and mixed L9 enc +14.3%
SEPARATED (67.02→76.64), 0 slower across 18 L1/L5/L9 cells.
Gap-share vs R5 ranking: text +2.57pp (−75.6→−73.0), mixed
+3.24pp (−77.4→−74.2).

R7 folds a fresh 24-cell in-process matrix onto this tree
(bench-only, code-identical to the R6 SHIP): tables above now
read R7 absolutes (n=70, MacBookAir, load1 1.06–1.44,
23 SEPARATED / 1 OVERLAP), re-confirming both L9 ships — text
L9 enc 61.27 and mixed L9 enc 76.31, bands disjoint vs the R5
ranking, gaps −73.0/−74.0 — and PGO is re-measured below
(9SEP/15OVER). Worst deficits: mixed L1 enc −74.8%,
mixed L9 enc −74.0%, text L9 enc −73.0% (all SEPARATED Apple).
Full R7 tables: `tmp/matrix-r7/RANKING.md`.

R8 ships the union of l9new (IRA floor + C1 flood-guard hoist +
C2 store load-fusion + C3 u27 tail-gate + R riders) and
mix1neutral (E2 stock-order redispatch + D1; enc md5 155d258a):
all 6 text/mixed enc cells SEPARATED (n=25 gated A/B base-vs-new,
24/24 cells incl L0, 0 slower) — text L1 +9.7% (93.91→103.01),
L5 +11.1% (86.09→95.68), L9 +25.9% (61.15→76.99); mixed L1
+15.8% (116.06→134.41), L5 +8.8% (107.67→117.15), L9 +25.2%
(76.22→95.42). Gap-share vs R7 ranking: text L9 +6.94pp
(−73.0→−66.0), mixed L9 +6.51pp (−74.0→−67.5), mixed L1 +4.01pp
(−74.8→−70.8), text L5 +3.88pp, text L1 +2.74pp, mixed L5 +2.66pp.
Mixed-L1 stacks superadditively (+15.8 vs +7.4/+7.1 solo);
per-side prizes re-verified first-hand (L9s +25.2/+25.7 x2 runs,
mixed-L1 +7.4/+7.8 incl mirror, L5s OV-or-better x3). S23/S24
closed. Evidence: `tmp/r8land/` (union) + `tmp/r8l9new/` +
`tmp/r8mix1neutral/`.

R9 folds a fresh 24-cell in-process matrix onto the R8 SHIP
(bench-only, code-identical): tables above now read R9 absolutes
(n=70, MacBookAir, load1 1.15–1.42, 23 SEPARATED / 1 OVERLAP),
re-confirming all 6 R8 ships — bands disjoint vs the R7 ranking
(mixed L1 enc 135.28, text L1 enc 103.52, mixed L5 enc 117.48,
text L5 enc 95.71, mixed L9 enc 96.08, text L9 enc 77.08; gaps
−70.7/−68.1/−66.3/−61.1/−67.6/−66.0) — and PGO is re-measured
below (9SEP/15OVER, same 9 cells). Worst deficits now: text L9
dec −72.1%, text L5 dec −71.7%, mixed L1 enc −70.7% (all
SEPARATED Apple). Full R9 tables: `tmp/matrix-r9/RANKING.md`.

R9 ships the union of mix1 (F6-PRE fresh6 stack prefilter;
+23/−0) and text1 (E-stack: nowin-inline + L1-extend precheck +
u35 put slow-split/inline; +109/−30; enc md5 41ec58b7): mixed L1
enc +17.4% SEPARATED (n=25 gated A/B base-vs-union, 135.28→
158.83) and text L1 enc +17.8% OVERLAP (102.88→121.24, run1-cold
both sides; narrowed text1 mechanism is +8.0% SEPARATED clean on
ab2, 103.18→111.46), 0 slower across 18 L1/L5/L9 cells.
Gap-share vs R9 ranking: mixed L1 +5.1pp (−70.7→−65.6), text L1
+5.4pp (−68.1→−62.7). L1s stack independently again (time-add
+17.2 predicted vs +17.4 observed, res 0.2pp). L5-neutrality
holds (L5s OV-or-better, mixed-L5 SEP-faster x2 narrowed). S25
closed. Evidence: `tmp/r9land/` (union) + `tmp/r9mix1/` +
`tmp/r9text1/` + `tmp/matrix-r9/`.

R10 folds a fresh 24-cell in-process matrix onto the R9 SHIP
(bench-only, code-identical, plus the warmup protocol: one
discarded full pass before run 1, 264/264 gates PASS): tables
above now read R10 absolutes (n=70, MacBookAir, load1 1.08–1.41,
23 SEPARATED / 1 OVERLAP), re-confirming both R9 ships — bands
disjoint vs the R9 ranking (mixed L1 enc 158.33, text L1 enc
122.61; gaps −65.5/−62.3; warmup killed the text-L1 43.47 cold
floor) — and PGO is re-measured below (8SEP/16OVER, L0 caveat
gone). Worst deficits now: text L9 dec −72.1%, text L5 dec −71.7%,
text L1 dec −70.5% (all SEPARATED Apple). Full R10 tables:
`tmp/matrix-r10/RANKING.md`.

R10 ships textdec (fast-loop v2: match take≤16 inline +
recents/bitpos scalars + sel-dedup + check-fusion + suffix
reorder; +129/−37; dec md5 bb5f2b9a): all 6 text/mixed dec cells
SEPARATED (n=35 gated A/B base-vs-new, 24/24 cells, 0 slower) —
text L1 +71.9% (416.67→716.33), L5 +63.8% (428.82→702.25), L9
+63.6% (431.78→706.21); mixed L1 +28.6% (816.99→1050.42), L5
+41.9% (946.97→1344.09), L9 +41.0% (886.52→1250.00). Gap-share
vs R10 ranking (projected): text L9 +17.9pp (−72.1→−54.2), text
L5 +18.0pp, text L1 +21.2pp, mixed L1 +9.6pp, mixed L5 +15.3pp,
mixed L9 +15.8pp. L5 KILL-rule holds (L5s SEP-faster x2). S26
closed. Evidence: `tmp/r10textdec/` (gates + Air n35) +
`tmp/matrix-r10/`.

R11 folds a fresh 24-cell in-process matrix onto the R10 SHIP
(bench-only pin re-point, code-identical, warmup protocol kept:
264/264 gates PASS): tables above now read R11 absolutes (n=70,
MacBookAir, load1 1.06–1.36, 23 SEPARATED / 1 OVERLAP),
re-confirming all 6 R10 ships — bands disjoint vs the R10 ranking
(text L1 dec 716.33, L5 704.23, L9 702.25; mixed L1 1041.67, L5
1344.09, L9 1237.62; gaps −49.0/−53.2/−54.5/−57.5/−49.5/−48.5;
two medians reproduce A/B opt EXACTLY) — and PGO is re-measured
below (9SEP/15OVER, L0 caveat closed). Worst deficits now: mixed
L0 enc −69.7%, mixed L9 enc −66.9%, mixed L5 enc −65.7% (all
SEPARATED Apple; top-8 all enc, decode evicted). Full R11 tables:
`tmp/matrix-r11/RANKING.md`.

R11 ships lastblock (outlined room==ds EQ leg for the exact-cap
last block; +324/−0; dec md5 db4ae6b7): exact-shape text dec
L1/L5/L9 +26.0/+15.7/+15.7% SEPARATED (n=35 gated A/B base-vs-new,
593.82→748.50 / 629.72→728.86 / 626.57→724.64; mixed-dec
+6.9/+7.7/+6.1 OVERLAP), 0 slower across 48 std+exact cells
(matrix-shape A/B 0 SEP/24 OV — the leg is dead at n+64 by
construction). Gap-share vs R11 ranking: ranked cells unmoved
(std-shape text-dec +3.9/+3.5/+3.2 OVERLAP ≈ noise, NO-CLAIM);
exact shape has no Apple baseline so no projected gaps.
Dose-response L1>L5=L9 tracks census shares 20.6>12.8≈11.8. S27
closed (l1cont2 HOLD carries to S28). Evidence:
`tmp/r11lastblock/` (gates + Air n35v2 + v1-kill n35) +
`tmp/matrix-r11/`.

R12 folds a fresh 24-cell in-process matrix onto the R11 SHIP
(bench-only pin re-point, code-identical, warmup protocol kept:
264/264 gates PASS) plus an Apple exact-cap arm (new
`abench-exact`, n+64→n, 24/24 EXACT): tables above now read R12
absolutes (n=70, MacBookAir, load1 1.10–1.70, 23 SEPARATED / 1
OVERLAP), re-confirming all 6 R11 ships 6/6 CONFIRM (exact-shape
medians inside A/B-opt bands, rel −2.1%…+0.1%) with 24/24 std
cells UNMOVED — and PGO is re-measured below (8SEP/16OVER,
tL1d suspect-run watch CLOSED). The exact-cap arm closes R11's
gap-pp N/A: exact gaps text-L1/L5/L9-dec −47.8/−53.0/−53.9%,
mixed −57.0/−48.4/−48.5%. Worst deficits now: mixed L0 enc
−69.8%, mixed L9 enc −66.7%, mixed L1 enc −66.0% (all
SEPARATED Apple; top-8 all enc, decode evicted). Full R12
tables: `tmp/matrix-r12/RANKING.md`.

R12 ships l0 (H1 4-lane histogram + H2 combined code|len put
clone + H3 zero-lanes delete; enc md5 5c35b9af solo, 0db0b219
in union): FIRST flagship on the #1 cell — mixed L0 enc +43.1%
SEPARATED (n=35 gated A/B base-vs-new, 261.51→374.25) and text
L0 enc +26.5% SEPARATED (384.62→486.38), 0 slower across 24
cells. Gap-share vs R11 ranking (projected): mixed L0 +13.1pp
(−69.7→−56.6), text L0 +10.7pp (−61.2→−50.5). Dose-response
tracks PMU −28.9/−20.2% cycles. S28 consumed (with h123).
Evidence: `tmp/r12l0/` (gates + Air n35 + 57904-direct) +
`port/bench/air/r12l0/`.

R12 ships h123 (H123 re-apply: span-x4 + split-hoist + huff-put,
plus H4 L1-best-spec + L1-skip-spec + lazy-once; enc md5
2385588d solo, 0db0b219 in union): 6 SEP all opt-faster / 18 OV
/ 0 slower (n=35 gated A/B base-vs-new, 24/24 cells) — mixed L1
+5.2% (158.83→167.11) and mixed L5 +5.8% (119.27→126.14)
flagships, text L1 +4.9%, text L5 +4.4%, L9s +2.1/+3.3%.
Gap-share vs R12 ranking (projected): +2.3/+2.3/+2.0/+1.9/+0.9/
+1.5pp. H4 stacks +1.1 over the banked H123 +3.8 on text (memo
probe/skip/walk census priced +1–2.5); mixed +5.2/+5.8 exceeds
the branch model (open mechanism, S29). S28 closed. Evidence:
`tmp/r12h123/` (gates + Air ab1 n35) + `tmp/r12memo/` (Q/A +
beds).

R13 folds a fresh 24-cell in-process matrix onto the R12 SHIP
(bench-only pin re-point, code-identical, warmup protocol kept:
264/264 gates PASS): tables above now read R13 absolutes (n=70,
MacBookAir, load1 1.13–1.36, 23 SEPARATED / 1 OVERLAP),
re-confirming all 8 R12 ships 8/8 CONFIRM (tip medians inside
A/B-opt bands, rel −2.1%…+0.0% with mL9e EXACT) with 16/16
non-ship cells UNMOVED — and PGO is re-measured below (6SEP/18OVER,
0 slower 4th round running: ships ate L0 headroom, tL0e SEP→OV).
Worst deficits now: mixed L9 enc −66.0%, text L9 enc −64.8%,
mixed L5 enc −64.0% (all SEPARATED Apple; R12 ships moved L0s off
the top: mixed L0 #1→#7 +12.3pp, text L0 #6→#11 +15.6pp). Full R13
tables: `tmp/matrix-r13/RANKING.md`.

R13 ships l0res (D1 stable-LSD radix sorts gated n>=64 for
huff_ord + rank_assign, plus D2b dbits=fbits reuse on the V2c
keep-path; enc md5 992a8804; +89/−11): 3 SEP all opt-faster / 21
OV / 0 slower (n=35 gated A/B base-vs-new, 24/24 cells) — mixed L0
enc +9.8% flagship (372.02→408.50), mixed L1 +5.2% and mixed L5
+4.4% (g1 radix bonus); text L0 +0.0% OV (nsym=25 keeps stock
sorts). Gap-share vs R13 ranking (projected): mixed L0 +4.9pp
(−57.5→−52.6), mixed L1 +2.0pp, mixed L5 +1.5pp. Dose-response:
post-stack sample projected ~10–11% mixed, measured +9.8.
tl1 H5 HOLD carries (+3.5% SEP below bar, banked). Evidence:
`tmp/r13l0res/` (gates + Air n35 + 57904-direct) +
`port/bench/air/r13l0res/`.

R14 folds a fresh 24-cell matrix onto the R13 SHIP (no code
change: tables above now read R14 absolutes, n=70, MacBookAir,
load1 1.11–1.62, 23 SEPARATED / 1 OVERLAP, 264/264 gates PASS),
confirming R13 3/3 CONFIRM (l0res A/B-opt bands, rel −1.6/−0.4/
−0.3%, projections within 0.8pp) with tl1-HOLD cells 2/2
UNCHANGED and 19/19 non-ship cells UNMOVED. Top-5 order
unchanged: mixed L9 enc −64.8%, text L9 enc −64.7%, mixed L5
−62.5%, mixed L1 −62.1%, text L1 −60.4%. Full R14 tables:
`tmp/matrix-r14/RANKING.md`.

R14 ships nothing: tl1stack H5+g1 HOLD (text L1 enc +4.7% SEP,
128.80→134.92, + text L5 +2.0 / L9 +1.1 SEP, 0 slower n=35;
banked e4cc8936; span_hide KILLED pre-Air as compiler-folded
0-delta, dbits closer +3.5 OV failed) and mL9 runscan-fuse v2
HOLD (mixed L9 enc +3.6% OV flagship, 105.22→108.98, + mixed L5
+4.6 SEP best, 0 slower n=35; banked 3fcff538; v1 memcpy
PMU-killed). Gap-share vs R14 ranking (projected): text L1
+1.9pp (−60.4→−58.5), mixed L9 +1.5pp, mixed L5 +2.0pp. Memo
priced the stack ceiling (+4.3–4.8 source, PGO-stack +12.0 the
only +5 closer) and re-moded H5 as fetch diet. Evidence stays
on the lane branches (HOLD verdicts banked, not merged).

R15 folds a fresh 24-cell matrix onto the R13 SHIP (pre-ship
tip: tables above still read R14 absolutes until the next
re-rank; R15 matrix n=70, MacBookAir, 23 SEPARATED / 1
OVERLAP, 264/264 gates PASS), confirming R13 3/3 CONFIRM
(rel −2.9/−0.7/−0.6%, medians inside A/B-opt) with HOLD
cells 3/3 UNCHANGED (rel −0.2% each) and 18/18 non-ship
cells UNMOVED. Top-5 same cells, #1/#2 noise swap (0.1pp):
text L9 enc −64.8%, mixed L9 enc −64.7%, mixed L5 −62.6%,
mixed L1 −62.1%, text L1 −60.5%. Full R15 tables:
`tmp/matrix-r15/RANKING.md`.

R15 ships tl1 stack+T2+S4 (banked H5+g1 verbatim + T2
per-site fusion on S3/S6, sb reused, T2 loops deleted +
S4-thread bitc publish via mblk; enc md5 c5ed7082; +142/−46
enc.c only): 3 SEP all opt-faster / 21 OV / 0 slower
(n=35 gated A/B) — text L1 enc +7.9% flagship
(128.93→139.12, 35/35 reps) + text L5 +4.3 / L9 +2.9 SEP.
Gap-share vs R15 ranking: text L1 +3.1pp (−60.3→−57.2),
text L5 +1.8pp, text L9 +1.0pp. T2+S4 realized ~+3.2 vs
priced +0.3–0.5 (superadditive; fetch-model autopsy owed).
PGO-stack +12.0 text L1 (6th straight 0-slower PGO round)
and dbits closers MOOT. mL9 3-layer HOLD carries (mixed L9
enc +3.2% OV flagship below bar, banked d4060747; unpriced
runscan-x-G1 L1 interaction +9.7/+6.6 banked unshipped,
union re-measure mandatory on any future merge). Evidence:
`tmp/r15tl1/` (gates + Air n35 + 57904-direct) +
`port/bench/air/r15tl1/` + land re-runs `tmp/r15land/`.

R16 folds a fresh 24-cell matrix onto the R15 SHIP (tip:
enc c5ed7082; R16 matrix n=70, MacBookAir, 23 SEPARATED /
1 OVERLAP, 264/264 gates PASS, DIV md5 5d68b09c == R15),
confirming R15-ship 3/3 CONFIRM (rel −0.1/+0.1/+0.0%,
medians inside A/B-new) + HOLD 1/1 UNCHANGED (mL9e +0.6%
rel, bands overlap) + 20/20 UNMOVED. Top-5 same cells,
#1/#2 swap back: mixed L9 enc −64.7%, text L9 enc −63.7%,
mixed L1 −62.3%, mixed L5 −62.1%, text L1 −57.1%
(tL1e +3.4pp on the ship). Full R16 tables:
`tmp/matrix-r16/RANKING.md`.

R16 ships nothing (0/2 hunters below bar, both 0 slower
n=35 gated A/B): mL9 union HOLD (tip + runscan-v2 + drain
verbatim, banked 0e5d9667; 4 SEP flagship — mixed L1 +9.5
/ mixed L5 +5.5 / text L1 +4.7 / text L9 +1.0 — one short
of the >=5 bar; drain-x-L1 superadditive +5.9pp on mixed
L1, union dominates runscan-only) + tL9 YF-MEMSET HOLD
(0 SEP / 24 OV, flagship +0.0%: clang −O2 already emits
the memset, −S delta guard-only; PMU branch+l1d no-cut;
datum: −S fold-check gates Air). Gap-share vs R16 ranking:
mixed L1 +3.6pp / mixed L5 +2.1 / text L1 +2.0 / text L9
+0.4. Memo: T2+S4 autopsy rule-2 (split br/chain/memop
before pricing) + l1x path+drain DEAD beds (interaction
is timing-only). PGO 6/18 7th straight 0-slower round
(text L1 +7.2 repriced); tL9d OV sliver persists (+6.4,
touch 739.64 exact). Evidence: branch lanes (`lanes/r16-mL9`,
`lanes/r16-tL9`) + drops + land re-runs `tmp/r16land/`.

R17 ships D20 (YF-helper + drain-prefetch, enc c9f8cefd):
text L9 enc +6.0% SEP (n=35, 1 SEP / 23 OV, 0 slower;
82.54->87.47, bands separated, sizes 79169=79169; gap-share
+2.2pp vs R17 ranking). Take consumed-marks (99.8% <=16B,
avg 9B) via a noinline outline helper (~11c vs ~33c
memset-call; parse perturbation 1 bl, L1/L5-neutral) +
peek-h1 prefetch piggybacked in the existing L9-only drain
arm (0 new L1/L5 branches). Kills banked on the lane:
STACK1 (L1/L5 slower), inline-YF shapes, D11, D18
(prefetch-guard toxic), flood leg-skips (memo FORMAL).
mL9 union HOLDs again (4/5 SEP flagship, 0 slower, banked
0e5d9667; layout gate PASS 0/24/0, prize is CODE; drain-x-L1
mechanism NAMED codegen-shape fetch, rule-2 amd #2; no
in-wave lever). Fresh R17 24-cell matrix (pre-ship tip
c5ed7082, n=70, 23 SEP / 1 OV, DIV md5 5d68b09c == R16):
top-5 mixed L9 enc -64.6% / text L9 -63.6 / mixed L5 -62.3
/ mixed L1 -62.3 / text L1 -57.2 (HOLD 2/2 UNCHANGED +
22/22 UNMOVED). Memo Q/A 2/2 FORMAL: flood NO >=5 path
(Q1 NO, Q2 SIMD-only, Q3 span OPEN) + memop-work
superadditivity + spill-store delta. PGO 6/18 8th straight
0-slower round; tL9d OV sliver persists (PGO +4.9, matrix
touch 739.64 exact). Evidence: branch lanes (`lanes/r17-mL9`,
`lanes/r17-tL9`, `lanes/r17-matrix`, `lanes/r17-memo`) +
drops + land re-runs `tmp/r17land/`.

R18 ships H9-sink (union' + S4/S5 sunk into i5 arms, enc 3fec8aa3):
mixed L9 enc +5.2% SEP (n=35, 3 SEP / 21 OV, 0 slower;
108.23->113.84, bands separated, sizes 32708=32708; gap-share
+1.9pp R18-conv) + mixed L1 +6.3 / mixed L5 +5.1 SEP (gap-share
+2.4/+1.9pp). Sink kills 33k dead cross-level guard evals/rep
(memo arcs EXACT) + VRP folds level legs; u37 br -14, bl flat,
L5-neutral (union' alone NO-SHIP: tL5e -1.4 SEP-slower,
diet-codegen-shape). Layout gate PASS (H9-vs-H9pad 0/24/0,
prize is CODE). Costs (OV): tL1e -1.6 (codegen, watch).
tL9 D21b HOLDs (S7-only guard +0.5 OV, 0 slower, banked
65ad1f37; D22 prefetch + D24 rep-fusion KILLED+reverted).
Fresh R18 24-cell matrix (D20 tip c9f8cefd, n=70, 23 SEP / 1 OV,
DIV md5 5d68b09c == R17): top-5 mixed L9 enc -63.4% / mixed L1
-62.3 / mixed L5 -61.9 / text L9 -61.5 (+2.1 ship) / text L1 -57.2
(R17-SHIP CONFIRM 1/1 + HOLD 1/1 + 22/22 UNMOVED). Memo Q/A 1/1
FORMAL: tL5e diet-codegen-shape attribution + H9 GREEN + span
SUBSET flood both flagships; fusion order-proof Q carried.
PGO 6/18 9th straight 0-slower round; tL9d OV sliver persists
(PGO +5.5, no touch). Evidence: branch lanes (`lanes/r18-mL9`,
`lanes/r18-tL9`, `lanes/r18-matrix`, `lanes/r18-memo`) +
drops + land re-runs `tmp/r18land/`.

R19 ships nothing (0 SHIP + 1 HOLD + 1 KILL, tree stays H9
3fec8aa3): mL9 T-prefetch V1 HOLDs (Air n=35: 0 faster + 4
SEP-slower tL1e -2.8 / tL5e -2.4 / mL1e -5.2 / mL5e -3.4,
flagship mL9e -2.6% OV; diet-codegen-shape kill, banked
7af283f81) + tL9 E3 drain-outline KILLed pre-Air (PMU RED:
ld +6.24 / st +7.41 SEP, +37 memops/drain call traffic;
R2-fuse vindicated, banked aba4109cf). Fresh R19 24-cell
matrix (H9 tip, n=70, 23 SEP / 1 OV, DIV md5 5d68b09c ==
R12-R18): top-5 mixed L9 enc -61.9% / text L9 -61.2 /
mixed L5 -59.8 / mixed L1 -59.4 / text L1 -57.7
(R18-SHIP CONFIRM 1/1 rel -0.9 gap-share +2.1pp + COGAIN
2/2 + HOLD 1/1 + 20/20 UNMOVED; L5-neutral +0.0). Memo
Q/A 4/4 FORMAL: fusion CLOSED all 3 ways (MOVE=NO lag0
DIVs, elision PMU-killed, static NONE) + h1h2-identity
named (hunter-killed) + H9-template EXHAUSTED (no second
sink); endgame Q3 carried. PGO 5/19 10th straight
0-slower round (mL9e SEP->OV on run9 transient, stands);
tL9d OV sliver persists (PGO +6.0, no touch). Evidence:
branch lanes (`lanes/r19-mL9`, `lanes/r19-tL9`,
`lanes/r19-matrix`, `lanes/r19-memo`) + drops + land
re-runs `tmp/r19land/`.

R20 ships nothing (0 SHIP + 2 HOLD, tree stays H9
3fec8aa3): tL9 T2-alloccache HOLDs (Air prescreen n=6 x2:
tL9e +0.3/+0.8% OV, 0 SEP-slower both; PGO-interaction
drag, ceiling +4.4 < 5; banked e4940491) + T1-genbit
KILLed by warmth (SLC-cold 200-400us > 140us wall,
P10-accepted restored) + mL9 no-vehicle HOLD (stack
~2-3%, SIMD dead structurally, T1 tL9-owned). Fresh R20
24-cell matrix (H9 tip, n=70, 23 SEP / 1 OV, DIV md5
5d68b09c == R12-R19): top-5 mixed L9 enc -62.1% / text
L9 -61.2 / mixed L5 -59.8 / mixed L1 -59.7 / text L1
-57.8 (HOLD 1/1 rel -0.3 gap-share -0.1pp + KILL 1/1
-0.0/-0.0pp + 22/22 UNMOVED; L5-neutral -0.1%). Memo
Q/A 2/2 FORMAL: peek-mixed CLOSE + emit diffuse + SIMD
KILL + T1 support/holes + lo-skip CLOSE (text-L9 DIV) +
bestcall picked-over + Q1-emit PMU (split top, no micro
>=0.5%). PGO 5/19 11th straight 0-slower round (same 5
cells; tL9d EXACT-touch OV 739.64==739.64, watch R21).
Evidence: branch lanes (`lanes/r20-mL9`, `lanes/r20-tL9`,
`lanes/r20-matrix`, `lanes/r20-memo`) + drops + land
re-runs `tmp/r20land/`.

R21 ships nothing (0 SHIP + 1 HOLD + 1 HOLD/KILL, tree stays H9
3fec8aa3): tL9 V3 separate-TU+attrs HOLDs (Air prescreen n=6 V3+PGO:
tL9e +0.5% OV, 5 SEP all faster, 0 slower; T2-drag BINARY not graded,
ceiling +4.3 < 5; banked 8ad7aabe/7470a305, NOT merged; dust exhausted)
+ mL9 HOLD + M1 KILL (Air prescreen n=6 M1-solo: mL9e -5.8% OV + mL5e
-3.5% SEP-slower, diet-codegen-shape; datum 812a784d0, src reverted).
Owner RULING-PGO-SHIP: PGO-ship ALLOWED (S37 policy closed; physics
kills stand). Fresh R21 24-cell matrix (H9 tip, n=70, 23 SEP / 1 OV,
DIV md5 5d68b09c == R12-R20): top-5 mixed L9 enc -62.0% / text L9 -61.3
/ mixed L5 -60.0 / mixed L1 -59.6 / text L1 -57.6 (HOLD 1/1 rel -0.1
gap-share -0.0pp + 23/23 UNMOVED; L5-neutral 3rd round). Memo Q/A 2/2
FORMAL + 1 ADD1: TU DOWNGRADE (171/12) + interpose-T2 LIVE + MICRO-1
CONFIRMED/EXHAUSTED + PGO-closer + v3killread. PGO 5/19 12th straight
0-slower round (same 5 cells, deltas -0.1/+0.1; tL9d EXACT-touch OV 2nd
round, mL9e OV tail 3rd round). Evidence: branch lanes (`lanes/r21-mL9`,
`lanes/r21-tL9`, `lanes/r21-matrix`, `lanes/r21-memo`) + drops + land
re-runs `tmp/r21land/`.

Reading the gap (both sides in-process, no pipe floor):

- Apple ahead on all 16 text/mixed codec cells (all SEPARATED,
  gaps −45.0% to −64.8%): real port deficits, the optimization
  frontier. Text L0 enc carries wide bands (median 473.04,
  band 390.02–489.24, Apple wide too 871–1082 — sprawl, not
  signal; R13 lift holds cross-run (483.56→473.04, bands
  overlap).
- Zeros cells are timer-floor artifacts (NO-CLAIM): enc medians
  flip between 2976/3125 quanta run to run; zeros L0 dec reads
  n=64/70 (ns==0 samples dropped by the cmp filter) OVERLAP;
  L1/L5/L9 dec +450.0% x3 SEPARATED-port is pure
  quantum artifact, not a speed claim (median flip back
  62500→31250 inside the same band).

Byte note: port enc == Apple enc on 9/12 cells; mixed-128k
L1/L5/L9 differ (e01 34181@1152, same length; e05 32799v32796@2;
e09 32708v32707@8; every output self-roundtrips and cross-decodes
OK, 12/12 both directions; not ship-introduced — R4 union
FULL 57904 NEW=0 base-vs-union direct; R6 ship FULL/HOLD/
FRESH 18800 DIV=0 base-vs-ship direct, land-rerun; R7 matrix
re-verifies the same 9/12 IDENT + 3 mixed DIVs, DIV lines
byte-identical x10 and == R5; R8 union FULL/HOLD/FRESH 18800
DIV=0 + NOWIN0 12784/0 base-vs-union direct, land-rerun, pins
12/12 on Air too; R9 matrix re-verifies the same 9/12 IDENT +
3 mixed DIVs (DIV lines byte-identical x10 and == R7) and R9
union FULL/HOLD/FRESH 18800 DIV=0 base-vs-union direct,
land-rerun, pins 18/18 on Air too; R10 matrix re-verifies the same
9/12 IDENT + 3 mixed DIVs, DIV lines byte-identical x10 and == R9,
and R10 textdec ship FULL/HOLD/FRESH 18800 DIV=0 base-vs-ship
direct, lane-gated + land-verified; R11 matrix re-verifies the same
9/12 IDENT + 3 mixed DIVs, DIV lines byte-identical x10, md5 93e58ca0
== R10, and R11 lastblock ship FULL/HOLD/FRESH 18800 DIV=0 x2
cap-modes (37600/0) + failparity 964/0 base-vs-ship direct,
lane-gated + land-verified; R12 matrix re-verifies the same
9/12 IDENT + 3 mixed DIVs, DIV lines byte-identical x10 x2 arms,
md5 5d68b09c == R11, and R12 l0+h123 union FULL/HOLD/FRESH
12784/3008/3008 DIV=0 + 57904-direct NEW=0 + units
1083/1076+1074/1067 parity IDENTICAL + smoke 1232/0
base-vs-union direct, lane-gated + land-rerun; R13 matrix
re-verifies the same 9/12 IDENT + 3 mixed DIVs, md5 5d68b09c ==
R12, and R13 l0res ship FULL/HOLD/FRESH 12784/3008/3008 NEW=0 +
57904-direct NEW=0 + units 1083/1076 both modes (land TRUE scalar
re-run) + smoke 1232/0 x2 modes base-vs-ship direct, lane-gated +
land-verified; R14 matrix re-verifies the same 9/12 IDENT + 3 mixed
DIVs, DIV lines byte-identical x10, md5 5d68b09c == R13, and R14
HOLD stacks tl1stack/mL9 each FULL/HOLD/FRESH 12784/3008/3008
NEW=0 + 57904-direct NEW=0 + units 1083/1076 both TRUE-scalar
modes + smoke 1232/0 x2 modes base-vs-stack direct, lane-gated +
land-verified filings (banked, unmerged)). Sizes within 3 B,
so dec-timing inputs are size-matched.

PGO is opt-in and build-only (`make pgo`; the default build tree
is untouched, no source change). Re-measured on this tree (Air,
interleaved --ab, n=70, 6SEP/18OVER, 6 faster, 0 slower, 5th
straight 0-slower round),
text-256k: enc L1/L5/L9 +12.0/+4.5/+5.8% (mixed-enc L1/L5/L9
+6.7/+6.8/+5.1%, see `tmp/matrix-r14/pgo/cmp-pgo.txt`; quoted cells
SEPARATED; text-enc L0 −0.8% OV and text-dec L9 +5.9% OV
(band-touch sliver persists, touch 739.64 exact, watch OPEN) +
text-dec L1/L5 +2.8/+6.0% + mixed-dec L0/L1/L5/L9
+0.0/+2.2/+3.6/+3.2% OVERLAP are NO-CLAIM).
Caveats: none SEPARATED-slower — L0 PGO headroom now ~0 on both
sides (text L0 enc −0.8% / mixed L0 enc −5.0% OVERLAP; static diet
only, no PGO-stack left on L0); text/mixed dec L0 −0.5/+0.0%
OVERLAP (NO-CLAIM); zeros all OVERLAP (timer floor).
Byte-identity holds
under PGO: PGO-binary enc == normal enc 12/12.

Sources: every cell traces to
`tmp/matrix-r14/ev/cmp-matrix.txt` (bands + n +
verdicts), reproducible byte-identically from `ev/matrix/`
run TSVs; PGO from `tmp/matrix-r14/pgo/cmp-pgo.txt` + `gated-pgoab/`;
exact-cap from `tmp/matrix-r12/exactcap/cmp-exactcap.txt`.
Gate: load1 < 16, 0 refusals on all runs (matrix 1.10–1.70,
pgoab 1.23–1.29, xcap 1.10–1.35), pin `taskpolicy-t0l0`.
Method: `docs/PERF.md` + `bench/GATED-PROTOCOL.md`; Apple
harness: in-process `bench/abench` (R4 piped-oracle tables
superseded).

## Clean-room methodology

No decompiler output, no Apple source, and no third-party LZMESH code
was read or used at any point. The derivation chain:

- Hint-firewall. Implementers read the final spec Part A only, plus
  the battery CLI contract and their own lane's divergence reports —
  never the Apple binary, never other lanes, never spec-side
  transcripts. A hard read barrier splits what each side may open;
  blocked on a clause, a lane files a one-way spec query instead of
  guessing.
- Black-box oracle batteries. Every behavior claim is measured by
  executing the port CLI and the Apple oracle CLI and comparing bytes
  — operators never read implementation source. The battery framework
  is a vendored, pinned copy; any resync invalidates prior campaigns.
- Zero Apple code. This repo never links an Apple binary: the oracle
  adapter source ships pinned (`tests/battery/oracle_probe.c`, see
  `tests/battery/VENDORED.md`), no default target builds or links it,
  and the library never touches Apple code. No Apple code or Apple
  library bytes are
  committed: the fixtures under `tests/unit/vectors/` are black-box
  functional data (test inputs plus recorded oracle output bytes,
  traced per-file in `vectors/manifest.txt`), and the tree contains
  no Mach-O.
- Neutral identifiers. Public prefix is `lzmesh_`; upstream
  `mesh_*`/`MESH_*` and Apple-private `Msh*`/`msh_*` identifiers are
  forbidden. No spec prose or constants tables are reproduced in the
  repo — per-file provenance lives in `docs/DERIVATION.md`, and every
  session logs its exhaustive inputs (including the sha256 of the
  exact spec version read) in `docs/CLEANROOM-LOG.md`.

## Build

```
make            # liblzmesh.a + port_cli
make selftest   # battery framework sanity — python3 only, no codec
make unit       # fast C unit tests over the public API
make smoke      # fast battery tier (needs port_cli + oracle_probe)
make full       # full battery tier (needs port_cli + ORACLE_LIB)
```

Flags: `-O2 -std=c11 -Wall -Wextra`, zero warnings required.
Details, variables, and platform notes: `docs/BUILD.md`.

## Use

```c
#include "lzmesh.h"

/* level selects the encoding level (0xE00/0xE01/0xE05/0xE09). */
size_t n = lzmesh_encode(dst, dst_cap, src, src_len, NULL, 0xE09);

/* one decoder handles all levels; size the destination first — an
   undersized buffer truncates with no error signal. */
size_t m = lzmesh_decode(out, lzmesh_decoded_size(dst, n), dst, n, NULL);
```

`scratch` may be `NULL` (the library allocates) or point to at least
`lzmesh_encode_scratch_size(level)` / `lzmesh_decode_scratch_size()`
bytes. Encode returns bytes written, or 0 on failure. Deterministic:
same input + level yields identical bytes on every run.

Integrity warning: the format carries no checksum; decoding never
validates. Applications needing integrity layer their own digest above
the codec.

## Layout

```
README.md            this file
BUILD.md             build + test gates, pinned to this tree
CHANGELOG.md         release history (Keep a Changelog)
LICENSE              license text (0BSD; injected at export)
LICENSE-CHOICE.md    decided license + pre-decision memo history
RELEASE-CHECKLIST.md ordered gates before publish
.gitignore           ignores (injected at export)
Makefile             all / selftest / unit / smoke / full / bench / pgo / clean
DECISIONS.md         every skeleton choice + its source directive
include/lzmesh.h     public API: encode, decode, scratch sizes,
                     decoded-size framing walker
src/                 port sources + port_cli.c (battery CLI:
                     enc|dec <selector-hex>, stdio byte pipe)
tests/battery/       vendored divergence-battery framework (pinned)
tests/unit/          fast unit tests + vector fixtures
bench/               benchmarks + pinned corpus + generator
release/             deterministic public-tree exporter (export.sh)
docs/                BUILD / TESTING / API / PERF / RELEASING /
                     SPEC-S9CR + CLEANROOM-LOG + DERIVATION + README
```

What does NOT ship: research notes, agent scratch, battery result
dirs, build artifacts, the oracle adapter binary, or any Apple library
— the tree builds with Apple libraries absent.

## License

License: 0BSD — see LICENSE.
