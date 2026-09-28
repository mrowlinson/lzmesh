#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""mkcorpus.py — generate the pinned bench corpus deterministically.

Usage: python3 mkcorpus.py [outdir]   (default: corpus/ next to this script)

Every file is byte-deterministic from its (seed, size, kind): fixed PRNG
seeds, no timestamps, no platform dependence. Recorded sha256 pins the
corpus; PERF.md quotes the hashes. Regenerating must reproduce identical
bytes (verified by --check).
"""
import hashlib
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# (name, size, kind, seed)
FILES = [
    ("text-256k.bin", 262144, "text", 11),
    ("mixed-128k.bin", 131072, "mixed", 22),
    ("zeros-64k.bin", 65536, "zeros", 33),
]

WORDS = [
    b"the", b"be", b"to", b"of", b"and", b"a", b"in", b"that", b"have",
    b"it", b"for", b"not", b"on", b"with", b"he", b"as", b"you", b"do",
    b"at", b"this", b"but", b"his", b"by", b"from", b"they", b"we",
    b"say", b"her", b"she", b"or", b"an", b"will", b"my", b"one",
    b"all", b"would", b"there", b"their", b"what", b"so", b"up",
    b"out", b"if", b"about", b"who", b"get", b"which", b"go", b"me",
    b"compress", b"encode", b"decode", b"buffer", b"level", b"block",
    b"entropy", b"literal", b"match", b"window", b"scratch", b"framing",
]


def gen_text(size, seed):
    rng = random.Random(seed)
    out = bytearray()
    while len(out) < size:
        w = rng.choice(WORDS)
        out += w
        r = rng.random()
        if r < 0.12:
            out += b"\n"
        elif r < 0.16:
            out += b". "
        else:
            out += b" "
    return bytes(out[:size])


def gen_mixed(size, seed):
    rng = random.Random(seed)
    out = bytearray()
    while len(out) < size:
        pick = rng.random()
        if pick < 0.35:  # text run
            n = rng.randint(16, 512)
            out += gen_text(n, rng.randint(0, 2 ** 30))
        elif pick < 0.55:  # zero / byte run
            n = rng.randint(8, 1024)
            out += bytes([rng.choice([0, 0, 0, 0xFF, 0x41])]) * n
        elif pick < 0.8:  # low-entropy structured binary
            n = rng.randint(16, 256)
            base = rng.randint(0, 255)
            out += bytes([(base + i * 7) & 0xFF for i in range(n)])
        else:  # incompressible tail
            n = rng.randint(16, 256)
            out += bytes(rng.getrandbits(8) for _ in range(n))
    return bytes(out[:size])


def gen_zeros(size, seed):
    return bytes(size)


GENS = {"text": gen_text, "mixed": gen_mixed, "zeros": gen_zeros}


def build(outdir):
    os.makedirs(outdir, exist_ok=True)
    rows = []
    for name, size, kind, seed in FILES:
        data = GENS[kind](size, seed)
        assert len(data) == size, name
        path = os.path.join(outdir, name)
        with open(path, "wb") as f:
            f.write(data)
        digest = hashlib.sha256(data).hexdigest()
        rows.append((name, size, digest))
        print("%s %d %s" % (name, size, digest))
    return rows


def main(argv):
    outdir = argv[1] if len(argv) > 1 and argv[1] != "--check" else os.path.join(HERE, "corpus")
    check = "--check" in argv
    if check:
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            rows = build(tmp)
            ok = True
            for name, size, digest in rows:
                path = os.path.join(outdir, name)
                with open(path, "rb") as f:
                    actual = hashlib.sha256(f.read()).hexdigest()
                match = actual == digest
                ok = ok and match
                print("%s %s" % (name, "OK" if match else "MISMATCH " + actual))
            sys.exit(0 if ok else 1)
    build(outdir)


if __name__ == "__main__":
    main(sys.argv)
