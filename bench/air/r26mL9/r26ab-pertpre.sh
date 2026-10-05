#!/bin/sh
# r26ab-pertpre.sh: P3 PRESCREEN n=14 k=7-warmed (V0+PGO vs P3+PGO, own trains).
# R26 track B (PRE-REG-R26-ML9 v1): P3 = V0 + minsize on u35_acc_flush (ADD1 rank 3).
# GREEN iff mL9e-delta >=+0.3 + mL0e-delta >=+2.0 + tL0e-delta >=+1.0 + 0 other slower.
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2), BENCH_REPS (default 7) => n=14. REPS must match gate.
# Output: port/bench/air/r26mL9/ab-pertpre-P3/ (builds, md5s, idents, cmp-std, cmp-drop1).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
A26=$P/bench/air/r26mL9
A23=$P/bench/air/r23mL9
OUT=$A26/ab-pertpre-P3
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-7}
export BENCH_INNER_WARMUP=7
VENC=$A23/enc-07700a11.c; VPIN=07700a112e4aa098e55ee903fa71c373
PENC=$A26/enc-p3.c; PPIN=4c1e2ec39458178536b7ae1ea574e050
echo "vehicle=V0+PGO vs P3+PGO"
rm -rf "$OUT"
mkdir -p "$OUT/genV" "$OUT/useV" "$OUT/profV" "$OUT/genP" "$OUT/useP" "$OUT/profP"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$VENC" "$PENC"
md5 "$P/src/lzmesh_dec.c" "$P/bench/bench.c"
[ "$(md5 -q "$VENC")" = "$VPIN" ] || { echo VEH-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$PENC")" = "$PPIN" ] || { echo PERT-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/droprep1.py")" = "255ef543a4b4e99d1f06818b36291736" ] || { echo DROP1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "0dae2df417bd56d06dc30f94fa0ebd31" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
grep -q BENCH_INNER_WARMUP "$P/bench/bench.c" || { echo KNOB-ABSENT-HALT; exit 1; }
echo PINS-OK
CFILES="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata) || { echo NO-PROFDATA-HALT; exit 1; }
echo "== train+build veh-PGO (on V0) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/genV/enc.o" "$VENC" 2> "$OUT/build-genV.log" || { echo GENV-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/genV/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-genV.log" || { echo GENV-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-genV" "$OUT/genV/dec.o" "$OUT/genV/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-genV.log" || { echo GENV-LINK-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/profV/full-%p.profraw" "$OUT/bench-genV" -n 3 $CFILES > /dev/null || { echo TRAINV-FULL-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/profV/l0-%p.profraw" "$OUT/bench-genV" -n 200 -l 0 $CFILES > /dev/null || { echo TRAINV-L0-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgoV.profdata" "$OUT"/profV/*.profraw || { echo MERGEV-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoV.profdata" -c -o "$OUT/useV/enc.o" "$VENC" 2> "$OUT/build-useV.log" || { echo USEV-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoV.profdata" -c -o "$OUT/useV/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-useV.log" || { echo USEV-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoV.profdata" -o "$OUT/bench-veh" "$OUT/useV/dec.o" "$OUT/useV/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-useV.log" || { echo USEV-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoV.profdata" -c -o "$OUT/useV/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-useV.log" || { echo USEV-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoV.profdata" -o "$OUT/cli-veh" "$OUT/useV/dec.o" "$OUT/useV/enc.o" "$OUT/useV/cli.o" 2>> "$OUT/build-useV.log" || { echo USEV-LINK-FAIL; exit 1; }
echo "useV-warnings=$(grep -ci warning "$OUT/build-useV.log" || true)"
md5 -q "$OUT/bench-veh" | tee "$OUT/bin-veh.md5"
md5 -q "$OUT/pgoV.profdata" | tee "$OUT/profdataV.md5"
echo VEH-BUILD-OK
echo "== train+build pert-PGO (on P3) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/genP/enc.o" "$PENC" 2> "$OUT/build-genP.log" || { echo GENP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/genP/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-genP.log" || { echo GENP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-genP" "$OUT/genP/dec.o" "$OUT/genP/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-genP.log" || { echo GENP-LINK-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/profP/full-%p.profraw" "$OUT/bench-genP" -n 3 $CFILES > /dev/null || { echo TRAINP-FULL-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/profP/l0-%p.profraw" "$OUT/bench-genP" -n 200 -l 0 $CFILES > /dev/null || { echo TRAINP-L0-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgoP.profdata" "$OUT"/profP/*.profraw || { echo MERGEP-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoP.profdata" -c -o "$OUT/useP/enc.o" "$PENC" 2> "$OUT/build-useP.log" || { echo USEP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoP.profdata" -c -o "$OUT/useP/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-useP.log" || { echo USEP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoP.profdata" -o "$OUT/bench-pert" "$OUT/useP/dec.o" "$OUT/useP/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-useP.log" || { echo USEP-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoP.profdata" -c -o "$OUT/useP/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-useP.log" || { echo USEP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoP.profdata" -o "$OUT/cli-pert" "$OUT/useP/dec.o" "$OUT/useP/enc.o" "$OUT/useP/cli.o" 2>> "$OUT/build-useP.log" || { echo USEP-LINK-FAIL; exit 1; }
echo "useP-warnings=$(grep -ci warning "$OUT/build-useP.log" || true)"
md5 -q "$OUT/bench-pert" | tee "$OUT/bin-pert.md5"
md5 -q "$OUT/pgoP.profdata" | tee "$OUT/profdataP.md5"
echo PERT-BUILD-OK
echo "== PERT-IDENT 12/12 (veh-PGO vs pert-PGO) =="
: > "$OUT/ident-pert.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-veh" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-pert" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/ident-pert.txt"; else echo "$corp $lv DIV" >> "$OUT/ident-pert.txt"; fi
  done
done
echo "PERT-IDENT: $(grep -c IDENT "$OUT"/ident-pert.txt)/12"
[ "$(grep -c IDENT "$OUT/ident-pert.txt")" = "12" ] || { echo PERT-IDENT-FAIL-HALT; exit 1; }
rm -f "$OUT/n.bin" "$OUT/g.bin"
echo "== gated A/B n=$((RUNS * REPS)) (RUNS=$RUNS REPS=$REPS) veh-vs-pert =="
uptime
BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh "$P/bench/run_gated.sh" --ab "$OUT/ab-r26mL9pertpre" "$OUT/bench-veh" "$OUT/bench-pert" $CFILES || { echo AB-FAIL; exit 1; }
echo "ab rc=0"
uptime
echo "== cmp standard (alongside) =="
python3 "$P/bench/cmp.py" "$OUT/ab-r26mL9pertpre/bbase" "$OUT/ab-r26mL9pertpre/bnew" | tee "$OUT/cmp-std.txt"
echo "== cmp drop-1 (matrix-3 standard) =="
python3 "$P/bench/droprep1.py" "$OUT/ab-r26mL9pertpre/bbase" "$OUT/d1base" --drop 1 || { echo DROP1-BASE-FAIL; exit 1; }
python3 "$P/bench/droprep1.py" "$OUT/ab-r26mL9pertpre/bnew" "$OUT/d1new" --drop 1 || { echo DROP1-NEW-FAIL; exit 1; }
python3 "$P/bench/cmp.py" "$OUT/d1base" "$OUT/d1new" | tee "$OUT/cmp-drop1.txt"
echo "== done =="
uptime
