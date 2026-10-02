#!/bin/sh
# r11gates.sh: Air-batch v2 byte-gates (non-timing). Builds base/new CLIs
# from STAGED dec files + tree enc/cli, oracle_probe from tree, then runs:
# smoke (tier smoke vs oracle) + prescreen FULL/HOLD/FRESH x slack/exact +
# failparity x slack/exact. Run on Air via:
#   easy-ssh --remote batch submit "sh port/bench/air/r11lastblock/gates/r11gates.sh"
# Output: port/bench/air/r11lastblock/gates-out/. Pull with:
# easy-ssh --remote batch pull port/bench/air/r11lastblock/gates-out
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../../.." && pwd)
STAGE=$P/bench/air/r11lastblock
OUT=$STAGE/gates-out
rm -rf "$OUT"
mkdir -p "$OUT"
echo "== pins =="
md5 "$STAGE/dec-base.c" "$STAGE/dec-new.c" "$P/src/lzmesh_enc.c" "$P/src/port_cli.c"
[ "$(md5 -q "$STAGE/dec-base.c")" = "bb5f2b9a107241fd2b8ca444a8b25286" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$STAGE/dec-new.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo NEW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "41ec58b7aa0621fea606dc6f5c258831" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/port_cli.c")" = "f3f961f5535ce48d625163ea1095ce3e" ] || { echo CLI-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build base/new cli + oracle_probe =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/port_cli-base" "$STAGE/dec-base.c" "$P/src/lzmesh_enc.c" "$P/src/port_cli.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/port_cli-new" "$STAGE/dec-new.c" "$P/src/lzmesh_enc.c" "$P/src/port_cli.c" 2> "$OUT/build-new.log" || { echo NEW-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -o "$OUT/oracle_probe" "$P/tests/battery/oracle_probe.c" 2> "$OUT/build-oracle.log" || { echo ORACLE-BUILD-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
echo "new-warnings=$(grep -ci warning "$OUT/build-new.log" || true)"
echo BUILD-OK
echo "== smoke (tier smoke vs oracle) =="
(cd "$P" && python3 tests/battery/battery.py --oracle "$OUT/oracle_probe" --port "$OUT/port_cli-new" --out "$OUT/smoke-new" --tier smoke 2>&1 | tee "$OUT/smoke-new-driver.log"; echo "smoke-rc=$?")
echo "== prescreen FULL/HOLD/FRESH x slack/exact =="
(cd "$P" && python3 "$STAGE/gates/prescreen_dec.py" --base "$OUT/port_cli-base" --new "$OUT/port_cli-new" --seeds 17 --seed-offset 0 --jobs 8 --capmode slack --out "$OUT/full-slack" 2>&1 | tail -n 1; echo "full-slack-rc=$?")
(cd "$P" && python3 "$STAGE/gates/prescreen_dec.py" --base "$OUT/port_cli-base" --new "$OUT/port_cli-new" --seeds 4 --seed-offset 17 --jobs 8 --capmode slack --out "$OUT/hold-slack" 2>&1 | tail -n 1; echo "hold-slack-rc=$?")
(cd "$P" && python3 "$STAGE/gates/prescreen_dec.py" --base "$OUT/port_cli-base" --new "$OUT/port_cli-new" --seeds 4 --seed-offset 21 --jobs 8 --capmode slack --out "$OUT/fresh-slack" 2>&1 | tail -n 1; echo "fresh-slack-rc=$?")
(cd "$P" && python3 "$STAGE/gates/prescreen_dec.py" --base "$OUT/port_cli-base" --new "$OUT/port_cli-new" --seeds 17 --seed-offset 0 --jobs 8 --capmode exact --out "$OUT/full-exact" 2>&1 | tail -n 1; echo "full-exact-rc=$?")
(cd "$P" && python3 "$STAGE/gates/prescreen_dec.py" --base "$OUT/port_cli-base" --new "$OUT/port_cli-new" --seeds 4 --seed-offset 17 --jobs 8 --capmode exact --out "$OUT/hold-exact" 2>&1 | tail -n 1; echo "hold-exact-rc=$?")
(cd "$P" && python3 "$STAGE/gates/prescreen_dec.py" --base "$OUT/port_cli-base" --new "$OUT/port_cli-new" --seeds 4 --seed-offset 21 --jobs 8 --capmode exact --out "$OUT/fresh-exact" 2>&1 | tail -n 1; echo "fresh-exact-rc=$?")
echo "== failparity slack + exact =="
(cd "$P" && python3 "$STAGE/gates/failparity11.py" "$OUT/port_cli-base" "$OUT/port_cli-new" "$OUT/failparity-slack.tsv" 1024 2>&1; echo "failparity-slack-rc=$?")
(cd "$P" && python3 "$STAGE/gates/failparity11.py" "$OUT/port_cli-base" "$OUT/port_cli-new" "$OUT/failparity-exact.tsv" 0 2>&1; echo "failparity-exact-rc=$?")
echo R11GATES-DONE
