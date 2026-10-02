#!/bin/bash
# air_matrix.sh: Round 1 phase B gate job — full in-process gap-matrix on Air.
# 24 cells (3 corpus x 0/1/5/9 x enc/dec), port bench vs Apple abench BOTH
# in-process, interleaved per cell, n=70 (RUNS=10 x REPS=7), gated + pinned.
# Runs in ~/Projects/LZMESH-jobs on MacBookAir via easy-ssh submit.
# Phase B runs this file UNTOUCHED after `easy-ssh push` from the
# lanes/matrix-harness tree (pins + script all ship with the push; no scp
# staging — tmp/ is push-excluded, so everything gate-needed lives here).
# Evidence: matrix-ev/ (job log, build logs, matrix TSVs, bytes, cmp txt).
#
# Phase-B commands (from lanes/matrix-harness worktree root):
#   export PATH="$HOME/.easy-ssh:$PATH"
#   easy-ssh push
#   easy-ssh submit "sh port/bench/air/air_matrix.sh"
#   easy-ssh monitor   # stream; Ctrl+C detaches, job keeps running
#   easy-ssh pull matrix-ev   # when finished; then clean up remote dirs
#
# Serial slot: P30 owns the Air until its fold lands — do NOT submit while a
# P30/fold job is live (easy-ssh status). This script also refuses when
# another air_matrix/fold job is already running on the box.
set -u
J=$(pwd)
ST=$J/port/bench/air
P=$J/port
EV=$J/matrix-ev
LOG=$EV/job-air-matrix.log
mkdir -p "$EV"
exec >"$LOG" 2>&1
echo "== air_matrix start $(date -u +%Y-%m-%dT%H:%M:%SZ) =="

echo "== serial-slot guard =="
if pgrep -f "air_matrix|air_exactcap|fold29|fold23|p30" >/dev/null 2>&1; then
  # pgrep sees our own sh wrapper; require a *second* match to refuse.
  n=$(pgrep -f "air_matrix|air_exactcap|fold29|fold23" 2>/dev/null | wc -l | tr -d ' ')
  echo "slot-probe matches=$n (1=self)"
  [ "$n" -gt 2 ] && { echo SLOT-BUSY-HALT; exit 1; }
fi
echo SLOT-OK

echo "== box facts =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "EXPECT_SHA=2c00ece7cee0a14b8371a0792f9b439958a4e791 (lanes/lane-4 R13-shipped base; r14-matrix)"
echo "EXPECT_ENC_MD5=992a88042792bd560d0aac52a537496e"
echo "EXPECT_DEC_MD5=db4ae6b77fd721301a7c46c4cb677a2c"
md5 "$P/src/lzmesh_enc.c" "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "992a88042792bd560d0aac52a537496e" ] || { echo TREE-PIN-FAIL-HALT-enc; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo TREE-PIN-FAIL-HALT-dec; exit 1; }
echo TREE-PIN-OK

echo "== corpus pins =="
(cd "$P" && python3 bench/mkcorpus.py --check bench/corpus) || { echo CORPUS-PIN-FAIL-HALT; exit 1; }
echo CORPUS-PIN-OK

echo "== build lib + bench + abench + encdump (warnings fatal) =="
(cd "$P" && make matrix-bins > "$EV/build-matrix.log" 2>&1; echo "build-matrix rc=$?")
w=$(grep -ci "warning" "$EV/build-matrix.log" || true)
echo "matrix-warnings=$w"
[ "$w" -eq 0 ] || { echo WARNINGS-FAIL-HALT; exit 1; }
echo BUILD-MATRIX-OK
echo "== build oracle_probe (oracle-gate only) =="
cc -O2 -o "$EV/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$EV/build-oracle.log" || { echo BUILD-ORACLE-FAIL-HALT; exit 1; }
echo BUILD-ORACLE-OK

echo "== oracle-gate 24/24 (pinned macOS-27 Air oracle) =="
python3 "$ST/oraclegate.py" "$EV/oracle_probe" "$P/bench/corpus" "$EV/oracle-now.bin"
if cmp -s "$EV/oracle-now.bin" "$ST/oracle-air.pinned"; then echo ORACLE-GATE-24/24-PASS; else echo ORACLE-GATE-FAIL-HALT; exit 1; fi

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

echo "== gap matrix n=70 interleaved (RUNS=10 REPS=7, cooldown 60s) =="
uptime
cd "$P"
MATRIX_RUNS=10 MATRIX_REPS=7 MATRIX_COOLDOWN_SECS=60 \
  sh bench/matrix_gated.sh "$EV/matrix" ./bench/bench ./bench/abench ./bench/encdump $CORPUS
echo "matrix rc=$? (3=EGATED hot box, partial kept)"
uptime

echo "== verdicts =="
python3 bench/cmp.py "$EV/matrix/port" "$EV/matrix/apple" | tee "$EV/cmp-matrix.txt"
echo "== bytes summary =="
grep -h -c IDENT "$EV/matrix/bytes"/run*.tsv | paste -sd+ | xargs -I{} echo "IDENT-rows-total={} (want 120 = 12 cells x 10 runs)"
grep -h 'DIV@\|X-FAIL\|ENC-FAIL' "$EV/matrix/bytes"/run*.tsv | head -20 || echo BYTES-CLEAN
grep -c 'PIN-MIXED\|MISMATCH' "$EV/matrix/MATRIX.log" | xargs -I{} echo "void-flags={} (want 0)"

echo "== air_matrix end $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
uptime
ls -la "$EV" | head -n 30
ls "$EV/matrix" | head
