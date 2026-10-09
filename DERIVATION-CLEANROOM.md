# DERIVATION-CLEANROOM.md — Clean-room RE track log

- Track: independent reverse-engineering of Apple LZMESH (`COMPRESSION_LZMESH = 0xE05`). Derive from Apple artifacts only.
- Coordinator session: pastel-lynx 01a0aac5.
- This file = this track's derivation log. Root `DERIVATION.md` is other side's, untracked, predates track, unreadable to us. See `spec-queries/Q-001-derivation-log-placement.md`.
- Append-only. One entry per session. Updated in same commit as anything it describes.

## Wall statement

- READ-ALLOW: `KICKOFF-CLEANROOM.md`; `docs/03-clean-room-re.md` (full); `docs/02-format-spec.md` lines 1–1004 ONLY (hard stop at line-1005 READ BARRIER); `docs/10-provenance-and-licensing.md` lines 133–298 ONLY (§10.2); files we create ourselves; exact `tmp/re/` paths named in brief.
- READ-FORBID (never open/grep/diff/log/show): `KICKOFF-IMPLEMENTATION.md`; `docs/00,01,04,05,06,07,08,09,11`; `docs/02` line 1005+; `docs/10` outside 133–298; `research/` `core/` `vendor/` `vectors/` `tools/` `scripts/` `tests/` `android/` `tmp2/`; `tmp/` except named paths; root `DERIVATION.md`; `.gitignore`; any git history/diff/message content (`status --short` + `diff --numstat` metadata only).
- `docs/02` Part A claims = UNPROVEN hypotheses. Never cited as evidence.
- Tag every finding: MEASURED (own bytes from Apple codec), SYMBOL (Apple binary name/addr), INFERRED (chain + strength), APPLE-DOC (Apple prose).
- WEB: `developer.apple.com` + `opensource.apple.com` ONLY. No github, no third-party LZMESH, no external-model consults on format.
- HYGIENE: decompiler/disassembler output stays in own `tmp` dir; Apple binaries never committed/copied out (sha256+size+source path only); lldb in-process own harness only; no DTrace; no Frida; no boot-args; simctl only, dedicated device, shutdown+delete at end, never Simulator.app. Ghidra 12.0.4 + ipsw 3.1.666 in Homebrew Cellar (`/opt/homebrew`), NOT on PATH — never conclude absent from PATH check.
- STYLE: terse fragments, numbers first. Scratch ONLY in assigned `tmp` dir (never `$HOME`, `/tmp`, repo root).
- Violation/contact with forbidden material = STOP + report, never continue.

## Inputs consulted (setup session, FRESH `shasum -a 256`, run 2026-09-16)

- `KICKOFF-CLEANROOM.md`: `835b70544a4b562970b31bf6695c4dc8ff95dc7cda318fb628679b1e54c2a85b`, 11107 B. Full read. Expect `835b7054…` — MATCH.
- `docs/03-clean-room-re.md`: `1976440de618d3f227323023f0f73a2f5cce19333b663335d8e5ecbfb1334aae`, 72142 B. Full read (1200 lines). Expect `1976440d…` — MATCH.
- `docs/02-format-spec.md`: `358a199935fdeb9263966d1904af06c588da7f8ddee59cb0cfee4303dccfb03c`, 78893 B. PART-A-ONLY lines 1–1004 read. Lines 1005+ never opened. Hash = whole-file `shasum` (hash only, no content viewed past barrier). Expect `358a1999…` — MATCH.
- `docs/10-provenance-and-licensing.md`: `6a9576a3b702a77c11c51ccce804469d29f7c31e483d683090e544f28037722b`, 53917 B. §10.2 lines 133–298 ONLY read. Outside never opened. Hash = whole-file `shasum` (hash only, no content viewed outside range). Expect `6a9576a3…` — MATCH.
- INPUTS EXPLICITLY NOT CONSULTED: all READ-FORBID material above. No binary touched. No web touched. No `docs/02` Part B. No `01`, `04`.

## Session entries

### Session 2026-09-16 · setup · agent session a4b2f2c2-1484-7f80-8d01-7c6b7f3b50e8

- Role: setup. Coordinator: pastel-lynx 01a0aac5.
- Did: `mkdir -p spec-queries tmp/re/stage0`. Both empty before (confirmed `ls -l`, total 0).
- Wrote: `spec-queries/Q-001-derivation-log-placement.md`; `spec-queries/Q-002-ground-truth-records.md`; `spec-queries/Q-003-stage5-handoff.md`; `DERIVATION-CLEANROOM.md` (this file). NOTHING else.
- Findings: none (setup only, no format claims).
- Outputs: 4 files created. No decompiler output. No Apple binary bytes. No commits made.

### Session 2026-09-16 · clause-writer · agent session dfc25e02-74c1-7c52-8fe4-0a541e54691a

- Role: clause writer (SOLE writer to shared files this run).
- Inputs consulted (fresh `shasum -a 256` this session, ALL MATCH setup values): `KICKOFF-CLEANROOM.md` `835b7054…` 11107 B full; `docs/03-clean-room-re.md` `1976440d…` 72142 B full (1200 lines); `docs/02-format-spec.md` `358a1999…` 78893 B pre-edit, lines 1–1004 ONLY (hash whole-file, content past 1004 never opened); `docs/10-provenance-and-licensing.md` `6a9576a3…` 53917 B §10.2 lines 133–298 ONLY; `DERIVATION-CLEANROOM.md` (own file, read + append); `spec-queries/Q-001..Q-003` (numbering continuity); `tmp/re/stage0/TARGETS.md` 9300 B full read; `nm-265.txt` 621 lines + `nm-270.txt` 654 lines (counts/needles re-verified by `grep`/`awk`); `u265.txt`/`u270.txt` (diff = +`_memcmp` only); dsc27 inspect files sizes only (91228/91215/1241/122 B, content unread); `tmp/re/blackbox/mesh_probe.c` 1093 B full read; `mesh_probe` executed (own fresh run).
- Apple artifacts measured (sha256+size+source path only, bytes never committed): 265 dylib `5b66dd28…` 961088 B arm64 IOSSIM26.5 LD1267.0; 270 extraction `010fdddf…` 6377472 B arm64 IOSSIM27.0 LD27037.1; sim DSC UUID 5827BB56-… primary 2043297792 + .01 1786019840 + .atlas 625262 + .map 458923; runtimes via simctl (27.0 24A434 UUID 41D21872-… 7.5G; 26.5 23F77 7.9G); host dyld dir 82 entries 2.3G `symbols` 0; PAC 0 both; fixups 576-line vs 0-entry+warning; usr/lib 156 vs 46 entries; no standalone 27.0 dylib.
- Inputs NOT consulted: all READ-FORBID; `docs/02` 1005+; `docs/10` outside 133–298; other `tmp/re/` stage dirs (content unread — `ls` inventory only; blackbox `.out`/`RESULTS.md` NOT read, CR-012 is own run); web untouched; no decompiler output; git history untouched (`status --short` + `diff --numstat` metadata only).
- Outputs: `docs/02-format-spec.md` S9-CR v0.1 insert (13 clauses CR-001–CR-013: 3 SYMBOL + 3 INFERRED + 7 MEASURED; 7 OPEN rows; pre-edit `358a1999…` 78893 B → post-edit `dc2c99c8…` 88485 B, +9592 B, barrier now line 1128, nothing past barrier touched); `spec-queries/Q-004..Q-010` (7 files, 974/939/980/882/917/958/988 B); this entry. No commits made (writer role). No decompiler output. No Apple bytes.
- Per-clause evidence: CR-001/002 → `nm-270.txt` greps + TARGETS.md Gate3/4; CR-003 → `nm-265.txt` greps + TARGETS.md Needles; CR-004 → 11↔11 counts both dumps; CR-005 → 0x10 spacings both dumps; CR-006 → shasum/ls/file/lipo/vtool/ipsw/simctl; CR-007 → wc/awk/t/s greps; CR-008 → size/otool-AUTH0/dyld_info; CR-009 → control/needle/PAC/U-diff greps; CR-010 → usr/lib ls + DSC ls/ipsw info + TARGETS.md tooling notes; CR-011 → host dyld ls/du; CR-012 → own `mesh_probe` exec `enc=27 dec=65536 match=1 dec_scratch=65536 enc_scratch=1388428`; CR-013 → PAC/arch/AUTH/fixup-head chain. OPEN-001–007 → Q-004–Q-010.
- Findings: Stage 0 numbers 100% re-verified (zero mismatches); S1.6/S4.2.d 64 KiB CONFIRMED by own run; zero Part A contradictions in settled scope; zero [APPLE-DOC] clauses (no primary Apple prose in allow-list — Q-009).

### Session 2026-09-16 · relocate-wave1 · agent session 01a0ab24-ea9f-74c2-b690-533eb657de49

- Role: relocate clean-room working files to sibling `/Users/mrowlinson/Projects/LZMESH-cleanroom/`.
- Inputs consulted: this brief (move list + wall); `ls` of `tmp/re/` (26 dirs + track README; zero symlinks via `find -type l` + `test -L` per entry at move time); sibling `tmp/` `ls` (11 dirs at move time, zero name collisions); `docs/02-format-spec.md` lines 995–1006 + 1120–1128 ONLY (junction windows; S9-CR body copied blind by line number 1003–1127); `spec-queries/Q-004..Q-010` (ref updates, full read, ours); sibling root `ls` (README.md + tmp/ only); this file (append).
- Inputs NOT consulted: all READ-FORBID; `docs/02` below `# PART B` barrier (line 1128 pre-move → 1004 post-move; below-barrier bytes preserved by line-addressed edit, never viewed); `tmp/` outside `tmp/re/` (contents unread — `ls` names only for delete-verify); git history/diff/message content (`status --short` + `diff --numstat` metadata only).
- Did: moved 26 dirs `tmp/re/` → sibling `tmp/` (appledocs arm64e blackbox buildhunt capstone container corpus dynamic entropy entropycurves gapfollowup ghidraprep hostcache levels lldbdesk lzfse resbits sigscripts simdev specinv specinv2 stage0 stage5diff tables tabverify toolchain; 0 renames, 0 symlinks); `README-CLEANROOM-TRACK.md` excepted per brief, deleted with `tmp/re/`; extracted S9-CR lines 1003–1127 → sibling `FINDINGS.md` (7-line header + 125 verbatim, `tmp/re/`→`tmp/` rewrite, 132 lines total); removed section from `docs/02`, restored `---` + `# PART B` junction + 1 HTML-comment relocation line (pre `dc2c99c8…` 1545 lines 88485 B → post `4b4294b6…` 1421 lines 78977 B, delta −124 lines); updated Q-004..Q-010 `S9-CR`→`LZMESH-cleanroom/FINDINGS.md` (9 refs, 0 remaining); `rm -rf tmp/re/`; LZMESH commit follows (docs/02, Q-001..Q-010, git rm this file; mailbox stays).
- Findings: none (relocation only, no format claims).
- Outputs: sibling `FINDINGS.md` + `tmp/` (+26 = 37 entries) + this file; LZMESH `tmp/re/` deleted. Mailbox + spec junction stay in LZMESH.

### Session 2026-09-17 · derivation-draft-R2 · agent session 01a0ab194-2bf7-7ed0-8bdd-1a9c71100122 (applied by parent)

- Role: derivation drafter (draft ONLY; parent applied to canonical; R2 refresh SUPERSEDES R1 staged draft, R1 retained at `tmp/derivation/DERIVATION-UPDATE-DRAFT.md` for audit).
- Inputs consulted (READ-ALLOW ONLY): R1 draft full; `tmp/derivation/XWALL-R38.md` full (R35-adopt + P40-fold + #105-fold-NEW); `tmp/verdict/VERDICT-R33.md` full (11/21 HOLD 0-flips); `tmp/fullbattery/round9/RUN-R9.md` full (ENC 16782 DEC-0); `MAILBOX.md` L4830-4874 tail ONLY (#105 zone).
- Inputs NOT consulted: everything else (no main LZMESH, no evidence, no .c, no other tmp/, no git history).
- Record (all second-hand-HERE except #105-tail re-read DIRECT; grades ride next verdict/audit): R33-single (11/21-HOLD 0-flips, N4 EVIDENCE-CLOSED formal-rides-next, twin INDEPENDENTLY-REPRODUCED G5-narrowed-further, G21-79/20-carried) + R9-single (59752 + 180adv, ENC-16782 narrowed-12, DEC-0, COMP-+12, VALID) + #105-single (78/20-ACTUAL, R65/A68/P27/P28-intake) + P40-NO + P39-NO-reconverged (asserted chain P25→P28) + Q20-bytes-join (inform-not-close) + Q19-apply-REFUSED-STAGED + DEFS-P28-FILED + C1-C13-converged + H38..H60-staged/H61-owns-next + M38-owed + cleanup-HELD-0 + wrap-owed.
- Never-cites (until lift): no R33-double, no #105-re-fold, no R9-beyond-census, no B3/B6/N1/N2/N3-graded, no N4-formal-CLOSED-bare, no v3-adopted-formal, no twin-blessed, no wrap-proven, no Q20-row-selected.
- Source: `tmp/derivation/DERIVATION-UPDATE-DRAFT-R2.md` (full draft + gaps §3).

## Wall incident 2026-09-16 (contained)

1. Writer-split-E1 agent (01a0ab50) ran an uncapped search on LZMESH docs/02-format-spec.md; tool returned ~25 lines from 1055-1362 (Part B region, per agent's own stop report). Agent stopped immediately, wrote zero bytes, retired permanently, never to touch track files.
2. Coordinator's own boundary-locating search for 'Part B' at track start returned line 1016 (below the then-barrier at 1005): one procedural sentence, no identifiers/constants/structure; influenced no derivation.
3. Containment: all track artifacts (FINDINGS CR-001..062, all probe artifacts, both LZMESH commits) predate the E1 contact and derive from independent measurements; no post-contact bytes exist in any track output.
4. Mechanism fix: search/grep banned on docs/02 + docs/10 in all subsequent briefs; read_file explicit windows only (02 lines 1-1001, 10 lines 133-298).
5. Xbuild agent (01a0ac2c) window-arithmetic fault: second docs/10 window (offset 233 limit 100) ran 233-332, 34 lines past barrier (299-332: 10.3 header + Apple-doc verbatim block). Agent self-stopped immediately, wrote zero bytes (tmp/xbuild/ never created), retired permanently. No post-contact bytes in any track output; all FINDINGS + probe artifacts predate contact and derive from independent measurements.
6. Mechanism fix 2: all subsequent briefs pin docs/10 as ONE window (offset 133 limit 166 = lines 133-298 exactly) with explicit no-second-window rule.

## Verified-law compile 2026-09-22 (track close-out; append-only; 34 filings)

- Inputs: `tmp/hints/` 10 memos (H3-R1..R3, H4-R1..R3, H5-R1..R3, H9-R1) + `tmp/encbatt/` R117..R123 + H4-ANALYSIS-R1..R7 + `tmp/u34/` Q234..Q239-LOG + `tmp/publish/` R806 + R808 + `tmp/verdict/` R523 + R524. Grounding extras: `tmp/lanes/LANES.md` L122-125 (B1), `tmp/encbatt/P0-RECORD-R1.md` (P0). All black-box vs oracle. No probes this pass. Verdict: V523 6 flips + V524 3 flips = open 0. Publish: P808 GO (4 MET + B5 dropped).
- Old caps DEAD everywhere below: 3/4096, 4/65536, 5/1048576-as-stated. Superseded by measured loss-D.

### H3 enc-take laws (e00 COMP-bed; oracle==port join 88/88)

- H3.1: L4 loss-D = 16390 exact. 16389 last-t1 (d129), 16390 first-t0, adjacent. 13 bisect + 2 anchors, 15/15 agree, tags 01/01. Cap 65536 dead (loss at 0.25x). Proof: `tmp/encbatt/ENC-BATT-R123.md` sec1; trail R122-sec2 (last-t1 16384, first-t0 24576); flipped `tmp/verdict/VERDICT-R523.md` item 1.
- H3.2: L5 loss-D = 16389 exact. 16388 last-t1, 16389 first-t0, adjacent. 14 bisect + 2 anchors, 16/16 agree. Cap 1048576 dead (loss at 0.016x). Proof: R123-sec2; trail R122-sec2 ((16384,32768]); flipped V523 item 2.
- H3.3: L3 loss-D in (16000,20000]. 16000 t1 d4 both; 20000 t0 d-2 both; 6/6 agree. Conservative cap 20000. Cap 4096 dead (takes thru 16000, 17/17 4K..16K). Optional 12-probe bisect nonblocking. Proof: R123-sec3; sweep R122-sec1; flipped V523 item 3.
- H3.4: Join oracle==port: tags+taken agree 88/88 cumulative (15 R121 + 34 R122 + 39 R123). Zero RAW tags. Zero splits. C4 ctrl-coding div (L5 ctrl o+32/+128 vs p+1/+2, test arms byte-identical) verdict-neutral -> ACCEPT, 0 taken flips. Proof: `tmp/encbatt/ENC-BATT-R121.md` sec2 (15/15 agree, 10/15 cap), R123-sec4/sec5; flipped V523 item 4. Harness: `tmp/encbatt/q7join.py`.
- H3.5: Floor e01/e05/e09 COMP-bed ~12. L6 59/59 d0, L8 63/63 d0, L12 60/62 d2 t1, L20 68/70 d2 t1. L12 D128 d2 = sanity gate (L6 sanity void). e00 floor lower: L3/4/5 D128 take (d5/d4/d1) vs COMP d0. Proof: R118-sec1 (P4), R119-sec1 (Q1/Q3/Q4/Q6); memo `tmp/hints/HINT-H3-R3.md` facts 2+4.
- H3.6: Repeat-distance cheaper. e05/e09 2-cycle < 6-cycle d3 (n1584, PASS). e01 needs Q8a bed (64-step seed8, d5) or Q8c bed (3v6, d4); old 48x24B 2v6 bed e01 d-2 stays e05/e09-only. Proof: R118-sec3 (FIX-P2), R119-sec1 (Q8), R120-sec1 (Q8a/Q8c/Q8old).
- H3.7: Prefix multi-block: mixed X (1Mpat+1Mrand+1Mpat) -> 19 blocks, prelen 1048762. Y-append: lcp 1048433, leading 8/8 blocks identical, tail 328B rewritten, full-lead cover 0. Scope = all-but-last-block. Pure-pattern X never splits (COMP48 at 8M); random X single STORED. Proof: R120-sec1/sec2 (Q9a/Q9b); memo H3-R3 Q9.
- H3.8: Small vectors + determinism, all 4 selectors. n0 -> FF (1B). n>=1 -> >=6 out (n+6 stored). 1xA -> `00 01 00 00 00 41 FF`. AB -> `00 02 00 00 00 41 42 FF`. A21 stored 27B tag00; A22 compressed 23B tag01. 265+ -> 27B flat (e01/e05/e09, to 1M). e00 1M -> 491B compressed (not store-only). Determinism 10/10 byte-ident, scratch == no-scratch. Proof: R117-sec3 (F5/F6/F7/F8); Q234 MIN/CROSS; Q235/Q236 MIN/CROSS recheck x4.

### H4 exclusion laws (e01 exclusion vs e00 noisy; H4-CLOSED)

- H4.1: Exclusion signature = size-equals + tag-equals + ndiff==1 (1-literal diff). 8/8 cells (zero-bed 36/36 + bed16 57/57, 01/01). Old byte-equals void. Proof: H4-ANALYSIS-R2 sec1 (P1 CONF); memo `tmp/hints/HINT-H4-R3.md` fact 1.
- H4.2: e01 exclusion: 0 flips everywhere. Triples 9/9 [0,0,0]. Sweeps +-64 387/387 COMP-CLEAN, 0 flips all caps. 0/3 within-cap takes + 0/3 cross-cap flips; closure blocked. Proof: R2-sec1 (P2/P3/P4), R3-sec1 (W3/W4).
- H4.3: Floor-12 e01 zero-bed. L6 42/42 d0, L8 46/46 d0, L12 48/50 d2, L20 56/58 d2, tags 01/01. Old floor-6 void. L12+ sanity gates all cap reads. Proof: R3-sec1 (W1); R2-sec3 (pos-fresh L6 0/6, L8 0/4).
- H4.4: Repeat emitter (not cap). Repeat-L3 5/5 d2 cap-indifferent: D100 35/37, D4000 43/45, D4096/4097 44/46, D5000 44/46. Take CONFIRMED: test->lastkill +2B = tc+1 litc+1 dc+0. Extra-copy FALSIFIED: kill-value 4/4 invariant. Chain-break: tail-kill d2 vs early-kill d10/11; first==mid 5/5. Gap-distance falsified (d_mid flat 11/11/10/10/10). Proof: R4-sec1; R2-sec3 (5/5 emitter); R3-sec2 (fact4 exact).
- H4.5: E00 split. e00 noisy marginal vs e01 exclusion, same inputs (L3 D4000: e00 549/550 d1 take vs e01 36/36 d0). L3 triple 3/3 takes both sides. Sweeps: L3 87/129 takes 32 flips; L4 4/129 takes 8 flips + negs exactly take+4; L5 28/129 takes 24 flips + 26 negs. Cap-signal FALSIFIED all L (runs span cap, e.g. L3 run (4095,4098)). E00 d0 = tie (nd>0, e.g. nd16; litc==len dc0). Tier curve L3 rate 0.55 -> 0.0025 (D100..D1M, monotone, sharp after 32K + 512K). Proof: R3-sec1 (W2/W6), R4-sec2.
- H4.6: F5 decode-side. 30/30 decode-ok + rt-ok (R4 20/20 + R3 10/10). Encode-side-only CONFIRMED (decoder refuses 0/20; same decoder roundtrips test + all kills). L3 token isolated (H4.4 deltas). Proof: R4-sec1 (F5 lines).
- H4.7: Kill-offset closed. H1 sticky-nearest+fallback FALSIFIED: double-kill d0 6->5, d1 5->3, d2 11->2 vs H1 ~11. H2 threshold L6|L7: L5 d15 + L6 d17 spike (dc1->2, m4 1->0) vs L7/L8 d4 cheap (dc1->1, m4=1); nonmonotonic (d17 > d15 then collapse d4). Formula open, nonblocking. Proof: R6-sec1/sec2, R7-sec1/sec2.
- H4.8: Hunt filter vacuous-consistent. L3/L5 D2097152 taken0; taken0 everywhere fresh so skip-rule untestable as gate. Proof: R1-sec2 (F3), R2-sec2 (F3).
- H4 close: promotion list FINAL 6 items (EXCLUSION-SIGNATURE, FLOOR-12, REPEAT-EMITTER, E00-SPLIT, F5, CAP-SIGNAL-FALSIFIED). Proof: R5-sec3; carried R6-sec3, R7-sec3; flipped V523 item 5.

### H5 decoder-acceptance laws (container shared all-4; E00-tested)

- H5.1: Stored forge works. 00 + 4B LE size + raw bytes, repeat, end FF. P1 AB ret2 `4142` x4 algs. P2 2blk ret4 concat. P4 100k A 1blk ret100000 match. Proof: Q234-LOG P1/P2/P4; Q235 P1-P4 recheck; Q236 P1/P2/P4.
- H5.2: Empty FF -> ret0 (0 bytes). Missing-FF -> ret0. Size query or known size tells apart from failure. Proof: Q234 RECIPE/EMPTY/NOF; Q235 carry; Q236 carry.
- H5.3: Trailing IGNORED 6/6: 1x00, 2x00, FF, DEADBEEF, 16B 00..0F, 00FF00 -> all ret2. TRAIL-MULTI CLOSED. Old trailing->failure void. Proof: Q234 TRAIL (FAIL vs R1), Q235 TRAIL 6/6.
- H5.4: Tags: 00 stored, 01 compressed. 02/7F/FE -> ret0 x3. Proof: Q234 TAG; Q235 TAG; Q236 TAG.
- H5.5: Truncation always fails. 8/8 stored cuts + 29/29 compressed cuts (E05 ABx500 len29). Proof: Q234 TRUNC-P1/TRUNC-C; Q235 recheck.
- H5.6: Short buffer truncates silently. cap C < true -> exactly C back, correct prefix, no error. Clamp P4cap50k -> ret50000. Proof: Q234 SHORT/CLAMP; Q235; Q236 SHORT/CLAMP.
- H5.7: Stored flip tolerated: payload bit -> ret2 `4042` wrong-content same-count. Compressed flip = pos-map (uniform-refusal void): 29B sweep 23 refuse / 2 same / 4 diff. Proof: Q234 FLIP-P1/FLIP-C; Q235 FLIP-C sweep.
- H5.8: Big-CB token layout PARTIAL (E05 ABx500 len29, ds1000 bo18 fo18 modes520 tc1 lc5 litc2 dc1). Lit exactly 2B (bytes 9-10: 10/10 diff-same-count, neighbors 8/11 8/8 refuse). Spare 2B ignored (15-16: 6/6 same any-value). Mode byte17 1 active bit (01->00 diff A+Bx999; 7/7 single-bit + 4/4 2-bit refuse). p14 subfield-same (03->04/05/06/07/08/10/40/7F/80/FF 10/10 same; 00/01/02 refuse). Core 11-12 hard-refuse (6/6 each adj-value). p13 1-same (c5->ff). Core 11-13 undecoded. 0 forged tokens. Proof: Q235 token-layout (LIT/CORE/SPARE/MODE/DS); Q236 T1-T4.
- H5.9: DS rule: trunc N<=orig (744/500/999 -> ret N trunc-ok), refuse past orig (1001 -> ret0). INFLATE CLOSED. Small-C N=40: trunc window 15..40, floor D15 exact (ds01-14 refuse x14 vs ds15-20 trunc), DS41 refuse. Proof: Q236 T6; Q238 N (DS20/39 trunc, DS41/DS1 refuse); Q239 D (1..20 full).
- H5.10: D1-order. S+C refused: 0/10 (N1/N2/N3/N4 + 2blk + adj + DS-variant) + 0/4 recheck. C+C 0/2 (+ T5 0/5 + E5 N=40 0/1). C+S accepts 2/2 (ret1002 head=orig tail4344; N=40 ret42). Second-C closed payload-independent (alone 4/4 OK -> chained 0/4; offsets 0/5: bo46/fo46, ds2000/ds2). Absolute-offset CLOSED. Old plant-N-via-stored void. Proof: Q235 DISTANCES (D1/D1-ADJ/C+C/C+S); Q236 D1/T5; Q237 B/D; Q238 E3-E5.
- H5.11: S-empty refuses ABSOLUTE (alone + chain ret0). len>=1 accepts (1B ret1001, 2+2 ret1004, 256B ret1256). Tails extensible (S+S, S256). First-mut chains: C+S survives 6/6 (spare/mode/p14/lit/ds500/ds744; N=500/744/1000; mode-forged head preserved). Leading stored poisons (S+C+S ret0). Proof: Q237 A/C/E1; Q238 T/SE.
- H5.12: Small-C N=40 map (E05 ABx20 len25, ds40 bo14 fo14 modes584 tc1 lc1 litc2 dc1, ret40). Layout lit2(9-10)+core2(11-12)+mode1(13), NO spare (5 payload bytes vs 9 big). Lit 6/6 diff40 (41->42 = all-42 Bx40). P11 3/3 refuse. P12 00-04 refuse x5 / 05-80 same x8 (05 base + 06/07/08/10/40/7f/80) / ff refuse (differs big p14 ff-same). Mode13 01->00 diff40 A+Bx39; 02/FF refuse. M14 bits 3+6 only (00/08/40/48 same; 01/02/04/10/20/80/ff refuse). M15 zero+base+high-nibble (00/02/10/20/40/80 same; 01/04/08/ff refuse). Modes-zero tolerated both bytes. P12 full-256 + modes joint-2B owed, nonblocking. Proof: Q238 K; Q239 P/M.
- H5.13: Selectors: 4 only E00/E01/E05/E09. E02/E03 -> None (encode fails). Decode cap 2^31 -> ret0. Input-side 2GiB owed (needs 2GB input). Proof: Q234 ENCSEL/CAP2G; Q235/Q236 carry.
- H5.14: Min cost + cross, x4 algs. n0 -> (1,`ff`). n1 -> len7. A21 -> (27,tag00). A22 -> (23,tag01). Proof: Q234 MIN/CROSS; Q235/Q236 carry.
- H5.15 (OWED, not law): true forged match token (0 forged all passes). P5/P6/D2-D4/V5-V7: distance window 1..written, xblock backref, parse-flip N->N+1. Redirect: single-block carve at N=40 window (Q237-end; Q238/Q239 mapped but 0 forged).
- H5 close: FIX5 (second-C) + FIX6 (S-empty) + FIX7 (tail-extend + N-window) + small-C layout + SE-absolute + ds-floor-D15 + P12/modes maps. Proof: Q237-end FIX5/6/7; Q238-end; Q239-end; flipped V523 item 6.

### B1 + P0 + H9 (publish gates)

- B1.1: Footer/modes CLOSED. 14 mode combos observed ((2,2,2,2)x54 most common; all-RAW (0,0,0,0)x13). Byte-region consumption exact 120/120 blocks. Modes in {0,1,2} only; reserved 3-7 never emitted. S3.2.c one-way: HUFFMAN->non-empty 115/115, no HUFFMAN+EMPTY. Gate = footer/modes-exact + modes-0/1/2-only + S3.2.c-one-way. Proof: `tmp/lanes/LANES.md` L122-125 MEASURED; gate `tmp/publish/PUBLISH-R806.md` (B1 MET-HERE, 1 flip).
- B5: DROPPED vestigial, not gating, 0 owed. 0 clause + 0 consumer + 0 measurement basis (lanes 0, LANES 0-true, hints 0/10 true, grade-chain 0 grade-hits, encbatt self-only; B5-MEASURE-R1 0 cells). Proof: `tmp/publish/PUBLISH-R808.md` (B5-DROP-HERE, GO).
- P0.1: GREEN smoke e05. 1232 cells, 0 fail, PASS, 57.2s, seeds 77 (0..76), tier smoke. Per-cell ENC oracle-vs-port byte-ident + DEC_A + DEC_P + ROUND + DETERM: 1232/1232 IDENTICAL, 0 ENC_DIFF/DEC_DIFF/CROSS/ROUNDTRIP/NONDET/CRASH/TIMEOUT. Scope = smoke-only (full tier, e00/e01/e09, seeds 77+ out of scope). Proof: `tmp/encbatt/P0-RECORD-R1.md` + `tmp/encbatt/p0r1out/summary.json`; carried R808 (P0 MET-carry).
- H9: ACCEPT-CLOSED (F1/F2/NIT). Bookkeeping, not codec: 0 oracle observables, 0 testable facts (HINT-H9-R1 why-not). Frozen Sep17/Sep21 history; R744-746 grep 0 hits; NIT 129th sighting, sub-threshold-never-fault. Owner proceed-on-recommendations; veto open. Proof: `tmp/verdict/VERDICT-R524.md` (3 flips, open 0).
- Status: codec laws H3/H4/H5 flipped V523; H9 closed V524; publish GO P808 (H3/H2/B1/P0 MET + B5 dropped). Spec-section draft: `tmp/SPEC-S9CR-DRAFT.md` (this pass, port-track handoff).

## Verified-law addendum 2026-09-22 (Q240 + Q241 + R124; 3 filings; append-only)

- Inputs: `tmp/u34/U34-Q240-LOG.md` (47 ops: 13 enc + 34 dec; 30 ACCEPT / 4 REFUSE / 0 FORGED) + `tmp/u34/U34-Q241-LOG.md` (191 ops: 5 enc + 186 dec; 114 ACCEPT / 72 REFUSE / 1 FORGED) + `tmp/encbatt/ENC-BATT-R124.md` (14/14 + Q9c; S01/S03 CLOSED). All black-box vs oracle. oq.py heredoc-import ONLY. No probes this pass.

### Q240 laws (H5-R4 forge-hunt; P12 + dist40-enc + P11-hard + modes-joint)

- H5-R4a: P12 = ds-35 exact. 40->05, 42->07, 60->19, 200->a5. = match_len-33. Encoder sets, decoder ignores. Proof: Q240 F1/F2/F6/F9/F10 (F 12/12).
- H5-R4b: Ax-last = ds-10 (dc0). 40->1e, 41->1f. Proof: Q240 F1/F11.
- H5-R4c: dist40-enc EXISTS. F7 = R40x2 len69 ds80 bo53 fo58 litc40 dc1, pay 40-lits + ff2407170501000008. N=40 window. Threshold: len20 stored (F8) vs len40 compressed (F7). 2B-repeat insufficient (F4/F5/D40/D41 stored). Proof: Q240 F7/F8.
- H5-R4d: P11-hard CLOSED (joint). 7e+06 / 80+06 0/2 refuse. P12 cannot rescue. Proof: Q240 G3.
- H5-R4e: modes-joint-zero same. M14xM15 (00,00)/(08,10)/(40,20)/(00,02) 4/4 same40 head41424142. P12xmode00 5/5 diff40 preserved (06/08/10/40/80). Modes ignored. Proof: Q240 G1/G2.
- H5-R4f: mode02 window-independent. ds20+m02 ret0 refuse. Token window = litc2, not ds. Not movable N+1. Proof: Q240 H1.
- H5-R4g: trunc-C chains. ds15+m00 ret15 trunc. ds20+S_AB ret22. c40+S_ABx20 ret80. C+S extensible 40B. Proof: Q240 H2/I2/I3.

### Q241 laws (F7-carve; tail-zones + D-FULL + N/N+1 + P11)

- H5-R4h: F7-tail zones (off49-57 = ff 24 07 17 05 01 00 00 08). 49 HARD (0/2). 50 HARD (0/4). 51 hard+1 (1/4, 08 SAME). 52 DIST (2/4). 53 DIST-SUB (2/4). 54 spare-tolerant (2/3). 55-56 IGNORED (2/2 SAME each). 57 hard+1 (1/4, 09 SAME). 12/29. Law: 49-51 structural. 52 dist. 53 dist-sub. 54 spare. 55-56 ignored. 57 structural. Proof: Q241 T.
- H5-R4i: D-FULL d1-d40 ALL forged. off52 map (53=05 base): 00-07->d1-d8 (lin+1). 08-0F->d17-d24 (+9). 10-17->d33-d40 (+17). 24/24 accept, div40, LZ-copy verified. off52 18-1f: 0/8 refuse. N+1 wall. off53 low-2b (52=17): xx00->d32 8/8 DIFF. xx01->d40 SAME 8/8. xx10/xx11->REFUSE 16/16. Period-4 exact over 00-1f. Gap1: 52=08..0F+53=00 -> d9-d16 8/8 (-8). Gap2: 52=10..17+53=00 -> d25-d32 8/8 (-8). Small: 52=00..07+53=00 -> d1-d8 same 8/8 (53 ignored low-range). dist3+ CLOSED (d3 = 52=02). 40/40 + gaps 24/24. Proof: Q241 D.
- H5-R4j: N/N+1 pair. 52=17/53=05 ret80 d40 decodes. 52=18/53=05 ret0 refuses. 1B +1. Same token. FIRST FORGED. Second: 52=17/53=00 d32 decodes, 52=18/53=00 refuses. Confirms wall (not d33). D2 CLOSED. Proof: Q241 D pair.
- H5-R4k: P11 = 7f exact. HARD CLOSED all contexts. Alone 0/8 (7e/80/7d/81/7b/77/6f/3f). +mode00 0/2. +p12=06 0/2. +ds15 0/2. +M1400 0/2. 0/16 total. Adjacent +-1/+-2 + bit-drops all refuse. Proof: Q241 P.
- H5-R4l (OWED carry): P12 full-256 owed. Modes larger-joint owed. Match-len byte hunt owed (len40 fixed here). Nonblocking (D-FULL done). D3 xblock + V5-V7 open on forged basis. Proof: Q241 owed.

### R124 laws (H3 S01/S03 CLOSE; C1-exact + Q9c-2sym)

- H3-C1: L3 loss-D = 17050 exact. 17049 last-t1. 17050 first-t0. Adjacent. 12 bisect + 2 anchors, 14/14 agree unique (16/16 with adj-rehit). Tags 01/01 all. D20000 1B/arm div verdict-neutral. Cap4096 dead (4.16x under). Bracket (16000,20000] dead. Supersedes CR-H3-03 conservative cap 20000. Join cumulative 102/102 (88 R121-R123 + 14 R124). Zero RAW tags. Zero splits. Proof: `tmp/encbatt/ENC-BATT-R124.md` sec1 trail (18000 t0 d-2; 17000 t1 d1; 17500/17250/17125/17062 t0 d0; 17031/17046 t1 d1; 17054/17050 t0 d0; 17048/17049 t1 d1); anchors 16000 t1 d4 / 20000 t0 d-2. S01 CLOSED. No further probes.
- H3-Q9c: 2-symbol 2M bed MULTI=1. X = Random(11).choice(b"AB") 2M (countA1048493 countB1048659, head BBBAABAABBAAABBA). Y = Random(9).randbytes 65536. e01. 2097152 X. 370591 prelen. 436132 fulllen. 357487 lcp. 370590 lead. 0 covers. 30 pre-data-blocks. 32 full-data-blocks. Pre first-8 COMP identical full first-8 (all COMP, ds/bo/fo match). lcp = last-COMP-off+1 (tag same, rest diverges). Tail-rewrite 13104B = full last block. Full-tail STORED370729/62449 + STORED433178/2953 + END. Single-alphabet multi-block CONFIRMED. Full-lead cover FAILS. All-but-last-block holds (29/30 preserved verbatim). Same structural tail rewrite as Q9b (there 328B, here 13104B). Oracle-only (no port arm, per R120-D3 spec). Proof: R124 sec2; harness `tmp/encbatt/r120h3.py` verbatim. S03 CLOSED. D1 scoping stands.

## Verified-law addendum 2026-09-22 (Q242 + Q243 + Q244-COMPLETE; 3 filings; append-only)

- Inputs: `tmp/u34/U34-Q242-LOG.md` (148 ops: 3 enc + 145 dec; 46 ACCEPT / 99 REFUSE / 0 FORGED) + `tmp/u34/U34-Q243-LOG.md` (729 ops: 22 enc + 707 dec; 345 ACCEPT / 362 REFUSE / 0 FORGED) + `tmp/u34/U34-Q244-LOG.md` (COMPLETE compile Q234..Q243, 1661 ops: 129 enc + 1532 dec; 0 new probes) + `tmp/SKIPPED.md` (S02 CLOSED, S07 owed). All black-box vs oracle. oq.py heredoc-import ONLY. No probes this pass.

### Q242 laws (LEN-IMPLICIT; D59 floor; D3-d4-len40; V5/V7-HOLD)

- H5-R4m: LEN-IMPLICIT. 29/87 same. 0 DIFF-len. L49 0/8 HARD. L50 0/11 HARD. L51 15/24 threshold-same (07..fe same 15/15; <07 refuse 8/8; ff refuse). L57 9/21 narrow window (08..1f same 9/9; <08 refuse 4/4; >=20 refuse 8/8). L54 8/18 bit-map spare (single-bit-or-zero same 8/8; multi-bit refuse 10/10). Footer 2/20 (modes-zero same; tc/lc/litc/dc HARD). 0 bytes give ret-change DIFF. len40 NOT separately encoded. Proof: Q242 L.
- H5-R4n: D59 floor exact. ds15..58 refuse 16/16. ds59 ret59 trunc. ds60 ret60. ds79 ret79. ds80 ret80 SAME. ds81/100 refuse. Window 59..80. 58->59 exact flip. Past-orig refuse. ds42 (len2 hypoth) refuses. Proof: Q242 S.
- H5-R4o: D3-d4-len40 forged. 52=03 ds80 ret80 head0001020304050607 mid262724252627 tail2425262724252627. LZ-copy d4 verified (24252627 repeat). d4-len2/d40-len2/d3-len2/d1-len2/d4-len1/d4-len3 0/6 refuse. ABCD-lits + ds42 0/2 refuse. Dist-forge OK, len-forge NO. Proof: Q242 D.
- H5-R4p: V5/V7-HOLD + V6-PARTIAL. N40 pair 52=17/53=05 ret80 vs 52=18/53=05 ret0. N32 pair 52=17/53=00 ret80 vs 52=18/53=00 ret0. V5 (N-ok/N+1-refuse) HOLD. V7 (same-token flip) HOLD. X-base/X-d4 C+S ret82 chains. X-d4-len2/X-d40-len2 ret0. V6 len40 chains OK, len2 owed-here. Proof: Q242 V.

### Q243 laws (len2-DISPROOF; NO-LEN-FIELD; floors D59/D15)

- H5-R4q: NO-LEN-FIELD. 394 unique muts (+109 dup). ret in {0,80}/{0,40} only. 0 T. 0 X. F7 TAIL 163 (S59 D18 R86; D only p52/p53 dist). F7 BODY 109 (S2 D79 R28; D = 40 lits x2 copies; S2 = modes->00). c40 PAY 92 (S12 D39 R41; p11/p12 hard). c40 BODY 30 (S2 R28). No byte sets len. len = ds-litc implicit. Proof: Q243 C.
- H5-R4r: floors EXACT. F7 ds 0..100: accepts exactly 59..80 (21T+1S, 79R). c40 ds 0..41: accepts exactly 15..40 (25T+1S, 16R). 46 T all exact orig prefixes. X=0. Min implicit len: F7 59-40=19. c40 15-2=13. len2 unreachable both bases. ds41/42/43 refuse (F7). ds3/ds4 refuse (c40). Proof: Q243 S.
- H5-R4s: joints dead 0/55. ds41/42/43 x tail-pos 0/48. litc+ds 0/7 (litc hard). Encoder 21 agree: R40+k threshold 21..30 (k2/3/4/5/8/10/15/20 stored; k30/k40 compressed). small-C 9 no short-match basis (R8x2/R16x2/R40+2/+5/+10/+20 stored). Encoder never emits short match. Proof: Q243 J.
- H5-R4t: len2-DISPROOF FINAL. 729 ops (22 enc + 707 dec). 345/362/0. 707 dec X=0 (no non-prefix, no inflate). xblock len2 dead: first block must decode; f7ds42+S ret0; c40ds20+S ret22 trunc-chains; f7d4+S ret82 re-verify. V6-len2 CLOSED. Chain: no len field (394) + floors forbid short ds (143) + joints dead (55) + encoder agrees (21). len2 UNFORGEABLE in carve space. S02 CLOSED. Proof: Q243 verdict/X; `tmp/SKIPPED.md` S02.

### Q244-COMPLETE (H5 verify/forge wave Q234..Q243 compile; 0 new probes)

- 10 logs. 1661 ops (129 enc + 1532 dec). 1 FORGED (N/N+1). D2-CLOSED. LEN2-CLOSED. V8-owed-only. Chain: Q234 recipe/accept -> Q235 pos-map/D1-order -> Q236 confine/inflate -> Q237 second-C-closed -> Q238 small-C -> Q239 floors/maps -> Q240 dist40-enc -> Q241 D-FULL/N-N+1 -> Q242 LEN-IMPLICIT -> Q243 len2-DISPROOF.
- Recipe/accept, pos-map, token big/small, D1-order, D-FULL, LEN-IMPLICIT, len2-DISPROOF compiled. P11-hard (Q241 0/16 + Q240 0/2). V1-V4 PASS. V5/V7 HOLD (Q242 N40+N32 re-verify). V6 len40-OK (ret82) / len2-CLOSED. FIX1-7 all addressed (oracle law, no code fix).
- R4 flip set: p14-subfield + inflate-rule + small-C layout + SE-absolute + ds-floors D15/D59 + P12/modes maps + P12-law + dist40-enc + P11-hard + modes-joint + F7-tail + D-FULL + N/N+1 + LEN-IMPLICIT/NO-LEN-FIELD + LEN2-CLOSED + V5/V7-hold. Proof: `tmp/u34/U34-Q244-LOG.md` ONLY.
- S07 map status: P12 full-256 14/256 probed (Q239: 00-04 refuse / 05-80 same / ff refuse). Modes joint single-byte only (Q239 M14 bits 3+6; M15 zero+base+high-nibble; Q240 joint-zero 4/4). OWED, nonblocking. Reopen: full token-field maps. Src: `tmp/SKIPPED.md` S07; Q239 P/M; Q240 G.

## Verified-law addendum 2026-09-22 (Q245; S08-CLOSED + S07-CLOSED; append-only)

- Inputs: `tmp/u34/U34-Q245-LOG.md` (10 ops: 9 enc + 1 dec; 5 ACCEPT-enc / 1 DEC-verify / 4 REJECT-giant; GREEN) + `tmp/u34/U34-Q244-LOG.md` (COMPLETE compile, re-read) + `tmp/SKIPPED.md` (S07 CLOSED refined, S08 owed-here). All black-box vs oracle. oq.py heredoc-import ONLY. No probes this pass.

### Q245 laws (S08 input-side; V8-CLOSED; MAX+2 all-4)

- H5-R4u: V8 input-cap CLOSED. G = 2147483649 = MAX+2 refused E00/E01/E05/E09, 4/4 ret0, 4-16us each (size-gated pre-scan, no input touch; vs 163ms 1G-accept). Controls 5/5: 1K E05 ret27. 1M E05 ret27. 1G E05 ret27 + DEC ret1073741824 edge-ok. 1G E00 ret447097 (~437KB blob, level-diff, << dstcap). Chain: decode-cap RAW (Q234 CAP2G) + encode MAX-exact E05 (MAX ok / MAX+1 zero / 2.2G+4.5G zero) + this all-4 MAX+2. Boundary holds both sides, every selector. H5-R4 V8 -> PASS. S08 CLOSED. No code fix (oracle law). Proof: Q245 C/G.

### S07 close (P12 refined + modes subset-AND; nonblocking owed -> law)

- H5-R4v: P12 refined rule CLOSED. 00-04 refuse / 05-fe same / ff refuse. 250 same, 6 refuse. Supersedes 14/256 partial (Q239). Src: `tmp/SKIPPED.md` S07; Q239 P; Q244.
- H5-R4w: modes subset-AND CLOSED. M14 in {00,08,40,48} AND M15 in {00,02,10,20,40,80}. 55 combos. Supersedes single-byte-only + joint-zero-4/4 partial (Q239 M/Q240 G). Src: `tmp/SKIPPED.md` S07; Q239 M; Q240 G; Q244.

### COMPLETE pointer (H5 verify/forge wave FINAL)

- COMPLETE: `tmp/u34/U34-Q244-LOG.md` (Q234..Q243 compile, 1661 ops: 129 enc + 1532 dec, 1 FORGED) + this Q245 (10 ops, V8-CLOSED). Wave total 1671 ops. 0 open probes. V1-V4 PASS. V5/V7 HOLD (N40+N32 re-verify). V6 len40-OK / len2-CLOSED. V8 PASS (Q245).
- H5-R4 flip set FINAL: p14-subfield + inflate-rule + small-C layout + SE-absolute + ds-floors D15/D59 + P12/modes maps + P12-law + dist40-enc + P11-hard + modes-joint + F7-tail + D-FULL + N/N+1 + LEN-IMPLICIT/NO-LEN-FIELD + LEN2-CLOSED + V5/V7-hold + V8-input-cap + S07-refined (P12-250/6 + modes-subset-AND). FIX1-7 addressed. No code fix (oracle law).
