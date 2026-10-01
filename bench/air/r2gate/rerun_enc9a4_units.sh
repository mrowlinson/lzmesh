#!/bin/bash
# rerun_enc9a4_units.sh: re-run enc9a4 unit gates (default + scalar) cleanly.
# Reason: during the main r2gate session a mid-build status probe re-synced
# base sources over the applied A4 file; A4 bins/batteries/matrix are forensically
# genuine (nm + __text), A4 default units are valid (no recompile, A4 objects),
# but A4 scalar units rebuilt from clobbered (base) sources -> re-run both modes
# here as a matched pair. Writes r2gate-ev2/ (does not touch r2gate-ev/).
set -u
J=$(pwd)
RG=$J/port/bench/air/r2gate
P=$J/port
EV=$J/r2gate-ev2
LOG=$EV/job-rerun.log
mkdir -p "$EV"
exec >"$LOG" 2>&1
echo "== rerun start $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
uptime
md5 "$P/src/lzmesh_enc.c" "$RG/var-enc9a4-enc.c"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "ea439a156dba453e98764ace0c034c55" ] || { echo TREE-NOT-BASE-HALT; exit 1; }
[ "$(md5 -q "$RG/var-enc9a4-enc.c")" = "f52bc083ca6eec2d517eaec83d9448c2" ] || { echo VAR-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "saved-bin forensics:"
ls -l "$J/r2gate-ev/bins/bench.enc9a4" "$J/r2gate-ev/bins/bench.base"
nm "$J/r2gate-ev/bins/bench.enc9a4" 2>/dev/null | grep -c "i4_flush\|i5_tr_parse\|t4_drain" | xargs -I{} echo "enc9a4 outline-sym hits={} (want 0)"
nm "$J/r2gate-ev/bins/bench.base" 2>/dev/null | grep -c "i4_flush\|i5_tr_parse\|t4_drain" | xargs -I{} echo "base outline-sym hits={} (want >0)"
cp "$RG/var-enc9a4-enc.c" "$P/src/lzmesh_enc.c"
(cd "$P" && make clean > /dev/null 2>&1 && make port_cli unit > "$EV/units-enc9a4-default.log" 2>&1; echo "default rc=$?")
(cd "$P" && make clean > /dev/null 2>&1 && make LZMESH_SCALAR=1 port_cli unit > "$EV/units-enc9a4-scalar.log" 2>&1; echo "scalar rc=$?")
for mode in default scalar; do
  lg="$EV/units-enc9a4-$mode.log"
  p=$(grep -c "^PASS" "$lg" || true); f=$(grep -c "^FAIL" "$lg" || true)
  xf=$(grep -c "^XFAIL" "$lg" || true); xp=$(grep -c "^XPASS" "$lg" || true)
  sk=$(grep -c "^SKIP" "$lg" || true)
  echo "UNITS enc9a4/$mode PASS=$p FAIL=$f XFAIL=$xf XPASS=$xp SKIP=$sk"
done
cp "$J/r2gate-ev/basesrc/lzmesh_enc.c.base" "$P/src/lzmesh_enc.c"
echo "restored: $(md5 -q "$P/src/lzmesh_enc.c")"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "ea439a156dba453e98764ace0c034c55" ] || { echo RESTORE-FAIL-HALT; exit 1; }
echo RESTORE-OK
echo "== rerun end $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
