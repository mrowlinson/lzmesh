# UNITLOG — Stage-6 unithuff (Huffman unit tests)

Lane: cleanroom-re / test-writer unithuff. Public API only (`lzmesh.h`);
no lane `.c` read, no port-source contact, no evidence. Oracle input:
black-box E00 bytes + manifest cells only.

## Files added (tests/unit/ only, plus `make unit` wiring in Makefile)

- `test_roundtrip.c` — (a) synthetic roundtrips: 21 shapes x 4 levels
  (0xE00/0xE01/0xE05/0xE09; round-4 re-pin per R-E-B1, was bare 0/1/5/9),
  determinism, sized-scratch, undersize-prefix, refusal, + 11
  invalid-level fail-closed asserts (bare/wide/negative).
- `test_e00_vectors.c` — (b) Apple E00-vector decode over vendored fixtures.
- `vectors/*.bin` (10 files) + `vectors/manifest.txt` — vendored oracle
  outputs; every input verified pure-zero
  (`sha256(n zero bytes) == manifest cell input_sha`), so expected
  plaintext is exactly n zero bytes.
- Makefile: `unit` target (builds `tests/unit/test_*`, runs each under
  `set -e`), `.PHONY`, `clean` removes unit binaries.

## Run now (2026-09-17): `make unit` — exit 0

- test_roundtrip: pass=112 fail=0 xfail=8
- test_e00_vectors: pass=4 fail=0 xfail=6 xpass=0
- Total: 116 pass, 0 fail, 14 xfail, 0 xpass. Zero compiler warnings
  (`-O2 -std=c11 -Wall -Wextra`).

## XFAILs (all with reason, none silent)

- 8x `comp-emit` (roundtrip): port encoder is raw-only (`outlen = n+6`
  at every level, e.g. zeros-4096 -> 4102, fib20 -> 28661);
  flips to PASS when comp emission lands. Reason:
  "raw-only; comp emission pending".
- 6x `e00 n{000022,000032,000100,001000,004096,032768}` (vectors):
  port decoder returns 0 on comp blocks. Reason: "decoder COMP pending".
  Guard: nonzero-but-wrong comp output would be a hard FAIL (corrupt,
  not pending) — not observed. No hangs (all comp decodes return fast).
- XPASS (comp vectors) stays green by design so COMP landing is visible
  in the log without breaking the gate.

## Observed decoder state (via public API / CLI probes)

- raw/end vectors exact (n = 0,1,8,21); empty anchor holds both ways
  (`enc(empty) = FF`, `dec(FF) = 0 bytes`, exit 0).
- `decoded_size` already walks comp framing correctly (all 6 comp
  vectors report exact n); only entropy decode is pending.
- Refusals verified: dropped end marker, 1-byte header, empty source
  all return 0; undersized dst returns capacity + correct prefix.

## S3.7 mapping (expected properties -> tests)

maxlen 10/5 + package-merge/quantum rebuild: `skew-exp64`
(exponential-decay counts), `fib20` (Fibonacci counts), `rare1`
(single-rare-symbol) shapes stress deep-tree/length-clamp paths through
exact-roundtrip assertions. Kraft-exact gate: `uni-{2,4,16,256}`
(power-of-two uniform) vs `uni-{3,5,17}` (off-boundary) pairs.
Degenerate nSym==1: `n1-*`, `arun-1000`, `zeros-*`. Canonical LSB-first
+ table-index decode: covered black-box by oracle comp-vector decode
(XFAIL until COMP lands) — no internal-structure assertions, per API.

## Deferred (not silent)

- Bad-tag / forged-grammar decode: no assertion here (tag grammar beyond
  observed raw/comp/end is unknown to this lane); battery ADV tier owns it.
- `make unit` must run from repo root (fixture-relative paths); missing
  fixtures are hard FAILs (exit 1 verified), never skips.

## Procedure

## Round-4 re-pin (2026-09-17, R-E-B1 SPEC-WINS)

- LEVELS bare {0,1,5,9} -> full-form {0xE00,0xE01,0xE05,0xE09}; case
  names `L%x`; + `invalid_level_one` x 11 (bare 0/1/5/9, 0xE02, 0xE0F,
  0x100, 0x1E05, 0x100E05, -1, -5: encode 0 + scratch 0 at n=32 and
  n=0). `make unit` exit 0: roundtrip 127/0/4 (was 116/0/4, +11 new
  PASS), e00-vectors 4/0/0/6 unchanged.

## Round-4+ extension (2026-09-17, R-E-B1 / Huffman / COMP / MTF)

New files (tests/unit/ only; no Makefile edit — `unit` auto-discovers
`test_*.c`). All generate vectors at runtime via the public API (no new
fixtures, no oracle reads); C11 `-Wall -Wextra` clean.

- `test_boundary.c` (64 PASS) — full-only boundary (S1.2/S1.3 Q20):
  43 invalid ids (bare 0..15, E02-E0F rejects, wide incl
  (int)0x80000E05, negatives incl INT_MIN, INT_MAX) each encode-0 +
  scratch-0 at n=32 and n=0; exact scratch table (S1.5: dec 65536,
  enc 323468/1372044/1388428/8728460); valid n=0 -> 1B ff;
  enc1('A') exact 7B all levels (S1.7); dec(ff)=0 + sizer(ff)=0;
  NULL edges fail-closed no crash (App C M1/Q11); encode caps 1..10
  all 0 (S4.5); big-dst returns ds.
- `test_framing.c` (18 PASS) — tags 02/03/7F/80/FE reject (S2.1);
  hand-forged RAW-1000/RAW-1 exact (S2.2/S2.7); trailing divergence
  sizer-0 + decoder-exact for RAW+1/+5 and COMP+2 (S2.4/Q8 pin);
  header gates C2/C4/C5/C6 incl fo==ds strict (S2.3); truncation.
- `test_huff_suffix.c` (18 PASS) — base zeros-22 e05 fresh-22 shape
  pin (23B, modes 0x49, tok1/len1/lit1/dist0; S3.13); lit/tok/len
  ->HUFFMAN over empty lanes fail-closed (S3.8); reserved modes
  3-7 reject (S3.2/S7.2); count0 REPEAT/HUFFMAN reject (S2.6); C8..C12
  decoder-only split proof sizer-22 + decode-0 (S2.5/Q12 + S1.8/Q9);
  footer hi-bits ignored byte-exact (S7.2); hand-forged C13 new-dist
  d=2>w=1 sizer-20 + decode-0 (S4.3/S3.11).
- `test_comp_roundtrip.c` (82 PASS + 10 XFAIL) — zero-run roundtrips
  all levels n=0..32768 + L5-62881 exact; outlen pins (n21 RAW 27B,
  n22 COMP 23B all levels; n264 L0 27B vs L1/L5/L9 23B; n265 27B);
  TIER-2 note: n22/23 kept COMP despite total>=input (tag gate, not
  size gate); L0 62880 PASS, L0 62881/2 XFAIL fail-closed-0 (M12:
  needs enc cap/split, no misdecode); fib20/text-like non-run emit
  XFAIL x8 (E-B2 general COMP pending).
- `test_mtf.c` (5 PASS + 2 XFAIL) — rep3 accounting-preserving accept
  on L5-match (0x07->0x1F) and L0 (0xC0->0xD8) run shapes (S3.9/Q17,
  P-D9); multi RAW+RAW / 3xRAW / COMP+RAW exact (S2.8/S3.12);
  RAW+COMP and COMP+COMP XFAIL (walk sums, decode fail-closed 0;
  sound: each block valid alone; oracle L0 multi decodes fine, so
  port-shape non-first COMP with a match is the gap).

`make unit` exit 0: 318 pass, 0 fail, 16 xfail, 6 xpass (roundtrip
127/0/4 + e00 4/0/0/6 unchanged; new: boundary 64/0, framing 18/0,
huff-suffix 18/0, comp 82/0/10, mtf 5/0/2). `make selftest` OK.
`make smoke` verdict PASS (1232 cells, 0 fail). Full tier not run
(~100x smoke time).

Findings for lanes (probed via public API, throwaway probes removed):
- Multi-token RAW/REPEAT single-block forges (2-token all-rep, 2token
  new-dist) and minimal suffix-bit forge (sb1/d13 with 5B lane region)
  return decode 0 with sizer ok. Forge-vs-port undetermined (Huffman
  multi-token + oracle L0 multi both work, so not all multi-token is
  broken); exact suffix-bit vectors await a verified lane-region forge
  or oracle vectors — NOT committed as XFAIL (unsound until verified).
- Non-first COMP with a match fails (RAW+COMP, COMP+COMP, cross with
  oracle blk0) while oracle blk0+blk1 works: port block-loop gap,
  committed as sound XFAILs above. Single oracle blk1 alone also
  fail-closes (cross-block match by design: needs blk0 window).

## Procedure (original run)

1. Built port (`make`), probed `port_cli enc/dec` per level: encoder
   raw-only, decoder raw-ok / comp-returns-0 / no hangs (5 s alarm guard).
2. Scanned oraclecache manifest cells for pure-zero E00 inputs; vendored
   10 spanning the raw/comp split (21/22) up to n=32768, with out-sha +
   FNV integrity pins.
3. Wrote both C binaries, wired `make unit`, ran: green with 14 pending
   xfails; negative-checked exit 1 on missing fixtures.
