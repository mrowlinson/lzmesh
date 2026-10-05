#!/bin/sh
# r27-flex4 E2 byte-gate batch (Air batch remote, non-timing). Runs from worktree root:
#   sh port/bench/air/r27flex4/batch-e2.sh
# Order: pins -> oracle-gate -> builds -> IDENTs -> units (swap runs) -> batteries -> -S.
# Verdicts in result JSONs; exit 1 on any gate FAIL (byte-gate = zero-diverge bar).
# Output: port/results/r27flex4-E2/ (+logs). tmp/ NOT synced to Air: all inputs in port/.
set -u
cd "$(dirname "$0")/../../.." || exit 2
OUT=results/r27flex4-E2
LOGD=$OUT/logs
KEEP=e2-keep/r27flex4
A23=bench/air/r23mL9
A4=bench/air/r27flex4
VENC=$A23/enc-07700a11.c; VPIN=07700a112e4aa098e55ee903fa71c373
EENC=$A4/enc-e2.c; EPIN=197de69ef77d0aa62e8f3e56da2468e9
DECPIN=0dae2df417bd56d06dc30f94fa0ebd31
mkdir -p "$KEEP"
echo "batch-e2 start $(date -u +%FT%TZ) host=$(uname -a)" | tee "$KEEP/batch.log"

echo "== pins ==" | tee -a "$KEEP/batch.log"
md5 "$VENC" "$EENC" src/lzmesh_dec.c bench/droprep1.py 2>&1 | tee -a "$KEEP/batch.log"
[ "$(md5 -q "$VENC")" = "$VPIN" ] || { echo VEH-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$EENC")" = "$EPIN" ] || { echo E2-PIN-FAIL-HALT; exit 1; }
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
echo "== builds veh/e2v ==" | tee -a "$KEEP/batch.log"
build_one veh "$VENC" 2>&1 | tee -a "$KEEP/batch.log"
build_one e2v "$EENC" 2>&1 | tee -a "$KEEP/batch.log"
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
echo "== IDENT 12/12 noPGO (veh vs e2v) ==" | tee -a "$KEEP/batch.log"
ident12 IDENT-e2v "$KEEP/cli-veh" "$KEEP/cli-e2v" "$KEEP/ident-e2v.txt"
echo "== PGO-IDENT 12/12 (veh-PGO vs e2v-PGO) ==" | tee -a "$KEEP/batch.log"
ident12 PGOIDENT-e2v "$KEEP/cli-veh-pgo" "$KEEP/cli-e2v-pgo" "$KEEP/pgoident-e2v.txt"
rm -f "$KEEP/n.bin" "$KEEP/g.bin"
echo IDENTS-OK | tee -a "$KEEP/batch.log"

# units: base(src) + V0-swap(control) + E2-swap(variant), each default + TRUE-scalar.
# E2 effect = variant-vs-control; V0-vs-src drift (if any) is pre-existing, reported not gated.
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
  # needs CLI present). CLI built from CURRENT tree (swap-aware: base/V0/E2).
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
echo "== units V0-swap (control) ==" | tee -a "$KEEP/batch.log"
cp src/lzmesh_enc.c "$KEEP/src-backup.c"
cp "$VENC" src/lzmesh_enc.c
run_units v0ctl
echo "== units E2-swap (variant) ==" | tee -a "$KEEP/batch.log"
cp "$EENC" src/lzmesh_enc.c
run_units e2var
cp "$KEEP/src-backup.c" src/lzmesh_enc.c 2>/dev/null || echo "RESTORE-SOURCE-MISSING-HALT"
[ "$(md5 -q src/lzmesh_enc.c)" = "$SRCMD5" ] || { echo SRC-RESTORE-FAIL-HALT; exit 1; }
echo "SRC-RESTORED md5=$(md5 -q src/lzmesh_enc.c)" | tee -a "$KEEP/batch.log"

# batteries veh-vs-e2v (no make clean after this point: results/ must survive).
mkdir -p "$LOGD" "$OUT"
cp "$KEEP/cli-veh" ./cli-base
cp "$KEEP/cli-e2v" ./cli-e2v
BAT="python3 tests/battery/battery.py --oracle ./cli-base --port ./cli-e2v --selectors e00,e01,e05,e09 --tier full"
$BAT --seeds 17 --seed-offset 0 --out "$OUT/full17" >"$LOGD/bat-full17.log" 2>&1
echo "full17 rc=$? $(cat "$OUT/full17/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 4 --seed-offset 17 --out "$OUT/hold" >"$LOGD/bat-hold.log" 2>&1
echo "hold rc=$? $(cat "$OUT/hold/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 4 --seed-offset 21 --out "$OUT/fresh" >"$LOGD/bat-fresh.log" 2>&1
echo "fresh rc=$? $(cat "$OUT/fresh/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
SMOKE1="python3 tests/battery/battery.py --oracle ./cli-base --port ./cli-e2v --tier smoke"
$SMOKE1 --out "$OUT/smoke1" >"$LOGD/bat-smoke1.log" 2>&1
echo "smoke1 rc=$? $(cat "$OUT/smoke1/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$SMOKE1 --selectors e00,e01,e05,e09 --out "$OUT/smoke4" >"$LOGD/bat-smoke4.log" 2>&1
echo "smoke4 rc=$? $(cat "$OUT/smoke4/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"
$BAT --seeds 77 --seed-offset 0 --out "$OUT/d77" >"$LOGD/bat-d77.log" 2>&1
echo "d77 rc=$? $(cat "$OUT/d77/summary.json" 2>/dev/null || echo NO-SUMMARY)" | tee -a "$KEEP/batch.log"

echo "== -S census (veh vs e2v slot_best region) ==" | tee -a "$KEEP/batch.log"
cc -O2 -std=c11 -Iinclude -S -o "$KEEP/veh.s" "$VENC" 2>>"$KEEP/batch.log" || { echo S-VEH-FAIL; exit 1; }
cc -O2 -std=c11 -Iinclude -S -o "$KEEP/e2v.s" "$EENC" 2>>"$KEEP/batch.log" || { echo S-E2-FAIL; exit 1; }
{
  echo "--- file line totals ---"
  wc -l "$KEEP/veh.s" "$KEEP/e2v.s"
  echo "--- cbnz/tbnz/cmp totals (veh vs e2v) ---"
  echo -n "veh: "; grep -c -E 'cb[nz]|tb[nz]|cmp\t|cmp ' "$KEEP/veh.s"
  echo -n "e2v: "; grep -c -E 'cb[nz]|tb[nz]|cmp\t|cmp ' "$KEEP/e2v.s"
  echo "--- filt_maxd refs (kept halves, veh vs e2v; slot_best fns inline, no spans) ---"
  echo -n "veh: "; grep -c filt_maxd "$KEEP/veh.s"
  echo -n "e2v: "; grep -c filt_maxd "$KEEP/e2v.s"
  echo "--- ccmp/cset totals (veh vs e2v) ---"
  echo -n "veh ccmp/cset: "; grep -c ccmp "$KEEP/veh.s"; grep -c -E 'cset[[:space:]]' "$KEEP/veh.s"
  echo -n "e2v ccmp/cset: "; grep -c ccmp "$KEEP/e2v.s"; grep -c -E 'cset[[:space:]]' "$KEEP/e2v.s"
} 2>&1 | tee "$KEEP/Scensus.txt" | tee -a "$KEEP/batch.log"

mv "$KEEP"/* "$LOGD"/ 2>/dev/null
rmdir "$KEEP" 2>/dev/null; rmdir e2-keep 2>/dev/null
rm -f ./cli-base ./cli-e2v
echo "=== VERDICTS ===" | tee -a "$LOGD/batch.log"
for t in full17 hold fresh smoke1 smoke4 d77; do
  f="$OUT/$t/summary.json"
  [ -f "$f" ] && python3 -c "import json;s=json.load(open('$f'));print('$t',s.get('cells'),s.get('fail_cells'),s.get('verdict'))" | tee -a "$LOGD/batch.log"
done
echo "batch-e2 end $(date -u +%FT%TZ)" | tee -a "$LOGD/batch.log"
