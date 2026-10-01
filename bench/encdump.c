/* SPDX-License-Identifier: 0BSD */
/*
 * encdump.c — in-process port-vs-Apple encoded-bytes comparator.
 *
 * For each corpus file and each level (0/1/5/9 -> 0xE00/0xE01/0xE05/0xE09):
 * encodes in-process with the port (liblzmesh.a) and with Apple
 * (dlopen libcompression, ORACLE_LIB env), compares encoded bytes, and
 * cross-decodes both ways (port-decode(apple-bytes), apple-decode(port-bytes)).
 * One TSV row per (file, level) on stdout:
 *   file  level  in_bytes  port_bytes  apple_bytes  match  xcode  port_fnv  apple_fnv
 * match: IDENT | DIV@<first-diff-offset> | ENC-FAIL(p/rc a/rc)
 * xcode: X-OK | X-FAIL(port-dec-rc/apple-dec-rc, byte-ok flags)
 * fnv: FNV-1a 64 of the encoded bytes (fingerprint only, not a claim).
 * Exit: 0 all encodes/decodes valid (DIV is data, still exit 0),
 *       1 any encode/decode failure, 2 usage.
 *
 * Build: cc -O2 -std=c11 -Wall -Wextra -I../include -o encdump encdump.c ../liblzmesh.a -ldl
 * Usage: ./encdump [-l 0159] file...
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

static const int LEVELS[] = { 0xE00, 0xE01, 0xE05, 0xE09 };
static const char *LNAMES[] = { "0", "1", "5", "9" };

typedef size_t (*encode_fn)(uint8_t *, size_t, const uint8_t *, size_t,
			    void *, int);
typedef size_t (*decode_fn)(uint8_t *, size_t, const uint8_t *, size_t,
			    void *, int);
typedef size_t (*scratch_fn)(int);

static encode_fn oenc;
static decode_fn odec;
static scratch_fn oesz, odsz;

static uint64_t fnv1a(const uint8_t *b, size_t n)
{
	uint64_t h = 14695981039346656037u;
	size_t i;
	for (i = 0; i < n; i++) {
		h ^= b[i];
		h *= 1099511628211u;
	}
	return h;
}

static int read_file(const char *path, uint8_t **out, size_t *out_len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	size_t cap = 65536, len = 0, n;

	if (!f) {
		fprintf(stderr, "encdump: cannot open %s\n", path);
		return -1;
	}
	buf = malloc(cap);
	if (!buf) {
		fclose(f);
		return -1;
	}
	for (;;) {
		if (len == cap) {
			uint8_t *nb = realloc(buf, cap * 2);
			if (!nb) {
				free(buf);
				fclose(f);
				return -1;
			}
			buf = nb;
			cap *= 2;
		}
		n = fread(buf + len, 1, cap - len, f);
		len += n;
		if (n == 0)
			break;
	}
	fclose(f);
	*out = buf;
	*out_len = len;
	return 0;
}

static int failures = 0;

static void cmp_file(const char *path, const char *label, unsigned lmask)
{
	uint8_t *src = NULL, *pe = NULL, *ae = NULL, *pd = NULL, *ad = NULL;
	size_t n = 0, cap;
	size_t li;

	if (read_file(path, &src, &n) != 0 || n == 0) {
		fprintf(stderr, "encdump: cannot read %s\n", path);
		failures++;
		free(src);
		return;
	}
	cap = n + 65536;
	pe = malloc(cap);
	ae = malloc(cap);
	pd = malloc(n + 64);
	ad = malloc(n + 64);
	if (!pe || !ae || !pd || !ad) {
		fprintf(stderr, "encdump: oom for %s\n", path);
		failures++;
		goto out;
	}

	for (li = 0; li < 4; li++) {
		int level;
		void *psc = NULL, *osc = NULL, *pdsc = NULL, *odsc = NULL;
		size_t pss, oss, pdss, odss, pl = 0, al = 0;
		char match[64], xcode[64];

		if (!(lmask & (1u << li)))
			continue;
		level = LEVELS[li];
		snprintf(match, sizeof match, "?");
		snprintf(xcode, sizeof xcode, "?");

		pss = lzmesh_encode_scratch_size(level);
		oss = oesz(level);
		pdss = lzmesh_decode_scratch_size();
		odss = odsz(level);
		if (pss == 0 || oss == 0 || pdss == 0 || odss == 0) {
			fprintf(stderr, "encdump: %s L%s scratch=0\n", label, LNAMES[li]);
			failures++;
			continue;
		}
		psc = malloc(pss);
		osc = malloc(oss);
		pdsc = malloc(pdss);
		odsc = malloc(odss);
		if (!psc || !osc || !pdsc || !odsc) {
			fprintf(stderr, "encdump: oom scratch %s L%s\n", label, LNAMES[li]);
			failures++;
			goto lvl_out;
		}

		pl = lzmesh_encode(pe, cap, src, n, psc, level);
		al = oenc(ae, cap, src, n, osc, level);
		if (pl == 0 || pl > cap || al == 0 || al > cap) {
			snprintf(match, sizeof match, "ENC-FAIL(p=%lu a=%lu)",
				 (unsigned long)pl, (unsigned long)al);
			snprintf(xcode, sizeof xcode, "X-SKIP");
			failures++;
		} else {
			size_t k, first = 0;
			int div = 0;
			size_t pdr, adr;
			int pok, aok;

			if (pl != al) {
				div = 1;
				first = pl < al ? pl : al;
			} else {
				for (k = 0; k < pl; k++) {
					if (pe[k] != ae[k]) {
						div = 1;
						first = k;
						break;
					}
				}
			}
			if (div)
				snprintf(match, sizeof match, "DIV@%lu", (unsigned long)first);
			else
				snprintf(match, sizeof match, "IDENT");
			/* Cross-decode both ways. */
			pdr = lzmesh_decode(pd, n + 64, ae, al, pdsc);
			adr = odec(ad, n + 64, pe, pl, odsc, level);
			pok = (pdr == n && memcmp(pd, src, n) == 0);
			aok = (adr == n && memcmp(ad, src, n) == 0);
			if (pok && aok)
				snprintf(xcode, sizeof xcode, "X-OK");
			else
				snprintf(xcode, sizeof xcode, "X-FAIL(p=%d a=%d)",
					 pok, aok);
			if (!pok || !aok)
				failures++;
		}
		printf("%s\t%s\t%lu\t%lu\t%lu\t%s\t%s\t%016llx\t%016llx\n",
		       label, LNAMES[li], (unsigned long)n,
		       (unsigned long)pl, (unsigned long)al,
		       match, xcode,
		       (unsigned long long)fnv1a(pe, pl),
		       (unsigned long long)fnv1a(ae, al));
lvl_out:
		free(psc);
		free(osc);
		free(pdsc);
		free(odsc);
	}
out:
	free(src);
	free(pe);
	free(ae);
	free(pd);
	free(ad);
}

int main(int argc, char **argv)
{
	int i, first = 1;
	unsigned lmask = 0xF;
	const char *lib;
	void *h;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
			const char *s = argv[++i];
			lmask = 0;
			for (; *s; s++) {
				const char *p = strchr("0159", *s);
				if (p)
					lmask |= 1u << (p - "0159");
			}
			first = i + 1;
		} else {
			fprintf(stderr, "usage: %s [-l 0159] file...\n", argv[0]);
			return 2;
		}
	}
	if (lmask == 0 || first >= argc) {
		fprintf(stderr, "usage: %s [-l 0159] file...\n", argv[0]);
		return 2;
	}
	lib = getenv("ORACLE_LIB");
	if (!lib)
		lib = "/usr/lib/libcompression.dylib";
	h = dlopen(lib, RTLD_NOW);
	if (!h) {
		fprintf(stderr, "encdump: dlopen %s failed\n", lib);
		return 1;
	}
	oenc = (encode_fn)dlsym(h, "compression_encode_buffer");
	odec = (decode_fn)dlsym(h, "compression_decode_buffer");
	oesz = (scratch_fn)dlsym(h, "compression_encode_scratch_buffer_size");
	odsz = (scratch_fn)dlsym(h, "compression_decode_scratch_buffer_size");
	if (!oenc || !odec || !oesz || !odsz) {
		fprintf(stderr, "encdump: dlsym failed on %s\n", lib);
		return 1;
	}
	for (i = first; i < argc; i++) {
		const char *label = strrchr(argv[i], '/');
		cmp_file(argv[i], label ? label + 1 : argv[i], lmask);
	}
	if (failures)
		fprintf(stderr, "encdump: %d failures\n", failures);
	return failures ? 1 : 0;
}
