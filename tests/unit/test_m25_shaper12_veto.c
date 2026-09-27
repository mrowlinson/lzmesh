/* SPDX-License-Identifier: 0BSD */
/*
 * test_m25_shaper12_veto.c — M25 SHAPE-R12 veto pins (u21, GAPLOG-u21
 * Task 4 vetoes, all fail-safe RAW n+6 + exact roundtrip).
 *
 * SPEC-v2: S5.3 (TIER-1 D>fo strict; TIER-2 fo+10<=n), S5.5
 * (mode_trivial HUF->fail-safe-0), S5.9.a (one parse), S4.3 (exact
 * end), S5.1 (determinism), S1.3/Q20 (full-form levels).
 * Vetoes (U21-R12B + gates): trailing short ([A12,B12,X1] n25),
 * interior r==2 ([A12,X2,B12] n26), TIER-1 ([X1,A12] n13:
 * D=13 > fo=13 false), TIER-2 ([X1,A12,B11] n24: fo+10=26 >
 * 24), lit-HUF ([{1,12}x10] n130: tokc10/litc20 -> lit lane
 * HUF -> want 0; pure-HUF pin — TIER-1/TIER-2/class all pass,
 * so the veto is the mode gate alone; lane's stated cell was
 * tokc10/litc21). Adjacent lead shorts ([X1,Y1,A12,B12] n26)
 * are NOT a veto since u23 SHAPE-R12B (lead [1,1] feature):
 * COMP 27B tag 01, oracle-identical (COMP-VERIFY-R10 SHAPER12B
 * vetoadj ohex, both legs exact) — pinned byte-exact below.
 * Each case pinned at all 3 match levels (mechanisms are
 * level-independent; verified via public API).
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

/* [X1,Y1,A12,B12] n26: 27B oracle-identical (ds26 bo16 fo16,
 * lit 58 59 41 42, tok 87 47, len-ex 02 REPEAT-packed,
 * modes 0x40, footer 2/2/4/0, END). Ex-veto: u23 SHAPE-R12B
 * lead-[1,1] feature; COMP-VERIFY-R10 SHAPER12B vetoadj ohex,
 * both legs exact both directions. */
static const uint8_t EXP_VETOADJ[] = {
	0x01, 0x1a, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x58,
	0x59, 0x41, 0x42, 0x87, 0x47, 0x02, 0x40, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0xff
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

	snprintf(name, sizeof(name), "shaper12-veto L%x %s n=%lu", level,
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

/* COMP byte-exact pin + sizer + decode-exact + determinism. */
static void comp_exact_one(int level, const size_t *rs, const uint8_t *bs,
			   size_t k, const uint8_t *exp, size_t elen,
			   const char *why)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, bs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "shaper12-veto L%x %s n=%lu", level,
		 why, (unsigned long)n);
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
	static const size_t V_TRAIL[] = { 12, 12, 1 };
	static const size_t V_INT2[] = { 12, 2, 12 };
	static const size_t V_ADJ[] = { 1, 1, 12, 12 };
	static const size_t V_T1[] = { 1, 12 };
	static const size_t V_T2[] = { 1, 12, 11 };
	static const size_t V_HUF[] = { 1, 12, 1, 12, 1, 12, 1, 12, 1, 12,
					1, 12, 1, 12, 1, 12, 1, 12, 1, 12 };
	static const uint8_t B_ABX[] = { 0x41, 0x42, 0x58 };
	static const uint8_t B_AXB[] = { 0x41, 0x58, 0x42 };
	static const uint8_t B_XYAB[] = { 0x58, 0x59, 0x41, 0x42 };
	static const uint8_t B_XA[] = { 0x58, 0x41 };
	static const uint8_t B_XAB[] = { 0x58, 0x41, 0x42 };
	static const uint8_t B_HUF[] = { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
					 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
					 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52,
					 0x53, 0x54 };
	size_t li;

	for (li = 0; li < 3; li++) {
		raw_one(MLEVELS[li], V_TRAIL, B_ABX, 3, "trailing-short");
		raw_one(MLEVELS[li], V_INT2, B_AXB, 3, "interior-r2");
		comp_exact_one(MLEVELS[li], V_ADJ, B_XYAB, 4,
			     EXP_VETOADJ, sizeof(EXP_VETOADJ),
			     "adjacent-shorts");
		raw_one(MLEVELS[li], V_T1, B_XA, 2, "tier1");
		raw_one(MLEVELS[li], V_T2, B_XAB, 3, "tier2");
		raw_one(MLEVELS[li], V_HUF, B_HUF, 20, "lit-huf");
	}

	printf("---\nm25-shaper12-veto: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
