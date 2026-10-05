#!/usr/bin/env python3
"""Extract one function's asm + count ld/st/cbr/mov/bl (macOS clang -S).
Usage: h3count.py <file.s> <bare-symbol>  -> prints lines ld st cbr mov bl.
Range: '; -- Begin function <sym>' (exact) through matching '; -- End function'.
Calibrated vs memo v-union/enc.s (expect 1455/165/120/109/347/33).
"""
import re
import sys

path, sym = sys.argv[1], sys.argv[2]
lines = open(path).read().splitlines()
start = next(i for i, l in enumerate(lines)
             if re.search(r";[ \t]*-- Begin function " + re.escape(sym) + r"\s*$", l))
end = len(lines)
for i in range(start + 1, len(lines)):
    if re.search(r";[ \t]*-- End function\s*$", lines[i]):
        end = i + 1
        break
body = lines[start + 1:end]
cnt = {"ld": 0, "st": 0, "cbr": 0, "mov": 0, "bl": 0}
for l in body:
    m = re.match(r"^[ \t]+([a-z][a-z0-9.]*)", l)
    if not m:
        continue
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
print("func=%s lines=%d ld=%d st=%d cbr=%d mov=%d bl=%d"
      % (sym, len(body), cnt["ld"], cnt["st"], cnt["cbr"], cnt["mov"], cnt["bl"]))
