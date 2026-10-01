/*
 * bigint.c - exact unsigned multiprecision arithmetic (see bigint.h).
 *
 * Limb loops use only 32x32->64 multiplication, 64-bit addition and
 * 64-by-32 division, which wasm32 executes natively (i64.mul, i64.div_u);
 * no 128-bit product or overflow idiom that would need compiler-rt.
 */

#include "bigint.h"

#define BI_F64_INF_BITS UINT64_C(0x7FF0000000000000)
#define BI_F64_ONE_BITS UINT64_C(0x3FF0000000000000)

/* ================================================================ helpers */

static inline uint32_t *bi_mut(bigint *x) {
    return x->cap ? x->u.heap : x->u.small;
}

static inline uint32_t bi_capacity(const bigint *x) {
    return x->cap ? x->cap : BI_INLINE_LIMBS;
}

/* Leading zero bits of a nonzero limb. */
static uint32_t bi_clz32(uint32_t x) {
    uint32_t n = 0;
    if (x <= 0x0000FFFFu) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFFu) { n += 8; x <<= 8; }
    if (x <= 0x0FFFFFFFu) { n += 4; x <<= 4; }
    if (x <= 0x3FFFFFFFu) { n += 2; x <<= 2; }
    if (x <= 0x7FFFFFFFu) n += 1;
    return n;
}

static uint32_t bi_bits32(uint32_t x) {
    return x ? 32u - bi_clz32(x) : 0u;
}

static uint32_t bi_bits64(uint64_t x) {
    uint32_t hi = (uint32_t)(x >> 32);
    return hi ? 32u + bi_bits32(hi) : bi_bits32((uint32_t)x);
}

/* Limbs holding a value of `bits` bits. Bounds below use bit lengths, not
 * limb counts, so results that fit two limbs stay inline. */
static uint64_t bi_limbs_for(uint64_t bits) {
    return (bits + 31u) / 32u;
}

static void bi_normalize(bigint *x, uint32_t len) {
    const uint32_t *l = bi_limbs(x);
    while (len > 0 && l[len - 1] == 0) len--;
    x->len = len;
}

static void bi_copy_limbs(uint32_t *dst, const uint32_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static void bi_zero_limbs(uint32_t *dst, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = 0;
}

static double bi_f64(uint64_t bits) {
    union {
        uint64_t u;
        double d;
    } pun;
    pun.u = bits;
    return pun.d;
}

/* ================================================================ limb kernels
 * r may equal a in the single-limb kernels (each limb is read before it is
 * written); multi-limb products need distinct storage. */

/* r[0..n) = a[0..n) * m; returns the carry limb. */
static uint32_t bi_mul_1(uint32_t *r, const uint32_t *a, uint32_t n, uint32_t m) {
    uint64_t carry = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t t = (uint64_t)a[i] * m + carry;
        r[i] = (uint32_t)t;
        carry = t >> 32;
    }
    return (uint32_t)carry;
}

/* r[0..n) += a[0..n) * m; returns the carry limb. */
static uint32_t bi_addmul_1(uint32_t *r, const uint32_t *a, uint32_t n, uint32_t m) {
    uint64_t carry = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t t = (uint64_t)a[i] * m + r[i] + carry; /* <= 2^64 - 1 */
        r[i] = (uint32_t)t;
        carry = t >> 32;
    }
    return (uint32_t)carry;
}

/* r[0..n) -= a[0..n) * m (mod 2^(32n)); returns the limb still owed at r[n].
 * The owed amount stays below 2^32 (r_old - a*m >= -a*m > -2^(32(n+1))). */
static uint32_t bi_submul_1(uint32_t *r, const uint32_t *a, uint32_t n, uint32_t m) {
    uint64_t carry = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t p = (uint64_t)a[i] * m + carry;
        uint32_t low = (uint32_t)p;
        uint32_t ri = r[i];
        carry = (p >> 32) + (ri < low ? 1u : 0u);
        r[i] = ri - low;
    }
    return (uint32_t)carry;
}

/* Adds c at r[k], propagating through r[k..n); returns the carry out of r[n-1]. */
static uint32_t bi_add_carry(uint32_t *r, uint32_t k, uint32_t n, uint32_t c) {
    while (c != 0 && k < n) {
        uint64_t s = (uint64_t)r[k] + c;
        r[k] = (uint32_t)s;
        c = (uint32_t)(s >> 32);
        k++;
    }
    return c;
}

/* Subtracts c at r[k], propagating through r[k..n); returns the borrow out. */
static uint32_t bi_sub_borrow(uint32_t *r, uint32_t k, uint32_t n, uint32_t c) {
    while (c != 0 && k < n) {
        uint32_t rk = r[k];
        r[k] = rk - c;
        c = rk < c ? 1u : 0u;
        k++;
    }
    return c;
}

/* r[0..an+bn) = a * b; r must not overlap a or b (a may equal b). */
static void bi_mul_core(uint32_t *r, const uint32_t *a, uint32_t an, const uint32_t *b,
                        uint32_t bn) {
    bi_zero_limbs(r, an);
    for (uint32_t j = 0; j < bn; j++) {
        uint32_t bj = b[j];
        r[j + an] = bj ? bi_addmul_1(r + j, a, an, bj) : 0u;
    }
}

/* q[0..n) = a[0..n) / d (q may equal a); returns a % d. d != 0. */
static uint32_t bi_divrem_1(uint32_t *q, const uint32_t *a, uint32_t n, uint32_t d) {
    uint64_t rem = 0;
    for (uint32_t i = n; i-- > 0;) {
        uint64_t cur = (rem << 32) | a[i];
        uint64_t digit = cur / d; /* < 2^32 because rem < d */
        q[i] = (uint32_t)digit;
        rem = cur - digit * d;
    }
    return (uint32_t)rem;
}

static uint32_t bi_mod_1(const uint32_t *a, uint32_t n, uint32_t d) {
    uint64_t rem = 0;
    for (uint32_t i = n; i-- > 0;) rem = ((rem << 32) | a[i]) % d;
    return (uint32_t)rem;
}

/* dst[0..dlen) = src[0..slen) << bits, zero filled; the value must fit.
 * dst must not overlap src. */
static void bi_shl_into(uint32_t *dst, uint32_t dlen, const uint32_t *src, uint32_t slen,
                        uint64_t bits) {
    uint32_t ls = (uint32_t)(bits >> 5);
    uint32_t bs = (uint32_t)(bits & 31u);
    bi_zero_limbs(dst, dlen);
    for (uint32_t i = 0; i < slen; i++) {
        uint64_t v = (uint64_t)src[i] << bs;
        dst[i + ls] |= (uint32_t)v;
        if (i + ls + 1 < dlen) dst[i + ls + 1] |= (uint32_t)(v >> 32);
    }
}

/* Knuth's algorithm D (TAOCP 4.3.1) with 32-bit digits. un[0..m+n] holds
 * the normalized dividend (top limb possibly 0), vn[0..n) the normalized
 * divisor (n >= 2, top bit of vn[n-1] set). Writes q[0..m] and leaves the
 * normalized remainder in un[0..n). */
static void bi_div_knuth(uint32_t *q, uint32_t *un, const uint32_t *vn, uint32_t m, uint32_t n) {
    const uint64_t base = (uint64_t)1 << 32;
    uint64_t vtop = vn[n - 1];
    uint64_t vnext = vn[n - 2];
    for (uint32_t j = m + 1; j-- > 0;) {
        uint64_t top = ((uint64_t)un[j + n] << 32) | un[j + n - 1];
        uint64_t qhat = top / vtop;
        uint64_t rhat = top - qhat * vtop;
        /* qhat < base is tested first, so qhat * vnext cannot overflow;
         * rhat < base inside the loop, so rhat << 32 cannot either. */
        while (qhat >= base || qhat * vnext > ((rhat << 32) | un[j + n - 2])) {
            qhat--;
            rhat += vtop;
            if (rhat >= base) break;
        }
        /* un[j..j+n] -= qhat * vn; qhat < base here. */
        uint64_t carry = 0;
        uint32_t borrow = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t p = qhat * vn[i] + carry;
            carry = p >> 32;
            uint64_t t = (uint64_t)un[i + j] - (uint32_t)p - borrow;
            un[i + j] = (uint32_t)t;
            borrow = (uint32_t)(t >> 63);
        }
        uint64_t t = (uint64_t)un[j + n] - carry - borrow;
        un[j + n] = (uint32_t)t;
        if (t >> 63) {
            /* qhat was one too large (probability ~2/base): add back. */
            qhat--;
            uint64_t c = 0;
            for (uint32_t i = 0; i < n; i++) {
                uint64_t s = (uint64_t)un[i + j] + vn[i] + c;
                un[i + j] = (uint32_t)s;
                c = s >> 32;
            }
            un[j + n] += (uint32_t)c;
        }
        q[j] = (uint32_t)qhat;
    }
}

/* ================================================================ pool */

typedef struct bi_free_node {
    struct bi_free_node *next;
} bi_free_node;

/* Free-list class of a block holding `limbs` (4 << class limbs). */
static uint32_t bi_class_of(uint32_t limbs) {
    uint32_t k = 0;
    uint32_t cap = BI_MIN_HEAP_LIMBS;
    while (cap < limbs) {
        cap <<= 1;
        k++;
    }
    return k;
}

/* A block of at least `limbs` limbs (uninitialized). Nothing changes on
 * failure. */
static ms_status bi_block_take(bi_pool *pool, uint64_t limbs, uint32_t **block, uint32_t *cap) {
    if (pool == NULL || pool->mem == NULL) return MS_ERR_INTERNAL;
    if (limbs > BI_MAX_LIMBS) return MS_ERR_RESOURCE_EXHAUSTED;
    uint32_t k = bi_class_of((uint32_t)limbs);
    uint32_t size = BI_MIN_HEAP_LIMBS << k;
    bi_free_node *node = (bi_free_node *)pool->free_lists[k];
    if (node != NULL) {
        pool->free_lists[k] = node->next;
        pool->cached--;
        pool->reused++;
        pool->outstanding++;
        *block = (uint32_t *)(void *)node;
        *cap = size;
        return MS_OK;
    }
    uint64_t misuses = pool->mem->misuses;
    void *fresh = rt_bump(pool->mem, (size_t)size * sizeof(uint32_t));
    if (fresh == NULL) {
        return pool->mem->misuses != misuses ? MS_ERR_INTERNAL : MS_ERR_RESOURCE_EXHAUSTED;
    }
    pool->carved++;
    pool->outstanding++;
    *block = (uint32_t *)fresh;
    *cap = size;
    return MS_OK;
}

/* Recycles a block (rt_bump blocks are 16-byte aligned and >= 16 bytes, so
 * the link fits). Without a pool the block is only forgotten. */
static void bi_block_give(bi_pool *pool, uint32_t *block, uint32_t cap) {
    if (pool == NULL) return;
    uint32_t k = bi_class_of(cap);
    bi_free_node *node = (bi_free_node *)(void *)block;
    node->next = (bi_free_node *)pool->free_lists[k];
    pool->free_lists[k] = node;
    pool->cached++;
    pool->outstanding--;
}

/* Capacity >= need, keeping x's value. Nothing changes on failure. */
static ms_status bi_grow(bi_pool *pool, bigint *x, uint64_t need) {
    if (need <= bi_capacity(x)) return MS_OK;
    uint32_t *block;
    uint32_t cap;
    ms_status status = bi_block_take(pool, need, &block, &cap);
    if (status != MS_OK) return status;
    bi_copy_limbs(block, bi_limbs(x), x->len);
    if (x->cap) bi_block_give(pool, x->u.heap, x->cap);
    x->u.heap = block;
    x->cap = cap;
    return MS_OK;
}

/* Replaces x's storage by `block` (x's old block is released). */
static void bi_adopt(bi_pool *pool, bigint *x, uint32_t *block, uint32_t cap, uint32_t len) {
    if (x->cap) bi_block_give(pool, x->u.heap, x->cap);
    x->u.heap = block;
    x->cap = cap;
    bi_normalize(x, len);
}

void bi_pool_init(bi_pool *pool, rt_mem *mem) {
    pool->mem = mem;
    bi_pool_reset(pool);
}

void bi_pool_reset(bi_pool *pool) {
    for (uint32_t k = 0; k < BI_POOL_CLASSES; k++) pool->free_lists[k] = NULL;
    pool->outstanding = 0;
    pool->cached = 0;
    pool->carved = 0;
    pool->reused = 0;
}

/* ================================================================ lifetime */

void bi_init(bigint *x) {
    x->len = 0;
    x->cap = 0;
    x->u.small[0] = 0;
    x->u.small[1] = 0;
}

void bi_free(bi_pool *pool, bigint *x) {
    if (x->cap) bi_block_give(pool, x->u.heap, x->cap);
    bi_init(x);
}

void bi_move(bi_pool *pool, bigint *dst, bigint *src) {
    if (dst == src) return;
    if (dst->cap) bi_block_give(pool, dst->u.heap, dst->cap);
    *dst = *src;
    bi_init(src);
}

void bi_swap(bigint *a, bigint *b) {
    bigint t = *a;
    *a = *b;
    *b = t;
}

ms_status bi_reserve(bi_pool *pool, bigint *x, uint32_t limbs) {
    return bi_grow(pool, x, limbs);
}

/* ================================================================ values */

void bi_set_zero(bigint *x) {
    x->len = 0;
}

void bi_set_u64(bigint *x, uint64_t value) {
    uint32_t *l = bi_mut(x); /* every capacity holds two limbs */
    l[0] = (uint32_t)value;
    l[1] = (uint32_t)(value >> 32);
    x->len = (value >> 32) ? 2u : value ? 1u : 0u;
}

ms_status bi_set_limbs(bi_pool *pool, bigint *x, const uint32_t *limbs, uint32_t count) {
    while (count > 0 && limbs[count - 1] == 0) count--;
    ms_status status = bi_grow(pool, x, count);
    if (status != MS_OK) return status;
    bi_copy_limbs(bi_mut(x), limbs, count);
    x->len = count;
    return MS_OK;
}

ms_status bi_copy(bi_pool *pool, bigint *dst, const bigint *src) {
    if (dst == src) return MS_OK;
    ms_status status = bi_grow(pool, dst, src->len);
    if (status != MS_OK) return status;
    bi_copy_limbs(bi_mut(dst), bi_limbs(src), src->len);
    dst->len = src->len;
    return MS_OK;
}

bool bi_get_u64(const bigint *x, uint64_t *out) {
    if (x->len > 2) return false;
    const uint32_t *l = bi_limbs(x);
    uint64_t value = 0;
    if (x->len > 0) value = l[0];
    if (x->len > 1) value |= (uint64_t)l[1] << 32;
    *out = value;
    return true;
}

int bi_cmp(const bigint *a, const bigint *b) {
    if (a->len != b->len) return a->len < b->len ? -1 : 1;
    const uint32_t *al = bi_limbs(a);
    const uint32_t *bl = bi_limbs(b);
    for (uint32_t i = a->len; i-- > 0;) {
        if (al[i] != bl[i]) return al[i] < bl[i] ? -1 : 1;
    }
    return 0;
}

bool bi_eq(const bigint *a, const bigint *b) {
    return bi_cmp(a, b) == 0;
}

uint32_t bi_bit_length(const bigint *x) {
    if (x->len == 0) return 0;
    return (x->len - 1) * 32u + bi_bits32(bi_limbs(x)[x->len - 1]);
}

/* ================================================================ arithmetic */

ms_status bi_add(bi_pool *pool, bigint *out, const bigint *a, const bigint *b) {
    if (a->len < b->len) {
        const bigint *t = a;
        a = b;
        b = t;
    }
    uint32_t an = a->len;
    uint32_t bn = b->len;
    if (bn == 0) return bi_copy(pool, out, a);
    uint32_t abits = bi_bit_length(a);
    uint32_t bbits = bi_bit_length(b); /* equal lengths: either may be longer */
    uint64_t n = bi_limbs_for((uint64_t)(abits > bbits ? abits : bbits) + 1); /* an or an + 1 */
    ms_status status = bi_grow(pool, out, n);
    if (status != MS_OK) return status;
    const uint32_t *al = bi_limbs(a); /* after bi_grow: out may be a or b */
    const uint32_t *bl = bi_limbs(b);
    uint32_t *r = bi_mut(out);
    uint64_t carry = 0;
    uint32_t i = 0;
    for (; i < bn; i++) {
        uint64_t s = (uint64_t)al[i] + bl[i] + carry;
        r[i] = (uint32_t)s;
        carry = s >> 32;
    }
    for (; i < an; i++) {
        uint64_t s = (uint64_t)al[i] + carry;
        r[i] = (uint32_t)s;
        carry = s >> 32;
    }
    if (n > an) r[an] = (uint32_t)carry; /* otherwise the carry is 0 */
    bi_normalize(out, (uint32_t)n);
    return MS_OK;
}

ms_status bi_sub(bi_pool *pool, bigint *out, const bigint *a, const bigint *b) {
    if (bi_cmp(a, b) < 0) return MS_ERR_INTERNAL;
    uint32_t an = a->len;
    uint32_t bn = b->len;
    ms_status status = bi_grow(pool, out, an);
    if (status != MS_OK) return status;
    const uint32_t *al = bi_limbs(a);
    const uint32_t *bl = bi_limbs(b);
    uint32_t *r = bi_mut(out);
    uint32_t borrow = 0;
    uint32_t i = 0;
    for (; i < bn; i++) {
        uint64_t d = (uint64_t)al[i] - bl[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (uint32_t)(d >> 63);
    }
    for (; i < an; i++) {
        uint64_t d = (uint64_t)al[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (uint32_t)(d >> 63);
    }
    bi_normalize(out, an);
    return MS_OK;
}

ms_status bi_mul(bi_pool *pool, bigint *out, const bigint *a, const bigint *b) {
    uint32_t an = a->len;
    uint32_t bn = b->len;
    if (an == 0 || bn == 0) {
        bi_set_zero(out);
        return MS_OK;
    }
    if ((uint64_t)bi_bit_length(a) + bi_bit_length(b) <= 64) {
        uint64_t av = 0;
        uint64_t bv = 0;
        bi_get_u64(a, &av);
        bi_get_u64(b, &bv);
        bi_set_u64(out, av * bv); /* < 2^64 by the bit lengths */
        return MS_OK;
    }
    uint32_t n = an + bn;
    if (out != a && out != b && n <= bi_capacity(out)) {
        bi_mul_core(bi_mut(out), bi_limbs(a), an, bi_limbs(b), bn);
        bi_normalize(out, n);
        return MS_OK;
    }
    uint32_t *block;
    uint32_t cap;
    ms_status status = bi_block_take(pool, n, &block, &cap);
    if (status != MS_OK) return status;
    bi_mul_core(block, bi_limbs(a), an, bi_limbs(b), bn);
    bi_adopt(pool, out, block, cap, n);
    return MS_OK;
}

ms_status bi_mul_u32(bi_pool *pool, bigint *out, const bigint *a, uint32_t m) {
    uint32_t an = a->len;
    if (an == 0 || m == 0) {
        bi_set_zero(out);
        return MS_OK;
    }
    uint64_t n = bi_limbs_for((uint64_t)bi_bit_length(a) + bi_bits32(m)); /* an or an + 1 */
    ms_status status = bi_grow(pool, out, n);
    if (status != MS_OK) return status;
    uint32_t *r = bi_mut(out);
    uint32_t carry = bi_mul_1(r, bi_limbs(a), an, m);
    if (n > an) r[an] = carry; /* otherwise the carry is 0 */
    bi_normalize(out, (uint32_t)n);
    return MS_OK;
}

/* acc += a * b through a temporary product (acc is a or b). Locals use
 * bi_init rather than an aggregate initializer, which a compiler may lower
 * to a memory-helper call: the library sources stay free of such calls. */
static ms_status bi_addmul_via_temp(bi_pool *pool, bigint *acc, const bigint *a, const bigint *b) {
    bigint product;
    bi_init(&product);
    ms_status status = bi_mul(pool, &product, a, b);
    if (status == MS_OK) status = bi_add(pool, acc, acc, &product);
    bi_free(pool, &product);
    return status;
}

ms_status bi_addmul(bi_pool *pool, bigint *acc, const bigint *a, const bigint *b) {
    uint32_t an = a->len;
    uint32_t bn = b->len;
    if (an == 0 || bn == 0) return MS_OK;
    if (acc == a || acc == b) return bi_addmul_via_temp(pool, acc, a, b);
    uint32_t len = acc->len;
    uint64_t accbits = bi_bit_length(acc);
    uint64_t pbits = (uint64_t)bi_bit_length(a) + bi_bit_length(b);
    /* acc + a*b < 2^(max + 1); n >= an + bn - 1, so every row fits. */
    uint64_t n = bi_limbs_for((accbits > pbits ? accbits : pbits) + 1);
    ms_status status = bi_grow(pool, acc, n);
    if (status != MS_OK) return status;
    uint32_t *r = bi_mut(acc);
    const uint32_t *al = bi_limbs(a);
    const uint32_t *bl = bi_limbs(b);
    bi_zero_limbs(r + len, (uint32_t)n - len);
    for (uint32_t j = 0; j < bn; j++) {
        if (bl[j] == 0) continue;
        uint32_t carry = bi_addmul_1(r + j, al, an, bl[j]);
        bi_add_carry(r, j + an, (uint32_t)n, carry); /* partial sums < 2^(32n) */
    }
    bi_normalize(acc, (uint32_t)n);
    return MS_OK;
}

ms_status bi_addmul_u32(bi_pool *pool, bigint *acc, const bigint *a, uint32_t m) {
    uint32_t an = a->len;
    if (an == 0 || m == 0) return MS_OK;
    if (acc == a) {
        bigint product;
        bi_init(&product);
        ms_status status = bi_mul_u32(pool, &product, a, m);
        if (status == MS_OK) status = bi_add(pool, acc, acc, &product);
        bi_free(pool, &product);
        return status;
    }
    uint32_t len = acc->len;
    uint64_t accbits = bi_bit_length(acc);
    uint64_t pbits = (uint64_t)bi_bit_length(a) + bi_bits32(m);
    uint64_t n = bi_limbs_for((accbits > pbits ? accbits : pbits) + 1); /* >= an */
    ms_status status = bi_grow(pool, acc, n);
    if (status != MS_OK) return status;
    uint32_t *r = bi_mut(acc);
    bi_zero_limbs(r + len, (uint32_t)n - len);
    uint32_t carry = bi_addmul_1(r, bi_limbs(a), an, m);
    bi_add_carry(r, an, (uint32_t)n, carry);
    bi_normalize(acc, (uint32_t)n);
    return MS_OK;
}

ms_status bi_submul(bi_pool *pool, bigint *acc, const bigint *a, const bigint *b) {
    uint32_t an = a->len;
    uint32_t bn = b->len;
    if (an == 0 || bn == 0) return MS_OK;
    if (acc == a || acc == b) {
        bigint product;
        bi_init(&product);
        ms_status status = bi_mul(pool, &product, a, b);
        if (status == MS_OK) status = bi_sub(pool, acc, acc, &product);
        bi_free(pool, &product);
        return status;
    }
    uint32_t n = acc->len;
    /* a * b >= 2^(32(an + bn - 2)): beyond n limbs it exceeds acc. */
    if (an + bn - 1 > n) return MS_ERR_INTERNAL;
    uint32_t *r = bi_mut(acc);
    const uint32_t *al = bi_limbs(a);
    const uint32_t *bl = bi_limbs(b);
    /* Row j subtracts a * b[j] * 2^(32j) modulo 2^(32n) (j + an <= n); any
     * borrow out of the top means acc < a * b. Adding the rows back modulo
     * 2^(32n) then restores acc exactly. */
    for (uint32_t j = 0; j < bn; j++) {
        if (bl[j] == 0) continue;
        uint32_t owed = bi_submul_1(r + j, al, an, bl[j]);
        uint32_t k = j + an;
        bool under = k < n ? bi_sub_borrow(r, k, n, owed) != 0 : owed != 0;
        if (under) {
            for (uint32_t i = 0; i <= j; i++) {
                if (bl[i] == 0) continue;
                uint32_t carry = bi_addmul_1(r + i, al, an, bl[i]);
                bi_add_carry(r, i + an, n, carry);
            }
            return MS_ERR_INTERNAL;
        }
    }
    bi_normalize(acc, n);
    return MS_OK;
}

ms_status bi_shl(bi_pool *pool, bigint *out, const bigint *a, uint32_t bits) {
    uint32_t an = a->len;
    if (an == 0) {
        bi_set_zero(out);
        return MS_OK;
    }
    uint32_t ls = bits >> 5;
    uint32_t bs = bits & 31u;
    uint64_t n = bi_limbs_for((uint64_t)bi_bit_length(a) + bits); /* an + ls or one more */
    if (n > BI_MAX_LIMBS) return MS_ERR_RESOURCE_EXHAUSTED;
    ms_status status = bi_grow(pool, out, n);
    if (status != MS_OK) return status;
    const uint32_t *s = bi_limbs(a);
    uint32_t *r = bi_mut(out);
    /* Top down, so shifting in place (out == a) reads limbs before
     * overwriting them. */
    if (bs == 0) {
        for (uint32_t i = an; i-- > 0;) r[i + ls] = s[i];
    } else {
        if (n > (uint64_t)an + ls) r[an + ls] = s[an - 1] >> (32u - bs);
        for (uint32_t i = an - 1; i > 0; i--) r[i + ls] = (s[i] << bs) | (s[i - 1] >> (32u - bs));
        r[ls] = s[0] << bs;
    }
    bi_zero_limbs(r, ls);
    bi_normalize(out, (uint32_t)n);
    return MS_OK;
}

ms_status bi_pow2(bi_pool *pool, bigint *out, uint32_t bits) {
    uint64_t n = (uint64_t)(bits >> 5) + 1;
    if (n > BI_MAX_LIMBS) return MS_ERR_RESOURCE_EXHAUSTED;
    ms_status status = bi_grow(pool, out, n);
    if (status != MS_OK) return status;
    uint32_t *r = bi_mut(out);
    bi_zero_limbs(r, (uint32_t)n - 1);
    r[n - 1] = (uint32_t)1 << (bits & 31u);
    out->len = (uint32_t)n;
    return MS_OK;
}

/* ================================================================ division */

ms_status bi_divmod_u32(bi_pool *pool, bigint *q, const bigint *a, uint32_t d, uint32_t *rem) {
    if (d == 0) return MS_ERR_INTERNAL;
    uint32_t an = a->len;
    if (q == NULL) {
        if (rem) *rem = bi_mod_1(bi_limbs(a), an, d);
        return MS_OK;
    }
    ms_status status = bi_grow(pool, q, an);
    if (status != MS_OK) return status;
    uint32_t r = bi_divrem_1(bi_mut(q), bi_limbs(a), an, d);
    bi_normalize(q, an);
    if (rem) *rem = r;
    return MS_OK;
}

ms_status bi_divexact_u32(bi_pool *pool, bigint *q, const bigint *a, uint32_t d) {
    if (d == 0) return MS_ERR_INTERNAL;
    if (q != a) {
        if (bi_mod_1(bi_limbs(a), a->len, d) != 0) return MS_ERR_INTERNAL;
        return bi_divmod_u32(pool, q, a, d, NULL);
    }
    /* In place; an inexact division is undone as q * d + r == a. */
    uint32_t n = q->len;
    uint32_t *l = bi_mut(q);
    uint32_t r = bi_divrem_1(l, l, n, d);
    if (r != 0) {
        uint64_t carry = r;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t t = (uint64_t)l[i] * d + carry;
            l[i] = (uint32_t)t;
            carry = t >> 32;
        }
        return MS_ERR_INTERNAL;
    }
    bi_normalize(q, n);
    return MS_OK;
}

/* Long division of a >= d by a divisor of >= 2 limbs (Knuth D on scratch
 * copies). Writes q (may be NULL) and r (may be NULL). With `exact` a
 * nonzero remainder is MS_ERR_INTERNAL. Outputs keep their values on
 * failure. */
static ms_status bi_divmod_long(bi_pool *pool, bigint *q, bigint *r, const bigint *a,
                                const bigint *d, bool exact) {
    uint32_t an = a->len;
    uint32_t n = d->len;
    uint32_t m = an - n;
    uint32_t *block;
    uint32_t cap;
    ms_status status = bi_block_take(pool, (uint64_t)an + 1 + n + m + 1, &block, &cap);
    if (status != MS_OK) return status;
    if (q) status = bi_grow(pool, q, (uint64_t)m + 1);
    if (status == MS_OK && r) status = bi_grow(pool, r, n);
    if (status != MS_OK) {
        bi_block_give(pool, block, cap);
        return status;
    }
    uint32_t *un = block;
    uint32_t *vn = un + an + 1;
    uint32_t *qn = vn + n;
    const uint32_t *dl = bi_limbs(d); /* after bi_grow: q or r may be a or d */
    uint32_t shift = bi_clz32(dl[n - 1]);
    bi_shl_into(un, an + 1, bi_limbs(a), an, shift);
    bi_shl_into(vn, n, dl, n, shift);
    bi_div_knuth(qn, un, vn, m, n);
    if (exact) {
        for (uint32_t i = 0; i < n; i++) {
            if (un[i] != 0) {
                bi_block_give(pool, block, cap);
                return MS_ERR_INTERNAL;
            }
        }
    }
    if (q) {
        bi_copy_limbs(bi_mut(q), qn, m + 1);
        bi_normalize(q, m + 1);
    }
    if (r) {
        uint32_t *rl = bi_mut(r);
        if (shift == 0) {
            bi_copy_limbs(rl, un, n);
        } else {
            for (uint32_t i = 0; i + 1 < n; i++) rl[i] = (un[i] >> shift) | (un[i + 1] << (32u - shift));
            rl[n - 1] = un[n - 1] >> shift;
        }
        bi_normalize(r, n);
    }
    bi_block_give(pool, block, cap);
    return MS_OK;
}

ms_status bi_divmod(bi_pool *pool, bigint *q, bigint *r, const bigint *a, const bigint *d) {
    if (d->len == 0) return MS_ERR_INTERNAL;
    if (q != NULL && q == r) return MS_ERR_INTERNAL;
    if (bi_cmp(a, d) < 0) {
        if (r) {
            ms_status status = bi_copy(pool, r, a);
            if (status != MS_OK) return status;
        }
        if (q) bi_set_zero(q);
        return MS_OK;
    }
    if (d->len == 1) {
        uint32_t divisor = bi_limbs(d)[0];
        uint32_t rem;
        ms_status status = bi_divmod_u32(pool, q, a, divisor, &rem);
        if (status != MS_OK) return status;
        if (r) bi_set_u64(r, rem);
        return MS_OK;
    }
    return bi_divmod_long(pool, q, r, a, d, false);
}

ms_status bi_divexact(bi_pool *pool, bigint *q, const bigint *a, const bigint *d) {
    if (d->len == 0) return MS_ERR_INTERNAL;
    if (a->len == 0) {
        bi_set_zero(q);
        return MS_OK;
    }
    if (bi_cmp(a, d) < 0) return MS_ERR_INTERNAL; /* 0 < a < d */
    if (d->len == 1) return bi_divexact_u32(pool, q, a, bi_limbs(d)[0]);
    return bi_divmod_long(pool, q, NULL, a, d, true);
}

/* ================================================================ solver helpers */

ms_status bi_binomial(bi_pool *pool, bigint *out, uint32_t n, uint32_t k) {
    if (k > n) {
        bi_set_zero(out);
        return MS_OK;
    }
    if (k > n - k) k = n - k;
    if (k == 0) {
        bi_set_u64(out, 1);
        return MS_OK;
    }
    /* C(n, k) < min(2^n, n^k); one more limb holds the batch multiplier.
     * Small results are computed on the stack, so values that fit two limbs
     * stay inline. */
    uint64_t bits = (uint64_t)k * bi_bits32(n);
    if (bits > n) bits = n;
    uint64_t need = bits / 32u + 3u;
    if (need > BI_MAX_LIMBS) return MS_ERR_RESOURCE_EXHAUSTED;
    uint32_t local[8];
    uint32_t *x = local;
    if (need > 8) {
        ms_status status = bi_grow(pool, out, need);
        if (status != MS_OK) return status;
        x = bi_mut(out);
    }
    uint32_t len = 1;
    x[0] = 1;
    uint32_t base = n - k;
    uint32_t i = 1;
    /* After step i, x = C(base + i, i). Factors are batched while their
     * products fit a limb: x * (base+i)...(base+j) / (i...j) is C(base+j, j). */
    while (i <= k) {
        uint64_t num = (uint64_t)base + i;
        uint64_t den = i;
        i++;
        while (i <= k) {
            uint64_t next_num = num * ((uint64_t)base + i);
            uint64_t next_den = den * i;
            if (next_num > UINT32_MAX || next_den > UINT32_MAX) break;
            num = next_num;
            den = next_den;
            i++;
        }
        uint32_t carry = bi_mul_1(x, x, len, (uint32_t)num);
        if (carry) x[len++] = carry;
        bi_divrem_1(x, x, len, (uint32_t)den); /* exact */
        while (x[len - 1] == 0) len--;
    }
    if (x == local) {
        ms_status status = bi_grow(pool, out, len);
        if (status != MS_OK) return status;
        bi_copy_limbs(bi_mut(out), local, len);
    }
    out->len = len;
    return MS_OK;
}

ms_status bi_binomial_next(bi_pool *pool, bigint *x, uint32_t n, uint32_t j) {
    if (j >= n) {
        bi_set_zero(x);
        return MS_OK;
    }
    uint32_t m = n - j;
    if (x->len <= BI_INLINE_LIMBS) {
        /* x * m < 2^96 on the stack: nothing is written unless exact. */
        uint32_t local[BI_INLINE_LIMBS + 1];
        uint32_t len = x->len;
        bi_copy_limbs(local, bi_limbs(x), len);
        local[len] = bi_mul_1(local, local, len, m);
        len++;
        if (bi_divrem_1(local, local, len, j + 1) != 0) return MS_ERR_INTERNAL;
        while (len > 0 && local[len - 1] == 0) len--;
        ms_status status = bi_grow(pool, x, len);
        if (status != MS_OK) return status;
        bi_copy_limbs(bi_mut(x), local, len);
        x->len = len;
        return MS_OK;
    }
    ms_status status = bi_mul_u32(pool, x, x, m);
    if (status != MS_OK) return status;
    status = bi_divexact_u32(pool, x, x, j + 1);
    if (status != MS_OK) {
        /* x * m was restored; divide the factor back out. */
        bi_divrem_1(bi_mut(x), bi_limbs(x), x->len, m);
        bi_normalize(x, x->len);
    }
    return status;
}

ms_status bi_poly_divexact_term(bi_pool *pool, bigint *d, uint32_t t, const bigint *q_t,
                                const bigint *h, uint32_t hlen) {
    if (hlen == 0 || h[0].len == 0) return MS_ERR_INTERNAL;
    bigint acc;
    bi_init(&acc);
    ms_status status = bi_copy(pool, &acc, q_t);
    uint32_t top = t < hlen - 1 ? t : hlen - 1;
    for (uint32_t i = 1; i <= top && status == MS_OK; i++) {
        status = bi_submul(pool, &acc, &h[i], &d[t - i]);
    }
    if (status == MS_OK) status = bi_divexact(pool, &acc, &acc, &h[0]);
    if (status == MS_OK) {
        bi_move(pool, &d[t], &acc);
    } else {
        bi_free(pool, &acc);
    }
    return status;
}

/* ================================================================ ratios */

/* Bits of the binary64 nearest to (q + f) * 2^-s, where q in [2^62, 2^64)
 * and f in [0, 1) is nonzero iff `sticky` (ties to even). */
static uint64_t bi_round_bits(uint64_t q, bool sticky, int64_t s) {
    int64_t e = (int64_t)bi_bits64(q) - 1 - s; /* value in [2^e, 2^(e+1)) */
    if (e > 1023) return BI_F64_INF_BITS;
    int64_t ulp = e - 52 < -1074 ? -1074 : e - 52; /* exponent of the last kept bit */
    int64_t drop = ulp + s;                         /* q bits below it, >= 10 */
    uint64_t m;
    if (drop >= 65) {
        m = 0; /* value < 2^(ulp - 1): below half the smallest subnormal */
    } else if (drop == 64) {
        uint64_t half = (uint64_t)1 << 63;
        m = (q > half || (q == half && sticky)) ? 1u : 0u;
    } else {
        m = q >> drop;
        uint64_t rest = q & (((uint64_t)1 << drop) - 1u);
        uint64_t half = (uint64_t)1 << (drop - 1);
        if (rest > half || (rest == half && (sticky || (m & 1u)))) m++;
    }
    /* Normal: m in [2^52, 2^53] and biased exponent ulp + 1075, so the bits
     * are ((ulp + 1074) << 52) + m (m == 2^53 carries into the exponent).
     * Subnormal (ulp == -1074): the bits are m itself, m <= 2^52. */
    uint64_t bits = ((uint64_t)(ulp + 1074) << 52) + m;
    return bits >= BI_F64_INF_BITS ? BI_F64_INF_BITS : bits;
}

/* Bits of the correctly rounded num / den, num > 0, den > 0. */
static ms_status bi_ratio_bits(bi_pool *pool, const bigint *num, const bigint *den,
                               uint64_t *out) {
    int64_t la = bi_bit_length(num);
    int64_t lb = bi_bit_length(den);
    if (la <= 53 && lb <= 53) {
        /* Both exact as doubles: IEEE division rounds correctly. */
        uint64_t nv = 0, dv = 0;
        bi_get_u64(num, &nv);
        bi_get_u64(den, &dv);
        union {
            double d;
            uint64_t u;
        } pun;
        pun.d = (double)nv / (double)dv;
        *out = pun.u;
        return MS_OK;
    }
    int64_t e = la - lb; /* num / den in (2^(e-1), 2^(e+1)) */
    if (e >= 1026) {
        *out = BI_F64_INF_BITS;
        return MS_OK;
    }
    if (e <= -1077) {
        *out = 0;
        return MS_OK;
    }
    /* q = floor(num * 2^s / den) lies in [2^62, 2^64). Computed as
     * (num << alpha) / (den << beta) with alpha - beta = s, the shifted
     * divisor ending on a limb boundary (normalized) with >= 2 limbs. */
    int64_t s = 63 - e;
    int64_t beta = s < 0 ? -s : 0;
    beta += (32 - (lb + beta) % 32) % 32;
    if (lb + beta < 64) beta += 32;
    int64_t alpha = beta + s;
    uint32_t n = (uint32_t)((lb + beta) / 32);
    uint32_t ulen = (uint32_t)((la + alpha + 31) / 32) + 1;
    uint32_t m = ulen - 1 - n;
    uint32_t *block;
    uint32_t cap;
    ms_status status = bi_block_take(pool, (uint64_t)ulen + n + m + 1, &block, &cap);
    if (status != MS_OK) return status;
    uint32_t *un = block;
    uint32_t *vn = un + ulen;
    uint32_t *qn = vn + n;
    bi_shl_into(un, ulen, bi_limbs(num), num->len, (uint64_t)alpha);
    bi_shl_into(vn, n, bi_limbs(den), den->len, (uint64_t)beta);
    bi_div_knuth(qn, un, vn, m, n);
    uint64_t q = qn[0];
    if (m >= 1) q |= (uint64_t)qn[1] << 32;
    bool sticky = false;
    for (uint32_t i = 0; i < n; i++) sticky = sticky || un[i] != 0;
    bi_block_give(pool, block, cap);
    *out = bi_round_bits(q, sticky, s);
    return MS_OK;
}

ms_status bi_ratio(bi_pool *pool, const bigint *num, const bigint *den, double *out) {
    if (den->len == 0) return MS_ERR_INTERNAL;
    if (num->len == 0) {
        *out = 0.0;
        return MS_OK;
    }
    uint64_t bits;
    ms_status status = bi_ratio_bits(pool, num, den, &bits);
    if (status != MS_OK) return status;
    *out = bi_f64(bits);
    return MS_OK;
}

ms_status bi_probability(bi_pool *pool, const bigint *num, const bigint *den, bool exact,
                         double *out) {
    if (den->len == 0) return MS_ERR_INTERNAL;
    int order = bi_cmp(num, den);
    if (order > 0) return MS_ERR_INTERNAL;
    if (order == 0) {
        *out = 1.0;
        return MS_OK;
    }
    if (num->len == 0) {
        *out = 0.0;
        return MS_OK;
    }
    uint64_t bits;
    ms_status status = bi_ratio_bits(pool, num, den, &bits);
    if (status != MS_OK) return status;
    if (exact && bits == 0) bits = BI_F64_MIN_SUBNORMAL_BITS;
    if (exact && bits == BI_F64_ONE_BITS) bits = BI_F64_BELOW_ONE_BITS;
    *out = bi_f64(bits);
    return MS_OK;
}
