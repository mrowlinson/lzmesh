#!/usr/bin/env python3
"""NEW/FIXED comparer for battery findings (lane r2-gate).

Compares a variant's findings.jsonl against the base tree's findings.jsonl
from the same battery slice (same tier/seeds/selectors/oracle): every
finding is an observable-only row, so a pure-perf variant must reproduce
the base file EXACTLY (NEW=0 FIXED=0).

Usage: newfixed.py <base-findings.jsonl> <var-findings.jsonl> [label]
Prints NEW/FIXED counts + up to 50 differing rows. Exit 0 always
(verdict recorded in text; the gate lane adjudicates).
"""
import collections
import json
import sys


def load(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rows.append(json.dumps(json.loads(line), sort_keys=True))
    return rows


def main():
    base_p, var_p = sys.argv[1], sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else var_p
    base = collections.Counter(load(base_p))
    var = collections.Counter(load(var_p))
    new = list((var - base).elements())
    fixed = list((base - var).elements())
    print(f"{label}: base_rows={sum(base.values())} var_rows={sum(var.values())} "
          f"NEW={len(new)} FIXED={len(fixed)}")
    for tag, rows in (("NEW", new), ("FIXED", fixed)):
        for r in rows[:25]:
            print(f"  {tag} {r}")
        if len(rows) > 25:
            print(f"  {tag} ... +{len(rows) - 25} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
