#!/usr/bin/env python3
# failparity11.py (Air-batch copy) -- truncated/corrupt bytes must fail
# (or decode) IDENTICALLY on base vs variant (same rc + same bytes).
# Usage: python3 failparity11.py <base-cli> <var-cli> <out-tsv> <slack>
# slack: 1024 (matrix shape) or 0 (exact cap; exercises EQ leg incl FAIL
# precedence around the cap-stop).
import os
import subprocess
import sys

BASE = sys.argv[1]
VAR = sys.argv[2]
OUT = sys.argv[3]
SLACK = int(sys.argv[4])
R = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 '..', '..', '..', '..'))
CORP = os.path.join(R, 'bench', 'corpus')

CORPORA = {'text-256k': 262144, 'mixed-128k': 131072, 'zeros-64k': 65536}
LEVELS = ['e00', 'e01', 'e05', 'e09']


def run(cli, op, sel, data, dec_size):
    env = dict(os.environ, DECODE_SIZE=str(dec_size + SLACK))
    p = subprocess.run([cli, op, sel], input=data, stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, timeout=60, env=env)
    return p.returncode, p.stdout


def trunc_points(n):
    pts = {0, 1, 2, 3, 4, 5, 8, 9, 10, 16, 32, 64}
    pts.update(n - d for d in (0, 1, 2, 3, 4, 5, 8, 16, 32) if n - d >= 0)
    step = max(n // 24, 1)
    pts.update(range(0, n + 1, step))
    return sorted(p for p in pts if 0 <= p <= n)


mismatch = 0
total = 0
with open(OUT, 'w') as fh:
    fh.write('corpus\tlevel\tnbytes\tcut\tbase_rc\tvar_rc\tout_equal\n')
    for c, n in CORPORA.items():
        raw = open(os.path.join(CORP, '%s.bin' % c), 'rb').read()
        for lv in LEVELS:
            p = subprocess.run([BASE, 'enc', lv], input=raw,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               timeout=120)
            if p.returncode != 0:
                fh.write('%s\t%s\t0\t-\tENC_RC=%s\t-\t-\n'
                         % (c, lv, p.returncode))
                continue
            blob = p.stdout
            for cut in trunc_points(len(blob)):
                total += 1
                b_rc, b_out = run(BASE, 'dec', lv, blob[:cut], n)
                v_rc, v_out = run(VAR, 'dec', lv, blob[:cut], n)
                eq = (b_rc == v_rc) and (b_out == v_out)
                if not eq:
                    mismatch += 1
                fh.write('%s\t%s\t%d\t%d\t%s\t%s\t%s\n'
                         % (c, lv, len(blob), cut, b_rc, v_rc,
                            '1' if eq else '0'))
print('failparity: total=%d mismatch=%d tsv=%s' % (total, mismatch, OUT))
sys.exit(1 if mismatch else 0)
