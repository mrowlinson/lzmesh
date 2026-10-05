#!/bin/sh
# r26ab-pre.sh: tL5d R26 prescreen -- std code x2 + std stack x3 + warmed stack x1, drop-1 secondaries.
# Vehicles: tip (0dae2df4 R23 ship dec) / d1 (B1B2a+D1 dual-sym, fused-clean)
#   / d3 (D3-octave alone, fusion-killer probe) / full (B1B2a+D1+D3).
# Same-TU tree (staged decs + tree enc 3fec8aa3). T0 PGO recipe (train on STD bench).
# Questions: (1) d1-code vs tip-code? (2) full-code vs tip-code? (3) d1-stack
#   vs tippgo (std + warmed k=7)? (4) full-stack vs tippgo (std)? (5) d3-stack
#   vs tippgo (std, collision confirm)? (6) warmed rig lift/clean on d1?
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2, n=14), BENCH_REPS (default 7, gate-matched).
# Output: port/bench/air/r26tL5d/ab/ (build logs, binaries, cmp x12).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r26tL5d/ab
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-7}
TIPQ=0dae2df417bd56d06dc30f94fa0ebd31
D1Q=5a147a81d4df9269f52e652981dd5776
D3Q=ef692681c149ad4249bb04638d0f729e
FULLQ=e68b27e31f00306d0969fc297e1901b6
BWQ=553c853dab2dc5d7b6f144449a598d5b
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
md5 "$P/bench/air/r26tL5d/dec-tip.c" "$P/bench/air/r26tL5d/dec-d1.c" "$P/bench/air/r26tL5d/dec-d3.c" "$P/bench/air/r26tL5d/dec-full.c" "$P/bench/air/r26tL5d/bench-w.c"
md5 "$P/src/lzmesh_enc.c" "$P/bench/bench.c"
[ "$(md5 -q "$P/bench/air/r26tL5d/dec-tip.c")" = "$TIPQ" ] || { echo TIP-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r26tL5d/dec-d1.c")" = "$D1Q" ] || { echo D1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r26tL5d/dec-d3.c")" = "$D3Q" ] || { echo D3-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r26tL5d/dec-full.c")" = "$FULLQ" ] || { echo FULL-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r26tL5d/bench-w.c")" = "$BWQ" ] || { echo BENCHW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/bench.c")" = "$BENCHQ" ] || { echo BENCH-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
build_std() {
  # $1 = tag (tip|d1|full), $2 = dec file
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-$1.o" "$2" 2> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  ar rcs "$OUT/lib-$1.a" "$OUT/dec-$1.o" "$OUT/enc.o" || { echo "$1-AR-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench26tL5d-$1" "$P/bench/bench.c" "$OUT/lib-$1.a" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-$1.o" "$P/src/port_cli.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-$1" "$OUT/dec-$1.o" "$OUT/enc.o" "$OUT/cli-$1.o" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  md5 -q "$P/bench/bench26tL5d-$1" | tee "$OUT/bin-$1.md5"
  echo "$1-BUILD-OK"
}
build_pgo() {
  # $1 = tag (tippgo|d1pgo|d3pgo|fullpgo), $2 = dec file. T0 recipe, STD bench train.
  # Links BOTH std bench ($P/bench/bench26tL5d-$1) and warmed bench (-W-$1, bench-w.c).
  PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
  [ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
  mkdir -p "$OUT/pgo-gen-$1" "$OUT/prof-$1"
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/dec.o" "$2" 2> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen-$1" "$OUT/pgo-gen-$1/dec.o" "$OUT/pgo-gen-$1/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
  LLVM_PROFILE_FILE="$OUT/prof-$1/full-%p.profraw" "$OUT/bench-gen-$1" -n 3 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$OUT/prof-$1/l0-%p.profraw" "$OUT/bench-gen-$1" -n 200 -l 0 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$OUT/$1.profdata" "$OUT"/prof-$1/*.profraw || { echo "$1-MERGE-FAIL"; exit 1; }
  md5 -q "$OUT/$1.profdata" | tee "$OUT/$1.profdata.md5"
  mkdir -p "$OUT/pgo-use-$1"
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/dec.o" "$2" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -o "$P/bench/bench26tL5d-$1" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -o "$P/bench/bench26tL5dW-$1" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/enc.o" "$P/bench/air/r26tL5d/bench-w.c" 2>> "$OUT/build-$1.log" || { echo "$1-WLINK-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -o "$OUT/cli-$1" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/enc.o" "$OUT/pgo-use-$1/cli.o" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  md5 -q "$P/bench/bench26tL5d-$1" | tee "$OUT/bin-$1.md5"
  md5 -q "$P/bench/bench26tL5dW-$1" | tee "$OUT/binW-$1.md5"
  echo "$1-BUILD-OK"
}
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
echo "== build std (tip, d1, full) =="
build_std tip "$P/bench/air/r26tL5d/dec-tip.c"
build_std d1 "$P/bench/air/r26tL5d/dec-d1.c"
build_std full "$P/bench/air/r26tL5d/dec-full.c"
echo "== build PGO (tippgo, d1pgo, d3pgo, fullpgo) =="
build_pgo tippgo "$P/bench/air/r26tL5d/dec-tip.c"
build_pgo d1pgo "$P/bench/air/r26tL5d/dec-d1.c"
build_pgo d3pgo "$P/bench/air/r26tL5d/dec-d3.c"
build_pgo fullpgo "$P/bench/air/r26tL5d/dec-full.c"
echo "== PGO-IDENT tip + d1 + full (d3 probe covered by bench roundtrip checks) =="
pgoident tip tippgo tip
pgoident d1 d1pgo d1
pgoident full fullpgo full
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
  # $1 = name, $2 = base bin, $3 = new bin, $4 = extra env ("" or BENCH_INNER_WARMUP=7)
  echo "== A/B $1 n=$RUNS x $REPS env=${4:-none} =="
  (cd "$P" && env ${4:-} BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-$1" "$2" "$3" bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-$1-rc=$?")
  if [ ! -f "$OUT/ab-$1/bbase/run1.tsv" ]; then echo "AB-$1-NO-DATA-HALT"; AB_FAIL=1; fi
  echo "== cmp $1 =="
  (cd "$P" && python3 bench/cmp.py "$OUT/ab-$1/bbase" "$OUT/ab-$1/bnew" 2>&1 | tee "$OUT/cmp-$1.txt"; echo "cmp-$1-rc=$?")
  echo "== drop-1 $1 =="
  (cd "$P" && python3 bench/droprep1.py "$OUT/ab-$1/bbase" "$OUT/ab-$1-d1/bbase" && python3 bench/droprep1.py "$OUT/ab-$1/bnew" "$OUT/ab-$1-d1/bnew" && python3 bench/cmp.py "$OUT/ab-$1-d1/bbase" "$OUT/ab-$1-d1/bnew" 2>&1 | tee "$OUT/cmp-$1-drop1.txt"; echo "cmp-$1-d1-rc=$?")
  echo "== cool-down 60s =="
  uptime; sleep 60; uptime
}
run_ab code-d1 ./bench/bench26tL5d-tip ./bench/bench26tL5d-d1 ""
run_ab code-full ./bench/bench26tL5d-tip ./bench/bench26tL5d-full ""
run_ab stack-d1 ./bench/bench26tL5d-tippgo ./bench/bench26tL5d-d1pgo ""
run_ab stack-d3 ./bench/bench26tL5d-tippgo ./bench/bench26tL5d-d3pgo ""
run_ab stack-full ./bench/bench26tL5d-tippgo ./bench/bench26tL5d-fullpgo ""
run_ab stackW-d1 ./bench/bench26tL5dW-tippgo ./bench/bench26tL5dW-d1pgo "BENCH_INNER_WARMUP=7"
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
if [ "$AB_FAIL" != "0" ]; then echo R26T5D-AB-FAIL-HALT; exit 1; fi
echo R26T5D-DONE
