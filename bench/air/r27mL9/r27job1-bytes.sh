#!/bin/sh
# r27job1-bytes.sh: R27-mL9 byte-gate round 1 (prescreen, NON-TIMING).
# pins + oracle-gate 24/24 + builds (V0/P2b/P2c/h3fact/E2 x noPGO/PGO)
# + IDENT 12/12 + PGO-IDENT 12/12 + -S census (E1 premise + P2 shape + E2 layout).
# Run on Air via: easy-ssh --remote prescreen submit --mem 4G --cpus 2 "sh port/bench/air/r27mL9/r27job1-bytes.sh"
# Output: port/bench/air/r27mL9/job1/ (pulled to NVME worktree, committed summaries).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
A27=$P/bench/air/r27mL9
A26=$P/bench/air/r26mL9
A23=$P/bench/air/r23mL9
OUT=$A27/job1
VENC=$A23/enc-07700a11.c; VPIN=07700a112e4aa098e55ee903fa71c373
BENC=$A27/enc-p2b.c; BPIN=1f5652433c23870a0d33e761aa0df9f9
CENC=$A27/enc-p2c.c; CPIN=a03df1c77bd373c28382e56df8ca7cf9
HENC=$A26/enc-h3fact.c; HPIN=55028305f9b9ce28c5294dde35bf42a3
EENC=$A27/enc-e2.c; EPIN=197de69ef77d0aa62e8f3e56da2468e9
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$VENC" "$BENC" "$CENC" "$HENC" "$EENC"
md5 "$P/src/lzmesh_dec.c" "$P/bench/droprep1.py"
[ "$(md5 -q "$VENC")" = "$VPIN" ] || { echo VEH-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$BENC")" = "$BPIN" ] || { echo P2B-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$CENC")" = "$CPIN" ] || { echo P2C-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$HENC")" = "$HPIN" ] || { echo H3F-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$EENC")" = "$EPIN" ] || { echo E2-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "0dae2df417bd56d06dc30f94fa0ebd31" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
rm -rf "$OUT"
mkdir -p "$OUT"
CFILES="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata) || { echo NO-PROFDATA-HALT; exit 1; }
echo "== oracle-gate 24/24 =="
cc -O2 -o "$OUT/oracle_probe" "$P/tests/battery/oracle_probe.c" 2>"$OUT/build-oracle.log" || { echo ORACLE-BUILD-FAIL; exit 1; }
python3 "$P/bench/air/oraclegate.py" "$OUT/oracle_probe" "$P/bench/corpus" "$OUT/oracle-gate.bin" 2>&1 | tee "$OUT/oracle-gate.txt"
if cmp -s "$OUT/oracle-gate.bin" "$P/bench/air/oracle-air.pinned"; then echo "ORACLE-GATE 24/24 GREEN"; else echo ORACLE-GATE-FAIL-HALT; exit 1; fi
build_one() {
  # $1 = tag, $2 = enc.c path
  TAG=$1; ENC=$2
  mkdir -p "$OUT/$TAG"
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/$TAG/enc-gen.o" "$ENC" 2>"$OUT/$TAG/build-gen.log" || { echo "$TAG-GENV-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/$TAG/dec-gen.o" "$P/src/lzmesh_dec.c" 2>>"$OUT/$TAG/build-gen.log" || { echo "$TAG-GENV-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/$TAG/bench-gen" "$OUT/$TAG/dec-gen.o" "$OUT/$TAG/enc-gen.o" "$P/bench/bench.c" 2>>"$OUT/$TAG/build-gen.log" || { echo "$TAG-GENV-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$OUT/$TAG/full-%p.profraw" "$OUT/$TAG/bench-gen" -n 3 $CFILES > /dev/null || { echo "$TAG-TRAIN-FULL-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$OUT/$TAG/l0-%p.profraw" "$OUT/$TAG/bench-gen" -n 200 -l 0 $CFILES > /dev/null || { echo "$TAG-TRAIN-L0-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$OUT/$TAG/pgo.profdata" "$OUT/$TAG"/*.profraw || { echo "$TAG-MERGE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$TAG/pgo.profdata" -c -o "$OUT/$TAG/enc-use.o" "$ENC" 2>"$OUT/$TAG/build-use.log" || { echo "$TAG-USE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$TAG/pgo.profdata" -c -o "$OUT/$TAG/dec-use.o" "$P/src/lzmesh_dec.c" 2>>"$OUT/$TAG/build-use.log" || { echo "$TAG-USE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$TAG/pgo.profdata" -c -o "$OUT/$TAG/cli-use.o" "$P/src/port_cli.c" 2>>"$OUT/$TAG/build-use.log" || { echo "$TAG-USE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/$TAG/pgo.profdata" -o "$OUT/$TAG/cli-pgo" "$OUT/$TAG/dec-use.o" "$OUT/$TAG/enc-use.o" "$OUT/$TAG/cli-use.o" 2>>"$OUT/$TAG/build-use.log" || { echo "$TAG-USE-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/$TAG/enc-nopgo.o" "$ENC" 2>"$OUT/$TAG/build-nopgo.log" || { echo "$TAG-NOPGO-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/$TAG/dec-nopgo.o" "$P/src/lzmesh_dec.c" 2>>"$OUT/$TAG/build-nopgo.log" || { echo "$TAG-NOPGO-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/$TAG/cli-nopgo.o" "$P/src/port_cli.c" 2>>"$OUT/$TAG/build-nopgo.log" || { echo "$TAG-NOPGO-FAIL"; exit 1; }
  cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/$TAG/cli" "$OUT/$TAG/dec-nopgo.o" "$OUT/$TAG/enc-nopgo.o" "$OUT/$TAG/cli-nopgo.o" 2>>"$OUT/$TAG/build-nopgo.log" || { echo "$TAG-NOPGO-FAIL"; exit 1; }
  echo "$TAG use-warnings=$(grep -ci warning "$OUT/$TAG/build-use.log" || true) nopgo-warnings=$(grep -ci warning "$OUT/$TAG/build-nopgo.log" || true) profdata=$(md5 -q "$OUT/$TAG/pgo.profdata")"
}
echo "== builds V0/P2b/P2c/h3fact/e2 =="
build_one veh "$VENC"
build_one p2b "$BENC"
build_one p2c "$CENC"
build_one h3f "$HENC"
build_one e2v "$EENC"
echo BUILDS-OK
ident12() {
  # $1 = label, $2 = cli-a, $3 = cli-b, $4 = outfile
  : > "$4"
  for corp in text-256k mixed-128k zeros-64k; do
    case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
    for lv in e00 e01 e05 e09; do
      "$2" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
      "$3" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
      if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$4"; else echo "$corp $lv DIV" >> "$4"; fi
    done
  done
  echo "$1: $(grep -c IDENT "$4")/12"
  [ "$(grep -c IDENT "$4")" = "12" ] || { echo "$1-FAIL-HALT"; exit 1; }
}
echo "== IDENT 12/12 noPGO (veh vs each) =="
ident12 IDENT-p2b "$OUT/veh/cli" "$OUT/p2b/cli" "$OUT/ident-p2b.txt"
ident12 IDENT-p2c "$OUT/veh/cli" "$OUT/p2c/cli" "$OUT/ident-p2c.txt"
ident12 IDENT-h3f "$OUT/veh/cli" "$OUT/h3f/cli" "$OUT/ident-h3f.txt"
ident12 IDENT-e2v "$OUT/veh/cli" "$OUT/e2v/cli" "$OUT/ident-e2v.txt"
echo "== PGO-IDENT 12/12 (veh-PGO vs each-PGO) =="
ident12 PGOIDENT-p2b "$OUT/veh/cli-pgo" "$OUT/p2b/cli-pgo" "$OUT/pgoident-p2b.txt"
ident12 PGOIDENT-p2c "$OUT/veh/cli-pgo" "$OUT/p2c/cli-pgo" "$OUT/pgoident-p2c.txt"
ident12 PGOIDENT-h3f "$OUT/veh/cli-pgo" "$OUT/h3f/cli-pgo" "$OUT/pgoident-h3f.txt"
ident12 PGOIDENT-e2v "$OUT/veh/cli-pgo" "$OUT/e2v/cli-pgo" "$OUT/pgoident-e2v.txt"
rm -f "$OUT/n.bin" "$OUT/g.bin"
echo IDENTS-OK
echo "== -S census (E1 premise + P2 shape + h3fact delta) =="
cc -O2 -std=c11 -I"$P/include" -S -o "$OUT/tip.s" "$P/src/lzmesh_enc.c" 2>"$OUT/build-S.log" || { echo S-TIP-FAIL; exit 1; }
cc -O2 -std=c11 -I"$P/include" -S -o "$OUT/p2b.s" "$BENC" 2>>"$OUT/build-S.log" || { echo S-P2B-FAIL; exit 1; }
cc -O2 -std=c11 -I"$P/include" -S -o "$OUT/p2c.s" "$CENC" 2>>"$OUT/build-S.log" || { echo S-P2C-FAIL; exit 1; }
cc -O2 -std=c11 -I"$P/include" -S -o "$OUT/h3f.s" "$HENC" 2>>"$OUT/build-S.log" || { echo S-H3F-FAIL; exit 1; }
cc -O2 -std=c11 -I"$P/include" -S -o "$OUT/veh.s" "$VENC" 2>>"$OUT/build-S.log" || { echo S-VEH-FAIL; exit 1; }
cc -O2 -std=c11 -I"$P/include" -S -o "$OUT/e2v.s" "$EENC" 2>>"$OUT/build-S.log" || { echo S-E2-FAIL; exit 1; }
{
  echo "--- E1 premise: mf_masks refs in tip.s ---"
  grep -c 'mf_masks' "$OUT/tip.s" || true
  echo "--- E1 premise: adrp pairs to mask tables ---"
  grep -c 'adrp.*l___const.*mf_masks\|mf_masks.*adrp' "$OUT/tip.s" || true
  echo "--- logical-imm ANDs with mask consts (65535/4294967295) in tip.s ---"
  grep -c 'and.*#65535\|and.*#4294967295' "$OUT/tip.s" || true
  echo "--- r2_emit_write line spans (veh/p2b/p2c) ---"
  for t in veh p2b p2c; do
    a=$(grep -n "_lzmesh_r2_emit_write:" "$OUT/$t.s" | head -n 1 | cut -d: -f1)
    echo "$t r2_start=$a"
  done
  echo "--- h3_split line spans (tip/h3f) ---"
  for t in tip h3f; do
    a=$(grep -n "_lzmesh_h3_split:\|_u37_h3_split:" "$OUT/$t.s" | head -n 1 | cut -d: -f1)
    echo "$t h3split_start=$a"
  done
  echo "--- file line totals ---"
  wc -l "$OUT/tip.s" "$OUT/veh.s" "$OUT/p2b.s" "$OUT/p2c.s" "$OUT/h3f.s" "$OUT/e2v.s"
} 2>&1 | tee "$OUT/Scensus.txt"
echo "== done =="
uptime
