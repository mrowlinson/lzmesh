#!/bin/sh
# r25ab-pertpre.sh: PERT PRESCREEN n=14 k=7-warmed (veh-PGO vs pert-PGO).
# R25 pert track (PRE-REG-R25-ML9 AMENDMENT v3, memo ADD1 battery): P1 cold-split
# / P2 unroll-cap locus perturbations. cmp reads pert-vs-veh. GREEN per PRE-REG.
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2), BENCH_REPS (default 7) => n=14; VEH=pert1|pert2a.
# Output: port/bench/air/r25mL9/ab-pertpre/ (builds, md5s, idents, cmp-std, cmp-drop1).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
A25=$P/bench/air/r25mL9
A23=$P/bench/air/r23mL9
OUT=$A25/ab-pertpre
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-7}
export BENCH_INNER_WARMUP=7
VEH=${VEH:-pert1}
VENC=$A23/enc-07700a11.c; VPIN=07700a112e4aa098e55ee903fa71c373
case "$VEH" in
  pert1) LENC=$A25/enc-pert1.c; LPIN=490c650412a0a10951b1a451739e1496;;
  pert2a) LENC=$A25/enc-pert2a.c; LPIN=abea388d76320a0892de2b1941048dae;;
  *) echo VEH-UNKNOWN-HALT; exit 1;;
esac
echo "vehicle=$VEH $VENC vs $LENC"
rm -rf "$OUT"
mkdir -p "$OUT/genV" "$OUT/useV" "$OUT/profV" "$OUT/genP" "$OUT/useP" "$OUT/profP"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$VENC" "$LENC"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$VENC")" = "$VPIN" ] || { echo VEH-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$LENC")" = "$LPIN" ] || { echo PERT-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/droprep1.py")" = "255ef543a4b4e99d1f06818b36291736" ] || { echo DROP1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "0dae2df417bd56d06dc30f94fa0ebd31" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
CFILES="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata) || { echo NO-PROFDATA-HALT; exit 1; }
echo "== train+build veh-PGO (on enc-new) =="
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
echo "== train+build lay-PGO (on enc-laypad) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/genP/enc.o" "$LENC" 2> "$OUT/build-genP.log" || { echo GENL-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/genP/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-genP.log" || { echo GENL-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-genP" "$OUT/genP/dec.o" "$OUT/genP/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-genP.log" || { echo GENL-LINK-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/profP/full-%p.profraw" "$OUT/bench-genP" -n 3 $CFILES > /dev/null || { echo TRAINL-FULL-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/profP/l0-%p.profraw" "$OUT/bench-genP" -n 200 -l 0 $CFILES > /dev/null || { echo TRAINL-L0-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgoL.profdata" "$OUT"/profP/*.profraw || { echo MERGEL-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoL.profdata" -c -o "$OUT/useP/enc.o" "$LENC" 2> "$OUT/build-useP.log" || { echo USEL-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoL.profdata" -c -o "$OUT/useP/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-useP.log" || { echo USEL-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoL.profdata" -o "$OUT/bench-pert" "$OUT/useP/dec.o" "$OUT/useP/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-useP.log" || { echo USEL-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoL.profdata" -c -o "$OUT/useP/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-useP.log" || { echo USEL-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgoL.profdata" -o "$OUT/cli-lay" "$OUT/useP/dec.o" "$OUT/useP/enc.o" "$OUT/useP/cli.o" 2>> "$OUT/build-useP.log" || { echo USEL-LINK-FAIL; exit 1; }
echo "useP-warnings=$(grep -ci warning "$OUT/build-useP.log" || true)"
md5 -q "$OUT/bench-pert" | tee "$OUT/bin-lay.md5"
md5 -q "$OUT/pgoL.profdata" | tee "$OUT/profdataL.md5"
echo PERT-BUILD-OK
echo "== veh-vs-lay IDENT 12/12 =="
: > "$OUT/ident-pert.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-veh" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-lay" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/ident-pert.txt"; else echo "$corp $lv DIV" >> "$OUT/ident-pert.txt"; fi
  done
done
echo "PERT-IDENT: $(grep -c IDENT "$OUT"/ident-pert.txt)/12"
[ "$(grep -c IDENT "$OUT/ident-pert.txt")" = "12" ] || { echo PERT-IDENT-FAIL-HALT; exit 1; }
rm -f "$OUT/n.bin" "$OUT/g.bin"
echo "== gated A/B n=$((RUNS * REPS)) (RUNS=$RUNS REPS=$REPS) veh-vs-lay =="
uptime
BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh "$P/bench/run_gated.sh" --ab "$OUT/ab-r25mL9pertpre" "$OUT/bench-veh" "$OUT/bench-pert" $CFILES || { echo AB-FAIL; exit 1; }
echo "ab rc=0"
uptime
echo "== cmp standard (alongside) =="
python3 "$P/bench/cmp.py" "$OUT/ab-r25mL9pertpre/bbase" "$OUT/ab-r25mL9pertpre/bnew" | tee "$OUT/cmp-std.txt"
echo "== cmp drop-1 (matrix-3 standard) =="
python3 "$P/bench/droprep1.py" "$OUT/ab-r25mL9pertpre/bbase" "$OUT/d1base" --drop 1 || { echo DROP1-BASE-FAIL; exit 1; }
python3 "$P/bench/droprep1.py" "$OUT/ab-r25mL9pertpre/bnew" "$OUT/d1new" --drop 1 || { echo DROP1-NEW-FAIL; exit 1; }
python3 "$P/bench/cmp.py" "$OUT/d1base" "$OUT/d1new" | tee "$OUT/cmp-drop1.txt"
echo "== done =="
uptime
