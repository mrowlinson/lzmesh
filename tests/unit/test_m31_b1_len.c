/* SPDX-License-Identifier: 0BSD */
/*
 * test_m31_b1_len.c — M31 B1 UNIFIED LENGTH pins (u6v, GAPLOG-u6v FLUSH3).
 *
 * SPEC-v2: S3.9 (token=(lit<<6)|(sel<<3)|len; L0-lit tok 0xC0),
 * S3.10 (len codec: lit esc 3 + extra, 1B iff extra<=254 else
 * ff+u32LE; mc = esc+u, lit_run = extra+3, total = 4+u), S4.3
 * (C18 consumed==litc; over-long 5B u<=254 C16-FAIL per u6q/M24),
 * S4.4 (term-clamp: LIT overhang -> C18), S1.8/Q9 (sizer
 * framing-only: decoder-only gates prove as walk-ok + decode-0).
 * P1-ANSWERS Q2 Rule B1: L0-lit bo==fo no-Huffman dc==0 shape with
 * tok (b10&0xE0)==0xC0 && (b10&7)!=7 takes L = u8@11 (1-byte) or
 * FF+u16@12..13 (extended); Apple ACCEPTS iff ds-4<=L<=ALIGN32(ds)-4
 * (overshoot CLAMPED same-output), else refuses. u6v implements the
 * REJECT leg only (out-of-range FAIL; in-range continues into the
 * S3.10/S4.4 replay path, which still refuses overshoot via C18 —
 * the accept-leg widen is oracle followup, GAPLOG-u6v FLUSH4).
 *
 * Pins (forge_lit1/5 = m24 L0-lit structure, bo==fo, tok 0xC0,
 * no-Huffman, dc==0, litc==ds): exact-L ACCEPTs (proves the forge
 * family valid); in-range overshoot XFAILs (Apple-accept per P1,
 * port C18-refuse — auto-PASS when the accept leg lands);
 * out-of-range + undershoot REFUSE (walk-ok + decode-0; B1 gate
 * post-M31, C18/C21 pre-M31 — same observable, green both).
 * ds=64 is the single-point edge ([60,60] per P1); ds=33 L=30 is
 * the R8R2 analogue (L=30 in [29,60]). Extended ds=259 covers the
 * FF-marker form (0xFF structural at L>=255). Public API only.
 * Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

static int g_pass, g_fail, g_xfail;

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
	g_xfail++;
	printf("XFAIL %s :: %s\n", name, reason);
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

/* 1-tok L0-form lit forge, 1B lenbytes: lit + tok + u8, bo==fo=12. */
static size_t forge_lit1(uint8_t *f, uint32_t ds, uint8_t lit, uint8_t tok,
			 uint8_t extra)
{
	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, 12);
	w16le(f + 7, 12);
	f[9] = lit;
	f[10] = tok;
	f[11] = extra;
	w16le(f + 12, 0x0049);
	w16le(f + 14, 1);
	w16le(f + 16, 1);
	w16le(f + 18, (uint16_t)ds);
	w16le(f + 20, 0);
	f[22] = 0xFF;
	return 23;
}

/* 1-tok L0-form lit forge, 5B lenbytes: lit + tok + ff+u32, bo==fo=16. */
static size_t forge_lit5(uint8_t *f, uint32_t ds, uint8_t lit, uint8_t tok,
			 uint32_t extra)
{
	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, 16);
	w16le(f + 7, 16);
	f[9] = lit;
	f[10] = tok;
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

/* Decoder-only reject: walk sums, decode 0 (Q9 split proof). */
static void split_one(const char *name, const uint8_t *src, size_t slen,
		      size_t want_ds)
{
	char detail[128];
	static uint8_t dec[512];
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
	static uint8_t dec[512];
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

/*
 * In-range overshoot: Apple ACCEPTs (clamp, P1 B1), port still
 * REFUSEs via C18 (accept-leg GAP, GAPLOG-u6v FLUSH4). XFAIL now,
 * auto-PASS when the widen lands. Anything else is a FAIL.
 */
static void overshoot_xfail_one(const char *name, const uint8_t *src,
			       size_t slen, size_t want_ds, uint8_t fill)
{
	char detail[160];
	static uint8_t dec[512];
	size_t ds, dr, i;
	int ok;

	ds = lzmesh_decoded_size(src, slen);
	dr = lzmesh_decode(dec, sizeof(dec), src, slen, NULL);
	ok = (ds == want_ds && dr == want_ds);
	for (i = 0; i < want_ds && ok; i++)
		if (dec[i] != fill)
			ok = 0;
	if (ok) {
		t_pass(name);
		return;
	}
	if (ds == want_ds && dr == 0) {
		snprintf(detail, sizeof(detail),
			 "walk %lu ok, C18-refused 0 (B1 accept-leg GAP; "
			 "Apple clamps, P1 B1)",
			 (unsigned long)ds);
		t_xfail(name, detail);
		return;
	}
	snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu, want %lu + fill",
		 (unsigned long)ds, (unsigned long)dr,
		 (unsigned long)want_ds);
	t_fail(name, detail);
}

int main(void)
{
	static uint8_t f[64];
	size_t slen;

	/* ds=20, range 16..28 (ALIGN32(20)=32). */
	slen = forge_lit1(f, 20, 0x41, 0xC0, 16);
	exact_fill_one("b1 ds20 exact L16 accept", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 17);
	overshoot_xfail_one("b1 ds20 L17 clamp-xfail", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 20);
	overshoot_xfail_one("b1 ds20 L20 clamp-xfail", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 28);
	overshoot_xfail_one("b1 ds20 L28 top-xfail", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 29);
	split_one("b1 ds20 L29 over refuse", f, slen, 20);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 40);
	split_one("b1 ds20 L40 over refuse", f, slen, 20);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 254);
	split_one("b1 ds20 L254 over refuse", f, slen, 20);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 15);
	split_one("b1 ds20 L15 under refuse", f, slen, 20);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 8);
	split_one("b1 ds20 L8 under refuse", f, slen, 20);
	slen = forge_lit1(f, 20, 0x41, 0xC0, 0);
	split_one("b1 ds20 L0 under refuse", f, slen, 20);

	/* ds=33 R8R2 analogue, range 29..60 (ALIGN32(33)=64). */
	slen = forge_lit1(f, 33, 0x42, 0xC0, 29);
	exact_fill_one("b1 ds33 exact L29 accept", f, slen, 33, 0x42);
	slen = forge_lit1(f, 33, 0x42, 0xC0, 30);
	overshoot_xfail_one("b1 ds33 L30 R8R2-xfail", f, slen, 33, 0x42);
	slen = forge_lit1(f, 33, 0x42, 0xC0, 60);
	overshoot_xfail_one("b1 ds33 L60 top-xfail", f, slen, 33, 0x42);
	slen = forge_lit1(f, 33, 0x42, 0xC0, 61);
	split_one("b1 ds33 L61 over refuse", f, slen, 33);
	slen = forge_lit1(f, 33, 0x42, 0xC0, 28);
	split_one("b1 ds33 L28 under refuse", f, slen, 33);

	/* ds=64 single-point edge [60,60] (ALIGN32(64)=64). */
	slen = forge_lit1(f, 64, 0x43, 0xC0, 60);
	exact_fill_one("b1 ds64 exact L60 accept", f, slen, 64, 0x43);
	slen = forge_lit1(f, 64, 0x43, 0xC0, 61);
	split_one("b1 ds64 L61 over refuse", f, slen, 64);
	slen = forge_lit1(f, 64, 0x43, 0xC0, 59);
	split_one("b1 ds64 L59 under refuse", f, slen, 64);

	/*
	 * Extended ds=259, range 255..284 (ALIGN32(259)=288; 0xFF is
	 * the structural extension marker, P1 n=259 shape change).
	 */
	slen = forge_lit5(f, 259, 0x44, 0xC0, 255);
	exact_fill_one("b1 ds259 ext L255 accept", f, slen, 259, 0x44);
	slen = forge_lit5(f, 259, 0x44, 0xC0, 256);
	overshoot_xfail_one("b1 ds259 ext L256 xfail", f, slen, 259, 0x44);
	slen = forge_lit5(f, 259, 0x44, 0xC0, 284);
	overshoot_xfail_one("b1 ds259 ext L284 top-xfail", f, slen, 259, 0x44);
	slen = forge_lit5(f, 259, 0x44, 0xC0, 285);
	split_one("b1 ds259 ext L285 over refuse", f, slen, 259);
	slen = forge_lit5(f, 259, 0x44, 0xC0, 300);
	split_one("b1 ds259 ext L300 over refuse", f, slen, 259);
	slen = forge_lit5(f, 259, 0x44, 0xC0, 254);
	split_one("b1 ds259 ext L254 nonmin refuse", f, slen, 259);

	/*
	 * Tok low-bits ignored inside the 28-set (P1: (b10&0xE0)==0xC0
	 * && (b10&7)!=7, low bits same-output): 0xC1/0xC6 exact-L twins
	 * must decode byte-identical to 0xC0.
	 */
	slen = forge_lit1(f, 20, 0x41, 0xC1, 16);
	exact_fill_one("b1 tok C1 lowbit accept", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 0xC6, 16);
	exact_fill_one("b1 tok C6 lowbit accept", f, slen, 20, 0x41);

	printf("---\nm31-b1-len: pass=%d fail=%d xfail=%d\n", g_pass, g_fail,
	       g_xfail);
	return g_fail ? 1 : 0;
}
