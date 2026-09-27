/* SPDX-License-Identifier: 0BSD */
/*
 * test_m21_shapep_suf.c — M21 SHAPE-P-SUF pins (u18, MERGE-M21).
 *
 * SPEC-v2: S3.9/S3.10/S3.11 (token_new, mc->ml=mc+2, dist sb/low/suf),
 * S3.3/S3.6/S6.1 (suffix via lanes, index-at-END), S5.3 (TIER-1 D>fo,
 * TIER-2 outpos<=n keep, Q18 litc!=0), S5.5 (mode_trivial), S5.6
 * (PAD1/B-TAB), S2.5/Q2 + C8-C12, S2.6, S5.1 (determinism), S1.3/Q20
 * (full-form levels).
 * SHAPE-P-SUF (u18): p-periodic 9..32 non-run single-token new-dist +
 * suffix lanes COMP at L1/L5/L9 (lit first-p RAW, tok new-dist, len
 * lc 1/2/6, dist 1B, lane0 1B, index 01 00 00 08, fo=bo+5, footer
 * (1,lc,p,1) + END). Fires only where u12/u15 want==0. Lane Task-4:
 * p9..32 x n100 x e01/e05/e09 70/72 COMP (2 slots-dirty RAW); merge
 * family tile(range(p)) 72/72 COMP + SELF-exact + ORACLE-ACCEPTS,
 * 72/72 byte-ident (p31/e01 take-point delta CLOSED by U1 s06
 * MX-shadow veto: oracle take@32, u37 GEN 61B).
 * Public API only. Exit 0 iff zero FAILs.
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

static const int MLEVELS[] = { 0xE01, 0xE05, 0xE09 };

static uint16_t r16le(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t r32le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void fill_periodic(uint8_t *dst, size_t n, int p)
{
	size_t i;

	for (i = 0; i < n; i++)
		dst[i] = (uint8_t)(i % (size_t)p);
}

static void fill_pat(uint8_t *dst, size_t n, const char *pat, size_t plen)
{
	size_t i;

	for (i = 0; i < n; i++)
		dst[i] = (uint8_t)pat[i % plen];
}

/* COMP with exact u18 layout: bo/fo/total/footer + roundtrip + determinism. */
static void comp_layout_one_lit(int level, int p, size_t n, uint16_t exp_bo,
				uint16_t exp_fo, size_t exp_total,
				uint16_t exp_lc, uint16_t exp_lit)
{
	char name[128], detail[256];
	uint8_t *src = malloc(n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;
	uint32_t dsc;
	uint16_t bo, fo, tokc, lenc, litc, distc;

	snprintf(name, sizeof(name), "psuf-comp L%x p%d n=%lu", level, p,
		 (unsigned long)n);
	if (!src || !enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	fill_periodic(src, n, p);
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret != exp_total || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x, want %lu/01",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)exp_total);
		t_fail(name, detail);
		goto out;
	}
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail(name, "nondeterministic");
		goto out;
	}
	dsc = r32le(enc + 1);
	bo = r16le(enc + 5);
	fo = r16le(enc + 7);
	if (dsc != n || bo != exp_bo || fo != exp_fo) {
		snprintf(detail, sizeof(detail),
			 "ds=%lu bo=%u fo=%u, want %lu/%u/%u",
			 (unsigned long)dsc, bo, fo, (unsigned long)n,
			 exp_bo, exp_fo);
		t_fail(name, detail);
		goto out;
	}
	if (fo != (uint16_t)(bo + 5)) {
		snprintf(detail, sizeof(detail), "fo=%u bo=%u, want fo=bo+5",
			 fo, bo);
		t_fail(name, detail);
		goto out;
	}
	tokc = r16le(enc + fo + 2);
	lenc = r16le(enc + fo + 4);
	litc = r16le(enc + fo + 6);
	distc = r16le(enc + fo + 8);
	if (tokc != 1 || lenc != exp_lc || litc != exp_lit ||
	    distc != 1 || enc[fo + 10] != 0xFF) {
		snprintf(detail, sizeof(detail),
			 "footer tok=%u len=%u lit=%u dist=%u end=%02x, want 1/%u/%u/1/ff",
			 tokc, lenc, litc, distc, enc[fo + 10], exp_lc,
			 exp_lit);
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (ds != n || dr != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(src);
	free(enc);
	free(enc2);
	free(dec);
}

static void comp_layout_one(int level, int p, size_t n, uint16_t exp_bo,
			    uint16_t exp_fo, size_t exp_total, uint16_t exp_lc)
{
	comp_layout_one_lit(level, p, n, exp_bo, exp_fo, exp_total, exp_lc,
			    (uint16_t)p);
}

/* RAW hold pin: tag 00 + outlen n+6 + exact roundtrip. */
static void raw_one(int level, const uint8_t *src, size_t n, const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr;

	snprintf(name, sizeof(name), "psuf-raw L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (eret != n + 6 || enc[0] != 0x00) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want %lu/00",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)(n + 6));
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (ds != n || dr != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(dec);
}

/* COMP pin: tag 01 + exact outlen + exact roundtrip. */
static void comp_one(int level, const uint8_t *src, size_t n,
		     size_t want_len, const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);

	size_t eret, ds, dr;

	snprintf(name, sizeof(name), "psuf-comp L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}

	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (eret != want_len || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want %lu/01",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)want_len);
		t_fail(name, detail);
		goto out;
	}

	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (ds != n || dr != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail(name, detail);
		goto out;
	}

	t_pass(name);
out:
	free(enc);
	free(dec);
}

/* Shape pin: COMP + fo==bo (u12) vs fo==bo+5 (u18) + roundtrip. */
static void shape_fo_one(int level, int p, size_t n, int want_plus5,
			 const char *tag)
{
	char name[128], detail[192];
	uint8_t *src = malloc(n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr;
	uint16_t bo, fo;

	snprintf(name, sizeof(name), "psuf-shape L%x %s p%d n=%lu", level,
		 tag, p, (unsigned long)n);
	if (!src || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	fill_periodic(src, n, p);
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (eret == 0 || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want COMP",
			 (unsigned long)eret, eret ? enc[0] : 0);
		t_fail(name, detail);
		goto out;
	}
	bo = r16le(enc + 5);
	fo = r16le(enc + 7);
	if (want_plus5) {
		if (fo != (uint16_t)(bo + 5)) {
			snprintf(detail, sizeof(detail),
				 "bo=%u fo=%u, want fo=bo+5 (u18)", bo, fo);
			t_fail(name, detail);
			goto out;
		}
	} else {
		if (fo != bo) {
			snprintf(detail, sizeof(detail),
				 "bo=%u fo=%u, want fo==bo (u12)", bo, fo);
			t_fail(name, detail);
			goto out;
		}
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (ds != n || dr != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(src);
	free(enc);
	free(dec);
}

int main(void)
{
	uint8_t *buf = malloc(400);
	size_t li;
	int p;

	if (!buf) {
		t_fail("alloc", "oom");
		goto done;
	}

	/* 72-cell sweep: p9..32 x n100 x e01/e05/e09, lc2 layout. */
	for (p = 9; p <= 32; p++) {
		uint16_t bo = (uint16_t)(13 + p);
		uint16_t fo = (uint16_t)(18 + p);
		size_t total = (size_t)(29 + p);

		for (li = 0; li < 3; li++) {
			/* U1 s06: e01-p31 is MX-shadowed (u18 veto ->
			 * u37 GEN take@32, oracle-ident 61B). */
			if (MLEVELS[li] == 0xE01 && p == 31) {
				comp_layout_one_lit(0xE01, p, 100, 45, 50,
						    61, 2, 32);
				continue;
			}
			comp_layout_one(MLEVELS[li], p, 100, bo, fo, total,
					2);
		}
	}

	/* Minimal fire p=9: n35 RAW, n36 COMP 37B bo21 fo26 lc1. */
	fill_periodic(buf, 35, 9);
	raw_one(0xE05, buf, 35, "p9");
	comp_layout_one(0xE05, 9, 36, 21, 26, 37, 1);

	/* p32 short holds RAW at n58 (G4/short, both families agree). */
	fill_periodic(buf, 58, 32);
	raw_one(0xE05, buf, 58, "p32");

	/* Len-esc pins: p12n200 41B lc2, p9n400 42B lc6. */
	for (li = 0; li < 3; li++) {
		comp_layout_one(MLEVELS[li], 12, 200, 25, 30, 41, 2);
		comp_layout_one(MLEVELS[li], 9, 400, 26, 31, 42, 6);
	}

	/* L0 periodic: litonly-HUF COMP where kept (u36, R4;
	 * oracle-identical: p9 83B, p12 87B), RAW on S5.5 rollback
	 * (p32 k=32). Old "stays RAW" note predates R3-A2. */
	fill_periodic(buf, 100, 9);
	comp_one(0xE00, buf, 100, 83, "p9");
	fill_periodic(buf, 100, 12);
	comp_one(0xE00, buf, 100, 87, "p12");
	fill_periodic(buf, 100, 32);
	raw_one(0xE00, buf, 100, "p32");

	/* G4 short holds RAW: AABCDEFGA n40 (ml31) at match levels. */
	for (li = 0; li < 3; li++) {
		fill_pat(buf, 40, "AABCDEFGA", 9);
		raw_one(MLEVELS[li], buf, 40, "AABCDEFGA");
	}

	/* Harmonic guard: p2/4/6 stay u12 (fo==bo), p12 is u18 (fo==bo+5). */
	shape_fo_one(0xE05, 2, 100, 0, "harmonic");
	shape_fo_one(0xE05, 4, 100, 0, "harmonic");
	shape_fo_one(0xE05, 6, 100, 0, "harmonic");
	shape_fo_one(0xE05, 12, 100, 1, "suf");

done:
	printf("---\nm21-shapep-suf: pass=%d fail=%d\n", g_pass, g_fail);
	free(buf);
	return g_fail ? 1 : 0;
}
