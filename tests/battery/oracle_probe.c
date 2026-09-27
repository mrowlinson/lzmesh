/* SPDX-License-Identifier: 0BSD */
/* Stage-6 Apple oracle probe — byte-pipe CLI over libcompression.
 *
 * Contract (same for port CLIs, see README.md):
 *   oracle_probe enc <selector-hex>   stdin: raw bytes   stdout: encoded bytes
 *   oracle_probe dec <selector-hex>   stdin: coded bytes stdout: decoded bytes
 *   exit 0  success, stdout holds output
 *   exit 10 codec returned 0 (clean refusal)
 *   exit 2  harness/usage error
 *
 * Loads libcompression via dlopen so the Apple build under test is
 * selectable: ORACLE_LIB env overrides the default "/usr/lib/libcompression.dylib".
 * Decode needs the decoded size up front (buffer API): caller passes it via
 * DECODE_SIZE env (battery sets it from the known input length). No probing,
 * no guessing, no codec logic — pure API plumbing.
 *
 * Build: clang -O2 -o oracle_probe oracle_probe.c -ldl
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXIT_REFUSED 10

typedef size_t (*encode_fn)(uint8_t *, size_t, const uint8_t *, size_t,
                            void *, int);
typedef size_t (*decode_fn)(uint8_t *, size_t, const uint8_t *, size_t,
                            void *, int);
typedef size_t (*scratch_fn)(int);

static uint8_t *read_all(size_t *len_out) {
    size_t cap = 1 << 16, len = 0;
    uint8_t *b = malloc(cap);
    size_t n;
    if (!b) return NULL;
    while ((n = fread(b + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            uint8_t *nb = realloc(b, cap);
            if (!nb) { free(b); return NULL; }
            b = nb;
        }
    }
    *len_out = len;
    return b;
}

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[1], "enc") && strcmp(argv[1], "dec")))
        return 2;
    int is_enc = !strcmp(argv[1], "enc");
    int algo = (int)strtol(argv[2], NULL, 16);

    const char *lib = getenv("ORACLE_LIB");
    if (!lib) lib = "/usr/lib/libcompression.dylib";
    void *h = dlopen(lib, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen %s failed\n", lib); return 2; }

    size_t in_len = 0;
    uint8_t *in = read_all(&in_len);
    if (!in) return 2;

    size_t out_cap;
    if (is_enc) {
        /* Worst case: raw block + end marker = size + 6, plus slack. */
        out_cap = in_len + 64;
        if (out_cap < 64) out_cap = 64;
    } else {
        const char *ds = getenv("DECODE_SIZE");
        if (!ds) { fprintf(stderr, "DECODE_SIZE required for dec\n"); return 2; }
        out_cap = (size_t)strtoull(ds, NULL, 10);
        if (out_cap == 0) out_cap = 1; /* allow empty-output decodes */
    }
    uint8_t *out = malloc(out_cap ? out_cap : 1);
    if (!out) return 2;

    size_t got = 0;
    if (is_enc) {
        encode_fn f = (encode_fn)dlsym(h, "compression_encode_buffer");
        scratch_fn s = (scratch_fn)dlsym(h, "compression_encode_scratch_buffer_size");
        if (!f || !s) return 2;
        size_t ss = s(algo);
        void *scratch = ss ? malloc(ss) : NULL;
        if (ss && !scratch) return 2;
        got = f(out, out_cap, in, in_len, scratch, algo);
        free(scratch);
    } else {
        decode_fn f = (decode_fn)dlsym(h, "compression_decode_buffer");
        scratch_fn s = (scratch_fn)dlsym(h, "compression_decode_scratch_buffer_size");
        if (!f || !s) return 2;
        size_t ss = s(algo);
        void *scratch = ss ? malloc(ss) : NULL;
        if (ss && !scratch) return 2;
        got = f(out, out_cap, in, in_len, scratch, algo);
        free(scratch);
    }
    /* got == 0 covers both refusal and empty-output decode; both sides
       use the same rule so agreement still reads as agreement. */
    if (got == 0) return EXIT_REFUSED;
    if (got > 0 && fwrite(out, 1, got, stdout) != got) return 2;
    return 0;
}
