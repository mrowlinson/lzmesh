#!/bin/sh
# matrix_gated.sh — interleaved port-vs-Apple gap-matrix driver (lane matrix-harness).
#
# 24 cells = 3 corpus (text-256k/mixed-128k/zeros-64k) x 4 levels (0/1/5/9)
# x 2 ops (enc/dec). Port side = bench/bench.c binary (in-process port),
# Apple side = bench/abench binary (in-process libcompression via dlopen;
# NOT oracle-bench.py — the stdio pipe is what this lane escapes).
#
# Interleave: for each outer run, for each (file, level) cell, port and Apple
# run back-to-back (never all-of-one-then-other); side order flips with run
# parity (odd runs port-first, even runs apple-first) to cancel order bias.
# Every cell-side invocation goes through run_gated.sh (BENCH_RUNS=1), so the
# load gate (refuse-and-log, exit 3 EGATED) + P-core pin record apply per
# cell-side, and every raw TSV keeps its `# gated-bench v1` provenance header.
#
# usage:
#   matrix_gated.sh <outdir> <port-bench> <apple-abench> <encdump> <corpus...>
#
# env:
#   MATRIX_RUNS=10 MATRIX_REPS=7 MATRIX_LEVELS="" (default 0159)
#   MATRIX_COOLDOWN_SECS=20   sleep between outer runs (60 on Air)
#   BENCH_MAXLOAD_MULT=2 BENCH_NOPIN="" BENCH_FORCE="" BENCH_FAKE_LOAD=""
#     (passed through to run_gated.sh; FORCE = schema smoke, never quoted)
#   ORACLE_LIB  passed through to abench/encdump (default: system lib)
#
# outputs under <outdir>:
#   port/run<N>.tsv apple/run<N>.tsv   stitched n=RUNSxREPS TSVs, `# matrix v2`
#     headers (box/load/pin/build-sha/side) + `# bin`/`# loadavg` lines so
#     cmp.py works unchanged:  cmp.py <out>/port <out>/apple
#   bytes/run<N>.tsv   encdump port-vs-Apple byte compare per (file,level)
#   raw/run<N>/<file>/L<lv>/<side>/run1.tsv  per-cell gated TSVs (audit trail)
#   MATRIX.log  per-run rows/bytes/pin + VOID flags
#   GATE.log    concatenated run_gated.sh gate logs (per cell-side)
#
#-corpus size pins (refuse on mismatch for known basenames):
#   text-256k.bin=262144 mixed-128k.bin=131072 zeros-64k.bin=65536
#
# ZEROS / DEC-FLOOR HAZARD (read before quoting): zeros-64k encodes to tens
# of bytes and decodes in ~microseconds; L0-dec everywhere is sub-ms. Timer
# granularity + call overhead dominate those cells — uplifts there are floor
# artifacts, never claims. The matrix records the numbers; the verdicts that
# matter are the slow cells (text/mixed enc L5/L9).
set -u

SELFDIR=$(dirname "$0")
RUNGATED="$SELFDIR/run_gated.sh"
CMP_PY="$SELFDIR/cmp.py"
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

[ $# -ge 5 ] || die "usage: $0 <outdir> <port-bench> <apple-abench> <encdump> <corpus...>"
OUT=$1; PORTBIN=$(abspath "$2"); APPLEBIN=$(abspath "$3"); ENCDUMP=$(abspath "$4"); shift 4
[ -x "$PORTBIN" ] || die "port bench not executable: $PORTBIN"
[ -x "$APPLEBIN" ] || die "apple abench not executable: $APPLEBIN"
[ -x "$ENCDUMP" ] || die "encdump not executable: $ENCDUMP"
[ -x "$RUNGATED" ] && [ -f "$CMP_PY" ] || die "run_gated.sh/cmp.py missing beside $0"

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
PORTDIR=$(dirname "$SELFDIR")
SHA=$(buildsha "$PORTDIR")
mkdir -p "$OUT/port" "$OUT/apple" "$OUT/bytes" "$OUT/raw"
MLOG="$OUT/MATRIX.log"
GATEAGG="$OUT/GATE.log"
: > "$MLOG"; : > "$GATEAGG"
log "matrix start runs=$RUNS reps=$REPS levels=$LEVELS ncpu=$NCPU box=$BOX sha=$SHA cooldown=${COOLDOWN}s"
log "matrix bins port=$PORTBIN apple=$APPLEBIN encdump=$ENCDUMP oracle_lib=${ORACLE_LIB:-default}"

LVLIST=$(printf '%s' "$LEVELS" | sed 's/./& /g')

i=1
while [ "$i" -le "$RUNS" ]; do
	if [ "$i" -gt 1 ] && [ "$COOLDOWN" -gt 0 ]; then
		log "run $i: cool-down ${COOLDOWN}s"
		sleep "$COOLDOWN"
	fi
	# Side order flips with run parity (order-bias cancellation).
	if [ $((i % 2)) -eq 1 ]; then ORDER="port apple"; else ORDER="apple port"; fi
	log "run $i: order=[$ORDER]"
	for f in "$@"; do
		lbl=$(basename "$f")
		for lv in $LVLIST; do
			for side in $ORDER; do
				case "$side" in
					port) BIN=$PORTBIN ;;
					*) BIN=$APPLEBIN ;;
				esac
				CELLD="$OUT/raw/run$i/$lbl/L$lv/$side"
				BENCH_RUNS=1 BENCH_REPS=$REPS BENCH_LEVELS=$lv \
					sh "$RUNGATED" "$CELLD" "$BIN" "$f" >>"$GATEAGG" 2>&1
				rc=$?
				echo "--- run$i $lbl L$lv $side rc=$rc" >>"$GATEAGG"
				if [ $rc -ne 0 ]; then
					log "run $i: $lbl L$lv $side rc=$rc (3=EGATED hot box); partial kept under $OUT"
					exit $rc
				fi
			done
		done
	done
	# Bytes for this run (encdump is untimed; pin irrelevant, run plain).
	STAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)
	{
		echo "# matrix-bytes v2 $STAMP"
		echo "# box $BOX ncpu $NCPU"
		echo "# build-sha $SHA encdump $ENCDUMP oracle_lib ${ORACLE_LIB:-default}"
		echo "# run $i-of-$RUNS levels $LEVELS"
	} > "$OUT/bytes/run$i.tsv"
	"$ENCDUMP" $([ "$LEVELS" = "0159" ] || printf -- '-l %s' "$LEVELS") "$@" \
		>>"$OUT/bytes/run$i.tsv" 2>"$OUT/bytes/run$i.log"
	rc=$?
	nident=$(grep -c 'IDENT' "$OUT/bytes/run$i.tsv" || true)
	ndiv=$(grep -c 'DIV@' "$OUT/bytes/run$i.tsv" || true)
	nxok=$(grep -c 'X-OK' "$OUT/bytes/run$i.tsv" || true)
	nxbad=$(grep -c 'X-FAIL\|ENC-FAIL' "$OUT/bytes/run$i.tsv" || true)
	log "run $i: bytes encdump rc=$rc IDENT=$nident DIV=$ndiv X-OK=$nxok XB=$nxbad"
	[ $rc -eq 0 ] || { log "run $i: ENCDUMP FAILED — investigate $OUT/bytes/run$i.log"; exit 1; }

	# Stitch per-side TSVs with matrix v2 headers.
	for side in port apple; do
		case "$side" in port) BIN=$PORTBIN ;; *) BIN=$APPLEBIN ;; esac
		TSV="$OUT/$side/run$i.tsv"
		# Conservative load summary: max of each field across this run's
		# cell TSVs (fields: # loadavg <1> <5> <15> ncpu <n> pin <st>).
		LOAD3=$(grep -h '^# loadavg' "$OUT"/raw/run"$i"/*/*/port/run1.tsv \
			"$OUT"/raw/run"$i"/*/*/apple/run1.tsv 2>/dev/null \
			| awk '{if ($3+0>m1) m1=$3; if ($4+0>m5) m5=$4; if ($5+0>m15) m15=$5} END{print (m1==""?"0 0 0":m1" "m5" "m15)}')
		PINS=$(grep -h '^# loadavg' "$OUT"/raw/run"$i"/*/*/"$side"/run1.tsv 2>/dev/null \
			| sed 's/.*pin //' | sort -u | tr '\n' ',' | sed 's/,$//')
		{
			echo "# matrix-bench v2 $STAMP"
			echo "# box $BOX ncpu $NCPU"
			echo "# build-sha $SHA"
			echo "# bin $BIN"
			echo "# side $side run $i-of-$RUNS reps $REPS levels $LEVELS order [$ORDER]"
			echo "# loadavg $LOAD3 ncpu $NCPU pin ${PINS:-n/a}"
		} > "$TSV"
		for f in "$@"; do
			lbl=$(basename "$f")
			for lv in $LVLIST; do
				grep -v '^#' "$OUT/raw/run$i/$lbl/L$lv/$side/run1.tsv" >>"$TSV"
			done
		done
		rows=$(grep -c -v '^#' "$TSV")
		nfiles=$#
		nlv=${#LEVELS}
		want=$((nfiles * nlv * 2 * REPS))
		log "run $i: side=$side rows=$rows want=$want"
		[ "$rows" -eq "$want" ] || log "run $i: ROW-COUNT MISMATCH side=$side rows=$rows want=$want (see cmp missing cells)"
	done
	# Pin-void check across both sides of this run (GATED-PROTOCOL §2).
	ALLPINS=$(grep -h '^# loadavg' "$OUT"/raw/run"$i"/*/*/*/run1.tsv 2>/dev/null \
		| sed 's/.*pin //' | sort -u | tr '\n' ',' | sed 's/,$//')
	case "$ALLPINS" in
		*,*) log "run $i: PIN-MIXED [$ALLPINS] — comparison VOID, re-run" ;;
		*) log "run $i: pin=[$ALLPINS] rows+bytes done" ;;
	esac
	i=$((i + 1))
done

log "matrix DONE runs=$RUNS (-> cmp.py $OUT/port $OUT/apple)"
