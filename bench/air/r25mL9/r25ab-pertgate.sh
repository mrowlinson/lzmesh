#!/bin/sh
# r25ab-pertgate.sh: PERT GATE n=35 k=7-warmed (tip-noPGO vs pert-PGO pert-trained).
# R25 pert track (PRE-REG-R25-ML9 AMENDMENT v3): gates the B2pre GREEN winner.
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 5), BENCH_REPS (default 7) => n=35; VEH=pert1|pert2a (== B2pre winner).
# Output: port/bench/air/r25mL9/ab-pertgate/ (builds, md5s, idents, cmp-std, cmp-drop1).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
A25=$P/bench/air/r25mL9
A23=$P/bench/air/r23mL9
OUT=$A25/ab-pertgate
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
export BENCH_INNER_WARMUP=7
VEH=${VEH:-pert1}
case "$VEH" in
  pert1) VENC=$A25/enc-pert1.c; VPIN=490c650412a0a10951b1a451739e1496;;
  pert2a) VENC=$A25/enc-pert2a.c; VPIN=abea388d76320a0892de2b1941048dae;;
  *) echo VEH-UNKNOWN-HALT; exit 1;;
esac
echo "vehicle=$VEH $VENC"
rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/use" "$OUT/prof"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$A23/enc-base.c" "$VENC"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$A23/enc-base.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$VENC")" = "$VPIN" ] || { echo NEW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/droprep1.py")" = "255ef543a4b4e99d1f06818b36291736" ] || { echo DROP1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "0dae2df417bd56d06dc30f94fa0ebd31" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
CFILES="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
echo "== build bench-base (tip, no PGO) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-base.o" "$A23/enc-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec.o" "$OUT/enc-base.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/bench-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-base.o" "$P/src/port_cli.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-base" "$OUT/dec.o" "$OUT/enc-base.o" "$OUT/cli-base.o" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$OUT/bench-base" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
echo "== build veh-noPGO (attribution baseline + IDENT) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-new.o" "$VENC" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec.o" "$OUT/enc-new.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-new.o" "$P/src/port_cli.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-new" "$OUT/dec.o" "$OUT/enc-new.o" "$OUT/cli-new.o" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
echo NEW-BUILD-OK
echo "== PGO train ON enc-new (T0 recipe: -n 3 all + -n 200 -l 0) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata) || { echo NO-PROFDATA-HALT; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/gen/enc.o" "$VENC" 2> "$OUT/build-gen.log" || { echo GEN-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/gen/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-gen.log" || { echo GEN-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/gen/dec.o" "$OUT/gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-gen.log" || { echo GEN-LINK-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CFILES > /dev/null || { echo TRAIN-FULL-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CFILES > /dev/null || { echo TRAIN-L0-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgo.profdata" "$OUT"/prof/*.profraw || { echo MERGE-FAIL; exit 1; }
md5 -q "$OUT/pgo.profdata" | tee "$OUT/profdata.md5"
echo TRAIN-OK
echo "== build veh-PGO (profile-use) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -c -o "$OUT/use/enc.o" "$VENC" 2> "$OUT/build-use.log" || { echo USE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -c -o "$OUT/use/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-use.log" || { echo USE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-pgo.a" "$OUT/use/dec.o" "$OUT/use/enc.o" || { echo USE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -o "$OUT/bench-pgo" "$OUT/use/dec.o" "$OUT/use/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-use.log" || { echo USE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -c -o "$OUT/use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-use.log" || { echo USE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -o "$OUT/cli-pgo" "$OUT/use/dec.o" "$OUT/use/enc.o" "$OUT/use/cli.o" 2>> "$OUT/build-use.log" || { echo USE-LINK-FAIL; exit 1; }
echo "use-warnings=$(grep -ci warning "$OUT/build-use.log" || true)"
md5 -q "$OUT/bench-pgo" | tee "$OUT/bin-pgo.md5"
echo USE-BUILD-OK
echo "== IDENTs 12/12 x2 (veh + PGO) =="
: > "$OUT/ident-new.txt"; : > "$OUT/ident-pgo.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-base" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-new" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/ident-new.txt"; else echo "$corp $lv DIV" >> "$OUT/ident-new.txt"; fi
    "$OUT/cli-pgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/ident-pgo.txt"; else echo "$corp $lv DIV" >> "$OUT/ident-pgo.txt"; fi
  done
done
echo "NEW-IDENT: $(grep -c IDENT "$OUT"/ident-new.txt)/12 PGO-IDENT: $(grep -c IDENT "$OUT"/ident-pgo.txt)/12"
[ "$(grep -c IDENT "$OUT/ident-new.txt")" = "12" ] || { echo NEW-IDENT-FAIL-HALT; exit 1; }
[ "$(grep -c IDENT "$OUT/ident-pgo.txt")" = "12" ] || { echo PGO-IDENT-FAIL-HALT; exit 1; }
rm -f "$OUT/n.bin" "$OUT/g.bin"
echo "== gated A/B n=$((RUNS * REPS)) (RUNS=$RUNS REPS=$REPS) base-vs-pgo =="
uptime
BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh "$P/bench/run_gated.sh" --ab "$OUT/ab-r25mL9pertgate" "$OUT/bench-base" "$OUT/bench-pgo" $CFILES || { echo AB-FAIL; exit 1; }
echo "ab rc=0"
uptime
echo "== cmp standard (alongside) =="
python3 "$P/bench/cmp.py" "$OUT/ab-r25mL9pertgate/bbase" "$OUT/ab-r25mL9pertgate/bnew" | tee "$OUT/cmp-std.txt"
echo "== cmp drop-1 (matrix-3 standard) =="
python3 "$P/bench/droprep1.py" "$OUT/ab-r25mL9pertgate/bbase" "$OUT/d1base" --drop 1 || { echo DROP1-BASE-FAIL; exit 1; }
python3 "$P/bench/droprep1.py" "$OUT/ab-r25mL9pertgate/bnew" "$OUT/d1new" --drop 1 || { echo DROP1-NEW-FAIL; exit 1; }
python3 "$P/bench/cmp.py" "$OUT/d1base" "$OUT/d1new" | tee "$OUT/cmp-drop1.txt"
echo "== done =="
uptime
