/* Tests for c/runtime.c: checked sizes, rt_mem, RNG, hashing, clock,
 * grid neighbors and the engine.h ABI helpers. */

#include "test_support.h"

/* 8-byte aligned scratch storage for ABI buffers. */
#define TEST_WORDS(bytes) (((bytes) + 7u) / 8u)

/* ---------------------------------------------------------------- arithmetic */

static void test_checked_arithmetic(void) {
    test_case("checked arithmetic and layout sizes");
    size_t s = 7;
    CHECK(rt_add_size(SIZE_MAX - 1, 1, &s) && s == SIZE_MAX);
    s = 7;
    CHECK(!rt_add_size(SIZE_MAX, 1, &s) && s == 7);
    CHECK(rt_mul_size(0, SIZE_MAX, &s) && s == 0);
    CHECK(rt_mul_size(SIZE_MAX / 3, 3, &s) && s == SIZE_MAX / 3 * 3);
    s = 7;
    CHECK(!rt_mul_size(SIZE_MAX / 2 + 1, 2, &s) && s == 7);
    uint64_t u = 5;
    CHECK(rt_mul_u64(0xFFFFFFFFull, 0xFFFFFFFFull, &u) && u == 0xFFFFFFFE00000001ull);
    u = 5;
    CHECK(!rt_mul_u64(1ull << 32, 1ull << 32, &u) && u == 5);
    CHECK(!rt_add_u64(UINT64_MAX, 1, &u) && u == 5);
    CHECK(rt_add_u64(UINT64_MAX - 1, 1, &u) && u == UINT64_MAX);

    CHECK_EQ(ms_cell_count(0, 5), 0);
    CHECK_EQ(ms_cell_count(5, 0), 0);
    CHECK_EQ(ms_cell_count(80, 80), 6400);
    CHECK_EQ(ms_cell_count(81, 80), 0);
    CHECK_EQ(ms_cell_count(3001, 1), 3001);
    CHECK_EQ(ms_cell_count(1, 6400), 6400);
    CHECK_EQ(ms_cell_count(6401, 1), 0);
    CHECK_EQ(ms_cell_count(UINT32_MAX, UINT32_MAX), 0);
    CHECK_EQ(ms_cell_count(65536, 65536), 0);

    CHECK_EQ(ms_view_size(5, 5), 80);         /* 48 + 25 -> 80 */
    CHECK_EQ(ms_obs_size(3001, 1), 3040);     /* 32 + 3001 -> 3040 */
    CHECK_EQ(ms_result_size(80, 80), 57720);  /* 120 + 9 * 6400 */
    CHECK_EQ(ms_result_size(3, 1), 152);      /* 120 + 27 -> 152 */
    CHECK_EQ(ms_view_size(0, 5), 0);
    CHECK_EQ(ms_obs_size(6401, 1), 0);
    CHECK_EQ(ms_result_size(1, 0), 0);
}

/* ---------------------------------------------------------------- rt_mem */

static bool aligned16(const void *p) {
    return ((uintptr_t)p & 15u) == 0;
}

static void test_mem_basics(void) {
    test_case("rt_mem allocation, alignment and accounting");
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    CHECK_EQ(mem.live, 0);
    CHECK_EQ(mem.peak, 0);
    CHECK_EQ(mem.live_blocks, 0);
    CHECK_STATUS(mem.error, MS_OK);

    CHECK_EQ(rt_mem_charge(100), 4096);
    CHECK_EQ(rt_mem_charge(5000), 8192);
    CHECK_EQ(rt_mem_charge((size_t)1 << 20), (size_t)2 << 20);
    CHECK_EQ(rt_mem_charge(RT_MEM_MAX_REQUEST + 1), 0);

    uint8_t *a = (uint8_t *)rt_alloc(&mem, 100);
    CHECK(a != NULL && aligned16(a));
    CHECK_EQ(mem.live, 4096);
    CHECK_EQ(mem.live_blocks, 1);
    CHECK_EQ(mem.requests, 1);
    for (int i = 0; i < 100; i++) a[i] = (uint8_t)i;

    uint8_t *b = (uint8_t *)rt_alloc(&mem, 5000);
    CHECK(b != NULL && aligned16(b));
    CHECK_EQ(mem.live, 4096 + 8192);
    base_memset(b, 0xAB, 5000);

    void *z = rt_alloc(&mem, 0);
    CHECK(z != NULL && aligned16(z) && z != a && z != b);
    CHECK_EQ(mem.live, 4096 + 8192 + 4096);
    CHECK_EQ(mem.peak, mem.live);

    rt_free(&mem, b);
    CHECK_EQ(mem.live, 8192);
    CHECK_EQ(mem.peak, 4096 + 8192 + 4096);
    rt_mem_reset_peak(&mem);
    CHECK_EQ(mem.peak, 8192);
    for (int i = 0; i < 100; i++) CHECK_EQ(a[i], i);

    uint32_t *zeroed = (uint32_t *)rt_calloc(&mem, 1000, sizeof(uint32_t));
    CHECK(zeroed != NULL && aligned16(zeroed));
    for (int i = 0; i < 1000; i++) CHECK_EQ(zeroed[i], 0);
    CHECK_EQ(mem.live, 8192 + rt_mem_charge(4000));

    size_t big = (size_t)1 << 20;
    uint8_t *large = (uint8_t *)rt_alloc(&mem, big);
    CHECK(large != NULL && aligned16(large));
    large[0] = 1;
    large[big - 1] = 2;
    CHECK_EQ(large[0] + large[big - 1], 3);

    size_t live = mem.live;
    size_t blocks = mem.live_blocks;
    rt_free(&mem, NULL);
    CHECK(rt_alloc(&mem, RT_MEM_MAX_REQUEST + 1) == NULL);
    CHECK_STATUS(mem.error, MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_EQ(mem.failures, 1);
    CHECK(rt_calloc(&mem, SIZE_MAX / 2 + 1, 2) == NULL);
    CHECK_EQ(mem.failures, 2);
    CHECK_EQ(mem.live, live);
    CHECK_EQ(mem.live_blocks, blocks);

    rt_mem_reset(&mem);
    CHECK_EQ(mem.live, 0);
    CHECK_EQ(mem.live_blocks, 0);
    CHECK_STATUS(mem.error, MS_OK);
    CHECK(rt_alloc(&mem, 10) != NULL);
    rt_mem_dispose(&mem);
    CHECK_EQ(mem.live, 0);
}

static void test_mem_budget(void) {
    test_case("rt_mem byte budget is enforced before allocating");
    rt_mem mem;
    rt_mem_init(&mem, 3 * 4096);
    void *a = rt_alloc(&mem, 100);
    void *b = rt_alloc(&mem, 100);
    void *c = rt_alloc(&mem, 100);
    CHECK(a && b && c);
    CHECK_EQ(mem.live, 3 * 4096);
    CHECK(rt_alloc(&mem, 1) == NULL);
    CHECK_STATUS(mem.error, MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_EQ(mem.live, 3 * 4096);
    CHECK_EQ(mem.live_blocks, 3);
    CHECK(rt_bump(&mem, 1) == NULL);
    rt_free(&mem, b);
    void *d = rt_alloc(&mem, 1);
    CHECK(d != NULL);
    CHECK(rt_alloc(&mem, 5000) == NULL);

    rt_mem_set_budget(&mem, 4096); /* below live: everything fails */
    CHECK(rt_alloc(&mem, 1) == NULL);
    rt_free(&mem, a);
    rt_free(&mem, c);
    rt_free(&mem, d);
    CHECK_EQ(mem.live, 0);
    void *e = rt_alloc(&mem, 100);
    CHECK(e != NULL);
    CHECK_EQ(mem.live, 4096);
    CHECK_EQ(mem.peak, 3 * 4096);

    rt_mem_set_budget(&mem, RT_MEM_UNLIMITED);
    size_t previous = rt_mem_limit(&mem, 8192);
    CHECK_EQ(previous, RT_MEM_UNLIMITED);
    CHECK_EQ(mem.budget, 4096 + 8192);
    void *f = rt_alloc(&mem, 5000);
    CHECK(f != NULL);
    CHECK(rt_alloc(&mem, 1) == NULL);
    rt_mem_set_budget(&mem, previous);
    CHECK(rt_alloc(&mem, 1) != NULL);
    previous = rt_mem_limit(&mem, SIZE_MAX); /* live + extra overflows: no cap */
    CHECK_EQ(mem.budget, RT_MEM_UNLIMITED);
    CHECK_EQ(previous, RT_MEM_UNLIMITED);
    rt_mem_set_budget(&mem, 100);
    CHECK_EQ(rt_mem_limit(&mem, 1 << 20), 100); /* never raises the budget */
    CHECK_EQ(mem.budget, 100);
    rt_mem_dispose(&mem);
}

static void test_mem_realloc(void) {
    test_case("rt_realloc keeps contents and ownership on failure");
    rt_mem mem;
    rt_mem other;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    rt_mem_init(&other, RT_MEM_UNLIMITED);
    uint8_t *p = (uint8_t *)rt_realloc(&mem, NULL, 10);
    CHECK(p != NULL);
    for (int i = 0; i < 10; i++) p[i] = (uint8_t)(i + 1);
    uint64_t requests = mem.requests;
    uint8_t *q = (uint8_t *)rt_realloc(&mem, p, 100); /* fits the 4 KiB block */
    CHECK(q == p);
    CHECK_EQ(mem.requests, requests);
    uint8_t *r = (uint8_t *)rt_realloc(&mem, q, 10000);
    CHECK(r != NULL && r != q && aligned16(r));
    for (int i = 0; i < 10; i++) CHECK_EQ(r[i], i + 1);
    CHECK_EQ(mem.live, rt_mem_charge(10000));
    CHECK_EQ(mem.live_blocks, 1);
    base_memset(r + 10, 0x5A, 9990);

    rt_mem_set_failure(&mem, 0, 1);
    CHECK(rt_realloc(&mem, r, 100000) == NULL);
    CHECK_STATUS(mem.error, MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_EQ(mem.live, rt_mem_charge(10000));
    CHECK_EQ(r[0] + r[9999], 1 + 0x5A);

    rt_mem_set_budget(&mem, mem.live + rt_mem_charge(100000) - 1); /* both must fit */
    CHECK(rt_realloc(&mem, r, 100000) == NULL);
    rt_mem_set_budget(&mem, mem.live + rt_mem_charge(100000));
    uint8_t *s = (uint8_t *)rt_realloc(&mem, r, 100000);
    CHECK(s != NULL);
    CHECK_EQ(mem.peak, rt_mem_charge(10000) + rt_mem_charge(100000));
    CHECK_EQ(mem.live, rt_mem_charge(100000));
    CHECK_EQ(s[0] + s[9999], 1 + 0x5A);
    CHECK(rt_realloc(&mem, s, 50) == s); /* shrinking stays in place */

    void *foreign = rt_alloc(&other, 10);
    rt_mem_reset(&mem); /* clears the sticky error */
    CHECK_EQ(mem.misuses, 0);
    CHECK(rt_realloc(&mem, foreign, 20) == NULL);
    CHECK_STATUS(mem.error, MS_ERR_INTERNAL);
    CHECK_EQ(mem.misuses, 1);
    CHECK_EQ(other.live, 4096);
    CHECK_EQ(other.misuses, 0);
    rt_mem_dispose(&mem);
    rt_mem_dispose(&other);
}

static void test_mem_grow(void) {
    test_case("rt_grow growable arrays");
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    size_t capacity = 12345; /* ignored while the array is NULL */
    uint32_t *list = (uint32_t *)rt_grow(&mem, NULL, &capacity, 1, sizeof(uint32_t));
    CHECK(list != NULL && aligned16(list));
    CHECK(capacity >= 8 && capacity * sizeof(uint32_t) <= 4096);
    size_t count = 0;
    for (uint32_t i = 0; i < 5000; i++) {
        uint32_t *grown = (uint32_t *)rt_grow(&mem, list, &capacity, count + 1, sizeof(uint32_t));
        CHECK(grown != NULL);
        list = grown;
        list[count++] = i * 7u;
    }
    CHECK(capacity >= 5000);
    for (uint32_t i = 0; i < 5000; i++) CHECK_EQ(list[i], i * 7u);
    CHECK_EQ(mem.live_blocks, 1);
    uint64_t requests = mem.requests;
    CHECK(rt_grow(&mem, list, &capacity, capacity, sizeof(uint32_t)) == list); /* fits */
    CHECK_EQ(mem.requests, requests);
    size_t reported = 0; /* an understated capacity only costs a lookup */
    CHECK(rt_grow(&mem, list, &reported, 1, sizeof(uint32_t)) == list);
    CHECK_EQ(reported, capacity);
    CHECK_EQ(mem.requests, requests);
    CHECK(reported * sizeof(uint32_t) <= rt_mem_capacity(&mem, list));

    size_t before = capacity;
    rt_mem_set_failure(&mem, 0, 1);
    CHECK(rt_grow(&mem, list, &capacity, capacity + 1, sizeof(uint32_t)) == NULL);
    CHECK_EQ(capacity, before);
    CHECK_EQ(list[4999], 4999u * 7u);
    CHECK(rt_grow(&mem, list, &capacity, SIZE_MAX / 2, sizeof(uint32_t)) == NULL);
    CHECK_EQ(capacity, before);
    CHECK_EQ(mem.misuses, 0);
    CHECK(rt_grow(&mem, list, &capacity, 1, 0) == NULL); /* zero-sized elements: misuse */
    CHECK_EQ(mem.misuses, 1);
    rt_free(&mem, list);
    CHECK_EQ(mem.live, 0);
    rt_mem_dispose(&mem);
}

static void test_mem_bump(void) {
    test_case("rt_bump chunks, marks and rewinds");
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    uint8_t *a = (uint8_t *)rt_bump(&mem, 1);
    CHECK(a != NULL && aligned16(a));
    CHECK_EQ(mem.live, RT_MEM_CHUNK_BLOCK); /* chunks fill a buddy block exactly */
    CHECK_EQ(mem.live_blocks, 1);
    uint8_t *b = (uint8_t *)rt_bump(&mem, 1);
    uint8_t *c = (uint8_t *)rt_bump(&mem, 0);
    CHECK(b == a + 16 && c == b + 16);
    for (int i = 0; i < 1000; i++) {
        uint8_t *p = (uint8_t *)rt_bump(&mem, 24);
        CHECK(p != NULL && aligned16(p));
        base_memset(p, i & 0xFF, 24);
    }
    CHECK_EQ(mem.live_blocks, 1);

    rt_mark mark = rt_mem_mark(&mem);
    uint8_t *big = (uint8_t *)rt_bump(&mem, 100000); /* dedicated larger chunk */
    CHECK(big != NULL && aligned16(big));
    CHECK_EQ(mem.live_blocks, 2);
    CHECK_EQ(mem.live, RT_MEM_CHUNK_BLOCK + rt_mem_charge(100000));
    uint8_t *after = (uint8_t *)rt_bump(&mem, 16); /* from the big chunk's tail */
    CHECK(after == big + 100000);
    CHECK_EQ(mem.live_blocks, 2);
    rt_mem_rewind(&mem, mark);
    CHECK_STATUS(mem.error, MS_OK);
    CHECK_EQ(mem.live_blocks, 1);
    CHECK_EQ(mem.live, RT_MEM_CHUNK_BLOCK);
    CHECK(rt_bump(&mem, 16) == mark.bump);

    uint64_t *zero = (uint64_t *)rt_bump_array(&mem, 10, sizeof(uint64_t));
    CHECK(zero != NULL);
    for (int i = 0; i < 10; i++) CHECK_EQ(zero[i], 0);
    CHECK(rt_bump_array(&mem, SIZE_MAX, 2) == NULL);
    CHECK(rt_bump(&mem, RT_MEM_MAX_REQUEST + 1) == NULL);
    CHECK_STATUS(mem.error, MS_ERR_RESOURCE_EXHAUSTED);
    rt_mem_reset(&mem);
    CHECK_EQ(mem.live, 0);

    rt_mark empty = rt_mem_mark(&mem);
    CHECK(rt_bump(&mem, 10) && rt_bump(&mem, 200000));
    CHECK_EQ(mem.live_blocks, 2);
    rt_mem_rewind(&mem, empty);
    CHECK_EQ(mem.live_blocks, 0);
    CHECK_EQ(mem.live, 0);
    CHECK(mem.bump == NULL);

    /* Stale marks are refused without changing anything. */
    CHECK(rt_bump(&mem, 10) != NULL);
    rt_mark first = rt_mem_mark(&mem);
    CHECK(rt_bump(&mem, 200000) != NULL);
    rt_mark second = rt_mem_mark(&mem);
    rt_mem_rewind(&mem, first);
    size_t live = mem.live;
    CHECK_EQ(mem.misuses, 0);
    rt_mem_rewind(&mem, second); /* its chunk is gone */
    CHECK_STATUS(mem.error, MS_ERR_INTERNAL);
    CHECK_EQ(mem.misuses, 1);
    CHECK_EQ(mem.live, live);
    rt_mem_reset(&mem);
    CHECK(rt_bump(&mem, 10) != NULL);
    first = rt_mem_mark(&mem);
    CHECK(rt_bump(&mem, 32) != NULL);
    second = rt_mem_mark(&mem);
    rt_mem_rewind(&mem, first);
    rt_mem_rewind(&mem, second); /* forward in the same chunk */
    CHECK_STATUS(mem.error, MS_ERR_INTERNAL);
    CHECK_EQ(mem.misuses, 2);
    CHECK(rt_bump(&mem, 16) == first.bump);
    rt_mem_dispose(&mem);
}

static void test_mem_failure_injection(void) {
    test_case("rt_mem failure injection");
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    rt_mem_set_failure(&mem, 2, 1);
    void *a = rt_alloc(&mem, 10);
    void *b = rt_bump(&mem, 10);
    void *c = rt_alloc(&mem, 10);
    void *d = rt_alloc(&mem, 10);
    CHECK(a && b && !c && d);
    CHECK_EQ(mem.requests, 4);
    CHECK_EQ(mem.failures, 1);
    CHECK_STATUS(mem.error, MS_ERR_RESOURCE_EXHAUSTED); /* sticky after success */

    rt_mem_set_failure(&mem, 0, RT_MEM_FOREVER);
    for (int i = 0; i < 5; i++) {
        CHECK(rt_alloc(&mem, 1) == NULL);
        CHECK(rt_bump(&mem, 1) == NULL); /* even with room in the chunk */
        CHECK(rt_calloc(&mem, 1, 1) == NULL);
    }
    CHECK_EQ(mem.failures, 16);
    CHECK(rt_realloc(&mem, a, 8) == a); /* in place: not a request */
    rt_mem_set_failure(&mem, 0, 0);
    CHECK(rt_alloc(&mem, 1) != NULL);
    rt_mem_reset(&mem);
    CHECK_STATUS(mem.error, MS_OK);
    rt_mem_dispose(&mem);
}

/* The rollback pattern peers use: every partial allocation is undone. */
typedef struct demo_parts {
    uint32_t *table;
    uint8_t *bytes;
} demo_parts;

static ms_status demo_build(rt_mem *mem, demo_parts *out) {
    rt_mark mark = rt_mem_mark(mem);
    out->table = NULL;
    out->bytes = NULL;
    out->table = (uint32_t *)rt_calloc(mem, 1000, sizeof(uint32_t));
    if (!out->table) goto fail;
    if (!rt_bump(mem, 300)) goto fail;
    out->bytes = (uint8_t *)rt_alloc(mem, 9000);
    if (!out->bytes) goto fail;
    if (!rt_bump(mem, 70000)) goto fail;
    return MS_OK;
fail:
    rt_free(mem, out->bytes);
    rt_free(mem, out->table);
    rt_mem_rewind(mem, mark);
    out->table = NULL;
    out->bytes = NULL;
    return MS_ERR_RESOURCE_EXHAUSTED;
}

static void test_mem_cleanup_sweep(void) {
    test_case("allocation failure at every point leaves nothing behind");
    for (uint64_t k = 0; k <= 5; k++) {
        rt_mem mem;
        rt_mem_init(&mem, RT_MEM_UNLIMITED);
        void *keep = rt_alloc(&mem, 64); /* unrelated live memory survives */
        CHECK(keep != NULL);
        size_t baseline = mem.live;
        rt_mem_set_failure(&mem, k, RT_MEM_FOREVER);
        demo_parts parts;
        ms_status status = demo_build(&mem, &parts);
        CHECK_STATUS(status, k < 4 ? MS_ERR_RESOURCE_EXHAUSTED : MS_OK);
        if (status != MS_OK) {
            CHECK_EQ(mem.live, baseline);
            CHECK_EQ(mem.live_blocks, 1);
        } else {
            rt_free(&mem, parts.bytes);
            rt_free(&mem, parts.table);
        }
        rt_mem_dispose(&mem);
    }
}

static void test_mem_lifecycle(void) {
    test_case("rt_mem initialization, disposal and misuse");
    static rt_mem never_initialized; /* zero-initialized, never rt_mem_init */
    CHECK(rt_alloc(&never_initialized, 8) == NULL);
    CHECK(rt_bump(&never_initialized, 8) == NULL);
    CHECK_STATUS(never_initialized.error, MS_ERR_INTERNAL);
    CHECK_EQ(never_initialized.misuses, 2);

    rt_mem mem;
    rt_mem other;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    rt_mem_init(&other, RT_MEM_UNLIMITED);
    uint8_t *p = (uint8_t *)rt_alloc(&mem, 32);
    CHECK(p != NULL && rt_bump(&mem, 32) != NULL);
    rt_free(&other, p); /* not other's block */
    CHECK_STATUS(other.error, MS_ERR_INTERNAL);
    CHECK_EQ(other.misuses, 1);
    CHECK_EQ(mem.live_blocks, 2);
    rt_free(&mem, p);
    CHECK_EQ(mem.live_blocks, 1);
    CHECK_EQ(mem.misuses, 0);
    rt_free(&mem, p); /* double free is detected, not executed */
    CHECK_STATUS(mem.error, MS_ERR_INTERNAL);
    CHECK_EQ(mem.misuses, 1);
    CHECK_EQ(mem.live_blocks, 1);

    rt_mem_dispose(&mem);
    CHECK_EQ(mem.live, 0);
    CHECK_EQ(mem.live_blocks, 0);
    CHECK(rt_alloc(&mem, 8) == NULL);
    CHECK_STATUS(mem.error, MS_ERR_INTERNAL);
    CHECK_EQ(mem.misuses, 2);
    rt_mem_dispose(&mem);
    rt_mem_reset(&mem);
    rt_mem_init(&mem, 4096);
    CHECK_EQ(mem.misuses, 0);
    CHECK(rt_alloc(&mem, 8) != NULL);
    CHECK_STATUS(mem.error, MS_OK);
    rt_mem_dispose(&mem);
    rt_mem_dispose(&other);
}

static void test_mem_pointer_safety(void) {
    test_case("wild, foreign, interior, freed and bump pointers are refused without access");
    static uint8_t outside[64]; /* static storage, never from rt_mem */
    static rt_mem never_initialized;
    rt_mem mem;
    rt_mem other;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    rt_mem_init(&other, RT_MEM_UNLIMITED);
    uint8_t *small = (uint8_t *)rt_alloc(&mem, 100);
    uint8_t *large = (uint8_t *)rt_calloc(&mem, 5000, 1);
    uint8_t *foreign = (uint8_t *)rt_alloc(&other, 64);
    uint8_t *bump = (uint8_t *)rt_bump(&mem, 32);
    CHECK(small && large && foreign && bump);
    size_t small_capacity = rt_mem_capacity(&mem, small);
    CHECK(small_capacity >= 100 && small_capacity < 4096);
    CHECK(rt_mem_capacity(&mem, large) >= 5000);
    CHECK(rt_mem_capacity(&other, foreign) >= 64);
    CHECK_EQ(rt_mem_capacity(&mem, NULL), 0);
    CHECK_EQ(rt_mem_capacity(NULL, small), 0);

    /* None of these starts a live allocation of `mem`, and most are not even
     * addressable: a header read before them would fault natively and trap
     * in WebAssembly. Each is refused as a misuse and changes nothing. */
    void *const refused[] = {
        (void *)(uintptr_t)1, (void *)(uintptr_t)16, (void *)(uintptr_t)4096,
        (void *)((uintptr_t)-1 & ~(uintptr_t)15), (void *)(uintptr_t)-1,
        outside, outside + 48, small + 1, small + 16, small + small_capacity, large + 4096,
        foreign, bump,
    };
    size_t live = mem.live;
    size_t blocks = mem.live_blocks;
    uint64_t misuses = mem.misuses;
    for (size_t i = 0; i < array_size(refused); i++) {
        void *ptr = refused[i];
        CHECK_EQ(rt_mem_capacity(&mem, ptr), 0);
        rt_free(&mem, ptr);
        CHECK_EQ(mem.misuses, ++misuses);
        CHECK(rt_realloc(&mem, ptr, 8) == NULL);
        CHECK_EQ(mem.misuses, ++misuses);
        size_t capacity = 77; /* growth is needed: ownership is checked */
        CHECK(rt_grow(&mem, ptr, &capacity, 100, sizeof(uint32_t)) == NULL);
        CHECK_EQ(capacity, 77);
        CHECK_EQ(mem.misuses, ++misuses);
    }
    CHECK_EQ(mem.live, live);
    CHECK_EQ(mem.live_blocks, blocks);
    CHECK_STATUS(mem.error, MS_ERR_INTERNAL);
    CHECK_EQ(other.misuses, 0);
    CHECK(rt_mem_capacity(&other, foreign) >= 64);

    /* The context still works and its own pointers still match. */
    small[0] = 1;
    small[99] = 2;
    CHECK_EQ(rt_mem_capacity(&mem, small), small_capacity);
    uint8_t *moved = (uint8_t *)rt_realloc(&mem, small, 10000);
    CHECK(moved != NULL && moved != small);
    CHECK_EQ(moved[0] + moved[99], 3);
    CHECK(rt_mem_capacity(&mem, moved) >= 10000);

    /* Freed: the block a reallocation moved away from, and a freed block. */
    CHECK_EQ(rt_mem_capacity(&mem, small), 0);
    rt_free(&mem, small);
    CHECK_EQ(mem.misuses, ++misuses);
    rt_free(&mem, large);
    CHECK_EQ(mem.misuses, misuses);
    CHECK_EQ(rt_mem_capacity(&mem, large), 0);
    rt_free(&mem, large);
    CHECK_EQ(mem.misuses, ++misuses);
    CHECK(rt_realloc(&mem, large, 8) == NULL);
    CHECK_EQ(mem.misuses, ++misuses);

    /* After a reset nothing old is owned; after disposal nothing at all. */
    rt_mem_reset(&mem);
    CHECK_EQ(rt_mem_capacity(&mem, moved), 0);
    rt_free(&mem, moved);
    CHECK_EQ(mem.misuses, ++misuses);
    void *fresh = rt_alloc(&mem, 8);
    CHECK(fresh != NULL && rt_mem_capacity(&mem, fresh) > 0);
    rt_mem_dispose(&mem);
    CHECK_EQ(rt_mem_capacity(&mem, fresh), 0);
    rt_free(&mem, fresh);
    CHECK_EQ(mem.misuses, ++misuses);
    CHECK_EQ(rt_mem_capacity(&never_initialized, foreign), 0);
    rt_mem_dispose(&other);
}

static void test_mem_capacity(void) {
    test_case("capacity covers every request (rt_mem_capacity contract of c/wasm_api.c)");
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    static const size_t sizes[] = {0, 1, 15, 16, 17, 4000, 4048, 4049, 4096, 8092, 8192,
                                   65537, (size_t)1 << 20};
    for (size_t i = 0; i < array_size(sizes); i++) {
        size_t size = sizes[i];
        uint8_t *p = (uint8_t *)rt_alloc(&mem, size);
        uint8_t *z = (uint8_t *)rt_calloc(&mem, size, 1);
        CHECK(p != NULL && z != NULL);
        CHECK(rt_mem_capacity(&mem, p) >= size && rt_mem_capacity(&mem, p) > 0);
        CHECK(rt_mem_capacity(&mem, z) >= size && rt_mem_capacity(&mem, z) > 0);
        uint8_t *grown = (uint8_t *)rt_realloc(&mem, p, 2 * size + 1);
        CHECK(grown != NULL && rt_mem_capacity(&mem, grown) >= 2 * size + 1);
        CHECK(rt_realloc(&mem, grown, size / 2) == grown); /* shrinking stays in place */
        CHECK(rt_mem_capacity(&mem, grown) >= 2 * size + 1);
        rt_free(&mem, grown);
        rt_free(&mem, z);
        CHECK_EQ(rt_mem_capacity(&mem, grown), 0);
        CHECK_EQ(rt_mem_capacity(&mem, z), 0);
    }
    CHECK_EQ(mem.live, 0);
    CHECK_EQ(mem.failures + mem.misuses, 0);
    rt_mem_dispose(&mem);
}

static void test_mem_stress(void) {
    test_case("rt_mem randomized alloc/realloc/free keeps contents and accounting");
    enum { SLOTS = 48 };
    uint8_t *ptr[SLOTS];
    size_t size[SLOTS];
    size_t charge[SLOTS];
    rt_mem mem;
    rt_rng rng;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    rt_rng_seed(&rng, 2024);
    for (int i = 0; i < SLOTS; i++) {
        ptr[i] = NULL;
        size[i] = 0;
        charge[i] = 0;
    }
    for (int step = 0; step < 3000; step++) {
        uint32_t i = rt_rng_below(&rng, SLOTS);
        uint32_t op = rt_rng_below(&rng, 3);
        if (ptr[i]) {
            for (size_t k = 0; k < size[i]; k++) CHECK_EQ(ptr[i][k], (uint8_t)(i * 31 + k));
        }
        if (op == 0 && ptr[i]) {
            rt_free(&mem, ptr[i]);
            ptr[i] = NULL;
            size[i] = 0;
            charge[i] = 0;
        } else {
            size_t want = 1 + rt_rng_below(&rng, 20000);
            uint8_t *next = (uint8_t *)rt_realloc(&mem, ptr[i], want);
            CHECK(next != NULL && aligned16(next));
            if (next != ptr[i]) charge[i] = rt_mem_charge(want);
            for (size_t k = 0; k < want; k++) next[k] = (uint8_t)(i * 31 + k);
            ptr[i] = next;
            size[i] = want;
        }
        size_t expected = 0;
        for (int k = 0; k < SLOTS; k++) expected += charge[k];
        CHECK_EQ(mem.live, expected);
    }
    CHECK_EQ(mem.failures + mem.misuses, 0);
    rt_mem_reset(&mem);
    CHECK_EQ(mem.live, 0);
    CHECK_STATUS(mem.error, MS_OK);
    rt_mem_dispose(&mem);
}

/* ---------------------------------------------------------------- RNG/hash */

static void test_rng_vectors(void) {
    test_case("splitmix64/xoshiro256** known-answer vectors");
    /* splitmix64 reference outputs for seed 0. */
    uint64_t state = 0;
    CHECK_EQ(rt_splitmix64(&state), 0xe220a8397b1dcdafull);
    CHECK_EQ(rt_splitmix64(&state), 0x6e789e6aa1b965f4ull);
    CHECK_EQ(rt_splitmix64(&state), 0x06c45d188009454full);

    /* xoshiro256** reference outputs for the state {1, 2, 3, 4}. */
    static const uint64_t reference[10] = {
        11520ull, 0ull, 1509978240ull, 1215971899390074240ull,
        1216172134540287360ull, 607988272756665600ull, 16172922978634559625ull,
        8476171486693032832ull, 10595114339597558777ull, 2904607092377533576ull,
    };
    rt_rng rng;
    rng.s[0] = 1;
    rng.s[1] = 2;
    rng.s[2] = 3;
    rng.s[3] = 4;
    for (int i = 0; i < 10; i++) CHECK_EQ(rt_rng_next(&rng), reference[i]);

    /* rt_rng_seed = four splitmix64 outputs; values from an independent
     * implementation of the published algorithms. */
    rt_rng_seed(&rng, 0);
    CHECK_EQ(rng.s[0], 0xe220a8397b1dcdafull);
    CHECK_EQ(rng.s[3], 0xf88bb8a8724c81ecull);
    CHECK_EQ(rt_rng_next(&rng), 0x99ec5f36cb75f2b4ull);
    CHECK_EQ(rt_rng_next(&rng), 0xbf6e1f784956452aull);
    CHECK_EQ(rt_rng_next(&rng), 0x1a5f849d4933e6e0ull);
    CHECK_EQ(rt_rng_next(&rng), 0x6aa594f1262d2d2cull);
    rt_rng_seed(&rng, 0x0123456789abcdefull);
    CHECK_EQ(rt_rng_next(&rng), 0xa2c2a42038d4ec3dull);
    CHECK_EQ(rt_rng_next(&rng), 0x05fc25d0738e7b0full);
    CHECK_EQ(rt_rng_next(&rng), 0x625e7bff938e701eull);
    CHECK_EQ(rt_rng_next(&rng), 0x1ba4ddc6fe2b5726ull);

    rt_rng_seed(&rng, 42);
    CHECK_EQ(rt_rng_u32(&rng), 0x15780b2eu);
    CHECK_EQ(rt_rng_u32(&rng), 0x6104d986u);
    CHECK_EQ(rt_rng_u32(&rng), 0xae175332u);
    CHECK_EQ(rt_rng_u32(&rng), 0xecb8ad47u);

    static const uint32_t tens[16] = {7, 2, 8, 9, 9, 8, 0, 1, 4, 1, 5, 7, 9, 8, 4, 5};
    rt_rng_seed(&rng, 7);
    for (int i = 0; i < 16; i++) CHECK_EQ(rt_rng_below(&rng, 10), tens[i]);
    static const uint32_t bounds[6] = {1, 2, 3, 6400, 0x80000001u, 0xffffffffu};
    static const uint32_t draws[6] = {0, 0, 2, 6279, 2127856246u, 3748535522u};
    rt_rng_seed(&rng, 7);
    for (int i = 0; i < 6; i++) CHECK_EQ(rt_rng_below(&rng, bounds[i]), draws[i]);

    rt_rng before = rng;
    CHECK_EQ(rt_rng_below(&rng, 0), 0);
    CHECK(base_memcmp(&before, &rng, sizeof(rng)) == 0); /* bound 0 draws nothing */

    uint32_t items[10];
    static const uint32_t chosen[10] = {3, 6, 5, 8, 4, 2, 1, 7, 0, 9};
    for (uint32_t i = 0; i < 10; i++) items[i] = i;
    rt_rng_seed(&rng, 99);
    rt_rng_choose(&rng, items, 10, 4);
    for (int i = 0; i < 10; i++) CHECK_EQ(items[i], chosen[i]);
    for (uint32_t i = 0; i < 10; i++) items[i] = i;
    rt_rng_choose(&rng, items, 3, 7); /* k clamped to count */
    for (uint32_t i = 3; i < 10; i++) CHECK_EQ(items[i], i);
    CHECK_EQ(items[0] + items[1] + items[2], 3);
}

static void test_rng_uniform(void) {
    test_case("bounded draws and choices are uniform and unbiased");
    rt_rng rng;
    rt_rng_seed(&rng, 12345);
    uint32_t buckets[7] = {0};
    for (int i = 0; i < 70000; i++) buckets[rt_rng_below(&rng, 7)]++;
    for (int i = 0; i < 7; i++) CHECK(buckets[i] >= 9500 && buckets[i] <= 10500);

    /* A modulo or plain multiply-shift draw would put about half of these
     * in [0, 2^30); the exact draw puts a third there. */
    uint32_t low = 0;
    for (int i = 0; i < 30000; i++) low += rt_rng_below(&rng, 0xC0000000u) < 0x40000000u;
    CHECK(low >= 9500 && low <= 10500);

    uint32_t ones = 0;
    for (int i = 0; i < 20000; i++) ones += rt_rng_bit(&rng);
    CHECK(ones >= 9700 && ones <= 10300);

    uint32_t pairs[5][5] = {{0}};
    for (int trial = 0; trial < 20000; trial++) {
        uint32_t items[5] = {0, 1, 2, 3, 4};
        rt_rng_choose(&rng, items, 5, 2);
        CHECK(items[0] != items[1]);
        pairs[items[0]][items[1]]++;
    }
    for (int a = 0; a < 5; a++) {
        for (int b = 0; b < 5; b++) {
            if (a == b) CHECK_EQ(pairs[a][b], 0);
            else CHECK(pairs[a][b] >= 850 && pairs[a][b] <= 1150);
        }
    }

    uint32_t perms[6] = {0};
    for (int trial = 0; trial < 12000; trial++) {
        uint32_t items[3] = {0, 1, 2};
        rt_rng_choose(&rng, items, 3, 3);
        uint32_t code = items[0] * 2 + (items[1] > items[2]); /* 6 permutations */
        CHECK(items[0] + items[1] + items[2] == 3 && items[1] != items[2]);
        perms[code]++;
    }
    for (int i = 0; i < 6; i++) CHECK(perms[i] >= 1800 && perms[i] <= 2200);

    rt_rng x;
    rt_rng y;
    rt_rng_seed(&x, 5);
    rt_rng_seed(&y, 5);
    for (int i = 0; i < 1000; i++) CHECK_EQ(rt_rng_next(&x), rt_rng_next(&y));
    rt_rng_seed(&y, 6);
    CHECK(rt_rng_next(&x) != rt_rng_next(&y));
}

static void test_hash_vectors(void) {
    test_case("rt_hash64 and ms_obs_hash known answers");
    CHECK_EQ(rt_hash64("", 0, 0), 0x0c342374724801c1ull);
    CHECK_EQ(rt_hash64("a", 1, 0), 0x11a4f600cf03939cull);
    CHECK_EQ(rt_hash64("abcdefgh", 8, 0), 0xb976fedfb12b6410ull);
    CHECK_EQ(rt_hash64("abcdefghi", 9, 0), 0xef216311973077c6ull);
    CHECK_EQ(rt_hash64("abc", 3, 0x6d732d6f62732d31ull), 0xfecf3d8f270a135aull);
    uint8_t bytes[101];
    for (int i = 0; i < 100; i++) bytes[i + 1] = (uint8_t)i;
    CHECK_EQ(rt_hash64(bytes + 1, 100, 0), 0x2f18c13ee3697bceull); /* unaligned input */
    CHECK(rt_hash64(bytes + 1, 100, 1) != rt_hash64(bytes + 1, 100, 0));
    CHECK(rt_hash64(bytes + 1, 99, 0) != rt_hash64(bytes + 1, 100, 0));

    uint64_t storage[TEST_WORDS(40)];
    CHECK_STATUS(ms_obs_init(storage, 40, 3, 1, 1), MS_OK);
    CHECK_STATUS(ms_obs_set_clue(storage, 1, 1), MS_OK);
    CHECK_EQ(ms_obs_hash(storage), 0x11b1012357efc2c3ull);
}

/* ---------------------------------------------------------------- clock */

static double nan_reading(void *ctx) {
    (void)ctx;
    double zero = 0.0;
    return zero / zero;
}

static void test_clock(void) {
    test_case("injected clock is finite and monotonic");
    rt_clock clock;
    fake_clock fake;
    fake_clock_init(&clock, &fake, 100.0, 0.0);
    CHECK(rt_clock_now(&clock) == 100.0);
    fake.now = 50.0; /* backwards */
    CHECK(rt_clock_now(&clock) == 100.0);
    fake.now = HUGE_VAL;
    CHECK(rt_clock_now(&clock) == 100.0);
    fake.now = 100.25;
    CHECK(rt_clock_now(&clock) == 100.25);
    CHECK_EQ(fake.reads, 4);

    rt_clock_init(&clock, nan_reading, NULL);
    CHECK(rt_clock_now(&clock) == 0.0);
    rt_clock_init(&clock, NULL, NULL);
    CHECK(rt_clock_now(&clock) == 0.0);
    fake_clock_init(&clock, &fake, -5.0, 1.0); /* first valid reading is taken as is */
    CHECK(rt_clock_now(&clock) == -5.0);
    CHECK(rt_clock_now(&clock) == -4.0);

    CHECK(rt_deadline(10.0, 1500.0, 0.35) == 535.0);
    CHECK(rt_deadline(10.0, 1500.0, 1.0) == 1510.0);
    CHECK(rt_deadline(10.0, 0.0, 1.0) == 10.0);
    CHECK(rt_deadline(10.0, HUGE_VAL, 0.5) == HUGE_VAL);
    CHECK(rt_deadline(10.0, HUGE_VAL, 0.0) == HUGE_VAL);
}

static void test_meter(void) {
    test_case("amortized deadline checks");
    rt_clock clock;
    fake_clock fake;
    rt_meter meter;
    fake_clock_init(&clock, &fake, 0.0, 0.0);
    rt_meter_init(&meter, &clock, 100.0, 4);
    CHECK(!rt_meter_work(&meter, 1));
    CHECK(!rt_meter_work(&meter, 1));
    CHECK(!rt_meter_work(&meter, 1));
    CHECK_EQ(fake.reads, 0);
    CHECK(!rt_meter_work(&meter, 1)); /* 4 pending: one reading */
    CHECK_EQ(fake.reads, 1);
    fake.now = 100.0; /* not past the deadline: strictly greater expires */
    CHECK(!rt_meter_work(&meter, 10));
    CHECK_EQ(fake.reads, 2);
    fake.now = 100.5;
    CHECK(!rt_meter_work(&meter, 3));
    CHECK(rt_meter_work(&meter, 1));
    CHECK_EQ(fake.reads, 3);
    CHECK(rt_meter_work(&meter, 1000) && rt_meter_check(&meter)); /* sticky */
    CHECK_EQ(fake.reads, 3);

    rt_meter_init(&meter, &clock, 1000.0, 0); /* interval 0 acts as 1 */
    CHECK(!rt_meter_work(&meter, 1) && fake.reads == 4);
    CHECK(!rt_meter_check(&meter) && fake.reads == 5);
    rt_meter_init(&meter, &clock, 1000.0, 8);
    CHECK(!rt_meter_work(&meter, 7));
    CHECK(!rt_meter_work(&meter, UINT32_MAX)); /* saturates, then reads */
    CHECK_EQ(fake.reads, 6);

    fake.now = 1e300;
    rt_meter_init(&meter, &clock, HUGE_VAL, 1);
    CHECK(!rt_meter_work(&meter, 5) && !rt_meter_check(&meter));
    double zero = 0.0;
    rt_meter_init(&meter, &clock, zero / zero, 1);
    CHECK(rt_meter_check(&meter));
}

/* ---------------------------------------------------------------- grid */

static void expect_neighbors(uint32_t width, uint32_t height, uint32_t index,
                             const uint32_t *want, uint32_t count) {
    uint32_t got[8];
    CHECK_EQ(rt_grid_neighbors(width, height, index, got), count);
    for (uint32_t i = 0; i < count; i++) CHECK_EQ(got[i], want[i]);
}

static void test_grid(void) {
    test_case("bounded grid neighbors");
    static const uint32_t corner[3] = {1, 80, 81};
    static const uint32_t top_right[3] = {78, 158, 159};
    static const uint32_t last[3] = {6318, 6319, 6398};
    static const uint32_t inner[8] = {0, 1, 2, 80, 82, 160, 161, 162};
    static const uint32_t chain_start[1] = {1};
    static const uint32_t chain_end[1] = {2999};
    static const uint32_t chain_mid[2] = {1499, 1501};
    expect_neighbors(80, 80, 0, corner, 3);
    expect_neighbors(80, 80, 79, top_right, 3);
    expect_neighbors(80, 80, 6399, last, 3);
    expect_neighbors(80, 80, 81, inner, 8);
    expect_neighbors(3001, 1, 0, chain_start, 1);
    expect_neighbors(3001, 1, 3000, chain_end, 1);
    expect_neighbors(3001, 1, 1500, chain_mid, 2);
    expect_neighbors(1, 5, 2, (const uint32_t[]){1, 3}, 2);
    expect_neighbors(1, 1, 0, NULL, 0);
    expect_neighbors(80, 80, 6400, NULL, 0);
    expect_neighbors(0, 80, 0, NULL, 0);
    expect_neighbors(65536, 65536, 0, NULL, 0);

    /* Brute force over every cell of small and thin grids. */
    for (uint32_t width = 1; width <= 7; width++) {
        for (uint32_t height = 1; height <= 7; height++) {
            for (uint32_t index = 0; index < width * height; index++) {
                uint32_t want[8];
                uint32_t count = 0;
                int row = (int)(index / width);
                int col = (int)(index % width);
                for (int dr = -1; dr <= 1; dr++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        int r = row + dr;
                        int c = col + dc;
                        if ((dr || dc) && r >= 0 && c >= 0 && r < (int)height && c < (int)width) {
                            want[count++] = (uint32_t)(r * (int)width + c);
                        }
                    }
                }
                expect_neighbors(width, height, index, want, count);
            }
        }
    }
}

/* ---------------------------------------------------------------- ABI */

static void test_abi_layout(void) {
    test_case("ABI layouts, names and constants");
    CHECK_EQ(sizeof(ms_view_header), 48);
    CHECK_EQ(MS_OFFSETOF(ms_view_header, generation), 8);
    CHECK_EQ(MS_OFFSETOF(ms_view_header, status), 16);
    CHECK_EQ(MS_OFFSETOF(ms_view_header, flags), 32);
    CHECK_EQ(sizeof(ms_obs_header), 32);
    CHECK_EQ(MS_OFFSETOF(ms_obs_header, total_mines), 16);
    CHECK_EQ(MS_OFFSETOF(ms_obs_header, revealed), 20);
    CHECK_EQ(sizeof(ms_infer_limits), 56);
    CHECK_EQ(MS_OFFSETOF(ms_infer_limits, flags), 20);
    CHECK_EQ(MS_OFFSETOF(ms_infer_limits, min_effective_samples), 32);
    CHECK_EQ(MS_OFFSETOF(ms_infer_limits, memory_budget_bytes), 40);
    CHECK_EQ(sizeof(ms_result_header), 120);
    CHECK_EQ(MS_OFFSETOF(ms_result_header, frontier_cells), 40);
    CHECK_EQ(MS_OFFSETOF(ms_result_header, nodes), 68);
    CHECK_EQ(MS_OFFSETOF(ms_result_header, proven_safe), 88);
    CHECK_EQ(MS_OFFSETOF(ms_result_header, has_effective_sample_size), 96);
    CHECK_EQ(MS_OFFSETOF(ms_result_header, effective_sample_size), 112);

    CHECK(base_strcmp(ms_status_name(MS_OK), "ok") == 0);
    CHECK(base_strcmp(ms_status_name(MS_ERR_STALE_REVISION), "stale_revision") == 0);
    CHECK(base_strcmp(ms_status_name(MS_ERR_GAME_NOT_STARTED), "game_not_started") == 0);
    CHECK(base_strcmp(ms_status_name(MS_ERR_INCONSISTENT), "inconsistent_observation") == 0);
    CHECK(base_strcmp(ms_status_name((ms_status)12345), "unknown_status") == 0);
    CHECK(ms_status_is_input_error(MS_ERR_INVALID_BUFFER));
    CHECK(!ms_status_is_input_error(MS_ERR_STALE_REVISION));
    CHECK(ms_status_is_conflict(MS_ERR_GAME_NOT_STARTED));
    CHECK(!ms_status_is_conflict(MS_ERR_INCONSISTENT));
    CHECK(base_strcmp(ms_prob_reason_name(MS_REASON_NONE), "") == 0);
    CHECK(base_strcmp(ms_prob_reason_name(MS_REASON_COUNTING_BUDGET_EXCEEDED),
                      "counting_budget_exceeded") == 0);
    CHECK(base_strcmp(ms_prob_reason_name(MS_REASON_MEMORY_BUDGET_EXHAUSTED),
                      "memory_budget_exhausted") == 0);
    CHECK(base_strcmp(ms_prob_reason_name(MS_REASON_GAME_OVER), "game_over") == 0);
    CHECK(ms_prob_reason_name(10) == NULL);
}

static void test_config_and_limits(void) {
    test_case("game configuration and inference limits validation");
    CHECK_STATUS(ms_game_check_config(5, 5, 1), MS_OK);
    CHECK_STATUS(ms_game_check_config(5, 5, 16), MS_OK);
    CHECK_STATUS(ms_game_check_config(5, 5, 17), MS_ERR_INVALID_MINES);
    CHECK_STATUS(ms_game_check_config(5, 5, 0), MS_ERR_INVALID_MINES);
    CHECK_STATUS(ms_game_check_config(80, 80, 6391), MS_OK);
    CHECK_STATUS(ms_game_check_config(80, 80, 6392), MS_ERR_INVALID_MINES);
    CHECK_STATUS(ms_game_check_config(4, 5, 1), MS_ERR_INVALID_WIDTH);
    CHECK_STATUS(ms_game_check_config(81, 5, 1), MS_ERR_INVALID_WIDTH);
    CHECK_STATUS(ms_game_check_config(5, 4, 1), MS_ERR_INVALID_HEIGHT);
    CHECK_STATUS(ms_game_check_config(5, 81, 1), MS_ERR_INVALID_HEIGHT);
    CHECK_STATUS(ms_game_check_config(0, 0, 0), MS_ERR_INVALID_WIDTH);
    CHECK_STATUS(ms_game_check_config(5, 0, 0), MS_ERR_INVALID_HEIGHT);
    CHECK_STATUS(ms_game_check_config(UINT32_MAX, 5, 1), MS_ERR_INVALID_WIDTH);

    ms_infer_limits limits;
    ms_limits_default(&limits);
    CHECK_STATUS(ms_limits_validate(&limits), MS_OK);
    CHECK(limits.time_budget_ms == 1500.0);
    CHECK_EQ(limits.node_budget, 100000);
    CHECK_EQ(limits.sample_budget, 2000);
    CHECK(limits.min_effective_samples == 50.0);
    CHECK_EQ(limits.max_stored_entries, 1500000);
    CHECK_EQ(limits.memory_budget_bytes, 256ull << 20);
    CHECK_EQ(limits.flags, 0);

    ms_infer_limits bad = limits;
    double zero = 0.0;
    bad.time_budget_ms = -1.0;
    CHECK_STATUS(ms_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    bad.time_budget_ms = zero / zero;
    CHECK_STATUS(ms_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    bad.time_budget_ms = HUGE_VAL;
    CHECK_STATUS(ms_limits_validate(&bad), MS_OK);
    bad.time_budget_ms = 0.0;
    bad.node_budget = 0;
    bad.sample_budget = 0;
    bad.memory_budget_bytes = 0;
    CHECK_STATUS(ms_limits_validate(&bad), MS_OK);
    bad.min_effective_samples = HUGE_VAL;
    CHECK_STATUS(ms_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    bad.min_effective_samples = -0.5;
    CHECK_STATUS(ms_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    bad = limits;
    bad.flags = MS_LIMIT_EXPLICIT_SEED;
    CHECK_STATUS(ms_limits_validate(&bad), MS_OK);
    bad.flags = 2;
    CHECK_STATUS(ms_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    bad = limits;
    bad.magic = MS_MAGIC_RESULT;
    CHECK_STATUS(ms_limits_validate(&bad), MS_ERR_INVALID_BUFFER);
    bad = limits;
    bad.version = MS_ABI_VERSION + 1;
    CHECK_STATUS(ms_limits_validate(&bad), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_limits_validate(NULL), MS_ERR_INVALID_BUFFER);
    uint64_t storage[TEST_WORDS(sizeof(ms_infer_limits) + 8)];
    uint8_t *odd = (uint8_t *)storage + 4;
    base_memcpy(odd, &limits, sizeof(limits));
    CHECK_STATUS(ms_limits_validate((const ms_infer_limits *)(void *)odd), MS_ERR_INVALID_BUFFER);
}

static void test_observation(void) {
    test_case("observation builder and structural validation");
    uint64_t obs[TEST_WORDS(40)];
    uint64_t copy[TEST_WORDS(40)];
    CHECK_STATUS(ms_obs_init(obs, sizeof(obs), 3, 1, 1), MS_OK);
    const ms_obs_header *h = (const ms_obs_header *)obs;
    CHECK_EQ(h->magic, MS_MAGIC_OBSERVATION);
    CHECK_EQ(h->width, 3);
    CHECK_EQ(h->total_mines, 1);
    CHECK_EQ(h->revealed, 0);
    for (int i = 0; i < 3; i++) CHECK_EQ(ms_obs_clues(obs)[i], MS_CLUE_HIDDEN);
    CHECK_STATUS(ms_obs_validate(obs, sizeof(obs)), MS_OK);
    CHECK_STATUS(ms_obs_set_clue(obs, 1, 1), MS_OK);
    CHECK_STATUS(ms_obs_set_clue(obs, 1, 1), MS_OK);
    CHECK_EQ(h->revealed, 1);
    CHECK_STATUS(ms_obs_set_clue(obs, 1, MS_CLUE_HIDDEN), MS_OK);
    CHECK_EQ(h->revealed, 0);
    CHECK_STATUS(ms_obs_set_clue(obs, 1, 1), MS_OK);
    CHECK_STATUS(ms_obs_set_clue(obs, 0, 9), MS_ERR_INVALID_OBSERVATION);
    CHECK_STATUS(ms_obs_set_clue(obs, 3, 0), MS_ERR_INVALID_OBSERVATION);
    CHECK_STATUS(ms_obs_validate(obs, sizeof(obs)), MS_OK);
    uint64_t hash = ms_obs_hash(obs);

#define EXPECT_OBS(expected, mutation)                                       \
    do {                                                                     \
        base_memcpy(copy, obs, sizeof(obs));                                 \
        ms_obs_header *m = (ms_obs_header *)copy;                            \
        uint8_t *clues = ms_obs_clues(copy);                                 \
        (void)m;                                                             \
        (void)clues;                                                         \
        mutation;                                                            \
        CHECK_STATUS(ms_obs_validate(copy, sizeof(copy)), expected);         \
    } while (0)

    EXPECT_OBS(MS_OK, (void)0);
    EXPECT_OBS(MS_ERR_INVALID_BUFFER, m->magic = MS_MAGIC_VIEW);
    EXPECT_OBS(MS_ERR_INVALID_BUFFER, m->version = 2);
    EXPECT_OBS(MS_ERR_INVALID_OBSERVATION, m->width = 0);
    EXPECT_OBS(MS_ERR_INVALID_OBSERVATION, m->width = 4); /* same length: padding byte reads as clue 0 */
    EXPECT_OBS(MS_ERR_INVALID_BUFFER, m->width = 9);
    EXPECT_OBS(MS_ERR_INVALID_OBSERVATION, m->total_mines = 4);
    EXPECT_OBS(MS_OK, m->total_mines = 3);
    EXPECT_OBS(MS_ERR_INVALID_OBSERVATION, m->reserved1 = 1);
    EXPECT_OBS(MS_ERR_INVALID_OBSERVATION, clues[0] = 9);
    EXPECT_OBS(MS_ERR_INVALID_OBSERVATION, clues[0] = 0x80);
    EXPECT_OBS(MS_ERR_INVALID_OBSERVATION, m->revealed = 2);
    EXPECT_OBS(MS_ERR_INVALID_BUFFER, ((uint8_t *)copy)[39] = 1);
#undef EXPECT_OBS
    CHECK_STATUS(ms_obs_validate(obs, sizeof(obs) - 8), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_obs_validate(NULL, sizeof(obs)), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_obs_validate((uint8_t *)obs + 4, sizeof(obs)), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_obs_validate(obs, 16), MS_ERR_INVALID_BUFFER);

    /* The fingerprint covers the clues and the total. */
    base_memcpy(copy, obs, sizeof(obs));
    CHECK_EQ(ms_obs_hash(copy), hash);
    CHECK_STATUS(ms_obs_set_clue(copy, 0, 0), MS_OK);
    CHECK(ms_obs_hash(copy) != hash);
    base_memcpy(copy, obs, sizeof(obs));
    ((ms_obs_header *)copy)->total_mines = 2;
    CHECK(ms_obs_hash(copy) != hash);

    CHECK_STATUS(ms_obs_init(obs, sizeof(obs), 0, 1, 0), MS_ERR_INVALID_OBSERVATION);
    CHECK_STATUS(ms_obs_init(obs, sizeof(obs), 3, 1, 4), MS_ERR_INVALID_OBSERVATION);
    CHECK_STATUS(ms_obs_init(obs, sizeof(obs) - 8, 3, 1, 1), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_obs_init((uint8_t *)obs + 4, 40, 3, 1, 1), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_obs_init(obs, sizeof(obs), 6401, 1, 1), MS_ERR_INVALID_OBSERVATION);

    /* Thin, full-size, all-mine and no-mine observations. */
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    size_t thin = ms_obs_size(3001, 1);
    void *chain = rt_alloc(&mem, thin);
    CHECK_STATUS(ms_obs_init(chain, thin, 3001, 1, 751), MS_OK);
    for (uint32_t i = 1; i < 3001; i += 2) CHECK_STATUS(ms_obs_set_clue(chain, i, 1), MS_OK);
    CHECK_EQ(((ms_obs_header *)chain)->revealed, 1500);
    CHECK_STATUS(ms_obs_validate(chain, thin), MS_OK);
    size_t full = ms_obs_size(80, 80);
    void *board = rt_alloc(&mem, full);
    CHECK_STATUS(ms_obs_init(board, full, 80, 80, 6400), MS_OK);
    CHECK_STATUS(ms_obs_validate(board, full), MS_OK);
    CHECK_STATUS(ms_obs_init(board, full, 80, 80, 0), MS_OK);
    CHECK_STATUS(ms_obs_validate(board, full), MS_OK);
    rt_mem_dispose(&mem);
}

/* Python's (4, 1, 1, {0: 1, 3: 0}): probabilities [None, 1.0, 0.0, None],
 * proven_mines [1], proven_safe [2], both fixed by propagation. */
static void make_forced_result(uint64_t obs[TEST_WORDS(40)], uint64_t res[TEST_WORDS(160)]) {
    CHECK_STATUS(ms_obs_init(obs, 40, 4, 1, 1), MS_OK);
    CHECK_STATUS(ms_obs_set_clue(obs, 0, 1), MS_OK);
    CHECK_STATUS(ms_obs_set_clue(obs, 3, 0), MS_OK);
    CHECK_EQ(ms_result_size(4, 1), 160);
    CHECK_STATUS(ms_result_init(res, 160, obs, 40), MS_OK);
    ms_result_header *r = (ms_result_header *)res;
    CHECK_EQ(r->status, 0);
    CHECK_EQ(r->hidden_cells, 2);
    CHECK_EQ(r->observation_hash, ms_obs_hash(obs));
    CHECK_STATUS(ms_result_validate(obs, 40, res, 160), MS_ERR_INVALID_RESULT);
    r->status = MS_PROB_EXACT;
    r->propagated_cells = 2;
    r->proven_safe = 1;
    r->proven_mines = 1;
    r->pair_reasoning_complete = 1;
    r->elapsed_ms = 0.5;
    double *p = ms_result_probabilities(res);
    uint8_t *f = ms_result_flags(res);
    p[1] = 1.0;
    f[1] = MS_PCELL_VALUE | MS_PCELL_PROVEN_MINE;
    p[2] = 0.0;
    f[2] = MS_PCELL_VALUE | MS_PCELL_PROVEN_SAFE;
}

/* Copies res_buf, applies `mutation`, and expects `accept` from the engine's
 * gate (ms_result_validate) and `solver` from ms_result_validate_solver. */
#define EXPECT_RESULT(obs_buf, res_buf, accept, solver, mutation)                    \
    do {                                                                             \
        uint64_t tmp_[TEST_WORDS(sizeof(res_buf))];                                  \
        base_memcpy(tmp_, res_buf, sizeof(res_buf));                                 \
        ms_result_header *r = (ms_result_header *)tmp_;                              \
        double *p = ms_result_probabilities(tmp_);                                   \
        uint8_t *f = ms_result_flags(tmp_);                                          \
        (void)r;                                                                     \
        (void)p;                                                                     \
        (void)f;                                                                     \
        mutation;                                                                    \
        CHECK_STATUS(ms_result_validate(obs_buf, sizeof(obs_buf), tmp_, sizeof(tmp_)), \
                     accept);                                                        \
        CHECK_STATUS(ms_result_validate_solver(obs_buf, sizeof(obs_buf), tmp_,        \
                                               sizeof(tmp_)),                        \
                     solver);                                                        \
    } while (0)

#define OK_ MS_OK
#define BAD MS_ERR_INVALID_RESULT
#define BUF MS_ERR_INVALID_BUFFER

static void test_result_exact(void) {
    test_case("probability result validation: exact");
    uint64_t obs[TEST_WORDS(40)];
    uint64_t res[TEST_WORDS(160)];
    make_forced_result(obs, res);
    CHECK_STATUS(ms_result_validate(obs, sizeof(obs), res, sizeof(res)), MS_OK);

    EXPECT_RESULT(obs, res, OK_, OK_, (void)0);
    EXPECT_RESULT(obs, res, BAD, BAD, f[0] = MS_PCELL_VALUE);  /* revealed cell */
    EXPECT_RESULT(obs, res, BAD, BAD, p[3] = 0.5);
    EXPECT_RESULT(obs, res, BAD, BAD, f[2] |= 0x08);
    EXPECT_RESULT(obs, res, BAD, BAD, f[2] |= MS_PCELL_PROVEN_MINE);
    EXPECT_RESULT(obs, res, BAD, BAD, p[2] = 0.25);           /* safe proof != 0 */
    EXPECT_RESULT(obs, res, BAD, BAD, f[2] = MS_PCELL_PROVEN_SAFE); /* no VALUE */
    EXPECT_RESULT(obs, res, BAD, BAD, f[1] = MS_PCELL_VALUE; r->proven_mines = 0);
    EXPECT_RESULT(obs, res, BAD, BAD, f[2] = MS_PCELL_VALUE; r->proven_safe = 0);
    EXPECT_RESULT(obs, res, OK_, OK_, p[1] = 0.5; f[1] = MS_PCELL_VALUE;
                  r->proven_mines = 0); /* structurally fine: truth is the solver's job */
    EXPECT_RESULT(obs, res, BAD, BAD, p[1] = 0.0; f[1] = MS_PCELL_VALUE;
                  r->proven_mines = 0); /* exact 0.0 must be a proof */
    EXPECT_RESULT(obs, res, BAD, BAD, r->proven_safe = 2);
    EXPECT_RESULT(obs, res, BAD, BAD, r->observation_hash ^= 1);
    EXPECT_RESULT(obs, res, BAD, BAD, r->hidden_cells = 3);
    EXPECT_RESULT(obs, res, BAD, BAD, r->remaining_mines = 2);
    EXPECT_RESULT(obs, res, BAD, BAD, r->effective_sample_size = 1.0);
    EXPECT_RESULT(obs, res, BAD, BAD, r->pair_reasoning_complete = 2);
    EXPECT_RESULT(obs, res, BAD, BAD, r->reserved = 1);
    EXPECT_RESULT(obs, res, BAD, BAD, r->elapsed_ms = -1.0);
    EXPECT_RESULT(obs, res, BAD, BAD, r->status = MS_PROB_NOT_STARTED);
    EXPECT_RESULT(obs, res, BAD, BAD, r->reason = MS_REASON_NOT_STARTED);
    EXPECT_RESULT(obs, res, BAD, BAD, r->reason = 10);
    EXPECT_RESULT(obs, res, BAD, BAD, r->total_mines = 2);
    EXPECT_RESULT(obs, res, BUF, BUF, r->magic = MS_MAGIC_OBSERVATION);
    EXPECT_RESULT(obs, res, BUF, BUF, ((uint8_t *)tmp_)[159] = 1);
    double zero = 0.0;
    EXPECT_RESULT(obs, res, BAD, BAD, p[2] = zero / zero);
    EXPECT_RESULT(obs, res, BAD, BAD, p[1] = 1.5);
    /* The server accepted any pairing of status, reason and diagnostics;
     * only solver output must keep Python's pairing. */
    EXPECT_RESULT(obs, res, OK_, BAD, r->reason = MS_REASON_COUNTING_BUDGET_EXCEEDED);
    EXPECT_RESULT(obs, res, OK_, BAD, r->components = 1);
    EXPECT_RESULT(obs, res, OK_, BAD, r->propagated_cells = 1);
    EXPECT_RESULT(obs, res, OK_, BAD, r->sample_attempts = 1);
    EXPECT_RESULT(obs, res, OK_, BAD, r->samples = 1; r->sample_attempts = 1);
    EXPECT_RESULT(obs, res, OK_, BAD, r->has_effective_sample_size = 1);
    CHECK_STATUS(ms_result_validate(obs, sizeof(obs), res, sizeof(res) - 8), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_result_validate(obs, sizeof(obs), (uint8_t *)res + 4, sizeof(res)),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_result_validate(obs, 32, res, sizeof(res)), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_result_init(res, sizeof(res) - 8, obs, sizeof(obs)), MS_ERR_INVALID_BUFFER);

    /* Exact values strictly between 0 and 1 for an unproven cell are fine;
     * exactly 0.0 or 1.0 without a proof is not. */
    uint64_t obs5[TEST_WORDS(40)];
    uint64_t res5[TEST_WORDS(168)];
    CHECK_STATUS(ms_obs_init(obs5, sizeof(obs5), 5, 1, 2), MS_OK);
    CHECK_STATUS(ms_obs_set_clue(obs5, 1, 1), MS_OK);
    CHECK_STATUS(ms_result_init(res5, sizeof(res5), obs5, sizeof(obs5)), MS_OK);
    ms_result_header *r5 = (ms_result_header *)res5;
    r5->status = MS_PROB_EXACT;
    r5->frontier_cells = 2;
    r5->unconstrained_cells = 2;
    r5->components = 1;
    r5->exact_components = 1;
    r5->remaining_mines = 2;
    r5->nodes = 3;
    double *p5 = ms_result_probabilities(res5);
    uint8_t *f5 = ms_result_flags(res5);
    static const uint32_t hidden5[4] = {0, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        p5[hidden5[i]] = 0.5;
        f5[hidden5[i]] = MS_PCELL_VALUE;
    }
    CHECK_STATUS(ms_result_validate_solver(obs5, sizeof(obs5), res5, sizeof(res5)), MS_OK);
    EXPECT_RESULT(obs5, res5, BAD, BAD, p[0] = 0.0);
    EXPECT_RESULT(obs5, res5, BAD, BAD, p[4] = 1.0);
    EXPECT_RESULT(obs5, res5, BAD, BAD, f[3] = 0; p[3] = 0.0);
    EXPECT_RESULT(obs5, res5, OK_, OK_, p[0] = 4.9e-324); /* smallest subnormal: not a proof */
}

static void test_result_estimates(void) {
    test_case("probability result validation: approximate, unavailable, placeholders");
    uint64_t obs[TEST_WORDS(40)];
    uint64_t res[TEST_WORDS(168)];
    CHECK_STATUS(ms_obs_init(obs, sizeof(obs), 5, 1, 2), MS_OK);
    CHECK_STATUS(ms_obs_set_clue(obs, 1, 1), MS_OK);
    CHECK_STATUS(ms_result_init(res, sizeof(res), obs, sizeof(obs)), MS_OK);
    ms_result_header *h = (ms_result_header *)res;
    h->status = MS_PROB_APPROXIMATE;
    h->reason = MS_REASON_COUNTING_BUDGET_EXCEEDED;
    h->frontier_cells = 2;
    h->unconstrained_cells = 2;
    h->components = 1;
    h->sampled_components = 1;
    h->samples = 10;
    h->sample_attempts = 12;
    h->remaining_mines = 2;
    h->has_effective_sample_size = 1;
    h->effective_sample_size = 8.5;
    h->elapsed_ms = 3.25;
    double *values = ms_result_probabilities(res);
    uint8_t *flags = ms_result_flags(res);
    static const uint32_t hidden[4] = {0, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        values[hidden[i]] = 0.5;
        flags[hidden[i]] = MS_PCELL_VALUE;
    }
    CHECK_STATUS(ms_result_validate_solver(obs, sizeof(obs), res, sizeof(res)), MS_OK);
    EXPECT_RESULT(obs, res, OK_, OK_, p[0] = 0.0; p[2] = 1.0); /* sampled endpoints, no proof */
    EXPECT_RESULT(obs, res, OK_, OK_, f[3] |= MS_PCELL_PROVEN_SAFE; p[3] = 0.0; r->proven_safe = 1);
    EXPECT_RESULT(obs, res, BAD, BAD, f[3] = 0; p[3] = 0.0);
    EXPECT_RESULT(obs, res, BAD, BAD, r->effective_sample_size = HUGE_VAL);
    EXPECT_RESULT(obs, res, OK_, BAD, r->has_effective_sample_size = 0;
                  r->effective_sample_size = 0.0);
    EXPECT_RESULT(obs, res, OK_, BAD, r->samples = 0);
    EXPECT_RESULT(obs, res, OK_, BAD, r->samples = 13);
    EXPECT_RESULT(obs, res, OK_, BAD, r->reason = MS_REASON_NONE);
    EXPECT_RESULT(obs, res, OK_, BAD, r->reason = MS_REASON_MEMORY_BUDGET_EXHAUSTED);

    /* Unavailable: only proven cells carry values. */
    h->status = MS_PROB_UNAVAILABLE;
    h->reason = MS_REASON_SAMPLING_BUDGET_EXHAUSTED;
    h->samples = 0;
    h->sample_attempts = 0;
    h->has_effective_sample_size = 0;
    h->effective_sample_size = 0.0;
    for (int i = 0; i < 4; i++) {
        values[hidden[i]] = 0.0;
        flags[hidden[i]] = 0;
    }
    CHECK_STATUS(ms_result_validate_solver(obs, sizeof(obs), res, sizeof(res)), MS_OK);
    EXPECT_RESULT(obs, res, OK_, OK_, f[4] = MS_PCELL_VALUE | MS_PCELL_PROVEN_MINE; p[4] = 1.0;
                  r->proven_mines = 1);
    EXPECT_RESULT(obs, res, BAD, BAD, f[4] = MS_PCELL_VALUE; p[4] = 0.5);
    EXPECT_RESULT(obs, res, OK_, OK_, r->reason = MS_REASON_MEMORY_BUDGET_EXHAUSTED);
    EXPECT_RESULT(obs, res, OK_, OK_, r->reason = MS_REASON_TIME_BUDGET_EXHAUSTED;
                  r->frontier_cells = 0; r->unconstrained_cells = 0; r->components = 0;
                  r->sampled_components = 0); /* stopped before components: at most */
    EXPECT_RESULT(obs, res, BAD, BAD, r->reason = MS_REASON_NOT_STARTED);
    EXPECT_RESULT(obs, res, OK_, BAD, r->propagated_cells = 1); /* 5 > 4 hidden */
    EXPECT_RESULT(obs, res, OK_, BAD, r->reason = MS_REASON_NONE);
    EXPECT_RESULT(obs, res, OK_, BAD, r->reason = MS_REASON_COUNTING_BUDGET_EXCEEDED);

    /* Placeholders are written by the engine, never accepted as solver output. */
    uint64_t holder[TEST_WORDS(352)];
    CHECK_EQ(ms_result_size(5, 5), 352);
    CHECK_STATUS(ms_result_placeholder(holder, 352, 5, 5, 3, MS_PROB_NOT_STARTED), MS_OK);
    const ms_result_header *ph = (const ms_result_header *)holder;
    CHECK_EQ(ph->magic, MS_MAGIC_RESULT);
    CHECK_EQ(ph->status, MS_PROB_NOT_STARTED);
    CHECK_EQ(ph->reason, MS_REASON_NOT_STARTED);
    CHECK_EQ(ph->total_mines, 3);
    CHECK_EQ(ph->frontier_cells + ph->samples + ph->observation_hash, 0);
    for (uint32_t i = 0; i < 25; i++) {
        CHECK(ms_result_probabilities(holder)[i] == 0.0);
        CHECK_EQ(ms_result_flags(holder)[i], 0);
    }
    CHECK_STATUS(ms_result_placeholder(holder, 352, 5, 5, 3, MS_PROB_FINISHED), MS_OK);
    CHECK_EQ(ph->reason, MS_REASON_GAME_OVER);
    CHECK_STATUS(ms_result_placeholder(holder, 352, 5, 5, 3, MS_PROB_EXACT), MS_ERR_INVALID_RESULT);
    CHECK_STATUS(ms_result_placeholder(holder, 344, 5, 5, 3, MS_PROB_FINISHED), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_result_placeholder(holder, 352, 5, 5, 26, MS_PROB_FINISHED), MS_ERR_INVALID_RESULT);
    CHECK_STATUS(ms_result_placeholder(holder, 352, 0, 5, 0, MS_PROB_FINISHED), MS_ERR_INVALID_RESULT);
    uint64_t obs25[TEST_WORDS(64)];
    CHECK_STATUS(ms_obs_init(obs25, sizeof(obs25), 5, 5, 3), MS_OK);
    CHECK_STATUS(ms_result_placeholder(holder, 352, 5, 5, 3, MS_PROB_NOT_STARTED), MS_OK);
    CHECK_STATUS(ms_result_validate(obs25, sizeof(obs25), holder, 352), MS_ERR_INVALID_RESULT);
}

#undef OK_
#undef BAD
#undef BUF

/* Builds a 5x5 view from a layout ('*' mine) and a state map: '#' hidden,
 * 'F' flagged, 'R' revealed safe (clue computed from the layout), 'X'
 * exploded mine. Mine bits are set iff the status is won/lost. */
static void build_view(uint64_t view[TEST_WORDS(80)], uint32_t status, const char *layout,
                       const char *state, double elapsed_ms) {
    base_memset(view, 0, 80);
    ms_view_header *h = (ms_view_header *)view;
    uint8_t *cells = ms_view_cells(view);
    bool terminal = status == MS_GAME_WON || status == MS_GAME_LOST;
    h->magic = MS_MAGIC_VIEW;
    h->version = MS_ABI_VERSION;
    h->generation = 1;
    h->revision = 3;
    h->status = status;
    h->width = 5;
    h->height = 5;
    h->elapsed_ms = elapsed_ms;
    for (uint32_t i = 0; i < 25; i++) {
        uint8_t b = MS_CELL_NO_ADJACENT;
        if (layout[i] == '*') h->mines++;
        if (state[i] == 'R') {
            uint32_t around[8];
            uint32_t n = rt_grid_neighbors(5, 5, i, around);
            uint8_t clue = 0;
            for (uint32_t k = 0; k < n; k++) clue += layout[around[k]] == '*';
            b = (uint8_t)(MS_CELL_REVEALED | clue);
        } else if (state[i] == 'X') {
            b |= MS_CELL_REVEALED | MS_CELL_EXPLODED;
        } else if (state[i] == 'F') {
            b |= MS_CELL_FLAGGED;
        }
        if (terminal && layout[i] == '*') b |= MS_CELL_MINE;
        h->flags += (b & MS_CELL_FLAGGED) != 0;
        h->revealed += (b & MS_CELL_REVEALED) != 0;
        cells[i] = b;
    }
}

#define EXPECT_VIEW(view_buf, expected, mutation)                                 \
    do {                                                                          \
        uint64_t tmp_[TEST_WORDS(80)];                                            \
        base_memcpy(tmp_, view_buf, 80);                                          \
        ms_view_header *h = (ms_view_header *)tmp_;                               \
        uint8_t *c = ms_view_cells(tmp_);                                         \
        (void)h;                                                                  \
        (void)c;                                                                  \
        mutation;                                                                 \
        CHECK_STATUS(ms_view_validate(tmp_, 80), expected);                       \
    } while (0)

static void test_view(void) {
    test_case("public view validation");
    static const char layout[] = "**......................*"; /* mines 0, 1, 24 */
    uint64_t view[TEST_WORDS(80)];
    CHECK_EQ(ms_view_size(5, 5), 80);

    build_view(view, MS_GAME_READY, layout, "#########################", 0.0);
    CHECK_STATUS(ms_view_validate(view, 80), MS_OK);
    EXPECT_VIEW(view, MS_OK, c[3] |= MS_CELL_FLAGGED; h->flags = 1); /* flags before the start */
    EXPECT_VIEW(view, MS_ERR_INTERNAL, h->elapsed_ms = 5.0);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[7] = MS_CELL_REVEALED | 1; h->revealed = 1);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, h->generation = 0);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, h->revision = MS_REVISION_MAX + 1u);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, h->status = 5);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, h->mines = 17);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, h->width = 4);
    EXPECT_VIEW(view, MS_ERR_INVALID_BUFFER, h->width = 7);
    EXPECT_VIEW(view, MS_ERR_INVALID_BUFFER, h->magic = MS_MAGIC_RESULT);
    EXPECT_VIEW(view, MS_ERR_INVALID_BUFFER, ((uint8_t *)tmp_)[79] = 1);
    double zero = 0.0;
    EXPECT_VIEW(view, MS_ERR_INTERNAL, h->elapsed_ms = zero / zero);
    CHECK_STATUS(ms_view_validate(view, 72), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_view_validate((uint8_t *)view + 4, 80), MS_ERR_INVALID_BUFFER);

    build_view(view, MS_GAME_PLAYING, layout, "############R#######F####", 1234.0);
    CHECK_STATUS(ms_view_validate(view, 80), MS_OK);
    CHECK_EQ(ms_view_cells(view)[12], MS_CELL_REVEALED | 0);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[0] |= MS_CELL_MINE);          /* hidden layout */
    EXPECT_VIEW(view, MS_ERR_INTERNAL, h->flags = 0);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[12] = MS_CELL_REVEALED | MS_CELL_NO_ADJACENT);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[13] = 3);                     /* hidden with a clue */
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[12] |= MS_CELL_FLAGGED; h->flags = 2);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[12] |= MS_CELL_EXPLODED);
    build_view(view, MS_GAME_PLAYING, layout, "##RRRRRRRRRRRRRRRRRRRRRR#", 1.0);
    CHECK_STATUS(ms_view_validate(view, 80), MS_ERR_INTERNAL); /* all safe open: won */

    build_view(view, MS_GAME_LOST, layout, "#####R######R###########X", 50.0);
    CHECK_STATUS(ms_view_validate(view, 80), MS_OK);
    CHECK_EQ(ms_view_cells(view)[5], MS_CELL_REVEALED | 2);
    CHECK_EQ(ms_view_cells(view)[24],
             MS_CELL_REVEALED | MS_CELL_MINE | MS_CELL_EXPLODED | MS_CELL_NO_ADJACENT);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[5] = MS_CELL_REVEALED | 1);   /* wrong clue */
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[1] &= (uint8_t)~MS_CELL_MINE);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[24] &= (uint8_t)~MS_CELL_EXPLODED);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[12] |= MS_CELL_EXPLODED);
    EXPECT_VIEW(view, MS_OK, c[7] |= MS_CELL_FLAGGED; h->flags = 1);   /* wrong flag shown */
    build_view(view, MS_GAME_LOST, layout, "#####R######R############", 50.0);
    CHECK_STATUS(ms_view_validate(view, 80), MS_ERR_INTERNAL); /* lost without explosion */

    build_view(view, MS_GAME_WON, layout, "FFRRRRRRRRRRRRRRRRRRRRRRF", 99.0);
    CHECK_STATUS(ms_view_validate(view, 80), MS_OK);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[0] &= (uint8_t)~MS_CELL_FLAGGED; h->flags = 2);
    EXPECT_VIEW(view, MS_ERR_INTERNAL, c[10] = MS_CELL_NO_ADJACENT; h->revealed = 21);
}

/* Harness regression: an empty test_print once spun forever inside corec's
 * write_all (zero-length iovec), so reaching the check below is the test. */
static void test_print_empty(void) {
    test_case("test_print(\"\") returns");
    static int reached;
    test_print("");
    reached = 1;
    CHECK_EQ(reached, 1);
}

void test_runtime(void) {
    test_print_empty();
    test_checked_arithmetic();
    test_mem_basics();
    test_mem_budget();
    test_mem_realloc();
    test_mem_grow();
    test_mem_bump();
    test_mem_failure_injection();
    test_mem_cleanup_sweep();
    test_mem_lifecycle();
    test_mem_pointer_safety();
    test_mem_capacity();
    test_mem_stress();
    test_rng_vectors();
    test_rng_uniform();
    test_hash_vectors();
    test_clock();
    test_meter();
    test_grid();
    test_abi_layout();
    test_config_and_limits();
    test_observation();
    test_result_exact();
    test_result_estimates();
    test_view();
}
