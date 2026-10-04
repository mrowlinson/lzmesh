#!/bin/sh
# r25ab-laypad.sh: LAYOUT-PERTURB gate for B1B2a+PGO (standing pre-ship gate).
# R23 tL9d shape (code+PGO ship precedent): bench-bbpgo (staged dec-b1b2a.c
# 0c86de68 + T0-PGO, default align) vs bench-laypgo (SAME dec + T0-PGO,
# -falign-functions=64 -falign-loops=64). Zero logic delta; only layout differs.
# If the B1B2a win is code (not layout lottery), lay == base on Air
# (0 SEP either way flagship, n=35). Any SEP either way => HOLD.
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 5), BENCH_REPS (default 7) => n=35.
# Output: port/bench/air/r25tL5d/ab-laypad/.
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r25tL5d/ab-laypad
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
ALIGN="-falign-functions=64 -falign-loops=64"
BBQ=0c86de681f191f6e874f1ac63ba90376
ENCQ=3fec8aa3274f901b1372be85936a77a2
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
pgrep -fl bztransmit 2>/dev/null || echo "bb-pre: none"
echo "== pins =="
md5 "$P/bench/air/r25tL5d/dec-b1b2a.c"
md5 "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r25tL5d/dec-b1b2a.c")" = "$BBQ" ] || { echo BB-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
build_pgo() {
  # $1 = tag, $2 = extra CFLAGS
  PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
  [ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
  mkdir -p "$OUT/pgo-gen-$1" "$OUT/prof-$1"
  # shellcheck disable=SC2086
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $2 -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/dec.o" "$P/bench/air/r25tL5d/dec-b1b2a.c" 2> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $2 -fprofile-instr-generate -o "$OUT/bench-gen-$1" "$OUT/pgo-gen-$1/dec.o" "$OUT/pgo-gen-$1/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
  LLVM_PROFILE_FILE="$OUT/prof-$1/full-%p.profraw" "$OUT/bench-gen-$1" -n 3 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$OUT/prof-$1/l0-%p.profraw" "$OUT/bench-gen-$1" -n 200 -l 0 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$OUT/$1.profdata" "$OUT"/prof-$1/*.profraw || { echo "$1-MERGE-FAIL"; exit 1; }
  md5 -q "$OUT/$1.profdata" | tee "$OUT/$1.profdata.md5"
  mkdir -p "$OUT/pgo-use-$1"
  # shellcheck disable=SC2086
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $2 "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/dec.o" "$P/bench/air/r25tL5d/dec-b1b2a.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/$1.profdata" -c -o "$OUT/pgo-use-$1/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $2 "-fprofile-instr-use=$OUT/$1.profdata" -o "$P/bench/bench25tL5d-$1" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  md5 -q "$P/bench/bench25tL5d-$1" | tee "$OUT/bin-$1.md5"
  nm "$OUT/pgo-use-$1/dec.o" 2>/dev/null | grep -E '_lz_u3_comp_block$|_lz_u3_replay$' | tee "$OUT/addr-$1.txt" || true
  echo "$1-BUILD-OK"
}
echo "== build bbpgo (default align) + laypgo (+ALIGN) =="
build_pgo bbpgo ""
build_pgo laypgo "$ALIGN"
echo "== layout-shift proof =="
cat "$OUT/addr-bbpgo.txt" "$OUT/addr-laypgo.txt" 2>/dev/null || echo "no fn addrs (inlined?)"
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
echo "== A/B lay n=$RUNS x $REPS (bbpgo vs laypgo) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-lay" ./bench/bench25tL5d-bbpgo ./bench/bench25tL5d-laypgo bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-lay-rc=$?")
if [ ! -f "$OUT/ab-lay/bbase/run1.tsv" ]; then echo "AB-lay-NO-DATA-HALT"; AB_FAIL=1; fi
echo "== cmp lay =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-lay/bbase" "$OUT/ab-lay/bnew" 2>&1 | tee "$OUT/cmp-lay.txt"; echo "cmp-lay-rc=$?")
echo "== drop-1 lay =="
(cd "$P" && python3 bench/droprep1.py "$OUT/ab-lay/bbase" "$OUT/ab-lay-d1/bbase" && python3 bench/droprep1.py "$OUT/ab-lay/bnew" "$OUT/ab-lay-d1/bnew" && python3 bench/cmp.py "$OUT/ab-lay-d1/bbase" "$OUT/ab-lay-d1/bnew" 2>&1 | tee "$OUT/cmp-lay-drop1.txt"; echo "cmp-lay-d1-rc=$?")
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
if [ "$AB_FAIL" != "0" ]; then echo R25T5D-LAY-AB-FAIL-HALT; exit 1; fi
echo R25T5D-LAYPAD-DONE
