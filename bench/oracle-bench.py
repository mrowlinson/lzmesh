#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""oracle-bench.py -- stdio-pipe throughput harness for the Apple oracle.

Same TSV schema + CLI shape as bench/bench.c so run_gated.sh / cmp.py
work unchanged:
  file  level  op  in_bytes  out_bytes  ns        (stdout, one row per rep)
  ./oracle-bench.py [-n reps] [-l 0159] corpus...  (probe via $ORACLE_PROBE)

For each corpus file and each level (0/1/5/9 -> e00/e01/e05/e09): one
untimed warmup encode (pins baseline bytes), N timed `oracle_probe enc`
reps (bytes must match baseline every rep), then N timed
`oracle_probe dec` reps over the baseline bytes (DECODE_SIZE=input len,
size checked every rep, bytes verified once). Mirrors bench.c order.

METHOD ASYMMETRY (read before quoting): bench.c times the port
in-process (CLOCK_MONOTONIC around the codec call). Every sample here
is one full process spawn: fork+exec+dyld, dlopen(libcompression),
stdin/stdout pipes, plus the codec. So:

  Apple in-process throughput  >=  piped numbers below, always.

Quoting direction: port-vs-piped-Apple gaps FAVOR THE PORT (Apple's
true in-process speed can only be higher). Any "Apple faster" cell is
conservative; any "port faster" cell must clear the pipe overhead to
mean anything. Overhead dominates small/fast cells (zeros-64k) and is
a few % on slow cells (text-256k L9-enc, ~50ms+ of codec per rep).

Env:
  ORACLE_PROBE  path to oracle_probe binary (required)
  ORACLE_LIB    passed through to oracle_probe (default: system lib)
Exit: 0 ok, 1 correctness failure, 2 usage (mirrors bench.c).
"""
import os
import subprocess
import sys
import time

LEVELS = (("0", "e00"), ("1", "e01"), ("5", "e05"), ("9", "e09"))

failures = 0


def fail(msg):
    global failures
    failures += 1
    sys.stderr.write("oracle-bench: %s\n" % msg)


def run_probe(probe, op, sel, data, decode_size, env):
    """One probe invocation. Returns (rc, stdout_bytes, ns_wall)."""
    e = dict(env)
    if not op == "enc":
        e["DECODE_SIZE"] = str(decode_size)
    t0 = time.monotonic_ns()
    try:
        p = subprocess.run([probe, op, sel], input=data,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           env=e, timeout=600)
    except subprocess.TimeoutExpired:
        return (-1, b"", time.monotonic_ns() - t0)
    return (p.returncode, p.stdout, time.monotonic_ns() - t0)


def bench_file(probe, path, label, reps, lmask, env):
    global failures
    try:
        with open(path, "rb") as f:
            src = f.read()
    except OSError:
        fail("cannot read %s" % path)
        return
    n = len(src)
    if n == 0:
        fail("empty input %s" % path)
        return
    for lname, sel in LEVELS:
        if lname not in lmask:
            continue
        # warmup + baseline bytes (untimed)
        rc, base, _ = run_probe(probe, "enc", sel, src, n, env)
        if rc == 10:
            fail("%s L%s enc refused" % (label, lname))
            continue
        if rc != 0 or len(base) == 0:
            fail("%s L%s enc failed rc=%d" % (label, lname, rc))
            continue
        # warmup decode: pins roundtrip validity before timing
        rc, dec0, _ = run_probe(probe, "dec", sel, base, n, env)
        if rc != 0 or dec0 != src:
            fail("%s L%s roundtrip mismatch rc=%d" % (label, lname, rc))
            continue
        for _ in range(reps):
            rc, out, ns = run_probe(probe, "enc", sel, src, n, env)
            if rc != 0 or out != base:
                fail("%s L%s enc nondeterministic rc=%d" % (label, lname, rc))
                break
            sys.stdout.write("%s\t%s\tenc\t%d\t%d\t%d\n"
                             % (label, lname, n, len(out), ns))
        else:
            for _ in range(reps):
                rc, out, ns = run_probe(probe, "dec", sel, base, n, env)
                if rc != 0 or len(out) != n:
                    fail("%s L%s dec rep -> rc=%d len=%d"
                         % (label, lname, rc, len(out)))
                    break
                sys.stdout.write("%s\t%s\tdec\t%d\t%d\t%d\n"
                                 % (label, lname, len(base), len(out), ns))
            else:
                if out != src:
                    fail("%s L%s decode bytes mismatch" % (label, lname))
                    continue
                continue
            continue


def main(argv):
    reps = 7
    lmask = set("0159")
    i = 1
    files_at = 1
    while i < len(argv) and argv[i].startswith("-"):
        if argv[i] == "-n" and i + 1 < len(argv):
            reps = int(argv[i + 1])
            i += 2
            files_at = i
        elif argv[i] == "-l" and i + 1 < len(argv):
            lmask = set(argv[i + 1]) & set("0159")
            i += 2
            files_at = i
        else:
            sys.stderr.write("usage: %s [-n reps] [-l 0159] file...\n" % argv[0])
            return 2
    files = argv[files_at:]
    probe = os.environ.get("ORACLE_PROBE", "")
    if reps < 1 or not lmask or not files:
        sys.stderr.write("usage: %s [-n reps] [-l 0159] file...\n" % argv[0])
        return 2
    if not probe or not os.access(probe, os.X_OK):
        sys.stderr.write("oracle-bench: $ORACLE_PROBE not executable: %r\n" % probe)
        return 2
    env = dict(os.environ)
    for path in files:
        label = path.rsplit("/", 1)[-1]
        bench_file(probe, path, label, reps, lmask, env)
    if failures:
        sys.stderr.write("oracle-bench: %d failures\n" % failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
