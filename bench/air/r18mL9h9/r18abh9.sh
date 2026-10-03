#!/bin/sh
# r18abh9.sh: H9-vs-tip A/B -- bench-base (staged enc-base.c = D20 tip
# c9f8cefd) vs bench-new (staged enc-new.c = H9-sink 3fec8aa3 = union' +
# S4/S5 sink into i5 arms). n=35 5th-SEP + L5-neutrality price.
# Run on Air gate slot via easy-ssh submit (CLAIM-air-slot.md held,
# dispatcher serial). Env: BENCH_RUNS (5), BENCH_REPS (7).
# Output: port/bench/air/r18mL9h9/ab/ (build logs, ab-mL9/bbase+bnew, cmp.txt).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r18mL9h9/ab
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box == "
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
pgrep -fl bztransmit 2>/dev/null || echo "bb-pre: none"
echo "== pins =="
md5 "$P/bench/air/r18mL9h9/enc-base.c" "$P/bench/air/r18mL9h9/enc-new.c"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/bench/air/r18mL9h9/enc-base.c")" = "c9f8cefde49cb15ecce2d28b1904a5ca" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r18mL9h9/enc-new.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo NEW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build bench-new (staged enc-new.c + tree dec) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-new.o" "$P/bench/air/r18mL9h9/enc-new.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-new.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec-new.o" "$OUT/enc-new.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench18h9-new" "$P/bench/bench.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
md5 -q "$P/bench/bench18h9-new" | tee "$OUT/bin-new.md5"
echo NEW-BUILD-OK
echo "== build bench-base (staged enc-base.c + same dec.o) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-base.o" "$P/bench/air/r18mL9h9/enc-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-new.o" "$OUT/enc-base.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench18h9-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$P/bench/bench18h9-base" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
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
echo "== A/B n=$RUNS x $REPS =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-mL9" ./bench/bench18h9-base ./bench/bench18h9-new bench/corpus/mixed-128k.bin bench/corpus/text-256k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-rc=$?")
echo "== cmp =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-mL9/bbase" "$OUT/ab-mL9/bnew" 2>&1 | tee "$OUT/cmp.txt"; echo "cmp-rc=$?")
echo "== recon =="
(cd "$P" && ./bench/bench18h9-base -n 1 bench/corpus/mixed-128k.bin 2>&1 | tail -n 4; ./bench/bench18h9-new -n 1 bench/corpus/mixed-128k.bin 2>&1 | tail -n 4)
echo R18AB-DONE
