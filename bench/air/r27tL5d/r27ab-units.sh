#!/bin/sh
# r27ab-units.sh: tL5d R27 UNITS re-run (fixes VOID: m19-cli-cap SKIP, no ./port_cli).
# Builds port_cli FIRST per mode (std + TRUE-scalar), then runs every
# tests/unit/test_* binary linked against the d1-vehicle lib, from port/ with
# ./port_cli present. Echoes scalar cc lines (flag evidence).
# Bar: 1083-anywhere / 1076-linestart, 0 FAIL, SKIP=0, BOTH modes TRUE-scalar + default.
# Run on Air BATCH remote (non-timing, minutes). Output: port/bench/air/r27tL5d/units/.
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r27tL5d/units
D1Q=5a147a81d4df9269f52e652981dd5776
ENCQ=3fec8aa3274f901b1372be85936a77a2
rm -rf "$OUT"
mkdir -p "$OUT/bin-default" "$OUT/bin-scalar"
echo "== pins =="
md5 "$P/bench/air/r27tL5d/dec-d1.c" "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r27tL5d/dec-d1.c")" = "$D1Q" ] || { echo D1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build libs + port_cli FIRST (default + scalar) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-def.o" "$P/bench/air/r27tL5d/dec-d1.c" 2> "$OUT/build-def.log" || { echo DEF-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-def.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-def.log" || { echo DEF-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-def.a" "$OUT/dec-def.o" "$OUT/enc-def.o" || { echo DEF-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-def.o" "$P/src/port_cli.c" 2>> "$OUT/build-def.log" || { echo DEF-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$P/port_cli" "$OUT/dec-def.o" "$OUT/enc-def.o" "$OUT/cli-def.o" 2>> "$OUT/build-def.log" || { echo DEF-LINK-FAIL; exit 1; }
echo "def-warnings=$(grep -ci warning "$OUT/build-def.log" || true)"
: > "$OUT/build-sca.log"
echo "cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I\$P/include -c dec-d1.c" | tee -a "$OUT/build-sca.log"
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/dec-sca.o" "$P/bench/air/r27tL5d/dec-d1.c" 2>> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
echo "cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I\$P/include -c lzmesh_enc.c" | tee -a "$OUT/build-sca.log"
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/enc-sca.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-sca.a" "$OUT/dec-sca.o" "$OUT/enc-sca.o" || { echo SCA-AR-FAIL; exit 1; }
echo "cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I\$P/include -o port_cli_scalar" | tee -a "$OUT/build-sca.log"
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/cli-sca.o" "$P/src/port_cli.c" 2>> "$OUT/build-sca.log" || { echo SCA-BUILD-FAIL; exit 1; }
echo "sca-warnings=$(grep -ci warning "$OUT/build-sca.log" || true)"
echo SCA-LIB-OK
UFAIL=0
echo "== units default (port_cli present) =="
: > "$OUT/units-default.log"
for t in "$P"/tests/unit/test_*.c; do
  b="$OUT/bin-default/$(basename "$t" .c)"
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$b" "$t" "$OUT/lib-def.a" 2>> "$OUT/units-default.log" || { echo "UNIT-BUILD-FAIL $t"; UFAIL=1; }
  (cd "$P" && "$b" >> "$OUT/units-default.log" 2>&1) || { echo "UNIT-FAIL-default $(basename "$t")"; UFAIL=1; }
done
echo "default anywhere=$(grep -c PASS "$OUT/units-default.log") linestart=$(grep -c '^PASS' "$OUT/units-default.log") FAIL=$(grep -c '^FAIL' "$OUT/units-default.log") SKIPLS=$(grep -c '^SKIP' "$OUT/units-default.log")"
echo "== units scalar (port_cli present; scalar CLI linked as ./port_cli) =="
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -o "$P/port_cli" "$OUT/dec-sca.o" "$OUT/enc-sca.o" "$OUT/cli-sca.o" 2>> "$OUT/build-sca.log" || { echo SCA-LINK-FAIL; exit 1; }
echo "cc -DLZMESH_SCALAR $P/tests/unit/test_*.c (each)" | tee -a "$OUT/build-sca.log"
: > "$OUT/units-scalar.log"
for t in "$P"/tests/unit/test_*.c; do
  b="$OUT/bin-scalar/$(basename "$t" .c)"
  cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -o "$b" "$t" "$OUT/lib-sca.a" 2>> "$OUT/units-scalar.log" || { echo "UNIT-BUILD-FAIL-sca $t"; UFAIL=1; }
  (cd "$P" && "$b" >> "$OUT/units-scalar.log" 2>&1) || { echo "UNIT-FAIL-scalar $(basename "$t")"; UFAIL=1; }
done
echo "scalar anywhere=$(grep -c PASS "$OUT/units-scalar.log") linestart=$(grep -c '^PASS' "$OUT/units-scalar.log") FAIL=$(grep -c '^FAIL' "$OUT/units-scalar.log") SKIPLS=$(grep -c '^SKIP' "$OUT/units-scalar.log")"
echo "sca-cc-lines-with-flag=$(grep -c DLZMESH_SCALAR "$OUT/build-sca.log")"
for m in default scalar; do
  a=$(grep -c PASS "$OUT/units-$m.log"); l=$(grep -c '^PASS' "$OUT/units-$m.log"); f=$(grep -c '^FAIL' "$OUT/units-$m.log"); s=$(grep -c '^SKIP' "$OUT/units-$m.log")
  echo "$m: anywhere=$a linestart=$l FAIL=$f SKIP=$s"
  [ "$a" = "1083" ] || { echo "$m-ANYWHERE-$a-NE-1083"; UFAIL=1; }
  [ "$l" = "1076" ] || { echo "$m-LINESTART-$l-NE-1076"; UFAIL=1; }
  [ "$f" = "0" ] || UFAIL=1
  [ "$s" = "0" ] || UFAIL=1
done
uptime
if [ "$UFAIL" != "0" ]; then echo R27T5D-UNITS-FAIL-HALT; exit 1; fi
echo R27T5D-UNITS-DONE
