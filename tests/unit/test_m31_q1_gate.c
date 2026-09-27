/* SPDX-License-Identifier: 0BSD */
/*
 * test_m31_q1_gate.c — M31 Q1 exactly-N/L0 gate vectors (u6v P1-ANSWERS
 * Q1 rules A1-A6; u6v REFUSED at M31 T5 on the B1 later-block misfire
 * — Q1 itself unrefuted, rework pending; these pins lock the
 * fail-closed direction on the reverted tree and the gate direction
 * once the rework lands).
 *
 * SPEC-v2: S3.2 (modes 0/1/2), S2.5/Q2 (footer modes/counts), S2.6
 * (count0 MUST be RAW), S3.3/Q3 (index_bits = last_byte>>3, seven
 * packed LSB-first fields, lane7 = remainder), S3.8/Q4 (Huffman
 * headers fail-closed), S1.8/Q9 (sizer framing-only: decoder-only
 * gates prove as walk-ok + decode-0). P1-ANSWERS Q1: 51B family
 * (bo=11 fo=40, L0 10B, L1..L7 2B, index 5B ib=4, modes 0x4A =
 * lit-HUFFMAN/tok-REPEAT/len-REPEAT/dist-RAW, tc=1 lc=1 litc=ds
 * dc=0) takes Rule A1 (no head tag 11 AND exactly N=1 heads tag
 * 01/10, head-only) + A2 (L0[4]: T=1 one-hot-bits1..7 else even
 * nonzero; odd/00 always refuse) + A3 (L0[8] lo-nib: T=1
 * {1,2,4,a,c} else {2,3,4,5,9,a,c}) + A4 (L0[0] in {09,91}) + A5
 * (L0[6] one-hot-8) + A6 (L0[1,2,3,5,7]==00, L0[9] free), T = any
 * INACTIVE head bit2 (active exempt, heads only). 59B (L0 11B,
 * lanes 3B, bo=11 fo=48) + 75B (L0 13B, lanes 5B, bo=11 fo=64)
 * take A1 with N=2. Per-REV-row map (P1 8/8): s01/s04/s09/s15/
 * s07-sp = 0-active 51B; s07p = 1-active 59B; s11 = 1-active 75B;
 * s02 = L0[8]=00.
 *
 * Method: hand forges per the P1 geometry (ds=100 arbitrary, must
 * exceed fo per C6). The 51B index bytes are PROVEN parsable by
 * test_m31_b3_index.c (ib4 q51-geometry accept), so 51B forges
 * reach the gate-or-Huffman stage, not parse-fail; 59B/75B reuse
 * the same ib=4 mechanics. Lane/stream bytes are arbitrary (not
 * valid Huffman), so refuse here is overdetermined (gate +
 * Huffman-fail both fail-closed) — these pins lock that each
 * Apple-refused mutant class stays refused; row-level gate proof
 * is battery R8/R9 at the rework merge. No valid-shape accept pins
 * (those need oracle-valid Huffman streams, outside the wall).
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

/*
 * 51B base: bo=11 fo=40, L0 10B T=0-valid, 1-active@L5, proven
 * index, footer modes 0x4A tc1/lc1/litc100/dc0, END. Mutants edit
 * the returned buffer in place. Lanes: L1@21 L2@23 L3@25 L4@27
 * L5@29 L6@31 L7@33 (2B each); L0@11; index@35.
 */
static size_t forge_51(uint8_t *f)
{
	static const uint8_t l0[10] =
		{ 0x09, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x02,
		  0x00 };
	static const uint8_t idx[5] = { 0x2A, 0x22, 0x22, 0x02, 0x20 };

	memset(f, 0, 51);
	f[0] = 0x01;
	w32le(f + 1, 100);
	w16le(f + 5, 11);
	w16le(f + 7, 40);
	f[9] = 0x41;
	f[10] = 0x41;
	memcpy(f + 11, l0, sizeof(l0));
	f[29] = 0x02;
	memcpy(f + 35, idx, sizeof(idx));
	w16le(f + 40, 0x004A);
	w16le(f + 42, 1);
	w16le(f + 44, 1);
	w16le(f + 46, 100);
	w16le(f + 48, 0);
	f[50] = 0xFF;
	return 51;
}

/*
 * 59B base: bo=11 fo=48, L0 11B zero, 2-active@L1+L6 (s07p class),
 * index fields [11,3,3,3,3,3,3], same footer/END. Lanes 3B:
 * L1@22 L2@25 L3@28 L4@31 L5@34 L6@37 L7@40; L0@11; index@43.
 */
static size_t forge_59(uint8_t *f)
{
	static const uint8_t idx[5] = { 0x3B, 0x33, 0x33, 0x03, 0x20 };

	memset(f, 0, 59);
	f[0] = 0x01;
	w32le(f + 1, 100);
	w16le(f + 5, 11);
	w16le(f + 7, 48);
	f[9] = 0x41;
	f[10] = 0x41;
	f[22] = 0x01;
	f[37] = 0x01;
	memcpy(f + 43, idx, sizeof(idx));
	w16le(f + 48, 0x004A);
	w16le(f + 50, 1);
	w16le(f + 52, 1);
	w16le(f + 54, 100);
	w16le(f + 56, 0);
	f[58] = 0xFF;
	return 59;
}

/*
 * 75B base: bo=11 fo=64, L0 13B zero, 2-active@L4+L7 (s11 class,
 * L7[0]=0xA2), index fields [13,5,5,5,5,5,5]. Lanes 5B: L1@24
 * L2@29 L3@34 L4@39 L5@44 L6@49 L7@54; L0@11; index@59.
 */
static size_t forge_75(uint8_t *f)
{
	static const uint8_t idx[5] = { 0x5D, 0x55, 0x55, 0x05, 0x20 };

	memset(f, 0, 75);
	f[0] = 0x01;
	w32le(f + 1, 100);
	w16le(f + 5, 11);
	w16le(f + 7, 64);
	f[9] = 0x41;
	f[10] = 0x41;
	f[39] = 0x01;
	f[54] = 0xA2;
	memcpy(f + 59, idx, sizeof(idx));
	w16le(f + 64, 0x004A);
	w16le(f + 66, 1);
	w16le(f + 68, 1);
	w16le(f + 70, 100);
	w16le(f + 72, 0);
	f[74] = 0xFF;
	return 75;
}

int main(void)
{
	static uint8_t f[80];
	size_t slen;

	/* A1 51B N=1: 0-active (s01/s04/s09/s15/s07-sp class). */
	slen = forge_51(f);
	f[29] = 0x00;
	split_one("q1 51 0-active refuse", f, slen, 100);

	/* A1 51B: 2-active L4+L5 + 2-active L1+L5. */
	slen = forge_51(f);
	f[27] = 0x01;
	split_one("q1 51 2-active L4L5 refuse", f, slen, 100);
	slen = forge_51(f);
	f[21] = 0x01;
	split_one("q1 51 2-active L1L5 refuse", f, slen, 100);

	/* A1 tag-11 never: at the active head + at an inactive head. */
	slen = forge_51(f);
	f[29] = 0x03;
	split_one("q1 51 tag11 active refuse", f, slen, 100);
	slen = forge_51(f);
	f[21] = 0x03;
	split_one("q1 51 tag11 inactive refuse", f, slen, 100);

	/* A2 L0[4]@15 (T=0 base): 00 + odd refuse. */
	slen = forge_51(f);
	f[15] = 0x00;
	split_one("q1 51 L0[4]=00 refuse", f, slen, 100);
	slen = forge_51(f);
	f[15] = 0x07;
	split_one("q1 51 L0[4]=07 odd refuse", f, slen, 100);

	/*
	 * A2 strict (T=1 via L1[0] bit2, count kept N=1): 0x06
	 * even-nonzero but not one-hot-bits1..7 refuses.
	 */
	slen = forge_51(f);
	f[21] = 0x04;
	split_one("q1 51 T=1 L0[4]=06 strict refuse", f, slen, 100);

	/* A3 L0[8]@19: 00 (s02 class) + lo-nib 6 refuse. */
	slen = forge_51(f);
	f[19] = 0x00;
	split_one("q1 51 L0[8]=00 s02 refuse", f, slen, 100);
	slen = forge_51(f);
	f[19] = 0x06;
	split_one("q1 51 L0[8]=06 refuse", f, slen, 100);

	/*
	 * A3 strict (T=1): lo-nib 3 is T=0-loose-only, refused
	 * under strict {1,2,4,a,c}.
	 */
	slen = forge_51(f);
	f[21] = 0x04;
	f[19] = 0x03;
	split_one("q1 51 T=1 L0[8]=03 strict refuse", f, slen, 100);

	/* A4 L0[0]@11 must be 09/91. */
	slen = forge_51(f);
	f[11] = 0x08;
	split_one("q1 51 L0[0]=08 refuse", f, slen, 100);

	/* A5 L0[6]@17 must be one-hot-8. */
	slen = forge_51(f);
	f[17] = 0x03;
	split_one("q1 51 L0[6]=03 refuse", f, slen, 100);

	/* A6 L0[1]@12 must be 00. */
	slen = forge_51(f);
	f[12] = 0x01;
	split_one("q1 51 L0[1]=01 refuse", f, slen, 100);

	/* A1 59B N=2 (s07p class): 2->1 + 3-active + tag-11. */
	slen = forge_59(f);
	f[37] = 0x00;
	split_one("q1 59 1-active refuse", f, slen, 100);
	slen = forge_59(f);
	f[28] = 0x02;
	split_one("q1 59 3-active refuse", f, slen, 100);
	slen = forge_59(f);
	f[22] = 0x03;
	split_one("q1 59 tag11 refuse", f, slen, 100);

	/* A1 75B N=2 (s11 class): 2->1 (a2->a0) + 3-active + tag-11. */
	slen = forge_75(f);
	f[54] = 0xA0;
	split_one("q1 75 1-active s11 refuse", f, slen, 100);
	slen = forge_75(f);
	f[29] = 0x01;
	split_one("q1 75 3-active refuse", f, slen, 100);
	slen = forge_75(f);
	f[39] = 0x03;
	split_one("q1 75 tag11 refuse", f, slen, 100);

	printf("---\nm31-q1-gate: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
