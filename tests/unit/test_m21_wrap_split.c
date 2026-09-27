/* SPDX-License-Identifier: 0BSD */
/*
 * test_m21_wrap_split.c — M21 lenbytes/mc+2 u32-wrap split pins (u6p).
 *
 * SPEC-v2: S3.10 (len codec: rep esc 7 + extra mc-7, 1B iff <=254
 * else ff+u32LE; lit esc 3, ff+u32LE), S4.3 (end checks; C18
 * consumed==litc), S4.4 (term-clamp: intra-token MATCH overhang MUST
 * truncate any amount; whole-token remainder MUST reject C21), S6.3
 * (wrap blessed ONLY for distance d', accept-iff-1<=d'<=bytes; no
 * length-wrap clause so wide arithmetic governs), S1.8/Q9 (sizer
 * framing-only: decoder-only gates prove as walk-ok + decode-0).
 * Wrap split (u6p): decode_len reports wrap + saturates UINT32_MAX;
 * lit_run FAILs on wrap (e00 REV-8 reject); match_len saturates ml to
 * UINT32_MAX on wrap or mc+2 overflow (ZA clamp-accept via existing
 * match_take). Valid-safe: largest valid mc min2 16777213 << 2^32.
 * Lane P1-P6 forged here as spec-derived analogues (row bins + oracle
 * outside wall; rerun decides counts): P1 lit u32=MAX esc3 ds20
 * REFUSE; P2 match u32=0xFFFFFFFE esc7 ds18 REFUSE (u6r M23: match
 * leg FAILs on wrap, was u6p clamp-accept — COMP-R6 0/7 refuted);
 * P2b exact-fill accept + P2c C21 refuse sanities; P3 lit u=MAX-3
 * (mc=MAX, no wrap) C18 refuse; P4 match mc=MAX-1 (mc+2 overflow)
 * REFUSE (u6r M23, was u6p clamp-accept); P5 L0-run 22x'A'
 * no-regress; P6 min2-mc 16777213 ACCEPT 18x'E'. P1 here is 1-tok
 * L0-form (lane P1 is 2-tok ds20); same esc3/u32=MAX mechanism,
 * walk-ok + decode-0 split.
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
	static uint8_t f[64], dec[128];
	size_t slen, dsr, dr, i;

	/* P1 lit-wrap (u32=MAX esc3, ds20) REFUSES. */
	slen = forge_lit5(f, 20, 0x41, 0xFFFFFFFFu);
	split_one("wrap P1 lit-wrap refuse", f, slen, 20);

	/* P3 lit-boundary (u=MAX-3, mc=MAX, no wrap) C18 REFUSES. */
	slen = forge_lit5(f, 20, 0x41, 0xFFFFFFFCu);
	split_one("wrap P3 lit-max refuse", f, slen, 20);

	/* P2 match-wrap (u32=0xFFFFFFFE esc7, ds18) REFUSES (u6r). */
	slen = forge_match5(f, 18, 0x42, 0xFFFFFFFEu);
	split_one("wrap P2 match-wrap refuse", f, slen, 18);

	/* P4 match mc=MAX-1 (mc+2 overflow, extra=MAX-8) REFUSES (u6r). */
	slen = forge_match5(f, 18, 0x44, 0xFFFFFFF7u);
	split_one("wrap P4 match-mcmax refuse", f, slen, 18);

	/* P6 min2-mc 16777213 (extra 16777206) ACCEPTS 18x'E'. */
	slen = forge_match5(f, 18, 0x45, 16777206u);
	exact_fill_one("wrap P6 match-min2 accept", f, slen, 18, 0x45);

	/* P2b exact-fill sanity: mc15 (extra 8, 1B) ds18 ACCEPTS 18x'B'. */
	f[0] = 0x01;
	w32le(f + 1, 18);
	w16le(f + 5, 12);
	w16le(f + 7, 12);
	f[9] = 0x42;
	f[10] = 0x07;
	f[11] = 0x08;
	w16le(f + 12, 0x0000);
	w16le(f + 14, 1);
	w16le(f + 16, 1);
	w16le(f + 18, 1);
	w16le(f + 20, 0);
	f[22] = 0xFF;
	exact_fill_one("wrap P2b exact-fill accept", f, 23, 18, 0x42);

	/* P2c C21 sanity: 2-tok (mc15 + t1) ds18 REFUSES (remainder). */
	f[0] = 0x01;
	w32le(f + 1, 18);
	w16le(f + 5, 13);
	w16le(f + 7, 13);
	f[9] = 0x42;
	f[10] = 0x07;
	f[11] = 0x00;
	f[12] = 0x08;
	w16le(f + 13, 0x0000);
	w16le(f + 15, 2);
	w16le(f + 17, 1);
	w16le(f + 19, 1);
	w16le(f + 21, 0);
	f[23] = 0xFF;
	split_one("wrap P2c C21 refuse", f, 24, 18);

	/* P5 no-regress: L0-run 22x'A' e00 COMP + byte-exact roundtrip. */
	{
		static uint8_t src[22], enc[128];
		size_t eret;

		memset(src, 0x41, sizeof(src));
		eret = lzmesh_encode(enc, sizeof(enc), src, sizeof(src),
				     NULL, 0xE00);
		if (eret == 0 || enc[0] != 0x01) {
			char detail[96];

			snprintf(detail, sizeof(detail),
				 "outlen=%lu tag=%02x, want COMP",
				 (unsigned long)eret, eret ? enc[0] : 0);
			t_fail("wrap P5 L0-run accept", detail);
		} else {
			dsr = lzmesh_decoded_size(enc, eret);
			dr = lzmesh_decode(dec, sizeof(dec), enc, eret,
					   NULL);
			if (dsr != 22 || dr != 22 || memcmp(dec, src, 22) != 0) {
				t_fail("wrap P5 L0-run accept",
				       "sizer/decode");
			} else {
				t_pass("wrap P5 L0-run accept");
			}
		}
		(void)i;
	}

	printf("---\nm21-wrap-split: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
