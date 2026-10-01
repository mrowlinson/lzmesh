/* SPDX-License-Identifier: 0BSD */
/*
 * abench.c — in-process throughput harness for the Apple oracle (libcompression).
 *
 * Same CLI + TSV schema as bench.c so run_gated.sh / cmp.py / matrix_gated.sh
 * work unchanged:
 *   file  level  op  in_bytes  out_bytes  ns        (stdout, one row per rep)
 *   ./abench [-n reps] [-l 0159] file...
 *
 * For each corpus file and each level (0/1/5/9 -> 0xE00/0xE01/0xE05/0xE09):
 * one untimed warmup encode (pins baseline bytes), N timed in-process
 * compression_encode_buffer reps (bytes must match baseline every rep),
 * then N timed compression_decode_buffer reps over the baseline bytes
 * (dest sized input-len + 64, exactly like bench.c; size checked every
 * rep, bytes verified once). Mirrors bench.c order and exit codes
 * (0 ok, 1 correctness failure, 2 usage).
 *
 * This is what oracle-bench.py is NOT: samples here are CLOCK_MONOTONIC
 * around the codec call inside one process (dlopen + scratch alloc stay
 * outside timing), so port-vs-Apple gaps measured with bench.c vs abench.c
 * are apples-to-apples. oracle-bench.py stays for pipe-floor reference.
 *
 * No codec logic, no probing, no guessing — pure API plumbing, like the
 * battery oracle_probe. Decode takes no DECODE_SIZE env: the harness knows
 * the input length the same way bench.c does.
 *
 * Build: cc -O2 -std=c11 -Wall -Wextra -o abench abench.c -ldl
 * Env:
 *   ORACLE_LIB  libcompression build under test
 *               (default: /usr/lib/libcompression.dylib)
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const int LEVELS[] = { 0xE00, 0xE01, 0xE05, 0xE09 };
static const char *LNAMES[] = { "0", "1", "5", "9" };

typedef size_t (*encode_fn)(uint8_t *, size_t, const uint8_t *, size_t,
			    void *, int);
typedef size_t (*decode_fn)(uint8_t *, size_t, const uint8_t *, size_t,
			    void *, int);
typedef size_t (*scratch_fn)(int);
/* NOTE: Apple's compression_decode_buffer takes 6 args (incl. algorithm),
 * unlike lzmesh_decode's 5 — decode_fn above carries the int for Apple. */

static encode_fn oenc;
static decode_fn odec;
static scratch_fn oesz, odsz;

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
		fprintf(stderr, "abench: cannot open %s\n", path);
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
	size_t n = 0, enc_cap;
	size_t li, r;

	if (read_file(path, &src, &n) != 0 || n == 0) {
		fprintf(stderr, "abench: cannot read %s\n", path);
		failures++;
		free(src);
		return;
	}
	/* Same slack convention as bench.c: a level that expands
	 * incompressible tails can never truncate. */
	enc_cap = n + 65536;
	enc = malloc(enc_cap);
	dec = malloc(n + 64);
	first = malloc(enc_cap);
	if (!enc || !dec || !first) {
		fprintf(stderr, "abench: oom for %s\n", path);
		failures++;
		goto out;
	}

	for (li = 0; li < 4; li++) {
		int level;
		void *escratch = NULL, *dscratch = NULL;
		size_t esc_size, dsc_size, eret0 = 0;
		uint64_t *ens = NULL, *dns = NULL;

		if (!(lmask & (1u << li)))
			continue;
		level = LEVELS[li];

		esc_size = oesz(level);
		dsc_size = odsz(level);
		if (esc_size == 0 || dsc_size == 0) {
			fprintf(stderr, "abench: %s L%s scratch=0\n", label, LNAMES[li]);
			failures++;
			continue;
		}
		escratch = malloc(esc_size);
		dscratch = malloc(dsc_size);
		ens = malloc((size_t)reps * sizeof(uint64_t));
		dns = malloc((size_t)reps * sizeof(uint64_t));
		if (!escratch || !dscratch || !ens || !dns) {
			fprintf(stderr, "abench: oom scratch %s L%s\n", label, LNAMES[li]);
			failures++;
			goto lvl_out;
		}

		/* Warmup + baseline bytes: also pins eret0 for determinism check. */
		eret0 = oenc(enc, enc_cap, src, n, escratch, level);
		if (eret0 == 0 || eret0 > enc_cap) {
			fprintf(stderr, "abench: %s L%s encode failed (%lu)\n",
				label, LNAMES[li], (unsigned long)eret0);
			failures++;
			goto lvl_out;
		}
		memcpy(first, enc, eret0);
		/* Decode dest is input-len + 64, exactly like bench.c: the
		 * harness knows n, so no decoded-size walker is needed and
		 * no truncation can hide (dr != n fails below). */
		if (odec(dec, n + 64, enc, eret0, dscratch, level) != n ||
		    memcmp(dec, src, n) != 0) {
			fprintf(stderr, "abench: %s L%s roundtrip mismatch\n",
				label, LNAMES[li]);
			failures++;
			goto lvl_out;
		}

		for (r = 0; r < (size_t)reps; r++) {
			uint64_t t0 = now_ns();
			size_t er = oenc(enc, enc_cap, src, n, escratch, level);
			ens[r] = now_ns() - t0;
			if (er != eret0 || memcmp(enc, first, eret0) != 0) {
				fprintf(stderr, "abench: %s L%s nondeterministic rep %lu\n",
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
			size_t dr = odec(dec, n + 64, first, eret0, dscratch, level);
			dns[r] = now_ns() - t0;
			if (dr != n) {
				fprintf(stderr, "abench: %s L%s decode rep %lu -> %lu\n",
					label, LNAMES[li], (unsigned long)r, (unsigned long)dr);
				failures++;
				goto lvl_out;
			}
			printf("%s\t%s\tdec\t%lu\t%lu\t%llu\n", label, LNAMES[li],
			       (unsigned long)eret0, (unsigned long)dr,
			       (unsigned long long)dns[r]);
		}
		if (memcmp(dec, src, n) != 0) {
			fprintf(stderr, "abench: %s L%s decode bytes mismatch\n",
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
	const char *lib;
	void *h;

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
	lib = getenv("ORACLE_LIB");
	if (!lib)
		lib = "/usr/lib/libcompression.dylib";
	h = dlopen(lib, RTLD_NOW);
	if (!h) {
		fprintf(stderr, "abench: dlopen %s failed\n", lib);
		return 1;
	}
	oenc = (encode_fn)dlsym(h, "compression_encode_buffer");
	odec = (decode_fn)dlsym(h, "compression_decode_buffer");
	oesz = (scratch_fn)dlsym(h, "compression_encode_scratch_buffer_size");
	odsz = (scratch_fn)dlsym(h, "compression_decode_scratch_buffer_size");
	if (!oenc || !odec || !oesz || !odsz) {
		fprintf(stderr, "abench: dlsym failed on %s\n", lib);
		return 1;
	}
	for (i = first; i < argc; i++) {
		const char *label = strrchr(argv[i], '/');
		bench_file(argv[i], label ? label + 1 : argv[i], reps, lmask);
	}
	if (failures)
		fprintf(stderr, "abench: %d failures\n", failures);
	return failures ? 1 : 0;
}
