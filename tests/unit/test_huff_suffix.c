/* SPDX-License-Identifier: 0BSD */
/*
 * test_huff_suffix.c — Huffman/suffix decode vectors: footer modes,
 * count gates (decoder-only), hi-bits tolerance, C13 distance gate.
 *
 * SPEC-v2: S2.5/Q2 (footer modes/counts), S2.6 (count0 MUST be RAW),
 * S3.2 (modes 0/1/2; 3-7 reject), S3.8/Q4 (Huffman headers; fail-closed
 * on empty lanes), S3.11/Q7 (dist symbol (sb<<3)|low3, d formula),
 * S4.3 (C13 post-literal strict: reject iff d==0 or d>w), S7.2
 * (reserved modes reject; footer hi-bits ignored), S1.8/Q9 (sizer
 * framing-only: footer/count/payload-invalid is decoder-only, so
 * walk-ok + decode-0 forges prove the split).
 *
 * Base fixture is port-encoder zeros-22 e05 COMP (S3.13 fresh-22 shape:
 * 23B, modes-[1,1,1,0], tok1/len1/lit1/dist0), mutated per case.
 * The C13 forge is hand-built per S2.3/S3.9/S3.10/S3.11 (single
 * new-dist token d=2 at w=1). Exact Huffman-bitstream and multi-token
 * suffix-bit vectors are pending (need lane-region forges or oracle
 * vectors; see re-scratch probes, NOT committed). Public API only.
 * Exit 0 iff zero FAILs.
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

static uint16_t u16le(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t u32le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void w16le(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static uint8_t BASE[64];
static size_t BASEN;
static uint16_t FO;

/* sizer==22 + decode==0 split proof (Q9: decoder-only gates). */
static void split_one(const char *name, const uint8_t *src, size_t slen)
{
	char detail[128];
	static uint8_t dec[128];
	size_t ds = lzmesh_decoded_size(src, slen);
	size_t dr = lzmesh_decode(dec, sizeof(dec), src, slen, NULL);

	if (ds != 22 || dr != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu, want 22/0",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail(name, detail);
		return;
	}
	t_pass(name);
}

static int base_load(void)
{
	static uint8_t z[64];

	BASEN = lzmesh_encode(BASE, sizeof(BASE), z, 22, NULL, 0xE05);
	if (BASEN != 23 || BASE[0] != 0x01 || u32le(BASE + 1) != 22 ||
	    BASE[BASEN - 1] != 0xFF) {
		t_fail("base shape", "zeros-22 e05 is not 23B COMP");
		return 0;
	}
	FO = u16le(BASE + 7);
	if (u16le(BASE + 5) != 12 || FO != 12 ||
	    u16le(BASE + FO) != 0x0049 || u16le(BASE + FO + 2) != 1 ||
	    u16le(BASE + FO + 4) != 1 || u16le(BASE + FO + 6) != 1 ||
	    u16le(BASE + FO + 8) != 0) {
		t_fail("base shape", "header/footer not fresh-22 form");
		return 0;
	}
	t_pass("base shape");
	return 1;
}

static void huffman_reject(void)
{
	uint8_t mut[64];

	/* Each substream flipped to HUFFMAN over empty lanes: fail-closed. */
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)((u16le(mut + FO) & ~0x7u) | 0x2u));
	split_one("huf lit", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)((u16le(mut + FO) & ~(0x7u << 3)) |
					   (0x2u << 3)));
	split_one("huf tok", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)((u16le(mut + FO) & ~(0x7u << 6)) |
					   (0x2u << 6)));
	split_one("huf len", mut, BASEN);
}

static void reserved_reject(void)
{
	uint8_t mut[64];

	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)((u16le(mut + FO) & ~0x7u) | 0x3u));
	split_one("res lit3", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)((u16le(mut + FO) & ~0x7u) | 0x7u));
	split_one("res lit7", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)((u16le(mut + FO) & ~(0x7u << 3)) |
					   (0x3u << 3)));
	split_one("res tok3", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)((u16le(mut + FO) & ~(0x7u << 6)) |
					   (0x5u << 6)));
	split_one("res len5", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)(u16le(mut + FO) | (0x3u << 9)));
	split_one("res dist3-c0", mut, BASEN);
}

static void count0_rule(void)
{
	uint8_t mut[64];

	/* distc==0 with REPEAT or HUFFMAN MUST reject (S2.6). */
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)(u16le(mut + FO) | (0x1u << 9)));
	split_one("c0 dist-repeat", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)(u16le(mut + FO) | (0x2u << 9)));
	split_one("c0 dist-huf", mut, BASEN);
}

static void count_gates(void)
{
	uint8_t mut[64];

	/* C8..C12 decoder-only: walk still 22, decode 0. */
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO + 2, 0);
	split_one("C8 tok0", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO + 2, 23);
	split_one("C9 tok>ds", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO + 4, 23);
	split_one("C10 len>ds", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO + 6, 23);
	split_one("C11 lit>ds", mut, BASEN);
	memcpy(mut, BASE, BASEN);
	w16le(mut + FO + 8, 2);
	split_one("C12 dist>tok", mut, BASEN);
}

static void hibits_ignored(void)
{
	uint8_t mut[64], dec[64];
	char detail[96];
	size_t i, ds, dr;
	int ok;

	memcpy(mut, BASE, BASEN);
	w16le(mut + FO, (uint16_t)(u16le(mut + FO) | 0xF000u));
	ds = lzmesh_decoded_size(mut, BASEN);
	dr = lzmesh_decode(dec, sizeof(dec), mut, BASEN, NULL);
	ok = (ds == 22 && dr == 22);
	for (i = 0; i < 22 && ok; i++)
		if (dec[i] != 0)
			ok = 0;
	if (!ok) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail("modes hibits", detail);
	} else {
		t_pass("modes hibits");
	}
}

static void c13_gate(void)
{
	/*
	 * ds=20, tokc=1, distc=1: token 0x31 = new-dist len_short 17
	 * (sel6, ml19), dist symbol 0x01 = sb0/low3-1 -> d=2. At the
	 * match w=1 (first byte only), d=2 > w -> C13 rejects. Framing
	 * is valid, so the sizer still walks 20 (replay-only gate).
	 */
	static uint8_t f[23];
	char detail[96];
	size_t ds, dr;

	f[0] = 0x01;
	f[1] = 20;
	f[2] = 0;
	f[3] = 0;
	f[4] = 0;
	f[5] = 12;
	f[6] = 0;
	f[7] = 12;
	f[8] = 0;
	f[9] = 0x41;
	f[10] = 0x31;
	f[11] = 0x01;
	w16le(f + 12, 0x0000);
	w16le(f + 14, 1);
	w16le(f + 16, 0);
	w16le(f + 18, 1);
	w16le(f + 20, 1);
	f[22] = 0xFF;
	{
		static uint8_t dec[64];

		ds = lzmesh_decoded_size(f, sizeof(f));
		dr = lzmesh_decode(dec, sizeof(dec), f, sizeof(f), NULL);
	}
	if (ds != 20 || dr != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu, want 20/0",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail("C13 d>w", detail);
	} else {
		t_pass("C13 d>w");
	}
}

int main(void)
{
	if (!base_load()) {
		printf("---\nhuff-suffix: pass=%d fail=%d\n", g_pass, g_fail);
		return 1;
	}
	huffman_reject();
	reserved_reject();
	count0_rule();
	count_gates();
	hibits_ignored();
	c13_gate();
	printf("---\nhuff-suffix: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
