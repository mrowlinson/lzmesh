#!/bin/sh
# r24ab-b1p2.sh: B1P2 DECODE prescreen/gate -- bench-base (staged
# dec-base.c @0dae2df4 R24 base add6cd20d, non-PGO) vs bench-new (staged
# dec-b1p2.c = B1+P2 b9f61d10, non-PGO) vs bench-newpgo (dec-b1p2.c +
# PGO use-build trained on Air, T0 recipe). Same-TU tree.
# Question: does B1P2 (+PGO stack) move tL5d >=5 SEP with 0 SEP-slower
# anywhere (R23 PGO anchor tL5d +6.2 OV)?
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2), BENCH_REPS (default 7 -- gate-matched
# per mL9-gate1 lesson) => n=14 prescreen; gate n=35 via BENCH_RUNS=5.
# Output: port/bench/air/r24tL5d/ab/ (build logs, binaries, cmp x4).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r24tL5d/ab
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-7}
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
pgrep -fl bztransmit 2>/dev/null || echo "bb-pre: none"
echo "== pins =="
md5 "$P/bench/air/r24tL5d/dec-base.c" "$P/bench/air/r24tL5d/dec-b1p2.c"
md5 "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r24tL5d/dec-base.c")" = "0dae2df417bd56d06dc30f94fa0ebd31" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r24tL5d/dec-b1p2.c")" = "b9f61d1094801073c73d9b3c2502c35a" ] || { echo B1P2-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build bench-base (staged dec-base.c + tree enc) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-base.o" "$P/bench/air/r24tL5d/dec-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-base.o" "$OUT/enc.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench24tL5d-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$P/bench/bench24tL5d-base" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
echo "== build bench-new (staged dec-b1p2.c, code-only + PGO-IDENT) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-b1p2.o" "$P/bench/air/r24tL5d/dec-b1p2.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec-b1p2.o" "$OUT/enc.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench24tL5d-new" "$P/bench/bench.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-new.o" "$P/src/port_cli.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-new" "$OUT/dec-b1p2.o" "$OUT/enc.o" "$OUT/cli-new.o" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
md5 -q "$P/bench/bench24tL5d-new" | tee "$OUT/bin-new.md5"
echo NEW-BUILD-OK
echo "== PGO train bench-new on staged b1p2 dec (T0 recipe) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
mkdir -p "$OUT/pgo-gen" "$OUT/prof"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/dec-b1p2.o" "$P/bench/air/r24tL5d/dec-b1p2.c" 2> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/pgo-gen/dec-b1p2.o" "$OUT/pgo-gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgo.profdata" "$OUT"/prof/*.profraw || { echo PGO-MERGE-FAIL; exit 1; }
md5 -q "$OUT/pgo.profdata" | tee "$OUT/profdata.md5"
mkdir -p "$OUT/pgo-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/dec-b1p2.o" "$P/bench/air/r24tL5d/dec-b1p2.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -o "$P/bench/bench24tL5d-b1p2pgo" "$OUT/pgo-use/dec-b1p2.o" "$OUT/pgo-use/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -o "$OUT/cli-b1p2pgo" "$OUT/pgo-use/dec-b1p2.o" "$OUT/pgo-use/enc.o" "$OUT/pgo-use/cli.o" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
echo "pgo-warnings=$(grep -ci warning "$OUT/build-pgo.log" || true)"
md5 -q "$P/bench/bench24tL5d-b1p2pgo" | tee "$OUT/bin-b1p2pgo.md5"
echo PGO-BUILD-OK
echo "== PGO-IDENT 12/12 enc + 12/12 dec-roundtrip (new vs b1p2pgo) =="
: > "$OUT/pgoident.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-new" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-b1p2pgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident.txt"; fi
    "$OUT/cli-new" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-b1p2pgo" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
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
echo "== A/B code n=$RUNS x $REPS (base vs new) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-code" ./bench/bench24tL5d-base ./bench/bench24tL5d-new bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-code-rc=$?")
echo "== cmp code =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-code/bbase" "$OUT/ab-code/bnew" 2>&1 | tee "$OUT/cmp-code.txt"; echo "cmp-code-rc=$?")
echo "== drop-1 code =="
(cd "$P" && python3 bench/droprep1.py "$OUT/ab-code/bbase" "$OUT/ab-code-d1/bbase" && python3 bench/droprep1.py "$OUT/ab-code/bnew" "$OUT/ab-code-d1/bnew" && python3 bench/cmp.py "$OUT/ab-code-d1/bbase" "$OUT/ab-code-d1/bnew" 2>&1 | tee "$OUT/cmp-code-drop1.txt"; echo "cmp-code-d1-rc=$?")
echo "== cool-down 60s between runs =="
uptime; sleep 60; uptime
echo "== A/B pgo n=$RUNS x $REPS (base vs newpgo) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-pgo" ./bench/bench24tL5d-base ./bench/bench24tL5d-b1p2pgo bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-pgo-rc=$?")
echo "== cmp pgo =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-pgo/bbase" "$OUT/ab-pgo/bnew" 2>&1 | tee "$OUT/cmp-pgo.txt"; echo "cmp-pgo-rc=$?")
echo "== drop-1 pgo =="
(cd "$P" && python3 bench/droprep1.py "$OUT/ab-pgo/bbase" "$OUT/ab-pgo-d1/bbase" && python3 bench/droprep1.py "$OUT/ab-pgo/bnew" "$OUT/ab-pgo-d1/bnew" && python3 bench/cmp.py "$OUT/ab-pgo-d1/bbase" "$OUT/ab-pgo-d1/bnew" 2>&1 | tee "$OUT/cmp-pgo-drop1.txt"; echo "cmp-pgo-d1-rc=$?")
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
echo R24T5D-DONE
