#!/bin/sh
# r22ab-stack.sh: B1P2M3+PGO STACK prescreen/gate -- bench-base (staged
# enc-base.c @3fec8aa3 R22 base 2c5fc993f, non-PGO) vs bench-stackpgo
# (staged enc-stack.c = B1+P2+M3 07700a11 + PGO use-build trained on Air).
# Same-TU tree (no t2alloc). Question: does B1P2M3+PGO reach >=5 SEP on
# tL9e with 0 SEP-slower anywhere (R22 PGO anchor tL9e +4.0 SEP)?
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2), BENCH_REPS (default 3) => n=6 prescreen;
# gate n=35 via BENCH_RUNS=5 BENCH_REPS=7 (matrix precedent).
# Output: port/bench/air/r22tL9-stack/ab/ (build logs, binaries, cmp.txt).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r22tL9-stack/ab
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-3}
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
pgrep -fl bztransmit 2>/dev/null || echo "bb-pre: none"
echo "== pins =="
md5 "$P/bench/air/r22tL9-stack/enc-base.c" "$P/bench/air/r22tL9-stack/enc-stack.c"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/bench/air/r22tL9-stack/enc-base.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r22tL9-stack/enc-stack.c")" = "07700a112e4aa098e55ee903fa71c373" ] || { echo STACK-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build bench-base (staged enc-base.c + tree dec) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-base.o" "$P/bench/air/r22tL9-stack/enc-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec.o" "$OUT/enc-base.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench22tL9s-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$P/bench/bench22tL9s-base" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
echo "== build bench-stack-nonpgo (staged enc-stack.c, for PGO-IDENT) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-stack.o" "$P/bench/air/r22tL9-stack/enc-stack.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec.o" "$OUT/enc-stack.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench22tL9s-new" "$P/bench/bench.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-new.o" "$P/src/port_cli.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-new" "$OUT/dec.o" "$OUT/enc-stack.o" "$OUT/cli-new.o" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
md5 -q "$P/bench/bench22tL9s-new" | tee "$OUT/bin-new.md5"
echo NEW-BUILD-OK
echo "== PGO train bench-stack on staged stack enc (enc-stack + dec) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
mkdir -p "$OUT/pgo-gen" "$OUT/prof"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/enc-stack.o" "$P/bench/air/r22tL9-stack/enc-stack.c" 2> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/pgo-gen/dec.o" "$OUT/pgo-gen/enc-stack.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgo.profdata" "$OUT"/prof/*.profraw || { echo PGO-MERGE-FAIL; exit 1; }
mkdir -p "$OUT/pgo-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/enc-stack.o" "$P/bench/air/r22tL9-stack/enc-stack.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -o "$P/bench/bench22tL9s-stackpgo" "$OUT/pgo-use/dec.o" "$OUT/pgo-use/enc-stack.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -o "$OUT/cli-stackpgo" "$OUT/pgo-use/dec.o" "$OUT/pgo-use/enc-stack.o" "$OUT/pgo-use/cli.o" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
echo "pgo-warnings=$(grep -ci warning "$OUT/build-pgo.log" || true)"
md5 -q "$P/bench/bench22tL9s-stackpgo" | tee "$OUT/bin-stackpgo.md5"
echo PGO-BUILD-OK
echo "== PGO-IDENT 12/12 (stack-nonpgo vs stack-pgo) =="
: > "$OUT/pgoident.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-new" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-stackpgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv DIV" >> "$OUT/pgoident.txt"; fi
  done
done
echo "PGO-IDENT: $(grep -c IDENT "$OUT/pgoident.txt")/12"
[ "$(grep -c IDENT "$OUT/pgoident.txt")" = "12" ] || { echo PGO-IDENT-FAIL-HALT; exit 1; }
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
echo "== A/B n=$RUNS x $REPS (base vs stackpgo) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-r22tL9stack" ./bench/bench22tL9s-base ./bench/bench22tL9s-stackpgo bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-rc=$?")
echo "== cmp =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-r22tL9stack/bbase" "$OUT/ab-r22tL9stack/bnew" 2>&1 | tee "$OUT/cmp.txt"; echo "cmp-rc=$?")
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
echo R22TL9STACK-DONE
