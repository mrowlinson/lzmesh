#!/bin/sh
# h2-stage2.sh: H2 fresh arm (local, non-timing, ZERO Air). GATED: runs only
# under PRE-REG-R27-FLEX9-H2 BINDING (ANSWER-r27-flex9-1 or SLA-default).
# Usage: sh h2-stage2.sh [seed-offset] [nseeds]   (default: 21 4)
# Recipe otherwise H5-verbatim: per-input files, -n 3 + L0 boost -n 200 -l 0.
# Run from worktree root. Output: ab-stage2/ (logs+txt committed; bins rm'd).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$HERE/ab-stage2
SEED_OFF=${1:-21}
NSEEDS=${2:-4}
UFLAGS="-DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9"
CFLAGS="-O2 -std=c11 -Wall -Wextra -I$P/include"
[ -f "$HERE/enc-union.c" ] || { echo NEED-ENC-UNION-HALT; exit 1; }
[ -f "$HERE/ab-stage1/h3-A.txt" ] || { echo NEED-STAGE1-HALT; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT" "$OUT/pgo-gen-B" "$OUT/prof-B" "$OUT/fresh-corpus"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
echo "== B fresh corpus (seeds $SEED_OFF..$((SEED_OFF + NSEEDS - 1))) =="
python3 "$HERE/mk_fresh.py" "$P/tests/battery" "$SEED_OFF" "$NSEEDS" "$OUT/fresh-corpus" > "$OUT/fresh-manifest.tsv" 2> "$OUT/fresh-manifest.err"
cat "$OUT/fresh-manifest.err"
echo "== B build+train (H5-verbatim recipe) =="
# shellcheck disable=SC2086
cc $CFLAGS $UFLAGS -fprofile-instr-generate -c -o "$OUT/pgo-gen-B/enc.o" "$HERE/enc-union.c" 2> "$OUT/build-B.log" || { echo B-BUILD-FAIL; exit 1; }
cc $CFLAGS -fprofile-instr-generate -c -o "$OUT/pgo-gen-B/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-B.log" || { echo B-BUILD-FAIL; exit 1; }
# shellcheck disable=SC2086
cc $CFLAGS $UFLAGS -fprofile-instr-generate -o "$OUT/bench-gen-B" "$OUT/pgo-gen-B/enc.o" "$OUT/pgo-gen-B/dec.o" "$P/bench/bench.c" 2>> "$OUT/build-B.log" || { echo B-LINK-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof-B/full-%p.profraw" "$OUT/bench-gen-B" -n 3 "$OUT"/fresh-corpus/*.bin > /dev/null || { echo B-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof-B/l0-%p.profraw" "$OUT/bench-gen-B" -n 200 -l 0 "$OUT"/fresh-corpus/*.bin > /dev/null || { echo B-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/B.profdata" "$OUT"/prof-B/*.profraw || { echo B-MERGE-FAIL; exit 1; }
md5 -q "$OUT/B.profdata" | tee "$OUT/B.profdata.md5"
echo "== B -S census (G2) + whole-file (G3) =="
cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/B.profdata" -S -o "$OUT/enc-B.S" "$HERE/enc-union.c" 2>/dev/null || { echo S-B-FAIL; exit 1; }
python3 "$HERE/h3count.py" "$OUT/enc-B.S" lzmesh_h3_split | tee "$OUT/h3-B.txt"
python3 "$HERE/scount.py" "$OUT/enc-B.S" | tee "$OUT/whole-B.txt"
python3 "$HERE/scount.py" "$OUT/../ab-stage1/enc-A.S" | tee "$OUT/whole-A.txt"
echo "A: $(cat "$OUT/../ab-stage1/h3-A.txt")"
echo "B: $(cat "$OUT/h3-B.txt")"
echo "== G1 PGO-IDENT B (std vs pgo) =="
mkdir -p "$OUT/pgo-use-B"
cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/B.profdata" -c -o "$OUT/pgo-use-B/enc.o" "$HERE/enc-union.c" 2>> "$OUT/build-B.log" || exit 1
cc $CFLAGS "-fprofile-instr-use=$OUT/B.profdata" -c -o "$OUT/pgo-use-B/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-B.log" || exit 1
cc $CFLAGS "-fprofile-instr-use=$OUT/B.profdata" -c -o "$OUT/pgo-use-B/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-B.log" || exit 1
cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/B.profdata" -o "$OUT/cli-Bpgo" "$OUT/pgo-use-B/enc.o" "$OUT/pgo-use-B/dec.o" "$OUT/pgo-use-B/cli.o" 2>> "$OUT/build-B.log" || exit 1
cc $CFLAGS $UFLAGS -c -o "$OUT/enc-u.o" "$HERE/enc-union.c" 2>/dev/null || exit 1
cc $CFLAGS -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2>/dev/null || exit 1
cc $CFLAGS -c -o "$OUT/cli.o" "$P/src/port_cli.c" 2>/dev/null || exit 1
cc $CFLAGS -o "$OUT/cli-Bstd" "$OUT/enc-u.o" "$OUT/dec.o" "$OUT/cli.o" 2>/dev/null || exit 1
echo "B-warnings=$(grep -ci warning "$OUT/build-B.log" || true)"
md5 -q "$OUT/cli-Bpgo" | tee "$OUT/bin-Bpgo.md5"
: > "$OUT/pgoident-B.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-Bstd" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-Bpgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident-B.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident-B.txt"; fi
    "$OUT/cli-Bstd" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-Bpgo" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
    if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/pgoident-B.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/pgoident-B.txt"; fi
  done
done
echo "PGO-IDENT-B-ENC: $(grep -c enc-IDENT "$OUT/pgoident-B.txt")/12"
echo "PGO-IDENT-B-DEC: $(grep -c dec-IDENT "$OUT/pgoident-B.txt")/12"
[ "$(grep -c enc-IDENT "$OUT/pgoident-B.txt")" = "12" ] || { echo G1-ENC-FAIL; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/pgoident-B.txt")" = "12" ] || { echo G1-DEC-FAIL; exit 1; }
echo G1-24/24-OK
echo "== cleanup bins (pins recorded; corpus kept? NO — manifest kept, bins rm'd) =="
rm -f "$OUT"/cli-* "$OUT"/bench-gen-* "$OUT"/*.o "$OUT"/pgo-gen-*/*.o "$OUT"/pgo-use-*/*.o "$OUT"/n.bin "$OUT"/g.bin "$OUT"/nd.bin "$OUT"/gd.bin
rm -rf "$OUT"/prof-* "$OUT"/pgo-gen-* "$OUT"/pgo-use-* "$OUT"/fresh-corpus
echo R27FLEX9-H2-STAGE2-DONE
