#!/bin/sh
# r28ab-phase.sh: tL5d R28 T2-PHASESPLIT -- solve-vs-emit warmed-gap split, L0-only.
# Rig: tip PGO + env-gated phase timers (phasesplit.diff, MEASUREMENT-ONLY).
# Arms: std (k=1) vs W+k1 (same-layout cold) vs warmed W+k7 (bench-w),
# L0 cells only (BENCH_LEVELS=0), n=14 prescreen-shape (RUNS=2 x REPS=7).
# W+k1 isolates warmth (W+k7-vs-W+k1, fixed layout) from layout (W+k1-vs-std,
# fixed warmth); kills the systematic W-shift confound (bench-w.o first in
# link order shifts all lib .text identically every wave). Falsifier:
# solve+emit ~= cell total; W+k1-vs-std ~= flavor-common (tL0e +0.39).
# Decision (t2design1): gap-in-solve => solve-lean hunt; gap-in-emit => put
# vehicle hunt; vanishes => Heisenberg/PMU fallback. Run on Air gate slot 4th.
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r28tL5d/ab-phase
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-7}
BASEQ=5a147a81d4df9269f52e652981dd5776
ENCQ=3fec8aa3274f901b1372be85936a77a2
BENCHQ=aec6dd8062e180c25531484d33f60ee8
BWQ=553c853dab2dc5d7b6f144449a598d5b
PHQ=4e32c3268592e9c8571cf9bffbe0b75d
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
pgrep -fl bztransmit 2>/dev/null || echo "bb-pre: none"
echo "== pins =="
md5 "$P/bench/air/r28tL5d/dec-base.c" "$P/src/lzmesh_enc.c" "$P/bench/bench.c"
md5 "$P/bench/air/r28tL5d/bench-w.c" "$P/bench/air/r28tL5d/phasesplit.diff"
[ "$(md5 -q "$P/bench/air/r28tL5d/dec-base.c")" = "$BASEQ" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/bench.c")" = "$BENCHQ" ] || { echo BENCH-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r28tL5d/bench-w.c")" = "$BWQ" ] || { echo BENCHW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r28tL5d/phasesplit.diff")" = "$PHQ" ] || { echo PHASE-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== oracle-gate 24/24 =="
cc -O2 -o "$OUT/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$OUT/build-oracle.log" || { echo BUILD-ORACLE-FAIL-HALT; exit 1; }
python3 "$P/bench/air/oraclegate.py" "$OUT/oracle_probe" "$P/bench/corpus" "$OUT/oracle-now.bin"
if cmp -s "$OUT/oracle-now.bin" "$P/bench/air/oracle-air.pinned"; then echo ORACLE-GATE-24/24-PASS; else echo ORACLE-GATE-FAIL-HALT; exit 1; fi
sha256 -q "$OUT/oracle-now.bin" | tee "$OUT/oracle-now.sha256"
echo "== apply phasesplit to enc copy =="
cp "$P/src/lzmesh_enc.c" "$OUT/enc-phase.c"
patch -s "$OUT/enc-phase.c" < "$P/bench/air/r28tL5d/phasesplit.diff" || { echo PHASE-APPLY-FAIL-HALT; exit 1; }
grep -c "t2_ph_dump" "$OUT/enc-phase.c"
echo PHASE-APPLY-OK
echo "== build phase-PGO (T0 recipe) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
mkdir -p "$OUT/pgo-gen" "$OUT/prof" "$OUT/pgo-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/dec.o" "$P/bench/air/r28tL5d/dec-base.c" 2> "$OUT/build-phase.log" || { echo PHASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/enc.o" "$OUT/enc-phase.c" 2>> "$OUT/build-phase.log" || { echo PHASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/pgo-gen/dec.o" "$OUT/pgo-gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-phase.log" || { echo PHASE-LINK-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CORPUS > /dev/null || { echo PHASE-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CORPUS > /dev/null || { echo PHASE-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/phase.profdata" "$OUT"/prof/*.profraw || { echo PHASE-MERGE-FAIL; exit 1; }
md5 -q "$OUT/phase.profdata" | tee "$OUT/phase.profdata.md5"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/phase.profdata" -c -o "$OUT/pgo-use/dec.o" "$P/bench/air/r28tL5d/dec-base.c" 2>> "$OUT/build-phase.log" || { echo PHASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/phase.profdata" -c -o "$OUT/pgo-use/enc.o" "$OUT/enc-phase.c" 2>> "$OUT/build-phase.log" || { echo PHASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/phase.profdata" -o "$P/bench/bench28tL5dPH" "$OUT/pgo-use/dec.o" "$OUT/pgo-use/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-phase.log" || { echo PHASE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/phase.profdata" -o "$P/bench/bench28tL5dPHW" "$OUT/pgo-use/dec.o" "$OUT/pgo-use/enc.o" "$P/bench/air/r28tL5d/bench-w.c" 2>> "$OUT/build-phase.log" || { echo PHASE-WLINK-FAIL; exit 1; }
echo "phase-warnings=$(grep -ci warning "$OUT/build-phase.log" || true)"
md5 -q "$P/bench/bench28tL5dPH" | tee "$OUT/bin-phase.md5"
md5 -q "$P/bench/bench28tL5dPHW" | tee "$OUT/bin-phaseW.md5"
echo PHASE-BUILD-OK
echo "== quiet-wait + cool-down 60s =="
tries=0
while [ $tries -lt 3 ]; do
  l1=$(uptime | sed -e "s/.*load averages*:[[:space:]]*//" -e "s/,//g" | awk "{print \$1}")
  echo "try $tries load1=$l1 $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  if awk -v l="$l1" "BEGIN{exit !(l < 4.0)}"; then break; fi
  tries=$((tries+1)); sleep 60
done
uptime; sleep 60; uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-ab: none"
echo "== phase-std L0-only n=$RUNS x $REPS =="
rm -f "$OUT/phase-std.log"
(cd "$P" && env LZMESH_PHASESPLIT=1 LZMESH_PHASELOG="$OUT/phase-std.log" BENCH_RUNS=$RUNS BENCH_REPS=$REPS BENCH_LEVELS=0 sh bench/run_gated.sh "$OUT/ab-phstd" ./bench/bench28tL5dPH bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-phstd-rc=$?")
[ -f "$OUT/ab-phstd/run1.tsv" ] || { echo AB-PHSTD-NO-DATA-HALT; exit 1; }
echo "== cool-down 60s =="
uptime; sleep 60; uptime
echo "== phase-Wk1 L0-only n=$RUNS x $REPS (same-layout cold) =="
rm -f "$OUT/phase-w1.log"
(cd "$P" && env LZMESH_PHASESPLIT=1 LZMESH_PHASELOG="$OUT/phase-w1.log" BENCH_INNER_WARMUP=1 BENCH_RUNS=$RUNS BENCH_REPS=$REPS BENCH_LEVELS=0 sh bench/run_gated.sh "$OUT/ab-phw1" ./bench/bench28tL5dPHW bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-phw1-rc=$?")
[ -f "$OUT/ab-phw1/run1.tsv" ] || { echo AB-PHW1-NO-DATA-HALT; exit 1; }
echo "== cool-down 60s =="
uptime; sleep 60; uptime
echo "== phase-warmed L0-only n=$RUNS x $REPS (k=7) =="
rm -f "$OUT/phase-w.log"
(cd "$P" && env LZMESH_PHASESPLIT=1 LZMESH_PHASELOG="$OUT/phase-w.log" BENCH_INNER_WARMUP=7 BENCH_RUNS=$RUNS BENCH_REPS=$REPS BENCH_LEVELS=0 sh bench/run_gated.sh "$OUT/ab-phw" ./bench/bench28tL5dPHW bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-phw-rc=$?")
[ -f "$OUT/ab-phw/run1.tsv" ] || { echo AB-PHW-NO-DATA-HALT; exit 1; }
echo "== phase scrape =="
grep -h PHASESPLIT "$OUT/phase-std.log" | awk -F'[ =]' '{s+=$3; e+=$5; n+=$7} END{print "std solve_sum="s" emit_sum="e" n_sum="n}'
grep -h PHASESPLIT "$OUT/phase-w1.log" | awk -F'[ =]' '{s+=$3; e+=$5; n+=$7} END{print "Wk1 solve_sum="s" emit_sum="e" n_sum="n}'
grep -h PHASESPLIT "$OUT/phase-w.log" | awk -F'[ =]' '{s+=$3; e+=$5; n+=$7} END{print "warmed solve_sum="s" emit_sum="e" n_sum="n}'
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
echo R28T5D-PHASE-DONE
