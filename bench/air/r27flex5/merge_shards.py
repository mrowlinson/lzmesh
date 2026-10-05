#!/usr/bin/env python3
"""Exact-merge battery seed-shards (R25 d77 method verbatim).
cells/buckets summed, findings concatenated shard-order, vectors union,
seconds=max shard, verdict recomputed. Deterministic per-seed corpus => merge exact.
Usage: merge_shards.py <outdir> <tier> <seeds_first> <seeds_last> <sharddir...>
"""
import json, os, shutil, sys
out, tier, first, last = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
shards = sys.argv[5:]
cells, fails, buckets, secs, sels = 0, 0, {}, 0.0, None
os.makedirs(os.path.join(out, "vectors"), exist_ok=True)
findings = []
for sh in shards:
    s = json.load(open(os.path.join(sh, "summary.json")))
    cells += s["cells"]; fails += s["fail_cells"]; secs = max(secs, s["seconds"])
    for k, v in s["buckets"].items(): buckets[k] = buckets.get(k, 0) + v
    if sels is None: sels = s["selectors"]
    assert s["selectors"] == sels, (sh, s["selectors"], sels)
    fp = os.path.join(sh, "findings.jsonl")
    if os.path.exists(fp):
        findings += open(fp).read().splitlines(keepends=True)
    vd = os.path.join(sh, "vectors")
    if os.path.isdir(vd):
        for v in sorted(os.listdir(vd)):
            shutil.copy(os.path.join(vd, v), os.path.join(out, "vectors", v))
n = last - first + 1
summ = {"cells": cells, "fail_cells": fails, "buckets": buckets,
        "seconds": round(secs, 1), "tier": tier, "seeds": [first, last, n],
        "selectors": sels, "verdict": "PASS" if fails == 0 else "FAIL"}
open(os.path.join(out, "findings.jsonl"), "w").writelines(findings)
json.dump(summ, open(os.path.join(out, "summary.json"), "w"), indent=2)
print(json.dumps({k: summ.get(k) for k in ("cells", "fail_cells", "buckets", "seconds", "seeds", "verdict")}))
