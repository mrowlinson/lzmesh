/* SPDX-License-Identifier: 0BSD */
/* lzmesh_enc.c — clean-room LZMESH encoder (Stage-6 blind build).
 * Behavior source: SPECFINAL.md only. No Apple code seen.
 * Layout: scratch/levels/dispatch, finder, lazy, budgets/splits,
 * gates, Huffman encode, API glue. Subsystem owners append in
 * marked sections; keep -Wall -Wextra clean, C11. Encode must be
 * as fast as decode: honor PERF appendix MUSTs.
 */
#include "lzmesh.h"

/* === scratch sizes + level dispatch + RAW fallback (owner: u1) === */
#include <string.h> /* memmove for RAW copy (u1-owned include; hoist at merge) */
#include <stdio.h>
#include <stdlib.h>
#if defined(__ARM_NEON)
#include <arm_neon.h> /* P3-N1: u37_extend vector path only */
#endif

/* S1.5 encode scratch sizes. Boundary takes FULL 0xE00-form ONLY
 * (S1.3/Q20 PROVEN; flips GAPLOG-u1.md G1 bare-only choice). Interior
 * strips to bare below (S5.3/CONST#23); strip point is merge work. */
#define LZMESH_U1_SCRATCH_E00 323468u /* 0x4ef8c */
#define LZMESH_U1_SCRATCH_E01 1372044u /* 0x14ef8c */
#define LZMESH_U1_SCRATCH_E05 1388428u /* 0x152f8c */
#define LZMESH_U1_SCRATCH_E09 8728460u /* 0x852f8c */

/* S2.1 tags; S6.5 per-block ds ceiling (CONST-TABLE #22). */
#define LZMESH_U1_TAG_RAW 0x00
#define LZMESH_U1_TAG_END 0xff
#define LZMESH_U1_DS_MAX 0x7fffffffu
#define LZMESH_U1_RAW_OVERHEAD 6u /* tag + u32 ds + END */

/* Boundary level dispatch: plain cmp on FULL-form per S1.3/Q20 (entry
 * matches full-form; bare lives below the strip point per S5.3 /
 * CONST-TABLE #23). Single source of truth: nonzero scratch == valid
 * (S1.2 reject => encode 0, scratch 0). Wide/negative/bare values all
 * reject via default (S1.3 full-integer match, no low-byte masking). */
static size_t lzmesh_u1_scratch_for(int level) {
    switch (level) {
    case 0xE00: return (size_t)LZMESH_U1_SCRATCH_E00;
    case 0xE01: return (size_t)LZMESH_U1_SCRATCH_E01;
    case 0xE05: return (size_t)LZMESH_U1_SCRATCH_E05;
    case 0xE09: return (size_t)LZMESH_U1_SCRATCH_E09;
    default: return 0;
    }
}

static int lzmesh_u1_level_ok(int level) {
    return lzmesh_u1_scratch_for(level) != 0;
}

/* S1.3/Q20: strip FULL-form entry level to BARE interior value
 * (sub-0xE00). Returns 0/1/5/9, or -1 for anything else. Entry
 * callers validate full-form first (level_ok above); everything
 * below the strip point dispatches on bare (S5.3/CONST #23). */
static int lzmesh_u1_strip(int level) {
    switch (level) {
    case 0xE00: return 0;
    case 0xE01: return 1;
    case 0xE05: return 5;
    case 0xE09: return 9;
    default: return -1;
    }
}

/* === finder 6B/4B/3B + skip-parity (owner: u2; E4 owns strict-cascade lines in query, LANE-E4) === */
/* --- S5.7/S5.8 match finder (heads 7B/5B/3B per S5.8; the "6B/4B/3B"
 * lane label is shorthand -- see GAPLOG-u2 G-FLEN). Tables: big 2^hb u32
 * shared by h1+h2 probes + small 2^12 u32 for h3 (layout identity vs
 * u1 scratch sizes in GAPLOG-u2 G-TAB). L1: h1 + rep0 only, no small
 * table. PERF: O(1) probes, head-verify before extend, no per-symbol
 * refill; caller gates pos+9<=size (S5.4 loop floor). Byte-identity
 * BLOCKED by R-002 (F5 stub below). */
#define LZMESH_U2_C1 0x995D97CB4C1DB100ULL /* CONST-TABLE #1 */
#define LZMESH_U2_C2 0x97CB4C1DB1000000ULL /* CONST-TABLE #2 */
#define LZMESH_U2_C3 0x3779B100U /* CONST-TABLE #3 */
#define LZMESH_S2_MX 0x5D97CB4C1DB10000ULL /* S2: e01 MX lane (HINT-FINDER-R2 sec1 spec digits) */
#define LZMESH_U2_H3BITS 12u
#define LZMESH_U2_H3SIZE (1u << LZMESH_U2_H3BITS) /* h3: (w32*C3)>>20 */
#define LZMESH_U2_EMPTY 0xFFFFFFFFu
#define LZMESH_U2_HEAD1 7u
#define LZMESH_U2_HEAD2 5u
#define LZMESH_U2_HEAD3 3u

static const uint32_t lzmesh_u2_maxd[7] = /* S6.7; P3: L3 16->4096 exact
    * (HINT-FINDER-R1 sec6: 4095 take / 4096 skip, 3 seeds; old 16 was a
    * stale placeholder), L6+ 134217728->unbounded->1000000001 (Q4:
    * ANSWER-P3-1 take d<=1e9 / skip d>=1e9+1 EXACT; strict-< table so
    * entry = first-skip dist). Dead code (no callers; live path is the
    * u37 table below) kept consistent. */
    { 0u, 0u, 0u, 4096u, 65536u, 1048576u, 1000000001u };

/* Take code (S5.8): rep0=0 rep1=-1 rep2=-2 fresh=dist>0; never sel3. */
typedef struct { int code; uint32_t dist; uint32_t len; } lzmesh_u2_cand;

typedef struct {
    uint32_t *big;   /* 2^hb u32 entries, carved by caller */
    uint32_t *small; /* 2^12 u32 (L5/L9); NULL on L1 */
    unsigned hb;
    int level;       /* bare 1/5/9; L0 never constructs (S5.7c) */
} lzmesh_u2_finder;

static unsigned lzmesh_u2_log2_u64(uint64_t v) {
    unsigned r = 0;
    while ((v >>= 1) != 0)
        r++;
    return r;
}

/* S5.7d: hb = min(cap, floor(log2(size+1))+5), cap 18 (L1/L5) / 21 (L9). */
unsigned lzmesh_u2_hash_bits(size_t size, int level) {
    unsigned cap, hb;
    if (level == 0)
        return 0;
    cap = (level == 9) ? 21u : 18u;
    hb = lzmesh_u2_log2_u64((uint64_t)size + 1u) + 5u;
    return hb < cap ? hb : cap;
}

size_t lzmesh_u2_table_bytes(unsigned hb, int level) {
    size_t n = (size_t)1 << hb;
    if (level == 1)
        return n * sizeof(uint32_t);
    return n * sizeof(uint32_t) + (size_t)LZMESH_U2_H3SIZE * sizeof(uint32_t);
}

void lzmesh_u2_init(lzmesh_u2_finder *f, uint32_t *big, uint32_t *small,
                    unsigned hb, int level) {
    size_t n;
    if (f == NULL || big == NULL || hb == 0 || hb > 21)
        return;
    f->big = big;
    f->small = (level == 1) ? NULL : small;
    f->hb = hb;
    f->level = level;
    n = (size_t)1 << hb;
    /* P8-T2: EMPTY is all-1 bits; memset-class fill, same bytes/bounds. */
    memset(big, 0xFF, n * sizeof *big);
    if (f->small != NULL)
        memset(f->small, 0xFF,
               (size_t)LZMESH_U2_H3SIZE * sizeof *f->small);
}

/* P5-W3 word loads: unaligned-safe LE primitives. memcpy is the
 * canonical unaligned load (clang/gcc lower to ldr on ARM64); the
 * byte-assemble leg covers compilers without __BYTE_ORDER__. */
#if !defined(__BYTE_ORDER__)
static uint16_t lzmesh_wl_ld16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t lzmesh_wl_ld32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t lzmesh_wl_ld64(const uint8_t *p) {
    uint32_t lo = lzmesh_wl_ld32(p);
    uint32_t hi = lzmesh_wl_ld32(p + 4);
    return (uint64_t)lo | ((uint64_t)hi << 32);
}
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static uint16_t lzmesh_wl_ld16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return (uint16_t)((v >> 8) | (v << 8));
}
static uint32_t lzmesh_wl_ld32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return ((v >> 24) | ((v >> 8) & 0xff00u) | ((v & 0xff00u) << 8) |
            (v << 24));
}
static uint64_t lzmesh_wl_ld64(const uint8_t *p) {
    uint32_t lo = lzmesh_wl_ld32(p);
    uint32_t hi = lzmesh_wl_ld32(p + 4);
    return (uint64_t)lo | ((uint64_t)hi << 32);
}
#else /* LE: memcpy value is already LE-correct. */
static uint16_t lzmesh_wl_ld16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}
static uint32_t lzmesh_wl_ld32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static uint64_t lzmesh_wl_ld64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}
#endif

/* P5-W3: LE load of n bytes. Widths derive from n (never a fixed 8),
 * and every word load lies fully inside [p,p+n) — a subset of the
 * bytes the old byte loop read — so no new OOB read is possible by
 * construction, on any input incl size<8. */
static uint64_t lzmesh_wl_load_n(const uint8_t *p, unsigned n) {
    uint64_t w;
    unsigned sh;
    if (n > 8u)
        n = 8u; /* defensive; max call-site n is 8 (old loop UB past 8) */
    if (n == 8u)
        return lzmesh_wl_ld64(p);
    w = 0u;
    sh = 0u;
    if (n >= 4u) {
        w = lzmesh_wl_ld32(p);
        p += 4;
        sh = 32u;
        n -= 4u;
    }
    if (n >= 2u) {
        w |= (uint64_t)lzmesh_wl_ld16(p) << sh;
        p += 2;
        sh += 16u;
        n -= 2u;
    }
    if (n == 1u)
        w |= (uint64_t)p[0] << sh;
    return w;
}

static uint64_t lzmesh_u2_load_n(const uint8_t *p, unsigned n) {
    return lzmesh_wl_load_n(p, n); /* LE load (GAPLOG-u2 G-END) */
}

static uint32_t lzmesh_u2_h1(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_U2_C1) >> (64u - hb));
}

static uint32_t lzmesh_u2_h2(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_U2_C2) >> (64u - hb));
}

static uint32_t lzmesh_u2_h3(uint32_t w) {
    return (w * LZMESH_U2_C3) >> 20u;
}

/* S2: MX slot (e01 lane): (load8B * MX) >> (64-hb). 8B load ->
 * 6B effective (tz16 kills top 2B); 6B head verify at query. */
static uint32_t lzmesh_s2_hx(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_S2_MX) >> (64u - hb));
}

static int lzmesh_u2_head_eq(const uint8_t *a, const uint8_t *b, unsigned n) {
    /* P5-W3: word compares, early exit on mismatch (the hot probe case).
     * Widths mirror wl_load_n: every load inside [p,p+n), no new OOB. */
    if (n >= 8u) {
        if (lzmesh_wl_ld64(a) != lzmesh_wl_ld64(b))
            return 0;
        if (n == 8u)
            return 1;
        a += 8;
        b += 8;
        n -= 8u;
    }
    if (n >= 4u) {
        if (lzmesh_wl_ld32(a) != lzmesh_wl_ld32(b))
            return 0;
        a += 4;
        b += 4;
        n -= 4u;
    }
    if (n >= 2u) {
        if (lzmesh_wl_ld16(a) != lzmesh_wl_ld16(b))
            return 0;
        a += 2;
        b += 2;
        n -= 2u;
    }
    if (n == 1u && *a != *b)
        return 0;
    return 1;
}

static uint32_t lzmesh_u2_extend(const uint8_t *s, size_t size, size_t pos,
                                 uint32_t dist, uint32_t start) {
    uint32_t len = start;
    uint32_t max = (uint32_t)(size - pos);
    size_t src = pos - (size_t)dist;
    while (len < max && s[pos + len] == s[src + len])
        len++;
    return len;
}

/* S6.7: idx=min(truelen,6), reject iff dist>=cap (strict <). H2/H3 only. */
static int lzmesh_u2_maxd_ok(uint32_t dist, uint32_t len) {
    unsigned idx = len < 6u ? len : 6u;
    return dist < lzmesh_u2_maxd[idx];
}

/* Insert-at-visited: store pos at every loadable hash slot. Skipped
 * positions are never inserted (caller schedule owns skips; S5.7c L1
 * miss-gaps NOT filled). h1 slot written last so it wins h1/h2
 * same-slot collisions (Q26 OPEN-guess INTERIM: h1-last-write stands;
 * flip to h2 on collision-cell mismatch; no conformance claim meanwhile). */
void lzmesh_u2_insert(lzmesh_u2_finder *f, const uint8_t *s, size_t size,
                      size_t pos) {
    unsigned hb;
    if (f == NULL || s == NULL || pos > 0x7fffffffu)
        return;
    if (f->hb == 0 || f->hb > 21)
        return;
    hb = f->hb;
    if (f->level != 1 && f->small != NULL && pos + LZMESH_U2_HEAD3 <= size)
        f->small[lzmesh_u2_h3((uint32_t)lzmesh_u2_load_n(s + pos,
                                                        LZMESH_U2_HEAD3))]
            = (uint32_t)pos;
    if (f->level != 1 && pos + LZMESH_U2_HEAD2 <= size)
        f->big[lzmesh_u2_h2(lzmesh_u2_load_n(s + pos, LZMESH_U2_HEAD2), hb)]
            = (uint32_t)pos;
    if (pos + LZMESH_U2_HEAD1 <= size)
        f->big[lzmesh_u2_h1(lzmesh_u2_load_n(s + pos, LZMESH_U2_HEAD1), hb)]
            = (uint32_t)pos;
}

/* Query at pos (lazy unit probes p/p+1/p+2 explicitly = stride).
 * Order: rep probes (2B/4B/4B heads, codes 0/-1/-2; L1 rep0-only)
 * then STRICT cascade hash lookup (7B, else 5B, else 3B heads;
 * L1 h1-only): first tier with slot-hit + head-verify wins, rest
 * skipped (E4 black-box: share7 lit@41+take@42 proves 7B-B-shadow
 * stops cascade; share6/share5/share3 take-30 proves miss-
 * continuation only). H2/H3 winner passes maxD; winner filter-fail
 * = miss (E4: filter-fallthrough REFUTED, mechanically moot:
 * 7B unfiltered, 5B-hits always pass, 3B is last). Slot-key widths
 * are head-keyed 7/5/3 (E4 token-decoded: 7B-key=7 via share6-take,
 * 5B-key=5 via share5 8/200 + share4 1/200 + share3 0/200 eclipse
 * scans, 3B-key=3 via L3-hit byte-3-independent; memo 8/8/4 dead).
 * Candidacy by probe-recency: hash dist matching a live recent codes
 * REP (lowest slot wins ties); else NEW (code=dist). No floors here
 * (u3 owns economic floors S7.3). Returns count in out[6]. */
int lzmesh_u2_query(lzmesh_u2_finder *f, const uint8_t *s, size_t size,
                    size_t pos, const uint32_t recent[4],
                    lzmesh_u2_cand *out) {
    static const unsigned heads[3] =
        { LZMESH_U2_HEAD1, LZMESH_U2_HEAD2, LZMESH_U2_HEAD3 };
    int n = 0;
    int nrep, nh, i;
    if (f == NULL || s == NULL || out == NULL || recent == NULL)
        return 0;
    if (size > 0x7fffffffu || pos >= size || f->hb == 0 || f->hb > 21)
        return 0;
    nrep = (f->level == 1) ? 1 : 3;
    for (i = 0; i < nrep; i++) {
        uint32_t r = recent[i];
        unsigned need = (i == 0) ? 2u : 4u;
        if (r == 0 || (size_t)r > pos || pos + need > size)
            continue;
        if (!lzmesh_u2_head_eq(s + pos, s + pos - r, need))
            continue;
        out[n].code = -i;
        out[n].dist = r;
        out[n].len = lzmesh_u2_extend(s, size, pos, r, need);
        n++;
    }
    nh = (f->level == 1) ? 1 : 3;
    for (i = 0; i < nh; i++) {
        unsigned hd = heads[i];
        uint32_t q, dist, len;
        int j, code;
        if (pos + hd > size)
            continue;
        if (i == 0)
            q = f->big[lzmesh_u2_h1(lzmesh_u2_load_n(s + pos, hd), f->hb)];
        else if (i == 1)
            q = f->big[lzmesh_u2_h2(lzmesh_u2_load_n(s + pos, hd), f->hb)];
        else if (f->small != NULL)
            q = f->small[lzmesh_u2_h3(
                (uint32_t)lzmesh_u2_load_n(s + pos, hd))];
        else
            continue;
        if (q == LZMESH_U2_EMPTY || (size_t)q >= pos)
            continue;
        dist = (uint32_t)pos - q;
        if (!lzmesh_u2_head_eq(s + pos, s + q, hd))
            continue; /* miss: cascade continues (E4) */
        len = lzmesh_u2_extend(s, size, pos, dist, hd);
        if (i > 0 && !lzmesh_u2_maxd_ok(dist, len))
            break; /* E4 strict: winner filter-fail = miss, no
                    * fallthrough (black-box refuted). */
        code = (int)dist;
        for (j = 0; j < nrep; j++) /* R==H REP-codes (GAPLOG-u2 G-REC) */
            if (recent[j] == dist) {
                code = -j;
                break;
            }
        out[n].code = code;
        out[n].dist = dist;
        out[n].len = len;
        n++;
        break; /* E4 strict: first verified hit wins (share7). */
    }
    return n;
}

/* L1 VIS stride (S5.7c/Q28): step = 1+(pos-anchor)>>8, anchor init 1,
 * 1-based chain (v0=1: 1..257, odds to 511, 513,516,519...).
 * Scheduler probes with pos+1; P-VIS/S-VIS gate + anchor update
 * caller-owned (GAPLOG G-VIS). */
uint32_t lzmesh_u2_l1_step(uint32_t pos, uint32_t anchor) {
    return 1u + ((pos - anchor) >> 8);
}

/* R-002 MAJOR OPEN: F5 finder-skip +1..+5 trigger unknown (NOT run
 * length; corr far-probe). Stub returns 0; NO byte-identity claim on
 * skip-affected cells until R-002 closes. */
unsigned lzmesh_u2_f5_skip(const uint8_t *s, size_t size, size_t pos) {
    (void)s;
    (void)size;
    (void)pos;
    return 0;
}

/* === lazy + budgets/splits + gates (owner: u3) === */
/* S5.4 budget machine. B1 by doubling, never immediate (CONST #19). */
#define LZMESH_U3_B0 0x4000u
#define LZMESH_U3_BCLAMP 0xF3F0u
#define LZMESH_U3_PRECHECK 0xF40u
#define LZMESH_U3_TOKCAP 16384u
#define LZMESH_U3_LENCAP 16384u
#define LZMESH_U3_REM39 39u
#define LZMESH_U3_SCANFLOOR 9u
/* S5.8 lazy: observed 38->39 flip per R-026 (text says >39). */
#define LZMESH_U3_LONG38 38u
#define LZMESH_U3_P_NOMINAL 12u
#define LZMESH_U3_P_MIN 10u
#define LZMESH_U3_L5_THRESH6 6u

uint32_t lzmesh_u3_budget_for(unsigned blk) {
    if (blk == 0)
        return LZMESH_U3_B0;
    if (blk == 1)
        return (uint32_t)(LZMESH_U3_B0 << 1); /* doubling per S5.4 */
    return LZMESH_U3_BCLAMP; /* B2+ sticky */
}

uint32_t lzmesh_u3_stop_after(uint32_t lit, uint32_t rem) {
    return lit + rem; /* recomputed AFTER every token; frozen on no-match */
}

int lzmesh_u3_loop_live(size_t pos, uint32_t stop, size_t size) {
    if (pos >= size)
        return 0;
    return pos < (size_t)stop && size - pos >= LZMESH_U3_SCANFLOOR;
}

unsigned lzmesh_u3_lenbytes(int extra_ge255) {
    return extra_ge255 ? 5u : 1u; /* 1, or 1+4 iff extra>=255 */
}

uint32_t lzmesh_u3_spend(uint32_t run, int extra_ge255, int is_new) {
    /* run + lenbytes + 1 + (5 iff new-dist); first byte FREE (caller). */
    return run + lzmesh_u3_lenbytes(extra_ge255) + 1u
        + (is_new ? 5u : 0u);
}

int lzmesh_u3_test2_absorb(uint32_t pos_or_stop, uint32_t E) {
    return (uint64_t)pos_or_stop + 9u > (uint64_t)E; /* 9B scan floor */
}

uint32_t lzmesh_u3_test2_x1(uint32_t E, uint32_t lpos, uint32_t rem,
                            uint32_t tc, uint32_t lc, int absorb) {
    uint32_t mx;
    if (absorb)
        return E;
    mx = tc > lc ? tc : lc;
    if (rem <= LZMESH_U3_REM39 || mx < LZMESH_U3_B0)
        return lpos + rem;
    return lpos; /* +0 branch (R-057 static-certain, dynamic unobserved) */
}

int lzmesh_u3_tail_absorb(size_t tail) {
    return tail <= 8u; /* <=8 absorbs, >=9 splits */
}

int lzmesh_u3_tok_interior_ok(uint32_t tok) {
    return tok <= LZMESH_U3_TOKCAP; /* tail race 16385 emissible (dec rejects) */
}

int lzmesh_u3_len_end(uint32_t len) {
    return len >= LZMESH_U3_LENCAP; /* post-account: ends after tok w/ len>=cap */
}

int lzmesh_u3_precheck_fail(uint32_t len, uint32_t lit, uint32_t tok,
                            uint32_t dist) {
    uint64_t sum = (uint64_t)len + (uint64_t)lit + (uint64_t)tok
        + (uint64_t)5 * (uint64_t)dist;
    return (sum >> 4) > (uint64_t)LZMESH_U3_PRECHECK; /* fail -1, not raw */
}

/* S5.3 TIER-1 / TIER-2 gates. No COMP-vs-RAW size compare anywhere. */
int lzmesh_u3_tier1_comp(uint32_t D, uint32_t fo) {
    return D > fo; /* D==fo -> RAW */
}

int lzmesh_u3_tier2_keep(size_t outpos, size_t n) {
    return outpos <= n; /* pre-ff outpos; <= keeps */
}

/* S5.3.a terminator: rep0 len-2 token + class + lc. */
uint8_t lzmesh_u3_term_token(unsigned run) {
    unsigned m = run < 3u ? run : 3u;
    return (uint8_t)((m << 6) | 0x00u);
}

uint32_t lzmesh_u3_term_class(unsigned R, unsigned lit) {
    if (R == 0u)
        return 0x00u;
    if (R == 1u)
        return (uint32_t)lit + 40u;
    if (R == 2u)
        return 0x80u;
    return 0xC0u; /* escape base; payload u4-owned (G-TERM) */
}

unsigned lzmesh_u3_term_lc(unsigned R, int first) {
    unsigned base = (R >= 3u && R - 3u >= 255u) ? 5u : 1u;
    return base + (first ? 1u : 0u);
}

/* S5.7c level parse rows. */
int lzmesh_u3_level_parse_ok(int level) {
    return level == 0 || level == 1 || level == 5 || level == 9;
}

int lzmesh_u3_lazy_on(int level) {
    return level == 5 || level == 9; /* L0/L1 no lazy */
}

int lzmesh_u3_nrep(int level) {
    if (level == 1)
        return 1;
    if (level == 5 || level == 9)
        return 3;
    return 0; /* L0: no finding */
}

int lzmesh_u3_nhash(int level) {
    if (level == 1)
        return 1;
    if (level == 5 || level == 9)
        return 3;
    return 0;
}

int lzmesh_u3_fresh_floor(int level) {
    if (level == 5 || level == 9)
        return 3;
    if (level == 1)
        return 6; /* S5.7c; S7.3 (3,7] window noted in GAPLOG */
    return -1; /* L0 never takes */
}

int lzmesh_u3_rep_floor(int level) {
    if (level == 1 || level == 5 || level == 9)
        return 2;
    return -1;
}

int lzmesh_u3_take_floor_ok(int level, int is_rep, uint32_t len) {
    int fl;
    if (level == 0)
        return 0;
    fl = is_rep ? lzmesh_u3_rep_floor(level) : lzmesh_u3_fresh_floor(level);
    if (fl < 0)
        return 0;
    return len >= (uint32_t)fl;
}

/* L1 VIS: take iff P-VIS AND S-VIS; predicates caller-owned (G-VIS open). */
int lzmesh_u3_l1_vis_take(int p_vis, int s_vis) {
    return p_vis && s_vis;
}

uint32_t lzmesh_u3_l1_anchor_init(uint32_t start) {
    (void)start;
    /* Q28 PROVEN: anchor init 1 (absolute, 1-based VIS chain: v0=1
     * gives 1..257, odds to 511, then 513,516,519... exactly as
     * specified; start-1 fails both step-1 and step-3 shapes).
     * Multiblock carry-vs-reset OPEN; scheduler owns. (u7 flip) */
    return 1u;
}

/* S5.8 lazy primitives. */
unsigned lzmesh_u3_bitlen(uint32_t v) {
    /* P11-WINS: clz idiom (was bit loop); identical for all v. */
    return v == 0u ? 0u : 32u - (unsigned)__builtin_clz(v);
}

/* sb(dist) inverse of S3.11 d=(8<<sb)+low+8*suf-7, suf width=sb (G-SB).
 * max(sb)=2^(sb+4)-8; min(sb)=2^(sb+3)-7. d==0 -> 0 (no dist). */
unsigned lzmesh_u3_sb_of(uint32_t d) {
    /* P11-WINS: closed form (was sb=1..28 scan). d<=mx(sb)=2^(sb+4)-8
     * iff sb+4 >= ceil(log2(d+8)) = bitlen(d+7); u64 avoids d+7 wrap.
     * R8-L9NEW R-f: branchless (d=1..8 already yield 0 via clz;
     * d==0 masked; cap+mask compile to csel). */
    uint64_t x = (uint64_t)d + 7u; /* >=7: clzll defined */
    unsigned sb = 64u - (unsigned)__builtin_clzll(x) - 4u;
    sb = sb <= 28u ? sb : 28u;
    return (d == 0u) ? 0u : sb;
}

int64_t lzmesh_u3_score(uint32_t len, uint32_t dist, uint32_t extralits) {
    return (int64_t)4 * (int64_t)len
        - (int64_t)(lzmesh_u3_bitlen(dist) + 5u)
        - (int64_t)4 * (int64_t)extralits;
}

int lzmesh_u3_long_take(uint32_t len) {
    return len > LZMESH_U3_LONG38; /* observed; text >39 is off-by-one */
}

/* L5 short (S5.8/Q33): caller gates len_next<=6 and dl>0. Dlen/Dsb are
 * SIGNED next-minus-cur diffs; Dsb<0 (next closer than current) must
 * flow raw into 4*Dlen>Dsb+4+P, never clamp >=0 (E-B4). */
int lzmesh_u3_l5_skip(int32_t Dlen, int32_t Dsb, unsigned P) {
    return (int64_t)4 * (int64_t)Dlen
        > (int64_t)Dsb + 4 + (int64_t)P;
}

/* L9 short H0-exact: L<=4 short-blocked TAKE; else strict >. */
int lzmesh_u3_l9_skip(int32_t L, int32_t sb_diff) {
    if (L <= 4)
        return 0;
    return (int64_t)4 * ((int64_t)L - 7) > (int64_t)sb_diff + 4;
}

/* Backext extend-to-mismatch (uncapped primitive; callers apply policy
 * caps: S4 caps at 7 per HINT-SCHED-R1 sec3). 0 == N/A. */
uint32_t lzmesh_u3_backext(const uint8_t *s, size_t pos, uint32_t dist) {
    uint32_t n = 0;
    size_t base;
    if (s == NULL || dist == 0u || (size_t)dist >= pos || pos == 0u)
        return 0u;
    base = pos - (size_t)dist;
    while (n < base && s[pos - 1u - n] == s[base - 1u - n])
        n++;
    return n;
}

int lzmesh_u3_p1_hash_wins(int64_t cur, int64_t nxt) {
    return nxt > cur; /* strict >; ties stay */
}

int lzmesh_u3_rep_tie_pick(int a_slot, int b_slot) {
    return a_slot < b_slot ? a_slot : b_slot; /* lowest-slot-wins */
}

/* R-002: F5 trigger unknown; forward u2 stub, never invent (G-F5). */
unsigned lzmesh_u3_f5_delay(const uint8_t *s, size_t size, size_t pos) {
    return lzmesh_u2_f5_skip(s, size, pos);
}

/* Lazy decide: 0 take-cur/emit-lit, 1 skip to +1, 2 skip to +2.
 * Order: floors -> backext -> long -> E01-nolazy -> short-gates ->
 * +1 true-rep0 -> +1 cost -> +2 rep0-only. +1 search scope (single vs
 * full) is caller finder work; cost model identical (S5.9).
 * No byte-identity claim on order-sensitive cells: Q36 leaves
 * cost-vs-+1rep0 fine order OPEN (R-020/R-039/R-044); interim order
 * (rep0-check before cost) stands until those close (E-B5). */
int lzmesh_u3_lazy_decide(int level, uint32_t cur_len, uint32_t cur_dist,
                          int cur_is_rep, uint32_t n1_len, uint32_t n1_dist,
                          int n1_is_true_rep0, int n1_is_rep,
                          uint32_t n2_rep0_len, int n2_rep0_live,
                          uint32_t extralits, unsigned P, int backext_applies,
                          int n1_recency_wins_tie) {
    int64_t sc, sn;
    int n1_live;
    if (!lzmesh_u3_take_floor_ok(level, cur_is_rep, cur_len))
        return 0;
    if (backext_applies)
        return 0; /* take immediately, +1 ignored */
    if (lzmesh_u3_long_take(cur_len))
        return 0;
    if (!lzmesh_u3_lazy_on(level))
        return 0; /* E01 no lazy */
    n1_live = n1_len != 0u
        && lzmesh_u3_take_floor_ok(level, n1_is_rep, n1_len);
    if (n1_live && level == 5 && n1_len <= LZMESH_U3_L5_THRESH6) {
        /* Signed next-minus-cur diffs (G-SHORTDIR). */
        int64_t dl = (int64_t)n1_len - (int64_t)cur_len;
        int64_t ds = (int64_t)lzmesh_u3_sb_of(n1_dist)
            - (int64_t)lzmesh_u3_sb_of(cur_dist);
        unsigned pp = P < LZMESH_U3_P_MIN ? LZMESH_U3_P_MIN : P;
        /* Raw signed ds per S5.8/Q33 (4*Dlen>Dsb+4+P); clamping ds>=0
         * takes where the spec skips when next-sb < cur-sb (E-B4). */
        if (!(dl > 0
                && (int64_t)4 * dl > ds + 4 + (int64_t)pp))
            return 0;
    }
    if (n1_live && level == 9) {
        int32_t diff = (int32_t)lzmesh_u3_sb_of(n1_dist)
            - (int32_t)lzmesh_u3_sb_of(cur_dist);
        if (!lzmesh_u3_l9_skip((int32_t)n1_len, diff))
            return 0; /* L<=4 blocked or H0 loses -> TAKE */
        if (n1_len <= 4u)
            return 0;
    }
    if (n1_live && n1_is_true_rep0)
        return 1; /* TRUE-rep0 wins, REP-coded d>=-1 */
    if (n1_live) {
        sc = lzmesh_u3_score(cur_len, cur_dist, extralits);
        sn = lzmesh_u3_score(n1_len, n1_dist, extralits + 1u);
        if (sn > sc)
            return 1;
        if (sn == sc && n1_recency_wins_tie)
            return 1; /* duel-ties to recency; else incumbent stays */
    }
    if (n2_rep0_live
        && lzmesh_u3_take_floor_ok(level, 1, n2_rep0_len))
        return 2; /* +2 rep0-ONLY; rep1@+2 never (caller filters) */
    return 0;
}

/* === Huffman encode + block writer (owner: u4) === */
/* S2.3/S2.5/S3.1-S3.12/S5.3.a/S5.5/S5.6. Pure helpers + Huffman core +
 * footer/COMP emit + lzmesh_encode COMP seam. Byte-identity BLOCKED by
 * R-002 (u2 F5 stub), R-100 (fo model), G-BOFO/G-HDRBIT (see GAPLOG-u4d);
 * S5.9 non-parity labeling applies (u1 G10). PERF: one-shot table build
 * per substream, O(n) table-driven emit, no per-symbol alloc/refill. */
#define LZMESH_U4_MODE_RAW 0u
#define LZMESH_U4_MODE_REPEAT 1u
#define LZMESH_U4_MODE_HUFFMAN 2u
#define LZMESH_U4_HDR73 73u /* S5.5 const73 header cost */
#define LZMESH_U4_MAXLEN_SYM 10u /* S3.7 symbols */
#define LZMESH_U4_MAXLEN_META 5u /* S3.7 meta */
#define LZMESH_U4_KRAFT16 0x10000u /* S3.7 exact gate, 2^16 units */
#define LZMESH_U4_KRAFT_META 1024u /* S3.8 exact gate both levels */
#define LZMESH_U4_TAG_COMP 0x01

/* S2.5 footer pack (Q2): mode lanes ascend lit,tok,len,dist in bits
 * 2:0/5:3/8:6/11:9; count slots stay footer order (tok,len,lit,dist). */
uint32_t lzmesh_u4_modes_pack(unsigned m_tok, unsigned m_len,
                              unsigned m_lit, unsigned m_dist) {
    return (m_lit & 7u) | ((m_tok & 7u) << 3) | ((m_len & 7u) << 6)
        | ((m_dist & 7u) << 9);
}

int lzmesh_u4_mode_ok(unsigned m) {
    return m <= LZMESH_U4_MODE_HUFFMAN; /* 3-7 reject (S3.2/S7.2) */
}

/* S2.6: count0 MUST be RAW. */
int lzmesh_u4_empty_mode_ok(unsigned m, uint32_t count) {
    if (count == 0u)
        return m == LZMESH_U4_MODE_RAW;
    return lzmesh_u4_mode_ok(m);
}

/* S3.9 token = (lit<<6)|(sel<<3)|len; sel = new?4:rep.
 * u9: new-dist callers with len_short>=8 MUST use lzmesh_u7_new_sel
 * or lzmesh_u7_token_new (Q17 len-hi in bits 4:3); this returns 4
 * (correct only for short<8). Rep path unchanged. */
uint32_t lzmesh_u4_sel(int is_new, unsigned rep) {
    return is_new ? 4u : (rep & 7u);
}

uint8_t lzmesh_u4_token(unsigned lit, uint32_t sel, unsigned len) {
    /* lit is 2-bit (0..3, escape 3 per S3.10); &7 overflows into sel. */
    return (uint8_t)(((lit & 3u) << 6) | ((sel & 7u) << 3) | (len & 7u));
}

int lzmesh_u4_rep_emittable(unsigned rep) {
    return rep <= 2u; /* never sel 3 (S3.9); dec still accepts (P-D9) */
}

/* S3.10: value>=0xff takes ff + full u32 (5B); inline 3/7|0x1f map = G-LENENC. */
int lzmesh_u4_len_is_long(uint32_t v) {
    return v >= 0xffu;
}

unsigned lzmesh_u4_len_nbytes(uint32_t v) {
    return lzmesh_u4_len_is_long(v) ? 5u : 1u;
}

uint32_t lzmesh_u4_lit_run(unsigned lit) {
    return (uint32_t)lit - 1u; /* lit_run = lit-1; L195 step at 258 */
}

/* S3.11 inverse: d=(8<<sb)+low+8*suf-7 -> (sb,low,suf). sb via u3 (G-SB);
 * (8<<sb) in u32, 0 for sb>=29 (S6.3). */
void lzmesh_u4_dist_split(uint32_t d, unsigned *sb, unsigned *low,
                          uint32_t *suf) {
    unsigned s;
    uint32_t base, t;
    if (sb == NULL || low == NULL || suf == NULL)
        return;
    s = lzmesh_u3_sb_of(d);
    base = (s >= 29u) ? 0u : (8u << s);
    t = d + 7u - base; /* u32 wrap per S6.3 */
    *sb = s;
    *low = (unsigned)(t & 7u);
    *suf = t >> 3;
}

/* P11-WINS: unchecked twin (all 10 call sites pass &local x3, never NULL;
 * callers audited 2026-09-29). Same body minus the guard. */
static void lzmesh_u4_dist_split_nc(uint32_t d, unsigned *sb,
                                    unsigned *low, uint32_t *suf) {
    unsigned s = lzmesh_u3_sb_of(d);
    uint32_t base = (s >= 29u) ? 0u : (8u << s);
    uint32_t t = d + 7u - base; /* u32 wrap per S6.3 */
    *sb = s;
    *low = (unsigned)(t & 7u);
    *suf = t >> 3;
}

int lzmesh_u4_dist_has_suffix(uint32_t d) {
    return d >= 9u; /* d<=8 sb=0 no lane bits; d=9 first suffix bit */
}

/* S3.12 recents: init {1,1,1,1}, MTF-dedup update (G-MTF), save/restore.
 * OPEN E-B8: v2 silent on dedup-vs-plain-shift; decode lane uses
 * plain-shift (G-u3-1). Stands interim; needs spec-query (rep-token
 * recents trace) before byte-identity claims on repeat-dist cells. */
void lzmesh_u4_recents_init(uint32_t r[4]) {
    if (r == NULL)
        return;
    r[0] = 1u;
    r[1] = 1u;
    r[2] = 1u;
    r[3] = 1u;
}

void lzmesh_u4_recents_update(uint32_t r[4], uint32_t dist) {
    uint32_t v[4];
    unsigned i, o;
    if (r == NULL)
        return;
    v[0] = r[0];
    v[1] = r[1];
    v[2] = r[2];
    v[3] = r[3];
    r[0] = dist;
    o = 1u;
    for (i = 0; i < 4u && o < 4u; i++) {
        if (v[i] == dist)
            continue;
        r[o++] = v[i];
    }
}

void lzmesh_u4_recents_save(const uint32_t r[4], uint32_t out[4]) {
    unsigned i;
    if (r == NULL || out == NULL)
        return;
    for (i = 0; i < 4u; i++)
        out[i] = r[i]; /* RAW path save/restore (S3.12); encoder restores */
}

/* S5.5 mode tests. Order: 0->RAW; all-equal->REPEAT; n<=10 (8n<=n+73)->RAW;
 * else HUF candidate subject to speculative rollback (bits>=8n restores).
 * tok (13,14] step is a consistency consequence, not a separate rule. */
unsigned lzmesh_u4_mode_trivial(uint32_t n, int all_equal) {
    if (n == 0u)
        return LZMESH_U4_MODE_RAW;
    if (all_equal)
        return LZMESH_U4_MODE_REPEAT;
    if ((uint64_t)8 * (uint64_t)n <= (uint64_t)n + LZMESH_U4_HDR73)
        return LZMESH_U4_MODE_RAW;
    return LZMESH_U4_MODE_HUFFMAN; /* candidate; rollback gate below */
}

int lzmesh_u4_huff_rollback(uint64_t bits, uint32_t n) {
    return bits >= (uint64_t)8 * (uint64_t)n; /* counts restored, bytes stay */
}

/* S5.6 RULE-PAD1: pad bit0 = 1 iff lane suffix-assigned. */
int lzmesh_u4_pad0_one(uint32_t distc, unsigned lane) {
    return (distc >= 8u) || (lane < distc);
}

/* S5.6 RULE-PADHI B-TAB(lastL,k); unlisted -> 0 (G-PADTAB). */
uint32_t lzmesh_u4_btab(unsigned lastL, unsigned k) {
    if (lastL == 7u && k == 6u)
        return 32u;
    if (lastL == 7u && k == 7u)
        return 96u;
    if (lastL == 9u && (k == 4u || k == 5u || k == 6u))
        return 8u;
    if (lastL == 9u && k == 7u)
        return 72u;
    if (lastL == 10u && (k == 4u || k == 5u))
        return 8u;
    if (lastL == 10u && (k == 6u || k == 7u))
        return 40u;
    return 0u;
}

int lzmesh_u4_k_in_range(unsigned k) {
    return k <= 7u; /* k MUST stay in 0..7 */
}

uint8_t lzmesh_u4_lane_final(uint8_t data, uint32_t P, unsigned m) {
    return (uint8_t)(data | (uint8_t)((P << (m & 7u)) & 0xffu));
}

/* S5.3.a u4-owned remainder (G-TERM): escape payload past C0 + overhang-2. */
int lzmesh_u4_term_is_escape(unsigned R) {
    return R >= 3u;
}

uint32_t lzmesh_u4_term_clamp(uint32_t end, uint32_t ds) {
    return end > ds ? ds : end; /* overhang +2 clamps to ds, never rejects */
}

/* S2.3 COMP header gates (C2/C3/C4/C5/C6). */
int lzmesh_u4_comp_gates_ok(uint32_t ds, uint32_t bo, uint32_t fo) {
    if (ds == 0u)
        return 0; /* C2 */
    if (ds > LZMESH_U1_DS_MAX)
        return 0; /* C3 per-block ds ceiling (S6.5) */
    if (bo < 9u)
        return 0; /* C4 */
    if (bo > fo)
        return 0; /* C5 */
    if (fo >= ds)
        return 0; /* C6 strict */
    return 1;
}

/* --- Huffman core (S3.7): package-merge + w23 + Kraft + canonical rbit --- */
static uint32_t lzmesh_u4_clz32(uint32_t v) {
    uint32_t n = 0;
    uint32_t bit = 1u << 31;
    if (v == 0u)
        return 32u;
    while ((v & bit) == 0u) {
        n++;
        bit >>= 1;
    }
    return n;
}

unsigned lzmesh_u4_w23(unsigned nsym, unsigned maxlen) {
    unsigned w;
    if (nsym == 0u)
        return 0u;
    w = 34u - lzmesh_u4_clz32((uint32_t)nsym);
    return w < maxlen ? w : maxlen;
}

/* One package-merge pass at limit L (KMT with parent pointers).
 * Caller scratch: idx[nsym] + items[9*L*nsym u32] + sel[2*L*nsym B]
 * (see lzmesh_u4_build_items_u32/sel_bytes; PERF: no alloc).
 * Item triple: (weight, childA, childB); childA==EMPTY => singleton
 * with symbol in childB. Returns 1 on usable lengths. */
#define LZMESH_U4_PM_EMPTY 0xffffffffu

static int lzmesh_u4_pm_pass(const uint32_t *freq, unsigned nsym, unsigned L,
                             unsigned char *lens, unsigned *idx,
                             uint32_t *items, unsigned char *sel) {
    unsigned i, d, m, n;
    if (L == 0u || L > 32u || freq == NULL || lens == NULL || idx == NULL
        || items == NULL || sel == NULL)
        return 0;
    for (i = 0; i < nsym; i++)
        idx[i] = i;
    for (i = 1; i < nsym; i++) { /* insertion sort by (freq, sym) */
        unsigned t = idx[i];
        unsigned j = i;
        while (j > 0u
               && (freq[idx[j - 1u]] > freq[t]
                   || (freq[idx[j - 1u]] == freq[t] && idx[j - 1u] > t))) {
            idx[j] = idx[j - 1u];
            j--;
        }
        idx[j] = t;
    }
    m = 0u;
    while (m < nsym && freq[idx[m]] == 0u)
        m++;
    n = nsym - m; /* live symbols */
    if (n == 0u)
        return 0;
    /* Degenerate n==1: single len-1 code (Kraft 0x8000). The S3.8 two-1-bit
     * shape is meta-level (needs a dummy symbol); symbol-level n==1 fails
     * the exact Kraft gate in build -> caller rolls back to RAW (S5.5).
     * Unreachable via mode_trivial all-equal->REPEAT in any case. */
    if (n == 1u) {
        for (i = 0; i < nsym; i++)
            lens[i] = 0u;
        lens[idx[m]] = 1u;
        return 1;
    }
    for (i = 0; i < nsym; i++)
        lens[i] = 0u;
    if (nsym > 256u)
        return 0;
    {
        /* KMT lists with parent pointers; strides sized by nsym. */
        unsigned ss = 3u * nsym; /* triples per level (cap + stage) */
        unsigned bs = 2u * nsym; /* sel bytes per level */
        unsigned nlc[32];
        unsigned cap = 2u * n - 2u;
        unsigned dcur, k;
        for (dcur = 0; dcur < L; dcur++)
            nlc[dcur] = 0u;
        for (i = 0; i < n; i++) { /* lists[0] = sorted singletons */
            items[3u * i] = freq[idx[m + i]];
            items[3u * i + 1u] = LZMESH_U4_PM_EMPTY;
            items[3u * i + 2u] = idx[m + i];
        }
        nlc[0] = n;
        for (dcur = 1; dcur < L; dcur++) {
            uint32_t *pv = items + 3u * ss * (dcur - 1u);
            uint32_t *cu = items + 3u * ss * dcur;
            unsigned nl = nlc[dcur - 1u];
            unsigned np = 0u;
            unsigned a, b, c;
            for (a = 0; a + 1u < nl; a += 2u) { /* package pairs */
                cu[3u * (cap + np)] = pv[3u * a] + pv[3u * (a + 1u)];
                cu[3u * (cap + np) + 1u] = a;
                cu[3u * (cap + np) + 2u] = a + 1u;
                np++;
            }
            a = 0u; /* merge packages with singletons, keep cap */
            b = 0u;
            c = 0u;
            while (c < cap && (a < np || b < n)) {
                uint32_t wa =
                    (a < np) ? cu[3u * (cap + a)] : 0xffffffffu;
                uint32_t wb = (b < n) ? freq[idx[m + b]] : 0xffffffffu;
                if (wa <= wb) {
                    cu[3u * c] = wa;
                    cu[3u * c + 1u] = cu[3u * (cap + a) + 1u];
                    cu[3u * c + 2u] = cu[3u * (cap + a) + 2u];
                    a++;
                } else {
                    cu[3u * c] = wb;
                    cu[3u * c + 1u] = LZMESH_U4_PM_EMPTY;
                    cu[3u * c + 2u] = idx[m + b];
                    b++;
                }
                c++;
            }
            nlc[dcur] = c;
        }
        for (d = 0; d < L; d++) /* selection bitmaps clear */
            for (k = 0; k < bs; k++)
                sel[bs * d + k] = 0u;
        for (k = 0; k < nlc[L - 1u]; k++) /* top level: all selected */
            sel[bs * (L - 1u) + k] = 1u;
        d = L;
        while (d-- > 1u) { /* packages pull children down */
            uint32_t *cu = items + 3u * ss * d;
            for (k = 0; k < nlc[d]; k++) {
                if (sel[bs * d + k] != 0u
                    && cu[3u * k + 1u] != LZMESH_U4_PM_EMPTY) {
                    sel[bs * (d - 1u) + cu[3u * k + 1u]] = 1u;
                    sel[bs * (d - 1u) + cu[3u * k + 2u]] = 1u;
                }
            }
        }
        for (d = 0; d < L; d++) /* len = selected-singleton levels */
            for (k = 0; k < nlc[d]; k++) {
                uint32_t *cu = items + 3u * ss * d;
                if (sel[bs * d + k] != 0u
                    && cu[3u * k + 1u] == LZMESH_U4_PM_EMPTY)
                    lens[cu[3u * k + 2u]]++;
            }
    }
    return 1;
}

/* Forward: Kraft gate used by build below, defined after (C11, no implicits). */
uint32_t lzmesh_u4_kraft16(const unsigned char *lens, unsigned nsym);

/* Caller scratch sizes for lzmesh_u4_build (PERF: carve from u1 scratch;
 * idx needs nsym u32 on top). */
size_t lzmesh_u4_build_items_u32(unsigned nsym, unsigned maxlen) {
    return (size_t)9 * (size_t)nsym * (size_t)maxlen;
}

size_t lzmesh_u4_build_sel_bytes(unsigned nsym, unsigned maxlen) {
    return (size_t)2 * (size_t)nsym * (size_t)maxlen;
}

/* Full build: w23 limit, doubling rebuild on overflow (S3.7). Kraft gate
 * is EXACT (==0x10000 else fail per S3.7); infeasible returns 0 and the
 * caller rolls back to RAW (S5.5). Correct PM output is complete for n>=2
 * so the exact gate passes; n==1 degenerate fails here by design. */
int lzmesh_u4_build(const uint32_t *freq, unsigned nsym, unsigned maxlen,
                    unsigned char *lens, unsigned *idx,
                    uint32_t *items, unsigned char *sel) {
    unsigned lim;
    if (freq == NULL || lens == NULL || nsym == 0u || nsym > 256u
        || maxlen == 0u || maxlen > 32u || idx == NULL || items == NULL
        || sel == NULL)
        return 0;
    lim = lzmesh_u4_w23(nsym, maxlen);
    if (lim == 0u)
        lim = 1u;
    for (;;) {
        if (lzmesh_u4_pm_pass(freq, nsym, lim, lens, idx, items, sel)
            && lzmesh_u4_kraft16(lens, nsym) == LZMESH_U4_KRAFT16)
            return 1;
        if (lim >= maxlen)
            return 0;
        lim *= 2u; /* overflow doubles quantum and rebuilds */
        if (lim > maxlen)
            lim = maxlen;
    }
}

uint32_t lzmesh_u4_kraft16(const unsigned char *lens, unsigned nsym) {
    uint64_t sum = 0u;
    unsigned i;
    if (lens == NULL)
        return 0u;
    for (i = 0; i < nsym; i++)
        if (lens[i] != 0u && lens[i] <= 16u)
            sum += (uint64_t)1 << (16u - lens[i]);
    return sum > 0xffffffffu ? 0xffffffffu : (uint32_t)sum;
}

int lzmesh_u4_kraft_ok(const unsigned char *lens, unsigned nsym) {
    return lzmesh_u4_kraft16(lens, nsym) == LZMESH_U4_KRAFT16;
}

uint32_t lzmesh_u4_rbit(uint32_t code, unsigned len) {
    uint32_t r = 0u;
    unsigned i;
    for (i = 0; i < len; i++) {
        r = (r << 1) | (code & 1u);
        code >>= 1;
    }
    return r;
}

/* Canonical ascending per-length, stored bit-reversed LSB-first (S3.7).
 * Caller gates Kraft-exact via lzmesh_u4_kraft_ok (P-D4/P-E3). */
void lzmesh_u4_canon(const unsigned char *lens, unsigned nsym,
                     uint32_t *codes) {
    uint32_t count[33];
    uint32_t nextcode[33];
    uint32_t code;
    unsigned i, l;
    if (lens == NULL || codes == NULL)
        return;
    for (l = 0; l < 33u; l++)
        count[l] = 0u;
    for (i = 0; i < nsym; i++)
        if (lens[i] != 0u && lens[i] < 33u)
            count[lens[i]]++;
    code = 0u;
    nextcode[0] = 0u;
    for (l = 1; l < 33u; l++) {
        code = (code + count[l - 1u]) << 1;
        nextcode[l] = code;
    }
    for (i = 0; i < nsym; i++)
        codes[i] = 0u;
    for (i = 0; i < nsym; i++) {
        if (lens[i] != 0u && lens[i] < 33u) {
            codes[i] = lzmesh_u4_rbit(nextcode[lens[i]], lens[i]);
            nextcode[lens[i]]++;
        }
    }
}

/* S3.8 meta-degenerate emit-time shape (E-B6): a single distinct meta
 * value MUST map to two 1-bit codes (value + dummy). lzmesh_u4_build
 * rejects n==1 via the exact Kraft gate by design (symbol level rolls
 * back to RAW per S5.5; no meta rollback exists), so merge emit MUST
 * call this instead of build when the 11 meta values are all equal.
 * Dummy = lowest unused index (byte-identity of the pick is merge /
 * battery work). Resulting Kraft: 2*2^15 == 0x10000 exact. */
void lzmesh_u4_meta_single_lens(unsigned value, unsigned nsym,
                                unsigned char *lens) {
    unsigned i;
    if (lens == NULL || nsym < 2u || value >= nsym)
        return;
    for (i = 0; i < nsym; i++)
        lens[i] = 0u;
    lens[value] = 1u;
    for (i = 0; i < nsym; i++)
        if (i != value) {
            lens[i] = 1u;
            break;
        }
}

/* Bit cost of a Huffman candidate over counts (rollback input, S5.5). */
uint64_t lzmesh_u4_huff_bits(const uint32_t *freq,
                             const unsigned char *lens, unsigned nsym) {
    uint64_t bits = 0u;
    unsigned i;
    if (freq == NULL || lens == NULL)
        return 0u;
    bits = LZMESH_U4_HDR73; /* const73 header cost */
    for (i = 0; i < nsym; i++)
        bits += (uint64_t)freq[i] * (uint64_t)lens[i];
    return bits;
}

/* S2.5 footer emit: five u16 LE (endianness G-FOOTEND). */
void lzmesh_u4_footer_emit(uint8_t *p, uint32_t modes, uint32_t tok,
                           uint32_t len, uint32_t lit, uint32_t dist) {
    uint32_t v[5];
    unsigned i;
    if (p == NULL)
        return;
    v[0] = modes;
    v[1] = tok;
    v[2] = len;
    v[3] = lit;
    v[4] = dist;
    for (i = 0; i < 5u; i++) {
        p[2u * i] = (uint8_t)(v[i] & 0xffu);
        p[2u * i + 1u] = (uint8_t)((v[i] >> 8) & 0xffu);
    }
}

/* u9 forward decls (defined below/after; C11 needs decls before
 * want_comp/encode use them). */
int lzmesh_u7_comp_keep(uint32_t D, uint32_t fo, size_t outpos, size_t n,
                        uint32_t ds, uint32_t bo, uint32_t litc,
                        int first_block);
int lzmesh_u9_is_run(const uint8_t *src, size_t size);
int lzmesh_u9_run_layout(size_t size, uint32_t *lenB, uint32_t *modes,
                         uint32_t *bo, uint32_t *fo);
size_t lzmesh_u9_run_emit(uint8_t *dst, size_t dst_capacity, size_t size,
                          uint8_t value);
int lzmesh_u10_l0_layout(size_t size, uint32_t *lenB, uint32_t *modes,
                         uint32_t *bo, uint32_t *fo);
size_t lzmesh_u10_l0_emit(uint8_t *dst, size_t dst_capacity, size_t size,
                          uint8_t value);
int lzmesh_u12_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u12_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* u15 fwd decl (SHAPE-P-LONG G4-relaxed; defined at end, reuses u12 emit). */
int lzmesh_u15_want(const uint8_t *src, size_t size, int level);
/* u18 fwd decls (SHAPE-P-SUF p9..64 + suffix lanes; defined at end). */
int lzmesh_u18_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u18_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* U1 fwd decl (e01 s06 MX-shadow veto; defined after S2, uses S2 macros). */
static int lzmesh_u1_mx_shadowed(const uint8_t *s, size_t n, size_t p);
/* u19 fwd decls (SHAPE-R k-run rep-chain; defined at end). */
int lzmesh_u19_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u19_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* u25 fwd decls (E2b 9B-blocked terminator; defined at end). */
int lzmesh_u25_term_layout(const uint8_t *src, size_t size,
                           uint32_t *tokc, uint32_t *litc,
                           uint32_t *lenC, uint32_t *lenB,
                           uint32_t *modes, uint32_t *bo,
                           uint32_t *fo);
size_t lzmesh_u25_term_emit(uint8_t *dst, size_t dst_capacity,
                            const uint8_t *src, size_t size);
/* u21 fwd decls (SHAPE-R12 lead-short/singleton rep-chain; defined at end). */
int lzmesh_u21_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u21_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level);
/* u23 fwd decls (SHAPE-R12B G==2-gap rep-chain; defined at end). */
int lzmesh_u23_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u23_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* u27 fwd decls (SHAPE-R12CD wide-gap rep-chain; defined at end). */
int lzmesh_u27_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u27_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* R14-mL9 runscan fuse v2: u29/u30/u32 trail layouts each rescan run-starts
 * (3x O(n) + 175KB buf churn per p11 chain). One shared scan; layouts
 * pointer-share it (zero copy) or stock-scan on NULL. Scan loop is the
 * stock layout loop verbatim (same init/cond/body/cap), so buf/nb exact.
 * v1 memcpy: KILLED by PMU (miss_st +3.3% churn); v2 shares the pointer
 * (analyses never write buf: all buf[]= writers are scans). */
typedef struct lzmesh_runs14 {
    size_t buf[21846];
    unsigned nb;
} lzmesh_runs14_t;
static void lzmesh_runs14_scan(const uint8_t *src, size_t size,
                               lzmesh_runs14_t *r) {
    size_t pos, q;
    unsigned nb = 0u;
    pos = 0u;
    while (pos < size && nb < 21846u) {
        r->buf[nb++] = pos;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        pos = q;
    }
    r->nb = nb;
}
/* u29 fwd decls (SHAPE-R-TRAIL trailing-short terminator; defined at end). */
int lzmesh_u29_want(const uint8_t *src, size_t size, int level,
                    const lzmesh_runs14_t *runs14);
size_t lzmesh_u29_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* u30 fwd decls (SHAPE-R-TC1 short-live + trailing term; defined at end). */
int lzmesh_u30_want(const uint8_t *src, size_t size, int level,
                    const lzmesh_runs14_t *runs14);
size_t lzmesh_u30_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* u32 fwd decls (SHAPE-R-TC2 r2-live + trailing term; defined at end). */
int lzmesh_u32_want(const uint8_t *src, size_t size, int level,
                    const lzmesh_runs14_t *runs14);
/* u33 fwd decls (SHAPE-E5 2-token concat-sequential; defined at end). */
int lzmesh_u33_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u33_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* u35 fwd decls (SHAPE-H1 single-terminator lit-HUF; defined at end). */
int lzmesh_u35_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u35_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level);
/* G4: u35m fwd decls (L1 litonly-HUF multi-token dist0; defined at end). */
int lzmesh_u35m_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u35m_emit(uint8_t *dst, size_t dst_capacity,
                        const uint8_t *src, size_t size, int level);
/* H6: u35m6 fwd decls (L1 litonly-HUF multitok dist0 multi-block). */
int lzmesh_u35m6_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u35m6_emit(uint8_t *dst, size_t dst_capacity,
                         const uint8_t *src, size_t size, int level);
/* u36 fwd decls (L0-litonly-HUF single-block; defined at end). */
int lzmesh_u36_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u36_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level);
/* u36m fwd decls (L0-litonly-HUF multi-block; defined at end). */
int lzmesh_u36m_want(const uint8_t *src, size_t size);
size_t lzmesh_u36m_emit(uint8_t *dst, size_t dst_capacity,
                        const uint8_t *src, size_t size, int level);
/* R2-STORE V1 fwd decls (fused L0 TRY; defined after u36m_emit). */
size_t lzmesh_r2_u36_try(uint8_t *dst, size_t dst_capacity,
                         const uint8_t *src, size_t size, int level);
size_t lzmesh_r2_u36m_try(uint8_t *dst, size_t dst_capacity,
                          const uint8_t *src, size_t size, int level);
/* u37 fwd decls (GEN5 general LZ single-COMP; defined at end). */
int lzmesh_u37_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u37_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level);
/* P14-S1: fusion calls build directly (defined at end). */
static size_t lzmesh_u37_build(const uint8_t *src, size_t size,
                               uint8_t *dst, size_t dst_capacity,
                               int level);
/* u38 fwd decls (D2-FD5 e01 single-spike new-dist; defined at end). */
int lzmesh_u38_want(const uint8_t *src, size_t size, int level);
size_t lzmesh_u38_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level);
size_t lzmesh_u32_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size);
/* u16 fwd decls (M12 multi-block L0; defined at end). */
#define LZMESH_U16_LITMAX 62880u /* S4.2 REPEAT-lit ceiling exact */
int lzmesh_u16_l0_nblocks(size_t size);
size_t lzmesh_u16_l0_blocksize(size_t size, unsigned k, unsigned i);
int lzmesh_u16_l0_want(const uint8_t *src, size_t size);
size_t lzmesh_u16_l0_emit(uint8_t *dst, size_t dst_capacity, size_t size,
                          uint8_t value);
/* COMP-attempt seam for lzmesh_encode (u9: run-shape live).
 * v2 CLOSES: G-BOFO (Q1) / G-MAP (Q2) / G-LENENC-long (Q5+Q6) /
 * G-SB (Q32) / G-END (Q25) / G-H2W (Q31) / G-SHORT (Q30) / entry
 * (Q20 full-form; `level` here is BARE post-strip) / anchor (Q28).
 * LIVE: all-equal runs n>=22 at levels 1/5/9 emit single-token
 * rep0 no-H COMP (TIER-1+TIER-2+Q1+Q18 via comp_keep; no size
 * compare); level 0 runs n>=22 emit L0 litonly COMP (u10: tc1/
 * dc0/litc==ds, tok 0xC0 per S3.13+S5.7c, n<=65535 u16-count
 * ceiling U10-CNT); level 0 non-runs n<=16393 emit L0 litonly-HUF
 * COMP (u36: same tc1/dc0/litc==ds frame, u35 lane pack, forced
 * parse — no finding at L0) and n>16393 multi-block litonly-HUF
 * (u36m: u20 schedule, per-block TIER-1, global TIER-2); p-periodic
 * non-runs (p 2..8) at levels 1/5/9
 * emit single-token new-dist no-H COMP (u12 SHAPE-P, same gates);
 * p-periodic 9..64 emit single-token new-dist + suffix lanes (u18
 * SHAPE-P-SUF, same gates + S3.3/S3.11/S5.6 lane pack); k-run
 * non-runs (k>=2, r_i>=3) at levels 1/5/9 emit k-token rep0
 * no-H COMP (u19 SHAPE-R, same gates); k-run non-runs with
 * lead-short/singleton shorts emit rep0 no-H COMP (u21
 * SHAPE-R12, same gates); k-run non-runs with G==2 interior
 * short-gaps emit rep0 no-H COMP (u23 SHAPE-R12B, same gates
 * + S4.3 lit-extra order); k-run non-runs with wide short-gaps
 * (lead L>=3 / interior G>=3) emit rep0 no-H COMP (u27
 * SHAPE-R12CD, same gates + lit esc3 rest + U27-REM9); k-run
 * non-runs with trailing shorts (R in [1,9]) emit rep0 +
 * terminator no-H COMP (u29 SHAPE-R-TRAIL, same gates); k-run
 * non-runs with short-bearing live + trailing R emit rep0 +
 * terminator no-H COMP (u30 SHAPE-R-TC1, same gates); k-run
 * non-runs with r2-bearing live + trailing R emit rep0 +
 * terminator no-H COMP (u32 SHAPE-R-TC2, same gates + esc3).
 * STILL RAW fail-safe (S5.9.a): non-run/non-periodic/non-repchain,
 * n<22 (TIER-2 veto),
 * general COMP (scheduler, R-100 general fo, R-002 F5, Huffman
 * lane/bit packer). U9-R1/R2/R3 need oracle cells at merge. */

/* E2 SPARSEPOS (black-box: single-outlier positional STORE sets, e01).
 * Oracle finder misses on exact (n,pos) sets; predicate takes (n,pos)
 * only (pox value + bg value inert, verified 0x01/0x41/0xFF/0x7F x
 * bg00/42/FF). Sets: n<=22 all S; n23 1-22; n24 2-22; n25 {2}+6-22;
 * n26 10-23; n27 12-23; n28 13-23; n29 14-23; n30 15-19+21-23;
 * n31 16-19+22-23; n32 17-19+23; n33 18-19; n34 {19}; n>=35 none.
 * Replaces D1 G2/G3 stopgaps (left-prefix/distc proxies) for e01.
 * e09 keeps D1 stopgaps (subset shape, right-suffix-biased, own lane). */
static int lzmesh_e2_sparsepos_store(size_t n, size_t pos) {
    if (pos >= n)
        return 0;
    if (n <= 22u)
        return 1;
    if (n == 23u)
        return pos >= 1u ? 1 : 0;
    if (n == 24u)
        return (pos >= 2u && pos <= 22u) ? 1 : 0;
    if (n == 25u)
        return (pos == 2u || (pos >= 6u && pos <= 22u)) ? 1 : 0;
    if (n == 26u)
        return (pos >= 10u && pos <= 23u) ? 1 : 0;
    if (n == 27u)
        return (pos >= 12u && pos <= 23u) ? 1 : 0;
    if (n == 28u)
        return (pos >= 13u && pos <= 23u) ? 1 : 0;
    if (n == 29u)
        return (pos >= 14u && pos <= 23u) ? 1 : 0;
    if (n == 30u)
        return ((pos >= 15u && pos <= 19u)
            || (pos >= 21u && pos <= 23u)) ? 1 : 0;
    if (n == 31u)
        return ((pos >= 16u && pos <= 19u)
            || (pos >= 22u && pos <= 23u)) ? 1 : 0;
    if (n == 32u)
        return ((pos >= 17u && pos <= 19u) || pos == 23u) ? 1 : 0;
    if (n == 33u)
        return (pos == 18u || pos == 19u) ? 1 : 0;
    if (n == 34u)
        return pos == 19u ? 1 : 0;
    return 0;
}

/* Single outlier at any bg: all bytes equal except one. Returns 1 with
 * *ppos = outlier index. All-equal (run) -> 0. Multi -> 0. */
static int lzmesh_e2_single_outlier(const uint8_t *src, size_t size,
                                    size_t *ppos) {
    size_t i, n = 0u, pos = 0u;
    uint8_t bg;
    if (src == NULL || size < 2u || ppos == NULL)
        return 0;
    if (src[0] == src[1]) {
        bg = src[0];
        for (i = 2u; i < size; i++) {
            if (src[i] != bg) {
                n++;
                pos = i;
                if (n > 1u)
                    return 0;
            }
        }
        if (n != 1u)
            return 0;
        *ppos = pos;
        return 1;
    }
    for (i = 2u; i < size; i++) {
        if (src[i] != src[1])
            break;
    }
    if (i == size) {
        *ppos = 0u; /* pos0: head differs, tail uniform */
        return 1;
    }
    for (i = 2u; i < size; i++) {
        if (src[i] != src[0])
            return 0;
    }
    *ppos = 1u; /* pos1: second differs, rest == head */
    return 1;
}

/* === H2 keep-tag (owner: H2; LANE-H2) ===
 * TAG-HUF keep-side (LANE-G1 R4): u37 GEN lit-HUF frames the oracle keeps
 * but the port STOREs via (a) RAW-fo C6/TIER decline in u37_build,
 * (b) D1 G1-bail. Two narrow keep-additions (never stricter: P8-safe):
 * K1 (u37_build, L1 only): when RAW-fo fails C6 but F2 est engages with
 * margin>=5, keep on HUF-fo; G1 must emit (proved at want via temp)
 * else decline. K2 (d1_gen_bail): pass G1-fires whose emitted frame
 * carries Huffman with margin>=5 (w==est on HUF emits).
 * Mined (tmp/h2/sweep.jsonl): 33 G1-fires seeds0-16 n<=64 e01/e09:
 * 12 oC min-margin 5, 21 oS max-margin 4; est0 fires all oS.
 * e09 emits never carry HUF (G1 L1-only) so e09 D1 behavior unchanged.
 * No parse/table change. */
#define LZMESH_H2_MARGIN 5u
static int lzmesh_h2_huf_modes(uint32_t modes) {
    return ((modes & 7u) == LZMESH_U4_MODE_HUFFMAN)
        || (((modes >> 3) & 7u) == LZMESH_U4_MODE_HUFFMAN)
        || (((modes >> 6) & 7u) == LZMESH_U4_MODE_HUFFMAN)
        || (((modes >> 9) & 7u) == LZMESH_U4_MODE_HUFFMAN);
}
/* U5: G1 margin-pass threshold (LANE-U5). Default 5 (H2/K1 threshold);
 * LZMESH_U6_G1M overrides, 0 restores H2-only behavior. */
static int lzmesh_u5_g1_margin(void) {
    static int init = 0, m = 5;
    if (!init) {
        const char *e = getenv("LZMESH_U6_G1M");
        m = (e == NULL) ? 5 : atoi(e);
        init = 1;
    }
    return m;
}

/* === K1 est-margin keep (owner: K1; LANE-K1) ===
 * GEN keep requires F2-est margin>=5 whenever est engages (L5/L9).
 * Mined (tmp/k1/phaseB.jsonl, 1462 est-relevant cells, seeds 0-16):
 * keep-path oS max margin 4 (s12/s15-n55/52-e01 + filed s02-SKIP-m4
 * L5), oC min margin 5 (s16-n57-e09, parse-clean); gatesfail-path oS
 * max margin 1 (s01-n60-e09), oC min margin 15 (D0). Two hooks in
 * u37_build (est-path margin + GATES-fail HUF-fo keep); L1 est-path
 * untouched (H2-owned); E3STOP/D1/MATH/duel/H3/finder untouched. */
#define LZMESH_K1_MARGIN 5u

/* D1 GEN-BAIL (black-box: 16 e01/e09 REGs, oracle STORE / port GEN COMP).
 * Bails C1 GEN path where port over-fires. e01/e09 only. n<=62 only
 * (larger GEN untouched: k-ladder/mixed/large keep). Fail-open: decode
 * failure proceeds (never bails on unknown). Rules:
 * G1 non-sparse lenc>=2 (8 alpha/text REGs n50-62; true R1/s04n57 lenc1).
 * E2 e01 single-outlier positional STORE (replaces G2/G3 stopgaps).
 * e09 keeps G2/G3 stopgaps (subset shape, own lane).
 * C4/u35 (D3) untouched. C2a/C3/C5 kept. */
static int lzmesh_d1_gen_bail(const uint8_t *src, size_t size, int level) {
    size_t i, nz = 0u, ppos = 0u, opos = 0u;
    uint8_t tmp[512];
    size_t w;
    uint32_t ds, bo, fo;
    uint32_t tokc, lenc, litc, distc, modes;
    if (src == NULL || size == 0u || size > 62u)
        return 0;
    for (i = 0u; i < size; i++) {
        if (src[i] != 0u) {
            nz++;
            ppos = i;
        }
    }
    if (level == 1 && lzmesh_e2_single_outlier(src, size, &opos)
        && lzmesh_e2_sparsepos_store(size, opos))
        return 1; /* E2 true rule */
    if (level == 9 && nz == 1u) {
        if (size == 25u && ppos >= 6u && ppos <= 9u)
            return 1; /* G3 kept for e09 */
        if (size == 26u && ppos >= 10u && ppos <= 11u)
            return 1; /* G3 kept for e09 */
    }
    w = lzmesh_u37_emit(tmp, sizeof tmp, src, size, level);
    if (w < 20u || w > sizeof tmp)
        return 0; /* fail-open */
    if (tmp[0] != 0x01u)
        return 0; /* not GEN COMP (STORE/fallback): no bail */
    ds = (uint32_t)tmp[1] | ((uint32_t)tmp[2] << 8)
        | ((uint32_t)tmp[3] << 16) | ((uint32_t)tmp[4] << 24);
    bo = (uint32_t)tmp[5] | ((uint32_t)tmp[6] << 8);
    fo = (uint32_t)tmp[7] | ((uint32_t)tmp[8] << 8);
    if (ds != (uint32_t)size || bo < 9u || fo < bo || (size_t)fo + 10u > w)
        return 0; /* fail-open */
    modes = (uint32_t)tmp[fo] | ((uint32_t)tmp[fo + 1u] << 8);
    tokc = (uint32_t)tmp[fo + 2u] | ((uint32_t)tmp[fo + 3u] << 8);
    lenc = (uint32_t)tmp[fo + 4u] | ((uint32_t)tmp[fo + 5u] << 8);
    litc = (uint32_t)tmp[fo + 6u] | ((uint32_t)tmp[fo + 7u] << 8);
    distc = (uint32_t)tmp[fo + 8u] | ((uint32_t)tmp[fo + 9u] << 8);
    (void)litc;
    (void)bo;
    /* F4: G1 keeps single-take parses (tokc<=2: 1 take + terminator).
     * s12-n51 e01 GEN bytes already oracle-identical (52/52); G1 was
     * the sole diverger. Mined: seeds0-16 n10-64 e01/e09 (3740
     * cells): 17 G1-fires, sole tokc<=2 is s12-n51 e01 oracle-COMP
     * (all oracle-STORE G1-fires tokc>=4 incl D1 REGs, still bail).
     * Holdout seeds17-24 (1760 cells): 0 tokc<=2 fires. */
    if (nz != 1u && lenc >= 2u && tokc > 2u) {
        /* H2 K2: HUF-margin pass (keep-additive; bail default kept). */
        size_t raw = size + (size_t)LZMESH_U1_RAW_OVERHEAD;
        if (lzmesh_h2_huf_modes(modes) && w < raw
            && raw - w >= (size_t)LZMESH_H2_MARGIN)
            return 0; /* HUF frame with margin: keep */
        /* U5: all-RAW margin pass (LANE-U5). G1 bailed oracle-COMP
         * all-RAW frames s12-n50-alphabet-e09 (margin 5) +
         * s21-n54-textlike-e09 holdout (margin 6); port emit bytes
         * oracle-identical in both, G1 sole diverger. Mined
         * seeds0-24 n<=64 e01/e09: G1-fires 2 oC (margins 5,6) vs
         * 13 oS (margins 1-4, all lit-HUF modes=2). Pass margin>=5
         * regardless of HUF (same threshold as H2/K1). Knob
         * LZMESH_U6_G1M (default 5, 0=H2-only). */
        if (w < raw && raw - w >= (size_t)lzmesh_u5_g1_margin()
            && lzmesh_u5_g1_margin() > 0)
            return 0; /* margin>=5 frame: keep */
        return 1; /* G1 */
    }
    if (level == 9 && nz == 1u && (size == 30u || size == 32u)
        && distc >= 1u)
        return 1; /* G2 kept for e09 */
    return 0;
}

/* === I3 u21 veto (owner: I3; LANE-I3) ===
 * Port u21 rep-chain over-claims flat-bg + isolated outliers at e01;
 * oracle goes GEN (13/13 nou21 IDENT on rescore-h STILL-sparse, H4's
 * 0/13 kill reversed by H1 schedule + I3 gap0-visibility: tmp/i3/).
 * Veto zone (black-box sweeps tmp/i3/vetosweep*.py, path detected by
 * base-vs-nou21 behavior): n >= 52 (n51 GEN STOREs, n52+ IDENT),
 * 2..8 outliers over any flat bg (k1/k0 u21-declines or u9-claims;
 * k12 churns GEN-unmatched, capped), min outlier gap >= 4 (gap<=3
 * u21-declines / oracle-other), any values/positions. L1 only.
 * Gated at BOTH want_comp + dispatch (consistency: vetoed cells fall
 * to u37 GEN, never w=0). */
static int lzmesh_i3_u21_bail(const uint8_t *src, size_t size, int level) {
    size_t counts[256], i;
    size_t best = 0u, k, prev, gap;
    int have_prev;
    if (level != 1 || src == NULL || size < 52u)
        return 0;
    for (i = 0u; i < 256u; i++)
        counts[i] = 0u;
    for (i = 0u; i < size; i++)
        counts[src[i]]++;
    for (i = 0u; i < 256u; i++) {
        if (counts[i] > best)
            best = counts[i];
    }
    k = size - best;
    if (k < 2u || k > 8u)
        return 0;
    have_prev = 0;
    prev = 0u;
    for (i = 0u; i < size; i++) {
        if (counts[src[i]] == best)
            continue;
        if (have_prev) {
            gap = i - prev;
            if (gap < 4u)
                return 0;
        }
        have_prev = 1;
        prev = i;
    }
    return 1;
}

int lzmesh_u4_want_comp(const uint8_t *src, size_t size, int level,
                        void *scratch) {
    uint32_t lenB, modes, bo, fo;
    (void)scratch; /* run path is scratch-free (scan + O(1) layout). */
    if (!lzmesh_u3_level_parse_ok(level))
        return 0;
    if (src == NULL || size <= 1u)
        return 0;
    if (size > (size_t)LZMESH_U1_DS_MAX)
        return 0; /* Q23: >MAX rejects (RAW path agrees). */
    if (!lzmesh_u9_is_run(src, size)) {
        /* G3: e01 non-0-bg single-outlier on E2-STORE (n,pos) is
         * oracle-STORE (census 1892/1892 oS, value/bg-free); port
         * u21 rep over-fires there (u21 veto + D1 bail are 0-bg-only
         * on this leg). Force RAW. bg00 untouched (already STORE
         * except flip keeps, which stay u21-COMP). */
        if (level == 1) {
            size_t gopos = 0u;
            if (lzmesh_e2_single_outlier(src, size, &gopos)
                && lzmesh_e2_sparsepos_store(size, gopos)
                && src[gopos == 0u ? 1u : 0u] != 0u)
                return 0;
        }
        if (level == 0) {
            if (lzmesh_u16_l0_nblocks(size) >= 2) /* u20: k>=2 multi */
                return lzmesh_u36m_want(src, size); /* u36m L0-HUF. */
            return lzmesh_u36_want(src, size, level); /* u36 L0-HUF. */
        }
        if (lzmesh_u12_want(src, size, level))
            return 1; /* u12 SHAPE-P (frozen). */
        if (lzmesh_u15_want(src, size, level))
            return 1; /* u15 SHAPE-P-LONG (frozen). */
        if (lzmesh_u18_want(src, size, level))
            return 1; /* u18 P-SUF (frozen). */
        if (lzmesh_u19_want(src, size, level))
            return 1; /* u19 SHAPE-R (frozen). */
        if (level == 1 && lzmesh_u38_want(src, size, level))
            return 1; /* u38 D2-FD5 e01 single-spike (before u21). */
        if (lzmesh_u21_want(src, size, level)
            && !lzmesh_i3_u21_bail(src, size, level))
            return 1; /* u21 SHAPE-R12 (frozen; I3 vetoes GEN-zone). */
        if (lzmesh_u23_want(src, size, level))
            return 1; /* u23 SHAPE-R12B (frozen). */
        if (lzmesh_u27_want(src, size, level))
            return 1; /* u27 SHAPE-R12CD (frozen). */
        if (lzmesh_u29_want(src, size, level, NULL))
            return 1; /* u29 SHAPE-R-TRAIL (frozen). */
        if (lzmesh_u30_want(src, size, level, NULL))
            return 1; /* u30 SHAPE-R-TC1 (frozen). */
        if (lzmesh_u32_want(src, size, level, NULL))
            return 1; /* u32 SHAPE-R-TC2 (frozen). */
        if (lzmesh_u33_want(src, size, level))
            return 1; /* u33 SHAPE-E5 (pack9). */
        if (level == 5 && lzmesh_u37_want(src, size, level))
            return 1; /* u37 GEN5 (general LZ, L5 only). */
        /* H6: u35m6 L1 multi-block litonly multitok dist0 (multi-size). */
        if (lzmesh_u35m6_want(src, size, level))
            return 1; /* u35m6 H6-multiblk. */
        /* G4: u35m L1 multi-token litonly dist0 (before u35 single). */
        if (lzmesh_u35m_want(src, size, level))
            return 1; /* u35m G4-multitok. */
        if (lzmesh_u35_want(src, size, level))
            return 1; /* u35 SHAPE-H1 (frozen). */
        if ((level == 1 || level == 9) && lzmesh_u37_want(src, size, level)
            && !lzmesh_d1_gen_bail(src, size, level))
            return 1; /* C1+D1/E2: u37 GEN5 fwd e01/e09 unless bail. */
        return 0;
    }
    if (level == 0) {
        /* u10 L0 litonly run (S5.7c tc1/dc0/litc==ds, S3.13 tok
         * 0xC0): same measure-then-gate, litc=size (Q18).
         * u16: size>LITMAX single litc breaches S4.2 (M12 both
         * reject) -> multi-block path (R5: any n<=DS_MAX, B2 repeat).
         * u20: E00 budget-cap split (S5.4/P-E2, R4-E2a) -> multi
         * iff schedule says k>=2 (n>=16398; u22: slivers multi +
         * RAW tail, TEST2-only absorb). */
        if (lzmesh_u16_l0_nblocks(size) >= 2)
            return lzmesh_u16_l0_want(src, size);
        if (!lzmesh_u10_l0_layout(size, &lenB, &modes, &bo, &fo))
            return 0;
        (void)lenB;
        (void)modes;
        return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                                   size, (uint32_t)size, bo,
                                   (uint32_t)size, 1);
    }
    if (!lzmesh_u9_run_layout(size, &lenB, &modes, &bo, &fo))
        return 0; /* size<10: no run shape (TIER-2 vetoes anyway). */
    (void)lenB;
    (void)modes;
    /* u9 measure-then-gate (R-100 sidestep for runs): D=ds single
     * block (U9-R3); outpos=fo+10 pre-ff; no size compare (S5.3). */
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u, size,
                               (uint32_t)size, bo, 1u, 1);
}

/* u7 v2 wiring: Q1 header emit + Q17 new-sel + S5.3/Q18 keep-decision.
 * Pure helpers for the merge scheduler; no behavior change (fail-safe
 * RAW stands until parse + lane/bit packer land; u9 run-COMP live). */
void lzmesh_u7_comp_header_emit(uint8_t *p, uint32_t ds, uint32_t bo,
                               uint32_t fo) {
    if (p == NULL)
        return;
    p[0] = 0x01; /* COMP tag (S2.1); header Q1: ds u32LE @1, bo u16LE
                  * @5, fo u16LE @7; block len = fo+10. Caller gates
                  * C2/C4/C5/C6 via lzmesh_u4_comp_gates_ok first. */
    p[1] = (uint8_t)(ds & 0xffu);
    p[2] = (uint8_t)((ds >> 8) & 0xffu);
    p[3] = (uint8_t)((ds >> 16) & 0xffu);
    p[4] = (uint8_t)((ds >> 24) & 0xffu);
    p[5] = (uint8_t)(bo & 0xffu);
    p[6] = (uint8_t)((bo >> 8) & 0xffu);
    p[7] = (uint8_t)(fo & 0xffu);
    p[8] = (uint8_t)((fo >> 8) & 0xffu);
}

uint32_t lzmesh_u7_new_sel(unsigned len_short) {
    /* Q17 (S3.9): new-dist len_short 0..31 -> sel 4..7 (bits 4:3 are
     * len-hi; no sel field for new). Caller packs via
     * lzmesh_u4_token(lit, sel, len_short & 7), gates len_short<=31
     * (Q6: 0..30 direct, 31 escape). */
    return 4u + ((len_short >> 3) & 3u);
}

int lzmesh_u7_comp_keep(uint32_t D, uint32_t fo, size_t outpos, size_t n,
                       uint32_t ds, uint32_t bo, uint32_t litc,
                       int first_block) {
    /* S5.3 + Q1 + Q18: TIER-1 D>fo (D==fo->RAW) + TIER-2 pre-ff
     * outpos<=n + Q1/C6 validity + Q18 first-block litc!=0. Returns 1
     * iff COMP kept, 0 takes RAW. Scheduler supplies D/fo/outpos from
     * speculative emit (R-100 sidestepped by measure-then-gate); no
     * COMP-vs-RAW size compare anywhere. */
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    if (!lzmesh_u3_tier1_comp(D, fo))
        return 0;
    if (!lzmesh_u3_tier2_keep(outpos, n))
        return 0;
    if (first_block && litc == 0u)
        return 0;
    return 1;
}

/* === API glue (owner: u1) === */
/* P11-A fused dispatch (owner: p11-probe): single-evaluation want+dispatch.
 * Bedded (P11 census, Air): every kept u37 encode evaluated the want chain
 * TWICE (want_comp gate + dispatch re-want) + d1_gen_bail twice at n<=62:
 * large u37 = 3 builds/6 parses per encode (4 parses killed, 67%); small
 * kept = 5 builds (4 killed, 80%). Each killed build costs a whole parse
 * (identical code path). This helper evaluates each want at most ONCE and
 * returns the dispatch arm, so dispatch never re-probes. Arm order and
 * gate conditions mirror want_comp + dispatch exactly (same sequence,
 * same short-circuit); wants are pure in (src,size,level)+process-cached
 * env knobs, so one evaluation == two. lzmesh_u4_want_comp kept intact
 * for API compat. */
enum {
    P11_NONE, P11_U36M0, P11_U36_0, P11_U12, P11_U18, P11_U19,
    P11_U38, P11_U21, P11_U23, P11_U27, P11_U29, P11_U30,
    P11_U32, P11_U33, P11_U37_TRY5, P11_U35M6, P11_U35M, P11_U35,
    P11_U37_TRY19, P11_U16_0, P11_U10_0, P11_U9RUN,
    P11_U36_TRY, P11_U36M_TRY /* R2-STORE V1: fused L0 TRY */
};

/* P14-STRUCT S1 (P11-B): u37 want/emit single-build fusion.
 * Bed (Air, bench corpus): every kept u37 encode ran want-build(NULL)
 * + emit-build(dst) = 2 builds/4 parses; want = 43-49% of build time
 * (e01 44/43%, e05 46/45%, e09 49/49% tx/mx). TRY arms run ONE
 * build(dst) (= original emit-build verbatim) and return its bytes.
 * Slow path (build failed, bench rate 0): re-evaluate want for the
 * exact original control flow (fallbacks / L5 chain / RAW).
 * Soundness: builds deterministic in (src,size,level)+cached env
 * (P11 purity precedent); emit!=0 ==> want==1 (h3_multi/single/h2
 * fronts dst-independent); d1 order preserved. */
static size_t p14_raw_emit(uint8_t *dst, size_t dst_capacity,
                           const uint8_t *src, size_t size) {
    /* Single-RAW-block ceiling: ds must fit u32 LE and ds <= MAX. */
    if (size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    /* Framing needs tag + u32 + payload + END = n+6; encode never
     * truncates (S4.5). n+6 cannot wrap: n <= MAX checked above. */
    if (dst_capacity < size + (size_t)LZMESH_U1_RAW_OVERHEAD)
        return 0;
    /* u32 LE layout derived from S1.7 enc1 vector (G7). */
    dst[0] = (uint8_t)LZMESH_U1_TAG_RAW;
    dst[1] = (uint8_t)(size & 0xffu);
    dst[2] = (uint8_t)((size >> 8) & 0xffu);
    dst[3] = (uint8_t)((size >> 16) & 0xffu);
    dst[4] = (uint8_t)((size >> 24) & 0xffu);
    memmove(dst + 5, src, size); /* memmove: alias-safe (G12). */
    dst[5 + size] = (uint8_t)LZMESH_U1_TAG_END;
    return size + (size_t)LZMESH_U1_RAW_OVERHEAD;
}

static size_t p14_u37_try(uint8_t *dst, size_t dst_capacity,
                          const uint8_t *src, size_t size, int level) {
    size_t w = lzmesh_u37_build(src, size, dst, dst_capacity, level);
    if (w != 0u) {
        /* want==1 implied (emit!=0 ==> front passed); d1 as in arm. */
        if (level != 5 && lzmesh_d1_gen_bail(src, size, level))
            return p14_raw_emit(dst, dst_capacity, src, size);
        return w;
    }
    /* Slow: emit-build failed. Recover want verdict (rare). */
    if (!lzmesh_u37_want(src, size, level)) {
        if (level == 5) {
            /* Arm tail exactly as p11_arm post-u37 order. */
            if (lzmesh_u35m6_want(src, size, level))
                return lzmesh_u35m6_emit(dst, dst_capacity, src, size,
                                         level);
            if (lzmesh_u35m_want(src, size, level))
                return lzmesh_u35m_emit(dst, dst_capacity, src, size,
                                        level);
            if (lzmesh_u35_want(src, size, level))
                return lzmesh_u35_emit(dst, dst_capacity, src, size,
                                       level);
        }
        return p14_raw_emit(dst, dst_capacity, src, size);
    }
    if (level != 5 && lzmesh_d1_gen_bail(src, size, level))
        return p14_raw_emit(dst, dst_capacity, src, size);
    /* want==1: original emit (build re-fails deterministically, then
     * u36/u36m fallbacks exactly as before). */
    return lzmesh_u37_emit(dst, dst_capacity, src, size, level);
}
static int lzmesh_p11_arm(const uint8_t *src, size_t size, int level) {
    uint32_t lenB, modes, bo, fo;
    if (!lzmesh_u3_level_parse_ok(level))
        return P11_NONE;
    if (src == NULL || size <= 1u)
        return P11_NONE;
    if (size > (size_t)LZMESH_U1_DS_MAX)
        return P11_NONE;
    if (!lzmesh_u9_is_run(src, size)) {
        if (level == 1) {
            size_t gopos = 0u;
            if (lzmesh_e2_single_outlier(src, size, &gopos)
                && lzmesh_e2_sparsepos_store(size, gopos)
                && src[gopos == 0u ? 1u : 0u] != 0u)
                return P11_NONE;
        }
        if (level == 0) {
            /* R2-STORE V1: TRY subsumes want (probe-fail -> RAW). */
            if (lzmesh_u16_l0_nblocks(size) >= 2)
                return P11_U36M_TRY;
            return P11_U36_TRY;
        }
        if (lzmesh_u12_want(src, size, level))
            return P11_U12;
        if (lzmesh_u15_want(src, size, level))
            return P11_U12;
        if (lzmesh_u18_want(src, size, level))
            return P11_U18;
        if (lzmesh_u19_want(src, size, level))
            return P11_U19;
        if (level == 1 && lzmesh_u38_want(src, size, level))
            return P11_U38;
        if (lzmesh_u21_want(src, size, level)
            && !lzmesh_i3_u21_bail(src, size, level))
            return P11_U21;
        if (lzmesh_u23_want(src, size, level))
            return P11_U23;
        if (lzmesh_u27_want(src, size, level))
            return P11_U27;
        { /* R14-mL9 runscan fuse: one run scan feeds u29/u30/u32. */
            lzmesh_runs14_t runs14;
            lzmesh_runs14_scan(src, size, &runs14);
            if (lzmesh_u29_want(src, size, level, &runs14))
                return P11_U29;
            if (lzmesh_u30_want(src, size, level, &runs14))
                return P11_U30;
            if (lzmesh_u32_want(src, size, level, &runs14))
                return P11_U32;
        }
        if (lzmesh_u33_want(src, size, level))
            return P11_U33;
        if (level == 5)
            return P11_U37_TRY5; /* P14-S1: fused single build. */
        if (lzmesh_u35m6_want(src, size, level))
            return P11_U35M6;
        if (lzmesh_u35m_want(src, size, level))
            return P11_U35M;
        if (lzmesh_u35_want(src, size, level))
            return P11_U35;
        if (level == 1 || level == 9)
            return P11_U37_TRY19; /* P14-S1: fused (d1 in emit). */
        return P11_NONE;
    }
    if (level == 0) {
        if (lzmesh_u16_l0_nblocks(size) >= 2)
            return lzmesh_u16_l0_want(src, size) ? P11_U16_0
                                                  : P11_NONE;
        if (!lzmesh_u10_l0_layout(size, &lenB, &modes, &bo, &fo))
            return P11_NONE;
        (void)lenB;
        (void)modes;
        return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                                   size, (uint32_t)size, bo,
                                   (uint32_t)size, 1) ? P11_U10_0
                                                        : P11_NONE;
    }
    if (!lzmesh_u9_run_layout(size, &lenB, &modes, &bo, &fo))
        return P11_NONE;
    (void)lenB;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u, size,
                               (uint32_t)size, bo, 1u, 1) ? P11_U9RUN
                                                           : P11_NONE;
}
size_t lzmesh_encode(uint8_t *dst, size_t dst_capacity,
                     const uint8_t *src, size_t src_size,
                     void *scratch, int level) {
    (void)scratch; /* RAW fallback is scratch-free; NULL==explicit identical. */

    /* Pre-S0 dispatch: entry matches FULL-form only (S1.2/S1.3/Q20);
     * invalid levels fail 0 before zero-handling (Q21). NOTE: header
     * still documents bare levels (O4 pre-freeze); coordinator must
     * update lzmesh.h. Bare/wide/negative all fail 0 (no masking). */
    if (!lzmesh_u1_level_ok(level))
        return 0;
    level = lzmesh_u1_strip(level); /* bare below strip point (S5.3) */
    if (level < 0)
        return 0;

    /* NULL gates (S1.7/API-SAFETY M1): src NULL legal iff len 0; dst NULL
     * returns 0 (Apple SIGSEGVs when cap suffices; port stays safe, G5). */
    if (src == NULL && src_size != 0)
        return 0;
    if (dst == NULL)
        return 0;

    /* Empty input -> single END byte (S1.7/S2.7). */
    if (src_size == 0) {
        if (dst_capacity < 1)
            return 0;
        dst[0] = (uint8_t)LZMESH_U1_TAG_END;
        return 1;
    }

    /* u9/u10/u12/u18/u19/u21/u23/u27/u29 COMP seam: want_comp is nonzero only
     * for kept run-shape COMP (levels 0/1/5/9, all-equal,
     * TIER-1+TIER-2+Q1+Q18) or kept u12 SHAPE-P (levels 1/5/9,
     * p-periodic 2..8 non-run) or kept u18 SHAPE-P-SUF (levels
     * 1/5/9, p-periodic 9..64 non-run + suffix lanes) or kept u19
     * SHAPE-R (levels 1/5/9, k-run rep-chain) or kept u21
     * SHAPE-R12 (levels 1/5/9, short-bearing rep-chain) or kept u23
     * SHAPE-R12B (levels 1/5/9, G==2-gap rep-chain) or kept u27
     * SHAPE-R12CD (levels 1/5/9, wide-gap rep-chain) or kept u29
     * SHAPE-R-TRAIL (levels 1/5/9, trailing-short rep-chain) or kept u30
     * SHAPE-R-TC1 (levels 1/5/9, short-live + trailing term) or kept u32
     * SHAPE-R-TC2 (levels 1/5/9, r2-live + trailing term). Emit below;
     * everything else falls through to u1 RAW (S5.9.a). General
     * COMP still blocked (scheduler/R-100/R-002/HDRBIT). */
    /* P11-A: single-evaluation dispatch (see lzmesh_p11_arm). Arm!=NONE
     * is exactly want_comp!=0; each emit matches the old dispatch arm. */
    {
        int arm = lzmesh_p11_arm(src, src_size, level);
        if (arm != P11_NONE) {
            size_t w = 0u;
            switch (arm) {
            case P11_U36M0:
                w = lzmesh_u36m_emit(dst, dst_capacity, src, src_size,
                                     level);
                break;
            case P11_U36_0:
                w = lzmesh_u36_emit(dst, dst_capacity, src, src_size,
                                    level);
                break;
            case P11_U36_TRY:
                w = lzmesh_r2_u36_try(dst, dst_capacity, src, src_size,
                                      level);
                break;
            case P11_U36M_TRY:
                w = lzmesh_r2_u36m_try(dst, dst_capacity, src,
                                       src_size, level);
                break;
            case P11_U12:
                w = lzmesh_u12_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U18:
                w = lzmesh_u18_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U19:
                w = lzmesh_u19_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U38:
                w = lzmesh_u38_emit(dst, dst_capacity, src, src_size,
                                    level);
                break;
            case P11_U21:
                w = lzmesh_u21_emit(dst, dst_capacity, src, src_size,
                                    level);
                break;
            case P11_U23:
                w = lzmesh_u23_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U27:
                w = lzmesh_u27_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U29:
                w = lzmesh_u29_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U30:
                w = lzmesh_u30_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U32:
                w = lzmesh_u32_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U33:
                w = lzmesh_u33_emit(dst, dst_capacity, src, src_size);
                break;
            case P11_U37_TRY5:
            case P11_U37_TRY19:
                w = p14_u37_try(dst, dst_capacity, src, src_size,
                                level);
                break;
            case P11_U35M6:
                w = lzmesh_u35m6_emit(dst, dst_capacity, src, src_size,
                                      level);
                break;
            case P11_U35M:
                w = lzmesh_u35m_emit(dst, dst_capacity, src, src_size,
                                     level);
                break;
            case P11_U35:
                w = lzmesh_u35_emit(dst, dst_capacity, src, src_size,
                                    level);
                break;
            case P11_U16_0:
                w = lzmesh_u16_l0_emit(dst, dst_capacity, src_size,
                                       src[0]);
                break;
            case P11_U10_0:
                w = lzmesh_u10_l0_emit(dst, dst_capacity, src_size,
                                       src[0]);
                break;
            case P11_U9RUN:
                w = lzmesh_u9_run_emit(dst, dst_capacity, src_size,
                                       src[0]);
                break;
            default:
                w = 0; /* unreachable: arm!=NONE gated entry. */
                break;
            }
            return w; /* 0 iff cap short (S4.5 no-trunc); never partial. */
        }
    }

    /* P14-S1: RAW tail shared with TRY slow paths (exact motion). */
    return p14_raw_emit(dst, dst_capacity, src, src_size);
}

size_t lzmesh_encode_scratch_size(int level) {
    return lzmesh_u1_scratch_for(level);
}

/* === v2 wiring: COMP header/token/escape/mode helpers (owner: u7) === */
/* Applies Q1 (header widths), Q6/Q17 (token len fields), Q5 (escape
 * chain), Q18 (first-block litc), S5.5 (mode order). Q2 verified in
 * u4 pack/emit (no change). R-100/R-002/scheduler/lane-bit packer
 * still block COMP bytes; want_comp stays fail-safe. */

/* NOTE: lzmesh_u7_comp_header_emit lives in the seam section above
 * (sibling u7-racer copy, Q1-identical bytes); kept once to avoid a
 * duplicate definition. This section holds the rest. */

/* Q1 PROVEN: COMP block length = fo+10. */
uint32_t lzmesh_u7_comp_block_len(uint32_t fo) {
    return fo + 10u;
}

/* Q6 PROVEN rep token: (lit<<6)|(rep<<3)|len_short, rep in {0,1,2}
 * (never 3; caller gates via lzmesh_u4_rep_emittable), len_short
 * 0..6 direct, 7 -> escape (payload via len_escape_write below). */
uint8_t lzmesh_u7_token_rep(unsigned lit, unsigned rep,
                            unsigned len_short) {
    /* u9: lit is 2-bit (S3.9/Q17); &3 matches u4_token (was &7,
     * overflowed into sel for lit>=4). */
    return (uint8_t)(((lit & 3u) << 6) | ((rep & 7u) << 3)
        | (len_short & 7u));
}

/* Q17 PROVEN: new-dist tokens carry 5-bit len_short in token bits
 * 4:0 (bit5 = new flag; bits 4:3 = len-hi, NOT sel). len_short
 * 0..30 direct, 31 -> escape (Q6). u4_sel(new)=4 alone drops
 * len-hi for len_short >= 8; this constructor keeps it. */
uint8_t lzmesh_u7_token_new(unsigned lit, unsigned len_short) {
    /* u9: lit &3 (2-bit grammar; was &7, bit8 silently dropped). */
    return (uint8_t)(((lit & 3u) << 6) | (1u << 5)
        | (len_short & 0x1fu));
}

/* Q5/Q6 PROVEN escape payload: rest = value - escape; rest <= 254
 * -> 1B; rest >= 255 -> marker 0xFF + u32LE(rest). Returns bytes
 * written (1 or 5); out must hold 5B. S3.13 check: mc 16777213,
 * escape 7 -> rest 16777206 -> ff f6 ff ff 00. */
unsigned lzmesh_u7_len_escape_write(uint32_t rest, uint8_t *out) {
    if (out == NULL)
        return 0u;
    if (rest <= 254u) {
        out[0] = (uint8_t)rest;
        return 1u;
    }
    out[0] = 0xffu;
    out[1] = (uint8_t)(rest & 0xffu);
    out[2] = (uint8_t)((rest >> 8) & 0xffu);
    out[3] = (uint8_t)((rest >> 16) & 0xffu);
    out[4] = (uint8_t)((rest >> 24) & 0xffu);
    return 5u;
}

/* Q18 PROVEN: first COMP block with literal_count == 0 MUST reject
 * on decode. Encoder MUST NOT emit it. Later blocks: any litc
 * legal at this gate (replay gates are decoder-side). */
int lzmesh_u7_first_block_litc_ok(uint32_t litc, int is_first) {
    if (is_first && litc == 0u)
        return 0;
    return 1;
}

/* S5.5 mode order per stream: 0 -> RAW; all-equal -> REPEAT;
 * 8n <= n+73 (n <= 10) -> RAW; else HUFFMAN candidate (caller
 * runs speculative HUF, rolls back iff bits >= 8n via
 * lzmesh_u4_huff_rollback). Streams in Q2 lane order:
 * [lit,tok,len,dist]. litc==0 + is_first MUST NOT reach COMP
 * at all (first_block_litc_ok above, Q18). */
void lzmesh_u7_block_modes(const uint32_t count[4], const int all_eq[4],
                           unsigned modes[4]) {
    unsigned i;
    if (count == NULL || all_eq == NULL || modes == NULL)
        return;
    for (i = 0u; i < 4u; i++)
        modes[i] = lzmesh_u4_mode_trivial(count[i], all_eq[i]);
}

/* === run-shape no-H COMP (owner: u9) === */
/* Single-token rep0 run COMP for all-equal inputs (levels 1/5/9).
 * Shape: lit[B]/c1/REPEAT, tok[0x07]/c1/REPEAT, len escape
 * c1-REPEAT (n<=264) or c5-RAW (n>=265), dist/c0/RAW; bo==fo
 * (no-H lane region empty, S3.3/S3.13); footer + END. Gates via
 * lzmesh_u7_comp_keep (S5.3, no size compare). S5.9.a non-parity
 * single parse; byte-identity NOT claimed (U9-R1 order / U9-R2 LE /
 * U9-R3 D-def need oracle cells). PERF: one early-exit O(n) scan +
 * O(1) emit; scratch-free; alias-safe (scan completes before any
 * dst write). */
int lzmesh_u9_is_run(const uint8_t *src, size_t size) {
    size_t i;
    uint8_t v;
    if (src == NULL || size == 0u)
        return 0;
    v = src[0];
    for (i = 1u; i < size; i++)
        if (src[i] != v)
            return 0;
    return 1;
}

int lzmesh_u9_run_layout(size_t size, uint32_t *lenB, uint32_t *modes,
                         uint32_t *bo, uint32_t *fo) {
    uint32_t rest, lb, m;
    int len_eq;
    if (size < 10u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lenB == NULL || modes == NULL || bo == NULL || fo == NULL)
        return 0;
    rest = (uint32_t)size - 10u; /* ml=size-1, mc=size-3, esc 7 */
    lb = rest <= 254u ? 1u : 5u;
    if (lb == 1u) {
        len_eq = 1; /* single value trivially all-equal */
    } else {
        uint8_t b0 = 0xffu;
        uint8_t b1 = (uint8_t)(rest & 0xffu);
        uint8_t b2 = (uint8_t)((rest >> 8) & 0xffu);
        uint8_t b3 = (uint8_t)((rest >> 16) & 0xffu);
        uint8_t b4 = (uint8_t)((rest >> 24) & 0xffu);
        len_eq = (b0 == b1 && b1 == b2 && b2 == b3 && b3 == b4);
    }
    m = lzmesh_u4_mode_trivial(lb, len_eq);
    *modes = lzmesh_u4_modes_pack(LZMESH_U4_MODE_REPEAT, m,
                                  LZMESH_U4_MODE_REPEAT,
                                  LZMESH_U4_MODE_RAW);
    *bo = 9u + 1u + 1u + lb; /* hdr + lit1B + tok1B + len + dist0B */
    *fo = *bo;
    *lenB = lb;
    return 1;
}

size_t lzmesh_u9_run_emit(uint8_t *dst, size_t dst_capacity, size_t size,
                          uint8_t value) {
    uint32_t lenB, modes, bo, fo, ds;
    uint8_t tok, lenb[5];
    size_t need, s;
    if (dst == NULL)
        return 0;
    if (!lzmesh_u9_run_layout(size, &lenB, &modes, &bo, &fo))
        return 0;
    need = (size_t)fo + 10u + 1u; /* block + END */
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0; /* belt-and-braces; comp_keep already gated */
    if (9u + 2u + lenB != bo)
        return 0; /* layout invariant; emit nothing */
    tok = lzmesh_u4_token(0u, 0u, 7u); /* lit0/rep0/esc = 0x07 (Q6+Q18; U7-LIT0: lit field 0 for litc==1, first byte pre-token per MERGE-M7) */
    if (lzmesh_u7_len_escape_write((uint32_t)size - 10u, lenb) != lenB)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    /* Payload in [9,bo): fetch order lit,tok,len (U9-R1 flip site:
     * reorder these stores if the oracle cell rejects). */
    s = 9u;
    dst[s++] = value; /* LIT (REPEAT 1B = first byte, Q18) */
    dst[s++] = tok; /* TOK (REPEAT 1B) */
    if (lenB == 1u) {
        dst[s++] = lenb[0]; /* LEN (REPEAT 1B) */
    } else {
        dst[s++] = lenb[0]; /* LEN (RAW 5B) */
        dst[s++] = lenb[1];
        dst[s++] = lenb[2];
        dst[s++] = lenb[3];
        dst[s++] = lenb[4];
    }
    /* DIST c0: 0 bytes. */
    lzmesh_u4_footer_emit(dst + fo, modes, 1u, lenB, 1u, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === L0 litonly run-shape no-H COMP (owner: u10) === */
/* Single-token L0 COMP for all-equal inputs at level 0 (S5.7c L0
 * row: no match finding, tc1 dc0 litc==ds; S3.13 E00-'A'22 pin).
 * Shape: lit[B]/cn/REPEAT, tok[0xC0]/c1/REPEAT, len escape
 * c1-REPEAT (n<=258) or c5-RAW (n>=259), dist/c0/RAW; bo==fo
 * (no-H lane region empty, S3.3/S3.13); footer + END. Token 0xC0
 * = lit3-esc/rep0/ml2 (Q6/Q16/Q17); lit_run=n-1 via extra=n-4
 * (Q5; C18-forced), rep0 ml2 overhangs exactly 2 -> truncate to
 * ds (Q14/S4.4). Gates via lzmesh_u7_comp_keep (S5.3, no size
 * compare; litc=n passes Q18). n<=65535 u16-count ceiling
 * (U10-CNT); shares U9-R1/R2/R3 assumptions. S5.9.a non-parity
 * single parse. PERF: O(1) past u9 is_run scan; scratch-free. */
int lzmesh_u10_l0_layout(size_t size, uint32_t *lenB, uint32_t *modes,
                         uint32_t *bo, uint32_t *fo) {
    uint32_t rest, lb, m;
    int len_eq;
    if (size < 10u || size > 65535u)
        return 0; /* TIER-2 vetoes <22 anyway; u16 litc (U10-CNT) */
    if (lenB == NULL || modes == NULL || bo == NULL || fo == NULL)
        return 0;
    rest = (uint32_t)size - 4u; /* lit_run=size-1, esc 3 (Q5) */
    lb = rest <= 254u ? 1u : 5u;
    if (lb == 1u) {
        len_eq = 1; /* single value trivially all-equal */
    } else {
        uint8_t b0 = 0xffu;
        uint8_t b1 = (uint8_t)(rest & 0xffu);
        uint8_t b2 = (uint8_t)((rest >> 8) & 0xffu);
        uint8_t b3 = (uint8_t)((rest >> 16) & 0xffu);
        uint8_t b4 = (uint8_t)((rest >> 24) & 0xffu);
        len_eq = (b0 == b1 && b1 == b2 && b2 == b3 && b3 == b4);
    }
    m = lzmesh_u4_mode_trivial(lb, len_eq);
    *modes = lzmesh_u4_modes_pack(LZMESH_U4_MODE_REPEAT, m,
                                  LZMESH_U4_MODE_REPEAT,
                                  LZMESH_U4_MODE_RAW);
    *bo = 9u + 1u + 1u + lb; /* hdr + lit1B + tok1B + len + dist0B */
    *fo = *bo;
    *lenB = lb;
    return 1;
}

size_t lzmesh_u10_l0_emit(uint8_t *dst, size_t dst_capacity, size_t size,
                          uint8_t value) {
    uint32_t lenB, modes, bo, fo, ds;
    uint8_t tok, lenb[5];
    size_t need, s;
    if (dst == NULL)
        return 0;
    if (!lzmesh_u10_l0_layout(size, &lenB, &modes, &bo, &fo))
        return 0;
    need = (size_t)fo + 10u + 1u; /* block + END */
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0; /* belt-and-braces; comp_keep already gated */
    if (9u + 2u + lenB != bo)
        return 0; /* layout invariant; emit nothing */
    tok = lzmesh_u4_token(3u, 0u, 0u); /* lit3-esc/rep0/ml2 = 0xC0 (S3.13) */
    if (lzmesh_u7_len_escape_write((uint32_t)size - 4u, lenb) != lenB)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    /* Payload in [9,bo): fetch order lit,tok,len (U9-R1 CONFIRMED). */
    s = 9u;
    dst[s++] = value; /* LIT (REPEAT 1B = first byte, Q18) */
    dst[s++] = tok; /* TOK (REPEAT 1B) */
    if (lenB == 1u) {
        dst[s++] = lenb[0]; /* LEN (REPEAT 1B) */
    } else {
        dst[s++] = lenb[0]; /* LEN (RAW 5B) */
        dst[s++] = lenb[1];
        dst[s++] = lenb[2];
        dst[s++] = lenb[3];
        dst[s++] = lenb[4];
    }
    /* DIST c0: 0 bytes. */
    lzmesh_u4_footer_emit(dst + fo, modes, 1u, lenB, (uint32_t)size, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === periodic single-new-dist no-H COMP, levels 1/5/9 (owner: u12) === */
/* SHAPE-P: p-periodic non-run input (smallest p in 2..8) emits one
 * new-dist token: lit = first p bytes (c-p RAW), tok =
 * token_new(V1=p-1, mc=n-p-2) c1 REPEAT, len = lit-extra? +
 * len-extra? (c-lc RAW/REPEAT), dist = [p-1] sb0 (d=p) c1 REPEAT;
 * bo==fo (no suffix bits, d<=8), footer (1,lc,p,1) + END. S3.10
 * N35 `7f`+`00` (mc31 ml33) pins this class (period-2 n=35).
 * First token consumes V+1=p lits (M10/M11-forced); total n exact.
 * Parse: single take at p (backext none; +1hash cost always stays;
 * +1/+2rep0 gated dead; floors via u3_take_floor_ok); finder
 * determinism via exact per-table slot check (Q26/Q31, dual-hb).
 * Gates via comp_keep (S5.3, no size compare). S5.9.a single parse;
 * byte-identity NOT claimed (U12-P0/U12-L1VIS/U12-HB/R-002/L9-low-
 * alpha gaps). PERF: early-exit scans + O(1) emit; scratch-free. */
#define LZMESH_U12_PMIN 2u
#define LZMESH_U12_PMAX 8u /* d=p<=8 -> sb0, no suffix (S3.11) */
#define LZMESH_U12_C1 0x995D97CB4C1DB100ULL /* CONST-TABLE #1 (u2-dup) */
#define LZMESH_U12_C2 0x97CB4C1DB1000000ULL /* CONST-TABLE #2 (u2-dup) */
#define LZMESH_U12_C3 0x3779B100U /* CONST-TABLE #3 (u2-dup) */

/* Smallest p in 2..8 with s[i]==s[i-p] for all i>=p; 0 if none.
 * Runs never reach here (want_comp is_run arm first). */
int lzmesh_u12_period(const uint8_t *src, size_t size) {
    unsigned p;
    size_t i;
    if (src == NULL)
        return 0;
    for (p = LZMESH_U12_PMIN; p <= LZMESH_U12_PMAX; p++) {
        if (size < (size_t)p + 2u)
            continue; /* FORMAT min ml>=2 (S3.9.b) */
        for (i = (size_t)p; i < size; i++)
            if (src[i] != src[i - (size_t)p])
                break;
        if (i == size)
            return (int)p;
    }
    return 0;
}

/* 2B rep-d1 head live at q (S5.8 slot0)? Unloadable/pos0 -> 0. */
int lzmesh_u12_rep2_live(const uint8_t *s, size_t n, size_t q) {
    if (s == NULL || q == 0u || q + 2u > n)
        return 0;
    return s[q] == s[q - 1u] && s[q + 1u] == s[q];
}

static uint64_t lzmesh_u12_load(const uint8_t *p, unsigned n) {
    return lzmesh_wl_load_n(p, n); /* LE (S5.7/Q25); P5-W3 word load */
}

static uint32_t lzmesh_u12_h1(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_U12_C1) >> (64u - hb));
}

static uint32_t lzmesh_u12_h2(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_U12_C2) >> (64u - hb));
}

static uint32_t lzmesh_u12_h3(uint32_t w) {
    return (w * LZMESH_U12_C3) >> 20u;
}

/* R3 FIX-A: common-prefix length of an insert head (a, na) vs the
 * query head (b, nb), min-length window. An aliasing insert (i>=1)
 * sharing <3 head bytes with the query can only MISS: head match is
 * correctness-required (unverified slot reads corrupt on every hash
 * collision), and extension-from-0 dies at the first differing byte,
 * below the L5/L9 fresh floor 3. Prefix>=3 may read live -> veto.
 * Min window is conservative vs u64 word-compare (Q31: equal words
 * imply equal min-bytes). L1 floor 6 moot (single probe, hit-gated). */
static unsigned lzmesh_u12_head_prefix(const uint8_t *a, unsigned na,
                                       const uint8_t *b, unsigned nb) {
    unsigned n = na < nb ? na : nb;
    unsigned k = 0u;
    if (a == NULL || b == NULL)
        return 0u;
    while (k < n && a[k] == b[k])
        k++;
    return k;
}

/* Exact finder-slot agreement at take pos p (S5.7/S5.8, Q26/Q31).
 * Inserts at 0..p-1 (dense, pos0 visited U12-P0); query at p must
 * read q=0 (d=p) on every live hash. R3 FIX-A: an insert (i,H) with
 * i>=1 aliasing a live query slot is IGNORABLE iff its head shares
 * <3 bytes with the query head (dead probe under any table-sharing
 * x replacement-policy x verify-semantics combination); a
 * prefix>=3 alias may read live -> reject (RAW). Soundness additionally
 * needs >=1 alias-free live query per hb whose slot's SOLE writer is
 * the same-head i=0 insert (guaranteed (d=p,ml) candidate; every other
 * live probe then yields MISS or the identical candidate, so candidacy
 * is moot and take=(d=p) is proven). L1 collapses to the old rule
 * (single probe: any alias vetoes) minus the i0-absent hole (now
 * vetoed: strictly safer). Big table (h1/h2) and small table (h3)
 * scoped separately. Checked under hb(n) AND hb(n-p) (S5.7d
 * size-input OPEN U12-HB). L1 (h1-only) needs h1 live: n>=p+7;
 * L5/L9 h3 live via floor. */
int lzmesh_u12_slots_clean(const uint8_t *s, size_t n, size_t p,
                           int level) {
    static const unsigned bigL[2] = { 7u, 5u };
    unsigned hbv[2];
    int hbi, q, h;
    size_t i;
    if (s == NULL || p < 2u || p > 8u || n < p + 2u)
        return 0;
    if (level == 1 && p + 7u > n)
        return 0; /* G6: L1 h1 must be loadable at p */
    hbv[0] = lzmesh_u2_hash_bits(n, level);
    hbv[1] = lzmesh_u2_hash_bits(n - p, level);
    for (hbi = 0; hbi < 2; hbi++) {
        unsigned hb = hbv[hbi];
        int hit = 0;
        if (hb == 0u || hb > 21u)
            return 0;
        for (q = 0; q < 2; q++) { /* big-table queries h1,h2 */
            unsigned Lq = bigL[q];
            uint64_t hqp;
            uint32_t S;
            int alias_ge1 = 0, matched_ge1 = 0, w0_same = 0,
                w0_cross = 0;
            if (level == 1 && q > 0)
                break; /* L1 h1-only */
            if (p + Lq > n)
                continue; /* dead query */
            hqp = lzmesh_u12_load(s + p, Lq);
            S = (q == 0) ? lzmesh_u12_h1(hqp, hb)
                : lzmesh_u12_h2(hqp, hb);
            for (i = 0u; i < p; i++) {
                for (h = 0; h < 2; h++) { /* big-table inserts */
                    unsigned Lh = bigL[h];
                    uint32_t T;
                    if (level == 1 && h > 0)
                        break;
                    if (i + Lh > n)
                        continue;
                    T = (h == 0)
                        ? lzmesh_u12_h1(lzmesh_u12_load(s + i, Lh), hb)
                        : lzmesh_u12_h2(lzmesh_u12_load(s + i, Lh), hb);
                    if (T != S)
                        continue;
                    if (i == 0u) {
                        if (h == q)
                            w0_same = 1;
                        else
                            w0_cross = 1;
                        continue;
                    }
                    alias_ge1 = 1;
                    if (lzmesh_u12_head_prefix(s + i, Lh, s + p, Lq)
                        >= 3u)
                        matched_ge1 = 1;
                }
            }
            if (matched_ge1)
                return 0;
            if (!alias_ge1 && w0_same && !w0_cross)
                hit = 1;
        }
        if (level != 1) { /* small-table query h3 */
            uint32_t S, T;
            int alias_ge1 = 0, matched_ge1 = 0, w0_same = 0;
            if (p + 3u > n)
                return 0; /* h3 must be live */
            S = lzmesh_u12_h3((uint32_t)lzmesh_u12_load(s + p, 3u));
            for (i = 0u; i < p; i++) {
                if (i + 3u > n)
                    continue;
                T = lzmesh_u12_h3((uint32_t)lzmesh_u12_load(s + i, 3u));
                if (T != S)
                    continue;
                if (i == 0u) {
                    w0_same = 1;
                    continue;
                }
                alias_ge1 = 1;
                if (lzmesh_u12_head_prefix(s + i, 3u, s + p, 3u) >= 3u)
                    matched_ge1 = 1;
            }
            if (matched_ge1)
                return 0;
            if (!alias_ge1 && w0_same)
                hit = 1;
        }
        if (!hit)
            return 0;
    }
    return 1;
}

int lzmesh_u12_layout(size_t size, unsigned p, uint32_t *lenB,
                      uint32_t *lenC, uint32_t *modes, uint32_t *bo,
                      uint32_t *fo) {
    uint32_t mc, lc, mlen, lenb;
    uint8_t lb[6];
    unsigned li = 0u, i;
    int all_eq = 1;
    if (p < LZMESH_U12_PMIN || p > LZMESH_U12_PMAX
        || size < (size_t)p + 2u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lenB == NULL || lenC == NULL || modes == NULL || bo == NULL
        || fo == NULL)
        return 0;
    if (p - 1u > 2u) /* V1=p-1>2 -> lit esc3, extra p-4 1B */
        lb[li++] = (uint8_t)(p - 4u);
    mc = (uint32_t)size - (uint32_t)p - 2u;
    if (mc > 30u) { /* new-len esc31 (Q6); rest<=254->1B else 5B */
        uint8_t eb[5];
        unsigned eb_n = lzmesh_u7_len_escape_write(mc - 31u, eb);
        for (i = 0u; i < eb_n; i++)
            lb[li++] = eb[i];
    }
    lc = li;
    for (i = 1u; i < li; i++)
        if (lb[i] != lb[0])
            all_eq = 0;
    mlen = lzmesh_u4_mode_trivial(lc, all_eq);
    if (mlen == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* lc<=6: unreachable; no-H only */
    lenb = (mlen == LZMESH_U4_MODE_REPEAT) ? 1u : lc;
    *modes = lzmesh_u4_modes_pack(LZMESH_U4_MODE_REPEAT, mlen,
                                  LZMESH_U4_MODE_RAW,
                                  LZMESH_U4_MODE_REPEAT);
    *bo = 9u + (uint32_t)p + 1u + lenb + 1u; /* hdr+lit+tok+len+dist */
    *fo = *bo;
    *lenB = lenb;
    *lenC = lc;
    return 1;
}

int lzmesh_u12_want(const uint8_t *src, size_t size, int level) {
    int p;
    size_t pp;
    uint32_t ml, lenB, lenC, modes, bo, fo;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    p = lzmesh_u12_period(src, size);
    if (p < 2 || p > 8)
        return 0;
    pp = (size_t)p;
    ml = (uint32_t)size - (uint32_t)p;
    if (!lzmesh_u3_take_floor_ok(level, 0, ml))
        return 0; /* L1 fresh6 nominal (Q34), L5/L9 fresh3 */
    if (lzmesh_u12_rep2_live(src, size, pp)
        || lzmesh_u12_rep2_live(src, size, pp + 1u)
        || lzmesh_u12_rep2_live(src, size, pp + 2u))
        return 0; /* G4: +1/+2 rep0 + candidacy (S5.8) */
    if (!lzmesh_u12_slots_clean(src, size, pp, level))
        return 0; /* finder agreement (Q26/Q31/U12-HB) */
    if (!lzmesh_u12_layout(size, (unsigned)p, &lenB, &lenC, &modes,
                           &bo, &fo))
        return 0;
    (void)lenB;
    (void)lenC;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, (uint32_t)p,
                               1);
}

size_t lzmesh_u12_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    int p;
    uint32_t lenB, lenC, modes, bo, fo, ds, mc;
    uint8_t tok, lb[6];
    size_t need, s;
    unsigned li = 0u, i;
    if (dst == NULL || src == NULL)
        return 0;
    p = lzmesh_u12_period(src, size);
    if (p < 2 || p > 8)
        return 0;
    if (!lzmesh_u12_layout(size, (unsigned)p, &lenB, &lenC, &modes,
                           &bo, &fo))
        return 0;
    need = (size_t)fo + 10u + 1u; /* block + END */
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    if (9u + (uint32_t)p + 1u + lenB + 1u != bo)
        return 0;
    mc = ds - (uint32_t)p - 2u;
    tok = lzmesh_u7_token_new(
        ((unsigned)p - 1u > 2u) ? 3u : (unsigned)p - 1u,
        (mc > 30u) ? 31u : mc);
    if ((unsigned)p - 1u > 2u)
        lb[li++] = (uint8_t)((unsigned)p - 4u);
    if (mc > 30u) {
        uint8_t eb[5];
        unsigned eb_n = lzmesh_u7_len_escape_write(mc - 31u, eb);
        for (i = 0u; i < eb_n; i++)
            lb[li++] = eb[i];
    }
    if (li != lenC)
        return 0;
    if (lenC == 0u && lenB != 0u)
        return 0;
    if (lenC > 0u && lenB != 1u && lenB != li)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    for (i = 0u; i < (unsigned)p; i++)
        dst[s++] = src[i]; /* LIT (RAW pB) */
    dst[s++] = tok; /* TOK (REPEAT 1B) */
    if (lenC > 0u) {
        if (lenB == 1u) {
            dst[s++] = lb[0]; /* LEN (REPEAT 1B) */
        } else {
            for (i = 0u; i < li; i++)
                dst[s++] = lb[i]; /* LEN (RAW lcB) */
        }
    }
    dst[s++] = (uint8_t)((unsigned)p - 1u); /* DIST sym sb0 (d=p) */
    if (s != bo)
        return 0; /* layout invariant; emit nothing */
    lzmesh_u4_footer_emit(dst + fo, modes, 1u, lenC, (uint32_t)p, 1u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === L0 multi-block run COMP, E00 budget-cap split (owner: u16/u20) === */
/* M12 closer (u16): L0 runs n in (62880,65535] emit multi-block
 * COMP + single END. u20 (R4-E2a): even-split REPLACED by S5.4
 * budget schedule (P-E2): ds0=16385 (B0+1, first byte free;
 * R4 o53 pin), ds1=32768 (B1, U20-B1 PROBABLE = px1 min),
 * ds2+=62448 cap (unreachable n<=65535); TEST2 absorb
 * (next<9 -> extend to E; S5.4/S2.9 tails<=8 absorb, >=9 split).
 * u22 (COMP-R7): U20-TAIL13 absorb FALSIFIED - oracle splits
 * tails 9..12 with a RAW tiny-tail block (tail COMP impossible:
 * L0 shape fo12 needs fo<ds strict). n<=16393 single
 * (u10, frozen); slivers r1/r2 in 9..12 multi + RAW tail;
 * n>=16398 multi (<=3 blocks). First block u10-identical
 * (litc=ds0 REPEAT, tok 0xC0, extra=ds0-4, footer
 * 1/lenB/ds0/0). Later blocks no-pre-emit shape (u17 fix for
 * M18 legs 1+2 C18 underuse): litc=ds_i REPEAT, tok 0xC0,
 * extra=ds_i-3, footer 1/lenB_i/ds_i/0. S4.1/Q18 + S3.10
 * first?1:0 read stream-first-only (Entry singular;
 * first-block-only lit0 gate): first 1+lit_run+2 trunc2,
 * later 0+lit_run+2 trunc2; Q14 C18 consumed==litc.
 * Per-block TIER-1 D=ds_i>fo (U16-R3); TIER-2 total
 * pre-ff outpos<=n; S2.8 walk sum==n; S4.2 per-block<=32780.
 * RAW tiny-tail (u22): no TIER-1 (COMP-only gate), bytes in TIER-2.
 * R5 FIX-E: schedule generalizes past 65535 (u20 B2=62448 repeat;
 * T5 closed). S5.9.a single parse. PERF: O(n)
 * is_run scan (once, in want) + O(k) layout/emit; scratch-free. */
#define LZMESH_U20_B0DS 16385u /* block0 ds = B0+1 (S5.4; R4 o53 pin) */
#define LZMESH_U20_B1DS 32768u /* block1 ds = B1 (S5.4 px1 min; U20-B1) */
#define LZMESH_U20_B2DS 62448u /* block2+ ds (S5.4; R5 run pin: n=114688
                                * -> C16385/C32768/C62448/C3087; 200k/300k
                                * repeat 62448) */
#define LZMESH_U22_SPLITMIN 9u /* TEST2 floor: tail>=9 splits (S5.4/S2.9) */
#define LZMESH_U22_RAWMAX 12u /* tail ds<=12 -> RAW tiny-tail (COMP-R7) */

/* Budget-cap split schedule (u20/u22), generalized past 65535 (R5
 * FIX-E/T5 close): stops (consumed) 16385, 49153, 111601, ...;
 * caps 16385 once, 32768 once, then 62448 repeating; TEST2 absorb
 * (remainder <9 folds into the last block) at EVERY stop incl S2+
 * (R5-verified at S2: n=111609 absorbs -> C62456, n=111610 splits
 * -> C62448+R9; S3+ same rule assumed). n<=DS_MAX -> k>=1
 * (k==1 iff n<=16393). Returns block count, 0 on bad input. */
static int lzmesh_u20_nblocks(size_t size) {
    size_t r;
    int k;
    int cap2 = 0; /* 0 -> B1 next, 1 -> B2 repeating */
    if (size < 10u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (size <= 16393u) /* TEST2: tail<9 absorbs -> single */
        return 1;
    r = size - (size_t)LZMESH_U20_B0DS;
    k = 1;
    for (;;) {
        size_t cap = cap2 ? (size_t)LZMESH_U20_B2DS
                          : (size_t)LZMESH_U20_B1DS;
        if (r <= cap)
            return k + 1; /* fits -> last block */
        if (r - cap < (size_t)LZMESH_U22_SPLITMIN)
            return k + 1; /* TEST2 absorb -> last block */
        r -= cap; /* full cap block, continue */
        k++;
        cap2 = 1;
    }
}

/* Block i geometry (u20 walker): *off = byte offset, *bs = block ds.
 * Positional O(1): full-cap blocks follow prefix sums (P0=0,
 * P1=16385, P2=49153, Pj=49153+(j-2)*62448); the last block takes
 * the rest (absorb folds <9 in). Returns 1 ok / 0 bad. */
static int lzmesh_u20_block(size_t size, int k, unsigned i, size_t *off,
                            size_t *bs) {
    size_t p;
    if (off == NULL || bs == NULL || k < 1 || (int)i >= k)
        return 0;
    if (k != lzmesh_u20_nblocks(size))
        return 0;
    if (i == 0u)
        p = 0u;
    else if (i == 1u)
        p = (size_t)LZMESH_U20_B0DS;
    else
        p = (size_t)LZMESH_U20_B0DS + (size_t)LZMESH_U20_B1DS
            + (size_t)(i - 2u) * (size_t)LZMESH_U20_B2DS;
    *off = p;
    if (i == (unsigned)k - 1u) {
        if (size < p)
            return 0;
        *bs = size - p; /* last block: the rest (absorb incl) */
        return *bs != 0u;
    }
    if (i == 0u)
        *bs = (size_t)LZMESH_U20_B0DS;
    else if (i == 1u)
        *bs = (size_t)LZMESH_U20_B1DS;
    else
        *bs = (size_t)LZMESH_U20_B2DS;
    return 1;
}

/* RAW tiny-tail predicate (u22): last scheduled block is RAW iff
 * multi-block with last ds<=12 (post-TEST2 tails are >=9, so
 * exactly 9..12; L0-COMP needs fo12<ds strict). */
static int lzmesh_u22_tail_raw(size_t size) {
    int k = lzmesh_u20_nblocks(size);
    size_t off, bs;
    if (k < 2)
        return 0;
    if (!lzmesh_u20_block(size, k, (unsigned)k - 1u, &off, &bs))
        return 0;
    return bs <= (size_t)LZMESH_U22_RAWMAX;
}

int lzmesh_u16_l0_nblocks(size_t size) {
    return lzmesh_u20_nblocks(size);
}

size_t lzmesh_u16_l0_blocksize(size_t size, unsigned k, unsigned i) {
    size_t off, bs;
    if (!lzmesh_u20_block(size, (int)k, i, &off, &bs))
        return 0u;
    return bs;
}

/* Later-block L0 layout (u17): mirrors u10_l0_layout but rest = bs-3
 * (no pre-emit: 0 + lit_run + ml2 trunc2 == ds; lit_run = extra+3).
 * First block keeps u10 rest = bs-4 (1 + lit_run + ml2 trunc2).
 * Guards + mode order identical to u10 (S5.5 trivial, S2.6). */
int lzmesh_u16_l0_later_layout(size_t size, uint32_t *lenB,
                               uint32_t *modes, uint32_t *bo,
                               uint32_t *fo) {
    uint32_t rest, lb, m;
    int len_eq;
    if (size < 10u || size > 65535u)
        return 0;
    if (lenB == NULL || modes == NULL || bo == NULL || fo == NULL)
        return 0;
    rest = (uint32_t)size - 3u; /* later: no +1 (S4.1/Q18/S3.10) */
    lb = rest <= 254u ? 1u : 5u;
    if (lb == 1u) {
        len_eq = 1;
    } else {
        uint8_t b0 = 0xffu;
        uint8_t b1 = (uint8_t)(rest & 0xffu);
        uint8_t b2 = (uint8_t)((rest >> 8) & 0xffu);
        uint8_t b3 = (uint8_t)((rest >> 16) & 0xffu);
        uint8_t b4 = (uint8_t)((rest >> 24) & 0xffu);
        len_eq = (b0 == b1 && b1 == b2 && b2 == b3 && b3 == b4);
    }
    m = lzmesh_u4_mode_trivial(lb, len_eq);
    *modes = lzmesh_u4_modes_pack(LZMESH_U4_MODE_REPEAT, m,
                                  LZMESH_U4_MODE_REPEAT,
                                  LZMESH_U4_MODE_RAW);
    *bo = 9u + 1u + 1u + lb;
    *fo = *bo;
    *lenB = lb;
    return 1;
}

int lzmesh_u16_l0_want(const uint8_t *src, size_t size) {
    int k;
    unsigned i;
    size_t outpos = 0u;
    if (src == NULL || !lzmesh_u9_is_run(src, size))
        return 0;
    k = lzmesh_u16_l0_nblocks(size);
    if (k < 2)
        return 0; /* single-block owned by frozen u10 path */
    for (i = 0u; i < (unsigned)k; i++) {
        size_t bs = lzmesh_u16_l0_blocksize(size, (unsigned)k, i);
        if (i == (unsigned)k - 1u && lzmesh_u22_tail_raw(size)) {
            if (bs == 0u || bs > (size_t)LZMESH_U1_DS_MAX)
                return 0;
            outpos += bs + 5u; /* RAW tiny-tail: tag + u32 + payload */
            continue;
        }
        uint32_t lenB, modes, bo, fo;
        int ok = (i == 0u)
            ? lzmesh_u10_l0_layout(bs, &lenB, &modes, &bo, &fo)
            : lzmesh_u16_l0_later_layout(bs, &lenB, &modes, &bo, &fo);
        if (!ok)
            return 0;
        (void)lenB;
        (void)modes;
        if (!lzmesh_u4_comp_gates_ok((uint32_t)bs, bo, fo))
            return 0;
        if (!lzmesh_u3_tier1_comp((uint32_t)bs, fo))
            return 0;
        outpos += (size_t)fo + 10u;
    }
    if (!lzmesh_u3_tier2_keep(outpos, size))
        return 0;
    return 1; /* Q18: first litc=bs0!=0 */
}

size_t lzmesh_u16_l0_emit(uint8_t *dst, size_t dst_capacity, size_t size,
                          uint8_t value) {
    int k;
    unsigned i;
    size_t s = 0u, need = 1u; /* +END */
    uint8_t tok, lenb[5];
    if (dst == NULL)
        return 0;
    k = lzmesh_u16_l0_nblocks(size);
    if (k < 2)
        return 0;
    for (i = 0u; i < (unsigned)k; i++) { /* validate-all pass */
        size_t bs = lzmesh_u16_l0_blocksize(size, (unsigned)k, i);
        if (i == (unsigned)k - 1u && lzmesh_u22_tail_raw(size)) {
            if (bs == 0u || bs > (size_t)LZMESH_U1_DS_MAX)
                return 0;
            need += bs + 5u; /* RAW tiny-tail: tag + u32 + payload */
            continue;
        }
        uint32_t lenB, modes, bo, fo, rest;
        int ok = (i == 0u)
            ? lzmesh_u10_l0_layout(bs, &lenB, &modes, &bo, &fo)
            : lzmesh_u16_l0_later_layout(bs, &lenB, &modes, &bo, &fo);
        if (!ok)
            return 0;
        if (!lzmesh_u4_comp_gates_ok((uint32_t)bs, bo, fo))
            return 0;
        if (9u + 2u + lenB != bo)
            return 0;
        rest = (uint32_t)bs - (i == 0u ? 4u : 3u);
        if (lzmesh_u7_len_escape_write(rest, lenb) != lenB)
            return 0;
        need += (size_t)fo + 10u;
    }
    if (dst_capacity < need)
        return 0;
    tok = lzmesh_u4_token(3u, 0u, 0u); /* 0xC0 */
    for (i = 0u; i < (unsigned)k; i++) { /* write pass */
        size_t bs = lzmesh_u16_l0_blocksize(size, (unsigned)k, i);
        if (i == (unsigned)k - 1u && lzmesh_u22_tail_raw(size)) {
            uint32_t rds; /* RAW tiny-tail: tag00 + u32LE + payload */
            size_t j;
            if (bs == 0u || bs > (size_t)LZMESH_U1_DS_MAX)
                return 0;
            rds = (uint32_t)bs;
            dst[s++] = (uint8_t)LZMESH_U1_TAG_RAW;
            dst[s++] = (uint8_t)(rds & 0xffu);
            dst[s++] = (uint8_t)((rds >> 8) & 0xffu);
            dst[s++] = (uint8_t)((rds >> 16) & 0xffu);
            dst[s++] = (uint8_t)((rds >> 24) & 0xffu);
            for (j = 0u; j < bs; j++)
                dst[s++] = value;
            continue;
        }
        uint32_t lenB, modes, bo, fo, ds;
        size_t t;
        int ok = (i == 0u)
            ? lzmesh_u10_l0_layout(bs, &lenB, &modes, &bo, &fo)
            : lzmesh_u16_l0_later_layout(bs, &lenB, &modes, &bo, &fo);
        if (!ok)
            return 0;
        ds = (uint32_t)bs;
        if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
            return 0;
        if (9u + 2u + lenB != bo)
            return 0;
        if (lzmesh_u7_len_escape_write(ds - (i == 0u ? 4u : 3u), lenb)
            != lenB)
            return 0;
        lzmesh_u7_comp_header_emit(dst + s, ds, bo, fo);
        t = s + 9u;
        dst[t++] = value; /* LIT (REPEAT 1B) */
        dst[t++] = tok; /* TOK (REPEAT 1B) */
        if (lenB == 1u) {
            dst[t++] = lenb[0];
        } else {
            dst[t++] = lenb[0];
            dst[t++] = lenb[1];
            dst[t++] = lenb[2];
            dst[t++] = lenb[3];
            dst[t++] = lenb[4];
        }
        if (t != s + bo)
            return 0;
        lzmesh_u4_footer_emit(dst + s + fo, modes, 1u, lenB, ds, 0u);
        s += (size_t)fo + 10u;
    }
    dst[s++] = (uint8_t)LZMESH_U1_TAG_END;
    return s == need ? need : 0;
}

/* === G4-relaxed periodic single-new-dist no-H COMP, levels 1/5/9 (owner: u15) === */
/* SHAPE-P-LONG: SAME bytes as u12 SHAPE-P (p-periodic 2..8 non-run
 * single-token new-dist no-H COMP, bo==fo, footer 1/lc/p/1 + END;
 * emit reuses u12_emit). Gate diff: SKIP u12 G4 (rep2-live at
 * p/p+1/p+2) iff long-take (ml>38, R-026 observed via
 * u3_long_take). S5.8 lazy order backext->long->cost->+1rep0->
 * +1hash->+2rep0: long takes immediately, +1 ignored, so +1/+2
 * rep0 candidacy cannot divert a long period match. u12 G4
 * over-conservative for long. u15 fires ONLY where u12_want==0
 * (want_comp checks u12 first) AND long AND period/floors/slots/
 * layout/comp_keep. Short-G4 stays RAW. Targets G4-class
 * periodics (AABA100). seq/text (Huffman) + L0-periodic + P9/10
 * (suffix lanes) stay RAW. S5.9.a posture; byte-identity NOT
 * claimed (inherit u12 gaps U12-P0/U12-L1VIS/U12-HB/R-002/
 * L9-low-alpha + U15-G4LONG order Q36 + L1 floor Q34). PERF: O(n)
 * period scan (once, after u12 fail) + O(1) gates; scratch-free. */
int lzmesh_u15_want(const uint8_t *src, size_t size, int level) {
    int p;
    size_t pp;
    uint32_t ml, lenB, lenC, modes, bo, fo;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0;
    p = lzmesh_u12_period(src, size);
    if (p < 2 || p > 8)
        return 0;
    pp = (size_t)p;
    ml = (uint32_t)size - (uint32_t)p;
    if (!lzmesh_u3_take_floor_ok(level, 0, ml))
        return 0;
    if (!lzmesh_u3_long_take(ml))
        return 0; /* short: u12 owns G4-clean; G4-shorts stay RAW. */
    /* SKIP G4: long-take short-circuits +1/+2 (S5.8, R-026). */
    if (!lzmesh_u12_slots_clean(src, size, pp, level))
        return 0;
    if (!lzmesh_u12_layout(size, (unsigned)p, &lenB, &lenC, &modes,
                           &bo, &fo))
        return 0;
    (void)lenB;
    (void)lenC;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, (uint32_t)p,
                               1);
}

/* === periodic single-new-dist + suffix lanes no-H COMP, levels 1/5/9 (owner: u18) === */
/* SHAPE-P-SUF: p-periodic non-run input (smallest p in 9..64) emits one
 * new-dist token + suffix via lanes: lit = first p bytes (c-p RAW,
 * forced S5.9.a), tok = token_new(V1=p-1 esc3, mc=n-p-2 [esc31 iff
 * >30]) c1 REPEAT, len = lit-extra (p-4, 1B) + len-extra? (c-lc, lc
 * 1/2/6) REPEAT iff all-equal else RAW, dist = [(sb<<3)|low] c1
 * REPEAT (Q32/S3.11 via u4_dist_split); lane0 = 1B (suf sb-bits
 * LSB-first + PAD1 + B-TAB hi), lanes1-7 empty; index 4B ib=1
 * (01 00 00 08); fo=bo+5; footer (1,lc,p,1) + END. S3.3/S3.6/S6.1
 * (suffix via lanes iff d>=9), S5.6 pads, S2.5/Q2 + C8-C12, S2.6.
 * Gates mirror u12+u15 (floors; G4 iff short, skip iff long R-026;
 * exact slots; comp_keep). Parse inherits u12 bundle (S5.9.a;
 * U12-* gaps + U18-HUF/U18-PADHI; U18-PMAX retired R3 (64; sb3
 * verified). PERF: early-exit scans
 * + O(1) emit; scratch-free. */
#define LZMESH_U18_PMIN 9u
#define LZMESH_U18_PMAX 64u /* R3: 56->64 (sb3 d57..64; lane0 1B fits sb<=7, dist-split general, PAD1+btab(1,k) rule verified byte-identical on 135 corpus p64 cells x e01/e05/e09); pack8: T5 p51 (h6/t5b); bounded lit-RAW */
#define LZMESH_U18_C1 0x995D97CB4C1DB100ULL /* CONST-TABLE #1 (u2-dup) */
#define LZMESH_U18_C2 0x97CB4C1DB1000000ULL /* CONST-TABLE #2 (u2-dup) */
#define LZMESH_U18_C3 0x3779B100U /* CONST-TABLE #3 (u2-dup) */
#define LZMESH_U18_LANEB 1u /* lane0 payload bytes (sb<=7 fits) */
#define LZMESH_U18_IDXB 4u /* index bytes at ib=1 */
#define LZMESH_U18_IB 1u

/* Smallest p in 9..64 with s[i]==s[i-p] for all i>=p; 0 if none.
 * Runs never reach here (want_comp is_run arm first). p<=8 inputs
 * are rejected by the u18_want guard (u12/u15 class), so a hit here
 * is the global smallest period. */
int lzmesh_u18_period(const uint8_t *src, size_t size) {
    unsigned p;
    size_t i;
    if (src == NULL)
        return 0;
    for (p = LZMESH_U18_PMIN; p <= LZMESH_U18_PMAX; p++) {
        if (size < (size_t)p + 2u)
            continue; /* FORMAT min ml>=2 (S3.9.b) */
        for (i = (size_t)p; i < size; i++)
            if (src[i] != src[i - (size_t)p])
                break;
        if (i == size)
            return (int)p;
    }
    return 0;
}

static uint64_t lzmesh_u18_load(const uint8_t *p, unsigned n) {
    return lzmesh_wl_load_n(p, n); /* LE (S5.7/Q25); P5-W3 word load */
}

static uint32_t lzmesh_u18_h1(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_U18_C1) >> (64u - hb));
}

static uint32_t lzmesh_u18_h2(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_U18_C2) >> (64u - hb));
}

static uint32_t lzmesh_u18_h3(uint32_t w) {
    return (w * LZMESH_U18_C3) >> 20u;
}

/* Exact finder-slot agreement at take pos p (S5.7/S5.8, Q26/Q31).
 * u12_slots_clean verbatim for p in 9..64 (u12 func frozen; dup),
 * R3 FIX-A included (head-prefix<3 aliases ignorable + >=1 sole-
 * writer same-head i=0 hit probe per hb; see u12 comment for the
 * soundness case; prefix helper shared per u12_period precedent).
 * Inserts at 0..p-1 (dense, pos0 visited U12-P0); query at p must
 * read q=0 (d=p). Dual-hb (U12-HB). L1 h1-only needs h1 live:
 * n>=p+7 (G6). Also kills harmonic fallthrough: a true sub-period
 * q<p aliases insert 0's slots at i=q (U18-HARM; harmonic aliases
 * share the full head, prefix>=3, still vetoed). */
int lzmesh_u18_slots_clean(const uint8_t *s, size_t n, size_t p,
                           int level) {
    static const unsigned bigL[2] = { 7u, 5u };
    unsigned hbv[2];
    int hbi, q, h;
    size_t i;
    if (s == NULL || p < (size_t)LZMESH_U18_PMIN
        || p > (size_t)LZMESH_U18_PMAX || n < p + 2u)
        return 0;
    if (level == 1 && p + 7u > n)
        return 0; /* G6: L1 h1 must be loadable at p */
    hbv[0] = lzmesh_u2_hash_bits(n, level);
    hbv[1] = lzmesh_u2_hash_bits(n - p, level);
    for (hbi = 0; hbi < 2; hbi++) {
        unsigned hb = hbv[hbi];
        int hit = 0;
        if (hb == 0u || hb > 21u)
            return 0;
        for (q = 0; q < 2; q++) { /* big-table queries h1,h2 */
            unsigned Lq = bigL[q];
            uint64_t hqp;
            uint32_t S;
            int alias_ge1 = 0, matched_ge1 = 0, w0_same = 0,
                w0_cross = 0;
            if (level == 1 && q > 0)
                break; /* L1 h1-only */
            if (p + Lq > n)
                continue; /* dead query */
            hqp = lzmesh_u18_load(s + p, Lq);
            S = (q == 0) ? lzmesh_u18_h1(hqp, hb)
                : lzmesh_u18_h2(hqp, hb);
            for (i = 0u; i < p; i++) {
                for (h = 0; h < 2; h++) { /* big-table inserts */
                    unsigned Lh = bigL[h];
                    uint32_t T;
                    if (level == 1 && h > 0)
                        break;
                    if (i + Lh > n)
                        continue;
                    T = (h == 0)
                        ? lzmesh_u18_h1(lzmesh_u18_load(s + i, Lh), hb)
                        : lzmesh_u18_h2(lzmesh_u18_load(s + i, Lh), hb);
                    if (T != S)
                        continue;
                    if (i == 0u) {
                        if (h == q)
                            w0_same = 1;
                        else
                            w0_cross = 1;
                        continue;
                    }
                    alias_ge1 = 1;
                    if (lzmesh_u12_head_prefix(s + i, Lh, s + p, Lq)
                        >= 3u)
                        matched_ge1 = 1;
                }
            }
            if (matched_ge1)
                return 0;
            if (!alias_ge1 && w0_same && !w0_cross)
                hit = 1;
        }
        if (level != 1) { /* small-table query h3 */
            uint32_t S, T;
            int alias_ge1 = 0, matched_ge1 = 0, w0_same = 0;
            if (p + 3u > n)
                return 0; /* h3 must be live */
            S = lzmesh_u18_h3((uint32_t)lzmesh_u18_load(s + p, 3u));
            for (i = 0u; i < p; i++) {
                if (i + 3u > n)
                    continue;
                T = lzmesh_u18_h3((uint32_t)lzmesh_u18_load(s + i, 3u));
                if (T != S)
                    continue;
                if (i == 0u) {
                    w0_same = 1;
                    continue;
                }
                alias_ge1 = 1;
                if (lzmesh_u12_head_prefix(s + i, 3u, s + p, 3u) >= 3u)
                    matched_ge1 = 1;
            }
            if (matched_ge1)
                return 0;
            if (!alias_ge1 && w0_same)
                hit = 1;
        }
        if (!hit)
            return 0;
    }
    return 1;
}

/* SHAPE-P-SUF layout: len extras [lit-extra p-4 1B]+[len-extra?],
 * modes (lit RAW, tok REPEAT, len trivial, dist REPEAT), bo = header
 * + lit + tok + len + dist-sym, fo = bo + lane1B + index4B. */
int lzmesh_u18_layout(size_t size, unsigned p, uint32_t *lenB,
                      uint32_t *lenC, uint32_t *modes, uint32_t *bo,
                      uint32_t *fo) {
    uint32_t mc, lc, mlen, lenb;
    unsigned sb, low;
    uint32_t suf;
    uint8_t lb[6];
    unsigned li = 0u, i;
    int all_eq = 1;
    if (p < LZMESH_U18_PMIN || p > LZMESH_U18_PMAX
        || size < (size_t)p + 2u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lenB == NULL || lenC == NULL || modes == NULL || bo == NULL
        || fo == NULL)
        return 0;
    lzmesh_u4_dist_split_nc((uint32_t)p, &sb, &low, &suf);
    if (sb == 0u || sb > 7u)
        return 0; /* d>=9 => sb>=1; 1 lane byte fits sb<=7 */
    if (suf >= (1u << sb))
        return 0; /* suf width invariant (S3.11) */
    (void)low;
    lb[li++] = (uint8_t)(p - 4u); /* V1=p-1>2 always: lit esc3 */
    mc = (uint32_t)size - (uint32_t)p - 2u;
    if (mc > 30u) { /* new-len esc31 (Q6); rest<=254->1B else 5B */
        uint8_t eb[5];
        unsigned eb_n = lzmesh_u7_len_escape_write(mc - 31u, eb);
        for (i = 0u; i < eb_n; i++)
            lb[li++] = eb[i];
    }
    lc = li;
    for (i = 1u; i < li; i++)
        if (lb[i] != lb[0])
            all_eq = 0;
    mlen = lzmesh_u4_mode_trivial(lc, all_eq);
    if (mlen == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* lc<=6: unreachable; no-H only */
    lenb = (mlen == LZMESH_U4_MODE_REPEAT) ? 1u : lc;
    *modes = lzmesh_u4_modes_pack(LZMESH_U4_MODE_REPEAT, mlen,
                                  LZMESH_U4_MODE_RAW,
                                  LZMESH_U4_MODE_REPEAT);
    *bo = 9u + (uint32_t)p + 1u + lenb + 1u; /* hdr+lit+tok+len+dist */
    *fo = *bo + LZMESH_U18_LANEB + LZMESH_U18_IDXB; /* +lane+index */
    *lenB = lenb;
    *lenC = lc;
    return 1;
}

int lzmesh_u18_want(const uint8_t *src, size_t size, int level) {
    int p;
    size_t pp;
    uint32_t ml, lenB, lenC, modes, bo, fo;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    if (lzmesh_u12_period(src, size) != 0)
        return 0; /* p<=8 class owned by u12/u15 (G4-shorts stay RAW) */
    p = lzmesh_u18_period(src, size);
    if (p < (int)LZMESH_U18_PMIN || p > (int)LZMESH_U18_PMAX)
        return 0; /* pack8: was hardcoded 9..32, shadowed PMAX */
    pp = (size_t)p;
    ml = (uint32_t)size - (uint32_t)p;
    if (!lzmesh_u3_take_floor_ok(level, 0, ml))
        return 0; /* L1 fresh6 nominal (Q34), L5/L9 fresh3 */
    if (!lzmesh_u3_long_take(ml)
        && (lzmesh_u12_rep2_live(src, size, pp)
            || lzmesh_u12_rep2_live(src, size, pp + 1u)
            || lzmesh_u12_rep2_live(src, size, pp + 2u)))
        return 0; /* G4 iff short (u12); long skips (u15/R-026) */
    if (!lzmesh_u18_slots_clean(src, size, pp, level))
        return 0; /* finder agreement (Q26/Q31/U12-HB) */
    if (level == 1 && lzmesh_u1_mx_shadowed(src, size, pp))
        return 0; /* U1 s06: MX-shadow veto (S2 D3-stop; oracle M@p) */
    if (!lzmesh_u18_layout(size, (unsigned)p, &lenB, &lenC, &modes,
                           &bo, &fo))
        return 0;
    (void)lenB;
    (void)lenC;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, (uint32_t)p,
                               1);
}

size_t lzmesh_u18_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    int p;
    uint32_t lenB, lenC, modes, bo, fo, ds, mc;
    uint8_t tok, lb[6], dsym, lane0, idx[4];
    size_t need, s;
    unsigned li = 0u, i, sb, low;
    uint32_t suf;
    if (dst == NULL || src == NULL)
        return 0;
    if (lzmesh_u12_period(src, size) != 0)
        return 0; /* p<=8 class owned by u12/u15 */
    p = lzmesh_u18_period(src, size);
    if (p < (int)LZMESH_U18_PMIN || p > (int)LZMESH_U18_PMAX)
        return 0; /* pack8: was hardcoded 9..32, shadowed PMAX */
    if (!lzmesh_u18_layout(size, (unsigned)p, &lenB, &lenC, &modes,
                           &bo, &fo))
        return 0;
    need = (size_t)fo + 10u + 1u; /* block + END */
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    if (9u + (uint32_t)p + 1u + lenB + 1u != bo)
        return 0;
    if (fo != bo + LZMESH_U18_LANEB + LZMESH_U18_IDXB)
        return 0;
    mc = ds - (uint32_t)p - 2u;
    tok = lzmesh_u7_token_new(3u, (mc > 30u) ? 31u : mc);
    lb[li++] = (uint8_t)((unsigned)p - 4u); /* lit esc3 extra */
    if (mc > 30u) {
        uint8_t eb[5];
        unsigned eb_n = lzmesh_u7_len_escape_write(mc - 31u, eb);
        for (i = 0u; i < eb_n; i++)
            lb[li++] = eb[i];
    }
    if (li != lenC)
        return 0;
    if (lenC == 0u && lenB != 0u)
        return 0;
    if (lenC > 0u && lenB != 1u && lenB != li)
        return 0;
    lzmesh_u4_dist_split_nc((uint32_t)p, &sb, &low, &suf);
    if (sb == 0u || sb > 7u)
        return 0;
    if (suf >= (1u << sb))
        return 0;
    dsym = (uint8_t)((sb << 3) | (low & 7u)); /* S3.11 symbol */
    { /* lane0: suf sb-bits LSB-first + PAD1 + B-TAB hi (S5.6) */
        unsigned m = sb & 7u; /* C=sb bits, m=C mod 8 */
        unsigned k = 8u - sb; /* L=1 byte */
        uint8_t data;
        uint32_t P;
        if (!lzmesh_u4_k_in_range(k))
            return 0;
        data = (uint8_t)(suf & ((1u << sb) - 1u));
        if (lzmesh_u4_pad0_one(1u, 0u))
            data |= (uint8_t)(1u << m);
        P = lzmesh_u4_btab(1u, k); /* lastL = lane0 byte-len 1 */
        lane0 = lzmesh_u4_lane_final(data, P, m);
    }
    { /* index at END (S3.3): 7 LSB-first fields = lane0..6 byte
       * lens ([1,0,0,0,0,0,0]), lane7 = remainder 0; ib=last>>3;
       * size = max(4,(7*ib+12)>>3) = 4 -> bytes 01 00 00 08. */
        unsigned f[7] = { 1u, 0u, 0u, 0u, 0u, 0u, 0u };
        unsigned ib = LZMESH_U18_IB;
        unsigned idxsz = (7u * ib + 12u) >> 3;
        if (idxsz < 4u)
            idxsz = 4u;
        if (idxsz != LZMESH_U18_IDXB || ib < 1u || ib > 23u)
            return 0;
        idx[0] = 0u;
        idx[1] = 0u;
        idx[2] = 0u;
        idx[3] = 0u;
        for (i = 0u; i < 7u; i++)
            idx[i * ib / 8u] |= (uint8_t)((f[i] & ((1u << ib) - 1u))
                << (i * ib % 8u));
        idx[idxsz - 1u] |= (uint8_t)(ib << 3);
    }
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    for (i = 0u; i < (unsigned)p; i++)
        dst[s++] = src[i]; /* LIT (RAW pB) */
    dst[s++] = tok; /* TOK (REPEAT 1B) */
    if (lenC > 0u) {
        if (lenB == 1u) {
            dst[s++] = lb[0]; /* LEN (REPEAT 1B) */
        } else {
            for (i = 0u; i < li; i++)
                dst[s++] = lb[i]; /* LEN (RAW lcB) */
        }
    }
    dst[s++] = dsym; /* DIST (REPEAT 1B) */
    if (s != bo)
        return 0; /* layout invariant; emit nothing */
    dst[s++] = lane0; /* lane0 payload (suffix + pads) */
    for (i = 0u; i < LZMESH_U18_IDXB; i++)
        dst[s++] = idx[i]; /* index at END (S3.3) */
    if (s != fo)
        return 0;
    lzmesh_u4_footer_emit(dst + fo, modes, 1u, lenC, (uint32_t)p, 1u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === k-run rep-chain no-H COMP, levels 1/5/9 (owner: u19) === */
/* SHAPE-R: k-run input (k>=2, every run r_i>=3) emits k-token rep0
 * chain: lit = run heads (c-k RAW), tok = rep tokens (c-k RAW),
 * len = len extras (c-lc RAW/REPEAT, mc=r_i-3 esc7 iff >6),
 * dist c0 (all rep, like u9); bo==fo (no suffix, d=1 sb0);
 * footer (k,lc,k,0) + END. Token0 lit0 (first-byte pre-emit),
 * token i>0 lit1. Total 1+0+(r0-1)+1+(r1-1)+..==n exact; C13 d1;
 * C18 consumed k==litc; exact end, no terminator. Takes at 1 and
 * each s_i+1 (rep probe table-free; hash NEW always loses cost vs
 * d1-rep; S5.9.a single parse, u9 precedent); takes need rem>=9
 * (S5.4 9B gate, u26 E2c veto: r_last<10 -> RAW). Gates via comp_keep
 * (S5.3, no size compare). Gaps: U19-R12 (runs<3), U19-REP1
 * (+1-true-rep0 order), U19-L9S (L9-short divergence, valid COMP),
 * U12-L1VIS/R-002 inherit. e00-E1 needs Huffman lit (open).
 * PERF: O(n) scans + O(1) emit; scratch-free. */
#define LZMESH_U19_RMIN 3u /* every run >=3 (rep2 floor + 2B head) */

/* Count runs + min run. Returns k (0 on NULL/empty/>MAX). */
unsigned lzmesh_u19_runs(const uint8_t *src, size_t size, unsigned *minr) {
    unsigned k, cur, mn;
    size_t i;
    if (minr != NULL)
        *minr = 0u;
    if (src == NULL || size == 0u)
        return 0u;
    if (size > (size_t)LZMESH_U1_DS_MAX)
        return 0u; /* >MAX rejects (Q23); caller agrees */
    k = 1u;
    cur = 1u;
    mn = (unsigned)size; /* <=MAX, fits */
    for (i = 1u; i < size; i++) {
        if (src[i] == src[i - 1u]) {
            cur++;
        } else {
            k++;
            if (cur < mn)
                mn = cur;
            cur = 1u;
        }
    }
    if (cur < mn)
        mn = cur;
    if (minr != NULL)
        *minr = mn;
    return k;
}

/* P4-N3: fail-fast gate for the (k>=2 && mn>=RMIN) run-split predicate.
 * Returns 1 with out k/mn filled EXACT (the values u19_runs would return)
 * iff kk>=2 && mn>=RMIN; else 0 (outs untouched, may be NULL).
 * Equivalence: mn<RMIN iff some run closes with length<RMIN, decided at
 * that run's end (fail-fast); otherwise the full scan runs u19_runs'
 * identical counter statements, so filled (k,mn) are exact and the
 * predicate matches `u19_runs(...)>=2 && mn>=RMIN` on all inputs. */
int lzmesh_u19_gate(const uint8_t *src, size_t size, unsigned *k,
                    unsigned *mn) {
    unsigned kk, cur, m;
    size_t i;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    kk = 1u;
    cur = 1u;
    m = (unsigned)size; /* <=MAX, fits */
    for (i = 1u; i < size; i++) {
        if (src[i] == src[i - 1u]) {
            cur++;
        } else {
            if (cur < LZMESH_U19_RMIN)
                return 0; /* short run closes: mn decided */
            kk++;
            if (cur < m)
                m = cur;
            cur = 1u;
        }
    }
    if (cur < m)
        m = cur;
    if (kk < 2u || m < LZMESH_U19_RMIN)
        return 0;
    if (k != NULL)
        *k = kk;
    if (mn != NULL)
        *mn = m;
    return 1;
}

int lzmesh_u19_layout(const uint8_t *src, size_t size, uint32_t *k,
                      uint32_t *lenC, uint32_t *lenB, uint32_t *modes,
                      uint32_t *bo, uint32_t *fo) {
    unsigned kk, mn, cur;
    size_t i;
    uint32_t lc, mlen, lenb;
    uint64_t b;
    uint8_t first;
    int have_first, all_eq;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (k == NULL || lenC == NULL || lenB == NULL || modes == NULL
        || bo == NULL || fo == NULL)
        return 0;
    if (!lzmesh_u19_gate(src, size, &kk, &mn))
        return 0;
    lc = 0u;
    have_first = 0;
    all_eq = 1;
    first = 0u;
    cur = 1u;
    for (i = 1u; i <= size; i++) {
        if (i < size && src[i] == src[i - 1u]) {
            cur++;
            continue;
        }
        if (cur < LZMESH_U19_RMIN)
            return 0; /* belt-and-braces; mn gate covers */
        if ((uint32_t)cur - 3u > 6u) { /* mc=r-3 esc7 (Q6) */
            uint8_t eb[5];
            unsigned eb_n, j;
            eb_n = lzmesh_u7_len_escape_write((uint32_t)cur - 3u - 7u,
                                              eb);
            for (j = 0u; j < eb_n; j++) {
                if (!have_first) {
                    first = eb[j];
                    have_first = 1;
                } else if (eb[j] != first) {
                    all_eq = 0;
                }
                lc++;
            }
        }
        cur = 1u;
    }
    mlen = lzmesh_u4_mode_trivial(lc, all_eq);
    if (mlen == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* no-H only */
    /* lit/tok: k>=2 heads/tokens differ => never all-equal. */
    if (lzmesh_u4_mode_trivial((uint32_t)kk, 0)
        == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* k>10 => Huffman lit/tok (open) */
    lenb = (mlen == LZMESH_U4_MODE_REPEAT) ? 1u : lc;
    *modes = lzmesh_u4_modes_pack(LZMESH_U4_MODE_RAW, mlen,
                                  LZMESH_U4_MODE_RAW,
                                  LZMESH_U4_MODE_RAW);
    b = (uint64_t)9 + (uint64_t)kk + (uint64_t)kk + (uint64_t)lenb;
    if (b > (uint64_t)LZMESH_U1_DS_MAX)
        return 0; /* u32-unsafe huge (TIER-2 vetoes anyway) */
    *bo = (uint32_t)b;
    *fo = (uint32_t)b;
    *k = (uint32_t)kk;
    *lenC = lc;
    *lenB = lenb;
    return 1;
}

int lzmesh_u19_want(const uint8_t *src, size_t size, int level) {
    unsigned kk, mn;
    uint32_t k, lenC, lenB, modes, bo, fo;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    if (!lzmesh_u19_gate(src, size, &kk, &mn))
        return 0; /* k==1 owned by u9; short runs U19-R12 */
    { /* u25 E2b: 9B-blocked? terminator/RAW : exact (frozen).
       * u26 E2c veto (rlast<10->RAW) removed (too broad, E2b->E1);
       * blocked+HUF-veto -> RAW (E2c preserved), blocked+keep ->
       * COMP (E2b close). Never exact-end when blocked (9B). */
        unsigned _kk, _mn, _t;
        size_t _pos, _q;
        int _blocked = 0;
        _kk = kk; /* P4-N3: reuse gate-filled exact (gate passed above) */
        _mn = mn;
        if (_kk >= 2u && _mn >= LZMESH_U19_RMIN) {
            _pos = 0u;
            for (_t = 0u; _t < _kk; _t++) {
                size_t _st = _pos;
                size_t _pt = (_t == 0u) ? 1u : _st + 1u;
                _q = _pos + 1u;
                while (_q < size && src[_q] == src[_pos])
                    _q++;
                _pos = _q;
                if (_pt + 9u > size) {
                    _blocked = 1;
                    break;
                }
            }
        }
        if (_blocked) {
            uint32_t t_tokc, t_litc, t_lenC, t_lenB, t_modes, t_bo,
                t_fo;
            if (!lzmesh_u25_term_layout(src, size, &t_tokc, &t_litc,
                                        &t_lenC, &t_lenB, &t_modes,
                                        &t_bo, &t_fo))
                return 0;
            (void)t_tokc;
            (void)t_lenC;
            (void)t_lenB;
            (void)t_modes;
            return lzmesh_u7_comp_keep((uint32_t)size, t_fo,
                                       (size_t)t_fo + 10u, size,
                                       (uint32_t)size, t_bo, t_litc, 1);
        }
    }
    if (!lzmesh_u3_take_floor_ok(level, 1, mn - 1u))
        return 0; /* rep floor on shortest take (min implies all) */
    if (!lzmesh_u19_layout(src, size, &k, &lenC, &lenB, &modes, &bo,
                           &fo))
        return 0;
    (void)k;
    (void)lenC;
    (void)lenB;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, k, 1);
}

size_t lzmesh_u19_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    uint32_t k, lenC, lenB, modes, bo, fo, ds;
    size_t need, s, pos;
    unsigned i;
    if (dst == NULL || src == NULL)
        return 0;
    { /* u25 E2b: 9B-blocked routes to terminator emit. */
        unsigned _kk, _mn, _t;
        size_t _pos, _q;
        int _blocked = 0;
        /* P4-N3: gate-first; re-walk below runs only when gate passes. */
        if (!lzmesh_u19_gate(src, size, &_kk, &_mn)) {
            _kk = 0u;
            _mn = 0u;
        }
        if (_kk >= 2u && _mn >= LZMESH_U19_RMIN) {
            _pos = 0u;
            for (_t = 0u; _t < _kk; _t++) {
                size_t _st = _pos;
                size_t _pt = (_t == 0u) ? 1u : _st + 1u;
                _q = _pos + 1u;
                while (_q < size && src[_q] == src[_pos])
                    _q++;
                _pos = _q;
                if (_pt + 9u > size) {
                    _blocked = 1;
                    break;
                }
            }
        }
        if (_blocked)
            return lzmesh_u25_term_emit(dst, dst_capacity, src, size);
    }
    if (!lzmesh_u19_layout(src, size, &k, &lenC, &lenB, &modes, &bo,
                           &fo))
        return 0;
    need = (size_t)fo + 10u + 1u; /* block + END */
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    if ((size_t)9 + (size_t)k + (size_t)k + (size_t)lenB
        != (size_t)bo)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    pos = 0u; /* LIT: run heads in order. */
    for (i = 0u; i < k; i++) {
        size_t j;
        if (pos >= size)
            return 0;
        dst[s++] = src[pos];
        j = pos + 1u;
        while (j < size && src[j] == src[pos])
            j++;
        pos = j;
    }
    if (pos != size)
        return 0; /* run count mismatch; emit nothing */
    pos = 0u; /* TOK: rep0 tokens (lit0 first, lit1 rest). */
    for (i = 0u; i < k; i++) {
        size_t j;
        unsigned r, litf;
        uint32_t mc;
        if (pos >= size)
            return 0;
        j = pos + 1u;
        while (j < size && src[j] == src[pos])
            j++;
        r = (unsigned)(j - pos);
        if (r < LZMESH_U19_RMIN)
            return 0;
        mc = (uint32_t)r - 3u;
        litf = (i == 0u) ? 0u : 1u;
        dst[s++] = lzmesh_u7_token_rep(litf, 0u, mc > 6u ? 7u : mc);
        pos = j;
    }
    if (lenC > 0u) { /* LEN: extras in token order. */
        if (lenB == 1u) { /* REPEAT: common byte once. */
            int done = 0;
            pos = 0u;
            for (i = 0u; i < k && !done; i++) {
                size_t j;
                unsigned r;
                uint32_t mc;
                if (pos >= size)
                    return 0;
                j = pos + 1u;
                while (j < size && src[j] == src[pos])
                    j++;
                r = (unsigned)(j - pos);
                if (r < LZMESH_U19_RMIN)
                    return 0;
                mc = (uint32_t)r - 3u;
                if (mc > 6u) {
                    uint8_t eb[5];
                    lzmesh_u7_len_escape_write(mc - 7u, eb);
                    dst[s++] = eb[0];
                    done = 1;
                }
                pos = j;
            }
            if (!done)
                return 0;
        } else {
            if (lenB != lenC)
                return 0;
            pos = 0u;
            for (i = 0u; i < k; i++) {
                size_t j;
                unsigned r;
                uint32_t mc;
                if (pos >= size)
                    return 0;
                j = pos + 1u;
                while (j < size && src[j] == src[pos])
                    j++;
                r = (unsigned)(j - pos);
                if (r < LZMESH_U19_RMIN)
                    return 0;
                mc = (uint32_t)r - 3u;
                if (mc > 6u) {
                    uint8_t eb[5];
                    unsigned eb_n, t;
                    eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
                    for (t = 0u; t < eb_n; t++)
                        dst[s++] = eb[t];
                }
                pos = j;
            }
        }
    }
    /* DIST c0: 0 bytes. */
    if (s != bo)
        return 0; /* layout invariant; emit nothing */
    lzmesh_u4_footer_emit(dst + fo, modes, k, lenC, k, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === short-bearing k-run rep-chain no-H COMP, levels 1/5/9 (owner: u21) === */
/* SHAPE-R12 (U19-R12 partial close): k-run input (k>=2, last run
 * r>=3, >=1 short run) emits rep0 chain over LONG runs only: each
 * long run takes at its 2nd byte (rep d1, ml=r-1); short bytes are
 * literals of the following token. Run0 short (r0 in {1,2}) ->
 * first-token lit r0 (u12 p=2/p=3 rule: literals = lit+1, proven);
 * interior singletons (r==1) -> non-first lit2 (U21-LIT2: 1-step
 * extrapolation, formula-consistent, oracle decides per S5.9.a);
 * other tokens u19-verbatim (lit0 first / lit1 rest). Interior
 * r==2, adjacent shorts, trailing shorts -> 0 (need non-first
 * lit-esc / terminator: open U21-R12B). tokc = #long runs;
 * litc = short bytes + tokc; len = per-long-run mc=r-3 esc7 iff
 * >6 (shorts add NO len bytes); dist c0 (all rep); bo==fo;
 * footer (tokc,lenC,litc,0) + END. Total litc+sum ml == n exact;
 * C18 consumed litc; exact end, no terminator. Gates via
 * comp_keep (S5.3, no size compare). Disjoint from u19 (u19 =
 * zero shorts) so u19 bytes freeze by construction. Inherits
 * U19-REP1 (+1-true-rep0 order), U19-L9S (L9-short, valid COMP),
 * U12-L1VIS (L1 VIS assumed; first take P=r0+1 in {1,2,3} is
 * step-1 chain-clean), R-002 (F5 none). PERF: O(n) scans + O(1)
 * emit; scratch-free. */
#define LZMESH_U21_RMIN 3u /* long-run floor (rep2 floor + 2B head) */

/* R38-FOLD (FIX-V) + R115-GFOLD (FIX-W candidate): single-spike 3-run
 * input (long run0 r0>=3, singleton middle, long last run r2 in 3..8)
 * is oracle literal-fold at ALL levels: last token = (lit=3esc, mc=0)
 * + LEN payload (r2-2); LIT gains full run2. 354/354 exact (e01/e05/e09
 * x rl3..8). Multi-spike generalization (R115): nruns>=3, long run0,
 * last-two runs [singleton, R 3..8] folds at L5/L9 ONLY (identical
 * inputs at L1 take oracle extra-toks + DISTs, no fold; level-gate
 * avoids 4 e01 MOVEs). 8/8 exact (e05/e09 x s50/s31/s36/s48).
 * Returns last-run length if fold, else 0. */
static unsigned lzmesh_u21_fold_r2(const uint8_t *src, size_t size,
                                   int level) {
    size_t pos, j;
    unsigned r0, rprev, rlast, nruns;
    if (src == NULL || size <= 1u)
        return 0u;
    nruns = 0u;
    r0 = 0u;
    rprev = 0u;
    rlast = 0u;
    pos = 0u;
    while (pos < size) {
        unsigned r;
        j = pos + 1u;
        while (j < size && src[j] == src[pos])
            j++;
        r = (unsigned)(j - pos);
        if (nruns == 0u)
            r0 = r;
        rprev = rlast;
        rlast = r;
        nruns++;
        pos = j;
    }
    if (nruns < 3u || r0 < 3u || rprev != 1u || rlast < 3u || rlast > 8u)
        return 0u;
    if (nruns != 3u && level == 1)
        return 0u; /* L1 multi-spike: oracle no-fold (toks+DISTs) */
    return rlast;
}


/* Validate + measure. Returns 1 with counts; 0 outside shape. */
int lzmesh_u21_layout(const uint8_t *src, size_t size, uint32_t *tokc,
                      uint32_t *litc, uint32_t *lenC, uint32_t *lenB,
                      uint32_t *modes, uint32_t *bo, uint32_t *fo,
                      uint32_t *minLong, int level) {
    size_t pos, j;
    unsigned tc, lc_lit, nshort, pend, mnL, fr2;
    uint32_t lc, lenb, mlen, mtok, tokb;
    uint64_t b;
    uint8_t first, first_tb;
    int have_first, all_eq, first_tok, have_tb, tok_eq;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (tokc == NULL || litc == NULL || lenC == NULL || lenB == NULL
        || modes == NULL || bo == NULL || fo == NULL
        || minLong == NULL)
        return 0;
    tc = 0u;
    lc_lit = 0u;
    nshort = 0u;
    pend = 0u;
    mnL = (unsigned)size; /* <=MAX, fits */
    lc = 0u;
    have_first = 0;
    all_eq = 1;
    first = 0u;
    first_tok = 1;
    have_tb = 0;
    tok_eq = 1;
    first_tb = 0u;
    pos = 0u;
    while (pos < size) {
        unsigned r, litf;
        int is_last;
        j = pos + 1u;
        while (j < size && src[j] == src[pos])
            j++;
        r = (unsigned)(j - pos);
        is_last = (j == size);
        if (r < LZMESH_U21_RMIN) {
            if (is_last)
                return 0; /* trailing short: terminator open */
            if (!first_tok && r != 1u)
                return 0; /* interior r==2: non-first lit-esc open */
            if (pend > 0u)
                return 0; /* adjacent shorts: lit-esc open */
            pend = r; /* 1..2 (run0) or 1 (interior) */
            nshort++;
        } else {
            if (r < mnL)
                mnL = r;
            if (first_tok)
                litf = pend; /* 0..2 direct (u12 rule) */
            else
                litf = pend + 1u; /* 1..2 direct (U21-LIT2 at 2) */
            if (litf > 2u)
                return 0; /* belt-and-braces */
            { /* tok all-eq fold (S5.5; byte-identical formula to emit) */
                uint8_t tb;
                uint32_t mc0 = (uint32_t)r - 3u;
                tb = lzmesh_u7_token_rep(litf, 0u,
                                         mc0 > 6u ? 7u : mc0);
                if (!have_tb) {
                    first_tb = tb;
                    have_tb = 1;
                } else if (tb != first_tb) {
                    tok_eq = 0;
                }
            }
            tc++;
            lc_lit += pend + 1u;
            if ((uint32_t)r - 3u > 6u) { /* mc=r-3 esc7 (Q6) */
                uint8_t eb[5];
                unsigned eb_n, t;
                eb_n = lzmesh_u7_len_escape_write((uint32_t)r - 3u - 7u,
                                                  eb);
                for (t = 0u; t < eb_n; t++) {
                    if (!have_first) {
                        first = eb[t];
                        have_first = 1;
                    } else if (eb[t] != first) {
                        all_eq = 0;
                    }
                    lc++;
                }
            }
            pend = 0u;
            first_tok = 0;
        }
        pos = j;
    }
    if (tc == 0u || nshort == 0u)
        return 0; /* k==1 u9-owned; zero-short u19-owned */
    fr2 = lzmesh_u21_fold_r2(src, size, level);
    { /* R38-FOLD: LIT gains (r2-1) tail bytes; LEN gains payload byte;
       * lane [esc0>=14, payload<=6] always distinct -> RAW. */
        if (fr2 != 0u) {
            lc_lit += fr2 - 1u;
            lc += 1u;
            all_eq = 0;
        }
    }
    mlen = lzmesh_u4_mode_trivial(lc, all_eq);
    if (mlen == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* no-H only */
    mtok = lzmesh_u4_mode_trivial(tc, tok_eq); /* S5.5 all-eq->REPEAT */
    if (mtok == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* tok HUF (large-k open) */
    if (lzmesh_u4_mode_trivial(lc_lit, 0) == LZMESH_U4_MODE_HUFFMAN
        && !(fr2 != 0u && lc_lit <= 16u))
        return 0; /* lit HUF (open; R115 fold-carve: oracle m0 to 16) */
    lenb = (mlen == LZMESH_U4_MODE_REPEAT) ? 1u : lc;
    tokb = (mtok == LZMESH_U4_MODE_REPEAT) ? 1u : tc;
    *modes = lzmesh_u4_modes_pack(mtok, mlen,
                                  LZMESH_U4_MODE_RAW,
                                  LZMESH_U4_MODE_RAW);
    b = (uint64_t)9 + (uint64_t)lc_lit + (uint64_t)tokb
        + (uint64_t)lenb;
    if (b > (uint64_t)LZMESH_U1_DS_MAX)
        return 0; /* u32-unsafe huge (TIER-2 vetoes anyway) */
    *bo = (uint32_t)b;
    *fo = (uint32_t)b;
    *tokc = tc;
    *litc = lc_lit;
    *lenC = lc;
    *lenB = lenb;
    *minLong = mnL;
    return 1;
}

/* R21-OVER keep veto (FIX-P): single-spike zero-bg inputs with head<=23
 * and tail<=8 are oracle-RAW (R21 overmap 5082 cells: exact; L1 tok=3
 * holes all have tail>=9, excluded). Plus L1-n25-pos[6,21] (minimum-n
 * margin collapse; map-exact, no CC inside). */
/* R23-FRESHVIS val-exception (FIX-R v2): single-spike zone rows where
 * the oracle TAKES (keepset) must skip the R22 wide-zone veto, else the
 * veto flips agree-COMP to RAW (s45-n26 REGRESS, v1 battery 33/1).
 * Map-exact over the whole veto domain (valset: 120 zone rows x 255
 * values, oracle-only, exhaustive; veto domain implies n<=34):
 * tail==9 -> keepset empty (veto unconditional; keeps s66-n29 FIX);
 * n<=25 -> empty (n=25 pos=12 x7 is FIX-P-moot, pre-existing oC-pR);
 * n=26..30 tail>=10 -> {34,37,68,93,102,136,186};
 * n>=31 -> {34,68,93}. */
static int lzmesh_u21_zone_keepval(size_t n, size_t tail, uint8_t v) {
    if (tail < 10u || n < 26u)
        return 0;
    if (n >= 31u)
        return v == 34u || v == 68u || v == 93u;
    return v == 34u || v == 37u || v == 68u || v == 93u || v == 102u
        || v == 136u || v == 186u;
}

static int lzmesh_u21_over_veto(const uint8_t *s, size_t n, int level) {
    size_t i, nz = 0u, pos = 0u;
    unsigned H;
    if (s == NULL)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0;
    H = 23u;
    for (i = 0u; i < n; i++) {
        if (s[i] != 0u) {
            nz++;
            pos = i;
            if (nz > 1u)
                return 0;
        }
    }
    if (nz != 1u)
        return 0;
    if (n - 1u - pos <= 8u && pos <= (size_t)H)
        return 1;
    if (level == 1 && n == 25u && pos >= 6u && pos <= 21u)
        return 1;
    /* R22-FRESHVIS (FIX-R): L1 wide-zone single-spike tail 9..14,
     * head<=23, P<21 (pos<20) is oracle-RAW (R22 freshvis 23-row map
     * + wvary S>=14/d<=8 rule: w=1 fresh needs P>=21 -> excluded;
     * vetmap 90 L1 rows: 78 OVER + 12 moot). L1-ONLY: L5/L9 zone
     * rows COMP-agree -> veto would regress. v2 (R23): skip veto on
     * keepset rows (zone_keepval) -- v1 regressed s45-n26 (33/1). */
    if (level == 1 && n - 1u - pos >= 9u && n - 1u - pos <= 14u
        && pos <= (size_t)H && pos < 20u
        && !lzmesh_u21_zone_keepval(n, n - 1u - pos, s[pos]))
        return 1;
    /* R25-T26T15 (FIX-T): L1 single-spike n=26 tail=15 (pos=10) is
     * oracle-RAW except keepset (R25 t15 A-map: n16..24 all-RAW but
     * port-agree moot; n25/n26 keepset-COMP; n27..48 all-COMP; tails
     * 16..18 take at n26). Single-row veto; keepset exception reuses
     * zone_keepval (n26/t15 -> 7-set, map-exact). L1-only: L5/L9
     * zone rows agree-COMP (V4). */
    if (level == 1 && n == 26u && n - 1u - pos == 15u
        && !lzmesh_u21_zone_keepval(n, n - 1u - pos, s[pos]))
        return 1;
    return 0;
}

/* T2 GEN-VETO (L5 only; LANE-T2): consecutive singleton runs with
 * equal values spanning one gap >= 40 go GEN (oracle NEW-match at the
 * later singleton), not rep-chain. Bedded on the s11-n257 family:
 * gap 39 -> rep (IDENT), gap 40 -> NEW-40 (DIFF); value equality
 * required (unequal pairs stay rep); single-gap required (split gaps
 * 18+22 stay rep at dist 40/60); no match-len floor (NEW-3
 * observed). u37 takes over and is byte-exact there (skip-stored
 * source visible). Returns 1 to veto (decline u21). */
static int lzmesh_t2_u21_gen_veto(const uint8_t *src, size_t size) {
    size_t pos = 0u;
    size_t prev_spos = 0u;
    unsigned prev_sval = 0u;
    int have_prev = 0;
    size_t runs_between = 0u;
    if (src == NULL || size <= 1u)
        return 0;
    while (pos < size) {
        size_t j = pos + 1u;
        unsigned r;
        while (j < size && src[j] == src[pos])
            j++;
        r = (unsigned)(j - pos);
        if (r == 1u) {
            if (have_prev && runs_between == 1u
                && src[pos] == prev_sval
                && pos - prev_spos >= 40u)
                return 1;
            prev_spos = pos;
            prev_sval = src[pos];
            have_prev = 1;
            runs_between = 0u;
        } else {
            runs_between++;
        }
        pos = j;
    }
    return 0;
}

/* V-CONSUME L9 NEWMATCH-VETO (LANE-V-CONSUME): oracle takes a d>1
 * NEW match at a u21 short+long junction (len>=3, mid-input minlen
 * per A-u-pins-1 j177 ladder) instead of the d1 rep-fold; u37 owns
 * those inputs (forced-u37 == oracle byte-exact, s11-n257-sp e09
 * 42B/495406396a848ac9). L9 ONLY: L1-oracle folds the same shape
 * (s11 e01 IDENT via u21), L5 has its own T2 veto. Size<=4096 bounds
 * the O(S*n) match scan. Env LZMESH_VCONSUME_U21VETO=0 restores. */
static int lzmesh_vconsume_u21veto_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_VCONSUME_U21VETO");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
static int lzmesh_vconsume_u21_newmatch_veto(const uint8_t *src,
                                             size_t size) {
    size_t pos = 0u;
    if (src == NULL || size <= 1u || size > 4096u)
        return 0;
    while (pos < size) {
        size_t j = pos + 1u, k;
        unsigned r, r2;
        while (j < size && src[j] == src[pos])
            j++;
        r = (unsigned)(j - pos);
        if (r >= LZMESH_U21_RMIN || pos == 0u || j >= size) {
            pos = j;
            continue;
        }
        k = j + 1u; /* junction? short must be followed by a long */
        while (k < size && src[k] == src[j])
            k++;
        r2 = (unsigned)(k - j);
        if (r2 < LZMESH_U21_RMIN) {
            pos = j;
            continue;
        }
        if (size - pos >= 3u) { /* d>1 len>=3 match starting at pos? */
            size_t s;
            for (s = 0u; s + 1u < pos; s++) {
                if (src[s] == src[pos] && src[s + 1u] == src[pos + 1u]
                    && src[s + 2u] == src[pos + 2u])
                    return 1;
            }
        }
        pos = j;
    }
    return 0;
}

int lzmesh_u21_want(const uint8_t *src, size_t size, int level) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, mnL;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    if (!lzmesh_u21_layout(src, size, &tokc, &litc, &lenC, &lenB,
                           &modes, &bo, &fo, &mnL, level))
        return 0;
    if (level == 5 && lzmesh_t2_u21_gen_veto(src, size))
        return 0; /* T2 GEN-VETO: oracle goes NEW-match, u37 owns. */
    if (level == 9 && lzmesh_vconsume_u21veto_on()
        && lzmesh_vconsume_u21_newmatch_veto(src, size))
        return 0; /* V-CONSUME: junction NEW-match, u37 owns. */
    (void)tokc;
    (void)lenC;
    (void)lenB;
    (void)modes;
    if (!lzmesh_u3_take_floor_ok(level, 1, mnL - 1u))
        return 0; /* rep floor on shortest long take */
    if (lzmesh_u21_over_veto(src, size, level))
        return 0; /* R21-OVER: oracle-RAW zone */
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, litc, 1);
}

size_t lzmesh_u21_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, mnL, ds;
    uint32_t mtok, tokB;
    unsigned fr2;
    size_t need, s, pos, j;
    unsigned ti, lit_w;
    if (dst == NULL || src == NULL)
        return 0;
    if (!lzmesh_u21_layout(src, size, &tokc, &litc, &lenC, &lenB,
                           &modes, &bo, &fo, &mnL, level))
        return 0;
    (void)mnL;
    mtok = (modes >> 3) & 7u; /* S2.5 tok lane (pack order tok,len,..) */
    tokB = (mtok == LZMESH_U4_MODE_REPEAT) ? 1u : tokc;
    { /* R38-FOLD predicate (layout already accepted; want gated). */
        unsigned f = lzmesh_u21_fold_r2(src, size, level);
        fr2 = f;
    }
    need = (size_t)fo + 10u + 1u; /* block + END */
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    if ((size_t)9 + (size_t)litc + (size_t)tokB + (size_t)lenB
        != (size_t)bo)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    pos = 0u; /* LIT: per long run, pending short bytes then head. */
    ti = 0u;
    lit_w = 0u;
    {
        size_t pend_pos = 0u;
        unsigned pend = 0u;
        while (pos < size) {
            unsigned r, t;
            j = pos + 1u;
            while (j < size && src[j] == src[pos])
                j++;
            r = (unsigned)(j - pos);
            if (r < LZMESH_U21_RMIN) {
                pend_pos = pos;
                pend = r;
            } else {
                for (t = 0u; t < pend; t++) {
                    dst[s++] = src[pend_pos + t];
                    lit_w++;
                }
                if (fr2 != 0u && j == size) { /* R38-FOLD: full tail run */
                    for (t = 0u; t < r; t++) {
                        dst[s++] = src[pos + t];
                        lit_w++;
                    }
                } else {
                    dst[s++] = src[pos]; /* head */
                    lit_w++;
                }
                pend = 0u;
                ti++;
            }
            pos = j;
        }
        if (ti != tokc || lit_w != litc)
            return 0;
    }
    pos = 0u; /* TOK: rep0 tokens (first lit=pend, rest lit=pend+1). */
    ti = 0u;
    {
        unsigned pend = 0u;
        int first_tok = 1;
        while (pos < size) {
            unsigned r, litf;
            uint32_t mc;
            j = pos + 1u;
            while (j < size && src[j] == src[pos])
                j++;
            r = (unsigned)(j - pos);
            if (r < LZMESH_U21_RMIN) {
                pend = r;
            } else {
                if (first_tok)
                    litf = pend;
                else
                    litf = pend + 1u;
                if (litf > 2u)
                    return 0;
                mc = (uint32_t)r - 3u;
                if (mtok != LZMESH_U4_MODE_REPEAT || ti == 0u) {
                    if (fr2 != 0u && j == size) /* R38-FOLD last token */
                        dst[s++] = lzmesh_u7_token_rep(3u, 0u, 0u);
                    else
                        dst[s++] = lzmesh_u7_token_rep(litf, 0u,
                                                       mc > 6u ? 7u : mc);
                }
                pend = 0u;
                first_tok = 0;
                ti++;
            }
            pos = j;
        }
        if (ti != tokc)
            return 0;
    }
    if (lenC > 0u) { /* LEN: extras per long run, token order. */
        if (lenB == 1u) { /* REPEAT: common byte once. */
            int done = 0;
            pos = 0u;
            while (pos < size && !done) {
                unsigned r;
                uint32_t mc;
                j = pos + 1u;
                while (j < size && src[j] == src[pos])
                    j++;
                r = (unsigned)(j - pos);
                if (r >= LZMESH_U21_RMIN) {
                    mc = (uint32_t)r - 3u;
                    if (mc > 6u) {
                        uint8_t eb[5];
                        lzmesh_u7_len_escape_write(mc - 7u, eb);
                        dst[s++] = eb[0];
                        done = 1;
                    }
                }
                pos = j;
            }
            if (!done)
                return 0;
        } else {
            unsigned lw = 0u;
            if (lenB != lenC)
                return 0;
            pos = 0u;
            while (pos < size) {
                unsigned r;
                uint32_t mc;
                j = pos + 1u;
                while (j < size && src[j] == src[pos])
                    j++;
                r = (unsigned)(j - pos);
                if (r >= LZMESH_U21_RMIN) {
                    mc = (uint32_t)r - 3u;
                    if (mc > 6u) {
                        uint8_t eb[5];
                        unsigned eb_n, t;
                        eb_n = lzmesh_u7_len_escape_write(mc - 7u,
                                                          eb);
                        for (t = 0u; t < eb_n; t++) {
                            dst[s++] = eb[t];
                            lw++;
                        }
                    }
                }
                pos = j;
            }
            if (fr2 != 0u) { /* R38-FOLD: LEN payload (r2-2), token order */
                dst[s++] = (uint8_t)(fr2 - 2u);
                lw++;
            }
            if (lw != lenC)
                return 0;
        }
    }
    /* DIST c0: 0 bytes. */
    if (s != bo)
        return 0; /* layout invariant; emit nothing */
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenC, litc, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === G==2-gap k-run rep-chain no-H COMP, levels 1/5/9 (owner: u23) === */
/* SHAPE-R12B (U21-R12B partial close): k-run input (k>=2, last run
 * r>=3, >=1 u23-feature) emits rep0 chain over LONG runs only: each
 * long run takes at its 2nd byte (rep d1, ml=r-1); gap bytes are
 * literals of the following token. Lead total L<=2 (direct litL,
 * u12 p=2/p=3 alphabet EXACT; adjacent [1,1] newly allowed);
 * interior gaps G==1 (non-first lit2, U21-LIT2 inherit) or G==2
 * ([r2] or [r1,r1]: non-first lit3 + len-lane extra 0x00 per S3.10
 * esc3, lit_run 3); G>=3 -> 0 (U23-R12C); lead L>=3 -> 0
 * (U23-R12D); trailing short -> 0 (terminator open). Len lane per
 * token [lit-extra?]+[len-extra?] in S4.3 intra-token order (u12
 * layout + u18 emit precedent; oracle-confirmed M16 p=7 which
 * carries both extras); mc=r-3 esc7 iff >6 (1B or 5B); dist c0
 * (all rep); bo==fo; footer (tokc,lenC,litc,0) + END. Total
 * litc+sum ml == n exact; C18 consumed litc; exact end, no
 * terminator. Gates via comp_keep (S5.3, no size compare).
 * Disjoint from u19 (zero shorts) + u21 (rejects G==2 + adjacent
 * shorts) by construction + feat gate. Inherits U19-REP1
 * (+1-true-rep0 order), U19-L9S (L9-short, valid COMP),
 * U12-L1VIS (L1 VIS assumed), R-002 (F5 none). PERF: O(n) scans
 * + O(1) emit; scratch-free. */
#define LZMESH_U23_RMIN 3u /* long-run floor (rep2 floor + 2B head) */

/* Validate + measure. Returns 1 with counts; 0 outside shape. */
int lzmesh_u23_layout(const uint8_t *src, size_t size, uint32_t *tokc,
                      uint32_t *litc, uint32_t *lenC, uint32_t *lenB,
                      uint32_t *modes, uint32_t *bo, uint32_t *fo,
                      uint32_t *minLong) {
    size_t pos, j;
    unsigned tc, lc_lit, mnL;
    unsigned pend, pend_runs;
    uint32_t lc, lenb, mlen, mtok, tokb;
    uint64_t b;
    uint8_t first, first_tb;
    int have_first, all_eq, first_tok, feat, have_tb, tok_eq;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (tokc == NULL || litc == NULL || lenC == NULL || lenB == NULL
        || modes == NULL || bo == NULL || fo == NULL
        || minLong == NULL)
        return 0;
    tc = 0u;
    lc_lit = 0u;
    pend = 0u;
    pend_runs = 0u;
    mnL = (unsigned)size; /* <=MAX, fits */
    lc = 0u;
    have_first = 0;
    all_eq = 1;
    first = 0u;
    first_tok = 1;
    feat = 0;
    have_tb = 0;
    tok_eq = 1;
    first_tb = 0u;
    pos = 0u;
    while (pos < size) {
        unsigned r, litf;
        int is_last;
        j = pos + 1u;
        while (j < size && src[j] == src[pos])
            j++;
        r = (unsigned)(j - pos);
        is_last = (j == size);
        if (r < LZMESH_U23_RMIN) {
            if (is_last)
                return 0; /* trailing short: terminator open */
            pend += r;
            pend_runs++;
            if (pend > 2u)
                return 0; /* G>=3 (U23-R12C) / lead L>=3 (U23-R12D) */
        } else {
            if (r < mnL)
                mnL = r;
            if (first_tok) {
                litf = pend; /* 0..2 direct (u12 rule) */
                if (pend_runs >= 2u)
                    feat = 1; /* adjacent lead shorts: u21 rejects */
            } else if (pend == 2u) {
                litf = 3u; /* G==2 NEW: lit3 + extra 0 */
                feat = 1;
            } else {
                litf = pend + 1u; /* 1..2 (U21-LIT2 at 2) */
            }
            { /* tok all-eq fold (S5.5; byte-identical formula to emit;
                 * R4-T4: u21-style fold, was hardcoded RAW). */
                uint8_t tb;
                uint32_t mc0 = (uint32_t)r - 3u;
                tb = lzmesh_u7_token_rep(litf, 0u,
                                         mc0 > 6u ? 7u : mc0);
                if (!have_tb) {
                    first_tb = tb;
                    have_tb = 1;
                } else if (tb != first_tb) {
                    tok_eq = 0;
                }
            }
            tc++;
            lc_lit += pend + 1u;
            if (litf == 3u) {
                /* lit_run = pend+1 = 3 -> esc3 rest 0 -> 1B 0x00
                 * (Q5/S3.10); first in token order (S4.3). */
                if (!have_first) {
                    first = 0u;
                    have_first = 1;
                } else if (first != 0u) {
                    all_eq = 0;
                }
                lc++;
            }
            if ((uint32_t)r - 3u > 6u) { /* mc=r-3 esc7 (Q6) */
                uint8_t eb[5];
                unsigned eb_n, t;
                eb_n = lzmesh_u7_len_escape_write((uint32_t)r - 3u - 7u,
                                                  eb);
                for (t = 0u; t < eb_n; t++) {
                    if (!have_first) {
                        first = eb[t];
                        have_first = 1;
                    } else if (eb[t] != first) {
                        all_eq = 0;
                    }
                    lc++;
                }
            }
            pend = 0u;
            pend_runs = 0u;
            first_tok = 0;
        }
        pos = j;
    }
    if (tc == 0u || !feat)
        return 0; /* k==1 u9-owned; no-feature u19/u21-owned */
    mlen = lzmesh_u4_mode_trivial(lc, all_eq);
    if (mlen == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* no-H only */
    mtok = lzmesh_u4_mode_trivial(tc, tok_eq); /* S5.5 all-eq->REPEAT */
    if (mtok == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* tok HUF (large-k open) */
    if (lzmesh_u4_mode_trivial(lc_lit, 0) == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* lit HUF (open) */
    lenb = (mlen == LZMESH_U4_MODE_REPEAT) ? 1u : lc;
    tokb = (mtok == LZMESH_U4_MODE_REPEAT) ? 1u : tc;
    *modes = lzmesh_u4_modes_pack(mtok, mlen,
                                  LZMESH_U4_MODE_RAW,
                                  LZMESH_U4_MODE_RAW);
    b = (uint64_t)9 + (uint64_t)lc_lit + (uint64_t)tokb
        + (uint64_t)lenb;
    if (b > (uint64_t)LZMESH_U1_DS_MAX)
        return 0; /* u32-unsafe huge (TIER-2 vetoes anyway) */
    *bo = (uint32_t)b;
    *fo = (uint32_t)b;
    *tokc = tc;
    *litc = lc_lit;
    *lenC = lc;
    *lenB = lenb;
    *minLong = mnL;
    return 1;
}

int lzmesh_u23_want(const uint8_t *src, size_t size, int level) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, mnL;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    if (!lzmesh_u23_layout(src, size, &tokc, &litc, &lenC, &lenB,
                           &modes, &bo, &fo, &mnL))
        return 0;
    (void)tokc;
    (void)lenC;
    (void)lenB;
    (void)modes;
    if (!lzmesh_u3_take_floor_ok(level, 1, mnL - 1u))
        return 0; /* rep floor on shortest long take */
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, litc, 1);
}

size_t lzmesh_u23_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, mnL, ds;
    uint32_t mtok, tokB;
    size_t need, s, pos, j;
    unsigned ti, lit_w;
    if (dst == NULL || src == NULL)
        return 0;
    if (!lzmesh_u23_layout(src, size, &tokc, &litc, &lenC, &lenB,
                           &modes, &bo, &fo, &mnL))
        return 0;
    (void)mnL;
    mtok = (modes >> 3) & 7u; /* S2.5 tok lane (pack order tok,len,..) */
    tokB = (mtok == LZMESH_U4_MODE_REPEAT) ? 1u : tokc;
    need = (size_t)fo + 10u + 1u; /* block + END */
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    if ((size_t)9 + (size_t)litc + (size_t)tokB + (size_t)lenB
        != (size_t)bo)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    pos = 0u; /* LIT: per long run, pending gap bytes then head. */
    ti = 0u;
    lit_w = 0u;
    {
        size_t gap_start = 0u;
        unsigned pend = 0u;
        unsigned pend_runs = 0u;
        while (pos < size) {
            unsigned r, t;
            j = pos + 1u;
            while (j < size && src[j] == src[pos])
                j++;
            r = (unsigned)(j - pos);
            if (r < LZMESH_U23_RMIN) {
                if (pend_runs == 0u)
                    gap_start = pos;
                pend += r;
                pend_runs++;
            } else {
                for (t = 0u; t < pend; t++) {
                    dst[s++] = src[gap_start + t];
                    lit_w++;
                }
                dst[s++] = src[pos]; /* head */
                lit_w++;
                pend = 0u;
                pend_runs = 0u;
                ti++;
            }
            pos = j;
        }
        if (ti != tokc || lit_w != litc)
            return 0;
    }
    pos = 0u; /* TOK: rep0 tokens (first lit=L, rest lit=G+1/3-esc). */
    ti = 0u;
    {
        unsigned pend = 0u;
        int first_tok = 1;
        while (pos < size) {
            unsigned r, litf;
            uint32_t mc;
            j = pos + 1u;
            while (j < size && src[j] == src[pos])
                j++;
            r = (unsigned)(j - pos);
            if (r < LZMESH_U23_RMIN) {
                pend += r;
            } else {
                if (first_tok)
                    litf = pend;
                else if (pend == 2u)
                    litf = 3u;
                else
                    litf = pend + 1u;
                if (litf == 3u && (first_tok || pend != 2u))
                    return 0;
                mc = (uint32_t)r - 3u;
                if (mtok != LZMESH_U4_MODE_REPEAT || ti == 0u)
                    dst[s++] = lzmesh_u7_token_rep(litf, 0u,
                                                   mc > 6u ? 7u : mc);
                pend = 0u;
                first_tok = 0;
                ti++;
            }
            pos = j;
        }
        if (ti != tokc)
            return 0;
    }
    if (lenC > 0u) { /* LEN: per-token [lit-extra?]+[len-extra?] (S4.3). */
        if (lenB == 1u) { /* REPEAT: common byte once. */
            int done = 0;
            pos = 0u;
            {
                unsigned pend = 0u;
                int first_tok = 1;
                while (pos < size && !done) {
                    unsigned r, litf;
                    uint32_t mc;
                    j = pos + 1u;
                    while (j < size && src[j] == src[pos])
                        j++;
                    r = (unsigned)(j - pos);
                    if (r < LZMESH_U23_RMIN) {
                        pend += r;
                    } else {
                        if (first_tok)
                            litf = pend;
                        else if (pend == 2u)
                            litf = 3u;
                        else
                            litf = pend + 1u;
                        mc = (uint32_t)r - 3u;
                        if (litf == 3u) {
                            dst[s++] = 0u; /* lit esc3 rest 0 */
                            done = 1;
                        } else if (mc > 6u) {
                            uint8_t eb[5];
                            lzmesh_u7_len_escape_write(mc - 7u, eb);
                            dst[s++] = eb[0];
                            done = 1;
                        }
                        pend = 0u;
                        first_tok = 0;
                    }
                    pos = j;
                }
            }
            if (!done)
                return 0;
        } else {
            unsigned lw = 0u;
            if (lenB != lenC)
                return 0;
            pos = 0u;
            {
                unsigned pend = 0u;
                int first_tok = 1;
                while (pos < size) {
                    unsigned r, litf;
                    uint32_t mc;
                    j = pos + 1u;
                    while (j < size && src[j] == src[pos])
                        j++;
                    r = (unsigned)(j - pos);
                    if (r < LZMESH_U23_RMIN) {
                        pend += r;
                    } else {
                        if (first_tok)
                            litf = pend;
                        else if (pend == 2u)
                            litf = 3u;
                        else
                            litf = pend + 1u;
                        if (litf == 3u) {
                            if (first_tok || pend != 2u)
                                return 0;
                            dst[s++] = 0u; /* lit esc3 rest 0 */
                            lw++;
                        }
                        mc = (uint32_t)r - 3u;
                        if (mc > 6u) {
                            uint8_t eb[5];
                            unsigned eb_n, t;
                            eb_n = lzmesh_u7_len_escape_write(mc - 7u,
                                                              eb);
                            for (t = 0u; t < eb_n; t++) {
                                dst[s++] = eb[t];
                                lw++;
                            }
                        }
                        pend = 0u;
                        first_tok = 0;
                    }
                    pos = j;
                }
            }
            if (lw != lenC)
                return 0;
        }
    }
    /* DIST c0: 0 bytes. */
    if (s != bo)
        return 0; /* layout invariant; emit nothing */
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenC, litc, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === wide-gap k-run rep-chain no-H COMP, levels 1/5/9 (owner: u27) === */
/* SHAPE-R12CD (U23-R12C + U23-R12D close): k-run input (last run
 * r>=3, >=1 wide gap: lead L>=3 or interior G>=3) emits rep0 chain
 * over LONG runs only: each long run takes at its 2nd byte (rep d1,
 * ml=r-1, mc=r-3); gap bytes are literals of the following token.
 * First token lit_run = lead L (direct L<=2 per u12 rule; esc3 +
 * rest L-3 for L>=3, the u12 p4..8 first-token pattern); non-first
 * lit_run = G+1 (direct 1..2; esc3 + rest G-2 for G>=2; G==2 rest
 * 0 is u23-verbatim). Rest bytes via len_escape_write 255-chain
 * (S3.10, shared by all escapes). Len lane per token
 * [lit-extra?]+[len-extra?] in S4.3 intra-token order; mc esc7 iff
 * >6 (1B or 5B); dist c0 (all rep); bo==fo; footer (tokc,lenC,
 * litc,0) + END. Tok lane all-eq->REPEAT incl tc==1 (S5.5 + u24
 * solo-0x48 pin); lit always RAW (varied-proof: every head differs
 * from the byte before it; litc==1 impossible under feat gate);
 * len REPEAT iff all extra bytes equal. Total litc+sum ml == n
 * exact; C18 consumed litc; exact end, no terminator. Gates via
 * comp_keep (S5.3, no size compare) + U27-REM9 (last-long r<10 ->
 * 0: S5.4 Q30 9B gate, last-take rem<9 never happens; u26/R11
 * precedent, native to u27; u21/u23 analogs sibling-owned).
 * Disjoint from u19 (zero shorts) + u21 (wide veto) + u23 (pend>2
 * veto) by the feat gate; u25-term never fires on shorts.
 * Inherits U19-REP1 (+1-true-rep0 order), U19-L9S (L9-short, valid
 * COMP), U12-L1VIS (+ u23 far-take e01 precedent), R-002 (F5 none).
 * PERF: O(n) scans + O(1) emit; scratch-free. */
#define LZMESH_U27_RMIN 3u /* long-run floor (rep2 floor + 2B head) */
#define LZMESH_U27_REM9_MIN 10u /* U27-REM9: last-long r>=10 (rem>=9) */

/* Validate + measure. Returns 1 with counts; 0 outside shape. */
int lzmesh_u27_layout(const uint8_t *src, size_t size, uint32_t *tokc,
                      uint32_t *litc, uint32_t *lenC, uint32_t *lenB,
                      uint32_t *modes, uint32_t *bo, uint32_t *fo,
                      uint32_t *minLong) {
    size_t pos, j;
    unsigned tc, lc_lit, mnL;
    unsigned pend, lastR;
    uint32_t lc, lenb, mlen, mtok, tokb;
    uint64_t b;
    uint8_t first, first_tb;
    int have_first, all_eq, first_tok, feat, have_tb, tok_eq;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (tokc == NULL || litc == NULL || lenC == NULL || lenB == NULL
        || modes == NULL || bo == NULL || fo == NULL
        || minLong == NULL)
        return 0;
    /* R8-L9NEW C3: trailing-short pre-gate. The run-scan below
     * returns 0 at its final iteration whenever the last run is
     * short (is_last=1, r<RMIN); no earlier iteration can return
     * 1 (the loop only returns 0 or continues). Both callers
     * (want/emit) discard outs on 0, so declining here is exact.
     * Text/mixed bench (tail run 1) skip the ~5-branches/byte
     * full scan. */
    {
        size_t g = size - 1u;
        uint8_t glast = src[g];
        unsigned gr = 1u;
        while (g > 0u && gr < LZMESH_U27_RMIN
            && src[g - 1u] == glast) {
            g--;
            gr++;
        }
        if (gr < LZMESH_U27_RMIN)
            return 0;
    }
    tc = 0u;
    lc_lit = 0u;
    pend = 0u;
    mnL = (unsigned)size; /* <=MAX, fits */
    lastR = 0u;
    lc = 0u;
    have_first = 0;
    all_eq = 1;
    first = 0u;
    first_tok = 1;
    feat = 0;
    have_tb = 0;
    tok_eq = 1;
    first_tb = 0u;
    pos = 0u;
    while (pos < size) {
        unsigned r, litf, lit_run;
        uint32_t mc0;
        uint8_t tb;
        int is_last;
        j = pos + 1u;
        while (j < size && src[j] == src[pos])
            j++;
        r = (unsigned)(j - pos);
        is_last = (j == size);
        if (r < LZMESH_U27_RMIN) {
            if (is_last)
                return 0; /* trailing short: terminator open (u28) */
            pend += r;
        } else {
            if (r < mnL)
                mnL = r;
            lastR = r;
            if (first_tok) {
                lit_run = pend; /* lead L */
                litf = (pend <= 2u) ? pend : 3u;
                if (pend >= 3u)
                    feat = 1; /* R12D: wide lead */
            } else {
                lit_run = pend + 1u; /* gap G + head */
                litf = (lit_run <= 2u) ? lit_run : 3u;
                if (pend >= 3u)
                    feat = 1; /* R12C: wide interior gap */
            }
            /* tok all-eq fold (S5.5; byte-identical formula to emit) */
            mc0 = (uint32_t)r - 3u;
            tb = lzmesh_u7_token_rep(litf, 0u, mc0 > 6u ? 7u : mc0);
            if (!have_tb) {
                first_tb = tb;
                have_tb = 1;
            } else if (tb != first_tb) {
                tok_eq = 0;
            }
            tc++;
            lc_lit += pend + 1u;
            if (litf == 3u) {
                /* lit esc3 rest = lit_run-3, 255-chain (Q5/S3.10);
                 * first in token order (S4.3). lit_run>=3 here. */
                uint8_t eb[5];
                unsigned eb_n, t;
                eb_n = lzmesh_u7_len_escape_write(lit_run - 3u, eb);
                for (t = 0u; t < eb_n; t++) {
                    if (!have_first) {
                        first = eb[t];
                        have_first = 1;
                    } else if (eb[t] != first) {
                        all_eq = 0;
                    }
                    lc++;
                }
            }
            if ((uint32_t)r - 3u > 6u) { /* mc=r-3 esc7 (Q6) */
                uint8_t eb[5];
                unsigned eb_n, t;
                eb_n = lzmesh_u7_len_escape_write((uint32_t)r - 3u - 7u,
                                                  eb);
                for (t = 0u; t < eb_n; t++) {
                    if (!have_first) {
                        first = eb[t];
                        have_first = 1;
                    } else if (eb[t] != first) {
                        all_eq = 0;
                    }
                    lc++;
                }
            }
            pend = 0u;
            first_tok = 0;
        }
        pos = j;
    }
    if (tc == 0u || !feat)
        return 0; /* k==1 u9-owned; no-wide-gap u19/u21/u23-owned */
    if (lastR < LZMESH_U27_REM9_MIN)
        return 0; /* U27-REM9: last take rem<9 (S5.4 Q30) */
    mlen = lzmesh_u4_mode_trivial(lc, all_eq);
    if (mlen == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* no-H only */
    mtok = lzmesh_u4_mode_trivial(tc, tok_eq); /* S5.5 all-eq->REPEAT */
    if (mtok == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* tok HUF (large-k open) */
    if (lzmesh_u4_mode_trivial(lc_lit, 0) == LZMESH_U4_MODE_HUFFMAN)
        return 0; /* lit HUF (open) */
    lenb = (mlen == LZMESH_U4_MODE_REPEAT) ? 1u : lc;
    tokb = (mtok == LZMESH_U4_MODE_REPEAT) ? 1u : tc;
    *modes = lzmesh_u4_modes_pack(mtok, mlen,
                                  LZMESH_U4_MODE_RAW,
                                  LZMESH_U4_MODE_RAW);
    b = (uint64_t)9 + (uint64_t)lc_lit + (uint64_t)tokb
        + (uint64_t)lenb;
    if (b > (uint64_t)LZMESH_U1_DS_MAX)
        return 0; /* u32-unsafe huge (TIER-2 vetoes anyway) */
    *bo = (uint32_t)b;
    *fo = (uint32_t)b;
    *tokc = tc;
    *litc = lc_lit;
    *lenC = lc;
    *lenB = lenb;
    *minLong = mnL;
    return 1;
}

int lzmesh_u27_want(const uint8_t *src, size_t size, int level) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, mnL;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    if (!lzmesh_u27_layout(src, size, &tokc, &litc, &lenC, &lenB,
                           &modes, &bo, &fo, &mnL))
        return 0;
    (void)tokc;
    (void)lenC;
    (void)lenB;
    (void)modes;
    if (!lzmesh_u3_take_floor_ok(level, 1, mnL - 1u))
        return 0; /* rep floor on shortest long take */
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, litc, 1);
}

size_t lzmesh_u27_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, mnL, ds;
    uint32_t mtok, tokB;
    size_t need, s, pos, j;
    unsigned ti, lit_w;
    if (dst == NULL || src == NULL)
        return 0;
    if (!lzmesh_u27_layout(src, size, &tokc, &litc, &lenC, &lenB,
                           &modes, &bo, &fo, &mnL))
        return 0;
    (void)mnL;
    mtok = (modes >> 3) & 7u; /* S2.5 tok lane (pack tok,len,..) */
    tokB = (mtok == LZMESH_U4_MODE_REPEAT) ? 1u : tokc;
    need = (size_t)fo + 10u + 1u; /* block + END */
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    if ((size_t)9 + (size_t)litc + (size_t)tokB + (size_t)lenB
        != (size_t)bo)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    pos = 0u; /* LIT: per long run, pending gap bytes then head. */
    ti = 0u;
    lit_w = 0u;
    {
        size_t gap_start = 0u;
        unsigned pend = 0u;
        while (pos < size) {
            unsigned r, t;
            j = pos + 1u;
            while (j < size && src[j] == src[pos])
                j++;
            r = (unsigned)(j - pos);
            if (r < LZMESH_U27_RMIN) {
                if (pend == 0u)
                    gap_start = pos;
                pend += r;
            } else {
                for (t = 0u; t < pend; t++) {
                    dst[s++] = src[gap_start + t];
                    lit_w++;
                }
                dst[s++] = src[pos]; /* head */
                lit_w++;
                pend = 0u;
                ti++;
            }
            pos = j;
        }
        if (ti != tokc || lit_w != litc)
            return 0;
    }
    pos = 0u; /* TOK: rep0 (first lit=L/3-esc, rest lit=G+1/3-esc). */
    ti = 0u;
    {
        unsigned pend = 0u;
        int first_tok = 1;
        while (pos < size) {
            unsigned r, litf;
            uint32_t mc;
            j = pos + 1u;
            while (j < size && src[j] == src[pos])
                j++;
            r = (unsigned)(j - pos);
            if (r < LZMESH_U27_RMIN) {
                pend += r;
            } else {
                if (first_tok)
                    litf = (pend <= 2u) ? pend : 3u;
                else
                    litf = (pend + 1u <= 2u) ? pend + 1u : 3u;
                if (litf > 3u)
                    return 0;
                mc = (uint32_t)r - 3u;
                if (mtok != LZMESH_U4_MODE_REPEAT || ti == 0u)
                    dst[s++] = lzmesh_u7_token_rep(litf, 0u,
                                                   mc > 6u ? 7u : mc);
                pend = 0u;
                first_tok = 0;
                ti++;
            }
            pos = j;
        }
        if (ti != tokc)
            return 0;
    }
    if (lenC > 0u) { /* LEN: per-token [lit-extra?]+[len-extra?] (S4.3). */
        if (lenB == 1u) { /* REPEAT: common byte once. */
            int done = 0;
            pos = 0u;
            {
                unsigned pend = 0u;
                int first_tok = 1;
                while (pos < size && !done) {
                    unsigned r, lit_run;
                    uint32_t mc;
                    int lit_esc;
                    j = pos + 1u;
                    while (j < size && src[j] == src[pos])
                        j++;
                    r = (unsigned)(j - pos);
                    if (r < LZMESH_U27_RMIN) {
                        pend += r;
                    } else {
                        if (first_tok)
                            lit_run = pend;
                        else
                            lit_run = pend + 1u;
                        lit_esc = (lit_run >= 3u);
                        mc = (uint32_t)r - 3u;
                        if (lit_esc) {
                            uint8_t eb[5];
                            lzmesh_u7_len_escape_write(lit_run - 3u,
                                                       eb);
                            dst[s++] = eb[0];
                            done = 1;
                        } else if (mc > 6u) {
                            uint8_t eb[5];
                            lzmesh_u7_len_escape_write(mc - 7u, eb);
                            dst[s++] = eb[0];
                            done = 1;
                        }
                        pend = 0u;
                        first_tok = 0;
                    }
                    pos = j;
                }
            }
            if (!done)
                return 0;
        } else {
            unsigned lw = 0u;
            if (lenB != lenC)
                return 0;
            pos = 0u;
            {
                unsigned pend = 0u;
                int first_tok = 1;
                while (pos < size) {
                    unsigned r, lit_run;
                    uint32_t mc;
                    j = pos + 1u;
                    while (j < size && src[j] == src[pos])
                        j++;
                    r = (unsigned)(j - pos);
                    if (r < LZMESH_U27_RMIN) {
                        pend += r;
                    } else {
                        if (first_tok)
                            lit_run = pend;
                        else
                            lit_run = pend + 1u;
                        if (lit_run >= 3u) {
                            uint8_t eb[5];
                            unsigned eb_n, t;
                            eb_n = lzmesh_u7_len_escape_write(
                                lit_run - 3u, eb);
                            for (t = 0u; t < eb_n; t++) {
                                dst[s++] = eb[t];
                                lw++;
                            }
                        }
                        mc = (uint32_t)r - 3u;
                        if (mc > 6u) {
                            uint8_t eb[5];
                            unsigned eb_n, t;
                            eb_n = lzmesh_u7_len_escape_write(mc - 7u,
                                                              eb);
                            for (t = 0u; t < eb_n; t++) {
                                dst[s++] = eb[t];
                                lw++;
                            }
                        }
                        pend = 0u;
                        first_tok = 0;
                    }
                    pos = j;
                }
            }
            if (lw != lenC)
                return 0;
        }
    }
    /* DIST c0: 0 bytes. */
    if (s != bo)
        return 0; /* layout invariant; emit nothing */
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenC, litc, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === E1 trailing-short terminator SHAPE-R, levels 1/5/9 (owner: u29) === */
/* SHAPE-R-TRAIL: k-run input with all-long live prefix (nl>=1 runs
 * r>=3) + trailing short suffix (1+ runs r<=2, total R in [1,9]).
 * Live takes at 1 + s_i+1 (u19-verbatim rep0/ml=r-1/lit0-first/
 * lit1-rest; every live take rem>=9 via single last-take check,
 * S5.4 Q30); trailing R via S5.3.a terminator: R==1 tok 0x40 /
 * R==2 tok 0x80 (direct lit, ZERO term len bytes, ml2 truncate
 * S4.4) / R>=3 tok 0xC0 + ONE len extra R-3 (u28-oracle-proven,
 * ZERO overhang). Counts: tokc=nl+1, litc=nl+R, lenC=live+
 * (R>=3); distc=0; bo==fo; TIER via comp_keep. Disjoint: R==0
 * exact-end + blocked-live + interior-shorts all 0 here
 * (sibling-owned / future combo); u19-u28 all 0 on trailing-
 * short inputs by construction. S5.9.a: U29-R1ABS (R==1 separate
 * 0x40 vs live-absorb), U29-CLASS (class wording vs token formula
 * -- formula wins); inherits U19-REP1/L9S, U12-L1VIS, R-002.
 * PERF: O(n) scans + O(1) emit; scratch-free. */
int lzmesh_u29_trail_layout(const uint8_t *src, size_t size,
                            uint32_t *tokc, uint32_t *litc,
                            uint32_t *lenC, uint32_t *lenB,
                            uint32_t *modes, uint32_t *bo,
                            uint32_t *fo,
                            const lzmesh_runs14_t *runs14) {
    unsigned kk, nl, e, t;
    size_t suf, R;
    uint32_t live_lc, lit_c, tok_c, len_c, lenb, tokb, litb;
    uint32_t m_lit, m_tok, m_len;
    uint64_t b;
    uint8_t first_len, first_tok, first_lit, term_tok;
    int have_llen, have_tok, have_lit;
    int len_eq, tok_eq, lit_eq;
    size_t buf_local[21846];
    const size_t *buf = buf_local;
    unsigned nb;
    size_t pos, q;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (tokc == NULL || litc == NULL || lenC == NULL || lenB == NULL
        || modes == NULL || bo == NULL || fo == NULL)
        return 0;
    /* R14-mL9 v2: shared runscan (pointer-share, zero copy) or stock
     * scan on NULL. v1 memcpy churned 3x175KB (PMU: miss_st +3.3%);
     * analyses never write buf (scan-only writers), so sharing is exact. */
    if (runs14 != NULL) {
        nb = runs14->nb;
        buf = runs14->buf;
    } else {
    /* P16-A: u19 rescan fuse (one scan, not two; see script). */
    nb = 0u;
    pos = 0u;
    while (pos < size && nb < 21846u) {
        buf_local[nb++] = pos;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        pos = q;
    }
    }
    kk = nb;
    if (kk < 2u)
        return 0;
    if (kk > 21845u)
        return 0;
    if (nb != kk)
        return 0;
    /* trailing suffix = maximal short-run tail; live = runs[0..nl). */
    nl = kk;
    while (nl > 0u) {
        size_t rs = buf[nl - 1u];
        size_t re = (nl < kk) ? buf[nl] : size;
        if (re - rs >= LZMESH_U19_RMIN)
            break;
        nl--;
    }
    if (nl == 0u || nl >= kk)
        return 0; /* all-short (no live) / exact-end (no trailing) */
    e = nl - 1u;
    for (t = 0u; t < nl; t++) {
        size_t rs = buf[t];
        size_t re = buf[t + 1u];
        if (re - rs < LZMESH_U19_RMIN)
            return 0; /* interior/lead short: future combo */
    }
    suf = buf[nl];
    R = size - suf;
    if (R < 1u || R >= 10u)
        return 0;
    { /* every live take rem>=9 (rem decreases -> last-take check). */
        size_t plast = (e == 0u) ? 1u : buf[e] + 1u;
        if (plast + 9u > size)
            return 0; /* blocked live take: future combo */
    }
    if (R == 1u)
        term_tok = 0x40u; /* S5.3.a token min(R,3)<<6|0 (U29-R1ABS) */
    else if (R == 2u)
        term_tok = 0x80u; /* token formula + class agree */
    else
        term_tok = 0xC0u; /* u28-oracle-proven */
    live_lc = 0u;
    have_llen = 0;
    len_eq = 1;
    first_len = 0u;
    for (t = 0u; t < nl; t++) {
        size_t rs = buf[t];
        size_t re = buf[t + 1u];
        unsigned r = (unsigned)(re - rs);
        uint32_t mc;
        if (r < LZMESH_U19_RMIN)
            return 0;
        mc = (uint32_t)r - 3u;
        if (mc > 6u) {
            uint8_t eb[5];
            unsigned eb_n, u;
            eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
            for (u = 0u; u < eb_n; u++) {
                if (!have_llen) {
                    first_len = eb[u];
                    have_llen = 1;
                } else if (eb[u] != first_len) {
                    len_eq = 0;
                }
                live_lc++;
            }
        }
    }
    if (R >= 3u) { /* ONE term len byte R-3 (u28 pin verbatim) */
        uint8_t tb0 = (uint8_t)(R - 3u);
        if (!have_llen) {
            first_len = tb0;
            have_llen = 1;
        } else if (tb0 != first_len) {
            len_eq = 0;
        }
        live_lc++;
    }
    len_c = live_lc;
    have_tok = 0;
    tok_eq = 1;
    first_tok = 0u;
    for (t = 0u; t < nl; t++) {
        size_t rs = buf[t];
        size_t re = buf[t + 1u];
        unsigned r = (unsigned)(re - rs);
        unsigned litf = (t == 0u) ? 0u : 1u;
        uint32_t mc = (uint32_t)r - 3u;
        uint8_t tb = lzmesh_u7_token_rep(litf, 0u, mc > 6u ? 7u : mc);
        if (!have_tok) {
            first_tok = tb;
            have_tok = 1;
        } else if (tb != first_tok) {
            tok_eq = 0;
        }
    }
    if (!have_tok) {
        first_tok = term_tok;
        have_tok = 1;
    } else if (term_tok != first_tok) {
        tok_eq = 0;
    }
    tok_c = nl + 1u;
    have_lit = 0;
    lit_eq = 1;
    first_lit = 0u;
    for (t = 0u; t < nl; t++) {
        uint8_t lb = src[buf[t]];
        if (!have_lit) {
            first_lit = lb;
            have_lit = 1;
        } else if (lb != first_lit) {
            lit_eq = 0;
        }
    }
    for (q = suf; q < size; q++) {
        if (!have_lit) {
            first_lit = src[q];
            have_lit = 1;
        } else if (src[q] != first_lit) {
            lit_eq = 0;
        }
    }
    /* ZERO overhang: lit = nl live heads + R trailing. */
    lit_c = nl + (uint32_t)R;
    m_lit = lzmesh_u4_mode_trivial(lit_c, lit_eq);
    if (m_lit == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    m_tok = lzmesh_u4_mode_trivial(tok_c, tok_eq);
    if (m_tok == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    m_len = lzmesh_u4_mode_trivial(len_c, len_eq);
    if (m_len == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    litb = (m_lit == LZMESH_U4_MODE_REPEAT) ? 1u : lit_c;
    tokb = (m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : tok_c;
    lenb = (m_len == LZMESH_U4_MODE_REPEAT) ? 1u : len_c;
    *modes = lzmesh_u4_modes_pack(m_tok, m_len, m_lit,
                                  LZMESH_U4_MODE_RAW);
    b = (uint64_t)9 + (uint64_t)litb + (uint64_t)tokb
        + (uint64_t)lenb;
    if (b > (uint64_t)LZMESH_U1_DS_MAX)
        return 0;
    *bo = (uint32_t)b;
    *fo = (uint32_t)b;
    *tokc = tok_c;
    *litc = lit_c;
    *lenC = len_c;
    *lenB = lenb;
    return 1;
}

int lzmesh_u29_want(const uint8_t *src, size_t size, int level,
                    const lzmesh_runs14_t *runs14) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    if (!lzmesh_u29_trail_layout(src, size, &tokc, &litc, &lenC,
                                 &lenB, &modes, &bo, &fo, runs14))
        return 0;
    { /* rep floor on shortest LIVE take (shape already validated). */
        size_t p = 0u;
        unsigned mnl = 0u;
        while (p < size) {
            size_t q = p + 1u;
            unsigned r;
            while (q < size && src[q] == src[p])
                q++;
            r = (unsigned)(q - p);
            if (r < LZMESH_U19_RMIN)
                break; /* trailing suffix starts */
            if (mnl == 0u || r < mnl)
                mnl = r;
            p = q;
        }
        if (mnl < LZMESH_U19_RMIN)
            return 0;
        if (!lzmesh_u3_take_floor_ok(level, 1, mnl - 1u))
            return 0;
    }
    (void)tokc;
    (void)lenC;
    (void)lenB;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, litc, 1);
}

size_t lzmesh_u29_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, ds;
    size_t need, s, suf, R, pos, q;
    unsigned nl, t, u;
    uint8_t term_tok;
    if (dst == NULL || src == NULL)
        return 0;
    if (!lzmesh_u29_trail_layout(src, size, &tokc, &litc, &lenC,
                                 &lenB, &modes, &bo, &fo, NULL))
        return 0;
    need = (size_t)fo + 10u + 1u;
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    /* re-derive live runs + suffix (same scan as layout). */
    nl = 0u;
    pos = 0u;
    while (pos < size) {
        size_t rs = pos;
        unsigned r;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        r = (unsigned)(q - rs);
        if (r < LZMESH_U19_RMIN)
            break;
        nl++;
        pos = q;
    }
    suf = pos;
    if (nl == 0u || suf >= size)
        return 0;
    R = size - suf;
    if (R < 1u || R >= 10u)
        return 0;
    if (R == 1u)
        term_tok = 0x40u;
    else if (R == 2u)
        term_tok = 0x80u;
    else
        term_tok = 0xC0u;
    if (tokc != nl + 1u)
        return 0;
    if (litc != nl + (uint32_t)R)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    /* LIT: live heads (nl) + trailing R, ZERO overhang (or REPEAT). */
    if ((modes & 7u) == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = src[0];
    } else {
        pos = 0u;
        for (t = 0u; t < nl; t++) {
            size_t st = pos;
            if (st >= size)
                return 0;
            dst[s++] = src[st];
            q = pos + 1u;
            while (q < size && src[q] == src[pos])
                q++;
            pos = q;
        }
        for (q = suf; q < size; q++)
            dst[s++] = src[q];
    }
    /* TOK: live rep toks (nl) + term_tok (or REPEAT 1B). */
    if (((modes >> 3) & 7u) == LZMESH_U4_MODE_REPEAT) {
        uint32_t mc0;
        size_t re0;
        unsigned r0;
        pos = 0u;
        q = 1u;
        while (q < size && src[q] == src[0])
            q++;
        re0 = q;
        r0 = (unsigned)(re0 - 0u);
        if (r0 < LZMESH_U19_RMIN)
            return 0;
        mc0 = (uint32_t)r0 - 3u;
        dst[s++] = lzmesh_u7_token_rep(0u, 0u, mc0 > 6u ? 7u : mc0);
    } else {
        pos = 0u;
        for (t = 0u; t < nl; t++) {
            size_t rs = pos;
            size_t re;
            unsigned r, litf;
            uint32_t mc;
            q = pos + 1u;
            while (q < size && src[q] == src[pos])
                q++;
            re = q;
            pos = q;
            if (rs >= size || re > size || re <= rs)
                return 0;
            r = (unsigned)(re - rs);
            if (r < LZMESH_U19_RMIN)
                return 0;
            litf = (t == 0u) ? 0u : 1u;
            mc = (uint32_t)r - 3u;
            dst[s++] = lzmesh_u7_token_rep(litf, 0u,
                                           mc > 6u ? 7u : mc);
        }
        dst[s++] = term_tok;
    }
    /* LEN: live extras (token order) + [R-3] iff R>=3 (or REPEAT). */
    if (lenC > 0u) {
        if (((modes >> 6) & 7u) == LZMESH_U4_MODE_REPEAT) {
            /* REPEAT single byte: first len byte in stream order. */
            int done = 0;
            pos = 0u;
            for (t = 0u; t < nl && !done; t++) {
                size_t rs = pos;
                size_t re;
                unsigned r;
                uint32_t mc;
                q = pos + 1u;
                while (q < size && src[q] == src[pos])
                    q++;
                re = q;
                pos = q;
                r = (unsigned)(re - rs);
                mc = (uint32_t)r - 3u;
                if (mc > 6u) {
                    uint8_t eb[5];
                    lzmesh_u7_len_escape_write(mc - 7u, eb);
                    dst[s++] = eb[0];
                    done = 1;
                }
            }
            if (!done && R >= 3u) {
                dst[s++] = (uint8_t)(R - 3u);
                done = 1;
            }
            if (!done)
                return 0;
            for (u = 1u; u < lenB; u++)
                (void)u;
            if (lenB != 1u)
                return 0;
        } else {
            unsigned lw = 0u;
            if (lenB != lenC)
                return 0;
            pos = 0u;
            for (t = 0u; t < nl; t++) {
                size_t rs = pos;
                size_t re;
                unsigned r;
                uint32_t mc;
                q = pos + 1u;
                while (q < size && src[q] == src[pos])
                    q++;
                re = q;
                pos = q;
                r = (unsigned)(re - rs);
                mc = (uint32_t)r - 3u;
                if (mc > 6u) {
                    uint8_t eb[5];
                    unsigned eb_n, w;
                    eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
                    for (w = 0u; w < eb_n; w++) {
                        dst[s++] = eb[w];
                        lw++;
                    }
                }
            }
            if (R >= 3u) {
                dst[s++] = (uint8_t)(R - 3u);
                lw++;
            }
            if (lw != lenC)
                return 0;
        }
    }
    /* DIST c0: 0 bytes. */
    if (s != bo)
        return 0;
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenC, litc, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === E1 short-live + trailing-term combo SHAPE-R, levels 1/5/9 (owner: u30) === */
/* SHAPE-R-TC1: k-run input (kk>=2) with short-bearing live prefix +
 * trailing short suffix (total R in [1,9]). Live = runs[0..nl):
 * >=1 long run (r>=3, takes) + >=1 short run (u21-legal: run0
 * r in {1,2}, interior r==1 non-adjacent); live takes at long-run
 * 2nd bytes (u21-verbatim: shorts are pending lits, no takes;
 * every take rem>=9 via single last-take check, S5.4 Q30).
 * Trailing R via S5.3.a terminator (u29-verbatim): R==1 tok 0x40
 * / R==2 tok 0x80 (ZERO term len bytes, S4.4 trunc) / R>=3 tok
 * 0xC0 + ONE len extra R-3 (ZERO overhang). Counts: tokc=nLong+1,
 * litc=liveLits+R, lenC=live+(R>=3); distc=0; bo==fo; TIER via
 * comp_keep. Disjoint: trailing vetoes u21/u23/u27; mn<3 vetoes
 * u19/u28; live shorts veto u29. Inherits U29-R1ABS/U29-CLASS,
 * U19-REP1/L9S, U12-L1VIS, R-002. No new grammar (u21-live +
 * u29-term glue). PERF: O(n) scans + O(1) emit; scratch-free. */
int lzmesh_u30_tc1_layout(const uint8_t *src, size_t size,
                          uint32_t *tokc, uint32_t *litc,
                          uint32_t *lenC, uint32_t *lenB,
                          uint32_t *modes, uint32_t *bo,
                          uint32_t *fo,
                          const lzmesh_runs14_t *runs14) {
    unsigned kk, nl, t;
    size_t suf, R;
    uint32_t live_lc, lit_c, tok_c, len_c, lenb, tokb, litb;
    uint32_t m_lit, m_tok, m_len;
    uint64_t b;
    uint8_t first_len, first_tok, first_lit, term_tok;
    int have_llen, have_tok, have_lit;
    int len_eq, tok_eq, lit_eq;
    size_t buf_local[21846];
    const size_t *buf = buf_local;
    unsigned nb;
    size_t pos, q;
    unsigned tc, nshort, pend, live_lits;
    int first_tokf;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (tokc == NULL || litc == NULL || lenC == NULL || lenB == NULL
        || modes == NULL || bo == NULL || fo == NULL)
        return 0;
    /* R14-mL9 v2: shared runscan (pointer-share, zero copy) or stock
     * scan on NULL. v1 memcpy churned 3x175KB (PMU: miss_st +3.3%);
     * analyses never write buf (scan-only writers), so sharing is exact. */
    if (runs14 != NULL) {
        nb = runs14->nb;
        buf = runs14->buf;
    } else {
    /* P16-A: u19 rescan fuse (one scan, not two; see script). */
    nb = 0u;
    pos = 0u;
    while (pos < size && nb < 21846u) {
        buf_local[nb++] = pos;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        pos = q;
    }
    }
    kk = nb;
    if (kk < 2u)
        return 0;
    if (kk > 21845u)
        return 0;
    if (nb != kk)
        return 0;
    /* trailing suffix = maximal short-run tail; live = runs[0..nl). */
    nl = kk;
    while (nl > 0u) {
        size_t rs = buf[nl - 1u];
        size_t re = (nl < kk) ? buf[nl] : size;
        if (re - rs >= LZMESH_U19_RMIN)
            break;
        nl--;
    }
    if (nl == 0u || nl >= kk)
        return 0; /* all-short (no live) / exact-end (no trailing) */
    /* live prefix: u21-legal shorts + >=1 long + >=1 short. */
    tc = 0u;
    nshort = 0u;
    pend = 0u;
    live_lits = 0u;
    first_tokf = 1;
    live_lc = 0u;
    have_llen = 0;
    len_eq = 1;
    first_len = 0u;
    have_tok = 0;
    tok_eq = 1;
    first_tok = 0u;
    for (t = 0u; t < nl; t++) {
        size_t rs = buf[t];
        size_t re = buf[t + 1u];
        unsigned r = (unsigned)(re - rs);
        if (r < LZMESH_U19_RMIN) {
            if (t > 0u && r != 1u)
                return 0; /* interior r==2: lit-esc open */
            if (t > 0u && pend > 0u)
                return 0; /* adjacent shorts: lit-esc open */
            pend = r; /* 1..2 (run0) or 1 (interior) */
            nshort++;
        } else {
            unsigned litf = first_tokf ? pend : pend + 1u;
            uint32_t mc = (uint32_t)r - 3u;
            uint8_t tb;
            if (litf > 2u)
                return 0; /* belt-and-braces */
            tb = lzmesh_u7_token_rep(litf, 0u, mc > 6u ? 7u : mc);
            if (!have_tok) {
                first_tok = tb;
                have_tok = 1;
            } else if (tb != first_tok) {
                tok_eq = 0;
            }
            tc++;
            live_lits += pend + 1u;
            if (mc > 6u) {
                uint8_t eb[5];
                unsigned eb_n, u;
                eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
                for (u = 0u; u < eb_n; u++) {
                    if (!have_llen) {
                        first_len = eb[u];
                        have_llen = 1;
                    } else if (eb[u] != first_len) {
                        len_eq = 0;
                    }
                    live_lc++;
                }
            }
            pend = 0u;
            first_tokf = 0;
        }
    }
    if (tc == 0u || nshort == 0u)
        return 0; /* no takes / no shorts (u29-owned) */
    if (pend != 0u)
        return 0; /* live ends short: impossible (maximal tail) */
    suf = buf[nl];
    R = size - suf;
    if (R < 1u || R >= 10u)
        return 0;
    { /* last live take rem>=9 (takes monotonic; run[nl-1] long). */
        size_t plast = buf[nl - 1u] + 1u;
        if (plast + 9u > size)
            return 0; /* blocked live take: future combo */
    }
    if (R == 1u)
        term_tok = 0x40u; /* S5.3.a token min(R,3)<<6|0 (U29-R1ABS) */
    else if (R == 2u)
        term_tok = 0x80u; /* token formula + class agree */
    else
        term_tok = 0xC0u; /* u28-oracle-proven */
    if (R >= 3u) { /* ONE term len byte R-3 (u28 pin verbatim) */
        uint8_t tb0 = (uint8_t)(R - 3u);
        if (!have_llen) {
            first_len = tb0;
            have_llen = 1;
        } else if (tb0 != first_len) {
            len_eq = 0;
        }
        live_lc++;
    }
    len_c = live_lc;
    if (!have_tok) {
        first_tok = term_tok;
        have_tok = 1;
    } else if (term_tok != first_tok) {
        tok_eq = 0;
    }
    tok_c = tc + 1u;
    /* LIT fold in stream order: live (pending + heads) + trailing R. */
    have_lit = 0;
    lit_eq = 1;
    first_lit = 0u;
    for (t = 0u; t < nl; t++) {
        size_t rs = buf[t];
        size_t re = buf[t + 1u];
        unsigned r = (unsigned)(re - rs);
        if (r < LZMESH_U19_RMIN) {
            for (q = rs; q < re; q++) {
                if (!have_lit) {
                    first_lit = src[q];
                    have_lit = 1;
                } else if (src[q] != first_lit) {
                    lit_eq = 0;
                }
            }
        } else {
            if (!have_lit) {
                first_lit = src[rs];
                have_lit = 1;
            } else if (src[rs] != first_lit) {
                lit_eq = 0;
            }
        }
    }
    for (q = suf; q < size; q++) {
        if (!have_lit) {
            first_lit = src[q];
            have_lit = 1;
        } else if (src[q] != first_lit) {
            lit_eq = 0;
        }
    }
    /* ZERO overhang: lit = live (pending + heads) + R trailing. */
    lit_c = live_lits + (uint32_t)R;
    m_lit = lzmesh_u4_mode_trivial(lit_c, lit_eq);
    if (m_lit == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    m_tok = lzmesh_u4_mode_trivial(tok_c, tok_eq);
    if (m_tok == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    m_len = lzmesh_u4_mode_trivial(len_c, len_eq);
    if (m_len == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    litb = (m_lit == LZMESH_U4_MODE_REPEAT) ? 1u : lit_c;
    tokb = (m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : tok_c;
    lenb = (m_len == LZMESH_U4_MODE_REPEAT) ? 1u : len_c;
    *modes = lzmesh_u4_modes_pack(m_tok, m_len, m_lit,
                                  LZMESH_U4_MODE_RAW);
    b = (uint64_t)9 + (uint64_t)litb + (uint64_t)tokb
        + (uint64_t)lenb;
    if (b > (uint64_t)LZMESH_U1_DS_MAX)
        return 0;
    *bo = (uint32_t)b;
    *fo = (uint32_t)b;
    *tokc = tok_c;
    *litc = lit_c;
    *lenC = len_c;
    *lenB = lenb;
    return 1;
}

int lzmesh_u30_want(const uint8_t *src, size_t size, int level,
                    const lzmesh_runs14_t *runs14) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    /* AA-SPARSE: L1 excluded (GEN owns). L1 oracle runs the MX finder
     * (newest S with lcp>=6) and takes NEW-matches where u30 emits
     * rep0+d1-runs (s18-n256-sparse e01: oracle 49B NEW-shape vs u30
     * 37B rep0-shape; u37_emit == oracle byte-exact). Corpus-wide
     * (all patterns, seeds 0-21) u30 fires on exactly 1 L1 cell (the
     * div) and 0 L1-true cells; census 440 sparse e01: 3 fires, all
     * div. L5/L9 legs kept (same input rep0-true there: oracle's
     * L5/L9 finder misses the d7 match). Falls through to u37. */
    if (level == 1)
        return 0;
    if (!lzmesh_u30_tc1_layout(src, size, &tokc, &litc, &lenC,
                               &lenB, &modes, &bo, &fo, runs14))
        return 0;
    { /* rep floor on shortest LONG live take (u21 rule). */
        size_t buf[21846];
        unsigned kk, nl, t, nb, mnl;
        size_t pos, q;
        kk = lzmesh_u19_runs(src, size, NULL);
        if (kk < 2u || kk > 21845u)
            return 0;
        nb = 0u;
        pos = 0u;
        while (pos < size && nb < kk) {
            buf[nb++] = pos;
            q = pos + 1u;
            while (q < size && src[q] == src[pos])
                q++;
            pos = q;
        }
        if (nb != kk)
            return 0;
        nl = kk;
        while (nl > 0u) {
            size_t rs = buf[nl - 1u];
            size_t re = (nl < kk) ? buf[nl] : size;
            if (re - rs >= LZMESH_U19_RMIN)
                break;
            nl--;
        }
        if (nl == 0u || nl >= kk)
            return 0;
        mnl = 0u;
        for (t = 0u; t < nl; t++) {
            size_t rs = buf[t];
            size_t re = buf[t + 1u];
            unsigned r = (unsigned)(re - rs);
            if (r < LZMESH_U19_RMIN)
                continue; /* shorts have no takes */
            if (mnl == 0u || r < mnl)
                mnl = r;
        }
        if (mnl < LZMESH_U19_RMIN)
            return 0;
        if (!lzmesh_u3_take_floor_ok(level, 1, mnl - 1u))
            return 0;
    }
    (void)tokc;
    (void)lenC;
    (void)lenB;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, litc, 1);
}

size_t lzmesh_u30_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, ds;
    size_t need, s, suf, R;
    unsigned kk, nl, t, u, tc, live_lits;
    size_t buf[21846];
    unsigned nb;
    size_t pos, q;
    uint8_t term_tok;
    if (dst == NULL || src == NULL)
        return 0;
    if (!lzmesh_u30_tc1_layout(src, size, &tokc, &litc, &lenC,
                               &lenB, &modes, &bo, &fo, NULL))
        return 0;
    need = (size_t)fo + 10u + 1u;
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    /* re-derive live runs + suffix (same scan as layout). */
    kk = lzmesh_u19_runs(src, size, NULL);
    if (kk < 2u || kk > 21845u)
        return 0;
    nb = 0u;
    pos = 0u;
    while (pos < size && nb < kk) {
        buf[nb++] = pos;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        pos = q;
    }
    if (nb != kk)
        return 0;
    nl = kk;
    while (nl > 0u) {
        size_t rs = buf[nl - 1u];
        size_t re = (nl < kk) ? buf[nl] : size;
        if (re - rs >= LZMESH_U19_RMIN)
            break;
        nl--;
    }
    if (nl == 0u || nl >= kk)
        return 0;
    tc = 0u;
    live_lits = 0u;
    {
        unsigned pend = 0u;
        for (t = 0u; t < nl; t++) {
            size_t rs = buf[t];
            size_t re = buf[t + 1u];
            unsigned r = (unsigned)(re - rs);
            if (r < LZMESH_U19_RMIN) {
                pend = r;
            } else {
                tc++;
                live_lits += pend + 1u;
                pend = 0u;
            }
        }
    }
    suf = buf[nl];
    if (suf >= size)
        return 0;
    R = size - suf;
    if (R < 1u || R >= 10u)
        return 0;
    if (tc == 0u)
        return 0;
    if (R == 1u)
        term_tok = 0x40u;
    else if (R == 2u)
        term_tok = 0x80u;
    else
        term_tok = 0xC0u;
    if (tokc != tc + 1u)
        return 0;
    if (litc != live_lits + (uint32_t)R)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    /* LIT: live (pending + heads) + trailing R (or REPEAT 1B). */
    if ((modes & 7u) == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = src[0];
    } else {
        pos = 0u;
        while (pos < suf) {
            unsigned r;
            size_t v;
            q = pos + 1u;
            while (q < suf && src[q] == src[pos])
                q++;
            r = (unsigned)(q - pos);
            if (r < LZMESH_U19_RMIN) {
                for (v = pos; v < q; v++)
                    dst[s++] = src[v];
            } else {
                dst[s++] = src[pos];
            }
            pos = q;
        }
        for (q = suf; q < size; q++)
            dst[s++] = src[q];
    }
    /* TOK: live rep toks + term_tok (or REPEAT first-tok 1B). */
    {
        unsigned pend = 0u;
        int first_tokf = 1;
        unsigned ti = 0u;
        pos = 0u;
        while (pos < suf) {
            unsigned r, litf;
            uint32_t mc;
            q = pos + 1u;
            while (q < suf && src[q] == src[pos])
                q++;
            r = (unsigned)(q - pos);
            if (r < LZMESH_U19_RMIN) {
                pend = r;
            } else {
                litf = first_tokf ? pend : pend + 1u;
                if (litf > 2u)
                    return 0;
                mc = (uint32_t)r - 3u;
                if (((modes >> 3) & 7u) != LZMESH_U4_MODE_REPEAT
                    || ti == 0u)
                    dst[s++] = lzmesh_u7_token_rep(litf, 0u,
                                                   mc > 6u ? 7u : mc);
                pend = 0u;
                first_tokf = 0;
                ti++;
            }
            pos = q;
        }
        if (((modes >> 3) & 7u) != LZMESH_U4_MODE_REPEAT)
            dst[s++] = term_tok;
        if (ti + 1u != tokc)
            return 0;
    }
    /* LEN: live extras (token order) + [R-3] iff R>=3 (or REPEAT). */
    if (lenC > 0u) {
        if (((modes >> 6) & 7u) == LZMESH_U4_MODE_REPEAT) {
            /* REPEAT single byte: first len byte in stream order. */
            int done = 0;
            pos = 0u;
            while (pos < suf && !done) {
                unsigned r;
                uint32_t mc;
                q = pos + 1u;
                while (q < suf && src[q] == src[pos])
                    q++;
                r = (unsigned)(q - pos);
                if (r >= LZMESH_U19_RMIN) {
                    mc = (uint32_t)r - 3u;
                    if (mc > 6u) {
                        uint8_t eb[5];
                        lzmesh_u7_len_escape_write(mc - 7u, eb);
                        dst[s++] = eb[0];
                        done = 1;
                    }
                }
                pos = q;
            }
            if (!done && R >= 3u) {
                dst[s++] = (uint8_t)(R - 3u);
                done = 1;
            }
            if (!done)
                return 0;
            for (u = 1u; u < lenB; u++)
                (void)u;
            if (lenB != 1u)
                return 0;
        } else {
            unsigned lw = 0u;
            if (lenB != lenC)
                return 0;
            pos = 0u;
            while (pos < suf) {
                unsigned r;
                uint32_t mc;
                q = pos + 1u;
                while (q < suf && src[q] == src[pos])
                    q++;
                r = (unsigned)(q - pos);
                if (r >= LZMESH_U19_RMIN) {
                    mc = (uint32_t)r - 3u;
                    if (mc > 6u) {
                        uint8_t eb[5];
                        unsigned eb_n, w;
                        eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
                        for (w = 0u; w < eb_n; w++) {
                            dst[s++] = eb[w];
                            lw++;
                        }
                    }
                }
                pos = q;
            }
            if (R >= 3u) {
                dst[s++] = (uint8_t)(R - 3u);
                lw++;
            }
            if (lw != lenC)
                return 0;
        }
    }
    /* DIST c0: 0 bytes. */
    if (s != bo)
        return 0;
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenC, litc, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === E1 r2-live + trailing-term combo SHAPE-R, levels 1/5/9 (owner: u32) === */
/* SHAPE-R-TC2: k-run input (kk>=2) with r2-bearing live prefix +
 * trailing short suffix (total R in [1,9]). Live = runs[0..nl):
 * >=1 long run (r>=3, takes) + EXACTLY ONE interior r==2 run
 * (u23-esc3: non-first lit3 + len extra 0x00, S3.10/S4.3) + other
 * shorts u30-legal (run0 r in {1,2}, interior r==1, non-adjacent);
 * live takes at long-run 2nd bytes (shorts are pending lits, no
 * takes; every take rem>=9 via single last-take check, S5.4 Q30).
 * Trailing R via S5.3.a terminator (u29-verbatim): R==1 tok 0x40
 * / R==2 tok 0x80 (ZERO term len bytes, S4.4 trunc) / R>=3 tok
 * 0xC0 + ONE len extra R-3 (ZERO overhang). Counts: tokc=nLong+1,
 * litc=liveLits+R, lenC=live+esc3+(R>=3); distc=0; bo==fo; TIER
 * via comp_keep. Disjoint: interior-r2 vetoes u30 (V-interior2);
 * trailing vetoes u21/u23/u27; mn<3 vetoes u19/u28; live shorts
 * veto u29. Inherits U29-R1ABS/U29-CLASS, U19-REP1/L9S,
 * U12-L1VIS, R-002. No new grammar (u23-esc3 + u30-live +
 * u29-term glue). PERF: O(n) scans + O(1) emit; scratch-free. */
int lzmesh_u32_tc2_layout(const uint8_t *src, size_t size,
                          uint32_t *tokc, uint32_t *litc,
                          uint32_t *lenC, uint32_t *lenB,
                          uint32_t *modes, uint32_t *bo,
                          uint32_t *fo,
                          const lzmesh_runs14_t *runs14) {
    unsigned kk, nl, t;
    size_t suf, R;
    uint32_t live_lc, lit_c, tok_c, len_c, lenb, tokb, litb;
    uint32_t m_lit, m_tok, m_len;
    uint64_t b;
    uint8_t first_len, first_tok, first_lit, term_tok;
    int have_llen, have_tok, have_lit;
    int len_eq, tok_eq, lit_eq;
    size_t buf_local[21846];
    const size_t *buf = buf_local;
    unsigned nb;
    size_t pos, q;
    unsigned tc, nshort, n2, pend, live_lits;
    int first_tokf;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (tokc == NULL || litc == NULL || lenC == NULL || lenB == NULL
        || modes == NULL || bo == NULL || fo == NULL)
        return 0;
    /* R14-mL9 v2: shared runscan (pointer-share, zero copy) or stock
     * scan on NULL. v1 memcpy churned 3x175KB (PMU: miss_st +3.3%);
     * analyses never write buf (scan-only writers), so sharing is exact. */
    if (runs14 != NULL) {
        nb = runs14->nb;
        buf = runs14->buf;
    } else {
    /* P16-A: u19 rescan fuse (one scan, not two; see script). */
    nb = 0u;
    pos = 0u;
    while (pos < size && nb < 21846u) {
        buf_local[nb++] = pos;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        pos = q;
    }
    }
    kk = nb;
    if (kk < 2u)
        return 0;
    if (kk > 21845u)
        return 0;
    if (nb != kk)
        return 0;
    /* trailing suffix = maximal short-run tail; live = runs[0..nl). */
    nl = kk;
    while (nl > 0u) {
        size_t rs = buf[nl - 1u];
        size_t re = (nl < kk) ? buf[nl] : size;
        if (re - rs >= LZMESH_U19_RMIN)
            break;
        nl--;
    }
    if (nl == 0u || nl >= kk)
        return 0; /* all-short (no live) / exact-end (no trailing) */
    /* live prefix: u30-legal shorts + EXACTLY ONE interior r==2. */
    tc = 0u;
    nshort = 0u;
    n2 = 0u;
    pend = 0u;
    live_lits = 0u;
    first_tokf = 1;
    live_lc = 0u;
    have_llen = 0;
    len_eq = 1;
    first_len = 0u;
    have_tok = 0;
    tok_eq = 1;
    first_tok = 0u;
    for (t = 0u; t < nl; t++) {
        size_t rs = buf[t];
        size_t re = buf[t + 1u];
        unsigned r = (unsigned)(re - rs);
        if (r < LZMESH_U19_RMIN) {
            if (t > 0u && pend > 0u)
                return 0; /* adjacent shorts: lit-esc open */
            if (t > 0u && r == 2u)
                n2++; /* THE interior r2 (esc3 on its token) */
            else if (t > 0u && r != 1u)
                return 0; /* belt-and-braces (r<RMIN) */
            pend = r; /* 1..2 (run0) or 1..2 (interior, r2 once) */
            nshort++;
        } else {
            unsigned litf;
            uint32_t mc = (uint32_t)r - 3u;
            uint8_t tb;
            if (first_tokf)
                litf = pend; /* 0..2 direct (u21 run0 rule) */
            else if (pend == 2u)
                litf = 3u; /* interior r2: esc3 (u23 rule) */
            else
                litf = pend + 1u; /* 1..2 */
            if (litf == 3u && (first_tokf || pend != 2u))
                return 0; /* esc3 only non-first pend2 */
            if (litf > 3u)
                return 0; /* belt-and-braces */
            tb = lzmesh_u7_token_rep(litf, 0u, mc > 6u ? 7u : mc);
            if (!have_tok) {
                first_tok = tb;
                have_tok = 1;
            } else if (tb != first_tok) {
                tok_eq = 0;
            }
            tc++;
            live_lits += pend + 1u;
            if (litf == 3u) {
                /* lit_run = pend+1 = 3 -> esc3 rest 0 -> 1B 0x00
                 * (Q5/S3.10); first in token order (S4.3). */
                if (!have_llen) {
                    first_len = 0u;
                    have_llen = 1;
                } else if (first_len != 0u) {
                    len_eq = 0;
                }
                live_lc++;
            }
            if (mc > 6u) {
                uint8_t eb[5];
                unsigned eb_n, u;
                eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
                for (u = 0u; u < eb_n; u++) {
                    if (!have_llen) {
                        first_len = eb[u];
                        have_llen = 1;
                    } else if (eb[u] != first_len) {
                        len_eq = 0;
                    }
                    live_lc++;
                }
            }
            pend = 0u;
            first_tokf = 0;
        }
    }
    if (tc == 0u || nshort == 0u)
        return 0; /* no takes / no shorts (u29-owned) */
    if (n2 != 1u)
        return 0; /* zero r2 (u30-owned) / multi r2 (future) */
    if (pend != 0u)
        return 0; /* live ends short: impossible (maximal tail) */
    suf = buf[nl];
    R = size - suf;
    if (R < 1u || R >= 10u)
        return 0;
    { /* last live take rem>=9 (takes monotonic; run[nl-1] long). */
        size_t plast = buf[nl - 1u] + 1u;
        if (plast + 9u > size)
            return 0; /* blocked live take: future combo */
    }
    if (R == 1u)
        term_tok = 0x40u; /* S5.3.a token min(R,3)<<6|0 (U29-R1ABS) */
    else if (R == 2u)
        term_tok = 0x80u; /* token formula + class agree */
    else
        term_tok = 0xC0u; /* u28-oracle-proven */
    if (R >= 3u) { /* ONE term len byte R-3 (u28 pin verbatim) */
        uint8_t tb0 = (uint8_t)(R - 3u);
        if (!have_llen) {
            first_len = tb0;
            have_llen = 1;
        } else if (tb0 != first_len) {
            len_eq = 0;
        }
        live_lc++;
    }
    len_c = live_lc;
    if (!have_tok) {
        first_tok = term_tok;
        have_tok = 1;
    } else if (term_tok != first_tok) {
        tok_eq = 0;
    }
    tok_c = tc + 1u;
    /* LIT fold in stream order: live (pending + heads) + trailing R. */
    have_lit = 0;
    lit_eq = 1;
    first_lit = 0u;
    for (t = 0u; t < nl; t++) {
        size_t rs = buf[t];
        size_t re = buf[t + 1u];
        unsigned r = (unsigned)(re - rs);
        if (r < LZMESH_U19_RMIN) {
            for (q = rs; q < re; q++) {
                if (!have_lit) {
                    first_lit = src[q];
                    have_lit = 1;
                } else if (src[q] != first_lit) {
                    lit_eq = 0;
                }
            }
        } else {
            if (!have_lit) {
                first_lit = src[rs];
                have_lit = 1;
            } else if (src[rs] != first_lit) {
                lit_eq = 0;
            }
        }
    }
    for (q = suf; q < size; q++) {
        if (!have_lit) {
            first_lit = src[q];
            have_lit = 1;
        } else if (src[q] != first_lit) {
            lit_eq = 0;
        }
    }
    /* ZERO overhang: lit = live (pending + heads) + R trailing. */
    lit_c = live_lits + (uint32_t)R;
    m_lit = lzmesh_u4_mode_trivial(lit_c, lit_eq);
    if (m_lit == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    m_tok = lzmesh_u4_mode_trivial(tok_c, tok_eq);
    if (m_tok == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    m_len = lzmesh_u4_mode_trivial(len_c, len_eq);
    if (m_len == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    litb = (m_lit == LZMESH_U4_MODE_REPEAT) ? 1u : lit_c;
    tokb = (m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : tok_c;
    lenb = (m_len == LZMESH_U4_MODE_REPEAT) ? 1u : len_c;
    *modes = lzmesh_u4_modes_pack(m_tok, m_len, m_lit,
                                  LZMESH_U4_MODE_RAW);
    b = (uint64_t)9 + (uint64_t)litb + (uint64_t)tokb
        + (uint64_t)lenb;
    if (b > (uint64_t)LZMESH_U1_DS_MAX)
        return 0;
    *bo = (uint32_t)b;
    *fo = (uint32_t)b;
    *tokc = tok_c;
    *litc = lit_c;
    *lenC = len_c;
    *lenB = lenb;
    return 1;
}

int lzmesh_u32_want(const uint8_t *src, size_t size, int level,
                    const lzmesh_runs14_t *runs14) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    if (!lzmesh_u32_tc2_layout(src, size, &tokc, &litc, &lenC,
                               &lenB, &modes, &bo, &fo, runs14))
        return 0;
    { /* rep floor on shortest LONG live take (u21 rule). */
        size_t buf[21846];
        unsigned kk, nl, t, nb, mnl;
        size_t pos, q;
        kk = lzmesh_u19_runs(src, size, NULL);
        if (kk < 2u || kk > 21845u)
            return 0;
        nb = 0u;
        pos = 0u;
        while (pos < size && nb < kk) {
            buf[nb++] = pos;
            q = pos + 1u;
            while (q < size && src[q] == src[pos])
                q++;
            pos = q;
        }
        if (nb != kk)
            return 0;
        nl = kk;
        while (nl > 0u) {
            size_t rs = buf[nl - 1u];
            size_t re = (nl < kk) ? buf[nl] : size;
            if (re - rs >= LZMESH_U19_RMIN)
                break;
            nl--;
        }
        if (nl == 0u || nl >= kk)
            return 0;
        mnl = 0u;
        for (t = 0u; t < nl; t++) {
            size_t rs = buf[t];
            size_t re = buf[t + 1u];
            unsigned r = (unsigned)(re - rs);
            if (r < LZMESH_U19_RMIN)
                continue; /* shorts have no takes */
            if (mnl == 0u || r < mnl)
                mnl = r;
        }
        if (mnl < LZMESH_U19_RMIN)
            return 0;
        if (!lzmesh_u3_take_floor_ok(level, 1, mnl - 1u))
            return 0;
    }
    (void)tokc;
    (void)lenC;
    (void)lenB;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, litc, 1);
}

size_t lzmesh_u32_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, ds;
    size_t need, s, suf, R;
    unsigned kk, nl, t, u, tc, live_lits;
    size_t buf[21846];
    unsigned nb;
    size_t pos, q;
    uint8_t term_tok;
    if (dst == NULL || src == NULL)
        return 0;
    if (!lzmesh_u32_tc2_layout(src, size, &tokc, &litc, &lenC,
                               &lenB, &modes, &bo, &fo, NULL))
        return 0;
    need = (size_t)fo + 10u + 1u;
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    /* re-derive live runs + suffix (same scan as layout). */
    kk = lzmesh_u19_runs(src, size, NULL);
    if (kk < 2u || kk > 21845u)
        return 0;
    nb = 0u;
    pos = 0u;
    while (pos < size && nb < kk) {
        buf[nb++] = pos;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        pos = q;
    }
    if (nb != kk)
        return 0;
    nl = kk;
    while (nl > 0u) {
        size_t rs = buf[nl - 1u];
        size_t re = (nl < kk) ? buf[nl] : size;
        if (re - rs >= LZMESH_U19_RMIN)
            break;
        nl--;
    }
    if (nl == 0u || nl >= kk)
        return 0;
    tc = 0u;
    live_lits = 0u;
    {
        unsigned pend = 0u;
        for (t = 0u; t < nl; t++) {
            size_t rs = buf[t];
            size_t re = buf[t + 1u];
            unsigned r = (unsigned)(re - rs);
            if (r < LZMESH_U19_RMIN) {
                pend = r;
            } else {
                tc++;
                live_lits += pend + 1u;
                pend = 0u;
            }
        }
    }
    suf = buf[nl];
    if (suf >= size)
        return 0;
    R = size - suf;
    if (R < 1u || R >= 10u)
        return 0;
    if (tc == 0u)
        return 0;
    if (R == 1u)
        term_tok = 0x40u;
    else if (R == 2u)
        term_tok = 0x80u;
    else
        term_tok = 0xC0u;
    if (tokc != tc + 1u)
        return 0;
    if (litc != live_lits + (uint32_t)R)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    /* LIT: live (pending + heads) + trailing R (or REPEAT 1B). */
    if ((modes & 7u) == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = src[0];
    } else {
        pos = 0u;
        while (pos < suf) {
            unsigned r;
            size_t v;
            q = pos + 1u;
            while (q < suf && src[q] == src[pos])
                q++;
            r = (unsigned)(q - pos);
            if (r < LZMESH_U19_RMIN) {
                for (v = pos; v < q; v++)
                    dst[s++] = src[v];
            } else {
                dst[s++] = src[pos];
            }
            pos = q;
        }
        for (q = suf; q < size; q++)
            dst[s++] = src[q];
    }
    /* TOK: live rep toks + term_tok (or REPEAT first-tok 1B). */
    {
        unsigned pend = 0u;
        int first_tokf = 1;
        unsigned ti = 0u;
        pos = 0u;
        while (pos < suf) {
            unsigned r, litf;
            uint32_t mc;
            q = pos + 1u;
            while (q < suf && src[q] == src[pos])
                q++;
            r = (unsigned)(q - pos);
            if (r < LZMESH_U19_RMIN) {
                pend = r;
            } else {
                if (first_tokf)
                    litf = pend;
                else if (pend == 2u)
                    litf = 3u;
                else
                    litf = pend + 1u;
                if (litf == 3u && (first_tokf || pend != 2u))
                    return 0;
                if (litf > 3u)
                    return 0;
                mc = (uint32_t)r - 3u;
                if (((modes >> 3) & 7u) != LZMESH_U4_MODE_REPEAT
                    || ti == 0u)
                    dst[s++] = lzmesh_u7_token_rep(litf, 0u,
                                                   mc > 6u ? 7u : mc);
                pend = 0u;
                first_tokf = 0;
                ti++;
            }
            pos = q;
        }
        if (((modes >> 3) & 7u) != LZMESH_U4_MODE_REPEAT)
            dst[s++] = term_tok;
        if (ti + 1u != tokc)
            return 0;
    }
    /* LEN: live extras in S4.3 token order + [R-3] iff R>=3 (or REPEAT). */
    if (lenC > 0u) {
        if (((modes >> 6) & 7u) == LZMESH_U4_MODE_REPEAT) {
            /* REPEAT single byte: first len byte in stream order. */
            int done = 0;
            pos = 0u;
            {
                unsigned pend = 0u;
                int first_tokf = 1;
                while (pos < suf && !done) {
                    unsigned r, litf;
                    uint32_t mc;
                    q = pos + 1u;
                    while (q < suf && src[q] == src[pos])
                        q++;
                    r = (unsigned)(q - pos);
                    if (r < LZMESH_U19_RMIN) {
                        pend = r;
                    } else {
                        if (first_tokf)
                            litf = pend;
                        else if (pend == 2u)
                            litf = 3u;
                        else
                            litf = pend + 1u;
                        mc = (uint32_t)r - 3u;
                        if (litf == 3u) {
                            dst[s++] = 0u; /* lit esc3 rest 0 */
                            done = 1;
                        } else if (mc > 6u) {
                            uint8_t eb[5];
                            lzmesh_u7_len_escape_write(mc - 7u, eb);
                            dst[s++] = eb[0];
                            done = 1;
                        }
                        pend = 0u;
                        first_tokf = 0;
                    }
                    pos = q;
                }
            }
            if (!done && R >= 3u) {
                dst[s++] = (uint8_t)(R - 3u);
                done = 1;
            }
            if (!done)
                return 0;
            for (u = 1u; u < lenB; u++)
                (void)u;
            if (lenB != 1u)
                return 0;
        } else {
            unsigned lw = 0u;
            if (lenB != lenC)
                return 0;
            pos = 0u;
            {
                unsigned pend = 0u;
                int first_tokf = 1;
                while (pos < suf) {
                    unsigned r, litf;
                    uint32_t mc;
                    q = pos + 1u;
                    while (q < suf && src[q] == src[pos])
                        q++;
                    r = (unsigned)(q - pos);
                    if (r < LZMESH_U19_RMIN) {
                        pend = r;
                    } else {
                        if (first_tokf)
                            litf = pend;
                        else if (pend == 2u)
                            litf = 3u;
                        else
                            litf = pend + 1u;
                        if (litf == 3u) {
                            dst[s++] = 0u; /* lit esc3 rest 0 */
                            lw++;
                        }
                        mc = (uint32_t)r - 3u;
                        if (mc > 6u) {
                            uint8_t eb[5];
                            unsigned eb_n, w;
                            eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
                            for (w = 0u; w < eb_n; w++) {
                                dst[s++] = eb[w];
                                lw++;
                            }
                        }
                        pend = 0u;
                        first_tokf = 0;
                    }
                    pos = q;
                }
            }
            if (R >= 3u) {
                dst[s++] = (uint8_t)(R - 3u);
                lw++;
            }
            if (lw != lenC)
                return 0;
        }
    }
    /* DIST c0: 0 bytes. */
    if (s != bo)
        return 0;
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenC, litc, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === E2b 9B-blocked terminator SHAPE-R, levels 1/5/9 (owner: u25, rework u28) === */
/* SHAPE-R-TERM: k-run input (k>=2, r_i>=3) with 9B-blocked trailing
 * takes (S5.4 pos+9<=size gate, Q30: rem<9 never happen). Live
 * takes at 1 + s_i+1 while pos+9<=size (prefix runs exact-end,
 * u19-verbatim rep0/ml=r-1/lit0-first-lit1-rest); first blocked
 * take at s_j+1 (rem R-1<9, R=size-s_j in [3,9]) SKIPPED, trailing
 * R bytes via S5.3.a terminator: rep0 len-2 tok 0xC0 (lit3 esc;
 * S5.3.a "escape+C0" names this TOKEN, not len bytes), ONE len
 * extra R-3 -> decoded lit R -> lit_run R covers trailing R
 * exact (S3.10 lit esc3; ZERO overhang), ml=2 (S4.4 match
 * truncate). Counts: tokc=j+1, litc=j+R, lenC=live_lc+1,
 * distc=0; bo==fo (no suffix, d=1 sb0); footer + END. TIER via
 * comp_keep (keep->COMP E2b close, veto->RAW E2c preserved).
 * Exact-end inputs (no blocked takes, rlast>=10 typical) return
 * 0 here (u19-owned, byte-frozen). Gaps CLOSED at merge (M28):
 * U25-OVH + U25-LENORD REFUTED (35B refused both sides; oracle
 * 32B pin lit=R len=[live,R-3] verified by real decoders both
 * sides). Overhang-2 covers non-final terms only. U19-REP1/L9S/
 * VIS/R-002 inherit. PERF: O(n) scans + O(1) emit; scratch-free. */
int lzmesh_u25_term_layout(const uint8_t *src, size_t size,
                           uint32_t *tokc, uint32_t *litc,
                           uint32_t *lenC, uint32_t *lenB,
                           uint32_t *modes, uint32_t *bo,
                           uint32_t *fo) {
    unsigned kk, mn, j, t;
    size_t sj, R;
    uint32_t live_lc, lit_c, tok_c, len_c, lenb, tokb, litb;
    uint32_t m_lit, m_tok, m_len;
    uint64_t b;
    uint8_t first_len, first_tok, first_lit;
    int have_llen, have_tok, have_lit;
    int len_eq, tok_eq, lit_eq;
    size_t buf[21846];
    unsigned nb;
    size_t pos, q;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (tokc == NULL || litc == NULL || lenC == NULL || lenB == NULL
        || modes == NULL || bo == NULL || fo == NULL)
        return 0;
    kk = lzmesh_u19_runs(src, size, &mn);
    if (kk < 2u || mn < LZMESH_U19_RMIN)
        return 0;
    if (kk > 21845u)
        return 0;
    nb = 0u;
    pos = 0u;
    while (pos < size && nb < kk) {
        buf[nb++] = pos;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        pos = q;
    }
    if (nb != kk)
        return 0;
    sj = 0u;
    j = kk;
    for (t = 0u; t < kk; t++) {
        size_t pt = (t == 0u) ? 1u : buf[t] + 1u;
        if (pt + 9u > size) {
            j = t;
            sj = buf[t];
            break;
        }
    }
    if (j >= kk)
        return 0;
    if (j == 0u)
        return 0;
    R = size - sj;
    if (R < 3u || R >= 10u)
        return 0;
    live_lc = 0u;
    have_llen = 0;
    len_eq = 1;
    first_len = 0u;
    for (t = 0u; t < j; t++) {
        size_t rs = buf[t];
        size_t re = (t + 1u < kk) ? buf[t + 1u] : size;
        unsigned r = (unsigned)(re - rs);
        uint32_t mc;
        if (r < LZMESH_U19_RMIN)
            return 0;
        mc = (uint32_t)r - 3u;
        if (mc > 6u) {
            uint8_t eb[5];
            unsigned eb_n, u;
            eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
            for (u = 0u; u < eb_n; u++) {
                if (!have_llen) {
                    first_len = eb[u];
                    have_llen = 1;
                } else if (eb[u] != first_len) {
                    len_eq = 0;
                }
                live_lc++;
            }
        }
    }
    { /* term adds ONE len byte: extra R-3 (M28 oracle pin; u25 [R-1,C0] refuted) */
        uint8_t tb0 = (uint8_t)(R - 3u);
        if (!have_llen) {
            first_len = tb0;
            have_llen = 1;
        } else if (tb0 != first_len) {
            len_eq = 0;
        }
        live_lc++;
    }
    len_c = live_lc;
    have_tok = 0;
    tok_eq = 1;
    first_tok = 0u;
    for (t = 0u; t < j; t++) {
        size_t rs = buf[t];
        size_t re = (t + 1u < kk) ? buf[t + 1u] : size;
        unsigned r = (unsigned)(re - rs);
        unsigned litf = (t == 0u) ? 0u : 1u;
        uint32_t mc = (uint32_t)r - 3u;
        uint8_t tb = lzmesh_u7_token_rep(litf, 0u, mc > 6u ? 7u : mc);
        if (!have_tok) {
            first_tok = tb;
            have_tok = 1;
        } else if (tb != first_tok) {
            tok_eq = 0;
        }
    }
    if (!have_tok) {
        first_tok = 0xC0u;
        have_tok = 1;
    } else if ((uint8_t)0xC0u != first_tok) {
        tok_eq = 0;
    }
    tok_c = j + 1u;
    have_lit = 0;
    lit_eq = 1;
    first_lit = 0u;
    for (t = 0u; t < j; t++) {
        uint8_t lb = src[buf[t]];
        if (!have_lit) {
            first_lit = lb;
            have_lit = 1;
        } else if (lb != first_lit) {
            lit_eq = 0;
        }
    }
    for (q = sj; q < size; q++) {
        if (!have_lit) {
            first_lit = src[q];
            have_lit = 1;
        } else if (src[q] != first_lit) {
            lit_eq = 0;
        }
    }
    /* ZERO overhang: lit = j live heads + R trailing (u25 +2 zeros refuted M28) */
    lit_c = j + (uint32_t)R;
    m_lit = lzmesh_u4_mode_trivial(lit_c, lit_eq);
    if (m_lit == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    m_tok = lzmesh_u4_mode_trivial(tok_c, tok_eq);
    if (m_tok == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    m_len = lzmesh_u4_mode_trivial(len_c, len_eq);
    if (m_len == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    litb = (m_lit == LZMESH_U4_MODE_REPEAT) ? 1u : lit_c;
    tokb = (m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : tok_c;
    lenb = (m_len == LZMESH_U4_MODE_REPEAT) ? 1u : len_c;
    *modes = lzmesh_u4_modes_pack(m_tok, m_len, m_lit,
                                  LZMESH_U4_MODE_RAW);
    b = (uint64_t)9 + (uint64_t)litb + (uint64_t)tokb
        + (uint64_t)lenb;
    if (b > (uint64_t)LZMESH_U1_DS_MAX)
        return 0;
    *bo = (uint32_t)b;
    *fo = (uint32_t)b;
    *tokc = tok_c;
    *litc = lit_c;
    *lenC = len_c;
    *lenB = lenb;
    return 1;
}

size_t lzmesh_u25_term_emit(uint8_t *dst, size_t dst_capacity,
                            const uint8_t *src, size_t size) {
    uint32_t tokc, litc, lenC, lenB, modes, bo, fo, ds;
    size_t need, s, sj, R, pos, q;
    unsigned kk, mn, j, t, u;
    if (dst == NULL || src == NULL)
        return 0;
    if (!lzmesh_u25_term_layout(src, size, &tokc, &litc, &lenC, &lenB,
                                &modes, &bo, &fo))
        return 0;
    need = (size_t)fo + 10u + 1u;
    if (dst_capacity < need)
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    kk = lzmesh_u19_runs(src, size, &mn);
    if (kk < 2u)
        return 0;
    /* re-find first blocked take j/sj (same scan as layout) */
    sj = 0u;
    j = kk;
    pos = 0u;
    for (t = 0u; t < kk; t++) {
        size_t st = pos;
        size_t pt = (t == 0u) ? 1u : st + 1u;
        q = pos + 1u;
        while (q < size && src[q] == src[pos])
            q++;
        pos = q;
        if (pt + 9u > size) {
            j = t;
            sj = st;
            break;
        }
    }
    if (j >= kk || j == 0u)
        return 0;
    R = size - sj;
    if (R < 3u || R >= 10u)
        return 0;
    if (tokc != j + 1u)
        return 0;
    if (litc != j + (uint32_t)R)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    /* LIT: live heads (j) + trailing R, ZERO overhang (or REPEAT 1B). */
    if ((modes & 7u) == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = src[0];
    } else {
        pos = 0u;
        for (t = 0u; t < j; t++) {
            size_t st = pos;
            if (st >= size)
                return 0;
            dst[s++] = src[st];
            q = pos + 1u;
            while (q < size && src[q] == src[pos])
                q++;
            pos = q;
        }
        for (q = sj; q < size; q++)
            dst[s++] = src[q];
    }
    /* TOK: live rep toks (j) + 0xC0 (or REPEAT 1B). */
    if (((modes >> 3) & 7u) == LZMESH_U4_MODE_REPEAT) {
        uint32_t mc0;
        size_t re0;
        unsigned r0;
        pos = 0u;
        q = 1u;
        while (q < size && src[q] == src[0])
            q++;
        re0 = q;
        r0 = (unsigned)(re0 - 0u);
        if (r0 < LZMESH_U19_RMIN)
            return 0;
        mc0 = (uint32_t)r0 - 3u;
        dst[s++] = lzmesh_u7_token_rep(0u, 0u, mc0 > 6u ? 7u : mc0);
    } else {
        pos = 0u;
        for (t = 0u; t < j; t++) {
            size_t rs = pos;
            size_t re;
            unsigned r, litf;
            uint32_t mc;
            q = pos + 1u;
            while (q < size && src[q] == src[pos])
                q++;
            re = q;
            pos = q;
            if (rs >= size || re > size || re <= rs)
                return 0;
            r = (unsigned)(re - rs);
            if (r < LZMESH_U19_RMIN)
                return 0;
            litf = (t == 0u) ? 0u : 1u;
            mc = (uint32_t)r - 3u;
            dst[s++] = lzmesh_u7_token_rep(litf, 0u,
                                           mc > 6u ? 7u : mc);
        }
        dst[s++] = 0xC0u;
    }
    /* LEN: live extras (token order) + [R-3] (or REPEAT 1B). */
    if (lenC > 0u) {
        if (((modes >> 6) & 7u) == LZMESH_U4_MODE_REPEAT) {
            /* REPEAT single byte: first len byte in stream order. */
            int done = 0;
            pos = 0u;
            for (t = 0u; t < j && !done; t++) {
                size_t rs = pos;
                size_t re;
                unsigned r;
                uint32_t mc;
                q = pos + 1u;
                while (q < size && src[q] == src[pos])
                    q++;
                re = q;
                pos = q;
                r = (unsigned)(re - rs);
                mc = (uint32_t)r - 3u;
                if (mc > 6u) {
                    uint8_t eb[5];
                    lzmesh_u7_len_escape_write(mc - 7u, eb);
                    dst[s++] = eb[0];
                    done = 1;
                }
            }
            if (!done) {
                dst[s++] = (uint8_t)(R - 3u);
                done = 1;
            }
            if (!done)
                return 0;
            for (u = 1u; u < lenB; u++)
                (void)u;
            if (lenB != 1u)
                return 0;
        } else {
            unsigned lw = 0u;
            if (lenB != lenC)
                return 0;
            pos = 0u;
            for (t = 0u; t < j; t++) {
                size_t rs = pos;
                size_t re;
                unsigned r;
                uint32_t mc;
                q = pos + 1u;
                while (q < size && src[q] == src[pos])
                    q++;
                re = q;
                pos = q;
                r = (unsigned)(re - rs);
                mc = (uint32_t)r - 3u;
                if (mc > 6u) {
                    uint8_t eb[5];
                    unsigned eb_n, w;
                    eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
                    for (w = 0u; w < eb_n; w++) {
                        dst[s++] = eb[w];
                        lw++;
                    }
                }
            }
            dst[s++] = (uint8_t)(R - 3u);
            lw++;
            if (lw != lenC)
                return 0;
        }
    }
    /* DIST c0: 0 bytes. */
    if (s != bo)
        return 0;
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenC, litc, 0u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* ==== PACK1 S3.7 Huffman tables (R1: w23/codes/Kraft exact; solver HELD) ====
 * S3.7 pins: maxlen 10 (symbols) / 5 (meta); package-merge quantum
 * w23=min(34-clz(nSym),maxlen), overflow doubles + rebuilds;
 * canonical ascending per-length, bit-reversed LSB-first;
 * Kraft gate exact (==0x10000 else fail); 2^maxlen table-index;
 * degenerate single-value -> two 1-bit codes. Zero behavior change:
 * nothing calls this region yet (no emission this round). */

unsigned lzmesh_pack1_w23(unsigned nsym, unsigned maxlen)
{
    unsigned n = 0u;
    unsigned v = nsym;
    unsigned w;
    if (nsym == 0u)
        return 0u;
    if ((v & 0xFFFF0000u) == 0u) { n += 16u; v <<= 16; }
    if ((v & 0xFF000000u) == 0u) { n += 8u; v <<= 8; }
    if ((v & 0xF0000000u) == 0u) { n += 4u; v <<= 4; }
    if ((v & 0xC0000000u) == 0u) { n += 2u; v <<= 2; }
    if ((v & 0x80000000u) == 0u) { n += 1u; }
    w = 34u - n;
    return w < maxlen ? w : maxlen;
}

unsigned lzmesh_pack1_kraft_ok(const uint8_t *lens, unsigned nsym)
{
    uint32_t sum = 0u;
    unsigned used = 0u;
    unsigned i;
    if (lens == 0)
        return 0u;
    for (i = 0u; i < nsym; i++) {
        unsigned l = lens[i];
        if (l == 0u)
            continue;
        if (l > 16u)
            return 0u;
        used++;
        sum += (uint32_t)1u << (16u - l);
    }
    if (used == 1u && sum == 0x8000u)
        sum <<= 1; /* degenerate single-value: two 1-bit codes. */
    return sum == 0x10000u;
}

static uint16_t lzmesh_pack1_rev(unsigned code, unsigned len)
{
    uint16_t r = 0u;
    while (len-- > 0u) {
        r = (uint16_t)((r << 1) | (code & 1u));
        code >>= 1;
    }
    return r;
}

unsigned lzmesh_pack1_canon(const uint8_t *lens, unsigned nsym,
    uint16_t *codes, unsigned maxlen)
{
    unsigned len;
    unsigned sym;
    unsigned code = 0u;
    unsigned prev = 0u;
    if (lens == 0 || codes == 0)
        return 0u;
    if (nsym == 0u || nsym > 1024u)
        return 0u;
    if (maxlen != 5u && maxlen != 10u)
        return 0u;
    for (sym = 0u; sym < nsym; sym++)
        codes[sym] = 0u;
    if (lzmesh_pack1_kraft_ok(lens, nsym) == 0u)
        return 0u;
    for (len = 1u; len <= maxlen; len++) {
        for (sym = 0u; sym < nsym; sym++) {
            if (lens[sym] != len)
                continue;
            code <<= (len - prev);
            prev = len;
            if (code >= (1u << len))
                return 0u;
            codes[sym] = lzmesh_pack1_rev(code, len);
            code++;
        }
    }
    return 1u;
}

/* ==== PACK6F solver: package-merge internals (S3.7, exact-tie) ====
 * Ports the validated throwaway (run_pack6f_solver.py: PM-vs-DP 300/300,
 * h6/uniform44/h1-meta vectors): L lists, stable insertion sort by
 * (weight, construction order) [R9-T1: oracle-proven 37/37 ties],
 * pair-pack + base merge,
 * final take 2n-2 smallest, length = containing-package count.
 * Fail-closed (returns 0, lens zeroed): nsym>256 (no regression: the
 * R1 stub failed every multi-symbol case), limit 1..10, capacity
 * n>2^limit, pool/list overflow, short final, or lengths failing the
 * 1..limit + Kraft-exact gate. Heap-free; ~37KB stack. */

struct lzmesh_pack1_pmnode {
    uint64_t w;
    uint16_t l;
    uint16_t r;
};

#define LZMESH_PACK1_PM_MAXN 256u
#define LZMESH_PACK1_PM_MAXL 10u
#define LZMESH_PACK1_PM_POOL (256u + 10u * 256u)
#define LZMESH_PACK1_PM_MAXLIST 512u

static int lzmesh_pack1_pm_cmp(const struct lzmesh_pack1_pmnode *pool,
    unsigned nleaf, unsigned a, unsigned b);

/* P2-bitio: bottom-up merge sort by (w,idx). Output is identical to
 * the insertion sort it replaces: pm_cmp is a strict total order
 * (distinct indices), so the sorted permutation is unique and every
 * correct sort produces it. tmp must hold n entries. */
static void lzmesh_pack1_pm_msort(const struct lzmesh_pack1_pmnode *pool,
    unsigned nleaf, uint16_t *list, uint16_t *tmp, unsigned n)
{
    uint16_t *a = list;
    uint16_t *b = tmp;
    unsigned w;
    unsigned i;
    if (n < 2u)
        return;
    for (w = 1u; w < n; w <<= 1) {
        for (i = 0u; i < n; i += w << 1) {
            unsigned lo = i;
            unsigned mid = (i + w < n) ? i + w : n;
            unsigned hi = (i + (w << 1) < n) ? i + (w << 1) : n;
            unsigned x = lo;
            unsigned y = mid;
            unsigned o = lo;
            while (x < mid && y < hi) {
                if (lzmesh_pack1_pm_cmp(pool, nleaf, a[x], a[y]) <= 0)
                    b[o++] = a[x++];
                else
                    b[o++] = a[y++];
            }
            while (x < mid)
                b[o++] = a[x++];
            while (y < hi)
                b[o++] = a[y++];
        }
        {
            uint16_t *t = a;
            a = b;
            b = t;
        }
    }
    if (a != list) {
        for (i = 0u; i < n; i++)
            list[i] = a[i];
    }
}

/* R9-T1 (37/37 tie blocks exact): oracle tie-break is (weight,
 * construction order), i.e. stable sort by weight. The old
 * (weight, leaf-expansion lex, shorter-first) key skewed 2:1:1
 * cost-ties ({L-1,L+1,L+1}) where the oracle balances ({L,L,L}).
 * -1/0/+1 by (weight, pool index). */
static int lzmesh_pack1_pm_cmp(const struct lzmesh_pack1_pmnode *pool,
    unsigned nleaf, unsigned a, unsigned b)
{
    if (pool[a].w < pool[b].w)
        return -1;
    if (pool[a].w > pool[b].w)
        return 1;
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    (void)nleaf;
    return 0;
}

static void lzmesh_pack1_pm_sort(const struct lzmesh_pack1_pmnode *pool,
    unsigned nleaf, uint16_t *list, unsigned n)
{
    unsigned i;
    for (i = 1u; i < n; i++) {
        uint16_t x = list[i];
        unsigned j = i;
        while (j > 0u &&
            lzmesh_pack1_pm_cmp(pool, nleaf, x, list[j - 1u]) < 0) {
            list[j] = list[j - 1u];
            j--;
        }
        list[j] = x;
    }
}

static unsigned lzmesh_pack1_solve(const uint32_t *freq, unsigned nsym,
    unsigned limit, uint8_t *lens)
{
    struct lzmesh_pack1_pmnode pool[LZMESH_PACK1_PM_POOL];
    uint16_t cur[LZMESH_PACK1_PM_MAXLIST];
    uint16_t nxt[LZMESH_PACK1_PM_MAXLIST];
    uint16_t syms[LZMESH_PACK1_PM_MAXN];
    uint16_t sub[LZMESH_PACK1_PM_POOL];
    uint32_t cnt[LZMESH_PACK1_PM_POOL];
    unsigned n = 0u;
    unsigned nnode;
    unsigned ncur;
    unsigned lv;
    unsigned i;
    unsigned take;
    unsigned k;
    if (freq == 0 || lens == 0)
        return 0u;
    if (nsym == 0u || nsym > LZMESH_PACK1_PM_MAXN)
        return 0u; /* 257+ keeps R1-stub behavior (decline). */
    if (limit == 0u || limit > LZMESH_PACK1_PM_MAXL)
        return 0u;
    for (i = 0u; i < nsym; i++) {
        lens[i] = 0u;
        if (freq[i] != 0u) {
            if (n >= LZMESH_PACK1_PM_MAXN)
                return 0u;
            syms[n++] = (uint16_t)i;
        }
    }
    if (n < 2u)
        return 0u; /* used0/1 handled by caller. */
    if (n > (1u << limit))
        return 0u; /* over capacity: caller doubles quantum. */
    for (i = 0u; i < n; i++) {
        pool[i].w = freq[syms[i]];
        pool[i].l = 0xFFFFu;
        pool[i].r = syms[i];
        cur[i] = (uint16_t)i;
    }
    nnode = n;
    ncur = n;
    lzmesh_pack1_pm_sort(pool, n, cur, ncur); /* base by (freq, sym). */
    for (lv = 1u; lv < limit; lv++) {
        unsigned npk = 0u;
        unsigned nnxt;
        for (i = 0u; i + 1u < ncur; i += 2u) {
            if (nnode >= LZMESH_PACK1_PM_POOL)
                return 0u;
            pool[nnode].w = pool[cur[i]].w + pool[cur[i + 1u]].w;
            pool[nnode].l = cur[i];
            pool[nnode].r = cur[i + 1u];
            if (npk >= LZMESH_PACK1_PM_MAXLIST)
                return 0u;
            nxt[npk++] = (uint16_t)nnode;
            nnode++;
        }
        for (i = 0u; i < n; i++) {
            if (npk >= LZMESH_PACK1_PM_MAXLIST)
                return 0u;
            nxt[npk++] = i; /* base leaves ascending. */
        }
        nnxt = npk;
        /* P2-bitio: msort == pm_sort output (total order, unique). cur
         * is dead here, so it doubles as merge scratch. */
        lzmesh_pack1_pm_msort(pool, n, nxt, cur, nnxt);
        for (i = 0u; i < nnxt; i++)
            cur[i] = nxt[i];
        ncur = nnxt;
    }
    take = 2u * n - 2u;
    if (ncur < take)
        return 0u;
    /* P2-bitio: subtree sizes (capped) + downward count propagation
     * replace the per-take-item expand walks. Children always carry
     * smaller pool indices than their package, so sizes ascend and
     * counts descend exactly. Success-path lens are the same visit
     * counts; every old fail maps to a fail here: expand n-cap <->
     * sub>256 (P3-harden: cap 257, since old expand fails only PAST
     * 256), mid-loop lens>=limit <-> final cnt>limit (checked before
     * narrowing), expand stack/idx fails are structural
     * impossibilities (depth<=limit-1<=9, children<nnode). */
    for (i = 0u; i < nnode; i++)
        cnt[i] = 0u;
    for (i = 0u; i < nnode; i++) {
        if (pool[i].l == 0xFFFFu) {
            sub[i] = 1u;
        } else {
            unsigned s = (unsigned)sub[pool[i].l] +
                (unsigned)sub[pool[i].r];
            sub[i] = (s > 257u) ? (uint16_t)257u : (uint16_t)s;
        }
    }
    for (k = 0u; k < take; k++) {
        if (sub[cur[k]] > 256u)
            goto fail;
        cnt[cur[k]]++;
    }
    i = nnode;
    while (i > n) {
        i--;
        if (cnt[i] != 0u) {
            cnt[pool[i].l] += cnt[i];
            cnt[pool[i].r] += cnt[i];
        }
    }
    for (i = 0u; i < n; i++) {
        if (cnt[i] == 0u || cnt[i] > limit)
            goto fail;
        lens[syms[i]] = (uint8_t)cnt[i];
    }
    for (i = 0u; i < nsym; i++) {
        if (freq[i] != 0u && (lens[i] == 0u || lens[i] > limit))
            goto fail;
        if (freq[i] == 0u && lens[i] != 0u)
            goto fail;
    }
    if (lzmesh_pack1_kraft_ok(lens, nsym) == 0u)
        goto fail;
    return 1u;
fail:
    for (i = 0u; i < nsym; i++)
        lens[i] = 0u;
    return 0u;
}

unsigned lzmesh_pack1_lengths(const uint32_t *freq, unsigned nsym,
    unsigned maxlen, uint8_t *lens)
{
    unsigned q;
    unsigned i;
    unsigned used = 0u;
    unsigned sole = 0u;
    if (freq == 0 || lens == 0)
        return 0u;
    if (maxlen != 5u && maxlen != 10u)
        return 0u;
    if (nsym == 0u || nsym > (1u << maxlen))
        return 0u;
    for (i = 0u; i < nsym; i++) {
        lens[i] = 0u;
        if (freq[i] != 0u) {
            used++;
            sole = i;
        }
    }
    if (used == 0u)
        return 0u; /* S2.6 count0->RAW: packer declines. */
    if (used == 1u) {
        lens[sole] = 1u; /* degenerate single-value: 1-bit (Kraft x2). */
        return 1u;
    }
    q = lzmesh_pack1_w23(nsym, maxlen);
    if (q == 0u || q > maxlen)
        return 0u;
    for (;;) {
        if (lzmesh_pack1_solve(freq, nsym, q, lens) != 0u)
            return 1u;
        if (q >= maxlen)
            return 0u;
        q <<= 1; /* overflow: double quantum and rebuild. */
        if (q > maxlen)
            q = maxlen;
    }
}

/* ==== PACK1 S3.8 header writer (R1: meta + bitmap + used values) ====
 * S3.8 pins: lane0 opens refill -> 11x3b meta lengths -> refill -> 32b
 * group bitmap (LSB-first, nonzero); used = 8*popcount length values
 * scatter ascending (set bit g <- next 8 values -> length[8g..8g+7]);
 * used values are coded with the meta table (maxlen 5) and dealt S3.5
 * round-robin i%8 across ALL 8 lanes with no re-align; all fields
 * LSB-first. Meta lengths come from lzmesh_pack1_lengths over the 11
 * length-values, so non-degenerate meta stays HELD on the a4 solver
 * stub (clean fail). Worst lane load is 65+32*5 = 225 bits < 256, so
 * the fixed 32B lanes cannot overflow. Zero behavior change: nothing
 * calls this region yet (no emission this round). */

struct lzmesh_pack1_lanes {
    uint8_t b[8][32];
    unsigned nbits[8];
};

unsigned lzmesh_pack1_bitmap(const uint8_t *lens, uint32_t *bm)
{
    uint32_t b = 0u;
    unsigned g;
    unsigned j;
    if (lens == 0 || bm == 0)
        return 0u;
    for (g = 0u; g < 32u; g++) {
        for (j = 0u; j < 8u; j++) {
            if (lens[g * 8u + j] != 0u) {
                b |= (uint32_t)1u << g;
                break;
            }
        }
    }
    if (b == 0u)
        return 0u; /* S2.6 count0->RAW; S3.8 bitmap nonzero. */
    *bm = b;
    return 1u;
}

unsigned lzmesh_pack1_gather(const uint8_t *lens, uint32_t bm, uint8_t *vals)
{
    unsigned g;
    unsigned j;
    unsigned n = 0u;
    if (lens == 0 || vals == 0 || bm == 0u)
        return 0u;
    for (g = 0u; g < 32u; g++) {
        if ((bm & ((uint32_t)1u << g)) == 0u)
            continue;
        for (j = 0u; j < 8u; j++)
            vals[n++] = lens[g * 8u + j];
    }
    return n; /* used = 8*popcount. */
}

static void lzmesh_pack1_put(struct lzmesh_pack1_lanes *out, unsigned lane,
    unsigned val, unsigned n)
{
    while (n-- > 0u) {
        unsigned p = out->nbits[lane]++;
        if ((val & 1u) != 0u)
            out->b[lane][p >> 3] |= (uint8_t)(1u << (p & 7u));
        val >>= 1;
    }
}

unsigned lzmesh_pack1_header(const uint8_t *lens, struct lzmesh_pack1_lanes *out)
{
    uint32_t bm = 0u;
    uint8_t vals[256];
    uint32_t mfreq[11];
    uint8_t mlens[11];
    uint16_t mcodes[11];
    unsigned used = 0u;
    unsigned i;
    unsigned k;
    if (lens == 0 || out == 0)
        return 0u;
    if (lzmesh_pack1_kraft_ok(lens, 256u) == 0u)
        return 0u; /* a4 tables must be Kraft-exact. */
    if (lzmesh_pack1_bitmap(lens, &bm) == 0u)
        return 0u;
    used = lzmesh_pack1_gather(lens, bm, vals);
    if (used == 0u || used > 256u || (used & 7u) != 0u)
        return 0u;
    for (i = 0u; i < 11u; i++)
        mfreq[i] = 0u;
    for (i = 0u; i < used; i++) {
        if (vals[i] > 10u)
            return 0u;
        mfreq[vals[i]]++;
    }
    if (lzmesh_pack1_lengths(mfreq, 11u, 5u, mlens) == 0u)
        return 0u; /* non-degenerate meta HELD on solver stub. */
    for (i = 0u; i < 11u; i++)
        if (mlens[i] > 7u)
            return 0u; /* S3.8 meta max 7: 3b field. */
    if (lzmesh_pack1_canon(mlens, 11u, mcodes, 5u) == 0u)
        return 0u;
    for (k = 0u; k < 8u; k++) {
        for (i = 0u; i < 32u; i++)
            out->b[k][i] = 0u;
        out->nbits[k] = 0u;
    }
    for (i = 0u; i < 11u; i++)
        lzmesh_pack1_put(out, 0u, mlens[i], 3u);
    lzmesh_pack1_put(out, 0u, bm, 32u);
    for (i = 0u; i < used; i++)
        lzmesh_pack1_put(out, i & 7u, mcodes[vals[i]], mlens[vals[i]]);
    return 1u;
}

/* ==== PACK1 S3.5 batch residue (R1: round-robin i%8 counts only) ====
 * S3.5 pins: symbol batches 40 per batch round-robin over 8 lanes
 * (i%8); suffix batches 16 per batch with all-lanes refill;
 * per-symbol refill forbidden. Full 40-batches deal 5 symbols per
 * lane; residue r = n % 40 deals lanes j%8 (40 is a multiple of 8,
 * so the residue restarts at lane 0). Suffix residue r = n % 16
 * likewise over j%8 (16 is a multiple of 8). Counts only, no bit
 * emission (later attempt). Zero behavior change: nothing calls
 * this region yet (no emission this round). */

unsigned lzmesh_pack1_lane_of(unsigned i)
{
    return i & 7u;
}

unsigned lzmesh_pack1_residue(unsigned n)
{
    return n % 40u;
}

unsigned lzmesh_pack1_suf_residue(unsigned n)
{
    return n % 16u;
}

void lzmesh_pack1_lane_counts(unsigned n, unsigned per_lane[8])
{
    unsigned full = (n / 40u) * 5u;
    unsigned r = n % 40u;
    unsigned k;
    unsigned i;
    if (per_lane == 0)
        return;
    for (k = 0u; k < 8u; k++)
        per_lane[k] = full;
    for (i = 0u; i < r; i++)
        per_lane[i & 7u]++;
}

void lzmesh_pack1_suf_counts(unsigned n, unsigned per_lane[8])
{
    unsigned full = (n / 16u) * 2u;
    unsigned r = n % 16u;
    unsigned k;
    unsigned i;
    if (per_lane == 0)
        return;
    for (k = 0u; k < 8u; k++)
        per_lane[k] = full;
    for (i = 0u; i < r; i++)
        per_lane[i & 7u]++;
}

/* ==== PACK1 S3.3 lane index (R1: ib select + size + pack) ====
 * S3.3 pins: no lane header at region start; lane payloads
 * [bo,bo+payload) + index at END; index_bits = last_byte>>3, range
 * 1..23 only (no minimality; ib0/ib24+ reject); index_size =
 * max(4,(7*ib+12)>>3); seven packed LSB-first fields = lane0..6
 * byte lens, lane7 = remainder; starts cumulative each <= payload;
 * fields read via u32le load (read idiom, not a header struct).
 * Encoder layout: field f occupies bits [f*ib,f*ib+ib) from index
 * base; ib sits in the top 5 bits of the last index byte; middle
 * bits zero (ib5 dissection `39 ca 18 5f 2c` pins this exactly).
 * Region MUST exceed index (payload>=1; empty rejects). No rollback
 * rule lives in S3.3 (rollback pins are S5.5/S3.12, outside this
 * round's read allow-list) -> rollback HELD. Zero behavior change:
 * nothing calls this region yet (no emission this round). */

unsigned lzmesh_pack1_index_bits(unsigned need)
{
    unsigned ib = 1u;
    if (need >= (1u << 23))
        return 0u; /* ib24+ rejects; ib0 rejects. */
    while (ib < 23u && need >= (1u << ib))
        ib++;
    return ib;
}

unsigned lzmesh_pack1_index_size(unsigned ib)
{
    unsigned s;
    if (ib == 0u || ib > 23u)
        return 0u;
    s = (7u * ib + 12u) >> 3;
    return s < 4u ? 4u : s;
}

unsigned lzmesh_pack1_index_pack(const unsigned lane[8], uint8_t *out,
    unsigned outcap)
{
    unsigned mx = 0u;
    unsigned ib;
    unsigned size;
    unsigned f;
    unsigned b;
    unsigned bit;
    uint64_t payload = 0u;
    uint64_t start = 0u;
    unsigned k;
    unsigned i;
    if (lane == 0 || out == 0)
        return 0u;
    for (k = 0u; k < 7u; k++) {
        if (lane[k] >= (1u << 23))
            return 0u;
        if (lane[k] > mx)
            mx = lane[k];
    }
    for (k = 0u; k < 8u; k++)
        payload += lane[k];
    if (payload == 0u)
        return 0u; /* empty payload: region==index rejects. */
    ib = lzmesh_pack1_index_bits(mx);
    if (ib == 0u)
        return 0u;
    size = lzmesh_pack1_index_size(ib);
    if (size == 0u || size > outcap)
        return 0u;
    if ((uint64_t)7u * ib + 5u > (uint64_t)size * 8u)
        return 0u;
    for (k = 0u; k < 8u; k++) {
        if (start > payload)
            return 0u; /* S3.3.d starts cumulative <= payload. */
        start += lane[k];
    }
    for (i = 0u; i < size; i++)
        out[i] = 0u;
    bit = 0u;
    for (f = 0u; f < 7u; f++) {
        unsigned v = lane[f];
        for (b = 0u; b < ib; b++) {
            if ((v & 1u) != 0u)
                out[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
            v >>= 1;
            bit++;
        }
    }
    bit = size * 8u - 5u; /* ib in top 5 bits of last byte. */
    for (b = 0u; b < 5u; b++) {
        if ((ib & 1u) != 0u)
            out[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
        ib >>= 1;
        bit++;
    }
    return size;
}

/* ==== PACK1 S5.6+S5.5 self-tests (R1: oracles + MUST-exact checks) ====
 * S5.5 pins: 0->RAW; all-equal->REPEAT; 8n<=n+73 (n<=10)->RAW;
 * speculative HUF + rollback iff bits>=8n (counts-restored /
 * bytes-kept, residue in pads); tok-RAW at counts 10-13, HUF at 14;
 * const73 header cost. S5.6 pins: RULE-PAD1 pad bit0 =
 * suffix-assigned ((distc>=8)||(lane<distc)); RULE-PADHI B-TAB on
 * the L00 path, else 0; final byte = data|(P<<m), m = C mod 8,
 * k in 0..7. Modes: 0=RAW, 1=REPEAT, 2=speculative-HUF. Oracles +
 * fail-count checks only: no runner, no emission, no callers.
 * Zero behavior change. Test4 T5 vector + alleq-vs-small order
 * HELD (need solver/emission / ambiguous order). */

unsigned lzmesh_pack1_mode(unsigned n, unsigned alleq)
{
    if (n == 0u)
        return 0u; /* 0->RAW (beats REPEAT). */
    if (alleq != 0u)
        return 1u; /* all-equal->REPEAT. */
    if (n <= 10u)
        return 0u; /* 8n<=n+73 -> RAW. */
    return 2u; /* speculative HUF. */
}

unsigned lzmesh_pack1_tokmode(unsigned count)
{
    if (count <= 13u)
        return 0u; /* tok-RAW at 10-13 (and below). */
    return 2u; /* HUF at 14+. */
}

unsigned lzmesh_pack1_rollback(unsigned bits, unsigned n)
{
    if ((uint64_t)bits >= (uint64_t)8u * n)
        return 1u;
    return 0u;
}

unsigned lzmesh_pack1_pad0(unsigned distc, unsigned lane)
{
    if (distc >= 8u || lane < distc)
        return 1u;
    return 0u;
}

unsigned lzmesh_pack1_btab(unsigned lastL, unsigned k)
{
    if (lastL == 7u) {
        if (k == 6u)
            return 32u;
        if (k == 7u)
            return 96u;
        return 0u;
    }
    if (lastL == 9u) {
        if (k == 7u)
            return 72u;
        if (k >= 4u && k <= 6u)
            return 8u;
        return 0u;
    }
    if (lastL == 10u) {
        if (k == 4u || k == 5u)
            return 8u;
        if (k == 6u || k == 7u)
            return 40u;
        return 0u;
    }
    return 0u;
}

unsigned lzmesh_pack1_padhi(unsigned lastL, unsigned k, unsigned l00)
{
    if (l00 == 0u)
        return 0u; /* non-L00 hi bits always 0. */
    return lzmesh_pack1_btab(lastL, k);
}

unsigned lzmesh_pack1_padk(unsigned cmod8)
{
    return (8u - (cmod8 & 7u)) & 7u; /* k in 0..7 by construction. */
}

unsigned lzmesh_pack1_final(unsigned data, unsigned m, unsigned p)
{
    if (m > 7u)
        return data & 0xFFu;
    return (data | (p << m)) & 0xFFu;
}

unsigned lzmesh_pack1_selftest_s55(void)
{
    unsigned f = 0u;
    unsigned i;
    if (lzmesh_pack1_mode(0u, 0u) != 0u)
        f++;
    if (lzmesh_pack1_mode(0u, 1u) != 0u)
        f++;
    if (lzmesh_pack1_mode(51u, 1u) != 1u)
        f++;
    if (lzmesh_pack1_mode(1000u, 1u) != 1u)
        f++;
    for (i = 1u; i <= 10u; i++)
        if (lzmesh_pack1_mode(i, 0u) != 0u)
            f++;
    if (lzmesh_pack1_mode(11u, 0u) != 2u)
        f++;
    if (lzmesh_pack1_mode(51u, 0u) != 2u)
        f++;
    for (i = 0u; i <= 12u; i++) {
        unsigned ineq = (8u * i <= i + 73u) ? 1u : 0u;
        unsigned small = (i <= 10u) ? 1u : 0u;
        if (ineq != small)
            f++;
    }
    for (i = 0u; i <= 13u; i++)
        if (lzmesh_pack1_tokmode(i) != 0u)
            f++;
    for (i = 14u; i <= 20u; i++)
        if (lzmesh_pack1_tokmode(i) != 2u)
            f++;
    if (lzmesh_pack1_rollback(7u, 1u) != 0u)
        f++;
    if (lzmesh_pack1_rollback(8u, 1u) != 1u)
        f++;
    if (lzmesh_pack1_rollback(9u, 1u) != 1u)
        f++;
    if (lzmesh_pack1_rollback(79u, 10u) != 0u)
        f++;
    if (lzmesh_pack1_rollback(80u, 10u) != 1u)
        f++;
    if (lzmesh_pack1_rollback(407u, 51u) != 0u)
        f++;
    if (lzmesh_pack1_rollback(408u, 51u) != 1u)
        f++;
    if (lzmesh_pack1_rollback(409u, 51u) != 1u)
        f++;
    return f;
}

unsigned lzmesh_pack1_selftest_s56(void)
{
    unsigned f = 0u;
    unsigned m;
    if (lzmesh_pack1_pad0(0u, 0u) != 0u)
        f++;
    if (lzmesh_pack1_pad0(0u, 7u) != 0u)
        f++;
    if (lzmesh_pack1_pad0(5u, 0u) != 1u)
        f++;
    if (lzmesh_pack1_pad0(5u, 4u) != 1u)
        f++;
    if (lzmesh_pack1_pad0(5u, 5u) != 0u)
        f++;
    if (lzmesh_pack1_pad0(5u, 7u) != 0u)
        f++;
    if (lzmesh_pack1_pad0(7u, 6u) != 1u)
        f++;
    if (lzmesh_pack1_pad0(7u, 7u) != 0u)
        f++;
    if (lzmesh_pack1_pad0(8u, 0u) != 1u)
        f++;
    if (lzmesh_pack1_pad0(8u, 7u) != 1u)
        f++;
    if (lzmesh_pack1_pad0(200u, 7u) != 1u)
        f++;
    if (lzmesh_pack1_btab(7u, 6u) != 32u)
        f++;
    if (lzmesh_pack1_btab(7u, 7u) != 96u)
        f++;
    if (lzmesh_pack1_btab(7u, 5u) != 0u)
        f++;
    if (lzmesh_pack1_btab(9u, 4u) != 8u)
        f++;
    if (lzmesh_pack1_btab(9u, 5u) != 8u)
        f++;
    if (lzmesh_pack1_btab(9u, 6u) != 8u)
        f++;
    if (lzmesh_pack1_btab(9u, 7u) != 72u)
        f++;
    if (lzmesh_pack1_btab(9u, 3u) != 0u)
        f++;
    if (lzmesh_pack1_btab(10u, 4u) != 8u)
        f++;
    if (lzmesh_pack1_btab(10u, 5u) != 8u)
        f++;
    if (lzmesh_pack1_btab(10u, 6u) != 40u)
        f++;
    if (lzmesh_pack1_btab(10u, 7u) != 40u)
        f++;
    if (lzmesh_pack1_btab(10u, 3u) != 0u)
        f++;
    if (lzmesh_pack1_btab(8u, 6u) != 0u)
        f++;
    if (lzmesh_pack1_padhi(7u, 6u, 0u) != 0u)
        f++;
    if (lzmesh_pack1_padhi(7u, 6u, 1u) != 32u)
        f++;
    if (lzmesh_pack1_padhi(9u, 7u, 1u) != 72u)
        f++;
    if (lzmesh_pack1_padk(0u) != 0u)
        f++;
    if (lzmesh_pack1_padk(1u) != 7u)
        f++;
    if (lzmesh_pack1_padk(3u) != 5u)
        f++;
    if (lzmesh_pack1_padk(7u) != 1u)
        f++;
    for (m = 0u; m < 8u; m++)
        if (lzmesh_pack1_padk(m) > 7u)
            f++;
    if (lzmesh_pack1_final(0x05u, 3u, 1u) != 0x0Du)
        f++;
    if (lzmesh_pack1_final(0x00u, 0u, 1u) != 0x01u)
        f++;
    if (lzmesh_pack1_final(0xFFu, 7u, 1u) != 0xFFu)
        f++;
    if (lzmesh_pack1_final(0x05u, 8u, 1u) != 0x05u)
        f++;
    return f;
}

unsigned lzmesh_pack1_selftest_pack1(void)
{
    return lzmesh_pack1_selftest_s55() + lzmesh_pack1_selftest_s56();
}
/* === SHAPE-E5 2-token concat-sequential-periodic, levels 1/5/9 (owner: u33) ===
 * pack9: closes e5 (2-token gap). Input = Axa ++ Bxb, A=1..p1 (p1 in
 * 4..8), B=1..p2 (p2>=9), a,b>=2. Parse (e5-PROVEN, sequential-PROVEN):
 * t0 = p1 lits + new d=p1 maxext m1=a*p1 (extends one p1-block into B:
 * B[0:p1]==A, stops at B[p1]=p1+1 vs B[0]=1); t1 = (p2-p1) lits +
 * new d=p2 maxext m2=(b-1)*p2 to exact end. Both tokens 0xff
 * (lit-esc + mc-esc); tok-REPEAT; len-RAW (lc bytes); dist-RAW 2B
 * (d1=p1 sb0, d2=p2 sb2 1..7); suffix slot1 -> lane1 1B; index ib1
 * 4B (fields [0,1,0,0,0,0,0]); region 5B; footer (2,lc,p2,2) + END.
 * Gates: sequential (gap-take-free PROOF: gap bytes exceed all prior,
 * so no take of any length/dist/level inside the lit gap; take#1/#2
 * rep-miss proved; detection (p1,p2) UNIQUE); floors (u3), long-take
 * both (lazy-skip), take#1 u12_slots_clean, take#2 dense-VIS
 * (P2-P1<=256 L1-only, chain-positional inserts incl match regions:
 * e5 d44-read PROVES 70 inserted) last-writer dual-hb ANALOG of
 * slots_clean (hb(n),hb(n-P2)), no-backext-1, lit-rollback via pack1
 * bits (u4 solver is NOT oracle-identical on ties: pack9 probe),
 * modes (1,RAW-or-REPEAT,0,0), comp_keep (S5.3, no size compare).
 * Caps: p2<=2048 (U33-P2CAP, probe buf). Open (u34): general-A/B gap
 * takes, take#2 hb-dual PROOF, len-long+REPEAT footer convention.
 * S5.9.a single parse. PERF: O(1) fast-path (needs 1,2,3,4 prefix),
 * O(56n) worst detect; stack ~2.5KB. */
#define LZMESH_U33_P1MIN 4u
#define LZMESH_U33_P1MAX 8u
#define LZMESH_U33_P2MIN 9u
#define LZMESH_U33_P2CAP 2048u
#define LZMESH_U33_C1 0x995D97CB4C1DB100ULL /* CONST-TABLE #1 (u2-dup) */
#define LZMESH_U33_C2 0x97CB4C1DB1000000ULL /* CONST-TABLE #2 (u2-dup) */
#define LZMESH_U33_C3 0x3779B100U /* CONST-TABLE #3 (u2-dup) */

static uint64_t lzmesh_u33_load(const uint8_t *p, unsigned n) {
    uint64_t w = 0u;
    unsigned i;
    for (i = 0u; i < n; i++)
        w |= (uint64_t)p[i] << (8u * i); /* LE (S5.7/Q25) */
    return w;
}

static uint32_t lzmesh_u33_h1(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_U33_C1) >> (64u - hb));
}

static uint32_t lzmesh_u33_h2(uint64_t w, unsigned hb) {
    return (uint32_t)((w * LZMESH_U33_C2) >> (64u - hb));
}

static uint32_t lzmesh_u33_h3(uint32_t w) {
    return (w * LZMESH_U33_C3) >> 20u;
}

/* Maximal match extension of dist d from pos `from` (bytes). */
static size_t lzmesh_u33_maxext(const uint8_t *s, size_t n, size_t from,
                                size_t d) {
    size_t m = 0u;
    if (s == NULL || d == 0u || from < d || from >= n)
        return 0u;
    while (from + m < n && s[from + m] == s[from + m - d])
        m++;
    return m;
}

/* Sequential 1..p block at s (need p bytes). */
static int lzmesh_u33_seq(const uint8_t *s, size_t p) {
    size_t i;
    if (s == NULL)
        return 0;
    for (i = 0u; i < p; i++)
        if (s[i] != (uint8_t)(i + 1u))
            return 0;
    return 1;
}

/* Take#2 slot agreement: dense-inserts last-writer==S2 on every live
 * hash (slots_clean ANALOG; inserts 0..P2-1 dense: L5/L9 always (u12
 * precedent), L1 under want's density gate; e5: slot reads 70).
 * Dual-hb (hb(n),hb(n-P2)) mirrors U12-HB. */
static int lzmesh_u33_take2_clean(const uint8_t *s, size_t n, size_t p2pos,
                                  size_t s2, int level) {
    static const unsigned bigL[2] = { 7u, 5u };
    unsigned hbv[2];
    int hbi, q, h;
    size_t i;
    if (s == NULL || s2 >= p2pos || p2pos >= n)
        return 0;
    if (level == 1 && p2pos + 7u > n)
        return 0; /* G6-analog: L1 h1 must be loadable at P2 */
    if (n <= p2pos)
        return 0;
    hbv[0] = lzmesh_u2_hash_bits(n, level);
    hbv[1] = lzmesh_u2_hash_bits(n - p2pos, level);
    for (hbi = 0; hbi < 2; hbi++) {
        unsigned hb = hbv[hbi];
        if (hb == 0u || hb > 21u)
            return 0;
        for (q = 0; q < 2; q++) { /* big-table queries h1,h2 */
            unsigned Lq = bigL[q];
            uint64_t hqp;
            uint32_t S;
            if (level == 1 && q > 0)
                break; /* L1 h1-only */
            if (p2pos + Lq > n)
                continue; /* dead query */
            hqp = lzmesh_u33_load(s + p2pos, Lq);
            S = (q == 0) ? lzmesh_u33_h1(hqp, hb)
                : lzmesh_u33_h2(hqp, hb);
            for (i = 0u; i < p2pos; i++) {
                for (h = 0; h < 2; h++) { /* big-table inserts */
                    unsigned Lh = bigL[h];
                    uint32_t T;
                    if (level == 1 && h > 0)
                        break;
                    if (i + Lh > n)
                        continue;
                    T = (h == 0)
                        ? lzmesh_u33_h1(lzmesh_u33_load(s + i, Lh), hb)
                        : lzmesh_u33_h2(lzmesh_u33_load(s + i, Lh), hb);
                    if (T == S && i > s2)
                        return 0; /* last-writer-wins (e5-PROVEN:
                                   * slot reads 70, not 0); earlier
                                   * same-hash inserts overwritten. */
                }
            }
        }
        if (level != 1) { /* small-table query h3 */
            uint32_t S, T;
            if (p2pos + 3u > n)
                return 0; /* h3 must be live */
            S = lzmesh_u33_h3((uint32_t)lzmesh_u33_load(s + p2pos, 3u));
            for (i = 0u; i < p2pos; i++) {
                if (i + 3u > n)
                    continue;
                T = lzmesh_u33_h3((uint32_t)lzmesh_u33_load(s + i, 3u));
                if (T == S && i > s2)
                    return 0; /* last-writer-wins (same rule). */
            }
        }
    }
    return 1;
}

/* Speculative lit-HUF bit cost via pack1 (oracle-identical solver).
 * bits = 33 (meta) + 32 (bitmap) + meta-coded vals + coded syms.
 * Returns 1 with *bits on full build success, else 0 (decline). */
static int lzmesh_u33_lit_huff_bits(const uint8_t *lits, size_t n,
                                    uint64_t *bits) {
    uint32_t freq[256], mfreq[11];
    uint8_t lens[256], mlens[11], vals[256];
    uint32_t bm = 0u;
    uint64_t b;
    unsigned used, i, nz;
    if (lits == NULL || bits == NULL || n == 0u || n > 65536u)
        return 0;
    for (i = 0u; i < 256u; i++)
        freq[i] = 0u;
    for (i = 0u; i < (unsigned)n; i++)
        freq[lits[i]]++;
    if (!lzmesh_pack1_lengths(freq, 256u, 10u, lens))
        return 0;
    if (!lzmesh_pack1_bitmap(lens, &bm))
        return 0;
    used = lzmesh_pack1_gather(lens, bm, vals);
    if (used == 0u || used > 256u || (used & 7u) != 0u)
        return 0;
    for (i = 0u; i < 11u; i++)
        mfreq[i] = 0u;
    for (i = 0u; i < used; i++) {
        if (vals[i] > 10u)
            return 0;
        mfreq[vals[i]]++;
    }
    if (!lzmesh_pack1_lengths(mfreq, 11u, 5u, mlens))
        return 0;
    nz = 0u; /* degenerate single-value meta: bit shape unowned */
    for (i = 0u; i < 11u; i++)
        if (mfreq[i] != 0u)
            nz++;
    if (nz < 2u)
        return 0;
    b = 33u + 32u;
    for (i = 0u; i < used; i++)
        b += mlens[vals[i]];
    for (i = 0u; i < (unsigned)n; i++)
        b += lens[lits[i]];
    *bits = b;
    return 1;
}

/* Level-free structural scan: sequential A/B, mismatch-stopped m1 =
 * a*p1, m2 to exact end, both tokens 0xff, long-take both,
 * no-backext-1, len no-H mode, lit-rollback taken. Detection UNIQUE
 * (sequential). Returns 1 with (p1,p2,m1,m2), else 0. */
static int lzmesh_u33_scan(const uint8_t *src, size_t size, unsigned *p1,
                           unsigned *p2, size_t *m1, size_t *m2) {
    unsigned q1, q2;
    for (q1 = LZMESH_U33_P1MIN; q1 <= LZMESH_U33_P1MAX; q1++) {
        size_t e1, a, bs, brem;
        if ((size_t)q1 + 2u > size)
            continue;
        if (!lzmesh_u33_seq(src, q1))
            continue; /* A sequential */
        e1 = lzmesh_u33_maxext(src, size, q1, q1);
        if (e1 < 2u || (size_t)q1 + e1 >= size)
            continue; /* mismatch-stopped */
        if (e1 % (size_t)q1 != 0u)
            continue;
        a = e1 / (size_t)q1;
        if (a < 2u)
            continue;
        bs = a * (size_t)q1;
        if (size < bs)
            continue;
        brem = size - bs;
        if (brem < 2u * LZMESH_U33_P2MIN)
            continue;
        for (q2 = LZMESH_U33_P2MIN; q2 <= (unsigned)(brem / 2u); q2++) {
            size_t P2, S2, e2, lit0, mc0, lit1, mc1;
            uint8_t lb[16];
            uint8_t litprobe[LZMESH_U33_P2CAP];
            unsigned li = 0u, i, k, en;
            uint8_t eb[5];
            uint64_t hbits;
            unsigned ml;
            int alleq = 1;
            if (q2 > LZMESH_U33_P2CAP)
                break;
            if (brem % (size_t)q2 != 0u)
                continue;
            if (brem / (size_t)q2 < 2u)
                continue;
            if (!lzmesh_u33_seq(src + bs, q2))
                continue; /* B sequential */
            P2 = bs + (size_t)q2;
            S2 = bs;
            e2 = lzmesh_u33_maxext(src, size, P2, (size_t)q2);
            if (P2 + e2 != size || e2 != brem - (size_t)q2)
                continue; /* runs exactly to end */
            lit0 = (size_t)q1 - 1u;
            lit1 = (size_t)q2 - (size_t)q1;
            mc0 = e1 - 2u; /* e1>=2 (stop gate), e2>=9 (end gate) */
            mc1 = e2 - 2u;
            if (lit0 < 3u || mc0 < 31u || lit1 < 3u || mc1 < 31u)
                continue; /* both tokens 0xff */
            if (e1 > 0x7fffffffu || e2 > 0x7fffffffu)
                continue;
            if (!lzmesh_u3_long_take((uint32_t)e1))
                continue;
            if (!lzmesh_u3_long_take((uint32_t)e2))
                continue;
            if (src[P2 - 1u] == src[S2 - 1u])
                continue; /* no-backext-1 */
            lb[li++] = (uint8_t)(lit0 - 3u);
            en = lzmesh_u7_len_escape_write((uint32_t)(mc0 - 31u), eb);
            if (en == 0u || li + en > sizeof lb)
                continue;
            for (k = 0u; k < en; k++)
                lb[li++] = eb[k];
            lb[li++] = (uint8_t)(lit1 - 3u);
            en = lzmesh_u7_len_escape_write((uint32_t)(mc1 - 31u), eb);
            if (en == 0u || li + en > sizeof lb)
                continue;
            for (k = 0u; k < en; k++)
                lb[li++] = eb[k];
            for (i = 1u; i < li; i++)
                if (lb[i] != lb[0])
                    alleq = 0;
            ml = lzmesh_u4_mode_trivial(li, alleq);
            if (ml != LZMESH_U4_MODE_RAW && ml != LZMESH_U4_MODE_REPEAT)
                continue; /* no-H only */
            for (i = 0u; i < q1; i++)
                litprobe[i] = src[i];
            for (i = 0u; i < q2 - q1; i++)
                litprobe[q1 + i] = src[bs + (size_t)q1 + i];
            if (!lzmesh_u33_lit_huff_bits(litprobe, (size_t)q2, &hbits))
                continue;
            if (!lzmesh_u4_huff_rollback(hbits, (uint32_t)q2))
                continue; /* HUF-kept world: not our shape */
            *p1 = q1;
            *p2 = q2;
            *m1 = e1;
            *m2 = e2;
            return 1;
        }
    }
    return 0;
}

/* SHAPE-E5 layout: modes (tok REPEAT, len trivial, lit RAW, dist RAW),
 * bo = 9+lit+1+lenb+2, fo = bo+1+4. */
int lzmesh_u33_layout(size_t size, unsigned p1, unsigned p2, size_t m1,
                      size_t m2, uint32_t *lenB, uint32_t *lenC,
                      uint32_t *modes, uint32_t *bo, uint32_t *fo) {
    uint8_t lb[16];
    unsigned li = 0u, i;
    uint32_t mc0, mc1, mlen, lenb;
    unsigned sb, low;
    uint32_t suf;
    int all_eq = 1;
    (void)size;
    if (lenB == NULL || lenC == NULL || modes == NULL || bo == NULL
        || fo == NULL)
        return 0;
    if (p1 < LZMESH_U33_P1MIN || p1 > LZMESH_U33_P1MAX
        || p2 < LZMESH_U33_P2MIN || p2 <= p1
        || p2 > LZMESH_U33_P2CAP)
        return 0;
    if (m1 < 33u || m1 > 0x7fffffffu || m2 < 33u || m2 > 0x7fffffffu)
        return 0;
    lzmesh_u4_dist_split_nc((uint32_t)p2, &sb, &low, &suf);
    if (sb == 0u || sb > 7u)
        return 0; /* d2>=9 sb>=1; 1 lane byte fits sb<=7 */
    if (suf >= (1u << sb))
        return 0;
    (void)low;
    lb[li++] = (uint8_t)((size_t)p1 - 1u - 3u); /* lit0 esc3 rest */
    mc0 = (uint32_t)m1 - 2u;
    if (mc0 <= 30u)
        return 0;
    {
        uint8_t eb[5];
        unsigned en = lzmesh_u7_len_escape_write(mc0 - 31u, eb);
        if (en == 0u || li + en > sizeof lb)
            return 0;
        for (i = 0u; i < en; i++)
            lb[li++] = eb[i];
    }
    lb[li++] = (uint8_t)((size_t)p2 - (size_t)p1 - 3u); /* lit1 rest */
    mc1 = (uint32_t)m2 - 2u;
    if (mc1 <= 30u)
        return 0;
    {
        uint8_t eb[5];
        unsigned en = lzmesh_u7_len_escape_write(mc1 - 31u, eb);
        if (en == 0u || li + en > sizeof lb)
            return 0;
        for (i = 0u; i < en; i++)
            lb[li++] = eb[i];
    }
    for (i = 1u; i < li; i++)
        if (lb[i] != lb[0])
            all_eq = 0;
    mlen = lzmesh_u4_mode_trivial(li, all_eq);
    if (mlen != LZMESH_U4_MODE_RAW && mlen != LZMESH_U4_MODE_REPEAT)
        return 0;
    lenb = (mlen == LZMESH_U4_MODE_REPEAT) ? 1u : li;
    *modes = lzmesh_u4_modes_pack(LZMESH_U4_MODE_REPEAT, mlen,
                                  LZMESH_U4_MODE_RAW,
                                  LZMESH_U4_MODE_RAW);
    *bo = 9u + (uint32_t)p2 + 1u + lenb + 2u;
    *fo = *bo + 1u + 4u; /* lane1 1B + index 4B (ib1) */
    *lenB = lenb;
    *lenC = li;
    return 1;
}

int lzmesh_u33_want(const uint8_t *src, size_t size, int level) {
    unsigned p1, p2;
    size_t m1, m2, P2, S2;
    uint32_t lenB, lenC, modes, bo, fo;
    if (src == NULL || size <= 1u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1 && level != 5 && level != 9)
        return 0; /* L0 excluded: no finding (S5.7c) */
    if (lzmesh_u9_is_run(src, size))
        return 0;
    if (lzmesh_u12_period(src, size) != 0)
        return 0; /* single-periodic owned by u12/u15/u18 */
    if (lzmesh_u18_period(src, size) != 0)
        return 0;
    if (!lzmesh_u33_scan(src, size, &p1, &p2, &m1, &m2))
        return 0;
    if (!lzmesh_u3_take_floor_ok(level, 0, (uint32_t)m1))
        return 0;
    if (!lzmesh_u3_take_floor_ok(level, 0, (uint32_t)m2))
        return 0;
    if (!lzmesh_u12_slots_clean(src, size, (size_t)p1, level))
        return 0; /* take#1 */
    S2 = m1 / (size_t)p1 * (size_t)p1; /* a*p1 == Bstart */
    P2 = S2 + (size_t)p2;
    if (level == 1 && P2 - (size_t)p1 > 256u)
        return 0; /* L1 dense-VIS for take#2 inserts */
    if (!lzmesh_u33_take2_clean(src, size, P2, S2, level))
        return 0; /* take#2 */
    if (!lzmesh_u33_layout(size, p1, p2, m1, m2, &lenB, &lenC, &modes,
                           &bo, &fo))
        return 0;
    (void)lenB;
    (void)lenC;
    (void)modes;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, (uint32_t)p2,
                               1);
}

size_t lzmesh_u33_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size) {
    unsigned p1, p2;
    size_t m1, m2, need, s, bs;
    uint32_t lenB, lenC, modes, bo, fo, mc0, mc1;
    uint8_t lb[16], dsym1, dsym2, lane1, idx[4];
    unsigned li = 0u, i, sb, low;
    uint32_t suf;
    if (dst == NULL || src == NULL)
        return 0;
    if (!lzmesh_u33_scan(src, size, &p1, &p2, &m1, &m2))
        return 0; /* level-free; want() owns level gates (u18 prec.) */
    if (!lzmesh_u33_layout(size, p1, p2, m1, m2, &lenB, &lenC, &modes,
                           &bo, &fo))
        return 0;
    need = (size_t)fo + 10u + 1u;
    if (dst_capacity < need)
        return 0;
    if (!lzmesh_u4_comp_gates_ok((uint32_t)size, bo, fo))
        return 0;
    if (9u + (uint32_t)p2 + 1u + lenB + 2u != bo)
        return 0;
    if (fo != bo + 1u + 4u)
        return 0;
    bs = m1 / (size_t)p1 * (size_t)p1; /* a*p1 == Bstart */
    if (bs + (size_t)p2 > size)
        return 0;
    mc0 = (uint32_t)m1 - 2u;
    mc1 = (uint32_t)m2 - 2u;
    if (lzmesh_u7_token_new(3u, 31u) != 0xffu)
        return 0;
    lb[li++] = (uint8_t)((size_t)p1 - 1u - 3u);
    {
        uint8_t eb[5];
        unsigned en = lzmesh_u7_len_escape_write(mc0 - 31u, eb);
        if (en == 0u || li + en > sizeof lb)
            return 0;
        for (i = 0u; i < en; i++)
            lb[li++] = eb[i];
    }
    lb[li++] = (uint8_t)((size_t)p2 - (size_t)p1 - 3u);
    {
        uint8_t eb[5];
        unsigned en = lzmesh_u7_len_escape_write(mc1 - 31u, eb);
        if (en == 0u || li + en > sizeof lb)
            return 0;
        for (i = 0u; i < en; i++)
            lb[li++] = eb[i];
    }
    if (li != lenC)
        return 0;
    if (lenB == 1u) { /* REPEAT-fold: all bytes must agree */
        for (i = 1u; i < li; i++)
            if (lb[i] != lb[0])
                return 0;
    } else if (lenB != li) {
        return 0;
    }
    dsym1 = (uint8_t)((uint32_t)p1 - 1u); /* sb0: low = d-1 */
    lzmesh_u4_dist_split_nc((uint32_t)p2, &sb, &low, &suf);
    if (sb == 0u || sb > 7u || suf >= (1u << sb))
        return 0;
    dsym2 = (uint8_t)((sb << 3) | (low & 7u));
    { /* lane1: suf sb-bits LSB-first + PAD1 + B-TAB hi (S5.6);
       * slot1 -> lane1 (slot0 sb0 advances, emits nothing). */
        unsigned m = sb & 7u;
        unsigned k = 8u - sb; /* L=1 byte */
        uint8_t data;
        uint32_t P;
        if (!lzmesh_u4_k_in_range(k))
            return 0;
        data = (uint8_t)(suf & ((1u << sb) - 1u)); /* sb<=7 above */
        if (lzmesh_u4_pad0_one(2u, 1u))
            data |= (uint8_t)(1u << m);
        P = lzmesh_u4_btab(1u, k); /* lastL = lane1 byte-len 1 */
        lane1 = lzmesh_u4_lane_final(data, P, m);
    }
    { /* index at END (S3.3): fields [0,1,0,0,0,0,0], ib=1. */
        unsigned f[7] = { 0u, 1u, 0u, 0u, 0u, 0u, 0u };
        unsigned ib = 1u;
        unsigned idxsz = (7u * ib + 12u) >> 3;
        if (idxsz < 4u)
            idxsz = 4u;
        if (idxsz != 4u)
            return 0;
        idx[0] = 0u;
        idx[1] = 0u;
        idx[2] = 0u;
        idx[3] = 0u;
        for (i = 0u; i < 7u; i++)
            idx[i * ib / 8u] |= (uint8_t)((f[i] & ((1u << ib) - 1u))
                << (i * ib % 8u));
        idx[idxsz - 1u] |= (uint8_t)(ib << 3);
    }
    lzmesh_u7_comp_header_emit(dst, (uint32_t)size, bo, fo);
    s = 9u;
    for (i = 0u; i < p1; i++)
        dst[s++] = src[i]; /* LIT: A */
    for (i = 0u; i < p2 - p1; i++)
        dst[s++] = src[bs + (size_t)p1 + i]; /* LIT: B tail */
    dst[s++] = 0xffu; /* TOK (REPEAT 1B) */
    if (lenB == 1u) {
        dst[s++] = lb[0]; /* LEN (REPEAT 1B) */
    } else {
        for (i = 0u; i < li; i++)
            dst[s++] = lb[i]; /* LEN (RAW lcB) */
    }
    dst[s++] = dsym1; /* DIST (RAW 2B) */
    dst[s++] = dsym2;
    if (s != bo)
        return 0;
    dst[s++] = lane1;
    for (i = 0u; i < 4u; i++)
        dst[s++] = idx[i];
    if (s != fo)
        return 0;
    lzmesh_u4_footer_emit(dst + fo, modes, 2u, lenC, (uint32_t)p2, 2u);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}
/* === SHAPE-H1 single-terminator lit-HUF, level 1 only (owner: u35) ===
 * pack9: closes h1 (HUF-payload gap). Input: L1-no-take-certified
 * (no repeated triple anywhere -> max ml 2 < 4 <= fresh floor (3,7];
 * no rep0-2B head at P<=257 (step1-zone takes; P>=258 always MISS:
 * even P unvis, odd P has S=P-1 even>=258 unvis; P>=513 S never
 * VIS); L1 probes rep0 + single-hash only, no lazy). Parse FORCED:
 * single token 0xc0 (lit-esc + rep0 mc0) + overhang-2 terminator
 * (u29 precedent, S5.3.a, G-TERM clamp): lits = input bytes
 * (1 pre + n-1), tc1 dc0 litc==ds. lit-HUF via pack1 solver
 * (ORACLE-IDENTICAL lengths, pack9 probe; u4 solver diverges on
 * ties — never use here), kept iff bits<8n (S5.5); header =
 * 11x3b meta + 32b bitmap (lane0, no-align: h1-PROVEN) + used vals
 * dealt i%8; 510 syms input-order dealt i%8 (h1-PROVEN); pads all 0
 * (distc=0 PAD1=0; B-TAB-listed (lastL,k) DECLINED: L00-ambiguity
 * containment, U35-BTAB); index via pack1_index_pack; footer
 * (1,lc_exp,n,0) + END. tok single-c0 REPEAT; len single-symbol
 * bytes-tested (all-eq->REPEAT else RAW: h1 5B RAW-PROVEN).
 * Emit takes level (re-certifies: never diverges from want).
 * Caps: n<=16393 (U35-NCAP, O(n^2) fresh6 cert; R31 FIX-U). Open (u34):
 * L5/L9 no-take certs, B-TAB-listed lanes (U3 discriminator),
 * meta-degenerate (E-B6), general HUF scheduler. PERF: O(n^2)
 * cert + O(n) emit, two-pass direct-to-dst (no lane bufs);
 * stack ~1.5KB. */
#define LZMESH_U35_NCAP 16393u /* R31 FIX-U (O35NCAP): NCAP is a compute
 * cap, not a fidelity cap -- oracle fires tok1-litonly past 4096
 * (s17/s65-n4097 + s40-n9999 e01). Lift to the U36 single-block
 * precedent (lengths/layout/emit already proven to 16393 via u36;
 * only the O(n^2) cert is newly admitted past 4096). */
#define LZMESH_U35_P1VIS_MAX 257u
/* L0 single-block litonly cap (R4: oracle max-single n=16393, k=2
 * alphabet bisect; first multi block ds=16385; matches u16 M12
 * "n<=16393 single" precedent). L0 needs no O(n^2) cert (no
 * finding), so lengths/layout run O(n) to this cap. */
#define LZMESH_U36_NCAP 16393u

/* No fresh (dist>=2) match with ml>=6 anywhere (L1 fresh floor 6,
 * R19-probed: ml6-10 take, ml3-5 skip; superset of oracle takes which
 * additionally need P-VIS+S-VIS). Replaces no-triple (ml<=2), which
 * spuriously vetoed floor-lawful litonly inputs (R20: 22 O35 cells
 * with max fresh ml 3-5, oracle tok=1). O(n^2 x 6); n<=NCAP. */
static int lzmesh_u35_no_fresh6(const uint8_t *s, size_t n) {
    size_t i, j;
    if (s == NULL || n > LZMESH_U35_NCAP)
        return 0;
    if (n < 7u)
        return 1;
    for (j = 2u; j < n; j++) {
        for (i = 0u; i + 2u <= j; i++) {
            size_t ml = 0u;
            while (ml < 6u && j + ml < n && s[i + ml] == s[j + ml])
                ml++;
            if (ml >= 6u)
                return 0;
        }
    }
    return 1;
}

/* No triple-head on the L1 stride chain (U1: R22 P<=257 + TAILREP
 * FALSIFIED 26/26 -- s07-class oracle takes rep triples at 329..4073;
 * take iff triple-head visited by the step-1+(lit>>8) chain from 1
 * with p+9<=n elig; doubles never take (T1d); offchain small-tail
 * r+t>=10 never takes (T1g 7/7). Beds tmp/u1_bed_t1*.py. First-take
 * existence only: later takes re-anchor the chain, which the u37
 * parse (not the cert) replays. P==0 impossible (needs s[-1]). */
static int lzmesh_u35_no_rep_take(const uint8_t *s, size_t n) {
    size_t p, litpos;
    if (s == NULL)
        return 0;
    if (n < 3u)
        return 1;
    p = 1u;
    litpos = 1u;
    while (p < n) {
        if (p + 9u > n)
            break;
        if (s[p] == s[p - 1u] && s[p + 1u] == s[p])
            return 0;
        p += (p - litpos) >> 8;
        p += 1u;
    }
    return 1;
}

static int lzmesh_u35_cert(const uint8_t *s, size_t n) {
    if (n < 4u || n > LZMESH_U35_NCAP)
        return 0;
    if (!lzmesh_u35_no_fresh6(s, n))
        return 0;
    return lzmesh_u35_no_rep_take(s, n);
}

/* C2b RANK-ASSIGN (30-cell black-box IDENTICAL: s0_0/s0_4/s0_8/s0_23/
 * min7 exact synth + 25 autoid): solved length multiset is dealt by
 * (freq desc, sym desc) rank. Larger symbols take shorter lengths
 * among ties. Pure tie permute: counts per length, mfreq, data bits
 * unchanged; only straddling ties move (else exact no-op). */
/* R13-L0RES-D1: stable LSD byte radix by u32 key (ASC), 1-4 passes
 * from key-OR (L0/g1 freqs < 2^16: always 2 passes). Same P13-RANK
 * proof: (key, distinct-sym) initial order is a strict total order,
 * stable radix emits that unique permutation, identical to the
 * insertion/msort it replaces. list/tmp hold n entries (n<=256).
 * Caller gates small n (pass overhead loses under ~64). */
static void lzmesh_u35_radix_idx(const uint32_t *key, uint16_t *list,
    uint16_t *tmp, unsigned n)
{
    uint16_t *a = list;
    uint16_t *b = tmp;
    unsigned cnt[256];
    unsigned i, p, passes;
    uint32_t orv = 0u;
    unsigned shift;
    for (i = 0u; i < n; i++)
        orv |= key[list[i]];
    if (orv == 0u)
        return; /* all keys equal: initial order already sorted */
    passes = 1u;
    if (orv > 0xFFu)
        passes = 2u;
    if (orv > 0xFFFFu)
        passes = 3u;
    if (orv > 0xFFFFFFu)
        passes = 4u;
    shift = 0u;
    for (p = 0u; p < passes; p++) {
        unsigned pos[256];
        unsigned acc = 0u;
        for (i = 0u; i < 256u; i++)
            cnt[i] = 0u;
        for (i = 0u; i < n; i++)
            cnt[(key[a[i]] >> shift) & 0xFFu]++;
        for (i = 0u; i < 256u; i++) {
            pos[i] = acc;
            acc += cnt[i];
        }
        for (i = 0u; i < n; i++) {
            unsigned k = (key[a[i]] >> shift) & 0xFFu;
            b[pos[k]++] = a[i];
        }
        {
            uint16_t *t = a;
            a = b;
            b = t;
        }
        shift += 8u;
    }
    if (a != list) {
        for (i = 0u; i < n; i++)
            list[i] = a[i];
    }
}

/* P13-RANK: bottom-up merge sort by (freq desc, sym desc), same
 * pattern as lzmesh_pack1_pm_msort. Output is identical to the
 * insertion sort it replaces: (freq, sym) with distinct syms is a
 * strict total order, so the sorted permutation is unique and every
 * correct sort produces it. tmp must hold n entries. */
static void lzmesh_u35_rank_msort(const uint32_t *freq, uint16_t *list,
    uint16_t *tmp, unsigned n)
{
    uint16_t *a = list;
    uint16_t *b = tmp;
    unsigned w;
    unsigned i;
    if (n < 2u)
        return;
    for (w = 1u; w < n; w <<= 1) {
        for (i = 0u; i < n; i += w << 1) {
            unsigned lo = i;
            unsigned mid = (i + w < n) ? i + w : n;
            unsigned hi = (i + (w << 1) < n) ? i + (w << 1) : n;
            unsigned x = lo;
            unsigned y = mid;
            unsigned o = lo;
            while (x < mid && y < hi) {
                uint16_t lx = a[x];
                uint16_t ry = a[y];
                if (freq[ry] > freq[lx]
                    || (freq[ry] == freq[lx] && ry > lx))
                    b[o++] = a[y++];
                else
                    b[o++] = a[x++];
            }
            while (x < mid)
                b[o++] = a[x++];
            while (y < hi)
                b[o++] = a[y++];
        }
        {
            uint16_t *t = a;
            a = b;
            b = t;
        }
    }
    if (a != list) {
        for (i = 0u; i < n; i++)
            list[i] = a[i];
    }
}

static void lzmesh_u35_rank_assign(const uint32_t *freq, uint8_t *lens) {
    unsigned cnt[11];
    unsigned i, rank = 0u;
    uint16_t order[256];
    unsigned n = 0u, L;
    if (freq == NULL || lens == NULL)
        return;
    for (i = 0u; i < 11u; i++)
        cnt[i] = 0u;
    for (i = 0u; i < 256u; i++) {
        if (freq[i] != 0u) {
            if (lens[i] == 0u || lens[i] > 10u)
                return; /* unsolved: leave untouched */
            order[n++] = (uint16_t)i;
            cnt[lens[i]]++;
        } else if (lens[i] != 0u) {
            return; /* not a solved table: leave untouched */
        }
    }
    if (n < 2u)
        return;
    { /* P13-RANK: msort == insertion output (total order, unique). */
        uint16_t rtmp[256];
        /* R13-L0RES-D1: radix above gate (identical total order:
         * ~freq ASC == freq DESC, sym-DESC init keeps DESC ties). */
        if (n >= 64u) {
            uint32_t nfreq[256];
            unsigned r;
            for (r = 0u; r < 256u; r++)
                nfreq[r] = ~freq[r];
            for (r = 0u; r < n / 2u; r++) {
                uint16_t t = order[r];
                order[r] = order[n - 1u - r];
                order[n - 1u - r] = t;
            }
            lzmesh_u35_radix_idx(nfreq, order, rtmp, n);
        } else {
            lzmesh_u35_rank_msort(freq, order, rtmp, n);
        }
    }
    for (L = 1u; L <= 10u; L++) {
        unsigned c;
        for (c = 0u; c < cnt[L]; c++) {
            if (rank >= n)
                return;
            lens[order[rank++]] = (uint8_t)L;
        }
    }
}

static int lzmesh_u35_layout_inner(size_t n, const unsigned *bitc,
                      uint32_t *lenB, uint32_t *lenC, uint32_t *modes,
                      uint32_t *bo, uint32_t *fo, unsigned *laneb,
                      uint8_t *idxbuf, unsigned idxcap, uint8_t *lenb5,
                      unsigned *idxsz, int l0, unsigned ncap, int later);
/* QM-D1 single-build meta (HINT-QM-R2 D1+D10 verbatim; black-box
 * 766/766 oracle-exact). inputs: raw mfreq[11], used (gather count).
 * no reshaping, no cost compare, no gates. order syms ascending raw
 * count (stable); q0=(used>>effmax)+1, effmax=w23(nz,5); floor
 * nonzero counts at q; 2-queue Huffman list-first ties; FIFO
 * tail-merge; package depth>=effmax => q*=2 and rebuild; maxlen 5.
 * returns 1 with mlens[11], else 0 (fail-closed). */
static int lzmesh_qm_meta_build(const uint32_t *mfreq, unsigned used,
                                uint8_t *mlens) {
    unsigned order[11];
    unsigned n = 0u;
    unsigned i;
    unsigned effmax;
    uint32_t q;
    unsigned iter;
    uint32_t pcnt[16];
    uint16_t tch[16][2];
    if (mfreq == NULL || mlens == NULL)
        return 0;
    for (i = 0u; i < 11u; i++)
        mlens[i] = 0u;
    for (i = 0u; i < 11u; i++) {
        if (mfreq[i] != 0u)
            order[n++] = i;
    }
    if (n < 2u)
        return 0;
    { /* stable ascending by raw count (insertion; order starts ascending). */
        unsigned a;
        for (a = 1u; a < n; a++) {
            unsigned x = order[a];
            unsigned b = a;
            while (b > 0u && mfreq[order[b - 1u]] > mfreq[x]) {
                order[b] = order[b - 1u];
                b--;
            }
            order[b] = x;
        }
    }
    effmax = lzmesh_pack1_w23(n, 5u);
    if (effmax == 0u || effmax > 5u)
        return 0;
    q = (used >> effmax) + 1u;
    for (iter = 0u; iter < 32u; iter++) {
        uint32_t w[11];
        unsigned pq[24];
        unsigned qh = 0u, qt = 0u;
        unsigned nxt = 0u; /* package count; id = 0x100+idx */
        unsigned p = 0u;
        unsigned root;
        unsigned node;
        unsigned overflow = 0u;
        uint8_t depth[0x110];
        for (i = 0u; i < n; i++)
            w[i] = mfreq[order[i]] < q ? q : mfreq[order[i]];
        for (;;) { /* 2-queue Huffman, list-first ties. */
            unsigned a, b;
            uint32_t ca, cb;
            uint32_t lc, pk;
            unsigned li;
            if (p < n) {
                li = order[p];
                lc = w[p];
            } else {
                li = 0x1FFu;
                lc = 0xFFFFFFFFu;
            }
            pk = (qt > qh) ? pcnt[pq[qh] - 0x100u] : 0xFFFFFFFFu;
            if (lc <= pk) {
                a = li;
                ca = lc;
                p++;
            } else {
                a = pq[qh++];
                ca = pk;
            }
            if (p < n) {
                li = order[p];
                lc = w[p];
            } else {
                li = 0x1FFu;
                lc = 0xFFFFFFFFu;
            }
            pk = (qt > qh) ? pcnt[pq[qh] - 0x100u] : 0xFFFFFFFFu;
            if (lc <= pk) {
                b = li;
                cb = lc;
                p++;
            } else {
                b = pq[qh++];
                cb = pk;
            }
            if (a == 0x1FFu || b == 0x1FFu)
                return 0; /* degenerate: cannot pick 2. */
            if (a >= 0x100u && a - 0x100u >= nxt)
                return 0;
            if (b >= 0x100u && b - 0x100u >= nxt)
                return 0;
            if (nxt >= 16u || qt >= 24u)
                return 0;
            pcnt[nxt] = ca + cb;
            tch[nxt][0] = (uint16_t)a;
            tch[nxt][1] = (uint16_t)b;
            pq[qt++] = 0x100u + nxt;
            nxt++;
            if (p >= n)
                break;
        }
        while (qt - qh > 1u) { /* FIFO tail-merge to one root. */
            unsigned a = pq[qh++];
            unsigned b = pq[qh++];
            if (a < 0x100u || b < 0x100u)
                return 0;
            if (a - 0x100u >= nxt || b - 0x100u >= nxt)
                return 0;
            if (nxt >= 16u || qt >= 24u)
                return 0;
            pcnt[nxt] = pcnt[a - 0x100u] + pcnt[b - 0x100u];
            tch[nxt][0] = (uint16_t)a;
            tch[nxt][1] = (uint16_t)b;
            pq[qt++] = 0x100u + nxt;
            nxt++;
        }
        root = pq[qh];
        if (root < 0x100u || root - 0x100u >= nxt)
            return 0;
        for (i = 0u; i < 0x110u; i++)
            depth[i] = 0xFFu;
        depth[root] = 0u;
        for (node = nxt; node > 0u; node--) { /* parents before children. */
            unsigned id = 0x100u + node - 1u;
            unsigned dn = depth[id];
            unsigned c0, c1;
            if (dn == 0xFFu)
                return 0; /* stale: unreachable package. */
            if (dn >= effmax) {
                overflow = 1u;
                break;
            }
            c0 = tch[node - 1u][0];
            c1 = tch[node - 1u][1];
            if (c0 >= 0x110u || c1 >= 0x110u)
                return 0;
            depth[c0] = (uint8_t)(dn + 1u);
            depth[c1] = (uint8_t)(dn + 1u);
        }
        if (overflow != 0u) {
            if (q > (1u << 24))
                return 0;
            q <<= 1;
            continue;
        }
        for (i = 0u; i < n; i++) {
            unsigned s = order[i];
            if (depth[s] == 0xFFu || depth[s] == 0u || depth[s] > 5u)
                return 0;
            mlens[s] = depth[s];
        }
        return 1;
    }
    return 0;
}
/* D3: single-build lit-HUF pipeline (lengths/codes/meta/bitc/tot).
 * Symbol lengths come from the H5DEEP q-floor loop (no q rungs).
 * Layout runs l0=1/later=0/ncap for the tot probe only. Returns 1
 * with all outs incl *tot_b else 0. */
/* P12-W8: fwd decl (defined in S3 section below; two-queue Huffman,
 * leaf-first ties, depths 1..10 or fail). */
static int lzmesh_s3_huff(const uint32_t *freq, unsigned nsym,
    uint8_t *lens);
static int lzmesh_u35_tryq(const uint8_t *s, size_t n, const uint32_t *freq,
                           uint8_t *lens, uint16_t *codes,
                           uint8_t *mlens, uint16_t *mcodes, uint8_t *vals,
                           unsigned *used, unsigned *bitc, uint64_t *ctot,
                           uint64_t *data, unsigned *tot_b, unsigned ncap) {
    uint32_t mfreq[11];
    uint32_t bm = 0u;
    uint32_t lenB, lenC, modes, bo, fo;
    unsigned laneb[8], idxsz;
    uint8_t idxbuf[24], lenb5[5];
    unsigned u, i, k, nz;
    uint64_t tot = 0u, dbits = 0u;
    { /* D3 H5DEEP q-floor (HINT-H5DEEP-R1 stage 2; black-box 53/53
       * skeletons incl fd7 no-5s): q=(n>>w)+1, lift nonzero counts
       * below q, PM@10; any depth>w doubles q and rebuilds. w is
       * the nsym bound; rank below still uses ORIGINAL freq. */
        uint32_t qfreq[256];
        unsigned unsym = 0u, wbound, qq, mx, i2;
        for (i2 = 0u; i2 < 256u; i2++)
            if (freq[i2] != 0u)
                unsym++;
        wbound = lzmesh_pack1_w23(unsym, 10u);
        if (wbound == 0u || wbound > 10u || n > 0xFFFFFFFFu)
            return 0;
        qq = (unsigned)(((uint32_t)n >> wbound) + 1u);
        for (;;) {
            for (i2 = 0u; i2 < 256u; i2++)
                qfreq[i2] =
                    (freq[i2] != 0u && freq[i2] < qq) ? qq : freq[i2];
            /* P12-W8 skip-solve: unconstrained Huffman first; its
             * length MULTISET equals PM@10's whenever it fits depth
             * <=10 (w8_diff: 660k+ harvested+fuzz, 0 MM, 0 DANGER),
             * and rank_assign below reassigns every length by
             * original-freq rank, so per-symbol placement is dead.
             * Binding/degenerate/overflow cases fall to PM (exact). */
            if (lzmesh_s3_huff(qfreq, 256u, lens) == 0
                && lzmesh_pack1_solve(qfreq, 256u, 10u, lens) == 0u)
                return 0;
            mx = 0u;
            for (i2 = 0u; i2 < 256u; i2++)
                if (lens[i2] > mx)
                    mx = lens[i2];
            if (mx <= wbound)
                break;
            if (qq > (1u << 24))
                return 0; /* unreachable: uniform always fits */
            qq <<= 1;
        }
    }
    lzmesh_u35_rank_assign(freq, lens); /* C2b+H5DEEP-5: orig-freq order */
    if (!lzmesh_pack1_canon(lens, 256u, codes, 10u))
        return 0;
    if (!lzmesh_pack1_bitmap(lens, &bm))
        return 0;
    u = lzmesh_pack1_gather(lens, bm, vals);
    if (u == 0u || u > 256u || (u & 7u) != 0u)
        return 0;
    for (i = 0u; i < 11u; i++)
        mfreq[i] = 0u;
    for (i = 0u; i < u; i++) {
        if (vals[i] > 10u)
            return 0;
        mfreq[vals[i]]++;
    }
    nz = 0u;
    for (i = 0u; i < 11u; i++)
        if (mfreq[i] != 0u)
            nz++;
    if (nz < 2u) {
        /* R6-A5 (E-B6 degenerate meta): oracle pads singleton mfreq
         * with a phantom 0 (cyc08-PROVEN: mfreq{6:64} -> mlens{0:1,
         * 6:1}, vals cost 1 bit each, NOT 0). Fully-packed groups
         * only (off-corpus); nz==1 sole is always nonzero here. */
        if (nz != 1u || mfreq[0] != 0u)
            return 0;
        mfreq[0] = 1u;
        nz = 2u;
    }
    { /* QM-D1 single build on raw mfreq (replaces D3 large-alpha +
       * FAM-N/J2-RSD + qm ladder/5-clause + E6 liftgap stack, all
       * deleted per HINT-QM-R2 D1+D10; 766/766 oracle-exact). */
        if (lzmesh_qm_meta_build(mfreq, u, mlens) == 0)
            return 0;
    }
    if (!lzmesh_pack1_canon(mlens, 11u, mcodes, 5u))
        return 0;
    for (k = 0u; k < 8u; k++)
        bitc[k] = 0u;
    bitc[0] = 33u + 32u; /* meta + bitmap, no-align (h1-PROVEN) */
    for (i = 0u; i < u; i++)
        bitc[i & 7u] += mlens[vals[i]];
    { /* R2-STORE V2c: rollback-early. tot is exactly 65 + meta +
       * data regardless of accumulation order (all sums commute):
       * data bits from freq (O(256)) + meta bits re-summed here
       * equal the O(n) loop's dbits + lane sums bit-for-bit, so a
       * tot>=8n verdict now returns BEFORE the O(n) bitc loop.
       * Decline blocks skip the loop; keep blocks pay O(256). */
        uint64_t mbits = 0u, fbits = 0u;
        unsigned f;
        for (i = 0u; i < u; i++)
            mbits += mlens[vals[i]];
        for (f = 0u; f < 256u; f++)
            fbits += (uint64_t)freq[f] * (uint64_t)lens[f];
        if (65u + mbits + fbits >= (uint64_t)8u * (uint64_t)n)
            return 0;
        /* R13-L0RES-D2b: V2c's fbits == V2d's dbits bit-for-bit (same
         * freq-weighted sum, R12-proven comment above); keep-path
         * reuses it, V2d drops its accumulation (lanes only). */
        dbits = fbits;
    }
    { /* R2-STORE V2d: x8 lane unroll + single lens lookup per byte
       * (the two lens[s[i]] reads are one value; dbits sum commutes). */
        unsigned n8 = (unsigned)n, m8 = n8 & ~7u;
        uint8_t L0, L1, L2, L3, L4, L5, L6, L7;
        for (i = 0u; i < m8; i += 8u) {
            L0 = lens[s[i]];
            L1 = lens[s[i + 1u]];
            L2 = lens[s[i + 2u]];
            L3 = lens[s[i + 3u]];
            L4 = lens[s[i + 4u]];
            L5 = lens[s[i + 5u]];
            L6 = lens[s[i + 6u]];
            L7 = lens[s[i + 7u]];
            bitc[0] += L0;
            bitc[1] += L1;
            bitc[2] += L2;
            bitc[3] += L3;
            bitc[4] += L4;
            bitc[5] += L5;
            bitc[6] += L6;
            bitc[7] += L7;
        }
        for (i = m8; i < n8; i++) {
            L0 = lens[s[i]];
            bitc[i & 7u] += L0;
        }
    }
    for (k = 0u; k < 8u; k++) {
        if (bitc[k] == 0u)
            return 0; /* empty lane: shape unowned */
        tot += bitc[k];
    }
    if (!lzmesh_u35_layout_inner(n, bitc, &lenB, &lenC, &modes,
                                 &bo, &fo, laneb, idxbuf,
                                 (unsigned)sizeof idxbuf, lenb5, &idxsz,
                                 1, ncap, 0))
        return 0;
    *used = u;
    *ctot = tot;
    *data = dbits;
    *tot_b = (unsigned)(fo + 11u);
    return 1;
}

/* Lengths + codes + vals + per-lane bit counts. Caller bufs: lens[256],
 * codes[256], mlens[11], mcodes[11], vals[256]. ncap = caller cap
 * (u35: NCAP; u36: U36_NCAP). Returns 1 with *used, bitc[8], *ctot
 * on full success, else 0.
 * D3: exact frequencies (no skew hack); single H5DEEP q-floor build
 * in tryq (no L/L-1 rungs). Replaces C4 fitted skew + q=L. */
static int lzmesh_u35_lengths(const uint8_t *s, size_t n, uint8_t *lens,
                              uint16_t *codes, uint8_t *mlens,
                              uint16_t *mcodes, uint8_t *vals,
                              unsigned *used, unsigned *bitc,
                              uint64_t *ctot, unsigned ncap) {
    uint32_t freq[256];
    uint8_t tlens[256], tmlens[11], tvals[256];
    uint16_t tcodes[256], tmcodes[11];
    unsigned tbitc[8], tused, ttot;
    uint64_t tctot, tdata;
    /* R12-L0-H1 (C-FREQ): extra histogram lanes (see below). */
    uint32_t f1[256], f2[256], f3[256];
    unsigned u10 = 0u, i, k;
    if (s == NULL || lens == NULL || codes == NULL || mlens == NULL
        || mcodes == NULL || vals == NULL || used == NULL || bitc == NULL
        || ctot == NULL || n == 0u || ncap == 0u || n > ncap)
        return 0;
    /* R12-L0-H1 (C-FREQ): 4-lane unrolled histogram. The scalar
     * freq[s[i]]++ is a load-add-store dependency chain (~31%
     * mixed / ~20% text L0-enc time, sample + PMU). 4 independent
     * tables x 4-way unroll break the chain; histogram sums
     * commute so the combine is exact. Each lane <= n <= ncap
     * (no u32 overflow); tail handles n%4 (n<4 possible: layout
     * rejects later, freq must still be exact here). N-GATE: the
     * 3 extra tables + combine cost ~1800 fixed ops, a net loss
     * under ~700B (L1 per-block litbufs; PMU +1.8% cyc mixed-L1
     * ungated) => scalar-verbatim under 1024B, 4-lane above.
     * Matrix L0 blocks are all >= 16385B (prize intact). */
    if ((unsigned)n < 1024u) {
        for (i = 0u; i < 256u; i++)
            freq[i] = 0u;
        for (i = 0u; i < (unsigned)n; i++)
            freq[s[i]]++;
    } else {
        for (i = 0u; i < 256u; i++) {
            freq[i] = 0u;
            f1[i] = 0u;
            f2[i] = 0u;
            f3[i] = 0u;
        }
        {
            unsigned n4 = (unsigned)n, m4 = n4 & ~3u;
            for (i = 0u; i < m4; i += 4u) {
                freq[s[i]]++;
                f1[s[i + 1u]]++;
                f2[s[i + 2u]]++;
                f3[s[i + 3u]]++;
            }
            for (; i < n4; i++)
                freq[s[i]]++;
            for (i = 0u; i < 256u; i++)
                freq[i] += f1[i] + f2[i] + f3[i];
        }
    }
    for (i = 0u; i < 256u; i++) {
        if (freq[i] != 0u)
            u10++;
    }
    if (u10 < 2u)
        return 0;
    if (!lzmesh_u35_tryq(s, n, freq, tlens, tcodes, tmlens, tmcodes,
                         tvals, &tused, tbitc, &tctot, &tdata, &ttot,
                         ncap))
        return 0;
    for (i = 0u; i < 256u; i++) {
        lens[i] = tlens[i];
        codes[i] = tcodes[i];
        vals[i] = tvals[i];
    }
    for (i = 0u; i < 11u; i++) {
        mlens[i] = tmlens[i];
        mcodes[i] = tmcodes[i];
    }
    for (k = 0u; k < 8u; k++)
        bitc[k] = tbitc[k];
    *used = tused;
    *ctot = tctot;
    return 1;
}

/* SHAPE-H1 layout from bit counts: lane bytes, pads (must be 0-class),
 * index pack into idxbuf (cap>=24), len bytes into lenb5, modes/bo/fo.
 * *lenC = expanded len bytes (footer), *lenB = stored bytes. l0 = L0
 * caller (u36): B-TAB-listed (lastL,k) lanes are ACCEPTED with P=0
 * (R4-proven: 1576/1576 L0 lanes P=0 incl 97 listed; row-(a)
 * B-TAB emission does not fire at L0 — spec carve-out R4-A3).
 * Non-L0 callers keep the U35-BTAB decline (row (a) presumed live
 * at L1+; untested — declining is safe). */
static int lzmesh_u35_layout_inner(size_t n, const unsigned *bitc,
                      uint32_t *lenB, uint32_t *lenC, uint32_t *modes,
                      uint32_t *bo, uint32_t *fo, unsigned *laneb,
                      uint8_t *idxbuf, unsigned idxcap, uint8_t *lenb5,
                      unsigned *idxsz, int l0, unsigned ncap, int later) {
    uint64_t tot = 0u;
    (void)l0; /* R21-BTAB: legacy decline removed; l0 kept for signature. */
    unsigned k, payload = 0u, isz;
    uint8_t eb[5];
    unsigned li, i;
    uint32_t mlen;
    int alleq = 1;
    if (bitc == NULL || lenB == NULL || lenC == NULL || modes == NULL
        || bo == NULL || fo == NULL || laneb == NULL || idxbuf == NULL
        || lenb5 == NULL || idxsz == NULL)
        return 0;
    if (ncap == 0u)
        return 0;
    if (n < 4u || n > ncap || n > 0xffffu)
        return 0;
    /* len: single lit-rest symbol, bytes-tested (h1 5B RAW-PROVEN).
     * later (u17-analog, R5-proven 1-byte diff): no pre-emit, rest
     * n-3 instead of n-4. */
    li = lzmesh_u7_len_escape_write((uint32_t)(n - (later ? 3u : 4u)),
                                    eb);
    if (li == 0u || li > 5u)
        return 0;
    for (i = 0u; i < li; i++)
        lenb5[i] = eb[i];
    for (i = 1u; i < li; i++)
        if (lenb5[i] != lenb5[0])
            alleq = 0;
    mlen = lzmesh_u4_mode_trivial(li, alleq);
    if (mlen != LZMESH_U4_MODE_RAW && mlen != LZMESH_U4_MODE_REPEAT)
        return 0;
    if (lzmesh_u4_mode_trivial(1u, 1) != LZMESH_U4_MODE_REPEAT)
        return 0; /* tok singleton c0 REPEAT */
    for (k = 0u; k < 8u; k++) {
        unsigned c = bitc[k], L, m, kk;
        if (c == 0u)
            return 0;
        tot += c;
        L = (c + 7u) / 8u;
        m = c & 7u;
        kk = (8u - m) & 7u;
        if (!lzmesh_u4_k_in_range(kk))
            return 0;
        if (lzmesh_pack1_pad0(0u, k) != 0u)
            return 0; /* distc=0 PAD1=0 */
        /* R21-BTAB: legacy (L,kk) decline REMOVED (spurious vs
         * (lastL,kk); exact decline lives in u35_want/emit_inner). */
        laneb[k] = L;
        payload += L;
    }
    if (tot >= (uint64_t)8u * (uint64_t)n)
        return 0; /* S5.5 rollback: HUF-kept iff bits<8n */
    isz = lzmesh_pack1_index_pack(laneb, idxbuf, idxcap);
    if (isz == 0u)
        return 0;
    *modes = lzmesh_u4_modes_pack(LZMESH_U4_MODE_REPEAT, mlen,
                                  LZMESH_U4_MODE_HUFFMAN,
                                  LZMESH_U4_MODE_RAW);
    *lenB = (mlen == LZMESH_U4_MODE_REPEAT) ? 1u : li;
    *lenC = li;
    *bo = 9u + 1u + *lenB; /* hdr + tok + len (lit-HUF: no lit bytes) */
    *fo = *bo + payload + isz;
    *idxsz = isz;
    if (*fo > 0xffffu)
        return 0; /* u16 header fields */
    return 1;
}

int lzmesh_u35_layout(size_t n, const unsigned *bitc, uint32_t *lenB,
                      uint32_t *lenC, uint32_t *modes, uint32_t *bo,
                      uint32_t *fo, unsigned *laneb, uint8_t *idxbuf,
                      unsigned idxcap, uint8_t *lenb5, unsigned *idxsz,
                      int l0) {
    return lzmesh_u35_layout_inner(n, bitc, lenB, lenC, modes, bo, fo,
                                   laneb, idxbuf, idxcap, lenb5, idxsz,
                                   l0, LZMESH_U36_NCAP, 0);
}

/* R21-BTAB exact decline: legacy (L,kk)-listed AND any lane would
 * fire by (lastL,kk). Spurious (L-listed, lastL-clean) cells fire
 * with P=0 (R20 proved force-emit agrees). Listed+firing cells
 * (s58-class) unaffected: they were never legacy-declined. */
static int lzmesh_u35_bprime_decline(const uint8_t *s, size_t n,
                                     const uint8_t *lens,
                                     const unsigned *bitc,
                                     const unsigned *laneb) {
    unsigned k, anyfire = 0u, legacy = 0u;
    if (s == NULL || lens == NULL || bitc == NULL || laneb == NULL)
        return 1;
    for (k = 0u; k < 8u; k++) {
        unsigned c = bitc[k], m = c & 7u, kk = (8u - m) & 7u;
        if (lzmesh_pack1_btab(laneb[k], kk) != 0u)
            legacy = 1u;
        if (kk != 0u && (unsigned)n > k) {
            unsigned li =
                (unsigned)(((n - 1u - k) >> 3) * 8u + k);
            unsigned lastL = lens[s[li]];
            if (lzmesh_pack1_btab(lastL, kk) != 0u)
                anyfire = 1u;
        }
    }
    return (legacy && anyfire) ? 1 : 0;
}

int lzmesh_u35_want(const uint8_t *src, size_t size, int level) {
    uint8_t lens[256], mlens[11], vals[256];
    uint16_t codes[256], mcodes[11];
    uint8_t idxbuf[24], lenb5[5];
    unsigned bitc[8], laneb[8], used, idxsz;
    uint64_t ctot;
    uint32_t lenB, lenC, modes, bo, fo;
    if (src == NULL || size < 4u || size > LZMESH_U35_NCAP
        || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 1)
        return 0; /* L1-only: fresh floor + rep0-only + VIS (S5.7c) */
    if (lzmesh_u9_is_run(src, size))
        return 0;
    if (lzmesh_u12_period(src, size) != 0)
        return 0; /* single-periodic owned by u12/u15/u18 */
    if (lzmesh_u18_period(src, size) != 0)
        return 0;
    if (!lzmesh_u35_cert(src, size))
        return 0;
    if (!lzmesh_u35_lengths(src, size, lens, codes, mlens, mcodes, vals,
                            &used, bitc, &ctot, LZMESH_U35_NCAP))
        return 0;
    (void)used;
    (void)ctot;
    if (!lzmesh_u35_layout(size, bitc, &lenB, &lenC, &modes, &bo, &fo,
                           laneb, idxbuf, (unsigned)sizeof idxbuf, lenb5,
                           &idxsz, 1))
        return 0;
    (void)lenB;
    (void)lenC;
    (void)modes;
    (void)idxbuf;
    (void)lenb5;
    (void)idxsz;
    if (lzmesh_u35_bprime_decline(src, size, lens, bitc, laneb))
        return 0;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, (uint32_t)size,
                               1);
}

/* === L0 litonly-HUF single-block, level 0 only (owner: u36) === */
/* R3-A2 close (R4): L0 has no finding (S5.7c), so the litonly parse
 * (tc1/dc0/litc==ds, tok 0xc0 per S3.13) is FORCED for every
 * non-run input — no cert needed (unlike u35's L1 no-take cert).
 * lit-RAW single-block is TIER-1-impossible (fo=11+n>n) and
 * lit-REPEAT needs a run, so L0-nonrun-COMP ⟺ lit-HUF. Layout +
 * lane pack are byte-identical to u35 (shared lengths/layout/put);
 * only the gate differs (level 0, no cert, no period exclusion —
 * u12/u18/u35 all exclude L0; oracle L0 periodics take this same
 * shape, R4-probed). Single-block cap U36_NCAP=16393 (oracle
 * max-single bisect; n>16393 multi-block open). Gates via
 * comp_keep (TIER-1 D>fo; TIER-2 outpos<=n; e00-min sits ON 43<=43).
 * PERF: O(n) lengths + O(n) emit, two-pass direct-to-dst;
 * stack ~1.5KB. */
int lzmesh_u36_want(const uint8_t *src, size_t size, int level) {
    uint8_t lens[256], mlens[11], vals[256];
    uint16_t codes[256], mcodes[11];
    uint8_t idxbuf[24], lenb5[5];
    unsigned bitc[8], laneb[8], used, idxsz;
    uint64_t ctot;
    uint32_t lenB, lenC, modes, bo, fo;
    if (src == NULL || size < 4u || size > LZMESH_U36_NCAP
        || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (level != 0)
        return 0; /* L0-only: no finding, forced parse (S5.7c) */
    if (lzmesh_u9_is_run(src, size))
        return 0; /* runs owned by u10/u16 */
    if (!lzmesh_u35_lengths(src, size, lens, codes, mlens, mcodes, vals,
                            &used, bitc, &ctot, LZMESH_U36_NCAP))
        return 0;
    (void)used;
    (void)ctot;
    if (!lzmesh_u35_layout(size, bitc, &lenB, &lenC, &modes, &bo, &fo,
                           laneb, idxbuf, (unsigned)sizeof idxbuf, lenb5,
                           &idxsz, 1))
        return 0;
    (void)lenB;
    (void)lenC;
    (void)modes;
    (void)laneb;
    (void)idxbuf;
    (void)lenb5;
    (void)idxsz;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, (uint32_t)size,
                               1);
}

/* LSB-first bit put into lane base; *pos advances.
 * P2-bitio shape (same bits ORed, same *pos advance); P15-PACK P5
 * implements it as one masked 64-bit RMW (was per-byte loop). */
/* R9-TEXT1-E2: fwd decl (slow below calls the wrapper further down). */
static void lzmesh_u35_put(uint8_t *base, unsigned *pos, unsigned val,
                           unsigned n);
/* R9-TEXT1-E2: cold slow half (n>32 two-put). Outlined so fast is
 * recursion-free and force-inlinable. */
__attribute__((noinline)) static void
lzmesh_u35_put_slow(uint8_t *base, unsigned *pos, unsigned val,
                    unsigned n) {
    lzmesh_u35_put(base, pos, val, 32u);
    lzmesh_u35_put(base, pos, 0u, n - 32u);
}

/* R9-TEXT1-E2: fast half, force-inline at the sole h3-suffix site
 * (34.5k calls/rep text-L1; sb<=31 there per P5). Body verbatim
 * incl the n>32 slow-fallback arm (dead but exact on all inputs). */
static inline __attribute__((always_inline)) void
lzmesh_u35_put_fast(uint8_t *base, unsigned *pos, unsigned val,
                    unsigned n) {
    /* P15-PACK P5: word RMW (was per-byte loop). Sole caller: h3 suffix
     * (sb<=31). n==0 no-op, n>32 fail-safe two-put (acc_put precedent)
     * preserve the generic contract. */
    unsigned p = *pos;
    unsigned b = p >> 3;
    unsigned sh = p & 7u;
    uint32_t mask;
    uint64_t w, v;
    if (n == 0u)
        return;
    if (n > 32u) {
        lzmesh_u35_put_slow(base, pos, val, n);
        return;
    }
    mask = (n >= 32u) ? 0xFFFFFFFFu : (uint32_t)((1u << n) - 1u);
    v = ((uint64_t)(val & mask)) << sh;
    *pos = p + n;
    memcpy(&w, base + b, sizeof w);
    w |= v;
    memcpy(base + b, &w, sizeof w);
}

/* R9-TEXT1-E2: outline u35_put kept (thin wrapper; contract path). */
static void lzmesh_u35_put(uint8_t *base, unsigned *pos, unsigned val,
                           unsigned n) {
    lzmesh_u35_put_fast(base, pos, val, n);
}

/* P6-W4 word accumulator: per-lane 64b bit buffer, 4B word flush.
 * Bit-identical to u35_put: bits pack LSB-first, bytes leave the lane in
 * order with each byte written exactly once; tail flush writes ceil(bits/8)
 * bytes; pads beyond stay 0 (callers pre-zero lanes). *pos advances by n
 * per put, so the count/emit skew guards keep their exact verdicts.
 * Measured n: 0..14 + 32 (probe: e00 262K puts/enc n<=7+32, suffix<=14);
 * nbits<=31 before put keeps acc < 2^64 (31+32=63). n>32 unreachable
 * (fail-safe two-put recursion preserves u35_put's zero-fill). */
typedef struct {
    uint64_t acc;
    unsigned nbits;
    uint8_t *out;
} lzmesh_u35_acc;

/* R9-TEXT1-E1: fwd decl (slow below calls the wrapper further down). */
static void lzmesh_u35_acc_put(lzmesh_u35_acc *a, unsigned *pos,
                               unsigned val, unsigned n);
/* R9-TEXT1-E1: cold slow half (n>32 two-put; measured unreachable,
 * kept for the generic contract). Outlined so the fast half below is
 * recursion-free and force-inlinable. */
__attribute__((noinline)) static void
lzmesh_u35_acc_put_slow(lzmesh_u35_acc *a, unsigned *pos,
                        unsigned val, unsigned n) {
    lzmesh_u35_acc_put(a, pos, val, 32u);
    lzmesh_u35_acc_put(a, pos, 0u, n - 32u);
}

/* R9-TEXT1-E1: fast half, force-inline at the 4 p16_huf_emit sites
 * (~80k calls/rep text-L1; n is 3/32/<=14/<=32 there). Body verbatim
 * incl the n>32 slow-fallback arm (dead but exact on all inputs).
 * Other 20+ sites keep calling the outline acc_put (cold). */
static inline __attribute__((always_inline)) void
lzmesh_u35_acc_put_fast(lzmesh_u35_acc *a, unsigned *pos,
                        unsigned val, unsigned n) {
    uint64_t v;
    if (n == 0u)
        return; /* sb=0 no-op (matches u35_put) */
    if (n > 32u) {
        lzmesh_u35_acc_put_slow(a, pos, val, n);
        return;
    }
    /* P16-PACK T2a KILLED (unit contract): mask removal broke u35acc
     * random-diff (generic val/n contract; real-codec proof insufficient).
     * Mask restored; T2b (lastL-direct) stands alone. */
    if (n == 32u)
        v = (uint64_t)val;
    else
        v = (uint64_t)(val & ((1u << n) - 1u));
    if (a->nbits >= 32u) {
        uint32_t w = (uint32_t)a->acc;
        memcpy(a->out, &w, 4u);
        a->out += 4;
        a->acc >>= 32;
        a->nbits -= 32u;
    }
    a->acc |= v << a->nbits;
    a->nbits += n;
    *pos += n;
}

/* R11-L1CONT2-H3: Huffman-put specialization (n in 1..10 proven at the
 * sole call site: table built from the emitted stream by g1_build,
 * whose q-floor loop caps solve lens at wbound<=10 and rank_assign
 * reassigns every used sym into 1..10; every stream byte is used).
 * Drops the dead n>32 slow arm and n==32 full-word arm; keeps the
 * n==0 no-op (its early return skips the flush, unlike a masked
 * no-op) and the mask (T2a unit contract). Body otherwise verbatim.
 * Contract: n <= 32. */
static inline __attribute__((always_inline)) void
lzmesh_u35_acc_put_huff(lzmesh_u35_acc *a, unsigned *pos,
                        unsigned val, unsigned n) {
    uint64_t v;
    if (n == 0u)
        return;
    v = (uint64_t)(val & ((1u << n) - 1u));
    if (a->nbits >= 32u) {
        uint32_t w = (uint32_t)a->acc;
        memcpy(a->out, &w, 4u);
        a->out += 4;
        a->acc >>= 32;
        a->nbits -= 32u;
    }
    a->acc |= v << a->nbits;
    a->nbits += n;
    *pos += n;
}

/* R12-L0-H2 (C-PUT): L0/litonly data-put clone. Contract: e =
 * code | len<<16 with len in 1..10 (u35_lengths success: s3/PM
 * depths 1..10 for all freq>0 syms, rank_assign covers every used
 * sym or leaves s3/PM depths in place, mx<=wbound<=10 gate; every
 * input byte has freq>0) and code < 2^len (pack1_canon gate
 * code<(1u<<len) + Kraft gate; rev preserves bit width; codes[]
 * zero for unused syms, which never appear in the stream). Drops
 * the dead n==32/n>32 arms + the mask vs acc_put_fast (a clone,
 * NOT acc_put: the P16 unit contract pins acc_put's generic
 * mask). Keeps the n==0 no-op (degenerate-table safety, never
 * taken on solved tables) + the drain (the write itself) with
 * acc/nbits/pos math verbatim. Same puts, same order => identical
 * bytes. nbits<=31 before put + len<=10 keeps acc < 2^64. */
static inline __attribute__((always_inline)) void
lzmesh_u35_acc_put_l0(lzmesh_u35_acc *a, unsigned *pos, uint32_t e) {
    unsigned n = e >> 16;
    if (n == 0u)
        return;
    if (a->nbits >= 32u) {
        uint32_t w = (uint32_t)a->acc;
        memcpy(a->out, &w, 4u);
        a->out += 4;
        a->acc >>= 32;
        a->nbits -= 32u;
    }
    a->acc |= (uint64_t)(e & 0xFFFFu) << a->nbits;
    a->nbits += n;
    *pos += n;
}

/* R9-TEXT1-E1: outline acc_put kept for the 20+ cold sites (thin
 * wrapper; fast inlines here too, 1 copy). */
static void lzmesh_u35_acc_put(lzmesh_u35_acc *a, unsigned *pos,
                               unsigned val, unsigned n) {
    lzmesh_u35_acc_put_fast(a, pos, val, n);
}

static void lzmesh_u35_acc_flush(lzmesh_u35_acc *a) {
    while (a->nbits >= 8u) {
        *a->out++ = (uint8_t)a->acc;
        a->acc >>= 8;
        a->nbits -= 8u;
    }
    if (a->nbits > 0u) {
        *a->out++ = (uint8_t)a->acc; /* partial tail, hi bits 0 */
        a->acc = 0u;
        a->nbits = 0u;
    }
}

/* === R2-STORE V1: fused litonly-HUF write (owner: r2-store) ===
 * u35_emit_inner's write half, factored to take precomputed tables.
 * emit_inner delegates below (pure code motion; L1 path exercises this
 * on every gate cell); L0 TRY fns call it with fused probe results.
 * Byte-identical by construction: same puts, same order, same guards. */
typedef struct {
    uint8_t lens[256];
    uint16_t codes[256];
    uint8_t mlens[11];
    uint16_t mcodes[11];
    uint8_t vals[256];
    unsigned used;
    unsigned bitc[8];
    unsigned laneb[8];
    uint8_t idxbuf[24];
    unsigned idxsz;
    uint8_t lenb5[5];
    uint32_t lenB, lenC, modes, bo, fo;
} lzmesh_r2_huftabs;

static size_t lzmesh_r2_emit_write(uint8_t *dst, size_t dst_capacity,
                                  const uint8_t *src, size_t size,
                                  int level, int l0, int noend,
                                  const lzmesh_r2_huftabs *t) {
    unsigned start[8], pos[8];
    lzmesh_u35_acc uacc[8]; /* P6-W4: word accumulator per lane */
    size_t need, s, payload = 0u;
    unsigned i, k;
    const uint8_t *lens = t->lens, *mlens = t->mlens, *vals = t->vals;
    const uint16_t *codes = t->codes, *mcodes = t->mcodes;
    const unsigned *bitc = t->bitc, *laneb = t->laneb;
    const uint8_t *idxbuf = t->idxbuf, *lenb5 = t->lenb5;
    unsigned used = t->used, idxsz = t->idxsz;
    uint32_t lenB = t->lenB, lenC = t->lenC, modes = t->modes;
    uint32_t bo = t->bo, fo = t->fo;
    need = (size_t)fo + 10u + (noend ? 0u : 1u);
    if (dst_capacity < need)
        return 0;
    if (!lzmesh_u4_comp_gates_ok((uint32_t)size, bo, fo))
        return 0;
    if (fo > 0xffffu)
        return 0;
    if (9u + 1u + lenB != bo)
        return 0;
    for (k = 0u; k < 8u; k++)
        payload += laneb[k];
    if (fo != bo + (uint32_t)payload + idxsz)
        return 0;
    lzmesh_u7_comp_header_emit(dst, (uint32_t)size, bo, fo);
    s = 9u;
    dst[s++] = 0xc0u; /* TOK (REPEAT 1B: lit-esc + rep0 mc0) */
    if (lenB == 1u) {
        if (lenC < 1u)
            return 0;
        dst[s++] = lenb5[0]; /* LEN (REPEAT 1B) */
    } else {
        if (lenB != lenC)
            return 0;
        for (i = 0u; i < lenC; i++)
            dst[s++] = lenb5[i]; /* LEN (RAW lcB) */
    }
    if (s != bo)
        return 0;
    for (k = 0u; k < 8u; k++) {
        start[k] = (unsigned)(s - bo);
        s += laneb[k];
        pos[k] = 0u;
        uacc[k].acc = 0u; /* P6-W4 */
        uacc[k].nbits = 0u;
        uacc[k].out = dst + bo + start[k];
    }
    /* R12-L0-H3 (C-ZERO, R4-ENC0 proof adopted): zero-lanes loop
     * DELETED. Post-P6-W4 (word-acc era) every lane byte is written
     * exactly once by acc_flush (per-lane total==bitc[k], out starts
     * at the lane base, sequential, no gaps; header/tok/len/index/
     * footer sit outside [bo,bo+payload)); tail partials have hi-0
     * (acc starts 0, only valid bits ORed) so pads-OR reads the same
     * byte as zero-then-OR; no read of dst lanes happens between.
     * ~0.3% (sample), kept for the traffic cut, gate-held. */
    for (i = 0u; i < 11u; i++)
        lzmesh_u35_acc_put(&uacc[0], &pos[0], mlens[i], 3u);
    { /* 32b bitmap LSB-first: recompute from lens (single source) */
        uint32_t bm = 0u;
        unsigned g, j;
        for (g = 0u; g < 32u; g++) {
            for (j = 0u; j < 8u; j++) {
                if (lens[g * 8u + j] != 0u) {
                    bm |= (uint32_t)1u << g;
                    break;
                }
            }
        }
        if (bm == 0u)
            return 0;
        lzmesh_u35_acc_put(&uacc[0], &pos[0], bm, 32u);
    }
    for (i = 0u; i < used; i++)
        lzmesh_u35_acc_put(&uacc[i & 7u], &pos[i & 7u],
                           mcodes[vals[i]], mlens[vals[i]]);
    { /* R2-STORE V2d: x8 lane unroll. Puts to the same lane keep
       * program order (sequential i); lanes are independent accs so
       * the interleave is unobservable. Fixed lane index per slot
       * drops the i&7 address dance. Tail handles size%8.
       * R12-L0-H2 (C-PUT): single combined code|len table (1 load
       * vs 2: codes[]+lens[] lived on separate lines) + the l0
       * mask-free put clone (contract at its decl). Meta/bitmap
       * puts above keep stock acc_put (other n contracts).
       * SIZE-GATE: the 256-entry build is a net loss under ~300B
       * (L1 small litbuf emits) => stock-verbatim loop below 512B,
       * ctab+clone above. Matrix L0 blocks all >= 16385B. */
        unsigned n8 = (unsigned)size, m8 = n8 & ~7u;
        if (n8 < 512u) {
            for (i = 0u; i < m8; i += 8u) {
                lzmesh_u35_acc_put(&uacc[0], &pos[0], codes[src[i]],
                                   lens[src[i]]);
                lzmesh_u35_acc_put(&uacc[1], &pos[1], codes[src[i + 1u]],
                                   lens[src[i + 1u]]);
                lzmesh_u35_acc_put(&uacc[2], &pos[2], codes[src[i + 2u]],
                                   lens[src[i + 2u]]);
                lzmesh_u35_acc_put(&uacc[3], &pos[3], codes[src[i + 3u]],
                                   lens[src[i + 3u]]);
                lzmesh_u35_acc_put(&uacc[4], &pos[4], codes[src[i + 4u]],
                                   lens[src[i + 4u]]);
                lzmesh_u35_acc_put(&uacc[5], &pos[5], codes[src[i + 5u]],
                                   lens[src[i + 5u]]);
                lzmesh_u35_acc_put(&uacc[6], &pos[6], codes[src[i + 6u]],
                                   lens[src[i + 6u]]);
                lzmesh_u35_acc_put(&uacc[7], &pos[7], codes[src[i + 7u]],
                                   lens[src[i + 7u]]);
            }
            for (i = m8; i < n8; i++)
                lzmesh_u35_acc_put(&uacc[i & 7u], &pos[i & 7u],
                                   codes[src[i]], lens[src[i]]);
        } else {
            uint32_t ctab[256];
            for (i = 0u; i < 256u; i++)
                ctab[i] = (uint32_t)codes[i] | ((uint32_t)lens[i] << 16);
            for (i = 0u; i < m8; i += 8u) {
                lzmesh_u35_acc_put_l0(&uacc[0], &pos[0], ctab[src[i]]);
                lzmesh_u35_acc_put_l0(&uacc[1], &pos[1],
                                      ctab[src[i + 1u]]);
                lzmesh_u35_acc_put_l0(&uacc[2], &pos[2],
                                      ctab[src[i + 2u]]);
                lzmesh_u35_acc_put_l0(&uacc[3], &pos[3],
                                      ctab[src[i + 3u]]);
                lzmesh_u35_acc_put_l0(&uacc[4], &pos[4],
                                      ctab[src[i + 4u]]);
                lzmesh_u35_acc_put_l0(&uacc[5], &pos[5],
                                      ctab[src[i + 5u]]);
                lzmesh_u35_acc_put_l0(&uacc[6], &pos[6],
                                      ctab[src[i + 6u]]);
                lzmesh_u35_acc_put_l0(&uacc[7], &pos[7],
                                      ctab[src[i + 7u]]);
            }
            for (i = m8; i < n8; i++)
                lzmesh_u35_acc_put_l0(&uacc[i & 7u], &pos[i & 7u],
                                      ctab[src[i]]);
        }
    }
    for (k = 0u; k < 8u; k++)
        lzmesh_u35_acc_flush(&uacc[k]); /* P6-W4: drain before guard/pads */
    for (k = 0u; k < 8u; k++)
        if (pos[k] != bitc[k])
            return 0; /* count/emit skew guard */
    if (l0 || level == 1) {
        /* R8 FIX-H (L0 B-TAB pads) + R11 FIX-L (L1 litonly pads):
         * oracle fires B-TAB(lastL,k) pads with lastL = length of the
         * LAST SYMBOL dealt to the lane (L0: 896-lane fit 91 fire +
         * 805 quiet; L1: e01 census 1 fire lane s58-n1000-alphabet
         * lane7 (7,7)->96 + full-corpus agree scan 0 port-side fires
         * outside it). Lanes with no syms keep P=0. Pure last-byte OR:
         * bitc/lengths untouched. L5/L9 keep zero pads (unvalidated). */
        for (k = 0u; k < 8u; k++) {
            unsigned m = bitc[k] & 7u;
            unsigned kk = (8u - m) & 7u;
            if (kk != 0u && (unsigned)size > k) {
                unsigned li = (unsigned)(((size - 1u - k) >> 3) * 8u + k);
                unsigned lastL = lens[src[li]];
                unsigned P = lzmesh_pack1_btab(lastL, kk);
                if (P != 0u) {
                    uint8_t *lb = dst + bo + start[k] + laneb[k] - 1u;
                    *lb = (uint8_t)(*lb | ((P << m) & 0xffu));
                }
            }
        }
    }
    for (i = 0u; i < idxsz; i++)
        dst[s++] = idxbuf[i];
    if (s != fo)
        return 0;
    lzmesh_u4_footer_emit(dst + fo, modes, 1u, lenC, (uint32_t)size, 0u);
    if (!noend)
        dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

static size_t lzmesh_u35_emit_inner(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level, int l0,
                       unsigned ncap, int later, int noend) {
    lzmesh_r2_huftabs t; /* R2-STORE: struct-backed locals, zero-copy */
    uint64_t ctot;
    if (dst == NULL || src == NULL)
        return 0;
    if (l0 ? level != 0 : level != 1)
        return 0; /* emit re-certifies (never diverges from want) */
    if (!l0 && !lzmesh_u35_cert(src, size))
        return 0; /* L0: no finding, parse forced (no cert) */
    if (!lzmesh_u35_lengths(src, size, t.lens, t.codes, t.mlens,
                            t.mcodes, t.vals, &t.used, t.bitc, &ctot,
                            ncap))
        return 0;
    if (!lzmesh_u35_layout_inner(size, t.bitc, &t.lenB, &t.lenC,
                                 &t.modes, &t.bo, &t.fo, t.laneb,
                                 t.idxbuf, (unsigned)sizeof t.idxbuf,
                                 t.lenb5, &t.idxsz, 1, ncap, later))
        return 0;
    if (!l0
        && lzmesh_u35_bprime_decline(src, size, t.lens, t.bitc,
                                     t.laneb))
        return 0;
    return lzmesh_r2_emit_write(dst, dst_capacity, src, size, level, l0,
                                noend, &t);
}

size_t lzmesh_u35_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level) {
    return lzmesh_u35_emit_inner(dst, dst_capacity, src, size, level, 0,
                                 LZMESH_U35_NCAP, 0, 0);
}

size_t lzmesh_u36_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level) {
    return lzmesh_u35_emit_inner(dst, dst_capacity, src, size, level, 1,
                                 LZMESH_U36_NCAP, 0, 0);
}

/* === L0 litonly-HUF multi-block, level 0 only (owner: u36m) === */
/* R5 FIX-D: L0 non-run n>16393 follows the u20 budget schedule
 * (16385/32768/62448-repeat + TEST2 absorb, shared walker): block0
 * is byte-identical to the single-block encoding of the head
 * (R5-proven b0==single(head)); later COMP blocks use the
 * no-pre-emit later shape (len rest n-3, R5-proven exactly-1-byte
 * diff vs single). Per-block COMP iff layout + rollback +
 * header-gates + per-block TIER-1 (ds>fo); NO local TIER-2
 * (R5-proven: alpha tail-33 COMP with blklen 42>33). Global keep
 * is TIER-2 on pre-ff total (sum blklen, END excluded) <= n
 * (R5 D-sweep pin: t=46 single, t=47 multi, outpos 16432 vs n).
 * Mixed content splits per-block (C+R / R+C / C+R+C all probed);
 * all-random collapses to single RAW via global TIER-2. All-equal
 * BLOCKS inside mixed input take the run shape (u10 first /
 * u17-later; whole-input runs still owned by u10/u16). PERF: O(n)
 * lengths+emit per block, walked twice (want) / thrice (emit);
 * stack ~1.5KB. */
#define LZMESH_U36M_BMAX 62456u /* max sched block ds (62448 + 8 absorb) */

/* Per-block COMP probe: 0 = RAW, 1 = HUF-COMP, 2 = RUN-COMP, with
 * *fo_out set for COMP kinds. Run blocks (R5 FIX-D2: oracle emits
 * the run shape for all-equal blocks inside mixed input, e.g.
 * run-head + alpha-tail -> C16385/16 + C-tail) take the u10
 * (first) / u17-later shape with the same per-block gates
 * (TIER-1 auto-RAWs 9..12 tails: ds<=fo). */
/* R2-STORE V2: probe with optional tables capture. t==NULL is exactly
 * today's u36m_block (locals discarded); t!=NULL fills *t on the
 * kind==1 path so the streaming TRY emits without rebuilding. */
static int lzmesh_u36m_block_tables(const uint8_t *blk, size_t bs,
                                    int later, uint32_t *fo_out,
                                    lzmesh_r2_huftabs *t) {
    lzmesh_r2_huftabs local;
    lzmesh_r2_huftabs *d = t != NULL ? t : &local;
    uint64_t ctot;
    uint32_t bo, fo;
    if (blk == NULL || fo_out == NULL)
        return 0;
    if (bs < 4u || bs > (size_t)LZMESH_U36M_BMAX
        || bs > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lzmesh_u9_is_run(blk, bs)) {
        uint32_t lenB, modes;
        int ok = later
            ? lzmesh_u16_l0_later_layout(bs, &lenB, &modes, &bo, &fo)
            : lzmesh_u10_l0_layout(bs, &lenB, &modes, &bo, &fo);
        if (!ok)
            return 0;
        (void)lenB;
        (void)modes;
        if (!lzmesh_u4_comp_gates_ok((uint32_t)bs, bo, fo))
            return 0;
        if (!lzmesh_u3_tier1_comp((uint32_t)bs, fo))
            return 0;
        *fo_out = fo;
        return 2;
    }
    if (!lzmesh_u35_lengths(blk, bs, d->lens, d->codes, d->mlens,
                            d->mcodes, d->vals, &d->used, d->bitc,
                            &ctot, LZMESH_U36M_BMAX))
        return 0;
    (void)ctot;
    if (!lzmesh_u35_layout_inner(bs, d->bitc, &d->lenB, &d->lenC,
                                 &d->modes, &d->bo, &d->fo, d->laneb,
                                 d->idxbuf, (unsigned)sizeof d->idxbuf,
                                 d->lenb5, &d->idxsz, 1,
                                 LZMESH_U36M_BMAX, later))
        return 0;
    bo = d->bo;
    fo = d->fo;
    if (!lzmesh_u4_comp_gates_ok((uint32_t)bs, bo, fo))
        return 0;
    if (!lzmesh_u3_tier1_comp((uint32_t)bs, fo))
        return 0;
    *fo_out = fo;
    return 1;
}

static int lzmesh_u36m_block(const uint8_t *blk, size_t bs, int later,
                             uint32_t *fo_out) {
    return lzmesh_u36m_block_tables(blk, bs, later, fo_out, NULL);
}

int lzmesh_u36m_want(const uint8_t *src, size_t size) {
    int k;
    unsigned i;
    size_t outpos = 0u;
    if (src == NULL || size <= (size_t)LZMESH_U36_NCAP
        || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lzmesh_u9_is_run(src, size))
        return 0; /* runs owned by u10/u16 */
    k = lzmesh_u20_nblocks(size);
    if (k < 2)
        return 0; /* single-block owned by u36 path */
    for (i = 0u; i < (unsigned)k; i++) {
        size_t off, bs;
        uint32_t fo;
        if (!lzmesh_u20_block(size, k, i, &off, &bs))
            return 0;
        if (lzmesh_u36m_block(src + off, bs, i > 0u, &fo))
            outpos += (size_t)fo + 10u;
        else
            outpos += bs + 5u; /* RAW block: tag + u32 + payload */
    }
    return lzmesh_u3_tier2_keep(outpos, size); /* global TIER-2 */
}

size_t lzmesh_u36m_emit(uint8_t *dst, size_t dst_capacity,
                        const uint8_t *src, size_t size, int level) {
    int k;
    unsigned i;
    size_t s = 0u, need = 1u; /* +END */
    if (dst == NULL || src == NULL)
        return 0;
    if (level != 0)
        return 0;
    if (size <= (size_t)LZMESH_U36_NCAP
        || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lzmesh_u9_is_run(src, size))
        return 0;
    k = lzmesh_u20_nblocks(size);
    if (k < 2)
        return 0;
    for (i = 0u; i < (unsigned)k; i++) { /* validate-all pass */
        size_t off, bs;
        uint32_t fo;
        if (!lzmesh_u20_block(size, k, i, &off, &bs))
            return 0;
        if (lzmesh_u36m_block(src + off, bs, i > 0u, &fo))
            need += (size_t)fo + 10u;
        else
            need += bs + 5u;
    }
    if (need - 1u > size)
        return 0; /* global TIER-2 re-check (never diverges) */
    if (dst_capacity < need)
        return 0;
    for (i = 0u; i < (unsigned)k; i++) { /* write pass */
        size_t off, bs, w;
        uint32_t fo, rds;
        size_t j;
        int kind;
        if (!lzmesh_u20_block(size, k, i, &off, &bs))
            return 0;
        kind = lzmesh_u36m_block(src + off, bs, i > 0u, &fo);
        if (kind == 0) {
            if (bs == 0u || bs > (size_t)LZMESH_U1_DS_MAX)
                return 0;
            rds = (uint32_t)bs;
            dst[s++] = (uint8_t)LZMESH_U1_TAG_RAW;
            dst[s++] = (uint8_t)(rds & 0xffu);
            dst[s++] = (uint8_t)((rds >> 8) & 0xffu);
            dst[s++] = (uint8_t)((rds >> 16) & 0xffu);
            dst[s++] = (uint8_t)((rds >> 24) & 0xffu);
            for (j = 0u; j < bs; j++)
                dst[s++] = src[off + j];
            continue;
        }
        if (kind == 2) { /* RUN-COMP block (u10 first / u17 later) */
            uint32_t lenB, modes, bo, rfo, ds;
            uint8_t tok, lenb[5];
            size_t t;
            int ok = (i == 0u)
                ? lzmesh_u10_l0_layout(bs, &lenB, &modes, &bo, &rfo)
                : lzmesh_u16_l0_later_layout(bs, &lenB, &modes, &bo,
                                             &rfo);
            if (!ok || rfo != fo)
                return 0;
            ds = (uint32_t)bs;
            if (!lzmesh_u4_comp_gates_ok(ds, bo, rfo))
                return 0;
            if (9u + 2u + lenB != bo)
                return 0;
            if (lzmesh_u7_len_escape_write(ds - (i == 0u ? 4u : 3u),
                                           lenb) != lenB)
                return 0;
            if (need - s < (size_t)rfo + 10u)
                return 0;
            tok = lzmesh_u4_token(3u, 0u, 0u); /* 0xC0 */
            lzmesh_u7_comp_header_emit(dst + s, ds, bo, rfo);
            t = s + 9u;
            dst[t++] = src[off]; /* LIT (REPEAT 1B) */
            dst[t++] = tok; /* TOK (REPEAT 1B) */
            if (lenB == 1u) {
                dst[t++] = lenb[0];
            } else {
                dst[t++] = lenb[0];
                dst[t++] = lenb[1];
                dst[t++] = lenb[2];
                dst[t++] = lenb[3];
                dst[t++] = lenb[4];
            }
            if (t != s + bo)
                return 0;
            lzmesh_u4_footer_emit(dst + s + rfo, modes, 1u, lenB, ds,
                                  0u);
            s += (size_t)rfo + 10u;
            continue;
        }
        w = lzmesh_u35_emit_inner(dst + s, need - s, src + off, bs, 0,
                                  1, LZMESH_U36M_BMAX, i > 0u, 1);
        if (w == 0u || w != (size_t)fo + 10u)
            return 0;
        s += w;
    }
    dst[s++] = (uint8_t)LZMESH_U1_TAG_END;
    return s == need ? need : 0;
}

/* === R2-STORE V1: fused L0 TRY (owner: r2-store) ===
 * P14-S1 precedent (u37 TRY): probe+emit in one evaluation.
 * Probe-fail verdicts mirror dispatch exactly (want=0 -> P11_NONE ->
 * single RAW); write-half failures mirror emit (return 0). Probes are
 * pure in (src,size) (P11 purity precedent), so fused == separate.
 * u36/u36m want+emit stay intact (u37 fallback + API compat). */

/* R2-STORE V2: shared multi-block piece writers (V1-cached + V2-stream).
 * Byte-identical to the u36m_emit write pass: same header/payload
 * order, same checks (cap-substituted for need), same 0-paths. */

/* RAW block: tag + ds u32LE + payload. Returns bs+5, else 0. */
static size_t lzmesh_r2_raw_block(uint8_t *dst, size_t cap,
                                  const uint8_t *src, size_t off,
                                  size_t bs) {
    uint32_t rds;
    size_t s = 0u, j;
    if (dst == NULL || src == NULL)
        return 0;
    if (bs == 0u || bs > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (cap < bs + 5u)
        return 0;
    rds = (uint32_t)bs;
    dst[s++] = (uint8_t)LZMESH_U1_TAG_RAW;
    dst[s++] = (uint8_t)(rds & 0xffu);
    dst[s++] = (uint8_t)((rds >> 8) & 0xffu);
    dst[s++] = (uint8_t)((rds >> 16) & 0xffu);
    dst[s++] = (uint8_t)((rds >> 24) & 0xffu);
    for (j = 0u; j < bs; j++)
        dst[s++] = src[off + j];
    return s;
}

/* RUN-COMP block (u10 first / u17 later). fo = cached probe value.
 * Returns rfo+10 (== fo+10, re-certified), else 0. */
static size_t lzmesh_r2_run_block(uint8_t *dst, size_t cap, uint8_t lit,
                                  size_t bs, int first, uint32_t fo) {
    uint32_t lenB, modes, bo, rfo, ds;
    uint8_t tok, lenb[5];
    size_t s = 0u, t;
    int ok = first
        ? lzmesh_u10_l0_layout(bs, &lenB, &modes, &bo, &rfo)
        : lzmesh_u16_l0_later_layout(bs, &lenB, &modes, &bo, &rfo);
    if (dst == NULL)
        return 0;
    if (!ok || rfo != fo)
        return 0;
    ds = (uint32_t)bs;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, rfo))
        return 0;
    if (9u + 2u + lenB != bo)
        return 0;
    if (lzmesh_u7_len_escape_write(ds - (first ? 4u : 3u), lenb)
        != lenB)
        return 0;
    if (cap < (size_t)rfo + 10u)
        return 0;
    tok = lzmesh_u4_token(3u, 0u, 0u); /* 0xC0 */
    lzmesh_u7_comp_header_emit(dst + s, ds, bo, rfo);
    t = s + 9u;
    dst[t++] = lit; /* LIT (REPEAT 1B) */
    dst[t++] = tok; /* TOK (REPEAT 1B) */
    if (lenB == 1u) {
        dst[t++] = lenb[0];
    } else {
        dst[t++] = lenb[0];
        dst[t++] = lenb[1];
        dst[t++] = lenb[2];
        dst[t++] = lenb[3];
        dst[t++] = lenb[4];
    }
    if (t != s + bo)
        return 0;
    lzmesh_u4_footer_emit(dst + s + rfo, modes, 1u, lenB, ds, 0u);
    s += (size_t)rfo + 10u;
    return s;
}

size_t lzmesh_r2_u36_try(uint8_t *dst, size_t dst_capacity,
                         const uint8_t *src, size_t size, int level) {
    lzmesh_r2_huftabs t;
    uint64_t ctot;
    if (dst == NULL || src == NULL)
        return 0;
    if (level != 0 || size < 4u || size > LZMESH_U36_NCAP
        || size > (size_t)LZMESH_U1_DS_MAX)
        return p14_raw_emit(dst, dst_capacity, src, size);
    if (lzmesh_u9_is_run(src, size))
        return p14_raw_emit(dst, dst_capacity, src, size);
    if (!lzmesh_u35_lengths(src, size, t.lens, t.codes, t.mlens,
                            t.mcodes, t.vals, &t.used, t.bitc, &ctot,
                            LZMESH_U36_NCAP))
        return p14_raw_emit(dst, dst_capacity, src, size);
    if (!lzmesh_u35_layout(size, t.bitc, &t.lenB, &t.lenC, &t.modes,
                           &t.bo, &t.fo, t.laneb, t.idxbuf,
                           (unsigned)sizeof t.idxbuf, t.lenb5, &t.idxsz,
                           1))
        return p14_raw_emit(dst, dst_capacity, src, size);
    if (!lzmesh_u7_comp_keep((uint32_t)size, t.fo, (size_t)t.fo + 10u,
                             size, (uint32_t)size, t.bo,
                             (uint32_t)size, 1))
        return p14_raw_emit(dst, dst_capacity, src, size);
    return lzmesh_r2_emit_write(dst, dst_capacity, src, size, 0, 1, 0,
                                &t);
}

/* (kind,fo) probe cache cap: 256 blocks ~= 16MB at full B2 caps.
 * k>KCAP falls back to today's want+emit verbatim (zero regression). */
#define LZMESH_R2_KCAP 256u

size_t lzmesh_r2_u36m_try(uint8_t *dst, size_t dst_capacity,
                          const uint8_t *src, size_t size, int level) {
    uint32_t kinds[LZMESH_R2_KCAP], fos[LZMESH_R2_KCAP];
    int k;
    unsigned i;
    size_t outpos = 0u, s = 0u, need;
    if (dst == NULL || src == NULL)
        return 0;
    if (level != 0 || size <= (size_t)LZMESH_U36_NCAP
        || size > (size_t)LZMESH_U1_DS_MAX)
        return p14_raw_emit(dst, dst_capacity, src, size);
    if (lzmesh_u9_is_run(src, size))
        return p14_raw_emit(dst, dst_capacity, src, size);
    k = lzmesh_u20_nblocks(size);
    if (k < 2)
        return p14_raw_emit(dst, dst_capacity, src, size);
    /* V2 stream gate: per-block TIER-1 (bs>fo) bounds every COMP block
     * fo+10 < bs+10 and every RAW block bs+5 < bs+10, so need =
     * outpos+1 <= size+10k (integer floor): cap >= size+10k fits
     * every block write. No size_t wrap: size <= 2^31-1,
     * k <= size/9, size+10k < 2^63. */
    if (dst_capacity >= size + 10u * (size_t)k) {
        /* V2 stream: one probe+emit per block into a single reused
         * tables struct (1 build/block, O(1) mem); TIER-2 epilogue.
         * TIER-2-fail overwrites with single RAW (raw_emit writes
         * dst[0..] fully, final bytes == want-fail path). Cap-short
         * stays pristine (gate proven upfront); never-diverge 0-paths
         * match today's post-write 0 class. */
        lzmesh_r2_huftabs lot;
        outpos = 0u;
        s = 0u;
        for (i = 0u; i < (unsigned)k; i++) {
            size_t off, bs, w;
            uint32_t fo = 0u;
            int kind;
            if (!lzmesh_u20_block(size, k, i, &off, &bs))
                return 0;
            kind = lzmesh_u36m_block_tables(src + off, bs, i > 0u,
                                            &fo, &lot);
            if (kind == 0) {
                w = lzmesh_r2_raw_block(dst + s, dst_capacity - s,
                                        src, off, bs);
                if (w == 0u)
                    return 0;
            } else if (kind == 2) {
                w = lzmesh_r2_run_block(dst + s, dst_capacity - s,
                                        src[off], bs, i == 0u, fo);
                if (w == 0u)
                    return 0;
            } else {
                w = lzmesh_r2_emit_write(dst + s, dst_capacity - s,
                                         src + off, bs, 0, 1, 1,
                                         &lot);
                if (w == 0u || w != (size_t)fo + 10u)
                    return 0;
            }
            outpos += kind ? (size_t)fo + 10u : bs + 5u;
            s += w;
        }
        if (!lzmesh_u3_tier2_keep(outpos, size))
            return p14_raw_emit(dst, dst_capacity, src, size);
        need = outpos + 1u; /* +END */
        if (need - 1u > size)
            return 0; /* unreachable: tier2 just passed */
        if (dst_capacity < need)
            return 0; /* unreachable: gate proven upfront */
        dst[s++] = (uint8_t)LZMESH_U1_TAG_END;
        return s == need ? need : 0;
    }
    if (k > (int)LZMESH_R2_KCAP) /* huge-n: today's path verbatim */
        return lzmesh_u36m_want(src, size)
            ? lzmesh_u36m_emit(dst, dst_capacity, src, size, level)
            : p14_raw_emit(dst, dst_capacity, src, size);
    for (i = 0u; i < (unsigned)k; i++) { /* single probe pass */
        size_t off, bs;
        uint32_t fo = 0u;
        int kind;
        if (!lzmesh_u20_block(size, k, i, &off, &bs))
            return p14_raw_emit(dst, dst_capacity, src, size);
        kind = lzmesh_u36m_block(src + off, bs, i > 0u, &fo);
        kinds[i] = (uint32_t)kind;
        fos[i] = fo;
        if (kind)
            outpos += (size_t)fo + 10u;
        else
            outpos += bs + 5u;
    }
    if (!lzmesh_u3_tier2_keep(outpos, size))
        return p14_raw_emit(dst, dst_capacity, src, size);
    need = outpos + 1u; /* +END */
    if (need - 1u > size)
        return 0; /* global TIER-2 re-check (never diverges) */
    if (dst_capacity < need)
        return 0;
    for (i = 0u; i < (unsigned)k; i++) { /* write pass (cached) */
        size_t off, bs, w;
        uint32_t fo = fos[i];
        uint32_t kind = kinds[i];
        if (!lzmesh_u20_block(size, k, i, &off, &bs))
            return 0;
        if (kind == 0u) {
            w = lzmesh_r2_raw_block(dst + s, need - s, src, off, bs);
            if (w == 0u)
                return 0;
            s += w;
            continue;
        }
        if (kind == 2u) { /* RUN-COMP block (u10 first / u17 later) */
            w = lzmesh_r2_run_block(dst + s, need - s, src[off], bs,
                                    i == 0u, fo);
            if (w == 0u)
                return 0;
            s += w;
            continue;
        }
        { /* kind == 1: single fresh tables build + cached write */
            lzmesh_r2_huftabs t;
            uint64_t ctot;
            const uint8_t *blk = src + off;
            if (!lzmesh_u35_lengths(blk, bs, t.lens, t.codes, t.mlens,
                                    t.mcodes, t.vals, &t.used, t.bitc,
                                    &ctot, LZMESH_U36M_BMAX))
                return 0;
            if (!lzmesh_u35_layout_inner(bs, t.bitc, &t.lenB, &t.lenC,
                                         &t.modes, &t.bo, &t.fo,
                                         t.laneb, t.idxbuf,
                                         (unsigned)sizeof t.idxbuf,
                                         t.lenb5, &t.idxsz, 1,
                                         LZMESH_U36M_BMAX, i > 0u))
                return 0;
            w = lzmesh_r2_emit_write(dst + s, need - s, blk, bs, 0, 1,
                                     1, &t);
            if (w == 0u || w != (size_t)fo + 10u)
                return 0;
            s += w;
        }
    }
    dst[s++] = (uint8_t)LZMESH_U1_TAG_END;
    return s == need ? need : 0;
}

/* === L1 litonly-HUF multi-token dist0, level 1 only (owner: u35m/G4) === */
/* G4: oracle e01 dist0 multitok (tok2-18). L1 rep0-only greedy + skip
 * P+=(P-litpos)>>8 then P+=1, guard pos+9<=n, immediate take (no lazy,
 * no hash: gated to no eligible fresh6 so NEW impossible). 15/15 takes
 * exact (tmp/g4/skipsim-all.py). Lit-HUF tables via stock u35_lengths
 * on lit-only buffer (tables code untouched, E6 owns); tok/len
 * RAW/REPEAT (never HUF), dist RAW count0. Single-block n<=16393;
 * multi-block tails open. MUST-NOT held: u35 tables unmodified,
 * single-token path untouched (takes==0 declines to u35). */
#define LZMESH_U35M_TOKCAP 1024u
#define LZMESH_U35M_LENCAP 8192u
/* G4: 1 iff eligible fresh6 exists (dist>=2, ml>=6, pos+9<=n). */
static int lzmesh_u35m_has_fresh6(const uint8_t *s, size_t n) {
    size_t j, i;
    if (s == NULL || n > LZMESH_U35_NCAP)
        return 1;
    if (n < 11u)
        return 0;
    for (j = 2u; j + 9u <= n; j++) {
        for (i = 0u; i + 2u <= j; i++) {
            size_t ml = 0u;
            while (ml < 6u && j + ml < n && s[i + ml] == s[j + ml])
                ml++;
            if (ml >= 6u)
                return 1;
        }
    }
    return 0;
}
/* G4: skip+rep0 parse. Fills tpos/tlen (cap), returns ntakes. 0 on
 * none/overflow/err (caller declines). P/litpos 0-based, P starts 1. */
static size_t lzmesh_u35m_parse(const uint8_t *s, size_t n, uint32_t *tpos,
                                uint32_t *tlen, size_t cap) {
    size_t p, litpos, ntok;
    if (s == NULL || tpos == NULL || tlen == NULL || cap == 0u)
        return 0u;
    if (n < 4u || n > LZMESH_U35_NCAP)
        return 0u;
    p = 1u;
    litpos = 1u;
    ntok = 0u;
    while (p < n) {
        if (p + 9u > n)
            break;
        if (p + 2u <= n && s[p] == s[p - 1u] && s[p + 1u] == s[p]) {
            size_t q = p - 1u, ln = 2u;
            while (p + ln < n && s[p + ln] == s[q + ln])
                ln++;
            if (ntok >= cap || ln > 0xFFFFFFFFu)
                return 0u;
            tpos[ntok] = (uint32_t)p;
            tlen[ntok] = (uint32_t)ln;
            ntok++;
            p += ln;
            litpos = p;
        } else {
            p += (p - litpos) >> 8;
            p += 1u;
        }
    }
    return ntok;
}
typedef struct {
    uint32_t tpos[LZMESH_U35M_TOKCAP];
    uint32_t tlen[LZMESH_U35M_TOKCAP];
    size_t ntakes;
    uint8_t litbuf[LZMESH_U35_NCAP];
    uint32_t litc;
    uint8_t tokbuf[LZMESH_U35M_TOKCAP + 1u];
    uint32_t tokc;
    uint8_t lenbuf[LZMESH_U35M_LENCAP];
    uint32_t lenc;
    uint8_t lens[256];
    uint16_t codes[256];
    uint8_t mlens[11];
    uint16_t mcodes[11];
    uint8_t vals[256];
    unsigned used;
    unsigned bitc[8];
    uint64_t ctot;
    uint32_t modes;
    uint32_t bo;
    uint32_t fo;
    unsigned laneb[8];
    uint8_t idxbuf[24];
    unsigned idxsz;
} lzmesh_u35m_work;
/* G4: prepare (parse+tables+layout+keep). 1 with *w filled, else 0. */
static int lzmesh_u35m_prepare(const uint8_t *s, size_t n,
                               lzmesh_u35m_work *w) {
    size_t ti, li, si, nlc;
    uint64_t tot;
    unsigned k, m_tok, m_len, isz;
    uint32_t sumlen, payload, tc;
    uint8_t eb[5];
    if (s == NULL || w == NULL)
        return 0;
    if (n < 4u || n > LZMESH_U35_NCAP || n > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lzmesh_u9_is_run(s, n))
        return 0;
    if (lzmesh_u12_period(s, n) != 0)
        return 0;
    if (lzmesh_u18_period(s, n) != 0)
        return 0;
    if (lzmesh_u35m_has_fresh6(s, n))
        return 0;
    w->ntakes = lzmesh_u35m_parse(s, n, w->tpos, w->tlen,
                                  (size_t)LZMESH_U35M_TOKCAP);
    if (w->ntakes == 0u || w->ntakes > (size_t)LZMESH_U35M_TOKCAP)
        return 0;
    sumlen = 0u;
    li = 0u;
    ti = 0u;
    for (si = 0u; si < n; si++) {
        if (ti < w->ntakes && si == (size_t)w->tpos[ti]) {
            uint32_t ml = w->tlen[ti];
            if (ml < 2u || si + (size_t)ml > n)
                return 0;
            sumlen += ml;
            si += (size_t)ml - 1u;
            ti++;
        } else {
            if (li >= (size_t)LZMESH_U35_NCAP)
                return 0;
            w->litbuf[li++] = s[si];
        }
    }
    if (ti != w->ntakes || li + (size_t)sumlen != n)
        return 0;
    if (li < 1u || li > 0xFFFFFFFFu)
        return 0;
    w->litc = (uint32_t)li;
    if (!lzmesh_u35_lengths(w->litbuf, (size_t)w->litc, w->lens, w->codes,
                            w->mlens, w->mcodes, w->vals, &w->used,
                            w->bitc, &w->ctot, LZMESH_U35_NCAP))
        return 0;
    tc = (uint32_t)w->ntakes + 1u;
    if (tc > (uint32_t)LZMESH_U35M_TOKCAP + 1u)
        return 0;
    w->tokc = tc;
    nlc = 0u;
    for (ti = 0u; ti < w->ntakes; ti++) {
        uint32_t run, mc, lf, ms;
        unsigned eb_n, ebi;
        if (ti == 0u) {
            if (w->tpos[0u] < 1u)
                return 0;
            run = w->tpos[0u] - 1u;
        } else {
            uint32_t pe = w->tpos[ti - 1u] + w->tlen[ti - 1u];
            if (w->tpos[ti] < pe)
                return 0;
            run = w->tpos[ti] - pe;
        }
        if (w->tlen[ti] < 2u)
            return 0;
        mc = w->tlen[ti] - 2u;
        lf = (run < 3u) ? run : 3u;
        ms = (mc < 7u) ? mc : 7u;
        w->tokbuf[ti] = lzmesh_u4_token((unsigned)lf, 0u, (unsigned)ms);
        if (lf == 3u) {
            if (run < 3u)
                return 0;
            eb_n = lzmesh_u7_len_escape_write(run - 3u, eb);
            if (eb_n == 0u || eb_n > 5u)
                return 0;
            if (nlc + (size_t)eb_n > (size_t)LZMESH_U35M_LENCAP)
                return 0;
            for (ebi = 0u; ebi < eb_n; ebi++)
                w->lenbuf[nlc++] = eb[ebi];
        }
        if (ms == 7u) {
            if (mc < 7u)
                return 0;
            eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
            if (eb_n == 0u || eb_n > 5u)
                return 0;
            if (nlc + (size_t)eb_n > (size_t)LZMESH_U35M_LENCAP)
                return 0;
            for (ebi = 0u; ebi < eb_n; ebi++)
                w->lenbuf[nlc++] = eb[ebi];
        }
    }
    {
        uint32_t le, run, lf;
        unsigned eb_n, ebi;
        le = w->tpos[w->ntakes - 1u] + w->tlen[w->ntakes - 1u];
        if (le > (uint32_t)n)
            return 0;
        run = (uint32_t)n - le;
        lf = (run < 3u) ? run : 3u;
        w->tokbuf[w->ntakes] = lzmesh_u4_token((unsigned)lf, 0u, 0u);
        if (lf == 3u) {
            if (run < 3u)
                return 0;
            eb_n = lzmesh_u7_len_escape_write(run - 3u, eb);
            if (eb_n == 0u || eb_n > 5u)
                return 0;
            if (nlc + (size_t)eb_n > (size_t)LZMESH_U35M_LENCAP)
                return 0;
            for (ebi = 0u; ebi < eb_n; ebi++)
                w->lenbuf[nlc++] = eb[ebi];
        }
    }
    if (nlc > 0xFFFFFFFFu)
        return 0;
    w->lenc = (uint32_t)nlc;
    /* G4 stopgap: long-lenc multis hit missing B-TAB (4,kk) pads
     * (s04 lenc11 + s07 lenc29 fire, port btab has no (4,kk)). Open
     * for pads lane; decline to hold base bytes (no NEW). */
    if (w->lenc > 10u)
        return 0;
    {
        int tokeq = 1, leneq = 1;
        uint32_t i;
        for (i = 1u; i < w->tokc; i++) {
            if (w->tokbuf[i] != w->tokbuf[0u]) {
                tokeq = 0;
                break;
            }
        }
        for (i = 1u; i < w->lenc; i++) {
            if (w->lenbuf[i] != w->lenbuf[0u]) {
                leneq = 0;
                break;
            }
        }
        m_tok = tokeq ? LZMESH_U4_MODE_REPEAT : LZMESH_U4_MODE_RAW;
        if (w->lenc == 0u)
            m_len = LZMESH_U4_MODE_RAW;
        else
            m_len = leneq ? LZMESH_U4_MODE_REPEAT : LZMESH_U4_MODE_RAW;
        w->modes = lzmesh_u4_modes_pack(m_tok, m_len,
                                        LZMESH_U4_MODE_HUFFMAN,
                                        LZMESH_U4_MODE_RAW);
    }
    {
        uint32_t tok_pay, len_pay;
        tot = 0u;
        payload = 0u;
        tok_pay = (m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : w->tokc;
        if (w->lenc == 0u)
            len_pay = 0u;
        else
            len_pay = (m_len == LZMESH_U4_MODE_REPEAT) ? 1u : w->lenc;
        if (9u + tok_pay + len_pay > 0xFFFFu)
            return 0;
        w->bo = 9u + tok_pay + len_pay;
        for (k = 0u; k < 8u; k++) {
            unsigned c = w->bitc[k], L;
            if (c == 0u)
                return 0;
            tot += (uint64_t)c;
            L = (c + 7u) / 8u;
            w->laneb[k] = L;
            payload += L;
        }
        if (tot >= (uint64_t)8u * (uint64_t)w->litc)
            return 0;
        isz = lzmesh_pack1_index_pack(w->laneb, w->idxbuf,
                                      (unsigned)sizeof w->idxbuf);
        if (isz == 0u)
            return 0;
        w->idxsz = isz;
        if ((uint64_t)w->bo + (uint64_t)payload + (uint64_t)isz > 0xFFFFu)
            return 0;
        w->fo = w->bo + payload + isz;
    }
    if (w->fo > 0xFFFFu)
        return 0;
    /* G4: pads use pos-based lastL (emit mirrors); no B-TAB decline
     * (bprime parity open, never fires on corpus flat tables). */
    return lzmesh_u7_comp_keep((uint32_t)n, w->fo, (size_t)w->fo + 10u,
                               n, (uint32_t)n, w->bo, w->litc, 1);
}
int lzmesh_u35m_want(const uint8_t *src, size_t size, int level) {
    lzmesh_u35m_work w;
    if (level != 1)
        return 0;
    if (src == NULL)
        return 0;
    return lzmesh_u35m_prepare(src, size, &w);
}
size_t lzmesh_u35m_emit(uint8_t *dst, size_t dst_capacity,
                        const uint8_t *src, size_t size, int level) {
    lzmesh_u35m_work w;
    size_t need, s, payload;
    unsigned start[8], pos[8], k, i;
    lzmesh_u35_acc uacc[8]; /* P6-W4 */
    uint32_t m_tok, m_len;
    if (dst == NULL || src == NULL)
        return 0;
    if (level != 1)
        return 0;
    if (!lzmesh_u35m_prepare(src, size, &w))
        return 0;
    need = (size_t)w.fo + 11u;
    if (dst_capacity < need)
        return 0;
    if (!lzmesh_u4_comp_gates_ok((uint32_t)size, w.bo, w.fo))
        return 0;
    if (w.fo > 0xFFFFu)
        return 0;
    m_tok = (w.modes >> 3) & 7u;
    m_len = (w.modes >> 6) & 7u;
    payload = 0u;
    for (k = 0u; k < 8u; k++)
        payload += w.laneb[k];
    if (w.fo != w.bo + payload + w.idxsz)
        return 0;
    lzmesh_u7_comp_header_emit(dst, (uint32_t)size, w.bo, w.fo);
    s = 9u;
    if (m_tok == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = w.tokbuf[0u];
    } else {
        if (m_tok != LZMESH_U4_MODE_RAW)
            return 0;
        for (i = 0u; i < w.tokc; i++)
            dst[s++] = w.tokbuf[i];
    }
    if (w.lenc == 0u) {
        if (m_len != LZMESH_U4_MODE_RAW)
            return 0;
    } else if (m_len == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = w.lenbuf[0u];
    } else {
        if (m_len != LZMESH_U4_MODE_RAW)
            return 0;
        for (i = 0u; i < w.lenc; i++)
            dst[s++] = w.lenbuf[i];
    }
    if (s != (size_t)w.bo)
        return 0;
    for (k = 0u; k < 8u; k++) {
        start[k] = (unsigned)(s - (size_t)w.bo);
        s += (size_t)w.laneb[k];
        pos[k] = 0u;
        uacc[k].acc = 0u; /* P6-W4 */
        uacc[k].nbits = 0u;
        uacc[k].out = dst + (size_t)w.bo + (size_t)start[k];
    }
    for (i = 0u; i < payload; i++)
        dst[(size_t)w.bo + (size_t)i] = 0u;
    for (i = 0u; i < 11u; i++)
        lzmesh_u35_acc_put(&uacc[0], &pos[0],
                           (unsigned)w.mlens[i], 3u);
    {
        uint32_t bm = 0u;
        unsigned g, j;
        for (g = 0u; g < 32u; g++) {
            for (j = 0u; j < 8u; j++) {
                if (w.lens[g * 8u + j] != 0u) {
                    bm |= (uint32_t)1u << g;
                    break;
                }
            }
        }
        if (bm == 0u)
            return 0;
        lzmesh_u35_acc_put(&uacc[0], &pos[0], bm, 32u);
    }
    for (i = 0u; i < w.used; i++)
        lzmesh_u35_acc_put(&uacc[i & 7u],
                           &pos[i & 7u], (unsigned)w.mcodes[w.vals[i]],
                           (unsigned)w.mlens[w.vals[i]]);
    for (i = 0u; i < w.litc; i++)
        lzmesh_u35_acc_put(&uacc[i & 7u],
                           &pos[i & 7u], (unsigned)w.codes[w.litbuf[i]],
                           (unsigned)w.lens[w.litbuf[i]]);
    for (k = 0u; k < 8u; k++)
        lzmesh_u35_acc_flush(&uacc[k]); /* P6-W4: drain before guard/pads */
    for (k = 0u; k < 8u; k++) {
        if (pos[k] != w.bitc[k])
            return 0;
    }
    for (k = 0u; k < 8u; k++) {
        unsigned m = w.bitc[k] & 7u, kk = (8u - m) & 7u;
        /* G4: pos-based lastL (black-box: s04/s07 pads fire). */
        if (kk != 0u && size > (size_t)k) {
            uint32_t li2 = (uint32_t)(((size - 1u - (size_t)k) >> 3) * 8u
                + (size_t)k);
            unsigned lastL = (unsigned)w.lens[src[li2]];
            unsigned P = lzmesh_pack1_btab(lastL, kk);
            if (P != 0u) {
                uint8_t *lb = dst + (size_t)w.bo + (size_t)start[k]
                    + (size_t)w.laneb[k] - 1u;
                *lb = (uint8_t)(*lb | ((P << m) & 0xffu));
            }
        }
    }
    for (i = 0u; i < w.idxsz; i++)
        dst[s++] = w.idxbuf[i];
    if (s != (size_t)w.fo)
        return 0;
    lzmesh_u4_footer_emit(dst + (size_t)w.fo, w.modes, w.tokc, w.lenc,
                          w.litc, 0u);
    dst[(size_t)w.fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === L1 litonly-HUF multitok dist0 multi-block, level 1 only (owner: u35m6/H6) === */
/* H6: oracle e01 multi-block litonly multitok dist0 (s05-n65537 3blk +
 * s14-n32768 2blk IDENT). Per-block G4-shape parse+framing + spend-cut
 * schedule (HINT-BLOCKEND-R1 budgets 16384/32768/62448: cut when
 * spend==B, spend=takes+lenc+runlits with runlits excluding implicit
 * byte0 on blk0) + later-block (P=9,litpos=0) reset (blk0 keeps G4
 * (1,1)). Black-box pinned tmp/h6/ (census 98 e01 multi: only s05/s14
 * pass with DS+takes exact; crafted p0 pins rel<=8 miss / rel>=9 take,
 * doubles, (9,0)-only lattice). Gates mirror G4/u36m: level 1, n in
 * (16393,1M], whole non-run non-periodic, global no-fresh6 (O(n) hash,
 * G4-exact), >=1 take, per-block lenc<=10 + tok/len alleq-or-RAW +
 * TIER-1, no per-block TIER-2, global TIER-2. Declines (falls through
 * to current bytes, zero NEW) on: fresh6, take-overshoot, exact-cut
 * (term run 0 mid-input), guard-zone takes, cross-cut takes, tiny last
 * block (<=12, absorb/RAW-tail unknown), tok/len-HUF-needing blocks
 * (non-alleq count>10), bs>65535, k<2, tables/layout fail. s13
 * first-block tok18 reparsed exactly but declined via fresh6 (later
 * blocks take NEW, GEN owns). PERF: O(n) hash + O(k) parses per pass;
 * want+emit walk ~4x; mallocs hash (<=16MB) + per-block litbuf. */
#define LZMESH_U35M6_BMAX 65535u
#define LZMESH_U35M6_TOKCAP 1024u
#define LZMESH_U35M6_KCAP 128u
#define LZMESH_U35M6_NMAX 0x100000u
extern void *malloc(size_t n);
extern void free(void *p);
typedef struct {
    uint32_t h;
    uint32_t pos;
} lzmesh_h6_slot;
#define LZMESH_H6_EMPTY 0xFFFFFFFFu
static uint32_t lzmesh_h6_key6(const uint8_t *s) {
    uint64_t k = (uint64_t)s[0] | ((uint64_t)s[1] << 8)
        | ((uint64_t)s[2] << 16) | ((uint64_t)s[3] << 24)
        | ((uint64_t)s[4] << 32) | ((uint64_t)s[5] << 40);
    k *= 0x9E3779B97F4A7C15ULL;
    return (uint32_t)(k >> 32);
}
static void lzmesh_h6_insert(lzmesh_h6_slot *t, uint32_t mask,
                             const uint8_t *s, size_t p) {
    uint32_t h = lzmesh_h6_key6(s + p), x = h & mask;
    for (;;) {
        if (t[x].pos == LZMESH_H6_EMPTY) {
            t[x].h = h;
            t[x].pos = (uint32_t)p;
            return;
        }
        if (t[x].h == h && memcmp(s + t[x].pos, s + p, 6) == 0)
            return; /* ascending inserts: first (=min) wins */
        x = (x + 1u) & mask;
    }
}
static int lzmesh_h6_query(lzmesh_h6_slot *t, uint32_t mask,
                           const uint8_t *s, size_t j) {
    uint32_t h = lzmesh_h6_key6(s + j), x = h & mask;
    for (;;) {
        if (t[x].pos == LZMESH_H6_EMPTY)
            return 0;
        if (t[x].h == h && memcmp(s + t[x].pos, s + j, 6) == 0)
            return (t[x].pos + 2u <= (uint32_t)j) ? 1 : 0;
        x = (x + 1u) & mask;
    }
}
/* H6: 1 iff eligible fresh6 exists (G4-exact: j in [2,n-9], i in
 * [0,j-2], 6B match). O(n) open hash, check-before-insert, early exit.
 * 1 on OOM/oversize (decline-safe). */
static int lzmesh_h6_has_fresh6(const uint8_t *s, size_t n) {
    lzmesh_h6_slot *t;
    uint32_t mask, i;
    size_t j, slots, m;
    unsigned hb;
    if (s == NULL)
        return 1;
    if (n < 11u)
        return 0;
    if (n > (size_t)LZMESH_U35M6_NMAX)
        return 1;
    /* R9-MIX1 F6-PRE: exact small-table prefilter (stack, no malloc).
     * Same open-hash semantics as the full table below over j in
     * [2,F6PRE_J): a hit here is a true fresh6 (memcmp-verified) so
     * return 1 is sound. Miss => fall through to the full table
     * (verbatim path). Bench j_hit 292/12 << F6PRE_J; FULL 46/47
     * hits within range, 21 full-scan cells pay +2048 probes. L1-only
     * callers (u35m6 want/emit gate level==1 first): L5 executes zero
     * bytes here. */
    {
        enum { F6PRE_K = 4096u, F6PRE_J = 2048u };
        lzmesh_h6_slot pt[F6PRE_K];
        uint32_t pti;
        size_t pj;
        for (pti = 0u; pti < F6PRE_K; pti++)
            pt[pti].pos = LZMESH_H6_EMPTY;
        lzmesh_h6_insert(pt, F6PRE_K - 1u, s, 0u);
        lzmesh_h6_insert(pt, F6PRE_K - 1u, s, 1u);
        for (pj = 2u; pj + 9u <= n && pj < (size_t)F6PRE_J; pj++) {
            if (lzmesh_h6_query(pt, F6PRE_K - 1u, s, pj))
                return 1;
            lzmesh_h6_insert(pt, F6PRE_K - 1u, s, pj);
        }
    }
    hb = 0u;
    m = n;
    while (m > 1u) {
        m >>= 1;
        hb++;
    }
    hb += 2u;
    if (hb < 10u)
        hb = 10u;
    if (hb > 21u)
        hb = 21u;
    slots = (size_t)1u << hb;
    mask = (uint32_t)(slots - 1u);
    t = (lzmesh_h6_slot *)malloc(slots * sizeof *t);
    if (t == NULL)
        return 1;
    for (i = 0u; i < (uint32_t)slots; i++)
        t[i].pos = LZMESH_H6_EMPTY;
    lzmesh_h6_insert(t, mask, s, 0u);
    lzmesh_h6_insert(t, mask, s, 1u);
    for (j = 2u; j + 9u <= n; j++) {
        if (lzmesh_h6_query(t, mask, s, j)) {
            free(t);
            return 1;
        }
        lzmesh_h6_insert(t, mask, s, j);
    }
    free(t);
    return 0;
}
/* H6: per-block-fresh skip+rep0 parse (global guard P+9<=n, extend to
 * n; block-rel guard enforced by the cut checker). blk0 (P=1,litpos=1)
 * keeps G4; later blocks (P=9,litpos=0) are black-box pinned. Takes
 * are block-relative. *err on overflow/bad input. */
static size_t lzmesh_h6_parse(const uint8_t *s, size_t n, size_t off,
                              int first, uint32_t *tpos, uint32_t *tlen,
                              size_t cap, int *err) {
    size_t p, litpos, ntok;
    if (err == NULL)
        return 0u;
    *err = 0;
    if (s == NULL || tpos == NULL || tlen == NULL || cap == 0u)
        return 0u;
    if (off >= n || n > (size_t)LZMESH_U35M6_NMAX)
        return 0u;
    p = off + (first ? 1u : 9u);
    litpos = off + (first ? 1u : 0u);
    ntok = 0u;
    while (p < n) {
        if (p + 9u > n)
            break;
        if (s[p] == s[p - 1u] && s[p + 1u] == s[p]) {
            size_t ln = 2u;
            while (p + ln < n && s[p + ln] == s[p - 1u + ln])
                ln++;
            if (ntok >= cap || p - off > 0xFFFFFFFFu
                || ln > 0xFFFFFFFFu) {
                *err = 1;
                return 0u;
            }
            tpos[ntok] = (uint32_t)(p - off);
            tlen[ntok] = (uint32_t)ln;
            ntok++;
            p += ln;
            litpos = p;
        } else {
            p += (p - litpos) >> 8;
            p += 1u;
        }
    }
    return ntok;
}
typedef struct {
    uint32_t off;
    uint32_t ds;
    uint32_t nt;
} lzmesh_h6_blk;
/* H6: plan one block: parse + spend-cut + post-checks. 1 with *ds
 * (cut: spend==B in a run/take, or partial to n), *nt (used takes),
 * *lenc (incl terminator). Takes arrays out iff non-NULL (cap TOKCAP).
 * Declines take-overshoot, exact-cut, guard-zone/cross-cut takes,
 * lenc>10 (G4 stopgap), tiny last block. */
static int lzmesh_h6_plan_block(const uint8_t *s, size_t n, size_t off,
                                int first, uint32_t B, uint32_t *ds_out,
                                uint32_t *nt_out, uint32_t *lenc_out,
                                uint32_t *tpos_out, uint32_t *tlen_out) {
    uint32_t tpos[LZMESH_U35M6_TOKCAP], tlen[LZMESH_U35M6_TOKCAP];
    uint32_t cpos0 = first ? 1u : 0u;
    uint32_t spend = 0u, le = 0u, prev_end = 0u, used = 0u, ds = 0u;
    uint32_t le_end = 0u, i, trun, te;
    size_t nt, ti;
    int err = 0;
    uint8_t eb[5];
    if (s == NULL || ds_out == NULL || nt_out == NULL
        || lenc_out == NULL)
        return 0;
    if (off >= n || n > (size_t)LZMESH_U35M6_NMAX)
        return 0;
    nt = lzmesh_h6_parse(s, n, off, first, tpos, tlen,
                         (size_t)LZMESH_U35M6_TOKCAP, &err);
    if (err)
        return 0;
    for (ti = 0u; ti < nt; ti++) {
        uint32_t tp = tpos[ti], ml = tlen[ti];
        uint32_t gap, run, lf, mc, ms, e;
        unsigned a, b;
        if (tp < prev_end || ml < 2u)
            return 0;
        gap = tp - prev_end;
        if (prev_end == 0u) {
            if (gap < cpos0)
                return 0;
            run = gap - cpos0;
        } else {
            run = gap;
        }
        if (spend < B && spend + run >= B) {
            uint32_t need = B - spend;
            uint32_t base = prev_end + (prev_end == 0u ? cpos0 : 0u);
            ds = base + need;
            goto cut;
        }
        if (spend + run >= B)
            return 0; /* unreachable (spend<B invariant); defensive */
        spend += run;
        lf = run >= 3u ? 3u : run;
        mc = ml - 2u;
        ms = mc >= 7u ? 7u : mc;
        a = (lf == 3u) ? lzmesh_u7_len_escape_write(run - 3u, eb) : 0u;
        b = (ms == 7u) ? lzmesh_u7_len_escape_write(mc - 7u, eb) : 0u;
        if ((lf == 3u && (a == 0u || a > 5u))
            || (ms == 7u && (b == 0u || b > 5u)))
            return 0;
        e = a + b;
        le += e;
        spend += 1u + e;
        if (spend > B)
            return 0; /* take-overshoot: oracle shape unknown */
        used++;
        if (spend == B) {
            ds = tp + ml;
            goto cut;
        }
        prev_end = tp + ml;
        if (prev_end < tp)
            return 0;
    }
    {
        uint32_t span = (uint32_t)(n - off);
        uint32_t tail, need, base;
        if (prev_end > span)
            return 0;
        if (prev_end == 0u) {
            if (span < cpos0)
                return 0;
            tail = span - cpos0;
        } else {
            tail = span - prev_end;
        }
        if (spend < B && spend + tail >= B) {
            need = B - spend;
            base = prev_end + (prev_end == 0u ? cpos0 : 0u);
            ds = base + need;
            goto cut;
        }
        if (spend + tail >= B)
            return 0; /* unreachable; defensive */
        ds = span;
    }
cut:
    if (ds == 0u || ds > (uint32_t)LZMESH_U35M6_BMAX
        || (size_t)ds > n - off)
        return 0;
    if (used > 0u) {
        le_end = tpos[used - 1u] + tlen[used - 1u];
        if (le_end < tpos[used - 1u])
            return 0;
    }
    if (le_end > ds)
        return 0; /* cross-cut take */
    for (i = 0u; i < used; i++) {
        if (tpos[i] + 9u > ds)
            return 0; /* guard-zone take: scope unpinned */
    }
    if (le_end + cpos0 > ds)
        return 0;
    trun = ds - le_end - cpos0;
    if (trun == 0u && off + (size_t)ds < n)
        return 0; /* exact-cut: oracle shape unknown */
    te = (trun >= 3u) ? lzmesh_u7_len_escape_write(trun - 3u, eb) : 0u;
    if (trun >= 3u && (te == 0u || te > 5u))
        return 0;
    if (le + te > 10u)
        return 0; /* G4 stopgap per block: B-TAB (4,kk) open */
    if (off + (size_t)ds == n && ds <= 12u)
        return 0; /* tiny tail: absorb/RAW-tail unknown */
    if (tpos_out != NULL && tlen_out != NULL) {
        for (i = 0u; i < used; i++) {
            tpos_out[i] = tpos[i];
            tlen_out[i] = tlen[i];
        }
    }
    *ds_out = ds;
    *nt_out = used;
    *lenc_out = le + te;
    return 1;
}
/* I2: reset-walk region scan (LANE-I2; black-box pins tmp/i2/).
 * Same skip+rep0 query rule as h6_parse, but starts at caller state
 * (p0, lit0) and records the first visit at/after next_off (0 = none).
 * Takes with tp<ds are collected; takes at/after next_off are ignored
 * (next block re-walks). *err on overflow/cap (fail-safe decline). */
static size_t lzmesh_h6_walk_region(const uint8_t *s, size_t n, size_t off,
                                    uint32_t ds, size_t p0, size_t lit0,
                                    size_t next_off, uint32_t *tpos,
                                    uint32_t *tlen, size_t cap,
                                    size_t *cross_out, int *err) {
    size_t p = p0, litpos = lit0, ntok = 0u;
    if (err == NULL)
        return 0u;
    *err = 0;
    if (cross_out != NULL)
        *cross_out = 0u;
    if (s == NULL || tpos == NULL || tlen == NULL || cap == 0u)
        return 0u;
    if (off >= n || n > (size_t)LZMESH_U35M6_NMAX || p0 < off
        || lit0 < off || (size_t)ds > n - off)
        return 0u;
    while (p < n) {
        if (next_off > 0u && p >= next_off) {
            if (cross_out != NULL)
                *cross_out = p;
            break;
        }
        if (p + 9u > n)
            break;
        if (s[p] == s[p - 1u] && s[p + 1u] == s[p]) {
            size_t ln = 2u;
            while (p + ln < n && s[p + ln] == s[p - 1u + ln])
                ln++;
            if (p - off > 0xFFFFFFFFu || ln > 0xFFFFFFFFu) {
                *err = 1;
                return 0u;
            }
            if (p - off < (size_t)ds) {
                if (ntok >= cap) {
                    *err = 1;
                    return 0u;
                }
                tpos[ntok] = (uint32_t)(p - off);
                tlen[ntok] = (uint32_t)ln;
                ntok++;
            }
            p += ln;
            litpos = p;
            if (next_off > 0u && p >= next_off) {
                if (cross_out != NULL)
                    *cross_out = p;
                break;
            }
        } else {
            p += (p - litpos) >> 8;
            p += 1u;
        }
    }
    return ntok;
}
/* I2: fresh-vs-reset agreement gate (LANE-I2).
 * H6 parses every later block fresh at (off+9, litpos=off). Oracle pins
 * (20 probes, tmp/i2/): the skip-walk carries P across block cuts and
 * only litpos resets (P floors at off+9). Fresh finds takes the oracle
 * never visits (s15-n262144 blk5 rel20: fresh takes, oracle tok1).
 * Rule: re-walk each block from carried P (blk0: (1,1)) and require the
 * same takes as the fresh plan; else decline (fail-safe). s05/s14 agree
 * (blk1 floors to +9, identical walks); s15 disagrees (blk5) -> base. */
static int lzmesh_h6_reset_agree(const uint8_t *s, size_t n,
                                 const lzmesh_h6_blk *blks, size_t nblocks) {
    uint32_t ftpos[LZMESH_U35M6_TOKCAP], ftlen[LZMESH_U35M6_TOKCAP];
    uint32_t rtpos[LZMESH_U35M6_TOKCAP], rtlen[LZMESH_U35M6_TOKCAP];
    size_t j, pcross = 0u;
    if (s == NULL || blks == NULL || nblocks < 2u
        || nblocks > (size_t)LZMESH_U35M6_KCAP)
        return 0;
    for (j = 0u; j < nblocks; j++) {
        size_t off = (size_t)blks[j].off, p0, lit0, rnt, k;
        uint32_t B = (j == 0u) ? 16384u : (j == 1u ? 32768u : 62448u);
        uint32_t fds = 0u, fnt = 0u, fle = 0u;
        size_t nxt = (j + 1u < nblocks) ? (size_t)blks[j + 1u].off : 0u;
        int err = 0;
        if (!lzmesh_h6_plan_block(s, n, off, j == 0u, B, &fds, &fnt,
                                  &fle, ftpos, ftlen))
            return 0;
        if (fds != blks[j].ds || fnt != blks[j].nt)
            return 0;
        if (j == 0u) {
            p0 = 1u;
            lit0 = 1u;
        } else {
            if (pcross == 0u)
                return 0;
            p0 = pcross;
            lit0 = off;
            if (p0 < off + 9u)
                p0 = off + 9u;
        }
        rnt = lzmesh_h6_walk_region(s, n, off, blks[j].ds, p0, lit0,
                                    nxt, rtpos, rtlen,
                                    (size_t)LZMESH_U35M6_TOKCAP, &pcross,
                                    &err);
        if (err)
            return 0;
        if (nxt > 0u && pcross == 0u)
            return 0;
        if (rnt != (size_t)fnt)
            return 0;
        for (k = 0u; k < rnt; k++) {
            if (rtpos[k] != ftpos[k] || rtlen[k] != ftlen[k])
                return 0;
        }
    }
    return 1;
}
/* H6: per-block COMP probe (dst NULL) or write. Re-plans the block
 * (must agree with the want-plan) then frames G4-shape: lit-HUF via
 * stock u35_lengths on the lit-only buffer, tok/len RAW/REPEAT (never
 * HUF; non-alleq count>10 declines), dist RAW count0. Per-block TIER-1
 * + gates; no local TIER-2 (u36m R5 pattern). 1 with *fo_out (and
 * *wrote_out=fo+10 iff dst). */
static int lzmesh_h6_block(const uint8_t *s, size_t n, size_t off,
                           uint32_t bs, int first, uint32_t bi,
                           uint32_t exp_nt, uint32_t *fo_out, uint8_t *dst,
                           size_t dst_cap, size_t *wrote_out) {
    uint32_t tpos[LZMESH_U35M6_TOKCAP], tlen[LZMESH_U35M6_TOKCAP];
    uint32_t B = (bi == 0u) ? 16384u : (bi == 1u ? 32768u : 62448u);
    uint32_t cpos0 = first ? 1u : 0u;
    uint32_t ds = 0u, nt = 0u, lenc_ex = 0u;
    uint8_t lens[256], mlens[11], vals[256];
    uint16_t codes[256], mcodes[11];
    uint8_t tokbuf[LZMESH_U35M6_TOKCAP + 1u];
    uint8_t lenbuf[16];
    uint8_t idxbuf[24];
    uint8_t *litbuf;
    unsigned bitc[8], laneb[8], used = 0u, idxsz = 0u;
    uint64_t ctot = 0u;
    uint32_t litc, tokc, lenc, modes, bo, fo;
    uint32_t m_tok, m_len, tok_pay, len_pay, payload = 0u;
    uint64_t tot = 0u;
    size_t ti, li, si, nlc;
    uint32_t sumlen, i, k;
    unsigned start[8], pos[8];
    lzmesh_u35_acc uacc[8]; /* P6-W4 */
    uint8_t eb[5];
    if (s == NULL || fo_out == NULL)
        return 0;
    if (off >= n || (size_t)bs > n - off || bs < 4u
        || bs > (uint32_t)LZMESH_U35M6_BMAX)
        return 0;
    if (!lzmesh_h6_plan_block(s, n, off, first, B, &ds, &nt, &lenc_ex,
                              tpos, tlen))
        return 0;
    if (ds != bs || nt != exp_nt)
        return 0;
    litbuf = (uint8_t *)malloc(bs);
    if (litbuf == NULL)
        return 0;
    sumlen = 0u;
    li = 0u;
    ti = 0u;
    for (si = 0u; si < (size_t)bs; si++) {
        if (ti < (size_t)nt && si == (size_t)tpos[ti]) {
            uint32_t ml = tlen[ti];
            if (ml < 2u || si + (size_t)ml > (size_t)bs) {
                free(litbuf);
                return 0;
            }
            sumlen += ml;
            si += (size_t)ml - 1u;
            ti++;
        } else {
            litbuf[li++] = s[off + si];
        }
    }
    if (ti != (size_t)nt || li + (size_t)sumlen != (size_t)bs || li < 1u
        || li > 0xFFFFFFFFu) {
        free(litbuf);
        return 0;
    }
    litc = (uint32_t)li;
    if (litc < 8u) {
        free(litbuf);
        return 0; /* ordinal pads need every lane dealt a lit */
    }
    if (!lzmesh_u35_lengths(litbuf, (size_t)litc, lens, codes, mlens,
                            mcodes, vals, &used, bitc, &ctot,
                            LZMESH_U35M6_BMAX)) {
        free(litbuf);
        return 0;
    }
    tokc = nt + 1u;
    if (tokc > (uint32_t)LZMESH_U35M6_TOKCAP + 1u) {
        free(litbuf);
        return 0;
    }
    nlc = 0u;
    for (ti = 0u; ti < (size_t)nt; ti++) {
        uint32_t run, mc, lf, ms, pe;
        unsigned eb_n, ebi;
        if (ti == 0u) {
            if (tpos[0u] < cpos0) {
                free(litbuf);
                return 0;
            }
            run = tpos[0u] - cpos0;
        } else {
            pe = tpos[ti - 1u] + tlen[ti - 1u];
            if (tpos[ti] < pe) {
                free(litbuf);
                return 0;
            }
            run = tpos[ti] - pe;
        }
        if (tlen[ti] < 2u) {
            free(litbuf);
            return 0;
        }
        mc = tlen[ti] - 2u;
        lf = (run < 3u) ? run : 3u;
        ms = (mc < 7u) ? mc : 7u;
        tokbuf[ti] = lzmesh_u4_token((unsigned)lf, 0u, (unsigned)ms);
        if (lf == 3u) {
            if (run < 3u) {
                free(litbuf);
                return 0;
            }
            eb_n = lzmesh_u7_len_escape_write(run - 3u, eb);
            if (eb_n == 0u || eb_n > 5u
                || nlc + (size_t)eb_n > sizeof lenbuf) {
                free(litbuf);
                return 0;
            }
            for (ebi = 0u; ebi < eb_n; ebi++)
                lenbuf[nlc++] = eb[ebi];
        }
        if (ms == 7u) {
            if (mc < 7u) {
                free(litbuf);
                return 0;
            }
            eb_n = lzmesh_u7_len_escape_write(mc - 7u, eb);
            if (eb_n == 0u || eb_n > 5u
                || nlc + (size_t)eb_n > sizeof lenbuf) {
                free(litbuf);
                return 0;
            }
            for (ebi = 0u; ebi < eb_n; ebi++)
                lenbuf[nlc++] = eb[ebi];
        }
    }
    {
        uint32_t le2 = 0u, run, lf;
        unsigned eb_n, ebi;
        if (nt > 0u)
            le2 = tpos[nt - 1u] + tlen[nt - 1u];
        if (le2 + cpos0 > bs) {
            free(litbuf);
            return 0;
        }
        run = bs - le2 - cpos0;
        lf = (run < 3u) ? run : 3u;
        tokbuf[nt] = lzmesh_u4_token((unsigned)lf, 0u, 0u);
        if (lf == 3u) {
            if (run < 3u) {
                free(litbuf);
                return 0;
            }
            eb_n = lzmesh_u7_len_escape_write(run - 3u, eb);
            if (eb_n == 0u || eb_n > 5u
                || nlc + (size_t)eb_n > sizeof lenbuf) {
                free(litbuf);
                return 0;
            }
            for (ebi = 0u; ebi < eb_n; ebi++)
                lenbuf[nlc++] = eb[ebi];
        }
    }
    if (nlc > 0xFFFFFFFFu || (uint32_t)nlc != lenc_ex) {
        free(litbuf);
        return 0;
    }
    lenc = (uint32_t)nlc;
    {
        int tokeq = 1, leneq = 1;
        for (i = 1u; i < tokc; i++) {
            if (tokbuf[i] != tokbuf[0u]) {
                tokeq = 0;
                break;
            }
        }
        for (i = 1u; i < lenc; i++) {
            if (lenbuf[i] != lenbuf[0u]) {
                leneq = 0;
                break;
            }
        }
        if (tokeq) {
            m_tok = LZMESH_U4_MODE_REPEAT;
        } else if (tokc <= 10u) {
            m_tok = LZMESH_U4_MODE_RAW; /* S5.5 n<=10 */
        } else {
            free(litbuf);
            return 0; /* tok-HUF-needing: G1 owns */
        }
        if (lenc == 0u) {
            m_len = LZMESH_U4_MODE_RAW;
        } else if (leneq) {
            m_len = LZMESH_U4_MODE_REPEAT;
        } else if (lenc <= 10u) {
            m_len = LZMESH_U4_MODE_RAW; /* S5.5 n<=10 */
        } else {
            free(litbuf);
            return 0; /* len-HUF-needing: G1 owns */
        }
        modes = lzmesh_u4_modes_pack(m_tok, m_len,
                                     LZMESH_U4_MODE_HUFFMAN,
                                     LZMESH_U4_MODE_RAW);
    }
    tok_pay = (m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : tokc;
    if (lenc == 0u) {
        len_pay = 0u;
    } else {
        len_pay = (m_len == LZMESH_U4_MODE_REPEAT) ? 1u : lenc;
    }
    if (9u + tok_pay + len_pay > 0xFFFFu) {
        free(litbuf);
        return 0;
    }
    bo = 9u + tok_pay + len_pay;
    for (k = 0u; k < 8u; k++) {
        unsigned c = bitc[k], L;
        if (c == 0u) {
            free(litbuf);
            return 0;
        }
        tot += (uint64_t)c;
        L = (c + 7u) / 8u;
        laneb[k] = L;
        payload += L;
    }
    if (tot >= (uint64_t)8u * (uint64_t)litc) {
        free(litbuf);
        return 0; /* S5.5 rollback */
    }
    idxsz = lzmesh_pack1_index_pack(laneb, idxbuf,
                                    (unsigned)sizeof idxbuf);
    if (idxsz == 0u) {
        free(litbuf);
        return 0;
    }
    if ((uint64_t)bo + (uint64_t)payload + (uint64_t)idxsz > 0xFFFFu) {
        free(litbuf);
        return 0;
    }
    fo = bo + payload + idxsz;
    if (fo > 0xFFFFu) {
        free(litbuf);
        return 0;
    }
    if (!lzmesh_u4_comp_gates_ok(bs, bo, fo)) {
        free(litbuf);
        return 0;
    }
    if (!lzmesh_u3_tier1_comp(bs, fo)) {
        free(litbuf);
        return 0;
    }
    *fo_out = fo;
    if (dst == NULL) {
        free(litbuf);
        return 1;
    }
    if (dst_cap < (size_t)fo + 10u) {
        free(litbuf);
        return 0;
    }
    {
        size_t s2 = 9u;
        lzmesh_u7_comp_header_emit(dst, bs, bo, fo);
        if (m_tok == LZMESH_U4_MODE_REPEAT) {
            dst[s2++] = tokbuf[0u];
        } else {
            for (i = 0u; i < tokc; i++)
                dst[s2++] = tokbuf[i];
        }
        if (lenc == 0u) {
            if (m_len != LZMESH_U4_MODE_RAW) {
                free(litbuf);
                return 0;
            }
        } else if (m_len == LZMESH_U4_MODE_REPEAT) {
            dst[s2++] = lenbuf[0u];
        } else {
            for (i = 0u; i < lenc; i++)
                dst[s2++] = lenbuf[i];
        }
        if (s2 != (size_t)bo) {
            free(litbuf);
            return 0;
        }
        for (k = 0u; k < 8u; k++) {
            start[k] = (unsigned)(s2 - (size_t)bo);
            s2 += (size_t)laneb[k];
            pos[k] = 0u;
            uacc[k].acc = 0u; /* P6-W4 */
            uacc[k].nbits = 0u;
            uacc[k].out = dst + (size_t)bo + (size_t)start[k];
        }
        for (i = 0u; i < payload; i++)
            dst[(size_t)bo + (size_t)i] = 0u;
        for (i = 0u; i < 11u; i++)
            lzmesh_u35_acc_put(&uacc[0],
                               &pos[0], (unsigned)mlens[i], 3u);
        {
            uint32_t bm = 0u;
            unsigned g, j;
            for (g = 0u; g < 32u; g++) {
                for (j = 0u; j < 8u; j++) {
                    if (lens[g * 8u + j] != 0u) {
                        bm |= (uint32_t)1u << g;
                        break;
                    }
                }
            }
            if (bm == 0u) {
                free(litbuf);
                return 0;
            }
            lzmesh_u35_acc_put(&uacc[0], &pos[0], bm, 32u);
        }
        for (i = 0u; i < used; i++)
            lzmesh_u35_acc_put(&uacc[i & 7u],
                               &pos[i & 7u],
                               (unsigned)mcodes[vals[i]],
                               (unsigned)mlens[vals[i]]);
        for (i = 0u; i < litc; i++)
            lzmesh_u35_acc_put(&uacc[i & 7u],
                               &pos[i & 7u], (unsigned)codes[litbuf[i]],
                               (unsigned)lens[litbuf[i]]);
        for (k = 0u; k < 8u; k++)
            lzmesh_u35_acc_flush(&uacc[k]); /* P6-W4: drain pre-guard */
        for (k = 0u; k < 8u; k++) {
            if (pos[k] != bitc[k]) {
                free(litbuf);
                return 0;
            }
        }
        for (k = 0u; k < 8u; k++) {
            unsigned m = bitc[k] & 7u, kk = (8u - m) & 7u;
            /* H6: ordinal lastL (last dealt lit per lane; s14 fires).
             * G4-R5 left pos-vs-ordinal open (P==0 both on G4-13);
             * takes shift dealing, ordinal fits 16/16 (tmp/h6/padfit). */
            if (kk != 0u && litc > k) {
                uint32_t li2 = (uint32_t)(((litc - 1u - k) >> 3) * 8u
                    + k);
                unsigned lastL = (unsigned)lens[litbuf[li2]];
                unsigned P = lzmesh_pack1_btab(lastL, kk);
                if (P != 0u) {
                    uint8_t *lb = dst + (size_t)bo
                        + (size_t)start[k] + (size_t)laneb[k] - 1u;
                    *lb = (uint8_t)(*lb | ((P << m) & 0xffu));
                }
            }
        }
        for (i = 0u; i < idxsz; i++)
            dst[s2++] = idxbuf[i];
        if (s2 != (size_t)fo) {
            free(litbuf);
            return 0;
        }
        lzmesh_u4_footer_emit(dst + (size_t)fo, modes, tokc, lenc,
                              litc, 0u);
        free(litbuf);
        if (wrote_out != NULL)
            *wrote_out = (size_t)fo + 10u;
        return 1;
    }
}
int lzmesh_u35m6_want(const uint8_t *src, size_t size, int level) {
    lzmesh_h6_blk blks[LZMESH_U35M6_KCAP];
    size_t off = 0u, outpos = 0u;
    unsigned bi = 0u, i;
    int any = 0;
    if (level != 1 || src == NULL)
        return 0;
    if (size <= (size_t)LZMESH_U35_NCAP
        || size > (size_t)LZMESH_U35M6_NMAX
        || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lzmesh_u9_is_run(src, size))
        return 0;
    if (lzmesh_u12_period(src, size) != 0)
        return 0;
    if (lzmesh_u18_period(src, size) != 0)
        return 0;
    if (lzmesh_h6_has_fresh6(src, size))
        return 0;
    while (off < size) {
        uint32_t B = (bi == 0u) ? 16384u
            : (bi == 1u ? 32768u : 62448u);
        uint32_t ds = 0u, nt = 0u, le = 0u;
        if (bi >= (unsigned)LZMESH_U35M6_KCAP)
            return 0;
        if (!lzmesh_h6_plan_block(src, size, off, bi == 0u, B, &ds,
                                  &nt, &le, NULL, NULL))
            return 0;
        blks[bi].off = (uint32_t)off;
        blks[bi].ds = ds;
        blks[bi].nt = nt;
        if (nt > 0u)
            any = 1;
        off += (size_t)ds;
        bi++;
    }
    if (bi < 2u || !any)
        return 0;
    /* I2: decline where fresh takes disagree with the carried-P walk. */
    if (!lzmesh_h6_reset_agree(src, size, blks, bi))
        return 0;
    for (i = 0u; i < bi; i++) {
        uint32_t fo = 0u;
        if (!lzmesh_h6_block(src, size, (size_t)blks[i].off,
                             blks[i].ds, i == 0u, i, blks[i].nt, &fo,
                             NULL, 0u, NULL))
            return 0;
        outpos += (size_t)fo + 10u;
    }
    return lzmesh_u3_tier2_keep(outpos, size);
}
size_t lzmesh_u35m6_emit(uint8_t *dst, size_t dst_capacity,
                         const uint8_t *src, size_t size, int level) {
    lzmesh_h6_blk blks[LZMESH_U35M6_KCAP];
    size_t off = 0u, s2 = 0u, need = 1u;
    unsigned bi = 0u, i;
    int any = 0;
    if (dst == NULL || src == NULL)
        return 0u;
    if (level != 1)
        return 0u;
    if (size <= (size_t)LZMESH_U35_NCAP
        || size > (size_t)LZMESH_U35M6_NMAX
        || size > (size_t)LZMESH_U1_DS_MAX)
        return 0u;
    if (lzmesh_u9_is_run(src, size))
        return 0;
    if (lzmesh_u12_period(src, size) != 0)
        return 0;
    if (lzmesh_u18_period(src, size) != 0)
        return 0;
    if (lzmesh_h6_has_fresh6(src, size))
        return 0;
    while (off < size) {
        uint32_t B = (bi == 0u) ? 16384u
            : (bi == 1u ? 32768u : 62448u);
        uint32_t ds = 0u, nt = 0u, le = 0u;
        if (bi >= (unsigned)LZMESH_U35M6_KCAP)
            return 0u;
        if (!lzmesh_h6_plan_block(src, size, off, bi == 0u, B, &ds,
                                  &nt, &le, NULL, NULL))
            return 0u;
        blks[bi].off = (uint32_t)off;
        blks[bi].ds = ds;
        blks[bi].nt = nt;
        if (nt > 0u)
            any = 1;
        off += (size_t)ds;
        bi++;
    }
    if (bi < 2u || !any)
        return 0u;
    /* I2: decline where fresh takes disagree with the carried-P walk. */
    if (!lzmesh_h6_reset_agree(src, size, blks, bi))
        return 0u;
    for (i = 0u; i < bi; i++) {
        uint32_t fo = 0u;
        if (!lzmesh_h6_block(src, size, (size_t)blks[i].off,
                             blks[i].ds, i == 0u, i, blks[i].nt, &fo,
                             NULL, 0u, NULL))
            return 0u;
        need += (size_t)fo + 10u;
    }
    if (need - 1u > size)
        return 0u; /* global TIER-2 re-check (never diverges) */
    if (dst_capacity < need)
        return 0u;
    for (i = 0u; i < bi; i++) {
        uint32_t fo = 0u;
        size_t w = 0u;
        if (!lzmesh_h6_block(src, size, (size_t)blks[i].off,
                             blks[i].ds, i == 0u, i, blks[i].nt, &fo,
                             dst + s2, need - s2, &w))
            return 0u;
        if (w == 0u || w != (size_t)fo + 10u)
            return 0u;
        s2 += w;
    }
    dst[s2++] = (uint8_t)LZMESH_U1_TAG_END;
    return s2 == need ? need : 0u;
}
/* === general LZ single-COMP, level 5 only (owner: u37) === */
/* GEN5 closes the e05 full-tier RAW gap (p_len==n+6 on general inputs):
 * match finder (L1 hash-chain 3-byte; L5/L9 G6 hashed single-slot
 * 7B/5B/3B strict cascade, LANE-E4 handoff) + greedy parse with a
 * 1-step score-lazy leg (u3_score cost duel, ties stay) + single COMP
 * block via the in-tree token emitters (u7 rep/new tokens, escape
 * codec, dist_split; u4 modes/footer; pack1 index pack). No Huffman:
 * streams are RAW (REPEAT iff all-equal, HUF->RAW fallback), suffix
 * lanes carry only new-dist suffix bits with zero pads. Byte-identity
 * with the oracle is NOT claimed (S12 parked); decode-correctness is.
 * Take guidance applied: CR-H3 loss-Ds (L3 17050 / L4 16390 / L5 16389
 * decline), level floors fresh 3 / rep 2 (L1 fresh 6 / rep 2; E3
 * oracle-proven token beds), rep short-circuit (any rep hit takes
 * immediately, lowest slot wins at any length, hash never consulted),
 * tiered most-recent hash pick (first extends>=7, else >=5, else
 * >=floor; L1 single-tier most-recent >=6; hash path always codes
 * NEW). Recents mirror
 * the DECODER exactly (new = plain shift-push, rep = move-to-front;
 * NOT u4 dedup-update) so multi-token rep coding stays valid.
 * Single-block only (n<=65535: u16 bo/fo + counts); bigger inputs
 * decline (multi-COMP chaining open). Lowest want priority (after all
 * frozen shapes); L5-only this pass (L1/L9 keep prior behaviour).
 * Gates: counts_ok + scratch_ok + comp_gates + comp_keep (TIER-1/2,
 * no size compare) + B1-misfire guard + fo-u16 mirror the decoder.
 * Fallback: when the LZ parse declines, emit the PROVEN u36/u36m
 * litonly-HUF bytes (level-agnostic grammar; same bytes L0 emits,
 * reused verbatim) iff their want passes: single-block (n<=16393)
 * then multi-block (n<=DS_MAX). Covers entropy-only inputs (k=16
 * alphabet) the no-H LZ path cannot TIER-2. Ownership declines gate
 * the fallback too (frozen vetoes stay RAW).
 * PERF: O(n) inserts + chain probes (L1 MX, J1-uncapped) / O(1)
 * slot probes (L5/L9) + extends; mallocs head 2^hb + prev n (L1,
 * hb<=18) / ~6M slots (L5/L9) per call (freed); deterministic. */
extern void *malloc(size_t n);
extern void free(void *p);

#define LZMESH_U37_NCAP 65535u
/* S2: L1 = MX-slot chains (head 2^hb + prev n; HINT-FINDER-R2 sec2 lane
 * map on J1-chain structure + D3 shadow-stop). Old fixed-2^15 port-hash
 * chain macros retired. */
#define LZMESH_U37_MINREP 2u
#define LZMESH_S2_MXLOAD 8u /* memo 8B load (6B effective) */
#define LZMESH_S2_MXHEAD 6u /* memo 6B verify, BedD edge 5|6 */
/* E3: fresh floor is level-aware (L1 6, else 3); rep floor 2 all
 * levels. Slot1/2 rep probes keep 4B heads (oracle-proven: len-3 at
 * a rep dist codes NEW, E3 P9E). */
#define LZMESH_U37_LOSSD_L3 4096u /* N1: CR-H3-03 (17050, e00-bedded) does
                                 * not transfer to u37; bedded 4096 exact
                                 * (LANE-N1). Hash-NEW only (reps bypass). */
#define LZMESH_U37_LOSSD_L4 65536u /* O2: CR-H3-01 (16390, e00-bedded)
                                 * does not transfer to u37; bedded 65536
                                 * EXACT (65535 TAKE / 65536 LIT, e05+e09;
                                 * LANE-O2). Hash-NEW only (reps bypass). */
#define LZMESH_U37_LOSSD_L5 1048576u /* O2: CR-H3-02 (16389, e00-bedded)
                                 * does not transfer to u37; bedded 1048576
                                 * EXACT (1048575 TAKE / 1048576 LIT, e05+e09;
                                 * LANE-O2). Hash-NEW only (reps bypass). */
#define LZMESH_U37_LITREPMAX 62880u /* S4.2 REPEAT-lit ceiling */
/* C3 rollback gate (B4-REVERSE, LANE-C3): oracle e05 bails to STORE where
 * the u36-litonly fallback still emits COMP (poor-parse Huffman at
 * small n). LITFLOOR: fallback at e05 needs n>=80 (first litonly
 * agree-COMP at n=80; oracle e05 COMP below is always GEN/shapes).
 * GEN-path rows stay for C2: no port-parse est separates them
 * (same-parse split pairs, e.g. k2n60 COMP vs s24n60 STORE; periodic
 * agree-COMP shares every port-parse statistic with random REV). */
#define LZMESH_C3_LITFLOOR 74u

typedef struct {
    uint32_t litrun;
    uint32_t mlen;
    uint32_t dist;
    unsigned is_new;
    unsigned slot;
} lzmesh_u37_tok;

/* S2: u37_hash3 chain hash retired (MX lane; HINT-FINDER-R2 sec2). */

/* G6 hashed single-slot cascade (owner: G6; LANE-F1 sec5 residue,
 * LANE-F4 R1; supersedes F1 exact-key tables, same cascade shape).
 * L5/L9 hash leg: direct-mapped last-writer slots with oracle hash
 * geometry (black-box pinned tmp/g6/, 43/43 targeted cells):
 * big 2^hb u32 shared by h1(7B)+h2(5B), small 2^12 u32 for h3(3B).
 * h1(w)=(w*C1)>>(64-hb), h2(w)=(w*C2)>>(64-hb),
 * h3(w)=(w*C3)>>20, hb=min(cap,floor(log2(size+1))+5), cap
 * 18 (L5) / 21 (L9). Strict cascade 7B else 5B else 3B; empty /
 * out-of-range / head-mismatch = miss-continue; winner loss_ok-fail
 * = miss, no fallthrough (E4 strict kept). Dense inserts (E4:
 * thinning unverified, not implemented). Shadowed pos emits LIT via
 * the u3 lazy leg (E4 P1: 7B-B-hit len7 loses to n1 29@42).
 * Collided pos misses all tiers via head-mismatch (G6 beds: oracle
 * LIT@Q, exact-key port took). L1 keeps the chain path (single-tier
 * >=6 geometry unverified; bit-identical). Reuses u2 hash funcs +
 * consts (same file, geometry only; economics stay u37 loss_ok). */
/* G6 store: small[h3(3B)] then big[h2(5B)] then big[h1(7B)] (u2
 * order: h1 wins same-pos same-slot ties; pos identical either way).
 * Tables are caller-owned u32 arrays (big 2^hb, small 2^12), EMPTY
 * filled. Dense: every visited pos stored (thinning unverified). */
static void lzmesh_u37_g6_store(uint32_t *big, uint32_t *small,
                                const uint8_t *src, size_t size, size_t ins,
                                unsigned hb) {
    /* P11-WINS: one u64 load feeds all three tiers (LE low bytes equal
     * load_n 3/5/7 bit-for-bit); tail keeps the stock guarded loads. */
    if (ins + 8u <= size) {
        uint64_t w8 = lzmesh_wl_ld64(src + ins);
        small[lzmesh_u2_h3((uint32_t)(w8 & 0xFFFFFFu))] = (uint32_t)ins;
        big[lzmesh_u2_h2(w8 & 0xFFFFFFFFFFull, hb)] = (uint32_t)ins;
        big[lzmesh_u2_h1(w8 & 0xFFFFFFFFFFFFFFull, hb)] = (uint32_t)ins;
        return;
    }
    if (ins + 3u <= size)
        small[lzmesh_u2_h3(
            (uint32_t)lzmesh_u2_load_n(src + ins, 3u))] = (uint32_t)ins;
    if (ins + 5u <= size)
        big[lzmesh_u2_h2(lzmesh_u2_load_n(src + ins, 5u),
                         hb)] = (uint32_t)ins;
    if (ins + 7u <= size)
        big[lzmesh_u2_h1(lzmesh_u2_load_n(src + ins, 7u),
                         hb)] = (uint32_t)ins;
}

/* H1 big-only store (lit-gap windows never touch small; probeO3 12/12
 * e05 + 4/4 e09: skipped pos absent from small). */
static void lzmesh_u37_g6_store_big(uint32_t *big,
                                    const uint8_t *src, size_t size, size_t ins,
                                    unsigned hb) {
    /* P11-WINS: fused u64 (see g6_store). */
    if (ins + 8u <= size) {
        uint64_t w8 = lzmesh_wl_ld64(src + ins);
        big[lzmesh_u2_h2(w8 & 0xFFFFFFFFFFull, hb)] = (uint32_t)ins;
        big[lzmesh_u2_h1(w8 & 0xFFFFFFFFFFFFFFull, hb)] = (uint32_t)ins;
        return;
    }
    if (ins + 5u <= size)
        big[lzmesh_u2_h2(lzmesh_u2_load_n(src + ins, 5u),
                         hb)] = (uint32_t)ins;
    if (ins + 7u <= size)
        big[lzmesh_u2_h1(lzmesh_u2_load_n(src + ins, 7u),
                         hb)] = (uint32_t)ins;
}

/* S2 MX chain link (L1 only): all writers kept, newest-first via
 * head[slot]/prev[pos]. Tail guard ins+8 (memo 8B load; tail-store
 * shape unbedded micro-open, filed LANE-S2). */
static void lzmesh_s2_mx_link(int32_t *head, int32_t *prev,
                              const uint8_t *src, size_t size, size_t ins,
                              unsigned hb) {
    if (ins + LZMESH_S2_MXLOAD <= size) {
        uint32_t s = lzmesh_s2_hx(
            lzmesh_u2_load_n(src + ins, LZMESH_S2_MXLOAD), hb);
        prev[ins] = head[s];
        head[s] = (int32_t)ins;
    }
}

/* R8-MIX1NEUTRAL try-1: mx_link_ng fwd-decl; body at EOF (E2 verbatim;
 * stays always_inline: inlines into the out-of-line nowin helper). */
static inline __attribute__((always_inline)) void
lzmesh_s2_mx_link_ng(int32_t *head, int32_t *prev,
                     const uint8_t *src, size_t size, size_t ins,
                     unsigned hb);

/* === N1 e05 take-loss split (owner: N1; LANE-N1) ===
 * Bedded black-box (tmp/n1/n1bedN.py, n1bedR2.py; e05+e01+e09):
 * rep takes are UNCAPPED (R2/R3/R4/R5 take to 200K all slots/levels;
 * 12 rescore-m e05 cells: port's CR-H3 caps reject R0/R1 L3-5@d17-28K
 * the oracle takes). Hash-NEW L3 cap is 4096 EXACT (4095 take / 4096
 * lit, 3 seeds + sweep; HINT-BYTEEXACT-R1 sec2 "3 under 4K" CONFIRMED
 * for u37; CR-H3-03's 17050 was bedded on e00 COMP-bed (u36 path) and
 * does not transfer to u37 levels). Hash L4/L5 caps kept (bedded take
 * thru 28K = under-take vs oracle, but zero corpus divs: unverifiable,
 * left for a lane with a proving cell). is_rep=1 bypasses all caps.
 * O2 (LANE-O2): N1's proving cells arrived (6 rescore-n e05 EVO-forward
 * divs: oracle NEW L4@d17-49K + L5@d22608, port loss-rejects; white-box
 * slot holds exact oracle take). L4 = 65536 EXACT, L5 = 1048576 EXACT
 * (hint sec2 "4 under 64K / 5 under 1M" CONFIRMED, e05+e09 bedded). */
/* === P3 table-driven NEW filter (owner: P3; LANE-P3) ===
 * HINT-FINDER-R1 sec6 CONFIRMED exact power-of-2 edges, e05/e09 shared:
 * L3 take d<=4095 skip d>=4096 (3 seeds), L4 65535|65536 (2-3 seeds),
 * L5 1048575|1048576 (2 seeds), L6+ unbounded (takes @40000, 12/12).
 * e01 hard min-6 everywhere (30/30 skip L3/4/5 at d=3000/20000; no
 * short path; L1 big-only single-hash). e09 shorts = e05 shape.
 * Table shape is 7 u32 per-length distance caps (28 bytes, our table;
 * length classes + edges are oracle-observed behavior, contents
 * behavior-derived from black-box probing, never transcribed). idx =
 * min(len,6); reject iff dist >= cap (strict <). idx0-2 cap 0 =
 * reject (unreachable: every caller floors len>=3/5/6/7 first;
 * documents the no-short-path rule). idx6 = 1000000001 = first-skip
 * dist (Q4: take d<=1e9 / skip d>=1e9+1 EXACT, ANSWER-P3-1, 2 seeds/
 * side L6/L7/L8/L20 x e05/e09 + e01 spot; d-cap not n-cap).
 * Reps bypass (N1/N2). Byte-identical to the
 * N1/O2 if-chain on all reachable (len,dist). */
static const uint32_t lzmesh_p3_filt_maxd[7] = { 0u, 0u, 0u,
    LZMESH_U37_LOSSD_L3, LZMESH_U37_LOSSD_L4, LZMESH_U37_LOSSD_L5,
    1000000001u };

static int lzmesh_u37_loss_ok(uint32_t len, uint32_t dist, int is_rep) {
    unsigned idx;
    if (is_rep)
        return 1;
    idx = len < 6u ? len : 6u;
    return dist < lzmesh_p3_filt_maxd[idx];
}

/* P29-MF A1: per-query path is call-free (MSH-shape: hot legs inline,
 * extend inlined at probe sites; zero behavior change). */
static inline __attribute__((always_inline)) uint32_t
lzmesh_u37_extend_scalar(const uint8_t *src, size_t size,
                                         size_t pos, size_t q,
                                         uint32_t need) {
    uint32_t len = need;
    uint32_t max = (uint32_t)(size - pos);
    while (len < max && src[pos + len] == src[q + len])
        len++;
    return len;
}

#if defined(__ARM_NEON)
/* P3-N1: 16B vector extend (zlib-ng compare256_neon shape, intrinsics).
 * Contract: identical return to scalar for all (src,size,pos,q,need).
 * No-overrun proof: vector loads issue only while max-len >= 16, so the
 * pos-side read window [pos+len,pos+len+16) ends at pos+len+16 <=
 * pos+max = size. All callers pass q < pos, so the q-side window
 * [q+len,q+len+16) ends strictly below size. size < 16 (max < 16) never
 * enters the loop: pure scalar tail. need >= max returns need unread,
 * exactly like scalar. */
static inline __attribute__((always_inline)) uint32_t
lzmesh_u37_extend_neon(const uint8_t *src, size_t size,
                                       size_t pos, size_t q, uint32_t need) {
    uint32_t len = need;
    uint32_t max = (uint32_t)(size - pos);
    if (len >= max)
        return len;
    while (max - len >= 16u) {
        uint8x16_t a = vld1q_u8(src + pos + len);
        uint8x16_t b = vld1q_u8(src + q + len);
        uint64x2_t e64 = vreinterpretq_u64_u8(vceqq_u8(a, b));
        uint64_t lo = vgetq_lane_u64(e64, 0);
        uint64_t hi = vgetq_lane_u64(e64, 1);
        if (lo == 0xFFFFFFFFFFFFFFFFull && hi == 0xFFFFFFFFFFFFFFFFull) {
            len += 16u;
            continue;
        }
        /* First diff byte: ctz over inverted lanes (equal byte = 0xFF). */
        if (lo != 0xFFFFFFFFFFFFFFFFull)
            return len + (uint32_t)(__builtin_ctzll(~lo) >> 3);
        return len + 8u + (uint32_t)(__builtin_ctzll(~hi) >> 3);
    }
    while (len < max && src[pos + len] == src[q + len])
        len++;
    return len;
}
#endif

/* P3-N1: scalar force hook. LZMESH_SCALAR=1 (runtime, cached read-once per
 * P2-GETENV pattern) or -DLZMESH_SCALAR (compile time) selects scalar. */
/* R2-ENC9 A4 DRAIN-FUSE: fuse the 7 remaining executed calls in u37_parse
 * (MSH-shape: hot path call-free; linked -O2 census: i5_tr x6 / i4_flush
 * x5 / t4_drain x3 / scalar x3 / svisg x2 / catchup x2 / mf_head_eq x1
 * cold-tail). Pure force-inline, zero behavior change by construction. */
static inline __attribute__((always_inline)) int lzmesh_p3_scalar_on(void) {
#ifdef LZMESH_SCALAR
    return 1;
#else
    static int init = 0, on = 0;
    if (!init) {
        const char *e = getenv("LZMESH_SCALAR");
        init = 1;
        on = (e != NULL && e[0] != '\0' && atoi(e) != 0);
    }
    return on;
#endif
}

static inline __attribute__((always_inline)) uint32_t
lzmesh_u37_extend(const uint8_t *src, size_t size,
                  size_t pos, size_t q, uint32_t need) {
#if defined(__ARM_NEON)
    if (!lzmesh_p3_scalar_on())
        return lzmesh_u37_extend_neon(src, size, pos, q, need);
#else
    (void)lzmesh_p3_scalar_on;
#endif
    return lzmesh_u37_extend_scalar(src, size, pos, q, need);
}

/* Best match at pos (E3 oracle-proven order, token beds P1-P13):
 * rep probes first (heads 2/4/4): lowest hitting slot takes
 * immediately at any length >= 2, hash never consulted. Else hash
 * (NEW): L1 S2 MX-slot chains (6B verify, extends>=6, shadow-stop);
 * L5/L9 G6 cascade (7B else 5B else 3B).
 * No length duel, no dist duel. Hash path always codes NEW (rep
 * coding only via rep-probe hit: len-3 at a rep dist codes NEW).
 * L1 probes rep0 only (1-deep recents, E3 P4D: second recent codes
 * NEW). History [0,pos) must be inserted. Returns 1 with
 * blen/bdist set iff a floor-passing match exists; *is_rep says
 * rep (1) or new. */
/* C2a TRAIL-GATE shared by rep + hash legs (black-box: k-ladder T=9,
 * suffix-ladder s=4, L-ladder T(3)=6, ABCDEFG-new-dist; HINT-BYTEEXACT
 * sec4 "n-9 last eligible start" + "loop guard pos+9 vs len"): matches
 * starting past size-9 are ineligible (trailing lits + terminator
 * cover the tail). */
static int lzmesh_u37_elig(size_t pos, size_t size, int relax) {
    if (pos + LZMESH_U37_MINREP > size)
        return 0;
    /* T2 (LANE-T2): +9 tail bar kept for CUR probes (load-bearing:
     * blanket removal regresses 98 e05 cells where the oracle goes
     * lit+term in the last 8 bytes, e.g. s00-n255 NEW-5@250
     * skipped). Challenger peeks (+1/+2, L5 only, relax=1) see past
     * it: oracle skips ahead to end-zone matches there (s15-n53
     * NEW-8@45/53 via +1, s14-n53 rep-7@45/53 via +2). Width safety
     * for relaxed peeks comes from each caller's own guard (rep
     * 2/4, MX 8, slot 7/5/3). */
    if (!relax && pos + 9u > size)
        return 0;
    return 1;
}

/* P29-MF A1: no-compare head gate (MSH-shape probe: tag-gate then extend).
 * head_eq(a,b,n) over LE bytes <=> (((u64(a)^u64(b)) & mask(n))==0).
 * Needs 8 readable bytes both sides (caller-established); else the stock
 * byte-width fallback (identical loads to head_eq, no new OOB). */
/* R5-QBR: force-inline (2 outline sites sat in u37_parse; pure). */
static inline __attribute__((always_inline)) int
lzmesh_mf_head_eq(const uint8_t *a, uint64_t wa, int wa_ok,
                             const uint8_t *b, int wb_ok, unsigned n) {
    static const uint64_t mf_masks[9] = { 0ull, 0xFFull, 0xFFFFull,
        0xFFFFFFull, 0xFFFFFFFFull, 0xFFFFFFFFFFull, 0xFFFFFFFFFFFFull,
        0xFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
    if (n <= 8u && wa_ok && wb_ok)
        return (((wa ^ lzmesh_wl_ld64(b)) & mf_masks[n]) == 0u);
    return lzmesh_u2_head_eq(a, b, n);
}

/* R5-QBR head_eq_r0: fast path only. Exact under the r0 contract
 * (non-relaxed elig: pos+9<=size; qq<pos; n<=8): wa_ok holds
 * (pos+8<=size) and wb_ok holds (qq+8<=pos+7<size), so stock always
 * takes its fast arm with identical inputs. Callers prove the contract. */
static inline __attribute__((always_inline)) int
lzmesh_mf_head_eq_r0(uint64_t wa, const uint8_t *b, unsigned n) {
    static const uint64_t mf_masks[9] = { 0ull, 0xFFull, 0xFFFFull,
        0xFFFFFFull, 0xFFFFFFFFull, 0xFFFFFFFFFFull, 0xFFFFFFFFFFFFull,
        0xFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
    return (((wa ^ lzmesh_wl_ld64(b)) & mf_masks[n]) == 0u);
}

/* Lowest-slot rep hit among the first nrep recents (heads 2/4/4,
 * floor 2). Returns 1 with blen/bdist set.
 * N2 (LANE-N2): L5 rep path is loss-exempt (loss caps are NEW-path
 * only; CR-H3 beds are NEW-shaped, hint sec4 never caps reps).
 * Bedded: oracle takes R0/R1 L3/L4/L5 at cap+1 on all three lens
 * (39 builds: R0 sweep 37 + R1R2 shape 2; unanimous) while port
 * declines-then-takes-shorter. 11 real cells (7 R1/R2 + 3 R0/R1 +
 * 1 R0/NEW) + s14 challenger-leg duel (8<18 strict) ride this.
 * L1/L9 keep loss_ok (unbedded there; N2 owns e05 slots). */
static inline __attribute__((always_inline)) int
lzmesh_u37_rep_best(const uint8_t *src, size_t size, size_t pos,
                               const uint32_t recent[4], unsigned nrep,
                               uint32_t *blen, uint32_t *bdist, int level,
                               int relax) {
    unsigned j;
    uint64_t mf_w8 = 0u;
    int mf_fused;
    (void)level; /* P29-MF A1: rep loss_ok(ln,r,1) is identically 1
                  * (u37_loss_ok is_rep short-circuit); call dropped. */
    /* P29-MF A1: elig inlined (same two predicates as u37_elig). */
    if (pos + LZMESH_U37_MINREP > size)
        return 0;
    if (!relax && pos + 9u > size)
        return 0;
    mf_fused = (pos + 8u <= size);
    if (mf_fused)
        mf_w8 = lzmesh_wl_ld64(src + pos);
    for (j = 0u; j < nrep; j++) {
        uint32_t r = recent[j];
        uint32_t need = (j == 0u) ? 2u : 4u;
        uint32_t ln;
        size_t qq;
        if (r == 0u || (size_t)r > pos)
            continue;
        if (pos + need > size)
            continue;
        qq = pos - (size_t)r;
        if (!lzmesh_mf_head_eq(src + pos, mf_w8, mf_fused, src + qq,
                               qq + 8u <= size, need))
            continue;
        ln = lzmesh_u37_extend(src, size, pos, qq, need);
        if (ln < LZMESH_U37_MINREP)
            continue;
        *blen = ln;
        *bdist = r;
        return 1;
    }
    return 0;
}

/* R8-L9NEW R-d: one rep probe (r0/r1 bodies below; first-hit-wins
 * order kept by the caller sequence). */
static inline __attribute__((always_inline)) int
lzmesh_r8_rep_probe(const uint8_t *src, size_t size, size_t pos,
                    uint64_t mf_w8, uint32_t r, uint32_t need,
                    uint32_t *blen, uint32_t *bdist) {
    uint32_t ln;
    size_t qq;
    if ((uint64_t)r - 1u >= (uint64_t)pos)
        return 0;
    qq = pos - (size_t)r;
    if (!lzmesh_mf_head_eq_r0(mf_w8, src + qq, need))
        return 0;
    ln = lzmesh_u37_extend(src, size, pos, qq, need);
    if (ln < LZMESH_U37_MINREP)
        return 0;
    *blen = ln;
    *bdist = r;
    return 1;
}

/* R5-QBR rep_best_r0: rep_best under non-relaxed elig (pos+9<=size,
 * caller-checked once in u37_best). Elig, need-gate (need<=4<9),
 * fused-select and head_eq fallback are provably dead (proofs in
 * head_eq_r0 note); r-fuse ((u64)r-1>=pos <=> r==0||r>pos) is exact
 * uint64 arithmetic incl. r==0 wrap. Extend/filter/returns verbatim.
 * R8-L9NEW R-d: unrolled x3 (nrep is 1/3 at all callers; need const
 * per probe; residual loop keeps totality for nrep>3). */
static inline __attribute__((always_inline)) int
lzmesh_u37_rep_best_r0(const uint8_t *src, size_t size, size_t pos,
                                  const uint32_t recent[4], unsigned nrep,
                                  uint32_t *blen, uint32_t *bdist) {
    unsigned j;
    uint64_t mf_w8 = lzmesh_wl_ld64(src + pos);
    if (nrep > 0u
        && lzmesh_r8_rep_probe(src, size, pos, mf_w8, recent[0], 2u,
                               blen, bdist))
        return 1;
    if (nrep > 1u
        && lzmesh_r8_rep_probe(src, size, pos, mf_w8, recent[1], 4u,
                               blen, bdist))
        return 1;
    if (nrep > 2u
        && lzmesh_r8_rep_probe(src, size, pos, mf_w8, recent[2], 4u,
                               blen, bdist))
        return 1;
    for (j = 3u; j < nrep; j++) {
        if (lzmesh_r8_rep_probe(src, size, pos, mf_w8, recent[j], 4u,
                                blen, bdist))
            return 1;
    }
    return 0;
}

/* === I3 L1 last-span invisibility (owner: I3; LANE-I3) ===
 * Oracle L1 does not see the most recent take's span interior at the
 * immediate next query ONLY (pos == last_end, gap 0): 71/71 base
 * TIE-NEW 56 + LENDIFF 15 all gap 0, port-near-IN vs oracle-far.
 * Gap >= 1: span visible again (76/83 residual oracle-q IN last
 * span; delay flushed by the intervening lit query). Take-start m
 * hidden at gap 0 ONLY after a REP take (rep path stores nothing
 * yet): 26/26 LENDIFF EARLIER/M + s10-n63 TIE all REP-last, oracle
 * takes short d12/13 over m-candidate len 8-139. After NEW, m stays
 * visible (hiding it loses 23/32: i3m rescore 10 vs i3n 32). L5/L9
 * unchanged (H1 probeC 12/12 e05). Query-side skip ==
 * delayed-by-one-query store (tmp/i3/spanchk_*.py + rescore_i3*.py). */
static int lzmesh_i3_l1_span_hide(int level, size_t pos, size_t qq,
                                  size_t last_m, size_t last_end,
                                  int last_rep) {
    size_t lo;
    if (level != 1 || pos != last_end)
        return 0;
    lo = last_rep ? last_m : last_m + 1u;
    return qq >= lo && qq < last_end;
}
/* R14-TL1STACK-SPAN: KILLED (verified asm-IDENTICAL whole-file vs
 * stock: clang -O2 VRP already folds the retest; memo DROP-r14-
 * memo-tl1stack sec0 confirmed first-hand). Call sites stock. */

/* === S2 e01 MX lane (owner: S2; LANE-S2; codes NEW) ===
 * Oracle L1 = MX hash lane, big table only, NO small table
 * (HINT-FINDER-R2 sec1-2): slot = (load8B * MX) >> (64-hb), hb =
 * min(18, bitlen(n+1)+4), 6B head verify, extends>=6 (BedD edge
 * 5|6), loss_ok filter. Chains per slot (J1 structure kept):
 * newest-first walk; first VISIBLE-verifying->=6 takes (K5 strict
 * hide: hidden-newer + visible-older takes older, 11/11 holds);
 * first NON-VERIFYING shadows older (D3 12/12: MX-evict kills;
 * BedD 42/42 + D2 18/18 5B/7B-ignored). Shadow blocks fallback
 * too (visible-shadowed C misses, D3). Hidden-verifying->=7 is
 * the K5 fallback (owner: K5; LANE-K5; NEW-last only per O6;
 * rep@P+1 deferral, len>=2). J1 uncap kept (deep same-slot
 * writers visible). Pins tmp/s2/. L5/L9 slot_best untouched.
 * Returns 1 with blen/bdist set. */
/* WSTORE (LANE-W-STORE): L1 gap-window removal + L1 HUF rescue.
 * NOWIN: L1 stores visited-direct + span only (no gap-backward
 * windows): 314/314 oracle gap reads VIS, 6/6 port over-takes from
 * WIN1-3. L1RESCUE: F1-HUF-rescue twin for L1 multi-block
 * probe-RAW blocks (oracle HUF-keeps, e.g. s09 blk0 16310B).
 * Both default ON; =0 restores stock. */
static int lzmesh_wstore_l1rescue(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_WSTORE_L1RESCUE");
        init = 1;
        on = (e == NULL || atoi(e) != 0);
    }
    return on;
}

static int lzmesh_wstore_nowin(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_WSTORE_NOWIN");
        init = 1;
        on = (e == NULL || atoi(e) != 0);
    }
    return on;
}
/* P2-GETENV: cached trace-gate helpers. Read-once-at-first-call (same
 * static-init idiom as the T4/VEB gates); later setenv/unsetenv of the
 * knob has no effect. Semantics preserved exactly, including the
 * empty-string edge (atoi("") == 0 matches pos/slot 0). */
static int lzmesh_wpins_s2dbg_at(size_t pos) {
    static int init = 0, have = 0;
    static long at = -1L;
    if (!init) {
        const char *e = getenv("LZMESH_WPINS_S2DBG");
        init = 1;
        if (e != NULL) {
            have = 1;
            at = atol(e);
        }
    }
    return have && pos == (size_t)at;
}
static inline __attribute__((always_inline)) int
lzmesh_s2_mx_best(const uint8_t *src, size_t size, size_t pos,
                             const int32_t *head, const int32_t *prev,
                             unsigned hb,
                             uint32_t *blen, uint32_t *bdist, int level,
                             size_t last_m, size_t last_end,
                             int last_rep, uint32_t rep0, int relax) {
    int32_t q;
    uint32_t k5_flen = 0u, k5_fdist = 0u;
    int k5_have = 0;
    if (!lzmesh_u37_elig(pos, size, relax))
        return 0;
    if (pos + LZMESH_S2_MXLOAD > size || head == NULL || prev == NULL
        || hb == 0u || hb > 21u)
        return 0;
    q = head[lzmesh_s2_hx(
        lzmesh_u2_load_n(src + pos, LZMESH_S2_MXLOAD), hb)];
    /* P11-WINS (W9-shape): hoist pos-constant predicates out of the link
     * loop. hide fires only if level==1 && pos==last_end (bedded peq
     * 79%/20% text/mixed-e01); otherwise every link skips the call. */
    {
        int p11_dbg = lzmesh_wpins_s2dbg_at(pos);
        int p11_hide = (level == 1 && pos == last_end);
        if (p11_dbg)
            fprintf(stderr, "WPS2 pos=%u slotq=%d\n", (unsigned)pos,
                    (int)q);
        while (q >= 0) { /* J1: uncapped (deep writers visible) */
            size_t qq = (size_t)q;
            uint32_t dist, ln;
            if (qq >= pos || qq >= size)
                break;
            q = prev[qq];
            if (p11_dbg)
                fprintf(stderr, "WPS2  qq=%u heq=%d hid=%d\n",
                        (unsigned)qq,
                        lzmesh_u2_head_eq(src + pos, src + qq,
                                          LZMESH_S2_MXHEAD),
                        lzmesh_i3_l1_span_hide(level, pos, qq, last_m,
                                               last_end, last_rep));
            if (p11_hide
                && lzmesh_i3_l1_span_hide(level, pos, qq, last_m,
                                          last_end, last_rep)) {
            /* I3 hidden: K5-remember first hidden-verifying->=7. */
            if (!k5_have
                && lzmesh_u2_head_eq(src + pos, src + qq,
                                     LZMESH_S2_MXHEAD)) {
                dist = (uint32_t)(pos - qq);
                ln = lzmesh_u37_extend(src, size, pos, qq,
                                       LZMESH_S2_MXHEAD);
                if (ln >= 7u && lzmesh_u37_loss_ok(ln, dist, 0)) {
                    k5_flen = ln;
                    k5_fdist = dist;
                    k5_have = 1;
                }
            }
            continue;
        }
        if (!lzmesh_u2_head_eq(src + pos, src + qq, LZMESH_S2_MXHEAD))
            return 0; /* D3 shadow: non-verifying blocks older */
        dist = (uint32_t)(pos - qq);
        ln = lzmesh_u37_extend(src, size, pos, qq, LZMESH_S2_MXHEAD);
        if (ln < 6u)
            continue;
        if (!lzmesh_u37_loss_ok(ln, dist, 0))
            continue;
        *blen = ln;
        *bdist = dist;
        return 1;
        }
    }
    /* K5: no visible->=6; fall back to hidden->=7 (NEW-last only;
     * defer to rep@P+1, len>=2). */
    if (k5_have && !last_rep) {
        if (rep0 >= 1u && rep0 <= pos + 1u && pos + 2u < size
            && src[pos + 1u] == src[pos + 1u - rep0]
            && src[pos + 2u] == src[pos + 2u - rep0])
            return 0; /* K5: defer to rep@P+1 */
        *blen = k5_flen;
        *bdist = k5_fdist;
        return 1;
    }
    return 0;
}

/* === U1 e01 s06 MX-shadow veto (owner: U1; LANE-U1 sec6) ===
 * u18_slots_clean (h1/C1 7B model) says CLEAN on s06 (sole-writer
 * i=0 @slot727) but oracle L1 = MX lane (HINT-FINDER-R2 sec2):
 * MX slot(Q16)=847 holders [0,3], newest@3 6B-non-verifying
 * (byte0 e9 vs 9f) D3-shadows 0 => oracle M@16, take@17.
 * Bedded 33/33 (native + 16 morphs + 16 pokes, every take-start
 * exact incl poke1-5 late takes@18/20/21/22 + native@17-from-1)
 * + 2 controls (16Ax3 TAKE/MX-clean, real48 SKIP/MX-shadow).
 * Morph 8|9 split = MX-load kill-byte signature (unit[9],[10] =
 * kill idx6,7 of load@3). Duel/schedule DEAD for s06 (late takes
 * not @17; ANSWER-U1-3's load-bearing "morph-u8 MISS" was a lane
 * misreport -- first-hand re-run: TAKE).
 * SCOPE: L1 u18_want only. Veto fires iff MX newest-holder@p is
 * 6B-non-verifying (S2-D3-stop mirror; empty-holder impossible
 * under exact period since slot(0)==slot(p), keep). Cannot fire
 * on identical cells (u18-shape => no takes before p, rep0=0,
 * no spans => X-class impossible). X-class (s01/K fallthrough
 * WITH early takes) untouched; fallback = S2 route. */
static int lzmesh_u1_mx_shadowed(const uint8_t *s, size_t n, size_t p) {
    unsigned hb;
    uint32_t S;
    size_t i;
    if (s == NULL || p == 0u || p + LZMESH_S2_MXLOAD > n)
        return 0;
    hb = lzmesh_u2_hash_bits(n, 1);
    if (hb == 0u || hb > 21u)
        return 0;
    S = lzmesh_s2_hx(lzmesh_u2_load_n(s + p, LZMESH_S2_MXLOAD), hb);
    for (i = p; i-- > 0u;) {
        if (i + LZMESH_S2_MXLOAD > n)
            continue;
        if (lzmesh_s2_hx(lzmesh_u2_load_n(s + i, LZMESH_S2_MXLOAD), hb)
            != S)
            continue;
        return !lzmesh_u2_head_eq(s + p, s + i, LZMESH_S2_MXHEAD);
    }
    return 0;
}

/* G6 hashed single-slot cascade (owner: G6; L5/L9 only, codes NEW).
 * First slot-hit + head-verify wins (7B, else 5B, else 3B); empty /
 * out-of-range / head-mismatch = miss-continue; winner loss_ok-fail
 * = miss, no fallthrough (E4 strict). Floors auto-satisfied (hit
 * at tier hd extends >= hd >= 3). History [0,pos) must be stored.
 * Returns 1 with blen/bdist set. */
/* YF fwd decl (defined below; h3 visited-gate). Precedes slot_best. */
/* R2-ENC9 A4: fuse (was outline x2 in parse). */
static inline __attribute__((always_inline)) int lzmesh_yf_svisg_on(void);
/* R4-ENC5 C3: L5 peek 7B-only probe (h1 leg of slot_best verbatim).
 * Exact: the cascade tries h1 first with early return; any h2/h3 win
 * carries tier 5/3 and dies at the I4 FIX-A gate, and h1 head-match +
 * filter-fail returns 0 strict (E4) in both shapes. blen/bdist are
 * written only on an h1 win; the peek caller consumes them only when
 * hhave (short-circuit duel guards; T4 trace print is output-only). */
static inline __attribute__((always_inline)) int
lzmesh_u37_slot_best_h1only(const uint8_t *src, size_t size, size_t pos,
                             const uint32_t *big, unsigned hb,
                             uint32_t *blen, uint32_t *bdist,
                             int relax) {
    uint32_t q;
    uint64_t p11_w8 = 0u;
    int p11_fused;
    if (pos + LZMESH_U37_MINREP > size)
        return 0;
    if (!relax && pos + 9u > size)
        return 0;
    p11_fused = (pos + 8u <= size);
    if (p11_fused)
        p11_w8 = lzmesh_wl_ld64(src + pos);
    if (pos + 7u <= size) {
        q = big[lzmesh_u2_h1(p11_fused ? (p11_w8 & 0xFFFFFFFFFFFFFFull)
                                       : lzmesh_u2_load_n(src + pos, 7u),
                             hb)];
        if (q != LZMESH_U2_EMPTY && (size_t)q < pos
            && lzmesh_mf_head_eq(src + pos, p11_w8, p11_fused, src + q,
                                 (size_t)q + 8u <= size, 7u)) {
            uint32_t dist = (uint32_t)pos - q;
            uint32_t ln = lzmesh_u37_extend(src, size, pos, (size_t)q,
                                            7u);
            if (ln < 7u
                || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
                return 0; /* strict: winner fail = miss */
            *blen = ln;
            *bdist = dist;
            return 1;
        }
    }
    return 0;
}
static inline __attribute__((always_inline)) int
lzmesh_u37_slot_best(const uint8_t *src, size_t size, size_t pos,
                                const uint32_t *big, const uint32_t *small,
                                unsigned hb,
                                uint32_t *blen, uint32_t *bdist,
                                int relax, int level,
                                const unsigned char *i5v,
                                const unsigned char *ycon,
                                const unsigned char *ywin,
                                int *win_tier) {
    uint32_t q;
    uint64_t p11_w8 = 0u;
    int p11_fused;
    /* P29-MF A1: elig inlined (same two predicates as u37_elig). */
    if (pos + LZMESH_U37_MINREP > size)
        return 0;
    if (!relax && pos + 9u > size)
        return 0;
    /* P11-WINS: one u64 feeds the 7/5/3 cascade (LE low bytes equal
     * load_n bit-for-bit); tail legs keep guarded stock loads. */
    p11_fused = (pos + 8u <= size);
    if (p11_fused)
        p11_w8 = lzmesh_wl_ld64(src + pos);
    if (pos + 7u <= size) {
        q = big[lzmesh_u2_h1(p11_fused ? (p11_w8 & 0xFFFFFFFFFFFFFFull)
                                       : lzmesh_u2_load_n(src + pos, 7u),
                             hb)];
        if (q != LZMESH_U2_EMPTY && (size_t)q < pos
            && lzmesh_mf_head_eq(src + pos, p11_w8, p11_fused, src + q,
                                 (size_t)q + 8u <= size, 7u)) {
            uint32_t dist = (uint32_t)pos - q;
            uint32_t ln = lzmesh_u37_extend(src, size, pos, (size_t)q,
                                            7u);
            if (ln < 7u
                || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
                return 0; /* strict: winner fail = miss */
            *blen = ln;
            *bdist = dist;
            if (win_tier != NULL) /* P29-MF A2: D2 tier out-param */
                *win_tier = 7;
            return 1;
        }
    }
    if (pos + 5u <= size) {
        q = big[lzmesh_u2_h2(p11_fused ? (p11_w8 & 0xFFFFFFFFFFull)
                                       : lzmesh_u2_load_n(src + pos, 5u),
                             hb)];
        if (q != LZMESH_U2_EMPTY && (size_t)q < pos
            && lzmesh_mf_head_eq(src + pos, p11_w8, p11_fused, src + q,
                                 (size_t)q + 8u <= size, 5u)) {
            uint32_t dist = (uint32_t)pos - q;
            uint32_t ln = lzmesh_u37_extend(src, size, pos, (size_t)q,
                                            5u);
            if (ln < 5u
                || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
                return 0; /* strict: winner fail = miss */
            *blen = ln;
            *bdist = dist;
            if (win_tier != NULL) /* P29-MF A2 */
                *win_tier = 5;
            return 1;
        }
    }
    if (pos + 3u <= size) {
        uint32_t s = lzmesh_u2_h3(
            (uint32_t)(p11_fused ? (p11_w8 & 0xFFFFFFu)
                                 : lzmesh_u2_load_n(src + pos, 3u)));
        q = small[s];
        if (q != LZMESH_U2_EMPTY && (size_t)q < pos
            && lzmesh_mf_head_eq(src + pos, p11_w8, p11_fused, src + q,
                                 (size_t)q + 8u <= size, 3u)) {
            uint32_t dist = (uint32_t)pos - q;
            uint32_t ln = lzmesh_u37_extend(src, size, pos, (size_t)q,
                                            3u);
            if (ln < 3u
                || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
                return 0;
            /* YF: h3 source-gate (gated; miss = fall through). Allow
             * landed (i5v), winpos (queried+won), take-consumed [m,end],
             * pos0-direct; decline never-touched flood-S. Blanket
             * visited-gate falsified battery-wide (121 NEW); span/peek
             * marks superseded (subsumed by consumed/winpos; s06@66 S47 =
             * winpos HIT vs s09big@238090 never-queried MISS). */
            if (level == 9 && i5v != NULL && lzmesh_yf_svisg_on()
                && (size_t)q < size && (size_t)q > 0u && !i5v[q]
                && (ycon == NULL || !ycon[q])
                && (ywin == NULL || !ywin[q])) {
                /* never-touched S: decline (fall to walk/dump/miss). */
            } else {
                *blen = ln;
                *bdist = dist;
                if (win_tier != NULL) /* P29-MF A2 */
                    *win_tier = 3;
                return 1;
            }
        }
        /* P30-CHAIN: W9 walk deleted (chain dead: qlink never written
         * since P23 V5; QPICK=1 vs base 0/12784 DIV + 0 takes FULL-bedded;
         * YF-decline falls through to miss, as before). */
    }
    return 0;
}

/* R5-QBR slot_best_r0: slot_best under non-relaxed elig (pos+9<=size,
 * caller-checked once in u37_best; u37_best's only caller passes
 * relax=0). Size gates (7/5/3<=9), fused-selects and head_eq fallback
 * provably dead; EMPTY-vs-range fused to q<pos under the huge-split
 * (size<=0xFFFFFFFFu => pos<=0xFFFFFFFFu => q==EMPTY fails q<pos
 * exactly like stock; huge inputs run stock). YF h3 gate, strict
 * winner-fail, extend/filter/returns verbatim. win_tier omitted
 * (u37_best passes NULL). */
static inline __attribute__((always_inline)) int
lzmesh_u37_slot_best_r0(const uint8_t *src, size_t size, size_t pos,
                                   const uint32_t *big, const uint32_t *small,
                                   unsigned hb,
                                   uint32_t *blen, uint32_t *bdist,
                                   int level,
                                   const unsigned char *i5v,
                                   const unsigned char *ycon,
                                   const unsigned char *ywin) {
    uint32_t q;
    uint64_t p11_w8;
    if (size > 0xFFFFFFFFu)
        return lzmesh_u37_slot_best(src, size, pos, big, small, hb,
                                    blen, bdist, 0, level, i5v,
                                    ycon, ywin, NULL);
    p11_w8 = lzmesh_wl_ld64(src + pos);
    q = big[lzmesh_u2_h1(p11_w8 & 0xFFFFFFFFFFFFFFull, hb)];
    if ((size_t)q < pos
        && lzmesh_mf_head_eq_r0(p11_w8, src + q, 7u)) {
        uint32_t dist = (uint32_t)pos - q;
        uint32_t ln = lzmesh_u37_extend(src, size, pos, (size_t)q, 7u);
        if (ln < 7u
            || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
            return 0; /* strict: winner fail = miss */
        *blen = ln;
        *bdist = dist;
        return 1;
    }
    q = big[lzmesh_u2_h2(p11_w8 & 0xFFFFFFFFFFull, hb)];
    if ((size_t)q < pos
        && lzmesh_mf_head_eq_r0(p11_w8, src + q, 5u)) {
        uint32_t dist = (uint32_t)pos - q;
        uint32_t ln = lzmesh_u37_extend(src, size, pos, (size_t)q, 5u);
        if (ln < 5u
            || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
            return 0; /* strict: winner fail = miss */
        *blen = ln;
        *bdist = dist;
        return 1;
    }
    {
        uint32_t s = lzmesh_u2_h3((uint32_t)(p11_w8 & 0xFFFFFFu));
        q = small[s];
        if ((size_t)q < pos
            && lzmesh_mf_head_eq_r0(p11_w8, src + q, 3u)) {
            uint32_t dist = (uint32_t)pos - q;
            uint32_t ln = lzmesh_u37_extend(src, size, pos, (size_t)q, 3u);
            if (ln < 3u
                || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
                return 0;
            /* YF: h3 source-gate verbatim (gated; miss = fall through). */
            if (level == 9 && i5v != NULL && lzmesh_yf_svisg_on()
                && (size_t)q < size && (size_t)q > 0u && !i5v[q]
                && (ycon == NULL || !ycon[q])
                && (ywin == NULL || !ywin[q])) {
                /* never-touched S: decline (fall to miss, as before). */
            } else {
                *blen = ln;
                *bdist = dist;
                return 1;
            }
        }
    }
    return 0;
}

/* R6-T9 rep_best_r1: rep_best under the L9 peek contract (caller-proven:
 * relax would be 1 via U8; pp+8<=size). Elig (pp+2<=size), need-gates
 * (need<=4), fused-select and head_eq fallback all provably dead;
 * r-fuse ((u64)r-1>=pp <=> r==0||r>pp) exact incl wrap; wb_ok holds
 * (qq=pp-r<=pp-1 => qq+8<=pp+7<size). Extend/floor/returns verbatim. */
static inline __attribute__((always_inline)) int
lzmesh_u37_rep_best_r1(const uint8_t *src, size_t size, size_t pp,
                                  const uint32_t recent[4], unsigned nrep,
                                  uint32_t *blen, uint32_t *bdist) {
    /* R8-L9NEW R-d: single-probe fast path (nrep==1 at all callers;
     * residual loop keeps totality). */
    unsigned j;
    uint64_t mf_w8 = lzmesh_wl_ld64(src + pp);
    if (nrep > 0u
        && lzmesh_r8_rep_probe(src, size, pp, mf_w8, recent[0], 2u,
                               blen, bdist))
        return 1;
    for (j = 1u; j < nrep; j++) {
        if (lzmesh_r8_rep_probe(src, size, pp, mf_w8, recent[j], 4u,
                                blen, bdist))
            return 1;
    }
    return 0;
}

/* R6-T9 slot_best_r1: slot_best under the L9 peek contract (caller-proven:
 * level==9, relax would be 1 via U8, pp+8<=size). Elig (pp+2<=size),
 * size gates (7/5/3<=8), fused-selects and head_eq fallback provably
 * dead; EMPTY-vs-range fused to q<pp under the huge-split (QBR proof:
 * size<=0xFFFFFFFF => q==EMPTY fails q<pp exactly like stock; huge
 * inputs run stock with relax=1). win_tier omitted: the L9 peek caller
 * reads mf_htier only under level==5 (I4 FIX-A gate), so the stores
 * are dead there. YF h3 gate minus the proven level check, strict
 * winner-fail, extend/filter verbatim. L1/L5 peek call sites untouched
 * (stock). */
static inline __attribute__((always_inline)) int
lzmesh_u37_slot_best_r1(const uint8_t *src, size_t size, size_t pp,
                                   const uint32_t *big, const uint32_t *small,
                                   unsigned hb,
                                   uint32_t *blen, uint32_t *bdist,
                                   const unsigned char *i5v,
                                   const unsigned char *ycon,
                                   const unsigned char *ywin) {
    uint32_t q;
    uint64_t p11_w8;
    if (size > 0xFFFFFFFFu)
        return lzmesh_u37_slot_best(src, size, pp, big, small, hb,
                                    blen, bdist, 1, 9, i5v,
                                    ycon, ywin, NULL);
    p11_w8 = lzmesh_wl_ld64(src + pp);
    q = big[lzmesh_u2_h1(p11_w8 & 0xFFFFFFFFFFFFFFull, hb)];
    if ((size_t)q < pp
        && lzmesh_mf_head_eq_r0(p11_w8, src + q, 7u)) {
        uint32_t dist = (uint32_t)pp - q;
        uint32_t ln = lzmesh_u37_extend(src, size, pp, (size_t)q, 7u);
        if (ln < 7u
            || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
            return 0; /* strict: winner fail = miss */
        *blen = ln;
        *bdist = dist;
        return 1;
    }
    q = big[lzmesh_u2_h2(p11_w8 & 0xFFFFFFFFFFull, hb)];
    if ((size_t)q < pp
        && lzmesh_mf_head_eq_r0(p11_w8, src + q, 5u)) {
        uint32_t dist = (uint32_t)pp - q;
        uint32_t ln = lzmesh_u37_extend(src, size, pp, (size_t)q, 5u);
        if (ln < 5u
            || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
            return 0; /* strict: winner fail = miss */
        *blen = ln;
        *bdist = dist;
        return 1;
    }
    {
        uint32_t s = lzmesh_u2_h3((uint32_t)(p11_w8 & 0xFFFFFFu));
        q = small[s];
        if ((size_t)q < pp
            && lzmesh_mf_head_eq_r0(p11_w8, src + q, 3u)) {
            uint32_t dist = (uint32_t)pp - q;
            uint32_t ln = lzmesh_u37_extend(src, size, pp, (size_t)q, 3u);
            if (ln < 3u
                || dist >= lzmesh_p3_filt_maxd[ln < 6u ? ln : 6u])
                return 0;
            /* YF: h3 source-gate (level==9 proven at all callers, so
             * the level check is dropped; remainder verbatim). */
            if (i5v != NULL && lzmesh_yf_svisg_on()
                && (size_t)q < size && (size_t)q > 0u && !i5v[q]
                && (ycon == NULL || !ycon[q])
                && (ywin == NULL || !ywin[q])) {
                /* never-touched S: decline (fall to miss, as before). */
            } else {
                *blen = ln;
                *bdist = dist;
                return 1;
            }
        }
    }
    return 0;
}

/* Cur-leg best: rep short-circuit (lowest slot, L1 rep0-only), else
 * hash (NEW): L1 S2 MX-slot chains, L5/L9 G6 hashed
 * single-slot cascade. *is_rep says rep (1) or new.
 * I3: last_m/last_end/last_rep thread the most recent take (L1 hide). */
/* R8 try-2: l1_rep_r0 ELIDED (stock rep_best_r0 covers the L1 rep leg
 * under stock order; unroll redundant). l1_mx_r0 fwd-decl; body EOF,
 * always_inline (inlined into the u37_best site as in E2). */
static inline __attribute__((always_inline)) int
lzmesh_r6_l1_mx_r0(const uint8_t *src, size_t size, size_t pos,
                   const int32_t *head, const int32_t *prev,
                   unsigned hb, uint32_t *blen, uint32_t *bdist,
                   size_t last_m, size_t last_end, int last_rep,
                   uint32_t rep0);

static int lzmesh_u37_best(const uint8_t *src, size_t size, size_t pos,
                           const int32_t *head, const int32_t *prev,
                           const uint32_t *big, const uint32_t *small,
                           unsigned hb,
                           const uint32_t recent[4], uint32_t *blen,
                           uint32_t *bdist, int *is_rep, int level,
                           size_t last_m, size_t last_end, int last_rep,
                           int relax,
                           const unsigned char *i5v,
                           const unsigned char *ycon,
                           const unsigned char *ywin) {
    unsigned nrep = (level == 1) ? 1u : 3u;
    /* R12-H123-H4: L1-best fast path (dead-arm removal, exact by
     * construction: relax==0 at the sole caller, level==1 const on
     * this arm, nrep==1 const, MINREP=2<9 so the fused tail check
     * returns 0 on exactly the union of the two stock tail checks;
     * the single rep probe + mx leg are the stock nrep=1 sequence
     * verbatim). Saves 6 dead branches/query at +2 dispatch. L5/L9
     * pay +1 predictable dispatch branch, stock order otherwise. */
    if (level == 1 && !relax) {
        uint64_t h4_w8;
        if (pos + 9u > size)
            return 0;
        h4_w8 = lzmesh_wl_ld64(src + pos);
        if (lzmesh_r8_rep_probe(src, size, pos, h4_w8, recent[0], 2u,
                                blen, bdist)) {
            *is_rep = 1;
            return 1;
        }
        if (lzmesh_r6_l1_mx_r0(src, size, pos, head, prev, hb, blen,
                               bdist, last_m, last_end, last_rep,
                               recent[0])) {
            *is_rep = 0;
            return 1;
        }
        return 0;
    }
    /* R5-QBR elig-once: u37_elig(pos,size,0) predicates verbatim. Stock
     * checked elig twice (rep + slot/mx); one check dominates because
     * all three legs return 0 on elig-fail (mx_best gates on u37_elig).
     * relax!=0 (no current caller; sole caller passes 0) keeps the
     * stock legs so the function stays total. */
    if (relax) {
        if (lzmesh_u37_rep_best(src, size, pos, recent, nrep, blen,
                                bdist, level, relax)) {
            *is_rep = 1;
            return 1;
        }
        if (level == 1) {
            if (lzmesh_s2_mx_best(src, size, pos, head, prev, hb,
                                  blen, bdist, level, last_m,
                                  last_end, last_rep, recent[0],
                                  relax)) {
                *is_rep = 0;
                return 1;
            }
        } else {
            if (lzmesh_u37_slot_best(src, size, pos, big, small, hb,
                                     blen, bdist, relax, level, i5v,
                                     ycon, ywin, NULL)) {
                *is_rep = 0;
                return 1;
            }
        }
        return 0;
    }
    if (pos + LZMESH_U37_MINREP > size)
        return 0;
    if (pos + 9u > size)
        return 0;
    /* R8 try-2: STOCK order (rep_best first, all levels incl L1) so the
     * L5 executed path matches base op-for-op (E2's level-first
     * redispatch is the prime L5-mover suspect; try-1 priced bloat
     * innocent: u37_parse -960B vs base yet L5 -1.3 OV). L1 rep leg =
     * stock rep_best_r0 (nrep=1: j=0 only; result-identical to the
     * elided l1_rep_r0 unroll, +2 loop branches/query; bytes prove).
     * L1 mx leg = inlined l1_mx_r0 (noinline call cost try-1 L1
     * +6.7->+4.1, unaffordable). */
    if (lzmesh_u37_rep_best_r0(src, size, pos, recent, nrep, blen,
                               bdist)) {
        *is_rep = 1;
        return 1;
    }
    if (level == 1) {
        if (lzmesh_r6_l1_mx_r0(src, size, pos, head, prev, hb, blen,
                               bdist, last_m, last_end, last_rep,
                               recent[0])) {
            *is_rep = 0;
            return 1;
        }
        return 0;
    }
    if (lzmesh_u37_slot_best_r0(src, size, pos, big, small, hb,
                                blen, bdist, level, i5v,
                                ycon, ywin)) {
        *is_rep = 0;
        return 1;
    }
    return 0;
}

static void lzmesh_u37_recents_init(uint32_t r[4]) {
    r[0] = 1u;
    r[1] = 1u;
    r[2] = 1u;
    r[3] = 1u;
}

/* E3 GEN-KEEP-STOP (L1 only): e01 zeros-base single-spike inputs where
 * the oracle STOREs but TIER-2 keeps (D2 exact-est owns the general
 * rule; this is the census-exact stopgap). Shape: exactly one nonzero
 * byte, n in 23..34, spike position in the oracle-STORE set below.
 * Census: tmp/e3/sparse-census.json (exhaustive n=10..62 x all ppos x
 * vals {1,130,255}; val-independent; n<=22 all-STORE already via TIER,
 * n>=35 all-COMP). Verified: u38/u35 silent on all 129 cells, so the
 * decline falls through to STORE = oracle bytes. Returns 1 to bail. */
static int lzmesh_u37_e01_sparse_bail(const uint8_t *src, size_t size) {
    static const uint8_t lo[12] = { 1, 2, 6, 10, 12, 13, 14, 15, 16, 17, 18, 19 };
    static const uint8_t hi[12] = { 22, 22, 22, 23, 23, 23, 23, 19, 19, 19, 19, 19 };
    static const uint8_t lo2[12] = { 0, 0, 2, 0, 0, 0, 0, 21, 22, 23, 0, 0 };
    static const uint8_t hi2[12] = { 0, 0, 2, 0, 0, 0, 0, 23, 23, 23, 0, 0 };
    size_t i, ppos = 0u;
    unsigned nz = 0u;
    unsigned k;
    if (src == NULL || size < 23u || size > 34u)
        return 0;
    for (i = 0u; i < size; i++) {
        if (src[i] != 0u) {
            nz++;
            ppos = i;
            if (nz > 1u)
                return 0;
        }
    }
    if (nz != 1u)
        return 0;
    k = (unsigned)size - 23u;
    if (ppos >= lo[k] && ppos <= hi[k])
        return 1;
    if (hi2[k] != 0u && ppos >= lo2[k] && ppos <= hi2[k])
        return 1;
    return 0;
}

/* Decoder-mirror updates (plain shift-push / move-to-front). */
static void lzmesh_u37_recents_push(uint32_t r[4], uint32_t d) {
    r[3] = r[2];
    r[2] = r[1];
    r[1] = r[0];
    r[0] = d;
}

static void lzmesh_u37_recents_rep(uint32_t r[4], unsigned k) {
    uint32_t tmp, i;
    if (k == 0u || k > 3u)
        return;
    tmp = r[k];
    for (i = k; i > 0u; i--)
        r[i] = r[i - 1u];
    r[0] = tmp;
}

/* H1 store schedule (LANE-H1; black-box pins tmp/h1/, all levels).
 * Visits thin: skip walk step 1+(litrun>>8), litpos init 1 (probeB
 * 14/14 EXACT shift8/l1 + e09/e01; same formula as u2_l1_step/Q28).
 * Stores temporal: visited-direct immediate (A4 36/36); match
 * catch-up = span-forward all tables (C 12/12 d72, OS d71 span-small,
 * O6 forward) then lit-gap windows backward (O4 windows-beat-span,
 * O5 backward) big-only L5/L9 (O3 no-small) / chain L1; skip visited
 * (C evictors stand). Window W=8 L5/L9 (S1/AH 8/8 blocks), W=5 L1
 * (AE 5/3 blocks). ins init 1 + pos0 init-stored (B0/B0b 18/18 each).
 * L1 order/span uniform-with-L5 (residue: gate-validated, unprobed).
 * Widths/cascade untouched (E4/G6). */
#define LZMESH_H1_WIN_L1 5u
#define LZMESH_H1_WIN_GEN 8u

/* Direct store at visited pos (all tables). stored = S4 L1
 * once-bitmap (NULL off-L1): re-storing a chained pos links
 * prev[p]=p (self-loop) or a cycle, hanging the uncapped J1 walk
 * to ~0u iters (s12-n65536 e01 0.03s -> 415s); J4-shifted take-m
 * re-hits stored visits, so skip re-stores (first wins, H1 C). */
/* P29-MF A3: store path inline (per-visit/per-drain calls fused into the
 * parse; zero behavior change). */
static inline __attribute__((always_inline)) void
lzmesh_h1_store_visit(int32_t *head, int32_t *prev,
                                  uint32_t *big, uint32_t *small,
                                  const uint8_t *src, size_t size, size_t pos,
                                  unsigned hb, int level,
                                  unsigned char *stored) {
    /* R7-NOBITMAP: S4 guard+mark deleted on the L1 arm (visit + span +
     * gap + nowin-span below). ANSWER-r6-mix1-1: nobitmap variant 0
     * DIVs/14476 + bench 3/3 IDENT (g0=2072 re-links idempotent:
     * re-insert lands the same chain). stored[] calloc kept (min
     * churn); unread/unwritten on L1. */
    (void)stored;
    if (level == 1) { /* S2 MX link (mx_link bounds-checks) */
        lzmesh_s2_mx_link(head, prev, src, size, pos, hb);
    } else {
        lzmesh_u37_g6_store(big, small, src, size, pos, hb);
    }
}

/* P29-MF B probe REMOVED (killed: run-skip DIVs at every live threshold;
 * bench-12 N=8:6 N=32:3 N=128:3 N=1024:2(mixed e01+e05); full-2seed N=8:
 * 52/1504; N=65536 silent-vacuous. See LANE-P29-MF). */

/* Match catch-up [ins, end), take [m, end): span forward (all tables)
 * from span_lo (m+1 when m direct-stored, m on challenger-win peek),
 * then gap backward (unvisited windowed-i only). vis = caller-owned
 * size bytes; marks visits in [ins, m). */
/* R9-TEXT1-E3: L1-nowin span loops force-INLINE (reverses R8
 * noinline; 34.6k outline calls/rep on text-L1 at ~25 instr/call
 * saved vs ~25 instr parse growth. L1-only caller (catchup L1-nowin
 * arm); L5/L9 never execute it (layout-only effect there, gated by
 * Air 951 L5-neutrality). Body EOF unchanged (E2 verbatim loops). */
static inline __attribute__((always_inline)) void
lzmesh_h1_l1_nowin(int32_t *head, int32_t *prev,
                   const uint8_t *src, size_t size, size_t span_lo,
                   size_t end, unsigned hb);
static void lzmesh_h1_catchup(int32_t *head, int32_t *prev,
                              uint32_t *big, uint32_t *small,
                              unsigned char *vis,
                              const uint8_t *src, size_t size,
                              size_t ins, size_t m, size_t end,
                              unsigned hb, int level, size_t span_lo,
                              unsigned char *stored) {
    size_t v = ins, i, nextv;
    unsigned w = (level == 1) ? LZMESH_H1_WIN_L1 : LZMESH_H1_WIN_GEN;
    size_t k;
    /* R6-MIX1 L1-nowin fast path: span only. The vis prologue is
     * unread when the gap walk is skipped (stock NOWIN return below);
     * vis is caller scratch no L1 path reads (i4/k2 windows are
     * L5-only). stored!=NULL proven (sole caller passes parse L1
     * calloc-checked). Guard-hoist: end+8<=size proves every span
     * guard, else the stock loop.
     * R8-MIX1NEUTRAL try-1: loops out-of-line in lzmesh_h1_l1_nowin
     * (noinline, EOF) so u37_parse keeps near-base shape; the two
     * loops are L1-only dead bytes at L5/L9. One predicted call per
     * L1-nowin catchup. */
    if (level == 1 && lzmesh_wstore_nowin()) {
        lzmesh_h1_l1_nowin(head, prev, src, size, span_lo, end, hb);
        return;
    }
    /* P17-FINDER win256: gap<=256 visits every pos (step is 1 while
     * (v-ins)<256), so the walk takes no else-leg and vis[] is pure
     * scratch: skip clear+mark+walk. Fails open (m<ins underflows to
     * a huge gap, running stock). */
    int win256 = (m - ins > 256u) ? 1 : 0;
    if (win256) {
        for (k = ins; k < m; k++)
            vis[k] = 0u;
        while (v < m) {
            vis[v] = 1u;
            v += (size_t)1u + ((v - ins) >> 8);
        }
    }
    /* R8 try-3: DEHOISTED to stock loop shape (per-iter level check;
     * the hoisted L5 loop body is the remaining L5-mover suspect after
     * try-1 priced bloat innocent and try-2 tests dispatch order).
     * L1-neutral: these loops are dead at L1-nowin-default (fast path
     * above returns; gcov taken IDENTICAL to try-2). R7-NOBITMAP L1
     * guard+mark deletion KEPT (prize, proof at visit site). */
    (void)stored;
    for (i = span_lo; i < end; i++) {
        if (level == 1) { /* S2 MX link */
            lzmesh_s2_mx_link(head, prev, src, size, i, hb);
        } else {
            lzmesh_u37_g6_store(big, small, src, size, i, hb);
        }
    }
    nextv = m;
    /* WSTORE NOWIN: L1 stores visited-direct + span only (no
     * gap-backward windows). */
    if (level == 1 && lzmesh_wstore_nowin())
        return;
    if (!win256) /* P17-FINDER win256: no else-leg fires (see above) */
        return;
    /* R8 try-3: DEHOISTED to stock loop shape (D1 deletion kept). */
    for (i = m; i > ins;) {
        --i;
        if (vis[i])
            nextv = i;
        else if (nextv - i <= (size_t)(w - 1u)) {
            if (level == 1) { /* S2 MX link */
                lzmesh_s2_mx_link(head, prev, src, size, i, hb);
            } else {
                lzmesh_u37_g6_store_big(big, src, size, i, hb);
            }
        }
    }
}

/* === I4 e05 bulk: n1 7B-only peek + span/rep-m flush lag (owner: I4; LANE-I4) ===
 * FIX-A (scope): oracle's n1 hash peek is 7B-only at L5 (black-box beds
 * tmp/i4/n1bed2.py 12/12: tier5 L5/L6 + tier3 L4 challengers never win in
 * oracle, tier7 L7 wins; port peeked the full cascade and skipped).
 * FIX-B (lag): after a take WITH a lit gap [ins, m), oracle
 * buffers the catch-up (rep-m store + span + gap windows) and
 * flushes at the next skip-walk or take store -- NOT at the lazy
 * peek pre-store (beds tmp/i4/lagbed2.py 6/6 + flushbed.py 8/8:
 * 0-lit next query misses span, >=1 lit sees whole span). Abutting
 * 0-lit takes (ins == m) catch up immediately (s43n50 vs s12n50
 * take@8; abutting big-lag overfits -- s02n60/s07n129 paradox,
 * tmp/i4/e9-superseded.diff);
 * 35/36 trace-fit on e05 ties + lazy first-divergences). Visits stay
 * immediate (H1 A4); gap windows stay immediate (no lag evidence;
 * H1 far evidence unaffected).
 * Both gates are level 5 only; L0/L1/L9 byte-identical. H1/G6/u37
 * helpers untouched (hooks below call owned helpers). */
/* P29-MF A2: lzmesh_i4_tier deleted (sole caller was the L5 peek FIX-A
 * site, now reusing slot_best's winning tier via out-param; win at T
 * implies i4 == T, P4 bedded 192048/192048 exact on text-L5). */

/* Pending take catch-up: rep-m direct store + span range + gap
 * window range. Flush replays h1_catchup's store ORDER (m, span,
 * windows); only the TIMING lags one take. */
typedef struct {
    size_t plo, phi; /* span [span_lo, end) */
    size_t pm; /* rep-m direct store */
    size_t wlo, wm; /* gap windows [ins, m) */
    int pok, pmok, wok;
} lzmesh_i4_pend;

/* Immediate gap windows for [ins, m) (mirror of h1_catchup's window
 * half; H1's function untouched). GEN big-only. L5-only caller. */
static void lzmesh_i4_windows(int32_t *head, int32_t *prev,
                              uint32_t *big,
                              unsigned char *vis,
                              const uint8_t *src, size_t size,
                              size_t ins, size_t m, unsigned hb) {
    size_t v = ins, i, nextv;
    size_t k;
    (void)head;
    (void)prev;
    /* P17-FINDER win256 (same proof as h1_catchup): gap<=256 takes no
     * else-leg; vis[] is pure scratch. */
    if (m - ins <= 256u)
        return;
    for (k = ins; k < m; k++)
        vis[k] = 0u;
    while (v < m) {
        vis[v] = 1u;
        v += (size_t)1u + ((v - ins) >> 8);
    }
    nextv = m;
    for (i = m; i > ins;) {
        --i;
        if (vis[i])
            nextv = i;
        else if (nextv - i <= (size_t)(LZMESH_H1_WIN_GEN - 1u))
            lzmesh_u37_g6_store_big(big, src, size, i, hb);
    }
}

/* Drain pending catch-up in base order (rep-m, span, windows). */
/* R2-ENC9 A4: fuse (was outline x5 in parse via k2_drain loop). */
static inline __attribute__((always_inline)) void
lzmesh_i4_flush(int32_t *head, int32_t *prev,
                            uint32_t *big, uint32_t *small,
                            unsigned char *vis,
                            const uint8_t *src, size_t size, unsigned hb,
                            lzmesh_i4_pend *p) {
    size_t i;
    if (p->pmok) {
        lzmesh_u37_g6_store(big, small, src, size, p->pm, hb);
        p->pmok = 0;
    }
    if (p->pok) {
        for (i = p->plo; i < p->phi; i++)
            lzmesh_u37_g6_store(big, small, src, size, i, hb);
        p->pok = 0;
    }
    if (p->wok) {
        lzmesh_i4_windows(head, prev, big, vis, src, size, p->wlo,
                          p->wm, hb);
        p->wok = 0;
    }
}

/* === K2 T3 queue: multi-pending FIFO + REP-no-flush (owner: K2; LANE-K2) ===
 * Beds tmp/k2/k2bed*.py (black-box oracle, merged-J base):
 * - REP take stores do NOT flush pending catch-up (E run-REP 8/8 SPLIT
 *   oracle-miss; F 2-chain 6/6 oracle-miss; V-A rep-artifact excluded
 *   by NEW-only guard + boff sweep).
 * - NEW take stores flush (I abutting-NEW 6/6 AGREE-hit).
 * - skip-walk (>=1 lit) flushes the whole queue (G 2-chain+k1 4/4
 *   AGREE; H/V-B 1REP+k 7/7 AGREE).
 * - queue, not single-slot: G shows T1 span recoverable after 2 REPs
 *   (overwrite would lose it). FIFO replay; within-entry order reuses
 *   i4_flush (rep-m, span, windows). I4 helpers untouched.
 * - abutting REP queues its own catch-up (s11-n513 natural: @65 own
 *   missed at @71).
 * - L1: gapped REP stores flush (s14-n100 @21: gap lit, incl
 *   lazy-consumed, is the drain trigger; abutting REP still queues).
 * OPEN (not shipped): abutting-NEW own span (s02n60 take@50 wants NOW,
 * s06/s10/s01/s07/s14/s09 want LAG; same local shape; hidden variable).
 * L3 SHIPPED the resolution (LANE-L3): abutting NEW queues (the s02
 * paradox dissolved on merged-K) and the n1 peek drains (peekbed 6/6).
 * L5-only; other levels byte-identical (all hooks level == 5). */
typedef struct {
    lzmesh_i4_pend *v;
    size_t n;
    size_t cap;
} lzmesh_k2_q;
static void lzmesh_k2_free(lzmesh_k2_q *q) {
    free(q->v);
    q->v = NULL;
    q->n = 0u;
    q->cap = 0u;
}
static int lzmesh_k2_append(lzmesh_k2_q *q, const lzmesh_i4_pend *e) {
    if (q->n == q->cap) {
        size_t ncap = (q->cap == 0u) ? 16u : q->cap * 2u;
        lzmesh_i4_pend *nv =
            (lzmesh_i4_pend *)realloc(q->v, ncap * sizeof *nv);
        if (nv == NULL)
            return 0;
        q->v = nv;
        q->cap = ncap;
    }
    q->v[q->n] = *e;
    q->n++;
    return 1;
}
static inline __attribute__((always_inline)) void
lzmesh_k2_drain(int32_t *head, int32_t *prev, uint32_t *big,
                            uint32_t *small, unsigned char *vis,
                            const uint8_t *src, size_t size, unsigned hb,
                            lzmesh_k2_q *q) {
    size_t i;
    for (i = 0u; i < q->n; i++)
        lzmesh_i4_flush(head, prev, big, small, vis, src, size, hb,
                        &q->v[i]);
    q->n = 0u;
}

/* === I5 e09 store rules (owner: I5; LANE-I5) ===
 * U15 (black-box tmp/i5/: 12/12 probes + 21/30 TIE replay, e05+e09 same):
 * SPAN head-conditional (skip iff slot head == i head, per tier h2/h3),
 * FLOOD [fup,ins) h2/h3 unconditional at first gap step,
 * WALK-SKIP backfill [prev_v,v) h1+h2 (BIG-only) when step>1,
 * H1 mirror (LZMESH_I5_H1=1 default): h1 alongside h2 in
 * direct/span/flood; FORWARD windows h2-only W8 (H1 shape verbatim).
 * L9-gated at call sites; other levels byte-identical. */
static int lzmesh_i5_on(int level) {
    return level == 9;
}

/* Head compare: slot content cur vs i over hd bytes. EMPTY/OOB -> differ. */
static int lzmesh_i5_heq(const uint8_t *src, size_t size, uint32_t cur,
                         size_t i, unsigned hd) {
    if (cur == LZMESH_U2_EMPTY || (size_t)cur + hd > size || i + hd > size)
        return 0;
    return lzmesh_u2_head_eq(src + cur, src + i, hd); /* P5-W3 word cmp */
}

/* O3 narrow opt-outs (default 1 = narrow on; 0 restores pre-O3 bytes).
 * P2-GETENV: single call site (TMTLO) folded into a cached gate. */
static int lzmesh_o3_tmtlo_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_O3_TMTLO");
        init = 1;
        on = (e == NULL || e[0] == '\0') ? 1 : atoi(e) != 0;
    }
    return on;
}

/* I5: trace writes to one slot (LZMESH_I5_SLOT; T4: all tables). */
static int lzmesh_t4_in_drain = 0; /* T4: set during queue replay. */
/* P5-HOIST: filter cache at file scope (was fn-static); parse verbatim incl
 * empty-string arms slot 0 (e != NULL, no e[0] check). */
static int lzmesh_i5_tr_init = 0, lzmesh_i5_tr_have = 0;
static long lzmesh_i5_tr_slot = -1L;
/* R2-ENC9 A4: fuse (per-store trace gate; cold fprintf stays outline). */
static inline __attribute__((always_inline)) void lzmesh_i5_tr_parse(void) {
    if (!lzmesh_i5_tr_init) {
        const char *e = getenv("LZMESH_I5_SLOT");
        lzmesh_i5_tr_init = 1;
        if (e != NULL) {
            lzmesh_i5_tr_have = 1;
            lzmesh_i5_tr_slot = atol(e);
        }
    }
}
static int lzmesh_i5_tr_armed(void) {
    lzmesh_i5_tr_parse();
    return lzmesh_i5_tr_have;
}
/* R2-ENC9 A4: fuse (was outline x6 in parse, executed per store). */
static inline __attribute__((always_inline)) void
lzmesh_i5_tr(uint32_t s, size_t pos, const char *op) {
    /* P2-GETENV: slot filter parsed once (read-once-at-first-call). */
    lzmesh_i5_tr_parse();
    if (lzmesh_i5_tr_have && (uint32_t)lzmesh_i5_tr_slot == s)
        fprintf(stderr, "I5SLOT %s%s s=%u pos=%u\n", op,
                lzmesh_t4_in_drain ? "D" : "", s, (unsigned)pos);
}

/* P5-HOIST fwd decls (defined below; cached gates snapshotted at parse-top). */
static int lzmesh_veb_tc(void);
static int lzmesh_f2_keyed_on(void);
static int lzmesh_f3_storeg_on(void);
static int lzmesh_veb_nostore_armed(void);
/* P18-FINDER S-SPANHOIST fwd decls (defs below; snapshot at mode init). */
static int lzmesh_upins_lega_on(void);
static int lzmesh_t4_h3_on(void);
static int lzmesh_o3_tmtlo_on(void);
static uint32_t lzmesh_f1_tmlen(void);

/* I5 store-rule modes (env parsed once per encode; I5-GRID-TEMP knobs). */
typedef struct {
    int run;   /* LZMESH_I5_RUN bit0=D,bit1=S */
    int h1;    /* LZMESH_I5_H1: h1 mirror alongside h2 (default 1) */
    int p0;    /* LZMESH_I5_P0: never overwrite slot 0 (default 0) */
    int rsp;   /* LZMESH_I5_SPAN: big rule 0..5 (default 5, M3) */
    int rss;   /* LZMESH_I5_SSPAN: small rule 0..5 (default 5, L4) */
    uint32_t *f3skip; /* F3: per-h3slot pos skipped by gate (NULL off) */
    unsigned char *ycon; /* YF: per-pos take-consumed mark (NULL off) */
    unsigned char *ywin; /* YF: per-pos winpos (queried+won) mark (NULL off) */
    int u9;    /* LZMESH_U9_SKIP: M3-h2 narrow 0=stock 1=itm4 2=tendi3 3=td8
                * 4=1|2 5=td8+itm2 6=td8+itm2+tendi4 (default 6, U9) */
    /* P5-HOIST: default-off gate snapshot (read-once values, taken once per
     * parse; hot legs branch on these, zero predicate calls when off). */
    int ho_f2; /* LZMESH_F2_KEYED */
    int ho_f3; /* LZMESH_F3_STOREG */
    int ho_tc; /* LZMESH_VEB_TC bitmask */
    int ho_ns; /* LZMESH_VEB_NOSTORE list non-empty */
    int ho_trh; /* LZMESH_I5_SLOT set (any value incl empty) */
    /* P18-FINDER S-SPANHOIST (extends P5-HOIST): j5-joint cached gates,
     * same read-once values any later per-pos call would read. */
    int ho_lega; /* LZMESH_UPINS_LEGA */
    int ho_t4h3; /* T4 h3 tlen gate */
    int ho_o3; /* LZMESH_O3_TMTLO */
    uint32_t f1cap; /* LZMESH_F1_TMLEN */
} lzmesh_i5_mode;
static int lzmesh_i5_env(const char *name, int dflt) {
    const char *e = getenv(name);
    return e != NULL ? atoi(e) : dflt;
}
static void lzmesh_i5_mode_init(lzmesh_i5_mode *md) {
    md->run = lzmesh_i5_env("LZMESH_I5_RUN", 0);
    md->h1 = lzmesh_i5_env("LZMESH_I5_H1", 1);
    md->p0 = lzmesh_i5_env("LZMESH_I5_P0", 0);
    md->rsp = lzmesh_i5_env("LZMESH_I5_SPAN", 5);
    md->rss = lzmesh_i5_env("LZMESH_I5_SSPAN", 5);
    md->f3skip = NULL;
    md->ycon = NULL;
    md->ywin = NULL;
    md->u9 = lzmesh_i5_env("LZMESH_U9_SKIP", 6);
    /* P5-HOIST: snapshot read-once gate values (same values any later per-pos
     * call would read; read-once-at-first-call semantics unchanged). */
    md->ho_f2 = lzmesh_f2_keyed_on();
    md->ho_f3 = lzmesh_f3_storeg_on();
    md->ho_tc = lzmesh_veb_tc();
    md->ho_ns = lzmesh_veb_nostore_armed();
    md->ho_trh = lzmesh_i5_tr_armed();
    /* P18-FINDER S-SPANHOIST (extends P5-HOIST). */
    md->ho_lega = lzmesh_upins_lega_on();
    md->ho_t4h3 = lzmesh_t4_h3_on();
    md->ho_o3 = lzmesh_o3_tmtlo_on();
    md->f1cap = lzmesh_f1_tmlen();
}
static int lzmesh_i5_isrun(const uint8_t *src, size_t size, size_t i) {
    return i + 1u < size && src[i] == src[i + 1u];
}
/* VEB NOSTORE (LANE-V-E09BULK): debug position-list skip of h3 stores.
 * LZMESH_VEB_NOSTORE="p1,p2,..." (cap 64): skip h3 store at listed input
 * positions on direct3+span3+flood3 legs. Pins-only diagnostic (positions
 * are input-specific); never shipped. L9-only via i5 call sites. */
/* P5-HOIST: list cache at file scope (was fn-static); parse verbatim. */
static int lzmesh_veb_ns_init = 0;
static size_t lzmesh_veb_ns_list[64];
static unsigned lzmesh_veb_ns_n = 0;
static void lzmesh_veb_ns_parse(void) {
    if (!lzmesh_veb_ns_init) {
        const char *e = getenv("LZMESH_VEB_NOSTORE");
        lzmesh_veb_ns_init = 1;
        if (e != NULL && e[0] != '\0') {
            const char *p = e;
            while (*p != '\0' && lzmesh_veb_ns_n < 64u) {
                while (*p == ' ' || *p == ',')
                    p++;
                if (*p == '\0')
                    break;
                lzmesh_veb_ns_list[lzmesh_veb_ns_n++] = (size_t)atol(p);
                while (*p != '\0' && *p != ',')
                    p++;
            }
        }
    }
}
static int lzmesh_veb_nostore_armed(void) {
    lzmesh_veb_ns_parse();
    return lzmesh_veb_ns_n != 0u;
}
/* P23-MIRROR V5: lzmesh_veb_nostore deleted (last callers were the span/
 * flood/direct ns gates, removed; ns_parse/list/n kept for ho_ns). */
/* VEB T-COMB (LANE-V-E09BULK): narrow h3 diff-key skip rule.
 * Skip h3 store iff slot occupied by DIFF-KEY occupant AND incoming is
 * INT-off1 (bit 1), LIT-off EXACTLY 2 (bit 2), or take-start (bit 4).
 * Bedded: 9 K-chain shadowers skipped-by-oracle (KO-heal 9/9 port-side,
 * oracle reads older S); 11 F2-must-store upstream overwrites excluded
 * (role-overlap kills broader cuts: LIT6/MSTART/INT2/INT3 all fire both
 * ways). Env LZMESH_VEB_TC bitmask (default 0 = ship; 4/6 restore).
 * L9-only via i5. Wave-V: full 32->30 NEW 0 (FLIP s08 + s09-sp e09),
 * holdout 8=8 NEW 0.
 * Z-E09FINDER (LANE-Z-E09FINDER): LIT-off2 leg (value 2) REMOVED from
 * default (6->4): it skips oracle-visible S stores (underfire s03/s04/
 * s07small/s12/s13: S TC-skipped early, lands one query late) and
 * shadower stores (overfire s07big/s09/s11). Bedded: takes-frame knob
 * bisection 8/8 (TC=0/4 heal o-shape EXACT, TC=2 = stock; T4/U3/SVISG/
 * L9CUT no effect) + full battery 10->8 NEW 0 (FLIP s07small + s11 e09).
 * Take-start leg (value 4) kept (wave-V s08/s09-sp flips hold under 4).
 * LZMESH_VEB_TC=6 restores the leg. L9-only.
 * Z-S13TAKE (LANE-Z-S13TAKE): take-start leg (value 4) REMOVED from
 * default (4->0): wave-V flips no longer need it (s08: 0 TC fires;
 * s09-sp: 327 fires fully inert; TC0/TC4/TC6 ALL byte-IDENT on both,
 * 7-knob TC0 x SVISG x L9CUT bisect ALL IDENT) while it costs s13
 * 34B (gap 35->1) via 867 takes-diffs (TC0-right 867/867, 0 TC4-right;
 * stale-cur + missing-S cascades, no store-side victim separator).
 * Battery footprint = s13 ONLY (7/7 other full rows + 7/7 holdout rows
 * byte-SAME). Subsuming always-on change wave-V..Y unidentified (open
 * Q-z-s13take-2 ask-1). LZMESH_VEB_TC=4 restores the leg. L9-only.
 * Take-start/LIT-off read the i5ts take-start bitmap (forward-marked at
 * take record: start[k+1] = end[k]); INT-off1 reads i5m[i-1]. NULL
 * bitmaps (drain-pm path) disable the legs there (match starts only;
 * T-COMB never targets match starts). */
static int lzmesh_veb_tc(void) {
    static int init = 0, tc = 0; /* Z-S13TAKE: was 4 (drop take-start) */
    if (!init) {
        const char *e = getenv("LZMESH_VEB_TC");
        init = 1;
        if (e != NULL && e[0] != '\0')
            tc = atoi(e);
    }
    return tc;
}
/* P23-MIRROR V5: lzmesh_veb_tctr + lzmesh_veb_tc_skip deleted (last
 * callers were the span/flood/direct tc gates, removed; T-COMB history in
 * the comment above and git). */
/* F2 leg-D trial (H-KEYED-STICKY diff-key leg; A-U9-2 s07 + A-lane-4-2
 * s08/s14): skip h3 small-table overwrite iff slot occupied AND head3
 * differs (first-writer-wins on diff-key pairs; same-key untouched).
 * L9-only via i5 call sites. Env LZMESH_F2_KEYED=1 (default 0 = stock). */
static int lzmesh_f2_keyed_on(void) {
    static int init = 0, on = 0; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_F2_KEYED");
        init = 1;
        on = (e != NULL && atoi(e) != 0);
    }
    return on;
}
/* F3 store-gate v2 (ROUND-F3; lane BED-STORECTX + killer-pair narrow).
 * Skip h3 small-table diff-key overwrite iff incoming is at take-start
 * (litrun==0: run-start/MSTART-adjacent) or INT-off1 (span/peeked-c cur-win).
 * v2-broad (litrun<=6) FALSIFIED lane-side: 768-off1 + 269-off2 are
 * shallow-but-STORED (s08#69/s07#15 breaks); offset/role/content ALL dead
 * (off1/off2 both ways, INT-1 both ways, no head pattern). K=0 keeps the
 * s09-9873 skip (take-start) and drops every observed over-skip. Empty
 * slots always store; same-key untouched; big table untouched. L9-only.
 * Env LZMESH_F3_STOREG=1 (off default; ON for ship per battery). */
static int lzmesh_f3_storeg_on(void) {
    static int init = 0, on = 0; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_F3_STOREG");
        init = 1;
        on = (e != NULL && atoi(e) != 0);
    }
    return on;
}
/* P30-CHAIN: W9 qpick/qtrace/taketrace + walk + qlink deleted. Chain
 * dead: qlink never written since P23 V5 (w9_prev_record deleted);
 * forced-QPICK vs base 0/12784 DIV + 0 takes FULL-bedded (HINT-P29-
 * MFCHAIN sec1; tmp/p30chain/qpick_full.tsv). YF kept verbatim. */
/* I5 direct (L9): h3+h2 unconditional, +h1 iff H1 on. le = last take end
 * (litrun at pos = pos-le; le>=pos forces litrun 0; le=0 keeps stock for
 * unknown contexts since pos>0 reads deep). */
static inline __attribute__((always_inline)) void
lzmesh_i5_direct(uint32_t *big, uint32_t *small,
                             const uint8_t *src, size_t size, size_t pos,
                             unsigned hb, size_t le,
                             const lzmesh_i5_mode *md,
                             const unsigned char *i5m,
                             const unsigned char *i5ts) {
    /* P23-MIRROR V5d: le/i5m/i5ts vestigial (rule-only params). */
    (void)le;
    (void)i5m;
    (void)i5ts;
    /* R6-T9: fast path (pos+8<=size, trace off): size gates + trace
     * gates provably dead; stores verbatim. Tail/trace run stock. */
    if (!md->ho_trh && pos + 8u <= size) {
        /* R8-L9NEW C2: one u64 feeds h3/h2/h1 (P11 pattern;
         * pos+8<=size proves the 8B read in-bounds).
         * R8-L9NEW R-e: branchless h1 leg (csel slot: h1==0
         * re-stores big[s]=pos idempotently; same value). */
        uint64_t w8 = lzmesh_wl_ld64(src + pos);
        uint32_t s3 = lzmesh_u2_h3((uint32_t)(w8 & 0xFFFFFFu));
        uint32_t s = lzmesh_u2_h2(w8 & 0xFFFFFFFFFFull, hb);
        uint32_t s1 = lzmesh_u2_h1(w8 & 0xFFFFFFFFFFFFFFull, hb);
        small[s3] = (uint32_t)pos;
        big[s] = (uint32_t)pos;
        big[md->h1 ? s1 : s] = (uint32_t)pos;
        return;
    }
    if (pos + 3u <= size) {
        uint32_t s3 = lzmesh_u2_h3(
            (uint32_t)lzmesh_u2_load_n(src + pos, 3u));
        /* P23-MIRROR V5d: direct3 unconditional (gates + redirect + w9
         * dropped; all dead on text/mixed/dense; FULL gate decides). */
        if (md->ho_trh)
            lzmesh_i5_tr(s3, pos, "direct3");
        small[s3] = (uint32_t)pos;
    }
    if (pos + 5u <= size) {
        uint32_t s = lzmesh_u2_h2(lzmesh_u2_load_n(src + pos, 5u), hb);
        if (md->ho_trh)
            lzmesh_i5_tr(s, pos, "direct");
        big[s] = (uint32_t)pos;
    }
    if (md->h1 && pos + 7u <= size) {
        uint32_t s1 = lzmesh_u2_h1(lzmesh_u2_load_n(src + pos, 7u), hb);
        if (md->ho_trh)
            lzmesh_i5_tr(s1, pos, "direct1");
        big[s1] = (uint32_t)pos;
    }
}

/* YF fwd decl (defined below; LANE-Y-E09FINDER L9 cut gate). */
static int lzmesh_yf_l9cut_on(void);
/* T4 fwd decl (defined below; LANE-T4 h3 tlen gate). */
static int lzmesh_t4_h3_on(void);

/* F1: take-m-cur tlen cap (LANE-F1). J5 take-m SKIP beds tlen 3 only
 * (6/7 beds are the c2-empty arm: occupants not take-m); T4 tlen<=7
 * over-fires (s14-n1025@233 tlen4, s15-n1024@647 tlen5 STORE). Cap 3
 * (default); LZMESH_F1_TMLEN=7 restores old. L9-only via r5 callers. */
static uint32_t lzmesh_f1_tmlen(void) {
    static int init = 0;
    static uint32_t cap = 3u;
    if (!init) {
        const char *e = getenv("LZMESH_F1_TMLEN");
        if (e != NULL && e[0] != '\0') {
            long v = atol(e);
            cap = (v < 0) ? 0u : (uint32_t)v;
        }
        init = 1;
    }
    return cap;
}

/* P2-GETENV: cached span-path gates (read-once-at-first-call). */
static int lzmesh_upins_lega_on(void) {
    static int init = 0, on = 0;
    if (!init) {
        const char *e = getenv("LZMESH_UPINS_LEGA");
        init = 1;
        on = (e != NULL && atoi(e) != 0);
    }
    return on;
}
/* P23-MIRROR V5b: lzmesh_u9_skipsites_on deleted (only caller was the
 * span s2 U9 arm, removed above). git history restores it. */
/* I5 span (L9): rule selected by LZMESH_I5_SPAN / LZMESH_I5_SSPAN.
 * 0=overwrite (h2 default), 1=keep-old-on-head-eq, 2=+visited, 3=+take-m,
 * 4=current-source (develop), 5=j5 joint (h3 default since L4).
 * h1 follows h2 verdict iff H1 on. Rule 5 (J5): h3 skips end-1 run-heads
 * and, mid-span, joint (h2-miss + take-len 6/7 + direct take +
 * take-dist <= 19 + occupant != span-start (L4)) or take-m cur,
 * all head-eq gated; h2 mirrors the mid joint on empty slots with
 * 8 <= take-dist <= 19 (M3; s00 needs td8, s11 over-fires td1).
 * L4: occupant==tlo is own span-start (agreed-fresh) => STORE.
 * O3: take-m skip iff occupant != tlo (s05-n129 stores @19;
 * s04-n46 needs cur==tm @4). L9-only via i5_span call sites.
 * Q1: h3 end-1 skip iff runlen>=4 (n=15 natural 3|4 exact + RIG2 causal
 * s14@25 rl3->5 STORE->SKIP at fixed EMPTY slot/geometry; EMPTY dead).
 * h3 path only; h2 end-1 untouched (boundary open, filed). L9-only. */
/* R2-ENC9 A4: fuse (de-outlined x7 in parse after catchup fuse; sole caller). */
static inline __attribute__((always_inline)) void
lzmesh_i5_span(uint32_t *big, uint32_t *small,
                           const uint8_t *src, size_t size,
                           size_t span_lo, size_t end,
                           unsigned hb, const unsigned char *i5v,
                           const unsigned char *i5m, uint32_t tdist,
                           size_t tm, size_t tlo,
                           const lzmesh_i5_mode *md) {
    /* P18-FINDER S-SPANHOIST: loop-inside (was 1 call/pos from catchup);
     * take-consts hoisted; per-pos body verbatim.
     * P23-MIRROR V5b: rule consts deleted with the rules (below). */
    (void)i5v;
    (void)i5m;
    (void)tdist;
    (void)tm;
    (void)tlo;
    size_t i;
    /* R5-QBR: hoist loop-invariant run-gate out of the span loop. */
    int qbr_run = ((md->run) & 2) ? 1 : 0;
    /* R6-T9: trace-split + size-split. Trace arm = stock verbatim.
     * Clean arm: main [span_lo,gm) gateless (i+7<=size proven), tail
     * [gm,end) size-gated; stores + run-gate verbatim in both. */
    if (md->ho_trh) {
        for (i = span_lo; i < end; i++) {
            if (qbr_run && lzmesh_i5_isrun(src, size, i))
                continue;
            if (i + 3u <= size) {
                uint32_t s = lzmesh_u2_h3(
                    (uint32_t)lzmesh_u2_load_n(src + i, 3u));
                lzmesh_i5_tr(s, i, "span3");
                small[s] = (uint32_t)i;
            }
            if (i + 5u <= size) {
                uint32_t s = lzmesh_u2_h2(lzmesh_u2_load_n(src + i, 5u),
                                          hb);
                lzmesh_i5_tr(s, i, "span");
                big[s] = (uint32_t)i;
                if (md->h1 && i + 7u <= size) {
                    uint32_t s1 =
                        lzmesh_u2_h1(lzmesh_u2_load_n(src + i, 7u), hb);
                    lzmesh_i5_tr(s1, i, "span1");
                    big[s1] = (uint32_t)i;
                }
            }
        }
        return;
    }
    /* R7-IRA span razor on the R6-T9 clean arm (mix9 862c9eb52 carried
     * onto shipped text9; trace arm above stays T9-verbatim, never
     * re-collided). Cold (run=1) runs the T9 gm main/tail verbatim;
     * hot (run=0, default) drops the run check and versions the main
     * on h1, with e7/e5/e3 tail ranges. Exact: run/trh/h1 are
     * parse-start snapshots (P5-HOIST), loop-invariant; e7 == T9 gm
     * (same min(end,size-6)/edge formula); e7<=e5<=e3 partition by
     * which bounds hold (i+7<=size => i+5,i+3 hold; mid/tail
     * i>=size-7 kills the h1 leg incl its flag check); per-i leg
     * order (h3,h2,h1), hashes and stores verbatim; [e3,end) is a
     * no-op in T9 (all bounds fail, run check false). */
    if (qbr_run) {
        size_t gm = end;
        /* Main/tail split: i+7<=size for all i<gm (i.e. gm<=size-6
         * when size>=7; size<7 runs all-tail). gm clamps to
         * [span_lo,end]: main empty + tail full at the edges. */
        if (size >= 7u) {
            size_t g = size - 6u;
            if (gm > g)
                gm = g;
        } else {
            gm = span_lo;
        }
        if (gm < span_lo)
            gm = span_lo;
        for (i = span_lo; i < gm; i++) {
            /* R8-L9NEW C2: one u64 feeds h3/h2/h1 (P11 query-side
             * pattern; LE low bytes equal load_n bit-for-bit;
             * i+7<=size proven by gm, so the 8B read is in-bounds). */
            uint64_t w8;
            uint32_t s, s3;
            if (qbr_run && lzmesh_i5_isrun(src, size, i))
                continue;
            w8 = lzmesh_wl_ld64(src + i);
            s3 = lzmesh_u2_h3((uint32_t)(w8 & 0xFFFFFFu));
            s = lzmesh_u2_h2(w8 & 0xFFFFFFFFFFull, hb);
            small[s3] = (uint32_t)i;
            big[s] = (uint32_t)i;
            if (md->h1) {
                uint32_t s1 =
                    lzmesh_u2_h1(w8 & 0xFFFFFFFFFFFFFFull, hb);
                big[s1] = (uint32_t)i;
            }
        }
        for (i = gm; i < end; i++) {
            if (qbr_run && lzmesh_i5_isrun(src, size, i))
                continue;
            if (i + 3u <= size) {
                uint32_t s = lzmesh_u2_h3(
                    (uint32_t)lzmesh_u2_load_n(src + i, 3u));
                small[s] = (uint32_t)i;
            }
            if (i + 5u <= size) {
                uint32_t s = lzmesh_u2_h2(lzmesh_u2_load_n(src + i, 5u),
                                          hb);
                big[s] = (uint32_t)i;
                if (md->h1 && i + 7u <= size) {
                    uint32_t s1 =
                        lzmesh_u2_h1(lzmesh_u2_load_n(src + i, 7u), hb);
                    big[s1] = (uint32_t)i;
                }
            }
        }
    } else {
        /* Exclusive ends: i+3<=size <=> i<size-2 (size>=3); tiny
         * sizes default to span_lo (no i qualifies; no underflow;
         * each size-N term short-circuits before size-(N-1)). */
        size_t e3 = (size >= 3u && end > size - 2u) ? size - 2u : end;
        size_t e5 = (size >= 5u && end > size - 4u) ? size - 4u : end;
        size_t e7 = (size >= 7u && end > size - 6u) ? size - 6u : end;
        if (size < 3u)
            e3 = span_lo;
        if (size < 5u)
            e5 = span_lo;
        if (size < 7u)
            e7 = span_lo;
        i = span_lo;
        if (e7 < span_lo)
            e7 = span_lo;
        if (e5 < e7)
            e5 = e7;
        if (e3 < e5)
            e3 = e5;
        /* R8-L9NEW C2: one u64 feeds h3/h2/h1 in the mains below
         * (P11 pattern; i+7<=size proven by e7). Tails keep stock
         * load_n (8B read unsafe there; <=4 iters each). */
        if (md->h1) {
            for (; i < e7; i++) {
                uint64_t w8 = lzmesh_wl_ld64(src + i);
                uint32_t s = lzmesh_u2_h3(
                    (uint32_t)(w8 & 0xFFFFFFu));
                small[s] = (uint32_t)i;
                s = lzmesh_u2_h2(w8 & 0xFFFFFFFFFFull, hb);
                big[s] = (uint32_t)i;
                s = lzmesh_u2_h1(w8 & 0xFFFFFFFFFFFFFFull, hb);
                big[s] = (uint32_t)i;
            }
        } else {
            for (; i < e7; i++) {
                uint64_t w8 = lzmesh_wl_ld64(src + i);
                uint32_t s = lzmesh_u2_h3(
                    (uint32_t)(w8 & 0xFFFFFFu));
                small[s] = (uint32_t)i;
                s = lzmesh_u2_h2(w8 & 0xFFFFFFFFFFull, hb);
                big[s] = (uint32_t)i;
            }
        }
        for (; i < e5; i++) {
            uint32_t s = lzmesh_u2_h3(
                (uint32_t)lzmesh_u2_load_n(src + i, 3u));
            small[s] = (uint32_t)i;
            s = lzmesh_u2_h2(lzmesh_u2_load_n(src + i, 5u), hb);
            big[s] = (uint32_t)i;
        }
        for (; i < e3; i++) {
            uint32_t s = lzmesh_u2_h3(
                (uint32_t)lzmesh_u2_load_n(src + i, 3u));
            small[s] = (uint32_t)i;
        }
    }
}

/* I5 flood (L9): [fup,ins) h3/h2 unconditional, except init pos0 slots
 * are never overwritten (s13-n60/s16-n57 pin this). +h1 iff H1 on. Returns ins.
 * P23-MIRROR V5c: kd/f3/ns/tc gates deleted (all dead on text/mixed/
 * dense); F3 verdict honoring removed with them (FULL gate decides). */
static size_t lzmesh_i5_flood(uint32_t *big, uint32_t *small,
                              const uint8_t *src, size_t size,
                              size_t fup, size_t ins, unsigned hb,
                              const lzmesh_i5_mode *md,
                              const unsigned char *i5m,
                              const unsigned char *i5ts) {
    /* P23-MIRROR V5c: i5m/i5ts vestigial (rule-only params). */
    (void)i5m;
    (void)i5ts;
    size_t i;
    /* R6-T9: trace-split + size-split. Trace arm = stock verbatim.
     * Clean arm: main [fup,gm) gateless (i+7<=size proven), tail
     * [gm,ins) size-gated; pos0 guards + stores verbatim in both. */
    if (md->ho_trh) {
        for (i = fup; i < ins; i++) {
            if (i + 3u <= size) {
                uint32_t s = lzmesh_u2_h3(
                    (uint32_t)lzmesh_u2_load_n(src + i, 3u));
                if (small[s] != 0u) {
                    lzmesh_i5_tr(s, i, "flood3");
                    small[s] = (uint32_t)i;
                }
            }
            if (i + 5u <= size) {
                uint32_t s = lzmesh_u2_h2(lzmesh_u2_load_n(src + i, 5u),
                                          hb);
                if (big[s] != 0u) {
                    lzmesh_i5_tr(s, i, "flood2");
                    big[s] = (uint32_t)i;
                }
            }
            if (md->h1 && i + 7u <= size) {
                uint32_t s = lzmesh_u2_h1(lzmesh_u2_load_n(src + i, 7u),
                                          hb);
                if (big[s] != 0u) {
                    lzmesh_i5_tr(s, i, "flood1");
                    big[s] = (uint32_t)i;
                }
            }
        }
        return ins;
    }
    /* R7-IRA flood razor on the R6-T9 clean arm (mix9 862c9eb52 carried
     * onto shipped text9; trace arm above stays T9-verbatim, never
     * re-collided). Hot loops version the main on h1 and split the
     * tail into e7/e5/e3 ranges. Exact: h1/trh are parse-start
     * snapshots (P5-HOIST), loop-invariant; e7 == T9 gm; ranges
     * partition by which bounds hold (mid/tail i>=size-7 kills the
     * h1 leg incl its flag check); per-i leg order (h3,h2,h1),
     * pos0 guards, hashes and stores verbatim; [e3,ins) is a no-op
     * in T9 (all bounds fail).
     * R8-L9NEW C1: pos0-guard hoist. The per-pos `slot[x] != 0u`
     * guards become one per-call snapshot + conditional restore.
     * Exact: (a) value 0 in L9 big/small is written ONLY by the
     * parse-top pos0-direct (all other L9 writers store i/pos/m/pm
     * >= 1: loop pos starts 1 and J4 backs to >= ins >= 1; span_lo
     * = m|m+1 >= 1; fup >= 1; skipback >= pos+1), so a slot holds
     * 0 at flood entry iff it is a never-overwritten pos0 slot.
     * (b) A 0-at-entry slot stays 0 under the guarded loop (every
     * would-be write to it is skipped; later writes are >= 1 so
     * no new 0 appears), and a nonzero-at-entry slot takes every
     * write (guards never fire). So guarded-loop == unconditional
     * loop + restore-0-to-entry-0-slots. (c) No query observes
     * mid-flood tables (flood runs inside the !chave skip arm; the
     * next query is a later loop iteration). (d) Snapshot
     * conditions replicate the pos0-direct write conditions
     * exactly (h3 iff 3<=size, h2 iff 5<=size, h1 iff h1&&7<=size),
     * so every entry-0 slot is snapshotted; aliasing (s1==s2)
     * restores the same value twice. Trace arm above untouched. */
    {
        /* Exclusive ends: i+3<=size <=> i<size-2 (size>=3); tiny
         * sizes default to fup (no i qualifies; no underflow). */
        size_t e3 = (size >= 3u && ins > size - 2u) ? size - 2u : ins;
        size_t e5 = (size >= 5u && ins > size - 4u) ? size - 4u : ins;
        size_t e7 = (size >= 7u && ins > size - 6u) ? size - 6u : ins;
        size_t i = fup;
        uint32_t c1_s3 = 0u, c1_s2 = 0u, c1_s1 = 0u;
        int c1_r3 = 0, c1_r2 = 0, c1_r1 = 0;
        if (size < 3u)
            e3 = fup;
        if (size < 5u)
            e5 = fup;
        if (size < 7u)
            e7 = fup;
        if (e7 < fup)
            e7 = fup;
        if (e5 < e7)
            e5 = e7;
        if (e3 < e5)
            e3 = e5;
        if (ins > fup) {
            if (3u <= size) {
                c1_s3 = lzmesh_u2_h3(
                    (uint32_t)lzmesh_u2_load_n(src, 3u));
                c1_r3 = (small[c1_s3] == 0u);
            }
            if (5u <= size) {
                c1_s2 = lzmesh_u2_h2(lzmesh_u2_load_n(src, 5u), hb);
                c1_r2 = (big[c1_s2] == 0u);
            }
            if (md->h1 && 7u <= size) {
                c1_s1 = lzmesh_u2_h1(lzmesh_u2_load_n(src, 7u), hb);
                c1_r1 = (big[c1_s1] == 0u);
            }
        }
        /* R8-L9NEW C2: one u64 feeds h3/h2/h1 in the mains below
         * (P11 pattern; i+7<=size proven by e7). Tails keep stock
         * load_n (8B read unsafe there; <=4 iters each). */
        if (md->h1) {
            for (; i < e7; i++) {
                uint64_t w8 = lzmesh_wl_ld64(src + i);
                uint32_t s = lzmesh_u2_h3(
                    (uint32_t)(w8 & 0xFFFFFFu));
                small[s] = (uint32_t)i;
                s = lzmesh_u2_h2(w8 & 0xFFFFFFFFFFull, hb);
                big[s] = (uint32_t)i;
                s = lzmesh_u2_h1(w8 & 0xFFFFFFFFFFFFFFull, hb);
                big[s] = (uint32_t)i;
            }
        } else {
            for (; i < e7; i++) {
                uint64_t w8 = lzmesh_wl_ld64(src + i);
                uint32_t s = lzmesh_u2_h3(
                    (uint32_t)(w8 & 0xFFFFFFu));
                small[s] = (uint32_t)i;
                s = lzmesh_u2_h2(w8 & 0xFFFFFFFFFFull, hb);
                big[s] = (uint32_t)i;
            }
        }
        for (; i < e5; i++) {
            uint32_t s = lzmesh_u2_h3(
                (uint32_t)lzmesh_u2_load_n(src + i, 3u));
            small[s] = (uint32_t)i;
            s = lzmesh_u2_h2(lzmesh_u2_load_n(src + i, 5u), hb);
            big[s] = (uint32_t)i;
        }
        for (; i < e3; i++) {
            uint32_t s = lzmesh_u2_h3(
                (uint32_t)lzmesh_u2_load_n(src + i, 3u));
            small[s] = (uint32_t)i;
        }
        if (c1_r3)
            small[c1_s3] = 0u;
        if (c1_r2)
            big[c1_s2] = 0u;
        if (c1_r1)
            big[c1_s1] = 0u;
    }
    return ins;
}

/* I5 walk-skip backfill (L9): [lo,hi) h1+h2 unconditional (BIG-only). */
static void lzmesh_i5_skipback(uint32_t *big,
                               const uint8_t *src, size_t size,
                               size_t lo, size_t hi, unsigned hb,
                               const lzmesh_i5_mode *md) {
    size_t i;
    for (i = lo; i < hi; i++) {
        if (i + 5u <= size) {
            uint32_t s = lzmesh_u2_h2(lzmesh_u2_load_n(src + i, 5u),
                                      hb);
            if (md->ho_trh)
                lzmesh_i5_tr(s, i, "skip2");
            big[s] = (uint32_t)i;
        }
        if (i + 7u <= size) {
            uint32_t s = lzmesh_u2_h1(lzmesh_u2_load_n(src + i, 7u),
                                      hb);
            if (md->ho_trh)
                lzmesh_i5_tr(s, i, "skip1");
            big[s] = (uint32_t)i;
        }
    }
}

/* I5 catchup (L9): H1 shape, conditional span (h2/h3, no h1),
 * forward windows h2-only W8. L1 MX path verbatim (unreached at L9). */
/* R2-ENC9 A4: fuse (was outline x2 in parse + x1 in t4_drain). */
static inline __attribute__((always_inline)) void
lzmesh_i5_catchup(int32_t *head, int32_t *prev,
                              uint32_t *big, uint32_t *small,
                              unsigned char *vis,
                              const uint8_t *src, size_t size,
                              size_t ins, size_t m, size_t end,
                              unsigned hb, int level, size_t span_lo,
                              const unsigned char *i5v,
                              const unsigned char *i5m, uint32_t tdist,
                              const lzmesh_i5_mode *md) {
    size_t v = ins, i, nextv;
    unsigned w = (level == 1) ? LZMESH_H1_WIN_L1 : LZMESH_H1_WIN_GEN;
    size_t k;
    /* P17-FINDER win256 (same proof as h1_catchup): gap<=256 takes no
     * else-leg; vis[] is pure scratch. */
    int win256 = (m - ins > 256u) ? 1 : 0;
    if (win256) {
        for (k = ins; k < m; k++)
            vis[k] = 0u;
        while (v < m) {
            vis[v] = 1u;
            v += (size_t)1u + ((v - ins) >> 8);
        }
    }
    if (level == 1) {
        for (i = span_lo; i < end; i++)
            lzmesh_s2_mx_link(head, prev, src, size, i, hb); /* S2 */
    } else {
        /* P18-FINDER S-SPANHOIST: single range call (loop moved inside). */
        lzmesh_i5_span(big, small, src, size, span_lo, end, hb, i5v,
                       i5m, tdist, m, span_lo, md);
    }
    nextv = m;
    if (!win256) /* P17-FINDER win256: no else-leg fires (see above) */
        return;
    for (i = m; i > ins;) {
        --i;
        if (vis[i])
            nextv = i;
        else if (nextv - i <= (size_t)(w - 1u)) {
            if (level == 1) {
                lzmesh_s2_mx_link(head, prev, src, size, i, hb); /* S2 */
            } else if (i + 5u <= size) {
                uint32_t s = lzmesh_u2_h2(lzmesh_u2_load_n(src + i, 5u),
                                          hb);
                if (md->ho_trh)
                    lzmesh_i5_tr(s, i, "win2");
                big[s] = (uint32_t)i;
            }
        }
    }
}

/* === T4 L9 span-lag + peek-store (owner: T4; LANE-T4) ===
 * Bedded black-box on e09 textlike (tmp/t4 py; 46 cells):
 * - take catch-up span[pm+1..)+windows QUEUE; NEW-store drains, REP-store
 *   does not (gap0 span[0] HIT 31/31 prevNEW / MISS 10/10 prevREP / 6
 *   chal-prun1 MISS; older-span HIT 164/164; gap1 HIT 46/46 via flood).
 * - NEW take-m immediate (Q5 183/183 abut+gap); REP take-m queued (0/4).
 * - lazy n1-peek direct-stores peek pos (offset-0-only visibility).
 * - L9 peek drains (L3-shape; s04-n257 take10 needs queued span at peek).
 * - skip-walk DRAINs (flood's pos0-guard skips init slots, so drain
 *   must replay queued writers there; s11-min h2-3056).
 * Verdicts evaluate at drain time (natural replay). L9-only; L0/L1/L5
 * byte-identical. Env LZMESH_T4_LAG=0 / LZMESH_T4_PEEK=0 bisect (opt-outs). */
typedef struct {
    size_t ins, m, end, span_lo; /* i5_catchup range args */
    uint32_t tdist; /* take dist (span verdicts) */
    size_t pm; /* queued rep-m direct store */
    int pmok;
} lzmesh_t4_pend;
typedef struct {
    lzmesh_t4_pend *v;
    size_t n;
    size_t cap;
} lzmesh_t4_q;
static void lzmesh_t4_free(lzmesh_t4_q *q) {
    free(q->v);
    q->v = NULL;
    q->n = 0u;
    q->cap = 0u;
}
static int lzmesh_t4_lag_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_T4_LAG");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
static int lzmesh_t4_peekdrain_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_T4_PEEKDRAIN");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
static int lzmesh_t4_sbduel_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_T4_SBDUEL");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
/* T4: L9 hash-chal sb-duel: 4*Dlen - Dsb - 4 > 0 (strict; ties stay).
 * LANE-T4: 397/397 agree-chals sb-win, 1254/1254 agree-directs
 * sb-tie/loss (bitlen-score splits margin-1 pairs + s07/s11 ties wrong). */
static int lzmesh_t4_sb_win(uint32_t nlen, uint32_t ndist,
                            uint32_t clen, uint32_t cdist) {
    int64_t dl = (int64_t)nlen - (int64_t)clen;
    int64_t ds = (int64_t)lzmesh_u3_sb_of(ndist)
        - (int64_t)lzmesh_u3_sb_of(cdist);
    return (int64_t)4 * dl - ds - 4 > 0;
}
static int lzmesh_t4_hash_win(int level, uint32_t hlen, uint32_t hdist,
                              uint32_t clen, uint32_t cdist,
                              uint32_t litrun, int64_t sc, int sbduel) {
    /* H1 (r27-flex3): hlen<=clen && hdist>=cdist ==> LOSS on every duel
     * path (sb/score/cost monotone + -4 strict margin); skip the duel. */
    if (hlen <= clen && hdist >= cdist)
        return 0;
    if (level == 9 && sbduel)
        return lzmesh_t4_sb_win(hlen, hdist, clen, cdist);
    return lzmesh_u3_score(hlen, hdist, litrun + 1u) > sc;
}
static int lzmesh_t4_h3_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_T4_H3");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
static int lzmesh_t4_peek_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_T4_PEEK");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
/* TC: r0-win peek-store skip (LANE-T-CONSUME). Skip iff r0 take dist>19
 * (M3-window top): near rep wins (td<=19) keep the peek-store (s05-n129
 * len2/d5 peek@13 must store), far rep wins skip it (s10 len6/d307
 * peek@19801 + s02-sp len2-far must skip; len does NOT discriminate).
 * The REP take-m still rides the T4 queue (pmok) in both arms.
 * LZMESH_TC_PEEKREP=0 restores stock. Default skip-on. L9-only. */
static int lzmesh_tc_peekrep_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_TC_PEEKREP");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
/* U3: short-NEW-FAR take span immediate (LANE-U3). Default K=3
 * (LZMESH_U3_IMM overrides, 0=off): L9 NEW take with clen<=K and
 * cdist>19 runs i5_catchup immediately instead of queueing (s10-n63
 * e09: m38 NEW L3 td21 span visible at query@41; queued span lands
 * one query late). REP takes always queue (T4). K=3 minimal (K>=5
 * breaks s10; K=4 untested-vs-3, 3 is conservative). td>19 gated by
 * s05-n129 (NEW L3 td8 must queue). */
static int lzmesh_u3_imm_k(void) {
    /* ZDUEL-IMM0 (LANE-Z-DUEL): default 0 (was 3). U3 immediate catchup
     * over-stores span positions the oracle queues (s09#2005: span-pos
     * 8925 imm-stored -> phantom d1-len5 beats o-shape d2157-len5;
     * KO@8925 oracle-side noop 5/5). s10-n63 raison retested clean 11/11
     * at TC4 base. Env LZMESH_U3_IMM=3 restores stock. */
    static int init = 0, k = 0;
    if (!init) {
        const char *e = getenv("LZMESH_U3_IMM");
        k = (e == NULL) ? 0 : atoi(e);
        init = 1;
    }
    return k;
}
static int lzmesh_t4_append(lzmesh_t4_q *q, const lzmesh_t4_pend *e) {
    if (q->n == q->cap) {
        size_t ncap = (q->cap == 0u) ? 16u : q->cap * 2u;
        lzmesh_t4_pend *nv =
            (lzmesh_t4_pend *)realloc(q->v, ncap * sizeof *nv);
        if (nv == NULL)
            return 0;
        q->v = nv;
        q->cap = ncap;
    }
    q->v[q->n] = *e;
    q->n++;
    return 1;
}
/* R2-ENC9 A4: fuse (was outline x3 in parse: L9 peek/skip/take drains). */
static inline __attribute__((always_inline)) void
lzmesh_t4_drain(int32_t *head, int32_t *prev, uint32_t *big,
                            uint32_t *small, unsigned char *vis,
                            const uint8_t *src, size_t size, unsigned hb,
                            int level, const unsigned char *i5v,
                            const unsigned char *i5m,
                            const lzmesh_i5_mode *md, lzmesh_t4_q *q) {
    size_t k;
    lzmesh_t4_in_drain = 1;
    for (k = 0u; k < q->n; k++) {
        lzmesh_t4_pend *p = &q->v[k];
        if (p->pmok)
            lzmesh_i5_direct(big, small, src, size, p->pm, hb, 0u, md,
                               NULL, NULL);
        lzmesh_i5_catchup(head, prev, big, small, vis, src, size,
                          p->ins, p->m, p->end, hb, level, p->span_lo,
                          i5v, i5m, p->tdist, md);
    }
    q->n = 0u;
    lzmesh_t4_in_drain = 0;
}

/* === J3 e05 P+2 rep0 leg (owner: J3; LANE-J3) ===
 * Hint-sec4 third challenger at L5 ONLY (L1/L9 byte-identical by
 * gate): rep0-only at P+2 (u3_lazy_decide +2 semantics, dead code
 * wired into the u37 L5 path): 4*(clen-n2len)+8 < cost0,
 * cost(d) = bitlen(d+7)+4. Cur is NEW here (rep-cur takes
 * immediately, E3). The +1 MATH replumb (rep/hash cost duel) is
 * parse-proven but keep-blocked (s02 pin; see LANE-J3) and NOT
 * shipped; +1 legs keep F5/G5 force + u3_score duel. */
static uint32_t lzmesh_j3_cost(uint32_t dist) {
    return lzmesh_u3_bitlen(dist + 7u) + 4u;
}
static int lzmesh_j3_p2_win(uint32_t clen, uint32_t cdist,
                            uint32_t n2len) {
    int64_t lhs = (int64_t)4 * ((int64_t)clen - (int64_t)n2len) + 8;
    /* M2 (LANE-M2): +2 floor n2len>=3 (was any len>=2). Bedded 8/8
     * oracle-STAY at n2len 2 (margins 1+2); corpus +2 takes min
     * n2len 3 (47 SKIP2). J3 beds (n2len 19) unaffected. */
    if (n2len < 3u)
        return 0;
    return lhs < (int64_t)lzmesh_j3_cost(cdist);
}

/* === L2 e05 MATH duel (owner: L2; LANE-L2) ===
 * J3 sec4 replumb on K1-keep: L5 ONLY (L1/L9 byte-identical).
 * rep <= / hash < / +2(J3 kept). cost(d)=bitlen(d+7)+4.
 * rep: 4*(clen-n1len)+4 < cost0 (ties stay; M1+M2: L2's <= rested
 * on J3-era take-ties, refuted on merged-L).
 * hash: cost1+4*(clen-n1len)+4 < cost0 (ties stay).
 * s02-SKIP declines via K1 est-margin (margin4). */
static uint32_t lzmesh_l2_cost(uint32_t dist) {
    return lzmesh_u3_bitlen(dist + 7u) + 4u;
}
static int lzmesh_l2_rep_win(uint32_t clen, uint32_t cdist,
                             uint32_t n1len) {
    int64_t lhs = (int64_t)4 * ((int64_t)clen - (int64_t)n1len) + 4;
    /* M1+M2: strict < at ties (HINT-BYTEEXACT-R1 sec4 as written).
     * L2's <= rested on J3-era take-ties: P8/P6I are STRICT takes,
     * corpus mines find 0 agreed take-ties (1305 IDENT-COMP cells;
     * 29 labeled ties all stay/hashwin) vs 36+27 stay-ties. L5-only. */
    return lhs < (int64_t)lzmesh_l2_cost(cdist);
}
static int lzmesh_l2_hash_win(uint32_t clen, uint32_t cdist,
                              uint32_t n1len, uint32_t n1dist) {
    int64_t lhs = (int64_t)lzmesh_l2_cost(n1dist)
        + (int64_t)4 * ((int64_t)clen - (int64_t)n1len) + 4;
    return lhs < (int64_t)lzmesh_l2_cost(cdist);
}

/* === M1 e05 40-gate (owner: M1; LANE-M1) + N4 L9 extension (LANE-N4) ===
 * HINT-BYTEEXACT-R1 sec4: len >= 40 takes immediately, no lazy.
 * Port's shared u3_long_take (>38, i.e. >=39) over-fires at L39 on L5:
 * bed tmp/m1/m1gatebed.py: L39 cur + winning rep0@+1 -> oracle SKIP1 6/6,
 * L40 control -> oracle STAY 6/6. L5-only; L1/L9 keep u3_long_take.
 * N4: same over-fire at L9 (s13-n100000-sparse e09: port L39-immediate
 * vs oracle SKIP1 for rep130; bed tmp/n4/n4gatebed.py L39 SKIP1 6/6 +
 * L40 STAY 6/6; corpus 33 strict-win L40+ agreed stays + 0 L39-live).
 * L1 keeps u3 (no lazy there anyway). */
static int lzmesh_m1_long_take(int level, uint32_t len) {
    if (level == 5 || level == 9)
        return len >= 40u;
    return lzmesh_u3_long_take(len);
}

/* === WPINS L9-J4-back1 (owner: w-pins; LANE-W-PINS) ===
 * J4 backext L9 leg, raw-back==1 only (default ON; LZMESH_WPINS_L9J4=0
 * restores the withheld-L9 behavior). S4 withheld the full L9 leg:
 * port over-stores C so it fires early and regresses (R748-e09
 * 847->844 vs o848; S4-1 reframes as bank, bank never landed).
 * Pins s11-n4097 (1089->1088 L4d924) + s06-n65536 (1611->1610 L4d66)
 * e09 both need raw-back==1 exactly; R748-class back-3 stays blocked.
 * All other J4 gates unchanged (hash-only cur, litrun>=1, short,
 * dist+8<=pos, cap-7 + litrun cap). */
static int lzmesh_wpins_l9j4_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_WPINS_L9J4");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
/* ZDUEL-J4LIT1 default ON (=0 restores stock L9 raw-back1-only). */
static int lzmesh_zduel_j4lit1_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_ZDUEL_J4LIT1");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}

/* WPINS L1-J4-long (experimental): allow J4 backext for long takes on
 * L1 (s01-n100000sp e01: D3-shadow miss@33294, long (179,17028)@33295;
 * back12/litrun1 -> (33294,180) = oracle). Compensation for the
 * finder miss, NOT the principled fix (w-store owns D3-continue).
 * Default ON for battery screen; ship iff NEW 0. */
static int lzmesh_wpins_l1j4long_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_WPINS_L1J4LONG");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}

/* === M4 L9 duel: +1-rep MATH + +2 leg (owner: M4; LANE-M4) ===
 * L9 ONLY (L1/L5 byte-identical by gate). Env LZMESH_M4_DUEL=0
 * restores the old score-duel/no-+2 behavior (opt-out).
 * +1 rep: 4*(clen-n1len)+4 < cost0 (STRICT: ties stay at L9;
 * s12-n9999/s15-n1024 e09 agreed-take at 8==8; L2's L5-<= does
 * NOT transfer). Replaces the u3_score rep fallback; F5/G5/J6
 * force legs kept (MATH subsumes cover-force: lhs<=4<8<=cost0).
 * Hash leg untouched (u3_score; s08-n512 contradicts hash-math).
 * +2: J3's p2_win at L9 (rep0-only; +1 legs lost first). n2 is
 * table-independent; the consumed P+1 lit is direct-stored. */
static int lzmesh_m4_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_M4_DUEL");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
/* U8: L9 challenger peeks see past the +9 tail bar (LANE-U8).
 * L9 ONLY (L1/L5 byte-identical by gate). Env LZMESH_U8_RELAX=0
 * restores relax=0 (opt-out). T2 gave L5 peeks relax=1 (J3 +2 leg
 * already 1; M4's L9 +2 port left 0). KEEP-trio root cause: s09
 * peek@39/47 + s15 peek@45/53 elig-barred (h=0, oracle len8
 * invisible); s14 +2 rep@45/53 barred (oracle REP-7 invisible).
 * Cur probes keep the bar (T2 load-bearing); width safety from
 * each caller's own guard (rep 2/4, slot 7/5/3). */
static int lzmesh_u8_relax_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_U8_RELAX");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
/* O4 rep-M guard opt-out (default on). LANE-O4. */
static int lzmesh_o4_on(void) {
    static int init = 0, on = 1;
    if (!init) {
        const char *e = getenv("LZMESH_O4_REPM");
        on = (e == NULL || atoi(e) != 0);
        init = 1;
    }
    return on;
}
static int lzmesh_m4_rep_win(uint32_t clen, uint32_t cdist,
                             uint32_t n1len) {
    int64_t lhs = (int64_t)4 * ((int64_t)clen - (int64_t)n1len) + 4;
    return lhs < (int64_t)lzmesh_l2_cost(cdist);
}

/* P2-GETENV: cached parse/build trace gates (read-once-at-first-call). */
static int lzmesh_t4_trace_on(void) {
    static int init = 0, on = 0;
    if (!init) {
        init = 1;
        on = (getenv("LZMESH_T4_TRACE") != NULL);
    }
    return on;
}
static int lzmesh_wpins_j4trace_on(void) {
    static int init = 0, on = 0;
    if (!init) {
        init = 1;
        on = (getenv("LZMESH_WPINS_J4TRACE") != NULL);
    }
    return on;
}
static int lzmesh_f1_dbg_on(void) {
    static int init = 0, on = 0;
    if (!init) {
        init = 1;
        on = (getenv("LZMESH_F1_DBG") != NULL);
    }
    return on;
}
/* Greedy + 1-step score-lazy parse. Emits one terminator token iff
 * trailing literals remain. Returns token count, 0 on overflow
 * (caller declines). */
static int lzmesh_ze01_l1cut_on(void);

/* R17-TL9 YF-HELPER (outline, noinline): YF consumed-mark with
 * inlined wide stores for <=16B (99.8% of takes, avg 9B; C2) and
 * memset fallback. Outline so u37_parse codegen stays a single bl
 * (same shape as base's compiler-memset call): L1/L5-neutral.
 * Same bytes (overlap exact, no overrun); FULL proves. */
__attribute__((noinline)) static void
lzmesh_r17_yf_mark(unsigned char *d, size_t n) {
    if (n <= 16u) {
        if (n >= 8u) {
            uint64_t o = 0x0101010101010101ull;
            memcpy(d, &o, 8);
            memcpy(d + n - 8u, &o, 8);
        } else if (n >= 4u) {
            uint32_t o4 = 0x01010101u;
            memcpy(d, &o4, 4);
            memcpy(d + n - 4u, &o4, 4);
        } else if (n >= 2u) {
            d[0] = 1u;
            d[1] = 1u;
            if (n == 3u)
                d[2] = 1u;
        } else {
            d[0] = 1u;
        }
    } else {
        memset(d, 1, n);
    }
}

static size_t lzmesh_u37_parse(const uint8_t *src, size_t size,
                               int32_t *head, int32_t *prev,
                               uint32_t *big, uint32_t *small, unsigned hb,
                               lzmesh_u37_tok *toks, size_t tokcap,
                               unsigned char *vis,
                               int level, const size_t *cuts,
                               size_t ncut) {
    uint32_t recent[4];
    size_t pos = 1u, ntok = 0u, ins = 1u;
    size_t last_m = 0u, last_end = 0u; /* I3: most recent take span */
    int last_rep = 0; /* I3: most recent take was rep (m-hide iff so) */
    size_t i5_fup = 1u; /* I5: flood watermark (L9 only) */
    lzmesh_i5_mode i5md;
    lzmesh_i5_mode_init(&i5md);
    uint32_t f3arr[LZMESH_U2_H3SIZE]; /* F3 skip record (per-call) */
    { /* tables are per-call too; init all-EMPTY */
        /* P8-T2: memset-class fill, same bytes/bounds. */
        memset(f3arr, 0xFF, sizeof f3arr);
        i5md.f3skip = f3arr;
    }
    unsigned char *i5v = NULL; /* I5: visited bitmap (L9 only) */
    unsigned char *i5m = NULL; /* I5-GRID-TEMP: take-m bitmap */
    unsigned char *i5ts = NULL; /* VEB: take-start bitmap (forward-marked) */
    unsigned char *ysp = NULL; /* YF: span+peek marks (2x size, L9 only) */
    unsigned char *stored = NULL; /* S4: L1 chain once-bitmap */
    uint32_t litrun = 0u;
    uint32_t globrun = 0u; /* F1: recorded run (never cut-reset; tiling) */
    size_t ncut_seen = 0u; /* F1: cuts at/below pos (J4 backtrack exact) */
    /* R5-QBR cutlive: the F1/YF cut-reset predicate (both per-pos sites
     * below) is loop-invariant (level/cuts/ncut params + cached env);
     * hoist once. Semantics verbatim: scan + ncut_seen/litrun untouched. */
    int qbr_cutlive = ((level == 5
                        || (level == 9 && lzmesh_yf_l9cut_on())
                        || (level == 1 && lzmesh_ze01_l1cut_on()))
                       && cuts != NULL && ncut > 0u)
                          ? 1
                          : 0;
    /* R6-T9 gate snapshot (P5/P18-HOIST precedent): the L9 lazy/take
     * env gates below are read-once cached, so sampling each once here
     * yields exactly the values any later per-pos call would read.
     * Hot arms branch on these locals (1 branch vs init+value calls).
     * L1/L5 call sites untouched. */
    int r6_u8 = lzmesh_u8_relax_on();
    int r6_m4 = lzmesh_m4_on();
    int r6_t4lag = lzmesh_t4_lag_on();
    int r6_t4pd = lzmesh_t4_peekdrain_on();
    int r6_t4peek = lzmesh_t4_peek_on();
    int r6_tcpr = lzmesh_tc_peekrep_on();
    int r6_zj4 = lzmesh_zduel_j4lit1_on();
    int r6_wj4 = lzmesh_wpins_l9j4_on();
    int r6_o4 = lzmesh_o4_on();
    int r6_u3k = lzmesh_u3_imm_k();
    /* R12-H123-H4: lazy-once (level loop-invariant; u3_lazy_on pure).
     * L1 (lazy==0) skips the m1_long_take call in the lazy gate below;
     * L5/L9 trade one lazy call for one local test. */
    int h4_lazy = lzmesh_u3_lazy_on(level);
    /* R13-TL1-H5: wpins L1J4LONG snapshot (cached env gate, read-once;
     * R6-T9 precedent: env fixed per run, snapshot invisible). */
    int h5_l1j4long = lzmesh_wpins_l1j4long_on();
    /* R8-L9NEW R-a/R-b: snapshot read-once trace/duel gates (T9 pattern). */
    int r6_t4tr = lzmesh_t4_trace_on();
    int r6_sbduel = lzmesh_t4_sbduel_on();
    /* N1 (r27-flex3): sc consumed only by the rep score arm +
     * hash score path, both unreachable unless L9 with m4/sbduel off
     * (L5 takes l2 arms; L1 has no lazy gates). loop-invariant. */
    int r6_sclive = (level == 9) && (!r6_m4 || !r6_sbduel);
    unsigned j;
    lzmesh_k2_q k2q; /* K2: T3 multi-pending FIFO (L5 only) */
    k2q.v = NULL;
    k2q.n = 0u;
    k2q.cap = 0u;
    lzmesh_t4_q t4q; /* T4: span-lag FIFO (L9 only) */
    t4q.v = NULL;
    t4q.n = 0u;
    t4q.cap = 0u;
    lzmesh_u37_recents_init(recent);
    if (lzmesh_f1_dbg_on())
        fprintf(stderr, "F1DBG parse ncut=%u\n", (unsigned)ncut);
    if (level == 1) { /* S4: once-bitmap (calloc fail declines) */
        stored = (unsigned char *)calloc(size > 0u ? size : 1u, 1u);
        if (stored == NULL) {
            lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
            return 0;
        }
    }
    if (lzmesh_i5_on(level)) { /* I5: visited + take-m bitmaps */
        /* P30-CHAIN: W9 qlink alloc+memset deleted with the walk (dead;
         * default-path allocs + OOM shape identical: unarmed path never
         * allocated qlink since P16 QNULL). */
        i5v = (unsigned char *)calloc(size > 0u ? size : 1u, 1u);
        i5m = (unsigned char *)calloc(size > 0u ? size : 1u, 1u);
        i5ts = (unsigned char *)calloc(size > 0u ? size : 1u, 1u);
        ysp = (unsigned char *)calloc(size > 0u ? 2u * size : 2u, 1u);
        if (i5v == NULL || i5m == NULL || i5ts == NULL || ysp == NULL) {
            lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
            free(i5v);
            free(i5m);
            free(i5ts);
            free(ysp);
            return 0;
        }
        i5md.ycon = ysp;
        i5md.ywin = ysp + (size > 0u ? size : 1u);
        i5ts[0] = 1u; /* first take starts at 0 */
    }
    if (level == 1) { /* S2: MX-slot chains (head 2^hb). */
        size_t nh = (size_t)1u << hb;
        /* P8-T2: -1 is all-1 bits; memset-class fill, same bytes/bounds. */
        memset(head, 0xFF, nh * sizeof *head);
    } else {
        size_t nbig = (size_t)1u << hb;
        /* P8-T2: memset-class fills, same bytes/bounds. */
        memset(big, 0xFF, nbig * sizeof *big);
        memset(small, 0xFF,
               (size_t)LZMESH_U2_H3SIZE * sizeof *small);
    }
    /* H1: pos0 init-stored (B0b 18/18 take with no prior match). */
    if (lzmesh_i5_on(level)) /* I5: h2/h3 only, no h1 */
        lzmesh_i5_direct(big, small, src, size, 0u, hb, 0u, &i5md,
                             i5m, i5ts);
    else
        lzmesh_h1_store_visit(head, prev, big, small, src, size, 0u, hb,
                              level, stored);
    while (pos < size) {
        uint32_t clen = 0u, cdist = 0u;
        int chave, cis_rep = 0;
        int pre_stored = 0, chal_win = 0, j4_hit = 0;
        int j31have = 0; /* U2: +2-win skipped lit queued (K2 lag) */
        size_t j31p1 = 0u;
        /* H1: no loop-top catch-up (ins advances at takes only). */
        if (pos + LZMESH_U37_MINREP > size)
            break;
        /* F1 (L5 only): T2-3a walk-phase reset at H3 cuts. Oracle parses
         * per-block (fresh litrun at cut, tables/rep carry); port carried
         * litrun across cuts (s14-n32768: walk hops over rep-2@16725).
         * globrun keeps the global run for take recording (split tiles
         * globally, adjusts to block-relative at framing).
         * YF: L9 twin gated by LZMESH_YF_L9CUT (same shape). */
        if (qbr_cutlive) {
            size_t nh = 0u;
            while (nh < ncut && pos >= cuts[nh])
                nh++;
            if (nh > ncut_seen) {
                litrun = 0u;
                ncut_seen = nh;
            } else if (nh < ncut_seen) {
                ncut_seen = nh; /* J4 backed across a cut: no reset */
            }
        }
        chave = lzmesh_u37_best(src, size, pos, head, prev, big, small,
                                hb, recent, &clen, &cdist, &cis_rep,
                                level, last_m, last_end, last_rep, 0,
                                i5v, i5md.ycon, i5md.ywin);
        /* YF: winpos mark (queried+won; L9). */
        if (chave && level == 9 && i5md.ywin != NULL && pos < size)
            i5md.ywin[pos] = 1u;
        /* J4 backext-1: hash-only backward extension before lazy
         * (HINT-BYTEEXACT-R1 sec4: dist+8<=pos, litlen>=1, P-1
         * matches; back capped by litrun; hit skips lazy).
         * L5 only; short takes (long takes immediate anyway).
         * s11-n4097 e05: o@1088L4d924 vs p@1089L3d924. LANE-J4.
         * S4: L1 leg added (same gates) + backlen cap 7 both legs
         * (HINT-SCHED-R1 sec3: R748 855->848 / R760 867->860 e05+e09,
         * U32-R748 871->864 e01+e05+e09, 0 over-7 despite natural
         * room; L1-cap bed tmp/s4/s4_l1cap.py). L9 leg withheld:
         * oracle backextends there too (848/860/864-class e09) but
         * port over-stores C (751/763 via skipback) so the leg fires
         * early and regresses takes (R748-e09 847->844 vs o848);
         * blocked on backfill-condition (OPEN, QUESTION-S4-1). */
        /* R13-TL1-H5: J4 L1-spec gate (level const per parse; m1(1,.)
         * == u3_long_take; default wpins-ON makes the long-arm TRUE so
         * the local is tested first and short-circuits). L1 saves the
         * level-dispatch + m1/wpins calls; L5 +1 predictable dispatch,
         * L9 same count, stock order otherwise. Same predicate value. */
        if (chave && !cis_rep
            && (level == 1
                ? (litrun >= 1u
                   && (h5_l1j4long || !lzmesh_u3_long_take(clen))
                   && (size_t)cdist + 8u <= pos)
                : ((level == 5 || (level == 9 && r6_wj4))
                   && litrun >= 1u
                   && !lzmesh_m1_long_take(level, clen)
                   && (size_t)cdist + 8u <= pos))) {
            uint32_t j4back = lzmesh_u3_backext(src, pos, cdist);
            if (level == 9 && j4back != 1u) {
                /* ZDUEL-J4LIT1 (LANE-Z-DUEL): L9 back-1 of a back-2+
                 * extension when litrun==1 (diagonal class: oracle takes
                 * (Q,0,4,d) where port lits then takes (Q+1,1,3,d);
                 * 16/16 divs back2-3+litrun1; TW 46/46 back1 unchanged;
                 * S4-R748 litrun2+ stays stock). Env LZMESH_ZDUEL_J4LIT1=0
                 * restores stock. */
                if (litrun == 1u && j4back >= 2u
                    && r6_zj4)
                    j4back = 1u;
                else
                    j4back = 0u; /* WPINS: L9 raw-back1-only */
            }
            if (j4back > 7u)
                j4back = 7u; /* S4: backlen-cap-7 (SCHED sec3) */
            if (j4back > litrun)
                j4back = litrun;
            if (j4back > 0u) {
                if (lzmesh_wpins_j4trace_on())
                    fprintf(stderr, "WPJ4 L%d pos=%u back=%u len=%u d=%u lr=%u\n",
                        level, (unsigned)pos, j4back, clen, cdist, litrun);
                pos -= (size_t)j4back;
                clen += j4back;
                litrun -= j4back;
                globrun -= j4back; /* F1 */
                j4_hit = 1; /* extended: skip lazy, take now */
            }
        }
        /* E3: rep-cur takes immediately, no lazy (oracle-proven P8D/P9D:
         * rep-2-cur beats new-29/n1 and rep-19-n1). New-cur runs ordered
         * n1 challengers: rep0 first, then tiered hash (E3 P8E/s04n57:
         * n1-leg has no rep1/rep2; winning challenger emits with ITS
         * coding, no re-probe). Score duel kept (sec4 math gaps filed).
         * F4: L1 has no n1 leg at all (oracle-proven B1/B2/j6-j30:
         * e01 takes NEW6-cur with rep160/31/6/5-n1 staring; e05
         * skips same beds; memo-sec4 math predicts e01 skip at
         * j7/j8 and is falsified). Gate = u3's own lazy_on. */
        /* R12-H123-H4: lazy-once (order swap lazy-before-longtake is
         * invisible: both arms pure, no side effects either order). */
        if (chave && !cis_rep && h4_lazy
            && !lzmesh_m1_long_take(level, clen) && !j4_hit) {
            uint32_t r0len = 0u, r0dist = 0u, hlen = 0u, hdist = 0u;
            int64_t sc = 0;
            if (r6_sclive)
                sc = lzmesh_u3_score(clen, cdist, litrun);
            int r0have, hhave;
            /* H1: visited-direct before n1 peek (A4 36/36 immediate).
             * Peek itself stores nothing (read-only). */
            /* I4 FIX-B: NO flush here (lagbed2 6/6: 0-lit next query
             * misses span; the peek pre-store is not a flush
             * trigger -- only skip-walk and take stores flush).
             * L3 (LANE-L3): the n1 peek DOES see pending catch-up
             * (peekbed 6/6 oracle takes P+1 with the queued span
             * writer; s11-n57 challenger@48 needs @32-span). Drain
             * the K2 queue before the peek queries (L5 only). The
             * cur query above still sees pre-flush tables, so
             * lagbed2 k=0 (cur-miss) still holds. */
            if (level == 5)
                lzmesh_k2_drain(head, prev, big, small, vis, src,
                                size, hb, &k2q);
            /* T4: L9 peek drains (L3-shape; LANE-T4 s04-n257 take10). */
            /* R15-ML9-DRAIN: skip empty drain (n==0 <=> body is flag
             * 1->0 + n=0 stores only, unobservable single-threaded;
             * memo ANSWER-r15-mL9-1-ADD1: 79% empty mL9, IDENT 6/6).
             * R18 merge: D20 prefetch verbatim (outer arm untouched);
             * diet gates the drain call only (both ships preserved). */
            if (level == 9 && r6_t4lag
                && r6_t4pd) {
                /* R17-TL9 DRAIN-PREFETCH: peek h1 line is L2-cold.
                 * Piggybacked here (existing L9-only arm: ZERO new
                 * L1/L5 branches): drain+direct+rep window ~65c. */
                if (r6_u8 && pos + 9u <= size) {
                    uint64_t r17_pw8 = lzmesh_wl_ld64(src + pos + 1u);
                    uint32_t r17_ps1 = lzmesh_u2_h1(
                        r17_pw8 & 0xFFFFFFFFFFFFFFull, hb);
                    __builtin_prefetch((const void *)&big[r17_ps1], 0, 3);
                }
                if (t4q.n != 0u)
                    lzmesh_t4_drain(head, prev, big, small, vis, src,
                                    size, hb, level, i5v, i5m, &i5md,
                                    &t4q);
            }
            if (lzmesh_i5_on(level)) /* I5: h2/h3 only, no h1 */
                lzmesh_i5_direct(big, small, src, size, pos, hb, 0u,
                                     &i5md, i5m, i5ts);
            else
                lzmesh_h1_store_visit(head, prev, big, small, src, size,
                                      pos, hb, level, stored);
            pre_stored = 1;
            /* R6-T9: L9 peek fast legs (r1 contract: u8 + pp+8<=size;
             * tail/!u8/L1/L5 run stock). */
            if (level == 9 && r6_u8 && pos + 9u <= size)
                r0have = lzmesh_u37_rep_best_r1(src, size, pos + 1u,
                                                recent, 1u, &r0len,
                                                &r0dist);
            else
                r0have = lzmesh_u37_rep_best(src, size, pos + 1u, recent,
                                             1u, &r0len, &r0dist, level,
                                             (level == 5 || (level == 9 && r6_u8)) ? 1 : 0);
            /* P29-MF A2: D2 tier reuse (peek reuses the winning tier,
             * no i4_tier recompute; sound: win at T implies higher heads
             * failed and T passed, so i4 == T; P4 bedded 192048 exact). */
            { int mf_htier = 0;
            if (level == 1)
                hhave = lzmesh_s2_mx_best(src, size, pos + 1u, head,
                                          prev, hb, &hlen, &hdist, level,
                                          last_m, last_end, last_rep,
                                          recent[0], 0);
            else if (level == 5)
                /* R4-ENC5 C3: 7B-only probe (h2/h3 legs elided; tier 7
                 * implied, gate below passes through). */
                hhave = lzmesh_u37_slot_best_h1only(src, size, pos + 1u,
                                                    big, hb, &hlen,
                                                    &hdist, 1),
                mf_htier = hhave ? 7 : 0;
            else if (level == 9 && r6_u8 && pos + 9u <= size)
                hhave = lzmesh_u37_slot_best_r1(src, size, pos + 1u, big,
                                                small, hb, &hlen, &hdist,
                                                i5v, i5md.ycon, i5md.ywin);
            else
                hhave = lzmesh_u37_slot_best(src, size, pos + 1u, big,
                                             small, hb, &hlen, &hdist,
                                             (level == 9 && r6_u8) ? 1 : 0,
                                             level, i5v,
                                             i5md.ycon, i5md.ywin,
                                             &mf_htier);
            /* I4 FIX-A: L5 n1 peek is 7B-only (beds 12/12). Tier read
             * only when hhave; fail paths leave 0. */
            if (level == 5 && hhave && mf_htier != 7)
                hhave = 0;
            }
            /* F3: peek pos captured; the peek-store moves below the
             * lazy duels (outcome-known gating). n2 rep_best is
             * table-free, so the move is query-neutral. */
            size_t f3_pp = pos + 1u;
            if (r6_t4tr)
                fprintf(stderr,
                        "T4PEEK pos=%u cur=%u@%u r0=%d:%u@%u h=%d:%u@%u\n",
                        (unsigned)pos, clen, cdist, r0have, r0len,
                        r0dist, hhave, hlen, hdist);
            /* F5-F1rep: L5/L9 rep0 challenger with L1>=3 wins
             * outright (oracle-proven); L1==2 keeps the score duel
             * (the L2/D0>=9 subrule breaks PARSE-EQ cells). L1 stock.
             * G5: short non-run rep0 keeps the duel (s02-n60 e05:
             * cur NEW6 d5 vs rep0 L3 d15; force took rep0, kept 65B
             * COMP vs oracle STORE 66B). Force stays when rep covers
             * cur (r0len>=clen) or continues a run (r0dist==1):
             * all 35 F5-fix duel sites (s04/s08/s09/s10/s14 e05)
             * satisfy one leg. LANE-G5.
             * J3: +1 legs unchanged (+2 leg appended below, LANE-J3). */
            /* J6: L9 run-leg off. Oracle keeps NEW cur over rep0 run
             * (s05-n1025/s07-n9999/s09-n4096/s10-n1000 e09: cur
             * NEW5 taken, rep0-L3/4-d1 declined). L5 run-leg kept
             * (G5 C1: non-cover run sites load-bearing).
             * L2: L5 uses MATH duel (rep<=/hash<); L9 keeps legs. */
            if (level == 5) {
                if (r0have
                    && lzmesh_l2_rep_win(clen, cdist, r0len)) {
                    litrun++;
                    globrun++; /* F1 */
                    pos++;
                    clen = r0len;
                    cdist = r0dist;
                    cis_rep = 1;
                    chal_win = 1;
                } else if (hhave
                    && lzmesh_l2_hash_win(clen, cdist, hlen,
                                          hdist)) {
                    litrun++;
                    globrun++; /* F1 */
                    pos++;
                    clen = hlen;
                    cdist = hdist;
                    cis_rep = 0;
                    chal_win = 1;
                }
            } else if (r0have
                && (level != 1 && r0len >= 3u
                        && (r0len >= clen
                            || (r0dist == 1u && level != 9))
                        ? 1
                        : (level == 9 && r6_m4
                            ? lzmesh_m4_rep_win(clen, cdist, r0len)
                            : lzmesh_u3_score(r0len, r0dist,
                                              litrun + 1u) > sc))) {
                litrun++;
                globrun++; /* F1 */
                pos++;
                clen = r0len;
                cdist = r0dist;
                cis_rep = 1;
                chal_win = 1;
            } else if (hhave
                && lzmesh_t4_hash_win(level, hlen, hdist, clen,
                                      cdist, litrun, sc, r6_sbduel)) {
                litrun++;
                globrun++; /* F1 */
                pos++;
                clen = hlen;
                cdist = hdist;
                cis_rep = 0;
                chal_win = 1;
            }
            /* M4 +2 leg (L9 only): both +1 legs lost; rep0-only
             * at P+2 (hint-sec4 third challenger; J3's leg is
             * L5-only). The skipped P+1 is consumed as a literal
             * (direct + visited, mirroring the skip walk). */
            if (level == 9 && r6_m4 && !chal_win) {
                uint32_t n2len = 0u, n2dist = 0u;
                /* R6-T9: n2 fast leg under the r1 contract (pp=pos+2). */
                int r6_n2 = (r6_u8 && pos + 10u <= size)
                    ? lzmesh_u37_rep_best_r1(src, size, pos + 2u,
                                             recent, 1u, &n2len,
                                             &n2dist)
                    : lzmesh_u37_rep_best(src, size, pos + 2u, recent,
                                          1u, &n2len, &n2dist, level,
                                          r6_u8 ? 1 : 0);
                if (r6_n2
                    && lzmesh_j3_p2_win(clen, cdist, n2len)) {
                    lzmesh_i5_direct(big, small, src, size, pos + 1u, hb,
                                     last_end, &i5md, i5m, i5ts);
                    if (i5v != NULL)
                        i5v[pos + 1u] = 1u;
                    litrun += 2u;
                    globrun += 2u; /* F1 */
                    pos += 2u;
                    clen = n2len;
                    cdist = n2dist;
                    cis_rep = 1;
                    chal_win = 1;
                }
            }
            /* T4: L9 peek direct-stores peek pos (LANE-T4), moved below
             * the lazy duels for F3 outcome-known gating (query-neutral:
             * n2 rep_best is table-free; +2leg-direct already covers the
             * +2-won LIT case with the same predicate). F3: cur-win =>
             * peek pos is INT-off1 (skip-if-overwrite); r0/h-win => MSTART
             * (stock); +2-win => already stored above (omit).
             * TC (LANE-T-CONSUME): r0-win (chal_win && cis_rep) skips the
             * peek-store: the REP take-m rides the T4 queue only (T4 Q5
             * REP-m queued 4/4; gap0 MISS 10/10 prevREP). Immediate store
             * over-fires (s10-n100k e09 @19807: port reads peek-stored
             * 19801/d6, oracle reads older 19743/d64; pox KO/KP/KM bed).
             * h-win (NEW) keeps the store (Q5 NEW-m immediate 183/183).
             * LZMESH_TC_PEEKREP=0 restores stock. L9-only. */
            if (level == 9 && r6_t4peek && pos != f3_pp + 1u
                && !(r6_tcpr && chal_win && cis_rep
                     && cdist > 19u)) {
                size_t f3_le = chal_win ? 0u : f3_pp;
                lzmesh_i5_direct(big, small, src, size, f3_pp, hb, f3_le,
                                 &i5md, i5m, i5ts);
            }
            /* J3 +2 leg (L5 only): both +1 legs lost; rep0-only at P+2
             * (hint-sec4 third challenger; u3_lazy_decide semantics).
             * The skipped P+1 is consumed as a literal.
             * U2: P+1 joins the K2 lag (queued with the take's own
             * catch-up; flushed at a LATER trigger) instead of
             * storing immediately: s14 take@24598 (P=24596) skips
             * 24597 and the NEXT query (t660, abutting: take end ==
             * M660) must read pre-skip tables (oracle's 19487, not
             * the 24597 shadow). Readers 100s of takes later (s14
             * take@17803 reads J3+1 14042; s08 take@27143 reads
             * 27124; s11 take@20596 reads 8804; s04 take@47794 reads
             * 32824) see it post-flush. Immediate small-only
             * overfires (32 NEW e05); width predicates overfit.
             * Recorded here, queued below right after this take's
             * store-drain (NEW/gapped-REP) and before the take's
             * own entry, so it is neither flushed in the same
             * iteration nor wins over the take's span. */
            if (level == 5 && !chal_win) {
                uint32_t n2len = 0u, n2dist = 0u;
                if (lzmesh_u37_rep_best(src, size, pos + 2u, recent, 1u,
                                        &n2len, &n2dist, level, 1)
                    && lzmesh_j3_p2_win(clen, cdist, n2len)) {
                    lzmesh_k2_drain(head, prev, big, small, vis, src,
                                    size, hb, &k2q);
                    j31have = 1;
                    j31p1 = pos + 1u;
                    litrun += 2u;
                    globrun += 2u; /* F1 */
                    pos += 2u;
                    clen = n2len;
                    cdist = n2dist;
                    cis_rep = 1;
                    chal_win = 1;
                }
            }
        }
        if (!chave) {
            /* H1: skip walk step 1+(litrun>>8), clamped at tail. */
            size_t step = (size_t)1u + (litrun >> 8);
            /* F1: truncate steps at cuts (land exactly; oracle phase).
             * YF: L9 twin gated by LZMESH_YF_L9CUT. */
            if (qbr_cutlive) {
                size_t ci = ncut_seen;
                while (ci < ncut && cuts[ci] <= pos)
                    ci++;
                if (ci < ncut && pos + step > cuts[ci])
                    step = cuts[ci] - pos;
            }
            /* R12-H123-H4: L1 skip fast path (level const per parse;
             * L1 always lands in the i5_on-else arm below, so skip the
             * dead L5-drain, L9-drain and i5_on arms; the store_visit
             * + tail-clamp sequence is that else-arm verbatim). L5/L9
             * pay +1 predictable dispatch, stock order otherwise. */
            if (level == 1) {
                lzmesh_h1_store_visit(head, prev, big, small, src, size,
                                      pos, hb, level, stored);
                if (pos + step > size)
                    step = size - pos;
            } else {
            if (lzmesh_i5_on(level)) {
                /* R18-ML9 H9-SINK: S5 sunk here (L9-only arm; first,
                 * order vs flood preserved; VRP folds level leg). */
            /* T4: L9 skip-walk DRAINs (LANE-T4: flood's pos0-guard
                 * skips init slots, so clear loses queued writers there;
                 * s11-min h2-3056 needs drained 188 over init 0). */
                /* R15-ML9-DRAIN: skip empty drain (same proof as peek site). */
                if (level == 9 && r6_t4lag
                    && t4q.n != 0u)
                    lzmesh_t4_drain(head, prev, big, small, vis, src,
                                    size, hb, level, i5v, i5m, &i5md,
                                    &t4q);
                /* I5: flood [fup,ins) h2/h3, direct h2/h3,
                 * skip-backfill h1+h2. Walk landing is visited. */
                if (ins > i5_fup)
                    i5_fup = lzmesh_i5_flood(big, small, src, size,
                                             i5_fup, ins, hb, &i5md,
                                             i5m, i5ts);
                if (i5v != NULL)
                    i5v[pos] = 1u;
                if (!((i5md.run) & 1)
                    || !lzmesh_i5_isrun(src, size, pos))
                    lzmesh_i5_direct(big, small, src, size, pos, hb, last_end,
                                     &i5md, i5m, i5ts);
                if (pos + step > size)
                    step = size - pos;
                if (step > 1u)
                    lzmesh_i5_skipback(big, src, size, pos + 1u,
                                       pos + step, hb, &i5md);
            } else {
                /* R18-ML9 H9-SINK: S4 sunk here (non-L9 arm; first,
                 * order vs store_visit preserved). */
            /* I4 FIX-B: flush pending catch-up (skip-walk store
                 * IS a trigger: >=1 lit sees span per flushbed 8/8).
                 * K2: drains the FIFO (beds G/H). */
                if (level == 5)
                    lzmesh_k2_drain(head, prev, big, small, vis, src,
                                    size, hb, &k2q);
                lzmesh_h1_store_visit(head, prev, big, small, src, size,
                                      pos, hb, level, stored);
                if (pos + step > size)
                    step = size - pos;
            }
            } /* R12-H123-H4: end L1-skip else (L5/L9 stock) */
            litrun += (uint32_t)step;
            globrun += (uint32_t)step; /* F1 */
            pos += step;
            continue;
        }
        if (ntok >= tokcap) {
            lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
            free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
            free(stored); /* S4 */
            return 0;
        }
        {
            int rep = cis_rep;
            unsigned slot = 0u;
            /* T4-TRACE (LANE-T4): per-take parse path. */
            if (r6_t4tr)
                fprintf(stderr,
                        "T4TAKE m=%u run=%u len=%u dist=%u isn=%d chw=%d pre=%d chwv=%d cisrep=%d q=%u\n",
                        (unsigned)pos, litrun, clen, cdist,
                        rep ? 0 : 1, chal_win, pre_stored, chave,
                        cis_rep, (unsigned)t4q.n);
            if (rep) {
                for (j = 0u; j < 3u; j++) {
                    if (recent[j] == cdist) {
                        slot = j;
                        break;
                    }
                }
            }
            toks[ntok].litrun = globrun; /* F1: global run (tiling) */
            toks[ntok].mlen = clen;
            toks[ntok].dist = cdist;
            toks[ntok].is_new = rep ? 0u : 1u;
            toks[ntok].slot = slot;
            ntok++;
            if (rep) {
                if (level == 1)
                    recent[0] = cdist; /* 1-deep: rep0 only */
                else
                    lzmesh_u37_recents_rep(recent, slot);
            } else if (level == 1)
                recent[0] = cdist; /* 1-deep: single slot */
            else
                lzmesh_u37_recents_push(recent, cdist);
        }
        /* H1: temporal match catch-up (span-forward all tables, then
         * gap windows backward; chal-win peek pos enters via span).
         * I4 FIX-B: at L5 the span + rep-m flush lag one take
         * (beds 6/6); visits + windows stay immediate. */
        {
            size_t m = pos, end = pos + (size_t)clen;
            size_t span_lo = chal_win ? m : m + 1u;
            /* T2 SPAN40-FLUSH (L5 only; LANE-T2): NEW takes with
             * run+mlen >= 40 store take-m IMMEDIATELY (no K2 lag for
             * pm). Bedded: 6/7 big TAKESDIFF cells diverge where the
             * oracle sees a take-m the port queued (s02/s03/s08/
             * s10/s11/s16: source spans 44/182/69/102/47/62, all
             * NEW takes; oracle takes dist == source-take-m).
             * s11-n257 family pins the edge (span 39 hidden /
             * 40 visible). NEW-ONLY (v3): v2 fired on REP too and
             * over-stored (s00-n16384/s16-t478: oracle misses REP-m
             * at spans 42/73). REP keeps K2 lag (beds E/F).
             * pm-ONLY: v1 flushed span too and flooded single-slots
             * with zeros (12 NEW sparse). Span/windows keep lagging. */
            int t2_big = (level == 5 && !cis_rep
                && (size_t)litrun + (size_t)clen >= 40u) ? 1 : 0;
            if (level == 5) {
                /* K2: NEW take stores flush the queue (bed I 6/6);
                 * REP take stores do not (beds E/F 14/14).
                 * L1: gapped REP stores flush (gap lit drains per
                 * G/H/V-B incl lazy-consumed lits; s14 @21). Abutting
                 * REP still queues (s11). */
                if (!cis_rep || ins != m)
                    lzmesh_k2_drain(head, prev, big, small, vis, src,
                                    size, hb, &k2q);
                /* U2: queued J3+1 (recorded at the +2 win above, all
                 * three take shapes): pm-only entry BEFORE the take's
                 * own entry so the take's span (flushed later in the
                 * same drain) keeps newest-wins over it — base order
                 * was skip-then-span (s07-n1000 take@309 skips 308,
                 * span-shadows it; reversed order unshadows a
                 * spurious NEW-43@404). Queued after this take's
                 * store-drain so it is not flushed in the same
                 * iteration (s14 t660 reads pre-skip 19487). */
                if (j31have) {
                    lzmesh_i4_pend k2j;
                    k2j.plo = 0u;
                    k2j.phi = 0u;
                    k2j.pm = j31p1;
                    k2j.wlo = 0u;
                    k2j.wm = 0u;
                    k2j.pok = 0;
                    k2j.pmok = 1;
                    k2j.wok = 0;
                    if (!lzmesh_k2_append(&k2q, &k2j)) {
                        lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
                        free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
                        return 0;
                    }
                    j31have = 0;
                }
                if (ins == m) {
                    if (cis_rep) {
                        /* K2: abutting REP queues its own catch-up
                         * (s11-n513; beds F/G). No windows when
                         * abutting. OOM declines (established path). */
                        lzmesh_i4_pend k2e;
                        k2e.plo = 0u;
                        k2e.phi = 0u;
                        k2e.pm = 0u;
                        k2e.wlo = 0u;
                        k2e.wm = 0u;
                        k2e.pok = 0;
                        k2e.pmok = 0;
                        k2e.wok = 0;
                        if (!chal_win && !pre_stored) {
                            k2e.pm = m;
                            k2e.pmok = 1;
                        }
                        k2e.plo = span_lo;
                        k2e.phi = end;
                        k2e.pok = 1;
                        if (t2_big && k2e.pmok) {
                            /* T2 SPAN40: pm immediate, span still lags. */
                            lzmesh_u37_g6_store(big, small, src, size,
                                                k2e.pm, hb);
                            k2e.pmok = 0;
                        }
                        if (!lzmesh_k2_append(&k2q, &k2e)) {
                            lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
                            free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
                            return 0;
                        }
                    } else {
                        /* L3 (LANE-L3): abutting NEW queues its own
                         * catch-up (rep-m + span, no windows), same
                         * as abutting REP above. The K2 OPEN paradox
                         * (s02-NOW vs s06-LAG) dissolved on merged-K:
                         * always-lag keeps s02/s12/s43 RAW; the real
                         * split is cur (stale) vs n1-peek (flushed),
                         * pinned by peekbed 6/6 + s11-n57. I4 E8
                         * abutting-immediate superseded (L5 only). */
                        lzmesh_i4_pend k2e;
                        k2e.plo = 0u;
                        k2e.phi = 0u;
                        k2e.pm = 0u;
                        k2e.wlo = 0u;
                        k2e.wm = 0u;
                        k2e.pok = 0;
                        k2e.pmok = 0;
                        k2e.wok = 0;
                        if (!chal_win && !pre_stored) {
                            k2e.pm = m;
                            k2e.pmok = 1;
                        }
                        k2e.plo = span_lo;
                        k2e.phi = end;
                        k2e.pok = 1;
                        if (t2_big && k2e.pmok) {
                            /* T2 SPAN40: pm immediate, span still lags. */
                            lzmesh_u37_g6_store(big, small, src, size,
                                                k2e.pm, hb);
                            k2e.pmok = 0;
                        }
                        if (!lzmesh_k2_append(&k2q, &k2e)) {
                            lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
                            free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
                            return 0;
                        }
                    }
                } else {
                    /* K2: gapped own catch-up appends (FIFO); NEW m
                     * stays immediate (I4). */
                    if (!chal_win && !pre_stored && !cis_rep)
                        lzmesh_h1_store_visit(head, prev, big,
                                              small, src, size, m,
                                              hb, level, stored);
                    {
                        lzmesh_i4_pend k2e;
                        k2e.plo = span_lo;
                        k2e.phi = end;
                        k2e.pm = 0u;
                        k2e.wlo = ins;
                        k2e.wm = m;
                        k2e.pok = 1;
                        k2e.pmok = 0;
                        k2e.wok = 1;
                        if (!chal_win && !pre_stored && cis_rep) {
                            k2e.pm = m;
                            k2e.pmok = 1;
                        }
                        if (t2_big && k2e.pmok) {
                            /* T2 SPAN40: pm immediate, span still lags. */
                            lzmesh_u37_g6_store(big, small, src, size,
                                                k2e.pm, hb);
                            k2e.pmok = 0;
                        }
                        if (!lzmesh_k2_append(&k2q, &k2e)) {
                            lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
                            free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
                            return 0;
                        }
                    }
                }
            } else if (lzmesh_i5_on(level) && r6_t4lag) {
                /* T4: L9 queued path (LANE-T4). NEW drains; NEW-m
                 * immediate; span+windows (+REP-m) queue. O4 cond
                 * mirrored from below (rep-only, append-time). */
                lzmesh_t4_pend t4e;
                int t4_o4_skip = 0;
                /* R15-ML9-DRAIN: skip empty drain (same proof). */
                if (!cis_rep
                    && t4q.n != 0u)
                    lzmesh_t4_drain(head, prev, big, small, vis, src,
                                    size, hb, level, i5v, i5m, &i5md,
                                    &t4q);
                if (r6_o4 && cis_rep && clen == 2u
                    && m > ins && (m - ins) == 1u && ins != 1u
                    && m + 3u <= size) {
                    uint32_t s = lzmesh_u2_h3(
                        (uint32_t)lzmesh_u2_load_n(src + m, 3u));
                    uint32_t cur = small[s];
                    if (cur != LZMESH_U2_EMPTY && i5v != NULL
                        && i5m != NULL && (size_t)cur < size
                        && i5v[cur] && !i5m[cur]
                        && lzmesh_i5_heq(src, size, cur, m, 3u))
                        t4_o4_skip = 1;
                }
                /* VEB: mark take-m BEFORE direct(m) so T-COMB legs see it
                 * (direct(m) must not self-skip; drain already ran above). */
                if (i5m != NULL)
                    i5m[m] = 1u;
                if (!chal_win && !pre_stored && !cis_rep)
                    lzmesh_i5_direct(big, small, src, size, m, hb, 0u,
                                     &i5md, i5m, i5ts);
                if (i5v != NULL) {
                    i5v[m] = 1u;
                    i5m[m] = 1u;
                }
                t4e.ins = ins;
                t4e.m = m;
                t4e.end = end;
                t4e.span_lo = span_lo;
                t4e.tdist = cdist;
                t4e.pm = 0u;
                t4e.pmok = 0;
                if (!chal_win && !pre_stored && cis_rep
                    && !t4_o4_skip) {
                    t4e.pm = m;
                    t4e.pmok = 1;
                }
                /* U3: short-NEW-FAR take span immediate (LANE-U3).
                 * s10-n63 (NEW L3 td21) span visible next query;
                 * s05-n129 (NEW L3 td8) span queued (else d1 beats
                 * oracle d2 at query@30). td>19 only (M3-window
                 * top; td<=19 keeps queue: conservative). */
                if (!cis_rep && r6_u3k > 0
                    && clen <= (uint32_t)r6_u3k
                    && cdist > 19u)
                    lzmesh_i5_catchup(head, prev, big, small, vis, src,
                                      size, ins, m, end, hb, level,
                                      span_lo, i5v, i5m, cdist, &i5md);
                else if (!lzmesh_t4_append(&t4q, &t4e)) {
                    lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
                    free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
                    free(stored); /* S4 */
                    return 0;
                }
            } else {
                if (!chal_win && !pre_stored) {
                    if (lzmesh_i5_on(level)) { /* I5: h2/h3 only, no h1 */
                        /* === O4 L9 rep-M joint-h3-head-eq guard (LANE-O4) ===
                         * Bedded black-box (tmp/o4/o4repbed5.py 22/22):
                         * oracle stores rep-take-M iff h3-occupant
                         * empty/head-neq (noQ READ-M 6/6, Qneq READ-M 6/6);
                         * keeps old on head-eq Q (SKIP+n1 6/6, joint: big
                         * follows h3 under big-empty gate). NEW-M
                         * overwrites (NEW-Q READ-M 4/4: rep-only). Bed
                         * envelope is RL2/run1/nonfirst/lit-Q; corpus
                         * shows STORE outside it (M/span occupants,
                         * RL!=2, first-take s15-n1024) so fire only
                         * inside: RL2 + run1 + nonfirst + lit-occupant
                         * (i5v, not take-m). L9-only. */
                        int o4_skip = 0;
                        if (r6_o4 && cis_rep && clen == 2u
                            && m > ins && (m - ins) == 1u && ins != 1u
                            && m + 3u <= size) {
                            uint32_t s = lzmesh_u2_h3(
                                (uint32_t)lzmesh_u2_load_n(src + m, 3u));
                            uint32_t cur = small[s];
                            if (cur != LZMESH_U2_EMPTY && i5v != NULL
                                && i5m != NULL && (size_t)cur < size
                                && i5v[cur] && !i5m[cur]
                                && lzmesh_i5_heq(src, size, cur, m, 3u))
                                o4_skip = 1;
                        }
                        /* VEB: mark take-m BEFORE direct(m) so T-COMB legs
                         * see it (direct(m) must not self-skip). */
                        if (i5m != NULL)
                            i5m[m] = 1u;
                        if (!o4_skip)
                            lzmesh_i5_direct(big, small, src, size, m, hb, 0u,
                                             &i5md, i5m, i5ts);
                    } else
                        lzmesh_h1_store_visit(head, prev, big, small, src,
                                              size, m, hb, level, stored);
                }
                if (i5v != NULL) { /* I5: take-m is visited + take-m */
                    i5v[m] = 1u;
                    i5m[m] = 1u;
                }
                if (lzmesh_i5_on(level)) /* I5: cond span + h2 windows */
                    lzmesh_i5_catchup(head, prev, big, small, vis, src,
                                      size, ins, m, end, hb, level,
                                      span_lo, i5v, i5m, cdist, &i5md);
                else
                    lzmesh_h1_catchup(head, prev, big, small, vis, src,
                                      size, ins, m, end, hb, level,
                                      span_lo, stored);
            }
            ins = end;
            last_m = m; /* I3: track most recent take span (L1 hide) */
            last_end = end;
            last_rep = cis_rep;
            /* VEB: forward-mark next take start (tiling: start[k+1]=end[k];
             * lets direct/flood evaluate LIT-off/take-start legs). */
            if (i5ts != NULL && end < size)
                i5ts[end] = 1u;
            /* YF: consumed mark [m,end] closed (L9 source-gate).
             * R17-TL9: outline helper (1 bl, base-identical shape). */
            if (level == 9 && i5md.ycon != NULL) {
                size_t hi = (end < size) ? end : size - 1u;
                if (hi >= m)
                    lzmesh_r17_yf_mark(i5md.ycon + m, hi - m + 1u);
            }
        }
        litrun = 0u;
        globrun = 0u; /* F1 */
        pos += clen;
    }
    /* I4: flush trailing pending catch-up (table completeness).
     * K2: drains the FIFO. */
    if (level == 5)
        lzmesh_k2_drain(head, prev, big, small, vis, src, size, hb,
                        &k2q);
    litrun += (uint32_t)(size - pos);
    globrun += (uint32_t)(size - pos); /* F1 */
    if (globrun > 0u) {
        if (ntok >= tokcap || ntok + 1u > size) {
            lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
            free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
            free(stored); /* S4 */
            return 0;
        }
        toks[ntok].litrun = globrun;
        toks[ntok].mlen = 2u; /* overhang-clamped terminator match */
        toks[ntok].dist = recent[0];
        toks[ntok].is_new = 0u;
        toks[ntok].slot = 0u;
        ntok++;
    }
    if (ntok == 0u || ntok > size) {
        lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
        free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
        free(stored); /* S4 */
        return 0;
    }
    lzmesh_k2_free(&k2q); lzmesh_t4_free(&t4q);
    free(i5v); free(ysp); free(i5m); free(i5ts); /* I5 */
    free(stored); /* S4 */
    return ntok;
}

/* Decoder-mirror B1 check: returns 1 iff the decoder's unified-length
 * leg would REFUSE this first block (caller must decline to RAW). */
static int lzmesh_u37_b1_refuse(uint32_t ds, uint32_t br_len,
                                uint32_t tokc, uint32_t distc, int lanes_empty,
                                uint32_t modes, uint32_t litc,
                                const uint8_t *lit, const uint8_t *tok,
                                const uint8_t *len) {
    uint32_t b10, b11, lval, lo, hi, aligned;
    uint8_t b12, b13;
    if (tokc != 1u || distc != 0u || !lanes_empty)
        return 0;
    if (modes != 0u)
        return 0;
    if (br_len < 3u || ds < 4u)
        return 0;
    /* br byte i: lit[0..litc), tok[0], len[0..lenc). */
    b10 = (1u < litc) ? lit[1] : ((1u == litc) ? tok[0] : len[1u - litc - 1u]);
    b11 = (2u < litc) ? lit[2] : ((2u == litc) ? tok[0] : len[2u - litc - 1u]);
    if ((b10 & 0xE0u) != 0xC0u || (b10 & 7u) == 7u)
        return 0;
    if (b11 != 0xFFu) {
        lval = b11;
    } else {
        if (br_len < 5u)
            return 1;
        b12 = (3u < litc) ? lit[3]
            : ((3u == litc) ? tok[0] : len[3u - litc - 1u]);
        b13 = (4u < litc) ? lit[4]
            : ((4u == litc) ? tok[0] : len[4u - litc - 1u]);
        lval = (uint32_t)b12 | ((uint32_t)b13 << 8);
    }
    lo = ds - 4u;
    aligned = (ds + 31u) & ~31u;
    hi = aligned - 4u;
    return (lval < lo || lval > hi) ? 1 : 0;
}

static uint32_t lzmesh_u37_round32(uint32_t n) {
    return (n + 31u) & ~31u;
}

/* Frozen-shape ownership: genuine-periodic 2+ periods (u12/u15/u18),
 * k-run (u19+, E2c vetoes), fresh-value short-run chain (u21/u27+
 * vetoes). Returns 1 iff u37 (LZ + HUF fallback) must decline. */
static int lzmesh_u37_owned(const uint8_t *src, size_t size) {
    unsigned char seen[256];
    unsigned runs = 0u, maxr = 0u, cur = 0u;
    size_t i;
    if (src == NULL || size == 0u)
        return 1;
    { /* Genuine periodicity needs 2+ periods; p>n/2 detections
       * (e.g. k=2 noise matching its last 2 bytes) are degenerate
       * and stay u37-eligible. */
        int p12 = lzmesh_u12_period(src, size);
        int p18 = lzmesh_u18_period(src, size);
        if (p12 != 0 && (size_t)p12 * 2u <= size)
            return 1;
        if (p18 != 0 && (size_t)p18 * 2u <= size)
            return 1;
    }
    /* P4-N3: gate-first (fail-fast at first short run; predicate-exact). */
    if (lzmesh_u19_gate(src, size, NULL, NULL))
        return 1;
    for (i = 0u; i < 256u; i++)
        seen[i] = 0u;
    /* P4-N3: i=0 iteration unrolled (runs=1, cur=1, head fresh); a repeat
     * head decides the predicate false (runs>=3 && fresh && maxr<64 needs
     * fresh) so return 0 at once. Arrival at the tail implies fresh. */
    seen[src[0]] = 1u;
    runs = 1u;
    cur = 1u;
    for (i = 1u; i < size; i++) {
        if (src[i] != src[i - 1u]) {
            runs++;
            if (cur > maxr)
                maxr = cur;
            cur = 1u;
            if (seen[src[i]])
                return 0;
            seen[src[i]] = 1u;
        } else {
            cur++;
        }
    }
    if (cur > maxr)
        maxr = cur;
    if (runs >= 3u && maxr < 64u)
        return 1;
    return 0;
}

/* U1 e01 s06 reclaim (owner: U1; LANE-U1 sec6): u18 MX-shadow-vetoed
 * periodics (L1) fall to u37 GEN (S2 M@16/T@17 = oracle). Narrow:
 * exact-p18 2+periods + MX-shadowed@p (same predicate as the u18
 * veto). L1-only (callers gate level; L5/L9 u18 unaffected). */
static int lzmesh_u1_u37_reclaim(const uint8_t *src, size_t size) {
    int p18 = lzmesh_u18_period(src, size);
    if (p18 <= 0 || (size_t)p18 * 2u > size)
        return 0;
    return lzmesh_u1_mx_shadowed(src, size, (size_t)p18);
}

/* F2 full 4-stream exact-est keep-gate (CLEAN-side behavioral, KEEPGATE
 * memo + HINT-BYTEEXACT sec5). Per-stream Huffman est over lit, tok,
 * len-extra, dist-sym + table overhead + suffix lanes + idx. Returns
 * COMP total est (fo_est+11) when any stream takes Huffman, 0 when all
 * streams RAW/REP (caller falls back to TIER-2 fo+11).
 * Per-stream rollback (E1 rule): n==0 RAW-0B; all-equal REP 1B; n<=10
 * RAW; Kraft fail RAW; 73+data+packed>=8n RAW. Meta E1/D3 qm ladder.
 * Pack: per-Huffman-stream meta 65b lane0; packed lens + data continuous
 * round-robin; suffix slot%8. P8: never stricter than oracle. */
static size_t lzmesh_f2_est_total(const uint8_t *lit, size_t li,
                                  const uint8_t *tok, size_t ti,
                                  const uint8_t *len, size_t eni,
                                  const uint8_t *dsym, size_t di,
                                  const lzmesh_u37_tok *toks, size_t ntok,
                                  uint32_t distc) {
    const uint8_t *str[4];
    size_t strn[4];
    unsigned char lens[4][256];
    uint8_t vals[4][256];
    unsigned char mlens[4][11];
    unsigned used4[4], mode4[4], s;
    uint32_t freq[256];
    unsigned idx256[256];
    uint32_t *items256 = NULL;
    unsigned char *sel256 = NULL;
    uint32_t mfreq[11];
    unsigned idx11[11];
    uint32_t *items11 = NULL;
    unsigned char *sel11 = NULL;
    unsigned bitc[8], laneb[8], k;
    uint8_t idxbuf[24];
    unsigned idxsz, u;
    uint32_t bo_est, fo_est, payload = 0u;
    uint32_t bm = 0u, havesome = 0u;
    size_t i, t;
    uint32_t slot = 0u;
    uint64_t packphase = 0u, dataphase = 0u;
    if (lit == NULL || tok == NULL || len == NULL || dsym == NULL
        || toks == NULL)
        return 0u;
    str[0] = lit;
    str[1] = tok;
    str[2] = len;
    str[3] = dsym;
    strn[0] = li;
    strn[1] = ti;
    strn[2] = eni;
    strn[3] = di;
    for (s = 0u; s < 4u; s++) {
        mode4[s] = 0u;
        used4[s] = 0u;
    }
    items256 = (uint32_t *)malloc(
        lzmesh_u4_build_items_u32(256u, 10u) * sizeof *items256);
    sel256 = (unsigned char *)malloc(
        lzmesh_u4_build_sel_bytes(256u, 10u) * sizeof *sel256);
    items11 = (uint32_t *)malloc(
        lzmesh_u4_build_items_u32(11u, 5u) * sizeof *items11);
    sel11 = (unsigned char *)malloc(
        lzmesh_u4_build_sel_bytes(11u, 5u) * sizeof *sel11);
    if (items256 == NULL || sel256 == NULL || items11 == NULL
        || sel11 == NULL)
        goto raw;
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        size_t n = strn[s];
        unsigned nz = 0u;
        uint64_t data_bits = 0u, packed_bits = 0u;
        unsigned used = 0u;
        if (n == 0u || n > 65535u) {
            mode4[s] = 0u; /* RAW: 0 bytes (n==0) or bail (huge) */
            if (n > 65535u)
                goto raw;
            continue;
        }
        for (i = 1u; i < n; i++) {
            if (sp[i] != sp[0])
                break;
        }
        if (i == n) {
            mode4[s] = 1u; /* REP 1B */
            continue;
        }
        if (n <= 10u) {
            mode4[s] = 0u; /* tiny RAW */
            continue;
        }
        for (i = 0u; i < 256u; i++)
            freq[i] = 0u;
        for (i = 0u; i < n; i++)
            freq[sp[i]]++;
        if (!lzmesh_u4_build(freq, 256u, 10u, lens[s], idx256,
                             items256, sel256))
            continue; /* Kraft fail RAW */
        for (i = 0u; i < 256u; i++)
            data_bits += (uint64_t)freq[i]
                * (uint64_t)lens[s][i];
        if (!lzmesh_pack1_bitmap(lens[s], &bm))
            continue;
        used = lzmesh_pack1_gather(lens[s], bm, vals[s]);
        if (used == 0u || used > 256u || (used & 7u) != 0u)
            continue;
        for (u = 0u; u < 11u; u++)
            mfreq[u] = 0u;
        for (u = 0u; u < used; u++) {
            if (vals[s][u] > 10u)
                break;
            mfreq[vals[s][u]]++;
        }
        if (u != used)
            continue;
        for (u = 0u; u < 11u; u++) {
            if (mfreq[u] != 0u)
                nz++;
        }
        if (nz < 2u) {
            if (nz != 1u)
                continue;
            lzmesh_u4_meta_single_lens((unsigned)vals[s][0], 11u,
                                       mlens[s]);
        } else {
            unsigned char ml3[11], ml4[11], ml5[11];
            uint64_t m3 = 0u, m4 = 0u, m5 = 0u;
            unsigned umaxm = 0u, M, wm, qm, ii;
            uint32_t d67;
            if (!lzmesh_u4_build(mfreq, 11u, 3u, ml3, idx11, items11,
                                 sel11))
                continue;
            if (!lzmesh_u4_build(mfreq, 11u, 4u, ml4, idx11, items11,
                                 sel11))
                continue;
            if (!lzmesh_u4_build(mfreq, 11u, 5u, ml5, idx11, items11,
                                 sel11))
                continue;
            for (ii = 0u; ii < 11u; ii++) {
                m3 += (uint64_t)mfreq[ii] * ml3[ii];
                m4 += (uint64_t)mfreq[ii] * ml4[ii];
                m5 += (uint64_t)mfreq[ii] * ml5[ii];
                if (ml5[ii] > umaxm)
                    umaxm = ml5[ii];
            }
            wm = lzmesh_u4_w23(nz, 5u);
            M = (wm < umaxm) ? wm : umaxm;
            d67 = (mfreq[6] > mfreq[7]) ? (mfreq[6] - mfreq[7])
                                        : (mfreq[7] - mfreq[6]);
            if (nz <= 5u) {
                if (M >= 5u) {
                    qm = ((m4 >= m5 + 9u)
                        || (m4 >= m5 + 4u && d67 >= 10u)
                        || (m4 == m5 + 4u && d67 <= 1u)
                        || (m4 == m5 + 3u && d67 >= 2u)
                        || (m4 == m5 + 2u && mfreq[6] >= 9u)) ? 5u
                                                                  : 4u;
                } else if (M == 4u) {
                    uint64_t gap = (m3 >= m4) ? (m3 - m4) : 0u;
                    qm = (gap >= 1u) ? 4u : 3u;
                } else {
                    qm = 3u;
                }
            } else {
                qm = ((m4 >= m5 + 9u)
                    || (m4 >= m5 + 4u && d67 >= 10u)
                    || (m4 == m5 + 4u && d67 <= 1u)
                    || (m4 == m5 + 3u && d67 >= 2u)
                    || (m4 == m5 + 2u && mfreq[6] >= 9u)) ? 5u : 4u;
            }
            for (ii = 0u; ii < 11u; ii++)
                mlens[s][ii] = (qm == 5u) ? ml5[ii]
                    : (qm == 4u) ? ml4[ii] : ml3[ii];
        }
        for (u = 0u; u < used; u++)
            packed_bits += mlens[s][vals[s][u]];
        if (lzmesh_u4_huff_rollback(LZMESH_U4_HDR73 + data_bits
                                        + packed_bits,
                                    (uint32_t)n))
            continue; /* rollback RAW */
        mode4[s] = 2u;
        used4[s] = used;
        havesome = 1u;
    }
    if (!havesome)
        goto raw;
    for (k = 0u; k < 8u; k++)
        bitc[k] = 0u;
    {
        uint64_t b = 9u;
        for (s = 0u; s < 4u; s++) {
            if (mode4[s] == 2u) {
                unsigned uu;
                size_t ii;
                bitc[0] += 33u + 32u;
                for (uu = 0u; uu < used4[s]; uu++) {
                    bitc[packphase & 7u] +=
                        mlens[s][vals[s][uu]];
                    packphase++;
                }
                for (ii = 0u; ii < strn[s]; ii++) {
                    bitc[dataphase & 7u] +=
                        lens[s][str[s][ii]];
                    dataphase++;
                }
            } else if (mode4[s] == 1u) {
                b += 1u;
            } else {
                b += (uint64_t)strn[s];
            }
        }
        if (b > (uint64_t)0xFFFFu)
            goto raw;
        bo_est = (uint32_t)b;
    }
    for (t = 0u; t < ntok; t++) {
        if (!toks[t].is_new)
            continue;
        bitc[slot % 8u] += lzmesh_u3_sb_of(toks[t].dist);
        slot++;
    }
    if (slot != distc)
        goto raw;
    for (k = 0u; k < 8u; k++) {
        laneb[k] = (bitc[k] + 7u) >> 3;
        payload += laneb[k];
    }
    if (payload == 0u) {
        fo_est = bo_est;
    } else {
        idxsz = lzmesh_pack1_index_pack(laneb, idxbuf,
                                        (unsigned)sizeof idxbuf);
        if (idxsz == 0u)
            goto raw;
        if ((uint64_t)bo_est + (uint64_t)payload + (uint64_t)idxsz
            > (uint64_t)0xFFFFu)
            goto raw;
        fo_est = bo_est + payload + idxsz;
    }
    free(items256);
    free(sel256);
    free(items11);
    free(sel11);
    return (size_t)fo_est + 10u + 1u;
raw:
    if (items256 != NULL)
        free(items256);
    if (sel256 != NULL)
        free(sel256);
    if (items11 != NULL)
        free(items11);
    if (sel11 != NULL)
        free(sel11);
    return 0u;
}

/* === G1 e01 GEN Huffman emission (owner: G1; LANE-G1) ===
 * u37 GEN emits RAW-only; oracle Huffman-codes streams per S5.5
 * (trivial + speculative + rollback bits>=8n). Table build ports the
 * u35 logic verbatim (D3 q-floor sym + rank, QM-D1 single-build
 * meta on raw mfreq; D3/FAM-N/J2/E6 gate stack deleted per
 * HINT-QM-R2 D1+D10). Layout per decoder S3.6 chain (fetch order lit,tok,
 * len,dist; headers lane0; vals/data restart lane0; suffix slot%8
 * continuing). Pads PAD1 + B-TAB(lastL,k) on suffix-free lanes.
 * L1-only. Gates/B1/keep see RAW modes (F2/D1 owned, untouched).
 * Any build/layout failure returns 0: caller falls through to the
 * proven RAW path, so RAW decisions stay byte-identical. */
typedef struct {
    uint8_t lens[256];
    uint16_t codes[256];
    uint8_t mlens[11];
    uint16_t mcodes[11];
    uint8_t vals[256];
    uint32_t bm;
    unsigned used;
    uint64_t data_bits;
    uint64_t pack_bits;
} lzmesh_g1_huff;

/* E6 table build for one GEN stream. Returns 1 with *h filled
 * (lens/codes/mlens/mcodes/vals/bm/used/bits), else 0 (RAW). */
static int lzmesh_g1_build(const uint8_t *s, size_t n,
                           lzmesh_g1_huff *h) {
    uint32_t freq[256];
    /* R14-TL1STACK-G1 (C-FREQ): extra histogram lanes (see below). */
    uint32_t gf1[256], gf2[256], gf3[256];
    uint32_t mfreq[11];
    uint32_t bm = 0u;
    unsigned u, i, nz;
    uint64_t dbits = 0u, pbits = 0u;
    if (s == NULL || h == NULL || n == 0u || n > 65535u)
        return 0;
    /* R14-TL1STACK-G1 (C-FREQ): 4-lane unrolled histogram (H1 analog
     * in u35_lengths). The scalar freq[s[i]]++ is a load-add-store
     * dependency chain; u35_lengths NEVER runs on tL1 so H1 does not
     * cover this site (ANSWER-r13-tl1-1). 4 independent tables x 4-way
     * unroll break the chain; histogram sums commute so the combine
     * is exact. Each lane <= n <= 65535 (no u32 overflow). N-GATE:
     * same fixed cost as H1 (~1800 ops) => scalar-verbatim under
     * 1024B, 4-lane above (tL1 n~3959, prize intact). */
    if ((unsigned)n < 1024u) {
        for (i = 0u; i < 256u; i++)
            freq[i] = 0u;
        for (i = 0u; i < (unsigned)n; i++)
            freq[s[i]]++;
    } else {
        for (i = 0u; i < 256u; i++) {
            freq[i] = 0u;
            gf1[i] = 0u;
            gf2[i] = 0u;
            gf3[i] = 0u;
        }
        {
            unsigned n4 = (unsigned)n, m4 = n4 & ~3u;
            for (i = 0u; i < m4; i += 4u) {
                freq[s[i]]++;
                gf1[s[i + 1u]]++;
                gf2[s[i + 2u]]++;
                gf3[s[i + 3u]]++;
            }
            for (; i < n4; i++)
                freq[s[i]]++;
            for (i = 0u; i < 256u; i++)
                freq[i] += gf1[i] + gf2[i] + gf3[i];
        }
    }
    { /* D3 H5DEEP q-floor sym (u35_tryq verbatim). */
        uint32_t qfreq[256];
        unsigned unsym = 0u, wbound, qq, mx, i2;
        for (i2 = 0u; i2 < 256u; i2++)
            if (freq[i2] != 0u)
                unsym++;
        wbound = lzmesh_pack1_w23(unsym, 10u);
        if (wbound == 0u || wbound > 10u)
            return 0;
        qq = (unsigned)(((uint32_t)n >> wbound) + 1u);
        for (;;) {
            for (i2 = 0u; i2 < 256u; i2++)
                qfreq[i2] =
                    (freq[i2] != 0u && freq[i2] < qq) ? qq : freq[i2];
            /* P12-W8 skip-solve (h3sym twin of the u35_tryq site;
             * same proof + rank_assign below reassigns all). */
            if (lzmesh_s3_huff(qfreq, 256u, h->lens) == 0
                && lzmesh_pack1_solve(qfreq, 256u, 10u, h->lens) == 0u)
                return 0;
            mx = 0u;
            for (i2 = 0u; i2 < 256u; i2++)
                if (h->lens[i2] > mx)
                    mx = h->lens[i2];
            if (mx <= wbound)
                break;
            if (qq > (1u << 24))
                return 0;
            qq <<= 1;
        }
    }
    lzmesh_u35_rank_assign(freq, h->lens);
    if (!lzmesh_pack1_canon(h->lens, 256u, h->codes, 10u))
        return 0;
    if (!lzmesh_pack1_bitmap(h->lens, &bm))
        return 0;
    u = lzmesh_pack1_gather(h->lens, bm, h->vals);
    if (u == 0u || u > 256u || (u & 7u) != 0u)
        return 0;
    for (i = 0u; i < 11u; i++)
        mfreq[i] = 0u;
    for (i = 0u; i < u; i++) {
        if (h->vals[i] > 10u)
            return 0;
        mfreq[h->vals[i]]++;
    }
    nz = 0u;
    for (i = 0u; i < 11u; i++)
        if (mfreq[i] != 0u)
            nz++;
    if (nz < 2u) { /* R6-A5 phantom (u35_tryq verbatim). */
        if (nz != 1u || mfreq[0] != 0u)
            return 0;
        mfreq[0] = 1u;
        nz = 2u;
    }
    { /* QM-D1 single build on raw mfreq (u35_tryq twin; gate stack
       * deleted per HINT-QM-R2 D1+D10; 766/766 oracle-exact). */
        if (lzmesh_qm_meta_build(mfreq, u, h->mlens) == 0)
            return 0;
    }
    if (!lzmesh_pack1_canon(h->mlens, 11u, h->mcodes, 5u))
        return 0;
    for (i = 0u; i < u; i++)
        pbits += h->mlens[h->vals[i]];
    for (i = 0u; i < (unsigned)n; i++)
        dbits += h->lens[s[i]];
    h->bm = bm;
    h->used = u;
    h->data_bits = dbits;
    h->pack_bits = pbits;
    return 1;
}

/* === S3 V-OR padsim (HINT-PADS-R2 sec7; LANE-S3) ===
 * Byte-exact pads via writer-accumulator V-OR simulation. Gate: tok-only
 * HUF (mode[1]==2, tok 1..64 syms), lit/len RAW (len 2..16), dist
 * RAW-or-scalar (a header byte; lane bits come only from mode-2 writes
 * or suffix, both excluded), suffix-free. Sim: V-OR over tok lens-call
 * (h->vals + mcodes) + tok vals (codes), then len lens-call + vals
 * (RAW lens via two-queue Huffman + gather + Huffman(valfreqs)+canon),
 * dealt q%8 per call, cumulative N from fresh (S0 pre-HUF skipped).
 * Lane bytes = V-OR low bytes; lane0 = 65 meta bits + V-OR.
 * Trust (U5): per-lane verified-only. Calls are dealt q%8 into disjoint
 * accumulators, so each lane's OR-image verifies independently against
 * the emitted payload bits; verified lanes take sim pads, the rest keep
 * PAD1/B-TAB. Lane0 S2-on-lane0 allowed only for byte-fill N1==8
 * (S2@N1-2, U4/S3-1); wider handoff falls back (several offsets
 * self-check with different pads: ambiguous, s05 bed; OPEN S3-1).
 * Returns a bitmask of verified lanes (0 = full abstain). Never
 * crashes: all loops bounded, all indexes checked. */
#define LZMESH_S3_MAXLANE 256u
/* AA-E01BIG: V1 big-lane cap (V2SM precedent: 64KB stack acc OK). S3/U5
 * stays 256 (envelope unchanged); V1 alone widens to multi-KB lanes. */
#define LZMESH_V1_MAXLANE 4096u
#define LZMESH_S3_MAXTOK 64u
#define LZMESH_S3_MINLEN 2u
#define LZMESH_S3_MAXLEN 16u
#define LZMESH_S3_META 65u

/* Two-queue Huffman depths (freq ASC, leaf-order base, ties prefer q1).
 * freq[nsym], lens[nsym] out. ord==NULL: sym ASC base (base-hw);
 * else leaves inserted in ord[0..nord) sequence (firstocc-vals: syms
 * by first lseq occurrence; ANSWER-u-e05trio-2 SHIP). Returns 1 iff
 * >=2 symbols and all depths in 1..10. */
static int lzmesh_s3_huff_ord(const uint32_t *freq, unsigned nsym,
    uint8_t *lens, const uint16_t *ord, unsigned nord)
{
    /* nodes: 0..nsym-1 leaves (sym order), nsym.. overflow guard. */
    uint32_t fw[512];
    uint16_t q1[256], q2[256], q1t[256];
    unsigned p1, p2, n1, n2, i, nn = 0u;
    uint16_t par[512];
    uint16_t chl[512], chr[512];
    unsigned nnode;
    if (freq == 0 || lens == 0 || nsym == 0u || nsym > 256u)
        return 0;
    for (i = 0u; i < nsym; i++)
        lens[i] = 0u;
    /* leaves with freq>0, insertion-sorted by (freq, base order). */
    n1 = 0u;
    if (ord == NULL) {
        for (i = 0u; i < nsym; i++) {
            if (freq[i] == 0u)
                continue;
            q1[n1++] = (uint16_t)i;
        }
    } else {
        unsigned j;
        if (nord == 0u || nord > 256u)
            return 0;
        for (j = 0u; j < nord; j++) {
            if (ord[j] >= nsym)
                return 0;
            if (freq[ord[j]] == 0u)
                continue;
            q1[n1++] = ord[j];
        }
    }
    if (n1 < 2u)
        return 0;
    /* stable insertion sort by freq (base order already sym ASC). */
    /* R13-L0RES-D1: radix above gate (identical total order: stable
     * ASC keeps collected-order ties, same as insertion). */
    if (n1 >= 64u) {
        lzmesh_u35_radix_idx(freq, q1, q1t, n1);
    } else {
        for (i = 1u; i < n1; i++) {
            uint16_t x = q1[i];
            unsigned j = i;
            while (j > 0u && freq[x] < freq[q1[j - 1u]]) {
                q1[j] = q1[j - 1u];
                j--;
            }
            q1[j] = x;
        }
    }
    for (i = 0u; i < 512u; i++) {
        fw[i] = 0u;
        par[i] = 0xFFFFu;
        chl[i] = 0xFFFFu;
        chr[i] = 0xFFFFu;
    }
    for (i = 0u; i < n1; i++)
        fw[q1[i]] = freq[q1[i]];
    nnode = nsym;
    p1 = 0u;
    p2 = 0u;
    n2 = 0u;
    nn = n1;
    while (nn > 1u) {
        uint16_t a, b;
        /* pop a: q1 iff q2 empty or q1.front <= q2.front. */
        if (n2 == p2)
            a = q1[p1++];
        else if (n1 == p1)
            a = q2[p2++];
        else if (fw[q1[p1]] <= fw[q2[p2]])
            a = q1[p1++];
        else
            a = q2[p2++];
        if (n2 == p2)
            b = q1[p1++];
        else if (n1 == p1)
            b = q2[p2++];
        else if (fw[q1[p1]] <= fw[q2[p2]])
            b = q1[p1++];
        else
            b = q2[p2++];
        if (nnode >= 512u || n2 >= 256u)
            return 0;
        chl[nnode] = a;
        chr[nnode] = b;
        par[a] = (uint16_t)nnode;
        par[b] = (uint16_t)nnode;
        fw[nnode] = fw[a] + fw[b];
        if (fw[nnode] < fw[a])
            return 0; /* u32 overflow guard */
        q2[n2++] = (uint16_t)nnode;
        nnode++;
        nn--;
    }
    /* depths by parent walk. */
    for (i = 0u; i < n1; i++) {
        unsigned d = 0u;
        uint16_t c = q1[i];
        while (par[c] != 0xFFFFu) {
            c = par[c];
            d++;
            if (d > 10u)
                return 0;
        }
        if (d == 0u || d > 10u)
            return 0;
        lens[q1[i]] = (uint8_t)d;
    }
    return 1;
}

/* Base-hw entry: sym ASC leaf order (V1 + lens tables unchanged). */
static int lzmesh_s3_huff(const uint32_t *freq, unsigned nsym,
    uint8_t *lens)
{
    return lzmesh_s3_huff_ord(freq, nsym, lens, NULL, 0u);
}

/* OR (elen e) at bit N into acc (cap bits). Truncates past cap.
 * P2-bitio: byte-at-a-time OR (same bits ORed, same truncation). */
static void lzmesh_s3_or(uint8_t *acc, unsigned cap, unsigned N,
    unsigned l, unsigned e)
{
    unsigned b = N;
    unsigned idx = N >> 3;
    unsigned sh = N & 7u;
    unsigned rem = l;
    while (rem > 0u && b < cap) {
        unsigned take = 8u - sh;
        if (take > rem)
            take = rem;
        if (take > cap - b)
            take = cap - b;
        acc[idx] |= (uint8_t)(((e & ((1u << take) - 1u))) << sh);
        e >>= take;
        rem -= take;
        b += take;
        idx++;
        sh = 0u;
    }
}

/* V-OR padsim. lanebase = dst+bo (payload lanes), start/laneb/bitc per
 * lane. sim_pad[k] out (masked to k bits). Returns verified-lane mask. */
static int lzmesh_s3_padsim(const uint8_t *str[4], const size_t strn[4],
    const unsigned mode[4], const lzmesh_g1_huff *h, uint32_t distc,
    const unsigned sufbits[8], const uint8_t *lanebase,
    const unsigned start[8], const unsigned laneb[8],
    const unsigned bitc[8], unsigned sim_pad[8])
{
    uint8_t acc[8][LZMESH_S3_MAXLANE];
    unsigned N[8], N1[8];
    unsigned k, i, q;
    uint8_t rlens[256];
    uint16_t rcodes[256];
    uint8_t rvals[256];
    uint32_t rbm = 0u;
    unsigned rused = 0u;
    uint8_t hlens[11];
    uint16_t hcodes[11];
    uint32_t vfreq[11];
    int have_len;
    if (str == 0 || strn == 0 || mode == 0 || h == 0 || sufbits == 0
        || lanebase == 0 || start == 0 || laneb == 0 || bitc == 0
        || sim_pad == 0)
        return 0;
    for (k = 0u; k < 8u; k++)
        sim_pad[k] = 0u;
    /* Gate: tok-only HUF; lit/len RAW; dist RAW-or-scalar (header byte,
     * no lane bits); suffix-free; small envelope (self-check guards). */
    if (mode[1] != 2u)
        return 0;
    if (mode[0] != 0u || mode[2] != 0u
        || (mode[3] != 0u && mode[3] != 1u))
        return 0;
    /* Dist contributes lane bits only via mode-2 writes (excluded by the
     * mode gate) or suffix (excluded below); distc/strn[3] alone (a
     * header byte) need not abstain. */
    (void)distc;
    if (strn[1] == 0u || strn[1] > LZMESH_S3_MAXTOK)
        return 0;
    /* len RAW envelope (big-RAW S2-deep still OPEN: T2-2 lens family). */
    if (strn[2] < LZMESH_S3_MINLEN || strn[2] > LZMESH_S3_MAXLEN)
        return 0;
    for (k = 0u; k < 8u; k++) {
        if (sufbits[k] != 0u)
            return 0;
        if (laneb[k] > LZMESH_S3_MAXLANE)
            return 0;
    }
    if (laneb[0] * 8u < LZMESH_S3_META)
        return 0;
    /* Gate: tok table sane (used lens + codes bounded). */
    if (h[1].used == 0u || h[1].used > 256u
        || (h[1].used & 7u) != 0u)
        return 0;
    for (i = 0u; i < h[1].used; i++) {
        unsigned v = h[1].vals[i];
        if (v > 10u || h[1].mlens[v] == 0u || h[1].mlens[v] > 5u)
            return 0;
    }
    for (i = 0u; i < (unsigned)strn[1]; i++) {
        unsigned v = str[1][i];
        if (h[1].lens[v] == 0u || h[1].lens[v] > 10u)
            return 0;
    }
    /* RAW len stream: two-queue lens + gather + lens-call table. */
    have_len = (strn[2] != 0u);
    if (have_len) {
        uint32_t freq[256];
        for (i = 0u; i < 256u; i++)
            freq[i] = 0u;
        for (i = 0u; i < (unsigned)strn[2]; i++)
            freq[str[2][i]]++;
        if (!lzmesh_s3_huff(freq, 256u, rlens))
            return 0;
        if (!lzmesh_pack1_canon(rlens, 256u, rcodes, 10u))
            return 0;
        if (!lzmesh_pack1_bitmap(rlens, &rbm))
            return 0;
        rused = lzmesh_pack1_gather(rlens, rbm, rvals);
        if (rused == 0u || rused > 256u || (rused & 7u) != 0u)
            return 0;
        for (i = 0u; i < 11u; i++)
            vfreq[i] = 0u;
        for (i = 0u; i < rused; i++) {
            if (rvals[i] > 10u)
                return 0;
            vfreq[rvals[i]]++;
        }
        if (!lzmesh_s3_huff(vfreq, 11u, hlens))
            return 0;
        if (!lzmesh_pack1_canon(hlens, 11u, hcodes, 10u))
            return 0;
        for (i = 0u; i < (unsigned)strn[2]; i++) {
            if (rlens[str[2][i]] == 0u || rlens[str[2][i]] > 10u)
                return 0;
        }
    }
    /* V-OR: tok lens-call + vals, then len lens-call + vals, q%8. */
    for (k = 0u; k < 8u; k++) {
        N[k] = 0u;
        N1[k] = 0u;
        for (i = 0u; i < laneb[k]; i++)
            acc[k][i] = 0u;
    }
    /* tok lens-call: h->vals + mcodes. */
    for (q = 0u; q < h[1].used; q++) {
        unsigned v = h[1].vals[q];
        unsigned l = h[1].mlens[v];
        unsigned e = h[1].mcodes[v];
        k = q & 7u;
        if (k == 0u)
            lzmesh_s3_or(acc[0], laneb[0] * 8u, LZMESH_S3_META + N[0],
                l, e);
        else
            lzmesh_s3_or(acc[k], laneb[k] * 8u, N[k], l, e);
        N[k] += l;
    }
    /* tok vals-call. */
    for (q = 0u; q < (unsigned)strn[1]; q++) {
        unsigned v = str[1][q];
        unsigned l = h[1].lens[v];
        unsigned e = h[1].codes[v];
        k = q & 7u;
        if (k == 0u)
            lzmesh_s3_or(acc[0], laneb[0] * 8u, LZMESH_S3_META + N[0],
                l, e);
        else
            lzmesh_s3_or(acc[k], laneb[k] * 8u, N[k], l, e);
        N[k] += l;
    }
    for (k = 0u; k < 8u; k++)
        N1[k] = N[k];
    if (have_len) {
        /* U4 S3-1 lane0-rule: S2-on-lane0 byte-fill (N1==8 -> S2@N1-2;
         * s12 15/15 FULL, ANSWER-S3-1 + u4_s12bed). Other N1: lane0
         * falls back (placement ambiguous: several offsets self-check
         * with different pads, s05 bed; OPEN: QUESTION-S3-1). Lanes
         * 1-7 are placement-free (concat stands, T2-2) and verify
         * independently. */
        if (laneb[0] * 8u - LZMESH_S3_META > N1[0] && N1[0] == 8u)
            N[0] = N1[0] - 2u;
        /* len lens-call. */
        for (q = 0u; q < rused; q++) {
            unsigned v = rvals[q];
            unsigned l = hlens[v];
            unsigned e = hcodes[v];
            k = q & 7u;
            if (l == 0u || l > 10u)
                return 0;
            if (k == 0u)
                lzmesh_s3_or(acc[0], laneb[0] * 8u,
                    LZMESH_S3_META + N[0], l, e);
            else
                lzmesh_s3_or(acc[k], laneb[k] * 8u, N[k], l, e);
            N[k] += l;
        }
        /* len vals-call. */
        for (q = 0u; q < (unsigned)strn[2]; q++) {
            unsigned v = str[2][q];
            unsigned l = rlens[v];
            unsigned e = rcodes[v];
            k = q & 7u;
            if (l == 0u || l > 10u)
                return 0;
            if (k == 0u)
                lzmesh_s3_or(acc[0], laneb[0] * 8u,
                    LZMESH_S3_META + N[0], l, e);
            else
                lzmesh_s3_or(acc[k], laneb[k] * 8u, N[k], l, e);
            N[k] += l;
        }
        /* lane0-rule: V-within-S1 (gate above) or byte-fill N1==8
         * (S2@N1-2); wider S2-on-lane0 still abstains (OPEN). */
    }
    /* U5 per-lane trust: calls are dealt q%8 into disjoint acc, so each
     * lane's OR-image verifies independently. Lane0 additionally needs
     * a bedded placement (no S2-on-lane0, or U4 N1==8 -> S2@N1-2);
     * other N1 falls back (ambiguous offsets self-check, s05 bed).
     * Returns bitmask of verified lanes (0 = full abstain). */
    {
        unsigned okmask = 0u;
        for (k = 0u; k < 8u; k++) {
            unsigned m, mask, j, ok;
            if (laneb[k] == 0u)
                continue;
            if (bitc[k] == 0u)
                continue;
            m = bitc[k] & 7u;
            ok = 1u;
            if (k == 0u) {
                /* lane0 acc holds V-OR only; meta (low 65 bits) trusted
                 * from emission. Check V-region payload bits. */
                unsigned nV = laneb[0] * 8u - LZMESH_S3_META;
                unsigned npay;
                if ((bitc[0] & 7u) != m)
                    continue;
                /* payload bits above meta: min(bitc0-65, nV). */
                if (bitc[0] < LZMESH_S3_META)
                    continue;
                if (have_len && nV > N1[0] && N1[0] != 8u)
                    continue; /* ambiguous placement: lane0 fallback */
                npay = bitc[0] - LZMESH_S3_META;
                if (npay > nV)
                    npay = nV;
                for (j = 0u; j < npay; j++) {
                    unsigned p = LZMESH_S3_META + j;
                    unsigned a = (acc[0][p >> 3] >> (p & 7u)) & 1u;
                    unsigned b = (lanebase[start[0] + (p >> 3)]
                        >> (p & 7u)) & 1u;
                    if (a != b) {
                        ok = 0u;
                        break;
                    }
                }
            } else {
                for (j = 0u; j + 1u < laneb[k]; j++) {
                    if (acc[k][j] != lanebase[start[k] + j]) {
                        ok = 0u;
                        break;
                    }
                }
                if (ok) {
                    mask = (m == 0u) ? 0xFFu : ((1u << m) - 1u);
                    if (((acc[k][laneb[k] - 1u]
                        ^ lanebase[start[k] + laneb[k] - 1u]) & mask)
                        != 0u)
                        ok = 0u;
                }
            }
            if (ok)
                okmask |= (1u << k);
        }
        /* Verified lanes: pads from sim last bytes. */
        for (k = 0u; k < 8u; k++) {
            unsigned m, kk, last;
            if (laneb[k] == 0u) {
                sim_pad[k] = 0u;
                continue;
            }
            m = bitc[k] & 7u;
            kk = (8u - m) & 7u;
            if (k == 0u) {
                /* lane0 acc holds V-OR at bit 65+; last byte mixes meta
                 * (emitted) + V-OR (sim). */
                unsigned p, byte = 0u;
                for (p = 0u; p < 8u; p++) {
                    unsigned gp = (laneb[0] - 1u) * 8u + p;
                    unsigned bit;
                    if (gp < LZMESH_S3_META)
                        bit = (lanebase[start[0] + (gp >> 3)]
                            >> (gp & 7u)) & 1u;
                    else
                        bit = (acc[0][gp >> 3] >> (gp & 7u)) & 1u;
                    byte |= bit << p;
                }
                last = byte;
            } else {
                last = acc[k][laneb[k] - 1u];
            }
            if (kk == 0u)
                sim_pad[k] = 0u;
            else
                sim_pad[k] = (last >> m) & ((1u << kk) - 1u);
        }
        return (int)okmask;
    }
}

/* === V1 multi/big-RAW padsim (wave-R v1-e05res) ===
 * Extends S3/U5 V-OR padsim to multi-HUF (lit+tok), lit-first (lit-HUF +
 * tok-RAW), and big len-RAW (strn up to 256): the e05 residue (LANE-U4
 * sec2 minus U5-claimed s05). Sim: HUF-prefix (consecutive mode-2
 * streams from the first HUF in S-order; RAW-before-HUF skipped per S3)
 * then the FIRST RAW stream's LENS-CALL ONLY (two-queue + gather + hw;
 * no RAW vals-call, no later streams). Bed: 33/33 diff-lane pads exact
 * on rescore-t e05 (tmp/v1b_pad.py); first-RAW-S2L == oracle head bits.
 * Trust (per-lane verified-only, U5 idiom): payload self-check PLUS
 * (a) slot gate: lane k < distc (suffix-slot lane) never trusted
 * (7 same-lanes falsify V-OR there: TRUE = B-TAB + pad0, tmp/v1c_port.py);
 * (b) coverage gate: first-RAW lens-call bits must cover the full pad
 * range (N2L[k] >= bitc[k]+kk[k]; excludes s00-class where pads reach
 * S2V/S3, tmp/v1d_cov.py); (c) tiny-stream defense: first-RAW strn < 11
 * abstains the cell (tok<=10 falsified: 15 NEWs); (d) lane0 under U5's
 * rule (N1==8 only;
 * auto-abstains all V1 cells). U5's envelope explicitly excluded (U5
 * owns s05-class; V1 fires only where U5 abstains). Returns verified-
 * lane bitmask (0 = full abstain). Never crashes: bounded loops. */
static int lzmesh_s3_padsim_v1(const uint8_t *str[4], const size_t strn[4],
    const unsigned mode[4], const lzmesh_g1_huff *h, uint32_t distc,
    const unsigned sufbits[8], const uint8_t *lanebase,
    const unsigned start[8], const unsigned laneb[8],
    const unsigned bitc[8], unsigned sim_pad[8])
{
    uint8_t acc[8][LZMESH_V1_MAXLANE];
    uint8_t w_acc0s1[LZMESH_V1_MAXLANE];
    unsigned N[8], N1[8], N2L[8], Ns[8];
    unsigned k, i, q, s, f, R;
    uint8_t rlens[256];
    uint8_t rvals[256];
    uint32_t rbm = 0u;
    unsigned rused = 0u;
    uint8_t hlens[11];
    uint16_t hcodes[11];
    uint32_t vfreq[11];
    (void)sufbits;
    if (str == 0 || strn == 0 || mode == 0 || h == 0
        || lanebase == 0 || start == 0 || laneb == 0 || bitc == 0
        || sim_pad == 0)
        return 0;
    for (k = 0u; k < 8u; k++)
        sim_pad[k] = 0u;
    /* Gate: len/dist-HUF unbedded (out of scope). */
    if (mode[2] == 2u || mode[3] == 2u)
        return 0;
    /* Gate: dist RAW-or-scalar only (U5 rationale: header byte). */
    if (mode[3] != 0u && mode[3] != 1u)
        return 0;
    /* Gate: need >=1 HUF among lit/tok. */
    if (mode[0] != 2u && mode[1] != 2u)
        return 0;
    /* Gate: U5's envelope excluded (U5 owns s05-class; no double-bed). */
    if (mode[1] == 2u && mode[0] == 0u && mode[2] == 0u
        && strn[1] >= (size_t)1u && strn[1] <= (size_t)LZMESH_S3_MAXTOK
        && strn[2] >= (size_t)LZMESH_S3_MINLEN
        && strn[2] <= (size_t)LZMESH_S3_MAXLEN)
        return 0;
    /* First HUF f in S-order (f in {0,1} given the gates above). */
    f = 4u;
    for (s = 0u; s < 4u; s++) {
        if (mode[s] == 2u) {
            f = s;
            break;
        }
    }
    if (f > 1u)
        return 0;
    /* First RAW R after HUF-prefix (mode-1 scalars skipped: no lane bits). */
    R = 4u;
    for (s = f; s < 4u; s++) {
        if (mode[s] == 0u) {
            R = s;
            break;
        }
    }
    if (R != 1u && R != 2u)
        return 0; /* dist-first-RAW (or none) unbedded */
    if (strn[R] < (size_t)11u)
        return 0; /* tiny-stream defense: tok<=10 falsified (15 NEWs,
         * s00 tok=4 + e01/textlike tok 8-10, tmp/v1f_newall.py);
         * bedded exact at tok=11 + len>=61 (tmp/v1b_pad.py) */
    for (k = 0u; k < 8u; k++) {
        if (laneb[k] > LZMESH_V1_MAXLANE)
            return 0;
    }
    if (laneb[0] * 8u < LZMESH_S3_META)
        return 0;
    /* Gate: every simmed HUF table sane (mirror U5 h[1] gates). */
    for (s = f; s < 4u && mode[s] == 2u; s++) {
        if (h[s].used == 0u || h[s].used > 256u
            || (h[s].used & 7u) != 0u)
            return 0;
        for (i = 0u; i < h[s].used; i++) {
            unsigned v = h[s].vals[i];
            if (v > 10u || h[s].mlens[v] == 0u || h[s].mlens[v] > 5u)
                return 0;
        }
        for (i = 0u; i < (unsigned)strn[s]; i++) {
            unsigned v = str[s][i];
            if (h[s].lens[v] == 0u || h[s].lens[v] > 10u)
                return 0;
        }
    }
    /* First-RAW lens-call table (two-queue + gather + hw). */
    {
        uint32_t freq[256];
        for (i = 0u; i < 256u; i++)
            freq[i] = 0u;
        for (i = 0u; i < (unsigned)strn[R]; i++)
            freq[str[R][i]]++;
        if (!lzmesh_s3_huff(freq, 256u, rlens))
            return 0;
        if (!lzmesh_pack1_bitmap(rlens, &rbm))
            return 0;
        rused = lzmesh_pack1_gather(rlens, rbm, rvals);
        if (rused == 0u || rused > 256u || (rused & 7u) != 0u)
            return 0;
        for (i = 0u; i < 11u; i++)
            vfreq[i] = 0u;
        for (i = 0u; i < rused; i++) {
            if (rvals[i] > 10u)
                return 0;
            vfreq[rvals[i]]++;
        }
        if (!lzmesh_s3_huff(vfreq, 11u, hlens))
            return 0;
        if (!lzmesh_pack1_canon(hlens, 11u, hcodes, 10u))
            return 0;
    }
    /* V-OR: HUF-prefix lens-call + vals, then first-RAW lens-call, q%8. */
    for (k = 0u; k < 8u; k++) {
        N[k] = 0u;
        N1[k] = 0u;
        N2L[k] = 0u;
        for (i = 0u; i < laneb[k]; i++)
            acc[k][i] = 0u;
    }
    for (s = f; s < 4u && mode[s] == 2u; s++) {
        for (q = 0u; q < h[s].used; q++) {
            unsigned v = h[s].vals[q];
            unsigned l = h[s].mlens[v];
            unsigned e = h[s].mcodes[v];
            k = q & 7u;
            if (k == 0u)
                lzmesh_s3_or(acc[0], laneb[0] * 8u, LZMESH_S3_META + N[0],
                    l, e);
            else
                lzmesh_s3_or(acc[k], laneb[k] * 8u, N[k], l, e);
            N[k] += l;
        }
        for (q = 0u; q < (unsigned)strn[s]; q++) {
            unsigned v = str[s][q];
            unsigned l = h[s].lens[v];
            unsigned e = h[s].codes[v];
            k = q & 7u;
            if (k == 0u)
                lzmesh_s3_or(acc[0], laneb[0] * 8u, LZMESH_S3_META + N[0],
                    l, e);
            else
                lzmesh_s3_or(acc[k], laneb[k] * 8u, N[k], l, e);
            N[k] += l;
        }
    }
    for (k = 0u; k < 8u; k++)
        N1[k] = N[k];
    /* W-E01BULK: snapshot lane0 S1 image for unique-fit retries below. */
    {
        for (i = 0u; i < laneb[0]; i++)
            w_acc0s1[i] = acc[0][i];
    }
    /* U4 S3-1 lane0-rule (N1==8 -> S2@N1-2; else lane0 falls back below). */
    if (laneb[0] * 8u - LZMESH_S3_META > N1[0] && N1[0] == 8u)
        N[0] = N1[0] - 2u;
    /* V-E01BULK: R==1 (tok-lens-call) S2@N1-1 on k!=0 lanes (s02 e01 L5/6/7
     * pads 8/0/2 exact vs inherit 0/0/4; 6 tie-break variants falsified).
     * R==2 untouched (30 e05 inherit-confirmed). Gates: N1>=8 (s16-n64 e05
     * N1=3/lastL=1 is inherit-TRUE; 2-point fit, battery+holdout guard)
     * + post-shift coverage (s18-n128 L4: N2L 32->31 loses coverage and
     * EVOs; shift only if N1-1+s2len still covers bitc+kk). Verify below
     * re-checks the overlapped S1-tail bit; slot gate unchanged. */
    if (R == 1u) {
        unsigned s2len[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
        for (q = 0u; q < rused; q++)
            s2len[q & 7u] += hlens[rvals[q]];
        for (k = 1u; k < 8u; k++) {
            unsigned m = bitc[k] & 7u;
            unsigned kk = (8u - m) & 7u;
            if (N[k] >= 8u
                && N[k] - 1u + s2len[k] >= bitc[k] + kk)
                N[k] = N[k] - 1u;
        }
    }
    /* X-E01RAZOR: S2-start snapshot for the R==1 vacuity gate. */
    for (k = 0u; k < 8u; k++)
        Ns[k] = N[k];
    for (q = 0u; q < rused; q++) {
        unsigned v = rvals[q];
        unsigned l = hlens[v];
        unsigned e = hcodes[v];
        k = q & 7u;
        if (l == 0u || l > 10u)
            return 0;
        if (k == 0u)
            lzmesh_s3_or(acc[0], laneb[0] * 8u,
                LZMESH_S3_META + N[0], l, e);
        else
            lzmesh_s3_or(acc[k], laneb[k] * 8u, N[k], l, e);
        N[k] += l;
    }
    for (k = 0u; k < 8u; k++)
        N2L[k] = N[k];
    /* Per-lane verified-only trust (U5 idiom) + slot + coverage gates. */
    {
        unsigned okmask = 0u;
        for (k = 0u; k < 8u; k++) {
            unsigned m, mask, j, ok, kk;
            if (laneb[k] == 0u)
                continue;
            if (bitc[k] == 0u)
                continue;
            if (k < distc)
                continue; /* slot gate: suffix-slot lanes keep B-TAB */
            m = bitc[k] & 7u;
            kk = (8u - m) & 7u;
            if (k == 0u) {
                unsigned nV = laneb[0] * 8u - LZMESH_S3_META;
                unsigned npay;
                if (bitc[0] < LZMESH_S3_META)
                    continue;
                /* W-E01BULK: minimal-overlap lane0 trust (shifts 0,-1,-2
                 * in order, first data-verifier wins; U4 N1==8 shift-2
                 * kept above). s04-n1000 N1=516: shifts 0 AND -1 fit data
                 * (S2[0]=1 overlaps a data-1 at -1), pads differ (1 vs 0);
                 * concat-default picks 0 = TRUE. None verifies or
                 * uncovered -> U5 fallback (status quo). */
                if (nV > N1[0] && N1[0] != 8u) {
                    unsigned sh, nfit = 0u, best = 0u;
                    uint8_t try_[LZMESH_V1_MAXLANE];
                    unsigned best_end = 0u;
                    for (sh = 0u; sh < 3u && nfit == 0u; sh++) {
                        unsigned nt = (N1[0] >= sh) ? N1[0] - sh : 0u;
                        unsigned qq, jj, vok = 1u;
                        if (N1[0] < sh)
                            continue;
                        for (jj = 0u; jj < laneb[0]; jj++)
                            try_[jj] = w_acc0s1[jj];
                        for (qq = 0u; qq < rused; qq++) {
                            unsigned vv, ll, ee;
                            if ((qq & 7u) != 0u)
                                continue;
                            vv = rvals[qq];
                            ll = hlens[vv];
                            ee = hcodes[vv];
                            lzmesh_s3_or(try_, laneb[0] * 8u,
                                LZMESH_S3_META + nt, ll, ee);
                            nt += ll;
                        }
                        if (nt + LZMESH_S3_META < bitc[0] + kk)
                            continue; /* coverage */
                        npay = bitc[0] - LZMESH_S3_META;
                        if (npay > nV)
                            npay = nV;
                        for (jj = 0u; jj < npay; jj++) {
                            unsigned pp = LZMESH_S3_META + jj;
                            unsigned a = (try_[pp >> 3] >> (pp & 7u))
                                & 1u;
                            unsigned bb = (lanebase[start[0]
                                + (pp >> 3)] >> (pp & 7u)) & 1u;
                            if (a != bb) {
                                vok = 0u;
                                break;
                            }
                        }
                        if (vok) {
                            nfit++;
                            best = sh;
                            best_end = nt;
                        }
                    }
                    if (nfit != 1u)
                        continue; /* ambiguous: U5 fallback */
                    {
                        unsigned nt = N1[0] - best;
                        unsigned qq, jj;
                        for (jj = 0u; jj < laneb[0]; jj++)
                            acc[0][jj] = w_acc0s1[jj];
                        for (qq = 0u; qq < rused; qq++) {
                            unsigned vv, ll, ee;
                            if ((qq & 7u) != 0u)
                                continue;
                            vv = rvals[qq];
                            ll = hlens[vv];
                            ee = hcodes[vv];
                            lzmesh_s3_or(acc[0], laneb[0] * 8u,
                                LZMESH_S3_META + nt, ll, ee);
                            nt += ll;
                        }
                        N2L[0] = best_end;
                    }
                }
                if (N2L[0] + LZMESH_S3_META < bitc[0] + kk)
                    continue; /* coverage */
                npay = bitc[0] - LZMESH_S3_META;
                if (npay > nV)
                    npay = nV;
                ok = 1u;
                for (j = 0u; j < npay; j++) {
                    unsigned p = LZMESH_S3_META + j;
                    unsigned a = (acc[0][p >> 3] >> (p & 7u)) & 1u;
                    unsigned b = (lanebase[start[0] + (p >> 3)]
                        >> (p & 7u)) & 1u;
                    if (a != b) {
                        ok = 0u;
                        break;
                    }
                }
            } else {
                if (N2L[k] < bitc[k] + kk)
                    continue; /* coverage: pads must lie in S2L */
                /* X-E01RAZOR: R==1 vacuous trusts (S2 starts at/past
                 * data end: zero S2 bits verified) abstain when the
                 * len-call is simmable (len-RAW strn>=11: overhang is
                 * the len-call, V2small takes it). s18-n128 lane4
                 * (Ns=27=bitc, len=11) sims 1c vs TRUE 02 -> abstain;
                 * s16-n64 (len=3) kept; s02-e01 shifted (Ns<bitc,
                 * non-vacuous) kept. */
                if (R == 1u && Ns[k] >= bitc[k] && mode[2] == 0u
                    && strn[2] >= (size_t)11u)
                    continue;
                ok = 1u;
                for (j = 0u; j + 1u < laneb[k]; j++) {
                    if (acc[k][j] != lanebase[start[k] + j]) {
                        ok = 0u;
                        break;
                    }
                }
                if (ok) {
                    mask = (m == 0u) ? 0xFFu : ((1u << m) - 1u);
                    if (((acc[k][laneb[k] - 1u]
                        ^ lanebase[start[k] + laneb[k] - 1u]) & mask)
                        != 0u)
                        ok = 0u;
                }
            }
            if (ok)
                okmask |= (1u << k);
        }
        for (k = 0u; k < 8u; k++) {
            unsigned m, kk, last;
            if (laneb[k] == 0u) {
                sim_pad[k] = 0u;
                continue;
            }
            m = bitc[k] & 7u;
            kk = (8u - m) & 7u;
            if (k == 0u) {
                unsigned p, byte = 0u;
                for (p = 0u; p < 8u; p++) {
                    unsigned gp = (laneb[0] - 1u) * 8u + p;
                    unsigned bit;
                    if (gp < LZMESH_S3_META)
                        bit = (lanebase[start[0] + (gp >> 3)]
                            >> (gp & 7u)) & 1u;
                    else
                        bit = (acc[0][gp >> 3] >> (gp & 7u)) & 1u;
                    byte |= bit << p;
                }
                last = byte;
            } else {
                last = acc[k][laneb[k] - 1u];
            }
            if (kk == 0u)
                sim_pad[k] = 0u;
            else
                sim_pad[k] = (last >> m) & ((1u << kk) - 1u);
        }
        return (int)okmask;
    }
}

#define LZMESH_V2SM_MAXLANE 4096u /* s09 blk5 lanes ~2899B (S3's 256B cap abstains) */
/* V2small: deal one lens/vals call into acc/N (q%8, lane0 meta offset).
 * vals[nq] syms via (lens,codes). Returns 0 on bad depth (abstain). */
static int lzmesh_v2sm_deal(uint8_t acc[][LZMESH_V2SM_MAXLANE],
    unsigned N[8], const unsigned laneb[8], const uint8_t *vals,
    unsigned nq, const uint8_t *lens, const uint16_t *codes)
{
    unsigned q;
    for (q = 0u; q < nq; q++) {
        unsigned v = vals[q];
        unsigned l = lens[v];
        unsigned e = codes[v];
        unsigned k = q & 7u;
        if (l == 0u || l > 10u)
            return 0;
        if (k == 0u)
            lzmesh_s3_or(acc[0], laneb[0] * 8u, LZMESH_S3_META + N[0],
                l, e);
        else
            lzmesh_s3_or(acc[k], laneb[k] * 8u, N[k], l, e);
        N[k] += l;
    }
    return 1;
}

/* V2small: 1 iff sim lane matches emitted payload (all but last byte
 * fully, last byte mask bits). lanebk>=1 (caller gates 0). */
static int lzmesh_v2sm_verify(const uint8_t *accl,
    const uint8_t *lanebase, unsigned startk, unsigned lanebk,
    unsigned m)
{
    unsigned j, mask;
    for (j = 0u; j + 1u < lanebk; j++) {
        if (accl[j] != lanebase[startk + j])
            return 0;
    }
    mask = (m == 0u) ? 0xFFu : ((1u << m) - 1u);
    if (((accl[lanebk - 1u] ^ lanebase[startk + lanebk - 1u]) & mask)
        != 0u)
        return 0;
    return 1;
}

/* === V2small padsim (wave-U u-e05trio; LANE-U-E05TRIO) ===
 * Small-builder pads for the lit-HUF + tok-RAW-tiny + len-RAW-big
 * envelope (s00-n4096 e05 3B + s09-n262144 e05 blk5 3B): the pads are
 * the SECOND raw (len) lens-call + vals-call head, skipping the tiny
 * first raw (tok). Bed: 6/6 diff lanes bit-exact (offsets 2/3 == the
 * per-lane tok-sim length; tmp/u-e05trio/u_replay.py) + 24/30 mined
 * tok<=4 lanes (u_mine.py); s00/s09 TRUE windows reach len-vals by up
 * to 1 bit (s09 l2 kk=7), so vals-call is simmed (unlike V1 lens-only).
 * Skip scan: first-RAW strn>=11 stays V1's (s16-n64 tok=11 exact);
 * len strn<11 abstains (all-tiny class = PAD1/B-TAB fallback, seed5
 * blk lastL=7 lane verified). Dist is NOT simmed (no TRUE window
 * reaches it; coverage abstains there). Trust: U5 idiom (payload
 * self-check) + slot gate + sufbit gate + coverage gate; lane0 always
 * abstains (no S3-1 bed for the skipped-prefix S2; trio lane0s are
 * slot/suf anyway). Vals hw TWIN (wave-V v-e05trio): base-hw + firstocc
 * tables simmed side by side, per-lane union (TRUE tiebreak is
 * content-dependent: s11-e01 base-hw, s4l5 firstocc; firstocc
 * false-verifies on s11 so base wins ties). Chain: fires only where
 * U5+V1 abstain (tiny
 * defense). Returns verified-lane bitmask (0 = full abstain). */
static int lzmesh_s3_padsim_v2small(const uint8_t *str[4],
    const size_t strn[4], const unsigned mode[4],
    const lzmesh_g1_huff *h, uint32_t distc,
    const unsigned sufbits[8], const uint8_t *lanebase,
    const unsigned start[8], const unsigned laneb[8],
    const unsigned bitc[8], unsigned sim_pad[8])
{
    uint8_t acc[8][LZMESH_V2SM_MAXLANE];
    uint8_t accF[8][LZMESH_V2SM_MAXLANE];
    unsigned N[8], NF[8];
    unsigned k, i, s;
    uint8_t rlens[256];
    uint16_t rcodes[256];
    uint8_t rvals[256];
    uint32_t rbm = 0u;
    unsigned rused = 0u;
    uint8_t hlens[11];
    uint16_t hcodes[11];
    uint8_t hlensF[11];
    uint16_t hcodesF[11];
    uint32_t vfreq[11];
    if (str == 0 || strn == 0 || mode == 0 || h == 0
        || lanebase == 0 || start == 0 || laneb == 0 || bitc == 0
        || sim_pad == 0)
        return 0;
    for (k = 0u; k < 8u; k++)
        sim_pad[k] = 0u;
    /* Gate: strict small-builder envelope (lit-HUF + tok-RAW + len-RAW;
     * dist RAW-or-scalar). len/dist-HUF unbedded (V1 rationale). */
    if (mode[2] == 2u || mode[3] == 2u)
        return 0;
    if (mode[3] != 0u && mode[3] != 1u)
        return 0;
    if (mode[0] != 2u || mode[1] != 0u || mode[2] != 0u)
        return 0;
    /* Gate: tok simmable (-> V1) vs tiny (skip); len must be simmable.
     * X-E01RAZOR: s18-n128 e01 tok=12 is skipped (V2small len-call 6/6
     * exact where V1 vacuous-trusts wrong); allow tok<=12. */
    if (strn[1] > (size_t)12u)
        return 0;
    if (strn[2] < (size_t)11u)
        return 0;
    for (k = 0u; k < 8u; k++) {
        if (laneb[k] > LZMESH_V2SM_MAXLANE)
            return 0;
    }
    if (laneb[0] * 8u < LZMESH_S3_META)
        return 0;
    /* Gate: simmed HUF table sane (mirror U5 h[1] gates; s=0 only here). */
    for (s = 0u; s < 4u && mode[s] == 2u; s++) {
        if (h[s].used == 0u || h[s].used > 256u
            || (h[s].used & 7u) != 0u)
            return 0;
        for (i = 0u; i < h[s].used; i++) {
            unsigned v = h[s].vals[i];
            if (v > 10u || h[s].mlens[v] == 0u || h[s].mlens[v] > 5u)
                return 0;
        }
        for (i = 0u; i < (unsigned)strn[s]; i++) {
            unsigned v = str[s][i];
            if (h[s].lens[v] == 0u || h[s].lens[v] > 10u)
                return 0;
        }
    }
    /* Len lens-call + vals-call tables (two-queue + gather + hw). */
    {
        uint32_t freq[256];
        for (i = 0u; i < 256u; i++)
            freq[i] = 0u;
        for (i = 0u; i < (unsigned)strn[2]; i++)
            freq[str[2][i]]++;
        if (!lzmesh_s3_huff(freq, 256u, rlens))
            return 0;
        if (!lzmesh_pack1_canon(rlens, 256u, rcodes, 10u))
            return 0;
        if (!lzmesh_pack1_bitmap(rlens, &rbm))
            return 0;
        rused = lzmesh_pack1_gather(rlens, rbm, rvals);
        if (rused == 0u || rused > 256u || (rused & 7u) != 0u)
            return 0;
        for (i = 0u; i < 11u; i++)
            vfreq[i] = 0u;
        for (i = 0u; i < rused; i++) {
            if (rvals[i] > 10u)
                return 0;
            vfreq[rvals[i]]++;
        }
        if (!lzmesh_s3_huff(vfreq, 11u, hlens))
            return 0;
        if (!lzmesh_pack1_canon(hlens, 11u, hcodes, 10u))
            return 0;
        /* firstocc-vals twin (ANSWER-u-e05trio-2; s4l5-class): leaf order
         * = first lseq occurrence. TRUE tiebreak is content-dependent
         * (s11-e01 wants sym-asc, s4l5 wants firstocc), so BOTH tables
         * are simmed and each lane takes a verified one. F-build
         * failure collapses to base (base-only behavior). */
        {
            uint16_t ford[11];
            unsigned nf = 0u, seen = 0u;
            int fok;
            for (i = 0u; i < rused; i++) {
                if ((seen & (1u << rvals[i])) == 0u) {
                    seen |= (1u << rvals[i]);
                    ford[nf++] = (uint16_t)rvals[i];
                }
            }
            fok = lzmesh_s3_huff_ord(vfreq, 11u, hlensF, ford, nf);
            if (fok)
                fok = lzmesh_pack1_canon(hlensF, 11u, hcodesF, 10u);
            if (!fok) {
                for (i = 0u; i < 11u; i++) {
                    hlensF[i] = hlens[i];
                    hcodesF[i] = hcodes[i];
                }
            }
        }
        for (i = 0u; i < (unsigned)strn[2]; i++) {
            if (rlens[str[2][i]] == 0u || rlens[str[2][i]] > 10u)
                return 0;
        }
    }
    /* V-OR twin: lit-HUF-prefix + len lens-call + vals dealt under BOTH
     * vals tables (base-hw acc/N, firstocc accF/NF); len-vals table is
     * shared (base-hw). (tok skipped: tiny). q%8 dealing. */
    for (k = 0u; k < 8u; k++) {
        N[k] = 0u;
        NF[k] = 0u;
        for (i = 0u; i < laneb[k]; i++) {
            acc[k][i] = 0u;
            accF[k][i] = 0u;
        }
    }
    for (s = 0u; s < 4u && mode[s] == 2u; s++) {
        if (!lzmesh_v2sm_deal(acc, N, laneb, h[s].vals, h[s].used,
            h[s].mlens, h[s].mcodes))
            return 0;
        if (!lzmesh_v2sm_deal(accF, NF, laneb, h[s].vals, h[s].used,
            h[s].mlens, h[s].mcodes))
            return 0;
        if (!lzmesh_v2sm_deal(acc, N, laneb, str[s], (unsigned)strn[s],
            h[s].lens, h[s].codes))
            return 0;
        if (!lzmesh_v2sm_deal(accF, NF, laneb, str[s],
            (unsigned)strn[s], h[s].lens, h[s].codes))
            return 0;
    }
    /* Len lens-call (twin tables). */
    if (!lzmesh_v2sm_deal(acc, N, laneb, rvals, rused, hlens, hcodes))
        return 0;
    if (!lzmesh_v2sm_deal(accF, NF, laneb, rvals, rused, hlensF,
        hcodesF))
        return 0;
    /* Len vals-call (shared table). */
    if (!lzmesh_v2sm_deal(acc, N, laneb, str[2], (unsigned)strn[2],
        rlens, rcodes))
        return 0;
    if (!lzmesh_v2sm_deal(accF, NF, laneb, str[2], (unsigned)strn[2],
        rlens, rcodes))
        return 0;
    /* Per-lane verified-only trust, UNION over twin tables + slot +
     * sufbit + coverage gates. Lane0 always abstains (no S3-1 bed for
     * skipped-prefix S2). Base wins ties (firstocc false-verifies on
     * s11-e01); firstocc only where base abstains. */
    {
        unsigned okmask = 0u;
        unsigned ch[8];
        for (k = 0u; k < 8u; k++)
            ch[k] = 2u;
        for (k = 0u; k < 8u; k++) {
            unsigned m, kk, okB, okF;
            if (k == 0u)
                continue;
            if (laneb[k] == 0u)
                continue;
            if (bitc[k] == 0u)
                continue;
            if (k < distc)
                continue; /* slot gate: suffix-slot lanes keep B-TAB */
            if (sufbits == 0)
                return 0;
            if (sufbits[k] != 0u)
                continue; /* suffix lanes keep fallback */
            m = bitc[k] & 7u;
            kk = (8u - m) & 7u;
            okB = 0u;
            okF = 0u;
            if (N[k] >= bitc[k] + kk
                && lzmesh_v2sm_verify(acc[k], lanebase, start[k],
                    laneb[k], m))
                okB = 1u;
            if (NF[k] >= bitc[k] + kk
                && lzmesh_v2sm_verify(accF[k], lanebase, start[k],
                    laneb[k], m))
                okF = 1u;
            /* base-first: firstocc false-verifies on s11-e01 (payload
             * matches, pads wrong), so base wins ties; firstocc only
             * where base abstains (s4l5-class). */
            if (okB != 0u) {
                ch[k] = 0u;
            } else if (okF != 0u) {
                ch[k] = 1u;
            } else {
                continue;
            }
            okmask |= (1u << k);
        }
        for (k = 0u; k < 8u; k++) {
            unsigned m, kk, last;
            if (laneb[k] == 0u) {
                sim_pad[k] = 0u;
                continue;
            }
            m = bitc[k] & 7u;
            kk = (8u - m) & 7u;
            last = (ch[k] == 1u) ? accF[k][laneb[k] - 1u]
                : acc[k][laneb[k] - 1u];
            if (kk == 0u)
                sim_pad[k] = 0u;
            else
                sim_pad[k] = (last >> m) & ((1u << kk) - 1u);
        }
        return (int)okmask;
    }
}

/* GEN Huffman emit (L1-only). Streams in fetch order; S3.6 lane
 * chain; PAD1 + B-TAB pads. Returns bytes or 0 (RAW fallback). */
static size_t lzmesh_g1_emit(uint8_t *dst, size_t dst_capacity,
                             const uint8_t *lit, size_t li,
                             const uint8_t *tok, size_t ti,
                             const uint8_t *len, size_t eni,
                             const uint8_t *dsym, size_t di,
                             const lzmesh_u37_tok *toks, size_t ntok,
                             uint32_t litc, uint32_t tokc, uint32_t lenc,
                             uint32_t distc, uint32_t ds) {
    const uint8_t *str[4];
    size_t strn[4];
    lzmesh_g1_huff h[4];
    unsigned mode[4];
    unsigned s, i, k;
    unsigned bitc[8], laneb[8], start[8], pos[8], sufbits[8];
    lzmesh_u35_acc uacc[8]; /* P6-W4 */
    uint8_t lastL[8];
    uint8_t idx[24];
    unsigned idxsz;
    uint32_t bo, fo, payload = 0u, modes;
    size_t need, b, t;
    if (dst == NULL || lit == NULL || tok == NULL || len == NULL
        || dsym == NULL || toks == NULL)
        return 0u;
    str[0] = lit;
    str[1] = tok;
    str[2] = len;
    str[3] = dsym;
    strn[0] = li;
    strn[1] = ti;
    strn[2] = eni;
    strn[3] = di;
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        size_t n = strn[s];
        int alleq = 1;
        mode[s] = 0u;
        if (n == 0u)
            continue;
        for (i = 1u; i < n; i++) {
            if (sp[i] != sp[0]) {
                alleq = 0;
                break;
            }
        }
        if (alleq) {
            mode[s] = 1u;
            continue;
        }
        if (n <= 10u)
            continue;
        if (!lzmesh_g1_build(sp, n, &h[s]))
            continue;
        if (65u + h[s].pack_bits + h[s].data_bits
            >= (uint64_t)8u * n)
            continue; /* S5.5 rollback: RAW */
        mode[s] = 2u;
    }
    if (mode[0] == 1u && litc > LZMESH_U37_LITREPMAX)
        mode[0] = 0u; /* S4.2 ceiling (u37 RAW path mirrors) */
    if (mode[0] != 2u && mode[1] != 2u && mode[2] != 2u
        && mode[3] != 2u)
        return 0u; /* no-HUF: caller RAW path (byte-identical) */
    {
        uint64_t b64 = 9u;
        for (s = 0u; s < 4u; s++) {
            if (mode[s] == 2u)
                continue;
            else if (mode[s] == 1u)
                b64 += 1u;
            else
                b64 += (uint64_t)strn[s];
        }
        if (b64 > (uint64_t)0xFFFFu)
            return 0u;
        bo = (uint32_t)b64;
    }
    for (k = 0u; k < 8u; k++) {
        bitc[k] = 0u;
        sufbits[k] = 0u;
    }
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        if (mode[s] != 2u)
            continue;
        bitc[0] += 33u + 32u;
        for (i = 0u; i < h[s].used; i++)
            bitc[i & 7u] += h[s].mlens[h[s].vals[i]];
        for (i = 0u; i < strn[s]; i++)
            bitc[i & 7u] += h[s].lens[sp[i]];
    }
    {
        uint32_t slot = 0u;
        for (t = 0u; t < ntok; t++) {
            unsigned sb;
            if (!toks[t].is_new)
                continue;
            sb = lzmesh_u3_sb_of(toks[t].dist);
            bitc[slot % 8u] += sb;
            sufbits[slot % 8u] += sb;
            slot++;
        }
        if (slot != distc)
            return 0u;
    }
    for (k = 0u; k < 8u; k++) {
        laneb[k] = (bitc[k] + 7u) >> 3;
        payload += laneb[k];
    }
    if (payload == 0u)
        return 0u;
    idxsz = lzmesh_pack1_index_pack(laneb, idx, (unsigned)sizeof idx);
    if (idxsz == 0u)
        return 0u;
    if ((uint64_t)bo + (uint64_t)payload + (uint64_t)idxsz
        > (uint64_t)0xFFFFu)
        return 0u;
    fo = bo + payload + idxsz;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0u;
    modes = lzmesh_u4_modes_pack(mode[1], mode[2], mode[0], mode[3]);
    need = (size_t)fo + 10u + 1u;
    if (dst_capacity < need)
        return 0u;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    b = 9u;
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        if (mode[s] == 2u)
            continue;
        else if (mode[s] == 1u)
            dst[b++] = sp[0];
        else {
            /* P15-PACK P2b (was byte loop). */
            memmove(dst + b, sp, strn[s]);
            b += strn[s];
        }
    }
    if (b != bo)
        return 0u;
    for (k = 0u; k < 8u; k++) {
        start[k] = (unsigned)(b - bo);
        b += laneb[k];
        pos[k] = 0u;
        lastL[k] = 0u; /* P15-PACK P1: init precedes fused emit stores. */
        uacc[k].acc = 0u; /* P6-W4 */
        uacc[k].nbits = 0u;
        uacc[k].out = dst + bo + start[k];
    }
    memset(dst + bo, 0, payload); /* P15-PACK P2a (was byte loop). */
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        if (mode[s] != 2u)
            continue;
        for (i = 0u; i < 11u; i++)
            lzmesh_u35_acc_put_fast(&uacc[0], &pos[0],
                                    h[s].mlens[i], 3u);
        lzmesh_u35_acc_put_fast(&uacc[0], &pos[0], h[s].bm, 32u);
        for (i = 0u; i < h[s].used; i++)
            lzmesh_u35_acc_put_fast(&uacc[i & 7u], &pos[i & 7u],
                                    h[s].mcodes[h[s].vals[i]],
                                    h[s].mlens[h[s].vals[i]]);
        for (i = 0u; i < strn[s]; i++) {
            lzmesh_u35_acc_put_fast(&uacc[i & 7u], &pos[i & 7u],
                                    h[s].codes[sp[i]],
                                    h[s].lens[sp[i]]);
            /* P16-PACK T2b: lastL-direct (store deleted; computed below). */
        }
    }
    /* P16-PACK T2b: lastL-direct (last write wins; 8 computes vs 70k stores;
     * bed2 strn=69889 tx-e09; FULL-gated). */
    for (k = 0u; k < 8u; k++) {
        unsigned ll = 0u;
        for (s = 4u; s > 0u; ) {
            size_t last;
            --s;
            if (mode[s] != 2u)
                continue;
            if (strn[s] > k) {
                last = strn[s] - 1u - ((strn[s] - 1u - k) & 7u);
                ll = h[s].lens[str[s][last]];
                break;
            }
        }
        lastL[k] = (uint8_t)ll;
    }
    for (k = 0u; k < 8u; k++)
        lzmesh_u35_acc_flush(&uacc[k]); /* P6-W4: drain pre-suffix loop */
    {
        uint32_t slot = 0u;
        for (t = 0u; t < ntok; t++) {
            unsigned sb, low, bb;
            uint32_t suf, kk;
            if (!toks[t].is_new)
                continue;
            lzmesh_u4_dist_split_nc(toks[t].dist, &sb, &low, &suf);
            kk = slot % 8u;
            for (bb = 0u; bb < sb; bb++) {
                unsigned p = pos[kk]++;
                if (((suf >> bb) & 1u) != 0u)
                    dst[bo + start[kk] + (p >> 3)]
                        |= (uint8_t)(1u << (p & 7u));
            }
            slot++;
        }
    }
    for (k = 0u; k < 8u; k++) {
        if (pos[k] != bitc[k])
            return 0u;
    }
    /* P15-PACK P1: lastL walk deleted (fused into vals-emit loop). */
    /* S3 V-OR padsim override (U5 per-lane verified-only; unverified
     * lanes keep PAD1/B-TAB). */
    {
        unsigned s3_pad[8];
        int s3_ok = lzmesh_s3_padsim(
            str, strn, mode, h, distc, sufbits,
            dst + bo, start, laneb, bitc, s3_pad);
        if (s3_ok == 0) /* V1: multi/lit-first/big-RAW fallback (U5 abstains) */
            s3_ok = lzmesh_s3_padsim_v1(
                str, strn, mode, h, distc, sufbits,
                dst + bo, start, laneb, bitc, s3_pad);
        if (s3_ok == 0) /* V2small: tok-tiny skip + len sim (V1 abstains) */
            s3_ok = lzmesh_s3_padsim_v2small(
                str, strn, mode, h, distc, sufbits,
                dst + bo, start, laneb, bitc, s3_pad);
        for (k = 0u; k < 8u; k++) {
            unsigned m = bitc[k] & 7u;
            unsigned kk = (8u - m) & 7u;
            unsigned fld = 0u;
            if (m == 0u)
                continue;
            if (laneb[k] == 0u)
                return 0u;
            if ((s3_ok & (1 << k)) != 0) {
                fld = s3_pad[k];
            } else {
                if (lzmesh_u4_pad0_one(distc, k))
                    fld |= 1u;
                if (sufbits[k] == 0u)
                    fld |= lzmesh_pack1_btab(lastL[k], kk);
                /* X-E01RAZOR: B-TAB row lastL=1 (kk 6,7) = bit0.
                 * s03-e01 H3 lanes 3,4 (lastL=1, suf=0, non-suffix)
                 * TRUE pad 0x01 vs table 0x00; kk<=5 stays 0 (s01-n61
                 * lanes 5,6,7 TRUE 00). Narrower than the table: the
                 * u35 decline gate reads btab(1,*) as 0 (load-bearing
                 * on e00 tiny lanes), so this lives in the fallback. */
                if (sufbits[k] == 0u && lastL[k] == 1u
                    && !lzmesh_u4_pad0_one(distc, k)
                    && (kk == 6u || kk == 7u))
                    fld |= 1u;
                /* AA-SPARSE: B-TAB row lastL=2 (kk 7) = bit0, lane0 only.
                 * s17-n1025-sparse e05+e09 lane0 (lastL=2, suf=0,
                 * non-suffix, V1-abstain: N1==8 U4-shift verify-fail)
                 * TRUE pad 0x01 vs table 0x00; V-OR S2@8 verifies
                 * data but predicts 03 (residue falsified: rule-based).
                 * Census 2/2 (holdout + s07 twin), 0 contra in 76
                 * sparse cells. kk==7 only (no kk=6 evidence); lane0
                 * only (s09-n16384-e01 lane5 lastL=2/kk=7 TRUE=00:
                 * full-battery NEW veto). Lives in the fallback
                 * (btab(2,*) stays 0 for the u35 decline gate). */
                if (k == 0u && sufbits[k] == 0u && lastL[k] == 2u
                    && !lzmesh_u4_pad0_one(distc, k)
                    && kk == 7u)
                    fld |= 1u;
            }
            fld &= (kk >= 8u) ? 0xFFu : ((1u << kk) - 1u);
            dst[bo + start[k] + laneb[k] - 1u] |=
                (uint8_t)((fld << m) & 0xFFu);
        }
    }
    for (i = 0u; i < idxsz; i++)
        dst[b++] = idx[i];
    if (b != fo)
        return 0u;
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenc, litc, distc);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === H5 e05/e09 GEN Huffman emission (owner: H5; LANE-H5) ===
 * G1's lzmesh_g1_emit is level-agnostic (streams/toks/counts only);
 * the u37 hook gated L1-only. Oracle Huffman-codes GEN streams at
 * L5/L9 per the same S5.5 decisions (rescore-g: 44/62 COUNTS_EQ
 * cells streams-ident, oracle-HUF vs port-RAW; tmp/h5/). H5 hooks
 * L5/L9 to the same emit; G1's L1 block untouched. want()/gates/B1
 * unchanged (dst==NULL path untouched); emit 0 falls through to
 * the proven RAW path, so RAW decisions stay byte-identical. */
static int lzmesh_h5_genhuff_on(int level) {
    return level == 5 || level == 9;
}

/* === H3 SPLIT: GEN multi-COMP chaining (owner: H3) === */
/* Budgets B0/B1/B2 via u3 (16384/32768/62448). Max footer M0=B0+7,
 * M1=B1+6, M2=B2+6 (black-box: full spends <=M). P2 TAILSPLIT
 * (HINT-TAILSPLIT-R1): overshoot-one (Sb<=B joins) + M ceiling
 * (Sn<=M) + short-prospective (Sb+lr<=B for L<LSTAR, L1-REP
 * exempt). End rule close-type dependent: take-close rem<=8
 * (TEST2 kept) + S0<=M+4 last-take rem>=9 (bedded); lit-close
 * rem<=17 blk0 / rem<=53 blk1 take-free schedule.
 * Finder/recents carry across blocks
 * (tables never cleared, absolute pos, recents init once). First
 * block pre-emits lit0 (first byte free, litc>=1); later no pre-emit
 * (litc may be 0). Per-block TIER-1 ds>fo, no local TIER-2; global
 * TIER-2 outpos<=n (pre-ff, END excluded). Per-block RAW fallback
 * on gates/TIER-1 fail (mixed RAW/COMP). E00 untouched (MUST-NOT). */
#define LZMESH_H3_M0 16391u
#define LZMESH_H3_M1 32774u
#define LZMESH_H3_M2 62454u
#define LZMESH_H3_SPLITMIN 9u
/* P2 TAILSPLIT (HINT-TAILSPLIT-R1): mid-input take joins iff Sb<=B
 * (overshoot-one; counted-S'' frame Sb''<=B-1) AND Sn<=M (non-strict
 * ceiling) AND (Sb+lr<=B OR L>=LSTAR) for L<LSTAR short takes
 * (L1-REP exempt). LSTAR in {6..9} OPEN (L6-8 unseen at edge);
 * default 6 = memo prediction (filter-tier/L1-min-6). End rule is
 * close-type dependent: take-close rem<=8 any block (kept TEST2) +
 * S0<=M+4 for last-take rem>=9 (bedded 0/1 x e01/e05/e09);
 * lit-close rem<=17 blk0 / rem<=53 blk1 in take-free schedule
 * (blk2+ 8: OPEN). */
#define LZMESH_H3_LSTAR 6u
#define LZMESH_H3_ENDALLOW0 17u
#define LZMESH_H3_ENDALLOW1 53u

typedef struct {
    size_t off;
    size_t bs;
    size_t start;
    size_t end;
    uint32_t term_run;
} lzmesh_h3_blk;

/* F1: T2-3a recut path (L5). LZMESH_F1_RECUT=0 disables (baseline).
 * SHIPPED default 1 (s14 + s15 IDENT, s05/s15-blk0 EXACT, s09 ndiff-3,
 * s00/s06/V1-cells/EVO-neutral; full-4sel NEW 0). */
static int lzmesh_f1_on(void) {
    static int init = 0, on = 1; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_F1_RECUT");
        init = 1;
        on = (e == NULL) ? 1 : ((e[0] == '0' && e[1] == '\0') ? 0 : 1);
    }
    return on;
}

/* YF: L9 walk-phase reset at H3 cuts (LANE-Y-E09FINDER). L9 twin of F1
 * T2-3a: oracle parses per-block (fresh litrun at cut, tables/rep
 * carry); port carried litrun across cuts (e09 far-class: walk hops
 * over s05@16623/s14@16725/s15@17826/s09big@237447, never queried).
 * Bedded: single-cut takes oracle takes on 3/4 far cells; full battery
 * 16->12 NEW 0 (flips s05/s09big/s14/s15 e09).
 * LZMESH_YF_L9CUT=0 disables (default 1 = ship). */
static int lzmesh_yf_l9cut_on(void) {
    static int init = 0, on = 1; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_YF_L9CUT");
        init = 1;
        on = (e == NULL) ? 1 : ((e[0] == '0' && e[1] == '\0') ? 0 : 1);
    }
    return on;
}

/* YF: h3 source-gate (LANE-Y-E09FINDER). h3-tier take requires S
 * landed (i5v), winpos (queried+won), take-consumed [m,end], or pos0;
 * never-touched flood-S declined. Bedded: overfire-S never-touched 6/6
 * (s09 238053, s14 14122/16023/13536, s05 49801) vs HIT-S touched;
 * s06@66 S47 = winpos HIT vs S238053 never-queried MISS (I5SLOT);
 * grafts: s09big floor-len5 via h2, s14 blind to len10, S238053 declined
 * at runs 1/6/11 (S-side, Q-side dead). Blanket visited-gate falsified
 * (121 NEW); span/peek marks superseded by consumed/winpos (15 NEW).
 * LZMESH_YF_SVISG=0 disables (default 1 = ship). L9-only. */
/* R2-ENC9 A4: fuse (matches fwd decl; was outline x2 in parse). */
static inline __attribute__((always_inline)) int lzmesh_yf_svisg_on(void) {
    static int init = 0, on = 1; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_YF_SVISG");
        init = 1;
        on = (e == NULL) ? 1 : ((e[0] == '0' && e[1] == '\0') ? 0 : 1);
    }
    return on;
}

/* V-PINS: L9 mid-sea-cut path. LZMESH_VPINS_L9MID=0 disables (baseline).
 * L9 lacked F1's mid-sea arms: take-end cuts framed [0,take_end)+rem,
 * b0 comp=0 + tier2-decline dropped take-poor takes (s05/s15-e09 t0:
 * onset n=16382, staircase n*(L)=16379+L, cap L>=38; F1DBG-proven).
 * Entry L5-identical (last-take L<=3, rem>=9, multi-tail); re-parse
 * fixpoint stays L5-only (T2-3a walk-phase; unbedded for L9). */
static int lzmesh_vpins_l9mid_on(void) {
    static int init = 0, on = 1; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_VPINS_L9MID");
        init = 1;
        on = (e == NULL) ? 1 : ((e[0] == '0' && e[1] == '\0') ? 0 : 1);
    }
    return on;
}

/* Z-E01PAIR: L1 cut-reset (default on; =0 restores baseline). F1-style
 * fixpoint for L1: re-parse with walk-phase reset (fresh litrun at cuts)
 * + step truncation (land exactly). Oracle parses per-block at L1 too:
 * without reset the 1+(litrun>>8) walk strides over post-cut takes
 * (s12-n65536/s13-n65535 e01: filler=cut0, oracle takes 44/76 lits later,
 * port jumps over). Bedded: both cells byte+takes IDENT. */
static int lzmesh_ze01_l1cut_on(void) {
    static int init = 0, on = 1; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_ZE01_L1CUT");
        init = 1;
        on = (e == NULL) ? 1 : ((e[0] == '0' && e[1] == '\0') ? 0 : 1);
    }
    return on;
}

/* AA-E01BIG: genuine L1 fixpoint (default on; =0 keeps pass-1 cuts). */
static int lzmesh_aae01big_fixpt_on(void) {
    static int init = 0, on = 1; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_AAE01BIG_FIXPT");
        init = 1;
        on = (e == NULL) ? 1 : ((e[0] == '0' && e[1] == '\0') ? 0 : 1);
    }
    return on;
}

/* AA-E01BIG: V1 padsim on H3 path (default on; =0 V2small-only). */
static int lzmesh_aae01big_v1h3_on(void) {
    static int init = 0, on = 1; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_AAE01BIG_V1H3");
        init = 1;
        on = (e == NULL) ? 1 : ((e[0] == '0' && e[1] == '\0') ? 0 : 1);
    }
    return on;
}

/* Y-E01QUAD: L1 mid-sea twin gate (default on). */
static int lzmesh_yquad_l1mid_on(void) {
    static int init = 0, on = 1; /* P2-GETENV: cached (was per-call) */
    if (!init) {
        const char *e = getenv("LZMESH_YQUAD_L1MID");
        init = 1;
        on = (e == NULL) ? 1 : ((e[0] == '0' && e[1] == '\0') ? 0 : 1);
    }
    return on;
}

/* Y-E01QUAD: L1 seacut remain gate. Fire mid-sea cut iff spend-remain
 * (B-Cb) >= 43. Bedded: 45 oracle mid-sea cuts fire (remains 43..32768,
 * pred==cut exact); 3 flush with crossing (remains 17/31/34). T in
 * (34,43]; 43 = conservative top (flush all unobserved-low). NO mlen
 * gate for L1: fires at mlen 2/3/6/7/9, nonfires at 2/6 (L5 L<=3 rule
 * does not transfer; s17-blk1 mlen-9 fires). */
#define LZMESH_YQUAD_L1REMAIN 43u

static uint32_t lzmesh_h3_max_for(unsigned blk) {
    if (blk == 0u)
        return LZMESH_H3_M0;
    if (blk == 1u)
        return LZMESH_H3_M1;
    return LZMESH_H3_M2;
}

static uint32_t lzmesh_h3_esc1(uint32_t rest) {
    return (rest <= 254u) ? 1u : 5u;
}

static uint32_t lzmesh_h3_tok_esc(uint32_t run, uint32_t mlen,
                                  int is_new) {
    uint32_t e = 0u;
    if (run >= 3u)
        e += lzmesh_h3_esc1(run - 3u);
    if (mlen >= 2u) {
        uint32_t mc = mlen - 2u;
        if (is_new) {
            if (mc > 30u)
                e += lzmesh_h3_esc1(mc - 31u);
        } else {
            if (mc > 6u)
                e += lzmesh_h3_esc1(mc - 7u);
        }
    }
    return e;
}

/* Detect global terminator (rep0 len2, run>0, cpos==size before mlen).
 * Sets *n_real (takes excl term) and *term_run (trailing, 0 if none). */
static void lzmesh_h3_term(const lzmesh_u37_tok *toks, size_t ntok,
                           size_t size, size_t *n_real,
                           uint32_t *term_run) {
    size_t cpos = 1u, t;
    int has = 0;
    if (toks == NULL || ntok == 0u || n_real == NULL
        || term_run == NULL) {
        if (n_real != NULL)
            *n_real = 0u;
        if (term_run != NULL)
            *term_run = 0u;
        return;
    }
    for (t = 0u; t < ntok; t++) {
        uint32_t run = toks[t].litrun;
        cpos += (size_t)run;
        if (t + 1u == ntok && toks[t].mlen == 2u
            && toks[t].is_new == 0u && toks[t].slot == 0u
            && cpos == size) {
            has = 1;
        } else {
            cpos += (size_t)toks[t].mlen;
        }
    }
    if (has) {
        *n_real = ntok - 1u;
        *term_run = toks[ntok - 1u].litrun;
    } else {
        *n_real = ntok;
        *term_run = 0u;
    }
}

/* GEN take-free lit-close schedule (HINT-TAILSPLIT-R1 sec3/5): caps
 * 16385 once (B0+1 pre-emit), 32768 once (B1), then 62448 repeating
 * (B2); end-absorb rem<=17 blk0, rem<=53 blk1, rem<=8 blk2+ (blk2
 * TBD-unbedded, keeps TEST2 shape). u20 untouched (E00-owned). */
static int lzmesh_h3_lit_nblocks(size_t size) {
    size_t r;
    int k;
    if (size < 10u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (size <= (size_t)LZMESH_U20_B0DS + (size_t)LZMESH_H3_ENDALLOW0)
        return 1;
    r = size - (size_t)LZMESH_U20_B0DS;
    k = 1;
    for (;;) {
        size_t cap = (k == 1) ? (size_t)LZMESH_U20_B1DS
                              : (size_t)LZMESH_U20_B2DS;
        size_t allow = (k == 1) ? (size_t)LZMESH_H3_ENDALLOW1 : 8u;
        if (r <= cap)
            return k + 1;
        if (r - cap <= allow)
            return k + 1;
        r -= cap;
        k++;
    }
}

static int lzmesh_h3_lit_block(size_t size, int k, unsigned i, size_t *off,
                               size_t *bs) {
    size_t p;
    if (off == NULL || bs == NULL || k < 1 || (int)i >= k)
        return 0;
    if (k != lzmesh_h3_lit_nblocks(size))
        return 0;
    if (i == 0u)
        p = 0u;
    else if (i == 1u)
        p = (size_t)LZMESH_U20_B0DS;
    else
        p = (size_t)LZMESH_U20_B0DS + (size_t)LZMESH_U20_B1DS
            + (size_t)(i - 2u) * (size_t)LZMESH_U20_B2DS;
    *off = p;
    if (i == (unsigned)k - 1u) {
        if (size < p)
            return 0;
        *bs = size - p;
        return *bs != 0u;
    }
    if (i == 0u)
        *bs = (size_t)LZMESH_U20_B0DS;
    else if (i == 1u)
        *bs = (size_t)LZMESH_U20_B1DS;
    else
        *bs = (size_t)LZMESH_U20_B2DS;
    return 1;
}

/* Find GEN block cuts from global takes (causal, prefix-frozen).
 * Returns nblocks>=1 with *out malloced (caller frees), 0 on fail. */
static size_t lzmesh_h3_split(lzmesh_u37_tok *toks, size_t n_real,
                              uint32_t gterm, size_t size, int level,
                              lzmesh_h3_blk **out, int f1mid, unsigned *yftwin) {
    lzmesh_h3_blk *b = NULL;
    size_t cap = 16u, nb = 0u, i;
    size_t off = 0u, start = 0u, pos = 1u;
    uint32_t S;
    unsigned blk = 0u;
    unsigned yf = 0u; /* S-SPLITSKIP count */
    unsigned spcuts = 0u; /* S-SPLITSKIP: arms cuts */
    uint32_t tokb = 0u, lenb = 0u;
    uint32_t *cost = NULL, *esc = NULL;
    int l1mid = 0; /* Y-E01QUAD: L1 mid-sea twin gate. */
    int r8_mse59 = 0, r8_mse1 = 0; /* R8-L9NEW R-c (see below). */
    (void)gterm;
    (void)level; /* F1/V2: short-prospective now level-free (NEW-only). */
    if (toks == NULL || out == NULL || size < 4u)
        return 0u;
    if (n_real > size)
        return 0u;
    b = (lzmesh_h3_blk *)malloc(cap * sizeof *b);
    cost = (uint32_t *)malloc((n_real > 0u ? n_real : 1u)
        * sizeof *cost);
    esc = (uint32_t *)malloc((n_real > 0u ? n_real : 1u)
        * sizeof *esc);
    if (b == NULL || cost == NULL || esc == NULL)
        goto fail;
    for (i = 0u; i < n_real; i++) {
        int nw = toks[i].is_new ? 1 : 0;
        esc[i] = lzmesh_h3_tok_esc(toks[i].litrun, toks[i].mlen, nw);
        cost[i] = 1u + esc[i] + toks[i].litrun
            + (nw ? 5u : 0u);
    }
    /* V-PINS: mid-sea arms (F1 L5 + L9 gated twin). */
    {
        int l9mid = (level == 9 && lzmesh_vpins_l9mid_on()) ? 1 : 0;
        if (l9mid && f1mid == 0)
            f1mid = 1;
        /* YF: f1mid==2 = L9 re-parse split (seacut-L9 on; pass-1/fail-safe
         * stay 0 = stock). L5 never passes 2. */
    }
    /* Y-E01QUAD: L1 gated twin (LZMESH_YQUAD_L1MID=0 disables). L1 never
     * cut mid-sea (take-end cuts only); oracle cuts L1 mid-sea with
     * filler bridge (e01 quad blk0 16310/16316/16364/16349). Spend
     * model predicts oracle cuts 51/51 exact (427-cell mine, blk0+blk1);
     * seacut gated by remain>=43 (see LZMESH_YQUAD_L1REMAIN). */
    l1mid = (level == 1 && lzmesh_yquad_l1mid_on()) ? 1 : 0;
    /* R8-L9NEW R-c: mid-sea gate hoist (f1mid/level/l1mid are settled
     * above and loop-invariant; T9 snapshot pattern). r8_mse59/1
     * declared with the locals at function top. */
    r8_mse59 = (f1mid && (level == 5 || level == 9)) ? 1 : 0;
    r8_mse1 = (l1mid && level == 1) ? 1 : 0;
    S = 1u;
    for (i = 0u; i < n_real; i++) {
        size_t pos_new;
        size_t rem;
        uint32_t S_new;
        uint32_t esci, costi;
        int nwi;
        /* F1: continuous-spend mid-sea cuts (L5, gated). Oracle cuts
         * mid-sea when block spend hits budget (s14 blk0 [0,16385)
         * take-free; s05 blk0 cut at 1200+15177=16377 mid-sea;
         * s09-take0 sea crosses 5 budgets); port only cut at take
         * ends (s05 framed [0,1200)+giant-rem, u37 declined, H6
         * litonly fallback). Runs shrink to block-relative (probe
         * tiles per block from off). Gates (all bedded): STRICTLY
         * mid-sea (cutpos < m); SHORT takes (L<=3: L2 s14/s15 + L3
         * s05/s09); HEAD-TEST2 remaining>=9 (s13: remaining 1 ->
         * oracle take-end 9998, NOT mid-sea 9999; symmetric with
         * TEST2 tail-absorb). == stays baseline arms (memo-24 NEW
         * take-end + V2 REP-join); L>=4 stays baseline (memo
         * long-exempt L>=9 join; L4-8 unbedded). */
        int litcut = 0;
        /* R11-L1CONT2-H2: B/M hoist (blk fixed within an i-iteration
         * except the sea-cut arm below, which refreshes both; every
         * other blk++ site ends the iteration via continue/done). */
        uint32_t h2B = lzmesh_u3_budget_for(blk);
        uint32_t h2M = lzmesh_h3_max_for(blk);
        /* R11-L1CONT2-H2: || L1-leg first (pure operands, same value;
         * L1 takes one branch instead of three). */
        if (r8_mse1 || (r8_mse59 && toks[i].mlen <= 3u)) {
            int firstcut = 1;
            for (;;) {
                uint32_t Bcur = h2B;
                uint32_t Cb = S - (blk == 0u ? 1u : 0u);
                size_t cutpos = 0u;
                if (Cb >= Bcur) {
                    /* fail-safe (arms should have cut). */
                    break;
                } else {
                    size_t remain = (size_t)Bcur - (size_t)Cb;
                    if (remain >= (size_t)toks[i].litrun)
                        break; /* sea fits, or == join: no cut */
                    if (remain < (size_t)LZMESH_H3_SPLITMIN)
                        break; /* head-TEST2: take-end (arms). */
                    /* Y-E01QUAD: L1 remain gate (no mlen gate). */
                    if (l1mid && level == 1
                        && remain < (size_t)LZMESH_YQUAD_L1REMAIN)
                        break;
                    cutpos = pos + remain;
                }
                if (cutpos == 0u)
                    break;
                if (nb + 1u >= cap) {
                    size_t nc = cap * 2u;
                    lzmesh_h3_blk *nb2 = (lzmesh_h3_blk *)malloc(
                        nc * sizeof *nb2);
                    size_t k;
                    if (nb2 == NULL)
                        goto fail;
                    for (k = 0u; k < nb; k++)
                        nb2[k] = b[k];
                    free(b);
                    b = nb2;
                    cap = nc;
                }
                b[nb].off = off;
                b[nb].bs = cutpos - off;
                b[nb].start = firstcut ? start : i;
                b[nb].end = i;
                b[nb].term_run = (uint32_t)(cutpos - pos);
                nb++;
                toks[i].litrun -= (uint32_t)(cutpos - pos);
                litcut = 1;
                spcuts++;
                off = cutpos;
                pos = cutpos;
                S = 0u;
                blk++;
                /* R11-L1CONT2-H2: refresh hoisted B/M (loop-back
                 * reuses h2B; M read after the sea loop). */
                h2B = lzmesh_u3_budget_for(blk);
                h2M = lzmesh_h3_max_for(blk);
                tokb = 0u;
                lenb = 0u;
                firstcut = 0;
                start = i;
            }
        }
        nwi = toks[i].is_new ? 1 : 0;
        /* P17-PACK S-ESC: esc[i]/cost[i] reuse for uncut toks
         * (tok_esc pure, args identical iff litrun unmutated;
         * sole mutation is the arms cut above; bed cuts=0). */
        if (!litcut) {
            esci = esc[i];
            costi = cost[i];
        } else {
            esci = lzmesh_h3_tok_esc(toks[i].litrun, toks[i].mlen, nwi);
            costi = 1u + esci + toks[i].litrun + (nwi ? 5u : 0u);
        }
        pos_new = pos + (size_t)toks[i].litrun
            + (size_t)toks[i].mlen;
        if (pos_new <= pos || pos_new > size)
            goto fail;
        rem = size - pos_new;
        if (costi > 0xFFFFFFu)
            goto fail;
        S_new = S + costi;
        if (S_new < S)
            goto fail;
        tokb += 1u;
        lenb += esci;
        if (tokb >= 16384u || lenb >= 16384u) {
            if (nb + 1u >= cap) {
                size_t nc = cap * 2u;
                lzmesh_h3_blk *nb2 = (lzmesh_h3_blk *)malloc(
                    nc * sizeof *nb2);
                size_t k;
                if (nb2 == NULL)
                    goto fail;
                for (k = 0u; k < nb; k++)
                    nb2[k] = b[k];
                free(b);
                b = nb2;
                cap = nc;
            }
            b[nb].off = off;
            b[nb].bs = pos_new - off;
            b[nb].start = start;
            b[nb].end = i + 1u;
            b[nb].term_run = 0u;
            nb++;
            if (rem == 0u)
                goto done;
            if (rem < LZMESH_H3_SPLITMIN) {
                if (nb + 1u >= cap) {
                    size_t nc = cap * 2u;
                    lzmesh_h3_blk *nb2 =
                        (lzmesh_h3_blk *)malloc(nc * sizeof *nb2);
                    size_t k;
                    if (nb2 == NULL)
                        goto fail;
                    for (k = 0u; k < nb; k++)
                        nb2[k] = b[k];
                    free(b);
                    b = nb2;
                    cap = nc;
                }
                b[nb].off = pos_new;
                b[nb].bs = rem;
                b[nb].start = n_real;
                b[nb].end = n_real;
                b[nb].term_run = (uint32_t)rem;
                nb++;
                goto done;
            }
            off = pos_new;
            start = i + 1u;
            pos = pos_new;
            S = 0u;
            blk++;
            tokb = 0u;
            lenb = 0u;
            continue;
        }
        if (rem == 0u) {
            if (nb + 1u >= cap) {
                size_t nc = cap * 2u;
                lzmesh_h3_blk *nb2 = (lzmesh_h3_blk *)malloc(
                    nc * sizeof *nb2);
                size_t k;
                if (nb2 == NULL)
                    goto fail;
                for (k = 0u; k < nb; k++)
                    nb2[k] = b[k];
                free(b);
                b = nb2;
                cap = nc;
            }
            b[nb].off = off;
            b[nb].bs = pos_new - off;
            b[nb].start = start;
            b[nb].end = i + 1u;
            b[nb].term_run = 0u;
            nb++;
            goto done;
        }
        if (rem < LZMESH_H3_SPLITMIN) {
            if (i + 1u != n_real)
                goto fail;
            if (nb + 1u >= cap) {
                size_t nc = cap * 2u;
                lzmesh_h3_blk *nb2 = (lzmesh_h3_blk *)malloc(
                    nc * sizeof *nb2);
                size_t k;
                if (nb2 == NULL)
                    goto fail;
                for (k = 0u; k < nb; k++)
                    nb2[k] = b[k];
                free(b);
                b = nb2;
                cap = nc;
            }
            b[nb].off = off;
            b[nb].bs = size - off;
            b[nb].start = start;
            b[nb].end = n_real;
            b[nb].term_run = (uint32_t)rem;
            nb++;
            goto done;
        }
        if (i + 1u == n_real) {
            uint32_t M = lzmesh_h3_max_for(blk);
            uint32_t esc_t =
                lzmesh_h3_tok_esc((uint32_t)rem, 2u, 0);
            uint32_t tc = 1u + esc_t + (uint32_t)rem;
            /* P2 take-close end rule (bedded tmp/p2/p2mid*.py): rem<=8
             * absorbs above (TEST2); rem>=9 absorbs iff S0<=M+4
             * (S0=S_new+tc). edges exact: blk0 e05 3 pre x2 tails +
             * e01/e09 2 pre each, blk1 e05 4 pre, all S0=M+4 absorb /
             * M+5 split at prefix end. (ii)/tl5/rand4 consistent
             * (rem<=8 uncapped). blk2 +4 by analogy (bedded 0/1). */
            if (S_new + tc <= M + 4u) {
                if (nb + 1u >= cap) {
                    size_t nc = cap * 2u;
                    lzmesh_h3_blk *nb2 =
                        (lzmesh_h3_blk *)malloc(nc * sizeof *nb2);
                    size_t k;
                    if (nb2 == NULL)
                        goto fail;
                    for (k = 0u; k < nb; k++)
                        nb2[k] = b[k];
                    free(b);
                    b = nb2;
                    cap = nc;
                }
                b[nb].off = off;
                b[nb].bs = size - off;
                b[nb].start = start;
                b[nb].end = n_real;
                b[nb].term_run = (uint32_t)rem;
                nb++;
                goto done;
            }
            if (nb + 2u >= cap) {
                size_t nc = cap * 2u + 2u;
                lzmesh_h3_blk *nb2 = (lzmesh_h3_blk *)malloc(
                    nc * sizeof *nb2);
                size_t k;
                if (nb2 == NULL)
                    goto fail;
                for (k = 0u; k < nb; k++)
                    nb2[k] = b[k];
                free(b);
                b = nb2;
                cap = nc;
            }
            /* F1: trailing-run end rule (L5). Take-end cut + fresh
             * tail subdivision, EXCEPT (c): NO take-end cut, fill
             * current block to spend-B mid-sea (take rides: s05 blk0
             * [0,16377) incl @1197 AND blk1 [16377,49141) incl t1@16623;
             * V: (c) covers single-tails too, case-(b) removed).
             * Take-end cases: (a) take-close Cb>=B; (d) head-TEST2
             * remaining<9 (s13-analog: don't mid-sea for a stub).
             * (c) needs last take L<=3 + remaining>=9. Final partial
             * uncapped; TEST2 absorbs tail<9. */
            /* MERGE6: e05trio comment/case-(b)-removal + v-pins L9 mid-sea-cut
             * (level==9 widened condition kept). */
            if (f1mid && (level == 5 || level == 9)) {
                uint32_t Bcur = lzmesh_u3_budget_for(blk);
                uint32_t Cb = S_new - (blk == 0u ? 1u : 0u);
                size_t tpos;
                uint32_t tS;
                unsigned tblk;
                if (toks[n_real - 1u].mlen > 3u) {
                    /* LONG last take: baseline framing (no subdiv). */
                    b[nb].off = off;
                    b[nb].bs = pos_new - off;
                    b[nb].start = start;
                    b[nb].end = n_real;
                    b[nb].term_run = 0u;
                    nb++;
                    b[nb].off = pos_new;
                    b[nb].bs = rem;
                    b[nb].start = n_real;
                    b[nb].end = n_real;
                    b[nb].term_run = (uint32_t)rem;
                    nb++;
                    goto done;
                }
                /* V: case-(b) P2-single-tail REMOVED (was: rem fits one fresh
                 * block -> take-end cut). s05-n65537 e05 proof: rem=48911
                 * take-end cut at 16626 (RAW249 swallowing t1@16623) cost
                 * the 49141 restart, T0/T1 stepped over, ENC_DIFF; fill
                 * to spend-B1 (cut 49141) heals byte-exact (takes+blocks
                 * converge). P2's r=39/3000 probes take-end-cut via the
                 * LONG-last-take arm above, never case-(b) (their last
                 * takes are L>=4; verified IDENT 6/6 without case-b).
                 * Full-4sel + holdout NEW 0 without it. Absorb rule
                 * above guarantees cutpos<size in the else arm
                 * (rem<=B-Cb implies S0<=M+4). */
                if (Cb >= Bcur
                    || (size_t)Bcur - (size_t)Cb
                        < (size_t)LZMESH_H3_SPLITMIN) {
                    b[nb].off = off;
                    b[nb].bs = pos_new - off;
                    b[nb].start = start;
                    b[nb].end = n_real;
                    b[nb].term_run = 0u;
                    nb++;
                    tpos = pos_new;
                    tS = 0u;
                    tblk = blk + 1u;
                } else {
                    size_t cutpos = pos_new + ((size_t)Bcur - (size_t)Cb);
                    b[nb].off = off;
                    b[nb].bs = cutpos - off;
                    b[nb].start = start;
                    b[nb].end = n_real;
                    b[nb].term_run = (uint32_t)(cutpos - pos_new);
                    nb++;
                    tpos = cutpos;
                    tS = 0u;
                    tblk = blk + 1u;
                }
                for (;;) {
                    uint32_t tB = lzmesh_u3_budget_for(tblk);
                    uint32_t tCb2 = tS - (tblk == 0u ? 1u : 0u);
                    size_t chunk;
                    if (tCb2 >= tB)
                        break; /* fail-safe: no progress */
                    chunk = (size_t)tB - (size_t)tCb2;
                    if (tpos + chunk >= size)
                        break; /* tail fits: final partial */
                    if (size - (tpos + chunk)
                        < (size_t)LZMESH_H3_SPLITMIN)
                        break; /* TEST2: absorb tail<9 */
                    if (nb + 1u >= cap) {
                        size_t nc = cap * 2u;
                        lzmesh_h3_blk *nb2 = (lzmesh_h3_blk *)malloc(
                            nc * sizeof *nb2);
                        size_t k;
                        if (nb2 == NULL)
                            goto fail;
                        for (k = 0u; k < nb; k++)
                            nb2[k] = b[k];
                        free(b);
                        b = nb2;
                        cap = nc;
                    }
                    b[nb].off = tpos;
                    b[nb].bs = chunk;
                    b[nb].start = n_real;
                    b[nb].end = n_real;
                    b[nb].term_run = (uint32_t)chunk;
                    nb++;
                    tpos += chunk;
                    tS = 0u;
                    tblk++;
                }
                if (nb + 1u >= cap) {
                    size_t nc = cap * 2u;
                    lzmesh_h3_blk *nb2 = (lzmesh_h3_blk *)malloc(
                        nc * sizeof *nb2);
                    size_t k;
                    if (nb2 == NULL)
                        goto fail;
                    for (k = 0u; k < nb; k++)
                        nb2[k] = b[k];
                    free(b);
                    b = nb2;
                    cap = nc;
                }
                b[nb].off = tpos;
                b[nb].bs = size - tpos;
                b[nb].start = n_real;
                b[nb].end = n_real;
                b[nb].term_run = (uint32_t)(size - tpos);
                nb++;
                goto done;
            }
            b[nb].off = off;
            b[nb].bs = pos_new - off;
            b[nb].start = start;
            b[nb].end = n_real;
            b[nb].term_run = 0u;
            nb++;
            b[nb].off = pos_new;
            b[nb].bs = rem;
            b[nb].start = n_real;
            b[nb].end = n_real;
            b[nb].term_run = (uint32_t)rem;
            nb++;
            goto done;
        } {
            uint32_t B = h2B;
            uint32_t M = h2M;
            uint32_t cn = cost[i + 1u];
            uint32_t Cb_new = S_new - (blk == 0u ? 1u : 0u);
            int cut = 0;
            int seacut = 0;
            /* R11-L1CONT2-H2: level dispatch (L1 skips the three dead
             * L5/L9/YF predicates; other levels run the stock four). */
            if (level == 1) {
                /* Y-E01QUAD: L1 seacut twin (remain gate, no mlen
                 * gate). Verbatim condition. */
                if (l1mid && Cb_new < B
                    && (size_t)B - (size_t)Cb_new
                        < (size_t)toks[i + 1u].litrun
                    && (size_t)B - (size_t)Cb_new
                        >= (size_t)LZMESH_YQUAD_L1REMAIN)
                    seacut = 1;
            } else {
                /* F1: strictly-mid-sea cut ahead in take i+1's sea
                 * (L<=3 + remaining>=9, same gate as sea loop): take
                 * i+1 opens a new block, so b/c lookahead (spanning
                 * the future cut) is moot. == / remaining<9 / L>=4
                 * do NOT suppress (baseline arms exact there:
                 * memo-24, s13, V2, long-exempt). */
                if (f1mid && level == 5 && Cb_new < B
                    && toks[i + 1u].mlen <= 3u
                    && (size_t)B - (size_t)Cb_new
                        < (size_t)toks[i + 1u].litrun
                    && (size_t)B - (size_t)Cb_new
                        >= (size_t)LZMESH_H3_SPLITMIN)
                    seacut = 1;
                /* YF: L9 seacut twin (re-parse splits only, f1mid==2,
                 * gated). L9-oracle suppresses take-end cut when take
                 * i+1's sea mid-sea-cuts (s05 blk0 [0,16377) keeps
                 * take@1197 despite take@16623 cost; port cut at
                 * take-end 1200 -> oscillation -> fail-safe). Same
                 * gate as L5. */
                if (f1mid == 2 && level == 9
                    && lzmesh_yf_l9cut_on()
                    && Cb_new < B
                    && toks[i + 1u].mlen <= 3u
                    && (size_t)B - (size_t)Cb_new
                        < (size_t)toks[i + 1u].litrun
                    && (size_t)B - (size_t)Cb_new
                        >= (size_t)LZMESH_H3_SPLITMIN)
                    seacut = 1;
                /* P17-PACK S-SPLITSKIP count: YF-twin condition sans
                 * f1mid (would-fire under f1=2). Caller skips the
                 * f1=2 re-split iff 0 (sole f1=1-vs-2 difference on
                 * L9; induction needs identical inputs, i.e. cuts==0
                 * since cuts mutate toks; twin-quiet + cut-free =>
                 * states identical every iter). */
                if (f1mid == 1 && i + 1u < n_real && level == 9
                    && toks[i + 1u].mlen <= 3u && Cb_new < B
                    && lzmesh_yf_l9cut_on()
                    && (size_t)B - (size_t)Cb_new
                        < (size_t)toks[i + 1u].litrun
                    && (size_t)B - (size_t)Cb_new
                        >= (size_t)LZMESH_H3_SPLITMIN)
                    yf++;
            }
            if (Cb_new >= B) {
                cut = 1;
            } else if (!seacut && S_new + cn > M) {
                cut = 1;
            } else if (!seacut && toks[i + 1u].mlen < LZMESH_H3_LSTAR
                && toks[i + 1u].is_new
                && Cb_new + toks[i + 1u].litrun > B - 1u) {
                /* F1/V2 port of abd3c71e: short-prospective is NEW-only.
                 * REP takes join regardless of litrun fit (s06-n262144
                 * e05: L5-REP lr1 joins at counted prospective B where
                 * port cut; memo 24/24 short-prospective cuts are NEW,
                 * 0 REP, in 800+ rows; subsumes L1-REP exemption). */
                cut = 1;
            }
            if (cut) {
                if (nb + 1u >= cap) {
                    size_t nc = cap * 2u;
                    lzmesh_h3_blk *nb2 =
                        (lzmesh_h3_blk *)malloc(nc * sizeof *nb2);
                    size_t k;
                    if (nb2 == NULL)
                        goto fail;
                    for (k = 0u; k < nb; k++)
                        nb2[k] = b[k];
                    free(b);
                    b = nb2;
                    cap = nc;
                }
                b[nb].off = off;
                b[nb].bs = pos_new - off;
                b[nb].start = start;
                b[nb].end = i + 1u;
                b[nb].term_run = 0u;
                nb++;
                off = pos_new;
                start = i + 1u;
                pos = pos_new;
                S = 0u;
                blk++;
                tokb = 0u;
                lenb = 0u;
                continue;
            }
        }
        S = S_new;
        pos = pos_new;
    }
    if (n_real == 0u) {
        int k, qi;
        if (size < 10u) {
            if (nb + 1u >= cap)
                goto fail;
            b[nb].off = 0u;
            b[nb].bs = size;
            b[nb].start = 0u;
            b[nb].end = 0u;
            b[nb].term_run = (uint32_t)(size - 1u);
            nb++;
            goto done;
        }
        k = lzmesh_h3_lit_nblocks(size);
        if (k < 1)
            goto fail;
        while ((size_t)k + nb >= cap) {
            size_t nc = cap * 2u + 2u;
            lzmesh_h3_blk *nb2 = (lzmesh_h3_blk *)malloc(
                nc * sizeof *nb2);
            size_t kk;
            if (nb2 == NULL)
                goto fail;
            for (kk = 0u; kk < nb; kk++)
                nb2[kk] = b[kk];
            free(b);
            b = nb2;
            cap = nc;
        }
        for (qi = 0; qi < k; qi++) {
            size_t off2, bs2;
            if (!lzmesh_h3_lit_block(size, k, (unsigned)qi, &off2,
                                     &bs2))
                goto fail;
            if (bs2 == 0u)
                goto fail;
            b[nb].off = off2;
            b[nb].bs = bs2;
            b[nb].start = 0u;
            b[nb].end = 0u;
            if (qi == 0)
                b[nb].term_run = (uint32_t)(bs2 - 1u);
            else
                b[nb].term_run = (uint32_t)bs2;
            nb++;
        }
        goto done;
    }
    goto fail;
done:
    if (yftwin != NULL)
        *yftwin = yf | spcuts;
    free(cost);
    free(esc);
    if (nb == 0u) {
        free(b);
        return 0u;
    }
    *out = b;
    return nb;
fail:
    if (b != NULL)
        free(b);
    if (cost != NULL)
        free(cost);
    if (esc != NULL)
        free(esc);
    return 0u;
}

/* Per-block GEN COMP probe (RAW-only, no HUF/F2). Builds streams
 * from token slice + term_run into lit/tok/len/dsym. Returns 1 for
 * COMP with outs set (fo/bo/modes/counts/lanes/idx), 0 for RAW. */
static int lzmesh_h3_probe(const uint8_t *src,
                           const lzmesh_u37_tok *toks, size_t start,
                           size_t end, uint32_t term_run, size_t off,
                           size_t bs, int is_first, uint8_t *lit,
                           uint8_t *tok, uint8_t *len, uint8_t *dsym,
                           size_t bufcap, uint32_t *fo_out,
                           uint32_t *bo_out, uint32_t *modes_out,
                           uint32_t *tokc_out, uint32_t *lenc_out,
                           uint32_t *litc_out, uint32_t *distc_out,
                           unsigned laneb_out[8], uint8_t idx_out[24],
                           unsigned *idxsz_out, int *streams_ok,
                           unsigned *bitc_out) {
    size_t li = 0u, ti = 0u, eni = 0u, di = 0u, t;
    size_t cpos;
    uint32_t litc, tokc, lenc, distc, ds;
    uint32_t dslot; /* R15-TL1-T2 S3-FUSE: fused NEW-take counter. */
    uint32_t m_lit, m_tok, m_len, m_dist, modes;
    uint32_t litB, tokB, lenB, distB, bo, fo;
    unsigned bitc[8], laneb[8], k;
    uint8_t idx[24];
    unsigned idxsz = 0u;
    uint32_t payload = 0u;
    int lit_eq = 1, tok_eq = 1, len_eq = 1, dist_eq = 1;
    if (streams_ok != NULL)
        *streams_ok = 0;
    if (src == NULL || toks == NULL || lit == NULL || tok == NULL
        || len == NULL || dsym == NULL || fo_out == NULL
        || bo_out == NULL || modes_out == NULL || tokc_out == NULL
        || lenc_out == NULL || litc_out == NULL
        || distc_out == NULL || laneb_out == NULL
        || idx_out == NULL || idxsz_out == NULL)
        return 0;
    if (bs == 0u || bs > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (start > end)
        return 0;
    if (bufcap < bs + 8u)
        return 0;
    cpos = is_first ? off + 1u : off;
    if (is_first) {
        if (off + 1u > off + bs)
            return 0;
        lit[li++] = src[off];
    }
    /* R15-TL1-T2 S3-FUSE (ANSWER-r15-tl1-1 sec-c): T2 fused into the
     * T-loop NEW leg below. bitc zeroed here (was: before deleted T2);
     * dslot counts NEW takes in T-loop order. bitc untouched between
     * T-loop and old T2 (counts/gates/eq-scans/modes/bo only); every
     * exit in between returns 0 with bitc stack-local => exact. dslot
     * (not slot: REP leg shadows `slot`). */
    for (k = 0u; k < 8u; k++)
        bitc[k] = 0u;
    dslot = 0u;
    for (t = start; t < end; t++) {
        uint32_t run = toks[t].litrun;
        uint32_t mlen = toks[t].mlen;
        uint32_t dist = toks[t].dist;
        int nw = toks[t].is_new ? 1 : 0;
        unsigned lit_f;
        uint32_t mc, ms, u;
        for (u = 0u; u < run; u++) {
            if (cpos >= off + bs || li >= bufcap)
                return 0;
            lit[li++] = src[cpos++];
        }
        lit_f = (run <= 2u) ? (unsigned)run : 3u;
        if (nw) {
            unsigned sb, low;
            uint32_t suf;
            if (mlen < 2u)
                return 0;
            mc = mlen - 2u;
            ms = mc > 30u ? 31u : mc;
            if (ti >= bufcap)
                return 0;
            tok[ti++] = lzmesh_u7_token_new(lit_f, ms);
            lzmesh_u4_dist_split_nc(dist, &sb, &low, &suf);
            /* R15-TL1-T2 S3-FUSE: accumulate (sb == sb_of(dist) by
             * split_nc; same NEW order => identical lanes). Guards
             * below return 0 => partial state unobservable. */
            bitc[dslot % 8u] += sb;
            dslot++;
            if (sb > 28u || dist == 0u)
                return 0;
            if (sb < 31u && suf >= (1u << sb))
                return 0;
            if (di >= bufcap)
                return 0;
            dsym[di++] = (uint8_t)((sb << 3) | (low & 7u));
            if (lit_f == 3u) {
                uint8_t eb[5];
                unsigned n = lzmesh_u7_len_escape_write(run - 3u,
                                                        eb);
                if (n == 0u || eni + n > bufcap)
                    return 0;
                for (u = 0u; u < n; u++)
                    len[eni++] = eb[u];
            }
            if (ms == 31u) {
                uint8_t eb[5];
                unsigned n = lzmesh_u7_len_escape_write(mc - 31u,
                                                        eb);
                if (n == 0u || eni + n > bufcap)
                    return 0;
                for (u = 0u; u < n; u++)
                    len[eni++] = eb[u];
            }
            if (cpos + (size_t)mlen > off + bs)
                return 0;
            cpos += (size_t)mlen;
        } else {
            unsigned slot = toks[t].slot;
            if (slot > 2u || mlen < 2u)
                return 0;
            mc = mlen - 2u;
            ms = mc > 6u ? 7u : mc;
            if (ti >= bufcap)
                return 0;
            tok[ti++] = lzmesh_u7_token_rep(lit_f, slot, ms);
            if (lit_f == 3u) {
                uint8_t eb[5];
                unsigned n = lzmesh_u7_len_escape_write(run - 3u,
                                                        eb);
                if (n == 0u || eni + n > bufcap)
                    return 0;
                for (u = 0u; u < n; u++)
                    len[eni++] = eb[u];
            }
            if (ms == 7u) {
                uint8_t eb[5];
                unsigned n = lzmesh_u7_len_escape_write(mc - 7u,
                                                        eb);
                if (n == 0u || eni + n > bufcap)
                    return 0;
                for (u = 0u; u < n; u++)
                    len[eni++] = eb[u];
            }
            if (cpos + (size_t)mlen > off + bs)
                return 0;
            cpos += (size_t)mlen;
        }
    }
    if (term_run > 0u) {
        unsigned lit_f = (term_run <= 2u) ? (unsigned)term_run : 3u;
        uint32_t u;
        for (u = 0u; u < term_run; u++) {
            if (cpos >= off + bs || li >= bufcap)
                return 0;
            lit[li++] = src[cpos++];
        }
        if (cpos != off + bs)
            return 0;
        if (ti >= bufcap)
            return 0;
        tok[ti++] = lzmesh_u7_token_rep(lit_f, 0u, 0u);
        if (lit_f == 3u) {
            uint8_t eb[5];
            unsigned n = lzmesh_u7_len_escape_write(term_run - 3u,
                                                    eb);
            if (n == 0u || eni + n > bufcap)
                return 0;
            for (u = 0u; u < n; u++)
                len[eni++] = eb[u];
        }
    } else {
        if (cpos != off + bs)
            return 0;
    }
    if (li > bs + 1u || ti > bs + 1u || eni > bs + 8u
        || di > ti)
        return 0;
    litc = (uint32_t)li;
    tokc = (uint32_t)ti;
    lenc = (uint32_t)eni;
    distc = (uint32_t)di;
    ds = (uint32_t)bs;
    if (tokc == 0u || tokc > ds || lenc > ds || litc > ds)
        return 0;
    if (distc > tokc)
        return 0;
    if (tokc > 0xFFFFu || lenc > 0xFFFFu || litc > 0xFFFFu
        || distc > 0xFFFFu)
        return 0;
    {
        uint32_t tot = 2576u;
        int32_t stot;
        if (tokc != 0u)
            tot += lzmesh_u37_round32(tokc);
        if (lenc != 0u)
            tot += lzmesh_u37_round32(lenc);
        if (litc != 0u)
            tot += lzmesh_u37_round32(litc);
        if (distc != 0u) {
            tot += lzmesh_u37_round32(distc);
            tot += lzmesh_u37_round32(distc * 4u);
        }
        stot = (int32_t)tot;
        if (stot > (int32_t)65536)
            return 0;
    }
    for (t = 1u; t < li; t++) {
        if (lit[t] != lit[0]) {
            lit_eq = 0;
            break;
        }
    }
    for (t = 1u; t < ti; t++) {
        if (tok[t] != tok[0]) {
            tok_eq = 0;
            break;
        }
    }
    for (t = 1u; t < eni; t++) {
        if (len[t] != len[0]) {
            len_eq = 0;
            break;
        }
    }
    for (t = 1u; t < di; t++) {
        if (dsym[t] != dsym[0]) {
            dist_eq = 0;
            break;
        }
    }
    m_lit = lzmesh_u4_mode_trivial(litc, lit_eq);
    m_tok = lzmesh_u4_mode_trivial(tokc, tok_eq);
    m_len = lzmesh_u4_mode_trivial(lenc, len_eq);
    m_dist = lzmesh_u4_mode_trivial(distc, dist_eq);
    if (m_lit == LZMESH_U4_MODE_HUFFMAN)
        m_lit = LZMESH_U4_MODE_RAW;
    if (m_tok == LZMESH_U4_MODE_HUFFMAN)
        m_tok = LZMESH_U4_MODE_RAW;
    if (m_len == LZMESH_U4_MODE_HUFFMAN)
        m_len = LZMESH_U4_MODE_RAW;
    if (m_dist == LZMESH_U4_MODE_HUFFMAN)
        m_dist = LZMESH_U4_MODE_RAW;
    if (m_lit == LZMESH_U4_MODE_REPEAT && litc > LZMESH_U37_LITREPMAX)
        m_lit = LZMESH_U4_MODE_RAW;
    modes = lzmesh_u4_modes_pack(m_tok, m_len, m_lit, m_dist);
    litB = (litc == 0u) ? 0u
        : ((m_lit == LZMESH_U4_MODE_REPEAT) ? 1u : litc);
    tokB = (tokc == 0u) ? 0u
        : ((m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : tokc);
    lenB = (lenc == 0u) ? 0u
        : ((m_len == LZMESH_U4_MODE_REPEAT) ? 1u : lenc);
    distB = (distc == 0u) ? 0u
        : ((m_dist == LZMESH_U4_MODE_REPEAT) ? 1u : distc);
    {
        uint64_t b64 = (uint64_t)9 + (uint64_t)litB
            + (uint64_t)tokB + (uint64_t)lenB + (uint64_t)distB;
        if (b64 > (uint64_t)0xFFFFu)
            return 0;
        bo = (uint32_t)b64;
    }
    /* R15-TL1-T2 S3-FUSE: bitc already fused (no re-zero); laneb init. */
    for (k = 0u; k < 8u; k++)
        laneb[k] = 0u;
    /* R15-TL1-T2 S3-FUSE: T2 loop deleted (fused into T-loop NEW leg);
     * distc cross-check on fused dslot. */
    if (dslot != distc)
        return 0;
    for (k = 0u; k < 8u; k++) {
        laneb[k] = (bitc[k] + 7u) >> 3;
        payload += laneb[k];
    }
    if (payload == 0u) {
        fo = bo;
    } else {
        unsigned lan8[8];
        for (k = 0u; k < 8u; k++)
            lan8[k] = laneb[k];
        idxsz = lzmesh_pack1_index_pack(lan8, idx,
                                        (unsigned)sizeof idx);
        if (idxsz == 0u)
            return 0;
        if ((uint64_t)bo + (uint64_t)payload + (uint64_t)idxsz
            > (uint64_t)0xFFFFu)
            return 0;
        fo = bo + payload + idxsz;
    }
    /* F1: publish outs before RAW gates (streams complete). Gate-fail
     * returns 0 with valid outs + streams_ok (HUF-rescue input). */
    *fo_out = fo;
    *bo_out = bo;
    *modes_out = modes;
    *tokc_out = tokc;
    *lenc_out = lenc;
    *litc_out = litc;
    *distc_out = distc;
    for (k = 0u; k < 8u; k++)
        laneb_out[k] = laneb[k];
    for (k = 0u; k < idxsz; k++)
        idx_out[k] = idx[k];
    *idxsz_out = idxsz;
    /* R15-TL1-T2 S4-THREAD: publish fused bitc (streams-complete point;
     * sok-gated by construction: only reached when outs publish). */
    if (bitc_out != NULL)
        for (k = 0u; k < 8u; k++)
            bitc_out[k] = bitc[k];
    if (streams_ok != NULL)
        *streams_ok = 1;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    if (!lzmesh_u3_tier1_comp(ds, fo))
        return 0;
    if (!lzmesh_u7_first_block_litc_ok(litc, is_first))
        return 0;
    if (is_first) {
        uint32_t br = (bo >= 9u) ? bo - 9u : 0u;
        if (lzmesh_u37_b1_refuse(ds, br, tokc, distc,
                                 payload == 0u, modes, litc,
                                 lit, tok, len))
            return 0;
    }
    return 1;
}

/* Q2 per-block HUF (G1/H5 tables, H3 framing). Streams + toks slice
 * for one COMP block; emits header+streams+lanes+idx+footer (no END).
 * Returns fo+10 on success, 0 for RAW fallback (no-HUF, rollback,
 * gates fail). dst==NULL size-only. Caller gates level. */
/* P16-PACK T1: huf measure/emit split (static-free table amortization).
 * loop2 calls h3_huf_block as a back-to-back measure+emit pair with
 * identical inputs (bed: every table built 2x, 32/32 pairwise-identical
 * on tx-e09). The split shares one measure via a caller-owned ctx
 * (stack, thread-safe, no statics). The wrapper keeps fused behavior
 * for all other callers. */
typedef struct {
    lzmesh_g1_huff h[4];
    unsigned mode[4];
    unsigned bitc[8];
    unsigned laneb[8];
    unsigned sufbits[8];
    uint8_t idx[24];
    unsigned idxsz;
    uint32_t bo;
    uint32_t fo;
    uint32_t payload;
    uint32_t modes;
    uint8_t *sbb; /* P17-PACK S-SBSTASH: per-take sb stash (malloc/free) */
} p16_huf_ctx;

static size_t p16_huf_measure(const uint8_t *lit, size_t li,
                              const uint8_t *tok, size_t ti,
                              const uint8_t *len, size_t eni,
                              const uint8_t *dsym, size_t di,
                              const lzmesh_u37_tok *toks, size_t start,
                              size_t end, uint32_t litc, uint32_t tokc,
                              uint32_t lenc, uint32_t distc, uint32_t ds,
                              int is_first, const unsigned *memo_bitc,
                              p16_huf_ctx *ctx) {
    const uint8_t *str[4];
    size_t strn[4];
    lzmesh_g1_huff h[4];
    unsigned mode[4];
    unsigned s, i, k;
    unsigned bitc[8], laneb[8], sufbits[8];
    uint8_t idx[24];
    unsigned idxsz;
    uint32_t bo, fo, payload = 0u, modes;
    size_t t;
    if (ctx != NULL)
        ctx->sbb = NULL;
    if (lit == NULL || tok == NULL || len == NULL || dsym == NULL
        || toks == NULL)
        return 0u;
    if (li != (size_t)litc || ti != (size_t)tokc || eni != (size_t)lenc
        || di != (size_t)distc)
        return 0u;
    if (start > end)
        return 0u;
    str[0] = lit;
    str[1] = tok;
    str[2] = len;
    str[3] = dsym;
    strn[0] = li;
    strn[1] = ti;
    strn[2] = eni;
    strn[3] = di;
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        size_t n = strn[s];
        int alleq = 1;
        mode[s] = 0u;
        if (n == 0u)
            continue;
        for (i = 1u; i < n; i++) {
            if (sp[i] != sp[0]) {
                alleq = 0;
                break;
            }
        }
        if (alleq) {
            mode[s] = 1u;
            continue;
        }
        if (n <= 10u)
            continue;
        if (!lzmesh_g1_build(sp, n, &h[s]))
            continue;
        if (65u + h[s].pack_bits + h[s].data_bits
            >= (uint64_t)8u * n)
            continue;
        mode[s] = 2u;
    }
    if (mode[0] == 1u && litc > LZMESH_U37_LITREPMAX)
        mode[0] = 0u;
    if (mode[0] != 2u && mode[1] != 2u && mode[2] != 2u
        && mode[3] != 2u)
        return 0u;
    {
        uint64_t b64 = 9u;
        for (s = 0u; s < 4u; s++) {
            if (mode[s] == 2u)
                continue;
            else if (mode[s] == 1u)
                b64 += 1u;
            else
                b64 += (uint64_t)strn[s];
        }
        if (b64 > (uint64_t)0xFFFFu)
            return 0u;
        bo = (uint32_t)b64;
    }
    for (k = 0u; k < 8u; k++) {
        bitc[k] = 0u;
        sufbits[k] = 0u;
    }
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        if (mode[s] != 2u)
            continue;
        bitc[0] += 33u + 32u;
        for (i = 0u; i < h[s].used; i++)
            bitc[i & 7u] += h[s].mlens[h[s].vals[i]];
        for (i = 0u; i < strn[s]; i++)
            bitc[i & 7u] += h[s].lens[sp[i]];
    }
    if (memo_bitc != NULL) {
        /* R15-TL1-T2 S4-THREAD (ANSWER-r15-tl1-1-ADD1): consume the
         * probe-published take bitc (same takes/subrange; takes
         * immutable loop1->loop2, no new assumption: emit already
         * mixes memo lanes + takes). bitc ADDS (stream bits already
         * in); sufbits == take bitc (identical += sb from 0);
         * sbb[j] = dsym[j]>>3 (dsym=(sb<<3)|low, slot order == di
         * order); slot!=distc vacuous (same takes probe counted).
         * Malloc-fail shape == stock (NULL sbb, continue, emit
         * falls back to split_nc). */
        uint32_t j;
        if (ctx != NULL)
            ctx->sbb = (distc == 0u) ? NULL : (uint8_t *)malloc(distc);
        for (k = 0u; k < 8u; k++) {
            bitc[k] += memo_bitc[k];
            sufbits[k] = memo_bitc[k];
        }
        /* dsym non-NULL: h3_multi entry-guards it; memo path only
         * runs there. di == distc validated at S4 entry. */
        if (ctx != NULL && ctx->sbb != NULL) {
            for (j = 0u; j < distc; j++)
                ctx->sbb[j] = (uint8_t)(dsym[j] >> 3);
        }
    } else {
        uint32_t slot = 0u;
        /* P17-PACK S-SBSTASH: stash sb per NEW take for emit. */
        if (ctx != NULL)
            ctx->sbb = (distc == 0u) ? NULL : (uint8_t *)malloc(distc);
        for (t = start; t < end; t++) {
            unsigned sb;
            if (!toks[t].is_new)
                continue;
            sb = lzmesh_u3_sb_of(toks[t].dist);
            if (ctx != NULL && ctx->sbb != NULL && slot < distc)
                ctx->sbb[slot] = (uint8_t)sb;
            bitc[slot % 8u] += sb;
            sufbits[slot % 8u] += sb;
            slot++;
        }
        if (slot != distc)
            return 0u;
    }
    for (k = 0u; k < 8u; k++) {
        laneb[k] = (bitc[k] + 7u) >> 3;
        payload += laneb[k];
    }
    if (payload == 0u)
        return 0u;
    idxsz = lzmesh_pack1_index_pack(laneb, idx, (unsigned)sizeof idx);
    if (idxsz == 0u)
        return 0u;
    if ((uint64_t)bo + (uint64_t)payload + (uint64_t)idxsz
        > (uint64_t)0xFFFFu)
        return 0u;
    fo = bo + payload + idxsz;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0u;
    modes = lzmesh_u4_modes_pack(mode[1], mode[2], mode[0], mode[3]);
    if (!lzmesh_u3_tier1_comp(ds, fo))
        return 0u;
    if (!lzmesh_u7_first_block_litc_ok(litc, is_first))
        return 0u;
    if (is_first) {
        uint32_t br = (bo >= 9u) ? bo - 9u : 0u;
        if (lzmesh_u37_b1_refuse(ds, br, tokc, distc,
                                 payload == 0u, modes, litc,
                                 lit, tok, len))
            return 0u;
    }
    if (ctx != NULL) {
        memcpy(ctx->h, h, sizeof ctx->h);
        memcpy(ctx->mode, mode, sizeof ctx->mode);
        memcpy(ctx->bitc, bitc, sizeof ctx->bitc);
        memcpy(ctx->laneb, laneb, sizeof ctx->laneb);
        memcpy(ctx->sufbits, sufbits, sizeof ctx->sufbits);
        memcpy(ctx->idx, idx, (size_t)idxsz);
        ctx->idxsz = idxsz;
        ctx->bo = bo;
        ctx->fo = fo;
        ctx->payload = payload;
        ctx->modes = modes;
    }
    return (size_t)fo + 10u;
}

static size_t p16_huf_emit(uint8_t *dst, size_t dst_capacity,
                           const uint8_t *lit, size_t li,
                           const uint8_t *tok, size_t ti,
                           const uint8_t *len, size_t eni,
                           const uint8_t *dsym, size_t di,
                           const lzmesh_u37_tok *toks, size_t start,
                           size_t end, uint32_t litc, uint32_t tokc,
                           uint32_t lenc, uint32_t distc, uint32_t ds,
                           const p16_huf_ctx *ctx) {
    const uint8_t *str[4];
    size_t strn[4];
    lzmesh_g1_huff h[4];
    unsigned mode[4];
    unsigned s, i, k;
    unsigned bitc[8], laneb[8], startb[8], pos[8], sufbits[8];
    lzmesh_u35_acc uacc[8]; /* P6-W4 */
    uint8_t lastL[8];
    uint8_t idx[24];
    unsigned idxsz;
    uint32_t bo, fo, payload, modes;
    size_t b, t;
    if (dst == NULL || ctx == NULL)
        return 0u;
    memcpy(h, ctx->h, sizeof h);
    memcpy(mode, ctx->mode, sizeof mode);
    memcpy(bitc, ctx->bitc, sizeof bitc);
    memcpy(laneb, ctx->laneb, sizeof laneb);
    memcpy(sufbits, ctx->sufbits, sizeof sufbits);
    memcpy(idx, ctx->idx, (size_t)ctx->idxsz);
    idxsz = ctx->idxsz;
    bo = ctx->bo;
    fo = ctx->fo;
    payload = ctx->payload;
    modes = ctx->modes;
    if (dst_capacity < (size_t)fo + 10u)
        return 0u;
    str[0] = lit;
    str[1] = tok;
    str[2] = len;
    str[3] = dsym;
    strn[0] = li;
    strn[1] = ti;
    strn[2] = eni;
    strn[3] = di;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    b = 9u;
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        if (mode[s] == 2u)
            continue;
        else if (mode[s] == 1u)
            dst[b++] = sp[0];
        else {
            /* P15-PACK P2b (was byte loop). */
            memmove(dst + b, sp, strn[s]);
            b += strn[s];
        }
    }
    if (b != bo)
        return 0u;
    for (k = 0u; k < 8u; k++) {
        startb[k] = (unsigned)(b - bo);
        b += laneb[k];
        pos[k] = 0u;
        lastL[k] = 0u; /* P15-PACK P1: init precedes fused emit stores. */
        uacc[k].acc = 0u; /* P6-W4 */
        uacc[k].nbits = 0u;
        uacc[k].out = dst + bo + startb[k];
    }
    memset(dst + bo, 0, payload); /* P15-PACK P2a (was byte loop). */
    for (s = 0u; s < 4u; s++) {
        const uint8_t *sp = str[s];
        if (mode[s] != 2u)
            continue;
        for (i = 0u; i < 11u; i++)
            lzmesh_u35_acc_put_fast(&uacc[0], &pos[0],
                                    h[s].mlens[i], 3u);
        lzmesh_u35_acc_put_fast(&uacc[0], &pos[0], h[s].bm, 32u);
        for (i = 0u; i < h[s].used; i++)
            lzmesh_u35_acc_put_fast(&uacc[i & 7u], &pos[i & 7u],
                                    h[s].mcodes[h[s].vals[i]],
                                    h[s].mlens[h[s].vals[i]]);
        for (i = 0u; i < strn[s]; i++) {
            /* R9-TEXT1-E1: hottest put site (~77k calls/rep text-L1).
             * R11-L1CONT2-H3: huff specialization (n in 1..10: table
             * built from this stream, q-floor+rank cap 10). */
            lzmesh_u35_acc_put_huff(&uacc[i & 7u], &pos[i & 7u],
                                    h[s].codes[sp[i]],
                                    h[s].lens[sp[i]]);
            /* P16-PACK T2b: lastL-direct (store deleted; computed below). */
        }
    }
    /* P16-PACK T2b: lastL-direct (last write wins; 8 computes vs 70k stores;
     * bed2 strn=69889 tx-e09; FULL-gated). */
    for (k = 0u; k < 8u; k++) {
        unsigned ll = 0u;
        for (s = 4u; s > 0u; ) {
            size_t last;
            --s;
            if (mode[s] != 2u)
                continue;
            if (strn[s] > k) {
                last = strn[s] - 1u - ((strn[s] - 1u - k) & 7u);
                ll = h[s].lens[str[s][last]];
                break;
            }
        }
        lastL[k] = (uint8_t)ll;
    }
    for (k = 0u; k < 8u; k++)
        lzmesh_u35_acc_flush(&uacc[k]); /* P6-W4: drain pre-suffix loop */
    {
        uint32_t slot = 0u;
        for (t = start; t < end; t++) {
            unsigned sb, low;
            uint32_t suf, kk;
            if (!toks[t].is_new)
                continue;
            /* P17-PACK S-SBSTASH: sb stashed by measure; derive
             * (low,suf) without sb_of (split_nc body sans clz).
             * Guards fall back to split_nc (fail-safe). */
            if (ctx != NULL && ctx->sbb != NULL && slot < distc) {
                uint32_t d = toks[t].dist;
                uint32_t base;
                uint32_t tt;
                sb = ctx->sbb[slot];
                base = (sb >= 29u) ? 0u : (8u << sb);
                tt = d + 7u - base;
                low = (unsigned)(tt & 7u);
                suf = tt >> 3;
            } else {
                lzmesh_u4_dist_split_nc(toks[t].dist, &sb, &low, &suf);
            }
            kk = slot % 8u;
            /* P2-bitio: identical bit-OR via u35_put (was inline loop). */
            /* R9-TEXT1-E2: sole hot site (34.5k calls/rep text-L1). */
            lzmesh_u35_put_fast(dst + bo + startb[kk], &pos[kk], suf,
                                sb);
            slot++;
        }
    }
    for (k = 0u; k < 8u; k++) {
        if (pos[k] != bitc[k])
            return 0u;
    }
    /* P15-PACK P1: lastL walk deleted (fused into vals-emit loop). */
    /* V2small padsim override (wave-U u-e05trio; unverified lanes
     * keep PAD1/B-TAB). U5 NOT wired here (unbedded on H3 path). */
    /* AA-E01BIG: V1 multi-HUF after V2small (default on,
     * LZMESH_AAE01BIG_V1H3=0 restores V2small-only). Incumbent-first
     * (V1 runs only where V2small abstains); U5 stays unwired here. */
    {
        unsigned v2_pad[8];
        unsigned sim_pad[8];
        int v2_ok = lzmesh_s3_padsim_v2small(
            str, strn, mode, h, distc, sufbits,
            dst + bo, startb, laneb, bitc, v2_pad);
        int sim_ok = v2_ok;
        for (k = 0u; k < 8u; k++)
            sim_pad[k] = v2_pad[k];
        if (sim_ok == 0 && lzmesh_aae01big_v1h3_on())
            sim_ok = lzmesh_s3_padsim_v1(
                str, strn, mode, h, distc, sufbits,
                dst + bo, startb, laneb, bitc, sim_pad);
        for (k = 0u; k < 8u; k++) {
            unsigned m = bitc[k] & 7u;
            unsigned kk = (8u - m) & 7u;
            unsigned fld = 0u;
            if (m == 0u)
                continue;
            if (laneb[k] == 0u)
                return 0u;
            if ((sim_ok & (1 << k)) != 0) {
                fld = sim_pad[k];
            } else {
                if (lzmesh_u4_pad0_one(distc, k))
                    fld |= 1u;
                if (sufbits[k] == 0u)
                    fld |= lzmesh_pack1_btab(lastL[k], kk);
                /* X-E01RAZOR: B-TAB row lastL=1 (kk 6,7) = bit0.
                 * s03-e01 H3 lanes 3,4 (lastL=1, suf=0, non-suffix)
                 * TRUE pad 0x01 vs table 0x00; kk<=5 stays 0 (s01-n61
                 * lanes 5,6,7 TRUE 00). Narrower than the table: the
                 * u35 decline gate reads btab(1,*) as 0 (load-bearing
                 * on e00 tiny lanes), so this lives in the fallback. */
                if (sufbits[k] == 0u && lastL[k] == 1u
                    && !lzmesh_u4_pad0_one(distc, k)
                    && (kk == 6u || kk == 7u))
                    fld |= 1u;
                /* AA-SPARSE: B-TAB row lastL=2 (kk 7) = bit0, lane0
                 * only (H3 twin of the G1/H5 rule above; same gate +
                 * rationale, incl s09-n16384 lane5 NEW veto). */
                if (k == 0u && sufbits[k] == 0u && lastL[k] == 2u
                    && !lzmesh_u4_pad0_one(distc, k)
                    && kk == 7u)
                    fld |= 1u;
            }
            fld &= (kk >= 8u) ? 0xFFu : ((1u << kk) - 1u);
            dst[bo + startb[k] + laneb[k] - 1u] |=
                (uint8_t)((fld << m) & 0xFFu);
        }
    }
    for (i = 0u; i < idxsz; i++)
        dst[b++] = idx[i];
    if (b != fo)
        return 0u;
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenc, litc, distc);
    return (size_t)fo + 10u;
}

static size_t lzmesh_h3_huf_block(const uint8_t *lit, size_t li,
                                  const uint8_t *tok, size_t ti,
                                  const uint8_t *len, size_t eni,
                                  const uint8_t *dsym, size_t di,
                                  const lzmesh_u37_tok *toks, size_t start,
                                  size_t end, uint32_t litc, uint32_t tokc,
                                  uint32_t lenc, uint32_t distc, uint32_t ds,
                                  int is_first, uint8_t *dst,
                                  size_t dst_capacity) {
    p16_huf_ctx ctx;
    size_t hsz = p16_huf_measure(lit, li, tok, ti, len, eni, dsym, di,
                                 toks, start, end, litc, tokc, lenc,
                                 distc, ds, is_first, NULL, &ctx);
    if (hsz == 0u) {
        free(ctx.sbb);
        return 0u;
    }
    if (dst == NULL) {
        free(ctx.sbb);
        return hsz;
    }
    { size_t hw = p16_huf_emit(dst, dst_capacity, lit, li, tok, ti,
                               len, eni, dsym, di, toks, start, end,
                               litc, tokc, lenc, distc, ds, &ctx);
      free(ctx.sbb);
      return hw; }
}

/* GEN multi validate + emit (Q2 per-block HUF + RAW fallback,
 * global TIER-2 on RAW keep). Returns need
 * (outpos+1) on success (keep), 0 on decline/fail.
 * I1: REP/recents gate. Decoder recents carry across blocks but RAW
 * blocks contribute no takes, so a REP after a RAW block resolves
 * against stale recents (s12-n65536 e01: blk0 RAW discards dist 3283,
 * blk1 REP0 decodes to 1, 4B corrupt). Simulate decoder-visible
 * recents (init 1/1/1/1, COMP takes only) and decline (0) on any REP
 * dist/slot mismatch (*rep_bad=1; caller falls through to
 * single-block). Other declines leave *rep_bad=0 (caller preserves
 * pre-I1 behavior: decline u37_build). rep_bad may be NULL. */
/* F1: HUF-rescue measure (L5, gated). Probe-RAW blocks with complete
 * streams (take-poor: RAW fo fails TIER-1) can still be HUF-COMP
 * (oracle lit-HUF: s14 blk1 fo 14863 < ds 16383). Returns hsz (fo+10)
 * iff HUF viable AND strictly beats RAW, else 0. huf_block's own
 * gates (TIER-1/b1/first/comp) apply inside. */
static size_t lzmesh_f1_rescue(const uint8_t *lit, uint32_t litc,
                               const uint8_t *tok, uint32_t tokc,
                               const uint8_t *len, uint32_t lenc,
                               const uint8_t *dsym, uint32_t distc,
                               const lzmesh_u37_tok *toks, size_t start,
                               size_t end, uint32_t ds, size_t bs,
                               int is_first, int level) {
    size_t hsz;
    /* V-PINS: L9 twin (own knob; F1 L5 unchanged). */
    /* WSTORE: L1 twin (own knob). */
    int ok = (level == 5 && lzmesh_f1_on())
        || (level == 9 && lzmesh_vpins_l9mid_on())
        || (level == 1 && lzmesh_wstore_l1rescue());
    if (!ok)
        return 0u;
    hsz = lzmesh_h3_huf_block(lit, (size_t)litc, tok, (size_t)tokc,
                              len, (size_t)lenc, dsym, (size_t)distc,
                              toks, start, end, litc, tokc, lenc,
                              distc, ds, is_first, NULL, 0u);
    if (hsz == 0u || hsz >= bs + 5u)
        return 0u;
    return hsz;
}

/* P16-FINDER MEMO: per-block probe memo. h3_multi probed every
 * block twice (measure loop, then emit loop) with identical inputs
 * and discarded the first result ((void) outs). Probe+rescue are
 * pure in-process (no globals/statics on their paths; src/toks/blks
 * unmutated between loops) => loop-2 recompute is dead. Save
 * outs+lanes per block in loop 1 (sok==1 only: pre-publish outs are
 * indeterminate), restore in loop 2. Any malloc fail => per-block
 * stock re-probe (fail-safe, bytes identical incl OOM shape). */
typedef struct {
    int ok; /* saved (else loop 2 re-probes stock) */
    int is_comp, sok;
    uint32_t fo, bo, modes, tokc, lenc, litc, distc;
    unsigned laneb[8];
    uint8_t idx[24];
    unsigned idxsz;
    size_t rhsz;
    unsigned bitc[8]; /* R15-TL1-T2 S4-THREAD: probe-published take bitc */
    uint8_t *lanes; /* lit[litc]+tok[tokc]+len[lenc]+dsym[distc] */
} p16_mblk;
static void p16_mblk_free(p16_mblk *mb, size_t n) {
    size_t j;
    if (mb == NULL)
        return;
    for (j = 0u; j < n; j++)
        free(mb[j].lanes);
    free(mb);
}
static size_t lzmesh_h3_multi(const uint8_t *src, size_t size,
                              const lzmesh_u37_tok *toks,
                              const lzmesh_h3_blk *blks, size_t nblocks,
                              uint8_t *lit, uint8_t *tok, uint8_t *len,
                              uint8_t *dsym, size_t bufcap, uint8_t *dst,
                              size_t dst_capacity, int level,
                              int *rep_bad) {
    size_t i, outpos = 0u, need = 1u;
    p16_mblk *p16_mb = NULL;
    uint32_t dec_rec[4];
    dec_rec[0] = 1u;
    dec_rec[1] = 1u;
    dec_rec[2] = 1u;
    dec_rec[3] = 1u;
    if (rep_bad != NULL)
        *rep_bad = 0;
    if (src == NULL || toks == NULL || blks == NULL || nblocks == 0u
        || lit == NULL || tok == NULL || len == NULL || dsym == NULL)
        return 0u;
    p16_mb = (p16_mblk *)calloc(nblocks, sizeof *p16_mb);
    /* calloc fail => NULL => both loops run stock (fail-safe). */
    for (i = 0u; i < nblocks; i++) {
        uint32_t fo, bo, modes, tokc, lenc, litc, distc;
        unsigned laneb[8], idxsz = 0u;
        unsigned bitc1[8]; /* R15-TL1-T2 S4-THREAD: probe bitc publish */
        uint8_t idx[24];
        size_t t;
        int is_first = (i == 0u) ? 1 : 0;
        int sok = 0;
        size_t rhsz = 0u;
        int is_comp = lzmesh_h3_probe(src, toks, blks[i].start,
                                      blks[i].end, blks[i].term_run,
                                      blks[i].off, blks[i].bs, is_first,
                                      lit, tok, len, dsym, bufcap, &fo,
                                      &bo, &modes, &tokc, &lenc, &litc,
                                      &distc, laneb, idx, &idxsz, &sok,
                                      bitc1);
        (void)bo;
        (void)modes;
        (void)tokc;
        (void)lenc;
        (void)litc;
        (void)distc;
        (void)laneb;
        (void)idx;
        (void)idxsz;
        if (!is_comp && sok)
            rhsz = lzmesh_f1_rescue(lit, litc, tok, tokc, len, lenc,
                                    dsym, distc, toks, blks[i].start,
                                    blks[i].end, (uint32_t)blks[i].bs,
                                    blks[i].bs, is_first, level);
        if (p16_mb != NULL && sok) {
            /* outs valid (published) => save for loop 2. */
            p16_mblk *m = &p16_mb[i];
            size_t tot = (size_t)litc + (size_t)tokc
                + (size_t)lenc + (size_t)distc;
            unsigned kk;
            m->is_comp = is_comp;
            m->sok = sok;
            m->fo = fo;
            m->bo = bo;
            m->modes = modes;
            m->tokc = tokc;
            m->lenc = lenc;
            m->litc = litc;
            m->distc = distc;
            for (kk = 0u; kk < 8u; kk++)
                m->laneb[kk] = laneb[kk];
            for (kk = 0u; kk < 8u; kk++)
                m->bitc[kk] = bitc1[kk];
            memcpy(m->idx, idx, idxsz);
            m->idxsz = idxsz;
            m->rhsz = rhsz;
            m->lanes = NULL;
            m->ok = 0;
            if (tot == 0u) {
                m->ok = 1;
            } else {
                uint8_t *ln = (uint8_t *)malloc(tot);
                if (ln != NULL) {
                    memcpy(ln, lit, litc);
                    memcpy(ln + litc, tok, tokc);
                    memcpy(ln + litc + tokc, len, lenc);
                    memcpy(ln + litc + tokc + lenc, dsym, distc);
                    m->lanes = ln;
                    m->ok = 1;
                }
            }
        }
        if (lzmesh_f1_dbg_on())
            fprintf(stderr,
                    "F1DBG multi b%u comp=%d sok=%d rhsz=%u fo=%u ds=%u\n",
                    (unsigned)i, is_comp, sok, (unsigned)rhsz, fo,
                    (unsigned)blks[i].bs);
        if (is_comp || rhsz != 0u) {
            for (t = blks[i].start; t < blks[i].end; t++) {
                if (toks[t].is_new) {
                    if (level == 1)
                        dec_rec[0] = toks[t].dist;
                    else
                        lzmesh_u37_recents_push(dec_rec, toks[t].dist);
                } else {
                    unsigned slot = toks[t].slot;
                    if (slot > 3u) {
                        if (rep_bad != NULL)
                            *rep_bad = 1;
                        p16_mblk_free(p16_mb, nblocks);
                        return 0u;
                    }
                    if (toks[t].dist != dec_rec[slot]) {
                        if (lzmesh_f1_dbg_on())
                            fprintf(stderr,
                                    "F1DBG multi REPBAD b%u t=%u d=%u slot=%u rec=%u\n",
                                    (unsigned)i, (unsigned)t,
                                    toks[t].dist, slot, dec_rec[slot]);
                        if (rep_bad != NULL)
                            *rep_bad = 1;
                        p16_mblk_free(p16_mb, nblocks);
                        return 0u;
                    }
                    if (level == 1)
                        dec_rec[0] = toks[t].dist;
                    else
                        lzmesh_u37_recents_rep(dec_rec, slot);
                }
            }
            outpos += (rhsz != 0u) ? rhsz : (size_t)fo + 10u;
        } else {
            outpos += blks[i].bs + 5u;
        }
        if (outpos > size + 65536u) {
            p16_mblk_free(p16_mb, nblocks);
            return 0u;
        }
    }
    if (!lzmesh_u3_tier2_keep(outpos, size)) {
        p16_mblk_free(p16_mb, nblocks);
        return 0u;
    }
    need = outpos + 1u;
    if (dst == NULL) {
        p16_mblk_free(p16_mb, nblocks);
        return need;
    }
    if (dst_capacity < need) {
        p16_mblk_free(p16_mb, nblocks);
        return 0u;
    }
    {
        size_t s = 0u;
        for (i = 0u; i < nblocks; i++) {
            uint32_t fo, bo, modes, tokc, lenc, litc, distc;
            unsigned laneb[8], idxsz = 0u, k;
            unsigned p16_bitc[8]; /* R15-TL1-T2 S4-THREAD: memo bitc */
            uint8_t idx[24];
            int is_first = (i == 0u) ? 1 : 0;
            int sok = 0;
            size_t t;
            size_t p16_hsz = 0u;
            int p16_memo = 0;
            int is_comp;
            if (p16_mb != NULL && p16_mb[i].ok) {
                /* loop-1 memo hit: restore outs+lanes (deterministic). */
                p16_mblk *m = &p16_mb[i];
                unsigned kk;
                is_comp = m->is_comp;
                sok = m->sok;
                fo = m->fo;
                bo = m->bo;
                modes = m->modes;
                tokc = m->tokc;
                lenc = m->lenc;
                litc = m->litc;
                distc = m->distc;
                for (kk = 0u; kk < 8u; kk++)
                    laneb[kk] = m->laneb[kk];
                for (kk = 0u; kk < 8u; kk++)
                    p16_bitc[kk] = m->bitc[kk];
                memcpy(idx, m->idx, m->idxsz);
                idxsz = m->idxsz;
                if (m->lanes != NULL) {
                    memcpy(lit, m->lanes, litc);
                    memcpy(tok, m->lanes + litc, tokc);
                    memcpy(len, m->lanes + litc + tokc, lenc);
                    memcpy(dsym, m->lanes + litc + tokc + lenc,
                           distc);
                }
                p16_hsz = m->rhsz;
                p16_memo = 1;
            } else {
                is_comp = lzmesh_h3_probe(src, toks, blks[i].start,
                                          blks[i].end, blks[i].term_run,
                                          blks[i].off, blks[i].bs,
                                          is_first, lit, tok, len, dsym,
                                          bufcap, &fo, &bo, &modes,
                                          &tokc, &lenc, &litc, &distc,
                                          laneb, idx, &idxsz, &sok,
                                          NULL);
            }
            uint32_t ds = (uint32_t)blks[i].bs;
            uint32_t m_lit, m_tok, m_len, m_dist;
            uint32_t payload = 0u;
            if (is_comp
                && (level == 1 || lzmesh_h5_genhuff_on(level))) {
                /* P16-PACK T1: share one measure (was measure+remeasure). */
                p16_huf_ctx p16c;
                size_t hsz = p16_huf_measure(lit, (size_t)litc, tok,
                                             (size_t)tokc, len,
                                             (size_t)lenc, dsym,
                                             (size_t)distc, toks,
                                             blks[i].start, blks[i].end,
                                             litc, tokc, lenc, distc,
                                             ds, is_first,
                                             p16_memo ? p16_bitc : NULL,
                                             &p16c);
                if (hsz != 0u && hsz <= (size_t)fo + 10u
                    && need - s >= hsz) {
                    size_t hw = p16_huf_emit(dst + s, need - s,
                                             lit, (size_t)litc,
                                             tok, (size_t)tokc,
                                             len, (size_t)lenc,
                                             dsym, (size_t)distc,
                                             toks, blks[i].start,
                                             blks[i].end, litc,
                                             tokc, lenc, distc,
                                             ds, &p16c);
                    if (hw == hsz) {
                        s += hw;
                        free(p16c.sbb);
                        continue;
                    }
                }
                free(p16c.sbb); /* S-SBSTASH: gate-fail/hw-mismatch */
            }
            /* F1: HUF-rescue emit (mirrors measure loop). */
            if (!is_comp && sok) {
                size_t hsz = p16_hsz;
                if (!p16_memo)
                    hsz = lzmesh_f1_rescue(lit, litc, tok, tokc,
                                           len, lenc, dsym, distc,
                                           toks, blks[i].start,
                                           blks[i].end, ds, blks[i].bs,
                                           is_first, level);
                if (hsz != 0u) {
                    if (need - s < hsz) {
                        p16_mblk_free(p16_mb, nblocks);
                        return 0u;
                    }
                    {
                        size_t hw = lzmesh_h3_huf_block(lit,
                                                        (size_t)litc, tok,
                                                        (size_t)tokc, len,
                                                        (size_t)lenc, dsym,
                                                        (size_t)distc, toks,
                                                        blks[i].start,
                                                        blks[i].end, litc,
                                                        tokc, lenc, distc,
                                                        ds, is_first,
                                                        dst + s, need - s);
                        if (hw != hsz) {
                            p16_mblk_free(p16_mb, nblocks);
                            return 0u; /* mismatch: decline safe */
                        }
                        s += hw;
                        continue;
                    }
                }
            }
            if (!is_comp) {
                size_t j;
                uint32_t rds = ds;
                if (need - s < blks[i].bs + 5u) {
                    p16_mblk_free(p16_mb, nblocks);
                    return 0u;
                }
                dst[s++] = (uint8_t)LZMESH_U1_TAG_RAW;
                dst[s++] = (uint8_t)(rds & 0xffu);
                dst[s++] = (uint8_t)((rds >> 8) & 0xffu);
                dst[s++] = (uint8_t)((rds >> 16) & 0xffu);
                dst[s++] = (uint8_t)((rds >> 24) & 0xffu);
                for (j = 0u; j < blks[i].bs; j++)
                    dst[s++] = src[blks[i].off + j];
                continue;
            }
            if (need - s < (size_t)fo + 10u) {
                p16_mblk_free(p16_mb, nblocks);
                return 0u;
            }
            m_lit = modes & 7u;
            m_tok = (modes >> 3) & 7u;
            m_len = (modes >> 6) & 7u;
            m_dist = (modes >> 9) & 7u;
            lzmesh_u7_comp_header_emit(dst + s, ds, bo, fo);
            {
                size_t p = s + 9u;
                if (m_lit == LZMESH_U4_MODE_REPEAT) {
                    dst[p++] = lit[0];
                } else {
                    for (t = 0u; t < (size_t)litc; t++)
                        dst[p++] = lit[t];
                }
                if (m_tok == LZMESH_U4_MODE_REPEAT) {
                    dst[p++] = tok[0];
                } else {
                    for (t = 0u; t < (size_t)tokc; t++)
                        dst[p++] = tok[t];
                }
                if (lenc > 0u) {
                    if (m_len == LZMESH_U4_MODE_REPEAT) {
                        dst[p++] = len[0];
                    } else {
                        for (t = 0u; t < (size_t)lenc; t++)
                            dst[p++] = len[t];
                    }
                }
                if (distc > 0u) {
                    if (m_dist == LZMESH_U4_MODE_REPEAT) {
                        dst[p++] = dsym[0];
                    } else {
                        for (t = 0u; t < (size_t)distc; t++)
                            dst[p++] = dsym[t];
                    }
                }
                if (p != s + bo) {
                    p16_mblk_free(p16_mb, nblocks);
                    return 0u;
                }
                for (k = 0u; k < 8u; k++)
                    payload += laneb[k];
                if (payload > 0u) {
                    unsigned start[8], posb[8];
                    uint32_t slot = 0u;
                    for (k = 0u; k < 8u; k++) {
                        start[k] = (unsigned)(p - (s + bo));
                        p += laneb[k];
                        posb[k] = 0u;
                    }
                    memset(dst + s + bo, 0, payload); /* P15-PACK P2a. */
                    for (t = blks[i].start; t < blks[i].end; t++) {
                        unsigned sb, low, bb;
                        uint32_t suf, kk;
                        if (!toks[t].is_new)
                            continue;
                        lzmesh_u4_dist_split_nc(toks[t].dist, &sb, &low,
                                             &suf);
                        kk = slot % 8u;
                        for (bb = 0u; bb < sb; bb++) {
                            unsigned pp = posb[kk]++;
                            if (((suf >> bb) & 1u) != 0u)
                                dst[s + bo + start[kk]
                                    + (pp >> 3)] |=
                                    (uint8_t)(1u << (pp & 7u));
                        }
                        slot++;
                    }
                    if (slot != distc) {
                        p16_mblk_free(p16_mb, nblocks);
                        return 0u;
                    }
                    for (k = 0u; k < 8u; k++) {
                        uint32_t bc = 0u, ss2 = 0u;
                        size_t tt;
                        unsigned m;
                        if (laneb[k] == 0u)
                            continue;
                        for (tt = blks[i].start; tt < blks[i].end;
                             tt++) {
                            if (!toks[tt].is_new)
                                continue;
                            if (ss2 % 8u == k)
                                bc += lzmesh_u3_sb_of(
                                    toks[tt].dist);
                            ss2++;
                        }
                        if (posb[k] != bc) {
                            p16_mblk_free(p16_mb, nblocks);
                            return 0u;
                        }
                        m = bc & 7u;
                        if (m != 0u
                            && lzmesh_u4_pad0_one(distc, k))
                            dst[s + bo + start[k] + laneb[k] - 1u] |=
                                (uint8_t)(1u << m);
                    }
                    for (t = 0u; t < idxsz; t++)
                        dst[p++] = idx[t];
                    if (p != s + fo) {
                        p16_mblk_free(p16_mb, nblocks);
                        return 0u;
                    }
                } else {
                    if (p != s + fo) {
                        p16_mblk_free(p16_mb, nblocks);
                        return 0u;
                    }
                }
                lzmesh_u4_footer_emit(dst + s + fo, modes, tokc, lenc,
                                      litc, distc);
                s += (size_t)fo + 10u;
            }
        }
        dst[s++] = (uint8_t)LZMESH_U1_TAG_END;
        p16_mblk_free(p16_mb, nblocks);
        return (s != 0u && s <= need) ? s : 0u;
    }
}


/* S4: 32B-aligned table bases (HINT-SCHED-R1 sec4: oracle carves
 * scratch-partition table bases 32B-aligned in init prologue; layout
 * only, NO parse-time role -- S2 align sweep flat). Returns aligned
 * pointer (or NULL), stashing the malloc base in *raw for free. */
static void *lzmesh_s4_alloc32(void **raw, size_t n) {
    void *p;
    uintptr_t a;
    if (raw == NULL || n == 0u)
        return NULL;
    p = malloc(n + 32u);
    if (p == NULL)
        return NULL;
    *raw = p;
    a = ((uintptr_t)p + 31u) & ~(uintptr_t)31u;
    return (void *)a;
}

/* P15-P0: F1 fixpoint no-op predicate (HINT-P15-FIXPT M1,
 * ANSWER-p14-struct-1; 2-site audit + proof verified first-hand here).
 * cuts touch u37_parse at exactly 2 sites (S-reset, S-trunc); pass-1
 * invariant: litrun == pos - last_take_end. P0: every it0 cut is
 * take-interior (never queried: pos jumps over) or a take end
 * (reset no-op: litrun already 0) => re-parse takes == pass-1 takes
 * => skip the it-loop (L9: + split-stability, pass 2 vs 1).
 * O(ntok+h3n), zero per-pos cost. Census: 14100 cells, 0 violations,
 * fires incl all 6 bench cells. */
typedef struct {
    const lzmesh_h3_blk *h3b;
    const size_t *l1;
    int isl1;
} p15_p0cuts_t;
static size_t p15_p0_cut_at(const p15_p0cuts_t *s, size_t j) {
    return s->isl1 ? s->l1[j] : s->h3b[j + 1u].off;
}
/* Returns 1 when the it-loop is provably a no-op (skip it). */
static int p15_p0_fire(const lzmesh_u37_tok *toks, size_t ntok,
                       const p15_p0cuts_t *s, size_t ncut) {
    size_t cur = 1u, d = 0u, k;
    size_t st = 0u, en = 1u;
    if (ntok > 0u) {
        st = cur + (size_t)toks[0].litrun;
        en = st + (size_t)toks[0].mlen;
    }
    for (k = 0u; k < ncut; k++) {
        size_t c = p15_p0_cut_at(s, k);
        while (d < ntok && en <= c) {
            if (en == c)
                break;
            d++;
            cur = en;
            if (d < ntok) {
                st = cur + (size_t)toks[d].litrun;
                en = st + (size_t)toks[d].mlen;
            } else {
                st = (size_t)-1;
                en = (size_t)-1;
            }
        }
        if (d < ntok && en == c)
            continue; /* take end: reset no-op (litrun 0) */
        if (d < ntok && st < c && c < en)
            continue; /* interior: never queried (pos jumps over) */
        if (d < ntok && st == c && st == cur)
            continue; /* abutting take start == prev end */
        if (c <= 1u)
            continue; /* parse-start edge */
        return 0; /* lit-gap cut: reset/trunc live, must re-parse */
    }
    return 1;
}

/* Parse + layout + gate; emit iff dst != NULL. Returns bytes (need)
 * on success, 0 on decline/fail. */
static size_t lzmesh_u37_build(const uint8_t *src, size_t size,
                               uint8_t *dst, size_t dst_capacity,
                               int level) {
    int32_t *head = NULL, *prev = NULL;
    uint32_t *big = NULL, *small = NULL;
    void *head_raw = NULL, *prev_raw = NULL; /* S4: 32B-base owners */
    void *big_raw = NULL, *small_raw = NULL;
    unsigned char *vis = NULL;
    unsigned hb = 0u;
    lzmesh_u37_tok *toks = NULL;
    uint8_t *lit = NULL, *tok = NULL, *len = NULL, *dsym = NULL;
    size_t ntok, li, ti, eni, di, t;
    size_t cpos;
    uint32_t litc, tokc, lenc, distc, ds;
    uint32_t dslot; /* R15-TL1-T2 S6-FUSE: fused NEW-take counter. */
    uint32_t m_lit, m_tok, m_len, m_dist, modes;
    uint32_t litB, tokB, lenB, distB, bo, fo;
    unsigned bitc[8], laneb[8], k;
    uint8_t idx[24];
    unsigned idxsz = 0u;
    uint32_t payload = 0u;
    size_t need = 0u, s;
    int lit_eq = 1, tok_eq = 1, len_eq = 1, dist_eq = 1;
    if (src == NULL || size < 4u
        || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lzmesh_u37_owned(src, size)
        && !(level == 1 && lzmesh_u1_u37_reclaim(src, size)))
        return 0;
    if (level == 1 && lzmesh_u37_e01_sparse_bail(src, size))
        return 0; /* E3: oracle-STORE census shape; TIER-2 over-keeps. */
    if (level == 1) { /* S2: MX-slot chains (head 2^hb + prev n); S4: 32B bases */
        hb = lzmesh_u2_hash_bits(size, level);
        head = (int32_t *)lzmesh_s4_alloc32(&head_raw,
            ((size_t)1u << hb) * sizeof *head);
        prev = (int32_t *)lzmesh_s4_alloc32(&prev_raw,
            size * sizeof *prev);
    } else { /* G6: hashed single-slot cascade tables (L5/L9) */
        hb = lzmesh_u2_hash_bits(size, level);
        big = (uint32_t *)lzmesh_s4_alloc32(&big_raw,
            ((size_t)1u << hb) * sizeof *big);
        small = (uint32_t *)lzmesh_s4_alloc32(&small_raw,
            (size_t)LZMESH_U2_H3SIZE * sizeof *small);
    }
    toks = (lzmesh_u37_tok *)malloc(size * sizeof *toks);
    lit = (uint8_t *)malloc(size);
    tok = (uint8_t *)malloc(size);
    len = (uint8_t *)malloc(size + 8u);
    dsym = (uint8_t *)malloc(size);
    vis = (unsigned char *)malloc(size);
    if (toks == NULL || lit == NULL || tok == NULL || len == NULL
        || dsym == NULL || vis == NULL)
        goto out;
    if (level == 1) {
        if (hb == 0u || hb > 21u || head == NULL || prev == NULL)
            goto out;
    } else {
        if (hb == 0u || hb > 21u || big == NULL || small == NULL)
            goto out;
    }
    ntok = lzmesh_u37_parse(src, size, head, prev, big, small, hb, toks,
                            size, vis, level, NULL, 0u);
    if (ntok == 0u || ntok > size)
        goto out;
    {
        size_t h3nr = 0u, h3n = 0u;
        uint32_t h3tr = 0u;
        lzmesh_h3_blk *h3b = NULL;
        lzmesh_u37_tok *toks0 = NULL; /* F1: pass-1 takes (fail-safe) */
        size_t ntok0 = 0u;
        int f1 = ((level == 5 && lzmesh_f1_on())
            || (level == 9 && lzmesh_yf_l9cut_on())
            || (level == 1 && lzmesh_ze01_l1cut_on())) ? 1 : 0;
        if (f1) {
            toks0 = (lzmesh_u37_tok *)malloc(ntok * sizeof *toks0);
            if (toks0 == NULL) {
                f1 = 0;
            } else {
                memcpy(toks0, toks, ntok * sizeof *toks0);
                ntok0 = ntok;
            }
        }
        lzmesh_h3_term(toks, ntok, size, &h3nr, &h3tr);
        if (lzmesh_f1_dbg_on()) {
            size_t d;
            fprintf(stderr, "F1DBG ntok=%u nreal=%u tr=%u\n",
                    (unsigned)ntok, (unsigned)h3nr, h3tr);
            for (d = 0u; d < ntok && d < 8u; d++)
                fprintf(stderr,
                        "F1DBG  t%u run=%u len=%u dist=%u isn=%u slot=%u\n",
                        (unsigned)d, toks[d].litrun, toks[d].mlen,
                        toks[d].dist, toks[d].is_new, toks[d].slot);
        }
        unsigned yftwin0 = 1u; /* S-SPLITSKIP: 1 = unknown/no-skip */
        h3n = lzmesh_h3_split(toks, h3nr, h3tr, size, level, &h3b, f1,
                             &yftwin0);
        if (lzmesh_f1_dbg_on()) {
            size_t d;
            fprintf(stderr, "F1DBG h3n=%u\n", (unsigned)h3n);
            for (d = 0u; d < h3n; d++)
                fprintf(stderr, "F1DBG  b%u off=%u bs=%u s=%u e=%u tr=%u\n",
                        (unsigned)d, (unsigned)h3b[d].off,
                        (unsigned)h3b[d].bs, (unsigned)h3b[d].start,
                        (unsigned)h3b[d].end, h3b[d].term_run);
        }
        if (h3n == 0u || h3b == NULL) {
            if (h3b != NULL)
                free(h3b);
            free(toks0);
            goto out;
        }
        /* F1: T2-3a fixpoint (L5, gated). Re-parse with walk-phase
         * reset at pass-1 cuts (oracle parses per-block); re-split;
         * stop when cuts stabilize (parse deterministic in cuts, so
         * stable cuts = fixed point). Cap 2 re-parses; else fail-safe
         * (pass-1 takes + F1 split). */
        if (f1 && h3n > 1u) {
            int conv = 0;
            int it;
            int p0skip = 0; /* P15-P0: provable no-op fixpoint skip */
            /* Z-E01PAIR R2: L1 resets only at cuts ending a take-rich
             * pass-1 block (h3b take range nonempty). Take-free blocks
             * keep the strided walk: oracle never visits there (s15-blk5
             * d1-run takes declined; every-cut reset overfires nt 6->17).
             * No rich cut => pass-1 stands (conv, baseline-identical).
             * AA-E01BIG: cuts rebuilt per iteration (L5 idiom; default
             * on, LZMESH_AAE01BIG_FIXPT=0 keeps pass-1 cuts). Stale
             * cuts strand stride phase across a shifted split (s18
             * alphabet e01: pass-1 cut 49067 misses 6xd7595 takes that
             * fresh cut 49066 visits; it=1 with stale cuts always
             * self-converges, vacuous). it=0 identical either way. */
            size_t *l1cuts = NULL;
            size_t l1ncut = 0u;
            if (level == 1 && lzmesh_ze01_l1cut_on()) {
                size_t j;
                l1cuts = (size_t *)malloc(h3n * sizeof *l1cuts);
                if (l1cuts != NULL) {
                    for (j = 1u; j < h3n; j++) {
                        if (h3b[j - 1u].end > h3b[j - 1u].start)
                            l1cuts[l1ncut++] = h3b[j].off;
                    }
                }
                if (l1ncut == 0u) {
                    conv = 1;
                    free(l1cuts);
                    l1cuts = NULL;
                }
            }
            /* P15-P0: evaluate over it0 cuts (toks/h3b = pass-1).
             * L1 uses the rich-only l1cuts actually passed to parse. */
            if (!conv) {
                p15_p0cuts_t pcs;
                size_t pcn;
                int p0ok = 0;
                if (level == 1) {
                    pcs.h3b = NULL;
                    pcs.l1 = l1cuts;
                    pcs.isl1 = 1;
                    pcn = l1ncut;
                    if (l1cuts != NULL)
                        p0ok = p15_p0_fire(toks, ntok, &pcs, pcn);
                } else {
                    pcs.h3b = h3b;
                    pcs.l1 = NULL;
                    pcs.isl1 = 0;
                    pcn = h3n - 1u;
                    p0ok = p15_p0_fire(toks, ntok, &pcs, pcn);
                }
                if (p0ok) {
                    if (level == 9) {
                        /* Split-stability (pass 2 vs 1): stock it0
                         * re-splits pass 2; adopt iff offs agree
                         * (exact it0-conv motion), else full loop. */
                        lzmesh_h3_blk *p0b2 = NULL;
                        size_t p0n2;
                        if (f1 == 1 && yftwin0 == 0u) {
                            /* S-SPLITSKIP: call0 twin-quiet+cut-free
                             * => f1=2 re-split provably identical. */
                            p0skip = 1;
                            p0n2 = h3n;
                        } else {
                            p0n2 = lzmesh_h3_split(toks, h3nr, h3tr,
                                                 size, level, &p0b2,
                                                 2, NULL);
                        }
                        if (p0n2 == h3n && p0b2 != NULL) {
                            size_t pj;
                            int pok = 1;
                            for (pj = 0u; pj < h3n; pj++) {
                                if (p0b2[pj].off != h3b[pj].off) {
                                    pok = 0;
                                    break;
                                }
                            }
                            if (pok) {
                                free(h3b);
                                h3b = p0b2;
                                h3n = p0n2;
                                p0skip = 1;
                            } else {
                                free(p0b2);
                            }
                        } else {
                            if (p0b2 != NULL)
                                free(p0b2);
                        }
                    } else {
                        /* L5/L1: same split pass => h3b2==h3b1 when
                         * takes ident => it0-conv guaranteed. */
                        p0skip = 1;
                    }
                }
                if (p0skip)
                    conv = 1;
            }
            for (it = 0; it < 2 && !conv; it++) {
                size_t *cuts = (size_t *)malloc(h3n * sizeof *cuts);
                if (cuts != NULL) {
                    size_t j, nn, h3n2 = 0u;
                    size_t *l1fresh = NULL;
                    size_t l1nfresh = 0u;
                    int use_fresh = 0;
                    lzmesh_h3_blk *h3b2 = NULL;
                    for (j = 1u; j < h3n; j++)
                        cuts[j - 1u] = h3b[j].off;
                    if (level == 1 && lzmesh_ze01_l1cut_on()
                        && lzmesh_aae01big_fixpt_on() && it > 0) {
                        l1fresh = (size_t *)malloc(h3n * sizeof *l1fresh);
                        if (l1fresh != NULL) {
                            for (j = 1u; j < h3n; j++) {
                                if (h3b[j - 1u].end > h3b[j - 1u].start)
                                    l1fresh[l1nfresh++] = h3b[j].off;
                            }
                            if (l1nfresh == 0u) {
                                conv = 1;
                                free(l1fresh);
                                free(cuts);
                                continue;
                            }
                            use_fresh = 1;
                        }
                    }
                    nn = lzmesh_u37_parse(src, size, head, prev, big,
                                          small, hb, toks, size, vis,
                                          level,
                                          (level == 1
                                           && lzmesh_ze01_l1cut_on())
                                              ? (use_fresh ? l1fresh : l1cuts)
                                              : cuts,
                                          (level == 1
                                           && lzmesh_ze01_l1cut_on())
                                              ? (use_fresh ? l1nfresh : l1ncut)
                                              : h3n - 1u);
                    if (nn == 0u || nn > size) {
                        free(l1fresh);
                        free(cuts);
                        break;
                    }
                    ntok = nn;
                    lzmesh_h3_term(toks, ntok, size, &h3nr, &h3tr);
                    /* YF: L9 re-parse splits pass 2 (seacut-L9); L5 keeps 1. */
                    h3n2 = lzmesh_h3_split(toks, h3nr, h3tr, size,
                                           level, &h3b2,
                                           (level == 9
                                            && lzmesh_yf_l9cut_on()) ? 2 : 1, NULL);
                    if (h3n2 == 0u || h3b2 == NULL) {
                        if (h3b2 != NULL)
                            free(h3b2);
                        free(l1fresh);
                        free(cuts);
                        break;
                    }
                    if (h3n2 == h3n) {
                        conv = 1;
                        for (j = 0u; j < h3n; j++) {
                            if (h3b2[j].off != h3b[j].off) {
                                conv = 0;
                                break;
                            }
                        }
                    }
                    free(h3b);
                    h3b = h3b2;
                    h3n = h3n2;
                    free(l1fresh);
                    free(cuts);
                }
            }
            free(l1cuts);
            /* Fail-safe: pass-1 takes + F1 split. (Keep-latest was
             * tried: pass-2 takes mix oracle-exact blk1 takes with
             * spurious d1-run takes (s09/s15-blk5, oracle declines
             * via unbedded run/table rule) and poison framing when
             * takes are incomplete (s05-blk1 (b)-cut at 16626 vs
             * oracle 49141). Tables-wave (Q2) completes the take set;
             * then convergence keeps latest naturally.) */
            if (!conv) {
                ntok = ntok0;
                memcpy(toks, toks0, ntok0 * sizeof *toks);
                lzmesh_h3_term(toks, ntok, size, &h3nr, &h3tr);
                free(h3b);
                h3b = NULL;
                h3n = lzmesh_h3_split(toks, h3nr, h3tr, size, level,
                                      &h3b, 1, NULL);
                if (h3n == 0u || h3b == NULL) {
                    if (h3b != NULL)
                        free(h3b);
                    free(toks0);
                    goto out;
                }
            }
        }
        if (lzmesh_f1_dbg_on()) {
            size_t d;
            fprintf(stderr, "F1DBG post ntok=%u nreal=%u tr=%u h3n=%u\n",
                    (unsigned)ntok, (unsigned)h3nr, h3tr,
                    (unsigned)h3n);
            for (d = 0u; d < ntok && d < 8u; d++)
                fprintf(stderr,
                        "F1DBG  pt%u run=%u len=%u dist=%u isn=%u slot=%u\n",
                        (unsigned)d, toks[d].litrun, toks[d].mlen,
                        toks[d].dist, toks[d].is_new, toks[d].slot);
            for (d = 0u; d < h3n; d++)
                fprintf(stderr, "F1DBG  pb%u off=%u bs=%u s=%u e=%u tr=%u\n",
                        (unsigned)d, (unsigned)h3b[d].off,
                        (unsigned)h3b[d].bs, (unsigned)h3b[d].start,
                        (unsigned)h3b[d].end, h3b[d].term_run);
        }
        if (h3n > 1u) {
            int h3_rep_bad = 0;
            size_t h3need = lzmesh_h3_multi(src, size, toks, h3b, h3n,
                                            lit, tok, len, dsym, size,
                                            dst, dst_capacity, level,
                                            &h3_rep_bad);
            free(h3b);
            if (h3need != 0u) {
                need = h3need;
                free(toks0);
                goto out;
            }
            if (!h3_rep_bad) {
                need = 0u;
                free(toks0);
                goto out;
            }
            /* I1: H3 REP/recents divergence only. Fall through to
             * single-block path (decode-correct). F1: restore
             * global runs (split may have adjusted in place). */
            if (toks0 != NULL) {
                ntok = ntok0;
                memcpy(toks, toks0, ntok0 * sizeof *toks);
            }
            free(toks0);
        } else {
            /* F1: single block (no cuts, takes pristine); restore anyway
             * for uniformity, then free. */
            if (toks0 != NULL) {
                ntok = ntok0;
                memcpy(toks, toks0, ntok0 * sizeof *toks);
            }
            free(toks0);
            free(h3b);
        }
    }
    /* T2 (LANE-T2): E3 WEAK-PARSE KEEP-STOP (L5, 10<=n<=64, mx<=7 ->
     * decline) REMOVED. Falsified: s14-n53-alphabet e05 is oracle-COMP
     * with oracle-parse max 7 (post elig-fix port parse identical,
     * comp_keep keeps: TIER-1 53>41 + TIER-2 51<=53). The stop's
     * s11-n50 protection is subsumed by comp_keep TIER-2 (full
     * battery NEW 0 without the stop). */
    /* L6 SECOND-RUN KEEP-STOP (L5 only; owner: L6): parses whose second
     * take has litrun>=3 at 10<=n<=64 are oracle-STORE (RAW path).
     * Evidence tmp/l6/: 74/74 corpus fires + 31/31 holdout fires STORED
     * by oracle; 8/8 scoped keeps have 2nd-run<=2 (max keep 2, min
     * decline 3, adjacent). L5-only: s16-n57-e09 (2nd-run 4) KEEPS.
     * n<=64 load-bearing (s00-n128 2nd-run 3 keeps). E3STOP-adjacent. */
    if (level == 5 && size >= 10u && size <= 64u && ntok >= 2u
        && toks[1].litrun >= 3u)
        goto out;
    /* Streams. */
    li = 0u;
    ti = 0u;
    eni = 0u;
    di = 0u;
    cpos = 1u;
    lit[li++] = src[0];
    /* R15-TL1-T2 S6-FUSE (ANSWER-r15-tl1-1 sec-c, S3 mirror): T2 fused
     * into the T-loop NEW leg below. Exits in between goto out with
     * need == 0 (set only on success path); bitc stack-local => exact. */
    for (k = 0u; k < 8u; k++)
        bitc[k] = 0u;
    dslot = 0u;
    for (t = 0u; t < ntok; t++) {
        uint32_t run = toks[t].litrun;
        uint32_t mc, ms;
        unsigned lit_f, term;
        uint32_t u;
        for (u = 0u; u < run; u++) {
            if (cpos >= size || li >= size)
                goto out;
            lit[li++] = src[cpos++];
        }
        term = (t + 1u == ntok && toks[t].mlen == 2u
            && toks[t].is_new == 0u && toks[t].slot == 0u
            && cpos + 0u == size) ? 1u : 0u;
        (void)term;
        lit_f = (run <= 2u) ? (unsigned)run : 3u;
        if (toks[t].is_new) {
            unsigned sb, low;
            uint32_t suf;
            if (toks[t].mlen < 2u)
                goto out;
            mc = toks[t].mlen - 2u;
            ms = mc > 30u ? 31u : mc;
            if (ti >= size)
                goto out;
            tok[ti++] = lzmesh_u7_token_new(lit_f, ms);
            lzmesh_u4_dist_split_nc(toks[t].dist, &sb, &low, &suf);
            /* R15-TL1-T2 S6-FUSE: accumulate (sb == sb_of(dist);
             * same NEW order => identical lanes). */
            bitc[dslot % 8u] += sb;
            dslot++;
            if (sb > 28u || suf >= (sb >= 31u ? 0xFFFFFFFFu : (1u << sb))
                || toks[t].dist == 0u)
                goto out;
            if (di >= size)
                goto out;
            dsym[di++] = (uint8_t)((sb << 3) | (low & 7u));
            if (lit_f == 3u) {
                uint8_t eb[5];
                unsigned n = lzmesh_u7_len_escape_write(run - 3u, eb);
                if (n == 0u || eni + n > size)
                    goto out;
                for (u = 0u; u < n; u++)
                    len[eni++] = eb[u];
            }
            if (ms == 31u) {
                uint8_t eb[5];
                unsigned n = lzmesh_u7_len_escape_write(mc - 31u, eb);
                if (n == 0u || eni + n > size)
                    goto out;
                for (u = 0u; u < n; u++)
                    len[eni++] = eb[u];
            }
            cpos += toks[t].mlen;
        } else {
            if (toks[t].slot > 2u || toks[t].mlen < 2u)
                goto out;
            mc = toks[t].mlen - 2u;
            ms = mc > 6u ? 7u : mc;
            if (ti >= size)
                goto out;
            tok[ti++] = lzmesh_u7_token_rep(lit_f, toks[t].slot, ms);
            if (lit_f == 3u) {
                uint8_t eb[5];
                unsigned n = lzmesh_u7_len_escape_write(run - 3u, eb);
                if (n == 0u || eni + n > size)
                    goto out;
                for (u = 0u; u < n; u++)
                    len[eni++] = eb[u];
            }
            if (ms == 7u) {
                uint8_t eb[5];
                unsigned n = lzmesh_u7_len_escape_write(mc - 7u, eb);
                if (n == 0u || eni + n > size)
                    goto out;
                for (u = 0u; u < n; u++)
                    len[eni++] = eb[u];
            }
            /* Terminator match clamps to the ds room (0 here). */
            if (cpos + toks[t].mlen <= size
                && !(t + 1u == ntok && cpos == size))
                cpos += toks[t].mlen;
            else if (!(t + 1u == ntok && cpos == size))
                goto out;
        }
    }
    if (cpos != size)
        goto out;
    if (li > size || ti != ntok || eni > size || di > ntok)
        goto out;
    litc = (uint32_t)li;
    tokc = (uint32_t)ti;
    lenc = (uint32_t)eni;
    distc = (uint32_t)di;
    ds = (uint32_t)size;
    /* Decoder-mirror count gates (C8..C12 + u16 footer). */
    if (tokc == 0u || tokc > ds || lenc > ds || litc > ds)
        goto out;
    if (distc > tokc)
        goto out;
    if (tokc > 0xFFFFu || lenc > 0xFFFFu || litc > 0xFFFFu
        || distc > 0xFFFFu)
        goto out;
    {
        uint32_t tot = 2576u; /* scratch ceiling mirror (S4.2). */
        int32_t stot;
        if (tokc != 0u)
            tot += lzmesh_u37_round32(tokc);
        if (lenc != 0u)
            tot += lzmesh_u37_round32(lenc);
        if (litc != 0u)
            tot += lzmesh_u37_round32(litc);
        if (distc != 0u) {
            tot += lzmesh_u37_round32(distc);
            tot += lzmesh_u37_round32(distc * 4u);
        }
        stot = (int32_t)tot;
        if (stot > (int32_t)65536)
            goto out;
    }
    for (t = 1u; t < li; t++) {
        if (lit[t] != lit[0]) {
            lit_eq = 0;
            break;
        }
    }
    for (t = 1u; t < ti; t++) {
        if (tok[t] != tok[0]) {
            tok_eq = 0;
            break;
        }
    }
    for (t = 1u; t < eni; t++) {
        if (len[t] != len[0]) {
            len_eq = 0;
            break;
        }
    }
    for (t = 1u; t < di; t++) {
        if (dsym[t] != dsym[0]) {
            dist_eq = 0;
            break;
        }
    }
    m_lit = lzmesh_u4_mode_trivial(litc, lit_eq);
    m_tok = lzmesh_u4_mode_trivial(tokc, tok_eq);
    m_len = lzmesh_u4_mode_trivial(lenc, len_eq);
    m_dist = lzmesh_u4_mode_trivial(distc, dist_eq);
    if (m_lit == LZMESH_U4_MODE_HUFFMAN)
        m_lit = LZMESH_U4_MODE_RAW; /* no-H path: HUF->RAW fallback */
    if (m_tok == LZMESH_U4_MODE_HUFFMAN)
        m_tok = LZMESH_U4_MODE_RAW;
    if (m_len == LZMESH_U4_MODE_HUFFMAN)
        m_len = LZMESH_U4_MODE_RAW;
    if (m_dist == LZMESH_U4_MODE_HUFFMAN)
        m_dist = LZMESH_U4_MODE_RAW;
    if (m_lit == LZMESH_U4_MODE_REPEAT && litc > LZMESH_U37_LITREPMAX)
        m_lit = LZMESH_U4_MODE_RAW;
    modes = lzmesh_u4_modes_pack(m_tok, m_len, m_lit, m_dist);
    litB = (litc == 0u) ? 0u
        : ((m_lit == LZMESH_U4_MODE_REPEAT) ? 1u : litc);
    tokB = (tokc == 0u) ? 0u
        : ((m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : tokc);
    lenB = (lenc == 0u) ? 0u
        : ((m_len == LZMESH_U4_MODE_REPEAT) ? 1u : lenc);
    distB = (distc == 0u) ? 0u
        : ((m_dist == LZMESH_U4_MODE_REPEAT) ? 1u : distc);
    {
        uint64_t b = (uint64_t)9 + (uint64_t)litB + (uint64_t)tokB
            + (uint64_t)lenB + (uint64_t)distB;
        if (b > (uint64_t)0xFFFFu)
            goto out;
        bo = (uint32_t)b;
    }
    /* Suffix lanes: slot i -> lane i%8, sb bits LSB-first.
     * R15-TL1-T2 S6-FUSE: T2 loop deleted (fused into T-loop NEW leg);
     * bitc already fused (no re-zero); check on fused dslot. */
    for (k = 0u; k < 8u; k++)
        laneb[k] = 0u;
    if (dslot != distc)
        goto out;
    for (k = 0u; k < 8u; k++) {
        laneb[k] = (bitc[k] + 7u) >> 3;
        payload += laneb[k];
    }
    if (payload == 0u) {
        fo = bo;
    } else {
        unsigned lan8[8];
        for (k = 0u; k < 8u; k++)
            lan8[k] = laneb[k];
        idxsz = lzmesh_pack1_index_pack(lan8, idx, (unsigned)sizeof idx);
        if (idxsz == 0u)
            goto out;
        if ((uint64_t)bo + (uint64_t)payload + (uint64_t)idxsz
            > (uint64_t)0xFFFFu)
            goto out;
        fo = bo + payload + idxsz;
    }
    {
        /* H2 K1: HUF-fo keep (L1 only). RAW-fo C6 fail no longer always
         * declines: F2 est with margin>=5 keeps on HUF-fo (h2_huf);
         * G1 must then emit (proved below) else decline. est==0 and
         * non-L1 paths are byte-identical to before. */
        int h2_huf = 0;
        size_t raw_total = size + (size_t)LZMESH_U1_RAW_OVERHEAD;
        size_t est_total;
        if (lzmesh_u4_comp_gates_ok(ds, bo, fo)) {
            est_total = lzmesh_f2_est_total(lit, li, tok, ti, len, eni,
                                            dsym, di, toks, ntok, distc);
            if (est_total == 0u) {
                if (!lzmesh_u7_comp_keep(ds, fo, (size_t)fo + 10u, size,
                                         ds, bo, litc, 1))
                    goto out;
            } else {
                if (!lzmesh_u3_tier1_comp(ds, fo))
                    goto out;
                if (litc == 0u)
                    goto out;
                if (est_total >= raw_total)
                    goto out;
                /* K1 est-margin (L5/L9; L1 unchanged): margin<5 declines.
                 * s02-MATH unblock (SKIP est margin 4 declines). */
                if (level != 1
                    && raw_total - est_total < (size_t)LZMESH_K1_MARGIN)
                    goto out;
            }
        } else if (level == 1) {
            est_total = lzmesh_f2_est_total(lit, li, tok, ti, len, eni,
                                            dsym, di, toks, ntok, distc);
            if (est_total == 0u || est_total >= raw_total
                || raw_total - est_total < (size_t)LZMESH_H2_MARGIN)
                goto out;
            if (litc == 0u)
                goto out;
            /* HUF C6/TIER-1 implied: est<raw gives fo_est<n=ds. */
            h2_huf = 1;
        } else if (level == 5 || level == 9) {
            /* K1 HUF-fo keep (L5/L9 H2-analog): RAW-fo C6 fail with F2
             * est engaged at margin>=5 keeps on HUF-fo (H5/G1 emit via
             * h2_huf block below; want/emit proved by temp). D0 fix. */
            est_total = lzmesh_f2_est_total(lit, li, tok, ti, len, eni,
                                            dsym, di, toks, ntok, distc);
            if (est_total == 0u || est_total >= raw_total
                || raw_total - est_total < (size_t)LZMESH_K1_MARGIN)
                goto out;
            if (litc == 0u)
                goto out;
            h2_huf = 1;
        } else {
            goto out;
        }
        if (lzmesh_u37_b1_refuse(ds, bo - 9u, tokc, distc, payload == 0u,
                                 modes, litc, lit, tok, len))
            goto out;
        if (h2_huf) {
            size_t vw;
            if (dst == NULL) {
                /* want(): prove G1 emits via temp (want/emit agree). */
                uint8_t *vt = (uint8_t *)malloc(est_total);
                vw = 0u;
                if (vt != NULL) {
                    vw = lzmesh_g1_emit(vt, est_total, lit, li, tok, ti,
                                        len, eni, dsym, di, toks, ntok,
                                        litc, tokc, lenc, distc, ds);
                    free(vt);
                }
                if (vw == 0u)
                    goto out;
                need = vw;
                goto out;
            }
            vw = lzmesh_g1_emit(dst, dst_capacity, lit, li, tok, ti,
                                len, eni, dsym, di, toks, ntok, litc,
                                tokc, lenc, distc, ds);
            if (vw == 0u) {
                need = 0u;
                goto out;
            }
            need = vw;
            goto out;
        }
    }
    if (level == 1 && dst != NULL) {
        /* G1: e01 GEN Huffman emission (RAW fallback inside). */
        size_t w = lzmesh_g1_emit(dst, dst_capacity, lit, li, tok, ti,
                                  len, eni, dsym, di, toks, ntok, litc,
                                  tokc, lenc, distc, ds);
        if (w != 0u) {
            need = w;
            goto out;
        }
    }
    if (lzmesh_h5_genhuff_on(level) && dst != NULL) {
        /* H5: e05/e09 GEN Huffman emission (same emit, RAW fallback). */
        size_t w = lzmesh_g1_emit(dst, dst_capacity, lit, li, tok, ti,
                                  len, eni, dsym, di, toks, ntok, litc,
                                  tokc, lenc, distc, ds);
        if (w != 0u) {
            need = w;
            goto out;
        }
    }
    need = (size_t)fo + 10u + 1u;
    if (dst == NULL)
        goto out; /* want(): gates passed, need set. */
    if (dst_capacity < need) {
        need = 0u;
        goto out;
    }
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    if (m_lit == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = lit[0];
    } else {
        for (t = 0u; t < li; t++)
            dst[s++] = lit[t];
    }
    if (m_tok == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = tok[0];
    } else {
        for (t = 0u; t < ti; t++)
            dst[s++] = tok[t];
    }
    if (lenc > 0u) {
        if (m_len == LZMESH_U4_MODE_REPEAT) {
            dst[s++] = len[0];
        } else {
            for (t = 0u; t < eni; t++)
                dst[s++] = len[t];
        }
    }
    if (distc > 0u) {
        if (m_dist == LZMESH_U4_MODE_REPEAT) {
            dst[s++] = dsym[0];
        } else {
            for (t = 0u; t < di; t++)
                dst[s++] = dsym[t];
        }
    }
    if (s != bo)
        goto fail;
    if (payload > 0u) {
        unsigned start[8], posb[8];
        uint32_t slot = 0u;
        for (k = 0u; k < 8u; k++) {
            start[k] = (unsigned)(s - bo);
            s += laneb[k];
            posb[k] = 0u;
        }
        for (t = 0u; t < payload; t++)
            dst[bo + t] = 0u; /* zero lanes; PAD1 set below */
        for (t = 0u; t < ntok; t++) {
            unsigned sb, low, b;
            uint32_t suf, kk;
            if (!toks[t].is_new)
                continue;
            lzmesh_u4_dist_split_nc(toks[t].dist, &sb, &low, &suf);
            kk = slot % 8u;
            for (b = 0u; b < sb; b++) {
                unsigned p = posb[kk]++;
                if (((suf >> b) & 1u) != 0u)
                    dst[bo + start[kk] + (p >> 3)]
                        |= (uint8_t)(1u << (p & 7u));
            }
            slot++;
        }
        for (k = 0u; k < 8u; k++) {
            if (posb[k] != bitc[k])
                goto fail;
        }
        for (k = 0u; k < 8u; k++) {
            if (laneb[k] > 0u) {
                unsigned m = bitc[k] & 7u;
                if (m != 0u && lzmesh_u4_pad0_one(distc, k))
                    dst[bo + start[k] + laneb[k] - 1u] |=
                        (uint8_t)(1u << m);
            }
        }
        for (t = 0u; t < idxsz; t++)
            dst[s++] = idx[t];
        if (s != fo)
            goto fail;
    } else if (s != fo) {
        goto fail;
    }
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenc, litc, distc);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    goto out;
fail:
    need = 0u;
out:
    if (head != NULL)
        free(head_raw);
    if (prev != NULL)
        free(prev_raw);
    if (big != NULL)
        free(big_raw);
    if (small != NULL)
        free(small_raw);
    if (vis != NULL)
        free(vis);
    if (toks != NULL)
        free(toks);
    if (lit != NULL)
        free(lit);
    if (tok != NULL)
        free(tok);
    if (len != NULL)
        free(len);
    if (dsym != NULL)
        free(dsym);
    return need;
}

int lzmesh_u37_want(const uint8_t *src, size_t size, int level) {
    if (src == NULL || size < 4u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lzmesh_u37_owned(src, size)
        && !(level == 1 && lzmesh_u1_u37_reclaim(src, size)))
        return 0;
    if (lzmesh_u37_build(src, size, NULL, 0, level) != 0)
        return 1;
    if (size <= (size_t)LZMESH_C3_LITFLOOR)
        return 0; /* C3: litonly fallback needs n>=80 at e05. */
    if (lzmesh_u36_want(src, size, 0))
        return 1; /* lit-HUF single fallback (L0 bytes) */
    return lzmesh_u36m_want(src, size); /* lit-HUF multi fallback */
}

size_t lzmesh_u37_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level) {
    size_t w;
    if (dst == NULL || src == NULL)
        return 0;
    if (size < 4u || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (lzmesh_u37_owned(src, size)
        && !(level == 1 && lzmesh_u1_u37_reclaim(src, size)))
        return 0;
    w = lzmesh_u37_build(src, size, dst, dst_capacity, level);
    if (w != 0u)
        return w;
    if (size <= (size_t)LZMESH_C3_LITFLOOR)
        return 0; /* C3: mirrors want() fallback floor. */
    w = lzmesh_u36_emit(dst, dst_capacity, src, size, 0);
    if (w != 0u)
        return w;
    return lzmesh_u36m_emit(dst, dst_capacity, src, size, 0);
}

/* === e01 single-outlier new-dist, level 1 only (owner: u38; G3 any-bg) === */
/* D2-FD5: single-outlier (any bg; G3: was 0-bg-only) at e01 takes
 * hash-new (distc1/2), not rep chain. Stereo parse (black-box
 * n60-pos6..44/n200/n1000 + tail/head
 * ladders): tok1 rep(head-1) lit0, tok2 new6/d7 lit1(spike), tok3
 * new(tail-6)/d6 lit0 iff tail>=15 else term-rep2 lit(tail-6).
 * Gates: L1 floor6 (head>=6, HINT sec4 + pos5/pos6 flip), trail-gate
 * pos+9<=size for tok2 (tail>=9) + tok3 (tail>=15, LANE-C2 sec1 copy).
 * Dists 7,6 sb0 (syms 06,05, no lanes, bo==fo). e01-only: e05/e09
 * stay rep IDENT. O(n) scan + O(1) emit, scratch-free. */
static int lzmesh_u38_locate(const uint8_t *src, size_t size,
                             size_t *ppos) {
    /* G3: any-bg single-outlier (was 0-bg-only spike count). Oracle
     * takes the stereo parse at every bg (census: 5060/5060 lanes +
     * 796/803 n41-62 match; bg00 path byte-identical). */
    size_t opos = 0u;
    if (src == NULL || size <= 1u
        || size > (size_t)LZMESH_U1_DS_MAX)
        return 0;
    if (ppos == NULL)
        return 0;
    if (!lzmesh_e2_single_outlier(src, size, &opos))
        return 0;
    *ppos = opos;
    return 1;
}

static int lzmesh_u38_layout(const uint8_t *src, size_t size, size_t pos,
                             uint8_t *lit, uint8_t *tok, uint8_t *len,
                             uint8_t *dsym, uint32_t *tokc,
                             uint32_t *lenc, uint32_t *litc,
                             uint32_t *distc, uint32_t *modes,
                             uint32_t *bo, uint32_t *fo) {
    uint32_t head, tail, mc1, ms1, mc3, ms3;
    uint8_t e1[5], e3[5];
    unsigned n1 = 0u, n3 = 0u, li = 0u, eni = 0u, u;
    uint32_t m_lit, m_tok, m_len, m_dist;
    uint32_t litB, tokB, lenB, distB;
    int lit_eq = 1, tok_eq = 1, len_eq = 1, dist_eq = 1;
    int two_new;
    if (src == NULL || lit == NULL || tok == NULL || len == NULL
        || dsym == NULL || tokc == NULL || lenc == NULL
        || litc == NULL || distc == NULL || modes == NULL
        || bo == NULL || fo == NULL)
        return 0;
    if (pos < 6u || pos + 1u + 9u > size)
        return 0;
    two_new = (pos + 7u + 9u <= size) ? 1 : 0;
    head = (uint32_t)pos;
    tail = (uint32_t)(size - pos - 1u);
    mc1 = head - 3u;
    ms1 = mc1 > 6u ? 7u : mc1;
    if (ms1 == 7u) {
        n1 = lzmesh_u7_len_escape_write(mc1 - 7u, e1);
        if (n1 == 0u)
            return 0;
    }
    lit[li++] = src[0];
    lit[li++] = src[pos];
    tok[0] = lzmesh_u7_token_rep(0u, 0u, ms1);
    tok[1] = lzmesh_u7_token_new(1u, 4u);
    if (two_new) {
        mc3 = tail - 8u;
        ms3 = mc3 > 30u ? 31u : mc3;
        if (ms3 == 31u) {
            n3 = lzmesh_u7_len_escape_write(mc3 - 31u, e3);
            if (n3 == 0u)
                return 0;
        }
        tok[2] = lzmesh_u7_token_new(0u, ms3);
        dsym[0] = 0x06u;
        dsym[1] = 0x05u;
        *distc = 2u;
    } else {
        uint32_t run = tail - 6u;
        uint8_t eb[5];
        unsigned n = lzmesh_u7_len_escape_write(run - 3u, eb);
        if (n == 0u)
            return 0;
        for (u = 0u; u < tail - 6u; u++)
            lit[li++] = src[0]; /* G3: bg fill (was 0u; pos>=6) */
        tok[2] = lzmesh_u7_token_rep(3u, 0u, 0u);
        for (u = 0u; u < n; u++)
            e3[u] = eb[u];
        n3 = n;
        dsym[0] = 0x06u;
        *distc = 1u;
    }
    for (u = 0u; u < n1; u++)
        len[eni++] = e1[u];
    for (u = 0u; u < n3; u++)
        len[eni++] = e3[u];
    *tokc = 3u;
    *litc = li;
    *lenc = eni;
    for (u = 1u; u < li; u++) {
        if (lit[u] != lit[0]) {
            lit_eq = 0;
            break;
        }
    }
    for (u = 1u; u < 3u; u++) {
        if (tok[u] != tok[0]) {
            tok_eq = 0;
            break;
        }
    }
    for (u = 1u; u < eni; u++) {
        if (len[u] != len[0]) {
            len_eq = 0;
            break;
        }
    }
    for (u = 1u; u < *distc; u++) {
        if (dsym[u] != dsym[0]) {
            dist_eq = 0;
            break;
        }
    }
    m_lit = lzmesh_u4_mode_trivial(*litc, lit_eq);
    m_tok = lzmesh_u4_mode_trivial(*tokc, tok_eq);
    m_len = lzmesh_u4_mode_trivial(*lenc, len_eq);
    m_dist = lzmesh_u4_mode_trivial(*distc, dist_eq);
    if (m_lit == LZMESH_U4_MODE_HUFFMAN
        || m_tok == LZMESH_U4_MODE_HUFFMAN
        || m_len == LZMESH_U4_MODE_HUFFMAN
        || m_dist == LZMESH_U4_MODE_HUFFMAN)
        return 0;
    *modes = lzmesh_u4_modes_pack(m_tok, m_len, m_lit, m_dist);
    litB = (m_lit == LZMESH_U4_MODE_REPEAT) ? 1u : *litc;
    tokB = (m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : *tokc;
    lenB = (*lenc == 0u) ? 0u
        : ((m_len == LZMESH_U4_MODE_REPEAT) ? 1u : *lenc);
    distB = (m_dist == LZMESH_U4_MODE_REPEAT) ? 1u : *distc;
    *bo = 9u + litB + tokB + lenB + distB;
    *fo = *bo;
    return 1;
}

int lzmesh_u38_want(const uint8_t *src, size_t size, int level) {
    size_t pos;
    uint8_t lit[16], tok[4], len[12], dsym[4];
    uint32_t tokc, lenc, litc, distc, modes, bo, fo;
    if (level != 1)
        return 0;
    if (!lzmesh_u38_locate(src, size, &pos))
        return 0;
    /* E5 value-minority decline (black-box: oracle takes a 2-token
     * rep-only fallback at these spike values, u38 stereo over-fires;
     * 2 corpus REGs s09-n56/s11-n48 e01 + validated class).
     * Gates: tail>=10 (tail=9 minority is a long fallback both port
     * paths miss, out of scope), 26<=n<=62 (u38 fires n>=26; n>=63
     * flipset shrinks to {0x22}/none, residue), spike in 7SET at
     * n<=30 else 3SET. Falls through to u21 rep (byte-identical to
     * oracle on every declined cell, enum-proven).
     * G3: bg00-only (census: non-0-bg flips take stereo 5060/5060). */
    {
        uint8_t spike = src[pos];
        uint8_t bgv = (pos == 0u) ? src[1] : src[0];
        size_t tail = size - pos - 1u;
        int v3 = (spike == 0x22u || spike == 0x44u || spike == 0x5du);
        int v7 = (v3 || spike == 0x25u || spike == 0x66u
            || spike == 0x88u || spike == 0xbau);
        if (bgv == 0u && tail >= 10u && size >= 26u && size <= 62u
            && (v3 || (size <= 30u && v7)))
            return 0;
    }
    if (!lzmesh_u38_layout(src, size, pos, lit, tok, len, dsym,
                           &tokc, &lenc, &litc, &distc, &modes,
                           &bo, &fo))
        return 0;
    (void)tokc;
    (void)lenc;
    (void)modes;
    if (!lzmesh_u3_take_floor_ok(level, 1, (uint32_t)pos - 1u))
        return 0;
    if (!lzmesh_u3_take_floor_ok(level, 0, 6u))
        return 0;
    return lzmesh_u7_comp_keep((uint32_t)size, fo, (size_t)fo + 10u,
                               size, (uint32_t)size, bo, litc, 1);
}

size_t lzmesh_u38_emit(uint8_t *dst, size_t dst_capacity,
                       const uint8_t *src, size_t size, int level) {
    size_t pos, need, s, t;
    uint8_t lit[16], tok[4], len[12], dsym[4];
    uint32_t tokc, lenc, litc, distc, modes, bo, fo, ds;
    uint32_t m_tok, m_len, m_lit, m_dist;
    uint32_t litB, tokB, lenB, distB;
    if (dst == NULL || src == NULL || level != 1)
        return 0;
    if (!lzmesh_u38_locate(src, size, &pos))
        return 0;
    if (!lzmesh_u38_layout(src, size, pos, lit, tok, len, dsym,
                           &tokc, &lenc, &litc, &distc, &modes,
                           &bo, &fo))
        return 0;
    ds = (uint32_t)size;
    if (!lzmesh_u4_comp_gates_ok(ds, bo, fo))
        return 0;
    m_lit = modes & 7u;
    m_tok = (modes >> 3) & 7u;
    m_len = (modes >> 6) & 7u;
    m_dist = (modes >> 9) & 7u;
    litB = (m_lit == LZMESH_U4_MODE_REPEAT) ? 1u : litc;
    tokB = (m_tok == LZMESH_U4_MODE_REPEAT) ? 1u : tokc;
    lenB = (lenc == 0u) ? 0u
        : ((m_len == LZMESH_U4_MODE_REPEAT) ? 1u : lenc);
    distB = (m_dist == LZMESH_U4_MODE_REPEAT) ? 1u : distc;
    if ((size_t)9 + (size_t)litB + (size_t)tokB + (size_t)lenB
            + (size_t)distB != (size_t)bo)
        return 0;
    need = (size_t)fo + 10u + 1u;
    if (dst_capacity < need)
        return 0;
    lzmesh_u7_comp_header_emit(dst, ds, bo, fo);
    s = 9u;
    if (m_lit == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = lit[0];
    } else {
        for (t = 0u; t < litc; t++)
            dst[s++] = lit[t];
    }
    if (m_tok == LZMESH_U4_MODE_REPEAT) {
        dst[s++] = tok[0];
    } else {
        for (t = 0u; t < tokc; t++)
            dst[s++] = tok[t];
    }
    if (lenc > 0u) {
        if (m_len == LZMESH_U4_MODE_REPEAT) {
            dst[s++] = len[0];
        } else {
            for (t = 0u; t < lenc; t++)
                dst[s++] = len[t];
        }
    }
    if (distc > 0u) {
        if (m_dist == LZMESH_U4_MODE_REPEAT) {
            dst[s++] = dsym[0];
        } else {
            for (t = 0u; t < distc; t++)
                dst[s++] = dsym[t];
        }
    }
    if (s != bo)
        return 0;
    lzmesh_u4_footer_emit(dst + fo, modes, tokc, lenc, litc, distc);
    dst[fo + 10u] = (uint8_t)LZMESH_U1_TAG_END;
    return need;
}

/* === R8-MIX1NEUTRAL EOF bodies (try-2: stock order + inline mx) ===
 * l1_mx_r0 always_inline (inlined at the u37_best site, E2 shape);
 * h1_l1_nowin noinline (out-of-line L1 span loops); mx_link_ng
 * always_inline (inlines into the nowin helper). l1_rep_r0 elided
 * (stock rep_best_r0 covers L1 under stock order). */

/* R6-MIX1 mx_link_ng: mx_link minus the tail guard. Exact when the
 * caller proves ins+8<=size (then the guard always passes and
 * prev[ins] is in-bounds exactly as stock). */
static inline __attribute__((always_inline)) void
lzmesh_s2_mx_link_ng(int32_t *head, int32_t *prev,
                     const uint8_t *src, size_t size, size_t ins,
                     unsigned hb) {
    uint32_t ms = lzmesh_s2_hx(
        lzmesh_u2_load_n(src + ins, LZMESH_S2_MXLOAD), hb);
    (void)size;
    prev[ins] = head[ms];
    head[ms] = (int32_t)ins;
}

/* R8 try-2: l1_rep_r0 body DELETED (elided; see fwd-decl note). */

/* R7 l1_mx_r0: s2_mx_best under elig-once + build-proven tables.
 * (R6-MIX1 mechanism, redispatched: QBR's u37_best L1 leg calls
 * the r0 pair directly instead of a top-of-best fork, so L5/L9
 * execute zero added branches.)
 * Dropped as dead: elig call (caller-checked), MXLOAD guard
 * (pos+8<=size via pos+9<=size), NULL/hb checks (build rejects
 * hb==0||hb>21||head==NULL||prev==NULL before both parses).
 * Walk/D3/extend/short/loss/K5/dbg verbatim; head verifies via r0
 * (qq-side 8B proven per-link by the kept qq<pos guard).
 * R7: heq via lzmesh_mf_head_eq_r0 (QBR, identical masked-xor). */
static inline __attribute__((always_inline)) int
lzmesh_r6_l1_mx_r0(const uint8_t *src, size_t size, size_t pos,
                   const int32_t *head, const int32_t *prev,
                   unsigned hb, uint32_t *blen, uint32_t *bdist,
                   size_t last_m, size_t last_end, int last_rep,
                   uint32_t rep0) {
    int32_t q;
    uint32_t k5_flen = 0u, k5_fdist = 0u;
    int k5_have = 0;
    int p11_dbg = lzmesh_wpins_s2dbg_at(pos);
    int p11_hide = (pos == last_end);
    /* R7-D2: w8 hoisted (loop-invariant pure load; no stores to src[]
     * in the loop body, dbg fprintf receives values only). */
    uint64_t w8 = lzmesh_wl_ld64(src + pos);
    q = head[lzmesh_s2_hx(
        lzmesh_u2_load_n(src + pos, LZMESH_S2_MXLOAD), hb)];
    if (p11_dbg)
        fprintf(stderr, "WPS2 pos=%u slotq=%d\n", (unsigned)pos,
                (int)q);
    while (q >= 0) { /* J1: uncapped (deep writers visible) */
        size_t qq = (size_t)q;
        uint32_t dist, ln;
        if (qq >= pos || qq >= size)
            break;
        q = prev[qq];
        if (p11_dbg)
            fprintf(stderr, "WPS2  qq=%u heq=%d hid=%d\n",
                    (unsigned)qq,
                    lzmesh_u2_head_eq(src + pos, src + qq,
                                      LZMESH_S2_MXHEAD),
                    lzmesh_i3_l1_span_hide(1, pos, qq, last_m,
                                           last_end, last_rep));
        if (p11_hide
            && lzmesh_i3_l1_span_hide(1, pos, qq, last_m,
                                      last_end, last_rep)) {
            /* I3 hidden: K5-remember first hidden-verifying->=7. */
            if (!k5_have
                && lzmesh_mf_head_eq_r0(w8, src + qq,
                                        LZMESH_S2_MXHEAD)) {
                dist = (uint32_t)(pos - qq);
                ln = lzmesh_u37_extend(src, size, pos, qq,
                                       LZMESH_S2_MXHEAD);
                if (ln >= 7u && lzmesh_u37_loss_ok(ln, dist, 0)) {
                    k5_flen = ln;
                    k5_fdist = dist;
                    k5_have = 1;
                }
            }
            continue;
        }
        if (!lzmesh_mf_head_eq_r0(w8, src + qq, LZMESH_S2_MXHEAD))
            return 0; /* D3 shadow: non-verifying blocks older */
        dist = (uint32_t)(pos - qq);
        /* R9-TEXT1-E6: scalar extend pre-check (L1-only leg): 6B
         * heq-verified above; when pos+14<=size (one predictable
         * guard; proves qq+14<=size too since qq<pos via the stale
         * break above), the next 8B decide inline: xor+ctz resolves
         * ln 6..13 with no extend call (LE ctz = first differing
         * byte; census text-L1: 80.8% of extends <=8, 99.8% <=14).
         * Equal-8B (ln>=14) or tail falls to stock extend (need 14
         * when 6..13 proven equal = same total). Exact: same ln the
         * stock extend-from-6 computes; R7-D2 short/loss deletion
         * covers ln>=6 identically. */
        if (pos + 14u <= size) {
            uint64_t xa, xb;
            memcpy(&xa, src + pos + 6u, 8);
            memcpy(&xb, src + qq + 6u, 8);
            xa ^= xb;
            if (xa != 0u) {
                ln = 6u + (uint32_t)((unsigned)__builtin_ctzll(xa)
                                     >> 3);
                *blen = ln;
                *bdist = dist;
                return 1;
            }
            ln = lzmesh_u37_extend(src, size, pos, qq, 14u);
        } else {
            ln = lzmesh_u37_extend(src, size, pos, qq,
                                   LZMESH_S2_MXHEAD);
        }
        /* R7-D2: short/loss legs deleted (dead: ANSWER-r6-mix1-1
         * short=0 loss=0 over 6.57M queries; noshortloss variant 0
         * DIVs/14476 + bench 3/3 IDENT). heq-verified extends are
         * always >=6 and loss_ok here. */
        *blen = ln;
        *bdist = dist;
        return 1;
    }
    /* K5: no visible->=6; fall back to hidden->=7 (NEW-last only;
     * defer to rep@P+1, len>=2). */
    if (k5_have && !last_rep) {
        if (rep0 >= 1u && rep0 <= pos + 1u && pos + 2u < size
            && src[pos + 1u] == src[pos + 1u - rep0]
            && src[pos + 2u] == src[pos + 2u - rep0])
            return 0; /* K5: defer to rep@P+1 */
        *blen = k5_flen;
        *bdist = k5_fdist;
        return 1;
    }
    return 0;
}

/* R9-TEXT1-E3: force-inline (see fwd-decl note). Loops verbatim. */
/* R11-L1CONT2-H1: x4 unroll of the _ng span loop (L1-only caller).
 * Hashes hoisted (4 loads + 4 muls pipeline; no src stores anywhere
 * so hash/store reorder is invisible); the 8 head/prev stores keep
 * stock order and values, so head/prev states (hence all future
 * queries, hence bytes) are identical. Tail (<4) runs stock. */
static inline __attribute__((always_inline)) void
lzmesh_h1_l1_nowin(int32_t *head, int32_t *prev,
                   const uint8_t *src, size_t size, size_t span_lo,
                   size_t end, unsigned hb) {
    size_t i;
    if (end + 8u <= size) {
        for (i = span_lo; i + 4u <= end; i += 4u) {
            uint32_t s0 = lzmesh_s2_hx(
                lzmesh_u2_load_n(src + i, LZMESH_S2_MXLOAD), hb);
            uint32_t s1 = lzmesh_s2_hx(
                lzmesh_u2_load_n(src + i + 1u, LZMESH_S2_MXLOAD),
                hb);
            uint32_t s2 = lzmesh_s2_hx(
                lzmesh_u2_load_n(src + i + 2u, LZMESH_S2_MXLOAD),
                hb);
            uint32_t s3 = lzmesh_s2_hx(
                lzmesh_u2_load_n(src + i + 3u, LZMESH_S2_MXLOAD),
                hb);
            prev[i] = head[s0];
            head[s0] = (int32_t)i;
            prev[i + 1u] = head[s1];
            head[s1] = (int32_t)(i + 1u);
            prev[i + 2u] = head[s2];
            head[s2] = (int32_t)(i + 2u);
            prev[i + 3u] = head[s3];
            head[s3] = (int32_t)(i + 3u);
        }
        for (; i < end; i++)
            lzmesh_s2_mx_link_ng(head, prev, src, size, i, hb);
    } else {
        for (i = span_lo; i < end; i++)
            lzmesh_s2_mx_link(head, prev, src, size, i, hb);
    }
}
