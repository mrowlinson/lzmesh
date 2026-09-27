/* SPDX-License-Identifier: 0BSD */
/*
 * test_m24_e2a_tail.c — M24 E2a-tail split pins (u22, MERGE-M24 T3/T6).
 *
 * SPEC-v2: S5.4 (budgets B0 0x4000 -> B1 0x8000 -> B2+ 0xF3F0
 * sticky; overshoot +0..+6 one token; px0 16385-16390, px1
 * 32768-32774; TEST2 absorb iff (pos/stop)+9>E, tail<=8 absorbs,
 * tail>=9 splits), S2.9 (tails 1-8 absorb, >=9 split), S2.2 (RAW
 * shape tag00+u32+payload, no footer), S2.8 (walk sum==n
 * END-exact), S5.3 (TIER-1 D>fo per block, TIER-2 whole), S4.1/Q18
 * (first-only pre-emit: first extra=ds-4, later extra=ds-3, u17
 * frozen), S3.10/Q5/Q14 (C18 exact, lit_run=extra+3), S4.4
 * (trunc2), S5.1 (determinism).
 *
 * Fix (GAPLOG-u22, merged M24): U20-TAIL13 FALSIFIED by COMP-R7 —
 * the oracle has NO tail minimum beyond TEST2, so tails 9..12
 * SPLIT with a RAW tiny-tail instead of absorbing: 16394-16397 ->
 * 41/42/43/44B 2-block [16385,9..12] COMP+RAW, 49162-49165 ->
 * 67/68/69/70B 3-block [16385,32768,9..12] COMP+COMP+RAW. M24 T6
 * confirms all 8 windows on the port binary (9/9 with the 16393
 * freeze pin). Tail COMP is impossible for ds<13 (L0 shape fo12,
 * C6 strict), hence the RAW tail. COMP blocks reuse o53/B1 byte
 * shapes (blk0 ds16385 26B bo16/fo16 extra ds-4; blk1 ds32768
 * later 26B extra ds-3). Changed bytes ONLY n in 16394-16397 +
 * 49162-49165; every other n is byte-frozen (o53/o79/onset pins
 * prove it here, mirroring test_m22_e2a). Note the 7-vs-8
 * corpus-subset: E2a ident was 29/36 (7 sampled windows); the fix
 * covers the FULL 8-window class, so row-level close (36/36) is
 * decided at the R7 compverify rerun. n>65535 stays RAW (U10-CNT,
 * frozen, pinned by m19-m12).
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

static uint32_t u32le(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t u16le(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

/*
 * Verify one L0 COMP block at off: tag 01, ds, bo==fo==16 (5B
 * extra) or 12 (1B extra), lit fill, tok 0xC0, len extra ==
 * want_extra (1B iff !=0xFF-led, else ff+u32LE), footer modes
 * 0x09 (5B len) or 0x49 (1B len) / tok 1 / len (lelen) / lit ds /
 * dist 0. Returns block end offset, or 0 on mismatch (detail set).
 */
static size_t check_l0_block(const uint8_t *enc, size_t eret, size_t off,
			     uint32_t want_ds, uint32_t want_extra,
			     uint8_t fill, char *detail, size_t dcap)
{
	uint32_t ds, le;
	uint16_t bo, fo, want_bo, want_modes;
	size_t ln, lelen, foff;

	if (off + 12 > eret) {
		snprintf(detail, dcap, "block @%lu truncated",
			 (unsigned long)off);
		return 0;
	}
	if (enc[off] != 0x01) {
		snprintf(detail, dcap, "block @%lu tag=%02x want 01",
			 (unsigned long)off, enc[off]);
		return 0;
	}
	ds = u32le(enc + off + 1);
	bo = u16le(enc + off + 5);
	fo = u16le(enc + off + 7);
	want_bo = (uint16_t)(enc[off + 11] != 0xFF ? 12 : 16);
	if (ds != want_ds || bo != want_bo || fo != want_bo) {
		snprintf(detail, dcap,
			 "block @%lu ds=%lu bo=%u fo=%u, want %lu/%u/%u",
			 (unsigned long)off, (unsigned long)ds, bo, fo,
			 (unsigned long)want_ds, want_bo, want_bo);
		return 0;
	}
	if (enc[off + 9] != fill || enc[off + 10] != 0xC0) {
		snprintf(detail, dcap, "block @%lu lit/tok moved",
			 (unsigned long)off);
		return 0;
	}
	ln = off + 11;
	if (enc[ln] != 0xFF) {
		lelen = 1;
		le = enc[ln];
	} else {
		lelen = 5;
		if (ln + 5 > eret) {
			snprintf(detail, dcap,
				 "block @%lu len escape truncated",
				 (unsigned long)off);
			return 0;
		}
		le = u32le(enc + ln + 1);
	}
	if (le != want_extra) {
		snprintf(detail, dcap, "block @%lu extra=%lu want %lu",
			 (unsigned long)off, (unsigned long)le,
			 (unsigned long)want_extra);
		return 0;
	}
	foff = ln + lelen;
	if (foff + 10 > eret) {
		snprintf(detail, dcap, "block @%lu footer truncated",
			 (unsigned long)off);
		return 0;
	}
	want_modes = (uint16_t)(lelen == 1 ? 0x49 : 0x09);
	if (u16le(enc + foff) != want_modes ||
	    u16le(enc + foff + 2) != 1 ||
	    u16le(enc + foff + 4) != (uint16_t)lelen ||
	    u16le(enc + foff + 6) != (uint16_t)want_ds ||
	    u16le(enc + foff + 8) != 0) {
		snprintf(detail, dcap, "block @%lu footer form moved",
			 (unsigned long)off);
		return 0;
	}
	return foff + 10;
}

/*
 * Verify one RAW tiny-tail block at off: tag 00 + u32 == tail +
 * tail fill bytes, no footer (S2.2). Returns block end offset, or 0
 * on mismatch (detail set).
 */
static size_t check_raw_tail(const uint8_t *enc, size_t eret, size_t off,
			     uint32_t tail, uint8_t fill, char *detail,
			     size_t dcap)
{
	size_t i;

	if (off + 5 + tail > eret) {
		snprintf(detail, dcap, "RAW tail @%lu truncated",
			 (unsigned long)off);
		return 0;
	}
	if (enc[off] != 0x00 || u32le(enc + off + 1) != tail) {
		snprintf(detail, dcap, "RAW tail @%lu tag/len moved",
			 (unsigned long)off);
		return 0;
	}
	for (i = 0; i < tail; i++) {
		if (enc[off + 5 + i] != fill) {
			snprintf(detail, dcap,
				 "RAW tail @%lu byte%lu moved",
				 (unsigned long)off, (unsigned long)i);
			return 0;
		}
	}
	return off + 5 + tail;
}

/*
 * E2a shape pin: ds splits + extras + END + sizer + byte-exact
 * decode + determinism. extras[i] = ds-4 (first) / ds-3 (later);
 * when last_raw != 0 the final block is a RAW tiny-tail (no extra).
 */
static void e2a_tail_one(size_t n, uint8_t fill, const uint32_t *ds,
			 const uint32_t *ex, size_t nb, size_t want_len,
			 int last_raw)
{
	char name[96], detail[192];
	uint8_t *src = malloc(n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, sizer, dret, i, off;

	snprintf(name, sizeof(name), "m24-e2a-tail L0 n=%lu%s",
		 (unsigned long)n, fill ? "-41" : "");
	if (!src || !enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	memset(src, fill, n);
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, 0xE00);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, 0xE00);
	if (eret != want_len || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x, want %lu/01",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)want_len);
		t_fail(name, detail);
		goto out;
	}
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail(name, "nondeterministic");
		goto out;
	}
	off = 0;
	for (i = 0; i < nb; i++) {
		if (last_raw && i + 1 == nb)
			off = check_raw_tail(enc, eret, off, ds[i], fill,
					     detail, sizeof(detail));
		else
			off = check_l0_block(enc, eret, off, ds[i], ex[i],
					     fill, detail, sizeof(detail));
		if (off == 0) {
			t_fail(name, detail);
			goto out;
		}
	}
	if (off + 1 != eret || enc[off] != 0xFF) {
		snprintf(detail, sizeof(detail),
			 "END not exact (off=%lu len=%lu)", (unsigned long)off,
			 (unsigned long)eret);
		t_fail(name, detail);
		goto out;
	}
	sizer = lzmesh_decoded_size(enc, eret);
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (sizer != n || dret != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)sizer, (unsigned long)dret);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(src);
	free(enc);
	free(enc2);
	free(dec);
}

int main(void)
{
	static const uint32_t S16393[] = { 16393 };
	static const uint32_t E16393[] = { 16389 };
	static const uint32_t S16394[] = { 16385, 9 };
	static const uint32_t E16394[] = { 16381, 0 };
	static const uint32_t S16395[] = { 16385, 10 };
	static const uint32_t E16395[] = { 16381, 0 };
	static const uint32_t S16396[] = { 16385, 11 };
	static const uint32_t E16396[] = { 16381, 0 };
	static const uint32_t S16397[] = { 16385, 12 };
	static const uint32_t E16397[] = { 16381, 0 };
	static const uint32_t S16398[] = { 16385, 13 };
	static const uint32_t E16398[] = { 16381, 10 };
	static const uint32_t S22027[] = { 16385, 5642 };
	static const uint32_t E22027[] = { 16381, 5639 };
	static const uint32_t S49161[] = { 16385, 32776 };
	static const uint32_t E49161[] = { 16381, 32773 };
	static const uint32_t S49162[] = { 16385, 32768, 9 };
	static const uint32_t E49162[] = { 16381, 32765, 0 };
	static const uint32_t S49163[] = { 16385, 32768, 10 };
	static const uint32_t E49163[] = { 16381, 32765, 0 };
	static const uint32_t S49164[] = { 16385, 32768, 11 };
	static const uint32_t E49164[] = { 16381, 32765, 0 };
	static const uint32_t S49165[] = { 16385, 32768, 12 };
	static const uint32_t E49165[] = { 16381, 32765, 0 };
	static const uint32_t S49166[] = { 16385, 32768, 13 };
	static const uint32_t E49166[] = { 16381, 32765, 10 };
	static const uint32_t S62881[] = { 16385, 32768, 13728 };
	static const uint32_t E62881[] = { 16381, 32765, 13725 };

	/* TEST2 absorb ceiling: tail 8 stays single-27B. */
	e2a_tail_one(16393, 0, S16393, E16393, 1, 27, 0);

	/* 2-block sliver window: RAW tiny-tail splits (THE u22 fix). */
	e2a_tail_one(16394, 0, S16394, E16394, 2, 41, 1);
	e2a_tail_one(16395, 0, S16395, E16395, 2, 42, 1);
	e2a_tail_one(16396, 0, S16396, E16396, 2, 43, 1);
	e2a_tail_one(16397, 0, S16397, E16397, 2, 44, 1);
	e2a_tail_one(16394, 0x41, S16394, E16394, 2, 41, 1);
	e2a_tail_one(16397, 0x41, S16397, E16397, 2, 44, 1);

	/* COMP-tail onset: ds13 tail is COMP (1B extra 10). */
	e2a_tail_one(16398, 0, S16398, E16398, 2, 49, 0);

	/* Second-window absorb ceiling: r2=8 stays 2-block 53B. */
	e2a_tail_one(49161, 0, S49161, E49161, 2, 53, 0);

	/* 3-block sliver window: RAW tiny-tail splits. */
	e2a_tail_one(49162, 0, S49162, E49162, 3, 67, 1);
	e2a_tail_one(49163, 0, S49163, E49163, 3, 68, 1);
	e2a_tail_one(49164, 0, S49164, E49164, 3, 69, 1);
	e2a_tail_one(49165, 0, S49165, E49165, 3, 70, 1);
	e2a_tail_one(49162, 0x41, S49162, E49162, 3, 67, 1);
	e2a_tail_one(49165, 0x41, S49165, E49165, 3, 70, 1);

	/* COMP-tail onset + byte-freeze pins (o53/o79 unchanged). */
	e2a_tail_one(49166, 0, S49166, E49166, 3, 75, 0);
	e2a_tail_one(22027, 0, S22027, E22027, 2, 53, 0);
	e2a_tail_one(62881, 0, S62881, E62881, 3, 79, 0);

	printf("---\nm24-e2a-tail: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
