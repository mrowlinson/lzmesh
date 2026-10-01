import random, subprocess, sys

ORACLE = sys.argv[1]
CORP = sys.argv[2]  # dir with mixed-128k.bin text-256k.bin zeros-64k.bin
OUT = sys.argv[3]

def gen(shape, n, seed):
    r = random.Random(seed)
    if shape == "sparse":
        b = bytearray(n)
        for i in r.sample(range(n), 5):
            b[i] = r.randrange(1, 256)
        return bytes(b)
    if shape == "alphabet":
        return bytes((i * 37 + seed) % 251 + 1 for i in range(n))
    alpha = b"etaoin shrdluETAOIN0123456789.,"
    return bytes(r.choice(alpha) for _ in range(n))

vecs = [
    ("zeros512", open(CORP + "/zeros-64k.bin", "rb").read()[:512]),
    ("text512", open(CORP + "/text-256k.bin", "rb").read()[:512]),
    ("mixed512", open(CORP + "/mixed-128k.bin", "rb").read()[:512]),
    ("sparse60", gen("sparse", 60, 7)),
    ("alpha60", gen("alphabet", 60, 7)),
    ("text60", gen("textlike", 60, 7)),
]
import hashlib
h = hashlib.sha256()
n = 0
with open(OUT, "wb") as f:
    for name, src in vecs:
        for lv in ("e00", "e01", "e05", "e09"):
            p = subprocess.run([ORACLE, "enc", lv], input=src,
                               capture_output=True)
            f.write(b"%s %s rc=%d len=%d\n" % (name.encode(), lv.encode(),
                                               p.returncode, len(p.stdout)))
            f.write(p.stdout)
            h.update(p.stdout)
            n += 1
print("vecs=%d sha=%s" % (n, h.hexdigest()))
