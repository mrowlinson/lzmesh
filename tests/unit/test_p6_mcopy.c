/* SPDX-License-Identifier: 0BSD */
/*
 * test_p6_mcopy.c — P6-W5 match_copy word-path differential + overrun pins.
 *
 * P6-W5 replaced the byte loops in lz_u3_match_copy_scalar with
 * word-at-a-time ops (u64 bulk + width-capped tails for d>=8; splat
 * fill for d==1; period-chunk word loops for d2-7). These pins assert:
 *  (a) value-exactness vs the single forward byte loop (ground truth
 *      all paths claim identity with) over d=1..8 x n=0..24 (required
 *      grid) plus d=9/15/16/17/31/64 and n=25/31/32/33/63/64/65/100
 *      (bulk/tail/NEON-dispatch edges), w in {d, d+1} + 64..71
 *      (unaligned word-op coverage), 3 history patterns, via BOTH
 *      lz_u3_match_copy_scalar and the lz_u3_match_copy dispatcher
 *      (default build exercises the NEON bulk+tail composition;
 *      the LZMESH_SCALAR=1 rebuild covers the pure-scalar legs);
 *  (b) exact-n: canary bytes below w-d and above w+n plus intact
 *      history [w-d,w) verified on every vector — any overrun,
 *      underrun, or source clobber fails;
 *  (c) guard-page edge probes (mmap + PROT_NONE): bulk+tail ending
 *      exactly at the page edge must not fault.
 * Includes src/lzmesh_dec.c for static access (dec.o refs libc only,
 * so no archive member is pulled and no duplicate arises — same trick
 * as test_p5_wordload.c). Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../../src/lzmesh_dec.c"

static int mc_pass, mc_fail;

static void mc_ok(const char *name)
{
	mc_pass++;
	printf("PASS  %s\n", name);
}

static void mc_bad(const char *name, const char *detail)
{
	mc_fail++;
	printf("FAIL  %s :: %s\n", name, detail);
}

/* Ground truth: single forward byte loop (+ n==0 no-op). */
static void ref_mcopy(uint8_t *dst, size_t w, uint32_t d, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		dst[w + i] = (uint8_t)dst[w + i - (size_t)d];
}

/* Pinned LCG (same stream every run). */
static uint64_t mc_rng = 0x9E3779B97F4A7C15ull;

static uint8_t mc_next(void)
{
	mc_rng = mc_rng * (uint64_t)6364136223846793005ull +
		 (uint64_t)1442695040888963407ull;
	return (uint8_t)(mc_rng >> 33);
}

#define MC_CAP 512
#define MC_CAN 0xC9

static const uint32_t mc_ds[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17,
				  31, 64 };
static const size_t mc_ns[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
				13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
				24, 25, 31, 32, 33, 63, 64, 65, 100 };

static void mc_fill_hist(uint8_t *b, size_t lo, size_t hi, int pat)
{
	size_t i;
	for (i = lo; i < hi; i++) {
		if (pat == 0)
			b[i] = (uint8_t)(i & 0xFFu); /* counter: unique */
		else if (pat == 1)
			b[i] = mc_next(); /* prng */
		else
			b[i] = (uint8_t)(i & 1u ? 0xFFu : 0x00u); /* alternating */
	}
}

/* One vector. fn: 0 = scalar, 1 = dispatcher. Returns fails. */
static int mc_vector(int fn, uint32_t d, size_t n, size_t w, int pat)
{
	static uint8_t a[MC_CAP], b[MC_CAP];
	size_t i, slo;
	char detail[160];
	slo = w - (size_t)d;
	memset(a, MC_CAN, sizeof(a));
	memset(b, MC_CAN, sizeof(b));
	mc_fill_hist(a, slo, w, pat);
	mc_fill_hist(b, slo, w, pat);
	/* Identical PRNG streams: refill b from a (LCG already advanced). */
	if (pat == 1)
		memcpy(b, a, sizeof(b));
	ref_mcopy(b, w, d, n);
	if (fn == 0)
		lz_u3_match_copy_scalar(a, w, d, n);
	else
		lz_u3_match_copy(a, w, d, n);
	if (memcmp(a + w, b + w, n) != 0) {
		snprintf(detail, sizeof(detail),
			 "fn=%d d=%u n=%lu w=%lu pat=%d bytes differ",
			 fn, d, (unsigned long)n, (unsigned long)w, pat);
		mc_bad("mcopy-value", detail);
		return 1;
	}
	for (i = 0; i < slo; i++) {
		if (a[i] != MC_CAN || b[i] != MC_CAN)
			break;
	}
	if (i != slo) {
		snprintf(detail, sizeof(detail),
			 "fn=%d d=%u n=%lu w=%lu pat=%d UNDERRUN @%lu",
			 fn, d, (unsigned long)n, (unsigned long)w, pat,
			 (unsigned long)i);
		mc_bad("mcopy-underrun", detail);
		return 1;
	}
	for (i = slo; i < w; i++) {
		if (a[i] != b[i])
			break;
	}
	if (i != w) {
		snprintf(detail, sizeof(detail),
			 "fn=%d d=%u n=%lu w=%lu pat=%d HISTORY-CLOBBER @%lu",
			 fn, d, (unsigned long)n, (unsigned long)w, pat,
			 (unsigned long)i);
		mc_bad("mcopy-history", detail);
		return 1;
	}
	for (i = w + n; i < MC_CAP; i++) {
		if (a[i] != MC_CAN)
			break;
	}
	if (i != MC_CAP) {
		snprintf(detail, sizeof(detail),
			 "fn=%d d=%u n=%lu w=%lu pat=%d OVERRUN @%lu",
			 fn, d, (unsigned long)n, (unsigned long)w, pat,
			 (unsigned long)i);
		mc_bad("mcopy-overrun", detail);
		return 1;
	}
	return 0;
}

/* Guard-page probe: dst[w+n] ends exactly at page edge; next page
 * PROT_NONE. Any write/read past the end faults (loud FAIL). */
static int mc_guard(uint32_t d, size_t n, int fn)
{
	long ps = sysconf(_SC_PAGESIZE);
	uint8_t *two;
	uint8_t *dst;
	uint8_t *ref;
	size_t w, i, fails0;
	char detail[128];
	if (ps < 4096)
		return 0;
	two = (uint8_t *)mmap(NULL, (size_t)2 * (size_t)ps,
			       PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANON, -1, 0);
	if (two == MAP_FAILED)
		return 0;
	if (mprotect(two + ps, (size_t)ps, PROT_NONE) != 0) {
		munmap(two, (size_t)2 * (size_t)ps);
		return 0;
	}
	/* w+n == ps (end at edge); w-d >= 64 (headroom for history). */
	w = (size_t)ps - n;
	dst = two;
	if (w < (size_t)d + (size_t)64) {
		munmap(two, (size_t)2 * (size_t)ps);
		return 0;
	}
	for (i = w - (size_t)d; i < w; i++)
		dst[i] = (uint8_t)(i & 0xFFu);
	fails0 = (size_t)mc_fail;
	if (fn == 0)
		lz_u3_match_copy_scalar(dst, w, d, n);
	else
		lz_u3_match_copy(dst, w, d, n);
	/* Value-verify vs forward-loop truth on a malloc mirror. */
	ref = (uint8_t *)malloc(w + n);
	if (ref == NULL) {
		mprotect(two + ps, (size_t)ps, PROT_READ | PROT_WRITE);
		munmap(two, (size_t)2 * (size_t)ps);
		return 0;
	}
	for (i = w - (size_t)d; i < w; i++)
		ref[i] = (uint8_t)(i & 0xFFu);
	ref_mcopy(ref, w, d, n);
	if (memcmp(dst + w, ref + w, n) != 0) {
		snprintf(detail, sizeof(detail), "fn=%d d=%u n=%lu guard-value",
			 fn, d, (unsigned long)n);
		mc_bad("mcopy-guard", detail);
	}
	free(ref);
	mprotect(two + ps, (size_t)ps, PROT_READ | PROT_WRITE);
	munmap(two, (size_t)2 * (size_t)ps);
	return mc_fail != (int)fails0;
}

int main(void)
{
	size_t di, ni, wi, fails0;
	int pat, fn;
	size_t wvals[10];
	size_t nw;
	char name[64];
	for (fn = 0; fn < 2; fn++) {
		for (di = 0; di < sizeof(mc_ds) / sizeof(mc_ds[0]); di++) {
			uint32_t d = mc_ds[di];
			fails0 = (size_t)mc_fail;
			for (ni = 0;
			     ni < sizeof(mc_ns) / sizeof(mc_ns[0]); ni++) {
				size_t n = mc_ns[ni];
				size_t k;
				nw = 0;
				wvals[nw++] = (size_t)d;
				wvals[nw++] = (size_t)d + (size_t)1;
				for (k = 0; k < 8; k++)
					wvals[nw++] = (size_t)64 + k;
				for (wi = 0; wi < nw; wi++) {
					size_t w = wvals[wi];
					if (w + n >= MC_CAP)
						continue;
					for (pat = 0; pat < 3; pat++)
						mc_vector(fn, d, n, w, pat);
				}
			}
			if (mc_fail == (int)fails0) {
				snprintf(name, sizeof(name),
					 "mcopy-fn%d-d%u-grid", fn, d);
				mc_ok(name);
			}
		}
	}
	/* Guard edges: bulk+tail at page end, both entries. */
	fails0 = (size_t)mc_fail;
	mc_guard(8, 15, 0);
	mc_guard(8, 15, 1);
	mc_guard(6, 100, 0);
	mc_guard(6, 100, 1);
	mc_guard(1, 100, 1);
	mc_guard(16, 33, 1);
	mc_guard(16, 33, 0);
	mc_guard(3, 65, 0);
	if (mc_fail == (int)fails0)
		mc_ok("mcopy-guard-edges");
	printf("p6_mcopy pass=%d fail=%d\n", mc_pass, mc_fail);
	return mc_fail == 0 ? 0 : 1;
}
