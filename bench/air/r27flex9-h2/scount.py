#!/usr/bin/env python3
"""Whole-file asm op counts (same opcode classes as h3count.py).
Usage: scount.py <file.s>  -> prints lines ld st cbr mov bl over ALL
indented mnemonic lines (functions + data-adjacent code; directives ignored).
Pairs with h3count.py per-function census (G3 vs G2).
"""
import re
import sys

path = sys.argv[1]
cnt = {"ld": 0, "st": 0, "cbr": 0, "mov": 0, "bl": 0}
n = 0
for l in open(path).read().splitlines():
    m = re.match(r"^[ \t]+([a-z][a-z0-9.]*)", l)
    if not m:
        continue
    n += 1
    op = m.group(1)
    if re.match(r"^ld(?![1234])[a-z0-9.]*$", op):
        cnt["ld"] += 1
    elif re.match(r"^st(?![1234])[a-z0-9.]*$", op):
        cnt["st"] += 1
    elif re.match(r"^(b\.[a-z]+|cbz|cbnz|tbz|tbnz)$", op):
        cnt["cbr"] += 1
    elif op.startswith("mov"):
        cnt["mov"] += 1
    elif op == "bl":
        cnt["bl"] += 1
print("whole lines=%d ld=%d st=%d cbr=%d mov=%d bl=%d"
      % (n, cnt["ld"], cnt["st"], cnt["cbr"], cnt["mov"], cnt["bl"]))
