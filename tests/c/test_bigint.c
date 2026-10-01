/*
 * Tests for c/bigint.c, the exact-arithmetic gate of the probability solver.
 *
 * Expected values come from the frozen BIGINT_* fixtures and from sources
 * independent of the code under test: closed forms (all-ones products,
 * Mersenne quotients, 10^400 = (10^200 + 1)(10^200 - 1) + 1, Vandermonde),
 * native u64 arithmetic, IEEE hardware division/scaling and the compiler's
 * correctly rounded decimal literals. Randomized multi-limb cases check
 * algebraic identities. Every test checks canonical form and that all
 * storage returns to its pool, with no rt_mem misuse.
 */

#include "test_support.h"

#include "bigint.h"
#include "fixtures/bigint_cases.h"

#define TEST_MAX_LIMBS 512u
#define RAND_LIMBS 48u
#define F64_ONE_BITS UINT64_C(0x3FF0000000000000)
#define F64_INF_BITS UINT64_C(0x7FF0000000000000)
#define F64_MAX_BITS UINT64_C(0x7FEFFFFFFFFFFFFF)
#define F64_MIN_NORMAL_BITS UINT64_C(0x0010000000000000)

static const char *test_label; /* printed with a failure when set */
static uint32_t test_limbs[TEST_MAX_LIMBS];
static uint32_t expect_limbs[TEST_MAX_LIMBS];

/* ---------------------------------------------------------------- harness */

typedef struct test_env {
    rt_mem mem;
    bi_pool pool;
} test_env;

static void env_open(test_env *env) {
    rt_mem_init(&env->mem, RT_MEM_UNLIMITED);
    bi_pool_init(&env->pool, &env->mem);
}

/* Every bigint must have been freed: no block outstanding or lost and no
 * rt_mem misuse. Then the arena is released. */
static void env_close(test_env *env) {
    CHECK_EQ(env->pool.outstanding, 0);
    CHECK_EQ(env->pool.cached, env->pool.carved);
    CHECK_EQ(env->mem.misuses, 0);
    bi_pool_reset(&env->pool);
    rt_mem_dispose(&env->mem);
    CHECK_EQ(env->mem.live, 0);
    CHECK_EQ(env->mem.live_blocks, 0);
}

static void init_all(bigint *xs, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) bi_init(&xs[i]);
}

static void free_all(test_env *env, bigint *xs, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) bi_free(&env->pool, &xs[i]);
}

/* ---------------------------------------------------------------- checks */

static void print_limbs(const uint32_t *limbs, uint32_t n) {
    while (n > 0 && limbs[n - 1] == 0) n--;
    if (n == 0) {
        test_print("0");
        return;
    }
    test_print("0x");
    for (uint32_t i = n; i-- > 0;) {
        char digits[10];
        for (uint32_t k = 0; k < 8; k++) digits[k] = "0123456789abcdef"[(limbs[i] >> (28u - 4u * k)) & 15u];
        digits[8] = i ? '_' : '\0';
        digits[9] = '\0';
        test_print(digits);
    }
}

static void print_label(void) {
    if (test_label == NULL) return;
    test_print(" [");
    test_print(test_label);
    test_print("]");
}

static bool big_canonical(const bigint *x) {
    if (x->cap != 0 && (x->cap < BI_MIN_HEAP_LIMBS || (x->cap & (x->cap - 1u)) != 0)) return false;
    if (x->len > (x->cap ? x->cap : BI_INLINE_LIMBS)) return false;
    return x->len == 0 || bi_limbs(x)[x->len - 1] != 0;
}

static bool big_is(const bigint *x, const uint32_t *limbs, uint32_t n) {
    while (n > 0 && limbs[n - 1] == 0) n--;
    if (!big_canonical(x) || x->len != n) return false;
    const uint32_t *l = bi_limbs(x);
    for (uint32_t i = 0; i < n; i++) {
        if (l[i] != limbs[i]) return false;
    }
    return true;
}

static void check_big(const bigint *x, const uint32_t *limbs, uint32_t n, const char *what,
                      const char *file, unsigned int line, const char *func) {
    if (big_is(x, limbs, n)) return;
    test_fail_begin(what, file, line, func);
    print_label();
    bool canonical = big_canonical(x);
    test_print(canonical ? " (actual " : " (non-canonical, actual ");
    if (canonical) print_limbs(bi_limbs(x), x->len);
    test_print(", expected ");
    print_limbs(limbs, n);
    test_print(")");
    test_fail_end();
}

static void check_big_u64(const bigint *x, uint64_t value, const char *what, const char *file,
                          unsigned int line, const char *func) {
    uint32_t limbs[2];
    limbs[0] = (uint32_t)value;
    limbs[1] = (uint32_t)(value >> 32);
    check_big(x, limbs, 2, what, file, line, func);
}

#define CHECK_BIG(x, limbs, n) \
    check_big((x), (limbs), (n), "CHECK_BIG(" #x ")", __FILE__, __LINE__, __func__)
#define CHECK_BIG_EQ(x, y)                                                                    \
    check_big((x), bi_limbs(y), (y)->len, "CHECK_BIG_EQ(" #x ", " #y ")", __FILE__, __LINE__, \
              __func__)
#define CHECK_BIG_FX(x, fx) \
    check_big((x), (fx).limbs, (fx).len, "CHECK_BIG_FX(" #x ", " #fx ")", __FILE__, __LINE__, __func__)
#define CHECK_BIG_U64(x, value) \
    check_big_u64((x), (value), "CHECK_BIG_U64(" #x ", " #value ")", __FILE__, __LINE__, __func__)

static uint64_t f64_bits(double value) {
    union {
        double d;
        uint64_t u;
    } pun;
    pun.d = value;
    return pun.u;
}

static void check_bits(double actual, uint64_t expected, const char *what, const char *file,
                       unsigned int line, const char *func) {
    if (f64_bits(actual) == expected) return;
    test_fail_begin(what, file, line, func);
    print_label();
    test_print(" (actual ");
    test_print_hex(f64_bits(actual));
    test_print(" = ");
    test_print_double(actual);
    test_print(", expected ");
    test_print_hex(expected);
    test_print(" = ");
    test_print_double(fixture_f64(expected));
    test_print(")");
    test_fail_end();
}

#define CHECK_BITS(actual, bits) \
    check_bits((actual), (bits), #actual " == " #bits, __FILE__, __LINE__, __func__)
#define CHECK_SAME(actual, expected) \
    check_bits((actual), f64_bits(expected), #actual " == " #expected, __FILE__, __LINE__, __func__)

/* ---------------------------------------------------------------- values */

static FixtureBig fx_big(const uint32_t *limbs, uint32_t len) {
    FixtureBig fx;
    fx.limbs = limbs;
    fx.len = len;
    return fx;
}

#define FX_ARRAY(arr) fx_big((arr), (uint32_t)array_size(arr))
#define FX_POW10_400 FX_ARRAY(bigint_zero_over_huge_exact_den_limbs)
#define FX_POW10_400_M1 FX_ARRAY(bigint_huge_minus_one_over_huge_exact_num_limbs)
#define FX_THIRD FX_ARRAY(bigint_third_of_huge_exact_num_limbs) /* (10^400 - 1) / 3 */
#define FX_C6400_3200 FX_ARRAY(bigint_c_6400_3200_limbs)

static void set_limbs(test_env *env, bigint *x, const uint32_t *limbs, uint32_t n) {
    CHECK_STATUS(bi_set_limbs(&env->pool, x, limbs, n), MS_OK);
}

static void set_fx(test_env *env, bigint *x, FixtureBig fx) {
    set_limbs(env, x, fx.limbs, fx.len);
}

static void set_copy(test_env *env, bigint *x, const bigint *src) {
    CHECK_STATUS(bi_copy(&env->pool, x, src), MS_OK);
}

/* Writes 2^bits - 1 limb by limb into `limbs`; returns the limb count. */
static uint32_t ones_into(uint32_t *limbs, uint32_t bits) {
    uint32_t n = (bits + 31u) / 32u;
    CHECK(n <= TEST_MAX_LIMBS);
    for (uint32_t i = 0; i < n; i++) limbs[i] = UINT32_MAX;
    if (bits % 32u) limbs[n - 1] = ((uint32_t)1 << (bits % 32u)) - 1u;
    return n;
}

static void set_ones(test_env *env, bigint *x, uint32_t bits) {
    set_limbs(env, x, test_limbs, ones_into(test_limbs, bits));
}

/* x = 10^exponent by repeated multiplication by ten. */
static void set_pow10(test_env *env, bigint *x, uint32_t exponent) {
    bi_set_u64(x, 1);
    for (uint32_t i = 0; i < exponent; i++) CHECK_STATUS(bi_mul_u32(&env->pool, x, x, 10), MS_OK);
}

/* x = 2^bits + add - sub for small add/sub. */
static void set_pow2_offset(test_env *env, bigint *x, uint32_t bits, uint64_t add, uint64_t sub) {
    bigint t = BI_ZERO_INIT;
    CHECK_STATUS(bi_pow2(&env->pool, x, bits), MS_OK);
    bi_set_u64(&t, add);
    CHECK_STATUS(bi_add(&env->pool, x, x, &t), MS_OK);
    bi_set_u64(&t, sub);
    CHECK_STATUS(bi_sub(&env->pool, x, x, &t), MS_OK);
}

/* ---------------------------------------------------------------- random */

static uint32_t rand_limb(rt_rng *rng) {
    switch (rt_rng_below(rng, 8)) {
    case 0: return 0;
    case 1: return UINT32_MAX;
    case 2: return 0x80000000u;
    case 3: return 1;
    case 4: return 0x7FFFFFFFu;
    default: return rt_rng_u32(rng);
    }
}

static void rand_big(test_env *env, rt_rng *rng, bigint *x, uint32_t max_limbs) {
    uint32_t limbs[RAND_LIMBS];
    CHECK(max_limbs <= RAND_LIMBS);
    uint32_t n = rt_rng_below(rng, max_limbs + 1u);
    for (uint32_t i = 0; i < n; i++) limbs[i] = rand_limb(rng);
    set_limbs(env, x, limbs, n);
}

/* A value of at most `bits` bits, often of a special shape. */
static uint64_t rand_value(rt_rng *rng, uint32_t bits) {
    if (bits == 0) return 0;
    uint64_t mask = bits >= 64 ? UINT64_MAX : (((uint64_t)1 << bits) - 1u);
    uint64_t top = (uint64_t)1 << (bits - 1u);
    uint64_t value = rt_rng_next(rng) & mask;
    switch (rt_rng_below(rng, 6)) {
    case 0: return mask;
    case 1: return top;
    case 2: return value | top;
    default: return value;
    }
}

static uint32_t ref_bit_length(uint64_t v) {
    uint32_t n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

/* 128-bit product from 32-bit halves, independent of the limb kernels. */
static void ref_mul64(uint64_t a, uint64_t b, uint32_t out[4]) {
    uint64_t al = a & 0xFFFFFFFFu, ah = a >> 32, bl = b & 0xFFFFFFFFu, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
    uint64_t low = (ll & 0xFFFFFFFFu) | (mid << 32);
    uint64_t high = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    out[0] = (uint32_t)low;
    out[1] = (uint32_t)(low >> 32);
    out[2] = (uint32_t)high;
    out[3] = (uint32_t)(high >> 32);
}

/* 2^-e as a double, 0 <= e <= 1074. */
static double pow2_neg(uint32_t e) {
    return e <= 1022 ? fixture_f64((uint64_t)(1023u - e) << 52) : fixture_f64((uint64_t)1 << (1074u - e));
}

/* a * 2^-k with one IEEE rounding: exact halvings while normal, then a
 * single hardware multiplication (the reference for gradual underflow). */
static double scaled_ref(double a, uint32_t k) {
    double normal_floor = pow2_neg(1021);
    while (k > 0 && a >= normal_floor) {
        a *= 0.5;
        k--;
    }
    if (k == 0) return a;
    if (k > 1074) return 0.0; /* a < 2^-1021: below half the smallest subnormal */
    return a * pow2_neg(k);
}

/* ---------------------------------------------------------------- representation */

static void test_representation(void) {
    test_case("representation: inline values, import, canonical form, comparison");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    CHECK_EQ(sizeof(bigint), 16);

    bigint z = BI_ZERO_INIT;
    CHECK(bi_is_zero(&z) && z.cap == 0 && big_canonical(&z));
    CHECK_EQ(bi_bit_length(&z), 0);
    bigint *zeroed = (bigint *)rt_bump_array(&env.mem, 3, sizeof(bigint));
    CHECK(zeroed != NULL);
    for (uint32_t i = 0; i < 3; i++) CHECK(bi_is_zero(&zeroed[i]) && zeroed[i].cap == 0);
    CHECK_STATUS(bi_add(pool, &zeroed[0], &zeroed[1], &zeroed[2]), MS_OK);
    CHECK(bi_is_zero(&zeroed[0]) && zeroed[0].cap == 0);

    static const uint64_t values[] = {0, 1, 0xFFFFFFFFull, 0x100000000ull, 0x123456789ABCDEF0ull,
                                      UINT64_MAX};
    static const uint32_t bits[] = {0, 1, 32, 33, 61, 64};
    for (size_t i = 0; i < array_size(values); i++) {
        bigint x = BI_ZERO_INIT;
        bi_set_u64(&x, values[i]);
        CHECK_EQ(x.cap, 0); /* never allocates */
        CHECK_BIG_U64(&x, values[i]);
        CHECK_EQ(bi_bit_length(&x), bits[i]);
        uint64_t back = 7;
        CHECK(bi_get_u64(&x, &back) && back == values[i]);
    }

    /* Import drops leading zero limbs; above two limbs a block is used. */
    static const uint32_t padded[5] = {5, 0, 7, 0, 0};
    bigint y = BI_ZERO_INIT;
    set_limbs(&env, &y, padded, 5);
    CHECK_EQ(y.len, 3);
    CHECK_EQ(y.cap, 4);
    CHECK_BIG(&y, padded, 3);
    CHECK_EQ(bi_bit_length(&y), 67);
    uint64_t untouched = 99;
    CHECK(!bi_get_u64(&y, &untouched) && untouched == 99);
    set_limbs(&env, &y, padded, 2); /* 5: the block is kept */
    CHECK_BIG_U64(&y, 5);
    CHECK_EQ(y.cap, 4);
    CHECK(bi_get_u64(&y, &untouched) && untouched == 5);
    set_limbs(&env, &y, NULL, 0);
    CHECK(bi_is_zero(&y) && y.cap == 4);

    /* Comparison: by length, then from the top limb down. */
    static const uint32_t two64[3] = {0, 0, 1};
    static const uint32_t two64_m1[3] = {UINT32_MAX, UINT32_MAX, 0};
    static const uint32_t two64_p1[3] = {1, 0, 1};
    static const uint32_t two65[3] = {0, 0, 2};
    bigint a = BI_ZERO_INIT, b = BI_ZERO_INIT, c = BI_ZERO_INIT, d = BI_ZERO_INIT;
    set_limbs(&env, &a, two64, 3);
    set_limbs(&env, &b, two64_m1, 3);
    set_limbs(&env, &c, two64_p1, 3);
    set_limbs(&env, &d, two65, 3);
    CHECK(b.len == 2 && b.cap == 0);
    CHECK(bi_cmp(&a, &b) == 1 && bi_cmp(&b, &a) == -1 && !bi_eq(&a, &b));
    CHECK(bi_cmp(&a, &c) == -1 && bi_cmp(&c, &a) == 1); /* lowest limb decides */
    CHECK(bi_cmp(&d, &c) == 1 && bi_cmp(&c, &d) == -1); /* top limb decides */
    CHECK(bi_cmp(&a, &a) == 0 && bi_eq(&a, &a));
    CHECK(bi_cmp(&z, &b) == -1 && bi_cmp(&b, &z) == 1 && bi_cmp(&z, &y) == 0);
    set_copy(&env, &y, &c);
    CHECK(bi_eq(&y, &c) && bi_cmp(&y, &c) == 0 && bi_limbs(&y) != bi_limbs(&c));
    bi_set_zero(&c);
    CHECK(bi_is_zero(&c) && c.cap == 4);

    bi_free(pool, &y);
    bi_free(pool, &a);
    bi_free(pool, &b);
    bi_free(pool, &c);
    bi_free(pool, &d);
    env_close(&env);
}

/* ---------------------------------------------------------------- ownership */

static void test_pool_lifetime(void) {
    test_case("ownership: carving, recycling, reserve, capacity reuse, move, swap, relocation");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    static const uint32_t three[3] = {1, 2, 3};

    bigint x = BI_ZERO_INIT;
    set_limbs(&env, &x, three, 3);
    CHECK_EQ(x.cap, 4);
    CHECK_EQ(pool->carved, 1);
    CHECK_EQ(pool->outstanding, 1);
    const uint32_t *first = bi_limbs(&x);
    bi_free(pool, &x);
    CHECK(x.len == 0 && x.cap == 0);
    CHECK_EQ(pool->outstanding, 0);
    CHECK_EQ(pool->cached, 1);
    bi_free(pool, &x); /* nothing to release */
    CHECK_EQ(pool->cached, 1);

    /* A released block is reused without asking the arena. */
    uint64_t requests = env.mem.requests;
    set_limbs(&env, &x, three, 3);
    CHECK(bi_limbs(&x) == first);
    CHECK_EQ(pool->reused, 1);
    CHECK_EQ(pool->carved, 1);
    CHECK_EQ(env.mem.requests, requests);

    /* Capacities are powers of two; reserve keeps the value. */
    bigint big = BI_ZERO_INIT;
    CHECK_STATUS(bi_reserve(pool, &big, 5), MS_OK);
    CHECK(big.cap == 8 && bi_is_zero(&big));
    CHECK_STATUS(bi_reserve(pool, &big, 8), MS_OK);
    CHECK_STATUS(bi_reserve(pool, &big, 1), MS_OK);
    CHECK(big.cap == 8 && pool->carved == 2);
    CHECK_STATUS(bi_reserve(pool, &x, 100), MS_OK);
    CHECK_EQ(x.cap, 128);
    CHECK_BIG(&x, three, 3);
    CHECK_EQ(pool->cached, 1); /* the 4-limb block went back */

    /* Results reuse an output's capacity. */
    const uint32_t *xblock = bi_limbs(&x);
    uint64_t carved = pool->carved;
    bigint p = BI_ZERO_INIT, q = BI_ZERO_INIT;
    CHECK_STATUS(bi_pow2(pool, &p, 1000), MS_OK);
    CHECK_STATUS(bi_pow2(pool, &q, 1500), MS_OK);
    CHECK_STATUS(bi_mul(pool, &x, &p, &q), MS_OK); /* 79 limbs fit 128 */
    CHECK(bi_limbs(&x) == xblock);
    CHECK_EQ(bi_bit_length(&x), 2501);
    CHECK_STATUS(bi_add(pool, &x, &x, &p), MS_OK);
    CHECK_STATUS(bi_shl(pool, &x, &x, 64), MS_OK);
    CHECK(bi_limbs(&x) == xblock && bi_bit_length(&x) == 2565);
    CHECK_EQ(pool->carved, carved + 2);

    /* Move releases the destination's block and transfers the source's. */
    bi_move(pool, &big, &x);
    CHECK(bi_limbs(&big) == xblock && big.cap == 128 && bi_bit_length(&big) == 2565);
    CHECK(x.len == 0 && x.cap == 0);
    bi_move(pool, &big, &big);
    CHECK(bi_limbs(&big) == xblock && bi_bit_length(&big) == 2565);

    /* Swap exchanges inline and heap representations. */
    bi_set_u64(&x, 42);
    bi_swap(&x, &big);
    CHECK_BIG_U64(&big, 42);
    CHECK(big.cap == 0 && bi_limbs(&x) == xblock && bi_bit_length(&x) == 2565);

    /* Relocation: structs copied byte for byte (a growing vector) stay
     * valid, the old copies are dead. */
    bigint *slots = (bigint *)rt_bump_array(&env.mem, 4, sizeof(bigint));
    bigint *moved = (bigint *)rt_bump_array(&env.mem, 4, sizeof(bigint));
    CHECK(slots != NULL && moved != NULL);
    bi_set_u64(&slots[0], 7);
    set_limbs(&env, &slots[1], three, 3);
    CHECK_STATUS(bi_pow2(pool, &slots[2], 200), MS_OK);
    base_memcpy(moved, slots, 4 * sizeof(bigint));
    base_memset(slots, 0xA5, 4 * sizeof(bigint));
    CHECK_BIG_U64(&moved[0], 7);
    CHECK(bi_limbs(&moved[0]) == moved[0].u.small); /* inline limbs travel with the struct */
    CHECK_BIG(&moved[1], three, 3);
    CHECK_EQ(bi_bit_length(&moved[2]), 201);
    CHECK(bi_is_zero(&moved[3]));
    CHECK_STATUS(bi_mul(pool, &moved[0], &moved[0], &moved[1]), MS_OK);
    static const uint32_t three_x7[3] = {7, 14, 21};
    CHECK_BIG(&moved[0], three_x7, 3);
    CHECK_STATUS(bi_add(pool, &moved[3], &moved[1], &moved[2]), MS_OK);
    CHECK_EQ(bi_bit_length(&moved[3]), 201);
    free_all(&env, moved, 4);

    bi_free(pool, &x);
    bi_free(pool, &big);
    bi_free(pool, &p);
    bi_free(pool, &q);
    env_close(&env);
}

/* Odd elements are 2^(64 + i % 300) + i (a block), even ones i (inline). */
static void check_element(test_env *env, const bigint *x, uint32_t i) {
    if (i % 2 == 0) {
        CHECK_BIG_U64(x, i);
        CHECK_EQ(x->cap, 0);
        return;
    }
    bigint t = BI_ZERO_INIT;
    CHECK_EQ(bi_bit_length(x), 65u + i % 300u);
    CHECK_STATUS(bi_pow2(&env->pool, &t, 64u + i % 300u), MS_OK);
    CHECK_STATUS(bi_sub(&env->pool, &t, x, &t), MS_OK);
    CHECK_BIG_U64(&t, i);
    bi_free(&env->pool, &t);
}

static void test_growable_array(void) {
    test_case("ownership: bigints in an rt_grow array survive relocation, swap-remove, reuse");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    bigint *items = NULL;
    size_t capacity = 0;
    uint32_t count = 0;
    uint32_t relocations = 0;
    for (uint32_t i = 0; i < 3000; i++) {
        if (items == NULL || count + 1u > capacity) {
            bigint *grown = (bigint *)rt_grow(&env.mem, items, &capacity, count + 1u, sizeof(bigint));
            CHECK(grown != NULL);
            if (items != NULL && grown != items) relocations++; /* old copies are dead */
            items = grown;
        }
        bigint *x = &items[count++];
        bi_init(x); /* rt_grow does not zero the new tail */
        if (i % 2 == 0) {
            bi_set_u64(x, i);
        } else {
            bigint low = BI_ZERO_INIT;
            bi_set_u64(&low, i);
            CHECK_STATUS(bi_pow2(pool, x, 64u + i % 300u), MS_OK);
            CHECK_STATUS(bi_add(pool, x, x, &low), MS_OK);
        }
    }
    CHECK(relocations >= 2);
    for (uint32_t i = 0; i < count; i++) check_element(&env, &items[i], i);
    CHECK_EQ(pool->outstanding, 1500);

    /* Elements stay usable in place, including aliased operands. */
    bigint saved = BI_ZERO_INIT;
    set_copy(&env, &saved, &items[1]);
    CHECK_STATUS(bi_mul(pool, &items[1], &items[1], &items[3]), MS_OK);
    CHECK_STATUS(bi_divexact(pool, &items[1], &items[1], &items[3]), MS_OK);
    CHECK_BIG_EQ(&items[1], &saved);
    bi_swap(&items[0], &items[1]);
    bi_swap(&items[0], &items[1]);
    check_element(&env, &items[0], 0);
    check_element(&env, &items[1], 1);

    /* Swap-remove: free the hole, then move the last element in by plain
     * struct assignment; the copy left behind is dead. */
    for (uint32_t hole = 10; hole < 20; hole++) {
        uint32_t last = count - 1u;
        bi_free(pool, &items[hole]);
        items[hole] = items[last];
        count--;
        check_element(&env, &items[hole], last);
    }
    CHECK_EQ(pool->outstanding, 1500 - 5 + 1); /* saved holds one block */

    /* A released element's block serves the next request of its class. */
    uint32_t victim = 2001; /* untouched by the swap-removes; 2001 % 300 == 1401 % 300 */
    const uint32_t *block = bi_limbs(&items[victim]);
    uint32_t cap = items[victim].cap;
    uint64_t carved = pool->carved;
    bi_free(pool, &items[victim]);
    CHECK_STATUS(bi_reserve(pool, &items[victim], cap), MS_OK);
    CHECK(bi_limbs(&items[victim]) == block && items[victim].cap == cap);
    CHECK_STATUS(bi_copy(pool, &items[victim], &items[1401]), MS_OK);
    CHECK(bi_limbs(&items[victim]) == block && pool->carved == carved);
    check_element(&env, &items[victim], 1401);

    for (uint32_t i = 0; i < count; i++) bi_free(pool, &items[i]);
    bi_free(pool, &saved);
    rt_free(&env.mem, items);
    env_close(&env);
}

static void test_pool_release(void) {
    test_case("lifetime: pool reset with arena rewind/reset/dispose, size limits, misuse");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    bigint xs[8];
    init_all(xs, 8);
    bigint early = BI_ZERO_INIT;
    CHECK_STATUS(bi_pow2(pool, &early, 100), MS_OK);
    rt_mark mark = rt_mem_mark(&env.mem);
    size_t live = env.mem.live;
    for (uint32_t i = 0; i < 8; i++) CHECK_STATUS(bi_pow2(pool, &xs[i], 100000u * (i + 1u)), MS_OK);
    for (uint32_t i = 0; i < 4; i++) bi_free(pool, &xs[i]);
    CHECK(env.mem.live > live);
    /* Releasing arena memory below the pool's blocks: forget the free lists
     * first; bigints still holding blocks are dead and only re-initialized,
     * including `early`, whose block survived. */
    bi_pool_reset(pool);
    rt_mem_rewind(&env.mem, mark);
    CHECK_EQ(env.mem.live, live);
    init_all(xs, 8);
    bi_init(&early);
    CHECK(pool->outstanding == 0 && pool->cached == 0);
    CHECK_STATUS(bi_pow2(pool, &xs[0], 1000), MS_OK);
    CHECK_EQ(bi_bit_length(&xs[0]), 1001);
    bi_free(pool, &xs[0]);

    /* rt_mem_reset between solves. */
    CHECK_STATUS(bi_binomial(pool, &xs[1], 6400, 3200), MS_OK);
    bi_pool_reset(pool);
    rt_mem_reset(&env.mem);
    bi_init(&xs[1]);
    CHECK_EQ(env.mem.live, 0);

    /* Size limits are checked before the arena is asked. */
    uint64_t requests = env.mem.requests;
    bigint one = BI_ZERO_INIT;
    bi_set_u64(&one, 1);
    CHECK_STATUS(bi_reserve(pool, &one, BI_MAX_LIMBS + 1u), MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_STATUS(bi_pow2(pool, &one, UINT32_MAX), MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_STATUS(bi_shl(pool, &one, &one, UINT32_MAX), MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_STATUS(bi_binomial(pool, &one, UINT32_MAX, UINT32_MAX / 2u), MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_EQ(env.mem.requests, requests);
    CHECK(one.cap == 0);
    CHECK_BIG_U64(&one, 1);

    /* A budget refusal is exhaustion and keeps the value; inline values
     * need no memory. */
    rt_mem_set_budget(&env.mem, 0);
    CHECK_STATUS(bi_pow2(pool, &one, 64), MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_BIG_U64(&one, 1);
    CHECK_STATUS(bi_pow2(pool, &one, 63), MS_OK);
    CHECK_BIG_U64(&one, (uint64_t)1 << 63);
    rt_mem_set_budget(&env.mem, RT_MEM_UNLIMITED);
    env_close(&env);

    /* Without an arena only inline values work. */
    bi_pool bare;
    bi_pool_init(&bare, NULL);
    bigint small = BI_ZERO_INIT;
    bi_set_u64(&small, 3);
    CHECK_STATUS(bi_shl(&bare, &small, &small, 62), MS_OK);
    CHECK_BIG_U64(&small, (uint64_t)3 << 62);
    CHECK_STATUS(bi_shl(&bare, &small, &small, 1), MS_ERR_INTERNAL);
    CHECK_BIG_U64(&small, (uint64_t)3 << 62);

    /* A disposed arena is misuse (MS_ERR_INTERNAL), not exhaustion. */
    test_env gone;
    env_open(&gone);
    rt_mem_dispose(&gone.mem);
    CHECK_STATUS(bi_pow2(&gone.pool, &small, 64), MS_ERR_INTERNAL);
    CHECK_BIG_U64(&small, (uint64_t)3 << 62);
    CHECK_EQ(gone.mem.misuses, 1);
    CHECK_EQ(gone.pool.outstanding, 0);

    /* bi_init forgets a block: it stays in use until the arena is released. */
    test_env leak;
    env_open(&leak);
    CHECK_STATUS(bi_pow2(&leak.pool, &small, 64), MS_OK);
    bi_init(&small);
    CHECK_EQ(leak.pool.outstanding, 1);
    CHECK(leak.mem.live > 0);
    bi_pool_reset(&leak.pool);
    rt_mem_dispose(&leak.mem);
    CHECK_EQ(leak.mem.live, 0);
}

/* ---------------------------------------------------------------- known answers */

static void test_word_boundaries(void) {
    test_case("known answers: carries, borrows, products, shifts at word boundaries");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    bigint x = BI_ZERO_INIT, y = BI_ZERO_INIT, r = BI_ZERO_INIT, one = BI_ZERO_INIT;
    bi_set_u64(&one, 1);
    for (uint32_t k = 1; k <= 10; k++) {
        set_ones(&env, &x, 32u * k); /* 2^(32k) - 1 */
        CHECK_EQ(bi_bit_length(&x), 32u * k);
        /* The carry ripples through every limb... */
        CHECK_STATUS(bi_add(pool, &y, &x, &one), MS_OK);
        for (uint32_t i = 0; i < k; i++) expect_limbs[i] = 0;
        expect_limbs[k] = 1;
        CHECK_BIG(&y, expect_limbs, k + 1);
        CHECK_EQ(bi_bit_length(&y), 32u * k + 1u);
        /* ...and the borrow back down. */
        CHECK_STATUS(bi_sub(pool, &r, &y, &one), MS_OK);
        CHECK_BIG_EQ(&r, &x);
        CHECK_STATUS(bi_sub(pool, &r, &y, &x), MS_OK);
        CHECK_BIG_U64(&r, 1);
        CHECK_STATUS(bi_sub(pool, &r, &x, &x), MS_OK);
        CHECK(bi_is_zero(&r));
        /* (2^(32k) - 1)^2 = 2^(64k) - 2^(32k+1) + 1 */
        CHECK_STATUS(bi_mul(pool, &r, &x, &x), MS_OK);
        expect_limbs[0] = 1;
        for (uint32_t i = 1; i < k; i++) expect_limbs[i] = 0;
        expect_limbs[k] = 0xFFFFFFFEu;
        for (uint32_t i = k + 1; i < 2 * k; i++) expect_limbs[i] = UINT32_MAX;
        CHECK_BIG(&r, expect_limbs, 2 * k);
        /* (2^(32k) - 1)(2^32 - 1) = 2^(32k+32) - 2^(32k) - 2^32 + 1 */
        CHECK_STATUS(bi_mul_u32(pool, &r, &x, UINT32_MAX), MS_OK);
        expect_limbs[0] = 1;
        for (uint32_t i = 1; i < k; i++) expect_limbs[i] = UINT32_MAX;
        expect_limbs[k] = 0xFFFFFFFEu;
        CHECK_BIG(&r, expect_limbs, k + 1);
        /* 2^(32k) = (2^32 - 1) * sum_{i<k} 2^(32i) + 1 */
        uint32_t rem = 0;
        CHECK_STATUS(bi_divmod_u32(pool, &r, &y, UINT32_MAX, &rem), MS_OK);
        for (uint32_t i = 0; i < k; i++) expect_limbs[i] = 1;
        CHECK_BIG(&r, expect_limbs, k);
        CHECK_EQ(rem, 1);
        CHECK_STATUS(bi_divexact_u32(pool, &r, &x, UINT32_MAX), MS_OK);
        CHECK_BIG(&r, expect_limbs, k);
        /* Shifts across the boundary. */
        CHECK_STATUS(bi_shl(pool, &r, &x, 1), MS_OK);
        expect_limbs[0] = 0xFFFFFFFEu;
        for (uint32_t i = 1; i < k; i++) expect_limbs[i] = UINT32_MAX;
        expect_limbs[k] = 1;
        CHECK_BIG(&r, expect_limbs, k + 1);
        CHECK_STATUS(bi_shl(pool, &r, &x, 32), MS_OK);
        expect_limbs[0] = 0;
        for (uint32_t i = 1; i <= k; i++) expect_limbs[i] = UINT32_MAX;
        CHECK_BIG(&r, expect_limbs, k + 1);
    }

    /* Every single-bit position over four limbs. */
    for (uint32_t bit = 0; bit < 128; bit++) {
        CHECK_STATUS(bi_pow2(pool, &x, bit), MS_OK);
        for (uint32_t i = 0; i < 4; i++) expect_limbs[i] = 0;
        expect_limbs[bit / 32u] = (uint32_t)1 << (bit % 32u);
        CHECK_BIG(&x, expect_limbs, 4);
        CHECK_EQ(bi_bit_length(&x), bit + 1u);
        bigint fresh = BI_ZERO_INIT;
        CHECK_STATUS(bi_shl(pool, &fresh, &one, bit), MS_OK);
        CHECK_BIG_EQ(&fresh, &x);
        CHECK(bit >= 64 || fresh.cap == 0); /* fits two limbs: stays inline */
        bi_free(pool, &fresh);
        /* 2^bit - 1 borrows through every lower limb. */
        CHECK_STATUS(bi_sub(pool, &r, &x, &one), MS_OK);
        for (uint32_t i = 0; i < 4; i++) expect_limbs[i] = 0;
        ones_into(expect_limbs, bit);
        CHECK_BIG(&r, expect_limbs, 4);
    }

    /* The inline/heap boundary: 2^64 - 1 grows in place and keeps its block. */
    bigint w = BI_ZERO_INIT;
    bi_set_u64(&w, UINT64_MAX);
    CHECK_STATUS(bi_add(pool, &w, &w, &one), MS_OK);
    static const uint32_t two64[3] = {0, 0, 1};
    CHECK_BIG(&w, two64, 3);
    CHECK_STATUS(bi_sub(pool, &w, &w, &one), MS_OK);
    CHECK_BIG_U64(&w, UINT64_MAX);
    CHECK_EQ(w.cap, 4);

    /* Small products, sums and fused updates never touch the arena. */
    uint64_t requests = env.mem.requests;
    bigint s = BI_ZERO_INIT, t = BI_ZERO_INIT, u = BI_ZERO_INIT;
    bi_set_u64(&s, 0xFFFFu);
    bi_set_u64(&t, 0xFFFFFFFFu);
    CHECK_STATUS(bi_mul(pool, &u, &s, &t), MS_OK);
    CHECK_BIG_U64(&u, 0xFFFFull * 0xFFFFFFFFull);
    CHECK_STATUS(bi_addmul(pool, &u, &s, &t), MS_OK);
    CHECK_BIG_U64(&u, 2ull * 0xFFFFull * 0xFFFFFFFFull);
    CHECK_STATUS(bi_addmul_u32(pool, &u, &one, UINT32_MAX), MS_OK);
    CHECK_BIG_U64(&u, 2ull * 0xFFFFull * 0xFFFFFFFFull + 0xFFFFFFFFull);
    CHECK_STATUS(bi_submul(pool, &u, &s, &t), MS_OK);
    CHECK_BIG_U64(&u, 0x10000ull * 0xFFFFFFFFull);
    CHECK_STATUS(bi_mul_u32(pool, &u, &t, 0x10000u), MS_OK);
    CHECK_BIG_U64(&u, 0xFFFFFFFF0000ull);
    CHECK_STATUS(bi_add(pool, &u, &u, &t), MS_OK);
    CHECK_BIG_U64(&u, 0xFFFFFFFF0000ull + 0xFFFFFFFFull);
    CHECK_STATUS(bi_shl(pool, &u, &u, 15), MS_OK);
    CHECK_BIG_U64(&u, (0xFFFFFFFF0000ull + 0xFFFFFFFFull) << 15);
    CHECK(u.cap == 0 && env.mem.requests == requests);

    /* Division at word boundaries; two-limb divisors take Knuth's path. */
    static const uint32_t two128_m1[4] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    static const uint32_t two128[5] = {0, 0, 0, 0, 1};
    static const uint32_t two96[4] = {0, 0, 0, 1};
    static const uint32_t two64_p1[3] = {1, 0, 1};
    bigint q = BI_ZERO_INIT, m = BI_ZERO_INIT;
    bi_set_u64(&x, UINT64_MAX); /* (2^64 - 1) / (2^32 - 1) = 2^32 + 1 */
    bi_set_u64(&y, 0xFFFFFFFFu);
    CHECK_STATUS(bi_divmod(pool, &q, &m, &x, &y), MS_OK);
    CHECK_BIG_U64(&q, 0x100000001ull);
    CHECK(bi_is_zero(&m));
    set_limbs(&env, &x, two128_m1, 4); /* (2^128 - 1) / (2^64 - 1) = 2^64 + 1 */
    bi_set_u64(&y, UINT64_MAX);
    CHECK_STATUS(bi_divmod(pool, &q, &m, &x, &y), MS_OK);
    CHECK_BIG(&q, two64_p1, 3);
    CHECK(bi_is_zero(&m));
    set_limbs(&env, &y, two64_p1, 3); /* (2^128 - 1) / (2^64 + 1) = 2^64 - 1 */
    CHECK_STATUS(bi_divmod(pool, &q, &m, &x, &y), MS_OK);
    CHECK_BIG_U64(&q, UINT64_MAX);
    CHECK(bi_is_zero(&m));
    set_limbs(&env, &x, two128, 5); /* 2^128 = (2^64 + 1)(2^64 - 1) + 1 */
    CHECK_STATUS(bi_divmod(pool, &q, &m, &x, &y), MS_OK);
    CHECK_BIG_U64(&q, UINT64_MAX);
    CHECK_BIG_U64(&m, 1);
    set_limbs(&env, &x, two96, 4); /* 2^96 = (2^64 - 1) 2^32 + 2^32 */
    bi_set_u64(&y, UINT64_MAX);
    CHECK_STATUS(bi_divmod(pool, &q, &m, &x, &y), MS_OK);
    CHECK_BIG_U64(&q, 0x100000000ull);
    CHECK_BIG_U64(&m, 0x100000000ull);

    bigint all[] = {x, y, r, one, w, s, t, u, q, m};
    free_all(&env, all, (uint32_t)array_size(all));
    env_close(&env);
}

static void test_random_u64(void) {
    test_case("randomized u64 references: add, sub, mul, divmod, shifts, compare, ratio");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    rt_rng rng;
    rt_rng_seed(&rng, 0x5EED0001u);
    bigint a = BI_ZERO_INIT, b = BI_ZERO_INIT, c = BI_ZERO_INIT, q = BI_ZERO_INIT, r = BI_ZERO_INIT;
    uint32_t prod[4];
    for (uint32_t iter = 0; iter < 20000; iter++) {
        uint32_t abits = rt_rng_below(&rng, 65);
        uint32_t bbits = rt_rng_below(&rng, 65);
        uint64_t av = rand_value(&rng, abits);
        uint64_t bv = rand_value(&rng, bbits);
        bi_set_u64(&a, av);
        bi_set_u64(&b, bv);

        CHECK(bi_cmp(&a, &b) == (av < bv ? -1 : av > bv ? 1 : 0));
        CHECK(bi_eq(&a, &b) == (av == bv));
        CHECK_EQ(bi_bit_length(&a), ref_bit_length(av));

        /* Sum, with the native carry as a third limb. */
        uint64_t sum = av + bv;
        uint32_t sum_limbs[3] = {(uint32_t)sum, (uint32_t)(sum >> 32), sum < av ? 1u : 0u};
        CHECK_STATUS(bi_add(pool, &r, &a, &b), MS_OK);
        CHECK_BIG(&r, sum_limbs, 3);

        /* Checked difference. */
        bi_set_u64(&r, 77);
        if (av >= bv) {
            CHECK_STATUS(bi_sub(pool, &r, &a, &b), MS_OK);
            CHECK_BIG_U64(&r, av - bv);
        } else {
            CHECK_STATUS(bi_sub(pool, &r, &a, &b), MS_ERR_INTERNAL);
            CHECK_BIG_U64(&r, 77);
        }

        /* Full 128-bit product, and products that fit 64 bits natively. */
        ref_mul64(av, bv, prod);
        CHECK_STATUS(bi_mul(pool, &r, &a, &b), MS_OK);
        CHECK_BIG(&r, prod, 4);
        uint64_t cv = rand_value(&rng, rt_rng_below(&rng, 65 - abits));
        bi_set_u64(&c, cv);
        CHECK_STATUS(bi_mul(pool, &r, &a, &c), MS_OK);
        CHECK_BIG_U64(&r, av * cv);

        /* u32 multipliers and fused updates that stay below 2^64. */
        uint32_t m = (uint32_t)rand_value(&rng, 32);
        uint64_t a31 = av >> 33;
        uint64_t acc = rt_rng_next(&rng) >> 1;
        bi_set_u64(&c, a31);
        CHECK_STATUS(bi_mul_u32(pool, &r, &c, m), MS_OK);
        CHECK_BIG_U64(&r, a31 * m);
        bi_set_u64(&r, acc);
        CHECK_STATUS(bi_addmul_u32(pool, &r, &c, m), MS_OK);
        CHECK_BIG_U64(&r, acc + a31 * m);
        bigint mb = BI_ZERO_INIT;
        bi_set_u64(&mb, m);
        bi_set_u64(&r, acc);
        CHECK_STATUS(bi_addmul(pool, &r, &c, &mb), MS_OK);
        CHECK_BIG_U64(&r, acc + a31 * m);
        CHECK_STATUS(bi_submul(pool, &r, &c, &mb), MS_OK);
        CHECK_BIG_U64(&r, acc);
        uint64_t small = acc >> rt_rng_below(&rng, 64);
        bi_set_u64(&r, small);
        if (small >= a31 * m) {
            CHECK_STATUS(bi_submul(pool, &r, &c, &mb), MS_OK);
            CHECK_BIG_U64(&r, small - a31 * m);
        } else {
            CHECK_STATUS(bi_submul(pool, &r, &c, &mb), MS_ERR_INTERNAL);
            CHECK_BIG_U64(&r, small);
        }

        /* Quotient and remainder against native / and %. */
        if (bv != 0) {
            CHECK_STATUS(bi_divmod(pool, &q, &r, &a, &b), MS_OK);
            CHECK_BIG_U64(&q, av / bv);
            CHECK_BIG_U64(&r, av % bv);
            bi_set_u64(&q, 5);
            if (av % bv == 0) {
                CHECK_STATUS(bi_divexact(pool, &q, &a, &b), MS_OK);
                CHECK_BIG_U64(&q, av / bv);
            } else {
                CHECK_STATUS(bi_divexact(pool, &q, &a, &b), MS_ERR_INTERNAL);
                CHECK_BIG_U64(&q, 5);
            }
            if (bv <= UINT32_MAX) {
                uint32_t rem = 0;
                CHECK_STATUS(bi_divmod_u32(pool, &q, &a, (uint32_t)bv, &rem), MS_OK);
                CHECK_BIG_U64(&q, av / bv);
                CHECK_EQ(rem, av % bv);
            }
        }

        /* Shifts, also past 64 bits. */
        uint32_t sh = rt_rng_below(&rng, 64);
        uint64_t lo = av << sh;
        uint64_t hi = sh ? av >> (64u - sh) : 0;
        uint32_t shifted[6] = {0, 0, (uint32_t)lo, (uint32_t)(lo >> 32), (uint32_t)hi,
                               (uint32_t)(hi >> 32)};
        CHECK_STATUS(bi_shl(pool, &r, &a, sh), MS_OK);
        CHECK_BIG(&r, shifted + 2, 4);
        CHECK_STATUS(bi_shl(pool, &r, &a, sh + 64u), MS_OK);
        CHECK_BIG(&r, shifted, 6);

        /* Ratios of values below 2^53: IEEE division is the reference, also
         * through long division ((a 2^k c) / (b 2^k c) is the same value). */
        uint64_t na = av >> 11;
        uint64_t nb = (bv >> 11) | 1u;
        double expected = (double)na / (double)nb;
        bi_set_u64(&a, na);
        bi_set_u64(&b, nb);
        double out = -1.0;
        CHECK_STATUS(bi_ratio(pool, &a, &b, &out), MS_OK);
        CHECK_SAME(out, expected);
        uint32_t odd = rand_limb(&rng) | 1u;
        uint32_t k = 54u + rt_rng_below(&rng, 300);
        CHECK_STATUS(bi_shl(pool, &a, &a, k), MS_OK);
        CHECK_STATUS(bi_shl(pool, &b, &b, k), MS_OK);
        CHECK_STATUS(bi_mul_u32(pool, &a, &a, odd), MS_OK);
        CHECK_STATUS(bi_mul_u32(pool, &b, &b, odd), MS_OK);
        out = -1.0;
        CHECK_STATUS(bi_ratio(pool, &a, &b, &out), MS_OK);
        CHECK_SAME(out, expected);
        if (na <= nb) {
            out = -1.0;
            CHECK_STATUS(bi_probability(pool, &a, &b, false, &out), MS_OK);
            CHECK_SAME(out, expected);
        }
    }
    bigint all[] = {a, b, c, q, r};
    free_all(&env, all, (uint32_t)array_size(all));
    env_close(&env);
}

/* ---------------------------------------------------------------- identities */

static void test_identities(void) {
    test_case("randomized multi-limb identities: ring laws, q*d + r, shifts, fused updates");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    rt_rng rng;
    rt_rng_seed(&rng, 0x1DE47172u);
    enum { VA, VB, VC, VS, VT, VU, VV, VQ, VR, VD, VCOUNT };
    bigint v[VCOUNT];
    init_all(v, VCOUNT);
    bigint one = BI_ZERO_INIT;
    bi_set_u64(&one, 1);
    for (uint32_t iter = 0; iter < 1500; iter++) {
        uint32_t span = iter % 4 == 0 ? RAND_LIMBS : 12u;
        rand_big(&env, &rng, &v[VA], span);
        rand_big(&env, &rng, &v[VB], span);
        rand_big(&env, &rng, &v[VC], span);
        const bigint *a = &v[VA], *b = &v[VB], *c = &v[VC];

        /* (a + b) - b == a, a + b == b + a */
        CHECK_STATUS(bi_add(pool, &v[VS], a, b), MS_OK);
        CHECK_STATUS(bi_add(pool, &v[VT], b, a), MS_OK);
        CHECK_BIG_EQ(&v[VS], &v[VT]);
        CHECK_STATUS(bi_sub(pool, &v[VT], &v[VS], b), MS_OK);
        CHECK_BIG_EQ(&v[VT], a);
        CHECK_STATUS(bi_sub(pool, &v[VT], &v[VS], a), MS_OK);
        CHECK_BIG_EQ(&v[VT], b);
        CHECK(bi_cmp(&v[VS], a) == (bi_is_zero(b) ? 0 : 1));

        /* a b == b a and a (b + c) == a b + a c (through bi_addmul) */
        CHECK_STATUS(bi_mul(pool, &v[VS], a, b), MS_OK);
        CHECK_STATUS(bi_mul(pool, &v[VT], b, a), MS_OK);
        CHECK_BIG_EQ(&v[VS], &v[VT]);
        CHECK_STATUS(bi_add(pool, &v[VU], b, c), MS_OK);
        CHECK_STATUS(bi_mul(pool, &v[VT], a, &v[VU]), MS_OK);
        set_copy(&env, &v[VV], &v[VS]);
        CHECK_STATUS(bi_addmul(pool, &v[VV], a, c), MS_OK);
        CHECK_BIG_EQ(&v[VV], &v[VT]);

        /* c + a b, and bi_submul undoes bi_addmul */
        CHECK_STATUS(bi_add(pool, &v[VT], c, &v[VS]), MS_OK);
        set_copy(&env, &v[VV], c);
        CHECK_STATUS(bi_addmul(pool, &v[VV], a, b), MS_OK);
        CHECK_BIG_EQ(&v[VV], &v[VT]);
        CHECK_STATUS(bi_submul(pool, &v[VV], a, b), MS_OK);
        CHECK_BIG_EQ(&v[VV], c);
        /* one below a b: underflow, unchanged */
        if (!bi_is_zero(&v[VS])) {
            CHECK_STATUS(bi_sub(pool, &v[VV], &v[VS], &one), MS_OK);
            set_copy(&env, &v[VU], &v[VV]);
            CHECK_STATUS(bi_submul(pool, &v[VV], a, b), MS_ERR_INTERNAL);
            CHECK_BIG_EQ(&v[VV], &v[VU]);
            CHECK_STATUS(bi_submul(pool, &v[VS], a, b), MS_OK);
            CHECK(bi_is_zero(&v[VS]));
        }

        /* u32 forms agree with one-limb operands */
        uint32_t m = rand_limb(&rng);
        bigint mb = BI_ZERO_INIT;
        bi_set_u64(&mb, m);
        CHECK_STATUS(bi_mul_u32(pool, &v[VS], a, m), MS_OK);
        CHECK_STATUS(bi_mul(pool, &v[VT], a, &mb), MS_OK);
        CHECK_BIG_EQ(&v[VS], &v[VT]);
        set_copy(&env, &v[VU], c);
        set_copy(&env, &v[VV], c);
        CHECK_STATUS(bi_addmul_u32(pool, &v[VU], a, m), MS_OK);
        CHECK_STATUS(bi_addmul(pool, &v[VV], a, &mb), MS_OK);
        CHECK_BIG_EQ(&v[VU], &v[VV]);

        /* a << s == a * 2^s */
        uint32_t s = rt_rng_below(&rng, 300);
        CHECK_STATUS(bi_shl(pool, &v[VS], a, s), MS_OK);
        CHECK_STATUS(bi_pow2(pool, &v[VU], s), MS_OK);
        CHECK_STATUS(bi_mul(pool, &v[VT], a, &v[VU]), MS_OK);
        CHECK_BIG_EQ(&v[VS], &v[VT]);
        CHECK_EQ(bi_bit_length(&v[VS]), bi_is_zero(a) ? 0 : bi_bit_length(a) + s);

        /* division: q d + r == a with r < d, and the constructed form
         * a d + r divides back to exactly (a, r) */
        set_copy(&env, &v[VD], bi_is_zero(b) ? &one : b);
        const bigint *d = &v[VD];
        CHECK_STATUS(bi_divmod(pool, &v[VQ], &v[VR], c, d), MS_OK);
        CHECK(bi_cmp(&v[VR], d) < 0);
        set_copy(&env, &v[VT], &v[VR]);
        CHECK_STATUS(bi_addmul(pool, &v[VT], &v[VQ], d), MS_OK);
        CHECK_BIG_EQ(&v[VT], c);
        CHECK_STATUS(bi_mul(pool, &v[VT], a, d), MS_OK);
        CHECK_STATUS(bi_divexact(pool, &v[VU], &v[VT], d), MS_OK);
        CHECK_BIG_EQ(&v[VU], a);
        CHECK_STATUS(bi_add(pool, &v[VT], &v[VT], &v[VR]), MS_OK);
        CHECK_STATUS(bi_divmod(pool, &v[VQ], &v[VS], &v[VT], d), MS_OK);
        CHECK_BIG_EQ(&v[VQ], a);
        CHECK_BIG_EQ(&v[VS], &v[VR]);
        if (!bi_is_zero(&v[VR])) {
            bi_set_u64(&v[VU], 5);
            CHECK_STATUS(bi_divexact(pool, &v[VU], &v[VT], d), MS_ERR_INTERNAL);
            CHECK_BIG_U64(&v[VU], 5);
        }
        /* single-limb divisors */
        if (m != 0) {
            uint32_t rem = 0;
            CHECK_STATUS(bi_divmod_u32(pool, &v[VQ], c, m, &rem), MS_OK);
            CHECK(rem < m);
            bi_set_u64(&v[VT], rem);
            CHECK_STATUS(bi_addmul_u32(pool, &v[VT], &v[VQ], m), MS_OK);
            CHECK_BIG_EQ(&v[VT], c);
        }
    }
    free_all(&env, v, VCOUNT);
    env_close(&env);
}

/* ---------------------------------------------------------------- aliasing */

static void test_aliasing(void) {
    test_case("aliasing: an output may be any input, results match distinct operands");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    rt_rng rng;
    rt_rng_seed(&rng, 0xA11A5u);
    enum { VA, VB, VX, VY, VREF, VREF2, VCOUNT };
    bigint v[VCOUNT];
    init_all(v, VCOUNT);
    bigint *a = &v[VA], *b = &v[VB], *x = &v[VX], *y = &v[VY], *ref = &v[VREF], *ref2 = &v[VREF2];
    bigint zero = BI_ZERO_INIT, one = BI_ZERO_INIT, two = BI_ZERO_INIT;
    bi_set_u64(&one, 1);
    bi_set_u64(&two, 2);
    for (uint32_t iter = 0; iter < 400; iter++) {
        uint32_t span = iter % 3 == 0 ? 30u : 4u; /* long values and the inline/heap boundary */
        rand_big(&env, &rng, a, span);
        rand_big(&env, &rng, b, span);
        uint32_t m = rand_limb(&rng);
        uint32_t s = rt_rng_below(&rng, 100);

        /* add */
        CHECK_STATUS(bi_add(pool, ref, a, b), MS_OK);
        set_copy(&env, x, a);
        set_copy(&env, y, b);
        CHECK_STATUS(bi_add(pool, x, x, y), MS_OK);
        CHECK_BIG_EQ(x, ref);
        CHECK_BIG_EQ(y, b);
        set_copy(&env, x, a);
        CHECK_STATUS(bi_add(pool, y, x, y), MS_OK);
        CHECK_BIG_EQ(y, ref);
        CHECK_BIG_EQ(x, a);
        CHECK_STATUS(bi_add(pool, ref, a, a), MS_OK);
        CHECK_STATUS(bi_add(pool, x, x, x), MS_OK);
        CHECK_BIG_EQ(x, ref);

        /* sub: larger minus smaller */
        const bigint *hi = bi_cmp(a, b) >= 0 ? a : b;
        const bigint *lo = hi == a ? b : a;
        CHECK_STATUS(bi_sub(pool, ref, hi, lo), MS_OK);
        set_copy(&env, x, hi);
        set_copy(&env, y, lo);
        CHECK_STATUS(bi_sub(pool, x, x, y), MS_OK);
        CHECK_BIG_EQ(x, ref);
        CHECK_BIG_EQ(y, lo);
        set_copy(&env, x, hi);
        CHECK_STATUS(bi_sub(pool, y, x, y), MS_OK);
        CHECK_BIG_EQ(y, ref);
        CHECK_BIG_EQ(x, hi);
        CHECK_STATUS(bi_sub(pool, x, x, x), MS_OK);
        CHECK(bi_is_zero(x));

        /* products */
        CHECK_STATUS(bi_mul(pool, ref, a, b), MS_OK);
        set_copy(&env, x, a);
        set_copy(&env, y, b);
        CHECK_STATUS(bi_mul(pool, x, x, y), MS_OK);
        CHECK_BIG_EQ(x, ref);
        CHECK_BIG_EQ(y, b);
        set_copy(&env, x, a);
        CHECK_STATUS(bi_mul(pool, y, x, y), MS_OK);
        CHECK_BIG_EQ(y, ref);
        CHECK_STATUS(bi_mul(pool, ref, a, a), MS_OK);
        CHECK_STATUS(bi_mul(pool, x, x, x), MS_OK);
        CHECK_BIG_EQ(x, ref);
        CHECK_STATUS(bi_mul_u32(pool, ref, a, m), MS_OK);
        set_copy(&env, x, a);
        CHECK_STATUS(bi_mul_u32(pool, x, x, m), MS_OK);
        CHECK_BIG_EQ(x, ref);

        /* fused updates whose accumulator is also a factor */
        CHECK_STATUS(bi_mul(pool, ref2, a, b), MS_OK);
        CHECK_STATUS(bi_add(pool, ref, ref2, a), MS_OK); /* a + a b */
        set_copy(&env, x, a);
        set_copy(&env, y, b);
        CHECK_STATUS(bi_addmul(pool, x, x, y), MS_OK);
        CHECK_BIG_EQ(x, ref);
        CHECK_STATUS(bi_add(pool, ref, ref2, b), MS_OK); /* b + a b */
        set_copy(&env, x, a);
        CHECK_STATUS(bi_addmul(pool, y, x, y), MS_OK);
        CHECK_BIG_EQ(y, ref);
        CHECK_STATUS(bi_mul(pool, ref2, a, a), MS_OK);
        CHECK_STATUS(bi_add(pool, ref, ref2, a), MS_OK); /* a + a^2 */
        CHECK_STATUS(bi_addmul(pool, x, x, x), MS_OK);
        CHECK_BIG_EQ(x, ref);
        CHECK_STATUS(bi_mul_u32(pool, ref2, a, m), MS_OK);
        CHECK_STATUS(bi_add(pool, ref, ref2, a), MS_OK); /* a + a m */
        set_copy(&env, x, a);
        CHECK_STATUS(bi_addmul_u32(pool, x, x, m), MS_OK);
        CHECK_BIG_EQ(x, ref);

        /* checked fused subtraction with the accumulator as a factor */
        set_copy(&env, x, a);
        CHECK_STATUS(bi_submul(pool, x, x, &one), MS_OK);
        CHECK(bi_is_zero(x));
        set_copy(&env, x, a);
        CHECK_STATUS(bi_submul(pool, x, x, &zero), MS_OK);
        CHECK_BIG_EQ(x, a);
        CHECK_STATUS(bi_submul(pool, x, x, &two), bi_is_zero(a) ? MS_OK : MS_ERR_INTERNAL);
        CHECK_BIG_EQ(x, a);
        set_copy(&env, x, a);
        set_copy(&env, y, b);
        bool fits = bi_is_zero(a) || bi_is_zero(b) || bi_eq(a, &one);
        CHECK_STATUS(bi_submul(pool, y, x, y), fits ? MS_OK : MS_ERR_INTERNAL);
        if (fits && bi_eq(a, &one)) {
            CHECK(bi_is_zero(y));
        } else {
            CHECK_BIG_EQ(y, b);
        }

        /* shifts */
        CHECK_STATUS(bi_shl(pool, ref, a, s), MS_OK);
        set_copy(&env, x, a);
        CHECK_STATUS(bi_shl(pool, x, x, s), MS_OK);
        CHECK_BIG_EQ(x, ref);

        /* division with outputs on the operands */
        if (!bi_is_zero(b)) {
            CHECK_STATUS(bi_divmod(pool, ref, ref2, a, b), MS_OK);
            set_copy(&env, x, a);
            set_copy(&env, y, b);
            CHECK_STATUS(bi_divmod(pool, x, y, x, y), MS_OK);
            CHECK_BIG_EQ(x, ref);
            CHECK_BIG_EQ(y, ref2);
            set_copy(&env, x, a);
            set_copy(&env, y, b);
            CHECK_STATUS(bi_divmod(pool, y, x, x, y), MS_OK);
            CHECK_BIG_EQ(y, ref);
            CHECK_BIG_EQ(x, ref2);
            set_copy(&env, x, a);
            CHECK_STATUS(bi_divmod(pool, x, NULL, x, b), MS_OK);
            CHECK_BIG_EQ(x, ref);
            set_copy(&env, x, a);
            CHECK_STATUS(bi_divmod(pool, NULL, x, x, b), MS_OK);
            CHECK_BIG_EQ(x, ref2);
            set_copy(&env, y, b);
            CHECK_STATUS(bi_divmod(pool, y, NULL, a, y), MS_OK);
            CHECK_BIG_EQ(y, ref);
            CHECK_STATUS(bi_mul(pool, x, a, b), MS_OK);
            CHECK_STATUS(bi_divexact(pool, x, x, b), MS_OK);
            CHECK_BIG_EQ(x, a);
            /* the two outputs must differ */
            CHECK_STATUS(bi_divmod(pool, x, x, x, b), MS_ERR_INTERNAL);
            CHECK_BIG_EQ(x, a);
        }
        if (m != 0) {
            uint32_t rem_ref = 0, rem = 0;
            CHECK_STATUS(bi_divmod_u32(pool, ref, a, m, &rem_ref), MS_OK);
            set_copy(&env, x, a);
            CHECK_STATUS(bi_divmod_u32(pool, x, x, m, &rem), MS_OK);
            CHECK_BIG_EQ(x, ref);
            CHECK_EQ(rem, rem_ref);
            CHECK_STATUS(bi_mul_u32(pool, x, a, m), MS_OK);
            CHECK_STATUS(bi_divexact_u32(pool, x, x, m), MS_OK);
            CHECK_BIG_EQ(x, a);
        }
        set_copy(&env, x, a);
        CHECK_STATUS(bi_copy(pool, x, x), MS_OK);
        CHECK_BIG_EQ(x, a);
    }
    free_all(&env, v, VCOUNT);
    env_close(&env);
}

/* ---------------------------------------------------------------- division */

static void check_divmod(test_env *env, const uint32_t *u, uint32_t ulen, const uint32_t *dv,
                         uint32_t dlen, const uint32_t *q, uint32_t qlen, const uint32_t *r,
                         uint32_t rlen) {
    bigint a = BI_ZERO_INIT, d = BI_ZERO_INIT, qq = BI_ZERO_INIT, rr = BI_ZERO_INIT;
    set_limbs(env, &a, u, ulen);
    set_limbs(env, &d, dv, dlen);
    CHECK_STATUS(bi_divmod(&env->pool, &qq, &rr, &a, &d), MS_OK);
    CHECK_BIG(&qq, q, qlen);
    CHECK_BIG(&rr, r, rlen);
    bool exact = big_is(&rr, NULL, 0);
    bi_set_u64(&qq, 9);
    CHECK_STATUS(bi_divexact(&env->pool, &qq, &a, &d), exact ? MS_OK : MS_ERR_INTERNAL);
    if (exact) {
        CHECK_BIG(&qq, q, qlen);
    } else {
        CHECK_BIG_U64(&qq, 9);
    }
    bigint all[] = {a, d, qq, rr};
    free_all(env, all, 4);
}

static void test_division_known(void) {
    test_case("division known answers: Knuth corrections, Mersenne quotients, 10^400");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;

    /* Step D6: qhat = 2^32 - 1 is one too large and is added back.
     * (2^127 - 2^95) = (2^32 - 2)(2^95 + 1) + 2^95 - 2^32 + 2 */
    static const uint32_t addback_u[4] = {0, 0, 0x80000000u, 0x7FFFFFFFu};
    static const uint32_t addback_v[3] = {1, 0, 0x80000000u};
    static const uint32_t addback_q[1] = {0xFFFFFFFEu};
    static const uint32_t addback_r[3] = {2, UINT32_MAX, 0x7FFFFFFFu};
    check_divmod(&env, addback_u, 4, addback_v, 3, addback_q, 1, addback_r, 3);
    /* Step D3: qhat = 2^32 + 1 is corrected twice, the second time leaving
     * the loop through rhat >= 2^32: u = v 2^32 - 1 = v (2^32 - 1) + v - 1 */
    static const uint32_t qhat_u[3] = {UINT32_MAX, 0xFFFFFFFEu, 0x80000000u};
    static const uint32_t qhat_v[2] = {UINT32_MAX, 0x80000000u};
    static const uint32_t qhat_q[1] = {UINT32_MAX};
    static const uint32_t qhat_r[2] = {0xFFFFFFFEu, 0x80000000u};
    check_divmod(&env, qhat_u, 3, qhat_v, 2, qhat_q, 1, qhat_r, 2);
    /* Normalization by 31 bits: d = 2^32 + 5, u = d (2^64 + 3) + d - 1 */
    static const uint32_t norm_u[4] = {19, 4, 5, 1};
    static const uint32_t norm_v[2] = {5, 1};
    static const uint32_t norm_q[3] = {3, 0, 1};
    static const uint32_t norm_r[2] = {4, 1};
    check_divmod(&env, norm_u, 4, norm_v, 2, norm_q, 3, norm_r, 2);
    /* Exact division by the same unnormalized two-limb divisor. */
    static const uint32_t exact_u[4] = {15, 3, 5, 1};
    check_divmod(&env, exact_u, 4, norm_v, 2, norm_q, 3, NULL, 0);

    /* Mersenne numbers: 2^k - 1 = (2^j - 1) sum_{i<k/j} 2^(k%j + ij) + 2^(k%j) - 1 */
    static const uint32_t mersenne[][2] = {{4000, 97}, {4000, 100}, {6400, 64}, {1000, 33},
                                           {777, 32},  {3000, 1000}, {65, 64},  {200, 199},
                                           {100, 7},   {64, 32},     {6000, 3001}, {96, 95},
                                           {6400, 6399}, {32, 32},   {31, 64}};
    bigint a = BI_ZERO_INIT, d = BI_ZERO_INIT, q = BI_ZERO_INIT, r = BI_ZERO_INIT, t = BI_ZERO_INIT;
    for (size_t i = 0; i < array_size(mersenne); i++) {
        uint32_t k = mersenne[i][0], j = mersenne[i][1];
        uint32_t quotient_terms = k / j, low = k % j;
        uint32_t qlimbs = (k + 31u) / 32u + 1u;
        CHECK(qlimbs <= TEST_MAX_LIMBS);
        for (uint32_t w = 0; w < qlimbs; w++) expect_limbs[w] = 0;
        for (uint32_t term = 0; term < quotient_terms; term++) {
            uint32_t bit = low + term * j;
            expect_limbs[bit / 32u] |= (uint32_t)1 << (bit % 32u);
        }
        set_ones(&env, &a, k);
        set_ones(&env, &d, j);
        CHECK_STATUS(bi_divmod(pool, &q, &r, &a, &d), MS_OK);
        CHECK_BIG(&q, expect_limbs, qlimbs);
        CHECK_BIG(&r, test_limbs, ones_into(test_limbs, low));
        bi_set_u64(&t, 9);
        CHECK_STATUS(bi_divexact(pool, &t, &a, &d), low == 0 && k >= j ? MS_OK : MS_ERR_INTERNAL);
        if (low == 0 && k >= j) CHECK_BIG(&t, expect_limbs, qlimbs);
    }

    /* 10^400 from repeated multiplication reproduces the fixture. */
    bigint p400 = BI_ZERO_INIT, p200 = BI_ZERO_INIT, one = BI_ZERO_INIT;
    bi_set_u64(&one, 1);
    set_pow10(&env, &p400, 400);
    CHECK_BIG_FX(&p400, FX_POW10_400);
    set_pow10(&env, &p200, 200);
    /* 10^400 = 3 (10^400 - 1) / 3 + 1, with the third as a 42-limb divisor */
    uint32_t rem = 0;
    CHECK_STATUS(bi_divmod_u32(pool, &q, &p400, 3, &rem), MS_OK);
    CHECK_BIG_FX(&q, FX_THIRD);
    CHECK_EQ(rem, 1);
    set_fx(&env, &d, FX_THIRD);
    CHECK_STATUS(bi_divmod(pool, &q, &r, &p400, &d), MS_OK);
    CHECK_BIG_U64(&q, 3);
    CHECK_BIG_U64(&r, 1);
    set_fx(&env, &a, FX_POW10_400_M1);
    CHECK_STATUS(bi_divexact(pool, &q, &a, &d), MS_OK);
    CHECK_BIG_U64(&q, 3);
    bi_set_u64(&q, 9);
    CHECK_STATUS(bi_divexact(pool, &q, &p400, &d), MS_ERR_INTERNAL);
    CHECK_BIG_U64(&q, 9);
    /* (10^400 - 1) / 9 is the repunit: 9 R + 1 = 10^400 */
    CHECK_STATUS(bi_divexact_u32(pool, &q, &a, 9), MS_OK);
    CHECK_STATUS(bi_mul_u32(pool, &t, &q, 9), MS_OK);
    CHECK_STATUS(bi_add(pool, &t, &t, &one), MS_OK);
    CHECK_BIG_EQ(&t, &p400);
    /* 10^400 / 10^200 = 10^200; 10^400 = (10^200 + 1)(10^200 - 1) + 1 */
    CHECK_STATUS(bi_divexact(pool, &q, &p400, &p200), MS_OK);
    CHECK_BIG_EQ(&q, &p200);
    CHECK_STATUS(bi_add(pool, &d, &p200, &one), MS_OK);
    CHECK_STATUS(bi_sub(pool, &t, &p200, &one), MS_OK);
    CHECK_STATUS(bi_divmod(pool, &q, &r, &p400, &d), MS_OK);
    CHECK_BIG_EQ(&q, &t);
    CHECK_BIG_U64(&r, 1);
    CHECK_STATUS(bi_divmod(pool, &q, &r, &a, &d), MS_OK);
    CHECK_BIG_EQ(&q, &t);
    CHECK(bi_is_zero(&r));

    /* Small dividends, zero, and division by zero (outputs unchanged). */
    bi_set_u64(&a, 5);
    CHECK_STATUS(bi_divmod(pool, &q, &r, &a, &p200), MS_OK);
    CHECK(bi_is_zero(&q));
    CHECK_BIG_U64(&r, 5);
    bi_set_u64(&q, 9);
    CHECK_STATUS(bi_divexact(pool, &q, &a, &p200), MS_ERR_INTERNAL);
    CHECK_BIG_U64(&q, 9);
    bigint zero = BI_ZERO_INIT;
    CHECK_STATUS(bi_divexact(pool, &q, &zero, &p200), MS_OK);
    CHECK(bi_is_zero(&q));
    bi_set_u64(&q, 9);
    bi_set_u64(&r, 8);
    rem = 7;
    CHECK_STATUS(bi_divmod(pool, &q, &r, &p400, &zero), MS_ERR_INTERNAL);
    CHECK_STATUS(bi_divexact(pool, &q, &p400, &zero), MS_ERR_INTERNAL);
    CHECK_STATUS(bi_divmod_u32(pool, &q, &p400, 0, &rem), MS_ERR_INTERNAL);
    CHECK_STATUS(bi_divexact_u32(pool, &q, &p400, 0), MS_ERR_INTERNAL);
    CHECK_BIG_U64(&q, 9);
    CHECK_BIG_U64(&r, 8);
    CHECK_EQ(rem, 7);
    /* Remainder only. */
    CHECK_STATUS(bi_divmod_u32(pool, NULL, &p400, 7, &rem), MS_OK);
    CHECK_EQ(rem, 4); /* 10^400 = (10^6)^66 * 10^4 = 10^4 = 4 (mod 7) */
    CHECK_STATUS(bi_divmod(pool, NULL, &r, &p400, &p200), MS_OK);
    CHECK(bi_is_zero(&r));

    bigint all[] = {a, d, q, r, t, p400, p200};
    free_all(&env, all, (uint32_t)array_size(all));
    env_close(&env);
}

/* ---------------------------------------------------------------- binomials */

static void test_binomials(void) {
    test_case("binomials: fixtures, u64 Pascal, symmetry, recurrence, Vandermonde, C(6400,3200)");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    rt_rng rng;
    rt_rng_seed(&rng, 0xB1A0u);
    bigint c = BI_ZERO_INIT, t = BI_ZERO_INIT, u = BI_ZERO_INIT, x = BI_ZERO_INIT, sum = BI_ZERO_INIT;

    /* Frozen fixtures, including C(6400, 3200) and the out-of-range zero. */
    for (size_t i = 0; i < BIGINT_BINOMIAL_COUNT; i++) {
        const BigintBinomialCase *bc = &BIGINT_BINOMIALS[i];
        bi_set_u64(&c, 12345); /* any previous value is replaced */
        CHECK_STATUS(bi_binomial(pool, &c, bc->n, bc->k), MS_OK);
        CHECK_BIG_FX(&c, bc->value);
        CHECK_EQ(bi_bit_length(&c), bc->bit_length);
        if (bc->k <= bc->n) {
            CHECK_STATUS(bi_binomial(pool, &t, bc->n, bc->n - bc->k), MS_OK);
            CHECK_BIG_EQ(&t, &c);
        }
    }

    /* Exact u64 Pascal triangle up to n = 67 (C(67, 33) < 2^64). */
    uint64_t row[68];
    row[0] = 1;
    for (uint32_t n = 0; n <= 67; n++) {
        if (n > 0) {
            row[n] = 1;
            for (uint32_t k = n - 1; k >= 1; k--) row[k] += row[k - 1];
        }
        for (uint32_t k = 0; k <= n + 1; k++) {
            CHECK_STATUS(bi_binomial(pool, &c, n, k), MS_OK);
            CHECK_BIG_U64(&c, k <= n ? row[k] : 0);
        }
    }
    /* row now holds C(67, k); step C(64, 32) -> C(64, 33) across the inline boundary */
    CHECK_STATUS(bi_binomial(pool, &x, 64, 32), MS_OK);
    CHECK(x.cap == 0);
    CHECK_STATUS(bi_binomial_next(pool, &x, 64, 32), MS_OK);
    CHECK_STATUS(bi_binomial(pool, &t, 64, 33), MS_OK);
    CHECK_BIG_EQ(&x, &t);

    /* Pascal's rule, symmetry and C(n, k) k = C(n-1, k-1) n at random points. */
    for (uint32_t iter = 0; iter < 60; iter++) {
        uint32_t n = 2u + rt_rng_below(&rng, iter < 15 ? 6399u : 500u);
        uint32_t k = 1u + rt_rng_below(&rng, n - 1u);
        CHECK_STATUS(bi_binomial(pool, &c, n, k), MS_OK);
        CHECK_STATUS(bi_binomial(pool, &t, n - 1u, k - 1u), MS_OK);
        CHECK_STATUS(bi_binomial(pool, &u, n - 1u, k), MS_OK);
        CHECK_STATUS(bi_add(pool, &x, &t, &u), MS_OK);
        CHECK_BIG_EQ(&x, &c);
        CHECK_STATUS(bi_mul_u32(pool, &x, &c, k), MS_OK);
        CHECK_STATUS(bi_mul_u32(pool, &t, &t, n), MS_OK);
        CHECK_BIG_EQ(&x, &t);
        CHECK_STATUS(bi_binomial(pool, &t, n, n - k), MS_OK);
        CHECK_BIG_EQ(&t, &c);
    }

    /* Extreme arguments. */
    CHECK_STATUS(bi_binomial(pool, &c, UINT32_MAX, 0), MS_OK);
    CHECK_BIG_U64(&c, 1);
    CHECK_STATUS(bi_binomial(pool, &c, UINT32_MAX, 1), MS_OK);
    CHECK_BIG_U64(&c, UINT32_MAX);
    CHECK_STATUS(bi_binomial(pool, &c, UINT32_MAX, UINT32_MAX - 1u), MS_OK);
    CHECK_BIG_U64(&c, UINT32_MAX);
    CHECK_STATUS(bi_binomial(pool, &c, UINT32_MAX, 2), MS_OK);
    CHECK_BIG_U64(&c, (uint64_t)UINT32_MAX * (UINT32_MAX / 2u)); /* (2^32-1)(2^32-2)/2 */
    CHECK_STATUS(bi_binomial(pool, &c, UINT32_MAX, UINT32_MAX), MS_OK);
    CHECK_BIG_U64(&c, 1);
    CHECK_STATUS(bi_binomial(pool, &c, 0, 1), MS_OK);
    CHECK(bi_is_zero(&c));
    CHECK_STATUS(bi_binomial(pool, &c, 5, UINT32_MAX), MS_OK);
    CHECK(bi_is_zero(&c));

    /* The recurrence across a whole row agrees with bi_binomial, ends in
     * C(6400, 6401) = 0, and the row sums to 2^6400. */
    bi_set_u64(&x, 1);
    bi_set_zero(&sum);
    for (uint32_t j = 0; j <= 6400; j++) {
        if (j % 640 == 0 || j == 1999 || j == 3200) {
            CHECK_STATUS(bi_binomial(pool, &t, 6400, j), MS_OK);
            CHECK_BIG_EQ(&x, &t);
        }
        if (j == 3200) CHECK_BIG_FX(&x, FX_C6400_3200);
        CHECK_STATUS(bi_add(pool, &sum, &sum, &x), MS_OK);
        CHECK_STATUS(bi_binomial_next(pool, &x, 6400, j), MS_OK);
    }
    CHECK(bi_is_zero(&x));
    CHECK_STATUS(bi_pow2(pool, &t, 6400), MS_OK);
    CHECK_BIG_EQ(&sum, &t);
    bi_set_u64(&x, 7);
    CHECK_STATUS(bi_binomial_next(pool, &x, 10, 10), MS_OK); /* C(10, 11) */
    CHECK(bi_is_zero(&x));
    bi_set_u64(&x, 7);
    CHECK_STATUS(bi_binomial_next(pool, &x, 10, 12), MS_OK);
    CHECK(bi_is_zero(&x));

    /* Vandermonde: sum_k C(3200, k)^2 = C(6400, 3200). */
    bi_set_u64(&x, 1);
    bi_set_zero(&sum);
    for (uint32_t k = 0; k <= 3200; k++) {
        CHECK_STATUS(bi_addmul(pool, &sum, &x, &x), MS_OK);
        CHECK_STATUS(bi_binomial_next(pool, &x, 3200, k), MS_OK);
    }
    CHECK_BIG_FX(&sum, FX_C6400_3200);

    /* Fixture neighbours: C(6375, 3) = C(6374, 2) 6375 / 3 and
     * C(6400, 2) = C(6400, 1) 6399 / 2. */
    set_fx(&env, &x, FX_ARRAY(bigint_c_6374_2_limbs));
    CHECK_STATUS(bi_mul_u32(pool, &x, &x, 6375), MS_OK);
    CHECK_STATUS(bi_divexact_u32(pool, &x, &x, 3), MS_OK);
    CHECK_BIG_FX(&x, FX_ARRAY(bigint_c_6375_3_limbs));
    set_fx(&env, &x, FX_ARRAY(bigint_c_6400_1_limbs));
    CHECK_STATUS(bi_binomial_next(pool, &x, 6400, 1), MS_OK);
    CHECK_BIG_FX(&x, FX_ARRAY(bigint_c_6400_2_limbs));

    /* A value that is not C(n, j) is rejected and kept. */
    bi_set_u64(&x, 121); /* 121 * 7 / 4 */
    CHECK_STATUS(bi_binomial_next(pool, &x, 10, 3), MS_ERR_INTERNAL);
    CHECK_BIG_U64(&x, 121);
    bi_set_u64(&t, 1);
    CHECK_STATUS(bi_pow2(pool, &x, 5000), MS_OK);
    CHECK_STATUS(bi_add(pool, &x, &x, &t), MS_OK); /* odd: (2^5000 + 1) * 3 / 2 */
    set_copy(&env, &u, &x);
    CHECK_STATUS(bi_binomial_next(pool, &x, 4, 1), MS_ERR_INTERNAL);
    CHECK_BIG_EQ(&x, &u);

    /* Benchmark-sized work: C(6400, 3200) repeatedly. */
    for (uint32_t rep = 0; rep < 20; rep++) {
        CHECK_STATUS(bi_binomial(pool, &c, 6400, 3200), MS_OK);
        CHECK_BIG_FX(&c, FX_C6400_3200);
    }

    bigint all[] = {c, t, u, x, sum};
    free_all(&env, all, (uint32_t)array_size(all));
    env_close(&env);
}

/* ---------------------------------------------------------------- ratios */

static void check_ratio_at(test_env *env, const bigint *num, const bigint *den, uint64_t bits,
                           const char *what, const char *file, unsigned int line) {
    double out = -1.0;
    test_check_status(bi_ratio(&env->pool, num, den, &out), MS_OK, what, file, line, "bi_ratio");
    check_bits(out, bits, what, file, line, "bi_ratio");
}

#define CHECK_RATIO(num, den, bits) \
    check_ratio_at(&env, (num), (den), (bits), "ratio " #num " / " #den, __FILE__, __LINE__)

static void test_ratios(void) {
    test_case("ratios: fixtures, endpoint guard, ESS, subnormals, overflow, ties, literals");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    enum { VN, VD, VT, VS1, VS2, VW, VCOUNT };
    bigint v[VCOUNT];
    init_all(v, VCOUNT);
    bigint *num = &v[VN], *den = &v[VD], *t = &v[VT];
    bigint one = BI_ZERO_INIT;
    bi_set_u64(&one, 1);
    double out;

    /* Frozen fixtures: 10^400 endpoints and the 80x80 pool/frontier ratios. */
    for (size_t i = 0; i < BIGINT_RATIO_COUNT; i++) {
        const BigintRatioCase *rc = &BIGINT_RATIOS[i];
        test_label = rc->name;
        set_fx(&env, num, rc->num);
        set_fx(&env, den, rc->den);
        out = -1.0;
        CHECK_STATUS(bi_probability(pool, num, den, rc->exact != 0, &out), MS_OK);
        if (rc->tolerance != 0.0) CHECK_NEAR(out, fixture_f64(rc->expected_bits), rc->tolerance);
        CHECK_BITS(out, rc->expected_bits); /* correct rounding gives the reference itself */
        uint64_t plain = rc->expected_bits; /* the same value without the guard */
        if (rc->exact && plain == BI_F64_MIN_SUBNORMAL_BITS) plain = 0;
        if (rc->exact && plain == BI_F64_BELOW_ONE_BITS) plain = F64_ONE_BITS;
        CHECK_RATIO(num, den, plain);
    }
    test_label = NULL;

    /* 0 and 1 stay reserved for integer-proven endpoints. */
    set_fx(&env, den, FX_POW10_400);
    bi_set_zero(num);
    CHECK_STATUS(bi_probability(pool, num, den, true, &out), MS_OK);
    CHECK_BITS(out, 0);
    set_copy(&env, num, den);
    CHECK_STATUS(bi_probability(pool, num, den, true, &out), MS_OK);
    CHECK_BITS(out, F64_ONE_BITS);
    bi_set_u64(num, 1);
    CHECK_STATUS(bi_probability(pool, num, den, true, &out), MS_OK);
    CHECK_BITS(out, BI_F64_MIN_SUBNORMAL_BITS);
    CHECK_STATUS(bi_probability(pool, num, den, false, &out), MS_OK);
    CHECK_BITS(out, 0);
    set_fx(&env, num, FX_POW10_400_M1);
    CHECK_STATUS(bi_probability(pool, num, den, true, &out), MS_OK);
    CHECK_BITS(out, BI_F64_BELOW_ONE_BITS);
    CHECK_STATUS(bi_probability(pool, num, den, false, &out), MS_OK);
    CHECK_BITS(out, F64_ONE_BITS);
    /* values merely near an endpoint are untouched */
    bi_set_u64(num, 1);
    CHECK_STATUS(bi_pow2(pool, den, 1000), MS_OK);
    CHECK_STATUS(bi_probability(pool, num, den, true, &out), MS_OK);
    CHECK_BITS(out, UINT64_C(23) << 52); /* 2^-1000 */
    bi_set_u64(den, 2);
    CHECK_STATUS(bi_probability(pool, num, den, true, &out), MS_OK);
    CHECK_BITS(out, UINT64_C(0x3FE0000000000000));
    /* not a probability, or no denominator: *out is not written */
    set_fx(&env, den, FX_POW10_400);
    CHECK_STATUS(bi_add(pool, num, den, &one), MS_OK);
    out = -1.0;
    CHECK_STATUS(bi_probability(pool, num, den, true, &out), MS_ERR_INTERNAL);
    CHECK_STATUS(bi_probability(pool, num, t, false, &out), MS_ERR_INTERNAL);
    CHECK_STATUS(bi_ratio(pool, num, t, &out), MS_ERR_INTERNAL);
    CHECK_SAME(out, -1.0);
    CHECK_RATIO(t, den, 0); /* 0 / 10^400 */
    CHECK_RATIO(den, &one, F64_INF_BITS); /* 10^400 overflows: +inf */

    /* Effective sample size s1^2 / s2 exceeds 1, up to the sample count:
     * 2000 equal samples of weight 2^5000, then weights 1 and 2 (x 2^4000). */
    bigint *s1 = &v[VS1], *s2 = &v[VS2], *w = &v[VW];
    CHECK_STATUS(bi_pow2(pool, w, 5000), MS_OK);
    CHECK_STATUS(bi_mul_u32(pool, s1, w, 2000), MS_OK);
    CHECK_STATUS(bi_mul(pool, t, w, w), MS_OK);
    CHECK_STATUS(bi_mul_u32(pool, s2, t, 2000), MS_OK);
    CHECK_STATUS(bi_mul(pool, t, s1, s1), MS_OK);
    CHECK_RATIO(t, s2, f64_bits(2000.0));
    bi_set_u64(w, 3);
    CHECK_STATUS(bi_shl(pool, s1, w, 4000), MS_OK);
    bi_set_u64(w, 5);
    CHECK_STATUS(bi_shl(pool, s2, w, 8000), MS_OK);
    CHECK_STATUS(bi_mul(pool, t, s1, s1), MS_OK);
    CHECK_RATIO(t, s2, f64_bits(9.0 / 5.0));

    /* Gradual underflow with exact powers of two. */
    bi_set_u64(num, 1);
    CHECK_STATUS(bi_pow2(pool, den, 1022), MS_OK);
    CHECK_RATIO(num, den, F64_MIN_NORMAL_BITS);
    CHECK_STATUS(bi_pow2(pool, den, 1074), MS_OK);
    CHECK_RATIO(num, den, 1);              /* 2^-1074 */
    bi_set_u64(num, (UINT64_C(1) << 52) - 1u);
    CHECK_RATIO(num, den, UINT64_C(0x000FFFFFFFFFFFFF)); /* largest subnormal */
    CHECK_STATUS(bi_pow2(pool, den, 1075), MS_OK);
    bi_set_u64(num, 1);
    CHECK_RATIO(num, den, 0);              /* half an ulp: tie to even 0 */
    bi_set_u64(num, 3);
    CHECK_RATIO(num, den, 2);              /* 1.5 ulps: tie to even 2 */
    bi_set_u64(num, 5);
    CHECK_RATIO(num, den, 2);              /* 2.5 ulps: tie to even 2 */
    bi_set_u64(num, (UINT64_C(1) << 53) - 1u);
    CHECK_RATIO(num, den, F64_MIN_NORMAL_BITS); /* 2^52 - 1/2 ulps rounds up to normal */
    bi_set_u64(num, (UINT64_C(1) << 53) - 3u);
    CHECK_RATIO(num, den, UINT64_C(0x000FFFFFFFFFFFFE));
    CHECK_STATUS(bi_pow2(pool, den, 1076), MS_OK);
    bi_set_u64(num, 3);
    CHECK_RATIO(num, den, 1);              /* 0.75 ulp */
    bi_set_u64(num, 1);
    CHECK_RATIO(num, den, 0);              /* 0.25 ulp */
    set_pow2_offset(&env, num, 1075, 1, 0);
    CHECK_STATUS(bi_pow2(pool, den, 2150), MS_OK);
    CHECK_RATIO(num, den, 1);              /* just above the tie: sticky bits */
    bi_set_u64(num, 1);
    set_pow2_offset(&env, den, 1075, 0, 1);
    CHECK_RATIO(num, den, 1);              /* above the tie: nonzero remainder */
    set_pow2_offset(&env, den, 1075, 1, 0);
    CHECK_RATIO(num, den, 0);              /* below the tie */
    CHECK_STATUS(bi_pow2(pool, den, 5000), MS_OK);
    CHECK_RATIO(num, den, 0);

    /* Overflow boundary: DBL_MAX = (2^53 - 1) 2^971, ties at 2^1024 - 2^970. */
    set_pow2_offset(&env, num, 53, 0, 1);
    CHECK_STATUS(bi_shl(pool, num, num, 971), MS_OK);
    CHECK_RATIO(num, &one, F64_MAX_BITS);
    CHECK_STATUS(bi_pow2(pool, t, 970), MS_OK);
    CHECK_STATUS(bi_add(pool, num, num, t), MS_OK); /* the tie */
    CHECK_RATIO(num, &one, F64_INF_BITS);
    CHECK_STATUS(bi_sub(pool, num, num, &one), MS_OK);
    CHECK_RATIO(num, &one, F64_MAX_BITS);
    CHECK_STATUS(bi_add(pool, num, num, &one), MS_OK);
    CHECK_STATUS(bi_mul_u32(pool, num, num, 3), MS_OK);
    bi_set_u64(den, 3);
    CHECK_RATIO(num, den, F64_INF_BITS);   /* the tie through long division */
    CHECK_STATUS(bi_sub(pool, num, num, &one), MS_OK);
    CHECK_RATIO(num, den, F64_MAX_BITS);   /* just below it */
    CHECK_STATUS(bi_pow2(pool, num, 1023), MS_OK);
    CHECK_RATIO(num, &one, UINT64_C(0x7FE0000000000000));
    CHECK_STATUS(bi_pow2(pool, num, 1024), MS_OK);
    CHECK_RATIO(num, &one, F64_INF_BITS);
    CHECK_STATUS(bi_pow2(pool, num, 2000), MS_OK);
    CHECK_STATUS(bi_pow2(pool, den, 977), MS_OK);
    CHECK_RATIO(num, den, UINT64_C(0x7FE0000000000000));
    CHECK_STATUS(bi_pow2(pool, num, 5000), MS_OK);
    bi_set_u64(den, 3);
    CHECK_RATIO(num, den, F64_INF_BITS);
    /* 1 + 2^-5000 rounds to 1; guarded probabilities step below 1 */
    set_pow2_offset(&env, den, 5000, 0, 1);
    CHECK_STATUS(bi_pow2(pool, num, 5000), MS_OK);
    CHECK_RATIO(num, den, F64_ONE_BITS);
    CHECK_RATIO(den, num, F64_ONE_BITS);
    CHECK_STATUS(bi_probability(pool, den, num, true, &out), MS_OK);
    CHECK_BITS(out, BI_F64_BELOW_ONE_BITS);

    /* Ties in the normal range: 2^53 + 1 -> 2^53, 2^53 + 3 -> 2^53 + 4,
     * and a sticky remainder deciding the direction. */
    set_pow2_offset(&env, num, 53, 1, 0);
    CHECK_RATIO(num, &one, UINT64_C(0x4340000000000000));
    set_pow2_offset(&env, num, 53, 3, 0);
    CHECK_RATIO(num, &one, UINT64_C(0x4340000000000002));
    set_pow2_offset(&env, num, 53, 1, 0);
    CHECK_STATUS(bi_shl(pool, num, num, 100), MS_OK);
    CHECK_STATUS(bi_pow2(pool, den, 100), MS_OK);
    CHECK_STATUS(bi_add(pool, t, num, &one), MS_OK);
    CHECK_RATIO(t, den, UINT64_C(0x4340000000000001));
    CHECK_STATUS(bi_sub(pool, t, num, &one), MS_OK);
    CHECK_RATIO(t, den, UINT64_C(0x4340000000000000));
    /* 1/3 on the fast path and through long division */
    bi_set_u64(num, 1);
    bi_set_u64(den, 3);
    CHECK_RATIO(num, den, UINT64_C(0x3FD5555555555555));
    CHECK_STATUS(bi_shl(pool, num, num, 200), MS_OK);
    CHECK_STATUS(bi_shl(pool, den, den, 200), MS_OK);
    CHECK_RATIO(num, den, UINT64_C(0x3FD5555555555555));

    /* Decimal values against the compiler's correctly rounded literals.
     * Hexadecimal float literals are avoided (MSVC's C mode lacks them), and
     * each literal's binary64 bits are pinned first (computed independently
     * with JavaScript's correctly rounded parser), so a compiler that rounds
     * decimal literals differently fails here, not in the arithmetic. */
    static const struct {
        double value;
        uint64_t bits;
    } literals[] = {
        {1e300, UINT64_C(0x7E37E43C8800759C)},          {1e-300, UINT64_C(0x01A56E1FC2F8F359)},
        {1e-320, UINT64_C(0x00000000000007E8)},         {1.7e-309, UINT64_C(0x000138F1427F48DC)},
        {3e-324, UINT64_C(0x0000000000000001)},         {2.5e-323, UINT64_C(0x0000000000000005)},
        {123456789e-330, UINT64_C(0x0000000000000019)}, {1e23, UINT64_C(0x44B52D02C7E14AF6)},
        {1e22, UINT64_C(0x4480F0CF064DD592)},           {1e308, UINT64_C(0x7FE1CCF385EBC8A0)},
        {3.1415926535897932, UINT64_C(0x400921FB54442D18)}, {2000.0, UINT64_C(0x409F400000000000)},
        {9.0 / 5.0, UINT64_C(0x3FFCCCCCCCCCCCCD)},
    };
    test_label = "compiler decimal literal";
    for (size_t i = 0; i < array_size(literals); i++) CHECK_BITS(literals[i].value, literals[i].bits);
    test_label = NULL;
    set_pow10(&env, den, 300);
    CHECK_RATIO(den, &one, f64_bits(1e300));
    CHECK_RATIO(&one, den, f64_bits(1e-300));
    set_pow10(&env, den, 320);
    CHECK_RATIO(&one, den, f64_bits(1e-320));
    set_pow10(&env, den, 310);
    bi_set_u64(num, 17);
    CHECK_RATIO(num, den, f64_bits(1.7e-309));
    set_pow10(&env, den, 324);
    bi_set_u64(num, 3);
    CHECK_RATIO(num, den, f64_bits(3e-324));
    bi_set_u64(num, 25);
    CHECK_RATIO(num, den, f64_bits(2.5e-323));
    set_pow10(&env, den, 330);
    bi_set_u64(num, 123456789);
    CHECK_RATIO(num, den, f64_bits(123456789e-330));
    set_pow10(&env, num, 23);
    CHECK_RATIO(num, &one, f64_bits(1e23));
    set_pow10(&env, num, 22);
    CHECK_RATIO(num, &one, f64_bits(1e22));
    set_pow10(&env, num, 400);
    set_pow10(&env, den, 92);
    CHECK_RATIO(num, den, f64_bits(1e308));
    set_pow10(&env, num, 307);
    CHECK_STATUS(bi_mul_u32(pool, num, num, 18), MS_OK); /* 1.8e308 > DBL_MAX */
    CHECK_RATIO(num, &one, F64_INF_BITS);
    set_pow10(&env, den, 16);
    bi_set_u64(num, 31415926535897932ull); /* 3.1415926535897932 */
    CHECK_RATIO(num, den, f64_bits(3.1415926535897932));

    /* Random scalings into the subnormal range against IEEE hardware:
     * a / 2^k, also as (a c) / (2^k c) with an odd c. */
    rt_rng rng;
    rt_rng_seed(&rng, 0xF10A7u);
    for (uint32_t iter = 0; iter < 4000; iter++) {
        uint64_t a = rand_value(&rng, 1u + rt_rng_below(&rng, 53));
        uint32_t k = rt_rng_below(&rng, 1200);
        double expected = scaled_ref((double)a, k);
        bi_set_u64(num, a);
        CHECK_STATUS(bi_pow2(pool, den, k), MS_OK);
        if (rt_rng_bit(&rng)) {
            uint32_t odd = rand_limb(&rng) | 1u;
            CHECK_STATUS(bi_mul_u32(pool, num, num, odd), MS_OK);
            CHECK_STATUS(bi_mul_u32(pool, den, den, odd), MS_OK);
        }
        CHECK_RATIO(num, den, f64_bits(expected));
    }

    free_all(&env, v, VCOUNT);
    env_close(&env);
}

/* ---------------------------------------------------------------- histogram division */

#define POLY_MAX 16u

/* Runs the term recurrence for t = 0..qlen-1; returns the first failure
 * and its term in *failed (qlen when every term succeeds). */
static ms_status poly_divide(test_env *env, bigint *d, const bigint *q, uint32_t qlen,
                             const bigint *h, uint32_t hlen, uint32_t *failed) {
    for (uint32_t t = 0; t < qlen; t++) {
        ms_status status = bi_poly_divexact_term(&env->pool, d, t, &q[t], h, hlen);
        if (status != MS_OK) {
            *failed = t;
            return status;
        }
    }
    *failed = qlen;
    return MS_OK;
}

static void test_poly_division(void) {
    test_case("histogram cavities: exact low-order polynomial division");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    bigint q[POLY_MAX], h[POLY_MAX], g[POLY_MAX], d[POLY_MAX];
    init_all(q, POLY_MAX);
    init_all(h, POLY_MAX);
    init_all(g, POLY_MAX);
    init_all(d, POLY_MAX);
    bigint one = BI_ZERO_INIT;
    bi_set_u64(&one, 1);
    uint32_t failed = 0;

    /* Frozen fixtures: H[k] = C(200, k), G[k] = C(300, k), Q = H G in full,
     * truncated above t = 6, and with Q[3] + 1. With h0 = 1 every division
     * is exact; the extra x^3 adds x^3 / H = x^3 (1 - 200 x + 20100 x^2 -
     * 1353400 x^3 ...), so d[6] = G[6] - 1353400 < 0: term 6 must fail. */
    for (size_t i = 0; i < BIGINT_POLY_DIVISION_COUNT; i++) {
        const BigintPolyDivisionCase *pc = &BIGINT_POLY_DIVISIONS[i];
        test_label = pc->name;
        CHECK(pc->dividend_len <= POLY_MAX && pc->divisor_len <= POLY_MAX);
        for (uint32_t t = 0; t < pc->dividend_len; t++) set_fx(&env, &q[t], pc->dividend[t]);
        for (uint32_t k = 0; k < pc->divisor_len; k++) set_fx(&env, &h[k], pc->divisor[k]);
        for (uint32_t t = 0; t < pc->dividend_len; t++) bi_set_u64(&d[t], 0xD1D1D1D1u);
        ms_status status = poly_divide(&env, d, q, pc->dividend_len, h, pc->divisor_len, &failed);
        if (pc->exact) {
            CHECK_STATUS(status, MS_OK);
            CHECK_EQ(pc->quotient_len, pc->dividend_len);
            for (uint32_t t = 0; t < pc->quotient_len; t++) CHECK_BIG_FX(&d[t], pc->quotient[t]);
        } else {
            CHECK_STATUS(status, MS_ERR_INTERNAL);
            CHECK_EQ(failed, 6);
            CHECK_BIG_U64(&d[failed], 0xD1D1D1D1u); /* the failed term keeps its value */
        }
    }
    test_label = NULL;

    /* The fixture polynomials rebuilt independently: binomial rows and the
     * product Q = H G through bi_addmul. */
    const BigintPolyDivisionCase *full = &BIGINT_POLY_DIVISIONS[0];
    for (uint32_t k = 0; k < 5; k++) {
        CHECK_STATUS(bi_binomial(pool, &h[k], 200, k), MS_OK);
        CHECK_BIG_FX(&h[k], full->divisor[k]);
    }
    for (uint32_t k = 0; k < 6; k++) {
        CHECK_STATUS(bi_binomial(pool, &g[k], 300, k), MS_OK);
        CHECK_BIG_FX(&g[k], full->quotient[k]);
    }
    for (uint32_t t = 0; t < full->dividend_len; t++) {
        bi_set_zero(&q[t]);
        for (uint32_t k = 0; k < 5; k++) {
            if (t >= k && t - k < 6) CHECK_STATUS(bi_addmul(pool, &q[t], &h[k], &g[t - k]), MS_OK);
        }
        CHECK_BIG_FX(&q[t], full->dividend[t]);
    }

    /* A multi-limb leading coefficient: H = C(6400, 3200) + C(6400, 3199) x,
     * G = 1 + 2 x + 3 x^2, Q = H G with two zero terms appended. */
    CHECK_STATUS(bi_binomial(pool, &h[0], 6400, 3200), MS_OK);
    CHECK_STATUS(bi_binomial(pool, &h[1], 6400, 3199), MS_OK);
    for (uint32_t t = 0; t < 6; t++) {
        bi_set_zero(&q[t]);
        for (uint32_t k = 0; k < 2; k++) {
            if (t >= k && t - k <= 2) CHECK_STATUS(bi_addmul_u32(pool, &q[t], &h[k], t - k + 1u), MS_OK);
        }
    }
    CHECK_STATUS(poly_divide(&env, d, q, 6, h, 2, &failed), MS_OK);
    for (uint32_t t = 0; t < 6; t++) CHECK_BIG_U64(&d[t], t <= 2 ? t + 1u : 0u);
    /* Q[1] + 1: the second term is 2 h0 + 1, nonnegative but not a multiple of h0. */
    CHECK_STATUS(bi_add(pool, &q[1], &q[1], &one), MS_OK);
    for (uint32_t t = 0; t < 6; t++) bi_set_u64(&d[t], 0xD1D1D1D1u);
    CHECK_STATUS(poly_divide(&env, d, q, 6, h, 2, &failed), MS_ERR_INTERNAL);
    CHECK_EQ(failed, 1);
    CHECK_BIG_U64(&d[0], 1);
    CHECK_BIG_U64(&d[1], 0xD1D1D1D1u);
    CHECK_STATUS(bi_sub(pool, &q[1], &q[1], &one), MS_OK);
    /* Q[2] = 0: a negative partial difference. */
    set_copy(&env, &g[0], &q[2]);
    bi_set_zero(&q[2]);
    CHECK_STATUS(poly_divide(&env, d, q, 6, h, 2, &failed), MS_ERR_INTERNAL);
    CHECK_EQ(failed, 2);
    CHECK_BIG_U64(&d[2], 0xD1D1D1D1u);
    set_copy(&env, &q[2], &g[0]);
    /* q_t may be d[t] itself: recovery in place. */
    for (uint32_t t = 0; t < 6; t++) set_copy(&env, &d[t], &q[t]);
    for (uint32_t t = 0; t < 6; t++) CHECK_STATUS(bi_poly_divexact_term(pool, d, t, &d[t], h, 2), MS_OK);
    for (uint32_t t = 0; t < 6; t++) CHECK_BIG_U64(&d[t], t <= 2 ? t + 1u : 0u);

    /* A constant divisor, and invalid divisors. */
    bi_set_u64(&h[0], 7);
    bi_set_u64(&q[0], 14);
    bi_set_u64(&q[1], 21);
    bi_set_zero(&q[2]);
    CHECK_STATUS(poly_divide(&env, d, q, 3, h, 1, &failed), MS_OK);
    CHECK_BIG_U64(&d[0], 2);
    CHECK_BIG_U64(&d[1], 3);
    CHECK(bi_is_zero(&d[2]));
    bigint zero = BI_ZERO_INIT;
    CHECK_STATUS(bi_poly_divexact_term(pool, d, 0, &q[0], h, 0), MS_ERR_INTERNAL);
    CHECK_STATUS(bi_poly_divexact_term(pool, d, 0, &q[0], &zero, 1), MS_ERR_INTERNAL);
    CHECK_BIG_U64(&d[0], 2);

    free_all(&env, q, POLY_MAX);
    free_all(&env, h, POLY_MAX);
    free_all(&env, g, POLY_MAX);
    free_all(&env, d, POLY_MAX);
    env_close(&env);
}

/* ---------------------------------------------------------------- failures */

static uint64_t blocks_held(const bigint *xs, uint32_t count) {
    uint64_t held = 0;
    for (uint32_t i = 0; i < count; i++) held += xs[i].cap != 0 ? 1u : 0u;
    return held;
}

enum {
    SLOT_P400,   /* 10^400 */
    SLOT_THIRD,  /* (10^400 - 1) / 3 */
    SLOT_C6400,  /* C(6400, 3200) */
    SLOT_SMALL,  /* 12345, inline */
    SLOT_P400M1, /* 10^400 - 1 */
    SLOT_OUT,    /* 7, inline: a typical output */
    SLOT_ONE,
    SLOT_BINOM,  /* C(100, 17) < 2^64 < C(100, 18), inline */
    SLOT_OUT2,   /* 9, inline: a second output */
    SLOT_P200,   /* 10^200 */
    SLOT_COUNT
};

typedef enum sweep_op {
    OP_ADD, OP_ADD_SELF, OP_SUB, OP_MUL, OP_MUL_SELF, OP_MUL_U32, OP_ADDMUL, OP_ADDMUL_SELF,
    OP_ADDMUL_U32, OP_ADDMUL_U32_SELF, OP_SUBMUL_SELF, OP_SHL, OP_SHL_SELF, OP_POW2, OP_COPY,
    OP_SET_LIMBS, OP_RESERVE, OP_DIVMOD_U32, OP_DIVEXACT_U32, OP_DIVMOD, OP_DIVMOD_SELF,
    OP_DIVEXACT, OP_DIVEXACT_SELF, OP_BINOMIAL, OP_BINOMIAL_NEXT, OP_RATIO, OP_PROBABILITY,
    OP_COUNT
} sweep_op;

static const char *const SWEEP_NAMES[OP_COUNT] = {
    "add", "add self", "sub", "mul", "mul self", "mul_u32", "addmul", "addmul self",
    "addmul_u32", "addmul_u32 self", "submul self", "shl", "shl self", "pow2", "copy",
    "set_limbs", "reserve", "divmod_u32", "divexact_u32", "divmod", "divmod self",
    "divexact", "divexact self", "binomial", "binomial_next", "ratio", "probability"};

/* Every block of the setup stays owned (nothing is cached), so each block
 * an operation needs is a fresh rt_mem request that injection can fail. */
static void sweep_setup(test_env *env, bigint *v) {
    init_all(v, SLOT_COUNT);
    set_fx(env, &v[SLOT_P400], FX_POW10_400);
    set_fx(env, &v[SLOT_THIRD], FX_THIRD);
    set_fx(env, &v[SLOT_C6400], FX_C6400_3200);
    bi_set_u64(&v[SLOT_SMALL], 12345);
    set_fx(env, &v[SLOT_P400M1], FX_POW10_400_M1);
    bi_set_u64(&v[SLOT_OUT], 7);
    bi_set_u64(&v[SLOT_ONE], 1);
    CHECK_STATUS(bi_binomial(&env->pool, &v[SLOT_BINOM], 100, 17), MS_OK);
    CHECK(v[SLOT_BINOM].cap == 0);
    bi_set_u64(&v[SLOT_OUT2], 9);
    CHECK_STATUS(bi_reserve(&env->pool, &v[SLOT_P200], 32), MS_OK);
    set_pow10(env, &v[SLOT_P200], 200);
    CHECK(env->pool.cached == 0);
}

static ms_status sweep_run(test_env *env, bigint *v, sweep_op op, double *out) {
    bi_pool *p = &env->pool;
    uint32_t rem = 0;
    ms_status status;
    switch (op) {
    case OP_ADD: return bi_add(p, &v[SLOT_OUT], &v[SLOT_P400], &v[SLOT_C6400]);
    case OP_ADD_SELF: return bi_add(p, &v[SLOT_SMALL], &v[SLOT_SMALL], &v[SLOT_P400]);
    case OP_SUB: return bi_sub(p, &v[SLOT_OUT], &v[SLOT_C6400], &v[SLOT_P400]);
    case OP_MUL: return bi_mul(p, &v[SLOT_OUT], &v[SLOT_P400], &v[SLOT_C6400]);
    case OP_MUL_SELF: return bi_mul(p, &v[SLOT_P400], &v[SLOT_P400], &v[SLOT_P400]);
    case OP_MUL_U32: return bi_mul_u32(p, &v[SLOT_OUT], &v[SLOT_P400], UINT32_MAX);
    case OP_ADDMUL: return bi_addmul(p, &v[SLOT_OUT], &v[SLOT_P400], &v[SLOT_C6400]);
    case OP_ADDMUL_SELF: return bi_addmul(p, &v[SLOT_P400], &v[SLOT_P400], &v[SLOT_THIRD]);
    case OP_ADDMUL_U32: return bi_addmul_u32(p, &v[SLOT_OUT], &v[SLOT_P400], 3);
    case OP_ADDMUL_U32_SELF: return bi_addmul_u32(p, &v[SLOT_P400], &v[SLOT_P400], 3);
    case OP_SUBMUL_SELF: return bi_submul(p, &v[SLOT_P400], &v[SLOT_P400], &v[SLOT_ONE]);
    case OP_SHL: return bi_shl(p, &v[SLOT_OUT], &v[SLOT_P400], 1000);
    case OP_SHL_SELF: return bi_shl(p, &v[SLOT_P400], &v[SLOT_P400], 1000);
    case OP_POW2: return bi_pow2(p, &v[SLOT_OUT], 5000);
    case OP_COPY: return bi_copy(p, &v[SLOT_OUT], &v[SLOT_C6400]);
    case OP_SET_LIMBS:
        return bi_set_limbs(p, &v[SLOT_OUT], bigint_c_6396_1999_limbs,
                            (uint32_t)array_size(bigint_c_6396_1999_limbs));
    case OP_RESERVE: return bi_reserve(p, &v[SLOT_SMALL], 100);
    case OP_DIVMOD_U32:
        status = bi_divmod_u32(p, &v[SLOT_OUT], &v[SLOT_P400], 3, &rem);
        if (status == MS_OK) bi_set_u64(&v[SLOT_OUT2], rem);
        return status;
    case OP_DIVEXACT_U32: return bi_divexact_u32(p, &v[SLOT_OUT], &v[SLOT_P400], 1220703125u); /* 5^13 */
    case OP_DIVMOD: return bi_divmod(p, &v[SLOT_OUT], &v[SLOT_OUT2], &v[SLOT_C6400], &v[SLOT_THIRD]);
    case OP_DIVMOD_SELF:
        return bi_divmod(p, &v[SLOT_C6400], &v[SLOT_THIRD], &v[SLOT_C6400], &v[SLOT_THIRD]);
    case OP_DIVEXACT: return bi_divexact(p, &v[SLOT_OUT], &v[SLOT_P400], &v[SLOT_P200]);
    case OP_DIVEXACT_SELF: return bi_divexact(p, &v[SLOT_P400M1], &v[SLOT_P400M1], &v[SLOT_THIRD]);
    case OP_BINOMIAL: return bi_binomial(p, &v[SLOT_OUT], 6400, 3200);
    case OP_BINOMIAL_NEXT: return bi_binomial_next(p, &v[SLOT_BINOM], 100, 17);
    case OP_RATIO: return bi_ratio(p, &v[SLOT_THIRD], &v[SLOT_P400], out);
    case OP_PROBABILITY: return bi_probability(p, &v[SLOT_THIRD], &v[SLOT_P400], true, out);
    case OP_COUNT: break;
    }
    return MS_ERR_INTERNAL;
}

static void test_failure_injection(void) {
    test_case("failure injection: exhaustion at every allocation leaves operands unchanged");
    for (uint32_t op = 0; op < OP_COUNT; op++) {
        test_label = SWEEP_NAMES[op];
        test_env ref_env;
        env_open(&ref_env);
        bigint ref[SLOT_COUNT];
        sweep_setup(&ref_env, ref);
        double ref_out = -1.0;
        CHECK_STATUS(sweep_run(&ref_env, ref, (sweep_op)op, &ref_out), MS_OK);
        uint32_t failures = 0;
        for (uint64_t after = 0;; after++) {
            test_env env, snap_env;
            env_open(&env);
            env_open(&snap_env);
            bigint v[SLOT_COUNT], snap[SLOT_COUNT];
            sweep_setup(&env, v);
            init_all(snap, SLOT_COUNT);
            for (uint32_t i = 0; i < SLOT_COUNT; i++) set_copy(&snap_env, &snap[i], &v[i]);
            CHECK_EQ(env.pool.outstanding, blocks_held(v, SLOT_COUNT));
            uint32_t caps[SLOT_COUNT];
            for (uint32_t i = 0; i < SLOT_COUNT; i++) caps[i] = v[i].cap;
            double out = -1.0;
            rt_mem_set_failure(&env.mem, after, RT_MEM_FOREVER);
            ms_status status = sweep_run(&env, v, (sweep_op)op, &out);
            rt_mem_set_failure(&env.mem, 0, 0);
            /* Every block belongs to an operand: no scratch leaked. (A failed
             * bi_divmod may leave q's capacity grown, its value kept.) */
            CHECK_EQ(env.pool.outstanding, blocks_held(v, SLOT_COUNT));
            if (status == MS_OK) {
                for (uint32_t i = 0; i < SLOT_COUNT; i++) CHECK_BIG_EQ(&v[i], &ref[i]);
                CHECK_BITS(out, f64_bits(ref_out));
            } else {
                CHECK_STATUS(status, MS_ERR_RESOURCE_EXHAUSTED);
                for (uint32_t i = 0; i < SLOT_COUNT; i++) {
                    CHECK_BIG_EQ(&v[i], &snap[i]);
                    CHECK(v[i].cap == caps[i] || (op == OP_DIVMOD && i == SLOT_OUT));
                }
                CHECK_BITS(out, f64_bits(-1.0));
                failures++;
            }
            free_all(&env, v, SLOT_COUNT);
            env_close(&env);
            free_all(&snap_env, snap, SLOT_COUNT);
            env_close(&snap_env);
            if (status == MS_OK) break;
            CHECK(after < 8);
        }
        CHECK(failures > 0); /* every operation above needs memory */
        free_all(&ref_env, ref, SLOT_COUNT);
        env_close(&ref_env);
    }

    /* The term of a histogram division keeps its value on exhaustion. */
    test_label = "poly_divexact_term";
    uint32_t failures = 0;
    for (uint64_t after = 0;; after++) {
        test_env env;
        env_open(&env);
        bigint h[2], q[2], d[2];
        init_all(h, 2);
        init_all(q, 2);
        init_all(d, 2);
        CHECK_STATUS(bi_binomial(&env.pool, &h[0], 6400, 3200), MS_OK);
        CHECK_STATUS(bi_binomial(&env.pool, &h[1], 6400, 3199), MS_OK);
        set_copy(&env, &q[1], &h[1]);
        CHECK_STATUS(bi_addmul_u32(&env.pool, &q[1], &h[0], 2), MS_OK); /* (h0 + h1 x)(1 + 2x) */
        bi_set_u64(&d[0], 1);
        bi_set_u64(&d[1], 5);
        CHECK(env.pool.cached == 0);
        rt_mem_set_failure(&env.mem, after, RT_MEM_FOREVER);
        ms_status status = bi_poly_divexact_term(&env.pool, d, 1, &q[1], h, 2);
        rt_mem_set_failure(&env.mem, 0, 0);
        CHECK_BIG_U64(&d[0], 1);
        if (status == MS_OK) {
            CHECK_BIG_U64(&d[1], 2);
        } else {
            CHECK_STATUS(status, MS_ERR_RESOURCE_EXHAUSTED);
            CHECK_BIG_U64(&d[1], 5);
            failures++;
        }
        free_all(&env, h, 2);
        free_all(&env, q, 2);
        free_all(&env, d, 2);
        env_close(&env);
        if (status == MS_OK) break;
        CHECK(after < 8);
    }
    CHECK(failures >= 2); /* the accumulator and the division scratch */
    test_label = NULL;

    /* A byte budget refuses the same way. */
    test_env env;
    env_open(&env);
    bigint big = BI_ZERO_INIT;
    rt_mem_set_budget(&env.mem, env.mem.live);
    CHECK_STATUS(bi_pow2(&env.pool, &big, 1u << 22), MS_ERR_RESOURCE_EXHAUSTED);
    CHECK(bi_is_zero(&big) && big.cap == 0);
    rt_mem_set_budget(&env.mem, RT_MEM_UNLIMITED);
    CHECK_STATUS(bi_pow2(&env.pool, &big, 1u << 22), MS_OK);
    CHECK_EQ(bi_bit_length(&big), (1u << 22) + 1u);
    bi_free(&env.pool, &big);
    env_close(&env);
}

/* ---------------------------------------------------------------- reuse */

static void test_reuse(void) {
    test_case("reuse: repeated solver-shaped rounds stay within the first round's blocks");
    test_env env;
    env_open(&env);
    bi_pool *pool = &env.pool;
    bigint work[10];
    init_all(work, 10);
    uint64_t carved = 0;
    size_t peak = 0;
    for (uint32_t round = 0; round < 6; round++) {
        for (uint32_t i = 0; i < 4; i++) {
            CHECK_STATUS(bi_binomial(pool, &work[i], 6000u + 100u * i, 1000u + 500u * i), MS_OK);
        }
        CHECK_STATUS(bi_mul(pool, &work[4], &work[0], &work[1]), MS_OK);
        CHECK_STATUS(bi_addmul(pool, &work[4], &work[2], &work[3]), MS_OK);
        CHECK_STATUS(bi_divmod(pool, &work[5], &work[6], &work[4], &work[2]), MS_OK);
        set_copy(&env, &work[7], &work[6]); /* q d + r == a */
        CHECK_STATUS(bi_addmul(pool, &work[7], &work[5], &work[2]), MS_OK);
        CHECK_BIG_EQ(&work[7], &work[4]);
        CHECK_STATUS(bi_shl(pool, &work[8], &work[5], 3000), MS_OK);
        double out = -1.0;
        CHECK_STATUS(bi_probability(pool, &work[6], &work[2], true, &out), MS_OK);
        CHECK(out > 0.0 && out < 1.0);
        /* acc = 3 a b loses a b three times without allocating */
        CHECK_STATUS(bi_mul(pool, &work[9], &work[0], &work[1]), MS_OK);
        CHECK_STATUS(bi_mul_u32(pool, &work[9], &work[9], 3), MS_OK);
        uint64_t requests = env.mem.requests;
        uint64_t taken = pool->carved + pool->reused;
        for (uint32_t i = 0; i < 3; i++) CHECK_STATUS(bi_submul(pool, &work[9], &work[0], &work[1]), MS_OK);
        CHECK(bi_is_zero(&work[9]));
        CHECK(env.mem.requests == requests && pool->carved + pool->reused == taken);
        free_all(&env, work, 10);
        CHECK_EQ(pool->outstanding, 0);
        if (round == 0) {
            carved = pool->carved;
            peak = env.mem.peak;
        } else {
            CHECK_EQ(pool->carved, carved);
            CHECK_EQ(env.mem.peak, peak);
        }
    }
    env_close(&env);
}

void test_bigint(void) {
    test_representation();
    test_pool_lifetime();
    test_growable_array();
    test_pool_release();
    test_word_boundaries();
    test_random_u64();
    test_identities();
    test_aliasing();
    test_division_known();
    test_binomials();
    test_ratios();
    test_poly_division();
    test_failure_injection();
    test_reuse();
}
