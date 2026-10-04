#!/bin/sh
# r23ab-d1p4.sh: D1P4+PGO DECODE prescreen/gate -- bench-base (staged
# dec-base.c @db4ae6b7 R23 base 9a4c6fc0c, non-PGO) vs bench-d1p4pgo
# (staged dec-d1p4.c = D1+P4 0dae2df4 + PGO use-build trained on Air).
# Same-TU tree. Question: does D1P4+PGO hold >=5 SEP on tL9d with
# 0 SEP-slower anywhere (R23 PGO anchor tL9d +6.0 SEP)?
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2), BENCH_REPS (default 3) => n=6 prescreen;
# gate n=35 via BENCH_RUNS=5 BENCH_REPS=7 (matrix precedent).
# Output: port/bench/air/r23tL9d/ab/ (build logs, binaries, cmp.txt).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r23tL9d/ab
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
md5 "$P/bench/air/r23tL9d/dec-base.c" "$P/bench/air/r23tL9d/dec-d1p4.c"
md5 "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r23tL9d/dec-base.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r23tL9d/dec-d1p4.c")" = "0dae2df417bd56d06dc30f94fa0ebd31" ] || { echo D1P4-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build bench-base (staged dec-base.c + tree enc) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-base.o" "$P/bench/air/r23tL9d/dec-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec-base.o" "$OUT/enc.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench23tL9d-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$P/bench/bench23tL9d-base" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
echo "== build bench-new (staged dec-d1p4.c, for PGO-IDENT) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-d1p4.o" "$P/bench/air/r23tL9d/dec-d1p4.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-new.a" "$OUT/dec-d1p4.o" "$OUT/enc.o" || { echo NEW-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/bench/bench23tL9d-new" "$P/bench/bench.c" "$OUT/lib-new.a" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-new.o" "$P/src/port_cli.c" 2>> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-new" "$OUT/dec-d1p4.o" "$OUT/enc.o" "$OUT/cli-new.o" 2>> "$OUT/build-new.log" || { echo NEW-LINK-FAIL; exit 1; }
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
md5 -q "$P/bench/bench23tL9d-new" | tee "$OUT/bin-new.md5"
echo NEW-BUILD-OK
echo "== PGO train bench-new on staged d1p4 dec (enc + dec-d1p4) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
mkdir -p "$OUT/pgo-gen" "$OUT/prof"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/dec-d1p4.o" "$P/bench/air/r23tL9d/dec-d1p4.c" 2> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/pgo-gen/dec-d1p4.o" "$OUT/pgo-gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgo.profdata" "$OUT"/prof/*.profraw || { echo PGO-MERGE-FAIL; exit 1; }
mkdir -p "$OUT/pgo-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/dec-d1p4.o" "$P/bench/air/r23tL9d/dec-d1p4.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -o "$P/bench/bench23tL9d-d1p4pgo" "$OUT/pgo-use/dec-d1p4.o" "$OUT/pgo-use/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -o "$OUT/cli-d1p4pgo" "$OUT/pgo-use/dec-d1p4.o" "$OUT/pgo-use/enc.o" "$OUT/pgo-use/cli.o" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
echo "pgo-warnings=$(grep -ci warning "$OUT/build-pgo.log" || true)"
md5 -q "$P/bench/bench23tL9d-d1p4pgo" | tee "$OUT/bin-d1p4pgo.md5"
echo PGO-BUILD-OK
echo "== PGO-IDENT 12/12 enc + 12/12 dec-roundtrip (new vs d1p4pgo) =="
: > "$OUT/pgoident.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-new" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-d1p4pgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident.txt"; fi
    "$OUT/cli-new" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-d1p4pgo" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
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
echo "== A/B n=$RUNS x $REPS (base vs d1p4pgo) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-r23tL9d" ./bench/bench23tL9d-base ./bench/bench23tL9d-d1p4pgo bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-rc=$?")
echo "== cmp =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-r23tL9d/bbase" "$OUT/ab-r23tL9d/bnew" 2>&1 | tee "$OUT/cmp.txt"; echo "cmp-rc=$?")
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
echo R23TL9D-DONE
