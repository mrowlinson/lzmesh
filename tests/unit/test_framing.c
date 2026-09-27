/* SPDX-License-Identifier: 0BSD */
/*
 * test_framing.c — container framing: tags, RAW shape, trailing
 * divergence, COMP header gates, truncation.
 *
 * SPEC-v2: S2.1 (tags 00/01/FF only), S2.2 (RAW shape), S2.3/Q1
 * (COMP header tag+ds+bo+fo, block len fo+10, fo<ds strict, bo<=fo,
 * bo>=9, ds!=0), S2.4/Q8 (C6/C7 strict; sizer rejects inexact END
 * landing while decoder returns on END and ignores trailing),
 * S1.8/Q9 (sizer framing-only), S4.5 (truncation 0), S4.8 (END handling).
 *
 * COMP fixtures are port-encoder bytes (zeros-22 e05) mutated in the
 * header only; RAW fixtures are hand-forged per S2.2 (tag + u32 ds +
 * bytes + END). Public API only. Exit 0 iff zero FAILs.
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

/* Assert sizer/dec pair on one input. */
static void pair_one(const char *name, const uint8_t *src, size_t slen,
		     size_t want_ds, size_t want_dec)
{
	char detail[128];
	static uint8_t dec[4096];
	size_t ds = lzmesh_decoded_size(src, slen);
	size_t dr = lzmesh_decode(dec, sizeof(dec), src, slen, NULL);

	if (ds != want_ds || dr != want_dec) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu, want %lu/%lu",
			 (unsigned long)ds, (unsigned long)dr,
			 (unsigned long)want_ds, (unsigned long)want_dec);
		t_fail(name, detail);
		return;
	}
	t_pass(name);
}

static void tag_rejects(void)
{
	/* Valid RAW-1 block bytes, then a bad tag with slack + END. */
	static uint8_t blk[16] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x41,
				   0x02, 0x00, 0x00, 0x00, 0x00, 0xFF };
	static const uint8_t TAGS[] = { 0x02, 0x03, 0x7F, 0x80, 0xFE };
	char name[48];
	size_t i;

	for (i = 0; i < sizeof(TAGS); i++) {
		blk[6] = TAGS[i];
		snprintf(name, sizeof(name), "tag-reject %02x", TAGS[i]);
		pair_one(name, blk, 12, 0, 0);
	}
}

static void raw_forge(void)
{
	static uint8_t raw[2048], dec[2048];
	size_t i, ds, dr;
	int ok;

	/* Hand-forged RAW-1000 per S2.2. */
	raw[0] = 0x00;
	raw[1] = 0xE8;
	raw[2] = 0x03;
	raw[3] = 0x00;
	raw[4] = 0x00;
	for (i = 0; i < 1000; i++)
		raw[5 + i] = (uint8_t)(i & 0xFF);
	raw[1005] = 0xFF;
	ds = lzmesh_decoded_size(raw, 1006);
	dr = lzmesh_decode(dec, sizeof(dec), raw, 1006, NULL);
	ok = (ds == 1000 && dr == 1000);
	for (i = 0; i < 1000 && ok; i++)
		if (dec[i] != (uint8_t)(i & 0xFF))
			ok = 0;
	if (!ok)
		t_fail("raw-1000", "sizer/decode/bytes");
	else
		t_pass("raw-1000");
	/* Hand-forged RAW-1 'A' (S2.7 degenerate vector). */
	{
		static const uint8_t R1[7] = { 0x00, 0x01, 0x00, 0x00,
					       0x00, 0x41, 0xFF };
		uint8_t d1[8];

		ds = lzmesh_decoded_size(R1, 7);
		dr = lzmesh_decode(d1, sizeof(d1), R1, 7, NULL);
		if (ds != 1 || dr != 1 || d1[0] != 0x41)
			t_fail("raw-1A", "sizer/decode/bytes");
		else
			t_pass("raw-1A");
	}
}

static void trailing_divergence(void)
{
	static uint8_t raw[2048], dec[2048];
	static uint8_t z[64], enc[64], mut[72];
	size_t i, eret, ds, dr;
	int ok;

	raw[0] = 0x00;
	raw[1] = 0xE8;
	raw[2] = 0x03;
	raw[3] = 0x00;
	raw[4] = 0x00;
	for (i = 0; i < 1000; i++)
		raw[5 + i] = (uint8_t)(i & 0xFF);
	raw[1005] = 0xFF;
	/* +1 trailing byte: sizer 0 AND decoder 1000 (Q8 pin). */
	raw[1006] = 0x00;
	ds = lzmesh_decoded_size(raw, 1007);
	dr = lzmesh_decode(dec, sizeof(dec), raw, 1007, NULL);
	ok = (ds == 0 && dr == 1000);
	for (i = 0; i < 1000 && ok; i++)
		if (dec[i] != (uint8_t)(i & 0xFF))
			ok = 0;
	if (!ok)
		t_fail("trail raw+1", "want sizer 0 + dec 1000 exact");
	else
		t_pass("trail raw+1");
	/* +5 trailing bytes. */
	raw[1006] = 0x01;
	raw[1007] = 0x02;
	raw[1008] = 0x03;
	raw[1009] = 0x04;
	raw[1010] = 0x05;
	ds = lzmesh_decoded_size(raw, 1011);
	dr = lzmesh_decode(dec, sizeof(dec), raw, 1011, NULL);
	ok = (ds == 0 && dr == 1000);
	for (i = 0; i < 1000 && ok; i++)
		if (dec[i] != (uint8_t)(i & 0xFF))
			ok = 0;
	if (!ok)
		t_fail("trail raw+5", "want sizer 0 + dec 1000 exact");
	else
		t_pass("trail raw+5");
	/* COMP + 2 trailing bytes: sizer 0, decoder 22 zeros. */
	eret = lzmesh_encode(enc, sizeof(enc), z, 22, NULL, 0xE05);
	if (eret != 23 || enc[0] != 0x01 || u32le(enc + 1) != 22) {
		t_fail("trail comp+2", "base COMP shape moved");
		return;
	}
	memcpy(mut, enc, eret);
	mut[eret] = 0xAA;
	mut[eret + 1] = 0xBB;
	ds = lzmesh_decoded_size(mut, eret + 2);
	dr = lzmesh_decode(dec, sizeof(dec), mut, eret + 2, NULL);
	ok = (ds == 0 && dr == 22);
	for (i = 0; i < 22 && ok; i++)
		if (dec[i] != 0)
			ok = 0;
	if (!ok)
		t_fail("trail comp+2", "want sizer 0 + dec 22 zeros");
	else
		t_pass("trail comp+2");
}

static void header_gates(void)
{
	static uint8_t z[64], enc[64], mut[64];
	size_t eret;

	eret = lzmesh_encode(enc, sizeof(enc), z, 22, NULL, 0xE05);
	if (eret != 23 || enc[0] != 0x01) {
		t_fail("hdr base", "base COMP shape moved");
		return;
	}
	/* C5: bo > fo. */
	memcpy(mut, enc, eret);
	w16le(mut + 5, 13);
	pair_one("hdr C5 bo>fo", mut, eret, 0, 0);
	/* C4: bo < 9. */
	memcpy(mut, enc, eret);
	w16le(mut + 5, 8);
	pair_one("hdr C4 bo<9", mut, eret, 0, 0);
	/* C2: ds == 0. */
	memcpy(mut, enc, eret);
	mut[1] = 0;
	mut[2] = 0;
	mut[3] = 0;
	mut[4] = 0;
	pair_one("hdr C2 ds0", mut, eret, 0, 0);
	/* C6 strict: fo == ds rejects (padded so END lands exactly). */
	{
		static uint8_t big[64];
		size_t nfo = 22;

		memcpy(big, enc, 12);
		w16le(big + 7, (uint16_t)nfo);
		memset(big + 12, 0, nfo - 12);
		memcpy(big + nfo, enc + 12, 10);
		big[nfo + 10] = 0xFF;
		pair_one("hdr C6 fo==ds", big, nfo + 11, 0, 0);
	}
	/* C6 strict: fo > ds rejects. */
	{
		static uint8_t big[64];
		size_t nfo = 23;

		memcpy(big, enc, 12);
		w16le(big + 7, (uint16_t)nfo);
		memset(big + 12, 0, nfo - 12);
		memcpy(big + nfo, enc + 12, 10);
		big[nfo + 10] = 0xFF;
		pair_one("hdr C6 fo>ds", big, nfo + 11, 0, 0);
	}
}

static void truncation(void)
{
	static const uint8_t R1[7] = { 0x00, 0x01, 0x00, 0x00,
				       0x00, 0x41, 0xFF };
	static uint8_t z[64], enc[64];
	size_t eret;

	pair_one("trunc raw-noEND", R1, 6, 0, 0);
	pair_one("trunc empty", R1, 0, 0, 0);
	eret = lzmesh_encode(enc, sizeof(enc), z, 22, NULL, 0xE05);
	pair_one("trunc comp-noEND", enc, eret - 1, 0, 0);
}

int main(void)
{
	tag_rejects();
	raw_forge();
	trailing_divergence();
	header_gates();
	truncation();
	printf("---\nframing: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
