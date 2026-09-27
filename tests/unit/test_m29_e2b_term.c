/* SPDX-License-Identifier: 0BSD */
/*
 * test_m29_e2b_term.c — M29 E2b SHAPE-R-TERM oracle-32B pins (u28).
 *
 * SPEC-v2: S5.3.a (terminator rep0 len-2 tok 0xC0; class R>=3
 * escape+C0; lc = R-3>=255?5:1; non-final overhang-2 emitted),
 * S5.4 (parser 9B look-ahead gate pos+9<=size, Q30 PROBABLE:
 * rem<9 takes NEVER happen), S5.9.a (fail-safe RAW where shape
 * parse invalid), S2.2 (RAW = tag 00 + u32 ds + payload + END),
 * S4.5 (encode caps), S5.1 (determinism), S1.3/Q20 (full-form
 * levels), S2.8 (terminator overhang clamp).
 * SHAPE-R-TERM (u28 rework of refuted u25, MERGE-M29): k-run
 * rep-chain takes with a 9B-blocked trailing take close via an
 * S5.3.a terminator packed to the M28-T5b oracle-32B layout: lit
 * ZERO overhang (litc=j+R), tok 0xC0 kept, len ONE term extra
 * R-3 (lenC=live+1). U25-OVH + U25-LENORD closed per the M28 pin.
 * MERGE-M29 T5 proof (145/145 x3, transient probe): T5A T5b-pin
 * family byte-identical to the pinned oracle hex; T5B n39 ABC
 * layout EXACT + SELF + oracle IDENT + both cross legs; T5C sweep
 * (r0,3,4) n19/n27 both-RAW agree, n39/n107 32B IDENT, n307 36B
 * IDENT, all legs exact; T5D E2c coherence (F1-F4 RAW + F6 COMP27
 * kept, covered in test_m27_e2c_veto); T5E freeze (n24/n30,
 * e00 F5 RAW45). T6 VERDICT: E2b CLOSED as oracle-identical
 * roundtrip. E2c veto-removal direction safe (HUF/TIER veto keeps
 * E2c cells RAW: litc>10 term HUF veto, e.g. n29).
 * Cell classes below: T5b x3 = oracle-identical (M29 T5A);
 * sweep sizes IDENT (M29 T5C), ABC fill = port-behavior byte pin
 * + full legs; R-sweep (32,R) R=3..9 = port-behavior byte pins
 * (term len byte R-3, counts tokc2/litc1+R, SELF-exact 7/7,
 * corroborating GAPLOG-u28 Task 3) + full legs; (32,10) 9B-gate
 * contrast (last take rem 9 -> regular u19 exact-end COMP, no
 * terminator) = port-behavior pin + legs; e00 shapes = canonical
 * RAW (u28 fires only at 1/5/9; M29 T5E).
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

/* T5b pin (32x23+3xAA+4xBB) n39: 32B oracle-identical (M28 T5b hex,
 * M29 T5A byte-identical x3 runs: tag 01 ds39 bo21 fo21, lit 8B =
 * 1 live head + R=7 trailing ZERO overhang, tok 07 C0, len 16 04
 * = live + ONE term extra R-3, modes 0, footer 2/2/8/0, END). */
static const uint8_t EXP_T5B[] = {
	0x01, 0x27, 0x00, 0x00, 0x00, 0x15, 0x00, 0x15, 0x00, 0x23,
	0xaa, 0xaa, 0xaa, 0xbb, 0xbb, 0xbb, 0xbb, 0x07, 0xc0, 0x16,
	0x04, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00,
	0x00, 0xff
};

/* Sweep (100,3,4) n107 e05: 32B (ds107 bo21 fo21, same term frame
 * as T5b; live len extra 0x5a = mc97-7; footer 2/2/8/0). Size 32B
 * IDENT per M29 T5C; ABC fill port-behavior byte pin + legs. */
static const uint8_t EXP_SWEEP107[] = {
	0x01, 0x6b, 0x00, 0x00, 0x00, 0x15, 0x00, 0x15, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x43, 0x43, 0x43, 0x43, 0x07, 0xc0, 0x5a,
	0x04, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00,
	0x00, 0xff
};

/* Sweep (300,3,4) n307 e05: 36B (ds307 bo25 fo25; live len esc
 * ff + u32le 0x122 = mc297-7 chain per S3.10/Q5; term extra 04;
 * footer tokc2 lenc6 litc8 distc0). Size 36B IDENT per M29 T5C;
 * ABC fill port-behavior byte pin + legs. */
static const uint8_t EXP_SWEEP307[] = {
	0x01, 0x33, 0x01, 0x00, 0x00, 0x19, 0x00, 0x19, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x43, 0x43, 0x43, 0x43, 0x07, 0xc0, 0xff,
	0x22, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x02, 0x00, 0x06,
	0x00, 0x08, 0x00, 0x00, 0x00, 0xff
};

/* L0 sweep (100,3,4) n107 e00: 54B oracle-identical L0
 * litonly-HUF (u36, R4). L0 sweep (300,3,4) n307 e00: 82B
 * oracle-identical. R3-A2 close: L0 non-runs emit COMP. */
static const uint8_t EXP_L0SWEEP107[] = {
	0x01, 0x6b, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x2b, 0x00, 0xc0,
	0x67, 0x91, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x80, 0x01, 0x01, 0x80, 0x01, 0x03, 0x80, 0x01, 0x03, 0x00,
	0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x60, 0x3b, 0x23,
	0x22, 0x02, 0x20, 0x4a, 0x00, 0x01, 0x00, 0x01, 0x00, 0x6b,
	0x00, 0x00, 0x00, 0xff
};
static const uint8_t EXP_L0SWEEP307[] = {
	0x01, 0x33, 0x01, 0x00, 0x00, 0x0f, 0x00, 0x47, 0x00, 0xc0,
	0xff, 0x2f, 0x01, 0x00, 0x00, 0x91, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00,
	0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x40, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
	0x40, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x6e, 0x56, 0x55, 0x05,
	0x20, 0x0a, 0x00, 0x01, 0x00, 0x05, 0x00, 0x33, 0x01, 0x00,
	0x00, 0xff
};

/* R-sweep (32,R) e05, R=3..9: (28+R-3)B TERM COMP. lit = head 41
 * + R trailing 42s (litc 1+R, ZERO overhang); tok 07 C0; len =
 * live 0x16 + term extra R-3; tokc2; modes 0; distc0. Term byte
 * R-3 + counts + SELF-exact corroborate GAPLOG-u28 Task 3 (7/7).
 * Port-behavior byte pins + full legs (oracle-identity unclaimed
 * beyond the n39-pin + sweep sizes). */
static const uint8_t EXP_R3[] = {
	0x01, 0x23, 0x00, 0x00, 0x00, 0x11, 0x00, 0x11, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x07, 0xc0, 0x16, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0xff
};

static const uint8_t EXP_R4[] = {
	0x01, 0x24, 0x00, 0x00, 0x00, 0x12, 0x00, 0x12, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x42, 0x07, 0xc0, 0x16, 0x01, 0x00, 0x00,
	0x02, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0xff
};

static const uint8_t EXP_R5[] = {
	0x01, 0x25, 0x00, 0x00, 0x00, 0x13, 0x00, 0x13, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x42, 0x42, 0x07, 0xc0, 0x16, 0x02, 0x00,
	0x00, 0x02, 0x00, 0x02, 0x00, 0x06, 0x00, 0x00, 0x00, 0xff
};

static const uint8_t EXP_R6[] = {
	0x01, 0x26, 0x00, 0x00, 0x00, 0x14, 0x00, 0x14, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x07, 0xc0, 0x16, 0x03,
	0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x07, 0x00, 0x00, 0x00,
	0xff
};

static const uint8_t EXP_R7[] = {
	0x01, 0x27, 0x00, 0x00, 0x00, 0x15, 0x00, 0x15, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x07, 0xc0, 0x16,
	0x04, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00,
	0x00, 0xff
};

static const uint8_t EXP_R8[] = {
	0x01, 0x28, 0x00, 0x00, 0x00, 0x16, 0x00, 0x16, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x07, 0xc0,
	0x16, 0x05, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x09, 0x00,
	0x00, 0x00, 0xff
};

static const uint8_t EXP_R9[] = {
	0x01, 0x29, 0x00, 0x00, 0x00, 0x17, 0x00, 0x17, 0x00, 0x41,
	0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x07,
	0xc0, 0x16, 0x06, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x0a,
	0x00, 0x00, 0x00, 0xff
};

/* 9B-gate contrast (32,10) n42 e05: 26B regular u19 exact-end COMP
 * (ds42 bo15 fo15, lit 41 42, tok 07 47 live rep0 — NO C0,
 * len 16 00, modes 0, footer 2/2/2/0). Last take rem 9 passes
 * the S5.4 gate, so no terminator fires. Port-behavior pin + legs. */
static const uint8_t EXP_BOUND3210[] = {
	0x01, 0x2a, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x41,
	0x42, 0x07, 0x47, 0x16, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x02, 0x00, 0x00, 0x00, 0xff
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

	snprintf(name, sizeof(name), "e2b-term L%x %s n=%lu", level, tag,
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

/* RAW byte-exact pin: canonical-RAW memcmp + sizer + decode-exact +
 * determinism. Canonical RAW is unique, so byte-exactness is
 * oracle-identity where the merge proves oracle-RAW (T5C n19/n27
 * both-RAW agree; T5E e00 F5 RAW45). */
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

	snprintf(name, sizeof(name), "e2b-term L%x %s n=%lu", level, tag,
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

/* S4.5 caps on the T5b pin: need-1 -> 0, need -> need, NULL==sized. */
static void cap_one(void)
{
	static const size_t RS[] = { 32, 3, 4 };
	static const uint8_t BS[] = { 0x23, 0xaa, 0xbb };
	char detail[128];
	size_t n = 0, eret, eret2;
	uint8_t *src = make_runs(RS, BS, 3, &n);
	uint8_t enc[64], enc2[64];
	void *es;
	size_t esz;

	if (!src) {
		t_fail("e2b-cap", "oom");
		return;
	}
	if (n != 39 || sizeof(EXP_T5B) != 32) {
		snprintf(detail, sizeof(detail), "witness n=%lu pin=%lu",
			 (unsigned long)n,
			 (unsigned long)sizeof(EXP_T5B));
		t_fail("e2b-cap", detail);
		goto out;
	}
	eret = lzmesh_encode(enc, 31, src, n, NULL, 0xE05);
	if (eret != 0) {
		snprintf(detail, sizeof(detail), "need-1 cap -> %lu, want 0",
			 (unsigned long)eret);
		t_fail("e2b-cap", detail);
		goto out;
	}
	eret = lzmesh_encode(enc, 32, src, n, NULL, 0xE05);
	if (eret != 32 || enc[0] != 0x01 ||
	    memcmp(enc, EXP_T5B, 32) != 0) {
		snprintf(detail, sizeof(detail), "need cap -> %lu/%02x",
			 (unsigned long)eret, eret ? enc[0] : 0);
		t_fail("e2b-cap", detail);
		goto out;
	}
	esz = lzmesh_encode_scratch_size(0xE05);
	es = malloc(esz ? esz : 1);
	if (!es) {
		t_fail("e2b-cap", "oom");
		goto out;
	}
	eret2 = lzmesh_encode(enc2, sizeof(enc2), src, n, es, 0xE05);
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail("e2b-cap", "NULL vs sized scratch differ");
		free(es);
		goto out;
	}
	free(es);
	t_pass("e2b-cap");
out:
	free(src);
}

int main(void)
{
	static const size_t T5B_RS[] = { 32, 3, 4 };
	static const uint8_t T5B_BS[] = { 0x23, 0xaa, 0xbb };
	static const size_t S19_RS[] = { 12, 3, 4 };
	static const size_t S27_RS[] = { 20, 3, 4 };
	static const size_t S107_RS[] = { 100, 3, 4 };
	static const size_t S307_RS[] = { 300, 3, 4 };
	static const size_t F5_RS[] = { 32, 3, 4 };
	static const uint8_t B_ABC[] = { 0x41, 0x42, 0x43 };
	static const size_t R3_RS[] = { 32, 3 };
	static const size_t R4_RS[] = { 32, 4 };
	static const size_t R5_RS[] = { 32, 5 };
	static const size_t R6_RS[] = { 32, 6 };
	static const size_t R7_RS[] = { 32, 7 };
	static const size_t R8_RS[] = { 32, 8 };
	static const size_t R9_RS[] = { 32, 9 };
	static const size_t B3210_RS[] = { 32, 10 };
	static const uint8_t B_AB[] = { 0x41, 0x42 };
	size_t li;

	/* T5b oracle-32B pin x all 3 match levels (M29 T5A IDENT). */
	for (li = 0; li < 3; li++)
		comp_exact_one(MLEVELS[li], T5B_RS, T5B_BS, 3,
			       EXP_T5B, sizeof(EXP_T5B), "T5b-pin");

	/* Sweep (r0,3,4) e05 (M29 T5C): n19/n27 both-RAW agree,
	 * n107 32B IDENT, n307 36B IDENT. */
	raw_exact_one(0xE05, S19_RS, B_ABC, 3, "sweep-n19-RAW");
	raw_exact_one(0xE05, S27_RS, B_ABC, 3, "sweep-n27-RAW");
	comp_exact_one(0xE05, S107_RS, B_ABC, 3,
		       EXP_SWEEP107, sizeof(EXP_SWEEP107), "sweep-n107");
	comp_exact_one(0xE05, S307_RS, B_ABC, 3,
		       EXP_SWEEP307, sizeof(EXP_SWEEP307), "sweep-n307");

	/* R-sweep (32,R) e05, R=3..9: term len byte R-3. */
	comp_exact_one(0xE05, R3_RS, B_AB, 2,
		       EXP_R3, sizeof(EXP_R3), "R3-term");
	comp_exact_one(0xE05, R4_RS, B_AB, 2,
		       EXP_R4, sizeof(EXP_R4), "R4-term");
	comp_exact_one(0xE05, R5_RS, B_AB, 2,
		       EXP_R5, sizeof(EXP_R5), "R5-term");
	comp_exact_one(0xE05, R6_RS, B_AB, 2,
		       EXP_R6, sizeof(EXP_R6), "R6-term");
	comp_exact_one(0xE05, R7_RS, B_AB, 2,
		       EXP_R7, sizeof(EXP_R7), "R7-term");
	comp_exact_one(0xE05, R8_RS, B_AB, 2,
		       EXP_R8, sizeof(EXP_R8), "R8-term");
	comp_exact_one(0xE05, R9_RS, B_AB, 2,
		       EXP_R9, sizeof(EXP_R9), "R9-term");

	/* 9B-gate contrast: (32,10) regular exact-end COMP. */
	comp_exact_one(0xE05, B3210_RS, B_AB, 2,
		       EXP_BOUND3210, sizeof(EXP_BOUND3210), "gate-rem9");

	/* L0: small term shapes hold RAW (TIER-2 veto); n107/n307
	 * emit L0 litonly-HUF COMP (u36, R4; oracle-identical 54B/82B).
	 * Old "holds RAW" note predates R3-A2. */
	raw_exact_one(0xE00, F5_RS, B_ABC, 3, "L0-F5shape");
	raw_exact_one(0xE00, T5B_RS, T5B_BS, 3, "L0-T5bshape");
	comp_exact_one(0xE00, S107_RS, B_ABC, 3,
		       EXP_L0SWEEP107, sizeof(EXP_L0SWEEP107),
		       "L0-sweep-n107");
	comp_exact_one(0xE00, S307_RS, B_ABC, 3,
		       EXP_L0SWEEP307, sizeof(EXP_L0SWEEP307),
		       "L0-sweep-n307");

	cap_one();

	printf("---\nm29-e2b-term: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
