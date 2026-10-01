#!/bin/bash
# r3gate.sh: Round-3 combined Air session — oracle-gate + FULL air_matrix.sh per survivor.
# Labels: base + local-byte-gate survivors (FULL NEW=0 + units green locally).
# Per label: swap staged var sources, run a pin-repointed COPY of air_matrix.sh
# (pin lines only: 2 md5s + sha echo; body untouched), stash matrix-ev/ as
# r3gate-ev/<lbl>/. Base runs first; base rc!=0 HALTs the session, a variant
# rc!=0 kills that label only (evidence kept). Then cmp.py base-vs-each on
# matrix/port (SEP/OVERLAP, honest in-process).
# Runs in ~/Projects/LZMESH-jobs via easy-ssh submit from lanes/r3-gate worktree.
# Evidence: r3gate-ev/ (job log, per-label matrix-ev dirs, cmp txts).
set -u
J=$(pwd)
ST=$J/port/bench/air
RG=$ST/r3gate
P=$J/port
EV=$J/r3gate-ev
LOG=$EV/job-r3gate.log
mkdir -p "$EV"
exec >"$LOG" 2>&1
echo "== r3gate start $(date -u +%Y-%m-%dT%H:%M:%SZ) =="

echo "== serial-slot guard =="
if pgrep -f "air_matrix|r3gate|fold29|fold23|p30" >/dev/null 2>&1; then
  n=$(pgrep -f "air_matrix|r3gate|fold29|fold23" 2>/dev/null | wc -l | tr -d ' ')
  echo "slot-probe matches=$n (1=self)"
  [ "$n" -gt 2 ] && { echo SLOT-BUSY-HALT; exit 1; }
fi
echo SLOT-OK

echo "== box facts =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "EXPECT_SHA=d8726b6dd82e56849ae8c52fad1ca491b39174c3 (lanes/lane-4 R2-shipped base)"
echo "EXPECT_ENC_MD5=1cdcc2a9b40b887fc708a349277070c0"
echo "EXPECT_DEC_MD5=dcfcb8832355526a13479aca8427e1a4"
md5 "$P/src/lzmesh_enc.c" "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "1cdcc2a9b40b887fc708a349277070c0" ] || { echo TREE-PIN-FAIL-HALT-enc; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "dcfcb8832355526a13479aca8427e1a4" ] || { echo TREE-PIN-FAIL-HALT-dec; exit 1; }
echo TREE-PIN-OK

echo "== corpus pins =="
(cd "$P" && python3 bench/mkcorpus.py --check bench/corpus) || { echo CORPUS-PIN-FAIL-HALT; exit 1; }
echo CORPUS-PIN-OK

echo "== variant file pins =="
md5 "$RG"/var-*.c
[ "$(md5 -q "$RG/var-textdec-dec.c")" = "1b2498d3952663f3692267af4d16d738" ] || { echo VAR-PIN-FAIL-HALT-textdec; exit 1; }
[ "$(md5 -q "$RG/var-mixdec-dec.c")" = "e9fa0b68b873da3a3108337010854720" ] || { echo VAR-PIN-FAIL-HALT-mixdec; exit 1; }
[ "$(md5 -q "$RG/var-uniondec-dec.c")" = "1b83ad31a1890d34ab9012d2824252ce" ] || { echo VAR-PIN-FAIL-HALT-uniondec; exit 1; }
[ "$(md5 -q "$RG/var-enc9-enc.c")" = "2358776d0181fec0cb5b954af7478c15" ] || { echo VAR-PIN-FAIL-HALT-enc9; exit 1; }
echo VAR-PIN-OK

mkdir -p "$EV/basesrc"
cp "$P/src/lzmesh_enc.c" "$EV/basesrc/lzmesh_enc.c.base"
cp "$P/src/lzmesh_dec.c" "$EV/basesrc/lzmesh_dec.c.base"

echo "== oracle-gate 24/24 (HALT-class) =="
cc -O2 -o "$EV/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$EV/build-oracle.log" || { echo BUILD-ORACLE-FAIL-HALT; exit 1; }
python3 "$ST/oraclegate.py" "$EV/oracle_probe" "$P/bench/corpus" "$EV/oracle-now.bin"
if cmp -s "$EV/oracle-now.bin" "$ST/oracle-air.pinned"; then echo ORACLE-GATE-24/24-PASS; else echo ORACLE-GATE-FAIL-HALT; exit 1; fi

BASE_ENC=1cdcc2a9b40b887fc708a349277070c0
BASE_DEC=dcfcb8832355526a13479aca8427e1a4
BASE_SHA=d8726b6dd82e56849ae8c52fad1ca491b39174c3
RAN=""
run_label() {
  lbl=$1; ef=$2; df=$3
  echo "===== label $lbl ($(date -u +%Y-%m-%dT%H:%M:%SZ)) ====="
  [ -n "$ef" ] && cp "$RG/$ef" "$P/src/lzmesh_enc.c"
  [ -n "$df" ] && cp "$RG/$df" "$P/src/lzmesh_dec.c"
  E=$(md5 -q "$P/src/lzmesh_enc.c"); D=$(md5 -q "$P/src/lzmesh_dec.c")
  echo "label-pins enc=$E dec=$D"
  sed -e "s/$BASE_ENC/$E/g" -e "s/$BASE_DEC/$D/g" -e "s/$BASE_SHA/r3gate-$lbl/" \
    "$ST/air_matrix.sh" > "$EV/air_matrix.$lbl.sh"
  grep "EXPECT_.*MD5" "$EV/air_matrix.$lbl.sh"
  rm -rf "$J/matrix-ev"
  sh "$EV/air_matrix.$lbl.sh"; rc=$?
  echo "label-$lbl air_matrix rc=$rc"
  if [ "$rc" -ne 0 ]; then
    echo "LABEL-FAIL $lbl (rc=$rc)"
    [ -d "$J/matrix-ev" ] && mv "$J/matrix-ev" "$EV/$lbl-FAIL"
    if [ "$lbl" = "base" ]; then echo BASE-FAIL-HALT; exit 1; fi
  else
    mv "$J/matrix-ev" "$EV/$lbl"
    RAN="$RAN $lbl"
  fi
  cp "$EV/basesrc/lzmesh_enc.c.base" "$P/src/lzmesh_enc.c"
  cp "$EV/basesrc/lzmesh_dec.c.base" "$P/src/lzmesh_dec.c"
  echo "restored: $(md5 -q "$P/src/lzmesh_enc.c") $(md5 -q "$P/src/lzmesh_dec.c")"
}
run_label base "" ""
run_label textdec "" var-textdec-dec.c
run_label mixdec "" var-mixdec-dec.c
run_label uniondec "" var-uniondec-dec.c
run_label enc9 var-enc9-enc.c ""
run_label allunion var-enc9-enc.c var-uniondec-dec.c
echo "RAN_LABELS=[$RAN]"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$BASE_ENC" ] || { echo RESTORE-FAIL-enc; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "$BASE_DEC" ] || { echo RESTORE-FAIL-dec; exit 1; }
echo RESTORE-OK

echo "== verdicts base-vs-each (matrix/port) =="
cd "$P"
for lbl in $RAN; do
  [ "$lbl" = "base" ] && continue
  python3 bench/cmp.py "$EV/base/matrix/port" "$EV/$lbl/matrix/port" | tee "$EV/cmp-$lbl.txt"
done

echo "== r3gate end $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
uptime
ls -la "$EV" | head -n 40
