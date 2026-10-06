/* SPDX-License-Identifier: 0BSD */
/*
 * test_p5_wordload.c — P5-W3 word-load head-op edge + differential pins.
 *
 * P5-W3 replaced the byte loops in lzmesh_u2_load_n, lzmesh_u2_head_eq,
 * lzmesh_u12_load and lzmesh_u18_load with word-at-a-time loads
 * (lzmesh_wl_*). These pins assert:
 *  (a) value-exactness vs the base byte loops (copied verbatim below as
 *      ref_*) over n=0..8, unaligned offsets, PRNG + edge patterns;
 *  (b) no overrun reads: exact-malloc buffers (ASan tripwire) + a guard-
 *      page probe (mmap + PROT_NONE) with n bytes ending at the page edge
 *      for every n=0..8 — any fixed-8 load would fault.
 * (c) i5_heq boundary pins REMOVED (hotfix): S2d-i5m deleted the
 * lzmesh_i5_heq wrapper outright (sole callers were the O4 guard
 * blocks, also gone); no bounds-checked (src,size,cur,p,hd) wrapper
 * remains in live code — callers check EMPTY/bounds manually then
 * call lzmesh_u2_head_eq directly, which (a)/(b) already pin.
 * Includes src/lzmesh_enc.c for static access (dec.o needs no enc
 * symbols, so no archive duplicate). Exit 0 iff zero FAILs.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../../src/lzmesh_enc.c"

static int w3_pass, w3_fail;

static void w3_ok(const char *name)
{
	w3_pass++;
	printf("PASS  %s\n", name);
}

static void w3_bad(const char *name, const char *detail)
{
	w3_fail++;
	printf("FAIL  %s :: %s\n", name, detail);
}

/* Base byte loops, verbatim from f333e4ac (reference oracle). */
static uint64_t ref_load_n(const uint8_t *p, unsigned n)
{
	uint64_t w = 0;
	unsigned i;
	for (i = 0; i < n; i++)
		w |= (uint64_t)p[i] << (8u * i);
	return w;
}

static int ref_head_eq(const uint8_t *a, const uint8_t *b, unsigned n)
{
	unsigned i;
	for (i = 0; i < n; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

/* Pinned LCG (same stream every run). */
static uint64_t w3_rng = 0x243F6A8885A308D3ull;

static unsigned w3_next(unsigned m)
{
	w3_rng = w3_rng * 6364136223846793005ull + 1442695040888963407ull;
	return (unsigned)((w3_rng >> 33) % m);
}

/* (a) load differential: all n, unaligned offsets, PRNG + patterns. */
static void t_load_diff(void)
{
	static const uint8_t pats[4][16] = {
		{ 0 },
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
		{ 0x80, 0x01, 0x7f, 0xfe, 0x00, 0xff, 0x55, 0xaa,
		  0x33, 0xcc, 0x0f, 0xf0, 0x99, 0x66, 0x5a, 0xa5 }
	};
	uint8_t buf[64];
	unsigned n, off, pi, t;
	unsigned long checked = 0;
	char detail[128];
	for (t = 0; t < 300; t++) {
		unsigned i;
		for (i = 0; i < sizeof buf; i++)
			buf[i] = (uint8_t)w3_next(256);
		for (n = 0; n <= 8; n++) {
			for (off = 0; off < 9; off++) {
				uint64_t e = ref_load_n(buf + off, n);
				if (lzmesh_u2_load_n(buf + off, n) != e ||
				    lzmesh_u12_load(buf + off, n) != e ||
				    lzmesh_u18_load(buf + off, n) != e ||
				    lzmesh_wl_load_n(buf + off, n) != e) {
					snprintf(detail, sizeof detail,
					    "n=%u off=%u rep=%u", n, off, t);
					w3_bad("load-diff", detail);
					return;
				}
				checked++;
			}
		}
	}
	for (pi = 0; pi < 4; pi++) {
		for (n = 0; n <= 8; n++) {
			for (off = 0; off + n <= 16; off++) {
				uint64_t e = ref_load_n(pats[pi] + off, n);
				if (lzmesh_u2_load_n(pats[pi] + off, n) != e ||
				    lzmesh_u2_head_eq(pats[pi] + off,
					pats[pi] + off, n) != 1) {
					snprintf(detail, sizeof detail,
					    "pat=%u n=%u off=%u", pi, n, off);
					w3_bad("load-pat", detail);
					return;
				}
				checked++;
			}
		}
	}
	/* n=9 defensive clamp: low 8 bytes must equal ref(8). */
	for (off = 0; off < 9; off++) {
		if (lzmesh_wl_load_n(buf + off, 9u) !=
		    ref_load_n(buf + off, 8u)) {
			snprintf(detail, sizeof detail, "clamp off=%u", off);
			w3_bad("load-clamp9", detail);
			return;
		}
		checked++;
	}
	printf("note  load-diff checked=%lu\n", checked);
	w3_ok("load-diff");
}

/* (a2) head_eq differential: equal + every single-byte-mismatch site. */
static void t_heq_diff(void)
{
	uint8_t a[32], b[32];
	unsigned n, oa, ob, m, t;
	unsigned long checked = 0;
	char detail[128];
	for (t = 0; t < 300; t++) {
		unsigned i;
		for (i = 0; i < sizeof a; i++) {
			a[i] = (uint8_t)w3_next(256);
			b[i] = a[i];
		}
		for (n = 0; n <= 8; n++) {
			for (oa = 0; oa < 8; oa++) {
				for (ob = 0; ob < 8; ob++) {
					if (!!lzmesh_u2_head_eq(a + oa, b + ob,
					    n) != !!ref_head_eq(a + oa,
					    b + ob, n)) {
						snprintf(detail, sizeof detail,
						    "eq n=%u oa=%u ob=%u rep=%u",
						    n, oa, ob, t);
						w3_bad("heq-diff", detail);
						return;
					}
					checked++;
					for (m = 0; m < n; m++) {
						uint8_t save = (b + ob)[m];
						(b + ob)[m] ^= 0xff;
						if (!!lzmesh_u2_head_eq(a + oa,
						    b + ob, n) !=
						    !!ref_head_eq(a + oa, b + ob,
						    n)) {
							snprintf(detail,
							    sizeof detail,
							    "mis n=%u oa=%u ob=%u m=%u rep=%u",
							    n, oa, ob, m, t);
							w3_bad("heq-diff", detail);
							return;
						}
						(b + ob)[m] = save;
						checked++;
					}
				}
			}
		}
	}
	printf("note  heq-diff checked=%lu\n", checked);
	w3_ok("heq-diff");
}

/* (b1) ASan tripwire: exact-size mallocs; any read past n aborts. */
static void t_exact_alloc(void)
{
	unsigned n;
	char detail[64];
	for (n = 0; n <= 8; n++) {
		uint8_t *a = (uint8_t *)malloc(n ? n : 1);
		uint8_t *b = (uint8_t *)malloc(n ? n : 1);
		unsigned i;
		if (a == NULL || b == NULL) {
			w3_bad("exact-alloc", "oom");
			free(a);
			free(b);
			return;
		}
		for (i = 0; i < n; i++) {
			a[i] = (uint8_t)w3_next(256);
			b[i] = a[i];
		}
		if (lzmesh_wl_load_n(a, n) != ref_load_n(a, n) ||
		    lzmesh_u2_load_n(a, n) != ref_load_n(a, n) ||
		    !lzmesh_u2_head_eq(a, b, n)) {
			snprintf(detail, sizeof detail, "n=%u", n);
			w3_bad("exact-alloc", detail);
			free(a);
			free(b);
			return;
		}
		if (n > 0) {
			b[n - 1] ^= 0x01;
			if (lzmesh_u2_head_eq(a, b, n)) {
				snprintf(detail, sizeof detail, "mis n=%u",
				    n);
				w3_bad("exact-alloc", detail);
				free(a);
				free(b);
				return;
			}
		}
		free(a);
		free(b);
	}
	w3_ok("exact-alloc");
}

/* (b2) guard page: n bytes ending exactly at PROT_NONE. Any fixed-8
 * (or wider-than-n) load faults. Runs even without ASan. */
static void t_guard_page(void)
{
	long ps = sysconf(_SC_PAGESIZE);
	uint8_t *map;
	uint8_t *edge;
	unsigned n;
	char detail[64];
	if (ps < 16)
		ps = 4096;
	map = (uint8_t *)mmap(NULL, (size_t)ps * 2, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0);
	if (map == MAP_FAILED) {
		w3_bad("guard-page", "mmap failed");
		return;
	}
	if (mprotect(map + ps, (size_t)ps, PROT_NONE) != 0) {
		w3_bad("guard-page", "mprotect failed");
		munmap(map, (size_t)ps * 2);
		return;
	}
	/* second mapping for head_eq's b side (also page-edged) */
	edge = map + ps;
	for (n = 0; n <= 8; n++) {
		uint8_t *a = edge - n;
		uint8_t *b = edge - n; /* same edge; distinct check below */
		unsigned i;
		for (i = 0; i < n; i++)
			a[i] = (uint8_t)(0xA5 + i * 31);
		if (lzmesh_wl_load_n(a, n) != ref_load_n(a, n) ||
		    lzmesh_u2_load_n(a, n) != ref_load_n(a, n) ||
		    !lzmesh_u2_head_eq(a, b, n)) {
			snprintf(detail, sizeof detail, "n=%u", n);
			w3_bad("guard-page", detail);
			munmap(map, (size_t)ps * 2);
			return;
		}
		/* b side on a private copy edged in the same page */
		if (n > 0) {
			uint8_t *c = edge - 16 - n;
			for (i = 0; i < n; i++)
				c[i] = a[i];
			if (!lzmesh_u2_head_eq(a, c, n)) {
				snprintf(detail, sizeof detail, "ab n=%u",
				    n);
				w3_bad("guard-page", detail);
				munmap(map, (size_t)ps * 2);
				return;
			}
			c[0] ^= 0x01;
			if (n > 0 && lzmesh_u2_head_eq(a, c, n)) {
				snprintf(detail, sizeof detail, "mab n=%u",
				    n);
				w3_bad("guard-page", detail);
				munmap(map, (size_t)ps * 2);
				return;
			}
		}
	}
	munmap(map, (size_t)ps * 2);
	w3_ok("guard-page");
}

int main(void)
{
	t_load_diff();
	t_heq_diff();
	t_exact_alloc();
	t_guard_page();
	printf("wordload: pass=%d fail=%d\n", w3_pass, w3_fail);
	return w3_fail ? 1 : 0;
}
