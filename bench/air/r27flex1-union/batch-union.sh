#!/bin/sh
# r27-flex1 h3fact-union byte-gate batch (Air or local, non-timing). Runs from worktree root:
#   sh port/bench/air/r27flex1-union/batch-union.sh
# Order: builds+units first (make clean churn), batteries last (results/ survives:
# make clean does rm -rf results). Verdicts in result JSONs; exit 0 unless a BUILD fails.
set -u
cd "$(dirname "$0")/../../.." || exit 2
KEEP=ubl-keep/r27flex1
OUT=results/r27flex1-UNION
LOGD=$OUT/logs
mkdir -p "$KEEP"
echo "batch-union start $(date -u +%FT%TZ) host=$(uname -a)" | tee "$KEEP/batch.log"

UFLAGS="-O2 -std=c11 -Wall -Wextra -Iinclude -DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9"
DFLAGS="-O2 -std=c11 -Wall -Wextra -Iinclude"

# 1. preprocess-identity: patched noflag vs pristine extract
cc -E -P $DFLAGS -o "$KEEP/patched.i" src/lzmesh_enc.c 2>>"$KEEP/batch.log"
cc -E -P $DFLAGS -o "$KEEP/pristine.i" bench/air/r27flex1-union/enc-pristine.c 2>>"$KEEP/batch.log"
if cmp -s "$KEEP/patched.i" "$KEEP/pristine.i"; then
  echo "PREPROC IDENTICAL" | tee -a "$KEEP/batch.log"
elif diff -bB -q "$KEEP/pristine.i" "$KEEP/patched.i" >/dev/null 2>&1; then
  echo "PREPROC IDENTICAL-modulo-whitespace (ifdef line-splits only)" | tee -a "$KEEP/batch.log"
  diff "$KEEP/pristine.i" "$KEEP/patched.i" | wc -l | tee -a "$KEEP/batch.log"
else
  echo "PREPROC DIFF (adjudicate):" | tee -a "$KEEP/batch.log"
  diff "$KEEP/pristine.i" "$KEEP/patched.i" | head -n 40 | tee -a "$KEEP/batch.log"
  diff "$KEEP/pristine.i" "$KEEP/patched.i" | wc -l | tee -a "$KEEP/batch.log"
fi
md5 bench/air/r27flex1-union/enc-pristine.c src/lzmesh_enc.c | tee -a "$KEEP/batch.log"

# 2. base CLI (no flags)
make clean >/dev/null 2>&1
if ! make port_cli >"$KEEP/build-base.log" 2>&1; then echo "BUILD BASE FAIL"; tail -n 20 "$KEEP/build-base.log"; exit 1; fi
cp port_cli cli-base
echo "base warn: $(grep -ci warning "$KEEP/build-base.log") md5: $(md5 -q cli-base)" | tee -a "$KEEP/batch.log"

# 3. union CLI (3 flags)
make clean >/dev/null 2>&1
if ! make port_cli CFLAGS="$UFLAGS" >"$KEEP/build-union.log" 2>&1; then echo "BUILD UNION FAIL"; tail -n 20 "$KEEP/build-union.log"; exit 1; fi
cp port_cli cli-union
echo "union warn: $(grep -ci warning "$KEEP/build-union.log") md5: $(md5 -q cli-union)" | tee -a "$KEEP/batch.log"

# 4. units: union default + union TRUE-scalar + base default (BEFORE batteries: clean churn)
unit_counts() {
  _log="$1"
  _any=$(grep -c PASS "$_log" 2>/dev/null); _any=${_any:-0}
  _line=$(grep -c "^PASS" "$_log" 2>/dev/null); _line=${_line:-0}
  _fail=$(grep -ci "FAIL" "$_log" 2>/dev/null); _fail=${_fail:-0}
  echo "anywhere=$_any linestart=$_line failgrep=$_fail"
}
make clean >/dev/null 2>&1
make unit CFLAGS="$UFLAGS" >"$KEEP/unit-union-default.log" 2>&1
echo "unit-union-default rc=$? $(unit_counts "$KEEP/unit-union-default.log")" | tee -a "$KEEP/batch.log"
make clean >/dev/null 2>&1
make unit CFLAGS="$UFLAGS -DLZMESH_SCALAR=1" >"$KEEP/unit-union-scalar.log" 2>&1
echo "unit-union-scalar rc=$? scalar-lines=$(grep -c LZMESH_SCALAR "$KEEP/unit-union-scalar.log") $(unit_counts "$KEEP/unit-union-scalar.log")" | tee -a "$KEEP/batch.log"
make clean >/dev/null 2>&1
make unit >"$KEEP/unit-base-default.log" 2>&1
echo "unit-base-default rc=$? $(unit_counts "$KEEP/unit-base-default.log")" | tee -a "$KEEP/batch.log"
# rebuild CLIs post-unit-churn (clean wiped port_cli; cli-base/cli-union copies survive)
make clean >/dev/null 2>&1
make port_cli >"$KEEP/build-base2.log" 2>&1 && cp port_cli cli-base
make clean >/dev/null 2>&1
make port_cli CFLAGS="$UFLAGS" >"$KEEP/build-union2.log" 2>&1 && cp port_cli cli-union
echo "rebuilt md5 base=$(md5 -q cli-base) union=$(md5 -q cli-union)" | tee -a "$KEEP/batch.log"

# 5. batteries tip-vs-tree (base=oracle, union=port). NO make clean after this point.
mkdir -p "$LOGD" "$OUT"
BAT="python3 tests/battery/battery.py --oracle ./cli-base --port ./cli-union --selectors e00,e01,e05,e09 --tier full"
$BAT --seeds 17 --seed-offset 0 --out "$OUT/full17" >"$LOGD/bat-full17.log" 2>&1
echo "full17 rc=$? $(cat "$OUT/full17/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 4 --seed-offset 17 --out "$OUT/hold" >"$LOGD/bat-hold.log" 2>&1
echo "hold rc=$? $(cat "$OUT/hold/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 4 --seed-offset 21 --out "$OUT/fresh" >"$LOGD/bat-fresh.log" 2>&1
echo "fresh rc=$? $(cat "$OUT/fresh/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
SMOKE1="python3 tests/battery/battery.py --oracle ./cli-base --port ./cli-union --tier smoke"
$SMOKE1 --out "$OUT/smoke1" >"$LOGD/bat-smoke1.log" 2>&1
echo "smoke1 rc=$? $(cat "$OUT/smoke1/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$SMOKE1 --selectors e00,e01,e05,e09 --out "$OUT/smoke4" >"$LOGD/bat-smoke4.log" 2>&1
echo "smoke4 rc=$? $(cat "$OUT/smoke4/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 77 --seed-offset 0 --out "$OUT/d77" >"$LOGD/bat-d77.log" 2>&1
echo "d77 rc=$? $(cat "$OUT/d77/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"

# 6. consolidate + verdict table
mv "$KEEP"/* "$LOGD"/ 2>/dev/null
rmdir "$KEEP" 2>/dev/null; rmdir ubl-keep 2>/dev/null
echo "=== VERDICTS ===" | tee -a "$LOGD/batch.log"
for t in full17 hold fresh smoke1 smoke4 d77; do
  f="$OUT/$t/summary.json"
  [ -f "$f" ] && python3 -c "import json;s=json.load(open('$f'));print('$t',s.get('cells'),s.get('fail_cells'),s.get('verdict'))" | tee -a "$LOGD/batch.log"
done
echo "batch-union end $(date -u +%FT%TZ)" | tee -a "$LOGD/batch.log"
