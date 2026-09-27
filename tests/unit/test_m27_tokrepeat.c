/* SPDX-License-Identifier: 0BSD */
/*
 * test_m27_tokrepeat.c — M27 U21-TOKREPEAT 21-cell ident pins (u24).
 *
 * SPEC-v2: S5.5 (mode tests exact: 0->RAW; all-equal->REPEAT;
 * n<=10->RAW), S3.2 (REPEAT = one byte x count const fill), S2.5
 * (modes m1|m2<<3|m3<<6|m4<<9 = lit/tok/len/dist; COUNT slots
 * tok,len,lit,dist), S5.3 (TIER-1 D>fo strict; TIER-2 keep), S5.1
 * (determinism), S1.3/Q20 (full-form levels). A single token is
 * trivially all-equal, so tc==1 takes REPEAT too (lead1solo
 * modes-only delta 0x40->0x48).
 * U21-TOKREPEAT (u24, MERGE-M27 T5a): the u21 tok lane now follows
 * S5.5 — all-equal tok pairs emit tok-REPEAT (1B) instead of
 * tok-RAW (2B). Merge-7 x e01/e05/e09 = 21/21 byte-identical to
 * the oracle (was 9/21 at M25): 12/12 flips lane-predicted EXACT
 * (lead1 26->25, solo modes-only 24, len 27->26, esc 31->30) +
 * 9/9 frozen byte-identical to pre-merge capture (lead2 27B,
 * litsingle 26B, singleton3 30B). SELF-exact + ORACLE-ACCEPTS +
 * port-dec-oracle 21/21; e00 RAW n+6 7/7. Expected bytes below are
 * the post-u24 oracle-identical COMP pins (same bytes as the
 * re-pinned test_m25_shaper12.c pins, corroborated independently
 * per M27 T4 NOTE); this file extends them to the full 21-cell
 * ident matrix + the 4 L0 pins m25 lacks.
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

/* lead1 [X1,A12,B12] n25: 25B oracle-identical (ds25 bo14 fo14,
 * lit 58 41 42, single tok 47 REPEAT-packed, len-ex 02,
 * modes 0x48, footer 2/2/3/0, END). Flip: was 26B
 * tok-RAW 47 47 / modes 0x40. */
static const uint8_t EXP_LEAD1[] = {
	0x01, 0x19, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x58,
	0x41, 0x42, 0x47, 0x02, 0x48, 0x00, 0x02, 0x00, 0x02, 0x00,
	0x03, 0x00, 0x00, 0x00, 0xff
};

/* lead2 [X2,A12,B12] n26: 27B oracle-identical (tok 87 47 differ
 * -> RAW kept, litc 4). Frozen (ident already at M25). */
static const uint8_t EXP_LEAD2[] = {
	0x01, 0x1a, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x58,
	0x58, 0x41, 0x42, 0x87, 0x47, 0x02, 0x40, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0xff
};

/* litsingle [A12,X1,B12] n25: 26B oracle-identical (tok 07 87
 * differ -> RAW kept, U21-LIT2). Frozen. */
static const uint8_t EXP_LITSINGLE[] = {
	0x01, 0x19, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x41,
	0x58, 0x42, 0x07, 0x87, 0x02, 0x40, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* lead1solo [X1,A24] n25: 24B oracle-identical (single tok 47,
 * single len extra 0e REPEAT-packed, modes 0x48, footer 1/1/2/0).
 * Flip is modes-byte-only (0x40->0x48, same length). */
static const uint8_t EXP_SOLO[] = {
	0x01, 0x19, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x0d, 0x00, 0x58,
	0x41, 0x47, 0x0e, 0x48, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02,
	0x00, 0x00, 0x00, 0xff
};

/* lead1len [X1,A20,B12] n33: 26B oracle-identical (single tok 47,
 * len-RAW extras 0a 02, modes 0x08 tok-REPEAT + len-RAW).
 * Flip: was 27B tok-RAW 47 47 / modes 0. */
static const uint8_t EXP_LEN[] = {
	0x01, 0x21, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x58,
	0x41, 0x42, 0x47, 0x0a, 0x02, 0x08, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* lead1esc [X1,A300,B12] n313: 30B oracle-identical (single tok 47,
 * 5B len esc ff+290 LE + 02, modes 0x08, footer 2/6/3/0).
 * Flip: was 31B tok-RAW 47 47 / modes 0. */
static const uint8_t EXP_ESC[] = {
	0x01, 0x39, 0x01, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x58,
	0x41, 0x42, 0x47, 0xff, 0x22, 0x01, 0x00, 0x00, 0x02, 0x08,
	0x00, 0x02, 0x00, 0x06, 0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* lead1esc L0 [X1,A300,B12] n313: 87B oracle-identical L0
 * litonly-HUF (u36, R4; tc1/dc0/litc==ds, tok 0xc0). R3-A2 close:
 * L0 non-runs emit COMP, not RAW. */
static const uint8_t EXP_L0ESC[] = {
	0x01, 0x39, 0x01, 0x00, 0x00, 0x0f, 0x00, 0x4c, 0x00, 0xc0,
	0xff, 0x35, 0x01, 0x00, 0x00, 0x91, 0x00, 0x00, 0x00, 0x00,
	0x12, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x28, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x02, 0x6e, 0x66, 0x66, 0x06, 0x20, 0x0a, 0x00, 0x01, 0x00,
	0x05, 0x00, 0x39, 0x01, 0x00, 0x00, 0xff
};

/* singleton3 [X1,A12,Y1,B12,Z1,C12] n39: 30B oracle-identical
 * (tok 47 87 87 differ -> RAW kept, U21-LIT2 x2, footer 3/3/6/0).
 * Frozen. */
static const uint8_t EXP_SING3[] = {
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

	snprintf(name, sizeof(name), "tokrepeat-ident L%x %s n=%lu", level,
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

	snprintf(name, sizeof(name), "tokrepeat-L0 %s n=%lu", tag,
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

int main(void)
{
	static const size_t R_LEAD1[] = { 1, 12, 12 };
	static const size_t R_LEAD2[] = { 2, 12, 12 };
	static const size_t R_LITS[] = { 12, 1, 12 };
	static const size_t R_SOLO[] = { 1, 24 };
	static const size_t R_LEN[] = { 1, 20, 12 };
	static const size_t R_ESC[] = { 1, 300, 12 };
	static const size_t R_SING3[] = { 1, 12, 1, 12, 1, 12 };
	static const uint8_t B_XAB[] = { 0x58, 0x41, 0x42 };
	static const uint8_t B_AXB[] = { 0x41, 0x58, 0x42 };
	static const uint8_t B_XA[] = { 0x58, 0x41 };
	static const uint8_t B_3S[] = { 0x58, 0x41, 0x59, 0x42, 0x5a, 0x43 };
	size_t li;

	/* Full ident matrix: merge-7 x all 3 match levels (identical
	 * bytes per shape; 12 flip cells + 9 frozen cells). */
	for (li = 0; li < 3; li++) {
		comp_exact_one(MLEVELS[li], R_LEAD1, B_XAB, 3,
			       EXP_LEAD1, sizeof(EXP_LEAD1), "lead1");
		comp_exact_one(MLEVELS[li], R_LEAD2, B_XAB, 3,
			       EXP_LEAD2, sizeof(EXP_LEAD2), "lead2");
		comp_exact_one(MLEVELS[li], R_LITS, B_AXB, 3,
			       EXP_LITSINGLE, sizeof(EXP_LITSINGLE),
			       "litsingle");
		comp_exact_one(MLEVELS[li], R_SOLO, B_XA, 2,
			       EXP_SOLO, sizeof(EXP_SOLO), "lead1solo");
		comp_exact_one(MLEVELS[li], R_LEN, B_XAB, 3,
			       EXP_LEN, sizeof(EXP_LEN), "lead1len");
		comp_exact_one(MLEVELS[li], R_ESC, B_XAB, 3,
			       EXP_ESC, sizeof(EXP_ESC), "lead1esc");
		comp_exact_one(MLEVELS[li], R_SING3, B_3S, 6,
			       EXP_SING3, sizeof(EXP_SING3), "singleton3");
	}

	/* L0: small shapes stay RAW (TIER-2 veto); lead1esc n313
	 * emits 87B L0 litonly-HUF COMP (u36, R4; oracle-identical).
	 * Old "stays RAW on all 7" note predates R3-A2. */
	raw_l0_one(R_LEAD2, B_XAB, 3, "lead2");
	raw_l0_one(R_SOLO, B_XA, 2, "lead1solo");
	raw_l0_one(R_LEN, B_XAB, 3, "lead1len");
	comp_exact_one(0xE00, R_ESC, B_XAB, 3,
		       EXP_L0ESC, sizeof(EXP_L0ESC), "lead1esc-L0");

	printf("---\nm27-tokrepeat: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
