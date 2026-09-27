/* SPDX-License-Identifier: 0BSD */
/*
 * test_m26_shaper12b_veto.c — M26 SHAPE-R12B veto pins (u23, GAPLOG-u23
 * Task 4 vetoes, all fail-safe RAW n+6 + exact roundtrip).
 *
 * SPEC-v2: S5.3 (TIER-1 D>fo strict; TIER-2 fo+10<=n), S5.5
 * (mode_trivial HUF->fail-safe-0), S5.9.a (one parse), S4.3 (exact
 * end), S5.1 (determinism), S1.3/Q20 (full-form levels).
 * SHAPE-R12B (u23, U21-R12B partial close): k-run rep-chain with
 * >=1 interior G==2 gap ([r2] or [r1,r1]) or adjacent-lead-shorts;
 * lead total L<=2, last run long. Vetoes here: interior G==3 via
 * [1,1,1] ([A12,X1,Y1,Z1,B12] n27), lead L==3 via [1,1,1]
 * ([X1,Y1,Z1,A12,B12] n27) and via [1,2] ([X1,Y2,A12,B12] n27),
 * TIER-2 ([A9,X2,B9] n20: u23 shape
 * but fo+10>n), C6/TIER-1 ([A3,X2,B3] n8: D<=fo), lit-HUF with G==2
 * (10xL12 + 8xG1 + 1xG2 n130: TIER/class pass, mode gate vetoes —
 * same class as M25 lit-huf tokc10/litc20).
 * M30 FLIP (u27 SHAPE-R12CD, U23-R12C close): interior G==3 via
 * [1,2] ([A14,X1,Y2,B14] n31) is no longer a veto — port now emits
 * COMP 30B tag 01, merge-proven byte-identical to oracle enc +
 * self-exact + both cross legs exact (M30 T5, 163/163). Pinned here
 * as COMP-exact (oracle-true bytes, hand-derived from the u27
 * Task-2a recipe + S3.9/S3.10, verified via public API).
 * M35 FLIP (u32 SHAPE-R-TC2 D1-family, MERGE-M35 T4/T5): trailing
 * short with G==2 present ([A14,X2,B14,Y1] n31) is no longer a
 * veto — port now emits COMP 31B tag 01, merge-proven byte-
 * identical to oracle enc + 3 legs exact x3 (M35 T5, 178/178).
 * Pinned here as COMP-exact (M30-class flip).
 * Each veto pinned at all 3 match levels (mechanisms are
 * level-independent; verified RAW at e01/e05/e09 via public API).
 * L0 excluded (no finding, same RAW by construction, pinned for
 * flips in test_m26_shaper12b.c).
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

/* M30 flip: [A14,X1,Y2,B14] n31 COMP 30B (ds31 bo19 fo19, lit 41 58
 * 59 59 42, tok 07 C7, len 04 01 04 [mc11 extra, G3 lit-rest1,
 * mc11 extra], modes 0, footer 2/3/5/0, END). */
static const uint8_t EXP_G3_12[] = {
	0x01, 0x1f, 0x00, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x41,
	0x58, 0x59, 0x59, 0x42, 0x07, 0xc7, 0x04, 0x01, 0x04, 0x00,
	0x00, 0x02, 0x00, 0x03, 0x00, 0x05, 0x00, 0x00, 0x00, 0xff
};

/* M35 flip: [A14,X2,B14,Y1] n31 COMP 31B (ds31 bo20 fo20, lit 41
 * 58 58 42 59, tok 07 C7, len 40 04 00 04, modes 0, footer
 * 3/3/5/0, END). Port-emitted, oracle byte-IDENT x3 (M35 T5). */
static const uint8_t EXP_TRAIL_R1[] = {
	0x01, 0x1f, 0x00, 0x00, 0x00, 0x14, 0x00, 0x14, 0x00, 0x41,
	0x58, 0x58, 0x42, 0x59, 0x07, 0xc7, 0x40, 0x04, 0x00, 0x04,
	0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x05, 0x00, 0x00, 0x00,
	0xff
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

/* RAW hold pin: tag 00 + outlen n+6 + exact roundtrip + determinism. */
static void raw_one(int level, const size_t *rs, const uint8_t *bs, size_t k,
		    const char *why)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, bs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "shaper12b-veto L%x %s n=%lu", level,
		 why, (unsigned long)n);
	if (!src || !enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret != n + 6 || enc[0] != 0x00) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want %lu/00",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)(n + 6));
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

/* M30 COMP flip pin: byte-exact + sizer + decode-exact + determinism. */
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

	snprintf(name, sizeof(name), "shaper12b-veto-flip L%x %s n=%lu",
		 level, tag, (unsigned long)n);
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

int main(void)
{
	static const size_t V_G3_12[] = { 14, 1, 2, 14 };
	static const size_t V_G3_111[] = { 12, 1, 1, 1, 12 };
	static const size_t V_L3_111[] = { 1, 1, 1, 12, 12 };
	static const size_t V_L3_12[] = { 1, 2, 12, 12 };
	static const size_t V_TRAIL[] = { 14, 2, 14, 1 };
	static const size_t V_T2[] = { 9, 2, 9 };
	static const size_t V_C6[] = { 3, 2, 3 };
	static const size_t V_HUF[] = { 12, 1, 12, 1, 12, 1, 12, 1, 12,
					2, 12, 1, 12, 1, 12, 1, 12, 1, 12 };
	static const uint8_t B_AXYB[] = { 0x41, 0x58, 0x59, 0x42 };
	static const uint8_t B_AXYZB[] = { 0x41, 0x58, 0x59, 0x5a, 0x42 };
	static const uint8_t B_XYZAB[] = { 0x58, 0x59, 0x5a, 0x41, 0x42 };
	static const uint8_t B_XYAB[] = { 0x58, 0x59, 0x41, 0x42 };
	static const uint8_t B_AXBY[] = { 0x41, 0x58, 0x42, 0x59 };
	static const uint8_t B_AXB[] = { 0x41, 0x58, 0x42 };
	static const uint8_t B_HUF[] = { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
					 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
					 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52,
					 0x53 };
	size_t li;

	for (li = 0; li < 3; li++) {
		comp_exact_one(MLEVELS[li], V_G3_12, B_AXYB, 4, EXP_G3_12,
			       sizeof(EXP_G3_12), "interior-G3-12-COMP");
		raw_one(MLEVELS[li], V_G3_111, B_AXYZB, 5, "interior-G3-111");
		raw_one(MLEVELS[li], V_L3_111, B_XYZAB, 5, "lead-L3-111");
		raw_one(MLEVELS[li], V_L3_12, B_XYAB, 4, "lead-L3-12");
		comp_exact_one(MLEVELS[li], V_TRAIL, B_AXBY, 4, EXP_TRAIL_R1,
			       sizeof(EXP_TRAIL_R1), "trailing-short-R1-COMP");
		raw_one(MLEVELS[li], V_T2, B_AXB, 3, "tier2");
		raw_one(MLEVELS[li], V_C6, B_AXB, 3, "c6-tier1");
		raw_one(MLEVELS[li], V_HUF, B_HUF, 19, "lit-huf");
	}

	printf("---\nm26-shaper12b-veto: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
