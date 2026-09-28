#!/bin/sh
# selftest_gated.sh — hermetic self-tests for run_gated.sh + cmp.py.
# Uses stub bench binaries (no codec, no real load); fast anywhere.
# Exit 0 = all PASS, 1 = any FAIL.
set -u
HERE=$(dirname "$0")
T=$(mktemp -d /tmp/gated-selftest.XXXXXX)
trap 'rm -rf "$T"' EXIT
FAIL=0
ok() { printf 'PASS: %s\n' "$1"; }
bad() { printf 'FAIL: %s\n' "$1"; FAIL=1; }

# stub bench: emits 2 deterministic TSV rows, ignores flags
mkdir -p "$T/bin"
cat > "$T/bin/stub-bench" <<'EOF'
#!/bin/sh
printf 'text-256k.bin\t0\tenc\t100\t50\t1000000\n'
printf 'text-256k.bin\t0\tdec\t50\t100\t500000\n'
EOF
chmod +x "$T/bin/stub-bench"

# 1. refusal under fake load, exit 3, REFUSED logged
BENCH_FAKE_LOAD="999 999 999" BENCH_RUNS=2 sh "$HERE/run_gated.sh" \
	"$T/ref" "$T/bin/stub-bench" corpus.bin >"$T/ref.out" 2>&1
RC=$?
[ $RC -eq 3 ] && grep -q REFUSED "$T/ref/GATE.log" \
	&& ok "refusal exit=3 + REFUSED logged" \
	|| bad "refusal (exit=$RC, see $T/ref.out)"

# 2. pass path: comments present, rows present, GATE.log PASS
BENCH_FAKE_LOAD="0.10 0.20 0.30" BENCH_RUNS=2 BENCH_NOPIN=1 \
	sh "$HERE/run_gated.sh" "$T/pass" "$T/bin/stub-bench" corpus.bin \
	>"$T/pass.out" 2>&1
RC=$?
if [ $RC -eq 0 ] && grep -q '^# gated-bench v1' "$T/pass/run1.tsv" \
	&& grep -q '^# loadavg 0.10 0.20 0.30 ncpu .* pin none(BENCH_NOPIN)' "$T/pass/run1.tsv" \
	&& [ "$(grep -c -v '^#' "$T/pass/run1.tsv")" = 2 ] \
	&& grep -q 'gate PASS' "$T/pass/GATE.log"; then
	ok "pass path: v1 header + loadavg/pin comments + 2 rows + GATE.log"
else
	bad "pass path (exit=$RC, see $T/pass.out)"
fi

# 3. --ab interleave: both sides get all runs
BENCH_FAKE_LOAD="0.5 0.5 0.5" BENCH_RUNS=3 BENCH_NOPIN=1 \
	sh "$HERE/run_gated.sh" --ab "$T/ab" "$T/bin/stub-bench" \
	"$T/bin/stub-bench" corpus.bin >"$T/ab.out" 2>&1
RC=$?
if [ $RC -eq 0 ] && [ "$(ls "$T/ab/bbase"/run*.tsv "$T/ab/bnew"/run*.tsv | wc -l)" -eq 6 ] \
	&& grep -q 'mode=AB-interleaved' "$T/ab/GATE.log"; then
	ok "ab interleave: 3+3 TSVs + mode logged"
else
	bad "ab interleave (exit=$RC, see $T/ab.out)"
fi

# 4. FORCE override: runs despite trip, OVERRIDE logged
BENCH_FAKE_LOAD="999 999 999" BENCH_FORCE=1 BENCH_RUNS=1 BENCH_NOPIN=1 \
	sh "$HERE/run_gated.sh" "$T/force" "$T/bin/stub-bench" corpus.bin \
	>"$T/force.out" 2>&1
RC=$?
[ $RC -eq 0 ] && grep -q OVERRIDE "$T/force/GATE.log" \
	&& ok "force override runs + OVERRIDE logged" \
	|| bad "force override (exit=$RC, see $T/force.out)"

# 5. cmp.py: SEPARATED + OVERLAP verdicts + loadavg header
mkdir -p "$T/cb" "$T/co"
gen() { # gen <dir> <ns> : one gated TSV with header + enc row
	printf '# gated-bench v1 2026-09-28T00:00:00Z\n# bin %s\n# loadavg 1.50 1.00 0.50 ncpu 10 pin taskpolicy-t0l0\n# reps 1 levels 0 run 1-of-1\ntext-256k.bin\t0\tenc\t1048576\t524288\t%s\n' "$2" "$3" > "$1/run1.tsv"
}
gen "$T/cb" base 1000000000   # 1.0 MiB/s
gen "$T/co" opt  250000000    # 4.0 MiB/s
OUT1=$(python3 "$HERE/cmp.py" "$T/cb" "$T/co")
echo "$OUT1" | grep -q 'load1 1.50-1.50' \
	&& echo "$OUT1" | grep -q 'SEPARATED(opt faster)' \
	&& ok "cmp header load1 range + SEPARATED verdict" \
	|| bad "cmp separated/header: $OUT1"
gen "$T/co" opt 1000000000    # identical -> OVERLAP
OUT2=$(python3 "$HERE/cmp.py" "$T/cb" "$T/co")
echo "$OUT2" | grep -q 'OVERLAP' \
	&& ok "cmp OVERLAP verdict on identical cells" \
	|| bad "cmp overlap: $OUT2"

# 6. cmp.py suspect-run detector: 1 hot run of 4 on base side
rm -f "$T/cb"/run*.tsv
i=1; while [ $i -le 4 ]; do
	NS=1000000000; [ $i -eq 3 ] && NS=4000000000
	printf '# gated-bench v1 t\n# loadavg 0.5 0.5 0.5 ncpu 10 pin none(x)\ntext-256k.bin\t0\tenc\t1048576\t524288\t%s\n' "$NS" > "$T/cb/run$i.tsv"
	i=$((i+1))
done
j=1; while [ $j -le 4 ]; do
	printf '# gated-bench v1 t\n# loadavg 0.5 0.5 0.5 ncpu 10 pin none(x)\ntext-256k.bin\t0\tenc\t1048576\t524288\t1100000000\n' > "$T/co/run$j.tsv"
	j=$((j+1))
done
OUT3=$(python3 "$HERE/cmp.py" "$T/cb" "$T/co")
echo "$OUT3" | grep -q 'suspect-base:run3.tsv' \
	&& ok "cmp suspect-run flags base:run3.tsv" \
	|| bad "cmp suspect-run: $(echo "$OUT3" | grep 'L0:')"
echo "$OUT3" | grep -q . || true

[ $FAIL -eq 0 ] && echo "GATED-SELFTEST-GREEN" || echo "GATED-SELFTEST-RED"
exit $FAIL
