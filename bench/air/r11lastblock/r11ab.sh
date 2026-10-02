#!/bin/sh
# r11ab.sh: prescreen-remote A/B -- bench-base (staged dec-base.c @4962f2277)
# vs bench-new (staged dec-new.c, NOT the pushed tree: prescreen tree may
# carry sibling dirt; staged files are the pinned bytes). TWO A/Bs:
#   ab-std   = stock bench.c (decode cap n+64; EQ leg dead -> 0-slower proof)
#   ab-exact = bench-exact.c (decode cap n; last block room==ds -> win proof)
# Matrix-scoped cells (full in-process bench cells, never micro-benches).
# Run on Air via:
#   easy-ssh --remote prescreen submit "sh port/bench/air/r11lastblock/r11ab.sh"
# Env knobs: BENCH_RUNS (default 5), BENCH_REPS (default 7).
# Output: port/bench/air/r11lastblock/ab/ (build logs, ab-std/ab-exact
# bbase+bnew, cmp-std.txt/cmp-exact.txt). Pull with:
# easy-ssh --remote prescreen pull port/bench/air/r11lastblock/ab
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r11lastblock/ab
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== pins =="
md5 "$P/bench/air/r11lastblock/dec-base.c" "$P/bench/air/r11lastblock/dec-new.c" "$P/bench/air/r11lastblock/bench-exact.c"
md5 "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r11lastblock/dec-base.c")" = "bb5f2b9a107241fd2b8ca444a8b25286" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r11lastblock/dec-new.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo NEW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r11lastblock/bench-exact.c")" = "a7935b173fc8b6f4c51a3fb6ef251904" ] || { echo EXACT-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "41ec58b7aa0621fea606dc6f5c258831" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build libs (staged dec + tree enc) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-new.o" "$P/bench/air/r11lastblock/dec-new.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-new.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec-new.o" "$OUT/enc-new.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-base.o" "$P/bench/air/r11lastblock/dec-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-base.o" "$OUT/enc-new.o" || { echo BASE-AR-FAIL; exit 1; }
echo "== build 4 bench bins =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench11-std-new" "$P/bench/bench.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench11-std-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench11-exact-new" "$P/bench/air/r11lastblock/bench-exact.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench11-exact-base" "$P/bench/air/r11lastblock/bench-exact.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
echo BUILD-OK
echo "== A/B std n=$RUNS x $REPS =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-std" ./bench/bench11-std-base ./bench/bench11-std-new bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-std-rc=$?")
echo "== A/B exact n=$RUNS x $REPS =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-exact" ./bench/bench11-exact-base ./bench/bench11-exact-new bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-exact-rc=$?")
echo "== cmp std =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-std/bbase" "$OUT/ab-std/bnew" 2>&1 | tee "$OUT/cmp-std.txt"; echo "cmp-std-rc=$?")
echo "== cmp exact =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-exact/bbase" "$OUT/ab-exact/bnew" 2>&1 | tee "$OUT/cmp-exact.txt"; echo "cmp-exact-rc=$?")
echo R11AB-DONE
