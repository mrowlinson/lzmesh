#!/bin/sh
# r24ab-laypad.sh: LAYOUT-PERTURB gate for pure-PGO tL5d ship (standing gate,
# before any ship claim). CFLAGS-only perturb (-falign-functions/loops=64,
# R23 method; replay 0x1150->0x16c0 proven locally), zero logic delta, dec
# only (enc .o shared to isolate dec layout).
# Arms: bench-base (dec-base.c std) vs bench-laystd (dec-base.c + align, std)
# vs bench-laypgo (dec-base.c + align + PGO, own T0 train on Air).
# PASS (pre-registered): laystd 0 SEP (layout alone prizeless) AND laypgo
# flagship tL5d >=5 SEP (PGO prize robust, not layout luck). A laypgo
# collapse to OV => HOLD (prize is layout luck, unshippable).
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 5), BENCH_REPS (default 7) => n=35 gate.
# Output: port/bench/air/r24tL5d/ab-laypad/ (build logs, binaries, cmp x4).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r24tL5d/ab-laypad
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
ALIGN="-falign-functions=64 -falign-loops=64"
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
pgrep -fl bztransmit 2>/dev/null || echo "bb-pre: none"
echo "== pins =="
md5 "$P/bench/air/r24tL5d/dec-base.c"
md5 "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r24tL5d/dec-base.c")" = "0dae2df417bd56d06dc30f94fa0ebd31" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build bench-base (staged dec-base.c + tree enc) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-base.o" "$P/bench/air/r24tL5d/dec-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-base.o" "$OUT/enc.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench24tL5d-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-base.o" "$P/src/port_cli.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-base" "$OUT/dec-base.o" "$OUT/enc.o" "$OUT/cli-base.o" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$P/bench/bench24tL5d-base" | tee "$OUT/bin-base.md5"
nm "$OUT/dec-base.o" | grep -E '_lz_u3_replay$' | tee "$OUT/addr-base.txt"
echo BASE-BUILD-OK
echo "== build bench-laystd (dec-base.c + ALIGN, std) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -c -o "$OUT/dec-lay.o" "$P/bench/air/r24tL5d/dec-base.c" 2> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-lay.a" "$OUT/dec-lay.o" "$OUT/enc.o" || { echo LAY-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -o "$P/bench/bench24tL5d-laystd" "$P/bench/bench.c" "$OUT/lib-lay.a" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -c -o "$OUT/cli-lay.o" "$P/src/port_cli.c" 2>> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -o "$OUT/cli-laystd" "$OUT/dec-lay.o" "$OUT/enc.o" "$OUT/cli-lay.o" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
echo "lay-warnings=$(grep -ci warning "$OUT/build-lay.log" || true)"
md5 -q "$P/bench/bench24tL5d-laystd" | tee "$OUT/bin-laystd.md5"
nm "$OUT/dec-lay.o" | grep -E '_lz_u3_replay$' | tee "$OUT/addr-lay.txt"
echo LAYSTD-BUILD-OK
echo "== PGO train bench-laystd dec (T0 recipe) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
mkdir -p "$OUT/pgo-gen" "$OUT/prof"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -fprofile-instr-generate -c -o "$OUT/pgo-gen/dec-lay.o" "$P/bench/air/r24tL5d/dec-base.c" 2> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/pgo-gen/dec-lay.o" "$OUT/pgo-gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgo.profdata" "$OUT"/prof/*.profraw || { echo PGO-MERGE-FAIL; exit 1; }
md5 -q "$OUT/pgo.profdata" | tee "$OUT/profdata.md5"
mkdir -p "$OUT/pgo-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/dec-lay.o" "$P/bench/air/r24tL5d/dec-base.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN "-fprofile-instr-use=$OUT/pgo.profdata" -o "$P/bench/bench24tL5d-laypgo" "$OUT/pgo-use/dec-lay.o" "$OUT/pgo-use/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" $ALIGN "-fprofile-instr-use=$OUT/pgo.profdata" -o "$OUT/cli-laypgo" "$OUT/pgo-use/dec-lay.o" "$OUT/pgo-use/enc.o" "$OUT/pgo-use/cli.o" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
echo "pgo-warnings=$(grep -ci warning "$OUT/build-pgo.log" || true)"
md5 -q "$P/bench/bench24tL5d-laypgo" | tee "$OUT/bin-laypgo.md5"
echo PGO-BUILD-OK
echo "== PGO-IDENT 12/12 enc + 12/12 dec-roundtrip (laystd vs laypgo) =="
: > "$OUT/pgoident.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-laystd" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-laypgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident.txt"; fi
    "$OUT/cli-laystd" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-laypgo" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
    if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/pgoident.txt"; fi
  done
done
echo "PGO-IDENT-ENC: $(grep -c enc-IDENT "$OUT/pgoident.txt")/12"
echo "PGO-IDENT-DEC: $(grep -c dec-IDENT "$OUT/pgoident.txt")/12"
[ "$(grep -c enc-IDENT "$OUT/pgoident.txt")" = "12" ] || { echo PGO-IDENT-ENC-FAIL-HALT; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/pgoident.txt")" = "12" ] || { echo PGO-IDENT-DEC-FAIL-HALT; exit 1; }
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
echo "== A/B laystd n=$RUNS x $REPS (base vs laystd) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-laystd" ./bench/bench24tL5d-base ./bench/bench24tL5d-laystd bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-laystd-rc=$?")
echo "== cmp laystd =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-laystd/bbase" "$OUT/ab-laystd/bnew" 2>&1 | tee "$OUT/cmp-laystd.txt"; echo "cmp-laystd-rc=$?")
echo "== drop-1 laystd =="
(cd "$P" && python3 bench/droprep1.py "$OUT/ab-laystd/bbase" "$OUT/ab-laystd-d1/bbase" && python3 bench/droprep1.py "$OUT/ab-laystd/bnew" "$OUT/ab-laystd-d1/bnew" && python3 bench/cmp.py "$OUT/ab-laystd-d1/bbase" "$OUT/ab-laystd-d1/bnew" 2>&1 | tee "$OUT/cmp-laystd-drop1.txt"; echo "cmp-laystd-d1-rc=$?")
echo "== cool-down 60s between runs =="
uptime; sleep 60; uptime
echo "== A/B laypgo n=$RUNS x $REPS (base vs laypgo) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-laypgo" ./bench/bench24tL5d-base ./bench/bench24tL5d-laypgo bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-laypgo-rc=$?")
echo "== cmp laypgo =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-laypgo/bbase" "$OUT/ab-laypgo/bnew" 2>&1 | tee "$OUT/cmp-laypgo.txt"; echo "cmp-laypgo-rc=$?")
echo "== drop-1 laypgo =="
(cd "$P" && python3 bench/droprep1.py "$OUT/ab-laypgo/bbase" "$OUT/ab-laypgo-d1/bbase" && python3 bench/droprep1.py "$OUT/ab-laypgo/bnew" "$OUT/ab-laypgo-d1/bnew" && python3 bench/cmp.py "$OUT/ab-laypgo-d1/bbase" "$OUT/ab-laypgo-d1/bnew" 2>&1 | tee "$OUT/cmp-laypgo-drop1.txt"; echo "cmp-laypgo-d1-rc=$?")
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
echo R24T5D-LAYPAD-DONE
