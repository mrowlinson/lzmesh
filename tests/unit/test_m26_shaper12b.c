/* SPDX-License-Identifier: 0BSD */
/*
 * test_m26_shaper12b.c — M26 SHAPE-R12B COMP pins (u23, GAPLOG-u23 Task 4).
 *
 * SPEC-v2: S5.3 (TIER-1 D>fo strict; TIER-2 fo+10<=n keep,
 * equality keeps), S5.5 (mode_trivial: small-count no-H stays
 * RAW/REPEAT, HUF->fail-safe-0), S5.9.a (one parse, rep takes need
 * no slots_clean; F5 none), S5.9.b (label rule: this harness gates
 * on port bytes via the public API only, never on lane labels),
 * S3.9/S3.10 (token=(lit<<6)|(sel<<3)|len, rep0 esc7 + extra mc-7,
 * mc=r-3, inline iff <=6; lit esc3), S3.11/Q5 (lit lane,
 * REPEAT-packed len lane stores one exemplar while lenc counts
 * logical extra bytes), S4.3 (C18 consumed==litc, exact end;
 * intra-token order lit-extra before len-extra), S4.5 (encode
 * caps), S5.1 (determinism), S1.3/Q20 (full-form levels).
 * SHAPE-R12B (u23, U21-R12B partial close): k-run rep-chain, k>=2,
 * last run >=3, >=1 u23-feature (interior G==2 gap as [r2] or
 * [r1,r1], or adjacent-lead-shorts); lead total L<=2; other shorts
 * u21-style isolated singletons. Take at 2nd byte of each LONG run
 * (rep0 d1, ml=r-1); gap bytes ride as literals of the following
 * token. First-token lit=L; non-first lit=G+1 (1, 2, or 3-esc for
 * G==2 with len-lane extra 0, NEW). bo==fo (no suffix, d=1 sb0);
 * footer (tokc,lenc,litc,0) + END. Levels 1/5/9, fires only where
 * u12/u15/u18/u19/u21 want==0 (disjoint by construction +
 * lane-proven). L0 excluded (no finding).
 * Cells (GAPLOG-u23 Task 4 flips, all e01/e05/e09 byte-identical):
 * [A14,X2,B14] n30 29B, [X1,Y1,A13,B13] n28 27B (lead [1,1]),
 * [A9,X2,B9,C9] n29 29B (len-REPEAT 00), [A13,X1,Y1,B13] n28 29B
 * (G==2 via [1,1], TIER-2 equality), [X1,A14,Y2,B14] n31 30B
 * (lead+G2 mixed), [A300,X2,B12] n314 33B (5B len esc).
 * PORT bytes pinned here (public API, self-exact + sizer-exact +
 * determinism verified). Merge-observed oracle status (M26 T5,
 * 21/21 self-exact + oracle-accepts + port-dec-oracle): byte-IDENT
 * cells pinned here are [A14,X2,B14] 29B, [X1,Y1,A13,B13] 27B,
 * [A13,X1,Y1,B13] 29B, [X1,A14,Y2,B14] 30B, [A300,X2,B12] 33B
 * (non-first lit3 leg VERIFIED: every lit3 cell byte-identical to
 * oracle enc). [A9,X2,B9,C9] 29B pins PORT bytes: oracle emits RAW
 * 35B (tag 00) there — valid reverse style-delta, oracle accepts
 * port's 29B byte-exact, both sides decode both framings (M26 T6,
 * G8-class, not a defect).
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

/* [A14,X2,B14] n30: 29B (ds30 bo18 fo18, lit 41 585842, tok 07 C7,
 * len 04 00 04, modes 0x0000, footer 2/3/4/0, END). */
static const uint8_t EXP_A14_X2_B14[] = {
	0x01, 0x1e, 0x00, 0x00, 0x00, 0x12, 0x00, 0x12, 0x00, 0x41,
	0x58, 0x58, 0x42, 0x07, 0xc7, 0x04, 0x00, 0x04, 0x00, 0x00,
	0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0xff
};

/* [X1,Y1,A13,B13] n28: 27B (lit 58 59 41 42, tok 87 47, len 03
 * REPEAT-packed, modes 0x40, footer 2/2/4/0). */
static const uint8_t EXP_X1_Y1_A13_B13[] = {
	0x01, 0x1c, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x58,
	0x59, 0x41, 0x42, 0x87, 0x47, 0x03, 0x40, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0xff
};

/* [A9,X2,B9,C9] n29: 29B (lit 41 58584243, tok 06 C6 46, len 00
 * REPEAT-packed via lit-extra branch, modes 0x40, footer 3/1/5/0). */
static const uint8_t EXP_A9_X2_B9_C9[] = {
	0x01, 0x1d, 0x00, 0x00, 0x00, 0x12, 0x00, 0x12, 0x00, 0x41,
	0x58, 0x58, 0x42, 0x43, 0x06, 0xc6, 0x46, 0x00, 0x40, 0x00,
	0x03, 0x00, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0xff
};

/* [A13,X1,Y1,B13] n28: 29B (G==2 via [1,1], lit 41 585942, tok 07
 * C7, len 03 00 03, modes 0x0000, footer 2/3/4/0; TIER-2 equality
 * 18+10==28 keeps). */
static const uint8_t EXP_A13_X1_Y1_B13[] = {
	0x01, 0x1c, 0x00, 0x00, 0x00, 0x12, 0x00, 0x12, 0x00, 0x41,
	0x58, 0x59, 0x42, 0x07, 0xc7, 0x03, 0x00, 0x03, 0x00, 0x00,
	0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0xff
};

/* [X1,A14,Y2,B14] n31: 30B (lead+G2 mixed, lit 58 41 595942, tok
 * 47 C7, len 04 00 04, modes 0x0000, footer 2/3/5/0). */
static const uint8_t EXP_X1_A14_Y2_B14[] = {
	0x01, 0x1f, 0x00, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x58,
	0x41, 0x59, 0x59, 0x42, 0x47, 0xc7, 0x04, 0x00, 0x04, 0x00,
	0x00, 0x02, 0x00, 0x03, 0x00, 0x05, 0x00, 0x00, 0x00, 0xff
};

/* [A300,X2,B12] n314: 33B (5B len esc ff+0122 LE + lit-extra 00 +
 * len-extra 02, modes 0, footer 2/7/4/0). */
static const uint8_t EXP_A300_X2_B12[] = {
	0x01, 0x3a, 0x01, 0x00, 0x00, 0x16, 0x00, 0x16, 0x00, 0x41,
	0x58, 0x58, 0x42, 0x07, 0xc7, 0xff, 0x22, 0x01, 0x00, 0x00,
	0x00, 0x02, 0x00, 0x00, 0x02, 0x00, 0x07, 0x00, 0x04, 0x00,
	0x00, 0x00, 0xff
};

static uint8_t *make_runs(const size_t *rs, const uint8_t *bs, size_t k,
			   size_t *np)
{
	size_t n = 0, i, j, p = 0;
	uint8_t *buf;

	for (i = 0; i < k; i++)
		n += rs[i];
	buf = malloc(n ? n : 1);
	if (!buf)
		return NULL;
	for (i = 0; i < k; i++)
		for (j = 0; j < rs[i]; j++)
			buf[p++] = bs[i];
	*np = n;
	return buf;
}

/* COMP byte-exact pin + sizer + decode-exact + determinism. */
static void comp_exact_one(int level, const size_t *rs, const uint8_t *bs,
			   size_t k, const uint8_t *exp, size_t elen,
			   const char *tag)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, bs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "shaper12b-comp L%x %s n=%lu", level,
		 tag, (unsigned long)n);
	if (!src || !enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret != elen || memcmp(enc, exp, elen) != 0) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x, want %lu + exact bytes",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)elen);
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
	free(src);
	free(enc);
	free(enc2);
	free(dec);
}

/* L0 stays RAW (no finding): tag 00 + outlen n+6 + exact roundtrip. */
static void raw_l0_one(const size_t *rs, const uint8_t *bs, size_t k,
		       const char *tag)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, bs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr;

	snprintf(name, sizeof(name), "shaper12b-L0 %s n=%lu", tag,
		 (unsigned long)n);
	if (!src || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, 0xE00);
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
	free(src);
	free(enc);
	free(dec);
}

/* S4.5 caps on the flagship: need-1 -> 0, need -> need, NULL==sized. */
static void cap_one(void)
{
	static const size_t RS[] = { 14, 2, 14 };
	static const uint8_t BS[] = { 0x41, 0x58, 0x42 };
	char detail[128];
	size_t n = 0, eret, eret2;
	uint8_t *src = make_runs(RS, BS, 3, &n);
	uint8_t enc[64], enc2[64];
	void *es;
	size_t esz;

	if (!src) {
		t_fail("shaper12b-cap", "oom");
		return;
	}
	eret = lzmesh_encode(enc, sizeof(EXP_A14_X2_B14) - 1, src, n, NULL,
			     0xE05);
	if (eret != 0) {
		snprintf(detail, sizeof(detail), "need-1 cap -> %lu, want 0",
			 (unsigned long)eret);
		t_fail("shaper12b-cap", detail);
		goto out;
	}
	eret = lzmesh_encode(enc, sizeof(EXP_A14_X2_B14), src, n, NULL,
			     0xE05);
	if (eret != sizeof(EXP_A14_X2_B14)) {
		snprintf(detail, sizeof(detail), "need cap -> %lu, want %lu",
			 (unsigned long)eret,
			 (unsigned long)sizeof(EXP_A14_X2_B14));
		t_fail("shaper12b-cap", detail);
		goto out;
	}
	esz = lzmesh_encode_scratch_size(0xE05);
	es = malloc(esz ? esz : 1);
	if (!es) {
		t_fail("shaper12b-cap", "oom");
		goto out;
	}
	eret2 = lzmesh_encode(enc2, sizeof(enc2), src, n, es, 0xE05);
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail("shaper12b-cap", "NULL vs sized scratch differ");
		free(es);
		goto out;
	}
	free(es);
	t_pass("shaper12b-cap");
out:
	free(src);
}

int main(void)
{
	static const size_t R_F1[] = { 14, 2, 14 };
	static const size_t R_F2[] = { 1, 1, 13, 13 };
	static const size_t R_F3[] = { 9, 2, 9, 9 };
	static const size_t R_F4[] = { 13, 1, 1, 13 };
	static const size_t R_F5[] = { 1, 14, 2, 14 };
	static const size_t R_F6[] = { 300, 2, 12 };
	static const uint8_t B_AXB[] = { 0x41, 0x58, 0x42 };
	static const uint8_t B_XYAB[] = { 0x58, 0x59, 0x41, 0x42 };
	static const uint8_t B_AXBC[] = { 0x41, 0x58, 0x42, 0x43 };
	static const uint8_t B_AXYB[] = { 0x41, 0x58, 0x59, 0x42 };
	static const uint8_t B_XAYB[] = { 0x58, 0x41, 0x59, 0x42 };
	size_t li;

	/* Flagships byte-exact all 3 match levels (identical bytes). */
	for (li = 0; li < 3; li++) {
		comp_exact_one(MLEVELS[li], R_F1, B_AXB, 3,
			       EXP_A14_X2_B14, sizeof(EXP_A14_X2_B14),
			       "A14+X2+B14");
		comp_exact_one(MLEVELS[li], R_F2, B_XYAB, 4,
			       EXP_X1_Y1_A13_B13,
			       sizeof(EXP_X1_Y1_A13_B13),
			       "X1+Y1+A13+B13-lead11");
		comp_exact_one(MLEVELS[li], R_F4, B_AXYB, 4,
			       EXP_A13_X1_Y1_B13,
			       sizeof(EXP_A13_X1_Y1_B13),
			       "A13+X1+Y1+B13-G2-11");
	}

	/* Remaining flips e05 (lane-verified identical across levels). */
	comp_exact_one(0xE05, R_F3, B_AXBC, 4, EXP_A9_X2_B9_C9,
		       sizeof(EXP_A9_X2_B9_C9), "A9+X2+B9+C9");
	comp_exact_one(0xE05, R_F5, B_XAYB, 4, EXP_X1_A14_Y2_B14,
		       sizeof(EXP_X1_A14_Y2_B14), "X1+A14+Y2+B14");
	comp_exact_one(0xE05, R_F6, B_AXB, 3, EXP_A300_X2_B12,
		       sizeof(EXP_A300_X2_B12), "A300+X2+B12");

	/* L0 stays RAW (no finding) on u23 shapes. */
	raw_l0_one(R_F1, B_AXB, 3, "A14+X2+B14");
	raw_l0_one(R_F2, B_XYAB, 4, "X1+Y1+A13+B13");
	raw_l0_one(R_F4, B_AXYB, 4, "A13+X1+Y1+B13");

	cap_one();

	printf("---\nm26-shaper12b: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
