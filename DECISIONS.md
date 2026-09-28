# DECISIONS.md — skeleton choices + source directives

Reads behind this file: KICKOFF-CLEANROOM.md (full, 151 lines);
03-clean-room-re.md (full, 1200 lines); 02-format-spec.md lines 1–1001;
10-provenance-and-licensing.md §10.2 window (lines 133–298, one window);
stage6scaf SCAFFOLD.md + briefs + battery/. Zero spec clauses copied
anywhere in this repo; §-pointers below are navigation, not content.

## Port directives extracted (task a)

- Language: no explicit mandate found. Nearest directives: clean-side
  example outputs are `src/lzmesh_decode.c`-style C files (10 §10.2.4
  log template); probe harness builds with `xcrun clang` (03 §7.3);
  lanes must build on stock macOS + Linux (IMPLEMENTER-LANE brief).
- Build: no Makefile/CMake directive found. Nearest: direct `clang`
  invocation (03 §7.3); lanes ship "BUILD*" notes (SCAFFOLD lane layout).
- Repo layout: full LZMESH-guide layout in 10 §10.2.3 (docs/,
  research/, spec-queries/, src/ + DERIVATION.md + CLEANROOM-LOG.md,
  port/, vendor/, vectors/, NOTICE, LICENSE, tmp/); research/ + tmp/
  in .gitignore from first commit (P10.10); no path refs from
  src/port/vectors/docs into research/ (P10.11).
- API surface: buffer API only, no streaming (02 S1.1/S5 scope);
  Apple shape = encode/decode buffer + scratch-size fns + algorithm
  selector (03 §7.3 harness, KICKOFF selector measurements);
  decode takes no effective selector (KICKOFF); level = low byte,
  one decoder for all levels (02 S5.7); public API must make
  decoded-size discovery hard to get wrong (02 S4.5.a); no-integrity
  warning in every port's API docs (02 S4.6.c); neutral identifiers,
  no upstream `mesh_*`/`MESH_*`, no Apple `Msh*`/`msh_*` (P10.7).
- License header: no directive found in allowed reads. OPEN (D10).

## Choices (task b–c)

- D1 Language = C. From: C output filenames in clean-side log template
  (10 §10.2.4), clang harness (03 §7.3), stock-toolchain portability
  (implementer brief). Mandate itself OPEN; C chosen as nearest match.
- D2 Build = single Makefile + direct `cc`. From: direct-clang
  precedent (03 §7.3), "BUILD* notes" lane shape (SCAFFOLD). No CMake
  directive exists; second build system rejected as unrequested.
- D3 Layout = task dirs (src/include/tests/bench/docs) + spec-queries/
  + docs log shells. spec-queries/ from P10.9 (only legal unblock path)
  + implementer brief queries dir. CLEANROOM-LOG.md + DERIVATION.md
  shells from 10 §10.2.3–10.2.4, relocated src/→docs/ (standalone repo:
  src/ holds code only; 10's layout targets the guide repo). No
  research/, port/, vendor/, vectors/ dirs: clean side has no path into
  research/ (10 §10.2.2); vectors arrive via battery lane per-cell.
- D4 API prefix `lzmesh_`. `LZMESH`/`COMPRESSION_LZMESH` is Apple's
  public name (02 S1.1); forbidden tokens are upstream `mesh_*` and
  Apple-private `Msh*`/`msh_*` (P10.5a/P10.7). `lzmesh_` is neither.
- D5 API shape (see include/lzmesh.h): `encode(..., level)` with
  level 0/1/5/9; `decode(...)` with no selector; `encode/decode_
  scratch_size`; `decoded_size` framing walker. From: decode-takes-
  no-selector (KICKOFF); level = low byte, one decoder (02 S5.7);
  size-walk helper duty (02 S4.5.a); Apple 6-arg shape as documented
  origin (03 §7.3). Deviation from Apple-exact shape (algo arg on both
  calls) recorded here; rationale = measured behaviour over mimicry.
  NOTE (round-4, R-E-B1 SPEC-WINS): bare-level wording SUPERSEDED —
  boundary is full-form-only {0xE00,0xE01,0xE05,0xE09}, bare rejects;
  see MERGE-M14. Header + port_cli + unit re-pinned accordingly.
- D6 Header = declarations + one-line docs only. No bodies, no
  constant/enum definitions, no internals. Constants are lane work
  from spec clauses; skeleton invents no values.
- D7 Battery = vendored COPY under tests/battery/ + VENDORED.md pin
  (file shas, date, resync rule). From: "lanes get copies, not edits"
  (SCAFFOLD), "deployed framework copy" (BATTERY-LANE brief).
  Scaffold stays frozen upstream; resync is wholesale + re-pin.
  Rejected reference (relative path into tmp/stage6scaf/): breaks the
  day the scaffold moves and violates copies-not-edits discipline.
- D8 .gitignore = research/ + tmp/ (P10.10, first-commit rule) +
  build artifacts + results/ + battery selftest droppings.
- D9 `port_cli.c` lives in src/, built by `make port_cli`. From:
  "src/ BUILD* CLI" lane deliverable (SCAFFOLD), "src/ complete port
  + port_cli binary or build recipe" (implementer brief).
- D10 License = 0BSD (DECIDED 2026-09-28, owner directive; see
  LICENSE-CHOICE.md). Was (SUPERSEDED): OPEN — 10 §10.2.3 shows
  NOTICE + LICENSE at root but names no license for clean-side code
  in the window read; no LICENSE, no NOTICE, no file headers
  invented; coordinator names it.
- D11 Zero spec content in skeleton. Clause §-pointers used as
  navigation in this file only; no field layouts, values, or grammar
  reproduced anywhere under portrepo/.
- D12 tests/unit + bench are shells with README pointers. First-unit
  suggestion (block-walk) cites the spec pointer only; lanes write it.
- D13 No Makefile target builds oracle_probe here. From: "No Apple
  binary linkage" (implementer brief); oracle builds in battery lane
  (BATTERY-LANE brief). Port repo must build with Apple libs absent
  (P10.11 analogue).
- D14 Determinism + no-chatter + exit 0/10 + DECODE_SIZE are port_cli
  contract terms (tests/battery/README.md), restated in src/README.md
  by reference, not duplicated.

## OPEN (directives silent — coordinator decides)

- O1 License choice: RESOLVED 2026-09-28 = 0BSD (owner directive;
  D10). Was: OPEN, blocked first-commit shape.
- O2 Final spec path + freeze hash lanes may read (blocks all lanes;
  cf. SCAFFOLD Q2). Nothing starts without it.
- O3 API shape confirm: simplified (D5) vs Apple-exact 6-arg mirror.
- O4 Level representation: RESOLVED round-4 (R-E-B1 SPEC-WINS) =
  full-form int {0xE00,0xE01,0xE05,0xE09} at the API boundary, bare
  only below the strip point (interior). Was: bare int (D5) vs header
  enum vs port_cli-only hex. See MERGE-M14.
- O5 Bench scope (black-box via port_cli vs in-process timing).
- O6 Lane count / lane dirs vs this skeleton: does each lane fork this
  skeleton, or does one lane fill it in place? (cf. SCAFFOLD Q3/Q4.)
- O7 Verdict-threshold confirmation for skeleton's `smoke`/`full`
  wiring (zero-tolerance default assumed; cf. SCAFFOLD Q9).

## Merge (Stage-6 build loop; 2026-09-17)

- MERGE-M1 Level dispatch accepts FULL-form (0xE00/0xE01/0xE05/0xE09)
  AND bare (0/1/5/9), identical scratch sizes + identical encode bytes
  (see `lzmesh_u1_scratch_for` / `lzmesh_u1_strip` in src/lzmesh_enc.c).
  Why: enc lane v2 pins full-form-only entry (S1.3/Q20) with bare
  rejecting, but the port contract is bare everywhere merge cannot
  touch — `lzmesh.h` documents 0/1/5/9 (D5), port_cli masks selectors
  to the low byte before calling, unit tests + battery all drive bare.
  Merging full-form-only broke every encode path (unit: 120 FAILs;
  smoke: total ENC_ZERO_ASYM). Full-form inputs behave exactly as the
  lane specifies; bare inputs (which the port API defines as valid)
  keep working. No other level values accepted (0xE02/0x100/negatives
  still fail 0). CONFLICT for coordinator: enc GAPLOG-u7 asks that
  lzmesh.h + D5/O4 flip to full-form-only with bare rejecting; M1
  keeps bare valid instead. Resolving toward full-form-only requires
  coordinated edits to port_cli.c + tests + header (all outside merge
  write-allow) and re-greens unit/smoke the other way. Owned by O3/O4.
- MERGE-M2 Merge state: src/lzmesh_dec.c == impldec lane byte-exact
  (no merge edit needed); src/lzmesh_enc.c == implenc lane byte-exact
  except M1 (dispatch + strip + 3 comments). Lane moved mid-merge (u7
  racer append grew the file 1246 -> 1279 lines incl. a dedup NOTE);
  final diff re-verified: only M1 hunks differ.
- MERGE-M3 Gates green 2026-09-17: `make all` zero warnings
  (-O2 -std=c11 -Wall -Wextra); `make unit` exit 0
  (roundtrip 112/0/8, e00-vectors 4/0/6/0 — matches UNITLOG baseline);
  `make selftest` OK; smoke (77 seeds, e05, oracle live) runs clean
  with ONLY expected raw-only buckets: ENC_DIFF 231 + DEC_A
  DEC_ZERO_ASYM 231, zero CRASH/TIMEOUT/NONDET/DEC_DIFF/CROSS_FAIL/
  ROUNDTRIP_FAIL; DEC_P fully clean (oracle decodes every port byte).
  Smoke verdict FAIL is spec-gap state, not breakage (see M4).
- MERGE-M4 Spec gaps observed at merge (no fix attempted; lane-owned):
  encoder always-RAW (COMP blocked: R-100 fo/TIER-1, R-002 F5 finder
  skip, parse/scheduler unwired, lane/bit packer + non-H residue,
  U7-LIT0) and decoder COMP fail-closed (payload layout F1, Huffman
  two-level H1, lit-run L1, match-len L2, dist/suffix D1). Evidence:
  8 comp-emit XFAILs (unit), 6 comp-vector XFAILs (unit), 231+231
  smoke findings confined to oracle-COMP cells. No corruption seen
  anywhere (no DEC_DIFF/CROSS_FAIL on either side).
- MERGE-M5 Round-2 enc merge (u9, 2026-09-17): merged sel-fix (u4_sel
  Q17-scope comment; u7_token_rep/new lit mask &7->&3) + run-COMP
  wiring (u9 fwd decls, want_comp run-gate via comp_keep, encode
  seam u9_run_emit, appended u9 section is_run/run_layout/run_emit).
  Lane moved mid-merge (1285->1402 lines); final diff re-verified
  M1-only (6 hunks: 5 dispatch/strip + 1 entry comment). Dec still
  byte-identical to lane; no dec-sibling flush yet at merge time.
- MERGE-M6 Round-2 gates: `make all` zero warnings (-O2 -std=c11
  -Wall -Wextra); `make selftest` OK; `make unit` exit NONZERO:
  roundtrip 103 pass / 12 FAIL / 5 xfail (was 112/0/8), e00-vectors
  unchanged 4/0/6/0. The 12 FAILs are port-encode-run-COMP +
  port-decode-fail-closed (decoder fetch GAP-u3-F1 rejects all
  COMP): one-sided enable state, NOT plumbing breakage — no merge
  fix attempted (decoder fetch + encoder token both lane-owned).
  3 comp-emit XFAIL->PASS flips (zeros-4096 L1/L5/L9) confirm the
  encoder now emits COMP for runs. Smoke buckets IDENTICAL to M3
  (ENC_DIFF 231 + DEC_ZERO_ASYM 231, zero corruption buckets).
- MERGE-M7 Oracle-cell findings for u9 (merge-observed, lane-owned):
  run22/e05 port-vs-oracle differ in exactly 1 byte (tok 0x47 vs
  0x07; header/bo/fo/lit/len/modes/counts/END all identical) —
  U9-R1 fetch-order CONFIRMED, but oracle REJECTS port COMP (exit
  10): token lit field must be 0 for litc==1 (first byte comes from
  literals[0] pre-token, Q18), port emits lit field 1
  (`lzmesh_u4_token(1u,0u,7u)` in run_emit). Pins the U7-LIT0
  lit-run<->field map for this shape; fix belongs to the enc lane
  (U9-R2/R3 still open).

## iOS target (Stage-6 build loop; 2026-09-17)

- IOS-1 Floor = iOS 7.0 for device arm64 + sim x86_64. Code is pure
  C11 + libc (stdio/stdlib/string/signal only; zero Apple API), so
  nothing raises the floor above the toolchain minimum; arm64 iOS
  begins at 7.0 (first 64-bit runtime). Xcode 27.0 compiles+links
  warning-free at 12/9/8/7 and stamps honest LC_VERSION_MIN_IPHONEOS
  7.0 (probe + vtool verified). SPECFINAL-v2.md has no deployment
  section (all "floor" hits are codec-algorithm floors) — spec adds
  no constraint.
- IOS-2 Sim arm64 stamps minos 14.0 (ld-enforced IOSSIMULATOR floor;
  no arm64-sim runtime exists below 14). Platform reality, not a
  code defect; accepted and documented, not fought.
- IOS-3 Recipe `ios/Makefile.ios`: IOS_MIN=7.0 default, host-mirror
  flags, device + sim-arm64 + sim-x86_64 slices, all outputs under
  ios/build/, zero src/ edits. `verify` target gates minos +
  libc-only undefined symbols (16 libc syms + stack/dyld; libSystem
  only; forbidden grep empty). Static libs also export ~110
  non-static lane helpers next to the 5 public syms — harmless in a
  .a; a framework pack step may add an export list (lane-owned).
- IOS-4 Smoke green: sim-arm64 CLI runs on booted iPhone sim via
  simctl spawn (ad-hoc-signed copy, explicit UDID, shell stdio
  redirect) — 4/4 roundtrips byte-identical (60 B e00; 1/4/32 KiB
  vectors e05). Host cross-check 10/10 vectors + synth. Full log +
  device-install steps: tmp/iostarget/IOS-BUILD.md; run recipe:
  ios/NOTES.md.
- MERGE-M8 Round-2 close (05:06): u9 lane final (Tasks 1-5 flushed,
  no code change after 04:57 merge). Dec sibling still unflushed —
  no new impldec gaplog/code as of close; dec stays byte-identical.
  Final state: enc diff M1-only (54 diff lines), dec identical,
  build zero-warning, selftest OK, unit 103/12/5 (lane-owned,
  see M6), smoke M3-identical buckets. No commits. Next merge
  triggers: dec COMP-fetch landing, u9 token-lit fix.
- MERGE-M9 Round-3 merge (u11 + dec-u6b, 2026-09-17): dec wholesale
  (lane 693->910 lines: D-B4 counts_ok C8..C12, D-B5 first-byte
  pre-emit, D-B2 ss_byte/decode_len/lit_run/match_len ml=mc+2,
  D-B1 RAW/REPEAT fetch [9,bo) fetch-order exact; Huffman + suffix
  still fail-closed) — port dec byte-identical to lane, zero merge
  edits. Enc: u11 token-lit fix only (run_emit 0x47->0x07 + shape
  comment); diff re-verified M1-only (6 hunks). Lane mtimes at
  merge: dec 05:13:18, enc 05:10:27; no u10/encomp-relaunch or new
  dec-racer flushes landed during the merge window.
- MERGE-M10 Round-3 gates: `make all` zero warnings (-O2 -std=c11
  -Wall -Wextra); `make unit` exit 0 (roundtrip 115/0/5 — the 12
  M6 FAILs resolved by decoder fetch; e00-vectors 4/0/0/6 with 6
  XPASS "comp decode exact; COMP landed"); `make selftest` OK;
  `make smoke` verdict PASS (1232 cells, 0 fail, empty buckets —
  was ENC_DIFF 231 + DEC_ZERO_ASYM 231). Run-shape oracle probes
  (results/r3probe/): port-vs-oracle byte-identical COMP at
  n=21/22/23/100/264/265/300/4096 e05, oracle-dec-port + port-dec-
  oracle both exit 0 + byte-exact — U7-LIT0 VERIFIED, U9-R1/R2/R3
  confirmed for run shape. Remaining lane-owned gaps: E-B2-general
  COMP (non-run), decoder Huffman fetch + suffix/dist (D-B1 full,
  D-B3), U9-R2/R3 beyond run shape. No commits.
- MERGE-M11 Round-3b merge (u10 + dec-D-B3, 2026-09-17): lanes moved
  mid-round (dec 05:19:07, enc 05:18:00) — re-merged. Dec wholesale
  (910->1003 lines: D-B3 lanes_parse Q3 index-at-END + suffix_bits
  LSB-first + new-dist dist_resolve sb/low3/suffix i%8; fetch takes
  lanes param) — byte-identical, zero merge edits. Enc wholesale +
  M1 re-apply (6 hunks, re-verified M1-only): u10 L0 run COMP
  (want_comp L0 branch via comp_keep, l0_layout/l0_emit tok 0xC0
  extra=n-4 litc=n, U10-CNT n<=65535). Both lane files syntax-gated
  pre-merge (stable 3+ min, u10 Tasks 1-5 flushed, u6g checkpoint
  only). Gates: build zero warnings; unit exit 0 (roundtrip
  116/0/4 — L0 zeros-4096 comp-emit flipped XFAIL->PASS; e00
  4/0/0/6); selftest OK; smoke PASS (0 fail cells). L0 oracle
  sweep (results/r3probe/): byte-identical + both legs OK at
  n=21/22/100/258/259/300/4096/8192/16384 — U10 pin VERIFIED.
- MERGE-M12 L0-62881 finding (merge-observed, lane-owned, NO merge
  fix): port L0 run COMP at n>=62881 rejected by oracle (exit 10)
  AND by port's own decoder (exit 10, fail-closed, zero bytes —
  no corruption, no misdecode). Bisect: 62880 accept / 62881
  reject both sides. Root: S4.2 REPEAT-lit ceiling 62880 vs u10
  U10-CNT cap 65535 (u16-only); shape uses lit/cn/REPEAT so litc=n
  breaches the ceiling. Oracle block-splits L0 runs (multi-block
  COMP from n=32768: 53B/79B; port single-block 27B still accepted
  there). Fix belongs to enc lane: cap L0 run COMP at n<=62880 or
  block-split (oracle splits at ds 16384). L1/L5/L9 runs unaffected
  (litc=1). Unit/smoke blind here (max run vector 4096); suggest a
  64K-run unit case to the lanes. No commits.
- MERGE-M13 Round-3c merge (dec-Huffman u6h, 2026-09-17): dec
  wholesale (1003->1396 lines: u6h Huffman section ~290 lines
  lz_u6h_* — canonical-ascending rbit table-index 2^maxlen,
  Kraft==1024 both levels, meta cap 5, bitmap gate, round-robin
  used/count, lane-reset/bitpos-persist, S3.4 peek-pad + strict
  consume, malloc bufs + local stdlib decls; G-u6h-DUP degenerate
  fill; fetch any_huf + Huffman-in-fetch-order + fail-path free;
  comp_block frees; u6b suffix reused unmodified) — byte-identical,
  zero merge edits. Lane compile note pending at merge (Flush 2+3
  landed, "Next: compile+probe") so merge syntax-gated pre-copy
  (clean) — full build zero warnings after. Enc untouched this
  round (stable since 05:18:00, still M1-only 6 hunks). Gates:
  unit exit 0 (roundtrip 116/0/4, e00 4/0/0/6); selftest OK; smoke
  PASS (0 fail cells). Huffman oracle probes (results/r3probe/
  h_*): port decodes oracle COMP byte-exact 8/8 — incl skew e05/e09
  with lit/tok/dist Huffman (850 tok, 788 dist, suffix-continued
  bitpos) and fib tok/len-Huffman; text all-RAW COMP OK. Merge-side
  ASan (macOS, no LSan): unit 116/0/4 + skew-Huffman decode byte-exact,
  zero reports. u6b F4
  line-count note (1063) vs merged 1003 pre-Huffman — count only,
  bytes verified. Remaining: E-B2-general COMP emit (non-run),
  D-B7 T-cap, L0-62881 cap (M12), u6h Flush-4 probe note. No commits.
- MERGE-M14 Round-4 M1-revert (R-E-B1 SPEC-WINS, 2026-09-17): MERGE-M1
  dual-accept REVERTED — port src/lzmesh_enc.c restored to lane bytes
  (cp + cmp proof: both enc and dec now byte-identical to lanes; the 6
  M1 hunks — scratch_for/strip bare cases + 3 comments + entry note —
  are gone). No lane flushes merged: no u12 flush exists (checked
  implenc/ listing), GAPLOG-u13 is a no-op revert confirmation (lane
  already full-only, zero bytes changed), GAPLOG-u6h Flush 4 is
  verify-only (25/25 probe + compile, no code delta), lane code mtimes
  stable (enc 05:18:00, dec 05:32:30). F-ADJUD §1a-c applied: (a)
  lzmesh.h flipped to full-form-only (D5 superseded, O4 resolved);
  (b) port_cli low-byte mask removed — selector forwarded as full int
  with INT_MAX overflow guard, codec does full-int match (mirrors
  oracle_probe, which never masked); (c) unit re-pinned: LEVELS
  full-form, case names L%x, + 11 invalid-level fail-closed asserts
  (bare/wide/negative: encode 0 + scratch 0 at n=32 and n=0).
  Battery/smoke needed no edit (already drives full-form selectors).
  Gates: `make all` zero warnings (-O2 -std=c11 -Wall -Wextra);
  `make unit` exit 0 (roundtrip 127/0/4 = 116 baseline + 11 new
  invalid-level PASS; e00-vectors 4/0/0/6 unchanged); `make selftest`
  OK; `make smoke` verdict PASS (1232 cells, 0 fail, empty buckets —
  corpus-identical to M10/M11). CLI boundary probe: e00/e01/e05/e09
  exit 0 (22-run -> 23B COMP each); bare 0/1/5/9 + e02/e0f/100/1e05/
  100e05/ffffffff all exit 10; e05 roundtrip byte-exact. Note: `make
  clean` removed results/ incl. prior r3probe probe dirs (standard
  clean behavior); smoke re-ran fresh PASS. No commits. Remaining
  lane-owned gaps unchanged: E-B2-general COMP, D-B7 T-cap, L0-62881
  cap (M12). ios/ untouched (outside merge write-allow).
- MERGE-M15 Round-5 merge (dec-u6i MTF + dec-u6j D-B7 + enc-u14 E-B4,
  2026-09-17): dec wholesale (1396->1457 lines: u6i DEC-MTF dedup —
  new `lz_u3_recents_rep` k==0 no-op / move-front with r[k+1..3]
  untouched, replay call site `sel<=3 ? rep : push`, push re-scoped to
  new-dist-only; u6j D-B7 scratch ceiling — `lz_u2_round32` +
  `lz_u2_scratch_ok` 0-if-count0-else-round32 + 2576 SIGNED b.gt,
  per-block header-pre-replay gate after counts_ok + REPEAT-lit) —
  byte-identical, zero merge edits. Enc wholesale + zero merge edits
  (u14 E-B4: `lz_u3_l5_skip` uint->int32/int64 signed, dead symbol,
  zero callers, run-path byte-frozen by construction) —
  byte-identical. Lane mtimes at merge: dec 06:02 (u6j), enc 05:59
  (u14); two mid-window racers caught + re-merged (enc-u14 at 05:59
  after first cmp, dec-u6j at 06:02 after first build — final cmp
  re-verified both identical post-copy). u12 still Task-1-only at
  close (Task-1b reads-only at 06:04, no code) — but u12 CODE
  landed 06:11 inside the watch window; merged as M16 below. u6j
  Flush 3 (FINAL: T/R-pair math + no-regress + compile, verify-only,
  zero code delta) landed 06:03 post-merge — merged bytes already
  final, no action.
  Gates: `make all` zero warnings (-O2 -std=c11 -Wall -Wextra);
  `make unit` exit 0 (roundtrip 127/0/4, e00 4/0/0/6 — M14 baseline);
  `make selftest` OK; `make smoke` verdict PASS (1232 cells, 0 fail).
  MTF trigger vectors forged + verified on port_cli (40B COMP forges,
  bo=24 fo=29, RAW lanes, sb0 dists): V1 rep1->rep2 41B byte-exact
  byte39=C (shift predicts E), V2 rep0->rep3 43B byte-exact byte41=E
  (shift predicts D) — u6i misdecode gone. Remaining lane-owned:
  E-B2-general COMP (u12), L0-62881 cap (M12), u6j Flush-3 T/R proof.
  No commits.
- MERGE-M16 Round-5b merge (enc-u12 SHAPE-P + dec-u6l ZA-fetch,
  2026-09-17): enc wholesale (1505->1781 lines: u12 SHAPE-P —
  p-periodic 2..8 non-run single-token new-dist no-H COMP at L1/L5/L9;
  want_comp !is_run arm, encode 3-way seam, appended ~270-line u12
  section; run/L0/boundary paths untouched) — byte-identical, zero
  merge edits; u14 hunk intact in lane (no l5_skip diff). Dec
  wholesale (1457->1459: u6l ZA fetch leniency — fo==bo accepted iff
  no-Huffman, unused-nonempty parse+ignore, FETCH2 superseded, dist_n
  removed) — byte-identical, zero merge edits. Merge refused u6l
  once mid-edit (lane syntax-broke `dist_n` at first gate, correctly
  rejected; re-gated clean after u6l FINAL Flush 3). One-sided window
  observed + closed in-round: post-u12/pre-u6l unit fell to 115/12/4
  (uni-2/3/4/5 x L1/L5/L9, decode-0 fail-closed, zero corruption —
  M6-class); u6l flipped all 12 back to PASS. Oracle-cell proofs
  (merge-observed): port SHAPE-P bytes BYTE-IDENTICAL to oracle at
  altAB35 (25B, N35 `7f`+`00` pin holds) + period7-n100 (31B);
  oracle-dec-port + port-dec-oracle both exit 0 + byte-exact both
  shapes (oracle_probe needs DECODE_SIZE env; bare call exits 2 —
  harness artifact, not signal). Root pin: oracle emits fo==bo with
  dist>0 for sb0-only no-Huffman (u6l theory confirmed on Apple
  bytes); old FETCH2 over-rejected exactly this. Final gates: `make
  all` zero warnings; `make unit` exit 0 (127/0/4, e00 4/0/0/6);
  `make selftest` OK; `make smoke` PASS (1232 cells, 0 fail —
  smoke corpus lacks periodic non-run flippers); MTF V1/V2 still
  byte-exact on final binary. Lane mtimes at close: enc 06:11 (u12),
  dec 06:22 (u6l FINAL); stable + cmp-verified at 06:26. Remaining
  lane-owned: E-B2 seq/text general COMP (Huffman+scheduler), L0-62881
  cap (M12), u6k EXP-CAP/REV (gaplog absent all round), ZA fuzz-rerun
  closure count (u6l prediction). No commits.
- MERGE-M17 Round-6 merge (dec-u6k EXP/REV + port_cli EXP-CAP + P0 proof,
  2026-09-17): dec wholesale (u6k 4 replay hunks: 3 EXP cap/underflow
  guards — lit wblk>ds FAIL, lit+match wblk>=room cap-stop, match
  take>room-wblk overflow-safe reorder — + REV C21 loop-top wblk==ds
  FAIL, S4.3 tokens-remain-after-fill; cap-hit pre-empts, S4.5
  precedence kept) — byte-identical, zero merge edits. Enc untouched
  (u15 Task-1-only, no code; lane enc == port enc at merge). Merge-side
  caller fix (u6k finding, R3 expansion-5 root cause): port_cli dec
  cap was max(env, walk+1024) — walk-as-floor sized the 49B giant-ds
  mutant (ds 0x200->0x5B000200) at 1.5GB while oracle_probe uses fixed
  DECODE_SIZE. Now min(walk, env) (env unset -> walk; 0 -> 1024
  fallback); no +slack arithmetic (env already +1024, walk exact);
  header comment re-pinned. P0 proof (results/r6probe/, forged here:
  oracle e05 period-512 + ds->0x5B000200 + len-u32->0x5B000200):
  walk-sized port_cli emits 1,526,727,168B (R3 number reproduced
  exactly); DECODE_SIZE=1536 port_cli emits 1536B rc=0; oracle same
  env emits 1536B rc=0, BYTE-IDENTICAL (cmp clean); first 512B ==
  original raw. ds=511 clamp-down 511B port==oracle identical;
  ds=513 both refuse. Gates: `make all` zero warnings (-O2 -std=c11
  -Wall -Wextra); `make unit` exit 0 (boundary 64/0, comp-roundtrip
  82/0/10, e00 4/0/0/6, framing 18/0, huff-suffix 18/0, mtf 5/0/2,
  roundtrip 127/0/4; 318 PASS / 0 FAIL); `make selftest` OK; `make
  smoke` PASS (1232 cells, 0 fail). Lane mtimes at close: dec
  06:28:41 (u6k FINAL), enc 06:11:55 (u12, stable all round).
  NOT merged: enc-u16 M12 multi-block L0 (code landed 06:42:39,
  Tasks 1-5 flushed by 06:44 — but merge REFUSED on compverify
  refutation, M16-refusal precedent). Throwaway probe
  (results/r6probe/u16test/, lane enc + port dec/cli, zero-warning
  build): L0 zeros-62881 e00 -> port 53B 2-block (as u16 predicts),
  oracle 79B 3-block; legs: (1) port-dec-port53 rc=10 REFUSED,
  (2) oracle-dec-port53 rc=10 REFUSED, (3) port-dec-oracle79 rc=0
  62881B EXACT, (4) port-dec-port27(62880) EXACT. U16-R3 +
  later-block pre-emit shape hypothesis REFUTED both sides (fail-
  closed, zero bytes, no misdecode); leg 3 pins the defect to u16
  emit-side (decoder eats oracle multi-block fine). Merge would be
  gate-neutral (L0cap XFAILs tolerate dret==0 with dynamic eret;
  smoke/full lack 62881 cells) but bakes oracle-refuted bytes —
  next step is enc-lane rework (split schedule / later-block
  header), not merge. Remaining lane-owned: E-B2 seq/text general
  COMP (u15), u16 M12 rework, REV-16 R4 count, ZA fuzz-rerun
  closure. No commits.
- MERGE-M18 Round-7 merge (enc-u16 M12 multi-block + enc-u15
  SHAPE-P-LONG, 2026-09-17): enc wholesale in two landings (1781 ->
  1916 u16, then 1916 -> 1968 u15 which landed mid-window 06:49:38
  after first build — caught by stability re-check + re-merged;
  final cmp re-verified both identical post-copy). u16: 4 additive
  hunks (fwd decls + LITMAX 62880, want_comp L0 arm, encode seam L0
  arm, appended ~119-line section; fires only L0 n>62880). u15: 3
  additive hunks (fwd decl, want_comp !is_run arm u12-first-then-u15,
  appended ~47-line want-only section reusing u12_emit; fires only
  where u12==0 + long + period). Zero merge edits. Dec untouched
  (byte-identical, u6k FINAL 06:28:41). Lane mtimes at close: enc
  06:49:38, GAPLOG-u15 06:50:47 (Tasks 3+4 verify-only, no new code),
  dec 06:28:41. Gates: `make all` zero warnings (-O2 -std=c11 -Wall
  -Wextra); `make unit` exit 0 (64/82/4/18/18/5/127 = 318 PASS / 0
  FAIL — M17-identical totals); `make selftest` OK; `make smoke`
  PASS (1232 cells, 0 fail). M12 proof (results/r7probe/, final
  binary): port enc zeros-62881 e00 -> 53B rc=0 (u16 live); leg1
  port-dec-port53 rc=10 0B SELF-REFUSED; oracle enc 62881 -> 79B;
  leg2 oracle-dec-port53 rc=10 0B; leg3 port-dec-oracle79 rc=0
  byte-exact 62881B; leg4 port 62880 -> 27B rc=0 byte-exact. So the
  53B 2-block emission is verified but the 62881 ROUNDTRIP is NOT —
  self + oracle both fail-closed (zero bytes, no misdecode), defect
  pinned u16 emit-side by leg3 (decoder eats oracle multi-block
  fine). Merged per coordinator order; M17 refusal grounds REPRODUCED
  on the merged binary (not resolved) — M12 stays open, needs
  enc-lane rework (split schedule / later-block header). u15 proof
  (results/r7probe/ab/, pre-u15 reconstructed by exact 3-hunk revert:
  1916 lines + 53B + zero-warning build): 210-candidate periodic
  sweep pre-vs-post = 45 diffs, ALL RAW->COMP (AABA/AAAB/AABABA x
  n=64..300 x e01/e05/e09; pre=n+6, post 28/30/32/34B), zero reverse;
  AABA100 e05 28B self-exact + oracle-accepts (oracle emits 29B, 1B
  style delta, G8-class) — agrees with lane Task-4 (28B, AABA400 32B
  vs oracle 33B). Remaining lane-owned: u16 M12 rework, E-B2 seq/text
  general COMP (Huffman), U15-G4LONG 1B delta, REV-16 R4 count, ZA
  fuzz-rerun closure. No commits.
|- MERGE-M19 Round-8 merge (enc-u17 M12 later-block fix, 2026-09-17):
  [T1 FLUSH] lane enc 2017 lines (07:01:11) vs port 1968:
  diff = exactly u17 5 edits (header re-pin + later_layout rest=bs-3 +
  want/validate/write i==0 branches; 94 diff lines, all inside u16
  section). GAPLOG-u17 Tasks 1-5 flushed 07:02:03. Dec lane == port
  byte-identical (u6m NO-OP: P3/P5 prove decoder correct, defect
  enc-side; zero dec bytes touched). No u18. No other racers.
  [T2 FLUSH] lane syntax-gated clean pre-copy; enc wholesale cp ->
  cmp identical (2017 lines); dec untouched (identical); zero merge
  edits. Lane mtimes stable (enc 07:01:11, dec 06:28:41); no u18.
  [T3 FLUSH] `make clean` + `make all` exit 0, zero warnings
  (-O2 -std=c11 -Wall -Wextra). clean removed results/ (standard).
  [T4 FLUSH] `make unit` exit 0: 64/84/4/18/18/3/7/47/5/127 = 377 PASS
  / 0 FAIL (m16 bins landed post-M18-gates +57; comp-roundtrip 82->84
  via L0cap 62881+62882 XFAIL->PASS — self-leg already positive).
  `make selftest` OK. `make smoke` PASS (1232 cells, 0 fail).
  [T5 FLUSH] M12 3-leg proof POSITIVE on merged binary (results/
  r8probe/): port enc zeros-62881 e00 -> 53B rc=0; leg1 port-dec-
  port53 rc=0 62881B byte-exact (was rc=10); oracle enc -> 79B rc=0;
  leg2 oracle-dec-port53 (DECODE_SIZE=63805) rc=0 62881B byte-exact
  (was rc=10); leg3 port-dec-oracle79 rc=0 byte-exact (stays PASS).
  Leg4 62880 -> 27B rc=0 byte-exact (frozen). Byte-confirm: blk1 len
  ff cd7a (extra 31437 = ds-3, was CC). M12 CLOSED as roundtrip;
  byte-identity vs oracle 79B NOT claimed (2-blk vs 3-blk split
  schedule, G8 open). [T6 N/A — no negative legs.] Close: lanes
  stable at close (enc 07:01:11, dec 06:28:41, GAPLOG-u17 07:02:03),
  both cmp-identical, no u18, zero merge edits. No commits.
  Remaining lane-owned: E-B2 seq/text general COMP (Huffman),
  U15-G4LONG 1B delta, G8 split-schedule identity, REV-16 R4 count,
  ZA fuzz-rerun closure.
- MERGE-M20 Round-9 CLI-cap fix (R4 TRUNC-1024 + REV-1024 P0,
  2026-09-17): port_cli.c ONLY (1-line behavior: dec cap = env when
  DECODE_SIZE set, else walk; 0 -> 1024 fallback kept for the no-env
  path only) + header comment re-pin. Root cause: M17 min(walk,env)
  sized walk-0/short mutants below the oracle's fixed DECODE_SIZE —
  the sizer MUST reject (0) on trailing-past-END/desync framing
  (S2.4/Q8) while the decoder MUST return on END and ignore trailing
  (S4.8), so the 1024 fallback emitted an rc=0 prefix where the
  oracle emits full output (TRUNC-1024, 2,698) or pre-empted replay
  rejects the oracle reaches (REV-1024, 1,736 + the 231 full-accept
  lookalikes where cap==raw). Fix is S6.5-mandated (forged sizes ->
  {0,min(cap,true)} with oracle's cap). Behavior delta is exactly
  env-set + walk<env; walk>=env (expansion-5 shape) and no-env paths
  are value-identical to M17 by construction. R4 repro proof
  (DECODE_SIZE=raw+1024, frozen oracle referee): R4R1 65536B
  IDENTICAL (was 1024), R4R2 1025B IDENTICAL (was 1024), R4R3/R4R4
  both-refused (was port-ok 1024); R4R5 (carried e00 plen==raw) +
  R4R6 (ZA port-refused) unchanged — non-cap mechanisms, P1 other
  lanes (bigger cap cannot flip refuse->accept). Expansion-5 holds:
  COMP + RAW giant-ds forges (ds 0x5B000200) three-way identical
  (new==M17-frozen==oracle, env-capped, no gigabyte); 1536B short-dst
  prefix three-way byte-identical (M17 P0 property preserved); no-env
  walk path full-exact 4096B (J27b: walk never capped). Gates: `make
  clean` + `make all` exit 0 zero warnings (-O2 -std=c11 -Wall
  -Wextra); `make unit` exit 0 (64/84/4/18/18/3/7/47/5/127 = 377
  PASS / 0 FAIL, M19-identical); `make selftest` OK; `make smoke`
  PASS (1232 cells, 0 fail). No commits.
- MERGE-M21 Round-9 enc-u18 + dec-u6p (2026-09-17): SHAPE-P-SUF
  (p-periodic 9..32 non-run single-token new-dist + suffix lanes COMP
  at 1/5/9) + lenbytes/mc+2 u32-wrap split (e00 REV-8 reject +
  ZA-417 clamp-accept). [T1] u18 Final (914/914 + 38/38 freeze,
  VERDICT live); u6o gaplog absent (never landed, racer-O no-show);
  u6p Flush2 in-progress at start -> held dec, watched, re-read at
  07:46 -> FINAL Flush4 (compile green + forged probes 8/8 +
  discriminator: neutralized copy flips P1 20->0 refuse, P2/P4 0->18
  accept). [T2 enc] wholesale cp lane->port, cmp identical (2353
  lines); delta is 9 hunks u18-only (+336: fwd decls, want arm
  u12->u15->u18, seam route (u12||u15)?u12_emit:u18_emit, u18 section
  split 217+107 around 9 frozen tail lines, doc comments); lane
  syntax-gated pre-copy, mtime stable 07:18:32. [T3 dec] merged only
  after FINAL: wholesale cp, cmp identical (1506 lines); delta 12
  hunks D-B2-only (+26: decode_len *wrapped + saturate, lit_run FAIL
  on wrap, match_len saturate on wrap/mc+2 overflow), matches Flush2
  description verbatim; lane mtime stable 07:25:03. Zero merge edits
  either file. [T4] `make clean` + `make all` exit 0, zero warnings
  (-O2 -std=c11 -Wall -Wextra), rebuilt after EACH merge (enc-only
  gates ran green first, then full re-gate post-dec). [T5] `make
  unit` exit 0: 64/84/4/18/18/3/7/47/17/16/9/55/5/127 = 474 PASS / 0
  FAIL (10 baseline binaries M20-identical -> byte-freeze holds; 4
  m19-* binaries 17/16/9/55 green); `make selftest` OK; `make smoke`
  PASS (1232 cells, 0 fail). [T6 SHAPE-P-SUF proof, results/r9shape/,
  final binary] 72 cells p9..32 x e01/e05/e09 x n100 (family:
  tile(bytes(range(p)))): 72 COMP (lane's 70/72 was its family: its
  p27 e05/e09 slots-dirty RAW; this family slots-clean there too),
  SELF-exact 72/72, ORACLE-ACCEPTS 72/72 (oracle decodes every port
  COMP byte-exact), byte-ident vs oracle enc 71/72. Single delta
  p31/e01 (port 60B bo44/fo49 vs oracle 61B bo45/fo50): take-point
  style delta, oracle takes lit32/mc66 (lit-extra 28, len-extra 35,
  footer litc 0x20) vs port lit31/mc67 (27/36/0x1f); same shape
  family, both decode exact both sides; enc-lane follow-up (L1
  arbitration), not merge-blocking. Boundaries e05: p9n35 RAW (41 =
  n+6) / p9n36 COMP (tag 01, ds36 bo21 fo26, 37B = fo+11 — layout
  EXACTLY as lane predicted); p32n58 RAW / p32n59 RAW (this family:
  ml27 short -> G4 rep2-live rejects, fail-closed by design; lane
  family passed G4 there — input-dependent, port==lane by cmp).
  Every boundary cell self+oracle exact. Determinism spot true.
  Close: both files cmp-identical, lanes stable (enc 07:18:32, dec
  07:25:03, u18 07:22:24, u6p 07:34:54), no u6o. No commits.
  Remaining lane-owned: seq/text general COMP (Huffman), U18-PMAX
  p33+, U18-PADHI, p31e01 L1 take-point note, R4 fuzz-rerun closure
  (REV-8 wrap + ZA-417 counts decided there, not in-lane).
- MERGE-M22 Round-10 merge (enc-u19 SHAPE-R + enc-u20 E2a budget-split,
  2026-09-17): k-run rep-chain COMP at 1/5/9 + e00 L0 S5.4 budget
  multi-block. [T1] u19 Final (36/36 freeze + 21/21 self, VERDICT
  live); u20 Task-2-only at start (67-line log, no compile/verify)
  -> HELD per brief, watched; u6q gaplog absent all round (no-show);
  dec lane == port byte-identical (u6p, cmp clean) so no dec merge.
  [T2 enc-u19] SELECTIVE merge (u20 code present in lane file but not
  final): 6-edit script (fwd decls, want doc, want arm u18-or-u19,
  seam doc, seam route u18-check+u19-else, 252-line section copied
  byte-exact from lane) -> port 2353->2617 lines; post-merge
  port-vs-lane diff = u20 hunks ONLY (L0-arm cond x2, sched comment,
  u20_sched + nblocks/blocksize), zero u19 residue. [T3 u20] landed
  FINAL mid-round (Tasks 3-5 flushed + VERDICT "E2a SPLIT LANDED",
  839/839 checks, code stable since 08:03 Task 2, verify-only after)
  -> wholesale cp, cmp identical (2660 lines); full delta vs R9
  baseline = u19 + u20 hunks exactly (17 hunk headers). Zero merge
  edits either landing. [T4] `make clean` + `make all` exit 0, zero
  warnings (-O2 -std=c11 -Wall -Wextra); `make unit`: 569 PASS / 4
  FAIL — the 4 are test_m19_m12 stale even-split pins (want 27/53
  at n=62880/62881/62882/65535; all other 15 binaries green incl
  new m21-shapep-suf 91/0 + m21-wrap-split 8/0); `make selftest` OK;
  `make smoke` PASS (1232 cells, 0 fail). The 4 FAILs assert
  oracle-REFUTED bytes (see T6: oracle emits 79B 3-block at all 4
  sizes) — test-lane-owned pin update, outside merge write-allow;
  merge does not revert oracle-confirmed bytes to satisfy them.
  [T5 SHAPE-R proof, results/r10shape/, 52/52] n24 12+12 e01/e05/e09:
  pre-merge binary RAW30 -> post COMP25B, layout EXACT (ds24 bo14
  fo14, lit 41 42, tok 07 47, len 02, modes 0x40, footer
  40/02/02/02/00, END) + BYTE-IDENTICAL to oracle enc + self-exact
  + oracle-accepts + port-dec-oracle; e00 stays RAW30 both sides.
  Extras e05: 3x10 n30 + 12+20 n32 flip + byte-ident (27B/26B);
  4x8 n32 flips + self + oracle-accepts while oracle stays RAW38
  (G8 superset emission, G8-class, both legs exact). [T6 E2a proof,
  results/r10e2a/] o53 EXACT (n=22027 53B [16385,5642] ident) +
  o79 IDENT at 62880/62881/65535 ([16385,32768,tail]) — U20-B1 pin
  CONFIRMED (oracle ds1=32768 exactly); boundaries 16393 single /
  16398 2-block / 49166 3-block all byte-ident; slivers 16394-16397
  (port single-27B vs oracle 2-block 41/44B, ds9 tail) + 49165
  (port 2-block 53B vs oracle 3-block 70B) = U20-TAIL13 absorb-vs-
  split policy delta, valid every leg both directions incl
  port-dec-oracle-sliver byte-exact (emit policy only, enc-lane
  follow-up). Close: both files cmp-identical, lanes stable (enc
  code 08:03, u20 log 08:05 FINAL, dec code 07:25); u6q gaplog
  landed 08:10 Task-1-partial (reads-only, zero code, R6 notes
  u6p wrap-split moved zero fuzz rows — lane-owned, no merge
  action) -> no dec merge. No commits.
  Remaining lane-owned: m19-m12 pin update (test lane), U20-TAIL13
  sliver policy, 8x4-style G8 superset review, seq/text general
  COMP (Huffman), U18-PMAX p33+, R4 fuzz-rerun closure.
- MERGE-M23 Round-11 merge (dec-u6r #53 match-wrap FAIL, 2026-09-17):
  [T1] u6r Final (match_len wrap arm FAILs, 8/8 forged probes, 2-line
  arm only, lit/match_take/replay untouched); u6q still FLUSH1
  reads-only (zero code, Task-1-partial) -> no merge; enc lane ==
  port byte-identical (cmp clean, u19+u20 already M22) -> no enc
  merge. [T2 dec-u6r] wholesale cp (port-vs-lane diff was EXACTLY
  the u6r 2 hunks: match_len comment + wrapped||mc>MAX-2 FAIL arm)
  + 1 merge-lane comment line (decode_len "match clamps" -> "both
  callers reject (lit C18, match C16-class)", u6r-flagged stale).
  [T4] `make clean` + `make all` exit 0, zero warnings (-O2 -std=c11
  -Wall -Wextra). [T5] `make unit`: 636 PASS / 6 FAIL = 4
  pre-existing m19-m12 stale even-split pins (M22-known,
  oracle-refuted) + 2 m21-wrap-split P2/P4 accept-pins flipped to
  refuse BY DESIGN (the u6r fix itself; sizer=18 dec=0 on both);
  all other 16 binaries green incl m22-e2a 27/0 + m22-shaper 42/0
  + roundtrip 127/0; `make selftest` OK; `make smoke` PASS (1232
  cells, e05). The 2 wrap pins assert oracle-REFUTED u6p bytes
  (COMP-R6 0/7 agree) — test-lane-owned pin flip to refuse-pins,
  outside merge write-allow. [T6 #53 proof on port binary]
  transient probe (rm after, zero files kept): port_cli P1 lit-wrap
  REFUSES (exit 10, lit-FAIL holds), P2 match-wrap REFUSES (was
  18xB accept in M22 8/0 run), P4 mc+2-overflow REFUSES, P3 lit-MAX
  REFUSES, P2b exact-fill 18xB + P6 min2 18xE accept byte-exact,
  P5 L0-run22 enc+dec roundtrip exact — 7/7 ALL-PASS; unit
  m21-wrap-split 6/2 corroborates (P1/P3/P2c refuse-pins pass,
  P2b/P5/P6 accept-pins pass, only the 2 stale accept-pins fail).
  #53 row-level close needs COMP-R7 (verify lanes). Close: dec ==
  lane + 1 comment line; enc untouched; lanes stable (u6r FINAL,
  u6q reads-only). No commits.
  Remaining lane-owned: m21-wrap-split P2/P4 pin flip to refuse
  (test lane) + m19-m12 pins, U20-TAIL13 sliver policy, 8x4-style
  G8 superset review, seq/text general COMP (Huffman), U18-PMAX
  p33+, R4/R6 fuzz-rerun closure (wrap-class counts decided there).
- MERGE-M24 Round-12 merge (dec-u6q P1-mech + enc-u22 E2a-remainder,
  2026-09-17):
  [T1] u6q Final (FLUSH5 verify FINAL, 5/5 forged probes, 2 hunks:
  e00 5B-overlong u<=254 -> C16-FAIL + ZA [9,bo)-slack tolerance);
  u6s racer planning-only (FLUSH1, zero code) -> ignore; u22
  VERDICT closed lane-side (Tasks 1-5 FLUSHED, 1787/0 lane probe,
  13-edit TEST2-only + RAW tiny-tail set present in lane file) ->
  FINAL, merge; u21 planning-only (Task-1 IN PROGRESS, zero code)
  -> ignore. [T2 dec-u6q] 2 surgical hunks onto port file (fetch
  off!=br_len -> off>br_len slack-ACCEPT + decode_len u<=254
  FAIL); port-vs-lane residual is 1 comment line where PORT is
  newer (M23 u6r "both callers reject", lane still u6p-stale
  "match clamps", code identical) -> kept port line, no wholesale
  cp. [T3 enc-u22] 11 edits (SPLITMIN 9/RAWMAX 12, sched floors,
  u22_tail_raw + want/validate/write RAW-tail branches, comments);
  port-vs-lane diff now EMPTY (no port-newer content lost).
  [T4] `make clean` + `make all` exit 0, zero warnings (-O2
  -std=c11 -Wall -Wextra). [T5] `make unit`: 649 PASS / 5 FAIL =
  5 m22-e2a sliver pins flipped to NEW shapes BY DESIGN (the u22
  fix itself: 16394-16397 -> 41/42/43/44B, 49165 -> 70B, all
  R7-oracle sizes); all other 18 binaries green incl m21-wrap
  8/0 + m19-m12 9/0 (M23-stale pins now fixed lane-side) +
  roundtrip 127/0; `make selftest` OK; `make smoke` PASS (1232
  cells, e05). The 5 e2a pins assert oracle-REFUTED U20-TAIL13
  absorbs (COMP-R7 falsified) — test-lane-owned pin flip to
  split shapes, outside merge write-allow. [T6 proof on port
  binary] transient probes (rm after, zero files kept): u6q
  P1 overlong-5B REFUSES 0 / P2 minimal-5B 259xA / P3 slack 20xA
  / P4 exact 20xA / P5 underlong REFUSES 0 — 5/5 ALL-PASS; u22
  slivers 16394-16397 -> 41/42/43/44B 2-block + 49162-49165 ->
  67/68/69/70B 3-block + 16393 27B 1-block freeze pin, all
  roundtrip-ok — 9/9 ALL-PASS. Close: dec == lane + 1 newer
  comment line; enc == lane byte-identical; u21 still open
  (E1-next shapes, no code yet). No commits.
  Remaining lane-owned: m22-e2a 5-pin flip to split shapes (test
  lane), U22 7-vs-8 corpus-subset note (fix covers full 8-window
  class), n>65535 U10-CNT RAW (frozen), seq/text general COMP
  (Huffman), U18-PMAX p33+, R7 fuzz-rerun closure (e00-REV/ZA row
  flips + E2a 29/36 -> 36/36 decided there).
- MERGE-M25 Round-13 merge (enc-u21 SHAPE-R12, 2026-09-17): k-run
  rep-chain with lead-short r0 in {1,2} + isolated interior
  singletons at 1/5/9 (U19-R12 partial close).
  [T1] u21 VERDICT Final (GAPLOG-u21 08:47:08, enc code stable
  08:45:10, 83/83 lane probe, C11 -Wall -Wextra clean, no
  commits): SHAPE-R12 live, disjoint from u19 (u19 cells u19==1/
  u21==0, u21 cells u19==0/u21==1). S5.9.a one-parse + S5.9.b
  label rule read (AM2-3 draft: harness MUST NOT gate on label
  bytes); non-first lit2 = U21-LIT2 1-step extrapolation,
  formula-consistent (total==n, consumed==litc), oracle-decides-
  at-merge — this merge's oracle-accepts leg is the verdict.
  Dec: lane == port + 1 port-newer comment line (M23 u6r);
  u6s FLUSH1 planning-only (zero code), u6q already M24 -> no
  dec merge. No other enc racers (u22 log 08:37 pre-dates u21
  code; port-vs-lane enc diff = u21 hunks ONLY, see T2).
  [T2] Lane syntax-gated clean pre-copy; wholesale cp lane->port,
  cmp identical (3007 lines). Delta = exactly 8 u21 hunks (fwd
  decls, want doc, want arm u19->u21, seam docs, seam route
  u19-check+u21-else, appended 292-line u21 section). Zero merge
  edits. Lane mtime stable 08:45:10; log 08:47:08.
  [T3] `make clean` + `make all` exit 0, zero warnings (-O2
  -std=c11 -Wall -Wextra). clean removed results/ (standard).
  [T4] `make unit` exit 0: 691 PASS / 0 FAIL (21 binaries:
  64/84/4/18/18/3/7/47/17/16/9/55/91/8/27/42/12/18/19/5/127 —
  M24's 5 m22-e2a stale pins now fixed test-lane-side incl new
  m24-e2a-tail 18/0 + m24-u6q-dec 19/0; byte-freeze holds
  everywhere); `make selftest` OK; `make smoke` verdict PASS
  (1232 cells, 0 fail, e05).
  [T5 SHAPE-R12 proof, transient probe (rm after, zero files
  kept)] 7 cells x e01/e05/e09 = 21/21: pre (reconstructed
  2704-line M24 baseline, exact 8-hunk reverse, zero-warning
  build) RAW n+6 -> post COMP (26/27/26/24/27/31/30B per
  cell, lane-predicted sizes EXACT); SELF-exact 21/21;
  ORACLE-ACCEPTS 21/21; port-dec-oracle 21/21; e00 stays RAW
  7/7; u19 n24 25B + run22 23B pre/post FROZEN 6/6;
  determinism 5/5 on lit2 cell. Byte-ident vs oracle enc
  9/21: lead2 3/3, litsingle [LIT2] 3/3, 3singleton [LIT2]
  3/3. Style deltas 12/21, ALL lead1-family (r0=1): oracle
  emits tok-REPEAT (modes 0x48, 1B shorter, e.g. 25B vs 26B)
  where port emits tok-RAW (modes 0x40) for all-equal tok
  pairs — S5.5 all-equal->REPEAT mode-selection delta,
  G8-class, both legs exact both directions; enc-lane
  follow-up (U21-TOKREPEAT), not merge-blocking.
  [T6 U21-LIT2 VERDICT: CONFIRMED, not refuted.] Oracle
  ACCEPTS all 6 lit2-shape emissions byte-exact (litsingle
  n25 + 3singleton n39 x 3 selectors) — and stronger,
  byte-IDENTICAL to oracle enc on all 6. No
  U21-LIT2-REFUTED record needed; the 1-step extrapolation
  is oracle-verified. Close: enc cmp-identical, lane stable
  (code 08:45:10, log 08:47:08); dec untouched (lane ==
  port + 1 port-newer M23 comment line); zero merge edits.
  No commits. Remaining lane-owned: U21-TOKREPEAT
  (tok-RAW vs oracle tok-REPEAT on all-equal pairs),
  U21-R12B (interior r2, adjacent/trailing shorts), k>10/
  lit-HUF + e00-E1, seq/text general COMP (Huffman),
  U18-PMAX p33+, R7 fuzz-rerun closure.
- MERGE-M26 Round-14 merge (enc-u23 SHAPE-R12B, 2026-09-17): k-run
  rep-chain with >=1 interior G==2 short-gap ([r2] or [r1,r1] via
  non-first lit3+extra0) or adjacent lead shorts, at 1/5/9 (U21-R12B
  partial close).
  [T1] u23 VERDICT Final (GAPLOG-u23, 88/88 flip + 52/52 freeze,
  C11 -Wall -Wextra clean, no commits): SHAPE-R12B live, disjoint
  from u19/u21 by construction + proven (u23 cells u23==1/u21==0/
  u19==0 and reverse). u24 NOT final (GAPLOG-u24 through Task-2
  implement only, no compile/probe/verdict) -> code EXCLUDED; no
  u25 file flushed -> nothing to merge. Dec: u6t FINAL zero-code
  (SPEC-QUERY SQ-u6t-1/2 outcome, never-guess rule), u6u FINAL
  zero-code (R5 Class A closed by landed u6r arm, discriminator
  proven) -> no dec merge; lane == port + 1 port-newer M23
  comment line (verified by diff).
  [T2] Lane file holds u23-Final + u24-nonfinal, so NO wholesale
  cp: 8 surgical edits onto port file (fwd decls, want doc, want
  arm u21->u23, seam docs, seam route u21-check+u23-else, appended
  ~350-line u23 section in 3 chunks). Post-merge port-vs-lane
  diff == EXACTLY the 6 u24 Task-2 edits (u21 mtok/tokb/tok_eq
  region) and nothing else; u23 bytes verbatim. Zero merge edits
  to logic. Port enc 3007 -> 3369 lines. Dec untouched.
  [T3] `make clean` + `make all` exit 0, zero warnings (-O2
  -std=c11 -Wall -Wextra). clean removed results/ (standard).
  [T4] `make unit`: 721 PASS / 3 FAIL, all 3 = m25-shaper12-veto
  adjacent-shorts pins ([X1,Y1,A12,B12] n26 -> now COMP 27B tag
  01 x e01/e05/e09) flipped BY DESIGN (the u23 fix itself: lead
  [1,1] is the u23 lead-feature); all other 21 binaries green
  incl m22-shaper 42/0 + m25-shaper12 15/0 + roundtrip 127/0 +
  mtf 5/0 (run directly: set -e stops `make unit` at the veto
  binary). The 3 pins assert oracle-REFUTED u21-veto bytes (this
  merge proves oracle emits byte-identical COMP on that exact
  cell, see T5) — test-lane-owned pin flip to COMP-pins, outside
  merge write-allow. `make selftest` OK; `make smoke` verdict
  PASS (1232 cells, 0 fail, e05).
  [T5 SHAPE-R12B proof, transient probes (rm after, zero files
  kept)] 7 cells (6 lane goldens + veto adjacent cell) x e01/e05/
  e09 = 21/21: pre (reconstructed 3007-line M25 baseline, exact
  8-hunk reverse, zero-warning build) RAW n+6 -> post COMP
  (29/27/29/29/30/33/27B per cell, lane-predicted sizes EXACT);
  SELF-exact 21/21; ORACLE-ACCEPTS 21/21; port-dec-oracle 21/21;
  e00 stays RAW 7/7; freeze run22 23B x4 + u21lead1 26B x3 +
  u19n24 25B all FROZEN; determinism on all cells. Both probe
  binaries 124/124 ALL-PASS. Byte-ident vs oracle enc 18/21:
  all cells IDENT x3 selectors EXCEPT g2b [A9,X2,B9,C9] x3 where
  oracle emits RAW 35B (tag 00) and port emits COMP 29B — valid
  style delta in the reverse direction (oracle accepts port's
  29B byte-exact; both sides decode both framings). Non-first
  lit3 leg (the u23 risk point) is oracle-verified: every lit3
  cell byte-identical to oracle enc, and oracle-accepts on all.
  [T6 Battery consequence (honest note)] g2b-shape inputs flip
  from both-RAW agree to port-COMP/oracle-RAW ENC_DIFF (valid on
  both sides, S5.9.a; same both-valid class as M25's 12 style
  deltas, reversed direction). E1 compverify should gain G==2-gap
  flips elsewhere; g2b-shape rows need the style-delta reading,
  not a defect reading. Close: enc == M25 + 8 u23 hunks, lane
  residual == u24-nonfinal only; dec untouched (lane == port + 1
  port-newer M23 comment line). No commits. Remaining lane-owned:
  u24 U21-TOKREPEAT to Final (then merge), u23-follow-up
  U23-TOKREPEAT (u23 same tok-RAW pattern), U23-R12C (interior
  G>=3), U23-R12D (lead L>=3), trailing shorts, m25-shaper12-veto
  3-pin flip to COMP-pins (test lane), k>10/lit-HUF + e00-E1,
  seq/text general COMP (Huffman), U18-PMAX p33+, R7/R8
  fuzz-rerun closure.
- MERGE-M27 Round-15 merge (enc-u24 U21-TOKREPEAT + enc-u26 E2c
  S5.4 take-gate, 2026-09-17): u21 tok lane follows S5.5
  (all-equal->REPEAT incl tc==1, 1B shorter / modes-only); u19
  takes gated by S5.4 9B look-ahead (r_last<10 -> fail-safe RAW).
  [T1] u24 VERDICT Final (M24 precedent: Tasks 1/2/5/3/4 all
  FLUSHED + VERDICT closed lane-side, 3287/0 lane checks, C11
  clean, no commits): 12/12 R9 delta cells match pinned oracle
  recoding, 9/9 ident cells frozen. u26 VERDICT Final (all 5
  tasks FLUSHED 09:35, 8630/0 lane checks): E2c veto live,
  173/173 sweep flips inside n24..37, witnesses at oracle
  sizes. u25 NOT final (GAPLOG-u25 Task-1 reads-only, zero
  code, no verdict) -> nothing to merge. Dec: u6t/u6u both
  FINAL zero-code (SPEC-QUERY outcome / landed-arm proof) ->
  no dec merge; lane == port + 1 port-newer M23 comment line.
  [T2 RACER TIMELINE — READ BEFORE REPLAY] Lane enc moved
  mid-merge: 09:20 (M26+u24) -> 09:31 (+u26 E2c, unflushed,
  no gaplog yet). Pre-merge diff (port-vs-lane-09:20) showed
  EXACTLY the 6 u24 hunks, but wholesale cp at 09:31 swept
  the in-flight E2c veto too. Caught by unit (m22-shaper
  42/0 -> 38/4, 4x8/6x6 COMP->RAW): root-caused to the E2c
  rlast<10 veto (rlast 8/6), surgically reverted both E2c
  hunks (veto block + doc line), re-greened (m22 42/0,
  m25-shaper12 8/7 = u24 BY-DESIGN sizes 25/24/26/30 +
  cap). u26 then flushed Final 09:35 (in-scope per task-3,
  M24 multi-merge precedent) -> re-applied the exact 2 E2c
  hunks verbatim. Final: enc cmp-identical to lane (3407
  lines, lane stable 09:31); dec untouched. Zero merge
  edits to logic. [T3] `make clean` + `make all` exit 0,
  zero warnings (-O2 -std=c11 -Wall -Wextra). [T4] `make
  unit` (direct, all 25 binaries): 760 PASS / 4 FAIL, all 4
  = m22-shaper E2c pins (4x8 x3 -> RAW38, 6x6 -> RAW42)
  flipped BY DESIGN (the E2c fix itself; this merge proves
  oracle emits byte-identical RAW on those cells, see T5b)
  — test-lane-owned pin flip to RAW-pins, outside merge
  write-allow. NOTE test lane re-pinned mid-round (m25-
  shaper12.c 09:38 -> post-u24 25B/0x48 oracle-ident pins,
  veto 09:39 -> post-u23 COMP pins): m25-shaper12 15/0 +
  veto 18/0 green, corroborating u24 bytes independently.
  `make selftest` OK; `make smoke` verdict PASS (e05).
  [T5a TOKREPEAT proof, transient probes (rm after, zero
  files kept)] Merge-7 x e01/e05/e09 = 21/21: SELF-exact
  21/21; ORACLE-ACCEPTS 21/21; port-dec-oracle 21/21;
  byte-ident vs oracle enc 21/21 (was 9/21 at M25); flips
  12/12 lane-predicted EXACT (lead1 26->25, solo modes-only
  24, len 27->26, esc 31->30); frozen 9/9 byte-identical to
  pre-merge capture; e00 RAW n+6 7/7; modes pins lead1/solo
  0x48 + len/esc 0x08, tok lane REPEAT; determinism;
  siblings run22 23B + u19n24 25B + u23g2a 29B FROZEN.
  [T5b E2c proof, same probes] 6 witness cells x 3 sels =
  18/18 lane pins (F1 n29 RAW35, F2 4x8 RAW38, F3 n24
  RAW30, F4 n33 RAW39, F6 n33 KEPT COMP27, F5 n39 RAW45);
  SELF 18/18; ORACLE-ACCEPTS 18/18; port-dec-oracle 18/18;
  byte-ident 15/18 — F5 x3 NONIDENT (port RAW45 vs oracle
  COMP32) is the lane-predicted E2b->E1 class move
  (count-neutral, still ENC_DIFF, both legs exact both
  directions; oracle packing needs Apple-parse cells, open
  gap). [T6 VERDICTS] U21-TOKREPEAT CLOSED at merge (R9
  12/12 style deltas -> 21/21 ident, both-valid class
  eliminated for merge-7). E2c veto LIVE at merge (R5
  witnesses at oracle sizes + ident where oracle-RAW).
  Close: enc == lane byte-identical; dec == lane + 1
  port-newer comment; lanes stable (enc 09:31, dec 08:35,
  u25 reads-only). No commits. Remaining lane-owned:
  m22-shaper 4-pin flip to RAW-pins (test lane), u25 E2b
  take-point delay to Final (then merge), U23-TOKREPEAT
  (u23 tok-RAW pattern), u21/u23 rem<9 analogs (sibling-
  owned; u23 n29 golden needs re-proof if gated),
  U23-R12C/D, trailing shorts, k>10/lit-HUF + e00-E1,
  seq/text general COMP (Huffman), U18-PMAX p33+, R5/R7/R8
  battery-rerun closure (E2c-120 ident count + E1 growth
  decided there).
- MERGE-M28 Round-16 u25 E2b SHAPE-R-TERM REFUSED on compverify
  refutation (M17 precedent), tree reverted to M27-green, 2026-09-17.
  [T1] u25 VERDICT Final read (GAPLOG-u25 09:55, all 5 tasks flushed,
  code stable 09:53, no racers): 9B-blocked trailing takes skipped
  (S5.4 Q30) + S5.3.a terminator (rep0 len-2 tok 0xC0, lit R+2 with
  2 zero overhang, len [R-1,0xC0]); u26 interaction: broad rlast<10
  veto REMOVED as E2b->E1 (E2c preserved via terminator HUF/TIER
  veto instead). Dec: no new code (lane == port + 1 port-newer M23
  comment line; u6t/u6u zero-code Finals already M27) -> no dec
  merge. Port-vs-lane enc diff pre-merge was EXACTLY the 4 u25
  hunks (fwd decls, want term-branch, emit term-branch, ~372-line
  appended layout+emit section).
  [T2] Merged wholesale (syntax-gated clean pre-copy, cp + cmp
  identical, 3834 lines, zero merge edits) -> compverify REFUTED
  (see T5) -> REVERTED by exact 4-hunk reverse + u26 veto block
  restore (one byte-fix: em-dash in the restored comment; final
  3407 lines / 127544 bytes = M27 size, behavior-verified 21/21,
  see T6; byte-identity to pre-merge unprovable without VCS, size
  + lines + behavior all match). Dec untouched throughout.
  [T3] `make clean` + `make all` exit 0, zero warnings (-O2 -std=c11
  -Wall -Wextra), both in merged and reverted states.
  [T4] Gates WITH merged (refuted) code were green-but-blind: `make
  unit` exit 0 (789 PASS / 0 FAIL, 26 binaries), `make selftest`
  OK, `make smoke` PASS (1232 cells, 0 fail, e05). Blind because
  no unit/smoke pin covers 9B-blocked SHAPE-R shapes (n39-class):
  coverage gap, test-lane follow-up suggested (n39-shape pin in
  unit + battery corpus, else the re-merge gates on nothing).
  [T5 REFUTATION proof, transient probe (rm after, zero files
  kept), 111 pass / 9 fail] M27-recon baseline (zero-warning
  build): n39 -> RAW45 x3 + F1 -> RAW35 x3 (live pre-image). Merged
  binary n39 (32x23+3xAA+4xBB) x e01/e05/e09: emits COMP35 with the
  lane-predicted layout EXACT (tag 01 ds39 bo24 fo24, lit [23
  AAABBBB 00 00], tok [07 C0], len [22 06 C0], modes 0x00, footer
  2/3/10/0 + END; lane==port behavior confirmed) BUT all 9 E2b
  legs fail: SELF-refused x3 (port decodes nothing of its own
  bytes) + ORACLE-REFUSED x3 + NONIDENT x3 (oracle emits 32B COMP,
  corroborating M27 T5b COMP32; u25's quoted R5 "35B oracle pin"
  does not match the live oracle on this input). Sweep (r0,3,4)
  e05: n=19/27 both-RAW agree (TIER veto works); n=39/107 port
  35B refused both sides vs oracle 32B; n=307 port 39B refused
  both vs oracle 36B — EVERY terminator-COMP refused both sides,
  oracle uniformly 3B tighter. Passing legs under merged code:
  E2c veto coherence HELD (F1 RAW35 / F2 RAW38 / F3 RAW30 / F4
  RAW39 / 6x6 RAW42 + SELF + oracle-accepts + ident, F6 COMP27
  kept + legs, all x3) and freeze (n24 25B byte-exact, n30 27B,
  e00 RAW n+6, determinism) — the veto-removal direction was
  safe; only the terminator BYTES are malformed. Defect is
  enc-side (M17 leg logic): port decodes oracle's 32B byte-exact
  (decoder handles true E2b packing), both decoders refuse port's
  35B. U25-OVH (zeros) + U25-LENORD ([extra,C0]) both REFUTED at
  merge (S5.9.a: merge decides) — lane's hand-replay is no
  substitute for real-decoder verification.
  [T5b ORACLE-32B PIN for enc-lane rework, n39 e05] tag 01 ds39
  bo21 fo21; lit[9:17] = 23 AA AA AA BB BB BB BB (R=8, ZERO
  overhang); tok[17:19] = 07 C0 (term tok matches u25); len[19:21]
  = 16 04 (ONE term extra 0x04, not [06,C0]); footer[21:31] =
  modes 0x0000 tokc2 lenc2 litc8 distc0; END. hex = 0127000000
  15001500 23aaaaaabbbbbbbb 07c0 1604 00000200020008000000 ff.
  Oracle-self + port-dec-oracle both byte-exact. Root-cause
  HYPOTHESIS (lane to confirm): 3 len bytes emitted where 2 are
  consumed (trailing 0xC0 unconsumed) + 2 zero overhang bytes
  break C18/counts; target is lit=R + len=[live...,0x04] (0x04
  semantics lane-derived, verify on R-sweep, not one cell).
  [T6 REVERT re-gate] build zero warnings; `make unit` exit 0
  (810 PASS / 0 FAIL, 27 binaries — test lane landed m27-e2c-veto
  21/0 mid-round, independently corroborating the restored veto);
  `make selftest` OK; `make smoke` PASS (1232 cells, 0 fail);
  revert-check 21/21 (n39 RAW45 + self + oracle32 + both legs,
  F1 RAW35, F6 COMP27, all x3). Close: enc reverted (M27 state),
  dec untouched (lane == port + 1 port-newer comment), lanes
  stable (enc-lane u25 Final unmerged, no new dec code), zero
  files kept from probes. No commits. Merging was gate-neutral
  but would bake self-refusing bytes + flip E2b-459 from ENC_DIFF
  to ROUNDTRIP/CROSS_FAIL at battery rerun — next step is
  enc-lane rework (match oracle 32B layout, decode-verify with
  REAL decoders both sides before reflushing), not merge; an
  M18-style override needs explicit coordinator order.
  Remaining lane-owned: u25-rework to Final (then re-merge),
  n39-shape unit/battery pins (test lane), E2b-459 open, E2c-120
  ident count (battery rerun), U23-TOKREPEAT, u21/u23 rem<9
  analogs, U23-R12C/D, trailing shorts, k>10/lit-HUF + e00-E1,
  seq/text general COMP (Huffman), U18-PMAX p33+, R5/R7/R8
  closure.
- MERGE-M29 Round-17 merge (enc-u28 E2b SHAPE-R-TERM rework to
  oracle-32B, 2026-09-17): lit ZERO overhang (litc=j+R), tok
  0xC0 kept, len ONE term extra R-3 (lenC=live+1).
  [T1] u28 VERDICT Final read (GAPLOG-u28 10:16, all 5 tasks
  flushed, code stable 10:14, 78/78 lane probe incl pin-32B
  byte-identical x3 via REAL lane decoder + R-sweep R=3..9 +
  sweep sizes + byte-freeze, C11 clean, no commits): E2b
  reworked to the M28 T5b oracle-32B layout. u27 GAPLOG absent
  at T1 -> mid-merge racer (see T2). Dec: no new code (lane ==
  port + 1 port-newer M23 u6r comment line, verified by diff;
  u6t/u6u zero-code Finals already M27; lane dec mtime stable
  08:35) -> no dec merge. Pre-merge port-vs-lane enc diff was
  EXACTLY 5 u28 hunks (fwd decls +8, want E2b branch replacing
  the u26 broad veto, emit route +23, appended 364-line
  reworked section "owner: u25, rework u28"); lane syntax-gated
  clean pre-copy.
  [T2 RACER TIMELINE] Lane enc moved mid-merge: u27 SHAPE-R12CD
  seam wiring landed 10:19:13+ (GAPLOG-u27 10:18:39 Task-2a,
  "Next: Task 2b hunks"), AFTER our 10:19:09 wholesale cp + cmp
  identical (3826 lines, zero merge edits). Port-vs-lane
  residual is EXACTLY u27 code (fwd decls, want_comp doc+arm,
  seam doc+route, appended u27 section): all 5 '-' lines sit in
  the 1121-1312 wiring region, zero term-region removals, port
  holds 0 'u27' occurrences + 'rework u28' present -> timed
  wholesale achieved the M26-precedent selective state (u28
  verbatim, u27 excluded). u27 NOT final at close (log: "Next:
  Task 5 compile-check, Task 3/4 probes", no VERDICT) ->
  excluded per brief; lane kept moving on u27 only (u28 bytes
  untouched since snapshot).
  [T3] `make clean` + `make all` exit 0, zero warnings (-O2
  -std=c11 -Wall -Wextra). clean removed results/ (standard).
  [T4] `make unit` (direct, all 27 binaries): 807 PASS / 3 FAIL,
  all 3 = m27-e2c-veto F5-E2bE1 n39 x e01/e05/e09 (port now 32B
  COMP tag 01 = the u28 fix itself; pins assert M27-true 45/RAW
  or u25-drift 35/COMP) -- test-lane-owned pin flip to 32B-COMP
  pins, outside merge write-allow; merge does not revert
  oracle-confirmed bytes to satisfy them. All other 26 binaries
  green (incl m22-shaper 42/0 + m25/m26 shaper suites).
  `make selftest` OK; `make smoke` verdict PASS (1232 cells,
  e05, oracle live).
  [T5 3-leg E2b proof, transient probe (rm after, zero files
  kept), 145/145 ALL-PASS x3 runs] A: M28-pin family
  (32x23+3xAA+4xBB) e05 -> port bytes BYTE-IDENTICAL to the
  pinned oracle hex (0127000000 15001500 23aaaaaabbbbbbbb 07c0
  1604 00000200020008000000 ff) + self + oracle32 + ident +
  leg2 + leg3. B: n39 ABC x e01/e05/e09 -> 32B layout EXACT
  (ds39 bo21 fo21, lit 41 42 42 42 43 43 43 43, tok 07 C0,
  len 16 04, modes 0, footer 2/2/8/0, END) + SELF-exact +
  oracle 32B + IDENT + oracle-dec-port + port-dec-oracle +
  determinism. C: sweep (r0,3,4) e05: n19/n27 both-RAW agree;
  n39/n107 32B IDENT; n307 36B IDENT + all legs exact. D: E2c
  coherence x3 (F1 RAW35 / F2 RAW38 / F3 RAW30 / F4 RAW39 +
  ident + legs; F6 COMP27 EXP_F6 bytes + ident + legs) -- the
  veto-removal direction is safe. E: freeze n24 25B ident +
  legs, n30 27B + legs (ident True), e00 F5 RAW45,
  determinism x5. Oracle live (run22 IDENT control).
  [T6 VERDICTS] E2b SHAPE-R-TERM CLOSED at merge as
  oracle-identical roundtrip (refuted u25 35B -> u28 32B: self
  + both cross legs exact, byte-identical to the live oracle on
  every E2b cell). U25-OVH + U25-LENORD closed per the M28 pin.
  No REFUSE (all legs green; M17/M28 precedent not triggered).
  Close: enc == u28-final verbatim (3826 lines); dec == lane + 1
  port-newer comment; lanes at close (enc-lane u27 in-progress
  non-final, dec stable), zero merge edits. No commits.
  Remaining lane-owned: m27-e2c-veto F5 3-pin flip to 32B-COMP
  pins (test lane), u27 SHAPE-R12CD to Final (then merge),
  U23-TOKREPEAT, u21/u23 rem<9 analogs, U23-R12C/D remainder,
  trailing shorts, k>10/lit-HUF + e00-E1, seq/text general COMP
  (Huffman), U18-PMAX p33+, R5/R7/R8 battery-rerun closure
  (E2b-459 ident count + E1 growth decided there).
- MERGE-M30 Round-18 merge (enc-u27 SHAPE-R12CD wide-gap rep-chain,
  2026-09-17): U23-R12C (interior G>=3) + U23-R12D (lead L>=3) close
  via first/non-first lit esc3 + 255-chain rest, tok all-eq->REPEAT
  incl tc==1, U27-REM9 rlast>=10 native, at 1/5/9 (L0 out).
  [T1] u27 VERDICT Final read (GAPLOG-u27 10:31, all 5 tasks flushed,
  code stable 10:23, 7946/0 lane checks incl 5/5 golden pins +
  185/185 sweep flips lane-decoded exact black-box, C11 clean, no
  commits): wide-gap k-run rep-chain COMP. u6v NOT final (GAPLOG-u6v
  FLUSH1 intake only, zero code edits) -> no dec merge per brief.
  Dec lane == port + 1 port-newer M23 u6r comment line (verified by
  diff, dec lane stable 08:35). Pre-merge port-vs-lane enc diff was
  EXACTLY 9 u27 hunk headers (fwd decls, want doc+arm, seam docs+
  route, appended ~382-line section); zero term-region changes
  (u28 bytes identical, 'rework u28' present both sides); port held
  0 'u27' refs. Lane syntax-gated clean pre-copy.
  [T2] Wholesale cp lane->port, cmp identical (4219 lines, 15 u27
  refs, u28 intact). Zero merge edits.
  [T3] `make clean` + `make all` exit 0, zero warnings (-O2 -std=c11
  -Wall -Wextra). clean removed results/ (standard).
  [T4] `make unit` (direct, all 28 binaries): 827 PASS / 3 FAIL, all
  3 = m26-shaper12b-veto interior-G3-12 n31 x e01/e05/e09 (port now
  COMP 30B tag 01 = the u27 fix itself; pins assert M26-true RAW37)
  -- test-lane-owned pin flip to COMP-pins, outside merge write-
  allow; this merge proves oracle emits byte-identical COMP on
  that exact cell (see T5). All other 27 binaries green (incl
  m29-e2b-term 20/0 + m27-e2c-veto 21/0: test lane re-pinned F5 to
  32B-COMP post-M29). `make selftest` OK; `make smoke` verdict
  PASS (1232 cells, e05, oracle live).
  [T5 R12CD 3-leg proof, transient probe (rm after, zero files
  kept), 163/163 ALL-PASS] Pre baseline reconstructed by exact
  7-block reverse (3826 lines, 0 u27 refs, zero-warning build):
  all 6 flip cells pre-RAW n+6 -> post COMP at lane-predicted
  sizes EXACT (A1 n33 27B / B n35 30B / C n36 31B / LR n33 30B /
  E n315 34B / VETO n31 30B, tag 01, x e01/e05/e09). Leg1
  port-dec-port SELF-exact 18/18; leg2 oracle-dec-port exact
  18/18; leg3 port-dec-oracle exact 18/18; byte-ident vs oracle
  enc 18/18 (incl the veto cell -- M26-precedent pin-flip
  justification). e00 stays RAW n+6 all 6 cells; freeze pre/post
  byte-identical (u19n24 / run22 / u28n39 x3, + self legs);
  determinism; oracle-live run22 IDENT control.
  [T6 VERDICTS] SHAPE-R12CD LIVE at merge as oracle-identical
  roundtrip (U23-R12C + U23-R12D closed; exact-end rep-chain fully
  closed except trailing-shorts/u28-owned and lit-HUF shapes).
  No REFUSE (all legs green; M17/M28 precedent not triggered).
  Close: enc cmp-identical, lanes stable at close (enc 10:23:08,
  dec 08:35:22, u27 log 10:31:05, u6v log 10:31:16 reads-only),
  zero merge edits, zero files kept. No commits.
  Remaining lane-owned: m26-shaper12b-veto interior-G3-12 3-pin
  flip to COMP-pins (test lane), U23-TOKREPEAT, u21/u23 rem<9
  analogs, trailing shorts, k>10/lit-HUF + e00-E1, seq/text
  general COMP (Huffman), U18-PMAX p33+, R5/R7/R8 battery-rerun
  closure (E1 growth decided there).
- MERGE-M31 Round-19 merge (dec-u6v P1 Q1+Q2 REFUSED+reverted,
  enc-u29 held non-final, 2026-09-17): u6v Q1 exactly-N/L0 gate +
  Q2 B1 unified-length reject leg + B2/B3 pins.
  [T1] u6v VERDICT Final read (GAPLOG-u6v 10:57, FLUSH4 FINAL:
  code-level e00-8 8/8 map, ZA-415 0/415 in-wall, compile clean,
  no commits; e00 proof is code-level, no oracle run — wall).
  u29 NOT final (log ends Task 2c hunks, no Tasks 3-5, no
  VERDICT; code+log still moving 10:58 mid-merge) -> enc HELD
  per brief (merge code ONLY if flushed FINAL). Pre-merge
  port-vs-lane dec diff was EXACTLY the 6 u6v add-hunks (Q1 gate
  + call, B1 gate + call, B2/B3 pins) + 1 port-newer M23 comment
  line; lane syntax-gated clean pre-copy.
  [T2] Merged wholesale (cp + M23 1-line comment re-apply, 1741
  lines, zero other merge edits) -> unit REGRESSION (see T5) ->
  REVERTED by exact 6-hunk reverse (sed range-delete on lane
  numbering; final 1515 lines = M30 size, 0 u6v refs, M23
  comment at 959 intact; byte-identity to pre-merge unprovable
  without VCS, size + lines + gates all match). Enc untouched
  throughout (M30 u27 state).
  [T3] `make clean` + `make all` exit 0, zero warnings (-O2
  -std=c11 -Wall -Wextra), both in merged and reverted states.
  [T4] Gates WITH merged (refused) code: `make unit` FAILED at
  comp-roundtrip (L0cap n=62880 decode=0; was PASS at M30).
  [T5 REFUTATION proof, transient probe (rm after, zero files
  kept)] Merged binary, zeros-62880 e00 -> port enc 79B rc=0
  (o79-ident shape); port-dec-port79 rc=10 REFUSED 0B; SAME 79B
  via oracle_probe (DECODE_SIZE=63804) rc=0 62880B BYTE-EXACT.
  Defect is dec-side (M17 leg logic): oracle accepts bytes the
  u6v gate refuses. Controls pass on merged binary: single-L0
  zeros-1000 27B self-exact, run22 e05 self-exact -> misfire is
  multi-block-L0 later-block shape (B1's bo==fo + no-Huffman +
  dc==0 + tok-pattern arm matches oracle-accepted u17
  later-block layouts whose L-field/range model B1 does not
  cover; Q1 skips, no-Huffman). Single-block L0 + run shapes
  unaffected. Merging would flip M22-CLOSED o79 legs (self +
  cross) from exact to refused at battery rerun.
  [T6 REVERT re-gate] build zero warnings; `make unit` (direct,
  all 30 binaries): 869 PASS / 0 FAIL (comp-roundtrip 84/0/8
  L0cap PASS again; m26-shaper12b-veto 24/0 + m21-wrap-split
  8/0: test lane re-pinned M30/M23-known pins post-M30, so the
  M30 3 FAILs are gone too); `make selftest` OK; `make smoke`
  verdict PASS (e05, oracle live). e00/ZA proof: NOT RUN on
  binary — mandatories blocked by the refusal (u6v e00-8 map
  stays code-level-only; ZA-415 accept leg was already
  oracle-GAP per u6v FLUSH4). No REFUSE-override without
  explicit coordinator order (M18 precedent).
  Close: dec reverted (M30 state + M23 comment), enc M30 (u29
  excluded non-final), lanes stable at close (dec code
  10:56:35, u6v log 10:57:20 Final, enc code 10:58:21, u29 log
  10:58:34 non-final), zero files kept. No commits.
  Remaining lane-owned: u6v-rework to Final (narrow B1 off
  oracle-accepted L0 multi-block shapes, decode-verify with
  REAL decoders both sides incl o79 before reflushing; then
  re-merge), u29 SHAPE-R-TRAIL to Final (then merge), U23-
  TOKREPEAT, u21/u23 rem<9 analogs, trailing shorts, k>10/
  lit-HUF + e00-E1, seq/text general COMP (Huffman), U18-PMAX
  p33+, R4/R5/R7/R8 battery-rerun closure.
- MERGE-M32 Round-20 merge (enc-u29 SHAPE-R-TRAIL trailing-short
  terminator, 2026-09-17): all-long live prefix + trailing short
  suffix R in [1,9] + small-R terminator (0x40/0x80/ZERO-len +
  u28-proven 0xC0+[R-3]) at 1/5/9, L0 out. ENC-ONLY round (dec
  held: lane holds REFUTED u6v hunks, u6w rework pending).
  [T1] u29 VERDICT Final read (GAPLOG-u29 11:03, all 5 tasks
  flushed, code stable 10:58, 7/7 golden pins + 219/219 sweep
  flips lane-decoded exact black-box, 5439/0 lane checks, C11
  clean, no commits, scratch deleted; byte-identity NOT claimed
  S5.9.a). Dec: no new code (port-vs-lane dec diff is EXACTLY
  the M31-known 6 u6v add-hunks + 1 port-newer comment line at
  959; lane dec stable 10:56, 0 u6v refs in port) -> no dec
  merge per brief. Pre-merge port-vs-lane enc diff was EXACTLY
  8 u29 hunk headers (fwd decls, want doc+arm, seam docs+route,
  appended 419-line section 3863-4281); u27/u28 bytes intact
  both sides; port held 0 u29 refs. Lane syntax-gated clean
  pre-copy.
  [T2] Wholesale cp lane->port, cmp identical (4649 lines, 14
  u29 refs). Zero merge edits. Lanes stable at merge (enc
  10:58:21, dec 10:56:35).
  [T3] `make clean` + `make all` exit 0, zero warnings (-O2
  -std=c11 -Wall -Wextra). clean removed results/ (standard).
  [T4] `make unit` (direct, all 31 binaries): 888 PASS / 0 FAIL
  (M31 869/0 +19 = test-lane m31-b1-len 19/0 landed 11:05
  mid-round, u29-neutral; comp-roundtrip 84/0/8 unchanged — no
  unit pin covers trailing-short shapes, blind spot same class
  as the M28 n39 note, test-lane follow-up suggested). Zero
  FAIL = no REFUSE trigger (M17/M28/M31 precedent not
  triggered). [T4b] `make selftest` OK.
  [T5 acceptance proof, transient probe (rm after, zero files
  kept), 202/202 ALL-PASS] 7/7 goldens x e01/e05/e09 at
  lane-predicted sizes EXACT (T1 n31 25B R1 / T2 n42 28B R2 /
  T3 n33 28B R3 / T4 n41 27B R1 / T5 n301 29B 5B-len-esc / T0
  n28 28B lenC0 / T9 n39 34B R9, tag 01) + determinism + leg1
  SELF-exact 21/21 + leg2 oracle-dec-port exact 21/21 + oracle
  enc byte-IDENT 21/21 + leg3 port-dec-oracle exact 21/21 +
  e00 RAW n+6 x7 + V-TIER1 veto RAW19 x3 + freeze spots
  (run22 23B, u28n39 32B). Oracle live (enc control). R1/R2
  legs all byte-ident: U29-R1ABS (separate 0x40) + U29-CLASS
  (formula-wins) both merge-ACCEPTED as oracle-identical.
  [T6 RACERS] Lane enc moved 11:18:21 mid-round (u30
  SHAPE-R-TC1, +509 lines): Tasks 1/2a-c/5 flushed, NO Task
  3/4/VERDICT (2 VERDICT hits are u29-VERDICT cites) ->
  non-final, EXCLUDED; timed wholesale achieved the
  M29-precedent selective u29 state (port 0 u30 refs, u29
  section intact in lane). Lane dec moved 11:06:20 (u6w B1
  narrow, FLUSH4 Final claimed) -> HELD, enc-only round per
  brief; next-round trigger with M31-mandated re-gate (o79
  legs both sides). `make smoke` verdict PASS (1232 cells, 0
  fail, empty buckets, e05, oracle live).
  Close: enc == u29-final verbatim (4649 lines, 14 u29 refs, 0
  u30); dec M30 state (1515 lines, 0 u6v); zero merge edits;
  zero files kept from probes. No commits.
  Remaining lane-owned: u30 SHAPE-R-TC1 to Final (then merge),
  u6w re-merge (re-gate incl o79 self + cross legs), trailing-
  short unit/battery pins (test lane), U23-TOKREPEAT, u21/u23
  rem<9 analogs, k>10/lit-HUF + e00-E1, seq/text general COMP
  (Huffman), U18-PMAX p33+, R4/R5/R7/R8 battery-rerun closure
  (E1 growth decided there).
|- MERGE-M33 Round-21 merge (dec-u6w B1-narrow rework of M31-refuted u6v,
  2026-09-17): Q1 exactly-N/L0 gate + B1 unified-length reject leg gated
  FIRST-block-only (is_first_block SKIP) + B2/B3 pins. DEC-ONLY round (enc
  held: lane holds non-final u30, port 0 u30 refs).
  [T1] u6w FLUSH4 Final read (GAPLOG-u6w 11:09:55: narrow applied Task 2,
  real lane-decoder verify Task 3 black-box 8/8 + gate-level 12/12 with
  V1-misfire reproduced pre / fixed post, no-regress Task 4 delta ONLY
  V1 0->200 exact, compile Task 5 zero warnings, scratch removed, no
  commits): B1 SKIP-widening on later blocks only (fail-open = M30-green
  behavior), first-block path logic-identical, Q1/fetch/replay untouched.
  Misfire mechanism (M31 T5): B1 arm matches u17 later-block layouts whose
  no-pre-emit D-B5 literal model B1's fixed-offset L read (tok@10, L@11)
  does not cover; P1 B1 evidence is single-block-only. Pre-merge
  port-vs-lane dec diff was EXACTLY the u6v 6 add-hunks (Q1 gate + call,
  B1 gate + is_first_block SKIP call, B2/B3 pins) + u6w 2-hunk narrow
  inside B1 (param + SKIP) + 1 port-newer M23 comment line (kept).
  Lane syntax-gated clean pre-copy. Pre-merge baseline (binaries as
  built): m31-q1-skip 17/0 + m31-q1-gate 20/0 + m31-b3-index 8/0 +
  comp-roundtrip 84/0/8 (L0cap PASS) -> full-suite baseline 933 PASS /
  0 FAIL over 34 binaries (M32-T4 888/0 over 31 + 45 from the 3
  test-lane files that landed 11:07-11:08 after M32-T4).
  [T2 RACER] Lane dec moved 11:23:17 mid-merge (c19 B2-accept-leg code,
  +16 lines: predicate + call-site arm; GAPLOG-c19b outside merge
  read-allow, no FINAL verdict known) AFTER the T1 diff; timed wholesale
  cp swept it in. Surgically reverted both c19 hunks (884B block delete
  + 1-line condition restore, M26/M29 selective precedent): port dec
  1767 -> 1751 lines (= T1 lane size), 0 lowercase-c19 refs (2
  pre-existing uppercase C19 spec pins kept), port-vs-lane residual is
  EXACTLY the 2 c19 hunks + the 1 port-newer M23 line (re-applied post-
  cp; one byte-fix: sed dropped the comment-close slash, restored to
  `*/` + junction verified). u6v/u6w refs 14. Enc untouched (M32 u29
  state, 4649 lines; lane 5158 = +509 u30 SHAPE-R-TC1, Tasks 3/4 +
  VERDICT still missing -> EXCLUDED per brief).
  [T3] `make clean` + `make all` exit 0, zero warnings (-O2 -std=c11
  -Wall -Wextra). clean removed results/ + unit binaries (standard).
  [T4] `make unit` exit 0 (direct full loop, all 34 binaries): 933 PASS /
  0 FAIL = 64/84/4/18/18/3/7/47/17/16/9/55/91/8/27/42/12/18/19/15/18/
  16/24/21/25/20/21/18/19/8/20/17/5/127 — EXACTLY the pre-merge
  baseline (M32-T4 888/0 over 31 + 45 from the 3 test-lane files that
  landed 11:07-11:08 after M32-T4: q1-skip 17 + q1-gate 20 + b3-index
  8). comp-roundtrip 84/0/8 L0cap PASS (M31-refutation shape fixed —
  was decode=0 under u6v). Zero regression → no REFUSE (M17/M28/M31
  precedent not triggered). [T4b] `make selftest` OK.
  [T5 acceptance proof, transient probe 20/20 ALL-PASS, rm after, zero
  files kept] o79 zeros-62880 e00: port==oracle 79B BYTE-IDENTICAL;
  leg1 port-dec-port ACCEPTS 62880B exact (was rc=10 at M31); leg2
  oracle-dec-port exact; leg3 port-dec-oracle exact; controls
  single-L0-1000 27B + run22-e05 self-exact. ZA B1 legs on single-L0
  zeros-100 (fields confirm arm: bo==fo=12, tok=c0, L=96=lo, range
  [96,124]): base both-accept exact; lo-1 (95) + hi+1 (125) both-
  refuse both sides (reject leg FIRES, oracle-safe). Q1 e00-8 replays
  (forge_51 0-active + s02 L0[8]=00): both-refuse both sides. One
  probe-harness fix mid-T5 (n=100 L0 emits 23B not 27B — expectation
  corrected, code untouched; rerun 20/20).
  [T6] `make smoke` verdict PASS (1232 cells, 0 fail, empty buckets,
  e05, oracle live). No REFUSE anywhere (all legs green).
  Close: dec 1751 lines / 62622 bytes (= T1 u6w-lane size; only the
  equal-length M23 comment line differs); enc M32 u29 state 4649 lines
  / 170371 bytes, 0 u30 refs. Lanes stable at close (dec code 11:23:17
  c19-held, enc code 11:18:21 u30-held, u6w log 11:09:55 Final, u30 log
  11:26:32 VERDICT). Zero merge edits to logic. Zero files kept from
  probes. No commits.
  Remaining lane-owned: u30 SHAPE-R-TC1 reached VERDICT Final mid-round
  (Tasks 3-5 flushed, 4025/0, scratch deleted — survey only here, HELD
  per brief, next-round trigger with merge compverify); c19 B2-accept
  leg to Final (then merge; port residual is exactly its 2 hunks + the
  M23 line); trailing-short unit/battery pins (test lane); U23-
  TOKREPEAT; u21/u23 rem<9 analogs; k>10/lit-HUF + e00-E1; seq/text
  general COMP (Huffman); U18-PMAX p33+; R4/R5/R7/R8 battery-rerun
  closure (E1 growth + ZA/B1 row flips decided there).
|- MERGE-M34 Round-22 merge (enc-u30 SHAPE-R-TC1 short-live + trailing-R term,
|  dec-c19 HELD no-verdict, 2026-09-17): u21-live (run0 r in {1,2} +
|  interior r==1 non-adjacent, >=1 live short) + trailing R in [1,9] +
|  u29-verbatim small-R terminator at 1/5/9, L0 out. ENC-ONLY round.
|  [T1] u30 VERDICT Final read (GAPLOG-u30 11:26: Tasks 1/2a-c/5/3/4 all
|  FLUSHED + ## VERDICT SHAPE-R-TC1 LIVE, 4025/0 lane checks black-box
|  lane-decoded, 6/6 goldens + 123/123 sweep, C11 clean, no commits,
|  scratch deleted; byte-identity NOT claimed S5.9.a). u31 IN FLIGHT in
|  same lane but ZERO code bytes: whole-file grep u31 = 0 hits + lane
|  enc mtime 11:18 predates u31 log 11:30 (planning-only) -> separation
|  trivial, no u31-hunk bytes exist to exclude. Port-vs-lane enc diff =
|  EXACTLY 8 u30 hunk headers (net +509 = 4649->5158: fwd decls, want
|  doc+arm u29->u30 LAST, seam docs+route u29-check+u30-else, appended
|  498-line u30 section in 2 diff hunks around }+blank junction,
|  single owner marker "owner: u30", 13 u30 refs, 0 u31/u32/u33;
|  E2b terminator header intact both sides, tail identical, no hunks
|  after). Dec: c19 HELD per brief -- NO verdict line (grep VERDICT in
|  GAPLOG-c19b hits only the audit-cite line 23; log ends FLUSH5
|  "C19-CONT B2-leg DONE", no ## VERDICT); diff vs M33-port is exactly
|  the c19 region + 1-line wire + standing port-newer M23 comment line
|  (code-identical), but the verdict conjunct fails -> HOLD, M31
|  merge-only-if-final precedent.
|  [T2] Lane syntax-gated clean pre-copy; wholesale cp lane->port, cmp
|  identical (5158 lines / 186981 bytes, 13 u30 refs, 0 u31). Timed
|  wholesale = selective u30 state (M29 precedent). Zero merge edits.
|  Dec untouched (M33 u6w state, 1751 lines). Lanes stable (enc 11:18,
|  dec 11:23).
|  [T3] `make clean` + `make all` exit 0, zero warnings (-O2 -std=c11
|  -Wall -Wextra). clean removed results/ + unit binaries (standard).
|  [T4] `make unit` exit 0 (direct full loop, all 34 binaries): 933 PASS /
|  0 FAIL = 64/84/4/18/18/3/7/47/17/16/9/55/91/8/27/42/12/18/19/15/18/
|  16/24/21/25/20/21/18/19/8/20/17/5/127 -- per-binary EXACTLY the M33
|  baseline (no unit pin covers short-live+trail shapes, blind spot same
|  class as M28 n39 / M32 notes, test-lane follow-up suggested). Zero
|  FAIL = no REFUSE trigger (M17/M28/M31 precedent not triggered).
|  [T4b] `make selftest` OK.
|  [T5 acceptance proof, transient probe 279/279 ALL-PASS, rm after, zero
|  files kept] 6/6 goldens x e01/e05/e09 at lane-predicted sizes EXACT
|  (C1 n32 26B R1 / C2 n43 29B R2 / C3 n35 30B R3 / C4 n42 28B R1 / C5
|  n302 30B 5B-len-esc / C6 n39 34B R8-litc10, tag 01, modes/tokc/litc
|  pins EXACT incl C6 litc10 keep + C3/C5/C6 modes 0x00, bo==fo, ds==n)
|  + leg1 SELF-exact 18/18 + leg2 oracle-dec-port exact 18/18 + oracle
|  enc byte-IDENT 18/18 + leg3 port-dec-oracle exact 18/18 + e00 RAW n+6
|  x6 + determinism x6 + freeze (run22 23B, u19n24 25B, u28n39 32B,
|  o79 79B + self) + vetoes (V-tier2 + V-R10 RAW n+6 x3 + self).
|  Oracle live. R1/R2 legs all byte-ident: U29-R1ABS + U29-CLASS inherit
|  merge-ACCEPTED as oracle-identical (M32 legs hold for the combo).
|  [T6] `make smoke` verdict PASS (1232 cells, 0 fail, empty buckets,
|  e05, oracle live). No REFUSE anywhere (all legs green).
|  Close: enc == u30-final verbatim (5158 lines / 186981 bytes, 13 u30
|  refs, 0 u31 -- lane file holds zero u31 bytes at close); dec M33 u6w
|  state (1751 lines / 62622 bytes, c19 excluded non-final). Lanes
|  stable at close (enc code 11:18, u30 log 11:26 VERDICT, dec code
|  11:23, c19b log 11:33 FLUSH5-no-verdict); port enc re-verified
|  cmp-identical at close. Zero merge edits to logic. Zero files kept
|  from probes (transient m34-t5.py + added-lines scratch both rm).
|  No commits.
|  Remaining lane-owned: c19 B2-accept leg to VERDICT Final (then merge;
|  port residual is exactly its 2 hunks + the M23 line); u31 to Final
|  (then merge); short-live+trail unit/battery pins (test lane);
|  U23-TOKREPEAT; u21/u23 rem<9 analogs; k>10/lit-HUF + e00-E1; seq/text
|  general COMP (Huffman); U18-PMAX p33+; R4/R5/R7/R8 battery-rerun
|  closure (E1 growth + ZA/B1 row flips decided there).
|- MERGE-M35 Round-23 merge (enc-u32 SHAPE-R-TC2 r2-live + trailing-R term,
|  dec-c19 B2-accept leg, 2026-09-17): u30-TC1-live + EXACTLY ONE interior
|  r==2 (u23-esc3) + trailing R in [1,9] + u29-verbatim small-R terminator
|  at 1/5/9, L0 out; dec over-long-5B len decode-through (accept-only).
|  [T1] u32 VERDICT Final read (GAPLOG-u32 12:07: Tasks 1/2a-c/5/1R/3/4
|  all FLUSHED + ## VERDICT SHAPE-R-TC2 LIVE, 5/5 goldens + 126/126
|  sweep, 1721/0 lane checks black-box lane-decoded, C11 clean, no
|  commits, scratch deleted; byte-identity NOT claimed S5.9.a). c19
|  VERDICT Final read (GAPLOG-c19b 12:07: FLUSH5 FINAL + FLUSH6 B2
|  re-proof black-box + ## VERDICT B2-LEG LIVE; accept-only FAIL->OK
|  on over-long-5B). Port-vs-lane enc diff = EXACTLY 8 u32 hunk
|  headers (fwd decls, want doc+arm u30->u32 LAST, seam docs+route
|  u30-check+u32-else, appended u32 section in 2 diff hunks
|  4805-5052+5055-5372 = 566 lines, 3 new u32_* funcs only, single
|  owner marker, 0 u31/u33 refs; E2b u25 terminator header intact
|  both sides, tail identical). u31 still zero code bytes
|  (planning-only). Port-vs-lane dec diff = EXACTLY the c19 region
|  (16 lines) + 1-line wire guard (u6q lines intact) + standing
|  port-newer M23 comment line (M23 wording kept). RACER CHECK: u6x
|  log FLUSH5 reads-only ("then FIRST HUNK" not yet cut, 12:08),
|  zero code cut, 0 u6x refs in lane dec; lane dec mtime 11:23
|  predates it -> ZERO u6x bytes, dec-c19 separable. Both lanes
|  syntax-gated clean pre-copy. Lane mtimes/sizes: enc 11:49:48
|  (206343 B), dec 11:23:17 (63544 B); port pre-merge enc 5158
|  lines / 186981 B, dec 1751 lines / 62622 B.
|  [T2] Lanes syntax-gated clean pre-copy; wholesale cp lane->port
|  both sides (enc cmp identical, 5735 lines / 206343 bytes; dec +
|  M23 1-line comment re-apply, 1767 lines, residual vs lane EXACTLY
|  the M23 line). Zero other merge edits. Lanes stable at merge
|  (enc 11:49:48, dec 11:23:17).
|  [T3] `make clean` + `make all` exit 0, zero warnings (-O2 -std=c11
|  -Wall -Wextra). clean removed results/ + unit binaries (standard).
|  [T4] `make unit` exit NONZERO (direct full loop, all 34 binaries):
|  925 PASS / 8 FAIL = m24-u6q-dec 14/5 + m26-shaper12b-veto 21/3,
|  other 32 binaries green (incl mtf 5/0, comp-roundtrip 84/0/8
|  L0cap PASS, m31-b1-len 19/0, q1-gate 20/0, q1-skip 17/0,
|  m23-match-wrap 12/0, roundtrip 127/0/4). The 8 FAILs are the two
|  lanes' INTENDED flips (refuse->accept / RAW->COMP, zero reverse):
|  5 over-long-5B refuse-pins (c19 target) + 3 trailing-short veto
|  pins (u32 D1-family shape). Adjudicated at T5 by live-oracle legs
|  (M17/M28/M31 method): oracle CONFIRMED the new code and REFUTED
|  the pins -> MERGED per M23 (stale-pin) + M30 (veto-flip)
|  precedents, test-lane pin flips owed. [T4b] `make selftest` OK.
|  [T5 acceptance proof, transient probe 178/178 ALL-PASS, rm after,
|  zero files kept] u32: 5/5 goldens x e01/e05/e09 at lane-predicted
|  sizes EXACT (D1 n45 31B R1 / D2 n46 32B R2 / D3 n47 36B R5 / D4
|  n68 37B R5 litc10 / D5 n65 33B R1, tag 01) + determinism + leg1
|  SELF-exact 15/15 + leg2 oracle-dec-port exact 15/15 + oracle enc
|  byte-IDENT 15/15 (R1/R2 ident too, stronger than lane's
|  ident-or-style-delta expectation) + leg3 port-dec-oracle exact
|  15/15 + e00 RAW n+6 x5. FLIP [A14,X2,B14,Y1] n31 x3: port 31B
|  tag01 + 3 legs exact + oracle byte-IDENT x3 (m26 pins stale,
|  M30-class). c19: 5 over-long forges (match u8/u254/u254-short,
|  lit u16/u254) + 5 1B twins + 2 u255 floors: port ACCEPTS
|  byte-exact 12/12 + ORACLE ACCEPTS byte-exact 12/12 (u6q pins
|  stale, M23-class; R6R1 row-close stays battery-rerun-owned).
|  Guards: o79 zeros-62880 e00 port==oracle 79B IDENT + 3 legs;
|  freeze run22 23B + u28n39 32B; vetoes V-multi/V-adj/V-R10/
|  V-blocked/V-tier2 RAW n+6 x3 + round-trip, V-exact COMP + rt.
|  P0-non-growth: zero accept->refuse flips anywhere (all B1/Q1/
|  C21/wrap/underlong refuse-pins still green); c19 accept-only
|  holds, adds zero refuses.
|  [T6] `make smoke` verdict PASS (e05, oracle live). RACER RE-CHECK
|  at close: lane code mtimes stable (enc 11:49:48, dec 11:23:17,
|  0 u6x refs in lane dec) -> zero u6x bytes regardless of u6x-log
|  movement (12:08->12:13, code untouched).
|  Close: enc == u32-final verbatim (5735 lines / 206343 bytes);
|  dec == c19-final + M23 line (1767 lines); port-vs-lane residual
|  is EXACTLY the 1 M23 comment line. Zero merge edits to logic.
|  Zero files kept from probes. No commits.
|  Remaining lane-owned: u31 to Final (then merge); m24/m26 pin
|  flips to accept/COMP-exact (test lane); R10-47-valid rerun at
|  battery (u6x rework owns the Q1/B1 narrow); U23-TOKREPEAT;
|  u21/u23 rem<9 analogs; k>10/lit-HUF + e00-E1; seq/text general
|  COMP (Huffman); U18-PMAX p33+; R4/R5/R7/R8 battery-rerun closure
|  (E1 growth + ZA/B1 row flips decided there).
|- MERGE-M36 Round-24 merge (dec-u6x R10 P0 over-fire refine, 2026-09-17):
|  [T1 FLUSH] u6x VERDICT U6X-REFINE LIVE verified (GAPLOG-u6x L93-94:
|  VALIDS 47/47 ret==raw + REV-8 8/8 + o79-shape + L0cap + MUT-SPOT
|  43/43 + freeze + compile clean). Port-vs-lane dec diff pre-merge
|  = EXACTLY H1 (F7 Q1-51B pre-gate base) + HUNK-A (keyed N-only
|  repair) + F9c (59/75 ctx-arm) + H2 (F7 B1 modes narrow) + HUNK-B
|  (B1 tc==1) + standing M23 comment line (118 diff lines). Port dec
|  1767 lines, lane 1879. Enc: port==lane byte-identical (cmp
|  silent, 5735 lines both); enc lane GAPLOG-pack1 is packer-a4
|  survey/planning-only (no emission) -> enc untouched this round.
|  [T2 FLUSH] dec wholesale cp lane->port + M23 1-line re-apply
|  (port wording "both callers reject" kept); residual vs lane
|  EXACTLY the M23 line (1317c1317). Port dec now 1879 lines /
|  68842 B. Lane mtime 12:54, stable. Zero other merge edits.
|  [T3] `make clean` + `make all` exit 0, zero warnings (-O2 -std=c11
|  -Wall -Wextra). clean removed results/ + unit binaries (standard).
|  [T4] `make unit` exit 0 (direct full loop, all 34 binaries): 933
|  PASS / 0 FAIL — testlane-green baseline reproduced EXACTLY (M35's
|  925/8 intended flips now pins: m24-u6q-dec 19/0, m26-shaper12b-
|  veto 24/0; q1-gate 20/0 + q1-skip 17/0 + m31-b1-len 19/0 green).
|  ANY-regression rule not triggered: zero FAILs, no revert needed.
|  [T4b] `make selftest` OK. [T5 LOAD-BEARING redrive, transient
|  probe vs PORT-built decoder (redrive.c + port sources, cap
|  70000, rm after, zero files kept)] R10-47 valids 47/47 ACCEPT
|  ret==raw EXACT (vs added_r10.log) + REV-8 8/8 refused (incl all
|  six 51B rows: 3 sig-path + 3 keyed-path = L0cap ARM cover) +
|  o79-shape 6/6 identical to lane a8 (all refuse 0/0) + MUT-SPOT
|  43/43 accept (all oracle=ok, zero wrong-direction). Verdict-r18
|  proof bar met on the merged binary.
|  [T6] `make smoke` verdict PASS (e05, oracle live). RACER
|  RE-CHECK at close: dec lane stable (12:54, merge source); enc
|  lane MOVED mid-window (11:49 -> 13:03, 5735 -> 5983 lines,
|  packer-a4 P1 header-writer code, in-lane smoke OK, proof owed
|  later rounds) -> NOT merged per round scope (enc survey-only),
|  port enc stays u32-final 5735 lines / 206343 B.
|  Close: dec == u6x-final + M23 line (1879 lines / 68842 B);
|  residual vs lane EXACTLY the M23 comment line. Enc untouched.
|  Zero files kept from probes. No commits (not a git repo).
|  Remaining lane-owned: packer-a4 P2-P4 + proof (then merge);
|  u31 to Final (then merge); 62880B scratch-ceiling redrive gap
|  (no such vector in rig, F8 gate-level only); B1 hi-leg vs Q5(b)
|  no-cap tension untouched (zero tc==1 large-L R10 vectors);
|  keyed-context sets n=small (revisit on new N-evidence); R10-47
|  battery rerun; U23-TOKREPEAT; u21/u23 rem<9 analogs; k>10/
|  lit-HUF + e00-E1; U18-PMAX p33+; R4/R5/R7/R8 battery-rerun
|  closure.
