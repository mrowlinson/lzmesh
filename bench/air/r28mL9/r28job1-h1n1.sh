#!/bin/sh
# r28-mL9 T1 H1N1-solo byte-gate (Air batch remote, non-timing, slot-window).
# Runs from worktree root: sh port/bench/air/r28mL9/r28job1-h1n1.sh
# Spec: ANSWER-r28-mL9-1-PRELIM Q1 (bytes-only; smoke2/FULL17/HOLD/FRESH/d77/
# units-x2, THEN STOP + BANK; NO timing arm). Adapted from r27flex4 batch-e2.sh
# (vectors + cliskip guards kept). H1N1 is tip-based: base arm builds from
# src/lzmesh_enc.c directly (pin 3fec8aa3); units 2-arm (base + h1n1var, no V0
# control — V0-vs-src drift N/A for a tip vehicle).
# Units run BEFORE batteries (make clean would wipe results/); verdict chain
# smoke->tiers->units honored in evaluation. tmp/ NOT synced: inputs in port/.
# Exit 1 on any gate FAIL (byte-gate = zero-diverge bar).
set -u
cd "$(dirname "$0")/../../.." || exit 2
OUT=results/r28mL9-H1N1
LOGD=$OUT/logs
KEEP=h1n1-keep/r28mL9
A4=bench/air/r28mL9
HENC=$A4/enc-h1n1.c; HPIN=086a5c2a76e62dea76452391e5d59644
ENCPIN=3fec8aa3274f901b1372be85936a77a2
DECPIN=5a147a81d4df9269f52e652981dd5776
mkdir -p "$KEEP"
echo "r28job1-h1n1 start $(date -u +%FT%TZ) host=$(uname -a)" | tee "$KEEP/batch.log"

echo "== pins ==" | tee -a "$KEEP/batch.log"
md5 "$HENC" src/lzmesh_enc.c src/lzmesh_dec.c bench/droprep1.py 2>&1 | tee -a "$KEEP/batch.log"
[ "$(md5 -q "$HENC")" = "$HPIN" ] || { echo H1N1-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q src/lzmesh_enc.c)" = "$ENCPIN" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q src/lzmesh_dec.c)" = "$DECPIN" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK | tee -a "$KEEP/batch.log"

echo "== vectors (e00 fixtures, rsync-shipped, NOT in easy-ssh sync) ==" | tee -a "$KEEP/batch.log"
[ -f tests/unit/vectors/manifest.txt ] || { echo VECTORS-MISSING-HALT; exit 1; }
[ "$(md5 -q tests/unit/vectors/manifest.txt)" = "b3ffd16744c9aa1b9f15277ca4b78c59" ] || { echo VECTORS-MANIFEST-DIV-HALT; exit 1; }
echo "VECTORS-OK files=$(ls tests/unit/vectors | wc -l)" | tee -a "$KEEP/batch.log"

echo "== oracle-gate 24/24 ==" | tee -a "$KEEP/batch.log"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata) || { echo NO-PROFDATA-HALT; exit 1; }
cc -O2 -o "$KEEP/oracle_probe" tests/battery/oracle_probe.c 2>"$KEEP/build-oracle.log" || { echo ORACLE-BUILD-FAIL; exit 1; }
python3 bench/air/oraclegate.py "$KEEP/oracle_probe" bench/corpus "$KEEP/oracle-gate.bin" 2>&1 | tee -a "$KEEP/batch.log"
if cmp -s "$KEEP/oracle-gate.bin" bench/air/oracle-air.pinned; then echo "ORACLE-GATE 24/24 GREEN" | tee -a "$KEEP/batch.log"; else echo ORACLE-GATE-FAIL-HALT; exit 1; fi

CFILES="bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin"
build_one() {
  # $1 = tag, $2 = enc.c path
  TAG=$1; ENC=$2
  mkdir -p "$KEEP/$TAG"
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -fprofile-instr-generate -c -o "$KEEP/$TAG/enc-gen.o" "$ENC" 2>"$KEEP/$TAG-build-gen.log" || { echo "$TAG-GEN-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -fprofile-instr-generate -c -o "$KEEP/$TAG/dec-gen.o" src/lzmesh_dec.c 2>>"$KEEP/$TAG-build-gen.log" || { echo "$TAG-GEN-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -fprofile-instr-generate -o "$KEEP/$TAG-bench-gen" "$KEEP/$TAG/dec-gen.o" "$KEEP/$TAG/enc-gen.o" bench/bench.c 2>>"$KEEP/$TAG-build-gen.log" || { echo "$TAG-GEN-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$KEEP/$TAG-full-%p.profraw" "$KEEP/$TAG-bench-gen" -n 3 $CFILES > /dev/null || { echo "$TAG-TRAIN-FULL-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$KEEP/$TAG-l0-%p.profraw" "$KEEP/$TAG-bench-gen" -n 200 -l 0 $CFILES > /dev/null || { echo "$TAG-TRAIN-L0-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$KEEP/$TAG.profdata" "$KEEP/$TAG-full-"*.profraw "$KEEP/$TAG-l0-"*.profraw || { echo "$TAG-MERGE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -fprofile-instr-use="$KEEP/$TAG.profdata" -c -o "$KEEP/$TAG-enc-use.o" "$ENC" 2>"$KEEP/$TAG-build-use.log" || { echo "$TAG-USE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -fprofile-instr-use="$KEEP/$TAG.profdata" -c -o "$KEEP/$TAG-dec-use.o" src/lzmesh_dec.c 2>>"$KEEP/$TAG-build-use.log" || { echo "$TAG-USE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -fprofile-instr-use="$KEEP/$TAG.profdata" -c -o "$KEEP/$TAG-cli-use.o" src/port_cli.c 2>>"$KEEP/$TAG-build-use.log" || { echo "$TAG-USE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -fprofile-instr-use="$KEEP/$TAG.profdata" -o "$KEEP/cli-$TAG-pgo" "$KEEP/$TAG-dec-use.o" "$KEEP/$TAG-enc-use.o" "$KEEP/$TAG-cli-use.o" 2>>"$KEEP/$TAG-build-use.log" || { echo "$TAG-USE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -c -o "$KEEP/$TAG-enc-nopgo.o" "$ENC" 2>"$KEEP/$TAG-build-nopgo.log" || { echo "$TAG-NOPGO-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -c -o "$KEEP/$TAG-dec-nopgo.o" src/lzmesh_dec.c 2>>"$KEEP/$TAG-build-nopgo.log" || { echo "$TAG-NOPGO-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -c -o "$KEEP/$TAG-cli-nopgo.o" src/port_cli.c 2>>"$KEEP/$TAG-build-nopgo.log" || { echo "$TAG-NOPGO-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -Iinclude -o "$KEEP/cli-$TAG" "$KEEP/$TAG-dec-nopgo.o" "$KEEP/$TAG-enc-nopgo.o" "$KEEP/$TAG-cli-nopgo.o" 2>>"$KEEP/$TAG-build-nopgo.log" || { echo "$TAG-NOPGO-FAIL"; exit 1; }
  echo "$TAG use-warnings=$(grep -ci warning "$KEEP/$TAG-build-use.log" || true) nopgo-warnings=$(grep -ci warning "$KEEP/$TAG-build-nopgo.log" || true) profdata=$(md5 -q "$KEEP/$TAG.profdata")"
}
echo "== builds base/h1n1v ==" | tee -a "$KEEP/batch.log"
build_one base src/lzmesh_enc.c 2>&1 | tee -a "$KEEP/batch.log"
build_one h1n1v "$HENC" 2>&1 | tee -a "$KEEP/batch.log"
echo BUILDS-OK | tee -a "$KEEP/batch.log"

ident12() {
  # $1 = label, $2 = cli-a, $3 = cli-b, $4 = outfile
  : > "$4"
  for corp in text-256k mixed-128k zeros-64k; do
    case $corp in text-256k) f=bench/corpus/text-256k.bin;; mixed-128k) f=bench/corpus/mixed-128k.bin;; *) f=bench/corpus/zeros-64k.bin;; esac
    for lv in e00 e01 e05 e09; do
      "$2" enc $lv < "$f" > "$KEEP/n.bin" 2>/dev/null
      "$3" enc $lv < "$f" > "$KEEP/g.bin" 2>/dev/null
      if cmp -s "$KEEP/n.bin" "$KEEP/g.bin"; then echo "$corp $lv IDENT" >> "$4"; else echo "$corp $lv DIV" >> "$4"; fi
    done
  done
  echo "$1: $(grep -c IDENT "$4")/12" | tee -a "$KEEP/batch.log"
  [ "$(grep -c IDENT "$4")" = "12" ] || { echo "$1-FAIL-HALT"; exit 1; }
}
echo "== IDENT 12/12 noPGO (base vs h1n1v) ==" | tee -a "$KEEP/batch.log"
ident12 IDENT-h1n1v "$KEEP/cli-base" "$KEEP/cli-h1n1v" "$KEEP/ident-h1n1v.txt"
echo "== PGO-IDENT 12/12 (base-PGO vs h1n1v-PGO) ==" | tee -a "$KEEP/batch.log"
ident12 PGOIDENT-h1n1v "$KEEP/cli-base-pgo" "$KEEP/cli-h1n1v-pgo" "$KEEP/pgoident-h1n1v.txt"
rm -f "$KEEP/n.bin" "$KEEP/g.bin"
echo IDENTS-OK | tee -a "$KEEP/batch.log"

# units: base(src) + H1N1-swap(variant), each default + TRUE-scalar.
# H1N1 effect = variant-vs-base (same tree, no V0 control needed).
unit_counts() {
  _log="$1"
  _any=$(grep -c PASS "$_log" 2>/dev/null); _any=${_any:-0}
  _line=$(grep -c "^PASS" "$_log" 2>/dev/null); _line=${_line:-0}
  _fail=$(grep -ci "FAIL" "$_log" 2>/dev/null); _fail=${_fail:-0}
  echo "anywhere=$_any linestart=$_line failgrep=$_fail"
}
SRCMD5=$(md5 -q src/lzmesh_enc.c)
run_units() {
  # $1 = tag. re-plant ./port_cli before EACH make unit (cliskip1: m19 Part B
  # 9 t_pass SKIP-green without CLI => 1074/1067 artifact; canon 1083/1076
  # needs CLI present). CLI built from CURRENT tree (swap-aware: base/H1N1).
  make clean >/dev/null 2>&1
  make port_cli >"$KEEP/unit-$1-cli-default.log" 2>&1 || { echo "unit-$1-CLI-BUILD-FAIL"; exit 1; }
  make unit >"$KEEP/unit-$1-default.log" 2>&1
  echo "unit-$1-default rc=$? $(unit_counts "$KEEP/unit-$1-default.log")" | tee -a "$KEEP/batch.log"
  grep -q "SKIP m19-cli-cap" "$KEEP/unit-$1-default.log" && { echo "unit-$1-CLI-SKIP-HALT"; exit 1; }
  make clean >/dev/null 2>&1
  make port_cli LZMESH_SCALAR=1 >"$KEEP/unit-$1-cli-scalar.log" 2>&1 || { echo "unit-$1-CLI-BUILD-FAIL"; exit 1; }
  make unit LZMESH_SCALAR=1 >"$KEEP/unit-$1-scalar.log" 2>&1
  echo "unit-$1-scalar rc=$? scalar-lines=$(grep -c LZMESH_SCALAR "$KEEP/unit-$1-scalar.log") $(unit_counts "$KEEP/unit-$1-scalar.log")" | tee -a "$KEEP/batch.log"
  grep -q "SKIP m19-cli-cap" "$KEEP/unit-$1-scalar.log" && { echo "unit-$1-CLI-SKIP-HALT"; exit 1; }
}
echo "== units base(src) ==" | tee -a "$KEEP/batch.log"
run_units base
echo "== units H1N1-swap (variant) ==" | tee -a "$KEEP/batch.log"
cp src/lzmesh_enc.c "$KEEP/src-backup.c"
cp "$HENC" src/lzmesh_enc.c
run_units h1n1var
cp "$KEEP/src-backup.c" src/lzmesh_enc.c 2>/dev/null || echo "RESTORE-SOURCE-MISSING-HALT"
[ "$(md5 -q src/lzmesh_enc.c)" = "$SRCMD5" ] || { echo SRC-RESTORE-FAIL-HALT; exit 1; }
echo "SRC-RESTORED md5=$(md5 -q src/lzmesh_enc.c)" | tee -a "$KEEP/batch.log"

# batteries base-vs-h1n1v (no make clean after this point: results/ must survive).
# order: smoke x2 first (fail-fast), then FULL17/HOLD/FRESH/d77 (PRELIM chain).
mkdir -p "$LOGD" "$OUT"
cp "$KEEP/cli-base" ./cli-base
cp "$KEEP/cli-h1n1v" ./cli-h1n1v
BAT="python3 tests/battery/battery.py --oracle ./cli-base --port ./cli-h1n1v --selectors e00,e01,e05,e09 --tier full"
SMOKE1="python3 tests/battery/battery.py --oracle ./cli-base --port ./cli-h1n1v --tier smoke"
$SMOKE1 --out "$OUT/smoke1" >"$LOGD/bat-smoke1.log" 2>&1
echo "smoke1 rc=$? $(cat "$OUT/smoke1/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$SMOKE1 --selectors e00,e01,e05,e09 --out "$OUT/smoke4" >"$LOGD/bat-smoke4.log" 2>&1
echo "smoke4 rc=$? $(cat "$OUT/smoke4/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 17 --seed-offset 0 --out "$OUT/full17" >"$LOGD/bat-full17.log" 2>&1
echo "full17 rc=$? $(cat "$OUT/full17/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 4 --seed-offset 17 --out "$OUT/hold" >"$LOGD/bat-hold.log" 2>&1
echo "hold rc=$? $(cat "$OUT/hold/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 4 --seed-offset 21 --out "$OUT/fresh" >"$LOGD/bat-fresh.log" 2>&1
echo "fresh rc=$? $(cat "$OUT/fresh/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 77 --seed-offset 0 --out "$OUT/d77" >"$LOGD/bat-d77.log" 2>&1
echo "d77 rc=$? $(cat "$OUT/d77/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"

echo "== -S census (base vs h1n1v, whole-file + ccmp/cset; detail = flex1) ==" | tee -a "$KEEP/batch.log"
cc -O2 -std=c11 -Iinclude -S -o "$KEEP/base.s" src/lzmesh_enc.c 2>>"$KEEP/batch.log" || { echo S-BASE-FAIL; exit 1; }
cc -O2 -std=c11 -Iinclude -S -o "$KEEP/h1n1v.s" "$HENC" 2>>"$KEEP/batch.log" || { echo S-H1N1-FAIL; exit 1; }
{
  echo "--- file line totals ---"
  wc -l "$KEEP/base.s" "$KEEP/h1n1v.s"
  echo "--- cbnz/tbnz/cmp totals (base vs h1n1v) ---"
  echo -n "base: "; grep -c -E 'cb[nz]|tb[nz]|cmp\t|cmp ' "$KEEP/base.s"
  echo -n "h1n1v: "; grep -c -E 'cb[nz]|tb[nz]|cmp\t|cmp ' "$KEEP/h1n1v.s"
  echo "--- ccmp/cset totals (base vs h1n1v) ---"
  echo -n "base ccmp/cset: "; grep -c ccmp "$KEEP/base.s"; grep -c -E 'cset[[:space:]]' "$KEEP/base.s"
  echo -n "h1n1v ccmp/cset: "; grep -c ccmp "$KEEP/h1n1v.s"; grep -c -E 'cset[[:space:]]' "$KEEP/h1n1v.s"
} 2>&1 | tee "$KEEP/Scensus.txt" | tee -a "$KEEP/batch.log"

mv "$KEEP"/* "$LOGD"/ 2>/dev/null
rmdir "$KEEP" 2>/dev/null; rmdir h1n1-keep 2>/dev/null
rm -f ./cli-base ./cli-h1n1v
echo "=== VERDICTS ===" | tee -a "$LOGD/batch.log"
for t in full17 hold fresh smoke1 smoke4 d77; do
  f="$OUT/$t/summary.json"
  [ -f "$f" ] && python3 -c "import json;s=json.load(open('$f'));print('$t',s.get('cells'),s.get('fail_cells'),s.get('verdict'))" | tee -a "$LOGD/batch.log"
done
echo "r28job1-h1n1 end $(date -u +%FT%TZ)" | tee -a "$LOGD/batch.log"
