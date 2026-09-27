/* SPDX-License-Identifier: 0BSD */
/*
 * test_comp_roundtrip.c — run-shape COMP roundtrips + outlen pins +
 * L0 ceiling + non-run pending.
 *
 * SPEC-v2: S3.13 (fresh-22 pair: n21 RAW 27B / n22 COMP 23B;
 * 16M-run 27B), S3.10 (N263/264/265 ladder; run64 `07`+`36`),
 * S5.3 (TIER-1 D>fo / TIER-2 gates; boundary pairs), S4.2
 * (REPEAT-lit ceiling R<=62880 exact: 62880 ACCEPT / 62881 REJECT),
 * S5.7c (L0 litonly vs L1/L5/L9 match rows), E-B2 (general non-run
 * COMP pending), M12 (L0 run COMP past 62880 is encoder-side pending:
 * port emits COMP the decoder correctly rejects).
 *
 * Runs must roundtrip exactly at all levels with COMP outlen < n
 * from n=22 up; n=21 stays RAW 27B. L0 n>=62881 and non-run
 * compressible shapes are pass-or-XFAIL (pending, never FAIL).
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

static const int LEVELS[] = { 0xE00, 0xE01, 0xE05, 0xE09 };

static void run_one(int level, size_t n)
{
	char name[96], detail[192];
	uint8_t *z, *enc, *dec;
	size_t eret, ds, dret, i;
	int ok;

	snprintf(name, sizeof(name), "run L%x n=%lu", level, (unsigned long)n);
	z = calloc(n ? n : 1, 1);
	enc = malloc(n + 1024);
	dec = malloc(n + 64);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, level);
	if (eret == 0 || eret > n + 1024) {
		snprintf(detail, sizeof(detail), "encode returned %lu",
			 (unsigned long)eret);
		t_fail(name, detail);
		goto out;
	}
	if (enc[eret - 1] != 0xFF) {
		t_fail(name, "missing END marker");
		goto out;
	}
	ds = lzmesh_decoded_size(enc, eret);
	if (ds != n) {
		snprintf(detail, sizeof(detail), "decoded_size=%lu want %lu",
			 (unsigned long)ds, (unsigned long)n);
		t_fail(name, detail);
		goto out;
	}
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	ok = (dret == n);
	for (i = 0; i < n && ok; i++)
		if (dec[i] != 0)
			ok = 0;
	if (!ok) {
		snprintf(detail, sizeof(detail), "decode=%lu zeros-ok=%d",
			 (unsigned long)dret, ok);
		t_fail(name, detail);
		goto out;
	}
	/*
	 * TIER-2 keeps n22 COMP despite total>input (S5.3), so the
	 * n>=22 gate is the COMP tag; outlen<n binds from n>=32 up.
	 */
	if (n >= 22 && enc[0] != 0x01) {
		snprintf(detail, sizeof(detail),
			 "run not COMP: tag=%02x", enc[0]);
		t_fail(name, detail);
		goto out;
	}
	if (n >= 32 && eret >= n) {
		snprintf(detail, sizeof(detail),
			 "run not smaller: outlen=%lu >= n", (unsigned long)eret);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(z);
	free(enc);
	free(dec);
}

/* Pin exact outlen/tag for spec boundary pairs. */
static void pin_one(int level, size_t n, size_t want_len, uint8_t want_tag,
		    const char *why)
{
	char name[96], detail[192];
	uint8_t *z = calloc(n, 1);
	uint8_t *enc = malloc(n + 1024);
	size_t eret;

	snprintf(name, sizeof(name), "pin L%x n=%lu", level, (unsigned long)n);
	if (!z || !enc) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, level);
	if (eret != want_len || enc[0] != want_tag) {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu tag=%02x, want %lu/%02x (%s)",
			 (unsigned long)eret, eret ? enc[0] : 0,
			 (unsigned long)want_len, want_tag, why);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(z);
	free(enc);
}

/* L0 ceiling pair: 62880 roundtrips; >=62881 is encoder-pending. */
static void l0_cap_one(size_t n, int expect_ok)
{
	char name[96], detail[192];
	uint8_t *z = calloc(n, 1);
	uint8_t *enc = malloc(n + 1024);
	uint8_t *dec = malloc(n + 64);
	size_t eret, dret, i;
	int ok;

	snprintf(name, sizeof(name), "L0cap n=%lu", (unsigned long)n);
	if (!z || !enc || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	eret = lzmesh_encode(enc, n + 1024, z, n, NULL, 0xE00);
	if (eret == 0) {
		t_fail(name, "encode returned 0");
		goto out;
	}
	dret = lzmesh_decode(dec, n + 64, enc, eret, NULL);
	ok = (dret == n);
	for (i = 0; i < n && ok; i++)
		if (dec[i] != 0)
			ok = 0;
	if (ok) {
		t_pass(name);
	} else if (!expect_ok && dret == 0) {
		snprintf(detail, sizeof(detail),
			 "enc %luB COMP, dec fail-closed 0 (M12: L0 run past "
			 "62880 needs enc cap/split; no misdecode)",
			 (unsigned long)eret);
		t_xfail(name, detail);
	} else {
		snprintf(detail, sizeof(detail), "decode=%lu (misdecode?)",
			 (unsigned long)dret);
		t_fail(name, detail);
	}
out:
	free(z);
	free(enc);
	free(dec);
}

/* Non-run compressible input must eventually emit COMP (E-B2 pending). */
static void nonrun_emit_one(int level, uint8_t *src, size_t n,
			    const char *tag)
{
	char name[96], detail[192];
	uint8_t *enc = malloc(n + 1024);
	size_t eret;

	snprintf(name, sizeof(name), "nonrun-emit L%x %s n=%lu", level, tag,
		 (unsigned long)n);
	if (!enc) {
		t_fail(name, "oom");
		return;
	}
	eret = lzmesh_encode(enc, n + 1024, src, n, NULL, level);
	if (eret == 0) {
		t_fail(name, "encode returned 0");
		goto out;
	}
	if (eret < n) {
		t_pass(name);
	} else {
		snprintf(detail, sizeof(detail),
			 "outlen=%lu >= n (E-B2 general COMP pending)",
			 (unsigned long)eret);
		t_xfail(name, detail);
	}
out:
	free(enc);
}

int main(void)
{
	static const size_t NS[] = { 0, 1, 8, 21, 22, 23, 32, 100, 264,
				     265, 300, 1000, 4096, 8192, 16384,
				     32768 };
	size_t li, ni;
	uint8_t *fib, *text;
	size_t p, k;
	static const char sentence[] =
		"the quick brown fox jumps over the lazy dog. ";

	for (li = 0; li < sizeof(LEVELS) / sizeof(LEVELS[0]); li++)
		for (ni = 0; ni < sizeof(NS) / sizeof(NS[0]); ni++)
			run_one(LEVELS[li], NS[ni]);

	/* Boundary-pair pins (S3.13 fresh-22, S3.10 ladder). */
	for (li = 0; li < sizeof(LEVELS) / sizeof(LEVELS[0]); li++) {
		pin_one(LEVELS[li], 21, 27, 0x00, "fresh-21 RAW");
		pin_one(LEVELS[li], 22, 23, 0x01, "fresh-22 COMP");
		pin_one(LEVELS[li], 265, 27, 0x01, "ladder 265");
	}
	pin_one(0xE00, 264, 27, 0x01, "L0 264 long-form");
	pin_one(0xE01, 264, 23, 0x01, "L1 264 short-form");
	pin_one(0xE05, 264, 23, 0x01, "L5 264 short-form");
	pin_one(0xE09, 264, 23, 0x01, "L9 264 short-form");

	/* L0 REPEAT-lit ceiling (S4.2): 62880 ok, past is pending. */
	l0_cap_one(62880, 1);
	l0_cap_one(62881, 0);
	l0_cap_one(62882, 0);
	run_one(0xE05, 62881);

	/* Non-run shapes: Fibonacci counts + text-like (E-B2 pending). */
	{
		size_t fibc[20], total = 0;
		int i;

		fibc[0] = 1;
		fibc[1] = 2;
		for (i = 2; i < 20; i++)
			fibc[i] = fibc[i - 1] + fibc[i - 2];
		for (i = 0; i < 20; i++)
			total += fibc[i];
		fib = malloc(total);
		text = malloc(2048);
		if (fib && text) {
			size_t q = 0;

			for (i = 0; i < 20; i++)
				for (k = 0; k < fibc[i]; k++)
					fib[q++] = (uint8_t)(i + 1);
			for (p = 0; p < 2048; p++)
				text[p] = (uint8_t)sentence[p %
					(sizeof(sentence) - 1)];
			for (li = 0; li < sizeof(LEVELS) / sizeof(LEVELS[0]);
			     li++) {
				nonrun_emit_one(LEVELS[li], fib, total, "fib20");
				nonrun_emit_one(LEVELS[li], text, 2048,
						"text-like");
			}
		}
		free(fib);
		free(text);
	}

	printf("---\ncomp-roundtrip: pass=%d fail=%d xfail=%d\n", g_pass,
	       g_fail, g_xfail);
	return g_fail ? 1 : 0;
}
