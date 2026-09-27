#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Stage-6 divergence battery framework — black-box, format-agnostic.

Compares two byte-pipe CLIs (Apple oracle vs port) over a seeded corpus.
Treats both sides as opaque byte functions: stdin bytes in, stdout bytes
out, exit code out. NO codec logic, NO format constants, NO tag parsing.

Adapter contract (both CLIs):
    <cmd> enc <selector-hex>   # stdin: raw input, stdout: encoded bytes
    <cmd> dec <selector-hex>   # stdin: encoded bytes, stdout: decoded bytes
    exit 0  -> success, stdout holds output bytes
    exit 10 -> codec returned 0 (refused), stdout ignored
    else    -> harness/adapter failure (counts against the SIDE, not the test)
    dec ops get DECODE_SIZE env = expected output length + slack.

Cell matrix per (seed, input, selector):
    ENC      oracle_enc(input) vs port_enc(input)          # byte-identity, encode
    DEC_A    oracle_dec(A) vs port_dec(A), A=oracle_enc    # decode Apple bytes
    DEC_P    oracle_dec(P) vs port_dec(P), P=port_enc      # decode port bytes
    ROUND    port round-trip == input; oracle round-trip  # sanity + self-consistency
    DETERM   enc twice per side -> identical               # determinism

Usage:
    battery.py --oracle ./oracle_probe --port ./port_cli --out results/a
    battery.py --seeds 77 --tier full ...
    battery.py --selftest   # loopback adapters, proves framework with zero codec
"""

import argparse
import hashlib
import json
import os
import random
import subprocess
import sys
import time

EXIT_REFUSED = 10          # codec returned 0 (clean refusal, not a crash)
DEFAULT_TIMEOUT = 60       # seconds per single CLI invocation
DEFAULT_SEEDS = 77

# Size classes. Dense small sweep (catches degenerate + threshold behaviour
# without naming any threshold), powers-of-two +/-1, then larger tiers.
SIZES_SMALL = list(range(0, 65))
SIZES_MID = [65, 100, 127, 128, 129, 255, 256, 257, 511, 512, 513,
             1000, 1024, 1025, 4095, 4096, 4097]
SIZES_LARGE = [9999, 16384, 32768, 65535, 65536, 65537, 100000, 262144]

PATTERNS = ("run", "period", "alphabet", "random", "textlike", "sparse")

# F6 E2-class coverage (LANE-E2 / HINT-SPARSEPOS-R1 fact1): deterministic
# single-outlier vectors. Additive to the full tier only; the smoke tier is
# untouched (gate stays 1232 cells) and the legacy full-tier rotation is
# untouched (same seed yields same legacy bytes in same order; F6 vectors
# append after and consume no rng state).
E2_FIXED = ((27, 12), (31, 16), (31, 17), (31, 18), (31, 19),
            (33, 18), (33, 19), (34, 19))
E2_SWEEP_NS = tuple(range(16, 41))
E2_SPARSEPOS_BG = (0x00, 0xFF, 0x42)
E2_SPARSEPOS_POX = (0x01, 0x41, 0xFF, 0x7F)

# --- divergence taxonomy: every bucket is observable-only -------------------
# FAIL buckets (block ship, threshold 0 — see VERDICT.md):
#   ENC_DIFF        both encoded, bytes differ
#   ENC_ZERO_ASYM   exactly one side refused to encode
#   DEC_DIFF        both decoded, bytes differ (on Apple bytes or port bytes)
#   DEC_ZERO_ASYM   exactly one side refused to decode
#   CROSS_FAIL      decoded output != original input (wrong bytes, right size or not)
#   ROUNDTRIP_FAIL  a side does not round-trip its own output
#   ORACLE_BAD      Apple itself failed to round-trip (invalidates battery, not port)
#   NONDET          same side+input encoded twice -> different bytes
#   CRASH           signal / traceback / harness error on either side
#   TIMEOUT         per-invocation timeout exceeded
# PASS: IDENTICAL (bytes equal, both succeeded, round-trips hold).

FAIL_BUCKETS = {"ENC_DIFF", "ENC_ZERO_ASYM", "DEC_DIFF", "DEC_ZERO_ASYM",
                "CROSS_FAIL", "ROUNDTRIP_FAIL", "ORACLE_BAD", "NONDET",
                "CRASH", "TIMEOUT"}


class Side:
    """One byte-pipe CLI under test."""

    def __init__(self, argv, timeout=DEFAULT_TIMEOUT):
        self.argv = argv
        self.timeout = timeout

    def run(self, op, selector, data, dec_size=None):
        """Returns (status, out_bytes). status in {ok, refused, crash, timeout}."""
        cmd = self.argv + [op, format(selector, "x")]
        env = None
        if op == "dec":
            # Buffer API needs destination capacity up front. Expected output
            # is the original input length; +slack so over-long decodes are
            # observable (and still fail comparison) instead of truncated.
            env = dict(os.environ, DECODE_SIZE=str((dec_size or 0) + 1024))
        try:
            p = subprocess.run(cmd, input=data, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=self.timeout,
                               env=env)
        except subprocess.TimeoutExpired:
            return ("timeout", b"")
        if p.returncode == 0:
            return ("ok", p.stdout)
        if p.returncode == EXIT_REFUSED:
            return ("refused", b"")
        return ("crash", p.stderr[:512])


class LoopbackSide:
    """Self-test adapter: enc prepends marker, dec strips it. Zero codec."""

    def __init__(self, tag=b"LB", sabotage=False):
        self.tag = tag
        self.sabotage = sabotage
        self.calls = 0

    def run(self, op, selector, data, dec_size=None):
        self.calls += 1
        if op == "enc":
            out = self.tag + data
            if self.sabotage and self.calls % 2 == 0:
                out += b"!"
            return ("ok", out)
        if data.startswith(self.tag):
            return ("ok", data[len(self.tag):])
        return ("refused", b"")


# --- seeded corpus ------------------------------------------------------------

def gen_input(rng, size, pattern):
    if size == 0:
        return b""
    if pattern == "run":
        return bytes([rng.randrange(256)]) * size
    if pattern == "period":
        p = rng.choice([1, 2, 3, 4, 8, 16, 64])
        unit = bytes(rng.randrange(256) for _ in range(p))
        return (unit * ((size // p) + 1))[:size]
    if pattern == "alphabet":
        k = rng.choice([2, 4, 16, 256])
        alpha = bytes(rng.randrange(256) for _ in range(k))
        return bytes(rng.choice(alpha) for _ in range(size))
    if pattern == "random":
        return bytes(rng.randrange(256) for _ in range(size))
    if pattern == "textlike":
        words = ["the", "mesh", "block", "token", "stream", "Apple", "codec",
                 "battery", "seed", "diverge", "bytes", "encode", "decode"]
        out = bytearray()
        while len(out) < size:
            out += rng.choice(words).encode() + b" "
        return bytes(out[:size])
    if pattern == "sparse":
        b = bytearray(size)
        for _ in range(max(1, size // 64)):
            b[rng.randrange(size)] = rng.randrange(1, 256)
        return bytes(b)
    raise ValueError(pattern)


def gen_single_pox(size, pos, bg=0x00, pox=0x41):
    """One outlier at pos over a flat background. Closed form, no rng."""
    b = bytearray([bg]) * size
    b[pos] = pox
    return bytes(b)


def corpus_for_seed(seed, tier):
    """Deterministic input list for one seed. Yields (input_id, bytes)."""
    rng = random.Random(seed)
    if tier == "smoke":
        sizes = [0, 1, 2, 9, 21, 22, 64, 256, 4096]
        patterns = ("run", "random")
    elif tier == "full":
        sizes = SIZES_SMALL + SIZES_MID + SIZES_LARGE
        patterns = PATTERNS
    else:
        raise ValueError(tier)
    # Per (size) pick 1-2 patterns deterministically: keeps full tier bounded
    # (~77 * ~90 sizes * ~1.3 patterns ~= 9k inputs) while covering all
    # patterns at every seed via rotation.
    for i, size in enumerate(sizes):
        npat = 2 if size < 65 else 1
        for j in range(npat):
            pat = patterns[(i + j + seed) % len(patterns)]
            yield (f"s{seed:02d}-n{size}-{pat}", gen_input(rng, size, pat))
    if tier == "full":
        # F6-1 singlepox: the 8 E2-fixed cells at memo values (bg00/pox41).
        # Every seed: gates every run, including single-seed slices.
        for (n, pos) in E2_FIXED:
            yield (f"s{seed:02d}-n{n}p{pos}-singlepox",
                   gen_single_pox(n, pos))
        # F6-2 sparsepos: per-seed rotating (n,pos,bg,pox) slice over the
        # memo n16-40 range. Full positional sweeps emerge across seeds;
        # 25 vectors/seed keep the tier bounded (+33/seed total).
        for k, n in enumerate(E2_SWEEP_NS):
            pos = (seed * 7 + k * 13) % n
            bg = E2_SPARSEPOS_BG[(seed + k) % len(E2_SPARSEPOS_BG)]
            pox = E2_SPARSEPOS_POX[(seed * 3 + k) % len(E2_SPARSEPOS_POX)]
            if pox == bg:
                pox = 0x41 if bg != 0x41 else 0x01
            yield (f"s{seed:02d}-n{n}p{pos}-sparsepos",
                   gen_single_pox(n, pos, bg, pox))


# --- cell execution -----------------------------------------------------------

def sha(b):
    return hashlib.sha256(b).hexdigest()[:16]


def run_cell(oracle, port, selector, input_id, data):
    """Run full 5-check matrix for one input. Returns list of finding dicts."""
    F = []
    ctx = {"input": input_id, "size": len(data), "selector": hex(selector)}

    o_enc = oracle.run("enc", selector, data)
    p_enc = port.run("enc", selector, data)

    for tag, res in (("oracle", o_enc), ("port", p_enc)):
        if res[0] == "timeout":
            F.append(ctx | {"check": "ENC", "bucket": "TIMEOUT", "side": tag})
        elif res[0] == "crash":
            F.append(ctx | {"check": "ENC", "bucket": "CRASH", "side": tag,
                            "detail": res[1][:200].decode("utf8", "replace")})
    if F and any(f["bucket"] in ("CRASH", "TIMEOUT") for f in F):
        return F  # no further checks meaningful

    if o_enc[0] == "refused" and p_enc[0] == "refused":
        return F  # both refused: agreed behaviour, nothing to classify
    if (o_enc[0] == "refused") != (p_enc[0] == "refused"):
        F.append(ctx | {"check": "ENC", "bucket": "ENC_ZERO_ASYM",
                        "oracle": o_enc[0], "port": p_enc[0]})
        return F
    if sha(o_enc[1]) != sha(p_enc[1]):
        F.append(ctx | {"check": "ENC", "bucket": "ENC_DIFF",
                        "o_len": len(o_enc[1]), "p_len": len(p_enc[1]),
                        "o_sha": sha(o_enc[1]), "p_sha": sha(p_enc[1])})

    # Decode both encoded forms with both sides.
    for label, blob in (("DEC_A", o_enc[1]), ("DEC_P", p_enc[1])):
        o_dec = oracle.run("dec", selector, blob, dec_size=len(data))
        p_dec = port.run("dec", selector, blob, dec_size=len(data))
        for tag, res in (("oracle", o_dec), ("port", p_dec)):
            if res[0] == "timeout":
                F.append(ctx | {"check": label, "bucket": "TIMEOUT", "side": tag})
            elif res[0] == "crash":
                F.append(ctx | {"check": label, "bucket": "CRASH", "side": tag})
        if any(f.get("check") == label and f["bucket"] in ("CRASH", "TIMEOUT")
               for f in F):
            continue
        if (o_dec[0] == "refused") != (p_dec[0] == "refused"):
            F.append(ctx | {"check": label, "bucket": "DEC_ZERO_ASYM",
                            "oracle": o_dec[0], "port": p_dec[0]})
            continue
        if o_dec[0] == "refused":
            continue
        if sha(o_dec[1]) != sha(p_dec[1]):
            F.append(ctx | {"check": label, "bucket": "DEC_DIFF",
                            "o_len": len(o_dec[1]), "p_len": len(p_dec[1]),
                            "o_sha": sha(o_dec[1]), "p_sha": sha(p_dec[1])})
        # Cross-check vs original input.
        if o_dec[1] != data and label == "DEC_A":
            F.append(ctx | {"check": label, "bucket": "ORACLE_BAD",
                            "got_len": len(o_dec[1])})
        if p_dec[1] != data:
            F.append(ctx | {"check": label, "bucket": "CROSS_FAIL",
                            "side": "port", "got_len": len(p_dec[1])})
        if o_dec[1] != data and label == "DEC_P":
            # Apple cannot decode port output: port bytes are off-spec
            # (or oracle quirk — either way a divergence to triage).
            F.append(ctx | {"check": label, "bucket": "CROSS_FAIL",
                            "side": "oracle-on-port-bytes",
                            "got_len": len(o_dec[1])})

    # Round-trip self-consistency per side (encode own, decode own).
    for tag, side, enc in (("oracle", oracle, o_enc), ("port", port, p_enc)):
        dec = side.run("dec", selector, enc[1], dec_size=len(data))
        if dec[0] == "ok" and dec[1] != data:
            F.append(ctx | {"check": "ROUND", "bucket":
                            "ORACLE_BAD" if tag == "oracle" else "ROUNDTRIP_FAIL",
                            "side": tag})

    # Determinism: re-encode, must be byte-identical.
    o_enc2 = oracle.run("enc", selector, data)
    p_enc2 = port.run("enc", selector, data)
    if o_enc2[0] == "ok" and o_enc2[1] != o_enc[1]:
        F.append(ctx | {"check": "DETERM", "bucket": "NONDET", "side": "oracle"})
    if p_enc2[0] == "ok" and p_enc2[1] != p_enc[1]:
        F.append(ctx | {"check": "DETERM", "bucket": "NONDET", "side": "port"})

    return F


# --- driver -------------------------------------------------------------------

def run_battery(oracle, port, seeds, selectors, tier, out_dir, save_vectors=True):
    os.makedirs(out_dir, exist_ok=True)
    vec_dir = os.path.join(out_dir, "vectors")
    if save_vectors:
        os.makedirs(vec_dir, exist_ok=True)
    findings_path = os.path.join(out_dir, "findings.jsonl")
    counts = {}
    cells = 0
    t0 = time.time()
    with open(findings_path, "w") as fh:
        for seed in seeds:
            for input_id, data in corpus_for_seed(seed, tier):
                for sel in selectors:
                    cells += 1
                    found = run_cell(oracle, port, sel, input_id, data)
                    for f in found:
                        counts[f["bucket"]] = counts.get(f["bucket"], 0) + 1
                        fh.write(json.dumps(f) + "\n")
                        fh.flush()
                        if save_vectors:
                            with open(os.path.join(
                                    vec_dir, f"{input_id}-{sel:#x}.bin"),
                                    "wb") as vf:
                                vf.write(data)
    fails = sum(n for b, n in counts.items() if b in FAIL_BUCKETS)
    summary = {"cells": cells, "fail_cells": fails, "buckets": counts,
               "seconds": round(time.time() - t0, 1), "tier": tier,
               "seeds": [seeds[0], seeds[-1], len(seeds)],
               "selectors": [hex(s) for s in selectors],
               "verdict": "PASS" if fails == 0 else "FAIL"}
    with open(os.path.join(out_dir, "summary.json"), "w") as fh:
        json.dump(summary, fh, indent=2)
    return summary


def selftest():
    print("selftest 1: loopback vs loopback (expect PASS)...")
    s = run_battery(LoopbackSide(), LoopbackSide(), seeds=list(range(77)),
                    selectors=[0xE05], tier="smoke", out_dir="tmp-selftest-pass",
                    save_vectors=False)
    assert s["verdict"] == "PASS", s
    print("  PASS cells=%d" % s["cells"])
    print("selftest 2: loopback vs sabotaged loopback (expect FAIL with ENC_DIFF)...")
    s = run_battery(LoopbackSide(), LoopbackSide(sabotage=True), seeds=[0],
                    selectors=[0xE05], tier="smoke", out_dir="tmp-selftest-fail",
                    save_vectors=False)
    assert s["verdict"] == "FAIL" and s["buckets"].get("ENC_DIFF"), s
    print("  FAIL as expected buckets=%s" % s["buckets"])
    print("selftest OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oracle", nargs="+", help="oracle CLI argv")
    ap.add_argument("--port", nargs="+", help="port CLI argv")
    ap.add_argument("--out", default="results")
    ap.add_argument("--seeds", type=int, default=DEFAULT_SEEDS)
    ap.add_argument("--seed-offset", type=int, default=0)
    ap.add_argument("--selectors", default="e05",
                    help="comma hex list, e.g. e00,e01,e05,e09")
    ap.add_argument("--tier", default="full", choices=("smoke", "full"))
    ap.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.oracle or not a.port:
        ap.error("--oracle and --port required (or --selftest)")
    seeds = list(range(a.seed_offset, a.seed_offset + a.seeds))
    selectors = [int(x, 16) for x in a.selectors.split(",")]
    summary = run_battery(Side(a.oracle, a.timeout), Side(a.port, a.timeout),
                          seeds, selectors, a.tier, a.out)
    print(json.dumps(summary, indent=2))
    return 0 if summary["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
