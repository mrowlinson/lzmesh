/* SPDX-License-Identifier: 0BSD */
/*
 * test_m31_b3_index.c — M31 B3 index-validation pins (u6v, GAPLOG-u6v
 * FLUSH3; u6v REFUSED at M31 T5 on B1 later-block misfire — B2/B3
 * were zero-delta pins, still valid on the reverted tree).
 *
 * SPEC-v2: S3.3 (index_bits = last_byte>>3 range 1..23, ib0/ib24+
 * reject; index_size = max(4,(7*ib+12)>>3); seven packed LSB-first
 * fields = lane0..6 lens, lane7 = remainder; starts<=payload;
 * region MUST exceed index; over-long ACCEPT, empty-payload
 * region==index REJECT), S4.6/u6l (unused-nonempty lanes parsed +
 * ignored on RAW shapes), S1.8/Q9 (sizer framing-only: index-invalid
 * proves as walk-ok + decode-0). P1-ANSWERS Q2 Rule B3: fields
 * validated by sum<=payload, pads ignored, ib-range enforced.
 *
 * Method: proven-accept RAW match forges (m24 forge_match1) + grown
 * lanes (fo past bo, payload + index, footer shifted). RAW fetch
 * ignores lane content, so ACCEPT legs prove the INDEX parses
 * (packing recipe validated through the public API) while REFUSE
 * legs pin each invalid class. The ib=4 shape reuses the exact
 * Q1-51B field geometry (L0=10, L1..L6=2, lane7=2, index 5B) — its
 * ACCEPT here proves the index bytes test_m31_q1_gate.c reuses.
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

/*
 * Grow lanes: insert paylen payload bytes + idxlen index bytes at bo,
 * shifting footer+END right; bo stays, fo grows. Returns new length.
 */
static size_t add_lanes(uint8_t *f, size_t len, const uint8_t *pay,
			size_t paylen, const uint8_t *idx, size_t idxlen)
{
	uint16_t bo = r16le(f + 5);
	size_t fo = (size_t)bo;
	size_t grow = paylen + idxlen;

	memmove(f + fo + grow, f + fo, len - fo);
	memcpy(f + fo, pay, paylen);
	memcpy(f + fo + paylen, idx, idxlen);
	w16le(f + 7, (uint16_t)(bo + grow));
	return len + grow;
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
	static uint8_t f[128];
	static const uint8_t pay1[1] = { 0x00 };
	static const uint8_t idx_ok[4] = { 0x00, 0x00, 0x00, 0x08 };
	static const uint8_t idx_padff[4] = { 0x00, 0xFF, 0xFF, 0x08 };
	static const uint8_t idx_ib0[4] = { 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t idx_ibff[4] = { 0x00, 0x00, 0x00, 0xFF };
	/* ib=4 Q1-51B geometry: fields [10,2,2,2,2,2,2], lane7=2. */
	static const uint8_t idx_51[5] = { 0x2A, 0x22, 0x22, 0x02, 0x20 };
	static const uint8_t idx_51sumbad[5] = { 0x2F, 0x22, 0x22, 0x02,
						 0x20 };
	static uint8_t pay24[24];
	static uint8_t pay24ff[24];
	size_t slen;

	memset(pay24, 0x00, sizeof(pay24));
	memset(pay24ff, 0xFF, sizeof(pay24ff));

	/*
	 * ib=1 shape (ds=18 match, fo 12->17): valid index ACCEPTs
	 * (unused lanes ignored); pad bytes 0xFF ACCEPT (pads
	 * ignored, P1 B3); ib0 / ib31 REFUSE (ib-range enforced).
	 */
	slen = forge_match1(f, 18, 0x42, 8);
	slen = add_lanes(f, slen, pay1, sizeof(pay1), idx_ok, sizeof(idx_ok));
	exact_fill_one("b3 ib1 valid accept", f, slen, 18, 0x42);
	slen = forge_match1(f, 18, 0x42, 8);
	slen = add_lanes(f, slen, pay1, sizeof(pay1), idx_padff,
			 sizeof(idx_padff));
	exact_fill_one("b3 ib1 padFF accept", f, slen, 18, 0x42);
	slen = forge_match1(f, 18, 0x42, 8);
	slen = add_lanes(f, slen, pay1, sizeof(pay1), idx_ib0,
			 sizeof(idx_ib0));
	split_one("b3 ib1 ib0 refuse", f, slen, 18);
	slen = forge_match1(f, 18, 0x42, 8);
	slen = add_lanes(f, slen, pay1, sizeof(pay1), idx_ibff,
			 sizeof(idx_ibff));
	split_one("b3 ib1 ib31 refuse", f, slen, 18);

	/* Empty payload (region==index, fo 12->16): REJECT (S3.3). */
	slen = forge_match1(f, 18, 0x42, 8);
	slen = add_lanes(f, slen, pay1, 0, idx_ok, sizeof(idx_ok));
	split_one("b3 ib1 empty-payload refuse", f, slen, 18);

	/*
	 * ib=4 shape (ds=64 match extra 54, fo 12->41): Q1-51B index
	 * geometry ACCEPTs with zero or FF payload (content ignored);
	 * L0=15 sum-break (15+12=27 > payload 24) REFUSEs (sum
	 * validated, P1 B3).
	 */
	slen = forge_match1(f, 64, 0x45, 54);
	slen = add_lanes(f, slen, pay24, sizeof(pay24), idx_51,
			 sizeof(idx_51));
	exact_fill_one("b3 ib4 q51-geometry accept", f, slen, 64, 0x45);
	slen = forge_match1(f, 64, 0x45, 54);
	slen = add_lanes(f, slen, pay24ff, sizeof(pay24ff), idx_51,
			 sizeof(idx_51));
	exact_fill_one("b3 ib4 payloadFF accept", f, slen, 64, 0x45);
	slen = forge_match1(f, 64, 0x45, 54);
	slen = add_lanes(f, slen, pay24, sizeof(pay24), idx_51sumbad,
			 sizeof(idx_51sumbad));
	split_one("b3 ib4 sum-break refuse", f, slen, 64);

	printf("---\nm31-b3-index: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
