/* SPDX-License-Identifier: 0BSD */
/*
 * test_m19_c21.c — M19 REV C21 stop-at-fill pins (u6k, MERGE-M17).
 *
 * SPEC-v2: S4.3/Q14 (end checks; tokens-remain-after-fill -> C21;
 * consumed<litc -> C18; MATCH overhang -> truncate to ds, C17
 * clamp-down), S4.4 (term-clamp: intra-token MATCH overhang MUST
 * truncate any amount; whole-token remainder MUST reject C21),
 * S3.9/Q6/Q17 (rep sel 0..2 direct, sel 3 accepted-never-emitted;
 * new-dist len_short bits 4:0), S3.10/Q5 (mc->ml=mc+2; rep esc 7 +
 * extra mc-7, 1B iff <=254 else ff+u32LE), S3.11/Q7 (dist symbol
 * (sb<<3)|low3; sb0 = 0 lane bits), S2.3 (C6 fo<ds strict), S2.5
 * (C8-C12 counts), S1.8/Q9 (sizer framing-only: C21 is decoder-only,
 * so walk-ok + decode-0 proves the split), P-D7 (ret0-dst = garbage:
 * no dst-content assert on rejects).
 * Forges are 2-token all-RAW no-Huffman blocks (modes 0x0000,
 * bo==fo per u6l ZA rule): litc=1 'A', t0 = rep0 filling ds exactly
 * (1 + ml0 == ds), t1 remaining -> C21. Each row pairs a REJECT
 * (sizer==D, decode==0) with an ACCEPT control (same streams, ds=D+2
 * exact-fill-on-last, byte-exact all-'A'): the pair differs ONLY in
 * ds, so the flip is attributable to the remainder gate (u6k P2
 * discriminator logic via ds-pairing, no code neutralization).
 * Row 1 (mc11/D14) is the u6k-P2 shape; rows 5/6 mirror the n264/n265
 * len-escape steps (254 max-1B / ff-u32 5B). A MATCH-overhang clamp
 * accept (u6k-P4 analogue) guards C21 over-fire: intra-token overhang
 * truncates, whole-token remainder rejects.
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

/*
 * Forge a 2-token C21 block: lit 'A', t0 = rep0/lit0 with match count
 * mc0 (ml0 = mc0+2), t1 as given, dist bytes iff distc>0. ds is the
 * test variable. Returns total bytes.
 */
static size_t forge_c21(uint8_t *f, uint32_t ds, uint32_t mc0, uint8_t t1,
			uint32_t distc, uint8_t dsym)
{
	uint32_t extra = (mc0 > 6) ? mc0 - 7 : 0;
	uint32_t lb = 0;	/* len-stream bytes */
	uint32_t fo, p;

	if (mc0 > 6)
		lb = (extra <= 254) ? 1 : 5;
	fo = 9 + 1 + 2 + lb + distc;

	f[0] = 0x01;
	w32le(f + 1, ds);
	w16le(f + 5, (uint16_t)fo);
	w16le(f + 7, (uint16_t)fo);
	f[9] = 0x41;
	f[10] = (uint8_t)((mc0 <= 6) ? mc0 : 7);
	f[11] = t1;
	p = 12;
	if (lb == 1) {
		f[p++] = (uint8_t)extra;
	} else if (lb == 5) {
		f[p++] = 0xFF;
		w32le(f + p, extra);
		p += 4;
	}
	if (distc)
		f[p++] = dsym;
	w16le(f + p, 0x0000);
	w16le(f + p + 2, 2);
	w16le(f + p + 4, (uint16_t)lb);
	w16le(f + p + 6, 1);
	w16le(f + p + 8, (uint16_t)distc);
	p += 10;
	f[p++] = 0xFF;
	return (size_t)p;
}

static int all_a(const uint8_t *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (b[i] != 0x41)
			return 0;
	return 1;
}

/* One C21 row: reject at D=mc0+3, accept control at D+2 byte-exact. */
static void c21_row(const char *tag, uint32_t mc0, uint8_t t1,
		    uint32_t distc, uint8_t dsym)
{
	char name[96], detail[160];
	static uint8_t f[64], dec[512];
	uint32_t d = mc0 + 3;
	size_t slen, dsr, dr;

	slen = forge_c21(f, d, mc0, t1, distc, dsym);
	snprintf(name, sizeof(name), "c21 %s reject D=%lu", tag,
		 (unsigned long)d);
	dsr = lzmesh_decoded_size(f, slen);
	dr = lzmesh_decode(dec, sizeof(dec), f, slen, NULL);
	if (dsr != d || dr != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu, want %lu/0",
			 (unsigned long)dsr, (unsigned long)dr,
			 (unsigned long)d);
		t_fail(name, detail);
	} else {
		t_pass(name);
	}

	slen = forge_c21(f, d + 2, mc0, t1, distc, dsym);
	snprintf(name, sizeof(name), "c21 %s control D=%lu", tag,
		 (unsigned long)(d + 2));
	dsr = lzmesh_decoded_size(f, slen);
	dr = lzmesh_decode(dec, sizeof(dec), f, slen, NULL);
	if (dsr != d + 2 || dr != d + 2 || !all_a(dec, d + 2)) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu want %lu exact",
			 (unsigned long)dsr, (unsigned long)dr,
			 (unsigned long)(d + 2));
		t_fail(name, detail);
	} else {
		t_pass(name);
	}
}

/*
 * MATCH-overhang clamp accept (u6k-P4 analogue): ds=15, single rep0
 * ml18 (mc16, esc extra 9): 1+18=19 > 15 intra-token -> truncate to
 * ds ACCEPT 15 all-'A'. C21 must NOT fire (no whole-token remainder).
 */
static void clamp_accept(void)
{
	char detail[128];
	static uint8_t f[32], dec[64];
	size_t dsr, dr;

	f[0] = 0x01;
	w32le(f + 1, 15);
	w16le(f + 5, 12);
	w16le(f + 7, 12);
	f[9] = 0x41;
	f[10] = 0x07;
	f[11] = 0x09;
	w16le(f + 12, 0x0000);
	w16le(f + 14, 1);
	w16le(f + 16, 1);
	w16le(f + 18, 1);
	w16le(f + 20, 0);
	f[22] = 0xFF;
	dsr = lzmesh_decoded_size(f, 23);
	dr = lzmesh_decode(dec, sizeof(dec), f, 23, NULL);
	if (dsr != 15 || dr != 15 || !all_a(dec, 15)) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu, want 15 exact",
			 (unsigned long)dsr, (unsigned long)dr);
		t_fail("c21 clamp match-overhang accept", detail);
	} else {
		t_pass("c21 clamp match-overhang accept");
	}
}

int main(void)
{
	c21_row("mc11-P2", 11, 0x00, 0, 0x00);
	c21_row("mc13", 13, 0x00, 0, 0x00);
	c21_row("mc19-fresh22", 19, 0x00, 0, 0x00);
	c21_row("mc100", 100, 0x00, 0, 0x00);
	c21_row("mc261-max1B", 261, 0x00, 0, 0x00);
	c21_row("mc262-ffu32", 262, 0x00, 0, 0x00);
	c21_row("mc19-t1rep3", 19, 0x18, 0, 0x00);
	c21_row("mc19-t1newdist", 19, 0x20, 1, 0x00);
	clamp_accept();
	printf("---\nm19-c21: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
