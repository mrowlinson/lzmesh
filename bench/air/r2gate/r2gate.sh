#!/bin/bash
# r2gate.sh: Round-2 combined gate job — byte gates + unit gates + n=70 matrix.
# Trees: base + 4 admitted variants (store=V1-fused-l0try, decv1=u32peek,
# decv2=reguard, enc9a4=DRAIN-FUSE), all FULL-prescreen-PASS locally.
# Runs in ~/Projects/LZMESH-jobs on MacBookAir via easy-ssh submit from the
# lanes/r2-gate worktree (everything gate-needed lives under port/bench/air).
# Evidence: r2gate-ev/ (job log, build/unit logs, batteries, matrix TSVs, cmps).
#
# Phase order (serial slot, quiet-check, cool-downs):
#   0. slot guard + tree/corpus pins + base build + oracle-gate (HALT on drift)
#   1. per-tree builds (serial): bins + units default/scalar, warnings fatal
#   2. batteries per tree (5 parallel bg jobs): FULL/HOLD/FRESH, full keys
#   3. NEW/FIXED per variant x tier vs base (expect 0/0)
#   4. quiet-wait + cool-down, then ONE interleaved n=70 round-robin matrix
#   5. cmp.py base-vs-each
set -u
J=$(pwd)
ST=$J/port/bench/air
RG=$ST/r2gate
P=$J/port
EV=$J/r2gate-ev
LOG=$EV/job-r2gate.log
mkdir -p "$EV" "$EV/bins" "$EV/bat"
exec >"$LOG" 2>&1
echo "== r2gate start $(date -u +%Y-%m-%dT%H:%M:%SZ) =="

echo "== serial-slot guard =="
if pgrep -f "air_matrix|r2gate|fold29|fold23|p30" >/dev/null 2>&1; then
  n=$(pgrep -f "air_matrix|r2gate|fold29|fold23" 2>/dev/null | wc -l | tr -d ' ')
  echo "slot-probe matches=$n (1=self)"
  [ "$n" -gt 2 ] && { echo SLOT-BUSY-HALT; exit 1; }
fi
echo SLOT-OK

echo "== box facts =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "EXPECT_SHA=401b06b2 (lanes/lane-4 P30-ship base)"
echo "EXPECT_ENC_MD5=ea439a156dba453e98764ace0c034c55"
echo "EXPECT_DEC_MD5=84c00b6ba2f2c9ed4f0fb5f33b1d3738"
md5 "$P/src/lzmesh_enc.c" "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "ea439a156dba453e98764ace0c034c55" ] || { echo TREE-PIN-FAIL-HALT-enc; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "84c00b6ba2f2c9ed4f0fb5f33b1d3738" ] || { echo TREE-PIN-FAIL-HALT-dec; exit 1; }
echo TREE-PIN-OK

echo "== corpus pins =="
(cd "$P" && python3 bench/mkcorpus.py --check bench/corpus) || { echo CORPUS-PIN-FAIL-HALT; exit 1; }
echo CORPUS-PIN-OK

echo "== variant file pins =="
md5 "$RG"/var-*.c
[ "$(md5 -q "$RG/var-store-enc.c")" = "0fff43ee567da68ed0b5beacbdafa292" ] || { echo VAR-PIN-FAIL-HALT-store; exit 1; }
[ "$(md5 -q "$RG/var-decv1-dec.c")" = "7b7ce763b3666938e4b87ef11106a208" ] || { echo VAR-PIN-FAIL-HALT-decv1; exit 1; }
[ "$(md5 -q "$RG/var-decv2-dec.c")" = "dcfcb8832355526a13479aca8427e1a4" ] || { echo VAR-PIN-FAIL-HALT-decv2; exit 1; }
[ "$(md5 -q "$RG/var-enc9a4-enc.c")" = "f52bc083ca6eec2d517eaec83d9448c2" ] || { echo VAR-PIN-FAIL-HALT-enc9a4; exit 1; }
echo VAR-PIN-OK

echo "== build base lib + bench + abench + encdump + port_cli (warnings fatal) =="
(cd "$P" && make matrix-bins port_cli > "$EV/build-base.log" 2>&1; echo "build-base rc=$?")
w=$(grep -ci "warning" "$EV/build-base.log" || true)
echo "base-warnings=$w"
[ "$w" -eq 0 ] || { echo WARNINGS-FAIL-HALT-base; exit 1; }
echo BUILD-BASE-OK
echo "== build oracle_probe (oracle-gate + batteries) =="
cc -O2 -o "$EV/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$EV/build-oracle.log" || { echo BUILD-ORACLE-FAIL-HALT; exit 1; }
echo BUILD-ORACLE-OK

echo "== oracle-gate 24/24 (pinned macOS-27 Air oracle) =="
python3 "$ST/oraclegate.py" "$EV/oracle_probe" "$P/bench/corpus" "$EV/oracle-now.bin"
if cmp -s "$EV/oracle-now.bin" "$ST/oracle-air.pinned"; then echo ORACLE-GATE-24/24-PASS; else echo ORACLE-GATE-FAIL-HALT; exit 1; fi

cp "$P/bench/bench" "$EV/bins/bench.base"
cp "$P/port_cli" "$EV/bins/port_cli.base"
chmod +x "$EV/bins/bench.base" "$EV/bins/port_cli.base"

# build_tree <label>: units default+scalar for the CURRENT port/ sources.
# bins must already be saved by the caller. Appends to UNITS.txt.
build_units() {
  lbl=$1
  (cd "$P" && make unit > "$EV/units-$lbl-default.log" 2>&1; echo "$lbl-default units rc=$?")
  (cd "$P" && make clean > /dev/null 2>&1 && make LZMESH_SCALAR=1 port_cli unit > "$EV/units-$lbl-scalar.log" 2>&1; echo "$lbl-scalar units rc=$?")
  for mode in default scalar; do
    lg="$EV/units-$lbl-$mode.log"
    p=$(grep -c "^PASS" "$lg" || true); f=$(grep -c "^FAIL" "$lg" || true)
    xf=$(grep -c "^XFAIL" "$lg" || true); xp=$(grep -c "^XPASS" "$lg" || true)
    sk=$(grep -c "^SKIP" "$lg" || true)
    echo "UNITS $lbl/$mode PASS=$p FAIL=$f XFAIL=$xf XPASS=$xp SKIP=$sk" | tee -a "$EV/UNITS.txt"
  done
}

: > "$EV/UNITS.txt"
echo "== units base (default + scalar) =="
build_units base
(cd "$P" && make clean > /dev/null 2>&1 && make matrix-bins port_cli > /dev/null 2>&1)

# Variant builds (serial; a build failure kills that variant, not the session).
# Pushed trees exclude .git, so base sources restore from saved copies.
BUILT="base"
mkdir -p "$EV/basesrc"
cp "$P/src/lzmesh_enc.c" "$EV/basesrc/lzmesh_enc.c.base"
cp "$P/src/lzmesh_dec.c" "$EV/basesrc/lzmesh_dec.c.base"

build_variant() {
  lbl=$1; varfile=$2; target=$3
  echo "== build variant $lbl ($varfile -> $target) =="
  cp "$RG/$varfile" "$P/src/$target"
  (cd "$P" && make clean > /dev/null 2>&1 && make matrix-bins port_cli > "$EV/build-$lbl.log" 2>&1; echo "build-$lbl rc=$?")
  w=$(grep -ci "warning" "$EV/build-$lbl.log" || true)
  echo "$lbl-warnings=$w"
  if [ "$w" -ne 0 ]; then
    echo "VARIANT-BUILD-FAIL $lbl (warnings=$w) — killed, session continues"
  else
    cp "$P/bench/bench" "$EV/bins/bench.$lbl"
    cp "$P/port_cli" "$EV/bins/port_cli.$lbl"
    chmod +x "$EV/bins/bench.$lbl" "$EV/bins/port_cli.$lbl"
    echo "BUILD-OK $lbl"
    build_units "$lbl"
    BUILT="$BUILT $lbl"
  fi
  cp "$EV/basesrc/$target.base" "$P/src/$target"
  echo "restored $target: $(md5 -q "$P/src/$target")"
}
build_variant store var-store-enc.c lzmesh_enc.c
build_variant decv1 var-decv1-dec.c lzmesh_dec.c
build_variant decv2 var-decv2-dec.c lzmesh_dec.c
build_variant enc9a4 var-enc9a4-enc.c lzmesh_enc.c
echo "BUILT_LABELS=[$BUILT]"
(cd "$P" && make clean > /dev/null 2>&1 && make matrix-bins port_cli > /dev/null 2>&1)
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "ea439a156dba453e98764ace0c034c55" ] || { echo RESTORE-FAIL-HALT-enc; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "84c00b6ba2f2c9ed4f0fb5f33b1d3738" ] || { echo RESTORE-FAIL-HALT-dec; exit 1; }
echo RESTORE-OK

echo "== batteries per tree (FULL/HOLD/FRESH, full keys, 5 parallel) =="
run_bat() {
  lbl=$1
  for spec in "full 17 0" "hold 4 17" "fresh 4 21"; do
    set -- $spec; tier=$1; seeds=$2; off=$3
    out="$EV/bat/$lbl-$tier"
    python3 "$P/tests/battery/battery.py" --oracle "$EV/oracle_probe" \
      --port "$EV/bins/port_cli.$lbl" --tier full --selectors e00,e01,e05,e09 \
      --seeds "$seeds" --seed-offset "$off" --out "$out" > "$out.log" 2>&1
    echo "$lbl-$tier rc=$? $(grep -o '"verdict": "[A-Z]*"' "$out.log" | head -1) $(grep -o '"cells": [0-9]*' "$out.log" | head -1)"
  done
}
for lbl in $BUILT; do run_bat "$lbl" > "$EV/bat-$lbl-driver.log" 2>&1 & done
wait
echo "== battery driver logs =="
cat "$EV"/bat-*-driver.log
echo "== battery summaries =="
for d in "$EV"/bat/*-full "$EV"/bat/*-hold "$EV"/bat/*-fresh; do
  [ -d "$d" ] || continue
  echo "--- $d"
  cat "$d/summary.json"
done

echo "== NEW/FIXED per variant x tier vs base =="
: > "$EV/newfixed.txt"
for lbl in $BUILT; do
  [ "$lbl" = "base" ] && continue
  for tier in full hold fresh; do
    python3 "$RG/newfixed.py" "$EV/bat/base-$tier/findings.jsonl" \
      "$EV/bat/$lbl-$tier/findings.jsonl" "$lbl/$tier" | tee -a "$EV/newfixed.txt"
  done
done

echo "== quiet-wait (load1 < 4, 5 tries x 120s) + cool-down 60s =="
tries=0
while [ $tries -lt 5 ]; do
  l1=$(uptime | sed -e 's/.*load averages*:[[:space:]]*//' -e 's/,//g' | awk '{print $1}')
  echo "try $tries load1=$l1 $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  if awk -v l="$l1" 'BEGIN{exit !(l < 4.0)}'; then break; fi
  tries=$((tries+1)); sleep 120
done
uptime; sleep 60; uptime
CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"

echo "== ONE interleaved n=70 round-robin matrix (RUNS=10 REPS=7, cooldown 60s) =="
uptime
cd "$P"
BINS=""
for lbl in $BUILT; do BINS="$BINS $EV/bins/bench.$lbl"; done
BINS=$(echo "$BINS" | sed 's/^ //')
R2_LABELS="$BUILT" R2_BINS="$BINS" MATRIX_RUNS=10 MATRIX_REPS=7 MATRIX_COOLDOWN_SECS=60 \
  sh bench/air/r2gate/r2gate_matrix.sh "$EV/matrix" $CORPUS
echo "matrix rc=$? (3=EGATED hot box, partial kept)"
uptime

echo "== verdicts base-vs-each =="
for lbl in $BUILT; do
  [ "$lbl" = "base" ] && continue
  python3 bench/cmp.py "$EV/matrix/base" "$EV/matrix/$lbl" | tee "$EV/cmp-$lbl.txt"
done

echo "== r2gate end $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
uptime
ls -la "$EV" | head -n 40
ls "$EV/matrix" | head
