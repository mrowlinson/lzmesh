/* SPDX-License-Identifier: 0BSD */
/*
 * test_m19_m12.c — M19 L0 multi-block pins (u16 emit + u17 later-block fix).
 *
 * SPEC-v2: S4.2 (REPEAT-lit ceiling R<=62880 exact: 62880 ACCEPT /
 * 62881 REJECT single-block; per-block ceiling, multiblock >64K OK),
 * S4.1/Q18 (first-byte pre-emit is stream-first-block-only; later
 * blocks carry extra=ds-3, first extra=ds-4), S3.10/Q5 (lit_run =
 * extra+3, total=(first?1:0)+...), S4.3/Q14 (C18 consumed==litc exact),
 * S4.4 (match trunc2), S2.3/S2.5/S2.8 (framing, footer, walk sum==n
 * END-exact), S5.3 (TIER-1 D>fo per block, TIER-2 whole-input).
 * MERGE-M19: port enc zeros-62881 e00 -> 53B 2-block COMP (u16/u17
 * even-split schedule; SUPERSEDED by u20 E2a budget-split, M22).
 * MERGE-M22 (E2a, oracle-true): port enc zeros-62880/62881/62882/
 * 65535 e00 -> 79B 3-block [16385,32768,tail] BYTE-IDENTICAL to
 * oracle (o79 IDENT; U20-B1 ds1=32768 CONFIRMED). M19 even-split
 * pins (27/53B) asserted oracle-REFUTED bytes — re-pinned here to
 * the M22 oracle-true schedule (mirrors test_m22_e2a o79 family).
 *
 * L0 block shape (26B): tag 01 + ds u32 + bo/fo u16 (=16) + lit 1B
 * (REPEAT) + tok 0xC0 + lenextra 5B (ff+u32LE) + footer 10B
 * (modes 0x09, tok 1, len 5, lit ds_i, dist 0) + single END.
 * n>65535 stays RAW (U10-CNT stands); L1/L5/L9 runs (litc=1)
 * unaffected by the ceiling (single block).
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
 * Verify one L0 block at off: 26B COMP, tok 0xC0, footer 1/5/ds/0,
 * len extra == want_extra with ff+u32LE form. Returns block end offset,
 * or 0 on mismatch (detail set).
 */
static size_t check_l0_block(const uint8_t *enc, size_t eret, size_t off,
			     uint32_t want_ds, uint32_t want_extra,
			     char *detail, size_t dcap)
{
	uint32_t ds, le;
	uint16_t bo, fo;

	if (off + 26 > eret) {
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
	if (ds != want_ds || bo != 16 || fo != 16) {
		snprintf(detail, dcap,
			 "block @%lu ds=%lu bo=%u fo=%u, want %lu/16/16",
			 (unsigned long)off, (unsigned long)ds, bo, fo,
			 (unsigned long)want_ds);
		return 0;
	}
	if (enc[off + 10] != 0xC0) {
		snprintf(detail, dcap, "block @%lu tok=%02x want c0",
			 (unsigned long)off, enc[off + 10]);
		return 0;
	}
	if (enc[off + 11] != 0xFF) {
		snprintf(detail, dcap, "block @%lu len0=%02x want ff",
			 (unsigned long)off, enc[off + 11]);
		return 0;
	}
	le = u32le(enc + off + 12);
	if (le != want_extra) {
		snprintf(detail, dcap, "block @%lu extra=%lu want %lu",
			 (unsigned long)off, (unsigned long)le,
			 (unsigned long)want_extra);
		return 0;
	}
	if (u16le(enc + off + 16) != 0x09 ||
	    u16le(enc + off + 18) != 1 ||
	    u16le(enc + off + 20) != 5 ||
	    u16le(enc + off + 22) != (uint16_t)want_ds ||
	    u16le(enc + off + 24) != 0) {
		snprintf(detail, dcap, "block @%lu footer form moved",
			 (unsigned long)off);
		return 0;
	}
	return off + 26;
}

/* L0 multi/single COMP shape pin + sizer + byte-exact decode + determinism. */
static void m12_one(size_t n, size_t want_len, const uint32_t *ds,
		    const uint32_t *ex, size_t nb)
{
	char name[96], detail[192];
	uint8_t *z = calloc(n, 1);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, sizer, dret, i, off;

	snprintf(name, sizeof(name), "m12 L0 n=%lu", (unsigned long)n);
	if (!z || !enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, 0xE00);
	eret2 = lzmesh_encode(enc2, n + 1024, z, n, NULL, 0xE00);
	if (eret != want_len) {
		snprintf(detail, sizeof(detail), "outlen=%lu want %lu",
			 (unsigned long)eret, (unsigned long)want_len);
		t_fail(name, detail);
		goto out;
	}
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail(name, "nondeterministic");
		goto out;
	}
	off = 0;
	for (i = 0; i < nb; i++) {
		off = check_l0_block(enc, eret, off, ds[i], ex[i], detail,
				     sizeof(detail));
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
	if (sizer != n || dret != n) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu want %lu",
			 (unsigned long)sizer, (unsigned long)dret,
			 (unsigned long)n);
		t_fail(name, detail);
		goto out;
	}
	for (i = 0; i < n; i++) {
		if (dec[i] != 0) {
			t_fail(name, "decoded bytes differ");
			goto out;
		}
	}
	t_pass(name);
out:
	free(z);
	free(enc);
	free(enc2);
	free(dec);
}

/* L0 RAW cap: n>65535 stays RAW (U10-CNT), outlen n+6, exact roundtrip. */
static void m12_raw_one(size_t n)
{
	char name[96], detail[192];
	uint8_t *z = calloc(n, 1);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, sizer, dret, i;

	snprintf(name, sizeof(name), "m12 L0-raw n=%lu", (unsigned long)n);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, 0xE00);
	if (eret != n + 6 || enc[0] != 0x00) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want %lu/00",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)(n + 6));
		t_fail(name, detail);
		goto out;
	}
	if (enc[eret - 1] != 0xFF || u32le(enc + 1) != (uint32_t)n) {
		t_fail(name, "RAW framing moved");
		goto out;
	}
	sizer = lzmesh_decoded_size(enc, eret);
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (sizer != n || dret != n) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)sizer, (unsigned long)dret);
		t_fail(name, detail);
		goto out;
	}
	for (i = 0; i < n; i++) {
		if (dec[i] != 0) {
			t_fail(name, "decoded bytes differ");
			goto out;
		}
	}
	t_pass(name);
out:
	free(z);
	free(enc);
	free(dec);
}

/* L1/L5/L9 run 62881: litc=1, no ceiling, single 27B COMP tok 0x07. */
static void m12_gen_one(int level)
{
	char name[96], detail[192];
	static const size_t N = 62881;
	uint8_t *z = calloc(N, 1);
	uint8_t *enc = malloc(N + 1024);
	uint8_t *dec = malloc(N + 64);
	size_t eret, sizer, dret, i;

	snprintf(name, sizeof(name), "m12 run L%x n=62881", level);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, N + 1024, z, N, NULL, level);
	if (eret != 27 || enc[0] != 0x01 || enc[10] != 0x07) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x tok=%02x, want 27/01/07",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 eret > 10 ? enc[10] : 0);
		t_fail(name, detail);
		goto out;
	}
	/* rest = 62881-10 = 62871 = 0xF597 -> ff 97 f5 00 00. */
	if (enc[11] != 0xFF || u32le(enc + 12) != 62871) {
		snprintf(detail, sizeof(detail), "len escape moved (%02x %08x)",
			 enc[11], u32le(enc + 12));
		t_fail(name, detail);
		goto out;
	}
	sizer = lzmesh_decoded_size(enc, eret);
	dret = lzmesh_decode(dec, N + 64, enc, eret, NULL);
	if (sizer != N || dret != N) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)sizer, (unsigned long)dret);
		t_fail(name, detail);
		goto out;
	}
	for (i = 0; i < N; i++) {
		if (dec[i] != 0) {
			t_fail(name, "decoded bytes differ");
			goto out;
		}
	}
	t_pass(name);
out:
	free(z);
	free(enc);
	free(dec);
}

int main(void)
{
	static const uint32_t DS80[] = { 16385, 32768, 13727 };
	static const uint32_t EX80[] = { 16381, 32765, 13724 };
	static const uint32_t DS81[] = { 16385, 32768, 13728 };
	static const uint32_t EX81[] = { 16381, 32765, 13725 };
	static const uint32_t DS82[] = { 16385, 32768, 13729 };
	static const uint32_t EX82[] = { 16381, 32765, 13726 };
	static const uint32_t DS65[] = { 16385, 32768, 16382 };
	static const uint32_t EX65[] = { 16381, 32765, 16379 };
	/* R5 FIX-E (T5 close): schedule extends past 65535 (B2 repeat);
	 * oracle-confirmed IDENT 79B before flipping. */
	static const uint32_t DS66[] = { 16385, 32768, 16383 };
	static const uint32_t EX66[] = { 16381, 32765, 16380 };
	static const uint32_t DS10[] = { 16385, 32768, 50847 };
	static const uint32_t EX10[] = { 16381, 32765, 50844 };
	static const int GEN[] = { 0xE01, 0xE05, 0xE09 };
	size_t i;

	/* M22 oracle-true o79 family (E2a budget-split, byte-identical to
	 * oracle; mirrors test_m22_e2a). First extra=ds-4, later=ds-3. */
	m12_one(62880, 79, DS80, EX80, 3);
	m12_one(62881, 79, DS81, EX81, 3);
	m12_one(62882, 79, DS82, EX82, 3);
	m12_one(65535, 79, DS65, EX65, 3);
	m12_one(65536, 79, DS66, EX66, 3);
	m12_one(100000, 79, DS10, EX10, 3);
	(void)m12_raw_one;
	for (i = 0; i < sizeof(GEN) / sizeof(GEN[0]); i++)
		m12_gen_one(GEN[i]);

	printf("---\nm19-m12: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
