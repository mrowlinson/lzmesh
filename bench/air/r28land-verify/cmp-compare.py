#!/usr/bin/env python3
"""Compare recompute28.py output vs cmp-matrix.txt / cmp-pgo.txt cell-by-cell.

Usage: cmp-compare.py <recompute-out> <cmp-file> <mode>
Compares: medians (exact .2f), bands (exact), n, verdict class+side
(mapped across naming), and uplift ARITHMETIC of each file from its own
medians (recompute: (p-a)/a; cmp: (o-b)/b) to printed .1f.
Exit 0 iff 24/24 cells match.
"""
import re
import sys

CELL_RE = re.compile(
    r"(\S+\.bin) L(\d) (enc|dec):\s+port ([\d.]+) \(([\d.]+)-([\d.]+)\)\s+"
    r"apple ([\d.]+) \(([\d.]+)-([\d.]+)\)\s+port-vs-apple ([+-][\d.]+)% n=(\d+)/(\d+) (OVERLAP|SEPARATED\((?:port|Apple) faster\))"
)
CMP_RE = re.compile(
    r"L(\d): base ([\d.]+) \(([\d.]+)-([\d.]+)\) opt ([\d.]+) \(([\d.]+)-([\d.]+)\) "
    r"uplift ([+-][\d.]+)% n=(\d+)/(\d+) (OVERLAP|SEPARATED\((?:opt|base) (?:faster|slower)\))"
)
HDR_RE = re.compile(r"--- (\S+\.bin) (enc|dec) ---")


def load_recompute(fn):
    cells = {}
    for line in open(fn):
        m = CELL_RE.search(line)
        if m:
            corp, lv, op, pm, plo, phi, am, alo, ahi, up, n1, n2, v = m.groups()
            cells[(corp, int(lv), op)] = (pm, plo, phi, am, alo, ahi, up, n1, n2, v)
    return cells


def load_cmp(fn):
    cells = {}
    corp = op = None
    for line in open(fn):
        h = HDR_RE.search(line)
        if h:
            corp, op = h.group(1), h.group(2)
            continue
        m = CMP_RE.search(line)
        if m and corp:
            lv, bm, blo, bhi, om, olo, ohi, up, n1, n2, v = m.groups()
            cells[(corp, int(lv), op)] = (bm, blo, bhi, om, olo, ohi, up, n1, n2, v)
    return cells


def rside(v):
    if v == "OVERLAP":
        return "OV"
    return "P" if "port faster" in v else "A"


def cside(v):
    if v == "OVERLAP":
        return "OV"
    if "opt faster" in v or "base slower" in v:
        return "O"
    return "B"


def main():
    ro, cf, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    r, c = load_recompute(ro), load_cmp(cf)
    assert len(r) == 24, f"recompute cells {len(r)}"
    assert len(c) == 24, f"cmp cells {len(c)}"
    bad = 0
    for k in sorted(r):
        pm, plo, phi, am, alo, ahi, up, n1, n2, v = r[k]
        bm, blo, bhi, om, olo, ohi, cup, cn1, cn2, cv = c[k]
        errs = []
        if not (pm == bm and plo == blo and phi == bhi):
            errs.append("base-medians")
        if not (am == om and alo == olo and ahi == ohi):
            errs.append("opt-medians")
        if not (n1 == cn1 and n2 == cn2):
            errs.append("n")
        # uplift arithmetic from own medians, to .1f
        rup = (float(pm) - float(am)) / float(am) * 100
        if abs(rup - float(up)) > 0.051:
            errs.append(f"recomp-uplift({rup:.2f}vs{up})")
        cup2 = (float(om) - float(bm)) / float(bm) * 100
        if abs(cup2 - float(cup)) > 0.051:
            errs.append(f"cmp-uplift({cup2:.2f}vs{cup})")
        # verdict: P<->B, A<->O (both modes: port=base-side, apple=opt-side)
        m = {"P": "B", "A": "O", "OV": "OV"}
        if m[rside(v)] != cside(cv):
            errs.append(f"verdict({v}vs{cv})")
        if errs:
            bad += 1
            print(f"MISMATCH {k}: {'; '.join(errs)}")
    print(f"{mode}: {24 - bad}/24 EXACT")
    sys.exit(1 if bad else 0)


main()
