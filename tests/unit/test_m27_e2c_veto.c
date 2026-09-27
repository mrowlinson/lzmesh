/* SPDX-License-Identifier: 0BSD */
/*
 * test_m27_e2c_veto.c — M27 E2c S5.4 take-gate pins (u26, MERGE-M27 T5b).
 *
 * SPEC-v2: S5.4 (loop iff pos<stop AND pos+9<=size; Q30 PROBABLE:
 * parser 9B look-ahead gate dominates, rem<9 takes NEVER happen),
 * S5.9.a (fail-safe RAW where shape parse invalid), S5.3 (TIER-1
 * D>fo strict; TIER-2 outpos<=n keep), S2.2 (RAW = tag 00 + u32 ds
 * + payload + END), S4.5 (encode caps), S5.1 (determinism),
 * S1.3/Q20 (full-form levels).
 * E2c veto (u26): u19 takes (at 1 + every s_i+1) gated by the S5.4
 * gate — takes increase so rem decreases, so gating the LAST take
 * only is exact (take at n-r_last+1, rem r_last-1; veto iff
 * r_last<10). Vetoed shapes emit canonical RAW via the frozen u1
 * path. MERGE-M27 T5b proof: 6 witness cells x e01/e05/e09 = 18/18
 * lane pins (F1 n29 RAW35, F2 4x8 RAW38, F3 n24 RAW30, F4 n33
 * RAW39, F6 n33 KEPT COMP27, F5 n39 RAW45); SELF + ORACLE-ACCEPTS
 * + port-dec-oracle 18/18; byte-ident 15/18 — F5 x3 NONIDENT (port
 * RAW45 vs oracle COMP32) is the lane-predicted E2b->E1 class move
 * (count-neutral, still ENC_DIFF, both legs exact both directions).
 * Sweep: 173/173 flips inside n24..37, min kept r_last 10.
 * RAW witnesses are pinned byte-exact (canonical RAW is unique, so
 * byte-exactness is oracle-identity for F1-F4; F5 explicitly NOT
 * ident). Kept-COMP cells pin port bytes + legs exact (F6 COMP27 is
 * merge-proven incl ident; boundary pair ident unclaimed).
 * M29 RECORD (2026-09-17, MERGE-M29 round-17 ACCEPT): the 09:57
 * unrecorded edit was u25 SHAPE-R-TERM (COMP35), REFUSED at M28
 * (refused both sides; live oracle emits 32B COMP); u28 reworked
 * the terminator to the M28-T5b oracle-32B layout (lit ZERO
 * overhang, tok 0xC0 kept, ONE term len extra R-3) and M29 merged
 * it after 145/145 3-leg proof (SELF + oracle-IDENT + both cross
 * legs exact on every E2b cell). F5 cells now pin the M29-true 32B
 * oracle-identical COMP (layout per M29 T5B); M27-true RAW45 and
 * u25-drift COMP35 are both oracle-refuted, so any non-32B F5
 * output is a hard FAIL.
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

/* F6 (9,12,12) n33: 27B kept COMP (ds33 bo16 fo16, lit 41 42 43,
 * tok 06 47 47, len-ex 02 REPEAT-packed, modes 0x40, footer
 * 3/2/3/0, END). Last take rem 11 >= 9: no veto. Merge-proven
 * COMP27 + legs + ident (M27 T5b). */
static const uint8_t EXP_F6[] = {
	0x01, 0x21, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x41,
	0x42, 0x43, 0x06, 0x47, 0x47, 0x02, 0x40, 0x00, 0x03, 0x00,
	0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* (12,12,10) n34: 29B kept COMP (ds34 bo18 fo18, lit 41 42 43,
 * tok 07 47 47, len-RAW extras 02 02 00, modes 0, footer 3/3/3/0).
 * Last take rem 9: kept at the gate boundary (r_last=10 vs F4's 9).
 * Port-behavior pin; oracle-identity unclaimed (not merge-proven). */
static const uint8_t EXP_12_12_10[] = {
	0x01, 0x22, 0x00, 0x00, 0x00, 0x12, 0x00, 0x12, 0x00, 0x41,
	0x42, 0x43, 0x07, 0x47, 0x47, 0x02, 0x02, 0x00, 0x00, 0x00,
	0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0xff
};

/* (15,10) n25: 26B kept COMP (ds25 bo15 fo15, lit 41 42, tok 07 47,
 * len-RAW extras 05 00, modes 0, footer 2/2/2/0). Last take rem 9:
 * kept at the gate boundary (r_last=10 vs F3's 9). Port-behavior
 * pin; oracle-identity unclaimed (not merge-proven). */
static const uint8_t EXP_15_10[] = {
	0x01, 0x19, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x41,
	0x42, 0x07, 0x47, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x02, 0x00, 0x00, 0x00, 0xff
};

/* F5 (32,3,4) n39 M29-true: 32B oracle-identical SHAPE-R-TERM COMP
 * (ds39 bo21 fo21, lit 41 42 42 42 43 43 43 43 = 1 live head + R=7
 * trailing, ZERO overhang; tok 07 C0; len 16 04 = live + ONE term
 * extra R-3; modes 0, footer 2/2/8/0, END). MERGE-M29 T5B layout
 * EXACT + SELF-exact + oracle 32B IDENT + both cross legs exact,
 * x e01/e05/e09. */
static const uint8_t EXP_F5_TERM32[] = {
	0x01, 0x27, 0x00, 0x00, 0x00, 0x15, 0x00, 0x15, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x43, 0x43, 0x43, 0x43, 0x07, 0xc0, 0x16,
	0x04, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00,
	0x00, 0xff
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

/* RAW byte-exact pin: canonical-RAW memcmp + sizer + decode-exact +
 * determinism. Canonical RAW is unique, so byte-exactness is
 * oracle-identity where the merge proves oracle-RAW (F1-F4). */
static void raw_exact_one(int level, const size_t *rs, const uint8_t *bs,
			  size_t k, const char *tag)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, bs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	uint8_t *exp = malloc(n + 6);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "e2c-veto L%x %s n=%lu", level, tag,
		 (unsigned long)n);
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

	snprintf(name, sizeof(name), "e2c-veto L%x %s n=%lu", level, tag,
		 (unsigned long)n);
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

/* F5 (32,3,4) n39 M29-true pin: 32B oracle-identical SHAPE-R-TERM
 * COMP (MERGE-M29 T5B; SELF-exact + oracle IDENT + both cross legs
 * exact + determinism). M27-true RAW45 and u25-drift COMP35 are both
 * oracle-refuted — any non-32B output is a hard FAIL (nonzero-wrong
 * decode stays a hard FAIL: corruption guard, never masked). */
static void f5_one(int level, const size_t *rs, const uint8_t *bs, size_t k)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, bs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "e2c-veto L%x F5-E2bE1 n=%lu", level,
		 (unsigned long)n);
	if (!src || !enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret != sizeof(EXP_F5_TERM32) ||
	    memcmp(enc, EXP_F5_TERM32, sizeof(EXP_F5_TERM32)) != 0) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x, want 32 + oracle-ident TERM",
			 (unsigned long)eret, eret ? enc[0] : 0);
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

/* S4.5 caps on the F1 witness: need-1 -> 0, need -> need,
 * NULL==sized. */
static void cap_one(void)
{
	static const size_t RS[] = { 10, 10, 9 };
	static const uint8_t BS[] = { 0x35, 0x0d, 0x45 };
	char detail[128];
	size_t n = 0, eret, eret2;
	uint8_t *src = make_runs(RS, BS, 3, &n);
	uint8_t enc[64], enc2[64];
	void *es;
	size_t esz;

	if (!src) {
		t_fail("e2c-cap", "oom");
		return;
	}
	if (n + 6 != 35) {
		snprintf(detail, sizeof(detail), "witness n=%lu, want 29",
			 (unsigned long)n);
		t_fail("e2c-cap", detail);
		goto out;
	}
	eret = lzmesh_encode(enc, 34, src, n, NULL, 0xE05);
	if (eret != 0) {
		snprintf(detail, sizeof(detail), "need-1 cap -> %lu, want 0",
			 (unsigned long)eret);
		t_fail("e2c-cap", detail);
		goto out;
	}
	eret = lzmesh_encode(enc, 35, src, n, NULL, 0xE05);
	if (eret != 35 || enc[0] != 0x00) {
		snprintf(detail, sizeof(detail), "need cap -> %lu/%02x",
			 (unsigned long)eret, eret ? enc[0] : 0);
		t_fail("e2c-cap", detail);
		goto out;
	}
	esz = lzmesh_encode_scratch_size(0xE05);
	es = malloc(esz ? esz : 1);
	if (!es) {
		t_fail("e2c-cap", "oom");
		goto out;
	}
	eret2 = lzmesh_encode(enc2, sizeof(enc2), src, n, es, 0xE05);
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail("e2c-cap", "NULL vs sized scratch differ");
		free(es);
		goto out;
	}
	free(es);
	t_pass("e2c-cap");
out:
	free(src);
}

int main(void)
{
	/* Witnesses: last-take rem = r_last-1; veto iff < 9. */
	static const size_t F1_RS[] = { 10, 10, 9 };          /* rem 8 */
	static const uint8_t F1_BS[] = { 0x35, 0x0d, 0x45 };
	static const size_t F2_RS[] = { 8, 8, 8, 8 };         /* rem 7 */
	static const uint8_t F2_BS[] = { 0x41, 0x42, 0x43, 0x44 };
	static const size_t F3_RS[] = { 15, 9 };              /* rem 8 */
	static const uint8_t F3_BS[] = { 0x41, 0x42 };
	static const size_t F4_RS[] = { 12, 12, 9 };          /* rem 8 */
	static const size_t F5_RS[] = { 32, 3, 4 };           /* rem 3 */
	static const size_t F6_RS[] = { 9, 12, 12 };          /* rem 11 */
	static const uint8_t B_ABC[] = { 0x41, 0x42, 0x43 };
	static const size_t B1_RS[] = { 12, 12, 10 };         /* rem 9 */
	static const size_t B2_RS[] = { 15, 10 };             /* rem 9 */
	static const uint8_t B_AB[] = { 0x41, 0x42 };
	size_t li;

	/* Witness matrix: all 6 cells x all 3 match levels. F1-F4 =
	 * canonical RAW (oracle-identical); F6 = kept COMP27
	 * (merge-proven); F5 = 32B TERM COMP (M29-true,
	 * oracle-identical per MERGE-M29 T5B). */
	for (li = 0; li < 3; li++) {
		raw_exact_one(MLEVELS[li], F1_RS, F1_BS, 3, "F1-n29");
		raw_exact_one(MLEVELS[li], F2_RS, F2_BS, 4, "F2-4x8");
		raw_exact_one(MLEVELS[li], F3_RS, F3_BS, 2, "F3-n24");
		raw_exact_one(MLEVELS[li], F4_RS, B_ABC, 3, "F4-n33");
		f5_one(MLEVELS[li], F5_RS, B_ABC, 3);
		comp_exact_one(MLEVELS[li], F6_RS, B_ABC, 3,
			       EXP_F6, sizeof(EXP_F6), "F6-kept-n33");
	}

	/* Gate-boundary contrast (e05): r_last 9->10 flips veto->keep
	 * with all else equal (F4/F3 are the veto halves above). */
	comp_exact_one(0xE05, B1_RS, B_ABC, 3,
		       EXP_12_12_10, sizeof(EXP_12_12_10), "r10-kept-n34");
	comp_exact_one(0xE05, B2_RS, B_AB, 2,
		       EXP_15_10, sizeof(EXP_15_10), "r10-kept-n25");

	cap_one();

	printf("---\nm27-e2c-veto: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
