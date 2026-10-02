#!/bin/bash
# air_exactcap.sh: R12 gate job — full in-process gap-matrix on the EXACT-CAP
# decode shape (decode dest sized n, not n+64), port bench-exact vs Apple
# abench-exact, interleaved per cell, n=70 (RUNS=10 x REPS=7), gated + pinned.
# Closes the R11 lastblock gap-pp N/A (no Apple exact-cap baseline existed).
# Runs in ~/Projects/LZMESH-jobs on MacBookAir via easy-ssh submit.
# Runs this file UNTOUCHED after `easy-ssh push` from the lanes/r12-matrix
# tree (pins + script all ship with the push; no scp staging — tmp/ is
# push-excluded, so everything gate-needed lives here).
# Evidence: exactcap-ev/ (job log, build logs, exactcap TSVs, bytes, cmp txt).
#
# Gate-phase commands (from lanes/r12-matrix worktree root):
#   export PATH="$HOME/.easy-ssh:$PATH"
#   easy-ssh push
#   easy-ssh submit "sh port/bench/air/r12matrix/air_exactcap.sh"
#   easy-ssh monitor   # stream; Ctrl+C detaches, job keeps running
#   easy-ssh pull exactcap-ev   # when finished; then clean up remote dirs
#
# Serial slot: r12-matrix owns the Air until its fold lands — do NOT submit
# while another gate job is live (easy-ssh status). This script also refuses
# when another air_matrix/air_exactcap job is already running on the box.
set -u
J=$(pwd)
ST=$J/port/bench/air
P=$J/port
EV=$J/exactcap-ev
LOG=$EV/job-air-exactcap.log
mkdir -p "$EV"
exec >"$LOG" 2>&1
echo "== air_exactcap start $(date -u +%Y-%m-%dT%H:%M:%SZ) =="

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
echo "EXPECT_SHA=49c308acf7a08601c03a2004651caae96087b06b (lanes/lane-4 R11-shipped base; r12-matrix)"
echo "EXPECT_ENC_MD5=41ec58b7aa0621fea606dc6f5c258831"
echo "EXPECT_DEC_MD5=db4ae6b77fd721301a7c46c4cb677a2c"
echo "EXPECT_BENCHEXACT_MD5=a7935b173fc8b6f4c51a3fb6ef251904"
echo "EXPECT_ABENCHEXACT_MD5=96bf3acfcc880c8de12449e4024c56de"
md5 "$P/src/lzmesh_enc.c" "$P/src/lzmesh_dec.c" "$P/bench/air/r11lastblock/bench-exact.c" "$P/bench/air/r12matrix/abench-exact.c"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "41ec58b7aa0621fea606dc6f5c258831" ] || { echo TREE-PIN-FAIL-HALT-enc; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo TREE-PIN-FAIL-HALT-dec; exit 1; }
[ "$(md5 -q "$P/bench/air/r11lastblock/bench-exact.c")" = "a7935b173fc8b6f4c51a3fb6ef251904" ] || { echo TREE-PIN-FAIL-HALT-benchexact; exit 1; }
[ "$(md5 -q "$P/bench/air/r12matrix/abench-exact.c")" = "96bf3acfcc880c8de12449e4024c56de" ] || { echo TREE-PIN-FAIL-HALT-abenchexact; exit 1; }
echo TREE-PIN-OK

echo "== corpus pins =="
(cd "$P" && python3 bench/mkcorpus.py --check bench/corpus) || { echo CORPUS-PIN-FAIL-HALT; exit 1; }
echo CORPUS-PIN-OK

echo "== build lib + bench-exact + abench-exact + encdump (warnings fatal) =="
(cd "$P" && make liblzmesh.a bench/encdump > "$EV/build-exact.log" 2>&1; echo "build-lib rc=$?")
(cd "$P" && cc -O2 -std=c11 -Wall -Wextra -Iinclude -o bench/bench12-exact bench/air/r11lastblock/bench-exact.c liblzmesh.a >> "$EV/build-exact.log" 2>&1; echo "build-bench-exact rc=$?")
(cd "$P" && cc -O2 -std=c11 -Wall -Wextra -o bench/abench12-exact bench/air/r12matrix/abench-exact.c -ldl >> "$EV/build-exact.log" 2>&1; echo "build-abench-exact rc=$?")
w=$(grep -ci "warning" "$EV/build-exact.log" || true)
echo "exact-warnings=$w"
[ "$w" -eq 0 ] || { echo WARNINGS-FAIL-HALT; exit 1; }
echo BUILD-EXACT-OK
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

echo "== exact-cap gap matrix n=70 interleaved (RUNS=10 REPS=7, cooldown 60s) =="
uptime
cd "$P"
MATRIX_RUNS=10 MATRIX_REPS=7 MATRIX_COOLDOWN_SECS=60 \
  sh bench/matrix_gated.sh "$EV/exactcap" ./bench/bench12-exact ./bench/abench12-exact ./bench/encdump $CORPUS
echo "exactcap rc=$? (3=EGATED hot box, partial kept)"
uptime

echo "== verdicts =="
python3 bench/cmp.py "$EV/exactcap/port" "$EV/exactcap/apple" | tee "$EV/cmp-exactcap.txt"
echo "== bytes summary =="
grep -h -c IDENT "$EV/exactcap/bytes"/run*.tsv | paste -sd+ | xargs -I{} echo "IDENT-rows-total={} (want 120 = 12 cells x 10 runs)"
grep -h 'DIV@\|X-FAIL\|ENC-FAIL' "$EV/exactcap/bytes"/run*.tsv | head -20 || echo BYTES-CLEAN
grep -c 'PIN-MIXED\|MISMATCH' "$EV/exactcap/MATRIX.log" | xargs -I{} echo "void-flags={} (want 0)"

echo "== air_exactcap end $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
uptime
ls -la "$EV" | head -n 30
ls "$EV/exactcap" | head
