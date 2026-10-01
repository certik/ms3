/* Real-solver integration for certainty-only autosolve (c/engine.c +
 * c/probability.c): every solve goes through the same path as the browser -
 * the engine exports the public observation, ms_solve answers it with the
 * default limits (on a frozen fake clock, so budgets other than nodes,
 * samples and memory never cut work short), and the engine accepts the
 * result before applying its proofs as one atomic batch. Ports the
 * c-autosolve destinations of tests/coverage-map.json and the engine's
 * real-solver end-to-end case (RealSolverIntegrationTests). */

#include "test_support.h"

#include "fixtures/autosolve_cases.h"
#include "fixtures/game_scripts.h"
#include "fixtures/probability_cases.h"

#define VIEW_WORDS ((sizeof(ms_view_header) + MS_GAME_MAX_CELLS + 7u) / 8u)
#define OBS_WORDS ((sizeof(ms_obs_header) + MS_GAME_MAX_CELLS + 7u) / 8u)
#define RESULT_WORDS ((sizeof(ms_result_header) + 9u * MS_GAME_MAX_CELLS + 7u) / 8u)

static uint64_t view_buf[VIEW_WORDS];
static uint64_t obs_buf[OBS_WORDS];
static uint64_t result_buf[RESULT_WORDS];
static uint64_t cached_buf[RESULT_WORDS];
static uint32_t layout_buf[MS_GAME_MAX_CELLS];

typedef struct solve_env {
    rt_mem mem;       /* engine: games and stored results */
    rt_mem workspace; /* solver, as the worker instance's pool */
    fake_clock fake;
    rt_clock clock;
    ms_engine engine;
    ms_infer_limits limits;
    uint32_t generation;
} solve_env;

static void env_open(solve_env *env, uint32_t width, uint32_t height, uint32_t mines,
                     const uint32_t *layout, uint32_t layout_count) {
    rt_mem_init(&env->mem, RT_MEM_UNLIMITED);
    rt_mem_init(&env->workspace, (size_t)MS_DEFAULT_MEMORY_BUDGET);
    fake_clock_init(&env->clock, &env->fake, 5000.0, 0.0);
    ms_limits_default(&env->limits);
    CHECK_STATUS(ms_engine_init(&env->engine, &env->mem, &env->clock), MS_OK);
    CHECK_STATUS(ms_engine_new_game_with_layout(&env->engine, width, height, mines, layout,
                                                layout_count, &env->generation),
                 MS_OK);
}

static void env_close(solve_env *env) {
    ms_engine_dispose(&env->engine);
    CHECK_EQ(env->mem.live, 0);
    CHECK_EQ(env->mem.misuses, 0);
    CHECK_EQ(env->workspace.live, 0);
    CHECK_EQ(env->workspace.misuses, 0);
    rt_mem_dispose(&env->mem);
    rt_mem_dispose(&env->workspace);
}

static size_t cells_of(const solve_env *env) {
    return (size_t)env->engine.width * env->engine.height;
}

static size_t obs_len(const solve_env *env) {
    return ms_obs_size(env->engine.width, env->engine.height);
}

static size_t result_len(const solve_env *env) {
    return ms_result_size(env->engine.width, env->engine.height);
}

static const ms_view_header *take_view(solve_env *env) {
    size_t len = ms_view_size(env->engine.width, env->engine.height);
    CHECK_STATUS(ms_engine_view(&env->engine, env->generation, view_buf, len), MS_OK);
    CHECK_STATUS(ms_view_validate(view_buf, len), MS_OK);
    return (const ms_view_header *)view_buf;
}

static char render_cell(uint8_t b) {
    if (b & MS_CELL_REVEALED) {
        return (b & MS_CELL_MINE) ? 'X' : (char)('0' + (b & MS_CELL_ADJACENT_MASK));
    }
    return (b & MS_CELL_FLAGGED) ? 'F' : '#';
}

static void check_render(const char *board, const char *const *rows) {
    const ms_view_header *h = (const ms_view_header *)view_buf;
    for (uint32_t i = 0; i < h->width * h->height; i++) {
        char want = board ? board[i] : fixture_rows_at(rows, h->width, h->height, i);
        char got = render_cell(ms_view_cells(view_buf)[i]);
        if (want != got) {
            test_print("  render mismatch at cell ");
            test_print_u64(i);
            test_print("\n");
            CHECK(want == got);
        }
    }
}

static void act(solve_env *env, uint32_t revision, uint32_t action, uint32_t cell) {
    bool changed = false;
    uint32_t width = env->engine.width;
    CHECK_STATUS(ms_engine_act(&env->engine, env->generation, revision, action, cell / width,
                               cell % width, &changed),
                 MS_OK);
    CHECK(changed);
}

/* What the worker does with the exported observation. */
static void solve(solve_env *env, uint32_t revision) {
    CHECK_STATUS(ms_engine_observe(&env->engine, env->generation, revision, obs_buf, obs_len(env)),
                 MS_OK);
    CHECK_STATUS(ms_solve(obs_buf, obs_len(env), &env->limits, &env->workspace, &env->clock,
                          result_buf, result_len(env)),
                 MS_OK);
    CHECK_STATUS(ms_result_validate_solver(obs_buf, obs_len(env), result_buf, result_len(env)),
                 MS_OK);
    CHECK_EQ(env->workspace.live, 0); /* the solve released its workspace */
}

static uint32_t count_flag(const void *result, uint32_t cells, uint8_t bit) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < cells; i++) count += (ms_result_flags(result)[i] & bit) != 0;
    return count;
}

/* The result's proofs of one kind are exactly the listed cells. */
static void check_proofs(const void *result, uint32_t cells, uint8_t bit, const FixtureRun *runs,
                         uint32_t run_count) {
    for (uint32_t i = 0; i < cells; i++) {
        bool proven = (ms_result_flags(result)[i] & bit) != 0;
        CHECK(proven == fixture_runs_contain(runs, run_count, i));
    }
}

/* The exported observation is exactly the frozen one. */
static void check_observation(const ProbabilityCase *pc) {
    const ms_obs_header *o = (const ms_obs_header *)obs_buf;
    CHECK_EQ(o->width, pc->width);
    CHECK_EQ(o->height, pc->height);
    CHECK_EQ(o->total_mines, pc->total_mines);
    int8_t dense[MS_GAME_MAX_CELLS];
    CHECK(fixture_case_dense_clues(pc, dense, pc->width * pc->height));
    for (uint32_t i = 0; i < pc->width * pc->height; i++) {
        uint8_t clue = ms_obs_clues(obs_buf)[i];
        CHECK_EQ(clue, dense[i] == FIXTURE_HIDDEN_CELL ? MS_CLUE_HIDDEN : (uint8_t)dense[i]);
    }
}

/* Exact odds of every hidden cell against the frozen classes (reference
 * doubles: endpoints exactly, other values within 1e-12 relative). */
static void check_exact_odds(const ProbabilityCase *pc, const void *result) {
    check_observation(pc);
    const ms_result_header *h = (const ms_result_header *)result;
    CHECK_EQ(h->status, MS_PROB_EXACT);
    uint32_t cells = pc->width * pc->height;
    int8_t dense[MS_GAME_MAX_CELLS];
    CHECK(fixture_case_dense_clues(pc, dense, cells));
    for (uint32_t i = 0; i < cells; i++) {
        uint8_t flags = ms_result_flags(result)[i];
        double p = ms_result_probabilities(result)[i];
        if (dense[i] != FIXTURE_HIDDEN_CELL) {
            CHECK_EQ(flags, 0);
            continue;
        }
        int32_t k = fixture_case_class(pc, i);
        CHECK(k >= 0);
        double want = fixture_f64(pc->classes[k].odds.value_bits);
        CHECK(flags & MS_PCELL_VALUE);
        if (want == 0.0 || want == 1.0) {
            CHECK(p == want);
        } else {
            CHECK_NEAR(p, want, 1e-12 * want);
        }
    }
    check_proofs(result, cells, MS_PCELL_PROVEN_SAFE, pc->proven_safe, pc->proven_safe_runs);
    check_proofs(result, cells, MS_PCELL_PROVEN_MINE, pc->proven_mines, pc->proven_mine_runs);
}

/* ---------------------------------------------------------------- end to end */

/* tests.test_server.RealSolverIntegrationTests.test_odds_for_a_forced_position
 * (coverage map: test_api.c::real_solver_two_mines_opened; it lives here
 * because the api suite is built without the solver). */
static void test_real_solver_two_mines_opened(void) {
    test_case("real_solver_two_mines_opened");
    const GameScript *s = &GAME_SCRIPTS[GAME_SCRIPT_REAL_SOLVER_CORRECTS_WRONG_FLAG_AND_WINS];
    const ProbabilityCase *pc = &PROBABILITY_CASES[PROB_CASE_TWO_MINES_OPENED];
    solve_env env;
    env_open(&env, s->width, s->height, s->mines, s->layout, s->layout_count);
    uint32_t g = env.generation;
    act(&env, 0, MS_ACTION_REVEAL, 0);

    /* Every hidden cell is proven (safe 15, 17, 19..24; mines 16, 18) and
     * the exact result is reused for the revision. */
    solve(&env, 1);
    check_exact_odds(pc, result_buf);
    CHECK_EQ(((const ms_result_header *)result_buf)->proven_safe, 8);
    CHECK_EQ(((const ms_result_header *)result_buf)->proven_mines, 2);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, result_len(&env)), MS_OK);
    bool available = false;
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 1, cached_buf, result_len(&env),
                                         &available),
                 MS_OK);
    CHECK(available);
    CHECK(base_memcmp(cached_buf, result_buf, result_len(&env)) == 0);
    env_close(&env);
}

/* tests.test_autosolve.RealAutosolveApiTests.test_real_solver_corrects_wrong_flag_and_wins */
static void test_real_solver_corrects_wrong_flag_and_wins(void) {
    test_case("real_solver_corrects_wrong_flag_and_wins");
    const GameScript *s = &GAME_SCRIPTS[GAME_SCRIPT_REAL_SOLVER_CORRECTS_WRONG_FLAG_AND_WINS];
    const ProbabilityCase *pc = &PROBABILITY_CASES[PROB_CASE_TWO_MINES_OPENED];
    solve_env env;
    env_open(&env, s->width, s->height, s->mines, s->layout, s->layout_count);
    uint32_t g = env.generation;
    act(&env, 0, MS_ACTION_REVEAL, 0);

    /* A wrong flag on safe cell 15 is not evidence: the solver sees the
     * same observation and proves the same cells; one batch clears the
     * flag, flags both mines and wins. */
    act(&env, 1, MS_ACTION_FLAG, 15);
    bool available = true;
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 2, cached_buf, result_len(&env),
                                         &available),
                 MS_OK);
    CHECK(!available);
    solve(&env, 2);
    check_exact_odds(pc, result_buf);
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 2, result_buf, result_len(&env)), MS_OK);
    bool changed = false;
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 2, &available, &changed), MS_OK);
    CHECK(available && changed);
    const GameStep *last = &s->steps[s->step_count - 1];
    const ms_view_header *v = take_view(&env);
    CHECK_EQ(v->status, MS_GAME_WON);
    CHECK_EQ(v->revision, (uint64_t)last->revision_after);
    CHECK_EQ(v->flags, 2);
    check_render(last->board, NULL);
    const uint8_t *cells = ms_view_cells(view_buf);
    CHECK((cells[16] & MS_CELL_FLAGGED) && (cells[18] & MS_CELL_FLAGGED));
    CHECK(!(cells[15] & MS_CELL_FLAGGED));
    env_close(&env);
}

/* ---------------------------------------------------------------- trajectories */

static uint32_t script_layout(const AutosolveScript *s, const uint32_t **layout) {
    if (s->layout != NULL) {
        *layout = s->layout;
        return s->layout_count;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->width * s->height; i++) {
        if (fixture_rows_at(s->layout_rows, s->width, s->height, i) == '*') layout_buf[count++] = i;
    }
    *layout = layout_buf;
    return count;
}

/* tests.test_autosolve.RealAutosolveProgressionTests: repeated batches,
 * pauses with fractional odds left visible, resume after a surviving manual
 * reveal; on 80x80 43 exact batches that never guess. */
static void run_script(const AutosolveScript *s) {
    test_print("  - autosolve_scripts/");
    test_print(s->name);
    test_print("\n");
    const uint32_t *layout = NULL;
    uint32_t layout_count = script_layout(s, &layout);
    CHECK_EQ(layout_count, s->mines);
    solve_env env;
    env_open(&env, s->width, s->height, s->mines, layout, layout_count);
    uint32_t g = env.generation;
    uint32_t cells = (uint32_t)cells_of(&env);
    act(&env, 0, MS_ACTION_REVEAL, s->first_cell);
    take_view(&env);
    if (s->board_after_first != NULL) check_render(s->board_after_first, NULL);
    uint32_t revision = 1;
    for (uint32_t k = 0; k < s->event_count; k++) {
        const AutosolveEvent *e = &s->events[k];
        if (e->kind == FIXTURE_AUTOSOLVE_MANUAL_REVEAL) {
            act(&env, revision, MS_ACTION_REVEAL, e->cell);
        } else {
            bool available = true, changed = !e->changed;
            CHECK_STATUS(ms_engine_cached_result(&env.engine, g, revision, cached_buf,
                                                 result_len(&env), &available),
                         MS_OK);
            CHECK(!available); /* every batch position is new */
            solve(&env, revision);
            const ms_result_header *r = (const ms_result_header *)result_buf;
            if (s->normative) CHECK_EQ(r->status, e->solver_status + MS_PROB_EXACT);
            CHECK_EQ(r->proven_safe, e->proven_safe_count);
            CHECK_EQ(r->proven_mines, e->proven_mine_count);
            if (e->proven_safe != NULL || e->proven_safe_count == 0) {
                check_proofs(result_buf, cells, MS_PCELL_PROVEN_SAFE, e->proven_safe,
                             e->proven_safe_runs);
            }
            if (e->proven_mines != NULL || e->proven_mine_count == 0) {
                check_proofs(result_buf, cells, MS_PCELL_PROVEN_MINE, e->proven_mines,
                             e->proven_mine_runs);
            }
            /* Every proof agrees with the private layout (never a guess). */
            for (uint32_t i = 0; i < cells; i++) {
                uint8_t f = ms_result_flags(result_buf)[i];
                bool mine = false;
                for (uint32_t m = 0; m < layout_count && !mine; m++) mine = layout[m] == i;
                if (f & MS_PCELL_PROVEN_SAFE) CHECK(!mine);
                if (f & MS_PCELL_PROVEN_MINE) CHECK(mine);
            }
            if (e->pause_odds != NULL) check_exact_odds(e->pause_odds, result_buf);
            CHECK_STATUS(ms_engine_accept_result(&env.engine, g, revision, result_buf,
                                                 result_len(&env)),
                         MS_OK);
            CHECK_STATUS(ms_engine_autosolve(&env.engine, g, revision, &available, &changed),
                         MS_OK);
            CHECK(available);
            CHECK(changed == (e->changed != 0));
            if (!changed) {
                /* A pause: nothing certain is left, the odds stay visible. */
                CHECK_EQ(count_flag(result_buf, cells, MS_PCELL_PROVEN_SAFE), 0);
                CHECK_STATUS(ms_engine_cached_result(&env.engine, g, revision, cached_buf,
                                                     result_len(&env), &available),
                             MS_OK);
                CHECK(available == (r->status != MS_PROB_UNAVAILABLE));
            }
        }
        const ms_view_header *v = take_view(&env);
        CHECK(v->status != MS_GAME_LOST);
        CHECK_EQ(v->status, e->status + MS_GAME_READY);
        CHECK_EQ(v->revision, (uint64_t)e->revision_after);
        CHECK_EQ(v->flags, (uint64_t)e->flags_after);
        CHECK_EQ(v->revealed, e->revealed_count);
        if (e->board != NULL) check_render(e->board, NULL);
        revision = v->revision;
    }
    if (s->layout_rows == LARGE_AUTOSOLVE_SEED19_LAYOUT_ROWS) {
        check_render(NULL, LARGE_AUTOSOLVE_SEED19_FINAL_ROWS);
    }
    env_close(&env);
}

static void test_autosolve_scripts(void) {
    for (uint32_t i = 0; i < AUTOSOLVE_SCRIPT_COUNT; i++) run_script(&AUTOSOLVE_SCRIPTS[i]);
}

/* A budget-cut solve still yields sound proofs that the engine accepts and
 * autosolve may apply, while the result itself is never reused. */
static void test_unavailable_solver_proofs(void) {
    test_case("zero budgets: unavailable proofs feed autosolve, never the cache");
    const AutosolveScript *s = &AUTOSOLVE_SCRIPTS[AUTOSOLVE_SCRIPT_BEGINNER_SEED185];
    solve_env env;
    env_open(&env, s->width, s->height, s->mines, s->layout, s->layout_count);
    uint32_t g = env.generation;
    act(&env, 0, MS_ACTION_REVEAL, s->first_cell);
    env.limits.node_budget = 0;
    env.limits.sample_budget = 0;
    solve(&env, 1);
    const ms_result_header *r = (const ms_result_header *)result_buf;
    CHECK_STATUS(ms_engine_accept_result(&env.engine, g, 1, result_buf, result_len(&env)), MS_OK);
    bool available = true, changed = false;
    CHECK_STATUS(ms_engine_cached_result(&env.engine, g, 1, cached_buf, result_len(&env),
                                         &available),
                 MS_OK);
    CHECK(available == (r->status != MS_PROB_UNAVAILABLE));
    /* Its proofs are a subset of the exact first batch's. */
    const AutosolveEvent *first = &s->events[0];
    for (uint32_t i = 0; i < s->width * s->height; i++) {
        uint8_t f = ms_result_flags(result_buf)[i];
        if (f & MS_PCELL_PROVEN_SAFE) {
            CHECK(fixture_runs_contain(first->proven_safe, first->proven_safe_runs, i));
        }
        if (f & MS_PCELL_PROVEN_MINE) {
            CHECK(fixture_runs_contain(first->proven_mines, first->proven_mine_runs, i));
        }
    }
    CHECK_STATUS(ms_engine_autosolve(&env.engine, g, 1, &available, &changed), MS_OK);
    CHECK(available);
    CHECK(changed == (r->proven_safe + r->proven_mines != 0));
    CHECK_EQ(take_view(&env)->status, MS_GAME_PLAYING);
    env_close(&env);
}

void test_autosolve(void) {
    test_real_solver_two_mines_opened();
    test_real_solver_corrects_wrong_flag_and_wins();
    test_autosolve_scripts();
    test_unavailable_solver_proofs();
}
