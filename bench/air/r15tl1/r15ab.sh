#!/bin/sh
# r15ab.sh: prescreen-remote A/B -- bench-base (staged enc-base.c @a131f7ce8
# R13 land tip) vs bench-new (staged enc-new.c = H5+g1+T2+S4 stack, NOT the
# pushed tree: prescreen tree may carry sibling dirt; staged files are the
# pinned bytes). Matrix-scoped cells (full in-process bench cells, never
# micro-benches). Run on Air via:
#   easy-ssh --remote prescreen submit "sh port/bench/air/r15tl1/r15ab.sh"
# Env knobs: BENCH_RUNS (default 5), BENCH_REPS (default 7).
# Output: port/bench/air/r15tl1/ab/ (build logs, ab-r15tl1/bbase+bnew,
# cmp.txt). Pull with: easy-ssh --remote prescreen pull port/bench/air/r15tl1/ab
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r15tl1/ab
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
md5 "$P/bench/air/r15tl1/enc-base.c" "$P/bench/air/r15tl1/enc-new.c"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/bench/air/r15tl1/enc-base.c")" = "992a88042792bd560d0aac52a537496e" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r15tl1/enc-new.c")" = "c5ed708277472b8968aaa2ceb614a942" ] || { echo NEW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build bench-new (staged enc-new.c + tree dec) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-new.o" "$P/bench/air/r15tl1/enc-new.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-new.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec-new.o" "$OUT/enc-new.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench15-new" "$P/bench/bench.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
md5 -q "$P/bench/bench15-new" | tee "$OUT/bin-new.md5"
echo NEW-BUILD-OK
echo "== build bench-base (staged enc-base.c + same dec.o) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-base.o" "$P/bench/air/r15tl1/enc-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-new.o" "$OUT/enc-base.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench15-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$P/bench/bench15-base" | tee "$OUT/bin-base.md5"
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
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-r15tl1" ./bench/bench15-base ./bench/bench15-new bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-rc=$?")
echo "== cmp =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-r15tl1/bbase" "$OUT/ab-r15tl1/bnew" 2>&1 | tee "$OUT/cmp.txt"; echo "cmp-rc=$?")
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
echo R15AB-DONE
