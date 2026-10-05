#!/bin/sh
# layenc-stage1b.sh: PGO-pair validation for enc-side perturb (local, non-timing).
# Mirror of laypad2 PGO arms: unionpgo (enc-union + own T0) + laypgo (enc-union +
# ALIGN + own T0); dec SHARED source/flags, per-arm profile-use compile.
# Proves: (1) PGO builds warn~0, (2) PGO-IDENT 24/24 x2 pairs, (3) T0+PGO
# -S h3_split counts vs ANSWER-r27-flex7-1-FORMAL bounds
# (ld<193 / cbr<=117 / st<=180 / mov<=520 / bl==33).
# Requires: layenc-stage1.sh DONE (enc-union.c generated). Run from worktree root.
# Output: port/bench/air/r27flex7-layenc/ab-stage1b/ (logs+txt committed; bins rm'd).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$HERE/ab-stage1b
ALIGN="-falign-functions=64 -falign-loops=64"
UFLAGS="-DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9"
CFLAGS="-O2 -std=c11 -Wall -Wextra -I$P/include"
[ -f "$HERE/enc-union.c" ] || { echo NEED-STAGE1-HALT; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
build_pgo() {
  # $1 = tag, $2 = extra enc flags ("" or ALIGN)
  mkdir -p "$OUT/pgo-gen-$1" "$OUT/prof-$1"
  # shellcheck disable=SC2086
  cc $CFLAGS $UFLAGS $2 -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/enc.o" "$HERE/enc-union.c" 2> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $UFLAGS $2 -fprofile-instr-generate -o "$OUT/bench-gen-$1" "$OUT/pgo-gen-$1/enc.o" "$OUT/pgo-gen-$1/dec.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
  LLVM_PROFILE_FILE="$OUT/prof-$1/full-%p.profraw" "$OUT/bench-gen-$1" -n 3 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$OUT/prof-$1/l0-%p.profraw" "$OUT/bench-gen-$1" -n 200 -l 0 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$OUT/$1.profdata" "$OUT"/prof-$1/*.profraw || { echo "$1-MERGE-FAIL"; exit 1; }
  md5 -q "$OUT/$1.profdata" | tee "$OUT/$1.profdata.md5"
  mkdir -p "$OUT/pgo-use-$1"
  # shellcheck disable=SC2086
  cc $CFLAGS $UFLAGS $2 "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/enc.o" "$HERE/enc-union.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $UFLAGS $2 "-fprofile-instr-use=$OUT/$1.profdata" -o "$OUT/cli-$1" "$OUT/pgo-use-$1/enc.o" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/cli.o" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  md5 -q "$OUT/cli-$1" | tee "$OUT/bin-$1.md5"
  echo "$1-BUILD-OK"
}
echo "== PGO builds (own T0 each) =="
build_pgo unionpgo ""
build_pgo laypgo "$ALIGN"
echo "== T0+PGO -S h3 counts vs FORMAL bounds =="
cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/unionpgo.profdata" -S -o "$OUT/enc-unionpgo.S" "$HERE/enc-union.c" 2>/dev/null || { echo S-UNIONPGO-FAIL; exit 1; }
cc $CFLAGS $UFLAGS $ALIGN "-fprofile-instr-use=$OUT/laypgo.profdata" -S -o "$OUT/enc-laypgo.S" "$HERE/enc-union.c" 2>/dev/null || { echo S-LAYPGO-FAIL; exit 1; }
python3 "$HERE/h3count.py" "$OUT/enc-unionpgo.S" lzmesh_h3_split | tee "$OUT/h3-unionpgo.txt"
python3 "$HERE/h3count.py" "$OUT/enc-laypgo.S" lzmesh_h3_split | tee "$OUT/h3-laypgo.txt"
echo "bounds: ld<193 cbr<=117 st<=180 mov<=520 bl==33"
echo "== PGO-IDENT x2 (std vs pgo per vehicle) =="
cp "$OUT/cli-unionpgo" "$OUT/cli-U" 2>/dev/null || true
pgoident() {
  a=$1; b=$2; tag=$3
  : > "$OUT/pgoident-$tag.txt"
  for corp in text-256k mixed-128k zeros-64k; do
    case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
    for lv in e00 e01 e05 e09; do
      "$OUT/cli-$a" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
      "$OUT/cli-$b" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
      if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident-$tag.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident-$tag.txt"; fi
      "$OUT/cli-$a" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
      "$OUT/cli-$b" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
      if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/pgoident-$tag.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/pgoident-$tag.txt"; fi
    done
  done
  echo "PGO-IDENT-$tag-ENC: $(grep -c enc-IDENT "$OUT/pgoident-$tag.txt")/12"
  echo "PGO-IDENT-$tag-DEC: $(grep -c dec-IDENT "$OUT/pgoident-$tag.txt")/12"
}
# std CLIs rebuilt cheap (stage1 rm'd bins)
cc $CFLAGS $UFLAGS -c -o "$OUT/enc-u.o" "$HERE/enc-union.c" 2>/dev/null || exit 1
cc $CFLAGS $UFLAGS $ALIGN -c -o "$OUT/enc-l.o" "$HERE/enc-union.c" 2>/dev/null || exit 1
cc $CFLAGS -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2>/dev/null || exit 1
cc $CFLAGS -c -o "$OUT/cli.o" "$P/src/port_cli.c" 2>/dev/null || exit 1
cc $CFLAGS -o "$OUT/cli-ustd" "$OUT/enc-u.o" "$OUT/dec.o" "$OUT/cli.o" 2>/dev/null || exit 1
cc $CFLAGS -o "$OUT/cli-lstd" "$OUT/enc-l.o" "$OUT/dec.o" "$OUT/cli.o" 2>/dev/null || exit 1
pgoident ustd unionpgo union
pgoident lstd laypgo lay
[ "$(grep -c enc-IDENT "$OUT/pgoident-union.txt")" = "12" ] || { echo PGO-IDENT-UNION-ENC-FAIL; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/pgoident-union.txt")" = "12" ] || { echo PGO-IDENT-UNION-DEC-FAIL; exit 1; }
[ "$(grep -c enc-IDENT "$OUT/pgoident-lay.txt")" = "12" ] || { echo PGO-IDENT-LAY-ENC-FAIL; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/pgoident-lay.txt")" = "12" ] || { echo PGO-IDENT-LAY-DEC-FAIL; exit 1; }
echo PGO-IDENT-2x24/24-OK
echo "== cleanup bins (pins recorded) =="
rm -f "$OUT"/cli-* "$OUT"/bench-gen-* "$OUT"/*.o "$OUT"/pgo-gen-*/*.o "$OUT"/pgo-use-*/*.o "$OUT"/n.bin "$OUT"/g.bin "$OUT"/nd.bin "$OUT"/gd.bin
rm -rf "$OUT"/prof-* "$OUT"/pgo-gen-* "$OUT"/pgo-use-*
echo R27FLEX7-LAYENC-STAGE1B-DONE
