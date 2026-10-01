/*
 * wasm_api.c - the production WebAssembly reactor: the exports of
 * wasm_api.h around the engine service (engine.c) and the solver
 * (ms_solve). Built for wasm32 only, with PLATFORM_SKIP_ENTRY and
 * --no-entry; JS calls ms_init exactly once per instance.
 *
 * Host buffers live in their own rt_mem (buffer_mem), separate from the
 * engine's games/results (engine_mem) and the solver workspace
 * (solver_mem). Every pointer from JS is resolved through the host-buffer
 * table of wasm_buffers.h before anything else happens and without reading
 * caller memory (registered requested bytes, confirmed by the allocator's
 * pure rt_mem_capacity; overflow-checked ranges; buffers of one call
 * pairwise disjoint). So a wild, freed, foreign, engine or aliased pointer
 * can neither trap nor reach private state, and outputs are written only
 * after the engine call succeeded.
 */

#include "wasm_api.h"

#include <platform/platform.h>

#include "runtime.h"
#include "wasm_buffers.h"

__attribute__((import_module("ms_host"), import_name("now_ms"))) double ms_host_now_ms(void);

static bool initialized;
static rt_mem buffer_mem; /* ms_alloc buffers */
static rt_mem engine_mem; /* the engine's games and stored result */
static rt_mem solver_mem; /* ms_solve workspace, reset after every call */
static rt_clock host_clock;
static ms_engine engine;
static ms_host_table host;

static double read_host_clock(void *ctx) {
    (void)ctx;
    return ms_host_now_ms();
}

static void *span(uint32_t ptr, uint32_t len, uint32_t align) {
    return ms_host_span(&host, ptr, len, align);
}

/* ======================================================================
 * Lifecycle, buffers and names
 * ====================================================================== */

uint32_t MS_WASM_EXPORT(ms_abi_version)(void) {
    return MS_ABI_VERSION;
}

int32_t MS_WASM_EXPORT(ms_init)(void) {
    if (initialized) return MS_ERR_INTERNAL;
    platform_init(0, NULL, NULL);
    rt_mem_init(&buffer_mem, MS_WASM_BUFFER_BUDGET);
    rt_mem_init(&engine_mem, MS_WASM_ENGINE_BUDGET);
    rt_mem_init(&solver_mem, MS_WASM_SOLVER_BUDGET);
    rt_clock_init(&host_clock, read_host_clock, NULL);
    ms_status status = ms_engine_init(&engine, &engine_mem, &host_clock);
    if (status != MS_OK) return status;
    ms_host_init(&host, &buffer_mem);
    initialized = true;
    return MS_OK;
}

uint32_t MS_WASM_EXPORT(ms_alloc)(uint32_t size) {
    if (!initialized) return 0;
    return (uint32_t)ms_host_alloc(&host, size);
}

int32_t MS_WASM_EXPORT(ms_free)(uint32_t ptr) {
    if (!initialized) return MS_ERR_INTERNAL;
    return ms_host_free(&host, ptr);
}

uint32_t MS_WASM_EXPORT(ms_view_bytes)(uint32_t width, uint32_t height) {
    return (uint32_t)ms_view_size(width, height);
}

uint32_t MS_WASM_EXPORT(ms_obs_bytes)(uint32_t width, uint32_t height) {
    return (uint32_t)ms_obs_size(width, height);
}

uint32_t MS_WASM_EXPORT(ms_result_bytes)(uint32_t width, uint32_t height) {
    return (uint32_t)ms_result_size(width, height);
}

uint32_t MS_WASM_EXPORT(ms_status_text)(int32_t status) {
    return (uint32_t)(uintptr_t)ms_status_name((ms_status)status);
}

uint32_t MS_WASM_EXPORT(ms_reason_text)(uint32_t reason) {
    return (uint32_t)(uintptr_t)ms_prob_reason_name(reason);
}

uint32_t MS_WASM_EXPORT(ms_live_bytes)(uint32_t pool) {
    if (!initialized) return 0;
    switch (pool) {
    case MS_WASM_POOL_BUFFERS: return (uint32_t)buffer_mem.live;
    case MS_WASM_POOL_ENGINE: return (uint32_t)engine_mem.live;
    case MS_WASM_POOL_SOLVER: return (uint32_t)solver_mem.live;
    default: return 0;
    }
}

/* ======================================================================
 * Game instance
 * ====================================================================== */

int32_t MS_WASM_EXPORT(ms_new_game)(uint32_t width, uint32_t height, uint32_t mines,
                                    uint32_t seed_lo, uint32_t seed_hi,
                                    uint32_t generation_out) {
    if (!initialized) return MS_ERR_INTERNAL;
    uint32_t *out = (uint32_t *)span(generation_out, 4, 4);
    if (out == NULL) return MS_ERR_INVALID_BUFFER;
    uint32_t generation = 0;
    uint64_t seed = ((uint64_t)seed_hi << 32) | seed_lo;
    ms_status status = ms_engine_new_game(&engine, width, height, mines, seed, &generation);
    if (status == MS_OK) *out = generation;
    return status;
}

int32_t MS_WASM_EXPORT(ms_act)(uint32_t generation, uint32_t revision, uint32_t action,
                               uint32_t row, uint32_t col, uint32_t changed_out) {
    if (!initialized) return MS_ERR_INTERNAL;
    uint32_t *out = (uint32_t *)span(changed_out, 4, 4);
    if (out == NULL) return MS_ERR_INVALID_BUFFER;
    bool changed = false;
    ms_status status = ms_engine_act(&engine, generation, revision, action, row, col, &changed);
    if (status == MS_OK) *out = changed ? 1u : 0u;
    return status;
}

int32_t MS_WASM_EXPORT(ms_get_view)(uint32_t generation, uint32_t view, uint32_t view_len) {
    if (!initialized) return MS_ERR_INTERNAL;
    void *out = span(view, view_len, 8);
    if (out == NULL) return MS_ERR_INVALID_BUFFER;
    return ms_engine_view(&engine, generation, out, view_len);
}

int32_t MS_WASM_EXPORT(ms_get_observation)(uint32_t generation, uint32_t revision,
                                           uint32_t obs, uint32_t obs_len) {
    if (!initialized) return MS_ERR_INTERNAL;
    void *out = span(obs, obs_len, 8);
    if (out == NULL) return MS_ERR_INVALID_BUFFER;
    return ms_engine_observe(&engine, generation, revision, out, obs_len);
}

int32_t MS_WASM_EXPORT(ms_get_cached_result)(uint32_t generation, uint32_t revision,
                                             uint32_t result, uint32_t result_len,
                                             uint32_t available_out) {
    if (!initialized) return MS_ERR_INTERNAL;
    void *out = NULL, *flag = NULL;
    if (ms_host_span2(&host, result, result_len, 8, available_out, 4, 4, &out, &flag) != MS_OK) {
        return MS_ERR_INVALID_BUFFER;
    }
    bool available = false;
    ms_status status =
        ms_engine_cached_result(&engine, generation, revision, out, result_len, &available);
    if (status == MS_OK) *(uint32_t *)flag = available ? 1u : 0u;
    return status;
}

int32_t MS_WASM_EXPORT(ms_accept_result)(uint32_t generation, uint32_t revision,
                                         uint32_t result, uint32_t result_len) {
    if (!initialized) return MS_ERR_INTERNAL;
    const void *in = span(result, result_len, 8);
    if (in == NULL) return MS_ERR_INVALID_BUFFER;
    return ms_engine_accept_result(&engine, generation, revision, in, result_len);
}

int32_t MS_WASM_EXPORT(ms_apply_autosolve)(uint32_t generation, uint32_t revision,
                                           uint32_t available_out, uint32_t changed_out) {
    if (!initialized) return MS_ERR_INTERNAL;
    void *available_flag = NULL, *changed_flag = NULL;
    if (ms_host_span2(&host, available_out, 4, 4, changed_out, 4, 4, &available_flag,
                      &changed_flag) != MS_OK) {
        return MS_ERR_INVALID_BUFFER;
    }
    bool available = false, changed = false;
    ms_status status = ms_engine_autosolve(&engine, generation, revision, &available, &changed);
    if (status == MS_OK) {
        *(uint32_t *)available_flag = available ? 1u : 0u;
        *(uint32_t *)changed_flag = changed ? 1u : 0u;
    }
    return status;
}

/* ======================================================================
 * Solver instance
 * ====================================================================== */

int32_t MS_WASM_EXPORT(ms_init_default_limits)(uint32_t limits, uint32_t limits_len) {
    if (!initialized) return MS_ERR_INTERNAL;
    void *out = span(limits, limits_len, 8);
    if (out == NULL || limits_len != MS_WASM_LIMITS_BYTES) return MS_ERR_INVALID_BUFFER;
    ms_limits_default((ms_infer_limits *)out);
    return MS_OK;
}

int32_t MS_WASM_EXPORT(ms_solve_observation)(uint32_t obs, uint32_t obs_len, uint32_t limits,
                                             uint32_t limits_len, uint32_t result,
                                             uint32_t result_len) {
    if (!initialized) return MS_ERR_INTERNAL;
    /* All three ranges owned, aligned and pairwise disjoint before the
     * solver may initialize (zero) its result: an aliased request would
     * corrupt its own input, so it is refused, not given in-place meaning. */
    void *in = NULL, *policy = NULL, *out = NULL;
    if (ms_host_solve_spans(&host, obs, obs_len, limits, limits_len, result, result_len, &in,
                            &policy, &out) != MS_OK) {
        return MS_ERR_INVALID_BUFFER;
    }
    ms_status status = ms_solve(in, obs_len, (const ms_infer_limits *)policy, &solver_mem,
                                &host_clock, out, result_len);
    /* ms_solve releases its workspace; the reset also clears the sticky
     * error so every solve starts from an empty, clean context. */
    rt_mem_reset(&solver_mem);
    return status;
}
