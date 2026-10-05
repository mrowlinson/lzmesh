#!/bin/sh
# layenc-stage1c.sh: T0-corpus sensitivity screen (local, non-timing).
# Q: does train CORPUS (not layout) move T0+PGO h3 shape? (memo H5 direction.)
# Arms (union vehicle, no ALIGN): T0-verbatim [have: ab-stage1b] vs zeros-only
# train vs text-only train. Each: train bench-gen, merge, -S, h3count.py.
# Verdict context: FORMAL bounds ld<193/cbr<=117/st<=180/mov<=520/bl==33.
# Run from worktree root. Output: ab-stage1c/ (logs+txt committed; bins rm'd).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$HERE/ab-stage1c
UFLAGS="-DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9"
CFLAGS="-O2 -std=c11 -Wall -Wextra -I$P/include"
[ -f "$HERE/enc-union.c" ] || { echo NEED-STAGE1-HALT; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
# shellcheck disable=SC2086
cc $CFLAGS $UFLAGS -fprofile-instr-generate -c -o "$OUT/enc.o" "$HERE/enc-union.c" 2> "$OUT/build-gen.log" || { echo GEN-BUILD-FAIL; exit 1; }
cc $CFLAGS -fprofile-instr-generate -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-gen.log" || { echo GEN-BUILD-FAIL; exit 1; }
cc $CFLAGS $UFLAGS -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/enc.o" "$OUT/dec.o" "$P/bench/bench.c" 2>> "$OUT/build-gen.log" || { echo GEN-LINK-FAIL; exit 1; }
echo "gen-warnings=$(grep -ci warning "$OUT/build-gen.log" || true)"
train_arm() {
  # $1 = tag, $2 = corpus files, $3 = extra bench flags
  mkdir -p "$OUT/prof-$1"
  # shellcheck disable=SC2086
  LLVM_PROFILE_FILE="$OUT/prof-$1/p-%p.profraw" "$OUT/bench-gen" -n 3 $3 $2 > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$OUT/$1.profdata" "$OUT"/prof-$1/*.profraw || { echo "$1-MERGE-FAIL"; exit 1; }
  md5 -q "$OUT/$1.profdata" | tee "$OUT/$1.profdata.md5"
  # shellcheck disable=SC2086
  cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/$1.profdata" -S -o "$OUT/enc-$1.S" "$HERE/enc-union.c" 2> "$OUT/build-$1.log" || { echo "$1-S-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  python3 "$HERE/h3count.py" "$OUT/enc-$1.S" lzmesh_h3_split | tee "$OUT/h3-$1.txt"
}
C="$P/bench/corpus"
train_arm zerosonly "$C/zeros-64k.bin" ""
train_arm textonly "$C/text-256k.bin" ""
echo "T0-ref: func=lzmesh_h3_split lines=1455 ld=165 st=120 cbr=109 mov=347 bl=33"
echo "bounds: ld<193 cbr<=117 st<=180 mov<=520 bl==33"
rm -f "$OUT"/bench-gen "$OUT"/*.o
rm -rf "$OUT"/prof-*
echo R27FLEX7-LAYENC-STAGE1C-DONE
