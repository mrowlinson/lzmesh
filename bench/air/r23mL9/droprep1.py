#!/usr/bin/env python3
"""droprep1.py — extended-warmup filter (R23 methodology standard).

Copies a gated-bench dir (run*.tsv + run*.log) while discarding the FIRST
timed rep per cell per run-file, uniformly over every (corpus, level, op)
and every file. bench.c and abench.c both emit reps r=0..N-1 sequentially
per cell, so "first data row per cell per file" == rep 1. Output feeds
verbatim cmp.py (n=70>60 for RUNS=10 x REPS=7).

Rule (pre-registered DROP-r23-matrix-3-methodology, prospective from R23):
extended warmup = one extra discarded rep per run per cell, both sides,
all rigs (matrix + PGO + future gates). Filed R21/R22 verdicts stand.

Usage: droprep1.py <indir> <outdir> [--drop K=1]
Provenance: each output TSV keeps all `#` headers and gains a trailing
`# extended-warmup` line (cmp.py skips `#` lines anywhere, so verdicts
derive from timed rows only).
"""
import datetime
import glob
import os
import shutil
import sys

DROP_DEFAULT = 1


def filter_tsv(src, dst, drop):
    seen = {}
    kept = dropped = 0
    with open(src) as f:
        lines = f.readlines()
    with open(dst, "w") as f:
        for line in lines:
            if line.startswith("#"):
                f.write(line)
                continue
            p = line.rstrip("\n").split("\t")
            if len(p) != 6:
                f.write(line)
                continue
            key = (p[0], p[1], p[2])
            n = seen.get(key, 0)
            seen[key] = n + 1
            if n < drop:
                dropped += 1
                continue
            f.write(line)
            kept += 1
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%SZ")
        f.write(f"# extended-warmup drop-first-{drop} {stamp} "
                f"kept={kept} dropped={dropped}\n")
    return kept, dropped


def main():
    toks = sys.argv[1:]
    args = []
    drop = DROP_DEFAULT
    i = 0
    while i < len(toks):
        t = toks[i]
        if t.startswith("--drop="):
            drop = int(t.split("=", 1)[1])
        elif t == "--drop" and i + 1 < len(toks):
            drop = int(toks[i + 1])
            i += 1
        else:
            args.append(t)
        i += 1
    if len(args) != 2:
        print("usage: droprep1.py <indir> <outdir> [--drop K]",
              file=sys.stderr)
        return 2
    indir, outdir = args
    if drop < 0:
        print("droprep1: --drop must be >= 0", file=sys.stderr)
        return 2
    os.makedirs(outdir, exist_ok=True)
    tsvs = sorted(glob.glob(f"{indir}/run*.tsv"))
    if not tsvs:
        print(f"droprep1: no run*.tsv in {indir}", file=sys.stderr)
        return 1
    tk = td = 0
    for src in tsvs:
        dst = os.path.join(outdir, os.path.basename(src))
        k, d = filter_tsv(src, dst, drop)
        tk += k
        td += d
        log = src[:-4] + ".log"
        if os.path.exists(log):
            shutil.copy(log, os.path.join(outdir,
                                          os.path.basename(log)))
    print(f"droprep1: files={len(tsvs)} drop={drop} kept={tk} "
          f"dropped={td}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
