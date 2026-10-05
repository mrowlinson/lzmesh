#!/bin/sh
# r28ab-laypad.sh: CORRECTIVE-pattern layout-perturb gate (pre-data): base-vs-PERTURBED-f1.
# Pattern: R27 prereg3 corrective (v1 FAIL recorded there: PGO-marginal on code prize
# unsatisfiable-by-construction). Correct test of intent (prize robust, not layout
# luck): does the vehicle advantage over BASE persist when VEHICLE layout is
# perturbed? CFLAGS-only perturb (-falign-functions/loops=64, R23/R24 method),
# zero logic delta, dec only (enc .o shared to isolate dec layout).
# Arms: (L2a) base-std vs f1-laystd (f1 + ALIGN, std); (L2b) basepgo vs
# f1-laypgo (f1 + ALIGN + own T0 train).
# PASS (binding, pre-data): gate flagship persists >=5 SEP on BOTH arms + 0
# SEP-slower on EITHER arm. RUN CONDITION: gate flagship >=5 (else moot, HOLD path).
# (F1 est 1-2%: expected moot; script is insurance.) Run on Air gate slot serial
# same claim. Env: BENCH_RUNS (5), BENCH_REPS (7).
# Output: port/bench/air/r28tL5d/ab-laypad/.
# No v1 arm (corrective-pattern direct; R27 v1-FAIL lesson inherited).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r28tL5d/ab-laypad
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
ALIGN="-falign-functions=64 -falign-loops=64"
BASEQ=5a147a81d4df9269f52e652981dd5776
F1Q=417189aa355215ff5f162e88b97656db
ENCQ=3fec8aa3274f901b1372be85936a77a2
BENCHQ=aec6dd8062e180c25531484d33f60ee8
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
pgrep -fl bztransmit 2>/dev/null || echo "bb-pre: none"
echo "== pins =="
md5 "$P/bench/air/r28tL5d/dec-base.c" "$P/src/lzmesh_dec.c"
md5 "$P/src/lzmesh_enc.c" "$P/bench/bench.c"
[ "$(md5 -q "$P/bench/air/r28tL5d/dec-base.c")" = "$BASEQ" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "$F1Q" ] || { echo F1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/bench.c")" = "$BENCHQ" ] || { echo BENCH-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build base-std =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-base.o" "$P/bench/air/r28tL5d/dec-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-base.o" "$OUT/enc.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench28tL5dL2-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-base.o" "$P/src/port_cli.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-base" "$OUT/dec-base.o" "$OUT/enc.o" "$OUT/cli-base.o" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$P/bench/bench28tL5dL2-base" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
echo "== build f1-laystd (tree F1 dec + ALIGN, std) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -c -o "$OUT/dec-lay.o" "$P/src/lzmesh_dec.c" 2> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-lay.a" "$OUT/dec-lay.o" "$OUT/enc.o" || { echo LAY-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -o "$P/bench/bench28tL5dL2-laystd" "$P/bench/bench.c" "$OUT/lib-lay.a" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -c -o "$OUT/cli-lay.o" "$P/src/port_cli.c" 2>> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -o "$OUT/cli-laystd" "$OUT/dec-lay.o" "$OUT/enc.o" "$OUT/cli-lay.o" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
echo "lay-warnings=$(grep -ci warning "$OUT/build-lay.log" || true)"
md5 -q "$P/bench/bench28tL5dL2-laystd" | tee "$OUT/bin-laystd.md5"
nm "$OUT/dec-lay.o" | grep -E '_lz_u3_replay$' | tee "$OUT/addr-lay.txt"
echo LAYSTD-BUILD-OK
build_pgo() {
  # $1 = tag, $2 = dec file, $3 = align flags ("" or ALIGN)
  PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
  [ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
  mkdir -p "$OUT/pgo-gen-$1" "$OUT/prof-$1"
  # shellcheck disable=SC2086
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $3 -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/dec.o" "$2" 2> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $3 -fprofile-instr-generate -o "$OUT/bench-gen-$1" "$OUT/pgo-gen-$1/dec.o" "$OUT/pgo-gen-$1/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
  LLVM_PROFILE_FILE="$OUT/prof-$1/full-%p.profraw" "$OUT/bench-gen-$1" -n 3 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$OUT/prof-$1/l0-%p.profraw" "$OUT/bench-gen-$1" -n 200 -l 0 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$OUT/$1.profdata" "$OUT"/prof-$1/*.profraw || { echo "$1-MERGE-FAIL"; exit 1; }
  md5 -q "$OUT/$1.profdata" | tee "$OUT/$1.profdata.md5"
  mkdir -p "$OUT/pgo-use-$1"
  # shellcheck disable=SC2086
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $3 "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/dec.o" "$2" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $3 "-fprofile-instr-use=$OUT/$1.profdata" -o "$P/bench/bench28tL5dL2-$1" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -o "$OUT/cli-$1" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/enc.o" "$OUT/pgo-use-$1/cli.o" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  md5 -q "$P/bench/bench28tL5dL2-$1" | tee "$OUT/bin-$1.md5"
  echo "$1-BUILD-OK"
}
echo "== build PGO (basepgo, laypgo) T0 own trains =="
build_pgo basepgo "$P/bench/air/r28tL5d/dec-base.c" ""
build_pgo laypgo "$P/src/lzmesh_dec.c" "$ALIGN"
pgoident() {
  # $1 = std cli tag, $2 = pgo cli tag, $3 = out tag
  : > "$OUT/pgoident-$3.txt"
  for corp in text-256k mixed-128k zeros-64k; do
    case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
    for lv in e00 e01 e05 e09; do
      "$OUT/cli-$1" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
      "$OUT/cli-$2" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
      if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident-$3.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident-$3.txt"; fi
      "$OUT/cli-$1" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
      "$OUT/cli-$2" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
      if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/pgoident-$3.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/pgoident-$3.txt"; fi
    done
  done
  echo "PGO-IDENT-$3-ENC: $(grep -c enc-IDENT "$OUT/pgoident-$3.txt")/12"
  echo "PGO-IDENT-$3-DEC: $(grep -c dec-IDENT "$OUT/pgoident-$3.txt")/12"
  [ "$(grep -c enc-IDENT "$OUT/pgoident-$3.txt")" = "12" ] || { echo "PGO-IDENT-$3-ENC-FAIL-HALT"; exit 1; }
  [ "$(grep -c dec-IDENT "$OUT/pgoident-$3.txt")" = "12" ] || { echo "PGO-IDENT-$3-DEC-FAIL-HALT"; exit 1; }
}
echo "== PGO-IDENT base + lay =="
pgoident base basepgo base
pgoident laystd laypgo lay
echo "== quiet-wait (load1 < 4, 3 tries x 60s) + cool-down 60s =="
tries=0
while [ $tries -lt 3 ]; do
  l1=$(uptime | sed -e "s/.*load averages*:[[:space:]]*//" -e "s/,//g" | awk "{print \$1}")
  echo "try $tries load1=$l1 $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  if awk -v l="$l1" "BEGIN{exit !(l < 4.0)}"; then break; fi
  tries=$((tries+1)); sleep 60
done
uptime; sleep 60; uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-ab: none"
AB_FAIL=0
run_ab() {
  # $1 = name, $2 = base bin, $3 = new bin
  echo "== A/B $1 n=$RUNS x $REPS =="
  (cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-$1" "$2" "$3" bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-$1-rc=$?")
  if [ ! -f "$OUT/ab-$1/bbase/run1.tsv" ]; then echo "AB-$1-NO-DATA-HALT"; AB_FAIL=1; fi
  echo "== cmp $1 =="
  (cd "$P" && python3 bench/cmp.py "$OUT/ab-$1/bbase" "$OUT/ab-$1/bnew" > "$OUT/cmp-$1.txt" 2>&1; echo "cmp-$1-rc=$?"; tail -2 "$OUT/cmp-$1.txt")
  echo "== drop-1 $1 =="
  (cd "$P" && python3 bench/droprep1.py "$OUT/ab-$1/bbase" "$OUT/ab-$1-drop1/bbase" && python3 bench/droprep1.py "$OUT/ab-$1/bnew" "$OUT/ab-$1-drop1/bnew" && python3 bench/cmp.py "$OUT/ab-$1-drop1/bbase" "$OUT/ab-$1-drop1/bnew" > "$OUT/cmp-$1-drop1.txt" 2>&1; echo "cmp-$1-drop1-rc=$?"; tail -2 "$OUT/cmp-$1-drop1.txt")
  echo "== cool-down 60s =="
  uptime; sleep 60; uptime
}
run_ab layship-std ./bench/bench28tL5dL2-base ./bench/bench28tL5dL2-laystd
run_ab layship-pgo ./bench/bench28tL5dL2-basepgo ./bench/bench28tL5dL2-laypgo
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
if [ "$AB_FAIL" != "0" ]; then echo R28T5D-LAYPAD-FAIL-HALT; exit 1; fi
echo R28T5D-LAYPAD-DONE
