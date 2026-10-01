#pragma once

/*
 * bigint.h - exact unsigned multiprecision integers for the probability
 * solver (layout counts, histogram polynomials, importance weights) and the
 * final rational-to-double conversion. Exactly the operations
 * minesweeper/probability.py performs on Python ints, nothing more.
 *
 * Little-endian base-2^32 limbs, 64-bit intermediates only: no system
 * headers, no libc, no __int128, no compiler-rt helpers (no 128-bit
 * overflow idioms); identical results natively and in WebAssembly.
 *
 * Representation
 * --------------
 * A bigint is a 16-byte value. `len` counts significant limbs (0 for zero;
 * otherwise the top limb is nonzero: every value is canonical, so equality
 * and comparison are exact). Without a block (cap == 0) up to
 * BI_INLINE_LIMBS limbs (values < 2^64) live inline in the struct; larger
 * values need a heap block of `cap` limbs (a power of two >=
 * BI_MIN_HEAP_LIMBS) from a bi_pool, which the bigint keeps until bi_free or
 * bi_move. Results are sized from operand bit lengths, so small results stay
 * inline. Read limbs through bi_limbs(); mutate only through the functions
 * below.
 *
 * - All-zero bytes are a valid zero without storage: BI_ZERO_INIT, static
 *   storage and zeroed memory (rt_bump_array/rt_calloc) need no bi_init.
 * - The struct holds no pointer into itself, so it may be relocated with
 *   memcpy/realloc (e.g. a growing array of bigints). Copying the struct
 *   does NOT clone: afterwards exactly one copy is live and the other must
 *   never be used again (not even for bi_free). Use bi_copy to clone and
 *   bi_move/bi_swap to transfer ownership.
 * - Distinct live bigints never share storage.
 *
 * Pools, ownership and lifetime
 * -----------------------------
 * A bi_pool hands out limb blocks carved with rt_bump from the rt_mem it
 * borrows (never rt_alloc: that costs >= 4 KiB per block) and recycles
 * blocks released by bi_free/bi_move/regrowth through per-size free lists.
 * Nothing goes back to the rt_mem until it is rewound, reset or disposed.
 * - Every function that may write a bigint's storage takes the pool that
 *   bigint's block came from; all bigints written in one call must share
 *   that pool. Read-only operands may come from any pool.
 * - Blocks live in the rt_mem's bump chunks. Call bi_pool_reset (it
 *   forgets the free lists) before rt_mem_rewind releases memory holding
 *   any of the pool's blocks (a mark taken before some block was carved)
 *   and before rt_mem_reset or rt_mem_dispose. Afterwards every bigint
 *   holding a block of that pool (cap != 0) is dead, even if its block
 *   survived: re-init it with bi_init, never bi_free it. Bigints with
 *   cap == 0 stay valid. Keep values of different lifetimes in different
 *   pools.
 * - bi_free returns a block for reuse; bi_init on a bigint that owns a block
 *   only forgets it (the bytes stay used until the arena is released).
 * - Scratch for division by multi-limb divisors, ratio conversion of
 *   operands above 2^53, products whose output aliases an operand and the
 *   histogram-division accumulator is taken from and returned to the same
 *   pool within the call: these calls can fail with
 *   MS_ERR_RESOURCE_EXHAUSTED even when the outputs have capacity.
 *
 * Calls, aliasing and failure
 * ---------------------------
 * - Output operands may be the same object as any input operand
 *   (bi_add(p, x, x, x), bi_mul(p, x, x, y), ...). Two outputs of one call
 *   must be distinct objects.
 * - Results reuse an output's existing capacity when it suffices; capacity
 *   never shrinks implicitly.
 * - Fallible functions return MS_OK, MS_ERR_RESOURCE_EXHAUSTED (rt_mem
 *   budget/injected/allocator failure, or a result above BI_MAX_LIMBS) or
 *   MS_ERR_INTERNAL (division by zero, checked subtraction or exact division
 *   that does not hold, a probability outside [0, 1], misuse such as a
 *   disposed rt_mem). On every failure each output keeps its value (only
 *   reserved capacity may have grown) and no partial result is produced.
 * - Single-threaded, like the rest of the engine.
 */

#include <base/types.h>

#include "engine.h"
#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BI_INLINE_LIMBS 2u
#define BI_MIN_HEAP_LIMBS 4u
#define BI_MAX_LIMBS ((uint32_t)1 << 24) /* 2^29 bits */
#define BI_POOL_CLASSES 23u              /* heap capacities 4 << k limbs, k < 23 */

/* Binary64 bit patterns of the exact-probability endpoint guard. */
#define BI_F64_MIN_SUBNORMAL_BITS UINT64_C(0x0000000000000001) /* 2^-1074 */
#define BI_F64_BELOW_ONE_BITS UINT64_C(0x3FEFFFFFFFFFFFFF)     /* 1 - 2^-53 */

typedef struct bigint {
    uint32_t len; /* significant limbs; 0 for zero */
    uint32_t cap; /* 0: inline limbs; else heap block capacity in limbs */
    union {
        uint32_t small[BI_INLINE_LIMBS];
        uint32_t *heap;
    } u;
} bigint;

#define BI_ZERO_INIT {0u, 0u, {{0u, 0u}}}

typedef struct bi_pool {
    rt_mem *mem;                         /* borrowed, not owned */
    void *free_lists[BI_POOL_CLASSES];   /* private: recycled blocks */
    uint64_t outstanding; /* blocks held by bigints/scratch (0 when all freed) */
    uint64_t cached;      /* blocks waiting in the free lists */
    uint64_t carved;      /* blocks ever carved with rt_bump */
    uint64_t reused;      /* block requests served from the free lists */
} bi_pool;

/* ------------------------------------------------------------ pool, lifetime */

void bi_pool_init(bi_pool *pool, rt_mem *mem); /* never allocates */
void bi_pool_reset(bi_pool *pool);             /* forget free lists, zero counters */

void bi_init(bigint *x);                       /* zero without storage */
void bi_free(bi_pool *pool, bigint *x);        /* release storage; x becomes 0 */
/* dst takes src's value and storage (dst's old block is released); src
 * becomes 0 without storage. dst == src: no-op. Never fails. */
void bi_move(bi_pool *pool, bigint *dst, bigint *src);
void bi_swap(bigint *a, bigint *b);            /* exchanges values and storage */
/* Capacity >= limbs, value kept. */
ms_status bi_reserve(bi_pool *pool, bigint *x, uint32_t limbs);

/* ------------------------------------------------------------ values */

void bi_set_zero(bigint *x);                   /* keeps capacity */
void bi_set_u64(bigint *x, uint64_t value);    /* never allocates */
/* Imports little-endian limbs (leading zero limbs allowed; count 0 is zero).
 * `limbs` must not overlap x's storage. */
ms_status bi_set_limbs(bi_pool *pool, bigint *x, const uint32_t *limbs, uint32_t count);
ms_status bi_copy(bi_pool *pool, bigint *dst, const bigint *src); /* deep clone */
/* true and *out = x when x < 2^64; false (out untouched) otherwise. */
bool bi_get_u64(const bigint *x, uint64_t *out);

/* x's len limbs. May point into *x itself (inline values): valid only until
 * x is modified, freed, moved, swapped or relocated. */
static inline const uint32_t *bi_limbs(const bigint *x) {
    return x->cap ? x->u.heap : x->u.small;
}

static inline bool bi_is_zero(const bigint *x) {
    return x->len == 0;
}

int bi_cmp(const bigint *a, const bigint *b);  /* -1, 0 or 1 */
bool bi_eq(const bigint *a, const bigint *b);
uint32_t bi_bit_length(const bigint *x);       /* 0 for zero */

/* ------------------------------------------------------------ arithmetic */

ms_status bi_add(bi_pool *pool, bigint *out, const bigint *a, const bigint *b);
/* out = a - b; a < b is MS_ERR_INTERNAL. */
ms_status bi_sub(bi_pool *pool, bigint *out, const bigint *a, const bigint *b);
ms_status bi_mul(bi_pool *pool, bigint *out, const bigint *a, const bigint *b);
ms_status bi_mul_u32(bi_pool *pool, bigint *out, const bigint *a, uint32_t m);
/* acc += a * b and acc += a * m (sums of products without temporaries). */
ms_status bi_addmul(bi_pool *pool, bigint *acc, const bigint *a, const bigint *b);
ms_status bi_addmul_u32(bi_pool *pool, bigint *acc, const bigint *a, uint32_t m);
/* acc -= a * b; acc < a * b is MS_ERR_INTERNAL. Allocates only when acc is
 * the same object as a or b. */
ms_status bi_submul(bi_pool *pool, bigint *acc, const bigint *a, const bigint *b);
ms_status bi_shl(bi_pool *pool, bigint *out, const bigint *a, uint32_t bits); /* a * 2^bits */
ms_status bi_pow2(bi_pool *pool, bigint *out, uint32_t bits);                 /* 2^bits */

/* ------------------------------------------------------------ division
 * d == 0 is MS_ERR_INTERNAL. Exact variants fail with MS_ERR_INTERNAL on a
 * nonzero remainder, leaving q unchanged. */

/* q = a / d (q may be NULL), *rem = a % d (rem may be NULL). */
ms_status bi_divmod_u32(bi_pool *pool, bigint *q, const bigint *a, uint32_t d, uint32_t *rem);
ms_status bi_divexact_u32(bi_pool *pool, bigint *q, const bigint *a, uint32_t d);
/* q = a / d, r = a % d. Either output may be NULL; if both are given they
 * must be distinct objects (else MS_ERR_INTERNAL). */
ms_status bi_divmod(bi_pool *pool, bigint *q, bigint *r, const bigint *a, const bigint *d);
ms_status bi_divexact(bi_pool *pool, bigint *q, const bigint *a, const bigint *d);

/* ------------------------------------------------------------ solver helpers */

/* out = C(n, k); 0 when k > n (Python's math.comb). */
ms_status bi_binomial(bi_pool *pool, bigint *out, uint32_t n, uint32_t k);
/* Binomial recurrence in place: x = C(n, j) becomes C(n, j + 1), i.e.
 * x * (n - j) / (j + 1), and 0 when j >= n. A non-exact division (x was not
 * C(n, j)) is MS_ERR_INTERNAL with x unchanged. */
ms_status bi_binomial_next(bi_pool *pool, bigint *x, uint32_t n, uint32_t j);

/* One term of exact low-order power-series division, the histogram cavity
 * recovery of probability.py's _cavity: with d[0..t) already computed,
 *   d[t] = (q_t - sum_{i=1}^{min(t, hlen-1)} h[i] * d[t-i]) / h[0].
 * Every partial difference must stay >= 0 and the division must be exact,
 * otherwise MS_ERR_INTERNAL ("inexact histogram division"); h[0] == 0 or
 * hlen == 0 is MS_ERR_INTERNAL too. d[t] is replaced only on success, so
 * q_t may be d[t] itself. Calling it for t = 0, 1, ... in turn lets the
 * caller check its time budget between terms. */
ms_status bi_poly_divexact_term(bi_pool *pool, bigint *d, uint32_t t, const bigint *q_t,
                                const bigint *h, uint32_t hlen);

/* ------------------------------------------------------------ ratios
 * Correct rounding (to nearest, ties to even) of the exact rational, like
 * Python's int / int, computed from the integers themselves: huge operands
 * are never converted to double first. Gradual underflow yields subnormals
 * or 0.0; a quotient rounding beyond DBL_MAX yields +infinity (the IEEE
 * result; Python raises OverflowError). *out is written only on MS_OK. */

/* num / den for any num and den > 0 (e.g. effective sample size, which may
 * exceed 1). */
ms_status bi_ratio(bi_pool *pool, const bigint *num, const bigint *den, double *out);

/* Probability num / den, requiring den > 0 and num <= den (else
 * MS_ERR_INTERNAL). With `exact`, 0.0 and 1.0 stay reserved for the
 * integer-proven endpoints num == 0 and num == den: a positive value that
 * rounds to 0.0 becomes 2^-1074 and a value below 1 that rounds to 1.0
 * becomes 1 - 2^-53 (probability.py's _ratio). Approximate (sampled)
 * values are returned unmodified. */
ms_status bi_probability(bi_pool *pool, const bigint *num, const bigint *den, bool exact,
                         double *out);

#ifdef __cplusplus
}
#endif
