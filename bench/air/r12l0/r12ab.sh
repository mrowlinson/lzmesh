#!/bin/sh
# r12ab.sh: prescreen-remote A/B -- bench-base (staged enc-base.c @49c308acf)
# vs bench-new (staged enc-new.c = gated H123, NOT the pushed tree:
# prescreen tree may carry sibling dirt; staged files are the pinned
# bytes). Matrix-scoped cells (full in-process bench cells, never
# micro-benches). Run on Air via:
#   easy-ssh --remote prescreen submit "sh port/bench/air/r12l0/r12ab.sh"
# Env knobs: BENCH_RUNS (default 5), BENCH_REPS (default 7).
# Output: port/bench/air/r12l0/ab/ (build logs, ab-l0/bbase+bnew,
# cmp.txt). Pull with: easy-ssh --remote prescreen pull port/bench/air/r12l0/ab
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r12l0/ab
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== pins =="
md5 "$P/bench/air/r12l0/enc-base.c" "$P/bench/air/r12l0/enc-new.c"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/bench/air/r12l0/enc-base.c")" = "41ec58b7aa0621fea606dc6f5c258831" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r12l0/enc-new.c")" = "5c35b9afd1794eec564da158a0eb729d" ] || { echo NEW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build bench-new (staged enc-new.c + tree dec) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-new.o" "$P/bench/air/r12l0/enc-new.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-new.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec-new.o" "$OUT/enc-new.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench12-new" "$P/bench/bench.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
echo NEW-BUILD-OK
echo "== build bench-base (staged enc-base.c + same dec.o) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-base.o" "$P/bench/air/r12l0/enc-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-new.o" "$OUT/enc-base.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench12-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
echo BASE-BUILD-OK
echo "== cooldown 60s =="
sleep 60
echo "== A/B n=$RUNS x $REPS =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-l0" ./bench/bench12-base ./bench/bench12-new bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-rc=$?")
echo "== cmp =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-l0/bbase" "$OUT/ab-l0/bnew" 2>&1 | tee "$OUT/cmp.txt"; echo "cmp-rc=$?")
echo R12AB-DONE
