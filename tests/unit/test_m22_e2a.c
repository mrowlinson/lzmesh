/* SPDX-License-Identifier: 0BSD */
/*
 * test_m22_e2a.c — M22 E2a budget-split pins (u20, MERGE-M22 T6).
 *
 * SPEC-v2: S5.4 (budgets B0 0x4000 -> B1 0x8000 -> B2+ 0xF3F0
 * sticky; overshoot +0..+6 one token; px0 16385-16390, px1
 * 32768-32774; TEST2 absorb iff (pos/stop)+9>E, tail<=8 absorbs,
 * tail>=9 splits), S5.3 (TIER-1 D>fo per block, TIER-2 whole),
 * S4.2 (R<=62880 per block), S2.8 (walk sum==n END-exact),
 * S4.1/Q18 (first-only pre-emit: first extra=ds-4, later extra=
 * ds-3, u17 frozen), S3.10/Q5/Q14 (C18 exact, lit_run=extra+3),
 * S4.4 (trunc2), S5.1 (determinism).
 * E2a schedule (u20, L0 runs, n<=65535): exact-fit min overshoot
 * every block (byte-divisible): ds0=16385 (px0 min), ds1=32768
 * (px1 min, U20-B1 pin), stops [16385, 49153]; TEST2-only absorb
 * (tail<=8) + RAW tiny-tail (u22 M24; U20-TAIL13 absorb SUPERSEDED,
 * COMP-R7 falsified): n<=16393 single, 16394-16397 2-block
 * [16385,9..12] COMP+RAW, 16398+ 2-block, r2<=8 absorb to 49161,
 * 49162-49165 3-block [16385,32768,9..12] COMP+COMP+RAW, else
 * 3-block [16385,32768,r2] COMP tail (n>=49166).
 * Merge-observed oracle byte-identity (M22 T6 + M24 T6): o53 EXACT
 * (22027 [16385,5642]) + o79 IDENT at 62880/62881/65535 +
 * boundaries 16393/16398/49161/49166 ident + slivers 16394-16397
 * 41/42/43/44B + 49162-49165 67/68/69/70B ident (M24);
 * U20-B1 CONFIRMED (oracle ds1=32768). L1/L5/L9 runs never split
 * (R4). n>65535 stays RAW (U10-CNT, pinned by m19-m12).
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
 * Verify one L0 block at off: tag 01, ds, bo==fo==16 (5B extra)
 * or 12 (1B extra), lit fill,
 * tok 0xC0, len extra want_extra (1B iff !=0xFF-led, else ff+u32LE),
 * footer modes 0x09 (5B len) or 0x49 (1B len) / tok 1 /
 * len (lelen) / lit ds / dist 0.
 * Returns block end offset, or 0 on mismatch (detail set).
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
 * tail zero bytes, no footer (S2.2). Returns block end offset, or 0
 * on mismatch (detail set).
 */
static size_t check_raw_tail(const uint8_t *enc, size_t eret, size_t off,
			     uint32_t tail, char *detail, size_t dcap)
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
		if (enc[off + 5 + i] != 0) {
			snprintf(detail, dcap,
				 "RAW tail @%lu byte%lu moved",
				 (unsigned long)off, (unsigned long)i);
			return 0;
		}
	}
	return off + 5 + tail;
}

/*
 * Sliver pin (u22 M24): like e2a_one but the final block is a RAW
 * tiny-tail (S2.9 split, tail 9..12). extras[i] = ds-4 (first) /
 * ds-3 (later) for the COMP prefix; the tail entry of ex[] is
 * unused.
 */
static void e2a_sliver_one(size_t n, const uint32_t *ds,
			   const uint32_t *ex, size_t nb, size_t want_len)
{
	char name[96], detail[192];
	uint8_t *src = malloc(n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, sizer, dret, i, off;

	snprintf(name, sizeof(name), "e2a L0 n=%lu", (unsigned long)n);
	if (!src || !enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	memset(src, 0, n);
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
		if (i + 1 == nb)
			off = check_raw_tail(enc, eret, off, ds[i],
					     detail, sizeof(detail));
		else
			off = check_l0_block(enc, eret, off, ds[i], ex[i],
					     0, detail, sizeof(detail));
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

/* Budget multi/single pin: ds splits + extras + END + sizer +
 * byte-exact decode + determinism. extras[i] = ds-4 (first) /
 * ds-3 (later). */
static void e2a_one(size_t n, uint8_t fill, const uint32_t *ds,
		    const uint32_t *ex, size_t nb, size_t want_len)
{
	char name[96], detail[192];
	uint8_t *src = malloc(n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, sizer, dret, i, off;

	snprintf(name, sizeof(name), "e2a L0 n=%lu%s", (unsigned long)n,
		 fill ? "-41" : "");
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
		off = check_l0_block(enc, eret, off, ds[i], ex[i], fill,
				     detail, sizeof(detail));
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

/* L1/L5/L9 runs never split: single 27B COMP tok 0x07 + exact legs. */
static void gen_one(int level, size_t n)
{
	char name[96], detail[192];
	uint8_t *z = calloc(n, 1);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, sizer, dret;

	snprintf(name, sizeof(name), "e2a-nosplit L%x n=%lu", level,
		 (unsigned long)n);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, level);
	if (eret != 27 || enc[0] != 0x01 || enc[10] != 0x07) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x tok=%02x, want 27/01/07",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 eret > 10 ? enc[10] : 0);
		t_fail(name, detail);
		goto out;
	}
	sizer = lzmesh_decoded_size(enc, eret);
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (sizer != n || dret != n || memcmp(dec, z, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)sizer, (unsigned long)dret);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(z);
	free(enc);
	free(dec);
}

int main(void)
{
	static const uint32_t S16384[] = { 16384 };
	static const uint32_t E16384[] = { 16380 };
	static const uint32_t S16385[] = { 16385 };
	static const uint32_t E16385[] = { 16381 };
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
	static const uint32_t S16399[] = { 16385, 14 };
	static const uint32_t E16399[] = { 16381, 11 };
	static const uint32_t S22027[] = { 16385, 5642 };
	static const uint32_t E22027[] = { 16381, 5639 };
	static const uint32_t S32768[] = { 16385, 16383 };
	static const uint32_t E32768[] = { 16381, 16380 };
	static const uint32_t S49153[] = { 16385, 32768 };
	static const uint32_t E49153[] = { 16381, 32765 };
	static const uint32_t S49154[] = { 16385, 32769 };
	static const uint32_t E49154[] = { 16381, 32766 };
	static const uint32_t S49165[] = { 16385, 32768, 12 };
	static const uint32_t E49165[] = { 16381, 32765, 0 };
	static const uint32_t S49166[] = { 16385, 32768, 13 };
	static const uint32_t E49166[] = { 16381, 32765, 10 };
	static const uint32_t S49167[] = { 16385, 32768, 14 };
	static const uint32_t E49167[] = { 16381, 32765, 11 };
	static const uint32_t S62880[] = { 16385, 32768, 13727 };
	static const uint32_t E62880[] = { 16381, 32765, 13724 };
	static const uint32_t S62881[] = { 16385, 32768, 13728 };
	static const uint32_t E62881[] = { 16381, 32765, 13725 };
	static const uint32_t S62882[] = { 16385, 32768, 13729 };
	static const uint32_t E62882[] = { 16381, 32765, 13726 };
	static const uint32_t S65535[] = { 16385, 32768, 16382 };
	static const uint32_t E65535[] = { 16381, 32765, 16379 };
	static const int GEN[] = { 0xE01, 0xE05, 0xE09 };
	size_t i;

	/* Singles: TEST2 absorb through 16393. */
	e2a_one(16384, 0, S16384, E16384, 1, 27);
	e2a_one(16385, 0, S16385, E16385, 1, 27);
	e2a_one(16393, 0, S16393, E16393, 1, 27);

	/* Slivers (u22 M24): RAW tiny-tail splits, R7-oracle sizes. */
	e2a_sliver_one(16394, S16394, E16394, 2, 41);
	e2a_sliver_one(16395, S16395, E16395, 2, 42);
	e2a_sliver_one(16396, S16396, E16396, 2, 43);
	e2a_sliver_one(16397, S16397, E16397, 2, 44);

	/* 2-block onset + o53. */
	e2a_one(16398, 0, S16398, E16398, 2, 49);
	e2a_one(16399, 0, S16399, E16399, 2, 49);
	e2a_one(22027, 0, S22027, E22027, 2, 53);
	e2a_one(22027, 0x41, S22027, E22027, 2, 53);
	e2a_one(32768, 0, S32768, E32768, 2, 53);
	e2a_one(49153, 0, S49153, E49153, 2, 53);
	e2a_one(49154, 0, S49154, E49154, 2, 53);

	/* Second-window sliver (u22 M24): 3-block RAW-tail split. */
	e2a_sliver_one(49165, S49165, E49165, 3, 70);

	/* 3-block onset + o79 family. */
	e2a_one(49166, 0, S49166, E49166, 3, 75);
	e2a_one(49167, 0, S49167, E49167, 3, 75);
	e2a_one(62880, 0, S62880, E62880, 3, 79);
	e2a_one(62881, 0, S62881, E62881, 3, 79);
	e2a_one(62882, 0, S62882, E62882, 3, 79);
	e2a_one(65535, 0, S65535, E65535, 3, 79);

	/* L1/L5/L9 runs: no split at E2a sizes. */
	for (i = 0; i < 3; i++) {
		gen_one(GEN[i], 22027);
		gen_one(GEN[i], 49166);
	}

	printf("---\nm22-e2a: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
