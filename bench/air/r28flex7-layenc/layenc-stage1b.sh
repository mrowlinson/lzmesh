#!/bin/sh
# layenc-stage1b.sh: R28 tip PGO-pairs + tripwire-path validation (local, non-timing).
# Mirror of R27 layenc-stage1b.sh minus vehicle: tip own-T0 + lay cross-T0
# (tip profile) + lay own-T0 fallback + u37 counts + PGO-IDENT x2.
# Proves: (1) tripwire PASS path on R28 tip, (2) lay op-IDENTITY under PGO,
# (3) warn profile. Bounds-gating awaits Q1b (counts recorded, not judged).
# Run from worktree root: sh port/bench/air/r28flex7-layenc/layenc-stage1b.sh
# Output: port/bench/air/r28flex7-layenc/ab-stage1b/ (logs+txt committed; bins rm'd).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$HERE/ab-stage1b
ALIGN="-falign-functions=64 -falign-loops=64"
CFLAGS="-O2 -std=c11 -Wall -Wextra -I$P/include"
PRISTINEQ=3fec8aa3274f901b1372be85936a77a2
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== pins =="
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$PRISTINEQ" ] || { echo PRISTINE-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
echo "== build std (tip + lay; dec .o shared) =="
# shellcheck disable=SC2086
cc $CFLAGS -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2> "$OUT/build-dec.log" || { echo DEC-BUILD-FAIL; exit 1; }
cc $CFLAGS -c -o "$OUT/enc-tip.o" "$P/src/lzmesh_enc.c" 2> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc $CFLAGS $ALIGN -c -o "$OUT/enc-lay.o" "$P/src/lzmesh_enc.c" 2> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc $CFLAGS -c -o "$OUT/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-dec.log" || { echo CLI-BUILD-FAIL; exit 1; }
cc $CFLAGS -o "$OUT/cli-tip" "$OUT/enc-tip.o" "$OUT/dec.o" "$OUT/cli.o" 2>> "$OUT/build-tip.log" || { echo TIP-LINK-FAIL; exit 1; }
cc $CFLAGS -o "$OUT/cli-lay" "$OUT/enc-lay.o" "$OUT/dec.o" "$OUT/cli.o" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
echo STD-BUILD-OK
build_pgo() {
  # $1 = tag, $2 = align flags, $3 = prof ("OWN"/path)
  mkdir -p "$OUT/pgo-gen-$1" "$OUT/prof-$1"
  # shellcheck disable=SC2086
  cc $CFLAGS $2 -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/enc.o" "$P/src/lzmesh_enc.c" 2> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $2 -fprofile-instr-generate -o "$OUT/bench-gen-$1" "$OUT/pgo-gen-$1/enc.o" "$OUT/pgo-gen-$1/dec.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
  LLVM_PROFILE_FILE="$OUT/prof-$1/full-%p.profraw" "$OUT/bench-gen-$1" -n 3 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$OUT/prof-$1/l0-%p.profraw" "$OUT/bench-gen-$1" -n 200 -l 0 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$OUT/$1.profdata" "$OUT"/prof-$1/*.profraw || { echo "$1-MERGE-FAIL"; exit 1; }
  md5 -q "$OUT/$1.profdata" | tee "$OUT/$1.profdata.md5"
  if [ "$3" = "OWN" ]; then USE="$OUT/$1.profdata"; else USE="$3"; fi
  mkdir -p "$OUT/pgo-use-$1"
  # shellcheck disable=SC2086
  cc $CFLAGS $2 "-fprofile-instr-use=$USE" -c -o "$OUT/pgo-use-$1/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS "-fprofile-instr-use=$USE" -c -o "$OUT/pgo-use-$1/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS "-fprofile-instr-use=$USE" -c -o "$OUT/pgo-use-$1/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $2 "-fprofile-instr-use=$USE" -o "$OUT/cli-$1" "$OUT/pgo-use-$1/enc.o" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/cli.o" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  md5 -q "$OUT/cli-$1" | tee "$OUT/bin-$1.md5"
  echo "$1-BUILD-OK"
}
echo "== tip own-T0 =="
build_pgo tippgo "" "OWN"
echo "== lay cross-T0 (tip profile) =="
mkdir -p "$OUT/pgo-use-laypgo-cross"
# shellcheck disable=SC2086
cc $CFLAGS $ALIGN "-fprofile-instr-use=$OUT/tippgo.profdata" -c -o "$OUT/pgo-use-laypgo-cross/enc.o" "$P/src/lzmesh_enc.c" 2> "$OUT/build-laypgo-cross.log" || { echo laypgo-cross-BUILD-FAIL; exit 1; }
cc $CFLAGS "-fprofile-instr-use=$OUT/tippgo.profdata" -c -o "$OUT/pgo-use-laypgo-cross/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-laypgo-cross.log" || { echo laypgo-cross-BUILD-FAIL; exit 1; }
cc $CFLAGS "-fprofile-instr-use=$OUT/tippgo.profdata" -c -o "$OUT/pgo-use-laypgo-cross/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-laypgo-cross.log" || { echo laypgo-cross-BUILD-FAIL; exit 1; }
# shellcheck disable=SC2086
cc $CFLAGS $ALIGN "-fprofile-instr-use=$OUT/tippgo.profdata" -o "$OUT/cli-laypgo-cross" "$OUT/pgo-use-laypgo-cross/enc.o" "$OUT/pgo-use-laypgo-cross/dec.o" "$OUT/pgo-use-laypgo-cross/cli.o" 2>> "$OUT/build-laypgo-cross.log" || { echo laypgo-cross-LINK-FAIL; exit 1; }
echo "laypgo-cross-warnings=$(grep -ci warning "$OUT/build-laypgo-cross.log" || true)"
md5 -q "$OUT/cli-laypgo-cross" | tee "$OUT/bin-laypgo-cross.md5"
echo LAYPGO-CROSS-BUILD-OK
echo "== tripwire (lay-std vs lay-cross) =="
: > "$OUT/tripwire.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-lay" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-laypgo-cross" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/tripwire.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/tripwire.txt"; fi
    "$OUT/cli-lay" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-laypgo-cross" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
    if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/tripwire.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/tripwire.txt"; fi
  done
done
echo "TRIPWIRE-ENC: $(grep -c enc-IDENT "$OUT/tripwire.txt")/12"
echo "TRIPWIRE-DEC: $(grep -c dec-IDENT "$OUT/tripwire.txt")/12"
if [ "$(grep -c enc-IDENT "$OUT/tripwire.txt")" = "12" ] && [ "$(grep -c dec-IDENT "$OUT/tripwire.txt")" = "12" ]; then
  echo TRIPWIRE-PASS-single-T0-VALID
else
  echo TRIPWIRE-DIV-NOTE
fi
echo "== lay own-T0 (fallback arm, always built for counts) =="
build_pgo laypgo-own "$ALIGN" "OWN"
echo "== u37 counts (T0+PGO -S) =="
cc $CFLAGS "-fprofile-instr-use=$OUT/tippgo.profdata" -S -o "$OUT/enc-tippgo.S" "$P/src/lzmesh_enc.c" 2>/dev/null || { echo S-FAIL; exit 1; }
cc $CFLAGS $ALIGN "-fprofile-instr-use=$OUT/tippgo.profdata" -S -o "$OUT/enc-laypgo-cross.S" "$P/src/lzmesh_enc.c" 2>/dev/null || { echo S-FAIL; exit 1; }
cc $CFLAGS $ALIGN "-fprofile-instr-use=$OUT/laypgo-own.profdata" -S -o "$OUT/enc-laypgo-own.S" "$P/src/lzmesh_enc.c" 2>/dev/null || { echo S-FAIL; exit 1; }
{
  python3 "$HERE/fncount.py" "$OUT/enc-tippgo.S" lzmesh_u37_parse
  python3 "$HERE/fncount.py" "$OUT/enc-laypgo-cross.S" lzmesh_u37_parse
  python3 "$HERE/fncount.py" "$OUT/enc-laypgo-own.S" lzmesh_u37_parse
} | tee "$OUT/u37-counts.txt"
echo "== PGO-IDENT x2 =="
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
pgoident tip tippgo tip
pgoident lay laypgo-own layown
pgoident lay laypgo-cross laycross
for f in "$OUT"/build-*.log; do echo "$(basename "$f") warnings=$(grep -ci warning "$f" || true)"; done > "$OUT/warnings.txt"
cat "$OUT/warnings.txt"
echo "== cleanup bins (pins recorded) =="
rm -f "$OUT"/cli-* "$OUT"/bench-gen-* "$OUT"/*.o "$OUT"/n.bin "$OUT"/g.bin "$OUT"/nd.bin "$OUT"/gd.bin
rm -rf "$OUT"/pgo-gen-* "$OUT"/pgo-use-* "$OUT"/prof-*
echo R28FLEX7-LAYENC-STAGE1B-DONE
