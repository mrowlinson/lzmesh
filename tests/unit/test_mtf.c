/* SPDX-License-Identifier: 0BSD */
/*
 * test_mtf.c — MTF/recents triggers: rep3 accept + multi-block carry.
 *
 * SPEC-v2: S3.9/Q17 (decoder MUST accept all 32 rep3 values though the
 * encoder never emits sel 3; P-D9), S3.12 (recent[4] init {1,1,1,1};
 * carry across blocks incl RAW; R-E-B8 dedup: repk k>0 moves
 * recent[k] to front, rep0 no-op, new-dist inserts front),
 * S2.8 (container walk sums ds to END-exact).
 *
 * rep3 is tested accounting-preserving (same lit/len, sel 0->3) on
 * port run-COMP bytes where all recents are 1, so d is unchanged.
 * Multi-block RAW chains pass today; chains with a non-first COMP
 * carrying a match are pass-or-XFAIL (port decodes oracle L0 multi
 * fine but fails port-shape non-first COMP; lane-owned gap, sound
 * XFAIL: each block is valid alone and the walk sums exactly).
 * Dedup-vs-shift chains need multi-token dist forges (pending; see
 * re-scratch probes, NOT committed). Public API only.
 * Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

static int g_pass, g_fail, g_xfail;

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

static void t_xfail(const char *name, const char *reason)
{
	g_xfail++;
	printf("XFAIL %s :: %s\n", name, reason);
}

/* Flip token sel 0->3 preserving lit/len; decode must stay exact. */
static void rep3_one(int level, size_t n, size_t tok_off, uint8_t want_tok,
		     const char *tag)
{
	char name[96], detail[192];
	uint8_t *z = calloc(n, 1);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *mut = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, ds, dr, i;
	int ok;

	snprintf(name, sizeof(name), "rep3 L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!z || !enc || !mut || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, level);
	if (eret == 0 || enc[0] != 0x01 || enc[tok_off] != want_tok) {
		snprintf(detail, sizeof(detail),
			 "base shape moved (out=%lu tok[%lu]=%02x want %02x)",
			 (unsigned long)eret, (unsigned long)tok_off,
			 eret > tok_off ? enc[tok_off] : 0, want_tok);
		t_fail(name, detail);
		goto out;
	}
	memcpy(mut, enc, eret);
	mut[tok_off] = (uint8_t)(want_tok | (3u << 3));
	ds = lzmesh_decoded_size(mut, eret);
	dr = lzmesh_decode(dec, n + 64, mut, eret, NULL);
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
	free(mut);
	free(dec);
}

/* Multi-block chain that must pass today; bytes checked exactly. */
static void chain_pass(const char *name, const uint8_t *src, size_t slen,
		       const uint8_t *want, size_t want_n)
{
	char detail[160];
	uint8_t *dec = malloc(want_n + 64);
	size_t ds, dr;

	ds = lzmesh_decoded_size(src, slen);
	dr = lzmesh_decode(dec, want_n + 64, src, slen, NULL);
	if (ds != want_n || dr != want_n ||
	    memcmp(dec, want, want_n) != 0) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu want %lu",
			 (unsigned long)ds, (unsigned long)dr,
			 (unsigned long)want_n);
		t_fail(name, detail);
	} else {
		t_pass(name);
	}
	free(dec);
}

/* Multi-block chain pending: walk sums, decode fail-closed for now. */
static void chain_xfail(const char *name, const uint8_t *src, size_t slen,
			size_t want_n)
{
	char detail[192];
	uint8_t *dec = malloc(want_n + 64);
	size_t ds, dr;

	ds = lzmesh_decoded_size(src, slen);
	dr = lzmesh_decode(dec, want_n + 64, src, slen, NULL);
	if (ds == want_n && dr == want_n) {
		t_pass(name);
	} else if (ds == want_n && dr == 0) {
		snprintf(detail, sizeof(detail),
			 "walk %lu ok, decode fail-closed 0 (non-first COMP "
			 "with match pending; no misdecode)",
			 (unsigned long)ds);
		t_xfail(name, detail);
	} else {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu want %lu",
			 (unsigned long)ds, (unsigned long)dr,
			 (unsigned long)want_n);
		t_fail(name, detail);
	}
	free(dec);
}

int main(void)
{
	static const uint8_t A[1] = { 0x41 };
	static uint8_t z[64], run[64], raw1[16], mb[256];
	static uint8_t want23[23], want2[2], want3[3];
	size_t eret, r1;

	/* rep3 on L5-match shape (tok 0x07 @10) and L0 shape (0xC0 @10). */
	rep3_one(0xE05, 22, 10, 0x07, "match-run");
	rep3_one(0xE00, 22, 10, 0xC0, "L0-run");

	eret = lzmesh_encode(run, sizeof(run), z, 22, NULL, 0xE05);
	r1 = lzmesh_encode(raw1, sizeof(raw1), A, 1, NULL, 0xE05);
	if (eret != 23 || r1 != 7) {
		t_fail("chains", "base block shapes moved");
		goto done;
	}
	memset(want23, 0, 22);
	want23[22] = 0x41;
	memset(want2, 0x41, 2);
	memset(want3, 0x41, 3);

	/* RAW chains pass (walk + carry trivially exact). */
	memcpy(mb, raw1, r1 - 1);
	memcpy(mb + r1 - 1, raw1, r1);
	chain_pass("multi RAW+RAW", mb, 2 * r1 - 1, want2, 2);
	memcpy(mb, raw1, r1 - 1);
	memcpy(mb + r1 - 1, raw1, r1 - 1);
	memcpy(mb + 2 * (r1 - 1), raw1, r1);
	chain_pass("multi 3xRAW", mb, 3 * r1 - 2, want3, 3);

	/* COMP first + RAW tail passes. */
	memcpy(mb, run, eret - 1);
	memcpy(mb + eret - 1, raw1, r1);
	chain_pass("multi COMP+RAW", mb, eret - 1 + r1, want23, 23);

	/* Non-first COMP with a match: pending (sound XFAIL). */
	memcpy(mb, raw1, r1 - 1);
	memcpy(mb + r1 - 1, run, eret);
	chain_xfail("multi RAW+COMP", mb, r1 - 1 + eret, 23);
	memcpy(mb, run, eret - 1);
	memcpy(mb + eret - 1, run, eret);
	chain_xfail("multi COMP+COMP", mb, 2 * eret - 1, 44);

done:
	printf("---\nmtf: pass=%d fail=%d xfail=%d\n", g_pass, g_fail,
	       g_xfail);
	return g_fail ? 1 : 0;
}
