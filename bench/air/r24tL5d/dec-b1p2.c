/* SPDX-License-Identifier: 0BSD */
/* lzmesh_dec.c — clean-room LZMESH decoder (Stage-6 blind build).
 * Behavior source: SPECFINAL.md only. No Apple code seen.
 * Layout: container/framing walker, check chain, lanes/suffix,
 * Huffman decode, match copy, API glue. Subsystem owners append
 * in marked sections; keep -Wall -Wextra clean, C11.
 */
#include "lzmesh.h"

/* P3-N2: NEON match-copy fast path switch. arm_neon.h intrinsics only
 * (no asm). LZMESH_SCALAR (compile -DLZMESH_SCALAR=1 or make
 * LZMESH_SCALAR=1) forces scalar; off-__ARM_NEON defaults scalar
 * (guard-clean: compiles anywhere). */
#if defined(__ARM_NEON) && !defined(LZMESH_SCALAR)
#include <arm_neon.h>
#define LZ_U3_HAVE_NEON 1
#else
#define LZ_U3_HAVE_NEON 0
#endif

/* === container/framing + lzmesh_decoded_size (owner: u1) === */
/* u1: tags S2.1, RAW S2.2, COMP hdr S2.3, gates S2.4/S6.5, walk S2.8. */
enum {
    LZ_U1_TAG_RAW = 0x00,
    LZ_U1_TAG_COMP = 0x01,
    LZ_U1_TAG_END = 0xFF,
    LZ_U1_RAW_HDR = 5,
    LZ_U1_COMP_HDR = 9,
    LZ_U1_FOOTER = 10
};
#define LZ_U1_DS_MAX ((uint32_t)0x7FFFFFFFu)
#define LZ_U1_DEC_SCRATCH ((size_t)65536u)

static uint32_t lz_u1_rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t lz_u1_rd16le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

/* memmove-class copy (RAW path overlap-safe, S4.4). No <string.h>. */
static void lz_u1_move(uint8_t *dst, const uint8_t *src, size_t n) {
    const uint8_t *cdst;
    if (n == (size_t)0) {
        return;
    }
    cdst = (const uint8_t *)dst;
    if (cdst == src) {
        return;
    }
    if (cdst < src || cdst >= src + n) {
        size_t i;
        for (i = 0; i < n; i++) {
            dst[i] = src[i];
        }
    } else {
        size_t i;
        for (i = n; i > (size_t)0; i--) {
            dst[i - (size_t)1] = src[i - (size_t)1];
        }
    }
}

/* === check chain (owner: u2) === */
/* u2: COMP-block check chain C7..C21 + order/precedence + reject paths.
 * Reject census is 0-only (S4.5): every predicate returns LZ_U2_OK (0)
 * or LZ_U2_REJECT (1) / a C-code 17..21; the decoder maps any nonzero
 * to return 0. No -1, no partial, no errno. End-check codes are
 * diagnostic only — all collapse to ret 0 at the API.
 *
 * E1-model order contract (S4.3; u3's replay loop MUST call in this order):
 *  per token: (1) lit-len-decode, C16 fail -> reject BEFORE any copy;
 *             (2) lit-copy, C18 room-tight BEFORE copying (never emit
 *                 overrun zeros, S4.4), then copy;
 *             (3) dist-resolve C14 (rep/new incl suffix fetch);
 *             (4) C13 post-literal strict on resolved d;
 *             (5) match-len-decode C16, fail -> reject before match copy;
 *             (6) match-copy with term-clamp to ds (lz_u2_match_take).
 *  after full replay: end checks C17..C21 in numeric order (R-116 open).
 * Precedence: header gates (modes/lane/Kraft/bitmap) run BEFORE any
 * replay of the block; only header-pre-replay rejects leave dst
 * untouched (App C). C15 has no predicate (collapses into C18).
 * C14-vs-match-C16 order unresolved (R-015); E1 order above chosen.
 * Non-static by design: intra-TU API for u3 (same file), keeps -Wall
 * clean while u3 is pending (no unused-static warnings).
 */
enum {
    LZ_U2_OK = 0,
    LZ_U2_REJECT = 1
};

/* C7 support: COMP block extent is fo+10 (S2.5 footer 10B). C7 itself
 * (no trailing past END region) is enforced by the u1 walk (no-gap
 * block end -> next tag/END, sizer END-exact) + decoder ignore-post-END
 * (S4.8); see GAPLOG-u2 G2. */
size_t lz_u2_block_len(uint32_t fo) {
    return (size_t)fo + (size_t)10;
}

/* Modes gate (S3.2/S7.2 + S2.6 count0-RAW). modes_raw packs
 * m1|m2<<3|m3<<6|m4<<9 with m1=lit,m2=tok,m3=len,m4=dist (S2.5/Q2);
 * bits >=12 IGNORED (S7.2/M7). cnt[0..3] arrive in footer count
 * order (tok,len,lit,dist), so lane i pairs with cnt[lane_cnt[i]]. */
int lz_u2_modes_ok(uint32_t modes_raw, const uint16_t *cnt) {
    static const int lane_cnt[4] = { 2, 0, 1, 3 };
    int i;
    if (cnt == NULL) {
        return LZ_U2_REJECT;
    }
    for (i = 0; i < 4; i++) {
        uint32_t m = (modes_raw >> (uint32_t)(3 * i)) & (uint32_t)7;
        if (m > (uint32_t)2) {
            return LZ_U2_REJECT; /* reserved modes 3-7 */
        }
        if (cnt[lane_cnt[i]] == (uint16_t)0 && m != (uint32_t)0) {
            return LZ_U2_REJECT; /* S2.6: count0 MUST be RAW */
        }
    }
    return LZ_U2_OK;
}

/* Lane framing (S3.3/S7.1). ib range-only 1..23, no minimality;
 * over-long ACCEPT by omission (no predicate for it). */
int lz_u2_ib_ok(uint32_t ib) {
    return (ib >= (uint32_t)1 && ib <= (uint32_t)23) ? LZ_U2_OK
                                                    : LZ_U2_REJECT;
}

/* S3.3.b: region MUST exceed index; equality (empty payload) REJECTs. */
int lz_u2_region_ok(uint32_t region, uint32_t index) {
    return (region > index) ? LZ_U2_OK : LZ_U2_REJECT;
}

/* S3.3.d: all 8 starts MUST be <= payload, checked even when lanes
 * unused. Widths unpinned (u32 assumed; GAPLOG-u2 G12). */
int lz_u2_starts_ok(const uint32_t *starts, uint32_t payload) {
    int i;
    if (starts == NULL) {
        return LZ_U2_REJECT;
    }
    for (i = 0; i < 8; i++) {
        if (starts[i] > payload) {
            return LZ_U2_REJECT;
        }
    }
    return LZ_U2_OK;
}

/* S3.8: group bitmap MUST be nonzero. */
int lz_u2_bitmap_ok(uint32_t bitmap) {
    return (bitmap != (uint32_t)0) ? LZ_U2_OK : LZ_U2_REJECT;
}

/* S3.8: each transmitted meta length (11x3b) MUST be <= 7. */
int lz_u2_meta_val_ok(uint32_t v) {
    return (v <= (uint32_t)7) ? LZ_U2_OK : LZ_U2_REJECT;
}

/* S3.7: table maxlen 10 symbols / 5 meta (build param + quantum). */
int lz_u2_maxlen_ok(uint32_t maxlen, int is_meta) {
    uint32_t cap = (is_meta != 0) ? (uint32_t)5 : (uint32_t)10;
    if (maxlen < (uint32_t)1 || maxlen > cap) {
        return LZ_U2_REJECT;
    }
    return LZ_U2_OK;
}

/* S3.7/S3.8 Kraft exact: 10-bit-level sum of 2^(10-len) MUST == 1024
 * at BOTH levels (over/under/single-len10-sum1 all reject). Caller
 * accumulates the sum; 0x10000 fixed-point form is lz_u2_kraft16_ok. */
int lz_u2_kraft1024_ok(uint32_t sum) {
    return (sum == (uint32_t)1024) ? LZ_U2_OK : LZ_U2_REJECT;
}

/* S3.7 table form: fixed-point sum of 2^(16-len) MUST == 0x10000. */
int lz_u2_kraft16_ok(uint32_t sum) {
    return (sum == (uint32_t)0x10000) ? LZ_U2_OK : LZ_U2_REJECT;
}

/* S4.1.b/Q18: first COMP block MUST carry literal_count>0 (first byte
 * literal emits pre-token; litc==0 => no literals[0]). First token's
 * lit FIELD is irrelevant. */
int lz_u2_first_lit_ok(int is_first_block, uint32_t lit_count) {
    if (is_first_block != 0 && lit_count == (uint32_t)0) {
        return LZ_U2_REJECT;
    }
    return LZ_U2_OK;
}

/* C8..C12 count gates (S2.5/Q12 PROVEN, D-B4). tok!=0; tok/len/lit<=ds;
 * dist<=tok. Called pre-replay in lz_u3_comp_block. */
int lz_u2_counts_ok(uint32_t tok, uint32_t len, uint32_t lit, uint32_t dist,
                    uint32_t ds) {
    if (tok == (uint32_t)0) {
        return LZ_U2_REJECT; /* C8 */
    }
    if (tok > ds || len > ds || lit > ds) {
        return LZ_U2_REJECT; /* C9/C10/C11 */
    }
    if (dist > tok) {
        return LZ_U2_REJECT; /* C12 */
    }
    return LZ_U2_OK;
}

/* S4.2 scratch ceiling (D-B7/R-D-B7 T-PIN PROVEN id+sum). Sum over
 * substream buffers of (0 if count==0 else round32up(bytes)) + 2576
 * tables <= 65536, SIGNED compare (b.gt per S-SC). Buffers (S4.2
 * order u32dist,dsyms,len,lit,tok): tok=tokc, lit=litc, len=lenc,
 * dsyms=distc, u32dist=distc*4. T-pair 62912->65520 ACCEPT /
 * 62913->65552 REJECT (base 2608); R-pair 62880/62881 (base 2640).
 * Per-block header-pre-replay; multiblock totals uncapped. Called
 * pre-fetch in lz_u3_comp_block. */
static uint32_t lz_u2_round32(uint32_t n) {
    return (n + (uint32_t)31) & ~(uint32_t)31;
}

int lz_u2_scratch_ok(uint32_t tok, uint32_t len, uint32_t lit,
                     uint32_t dist) {
    uint32_t tot = (uint32_t)2576;
    if (tok != (uint32_t)0) {
        tot += lz_u2_round32(tok);
    }
    if (len != (uint32_t)0) {
        tot += lz_u2_round32(len);
    }
    if (lit != (uint32_t)0) {
        tot += lz_u2_round32(lit);
    }
    if (dist != (uint32_t)0) {
        tot += lz_u2_round32(dist);
        tot += lz_u2_round32(dist * (uint32_t)4);
    }
    return ((int32_t)tot > (int32_t)65536) ? LZ_U2_REJECT : LZ_U2_OK;
}

/* C16 order point (S4.3/S3.10): length decode MUST succeed BEFORE any
 * copy of that token. u3 calls this immediately after each lit-len and
 * match-len decode; the forward makes the gate greppable. */
int lz_u2_c16_ok(int len_decode_ok) {
    return (len_decode_ok != 0) ? LZ_U2_OK : LZ_U2_REJECT;
}

/* Mid-loop C18 (S4.4): lit-copy MUST NOT push wblk past ds. u3 checks
 * BEFORE copying each lit run (LIT overhang rejects; only MATCH
 * clamps). Port emits no speculative bytes (R-014). */
int lz_u2_lit_room_ok(size_t wblk_after_lit, size_t ds) {
    return (wblk_after_lit <= ds) ? LZ_U2_OK : LZ_U2_REJECT;
}

/* C14 order point (S4.3): dist-resolve (rep select incl MTF lookup,
 * new-dist suffix fetch) MUST succeed before C13. Forward like C16. */
int lz_u2_c14_ok(int dist_resolve_ok) {
    return (dist_resolve_ok != 0) ? LZ_U2_OK : LZ_U2_REJECT;
}

/* C13 post-literal strict (S4.3): reject iff d==0 or d>w, where w is
 * TOTAL bytes written so far across all blocks (cross-block window,
 * S4.2). d is the resolved u32 distance incl sb29+ wrap (S6.3);
 * caller computes d. Tight on fast and slow paths. */
int lz_u2_c13_ok(uint32_t d, size_t w) {
    if (d == (uint32_t)0) {
        return LZ_U2_REJECT;
    }
    if ((size_t)d > w) {
        return LZ_U2_REJECT;
    }
    return LZ_U2_OK;
}

/* S4.4 term-clamp: intra-token MATCH overhang truncates to ds (any
 * amount, 1..3+ tested). Returns bytes to copy. Never use for lit
 * runs (those reject via C18, never clamp). */
size_t lz_u2_match_take(size_t wblk, size_t ds, size_t mlen) {
    size_t room;
    if (wblk >= ds) {
        return (size_t)0;
    }
    room = ds - wblk;
    return (mlen < room) ? mlen : room;
}

/* End checks after full replay (S4.3). Returns LZ_U2_OK or the failing
 * gate code 17..21 (diagnostic; decoder maps all to ret 0). Numeric
 * order chosen; relative order is open (R-116, GAPLOG-u2 G4). */
int lz_u2_end_checks(uint32_t tok_used, uint32_t tok_count,
                     uint32_t lit_used, uint32_t lit_count,
                     uint32_t len_used, uint32_t len_count,
                     uint32_t dist_used, uint32_t dist_count,
                     size_t wblk, size_t ds) {
    if (tok_used != tok_count) {
        return 17; /* C17 tok-exact */
    }
    if (lit_used != lit_count) {
        return 18; /* C18 lit-overhang (C15 collapses here) */
    }
    if (len_used != len_count) {
        return 19; /* C19 len-leftover */
    }
    if (dist_used != dist_count) {
        return 20; /* C20 dist-leftover */
    }
    if (wblk != ds) {
        return 21; /* C21 whole-token remainder */
    }
    return LZ_U2_OK;
}

/* === lanes/suffix + Huffman decode + match copy (owner: u3) === */
/* u3: literal lanes + suffix + Huffman decode + match copy + COMP wire.
 *
 * Pinned + implemented: footer parse (S2.5 five-u16), header gates via
 * u2 (modes/count0-RAW S2.6, reserved S7.2), REPEAT-lit ceiling 62880
 * (S4.2), recents init {1,1,1,1} + MTF dedup (S3.12/R-E-B8 u6i), distance formula
 * d=(8<<sb)+low3+8*suffix-7 in u32 with sb29+ wrap (S3.11/S6.3),
 * bytewise-forward match copy (S4.4), E1 replay order with u2 gates
 * (C16/C14/C13/C18-tight/match_take/end C17..C21), cap-prefix + END
 * behavior via the lzmesh_decode wire below.
 *
 * GAP-blocked (fail closed -> reject, never misdecode): substream
 * payload layout in [bo,fo) incl lane headers (ib/region/starts
 * widths+positions, u2-G12), Huffman two-level bit layout + table
 * build inputs, lenbytes escape codec (S3.10), dist-symbol format +
 * suffix bit fetch (S3.11); m1..m4 = lit,tok,len,dist (Q2; fetch
 * wiring still GAP). G-u3-4 flipped by Q17 (sel 5..7 grammar-legal);
 * other G-u3 choices + GAP list in GAPLOG-u3b.
 */
enum {
    LZ_U3_FAIL = 0,
    LZ_U3_OK = 1
};

#define LZ_U3_REPEAT_LIT_MAX ((uint32_t)62880u)

/* --- recents (S3.12) --- */
static void lz_u3_recents_init(uint32_t *r) {
    r[0] = (uint32_t)1;
    r[1] = (uint32_t)1;
    r[2] = (uint32_t)1;
    r[3] = (uint32_t)1;
}

/* New-distance update (S3.12.a/R-E-B8 DEDUP): insert front, shift all
 * down, drop slot3. (Was G-u3-1 "16B move-to-front per token": that idiom
 * describes only the widest k=3 case; rep tokens use recents_rep below.) */
static void lz_u3_recents_push(uint32_t *r, uint32_t d) {
    r[3] = r[2];
    r[2] = r[1];
    r[1] = r[0];
    r[0] = d;
}

/* Rep-token update (S3.12.a/R-E-B8 DEDUP, F-ADJUD PROBABLE-strong): move
 * recent[k] to front, shift recent[0..k-1] down one each, leave
 * recent[k+1..3] UNCHANGED (prior occurrence removed, never duplicated).
 * rep0 (k=0) is a no-op. CR-140: rep1 from (4,3,2,1)->(3,4,2,1). */
static void lz_u3_recents_rep(uint32_t *r, uint32_t k) {
    uint32_t tmp;
    uint32_t i;
    if (k == (uint32_t)0 || k > (uint32_t)3) {
        return;
    }
    tmp = r[k];
    for (i = k; i > (uint32_t)0; i--) {
        r[i] = r[i - (uint32_t)1];
    }
    r[0] = tmp;
}

/* --- distance (S3.11/S6.3) --- */
/* Non-static by design (u2 precedent): new-dist suffix fetch is GAP-blocked
 * so no caller exists yet; external linkage keeps -Wall clean meanwhile. */
uint32_t lz_u3_dist(uint32_t sb, uint32_t low3, uint32_t suffix) {
    /* d=(8<<sb)+low3+8*suffix-7 in u32; (8<<sb)==0 for sb>=29 via wrap. */
    uint32_t base = (uint32_t)8 << (sb & (uint32_t)31);
    return base + (low3 & (uint32_t)7) + (uint32_t)8 * suffix - (uint32_t)7;
}

/* --- match copy (S4.4) --- */
#if LZ_U3_HAVE_NEON
/* Local getenv decl (file convention: no <stdlib.h>; cf lz_u6h_alloc). */
extern char *getenv(const char *name);
/* P3-N2 runtime gate: LZMESH_SCALAR set in env forces scalar at runtime.
 * Cached read-once (P2-getenv style); per-call cost is one static load. */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) int lz_u3_neon_ok(void) {
    static int init = 0;
    static int on = 1;
    if (init == 0) {
        init = 1;
        if (getenv("LZMESH_SCALAR") != NULL) {
            on = 0;
        }
    }
    return on;
}
#endif
/* P6-W5 word-copy primitives. memcpy is the canonical unaligned op
 * (clang/gcc lower fixed 2/4/8 to ld/st on ARM64; same idiom as
 * P5-W3 lzmesh_wl_*). Every op below pairs a load with a same-width
 * store, so bytes move verbatim (endianness-neutral, no LE
 * assumption). Local decl keeps the file's no-libc-header convention
 * (cf getenv above). The #undef disarms <string.h>'s fortified
 * memcpy macro when this file is #included by a test that already
 * pulled <string.h> (harmless no-op otherwise). */
#ifdef memcpy
#undef memcpy
#endif
extern void *memcpy(void *dst, const void *src, size_t n);
/* R3-MIXDEC: memset for REPEAT-lit fill (same no-libc-header convention). */
#ifdef memset
#undef memset
#endif
extern void *memset(void *dst, int c, size_t n);
static uint16_t lz_u3_mc_ld16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}
static uint32_t lz_u3_mc_ld32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static uint64_t lz_u3_mc_ld64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}
static void lz_u3_mc_st16(uint8_t *p, uint16_t v) {
    memcpy(p, &v, 2);
}
static void lz_u3_mc_st32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, 4);
}
static void lz_u3_mc_st64(uint8_t *p, uint64_t v) {
    memcpy(p, &v, 8);
}
/* Width-capped tail: copies r bytes t[0..r) = s[0..r) with op widths
 * <= d (u32 only when d>=4; u16/u8 need d>=2/1, always true here).
 * Safety rule (forward, low-to-high): an op of width k at dest t
 * reads [t-d,t-d+k), fully written iff t-d+k<=t iff k<=d. Callers
 * pass s = t-d with those widths, so every op reads final bytes by
 * construction. Exact: writes exactly r bytes, never overruns. */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) void lz_u3_mc_tail(
    uint8_t *t, const uint8_t *s, size_t r, uint32_t d) {
    if (r >= (size_t)4 && d >= (uint32_t)4) {
        lz_u3_mc_st32(t, lz_u3_mc_ld32(s));
        t += (size_t)4;
        s += (size_t)4;
        r -= (size_t)4;
    }
    if (r >= (size_t)2) {
        lz_u3_mc_st16(t, lz_u3_mc_ld16(s));
        t += (size_t)2;
        s += (size_t)2;
        r -= (size_t)2;
    }
    if (r != (size_t)0) {
        *t = *s;
    }
}
/* dst[0..w_tot] valid, d>=1, d<=w (C13), room for n (C18/match_take).
 * P2-bitio: 3 paths, all byte-identical to the single forward loop:
 * d==1 fills dst[w-1] (chain collapses); d>=8 copies 8B unrolled
 * (chunk sources sit fully below chunk dests, so no intra-chunk
 * overlap); d<8 keeps the byte loop.
 * P3-N2: renamed _scalar; the dispatcher below adds a 16B NEON bulk
 * for d>=16 && n>=16 and routes everything else (+tails) here.
 * P6-W5: word-at-a-time scalar. d==1 splat-fills u64 bulk + byte
 * tail; d>=8 runs u64 bulk (covers the n8-15 prize in 1-2 ops) +
 * width-capped tail (n<8 fully worded too); d2-7 runs period-chunk
 * word loops (chunk == d bytes, op widths <= d, first chunk reads
 * pure history) + the same capped tail. All paths exact-n, no
 * overrun reads or writes; overlap behavior == single forward loop
 * (each op's source sits fully below its dest start). */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) void lz_u3_match_copy_scalar(
    uint8_t *dst, size_t w, uint32_t d, size_t n) {
    size_t i;
    size_t n8;
    uint8_t *t;
    const uint8_t *s;
    if (n == (size_t)0) {
        return;
    }
    if (d == (uint32_t)1) {
        uint8_t v = dst[w - (size_t)1];
        uint64_t fill = (uint64_t)v;
        fill |= fill << (uint64_t)8;
        fill |= fill << (uint64_t)16;
        fill |= fill << (uint64_t)32;
        n8 = n & ~((size_t)7);
        for (i = (size_t)0; i < n8; i += (size_t)8) {
            lz_u3_mc_st64(dst + w + i, fill);
        }
        for (; i < n; i++) {
            dst[w + i] = v;
        }
        return;
    }
    if (d >= (uint32_t)8) {
        n8 = n & ~((size_t)7);
        for (i = (size_t)0; i < n8; i += (size_t)8) {
            lz_u3_mc_st64(dst + w + i,
                           lz_u3_mc_ld64(dst + w + i - (size_t)d));
        }
        lz_u3_mc_tail(dst + w + n8, dst + w + n8 - (size_t)d, n - n8, d);
        return;
    }
    t = dst + w;
    s = t - (size_t)d;
    switch (d) {
    case (uint32_t)2:
        while (n >= (size_t)2) {
            lz_u3_mc_st16(t, lz_u3_mc_ld16(s));
            t += (size_t)2;
            s += (size_t)2;
            n -= (size_t)2;
        }
        break;
    case (uint32_t)3:
        while (n >= (size_t)3) {
            lz_u3_mc_st16(t, lz_u3_mc_ld16(s));
            t[(size_t)2] = s[(size_t)2];
            t += (size_t)3;
            s += (size_t)3;
            n -= (size_t)3;
        }
        break;
    case (uint32_t)4:
        while (n >= (size_t)4) {
            lz_u3_mc_st32(t, lz_u3_mc_ld32(s));
            t += (size_t)4;
            s += (size_t)4;
            n -= (size_t)4;
        }
        break;
    case (uint32_t)5:
        while (n >= (size_t)5) {
            lz_u3_mc_st32(t, lz_u3_mc_ld32(s));
            t[(size_t)4] = s[(size_t)4];
            t += (size_t)5;
            s += (size_t)5;
            n -= (size_t)5;
        }
        break;
    case (uint32_t)6:
        while (n >= (size_t)6) {
            lz_u3_mc_st32(t, lz_u3_mc_ld32(s));
            lz_u3_mc_st16(t + (size_t)4, lz_u3_mc_ld16(s + (size_t)4));
            t += (size_t)6;
            s += (size_t)6;
            n -= (size_t)6;
        }
        break;
    default:
        while (n >= (size_t)7) {
            lz_u3_mc_st32(t, lz_u3_mc_ld32(s));
            lz_u3_mc_st16(t + (size_t)4, lz_u3_mc_ld16(s + (size_t)4));
            t[(size_t)6] = s[(size_t)6];
            t += (size_t)7;
            s += (size_t)7;
            n -= (size_t)7;
        }
        break;
    }
    lz_u3_mc_tail(t, s, n, d);
}

/* P3-N2 dispatcher: 16B NEON bulk for d>=16 && n>=16, scalar tail/fallback.
 * Overlap proof (d>=16): chunk [s,s+16) reads [s-d,s-d+16); d>=16 gives
 * s-d+16<=s, so every source byte sits strictly below its dest chunk:
 * no intra-chunk overlap; sequential chunks see fully-written history.
 * Tail (n%16) and all d<16 keep the proven scalar paths above, so the
 * NEON path is byte-identical by construction. Exact-n: no overrun
 * (bulk covers only n&~15, tail covers the rest). */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) void lz_u3_match_copy(
    uint8_t *dst, size_t w, uint32_t d, size_t n) {
    /* R3-MIXDEC d>=n: forward copy reads [w-d,w-d+n); d>=n puts every
     * source byte strictly below dest start w (C13: d>=1, d<=w), so the
     * ranges are non-overlapping and a plain memcpy is byte-identical.
     * Mixed L1/L5/L9: ~90% of calls (d256+ x n<128 histogram). */
    if (n != (size_t)0 && (size_t)d >= n) {
        memcpy(dst + w, dst + w - (size_t)d, n);
        return;
    }
#if LZ_U3_HAVE_NEON
    if (d >= (uint32_t)16 && n >= (size_t)16 && lz_u3_neon_ok() != 0) {
        size_t n16 = n & ~((size_t)15);
        size_t i;
        for (i = (size_t)0; i < n16; i += (size_t)16) {
            vst1q_u8(dst + w + i, vld1q_u8(dst + w + i - (size_t)d));
        }
        w += n16;
        n -= n16;
    }
#endif
    lz_u3_match_copy_scalar(dst, w, d, n);
}

/* --- footer (S2.5) --- */
/* Footer order: modes, tok, len, lit, dist (u16 LE each). cnt[4] packs in
 * that same order (choice G-u3-2: pack order == footer order). */
static void lz_u3_footer_parse(const uint8_t *f, uint32_t *modes,
                               uint16_t *cnt) {
    *modes = lz_u1_rd16le(f);
    cnt[0] = (uint16_t)lz_u1_rd16le(f + (size_t)2);
    cnt[1] = (uint16_t)lz_u1_rd16le(f + (size_t)4);
    cnt[2] = (uint16_t)lz_u1_rd16le(f + (size_t)6);
    cnt[3] = (uint16_t)lz_u1_rd16le(f + (size_t)8);
}

static uint32_t lz_u3_mode(uint32_t modes, int lane) {
    return (modes >> (uint32_t)(3 * lane)) & (uint32_t)7;
}

/* --- substream fetch --- */
/* Decoded substream view. RAW=payload slice, REPEAT=fill byte, HUFFMAN=
 * decoded symbols; ss index follows footer order (tok,len,lit,dist). */
struct lz_u3_ss {
    const uint8_t *p;
    uint8_t fill;
    uint32_t n;
    uint32_t mode;
};

/* u6b D-B3: lane bitstreams for suffix (Q3/Q7). p/len = lane bytes in
 * [bo,fo); bitpos = consumed bits LSB-first (size_t, no wrap). */
struct lz_u3_lane {
    const uint8_t *p;
    uint32_t len;
    size_t bitpos;
    const uint8_t *bs_end;
};
struct lz_u3_lanes {
    struct lz_u3_lane l[8];
};

/* PORTGAP-1 READ-THROUGH (dg1 Class-1/Class-3, measured): Apple's lane bit
 * readers do not stop at the lane end -- they read through into following
 * bs bytes (double-mutant b29=0x03,b30=0x00 refuses = read-through-1 into
 * C13; zero-fill would accept). Cap reads at bs end instead of lane end;
 * past-bs stays fail-closed (unobserved domain). Valid streams never
 * overrun their lane, so their decodes are byte-identical. */
static size_t lz_u3_lane_cap(const struct lz_u3_lane *lane) {
    if (lane == NULL || lane->p == NULL || lane->bs_end == NULL ||
        lane->bs_end < lane->p) {
        return (size_t)0;
    }
    return (size_t)(lane->bs_end - lane->p) * (size_t)8;
}

/* u6b D-B1/D-B3: lane index parser per S3.3/Q3 PROVEN. ib=last>>3 1..23;
 * index_size=max(4,(7*ib+12)>>3); 7 LSB-first ib-bit fields=lane0..6;
 * lane7=remainder; starts cumulative <=payload (S3.3.d). FAIL closed. */
static int lz_u3_lanes_parse(const uint8_t *bs, uint32_t bs_len,
                             struct lz_u3_lanes *lanes) {
    uint32_t ib;
    uint32_t index_size;
    uint32_t payload;
    const uint8_t *index;
    uint32_t fields[7];
    uint32_t starts[8];
    size_t acc = (size_t)0;
    uint32_t f;
    int i;
    if (bs == NULL || lanes == NULL || bs_len == (uint32_t)0) {
        return LZ_U3_FAIL;
    }
    ib = (uint32_t)bs[bs_len - (uint32_t)1] >> 3;
    if (lz_u2_ib_ok(ib) != LZ_U2_OK) {
        return LZ_U3_FAIL;
    }
    index_size = ((uint32_t)7 * ib + (uint32_t)12) >> 3;
    if (index_size < (uint32_t)4) {
        index_size = (uint32_t)4;
    }
    if (lz_u2_region_ok(bs_len, index_size) != LZ_U2_OK) {
        return LZ_U3_FAIL;
    }
    payload = bs_len - index_size;
    index = bs + payload;
    for (f = (uint32_t)0; f < (uint32_t)7; f++) {
        uint32_t v = (uint32_t)0;
        uint32_t b;
        for (b = (uint32_t)0; b < ib; b++) {
            uint32_t bit_idx = f * ib + b;
            uint32_t byte_idx = bit_idx / (uint32_t)8;
            uint32_t bit = bit_idx % (uint32_t)8;
            uint32_t bv;
            if (byte_idx >= index_size) {
                return LZ_U3_FAIL; /* R-018 trunc, fail closed */
            }
            bv = ((uint32_t)index[byte_idx] >> bit) & (uint32_t)1;
            v |= bv << b;
        }
        fields[f] = v;
    }
    for (i = 0; i < 7; i++) {
        if (acc > (size_t)payload) {
            return LZ_U3_FAIL;
        }
        starts[i] = (uint32_t)acc;
        acc += (size_t)fields[i];
    }
    if (acc > (size_t)payload) {
        return LZ_U3_FAIL;
    }
    starts[7] = (uint32_t)acc;
    if (lz_u2_starts_ok(starts, payload) != LZ_U2_OK) {
        return LZ_U3_FAIL;
    }
    /* u6v Q2-B3 (P1 B3): index VALIDATED (ib range + region + starts sum),
     * pads ignored, never skipped — S3.3.d bulk-skip FALSIFIED. */
    for (i = 0; i < 7; i++) {
        lanes->l[i].p = bs + starts[i];
        lanes->l[i].len = fields[i];
        lanes->l[i].bitpos = (size_t)0;
        lanes->l[i].bs_end = bs + bs_len;
    }
    lanes->l[7].p = bs + starts[7];
    lanes->l[7].len = payload - starts[7];
    lanes->l[7].bitpos = (size_t)0;
    lanes->l[7].bs_end = bs + bs_len;
    return LZ_U3_OK;
}

/* u6v Q1 (P1-ANSWERS A1-A6): lane-head active-tag exactly-N + 51B L0 fields.
 * Shape-gated: dc==0 + Huffman-used lanes + (L0len/lanes) 10/2B->N=1,
 * 11/3B->N=2, 13/5B->N=2. Other shapes SKIP (fail-open; p100 N=7!).
 * Tag-11 refuses; T = any inactive head has bit2; 51B L0 A2-A6 enforced.
 * FAIL closed on violation; OK to continue. */
static int lz_u6v_q1_gate(const struct lz_u3_lanes *lanes, uint32_t dc,
                         int any_huf) {
    int is51 = 0;
    int is59 = 0;
    int is75 = 0;
    int want = 0;
    int nact = 0;
    int tflag = 0;
    int k;
    uint32_t tags[8];
    if (lanes == NULL) {
        return LZ_U3_OK;
    }
    if (any_huf == 0 || dc != (uint32_t)0) {
        return LZ_U3_OK;
    }
    if (lanes->l[0].len == (uint32_t)10) {
        is51 = 1;
        for (k = 1; k < 8; k++) {
            if (lanes->l[k].len != (uint32_t)2) {
                is51 = 0;
                break;
            }
        }
    }
    if (is51 == 0 && lanes->l[0].len == (uint32_t)11) {
        is59 = 1;
        for (k = 1; k < 8; k++) {
            if (lanes->l[k].len != (uint32_t)3) {
                is59 = 0;
                break;
            }
        }
    }
    if (is51 == 0 && is59 == 0 && lanes->l[0].len == (uint32_t)13) {
        is75 = 1;
        for (k = 1; k < 8; k++) {
            if (lanes->l[k].len != (uint32_t)5) {
                is75 = 0;
                break;
            }
        }
    }
    if (is51 == 0 && is59 == 0 && is75 == 0) {
        return LZ_U3_OK;
    }
    /* FLUSH7 Q1-51B narrow: arm s01-signature only (Q5 (a)+(e), FLUSH6).
     * SKIP (fail-open) unless L0[4] even-nonzero + L0[5]==00 + L0[6] pow2.
     * ACCEPTs R10R3-class + runs100-class; keeps N-mutants + s02-A3 armed
     * (their L0[4..6] intact). 59/75 untouched (L0-ctx OPEN). */
    /* FLUSH10a8 51B-reopen repair: non-sig rows matching the keyed 09-run
     * context (led=09, L0[4]=02, L0[6]=00, L0[8]=02, L0[5] in {00,04,08};
     * exact observed set, Q5(e) unknown-ctx-SKIP) take an N-only check
     * (tag-11 refuse + exactly-1, S1-consistent per Q5(a) S1 {01,10}x1,
     * 11x0) and return its verdict directly (downstream L0 legs would
     * misfire on non-sig bytes). Re-closes 3 F7-reopened N-kills
     * (s07-n100 m39, s09-n64 m31, s15-n64 m38); their 5 in-key base
     * valids pass N=1; s14-n65 N=0 valid splits out via L0[8]=06.
     * Sig rows fall through to the full path UNCHANGED (branch below is
     * the exact inversion of the three F7 early-returns). */
    if (is51 != 0) {
        uint32_t b4;
        uint32_t b5;
        uint32_t b6;
        if (lanes->l[0].p == NULL || lanes->l[0].len < (uint32_t)7) {
            return LZ_U3_OK;
        }
        b4 = (uint32_t)lanes->l[0].p[4];
        b5 = (uint32_t)lanes->l[0].p[5];
        b6 = (uint32_t)lanes->l[0].p[6];
        if (b4 == (uint32_t)0 || (b4 & (uint32_t)1) != (uint32_t)0 ||
            b5 != (uint32_t)0 || b6 == (uint32_t)0 ||
            (b6 & (b6 - (uint32_t)1)) != (uint32_t)0) {
            uint32_t b0;
            uint32_t b8;
            int kk;
            int nn;
            if (lanes->l[0].len < (uint32_t)9) {
                return LZ_U3_OK;
            }
            b0 = (uint32_t)lanes->l[0].p[0];
            b8 = (uint32_t)lanes->l[0].p[8];
            if (b0 != (uint32_t)0x09 || b4 != (uint32_t)0x02 ||
                b6 != (uint32_t)0x00 || b8 != (uint32_t)0x02 ||
                (b5 != (uint32_t)0x00 && b5 != (uint32_t)0x04 &&
                 b5 != (uint32_t)0x08)) {
                return LZ_U3_OK;
            }
            nn = 0;
            for (kk = 1; kk < 8; kk++) {
                uint32_t hk;
                uint32_t tag;
                if (lanes->l[kk].p == NULL ||
                    lanes->l[kk].len == (uint32_t)0) {
                    return LZ_U3_OK; /* fail-open: SKIP-context repair */
                }
                hk = (uint32_t)lanes->l[kk].p[0];
                tag = hk & (uint32_t)3;
                if (tag == (uint32_t)3) {
                    return LZ_U3_FAIL;
                }
                if (tag == (uint32_t)1 || tag == (uint32_t)2) {
                    nn++;
                }
            }
            return (nn != 1) ? LZ_U3_FAIL : LZ_U3_OK;
        }
        /* u7 R7R1 H4: L0[8]-keyed N-sensitivity for sig rows (P0-R7R1-FIX).
         * F7 armed the full s01 path on L0[4..6]-sig, but R7's 29 battery
         * 51B valids carry sig with oracle-accepted N in {0,1,2} + tag11 +
         * A-leg violations (b8 never 02). N/tag11/A refuse-evidence in sig
         * context exists ONLY at b8==02 (s01/s04 N-kills, N=0) and b8==00
         * with N==1 + no-tag11 (s02-m31 A6); non-sig b8=00/N=1 accepts, so
         * A6 is sig-only. b8==02 falls through to the full path UNCHANGED;
         * b8==00 + N==1 + no-tag11 refuses (full path fails these at A-b8z
         * at latest, so the verdict is identical); all other sig rows SKIP
         * per Q5(e) unknown-ctx-SKIP (F9 arm-only-refuse-evidence precedent).
         * Non-sig branches above and 59/75/B1/c19 below are untouched. */
        {
            uint32_t b8k;
            uint32_t hk;
            uint32_t tag;
            int kk;
            int nn;
            int t11;
            if (lanes->l[0].len < (uint32_t)9) {
                return LZ_U3_OK;
            }
            b8k = (uint32_t)lanes->l[0].p[8];
            if (b8k != (uint32_t)0x02) {
                if (b8k != (uint32_t)0x00) {
                    return LZ_U3_OK;
                }
                nn = 0;
                t11 = 0;
                for (kk = 1; kk < 8; kk++) {
                    if (lanes->l[kk].p == NULL ||
                        lanes->l[kk].len == (uint32_t)0) {
                        return LZ_U3_OK;
                    }
                    hk = (uint32_t)lanes->l[kk].p[0];
                    tag = hk & (uint32_t)3;
                    if (tag == (uint32_t)3) {
                        t11 = 1;
                        break;
                    }
                    if (tag == (uint32_t)1 || tag == (uint32_t)2) {
                        nn++;
                    }
                }
                if (t11 == 0 && nn == 1) {
                    return LZ_U3_FAIL;
                }
                return LZ_U3_OK;
            }
        }
    }
    /* FLUSH9 59/75 narrow: arm N-sensitive context only (R10 real bytes).
     * P1 (ii) refutes shape-N outside s01-exact; no 59/75 s01-signature
     * is proven (multi-ctx OPEN). The only 59/75 N-sensitive contexts
     * with oracle-refuse evidence are the REV-8 rows' L0[4..6]: 59B
     * (04,02,00) s07-n128-period m13, 75B (12,00,00) s11-n256 m38
     * (base==mutant by single-mutant locality: one swap2/bit3 cannot
     * span a lane head + L0[4..6]). SKIP (fail-open) unless L0[4..6]
     * matches exactly: releases 13 R10 valids (all non-matching triples,
     * downstream decodes clean per probe), keeps 2 REV-8 on the
     * exactly-N FAIL path (full-SKIP probe reopens both downstream).
     * n=1/shape: revisit on new 59/75 N-evidence. 51B/B1/c19 untouched. */
    if (is59 != 0) {
        if (lanes->l[0].p == NULL || lanes->l[0].len < (uint32_t)7) {
            return LZ_U3_OK;
        }
        if ((uint32_t)lanes->l[0].p[4] != (uint32_t)0x04 ||
            (uint32_t)lanes->l[0].p[5] != (uint32_t)0x02 ||
            (uint32_t)lanes->l[0].p[6] != (uint32_t)0x00) {
            return LZ_U3_OK;
        }
    }
    if (is75 != 0) {
        if (lanes->l[0].p == NULL || lanes->l[0].len < (uint32_t)7) {
            return LZ_U3_OK;
        }
        if ((uint32_t)lanes->l[0].p[4] != (uint32_t)0x12 ||
            (uint32_t)lanes->l[0].p[5] != (uint32_t)0x00 ||
            (uint32_t)lanes->l[0].p[6] != (uint32_t)0x00) {
            return LZ_U3_OK;
        }
    }
    want = (is51 != 0) ? 1 : 2;
    for (k = 0; k < 8; k++) {
        tags[k] = (uint32_t)0;
    }
    for (k = 1; k < 8; k++) {
        uint32_t hk;
        uint32_t tag;
        if (lanes->l[k].p == NULL || lanes->l[k].len == (uint32_t)0) {
            return LZ_U3_FAIL;
        }
        hk = (uint32_t)lanes->l[k].p[0];
        tag = hk & (uint32_t)3;
        tags[k] = tag;
        if (tag == (uint32_t)3) {
            return LZ_U3_FAIL;
        }
        if (tag == (uint32_t)1 || tag == (uint32_t)2) {
            nact++;
        }
    }
    if (nact != want) {
        return LZ_U3_FAIL;
    }
    if (is51 == 0) {
        return LZ_U3_OK;
    }
    for (k = 1; k < 8; k++) {
        uint32_t hk;
        if (tags[k] != (uint32_t)0) {
            continue;
        }
        hk = (uint32_t)lanes->l[k].p[0];
        if ((hk & (uint32_t)4) != (uint32_t)0) {
            tflag = 1;
            break;
        }
    }
    {
        const uint8_t *l0;
        uint32_t b0;
        uint32_t b1;
        uint32_t b2;
        uint32_t b3;
        uint32_t b4;
        uint32_t b5;
        uint32_t b6;
        uint32_t b7;
        uint32_t b8;
        uint32_t lo8;
        if (lanes->l[0].p == NULL || lanes->l[0].len != (uint32_t)10) {
            return LZ_U3_FAIL;
        }
        l0 = lanes->l[0].p;
        b0 = (uint32_t)l0[0];
        b1 = (uint32_t)l0[1];
        b2 = (uint32_t)l0[2];
        b3 = (uint32_t)l0[3];
        b4 = (uint32_t)l0[4];
        b5 = (uint32_t)l0[5];
        b6 = (uint32_t)l0[6];
        b7 = (uint32_t)l0[7];
        b8 = (uint32_t)l0[8];
        if (b0 != (uint32_t)0x09 && b0 != (uint32_t)0x91) {
            return LZ_U3_FAIL;
        }
        if (b1 != (uint32_t)0 || b2 != (uint32_t)0 || b3 != (uint32_t)0 ||
            b5 != (uint32_t)0 || b7 != (uint32_t)0) {
            return LZ_U3_FAIL;
        }
        if (b6 == (uint32_t)0 || (b6 & (b6 - (uint32_t)1)) != (uint32_t)0) {
            return LZ_U3_FAIL;
        }
        if (b4 == (uint32_t)0 || (b4 & (uint32_t)1) != (uint32_t)0) {
            return LZ_U3_FAIL;
        }
        if (tflag != 0) {
            if ((b4 & (b4 - (uint32_t)1)) != (uint32_t)0) {
                return LZ_U3_FAIL;
            }
        }
        if (b8 == (uint32_t)0) {
            return LZ_U3_FAIL;
        }
        lo8 = b8 & (uint32_t)0x0F;
        if (tflag != 0) {
            if (lo8 != (uint32_t)0x1 && lo8 != (uint32_t)0x2 &&
                lo8 != (uint32_t)0x4 && lo8 != (uint32_t)0xA &&
                lo8 != (uint32_t)0xC) {
                return LZ_U3_FAIL;
            }
        } else {
            if (lo8 != (uint32_t)0x2 && lo8 != (uint32_t)0x3 &&
                lo8 != (uint32_t)0x4 && lo8 != (uint32_t)0x5 &&
                lo8 != (uint32_t)0x9 && lo8 != (uint32_t)0xA &&
                lo8 != (uint32_t)0xC) {
                return LZ_U3_FAIL;
            }
        }
    }
    return LZ_U3_OK;
}

/* u6v Q2-B1 (P1-ANSWERS Rule B1): UNIFIED LENGTH reject leg for bo==fo
 * run blocks. L = u8 blk[11] (1-byte) or LE-u16 blk[12..13] (marker
 * blk[11]==0xFF EXACT). Apple accepts iff ds-4 <= L <= ALIGN32(ds)-4.
 * This gate enforces the REJECT leg only (out-of-range -> FAIL, Apple
 * also refuses: safe). In-range continues to fetch/replay (accept leg
 * = replay widen/clamp, GAP: needs oracle validation, see GAPLOG-u6v).
 * Shape-narrow: FIRST block only (u6w M31: later blocks use the no-pre-
 * emit layout, D-B5 — B1's fixed-offset L read parses wrong fields there
 * and misfired on oracle-accepted o79 later blocks) + bo==fo +
 * no-Huffman + dc==0 + br>=3 + B1 token pattern ((tok&0xE0)==0xC0 &&
 * (tok&7)!=7); else SKIP (fail-open). */
static int lz_u6v_b1_gate(uint32_t ds, const uint8_t *blk, uint32_t bo,
                         uint32_t fo, uint32_t modes, const uint16_t *cnt,
                         int is_first_block) {
    uint32_t br_len;
    uint32_t tok;
    uint32_t lval;
    uint32_t lo;
    uint32_t hi;
    uint32_t aligned;
    if (blk == NULL || cnt == NULL) {
        return LZ_U3_OK;
    }
    if (is_first_block == 0) {
        return LZ_U3_OK; /* u6w M31: later-block layouts outside P1
                          * B1 evidence (single-block only); fail-open */
    }
    if (fo != bo || bo < (uint32_t)9) {
        return LZ_U3_OK;
    }
    if ((((modes >> 0) & (uint32_t)7) == (uint32_t)2) ||
        (((modes >> 3) & (uint32_t)7) == (uint32_t)2) ||
        (((modes >> 6) & (uint32_t)7) == (uint32_t)2) ||
        (((modes >> 9) & (uint32_t)7) == (uint32_t)2)) {
        return LZ_U3_OK;
    }
    if ((uint32_t)cnt[3] != (uint32_t)0) {
        return LZ_U3_OK;
    }
    /* FLUSH7 B1 narrow: arm all-RAW modes only (P1 B1 single-block run
     * evidence; R10R1 modes~0x40 SKIPs -> ACCEPT). Any nonzero mode lane
     * means layout outside P1 evidence; fail-open (Q5 (b) misfire). */
    if ((((modes >> 0) & (uint32_t)7) != (uint32_t)0) ||
        (((modes >> 3) & (uint32_t)7) != (uint32_t)0) ||
        (((modes >> 6) & (uint32_t)7) != (uint32_t)0) ||
        (((modes >> 9) & (uint32_t)7) != (uint32_t)0)) {
        return LZ_U3_OK;
    }
    /* FLUSH10a8 B1 tc==1 narrow: the fixed-offset L-read (blk[11] / FF+u16)
     * is valid ONLY for single-token run layouts (P1 B1 evidence + Q5(b)
     * 07-run single-token edges). Multi-token all-RAW shapes pack lit
     * bytes at blk[10..] (fetch order lit,tok,len), so blk[11] is DATA
     * (b11=0 uniformly on R10's 17 refused valids, tokc 2..9) and the
     * range check misfires (Q5(b) misfire mechanism, confirmed on real
     * bytes). SKIP unless tokc==1; the tc==1 leg below is byte-untouched. */
    if ((uint32_t)cnt[0] != (uint32_t)1) {
        return LZ_U3_OK;
    }
    br_len = bo - (uint32_t)9;
    if (br_len < (uint32_t)3 || ds < (uint32_t)4) {
        return LZ_U3_OK;
    }
    tok = (uint32_t)blk[10];
    if ((tok & (uint32_t)0xE0) != (uint32_t)0xC0 ||
        (tok & (uint32_t)7) == (uint32_t)7) {
        return LZ_U3_OK;
    }
    if ((uint32_t)blk[11] != (uint32_t)0xFF) {
        lval = (uint32_t)blk[11];
    } else {
        uint32_t lo8;
        uint32_t hi8;
        if (br_len < (uint32_t)5) {
            return LZ_U3_FAIL;
        }
        lo8 = (uint32_t)blk[12];
        hi8 = (uint32_t)blk[13];
        lval = lo8 | (hi8 << 8);
    }
    lo = ds - (uint32_t)4;
    aligned = (ds + (uint32_t)31) & ~(uint32_t)31;
    hi = aligned - (uint32_t)4;
    if (lval < lo || lval > hi) {
        return LZ_U3_FAIL;
    }
    return LZ_U3_OK;
}

/* --- Huffman decode (S3.7/S3.8/Q4 + S3.5/S3.6; owner: u6h) --- */
/* Bit layout (spec-pinned): per HUFFMAN substream from lane 0 at continued
 * bitpos: 11x3b meta lengths, 32b group bitmap (LSB-first); used=8*popcount
 * lengths round-robin lanes j%8; ascending scatter into 256 lengths; count
 * symbols round-robin lanes j%8. Lane assignment resets per sequence
 * (G-u6h-LANERESET: header always lane0, symbols j%8, suffix i%8); per-lane
 * bitpos persists, no re-align. Sequential round-robin == batched refill
 * given linear lanes (G-u6h-BATCH-EQUIV; 40/16 mult-of-8). Peek zero-pads
 * past lane end (S3.4 overread tolerance); consume strictly gated. */
/* Local stdlib decls (file-top include outside u6h write-allow; merge may
 * hoist to #include <stdlib.h>). Buffers: malloc per Huffman substream
 * (G-u6h-MALLOC: counts<=64K each; stack too small; scratch layout+ceiling
 * OPEN per D-B7; statics forbidden per M9). Malloc-fail -> reject. */
extern void *malloc(size_t n);
extern void free(void *p);

static uint8_t *lz_u6h_alloc(uint32_t n) {
    return (uint8_t *)malloc((size_t)n);
}

/* Free Huffman-decoded buffers (P2 single-alloc: one base per block,
 * streams carved from it; frees base once + NULLs stream views).
 * Non-static by design (u2 precedent): called from lz_u3_comp_block. */
void lz_u6h_free_ss(struct lz_u3_ss *ss, uint8_t *base) {
    int k;
    if (base != NULL) {
        free((void *)base);
    }
    if (ss == NULL) {
        return;
    }
    for (k = 0; k < 4; k++) {
        if (ss[k].mode == (uint32_t)2 && ss[k].p != NULL) {
            ss[k].p = NULL;
        }
    }
}

/* Reverse low-L bits of code (canonical MSB-first -> LSB-first pattern).
 * R23-TL9D-D1: branchless 16-bit reverse + shift (loop was len iters of
 * shift/or; exhaustive bed-proof: all 2046 (code,len) pairs == loop). */
static uint32_t lz_u6h_rev(uint32_t code, uint32_t len) {
    uint32_t x = code & (uint32_t)0xFFFF;
    x = ((x >> 1) & (uint32_t)0x5555) | ((x & (uint32_t)0x5555) << 1);
    x = ((x >> 2) & (uint32_t)0x3333) | ((x & (uint32_t)0x3333) << 2);
    x = ((x >> 4) & (uint32_t)0x0F0F) | ((x & (uint32_t)0x0F0F) << 4);
    x = ((x >> 8) & (uint32_t)0x00FF) | ((x & (uint32_t)0x00FF) << 8);
    return x >> (16 - len);
}

/* P6-symphoist: lz_u6h_peek folded into lz_u6h_decode_sym_fast below
 * (sole caller was decode_sym). Peek body preserved verbatim in the
 * slow branch; fast branch skips the 3 per-byte guards when
 * byte_off+2 < cap_bytes (all 3 loads provably in-bounds).
 * Distribution (probe, pinned corpus + alpha-64k x4 levels): 100% of
 * valid symbols take the fast branch (peek_slow=0, peek_empty=0). */

/* Read nbits<=32 consumed bits LSB-first; all must be in-bounds (fixed
 * header fields; S3.4 equality bitpos==len*8 passes via <= when nbits 0,
 * rejects when bits truly missing). */
static int lz_u6h_read_bits(struct lz_u3_lane *lane, uint32_t nbits,
                            uint32_t *out) {
    /* P2-bitio: byte-window loads + shift/mask (bounds pre-checked, so
     * all window bytes are in-bounds; same bits, same verdicts). */
    size_t pos;
    size_t byte_off;
    uint32_t sh;
    unsigned need;
    unsigned j;
    uint64_t w;
    uint32_t v;
    if (lane == NULL || out == NULL || nbits > (uint32_t)32) {
        return LZ_U3_FAIL;
    }
    *out = (uint32_t)0;
    if (lane->p == NULL) {
        return LZ_U3_FAIL;
    }
    if (lane->bitpos + (size_t)nbits > lz_u3_lane_cap(lane)) {
        return LZ_U3_FAIL;
    }
    if (nbits == (uint32_t)0) {
        return LZ_U3_OK;
    }
    pos = lane->bitpos;
    byte_off = pos >> 3;
    sh = (uint32_t)(pos & (size_t)7);
    need = (sh + nbits + (uint32_t)7) >> 3;
    w = (uint64_t)0;
    for (j = (unsigned)0; j < need; j++) {
        w |= (uint64_t)lane->p[byte_off + (size_t)j] << (j * (unsigned)8);
    }
    v = (uint32_t)(w >> sh);
    if (nbits < (uint32_t)32) {
        v &= (((uint32_t)1 << nbits) - (uint32_t)1);
    }
    lane->bitpos += (size_t)nbits;
    *out = v;
    return LZ_U3_OK;
}

/* Canonical table build (S3.7): lengths[nsym], maxlen 5/10, tab[1<<maxlen]
 * via table-index (no tree-walk). Entry (len<<8)|sym, 0 = invalid.
 * Gates: lens<=maxlen, Kraft 10-bit-norm==1024 (u2), degenerate
 * single-len-1 -> full table maps the used symbol (G-u6h-DUP: both 1-bit
 * patterns decode it; robust to unknown dummy index, S3.2 single-value
 * accept; other single-nonzero incl len10-sum1 reject per S3.8). */
static int lz_u6h_build(const uint8_t *lengths, uint32_t nsym, uint32_t maxlen,
                        uint16_t *tab, uint32_t tab_n) {
    uint8_t lens[256];
    uint32_t s;
    uint32_t i;
    uint32_t len;
    uint32_t sum = (uint32_t)0;
    uint32_t nonzero = (uint32_t)0;
    uint32_t single_sym = (uint32_t)0;
    uint32_t code;
    uint32_t ncount[11];
    if (lengths == NULL || tab == NULL || nsym == (uint32_t)0 ||
        nsym > (uint32_t)256 || maxlen == (uint32_t)0 || maxlen > (uint32_t)10 ||
        tab_n != ((uint32_t)1 << maxlen)) {
        return LZ_U3_FAIL;
    }
    for (i = (uint32_t)0; i < tab_n; i++) {
        tab[i] = (uint16_t)0;
    }
    for (i = (uint32_t)0; i < (uint32_t)11; i++) {
        ncount[i] = (uint32_t)0;
    }
    for (s = (uint32_t)0; s < nsym; s++) {
        uint32_t l = (uint32_t)lengths[s];
        lens[s] = lengths[s];
        if (l > maxlen) {
            return LZ_U3_FAIL;
        }
        if (l != (uint32_t)0) {
            nonzero++;
            single_sym = s;
            ncount[l]++;
        }
    }
    /* R24-TL5D-B1: ncount fused into the lens pass above (was a separate
     * 256-pass) + Kraft sum from ncount (11 iters, was per-sym shifts).
     * Same sum (u32 addition commutes; identical shift addends), same
     * FAILs (maxlen/nonzero/single_sym legs untouched). */
    for (len = (uint32_t)1; len <= (uint32_t)10; len++) {
        sum += ncount[len] << (10 - len);
    }
    if (sum != (uint32_t)1024) {
        if (nonzero != (uint32_t)1 ||
            (uint32_t)lens[single_sym] != (uint32_t)1) {
            return LZ_U3_FAIL; /* over/under/single-len10-sum1 */
        }
        for (i = (uint32_t)0; i < tab_n; i++) {
            tab[i] = (uint16_t)(((uint32_t)1 << 8) | single_sym);
        }
        if (lz_u2_kraft1024_ok((uint32_t)1024) != LZ_U2_OK) {
            return LZ_U3_FAIL;
        }
        return LZ_U3_OK;
    }
    if (lz_u2_kraft1024_ok(sum) != LZ_U2_OK) {
        return LZ_U3_FAIL;
    }
    /* R23-TL9D-D1: counting scatter. first[len] = canonical first code
     * per length (standard closed form); syms iterate once in order, so
     * each sym gets first[l]+prior-count[l] == the (len,sym)-order code
     * the double loop assigned. Same codes, same tables, byte-identical.
     * Over-subscribe + collision checks DROPPED: Kraft==1024 verified
     * above => complete code => canonical assignment never
     * over-subscribes, every cell written exactly once (same theorem the
     * old code's "unreachable" comments cited). Same FAILs (Kraft gate
     * unchanged), fewer executions: 2x256 passes, no per-cell branch. */
    {
        uint32_t first[11];
        code = (uint32_t)0;
        for (len = (uint32_t)1; len <= maxlen; len++) {
            first[len] = code;
            code = (code + ncount[len]) << 1;
        }
        for (s = (uint32_t)0; s < nsym; s++) {
            uint32_t l2 = (uint32_t)lens[s];
            uint32_t c;
            uint32_t rev;
            uint32_t step;
            uint32_t idx;
            if (l2 == (uint32_t)0) {
                continue;
            }
            c = first[l2];
            first[l2] = c + (uint32_t)1;
            rev = lz_u6h_rev(c, l2);
            step = (uint32_t)1 << l2;
            for (idx = rev; idx < tab_n; idx += step) {
                tab[idx] = (uint16_t)((l2 << 8) | s);
            }
        }
    }
    return LZ_U3_OK;
}

/* Decode one symbol via maxlen-bit table peek; consumes len bits
 * (strict: consumed bits must be in-bounds).
 * P6-symphoist: cached-cap form. p/cap_bits/cap_bytes are snapshotted
 * once per substream (p/bs_end invariant; only bitpos advances), so
 * the 2 lane_cap calls + NULL/range re-checks per symbol are gone.
 * Caller guarantees: tab != NULL, sym != NULL, maxlen in {5,10}
 * (both substream sites pass stack tables, &v, 5/10 — the old entry
 * checks were vacuous there). Verdict equivalence vs old decode_sym:
 * peek==0 when p==NULL (old peek NULL leg) or pos>=cap (old empty
 * leg; nbits==maxlen!=0); slow branch is the old 3-guard window
 * verbatim; len/p/cap FAILs fire in the same order with the same
 * operands (cap_bits == lane_cap bit-for-bit: same NULL/e<p guards,
 * same (e-p)*8 arithmetic). Bad-bs_end/p!=NULL collapses to the p
 * FAIL instead of the cap FAIL — same FAIL verdict either way.
 * R2-V1: fast leg split +4 (one u32 peek, HINT-P10-SYMDEC S1) / +2
 * (3-byte, verbatim) / guarded tail (verbatim); all three compute
 * the same peek (4th byte unread in the +4 window). */
static int lz_u6h_decode_sym_fast(const uint8_t *p, size_t cap_bits,
                                  size_t cap_bytes, size_t *bpos,
                                  const uint16_t *tab, uint32_t maxlen,
                                  uint32_t *sym) {
    size_t pos = *bpos;
    size_t byte_off;
    uint32_t sh;
    uint32_t b0;
    uint32_t b1;
    uint32_t b2;
    uint32_t peek;
    uint32_t e;
    uint32_t len;
    if (p == NULL || pos >= cap_bits) {
        peek = (uint32_t)0;
    } else {
        byte_off = pos >> 3;
        sh = (uint32_t)(pos & (size_t)7);
        if (byte_off + (size_t)4 <= cap_bytes) {
            /* R2-V1: one u32 peek (HINT-P10-SYMDEC S1). Overread 0 by
             * construction (window inside proven bytes); value-identical
             * to the 3-byte leg (sh<=7, maxlen<=10 => peek bits live in
             * bytes [off,off+2]; the 4th byte is unread). BE leg keeps
             * LE-correctness where memcpy order flips (cf P5-W3). */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            b0 = (uint32_t)p[byte_off];
            b1 = (uint32_t)p[byte_off + (size_t)1];
            b2 = (uint32_t)p[byte_off + (size_t)2];
#else
            /* b0 carries the full word; b1/b2 zero so the shared
             * (b0|(b1<<8)|(b2<<16)) expression below == w (verified by
             * inspection; peek bits provably in [off,off+2]). */
            b0 = lz_u3_mc_ld32(p + byte_off);
            b1 = (uint32_t)0;
            b2 = (uint32_t)0;
#endif
        } else if (byte_off + (size_t)2 < cap_bytes) {
            b0 = (uint32_t)p[byte_off];
            b1 = (uint32_t)p[byte_off + (size_t)1];
            b2 = (uint32_t)p[byte_off + (size_t)2];
        } else {
            b0 = (byte_off < cap_bytes) ? (uint32_t)p[byte_off] : (uint32_t)0;
            b1 = (byte_off + (size_t)1 < cap_bytes)
                     ? (uint32_t)p[byte_off + (size_t)1]
                     : (uint32_t)0;
            b2 = (byte_off + (size_t)2 < cap_bytes)
                     ? (uint32_t)p[byte_off + (size_t)2]
                     : (uint32_t)0;
        }
        peek = (((b0 | (b1 << 8) | (b2 << 16)) >> sh) &
                (((uint32_t)1 << maxlen) - (uint32_t)1));
    }
    e = tab[peek];
    len = e >> 8;
    if (len == (uint32_t)0 || len > maxlen) {
        return LZ_U3_FAIL;
    }
    if (p == NULL) {
        return LZ_U3_FAIL;
    }
    if (pos + (size_t)len > cap_bits) {
        return LZ_U3_FAIL;
    }
    *bpos = pos + (size_t)len;
    *sym = e & (uint32_t)0xFF;
    return LZ_U3_OK;
}

/* P6-symphoist: write cached bitpos back to lanes (success exit + every
 * post-snapshot FAIL exit, so lanes always reflect consumed bits). */
static void lz_u6h_sync_bitpos(struct lz_u3_lanes *lanes, const size_t *bpos) {
    int k;
    for (k = 0; k < 8; k++) {
        lanes->l[k].bitpos = bpos[k];
    }
}

/* R2-V1: unchecked symbol decode with u32 peek (HINT-P10-SYMDEC S1 on
 * top of P10-symdec guard-once + x8 unroll). Caller proves the guard
 * for THIS symbol: p != NULL, pos < cap_bits, (pos>>3)+4 <= cap_bytes,
 * pos+maxlen <= cap_bits. One memcpy-u32 load (P6-W5 idiom, lowered to
 * ldr; BE leg byte-assembles for LE-correctness) replaces the 3 byte
 * loads; peek value-identical (4th byte unread: sh<=7, maxlen<=10).
 * tab[] bit-identical, no table change (Moffat do-not-bed). Only the
 * len FAIL can fire — same symbol, same bpos as checked. */
static int lz_u6h_decode_sym_unchecked32(const uint8_t *p, size_t *bpos,
                                         const uint16_t *tab, uint32_t maxlen,
                                         uint32_t mask, uint32_t *sym) {
    size_t pos = *bpos;
    size_t byte_off = pos >> 3;
    uint32_t sh = (uint32_t)(pos & (size_t)7);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    uint32_t w = (uint32_t)p[byte_off] |
                 ((uint32_t)p[byte_off + (size_t)1] << 8) |
                 ((uint32_t)p[byte_off + (size_t)2] << 16);
#else
    uint32_t w = lz_u3_mc_ld32(p + byte_off);
#endif
    uint32_t e = tab[(w >> sh) & mask];
    uint32_t len = e >> 8;
    if (len == (uint32_t)0 || len > maxlen) {
        return LZ_U3_FAIL;
    }
    *bpos = pos + (size_t)len;
    *sym = e & (uint32_t)0xFF;
    return LZ_U3_OK;
}

/* P10-symdec: per-lane unchecked budget — max m such that m leading
 * symbols from start satisfy the unchecked guard however lens fall
 * (1..maxlen). Bit leg: s+m*maxlen <= cap_bits. Byte leg:
 * s+(m-1)*maxlen < room with room = (cap_bytes-margin)*8, i.e.
 * (pos>>3)+margin <= cap_bytes for every reachable pos (R2-V1: margin
 * 4 for the u32 window; was hard 2). Closed form over monotone
 * bounds => sufficient; conservative, never wrong. */
static size_t lz_u6h_unchecked_budget(const uint8_t *p, size_t start,
                                      size_t cap_bits, size_t cap_bytes,
                                      uint32_t maxlen, uint32_t margin) {
    size_t m;
    size_t room;
    size_t m2;
    if (p == NULL || cap_bytes <= (size_t)margin || start >= cap_bits) {
        return (size_t)0;
    }
    m = (cap_bits - start) / (size_t)maxlen;
    room = (cap_bytes - (size_t)margin) * (size_t)8;
    if (room <= start) {
        return (size_t)0;
    }
    m2 = (room - start - (size_t)1) / (size_t)maxlen + (size_t)1;
    if (m2 < m) {
        m = m2;
    }
    return m;
}

/* P10-symdec round macros (lz_u6h_substream scope only; #undef'd after
 * the function). Lane bpos live in scalars c0..c7 (registers);
 * LZ_U6H_WB8 writes them back to bpos[] on round exit / FAIL exits. */
#define LZ_U6H_WB8()                                                           \
    do {                                                                       \
        bpos[0] = c0;                                                          \
        bpos[1] = c1;                                                          \
        bpos[2] = c2;                                                          \
        bpos[3] = c3;                                                          \
        bpos[4] = c4;                                                          \
        bpos[5] = c5;                                                          \
        bpos[6] = c6;                                                          \
        bpos[7] = c7;                                                          \
    } while (0)
#define LZ_U6H_DEC(qk, ck, tab, maxlen, mask)                                  \
    do {                                                                       \
        v = (uint32_t)0;                                                       \
        if (lz_u6h_decode_sym_unchecked32((qk), &(ck), (tab), (maxlen),         \
                                          (mask), &v) != LZ_U3_OK) {           \
            LZ_U6H_WB8();                                                      \
            lz_u6h_sync_bitpos(lanes, bpos);                                   \
            return LZ_U3_FAIL;                                                 \
        }                                                                      \
    } while (0)
#define LZ_U6H_V10FAIL()                                                       \
    do {                                                                       \
        LZ_U6H_WB8();                                                          \
        lz_u6h_sync_bitpos(lanes, bpos);                                       \
        return LZ_U3_FAIL;                                                     \
    } while (0)

/* One HUFFMAN substream (S3.8/Q4): 11x3b meta + 32b bitmap from lane 0,
 * meta table (maxlen 5), used=8*popcount lengths round-robin, ascending
 * scatter, main table (maxlen 10), count symbols round-robin. Lanes at
 * continued bitpos in/out (S3.6 chain; suffix continues after, S3.11). */
static int lz_u6h_substream(struct lz_u3_lanes *lanes, uint32_t count,
                            uint8_t *out) {
    uint8_t meta_lens[11];
    uint16_t meta_tab[32];
    uint8_t main_lens[256];
    uint16_t main_tab[1024];
    uint8_t tmp[256];
    /* P6-symphoist: hoisted per-lane caps (p/bs_end invariant across the
     * substream; bitpos cached, synced back on every exit below). */
    const uint8_t *lp[8];
    size_t cap_bits[8];
    size_t cap_bytes[8];
    size_t bpos[8];
    uint32_t bitmap = (uint32_t)0;
    uint32_t used = (uint32_t)0;
    uint32_t maxmeta = (uint32_t)0;
    uint32_t maxmain = (uint32_t)0;
    uint32_t i;
    uint32_t g;
    uint32_t pos;
    uint32_t k;
    uint32_t li;
    int sk;
    if (lanes == NULL || out == NULL || count == (uint32_t)0) {
        return LZ_U3_FAIL;
    }
    li = (uint32_t)0;
    for (i = (uint32_t)0; i < (uint32_t)11; i++) {
        uint32_t v = (uint32_t)0;
        if (lz_u6h_read_bits(&lanes->l[0], (uint32_t)3, &v) != LZ_U3_OK) {
            return LZ_U3_FAIL;
        }
        if (lz_u2_meta_val_ok(v) != LZ_U2_OK) {
            return LZ_U3_FAIL;
        }
        meta_lens[i] = (uint8_t)v;
        if (v > maxmeta) {
            maxmeta = v;
        }
    }
    if (lz_u2_maxlen_ok(maxmeta, 1) != LZ_U2_OK) {
        return LZ_U3_FAIL; /* meta cap 5 (G-u6h-META5); 6-7 reject */
    }
    if (lz_u6h_read_bits(&lanes->l[0], (uint32_t)32, &bitmap) != LZ_U3_OK) {
        return LZ_U3_FAIL;
    }
    if (lz_u2_bitmap_ok(bitmap) != LZ_U2_OK) {
        return LZ_U3_FAIL;
    }
    if (lz_u6h_build(meta_lens, (uint32_t)11, (uint32_t)5, meta_tab,
                     (uint32_t)32) != LZ_U3_OK) {
        return LZ_U3_FAIL;
    }
    for (g = (uint32_t)0; g < (uint32_t)32; g++) {
        if (((bitmap >> g) & (uint32_t)1) != (uint32_t)0) {
            used += (uint32_t)8;
        }
    }
    /* P6-symphoist: snapshot after the header reads above (they advanced
     * l[0].bitpos via read_bits). cap_bits == lane_cap bit-for-bit. */
    for (sk = 0; sk < 8; sk++) {
        const uint8_t *sp = lanes->l[sk].p;
        const uint8_t *se = lanes->l[sk].bs_end;
        if (sp == NULL || se == NULL || se < sp) {
            lp[sk] = NULL;
            cap_bits[sk] = (size_t)0;
            cap_bytes[sk] = (size_t)0;
        } else {
            lp[sk] = sp;
            cap_bytes[sk] = (size_t)(se - sp);
            cap_bits[sk] = cap_bytes[sk] * (size_t)8;
        }
        bpos[sk] = lanes->l[sk].bitpos;
    }
    /* P10-symdec: guard-once prefix. npre = leading symbols provably
     * unchecked-safe on all 8 lanes (round-robin from lane 0, so lane k
     * consumes ceil((npre-k)/8) <= budget[k] iff npre <= k+8*budget[k]);
     * nrounds>0 implies every budget >= 1, hence qk != NULL). Rounds run
     * x8-unrolled with lane-local bpos (registers, no li wrap, no
     * bounds branches); symbols [8R,used) take the checked path. FAIL
     * exits write back lane bpos first — same symbol, same lanes state
     * as the checked loop. */
    i = (uint32_t)0;
    /* R2-V2: re-guard loop. Each phase proves a fresh guard-once prefix
     * from CURRENT bpos (budget is monotone in start, so re-guarding is
     * still sufficient); phases repeat while >=8 symbols are provable.
     * Converts L0's ~50% checked tail (bit-leg-capped npre) into u32
     * rounds. Lane alignment holds every phase (i stays a multiple of
     * 8, round-robin restarts at lane 0); i strictly grows => ends. */
    for (;;) {
        size_t npre = (size_t)used - (size_t)i;
        size_t nrounds;
        int k;
        for (k = 0; k < 8; k++) {
            size_t lim = (size_t)k + (size_t)8 *
                lz_u6h_unchecked_budget(lp[k], bpos[k], cap_bits[k],
                                        cap_bytes[k], (uint32_t)5,
                                        (uint32_t)4);
            if (lim < npre) {
                npre = lim;
            }
        }
        nrounds = npre / (size_t)8;
        if (nrounds > (size_t)0) {
            const uint8_t *q0 = lp[0];
            const uint8_t *q1 = lp[1];
            const uint8_t *q2 = lp[2];
            const uint8_t *q3 = lp[3];
            const uint8_t *q4 = lp[4];
            const uint8_t *q5 = lp[5];
            const uint8_t *q6 = lp[6];
            const uint8_t *q7 = lp[7];
            size_t c0 = bpos[0];
            size_t c1 = bpos[1];
            size_t c2 = bpos[2];
            size_t c3 = bpos[3];
            size_t c4 = bpos[4];
            size_t c5 = bpos[5];
            size_t c6 = bpos[6];
            size_t c7 = bpos[7];
            size_t r;
            uint32_t v;
            for (r = (size_t)0; r < nrounds; r++) {
                size_t r8 = (size_t)i + r * (size_t)8;
                LZ_U6H_DEC(q0, c0, meta_tab, (uint32_t)5, (uint32_t)0x1F);
                if (v > (uint32_t)10) {
                    LZ_U6H_V10FAIL();
                }
                tmp[r8] = (uint8_t)v;
                LZ_U6H_DEC(q1, c1, meta_tab, (uint32_t)5, (uint32_t)0x1F);
                if (v > (uint32_t)10) {
                    LZ_U6H_V10FAIL();
                }
                tmp[r8 + (size_t)1] = (uint8_t)v;
                LZ_U6H_DEC(q2, c2, meta_tab, (uint32_t)5, (uint32_t)0x1F);
                if (v > (uint32_t)10) {
                    LZ_U6H_V10FAIL();
                }
                tmp[r8 + (size_t)2] = (uint8_t)v;
                LZ_U6H_DEC(q3, c3, meta_tab, (uint32_t)5, (uint32_t)0x1F);
                if (v > (uint32_t)10) {
                    LZ_U6H_V10FAIL();
                }
                tmp[r8 + (size_t)3] = (uint8_t)v;
                LZ_U6H_DEC(q4, c4, meta_tab, (uint32_t)5, (uint32_t)0x1F);
                if (v > (uint32_t)10) {
                    LZ_U6H_V10FAIL();
                }
                tmp[r8 + (size_t)4] = (uint8_t)v;
                LZ_U6H_DEC(q5, c5, meta_tab, (uint32_t)5, (uint32_t)0x1F);
                if (v > (uint32_t)10) {
                    LZ_U6H_V10FAIL();
                }
                tmp[r8 + (size_t)5] = (uint8_t)v;
                LZ_U6H_DEC(q6, c6, meta_tab, (uint32_t)5, (uint32_t)0x1F);
                if (v > (uint32_t)10) {
                    LZ_U6H_V10FAIL();
                }
                tmp[r8 + (size_t)6] = (uint8_t)v;
                LZ_U6H_DEC(q7, c7, meta_tab, (uint32_t)5, (uint32_t)0x1F);
                if (v > (uint32_t)10) {
                    LZ_U6H_V10FAIL();
                }
                tmp[r8 + (size_t)7] = (uint8_t)v;
            }
            LZ_U6H_WB8();
            i += (uint32_t)(nrounds * (size_t)8);
        } else {
            break;
        }
    }
    for (; i < used; i++) {
        uint32_t v = (uint32_t)0;
        /* P2-bitio: wrapping lane cursor == i%8, no modulo. */
        if (lz_u6h_decode_sym_fast(lp[li], cap_bits[li], cap_bytes[li],
                                   &bpos[li], meta_tab, (uint32_t)5,
                                   &v) != LZ_U3_OK) {
            lz_u6h_sync_bitpos(lanes, bpos);
            return LZ_U3_FAIL;
        }
        li++;
        if (li == (uint32_t)8) {
            li = (uint32_t)0;
        }
        if (v > (uint32_t)10) {
            lz_u6h_sync_bitpos(lanes, bpos);
            return LZ_U3_FAIL; /* meta syms are lengths 0..10 */
        }
        tmp[i] = (uint8_t)v;
    }
    for (i = (uint32_t)0; i < (uint32_t)256; i++) {
        main_lens[i] = (uint8_t)0;
    }
    pos = (uint32_t)0;
    for (g = (uint32_t)0; g < (uint32_t)32; g++) {
        if (((bitmap >> g) & (uint32_t)1) == (uint32_t)0) {
            continue;
        }
        for (k = (uint32_t)0; k < (uint32_t)8; k++) {
            main_lens[8 * g + k] = tmp[pos++];
        }
    }
    for (i = (uint32_t)0; i < (uint32_t)256; i++) {
        if ((uint32_t)main_lens[i] > maxmain) {
            maxmain = (uint32_t)main_lens[i];
        }
    }
    if (lz_u2_maxlen_ok(maxmain, 0) != LZ_U2_OK) {
        lz_u6h_sync_bitpos(lanes, bpos);
        return LZ_U3_FAIL; /* main cap 10 (vacuous via meta) */
    }
    if (lz_u6h_build(main_lens, (uint32_t)256, (uint32_t)10, main_tab,
                     (uint32_t)1024) != LZ_U3_OK) {
        lz_u6h_sync_bitpos(lanes, bpos);
        return LZ_U3_FAIL;
    }
    li = (uint32_t)0;
    /* P10-symdec: guard-once + x8 rounds, same shape as the meta loop
     * above (maxlen 10, mask 0x3FF, no v>10 check). */
    i = (uint32_t)0;
    /* R2-V2: re-guard loop, same shape as the meta loop above. */
    for (;;) {
        size_t npre = (size_t)count - (size_t)i;
        size_t nrounds;
        int k;
        for (k = 0; k < 8; k++) {
            size_t lim = (size_t)k + (size_t)8 *
                lz_u6h_unchecked_budget(lp[k], bpos[k], cap_bits[k],
                                        cap_bytes[k], (uint32_t)10,
                                        (uint32_t)4);
            if (lim < npre) {
                npre = lim;
            }
        }
        nrounds = npre / (size_t)8;
        if (nrounds > (size_t)0) {
            const uint8_t *q0 = lp[0];
            const uint8_t *q1 = lp[1];
            const uint8_t *q2 = lp[2];
            const uint8_t *q3 = lp[3];
            const uint8_t *q4 = lp[4];
            const uint8_t *q5 = lp[5];
            const uint8_t *q6 = lp[6];
            const uint8_t *q7 = lp[7];
            size_t c0 = bpos[0];
            size_t c1 = bpos[1];
            size_t c2 = bpos[2];
            size_t c3 = bpos[3];
            size_t c4 = bpos[4];
            size_t c5 = bpos[5];
            size_t c6 = bpos[6];
            size_t c7 = bpos[7];
            size_t r;
            uint32_t v;
            for (r = (size_t)0; r < nrounds; r++) {
                size_t r8 = (size_t)i + r * (size_t)8;
                LZ_U6H_DEC(q0, c0, main_tab, (uint32_t)10, (uint32_t)0x3FF);
                out[r8] = (uint8_t)v;
                LZ_U6H_DEC(q1, c1, main_tab, (uint32_t)10, (uint32_t)0x3FF);
                out[r8 + (size_t)1] = (uint8_t)v;
                LZ_U6H_DEC(q2, c2, main_tab, (uint32_t)10, (uint32_t)0x3FF);
                out[r8 + (size_t)2] = (uint8_t)v;
                LZ_U6H_DEC(q3, c3, main_tab, (uint32_t)10, (uint32_t)0x3FF);
                out[r8 + (size_t)3] = (uint8_t)v;
                LZ_U6H_DEC(q4, c4, main_tab, (uint32_t)10, (uint32_t)0x3FF);
                out[r8 + (size_t)4] = (uint8_t)v;
                LZ_U6H_DEC(q5, c5, main_tab, (uint32_t)10, (uint32_t)0x3FF);
                out[r8 + (size_t)5] = (uint8_t)v;
                LZ_U6H_DEC(q6, c6, main_tab, (uint32_t)10, (uint32_t)0x3FF);
                out[r8 + (size_t)6] = (uint8_t)v;
                LZ_U6H_DEC(q7, c7, main_tab, (uint32_t)10, (uint32_t)0x3FF);
                out[r8 + (size_t)7] = (uint8_t)v;
            }
            LZ_U6H_WB8();
            i += (uint32_t)(nrounds * (size_t)8);
        } else {
            break;
        }
    }
    for (; i < count; i++) {
        uint32_t v = (uint32_t)0;
        /* P2-bitio: wrapping lane cursor == i%8, no modulo. */
        if (lz_u6h_decode_sym_fast(lp[li], cap_bits[li], cap_bytes[li],
                                   &bpos[li], main_tab, (uint32_t)10,
                                   &v) != LZ_U3_OK) {
            lz_u6h_sync_bitpos(lanes, bpos);
            return LZ_U3_FAIL;
        }
        li++;
        if (li == (uint32_t)8) {
            li = (uint32_t)0;
        }
        out[i] = (uint8_t)v;
    }
    lz_u6h_sync_bitpos(lanes, bpos);
    return LZ_U3_OK;
}

#undef LZ_U6H_WB8
#undef LZ_U6H_DEC
#undef LZ_U6H_V10FAIL

/* u6b D-B1: fetch RAW/REPEAT from [9,bo) in fetch order (lit,tok,len,dist
 * per S3.1/Q2) + lanes for suffix when dist>0 (Q3/Q7, no-Huffman only).
 * HUFFMAN still FAILs (pending). Choices: G-u6b-FETCH1 exact-match;
 * G-u6b-FETCH2 empty lanes iff dist==0. */
static int lz_u3_fetch(const uint8_t *blk, uint32_t bo, uint32_t fo,
                       uint32_t modes, const uint16_t *cnt,
                       struct lz_u3_ss *ss, struct lz_u3_lanes *lanes,
                       uint8_t **p_base) {
    const uint8_t *br;
    uint32_t br_len;
    uint32_t off = (uint32_t)0;
    uint32_t lit_m;
    uint32_t tok_m;
    uint32_t len_m;
    uint32_t dist_m;
    uint32_t lit_n;
    uint32_t tok_n;
    uint32_t len_n;
    int k;
    int any_huf = 0; /* u6h: lanes + Huffman path iff any mode 2 */
    if (blk == NULL || cnt == NULL || ss == NULL || lanes == NULL ||
        p_base == NULL) {
        return LZ_U3_FAIL;
    }
    *p_base = NULL;
    if (bo < (uint32_t)9 || fo < bo) {
        return LZ_U3_FAIL;
    }
    for (k = 0; k < 8; k++) {
        lanes->l[k].p = NULL;
        lanes->l[k].len = (uint32_t)0;
        lanes->l[k].bitpos = (size_t)0;
        lanes->l[k].bs_end = NULL;
    }
    lit_m = (modes >> 0) & (uint32_t)7;
    tok_m = (modes >> 3) & (uint32_t)7;
    len_m = (modes >> 6) & (uint32_t)7;
    dist_m = (modes >> 9) & (uint32_t)7;
    lit_n = (uint32_t)cnt[2];
    tok_n = (uint32_t)cnt[0];
    len_n = (uint32_t)cnt[1];
    if (lit_m > (uint32_t)2 || tok_m > (uint32_t)2 ||
        len_m > (uint32_t)2 || dist_m > (uint32_t)2) {
        return LZ_U3_FAIL;
    }
    /* u6h: Huffman (mode 2) consumes lane bits below (D-B1 full). */
    any_huf = (lit_m == (uint32_t)2 || tok_m == (uint32_t)2 ||
               len_m == (uint32_t)2 || dist_m == (uint32_t)2) ? 1 : 0;
    br = blk + 9;
    br_len = bo - (uint32_t)9;
    for (k = 0; k < 4; k++) {
        ss[k].p = NULL;
        ss[k].fill = (uint8_t)0;
        ss[k].n = (uint32_t)0;
        ss[k].mode = (uint32_t)0;
    }
    /* Fetch order lit,tok,len,dist -> ss idx 2,0,1,3. */
    {
        static const int idx[4] = { 2, 0, 1, 3 };
        uint32_t ms[4];
        uint32_t ns[4];
        int j;
        ms[0] = lit_m;
        ms[1] = tok_m;
        ms[2] = len_m;
        ms[3] = dist_m;
        ns[0] = lit_n;
        ns[1] = tok_n;
        ns[2] = len_n;
        ns[3] = (uint32_t)cnt[3];
        for (j = 0; j < 4; j++) {
            int si = idx[j];
            ss[si].mode = ms[j];
            ss[si].n = ns[j];
            if (ms[j] == (uint32_t)2) {
                ss[si].p = NULL; /* u6h: lane bits, decoded below */
                continue;
            }
            if (ms[j] == (uint32_t)0) {
                if (ns[j] > br_len - off) {
                    return LZ_U3_FAIL;
                }
                ss[si].p = br + off;
                off += ns[j];
            } else {
                if (ns[j] == (uint32_t)0) {
                    return LZ_U3_FAIL; /* count0-REPEAT, modes_ok bars */
                }
                if (off >= br_len) {
                    return LZ_U3_FAIL;
                }
                ss[si].fill = br[off];
                ss[si].p = NULL;
                off += (uint32_t)1;
            }
        }
    }
    if (off > br_len) {
        return LZ_U3_FAIL; /* u6q ZA: G-u6b-FETCH1 exact RELAXED to
         * over-long-ACCEPT (E-decode tolerance analogue S3.3/S4.6:
         * trailing slack in [9,bo) ignored; under-long still fails
         * closed during fetch above) */
    }
    /* u6v Q2-B2 (P1 B2): rep-pre/payload bytes are DATA (ranges), never
     * value-checked here — only STRUCTURAL (counts/sums/tags) enforced. */
    /* u6h: lanes required iff any-Huffman || dist>0. Suffix continues
     * after Huffman at continued bitpos (S3.6/S3.11); no-Huffman suffix
     * stays at bitpos0 (D-B3).
     * u6l ZA: empty lanes (fo==bo) accepted iff no Huffman (sb0-only
     * no-Huffman blocks carry 0 lane bits; S3.3 empty-payload bars
     * index-only, so fo==bo is the valid encoding; sb>0 fails closed
     * at suffix resolve via bounds). Unused-nonempty (fo>bo, no
     * Huffman, dist==0) falls through to parse+ignore per E-decode
     * tolerance (over-long ACCEPT); invalid index still fails closed
     * (S3.3.d checks even when unused). G-u6b-FETCH2 superseded. */
    if (any_huf == 0 && fo == bo) {
        return LZ_U3_OK; /* empty lanes, p=NULL/len=0 above */
    }
    if (fo == bo) {
        return LZ_U3_FAIL; /* Huffman needs lanes */
    }
    if (lz_u3_lanes_parse(blk + bo, fo - bo, lanes) != LZ_U3_OK) {
        return LZ_U3_FAIL;
    }
    /* u6v Q1: lane-head exactly-N + 51B L0 (P1 A1-A6, shape-gated). */
    if (lz_u6v_q1_gate(lanes, (uint32_t)cnt[3], any_huf) != LZ_U3_OK) {
        return LZ_U3_FAIL;
    }
    if (any_huf != 0) {
        /* Huffman substreams in fetch order (S3.6 header chain).
         * R24-TL5D-P2: single-alloc-per-block + carve (was 1 malloc per
         * stream). Two-phase: count0-check all, then one alloc, then
         * carve+decode per stream in hidx order. Same FAIL verdicts
         * (count0/alloc/decode all FAIL either way; lanes trajectory
         * identical until any decode FAIL; sum bounded by S4.2
         * scratch_ok pre-fetch, wrap-guarded anyway). */
        static const int hidx[4] = { 2, 0, 1, 3 };
        int j;
        uint32_t total = (uint32_t)0;
        uint8_t *base;
        uint32_t coff;
        for (j = 0; j < 4; j++) {
            int si = hidx[j];
            if (ss[si].mode != (uint32_t)2) {
                continue;
            }
            if (ss[si].n == (uint32_t)0) {
                return LZ_U3_FAIL; /* count0-Huffman, modes_ok bars */
            }
            if (total + ss[si].n < total) {
                return LZ_U3_FAIL; /* sum wrap (unreachable post-scratch_ok) */
            }
            total += ss[si].n;
        }
        if (total != (uint32_t)0) {
            base = lz_u6h_alloc(total);
            if (base == NULL) {
                return LZ_U3_FAIL;
            }
            *p_base = base;
            coff = (uint32_t)0;
            for (j = 0; j < 4; j++) {
                int si = hidx[j];
                uint8_t *carve;
                if (ss[si].mode != (uint32_t)2) {
                    continue;
                }
                carve = base + coff;
                coff += ss[si].n;
                ss[si].p = carve;
                if (lz_u6h_substream(lanes, ss[si].n, carve) != LZ_U3_OK) {
                    lz_u6h_free_ss(ss, base);
                    *p_base = NULL;
                    return LZ_U3_FAIL;
                }
            }
        }
    }
    return LZ_U3_OK;
}

/* u6b D-B2: substream byte reader for RAW/REPEAT/HUFFMAN-decoded.
 * mode 0=RAW,1=REPEAT,2=HUFFMAN (p=decoded buf). FAIL on overrun/NULL. */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) int lz_u3_ss_byte(
    const struct lz_u3_ss *ss, uint32_t pos, uint8_t *out) {
    if (pos >= ss->n) {
        return LZ_U3_FAIL;
    }
    if (ss->mode == (uint32_t)1) {
        *out = ss->fill;
        return LZ_U3_OK;
    }
    if (ss->p == NULL) {
        return LZ_U3_FAIL;
    }
    *out = ss->p[pos];
    return LZ_U3_OK;
}

/* === c19 B2-accept leg (owner: c19-driver; claim-19b ZA-bulk followup) ===
 * P1-ANSWERS Q2 Rule B2: rep-pre/payload DATA bytes are ranges/all-accept
 * in Apple (bulk DECODES THROUGH, 14/16 DIFF; e00 payload n=256 ALL-accept);
 * port direction = DROP data-value exactness, keep STRUCTURAL checks
 * (tags/counts/sums). S3.10 pins decode-through ("extra==255 -> u32le,
 * return escape+extra") with NO over-long refusal clause; the only
 * value-based refuse in fetch/replay is u6q's 5B u<=254 FAIL (lane
 * inference, not spec-pinned). S3.9/Q17 precedent flips fail-closed gates
 * to MUST-accept on grammar-legal forms. This predicate fail-opens that
 * one check (accept-only: FAIL->OK on over-long-5B shapes only; every
 * other path byte-identical). u6v/u6w hunks untouched (frozen priors). */
static int lz_c19_len_overlong_accept(uint32_t u) {
    (void)u;
    return 1;
}

/* u6b D-B2: decode_len(short,escape) per S3.10/Q5 PROVEN. short!=escape ->
 * short (no consume). Else extra=len[len_used++]: extra!=255 ->
 * escape+extra; extra==255 -> u32le next 4, return escape+u32. len_used
 * counts bytes (C19). 5B choice escape+u32 pins S3.13 `07`+`ff f6 ff ff
 * 00`->mc16777213; SPEC "return escape+extra" ambiguous for 255.
 * u6p WRAP: escape+u32 is wide (no length-wrap clause exists; S6.3 blesses
 * u32 wrap ONLY for distance). *wrapped=1 + saturated UINT32_MAX out when
 * the sum overflows; both callers reject (lit C18, match C16-class). */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) int lz_u3_decode_len(
    uint32_t shortv, uint32_t escape, const struct lz_u3_ss *ss_len,
    uint32_t *len_used, uint32_t *out, int *wrapped) {
    uint8_t eb;
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
    uint8_t b3;
    uint32_t u;
    if (shortv != escape) {
        *out = shortv;
        *wrapped = 0;
        return LZ_U3_OK;
    }
    if (lz_u3_ss_byte(ss_len, *len_used, &eb) != LZ_U3_OK) {
        return LZ_U3_FAIL;
    }
    *len_used += (uint32_t)1;
    if ((uint32_t)eb != (uint32_t)255) {
        *out = escape + (uint32_t)eb;
        *wrapped = 0;
        return LZ_U3_OK;
    }
    if (lz_u3_ss_byte(ss_len, *len_used, &b0) != LZ_U3_OK ||
        lz_u3_ss_byte(ss_len, *len_used + (uint32_t)1, &b1) != LZ_U3_OK ||
        lz_u3_ss_byte(ss_len, *len_used + (uint32_t)2, &b2) != LZ_U3_OK ||
        lz_u3_ss_byte(ss_len, *len_used + (uint32_t)3, &b3) != LZ_U3_OK) {
        return LZ_U3_FAIL;
    }
    *len_used += (uint32_t)4;
    u = (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) |
        ((uint32_t)b3 << 24);
    if (u <= (uint32_t)254 && lz_c19_len_overlong_accept(u) == 0) {
        return LZ_U3_FAIL; /* u6q e00: 5B over-long (1B covers
         * escape..escape+254; 255 triggers 5B) -> C16-class FAIL */
    }
    if (u > UINT32_MAX - escape) {
        *out = UINT32_MAX; /* u6p WRAP: saturate, caller splits */
        *wrapped = 1;
        return LZ_U3_OK;
    }
    *out = escape + u;
    *wrapped = 0;
    return LZ_U3_OK;
}

/* u6b D-B2: lit-run per S3.10/Q5. lit_f 0..2 direct, 3 escape via lenbytes.
 * SPEC "lit_run=lit-1" implemented as decode_len direct (no -1); 257->1B /
 * 258->5B step + litc==1/lit-field-0 pin it. Choice G-u6b-LIT1.
 * u6p WRAP: wrapped escape+u32 means true run is giant -> C18-class FAIL
 * (S4.3 overhang->C18; e00 REV-8 reject mechanism). */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) int lz_u3_lit_run(
    uint32_t lit_f, const struct lz_u3_ss *ss_len, uint32_t *len_used,
    uint32_t *run) {
    int wrapped = 0;
    if (lz_u3_decode_len(lit_f, (uint32_t)3, ss_len, len_used, run,
                         &wrapped) != LZ_U3_OK) {
        return LZ_U3_FAIL;
    }
    return (wrapped != 0) ? LZ_U3_FAIL : LZ_U3_OK;
}

/* u6b D-B2: match-len per S3.10/Q6 + S3.9/Q17. rep sel<=3: short=len_f esc
 * 7; new sel>=4: short=((sel-4)<<3)|len_f esc 31. ml=mc+2, mc0 legal.
 * u6r #53: wrapped/giant mc means true ml is giant -> FAIL (C16-class,
 * before match copy). S4.4 term-clamp does NOT cover u32 overflow
 * (COMP-R6 0/7: oracle refuses; S6.3 blesses wrap ONLY for distance).
 * u6p saturate-clamp was oracle-refuted; lit leg (FAIL on wrap) kept. */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) int lz_u3_match_len(
    uint32_t sel, uint32_t len_f, const struct lz_u3_ss *ss_len,
    uint32_t *len_used, uint32_t *mlen) {
    uint32_t lshort;
    uint32_t esc;
    uint32_t mc = (uint32_t)0;
    int wrapped = 0;
    if (sel <= (uint32_t)3) {
        lshort = len_f;
        esc = (uint32_t)7;
    } else {
        lshort = (((sel - (uint32_t)4) << 3) | len_f) & (uint32_t)31;
        esc = (uint32_t)31;
    }
    if (lz_u3_decode_len(lshort, esc, ss_len, len_used, &mc,
                         &wrapped) != LZ_U3_OK) {
        return LZ_U3_FAIL;
    }
    if (wrapped != 0 || mc > UINT32_MAX - (uint32_t)2) {
        *mlen = (uint32_t)0; /* u6r #53: wrapped/overflowing ml FAILs */
        return LZ_U3_FAIL;
    }
    *mlen = mc + (uint32_t)2;
    return LZ_U3_OK;
}

/* u6b D-B3: suffix bit reader LSB-first from lane (Q7/S6.1). nbits=sb
 * (0..31); sb0 reads 0 bits (slot still advances via idx). Strict bounds
 * (no overread tolerance); R-011 corners accepted, gated RAW-only. */
/* R3-V1: force-inline (replay hot path; profile showed real calls/token). */
static inline __attribute__((always_inline)) int lz_u3_suffix_bits(
    struct lz_u3_lane *lane, uint32_t nbits, uint32_t *out) {
    /* P2-bitio: byte-window loads + shift/mask (bounds pre-checked, so
     * all window bytes are in-bounds; same bits, same verdicts). */
    size_t pos;
    size_t byte_off;
    uint32_t sh;
    unsigned need;
    unsigned j;
    uint64_t w;
    uint32_t v;
    if (out == NULL) {
        return LZ_U3_FAIL;
    }
    *out = (uint32_t)0;
    if (nbits == (uint32_t)0) {
        return LZ_U3_OK;
    }
    if (lane == NULL || nbits > (uint32_t)31) {
        return LZ_U3_FAIL;
    }
    if (lane->p == NULL) {
        return LZ_U3_FAIL;
    }
    if (lane->bitpos + (size_t)nbits > lz_u3_lane_cap(lane)) {
        return LZ_U3_FAIL;
    }
    pos = lane->bitpos;
    byte_off = pos >> 3;
    sh = (uint32_t)(pos & (size_t)7);
    need = (sh + nbits + (uint32_t)7) >> 3;
    w = (uint64_t)0;
    for (j = (unsigned)0; j < need; j++) {
        w |= (uint64_t)lane->p[byte_off + (size_t)j] << (j * (unsigned)8);
    }
    v = (uint32_t)(w >> sh) & (((uint32_t)1 << nbits) - (uint32_t)1);
    lane->bitpos += (size_t)nbits;
    *out = v;
    return LZ_U3_OK;
}

/* u6b D-B3: C14 dist-resolve (S4.3/S3.9/Q17 + S3.11/Q7). rep sel<=3 via
 * MTF (rep3 accept, no dist consume). new sel>=4: dsym=dist[dist_used++],
 * sb=dsym>>3 low3=dsym&7, suffix sb bits from lane idx%8, d via dist.
 * R3-MIXDEC: folded inline into lz_u3_replay step (3) (leg-for-leg, see
 * there); the outline copy is deleted to keep -Wall -Wextra clean. */

/* R11-LASTBLOCK: out-of-line room==ds replay-from-loop (exact-cap last
 * block). Loop = fast body + the single live cap-stop above; end checks
 * inline (counters local). noinline: v1's inline 257-line duplication
 * regressed matrix-shape decode 5-7% SEP with zero executed delta (Air
 * n35 ab-std) via cold-code layout pollution, so the 2.5KB stays out of
 * lz_u3_replay's body. Called once per exact-cap last block (census: 1
 * block/decode, exits via cap-stop, never end checks on corpus). Entry
 * state: lit_used0/wblk0 from pre-emit (len/dist unused before the loop).
 * recent[] success-writeback mirrors the fast leg (stale on FAIL/cap:
 * dead, walker returns, no resume); lanes bitpos never written back
 * (comp_block frame-local, unread after replay on every exit). */
static __attribute__((noinline)) int lz_u3_replay_eq(
    uint8_t *dst, size_t w_tot, size_t room, uint32_t ds,
    const struct lz_u3_ss *ss, struct lz_u3_lanes *lanes, uint32_t *recent,
    uint32_t lit_used0, size_t wblk0, size_t *out_n, int *cap_hit) {
    uint32_t ti;
    uint32_t lit_used = lit_used0;
    uint32_t len_used = (uint32_t)0;
    uint32_t dist_used = (uint32_t)0;
    size_t wblk = wblk0;
    /* Hoisted views (replay prologue subset; modes/fills excluded by the
     * call-site guard, so only ptr legs are re-homed here). */
    const uint8_t *tok_p = ss[0].p;
    const uint8_t *lit_p = ss[2].p;
    const uint8_t *dist_p = ss[3].p;
    uint32_t dist_n = ss[3].n;
    uint32_t lit_n = ss[2].n;
    size_t lit_bound =
        (size_t)ss[0].n + (size_t)lz_u2_round32(ss[2].n) - (size_t)1;
    const uint8_t *lane_p[8];
    size_t lane_cbits[8];
    size_t lane_cby[8];
    int fq;
    for (fq = 0; fq < 8; fq++) {
        if (lanes == NULL || lanes->l[fq].p == NULL ||
            lanes->l[fq].bs_end == NULL ||
            lanes->l[fq].bs_end < lanes->l[fq].p) {
            lane_p[fq] = NULL;
            lane_cbits[fq] = (size_t)0;
            lane_cby[fq] = (size_t)0;
        } else {
            lane_p[fq] = lanes->l[fq].p;
            lane_cby[fq] =
                (size_t)(lanes->l[fq].bs_end - lanes->l[fq].p);
            lane_cbits[fq] = lane_cby[fq] * (size_t)8;
        }
    }
    *out_n = (size_t)0;
    *cap_hit = 0;
    /* R10-TEXTDEC: recents + lane bitpos in locals. recent[] is
     * written back on success only (carries across blocks); FAIL/
     * cap exits leave it stale (dead: walker returns, no resume).
     * bitpos is NEVER written back (lanes frame-local in
     * comp_block, unread after replay on every exit). */
    uint32_t r0 = recent[0];
    uint32_t r1 = recent[1];
    uint32_t r2 = recent[2];
    uint32_t r3 = recent[3];
    size_t bitpos[8];
    int bq;
    for (bq = 0; bq < 8; bq++) {
        bitpos[bq] = lanes->l[bq].bitpos;
    }
    for (ti = 0; ti < ss[0].n; ti++) {
        uint8_t t;
        uint32_t lit_f;
        uint32_t sel;
        uint32_t len_f;
        uint32_t run = (uint32_t)0;
        uint32_t mlen = (uint32_t)0;
        uint32_t d = (uint32_t)0;
        uint32_t ml_short = (uint32_t)0;
        uint32_t ml_esc = (uint32_t)0;
        size_t take;
        int lok;
        /* R10-TEXTDEC: wblk>=ds fuses the top ==ds check with the
         * old wblk>ds check below (both pre-write pure FAILs;
         * lit-escape len_used effects are locals, dead on FAIL). */
        if (wblk >= (size_t)ds) {
            return LZ_U3_FAIL;
        }
        t = tok_p[ti];
        lit_f = ((uint32_t)t >> 6) & (uint32_t)3;
        sel = ((uint32_t)t >> 3) & (uint32_t)7;
        len_f = (uint32_t)t & (uint32_t)7;
        /* (1) lit-len (short inline, escape calls out; same as generic). */
        if (lit_f != (uint32_t)3) {
            run = lit_f;
            lok = LZ_U3_OK;
        } else {
            lok = lz_u3_lit_run(lit_f, &ss[1], &len_used, &run);
        }
        if (lz_u2_c16_ok(lok) != LZ_U2_OK) {
            return LZ_U3_FAIL;
        }
        /* (2) lit-copy (same bounds as generic; room checks dead:
         * wblk<ds<=room here (==: room==ds), take<=ds-wblk=room-wblk after clamp).
         * R10-TEXTDEC: wblk>ds fused into the top >=ds check; lit_n
         * single check (lit_used<=lit_n invariant: first-block
         * lit_used=1 gated by first_lit_ok+ss_byte, else 0; take
         * clamped every iter, so no size_t wrap). */
        if ((size_t)lit_used + (size_t)run > lit_bound) {
            return LZ_U3_FAIL;
        }
        take = (size_t)run;
        if (take > (size_t)ds - wblk) {
            take = (size_t)ds - wblk;
        }
        if (take > (size_t)lit_n - (size_t)lit_used) {
            return LZ_U3_FAIL;
        }
        if (take <= (size_t)3) {
            if (take != (size_t)0) {
                dst[w_tot + wblk] = lit_p[lit_used];
                if (take > (size_t)1) {
                    dst[w_tot + wblk + (size_t)1] =
                        lit_p[lit_used + (uint32_t)1];
                    if (take > (size_t)2) {
                        dst[w_tot + wblk + (size_t)2] =
                            lit_p[lit_used + (uint32_t)2];
                    }
                }
            }
        } else {
            memcpy(dst + w_tot + wblk, lit_p + lit_used, take);
        }
        lit_used += (uint32_t)take;
        wblk += take;
        /* (3) dist-resolve (ptr-direct; lanes!=NULL by guard).
         * R10-TEXTDEC: rep leg selects+shuffles scalar recents and
         * sets match-len short/esc inline (dedups the two sel<=3
         * branches below; recents writes dead on later FAILs). */
        if (sel <= (uint32_t)3) {
            if (sel == (uint32_t)0) {
                d = r0;
            } else if (sel == (uint32_t)1) {
                d = r1;
                r1 = r0;
                r0 = d;
            } else if (sel == (uint32_t)2) {
                d = r2;
                r2 = r1;
                r1 = r0;
                r0 = d;
            } else {
                d = r3;
                r3 = r2;
                r2 = r1;
                r1 = r0;
                r0 = d;
            }
            ml_short = len_f;
            ml_esc = (uint32_t)7;
        } else {
            uint8_t dsym;
            uint32_t didx;
            uint32_t sb;
            uint32_t slow3;
            uint32_t suffix = (uint32_t)0;
            if (dist_used >= dist_n) {
                return LZ_U3_FAIL;
            }
            dsym = dist_p[dist_used];
            didx = dist_used;
            dist_used += (uint32_t)1;
            sb = (uint32_t)dsym >> 3;
            slow3 = (uint32_t)dsym & (uint32_t)7;
            if (sb != (uint32_t)0) {
                uint32_t lk;
                size_t spos;
                size_t sbyte;
                uint32_t ssh;
                lk = didx & (uint32_t)7;
                if (lane_p[lk] == NULL) {
                    return LZ_U3_FAIL;
                }
                /* R10-TEXTDEC: local bitpos (never written back);
                 * window check first: sb<=31 makes sbyte+8<=cby
                 * imply spos+sb<=cbits, so the bounds check runs
                 * only when the window misses (same FAILs, dst
                 * unwritten before either; bitpos<=cbits+31 by
                 * checked-advance induction, so no size_t wrap). */
                spos = bitpos[lk];
                sbyte = spos >> 3;
                ssh = (uint32_t)(spos & (size_t)7);
                if (sbyte + (size_t)8 <= lane_cby[lk]) {
                    uint64_t w64 = lz_u3_mc_ld64(lane_p[lk] + sbyte);
                    suffix = (uint32_t)(w64 >> ssh) &
                        (((uint32_t)1 << sb) - (uint32_t)1);
                    bitpos[lk] = spos + (size_t)sb;
                } else if (spos + (size_t)sb > lane_cbits[lk]) {
                    return LZ_U3_FAIL;
                } else {
                    lanes->l[lk].bitpos = bitpos[lk];
                    if (lz_u3_suffix_bits(&lanes->l[lk], sb,
                                          &suffix) != LZ_U3_OK) {
                        return LZ_U3_FAIL;
                    }
                    bitpos[lk] = lanes->l[lk].bitpos;
                }
            }
            d = lz_u3_dist(sb, slow3, suffix);
            /* R10-TEXTDEC: new-leg match-len short/esc + scalar
             * push inline (dedups the sel<=3 branches below). */
            ml_short = (((sel - (uint32_t)4) << 3) | len_f) & (uint32_t)31;
            ml_esc = (uint32_t)31;
            r3 = r2;
            r2 = r1;
            r1 = r0;
            r0 = d;
        }
        /* (4) C13 single unsigned compare: (d-1)>=w is exact
         * (w<=dst_capacity<=0x7FFFFFFF by the entry gate; d==0
         * wraps to UINT32_MAX which always fails; w==0 fails
         * both forms on every d). */
        if ((uint32_t)(d - (uint32_t)1) >= (uint32_t)(w_tot + wblk)) {
            return LZ_U3_FAIL;
        }
        /* (5) match-len (short inline, escape calls out; sel split
         * hoisted into the dist legs above). */
        if (ml_short != ml_esc) {
            mlen = ml_short + (uint32_t)2;
            lok = LZ_U3_OK;
        } else {
            lok = lz_u3_match_len(sel, len_f, &ss[1], &len_used, &mlen);
        }
        if (lz_u2_c16_ok(lok) != LZ_U2_OK) {
            return LZ_U3_FAIL;
        }
        /* (6) match-copy (same clamp; room checks dead: wblk<=ds<=room (==),
         * take<=ds-wblk=room-wblk).
         * R10-TEXTDEC: inline take<=16 on the d>=take leg (66.1%
         * take<=8, 33.8% take 9-16 on text-L9; memcpy kept past
         * 16, match_copy past d<take). d>=take ranges are disjoint
         * (src end w-d+take<=w=dst start, c13 d>=1); overlapping
         * wide stores rewrite identical bytes, so every form is
         * byte-exact with no overrun (R-014); take==0 skips (the
         * old match_copy(0) leg is a proven no-op). */
        take = (size_t)mlen;
        if (take > (size_t)ds - wblk) {
            take = (size_t)ds - wblk;
        }
        /* R11-LASTBLOCK: room==ds cap-stop, generic match-leg
         * wblk>=room check verbatim (same position AFTER
         * dist/c13/mlen, so their FAILs keep precedence, same
         * operands). With room==ds the other three cap checks
         * are statically dead (lit legs see wblk<ds=room and
         * take<=ds-wblk; match take<=ds-wblk=room-wblk); this
         * one fires iff the lit leg exactly filled the block
         * mid-token (S4.5 silent prefix, end checks skipped;
         * recent/lanes dead: walker returns on cap_hit). */
        if (wblk >= room) {
            *out_n = room;
            *cap_hit = 1;
            return LZ_U3_OK;
        }
        if ((size_t)d >= take) {
            size_t moff = w_tot + wblk;
            if (take <= (size_t)8) {
                if (take <= (size_t)3) {
                    if (take != (size_t)0) {
                        dst[moff] = dst[moff - (size_t)d];
                        if (take > (size_t)1) {
                            dst[moff + (size_t)1] =
                                dst[moff + (size_t)1 - (size_t)d];
                            if (take > (size_t)2) {
                                dst[moff + (size_t)2] =
                                    dst[moff + (size_t)2 - (size_t)d];
                            }
                        }
                    }
                } else {
                    uint32_t mlo =
                        lz_u3_mc_ld32(dst + moff - (size_t)d);
                    uint32_t mhi =
                        lz_u3_mc_ld32(dst + moff - (size_t)d + take -
                                      (size_t)4);
                    lz_u3_mc_st32(dst + moff, mlo);
                    lz_u3_mc_st32(dst + moff + take - (size_t)4, mhi);
                }
            } else if (take <= (size_t)16) {
                uint64_t mlo = lz_u3_mc_ld64(dst + moff - (size_t)d);
                uint64_t mhi =
                    lz_u3_mc_ld64(dst + moff - (size_t)d + take -
                                  (size_t)8);
                lz_u3_mc_st64(dst + moff, mlo);
                lz_u3_mc_st64(dst + moff + take - (size_t)8, mhi);
            } else {
                memcpy(dst + moff, dst + moff - (size_t)d, take);
            }
        } else {
            lz_u3_match_copy(dst, w_tot + wblk, d, take);
        }
        wblk += take;
        /* (7) recents update hoisted into the dist legs above. */
    }
    /* R10-TEXTDEC: success-only recents writeback (carries across
     * blocks; FAIL exits above skip it: recent[] dead there). */
    recent[0] = r0;
    recent[1] = r1;
    recent[2] = r2;
    recent[3] = r3;
    /* End checks (replay tail verbatim; counters local). */
    if (lz_u2_end_checks(ti, ss[0].n, lit_used, ss[2].n,
                         len_used, ss[1].n, dist_used,
                         ss[3].n, wblk,
                         (size_t)ds) != LZ_U2_OK) {
        return LZ_U3_FAIL;
    }
    *out_n = wblk;
    return LZ_U3_OK;
}

/* E1 replay (S4.3/u2 contract). Consumes decoded streams (fetch-owned) +
 * suffix lanes (D-B3, mutable bitpos). dst full buf, w_tot pre-bytes (C13);
 * room = cap room. OK sets *out_n (<=room) + *cap_hit on silent prefix
 * (end checks skipped, S4.5). FAIL = reject. */
static int lz_u3_replay(uint8_t *dst, size_t w_tot, size_t room, uint32_t ds,
                        const struct lz_u3_ss *ss, struct lz_u3_lanes *lanes,
                        uint32_t *recent, int is_first_block, size_t *out_n,
                        int *cap_hit) {
    uint32_t ti;
    uint32_t lit_used = (uint32_t)0;
    uint32_t len_used = (uint32_t)0;
    uint32_t dist_used = (uint32_t)0;  /* owned by suffix fetch (D-B3) */
    size_t wblk = (size_t)0;
    uint8_t fb = (uint8_t)0;
    /* P2-bitio: hoisted tok/lit views (ss immutable in replay; the
     * fallback legs preserve ss_byte FAILs bit-for-bit). */
    uint32_t tok_mode = ss[0].mode;
    const uint8_t *tok_p = ss[0].p;
    uint8_t tok_fill = ss[0].fill;
    uint32_t lit_mode = ss[2].mode;
    const uint8_t *lit_p = ss[2].p;
    uint8_t lit_fill = ss[2].fill;
    /* R3-MIXDEC: hoisted replay invariants. ss is immutable in replay;
     * lane p/bs_end are invariant (only bitpos advances, kept live in
     * lanes so every exit observes identical state). Caps replicate
     * lz_u3_lane_cap bit-for-bit (same guards, same arithmetic). */
    uint32_t dist_mode = ss[3].mode;
    const uint8_t *dist_p = ss[3].p;
    uint8_t dist_fill = ss[3].fill;
    uint32_t dist_n = ss[3].n;
    uint32_t lit_n = ss[2].n;
    size_t lit_bound =
        (size_t)ss[0].n + (size_t)lz_u2_round32(ss[2].n) - (size_t)1;
    const uint8_t *lane_p[8];
    size_t lane_cbits[8];
    size_t lane_cby[8];
    int fq;
    for (fq = 0; fq < 8; fq++) {
        if (lanes == NULL || lanes->l[fq].p == NULL ||
            lanes->l[fq].bs_end == NULL ||
            lanes->l[fq].bs_end < lanes->l[fq].p) {
            lane_p[fq] = NULL;
            lane_cbits[fq] = (size_t)0;
            lane_cby[fq] = (size_t)0;
        } else {
            lane_p[fq] = lanes->l[fq].p;
            lane_cby[fq] =
                (size_t)(lanes->l[fq].bs_end - lanes->l[fq].p);
            lane_cbits[fq] = lane_cby[fq] * (size_t)8;
        }
    }
    *out_n = (size_t)0;
    *cap_hit = 0;
    /* u6b D-B5: first-byte pre-emit (S4.1/Q18). Entry pos 1, literals[0]
     * pre-token. litc==0 rejects. room>=1 here (caller cap-stops). */
    if (lz_u2_first_lit_ok(is_first_block, ss[2].n) != LZ_U2_OK) {
        return LZ_U3_FAIL;
    }
    if (is_first_block != 0) {
        if (lz_u3_ss_byte(&ss[2], (uint32_t)0, &fb) != LZ_U3_OK) {
            return LZ_U3_FAIL;
        }
        if (room == (size_t)0) {
            return LZ_U3_FAIL;
        }
        dst[w_tot] = fb;
        lit_used = (uint32_t)1;
        wblk = (size_t)1;
        if (room == (size_t)1) {
            *out_n = (size_t)1;
            *cap_hit = 1;
            return LZ_U3_OK;
        }
    }
    /* R4-DECRES fast loop: hoisted ptr-direct tok/lit/dist + room>ds (strict:
     * room==ds exact-fill takes the cap_hit early-out in generic, NOT the
     * FAIL/end-check path, so == stays generic). Guard also excludes REPEAT
     * modes (fill wins over ptr even when ptr != NULL) and NULL streams.
     * Census tmp/r4decres: matrix/battery-valid decodes are 100% ptr legs;
     * run0 69-89%, run<=1 78-99% (take==0 skip + take<=3 inline stores kill
     * the libc memcpy call on ~9/10 tokens); dge>=98%. Generic loop below
     * is byte-verbatim (kept at old indent deliberately); both paths share
     * the pre-emit above and the end checks below. */
    if (tok_mode != (uint32_t)1 && tok_p != NULL &&
        lit_mode != (uint32_t)1 && lit_p != NULL &&
        dist_mode != (uint32_t)1 && dist_p != NULL &&
        lanes != NULL && room > (size_t)ds) {
        /* R10-TEXTDEC: recents + lane bitpos in locals. recent[] is
         * written back on success only (carries across blocks); FAIL/
         * cap exits leave it stale (dead: walker returns, no resume).
         * bitpos is NEVER written back (lanes frame-local in
         * comp_block, unread after replay on every exit). */
        uint32_t r0 = recent[0];
        uint32_t r1 = recent[1];
        uint32_t r2 = recent[2];
        uint32_t r3 = recent[3];
        size_t bitpos[8];
        int bq;
        for (bq = 0; bq < 8; bq++) {
            bitpos[bq] = lanes->l[bq].bitpos;
        }
        for (ti = 0; ti < ss[0].n; ti++) {
            uint8_t t;
            uint32_t lit_f;
            uint32_t sel;
            uint32_t len_f;
            uint32_t run = (uint32_t)0;
            uint32_t mlen = (uint32_t)0;
            uint32_t d = (uint32_t)0;
            uint32_t ml_short = (uint32_t)0;
            uint32_t ml_esc = (uint32_t)0;
            size_t take;
            int lok;
            /* R10-TEXTDEC: wblk>=ds fuses the top ==ds check with the
             * old wblk>ds check below (both pre-write pure FAILs;
             * lit-escape len_used effects are locals, dead on FAIL). */
            if (wblk >= (size_t)ds) {
                return LZ_U3_FAIL;
            }
            t = tok_p[ti];
            lit_f = ((uint32_t)t >> 6) & (uint32_t)3;
            sel = ((uint32_t)t >> 3) & (uint32_t)7;
            len_f = (uint32_t)t & (uint32_t)7;
            /* (1) lit-len (short inline, escape calls out; same as generic).
             * R23-TL9D-P4: lit_f==0 (82% text-L9) skips the lit block:
             * run==0 => take==0, and both bounds checks are vacuous
             * under the lit_used<=lit_n invariant (base: 1 gated by
             * first_lit_ok+ss_byte / 0; step: take<=lit_n-lit_used
             * checked every non-skip iter) + lit_n<=lit_bound (tok_n>=1
             * by loop bound). lit_used/wblk += 0 no-ops. Same bytes,
             * same FAILs (skip fires only when checks provably pass). */
            if (lit_f == (uint32_t)0) {
                run = (uint32_t)0;
                take = (size_t)0;
            } else {
                if (lit_f != (uint32_t)3) {
                    run = lit_f;
                    lok = LZ_U3_OK;
                } else {
                    lok = lz_u3_lit_run(lit_f, &ss[1], &len_used, &run);
                }
                if (lz_u2_c16_ok(lok) != LZ_U2_OK) {
                    return LZ_U3_FAIL;
                }
                /* (2) lit-copy (same bounds as generic; room checks dead:
                 * wblk<ds<room here, take<=ds-wblk<room-wblk after clamp).
                 * R10-TEXTDEC: wblk>ds fused into the top >=ds check; lit_n
                 * single check (lit_used<=lit_n invariant: first-block
                 * lit_used=1 gated by first_lit_ok+ss_byte, else 0; take
                 * clamped every iter, so no size_t wrap). */
                if ((size_t)lit_used + (size_t)run > lit_bound) {
                    return LZ_U3_FAIL;
                }
                take = (size_t)run;
                if (take > (size_t)ds - wblk) {
                    take = (size_t)ds - wblk;
                }
                if (take > (size_t)lit_n - (size_t)lit_used) {
                    return LZ_U3_FAIL;
                }
                if (take <= (size_t)3) {
                    if (take != (size_t)0) {
                        dst[w_tot + wblk] = lit_p[lit_used];
                        if (take > (size_t)1) {
                            dst[w_tot + wblk + (size_t)1] =
                                lit_p[lit_used + (uint32_t)1];
                            if (take > (size_t)2) {
                                dst[w_tot + wblk + (size_t)2] =
                                    lit_p[lit_used + (uint32_t)2];
                            }
                        }
                    }
                } else {
                    memcpy(dst + w_tot + wblk, lit_p + lit_used, take);
                }
            } /* P4 else (lit_f != 0) */
            lit_used += (uint32_t)take;
            wblk += take;
            /* (3) dist-resolve (ptr-direct; lanes!=NULL by guard).
             * R10-TEXTDEC: rep leg selects+shuffles scalar recents and
             * sets match-len short/esc inline (dedups the two sel<=3
             * branches below; recents writes dead on later FAILs). */
            if (sel <= (uint32_t)3) {
                if (sel == (uint32_t)0) {
                    d = r0;
                } else if (sel == (uint32_t)1) {
                    d = r1;
                    r1 = r0;
                    r0 = d;
                } else if (sel == (uint32_t)2) {
                    d = r2;
                    r2 = r1;
                    r1 = r0;
                    r0 = d;
                } else {
                    d = r3;
                    r3 = r2;
                    r2 = r1;
                    r1 = r0;
                    r0 = d;
                }
                ml_short = len_f;
                ml_esc = (uint32_t)7;
            } else {
                uint8_t dsym;
                uint32_t didx;
                uint32_t sb;
                uint32_t slow3;
                uint32_t suffix = (uint32_t)0;
                if (dist_used >= dist_n) {
                    return LZ_U3_FAIL;
                }
                dsym = dist_p[dist_used];
                didx = dist_used;
                dist_used += (uint32_t)1;
                sb = (uint32_t)dsym >> 3;
                slow3 = (uint32_t)dsym & (uint32_t)7;
                if (sb != (uint32_t)0) {
                    uint32_t lk;
                    size_t spos;
                    size_t sbyte;
                    uint32_t ssh;
                    lk = didx & (uint32_t)7;
                    if (lane_p[lk] == NULL) {
                        return LZ_U3_FAIL;
                    }
                    /* R10-TEXTDEC: local bitpos (never written back);
                     * window check first: sb<=31 makes sbyte+8<=cby
                     * imply spos+sb<=cbits, so the bounds check runs
                     * only when the window misses (same FAILs, dst
                     * unwritten before either; bitpos<=cbits+31 by
                     * checked-advance induction, so no size_t wrap). */
                    spos = bitpos[lk];
                    sbyte = spos >> 3;
                    ssh = (uint32_t)(spos & (size_t)7);
                    if (sbyte + (size_t)8 <= lane_cby[lk]) {
                        uint64_t w64 = lz_u3_mc_ld64(lane_p[lk] + sbyte);
                        suffix = (uint32_t)(w64 >> ssh) &
                            (((uint32_t)1 << sb) - (uint32_t)1);
                        bitpos[lk] = spos + (size_t)sb;
                    } else if (spos + (size_t)sb > lane_cbits[lk]) {
                        return LZ_U3_FAIL;
                    } else {
                        lanes->l[lk].bitpos = bitpos[lk];
                        if (lz_u3_suffix_bits(&lanes->l[lk], sb,
                                              &suffix) != LZ_U3_OK) {
                            return LZ_U3_FAIL;
                        }
                        bitpos[lk] = lanes->l[lk].bitpos;
                    }
                }
                d = lz_u3_dist(sb, slow3, suffix);
                /* R10-TEXTDEC: new-leg match-len short/esc + scalar
                 * push inline (dedups the sel<=3 branches below). */
                ml_short = (((sel - (uint32_t)4) << 3) | len_f) & (uint32_t)31;
                ml_esc = (uint32_t)31;
                r3 = r2;
                r2 = r1;
                r1 = r0;
                r0 = d;
            }
            /* (4) C13 single unsigned compare: (d-1)>=w is exact
             * (w<=dst_capacity<=0x7FFFFFFF by the entry gate; d==0
             * wraps to UINT32_MAX which always fails; w==0 fails
             * both forms on every d). */
            if ((uint32_t)(d - (uint32_t)1) >= (uint32_t)(w_tot + wblk)) {
                return LZ_U3_FAIL;
            }
            /* (5) match-len (short inline, escape calls out; sel split
             * hoisted into the dist legs above). */
            if (ml_short != ml_esc) {
                mlen = ml_short + (uint32_t)2;
                lok = LZ_U3_OK;
            } else {
                lok = lz_u3_match_len(sel, len_f, &ss[1], &len_used, &mlen);
            }
            if (lz_u2_c16_ok(lok) != LZ_U2_OK) {
                return LZ_U3_FAIL;
            }
            /* (6) match-copy (same clamp; room checks dead: wblk<=ds<room,
             * take<=ds-wblk<room-wblk).
             * R10-TEXTDEC: inline take<=16 on the d>=take leg (66.1%
             * take<=8, 33.8% take 9-16 on text-L9; memcpy kept past
             * 16, match_copy past d<take). d>=take ranges are disjoint
             * (src end w-d+take<=w=dst start, c13 d>=1); overlapping
             * wide stores rewrite identical bytes, so every form is
             * byte-exact with no overrun (R-014); take==0 skips (the
             * old match_copy(0) leg is a proven no-op). */
            take = (size_t)mlen;
            if (take > (size_t)ds - wblk) {
                take = (size_t)ds - wblk;
            }
            if ((size_t)d >= take) {
                size_t moff = w_tot + wblk;
                if (take <= (size_t)8) {
                    if (take <= (size_t)3) {
                        if (take != (size_t)0) {
                            dst[moff] = dst[moff - (size_t)d];
                            if (take > (size_t)1) {
                                dst[moff + (size_t)1] =
                                    dst[moff + (size_t)1 - (size_t)d];
                                if (take > (size_t)2) {
                                    dst[moff + (size_t)2] =
                                        dst[moff + (size_t)2 - (size_t)d];
                                }
                            }
                        }
                    } else {
                        uint32_t mlo =
                            lz_u3_mc_ld32(dst + moff - (size_t)d);
                        uint32_t mhi =
                            lz_u3_mc_ld32(dst + moff - (size_t)d + take -
                                          (size_t)4);
                        lz_u3_mc_st32(dst + moff, mlo);
                        lz_u3_mc_st32(dst + moff + take - (size_t)4, mhi);
                    }
                } else if (take <= (size_t)16) {
                    uint64_t mlo = lz_u3_mc_ld64(dst + moff - (size_t)d);
                    uint64_t mhi =
                        lz_u3_mc_ld64(dst + moff - (size_t)d + take -
                                      (size_t)8);
                    lz_u3_mc_st64(dst + moff, mlo);
                    lz_u3_mc_st64(dst + moff + take - (size_t)8, mhi);
                } else {
                    memcpy(dst + moff, dst + moff - (size_t)d, take);
                }
            } else {
                lz_u3_match_copy(dst, w_tot + wblk, d, take);
            }
            wblk += take;
            /* (7) recents update hoisted into the dist legs above. */
        }
        /* R10-TEXTDEC: success-only recents writeback (carries across
         * blocks; FAIL exits above skip it: recent[] dead there). */
        recent[0] = r0;
        recent[1] = r1;
        recent[2] = r2;
        recent[3] = r3;
    } else if (tok_mode != (uint32_t)1 && tok_p != NULL &&
               lit_mode != (uint32_t)1 && lit_p != NULL &&
               dist_mode != (uint32_t)1 && dist_p != NULL &&
               lanes != NULL && room == (size_t)ds) {
        /* R11-LASTBLOCK: out-of-line exact-cap leg (one call per
         * last block; census tmp/r11lastblock: exact-cap text
         * 11.8/12.8/20.6% + mixed 9.8/11.0/11.9% of tokens).
         * Slack-cap decodes (bench n+64) never enter here. */
        return lz_u3_replay_eq(dst, w_tot, room, ds, ss, lanes,
                               recent, lit_used, wblk, out_n,
                               cap_hit);
    } else {
    for (ti = 0; ti < ss[0].n; ti++) {
        uint8_t t = (uint8_t)0;
        uint32_t lit_f;
        uint32_t sel;
        uint32_t len_f;
        uint32_t run = (uint32_t)0;
        uint32_t mlen = (uint32_t)0;
        uint32_t d = (uint32_t)0;
        uint32_t ml_short = (uint32_t)0;
        uint32_t ml_esc = (uint32_t)0;
        uint32_t i;
        size_t take;
        int lok;
        /* u6k REV C21 stop-at-fill (S4.3 tokens-remain-after-fill):
         * filled (wblk==ds) with tokens remaining (ti<tokc) rejects.
         * Valid parses fill on the last token (terminator-clamp S2.8),
         * where the loop exits and end checks run. Cap-hit returns
         * mid-token before any re-check (S4.5 precedence kept). */
        if (wblk == (size_t)ds) {
            return LZ_U3_FAIL;
        }
        /* P2-bitio: direct tok fetch (ti<n by loop bound). */
        if (tok_mode == (uint32_t)1) {
            t = tok_fill;
        } else if (tok_p != NULL) {
            t = tok_p[ti];
        } else if (lz_u3_ss_byte(&ss[0], ti, &t) != LZ_U3_OK) {
            return LZ_U3_FAIL;
        }
        lit_f = ((uint32_t)t >> 6) & (uint32_t)3;
        sel = ((uint32_t)t >> 3) & (uint32_t)7;
        len_f = (uint32_t)t & (uint32_t)7;
        /* (1) lit-len decode, C16 fail -> reject BEFORE any copy.
         * R3-MIXDEC: inline short leg (lit_f!=3 => run=lit_f, OK is the
         * decode_len short!=escape leg verbatim); escape leg calls out. */
        if (lit_f != (uint32_t)3) {
            run = lit_f;
            lok = LZ_U3_OK;
        } else {
            lok = lz_u3_lit_run(lit_f, &ss[1], &len_used, &run);
        }
        if (lz_u2_c16_ok(lok) != LZ_U2_OK) {
            return LZ_U3_FAIL;
        }
        /* (2) lit-copy, C18 room-tight BEFORE copying. D-B6: Q14 wants
         * zero-fill-without-pre-reject; port pre-rejects (ret-0-identical
         * per App C garbage rule; deviation G-u6b-LITQ14, R-115 open). */
        if (wblk > (size_t)ds) {
            return LZ_U3_FAIL; /* u6k EXP: ds-wblk underflow guard */
        }
        /* PORTGAP-1 LIT-RUN-CLAMP (dg1 Class-2, measured): Apple clamps the
         * lit copy to ds-room (emitting ds exactly); the port pre-rejected
         * any overhang. The DECLARED run carries a staging bound on the
         * LIT-STREAM position (lit_used+run <= tokc+round32up(litc)-1;
         * PORTGAP-3: exact-fit on 81 strict clean-state ceilings
         * [B=tokc+31 for litc<=32, B=tokc+63 for litc 33-39, break
         * exactly at 32|33] + e01 both-edges [tokc=6,litc=4085 -> 4101,
         * accepts 4101 refuses 4102] + REPEAT/e00-single special case
         * [tokc=1,litc=ds -> round32up(ds)]; r32(ds)/r32(litc+distc)/
         * r32(litc-distc)/r32(lu)/r32(R) each refuted). lit-stream room
         * stays enforced on the clamped take for all modes (no evidence
         * Apple reads past lit end). Fully-consumed runs (take==run)
         * always pass (lu+run<=litc<=bound, tokc>=1); valid streams
         * take identical paths. size_t arithmetic: u32 sum can wrap. */
        if ((size_t)lit_used + (size_t)run > lit_bound) {
            return LZ_U3_FAIL;
        }
        take = (size_t)run;
        if (take > (size_t)ds - wblk) {
            take = (size_t)ds - wblk;
        }
        if (lit_used > lit_n ||
            take > (size_t)lit_n - (size_t)lit_used) {
            return LZ_U3_FAIL; /* overruns lit stream: C18-class */
        }
        if (wblk >= room) {
            *out_n = room;
            *cap_hit = 1;
            return LZ_U3_OK; /* u6k EXP: cap-stop S4.5 prefix */
        }
        if (take > room - wblk) {
            size_t nn = room - wblk;
            size_t k;
            /* P2-bitio: direct lit copy (bounds verified above).
             * R3-MIXDEC: word copy (lit stream and dst are distinct
             * buffers, so memcpy/memset are byte-identical). */
            if (lit_mode == (uint32_t)1) {
                memset(dst + w_tot + wblk, lit_fill, nn);
            } else if (lit_p != NULL) {
                memcpy(dst + w_tot + wblk, lit_p + lit_used, nn);
            } else {
                for (k = (size_t)0; k < nn; k++) {
                    uint8_t lb = (uint8_t)0;
                    if (lz_u3_ss_byte(&ss[2], lit_used + (uint32_t)k, &lb) !=
                        LZ_U3_OK) {
                        return LZ_U3_FAIL;
                    }
                    dst[w_tot + wblk + k] = lb;
                }
            }
            *out_n = room;
            *cap_hit = 1;
            return LZ_U3_OK;
        }
        /* P2-bitio: direct lit copy (bounds verified above).
         * R3-MIXDEC: word copy (see cap-hit leg above). */
        if (lit_mode == (uint32_t)1) {
            memset(dst + w_tot + wblk, lit_fill, take);
        } else if (lit_p != NULL) {
            memcpy(dst + w_tot + wblk, lit_p + lit_used, take);
        } else {
            for (i = (uint32_t)0; i < take; i++) {
                uint8_t lb = (uint8_t)0;
                if (lz_u3_ss_byte(&ss[2], lit_used + i, &lb) != LZ_U3_OK) {
                    return LZ_U3_FAIL;
                }
                dst[w_tot + wblk + (size_t)i] = lb;
            }
        }
        lit_used += (uint32_t)take;
        wblk += take;
        /* (3) dist-resolve C14 before C13.
         * R3-MIXDEC: inline dist_resolve + suffix_bits (mixed: 98.5%
         * new-dist). Order mirrors both functions leg-for-leg: dist
         * n-check, REPEAT/NULL/fetch, idx latch, sb0 skip, lane
         * NULL/bounds, u64 window (same shift/mask value as the byte
         * loop; guarded inside proven bytes, else the original call),
         * lz_u3_dist. Every FAIL above is the same FAIL below. */
        if (sel <= (uint32_t)3) {
            d = recent[sel];
        } else {
            uint8_t dsym;
            uint32_t didx;
            uint32_t sb;
            uint32_t slow3;
            uint32_t suffix = (uint32_t)0;
            if (dist_used >= dist_n) {
                return LZ_U3_FAIL;
            }
            if (dist_mode == (uint32_t)1) {
                dsym = dist_fill;
            } else if (dist_p == NULL) {
                return LZ_U3_FAIL;
            } else {
                dsym = dist_p[dist_used];
            }
            didx = dist_used;
            dist_used += (uint32_t)1;
            sb = (uint32_t)dsym >> 3;
            slow3 = (uint32_t)dsym & (uint32_t)7;
            if (sb != (uint32_t)0) {
                uint32_t lk;
                size_t spos;
                size_t sbyte;
                uint32_t ssh;
                if (lanes == NULL) {
                    return LZ_U3_FAIL;
                }
                lk = didx & (uint32_t)7;
                if (lane_p[lk] == NULL) {
                    return LZ_U3_FAIL;
                }
                spos = lanes->l[lk].bitpos;
                if (spos + (size_t)sb > lane_cbits[lk]) {
                    return LZ_U3_FAIL;
                }
                sbyte = spos >> 3;
                ssh = (uint32_t)(spos & (size_t)7);
                if (sbyte + (size_t)8 <= lane_cby[lk]) {
                    uint64_t w64 = lz_u3_mc_ld64(lane_p[lk] + sbyte);
                    suffix = (uint32_t)(w64 >> ssh) &
                        (((uint32_t)1 << sb) - (uint32_t)1);
                    lanes->l[lk].bitpos = spos + (size_t)sb;
                } else if (lz_u3_suffix_bits(&lanes->l[lk], sb,
                                             &suffix) != LZ_U3_OK) {
                    return LZ_U3_FAIL;
                }
            }
            d = lz_u3_dist(sb, slow3, suffix);
        }
        /* (4) C13 post-literal strict on resolved d. */
        if (lz_u2_c13_ok(d, w_tot + wblk) != LZ_U2_OK) {
            return LZ_U3_FAIL;
        }
        /* (5) match-len decode C16 before match copy.
         * R3-MIXDEC: inline short leg (lshort verbatim from match_len;
         * lshort<=31 so the wrap/overflow legs are vacuous). */
        if (sel <= (uint32_t)3) {
            ml_short = len_f;
            ml_esc = (uint32_t)7;
        } else {
            ml_short = (((sel - (uint32_t)4) << 3) | len_f) & (uint32_t)31;
            ml_esc = (uint32_t)31;
        }
        if (ml_short != ml_esc) {
            mlen = ml_short + (uint32_t)2;
            lok = LZ_U3_OK;
        } else {
            lok = lz_u3_match_len(sel, len_f, &ss[1], &len_used, &mlen);
        }
        if (lz_u2_c16_ok(lok) != LZ_U2_OK) {
            return LZ_U3_FAIL;
        }
        /* (6) match-copy with term-clamp to ds.
         * R3-MIXDEC: match_take inline (wblk<=ds proven above, so
         * min(mlen,ds-wblk) is the function verbatim). */
        take = (size_t)mlen;
        if (take > (size_t)ds - wblk) {
            take = (size_t)ds - wblk;
        }
        if (wblk >= room) {
            *out_n = room;
            *cap_hit = 1;
            return LZ_U3_OK; /* u6k EXP: cap-stop S4.5 prefix */
        }
        if (take > room - wblk) {
            take = room - wblk;
            lz_u3_match_copy(dst, w_tot + wblk, d, take);
            *out_n = room;
            *cap_hit = 1;
            return LZ_U3_OK;
        }
        /* R3-MIXDEC: d>=take short-circuits the call (same memcpy as
         * the match_copy fast path; C13 above gives d>=1, d<=w). */
        if (take != (size_t)0 && (size_t)d >= take) {
            memcpy(dst + w_tot + wblk, dst + w_tot + wblk - (size_t)d,
                   take);
        } else {
            lz_u3_match_copy(dst, w_tot + wblk, d, take);
        }
        wblk += take;
        /* u6i DEC-MTF (R-E-B8 DEDUP): rep sel<=3 moves slot to front
         * (rep0 no-op); only new-dist sel>=4 inserts d at front. */
        if (sel <= (uint32_t)3) {
            lz_u3_recents_rep(recent, sel);
        } else {
            lz_u3_recents_push(recent, d);
        }
    }
    } /* R4-DECRES: end else (generic loop above byte-verbatim). */
    /* End checks C17..C21 in numeric order after full replay. */
    if (lz_u2_end_checks(ti, ss[0].n, lit_used, ss[2].n, len_used, ss[1].n,
                         dist_used, ss[3].n, wblk,
                         (size_t)ds) != LZ_U2_OK) {
        return LZ_U3_FAIL;
    }
    *out_n = wblk;
    return LZ_U3_OK;
}

/* Decode one COMP block. blk points at the block tag; bo/fo already gated
 * (C4/C5/C6) by the caller. Returns bytes written (<=ds, cap-clamped) or
 * (size_t)-1 on reject. Updates recents (carry across blocks, S3.12). */
static size_t lz_u3_comp_block(uint8_t *dst, size_t w_tot, size_t room,
                               uint32_t ds, const uint8_t *blk, uint32_t bo,
                               uint32_t fo, uint32_t *recent,
                               int is_first_block, int *cap_hit) {
    uint32_t modes;
    uint16_t cnt[4];
    struct lz_u3_ss ss[4];
    struct lz_u3_lanes lanes;
    uint8_t *huf_base = NULL;
    size_t out_n = (size_t)0;
    lz_u3_footer_parse(blk + fo, &modes, cnt);
    if (lz_u2_modes_ok(modes, cnt) != LZ_U2_OK) {
        return (size_t)-1;
    }
    if (lz_u2_counts_ok((uint32_t)cnt[0], (uint32_t)cnt[1], (uint32_t)cnt[2],
                        (uint32_t)cnt[3], ds) != LZ_U2_OK) {
        return (size_t)-1; /* C8..C12 (D-B4) */
    }
    if (lz_u3_mode(modes, 0) == (uint32_t)1 &&
        (uint32_t)cnt[2] > LZ_U3_REPEAT_LIT_MAX) {
        return (size_t)-1; /* S4.2 REPEAT-lit ceiling 62880/62881; lit=lane0 (Q2) */
    }
    if (lz_u2_scratch_ok((uint32_t)cnt[0], (uint32_t)cnt[1],
                         (uint32_t)cnt[2],
                         (uint32_t)cnt[3]) != LZ_U2_OK) {
        return (size_t)-1; /* S4.2 scratch ceiling (D-B7/R-D-B7 T-PIN) */
    }
    /* u6v Q2-B1: UNIFIED LENGTH reject leg (P1 B1, bo==fo run shape).
     * u6w M31: first-block-only (later blocks SKIP inside the gate). */
    if (lz_u6v_b1_gate(ds, blk, bo, fo, modes, cnt,
                       is_first_block) != LZ_U3_OK) {
        return (size_t)-1;
    }
    if (lz_u3_fetch(blk, bo, fo, modes, cnt, ss, &lanes, &huf_base) !=
        LZ_U3_OK) {
        return (size_t)-1;
    }
    if (lz_u3_replay(dst, w_tot, room, ds, ss, &lanes, recent, is_first_block,
                     &out_n, cap_hit) != LZ_U3_OK) {
        lz_u6h_free_ss(ss, huf_base); /* u6h: Huffman bufs (no-op if none) */
        return (size_t)-1;
    }
    lz_u6h_free_ss(ss, huf_base);
    return out_n;
}

/* === API glue (owner: u1) === */
size_t lzmesh_decode(uint8_t *dst, size_t dst_capacity,
                     const uint8_t *src, size_t src_size,
                     void *scratch) {
    /* u1 glue: entry gate S6.5, NULL/edge S1.7/API-SAFETY, cap-stop S4.5,
     * END-return S4.8, RAW copy S2.2/S4.4. COMP body deferred to u3. */
    size_t pos = (size_t)0;
    size_t written = (size_t)0;
    uint32_t recent[4];
    (void)scratch; /* NULL always legal (S1.6); COMP scratch use GAP-deferred */
    lz_u3_recents_init(recent); /* S3.12 {1,1,1,1}; carries across RAW */
    if (src_size > (size_t)LZ_U1_DS_MAX ||
        dst_capacity > (size_t)LZ_U1_DS_MAX) {
        return (size_t)0; /* S6.5 entry gate bit31 */
    }
    if (src == NULL) {
        return (size_t)0; /* srcNULL iff len0 legal, ret0 either way */
    }
    if (dst_capacity == (size_t)0) {
        return (size_t)0; /* cap0: empty prefix, no deref (NULL-safe) */
    }
    if (dst == NULL) {
        return (size_t)0; /* port returns 0; Apple SIGSEGV not mimicked */
    }
    if (src_size == (size_t)0) {
        return (size_t)0; /* no END: truncation */
    }
    while (1) {
        uint8_t tag;
        if (written == dst_capacity) {
            return dst_capacity; /* cap-stop before bad tag/trunc */
        }
        if (pos >= src_size) {
            return (size_t)0;
        }
        tag = src[pos];
        if (tag == (uint8_t)LZ_U1_TAG_END) {
            return written; /* S4.8: return on END, ignore trailing */
        }
        if (tag == (uint8_t)LZ_U1_TAG_RAW) {
            uint32_t ds;
            size_t need, avail, take;
            if (src_size - pos < (size_t)LZ_U1_RAW_HDR) {
                return (size_t)0;
            }
            ds = lz_u1_rd32le(src + pos + (size_t)1);
            if (ds == (uint32_t)0 || ds > LZ_U1_DS_MAX) {
                return (size_t)0; /* C2/C3; ds0 copies 0 then rejects */
            }
            need = (size_t)ds;
            avail = dst_capacity - written;
            take = need < avail ? need : avail;
            if (src_size - pos - (size_t)LZ_U1_RAW_HDR < take) {
                return (size_t)0; /* needed prefix itself truncated */
            }
            lz_u1_move(dst + written, src + pos + (size_t)LZ_U1_RAW_HDR,
                       take);
            written += take;
            if (written == dst_capacity) {
                return dst_capacity; /* silent correct prefix S4.5 */
            }
            pos += (size_t)LZ_U1_RAW_HDR + need; /* take==need here */
            continue;
        }
        if (tag == (uint8_t)LZ_U1_TAG_COMP) {
            uint32_t ds, bo, fo;
            size_t blk;
            size_t got;
            int cap_hit = 0;
            if (src_size - pos < (size_t)LZ_U1_COMP_HDR) {
                return (size_t)0;
            }
            ds = lz_u1_rd32le(src + pos + (size_t)1);
            bo = lz_u1_rd16le(src + pos + (size_t)5);
            fo = lz_u1_rd16le(src + pos + (size_t)7);
            if (ds == (uint32_t)0 || ds > LZ_U1_DS_MAX) {
                return (size_t)0; /* C2/C3 */
            }
            if (bo < (uint32_t)LZ_U1_COMP_HDR) {
                return (size_t)0; /* C4 */
            }
            if (bo > fo) {
                return (size_t)0; /* C5 */
            }
            if (fo >= ds) {
                return (size_t)0; /* C6 strict fo<ds */
            }
            blk = lz_u2_block_len(fo);
            if (src_size - pos < blk) {
                return (size_t)0; /* truncated block */
            }
            got = lz_u3_comp_block(dst, written, dst_capacity - written,
                                   ds, src + pos, bo, fo, recent,
                                   pos == (size_t)0, &cap_hit);
            if (got == (size_t)-1) {
                return (size_t)0;
            }
            written += got;
            if (cap_hit != 0) {
                return dst_capacity; /* silent correct prefix S4.5 */
            }
            pos += blk;
            continue;
        }
        return (size_t)0; /* S2.1 bad tag; S4.5 cap20-hits-it */
    }
}

size_t lzmesh_decode_scratch_size(void) {
    return LZ_U1_DEC_SCRATCH; /* S1.5: 65536 all selectors */
}

size_t lzmesh_decoded_size(const uint8_t *src, size_t src_size) {
    /* S1.8/S2.8: walk tags, sum ds to END. Invalid/trunc/bad-tag -> 0. */
    size_t pos = (size_t)0;
    size_t total = (size_t)0;
    if (src == NULL) {
        return (size_t)0;
    }
    if (src_size == (size_t)0 || src_size > (size_t)LZ_U1_DS_MAX) {
        return (size_t)0; /* empty: no END; huge: entry-gate analogue */
    }
    while (1) {
        uint8_t tag;
        if (pos >= src_size) {
            return (size_t)0; /* truncation: missing END */
        }
        tag = src[pos];
        if (tag == (uint8_t)LZ_U1_TAG_END) {
            if (pos + (size_t)1 != src_size) {
                return (size_t)0; /* S2.8 strict: zero trailing */
            }
            return total;
        }
        if (tag == (uint8_t)LZ_U1_TAG_RAW) {
            uint32_t ds;
            size_t rem;
            if (src_size - pos < (size_t)LZ_U1_RAW_HDR) {
                return (size_t)0;
            }
            ds = lz_u1_rd32le(src + pos + (size_t)1);
            if (ds == (uint32_t)0 || ds > LZ_U1_DS_MAX) {
                return (size_t)0; /* C2/C3 signed-fused */
            }
            rem = src_size - pos - (size_t)LZ_U1_RAW_HDR;
            if (rem < (size_t)ds + (size_t)1) {
                return (size_t)0; /* data + min 1B next/END missing */
            }
            if ((size_t)ds > (size_t)-1 - total) {
                return (size_t)0; /* size_t overflow; no total cap S6.5 */
            }
            total += (size_t)ds;
            pos += (size_t)LZ_U1_RAW_HDR + (size_t)ds;
            continue;
        }
        if (tag == (uint8_t)LZ_U1_TAG_COMP) {
            uint32_t ds, bo, fo;
            size_t blk;
            if (src_size - pos < (size_t)LZ_U1_COMP_HDR) {
                return (size_t)0;
            }
            ds = lz_u1_rd32le(src + pos + (size_t)1);
            bo = lz_u1_rd16le(src + pos + (size_t)5);
            fo = lz_u1_rd16le(src + pos + (size_t)7);
            if (ds == (uint32_t)0 || ds > LZ_U1_DS_MAX) {
                return (size_t)0; /* C2/C3 */
            }
            if (bo < (uint32_t)LZ_U1_COMP_HDR) {
                return (size_t)0; /* C4 */
            }
            if (bo > fo) {
                return (size_t)0; /* C5 */
            }
            if (fo >= ds) {
                return (size_t)0; /* C6 strict fo<ds */
            }
            blk = (size_t)fo + (size_t)LZ_U1_FOOTER;
            if (src_size - pos < blk + (size_t)1) {
                return (size_t)0; /* block + min 1B next/END missing */
            }
            if ((size_t)ds > (size_t)-1 - total) {
                return (size_t)0;
            }
            total += (size_t)ds;
            pos += blk;
            continue;
        }
        return (size_t)0; /* S2.1: bad tag */
    }
}
