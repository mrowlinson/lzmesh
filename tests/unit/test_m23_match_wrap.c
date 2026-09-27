/* SPDX-License-Identifier: 0BSD */
/*
 * test_m23_match_wrap.c — M23 match-wrap FAIL pins (u6r, #53 close).
 *
 * SPEC-v2: S3.10/Q5/Q6 (decode_len: escape+extra, ff->u32 chain;
 * rep esc 7, lit esc 3; ml=mc+2, mc0 legal), S4.3/Q14 (C16 len-decode
 * fails before any copy of that token; consumed<litc -> C18;
 * tokens-remain-after-fill -> C21), S4.4 (term-clamp: intra-token
 * MATCH overhang MUST truncate any amount; LIT overhang -> C18;
 * whole-token remainder -> C21; R2 re-verify #12 carve-out: clamp
 * does NOT cover u32 overflow), S6.3 (u32 wrap blessed ONLY for
 * distance d', accept-iff-1<=d'<=bytes; no length-wrap clause so
 * wrapped lengths FAIL), S1.8/Q9 (sizer framing-only: decoder-only
 * gates prove as walk-ok + decode-0).
 * Fix (DECISIONS M23, GAPLOG-u6r): lz_u3_match_len FAILs on
 * wrapped || mc>MAX-2 (C16-class, before match copy); decode_len
 * wrapped-reporting kept (lit leg needs it). Lit leg unchanged
 * (lit_run FAILs on wrap, M21). Pre-fix u6p match leg saturated to
 * UINT32_MAX + clamp-accepted (COMP-R6 0/7 agree, port-accept /
 * oracle-refuse); post-fix every wrapped||mc+2-overflowing match
 * FAILs = oracle refuse on all 7 rows (row-level close needs COMP-R7).
 * Pins here: P2/P4 corrected to refuse (m21-wrap-split still asserts
 * the refuted accept — stale, test-lane-owned flip outside this
 * file); wrap min/max-extra boundaries (wrapped values mc0/mc6 would
 * be legal unwrapped per Q16/direct, so refuse proves the wrap arm,
 * not the value); mc=MAX no-wrap overflow (isolates the mc+2 arm
 * from the wrap arm); mc=MAX-2 clamp-accept (largest non-overflowing
 * mc, proves no over-fire); lit-wrap holds with fresh ds/fill;
 * exact-fill + C21 pair with fresh mc/fill/ds (m19-c21 rows use 'A'
 * + other mc; m21-P2b/P2c use ds18/mc15/'B').
 * Forges are 1-tok/2-tok all-RAW no-Huffman blocks (modes 0x0000,
 * bo==fo per u6l ZA rule; L0-form modes 0x09 for lit forges).
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

static void w16le(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void w32le(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* 1-tok rep0 esc5B match forge: lit + 0x07 + ff+u32, RAW, bo==fo=16. */
static size_t forge_match5(uint8_t *f, uint32_t ds, uint8_t lit,
			   uint32_t extra)
{
	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, 16);
	w16le(f + 7, 16);
	f[9] = lit;
	f[10] = 0x07;
	f[11] = 0xFF;
	w32le(f + 12, extra);
	w16le(f + 16, 0x0000);
	w16le(f + 18, 1);
	w16le(f + 20, 5);
	w16le(f + 22, 1);
	w16le(f + 24, 0);
	f[26] = 0xFF;
	return 27;
}

/* 1-tok L0-form lit forge: lit + 0xC0 + ff+u32, modes 0x09, litc=ds. */
static size_t forge_lit5(uint8_t *f, uint32_t ds, uint8_t lit,
			 uint32_t extra)
{
	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, 16);
	w16le(f + 7, 16);
	f[9] = lit;
	f[10] = 0xC0;
	f[11] = 0xFF;
	w32le(f + 12, extra);
	w16le(f + 16, 0x0009);
	w16le(f + 18, 1);
	w16le(f + 20, 5);
	w16le(f + 22, (uint16_t)ds);
	w16le(f + 24, 0);
	f[26] = 0xFF;
	return 27;
}

/*
 * 2-tok C21 forge: fill pre-emit, t0 = rep0/lit0 mc0 (1B extra),
 * t1 = 0x00 remainder, distc 0. ds is the test variable.
 */
static size_t forge_c21_2tok(uint8_t *f, uint32_t ds, uint8_t fill,
			     uint32_t mc0)
{
	uint32_t extra = mc0 - 7;	/* mc0 > 6, 1B form */
	uint32_t fo = 13;

	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, (uint16_t)fo);
	w16le(f + 7, (uint16_t)fo);
	f[9] = fill;
	f[10] = 0x07;
	f[11] = 0x00;
	f[12] = (uint8_t)extra;
	w16le(f + 13, 0x0000);
	w16le(f + 15, 2);
	w16le(f + 17, 1);
	w16le(f + 19, 1);
	w16le(f + 21, 0);
	f[23] = 0xFF;
	return 24;
}

/* Decoder-only reject: walk sums, decode 0 (Q9 split proof). */
static void split_one(const char *name, const uint8_t *src, size_t slen,
		      size_t want_ds)
{
	char detail[128];
	static uint8_t dec[128];
	size_t ds = lzmesh_decoded_size(src, slen);
	size_t dr = lzmesh_decode(dec, sizeof(dec), src, slen, NULL);

	if (ds != want_ds || dr != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu, want %lu/0",
			 (unsigned long)ds, (unsigned long)dr,
			 (unsigned long)want_ds);
		t_fail(name, detail);
		return;
	}
	t_pass(name);
}

/* Exact accept: sizer==ds, decode==ds, all fill byte. */
static void exact_fill_one(const char *name, const uint8_t *src,
			   size_t slen, size_t want_ds, uint8_t fill)
{
	char detail[128];
	static uint8_t dec[128];
	size_t ds, dr, i;

	ds = lzmesh_decoded_size(src, slen);
	dr = lzmesh_decode(dec, sizeof(dec), src, slen, NULL);
	if (ds != want_ds || dr != want_ds) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu, want %lu",
			 (unsigned long)ds, (unsigned long)dr,
			 (unsigned long)want_ds);
		t_fail(name, detail);
		return;
	}
	for (i = 0; i < want_ds; i++) {
		if (dec[i] != fill) {
			snprintf(detail, sizeof(detail),
				 "byte%lu=%02x want %02x",
				 (unsigned long)i, dec[i], fill);
			t_fail(name, detail);
			return;
		}
	}
	t_pass(name);
}

int main(void)
{
	static uint8_t f[64];
	size_t slen;

	/* P2 corrected: match-wrap (u32=0xFFFFFFFE esc7, ds18) REFUSES. */
	slen = forge_match5(f, 18, 0x42, 0xFFFFFFFEu);
	split_one("m23 P2 match-wrap refuse", f, slen, 18);

	/* P4 corrected: mc=MAX-1 (extra=MAX-8, no wrap) REFUSES. */
	slen = forge_match5(f, 18, 0x44, 0xFFFFFFF7u);
	split_one("m23 P4 match-mcmax refuse", f, slen, 18);

	/*
	 * Smallest wrapping extra (7+u wraps to mc0): REFUSES. mc0
	 * unwrapped is legal (Q16), so refuse proves the wrap arm.
	 */
	slen = forge_match5(f, 18, 0x42, 0xFFFFFFF9u);
	split_one("m23 match-wrap min-extra refuse", f, slen, 18);

	/*
	 * Largest wrapping extra (7+MAX wraps to mc6): REFUSES. mc6
	 * unwrapped is direct-legal, so refuse proves the wrap arm.
	 */
	slen = forge_match5(f, 18, 0x42, 0xFFFFFFFFu);
	split_one("m23 match-wrap max-extra refuse", f, slen, 18);

	/*
	 * mc=MAX exactly (extra=MAX-7, NO wrap): REFUSES via the
	 * mc+2-overflow arm alone — isolates it from the wrap arm.
	 */
	slen = forge_match5(f, 18, 0x44, 0xFFFFFFF8u);
	split_one("m23 match mc-MAX overflow refuse", f, slen, 18);

	/*
	 * mc=MAX-2 (extra=MAX-9, ml=MAX, no wrap, no overflow):
	 * term-clamp ACCEPTS 18x'F' — largest non-overflowing mc,
	 * proves the overflow arm does not over-fire.
	 */
	slen = forge_match5(f, 18, 0x46, 0xFFFFFFF6u);
	exact_fill_one("m23 match mc-MAX-2 clamp accept", f, slen, 18,
		       0x46);

	/* Lit-FAIL hold, fresh ds/fill: lit-wrap (u32=MAX) REFUSES. */
	slen = forge_lit5(f, 18, 0x43, 0xFFFFFFFFu);
	split_one("m23 lit-wrap hold refuse", f, slen, 18);

	/*
	 * Smallest wrapping lit extra (3+u wraps to 0): REFUSES.
	 * Lit-leg min boundary (P1 analogue pins the max).
	 */
	slen = forge_lit5(f, 18, 0x43, 0xFFFFFFFDu);
	split_one("m23 lit-wrap min-extra refuse", f, slen, 18);

	/*
	 * Exact-fill accept, fresh shape: mc17 (extra 10, 1B) ds20
	 * -> 1+19 exact, 20x'G' byte-exact.
	 */
	f[0] = 0x01;
	w32le(f + 1, 20);
	w16le(f + 5, 12);
	w16le(f + 7, 12);
	f[9] = 0x47;
	f[10] = 0x07;
	f[11] = 0x0A;
	w16le(f + 12, 0x0000);
	w16le(f + 14, 1);
	w16le(f + 16, 1);
	w16le(f + 18, 1);
	w16le(f + 20, 0);
	f[22] = 0xFF;
	exact_fill_one("m23 exact-fill accept", f, 23, 20, 0x47);

	/*
	 * Same token streams at ds18: 1+19=20 intra-token overhang
	 * truncates -> 18x'G'. Exact/clamp ds-pair.
	 */
	f[0] = 0x01;
	w32le(f + 1, 18);
	w16le(f + 5, 12);
	w16le(f + 7, 12);
	f[9] = 0x47;
	f[10] = 0x07;
	f[11] = 0x0A;
	w16le(f + 12, 0x0000);
	w16le(f + 14, 1);
	w16le(f + 16, 1);
	w16le(f + 18, 1);
	w16le(f + 20, 0);
	f[22] = 0xFF;
	exact_fill_one("m23 exact-fill clamp pair accept", f, 23, 18,
		       0x47);

	/*
	 * C21 refuse, fresh shape: 2-tok mc21 ds24 (1+23 exact on t0,
	 * t1 remains) REFUSES; control ds26 exact-fills 26x'H'.
	 */
	slen = forge_c21_2tok(f, 24, 0x48, 21);
	split_one("m23 C21 refuse", f, slen, 24);
	slen = forge_c21_2tok(f, 26, 0x48, 21);
	exact_fill_one("m23 C21 control accept", f, slen, 26, 0x48);

	printf("---\nm23-match-wrap: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
