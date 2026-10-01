#!/bin/sh
# r2gate_matrix.sh — interleaved multi-binary round-robin matrix (lane r2-gate).
#
# ONE matrix across base + all admitted Round-2 variants: N labels
# (N=5: base store decv1 decv2 enc9a4), 24 cells = 3 corpus x 4 levels
# (0/1/5/9) x 2 ops (enc/dec), n = RUNS x REPS (10 x 7 = 70), gated + pinned.
# Modeled on bench/matrix_gated.sh (same run_gated.sh cell path, same
# stitched `# matrix-bench v2` TSV schema so bench/cmp.py works unchanged).
#
# Interleave: for each outer run, for each (file, level) cell, ALL labels run
# back-to-back in an order rotated by (run + cellidx) mod N, so no label is
# systematically first/last (order-bias cancellation across N sides).
# Every cell-label invocation goes through run_gated.sh (BENCH_RUNS=1), so the
# load gate (refuse-and-log, exit 3 EGATED) + P-core pin record apply per
# cell-label, and every raw TSV keeps its `# gated-bench v1` header.
#
# usage:
#   R2_LABELS="base store ..." R2_BINS="/path/bench.base /path/bench.store ..."
#     r2gate_matrix.sh <outdir> <corpus...>
#
# env:
#   R2_LABELS / R2_BINS  space-separated, same arity (required)
#   MATRIX_RUNS=10 MATRIX_REPS=7 MATRIX_LEVELS="" (default 0159)
#   MATRIX_COOLDOWN_SECS=20   sleep between outer runs (60 on Air)
#   BENCH_MAXLOAD_MULT=2 BENCH_NOPIN="" BENCH_FORCE="" BENCH_FAKE_LOAD=""
#     (passed through to run_gated.sh; FORCE = schema smoke, never quoted)
#
# outputs under <outdir>:
#   <label>/run<N>.tsv   stitched n=RUNSxREPS TSVs with `# matrix v2` headers
#     + `# bin`/`# loadavg` lines:  cmp.py <out>/base <out>/<label>
#   raw/run<N>/<file>/L<lv>/<label>/run1.tsv  per-cell gated TSVs (audit)
#   MATRIX.log  per-run rows/pin + VOID flags
#   GATE.log    concatenated run_gated.sh gate logs (per cell-label)
#
#-corpus size pins (refuse on mismatch for known basenames):
#   text-256k.bin=262144 mixed-128k.bin=131072 zeros-64k.bin=65536
#
# ZEROS / DEC-FLOOR HAZARD (read before quoting): zeros-64k encodes to tens
# of bytes and decodes in ~microseconds; L0-dec everywhere is sub-ms. Timer
# granularity + call overhead dominate those cells — uplifts there are floor
# artifacts, never claims. The matrix records the numbers; ship/kill counts
# exclude zeros cells (see LANE-R2-GATE.md).
set -u

SELFDIR=$(dirname "$0")
RUNGATED="$SELFDIR/../../run_gated.sh"
MLOG=""

log() { printf '%s\n' "$*" >> "$MLOG"; printf '%s\n' "$*" >&2; }
die() { log "MATRIX-FATAL: $*"; exit 2; }

abspath() {
	case "$1" in
		/*) printf '%s' "$1" ;;
		*) printf '%s/%s' "$(pwd)" "$1" ;;
	esac
}

ncpu() {
	n=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null ||
	    getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
	printf '%s' "$n"
}

boxname() {
	h=$(hostname 2>/dev/null || echo unknown)
	os=$(sw_vers -productVersion 2>/dev/null || uname -r 2>/dev/null || echo unknown)
	printf '%s/%s' "$h" "$os"
}

buildsha() {
	# $1 = dir to probe (port dir). nogit on pushed trees (easy-ssh excludes .git).
	if (cd "$1" && git rev-parse HEAD 2>/dev/null); then :; else echo "nogit"; fi
}

check_corpus_size() {
	# $1 = path. Refuse on known-basename size mismatch.
	base=$(basename "$1")
	sz=$(wc -c < "$1" | tr -d ' ')
	case "$base" in
		text-256k.bin) want=262144 ;;
		mixed-128k.bin) want=131072 ;;
		zeros-64k.bin) want=65536 ;;
		*) return 0 ;;
	esac
	if [ "$sz" != "$want" ]; then
		die "corpus size drift: $base is $sz bytes, want $want"
	fi
}

# rotate <shift> <list...>: print list rotated left by shift (mod arity).
rotate() {
	shift_n=$1; shift
	n=$#
	[ "$n" -gt 0 ] || return 0
	shift_n=$((shift_n % n))
	i=1
	for x in "$@"; do
		if [ "$i" -gt "$shift_n" ]; then printf '%s ' "$x"; fi
		i=$((i + 1))
	done
	i=1
	for x in "$@"; do
		if [ "$i" -le "$shift_n" ]; then printf '%s ' "$x"; fi
		i=$((i + 1))
	done
	echo
}

# bin_for <label>: print bench binary path for a label (parallel lists).
bin_for() {
	need=$1
	set -- $R2_BINS
	for lbl in $R2_LABELS; do
		b=$1; shift
		if [ "$lbl" = "$need" ]; then printf '%s' "$b"; return 0; fi
	done
	return 1
}

[ $# -ge 2 ] || die "usage: R2_LABELS=.. R2_BINS=.. $0 <outdir> <corpus...>"
[ -n "${R2_LABELS:-}" ] && [ -n "${R2_BINS:-}" ] || die "R2_LABELS/R2_BINS required"
[ -x "$RUNGATED" ] || die "run_gated.sh missing: $RUNGATED"
OUT=$1; shift
NLBL=$(printf '%s' "$R2_LABELS" | wc -w | tr -d ' ')
NBIN=$(printf '%s' "$R2_BINS" | wc -w | tr -d ' ')
[ "$NLBL" = "$NBIN" ] || die "R2_LABELS ($NLBL) / R2_BINS ($NBIN) arity mismatch"
[ "$NLBL" -ge 2 ] || die "need >= 2 labels"
for b in $R2_BINS; do
	[ -x "$b" ] || die "bench not executable: $b"
done

RUNS=${MATRIX_RUNS:-10}; REPS=${MATRIX_REPS:-7}; LEVELS=${MATRIX_LEVELS:-0159}
COOLDOWN=${MATRIX_COOLDOWN_SECS:-20}
case "$RUNS$REPS" in *[!0-9]*|"") die "MATRIX_RUNS/REPS must be positive ints";; esac
[ "$RUNS" -ge 1 ] && [ "$REPS" -ge 1 ] || die "MATRIX_RUNS/REPS must be >= 1"
echo "$LEVELS" | grep -q '[^0159]' && die "MATRIX_LEVELS subset of 0159 only"
[ -n "$LEVELS" ] || die "MATRIX_LEVELS empty"

for f in "$@"; do
	[ -f "$f" ] || die "corpus not a file: $f"
	check_corpus_size "$f"
done

NCPU=$(ncpu); BOX=$(boxname)
PORTDIR=$(dirname "$(dirname "$(dirname "$SELFDIR")")")
SHA=$(buildsha "$PORTDIR")
mkdir -p "$OUT" "$OUT/raw"
MLOG="$OUT/MATRIX.log"
GATEAGG="$OUT/GATE.log"
: > "$MLOG"; : > "$GATEAGG"
for lbl in $R2_LABELS; do mkdir -p "$OUT/$lbl"; done
log "r2gate matrix start labels=[$R2_LABELS] runs=$RUNS reps=$REPS levels=$LEVELS ncpu=$NCPU box=$BOX sha=$SHA cooldown=${COOLDOWN}s"
log "r2gate matrix bins=[$R2_BINS]"

LVLIST=$(printf '%s' "$LEVELS" | sed 's/./& /g')

i=1
while [ "$i" -le "$RUNS" ]; do
	if [ "$i" -gt 1 ] && [ "$COOLDOWN" -gt 0 ]; then
		log "run $i: cool-down ${COOLDOWN}s"
		sleep "$COOLDOWN"
	fi
	cello=0
	for f in "$@"; do
		lbl_file=$(basename "$f")
		for lv in $LVLIST; do
			ORDER=$(rotate $(( (i + cello) % NLBL )) $R2_LABELS)
			cello=$((cello + 1))
			for lbl in $ORDER; do
				BIN=$(bin_for "$lbl") || die "no bin for label $lbl"
				CELLD="$OUT/raw/run$i/$lbl_file/L$lv/$lbl"
				BENCH_RUNS=1 BENCH_REPS=$REPS BENCH_LEVELS=$lv \
					sh "$RUNGATED" "$CELLD" "$BIN" "$f" >>"$GATEAGG" 2>&1
				rc=$?
				echo "--- run$i $lbl_file L$lv $lbl rc=$rc" >>"$GATEAGG"
				if [ $rc -ne 0 ]; then
					log "run $i: $lbl_file L$lv $lbl rc=$rc (3=EGATED hot box); partial kept under $OUT"
					exit $rc
				fi
			done
		done
	done
	STAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)
	# Stitch per-label TSVs with matrix v2 headers.
	for lbl in $R2_LABELS; do
		BIN=$(bin_for "$lbl")
		TSV="$OUT/$lbl/run$i.tsv"
		LOAD3=$(grep -h '^# loadavg' "$OUT"/raw/run"$i"/*/*/*/run1.tsv 2>/dev/null \
			| awk '{if ($3+0>m1) m1=$3; if ($4+0>m5) m5=$4; if ($5+0>m15) m15=$5} END{print (m1==""?"0 0 0":m1" "m5" "m15)}')
		PINS=$(grep -h '^# loadavg' "$OUT"/raw/run"$i"/*/*/"$lbl"/run1.tsv 2>/dev/null \
			| sed 's/.*pin //' | sort -u | tr '\n' ',' | sed 's/,$//')
		{
			echo "# matrix-bench v2 $STAMP"
			echo "# box $BOX ncpu $NCPU"
			echo "# build-sha $SHA"
			echo "# bin $BIN"
			echo "# side $lbl run $i-of-$RUNS reps $REPS levels $LEVELS"
			echo "# loadavg $LOAD3 ncpu $NCPU pin ${PINS:-n/a}"
		} > "$TSV"
		for f in "$@"; do
			lbl_file=$(basename "$f")
			for lv in $LVLIST; do
				grep -v '^#' "$OUT/raw/run$i/$lbl_file/L$lv/$lbl/run1.tsv" >>"$TSV"
			done
		done
		rows=$(grep -c -v '^#' "$TSV")
		nfiles=$#
		nlv=${#LEVELS}
		want=$((nfiles * nlv * 2 * REPS))
		log "run $i: side=$lbl rows=$rows want=$want"
		[ "$rows" -eq "$want" ] || log "run $i: ROW-COUNT MISMATCH side=$lbl rows=$rows want=$want (see cmp missing cells)"
	done
	# Pin-void check across all labels of this run (GATED-PROTOCOL §2).
	ALLPINS=$(grep -h '^# loadavg' "$OUT"/raw/run"$i"/*/*/*/run1.tsv 2>/dev/null \
		| sed 's/.*pin //' | sort -u | tr '\n' ',' | sed 's/,$//')
	case "$ALLPINS" in
		*,*) log "run $i: PIN-MIXED [$ALLPINS] — comparison VOID, re-run" ;;
		*) log "run $i: pin=[$ALLPINS] rows done" ;;
	esac
	i=$((i + 1))
done

log "r2gate matrix DONE runs=$RUNS (-> cmp.py $OUT/<base> $OUT/<label>)"
