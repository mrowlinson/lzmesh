#!/bin/sh
# r27-flex8 matrix-assist-shards successor: tier + units + smoke batch (Air batch remote, non-timing).
# Vendored from r27-flex5 batch-tiers.sh @6af0cd4e6 (FORMAL bar intact) + cleanwipe fix.
# Implements matrix PARTIAL LEFT-3 verbatim. Runs from port/:
#   sh bench/air/r27flex8/batch-tiers.sh
# Order: r26ref-backup -> pins -> oracle-gate -> cli builds -> units x2 -> smoke x2 ->
# gate arm (tip-vs-tip) -> forensics arm (oracle-vs-tip) -> merges -> verdicts.
# make clean rm-rfs results/ (Makefile:230): R26 refs MUST be backed up first;
# forensics-cmp reads the backup. NO make clean after batteries start.
# make unit needs ./port_cli PRESENT (R26 standing note): m19 Part B runs 9
# CLI-edge PASS via the binary, else SKIP-green -> 1074/1067 artifact
# (NOT tree drift). script re-plants cli-tip/cli-tip-scalar before each
# make unit + HALTs on "CLI edges skipped".
# Output: results/r27matrix-{hold,fresh,full,d77} + -vsoracle + smoke/smoke-scalar
# + r27matrix-logs/ (KEEP). tmp/ NOT synced to Air: merge vendored in A5/.
set -u
cd "$(dirname "$0")/../../.." || exit 2
A5=bench/air/r27flex8
KEEP=tiers-keep/r27flex8
OUT=results
ENCPIN=3fec8aa3274f901b1372be85936a77a2
DECPIN=0dae2df417bd56d06dc30f94fa0ebd31
CLIPIN=f3f961f5535ce48d625163ea1095ce3e
MERGEPIN=553bf1981689340159b7b9ef3b660169
ORACLEPIN=1e627ce5
mkdir -p "$KEEP"
echo "batch-tiers start $(date -u +%FT%TZ) host=$(uname -a) ncpu=$(sysctl -n hw.ncpu 2>/dev/null || echo ?)" | tee "$KEEP/batch.log"

echo "== r26ref backup (before any make clean) ==" | tee -a "$KEEP/batch.log"
mkdir -p "$KEEP/r26ref"
cp -r "$OUT"/r26matrix-hold-vsoracle "$OUT"/r26matrix-fresh-vsoracle "$OUT"/r26matrix-full-vsoracle "$OUT"/r26matrix-d77-vsoracle "$KEEP/r26ref/" 2>&1 | tee -a "$KEEP/batch.log"
REFN=0; for t in hold fresh full d77; do [ -f "$KEEP/r26ref/r26matrix-$t-vsoracle/findings.jsonl" ] && REFN=$((REFN+1)); done
echo "r26ref backed up: $REFN/4 vsoracle findings" | tee -a "$KEEP/batch.log"
[ "$REFN" = "4" ] || { echo R26REF-MISSING-HALT; exit 1; }

echo "== fixtures preflight (fail-fast min-0, not mid-run) ==" | tee -a "$KEEP/batch.log"
[ -f tests/unit/vectors/manifest.txt ] || { echo VECTORS-MISSING-HALT; echo "FIX: vectors/ is .easy-ssh-ignore'd; ship explicitly: rsync -a port/tests/unit/vectors/ <remote-job>/port/tests/unit/vectors/ (216K) then re-submit"; exit 1; }
echo "vectors files=$(ls tests/unit/vectors/*.bin 2>/dev/null | wc -l | tr -d ' ')" | tee -a "$KEEP/batch.log"
CNBN=$(ls bench/corpus/*.bin 2>/dev/null | wc -l | tr -d ' '); CNBN=${CNBN:-0}
echo "corpus bins=$CNBN" | tee -a "$KEEP/batch.log"
[ "$CNBN" -ge 1 ] || { echo CORPUS-MISSING-HALT; exit 1; }
echo PREFLIGHT-OK | tee -a "$KEEP/batch.log"

echo "== pins ==" | tee -a "$KEEP/batch.log"
md5 src/lzmesh_enc.c src/lzmesh_dec.c src/port_cli.c "$A5/merge_shards.py" tests/battery/battery.py 2>&1 | tee -a "$KEEP/batch.log"
[ "$(md5 -q src/lzmesh_enc.c)" = "$ENCPIN" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q src/lzmesh_dec.c)" = "$DECPIN" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q src/port_cli.c)" = "$CLIPIN" ] || { echo CLI-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$A5/merge_shards.py")" = "$MERGEPIN" ] || { echo MERGE-PIN-FAIL-HALT; exit 1; }
echo PINS-OK | tee -a "$KEEP/batch.log"

echo "== oracle-gate 24/24 ==" | tee -a "$KEEP/batch.log"
cc -O2 -o "$KEEP/oracle_probe" tests/battery/oracle_probe.c 2>"$KEEP/build-oracle.log" || { echo ORACLE-BUILD-FAIL; exit 1; }
python3 bench/air/oraclegate.py "$KEEP/oracle_probe" bench/corpus "$KEEP/oracle-gate.bin" 2>&1 | tee -a "$KEEP/batch.log"
if cmp -s "$KEEP/oracle-gate.bin" bench/air/oracle-air.pinned; then echo "ORACLE-GATE 24/24 GREEN sha=$ORACLEPIN" | tee -a "$KEEP/batch.log"; else echo ORACLE-GATE-FAIL-HALT; exit 1; fi

echo "== cli builds (tip default + TRUE-scalar) ==" | tee -a "$KEEP/batch.log"
make clean >/dev/null 2>&1
make port_cli >"$KEEP/build-tip.log" 2>&1 || { echo TIP-BUILD-FAIL; exit 1; }
echo "tip warnings=$(grep -ci warning "$KEEP/build-tip.log" || true) md5=$(md5 -q port_cli)" | tee -a "$KEEP/batch.log"
cp port_cli "$KEEP/cli-tip"
[ "$(grep -ci warning "$KEEP/build-tip.log" || true)" = "0" ] || { echo TIP-WARN-FAIL-HALT; exit 1; }
make clean >/dev/null 2>&1
make port_cli LZMESH_SCALAR=1 >"$KEEP/build-scal.log" 2>&1 || { echo SCAL-BUILD-FAIL; exit 1; }
echo "scal warnings=$(grep -ci warning "$KEEP/build-scal.log" || true) dlines=$(grep -c DLZMESH_SCALAR "$KEEP/build-scal.log" || true) md5=$(md5 -q port_cli)" | tee -a "$KEEP/batch.log"
cp port_cli "$KEEP/cli-tip-scalar"
[ "$(md5 -q "$KEEP/cli-tip")" != "$(md5 -q "$KEEP/cli-tip-scalar")" ] || { echo BINS-IDENTICAL-FAIL-HALT; exit 1; }
"$KEEP/cli-tip" enc e05 < bench/corpus/text-256k.bin > "$KEEP/s-tip.bin" 2>/dev/null
"$KEEP/cli-tip-scalar" enc e05 < bench/corpus/text-256k.bin > "$KEEP/s-scal.bin" 2>/dev/null
cmp -s "$KEEP/s-tip.bin" "$KEEP/s-scal.bin" || { echo STREAMS-DIV-FAIL-HALT; exit 1; }
echo "STREAMS-IDENTICAL md5=$(md5 -q "$KEEP/s-tip.bin")" | tee -a "$KEEP/batch.log"
rm -f "$KEEP/s-tip.bin" "$KEEP/s-scal.bin"
echo BUILDS-OK | tee -a "$KEEP/batch.log"

unit_counts() {
  _log="$1"
  _any=$(grep -c PASS "$_log" 2>/dev/null); _any=${_any:-0}
  _line=$(grep -c "^PASS" "$_log" 2>/dev/null); _line=${_line:-0}
  _fail=$(grep -ci "FAIL" "$_log" 2>/dev/null); _fail=${_fail:-0}
  echo "anywhere=$_any linestart=$_line failgrep=$_fail"
}
echo "== units default ==" | tee -a "$KEEP/batch.log"
make clean >/dev/null 2>&1
cp "$KEEP/cli-tip" ./port_cli
make unit >"$KEEP/unit-default.log" 2>&1; URC=$?
echo "unit-default rc=$URC $(unit_counts "$KEEP/unit-default.log")" | tee -a "$KEEP/batch.log"
[ "$URC" = "0" ] || { echo UNIT-DEFAULT-RC-FAIL-HALT; exit 1; }
[ "$(grep -c "CLI edges skipped" "$KEEP/unit-default.log" || true)" = "0" ] || { echo CLI-SKIP-DEFAULT-HALT; exit 1; }
[ "$(grep -c "^FAIL" "$KEEP/unit-default.log" || true)" = "0" ] || { echo BARE-FAIL-DEFAULT-HALT; exit 1; }
[ "$(grep -cE "(^| )fail=[1-9]" "$KEEP/unit-default.log" || true)" = "0" ] || { echo FAILN-DEFAULT-HALT; exit 1; }
[ "$(grep -c XFAIL "$KEEP/unit-default.log" || true)" = "6" ] || { echo XFAIL-COUNT-DEFAULT-HALT; exit 1; }
[ "$(grep -c XPASS "$KEEP/unit-default.log" || true)" = "6" ] || { echo XPASS-COUNT-DEFAULT-HALT; exit 1; }
echo "== units TRUE-scalar ==" | tee -a "$KEEP/batch.log"
make clean >/dev/null 2>&1
cp "$KEEP/cli-tip-scalar" ./port_cli
make unit LZMESH_SCALAR=1 >"$KEEP/unit-scalar.log" 2>&1; USRC=$?
echo "unit-scalar rc=$USRC scalar-lines=$(grep -c LZMESH_SCALAR "$KEEP/unit-scalar.log" || true) $(unit_counts "$KEEP/unit-scalar.log")" | tee -a "$KEEP/batch.log"
[ "$USRC" = "0" ] || { echo UNIT-SCALAR-RC-FAIL-HALT; exit 1; }
[ "$(grep -c "CLI edges skipped" "$KEEP/unit-scalar.log" || true)" = "0" ] || { echo CLI-SKIP-SCALAR-HALT; exit 1; }
[ "$(grep -c "^FAIL" "$KEEP/unit-scalar.log" || true)" = "0" ] || { echo BARE-FAIL-SCALAR-HALT; exit 1; }
[ "$(grep -cE "(^| )fail=[1-9]" "$KEEP/unit-scalar.log" || true)" = "0" ] || { echo FAILN-SCALAR-HALT; exit 1; }
[ "$(grep -c XFAIL "$KEEP/unit-scalar.log" || true)" = "6" ] || { echo XFAIL-COUNT-SCALAR-HALT; exit 1; }
[ "$(grep -c XPASS "$KEEP/unit-scalar.log" || true)" = "6" ] || { echo XPASS-COUNT-SCALAR-HALT; exit 1; }
SCALCC=$(grep -c "^cc " "$KEEP/unit-scalar.log" || true); SCALD=$(grep -c "^cc .*LZMESH_SCALAR" "$KEEP/unit-scalar.log" || true)
echo "scalar census cc=$SCALCC dflag=$SCALD (bar 50/50)" | tee -a "$KEEP/batch.log"
{ [ "$SCALCC" = "50" ] && [ "$SCALD" = "50" ]; } || { echo SCALAR-CENSUS-HALT; exit 1; }
echo UNITS-OK | tee -a "$KEEP/batch.log"

# NO make clean from here on (results/ must survive). Batteries:
mkdir -p "$OUT"
cp "$KEEP/cli-tip" ./cli-tip
cp "$KEEP/cli-tip-scalar" ./cli-tip-scalar
ORACLE="$KEEP/oracle_probe"
SMK="python3 tests/battery/battery.py --tier smoke"
echo "== smoke x2 (vs oracle) ==" | tee -a "$KEEP/batch.log"
$SMK --oracle "$ORACLE" --port ./cli-tip --out "$OUT/r27matrix-smoke" >"$KEEP/bat-smoke.log" 2>&1
echo "smoke rc=$? $(cat "$OUT/r27matrix-smoke/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$SMK --oracle "$ORACLE" --port ./cli-tip-scalar --out "$OUT/r27matrix-smoke-scalar" >"$KEEP/bat-smoke-scalar.log" 2>&1
echo "smoke-scalar rc=$? $(cat "$OUT/r27matrix-smoke-scalar/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"

run_tier() {
  # $1=label $2=oracle-side $3=port-side $4=seeds $5=offset $6=outdir $7=log
  python3 tests/battery/battery.py --oracle "$2" --port "$3" --selectors e00,e01,e05,e09 \
    --tier full --seeds "$4" --seed-offset "$5" --out "$6" >"$7" 2>&1
  echo "$1 rc=$? $(cat "$6/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
}
echo "== gate arm tip-vs-tip ==" | tee -a "$KEEP/batch.log"
run_tier gate-hold ./cli-tip ./cli-tip 4 1000 "$OUT/r27matrix-hold" "$KEEP/bat-gate-hold.log"
run_tier gate-fresh ./cli-tip ./cli-tip 4 2000 "$OUT/r27matrix-fresh" "$KEEP/bat-gate-fresh.log"
run_tier gate-full-s0 ./cli-tip ./cli-tip 5 0 "$KEEP/g-full-s0" "$KEEP/bat-gate-full-s0.log"
run_tier gate-full-s1 ./cli-tip ./cli-tip 4 5 "$KEEP/g-full-s1" "$KEEP/bat-gate-full-s1.log"
run_tier gate-full-s2 ./cli-tip ./cli-tip 4 9 "$KEEP/g-full-s2" "$KEEP/bat-gate-full-s2.log"
run_tier gate-full-s3 ./cli-tip ./cli-tip 4 13 "$KEEP/g-full-s3" "$KEEP/bat-gate-full-s3.log"
run_tier gate-d77-g0 ./cli-tip ./cli-tip 13 0 "$KEEP/g-d77-g0" "$KEEP/bat-gate-d77-g0.log"
run_tier gate-d77-g1 ./cli-tip ./cli-tip 13 13 "$KEEP/g-d77-g1" "$KEEP/bat-gate-d77-g1.log"
run_tier gate-d77-g2 ./cli-tip ./cli-tip 13 26 "$KEEP/g-d77-g2" "$KEEP/bat-gate-d77-g2.log"
run_tier gate-d77-g3 ./cli-tip ./cli-tip 13 39 "$KEEP/g-d77-g3" "$KEEP/bat-gate-d77-g3.log"
run_tier gate-d77-g4 ./cli-tip ./cli-tip 13 52 "$KEEP/g-d77-g4" "$KEEP/bat-gate-d77-g4.log"
run_tier gate-d77-g5 ./cli-tip ./cli-tip 12 65 "$KEEP/g-d77-g5" "$KEEP/bat-gate-d77-g5.log"
echo "== forensics arm oracle-vs-tip ==" | tee -a "$KEEP/batch.log"
run_tier fore-hold "$ORACLE" ./cli-tip 4 1000 "$OUT/r27matrix-hold-vsoracle" "$KEEP/bat-fore-hold.log"
run_tier fore-fresh "$ORACLE" ./cli-tip 4 2000 "$OUT/r27matrix-fresh-vsoracle" "$KEEP/bat-fore-fresh.log"
run_tier fore-full-s0 "$ORACLE" ./cli-tip 5 0 "$KEEP/f-full-s0" "$KEEP/bat-fore-full-s0.log"
run_tier fore-full-s1 "$ORACLE" ./cli-tip 4 5 "$KEEP/f-full-s1" "$KEEP/bat-fore-full-s1.log"
run_tier fore-full-s2 "$ORACLE" ./cli-tip 4 9 "$KEEP/f-full-s2" "$KEEP/bat-fore-full-s2.log"
run_tier fore-full-s3 "$ORACLE" ./cli-tip 4 13 "$KEEP/f-full-s3" "$KEEP/bat-fore-full-s3.log"
run_tier fore-d77-g0 "$ORACLE" ./cli-tip 13 0 "$KEEP/f-d77-g0" "$KEEP/bat-fore-d77-g0.log"
run_tier fore-d77-g1 "$ORACLE" ./cli-tip 13 13 "$KEEP/f-d77-g1" "$KEEP/bat-fore-d77-g1.log"
run_tier fore-d77-g2 "$ORACLE" ./cli-tip 13 26 "$KEEP/f-d77-g2" "$KEEP/bat-fore-d77-g2.log"
run_tier fore-d77-g3 "$ORACLE" ./cli-tip 13 39 "$KEEP/f-d77-g3" "$KEEP/bat-fore-d77-g3.log"
run_tier fore-d77-g4 "$ORACLE" ./cli-tip 13 52 "$KEEP/f-d77-g4" "$KEEP/bat-fore-d77-g4.log"
run_tier fore-d77-g5 "$ORACLE" ./cli-tip 12 65 "$KEEP/f-d77-g5" "$KEEP/bat-fore-d77-g5.log"

echo "== merges ==" | tee -a "$KEEP/batch.log"
python3 "$A5/merge_shards.py" "$OUT/r27matrix-full" full 0 16 "$KEEP/g-full-s0" "$KEEP/g-full-s1" "$KEEP/g-full-s2" "$KEEP/g-full-s3" 2>&1 | tee -a "$KEEP/batch.log"
python3 "$A5/merge_shards.py" "$OUT/r27matrix-d77" d77 0 76 "$KEEP/g-d77-g0" "$KEEP/g-d77-g1" "$KEEP/g-d77-g2" "$KEEP/g-d77-g3" "$KEEP/g-d77-g4" "$KEEP/g-d77-g5" 2>&1 | tee -a "$KEEP/batch.log"
python3 "$A5/merge_shards.py" "$OUT/r27matrix-full-vsoracle" full 0 16 "$KEEP/f-full-s0" "$KEEP/f-full-s1" "$KEEP/f-full-s2" "$KEEP/f-full-s3" 2>&1 | tee -a "$KEEP/batch.log"
python3 "$A5/merge_shards.py" "$OUT/r27matrix-d77-vsoracle" d77 0 76 "$KEEP/f-d77-g0" "$KEEP/f-d77-g1" "$KEEP/f-d77-g2" "$KEEP/f-d77-g3" "$KEEP/f-d77-g4" "$KEEP/f-d77-g5" 2>&1 | tee -a "$KEEP/batch.log"

echo "== forensics vs R26 filed (backup: results/ was cleaned) ==" | tee -a "$KEEP/batch.log"
FFAIL=0
for t in hold fresh full d77; do
  if cmp -s "$OUT/r27matrix-$t-vsoracle/findings.jsonl" "$KEEP/r26ref/r26matrix-$t-vsoracle/findings.jsonl"; then
    echo "vsoracle-$t BYTE-IDENTICAL-TO-R26 md5=$(md5 -q "$OUT/r27matrix-$t-vsoracle/findings.jsonl")" | tee -a "$KEEP/batch.log"
  else
    echo "vsoracle-$t DIFFERS-FROM-R26 md5new=$(md5 -q "$OUT/r27matrix-$t-vsoracle/findings.jsonl") md5r26=$(md5 -q "$KEEP/r26ref/r26matrix-$t-vsoracle/findings.jsonl")" | tee -a "$KEEP/batch.log"
    echo "LADDER-$t: oracle-gate was GREEN above => tree-drift suspect; bisect: (1) one-shard single-seed re-run vs sharded output, (2) full-tier single-run iff inconclusive; gate-arm 0-NEW is supporting-only" | tee -a "$KEEP/batch.log"
    FFAIL=1
  fi
done

rm -f ./cli-tip ./cli-tip-scalar
rm -f "$KEEP/cli-tip" "$KEEP/cli-tip-scalar" "$KEEP/oracle_probe" ./port_cli
rm -f "$KEEP"/n.bin "$KEEP"/g.bin
mkdir -p "$OUT/r27matrix-logs"
mv "$KEEP"/* "$OUT/r27matrix-logs"/ 2>/dev/null
rmdir "$KEEP" 2>/dev/null; rmdir tiers-keep 2>/dev/null
echo "=== VERDICTS ===" | tee -a "$OUT/r27matrix-logs/batch.log"
FAIL=0
for t in hold fresh full d77; do
  f="$OUT/r27matrix-$t/summary.json"
  [ -f "$f" ] && python3 -c "import json;s=json.load(open('$f'));print('gate-$t',s.get('cells'),s.get('fail_cells'),s.get('verdict'))" | tee -a "$OUT/r27matrix-logs/batch.log"
  NFC=$(python3 -c "import json;print(json.load(open('$f')).get('fail_cells'))" 2>/dev/null || echo X)
  [ "$NFC" = "0" ] || FAIL=1
  case $t in hold) WANT=3008;; fresh) WANT=3008;; full) WANT=12784;; d77) WANT=57904;; esac
  NCC=$(python3 -c "import json;print(json.load(open('$f')).get('cells'))" 2>/dev/null || echo X)
  [ "$NCC" = "$WANT" ] || FAIL=1
done
SFAIL=0
for t in smoke smoke-scalar; do
  f="$OUT/r27matrix-$t/summary.json"
  [ -f "$f" ] && python3 -c "import json;s=json.load(open('$f'));print('$t',s.get('cells'),s.get('fail_cells'),s.get('verdict'))" | tee -a "$OUT/r27matrix-logs/batch.log"
  SFC=$(python3 -c "import json;print(json.load(open('$f')).get('fail_cells'))" 2>/dev/null || echo X)
  SCCC=$(python3 -c "import json;print(json.load(open('$f')).get('cells'))" 2>/dev/null || echo X)
  { [ "$SFC" = "0" ] && [ "$SCCC" = "1232" ]; } || SFAIL=1
done
for t in hold fresh full d77; do
  f="$OUT/r27matrix-$t-vsoracle/summary.json"
  [ -f "$f" ] && python3 -c "import json;s=json.load(open('$f'));print('fore-$t',s.get('cells'),s.get('fail_cells'),s.get('verdict'))" | tee -a "$OUT/r27matrix-logs/batch.log"
done
echo "batch-tiers end $(date -u +%FT%TZ) gateFAIL=$FAIL foreFAIL=$FFAIL smokeFAIL=$SFAIL" | tee -a "$OUT/r27matrix-logs/batch.log"
[ "$FAIL" = "0" ] || { echo GATE-NEW-NONZERO-HALT; exit 1; }
[ "$FFAIL" = "0" ] || { echo FORENSICS-DIV-HALT; exit 1; }
[ "$SFAIL" = "0" ] || { echo SMOKE-NON1232-HALT; exit 1; }
echo TIERS-GREEN
