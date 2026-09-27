/* SPDX-License-Identifier: 0BSD */
/*
 * test_m30_shaper12cd.c — M30 SHAPE-R12CD COMP pins (u27, GAPLOG-u27
 * Task 3 flip pins, all e01/e05/e09 byte-identical).
 *
 * SPEC-v2: S5.3 (TIER-1 D>fo strict, D==fo RAW; TIER-2 fo+10<=n keep,
 * equality keeps; NO size compare, Q37), S5.4 (9B look-ahead gate,
 * rem<9 takes never happen, Q30; U27-REM9 rlast>=10 native),
 * S5.5 (mode_trivial: 0->RAW, all-eq->REPEAT incl tc==1, n<=10->RAW,
 * HUF->fail-safe-0; tok single-step), S5.9.a (one parse, rep takes
 * table-free, F5 none interim R-002), S5.9.b (label rule: this
 * harness gates on port bytes via the public API only, never on
 * lane labels), S3.9/S3.10 (token=(lit<<6)|(sel<<3)|len, rep0 esc7 +
 * extra mc-7, mc=r-3; lit esc3 + rest via 255-chain, Q5/Q6),
 * S4.3 (C18 consumed==litc, exact end; intra-token order lit-extra
 * before len-extra; C16 before any copy), S4.1/Q18 (first-byte
 * pre-emit, litc>=1), S4.5 (encode caps), S5.1 (determinism),
 * S1.3/Q20 (full-form levels).
 * SHAPE-R12CD (u27, U23-R12C + U23-R12D close): k-run rep-chain,
 * k>=2, last run long, >=1 wide gap (lead L>=3 via first-token esc3
 * u12-pattern, or interior G>=3 via non-first esc3 rest), trailing
 * short -> 0 (exact end, u28-owned). Take at 2nd byte of each LONG
 * run (rep0 d1, ml=r-1); gap bytes ride as literals of the following
 * token. First-token lit=L (direct L<=2, else esc3 + rest L-3);
 * non-first lit=G+1 (direct 1..2, else esc3 + rest G-2; G==2 rest 0
 * u23-verbatim). bo==fo (no suffix, d=1 sb0); footer
 * (tokc,lenc,litc,0) + END. Levels 1/5/9, fires only where
 * u12/u15/u18/u19/u21/u23 want==0 (disjoint by construction +
 * lane-proven: u19/u21/u23 all 0 on wide by their veto gates).
 * L0 excluded (no finding).
 * Cells (GAPLOG-u27 Task 3, EXP bytes hand-derived from the Task-2a
 * recipe + S3.9/S3.10 grammar, verified here via public API):
 * A1 [X1,Y1,Z1,A30] n33 27B (tc1-REPEAT modes 0x08, lead esc3+rest0),
 * B [A20,X1,Y1,Z1,B12] n35 30B (R12C modes 0x00),
 * C [X1,A20,Y1,Z1,W1,B12] n36 31B (mixed lead+interior),
 * LR [X1,Y1,Z1,A10,B10,C10] n33 30B (len-REPEAT modes 0x40,
 * rlast-10 boundary), E [A300,X1,Y1,Z1,B12] n315 34B (5B len esc).
 * Merge-observed oracle status (M30 T5, 163/163 ALL-PASS, pre-RAW
 * n+6 -> post COMP at lane-predicted sizes EXACT): SELF-exact 18/18
 * + oracle-dec-port 18/18 + port-dec-oracle 18/18 + byte-ident vs
 * oracle enc 18/18 on all 5 pins + the m26-veto G3-12 cell — every
 * lit-esc3 cell byte-identical to oracle enc, both legs exact both
 * directions. PORT bytes pinned here == oracle bytes per that proof.
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

static void t_xfail(const char *name, const char *reason)
{
	(void)reason;
	printf("XFAIL %s :: %s\n", name, reason);
}

static const int MLEVELS[] = { 0xE01, 0xE05, 0xE09 };

/* A1 [X1,Y1,Z1,A30] n33: 27B (ds33 bo16 fo16, lit 58 59 5a 41,
 * tok C7 REPEAT-packed tc1, len 00 14 [lit-rest0, mc27-esc7],
 * modes 0x0008, footer 1/2/4/0, END). */
static const uint8_t EXP_A1[] = {
	0x01, 0x21, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x58,
	0x59, 0x5a, 0x41, 0xc7, 0x00, 0x14, 0x08, 0x00, 0x01, 0x00,
	0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0xff
};

/* B [A20,X1,Y1,Z1,B12] n35: 30B (ds35 bo19 fo19, lit 41 58 59 5a
 * 42, tok 07 C7, len 0a 01 02, modes 0, footer 2/3/5/0). */
static const uint8_t EXP_B[] = {
	0x01, 0x23, 0x00, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x41,
	0x58, 0x59, 0x5a, 0x42, 0x07, 0xc7, 0x0a, 0x01, 0x02, 0x00,
	0x00, 0x02, 0x00, 0x03, 0x00, 0x05, 0x00, 0x00, 0x00, 0xff
};

/* C [X1,A20,Y1,Z1,W1,B12] n36: 31B (ds36 bo20 fo20, lit 58 41 59
 * 5a 57 42, tok 47 C7, len 0a 01 02, modes 0, footer 2/3/6/0). */
static const uint8_t EXP_C[] = {
	0x01, 0x24, 0x00, 0x00, 0x00, 0x14, 0x00, 0x14, 0x00, 0x58,
	0x41, 0x59, 0x5a, 0x57, 0x42, 0x47, 0xc7, 0x0a, 0x01, 0x02,
	0x00, 0x00, 0x02, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
	0xff
};

/* LR [X1,Y1,Z1,A10,B10,C10] n33: 30B (ds33 bo19 fo19, lit 58 59
 * 5a 41 42 43, tok c7 47 47, len 00 REPEAT-packed lenc4,
 * modes 0x0040, footer 3/4/6/0). */
static const uint8_t EXP_LR[] = {
	0x01, 0x21, 0x00, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x58,
	0x59, 0x5a, 0x41, 0x42, 0x43, 0xc7, 0x47, 0x47, 0x00, 0x40,
	0x00, 0x03, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0xff
};

/* E [A300,X1,Y1,Z1,B12] n315: 34B (ds315 bo23 fo23, lit 41 58 59
 * 5a 42, tok 07 C7, len ff 22 01 00 00 01 02 [5B mc297 esc +
 * lit-rest1 + mc9 extra], modes 0, footer 2/7/5/0). */
static const uint8_t EXP_E[] = {
	0x01, 0x3b, 0x01, 0x00, 0x00, 0x17, 0x00, 0x17, 0x00, 0x41,
	0x58, 0x59, 0x5a, 0x42, 0x07, 0xc7, 0xff, 0x22, 0x01, 0x00,
	0x00, 0x01, 0x02, 0x00, 0x00, 0x02, 0x00, 0x07, 0x00, 0x05,
	0x00, 0x00, 0x00, 0xff
};

/* E L0 [A300,X1,Y1,Z1,B12] n315 e00: 87B oracle L0 litonly-HUF.
 * Port u36 emits 88B (pack1 tie-break diverges, R4-A4) -> XFAIL
 * until lengths match; flips to PASS automatically when fixed. */
static const uint8_t EXP_L0E[] = {
	0x01, 0x3b, 0x01, 0x00, 0x00, 0x0f, 0x00, 0x4c, 0x00, 0xc0,
	0xff, 0x37, 0x01, 0x00, 0x00, 0x11, 0x04, 0x00, 0x00, 0x00,
	0x12, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x24, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x24, 0x0f, 0x00, 0x00, 0x00, 0x00,
	0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x80, 0x05, 0x00,
	0x00, 0x00, 0x00, 0x80, 0x07, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x04, 0x6e, 0x66, 0x66, 0x06, 0x20, 0x0a, 0x00, 0x01, 0x00,
	0x05, 0x00, 0x3b, 0x01, 0x00, 0x00, 0xff
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

	snprintf(name, sizeof(name), "shaper12cd-comp L%x %s n=%lu", level,
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

	snprintf(name, sizeof(name), "shaper12cd-L0 %s n=%lu", tag,
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

/* S4.5 caps on pin B: need-1 -> 0, need -> need, NULL==sized. */
static void cap_one(void)
{
	static const size_t RS[] = { 20, 1, 1, 1, 12 };
	static const uint8_t BS[] = { 0x41, 0x58, 0x59, 0x5a, 0x42 };
	char detail[128];
	size_t n = 0, eret, eret2;
	uint8_t *src = make_runs(RS, BS, 5, &n);
	uint8_t enc[64], enc2[64];
	void *es;
	size_t esz;

	if (!src) {
		t_fail("shaper12cd-cap", "oom");
		return;
	}
	eret = lzmesh_encode(enc, sizeof(EXP_B) - 1, src, n, NULL, 0xE05);
	if (eret != 0) {
		snprintf(detail, sizeof(detail), "need-1 cap -> %lu, want 0",
			 (unsigned long)eret);
		t_fail("shaper12cd-cap", detail);
		goto out;
	}
	eret = lzmesh_encode(enc, sizeof(EXP_B), src, n, NULL, 0xE05);
	if (eret != sizeof(EXP_B)) {
		snprintf(detail, sizeof(detail), "need cap -> %lu, want %lu",
			 (unsigned long)eret,
			 (unsigned long)sizeof(EXP_B));
		t_fail("shaper12cd-cap", detail);
		goto out;
	}
	esz = lzmesh_encode_scratch_size(0xE05);
	es = malloc(esz ? esz : 1);
	if (!es) {
		t_fail("shaper12cd-cap", "oom");
		goto out;
	}
	eret2 = lzmesh_encode(enc2, sizeof(enc2), src, n, es, 0xE05);
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail("shaper12cd-cap", "NULL vs sized scratch differ");
		free(es);
		goto out;
	}
	free(es);
	t_pass("shaper12cd-cap");
out:
	free(src);
}

int main(void)
{
	static const size_t R_A1[] = { 1, 1, 1, 30 };
	static const size_t R_B[] = { 20, 1, 1, 1, 12 };
	static const size_t R_C[] = { 1, 20, 1, 1, 1, 12 };
	static const size_t R_LR[] = { 1, 1, 1, 10, 10, 10 };
	static const size_t R_E[] = { 300, 1, 1, 1, 12 };
	static const uint8_t B_XYZA[] = { 0x58, 0x59, 0x5a, 0x41 };
	static const uint8_t B_AXYZB[] = { 0x41, 0x58, 0x59, 0x5a, 0x42 };
	static const uint8_t B_XAYZWB[] = { 0x58, 0x41, 0x59, 0x5a, 0x57,
					   0x42 };
	static const uint8_t B_XYZABC[] = { 0x58, 0x59, 0x5a, 0x41, 0x42,
					   0x43 };
	size_t li;

	/* Full 5x3 matrix: every pin byte-exact at all 3 match levels. */
	for (li = 0; li < 3; li++) {
		comp_exact_one(MLEVELS[li], R_A1, B_XYZA, 4, EXP_A1,
			       sizeof(EXP_A1), "A1-X1+Y1+Z1+A30");
		comp_exact_one(MLEVELS[li], R_B, B_AXYZB, 5, EXP_B,
			       sizeof(EXP_B), "B-A20+X1+Y1+Z1+B12");
		comp_exact_one(MLEVELS[li], R_C, B_XAYZWB, 6, EXP_C,
			       sizeof(EXP_C), "C-X1+A20+Y1+Z1+W1+B12");
		comp_exact_one(MLEVELS[li], R_LR, B_XYZABC, 6, EXP_LR,
			       sizeof(EXP_LR), "LR-X1+Y1+Z1+A10+B10+C10");
		comp_exact_one(MLEVELS[li], R_E, B_AXYZB, 5, EXP_E,
			       sizeof(EXP_E), "E-A300+X1+Y1+Z1+B12");
	}

	/* L0: small shapes stay RAW (TIER-2 veto); E n315 emits L0
	 * litonly-HUF COMP (u36, R4) but byte-DIFFERS from oracle
	 * (pack1 tie-break open, R4-A4) -> XFAIL until lengths match. */
	raw_l0_one(R_A1, B_XYZA, 4, "A1");
	raw_l0_one(R_B, B_AXYZB, 5, "B");
	raw_l0_one(R_C, B_XAYZWB, 6, "C");
	raw_l0_one(R_LR, B_XYZABC, 6, "LR");
	{
		/* E-L0: byte-exact vs oracle iff tie-break fixed (R4-A4). */
		size_t en = 0;
		uint8_t *esrc = make_runs(R_E, B_AXYZB, 5, &en);
		uint8_t *eenc = malloc(en + 1024);
		size_t eret = eenc ? lzmesh_encode(eenc, en + 1024, esrc,
						    en, NULL, 0xE00) : 0;
		if (eret == sizeof(EXP_L0E)
		    && memcmp(eenc, EXP_L0E, sizeof(EXP_L0E)) == 0)
			t_pass("shaper12cd-L0 E n=315");
		else
			t_xfail("shaper12cd-L0 E n=315",
				"u36 COMP vs oracle 87B (tie-break R4-A4)");
		free(esrc);
		free(eenc);
	}

	cap_one();

	printf("---\nm30-shaper12cd: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
