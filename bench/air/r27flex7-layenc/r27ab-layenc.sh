#!/bin/sh
# r27ab-layenc.sh: STAGED union layout-perturb gate (post-d77, dispatcher call only).
# flex7 stages, never submits. Enc-side R24 ALIGN mirror of r27ab-laypad2.sh.
# PASS: bytes+static (PGO-IDENT 24/24 x2 + h3 bounds) + timing 0-slower-only
# (E2a/E2b, NO prize bar). See PRE-REG-R27-FLEX7-LAYENC.md (binding).
# Env: BENCH_RUNS (5), BENCH_REPS (7), BASELINE (union|tip, default union;
# union pair ALWAYS runs (binding, Q2b); tip ADDS corroboration pair,
# non-binding), T0MODE (single|own, default single with PGO-IDENT tripwire +
# own-T0 fallback, Q2a).
# Q2a tripwire: single-T0 attempt (union profile -> lay); cross-pair IDENT =>
# reuse VALID; any DIV => discard cross arm, train own-T0, rebuild, continue.
# Both profdata md5s recorded either way.
# Output: port/bench/air/r27flex7-layenc/ab-layenc/.
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
OUT=$P/bench/air/r27flex7-layenc/ab-layenc
RUNS=${BENCH_RUNS:-5}
REPS=${BENCH_REPS:-7}
BASELINE=${BASELINE:-union}
T0MODE=${T0MODE:-single}
ALIGN="-falign-functions=64 -falign-loops=64"
UFLAGS="-DR26H3_SLIM -DR26H3_NOYF -DR26H3_NOARM2L9"
CFLAGS="-O2 -std=c11 -Wall -Wextra -I$P/include"
PRISTINEQ=3fec8aa3274f901b1372be85936a77a2
UNIONQ=3860c43cdda4a71aa705583effbbdc10
rm -rf "$OUT"
mkdir -p "$OUT"
echo "BASELINE=$BASELINE T0MODE=$T0MODE"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
pgrep -fl bztransmit 2>/dev/null || echo "bb-pre: none"
echo "== pins =="
md5 "$P/src/lzmesh_enc.c" "$HERE/enc-union.c"
[ "$(md5 -q "$P/src/lzmesh_enc.c")" = "$PRISTINEQ" ] || { echo PRISTINE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$HERE/enc-union.c")" = "$UNIONQ" ] || { echo UNION-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
echo "== build std (tip + union + lay; dec .o shared) =="
# shellcheck disable=SC2086
cc $CFLAGS -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2> "$OUT/build-dec.log" || { echo DEC-BUILD-FAIL; exit 1; }
cc $CFLAGS -c -o "$OUT/enc-tip.o" "$P/src/lzmesh_enc.c" 2> "$OUT/build-tip.log" || { echo TIP-BUILD-FAIL; exit 1; }
cc $CFLAGS $UFLAGS -c -o "$OUT/enc-union.o" "$HERE/enc-union.c" 2> "$OUT/build-union.log" || { echo UNION-BUILD-FAIL; exit 1; }
cc $CFLAGS $UFLAGS $ALIGN -c -o "$OUT/enc-lay.o" "$HERE/enc-union.c" 2> "$OUT/build-lay.log" || { echo LAY-BUILD-FAIL; exit 1; }
cc $CFLAGS -c -o "$OUT/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-dec.log" || { echo CLI-BUILD-FAIL; exit 1; }
for t in tip union lay; do
  cc $CFLAGS -o "$OUT/cli-$t" "$OUT/enc-$t.o" "$OUT/dec.o" "$OUT/cli.o" 2>> "$OUT/build-$t.log" || { echo "$t-LINK-FAIL"; exit 1; }
  cc $CFLAGS -o "$P/bench/bench27flex7-$t" "$P/bench/bench.c" "$OUT/enc-$t.o" "$OUT/dec.o" 2>> "$OUT/build-$t.log" || { echo "$t-BENCH-LINK-FAIL"; exit 1; }
done
for f in "$OUT"/build-*.log; do echo "$(basename "$f") warnings=$(grep -ci warning "$f" || true)"; done | tee "$OUT/warnings-std.txt"
echo STD-BUILD-OK
build_pgo() {
  # $1 = tag, $2 = enc file, $3 = dflags, $4 = align flags, $5 = prof ("OWN"/path)
  PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata)
  [ -n "$PROFDATA" ] || { echo NO-PROFDATA-HALT; exit 1; }
  mkdir -p "$OUT/pgo-gen-$1" "$OUT/prof-$1"
  # shellcheck disable=SC2086
  cc $CFLAGS $3 $4 -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/enc.o" "$2" 2> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS -fprofile-instr-generate -c -o "$OUT/pgo-gen-$1/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $3 $4 -fprofile-instr-generate -o "$OUT/bench-gen-$1" "$OUT/pgo-gen-$1/enc.o" "$OUT/pgo-gen-$1/dec.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  CORPUS="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
  LLVM_PROFILE_FILE="$OUT/prof-$1/full-%p.profraw" "$OUT/bench-gen-$1" -n 3 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  LLVM_PROFILE_FILE="$OUT/prof-$1/l0-%p.profraw" "$OUT/bench-gen-$1" -n 200 -l 0 $CORPUS > /dev/null || { echo "$1-TRAIN-FAIL"; exit 1; }
  "$PROFDATA" merge -o "$OUT/$1.profdata" "$OUT"/prof-$1/*.profraw || { echo "$1-MERGE-FAIL"; exit 1; }
  md5 -q "$OUT/$1.profdata" | tee "$OUT/$1.profdata.md5"
  if [ "$5" = "OWN" ]; then USE="$OUT/$1.profdata"; else USE="$5"; fi
  mkdir -p "$OUT/pgo-use-$1"
  # shellcheck disable=SC2086
  cc $CFLAGS $3 $4 "-fprofile-instr-use=$USE" -c -o "$OUT/pgo-use-$1/enc.o" "$2" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS "-fprofile-instr-use=$USE" -c -o "$OUT/pgo-use-$1/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  cc $CFLAGS "-fprofile-instr-use=$USE" -c -o "$OUT/pgo-use-$1/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-$1.log" || { echo "$1-BUILD-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $3 $4 "-fprofile-instr-use=$USE" -o "$OUT/cli-$1" "$OUT/pgo-use-$1/enc.o" "$OUT/pgo-use-$1/dec.o" "$OUT/pgo-use-$1/cli.o" 2>> "$OUT/build-$1.log" || { echo "$1-LINK-FAIL"; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $3 $4 "-fprofile-instr-use=$USE" -o "$P/bench/bench27flex7-$1" "$OUT/pgo-use-$1/enc.o" "$OUT/pgo-use-$1/dec.o" "$P/bench/bench.c" 2>> "$OUT/build-$1.log" || { echo "$1-BENCH-LINK-FAIL"; exit 1; }
  echo "$1-warnings=$(grep -ci warning "$OUT/build-$1.log" || true)"
  md5 -q "$OUT/cli-$1" | tee "$OUT/bin-$1.md5"
  echo "$1-BUILD-OK"
}
echo "== build PGO (T0MODE=$T0MODE BASELINE=$BASELINE) =="
build_pgo unionpgo "$HERE/enc-union.c" "$UFLAGS" "" "OWN"
if [ "$T0MODE" = "single" ]; then
  echo "== single-T0 attempt (union profile -> lay) + tripwire =="
  mkdir -p "$OUT/pgo-use-laypgo"
  # shellcheck disable=SC2086
  cc $CFLAGS $UFLAGS $ALIGN "-fprofile-instr-use=$OUT/unionpgo.profdata" -c -o "$OUT/pgo-use-laypgo/enc.o" "$HERE/enc-union.c" 2> "$OUT/build-laypgo.log" || { echo laypgo-BUILD-FAIL; exit 1; }
  cc $CFLAGS "-fprofile-instr-use=$OUT/unionpgo.profdata" -c -o "$OUT/pgo-use-laypgo/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-laypgo.log" || { echo laypgo-BUILD-FAIL; exit 1; }
  cc $CFLAGS "-fprofile-instr-use=$OUT/unionpgo.profdata" -c -o "$OUT/pgo-use-laypgo/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-laypgo.log" || { echo laypgo-BUILD-FAIL; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $UFLAGS $ALIGN "-fprofile-instr-use=$OUT/unionpgo.profdata" -o "$OUT/cli-laypgo" "$OUT/pgo-use-laypgo/enc.o" "$OUT/pgo-use-laypgo/dec.o" "$OUT/pgo-use-laypgo/cli.o" 2>> "$OUT/build-laypgo.log" || { echo laypgo-LINK-FAIL; exit 1; }
  # shellcheck disable=SC2086
  cc $CFLAGS $UFLAGS $ALIGN "-fprofile-instr-use=$OUT/unionpgo.profdata" -o "$P/bench/bench27flex7-laypgo" "$OUT/pgo-use-laypgo/enc.o" "$OUT/pgo-use-laypgo/dec.o" "$P/bench/bench.c" 2>> "$OUT/build-laypgo.log" || { echo laypgo-BENCH-LINK-FAIL; exit 1; }
  echo "laypgo-cross-warnings=$(grep -ci warning "$OUT/build-laypgo.log" || true)"
  md5 -q "$OUT/cli-laypgo" | tee "$OUT/bin-laypgo-cross.md5"
  : > "$OUT/tripwire.txt"
  for corp in text-256k mixed-128k zeros-64k; do
    case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
    for lv in e00 e01 e05 e09; do
      "$OUT/cli-lay" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
      "$OUT/cli-laypgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
      if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/tripwire.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/tripwire.txt"; fi
      "$OUT/cli-lay" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
      "$OUT/cli-laypgo" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
      if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/tripwire.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/tripwire.txt"; fi
    done
  done
  echo "TRIPWIRE-ENC: $(grep -c enc-IDENT "$OUT/tripwire.txt")/12"
  echo "TRIPWIRE-DEC: $(grep -c dec-IDENT "$OUT/tripwire.txt")/12"
  if [ "$(grep -c enc-IDENT "$OUT/tripwire.txt")" = "12" ] && [ "$(grep -c dec-IDENT "$OUT/tripwire.txt")" = "12" ]; then
    echo TRIPWIRE-PASS-single-T0-VALID
    cp "$OUT/unionpgo.profdata" "$OUT/laypgo.profdata"
    md5 -q "$OUT/laypgo.profdata" | tee "$OUT/laypgo.profdata.md5"
  else
    echo TRIPWIRE-DIV-falling-back-to-own-T0
    build_pgo laypgo-own "$HERE/enc-union.c" "$UFLAGS" "$ALIGN" "OWN"
    cp "$OUT/cli-laypgo-own" "$OUT/cli-laypgo"
    cp "$P/bench/bench27flex7-laypgo-own" "$P/bench/bench27flex7-laypgo"
    cp "$OUT/laypgo-own.profdata" "$OUT/laypgo.profdata"
    md5 -q "$OUT/laypgo.profdata" | tee "$OUT/laypgo.profdata.md5"
  fi
else
  build_pgo laypgo "$HERE/enc-union.c" "$UFLAGS" "$ALIGN" "OWN"
fi
if [ "$BASELINE" = "tip" ]; then build_pgo tippgo "$P/src/lzmesh_enc.c" "" "" "OWN"; fi
echo "== static h3 bounds (h3count.py) =="
cc $CFLAGS $UFLAGS "-fprofile-instr-use=$OUT/unionpgo.profdata" -S -o "$OUT/enc-unionpgo.S" "$HERE/enc-union.c" 2>/dev/null || { echo S-FAIL; exit 1; }
cc $CFLAGS $UFLAGS $ALIGN "-fprofile-instr-use=$OUT/laypgo.profdata" -S -o "$OUT/enc-laypgo.S" "$HERE/enc-union.c" 2>/dev/null || { echo S-FAIL; exit 1; }
python3 "$HERE/h3count.py" "$OUT/enc-laypgo.S" lzmesh_h3_split | tee "$OUT/h3-laypgo.txt"
python3 - "$OUT/h3-laypgo.txt" <<'EOF' || { echo STATIC-BOUNDS-FAIL-HALT; exit 1; }
import re, sys
m = dict(re.findall(r"(ld|st|cbr|mov|bl)=(\d+)", open(sys.argv[1]).read()))
ok = int(m["ld"]) < 193 and int(m["cbr"]) <= 117 and int(m["st"]) <= 180 and int(m["mov"]) <= 520 and int(m["bl"]) == 33
print("STATIC-BOUNDS:", "MET" if ok else "FAIL", m)
sys.exit(0 if ok else 1)
EOF
pgoident() {
  a=$1; b=$2; tag=$3
  : > "$OUT/pgoident-$tag.txt"
  for corp in text-256k mixed-128k zeros-64k; do
    case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
    for lv in e00 e01 e05 e09; do
      "$OUT/cli-$a" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
      "$OUT/cli-$b" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
      if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv enc-IDENT" >> "$OUT/pgoident-$tag.txt"; else echo "$corp $lv enc-DIV" >> "$OUT/pgoident-$tag.txt"; fi
      "$OUT/cli-$a" dec x < "$OUT/n.bin" > "$OUT/nd.bin" 2>/dev/null
      "$OUT/cli-$b" dec x < "$OUT/g.bin" > "$OUT/gd.bin" 2>/dev/null
      if cmp -s "$OUT/nd.bin" "$OUT/gd.bin"; then echo "$corp $lv dec-IDENT" >> "$OUT/pgoident-$tag.txt"; else echo "$corp $lv dec-DIV" >> "$OUT/pgoident-$tag.txt"; fi
    done
  done
  echo "PGO-IDENT-$tag-ENC: $(grep -c enc-IDENT "$OUT/pgoident-$tag.txt")/12"
  echo "PGO-IDENT-$tag-DEC: $(grep -c dec-IDENT "$OUT/pgoident-$tag.txt")/12"
  [ "$(grep -c enc-IDENT "$OUT/pgoident-$tag.txt")" = "12" ] || { echo "PGO-IDENT-$tag-ENC-FAIL-HALT"; exit 1; }
  [ "$(grep -c dec-IDENT "$OUT/pgoident-$tag.txt")" = "12" ] || { echo "PGO-IDENT-$tag-FAIL-HALT"; exit 1; }
}
echo "== PGO-IDENT x2 (binding) =="
pgoident union unionpgo union
pgoident lay laypgo lay
if [ "$BASELINE" = "tip" ]; then pgoident tip tippgo tip; fi
echo "== quiet-wait (load1 < 4, 3 tries x 60s) + cool-down 60s =="
tries=0
while [ $tries -lt 3 ]; do
  l1=$(uptime | sed -e "s/.*load averages*:[[:space:]]*//" -e "s/,//g" | awk "{print \$1}")
  echo "try $tries load1=$l1 $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  if awk -v l="$l1" "BEGIN{exit !(l < 4.0)}"; then break; fi
  tries=$((tries+1)); sleep 60
done
uptime; sleep 60; uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-ab: none"
# Binding pair (Q2b): union-std vs lay + unionpgo vs laypgo, ALWAYS runs.
# tip pair: corroboration ONLY when BASELINE=tip (non-binding; lay OV-vs-union
# but slower-vs-tip reads as no-prize-confirmed, NOT layout harm).
AB_FAIL=0
run_ab() {
  echo "== A/B $1 n=$RUNS x $REPS =="
  (cd "$P" && BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh bench/run_gated.sh --ab "$OUT/ab-$1" "$2" "$3" bench/corpus/text-256k.bin bench/corpus/mixed-128k.bin bench/corpus/zeros-64k.bin 2>&1; echo "ab-$1-rc=$?")
  if [ ! -f "$OUT/ab-$1/bbase/run1.tsv" ]; then echo "AB-$1-NO-DATA-HALT"; AB_FAIL=1; fi
  echo "== cmp $1 =="
  (cd "$P" && python3 bench/cmp.py "$OUT/ab-$1/bbase" "$OUT/ab-$1/bnew" > "$OUT/cmp-$1.txt" 2>&1; echo "cmp-$1-rc=$?"; tail -2 "$OUT/cmp-$1.txt")
  echo "== drop-1 $1 =="
  (cd "$P" && python3 bench/droprep1.py "$OUT/ab-$1/bbase" "$OUT/ab-$1-d1/bbase" && python3 bench/droprep1.py "$OUT/ab-$1/bnew" "$OUT/ab-$1-d1/bnew" && python3 bench/cmp.py "$OUT/ab-$1-d1/bbase" "$OUT/ab-$1-d1/bnew" > "$OUT/cmp-$1-drop1.txt" 2>&1; echo "cmp-$1-d1-rc=$?"; tail -2 "$OUT/cmp-$1-drop1.txt")
  echo "== cool-down 60s =="
  uptime; sleep 60; uptime
}
run_ab layenc-std ./bench/bench27flex7-union ./bench/bench27flex7-lay
run_ab layenc-pgo ./bench/bench27flex7-unionpgo ./bench/bench27flex7-laypgo
if [ "$BASELINE" = "tip" ]; then
  echo "== corroboration pair (NON-BINDING, Q2b) =="
  run_ab layenc-tip-std ./bench/bench27flex7-tip ./bench/bench27flex7-lay
  run_ab layenc-tip-pgo ./bench/bench27flex7-tippgo ./bench/bench27flex7-laypgo
fi
uptime
pgrep -fl bztransmit 2>/dev/null || echo "bb-post: none"
if [ "$AB_FAIL" != "0" ]; then echo R27FLEX7-LAYENC-FAIL-HALT; exit 1; fi
echo R27FLEX7-LAYENC-DONE
