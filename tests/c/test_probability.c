/* Tests for c/probability.c (ms_solve): the c-probability destinations of
 * tests/coverage-map.json. Exact fixtures (probability_cases.h,
 * large_boards.h) and budgeted sampling fixtures (sampling_cases.h) are
 * frozen reference data; an independent whole-board enumerator written here
 * checks random small observations generated with the C PRNG. Clocks are
 * fake (deterministic): a step of 0 means time never passes, so only node,
 * sample, entry and byte budgets bind. */

#include "test_support.h"

#include "bigint.h"
#include "fixtures/fixture_types.h"
#include "fixtures/large_boards.h"
#include "fixtures/probability_cases.h"
#include "fixtures/sampling_cases.h"

#define MAX_CELLS MS_SOLVER_MAX_CELLS
#define OBS_WORDS ((sizeof(ms_obs_header) + MAX_CELLS + 7u) / 8u)
#define RESULT_WORDS ((sizeof(ms_result_header) + (size_t)MAX_CELLS * 9u + 7u) / 8u)

static uint64_t obs_buf[OBS_WORDS];
static uint64_t obs_copy[OBS_WORDS];
static uint64_t obs_alt[OBS_WORDS];
static uint64_t result_buf[RESULT_WORDS];
static uint64_t result_alt[RESULT_WORDS];
static uint64_t result_ref[RESULT_WORDS];
static int8_t dense[MAX_CELLS];
static int8_t dense_alt[MAX_CELLS];

/* ---------------------------------------------------------------- helpers */

typedef struct solve_env {
    rt_mem mem;
    fake_clock fake;
    rt_clock clock;
    ms_infer_limits limits;
} solve_env;

static void env_open(solve_env *env, double step) {
    rt_mem_init(&env->mem, RT_MEM_UNLIMITED);
    fake_clock_init(&env->clock, &env->fake, 1000.0, step);
    ms_limits_default(&env->limits);
}

static void env_close(solve_env *env) {
    CHECK_EQ(env->mem.live, 0);
    CHECK_EQ(env->mem.live_blocks, 0);
    CHECK_EQ(env->mem.misuses, 0);
    rt_mem_dispose(&env->mem);
}

static uint32_t cells_of(const void *obs) {
    const ms_obs_header *h = (const ms_obs_header *)obs;
    return h->width * h->height;
}

static size_t result_len_of(const void *obs) {
    const ms_obs_header *h = (const ms_obs_header *)obs;
    return ms_result_size(h->width, h->height);
}

static bool is_hidden(const void *obs, uint32_t cell) {
    return ms_obs_clues(obs)[cell] == MS_CLUE_HIDDEN;
}

static const ms_result_header *header(const void *result) {
    return (const ms_result_header *)result;
}

static double prob_at(const void *result, uint32_t cell) {
    return ms_result_probabilities(result)[cell];
}

static uint8_t flag_at(const void *result, uint32_t cell) {
    return ms_result_flags(result)[cell];
}

static uint64_t f64_bits(double value) {
    union {
        double d;
        uint64_t u;
    } pun;
    pun.d = value;
    return pun.u;
}

/* Dense clues (-1 hidden) -> observation. */
static size_t make_obs(void *obs, uint32_t width, uint32_t height, uint32_t total,
                       const int8_t *clues) {
    size_t len = ms_obs_size(width, height);
    CHECK(len > 0);
    CHECK_STATUS(ms_obs_init(obs, len, width, height, total), MS_OK);
    for (uint32_t i = 0; i < width * height; i++) {
        if (clues[i] >= 0) CHECK_STATUS(ms_obs_set_clue(obs, i, (uint8_t)clues[i]), MS_OK);
    }
    CHECK_STATUS(ms_obs_validate(obs, len), MS_OK);
    return len;
}

static size_t case_obs(const ProbabilityCase *c, void *obs) {
    CHECK(fixture_case_dense_clues(c, dense, c->width * c->height));
    return make_obs(obs, c->width, c->height, c->total_mines, dense);
}

/* ms_solve plus the invariants every call must keep: a valid canonical
 * result on MS_OK, a read-only observation, and a workspace returned to
 * exactly its previous live bytes, blocks and budget without misuse. */
static ms_status solve(solve_env *env, const void *obs, size_t obs_len, void *result) {
    size_t live = env->mem.live;
    size_t blocks = env->mem.live_blocks;
    size_t budget = env->mem.budget;
    uint64_t misuses = env->mem.misuses;
    base_memcpy(obs_copy, obs, obs_len);
    size_t result_len = result_len_of(obs);
    ms_status status = ms_solve(obs, obs_len, &env->limits, &env->mem, &env->clock, result,
                                result_len);
    CHECK_EQ(env->mem.live, live);
    CHECK_EQ(env->mem.live_blocks, blocks);
    CHECK_EQ(env->mem.budget, budget);
    CHECK_EQ(env->mem.misuses, misuses);
    CHECK(base_memcmp(obs_copy, obs, obs_len) == 0);
    if (status == MS_OK) {
        CHECK_STATUS(ms_result_validate_solver(obs, obs_len, result, result_len), MS_OK);
    }
    return status;
}

static void print_case(const char *what, const char *name, uint32_t cell) {
    test_print("    ");
    test_print(what);
    test_print(" in ");
    test_print(name);
    test_print(" at cell ");
    test_print_u64(cell);
    test_print("\n");
}

/* Sum of the reported values (meaningful for EXACT/APPROXIMATE). */
static double value_sum(const void *obs, const void *result) {
    double sum = 0.0;
    for (uint32_t i = 0; i < cells_of(obs); i++) {
        if (flag_at(result, i) & MS_PCELL_VALUE) sum += prob_at(result, i);
    }
    return sum;
}

/* Every proof is true in a generating layout ('*' map rows). */
static void check_proofs_match_layout(const ProbabilityCase *c, const void *result) {
    for (uint32_t i = 0; i < c->width * c->height; i++) {
        uint8_t f = flag_at(result, i);
        bool mine = fixture_case_layout_mine(c, i);
        if ((f & MS_PCELL_PROVEN_SAFE) && mine) print_case("false safe proof", c->name, i);
        if ((f & MS_PCELL_PROVEN_MINE) && !mine) print_case("false mine proof", c->name, i);
        CHECK(!((f & MS_PCELL_PROVEN_SAFE) && mine));
        CHECK(!((f & MS_PCELL_PROVEN_MINE) && !mine));
    }
}

/* Truth of a hidden cell from a case's exact classes: -1 unknown, else the
 * reference odds. */
static const FixtureRatio *case_odds(const ProbabilityCase *c, uint32_t cell) {
    int32_t cls = fixture_case_class(c, cell);
    return cls < 0 ? NULL : &c->classes[cls].odds;
}

/* Every proof is certain in the exact truth of the case. */
static void check_proofs_truthful(const ProbabilityCase *c, const void *obs, const void *result) {
    if (c->truth == FIXTURE_TRUTH_UNRECORDED) {
        check_proofs_match_layout(c, result);
        return;
    }
    for (uint32_t i = 0; i < cells_of(obs); i++) {
        uint8_t f = flag_at(result, i);
        if (!(f & (MS_PCELL_PROVEN_SAFE | MS_PCELL_PROVEN_MINE))) continue;
        const FixtureRatio *odds = case_odds(c, i);
        CHECK(odds != NULL);
        if (f & MS_PCELL_PROVEN_SAFE) {
            if (odds->num != 0) print_case("untrue safe proof", c->name, i);
            CHECK(odds->num == 0);
        } else {
            bool certain = odds->den != 0 && odds->num == odds->den;
            if (!certain) print_case("untrue mine proof", c->name, i);
            CHECK(certain);
        }
    }
}

/* Exact result == the case's reference doubles (bit for bit), proofs ==
 * the recorded proof lists, meta == the baseline counts. */
static void check_exact_case(const ProbabilityCase *c, const void *obs, const void *result) {
    const ms_result_header *h = header(result);
    CHECK_EQ(h->status, MS_PROB_EXACT);
    CHECK_EQ(h->reason, MS_REASON_NONE);
    CHECK_EQ(h->frontier_cells, c->meta.frontier_cells);
    CHECK_EQ(h->components, c->meta.components);
    CHECK_EQ(h->unconstrained_cells, c->meta.unconstrained_cells);
    CHECK_EQ(h->exact_components, c->meta.components);
    CHECK_EQ(h->samples, 0);
    CHECK_EQ(h->sample_attempts, 0);
    uint32_t safe = 0, mines = 0;
    for (uint32_t i = 0; i < cells_of(obs); i++) {
        uint8_t f = flag_at(result, i);
        if (!is_hidden(obs, i)) {
            CHECK_EQ(f, 0);
            continue;
        }
        CHECK(f & MS_PCELL_VALUE);
        const FixtureRatio *odds = case_odds(c, i);
        CHECK(odds != NULL);
        double got = prob_at(result, i);
        if (f64_bits(got) != odds->value_bits) {
            print_case("odds differ", c->name, i);
            test_print("    got ");
            test_print_double(got);
            test_print(" want ");
            test_print_double(fixture_f64(odds->value_bits));
            test_print("\n");
        }
        CHECK_EQ(f64_bits(got), odds->value_bits);
        bool want_safe = fixture_runs_contain(c->proven_safe, c->proven_safe_runs, i);
        bool want_mine = fixture_runs_contain(c->proven_mines, c->proven_mine_runs, i);
        if (((f & MS_PCELL_PROVEN_SAFE) != 0) != want_safe ||
            ((f & MS_PCELL_PROVEN_MINE) != 0) != want_mine) {
            print_case("proof differs", c->name, i);
        }
        CHECK(((f & MS_PCELL_PROVEN_SAFE) != 0) == want_safe);
        CHECK(((f & MS_PCELL_PROVEN_MINE) != 0) == want_mine);
        safe += want_safe;
        mines += want_mine;
    }
    CHECK_EQ(h->proven_safe, safe);
    CHECK_EQ(h->proven_mines, mines);
    CHECK_EQ(safe, fixture_runs_size(c->proven_safe, c->proven_safe_runs));
    CHECK_EQ(mines, fixture_runs_size(c->proven_mines, c->proven_mine_runs));
    CHECK_NEAR(value_sum(obs, result), (double)c->total_mines, 1e-6);
}

/* ------------------------------------------------- exact fixture cases */

static void test_probability_cases(void) {
    test_case("probability_cases: exact odds, proofs and meta; contradictions");
    for (uint32_t k = 0; k < PROBABILITY_CASE_COUNT; k++) {
        const ProbabilityCase *c = &PROBABILITY_CASES[k];
        solve_env env;
        env_open(&env, 0.0);
        size_t len = case_obs(c, obs_buf);
        ms_status status = solve(&env, obs_buf, len, result_buf);
        if (c->truth == FIXTURE_TRUTH_INCONSISTENT) {
            if (status != MS_ERR_INCONSISTENT) print_case("expected inconsistent", c->name, 0);
            CHECK_STATUS(status, MS_ERR_INCONSISTENT);
        } else {
            CHECK(c->accepted_statuses == FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT));
            if (status != MS_OK) print_case("solve failed", c->name, 0);
            CHECK_STATUS(status, MS_OK);
            check_exact_case(c, obs_buf, result_buf);
        }
        env_close(&env);
    }
}

static void test_large_boards(void) {
    test_case("large_board_cases: two played 80x80 boards are exact");
    for (uint32_t k = 0; k < LARGE_BOARD_CASE_COUNT; k++) {
        const ProbabilityCase *c = &LARGE_BOARD_CASES[k];
        if (c->truth != FIXTURE_TRUTH_ODDS) continue;
        solve_env env;
        env_open(&env, 0.0);
        size_t len = case_obs(c, obs_buf);
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        check_exact_case(c, obs_buf, result_buf);
        check_proofs_match_layout(c, result_buf);
        CHECK(header(result_buf)->unconstrained_cells > 0);
        CHECK_EQ(header(result_buf)->pair_reasoning_complete, 1);
        env_close(&env);
    }
}

static void test_huge_and_near_certain(void) {
    test_case("huge binomials cancel exactly; near-certain cells are not proofs");
    solve_env env;
    env_open(&env, 0.0);
    /* C(6400, 3200): every cell exactly one half. */
    size_t len = case_obs(&PROBABILITY_CASES[PROB_CASE_PRIOR_80X80_HALF], obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    for (uint32_t i = 0; i < 6400; i++) CHECK(prob_at(result_buf, i) == 0.5);
    /* Corner clue: frontier exactly 1/3, the pool exactly 1999/6396. */
    len = case_obs(&PROBABILITY_CASES[PROB_CASE_CORNER_80X80_2000], obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK(prob_at(result_buf, 1) == 1.0 / 3.0);
    CHECK(prob_at(result_buf, 80) == 1.0 / 3.0);
    CHECK(prob_at(result_buf, 81) == 1.0 / 3.0);
    CHECK(prob_at(result_buf, 6399) == 1999.0 / 6396.0);
    /* Ring of eight 1s around y: pinwheel cells ~2e-11 and y ~1 - 5e-11 are
     * extreme yet unproven; impossible ring cells are proven safe. */
    const ProbabilityCase *ring = &PROBABILITY_CASES[PROB_CASE_NEAR_CERTAIN_80X80];
    len = case_obs(ring, obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    const ms_result_header *h = header(result_buf);
    CHECK_EQ(h->status, MS_PROB_EXACT);
    CHECK_EQ(h->proven_mines, 0);
    CHECK_EQ(h->proven_safe, 8);
    uint32_t y = 40u * 80u + 40u;
    CHECK(prob_at(result_buf, y) < 1.0);
    CHECK(prob_at(result_buf, y) > 0.9999999999);
    CHECK((flag_at(result_buf, y) & MS_PCELL_PROVEN_MINE) == 0);
    uint32_t tiny = 0;
    for (uint32_t i = 0; i < 6400; i++) {
        double p = prob_at(result_buf, i);
        if (is_hidden(obs_buf, i) && p > 0.0 && p < 1e-10) {
            tiny++;
            CHECK((flag_at(result_buf, i) & MS_PCELL_PROVEN_SAFE) == 0);
        }
    }
    CHECK_EQ(tiny, 8);
    env_close(&env);
}

/* Shapes far outside the UI's 5..80 rules are valid solver input. */
static void test_degenerate_boards(void) {
    test_case("degenerate shapes: 1x1, 1x2, 6400x1 and 1x6400 boards");
    solve_env env;
    env_open(&env, 0.0);
    dense[0] = -1;
    size_t len = make_obs(obs_buf, 1, 1, 0, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_EXACT);
    CHECK_EQ(flag_at(result_buf, 0), MS_PCELL_VALUE | MS_PCELL_PROVEN_SAFE);
    len = make_obs(obs_buf, 1, 1, 1, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(flag_at(result_buf, 0), MS_PCELL_VALUE | MS_PCELL_PROVEN_MINE);
    CHECK(prob_at(result_buf, 0) == 1.0);
    dense[0] = 0;
    len = make_obs(obs_buf, 1, 1, 0, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_EXACT);
    CHECK_EQ(header(result_buf)->hidden_cells, 0);
    dense[0] = 1;
    len = make_obs(obs_buf, 1, 1, 0, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_ERR_INCONSISTENT);
    dense[1] = -1;
    len = make_obs(obs_buf, 1, 2, 1, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(flag_at(result_buf, 1), MS_PCELL_VALUE | MS_PCELL_PROVEN_MINE);
    CHECK_EQ(header(result_buf)->propagated_cells, 1);
    for (uint32_t i = 0; i < MAX_CELLS; i++) dense[i] = -1;
    len = make_obs(obs_buf, MAX_CELLS, 1, MAX_CELLS / 2u, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    for (uint32_t i = 0; i < MAX_CELLS; i++) CHECK(prob_at(result_buf, i) == 0.5);
    len = make_obs(obs_buf, 1, MAX_CELLS, MAX_CELLS / 2u, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    for (uint32_t i = 0; i < MAX_CELLS; i++) CHECK(prob_at(result_buf, i) == 0.5);
    env_close(&env);
}

static void test_long_chain(void) {
    test_case("3001-cell chain is exact without recursion");
    solve_env env;
    env_open(&env, 0.0);
    const ProbabilityCase *c = &PROBABILITY_CASES[PROB_CASE_CHAIN_3001_EXACT];
    size_t len = case_obs(c, obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    check_exact_case(c, obs_buf, result_buf);
    for (uint32_t i = 0; i < 3001; i += 2) {
        uint8_t want = (i % 4u == 0) ? MS_PCELL_PROVEN_MINE : MS_PCELL_PROVEN_SAFE;
        CHECK(flag_at(result_buf, i) & want);
    }
    /* 750 mines: only the layout mining 2, 6, ..., 2998 survives (the
     * other end of the component histogram). */
    CHECK(fixture_case_dense_clues(c, dense, 3001));
    len = make_obs(obs_buf, 3001, 1, 750, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_EXACT);
    for (uint32_t i = 0; i < 3001; i += 2) {
        uint8_t want = (i % 4u == 2) ? MS_PCELL_PROVEN_MINE : MS_PCELL_PROVEN_SAFE;
        CHECK(flag_at(result_buf, i) & want);
    }
    /* 752 mines fit neither layout. */
    len = make_obs(obs_buf, 3001, 1, 752, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_ERR_INCONSISTENT);
    env_close(&env);
}

/* ------------------------------------------------- malformed and errors */

static uint32_t raw_u32(int64_t value) {
    return value < 0 || value > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static void test_malformed_observations(void) {
    test_case("malformed_observations: every C-representable case is rejected");
    solve_env env;
    env_open(&env, 0.0);
    for (uint32_t k = 0; k < MALFORMED_OBSERVATION_COUNT; k++) {
        const MalformedObservationCase *m = &MALFORMED_OBSERVATIONS[k];
        CHECK_EQ(m->expected_status, FIXTURE_STATUS_MALFORMED);
        /* Negative budgets are not representable: the C fields are
         * unsigned and the JS adapter rejects them before marshalling. */
        if (m->node_budget < 0 || m->sample_budget < 0) continue;
        uint32_t width = raw_u32(m->width);
        uint32_t height = raw_u32(m->height);
        uint32_t total = raw_u32(m->total_mines);
        uint32_t cells = ms_cell_count(width, height);
        ms_infer_limits limits;
        ms_limits_default(&limits);
        if (m->time_budget_kind == FIXTURE_VALUE_NAN) {
            limits.time_budget_ms = (double)NAN;
        } else if (m->time_budget_kind == FIXTURE_VALUE_FINITE) {
            limits.time_budget_ms = m->time_budget_s * 1000.0;
        }
        /* A raw buffer with the header fields as given. */
        base_memset(obs_buf, 0, sizeof(obs_buf));
        ms_obs_header *h = (ms_obs_header *)obs_buf;
        h->magic = MS_MAGIC_OBSERVATION;
        h->version = MS_ABI_VERSION;
        h->width = width;
        h->height = height;
        h->total_mines = total;
        size_t len = cells ? ms_obs_size(width, height) : ms_pad8(sizeof(ms_obs_header) + 9u);
        if (cells) base_memset(ms_obs_clues(obs_buf), MS_CLUE_HIDDEN, cells);
        bool builder_rejects = false;
        for (uint32_t c = 0; c < m->clue_count; c++) {
            int64_t cell = m->clue_cells[c];
            int64_t value = m->clue_values[c];
            if (cell < 0 || cell >= (int64_t)cells) {
                /* The dense observation cannot hold it; its builder refuses. */
                if (cells) {
                    CHECK_STATUS(ms_obs_set_clue(obs_buf, raw_u32(cell), 1), MS_ERR_INVALID_OBSERVATION);
                }
                builder_rejects = true;
                continue;
            }
            uint8_t byte = value >= 0 && value <= 8 ? (uint8_t)value
                           : value > 8 && value < MS_CLUE_HIDDEN ? (uint8_t)value
                                                                 : (uint8_t)0xFE;
            if (value < 0 || value > 8) {
                CHECK_STATUS(ms_obs_set_clue(obs_buf, (uint32_t)cell, byte),
                             MS_ERR_INVALID_OBSERVATION);
            }
            ms_obs_clues(obs_buf)[cell] = byte;
            h->revealed++; /* a repeated sparse index counts twice */
        }
        if (builder_rejects) continue;
        ms_status status = ms_solve(obs_buf, len, &limits, &env.mem, &env.clock, result_buf,
                                    cells ? ms_result_size(width, height) : sizeof(result_buf));
        if (m->time_budget_kind != FIXTURE_VALUE_FINITE || m->time_budget_s < 0) {
            CHECK_STATUS(status, MS_ERR_INVALID_LIMITS);
        } else {
            CHECK_STATUS(status, MS_ERR_INVALID_OBSERVATION);
        }
        CHECK_EQ(env.mem.live, 0);
    }
    env_close(&env);
}

static void test_error_statuses(void) {
    test_case("error statuses: buffers, limits, inconsistency and budgets are distinct");
    solve_env env;
    env_open(&env, 0.0);
    const ProbabilityCase *ok = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    size_t len = case_obs(ok, obs_buf);
    size_t rlen = result_len_of(obs_buf);
    /* Pointers, alignment and lengths come first. */
    CHECK_STATUS(ms_solve(NULL, len, &env.limits, &env.mem, &env.clock, result_buf, rlen),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_solve(obs_buf, len, NULL, &env.mem, &env.clock, result_buf, rlen),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_solve(obs_buf, len, &env.limits, NULL, &env.clock, result_buf, rlen),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_solve(obs_buf, len, &env.limits, &env.mem, NULL, result_buf, rlen),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_solve(obs_buf, len, &env.limits, &env.mem, &env.clock, NULL, rlen),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_solve(obs_buf, len, &env.limits, &env.mem, &env.clock,
                          (uint8_t *)result_buf + 4, rlen),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_solve(obs_buf, len, &env.limits, &env.mem, &env.clock, result_buf, rlen + 8),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_solve(obs_buf, len + 8, &env.limits, &env.mem, &env.clock, result_buf, rlen),
                 MS_ERR_INVALID_BUFFER);
    /* The result may not overlap the observation. */
    base_memcpy(result_alt, obs_buf, len);
    CHECK_STATUS(ms_solve(result_alt, len, &env.limits, &env.mem, &env.clock, result_alt, rlen),
                 MS_ERR_INVALID_BUFFER);
    ms_infer_limits limits = env.limits;
    limits.magic = 0;
    CHECK_STATUS(ms_solve(obs_buf, len, &limits, &env.mem, &env.clock, result_buf, rlen),
                 MS_ERR_INVALID_BUFFER);
    limits = env.limits;
    limits.flags = 0x80u;
    CHECK_STATUS(ms_solve(obs_buf, len, &limits, &env.mem, &env.clock, result_buf, rlen),
                 MS_ERR_INVALID_LIMITS);
    limits = env.limits;
    limits.min_effective_samples = -1.0;
    CHECK_STATUS(ms_solve(obs_buf, len, &limits, &env.mem, &env.clock, result_buf, rlen),
                 MS_ERR_INVALID_LIMITS);
    env.limits.time_budget_ms = (double)INFINITY; /* unbounded time is valid */
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    check_exact_case(ok, obs_buf, result_buf);
    env.limits.time_budget_ms = MS_DEFAULT_TIME_BUDGET_MS;
    /* Malformed observation content. */
    base_memcpy(obs_alt, obs_buf, len);
    ms_obs_clues(obs_alt)[0] = 9;
    CHECK_STATUS(ms_solve(obs_alt, len, &env.limits, &env.mem, &env.clock, result_buf, rlen),
                 MS_ERR_INVALID_OBSERVATION);
    /* Inconsistent: an error with no result, never an unavailable answer. */
    len = case_obs(&PROBABILITY_CASES[PROB_CASE_INCONSISTENT_TOTAL_UNREACHABLE], obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_ERR_INCONSISTENT);
    /* Budget exhaustion is a successful unavailable result. */
    len = case_obs(&PROBABILITY_CASES[PROB_CASE_CHAIN_BOARD_2_TOTAL_4], obs_buf);
    env.limits.node_budget = 0;
    env.limits.sample_budget = 0;
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_UNAVAILABLE);
    CHECK_EQ(header(result_buf)->reason, MS_REASON_SAMPLING_BUDGET_EXHAUSTED);
    env_close(&env);
}

/* One arena holding several argument objects at chosen byte offsets. */
#define ALIAS_BYTES 4096u
static uint64_t alias_area[ALIAS_BYTES / 8u];
static uint64_t alias_before[ALIAS_BYTES / 8u];

static uint8_t *alias_at(size_t offset) {
    return (uint8_t *)alias_area + offset;
}

/* A rejected call: MS_ERR_INVALID_BUFFER with no byte of the arena (or the
 * observation/result/limits scratch), no clock reading and no workspace
 * request changed. */
static void check_rejected(solve_env *env, const void *obs, size_t obs_len,
                           const ms_infer_limits *limits, rt_mem *workspace, rt_clock *clock,
                           void *result, size_t result_len) {
    base_memcpy(alias_before, alias_area, ALIAS_BYTES);
    base_memcpy(obs_copy, obs_buf, sizeof(obs_buf));
    base_memcpy(result_ref, result_buf, sizeof(result_buf));
    uint32_t reads = env->fake.reads;
    uint64_t requests = env->mem.requests;
    size_t live = env->mem.live, budget = env->mem.budget;
    CHECK_STATUS(ms_solve(obs, obs_len, limits, workspace, clock, result, result_len),
                 MS_ERR_INVALID_BUFFER);
    CHECK(base_memcmp(alias_before, alias_area, ALIAS_BYTES) == 0);
    CHECK(base_memcmp(obs_copy, obs_buf, sizeof(obs_buf)) == 0);
    CHECK(base_memcmp(result_ref, result_buf, sizeof(result_buf)) == 0);
    CHECK_EQ(env->fake.reads, reads);
    CHECK_EQ(env->mem.requests, requests);
    CHECK_EQ(env->mem.live, live);
    CHECK_EQ(env->mem.budget, budget);
}

static void test_aliased_buffers(void) {
    test_case("aliased or wrapping observation/limits/result ranges are refused untouched");
    solve_env env;
    env_open(&env, 1.0);
    const ProbabilityCase *c = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    CHECK(fixture_case_dense_clues(c, dense, c->width * c->height));
    size_t olen = ms_obs_size(c->width, c->height);
    size_t rlen = ms_result_size(c->width, c->height);
    size_t llen = sizeof(ms_infer_limits);
    CHECK(olen % 8u == 0 && rlen % 8u == 0 && rlen > 64u + llen);
    for (uint32_t i = 0; i < ALIAS_BYTES / 8u; i++) alias_area[i] = 0x5A5A5A5A5A5A5A5Aull;
    make_obs(obs_buf, c->width, c->height, c->total_mines, dense);
    ms_infer_limits *limits = &env.limits;

    /* The result is the observation, starts inside it, or contains it. */
    uint8_t *o = alias_at(0);
    make_obs(o, c->width, c->height, c->total_mines, dense);
    check_rejected(&env, o, olen, limits, &env.mem, &env.clock, o, rlen);
    check_rejected(&env, o, olen, limits, &env.mem, &env.clock, o + 8, rlen);
    check_rejected(&env, o, olen, limits, &env.mem, &env.clock, o + olen - 8, rlen);
    make_obs(alias_at(64), c->width, c->height, c->total_mines, dense);
    check_rejected(&env, alias_at(64), olen, limits, &env.mem, &env.clock, alias_at(0), rlen);
    check_rejected(&env, alias_at(64), olen, limits, &env.mem, &env.clock, alias_at(8), rlen);

    /* The limits share bytes with the result or the observation. */
    ms_limits_default((ms_infer_limits *)alias_at(1024 + 64));
    check_rejected(&env, obs_buf, olen, (const ms_infer_limits *)alias_at(1024 + 64), &env.mem,
                   &env.clock, alias_at(1024), rlen);
    ms_limits_default((ms_infer_limits *)alias_at(1024 + rlen - 8));
    check_rejected(&env, obs_buf, olen, (const ms_infer_limits *)alias_at(1024 + rlen - 8),
                   &env.mem, &env.clock, alias_at(1024), rlen);
    make_obs(alias_at(2048), c->width, c->height, c->total_mines, dense);
    ms_limits_default((ms_infer_limits *)alias_at(2048 + olen - 8));
    check_rejected(&env, alias_at(2048), olen, (const ms_infer_limits *)alias_at(2048 + olen - 8),
                   &env.mem, &env.clock, result_buf, rlen);

    /* The result or an input covers the workspace or the clock state. An
     * unchecked call would allocate through, or call into, these structs. */
    rt_mem *inner_mem = (rt_mem *)alias_at(3072 + 16);
    rt_mem_init(inner_mem, RT_MEM_UNLIMITED);
    check_rejected(&env, obs_buf, olen, limits, inner_mem, &env.clock, alias_at(3072), rlen);
    CHECK_EQ(inner_mem->requests, 0);
    rt_mem_dispose(inner_mem);
    fake_clock inner_fake;
    rt_clock *inner_clock = (rt_clock *)alias_at(3072 + rlen - sizeof(rt_clock));
    fake_clock_init(inner_clock, &inner_fake, 0.0, 1.0);
    check_rejected(&env, obs_buf, olen, limits, &env.mem, inner_clock, alias_at(3072), rlen);
    CHECK_EQ(inner_fake.reads, 0);
    /* Valid limits written over the clock: only the range check stands
     * between this call and a jump through the clobbered callback. */
    ms_limits_default((ms_infer_limits *)alias_at(3072 + rlen - llen));
    check_rejected(&env, obs_buf, olen, (const ms_infer_limits *)alias_at(3072 + rlen - llen),
                   &env.mem, inner_clock, result_buf, rlen);

    /* Ranges that wrap the address space are refused without being read. */
    uintptr_t top = MS_UINTPTR_MAX - 15u; /* 16-byte aligned, 16 bytes left */
    check_rejected(&env, (const void *)top, olen, limits, &env.mem, &env.clock, result_buf, rlen);
    check_rejected(&env, obs_buf, olen, (const ms_infer_limits *)top, &env.mem, &env.clock,
                   result_buf, rlen);
    check_rejected(&env, obs_buf, olen, limits, &env.mem, &env.clock, (void *)top, rlen);
    check_rejected(&env, obs_buf, SIZE_MAX, limits, &env.mem, &env.clock, result_buf, rlen);
    check_rejected(&env, obs_buf, olen, limits, &env.mem, &env.clock, result_buf, SIZE_MAX);
    /* Empty ranges are refused before anything is read, as by the reactor. */
    check_rejected(&env, obs_buf, 0, limits, &env.mem, &env.clock, result_buf, rlen);
    check_rejected(&env, obs_buf, olen, limits, &env.mem, &env.clock, result_buf, 0);

    /* Adjacent but disjoint ranges in one arena are fine. */
    for (uint32_t i = 0; i < ALIAS_BYTES / 8u; i++) alias_area[i] = 0;
    uint8_t *adj_obs = alias_at(0);
    ms_infer_limits *adj_limits = (ms_infer_limits *)alias_at(olen);
    uint8_t *adj_result = alias_at(olen + llen);
    make_obs(adj_obs, c->width, c->height, c->total_mines, dense);
    ms_limits_default(adj_limits);
    base_memcpy(obs_copy, adj_obs, olen);
    CHECK_STATUS(ms_solve(adj_obs, olen, adj_limits, &env.mem, &env.clock, adj_result, rlen),
                 MS_OK);
    CHECK(base_memcmp(obs_copy, adj_obs, olen) == 0);
    CHECK_EQ(adj_limits->magic, MS_MAGIC_LIMITS);
    CHECK_STATUS(ms_result_validate_solver(adj_obs, olen, adj_result, rlen), MS_OK);
    check_exact_case(c, adj_obs, adj_result);
    /* The result before the observation, touching it. */
    uint8_t *pre_result = alias_at(1024);
    uint8_t *post_obs = alias_at(1024 + rlen);
    make_obs(post_obs, c->width, c->height, c->total_mines, dense);
    CHECK_STATUS(ms_solve(post_obs, olen, limits, &env.mem, &env.clock, pre_result, rlen), MS_OK);
    check_exact_case(c, post_obs, pre_result);
    env_close(&env);
}

/* Python's clues_for: mines among each shown cell's neighbors. */
static void clues_for(uint32_t width, uint32_t height, const uint32_t *mines, uint32_t count,
                      const uint32_t *shown, uint32_t shown_count, int8_t *out) {
    for (uint32_t i = 0; i < width * height; i++) out[i] = -1;
    for (uint32_t s = 0; s < shown_count; s++) {
        uint32_t cell = shown[s];
        int32_t row = (int32_t)(cell / width), col = (int32_t)(cell % width);
        int8_t clue = 0;
        for (uint32_t m = 0; m < count; m++) {
            int32_t r = (int32_t)(mines[m] / width), c = (int32_t)(mines[m] % width);
            int32_t dr = r - row, dc = c - col;
            if (mines[m] != cell && dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1) clue++;
        }
        out[cell] = clue;
    }
}

static void test_public_equivalence(void) {
    test_case("public_equivalence_4x4: identical observations, identical answers");
    clues_for(4, 4, PUBLIC_EQUIVALENCE_4X4_LAYOUT_A, 3, PUBLIC_EQUIVALENCE_4X4_SHOWN, 6, dense);
    clues_for(4, 4, PUBLIC_EQUIVALENCE_4X4_LAYOUT_B, 3, PUBLIC_EQUIVALENCE_4X4_SHOWN, 6, dense_alt);
    CHECK(base_memcmp(dense, dense_alt, 16) == 0);
    solve_env env;
    env_open(&env, 0.0);
    size_t len = make_obs(obs_buf, 4, 4, 3, dense);
    size_t len_alt = make_obs(obs_alt, 4, 4, 3, dense_alt);
    CHECK(base_memcmp(obs_buf, obs_alt, len) == 0);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_STATUS(solve(&env, obs_alt, len_alt, result_alt), MS_OK);
    CHECK(base_memcmp(result_buf, result_alt, result_len_of(obs_buf)) == 0);
    check_exact_case(&PROBABILITY_CASES[PROB_CASE_PUBLIC_EQUIVALENCE_4X4], obs_buf, result_buf);
    env_close(&env);
}

/* ------------------------------------- independent whole-board oracle */

#define ORACLE_MAX_HIDDEN 24u

typedef struct oracle {
    uint32_t nh;
    uint32_t hidden[ORACLE_MAX_HIDDEN];
    uint64_t layouts;
    uint64_t count[ORACLE_MAX_HIDDEN];
} oracle;

static uint32_t popcount64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return (uint32_t)((x * 0x0101010101010101ull) >> 56);
}

/* Enumerates every placement of `total` mines on the hidden cells and keeps
 * those matching every clue; false if there are too many hidden cells. */
static bool oracle_run(uint32_t width, uint32_t height, uint32_t total, const int8_t *clues,
                       oracle *o) {
    uint32_t n = width * height;
    static uint32_t index_of[MAX_CELLS];
    static uint64_t clue_mask[MAX_CELLS];
    static uint32_t clue_cell[MAX_CELLS];
    o->nh = 0;
    o->layouts = 0;
    for (uint32_t i = 0; i < n; i++) {
        index_of[i] = UINT32_MAX;
        if (clues[i] >= 0) continue;
        if (o->nh >= ORACLE_MAX_HIDDEN) return false;
        index_of[i] = o->nh;
        o->hidden[o->nh++] = i;
    }
    uint32_t nclues = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (clues[i] < 0) continue;
        int32_t row = (int32_t)(i / width), col = (int32_t)(i % width);
        uint64_t mask = 0;
        for (int32_t dr = -1; dr <= 1; dr++) {
            for (int32_t dc = -1; dc <= 1; dc++) {
                int32_t r = row + dr, c = col + dc;
                if ((dr == 0 && dc == 0) || r < 0 || c < 0 || r >= (int32_t)height ||
                    c >= (int32_t)width) {
                    continue;
                }
                uint32_t j = (uint32_t)r * width + (uint32_t)c;
                if (index_of[j] != UINT32_MAX) mask |= 1ull << index_of[j];
            }
        }
        clue_mask[nclues] = mask;
        clue_cell[nclues++] = i;
    }
    for (uint32_t k = 0; k < o->nh; k++) o->count[k] = 0;
    if (total > o->nh) return true;
    uint64_t limit = 1ull << o->nh;
    uint64_t mask = total == 0 ? 0 : (1ull << total) - 1u;
    while (mask < limit) {
        bool ok = true;
        for (uint32_t k = 0; k < nclues && ok; k++) {
            ok = popcount64(mask & clue_mask[k]) == (uint32_t)clues[clue_cell[k]];
        }
        if (ok) {
            o->layouts++;
            for (uint32_t k = 0; k < o->nh; k++) o->count[k] += (mask >> k) & 1u;
        }
        if (total == 0) break;
        uint64_t low = mask & (0u - mask); /* Gosper: next mask, same popcount */
        uint64_t ripple = mask + low;
        mask = (((ripple ^ mask) >> 2) / low) | ripple;
    }
    return true;
}

/* The solve of (width, height, total, clues) equals the oracle: exact,
 * correctly rounded values, 0/1 exactly for certain cells and proofs equal
 * to the certain cells; or inconsistent when no layout exists. */
static void check_against_oracle(solve_env *env, uint32_t width, uint32_t height, uint32_t total,
                                 const int8_t *clues, const char *label) {
    oracle o;
    CHECK(oracle_run(width, height, total, clues, &o));
    size_t len = make_obs(obs_buf, width, height, total, clues);
    ms_status status = solve(env, obs_buf, len, result_buf);
    if (o.layouts == 0) {
        if (status != MS_ERR_INCONSISTENT) print_case("expected inconsistent", label, 0);
        CHECK_STATUS(status, MS_ERR_INCONSISTENT);
        return;
    }
    CHECK_STATUS(status, MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_EXACT);
    double sum = 0.0;
    for (uint32_t k = 0; k < o.nh; k++) {
        uint32_t cell = o.hidden[k];
        double want = (double)o.count[k] / (double)o.layouts; /* both < 2^53: exact rounding */
        double got = prob_at(result_buf, cell);
        uint8_t f = flag_at(result_buf, cell);
        if (got != want) print_case("oracle odds differ", label, cell);
        CHECK(got == want);
        CHECK(((f & MS_PCELL_PROVEN_SAFE) != 0) == (o.count[k] == 0));
        CHECK(((f & MS_PCELL_PROVEN_MINE) != 0) == (o.count[k] == o.layouts));
        sum += got;
    }
    CHECK_NEAR(sum, (double)total, 1e-9);
}

/* Python's random_observation with the C generator: a random layout, then a
 * random subset of its safe cells revealed (at most 14 hidden cells). */
static void random_observation(rt_rng *rng, uint32_t *width, uint32_t *height, uint32_t *total,
                               int8_t *clues, uint32_t max_hidden) {
    static const uint32_t sizes[][2] = {{3, 3}, {4, 3}, {4, 4}, {5, 3}, {1, 9}, {2, 6}, {6, 2},
                                        {5, 4}, {6, 3}, {7, 3}, {5, 5}, {11, 2}, {3, 7}};
    uint32_t kinds = max_hidden > 14u ? 13u : 8u;
    uint32_t pick = rt_rng_below(rng, kinds);
    uint32_t w = sizes[pick][0], h = sizes[pick][1], n = w * h;
    uint32_t top = n / 3u > 1u ? n / 3u : 1u;
    uint32_t mines = 1u + rt_rng_below(rng, top);
    uint32_t cells[64], safe[64];
    bool is_mine[64];
    for (uint32_t i = 0; i < n; i++) {
        cells[i] = i;
        is_mine[i] = false;
    }
    rt_rng_choose(rng, cells, n, mines);
    for (uint32_t m = 0; m < mines; m++) is_mine[cells[m]] = true;
    uint32_t nsafe = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!is_mine[i]) safe[nsafe++] = i;
    }
    uint32_t minimum = n > max_hidden ? n - max_hidden : 0u;
    uint32_t lo = minimum < nsafe ? minimum : nsafe;
    uint32_t shown = lo + rt_rng_below(rng, nsafe - lo + 1u);
    rt_rng_choose(rng, safe, nsafe, shown);
    clues_for(w, h, cells, mines, safe, shown, clues);
    *width = w;
    *height = h;
    *total = mines;
}

static void test_random_boards(void) {
    test_case("random_boards_match_c_brute_force (C PRNG observations)");
    solve_env env;
    env_open(&env, 0.0);
    rt_rng rng;
    rt_rng_seed(&rng, 0x5EEDB0A2D5ull);
    for (uint32_t round = 0; round < 400u; round++) {
        uint32_t width, height, total;
        random_observation(&rng, &width, &height, &total, dense, round < 250u ? 14u : 20u);
        check_against_oracle(&env, width, height, total, dense, "random board");
        /* The same clues with every other mine total: global coupling,
         * pool weights and contradictions of the total. */
        uint32_t n = width * height, hidden = 0;
        for (uint32_t i = 0; i < n; i++) hidden += dense[i] < 0;
        if (round % 8u == 0 && hidden <= 14u) {
            for (uint32_t t = 0; t <= hidden; t++) {
                check_against_oracle(&env, width, height, t, dense, "random board, other total");
            }
        }
    }
    /* The small fixture observations against the C oracle as well. */
    for (uint32_t k = 0; k < PROBABILITY_CASE_COUNT; k++) {
        const ProbabilityCase *c = &PROBABILITY_CASES[k];
        uint32_t n = c->width * c->height;
        if (n > 64u) continue;
        CHECK(fixture_case_dense_clues(c, dense, n));
        oracle o;
        if (!oracle_run(c->width, c->height, c->total_mines, dense, &o)) continue;
        if (c->layouts.len) {
            CHECK(c->layouts.len <= 2u);
            uint64_t z = c->layouts.limbs[0];
            if (c->layouts.len == 2u) z |= (uint64_t)c->layouts.limbs[1] << 32;
            CHECK_EQ(o.layouts, z);
        }
        check_against_oracle(&env, c->width, c->height, c->total_mines, dense, c->name);
    }
    env_close(&env);
}

/* -------------------------------------------------------------- sampling */

static uint32_t fixture_status_of(uint32_t status) {
    switch (status) {
    case MS_PROB_EXACT: return FIXTURE_STATUS_EXACT;
    case MS_PROB_APPROXIMATE: return FIXTURE_STATUS_APPROXIMATE;
    case MS_PROB_UNAVAILABLE: return FIXTURE_STATUS_UNAVAILABLE;
    default: return FIXTURE_STATUS_COUNT;
    }
}

/* ms_prob_reason -> FixtureReason (MEMORY_BUDGET_EXHAUSTED is C-only). */
static uint32_t fixture_reason_of(uint32_t reason) {
    if (reason <= MS_REASON_TIME_BUDGET_EXHAUSTED) return reason;
    if (reason == MS_REASON_NOT_STARTED) return FIXTURE_REASON_NOT_STARTED;
    if (reason == MS_REASON_GAME_OVER) return FIXTURE_REASON_GAME_OVER;
    return FIXTURE_REASON_COUNT;
}

/* Approximate values within `sigmas` standard errors (0.5 / sqrt(ESS)) of
 * the exact truth of the observation. */
static void check_estimates(const ProbabilityCase *pc, const void *obs, const void *result,
                            double sigmas, const char *label) {
    double ess = header(result)->effective_sample_size;
    double limit = sigmas * 0.5 * sigmas * 0.5;
    for (uint32_t i = 0; i < cells_of(obs); i++) {
        if (!is_hidden(obs, i)) continue;
        const FixtureRatio *odds = case_odds(pc, i);
        CHECK(odds != NULL);
        double diff = prob_at(result, i) - fixture_f64(odds->value_bits);
        if (diff * diff * ess > limit) {
            print_case("estimate outside tolerance", label, i);
            test_print("    got ");
            test_print_double(prob_at(result, i));
            test_print(" truth ");
            test_print_double(fixture_f64(odds->value_bits));
            test_print(" ess ");
            test_print_double(ess);
            test_print("\n");
        }
        CHECK(diff * diff * ess <= limit);
    }
}

static void check_sampling_result(const SamplingCase *sc, const void *obs, const void *result) {
    const ProbabilityCase *pc = sc->observation;
    const ms_result_header *h = header(result);
    uint32_t fstatus = fixture_status_of(h->status);
    uint32_t freason = fixture_reason_of(h->reason);
    if (!(sc->accepted_statuses & FIXTURE_STATUS_BIT(fstatus)) || freason >= FIXTURE_REASON_COUNT ||
        !(sc->accepted_reasons & FIXTURE_REASON_BIT(freason))) {
        print_case("unexpected status/reason", sc->name, 0);
        test_print("    status ");
        test_print_u64(h->status);
        test_print(" reason ");
        test_print_u64(h->reason); /* a number: ms_prob_reason_name is NULL out of range */
        test_print("\n");
    }
    CHECK(sc->accepted_statuses & FIXTURE_STATUS_BIT(fstatus));
    CHECK(freason < FIXTURE_REASON_COUNT);
    CHECK(sc->accepted_reasons & FIXTURE_REASON_BIT(freason));
    if (sc->proofs_rule == FIXTURE_PROOFS_EXACT_MATCH) {
        for (uint32_t i = 0; i < cells_of(obs); i++) {
            uint8_t f = flag_at(result, i);
            CHECK(((f & MS_PCELL_PROVEN_SAFE) != 0) ==
                  fixture_runs_contain(sc->proven_safe, sc->proven_safe_runs, i));
            CHECK(((f & MS_PCELL_PROVEN_MINE) != 0) ==
                  fixture_runs_contain(sc->proven_mines, sc->proven_mine_runs, i));
        }
    } else if (sc->proofs_rule == FIXTURE_PROOFS_EMPTY) {
        CHECK_EQ(h->proven_safe, 0);
        CHECK_EQ(h->proven_mines, 0);
    } else {
        check_proofs_truthful(pc, obs, result);
    }
    if (h->status != MS_PROB_UNAVAILABLE) {
        CHECK_NEAR(value_sum(obs, result), (double)pc->total_mines,
                   sc->mine_sum_tolerance * (pc->total_mines > 1u ? pc->total_mines : 1u));
    }
    if (h->status == MS_PROB_APPROXIMATE) {
        CHECK(h->has_effective_sample_size);
        CHECK(h->effective_sample_size >= sc->min_effective_samples);
        if (sc->tolerance_sigmas > 0.0 && pc->truth == FIXTURE_TRUTH_ODDS) {
            check_estimates(pc, obs, result, sc->tolerance_sigmas, sc->name);
        }
    }
    if (sc->exact_effective_samples >= 0.0) {
        CHECK(h->has_effective_sample_size);
        CHECK(h->effective_sample_size == sc->exact_effective_samples);
    }
    CHECK(h->exact_components >= sc->min_exact_components);
    CHECK(h->sampled_components >= sc->min_sampled_components);
    if (sc->time_budget_ms) CHECK(h->elapsed_ms <= (double)sc->time_budget_ms + 500.0);
    /* Proposals come only from the shared sample budget; with none, not one
     * is made (the baseline asserted meta.samples == 0) and no ESS exists. */
    CHECK(h->sample_attempts <= sc->sample_budget);
    CHECK(h->samples <= h->sample_attempts);
    if (sc->sample_budget == 0) {
        if (h->samples != 0 || h->sample_attempts != 0 || h->has_effective_sample_size != 0) {
            test_print("    proposals without a sample budget in ");
            test_print(sc->name);
            test_print("\n");
        }
        CHECK_EQ(h->samples, 0);
        CHECK_EQ(h->sample_attempts, 0);
        CHECK_EQ(h->has_effective_sample_size, 0);
    }
}

static void sampling_limits(solve_env *env, const SamplingCase *sc) {
    env->limits.node_budget = sc->node_budget;
    env->limits.sample_budget = sc->sample_budget;
    env->limits.time_budget_ms = sc->time_budget_ms ? (double)sc->time_budget_ms : 60000.0;
}

static void test_sampling_cases(void) {
    test_case("sampling_cases: statuses, reasons, proofs, ESS and 5-sigma estimates");
    for (uint32_t k = 0; k < SAMPLING_CASE_COUNT; k++) {
        const SamplingCase *sc = &SAMPLING_CASES[k];
        solve_env env;
        /* Timed cases advance the fake clock 0.01 ms per reading. */
        env_open(&env, sc->time_budget_ms ? 0.01 : 0.0);
        sampling_limits(&env, sc);
        size_t len = case_obs(sc->observation, obs_buf);
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        check_sampling_result(sc, obs_buf, result_buf);
        if (sc->observation->truth == FIXTURE_TRUTH_UNRECORDED) {
            CHECK(header(result_buf)->frontier_cells > 1000u);
        }
        env_close(&env);
    }
}

static void test_sampled_certainties(void) {
    test_case("chain_3001_sampled: sampled 0/1 estimates are never proofs");
    solve_env env;
    env_open(&env, 0.0);
    const SamplingCase *sc = &SAMPLING_CASES[SAMPLING_CASE_CHAIN_3001_SAMPLED];
    sampling_limits(&env, sc);
    size_t len = case_obs(sc->observation, obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    const ms_result_header *h = header(result_buf);
    CHECK_EQ(h->status, MS_PROB_APPROXIMATE);
    CHECK_EQ(h->proven_safe + h->proven_mines, 0);
    for (uint32_t i = 0; i < 3001; i += 2) {
        CHECK(prob_at(result_buf, i) == (i % 4u == 0 ? 1.0 : 0.0));
        CHECK_EQ(flag_at(result_buf, i), MS_PCELL_VALUE);
    }
    CHECK_NEAR(value_sum(obs_buf, result_buf), 751.0, 1e-9);
    env_close(&env);
}

static void test_sampling_determinism(void) {
    test_case("sampling is deterministic for a seed; the default seed is the observation's");
    solve_env env;
    env_open(&env, 0.0);
    const SamplingCase *sc = &SAMPLING_CASES[SAMPLING_CASE_RANDOM_SEED_134_SAMPLED];
    sampling_limits(&env, sc);
    size_t len = case_obs(sc->observation, obs_buf);
    size_t rlen = result_len_of(obs_buf);
    env.limits.flags = MS_LIMIT_EXPLICIT_SEED;
    env.limits.seed = 0x0123456789ABCDEFull;
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_APPROXIMATE);
    CHECK(header(result_buf)->samples > 0);
    CHECK(header(result_buf)->sampled_components > 0);
    check_sampling_result(sc, obs_buf, result_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_alt), MS_OK);
    CHECK(base_memcmp(result_buf, result_alt, rlen) == 0);
    env.limits.seed = 0x0123456789ABCDEEull;
    CHECK_STATUS(solve(&env, obs_buf, len, result_alt), MS_OK);
    check_sampling_result(sc, obs_buf, result_alt);
    CHECK(base_memcmp(result_buf, result_alt, rlen) != 0);
    /* Without a seed: ms_obs_hash(obs), so repeated solves agree. */
    env.limits.flags = 0;
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_STATUS(solve(&env, obs_buf, len, result_alt), MS_OK);
    CHECK(base_memcmp(result_buf, result_alt, rlen) == 0);
    env.limits.flags = MS_LIMIT_EXPLICIT_SEED;
    env.limits.seed = ms_obs_hash(obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_alt), MS_OK);
    CHECK(base_memcmp(result_buf, result_alt, rlen) == 0);
    env_close(&env);
}

static void test_incompatible_samples(void) {
    test_case("globally incompatible samples are unavailable, never an inconsistency proof");
    solve_env env;
    env_open(&env, 0.0);
    const SamplingCase *sc = &SAMPLING_CASES[SAMPLING_CASE_GLOBALLY_INCOMPATIBLE];
    sampling_limits(&env, sc);
    env.limits.flags = MS_LIMIT_EXPLICIT_SEED;
    size_t len = case_obs(sc->observation, obs_buf);
    uint32_t incompatible = 0;
    for (uint64_t seed = 1; seed <= 16; seed++) {
        env.limits.seed = seed;
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        check_sampling_result(sc, obs_buf, result_buf);
        const ms_result_header *h = header(result_buf);
        CHECK_EQ(h->status, MS_PROB_UNAVAILABLE);
        CHECK_EQ(h->samples, 12);
        CHECK_EQ(h->sample_attempts, 12);
        incompatible += h->reason == MS_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES;
    }
    CHECK(incompatible >= 15u);
    env_close(&env);
}

/* An exactly counted component conditioned on a sampled one: its globally
 * reweighted numerator can be 0 or Z without the cell being certain, so
 * only structural proofs may be listed in an approximate answer. */
static void test_reweighted_endpoints_are_not_proofs(void) {
    test_case("approximate: reweighted 0/Z of an exact component is not a proof");
    solve_env env;
    env_open(&env, 0.0);
    /* ?1?1???1?1?? with 6 mines: exactly, every cell is certain. */
    const ProbabilityCase *c = &PROBABILITY_CASES[PROB_CASE_COMPONENTS_TOTAL_6];
    size_t len = case_obs(c, obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    check_exact_case(c, obs_buf, result_buf);
    uint32_t both = header(result_buf)->nodes;
    CHECK(both % 2u == 0);
    /* Nodes for the first component only: {0,2,4} exact, {6,8,10} sampled. */
    env.limits.node_budget = both / 2u;
    env.limits.sample_budget = 400;
    env.limits.flags = MS_LIMIT_EXPLICIT_SEED;
    env.limits.seed = 99;
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    const ms_result_header *h = header(result_buf);
    CHECK_EQ(h->status, MS_PROB_APPROXIMATE);
    CHECK_EQ(h->exact_components, 1);
    CHECK_EQ(h->sampled_components, 1);
    CHECK_EQ(h->proven_safe + h->proven_mines, 0);
    static const uint32_t cells[] = {0, 2, 4, 5, 6, 8, 10, 11};
    static const double want[] = {1.0, 0.0, 1.0, 1.0, 1.0, 0.0, 1.0, 1.0};
    for (uint32_t k = 0; k < array_size(cells); k++) {
        CHECK(prob_at(result_buf, cells[k]) == want[k]);
        CHECK_EQ(flag_at(result_buf, cells[k]), MS_PCELL_VALUE);
    }
    CHECK_NEAR(value_sum(obs_buf, result_buf), 6.0, 1e-12);
    env_close(&env);
}

/* Digests of whole result buffers: native, Wasmtime and Node runs of the
 * same deterministic solve (fixed seeds, fake clocks) must agree bit for
 * bit. These constants come from this port; correctness is established by
 * the independent oracles above, the digests only pin cross-runtime parity. */
static uint64_t result_digest(const void *obs, const void *result) {
    return rt_hash64(result, result_len_of(obs), 0x7072622D64696730ull);
}

#define DIGEST_SEED_134 0xb4aa8ff7f0552feeull
#define DIGEST_MIXED 0xea6fabc97c67f771ull
#define DIGEST_PLAYED0 0x7cc8063412b5c863ull
#define DIGEST_LATTICE 0xc01e3f3fd9b4e94eull

static void check_digest(const char *name, uint64_t got, uint64_t want) {
    if (got != want) {
        test_print("    digest of ");
        test_print(name);
        test_print(": ");
        test_print_hex(got);
        test_print("\n");
    }
    CHECK_EQ(got, want);
}

static void test_cross_runtime_digests(void) {
    test_case("deterministic results are bit-identical on every runtime");
    solve_env env;
    env_open(&env, 0.0);
    const SamplingCase *sc = &SAMPLING_CASES[SAMPLING_CASE_RANDOM_SEED_134_SAMPLED];
    sampling_limits(&env, sc);
    env.limits.flags = MS_LIMIT_EXPLICIT_SEED;
    env.limits.seed = 0x0123456789ABCDEFull;
    size_t len = case_obs(sc->observation, obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    check_digest("random_seed_134 seeded", result_digest(obs_buf, result_buf),
                 DIGEST_SEED_134);
    sc = &SAMPLING_CASES[SAMPLING_CASE_MIXED_EXACT_AND_SAMPLED];
    sampling_limits(&env, sc);
    env.limits.flags = 0;
    len = case_obs(sc->observation, obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    check_digest("mixed default seed", result_digest(obs_buf, result_buf), DIGEST_MIXED);
    ms_limits_default(&env.limits);
    len = case_obs(&LARGE_BOARD_CASES[LARGE_CASE_PLAYED_80X80_SEED0], obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    check_digest("played_80x80_seed0", result_digest(obs_buf, result_buf), DIGEST_PLAYED0);
    env_close(&env);
    env_open(&env, 0.01);
    sc = &SAMPLING_CASES[SAMPLING_CASE_LATTICE_300MS];
    sampling_limits(&env, sc);
    len = case_obs(sc->observation, obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    check_digest("lattice 300 ms", result_digest(obs_buf, result_buf), DIGEST_LATTICE);
    env_close(&env);
}

static void test_result_bounds_and_stress(void) {
    test_case("pathological connected board: bounded memory, bytes and sound proofs");
    /* Nothing past result_len is written. */
    solve_env env;
    env_open(&env, 0.0);
    const ProbabilityCase *c = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_21];
    size_t len = case_obs(c, obs_buf);
    size_t rlen = result_len_of(obs_buf);
    base_memset(result_alt, 0xA5, sizeof(result_alt));
    CHECK_STATUS(solve(&env, obs_buf, len, result_alt), MS_OK);
    for (size_t i = rlen; i < sizeof(result_alt); i++) CHECK_EQ(((uint8_t *)result_alt)[i], 0xA5);
    check_exact_case(c, obs_buf, result_alt);
    env_close(&env);
    /* The ~3000-cell lattice component under memory caps, with node and
     * entry budgets far above what the memory allows. */
    const ProbabilityCase *lattice = &LARGE_BOARD_CASES[LARGE_CASE_LATTICE_80X80_SEED4];
    static const uint64_t caps[] = {1u << 20, 4u << 20, 16u << 20};
    for (uint32_t k = 0; k < array_size(caps); k++) {
        env_open(&env, 0.0);
        env.limits.memory_budget_bytes = caps[k];
        env.limits.node_budget = UINT32_MAX;
        env.limits.max_stored_entries = UINT32_MAX;
        len = case_obs(lattice, obs_buf);
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        const ms_result_header *h = header(result_buf);
        CHECK(h->status != MS_PROB_EXACT);
        CHECK(env.mem.peak <= caps[k]);
        check_proofs_match_layout(lattice, result_buf);
        env_close(&env);
    }
}

/* ------------------------------------------------------ budgets and memory */

/* p = num / den as the solver must report it in an exact result. */
static double exact_ratio(bi_pool *pool, const bigint *num, const bigint *den) {
    double value = -1.0;
    CHECK_STATUS(bi_probability(pool, num, den, true, &value), MS_OK);
    return value;
}

/* acc += C(n1, k1) * C(n2, k2), zero when a k is out of range. */
static void add_binomial_product(bi_pool *pool, bigint *acc, int64_t n1, int64_t k1, int64_t n2,
                                 int64_t k2) {
    if (k1 < 0 || k2 < 0 || k1 > n1 || k2 > n2) return;
    bigint a = BI_ZERO_INIT, b = BI_ZERO_INIT;
    CHECK_STATUS(bi_binomial(pool, &a, (uint32_t)n1, (uint32_t)k1), MS_OK);
    CHECK_STATUS(bi_binomial(pool, &b, (uint32_t)n2, (uint32_t)k2), MS_OK);
    CHECK_STATUS(bi_addmul(pool, acc, &a, &b), MS_OK);
    bi_free(pool, &a);
    bi_free(pool, &b);
}

static void test_many_components(void) {
    test_case("1000 components coupled only by the total: closed form, bounded memory");
    /* 1000 blocks '?1?1??': each component {6b, 6b+2, 6b+4} holds its middle
     * mine (1) or both ends (2); cell 6b+5 joins the pool (U = 1000). With
     * R mines and j two-mine blocks, Z = sum_j C(B, j) C(U, R - B - j). */
    const uint32_t blocks = 1000, total = 1700;
    const int64_t B = blocks, U = blocks, R = total;
    uint32_t width = 6u * blocks;
    for (uint32_t i = 0; i < width; i++) dense[i] = (i % 6u == 1u || i % 6u == 3u) ? 1 : -1;
    solve_env env;
    env_open(&env, 0.0);
    size_t len = make_obs(obs_buf, width, 1, total, dense);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    const ms_result_header *h = header(result_buf);
    CHECK_EQ(h->status, MS_PROB_EXACT);
    CHECK_EQ(h->components, blocks);
    CHECK_EQ(h->unconstrained_cells, blocks);
    CHECK(env.mem.peak < ((size_t)8 << 20));
    /* All 1000 histograms are identical, so their outside weights are
     * computed once (Python's cavity cache). Metered work reads the clock
     * every RT_METER_INTERVAL units: about 3.5k readings in total here,
     * whereas one cavity per component would add roughly 2k more. */
    CHECK(env.fake.reads < 4000u);

    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    bi_pool pool;
    bi_pool_init(&pool, &mem);
    bigint z = BI_ZERO_INIT, middle = BI_ZERO_INIT, end = BI_ZERO_INIT, free_cell = BI_ZERO_INIT;
    for (int64_t j = 0; j <= B; j++) {
        add_binomial_product(&pool, &z, B, j, U, R - B - j);
        add_binomial_product(&pool, &middle, B - 1, j, U, R - B - j);
        add_binomial_product(&pool, &end, B - 1, j - 1, U, R - B - j);
        add_binomial_product(&pool, &free_cell, B, j, U - 1, R - B - j - 1);
    }
    double p_middle = exact_ratio(&pool, &middle, &z);
    double p_end = exact_ratio(&pool, &end, &z);
    double p_free = exact_ratio(&pool, &free_cell, &z);
    for (uint32_t b = 0; b < blocks; b++) {
        CHECK(prob_at(result_buf, 6u * b) == p_end);
        CHECK(prob_at(result_buf, 6u * b + 2u) == p_middle);
        CHECK(prob_at(result_buf, 6u * b + 4u) == p_end);
        CHECK(prob_at(result_buf, 6u * b + 5u) == p_free);
    }
    CHECK_NEAR(value_sum(obs_buf, result_buf), (double)total, 1e-7);
    bi_pool_reset(&pool);
    rt_mem_dispose(&mem);
    env_close(&env);
}

/* Invariants of any budget-limited solve of a case with recorded exact
 * truth: a valid result, proofs a subset of the exact proofs, exact results
 * identical to the fixture, estimates summing to the mine total. */
static void check_degraded(const ProbabilityCase *c, const void *obs, const void *result) {
    const ms_result_header *h = header(result);
    if (h->status == MS_PROB_EXACT) {
        check_exact_case(c, obs, result);
        return;
    }
    for (uint32_t i = 0; i < cells_of(obs); i++) {
        uint8_t f = flag_at(result, i);
        if (f & MS_PCELL_PROVEN_SAFE) {
            if (!fixture_runs_contain(c->proven_safe, c->proven_safe_runs, i)) {
                print_case("unsound safe proof", c->name, i);
            }
            CHECK(fixture_runs_contain(c->proven_safe, c->proven_safe_runs, i));
        }
        if (f & MS_PCELL_PROVEN_MINE) {
            if (!fixture_runs_contain(c->proven_mines, c->proven_mine_runs, i)) {
                print_case("unsound mine proof", c->name, i);
            }
            CHECK(fixture_runs_contain(c->proven_mines, c->proven_mine_runs, i));
        }
    }
    if (h->status == MS_PROB_APPROXIMATE) {
        CHECK_NEAR(value_sum(obs, result), (double)c->total_mines, 1e-6);
        CHECK(h->effective_sample_size >= MS_DEFAULT_MIN_EFFECTIVE_SAMPLES);
    }
    if (c->board_rows) check_proofs_match_layout(c, result);
}

static void test_time_budgets(void) {
    test_case("time budgets: phase deadlines on a fake clock");
    solve_env env;
    env_open(&env, 1.0);
    /* No time at all: counting and sampling are skipped, the logically
     * forced mine is still proven. */
    const ProbabilityCase *tail = &PROBABILITY_CASES[PROB_CASE_CHAIN_BOARD_2_FORCED_TAIL];
    size_t len = case_obs(tail, obs_buf);
    env.limits.time_budget_ms = 0.0;
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    const ms_result_header *h = header(result_buf);
    CHECK_EQ(h->status, MS_PROB_UNAVAILABLE);
    CHECK_EQ(h->reason, MS_REASON_TIME_BUDGET_EXHAUSTED);
    CHECK_EQ(h->proven_mines, 1);
    CHECK(flag_at(result_buf, 12) & MS_PCELL_PROVEN_MINE);
    CHECK_EQ(h->samples, 0);
    env_close(&env);

    /* A sweep of budgets over a played 80x80 board: each answer is valid,
     * sound and finishes close to its budget on the fake clock. */
    const ProbabilityCase *board = &LARGE_BOARD_CASES[LARGE_CASE_PLAYED_80X80_SEED0];
    static const double budgets[] = {0.0, 0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 13.0, 21.0, 34.0,
                                     55.0, 89.0, 144.0, 233.0, 1500.0};
    uint32_t statuses = 0;
    bool pairs_cut = false, counting_cut = false, sampling_cut = false;
    for (uint32_t b = 0; b < array_size(budgets); b++) {
        env_open(&env, 0.01);
        len = case_obs(board, obs_buf);
        env.limits.time_budget_ms = budgets[b];
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        check_degraded(board, obs_buf, result_buf);
        h = header(result_buf);
        statuses |= 1u << h->status;
        CHECK(h->elapsed_ms <= budgets[b] + 2.0);
        if (h->status == MS_PROB_UNAVAILABLE) {
            CHECK(h->reason == MS_REASON_TIME_BUDGET_EXHAUSTED ||
                  h->reason == MS_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES ||
                  h->reason == MS_REASON_NO_CONSISTENT_SAMPLES ||
                  h->reason == MS_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES);
        }
        /* Which phase the deadline interrupted. */
        pairs_cut = pairs_cut || !h->pair_reasoning_complete;
        counting_cut = counting_cut || (h->exact_components > 0 && h->sampled_components > 0);
        sampling_cut = sampling_cut ||
                       (h->reason == MS_REASON_TIME_BUDGET_EXHAUSTED && h->sample_attempts > 0);
        env_close(&env);
    }
    CHECK(statuses & (1u << MS_PROB_EXACT));
    CHECK(statuses & (1u << MS_PROB_UNAVAILABLE));
    CHECK(pairs_cut && counting_cut && sampling_cut);
}

/* A clock that stands still for `cliff` readings and then jumps past every
 * deadline: sweeping `cliff` interrupts the solve at each of its clock
 * checks in turn (deadline injection, like allocation failure injection). */
typedef struct cliff_clock {
    uint32_t reads;
    uint32_t cliff;
} cliff_clock;

static double cliff_read(void *ctx) {
    cliff_clock *cliff = (cliff_clock *)ctx;
    return cliff->reads++ < cliff->cliff ? 0.0 : 1e9;
}

static void deadline_sweep(const ProbabilityCase *c, const int8_t *clues, uint32_t width,
                           uint32_t height, uint32_t total, uint32_t stride,
                           bool *combination_cut) {
    for (uint32_t cliff = 0;; cliff += stride) {
        solve_env env;
        env_open(&env, 0.0);
        cliff_clock clock_state = {0, cliff};
        rt_clock_init(&env.clock, cliff_read, &clock_state);
        size_t len = clues ? make_obs(obs_buf, width, height, total, clues) : case_obs(c, obs_buf);
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        const ms_result_header *h = header(result_buf);
        if (c) check_degraded(c, obs_buf, result_buf);
        if (h->status == MS_PROB_APPROXIMATE) {
            CHECK_NEAR(value_sum(obs_buf, result_buf), (double)total, 1e-6);
        }
        if (h->reason == MS_REASON_TIME_BUDGET_EXHAUSTED && h->sample_attempts == 0 &&
            h->exact_components == h->components && h->components > 0) {
            *combination_cut = true; /* every component counted: cut while combining */
        }
        bool finished = clock_state.reads <= cliff;
        env_close(&env);
        if (finished) {
            CHECK_EQ(h->status, MS_PROB_EXACT);
            break;
        }
    }
}

static void test_deadline_injection(void) {
    test_case("deadline injection at every clock check: valid, sound answers");
    bool combination_cut = false;
    const ProbabilityCase *board = &LARGE_BOARD_CASES[LARGE_CASE_PLAYED_80X80_SEED0];
    deadline_sweep(board, NULL, 0, 0, board->total_mines, 1, &combination_cut);
    board = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    deadline_sweep(board, NULL, 0, 0, board->total_mines, 1, &combination_cut);
    /* 1000 coupled components: long histogram products and cavities. */
    for (uint32_t i = 0; i < 6000u; i++) dense_alt[i] = (i % 6u == 1u || i % 6u == 3u) ? 1 : -1;
    deadline_sweep(NULL, dense_alt, 6000, 1, 1700, 37, &combination_cut);
    CHECK(combination_cut);
}

/* Mutation check of the retained-entry guard: a DP layer's merged spans can
 * hold zero-filled mine counts that no incoming part covered, so the guard
 * must count the layer actually kept, not the running estimate of its
 * parts. On this board (found among 3000 C-PRNG boards; '*' is a hidden
 * mine of the generating layout, '.' a hidden safe cell) exact counting
 * retains exactly 59 entries, one more than the parts' estimate allows. */
static const char *const STORE_GAP_ROWS[4] = {
    "**12**",
    "*..3*3",
    ".11*.2",
    "...2*1",
};
#define STORE_GAP_ENTRIES 59u

static void test_store_guard_counts_merged_spans(void) {
    test_case("retained-entry guard counts merged spans, zero-filled gaps included");
    uint32_t width = 6, height = 4, total = 0;
    for (uint32_t r = 0; r < height; r++) {
        for (uint32_t col = 0; col < width; col++) {
            char ch = STORE_GAP_ROWS[r][col];
            dense[r * width + col] = ch >= '0' && ch <= '8' ? (int8_t)(ch - '0') : -1;
            total += ch == '*';
        }
    }
    oracle o;
    CHECK(oracle_run(width, height, total, dense, &o));
    CHECK(o.layouts > 0);
    for (uint32_t limit = 0; limit <= STORE_GAP_ENTRIES; limit++) {
        solve_env env;
        env_open(&env, 0.0);
        env.limits.max_stored_entries = limit;
        size_t len = make_obs(obs_buf, width, height, total, dense);
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        const ms_result_header *h = header(result_buf);
        CHECK((h->status == MS_PROB_EXACT) == (limit == STORE_GAP_ENTRIES));
        for (uint32_t k = 0; k < o.nh; k++) {
            uint32_t cell = o.hidden[k];
            uint8_t f = flag_at(result_buf, cell);
            CHECK(!(f & MS_PCELL_PROVEN_SAFE) || o.count[k] == 0);
            CHECK(!(f & MS_PCELL_PROVEN_MINE) || o.count[k] == o.layouts);
            CHECK(!(f & MS_PCELL_PROVEN_SAFE) || STORE_GAP_ROWS[cell / width][cell % width] != '*');
            if (h->status == MS_PROB_EXACT) {
                CHECK(prob_at(result_buf, cell) == (double)o.count[k] / (double)o.layouts);
            }
        }
        env_close(&env);
    }
}

static void test_node_and_entry_budgets(void) {
    test_case("node and retained-entry budgets fall back to sampling soundly");
    const ProbabilityCase *board = &LARGE_BOARD_CASES[LARGE_CASE_PLAYED_80X80_SEED1];
    static const uint32_t nodes[] = {0, 1, 2, 3, 5, 10, 30, 100, 300, 1000, 3000, 100000};
    uint32_t statuses = 0;
    for (uint32_t b = 0; b < array_size(nodes); b++) {
        solve_env env;
        env_open(&env, 0.0);
        size_t len = case_obs(board, obs_buf);
        env.limits.node_budget = nodes[b];
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        check_degraded(board, obs_buf, result_buf);
        const ms_result_header *h = header(result_buf);
        statuses |= 1u << h->status;
        CHECK(h->nodes <= nodes[b] + h->components);
        env_close(&env);
    }
    CHECK(statuses & (1u << MS_PROB_EXACT));
    CHECK(statuses & (1u << MS_PROB_APPROXIMATE));
    static const uint32_t entries[] = {0, 1, 10, 100, 1000, 10000};
    for (uint32_t b = 0; b < array_size(entries); b++) {
        solve_env env;
        env_open(&env, 0.0);
        size_t len = case_obs(board, obs_buf);
        env.limits.max_stored_entries = entries[b];
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        check_degraded(board, obs_buf, result_buf);
        if (entries[b] == 0) CHECK_EQ(header(result_buf)->exact_components, 0);
        env_close(&env);
    }
    /* Mixed: one exact and one sampled component, coupled by the total. */
    solve_env env;
    env_open(&env, 0.0);
    const SamplingCase *sc = &SAMPLING_CASES[SAMPLING_CASE_MIXED_EXACT_AND_SAMPLED];
    sampling_limits(&env, sc);
    size_t len = case_obs(sc->observation, obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_APPROXIMATE);
    CHECK_EQ(header(result_buf)->exact_components, 1);
    CHECK_EQ(header(result_buf)->sampled_components, 1);
    /* Python's accounting: one node for A, then B's first node and the
     * node that exceeded the budget. */
    CHECK_EQ(header(result_buf)->nodes, 3);
    check_sampling_result(sc, obs_buf, result_buf);
    env_close(&env);
}

/* Solves `c` under allocation failure injection at every request (or every
 * `stride`-th): the answer is always a valid result with sound proofs, the
 * workspace (holding unrelated caller allocations) is restored, and once no
 * failure is hit the answer equals the unrestricted one. */
static void injection_sweep(const ProbabilityCase *c, uint32_t node_budget, uint32_t stride,
                            uint64_t burst) {
    solve_env env;
    env_open(&env, 0.0);
    env.limits.node_budget = node_budget;
    size_t len = case_obs(c, obs_buf);
    size_t rlen = result_len_of(obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_ref), MS_OK);
    env_close(&env);
    for (uint64_t after = 0;; after += stride) {
        env_open(&env, 0.0);
        env.limits.node_budget = node_budget;
        uint8_t *keep = (uint8_t *)rt_alloc(&env.mem, 1000);
        uint8_t *small = (uint8_t *)rt_bump(&env.mem, 48);
        CHECK(keep != NULL && small != NULL);
        for (uint32_t i = 0; i < 1000; i++) keep[i] = (uint8_t)(i * 7u);
        for (uint32_t i = 0; i < 48; i++) small[i] = (uint8_t)(i + 1u);
        rt_mem_set_failure(&env.mem, after, burst);
        uint64_t failures = env.mem.failures;
        CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
        bool hit = env.mem.failures != failures;
        rt_mem_set_failure(&env.mem, 0, 0);
        for (uint32_t i = 0; i < 1000; i++) CHECK_EQ(keep[i], (uint8_t)(i * 7u));
        for (uint32_t i = 0; i < 48; i++) CHECK_EQ(small[i], (uint8_t)(i + 1u));
        check_degraded(c, obs_buf, result_buf);
        rt_free(&env.mem, keep);
        rt_mem_reset(&env.mem);
        env_close(&env);
        if (!hit) {
            CHECK(base_memcmp(result_buf, result_ref, rlen) == 0);
            break;
        }
    }
}

static void test_allocation_failures(void) {
    test_case("allocation failure injection: valid answers and complete cleanup");
    injection_sweep(&PROBABILITY_CASES[PROB_CASE_README_WEIGHTING], MS_DEFAULT_NODE_BUDGET, 1,
                    RT_MEM_FOREVER);
    injection_sweep(&PROBABILITY_CASES[PROB_CASE_README_WEIGHTING], MS_DEFAULT_NODE_BUDGET, 1, 1);
    injection_sweep(&PROBABILITY_CASES[PROB_CASE_COMPONENTS_TOTAL_6], MS_DEFAULT_NODE_BUDGET, 1,
                    RT_MEM_FOREVER);
    injection_sweep(&PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_82], MS_DEFAULT_NODE_BUDGET, 1, 1);
    injection_sweep(&PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_134], 0, 1, RT_MEM_FOREVER);
    injection_sweep(&PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_134], 0, 1, 1);
    injection_sweep(&PROBABILITY_CASES[PROB_CASE_MIXED_COMPONENTS_18X1], 2, 1, 1);
    injection_sweep(&PROBABILITY_CASES[PROB_CASE_NEAR_CERTAIN_80X80], MS_DEFAULT_NODE_BUDGET, 1,
                    RT_MEM_FOREVER);
    injection_sweep(&LARGE_BOARD_CASES[LARGE_CASE_PLAYED_80X80_SEED0], MS_DEFAULT_NODE_BUDGET, 7,
                    RT_MEM_FOREVER);
    injection_sweep(&LARGE_BOARD_CASES[LARGE_CASE_PLAYED_80X80_SEED0], MS_DEFAULT_NODE_BUDGET, 13,
                    1);
}

static void test_memory_budgets(void) {
    test_case("memory budgets: exhaustion is an unavailable answer, never an error");
    static const uint64_t budgets[] = {0, 1, 4096, 16384, 65536, 131072, 262144, 524288,
                                       1048576, 4194304};
    const ProbabilityCase *cases[] = {
        &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING],
        &PROBABILITY_CASES[PROB_CASE_CHAIN_3001_EXACT],
        &PROBABILITY_CASES[PROB_CASE_CORNER_80X80_2000],
        &LARGE_BOARD_CASES[LARGE_CASE_PLAYED_80X80_SEED1],
    };
    uint32_t memory = 0;
    for (uint32_t k = 0; k < array_size(cases); k++) {
        for (uint32_t b = 0; b < array_size(budgets); b++) {
            solve_env env;
            env_open(&env, 0.0);
            env.limits.memory_budget_bytes = budgets[b];
            size_t len = case_obs(cases[k], obs_buf);
            CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
            check_degraded(cases[k], obs_buf, result_buf);
            memory += header(result_buf)->reason == MS_REASON_MEMORY_BUDGET_EXHAUSTED;
            CHECK(env.mem.peak <= budgets[b] + RT_MEM_CHUNK_BLOCK || budgets[b] == 0);
            env_close(&env);
        }
    }
    CHECK(memory > 0);
    /* A caller budget below its live bytes: everything of ours fails, the
     * budget is restored and the caller's memory untouched. */
    solve_env env;
    env_open(&env, 0.0);
    void *held = rt_alloc(&env.mem, 10000);
    CHECK(held != NULL);
    rt_mem_set_budget(&env.mem, env.mem.live);
    size_t len = case_obs(&PROBABILITY_CASES[PROB_CASE_PRIOR_5X4_7], obs_buf);
    CHECK_STATUS(solve(&env, obs_buf, len, result_buf), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_UNAVAILABLE);
    CHECK_EQ(header(result_buf)->reason, MS_REASON_MEMORY_BUDGET_EXHAUSTED);
    rt_free(&env.mem, held);
    env_close(&env);
}

void test_probability(void) {
    test_probability_cases();
    test_large_boards();
    test_huge_and_near_certain();
    test_degenerate_boards();
    test_long_chain();
    test_malformed_observations();
    test_error_statuses();
    test_aliased_buffers();
    test_public_equivalence();
    test_random_boards();
    test_many_components();
    test_sampling_cases();
    test_sampled_certainties();
    test_sampling_determinism();
    test_incompatible_samples();
    test_reweighted_endpoints_are_not_proofs();
    test_cross_runtime_digests();
    test_result_bounds_and_stress();
    test_time_budgets();
    test_deadline_injection();
    test_node_and_entry_budgets();
    test_store_guard_counts_merged_spans();
    test_allocation_failures();
    test_memory_budgets();
}
