/* SPDX-License-Identifier: 0BSD */
/*
 * test_fetch2_kraft.c — Kraft-edge table-build pins for the R4-FETCH2
 * dead-check deletion (dec2a/dec2b per-sym len checks removed).
 *
 * The vehicle's soundness case: lz_u6h_build OK-paths overwrite EVERY
 * tab cell with len in 1..maxlen, so the deleted check (len==0 or
 * len>maxlen) cannot fire. These pins assert BOTH the verdict edge
 * (Kraft 1023/1024/1025, single-sym legs, over/under-complete, lens
 * guards) AND the vehicle post-condition: on every OK build, all
 * tab_n cells hold len 1..maxlen. Includes src/lzmesh.c for
 * static access (test_p6_mcopy.c precedent). Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/lzmesh.c"

static int g_pass, g_fail;

static void t_pass(const char *name)
{
	g_pass++;
	printf("PASS  fetch2-kraft %s\n", name);
}

static void t_fail(const char *name, const char *detail)
{
	g_fail++;
	printf("FAIL  fetch2-kraft %s :: %s\n", name, detail);
}

/* Run one build vector: expect verdict, and on OK verify every cell. */
static void t_build(const uint8_t *lens, uint32_t nsym, uint32_t maxlen,
		    int expect_ok, const char *name)
{
	uint16_t tab[1024];
	uint32_t tab_n = (uint32_t)1 << maxlen;
	uint32_t i;
	int rc;

	memset(tab, 0xA5, sizeof(tab));
	rc = lz_u6h_build(lens, nsym, maxlen, tab, tab_n);
	if (expect_ok) {
		char det[96];
		if (rc != LZ_U3_OK) {
			t_fail(name, "expected OK, got FAIL");
			return;
		}
		for (i = 0; i < tab_n; i++) {
			uint32_t len = (uint32_t)tab[i] >> 8;
			if (len == (uint32_t)0 || len > maxlen) {
				snprintf(det, sizeof(det),
					 "cell %u len %u outside 1..%u",
					 i, len, maxlen);
				t_fail(name, det);
				return;
			}
		}
		t_pass(name);
	} else {
		if (rc == LZ_U3_OK)
			t_fail(name, "expected FAIL, got OK");
		else
			t_pass(name);
	}
}

int main(void)
{
	/* Main shape (nsym 256, maxlen 10, tab 1024). */
	static uint8_t z256[256];
	static uint8_t uni8[256];
	static uint8_t k1023[256];
	static uint8_t k1024b[256];
	static uint8_t k1025[256];
	static uint8_t single1[256];
	static uint8_t single10[256];
	static uint8_t single2[256];
	static uint8_t over1[256];
	static uint8_t badlen[256];
	uint32_t i;

	/* Meta shape (nsym 11, maxlen 5, tab 32). */
	static uint8_t m_ok[11];
	static uint8_t m_single1[11];
	static uint8_t m_2222[11];
	static uint8_t m_992[11];
	static uint8_t m_over[11];
	static uint8_t m_badlen[11];

	for (i = 0; i < (uint32_t)256; i++)
		uni8[i] = (uint8_t)8; /* 256 x 2^2 = 1024 */
	/* 1023 = 512+256+128+64+32+16+8+4+2+1 */
	for (i = 0; i < (uint32_t)10; i++)
		k1023[i] = (uint8_t)(i + 1);
	/* 1024 = ...+2+2 (last pair len 9) */
	for (i = 0; i < (uint32_t)9; i++)
		k1024b[i] = (uint8_t)(i + 1);
	k1024b[9] = (uint8_t)9;
	/* 1025 = 512+512+1 */
	k1025[0] = (uint8_t)1;
	k1025[1] = (uint8_t)1;
	k1025[2] = (uint8_t)10;
	single1[0] = (uint8_t)1;
	single10[0] = (uint8_t)10;
	single2[0] = (uint8_t)2;
	over1[0] = (uint8_t)1;
	over1[1] = (uint8_t)1;
	over1[2] = (uint8_t)1; /* 1536 */
	memcpy(badlen, uni8, sizeof(badlen));
	badlen[7] = (uint8_t)11; /* > maxlen */

	t_build(uni8, 256, 10, 1, "main uniform-8 complete");
	t_build(k1023, 256, 10, 0, "main kraft-1023 under-by-1");
	t_build(k1024b, 256, 10, 1, "main kraft-1024 skewed");
	t_build(k1025, 256, 10, 0, "main kraft-1025 over-by-1");
	t_build(single1, 256, 10, 1, "main single-len1 fills-all");
	t_build(single10, 256, 10, 0, "main single-len10 reject");
	t_build(single2, 256, 10, 0, "main single-len2 reject");
	t_build(z256, 256, 10, 0, "main all-zero reject");
	t_build(over1, 256, 10, 0, "main triple-len1 oversubscribed");
	t_build(badlen, 256, 10, 0, "main lens-above-maxlen reject");

	/* Meta: {1,2,3,4,5,5} = 1024; {1,2,3,4,5} = 992 (closest
	 * under: odd sums unachievable at maxlen 5, min addend 32). */
	m_ok[0] = 1; m_ok[1] = 2; m_ok[2] = 3;
	m_ok[3] = 4; m_ok[4] = 5; m_ok[5] = 5;
	m_single1[0] = 1;
	m_2222[0] = 2; m_2222[1] = 2; m_2222[2] = 2; m_2222[3] = 2;
	m_992[0] = 1; m_992[1] = 2; m_992[2] = 3; m_992[3] = 4; m_992[4] = 5;
	m_over[0] = 1; m_over[1] = 1; m_over[2] = 1;
	m_badlen[0] = 6; /* > maxlen 5 */
	t_build(m_ok, 11, 5, 1, "meta 123455 complete");
	t_build(m_single1, 11, 5, 1, "meta single-len1 fills-all");
	t_build(m_2222, 11, 5, 1, "meta 2222 complete");
	t_build(m_992, 11, 5, 0, "meta kraft-992 under");
	t_build(m_over, 11, 5, 0, "meta triple-len1 oversubscribed");
	t_build(m_badlen, 11, 5, 0, "meta lens-above-maxlen reject");

	/* API guards. */
	{
		uint16_t tab[1024];
		if (lz_u6h_build(NULL, 256, 10, tab, 1024) == LZ_U3_OK)
			t_fail("null-lens", "expected FAIL");
		else
			t_pass("null-lens");
		if (lz_u6h_build(uni8, 256, 10, NULL, 1024) == LZ_U3_OK)
			t_fail("null-tab", "expected FAIL");
		else
			t_pass("null-tab");
		if (lz_u6h_build(uni8, 0, 10, tab, 1024) == LZ_U3_OK)
			t_fail("nsym-0", "expected FAIL");
		else
			t_pass("nsym-0");
		if (lz_u6h_build(uni8, 256, 10, tab, 512) == LZ_U3_OK)
			t_fail("tabn-mismatch", "expected FAIL");
		else
			t_pass("tabn-mismatch");
	}

	printf("fetch2-kraft: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
