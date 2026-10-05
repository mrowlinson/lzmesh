#!/bin/sh
# r27ab-land.sh: r27-land2 MERGED-tree byte re-verify (SHIP dec 5a147a81 in port/src).
# Units x2 (1083/1076/0/0, SKIP=0, X6/X6) + smoke x2 (1232/0) + tiers std+PGO x4
# (FULL/HOLD/FRESH/d77 oracle-vs-tree; findings diffed vs banked tip sets post-pull;
#  NEW must be 0) + PGO-IDENT 24/24 + profdata pin (expect 9a00a685).
# Run on Air BATCH remote ONLY (claim-poll slot ABSENT before submit; caps --mem 4G
# --cpus 2). Vectors NOT synced by easy-ssh (ignore) -> rsync explicitly BEFORE submit.
# NO make/make-clean anywhere (cleanwipe guard: results/ holds committed refs).
# Output: port/bench/air/r27land/land/ (summaries + findings + logs; bins excluded).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r27land/land
DECQ=5a147a81d4df9269f52e652981dd5776
ENCQ=3fec8aa3274f901b1372be85936a77a2
rm -rf "$OUT"
mkdir -p "$OUT/bin-default" "$OUT/bin-scalar"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins (merged tree) =="
md5 "$P/src/lzmesh_dec.c" "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "$DECQ" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== vectors preflight (26 files) =="
nv=$(ls "$P/tests/unit/vectors/" | wc -l | tr -d ' ')
echo "vectors=$nv"
[ "$nv" = "26" ] || { echo VECTORS-MISSING-HALT; exit 1; }
echo "== build oracle_probe + oracle-gate 24/24 =="
cc -O2 -o "$OUT/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$OUT/build-oracle.log" || { echo BUILD-ORACLE-FAIL-HALT; exit 1; }
python3 "$P/bench/air/oraclegate.py" "$OUT/oracle_probe" "$P/bench/corpus" "$OUT/oracle-now.bin" || { echo ORACLE-GATE-FAIL-HALT; exit 1; }
if cmp -s "$OUT/oracle-now.bin" "$P/bench/air/oracle-air.pinned"; then echo ORACLE-GATE-24/24-PASS; else echo ORACLE-GATE-FAIL-HALT; exit 1; fi
echo "== build merged std lib + cli =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-std.o" "$P/src/lzmesh_dec.c" 2> "$OUT/build-std.log" || { echo STD-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-std.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-std.log" || { echo STD-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-std.a" "$OUT/dec-std.o" "$OUT/enc-std.o" || { echo STD-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-std.o" "$P/src/port_cli.c" 2>> "$OUT/build-std.log" || { echo STD-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-std" "$OUT/dec-std.o" "$OUT/enc-std.o" "$OUT/cli-std.o" 2>> "$OUT/build-std.log" || { echo STD-LINK-FAIL; exit 1; }
echo "std-warnings=$(grep -ci warning "$OUT/build-std.log" || true)"
[ "$(grep -ci warning "$OUT/build-std.log" || true)" = "0" ] || { echo STD-WARN-HALT; exit 1; }
echo STD-BUILD-OK
echo "== build merged scalar lib + cli (-DLZMESH_SCALAR) =="
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/dec-sca.o" "$P/src/lzmesh_dec.c" 2> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/enc-sca.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-sca.a" "$OUT/dec-sca.o" "$OUT/enc-sca.o" || { echo SCA-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/cli-sca.o" "$P/src/port_cli.c" 2>> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -o "$OUT/cli-sca" "$OUT/dec-sca.o" "$OUT/enc-sca.o" "$OUT/cli-sca.o" 2>> "$OUT/build-sca.log" || { echo SCA-LINK-FAIL; exit 1; }
echo "sca-warnings=$(grep -ci warning "$OUT/build-sca.log" || true)"
[ "$(grep -ci warning "$OUT/build-sca.log" || true)" = "0" ] || { echo SCA-WARN-HALT; exit 1; }
echo SCA-BUILD-OK
echo "bins-differ?"
if cmp -s "$OUT/cli-std" "$OUT/cli-sca"; then echo BINS-IDENTICAL-HALT; exit 1; else echo BINS-DIFFER-OK; fi
echo "== build merged PGO lib + cli (T0 recipe) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
mkdir -p "$OUT/pgo-gen" "$OUT/prof" "$OUT/pgo-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/dec.o" "$P/src/lzmesh_dec.c" 2> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/pgo-gen/dec.o" "$OUT/pgo-gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-LINK-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/vehpgo.profdata" "$OUT"/prof/*.profraw || { echo PGO-MERGE-FAIL; exit 1; }
md5 -q "$OUT/vehpgo.profdata" | tee "$OUT/vehpgo.profdata.md5"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/vehpgo.profdata" -c -o "$OUT/pgo-use/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/vehpgo.profdata" -c -o "$OUT/pgo-use/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-pgo.a" "$OUT/pgo-use/dec.o" "$OUT/pgo-use/enc.o" || { echo PGO-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/vehpgo.profdata" -c -o "$OUT/pgo-use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/vehpgo.profdata" -o "$OUT/cli-pgo" "$OUT/pgo-use/dec.o" "$OUT/pgo-use/enc.o" "$OUT/pgo-use/cli.o" 2>> "$OUT/build-pgo.log" || { echo PGO-LINK-FAIL; exit 1; }
echo "pgo-warnings=$(grep -ci warning "$OUT/build-pgo.log" || true)"
echo PGO-BUILD-OK
echo "== PGO-IDENT 12/12 enc + 12/12 dec (std vs pgo) =="
: > "$OUT/pgoident.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-std" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-pgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident.txt"; fi
    "$OUT/cli-std" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
    "$OUT/cli-pgo" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
    if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/pgoident.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/pgoident.txt"; fi
  done
done
echo "PGO-IDENT-ENC: $(grep -c enc-IDENT "$OUT/pgoident.txt")/12"
echo "PGO-IDENT-DEC: $(grep -c dec-IDENT "$OUT/pgoident.txt")/12"
[ "$(grep -c enc-IDENT "$OUT/pgoident.txt")" = "12" ] || { echo PGO-IDENT-ENC-FAIL-HALT; exit 1; }
[ "$(grep -c dec-IDENT "$OUT/pgoident.txt")" = "12" ] || { echo PGO-IDENT-DEC-FAIL-HALT; exit 1; }
echo "== port_cli FIRST per mode + units x2 (bar 1083/1076/0/0 SKIP=0 X6/X6) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/port_cli" "$OUT/dec-std.o" "$OUT/enc-std.o" "$OUT/cli-std.o" 2>> "$OUT/build-std.log" || { echo DEF-CLI-FAIL; exit 1; }
UFAIL=0
: > "$OUT/units-default.log"
for t in "$P"/tests/unit/test_*.c; do
  b="$OUT/bin-default/$(basename "$t" .c)"
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$b" "$t" "$OUT/lib-std.a" 2>> "$OUT/units-default.log" || { echo "UNIT-BUILD-FAIL $t"; UFAIL=1; }
  (cd "$P" && "$b" >> "$OUT/units-default.log" 2>&1) || { echo "UNIT-FAIL-default $(basename "$t")"; UFAIL=1; }
done
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -o "$P/port_cli" "$OUT/dec-sca.o" "$OUT/enc-sca.o" "$OUT/cli-sca.o" 2>> "$OUT/build-sca.log" || { echo SCA-CLI-FAIL; exit 1; }
: > "$OUT/units-scalar.log"
for t in "$P"/tests/unit/test_*.c; do
  b="$OUT/bin-scalar/$(basename "$t" .c)"
  cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -o "$b" "$t" "$OUT/lib-sca.a" 2>> "$OUT/units-scalar.log" || { echo "UNIT-BUILD-FAIL-sca $t"; UFAIL=1; }
  (cd "$P" && "$b" >> "$OUT/units-scalar.log" 2>&1) || { echo "UNIT-FAIL-scalar $(basename "$t")"; UFAIL=1; }
done
[ "$UFAIL" = "0" ] || { echo UNITS-RC-FAIL-HALT; exit 1; }
for m in default scalar; do
  a=$(grep -c PASS "$OUT/units-$m.log"); l=$(grep -c '^PASS' "$OUT/units-$m.log"); f=$(grep -c '^FAIL' "$OUT/units-$m.log"); s=$(grep -c SKIP "$OUT/units-$m.log")
  fn=$(grep -cE '(^| )fail=[1-9]' "$OUT/units-$m.log" || true); x6f=$(grep -c XFAIL "$OUT/units-$m.log"); x6p=$(grep -c XPASS "$OUT/units-$m.log")
  echo "$m: anywhere=$a linestart=$l FAIL=$f SKIP=$s failN=$fn XFAIL=$x6f XPASS=$x6p"
  [ "$a" = "1083" ] && [ "$l" = "1076" ] && [ "$f" = "0" ] && [ "$s" = "0" ] && [ "$fn" = "0" ] && [ "$x6f" = "6" ] && [ "$x6p" = "6" ] || { echo UNITS-BAR-FAIL-HALT-$m; exit 1; }
done
echo UNITS-1083-1076-X2-PASS
run_tier() {
  echo "== tier $1 ($2, tier=$3 seeds=$4 off=$5) =="
  (cd "$P" && python3 tests/battery/battery.py --oracle "$OUT/oracle_probe" --port "$OUT/$2" --out "$OUT/$1" --tier "$3" --seeds "$4" --seed-offset "$5" --selectors e00,e01,e05,e09 2>&1 | tail -3; echo "$1-rc=$?")
  cat "$OUT/$1/summary.json"
}
run_tier land-FULL cli-std full 17 0
run_tier land-HOLD cli-std full 4 1000
run_tier land-FRESH cli-std full 4 2000
run_tier land-d77 cli-std full 77 0
run_tier landpgo-FULL cli-pgo full 17 0
run_tier landpgo-HOLD cli-pgo full 4 1000
run_tier landpgo-FRESH cli-pgo full 4 2000
run_tier landpgo-d77 cli-pgo full 77 0
echo "== smoke x2 vs oracle (std + scalar) =="
(cd "$P" && python3 tests/battery/battery.py --oracle "$OUT/oracle_probe" --port "$OUT/cli-std" --out "$OUT/smoke-std" --tier smoke --selectors e00,e01,e05,e09 2>&1 | tail -3)
cat "$OUT/smoke-std/summary.json"
(cd "$P" && python3 tests/battery/battery.py --oracle "$OUT/oracle_probe" --port "$OUT/cli-sca" --out "$OUT/smoke-sca" --tier smoke --selectors e00,e01,e05,e09 2>&1 | tail -3)
cat "$OUT/smoke-sca/summary.json"
for t in smoke-std smoke-sca; do
  fc=$(python3 -c "import json;print(json.load(open('$OUT/$t/summary.json'))['fail_cells'])")
  ccells=$(python3 -c "import json;print(json.load(open('$OUT/$t/summary.json'))['cells'])")
  echo "$t cells=$ccells fail_cells=$fc"
  [ "$fc" = "0" ] && [ "$ccells" = "1232" ] || { echo SMOKE-FAIL-HALT-$t; exit 1; }
done
echo SMOKE-1232-X2-PASS
uptime
echo R27LAND-DONE
