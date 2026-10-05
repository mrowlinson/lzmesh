#!/bin/sh
# verify-job.sh: r28land merged-tree VERIFY (ONE capped submit, non-timing, [batch] remote).
# Runs AFTER tiers pull (same LAND-WINDOW, sequential). Scope per ANSWER-r28-land-2 Q2c:
# FULL/HOLD/FRESH/d77 gate arm on MERGED tree + build sanity (pins + CLI + smoke-default).
# Units TRUE-scalar x2 + smoke-scalar + forensics verdicts come FROM tiers output (no re-run).
# Bar: gate 0 NEW (HOLD 3008 + FRESH 3008 + FULL 12784 + d77 57904) + smoke 1232/0.
# Method: tiers batch-job.sh gate sections verbatim (same shards).
set -u
J=$(pwd); P=$J/port; EV=$J/verify-ev
rm -rf "$EV"; mkdir -p "$EV"
exec >"$EV/job-verify.log" 2>&1
echo "== verify start $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
echo "== pins (merged tree must equal ship tip) =="
md5 "$P/src/lzmesh_enc.c" "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo ENC-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "5a147a81d4df9269f52e652981dd5776" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build oracle_probe + port_cli default =="
cc -O2 -o "$EV/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$EV/build-oracle.log" || { echo ORACLE-BUILD-FAIL-HALT; exit 1; }
(cd "$P" && make port_cli >"$EV/build-cli.log" 2>&1) || { echo CLI-BUILD-FAIL-HALT; exit 1; }
cp "$P/port_cli" "$EV/port_cli_default"
echo BUILD-OK
BAT="$P/tests/battery/battery.py"; OR="$EV/oracle_probe"; CLI="$EV/port_cli_default"
echo "== smoke default (build sanity) =="
python3 "$BAT" --oracle "$OR" --port "$CLI" --out "$EV/smoke" --tier smoke || { echo SMOKE-FAIL-HALT; exit 1; }
python3 -c "import json,sys;d=json.load(open('$EV/smoke/summary.json'));print('smoke',d['cells'],d['fail_cells'],d['verdict']);sys.exit(0 if (d['cells']==1232 and d['fail_cells']==0) else 1)" || { echo SMOKE-NE-1232-0-HALT; exit 1; }
echo SMOKE-DONE-1232-0
echo "== gate arm (tip-vs-tip, merged tree) =="
python3 "$BAT" --oracle "$CLI" --port "$CLI" --out "$EV/g-hold" --tier full --selectors e00,e01,e05,e09 --seeds 4 --seed-offset 1000 || { echo GATE-HOLD-RUN-FAIL; exit 1; }
python3 "$BAT" --oracle "$CLI" --port "$CLI" --out "$EV/g-fresh" --tier full --selectors e00,e01,e05,e09 --seeds 4 --seed-offset 2000 || { echo GATE-FRESH-RUN-FAIL; exit 1; }
i=0; for spec in "0 5" "5 4" "9 4" "13 4"; do set -- $spec; python3 "$BAT" --oracle "$CLI" --port "$CLI" --out "$EV/g-full-$i" --tier full --selectors e00,e01,e05,e09 --seeds $2 --seed-offset $1 || { echo GATE-FULL-$i-RUN-FAIL; exit 1; }; i=$((i+1)); done
i=0; for spec in "0 13" "13 13" "26 13" "39 13" "52 13" "65 12"; do set -- $spec; python3 "$BAT" --oracle "$CLI" --port "$CLI" --out "$EV/g-d77-$i" --tier full --selectors e00,e01,e05,e09 --seeds $2 --seed-offset $1 || { echo GATE-D77-$i-RUN-FAIL; exit 1; }; i=$((i+1)); done
for g in g-hold g-fresh g-full-0 g-full-1 g-full-2 g-full-3 g-d77-0 g-d77-1 g-d77-2 g-d77-3 g-d77-4 g-d77-5; do
  python3 -c "import json,sys;d=json.load(open('$EV/$g/summary.json'));print('$g',d['cells'],d['fail_cells'],d['verdict']);sys.exit(0 if d['fail_cells']==0 else 1)" || { echo "$g-GATE-NEW-HALT"; exit 1; }
done
echo GATE-ARM-0-NEW
echo "== verify end $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
uptime
ls "$EV"
