#!/bin/sh
# run_gated.sh — load-gated bench driver (lane p8-benchharden).
#
# Wraps bench/bench over the pinned corpus with:
#   (1) pre-run load gate: refuse-and-log when 1-min loadavg >= 2x ncpu
#       (exit 3 EGATED, distinct from bench 1=fail / 2=usage);
#   (2) best-effort P-core pinning via taskpolicy -t 0 -l 0, logged;
#   (3) interleaved A/B/A/B run order (--ab mode);
#   (4) per-run loadavg + provenance recorded as TSV `#` comments.
#
# usage:
#   run_gated.sh <outdir> <benchbin> <corpus...>
#   run_gated.sh --ab <outdir> <basebin> <newbin> <corpus...>
#
# env:
#   BENCH_RUNS=5            outer runs (n = RUNS x REPS per cell)
#   BENCH_REPS=7            inner reps passed as bench -n
#   BENCH_LEVELS=""         level filter passed as bench -l ("" = all 0159)
#   BENCH_MAXLOAD_MULT=2    gate threshold = MULT x ncpu on 1-min loadavg
#   BENCH_FAKE_LOAD=""      test injection: "1.0 0.5 0.2" triple, else real
#   BENCH_NOPIN=1           skip taskpolicy even when present
#   BENCH_FORCE=1           run despite gate trip (override, loudly logged;
#                           for schema/CI smoke only — never a measurement)
#
# outputs under <outdir> (single mode):
#   run<N>.tsv   bench TSV rows with `#` provenance header (see below)
#   run<N>.log   bench stderr
#   GATE.log     gate decisions + pin state for every run
# --ab mode: same layout under <outdir>/bbase/ and <outdir>/bnew/,
# plus <outdir>/GATE.log shared. Interleave order is base,new,base,new...
#
# TSV `#` header schema (v1; cmp.py parses `# loadavg` lines):
#   # gated-bench v1 <utc-timestamp>
#   # bin <benchbin-path>
#   # loadavg <1min> <5min> <15min> ncpu <n> pin <pin-state>
#   # reps <reps> levels <0159|subset> run <k-of-N>
set -u
EGATED=3

log() { printf '%s\n' "$*" >> "$GATELOG"; printf '%s\n' "$*" >&2; }

ncpu() {
	n=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null ||
	    getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
	printf '%s' "$n"
}

# prints "1min 5min 15min" triple
loadavg() {
	if [ -n "${BENCH_FAKE_LOAD:-}" ]; then
		printf '%s\n' "$BENCH_FAKE_LOAD"
		return
	fi
	if [ -r /proc/loadavg ]; then
		awk '{print $1, $2, $3}' /proc/loadavg
		return
	fi
	# macOS uptime: "... load averages: 1.23 0.98 0.76"
	uptime | sed -e 's/.*load averages*:[[:space:]]*//' -e 's/,//g' |
		awk '{print $1, $2, $3}'
}

# gate_check <ncpu> <load-triple> -> 0 pass, 1 trip
gate_check() {
	awk -v n="$1" -v m="${BENCH_MAXLOAD_MULT:-2}" \
		-v f="${BENCH_FORCE:-}" '
		{ trip = ($1 >= m * n);
		  if (trip && f != "") { print "FORCE"; exit 0 }
		  exit (trip ? 1 : 0) }' <<EOF
$2
EOF
}

pin_state() {
	if [ -n "${BENCH_NOPIN:-}" ]; then
		echo "none(BENCH_NOPIN)"
		return
	fi
	if ! command -v taskpolicy >/dev/null 2>&1; then
		echo "none(no-taskpolicy)"
		return
	fi
	# best effort: top throughput+latency tiers prefer P-cores on
	# Apple Silicon (a scheduler hint, not an affinity mask).
	# NOTE: -c only accepts utility/background/maintenance (E-core
	# direction); tier flags are the P-core direction. See man taskpolicy.
	if taskpolicy -t 0 -l 0 "$(command -v true)" 2>/dev/null; then
		echo "taskpolicy-t0l0"
	else
		echo "none(taskpolicy-failed)"
	fi
}

# one_run <bindir-out> <bin> <runidx> <runs> <reps> <levels> <pin> <ncpu> -- <files...>
one_run() {
	OUTD=$1; BIN=$2; IDX=$3; RUNS=$4; REPS=$5; LEVELS=$6
	PIN=$7; NCPU=$8; shift 8
	[ "${1:-}" = "--" ] && shift
	TSV="$OUTD/run$IDX.tsv"; LOG="$OUTD/run$IDX.log"
	LOAD=$(loadavg)
	L1=$(printf '%s' "$LOAD" | awk '{print $1}')
	L2=$(printf '%s' "$LOAD" | awk '{print $2}')
	L3=$(printf '%s' "$LOAD" | awk '{print $3}')
	GATE_RES=$(gate_check "$NCPU" "$LOAD"; echo "rc=$?")
	case "$GATE_RES" in
		FORCE*)
			log "run $IDX: LOAD $L1/$L2/$L3 ncpu=$NCPU thr=$(awk -v n="$NCPU" -v m="${BENCH_MAXLOAD_MULT:-2}" 'BEGIN{print m*n}') TRIPPED but BENCH_FORCE=1 -> OVERRIDE (schema smoke, not a measurement)"
			;;
		*rc=1)
			log "run $IDX: REFUSED load1=$L1 >= ${BENCH_MAXLOAD_MULT:-2}x$NCPU (EGATED=$EGATED); partial results kept under $OUTD"
			return $EGATED
			;;
		*)
			log "run $IDX: gate PASS load1=$L1 (thr ${BENCH_MAXLOAD_MULT:-2}x$NCPU=$NCPU*${BENCH_MAXLOAD_MULT:-2}) pin=$PIN"
			;;
	esac
	STAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)
	LVL_DISP=${LEVELS:-0159}
	{
		echo "# gated-bench v1 $STAMP"
		echo "# bin $BIN"
		echo "# loadavg $L1 $L2 $L3 ncpu $NCPU pin $PIN"
		echo "# reps $REPS levels $LVL_DISP run $IDX-of-$RUNS"
	} > "$TSV"
	if [ -n "$LEVELS" ]; then set -- -n "$REPS" -l "$LEVELS" "$@"; else set -- -n "$REPS" "$@"; fi
	case "$PIN" in
		taskpolicy-*) taskpolicy -t 0 -l 0 "$BIN" "$@" >> "$TSV" 2> "$LOG" ;;
		*) "$BIN" "$@" >> "$TSV" 2> "$LOG" ;;
	esac
	RC=$?
	log "run $IDX: bench exit=$RC rows=$(grep -c -v '^#' "$TSV")"
	return $RC
}

AB=0
if [ "${1:-}" = "--ab" ]; then AB=1; shift; fi
if [ "$AB" = 1 ]; then
	[ $# -ge 4 ] || { echo "usage: $0 --ab <outdir> <basebin> <newbin> <corpus...>" >&2; exit 2; }
	OUT=$1; BASEBIN=$2; NEWBIN=$3; shift 3
else
	[ $# -ge 3 ] || { echo "usage: $0 [--ab] <outdir> <benchbin> [<newbin>] <corpus...>" >&2; exit 2; }
	OUT=$1; BASEBIN=$2; shift 2
fi

RUNS=${BENCH_RUNS:-5}; REPS=${BENCH_REPS:-7}; LEVELS=${BENCH_LEVELS:-}
NCPU=$(ncpu); PIN=$(pin_state)
mkdir -p "$OUT"
GATELOG="$OUT/GATE.log"
export GATELOG
: > "$GATELOG"
log "gated-bench start mode=$([ "$AB" = 1 ] && echo AB-interleaved || echo single) runs=$RUNS reps=$REPS levels=${LEVELS:-all} ncpu=$NCPU pin=$PIN base=$BASEBIN new=${NEWBIN:-} force=${BENCH_FORCE:-0} fakeload=${BENCH_FAKE_LOAD:-none}"

i=1
while [ "$i" -le "$RUNS" ]; do
	if [ "$AB" = 1 ]; then
		mkdir -p "$OUT/bbase" "$OUT/bnew"
		GATELOG="$OUT/GATE.log" one_run "$OUT/bbase" "$BASEBIN" "$i" "$RUNS" "$REPS" "$LEVELS" "$PIN" "$NCPU" -- "$@" || exit $?
		GATELOG="$OUT/GATE.log" one_run "$OUT/bnew" "$NEWBIN" "$i" "$RUNS" "$REPS" "$LEVELS" "$PIN" "$NCPU" -- "$@" || exit $?
	else
		GATELOG="$OUT/GATE.log" one_run "$OUT" "$BASEBIN" "$i" "$RUNS" "$REPS" "$LEVELS" "$PIN" "$NCPU" -- "$@" || exit $?
	fi
	i=$((i + 1))
done
log "gated-bench DONE runs=$RUNS"
