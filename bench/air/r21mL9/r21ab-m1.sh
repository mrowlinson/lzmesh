#!/bin/sh
# r21ab-m1.sh: M1-solo prescreen -- bench-base (staged enc-base.c @3fec8aa3
# R21 base 5947e2456) vs bench-new (staged enc-new.c = M1 e028f916, 13+/29-).
# Matrix-scoped cells. Question: M1 (dead post-extend floors) direction +
# 0-slower + L5-neutrality (est ~0.2-0.5%, OV expected; BANK with numbers).
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2), BENCH_REPS (default 3) => n=6 prescreen.
# Output: port/bench/air/r21mL9/ab1/ (build logs, md5s, cmp.txt).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r21mL9/ab1
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-3}
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$P/bench/air/r21mL9/enc-base.c" "$P/bench/air/r21mL9/enc-new.c"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/bench/air/r21mL9/enc-base.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r21mL9/enc-new.c")" = "e028f916ae4decbc5f995392de68d2a6" ] || { echo NEW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build bench-base (staged enc-base.c + tree dec) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-base.o" "$P/bench/air/r21mL9/enc-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec.o" "$OUT/enc-base.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/bench-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-base.o" "$P/src/port_cli.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-base" "$OUT/dec.o" "$OUT/enc-base.o" "$OUT/cli-base.o" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$OUT/bench-base" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
echo "== build bench-new (staged enc-new.c + tree dec) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-new.o" "$P/bench/air/r21mL9/enc-new.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec.o" "$OUT/enc-new.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/bench-new" "$P/bench/bench.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-new.o" "$P/src/port_cli.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-new" "$OUT/dec.o" "$OUT/enc-new.o" "$OUT/cli-new.o" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
md5 -q "$OUT/bench-new" | tee "$OUT/bin-new.md5"
echo NEW-BUILD-OK
echo "== base-vs-new IDENT 12/12 (remote byte insurance) =="
: > "$OUT/newident.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-base" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-new" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/newident.txt"; else echo "$corp $lv DIV" >> "$OUT/newident.txt"; fi
  done
done
echo "NEW-IDENT: $(grep -c IDENT "$OUT"/newident.txt)/12"
[ "$(grep -c IDENT "$OUT/newident.txt")" = "12" ] || { echo NEW-IDENT-FAIL-HALT; exit 1; }
rm -f "$OUT/n.bin" "$OUT/g.bin"
echo "== gated A/B n=$((RUNS * REPS)) (RUNS=$RUNS REPS=$REPS) =="
uptime
BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh "$P/bench/run_gated.sh" --ab "$OUT/ab-r21mL9m1" "$OUT/bench-base" "$OUT/bench-new" "$P/bench/corpus/text-256k.bin" "$P/bench/corpus/mixed-128k.bin" "$P/bench/corpus/zeros-64k.bin" || { echo AB-FAIL; exit 1; }
echo "ab rc=0"
uptime
python3 "$P/bench/cmp.py" "$OUT/ab-r21mL9m1/bbase" "$OUT/ab-r21mL9m1/bnew" | tee "$OUT/cmp.txt"
echo "== done =="
uptime
