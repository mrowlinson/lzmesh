/* SPDX-License-Identifier: 0BSD */
/*
 * test_m22_shaper.c — M22 SHAPE-R pins (u19, MERGE-M22 T5).
 *
 * SPEC-v2: S5.3 (TIER-1 D>fo strict incl D==fo RAW pin; TIER-2
 * outpos<=n keep, equality keeps), S5.5 (mode_trivial: small-count
 * no-H stays RAW/REPEAT, HUF->fail-safe-0), S5.9.a (rep takes need
 * no slots_clean; F5 none), S3.9/S3.10 (token=(lit<<6)|(sel<<3)|len,
 * rep0 esc7 + extra mc-7, mc=r-3, inline iff <=6), S3.11/Q5 (lit
 * lane, REPEAT-packed len lane stores one exemplar while lenc counts
 * logical extra bytes), Q18 (first litc!=0), S4.3 (C18 consumed==
 * litc, exact end), S4.5 (encode caps), S5.1 (determinism), S1.3/Q20
 * (full-form levels).
 * SHAPE-R (u19): k-run rep-chain, k>=2, all runs r_i>=3, levels
 * 1/5/9, fires only where u12/u15/u18 want==0. Take at 1 (run0
 * rest, lit0/rep0) + take at each s_i+1 (lit1/rep0); bo==fo (no
 * suffix, d=1 sb0); footer (k,lc,k,0) + END. L0 excluded (no
 * finding); r_i<3 out (U19-R12); k==1 owned by u9; k>10 HUF->0.
 * Takes gated by S5.4 9B look-ahead (M27/u26 E2c: r_last<10 ->
 * fail-safe RAW; Q30 PROBABLE).
 * Merge-observed oracle byte-identity (M22 T5): n24 12+12 25B (all
 * 3 match levels) + 3x10 n30 27B + 12+20 n32 26B; e00 stays RAW30
 * both sides. 4x8 n32 + 6x6 n36 were G8 SUPERSET COMP emissions
 * until M27; the S5.4 E2c take-gate (u26: last-take rem<9 never
 * happens) vetoes them to fail-safe RAW38/RAW42, oracle-identical
 * per MERGE-M27 T5b (canonical RAW, both legs exact both
 * directions) — pinned byte-exact below.
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

/* 12+12 n24: 25B oracle-identical (ds24 bo14 fo14, lit 41 42, tok 07
 * 47, len-ex 02 REPEAT-packed, modes 0x40, footer 2/2/2/0, END). */
static const uint8_t EXP_12_12[] = {
	0x01, 0x18, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x41,
	0x42, 0x07, 0x47, 0x02, 0x40, 0x00, 0x02, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x00, 0x00, 0xff
};

/* 3x10 n30: 27B oracle-identical (mc7 esc extras 0,0,0 REPEAT 1B). */
static const uint8_t EXP_3X10[] = {
	0x01, 0x1e, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x41,
	0x42, 0x43, 0x07, 0x47, 0x47, 0x00, 0x40, 0x00, 0x03, 0x00,
	0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* 12+20 n32: 26B oracle-identical (len-RAW modes 0, extras 02 0a). */
static const uint8_t EXP_12_20[] = {
	0x01, 0x20, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x41,
	0x42, 0x07, 0x47, 0x02, 0x0a, 0x00, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x02, 0x00, 0x00, 0x00, 0xff
};

/* NOTE (M27): EXP_4X8 (28B COMP) + EXP_6X6 (32B COMP) removed — the
 * S5.4 E2c take-gate vetoes both shapes to canonical RAW (38/42B,
 * oracle-identical per MERGE-M27 T5b). RAW-pinned byte-exact via
 * raw_exact_one below. */

/* 5x11 n55: 31B (mc8 extras 1x5 REPEAT-packed, modes 0x40). */
static const uint8_t EXP_5X11[] = {
	0x01, 0x37, 0x00, 0x00, 0x00, 0x14, 0x00, 0x14, 0x00, 0x41,
	0x42, 0x43, 0x44, 0x45, 0x07, 0x47, 0x47, 0x47, 0x47, 0x01,
	0x40, 0x00, 0x05, 0x00, 0x05, 0x00, 0x05, 0x00, 0x00, 0x00,
	0xff
};

/* 300+12 n312: 30B (len 5B esc ff+290 LE + 02, modes 0). */
static const uint8_t EXP_300_12[] = {
	0x01, 0x38, 0x01, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x41,
	0x42, 0x07, 0x47, 0xff, 0x22, 0x01, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x02, 0x00, 0x06, 0x00, 0x02, 0x00, 0x00, 0x00, 0xff
};

/* 2x100 n200: 25B (mc97 extras 5a,5a REPEAT-packed, modes 0x40). */
static const uint8_t EXP_2X100[] = {
	0x01, 0xc8, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x41,
	0x42, 0x07, 0x47, 0x5a, 0x40, 0x00, 0x02, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x00, 0x00, 0xff
};

static uint8_t *make_runs(const size_t *rs, size_t k, size_t *np)
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
			buf[p++] = (uint8_t)(0x41 + i);
	*np = n;
	return buf;
}

/* COMP byte-exact pin + sizer + decode-exact + determinism. */
static void comp_exact_one(int level, const size_t *rs, size_t k,
			   const uint8_t *exp, size_t elen, const char *tag)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "shaper-comp L%x %s n=%lu", level,
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

/* RAW hold pin: tag 00 + outlen n+6 + exact roundtrip. */
static void raw_one(int level, const size_t *rs, size_t k, const char *why)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr;

	snprintf(name, sizeof(name), "shaper-raw L%x %s n=%lu", level, why,
		 (unsigned long)n);
	if (!src || !enc || !dec) {
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
	free(src);
	free(enc);
	free(dec);
}

/* RAW byte-exact pin (M27 E2c vetoes): canonical-RAW memcmp + sizer +
 * decode-exact + determinism. Canonical RAW (tag 00 + u32le ds +
 * payload + END) is unique, so byte-exactness here is oracle-identity
 * (MERGE-M27 T5b proves the oracle emits byte-identical RAW on these
 * cells, both legs exact both directions). */
static void raw_exact_one(int level, const size_t *rs, size_t k,
			  const char *tag)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	uint8_t *exp = malloc(n + 6);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "shaper-comp L%x %s n=%lu", level,
		 tag, (unsigned long)n);
	if (!src || !enc || !enc2 || !dec || !exp) {
		t_fail(name, "oom");
		goto out;
	}
	exp[0] = 0x00;
	exp[1] = (uint8_t)(n & 0xff);
	exp[2] = (uint8_t)((n >> 8) & 0xff);
	exp[3] = (uint8_t)((n >> 16) & 0xff);
	exp[4] = (uint8_t)((n >> 24) & 0xff);
	memcpy(exp + 5, src, n);
	exp[5 + n] = 0xff;
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret != n + 6 || memcmp(enc, exp, n + 6) != 0) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x, want %lu + canonical RAW",
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
	free(exp);
}

/* S4.5 caps on the flagship: need-1 -> 0, need -> need, NULL==sized. */
static void cap_one(void)
{
	static const size_t RS[] = { 12, 12 };
	char detail[128];
	size_t n = 0, eret, eret2;
	uint8_t *src = make_runs(RS, 2, &n);
	uint8_t enc[64], enc2[64];
	void *es;
	size_t esz;

	if (!src) {
		t_fail("shaper-cap", "oom");
		return;
	}
	eret = lzmesh_encode(enc, sizeof(EXP_12_12) - 1, src, n, NULL,
			     0xE05);
	if (eret != 0) {
		snprintf(detail, sizeof(detail), "need-1 cap -> %lu, want 0",
			 (unsigned long)eret);
		t_fail("shaper-cap", detail);
		goto out;
	}
	eret = lzmesh_encode(enc, sizeof(EXP_12_12), src, n, NULL, 0xE05);
	if (eret != sizeof(EXP_12_12)) {
		snprintf(detail, sizeof(detail), "need cap -> %lu, want %lu",
			 (unsigned long)eret,
			 (unsigned long)sizeof(EXP_12_12));
		t_fail("shaper-cap", detail);
		goto out;
	}
	esz = lzmesh_encode_scratch_size(0xE05);
	es = malloc(esz ? esz : 1);
	if (!es) {
		t_fail("shaper-cap", "oom");
		goto out;
	}
	eret2 = lzmesh_encode(enc2, sizeof(enc2), src, n, es, 0xE05);
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail("shaper-cap", "NULL vs sized scratch differ");
		free(es);
		goto out;
	}
	free(es);
	t_pass("shaper-cap");
out:
	free(src);
}

int main(void)
{
	static const size_t R12_12[] = { 12, 12 };
	static const size_t R3X10[] = { 10, 10, 10 };
	static const size_t R12_20[] = { 12, 20 };
	static const size_t R4X8[] = { 8, 8, 8, 8 };
	static const size_t R5X11[] = { 11, 11, 11, 11, 11 };
	static const size_t R300_12[] = { 300, 12 };
	static const size_t R2X100[] = { 100, 100 };
	static const size_t R6X6[] = { 6, 6, 6, 6, 6, 6 };
	static const size_t V3_3[] = { 3, 3 };
	static const size_t V10_10[] = { 10, 10 };
	static const size_t V5_5_5[] = { 5, 5, 5 };
	static const size_t V12_2_12[] = { 12, 2, 12 };
	static const size_t V11_11[] = { 11, 11 };
	static const size_t V10X3[] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
	static const size_t V11X5[] = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 };
	size_t li;

	/* Byte-exact COMP flips (identical bytes all 3 match levels). */
	for (li = 0; li < 3; li++) {
		comp_exact_one(MLEVELS[li], R12_12, 2, EXP_12_12,
			       sizeof(EXP_12_12), "12+12");
		comp_exact_one(MLEVELS[li], R3X10, 3, EXP_3X10,
			       sizeof(EXP_3X10), "3x10");
		comp_exact_one(MLEVELS[li], R12_20, 2, EXP_12_20,
			       sizeof(EXP_12_20), "12+20");
		raw_exact_one(MLEVELS[li], R4X8, 4, "4x8-E2c");
		comp_exact_one(MLEVELS[li], R300_12, 2, EXP_300_12,
			       sizeof(EXP_300_12), "300+12");
	}
	comp_exact_one(0xE05, R5X11, 5, EXP_5X11, sizeof(EXP_5X11),
		       "5x11");
	comp_exact_one(0xE05, R2X100, 2, EXP_2X100, sizeof(EXP_2X100),
		       "2x100");
	raw_exact_one(0xE05, R6X6, 6, "6x6-E2c");

	/* Vetoes hold RAW (TIER-1 / TIER-2 / D==fo / short-run /
	 * boundary / TIER-2-long / k>10-HUF). */
	for (li = 0; li < 3; li++) {
		raw_one(MLEVELS[li], V3_3, 2, "tier1");
		raw_one(MLEVELS[li], V10_10, 2, "tier2");
		raw_one(MLEVELS[li], V5_5_5, 3, "deqfo");
		raw_one(MLEVELS[li], V12_2_12, 3, "shortrun");
		raw_one(MLEVELS[li], V11_11, 2, "boundary");
		raw_one(MLEVELS[li], V10X3, 10, "tier2-long");
		raw_one(MLEVELS[li], V11X5, 11, "huf-k11");
	}

	/* L0 stays RAW (no finding): both-sides pin per M22 T5. */
	raw_one(0xE00, R12_12, 2, "L0-norun");
	raw_one(0xE00, R3X10, 3, "L0-norun");

	cap_one();

	printf("---\nm22-shaper: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
