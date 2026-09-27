/* SPDX-License-Identifier: 0BSD */
/*
 * test_m30_shaper12cd_veto.c — M30 SHAPE-R12CD veto pins (u27,
 * GAPLOG-u27 Task 3 veto pins, all fail-safe RAW n+6 + exact
 * roundtrip + determinism).
 *
 * SPEC-v2: S5.3 (TIER-1 D>fo strict; TIER-2 fo+10<=n), S5.4 (9B
 * look-ahead gate, Q30; U27-REM9 rlast>=10 native, last-only exact),
 * S5.5 (mode_trivial lit-HUF->fail-safe-0; no Huffman emission in
 * this shape family), S5.9.a (one parse, exact end, no terminator
 * here — trailing shorts are u28-owned), S4.3 (exact end), S5.1
 * (determinism), S1.3/Q20 (full-form levels).
 * SHAPE-R12CD (u27): k-run rep-chain with >=1 wide gap (lead L>=3
 * or interior G>=3) at 1/5/9. Vetoes here (GAPLOG-u27 Task 3, seam
 * RAW n+6 x3 + e00 observed lane-side on the full lane file):
 * TIER [X1,Y1,Z1,A12] n16 (layout VALID, TIER-1 D15>fo16 fails);
 * lit-HUF lead-12 + A30 n42 (litc 13 -> HUF attempt -> veto);
 * REM9 [A20,X1,Y1,Z1,B8] n31 rlast 8 (TIER would keep: bo18,
 * 28<=31) + LR-9 [X1,Y1,Z1,A10,B10,C9] n32 rlast 9 (TIER would
 * keep: bo19, 29<=32) vs LR rlast 10 keeps (pinned COMP in
 * test_m30_shaper12cd.c — knife-edge 9/10 exact);
 * trailing short [A12,X1,Y1,Z1] n15 + trailing r2
 * [X1,Y1,Z1,A30,B2] n38 (exact end, terminator is u28's).
 * Each veto pinned at all 3 match levels (mechanisms are
 * level-independent; verified RAW at e01/e05/e09 via public API).
 * L0 excluded (no finding, same RAW by construction, pinned for
 * flips in test_m30_shaper12cd.c).
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

static const int MLEVELS[] = { 0xE01, 0xE05, 0xE09 };

static uint8_t *make_runs(const size_t *rs, const uint8_t *bs, size_t k,
			   size_t *np)
{
	size_t n = 0, i, j, p = 0;
	uint8_t *buf;

	for (i = 0; i < k; i++)
		n += rs[i];
	buf = malloc(n ? n : 1);
	if (!buf)
		return NULL;
	for (i = 0; i < k; i++)
		for (j = 0; j < rs[i]; j++)
			buf[p++] = bs[i];
	*np = n;
	return buf;
}

/* RAW hold pin: tag 00 + outlen n+6 + exact roundtrip + determinism. */
static void raw_one(int level, const size_t *rs, const uint8_t *bs, size_t k,
		    const char *why)
{
	char name[128], detail[192];
	size_t n = 0;
	uint8_t *src = make_runs(rs, bs, k, &n);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, ds, dr;

	snprintf(name, sizeof(name), "shaper12cd-veto L%x %s n=%lu", level,
		 why, (unsigned long)n);
	if (!src || !enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret != n + 6 || enc[0] != 0x00) {
		snprintf(detail, sizeof(detail), "outlen=%lu tag=%02x, want %lu/00",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)(n + 6));
		t_fail(name, detail);
		goto out;
	}
	if (eret2 != eret || memcmp(enc, enc2, eret) != 0) {
		t_fail(name, "nondeterministic");
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (ds != n || dr != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)ds, (unsigned long)dr);
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
	static const size_t V_TIER[] = { 1, 1, 1, 12 };
	static const size_t V_HUF[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 30 };
	static const size_t V_REM8[] = { 20, 1, 1, 1, 8 };
	static const size_t V_REM9[] = { 1, 1, 1, 10, 10, 9 };
	static const size_t V_TRAIL[] = { 12, 1, 1, 1 };
	static const size_t V_TRAIL2[] = { 1, 1, 1, 30, 2 };
	static const uint8_t B_XYZA[] = { 0x58, 0x59, 0x5a, 0x41 };
	static const uint8_t B_HUF[] = { 0x42, 0x43, 0x44, 0x45, 0x46,
					0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
					0x4d, 0x41 };
	static const uint8_t B_AXYZB[] = { 0x41, 0x58, 0x59, 0x5a, 0x42 };
	static const uint8_t B_XYZABC[] = { 0x58, 0x59, 0x5a, 0x41, 0x42,
					   0x43 };
	static const uint8_t B_AXYZ[] = { 0x41, 0x58, 0x59, 0x5a };
	static const uint8_t B_XYZAB[] = { 0x58, 0x59, 0x5a, 0x41, 0x42 };
	size_t li;

	for (li = 0; li < 3; li++) {
		raw_one(MLEVELS[li], V_TIER, B_XYZA, 4, "tier1-D15-fo16");
		raw_one(MLEVELS[li], V_HUF, B_HUF, 13, "lit-huf-litc13");
		raw_one(MLEVELS[li], V_REM8, B_AXYZB, 5, "rem9-rlast8");
		raw_one(MLEVELS[li], V_REM9, B_XYZABC, 6, "rem9-rlast9");
		raw_one(MLEVELS[li], V_TRAIL, B_AXYZ, 4, "trailing-short");
		raw_one(MLEVELS[li], V_TRAIL2, B_XYZAB, 5, "trailing-r2");
	}

	printf("---\nm30-shaper12cd-veto: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
