/* SPDX-License-Identifier: 0BSD */
/*
 * test_m31_q1_skip.c — M31 Q1 fail-open no-regress pins (u6v Q1 gate
 * shape-narrows to Huffman + dc==0 + 51/59/75B COMP; everything else
 * MUST skip untouched, GAPLOG-u6v FLUSH4; u6v REFUSED at M31 T5 on
 * the B1 later-block misfire — Q1 itself unrefuted, rework pending;
 * these pins lock valids on the reverted tree and guard the rework).
 *
 * SPEC-v2: S1.7 (enc1/RAW shapes), S2.8 (container walk sums ds to
 * END-exact), S4.1 (roundtrip exact), S5.1 (determinism), S5.9.a
 * (one parse), S1.3/Q20 (full-form levels). P1-ANSWERS Q1 SAFE:
 * shape-gated N-check + 51B-gated L0 rules SKIP every other shape
 * (p100 N=7 stays valid).
 *
 * Method: run-free aperiodic (sequential-byte) inputs at n=45/53/69
 * MUST stay RAW n+6 (outputs exactly 51/59/75B, tag 00, no Huffman
 * lanes anywhere) + exact roundtrip at all 4 levels — SIZE-matching
 * but gate-invisible shapes proving the fail-open. Pure-run COMP
 * controls (no Huffman by construction) + a 51B-total COMP+RAW
 * chain (COMP-first multi-block decodes, mtf-pattern) close the
 * no-regress net. All shapes are SHAPE-R-TRAIL-orthogonal (no runs,
 * no rep-chain), so pins hold with or without the M32 u29 enc.
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

static const int LEVELS[] = { 0xE00, 0xE01, 0xE05, 0xE09 };

/* Sequential input MUST stay RAW n+6 + exact roundtrip (gate SKIPs). */
static void raw_skip_one(int level, size_t n)
{
	char name[96], detail[192];
	uint8_t *src = malloc(n ? n : 1);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr, i;
	int ok;

	snprintf(name, sizeof(name), "skip L%x seq RAW n=%lu", level,
		 (unsigned long)n);
	if (!src || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	for (i = 0; i < n; i++)
		src[i] = (uint8_t)i;
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (eret != n + 6 || enc[0] != 0x00) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x, want %lu/00 (must stay RAW)",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)(n + 6));
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	ok = (ds == n && dr == n);
	for (i = 0; i < n && ok; i++)
		if (dec[i] != (uint8_t)i)
			ok = 0;
	if (!ok) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(src);
	free(enc);
	free(dec);
}

/* Pure-run COMP control: COMP tag + exact roundtrip (no Huffman). */
static void run_skip_one(int level, size_t n)
{
	char name[96], detail[192];
	uint8_t *z = calloc(n ? n : 1, 1);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr, i;
	int ok;

	snprintf(name, sizeof(name), "skip L%x run COMP n=%lu", level,
		 (unsigned long)n);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, level);
	if (eret == 0 || enc[0] != 0x01) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x, want COMP",
			 (unsigned long)eret, eret ? enc[0] : 0);
		t_fail(name, detail);
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	dr = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	ok = (ds == n && dr == n);
	for (i = 0; i < n && ok; i++)
		if (dec[i] != 0)
			ok = 0;
	if (!ok) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu",
			 (unsigned long)ds, (unsigned long)dr);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(z);
	free(enc);
	free(dec);
}

/* 51B-total COMP+RAW chain (run22-COMP 23B + seq23-RAW 29B). */
static void chain51_one(void)
{
	char detail[192];
	static uint8_t z[22], seq[23], run[64], raw[64], mb[128], want[45];
	size_t eret, rlen, i;
	size_t ds, dr;

	for (i = 0; i < sizeof(seq); i++)
		seq[i] = (uint8_t)i;
	eret = lzmesh_encode(run, sizeof(run), z, sizeof(z), NULL, 0xE05);
	rlen = lzmesh_encode(raw, sizeof(raw), seq, sizeof(seq), NULL,
			     0xE05);
	if (eret != 23 || run[0] != 0x01 || rlen != 29 ||
	    raw[0] != 0x00) {
		snprintf(detail, sizeof(detail),
			 "base shapes moved (run %lu/%02x, raw %lu/%02x)",
			 (unsigned long)eret, eret ? run[0] : 0,
			 (unsigned long)rlen, rlen ? raw[0] : 0);
		t_fail("skip chain51 COMP+RAW", detail);
		return;
	}
	memcpy(mb, run, eret - 1);
	memcpy(mb + eret - 1, raw, rlen);
	memset(want, 0, sizeof(z));
	memcpy(want + sizeof(z), seq, sizeof(seq));
	ds = lzmesh_decoded_size(mb, eret - 1 + rlen);
	{
		static uint8_t dec[128];

		dr = lzmesh_decode(dec, sizeof(dec), mb, eret - 1 + rlen,
				   NULL);
		if (eret - 1 + rlen != 51 || ds != 45 || dr != 45 ||
		    memcmp(dec, want, 45) != 0) {
			snprintf(detail, sizeof(detail),
				 "shapelen=%lu sizer=%lu dec=%lu",
				 (unsigned long)(eret - 1 + rlen),
				 (unsigned long)ds, (unsigned long)dr);
			t_fail("skip chain51 COMP+RAW", detail);
			return;
		}
	}
	t_pass("skip chain51 COMP+RAW");
}

int main(void)
{
	static const size_t NS[] = { 45, 53, 69 };
	size_t li, ni;

	for (li = 0; li < sizeof(LEVELS) / sizeof(LEVELS[0]); li++) {
		for (ni = 0; ni < sizeof(NS) / sizeof(NS[0]); ni++)
			raw_skip_one(LEVELS[li], NS[ni]);
		run_skip_one(LEVELS[li], 100);
	}
	chain51_one();

	printf("---\nm31-q1-skip: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
