#pragma once

/*
 * runtime.h - small portable foundations on top of corec base/ and
 * platform/: checked sizes, a fallible budgeted allocation context, a
 * seeded PRNG, hashing, an injected monotonic clock with amortized deadline
 * checks, and grid neighbors. Pure C, no system headers, no libc, no
 * __int128; identical results natively and in WebAssembly.
 *
 * Single-threaded: every WASM instance and native test is single-threaded.
 */

#include <base/types.h>
#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * Checked arithmetic: return false (and leave *out untouched) on overflow.
 * Use these instead of `a > UINT64_MAX / b` style checks: LLVM rewrites
 * that idiom into a 128-bit multiply, which wasm32 can only link through
 * compiler-rt's __multi3 (rejected by our link). No __builtin_*_overflow
 * either (unavailable in MSVC, same helper problem on wasm32).
 * ------------------------------------------------------------------------ */
bool rt_add_size(size_t a, size_t b, size_t *out);
bool rt_mul_size(size_t a, size_t b, size_t *out);
bool rt_add_u64(uint64_t a, uint64_t b, uint64_t *out);
bool rt_mul_u64(uint64_t a, uint64_t b, uint64_t *out);

/* ------------------------------------------------------------------------
 * rt_mem: a fallible allocation context with ownership and a byte budget
 *
 * Backed by corec's buddy allocator (platform_init/buddy_init must have
 * run). Two kinds of memory, both 16-byte aligned and owned by the context:
 *
 * - rt_alloc/rt_calloc/rt_realloc/rt_free: one buddy block per allocation,
 *   individually freeable. Use for arrays and growable buffers. Each costs a
 *   power-of-two block of at least 4 KiB: never use it for many small
 *   objects.
 * - rt_bump/rt_bump_array: carved from 64 KiB chunks; released only
 *   together, by rt_mem_rewind to an earlier rt_mem_mark, rt_mem_reset or
 *   rt_mem_dispose. Use for many small objects (big-integer limbs, DP
 *   entries, per-phase scratch).
 *
 * Accounting: `live` is the sum of the buddy blocks the context owns (size
 * rounding and headers included), `peak` its high-water mark. A request
 * that would push live above `budget` fails BEFORE calling buddy_alloc, as
 * do requests above RT_MEM_MAX_REQUEST or whose size computation overflows.
 * Every failure returns NULL, counts in `failures` and records the first
 * status in `error` (sticky until reset): MS_ERR_RESOURCE_EXHAUSTED for
 * budget/size/injected/buddy failures, MS_ERR_INTERNAL for misuse. Misuse
 * (freeing a foreign, wild, interior, bump or already freed pointer, using a
 * disposed or uninitialized context, rewinding to a stale mark) changes
 * nothing and also counts in `misuses`, which tests can assert stays 0. A
 * failed request changes nothing else; a failed rt_realloc leaves the old
 * block valid and owned.
 *
 * Ownership: a pointer belongs to the context only if it equals the user
 * pointer of one of its live allocations, found by walking the context's
 * own block list (most recently used first) before any header is read.
 * rt_free, rt_realloc, a growing rt_grow and rt_mem_capacity therefore
 * refuse any other pointer value, even one outside the heap or the
 * WebAssembly memory, instead of faulting.
 *
 * Limits of the guarantee: corec's buddy allocator aborts the process (in
 * WASM: proc_exit) if the platform cannot grow the heap, so the budget is
 * the real defence; keep budgets well below what the host can provide.
 *
 * Failure injection (tests): rt_mem_set_failure(mem, after, count) lets the
 * next `after` requests through and then fails `count` consecutive requests
 * (RT_MEM_FOREVER: all). Every rt_alloc/rt_calloc/rt_bump/rt_bump_array
 * call, and every rt_realloc/rt_grow that needs a new block, is one request.
 *
 * corec's vector.h/hashtable.h grow through arena_alloc, which aborts on
 * exhaustion and never frees: acceptable for small bounded setup data, not
 * for budgeted solver or big-integer storage, which must use rt_mem.
 *
 * Fields are public for inspection; mutate only through the functions.
 * ------------------------------------------------------------------------ */
#define RT_MEM_ALIGN 16u
#define RT_MEM_UNLIMITED ((size_t)-1)
#define RT_MEM_FOREVER UINT64_MAX
/* Largest single request: keeps every buddy block <= 2 GiB, which corec's
 * size arithmetic can represent on wasm32 (32-bit size_t). */
#define RT_MEM_MAX_REQUEST ((size_t)1 << 30)
#define RT_MEM_CHUNK_BLOCK ((size_t)64 << 10)

typedef struct rt_block rt_block;

typedef struct rt_mark {
    rt_block *chunk;
    uint8_t *bump;
} rt_mark;

struct rt_mem {
    rt_block *blocks;     /* individually freeable allocations */
    rt_block *chunks;     /* bump chunks, newest first */
    uint8_t *bump;        /* next free byte of the newest chunk */
    uint8_t *bump_end;
    size_t budget;        /* max live bytes; RT_MEM_UNLIMITED for none */
    size_t live;          /* bytes of buddy blocks currently owned */
    size_t peak;          /* high-water mark of live */
    size_t live_blocks;   /* buddy blocks currently owned (allocs + chunks) */
    uint64_t requests;    /* allocation requests since rt_mem_init */
    uint64_t failures;    /* failed requests since rt_mem_init */
    uint64_t misuses;     /* misuse events since rt_mem_init */
    uint64_t fail_after;  /* failure injection, see above */
    uint64_t fail_count;
    ms_status error;      /* first failure since init/reset, MS_OK if none */
    uint32_t state;       /* private: live/disposed marker */
};

/* Starts an empty context. Never allocates. */
void rt_mem_init(rt_mem *mem, size_t budget);

/* Frees every block and chunk; keeps budget, injection, counters and peak;
 * clears `error`. Marks taken before are invalid afterwards. */
void rt_mem_reset(rt_mem *mem);

/* rt_mem_reset, then the context refuses every request (MS_ERR_INTERNAL)
 * until rt_mem_init. Safe to call twice. */
void rt_mem_dispose(rt_mem *mem);

/* A budget below `live` is allowed: requests fail until enough is freed. */
void rt_mem_set_budget(rt_mem *mem, size_t budget);

/* Lowers the budget to min(budget, live + extra) and returns the previous
 * budget, to be restored with rt_mem_set_budget: caps one phase's own
 * allocations while the context holds unrelated live memory. */
size_t rt_mem_limit(rt_mem *mem, size_t extra);

void rt_mem_reset_peak(rt_mem *mem); /* peak = live */
void rt_mem_set_failure(rt_mem *mem, uint64_t after, uint64_t count);

/* Bytes rt_alloc(size) would add to `live` (0 if size is too large). */
size_t rt_mem_charge(size_t size);

void *rt_alloc(rt_mem *mem, size_t size);                  /* uninitialized */
void *rt_calloc(rt_mem *mem, size_t count, size_t size);   /* zeroed, checked */
/* Keeps the old contents; stays in place while the block's capacity
 * suffices (shrinking never moves). NULL ptr: rt_alloc. */
void *rt_realloc(rt_mem *mem, void *ptr, size_t size);
void rt_free(rt_mem *mem, void *ptr);                      /* NULL: no-op */

/* Usable bytes of the allocation that `ptr` starts, when `ptr` is the
 * pointer a live rt_alloc/rt_calloc/rt_realloc/rt_grow of this context
 * returned; otherwise 0 (NULL mem or ptr, wild, foreign, interior, freed or
 * bump pointers, disposed or uninitialized contexts). Every live allocation
 * has a nonzero capacity of at least the bytes last requested for it (bytes
 * beyond the request are not zeroed by rt_calloc). A pure lookup: never
 * reads memory at `ptr`, never counts misuse. A host-supplied range
 * [ptr, ptr + len) lies inside one allocation iff the result is nonzero and
 * >= len; c/wasm_api.c relies on exactly this to refuse pointer/length pairs
 * before touching memory. */
size_t rt_mem_capacity(const rt_mem *mem, const void *ptr);

/* Growable arrays: returns `items` (an rt_alloc'd array of this context, or
 * NULL for none) resized to hold at least `needed` elements of `elem_size`
 * bytes, growing geometrically; *capacity receives the usable element count.
 * On input *capacity is ignored when items is NULL and must otherwise not
 * exceed the count rt_grow last stored for `items`. `needed <= *capacity`
 * returns `items` at once (no lookup, no memory access); any other call
 * first proves that `items` is owned (else a misuse) and takes the capacity
 * from the allocation itself. On failure returns NULL and leaves items and
 * *capacity unchanged:
 *     uint32_t *grown = rt_grow(mem, list, &cap, count + 1, sizeof *list);
 *     if (!grown) return MS_ERR_RESOURCE_EXHAUSTED;
 *     list = grown; */
void *rt_grow(rt_mem *mem, void *items, size_t *capacity, size_t needed, size_t elem_size);

void *rt_bump(rt_mem *mem, size_t size);                   /* uninitialized */
void *rt_bump_array(rt_mem *mem, size_t count, size_t size); /* zeroed, checked */
rt_mark rt_mem_mark(const rt_mem *mem);
void rt_mem_rewind(rt_mem *mem, rt_mark mark);

/* ------------------------------------------------------------------------
 * Hashing and the PRNG
 *
 * rt_rng is xoshiro256** seeded through splitmix64 (Blackman & Vigna), a
 * fixed, specified sequence on every platform. Bounded draws are exactly
 * uniform (Lemire's multiply-shift with rejection, 32-bit arithmetic only).
 * ------------------------------------------------------------------------ */
typedef struct rt_rng {
    uint64_t s[4];
} rt_rng;

uint64_t rt_mix64(uint64_t x);            /* splitmix64 finalizer, a bijection */
uint64_t rt_splitmix64(uint64_t *state);  /* state += golden gamma; mix */

/* Non-cryptographic 64-bit hash of bytes (little-endian word stream);
 * identical on every platform. */
uint64_t rt_hash64(const void *data, size_t len, uint64_t seed);

void rt_rng_seed(rt_rng *rng, uint64_t seed);
uint64_t rt_rng_next(rt_rng *rng);
uint32_t rt_rng_u32(rt_rng *rng);         /* upper 32 bits of rt_rng_next */
bool rt_rng_bit(rt_rng *rng);             /* top bit of rt_rng_next */

/* Uniform in [0, bound). bound 0 returns 0 without drawing. */
uint32_t rt_rng_below(rt_rng *rng, uint32_t bound);

/* Partial Fisher-Yates: afterwards items[0..k) is a uniformly random k-subset
 * of the original items in uniformly random order (k is clamped to count). */
void rt_rng_choose(rt_rng *rng, uint32_t *items, uint32_t count, uint32_t k);

/* ------------------------------------------------------------------------
 * Injected monotonic clock and amortized deadlines
 *
 * corec has no clock API: the host supplies `now` (browser/Node adapters:
 * performance.now(); C tests: a fake clock). rt_clock_now never goes
 * backwards and is always finite: NaN, infinite or decreasing readings
 * repeat the previous value (0 before the first valid reading); a NULL
 * callback always reads the previous value.
 * ------------------------------------------------------------------------ */
typedef double (*rt_now_fn)(void *ctx);

struct rt_clock {
    rt_now_fn now;
    void *ctx;
    double last;
    bool started;
};

void rt_clock_init(rt_clock *clock, rt_now_fn now, void *ctx);
double rt_clock_now(rt_clock *clock);

/* start + budget * fraction; +infinity when budget is +infinity (Python's
 * _Clock.at). Callers validate budget >= 0 and fraction in [0, 1]. */
double rt_deadline(double start_ms, double budget_ms, double fraction);

/* Amortized deadline check (Python's _Meter.work/_CLOCK_WORK): work units
 * accumulate and the clock is read once `interval` units are pending. The
 * deadline has passed when a reading is strictly greater than it; expiry is
 * sticky. A +infinity deadline never expires; NaN expires at once. */
#define RT_METER_INTERVAL 2048u

typedef struct rt_meter {
    rt_clock *clock;
    double deadline_ms;
    uint32_t pending;
    uint32_t interval;
    bool expired;
} rt_meter;

void rt_meter_init(rt_meter *meter, rt_clock *clock, double deadline_ms, uint32_t interval);
bool rt_meter_work(rt_meter *meter, uint32_t units); /* true once expired */
bool rt_meter_check(rt_meter *meter);                /* reads the clock now */

/* ------------------------------------------------------------------------
 * Grid neighbors
 * ------------------------------------------------------------------------ */

/* Writes the up to 8 in-bounds neighbors of `index` on a width x height
 * row-major grid in ascending order (Python's _neighbors order) and returns
 * their count; 0 for an empty grid, a grid above UINT32_MAX cells or an
 * out-of-range index. */
uint32_t rt_grid_neighbors(uint32_t width, uint32_t height, uint32_t index, uint32_t out[8]);

#ifdef __cplusplus
}
#endif
