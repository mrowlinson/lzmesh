/* SPDX-License-Identifier: 0BSD */
/*
 * test_p11_sb_clz.c — P11-WINS differential: u3_sb_of / u3_bitlen closed
 * forms vs the loop originals, plus u4_dist_split field cross-check.
 *
 * Reference bodies are the pre-P11 loops (behavioral oracle, kept here).
 * Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern unsigned lzmesh_u3_sb_of(uint32_t d);
extern unsigned lzmesh_u3_bitlen(uint32_t v);
extern void lzmesh_u4_dist_split(uint32_t d, unsigned *sb, unsigned *low,
                                 uint32_t *suf);

static int g_pass, g_fail;

static void t_pass(const char *name)
{
	g_pass++;
	printf("PASS  %s\n", name);
}

static void t_fail(const char *name, uint32_t d, unsigned got, unsigned want)
{
	g_fail++;
	printf("FAIL  %s :: d=%u got=%u want=%u\n", name, d, got, want);
}

/* Pre-P11 reference loops (verbatim logic). */
static unsigned ref_bitlen(uint32_t v)
{
	unsigned n = 0;
	while (v != 0) {
		n++;
		v >>= 1;
	}
	return n;
}

static unsigned ref_sb_of(uint32_t d)
{
	unsigned sb;
	if (d <= 8u)
		return 0u;
	for (sb = 1u; sb <= 28u; sb++) {
		uint64_t mx = ((uint64_t)1 << (sb + 4u)) - 8u;
		if ((uint64_t)d <= mx)
			return sb;
	}
	return 28u;
}

static uint32_t rng_state = 0x12345678u;
static uint32_t rng_next(void)
{
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state;
}

int main(void)
{
	uint32_t d, i;
	unsigned sb, low, rsb, rlow;
	uint32_t suf, rsuf, base, t;

	/* bitlen exhaustive 0..3M. */
	for (d = 0; d < 3000000u; d++) {
		unsigned g = lzmesh_u3_bitlen(d), w = ref_bitlen(d);
		if (g != w) {
			t_fail("bitlen-exhaust", d, g, w);
			return 1;
		}
	}
	t_pass("bitlen-exhaust-0-3M");

	/* bitlen random 1M. */
	for (i = 0; i < 1000000u; i++) {
		d = rng_next();
		unsigned g = lzmesh_u3_bitlen(d), w = ref_bitlen(d);
		if (g != w) {
			t_fail("bitlen-random", d, g, w);
			return 1;
		}
	}
	t_pass("bitlen-random-1M");

	/* sb_of exhaustive 0..3M. */
	for (d = 0; d < 3000000u; d++) {
		unsigned g = lzmesh_u3_sb_of(d), w = ref_sb_of(d);
		if (g != w) {
			t_fail("sb-exhaust", d, g, w);
			return 1;
		}
	}
	t_pass("sb-exhaust-0-3M");

	/* sb_of mx-boundary edges: every 2^(sb+4)-8 for sb 1..28, +/-8. */
	for (sb = 1u; sb <= 28u; sb++) {
		uint64_t mx = ((uint64_t)1 << (sb + 4u)) - 8u;
		int64_t k;
		for (k = -8; k <= 8; k++) {
			int64_t v = (int64_t)mx + k;
			if (v < 0 || v > (int64_t)0xFFFFFFFFu)
				continue;
			d = (uint32_t)v;
			unsigned g = lzmesh_u3_sb_of(d), w = ref_sb_of(d);
			if (g != w) {
				t_fail("sb-edge", d, g, w);
				return 1;
			}
		}
	}
	/* u32 top edges incl d+7 wrap zone. */
	for (i = 0; i < 32u; i++) {
		d = 0xFFFFFFFFu - i;
		unsigned g = lzmesh_u3_sb_of(d), w = ref_sb_of(d);
		if (g != w) {
			t_fail("sb-topedge", d, g, w);
			return 1;
		}
	}
	t_pass("sb-edges");

	/* sb_of random 1M. */
	for (i = 0; i < 1000000u; i++) {
		d = rng_next();
		unsigned g = lzmesh_u3_sb_of(d), w = ref_sb_of(d);
		if (g != w) {
			t_fail("sb-random", d, g, w);
			return 1;
		}
	}
	t_pass("sb-random-1M");

	/* dist_split fields vs reference over sb edges + tops + random. */
	for (sb = 1u; sb <= 28u; sb++) {
		uint64_t mx = ((uint64_t)1 << (sb + 4u)) - 8u;
		int64_t k;
		for (k = -2; k <= 2; k++) {
			int64_t v = (int64_t)mx + k;
			if (v < 0 || v > (int64_t)0xFFFFFFFFu)
				continue;
			d = (uint32_t)v;
			lzmesh_u4_dist_split(d, &sb, &low, &suf);
			rsb = ref_sb_of(d);
			base = (rsb >= 29u) ? 0u : (8u << rsb);
			t = d + 7u - base;
			rlow = (unsigned)(t & 7u);
			rsuf = t >> 3;
			if (sb != rsb || low != rlow || suf != rsuf) {
				g_fail++;
				printf("FAIL  dist-edge :: d=%u got=(%u,%u,%u) want=(%u,%u,%u)\n",
				       d, sb, low, suf, rsb, rlow, rsuf);
				return 1;
			}
		}
	}
	for (i = 0; i < 200000u; i++) {
		d = rng_next();
		lzmesh_u4_dist_split(d, &sb, &low, &suf);
		rsb = ref_sb_of(d);
		base = (rsb >= 29u) ? 0u : (8u << rsb);
		t = d + 7u - base;
		rlow = (unsigned)(t & 7u);
		rsuf = t >> 3;
		if (sb != rsb || low != rlow || suf != rsuf) {
			g_fail++;
			printf("FAIL  dist-random :: d=%u got=(%u,%u,%u) want=(%u,%u,%u)\n",
			       d, sb, low, suf, rsb, rlow, rsuf);
			return 1;
		}
	}
	t_pass("dist-split-edges-random");

	printf("p11-sb-clz: %d PASS %d FAIL\n", g_pass, g_fail);
	return g_fail != 0;
}
