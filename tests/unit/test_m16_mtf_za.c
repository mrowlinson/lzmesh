/* SPDX-License-Identifier: 0BSD */
/*
 * test_m16_mtf_za.c — M16 decode pins: MTF dedup (u6i) + ZA fo==bo (u6l).
 *
 * SPEC-v2: S3.9 (token grammar), S3.10 (len codec), S3.11/Q7 (dist
 * symbol (sb<<3)|low3, d formula, sb0 = 0 lane bits), S3.3 (lane
 * framing: payload + index-at-END, ib range, region>index,
 * unused-nonempty/empty-payload tolerance), S6.1 (sb0 through lanes
 * with 0 bits), S1.8/Q9 (sizer framing-only: decoder-only gates
 * prove as walk-ok + decode-0). App-F R-E-B8 DEDUP (S3.12.a
 * amended PROBABLE-strong: repk k>0 moves recent[k] to front,
 * recent[k+1..3] UNCHANGED; rep0 no-op; new-dist insert-shift-drop;
 * CR-140 states (3,4,2,1)/(2,4,3,1)).
 *
 * Forges are the u6i V1/V2 trigger vectors verbatim (gaplog header):
 * lit=5 RAW "ABCDE", tok=6 RAW, len=0, dist=4 RAW sb0 (dsyms
 * 00 01 02 03 -> d=1,2,3,4 -> recents (4,3,2,1)), bo=24, lanes 5B
 * (payload 1B + 4B index, ib=1, all-zero fields), fo=29, footer
 * modes=0 counts (tok6,len0,lit5,dist4), +END = 40B.
 * V1 toks A6 26 26 26 88 10 (rep1->rep2), ds=41:
 *   expect 41 42 [43 x33] 44 45 43 44 43 44; byte39=C iff dedup
 *   (plain shift predicts E).
 * V2 toks A6 26 26 26 82 18 (rep0->rep3), ds=43:
 *   expect 41 42 [43 x33] 44 45 43 43 44 45 45 45; byte41=E iff
 *   dedup (plain shift predicts D; also proves rep0 no-op).
 * ZA (u6l: Apple emits fo==bo with dist>0 for sb0-only no-Huffman):
 * V1/V2 minus lanes (bo=fo=24) MUST accept byte-identical; fo==bo
 * with sb>0 MUST fail closed (missing suffix bits); unused-nonempty
 * (fo>bo, dist==0, no Huffman) MUST parse+ignore (valid index) yet
 * still reject a bad index (ib0, S3.3.d checked even when unused).
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

/* Build the 40B V1/V2 COMP forge; v2sel picks V2 tail (0x82 0x18). */
static size_t forge_v(uint8_t *f, int v2sel, uint32_t ds)
{
	static const uint8_t LIT[5] = { 0x41, 0x42, 0x43, 0x44, 0x45 };
	static const uint8_t TOK1[4] = { 0xA6, 0x26, 0x26, 0x26 };
	static const uint8_t DSYM[4] = { 0x00, 0x01, 0x02, 0x03 };
	static const uint8_t LANES[5] = { 0x00, 0x00, 0x00, 0x00, 0x08 };

	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, 24);
	w16le(f + 7, 29);
	memcpy(f + 9, LIT, 5);
	memcpy(f + 14, TOK1, 4);
	if (v2sel) {
		f[18] = 0x82;
		f[19] = 0x18;
	} else {
		f[18] = 0x88;
		f[19] = 0x10;
	}
	memcpy(f + 20, DSYM, 4);
	memcpy(f + 24, LANES, 5);
	w16le(f + 29, 0x0000);
	w16le(f + 31, 6);
	w16le(f + 33, 0);
	w16le(f + 35, 5);
	w16le(f + 37, 4);
	f[39] = 0xFF;
	return 40;
}

/* V1/V2 minus lanes: bo=fo=24, 35B. */
static size_t forge_v_flat(uint8_t *f, int v2sel, uint32_t ds)
{
	uint8_t full[40];

	forge_v(full, v2sel, ds);
	memcpy(f, full, 24);
	w16le(f + 7, 24);
	memcpy(f + 24, full + 29, 11);
	return 35;
}

static void build_want(uint8_t *w, int v2sel)
{
	size_t i;

	w[0] = 0x41;
	w[1] = 0x42;
	for (i = 0; i < 33; i++)
		w[2 + i] = 0x43;
	w[35] = 0x44;
	w[36] = 0x45;
	if (v2sel) {
		w[37] = 0x43;
		w[38] = 0x43;
		w[39] = 0x44;
		w[40] = 0x45;
		w[41] = 0x45;
		w[42] = 0x45;
	} else {
		w[37] = 0x43;
		w[38] = 0x44;
		w[39] = 0x43;
		w[40] = 0x44;
	}
}

static void exact_one(const char *name, const uint8_t *src, size_t slen,
		      int v2sel, uint32_t ds)
{
	char detail[160];
	uint8_t want[48], dec[128];
	size_t dsr, dr;

	build_want(want, v2sel);
	dsr = lzmesh_decoded_size(src, slen);
	dr = lzmesh_decode(dec, sizeof(dec), src, slen, NULL);
	if (dsr != ds || dr != ds || memcmp(dec, want, ds) != 0) {
		snprintf(detail, sizeof(detail),
			 "sizer=%lu dec=%lu want %lu (byte39=%02x byte41=%02x)",
			 (unsigned long)dsr, (unsigned long)dr,
			 (unsigned long)ds, dr > 39 ? dec[39] : 0,
			 dr > 41 ? dec[41] : 0);
		t_fail(name, detail);
		return;
	}
	t_pass(name);
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

int main(void)
{
	static uint8_t f[64], dec[128];
	size_t slen, dsr, dr;
	int ok;
	size_t i;

	/* MTF V1/V2 with lanes: dedup byte-exact. */
	slen = forge_v(f, 0, 41);
	exact_one("mtf V1 rep1->rep2", f, slen, 0, 41);
	slen = forge_v(f, 1, 43);
	exact_one("mtf V2 rep0->rep3", f, slen, 1, 43);

	/* ZA: same shapes minus lanes (fo==bo) accept byte-identical. */
	slen = forge_v_flat(f, 0, 41);
	exact_one("za V1 fo==bo", f, slen, 0, 41);
	slen = forge_v_flat(f, 1, 43);
	exact_one("za V2 fo==bo", f, slen, 1, 43);

	/* ZA: fo==bo with sb>0 MUST fail closed (suffix bits missing). */
	slen = forge_v_flat(f, 0, 41);
	f[20] = 0x08;
	split_one("za fo==bo sb>0 reject", f, slen, 41);

	/* ZA unused-nonempty: port run22 COMP + 5B valid lanes, parse+ignore. */
	{
		static uint8_t z[64], base[64], mut[64];
		static const uint8_t LANES[5] = { 0x00, 0x00, 0x00, 0x00,
						  0x08 };
		size_t bn;

		bn = lzmesh_encode(base, sizeof(base), z, 22, NULL, 0xE05);
		if (bn != 23 || base[0] != 0x01) {
			t_fail("za unused base", "run22 shape moved");
			goto done;
		}
		memcpy(mut, base, 12);
		w16le(mut + 7, 17);
		memcpy(mut + 12, LANES, 5);
		memcpy(mut + 17, base + 12, 11);
		dsr = lzmesh_decoded_size(mut, 28);
		dr = lzmesh_decode(dec, sizeof(dec), mut, 28, NULL);
		ok = (dsr == 22 && dr == 22);
		for (i = 0; i < 22 && ok; i++)
			if (dec[i] != 0)
				ok = 0;
		if (!ok) {
			char detail[96];

			snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
				 (unsigned long)dsr, (unsigned long)dr);
			t_fail("za unused-nonempty accept", detail);
		} else {
			t_pass("za unused-nonempty accept");
		}
		/* Same region with ib0 index: STILL rejects (S3.3.d). */
		mut[16] = 0x00;
		split_one("za unused ib0 reject", mut, 28, 22);
	}

done:
	printf("---\nm16-mtf-za: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
