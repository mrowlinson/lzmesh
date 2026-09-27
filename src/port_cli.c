/* SPDX-License-Identifier: 0BSD */
/*
 * port_cli.c — battery CLI plumbing for the standalone clean-room LZMESH port.
 *
 * Contract (tests/battery/README.md):
 *   port_cli enc <selector-hex>   stdin: raw bytes      stdout: encoded bytes
 *   port_cli dec <selector-hex>   stdin: encoded bytes  stdout: decoded bytes
 * Exit 0 on success, 10 when the codec returns 0. No chatter on either
 * stream. Decode sizes its destination at DECODE_SIZE when set (exactly
 * like the oracle harness's fixed buffer), else at the framing-walk size,
 * so giant-ds mutants yield an env-capped prefix, never a walk-sized
 * gigabyte buffer. Scratch buffers are malloc'd via lzmesh_*_scratch_size.
 */

#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzmesh.h"

/* Read all of f into a malloc'd buffer. Returns 0 on success, -1 on failure. */
static int read_all(FILE *f, uint8_t **out, size_t *out_len)
{
	size_t cap = 8192;
	size_t len = 0;
	uint8_t *buf = (uint8_t *)malloc(cap);
	size_t n;

	if (buf == NULL)
		return -1;
	for (;;) {
		if (len == cap) {
			size_t ncap = cap * 2 + 1024;
			uint8_t *nbuf;
			if (ncap <= cap) { /* overflow guard */
				free(buf);
				return -1;
			}
			nbuf = (uint8_t *)realloc(buf, ncap);
			if (nbuf == NULL) {
				free(buf);
				return -1;
			}
			buf = nbuf;
			cap = ncap;
		}
		n = fread(buf + len, 1, cap - len, f);
		len += n;
		if (n == 0) {
			if (ferror(f)) {
				free(buf);
				return -1;
			}
			break; /* EOF */
		}
	}
	*out = buf;
	*out_len = len;
	return 0;
}

/* Parse selector hex (e.g. "e05"); forwarded as a full-form int with no
 * masking (R-E-B1: boundary matches FULL 0xE00-form only; the codec
 * rejects anything outside {0xE00,0xE01,0xE05,0xE09}). -1 on failure. */
static int parse_level(const char *s, int *level)
{
	char *end = NULL;
	unsigned long v;

	if (s == NULL || *s == '\0')
		return -1;
	v = strtoul(s, &end, 16);
	if (end == NULL || *end != '\0')
		return -1;
	if (v > (unsigned long)INT_MAX)
		return -1;
	*level = (int)v;
	return 0;
}

static int do_enc(const char *selector, const uint8_t *src, size_t src_len)
{
	int level;
	size_t scratch_sz;
	void *scratch = NULL;
	size_t cap;
	uint8_t *dst = NULL;
	size_t n = 0;
	int attempt;

	if (parse_level(selector, &level) != 0)
		return 10;
	scratch_sz = lzmesh_encode_scratch_size(level);
	if (scratch_sz == 0)
		scratch_sz = 1;
	scratch = malloc(scratch_sz);
	if (scratch == NULL)
		return 10;

	/* Generous first capacity; grow-and-retry since 0 is ambiguous
	 * (failure vs. too-small destination). Encode is deterministic,
	 * so retries yield identical bytes. */
	cap = src_len + (src_len >> 3) + 4096;
	if (cap < 4096)
		cap = 4096;
	for (attempt = 0; attempt < 3; attempt++) {
		uint8_t *ndst = (uint8_t *)realloc(dst, cap);
		size_t ncap;
		if (ndst == NULL) {
			free(dst);
			free(scratch);
			return 10;
		}
		dst = ndst;
		n = lzmesh_encode(dst, cap, src, src_len, scratch, level);
		if (n > 0)
			break;
		ncap = cap * 2 + 4096;
		if (ncap <= cap)
			break;
		cap = ncap;
	}
	free(scratch);
	if (n == 0) {
		free(dst);
		return 10;
	}
	if (fwrite(dst, 1, n, stdout) != n) {
		free(dst);
		return 10;
	}
	free(dst);
	return 0;
}

static int do_dec(const uint8_t *src, size_t src_len)
{
	const char *env = getenv("DECODE_SIZE");
	size_t env_cap = 0;
	size_t walk;
	size_t cap;
	size_t scratch_sz;
	void *scratch = NULL;
	uint8_t *dst = NULL;
	size_t n;

	if (env != NULL && *env != '\0') {
		char *end = NULL;
		unsigned long v = strtoul(env, &end, 10);
		if (end != NULL && *end == '\0')
			env_cap = (size_t)v;
	}
	/* Caller-side EXP-CAP (u6k finding, R3 expansion-5; M20 fix): the
	 * oracle harness sizes its decode buffer at fixed DECODE_SIZE, so
	 * size ours the same way — exactly env when set. The walk is NOT
	 * a ceiling: the sizer MUST reject (0) on trailing-past-END/desync
	 * framing (S2.4/Q8) while the decoder MUST return on END and ignore
	 * trailing (S4.8), so min(walk,env) truncated oracle-full outputs
	 * to a 1024B prefix (R4 TRUNC-1024) and let cap-stop pre-empt replay
	 * rejects (R4 REV-1024). env unset -> walk (0 -> 1024 fallback for
	 * the no-env CLI path only). Never max(): walk-as-floor turned the
	 * 49B giant-ds mutant (ds 0x200 -> 0x5B000200) into a 1.5GB port
	 * output while the oracle returned 1536B (=DECODE_SIZE). No +slack
	 * arithmetic: env already carries +1024. J27b: walk itself is never
	 * capped, so huge-but-valid framings (2G ds) still decode fully. */
	walk = lzmesh_decoded_size(src, src_len);
	if (env_cap != 0)
		cap = env_cap;
	else
		cap = walk;
	if (cap == 0)
		cap = 1024;

	scratch_sz = lzmesh_decode_scratch_size();
	if (scratch_sz == 0)
		scratch_sz = 1;
	scratch = malloc(scratch_sz);
	if (scratch == NULL)
		return 10;
	dst = (uint8_t *)malloc(cap);
	if (dst == NULL) {
		free(scratch);
		return 10;
	}
	n = lzmesh_decode(dst, cap, src, src_len, scratch);
	free(scratch);
	if (n == 0) {
		free(dst);
		return 10; /* clean refusal: truncated/invalid input */
	}
	if (fwrite(dst, 1, n, stdout) != n) {
		free(dst);
		return 10;
	}
	free(dst);
	return 0;
}

int main(int argc, char **argv)
{
	uint8_t *src = NULL;
	size_t src_len = 0;
	int rc;

	/* Only exits 0/10 ever: anything else counts as CRASH against us. */
	if (argc != 3)
		return 10;
	if (strcmp(argv[1], "enc") != 0 && strcmp(argv[1], "dec") != 0)
		return 10;
	signal(SIGPIPE, SIG_IGN);

	if (read_all(stdin, &src, &src_len) != 0)
		return 10;
	if (src_len == 0 && src == NULL)
		return 10;

	if (strcmp(argv[1], "enc") == 0)
		rc = do_enc(argv[2], src, src_len);
	else
		rc = do_dec(src, src_len);
	free(src);
	return rc;
}
