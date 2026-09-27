/* SPDX-License-Identifier: 0BSD */
/*
 * test_m25_shaper12.c — M25 SHAPE-R12 COMP pins (u21, MERGE-M25 T5/T6).
 *
 * SPEC-v2: S5.3 (TIER-1 D>fo strict; TIER-2 fo+10<=n keep,
 * equality keeps), S5.5 (mode_trivial: small-count no-H stays
 * RAW/REPEAT, HUF->fail-safe-0), S5.9.a (one parse, rep takes need
 * no slots_clean; F5 none), S5.9.b (label rule: this harness gates
 * on port bytes via the public API only, never on lane labels),
 * S3.9/S3.10 (token=(lit<<6)|(sel<<3)|len, rep0 esc7 + extra mc-7,
 * mc=r-3, inline iff <=6), S3.11/Q5 (lit lane, REPEAT-packed len
 * lane stores one exemplar while lenc counts logical extra bytes),
 * Q18 (first litc!=0), S4.3 (C18 consumed==litc, exact end), S4.5
 * (encode caps), S5.1 (determinism), S1.3/Q20 (full-form levels).
 * SHAPE-R12 (u21, U19-R12 partial close): k-run rep-chain, k>=2,
 * last run >=3, >=1 short: lead r0 in {1,2} and/or isolated
 * interior singletons (r==1); all other runs >=3. Levels 1/5/9,
 * fires only where u12/u15/u18/u19 want==0 (disjoint from u19 by
 * construction + lane-proven). Take at 2nd byte of each LONG run
 * (rep0 d1, ml=r-1); short bytes ride as literals of the following
 * token. First-token lit = r0-short ? r0 : 0; non-first lit =
 * singleton-before ? 2 (U21-LIT2) : 1. bo==fo (no suffix, d=1
 * sb0); footer (tokc,lenc,litc,0) + END. L0 excluded (no finding);
 * interior r==2 / adjacent shorts / trailing shorts stay RAW
 * (U21-R12B, pinned in test_m25_shaper12_veto.c).
 * Merge-observed oracle status (M25 T5/T6, 21/21 self-exact +
 * oracle-accepts + port-dec-oracle): byte-IDENTICAL cells pinned
 * here are lead2 [X2,A12,B12] 27B, litsingle [A12,X1,B12] 26B
 * (U21-LIT2 live, VERDICT CONFIRMED), 3-singleton n39 30B
 * (U21-LIT2 live). Lead1-family cells ([X1,A12,B12] 25B, [X1,A24]
 * 24B, [X1,A20,B12] 26B, [X1,A300,B12] 30B) are oracle-identical
 * since U21-TOKREPEAT closed: port emits the oracle tok-REPEAT
 * form (single tok 47 + modes 0x48/0x08, 1B shorter except the
 * single-tok solo cell) — COMP-VERIFY-R10 SHAPER12 ohex, both
 * legs exact both directions.
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

/* [X1,A12,B12] n25: 25B oracle-identical (ds25 bo14 fo14,
 * lit 58 41 42, single tok 47 REPEAT-packed, len-ex 02,
 * modes 0x48, footer 2/2/3/0, END). U21-TOKREPEAT closed:
 * port now emits the oracle tok-REPEAT form (was 26B
 * tok-RAW 47 47 / modes 0x40). */
static const uint8_t EXP_X1_A12_B12[] = {
	0x01, 0x19, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x58,
	0x41, 0x42, 0x47, 0x02, 0x48, 0x00, 0x02, 0x00, 0x02, 0x00,
	0x03, 0x00, 0x00, 0x00, 0xff
};

/* [X2,A12,B12] n26: 27B oracle-identical (tok 87 47, litc 4). */
static const uint8_t EXP_X2_A12_B12[] = {
	0x01, 0x1a, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x58,
	0x58, 0x41, 0x42, 0x87, 0x47, 0x02, 0x40, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0xff
};

/* [A12,X1,B12] n25: 26B oracle-identical (tok 07 87, U21-LIT2). */
static const uint8_t EXP_A12_X1_B12[] = {
	0x01, 0x19, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x41,
	0x58, 0x42, 0x07, 0x87, 0x02, 0x40, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* [X1,A24] n25: 24B oracle-identical (single tok 47, single
 * len extra 0e REPEAT-packed, modes 0x48, footer 1/1/2/0).
 * U21-TOKREPEAT closed: modes byte 0x40->0x48, same length. */
static const uint8_t EXP_X1_A24[] = {
	0x01, 0x19, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x58,
	0x41, 0x47, 0x0e, 0x48, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02,
	0x00, 0x00, 0x00, 0xff
};

/* [X1,A20,B12] n33: 26B oracle-identical (single tok 47,
 * len-RAW extras 0a 02, modes 0x08 tok-REPEAT + len-RAW).
 * U21-TOKREPEAT closed (was 27B tok-RAW 47 47 / modes 0). */
static const uint8_t EXP_X1_A20_B12[] = {
	0x01, 0x21, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x58,
	0x41, 0x42, 0x47, 0x0a, 0x02, 0x08, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* [X1,A300,B12] n313: 30B oracle-identical (single tok 47,
 * 5B len esc ff+290 LE + 02, modes 0x08, footer 2/6/3/0).
 * U21-TOKREPEAT closed (was 31B tok-RAW 47 47 / modes 0). */
static const uint8_t EXP_X1_A300_B12[] = {
	0x01, 0x39, 0x01, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x58,
	0x41, 0x42, 0x47, 0xff, 0x22, 0x01, 0x00, 0x00, 0x02, 0x08,
	0x00, 0x02, 0x00, 0x06, 0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* [X1,A12,Y1,B12,Z1,C12] n39: 30B oracle-identical (tok 47 87 87,
 * U21-LIT2 x2, footer 3/3/6/0). */
static const uint8_t EXP_3SING[] = {
	0x01, 0x27, 0x00, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x58,
	0x41, 0x59, 0x42, 0x5a, 0x43, 0x47, 0x87, 0x87, 0x02, 0x40,
	0x00, 0x03, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00, 0xff
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

	snprintf(name, sizeof(name), "shaper12-comp L%x %s n=%lu", level,
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

	snprintf(name, sizeof(name), "shaper12-L0 %s n=%lu", tag,
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
	static const size_t RS[] = { 1, 12, 12 };
	static const uint8_t BS[] = { 0x58, 0x41, 0x42 };
	char detail[128];
	size_t n = 0, eret, eret2;
	uint8_t *src = make_runs(RS, BS, 3, &n);
	uint8_t enc[64], enc2[64];
	void *es;
	size_t esz;

	if (!src) {
		t_fail("shaper12-cap", "oom");
		return;
	}
	eret = lzmesh_encode(enc, sizeof(EXP_X1_A12_B12) - 1, src, n, NULL,
			     0xE05);
	if (eret != 0) {
		snprintf(detail, sizeof(detail), "need-1 cap -> %lu, want 0",
			 (unsigned long)eret);
		t_fail("shaper12-cap", detail);
		goto out;
	}
	eret = lzmesh_encode(enc, sizeof(EXP_X1_A12_B12), src, n, NULL,
			     0xE05);
	if (eret != sizeof(EXP_X1_A12_B12)) {
		snprintf(detail, sizeof(detail), "need cap -> %lu, want %lu",
			 (unsigned long)eret,
			 (unsigned long)sizeof(EXP_X1_A12_B12));
		t_fail("shaper12-cap", detail);
		goto out;
	}
	esz = lzmesh_encode_scratch_size(0xE05);
	es = malloc(esz ? esz : 1);
	if (!es) {
		t_fail("shaper12-cap", "oom");
		goto out;
	}
	eret2 = lzmesh_encode(enc2, sizeof(enc2), src, n, es, 0xE05);
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail("shaper12-cap", "NULL vs sized scratch differ");
		free(es);
		goto out;
	}
	free(es);
	t_pass("shaper12-cap");
out:
	free(src);
}

int main(void)
{
	static const size_t R_X1[] = { 1, 12, 12 };
	static const size_t R_X2[] = { 2, 12, 12 };
	static const size_t R_LIT2[] = { 12, 1, 12 };
	static const size_t R_X1A24[] = { 1, 24 };
	static const size_t R_X1A20[] = { 1, 20, 12 };
	static const size_t R_X1A300[] = { 1, 300, 12 };
	static const size_t R_3S[] = { 1, 12, 1, 12, 1, 12 };
	static const uint8_t B_XAB[] = { 0x58, 0x41, 0x42 };
	static const uint8_t B_AXB[] = { 0x41, 0x58, 0x42 };
	static const uint8_t B_XA[] = { 0x58, 0x41 };
	static const uint8_t B_3S[] = { 0x58, 0x41, 0x59, 0x42, 0x5a, 0x43 };
	size_t li;

	/* Flagships byte-exact all 3 match levels (identical bytes). */
	for (li = 0; li < 3; li++) {
		comp_exact_one(MLEVELS[li], R_X1, B_XAB, 3,
			       EXP_X1_A12_B12, sizeof(EXP_X1_A12_B12),
			       "X1+A12+B12");
		comp_exact_one(MLEVELS[li], R_LIT2, B_AXB, 3,
			       EXP_A12_X1_B12, sizeof(EXP_A12_X1_B12),
			       "A12+X1+B12-lit2");
	}

	/* Remaining flips e05 (lane-verified identical across levels
	 * for X1/X2 shapes; single-level pins here). */
	comp_exact_one(0xE05, R_X2, B_XAB, 3, EXP_X2_A12_B12,
		       sizeof(EXP_X2_A12_B12), "X2+A12+B12");
	comp_exact_one(0xE05, R_X1A24, B_XA, 2, EXP_X1_A24,
		       sizeof(EXP_X1_A24), "X1+A24");
	comp_exact_one(0xE05, R_X1A20, B_XAB, 3, EXP_X1_A20_B12,
		       sizeof(EXP_X1_A20_B12), "X1+A20+B12");
	comp_exact_one(0xE05, R_X1A300, B_XAB, 3, EXP_X1_A300_B12,
		       sizeof(EXP_X1_A300_B12), "X1+A300+B12");
	comp_exact_one(0xE05, R_3S, B_3S, 6, EXP_3SING,
		       sizeof(EXP_3SING), "3singleton-lit2");

	/* L0 stays RAW (no finding) on u21 shapes. */
	raw_l0_one(R_X1, B_XAB, 3, "X1+A12+B12");
	raw_l0_one(R_LIT2, B_AXB, 3, "A12+X1+B12");
	raw_l0_one(R_3S, B_3S, 6, "3singleton");

	cap_one();

	printf("---\nm25-shaper12: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
