/* R21-SEPTU2 (memo bed): T2 alloc-cache as interpose DYLIB.
 * ZERO delta on port tree: tip port_cli/bench binaries run UNMODIFIED
 * under DYLD_INSERT_LIBRARIES=libt2i.dylib. dyld redirects malloc/free
 * calls from all OTHER images to us; our own malloc/free calls go to the
 * original (dyld rule, verified first-hand: static-link table ignored for
 * own-image calls, dylib table live). => passthrough needs NO dlsym.
 * Same policy as R20 T2 (e4940491): best-fit 64-slot free-list,
 * malloc/free only. T2STAT=1 => destructor stats to stderr. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#define T2D_USABLE(p) malloc_size(p)
#else
#include <malloc.h>
#define T2D_USABLE(p) malloc_usable_size(p)
#endif

#define T2D_NCACHE 64
#define T2D_MAXCACHE (16u << 20)
#define T2D_MAXTOTAL (64u << 20)

static void *t2d_ptr[T2D_NCACHE];
static size_t t2d_usz[T2D_NCACHE];
static size_t t2d_retained;
static unsigned long long t2d_hits, t2d_miss, t2d_fhit, t2d_ffall;

void *t2d_malloc(size_t n) {
    int i, bi = -1;
    if (n != 0u) {
        for (i = 0; i < T2D_NCACHE; i++) {
            if (t2d_ptr[i] != NULL && t2d_usz[i] >= n
                && (bi < 0 || t2d_usz[i] < t2d_usz[bi])) {
                bi = i;
                if (t2d_usz[i] == n)
                    break;
            }
        }
        if (bi >= 0) {
            void *p = t2d_ptr[bi];
            t2d_retained -= t2d_usz[bi];
            t2d_ptr[bi] = NULL;
            t2d_usz[bi] = 0u;
            t2d_hits++;
            return p;
        }
    }
    t2d_miss++;
    return malloc(n); /* own-image call => original (dyld rule) */
}

void t2d_free(void *p) {
    size_t u;
    int i;
    if (p == NULL)
        return;
    u = T2D_USABLE(p);
    if (u > T2D_MAXCACHE || t2d_retained + u > T2D_MAXTOTAL) {
        t2d_ffall++;
        free(p);
        return;
    }
    for (i = 0; i < T2D_NCACHE; i++) {
        if (t2d_ptr[i] == NULL) {
            t2d_ptr[i] = p;
            t2d_usz[i] = u;
            t2d_retained += u;
            t2d_fhit++;
            return;
        }
    }
    t2d_ffall++;
    free(p);
}

typedef struct {
    const void *new_fn;
    const void *orig_fn;
} t2d_interpose_t;

__attribute__((used, section("__DATA,__interpose"))) const t2d_interpose_t
    t2d_table[2] = {
        {(const void *)t2d_malloc, (const void *)malloc},
        {(const void *)t2d_free, (const void *)free},
    };

__attribute__((destructor)) static void t2d_report(void) {
    const char *e = getenv("T2STAT");
    if (e != NULL && *e != '\0')
        fprintf(stderr,
                "T2D hits=%llu miss=%llu freehit=%llu freefall=%llu "
                "retained=%zu\n",
                t2d_hits, t2d_miss, t2d_fhit, t2d_ffall, t2d_retained);
}
