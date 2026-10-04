#!/bin/sh
# r22ab-pgot3.sh: T0-vs-T3 PGO-train prescreen (memo Q2c: T3 = alternative PGO)
# -- bench-t0 (tip + T0-recipe PGO: -n 3 all + -n 200 -l 0) vs bench-t3
# (tip + T3 train: T0 + -n 25 -l 9 L9-boost). STANDARD RIG (no drop1):
# clean read of the train effect. Decides which PGO trains any stack.
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2), BENCH_REPS (default 3) => n=6 prescreen.
# Output: port/bench/air/r22mL9/abt3/ (build logs, md5s, idents, cmp).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
A=$P/bench/air/r22mL9
OUT=$A/abt3
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-3}
rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/prof" "$OUT/use-t0" "$OUT/use-t3"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$A/enc-base.c"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$A/enc-base.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
CFILES="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata) || { echo NO-PROFDATA-HALT; exit 1; }
echo "== build bench-gen (tip, instrumented) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/gen/enc.o" "$A/enc-base.c" 2> "$OUT/build-gen.log" || { echo GEN-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/gen/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-gen.log" || { echo GEN-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/gen/dec.o" "$OUT/gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-gen.log" || { echo GEN-LINK-FAIL; exit 1; }
echo GEN-BUILD-OK
echo "== train T0 + T3 =="
LLVM_PROFILE_FILE="$OUT/prof/t0-full-%p.profraw" "$OUT/bench-gen" -n 3 $CFILES > /dev/null || { echo TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/t0-l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CFILES > /dev/null || { echo TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/t0.profdata" "$OUT"/prof/t0-*.profraw || { echo MERGE-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/t3-l9-%p.profraw" "$OUT/bench-gen" -n 25 -l 9 $CFILES > /dev/null || { echo TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/t3.profdata" "$OUT"/prof/t0-*.profraw "$OUT"/prof/t3-*.profraw || { echo MERGE-FAIL; exit 1; }
md5 -q "$OUT/t0.profdata" | tee "$OUT/t0.md5"
md5 -q "$OUT/t3.profdata" | tee "$OUT/t3.md5"
echo TRAIN-OK
for v in t0 t3; do
  echo "== build bench-$v (profile-use) =="
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$v.profdata" -c -o "$OUT/use-$v/enc.o" "$A/enc-base.c" 2> "$OUT/build-use-$v.log" || { echo USE-BUILD-FAIL-$v; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$v.profdata" -c -o "$OUT/use-$v/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-use-$v.log" || { echo USE-BUILD-FAIL-$v; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$v.profdata" -o "$OUT/bench-$v" "$OUT/use-$v/dec.o" "$OUT/use-$v/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-use-$v.log" || { echo USE-LINK-FAIL-$v; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$v.profdata" -c -o "$OUT/use-$v/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-use-$v.log" || { echo USE-BUILD-FAIL-$v; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$v.profdata" -o "$OUT/cli-$v" "$OUT/use-$v/dec.o" "$OUT/use-$v/enc.o" "$OUT/use-$v/cli.o" 2>> "$OUT/build-use-$v.log" || { echo USE-LINK-FAIL-$v; exit 1; }
  echo "$v-warnings=$(grep -ci warning "$OUT/build-use-$v.log" || true)"
  md5 -q "$OUT/bench-$v" | tee "$OUT/bin-$v.md5"
done
echo USE-BUILD-OK
echo "== PGO-IDENT t0-vs-t3 12/12 =="
: > "$OUT/ident.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-t0" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-t3" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/ident.txt"; else echo "$corp $lv DIV" >> "$OUT/ident.txt"; fi
  done
done
echo "IDENT: $(grep -c IDENT "$OUT"/ident.txt)/12"
[ "$(grep -c IDENT "$OUT/ident.txt")" = "12" ] || { echo IDENT-FAIL-HALT; exit 1; }
rm -f "$OUT/n.bin" "$OUT/g.bin"
echo "== gated A/B n=$((RUNS * REPS)) (RUNS=$RUNS REPS=$REPS) t0-vs-t3 =="
uptime
BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh "$P/bench/run_gated.sh" --ab "$OUT/ab-r22mL9t3" "$OUT/bench-t0" "$OUT/bench-t3" $CFILES || { echo AB-FAIL; exit 1; }
echo "ab rc=0"
uptime
python3 "$P/bench/cmp.py" "$OUT/ab-r22mL9t3/bbase" "$OUT/ab-r22mL9t3/bnew" | tee "$OUT/cmp.txt"
echo "== done =="
uptime
