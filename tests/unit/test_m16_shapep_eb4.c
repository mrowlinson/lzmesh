/* SPDX-License-Identifier: 0BSD */
/*
 * test_m16_shapep_eb4.c — M16 encoder pins: SHAPE-P (u12) + E-B4 (u14).
 *
 * SPEC-v2: S3.9/S3.10 (token_new grammar, N35 `7f`+`00` = mc31 ml33),
 * S5.3 (TIER-1 D>fo, TIER-2 outpos<=n keep, Q18 first-block litc!=0),
 * S5.5 (mode_trivial: small-count no-H stays RAW/REPEAT per stream),
 * S5.7c (L0 litonly vs L1/L5/L9 match rows), S5.8 L5 short rule
 * (E-B4: skip iff 4*Dlen>Dsb+4+P with RAW SIGNED ds, Q33 PROBABLE
 * caller-flippable; App-E E-B4 = Dsb clamp>=0 IMPL-DEVIATION),
 * S5.1 (determinism), S1.3/Q20 (full-form levels).
 * SHAPE-P (u12): p-periodic 2..8 non-run single-token new-dist no-H
 * COMP at L1/L5/L9 (bo==fo fetch-order layout, footer (1,lc,p,1));
 * N35 pin (altAB35 = token_new(1,31) EXACTLY); minimal fire p=2
 * n>=23; L0 periodic TIER-2-vetoes to RAW; G4 rep2-live kill
 * ("AABA"x100 -> RAW). Merge-observed oracle byte-identity: altAB35
 * 25B + period7-n100 31B (M16). E-B4 skip is landed-but-dead (zero
 * callers): short-rule cells assert roundtrip + determinism only
 * (normative NOW); NO skip byte-identity claim (pending per Q33).
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

static uint32_t xs32(uint32_t *s)
{
	uint32_t x = *s;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

static const int MLEVELS[] = { 0xE01, 0xE05, 0xE09 };

/* Encode must emit `want_tag`, size exactly, and decode byte-exact. */
static void shape_one(int level, const uint8_t *src, size_t n,
		      uint8_t want_tag, const char *tag)
{
	char name[128], detail[192];
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr;

	snprintf(name, sizeof(name), "shapep L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (eret == 0 || enc[0] != want_tag) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x want %02x",
			 (unsigned long)eret, eret ? enc[0] : 0, want_tag);
		t_fail(name, detail);
		goto out;
	}
	if (want_tag == 0x00 && eret != n + 6) {
		snprintf(detail, sizeof(detail), "RAW outlen=%lu want %lu",
			 (unsigned long)eret, (unsigned long)(n + 6));
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

static void determinism_one(int level, const uint8_t *src, size_t n,
			    const char *tag)
{
	char name[128], detail[128];
	uint8_t *a = malloc(n + 1024);
	uint8_t *b = malloc(n + 1024);
	size_t ra, rb;

	snprintf(name, sizeof(name), "shapep-det L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!a || !b) {
		t_fail(name, "oom");
		goto out;
	}
	ra = lzmesh_encode(a, n + 1024, src, n, NULL, level);
	rb = lzmesh_encode(b, n + 1024, src, n, NULL, level);
	if (!ra || ra != rb || memcmp(a, b, ra) != 0) {
		snprintf(detail, sizeof(detail), "runs differ (%lu vs %lu)",
			 (unsigned long)ra, (unsigned long)rb);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(a);
	free(b);
}

/* N35: altAB35/e05 is 25B with tok 0x7f + lenextra 0x00 (mc31 ml33). */
static void n35_pin(void)
{
	char detail[160];
	uint8_t src[35], *enc = malloc(1024), dec[128];
	size_t i, eret, ds, dr;

	for (i = 0; i < 35; i++)
		src[i] = (uint8_t)(i & 1 ? 0x42 : 0x41);
	eret = lzmesh_encode(enc, 1024, src, 35, NULL, 0xE05);
	if (eret != 25 || enc[0] != 0x01 || enc[11] != 0x7F ||
	    enc[12] != 0x00) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tok[11]=%02x lenx[12]=%02x, want 25/7f/00",
			 (unsigned long)eret, eret > 12 ? enc[11] : 0,
			 eret > 12 ? enc[12] : 0);
		t_fail("shapep N35 pin", detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, sizeof(dec), enc, eret, NULL);
	if (ds != 35 || dr != 35 || memcmp(dec, src, 35) != 0) {
		t_fail("shapep N35 pin", "sizer/decode");
		goto out;
	}
	t_pass("shapep N35 pin");
out:
	free(enc);
}

/* period7-n100/e05 is 31B (merge-observed oracle-identical shape). */
static void p7_pin(void)
{
	char detail[160];
	uint8_t src[100], *enc = malloc(1024), dec[256];
	size_t i, eret, ds, dr;

	for (i = 0; i < 100; i++)
		src[i] = (uint8_t)(i % 7);
	eret = lzmesh_encode(enc, 1024, src, 100, NULL, 0xE05);
	if (eret != 31 || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want 31/01",
			 (unsigned long)eret, eret ? enc[0] : 0);
		t_fail("shapep p7-100 pin", detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, sizeof(dec), enc, eret, NULL);
	if (ds != 100 || dr != 100 || memcmp(dec, src, 100) != 0) {
		t_fail("shapep p7-100 pin", "sizer/decode");
		goto out;
	}
	t_pass("shapep p7-100 pin");
out:
	free(enc);
}

/* Tag-agnostic roundtrip + determinism (finder-fallback shapes). */
static void shape_any(int level, const uint8_t *src, size_t n,
		      const char *tag)
{
	char name[128], detail[160];
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "shapep-any L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret == 0 || eret != eret2 || memcmp(enc, enc2, eret) != 0) {
		snprintf(detail, sizeof(detail), "encode/det (%lu vs %lu)",
			 (unsigned long)eret, (unsigned long)eret2);
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (ds != n || dr != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu tag=%02x",
			 (unsigned long)ds, (unsigned long)dr, enc[0]);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(enc2);
	free(dec);
}

/*
 * AABA400: port emits spec-legal SHAPE-P p=4 COMP (32B: lit "AABA",
 * tok 0xff, lit-extra 0, len ff-chain mc394, d=4) at all 3 levels —
 * the u12 Task-4 "G4 rep2-live kill -> RAW" note does NOT reproduce
 * on port bytes. TIER-legal (D=400>fo=21, fo+10=31<=400), so COMP
 * stands; the G4 kill is lane-internal, not a spec MUST.
 */
static void aaba_pin(int level, const uint8_t *src)
{
	char name[96], detail[160];
	uint8_t *enc = malloc(1024);
	uint8_t *dec = malloc(512);
	size_t eret, ds, dr;

	snprintf(name, sizeof(name), "shapep L%x AABA400 pin", level);
	if (!enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, 1024, src, 400, NULL, level);
	if (eret != 32 || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want 32/01",
			 (unsigned long)eret, eret ? enc[0] : 0);
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, 512, enc, eret, NULL);
	if (ds != 400 || dr != 400 || memcmp(dec, src, 400) != 0) {
		t_fail(name, "sizer/decode");
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(dec);
}

/* E-B4 short-rule cell: exact roundtrip + determinism at L5 (skip unwired). */
static void eb4_one(const uint8_t *src, size_t n, const char *tag)
{
	char name[128], detail[160];
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "eb4 Le05 %s n=%lu", tag, (unsigned long)n);
	if (!enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, 0xE05);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, 0xE05);
	if (eret == 0 || eret != eret2 || memcmp(enc, enc2, eret) != 0) {
		snprintf(detail, sizeof(detail), "encode/det (%lu vs %lu)",
			 (unsigned long)eret, (unsigned long)eret2);
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (ds != n || dr != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu tag=%02x",
			 (unsigned long)ds, (unsigned long)dr, enc[0]);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(enc2);
	free(dec);
}

int main(void)
{
	static const int PS[] = { 3, 4, 5, 6, 8 };
	uint8_t *alt, *p7, *pp, *g4, *e1, *e2, *e3, *e4;
	size_t i, li, pi;
	uint32_t seed = 0x5EED05u;

	alt = malloc(1000);
	p7 = malloc(1000);
	pp = malloc(100);
	g4 = malloc(400);
	e1 = malloc(256);
	e2 = malloc(640);
	e3 = malloc(81);
	e4 = malloc(101);
	if (!alt || !p7 || !pp || !g4 || !e1 || !e2 || !e3 || !e4) {
		t_fail("alloc", "oom");
		goto done;
	}
	for (i = 0; i < 1000; i++) {
		alt[i] = (uint8_t)(i & 1 ? 0x42 : 0x41);
		p7[i] = (uint8_t)(i % 7);
	}
	for (i = 0; i < 400; i++)
		g4[i] = (uint8_t)("AABA"[i & 3]);

	/* altAB ladder: 22 RAW (TIER-2 veto), 23+ COMP. */
	for (li = 0; li < 3; li++) {
		shape_one(MLEVELS[li], alt, 22, 0x00, "altAB");
		shape_one(MLEVELS[li], alt, 23, 0x01, "altAB");
		shape_one(MLEVELS[li], alt, 35, 0x01, "altAB");
		shape_one(MLEVELS[li], alt, 100, 0x01, "altAB");
		shape_one(MLEVELS[li], alt, 1000, 0x01, "altAB");
	}
	/* period7 ladder: 28 RAW, 29+ COMP. */
	for (li = 0; li < 3; li++) {
		shape_one(MLEVELS[li], p7, 28, 0x00, "period7");
		shape_one(MLEVELS[li], p7, 29, 0x01, "period7");
		shape_one(MLEVELS[li], p7, 100, 0x01, "period7");
	}
	/* p-sweep n=100 (e05 all p; e01/e09 spot p=3 and p=8). */
	for (pi = 0; pi < sizeof(PS) / sizeof(PS[0]); pi++) {
		char ptag[16];

		snprintf(ptag, sizeof(ptag), "p%d", PS[pi]);
		if (PS[pi] == 8) {
			/*
			 * iota p8 (bytes 0..7) hits the hash-alias RAW
			 * fallback (byte-dependent, R-002-open), so the
			 * p8 COMP pin uses alpha bytes; iota stays a
			 * roundtrip-only pin below.
			 */
			for (i = 0; i < 100; i++)
				pp[i] = (uint8_t)(0x41 + (i % 8));
		} else {
			for (i = 0; i < 100; i++)
				pp[i] = (uint8_t)(i % (size_t)PS[pi]);
		}
		shape_one(0xE05, pp, 100, 0x01, ptag);
	}
	for (i = 0; i < 100; i++)
		pp[i] = (uint8_t)(0x41 + (i % 8));
	shape_one(0xE01, pp, 100, 0x01, "p8-alpha");
	shape_one(0xE09, pp, 100, 0x01, "p8-alpha");
	for (i = 0; i < 100; i++)
		pp[i] = (uint8_t)(i % 3);
	shape_one(0xE01, pp, 100, 0x01, "p3");
	shape_one(0xE09, pp, 100, 0x01, "p3");
	for (i = 0; i < 100; i++)
		pp[i] = (uint8_t)(i % 8);
	shape_any(0xE05, pp, 100, "p8-iota-fallback");
	/* L0 periodic emits litonly-HUF COMP (u36, R4; oracle-identical:
	 * altAB 51B + period7 75B). Old "stays RAW" note predates R3-A2. */
	shape_one(0xE00, alt, 100, 0x01, "altAB");
	shape_one(0xE00, p7, 100, 0x01, "period7");
	for (li = 0; li < 3; li++)
		aaba_pin(MLEVELS[li], g4);

	n35_pin();
	p7_pin();
	determinism_one(0xE05, alt, 35, "altAB35");
	determinism_one(0xE05, p7, 100, "period7-100");

	/* E-B4 short-rule neighborhood cells (L5; skip pending, no byte pin). */
	for (i = 0; i < 256; i++)
		e1[i] = (uint8_t)(xs32(&seed) & 0xFF);
	memcpy(e1 + 100, e1 + 92, 4); /* len4 @d8: short-near */
	memcpy(e1 + 200, e1 + 136, 6); /* len6 @d64 */
	eb4_one(e1, 256, "short-near");
	for (i = 0; i < 640; i++)
		e2[i] = (uint8_t)(xs32(&seed) & 0xFF);
	memcpy(e2 + 500, e2 + 100, 10); /* len10 @d400: far-current */
	memcpy(e2 + 520, e2 + 512, 4); /* len4 @d8: near-next (Dsb<0) */
	eb4_one(e2, 640, "far-then-near");
	memset(e3, 0x41, 40);
	e3[40] = 0x42;
	memset(e3 + 41, 0x41, 40);
	eb4_one(e3, 81, "run-B-run");
	for (i = 0; i < 50; i++) {
		e4[i] = (uint8_t)(i & 1 ? 0x42 : 0x41);
		e4[51 + i] = (uint8_t)(i & 1 ? 0x42 : 0x41);
	}
	e4[50] = 0x43; /* broken periodicity: not SHAPE-P */
	eb4_one(e4, 101, "altAB-break");

done:
	printf("---\nm16-shapep-eb4: pass=%d fail=%d\n", g_pass, g_fail);
	free(alt);
	free(p7);
	free(pp);
	free(g4);
	free(e1);
	free(e2);
	free(e3);
	free(e4);
	return g_fail ? 1 : 0;
}
