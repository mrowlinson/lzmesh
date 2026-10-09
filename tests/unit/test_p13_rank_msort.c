/* SPDX-License-Identifier: 0BSD */
/*
 * test_p13_rank_msort.c — P13-RANK msort-vs-insertion differential pins.
 *
 * lzmesh_u35_rank_msort must produce exactly the insertion sort's
 * permutation: (freq desc, sym desc) with distinct syms is a strict
 * total order, so the permutation is unique. Reference insertion
 * mirrors the replaced code verbatim. Includes src/lzmesh.c for
 * static access. Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/lzmesh.c"

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

/* Reference: the replaced insertion sort, verbatim logic. */
static void ref_insertion(const uint32_t *freq, uint16_t *order, unsigned n)
{
	unsigned i;
	for (i = 1u; i < n; i++) {
		uint16_t x = order[i];
		unsigned j = i;
		while (j > 0u
		    && (freq[x] > freq[order[j - 1u]]
		        || (freq[x] == freq[order[j - 1u]]
		            && x > order[j - 1u]))) {
			order[j] = order[j - 1u];
			j--;
		}
		order[j] = x;
	}
}

static void one_case(const uint32_t *freq, unsigned n, const char *tag,
    unsigned long *mism)
{
	uint16_t a[256], b[256], tmp[256];
	unsigned i;
	static char detail[128];
	for (i = 0u; i < n; i++)
		a[i] = b[i] = (uint16_t)i;
	ref_insertion(freq, a, n);
	lzmesh_u35_rank_msort(freq, b, tmp, n);
	for (i = 0u; i < n; i++) {
		if (a[i] != b[i]) {
			(*mism)++;
			if (*mism < 4u) {
				snprintf(detail, sizeof detail,
				    "%s n=%u i=%u ref=%u new=%u", tag, n, i,
				    a[i], b[i]);
				t_bad("rank-msort-ident", detail);
			}
			return;
		}
	}
}

int main(void)
{
	unsigned long mism = 0u;
	unsigned long cases = 0u;
	unsigned fi, k;
	g_rng = 0x9e3779b97f4a7c15ULL;
	/* Edge shapes. */
	{
		uint32_t freq[256];
		/* all-equal freq (tie path, sym desc decides). */
		for (k = 0u; k < 256u; k++)
			freq[k] = 7u;
		one_case(freq, 256u, "eq256", &mism);
		cases++;
		/* strictly ascending / descending. */
		for (k = 0u; k < 256u; k++)
			freq[k] = k + 1u;
		one_case(freq, 256u, "asc", &mism);
		cases++;
		for (k = 0u; k < 256u; k++)
			freq[k] = 256u - k;
		one_case(freq, 256u, "desc", &mism);
		cases++;
		/* n=2 minimal. */
		freq[0] = 5u;
		freq[1] = 5u;
		one_case(freq, 2u, "n2tie", &mism);
		cases++;
		freq[0] = 1u;
		freq[1] = 99u;
		one_case(freq, 2u, "n2", &mism);
		cases++;
		/* heavy ties (qq-floor shape). */
		for (k = 0u; k < 256u; k++)
			freq[k] = 1u + (k % 3u);
		one_case(freq, 256u, "ties3", &mism);
		cases++;
	}
	/* Fuzz: random n, sizes incl odd/non-pow2, tie-heavy + sparse. */
	for (fi = 0u; fi < 4000u; fi++) {
		uint32_t freq[256];
		unsigned n = 2u + rnd() % 255u;
		unsigned mode = fi % 4u;
		for (k = 0u; k < 256u; k++)
			freq[k] = 0u;
		for (k = 0u; k < n; k++) {
			unsigned s = (rnd() + k * 37u) % 256u;
			switch (mode) {
			case 0u:
				freq[s] = 1u + rnd() % 4u;
				break;
			case 1u:
				freq[s] = 1u + rnd() % 65536u;
				break;
			case 2u:
				freq[s] = (rnd() % 5u == 0u) ? 1000u : 1u;
				break;
			default:
				freq[s] = 1u + rnd() % 251u;
				break;
			}
		}
		one_case(freq, 256u, "fuzz", &mism);
		cases++;
	}
	if (mism == 0u)
		t_ok("rank-msort 4006/4006 ident");
	printf("rankmsort: cases=%lu mism=%lu pass=%d fail=%d\n", cases, mism,
	    t_pass, t_fail);
	return t_fail != 0;
}
