/* SPDX-License-Identifier: 0BSD */
/*
 * test_m32_slots_fixa.c — M32 encoder pins: R3 FIX-A (u12/u18 slots) + PMAX64.
 *
 * SPEC-v12: S5.7/S5.8 (finder heads 7B/5B/3B, hash consts C1/C2/C3,
 * take codes), S5.3 (TIER-1 D>fo, TIER-2 outpos<=n, no size compare),
 * S2.1/S2.3/S2.5 (COMP tag/header/footer), S3.9/S3.10/S3.11 (token_new,
 * esc31, dist sb/low/suf), S3.3 (index-at-END), S5.6 (PAD1), S5.1
 * (determinism), S1.3/Q20 (full-form levels).
 * FIX-A (ENC-BATT-R3): a slots aliasing insert (i>=1) whose head
 * shares <3 bytes with the query head can only MISS (head match is
 * correctness-required; extension-from-0 dies below fresh floor 3),
 * so it no longer vetoes; soundness needs >=1 alias-free live query
 * per hb whose slot's sole writer is the same-head i=0 insert.
 * PMAX64 (ENC-BATT-R3): u18 SHAPE-P-SUF p range 9..56 -> 9..64 (sb3
 * d57..64; lane0 1B + PAD1 rule verified byte-identical on 135
 * corpus p64 cells x e01/e05/e09; U18-PMAX retired).
 * Pins are black-box oracle bytes (battery corpus cells, see
 * ENC-BATT-R3 report): port must emit them byte-exact. L1 no-hit
 * (s63 @E01) and L0 non-run (s01n43 @E00) stay RAW: committed as
 * XFAIL pending spec-lane amendments R3-A1 (L1 probe model) and
 * R3-A2 (L0 litonly-HUF coverage).
 * Public API only. Exit 0 iff zero FAILs.
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

/* s64-n26-period: p4 unit ca d4 f9 05 x6 + ca d4 (u12 FIX-A min repro). */
static const uint8_t IN_P4N26[] = {
	0xca, 0xd4, 0xf9, 0x05, 0xca, 0xd4, 0xf9, 0x05, 0xca, 0xd4,
	0xf9, 0x05, 0xca, 0xd4, 0xf9, 0x05, 0xca, 0xd4, 0xf9, 0x05,
	0xca, 0xd4, 0xf9, 0x05, 0xca, 0xd4
};

/* Oracle bytes @E05/E09 for IN_P4N26 (27B COMP, bo=fo=16, 0x248/1/1/4/1). */
static const uint8_t OUT_P4N26[] = {
	0x01, 0x1a, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0xca,
	0xd4, 0xf9, 0x05, 0xf4, 0x00, 0x03, 0x48, 0x02, 0x01, 0x00,
	0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0xff
};

/* s63-n45-period: p16 unit x2 + first 13 (u18 FIX-A cell). */
static const uint8_t IN_P16N45[] = {
	0x8e, 0xfd, 0xd4, 0x73, 0x3a, 0xce, 0x3e, 0xec, 0xa5, 0x10,
	0x28, 0xec, 0xbc, 0x83, 0x8d, 0x4b, 0x8e, 0xfd, 0xd4, 0x73,
	0x3a, 0xce, 0x3e, 0xec, 0xa5, 0x10, 0x28, 0xec, 0xbc, 0x83,
	0x8d, 0x4b, 0x8e, 0xfd, 0xd4, 0x73, 0x3a, 0xce, 0x3e, 0xec,
	0xa5, 0x10, 0x28, 0xec, 0xbc
};

/* Oracle bytes @E05/E09 (== @E01) for IN_P16N45 (44B, bo=28 fo=33). */
static const uint8_t OUT_P16N45[] = {
	0x01, 0x2d, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x21, 0x00, 0x8e,
	0xfd, 0xd4, 0x73, 0x3a, 0xce, 0x3e, 0xec, 0xa5, 0x10, 0x28,
	0xec, 0xbc, 0x83, 0x8d, 0x4b, 0xfb, 0x0c, 0x0f, 0x02, 0x01,
	0x00, 0x00, 0x08, 0x48, 0x02, 0x01, 0x00, 0x01, 0x00, 0x10,
	0x00, 0x01, 0x00, 0xff
};

/* s19-n100-period p64 unit (first 64 of the 100B input; tail = unit[0:36]). */
static const uint8_t UNIT_P64[] = {
	0x25, 0x3b, 0xce, 0x9b, 0x3d, 0x41, 0xd4, 0xb4, 0x7d, 0x85,
	0xe4, 0x8c, 0xa5, 0x47, 0x9e, 0x71, 0x80, 0xef, 0xdd, 0xb4,
	0x2e, 0xf4, 0xd1, 0x2e, 0xa4, 0x17, 0x0b, 0x1f, 0x3b, 0xf3,
	0xad, 0x69, 0x36, 0x5e, 0x6b, 0x48, 0x2f, 0x38, 0x25, 0x4b,
	0x25, 0x03, 0x49, 0xd5, 0x2e, 0xcc, 0x8e, 0xc2, 0x2f, 0x39,
	0x02, 0xe0, 0x99, 0x81, 0x52, 0x1e, 0x73, 0x49, 0xfb, 0x61,
	0xaa, 0x13, 0x3a, 0xd4
};

/* Oracle bytes @E01/E05/E09 for the p64 n100 input (93B, bo=77 fo=82,
 * modes 0x808 tc=1 lc=2 litc=64 dc=1; identical all three levels). */
static const uint8_t OUT_P64N100[] = {
	0x01, 0x64, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x52, 0x00, 0x25,
	0x3b, 0xce, 0x9b, 0x3d, 0x41, 0xd4, 0xb4, 0x7d, 0x85, 0xe4,
	0x8c, 0xa5, 0x47, 0x9e, 0x71, 0x80, 0xef, 0xdd, 0xb4, 0x2e,
	0xf4, 0xd1, 0x2e, 0xa4, 0x17, 0x0b, 0x1f, 0x3b, 0xf3, 0xad,
	0x69, 0x36, 0x5e, 0x6b, 0x48, 0x2f, 0x38, 0x25, 0x4b, 0x25,
	0x03, 0x49, 0xd5, 0x2e, 0xcc, 0x8e, 0xc2, 0x2f, 0x39, 0x02,
	0xe0, 0x99, 0x81, 0x52, 0x1e, 0x73, 0x49, 0xfb, 0x61, 0xaa,
	0x13, 0x3a, 0xd4, 0xff, 0x3c, 0x03, 0x1f, 0x08, 0x01, 0x00,
	0x00, 0x08, 0x08, 0x02, 0x01, 0x00, 0x02, 0x00, 0x40, 0x00,
	0x01, 0x00, 0xff
};

/* s01-n43-alphabet (L0-nonrun R3-A2 XFAIL input). */
static const uint8_t IN_E00N43[] = {
	0x1e, 0x1e, 0xa0, 0x1e, 0x1e, 0x1e, 0xa0, 0xa0, 0x1e, 0x1e,
	0xa0, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
	0x1e, 0xa0, 0xa0, 0xa0, 0x1e, 0x1e, 0xa0, 0x1e, 0xa0, 0xa0,
	0xa0, 0xa0, 0xa0, 0xa0, 0x1e, 0xa0, 0x1e, 0xa0, 0xa0, 0x1e,
	0x1e, 0x1e, 0x1e
};

/* Encode must equal the pinned oracle bytes + roundtrip + determinism. */
static void comp_pin_one(int level, const uint8_t *src, size_t n,
			 const uint8_t *want, size_t want_n, const char *tag)
{
	char name[128], detail[256];
	uint8_t *enc = malloc(n + 1024);
	uint8_t *enc2 = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, eret2, dret;

	snprintf(name, sizeof(name), "fixa L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!enc || !enc2 || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (eret != want_n || memcmp(enc, want, want_n) != 0) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu want %lu tag=%02x (byte-identity)",
			 (unsigned long)eret, (unsigned long)want_n,
			 eret ? enc[0] : 0);
		t_fail(name, detail);
		goto out;
	}
	eret2 = lzmesh_encode(enc2, n + 1024, src, n, NULL, level);
	if (eret2 != eret || memcmp(enc2, enc, eret) != 0) {
		t_fail(name, "nondeterministic re-encode");
		goto out;
	}
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	if (dret != n || memcmp(dec, src, n) != 0) {
		snprintf(detail, sizeof(detail), "roundtrip %lu != %lu",
			 (unsigned long)dret, (unsigned long)n);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(enc);
	free(enc2);
	free(dec);
}

/* Known-RAW pending spec amendment: XFAIL while RAW, XPASS-note if COMP. */
static void raw_pending_one(int level, const uint8_t *src, size_t n,
			    const char *tag, const char *amend)
{
	char name[128], detail[256];
	uint8_t *enc = malloc(n + 1024);
	size_t eret;

	snprintf(name, sizeof(name), "pending L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!enc) {
		t_fail(name, "oom");
		return;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (eret == 0) {
		snprintf(detail, sizeof(detail), "refused (want RAW %lu)",
			 (unsigned long)(n + 6));
		t_fail(name, detail);
	} else if (enc[0] == 0x00 && eret == n + 6) {
		t_xfail(name, amend);
	} else {
		snprintf(detail, sizeof(detail),
			 "now COMP outlen=%lu: promote to comp_pin (amend landed)",
			 (unsigned long)eret);
		t_xfail(name, detail);
	}
	free(enc);
}

int main(void)
{
	static const int MLV[] = { 0xE05, 0xE09 };
	static const int MLV3[] = { 0xE01, 0xE05, 0xE09 };
	uint8_t p64in[100];
	size_t i, li;

	/* FIX-A u12 min repro: COMP 27B byte-identical @E05/E09. */
	for (li = 0; li < 2; li++)
		comp_pin_one(MLV[li], IN_P4N26, sizeof(IN_P4N26),
			     OUT_P4N26, sizeof(OUT_P4N26), "p4n26");

	/* FIX-A u18 cell: COMP 44B byte-identical @E05/E09. */
	for (li = 0; li < 2; li++)
		comp_pin_one(MLV[li], IN_P16N45, sizeof(IN_P16N45),
			     OUT_P16N45, sizeof(OUT_P16N45), "p16n45");

	/* PMAX64: p64 n100 COMP 93B byte-identical @E01/E05/E09. */
	for (i = 0; i < 100; i++)
		p64in[i] = UNIT_P64[i % 64];
	for (li = 0; li < 3; li++)
		comp_pin_one(MLV3[li], p64in, sizeof(p64in),
			     OUT_P64N100, sizeof(OUT_P64N100), "p64n100");

	/* Pending amendments (RAW today, oracle COMP). */
	raw_pending_one(0xE01, IN_P16N45, sizeof(IN_P16N45), "p16n45",
			"R3-A1 L1 probe model (oracle COMP 44B, port RAW)");
	raw_pending_one(0xE00, IN_E00N43, sizeof(IN_E00N43), "alpha43",
			"R3-A2 L0 litonly-HUF coverage (oracle COMP 44B, port RAW)");

	printf("---\nm32-slots-fixa: pass=%d fail=%d xfail=%d\n",
	       g_pass, g_fail, g_xfail);
	return g_fail ? 1 : 0;
}
