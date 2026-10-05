#!/bin/sh
# layenc-stage1a.sh: R28 tip-instrument validation (local, non-timing).
# Mirror of R27 flex7 layenc-stage1.sh minus the vehicle (H1N1 arm pending
# ANSWER-r28-flex7-1 Q1c): perturb TIP layout (-falign-functions=64
# -falign-loops=64), DEC .o SHARED to isolate enc layout.
# Proves: (1) instrument moves layout (bins/-S/nm differ), (2) streams
# byte-IDENT (tip vs tip-lay 24/24), (3) static preserved (-S counts),
# (4) warn0. Gate PASS rule gated on memo Q1 (PRE-REG draft next).
# Run from worktree root: sh port/bench/air/r28flex7-layenc/layenc-stage1a.sh
# Output: port/bench/air/r28flex7-layenc/ab-stage1a/ (logs+txt committed; bins rm'd).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$HERE/ab-stage1a
ALIGN="-falign-functions=64 -falign-loops=64"
CFLAGS="-O2 -std=c11 -Wall -Wextra -I$P/include"
PRISTINEQ=3fec8aa3274f901b1372be85936a77a2
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== pins =="
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$PRISTINEQ" ] || { echo PRISTINE-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== builds =="
# shellcheck disable=SC2086
cc $CFLAGS -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2> "$OUT/build-dec.log" || { echo DEC-BUILD-FAIL; exit 1; }
cc $CFLAGS -c -o "$OUT/enc-tip.o" "$P/src/lzmesh_enc.c" 2> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc $CFLAGS $ALIGN -c -o "$OUT/enc-lay.o" "$P/src/lzmesh_enc.c" 2> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc $CFLAGS -c -o "$OUT/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-dec.log" || { echo CLI-BUILD-FAIL; exit 1; }
cc $CFLAGS -o "$OUT/cli-tip" "$OUT/enc-tip.o" "$OUT/dec.o" "$OUT/cli.o" 2>> "$OUT/build-tip.log" || { echo TIP-LINK-FAIL; exit 1; }
cc $CFLAGS -o "$OUT/cli-lay" "$OUT/enc-lay.o" "$OUT/dec.o" "$OUT/cli.o" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
for t in tip lay; do
  cc $CFLAGS -o "$OUT/bench-$t" "$P/bench/bench.c" "$OUT/enc-$t.o" "$OUT/dec.o" 2>> "$OUT/build-$t.log" || { echo "$t-BENCH-LINK-FAIL"; exit 1; }
done
for f in "$OUT"/build-*.log; do echo "$(basename "$f") warnings=$(grep -ci warning "$f" || true)"; done > "$OUT/warnings.txt"
cat "$OUT/warnings.txt"
md5 -q "$OUT/cli-tip" | tee "$OUT/bin-tip.md5"
md5 -q "$OUT/cli-lay" | tee "$OUT/bin-lay.md5"
echo BUILDS-OK
echo "== layout-moved proof (nm addrs + -S diffstat) =="
nm "$OUT/enc-tip.o" | grep ' T ' | head -8 | tee "$OUT/nm-tip.txt"
nm "$OUT/enc-lay.o" | grep ' T ' | head -8 | tee "$OUT/nm-lay.txt"
cc $CFLAGS -S -o "$OUT/enc-tip.S" "$P/src/lzmesh_enc.c" 2>/dev/null || { echo S-TIP-FAIL; exit 1; }
cc $CFLAGS $ALIGN -S -o "$OUT/enc-lay.S" "$P/src/lzmesh_enc.c" 2>/dev/null || { echo S-LAY-FAIL; exit 1; }
diff "$OUT/enc-tip.S" "$OUT/enc-lay.S" | wc -l | tee "$OUT/s-diff-lines.txt"
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
{ count_s "$OUT/enc-tip.S" tip; count_s "$OUT/enc-lay.S" lay; } | tee "$OUT/static-counts.txt"
echo "== ident probe (tip vs lay) =="
: > "$OUT/ident-tip-lay.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-tip" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-lay" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/ident-tip-lay.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/ident-tip-lay.txt"; fi
    "$OUT/cli-tip" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-lay" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
    if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/ident-tip-lay.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/ident-tip-lay.txt"; fi
  done
done
echo "IDENT-TIP-LAY-ENC: $(grep -c enc-IDENT "$OUT/ident-tip-lay.txt")/12"
echo "IDENT-TIP-LAY-DEC: $(grep -c dec-IDENT "$OUT/ident-tip-lay.txt")/12"
[ "$(grep -c enc-IDENT "$OUT/ident-tip-lay.txt")" = "12" ] || { echo IDENT-TIP-LAY-ENC-FAIL; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/ident-tip-lay.txt")" = "12" ] || { echo IDENT-TIP-LAY-DEC-FAIL; exit 1; }
echo IDENT-TIP-LAY-24/24-OK
echo "== cleanup bins (pins recorded) =="
rm -f "$OUT"/cli-* "$OUT"/bench-* "$OUT"/*.o "$OUT"/n.bin "$OUT"/g.bin "$OUT"/nd.bin "$OUT"/gd.bin
echo R28FLEX7-LAYENC-STAGE1A-DONE
