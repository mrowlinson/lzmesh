/* SPDX-License-Identifier: 0BSD */
/*
 * test_m19_shapep_long.c — M19 SHAPE-P-LONG pins (u15, MERGE-M18).
 *
 * SPEC-v2: S3.9/S3.10/S3.11 (token_new grammar, mc->ml=mc+2),
 * S5.3 (TIER-1 D>fo, TIER-2 outpos<=n keep, Q18 first-block litc!=0),
 * S5.5 (mode_trivial), S5.7c (L0 litonly vs L1/L5/L9 match rows),
 * S5.8 (backext long-take; S5.9.a non-parity posture), S5.1
 * (determinism), S1.3/Q20 (full-form levels).
 * SHAPE-P-LONG (u15): SAME bytes as SHAPE-P (single-token new-dist
 * no-H COMP, p-periodic 2..8, bo==fo, footer (1,lc,p,1); reuses
 * u12_emit) but the G4 gate (rep2-live at p/p+1/p+2) is SKIPPED iff
 * long-take (ml>38, R-026 observed). Fires only where u12_want==0.
 * Lane Task-4: AABA40 (ml36 short) RAW; AABA100 e05 COMP 28B (was
 * RAW); AABA100 COMP at e01/e05/e09, RAW at e00; seq/text still RAW
 * (Huffman open). Merge sweep: 45 RAW->COMP diffs (AABA/AAAB/AABABA
 * x n=64..300 x e01/e05/e09; post 28/30/32/34B), zero reverse.
 * AABA400 32B is pinned by test_m16_shapep_eb4 (not duplicated here).
 * U15-G4LONG 1B delta (AABA400 32B vs oracle 33B) stays open.
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

/* COMP flip pin: tag 01 + exact roundtrip + smaller + determinism. */
static void comp_one(int level, const uint8_t *src, size_t n,
		     const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "plong-comp L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret == 0 || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want COMP",
			 (unsigned long)eret, eret ? enc[0] : 0);
		t_fail(name, detail);
		goto out;
	}
	if (eret >= n) {
		snprintf(detail, sizeof(detail), "outlen=%lu >= n", (unsigned long)eret);
		t_fail(name, detail);
		goto out;
	}
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail(name, "nondeterministic");
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
	free(enc2);
	free(dec);
}

/* RAW hold pin: tag 00 + outlen n+6 + exact roundtrip. */
static void raw_one(int level, const uint8_t *src, size_t n, const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr;

	snprintf(name, sizeof(name), "plong-raw L%x %s n=%lu", level, tag,
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

/* AABA100/e05 is 28B COMP (lane Task-4 pin, merge-confirmed). */
static void aaba100_pin(int level)
{
	char name[96], detail[160];
	uint8_t *src = malloc(100);
	uint8_t *enc = malloc(1024);
	uint8_t *dec = malloc(256);
	size_t i, eret, ds, dr;

	snprintf(name, sizeof(name), "plong L%x AABA100 28B pin", level);
	if (!src || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	for (i = 0; i < 100; i++)
		src[i] = (uint8_t)("AABA"[i & 3]);
	eret = lzmesh_encode(enc, 1024, src, 100, NULL, level);
	if (eret != 28 || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want 28/01",
			 (unsigned long)eret, eret ? enc[0] : 0);
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, 256, enc, eret, NULL);
	if (ds != 100 || dr != 100 || memcmp(dec, src, 100) != 0) {
		t_fail(name, "sizer/decode");
		goto out;
	}
	t_pass(name);
out:
	free(src);
	free(enc);
	free(dec);
}

static void fill_pat(uint8_t *dst, size_t n, const char *pat, size_t plen)
{
	size_t i;

	for (i = 0; i < n; i++)
		dst[i] = (uint8_t)pat[i % plen];
}

int main(void)
{
	static const size_t SWEEP[] = { 64, 100, 200, 300 };
	static const char *PATS[] = { "AABA", "AAAB", "AABABA" };
	static const size_t PLENS[] = { 4, 4, 6 };
	uint8_t *buf = malloc(300);
	size_t li, pi, ni;
	char ptag[16];

	if (!buf) {
		t_fail("alloc", "oom");
		goto done;
	}

	/* Lane Task-4 pins: AABA100 28B COMP at match levels; 51B
	 * litonly-HUF COMP at L0 (u36, R4; oracle-identical). Old L0-RAW
	 * note predates R3-A2. */
	for (li = 0; li < 3; li++)
		aaba100_pin(MLEVELS[li]);
	fill_pat(buf, 100, "AABA", 4);
	comp_one(0xE00, buf, 100, "AABA");

	/* ml>38 razor: AABA42 (ml38) RAW, AABA43 (ml39) COMP. */
	for (li = 0; li < 3; li++) {
		fill_pat(buf, 42, "AABA", 4);
		raw_one(MLEVELS[li], buf, 42, "AABA");
		fill_pat(buf, 43, "AABA", 4);
		comp_one(MLEVELS[li], buf, 43, "AABA");
	}

	/* Short-G4 holds RAW (ml<=38 by design): AABA40 + sisters. */
	for (li = 0; li < 3; li++) {
		fill_pat(buf, 40, "AABA", 4);
		raw_one(MLEVELS[li], buf, 40, "AABA");
		fill_pat(buf, 40, "AAAB", 4);
		raw_one(MLEVELS[li], buf, 40, "AAAB");
		fill_pat(buf, 40, "AABABA", 6);
		raw_one(MLEVELS[li], buf, 40, "AABABA");
	}

	/* Merge sweep: long G4-class periodics flip RAW->COMP. */
	for (pi = 0; pi < 3; pi++) {
		for (ni = 0; ni < sizeof(SWEEP) / sizeof(SWEEP[0]); ni++) {
			for (li = 0; li < 3; li++) {
				snprintf(ptag, sizeof(ptag), "%s",
					 PATS[pi]);
				fill_pat(buf, SWEEP[ni], PATS[pi],
					 PLENS[pi]);
				comp_one(MLEVELS[li], buf, SWEEP[ni], ptag);
			}
		}
	}

done:
	printf("---\nm19-shapep-long: pass=%d fail=%d\n", g_pass, g_fail);
	free(buf);
	return g_fail ? 1 : 0;
}
