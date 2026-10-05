#!/usr/bin/env python3
"""Materialize battery full-tier inputs for a seed range as train files.
Usage: mk_fresh.py <battery.py-dir> <seed-offset> <nseeds> <outdir>
Skips size-0 inputs (no bytes, no profile). Prints manifest TSV to stdout
(input_id, size, file) + summary line. Deterministic (seeded rng in battery).
"""
import os
import sys

sys.path.insert(0, sys.argv[1])
from battery import corpus_for_seed  # noqa: E402

off, n, outdir = int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
os.makedirs(outdir, exist_ok=True)
nfiles = nbytes = nskip = 0
for seed in range(off, off + n):
    for input_id, data in corpus_for_seed(seed, "full"):
        if len(data) == 0:
            nskip += 1
            continue
        fn = "fresh-s%02d-%s.bin" % (seed, input_id)
        with open(os.path.join(outdir, fn), "wb") as f:
            f.write(data)
        nfiles += 1
        nbytes += len(data)
        print("%s\t%d\t%s" % (input_id, len(data), fn))
print("# seeds=%d-%d files=%d bytes=%d skipped-empty=%d"
      % (off, off + n - 1, nfiles, nbytes, nskip), file=sys.stderr)
