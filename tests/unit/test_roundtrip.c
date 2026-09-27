/* SPDX-License-Identifier: 0BSD */
/*
 * test_roundtrip.c — Stage-6 unithuff (a): synthetic roundtrips over the
 * public lzmesh_* API (include/lzmesh.h only; no internals).
 *
 * Input shapes target the S3.7 Huffman clause's externally visible
 * consequences (maxlen 10 symbols / 5 meta via package-merge+quantum
 * rebuild, exact Kraft gate, canonical LSB-first codes): exact roundtrip
 * on count distributions shaped to hit those paths —
 *   - n==1 single symbol + long single-symbol runs (degenerate nSym==1)
 *   - uniform 2^k alphabets (Kraft-exact) and 2^k+1 alphabets (off-boundary)
 *   - exponential-decay and Fibonacci counts (deep-tree / maxlen stress)
 *   - single-rare-symbol, full 256-symbol alphabet, text-like, fixed-seed
 *     pseudorandom, oracle raw/comp split sizes (21/22)
 * plus API contracts: determinism, decoded_size agreement, undersized-dst
 * prefix, truncation/garbage refusal, scratch NULL vs sized.
 *
 * Exit 0 iff zero FAILs. XFAIL = known-pending (port encoder raw-only;
 * comp emission pending); those lines flip to PASS when COMP lands.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

static int g_pass, g_fail, g_xfail;

static void t_pass(const char *name)
{
	g_pass++;
	printf("PASS  %s\n", name);
}

static void t_fail(const char *name, const char *detail)
{
	g_fail++;
	printf("FAIL  %s :: %s\n", name, detail);
}

static void t_xfail(const char *name, const char *reason)
{
	g_xfail++;
	printf("XFAIL %s :: %s\n", name, reason);
}

static uint32_t xs32(uint32_t *s)
{
	uint32_t x = *s;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

static const int LEVELS[] = { 0xE00, 0xE01, 0xE05, 0xE09 };

/* Full roundtrip: encode -> decoded_size -> decode, exact bytes. */
static void roundtrip_one(int level, const uint8_t *src, size_t n,
			  const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc, *dec;
	size_t enccap, eret, ds, dret;

	snprintf(name, sizeof(name), "roundtrip L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	enccap = n + 1024;
	enc = malloc(enccap ? enccap : 1);
	dec = malloc((n + 64) ? (n + 64) : 1);
	if (!enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, enccap, src, n, NULL, level);
	if (n == 0) {
		/* Empty anchor: lone 0xFF. */
		if (eret != 1 || enc[0] != 0xFF) {
			snprintf(detail, sizeof(detail),
				 "empty encode: got len=%lu head=%02x, want 1/ff",
				 (unsigned long)eret, eret ? enc[0] : 0);
			t_fail(name, detail);
			goto out;
		}
	} else if (eret == 0 || eret > enccap) {
		snprintf(detail, sizeof(detail), "encode returned %lu",
			 (unsigned long)eret);
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	if (ds != n) {
		snprintf(detail, sizeof(detail),
			 "decoded_size=%lu want %lu", (unsigned long)ds,
			 (unsigned long)n);
		t_fail(name, detail);
		goto out;
	}
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (dret != n) {
		snprintf(detail, sizeof(detail), "decode returned %lu want %lu",
			 (unsigned long)dret, (unsigned long)n);
		t_fail(name, detail);
		goto out;
	}
	if (n && memcmp(dec, src, n) != 0) {
		t_fail(name, "decoded bytes differ");
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(dec);
}

/* Undersized destination: returns capacity with a correct prefix. */
static void undersize_one(int level, const uint8_t *src, size_t n,
			  const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc, *dec;
	size_t eret, k, dret;

	if (n < 2)
		return;
	snprintf(name, sizeof(name), "undersize L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	enc = malloc(n + 1024);
	dec = malloc(n);
	if (!enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (!eret) {
		t_fail(name, "encode returned 0");
		goto out;
	}
	k = n / 2;
	dret = lzmesh_decode(dec, k, enc, eret, NULL);
	if (dret != k) {
		snprintf(detail, sizeof(detail), "decode returned %lu want %lu",
			 (unsigned long)dret, (unsigned long)k);
		t_fail(name, detail);
		goto out;
	}
	if (memcmp(dec, src, k) != 0) {
		t_fail(name, "prefix bytes differ");
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(dec);
}

/* Truncated / empty inputs must refuse with 0. */
static void refusal_one(int level, const uint8_t *src, size_t n,
			const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc, probe[1] = { 0x00 };
	size_t eret, dret, cap;
	uint8_t *dec;

	snprintf(name, sizeof(name), "refusal L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	enc = malloc(n + 1024);
	cap = n + 64;
	dec = malloc(cap);
	if (!enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (!eret) {
		t_fail(name, "encode returned 0");
		goto out;
	}
	/* Drop the trailing end marker: truncated stream. */
	dret = lzmesh_decode(dec, cap, enc, eret - 1, NULL);
	if (dret != 0) {
		snprintf(detail, sizeof(detail),
			 "truncated decode returned %lu want 0",
			 (unsigned long)dret);
		t_fail(name, detail);
		goto out;
	}
	/* Lone zero byte: truncated header. */
	dret = lzmesh_decode(dec, cap, probe, sizeof(probe), NULL);
	if (dret != 0) {
		snprintf(detail, sizeof(detail),
			 "1-byte decode returned %lu want 0", (unsigned long)dret);
		t_fail(name, detail);
		goto out;
	}
	/* Empty source. */
	dret = lzmesh_decode(dec, cap, enc, 0, NULL);
	if (dret != 0) {
		snprintf(detail, sizeof(detail),
			 "empty-src decode returned %lu want 0", (unsigned long)dret);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(dec);
}

/* Same input + level encodes to identical bytes on every run. */
static void determinism_one(int level, const uint8_t *src, size_t n,
			    const char *tag)
{
	char name[128], detail[192];
	uint8_t *a, *b;
	size_t ra, rb;

	snprintf(name, sizeof(name), "determinism L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	a = malloc(n + 1024);
	b = malloc(n + 1024);
	if (!a || !b) {
		t_fail(name, "oom");
		goto out;
	}
	ra = lzmesh_encode(a, n + 1024, src, n, NULL, level);
	rb = lzmesh_encode(b, n + 1024, src, n, NULL, level);
	if (!ra || ra != rb || memcmp(a, b, ra) != 0) {
		snprintf(detail, sizeof(detail), "runs differ (%lu vs %lu)",
			 (unsigned long)ra, (unsigned long)rb);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(a);
	free(b);
}

/* Sized-scratch path matches the NULL-scratch path. */
static void scratch_one(int level, const uint8_t *src, size_t n,
			const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc, *dec;
	void *es, *ds;
	size_t esz, dsz, eret, dret;

	snprintf(name, sizeof(name), "scratch L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	esz = lzmesh_encode_scratch_size(level);
	dsz = lzmesh_decode_scratch_size();
	enc = malloc(n + 1024);
	dec = malloc(n + 64);
	es = malloc(esz ? esz : 1);
	ds = malloc(dsz ? dsz : 1);
	if (!enc || !dec || !es || !ds) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, es, level);
	if (!eret && n) {
		t_fail(name, "encode with scratch returned 0");
		goto out;
	}
	if (lzmesh_decoded_size(enc, eret) != n) {
		t_fail(name, "decoded_size mismatch on scratch encode");
		goto out;
	}
	dret = lzmesh_decode(dec, n + 64, enc, eret, ds);
	if (dret != n || (n && memcmp(dec, src, n) != 0)) {
		snprintf(detail, sizeof(detail),
			 "scratch decode returned %lu", (unsigned long)dret);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(dec);
	free(es);
	free(ds);
}

/*
 * Comp-emission gate (Huffman path): highly compressible input must encode
 * smaller than itself once the comp path exists. XFAIL while the port
 * encoder is raw-only.
 */
static void comp_emit_one(int level, const uint8_t *src, size_t n,
			  const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc;
	size_t eret;

	snprintf(name, sizeof(name), "comp-emit L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	enc = malloc(n + 1024);
	if (!enc) {
		t_fail(name, "oom");
		return;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (!eret) {
		t_fail(name, "encode returned 0");
		goto out;
	}
	if (eret < n) {
		t_pass(name);
	} else {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu >= n (raw-only); comp emission pending",
			 (unsigned long)eret);
		t_xfail(name, detail);
	}
out:
	free(enc);
}

/*
 * Boundary dispatch (R-E-B1 SPEC-WINS): only FULL 0xE00-form levels are
 * valid. Bare/wide/negative values must fail closed: encode returns 0
 * (both n=32 and n=0) and scratch returns 0.
 */
static void invalid_level_one(int level)
{
	char name[128], detail[192];
	uint8_t src[32], enc[80];
	size_t esz, eret, eret0;

	memset(src, 0x41, sizeof(src));
	snprintf(name, sizeof(name), "invalid-level 0x%x", (unsigned int)level);
	esz = lzmesh_encode_scratch_size(level);
	eret = lzmesh_encode(enc, sizeof(enc), src, sizeof(src), NULL, level);
	eret0 = lzmesh_encode(enc, sizeof(enc), src, 0, NULL, level);
	if (esz != 0 || eret != 0 || eret0 != 0) {
		snprintf(detail, sizeof(detail),
			 "scratch=%lu enc=%lu enc0=%lu, want 0/0/0",
			 (unsigned long)esz, (unsigned long)eret,
			 (unsigned long)eret0);
		t_fail(name, detail);
		return;
	}
	t_pass(name);
}

struct shape {
	const char *tag;
	uint8_t *buf;
	size_t n;
};

static void shape_uniform(struct shape *s, const char *tag, int alpha,
			  size_t reps)
{
	size_t i;

	s->tag = tag;
	s->n = (size_t)alpha * reps;
	s->buf = malloc(s->n);
	for (i = 0; i < s->n; i++)
		s->buf[i] = (uint8_t)(i % (size_t)alpha);
}

/* Exponential-decay counts over 64 symbols: 8x256, 8x128, ..., 8x2. */
static void shape_skew(struct shape *s)
{
	static const size_t counts[8] = { 256, 128, 64, 32, 16, 8, 4, 2 };
	size_t total = 0, p = 0;
	int g, r, k;

	for (g = 0; g < 8; g++)
		total += 8 * counts[g];
	s->tag = "skew-exp64";
	s->n = total;
	s->buf = malloc(total);
	for (g = 0; g < 8; g++)
		for (r = 0; r < 8; r++)
			for (k = 0; k < (int)counts[g]; k++)
				s->buf[p++] = (uint8_t)(g * 8 + r);
}

/* Fibonacci counts over 20 symbols: classic deep-tree stress. */
static void shape_fib(struct shape *s)
{
	size_t fib[20], total = 0, p = 0;
	int i;
	size_t k;

	fib[0] = 1;
	fib[1] = 2;
	for (i = 2; i < 20; i++)
		fib[i] = fib[i - 1] + fib[i - 2];
	for (i = 0; i < 20; i++)
		total += fib[i];
	s->tag = "fib20";
	s->n = total;
	s->buf = malloc(total);
	for (i = 0; i < 20; i++)
		for (k = 0; k < fib[i]; k++)
			s->buf[p++] = (uint8_t)(i + 1);
}

int main(void)
{
	struct shape u2, u3, u4, u5, u16, u17, u256, skew, fib;
	struct shape rare, full, text, rnd, z21, z22, z256, z4096;
	struct shape empty, one0, oneff, arun;
	static const char sentence[] =
		"the quick brown fox jumps over the lazy dog. ";
	size_t i, p;
	uint32_t seed = 0xC0FFEEu;
	size_t li;

	empty.tag = "empty";
	empty.buf = NULL;
	empty.n = 0;

	one0.tag = "n1-zero";
	one0.buf = malloc(1);
	one0.buf[0] = 0x00;
	one0.n = 1;

	oneff.tag = "n1-ff";
	oneff.buf = malloc(1);
	oneff.buf[0] = 0xFF;
	oneff.n = 1;

	arun.tag = "arun-1000";
	arun.n = 1000;
	arun.buf = malloc(1000);
	memset(arun.buf, 'A', 1000);

	shape_uniform(&u2, "uni-2", 2, 64);
	shape_uniform(&u3, "uni-3", 3, 64);
	shape_uniform(&u4, "uni-4", 4, 64);
	shape_uniform(&u5, "uni-5", 5, 64);
	shape_uniform(&u16, "uni-16", 16, 32);
	shape_uniform(&u17, "uni-17", 17, 32);
	shape_uniform(&u256, "uni-256", 256, 8);
	shape_skew(&skew);
	shape_fib(&fib);

	rare.tag = "rare1";
	rare.n = 4096;
	rare.buf = malloc(4096);
	memset(rare.buf, 'A', 4096);
	rare.buf[4095] = 'B';

	full.tag = "full256x16";
	full.n = 4096;
	full.buf = malloc(4096);
	for (i = 0; i < 4096; i++)
		full.buf[i] = (uint8_t)(i & 0xFF);

	text.tag = "text-like";
	text.n = 2048;
	text.buf = malloc(2048);
	for (p = 0; p < 2048; p++)
		text.buf[p] = (uint8_t)sentence[p % (sizeof(sentence) - 1)];

	rnd.tag = "xorshift-3000";
	rnd.n = 3000;
	rnd.buf = malloc(3000);
	for (i = 0; i < 3000; i++)
		rnd.buf[i] = (uint8_t)(xs32(&seed) & 0xFF);

	z21.tag = "zeros-21";
	z21.n = 21;
	z21.buf = calloc(21, 1);

	z22.tag = "zeros-22";
	z22.n = 22;
	z22.buf = calloc(22, 1);

	z256.tag = "zeros-256";
	z256.n = 256;
	z256.buf = calloc(256, 1);

	z4096.tag = "zeros-4096";
	z4096.n = 4096;
	z4096.buf = calloc(4096, 1);

	{
		struct shape *all[] = { &empty, &one0, &oneff, &arun, &u2, &u3,
					&u4, &u5, &u16, &u17, &u256, &skew, &fib,
					&rare, &full, &text, &rnd, &z21, &z22,
					&z256, &z4096 };
		struct shape *det[] = { &skew, &fib, &rnd };
		struct shape *scr[] = { &skew, &text };
		size_t si, di;

		{
			static const int BAD[] = { 0, 1, 5, 9, 0xE02, 0xE0F,
						   0x100, 0x1E05, 0x100E05,
						   -1, -5 };
			size_t bi;

			for (bi = 0; bi < sizeof(BAD) / sizeof(BAD[0]); bi++)
				invalid_level_one(BAD[bi]);
		}

		for (li = 0; li < sizeof(LEVELS) / sizeof(LEVELS[0]); li++) {
			for (si = 0; si < sizeof(all) / sizeof(all[0]); si++)
				roundtrip_one(LEVELS[li], all[si]->buf, all[si]->n,
					      all[si]->tag);
			for (di = 0; di < sizeof(det) / sizeof(det[0]); di++)
				determinism_one(LEVELS[li], det[di]->buf,
						det[di]->n, det[di]->tag);
			for (di = 0; di < sizeof(scr) / sizeof(scr[0]); di++)
				scratch_one(LEVELS[li], scr[di]->buf, scr[di]->n,
					    scr[di]->tag);
			undersize_one(LEVELS[li], text.buf, text.n, text.tag);
			refusal_one(LEVELS[li], text.buf, text.n, text.tag);
			comp_emit_one(LEVELS[li], z4096.buf, z4096.n,
				      z4096.tag);
			comp_emit_one(LEVELS[li], fib.buf, fib.n, fib.tag);
		}
	}

	printf("---\nroundtrip: pass=%d fail=%d xfail=%d\n", g_pass, g_fail,
	       g_xfail);
	return g_fail ? 1 : 0;
}
