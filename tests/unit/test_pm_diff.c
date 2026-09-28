/* SPDX-License-Identifier: 0BSD */
/*
 * test_pm_diff.c — P3-harden pack1 differential (lane lanes/p3-harden).
 *
 * Shipped lzmesh_pack1_lengths (P2: msort + capped-sub/propagation solve)
 * vs oracle orp1_lengths_trk (pre-P2 per-take-item expand, copied verbatim
 * from ce3c3dc5 port/src/lzmesh_enc.c, symbols renamed lzmesh_pack1_->orp1_).
 * Asserts identical ret + lens[] over >=100k freq vectors + crafted
 * exactly-256-occurrence shapes + edge sweeps. Exit 0 iff zero mismatches.
 *
 * Oracle deviations from verbatim (behavior-neutral, documented):
 *  - orp1_kraft_ok closing brace re-added (slice cut; identical body).
 *  - orp1_solve_trk: added nullable take_sub[] out-param recording each
 *    take-item expand count (cnt==256 <=> exactly-256-occurrence subtree:
 *    expand returns 0 past 256, else the exact count). No other change.
 *  - orp1_lengths_trk: lengths body calling orp1_solve_trk, max-buckets
 *    aggregated across q attempts. Comparison uses ret/lens only.
 * Take-subtree coverage buckets are reported; the gate requires
 * max-sub >= 250 (spike n=256 deterministically yields 255). NOTE: an
 * exactly-256 take subtree was NOT found in ~700k shapes hunted during
 * lane work (256-occurrence packages form only as the unique-max-weight
 * final item, which take=2n-2 of ncur~2n-1 always excludes; in-take max
 * observed is 255): pre- and post-fix code are observationally
 * identical over the whole corpus, and the cap-257 fix makes them
 * identical BY CONSTRUCTION (sub==min(true,257): old proceeds at
 * true<=256/new proceeds at sub<=256; old fails past 256/new fails at
 * sub=257). Sensitivity of this differential was proven by mutation
 * (shipped bound flipped to >=255 fails the spike-255 case; reverted).
 * Public-shipped entry: lzmesh_pack1_lengths is a global (non-static)
 * symbol in liblzmesh.a; declared extern here (not in lzmesh.h).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

unsigned lzmesh_pack1_lengths(const uint32_t *freq, unsigned nsym,
    unsigned maxlen, uint8_t *lens);

/* ==== oracle (ce3c3dc5, renamed) ==== */
unsigned orp1_w23(unsigned nsym, unsigned maxlen)
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

unsigned orp1_kraft_ok(const uint8_t *lens, unsigned nsym)
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

struct orp1_pmnode {
    uint64_t w;
    uint16_t l;
    uint16_t r;
};

#define ORP1_MAXN 256u
#define ORP1_MAXL 10u
#define ORP1_POOL (256u + 10u * 256u)
#define ORP1_MAXLIST 512u
#define ORP1_MAXTAKE (2u * 256u - 2u)

static unsigned orp1_pm_expand(const struct orp1_pmnode *pool,
    unsigned nleaf, unsigned idx, uint16_t *seq, unsigned seqcap)
{
    uint16_t st[12];
    unsigned sp = 0u;
    unsigned n = 0u;
    if (idx >= ORP1_POOL || seqcap == 0u)
        return 0u;
    st[sp++] = (uint16_t)idx;
    while (sp > 0u) {
        unsigned c = st[--sp];
        if (c >= ORP1_POOL)
            return 0u;
        if (pool[c].l == 0xFFFFu) {
            if (n >= seqcap || n >= ORP1_MAXN)
                return 0u;
            seq[n++] = pool[c].r;
        } else {
            if (sp + 2u > 12u)
                return 0u;
            st[sp++] = pool[c].r;
            st[sp++] = pool[c].l;
        }
    }
    (void)nleaf;
    return n;
}

static int orp1_pm_cmp(const struct orp1_pmnode *pool,
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

static void orp1_pm_sort(const struct orp1_pmnode *pool,
    unsigned nleaf, uint16_t *list, unsigned n)
{
    unsigned i;
    for (i = 1u; i < n; i++) {
        uint16_t x = list[i];
        unsigned j = i;
        while (j > 0u &&
            orp1_pm_cmp(pool, nleaf, x, list[j - 1u]) < 0) {
            list[j] = list[j - 1u];
            j--;
        }
        list[j] = x;
    }
}

static unsigned orp1_solve_trk(const uint32_t *freq, unsigned nsym,
    unsigned limit, uint8_t *lens, unsigned *take_sub)
{
    struct orp1_pmnode pool[ORP1_POOL];
    uint16_t cur[ORP1_MAXLIST];
    uint16_t nxt[ORP1_MAXLIST];
    uint16_t syms[ORP1_MAXN];
    uint16_t seq[ORP1_MAXN];
    unsigned n = 0u;
    unsigned nnode;
    unsigned ncur;
    unsigned lv;
    unsigned i;
    unsigned take;
    unsigned k;
    if (freq == 0 || lens == 0)
        return 0u;
    if (nsym == 0u || nsym > ORP1_MAXN)
        return 0u; /* 257+ keeps R1-stub behavior (decline). */
    if (limit == 0u || limit > ORP1_MAXL)
        return 0u;
    for (i = 0u; i < nsym; i++) {
        lens[i] = 0u;
        if (freq[i] != 0u) {
            if (n >= ORP1_MAXN)
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
    orp1_pm_sort(pool, n, cur, ncur); /* base by (freq, sym). */
    for (lv = 1u; lv < limit; lv++) {
        unsigned npk = 0u;
        unsigned nnxt;
        for (i = 0u; i + 1u < ncur; i += 2u) {
            if (nnode >= ORP1_POOL)
                return 0u;
            pool[nnode].w = pool[cur[i]].w + pool[cur[i + 1u]].w;
            pool[nnode].l = cur[i];
            pool[nnode].r = cur[i + 1u];
            if (npk >= ORP1_MAXLIST)
                return 0u;
            nxt[npk++] = (uint16_t)nnode;
            nnode++;
        }
        for (i = 0u; i < n; i++) {
            if (npk >= ORP1_MAXLIST)
                return 0u;
            nxt[npk++] = i; /* base leaves ascending. */
        }
        nnxt = npk;
        orp1_pm_sort(pool, n, nxt, nnxt);
        for (i = 0u; i < nnxt; i++)
            cur[i] = nxt[i];
        ncur = nnxt;
    }
    take = 2u * n - 2u;
    if (ncur < take)
        return 0u;
    for (k = 0u; k < take; k++) {
        unsigned cnt =
            orp1_pm_expand(pool, n, cur[k], seq, ORP1_MAXN);
        unsigned j;
        if (take_sub != 0 && k < ORP1_MAXTAKE)
            take_sub[k] = cnt + 1u; /* +1: 1=fail-class, 257=eq256 */
        if (cnt == 0u)
            goto fail;
        for (j = 0u; j < cnt; j++) {
            if (seq[j] >= nsym || lens[seq[j]] >= limit)
                goto fail;
            lens[seq[j]]++;
        }
    }
    for (i = 0u; i < nsym; i++) {
        if (freq[i] != 0u && (lens[i] == 0u || lens[i] > limit))
            goto fail;
        if (freq[i] == 0u && lens[i] != 0u)
            goto fail;
    }
    if (orp1_kraft_ok(lens, nsym) == 0u)
        goto fail;
    return 1u;
fail:
    for (i = 0u; i < nsym; i++)
        lens[i] = 0u;
    return 0u;
}

static unsigned orp1_lengths_trk(const uint32_t *freq, unsigned nsym,
    unsigned maxlen, uint8_t *lens, unsigned *take_sub, unsigned *natt)
{
    unsigned q;
    unsigned i;
    unsigned used = 0u;
    unsigned sole = 0u;
    unsigned att = 0u;
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
    q = orp1_w23(nsym, maxlen);
    if (q == 0u || q > maxlen)
        return 0u;
    for (;;) {
        att++;
        if (orp1_solve_trk(freq, nsym, q, lens,
            (att == 1u) ? take_sub : 0) != 0u) {
            if (natt != 0)
                *natt = att;
            return 1u;
        }
        if (q >= maxlen) {
            if (natt != 0)
                *natt = att;
            return 0u;
        }
        q <<= 1; /* overflow: double quantum and rebuild. */
        if (q > maxlen)
            q = maxlen;
    }
}
/* ==== end oracle ==== */

#define FMAX 1025u
#define NRAND 100000u

static uint64_t g_rng;

static uint64_t xnext(void)
{
    uint64_t x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return x * 2685821657736338717ULL;
}

static uint32_t r32(void)
{
    return (uint32_t)(xnext() >> 32);
}

static uint32_t fnv(const uint32_t *f, unsigned n)
{
    uint32_t h = 2166136261u;
    unsigned i;
    for (i = 0u; i < n; i++) {
        uint32_t v = f[i];
        unsigned b;
        for (b = 0u; b < 4u; b++) {
            h ^= (uint8_t)(v & 0xFFu);
            h *= 16777619u;
            v >>= 8;
        }
    }
    return h;
}

static void gen_freq(uint32_t *f, unsigned nsym, unsigned mode)
{
    static const uint32_t eqv[] = { 1u, 2u, 3u, 7u, 64u, 1000u };
    unsigned i;
    switch (mode & 7u) {
    case 0u: /* small uniform */
        for (i = 0u; i < nsym; i++)
            f[i] = 1u + r32() % 7u;
        break;
    case 1u: /* wide, some zeros */
        for (i = 0u; i < nsym; i++) {
            uint32_t r = r32();
            f[i] = ((r & 7u) == 0u) ? 0u : (r ^ (r >> 5) ^ 1u);
        }
        break;
    case 2u: /* all-equal (max ties) */
        for (i = 0u; i < nsym; i++)
            f[i] = eqv[r32() % 6u];
        break;
    case 3u: { /* sparse */
        unsigned k = 2u;
        if (nsym > 3u)
            k = 2u + r32() % (nsym < 16u ? nsym - 1u : 15u);
        for (i = 0u; i < nsym; i++)
            f[i] = 0u;
        while (k > 0u) {
            i = r32() % nsym;
            if (f[i] == 0u) {
                f[i] = 1u + r32() % 1000u;
                k--;
            }
        }
        break;
    }
    case 4u: /* skewed */
        for (i = 0u; i < nsym; i++) {
            uint32_t a = r32() % 1024u;
            uint32_t b = r32() % 1024u;
            f[i] = 1u + (a * b) / 8u;
        }
        break;
    case 5u: /* powers of two + jitter */
        for (i = 0u; i < nsym; i++)
            f[i] = (1u << (r32() % 20u)) + r32() % 3u;
        break;
    case 6u: /* binary */
        for (i = 0u; i < nsym; i++)
            f[i] = r32() & 1u;
        break;
    default: { /* two-valued mix (ties) */
        uint32_t a = 1u + r32() % 5u;
        uint32_t b = 1u + r32() % 500u;
        for (i = 0u; i < nsym; i++)
            f[i] = (r32() & 1u) ? a : b;
        break;
    }
    }
}

static uint32_t g_freq[FMAX];
static uint8_t g_lo[FMAX], g_ln[FMAX];
static unsigned g_take[ORP1_MAXTAKE];

static unsigned long g_cases, g_mm;
static unsigned long g_b_lt, g_b_eq, g_b_gt, g_b_na;
static unsigned long g_r00, g_r11;
static unsigned g_printed;
static unsigned g_maxsub;

static void one_case(unsigned nsym, unsigned maxlen, unsigned long tag)
{
    unsigned ro, rn, natt = 0u, i;
    unsigned mx = 0u, hasz = 0u;
    memset(g_take, 0, sizeof g_take);
    ro = orp1_lengths_trk(g_freq, nsym, maxlen, g_lo, g_take, &natt);
    rn = lzmesh_pack1_lengths(g_freq, nsym, maxlen, g_ln);
    if (ro == 0u && rn == 0u)
        g_r00++;
    else if (ro == 1u && rn == 1u)
        g_r11++;
    /* bucket from first-attempt take counts (stored +1: 1 = expand
     * fail, i.e. >256-count or structural; 257 = exactly-256). */
    if (natt == 0u) {
        g_b_na++;
    } else {
        unsigned fail = 0u;
        for (i = 0u; i < ORP1_MAXTAKE; i++) {
            if (g_take[i] == 0u)
                continue;
            hasz = 1u;
            if (g_take[i] == 1u)
                fail = 1u;
            else if (g_take[i] > mx)
                mx = g_take[i];
        }
        if (hasz == 0u || fail != 0u) {
            g_b_gt++;
        } else if (mx == 257u) {
            g_b_eq++;
            g_maxsub = 256u;
        } else {
            g_b_lt++;
            if (mx > 0u && mx - 1u > g_maxsub)
                g_maxsub = mx - 1u;
        }
    }
    if (ro != rn || (ro == 1u && memcmp(g_lo, g_ln, nsym) != 0)) {
        unsigned di = 0u;
        g_mm++;
        if (g_printed >= 5u)
            return;
        g_printed++;
        if (ro == 1u && rn == 1u) {
            for (i = 0u; i < nsym; i++) {
                if (g_lo[i] != g_ln[i]) {
                    di = i;
                    break;
                }
            }
        }
        printf("MM #%lu tag=%lu nsym=%u maxlen=%u or=%u new=%u natt=%u "
            "mxsub=%u fdiff=%u fnv=%08x\n",
            g_mm, tag, nsym, maxlen, ro, rn, natt, mx ? mx - 1u : 0u, di,
            fnv(g_freq, nsym));
        printf("  freq:");
        for (i = 0u; i < nsym && i < 64u; i++)
            printf(" %u", g_freq[i]);
        printf("%s\n", nsym > 64u ? " ..." : "");
    }
}

int main(void)
{
    static const unsigned cN[] =
        { 64u, 128u, 192u, 200u, 224u, 240u, 248u, 252u, 254u, 255u, 256u };
    static const uint32_t cV[] = { 1u, 2u, 3u };
    static const unsigned eN[] = { 0u, 1u, 257u, 300u, 512u, 1024u, 1025u };
    static const unsigned eM[] = { 0u, 1u, 4u, 6u, 9u, 11u, 16u };
    unsigned long t;
    unsigned a, b, i;
    /* 1. random fuzz: nsym/mode/maxlen cycle, 2 seeds x NRAND/2. */
    for (t = 0u; t < NRAND; t++) {
        unsigned nsym, mode, maxlen;
        if (t == 0u || t == NRAND / 2u)
            g_rng = 0x12345678ABCDEF01ULL + t;
        mode = (unsigned)(t & 7u);
        maxlen = (t & 8u) ? 5u : 10u;
        if ((t & 15u) < 6u)
            nsym = 2u + (unsigned)(t % 255u); /* full 2..256 sweep */
        else
            nsym = 2u + r32() % 255u;
        gen_freq(g_freq, nsym, mode);
        one_case(nsym, maxlen, t);
        g_cases++;
    }
    /* 2. crafted: big-n all-equal (tie-heavy, 256-subtree prone). */
    for (a = 0u; a < 11u; a++) {
        for (b = 0u; b < 3u; b++) {
            for (i = 0u; i < cN[a]; i++)
                g_freq[i] = cV[b];
            one_case(cN[a], 10u, 1000000ul + a * 10u + b);
            g_cases++;
            one_case(cN[a], 5u, 2000000ul + a * 10u + b);
            g_cases++;
        }
    }
    /* 3. crafted: spike / ramp / pairs / maxval. */
    for (a = 0u; a < 11u; a++) {
        unsigned n = cN[a];
        for (i = 0u; i < n; i++)
            g_freq[i] = 1u;
        g_freq[0] = 1000000u;
        one_case(n, 10u, 3000000ul + a);
        g_cases++;
        for (i = 0u; i < n; i++)
            g_freq[i] = i + 1u;
        one_case(n, 10u, 3100000ul + a);
        g_cases++;
        for (i = 0u; i < n; i++)
            g_freq[i] = 1u + (i & 1u);
        one_case(n, 10u, 3200000ul + a);
        g_cases++;
        for (i = 0u; i < n; i++)
            g_freq[i] = 0xFFFFFFFFu - i;
        one_case(n, 10u, 3300000ul + a);
        g_cases++;
    }
    /* 4. edge sweeps: degenerate nsym/maxlen decline paths. */
    for (a = 0u; a < 7u; a++) {
        unsigned n = eN[a] < FMAX ? eN[a] : 0u;
        if (n > 1u) {
            for (i = 0u; i < n; i++)
                g_freq[i] = 1u + (i % 3u);
        } else if (n == 1u) {
            g_freq[0] = 42u;
        }
        one_case(n, 10u, 4000000ul + a);
        g_cases++;
        one_case(n, 5u, 4100000ul + a);
        g_cases++;
    }
    for (a = 0u; a < 7u; a++) {
        for (i = 0u; i < 200u; i++)
            g_freq[i] = 1u + r32() % 9u;
        g_rng = 999u + a;
        one_case(200u, eM[a], 4200000ul + a);
        g_cases++;
    }
    /* used0/used1 pins. */
    memset(g_freq, 0, sizeof g_freq);
    one_case(256u, 10u, 5000001ul);
    g_cases++;
    g_freq[17] = 7u;
    one_case(256u, 10u, 5000002ul);
    g_cases++;
    one_case(256u, 5u, 5000003ul);
    g_cases++;
    printf("pm-diff: cases=%lu mm=%lu r00=%lu r11=%lu "
        "sub_lt=%lu sub_eq256=%lu sub_gt=%lu na=%lu maxsub=%u\n",
        g_cases, g_mm, g_r00, g_r11, g_b_lt, g_b_eq, g_b_gt, g_b_na,
        g_maxsub);
    if (g_mm != 0u) {
        printf("FAIL  pm-diff :: %lu mismatches / %lu cases\n",
            g_mm, g_cases);
        return 1;
    }
    if (g_maxsub < 250u) {
        printf("FAIL  pm-diff :: near-boundary blind (maxsub=%u)\n",
            g_maxsub);
        return 1;
    }
    printf("PASS  pm-diff %lu cases 0 mismatch (maxsub=%u eq256=%lu)\n",
        g_cases, g_maxsub, g_b_eq);
    return 0;
}
