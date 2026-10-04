#!/bin/sh
# r23ablay.sh: LAYOUT-PERTURB gate (standing pre-ship) -- bench-base (staged
# dec-d1p4.c = D1P4 0dae2df4 + T0-PGO, default align) vs bench-laypgo (SAME
# dec-d1p4.c + T0-PGO, -falign-functions=64 -falign-loops=64).
# Zero logic delta (identical source, identical train recipe); only layout
# differs. If the D1P4 win is code (not layout lottery), lay == base on Air
# (0 SEP either way, n=35). Any SEP either way KILLS the ship claim.
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 5), BENCH_REPS (default 7) => n=35.
# Output: port/bench/air/r23tL9d/ab-lay/ (build logs, binaries, cmp.txt).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r23tL9d/ab-lay
RUNS=${BENCH_RUNS:-5}
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
md5 "$P/bench/air/r23tL9d/dec-d1p4.c"
md5 "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r23tL9d/dec-d1p4.c")" = "0dae2df417bd56d06dc30f94fa0ebd31" ] || { echo D1P4-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== layout proof (replay address must differ) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/probe-def.o" "$P/bench/air/r23tL9d/dec-d1p4.c" 2>/dev/null || { echo PROBE-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -falign-functions=64 -falign-loops=64 -c -o "$OUT/probe-lay.o" "$P/bench/air/r23tL9d/dec-d1p4.c" 2>/dev/null || { echo PROBE-FAIL; exit 1; }
nm "$OUT/probe-def.o" | grep ' t _lz_u3_replay$' | tee "$OUT/lay-addrs.txt"
nm "$OUT/probe-lay.o" | grep ' t _lz_u3_replay$' | tee -a "$OUT/lay-addrs.txt"
[ "$(nm "$OUT/probe-def.o" | grep ' t _lz_u3_replay$' | awk '{print $1}')" != "$(nm "$OUT/probe-lay.o" | grep ' t _lz_u3_replay$' | awk '{print $1}')" ] || { echo LAYOUT-NOSHIFT-HALT; exit 1; }
echo LAYOUT-SHIFT-OK
echo "== build bench-base (D1P4 + T0-PGO default align) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
mkdir -p "$OUT/pgo-gen" "$OUT/prof"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/dec.o" "$P/bench/air/r23tL9d/dec-d1p4.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/pgo-gen/dec.o" "$OUT/pgo-gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgo.profdata" "$OUT"/prof/*.profraw || { echo PGO-MERGE-FAIL; exit 1; }
mkdir -p "$OUT/pgo-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/dec.o" "$P/bench/air/r23tL9d/dec-d1p4.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -o "$P/bench/bench23tL9d-laybase" "$OUT/pgo-use/dec.o" "$OUT/pgo-use/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/pgo-use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/pgo.profdata" -o "$OUT/cli-base" "$OUT/pgo-use/dec.o" "$OUT/pgo-use/enc.o" "$OUT/pgo-use/cli.o" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$P/bench/bench23tL9d-laybase" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
echo "== build bench-lay (D1P4 + T0-PGO align64, same profdata) =="
mkdir -p "$OUT/lay-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -falign-functions=64 -falign-loops=64 "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/lay-use/dec.o" "$P/bench/air/r23tL9d/dec-d1p4.c" 2> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -falign-functions=64 -falign-loops=64 "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/lay-use/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -falign-functions=64 -falign-loops=64 "-fprofile-instr-use=$OUT/pgo.profdata" -o "$P/bench/bench23tL9d-lay" "$OUT/lay-use/dec.o" "$OUT/lay-use/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -falign-functions=64 -falign-loops=64 "-fprofile-instr-use=$OUT/pgo.profdata" -c -o "$OUT/lay-use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -falign-functions=64 -falign-loops=64 "-fprofile-instr-use=$OUT/pgo.profdata" -o "$OUT/cli-lay" "$OUT/lay-use/dec.o" "$OUT/lay-use/enc.o" "$OUT/lay-use/cli.o" 2>> "$OUT/build-lay.log" || { echo LAY-LINK-FAIL; exit 1; }
echo "lay-warnings=$(grep -ci warning "$OUT/build-lay.log" || true)"
md5 -q "$P/bench/bench23tL9d-lay" | tee "$OUT/bin-lay.md5"
echo LAY-BUILD-OK
echo "== IDENT 12/12 enc + 12/12 dec (base-pgo vs lay-pgo, same logic) =="
: > "$OUT/pgoident.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-base" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-lay" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident.txt"; fi
    "$OUT/cli-base" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-lay" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
    if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/pgoident.txt"; fi
  done
done
echo "IDENT-ENC: $(grep -c enc-IDENT "$OUT/pgoident.txt")/12"
echo "IDENT-DEC: $(grep -c dec-IDENT "$OUT/pgoident.txt")/12"
[ "$(grep -c enc-IDENT "$OUT/pgoident.txt")" = "12" ] || { echo IDENT-ENC-FAIL-HALT; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/pgoident.txt")" = "12" ] || { echo IDENT-DEC-FAIL-HALT; exit 1; }
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
echo "== A/B n=$RUNS x $REPS (base-pgo vs lay-pgo, expect 0 SEP) =="
(cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-r23tL9dlay" ./bench/bench23tL9d-laybase ./bench/bench23tL9d-lay bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-rc=$?")
echo "== cmp =="
(cd "$P" && python3 bench/cmp.py "$OUT/ab-r23tL9dlay/bbase" "$OUT/ab-r23tL9dlay/bnew" 2>&1 | tee "$OUT/cmp.txt"; echo "cmp-rc=$?")
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
echo R23TL9DLAY-DONE
