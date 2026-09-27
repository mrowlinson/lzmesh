/* SPDX-License-Identifier: 0BSD */
/*
 * test_m24_u6q_dec.c — M24 decoder pins: e00 5B-overlong refuse + ZA slack
 * accept (u6q, MERGE-M24 T2/T6).
 *
 * SPEC-v2: S3.10 (len codec: 1B covers escape..escape+254, u=255
 * triggers 5B ff+u32LE; rep esc 7, lit esc 3; mc = esc+u, ml = mc+2,
 * lit_run = extra+3), S4.3/Q14 (C16 len-decode fails before any copy
 * of that token), S4.4 (term-clamp: intra-token MATCH overhang MUST
 * truncate; LIT overhang -> C18), S3.3 (E-decode over-long ACCEPT) +
 * S4.6 (pads-ignored analogue) for [9,bo) slack tolerance, S1.8/Q9
 * (sizer framing-only: decoder-only gates prove as walk-ok +
 * decode-0).
 *
 * Fix (GAPLOG-u6q FLUSH2/FLUSH3, merged M24): (a) decode_len 5B path
 * with u<=254 FAILs (C16-class) — non-minimal over-long, the 1B form
 * covers u 0..254; (b) fetch off!=br_len -> off>br_len — over-long
 * [9,bo) slack is parsed+ignored, under-long still FAILs. M24 T6
 * confirms on the port binary: P1 overlong-5B REFUSES 0 / P2
 * minimal-5B 259xA / P3 slack 20xA / P4 exact 20xA / P5 underlong
 * REFUSES 0 (5/5). Mechanism closes R6 classes: e00-8 REV
 * (plen==raw, e.g. R6R1 s01-n100-sparse swap2 m13 raw100 len51
 * oracle-refused/port-ok) via (a); ZA-417 (e.g. R6R2 s06-n33-run
 * byte1 m39 raw33 len23 oracle-ok/port-refused) via (b); row-level
 * close needs the R7 fuzz rerun. NOTE: v2 is silent on lenbytes
 * minimality (ib explicitly no-minimality S3.3; lenbytes
 * unmentioned) — SPEC-QUERY owed if R7 keeps REV (lane FLUSH3).
 *
 * M35 c19 FLIP (MERGE-M35 T4/T5, oracle-CONFIRMED 12/12): the 5
 * over-long-5B pins now ACCEPT byte-exact (decode-through); the
 * FORM no longer refuses. Pins here are lane-probe analogues
 * (P1..P5 shapes) plus twin ladders: same mc in 1B ACCEPTs and 5B
 * ACCEPTs (u 0/8/16/254 over-long vs u 255 minimal + 1B twins);
 * a ds-short pair (5B u254 ACCEPT vs 1B u254 clamp-ACCEPT); slack pads
 * 0x00/0xFF x1/x2 ACCEPT byte-identical to exact; under-long
 * need3/have2 REFUSEs on both legs. Valid-safe: enc emits minimal
 * + exact (P2/P4 twins prove), oracle likewise (e00-vectors green).
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

static uint16_t r16le(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

/* 1-tok rep0 match forge, 1B lenbytes: lit + 0x07 + u8, RAW, bo==fo=12. */
static size_t forge_match1(uint8_t *f, uint32_t ds, uint8_t lit,
			   uint8_t extra)
{
	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, 12);
	w16le(f + 7, 12);
	f[9] = lit;
	f[10] = 0x07;
	f[11] = extra;
	w16le(f + 12, 0x0000);
	w16le(f + 14, 1);
	w16le(f + 16, 1);
	w16le(f + 18, 1);
	w16le(f + 20, 0);
	f[22] = 0xFF;
	return 23;
}

/* 1-tok rep0 match forge, 5B lenbytes: lit + 0x07 + ff+u32, bo==fo=16. */
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

/* 1-tok L0-form lit forge, 1B lenbytes: lit + 0xC0 + u8, modes 0x49. */
static size_t forge_lit1(uint8_t *f, uint32_t ds, uint8_t lit,
			 uint8_t extra)
{
	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, 12);
	w16le(f + 7, 12);
	f[9] = lit;
	f[10] = 0xC0;
	f[11] = extra;
	w16le(f + 12, 0x0049);
	w16le(f + 14, 1);
	w16le(f + 16, 1);
	w16le(f + 18, (uint16_t)ds);
	w16le(f + 20, 0);
	f[22] = 0xFF;
	return 23;
}

/* 1-tok L0-form lit forge, 5B lenbytes: lit + 0xC0 + ff+u32, modes 0x09. */
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
 * Insert npad pad bytes at the fetch slack point (bo end): shifts
 * footer+END right, grows bo/fo. Turns an exact forge into its slack
 * twin. Returns new length.
 */
static size_t add_slack(uint8_t *f, size_t len, uint8_t pad, size_t npad)
{
	uint16_t bo = r16le(f + 5);
	size_t fo = (size_t)bo;

	memmove(f + fo + npad, f + fo, len - fo);
	memset(f + fo, pad, npad);
	w16le(f + 5, (uint16_t)(bo + npad));
	w16le(f + 7, (uint16_t)(bo + npad));
	return len + npad;
}

/*
 * Drop the last byte-region byte (the 1B lenbyte): exact 3B region
 * becomes need3/have2 under-long. Returns new length.
 */
static size_t drop_region_byte(uint8_t *f, size_t len)
{
	uint16_t bo = r16le(f + 5);
	size_t fo = (size_t)bo;

	memmove(f + fo - 1, f + fo, len - fo);
	w16le(f + 5, (uint16_t)(bo - 1));
	w16le(f + 7, (uint16_t)(bo - 1));
	return len - 1;
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

int main(void)
{
	static uint8_t f[64];
	size_t slen;

	/*
	 * Match-leg over-long ladder (rep esc 7: mc = 7+u, total = 10+u).
	 * M35 c19 FLIP: 5B with u<=254 ACCEPTs byte-exact (oracle-
	 * confirmed 12/12, M35 T5); same-u 1B twins ACCEPT. (u0
	 * exact-fill is unframable — total 10 < fo 12 violates C6
	 * fo<ds — so the ladder starts at u8.)
	 */
	slen = forge_match5(f, 18, 0x42, 8);
	exact_fill_one("m24 match overlong u8 accept", f, slen, 18, 0x42);
	slen = forge_match1(f, 18, 0x42, 8);
	exact_fill_one("m24 match 1B u8 accept", f, slen, 18, 0x42);

	slen = forge_match5(f, 264, 0x42, 254);
	exact_fill_one("m24 match overlong u254 accept", f, slen, 264, 0x42);
	slen = forge_match1(f, 264, 0x42, 254);
	exact_fill_one("m24 match 1B u254 accept", f, slen, 264, 0x42);

	/* Minimal 5B floor: u=255 (mc262, total 265) ACCEPTs. */
	slen = forge_match5(f, 265, 0x42, 255);
	exact_fill_one("m24 match 5B u255 accept", f, slen, 265, 0x42);

	/*
	 * C16-before-clamp pair (ds=20 short of the 264 total): 1B
	 * u254 clamp-ACCEPTs 20x (term-clamp); M35 c19 FLIP: 5B
	 * u254 ACCEPTs 20x (over-long decode-through, oracle-
	 * confirmed M35 T5).
	 */
	slen = forge_match1(f, 20, 0x43, 254);
	exact_fill_one("m24 match 1B u254 clamp accept", f, slen, 20, 0x43);
	slen = forge_match5(f, 20, 0x43, 254);
	exact_fill_one("m24 match 5B u254 short accept", f, slen, 20, 0x43);

	/*
	 * Lit-leg over-long ladder (L0 esc 3: mc = 3+u, total = 4+u).
	 * M35 c19 FLIP: 5B with u<=254 ACCEPTs byte-exact (oracle-
	 * confirmed 12/12, M35 T5). Lane P1 analogue is u16/ds20;
	 * lane P2 analogue is u255/ds259. (u0 exact-fill is
	 * unframable — total 4 < fo 12 violates C6 fo<ds — so the
	 * ladder starts at u16.)
	 */
	slen = forge_lit5(f, 20, 0x41, 16);
	exact_fill_one("m24 lit overlong u16 accept", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 16);
	exact_fill_one("m24 lit 1B u16 accept", f, slen, 20, 0x41);

	slen = forge_lit5(f, 258, 0x41, 254);
	exact_fill_one("m24 lit overlong u254 accept", f, slen, 258, 0x41);
	slen = forge_lit1(f, 258, 0x41, 254);
	exact_fill_one("m24 lit 1B u254 accept", f, slen, 258, 0x41);

	slen = forge_lit5(f, 259, 0x41, 255);
	exact_fill_one("m24 lit 5B u255 accept", f, slen, 259, 0x41);

	/*
	 * ZA slack (lane P3/P4 analogues): L0-run20 exact (bo12)
	 * ACCEPTs; +1/+2 pad slack (bo13/bo14) ACCEPTs byte-identical.
	 * Pad 0xFF proves trailing is ignored, not parsed.
	 */
	slen = forge_lit1(f, 20, 0x41, 16);
	exact_fill_one("m24 slack L0 exact accept", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 16);
	slen = add_slack(f, slen, 0x00, 1);
	exact_fill_one("m24 slack L0 pad00 accept", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 16);
	slen = add_slack(f, slen, 0xFF, 1);
	exact_fill_one("m24 slack L0 padFF accept", f, slen, 20, 0x41);
	slen = forge_lit1(f, 20, 0x41, 16);
	slen = add_slack(f, slen, 0x00, 2);
	exact_fill_one("m24 slack L0 pad2 accept", f, slen, 20, 0x41);

	/* Rep-leg slack twin: P2b shape + 1 pad ACCEPTs 18x'B'. */
	slen = forge_match1(f, 18, 0x42, 8);
	slen = add_slack(f, slen, 0x00, 1);
	exact_fill_one("m24 slack rep pad accept", f, slen, 18, 0x42);

	/*
	 * Under-long still FAILs (lane P5 analogue): need3/have2
	 * region, sizer sums, decode 0 — both legs.
	 */
	slen = forge_lit1(f, 20, 0x41, 16);
	slen = drop_region_byte(f, slen);
	split_one("m24 underlong L0 refuse", f, slen, 20);
	slen = forge_match1(f, 18, 0x42, 8);
	slen = drop_region_byte(f, slen);
	split_one("m24 underlong rep refuse", f, slen, 18);

	printf("---\nm24-u6q-dec: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
