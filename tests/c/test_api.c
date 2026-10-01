/* Tests for c/engine.c, the per-tab engine service beneath the WebAssembly
 * reactor: generations, revision-bound result acceptance and storage,
 * placeholders, public observations, atomic certainty-only autosolve and
 * allocation/ownership guarantees. Independent of the solver: results are
 * fixture blobs (tests/c/fixtures/engine_cases.h, autosolve proof lists)
 * validated by the engine exactly like worker results. Ports the c-api
 * destinations of tests/coverage-map.json. */

#include "test_support.h"

#include "fixtures/autosolve_cases.h"
#include "fixtures/engine_cases.h"
#include "fixtures/game_scripts.h"
#include "fixtures/probability_cases.h"
#include "game.h"
#include "wasm_buffers.h"

#define VIEW_WORDS ((sizeof(ms_view_header) + MS_GAME_MAX_CELLS + 7u) / 8u)
#define OBS_WORDS ((sizeof(ms_obs_header) + MS_GAME_MAX_CELLS + 7u) / 8u)
#define RESULT_WORDS ((sizeof(ms_result_header) + 9u * MS_GAME_MAX_CELLS + 7u) / 8u)

static uint64_t view_buf[VIEW_WORDS];
static uint64_t before_buf[VIEW_WORDS];
static uint64_t obs_buf[OBS_WORDS];
static uint64_t obs_copy[OBS_WORDS];
static uint64_t result_buf[RESULT_WORDS];
static uint64_t result_copy[RESULT_WORDS];
static uint64_t cached_buf[RESULT_WORDS];

/* ---------------------------------------------------------------- helpers */

typedef struct api_env {
    rt_mem mem;
    fake_clock fake;
    rt_clock clock;
    ms_engine engine;
} api_env;

static void api_open(api_env *env) {
    rt_mem_init(&env->mem, RT_MEM_UNLIMITED);
    fake_clock_init(&env->clock, &env->fake, 1000.0, 0.0);
    CHECK_STATUS(ms_engine_init(&env->engine, &env->mem, &env->clock), MS_OK);
}

static void api_close(api_env *env) {
    ms_engine_dispose(&env->engine);
    CHECK_EQ(env->mem.live, 0);
    CHECK_EQ(env->mem.misuses, 0);
    rt_mem_dispose(&env->mem);
}

static size_t view_len(const api_env *env) {
    return ms_view_size(env->engine.width, env->engine.height);
}

static size_t obs_len(const api_env *env) {
    return ms_obs_size(env->engine.width, env->engine.height);
}

static size_t result_len(const api_env *env) {
    return ms_result_size(env->engine.width, env->engine.height);
}

static const ms_view_header *take_view(api_env *env, uint32_t generation, uint64_t *buf) {
    CHECK_STATUS(ms_engine_view(&env->engine, generation, buf, view_len(env)), MS_OK);
    CHECK_STATUS(ms_view_validate(buf, view_len(env)), MS_OK);
    return (const ms_view_header *)buf;
}

static bool same_bytes(const void *a, const void *b, size_t len) {
    return base_memcmp(a, b, len) == 0;
}

/* Prints a table-driven case label "<table>/<row>" (tests/coverage-map.json
 * pending_test names). */
static void test_case_in(const char *table, const char *row) {
    test_print("  - ");
    test_print(table);
    test_print("/");
    test_print(row);
    test_print("\n");
}

static char render_cell(uint8_t b) {
    if (b & MS_CELL_REVEALED) {
        return (b & MS_CELL_MINE) ? 'X' : (char)('0' + (b & MS_CELL_ADJACENT_MASK));
    }
    return (b & MS_CELL_FLAGGED) ? 'F' : '#';
}

static void check_board(const void *view, const char *board) {
    const ms_view_header *h = (const ms_view_header *)view;
    uint32_t count = h->width * h->height;
    CHECK_EQ(base_strlen(board), count);
    for (uint32_t i = 0; i < count; i++) {
        if (render_cell(ms_view_cells(view)[i]) != board[i]) {
            test_print("  board mismatch at cell ");
            test_print_u64(i);
            test_print("\n");
            CHECK(render_cell(ms_view_cells(view)[i]) == board[i]);
        }
    }
}

/* The TWO_MINES game of engine_cases.h: new game, returns its generation. */
static uint32_t two_mines_game(api_env *env) {
    uint32_t generation = 0;
    CHECK_STATUS(ms_engine_new_game_with_layout(&env->engine, 5, 5, 2, ENGINE_BASE_LAYOUT, 2,
                                                &generation),
                 MS_OK);
    CHECK_EQ(generation, env->engine.generation);
    return generation;
}

static void act(api_env *env, uint32_t generation, uint32_t revision, uint32_t action,
                uint32_t row, uint32_t col, bool expect_changed) {
    bool changed = !expect_changed;
    CHECK_STATUS(ms_engine_act(&env->engine, generation, revision, action, row, col, &changed),
                 MS_OK);
    CHECK(changed == expect_changed);
}

/* TWO_MINES after revealing (0,0): revision 1, cells 0..14 revealed. */
static uint32_t opened_two_mines(api_env *env) {
    uint32_t generation = two_mines_game(env);
    act(env, generation, 0, MS_ACTION_REVEAL, 0, 0, true);
    return generation;
}

static bool cached(api_env *env, uint32_t generation, uint32_t revision) {
    bool available = false;
    CHECK_STATUS(ms_engine_cached_result(&env->engine, generation, revision, cached_buf,
                                         result_len(env), &available),
                 MS_OK);
    return available;
}

/* Observation of the current position and a result started for it. */
static void start_result(api_env *env, uint32_t generation, uint32_t revision, void *result) {
    CHECK_STATUS(ms_engine_observe(&env->engine, generation, revision, obs_buf, obs_len(env)),
                 MS_OK);
    CHECK_STATUS(ms_result_init(result, result_len(env), obs_buf, obs_len(env)), MS_OK);
}

/* engine_cases.h base result (tests/test_server.py fake_odds). */
static void base_result(api_env *env, uint32_t generation, uint32_t revision, void *result) {
    start_result(env, generation, revision, result);
    ms_result_header *h = (ms_result_header *)result;
    h->status = MS_PROB_EXACT;
    h->reason = MS_REASON_NONE;
    h->frontier_cells = ENGINE_BASE_FRONTIER_CELLS;
    h->components = ENGINE_BASE_COMPONENTS;
    h->unconstrained_cells = ENGINE_BASE_UNCONSTRAINED_CELLS;
    h->samples = ENGINE_BASE_SAMPLES;
    h->elapsed_ms = ENGINE_BASE_ELAPSED_MS;
    const uint8_t *clues = ms_obs_clues(obs_buf);
    for (uint32_t i = 0; i < h->width * h->height; i++) {
        if (clues[i] != MS_CLUE_HIDDEN) continue;
        ms_result_flags(result)[i] = MS_PCELL_VALUE;
        ms_result_probabilities(result)[i] = ENGINE_BASE_HIDDEN_PROBABILITY;
    }
    CHECK_STATUS(ms_result_validate(obs_buf, obs_len(env), result, result_len(env)), MS_OK);
}

static void add_proof(void *result, uint32_t cell, uint8_t bit) {
    ms_result_header *h = (ms_result_header *)result;
    uint8_t *flags = ms_result_flags(result);
    if (flags[cell] & bit) return;
    flags[cell] |= (uint8_t)(MS_PCELL_VALUE | bit);
    ms_result_probabilities(result)[cell] = bit == MS_PCELL_PROVEN_MINE ? 1.0 : 0.0;
    if (bit == MS_PCELL_PROVEN_MINE) {
        h->proven_mines++;
    } else {
        h->proven_safe++;
    }
}

/* An UNAVAILABLE result carrying only the given proofs (what a budget-cut
 * solve returns): the engine must treat its proofs like any other. */
static void proofs_result(api_env *env, uint32_t generation, uint32_t revision, void *result,
                          const FixtureRun *safe, uint32_t safe_runs, const FixtureRun *mines,
                          uint32_t mine_runs) {
    start_result(env, generation, revision, result);
    ms_result_header *h = (ms_result_header *)result;
    h->status = MS_PROB_UNAVAILABLE;
    h->reason = MS_REASON_TIME_BUDGET_EXHAUSTED;
    for (uint32_t r = 0; r < safe_runs; r++) {
        for (uint32_t k = 0; k < safe[r].count; k++) {
            add_proof(result, fixture_run_cell(&safe[r], k), MS_PCELL_PROVEN_SAFE);
        }
    }
    for (uint32_t r = 0; r < mine_runs; r++) {
        for (uint32_t k = 0; k < mines[r].count; k++) {
            add_proof(result, fixture_run_cell(&mines[r], k), MS_PCELL_PROVEN_MINE);
        }
    }
    CHECK_STATUS(ms_result_validate(obs_buf, obs_len(env), result, result_len(env)), MS_OK);
}

/* ---------------------------------------------------------------- fixture mutations */

static uint32_t map_prob_status(int64_t raw) {
    if (raw >= FIXTURE_STATUS_EXACT && raw <= FIXTURE_STATUS_FINISHED) {
        return (uint32_t)raw + MS_PROB_EXACT; /* exact .. finished in both enums */
    }
    return (uint32_t)raw; /* out of range in both */
}

static uint32_t map_reason(int64_t raw) {
    if (raw >= FIXTURE_REASON_NONE && raw <= FIXTURE_REASON_TIME_BUDGET_EXHAUSTED) {
        return (uint32_t)raw;
    }
    if (raw == FIXTURE_REASON_NOT_STARTED) return MS_REASON_NOT_STARTED;
    if (raw == FIXTURE_REASON_GAME_OVER) return MS_REASON_GAME_OVER;
    return (uint32_t)raw;
}

static double fixture_value(uint32_t kind, double value) {
    switch (kind) {
    case FIXTURE_VALUE_NAN: return fixture_f64(0x7FF8000000000000ull);
    case FIXTURE_VALUE_POS_INF: return fixture_f64(0x7FF0000000000000ull);
    case FIXTURE_VALUE_NEG_INF: return fixture_f64(0xFFF0000000000000ull);
    default: return value;
    }
}

/* JSON value null <-> no MS_PCELL_VALUE and 0.0; a number <-> the bit and
 * the number. Proof bits are separate lists and stay untouched. */
static void set_value(void *result, uint32_t cell, const FixtureMutation *m) {
    uint8_t *flags = ms_result_flags(result);
    double *p = ms_result_probabilities(result);
    if (m->value_kind == FIXTURE_VALUE_NULL) {
        flags[cell] &= (uint8_t)~MS_PCELL_VALUE;
        p[cell] = 0.0;
    } else {
        flags[cell] |= MS_PCELL_VALUE;
        p[cell] = fixture_value(m->value_kind, m->value);
    }
}

/* Proof indices become flag bits; an index naming no cell can only be
 * encoded as a proof count without a flag, which validation rejects. */
static void mutate_proof(void *result, int64_t raw, uint8_t bit) {
    ms_result_header *h = (ms_result_header *)result;
    uint32_t cells = h->width * h->height;
    uint8_t *flags = ms_result_flags(result);
    if (raw >= 0 && raw < (int64_t)cells) {
        if (flags[raw] & bit) return; /* duplicates merge */
        flags[raw] |= bit;
    }
    if (bit == MS_PCELL_PROVEN_SAFE) {
        h->proven_safe++;
    } else {
        h->proven_mines++;
    }
}

/* The header's count fields: indices 0..3 follow FixtureMetaField
 * (frontier, components, unconstrained, samples); 4..8 are the rest the
 * engine checks. */
#define COUNT_FIELDS 9u

static uint32_t *count_field(ms_result_header *h, uint32_t index) {
    switch (index) {
    case 0: return &h->frontier_cells;
    case 1: return &h->components;
    case 2: return &h->unconstrained_cells;
    case 3: return &h->samples;
    case 4: return &h->exact_components;
    case 5: return &h->sampled_components;
    case 6: return &h->sample_attempts;
    case 7: return &h->nodes;
    case 8: return &h->propagated_cells;
    default: break;
    }
    CHECK(!"unknown count field");
    return &h->reserved;
}

/* Applies one mutation; *len may change (short probability list). Signed
 * fixture counts are stored as their two's complement uint32 bits. */
static void mutate(void *result, size_t *len, const FixtureMutation *m) {
    ms_result_header *h = (ms_result_header *)result;
    uint32_t cells = h->width * h->height;
    switch (m->kind) {
    case FIXTURE_MUT_STATUS: h->status = map_prob_status(m->raw); break;
    case FIXTURE_MUT_PROBABILITY_COUNT:
        *len = ms_pad8(sizeof(ms_result_header) + (size_t)m->raw * 9u);
        break;
    case FIXTURE_MUT_ALL_PROBABILITIES:
        for (uint32_t i = 0; i < cells; i++) set_value(result, i, m);
        break;
    case FIXTURE_MUT_HIDDEN_PROBABILITIES:
        for (uint32_t i = 0; i < cells; i++) {
            if (ms_obs_clues(obs_buf)[i] == MS_CLUE_HIDDEN) set_value(result, i, m);
        }
        break;
    case FIXTURE_MUT_PROBABILITY: set_value(result, m->target, m); break;
    case FIXTURE_MUT_ADD_SAFE: mutate_proof(result, m->raw, MS_PCELL_PROVEN_SAFE); break;
    case FIXTURE_MUT_ADD_MINE: mutate_proof(result, m->raw, MS_PCELL_PROVEN_MINE); break;
    case FIXTURE_MUT_META_COUNT:
        CHECK(m->target <= FIXTURE_META_SAMPLES);
        *count_field(h, m->target) = (uint32_t)m->raw;
        break;
    case FIXTURE_MUT_ELAPSED_MS: h->elapsed_ms = fixture_value(m->value_kind, m->value); break;
    case FIXTURE_MUT_REASON: h->reason = map_reason(m->raw); break;
    case FIXTURE_MUT_DIAGNOSTIC:
        if (m->target == FIXTURE_DIAG_EXACT_COMPONENTS) {
            h->exact_components = (uint32_t)m->raw;
        } else if (m->target == FIXTURE_DIAG_SAMPLED_COMPONENTS) {
            h->sampled_components = (uint32_t)m->raw;
        } else if (m->target == FIXTURE_DIAG_SAMPLE_ATTEMPTS) {
            h->sample_attempts = (uint32_t)m->raw;
        } else {
            CHECK_EQ(m->target, FIXTURE_DIAG_EFFECTIVE_SAMPLE_SIZE);
            bool present = m->value_kind != FIXTURE_VALUE_NULL;
            h->has_effective_sample_size = present;
            h->effective_sample_size = present ? fixture_value(m->value_kind, m->value) : 0.0;
        }
        break;
    default: CHECK(!"unknown fixture mutation"); break;
    }
}

static bool list_has(const uint32_t *list, uint32_t count, uint32_t cell) {
    for (uint32_t i = 0; i < count; i++) {
        if (list[i] == cell) return true;
    }
    return false;
}

/* tests.test_server.ProbabilityTests (malformed output, proofs,
 * completeness, revealed cells, diagnostics) and
 * tests.test_autosolve.AutosolveApiTests: every ENGINE_RESULT_CASES row.
 * Rejected rows also show that autosolve never applies anything but the
 * proofs of a result the engine accepted for the current revision. */
static void test_engine_result_cases(void) {
    test_case("autosolve_applies_only_engine_validated_proofs");
    for (uint32_t c = 0; c < ENGINE_RESULT_CASE_COUNT; c++) {
        const EngineResultCase *rc = &ENGINE_RESULT_CASES[c];
        test_case_in("engine_results", rc->name);
        api_env env;
        api_open(&env);
        uint32_t g = opened_two_mines(&env);
        CHECK_EQ(g, 1);
        base_result(&env, g, ENGINE_BASE_REVISION, result_buf);
        size_t len = result_len(&env);
        for (uint32_t m = 0; m < rc->mutation_count; m++) mutate(result_buf, &len, &rc->mutations[m]);
        base_memcpy(result_copy, result_buf, result_len(&env));
        take_view(&env, g, before_buf);

        ms_status status = ms_engine_accept_result(&env.engine, g, ENGINE_BASE_REVISION,
                                                   result_buf, len);
        CHECK(same_bytes(result_buf, result_copy, result_len(&env))); /* input untouched */
        bool available = true, changed = true;
        if (!rc->accepted) {
            CHECK(status == MS_ERR_INVALID_RESULT || status == MS_ERR_INVALID_BUFFER);
            CHECK_EQ(env.engine.stored, 0);
            CHECK(!cached(&env, g, 1));
            CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
            CHECK(!available && !changed);
            CHECK(same_bytes(before_buf, take_view(&env, g, view_buf), view_len(&env)));
            api_close(&env);
            continue;
        }
        CHECK_STATUS(status, MS_OK);
        CHECK_EQ(env.engine.stored, 1);
        const ms_result_header *r = (const ms_result_header *)result_buf;
        CHECK_EQ(env.engine.stored_status, r->status);
        /* Normalized proofs: exactly the listed cells, each once. */
        const uint8_t *flags = ms_result_flags(env.engine.result);
        uint32_t safe = 0, mines = 0;
        for (uint32_t i = 0; i < 25; i++) {
            bool s = (flags[i] & MS_PCELL_PROVEN_SAFE) != 0;
            bool mn = (flags[i] & MS_PCELL_PROVEN_MINE) != 0;
            CHECK(s == list_has(rc->normalized_safe, rc->normalized_safe_count, i));
            CHECK(mn == list_has(rc->normalized_mines, rc->normalized_mine_count, i));
            safe += s;
            mines += mn;
        }
        CHECK_EQ(safe, rc->normalized_safe_count);
        CHECK_EQ(mines, rc->normalized_mine_count);
        /* Complete results are served again (diagnostics preserved bit for
         * bit); unavailable ones only feed autosolve. */
        bool complete = r->status != MS_PROB_UNAVAILABLE;
        CHECK(cached(&env, g, 1) == complete);
        if (complete) CHECK(same_bytes(cached_buf, result_buf, len));

        CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
        CHECK(available);
        CHECK(changed == (rc->batch_changed != 0));
        const ms_view_header *v = take_view(&env, g, view_buf);
        CHECK_EQ(v->revision, (uint64_t)rc->revision_after_batch);
        check_board(view_buf, rc->board_after_batch);
        /* A batch clears the stored result; a pause keeps it. */
        CHECK_EQ(env.engine.stored, changed ? 0u : 1u);
        api_close(&env);
    }
}

/* ---------------------------------------------------------------- lifecycle */

static void test_engine_misuse(void) {
    test_case("engine misuse: NULL, uninitialized and disposed engines");
    api_env env;
    rt_mem_init(&env.mem, RT_MEM_UNLIMITED);
    fake_clock_init(&env.clock, &env.fake, 0.0, 0.0);
    CHECK_STATUS(ms_engine_init(NULL, &env.mem, &env.clock), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_init(&env.engine, NULL, &env.clock), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_new_game(&env.engine, 9, 9, 10, 1, NULL), MS_ERR_INTERNAL);
    CHECK_STATUS(ms_engine_init(&env.engine, &env.mem, NULL), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_act(&env.engine, 1, 0, MS_ACTION_FLAG, 0, 0, NULL), MS_ERR_INTERNAL);
    CHECK_STATUS(ms_engine_act(NULL, 1, 0, MS_ACTION_FLAG, 0, 0, NULL), MS_ERR_INTERNAL);
    bool available = true;
    CHECK_STATUS(ms_engine_cached_result(NULL, 1, 0, cached_buf, 352, &available),
                 MS_ERR_INTERNAL);
    CHECK(available); /* outputs are written only on MS_OK */
    CHECK_STATUS(ms_engine_accept_result(NULL, 1, 0, result_buf, 352), MS_ERR_INTERNAL);
    CHECK_STATUS(ms_engine_autosolve(NULL, 1, 0, NULL, NULL), MS_ERR_INTERNAL);
    CHECK_STATUS(ms_engine_view(NULL, 1, view_buf, 80), MS_ERR_INTERNAL);
    CHECK_STATUS(ms_engine_observe(NULL, 1, 0, obs_buf, 64), MS_ERR_INTERNAL);
    ms_engine_dispose(NULL);

    CHECK_STATUS(ms_engine_init(&env.engine, &env.mem, &env.clock), MS_OK);
    /* Before the first game every generation is stale (or invalid). */
    CHECK_STATUS(ms_engine_act(&env.engine, 1, 0, MS_ACTION_FLAG, 0, 0, NULL),
                 MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_act(&env.engine, 1, 0, 7, 0, 0, NULL), MS_ERR_INVALID_ACTION);
    CHECK_STATUS(ms_engine_act(&env.engine, 0, 0, MS_ACTION_FLAG, 0, 0, NULL),
                 MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_engine_view(&env.engine, 1, view_buf, 80), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_observe(&env.engine, 1, 0, obs_buf, 64), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, 1, 0, cached_buf, 352, &available),
                 MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, 1, 0, result_buf, 352),
                 MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_autosolve(&env.engine, 1, 0, NULL, NULL), MS_ERR_STALE_REVISION);

    uint32_t g = two_mines_game(&env);
    CHECK_EQ(g, 1);
    CHECK(env.mem.live > 0);
    ms_engine_dispose(&env.engine);
    CHECK_EQ(env.mem.live, 0);
    ms_engine_dispose(&env.engine);
    CHECK_STATUS(ms_engine_view(&env.engine, 1, view_buf, 80), MS_ERR_INTERNAL);
    CHECK_STATUS(ms_engine_new_game(&env.engine, 9, 9, 10, 1, NULL), MS_ERR_INTERNAL);
    CHECK_EQ(env.mem.misuses, 0);
    rt_mem_dispose(&env.mem);
}

/* tests.test_game.GameStoreTests.test_invalid_config_is_not_stored and
 * allocator/generation exhaustion: a failed new game changes nothing. */
static void test_failed_new_game_keeps_current_game(void) {
    test_case("failed_new_game_keeps_current_game");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    base_result(&env, g, 1, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, result_len(&env)), MS_OK);
    take_view(&env, g, before_buf);
    ms_game *game = env.engine.game;
    size_t live = env.mem.live;

    for (uint32_t i = 0; i < GAME_CONFIG_CASE_COUNT; i++) {
        const GameConfigCase *c = &GAME_CONFIG_CASES[i];
        if (c->error == FIXTURE_ERR_NONE) continue;
        uint32_t w = c->width < 0 || c->width > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)c->width;
        uint32_t h = c->height < 0 || c->height > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)c->height;
        uint32_t m = c->mines < 0 || c->mines > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)c->mines;
        uint32_t out = 77;
        ms_status expected = c->error == FIXTURE_ERR_INVALID_WIDTH    ? MS_ERR_INVALID_WIDTH
                             : c->error == FIXTURE_ERR_INVALID_HEIGHT ? MS_ERR_INVALID_HEIGHT
                                                                      : MS_ERR_INVALID_MINES;
        CHECK_STATUS(ms_engine_new_game(&env.engine, w, h, m, i, &out), expected);
        CHECK_EQ(out, 77);
    }
    /* Allocation failure on the first (storage) and second (game) block. */
    for (uint64_t after = 0; after < 2; after++) {
        rt_mem_set_failure(&env.mem, after, 1);
        CHECK_STATUS(ms_engine_new_game(&env.engine, 80, 80, 99, 5, NULL),
                     MS_ERR_RESOURCE_EXHAUSTED);
        rt_mem_set_failure(&env.mem, 0, 0);
        CHECK_EQ(env.mem.live, live);
    }
    /* The engine budget refuses a second 80x80 game. */
    rt_mem_set_budget(&env.mem, live + 4096);
    CHECK_STATUS(ms_engine_new_game(&env.engine, 80, 80, 99, 5, NULL), MS_ERR_RESOURCE_EXHAUSTED);
    rt_mem_set_budget(&env.mem, RT_MEM_UNLIMITED);
    CHECK_EQ(env.mem.live, live);

    CHECK(env.engine.game == game);
    CHECK_EQ(env.engine.generation, g);
    CHECK(same_bytes(before_buf, take_view(&env, g, view_buf), view_len(&env)));
    CHECK(cached(&env, g, 1));
    CHECK(same_bytes(cached_buf, result_buf, result_len(&env)));
    api_close(&env);

    /* Generation exhaustion. */
    api_open(&env);
    env.engine.generation = MS_GENERATION_MAX - 1;
    uint32_t out = 0;
    CHECK_STATUS(ms_engine_new_game(&env.engine, 9, 9, 10, 3, &out), MS_OK);
    CHECK_EQ(out, MS_GENERATION_MAX);
    CHECK_STATUS(ms_engine_new_game(&env.engine, 9, 9, 10, 3, &out), MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_EQ(env.engine.generation, MS_GENERATION_MAX);
    act(&env, MS_GENERATION_MAX, 0, MS_ACTION_REVEAL, 4, 4, true);
    CHECK_EQ(take_view(&env, MS_GENERATION_MAX, view_buf)->generation, MS_GENERATION_MAX);
    api_close(&env);
}

/* tests.test_server.CreateGameTests.test_games_are_independent */
static void test_engine_instances_are_independent(void) {
    test_case("engine_instances_are_independent");
    api_env a, b;
    api_open(&a);
    api_open(&b);
    uint32_t ga = 0, gb = 0;
    CHECK_STATUS(ms_engine_new_game(&a.engine, 16, 16, 40, 42, &ga), MS_OK);
    CHECK_STATUS(ms_engine_new_game(&b.engine, 16, 16, 40, 42, &gb), MS_OK);
    CHECK_EQ(ga, 1);
    CHECK_EQ(gb, 1);
    act(&a, 1, 0, MS_ACTION_REVEAL, 8, 8, true);
    const ms_view_header *vb = take_view(&b, 1, view_buf);
    CHECK_EQ(vb->status, MS_GAME_READY);
    CHECK_EQ(vb->revision, 0);
    CHECK(!ms_game_test_placed(b.engine.game));
    CHECK_STATUS(ms_engine_new_game(&b.engine, 9, 9, 10, 7, &gb), MS_OK);
    CHECK_EQ(gb, 2);
    CHECK_EQ(a.engine.generation, 1);
    CHECK_EQ(take_view(&a, 1, view_buf)->revision, 1);

    /* A new game drops the old one: its generation is stale everywhere and
     * its stored result is gone. */
    uint32_t g = opened_two_mines(&a);
    CHECK_EQ(g, 2);
    base_result(&a, g, 1, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&a.engine, g, 1, result_buf, result_len(&a)), MS_OK);
    uint32_t next = 0;
    CHECK_STATUS(ms_engine_new_game_with_layout(&a.engine, 5, 5, 2, ENGINE_BASE_LAYOUT, 2, &next),
                 MS_OK);
    CHECK_EQ(next, 3);
    CHECK_EQ(a.engine.stored, 0);
    CHECK_STATUS(ms_engine_view(&a.engine, g, view_buf, 80), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_act(&a.engine, g, 1, MS_ACTION_FLAG, 4, 4, NULL), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_accept_result(&a.engine, g, 1, result_buf, 352), MS_ERR_STALE_REVISION);
    bool available = true;
    CHECK_STATUS(ms_engine_autosolve(&a.engine, g, 1, &available, NULL), MS_ERR_STALE_REVISION);
    act(&a, next, 0, MS_ACTION_REVEAL, 0, 0, true);
    CHECK(!cached(&a, next, 1)); /* same position, but the old result is gone */
    /* The old generation stays stale even at the new game's revision. */
    CHECK_STATUS(ms_engine_cached_result(&a.engine, g, 1, cached_buf, 352, &available),
                 MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_observe(&a.engine, g, 1, obs_buf, 64), MS_ERR_STALE_REVISION);
    base_result(&a, next, 1, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&a.engine, g, 1, result_buf, 352), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_act(&a.engine, g, 1, MS_ACTION_FLAG, 4, 4, NULL), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_accept_result(&a.engine, next, 1, result_buf, 352), MS_OK);
    CHECK_STATUS(ms_engine_autosolve(&a.engine, g, 1, &available, NULL), MS_ERR_STALE_REVISION);
    api_close(&a);
    api_close(&b);
}

static void test_repeated_new_games(void) {
    test_case("repeated new games release the previous game");
    api_env env;
    api_open(&env);
    CHECK_STATUS(ms_engine_new_game(&env.engine, 9, 9, 10, 1, NULL), MS_OK);
    size_t live9 = env.mem.live;
    CHECK_STATUS(ms_engine_new_game(&env.engine, 80, 80, 1280, 2, NULL), MS_OK);
    size_t live80 = env.mem.live;
    CHECK(live80 > live9);
    for (uint32_t i = 0; i < 50; i++) {
        uint32_t g = 0;
        CHECK_STATUS(ms_engine_new_game(&env.engine, 80, 80, 1280, 10 + i, &g), MS_OK);
        CHECK_EQ(g, 3 + i);
        act(&env, g, 0, MS_ACTION_REVEAL, 40, 40, true);
        CHECK_EQ(env.mem.live, live80);
    }
    CHECK_STATUS(ms_engine_new_game(&env.engine, 9, 9, 10, 3, NULL), MS_OK);
    CHECK_EQ(env.mem.live, live9);
    CHECK_EQ(env.mem.live_blocks, 2);
    api_close(&env);
}

/* ---------------------------------------------------------------- placeholders and requests */

static void check_placeholder(const void *result, uint32_t status, uint32_t reason) {
    const ms_result_header *h = (const ms_result_header *)result;
    CHECK_EQ(h->magic, MS_MAGIC_RESULT);
    CHECK_EQ(h->status, status);
    CHECK_EQ(h->reason, reason);
    CHECK_EQ(h->frontier_cells + h->components + h->unconstrained_cells + h->samples, 0);
    CHECK(h->elapsed_ms == 0.0);
    for (uint32_t i = 0; i < h->width * h->height; i++) {
        CHECK_EQ(ms_result_flags(result)[i], 0);
        CHECK(ms_result_probabilities(result)[i] == 0.0);
    }
}

/* tests.test_server.ProbabilityTests.test_not_started_game */
static void test_placeholder_not_started(void) {
    test_case("placeholder_not_started");
    api_env env;
    api_open(&env);
    uint32_t g = two_mines_game(&env);
    CHECK(cached(&env, g, 0));
    check_placeholder(cached_buf, MS_PROB_NOT_STARTED, MS_REASON_NOT_STARTED);
    CHECK_EQ(((const ms_result_header *)cached_buf)->width, 5);
    CHECK_EQ(((const ms_result_header *)cached_buf)->total_mines, 2);
    /* No solve request exists for a ready game. */
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 0, obs_buf, 64), MS_ERR_GAME_NOT_STARTED);
    api_close(&env);
}

/* tests.test_server.ProbabilityTests.test_finished_game */
static void test_placeholder_finished(void) {
    test_case("placeholder_finished");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    act(&env, g, 1, MS_ACTION_REVEAL, 3, 1, true); /* mine 16: lost */
    CHECK(cached(&env, g, 2));
    check_placeholder(cached_buf, MS_PROB_FINISHED, MS_REASON_GAME_OVER);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 2, obs_buf, 64), MS_ERR_GAME_OVER);

    /* Won games answer with the same finished placeholder. */
    g = opened_two_mines(&env);
    const ProbabilityCase *pc = &PROBABILITY_CASES[PROB_CASE_TWO_MINES_OPENED];
    proofs_result(&env, g, 1, result_buf, pc->proven_safe, pc->proven_safe_runs, pc->proven_mines,
                  pc->proven_mine_runs);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    bool available = false, changed = false;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
    CHECK(available && changed);
    CHECK_EQ(take_view(&env, g, view_buf)->status, MS_GAME_WON);
    CHECK(cached(&env, g, 2));
    check_placeholder(cached_buf, MS_PROB_FINISHED, MS_REASON_GAME_OVER);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 2, obs_buf, 64), MS_ERR_GAME_OVER);
    api_close(&env);
}

/* tests.test_autosolve.AutosolveApiTests.test_ready_and_finished_games_do_not_run_the_solver */
static void test_autosolve_refuses_ready_and_finished_games(void) {
    test_case("autosolve_refuses_ready_and_finished_games");
    api_env env;
    api_open(&env);
    uint32_t g = two_mines_game(&env);
    bool available = true, changed = true;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 0, &available, &changed),
                 MS_ERR_GAME_NOT_STARTED);
    CHECK(available && changed); /* untouched: outputs are written only on MS_OK */
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 0, obs_buf, 64), MS_ERR_GAME_NOT_STARTED);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 0, result_buf, 352),
                 MS_ERR_GAME_NOT_STARTED);

    act(&env, g, 0, MS_ACTION_REVEAL, 0, 0, true);
    act(&env, g, 1, MS_ACTION_REVEAL, 3, 1, true); /* mine 16: lost */
    take_view(&env, g, before_buf);
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 2, &available, &changed), MS_ERR_GAME_OVER);
    CHECK(available && changed);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 2, obs_buf, 64), MS_ERR_GAME_OVER);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 2, result_buf, 352), MS_ERR_GAME_OVER);
    CHECK(same_bytes(before_buf, take_view(&env, g, view_buf), 80));
    api_close(&env);
}

/* tests.test_server.ProbabilityTests.test_playing_game_uses_only_public_observation */
static void test_solve_request(void) {
    test_case("solve_request_uses_public_observation_and_default_budgets");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, obs_buf, obs_len(&env)), MS_OK);
    CHECK_STATUS(ms_obs_validate(obs_buf, obs_len(&env)), MS_OK);
    const ProbabilityCase *pc = &PROBABILITY_CASES[PROB_CASE_TWO_MINES_OPENED];
    int8_t dense[25];
    CHECK(fixture_case_dense_clues(pc, dense, 25));
    const ms_obs_header *o = (const ms_obs_header *)obs_buf;
    CHECK_EQ(o->width, pc->width);
    CHECK_EQ(o->height, pc->height);
    CHECK_EQ(o->total_mines, pc->total_mines);
    for (uint32_t i = 0; i < 25; i++) {
        uint8_t clue = ms_obs_clues(obs_buf)[i];
        CHECK_EQ(clue, dense[i] == FIXTURE_HIDDEN_CELL ? MS_CLUE_HIDDEN : (uint8_t)dense[i]);
    }
    /* The request is tagged with (generation, revision) beside the bytes. */
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 0, obs_buf, obs_len(&env)),
                 MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_observe(&env.engine, g + 1, 1, obs_buf, obs_len(&env)),
                 MS_ERR_STALE_REVISION);

    ms_infer_limits limits;
    ms_limits_default(&limits);
    CHECK_STATUS(ms_limits_validate(&limits), MS_OK);
    CHECK(limits.time_budget_ms == (double)FIXTURE_DEFAULT_TIME_BUDGET_MS);
    CHECK_EQ(limits.node_budget, FIXTURE_DEFAULT_NODE_BUDGET);
    CHECK_EQ(limits.sample_budget, FIXTURE_DEFAULT_SAMPLE_BUDGET);
    CHECK(limits.min_effective_samples == FIXTURE_MIN_EFFECTIVE_SAMPLE_SIZE);
    CHECK_EQ(limits.max_stored_entries, FIXTURE_MAX_STORED_ENTRIES);
    CHECK_EQ(limits.flags, 0);
    api_close(&env);
}

/* tests.test_probability.PublicInformationOnlyTests.test_signature_has_no_secret_or_flag_inputs */
static void test_solver_input_public_only(void) {
    test_case("solver_input_is_public_observation_only");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, obs_buf, obs_len(&env)), MS_OK);
    /* Only dimensions, the mine total and revealed clues: no generation,
     * revision, flags or layout fields exist, and the reserved words are 0. */
    const ms_obs_header *o = (const ms_obs_header *)obs_buf;
    CHECK_EQ(o->magic, MS_MAGIC_OBSERVATION);
    CHECK_EQ(o->reserved0 + o->reserved1, 0);
    CHECK_EQ(obs_len(&env), ms_pad8(sizeof(ms_obs_header) + 25));

    /* Flags are not evidence: flagging a mine and a safe cell changes the
     * revision only; the solver input stays byte-identical. */
    base_memcpy(obs_copy, obs_buf, obs_len(&env));
    act(&env, g, 1, MS_ACTION_FLAG, 3, 0, true);
    act(&env, g, 2, MS_ACTION_FLAG, 3, 1, true);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 3, obs_buf, obs_len(&env)), MS_OK);
    CHECK(same_bytes(obs_buf, obs_copy, obs_len(&env)));
    for (uint32_t i = 15; i < 25; i++) CHECK_EQ(ms_obs_clues(obs_buf)[i], MS_CLUE_HIDDEN);
    api_close(&env);
}

/* tests.test_probability.PublicInformationOnlyTests
 * .test_layouts_with_the_same_observation_get_the_same_answer (game level) */
static void test_public_equivalence(void) {
    test_case("public_equivalence_game_5x5");
    api_env a, b;
    api_open(&a);
    api_open(&b);
    uint32_t ga = 0, gb = 0;
    CHECK_STATUS(ms_engine_new_game_with_layout(&a.engine, 5, 5, 3, PUBLIC_EQUIVALENCE_GAME_LAYOUT_A,
                                                3, &ga),
                 MS_OK);
    CHECK_STATUS(ms_engine_new_game_with_layout(&b.engine, 5, 5, 3, PUBLIC_EQUIVALENCE_GAME_LAYOUT_B,
                                                3, &gb),
                 MS_OK);
    act(&a, ga, 0, MS_ACTION_REVEAL, 0, 0, true);
    act(&b, gb, 0, MS_ACTION_REVEAL, 0, 0, true);
    take_view(&a, ga, view_buf);
    take_view(&b, gb, before_buf);
    CHECK(same_bytes(view_buf, before_buf, view_len(&a)));
    CHECK_STATUS(ms_engine_observe(&a.engine, ga, 1, obs_buf, obs_len(&a)), MS_OK);
    CHECK_STATUS(ms_engine_observe(&b.engine, gb, 1, obs_copy, obs_len(&b)), MS_OK);
    CHECK(same_bytes(obs_buf, obs_copy, obs_len(&a)));
    const ProbabilityCase *pc = &PROBABILITY_CASES[PROB_CASE_PUBLIC_EQUIVALENCE_GAME_5X5];
    int8_t dense[25];
    CHECK(fixture_case_dense_clues(pc, dense, 25));
    for (uint32_t i = 0; i < 25; i++) {
        uint8_t clue = ms_obs_clues(obs_buf)[i];
        CHECK_EQ(clue, dense[i] == FIXTURE_HIDDEN_CELL ? MS_CLUE_HIDDEN : (uint8_t)dense[i]);
    }
    api_close(&a);
    api_close(&b);
}

/* tests.test_server.ProbabilityTests.test_stale_revision_is_a_conflict */
static void test_odds_for_stale_revision(void) {
    test_case("odds_for_stale_revision_rejected");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    act(&env, g, 1, MS_ACTION_FLAG, 3, 1, true);
    bool available = true;
    /* No request, no cached answer and no acceptance for revision 1 ... */
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, obs_buf, 64), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 1, cached_buf, 352, &available),
                 MS_ERR_STALE_REVISION);
    CHECK(available); /* untouched: outputs are written only on MS_OK */
    base_result(&env, g, 2, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352),
                 MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 3, cached_buf, 352, &available),
                 MS_ERR_STALE_REVISION);
    /* ... and the latest state is always available to show instead. */
    const ms_view_header *v = take_view(&env, g, view_buf);
    CHECK_EQ(v->revision, 2);
    CHECK_EQ(v->flags, 1);
    CHECK_EQ(env.engine.stored, 0);
    api_close(&env);
}

/* ---------------------------------------------------------------- caching */

/* tests.test_game.GameStoreTests.test_snapshot_and_probability_cache_follow_revisions
 * and tests.test_server.ProbabilityTests.test_results_are_cached_per_revision */
static void test_cache_follows_revisions(void) {
    test_case("cache_follows_revisions");
    api_env env;
    api_open(&env);
    uint32_t g = two_mines_game(&env);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 0, obs_buf, 64), MS_ERR_GAME_NOT_STARTED);
    act(&env, g, 0, MS_ACTION_REVEAL, 0, 0, true);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, obs_buf, 64), MS_OK);
    CHECK_EQ(((const ms_obs_header *)obs_buf)->revealed, 15);

    base_result(&env, g, 1, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 0, result_buf, 352), MS_ERR_STALE_REVISION);
    CHECK(!cached(&env, g, 1));
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    CHECK(cached(&env, g, 1));
    CHECK(same_bytes(cached_buf, result_buf, 352));
    act(&env, g, 1, MS_ACTION_FLAG, 1, 1, false); /* no-op keeps it */
    act(&env, g, 1, MS_ACTION_CHORD, 4, 4, false);
    CHECK(cached(&env, g, 1));
    act(&env, g, 1, MS_ACTION_FLAG, 3, 1, true); /* a change drops it */
    CHECK(!cached(&env, g, 2));
    bool available = true;
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 1, cached_buf, 352, &available),
                 MS_ERR_STALE_REVISION);
    CHECK_EQ(env.engine.stored, 0);
    api_close(&env);
}

/* tests.test_server.ProbabilityTests.test_complete_results_are_reused_for_the_same_revision */
static void test_complete_results_cached(void) {
    test_case("complete_results_cached");
    for (uint32_t approximate = 0; approximate < 2; approximate++) {
        api_env env;
        api_open(&env);
        uint32_t g = opened_two_mines(&env);
        base_result(&env, g, 1, result_buf);
        if (approximate) ((ms_result_header *)result_buf)->status = MS_PROB_APPROXIMATE;
        CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
        for (uint32_t k = 0; k < 3; k++) {
            CHECK(cached(&env, g, 1));
            CHECK(same_bytes(cached_buf, result_buf, 352));
        }
        api_close(&env);
    }
}

/* tests.test_server.ProbabilityTests.test_unavailable_results_are_recomputed_on_retry */
static void test_unavailable_not_cached(void) {
    test_case("unavailable_not_cached");
    static const FixtureRun safe15[1] = {{15u, 1u, 1u}};
    static const FixtureRun mine16[1] = {{16u, 1u, 1u}};
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    proofs_result(&env, g, 1, result_buf, safe15, 1, mine16, 1);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    CHECK(!cached(&env, g, 1)); /* never served again: a retry solves anew */
    CHECK(!cached(&env, g, 1));

    /* A complete result for the same revision replaces it and is reused. */
    base_result(&env, g, 1, result_copy);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_copy, 352), MS_OK);
    CHECK(cached(&env, g, 1));
    CHECK(same_bytes(cached_buf, result_copy, 352));
    CHECK_EQ(env.engine.stored_status, MS_PROB_EXACT);
    api_close(&env);

    /* Proofs of a current unavailable result still feed autosolve; the
     * latest unavailable result is the one used. */
    api_open(&env);
    g = opened_two_mines(&env);
    proofs_result(&env, g, 1, result_buf, safe15, 1, NULL, 0);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    proofs_result(&env, g, 1, result_buf, safe15, 1, mine16, 1);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    bool available = false, changed = false;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
    CHECK(available && changed);
    take_view(&env, g, view_buf);
    check_board(view_buf, "00000" "00000" "11211" "1F###" "#####");
    api_close(&env);
}

/* The latest accepted result for a revision wins, whatever its status: a
 * sampled APPROXIMATE result may carry unproven 0/1 endpoints and no proofs,
 * while a later budget-limited UNAVAILABLE run proves cells exactly. Its
 * proofs must not be discarded, and the revision is no longer served from
 * the cache (a retry recomputes). */
static void test_latest_result_wins(void) {
    test_case("latest accepted result replaces a complete one (approximate, then unavailable)");
    static const FixtureRun safe15[1] = {{15u, 1u, 1u}};
    static const FixtureRun mine16[1] = {{16u, 1u, 1u}};
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    /* Approximate: every hidden cell valued, sampled endpoints on 15 (safe
     * in truth, sampled 1.0) and 16 (a mine, sampled 0.0), no proof bits. */
    base_result(&env, g, 1, result_copy);
    ms_result_header *h = (ms_result_header *)result_copy;
    h->status = MS_PROB_APPROXIMATE;
    h->reason = MS_REASON_COUNTING_BUDGET_EXCEEDED;
    ms_result_probabilities(result_copy)[15] = 1.0;
    ms_result_probabilities(result_copy)[16] = 0.0;
    CHECK_STATUS(ms_result_validate(obs_buf, 64, result_copy, 352), MS_OK);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_copy, 352), MS_OK);
    CHECK(cached(&env, g, 1));
    CHECK(same_bytes(cached_buf, result_copy, 352));
    bool available = false, changed = true;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
    CHECK(available && !changed); /* sampled endpoints are never moves */

    /* Unavailable, with sound proofs: it replaces the approximate result. */
    proofs_result(&env, g, 1, result_buf, safe15, 1, mine16, 1);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    CHECK_EQ(env.engine.stored_status, MS_PROB_UNAVAILABLE);
    CHECK(same_bytes(env.engine.result, result_buf, 352));
    base_memset(cached_buf, 0x5C, 352);
    CHECK(!cached(&env, g, 1)); /* display falls back to a fresh solve */
    for (uint32_t i = 0; i < 352; i++) CHECK_EQ(((const uint8_t *)cached_buf)[i], 0x5C);

    /* Autosolve applies the latest proofs: 15 revealed, 16 flagged. */
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
    CHECK(available && changed);
    const ms_view_header *v = take_view(&env, g, view_buf);
    CHECK_EQ(v->revision, 2);
    CHECK_EQ(v->flags, 1);
    check_board(view_buf, "00000" "00000" "11211" "1F###" "#####");
    CHECK_EQ(env.engine.stored, 0);
    api_close(&env);
}

/* ---------------------------------------------------------------- autosolve */

/* tests.test_autosolve.AutosolveApiTests: reuse, duplicate and late batches,
 * and only engine-validated proofs. */
static void test_autosolve_batches(void) {
    test_case("autosolve_reuses_cached_result");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    bool available = true, changed = true;
    /* No accepted result: nothing to apply, nothing changes. */
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
    CHECK(!available && !changed);
    CHECK_EQ(take_view(&env, g, view_buf)->revision, 1);

    base_result(&env, g, 1, result_buf);
    add_proof(result_buf, 15, MS_PCELL_PROVEN_SAFE);
    add_proof(result_buf, 16, MS_PCELL_PROVEN_MINE);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    CHECK(cached(&env, g, 1));
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
    CHECK(available && changed);
    const ms_view_header *v = take_view(&env, g, view_buf);
    CHECK_EQ(v->revision, 2);
    CHECK_EQ(v->flags, 1);
    check_board(view_buf, "00000" "00000" "11211" "1F###" "#####");
    CHECK(!cached(&env, g, 2)); /* the batch invalidated the cache */
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 2, obs_buf, 64), MS_OK);
    CHECK_EQ(((const ms_obs_header *)obs_buf)->revealed, 16);
    CHECK_EQ(ms_obs_clues(obs_buf)[15], 1);

    /* A duplicate batch for revision 1 is stale and toggles nothing back. */
    test_case("duplicate_batch_is_stale");
    take_view(&env, g, before_buf);
    changed = false;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed),
                 MS_ERR_STALE_REVISION);
    CHECK(!changed);
    CHECK(same_bytes(before_buf, take_view(&env, g, view_buf), 80));
    CHECK_EQ(((const ms_view_header *)view_buf)->flags, 1);
    /* Nothing is stored for revision 2 yet. */
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 2, &available, &changed), MS_OK);
    CHECK(!available && !changed);
    api_close(&env);
}

/* tests.test_autosolve.AutosolveApiTests.test_move_during_calculation_invalidates_the_whole_batch
 * and tests.test_server.ProbabilityTests.test_game_stays_playable_while_solving */
static void test_late_results(void) {
    test_case("late_result_for_old_revision_rejected");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    base_result(&env, g, 1, result_buf);
    add_proof(result_buf, 15, MS_PCELL_PROVEN_SAFE);
    add_proof(result_buf, 16, MS_PCELL_PROVEN_MINE);

    /* The player moves while the worker solves revision 1. */
    act(&env, g, 1, MS_ACTION_FLAG, 4, 4, true);
    take_view(&env, g, before_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352),
                 MS_ERR_STALE_REVISION);
    bool available = true, changed = true;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed),
                 MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 2, &available, &changed), MS_OK);
    CHECK(!available && !changed);
    CHECK(same_bytes(before_buf, take_view(&env, g, view_buf), 80));

    /* A result relabelled with the new revision is bound to the old
     * observation: once a reveal changed the clues its hash no longer
     * matches. (A flag-only move keeps the observation, and the proofs of
     * an identical observation stay sound.) */
    act(&env, g, 2, MS_ACTION_REVEAL, 4, 0, true);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 3, result_buf, 352),
                 MS_ERR_INVALID_RESULT);
    CHECK_EQ(env.engine.stored, 0);
    CHECK(!cached(&env, g, 3));
    api_close(&env);
}

/* tests.test_server.ProbabilityTests.test_solver_inconsistency_is_a_logged_internal_error */
static void test_inconsistent_result(void) {
    test_case("inconsistent_result_is_engine_failure");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    /* The worker's ms_solve failed (MS_ERR_INCONSISTENT): its buffer holds
     * no result. Whatever bytes arrive are rejected and nothing is stored. */
    start_result(&env, g, 1, result_buf); /* status 0: not a result */
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352),
                 MS_ERR_INVALID_RESULT);
    base_memset(result_buf, 0, 352);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352),
                 MS_ERR_INVALID_BUFFER);
    CHECK_EQ(env.engine.stored, 0);
    CHECK(!cached(&env, g, 1)); /* a retry solves again */
    bool available = true;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, NULL), MS_OK);
    CHECK(!available);
    base_result(&env, g, 1, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    CHECK(cached(&env, g, 1));
    api_close(&env);
}

/* ---------------------------------------------------------------- boundaries */

/* tests.test_server.GameActionTests.test_get_and_head_game */
static void test_reading_is_side_effect_free(void) {
    test_case("reading_state_is_side_effect_free");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    base_result(&env, g, 1, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    take_view(&env, g, before_buf);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, obs_copy, 64), MS_OK);
    for (uint32_t k = 0; k < 3; k++) {
        CHECK(same_bytes(before_buf, take_view(&env, g, view_buf), 80));
        CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, obs_buf, 64), MS_OK);
        CHECK(same_bytes(obs_buf, obs_copy, 64));
        CHECK(cached(&env, g, 1));
    }
    CHECK_EQ(((const ms_view_header *)view_buf)->revision, 1);
    api_close(&env);
}

/* Caller buffers are copied in and out; neither side aliases the other. */
static void test_copy_ownership(void) {
    test_case("stored results are engine-owned copies");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    base_result(&env, g, 1, result_buf);
    base_memcpy(result_copy, result_buf, 352);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    base_memset(result_buf, 0x5A, 352);
    CHECK(cached(&env, g, 1));
    CHECK(same_bytes(cached_buf, result_copy, 352));
    base_memset(cached_buf, 0x33, 352);
    CHECK(cached(&env, g, 1));
    CHECK(same_bytes(cached_buf, result_copy, 352));
    /* An unavailable answer leaves the caller's buffer untouched. */
    act(&env, g, 1, MS_ACTION_FLAG, 4, 4, true);
    base_memset(cached_buf, 0x77, 352);
    CHECK(!cached(&env, g, 2));
    for (uint32_t i = 0; i < 352; i++) CHECK_EQ(((const uint8_t *)cached_buf)[i], 0x77);
    api_close(&env);
}

static void test_buffer_checks(void) {
    test_case("buffer checks and their order");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    uint8_t *odd_view = (uint8_t *)view_buf + 4;
    bool available = true;
    /* NULL/misaligned before the revision checks ... */
    CHECK_STATUS(ms_engine_view(&env.engine, 9, NULL, 80), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_view(&env.engine, 9, odd_view, 80), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_observe(&env.engine, 9, 9, (uint8_t *)obs_buf + 4, 64),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, 9, 9, cached_buf, 352, NULL),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, 9, 9, NULL, 352, &available),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, 9, 9, (uint8_t *)result_buf + 4, 352),
                 MS_ERR_INVALID_BUFFER);
    /* ... exact lengths after them. */
    CHECK_STATUS(ms_engine_view(&env.engine, 9, view_buf, 72), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_engine_view(&env.engine, g, view_buf, 72), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_view(&env.engine, g, view_buf, 88), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, obs_buf, 56), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 1, cached_buf, 344, &available),
                 MS_ERR_INVALID_BUFFER);
    base_result(&env, g, 1, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 344),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 360),
                 MS_ERR_INVALID_BUFFER);
    /* Revision ranges. */
    CHECK_STATUS(ms_engine_view(&env.engine, 0, view_buf, 80), MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_engine_view(&env.engine, MS_GENERATION_MAX + 1, view_buf, 80),
                 MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, MS_REVISION_MAX + 1, obs_buf, 64),
                 MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, UINT32_MAX, cached_buf, 352, &available),
                 MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, MS_REVISION_MAX + 1, result_buf, 352),
                 MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_engine_autosolve(&env.engine, 0, 1, NULL, NULL), MS_ERR_INVALID_REVISION);
    /* A result for another board size. */
    api_env other;
    api_open(&other);
    uint32_t go = 0;
    CHECK_STATUS(ms_engine_new_game(&other.engine, 9, 9, 10, 11, &go), MS_OK);
    act(&other, go, 0, MS_ACTION_REVEAL, 4, 4, true);
    start_result(&other, go, 1, result_copy);
    ((ms_result_header *)result_copy)->status = MS_PROB_UNAVAILABLE;
    ((ms_result_header *)result_copy)->reason = MS_REASON_TIME_BUDGET_EXHAUSTED;
    CHECK_STATUS(ms_engine_accept_result(&other.engine, go, 1, result_copy, result_len(&other)),
                 MS_OK);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_copy, result_len(&other)),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_copy, 352),
                 MS_ERR_INVALID_RESULT);
    CHECK_EQ(env.engine.stored, 0);
    api_close(&other);
    api_close(&env);
}

static void test_result_masks_and_counts(void) {
    test_case("malformed proof masks and signed count limits");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    /* Unknown flag bits, both proofs, a value on a revealed cell. */
    const uint8_t bad_flags[3] = {MS_PCELL_VALUE | 0x08u,
                                  MS_PCELL_VALUE | MS_PCELL_PROVEN_SAFE | MS_PCELL_PROVEN_MINE,
                                  0x80u};
    for (uint32_t k = 0; k < 3; k++) {
        base_result(&env, g, 1, result_buf);
        ms_result_flags(result_buf)[20] = bad_flags[k];
        CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352),
                     MS_ERR_INVALID_RESULT);
    }
    base_result(&env, g, 1, result_buf);
    ms_result_flags(result_buf)[3] = MS_PCELL_VALUE;
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352),
                 MS_ERR_INVALID_RESULT);
    CHECK_EQ(env.engine.stored, 0);

    /* Every count field: INT32_MAX passes, INT32_MAX + 1 is "negative". */
    for (uint32_t field = 0; field < COUNT_FIELDS; field++) {
        for (uint32_t over = 0; over < 2; over++) {
            base_result(&env, g, 1, result_buf);
            *count_field((ms_result_header *)result_buf, field) = (uint32_t)INT32_MAX + over;
            CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352),
                         over ? MS_ERR_INVALID_RESULT : MS_OK);
        }
    }
    api_close(&env);
}

static void test_engine_revision_limit(void) {
    test_case("engine at MS_REVISION_MAX: actions and batches refuse to wrap");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    ms_game_test_set_revision(env.engine.game, MS_REVISION_MAX);
    const uint32_t r = MS_REVISION_MAX;
    CHECK_STATUS(ms_engine_act(&env.engine, g, r, MS_ACTION_FLAG, 4, 4, NULL),
                 MS_ERR_RESOURCE_EXHAUSTED);
    base_result(&env, g, r, result_buf);
    add_proof(result_buf, 15, MS_PCELL_PROVEN_SAFE);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, r, result_buf, 352), MS_OK);
    bool available = false, changed = false;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, r, &available, &changed),
                 MS_ERR_RESOURCE_EXHAUSTED);
    CHECK(!available && !changed); /* untouched */
    CHECK(cached(&env, g, r));
    CHECK_EQ(take_view(&env, g, view_buf)->revision, MS_REVISION_MAX);
    api_close(&env);

    /* A stored result belongs to its revision, independently of the
     * invalidation on change (the test hook moves the revision behind the
     * engine's back). */
    api_open(&env);
    g = opened_two_mines(&env);
    base_result(&env, g, 1, result_buf);
    add_proof(result_buf, 15, MS_PCELL_PROVEN_SAFE);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    ms_game_test_set_revision(env.engine.game, 5);
    CHECK(!cached(&env, g, 5));
    available = true;
    changed = true;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 5, &available, &changed), MS_OK);
    CHECK(!available && !changed);
    api_close(&env);
}

/* ---------------------------------------------------------------- aliasing */

/* Caller buffers never alias each other or the engine's own storage: such
 * calls are rejected before anything is written or read. */
static void test_engine_buffer_aliasing(void) {
    test_case("engine buffers: aliasing engine storage or each other is rejected");
    api_env env;
    api_open(&env);
    uint32_t g = opened_two_mines(&env);
    base_result(&env, g, 1, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    base_memcpy(result_copy, env.engine.result, 352);
    take_view(&env, g, before_buf);
    uint8_t *storage = (uint8_t *)env.engine.storage;
    CHECK(env.engine.storage_len >= 352u + 64u);
    bool available = true, changed = true;

    /* The engine's stored result and observation scratch as caller buffers. */
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, env.engine.result, 352),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, env.engine.obs, 352),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 1, env.engine.result, 352, &available),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, env.engine.obs, 64), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_view(&env.engine, g, env.engine.result, 80), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_view(&env.engine, g, storage + env.engine.storage_len - 8, 80),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 1, cached_buf, 352, (bool *)storage),
                 MS_ERR_INVALID_BUFFER);
    CHECK(available);

    /* An availability flag inside its own result buffer. */
    base_memset(cached_buf, 0xA5, 352);
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 1, cached_buf, 352,
                                         (bool *)((uint8_t *)cached_buf + 8)),
                 MS_ERR_INVALID_BUFFER);
    for (uint32_t i = 0; i < 352; i++) CHECK_EQ(((const uint8_t *)cached_buf)[i], 0xA5);

    /* Aliased autosolve outputs. */
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &changed, &changed), MS_ERR_INVALID_BUFFER);
    CHECK(changed);

    /* Nothing changed: stored result, game and its revision. */
    CHECK(same_bytes(env.engine.result, result_copy, 352));
    CHECK_EQ(env.engine.stored, 1);
    CHECK(same_bytes(before_buf, take_view(&env, g, view_buf), 80));
    CHECK(cached(&env, g, 1));
    CHECK(same_bytes(cached_buf, result_copy, 352));
    api_close(&env);
}

/* The reactor's host-buffer table (wasm_buffers.h), the code every
 * WebAssembly export uses to resolve JS pointers, run natively. */
static ms_host_table host_table;

static uint32_t checksum(const void *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) h = (h ^ ((const uint8_t *)data)[i]) * 16777619u;
    return h;
}

static void test_host_buffers(void) {
    test_case("host buffers: ownership, capacity, alignment, wild pointers");
    rt_mem mem, other;
    rt_mem_init(&mem, MS_WASM_BUFFER_BUDGET);
    rt_mem_init(&other, RT_MEM_UNLIMITED);
    ms_host_init(&host_table, &mem);
    CHECK_EQ(ms_host_alloc(&host_table, 0), 0);
    CHECK_EQ(ms_host_alloc(&host_table, MS_WASM_MAX_BUFFER_BYTES + 1u), 0);
    uintptr_t a = ms_host_alloc(&host_table, 100);
    CHECK(a != 0 && a % 16 == 0);
    for (uint32_t i = 0; i < 100; i++) CHECK_EQ(((const uint8_t *)a)[i], 0);

    /* Inside the requested bytes only, at the requested alignment. */
    CHECK(ms_host_span(&host_table, a, 100, 8) == (void *)a);
    CHECK(ms_host_span(&host_table, a + 96, 4, 4) == (void *)(a + 96));
    CHECK(ms_host_span(&host_table, a + 96, 8, 4) == NULL); /* crosses the requested end */
    CHECK(ms_host_span(&host_table, a + 100, 1, 1) == NULL);
    CHECK(ms_host_span(&host_table, a + 4, 8, 8) == NULL);  /* misaligned */
    CHECK(ms_host_span(&host_table, a, 0, 1) == NULL);
    CHECK(ms_host_span(&host_table, 0, 4, 4) == NULL);
    /* Wild pointers, including ranges that would wrap the address space. */
    const uintptr_t wild[5] = {1, 8, MS_UINTPTR_MAX - 7, MS_UINTPTR_MAX - 3, MS_UINTPTR_MAX};
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(ms_host_span(&host_table, wild[i], 4, 1) == NULL);
        CHECK(ms_host_span(&host_table, wild[i], 16, 1) == NULL);
        CHECK_STATUS(ms_host_free(&host_table, wild[i]), MS_ERR_INVALID_BUFFER);
    }
    CHECK(!ms_host_range_ok(MS_UINTPTR_MAX - 3, 8));
    CHECK(ms_host_range_ok(MS_UINTPTR_MAX - 8, 8));
    CHECK(ms_host_span(&host_table, a, SIZE_MAX, 1) == NULL);
    /* A live allocation of another context (e.g. engine storage). */
    void *foreign = rt_alloc(&other, 64);
    CHECK(ms_host_span(&host_table, (uintptr_t)foreign, 8, 8) == NULL);
    CHECK_STATUS(ms_host_free(&host_table, (uintptr_t)foreign), MS_ERR_INVALID_BUFFER);

    /* Frees: interior and repeated frees are refused; 0 is a no-op. */
    CHECK_STATUS(ms_host_free(&host_table, a + 16), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_host_free(&host_table, 0), MS_OK);
    CHECK_STATUS(ms_host_free(&host_table, a), MS_OK);
    CHECK(ms_host_span(&host_table, a, 4, 4) == NULL);
    CHECK_STATUS(ms_host_free(&host_table, a), MS_ERR_INVALID_BUFFER);
    CHECK_EQ(mem.misuses, 0);

    /* The allocator's ownership query is authoritative. */
    uintptr_t b = ms_host_alloc(&host_table, 64);
    rt_free(&mem, (void *)b); /* behind the table's back */
    CHECK(ms_host_span(&host_table, b, 4, 4) == NULL);
    CHECK_STATUS(ms_host_free(&host_table, b), MS_ERR_INTERNAL);

    /* Live-buffer and byte budgets. */
    rt_mem_reset(&mem);
    ms_host_init(&host_table, &mem);
    for (uint32_t i = 0; i < MS_WASM_MAX_BUFFERS; i++) CHECK(ms_host_alloc(&host_table, 16) != 0);
    CHECK_EQ(ms_host_alloc(&host_table, 16), 0);
    CHECK_EQ(host_table.count, MS_WASM_MAX_BUFFERS);
    rt_mem_reset(&mem);
    ms_host_init(&host_table, &mem);
    rt_mem_set_budget(&mem, 4096);
    CHECK(ms_host_alloc(&host_table, 16) != 0);
    CHECK_EQ(ms_host_alloc(&host_table, 16), 0);
    CHECK_EQ(host_table.count, 1);
    CHECK_EQ(mem.misuses, 0);
    rt_mem_dispose(&mem);
    rt_mem_dispose(&other);
}

/* Same-buffer and overlapping calls are refused before anything is written
 * (ms_solve zeroes and fills the result while still reading the observation). */
#define OVERLAP_BUFFER 600u

/* {observation, limits, result} offsets into one host buffer; the valid
 * layout is {0, 64, 120}. Every row stays inside the buffer, so each is
 * refused for overlapping alone. */
static const uint32_t OVERLAPPING[7][3] = {
    {0, 64, 0},    /* result is the observation */
    {0, 64, 64},   /* result is the limits */
    {0, 0, 120},   /* limits are the observation */
    {0, 64, 56},   /* result starts inside the observation */
    {120, 64, 120},/* observation starts inside the result */
    {0, 464, 120}, /* limits overlap the result's end */
    {0, 8, 120},   /* limits start inside the observation */
};

static void test_host_overlaps(void) {
    test_case("host buffers: same-buffer and overlapping requests are rejected");
    rt_mem mem;
    rt_mem_init(&mem, MS_WASM_BUFFER_BUDGET);
    ms_host_init(&host_table, &mem);
    /* One buffer holding observation, limits and result side by side. */
    uintptr_t p = ms_host_alloc(&host_table, OVERLAP_BUFFER);
    uintptr_t obs = p, limits = p + 64, result = p + 120;
    for (uint32_t i = 0; i < OVERLAP_BUFFER; i++) ((uint8_t *)p)[i] = (uint8_t)(i * 7u);
    uint32_t sum = checksum((const void *)p, OVERLAP_BUFFER);
    void *o = NULL, *l = NULL, *r = NULL;
    CHECK_STATUS(ms_host_solve_spans(&host_table, obs, 64, limits, 56, result, 352, &o, &l, &r),
                 MS_OK);
    CHECK(o == (void *)obs && l == (void *)limits && r == (void *)result);

    void *sentinel = (void *)&host_table;
    for (uint32_t k = 0; k < array_size(OVERLAPPING); k++) {
        o = l = r = sentinel;
        CHECK_STATUS(ms_host_solve_spans(&host_table, p + OVERLAPPING[k][0], 64,
                                         p + OVERLAPPING[k][1], 56, p + OVERLAPPING[k][2], 352, &o,
                                         &l, &r),
                     MS_ERR_INVALID_BUFFER);
        CHECK(o == sentinel && l == sentinel && r == sentinel);
    }
    o = l = r = sentinel;
    CHECK_STATUS(ms_host_solve_spans(&host_table, obs, 64, limits, 48, result, 352, &o, &l, &r),
                 MS_ERR_INVALID_BUFFER); /* limits must be MS_WASM_LIMITS_BYTES */
    CHECK_STATUS(ms_host_solve_spans(&host_table, obs, 64, limits, 56, result + 4, 352, &o, &l, &r),
                 MS_ERR_INVALID_BUFFER); /* misaligned and past the end */
    CHECK(o == sentinel && l == sentinel && r == sentinel);

    /* Two-buffer calls (result + availability, availability + changed). */
    void *x = sentinel, *y = sentinel;
    CHECK_STATUS(ms_host_span2(&host_table, result, 352, 8, result + 8, 4, 4, &x, &y),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_host_span2(&host_table, obs, 4, 4, obs, 4, 4, &x, &y), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_host_span2(&host_table, obs, 8, 4, obs + 4, 4, 4, &x, &y),
                 MS_ERR_INVALID_BUFFER);
    CHECK(x == sentinel && y == sentinel);
    CHECK_STATUS(ms_host_span2(&host_table, result, 352, 8, result + 352, 4, 4, &x, &y), MS_OK);
    CHECK(x == (void *)result && y == (void *)(result + 352)); /* adjacent is fine */
    CHECK_STATUS(ms_host_span2(&host_table, obs, 4, 4, obs + 4, 4, 4, &x, &y), MS_OK);

    /* Resolution never writes caller memory. */
    CHECK_EQ(checksum((const void *)p, OVERLAP_BUFFER), sum);
    CHECK_STATUS(ms_host_free(&host_table, p), MS_OK);
    CHECK_EQ(mem.live, 0);
    rt_mem_dispose(&mem);
}

/* Only ms_engine_new_game allocates: with every later request failing, the
 * whole service keeps working. */
static void test_no_allocation_after_new_game(void) {
    test_case("only new games allocate (failure injection)");
    api_env env;
    api_open(&env);
    uint32_t g = two_mines_game(&env);
    rt_mem_set_failure(&env.mem, 0, RT_MEM_FOREVER);
    act(&env, g, 0, MS_ACTION_REVEAL, 0, 0, true);
    CHECK(!cached(&env, g, 1));
    CHECK_STATUS(ms_engine_observe(&env.engine, g, 1, obs_buf, 64), MS_OK);
    base_result(&env, g, 1, result_buf);
    add_proof(result_buf, 15, MS_PCELL_PROVEN_SAFE);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, 352), MS_OK);
    CHECK(cached(&env, g, 1));
    bool available = false, changed = false;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
    CHECK(available && changed);
    CHECK_EQ(env.mem.failures, 0);
    /* A new game is refused cleanly and the current one survives. */
    CHECK_STATUS(ms_engine_new_game(&env.engine, 9, 9, 10, 1, NULL), MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_EQ(take_view(&env, g, view_buf)->revision, 2);
    rt_mem_set_failure(&env.mem, 0, 0);
    api_close(&env);
}

/* ---------------------------------------------------------------- trajectories */

/* tests.test_autosolve.RealAutosolveProgressionTests (engine half): the
 * frozen proof sets drive repeated batches, pauses and the resume after a
 * surviving manual reveal through the service. test_autosolve.c replays the
 * same trajectories with the real solver. */
static void run_fixture_fed(const AutosolveScript *s) {
    test_case_in("fixture_proof_trajectories", s->name);
    api_env env;
    api_open(&env);
    uint32_t g = 0;
    CHECK_STATUS(ms_engine_new_game_with_layout(&env.engine, s->width, s->height, s->mines,
                                                s->layout, s->layout_count, &g),
                 MS_OK);
    act(&env, g, 0, MS_ACTION_REVEAL, s->first_cell / s->width, s->first_cell % s->width, true);
    check_board(take_view(&env, g, view_buf), s->board_after_first);
    uint32_t revision = 1;
    for (uint32_t k = 0; k < s->event_count; k++) {
        const AutosolveEvent *e = &s->events[k];
        if (e->kind == FIXTURE_AUTOSOLVE_BATCH) {
            CHECK_EQ(fixture_runs_size(e->proven_safe, e->proven_safe_runs), e->proven_safe_count);
            CHECK_EQ(fixture_runs_size(e->proven_mines, e->proven_mine_runs), e->proven_mine_count);
            proofs_result(&env, g, revision, result_buf, e->proven_safe, e->proven_safe_runs,
                          e->proven_mines, e->proven_mine_runs);
            CHECK_STATUS(ms_engine_accept_result(&env.engine, g, revision, result_buf,
                                                 result_len(&env)),
                         MS_OK);
            bool available = false, changed = !e->changed;
            CHECK_STATUS(ms_engine_autosolve(&env.engine, g, revision, &available, &changed), MS_OK);
            CHECK(available);
            CHECK(changed == (e->changed != 0));
        } else {
            act(&env, g, revision, MS_ACTION_REVEAL, e->cell / s->width, e->cell % s->width,
                e->changed != 0);
        }
        const ms_view_header *v = take_view(&env, g, view_buf);
        CHECK_EQ(v->status, e->status + MS_GAME_READY);
        CHECK_EQ(v->revision, (uint64_t)e->revision_after);
        CHECK_EQ(v->flags, (uint64_t)e->flags_after);
        CHECK_EQ(v->revealed, e->revealed_count);
        if (e->board != NULL) check_board(view_buf, e->board);
        revision = v->revision;
    }
    api_close(&env);
}

static void test_fixture_fed_trajectories(void) {
    for (uint32_t i = 0; i < AUTOSOLVE_SCRIPT_COUNT; i++) {
        if (AUTOSOLVE_SCRIPTS[i].layout != NULL) run_fixture_fed(&AUTOSOLVE_SCRIPTS[i]);
    }
}

void test_api(void) {
    test_engine_misuse();
    test_failed_new_game_keeps_current_game();
    test_engine_instances_are_independent();
    test_repeated_new_games();
    test_placeholder_not_started();
    test_placeholder_finished();
    test_autosolve_refuses_ready_and_finished_games();
    test_solve_request();
    test_solver_input_public_only();
    test_public_equivalence();
    test_odds_for_stale_revision();
    test_cache_follows_revisions();
    test_engine_result_cases();
    test_complete_results_cached();
    test_unavailable_not_cached();
    test_latest_result_wins();
    test_autosolve_batches();
    test_late_results();
    test_inconsistent_result();
    test_reading_is_side_effect_free();
    test_copy_ownership();
    test_buffer_checks();
    test_result_masks_and_counts();
    test_engine_buffer_aliasing();
    test_host_buffers();
    test_host_overlaps();
    test_engine_revision_limit();
    test_no_allocation_after_new_game();
    test_fixture_fed_trajectories();
}
