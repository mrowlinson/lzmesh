#!/bin/sh
# r22ab-m23stack.sh: FULL-STACK prescreen (memo Q2 burn plan step 1) --
# bench-base (tip 3fec8aa3, no PGO) vs bench-stack
# (M23 be2b624b + T0-recipe PGO trained ON M23 + interpose-T2D ride-along).
# Matrix-scoped cells. Decides >=5-plausible in 1 burn (median + direction +
# 0-slower + L5); dissect (m23solo / noT2) only iff short.
# Run on Air via dispatcher CLAIM-poll -> CONFIRM -> easy-ssh submit ONLY.
# Env knobs: BENCH_RUNS (default 2), BENCH_REPS (default 3) => n=6 prescreen.
# Output: port/bench/air/r22mL9/abstack1/ (build logs, md5s, idents, cmp).
set -u
HERE=$(dirname "$0")
P=$(cd "$HERE/../../.." && pwd)
A=$P/bench/air/r22mL9
OUT=$A/abstack1
RUNS=${BENCH_RUNS:-2}
REPS=${BENCH_REPS:-3}
rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/use" "$OUT/prof"
echo "== box =="
uptime
sysctl -n hw.ncpu 2>/dev/null
sw_vers -productVersion 2>/dev/null
cc --version 2>/dev/null | head -n 1
echo "== pins =="
md5 "$A/enc-base.c" "$A/enc-new.c" "$A/lzmesh_t2d.c"
md5 "$P/src/lzmesh_dec.c"
[ "$(md5 -q "$A/enc-base.c")" = "3fec8aa3274f901b1372be85936a77a2" ] || { echo BASE-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$A/enc-new.c")" = "be2b624b7815ac626915669b86d9489c" ] || { echo NEW-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$A/lzmesh_t2d.c")" = "9ebda3c6f7eab5586d6bb231add72890" ] || { echo T2D-PIN-FAIL-HALT; exit 1; }
[ "$(md5 -q "$P/src/lzmesh_dec.c")" = "db4ae6b77fd721301a7c46c4cb677a2c" ] || { echo DEC-PIN-FAIL-HALT; exit 1; }
echo PINS-OK
C="text-256k.bin mixed-128k.bin zeros-64k.bin"
CFILES="$P/bench/corpus/text-256k.bin $P/bench/corpus/mixed-128k.bin $P/bench/corpus/zeros-64k.bin"
echo "== build bench-base (tip, no PGO) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-base.o" "$A/enc-base.c" 2> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-base.a" "$OUT/dec.o" "$OUT/enc-base.o" || { echo BASE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/bench-base" "$P/bench/bench.c" "$OUT/lib-base.a" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-base.o" "$P/src/port_cli.c" 2>> "$OUT/build-base.log" || { echo BASE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-base" "$OUT/dec.o" "$OUT/enc-base.o" "$OUT/cli-base.o" 2>> "$OUT/build-base.log" || { echo BASE-LINK-FAIL; exit 1; }
echo "base-warnings=$(grep -ci warning "$OUT/build-base.log" || true)"
md5 -q "$OUT/bench-base" | tee "$OUT/bin-base.md5"
echo BASE-BUILD-OK
echo "== build M23-noPGO (attribution baseline + IDENT) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/enc-m23.o" "$A/enc-new.c" 2> "$OUT/build-m23.log" || { echo M23-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-m23.a" "$OUT/dec.o" "$OUT/enc-m23.o" || { echo M23-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -c -o "$OUT/cli-m23.o" "$P/src/port_cli.c" 2>> "$OUT/build-m23.log" || { echo M23-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -o "$OUT/cli-m23" "$OUT/dec.o" "$OUT/enc-m23.o" "$OUT/cli-m23.o" 2>> "$OUT/build-m23.log" || { echo M23-LINK-FAIL; exit 1; }
echo "m23-warnings=$(grep -ci warning "$OUT/build-m23.log" || true)"
echo M23-BUILD-OK
echo "== PGO train ON M23 (T0 recipe: -n 3 all + -n 200 -l 0) =="
PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || command -v llvm-profdata) || { echo NO-PROFDATA-HALT; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/gen/enc.o" "$A/enc-new.c" 2> "$OUT/build-gen.log" || { echo GEN-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -c -o "$OUT/gen/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-gen.log" || { echo GEN-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-generate -o "$OUT/bench-gen" "$OUT/gen/dec.o" "$OUT/gen/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-gen.log" || { echo GEN-LINK-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/full-%p.profraw" "$OUT/bench-gen" -n 3 $CFILES > /dev/null || { echo TRAIN-FULL-FAIL; exit 1; }
LLVM_PROFILE_FILE="$OUT/prof/l0-%p.profraw" "$OUT/bench-gen" -n 200 -l 0 $CFILES > /dev/null || { echo TRAIN-L0-FAIL; exit 1; }
"$PROFDATA" merge -o "$OUT/pgo.profdata" "$OUT"/prof/*.profraw || { echo MERGE-FAIL; exit 1; }
md5 -q "$OUT/pgo.profdata" | tee "$OUT/profdata.md5"
echo TRAIN-OK
echo "== build M23-PGO (profile-use) =="
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -c -o "$OUT/use/enc.o" "$A/enc-new.c" 2> "$OUT/build-use.log" || { echo USE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -c -o "$OUT/use/dec.o" "$P/src/lzmesh_dec.c" 2>> "$OUT/build-use.log" || { echo USE-BUILD-FAIL; exit 1; }
ar rcs "$OUT/lib-m23pgo.a" "$OUT/use/dec.o" "$OUT/use/enc.o" || { echo USE-AR-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -o "$OUT/bench-m23pgo" "$OUT/use/dec.o" "$OUT/use/enc.o" "$P/bench/bench.c" 2>> "$OUT/build-use.log" || { echo USE-LINK-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -c -o "$OUT/use/cli.o" "$P/src/port_cli.c" 2>> "$OUT/build-use.log" || { echo USE-BUILD-FAIL; exit 1; }
cc -O2 -std=c11 -Wall -Wextra -I"$P/include" -fprofile-instr-use="$OUT/pgo.profdata" -o "$OUT/cli-m23pgo" "$OUT/use/dec.o" "$OUT/use/enc.o" "$OUT/use/cli.o" 2>> "$OUT/build-use.log" || { echo USE-LINK-FAIL; exit 1; }
echo "use-warnings=$(grep -ci warning "$OUT/build-use.log" || true)"
md5 -q "$OUT/bench-m23pgo" | tee "$OUT/bin-m23pgo.md5"
echo USE-BUILD-OK
echo "== build libt2d + stack wrapper =="
cc -O2 -Wall -Wextra -dynamiclib -o "$OUT/libt2d.dylib" "$A/lzmesh_t2d.c" 2> "$OUT/build-t2d.log" || { echo T2D-BUILD-FAIL; exit 1; }
echo "t2d-warnings=$(grep -ci warning "$OUT/build-t2d.log" || true)"
printf '#!/bin/sh\n# stack wrapper: M23-PGO under interpose-T2D (own ad-hoc bins, SIP-clean).\nexec env DYLD_INSERT_LIBRARIES="%s" T2STAT=1 "%s" "$@"\n' "$OUT/libt2d.dylib" "$OUT/bench-m23pgo" > "$OUT/bench-stack"
chmod +x "$OUT/bench-stack"
printf '#!/bin/sh\nexec env DYLD_INSERT_LIBRARIES="%s" "%s" "$@"\n' "$OUT/libt2d.dylib" "$OUT/cli-m23pgo" > "$OUT/cli-stack"
chmod +x "$OUT/cli-stack"
echo T2D-BUILD-OK
echo "== IDENTs 12/12 x3 (M23 + PGO + VD) =="
: > "$OUT/ident-m23.txt"; : > "$OUT/ident-pgo.txt"; : > "$OUT/ident-vd.txt"
for corp in text-256k mixed-128k zeros-64k; do
  case $corp in text-256k) f=$P/bench/corpus/text-256k.bin;; mixed-128k) f=$P/bench/corpus/mixed-128k.bin;; *) f=$P/bench/corpus/zeros-64k.bin;; esac
  for lv in e00 e01 e05 e09; do
    "$OUT/cli-base" enc $lv < "$f" > "$OUT/n.bin" 2>/dev/null
    "$OUT/cli-m23" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/ident-m23.txt"; else echo "$corp $lv DIV" >> "$OUT/ident-m23.txt"; fi
    "$OUT/cli-m23pgo" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/ident-pgo.txt"; else echo "$corp $lv DIV" >> "$OUT/ident-pgo.txt"; fi
    "$OUT/cli-stack" enc $lv < "$f" > "$OUT/g.bin" 2>/dev/null
    if cmp -s "$OUT/n.bin" "$OUT/g.bin"; then echo "$corp $lv IDENT" >> "$OUT/ident-vd.txt"; else echo "$corp $lv DIV" >> "$OUT/ident-vd.txt"; fi
  done
done
echo "M23-IDENT: $(grep -c IDENT "$OUT"/ident-m23.txt)/12 PGO-IDENT: $(grep -c IDENT "$OUT"/ident-pgo.txt)/12 VD-IDENT: $(grep -c IDENT "$OUT"/ident-vd.txt)/12"
[ "$(grep -c IDENT "$OUT/ident-m23.txt")" = "12" ] || { echo M23-IDENT-FAIL-HALT; exit 1; }
[ "$(grep -c IDENT "$OUT/ident-pgo.txt")" = "12" ] || { echo PGO-IDENT-FAIL-HALT; exit 1; }
[ "$(grep -c IDENT "$OUT/ident-vd.txt")" = "12" ] || { echo VD-IDENT-FAIL-HALT; exit 1; }
rm -f "$OUT/n.bin" "$OUT/g.bin"
echo "== T2D live probe (mL9e, T2STAT) =="
"$OUT/bench-stack" -n 1 -l 9 "$P/bench/corpus/mixed-128k.bin" > /dev/null 2> "$OUT/t2stat.txt" || { echo T2D-PROBE-FAIL; exit 1; }
cat "$OUT/t2stat.txt"
grep -q 'hits=[1-9]' "$OUT/t2stat.txt" || { echo T2D-NOHIT-HALT; exit 1; }
echo T2D-LIVE-OK
echo "== gated A/B n=$((RUNS * REPS)) (RUNS=$RUNS REPS=$REPS) base-vs-stack =="
uptime
BENCH_RUNS=$RUNS BENCH_REPS=$REPS sh "$P/bench/run_gated.sh" --ab "$OUT/ab-r22mL9stack" "$OUT/bench-base" "$OUT/bench-stack" $CFILES || { echo AB-FAIL; exit 1; }
echo "ab rc=0"
uptime
python3 "$P/bench/cmp.py" "$OUT/ab-r22mL9stack/bbase" "$OUT/ab-r22mL9stack/bnew" | tee "$OUT/cmp.txt"
echo "== done =="
uptime
