/* SPDX-License-Identifier: 0BSD */
/*
 * bench.c — in-process throughput harness for the LZMESH port.
 *
 * Settles DECISIONS.md O5 as in-process timing (black-box port_cli timing
 * stays available via `time port_cli`, but process-spawn + stdio noise
 * dominates small inputs, so the recorded baseline is in-process).
 *
 * For each corpus file and each level (0/1/5/9, full-form 0xE00/0xE01/
 * 0xE05/0xE09 at the API boundary): times N encode reps and N decode reps
 * (decode input = that level's encoded output), verifies roundtrip bytes
 * and encode determinism every rep. Prints one TSV row per rep sample:
 *   file  level  op  in_bytes  out_bytes  ns
 * plus a human summary table on stderr. Exit nonzero on any failure.
 *
 * Build: cc -O2 -std=c11 -Wall -Wextra -I../include -o bench bench.c ../liblzmesh.a
 * Usage: ./bench [-n reps] [-l 0159] file...
 * (-l restricts levels, e.g. -l 0 for L0 only; default all four.)
 *
 * R11-LASTBLOCK: exact-cap twin (decode dest sized n, not n+64; 3 lines
 * differ). Last block runs room==ds here, exercising the EQ leg.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lzmesh.h"

static const int LEVELS[] = { 0xE00, 0xE01, 0xE05, 0xE09 };
static const char *LNAMES[] = { "0", "1", "5", "9" };

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static int read_file(const char *path, uint8_t **out, size_t *out_len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	size_t cap = 65536, len = 0, n;

	if (!f) {
		fprintf(stderr, "bench: cannot open %s\n", path);
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

static void bench_file(const char *path, const char *label, int reps,
		       unsigned lmask)
{
	uint8_t *src = NULL, *enc = NULL, *dec = NULL, *first = NULL;
	size_t n = 0, enc_cap, ds;
	size_t li, r;

	if (read_file(path, &src, &n) != 0 || n == 0) {
		fprintf(stderr, "bench: cannot read %s\n", path);
		failures++;
		free(src);
		return;
	}
	/* Unit-test convention is n+1024; bench keeps 64K slack so a level
	 * that expands incompressible tails can never truncate. */
	enc_cap = n + 65536;
	enc = malloc(enc_cap);
	dec = malloc(n);
	first = malloc(enc_cap);
	if (!enc || !dec || !first) {
		fprintf(stderr, "bench: oom for %s\n", path);
		failures++;
		goto out;
	}

	for (li = 0; li < 4; li++) {
		int level;
		if (!(lmask & (1u << li)))
			continue;
		level = LEVELS[li];
		void *escratch = NULL, *dscratch = NULL;
		size_t esc_size, dsc_size, eret0 = 0;
		uint64_t *ens = NULL, *dns = NULL;

		esc_size = lzmesh_encode_scratch_size(level);
		dsc_size = lzmesh_decode_scratch_size();
		if (esc_size == 0 || dsc_size == 0) {
			fprintf(stderr, "bench: %s L%s scratch=0\n", label, LNAMES[li]);
			failures++;
			continue;
		}
		escratch = malloc(esc_size);
		dscratch = malloc(dsc_size);
		ens = malloc((size_t)reps * sizeof(uint64_t));
		dns = malloc((size_t)reps * sizeof(uint64_t));
		if (!escratch || !dscratch || !ens || !dns) {
			fprintf(stderr, "bench: oom scratch %s L%s\n", label, LNAMES[li]);
			failures++;
			goto lvl_out;
		}

		/* Warmup + baseline bytes: also pins eret0 for determinism check. */
		eret0 = lzmesh_encode(enc, enc_cap, src, n, escratch, level);
		if (eret0 == 0 || eret0 > enc_cap) {
			fprintf(stderr, "bench: %s L%s encode failed (%lu)\n",
				label, LNAMES[li], (unsigned long)eret0);
			failures++;
			goto lvl_out;
		}
		memcpy(first, enc, eret0);
		ds = lzmesh_decoded_size(enc, eret0);
		if (ds != n) {
			fprintf(stderr, "bench: %s L%s decoded_size=%lu want %lu\n",
				label, LNAMES[li], (unsigned long)ds, (unsigned long)n);
			failures++;
			goto lvl_out;
		}
		if (lzmesh_decode(dec, n, enc, eret0, dscratch) != n ||
		    memcmp(dec, src, n) != 0) {
			fprintf(stderr, "bench: %s L%s roundtrip mismatch\n",
				label, LNAMES[li]);
			failures++;
			goto lvl_out;
		}

		for (r = 0; r < (size_t)reps; r++) {
			uint64_t t0 = now_ns();
			size_t er = lzmesh_encode(enc, enc_cap, src, n, escratch, level);
			ens[r] = now_ns() - t0;
			if (er != eret0 || memcmp(enc, first, eret0) != 0) {
				fprintf(stderr, "bench: %s L%s nondeterministic rep %lu\n",
					label, LNAMES[li], (unsigned long)r);
				failures++;
				goto lvl_out;
			}
			printf("%s\t%s\tenc\t%lu\t%lu\t%llu\n", label, LNAMES[li],
			       (unsigned long)n, (unsigned long)er,
			       (unsigned long long)ens[r]);
		}
		for (r = 0; r < (size_t)reps; r++) {
			uint64_t t0 = now_ns();
			size_t dr = lzmesh_decode(dec, n, first, eret0, dscratch);
			dns[r] = now_ns() - t0;
			if (dr != n) {
				fprintf(stderr, "bench: %s L%s decode rep %lu -> %lu\n",
					label, LNAMES[li], (unsigned long)r, (unsigned long)dr);
				failures++;
				goto lvl_out;
			}
			printf("%s\t%s\tdec\t%lu\t%lu\t%llu\n", label, LNAMES[li],
			       (unsigned long)eret0, (unsigned long)dr,
			       (unsigned long long)dns[r]);
		}
		if (memcmp(dec, src, n) != 0) {
			fprintf(stderr, "bench: %s L%s decode bytes mismatch\n",
				label, LNAMES[li]);
			failures++;
		}
lvl_out:
		free(escratch);
		free(dscratch);
		free(ens);
		free(dns);
	}
out:
	free(src);
	free(enc);
	free(dec);
	free(first);
}

int main(int argc, char **argv)
{
	int reps = 7, i, first = 1;
	unsigned lmask = 0xF;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			reps = atoi(argv[++i]);
			first = i + 1;
		} else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
			const char *s = argv[++i];
			lmask = 0;
			for (; *s; s++) {
				const char *p = strchr("0159", *s);
				if (p)
					lmask |= 1u << (p - "0159");
			}
			first = i + 1;
		} else {
			fprintf(stderr, "usage: %s [-n reps] [-l 0159] file...\n", argv[0]);
			return 2;
		}
	}
	if (reps < 1 || lmask == 0 || first >= argc) {
		fprintf(stderr, "usage: %s [-n reps] [-l 0159] file...\n", argv[0]);
		return 2;
	}
	for (i = first; i < argc; i++) {
		const char *label = strrchr(argv[i], '/');
		bench_file(argv[i], label ? label + 1 : argv[i], reps, lmask);
	}
	if (failures)
		fprintf(stderr, "bench: %d failures\n", failures);
	return failures ? 1 : 0;
}
