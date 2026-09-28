#!/usr/bin/env python3
"""Compare base vs opt gated-bench dirs: median MiB/s + uplift + verdict.

Promoted from tmp/p7-w7/cmp.py (lane p8-benchharden) with:
  - `# loadavg` / `# bin` / `# pin` header parsing -> provenance header
  - per-cell verdict: SEPARATED (bands disjoint) vs OVERLAP (bands touch)
  - suspect-run detector: OVERLAP cells where dropping exactly one run's
    samples would separate the bands (host-noise excursion, cf. PERF.md
    baseline note on text-256k L9-enc run 3)

Usage: cmp.py <basedir> <optdir>   (each holds run*.tsv from run_gated.sh)

Throughput convention (unchanged): encode MiB/s over input bytes,
decode MiB/s over decoded bytes. Bands are p10-p90 over all samples.
See GATED-PROTOCOL.md for the exact separation rule + n policy.
"""
import glob
import statistics
import sys


def parse_header(fn):
    """Extract loadavg triple, ncpu, pin, bin from `#` comment lines."""
    h = {"load": None, "ncpu": None, "pin": None, "bin": None}
    with open(fn) as f:
        for line in f:
            if not line.startswith("#"):
                break
            p = line[1:].split()
            if len(p) >= 5 and p[0] == "loadavg":
                try:
                    h["load"] = tuple(float(x) for x in p[1:4])
                except ValueError:
                    pass
                for k, v in zip(p[4::2], p[5::2]):
                    if k == "ncpu":
                        h["ncpu"] = v
                    elif k == "pin":
                        h["pin"] = v
            elif len(p) >= 2 and p[0] == "bin":
                h["bin"] = p[1]
    return h


def load(d):
    """-> (cells {(corpus,level,op): [(run, mib)]}, headers {fn: header})."""
    cells, headers = {}, {}
    for fn in sorted(glob.glob(f"{d}/run*.tsv")):
        headers[fn] = parse_header(fn)
        with open(fn) as f:
            for line in f:
                if line.startswith("#"):
                    continue
                p = line.rstrip("\n").split("\t")
                if len(p) != 6:
                    continue
                corpus, level, op, inb, outb, ns = p
                level, inb, outb, ns = int(level), int(inb), int(outb), int(ns)
                if ns == 0:
                    continue
                mib = (inb if op == "enc" else outb) / 1048576 / (ns / 1e9)
                cells.setdefault((corpus, level, op), []).append((fn, mib))
    return cells, headers


def q(v, f):
    v = sorted(v)
    i = (len(v) - 1) * f
    lo, hi = int(i), min(int(i) + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (i - lo)


def band(v):
    return q(v, 0.1), q(v, 0.9)


def separated(b, o):
    """Bands disjoint? Returns +1 (opt faster), -1 (opt slower), 0 overlap."""
    blo, bhi = band(b)
    olo, ohi = band(o)
    if olo > bhi:
        return 1
    if blo > ohi:
        return -1
    return 0


def suspect_run(bpairs, opairs):
    """Run whose removal alone separates the bands, else None.

    Checks both sides' run files; reports e.g. 'base:run3.tsv'.
    """
    for tag, pairs in (("base", bpairs), ("opt", opairs)):
        runs = sorted(set(fn for fn, _ in pairs))
        if len(runs) < 3:
            continue
        for r in runs:
            rest = [m for fn, m in pairs if fn != r]
            other = [m for _, m in (opairs if tag == "base" else bpairs)]
            if len(rest) < 3 or len(other) < 3:
                continue
            s = separated(rest, other) if tag == "base" else separated(other, rest)
            if s != 0:
                short = r.rsplit("/", 1)[-1]
                return f"{tag}:{short}"
    return None


def header_line(name, headers):
    loads = [h["load"][0] for h in headers.values() if h["load"]]
    pins = set(h["pin"] for h in headers.values() if h["pin"])
    ncpus = set(h["ncpu"] for h in headers.values() if h["ncpu"])
    bins = set(h["bin"] for h in headers.values() if h["bin"])
    if loads:
        lstr = f"load1 {min(loads):.2f}-{max(loads):.2f}"
    else:
        lstr = "load1 n/a (no # loadavg headers — ungated run?)"
    return (f"{name}: runs={len(headers)} {lstr} "
            f"ncpu={','.join(sorted(ncpus)) or 'n/a'} "
            f"pin={','.join(sorted(pins)) or 'n/a'} "
            f"bin={','.join(sorted(bins)) or 'n/a'}")


def main():
    base, baseh = load(sys.argv[1])
    opt, opth = load(sys.argv[2])
    print(header_line("base", baseh))
    print(header_line("opt ", opth))
    print(f"base cells={len(base)} opt cells={len(opt)}")
    n_sep = n_ov = 0
    for corp in ("text-256k.bin", "mixed-128k.bin", "zeros-64k.bin"):
        for op in ("enc", "dec"):
            print(f"--- {corp} {op} ---")
            for lv in (0, 1, 5, 9):
                b, o = base.get((corp, lv, op), []), opt.get((corp, lv, op), [])
                if not b or not o:
                    print("  L%d: missing" % lv)
                    continue
                bv = [m for _, m in b]
                ov = [m for _, m in o]
                mb, mo = statistics.median(bv), statistics.median(ov)
                s = separated(bv, ov)
                if s != 0:
                    verdict = "SEPARATED(opt %s)" % ("faster" if s > 0 else "slower")
                    n_sep += 1
                else:
                    n_ov += 1
                    sus = suspect_run(b, o)
                    verdict = "OVERLAP" + (f" suspect-{sus}" if sus else "")
                print(f"  L{lv}: base {mb:.2f} ({q(bv,.1):.2f}-{q(bv,.9):.2f}) "
                      f"opt {mo:.2f} ({q(ov,.1):.2f}-{q(ov,.9):.2f}) "
                      f"uplift {(mo-mb)/mb*100:+.1f}% n={len(b)}/{len(o)} {verdict}")
    print(f"verdicts: {n_sep} SEPARATED, {n_ov} OVERLAP")


if __name__ == "__main__":
    main()
