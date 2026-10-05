#!/bin/sh
# r27ab-tipbase.sh: tL5d R27 TIP-BASELINE for tier NEW-diff adjudication + scalar cc-lines echo.
# Cause: d1 bytes tiers show HOLD 3 / FRESH 8 / d77 90 fail_cells, 101/101 ENC_DIFF
# (vehicle is dec-only; enc identical tree code; std/pgo findings byte-identical).
# This job builds TIP-std CLI (dec-tip.c 0dae2df4) + runs HOLD/FRESH/d77 on the SAME
# box/oracle, then diffs finding cell-IDs vs d1 findings: 0 NEW = (e) MET.
# Also re-runs the d1 scalar build+units with echoed cc lines (TRUE-scalar flag evidence).
# Run on Air BATCH remote (non-timing). Output: port/bench/air/r27tL5d/tipbase/.
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r27tL5d/tipbase
TIPQ=0dae2df417bd56d06dc30f94fa0ebd31
D1Q=5a147a81d4df9269f52e652981dd5776
ENCQ=3fec8aa3274f901b1372be85936a77a2
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== box =="
uptime
echo "== pins =="
md5 "$P/bench/air/r27tL5d/dec-tip.c" "$P/bench/air/r27tL5d/dec-d1.c" "$P/src/lzmesh_enc.c"
[ "$(md5 -q "$P/bench/air/r27tL5d/dec-tip.c")" = "$TIPQ" ] || { echo TIP-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/bench/air/r27tL5d/dec-d1.c")" = "$D1Q" ] || { echo D1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$ENCQ" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build oracle_probe =="
cc -O2 -o "$OUT/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$OUT/build-oracle.log" || { echo BUILD-ORACLE-FAIL-HALT; exit 1; }
echo "== build tip-std CLI =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec-tip.o" "$P/bench/air/r27tL5d/dec-tip.c" 2> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-tip" "$OUT/dec-tip.o" "$OUT/enc.o" "$OUT/cli.o" 2>> "$OUT/build-tip.log" || { echo TIP-LINK-FAIL; exit 1; }
echo "tip-warnings=$(grep -ci warning "$OUT/build-tip.log" || true)"
echo TIP-BUILD-OK
run_tier() {
  # $1 = out-tag, $2 = seeds, $3 = seed-offset
  echo "== tier $1 (cli-tip, seeds=$2 off=$3) =="
  (cd "$P" && python3 tests/battery/battery.py --oracle "$OUT/oracle_probe" --port "$OUT/cli-tip" --out "$OUT/$1" --tier full --seeds "$2" --seed-offset "$3" --selectors e00,e01,e05,e09 2>&1 | tail -3; echo "$1-rc=$?")
  cat "$OUT/$1/summary.json"
}
run_tier tip-HOLD 4 1000
run_tier tip-FRESH 4 2000
run_tier tip-d77 77 0
echo "== NEW-diff vs d1 findings (cell-ID sets) =="
NEW_FAIL=0
for t in HOLD FRESH d77; do
  python3 -c "
import json
d1=set((json.loads(l)['input'],json.loads(l)['selector'],json.loads(l)['check']) for l in open('$P/bench/air/r27tL5d/bytes/std-$t/findings.jsonl'))
tip=set((json.loads(l)['input'],json.loads(l)['selector'],json.loads(l)['check']) for l in open('$OUT/tip-$t/findings.jsonl'))
new=d1-tip
print('$t: d1=%d tip=%d NEW=%d' % (len(d1),len(tip),len(new)))
for c in sorted(new)[:20]: print('  NEW:',c)
open('$OUT/NEW-$t.txt','w').write('\n'.join('%s %s %s'%c for c in sorted(new)))
"
  n=$(wc -l < "$OUT/NEW-$t.txt" | tr -d ' ')
  [ "$n" = "0" ] || NEW_FAIL=1
done
echo "== scalar echo-rebuild + units (TRUE-scalar cc-lines evidence) =="
mkdir -p "$OUT/sca" "$OUT/unit-scalar2"
: > "$OUT/build-sca2.log"; : > "$OUT/units-scalar2.log"
{
echo "cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I\$P/include -c dec-d1.c"
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/sca/dec.o" "$P/bench/air/r27tL5d/dec-d1.c" 2>> "$OUT/build-sca2.log"
echo "cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I\$P/include -c lzmesh_enc.c"
cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -c -o "$OUT/sca/enc.o" "$P/src/lzmesh_enc.c" 2>> "$OUT/build-sca2.log"
ar rcs "$OUT/sca/lib.a" "$OUT/sca/dec.o" "$OUT/sca/enc.o"
} 2>&1 | tee -a "$OUT/build-sca2.log" | grep -c DLZMESH_SCALAR | tee "$OUT/sca-flag-count.txt"
UFAIL=0
for t in "$P"/tests/unit/test_*.c; do
  b="$OUT/unit-scalar2/$(basename "$t" .c)"
  echo "cc -DLZMESH_SCALAR $t" >> "$OUT/build-sca2.log"
  cc -O2 -std=c11 -Wall -Wextra -DLZMESH_SCALAR -I"$P/include" -o "$b" "$t" "$OUT/sca/lib.a" 2>> "$OUT/build-sca2.log" || { echo "UNIT-BUILD-FAIL $t"; UFAIL=1; }
  (cd "$P" && "$b" >> "$OUT/units-scalar2.log" 2>&1) || { echo "UNIT-FAIL-scalar2 $(basename "$t")"; UFAIL=1; }
done
echo "units-scalar2 PASS=$(grep -c '^PASS' "$OUT/units-scalar2.log") FAIL=$(grep -c '^FAIL' "$OUT/units-scalar2.log")"
echo "sca-cc-lines-with-flag=$(grep -c DLZMESH_SCALAR "$OUT/build-sca2.log")"
[ "$UFAIL" = "0" ] || NEW_FAIL=1
uptime
if [ "$NEW_FAIL" != "0" ]; then echo R27T5D-TIPBASE-NEW-FOUND; exit 1; fi
echo R27T5D-TIPBASE-DONE
