# P3-harden pack1 cap-257: differential summary (lane lanes/p3-harden)

STYLE: caveman. numbers-first. base 7500c15a. 1 fix + 1 test, no other
touched files.

## 0. verdict

- FIX (port/src/lzmesh_enc.c, pack1_solve): sub[] cap 256 -> 257,
  take-gate `>= 256` -> `> 256`. 2 constants + comment. Exact old
  (pre-P2 expand) boundary semantics by construction.
- DIFFERENTIAL (port/tests/unit/test_pm_diff.c, committed): shipped
  lzmesh_pack1_lengths vs oracle (ce3c3dc5 expand/sort/solve/lengths,
  verbatim copy renamed orp1_*, self-contained): 100134 cases,
  0 mismatch. r00=44009 r11=56125. maxsub=255 (gate >= 250 green).
- SENSITIVITY proven by mutation: bound flipped to >= 255 ->
  mm=1 (spike-255 crafted case), FAIL. Reverted, green again.
- UNIT: ship flags 982 PASS / 0 FAIL exit 0; debug (-O0 -g)
  982-equiv / 0 FAIL; all 39 binaries pass from tests/unit cwd too.
- BATTERY: FULL 12784/0 + hold 3008/0 + fresh 3008/5, findings
  row-sets byte-identical to tmp/verify-p2 bases, 5 fresh vectors
  byte-identical (0 NEW everywhere).
- BENCH n=35 solo vs P2 opt medians: median uplift ~0% (20/24 cells
  within +/-8%, bands overlap all 24; outliers move run-to-run and
  include decoder cells untouched by this fix -> load noise).

## 1. the divergence (parent-proven, code-reading)

- OLD (ce3c3dc5 pm_expand): emits leaves until seqcap 256; returns 0
  (fail) only when a 257th leaf arrives -> fails iff count > 256.
- P2 (7500c15a sub/propagation): sub[i] = min(true,256); take-gate
  fails iff sub >= 256 -> fails at exactly 256 too.
- DIVERGE condition: take item with EXACTLY 256 leaf-occurrences
  AND old otherwise succeeding (old proceeds through it, counts
  256 visits; P2 fails the whole solve -> retry-at-2q or decline).

## 2. reachability hunt (scratch probes, NOT committed)

~700k shapes across 11 probes (uniform/ties/spike/plateau/ramp/
binary-{A,B}/pow2/geometric/random-skewed, n = 2..256, maxlen 5+10):

- take-resident 256: NEVER observed (0 / ~700k).
- in-take max observed: 255 (spike n=256, 255x1 + F>=255, or=1).
- 256-occurrence packages DO form (5+ cases) but ALWAYS as the
  unique-maximum-weight final item at rank == take (first excluded):
  take = 2n-2 smallest of ncur ~ 2n-1 excludes exactly it.
- twin-256 (tie pulling one into take): 0 / 60k tie-heavy shapes.
- repeats exist (400 take-resident, pow2-tie shapes, sizes <= ~240)
  but never reach 256 in-take; nothing anywhere exceeds 256 in
  2255-case uncapped census (1.1M nodes).
- direct solve() callers (u35_lengths, g1_build): nsym=256 limit=10
  q-floored histograms -- same hunted shape, less skew (flooring
  pushes toward uniform = excluded-256 regime).

CONJECTURE (evidence-backed, not proven): take-resident-256 is
unreachable through lengths()/solve(): a 256-occurrence package is
always the unique max-weight final item. Pre- and post-fix code are
therefore observationally identical on all inputs found; the fix
removes even the latent boundary difference.

## 3. correspondence argument (why cap-257 == old EXACT)

For every take item with TRUE subtree size t (multiplicity):

- t <= 256: old expand returns t (proceeds); new sub = min(t,257)
  = t <= 256 (proceeds). IDENTICAL.
- t > 256: old expand returns 0 (fail); new sub = 257 > 256
  (fail). IDENTICAL.

Propagation re-audit under cap-257 (unchanged code paths):

- sub[] ascending valid: children always smaller pool indices than
  their package (bases 0..n-1; packages built from earlier nodes).
- descent valid (i = nnode-1 .. n): cnt[l] += cnt[i], cnt[r] +=
  cnt[i] distributes each take-item's 1 count per occurrence;
  leaves accumulate exactly old's per-leaf visit totals.
- mid-loop lens>=limit fail <=> final cnt>limit (visits monotone
  per leaf; checked before narrowing to uint8). cnt[i]==0 <=> old
  lens==0. seq[j]>=nsym dead (pool r = syms[] < nsym always).
- overflow: cnt <= take * max-occurrences-per-subtree (~510*512)
  << 2^32 (uint32_t); sub capped 257 (uint16_t). No narrowing risk.
- msort == pm_sort: (weight, idx) is a strict total order
  (distinct indices) -> unique sorted permutation; any correct
  sort agrees (bottom-up mergesort, verified textbook shape).

## 4. differential corpus (in-test, deterministic, no files)

- 100000 random: 8-mode mixture (small-unif / wide+zeros /
  all-equal-ties / sparse / skewed / pow2+jitter / binary /
  two-valued), nsym 2..256 sweep + random, maxlen 5/10 alternate,
  2 xorshift seeds. FNV of each vector printed on mismatch.
- 66 crafted tie-heavy (n = 64..256 x v = 1..3 x maxlen 5/10).
- 44 crafted spike/ramp/pairs/uint32max (n = 64..256).
- 14 degenerate-nsym decline (0/1/257/300/512/1024/1025 x 5/10).
- 7 invalid-maxlen decline (0/1/4/6/9/11/16).
- 3 used0/used1 pins.
- total 100134. first-attempt take-sub buckets tracked from oracle
  expand counts (cnt==256 <=> exactly-256, since expand returns 0
  past 256 else exact).

## 5. battery gate (setdiff vs tmp/verify-p2 bases)

- FULL (seeds 0-16, e00,e01,e05,e09, tier full): 12784 cells,
  0 fail, PASS (911.5s loaded box). findings 0 rows = base 0 rows.
- hold (seeds 17-20): 3008 cells, 0 fail, PASS. findings 0 = base 0.
- fresh (seeds 21-24): 3008 cells, 5 ENC_DIFF (exit 1 = base
  behavior). findings 5 rows byte-identical to base 5 rows
  (s21 e05+e09, s22-e01-255B, s24-e09-49B+64K, same o/p lens+shas);
  all 5 vectors/ .bin byte-identical (cmp).
- rule met: 0 NEW on all three tiers.

## 6. bench (make bench BENCH_REPS=35, pinned corpus, solo)

- 3 runs (run1 concurrent with FULL discarded as loaded; run2/run3
  solo). Final = run3 vs tmp/p2-bench/bnew via cmp.py: 24/24 cells
  present, n=35/35 (zeros-dec sub-ms partial as in base).
- uplift spread: worst -16.4% (mixed-dec-L9, band 250-446 vs base
  375-433, overlapping) is a DECODER cell (code untouched) -> load
  noise by construction; run2's worst (-21% mixed-enc-L5) moved to
  -4.6% in run3. Median uplift ~0%. Perf-neutral as expected
  (2 changed immediates, no added work).

## 7. files

- port/src/lzmesh_enc.c (fix, 5+/4- with comment).
- port/tests/unit/test_pm_diff.c (new, 100134-case differential).
- this file (summary). scratch probes under tmp/p3-harden (uncommitted):
  probe*.c, prefix-run.txt, unit-*.log, full/hold/fresh logs, bench TSVs.
