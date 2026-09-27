/* SPDX-License-Identifier: 0BSD */
/*
 * test_boundary.c — full-only level boundary + scratch table + API edges.
 *
 * SPEC-v2: S1.2 (exactly {E00,E01,E05,E09}), S1.3/Q20 (full 0xE00-form
 * only at the boundary, full-int match, no masking; bare interior below
 * the strip point), S1.5 (scratch sizes), S1.7 (edge vectors), S4.5
 * (encode has no trunc-cap), App C M1/Q11 (NULL fail-closed, no crash).
 * Round-4 R-E-B1 SPEC-WINS: bare/wide/negative MUST reject (encode 0 +
 * scratch 0, at n=32 and n=0).
 *
 * Public API only (lzmesh.h). Exit 0 iff zero FAILs.
 */
#include <limits.h>
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

static const int LEVELS[] = { 0xE00, 0xE01, 0xE05, 0xE09 };
static const size_t ENCSZ[] = { 323468, 1372044, 1388428, 8728460 };

static void scratch_exact(void)
{
	char detail[128];
	size_t i, ds;

	ds = lzmesh_decode_scratch_size();
	if (ds != 65536) {
		snprintf(detail, sizeof(detail), "decode scratch=%lu want 65536",
			 (unsigned long)ds);
		t_fail("scratch decode", detail);
	} else {
		t_pass("scratch decode");
	}
	for (i = 0; i < sizeof(LEVELS) / sizeof(LEVELS[0]); i++) {
		char name[48];
		size_t s = lzmesh_encode_scratch_size(LEVELS[i]);

		snprintf(name, sizeof(name), "scratch L%x", LEVELS[i]);
		if (s != ENCSZ[i]) {
			snprintf(detail, sizeof(detail), "got %lu want %lu",
				 (unsigned long)s, (unsigned long)ENCSZ[i]);
			t_fail(name, detail);
		} else {
			t_pass(name);
		}
	}
}

static void valid_accept(void)
{
	uint8_t src[32], enc[80];
	char detail[128];
	size_t i;

	memset(src, 0x41, sizeof(src));
	for (i = 0; i < sizeof(LEVELS) / sizeof(LEVELS[0]); i++) {
		char name[48];
		size_t e32, e0;

		snprintf(name, sizeof(name), "valid L%x", LEVELS[i]);
		e32 = lzmesh_encode(enc, sizeof(enc), src, sizeof(src), NULL,
				    LEVELS[i]);
		e0 = lzmesh_encode(enc, sizeof(enc), src, 0, NULL, LEVELS[i]);
		if (e32 == 0 || e0 != 1 || enc[0] != 0xFF) {
			snprintf(detail, sizeof(detail),
				 "enc32=%lu enc0=%lu head=%02x, want >0/1/ff",
				 (unsigned long)e32, (unsigned long)e0,
				 e0 ? enc[0] : 0);
			t_fail(name, detail);
		} else {
			t_pass(name);
		}
	}
}

static void invalid_one(int level)
{
	char name[64], detail[160];
	uint8_t src[32], enc[80];
	size_t esz, eret, eret0;

	memset(src, 0x41, sizeof(src));
	snprintf(name, sizeof(name), "invalid 0x%x", (unsigned int)level);
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

static void invalid_sweep(void)
{
	/* Bare 0..15, full-form E02-E0F rejects, wide, negatives. */
	static const int BAD[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
				   12, 13, 14, 15, 0xE02, 0xE03, 0xE04,
				   0xE06, 0xE07, 0xE08, 0xE0A, 0xE0B,
				   0xE0C, 0xE0D, 0xE0E, 0xE0F, 0x100,
				   0x1E00, 0x10000, 0x1E05, 0x100E05,
				   0xDFF, 0xE10, 0xF00, -1, -5, -256,
				   INT_MIN, INT_MAX, 0x7FFFFFFF };
	size_t i;

	for (i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
		invalid_one(BAD[i]);
	/* 0x80000E05 as 32-bit int (negative); full-int match, no masking. */
	invalid_one((int)0x80000E05L);
}

static void edge_vectors(void)
{
	static const uint8_t A[1] = { 0x41 };
	static const uint8_t WANT[7] = { 0x00, 0x01, 0x00, 0x00, 0x00,
					 0x41, 0xFF };
	static const uint8_t FF[1] = { 0xFF };
	uint8_t enc[32], dec[32];
	char detail[128];
	size_t i;

	for (i = 0; i < sizeof(LEVELS) / sizeof(LEVELS[0]); i++) {
		char name[48];
		size_t e = lzmesh_encode(enc, sizeof(enc), A, 1, NULL,
					 LEVELS[i]);

		snprintf(name, sizeof(name), "enc1 L%x", LEVELS[i]);
		if (e != 7 || memcmp(enc, WANT, 7) != 0) {
			snprintf(detail, sizeof(detail), "outlen=%lu",
				 (unsigned long)e);
			t_fail(name, detail);
		} else {
			t_pass(name);
		}
	}
	if (lzmesh_decode(dec, sizeof(dec), FF, 1, NULL) != 0 ||
	    lzmesh_decoded_size(FF, 1) != 0) {
		t_fail("dec-ff", "dec(ff)!=0 or sizer(ff)!=0");
	} else {
		t_pass("dec-ff");
	}
}

static void null_edges(void)
{
	static const uint8_t FF[1] = { 0xFF };
	uint8_t src[8], enc[8], dec[8];
	size_t r;

	memset(src, 0x41, sizeof(src));
	/* Each MUST return 0 (or the empty encoding) without crashing. */
	r = lzmesh_encode(NULL, 0, src, 1, NULL, 0xE05);
	if (r != 0)
		t_fail("null enc-dst-cap0", "nonzero");
	else
		t_pass("null enc-dst-cap0");
	r = lzmesh_encode(enc, sizeof(enc), NULL, 0, NULL, 0xE05);
	if (r != 1 || enc[0] != 0xFF)
		t_fail("null enc-src-len0", "not 1/ff");
	else
		t_pass("null enc-src-len0");
	r = lzmesh_decode(NULL, 0, FF, 1, NULL);
	if (r != 0)
		t_fail("null dec-dst-cap0", "nonzero");
	else
		t_pass("null dec-dst-cap0");
	r = lzmesh_decode(dec, sizeof(dec), NULL, 0, NULL);
	if (r != 0)
		t_fail("null dec-src-len0", "nonzero");
	else
		t_pass("null dec-src-len0");
	r = lzmesh_decoded_size(NULL, 0);
	if (r != 0)
		t_fail("null sizer-len0", "nonzero");
	else
		t_pass("null sizer-len0");
}

static void caps(void)
{
	uint8_t src[32], enc[80], dec[80];
	char detail[96];
	size_t c, eret, dret;
	int bad = 0;

	memset(src, 0x41, sizeof(src));
	for (c = 1; c <= 10; c++) {
		if (lzmesh_encode(enc, c, src, sizeof(src), NULL, 0xE05) != 0)
			bad = 1;
	}
	if (bad)
		t_fail("enc tiny-caps", "encode accepted cap 1..10");
	else
		t_pass("enc tiny-caps");
	eret = lzmesh_encode(enc, sizeof(enc), src, sizeof(src), NULL, 0xE05);
	dret = lzmesh_decode(dec, sizeof(dec), enc, eret, NULL);
	if (dret != sizeof(src)) {
		snprintf(detail, sizeof(detail), "big-dst dec=%lu want 32",
			 (unsigned long)dret);
		t_fail("dec big-dst", detail);
	} else {
		t_pass("dec big-dst");
	}
}

int main(void)
{
	scratch_exact();
	valid_accept();
	invalid_sweep();
	edge_vectors();
	null_edges();
	caps();
	printf("---\nboundary: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
