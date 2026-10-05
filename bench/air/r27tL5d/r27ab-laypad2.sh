#!/bin/sh
# r27ab-laypad2.sh: CORRECTIVE layout-perturb gate (prereg3, pre-data): tip-vs-PERTURBED-d1.
# Cause: v1 laypgo arm (d1-std vs d1-laypgo) measured PGO-marginal on L0d (~0 by
# mechanism: L0d prize is code-driven, code +20.3 ~= stack +20.1) -- unsatisfiable
# by construction for a code prize. Correct test of standing-gate intent (prize
# robust, not layout luck): does d1's L0d advantage over TIP persist when the
# VEHICLE layout is perturbed?
# CFLAGS-only perturb (-falign-functions/loops=64, R23/R24 method), zero logic
# delta, dec only (enc .o shared to isolate dec layout).
# Arms: (L2a) tip-std vs d1-laystd (d1 + ALIGN, std); (L2b) tippgo vs d1-laypgo
# (d1 + ALIGN + own T0 train).
# PASS (binding, prereg3): L2a tL0d >=5 SEP AND L2b tL0d >=5 SEP. 0 SEP-slower
# on EITHER arm => HOLD. v1-FAIL stands recorded; land adjudicates.
# Run on Air gate slot serial same claim. Env: BENCH_RUNS (5), BENCH_REPS (7).
# Output: port/bench/air/r27tL5d/ab-laypad2/.
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r27tL5d/ab-laypad2
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
ALIGN="-falign-functions=64 -falign-loops=64"
TIPQ=0dae2df417bd56d06dc30f94fa0ebd31
D1Q=5a147a81d4df9269f52e652981dd5776
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
md5 "$P/bench/air/r27tL5d/dec-tip.c" "$P/bench/air/r27tL5d/dec-d1.c"
md5 "$P/src/lzmesh_enc.c" "$P/bench/bench.c"
[ "$(md5 -q "$P/bench/air/r27tL5d/dec-tip.c")" = "$TIPQ" ] || { echo TIP-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r27tL5d/dec-d1.c")" = "$D1Q" ] || { echo D1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/bench.c")" = "$BENCHQ" ] || { echo BENCH-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build tip-std =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-tip.o" "$P/bench/air/r27tL5d/dec-tip.c" 2> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-tip.a" "$OUT/dec-tip.o" "$OUT/enc.o" || { echo TIP-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench27tL5dL2-tip" "$P/bench/bench.c" "$OUT/lib-tip.a" 2>> "$OUT/build-tip.log" || { echo TIP-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-tip.o" "$P/src/port_cli.c" 2>> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-tip" "$OUT/dec-tip.o" "$OUT/enc.o" "$OUT/cli-tip.o" 2>> "$OUT/build-tip.log" || { echo TIP-LINK-FAIL; exit 1; }
echo "tip-warnings=$(grep -ci warning "$OUT/build-tip.log" || true)"
md5 -q "$P/bench/bench27tL5dL2-tip" | tee "$OUT/bin-tip.md5"
echo TIP-BUILD-OK
echo "== build d1-laystd (dec-d1.c + ALIGN, std) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -c -o "$OUT/dec-lay.o" "$P/bench/air/r27tL5d/dec-d1.c" 2> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-lay.a" "$OUT/dec-lay.o" "$OUT/enc.o" || { echo LAY-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -o "$P/bench/bench27tL5dL2-laystd" "$P/bench/bench.c" "$OUT/lib-lay.a" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -c -o "$OUT/cli-lay.o" "$P/src/port_cli.c" 2>> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -o "$OUT/cli-laystd" "$OUT/dec-lay.o" "$OUT/enc.o" "$OUT/cli-lay.o" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
echo "lay-warnings=$(grep -ci warning "$OUT/build-lay.log" || true)"
md5 -q "$P/bench/bench27tL5dL2-laystd" | tee "$OUT/bin-laystd.md5"
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
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $3 "-fprofile-instr-use=$OUT/$1.profdata" -o "$P/bench/bench27tL5dL2-$1" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -o "$OUT/cli-$1" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/enc.o" "$OUT/pgo-use-$1/cli.o" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  md5 -q "$P/bench/bench27tL5dL2-$1" | tee "$OUT/bin-$1.md5"
  echo "$1-BUILD-OK"
}
echo "== build PGO (tippgo, laypgo) T0 own trains =="
build_pgo tippgo "$P/bench/air/r27tL5d/dec-tip.c" ""
build_pgo laypgo "$P/bench/air/r27tL5d/dec-d1.c" "$ALIGN"
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
echo "== PGO-IDENT tip + lay =="
pgoident tip tippgo tip
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
  (cd "$P" && python3 bench/droprep1.py "$OUT/ab-$1/bbase" "$OUT/ab-$1-d1/bbase" && python3 bench/droprep1.py "$OUT/ab-$1/bnew" "$OUT/ab-$1-d1/bnew" && python3 bench/cmp.py "$OUT/ab-$1-d1/bbase" "$OUT/ab-$1-d1/bnew" > "$OUT/cmp-$1-drop1.txt" 2>&1; echo "cmp-$1-d1-rc=$?"; tail -2 "$OUT/cmp-$1-drop1.txt")
  echo "== cool-down 60s =="
  uptime; sleep 60; uptime
}
run_ab layship-std ./bench/bench27tL5dL2-tip ./bench/bench27tL5dL2-laystd
run_ab layship-pgo ./bench/bench27tL5dL2-tippgo ./bench/bench27tL5dL2-laypgo
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
if [ "$AB_FAIL" != "0" ]; then echo R27T5D-LAYPAD2-FAIL-HALT; exit 1; fi
echo R27T5D-LAYPAD2-DONE
