/*
 * engine.c - the per-tab engine service (replaces server.py's GameEntry and
 * ProbabilityService for one browser tab): the current game, its
 * generation, the result accepted for its current revision and atomic
 * certainty-only autosolve. See engine.h ("Engine service API").
 *
 * The service never solves: the solver runs in another instance and its
 * results are only trusted after ms_result_validate against a freshly built
 * observation of the current (generation, revision). All storage of a game
 * is allocated by ms_engine_new_game, so every other operation validates
 * and then cannot fail half-way.
 */

#include "engine.h"

#include <base/mem.h>

#include "game.h"
#include "runtime.h"

#define ENGINE_LIVE 0x4E474E45u     /* "ENGN" */
#define ENGINE_DISPOSED 0x44474E45u /* "ENGD" */

static bool engine_ok(const ms_engine *engine) {
    return engine != NULL && engine->state == ENGINE_LIVE;
}

static bool aligned8(const void *ptr) {
    return ptr != NULL && ((uintptr_t)ptr & 7u) == 0;
}

/* Whether [a, a + a_len) and [b, b + b_len) share a byte; a range that
 * would wrap the address space counts as reaching its top. */
static bool ranges_overlap(const void *a, size_t a_len, const void *b, size_t b_len) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (a == NULL || b == NULL || a_len == 0 || b_len == 0) return false;
    uintptr_t x_end = (uintptr_t)a_len > MS_UINTPTR_MAX - x ? MS_UINTPTR_MAX : x + (uintptr_t)a_len;
    uintptr_t y_end = (uintptr_t)b_len > MS_UINTPTR_MAX - y ? MS_UINTPTR_MAX : y + (uintptr_t)b_len;
    return x < y_end && y < x_end;
}

/* A caller buffer that is NULL, misaligned or aliases engine storage. */
static bool bad_buffer(const ms_engine *engine, const void *ptr, size_t len) {
    return !aligned8(ptr) || ranges_overlap(ptr, len, engine->storage, engine->storage_len);
}

static bool is_over(uint32_t status) {
    return status == MS_GAME_WON || status == MS_GAME_LOST;
}

ms_status ms_engine_init(ms_engine *engine, rt_mem *mem, rt_clock *clock) {
    if (engine == NULL) return MS_ERR_INVALID_BUFFER;
    base_memset(engine, 0, sizeof(*engine));
    if (mem == NULL || clock == NULL) return MS_ERR_INVALID_BUFFER;
    engine->mem = mem;
    engine->clock = clock;
    engine->state = ENGINE_LIVE;
    return MS_OK;
}

void ms_engine_dispose(ms_engine *engine) {
    if (!engine_ok(engine)) return;
    ms_game_destroy(engine->game);
    rt_free(engine->mem, engine->storage);
    base_memset(engine, 0, sizeof(*engine));
    engine->state = ENGINE_DISPOSED;
}

/* ======================================================================
 * Games
 * ====================================================================== */

static ms_status new_game(ms_engine *engine, uint32_t width, uint32_t height, uint32_t mines,
                          uint64_t seed, bool with_layout, const uint32_t *layout,
                          uint32_t layout_count, uint32_t *generation) {
    if (!engine_ok(engine)) return MS_ERR_INTERNAL;
    ms_status status = ms_game_check_config(width, height, mines);
    if (status != MS_OK) return status;
    if (engine->generation >= MS_GENERATION_MAX) return MS_ERR_RESOURCE_EXHAUSTED;
    uint32_t next = engine->generation + 1;

    /* One block: stored result, observation scratch, two proof lists. The
     * first two sizes are multiples of 8, so every part stays aligned. */
    uint32_t cells = width * height;
    size_t result_len = ms_result_size(width, height);
    size_t obs_len = ms_obs_size(width, height);
    size_t lists = (size_t)cells * sizeof(uint32_t) * 2u;
    uint8_t *storage = (uint8_t *)rt_alloc(engine->mem, result_len + obs_len + lists);
    if (storage == NULL) return MS_ERR_RESOURCE_EXHAUSTED;
    ms_game *game = NULL;
    if (with_layout) {
        status = ms_game_create_with_layout(&game, engine->mem, engine->clock, width, height,
                                            mines, next, layout, layout_count);
    } else {
        status = ms_game_create(&game, engine->mem, engine->clock, width, height, mines, next,
                                seed);
    }
    if (status != MS_OK) {
        rt_free(engine->mem, storage);
        return status;
    }

    /* Everything is allocated: only now drop the old game and its result. */
    ms_game_destroy(engine->game);
    rt_free(engine->mem, engine->storage);
    engine->game = game;
    engine->generation = next;
    engine->width = width;
    engine->height = height;
    engine->mines = mines;
    engine->stored = 0;
    engine->stored_revision = 0;
    engine->stored_status = 0;
    engine->storage = storage;
    engine->storage_len = result_len + obs_len + lists;
    engine->result = storage;
    engine->result_len = result_len;
    engine->obs = storage + result_len;
    engine->obs_len = obs_len;
    engine->proof_safe = (uint32_t *)(storage + result_len + obs_len);
    engine->proof_mines = engine->proof_safe + cells;
    if (generation) *generation = next;
    return MS_OK;
}

ms_status ms_engine_new_game(ms_engine *engine, uint32_t width, uint32_t height,
                             uint32_t mines, uint64_t seed, uint32_t *generation) {
    return new_game(engine, width, height, mines, seed, false, NULL, 0, generation);
}

ms_status ms_engine_new_game_with_layout(ms_engine *engine, uint32_t width, uint32_t height,
                                         uint32_t mines, const uint32_t *layout,
                                         uint32_t layout_count, uint32_t *generation) {
    return new_game(engine, width, height, mines, 0, true, layout, layout_count, generation);
}

static ms_status check_generation(const ms_engine *engine, uint32_t generation) {
    if (generation == 0 || generation > MS_GENERATION_MAX) return MS_ERR_INVALID_REVISION;
    if (engine->game == NULL || generation != engine->generation) return MS_ERR_STALE_REVISION;
    return MS_OK;
}

static ms_status check_current(const ms_engine *engine, uint32_t generation, uint32_t revision) {
    if (generation == 0 || generation > MS_GENERATION_MAX || revision > MS_REVISION_MAX) {
        return MS_ERR_INVALID_REVISION;
    }
    if (engine->game == NULL || generation != engine->generation ||
        revision != ms_game_get_revision(engine->game)) {
        return MS_ERR_STALE_REVISION;
    }
    return MS_OK;
}

/* READY -> not started, won/lost -> game over, PLAYING -> MS_OK. */
static ms_status check_playing(const ms_engine *engine) {
    uint32_t status = ms_game_get_status(engine->game);
    if (status == MS_GAME_READY) return MS_ERR_GAME_NOT_STARTED;
    if (is_over(status)) return MS_ERR_GAME_OVER;
    return MS_OK;
}

ms_status ms_engine_act(ms_engine *engine, uint32_t generation, uint32_t revision,
                        uint32_t action, uint32_t row, uint32_t col, bool *changed) {
    if (!engine_ok(engine)) return MS_ERR_INTERNAL;
    if (engine->game == NULL) {
        if (action < MS_ACTION_REVEAL || action > MS_ACTION_CHORD) return MS_ERR_INVALID_ACTION;
        return check_current(engine, generation, revision);
    }
    bool did = false;
    ms_status status = ms_game_act(engine->game, generation, revision, action, row, col, &did);
    if (status != MS_OK) return status;
    if (did) engine->stored = 0;
    if (changed) *changed = did;
    return MS_OK;
}

ms_status ms_engine_view(ms_engine *engine, uint32_t generation, void *view, size_t view_len) {
    if (!engine_ok(engine)) return MS_ERR_INTERNAL;
    if (bad_buffer(engine, view, view_len)) return MS_ERR_INVALID_BUFFER;
    ms_status status = check_generation(engine, generation);
    if (status != MS_OK) return status;
    return ms_game_view(engine->game, view, view_len);
}

ms_status ms_engine_observe(ms_engine *engine, uint32_t generation, uint32_t revision,
                            void *obs, size_t obs_len) {
    if (!engine_ok(engine)) return MS_ERR_INTERNAL;
    if (bad_buffer(engine, obs, obs_len)) return MS_ERR_INVALID_BUFFER;
    ms_status status = check_current(engine, generation, revision);
    if (status != MS_OK) return status;
    if (obs_len != engine->obs_len) return MS_ERR_INVALID_BUFFER;
    return ms_game_observe(engine->game, generation, revision, obs, obs_len);
}

/* ======================================================================
 * Results and autosolve
 * ====================================================================== */

static bool has_result(const ms_engine *engine, uint32_t revision) {
    return engine->stored && engine->stored_revision == revision;
}

ms_status ms_engine_cached_result(ms_engine *engine, uint32_t generation, uint32_t revision,
                                  void *result, size_t result_len, bool *available) {
    if (!engine_ok(engine)) return MS_ERR_INTERNAL;
    if (available == NULL || bad_buffer(engine, result, result_len) ||
        ranges_overlap(available, sizeof(*available), result, result_len) ||
        ranges_overlap(available, sizeof(*available), engine->storage, engine->storage_len)) {
        return MS_ERR_INVALID_BUFFER;
    }
    ms_status status = check_current(engine, generation, revision);
    if (status != MS_OK) return status;
    if (result_len != engine->result_len) return MS_ERR_INVALID_BUFFER;

    uint32_t game_status = ms_game_get_status(engine->game);
    if (game_status == MS_GAME_READY || is_over(game_status)) {
        uint32_t placeholder = game_status == MS_GAME_READY ? MS_PROB_NOT_STARTED : MS_PROB_FINISHED;
        status = ms_result_placeholder(result, result_len, engine->width, engine->height,
                                       engine->mines, placeholder);
        if (status != MS_OK) return status;
        *available = true;
        return MS_OK;
    }
    bool reusable = has_result(engine, revision) && engine->stored_status != MS_PROB_UNAVAILABLE;
    if (reusable) base_memcpy(result, engine->result, result_len);
    *available = reusable;
    return MS_OK;
}

/* Count fields must be non-negative as signed 32-bit numbers. */
static bool counts_ok(const ms_result_header *r) {
    uint32_t any = r->frontier_cells | r->components | r->unconstrained_cells | r->samples |
                   r->exact_components | r->sampled_components | r->sample_attempts | r->nodes |
                   r->propagated_cells;
    return any <= (uint32_t)INT32_MAX;
}

ms_status ms_engine_accept_result(ms_engine *engine, uint32_t generation, uint32_t revision,
                                  const void *result, size_t result_len) {
    if (!engine_ok(engine)) return MS_ERR_INTERNAL;
    if (bad_buffer(engine, result, result_len)) return MS_ERR_INVALID_BUFFER;
    ms_status status = check_current(engine, generation, revision);
    if (status != MS_OK) return status;
    status = check_playing(engine);
    if (status != MS_OK) return status;

    /* A fresh public observation of exactly this revision: the result must
     * answer it (dimensions, total, revealed clues via observation_hash). */
    status = ms_game_observe(engine->game, generation, revision, engine->obs, engine->obs_len);
    if (status != MS_OK) return status;
    status = ms_result_validate(engine->obs, engine->obs_len, result, result_len);
    if (status != MS_OK) return status;
    const ms_result_header *r = (const ms_result_header *)result;
    if (!counts_ok(r)) return MS_ERR_INVALID_RESULT;

    /* The latest validated result for the revision always replaces the
     * stored one: no status subsumes another's proofs (a sampled result may
     * leave 0/1 endpoints unproven that a later budget-limited run proves),
     * and JS request generations already drop obsolete replies. */
    base_memcpy(engine->result, result, result_len);
    engine->stored = 1;
    engine->stored_revision = revision;
    engine->stored_status = r->status;
    return MS_OK;
}

ms_status ms_engine_autosolve(ms_engine *engine, uint32_t generation, uint32_t revision,
                              bool *available, bool *changed) {
    if (!engine_ok(engine)) return MS_ERR_INTERNAL;
    if (available != NULL && (void *)available == (void *)changed) return MS_ERR_INVALID_BUFFER;
    ms_status status = check_current(engine, generation, revision);
    if (status != MS_OK) return status;
    status = check_playing(engine);
    if (status != MS_OK) return status;
    bool stored = has_result(engine, revision);
    bool did = false;
    if (stored) {
        /* Only the proof flags of the validated result become moves. */
        const uint8_t *flags = ms_result_flags(engine->result);
        uint32_t cells = engine->width * engine->height;
        uint32_t safe_count = 0, mine_count = 0;
        for (uint32_t i = 0; i < cells; i++) {
            if (flags[i] & MS_PCELL_PROVEN_SAFE) engine->proof_safe[safe_count++] = i;
            if (flags[i] & MS_PCELL_PROVEN_MINE) engine->proof_mines[mine_count++] = i;
        }
        status = ms_game_apply_deductions(engine->game, generation, revision, engine->proof_safe,
                                          safe_count, engine->proof_mines, mine_count, &did);
        if (status != MS_OK) return status;
        if (did) engine->stored = 0;
    }
    if (available) *available = stored;
    if (changed) *changed = did;
    return MS_OK;
}
