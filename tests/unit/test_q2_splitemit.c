/* SPDX-License-Identifier: 0BSD */
/*
 * test_q2_splitemit.c — GEN multi per-block HUF emission (Q2 SPLITEMIT).
 *
 * H3 multi with agreed parse emitted RAW-only per block (modes 0);
 * oracle Huffman-codes each block (G1/H5 tables, H3 framing). Q2
 * upgrades COMP-RAW blocks to COMP-HUF (keep/parse untouched).
 * Pins: synthetic textlike multis (2 seeds x 2 sizes, e05) emit
 * >=2 COMP blocks, each with >=1 HUF stream, roundtrip IDENT.
 * Base (pre-Q2) fails the HUF check (modes all RAW/REPEAT).
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

/* Textlike bytes: word repeats give dense short takes + multi. */
static void gen_textlike(uint8_t *out, size_t n, uint32_t seed)
{
	static const char *WORDS[] = { "the", "mesh", "block", "token",
		"stream", "Apple", "codec", "battery", "seed",
		"diverge", "bytes", "encode", "decode" };
	static const size_t NW =
		sizeof(WORDS) / sizeof(WORDS[0]);
	size_t pos = 0u;
	size_t i;
	g_x = seed;
	while (pos < n) {
		const char *w = WORDS[xnext() % NW];
		for (i = 0u; w[i] != '\0' && pos < n; i++)
			out[pos++] = (uint8_t)w[i];
		if (pos < n)
			out[pos++] = (uint8_t)' ';
	}
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

static void check_multi_huf(uint32_t seed, size_t n)
{
	char name[96], detail[192];
	uint8_t *z, *enc, *dec;
	size_t eret, ds, dret, p;
	unsigned ncomp = 0u, nhuf = 0u;

	snprintf(name, sizeof(name), "splitemit-huf seed=%08x n=%lu",
		 seed, (unsigned long)n);
	z = malloc(n);
	enc = malloc(n + 1024);
	dec = malloc(n + 64);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	gen_textlike(z, n, seed);
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, 0xE05);
	if (eret == 0 || eret > n + 1024) {
		snprintf(detail, sizeof(detail), "encode returned %lu",
			 (unsigned long)eret);
		t_fail(name, detail);
		goto out;
	}
	p = 0u;
	while (p < eret) {
		uint8_t tag = enc[p];
		if (tag == 0xFF) {
			p += 1u;
			break;
		} else if (tag == 0x00) {
			uint32_t bds;
			if (p + 5u > eret) {
				t_fail(name, "RAW trunc");
				goto out;
			}
			bds = rd32le(enc + p + 1);
			p += 5u + (size_t)bds;
		} else if (tag == 0x01) {
			uint32_t bds, bo, fo, modes;
			unsigned ml, mt, mn, md;
			if (p + 9u > eret) {
				t_fail(name, "COMP hdr trunc");
				goto out;
			}
			bds = rd32le(enc + p + 1);
			bo = rd16le(enc + p + 5);
			fo = rd16le(enc + p + 7);
			if (p + (size_t)fo + 10u > eret) {
				t_fail(name, "COMP trunc");
				goto out;
			}
			modes = rd16le(enc + p + fo);
			ml = modes & 7u;
			mt = (modes >> 3) & 7u;
			mn = (modes >> 6) & 7u;
			md = (modes >> 9) & 7u;
			(void)bds;
			(void)bo;
			ncomp++;
			if (ml == 2u || mt == 2u || mn == 2u
			    || md == 2u)
				nhuf++;
			p += (size_t)fo + 10u;
		} else {
			snprintf(detail, sizeof(detail), "tag=%02x",
				 tag);
			t_fail(name, detail);
			goto out;
		}
	}
	if (p != eret || enc[eret - 1] != 0xFF) {
		t_fail(name, "END marker");
		goto out;
	}
	if (ncomp < 2u) {
		snprintf(detail, sizeof(detail), "ncomp=%u want >=2",
			 ncomp);
		t_fail(name, detail);
		goto out;
	}
	if (nhuf != ncomp) {
		snprintf(detail, sizeof(detail),
			 "hufblocks=%u/%u (RAW-only?)", nhuf, ncomp);
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

int main(void)
{
	static const uint32_t SEEDS[] = { 0xDEADBEEFu, 0x11111111u };
	static const size_t SIZES[] = { 32768u, 65537u };
	size_t i, j;

	for (i = 0u; i < sizeof(SEEDS) / sizeof(SEEDS[0]); i++) {
		for (j = 0u; j < sizeof(SIZES) / sizeof(SIZES[0]);
		     j++)
			check_multi_huf(SEEDS[i], SIZES[j]);
	}
	printf("---\nq2-splitemit: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
