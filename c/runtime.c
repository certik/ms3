#include "runtime.h"

#include <base/buddy.h>
#include <base/math.h>
#include <base/mem.h>

static bool rt_finite(double x) {
    return x == x && x - x == 0.0;
}

/* ======================================================================
 * Checked arithmetic
 * ====================================================================== */

bool rt_add_size(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) return false;
    *out = a + b;
    return true;
}

bool rt_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a > UINT64_MAX - b) return false;
    *out = a + b;
    return true;
}

/* Long multiplication on 32-bit halves. The usual `a > UINT64_MAX / b`
 * check is rewritten by LLVM into a 128-bit multiply, which wasm32 can only
 * do through compiler-rt's __multi3. */
bool rt_mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    uint64_t a_hi = a >> 32;
    uint64_t b_hi = b >> 32;
    if (a_hi != 0 && b_hi != 0) return false;
    uint64_t a_lo = a & 0xFFFFFFFFu;
    uint64_t b_lo = b & 0xFFFFFFFFu;
    uint64_t cross = a_hi * b_lo + a_lo * b_hi; /* one term is zero */
    if (cross >> 32) return false;
    uint64_t low = a_lo * b_lo;
    uint64_t product = low + (cross << 32);
    if (product < low) return false;
    *out = product;
    return true;
}

bool rt_mul_size(size_t a, size_t b, size_t *out) {
    uint64_t product;
    if (!rt_mul_u64(a, b, &product)) return false;
    if ((uint64_t)(size_t)product != product) return false; /* 32-bit size_t */
    *out = (size_t)product;
    return true;
}

/* ======================================================================
 * rt_mem
 * ====================================================================== */

#define RT_MEM_STATE_LIVE 0x4C4D5452u     /* "RTML" */
#define RT_MEM_STATE_DISPOSED 0x444D5452u /* "RTMD" */
#define RT_BLOCK_ALLOC 0x41425452u        /* "RTBA" */
#define RT_BLOCK_CHUNK 0x43425452u        /* "RTBC" */

/* Placed at the first 16-byte boundary of every buddy block we own; the
 * caller's memory starts RT_HEADER bytes later. */
struct rt_block {
    rt_block *prev;
    rt_block *next;
    rt_mem *owner;
    void *base;      /* pointer returned by buddy_alloc */
    size_t charge;   /* whole buddy block, as counted in rt_mem.live */
    size_t capacity; /* usable bytes after the header */
    uint32_t kind;
};

#define RT_HEADER ((sizeof(rt_block) + RT_MEM_ALIGN - 1u) / RT_MEM_ALIGN * RT_MEM_ALIGN)
#define RT_OVERHEAD (RT_HEADER + RT_MEM_ALIGN - 1u)
/* corec's private buddy header (int order + two list pointers) takes three
 * pointer-sized words on every target we build. It is only used to predict
 * charges before calling buddy_alloc; charges are computed from what
 * buddy_alloc reports, and the budget is re-checked afterwards. */
#define RT_BUDDY_HEADER (3u * sizeof(void *))
#define RT_BUDDY_MIN_BLOCK ((size_t)4096)
#define RT_CHUNK_PAYLOAD (RT_MEM_CHUNK_BLOCK - RT_BUDDY_HEADER - RT_OVERHEAD)

static uint8_t *rt_user(rt_block *block) {
    return (uint8_t *)block + RT_HEADER;
}

/* Smallest buddy block (power of two, at least 4 KiB) holding n bytes;
 * n stays far below SIZE_MAX / 2 because requests are capped. */
static size_t rt_pow2_block(size_t n) {
    size_t block = RT_BUDDY_MIN_BLOCK;
    while (block < n) block <<= 1;
    return block;
}

size_t rt_mem_charge(size_t size) {
    if (size > RT_MEM_MAX_REQUEST) return 0;
    return rt_pow2_block(size + RT_OVERHEAD + RT_BUDDY_HEADER);
}

static void rt_note(rt_mem *mem, ms_status status) {
    if (mem->error == MS_OK) mem->error = status;
}

static void rt_misuse(rt_mem *mem) {
    mem->misuses++;
    rt_note(mem, MS_ERR_INTERNAL);
}

static void *rt_fail(rt_mem *mem, ms_status status) {
    mem->failures++;
    rt_note(mem, status);
    return NULL;
}

static bool rt_live(const rt_mem *mem) {
    return mem->state == RT_MEM_STATE_LIVE;
}

/* Counts one request and applies failure injection. */
static bool rt_admit(rt_mem *mem) {
    if (!rt_live(mem)) {
        rt_misuse(mem);
        rt_fail(mem, MS_ERR_INTERNAL);
        return false;
    }
    mem->requests++;
    if (mem->fail_count != 0) {
        if (mem->fail_after != 0) {
            mem->fail_after--;
        } else {
            if (mem->fail_count != RT_MEM_FOREVER) mem->fail_count--;
            rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
            return false;
        }
    }
    return true;
}

static size_t rt_room(const rt_mem *mem) {
    return mem->budget > mem->live ? mem->budget - mem->live : 0;
}

static rt_block *rt_acquire(rt_mem *mem, size_t payload, uint32_t kind) {
    if (payload > RT_MEM_MAX_REQUEST) {
        rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
        return NULL;
    }
    size_t request = payload + RT_OVERHEAD;
    if (rt_mem_charge(payload) > rt_room(mem)) {
        rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
        return NULL;
    }
    size_t usable = 0;
    void *base = buddy_alloc(request, &usable);
    if (base == NULL || usable < request) {
        if (base != NULL) buddy_free(base);
        rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
        return NULL;
    }
    size_t charge = rt_pow2_block(usable);
    if (charge > rt_room(mem)) {
        buddy_free(base);
        rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
        return NULL;
    }
    uintptr_t at = ((uintptr_t)base + RT_MEM_ALIGN - 1u) & ~(uintptr_t)(RT_MEM_ALIGN - 1u);
    rt_block *block = (rt_block *)at;
    block->prev = NULL;
    block->next = NULL;
    block->owner = mem;
    block->base = base;
    block->charge = charge;
    block->capacity = usable - (size_t)(at - (uintptr_t)base) - RT_HEADER;
    block->kind = kind;
    mem->live += charge;
    if (mem->live > mem->peak) mem->peak = mem->live;
    mem->live_blocks++;
    return block;
}

static void rt_release(rt_mem *mem, rt_block *block) {
    mem->live -= block->charge;
    mem->live_blocks--;
    block->kind = 0;
    block->owner = NULL;
    buddy_free(block->base);
}

static void rt_link(rt_mem *mem, rt_block *block) {
    block->prev = NULL;
    block->next = mem->blocks;
    if (mem->blocks) mem->blocks->prev = block;
    mem->blocks = block;
}

static void rt_unlink(rt_mem *mem, rt_block *block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        mem->blocks = block->next;
    }
    if (block->next) block->next->prev = block->prev;
}

/* The allocation of this context whose user pointer is `ptr`, or NULL. It is
 * found by walking the context's own block list and comparing user pointers
 * (equality only); memory at or before `ptr` is never read, so a wild,
 * foreign, interior, freed or bump pointer is simply not found and cannot
 * fault. O(live allocations), most recently used first. */
static rt_block *rt_find(const rt_mem *mem, const void *ptr) {
    if (ptr == NULL || !rt_live(mem)) return NULL;
    for (rt_block *block = mem->blocks; block != NULL; block = block->next) {
        if ((const void *)rt_user(block) == ptr) return block;
    }
    return NULL;
}

/* rt_find for an operation on the allocation: counts a misuse when `ptr`
 * is not owned, and moves a found block to the front of the list so that
 * repeated operations on one allocation stay O(1). */
static rt_block *rt_owned(rt_mem *mem, void *ptr) {
    rt_block *block = rt_find(mem, ptr);
    if (block == NULL) {
        rt_misuse(mem);
        return NULL;
    }
    if (block != mem->blocks) {
        rt_unlink(mem, block);
        rt_link(mem, block);
    }
    return block;
}

void rt_mem_init(rt_mem *mem, size_t budget) {
    base_memset(mem, 0, sizeof(*mem));
    mem->budget = budget;
    mem->error = MS_OK;
    mem->state = RT_MEM_STATE_LIVE;
}

void rt_mem_reset(rt_mem *mem) {
    if (!rt_live(mem)) return;
    while (mem->blocks) {
        rt_block *block = mem->blocks;
        mem->blocks = block->next;
        rt_release(mem, block);
    }
    while (mem->chunks) {
        rt_block *chunk = mem->chunks;
        mem->chunks = chunk->next;
        rt_release(mem, chunk);
    }
    mem->bump = NULL;
    mem->bump_end = NULL;
    mem->error = MS_OK;
}

void rt_mem_dispose(rt_mem *mem) {
    if (rt_live(mem)) rt_mem_reset(mem);
    mem->state = RT_MEM_STATE_DISPOSED;
}

void rt_mem_set_budget(rt_mem *mem, size_t budget) {
    mem->budget = budget;
}

size_t rt_mem_limit(rt_mem *mem, size_t extra) {
    size_t previous = mem->budget;
    size_t cap;
    if (!rt_add_size(mem->live, extra, &cap)) cap = RT_MEM_UNLIMITED;
    if (cap < mem->budget) mem->budget = cap;
    return previous;
}

void rt_mem_reset_peak(rt_mem *mem) {
    mem->peak = mem->live;
}

void rt_mem_set_failure(rt_mem *mem, uint64_t after, uint64_t count) {
    mem->fail_after = after;
    mem->fail_count = count;
}

/* One request for a new individually freeable allocation block. */
static rt_block *rt_alloc_block(rt_mem *mem, size_t size) {
    if (!rt_admit(mem)) return NULL;
    rt_block *block = rt_acquire(mem, size, RT_BLOCK_ALLOC);
    if (block) rt_link(mem, block);
    return block;
}

/* The block holding an owned allocation's data after resizing it to `size`
 * bytes: `old` itself when its capacity suffices, else a new block with the
 * whole old capacity copied. NULL on failure, leaving `old` valid. */
static rt_block *rt_resize(rt_mem *mem, rt_block *old, size_t size) {
    if (size <= old->capacity) return old;
    rt_block *block = rt_alloc_block(mem, size);
    if (!block) return NULL;
    /* The whole old capacity is usable (rt_grow hands it out), and it is
     * smaller than `size`: copy all of it. */
    base_memcpy(rt_user(block), rt_user(old), old->capacity);
    rt_unlink(mem, old);
    rt_release(mem, old);
    return block;
}

void *rt_alloc(rt_mem *mem, size_t size) {
    rt_block *block = rt_alloc_block(mem, size);
    return block ? rt_user(block) : NULL;
}

void *rt_calloc(rt_mem *mem, size_t count, size_t size) {
    if (!rt_admit(mem)) return NULL;
    size_t total;
    if (!rt_mul_size(count, size, &total)) return rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
    rt_block *block = rt_acquire(mem, total, RT_BLOCK_ALLOC);
    if (!block) return NULL;
    rt_link(mem, block);
    base_memset(rt_user(block), 0, total);
    return rt_user(block);
}

void *rt_realloc(rt_mem *mem, void *ptr, size_t size) {
    if (ptr == NULL) return rt_alloc(mem, size);
    rt_block *old = rt_owned(mem, ptr);
    if (!old) return rt_fail(mem, MS_ERR_INTERNAL);
    rt_block *block = rt_resize(mem, old, size);
    return block ? rt_user(block) : NULL;
}

void rt_free(rt_mem *mem, void *ptr) {
    if (ptr == NULL) return;
    rt_block *block = rt_owned(mem, ptr);
    if (!block) return;
    rt_unlink(mem, block);
    rt_release(mem, block);
}

size_t rt_mem_capacity(const rt_mem *mem, const void *ptr) {
    const rt_block *block = mem != NULL ? rt_find(mem, ptr) : NULL;
    return block != NULL ? block->capacity : 0;
}

void *rt_grow(rt_mem *mem, void *items, size_t *capacity, size_t needed, size_t elem_size) {
    if (elem_size == 0) {
        rt_misuse(mem);
        return rt_fail(mem, MS_ERR_INTERNAL);
    }
    if (items != NULL && needed <= *capacity) return items; /* appends stay O(1) */
    rt_block *old = NULL;
    size_t current = 0;
    if (items != NULL) {
        old = rt_owned(mem, items);
        if (!old) return rt_fail(mem, MS_ERR_INTERNAL);
        current = old->capacity / elem_size;
        if (needed <= current) {
            *capacity = current;
            return items;
        }
    }
    size_t target = current > 8 ? current : 8;
    while (target < needed && target <= SIZE_MAX / 2) target *= 2;
    size_t bytes;
    if (target < needed || !rt_mul_size(target, elem_size, &bytes) || bytes > RT_MEM_MAX_REQUEST) {
        target = needed; /* geometric growth would overshoot: ask for exactly enough */
        if (!rt_mul_size(target, elem_size, &bytes)) return rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
    }
    rt_block *block = old ? rt_resize(mem, old, bytes) : rt_alloc_block(mem, bytes);
    if (block == NULL) return NULL;
    *capacity = block->capacity / elem_size;
    return rt_user(block);
}

static void *rt_bump_take(rt_mem *mem, size_t size) {
    if (size > RT_MEM_MAX_REQUEST) return rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
    size_t need = (size + RT_MEM_ALIGN - 1u) & ~(size_t)(RT_MEM_ALIGN - 1u);
    if (need == 0) need = RT_MEM_ALIGN;
    if (mem->bump == NULL || (size_t)(mem->bump_end - mem->bump) < need) {
        rt_block *chunk = rt_acquire(mem, need > RT_CHUNK_PAYLOAD ? need : RT_CHUNK_PAYLOAD,
                                     RT_BLOCK_CHUNK);
        if (!chunk) return NULL;
        chunk->next = mem->chunks;
        mem->chunks = chunk;
        mem->bump = rt_user(chunk);
        mem->bump_end = rt_user(chunk) + chunk->capacity;
    }
    void *ptr = mem->bump;
    mem->bump += need;
    return ptr;
}

void *rt_bump(rt_mem *mem, size_t size) {
    if (!rt_admit(mem)) return NULL;
    return rt_bump_take(mem, size);
}

void *rt_bump_array(rt_mem *mem, size_t count, size_t size) {
    if (!rt_admit(mem)) return NULL;
    size_t total;
    if (!rt_mul_size(count, size, &total)) return rt_fail(mem, MS_ERR_RESOURCE_EXHAUSTED);
    void *ptr = rt_bump_take(mem, total);
    if (ptr) base_memset(ptr, 0, total);
    return ptr;
}

rt_mark rt_mem_mark(const rt_mem *mem) {
    rt_mark mark;
    mark.chunk = mem->chunks;
    mark.bump = mem->bump;
    return mark;
}

void rt_mem_rewind(rt_mem *mem, rt_mark mark) {
    if (!rt_live(mem)) {
        rt_misuse(mem);
        return;
    }
    rt_block *chunk = mem->chunks;
    while (chunk && chunk != mark.chunk) chunk = chunk->next;
    if (chunk != mark.chunk) {
        rt_misuse(mem); /* chunk already released: stale mark */
        return;
    }
    if (mark.chunk) {
        uint8_t *start = rt_user(mark.chunk);
        uint8_t *end = start + mark.chunk->capacity;
        bool forward = mark.chunk == mem->chunks && mark.bump > mem->bump;
        if (mark.bump < start || mark.bump > end || forward) {
            rt_misuse(mem);
            return;
        }
    }
    while (mem->chunks != mark.chunk) {
        rt_block *newest = mem->chunks;
        mem->chunks = newest->next;
        rt_release(mem, newest);
    }
    if (mark.chunk) {
        mem->bump = mark.bump;
        mem->bump_end = rt_user(mark.chunk) + mark.chunk->capacity;
    } else {
        mem->bump = NULL;
        mem->bump_end = NULL;
    }
}

/* ======================================================================
 * Hashing and the PRNG
 * ====================================================================== */

uint64_t rt_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

uint64_t rt_splitmix64(uint64_t *state) {
    *state += 0x9e3779b97f4a7c15ull;
    return rt_mix64(*state);
}

uint64_t rt_hash64(const void *data, size_t len, uint64_t seed) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = rt_mix64(seed ^ 0x243f6a8885a308d3ull ^ ((uint64_t)len * 0x9e3779b97f4a7c15ull));
    while (len >= 8) {
        uint64_t word = 0;
        for (int i = 7; i >= 0; i--) word = (word << 8) | p[i];
        h = rt_mix64(h ^ word);
        p += 8;
        len -= 8;
    }
    uint64_t tail = 0;
    for (size_t i = len; i > 0; i--) tail = (tail << 8) | p[i - 1];
    return rt_mix64(h ^ tail ^ 0x13198a2e03707344ull);
}

void rt_rng_seed(rt_rng *rng, uint64_t seed) {
    uint64_t state = seed;
    for (int i = 0; i < 4; i++) rng->s[i] = rt_splitmix64(&state);
}

static uint64_t rt_rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t rt_rng_next(rt_rng *rng) {
    uint64_t *s = rng->s;
    uint64_t result = rt_rotl(s[1] * 5u, 7) * 9u;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rt_rotl(s[3], 45);
    return result;
}

uint32_t rt_rng_u32(rt_rng *rng) {
    return (uint32_t)(rt_rng_next(rng) >> 32);
}

bool rt_rng_bit(rt_rng *rng) {
    return (rt_rng_next(rng) >> 63) != 0;
}

uint32_t rt_rng_below(rt_rng *rng, uint32_t bound) {
    if (bound == 0) return 0;
    uint64_t product = (uint64_t)rt_rng_u32(rng) * bound;
    uint32_t low = (uint32_t)product;
    if (low < bound) {
        uint32_t threshold = (0u - bound) % bound; /* 2^32 mod bound */
        while (low < threshold) {
            product = (uint64_t)rt_rng_u32(rng) * bound;
            low = (uint32_t)product;
        }
    }
    return (uint32_t)(product >> 32);
}

void rt_rng_choose(rt_rng *rng, uint32_t *items, uint32_t count, uint32_t k) {
    if (k > count) k = count;
    for (uint32_t i = 0; i < k; i++) {
        uint32_t j = i + rt_rng_below(rng, count - i);
        uint32_t tmp = items[i];
        items[i] = items[j];
        items[j] = tmp;
    }
}

/* ======================================================================
 * Clock and deadlines
 * ====================================================================== */

void rt_clock_init(rt_clock *clock, rt_now_fn now, void *ctx) {
    clock->now = now;
    clock->ctx = ctx;
    clock->last = 0.0;
    clock->started = false;
}

double rt_clock_now(rt_clock *clock) {
    if (clock->now == NULL) return clock->last;
    double value = clock->now(clock->ctx);
    if (!rt_finite(value)) return clock->last;
    if (!clock->started) {
        clock->started = true;
        clock->last = value;
    } else if (value > clock->last) {
        clock->last = value;
    }
    return clock->last;
}

double rt_deadline(double start_ms, double budget_ms, double fraction) {
    if (budget_ms == HUGE_VAL) return HUGE_VAL;
    return start_ms + budget_ms * fraction;
}

void rt_meter_init(rt_meter *meter, rt_clock *clock, double deadline_ms, uint32_t interval) {
    meter->clock = clock;
    meter->deadline_ms = deadline_ms;
    meter->pending = 0;
    meter->interval = interval ? interval : 1u;
    meter->expired = deadline_ms != deadline_ms;
}

bool rt_meter_check(rt_meter *meter) {
    if (meter->expired) return true;
    meter->pending = 0;
    if (rt_clock_now(meter->clock) > meter->deadline_ms) meter->expired = true;
    return meter->expired;
}

bool rt_meter_work(rt_meter *meter, uint32_t units) {
    if (meter->expired) return true;
    uint32_t pending = meter->pending + units;
    if (pending < meter->pending) pending = UINT32_MAX;
    if (pending < meter->interval) {
        meter->pending = pending;
        return false;
    }
    return rt_meter_check(meter);
}

/* ======================================================================
 * Grid neighbors
 * ====================================================================== */

uint32_t rt_grid_neighbors(uint32_t width, uint32_t height, uint32_t index, uint32_t out[8]) {
    if (width == 0 || height == 0 || height > UINT32_MAX / width) return 0;
    if (index >= width * height) return 0;
    uint32_t row = index / width;
    uint32_t col = index % width;
    uint32_t r0 = row > 0 ? row - 1 : 0;
    uint32_t r1 = row + 1 < height ? row + 1 : row;
    uint32_t c0 = col > 0 ? col - 1 : 0;
    uint32_t c1 = col + 1 < width ? col + 1 : col;
    uint32_t count = 0;
    for (uint32_t r = r0; r <= r1; r++) {
        for (uint32_t c = c0; c <= c1; c++) {
            if (r != row || c != col) out[count++] = r * width + c;
        }
    }
    return count;
}

/* ======================================================================
 * ABI helpers (engine.h)
 * ====================================================================== */

#define MS_OBS_HASH_SEED 0x6d732d6f62732d31ull /* "ms-obs-1" */

static bool ms_aligned(const void *ptr) {
    return ptr != NULL && ((uintptr_t)ptr & 7u) == 0;
}

static bool ms_zero_bytes(const uint8_t *p, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (p[i] != 0) return false;
    }
    return true;
}

const char *ms_status_name(ms_status status) {
    switch (status) {
    case MS_OK: return "ok";
    case MS_ERR_INVALID_WIDTH: return "invalid_width";
    case MS_ERR_INVALID_HEIGHT: return "invalid_height";
    case MS_ERR_INVALID_MINES: return "invalid_mines";
    case MS_ERR_INVALID_ACTION: return "invalid_action";
    case MS_ERR_OUT_OF_BOUNDS: return "out_of_bounds";
    case MS_ERR_INVALID_REVISION: return "invalid_revision";
    case MS_ERR_INVALID_DEDUCTIONS: return "invalid_deductions";
    case MS_ERR_INVALID_OBSERVATION: return "invalid_observation";
    case MS_ERR_INVALID_LIMITS: return "invalid_limits";
    case MS_ERR_INVALID_RESULT: return "invalid_result";
    case MS_ERR_INVALID_BUFFER: return "invalid_buffer";
    case MS_ERR_STALE_REVISION: return "stale_revision";
    case MS_ERR_GAME_OVER: return "game_over";
    case MS_ERR_GAME_NOT_STARTED: return "game_not_started";
    case MS_ERR_INCONSISTENT: return "inconsistent_observation";
    case MS_ERR_RESOURCE_EXHAUSTED: return "resource_exhausted";
    case MS_ERR_INTERNAL: return "internal_error";
    }
    return "unknown_status";
}

const char *ms_prob_reason_name(uint32_t reason) {
    static const char *const names[] = {
        "",
        "counting_budget_exceeded",
        "sampling_budget_exhausted",
        "no_consistent_samples",
        "no_globally_compatible_samples",
        "insufficient_effective_samples",
        "time_budget_exhausted",
        "memory_budget_exhausted",
        "not_started",
        "game_over",
    };
    return reason < array_size(names) ? names[reason] : NULL;
}

ms_status ms_game_check_config(uint32_t width, uint32_t height, uint32_t mines) {
    if (width < MS_GAME_MIN_SIDE || width > MS_GAME_MAX_SIDE) return MS_ERR_INVALID_WIDTH;
    if (height < MS_GAME_MIN_SIDE || height > MS_GAME_MAX_SIDE) return MS_ERR_INVALID_HEIGHT;
    if (mines < MS_GAME_MIN_MINES || mines > width * height - MS_GAME_SAFE_START_CELLS) {
        return MS_ERR_INVALID_MINES;
    }
    return MS_OK;
}

void ms_limits_default(ms_infer_limits *limits) {
    base_memset(limits, 0, sizeof(*limits));
    limits->magic = MS_MAGIC_LIMITS;
    limits->version = MS_ABI_VERSION;
    limits->node_budget = MS_DEFAULT_NODE_BUDGET;
    limits->sample_budget = MS_DEFAULT_SAMPLE_BUDGET;
    limits->max_stored_entries = MS_DEFAULT_MAX_STORED_ENTRIES;
    limits->flags = 0;
    limits->time_budget_ms = MS_DEFAULT_TIME_BUDGET_MS;
    limits->min_effective_samples = MS_DEFAULT_MIN_EFFECTIVE_SAMPLES;
    limits->memory_budget_bytes = MS_DEFAULT_MEMORY_BUDGET;
    limits->seed = 0;
}

ms_status ms_limits_validate(const ms_infer_limits *limits) {
    if (!ms_aligned(limits)) return MS_ERR_INVALID_BUFFER;
    if (limits->magic != MS_MAGIC_LIMITS || limits->version != MS_ABI_VERSION) {
        return MS_ERR_INVALID_BUFFER;
    }
    if (limits->flags & ~(uint32_t)MS_LIMIT_KNOWN_FLAGS) return MS_ERR_INVALID_LIMITS;
    if (!(limits->time_budget_ms >= 0.0)) return MS_ERR_INVALID_LIMITS; /* NaN too */
    if (!rt_finite(limits->min_effective_samples) || limits->min_effective_samples < 0.0) {
        return MS_ERR_INVALID_LIMITS;
    }
    return MS_OK;
}

ms_status ms_obs_init(void *obs, size_t obs_len, uint32_t width, uint32_t height,
                      uint32_t total_mines) {
    if (!ms_aligned(obs)) return MS_ERR_INVALID_BUFFER;
    uint32_t cells = ms_cell_count(width, height);
    if (cells == 0) return MS_ERR_INVALID_OBSERVATION;
    if (obs_len != ms_obs_size(width, height)) return MS_ERR_INVALID_BUFFER;
    if (total_mines > cells) return MS_ERR_INVALID_OBSERVATION;
    base_memset(obs, 0, obs_len);
    ms_obs_header *h = (ms_obs_header *)obs;
    h->magic = MS_MAGIC_OBSERVATION;
    h->version = MS_ABI_VERSION;
    h->width = width;
    h->height = height;
    h->total_mines = total_mines;
    h->revealed = 0;
    base_memset(ms_obs_clues(obs), (int)MS_CLUE_HIDDEN, cells);
    return MS_OK;
}

ms_status ms_obs_set_clue(void *obs, uint32_t index, uint8_t clue) {
    ms_obs_header *h = (ms_obs_header *)obs;
    if (index >= ms_cell_count(h->width, h->height)) return MS_ERR_INVALID_OBSERVATION;
    if (clue > MS_MAX_CLUE && clue != MS_CLUE_HIDDEN) return MS_ERR_INVALID_OBSERVATION;
    uint8_t *clues = ms_obs_clues(obs);
    bool was_revealed = clues[index] != MS_CLUE_HIDDEN;
    bool now_revealed = clue != MS_CLUE_HIDDEN;
    if (was_revealed && !now_revealed) h->revealed--;
    if (!was_revealed && now_revealed) h->revealed++;
    clues[index] = clue;
    return MS_OK;
}

ms_status ms_obs_validate(const void *obs, size_t obs_len) {
    if (!ms_aligned(obs) || obs_len < sizeof(ms_obs_header)) return MS_ERR_INVALID_BUFFER;
    const ms_obs_header *h = (const ms_obs_header *)obs;
    if (h->magic != MS_MAGIC_OBSERVATION || h->version != MS_ABI_VERSION) {
        return MS_ERR_INVALID_BUFFER;
    }
    uint32_t cells = ms_cell_count(h->width, h->height);
    if (cells == 0) return MS_ERR_INVALID_OBSERVATION;
    if (obs_len != ms_obs_size(h->width, h->height)) return MS_ERR_INVALID_BUFFER;
    if (h->total_mines > cells || h->reserved0 != 0 || h->reserved1 != 0) {
        return MS_ERR_INVALID_OBSERVATION;
    }
    const uint8_t *clues = ms_obs_clues(obs);
    uint32_t revealed = 0;
    for (uint32_t i = 0; i < cells; i++) {
        if (clues[i] == MS_CLUE_HIDDEN) continue;
        if (clues[i] > MS_MAX_CLUE) return MS_ERR_INVALID_OBSERVATION;
        revealed++;
    }
    if (revealed != h->revealed) return MS_ERR_INVALID_OBSERVATION;
    size_t used = sizeof(ms_obs_header) + cells;
    if (!ms_zero_bytes((const uint8_t *)obs + used, obs_len - used)) return MS_ERR_INVALID_BUFFER;
    return MS_OK;
}

uint64_t ms_obs_hash(const void *obs) {
    const ms_obs_header *h = (const ms_obs_header *)obs;
    return rt_hash64(obs, ms_obs_size(h->width, h->height), MS_OBS_HASH_SEED);
}

ms_status ms_result_init(void *result, size_t result_len, const void *obs, size_t obs_len) {
    ms_status status = ms_obs_validate(obs, obs_len);
    if (status != MS_OK) return status;
    const ms_obs_header *o = (const ms_obs_header *)obs;
    if (!ms_aligned(result) || result_len != ms_result_size(o->width, o->height)) {
        return MS_ERR_INVALID_BUFFER;
    }
    base_memset(result, 0, result_len);
    ms_result_header *r = (ms_result_header *)result;
    r->magic = MS_MAGIC_RESULT;
    r->version = MS_ABI_VERSION;
    r->width = o->width;
    r->height = o->height;
    r->total_mines = o->total_mines;
    r->revealed = o->revealed;
    r->observation_hash = ms_obs_hash(obs);
    r->hidden_cells = o->width * o->height - o->revealed;
    return MS_OK;
}

ms_status ms_result_placeholder(void *result, size_t result_len, uint32_t width,
                                uint32_t height, uint32_t total_mines, uint32_t status) {
    if (!ms_aligned(result)) return MS_ERR_INVALID_BUFFER;
    uint32_t cells = ms_cell_count(width, height);
    if (cells == 0) return MS_ERR_INVALID_RESULT;
    if (result_len != ms_result_size(width, height)) return MS_ERR_INVALID_BUFFER;
    if (total_mines > cells) return MS_ERR_INVALID_RESULT;
    if (status != MS_PROB_NOT_STARTED && status != MS_PROB_FINISHED) return MS_ERR_INVALID_RESULT;
    base_memset(result, 0, result_len);
    ms_result_header *r = (ms_result_header *)result;
    r->magic = MS_MAGIC_RESULT;
    r->version = MS_ABI_VERSION;
    r->status = status;
    r->reason = status == MS_PROB_NOT_STARTED ? MS_REASON_NOT_STARTED : MS_REASON_GAME_OVER;
    r->width = width;
    r->height = height;
    r->total_mines = total_mines;
    return MS_OK;
}

static bool ms_result_header_ok(const ms_result_header *r, uint32_t cells) {
    if (r->status < MS_PROB_EXACT || r->status > MS_PROB_UNAVAILABLE) return false;
    if (r->reason > MS_REASON_MEMORY_BUDGET_EXHAUSTED) return false;
    if (r->hidden_cells != cells - r->revealed || r->remaining_mines > r->total_mines) return false;
    if (r->pair_reasoning_complete > 1 || r->has_effective_sample_size > 1 || r->reserved) {
        return false;
    }
    if (!rt_finite(r->elapsed_ms) || r->elapsed_ms < 0.0) return false;
    if (r->has_effective_sample_size) {
        if (!rt_finite(r->effective_sample_size) || r->effective_sample_size < 0.0) return false;
    } else if (r->effective_sample_size != 0.0) {
        return false;
    }
    return true;
}

/* Python's pairing of status, reason and meta, which the solver keeps. */
static bool ms_result_pairing_ok(const ms_result_header *r) {
    switch (r->status) {
    case MS_PROB_EXACT:
        if (r->reason != MS_REASON_NONE || r->sampled_components || r->samples ||
            r->sample_attempts || r->has_effective_sample_size) {
            return false;
        }
        break;
    case MS_PROB_APPROXIMATE:
        if (r->reason != MS_REASON_COUNTING_BUDGET_EXCEEDED || !r->sampled_components ||
            !r->samples || !r->has_effective_sample_size) {
            return false;
        }
        break;
    default:
        if (r->reason < MS_REASON_SAMPLING_BUDGET_EXHAUSTED) return false;
        break;
    }
    uint64_t parts = (uint64_t)r->frontier_cells + r->unconstrained_cells + r->propagated_cells;
    if (r->status == MS_PROB_UNAVAILABLE ? parts > r->hidden_cells : parts != r->hidden_cells) {
        return false;
    }
    if ((uint64_t)r->components != (uint64_t)r->exact_components + r->sampled_components) {
        return false;
    }
    return r->samples <= r->sample_attempts;
}

ms_status ms_result_validate(const void *obs, size_t obs_len, const void *result,
                             size_t result_len) {
    ms_status status = ms_obs_validate(obs, obs_len);
    if (status != MS_OK) return status;
    const ms_obs_header *o = (const ms_obs_header *)obs;
    if (!ms_aligned(result) || result_len < sizeof(ms_result_header)) return MS_ERR_INVALID_BUFFER;
    const ms_result_header *r = (const ms_result_header *)result;
    if (r->magic != MS_MAGIC_RESULT || r->version != MS_ABI_VERSION) return MS_ERR_INVALID_BUFFER;
    if (result_len != ms_result_size(o->width, o->height)) return MS_ERR_INVALID_BUFFER;
    if (r->width != o->width || r->height != o->height || r->total_mines != o->total_mines ||
        r->revealed != o->revealed || r->observation_hash != ms_obs_hash(obs)) {
        return MS_ERR_INVALID_RESULT;
    }
    uint32_t cells = o->width * o->height;
    if (!ms_result_header_ok(r, cells)) return MS_ERR_INVALID_RESULT;

    const uint8_t *clues = ms_obs_clues(obs);
    const double *values = ms_result_probabilities(result);
    const uint8_t *flags = ms_result_flags(result);
    uint32_t safe = 0;
    uint32_t mines = 0;
    for (uint32_t i = 0; i < cells; i++) {
        double p = values[i];
        uint8_t f = flags[i];
        if (clues[i] != MS_CLUE_HIDDEN) {
            if (f != 0 || p != 0.0) return MS_ERR_INVALID_RESULT;
            continue;
        }
        if (f & ~(uint8_t)MS_PCELL_KNOWN_BITS) return MS_ERR_INVALID_RESULT;
        bool value = (f & MS_PCELL_VALUE) != 0;
        bool proven_safe = (f & MS_PCELL_PROVEN_SAFE) != 0;
        bool proven_mine = (f & MS_PCELL_PROVEN_MINE) != 0;
        if (proven_safe && proven_mine) return MS_ERR_INVALID_RESULT;
        if (value) {
            if (!rt_finite(p) || p < 0.0 || p > 1.0) return MS_ERR_INVALID_RESULT;
        } else if (p != 0.0) {
            return MS_ERR_INVALID_RESULT;
        }
        if (proven_safe) {
            if (!value || p != 0.0) return MS_ERR_INVALID_RESULT;
            safe++;
        }
        if (proven_mine) {
            if (!value || p != 1.0) return MS_ERR_INVALID_RESULT;
            mines++;
        }
        if (r->status == MS_PROB_UNAVAILABLE) {
            if (value != (proven_safe || proven_mine)) return MS_ERR_INVALID_RESULT;
        } else if (!value) {
            return MS_ERR_INVALID_RESULT;
        } else if (r->status == MS_PROB_EXACT &&
                   ((p == 0.0) != proven_safe || (p == 1.0) != proven_mine)) {
            return MS_ERR_INVALID_RESULT;
        }
    }
    if (safe != r->proven_safe || mines != r->proven_mines) return MS_ERR_INVALID_RESULT;
    size_t used = sizeof(ms_result_header) + (size_t)cells * 9u;
    if (!ms_zero_bytes((const uint8_t *)result + used, result_len - used)) {
        return MS_ERR_INVALID_BUFFER;
    }
    return MS_OK;
}

ms_status ms_result_validate_solver(const void *obs, size_t obs_len, const void *result,
                                    size_t result_len) {
    ms_status status = ms_result_validate(obs, obs_len, result, result_len);
    if (status != MS_OK) return status;
    return ms_result_pairing_ok((const ms_result_header *)result) ? MS_OK : MS_ERR_INVALID_RESULT;
}

ms_status ms_view_validate(const void *view, size_t view_len) {
    if (!ms_aligned(view) || view_len < sizeof(ms_view_header)) return MS_ERR_INVALID_BUFFER;
    const ms_view_header *h = (const ms_view_header *)view;
    if (h->magic != MS_MAGIC_VIEW || h->version != MS_ABI_VERSION) return MS_ERR_INVALID_BUFFER;
    if (ms_game_check_config(h->width, h->height, h->mines) != MS_OK) return MS_ERR_INTERNAL;
    if (view_len != ms_view_size(h->width, h->height)) return MS_ERR_INVALID_BUFFER;
    uint32_t status = h->status;
    if (h->generation == 0 || h->generation > MS_GENERATION_MAX ||
        h->revision > MS_REVISION_MAX || status < MS_GAME_READY || status > MS_GAME_LOST) {
        return MS_ERR_INTERNAL;
    }
    bool terminal = status == MS_GAME_WON || status == MS_GAME_LOST;
    if (!rt_finite(h->elapsed_ms) || h->elapsed_ms < 0.0) return MS_ERR_INTERNAL;
    if (status == MS_GAME_READY && h->elapsed_ms != 0.0) return MS_ERR_INTERNAL;

    const uint8_t *cells = ms_view_cells(view);
    uint32_t count = h->width * h->height;
    uint32_t flagged = 0, revealed = 0, mines = 0, exploded = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t b = cells[i];
        uint32_t adjacent = b & MS_CELL_ADJACENT_MASK;
        bool is_revealed = (b & MS_CELL_REVEALED) != 0;
        bool is_flagged = (b & MS_CELL_FLAGGED) != 0;
        bool is_mine = (b & MS_CELL_MINE) != 0;
        bool is_exploded = (b & MS_CELL_EXPLODED) != 0;
        if (is_revealed && is_flagged) return MS_ERR_INTERNAL;
        if (is_mine && !terminal) return MS_ERR_INTERNAL;
        if (is_exploded != (is_revealed && is_mine)) return MS_ERR_INTERNAL;
        if (is_exploded && status != MS_GAME_LOST) return MS_ERR_INTERNAL;
        if (status == MS_GAME_READY && is_revealed) return MS_ERR_INTERNAL;
        if (status == MS_GAME_WON &&
            (is_mine ? !is_flagged || is_revealed : !is_revealed)) {
            return MS_ERR_INTERNAL;
        }
        if (is_revealed && !is_mine) {
            if (adjacent > MS_MAX_CLUE) return MS_ERR_INTERNAL;
            if (terminal) {
                uint32_t around[8];
                uint32_t n = rt_grid_neighbors(h->width, h->height, i, around);
                uint32_t nearby = 0;
                for (uint32_t k = 0; k < n; k++) nearby += (cells[around[k]] & MS_CELL_MINE) != 0;
                if (nearby != adjacent) return MS_ERR_INTERNAL;
            }
        } else if (adjacent != MS_CELL_NO_ADJACENT) {
            return MS_ERR_INTERNAL;
        }
        flagged += is_flagged;
        revealed += is_revealed;
        mines += is_mine;
        exploded += is_exploded;
    }
    if (flagged != h->flags || revealed != h->revealed) return MS_ERR_INTERNAL;
    if (terminal && mines != h->mines) return MS_ERR_INTERNAL;
    if (status == MS_GAME_LOST && exploded == 0) return MS_ERR_INTERNAL;
    if (status == MS_GAME_PLAYING && (revealed == 0 || revealed >= count - h->mines)) {
        return MS_ERR_INTERNAL;
    }
    size_t used = sizeof(ms_view_header) + count;
    if (!ms_zero_bytes((const uint8_t *)view + used, view_len - used)) return MS_ERR_INVALID_BUFFER;
    return MS_OK;
}
