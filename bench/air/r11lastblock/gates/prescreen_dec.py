#!/usr/bin/env python3
"""R11-LASTBLOCK FULL/HOLD/FRESH decode prescreen (Air-batch copy).

Bytes-only. For every battery full-tier input: encode once with BASE,
decode the blob with base and new, compare rc + bytes. DIV=0 ==> equal.
Seeds: FULL 0-16 (12784), HOLD 17-20 (3008), FRESH 21-24 (3008).
--capmode exact (cap=len(data), exercises EQ leg) or slack (+1024).
Battery import resolves from the pushed tree (port/tests/battery).
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
# gates/ lives at port/bench/air/r11lastblock/gates/; battery at port/tests/battery
sys.path.insert(0, os.path.normpath(os.path.join(
    HERE, '..', '..', '..', '..', 'tests', 'battery')))
import battery as B  # noqa: E402


def run_cli(cli, op, sel, data, timeout, dec_size=0):
    env = None
    if op == 'dec':
        env = dict(os.environ, DECODE_SIZE=str(dec_size))
    try:
        p = subprocess.run([cli, op, sel], input=data, capture_output=True,
                           timeout=timeout, env=env)
        return p.returncode, p.stdout
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', b''


def one(base, new, sel, iid, data, timeout, capmode):
    brc, blob = run_cli(base, 'enc', sel, data, timeout)
    if brc != 0:
        return None
    cap = len(data) + (0 if capmode == 'exact' else 1024)
    brc2, bout = run_cli(base, 'dec', sel, blob, timeout, cap)
    nrc2, nout = run_cli(new, 'dec', sel, blob, timeout, cap)
    if brc2 != nrc2:
        return (iid, sel, 'RC %s vs %s' % (brc2, nrc2), len(bout), len(nout),
                '', '')
    if brc2 != 0:
        return None
    if bout != nout:
        return (iid, sel, 'DIV', len(bout), len(nout),
                hashlib.sha256(bout).hexdigest()[:16],
                hashlib.sha256(nout).hexdigest()[:16])
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', required=True)
    ap.add_argument('--new', required=True)
    ap.add_argument('--seeds', type=int, default=17)
    ap.add_argument('--seed-offset', type=int, default=0)
    ap.add_argument('--selectors', default='e00,e01,e05,e09')
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--capmode', default='slack', choices=('slack', 'exact'))
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    sels = ['e%02x' % int(x, 16) for x in a.selectors.split(',')]
    cells = []
    for seed in range(a.seed_offset, a.seed_offset + a.seeds):
        for iid, data in B.corpus_for_seed(seed, 'full'):
            for sel in sels:
                cells.append((seed, sel, iid, data))
    t0 = time.time()
    divs, done = [], [0]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        futs = {ex.submit(one, a.base, a.new, sel, iid, data, a.timeout,
                          a.capmode):
                (iid, sel) for _, sel, iid, data in cells}
        for f in concurrent.futures.as_completed(futs):
            done[0] += 1
            if done[0] % 4000 == 0:
                print('... %d/%d div=%d' % (done[0], len(cells), len(divs)),
                      flush=True)
            r = f.result()
            if r:
                divs.append(r)
    with open(os.path.join(a.out, 'div.tsv'), 'w') as fh:
        fh.write('input\tselector\tkind\tbase_len\tnew_len\tbase_sha16\tnew_sha16\n')
        for r in sorted(divs):
            fh.write('\t'.join(str(x) for x in r) + '\n')
    summ = {'cells': len(cells), 'div': len(divs),
            'seconds': round(time.time() - t0, 1),
            'base': a.base, 'new': a.new, 'seeds': a.seeds,
            'seed_offset': a.seed_offset, 'selectors': a.selectors,
            'capmode': a.capmode}
    json.dump(summ, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1)
    print(json.dumps(summ))
    return 1 if divs else 0


if __name__ == '__main__':
    sys.exit(main())
