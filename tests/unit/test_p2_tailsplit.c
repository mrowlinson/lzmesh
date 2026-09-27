/* SPDX-License-Identifier: 0BSD */
/*
 * test_p2_tailsplit.c — GEN take-free lit-close end edges (P2 TAILSPLIT).
 *
 * HINT-TAILSPLIT-R1 sec3/5: take-free GEN blk0 closes at ds 16385
 * (B0+1 pre-emit); lit-close end rule absorbs rem<=17 (blk0).
 * Pins: n=16393/16394/16402 single COMP ds=n (rem 8/9/17 absorb),
 * n=16403 COMP 16385 + RAW 18 (rem 18 splits). Vectors verified
 * oracle byte-identical (tmp/p2/genprobe2.py) and take-free
 * (footer tok==1/lit==ds/dist==0); base port splits n>=16394.
 *
 * Public API only (lzmesh.h). Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

static int g_pass, g_fail;

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

static uint32_t g_x;

static uint32_t xnext(void)
{
	uint32_t x = g_x;
	x ^= (x << 13);
	x ^= (x >> 17);
	x ^= (x << 5);
	g_x = x;
	return x;
}

/* Skewed alpha256 bytes: 256-slot alphabet, ~159 distinct values. */
static void gen_takefree(uint8_t *out, size_t n, uint32_t seed)
{
	uint8_t alpha[256];
	size_t i;
	g_x = seed;
	for (i = 0u; i < 256u; i++)
		alpha[i] = (uint8_t)(xnext() & 0xffu);
	for (i = 0u; i < n; i++)
		out[i] = alpha[xnext() % 256u];
}

static uint32_t rd32le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t rd16le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static void check_single(uint32_t seed, size_t n)
{
	char name[96], detail[192];
	uint8_t *z, *enc, *dec;
	size_t eret, ds, dret;
	uint32_t fo, tok, lit, dist;

	snprintf(name, sizeof(name), "litclose-single seed=%08x n=%lu",
		 seed, (unsigned long)n);
	z = malloc(n);
	enc = malloc(n + 1024);
	dec = malloc(n + 64);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	gen_takefree(z, n, seed);
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, 0xE05);
	if (eret == 0 || eret > n + 1024) {
		snprintf(detail, sizeof(detail), "encode returned %lu",
			 (unsigned long)eret);
		t_fail(name, detail);
		goto out;
	}
	if (enc[0] != 0x01) {
		snprintf(detail, sizeof(detail), "tag=%02x want COMP",
			 enc[0]);
		t_fail(name, detail);
		goto out;
	}
	if (rd32le(enc + 1) != (uint32_t)n) {
		snprintf(detail, sizeof(detail), "ds=%lu want %lu",
			 (unsigned long)rd32le(enc + 1), (unsigned long)n);
		t_fail(name, detail);
		goto out;
	}
	fo = rd16le(enc + 7);
	if ((size_t)fo + 10u + 1u != eret || enc[eret - 1] != 0xFF) {
		snprintf(detail, sizeof(detail),
			 "framing eret=%lu fo=%lu", (unsigned long)eret,
			 (unsigned long)fo);
		t_fail(name, detail);
		goto out;
	}
	tok = rd16le(enc + fo + 2);
	lit = rd16le(enc + fo + 6);
	dist = rd16le(enc + fo + 8);
	if (tok != 1u || lit != (uint32_t)n || dist != 0u) {
		snprintf(detail, sizeof(detail),
			 "footer tok=%lu lit=%lu dist=%lu (take-free?)",
			 (unsigned long)tok, (unsigned long)lit,
			 (unsigned long)dist);
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	if (ds != n) {
		snprintf(detail, sizeof(detail), "decoded_size=%lu",
			 (unsigned long)ds);
		t_fail(name, detail);
		goto out;
	}
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (dret != n || memcmp(dec, z, n) != 0) {
		t_fail(name, "roundtrip mismatch");
		goto out;
	}
	t_pass(name);
out:
	free(z);
	free(enc);
	free(dec);
}

static void check_split(uint32_t seed, size_t n)
{
	char name[96], detail[192];
	uint8_t *z, *enc, *dec;
	size_t eret, ds, dret, p;
	uint32_t fo;

	snprintf(name, sizeof(name), "litclose-split seed=%08x n=%lu",
		 seed, (unsigned long)n);
	z = malloc(n);
	enc = malloc(n + 1024);
	dec = malloc(n + 64);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	gen_takefree(z, n, seed);
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, 0xE05);
	if (eret == 0 || eret > n + 1024) {
		snprintf(detail, sizeof(detail), "encode returned %lu",
			 (unsigned long)eret);
		t_fail(name, detail);
		goto out;
	}
	if (enc[0] != 0x01 || rd32le(enc + 1) != 16385u) {
		snprintf(detail, sizeof(detail), "blk0 tag=%02x ds=%lu",
			 enc[0], (unsigned long)rd32le(enc + 1));
		t_fail(name, detail);
		goto out;
	}
	fo = rd16le(enc + 7);
	p = (size_t)fo + 10u;
	if (p + 5u > eret || enc[p] != 0x00
	    || rd32le(enc + p + 1) != (uint32_t)(n - 16385u)) {
		snprintf(detail, sizeof(detail), "tail framing at %lu",
			 (unsigned long)p);
		t_fail(name, detail);
		goto out;
	}
	p += 5u + (n - 16385u);
	if (p + 1u != eret || enc[p] != 0xFF) {
		t_fail(name, "END marker");
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	if (ds != n) {
		snprintf(detail, sizeof(detail), "decoded_size=%lu",
			 (unsigned long)ds);
		t_fail(name, detail);
		goto out;
	}
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (dret != n || memcmp(dec, z, n) != 0) {
		t_fail(name, "roundtrip mismatch");
		goto out;
	}
	t_pass(name);
out:
	free(z);
	free(enc);
	free(dec);
}

int main(void)
{
	static const uint32_t SEEDS[] = { 0xDEADBEEFu, 0x11111111u };
	static const size_t SINGLES[] = { 16393u, 16394u, 16402u };
	size_t i, j;

	for (i = 0u; i < sizeof(SEEDS) / sizeof(SEEDS[0]); i++) {
		for (j = 0u; j < sizeof(SINGLES) / sizeof(SINGLES[0]);
		     j++)
			check_single(SEEDS[i], SINGLES[j]);
		check_split(SEEDS[i], 16403u);
	}
	printf("---\np2-tailsplit: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
