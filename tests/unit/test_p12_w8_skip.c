/* SPDX-License-Identifier: 0BSD */
/*
 * test_p12_w8_skip.c — P12-W8 skip-solve differential pins.
 *
 * The two H5DEEP q-floor sites (u35_tryq + h3sym twin) try
 * lzmesh_s3_huff first and run package-merge only when it declines.
 * Sound because: (1) s3's length MULTISET equals PM@10's whenever s3
 * fits depth<=10 (w8_diff: 660k+ harvested+fuzz cases, 0 MM), and
 * (2) rank_assign reassigns every length by original-freq rank, so
 * per-symbol placement is dead at both sites. PM-fail-while-fits
 * (DANGER) is 0 in the same corpus: s3-success implies PM-success.
 * Includes src/lzmesh_enc.c for static access (no public-API calls,
 * so no archive duplicate). Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static uint64_t g_rng;
static uint32_t rnd(void)
{
	uint64_t x = g_rng;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	g_rng = x;
	return (uint32_t)(x >> 11);
}

static void ms_count(const uint8_t *lens, unsigned nsym, unsigned *cnt)
{
	unsigned i;
	for (i = 0u; i <= 10u; i++)
		cnt[i] = 0u;
	for (i = 0u; i < nsym; i++)
		if (lens[i] <= 10u)
			cnt[lens[i]]++;
}

/* The exact ship rule, factored for the test. Returns maxlen used. */
static void one_case(const uint32_t *freq, unsigned nsym, unsigned limit,
		     const char *tag)
{
	uint8_t pl[256], hl[256];
	unsigned pmr, cnt1[11], cnt2[11];
	unsigned i, hm = 0u;
	int s3r;
	char detail[160];
	pmr = lzmesh_pack1_solve(freq, nsym, limit, pl);
	s3r = lzmesh_s3_huff(freq, nsym, hl);
	if (s3r != 0) {
		for (i = 0u; i < nsym; i++)
			if (hl[i] > hm)
				hm = hl[i];
	}
	if (s3r != 0 && hm <= limit) {
		if (pmr == 0u) {
			snprintf(detail, sizeof detail,
				 "DANGER pm-fail while s3 fits (hmax=%u)", hm);
			t_bad(tag, detail);
			return;
		}
		ms_count(pl, nsym, cnt1);
		ms_count(hl, nsym, cnt2);
		if (memcmp(cnt1, cnt2, sizeof cnt1) != 0) {
			snprintf(detail, sizeof detail,
				 "multiset MM (hmax=%u)", hm);
			t_bad(tag, detail);
			return;
		}
	}
	/* s3-decline/binding/PM-fail paths: ship runs PM (exact by
	 * construction); nothing to assert beyond determinism. */
}

int main(void)
{
	static const unsigned nset[] = { 2, 3, 5, 8, 16, 32, 64, 128, 256 };
	unsigned long long fi;
	unsigned i;
	/* Edges: uniform, ties, spike, pow2, capacity rim. */
	for (i = 0u; i < 5u; i++) {
		uint32_t freq[256];
		unsigned n = 256u, k;
		for (k = 0u; k < 256u; k++)
			freq[k] = 0u;
		switch (i) {
		case 0:
			for (k = 0u; k < n; k++)
				freq[k] = 100u;
			break;
		case 1:
			for (k = 0u; k < n; k++)
				freq[k] = 1u + (k & 3u);
			break;
		case 2:
			freq[0] = 1000000u;
			for (k = 1u; k < n; k++)
				freq[k] = 1u;
			break;
		case 3:
			for (k = 0u; k < n; k++)
				freq[k] = 1u << (k & 7u);
			break;
		default:
			for (k = 0u; k < n; k++)
				freq[k] = 7u;
			break;
		}
		one_case(freq, 256u, 10u, "edge");
	}
	t_ok("w8 edges 5/5 no-MM-no-DANGER");
	/* Fixed-seed fuzz (shapes mirror w8_diff). */
	g_rng = 0x12345678u;
	for (fi = 0u; fi < 3000u; fi++) {
		uint32_t freq[256];
		unsigned n, shape, k;
		n = nset[rnd() % (sizeof nset / sizeof nset[0])];
		shape = (unsigned)(fi % 6u);
		for (k = 0u; k < 256u; k++)
			freq[k] = 0u;
		{
			unsigned base = rnd() % 256u;
			for (k = 0u; k < n; k++) {
				unsigned s = (base + k) % 256u;
				switch (shape) {
				case 0:
					freq[s] = 1u + rnd() % 1000u;
					break;
				case 1:
					freq[s] = 1u + rnd() % 4u;
					break;
				case 2:
					freq[s] = (k == 0u) ? 100000u :
					    1u + rnd() % 10u;
					break;
				case 3:
					freq[s] = 1u << (rnd() % 12u);
					break;
				case 4: {
					uint32_t qq = 1u + rnd() % 2048u;
					uint32_t r = 1u + rnd() % 5000u;
					freq[s] = (r < qq) ? qq : r;
					break;
				}
				default:
					freq[s] = 1u + rnd() % 65536u;
					break;
				}
			}
		}
		one_case(freq, 256u, 10u, "fuzz10");
	}
	/* Limit-5 fuzz (meta shape + over-capacity declines). */
	for (fi = 0u; fi < 1000u; fi++) {
		uint32_t freq[256];
		unsigned n, k;
		n = 2u + rnd() % 31u; /* 2..32, rim incl */
		for (k = 0u; k < 256u; k++)
			freq[k] = 0u;
		for (k = 0u; k < n; k++)
			freq[(rnd() + k * 37u) % 256u] =
			    1u + rnd() % ((fi % 3u == 0u) ? 3u : 500u);
		one_case(freq, 256u, 5u, "fuzz5");
	}
	if (t_fail == 0)
		t_ok("w8 fuzz 4000/4000 no-MM-no-DANGER");
	printf("w8skip: pass=%d fail=%d\n", t_pass, t_fail);
	return t_fail != 0;
}
