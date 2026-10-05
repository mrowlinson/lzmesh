#!/bin/sh
# layenc-stage1.sh: enc-side layout-perturb INSTRUMENT validation (local, non-timing).
# Mirror of tL5d R24 ALIGN method, flipped to encode: perturb ENC layout
# (-falign-functions=64 -falign-loops=64), DEC .o SHARED to isolate enc layout.
# Vehicle: union (bed-h3.patch 402d7b59 + -DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9).
# Proves: (1) instrument moves layout (bins/-S/nm differ), (2) streams byte-IDENT
# (union vs union-lay 24/24 + tip vs union 24/24), (3) static price preserved
# (-S counts), (4) warn0. Timing PASS rule gated on memo Q1 (PRE-REG draft).
# Run from worktree root: sh port/bench/air/r27flex7-layenc/layenc-stage1.sh
# Output: port/bench/air/r27flex7-layenc/ab-stage1/ (logs+txt committed; bins rm'd).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$HERE/ab-stage1
ALIGN="-falign-functions=64 -falign-loops=64"
UFLAGS="-DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9"
CFLAGS="-O2 -std=c11 -Wall -Wextra -I$P/include"
PRISTINEQ=3fec8aa3274f901b1372be85936a77a2
PATCHQ=402d7b5988e2c28b0d1a669a2d1ca713
UNIONQ=3860c43cdda4a71aa705583effbbdc10
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== pins =="
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$PRISTINEQ" ] || { echo PRISTINE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/../tmp/r26flex2/bed-h3.patch")" = "$PATCHQ" ] || { echo PATCH-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== gen enc-union.c =="
mkdir -p "$OUT/ps/port/src"
cp "$P/src/lzmesh_enc.c" "$OUT/ps/port/src/lzmesh_enc.c"
(cd "$OUT/ps" && patch -s -p1 < "$P/../tmp/r26flex2/bed-h3.patch") || { echo PATCH-APPLY-FAIL-HALT; exit 1; }
mv "$OUT/ps/port/src/lzmesh_enc.c" "$HERE/enc-union.c"
rm -rf "$OUT/ps"
[ "$(md5 -q "$HERE/enc-union.c")" = "$UNIONQ" ] || { echo UNION-PIN-FAIL-HALT; exit 1; }
echo UNION-GEN-OK
echo "== builds =="
# shellcheck disable=SC2086
cc $CFLAGS -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2> "$OUT/build-dec.log" || { echo DEC-BUILD-FAIL; exit 1; }
cc $CFLAGS -c -o "$OUT/enc-tip.o" "$P/src/lzmesh_enc.c" 2> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc $CFLAGS $UFLAGS -c -o "$OUT/enc-union.o" "$HERE/enc-union.c" 2> "$OUT/build-union.log" || { echo UNION-BUILD-FAIL; exit 1; }
cc $CFLAGS $UFLAGS $ALIGN -c -o "$OUT/enc-lay.o" "$HERE/enc-union.c" 2> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc $CFLAGS -c -o "$OUT/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-dec.log" || { echo CLI-BUILD-FAIL; exit 1; }
cc $CFLAGS -o "$OUT/cli-tip" "$OUT/enc-tip.o" "$OUT/dec.o" "$OUT/cli.o" 2>> "$OUT/build-tip.log" || { echo TIP-LINK-FAIL; exit 1; }
cc $CFLAGS -o "$OUT/cli-union" "$OUT/enc-union.o" "$OUT/dec.o" "$OUT/cli.o" 2>> "$OUT/build-union.log" || { echo UNION-LINK-FAIL; exit 1; }
cc $CFLAGS -o "$OUT/cli-lay" "$OUT/enc-lay.o" "$OUT/dec.o" "$OUT/cli.o" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
for t in tip union lay; do
  cc $CFLAGS -o "$OUT/bench-$t" "$P/bench/bench.c" "$OUT/enc-$t.o" "$OUT/dec.o" 2>> "$OUT/build-$t.log" || { echo "$t-BENCH-LINK-FAIL"; exit 1; }
done
for f in "$OUT"/build-*.log; do echo "$(basename "$f") warnings=$(grep -ci warning "$f" || true)"; done > "$OUT/warnings.txt"
cat "$OUT/warnings.txt"
md5 -q "$OUT/cli-tip" | tee "$OUT/bin-tip.md5"
md5 -q "$OUT/cli-union" | tee "$OUT/bin-union.md5"
md5 -q "$OUT/cli-lay" | tee "$OUT/bin-lay.md5"
echo BUILDS-OK
echo "== layout-moved proof (nm addrs + -S diffstat) =="
nm "$OUT/enc-union.o" | grep ' T ' | head -8 | tee "$OUT/nm-union.txt"
nm "$OUT/enc-lay.o" | grep ' T ' | head -8 | tee "$OUT/nm-lay.txt"
cc $CFLAGS $UFLAGS -S -o "$OUT/enc-union.S" "$HERE/enc-union.c" 2>/dev/null || { echo S-UNION-FAIL; exit 1; }
cc $CFLAGS $UFLAGS $ALIGN -S -o "$OUT/enc-lay.S" "$HERE/enc-union.c" 2>/dev/null || { echo S-LAY-FAIL; exit 1; }
diff "$OUT/enc-union.S" "$OUT/enc-lay.S" | wc -l | tee "$OUT/s-diff-lines.txt"
grep -c p2align "$OUT/enc-lay.S" | tee "$OUT/s-lay-p2align.txt"
echo "== static counts (whole-file enc -S) =="
count_s() {
  f=$1
  ld=$(grep -cE '^[[:space:]]+ld[ru][bhsxwq]?' "$f" || true)
  st=$(grep -cE '^[[:space:]]+st[rp][bhsxwq]?' "$f" || true)
  cbr=$(grep -cE '^[[:space:]]+(b\.|cbz|cbnz|tbz|tbnz)' "$f" || true)
  mov=$(grep -cE '^[[:space:]]+mov' "$f" || true)
  echo "$2 ld=$ld st=$st cbr=$cbr mov=$mov"
}
{ count_s "$OUT/enc-union.S" union; count_s "$OUT/enc-lay.S" lay; } | tee "$OUT/static-counts.txt"
echo "== ident probe 24/24 (union vs lay; tip vs union) =="
ident24() {
  a=$1; b=$2; tag=$3
  : > "$OUT/ident-$tag.txt"
  for corp in text-256k mixed-128k zeros-64k; do
    case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
    for lv in e00 e01 e05 e09; do
      "$OUT/cli-$a" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
      "$OUT/cli-$b" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
      if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/ident-$tag.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/ident-$tag.txt"; fi
      "$OUT/cli-$a" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
      "$OUT/cli-$b" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
      if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/ident-$tag.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/ident-$tag.txt"; fi
    done
  done
  echo "IDENT-$tag-ENC: $(grep -c enc-IDENT "$OUT/ident-$tag.txt")/12"
  echo "IDENT-$tag-DEC: $(grep -c dec-IDENT "$OUT/ident-$tag.txt")/12"
}
ident24 union lay union-lay
ident24 tip union tip-union
[ "$(grep -c enc-IDENT "$OUT/ident-union-lay.txt")" = "12" ] || { echo IDENT-UNION-LAY-ENC-FAIL; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/ident-union-lay.txt")" = "12" ] || { echo IDENT-UNION-LAY-DEC-FAIL; exit 1; }
echo IDENT-UNION-LAY-24/24-OK
echo "== cleanup bins (pins recorded) =="
rm -f "$OUT"/cli-* "$OUT"/bench-* "$OUT"/*.o "$OUT"/n.bin "$OUT"/g.bin "$OUT"/nd.bin "$OUT"/gd.bin
echo R27FLEX7-LAYENC-STAGE1-DONE
