#!/bin/sh
# r27ab-bytes.sh: tL5d R27 L0-ship BYTE gates on d1 vehicle (std-self AND d1pgo CLI).
# Tiers (battery.py): FULL (--seeds 17 off 0, 12784 cells) + HOLD (--seeds 4 off 1000,
# 3008) + FRESH (--seeds 4 off 2000, 3008) + d77 (--seeds 77 off 0, 57904), ALL NEW=0.
# Units: all tests/unit/test_* vs veh lib, default + TRUE-scalar (-DLZMESH_SCALAR
# forced rebuild), 0 FAIL both modes. Smoke: battery --tier smoke (1232 cells) vs
# Apple oracle, default CLI + scalar CLI, 0 fail both.
# Run on Air BATCH remote (non-timing, long: d77 ~1h x2). easy-ssh submit ONLY.
# Output: port/bench/air/r27tL5d/bytes/ (summaries + logs; vectors only on FAIL).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r27tL5d/bytes
D1Q=5a147a81d4df9269f52e652981dd5776
ENCQ=3fec8aa3274f901b1372be85936a77a2
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$P/bench/air/r27tL5d/dec-d1.c" "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r27tL5d/dec-d1.c")" = "$D1Q" ] || { echo D1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build oracle_probe =="
cc -O2 -o "$OUT/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$OUT/build-oracle.log" || { echo BUILD-ORACLE-FAIL-HALT; exit 1; }
echo BUILD-ORACLE-OK
echo "== build veh std lib + cli =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-std.o" "$P/bench/air/r27tL5d/dec-d1.c" 2> "$OUT/build-std.log" || { echo STD-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-std.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-std.log" || { echo STD-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-std.a" "$OUT/dec-std.o" "$OUT/enc-std.o" || { echo STD-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-std.o" "$P/src/port_cli.c" 2>> "$OUT/build-std.log" || { echo STD-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-std" "$OUT/dec-std.o" "$OUT/enc-std.o" "$OUT/cli-std.o" 2>> "$OUT/build-std.log" || { echo STD-LINK-FAIL; exit 1; }
echo "std-warnings=$(grep -ci warning "$OUT/build-std.log" || true)"
echo STD-BUILD-OK
echo "== build veh scalar lib + cli (-DLZMESH_SCALAR) =="
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/dec-sca.o" "$P/bench/air/r27tL5d/dec-d1.c" 2> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/enc-sca.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-sca.a" "$OUT/dec-sca.o" "$OUT/enc-sca.o" || { echo SCA-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/cli-sca.o" "$P/src/port_cli.c" 2>> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -o "$OUT/cli-sca" "$OUT/dec-sca.o" "$OUT/enc-sca.o" "$OUT/cli-sca.o" 2>> "$OUT/build-sca.log" || { echo SCA-LINK-FAIL; exit 1; }
echo "sca-warnings=$(grep -ci warning "$OUT/build-sca.log" || true)"
echo SCA-BUILD-OK
echo "== build veh PGO lib + cli (T0 recipe, std bench train) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
[ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
mkdir -p "$OUT/pgo-gen" "$OUT/prof" "$OUT/pgo-use"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/dec.o" "$P/bench/air/r27tL5d/dec-d1.c" 2> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/pgo-gen/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/pgo-gen/dec.o" "$OUT/pgo-gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-pgo.log" || { echo PGO-LINK-FAIL; exit 1; }
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CORPUS > /dev/null || { echo PGO-TRAIN-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/vehpgo.profdata" "$OUT"/prof/*.profraw || { echo PGO-MERGE-FAIL; exit 1; }
md5 -q "$OUT/vehpgo.profdata" | tee "$OUT/vehpgo.profdata.md5"
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" "-fprofile-instr-use=$OUT/vehpgo.profdata" -c -o "$OUT/pgo-use/dec.o" "$P/bench/air/r27tL5d/dec-d1.c" 2>> "$OUT/build-pgo.log" || { echo PGO-BUILD-FAIL; exit 1; }
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
run_tier() {
  # $1 = out-tag, $2 = cli, $3 = tier, $4 = seeds, $5 = seed-offset
  echo "== tier $1 ($2, tier=$3 seeds=$4 off=$5) =="
  (cd "$P" && python3 tests/battery/battery.py --oracle "$OUT/oracle_probe" --port "$OUT/$2" --out "$OUT/$1" --tier "$3" --seeds "$4" --seed-offset "$5" --selectors e00,e01,e05,e09 2>&1 | tail -5; echo "$1-rc=$?")
  cat "$OUT/$1/summary.json"
}
BYTE_FAIL=0
run_tier std-FULL cli-std full 17 0
run_tier std-HOLD cli-std full 4 1000
run_tier std-FRESH cli-std full 4 2000
run_tier std-d77 cli-std full 77 0
run_tier pgo-FULL cli-pgo full 17 0
run_tier pgo-HOLD cli-pgo full 4 1000
run_tier pgo-FRESH cli-pgo full 4 2000
run_tier pgo-d77 cli-pgo full 77 0
for t in std-FULL std-HOLD std-FRESH std-d77 pgo-FULL pgo-HOLD pgo-FRESH pgo-d77; do
  fc=$(python3 -c "import json;print(json.load(open('$OUT/$t/summary.json'))['fail_cells'])")
  echo "$t fail_cells=$fc"
  [ "$fc" = "0" ] || BYTE_FAIL=1
done
echo "== smoke x2 vs oracle (std + scalar) =="
(cd "$P" && python3 tests/battery/battery.py --oracle "$OUT/oracle_probe" --port "$OUT/cli-std" --out "$OUT/smoke-std" --tier smoke --selectors e00,e01,e05,e09 2>&1 | tail -3)
cat "$OUT/smoke-std/summary.json"
(cd "$P" && python3 tests/battery/battery.py --oracle "$OUT/oracle_probe" --port "$OUT/cli-sca" --out "$OUT/smoke-sca" --tier smoke --selectors e00,e01,e05,e09 2>&1 | tail -3)
cat "$OUT/smoke-sca/summary.json"
for t in smoke-std smoke-sca; do
  fc=$(python3 -c "import json;print(json.load(open('$OUT/$t/summary.json'))['fail_cells'])")
  echo "$t fail_cells=$fc"
  [ "$fc" = "0" ] || BYTE_FAIL=1
done
echo "== units default + TRUE-scalar (port_cli first) =="
mkdir -p "$OUT/unit-default" "$OUT/unit-scalar"
: > "$OUT/units-default.log"; : > "$OUT/units-scalar.log"
UFAIL=0
for t in "$P"/tests/unit/test_*.c; do
  b="$OUT/unit-default/$(basename "$t" .c)"
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$b" "$t" "$OUT/lib-std.a" 2>> "$OUT/units-default.log" || { echo "UNIT-BUILD-FAIL $t"; UFAIL=1; }
  (cd "$P" && "$b" >> "$OUT/units-default.log" 2>&1) || { echo "UNIT-FAIL-default $(basename "$t")"; UFAIL=1; }
  bs="$OUT/unit-scalar/$(basename "$t" .c)"
  cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -o "$bs" "$t" "$OUT/lib-sca.a" 2>> "$OUT/units-scalar.log" || { echo "UNIT-BUILD-FAIL-sca $t"; UFAIL=1; }
  (cd "$P" && "$bs" >> "$OUT/units-scalar.log" 2>&1) || { echo "UNIT-FAIL-scalar $(basename "$t")"; UFAIL=1; }
done
echo "units-default PASS=$(grep -c '^PASS' "$OUT/units-default.log") FAIL=$(grep -c '^FAIL' "$OUT/units-default.log")"
echo "units-scalar PASS=$(grep -c '^PASS' "$OUT/units-scalar.log") FAIL=$(grep -c '^FAIL' "$OUT/units-scalar.log")"
[ "$UFAIL" = "0" ] || BYTE_FAIL=1
uptime
if [ "$BYTE_FAIL" != "0" ]; then echo R27T5D-BYTES-FAIL-HALT; exit 1; fi
echo R27T5D-BYTES-DONE
