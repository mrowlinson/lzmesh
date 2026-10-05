#!/bin/sh
# h2-stage1.sh: H2 control-A reproduction (local, non-timing, ZERO Air).
# Method: H5/traincorp + flex7 stage1b pins. Proves rig equivalence before
# any H2 fresh arm runs: profdata md5 must == 98cfccfe (flex7 pin), h3 census must ==
# 1455/165/120/109/347/33. Run from worktree root:
#   sh port/bench/air/r27flex9-h2/h2-stage1.sh
# Output: port/bench/air/r27flex9-h2/ab-stage1/ (logs+txt committed; bins rm'd).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$HERE/ab-stage1
UFLAGS="-DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9"
CFLAGS="-O2 -std=c11 -Wall -Wextra -I$P/include"
[ -f "$HERE/enc-union.c" ] || { echo NEED-ENC-UNION-HALT; exit 1; }
[ "$(md5 -q "$HERE/enc-union.c" | cut -c1-8)" = "3860c43c" ] || { echo ENC-UNION-MD5-HALT; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT" "$OUT/pgo-gen-A" "$OUT/prof-A"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
echo "== A control build+train (T0 verbatim) =="
# shellcheck disable=SC2086
cc $CFLAGS $UFLAGS -fprofile-instr-generate -c -o "$OUT/pgo-gen-A/enc.o" "$HERE/enc-union.c" 2> "$OUT/build-A.log" || { echo A-BUILD-FAIL; exit 1; }
cc $CFLAGS -fprofile-instr-generate -c -o "$OUT/pgo-gen-A/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-A.log" || { echo A-BUILD-FAIL; exit 1; }
# shellcheck disable=SC2086
cc $CFLAGS $UFLAGS -fprofile-instr-generate -o "$OUT/bench-gen-A" "$OUT/pgo-gen-A/enc.o" "$OUT/pgo-gen-A/dec.o" "$P/bench/bench.c" 2>> "$OUT/build-A.log" || { echo A-LINK-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof-A/full-%p.profraw" "$OUT/bench-gen-A" -n 3 $CORPUS > /dev/null || { echo A-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof-A/l0-%p.profraw" "$OUT/bench-gen-A" -n 200 -l 0 $CORPUS > /dev/null || { echo A-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/A.profdata" "$OUT"/prof-A/*.profraw || { echo A-MERGE-FAIL; exit 1; }
md5 -q "$OUT/A.profdata" | tee "$OUT/A.profdata.md5"
[ "$(cut -c1-8 "$OUT/A.profdata.md5")" = "98cfccfe" ] || { echo A-PROFDATA-MISMATCH-HALT; exit 1; }
echo "A-PROFDATA-EXACT-OK (flex7 stage1b-recipe pin; memo Makefile-recipe pin 5fb4522e differs by rig lineage, census binds)"
echo "== A -S h3 census =="
cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/A.profdata" -S -o "$OUT/enc-A.S" "$HERE/enc-union.c" 2>/dev/null || { echo S-A-FAIL; exit 1; }
python3 "$HERE/h3count.py" "$OUT/enc-A.S" lzmesh_h3_split | tee "$OUT/h3-A.txt"
grep -q "lines=1455 ld=165 st=120 cbr=109 mov=347 bl=33" "$OUT/h3-A.txt" || { echo A-CENSUS-MISMATCH-HALT; exit 1; }
echo "A-CENSUS-EXACT-OK"
echo "== PGO-IDENT A (std vs pgo) =="
cc $CFLAGS $UFLAGS -c -o "$OUT/enc-u.o" "$HERE/enc-union.c" 2>/dev/null || exit 1
cc $CFLAGS -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2>/dev/null || exit 1
cc $CFLAGS -c -o "$OUT/cli.o" "$P/src/port_cli.c" 2>/dev/null || exit 1
cc $CFLAGS -o "$OUT/cli-Astd" "$OUT/enc-u.o" "$OUT/dec.o" "$OUT/cli.o" 2>/dev/null || exit 1
mkdir -p "$OUT/pgo-use-A"
cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/A.profdata" -c -o "$OUT/pgo-use-A/enc.o" "$HERE/enc-union.c" 2>> "$OUT/build-A.log" || exit 1
cc $CFLAGS "-fprofile-instr-use=$OUT/A.profdata" -c -o "$OUT/pgo-use-A/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-A.log" || exit 1
cc $CFLAGS "-fprofile-instr-use=$OUT/A.profdata" -c -o "$OUT/pgo-use-A/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-A.log" || exit 1
cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/A.profdata" -o "$OUT/cli-Apgo" "$OUT/pgo-use-A/enc.o" "$OUT/pgo-use-A/dec.o" "$OUT/pgo-use-A/cli.o" 2>> "$OUT/build-A.log" || exit 1
echo "A-warnings=$(grep -ci warning "$OUT/build-A.log" || true)"
md5 -q "$OUT/cli-Apgo" | tee "$OUT/bin-Apgo.md5"
: > "$OUT/pgoident-A.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-Astd" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-Apgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident-A.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident-A.txt"; fi
    "$OUT/cli-Astd" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-Apgo" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
    if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/pgoident-A.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/pgoident-A.txt"; fi
  done
done
echo "PGO-IDENT-A-ENC: $(grep -c enc-IDENT "$OUT/pgoident-A.txt")/12"
echo "PGO-IDENT-A-DEC: $(grep -c dec-IDENT "$OUT/pgoident-A.txt")/12"
[ "$(grep -c enc-IDENT "$OUT/pgoident-A.txt")" = "12" ] || { echo PGO-IDENT-A-ENC-FAIL; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/pgoident-A.txt")" = "12" ] || { echo PGO-IDENT-A-DEC-FAIL; exit 1; }
echo PGO-IDENT-A-24/24-OK
echo "== cleanup bins (pins recorded) =="
rm -f "$OUT"/cli-* "$OUT"/bench-gen-* "$OUT"/*.o "$OUT"/pgo-gen-*/*.o "$OUT"/pgo-use-*/*.o "$OUT"/n.bin "$OUT"/g.bin "$OUT"/nd.bin "$OUT"/gd.bin
rm -rf "$OUT"/prof-* "$OUT"/pgo-gen-* "$OUT"/pgo-use-*
echo R27FLEX9-H2-STAGE1-DONE
