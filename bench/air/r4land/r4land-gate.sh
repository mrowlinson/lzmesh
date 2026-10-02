#!/bin/bash
# r4land-gate.sh: R4 land gate — full 24-cell n=70 A/B base-vs-union on Air.
# base = staged dec-base.c + enc-base.c (@88cda9ac1, md5-pinned).
# union = pushed tree port/src (decres + enc5).
# Runs in ~/Projects/LZMESH-jobs on MacBookAir via easy-ssh submit (gate remote).
# Env knobs: BENCH_RUNS (default 10), BENCH_REPS (default 7).
# Output: port/bench/air/r4land/ab/ (build logs, ab-r4land/bbase+bnew, cmp.txt).
# Pull: easy-ssh pull port/bench/air/r4land/ab (flattens one level; re-nest after).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r4land/ab
RUNS=${BENCH_RUNS:-10}
REPS=${BENCH_REPS:-7}
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== r4land-gate start $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
echo "== serial-slot guard =="
if pgrep -f "air_matrix|r4land-gate|r4ab|fold29|fold23|p30" >/dev/null 2>&1; then
  n=$(pgrep -f "air_matrix|r4land-gate|r4ab" 2>/dev/null | wc -l | tr -d ' ')
  echo "slot-probe matches=$n (1=self)"
  [ "$n" -gt 2 ] && { echo SLOT-BUSY-HALT; exit 1; }
fi
echo SLOT-OK
echo "== box facts =="
uptime; sysctl -n hw.ncpu 2>/dev/null; sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$P/bench/air/r4land/dec-base.c" "$P/bench/air/r4land/enc-base.c"
md5 "$P/src/lzmesh_dec.c" "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r4land/dec-base.c")" = "1b83ad31a1890d34ab9012d2824252ce" ] || { echo BASE-PIN-FAIL-HALT-dec; exit 1; }
[ "$(md5 -q "$P/bench/air/r4land/enc-base.c")" = "1cdcc2a9b40b887fc708a349277070c0" ] || { echo BASE-PIN-FAIL-HALT-enc; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "5fa2dea518009c8d8fd075109ccf2aaf" ] || { echo UNION-PIN-FAIL-HALT-dec; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "f4cc2699542c45548918d829d67b01c3" ] || { echo UNION-PIN-FAIL-HALT-enc; exit 1; }
echo PINS-OK
echo "== corpus pins =="
(cd "$P" && python3 bench/mkcorpus.py --check bench/corpus) || { echo CORPUS-PIN-FAIL-HALT; exit 1; }
echo CORPUS-PIN-OK
echo "== build union matrix-bins (Air-built: touch first, warnings fatal) =="
(cd "$P" && touch src/*.c bench/*.c && make matrix-bins > "$OUT/build-new.log" 2>&1; echo "rc=$?")
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
[ "$(grep -ci warning "$OUT/build-new.log" || true)" -eq 0 ] || { echo WARNINGS-FAIL-HALT-new; exit 1; }
cp "$P/bench/bench" "$P/bench/bench-new"
echo UNION-BUILD-OK
echo "== build bench-base (staged base .c files, warnings fatal) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-base.o" "$P/bench/air/r4land/dec-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL-dec; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-base.o" "$P/bench/air/r4land/enc-base.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL-enc; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-base.o" "$OUT/enc-base.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
[ "$(grep -ci warning "$OUT/build-base.log" || true)" -eq 0 ] || { echo WARNINGS-FAIL-HALT-base; exit 1; }
echo BASE-BUILD-OK
echo "== BINS sanity (must differ: base vs union objects) =="
if cmp -s "$P/bench/bench-base" "$P/bench/bench-new"; then echo BINS-IDENTICAL-HALT; exit 1; fi
echo BINS-DIFFER-OK
echo "== quiet-wait (load1 < 4, 5 tries x 120s) + cool-down 60s =="
tries=0
while [ $tries -lt 5 ]; do
  l1=$(uptime | sed -e 's/.*load averages*:[[:space:]]*//' -e 's/,//g' | awk '{print $1}')
  echo "try $tries load1=$l1 $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  if awk -v l="$l1" 'BEGIN{exit !(l < 4.0)}'; then break; fi
  tries=$((tries+1)); sleep 120
done
uptime; sleep 60; uptime
echo "== A/B n=$RUNS x $REPS (24 cells, interleaved) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-r4land" ./bench/bench-base ./bench/bench-new bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-rc=$?")
echo "== cmp =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-r4land/bbase" "$OUT/ab-r4land/bnew" 2>&1 | tee "$OUT/cmp.txt"; echo "cmp-rc=$?")
echo R4LAND-GATE-DONE
