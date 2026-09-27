/* SPDX-License-Identifier: 0BSD */
/*
 * test_e00_vectors.c — Stage-6 unithuff (b): Apple E00 oracle-vector decode.
 *
 * Fixtures in vectors/ are black-box oracle bytes vendored from
 * tmp/oraclecache/e00.zip (see vectors/manifest.txt for the oracle
 * input_id + output-sha traceability). Every fixture input was verified
 * pure-zero (sha256(n zero bytes) == manifest cell input_sha), so the
 * exact expected plaintext is n zero bytes with no other dependency.
 *
 * raw/end vectors must decode exactly today; comp vectors are
 * XFAIL-with-reason while the port decoder COMP path is pending (a
 * nonzero-but-wrong comp decode is a hard FAIL: corrupt, not pending).
 * Exit 0 iff zero FAILs; XPASS stays green so COMP landing shows in the log.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

static int g_pass, g_fail, g_xfail, g_xpass;

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

static void t_xpass(const char *name, const char *detail)
{
	g_xpass++;
	printf("XPASS %s :: %s\n", name, detail);
}

static uint32_t fnv1a(const uint8_t *b, size_t n)
{
	uint32_t h = 0x811c9dc5u;
	size_t i;

	for (i = 0; i < n; i++) {
		h ^= b[i];
		h *= 0x01000193u;
	}
	return h;
}

struct vec {
	const char *file;	/* under vectors/ */
	size_t n;		/* expected plaintext: n zero bytes */
	size_t outlen;		/* expected fixture size */
	uint32_t fnv;		/* expected FNV-1a of fixture bytes */
	int is_comp;		/* 1 = comp block (COMP pending), 0 = raw/end */
	const char *oracle;	/* oracle input_id for traceability */
};

static const struct vec VECS[] = {
	{ "n000000.bin", 0, 1, 0x7a0b824e, 0, "s01-n000000-zeros-v1" },
	{ "n000001.bin", 1, 7, 0x94d3673f, 0, "s00-n000001-zeros-v1" },
	{ "n000008.bin", 8, 14, 0xad942248, 0, "s05-n000008-zeros-v1" },
	{ "n000021.bin", 21, 27, 0xb7d4f293, 0, "s04-n000021-zeros-v1" },
	{ "n000022.bin", 22, 23, 0x2182e858, 1, "s09-n000022-zeros-v1" },
	{ "n000032.bin", 32, 23, 0xa5170712, 1, "s00-n000032-zeros" },
	{ "n000100.bin", 100, 23, 0x9cb2effe, 1, "s00-n000100-zeros" },
	{ "n001000.bin", 1000, 27, 0x9bbd9690, 1, "s13-n001000-zeros" },
	{ "n004096.bin", 4096, 27, 0x8cc93f16, 1, "s02-n004096-zeros" },
	{ "n032768.bin", 32768, 53, 0xc35b728b, 1, "s09-n032768-zeros" },
};

static uint8_t *load_vec(const char *file, size_t *out_n, char *err,
			 size_t errcap)
{
	static const char *roots[] = { "tests/unit/vectors/", "vectors/" };
	char path[256];
	FILE *f;
	size_t i, n, got;
	uint8_t *b;

	for (i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
		snprintf(path, sizeof(path), "%s%s", roots[i], file);
		f = fopen(path, "rb");
		if (!f)
			continue;
		fseek(f, 0, SEEK_END);
		n = (size_t)ftell(f);
		fseek(f, 0, SEEK_SET);
		b = malloc(n ? n : 1);
		if (!b) {
			fclose(f);
			snprintf(err, errcap, "oom");
			return NULL;
		}
		got = fread(b, 1, n, f);
		fclose(f);
		if (got != n) {
			free(b);
			snprintf(err, errcap, "short read %s", path);
			return NULL;
		}
		*out_n = n;
		return b;
	}
	snprintf(err, errcap, "fixture %s not found (run via make unit)", file);
	return NULL;
}

static int all_zero(const uint8_t *b, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (b[i])
			return 0;
	return 1;
}

static void vec_one(const struct vec *v)
{
	char name[128], detail[256];
	uint8_t *blob, *dec;
	size_t blen, ds, dret;

	snprintf(name, sizeof(name), "e00 %s n=%lu", v->file,
		 (unsigned long)v->n);
	blob = load_vec(v->file, &blen, detail, sizeof(detail));
	if (!blob) {
		t_fail(name, detail);
		return;
	}
	if (blen != v->outlen || fnv1a(blob, blen) != v->fnv) {
		snprintf(detail, sizeof(detail),
			 "fixture integrity: size=%lu fnv=%08x, want %lu/%08x",
			 (unsigned long)blen, fnv1a(blob, blen),
			 (unsigned long)v->outlen, v->fnv);
		t_fail(name, detail);
		free(blob);
		return;
	}
	/* Framing walk first: reads framing only, decodes no entropy data. */
	ds = lzmesh_decoded_size(blob, blen);
	if (ds != v->n) {
		if (v->is_comp) {
			snprintf(detail, sizeof(detail),
				 "decoded_size=%lu want %lu; block-walk over comp framing pending",
				 (unsigned long)ds, (unsigned long)v->n);
			t_xfail(name, detail);
		} else {
			snprintf(detail, sizeof(detail),
				 "decoded_size=%lu want %lu", (unsigned long)ds,
				 (unsigned long)v->n);
			t_fail(name, detail);
		}
		free(blob);
		return;
	}
	dec = malloc((v->n + 64) ? (v->n + 64) : 1);
	if (!dec) {
		t_fail(name, "oom");
		free(blob);
		return;
	}
	dret = lzmesh_decode(dec, v->n + 64, blob, blen, NULL);
	if (dret == v->n && (v->n == 0 || all_zero(dec, v->n))) {
		if (v->is_comp)
			t_xpass(name, "comp decode exact; COMP landed");
		else
			t_pass(name);
	} else if (v->is_comp && dret == 0) {
		t_xfail(name, "decoder COMP pending (returns 0)");
	} else {
		snprintf(detail, sizeof(detail),
			 "decode returned %lu want %lu (oracle %s)",
			 (unsigned long)dret, (unsigned long)v->n, v->oracle);
		t_fail(name, detail);
	}
	free(dec);
	free(blob);
}

int main(void)
{
	size_t i;

	for (i = 0; i < sizeof(VECS) / sizeof(VECS[0]); i++)
		vec_one(&VECS[i]);
	printf("---\ne00-vectors: pass=%d fail=%d xfail=%d xpass=%d\n", g_pass,
	       g_fail, g_xfail, g_xpass);
	return g_fail ? 1 : 0;
}
