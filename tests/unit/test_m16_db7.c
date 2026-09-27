/* SPDX-License-Identifier: 0BSD */
/*
 * test_m16_db7.c — M16 D-B7 scratch-ceiling pins (u6j, F-ADJUD R-D-B7).
 *
 * SPEC-v2: S4.2 (buffers 32B-rounded + 2576 tables, order
 * u32dist,dsyms,len,lit,tok; T62912 ACCEPT / T62913 REJECT), S4.1.b/
 * Q18 (first-block litc>=1), S1.8/Q9 (sizer framing-only: ceiling is
 * decoder-only, so walk-ok + decode-0 proves the split), App C
 * (rejects leave dst untouched). App-F R-D-B7 T-PIN (PROVEN id+sum,
 * PROBABLE template): T = tok_count; T-template = first-block COMP,
 * litc>=1, lenc=0, dc=0, tokc=T, rep-only 0x00 (ml2), output 2T+1
 * all-A; binding sum = 2576 + 32 (lit 1B->round32) + round32(tok);
 * T62912 -> 65520 ACCEPT; T62913 -> 62944+32+2576 = 65552 REJECT
 * (SIGNED b.gt); 0->0 CONFIRMED. R-pair (62880/62881) already pinned
 * by test_comp_roundtrip L0cap; multiblock >64K ACCEPT is RAW-path
 * (test_mtf chains) since the gate is per-COMP-block.
 *
 * Forges: lit 1B 'A' RAW, tok T x 0x00 RAW, bo=fo=10+T (empty lanes,
 * no Huffman), modes=0, counts (T,0,1,0), ds=2T+1. T10 -> 21B sanity
 * (F: T10->21), T62912 -> 125825B exact, T62913 -> sizer ok +
 * decode 0 + dst untouched. Public API only. Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

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

/* Build a T-template COMP block; returns total bytes (0 on oom). */
static size_t forge_t(uint8_t *f, uint32_t t)
{
	uint32_t bo = 10 + t;
	uint32_t ds = 2 * t + 1;
	uint32_t i;

	f[0] = 0x01;
	f[1] = (uint8_t)ds;
	f[2] = (uint8_t)(ds >> 8);
	f[3] = (uint8_t)(ds >> 16);
	f[4] = (uint8_t)(ds >> 24);
	f[5] = (uint8_t)bo;
	f[6] = (uint8_t)(bo >> 8);
	f[7] = (uint8_t)bo;
	f[8] = (uint8_t)(bo >> 8);
	f[9] = 0x41;
	for (i = 0; i < t; i++)
		f[10 + i] = 0x00;
	f[bo] = 0x00;
	f[bo + 1] = 0x00;
	f[bo + 2] = (uint8_t)t;
	f[bo + 3] = (uint8_t)(t >> 8);
	f[bo + 4] = 0x00;
	f[bo + 5] = 0x00;
	f[bo + 6] = 0x01;
	f[bo + 7] = 0x00;
	f[bo + 8] = 0x00;
	f[bo + 9] = 0x00;
	f[bo + 10] = 0xFF;
	return (size_t)bo + 11;
}

static void accept_one(uint32_t t)
{
	char name[64], detail[160];
	uint32_t ds = 2 * t + 1;
	size_t slen = (size_t)10 + t + 11;
	uint8_t *f = malloc(slen);
	uint8_t *dec = malloc((size_t)ds + 64);
	size_t dsr, dr, i;
	int ok;

	snprintf(name, sizeof(name), "db7 T%lu accept", (unsigned long)t);
	if (!f || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	if (forge_t(f, t) != slen) {
		t_fail(name, "forge length");
		goto out;
	}
	dsr = lzmesh_decoded_size(f, slen);
	dr = lzmesh_decode(dec, (size_t)ds + 64, f, slen, NULL);
	ok = (dsr == ds && dr == ds);
	for (i = 0; i < ds && ok; i++)
		if (dec[i] != 0x41)
			ok = 0;
	if (!ok) {
		snprintf(detail, sizeof(detail), "sizer=%lu dec=%lu want %lu",
			 (unsigned long)dsr, (unsigned long)dr,
			 (unsigned long)ds);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(f);
	free(dec);
}

static void reject_one(uint32_t t)
{
	char name[64], detail[160];
	uint32_t ds = 2 * t + 1;
	size_t slen = (size_t)10 + t + 11;
	uint8_t *f = malloc(slen);
	uint8_t *dec = malloc((size_t)ds + 64);
	size_t dsr, dr, i;
	int clean;

	snprintf(name, sizeof(name), "db7 T%lu reject", (unsigned long)t);
	if (!f || !dec) {
		t_fail(name, "oom");
		goto out;
	}
	forge_t(f, t);
	memset(dec, 0xA5, (size_t)ds + 64);
	dsr = lzmesh_decoded_size(f, slen);
	dr = lzmesh_decode(dec, (size_t)ds + 64, f, slen, NULL);
	clean = 1;
	for (i = 0; i < (size_t)ds + 64 && clean; i++)
		if (dec[i] != 0xA5)
			clean = 0;
	if (dsr != ds || dr != 0 || !clean) {
		snprintf(detail, sizeof(detail),
			 "sizer=%lu dec=%lu dst-clean=%d, want %lu/0/1",
			 (unsigned long)dsr, (unsigned long)dr, clean,
			 (unsigned long)ds);
		t_fail(name, detail);
		goto out;
	}
	t_pass(name);
out:
	free(f);
	free(dec);
}

int main(void)
{
	accept_one(10);
	accept_one(62912);
	reject_one(62913);
	printf("---\nm16-db7: pass=%d fail=%d\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
