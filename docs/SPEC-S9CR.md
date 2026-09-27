# SPEC-S9CR-DRAFT — S9-CR spec-section draft (port-track handoff)

- Date: 2026-09-22. Source: 41 filings (34 + U34 Q240/Q241/Q242/Q243/Q244-COMPLETE/Q245-S08-CLOSED + encbatt R124). All black-box vs oracle.
- Status: codec H3/H4/H5 flipped (V523, 6 flips). H9 accept-closed (V524, open 0). Publish GO P808 (H3/H2/B1/P0 MET + B5 dropped vestigial).
- Old caps DEAD: 3/4096, 4/65536, 5/1048576-as-stated. Superseded by CR-H3-01..03.
- Normative = SHALL. Informative = notes + owed. Port SHALL satisfy all CR clauses; owed items SHALL NOT gate port.

## CR-H3 enc-take (e00 COMP-bed, oracle==port)

- CR-H3-01: L4 take-loss distance SHALL be 16390. 16389 takes, 16390+ not. Adjacent pair. Proof: R123-sec1 (13 bisect + 2 anchors, 15/15 agree, tags 01/01).
- CR-H3-02: L5 take-loss distance SHALL be 16389. 16388 takes, 16389+ not. Adjacent pair. Proof: R123-sec2 (14 bisect + 2 anchors, 16/16 agree).
- CR-H3-03: L3 take-loss distance SHALL be 17050. 17049 takes, 17050+ not. Adjacent pair. Takes thru 16000 (17/17 4K..16K). Old (16000,20000]/cap-20000 superseded. Proof: R124-sec1 (12 bisect + 2 anchors, 14/14 agree, 16/16 with adj-rehit, tags 01/01).
- CR-H3-04: oracle vs port SHALL agree tags+taken 102/102 on H3 grids (88 R121-R123 + 14 R124). Zero RAW tags. Zero splits. L5 ctrl-coding div (test arms byte-identical; ctrl o+32/+128 vs p+1/+2) + D20000 1B/arm div are verdict-neutral ACCEPT, 0 taken flips. Proof: R121-sec2, R122-sec3, R123-sec4/sec5, R124-sec1/sec3. Harness: `tmp/encbatt/q7join.py`.
- CR-H3-05: floor e01/e05/e09 COMP-bed SHALL be ~12. L6/L8 delta 0; L12/L20 delta 2. L12 D128 delta-2 is the sanity gate (L6 sanity void). e00 floor lower: L3/4/5 D128 take (d5/d4/d1). Proof: R118-sec1 P4; R119-sec1 Q1/Q3/Q4/Q6.
- CR-H3-06: repeat-distance SHALL cost fewer bytes. e05/e09 2-cycle < 6-cycle d3 (n1584). e01 same result on Q8a bed (64-step seed8, d5) or Q8c bed (3v6, d4); old 48x24B 2v6 bed e01 d-2 stays e05/e09-only. Proof: R118-sec3, R119-sec1 Q8, R120-sec1.
- CR-H3-07: prefix property scope SHALL be all-but-last-block. Mixed X -> 19 blocks; Y-append preserves leading 8/8 blocks verbatim; tail 328B rewritten; full-lead cover fails. Pure-pattern X never splits (COMP48 at 8M); random X single STORED. Q9c 2-symbol 2M bed: MULTI=1, 0 covers, 29/30 blocks preserved verbatim, tail-rewrite 13104B (full last block), lcp357487 = last-COMP-off+1. Oracle-only. Proof: R120-sec1/sec2 Q9a/Q9b; R124-sec2 Q9c.
- CR-H3-08: small vectors + determinism SHALL hold all 4 selectors. n0 -> `FF` (1B). n>=1 -> >=6 out (n+6 stored). 1xA -> `00 01 00 00 00 41 FF`. AB -> `00 02 00 00 00 41 42 FF`. A21 stored 27B tag00; A22 compressed 23B tag01. 265+ -> 27B flat e01/e05/e09 (to 1M). e00 1M -> 491B. Determinism 10/10 byte-ident; scratch == no-scratch. Proof: R117-sec3 F5/F6/F7/F8; Q234-Q236 MIN/CROSS x4.

## CR-H4 exclusion (e01 exclusion vs e00 noisy)

- CR-H4-01: exclusion signature SHALL be size-equals + tag-equals + ndiff==1 (1-literal diff). 8/8 cells. Byte-equality void. Proof: H4R2-sec1 P1; HINT-H4-R3 fact 1.
- CR-H4-02: e01 SHALL show 0 flips. Triples 9/9 [0,0,0]. Sweeps +-64 387/387 COMP-CLEAN 0 flips all caps. 0/3 takes + 0/3 flips. Proof: H4R2-sec1, H4R3-sec1 W3/W4.
- CR-H4-03: floor-12 e01 zero-bed SHALL hold. L6 42/42 d0; L8 46/46 d0; L12 48/50 d2; L20 56/58 d2. L12+ sanity gates cap reads. Proof: H4R3-sec1 W1; H4R2-sec3.
- CR-H4-04: repeat-L3 SHALL emit 5/5 d2 cap-indifferent (D100 35/37, D4000 43/45, D4096/4097 44/46, D5000 44/46). Take = tc+1 litc+1 dc+0 (test->lastkill +2B). Extra-copy falsified (kill-value 4/4 invariant). Tail-kill d2; early-kill d10/11; first==mid 5/5. Gap-distance falsified. Proof: H4R4-sec1; H4R2-sec3; H4R3-sec2.
- CR-H4-05: e00 SHALL be noisy marginal vs e01 exclusion. Same input L3 D4000: e00 549/550 d1 take vs e01 36/36 d0. L3 triple 3/3 takes both sides. Sweeps: L3 87/129 takes 32 flips; L4 4/129 + negs at take+4; L5 28/129 + 26 negs. Cap-signal falsified all L (runs span cap). E00 d0 = tie (nd>0; litc==len dc0). Tier curve 0.55 -> 0.0025 monotone. Proof: H4R3-sec1 W2/W6; H4R4-sec2.
- CR-H4-06: decoder SHALL accept all H4 outputs (30/30 ok + rt-ok). Encode-side-only: decoder refuses 0/20; same decoder roundtrips test + all kills. L3 token isolated per CR-H4-04. Proof: H4R4-sec1 F5.
- CR-H4-07: kill-offset threshold SHALL sit at L6|L7 (D4000 seed100). L5 d15 + L6 d17 spike (dc1->2, m4 1->0); L7/L8 d4 cheap (dc1->1, m4=1). H1 sticky-nearest+fallback dead (double-kill 6->5, 5->3, 11->2 vs ~11). Formula open, nonblocking. Proof: H4R6-sec1/sec2; H4R7-sec1/sec2.
- CR-H4-08 (informative): hunt filter vacuous-consistent. L3/L5 D2097152 taken0; untestable as gate (taken0 everywhere fresh). Proof: H4R1-sec2 F3; H4R2-sec2 F3.

## CR-H5 decoder acceptance (container shared all-4)

- CR-H5-01: stored forge SHALL decode. 00 + 4B LE size + raw, repeat, end FF. AB ret2 `4142` x4; 2blk ret4 concat; 100k-A 1blk ret100000. Proof: Q234 P1/P2/P4; Q235; Q236.
- CR-H5-02: empty `FF` SHALL return 0 bytes; missing-FF SHALL fail (ret0). Size query tells apart. Proof: Q234 RECIPE/EMPTY/NOF; Q235/Q236.
- CR-H5-03: trailing bytes after FF SHALL be ignored. 6/6 (1x00, 2x00, FF, DEADBEEF, 16B, 00FF00 -> ret2). Proof: Q234 TRAIL; Q235 TRAIL.
- CR-H5-04: tag SHALL be 00 stored / 01 compressed. 02/7F/FE SHALL fail (ret0 x3). Proof: Q234-Q236 TAG.
- CR-H5-05: truncation SHALL fail at every cut. 8/8 stored + 29/29 compressed (E05 ABx500 len29). Proof: Q234-Q235 TRUNC.
- CR-H5-06: short buffer SHALL truncate silently. cap C < true -> exactly C + correct prefix, no error. Clamp 50k -> ret50000. Proof: Q234-Q236 SHORT/CLAMP.
- CR-H5-07: stored 1-bit flip SHALL be tolerated (ret2 `4042` wrong-content same-count). Compressed flip SHALL follow pos-map: 29B sweep 23 refuse / 2 same / 4 diff. Uniform-refusal void. Proof: Q234 FLIP-P1/FLIP-C; Q235 FLIP-C.
- CR-H5-08: big-CB layout (E05 ABx500 len29, ds1000 bo18 fo18 tc1 lc5 litc2 dc1) SHALL be: lit exactly 2B at bytes 9-10 (10/10 diff, neighbors 8/11 8/8 refuse); spare 2B ignored at 15-16 (6/6 same); mode byte17 1 active bit (01->00 diff A+Bx999; 7/7 single + 4/4 2-bit refuse); p14 subfield-same (10/10 same incl 05/06/07; 00/01/02 refuse); core 11-12 hard-refuse (6/6 each); p13 1-same (c5->ff). Core 11-13 undecoded. Proof: Q235 layout; Q236 T1-T4.
- CR-H5-09: DS SHALL truncate N<=orig (744/500/999 -> ret N) and refuse past orig (1001 -> ret0). Small-C N=40 trunc window SHALL be 15..40: floor D15 exact (ds01-14 refuse, ds15-20 trunc), DS41 refuse. Proof: Q236 T6; Q238 N; Q239 D.
- CR-H5-10: block order SHALL be: S+C refuses (0/10 + 0/4 recheck); C+C refuses (0/2 + T5 0/5 + N=40 E5); C+S accepts (ret1002 / ret42). Second-C closed payload-independent (alone 4/4 OK -> chained 0/4; offsets 0/5). Absolute-offset closed. Proof: Q235 DISTANCES; Q236 D1/T5; Q237 B/D; Q238 E3-E5.
- CR-H5-11: stored len0 after C SHALL refuse absolute (alone + chain ret0). len>=1 SHALL accept (1B ret1001; 2+2 ret1004; 256B ret1256). Tails extensible. C+S survives first-muts 6/6 (N=500/744/1000; mode-forged head preserved). Leading stored poisons (S+C+S ret0). Proof: Q237 A/C/E1; Q238 T/SE.
- CR-H5-12: small-C N=40 map (E05 ABx20 len25, ds40 bo14 fo14 tc1 lc1 litc2 dc1) SHALL be: lit2(9-10)+core2(11-12)+mode1(13), NO spare. Lit 6/6 diff40. P11 3/3 refuse. P12 00-04 refuse / 05-80 same / ff refuse (big p14 ff-same differs). Mode13 01->00 diff40 A+Bx39; 02/FF refuse. M14 bits 3+6 only (00/08/40/48 same; rest refuse). M15 zero+base+high-nibble (00/02/10/20/40/80 same; 01/04/08/ff refuse). Modes-zero tolerated. P12 refined CLOSED: 00-04 refuse / 05-fe same / ff refuse (250 same, 6 refuse). Modes subset-AND CLOSED: M14 in {00,08,40,48} AND M15 in {00,02,10,20,40,80} (55 combos). S07 CLOSED. Proof: Q238 K; Q239 P/M; Q244; `tmp/SKIPPED.md` S07.
- CR-H5-13: selectors SHALL be exactly E00/E01/E05/E09. E02/E03 encode SHALL fail (None). Decode cap 2^31 SHALL fail (ret0). Input-side SHALL refuse MAX+2 = 2147483649 on all 4 selectors (4/4 ret0, 4-16us size-gated; controls 1K/1M/1G E05 ret27 + 1G E00 ret447097 + 1G DEC edge-ok). V8 CLOSED. S08 CLOSED. Proof: Q234 ENCSEL/CAP2G; Q235/Q236; Q245 C/G.
- CR-H5-14: min cost SHALL be: n0 -> (1,`ff`) x4; n1 -> len7 x4; A21 -> (27,tag00) x4; A22 -> (23,tag01) x4. Proof: Q234-Q236 MIN/CROSS.
- CR-H5-15 (CLOSED: D2/D3-len40/LEN2/V5-V8/V6-len2/S07; 0 owed): forged match token SHALL decode d1..d40. D-FULL map: off52 00-07->d1-d8, 08-0F->d17-d24, 10-17->d33-d40 (53=05 base), 24/24 LZ-copy verified; off53 low-2b: xx01 base, xx00 -8 mid/hi (d9-d16/d25-d32 gaps 16/16), xx10/xx11 refuse; 52=00..07 +53=00 -> d1-d8 (53 ignored low). D2 CLOSED: N/N+1 pair 52=17/53=05 ret80 d40 vs 52=18/53=05 ret0, 1B +1 same token (second: d32 vs refuse at 53=00). dist3+ CLOSED (d3 = 52=02). F7-tail zones: 49-51 structural, 52 dist, 53 dist-sub, 54 spare, 55-56 ignored, 57 structural (12/29). F7 basis: R40x2 len69 ds80 litc40 dc1. P12 = ds-35 (05/07/19/a5); Ax-last = ds-10 (1e/1f). P11 = 7f exact HARD (0/16: alone 0/8 + 4 contexts 0/8). Modes-joint-zero same (G2 4/4); mode02 window-independent. LEN-IMPLICIT: 29/87, 0 DIFF-len (L49 0/8 + L50 0/11 HARD; L51 15/24 threshold; L57 9/21 narrow 08..1f; L54 8/18 bit-map; footer 2/20, tc/lc/litc/dc HARD). NO-LEN-FIELD: 394 muts, ret in {0,80}/{0,40} only, 0 T/X; len = ds-litc implicit. Floors exact: F7 ds 59..80 (D59), c40 ds 15..40 (D15); 46 T prefixes; X=0; min len F7 19 / c40 13. D3-d4-len40 forged ret80 LZ-verified; len-short 0/6 refuse. Joints 0/55; encoder 21 agree (R40+k threshold 21..30; small-C 9 no short basis). V5/V7 HOLD (N40 + N32 pairs re-verified). V6: len40 chains ret82 HOLD, len2 CLOSED (f7ds42+S ret0). len2-DISPROOF: 729 ops, 707 dec X=0, UNFORGEABLE in carve space. V8 CLOSED: input MAX+2 refused all 4 (Q245, S08). S07 CLOSED: P12 00-04 refuse / 05-fe same / ff refuse (250/6); modes subset-AND M14xM15 (55 combos). Wave total 1671 ops (Q244 1661 + Q245 10). 0 owed. Proof: Q240 F/G/H/I; Q241 T/D/P; Q242 L/S/D/V; Q243 C/S/J/X; Q244 COMPLETE (1661 ops, 1 FORGED); Q245 (10 ops, V8-CLOSED).

## CR-B1 footer/modes + CR-P0 record

- CR-B1-01: footer/modes SHALL be: 14 combos observed ((2,2,2,2)x54; all-RAW (0,0,0,0)x13); byte-region consumption exact 120/120; modes in {0,1,2} only (3-7 never emitted); S3.2.c one-way HUFFMAN->non-empty 115/115, no HUFFMAN+EMPTY. Gate CLOSED R806. Proof: `tmp/lanes/LANES.md` L122-125 MEASURED; `tmp/publish/PUBLISH-R806.md`.
- CR-B1-02 (informative): B5 dropped vestigial. 0 clause + 0 consumer + 0 basis. Not gating, 0 owed. Proof: `tmp/publish/PUBLISH-R808.md`.
- CR-P0-01: smoke record SHALL be GREEN: 1232 cells, 0 fail, PASS, 57.2s, seeds 77, tier smoke, e05. 1232/1232 oracle-vs-port byte-ident; 0 ENC_DIFF/DEC_DIFF/CROSS/ROUNDTRIP/NONDET/CRASH/TIMEOUT. Scope smoke-only (full tier, e00/e01/e09, seeds 77+ out of scope). Proof: `tmp/encbatt/P0-RECORD-R1.md` + `tmp/encbatt/p0r1out/summary.json`; P808 carry.
- CR-H9-01 (informative): H9 F1/F2/NIT accept-closed. Bookkeeping, 0 oracle observables. Frozen history; NIT 129th sighting sub-threshold. Proof: `tmp/verdict/VERDICT-R524.md`; HINT-H9-R1 why-not.

## Clause count

- 8 H3 + 8 H4 + 15 H5 + 2 B1 + 1 P0 + 1 H9 = 35 clauses. Normative 32 (H5.15 FULLY CLOSED: D2/D3-len40/LEN2/V5-V8/V6-len2/S07; 0 owed). Informative/owed 3 (H4.8, B1.02, H9.01).
- Verdict pointers: V523 6 flips (H3-C1/C2/C3/C4, H4, H5) + V524 3 flips (H9). Publish: P808 GO.
