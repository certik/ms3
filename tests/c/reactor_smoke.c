/*
 * Test-only WebAssembly reactor, built by `scripts/build.mjs smoke` with
 * PLATFORM_SKIP_ENTRY and --no-entry and driven by scripts/check-wasm.mjs
 * through corec's platform/js/wasi.js. It proves the conventions the
 * production c/wasm_api.c follows (engine.h, "Reactor ABI rules"):
 * host-managed one-time platform_init, explicit exports, 16-byte aligned
 * budgeted buffers, memory growth, the ms_host clock import, proc_exit
 * propagation, ABI buffers marshalled through linear memory and host
 * pointers refused by ownership checks (rt_mem_capacity) instead of traps.
 * It is not part of the shipped application.
 */

#include <platform/platform.h>

#include "engine.h"
#include "runtime.h"

#define SMOKE_EXPORT(name) __attribute__((export_name(#name))) name

__attribute__((import_module("ms_host"), import_name("now_ms"))) double ms_host_now_ms(void);

static bool initialized;
static rt_mem buffers;
static rt_clock host_clock;

static double read_host_clock(void *ctx) {
    (void)ctx;
    return ms_host_now_ms();
}

static void *at(uint32_t ptr) {
    return (void *)(uintptr_t)ptr;
}

/* [ptr, ptr + len) lies inside one live buffer whose start is `ptr`. Every
 * pointer-taking export checks this first: rt_mem_capacity proves ownership
 * without reading memory, so no host pointer value can trap. */
static bool in_buffer(uint32_t ptr, size_t len) {
    size_t capacity = rt_mem_capacity(&buffers, at(ptr));
    return capacity != 0 && len <= capacity;
}

uint32_t SMOKE_EXPORT(smoke_abi_version)(void) {
    return MS_ABI_VERSION;
}

int32_t SMOKE_EXPORT(smoke_init)(void) {
    if (initialized) return MS_ERR_INTERNAL;
    platform_init(0, NULL, NULL);
    rt_mem_init(&buffers, (size_t)64 << 20);
    rt_clock_init(&host_clock, read_host_clock, NULL);
    initialized = true;
    return MS_OK;
}

uint32_t SMOKE_EXPORT(smoke_alloc)(uint32_t size) {
    if (!initialized) return 0;
    return (uint32_t)(uintptr_t)rt_alloc(&buffers, size);
}

int32_t SMOKE_EXPORT(smoke_free)(uint32_t ptr) {
    if (!initialized) return MS_ERR_INTERNAL;
    uint64_t misuses = buffers.misuses;
    rt_free(&buffers, at(ptr));
    return buffers.misuses != misuses ? MS_ERR_INTERNAL : MS_OK;
}

/* Usable bytes of the live buffer starting at `ptr`, else 0 (also before
 * smoke_init: the context is then uninitialized). */
uint32_t SMOKE_EXPORT(smoke_capacity)(uint32_t ptr) {
    return (uint32_t)rt_mem_capacity(&buffers, at(ptr));
}

uint32_t SMOKE_EXPORT(smoke_live_bytes)(void) {
    return initialized ? (uint32_t)buffers.live : 0;
}

/* 0 when the range is not inside one buffer. */
uint64_t SMOKE_EXPORT(smoke_hash)(uint32_t ptr, uint32_t len, uint64_t seed) {
    if (!in_buffer(ptr, len)) return 0;
    return rt_hash64(at(ptr), len, seed);
}

/* The output after skipping `skip` draws from rt_rng_seed(seed). */
uint64_t SMOKE_EXPORT(smoke_rng)(uint64_t seed, uint32_t skip) {
    rt_rng rng;
    rt_rng_seed(&rng, seed);
    for (uint32_t i = 0; i < skip; i++) rt_rng_next(&rng);
    return rt_rng_next(&rng);
}

/* 0 when `out` is not a buffer holding 8 indices (a valid cell of a board
 * of at least 2 cells always has a neighbor). */
uint32_t SMOKE_EXPORT(smoke_neighbors)(uint32_t width, uint32_t height, uint32_t index,
                                       uint32_t out) {
    if (!in_buffer(out, 8 * sizeof(uint32_t))) return 0;
    return rt_grid_neighbors(width, height, index, (uint32_t *)at(out));
}

double SMOKE_EXPORT(smoke_clock_ms)(void) {
    return initialized ? rt_clock_now(&host_clock) : -1.0;
}

/* The ABI validators below answer MS_ERR_INVALID_BUFFER for any range that
 * is not inside one buffer, before C reads it. */
int32_t SMOKE_EXPORT(smoke_obs_validate)(uint32_t obs, uint32_t obs_len) {
    if (!in_buffer(obs, obs_len)) return MS_ERR_INVALID_BUFFER;
    return ms_obs_validate(at(obs), obs_len);
}

int32_t SMOKE_EXPORT(smoke_result_init)(uint32_t result, uint32_t result_len, uint32_t obs,
                                        uint32_t obs_len) {
    if (!in_buffer(result, result_len) || !in_buffer(obs, obs_len)) return MS_ERR_INVALID_BUFFER;
    return ms_result_init(at(result), result_len, at(obs), obs_len);
}

int32_t SMOKE_EXPORT(smoke_result_validate)(uint32_t obs, uint32_t obs_len, uint32_t result,
                                            uint32_t result_len) {
    if (!in_buffer(obs, obs_len) || !in_buffer(result, result_len)) return MS_ERR_INVALID_BUFFER;
    return ms_result_validate(at(obs), obs_len, at(result), result_len);
}

int32_t SMOKE_EXPORT(smoke_view_validate)(uint32_t view, uint32_t view_len) {
    if (!in_buffer(view, view_len)) return MS_ERR_INVALID_BUFFER;
    return ms_view_validate(at(view), view_len);
}

int32_t SMOKE_EXPORT(smoke_limits_validate)(uint32_t limits) {
    if (!in_buffer(limits, sizeof(ms_infer_limits))) return MS_ERR_INVALID_BUFFER;
    return ms_limits_validate((const ms_infer_limits *)at(limits));
}

void SMOKE_EXPORT(smoke_exit)(int32_t status) {
    platform_exit(status);
}
