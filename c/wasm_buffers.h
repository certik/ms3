#pragma once

/*
 * wasm_buffers.h - the reactor's host-buffer table (internal, header-only).
 *
 * c/wasm_api.c resolves every pointer/length pair from JavaScript through
 * these functions, and tests/c/test_api.c runs the very same code natively,
 * so the WebAssembly boundary rules are unit-tested on every platform.
 *
 * Host buffers are rt_calloc blocks of a dedicated rt_mem (never the engine's
 * or the solver's). A pointer is accepted only if [ptr, ptr + len) lies
 * inside the bytes requested for one registered buffer (interior pointers
 * allowed) AND the allocator's pure ownership query (rt_mem_capacity) still
 * confirms that buffer. Resolution compares addresses only - caller memory
 * is never read - with overflow-checked uintptr_t/size_t arithmetic, so a
 * wild pointer (0, 1, beyond the address space or linear memory), a freed or
 * foreign buffer, or an over-long range is refused without trapping. Calls
 * that take several buffers also require them to be pairwise disjoint: the
 * engine and solver write outputs before or while reading inputs (ms_solve
 * zeroes and fills the whole result while still reading the observation),
 * so aliasing is rejected, never given an in-place meaning. Nothing here
 * writes caller memory.
 */

#include "runtime.h"
#include "wasm_api.h"

_Static_assert(sizeof(size_t) == sizeof(uintptr_t), "host ranges use one width");

typedef struct ms_host_buffer {
    uintptr_t start; /* pointer returned by rt_calloc */
    size_t size;     /* bytes requested (zero-filled); the usable range */
} ms_host_buffer;

typedef struct ms_host_table {
    rt_mem *mem; /* host buffers only */
    uint32_t count;
    ms_host_buffer items[MS_WASM_MAX_BUFFERS];
} ms_host_table;

static inline void ms_host_init(ms_host_table *table, rt_mem *mem) {
    table->mem = mem;
    table->count = 0;
}

/* A nonempty range [ptr, ptr + len) that does not start at 0 or wrap. */
static inline bool ms_host_range_ok(uintptr_t ptr, size_t len) {
    return ptr != 0 && len != 0 && (uintptr_t)len <= MS_UINTPTR_MAX - ptr;
}

/* Two valid ranges (ms_host_range_ok) share no byte; adjacent is fine. */
static inline bool ms_host_disjoint(uintptr_t a, size_t a_len, uintptr_t b, size_t b_len) {
    return a + (uintptr_t)a_len <= b || b + (uintptr_t)b_len <= a;
}

/* A zero-filled, 16-byte aligned host buffer, or 0 (size 0 or above
 * MS_WASM_MAX_BUFFER_BYTES, MS_WASM_MAX_BUFFERS live, budget exhausted). */
static inline uintptr_t ms_host_alloc(ms_host_table *table, size_t size) {
    if (size == 0 || size > MS_WASM_MAX_BUFFER_BYTES || table->count >= MS_WASM_MAX_BUFFERS) {
        return 0;
    }
    void *ptr = rt_calloc(table->mem, 1, size);
    if (ptr == NULL) return 0;
    table->items[table->count].start = (uintptr_t)ptr;
    table->items[table->count].size = size;
    table->count++;
    return (uintptr_t)ptr;
}

/* Whether a registered buffer is still a live allocation of the table's
 * rt_mem with room for its requested size (pure). */
static inline bool ms_host_owned(const ms_host_table *table, const ms_host_buffer *buffer) {
    return rt_mem_capacity(table->mem, (const void *)buffer->start) >= buffer->size;
}

/* The native pointer for [ptr, ptr + len) when it is aligned to `align` (a
 * power of two) and lies inside one owned buffer's requested bytes; NULL
 * otherwise. */
static inline void *ms_host_span(const ms_host_table *table, uintptr_t ptr, size_t len,
                                 size_t align) {
    if (!ms_host_range_ok(ptr, len) || (ptr & (uintptr_t)(align - 1u)) != 0) return NULL;
    for (uint32_t i = 0; i < table->count; i++) {
        const ms_host_buffer *buffer = &table->items[i];
        if (ptr < buffer->start) continue;
        uintptr_t offset = ptr - buffer->start;
        if (offset > buffer->size || (uintptr_t)len > buffer->size - offset) continue;
        return ms_host_owned(table, buffer) ? (void *)ptr : NULL;
    }
    return NULL;
}

/* Frees the buffer that starts at `ptr`. 0 is a no-op; anything else that
 * is not a registered start is MS_ERR_INVALID_BUFFER and changes nothing; a
 * registered buffer the allocator no longer confirms is MS_ERR_INTERNAL. */
static inline ms_status ms_host_free(ms_host_table *table, uintptr_t ptr) {
    if (ptr == 0) return MS_OK;
    for (uint32_t i = 0; i < table->count; i++) {
        if (table->items[i].start != ptr) continue;
        if (!ms_host_owned(table, &table->items[i])) return MS_ERR_INTERNAL;
        rt_free(table->mem, (void *)ptr);
        uint32_t last = --table->count;
        table->items[i].start = table->items[last].start;
        table->items[i].size = table->items[last].size;
        return MS_OK;
    }
    return MS_ERR_INVALID_BUFFER;
}

/* Two spans of one call (e.g. a result and its availability flag): both
 * valid and disjoint, else MS_ERR_INVALID_BUFFER with the outputs pa and pb
 * untouched. */
static inline ms_status ms_host_span2(const ms_host_table *table, uintptr_t a, size_t a_len,
                                      size_t a_align, uintptr_t b, size_t b_len, size_t b_align,
                                      void **pa, void **pb) {
    void *span_a = ms_host_span(table, a, a_len, a_align);
    void *span_b = ms_host_span(table, b, b_len, b_align);
    if (span_a == NULL || span_b == NULL || !ms_host_disjoint(a, a_len, b, b_len)) {
        return MS_ERR_INVALID_BUFFER;
    }
    *pa = span_a;
    *pb = span_b;
    return MS_OK;
}

/* The solver request: observation, limits and result each valid, 8-byte
 * aligned and pairwise disjoint (limits_len must be MS_WASM_LIMITS_BYTES),
 * else MS_ERR_INVALID_BUFFER with the outputs untouched - checked before
 * the solver initializes its result. */
static inline ms_status ms_host_solve_spans(const ms_host_table *table, uintptr_t obs,
                                            size_t obs_len, uintptr_t limits, size_t limits_len,
                                            uintptr_t result, size_t result_len, void **obs_out,
                                            void **limits_out, void **result_out) {
    if (limits_len != MS_WASM_LIMITS_BYTES) return MS_ERR_INVALID_BUFFER;
    void *o = ms_host_span(table, obs, obs_len, 8);
    void *l = ms_host_span(table, limits, limits_len, 8);
    void *r = ms_host_span(table, result, result_len, 8);
    if (o == NULL || l == NULL || r == NULL || !ms_host_disjoint(obs, obs_len, limits, limits_len) ||
        !ms_host_disjoint(result, result_len, obs, obs_len) ||
        !ms_host_disjoint(result, result_len, limits, limits_len)) {
        return MS_ERR_INVALID_BUFFER;
    }
    *obs_out = o;
    *limits_out = l;
    *result_out = r;
    return MS_OK;
}
