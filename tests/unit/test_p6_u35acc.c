/* SPDX-License-Identifier: 0BSD */
/*
 * test_p6_u35acc.c — P6-W4 u35 word-accumulator edge + differential pins.
 *
 * P6-W4 replaced the per-symbol byte-RMW loop (u35_put) at the five
 * Huffman emit sites with a per-lane 64b accumulator (lzmesh_u35_acc_put,
 * 4B word flush, tail drain). These pins assert:
 *  (a) bit-exactness vs the base byte loop (copied verbatim below as
 *      ref_put) over pinned-PRNG (val,n) streams, n=0..32 + fail-safe
 *      n>32, incl val garbage above bit n (mask check);
 *  (b) flush-boundary vectors: nbits hitting exactly 32 pre-put, 31+32
 *      (63-bit acc max), the 11x3b+32b meta pattern, empty lane, and
 *      total-bit tails B%8 = 0..7;
 *  (c) 8-lane i&7 interleave differential in emit order;
 *  (d) no-overrun: pad sentinel past ceil(B/8) untouched + guard-page
 *      lanes ending at a PROT_NONE edge (any over-wide flush faults).
 * Includes src/lzmesh_enc.c for static access (no public-API calls, so
 * no archive duplicate). Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../../src/lzmesh_enc.c"

static int t_pass, t_fail;

static void t_ok(const char *name)
{
	t_pass++;
	printf("PASS  %s\n", name);
}

static void t_bad(const char *name, const char *detail)
{
	t_fail++;
	printf("FAIL  %s :: %s\n", name, detail);
}

/* Base byte loop, verbatim from 2eec7385 (reference oracle). */
static void ref_put(uint8_t *base, unsigned *pos, unsigned val, unsigned n)
{
	unsigned p = *pos;
	unsigned b = p >> 3;
	unsigned sh = p & 7u;
	*pos = p + n;
	while (n > 0u) {
		unsigned take = 8u - sh;
		if (take > n)
			take = n;
		base[b] |= (uint8_t)(((val & ((1u << take) - 1u))) << sh);
		val >>= take;
		n -= take;
		b++;
		sh = 0u;
	}
}

/* Pinned LCG (same stream every run). */
static uint64_t t_rng = 0x2545F4914F6CDD1Dull;

static uint32_t t_next(void)
{
	t_rng = t_rng * 6364136223846793005ull + 1442695040888963407ull;
	return (uint32_t)(t_rng >> 33);
}

/* Run one (val,n) stream through both impls; compare pos + full span. */
static int t_stream_diff(const uint32_t *vals, const unsigned *ns,
			 unsigned count, unsigned span)
{
	static uint8_t rb[516], ab[516];
	unsigned rp = 0u, ap = 0u, i;
	lzmesh_u35_acc a;
	if (span > (unsigned)sizeof rb)
		return 0;
	memset(rb, 0, sizeof rb);
	memset(ab, 0, sizeof ab);
	a.acc = 0u;
	a.nbits = 0u;
	a.out = ab;
	for (i = 0u; i < count; i++) {
		ref_put(rb, &rp, vals[i], ns[i]);
		lzmesh_u35_acc_put(&a, &ap, vals[i], ns[i]);
	}
	lzmesh_u35_acc_flush(&a);
	if (rp != ap)
		return 0;
	return memcmp(rb, ab, span) == 0;
}

static void t_random_diff(void)
{
	static uint32_t vals[96];
	static unsigned ns[96];
	static const unsigned nbias[] = {
		0, 1, 2, 3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 32
	};
	unsigned seq, i;
	for (seq = 0u; seq < 2000u; seq++) {
		unsigned count = 1u + t_next() % 96u;
		unsigned tot = 0u;
		for (i = 0u; i < count; i++) {
			unsigned n;
			if ((t_next() & 7u) == 0u)
				n = t_next() % 33u; /* uniform 0..32 */
			else
				n = nbias[t_next() %
					  (unsigned)(sizeof nbias /
						     sizeof nbias[0])];
			ns[i] = n;
			vals[i] = t_next() ^ ((uint32_t)t_next() << 16);
			if ((t_next() & 3u) == 0u)
				vals[i] |= 0xFFFFFFFFu; /* garbage hi bits */
			tot += n;
		}
		if (!t_stream_diff(vals, ns, count, (tot + 7u) / 8u + 1u)) {
			t_bad("random-diff", "mismatch");
			return;
		}
	}
	t_ok("random-diff");
}

static void t_fail_safe_n(void)
{
	/* n>32 unreachable in-tree; pin the zero-fill recursion anyway. */
	static const unsigned ns[] = { 33, 40, 64, 33 };
	static const uint32_t vals[] = {
		0xDEADBEEFu, 0xFFFFFFFFu, 0x12345678u, 0u
	};
	if (!t_stream_diff(vals, ns, 4u, 24u)) {
		t_bad("failsafe-n", "mismatch");
		return;
	}
	t_ok("failsafe-n");
}

static void t_boundaries(void)
{
	/* nbits==32 pre-put, 31+32 max, meta 11x3+32, empty lane. */
	static uint8_t rb[64], ab[64];
	unsigned rp, ap, i;
	lzmesh_u35_acc a;
#define T_RESET() do { \
		memset(rb, 0, sizeof rb); memset(ab, 0, sizeof ab); \
		rp = 0u; ap = 0u; a.acc = 0u; a.nbits = 0u; a.out = ab; \
	} while (0)
#define T_CMP(tag, span) do { \
		lzmesh_u35_acc_flush(&a); \
		if (rp != ap || memcmp(rb, ab, span) != 0) { \
			t_bad("boundaries", tag); \
			return; \
		} \
	} while (0)
	/* 32 x 1b then 32b: pre-put nbits exactly 32 -> flush-then-OR. */
	T_RESET();
	for (i = 0u; i < 32u; i++) {
		ref_put(rb, &rp, 1u, 1u);
		lzmesh_u35_acc_put(&a, &ap, 1u, 1u);
	}
	ref_put(rb, &rp, 0xA5A5A5A5u, 32u);
	lzmesh_u35_acc_put(&a, &ap, 0xA5A5A5A5u, 32u);
	T_CMP("nbits32+32", 9u);
	/* 31 x 1b then 32b: acc peaks at 63 bits, no flush mid-put. */
	T_RESET();
	for (i = 0u; i < 31u; i++) {
		ref_put(rb, &rp, (i & 1u), 1u);
		lzmesh_u35_acc_put(&a, &ap, (i & 1u), 1u);
	}
	ref_put(rb, &rp, 0xFFFFFFFFu, 32u);
	lzmesh_u35_acc_put(&a, &ap, 0xFFFFFFFFu, 32u);
	T_CMP("nbits31+32", 9u);
	/* meta pattern: 11 x 3b then 32b bitmap. */
	T_RESET();
	for (i = 0u; i < 11u; i++) {
		ref_put(rb, &rp, i & 7u, 3u);
		lzmesh_u35_acc_put(&a, &ap, i & 7u, 3u);
	}
	ref_put(rb, &rp, 0x00000001u, 32u);
	lzmesh_u35_acc_put(&a, &ap, 0x00000001u, 32u);
	T_CMP("meta11x3+32", 10u);
	/* empty lane: flush writes nothing. */
	T_RESET();
	ab[0] = 0xAAu; /* poison to prove no write */
	a.out = ab;
	lzmesh_u35_acc_flush(&a);
	if (ab[0] != 0xAAu || rp != ap) {
		t_bad("boundaries", "empty-flush");
		return;
	}
	/* tails B%8 = 0..7 via single puts n = 8..15 + 32b pairs. */
	for (i = 0u; i < 8u; i++) {
		T_RESET();
		ref_put(rb, &rp, 0x5A5A5A5Au, 8u + i);
		lzmesh_u35_acc_put(&a, &ap, 0x5A5A5A5Au, 8u + i);
		ref_put(rb, &rp, 0xFFFFFFFFu, 32u);
		lzmesh_u35_acc_put(&a, &ap, 0xFFFFFFFFu, 32u);
		T_CMP("tail-sweep", 7u);
	}
	t_ok("boundaries");
#undef T_RESET
#undef T_CMP
}

static void t_interleave(void)
{
	/* 8-lane emit order: meta on lane 0, then i&7 round-robin. */
	static uint8_t rb[8][600], ab[8][600];
	unsigned rp[8], ap[8], i, k;
	lzmesh_u35_acc a[8];
	for (k = 0u; k < 8u; k++) {
		memset(rb[k], 0, sizeof rb[k]);
		memset(ab[k], 0, sizeof ab[k]);
		rp[k] = 0u;
		ap[k] = 0u;
		a[k].acc = 0u;
		a[k].nbits = 0u;
		a[k].out = ab[k];
	}
	for (i = 0u; i < 11u; i++) {
		unsigned v = t_next() & 7u;
		ref_put(rb[0], &rp[0], v, 3u);
		lzmesh_u35_acc_put(&a[0], &ap[0], v, 3u);
	}
	{
		unsigned bm = t_next();
		ref_put(rb[0], &rp[0], bm, 32u);
		lzmesh_u35_acc_put(&a[0], &ap[0], bm, 32u);
	}
	for (i = 0u; i < 4000u; i++) {
		unsigned n = 1u + t_next() % 10u;
		unsigned v = t_next() & ((1u << n) - 1u);
		k = i & 7u;
		ref_put(rb[k], &rp[k], v, n);
		lzmesh_u35_acc_put(&a[k], &ap[k], v, n);
	}
	for (k = 0u; k < 8u; k++)
		lzmesh_u35_acc_flush(&a[k]);
	for (k = 0u; k < 8u; k++) {
		if (rp[k] != ap[k]
		    || memcmp(rb[k], ab[k], (rp[k] + 7u) / 8u + 1u) != 0) {
			t_bad("interleave", "lane-mismatch");
			return;
		}
	}
	t_ok("interleave");
}

static void t_sentinel(void)
{
	/* Bytes past ceil(B/8) stay untouched (0xA5) on both sides. */
	static uint8_t rb[80], ab[80];
	unsigned rp = 0u, ap = 0u, i;
	lzmesh_u35_acc a;
	memset(rb, 0xA5, sizeof rb);
	memset(ab, 0xA5, sizeof ab);
	memset(rb, 0, 33u); /* ceil(259/8); [33,80) must stay 0xA5 */
	memset(ab, 0, 33u);
	a.acc = 0u;
	a.nbits = 0u;
	a.out = ab;
	ref_put(rb, &rp, 0xFFFFFFFFu, 0u); /* n=0 no-op pins */
	lzmesh_u35_acc_put(&a, &ap, 0xFFFFFFFFu, 0u);
	for (i = 0u; i < 37u; i++) { /* 37 x 7b = 259b -> 33B */
		ref_put(rb, &rp, 0x55u, 7u);
		lzmesh_u35_acc_put(&a, &ap, 0x55u, 7u);
	}
	lzmesh_u35_acc_flush(&a);
	if (rp != ap || rp != 259u || memcmp(rb, ab, 33u) != 0) {
		t_bad("sentinel", "body-mismatch");
		return;
	}
	for (i = 33u; i < (unsigned)sizeof ab; i++) {
		if (rb[i] != 0xA5u || ab[i] != 0xA5u) {
			t_bad("sentinel", "pad-overwrite");
			return;
		}
	}
	t_ok("sentinel");
}

static void t_guard_page(void)
{
	/* Lane ending exactly at a PROT_NONE edge; any over-wide flush
	 * (or tail write past ceil) faults. Both impls must survive. */
	long ps = sysconf(_SC_PAGESIZE);
	static const unsigned lens[] = { 1, 2, 3, 4, 5, 8, 9, 33, 65 };
	unsigned li;
	uint8_t *map;
	if (ps < 16)
		return; /* cannot probe; sentinel test still guards */
	map = (uint8_t *)mmap(NULL, (size_t)ps * 2u, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANON, -1, 0);
	if (map == MAP_FAILED) {
		t_bad("guard-page", "mmap");
		return;
	}
	if (mprotect(map + ps, (size_t)ps, PROT_NONE) != 0) {
		t_bad("guard-page", "mprotect");
		munmap(map, (size_t)ps * 2u);
		return;
	}
	for (li = 0u; li < (unsigned)(sizeof lens / sizeof lens[0]); li++) {
		unsigned L = lens[li];
		uint8_t *lane = map + ps - L;
		unsigned ap = 0u, i;
		lzmesh_u35_acc a;
		/* exact-fill: 8L bits. Any over-wide flush faults. */
		memset(lane, 0, L);
		a.acc = 0u;
		a.nbits = 0u;
		a.out = lane;
		for (i = 0u; i < L * 2u; i++)
			lzmesh_u35_acc_put(&a, &ap, t_next() & 0xFu, 4u);
		lzmesh_u35_acc_flush(&a);
		if (ap != L * 8u) {
			t_bad("guard-page", "pos");
			munmap(map, (size_t)ps * 2u);
			return;
		}
		/* partial tail: 8L-3 bits. */
		memset(lane, 0, L);
		a.acc = 0u;
		a.nbits = 0u;
		a.out = lane;
		ap = 0u;
		for (i = 0u; i < L * 2u - 1u; i++)
			lzmesh_u35_acc_put(&a, &ap, 3u, 4u);
		lzmesh_u35_acc_put(&a, &ap, 1u, 1u);
		lzmesh_u35_acc_flush(&a);
		if (ap != L * 8u - 3u) {
			t_bad("guard-page", "tail-pos");
			munmap(map, (size_t)ps * 2u);
			return;
		}
	}
	munmap(map, (size_t)ps * 2u);
	t_ok("guard-page");
}

int main(void)
{
	t_random_diff();
	t_fail_safe_n();
	t_boundaries();
	t_interleave();
	t_sentinel();
	t_guard_page();
	printf("u35acc: pass=%d fail=%d\n", t_pass, t_fail);
	return t_fail ? 1 : 0;
}
