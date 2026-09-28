/* SPDX-License-Identifier: 0BSD */
/*
 * test_p4n3_gate.c — P4-N3 u19 gate/run-split equivalence pins.
 *
 * P4-N3 added lzmesh_u19_gate (fail-fast at first short run, exact
 * k/mn fill on pass) and converted the owned/u19 call sites to it;
 * u19_runs itself is byte-identical. Gate is a pure function of the
 * input bytes; these pins assert gate() == (runs>=2 && minrun>=3) and
 * exact-fill equality vs u19_runs on edge shapes + pinned-PRNG fuzz.
 * Symbols are globals in liblzmesh.a, declared extern here (not in
 * lzmesh.h), same as test_pm_diff.c. Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

extern unsigned lzmesh_u19_runs(const uint8_t *src, size_t size,
                                unsigned *minr);
extern int lzmesh_u19_gate(const uint8_t *src, size_t size, unsigned *k,
                           unsigned *mn);

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

/* Pinned LCG (same stream every run). */
static uint64_t g_rng = 0x243F6A8885A308D3ull;

static unsigned rnd(unsigned n)
{
	g_rng = g_rng * 6364136223846793005ull + 1442695040888963407ull;
	return (unsigned)((g_rng >> 33) % (n == 0 ? 1 : n));
}

static char g_detail[256];

/* One differential case. Returns 1 on match. */
static int diff_one(const uint8_t *b, size_t n, const char *what)
{
	unsigned k = 0xDEADu, mn = 0xDEADu, gk = 0xBEEFu, gmn = 0xBEEFu;
	unsigned ek, emn;
	int g, want;
	ek = lzmesh_u19_runs(b, n, &emn);
	g = lzmesh_u19_gate(b, n, &gk, &gmn);
	want = (ek >= 2u && emn >= 3u) ? 1 : 0;
	if (g != want) {
		snprintf(g_detail, sizeof(g_detail),
		    "%s n=%zu runs=%u min=%u gate=%d want=%d", what, n,
		    ek, emn, g, want);
		return 0;
	}
	if (g == 1 && (gk != ek || gmn != emn)) {
		snprintf(g_detail, sizeof(g_detail),
		    "%s n=%zu fill runs=%u/%u min=%u/%u", what, n, gk,
		    ek, gmn, emn);
		return 0;
	}
	if (g == 0 && (gk != 0xBEEFu || gmn != 0xBEEFu)) {
		snprintf(g_detail, sizeof(g_detail),
		    "%s n=%zu fail-path clobbered outs", what, n);
		return 0;
	}
	/* NULL-outs predicate-only form must agree too. */
	if (lzmesh_u19_gate(b, n, NULL, NULL) != want) {
		snprintf(g_detail, sizeof(g_detail),
		    "%s n=%zu null-outs mismatch", what, n);
		return 0;
	}
	(void)k;
	(void)mn;
	return 1;
}

static void edge_cases(void)
{
	static uint8_t b[2052];
	size_t i;
	int ok = 1;

	/* guards: NULL/empty/oversize (DS_MAX = 0x7fffffff). */
	if (lzmesh_u19_gate(NULL, 10, NULL, NULL) != 0)
		ok = 0;
	if (lzmesh_u19_gate(b, 0, NULL, NULL) != 0)
		ok = 0;
	if (lzmesh_u19_gate(NULL, 0, NULL, NULL) != 0)
		ok = 0;
	if (ok)
		t_pass("guards-null-empty");
	else
		t_fail("guards-null-empty", "guard mismatch");

	/* size 1..2: k=1 or short-only -> 0. */
	b[0] = 'x';
	ok = diff_one(b, 1, "n1") ? 1 : 0;
	b[1] = 'x';
	ok = diff_one(b, 2, "n2-same") && ok;
	b[1] = 'y';
	ok = diff_one(b, 2, "n2-diff") && ok;
	if (ok)
		t_pass("tiny-1-2");
	else
		t_fail("tiny-1-2", g_detail);

	/* exact RMIN edges: runs of 2/3, short head/mid/tail. */
	memcpy(b, "aabbb", 5);
	ok = diff_one(b, 5, "short-head") ? 1 : 0;
	memcpy(b, "bbbaa", 5);
	ok = diff_one(b, 5, "short-tail") && ok;
	memcpy(b, "bbbaabb", 7);
	ok = diff_one(b, 7, "short-mid") && ok;
	memcpy(b, "aaabbb", 6);
	ok = diff_one(b, 6, "pass-2x3") && ok;
	memcpy(b, "aaaa", 4);
	ok = diff_one(b, 4, "k1-long") && ok;
	if (ok)
		t_pass("rmin-edges");
	else
		t_fail("rmin-edges", g_detail);

	/* k edges: 1 run vs 2 runs at RMIN. */
	memset(b, 'Q', 64);
	ok = diff_one(b, 64, "k1-64") ? 1 : 0;
	memset(b, 'Q', 32);
	memset(b + 32, 'R', 32);
	ok = diff_one(b, 64, "k2-32") && ok;
	if (ok)
		t_pass("k-edges");
	else
		t_fail("k-edges", g_detail);

	/* block edges 15/16/17 + 255/256/257, pass and fail shapes. */
	for (i = 0; i < 6; i++) {
		static const size_t ns[] = { 15, 16, 17, 255, 256, 257 };
		size_t n = ns[i];
		memset(b, 'A', n);
		if (n >= 6) {
			memset(b + n - 3, 'B', 3); /* pass: 2 long runs */
			ok = diff_one(b, n, "edge-pass") && ok;
			b[0] = 'C'; /* fail: short head */
			ok = diff_one(b, n, "edge-fail") && ok;
		}
	}
	if (ok)
		t_pass("block-edges");
	else
		t_fail("block-edges", g_detail);

	/* long-run tail len 9/10 (u25-blocked neighborhood shapes). */
	memset(b, 'A', 100);
	memset(b + 100, 'B', 9);
	ok = diff_one(b, 109, "tail9") ? 1 : 0;
	memset(b + 100, 'B', 10);
	memset(b + 110, 'C', 3);
	ok = diff_one(b, 113, "tail10c") && ok;
	if (ok)
		t_pass("tail-lens");
	else
		t_fail("tail-lens", g_detail);
}

static void fuzz_runs(void)
{
	static uint8_t b[70000];
	int ok = 1, t;
	for (t = 0; t < 400; t++) {
		size_t n = 0, target = 1 + rnd(66000);
		unsigned alpha = 1 + rnd(5); /* small alphabet: mixed runs */
		unsigned maxrl = 1 + rnd(24);
		while (n < target) {
			unsigned rl = 1 + rnd(maxrl);
			uint8_t v = (uint8_t)('a' + rnd(alpha));
			size_t j;
			if (n + rl > target)
				rl = (unsigned)(target - n);
			for (j = 0; j < rl; j++)
				b[n++] = v;
			if (n >= target)
				break;
		}
		if (!diff_one(b, n, "fuzz-runs")) {
			ok = 0;
			break;
		}
	}
	if (ok)
		t_pass("fuzz-runs-400");
	else
		t_fail("fuzz-runs-400", g_detail);
}

static void fuzz_bytes(void)
{
	static uint8_t b[70000];
	int ok = 1, t;
	for (t = 0; t < 400; t++) {
		size_t n = 1 + rnd(66000), i;
		unsigned mode = rnd(4);
		for (i = 0; i < n; i++) {
			if (mode == 0)
				b[i] = (uint8_t)rnd(256);
			else if (mode == 1)
				b[i] = (uint8_t)('a' + rnd(3));
			else if (mode == 2)
				b[i] = (i % 7 == 0) ? (uint8_t)'Z' : (uint8_t)'a';
			else
				b[i] = (uint8_t)(i & 0xFF);
		}
		if (!diff_one(b, n, "fuzz-bytes")) {
			ok = 0;
			break;
		}
	}
	if (ok)
		t_pass("fuzz-bytes-400");
	else
		t_fail("fuzz-bytes-400", g_detail);
}

static void fuzz_wide(void)
{
	/* 256-symbol alphabet incl long fresh-head chains (owned shape). */
	static uint8_t b[70000];
	int ok = 1, t;
	for (t = 0; t < 200; t++) {
		size_t n = 1 + rnd(66000), i = 0;
		while (i < n) {
			unsigned rl = 1 + rnd(70);
			uint8_t v = (uint8_t)rnd(256);
			size_t j;
			if (i + rl > n)
				rl = (unsigned)(n - i);
			for (j = 0; j < rl; j++)
				b[i++] = v;
		}
		if (!diff_one(b, n, "fuzz-wide")) {
			ok = 0;
			break;
		}
	}
	if (ok)
		t_pass("fuzz-wide-200");
	else
		t_fail("fuzz-wide-200", g_detail);
}

int main(void)
{
	edge_cases();
	fuzz_runs();
	fuzz_bytes();
	fuzz_wide();
	printf("p4n3_gate: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
