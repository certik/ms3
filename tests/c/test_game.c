/* Tests for c/game.c: configuration, lazy uniform placement, flood, flags,
 * chords, timers, terminal states, revisions, public view/observation and
 * atomic deduction batches. Ports the c-game destinations of
 * tests/coverage-map.json using tests/c/fixtures/game_scripts.h. */

#include "test_support.h"

#include "fixtures/game_scripts.h"
#include "fixtures/probability_cases.h"
#include "game.h"

#define VIEW_WORDS ((sizeof(ms_view_header) + MS_GAME_MAX_CELLS + 7u) / 8u)
#define OBS_WORDS ((sizeof(ms_obs_header) + MS_GAME_MAX_CELLS + 7u) / 8u)

static uint64_t view_buf[VIEW_WORDS];
static uint64_t before_buf[VIEW_WORDS];
static uint64_t obs_buf[OBS_WORDS];
static uint64_t obs_copy[OBS_WORDS];

/* ---------------------------------------------------------------- helpers */

/* Fixture integers -> C uint32 parameters without truncation onto a valid
 * value: anything outside 0..2^32-1 becomes UINT32_MAX, which no board
 * index, dimension, mine count or action accepts. */
static uint32_t to_u32(int64_t value) {
    return value < 0 || value > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

/* Revisions follow the JS adapter (wasm_api.h): negative values are
 * invalid; a safe integer above the C range can never be current and is
 * reported stale - modelled by MS_REVISION_MAX, which C also rejects as
 * stale for any game below it. */
static uint32_t to_revision(int64_t value, uint32_t current) {
    if (value == FIXTURE_REV_CURRENT) return current;
    if (value < 0) return UINT32_MAX;
    if (value > (int64_t)UINT32_MAX) return MS_REVISION_MAX;
    return (uint32_t)value;
}

/* Fixture op numbering (0 reveal, 1 flag, 2 chord) -> ms_action; every
 * other raw value maps to a value outside the C enum. */
static uint32_t to_action(uint32_t op, int64_t raw) {
    if (op == FIXTURE_OP_RAW_ACTION) {
        if (raw >= 0 && raw <= 2) return (uint32_t)raw + MS_ACTION_REVEAL;
        if (raw < 0 || raw >= (int64_t)UINT32_MAX) return 0;
        return (uint32_t)raw + 1u;
    }
    return op + MS_ACTION_REVEAL;
}

static ms_status to_status(uint32_t error) {
    switch (error) {
    case FIXTURE_ERR_INVALID_WIDTH: return MS_ERR_INVALID_WIDTH;
    case FIXTURE_ERR_INVALID_HEIGHT: return MS_ERR_INVALID_HEIGHT;
    case FIXTURE_ERR_INVALID_MINES: return MS_ERR_INVALID_MINES;
    case FIXTURE_ERR_INVALID_ACTION: return MS_ERR_INVALID_ACTION;
    case FIXTURE_ERR_OUT_OF_BOUNDS: return MS_ERR_OUT_OF_BOUNDS;
    case FIXTURE_ERR_INVALID_REVISION: return MS_ERR_INVALID_REVISION;
    case FIXTURE_ERR_STALE_REVISION: return MS_ERR_STALE_REVISION;
    case FIXTURE_ERR_GAME_OVER: return MS_ERR_GAME_OVER;
    case FIXTURE_ERR_GAME_NOT_STARTED: return MS_ERR_GAME_NOT_STARTED;
    case FIXTURE_ERR_INVALID_DEDUCTIONS: return MS_ERR_INVALID_DEDUCTIONS;
    case FIXTURE_ERR_INVALID_LAYOUT: return MS_ERR_INTERNAL; /* engine.h: rejected layout */
    case FIXTURE_ERR_NONE: return MS_OK;
    default: break; /* invalid_coordinates: JS adapter only */
    }
    CHECK(!"fixture error code without a C counterpart");
    return MS_ERR_INTERNAL;
}

static uint32_t to_game_status(uint32_t status) {
    return status + MS_GAME_READY; /* READY, PLAYING, WON, LOST in both enums */
}

typedef struct game_env {
    rt_mem mem;
    fake_clock fake;
    rt_clock clock;
    ms_game *game;
    uint32_t width;
    uint32_t height;
    uint32_t mines;
} game_env;

static void env_open(game_env *env, uint32_t width, uint32_t height, uint32_t mines,
                     const uint32_t *layout, uint32_t layout_count, double clock_start,
                     uint64_t seed) {
    rt_mem_init(&env->mem, RT_MEM_UNLIMITED);
    fake_clock_init(&env->clock, &env->fake, clock_start, 0.0);
    env->game = NULL;
    env->width = width;
    env->height = height;
    env->mines = mines;
    if (layout != NULL) {
        CHECK_STATUS(ms_game_create_with_layout(&env->game, &env->mem, &env->clock, width, height,
                                                mines, 1, layout, layout_count),
                     MS_OK);
    } else {
        CHECK_STATUS(ms_game_create(&env->game, &env->mem, &env->clock, width, height, mines, 1,
                                    seed),
                     MS_OK);
    }
    CHECK(env->game != NULL);
}

static void env_close(game_env *env) {
    ms_game_destroy(env->game);
    CHECK_EQ(env->mem.live, 0);
    CHECK_EQ(env->mem.misuses, 0);
    rt_mem_dispose(&env->mem);
}

/* An env view of a game created elsewhere (for take_view only). */
static void env_wrap(game_env *env, ms_game *game, uint32_t width, uint32_t height,
                     uint32_t mines) {
    env->game = game;
    env->width = width;
    env->height = height;
    env->mines = mines;
}

static size_t env_view_len(const game_env *env) {
    return ms_view_size(env->width, env->height);
}

/* Writes the view into `buf`, validates it and returns its header. */
static const ms_view_header *take_view(game_env *env, uint64_t *buf) {
    CHECK_STATUS(ms_game_view(env->game, buf, env_view_len(env)), MS_OK);
    CHECK_STATUS(ms_view_validate(buf, env_view_len(env)), MS_OK);
    return (const ms_view_header *)buf;
}

static char render_cell(uint8_t b) {
    if (b & MS_CELL_REVEALED) {
        return (b & MS_CELL_MINE) ? 'X' : (char)('0' + (b & MS_CELL_ADJACENT_MASK));
    }
    return (b & MS_CELL_FLAGGED) ? 'F' : '#';
}

static void check_board(const void *view, const char *board, const char *label) {
    const ms_view_header *h = (const ms_view_header *)view;
    const uint8_t *cells = ms_view_cells(view);
    uint32_t count = h->width * h->height;
    CHECK_EQ(base_strlen(board), count);
    for (uint32_t i = 0; i < count; i++) {
        if (render_cell(cells[i]) == board[i]) continue;
        test_fail_begin("board render differs", __FILE__, __LINE__, label);
        test_print(" at cell ");
        test_print_u64(i);
        test_print("\n  expected / actual:\n");
        for (uint32_t r = 0; r < h->height; r++) {
            char line[2 * MS_GAME_MAX_SIDE + 8];
            uint32_t n = 0;
            line[n++] = ' ';
            line[n++] = ' ';
            for (uint32_t c = 0; c < h->width; c++) line[n++] = board[r * h->width + c];
            line[n++] = ' ';
            for (uint32_t c = 0; c < h->width; c++) line[n++] = render_cell(cells[r * h->width + c]);
            line[n++] = '\n';
            line[n] = '\0';
            test_print(line);
        }
        test_fail_end();
    }
}

static bool views_equal(const void *a, const void *b, size_t len) {
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

static bool layout_has(const uint32_t *layout, uint32_t count, uint32_t cell) {
    for (uint32_t i = 0; i < count; i++) {
        if (layout[i] == cell) return true;
    }
    return false;
}

static bool in_block(uint32_t width, uint32_t center, uint32_t cell) {
    int64_t dr = (int64_t)(center / width) - (int64_t)(cell / width);
    int64_t dc = (int64_t)(center % width) - (int64_t)(cell % width);
    return dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1;
}

/* ---------------------------------------------------------------- scripts */

static void check_step_state(game_env *env, const GameScript *s, const GameStep *step) {
    const ms_view_header *h = take_view(env, view_buf);
    CHECK_EQ(h->generation, 1);
    CHECK_EQ(h->status, to_game_status(step->status));
    CHECK_EQ(h->revision, (uint64_t)step->revision_after);
    CHECK_EQ(h->flags, (uint64_t)step->flags_after);
    CHECK_EQ(h->revealed, step->revealed_count);
    CHECK_EQ(h->width, s->width);
    CHECK_EQ(h->height, s->height);
    CHECK_EQ(h->mines, s->mines);
    if (step->elapsed_ms != FIXTURE_UNCHECKED) {
        CHECK(h->elapsed_ms == (double)step->elapsed_ms);
    }
    if (step->board != NULL) check_board(view_buf, step->board, s->name);

    const uint8_t *cells = ms_view_cells(view_buf);
    uint32_t count = s->width * s->height;
    uint32_t exploded = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!(cells[i] & MS_CELL_EXPLODED)) continue;
        CHECK(layout_has(step->exploded, step->exploded_count, i));
        exploded++;
    }
    CHECK_EQ(exploded, step->exploded_count);

    /* Placement happens exactly at the first reveal (test-only hook), and a
     * terminal state discloses exactly the layout. */
    bool terminal = h->status == MS_GAME_WON || h->status == MS_GAME_LOST;
    if (h->status == MS_GAME_READY) {
        CHECK(!ms_game_test_placed(env->game));
        CHECK_EQ(ms_game_test_mine_count(env->game), 0);
    } else {
        CHECK(ms_game_test_placed(env->game));
        CHECK_EQ(ms_game_test_mine_count(env->game), s->mines);
    }
    for (uint32_t i = 0; i < count; i++) {
        bool mine = ms_game_test_is_mine(env->game, i);
        if (s->layout != NULL && h->status != MS_GAME_READY) {
            CHECK(mine == layout_has(s->layout, s->layout_count, i));
        }
        CHECK(((cells[i] & MS_CELL_MINE) != 0) == (terminal && mine));
    }
}

static void run_script(const GameScript *s) {
    test_case_in("game_scripts", s->name);
    game_env env;
    env_open(&env, s->width, s->height, s->mines, s->layout, s->layout_count,
             (double)s->clock_start_ms, 0x5EED0000u + s->width * 131u + s->mines);
    size_t len = env_view_len(&env);
    uint32_t revision = 0;
    for (uint32_t k = 0; k < s->step_count; k++) {
        const GameStep *step = &s->steps[k];
        take_view(&env, before_buf);
        ms_status status = MS_OK;
        bool changed = false;
        if (step->op == FIXTURE_OP_ADVANCE) {
            CHECK_EQ(step->expect, FIXTURE_EXPECT_NONE);
            env.fake.now += (double)step->advance_ms;
            check_step_state(&env, s, step);
            continue;
        }
        uint32_t rev = to_revision(step->revision, revision);
        if (step->op == FIXTURE_OP_DEDUCE) {
            uint32_t safe[64], mines[64];
            CHECK(step->safe_count <= 64 && step->mine_count <= 64);
            for (uint32_t i = 0; i < step->safe_count; i++) safe[i] = to_u32(step->safe[i]);
            for (uint32_t i = 0; i < step->mine_count; i++) mines[i] = to_u32(step->mines[i]);
            status = ms_game_apply_deductions(env.game, 1, rev, step->safe ? safe : NULL,
                                              step->safe_count, step->mines ? mines : NULL,
                                              step->mine_count, &changed);
        } else {
            status = ms_game_act(env.game, 1, rev, to_action(step->op, step->raw_action),
                                 to_u32(step->row), to_u32(step->col), &changed);
        }
        switch (step->expect) {
        case FIXTURE_EXPECT_CHANGED:
            CHECK_STATUS(status, MS_OK);
            CHECK(changed);
            break;
        case FIXTURE_EXPECT_NOOP:
            CHECK_STATUS(status, MS_OK);
            CHECK(!changed);
            CHECK(views_equal(before_buf, take_view(&env, view_buf), len));
            break;
        default:
            CHECK_EQ(step->expect, FIXTURE_EXPECT_ERROR);
            CHECK_STATUS(status, to_status(step->error));
            CHECK(!changed);
            CHECK(views_equal(before_buf, take_view(&env, view_buf), len));
            break;
        }
        check_step_state(&env, s, step);
        revision = ((const ms_view_header *)view_buf)->revision;
    }
    env_close(&env);
}

static void test_game_scripts(void) {
    for (uint32_t i = 0; i < GAME_SCRIPT_COUNT; i++) run_script(&GAME_SCRIPTS[i]);
}

/* tests.test_game.SerializationTests.test_cells_are_row_major */
static void test_row_major(void) {
    test_case("cells are row-major");
    const GameScript *s = &GAME_SCRIPTS[GAME_SCRIPT_FLOOD_STOPS_AT_NUMBERS];
    game_env env;
    env_open(&env, s->width, s->height, s->mines, s->layout, s->layout_count, 0.0, 0);
    bool changed = false;
    CHECK_STATUS(ms_game_act(env.game, 1, 0, MS_ACTION_REVEAL, 2, 0, &changed), MS_OK);
    take_view(&env, view_buf);
    const uint8_t *cells = ms_view_cells(view_buf);
    CHECK_EQ(cells[1 * 7 + 2], MS_CELL_REVEALED | 2u);
    CHECK_EQ(cells[2 * 7 + 3], MS_CELL_NO_ADJACENT);
    env_close(&env);
}

/* ---------------------------------------------------------------- configuration */

static void test_config_cases(void) {
    test_case("game_config_cases");
    for (uint32_t i = 0; i < GAME_CONFIG_CASE_COUNT; i++) {
        const GameConfigCase *c = &GAME_CONFIG_CASES[i];
        uint32_t w = to_u32(c->width), h = to_u32(c->height), m = to_u32(c->mines);
        ms_status expected = to_status(c->error);
        CHECK_STATUS(ms_game_check_config(w, h, m), expected);

        rt_mem mem;
        fake_clock fake;
        rt_clock clock;
        rt_mem_init(&mem, RT_MEM_UNLIMITED);
        fake_clock_init(&clock, &fake, 0.0, 0.0);
        ms_game *game = NULL;
        CHECK_STATUS(ms_game_create(&game, &mem, &clock, w, h, m, 1, i), expected);
        if (expected == MS_OK) {
            game_env env;
            env_wrap(&env, game, w, h, m);
            const ms_view_header *v = take_view(&env, view_buf);
            CHECK_EQ(v->status, MS_GAME_READY);
            CHECK_EQ(v->width, w);
            CHECK_EQ(v->height, h);
            CHECK_EQ(v->mines, m);
            ms_game_destroy(game);
        } else {
            CHECK(game == NULL);
        }
        CHECK_EQ(mem.live, 0);
        if (c->max_mines >= 0) {
            uint32_t limit = (uint32_t)c->max_mines;
            CHECK_EQ(limit, w * h - MS_GAME_SAFE_START_CELLS);
            CHECK_STATUS(ms_game_check_config(w, h, limit), MS_OK);
            CHECK_STATUS(ms_game_check_config(w, h, limit + 1), MS_ERR_INVALID_MINES);
            CHECK_STATUS(ms_game_check_config(w, h, 0), MS_ERR_INVALID_MINES);
        }
        rt_mem_dispose(&mem);
    }
}

static void test_create_arguments(void) {
    test_case("creation: argument order, generations, layout hook, allocation failure");
    rt_mem mem;
    fake_clock fake;
    rt_clock clock;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    fake_clock_init(&clock, &fake, 0.0, 0.0);
    ms_game *game = NULL;
    uint32_t layout[2] = {16, 18};

    CHECK_STATUS(ms_game_create(NULL, &mem, &clock, 9, 9, 10, 1, 0), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_create(&game, NULL, &clock, 9, 9, 10, 1, 0), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_create(&game, &mem, NULL, 9, 9, 10, 1, 0), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_create(NULL, &mem, &clock, 4, 9, 10, 0, 0), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_create(&game, &mem, &clock, 4, 9, 10, 0, 0), MS_ERR_INVALID_WIDTH);
    CHECK_STATUS(ms_game_create(&game, &mem, &clock, 9, 9, 10, 0, 0), MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_game_create(&game, &mem, &clock, 9, 9, 10, MS_GENERATION_MAX + 1, 0),
                 MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_game_create_with_layout(&game, &mem, &clock, 5, 5, 2, 1, NULL, 2),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_create_with_layout(&game, &mem, &clock, 5, 5, 2, 1, layout, 26),
                 MS_ERR_INVALID_BUFFER);
    CHECK(game == NULL);
    CHECK_EQ(mem.live, 0);

    /* Allocation failure and a budget too small for the game. */
    rt_mem_set_failure(&mem, 0, 1);
    CHECK_STATUS(ms_game_create(&game, &mem, &clock, 80, 80, 10, 1, 0),
                 MS_ERR_RESOURCE_EXHAUSTED);
    CHECK(game == NULL);
    CHECK_EQ(mem.live, 0);
    rt_mem_set_budget(&mem, 4096);
    CHECK_STATUS(ms_game_create(&game, &mem, &clock, 80, 80, 10, 1, 0),
                 MS_ERR_RESOURCE_EXHAUSTED);
    CHECK(game == NULL);
    CHECK_EQ(mem.live, 0);
    rt_mem_set_budget(&mem, RT_MEM_UNLIMITED);

    CHECK_STATUS(ms_game_create(&game, &mem, &clock, 9, 9, 10, MS_GENERATION_MAX, 7), MS_OK);
    game_env env;
    env_wrap(&env, game, 9, 9, 10);
    CHECK_EQ(take_view(&env, view_buf)->generation, MS_GENERATION_MAX);
    ms_game_destroy(game);
    ms_game_destroy(NULL);

    /* An empty layout is still a layout: the first reveal rejects it. */
    CHECK_STATUS(ms_game_create_with_layout(&game, &mem, &clock, 5, 5, 2, 3, NULL, 0), MS_OK);
    bool changed = true;
    CHECK_STATUS(ms_game_act(game, 3, 0, MS_ACTION_REVEAL, 0, 0, &changed), MS_ERR_INTERNAL);
    CHECK(changed); /* outputs are written only on MS_OK */
    CHECK(!ms_game_test_placed(game));
    ms_game_destroy(game);
    CHECK_EQ(mem.live, 0);
    CHECK_EQ(mem.misuses, 0);
    rt_mem_dispose(&mem);
}

/* tests.test_game.SerializationTests.test_state_schema and
 * tests.test_server.CreateGameTests.test_create_returns_hidden_ready_state */
static void test_initial_view(void) {
    test_case("public_state_initial_values");
    game_env env;
    env_open(&env, 12, 10, 25, NULL, 0, 1234.0, 99);
    const ms_view_header *h = take_view(&env, view_buf);
    CHECK_EQ(h->magic, MS_MAGIC_VIEW);
    CHECK_EQ(h->version, MS_ABI_VERSION);
    CHECK_EQ(h->generation, 1);
    CHECK_EQ(h->revision, 0);
    CHECK_EQ(h->status, MS_GAME_READY);
    CHECK_EQ(h->width, 12);
    CHECK_EQ(h->height, 10);
    CHECK_EQ(h->mines, 25);
    CHECK_EQ(h->flags, 0);
    CHECK_EQ(h->revealed, 0);
    CHECK(h->elapsed_ms == 0.0);
    const uint8_t *cells = ms_view_cells(view_buf);
    for (uint32_t i = 0; i < 120; i++) CHECK_EQ(cells[i], MS_CELL_NO_ADJACENT);
    for (size_t i = sizeof(ms_view_header) + 120; i < env_view_len(&env); i++) {
        CHECK_EQ(((const uint8_t *)view_buf)[i], 0);
    }
    CHECK(!ms_game_test_placed(env.game));
    /* Reading the view twice is side-effect free. */
    take_view(&env, before_buf);
    CHECK(views_equal(view_buf, before_buf, env_view_len(&env)));
    env_close(&env);
}

/* ---------------------------------------------------------------- placement */

static void reveal_first(game_env *env, uint32_t row, uint32_t col) {
    bool changed = false;
    CHECK_STATUS(ms_game_act(env->game, 1, 0, MS_ACTION_REVEAL, row, col, &changed), MS_OK);
    CHECK(changed);
}

/* Mines placed, none in the clipped block, the first cell shows 0. */
static void check_first_reveal(game_env *env, uint32_t row, uint32_t col) {
    uint32_t first = row * env->width + col;
    const ms_view_header *h = take_view(env, view_buf);
    CHECK(h->status == MS_GAME_PLAYING || h->status == MS_GAME_WON);
    CHECK_EQ(h->revision, 1);
    CHECK_EQ(ms_game_test_mine_count(env->game), env->mines);
    for (uint32_t i = 0; i < env->width * env->height; i++) {
        if (in_block(env->width, first, i)) CHECK(!ms_game_test_is_mine(env->game, i));
    }
    CHECK_EQ(ms_view_cells(view_buf)[first], MS_CELL_REVEALED | 0u);
}

/* The baseline's first-reveal spots (0,0), (0,w-1), (h-1,0), (h-1,w-1),
 * (0,w/2), (h/2,0), (h/2,w/2), (1,1) as codes 0: 0, 1: size-1, 2: size/2,
 * 3: 1, applied to (height, width). */
static const uint8_t FIRST_SPOTS[8][2] = {
    {0, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 2}, {2, 0}, {2, 2}, {3, 3},
};

static uint32_t spot_coord(uint8_t code, uint32_t size) {
    switch (code) {
    case 0: return 0;
    case 1: return size - 1;
    case 2: return size / 2;
    default: return 1;
    }
}

/* tests.test_game.FirstRevealTests.test_first_reveal_and_neighbors_are_safe_everywhere */
static void test_first_reveal_property(void) {
    test_case("placement_first_reveal_safety_property");
    for (uint32_t c = 0; c < 5; c++) {
        uint32_t w = GAME_FIRST_REVEAL_CONFIGS[c][0];
        uint32_t h = GAME_FIRST_REVEAL_CONFIGS[c][1];
        uint32_t m = GAME_FIRST_REVEAL_CONFIGS[c][2];
        for (uint32_t p = 0; p < 8; p++) {
            uint32_t row = spot_coord(FIRST_SPOTS[p][0], h);
            uint32_t col = spot_coord(FIRST_SPOTS[p][1], w);
            for (uint32_t seed = 0; seed < GAME_FIRST_REVEAL_SEEDS; seed++) {
                game_env env;
                env_open(&env, w, h, m, NULL, 0, 0.0, rt_mix64(c * 1000u + p * 100u + seed));
                reveal_first(&env, row, col);
                check_first_reveal(&env, row, col);
                env_close(&env);
            }
        }
    }
}

/* tests.test_game.FirstRevealTests.test_maximum_density_corner_start_places_exact_count */
static void test_corner_density(void) {
    test_case("placement_corner_density_property");
    for (uint32_t seed = 0; seed < GAME_CORNER_DENSITY_SEEDS; seed++) {
        game_env env;
        env_open(&env, 5, 5, 16, NULL, 0, 0.0, seed);
        reveal_first(&env, 4, 0);
        check_first_reveal(&env, 4, 0); /* 16 of the 21 cells outside {15,16,20,21} */
        env_close(&env);
    }
}

/* tests.test_game.FirstRevealTests.test_mine_placement_is_uniform_outside_the_start_area */
static void test_uniform_placement(void) {
    test_case("placement_uniformity_chi_square");
    uint32_t counts[25] = {0};
    for (uint32_t trial = 0; trial < GAME_UNIFORMITY_TRIALS; trial++) {
        game_env env;
        env_open(&env, 5, 5, 1, NULL, 0, 0.0, 0xC0FFEE00u + trial);
        reveal_first(&env, 0, 0);
        uint32_t found = 0;
        for (uint32_t i = 0; i < 25; i++) {
            if (ms_game_test_is_mine(env.game, i)) {
                counts[i]++;
                found++;
            }
        }
        CHECK_EQ(found, 1);
        env_close(&env);
    }
    double expected = (double)GAME_UNIFORMITY_TRIALS / 21.0;
    double chi2 = 0.0;
    for (uint32_t i = 0; i < 25; i++) {
        if (in_block(5, 0, i)) {
            CHECK_EQ(counts[i], 0);
            continue;
        }
        double d = (double)counts[i] - expected;
        chi2 += d * d / expected;
    }
    CHECK(chi2 < GAME_UNIFORMITY_CHI2_LIMIT);
}

static void test_placement_determinism(void) {
    test_case("same seed and moves give the same game; seeds matter");
    game_env a, b, c;
    env_open(&a, 30, 16, 99, NULL, 0, 0.0, 0x123456789ABCDEFull);
    env_open(&b, 30, 16, 99, NULL, 0, 0.0, 0x123456789ABCDEFull);
    env_open(&c, 30, 16, 99, NULL, 0, 0.0, 0x123456789ABCDEEull);
    reveal_first(&a, 8, 15);
    reveal_first(&b, 8, 15);
    reveal_first(&c, 8, 15);
    take_view(&a, view_buf);
    take_view(&b, before_buf);
    CHECK(views_equal(view_buf, before_buf, env_view_len(&a)));
    bool differs = false;
    for (uint32_t i = 0; i < 480; i++) {
        CHECK(ms_game_test_is_mine(a.game, i) == ms_game_test_is_mine(b.game, i));
        differs = differs || ms_game_test_is_mine(a.game, i) != ms_game_test_is_mine(c.game, i);
    }
    CHECK(differs);
    env_close(&a);
    env_close(&b);
    env_close(&c);
}

/* tests.test_game.SerializationTests.test_no_hidden_information_before_game_ends */
static void test_no_leak_property(void) {
    test_case("public_state_no_leak_property");
    uint32_t w = GAME_NO_LEAK_CONFIG[0], h = GAME_NO_LEAK_CONFIG[1], m = GAME_NO_LEAK_CONFIG[2];
    for (uint32_t seed = 0; seed < GAME_NO_LEAK_SEEDS; seed++) {
        game_env env;
        env_open(&env, w, h, m, NULL, 0, 0.0, 0xAB00u + seed);
        reveal_first(&env, 5, 5);
        bool changed = false;
        CHECK_STATUS(ms_game_act(env.game, 1, 1, MS_ACTION_FLAG, 0, 0, &changed), MS_OK);
        const ms_view_header *v = take_view(&env, view_buf);
        if (v->status == MS_GAME_PLAYING) {
            const uint8_t *cells = ms_view_cells(view_buf);
            for (uint32_t i = 0; i < w * h; i++) {
                CHECK(!(cells[i] & (MS_CELL_MINE | MS_CELL_EXPLODED)));
                if (!(cells[i] & MS_CELL_REVEALED)) {
                    CHECK_EQ(cells[i] & MS_CELL_ADJACENT_MASK, MS_CELL_NO_ADJACENT);
                }
            }
        }
        env_close(&env);
    }
}

/* ---------------------------------------------------------------- revisions */

static void test_revision_checks(void) {
    test_case("generation/revision validation and the MS_REVISION_MAX guard");
    const GameScript *s = &GAME_SCRIPTS[GAME_SCRIPT_REVEALED_CLUES_PUBLIC_ONLY];
    game_env env;
    env_open(&env, 5, 5, 2, s->layout, s->layout_count, 0.0, 0);
    size_t len = env_view_len(&env);
    bool changed = true;
    CHECK_STATUS(ms_game_act(env.game, 0, 0, MS_ACTION_REVEAL, 0, 0, &changed),
                 MS_ERR_INVALID_REVISION);
    CHECK(changed); /* untouched: outputs are written only on MS_OK */
    CHECK_STATUS(ms_game_act(env.game, MS_GENERATION_MAX + 1, 0, MS_ACTION_REVEAL, 0, 0, NULL),
                 MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_game_act(env.game, 2, 0, MS_ACTION_REVEAL, 0, 0, NULL), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_game_act(env.game, 1, MS_REVISION_MAX + 1, MS_ACTION_REVEAL, 0, 0, NULL),
                 MS_ERR_INVALID_REVISION);
    CHECK_STATUS(ms_game_act(env.game, 1, 0, 0, 0, 0, NULL), MS_ERR_INVALID_ACTION);
    CHECK_STATUS(ms_game_act(env.game, 1, 0, UINT32_MAX, 0, 0, NULL), MS_ERR_INVALID_ACTION);
    /* Order: action, bounds, revision. */
    CHECK_STATUS(ms_game_act(env.game, 0, 7, 9, 99, 0, NULL), MS_ERR_INVALID_ACTION);
    CHECK_STATUS(ms_game_act(env.game, 0, 7, MS_ACTION_FLAG, 0, 5, NULL), MS_ERR_OUT_OF_BOUNDS);
    CHECK_STATUS(ms_game_act(env.game, 1, 0, MS_ACTION_REVEAL, 0, 0, &changed), MS_OK);
    CHECK(changed);

    /* At MS_REVISION_MAX every action fails after its other checks, no-ops
     * included, so revisions never wrap; nothing changes. */
    ms_game_test_set_revision(env.game, MS_REVISION_MAX - 1);
    CHECK_STATUS(ms_game_act(env.game, 1, MS_REVISION_MAX - 1, MS_ACTION_FLAG, 4, 4, &changed),
                 MS_OK);
    CHECK(changed);
    take_view(&env, before_buf);
    CHECK_EQ(((const ms_view_header *)before_buf)->revision, MS_REVISION_MAX);
    const uint32_t r = MS_REVISION_MAX;
    CHECK_STATUS(ms_game_act(env.game, 1, r, MS_ACTION_FLAG, 4, 4, &changed),
                 MS_ERR_RESOURCE_EXHAUSTED);
    CHECK(changed); /* untouched since the last successful call */
    CHECK_STATUS(ms_game_act(env.game, 1, r, MS_ACTION_REVEAL, 0, 0, NULL),
                 MS_ERR_RESOURCE_EXHAUSTED); /* would be a no-op */
    CHECK_STATUS(ms_game_act(env.game, 1, r, 4, 0, 0, NULL), MS_ERR_INVALID_ACTION);
    CHECK_STATUS(ms_game_act(env.game, 1, r, MS_ACTION_FLAG, 5, 0, NULL), MS_ERR_OUT_OF_BOUNDS);
    CHECK_STATUS(ms_game_act(env.game, 1, r - 1, MS_ACTION_FLAG, 4, 4, NULL), MS_ERR_STALE_REVISION);
    uint32_t safe[1] = {15};
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, r, safe, 1, NULL, 0, &changed),
                 MS_ERR_RESOURCE_EXHAUSTED);
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, r, NULL, 0, NULL, 0, &changed),
                 MS_ERR_RESOURCE_EXHAUSTED);
    uint32_t revealed_cell[1] = {0};
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, r, revealed_cell, 1, NULL, 0, &changed),
                 MS_ERR_INVALID_DEDUCTIONS);
    CHECK(views_equal(before_buf, take_view(&env, view_buf), len));
    CHECK_STATUS(ms_game_observe(env.game, 1, r, obs_buf, ms_obs_size(5, 5)), MS_OK);
    env_close(&env);

    /* A terminal game reports game_over even at MS_REVISION_MAX. */
    env_open(&env, 5, 5, 2, s->layout, s->layout_count, 0.0, 0);
    reveal_first(&env, 0, 0);
    CHECK_STATUS(ms_game_act(env.game, 1, 1, MS_ACTION_REVEAL, 3, 1, &changed), MS_OK);
    ms_game_test_set_revision(env.game, MS_REVISION_MAX);
    CHECK_STATUS(ms_game_act(env.game, 1, MS_REVISION_MAX, MS_ACTION_FLAG, 4, 4, NULL),
                 MS_ERR_GAME_OVER);
    env_close(&env);
}

static void test_timer_rounding(void) {
    test_case("timer: whole milliseconds (ties to even), frozen at the end");
    const GameScript *s = &GAME_SCRIPTS[GAME_SCRIPT_REVEALED_CLUES_PUBLIC_ONLY];
    game_env env;
    env_open(&env, 5, 5, 2, s->layout, s->layout_count, 1000.25, 0);
    reveal_first(&env, 0, 0);
    const double steps[][2] = {{2.5, 2.0}, {1.0, 4.0}, {0.4, 4.0}, {0.2, 4.0}, {0.5, 5.0}};
    double offset = 0.0;
    for (uint32_t i = 0; i < 5; i++) {
        offset += steps[i][0];
        env.fake.now = 1000.25 + offset;
        CHECK(take_view(&env, view_buf)->elapsed_ms == steps[i][1]);
    }
    bool changed = false;
    CHECK_STATUS(ms_game_act(env.game, 1, 1, MS_ACTION_REVEAL, 3, 1, &changed), MS_OK);
    env.fake.now += 1e6;
    CHECK(take_view(&env, view_buf)->elapsed_ms == 5.0);
    env_close(&env);
}

/* ---------------------------------------------------------------- observation */

/* tests.test_game.SerializationTests.test_revealed_clues_contains_only_public_safe_cells */
static void test_observation(void) {
    test_case("revealed_clues_public_only: observation export");
    const GameScript *s = &GAME_SCRIPTS[GAME_SCRIPT_REVEALED_CLUES_PUBLIC_ONLY];
    game_env env;
    env_open(&env, 5, 5, 2, s->layout, s->layout_count, 0.0, 0);
    size_t obs_len = ms_obs_size(5, 5);

    CHECK_STATUS(ms_game_observe(env.game, 1, 0, obs_buf, obs_len), MS_ERR_GAME_NOT_STARTED);
    reveal_first(&env, 0, 0);
    bool changed = false;
    CHECK_STATUS(ms_game_act(env.game, 1, 1, MS_ACTION_FLAG, 3, 1, &changed), MS_OK);

    /* Buffer problems first, then revision checks. */
    CHECK_STATUS(ms_game_observe(env.game, 1, 2, NULL, obs_len), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_observe(env.game, 1, 2, (uint8_t *)obs_buf + 4, obs_len),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_observe(env.game, 1, 0, obs_buf, obs_len - 8), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_observe(env.game, 1, 1, obs_buf, obs_len), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_game_observe(env.game, 2, 2, obs_buf, obs_len), MS_ERR_STALE_REVISION);
    CHECK_STATUS(ms_game_observe(env.game, 0, 2, obs_buf, obs_len), MS_ERR_INVALID_REVISION);

    CHECK_STATUS(ms_game_observe(env.game, 1, 2, obs_buf, obs_len), MS_OK);
    CHECK_STATUS(ms_obs_validate(obs_buf, obs_len), MS_OK);
    const ms_obs_header *o = (const ms_obs_header *)obs_buf;
    CHECK_EQ(o->width, 5);
    CHECK_EQ(o->height, 5);
    CHECK_EQ(o->total_mines, 2);
    CHECK_EQ(o->revealed, 15);

    /* Exactly the render's digits (PROB_CASE_TWO_MINES_OPENED): no flags,
     * nothing derived from the layout. */
    const ProbabilityCase *pc = &PROBABILITY_CASES[PROB_CASE_TWO_MINES_OPENED];
    int8_t dense[25];
    CHECK(fixture_case_dense_clues(pc, dense, 25));
    const uint8_t *clues = ms_obs_clues(obs_buf);
    for (uint32_t i = 0; i < 25; i++) {
        uint8_t expected = dense[i] == FIXTURE_HIDDEN_CELL ? MS_CLUE_HIDDEN : (uint8_t)dense[i];
        CHECK_EQ(clues[i], expected);
    }
    CHECK_EQ(clues[16], MS_CLUE_HIDDEN); /* flagged mine: not evidence */

    /* The caller owns the buffer: scribbling on it cannot affect the game. */
    base_memcpy(obs_copy, obs_buf, obs_len);
    take_view(&env, before_buf);
    base_memset(obs_buf, 0xA5, obs_len);
    CHECK_STATUS(ms_game_observe(env.game, 1, 2, obs_buf, obs_len), MS_OK);
    CHECK(base_memcmp(obs_buf, obs_copy, obs_len) == 0);
    CHECK(views_equal(before_buf, take_view(&env, view_buf), env_view_len(&env)));

    /* Finished games have no observation. */
    CHECK_STATUS(ms_game_act(env.game, 1, 2, MS_ACTION_REVEAL, 3, 3, &changed), MS_OK);
    CHECK_STATUS(ms_game_observe(env.game, 1, 3, obs_buf, obs_len), MS_ERR_GAME_OVER);
    env_close(&env);
}

static void test_view_buffers(void) {
    test_case("view buffer checks");
    game_env env;
    env_open(&env, 9, 9, 10, NULL, 0, 0.0, 5);
    size_t len = env_view_len(&env);
    CHECK_STATUS(ms_game_view(env.game, NULL, len), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_view(env.game, (uint8_t *)view_buf + 4, len), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_view(env.game, view_buf, len - 8), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_view(env.game, view_buf, len + 8), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_view(NULL, view_buf, len), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_act(NULL, 1, 0, MS_ACTION_REVEAL, 0, 0, NULL), MS_ERR_INVALID_BUFFER);
    env_close(&env);
}

/* ---------------------------------------------------------------- deductions */

static void test_deduction_details(void) {
    test_case("deductions: list checks, duplicates, wrong proofs use REVEAL semantics");
    const GameScript *s = &GAME_SCRIPTS[GAME_SCRIPT_DEDUCE_BATCH_WINS];
    game_env env;
    env_open(&env, 5, 5, 2, s->layout, s->layout_count, 0.0, 0);
    bool changed = true;
    uint32_t safe[3] = {15, 15, 20};
    uint32_t mines[2] = {18, 18};
    /* NULL lists with a count are buffer errors, before anything else. */
    CHECK_STATUS(ms_game_apply_deductions(env.game, 7, 9, NULL, 1, NULL, 0, &changed),
                 MS_ERR_INVALID_BUFFER);
    CHECK(changed); /* untouched */
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, 0, NULL, 0, NULL, 2, NULL),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_game_apply_deductions(NULL, 1, 0, NULL, 0, NULL, 0, NULL),
                 MS_ERR_INVALID_BUFFER);
    reveal_first(&env, 0, 0);

    /* An output aliasing an input list cannot alter a rejected batch:
     * *changed is written only on MS_OK, after the lists were read. */
    union {
        uint32_t list[2];
        bool flag;
    } alias;
    alias.list[0] = 99; /* off the board: the batch is rejected */
    alias.list[1] = 20;
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, 1, alias.list, 2, NULL, 0, &alias.flag),
                 MS_ERR_INVALID_DEDUCTIONS);
    CHECK_EQ(alias.list[0], 99);
    CHECK_EQ(alias.list[1], 20);

    /* Duplicates within a list are allowed and applied once. */
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, 1, safe, 3, mines, 2, &changed), MS_OK);
    CHECK(changed);
    const ms_view_header *h = take_view(&env, view_buf);
    CHECK_EQ(h->revision, 2);
    CHECK_EQ(h->flags, 1);
    check_board(view_buf, "00000" "00000" "11211" "1##F#" "1####", "deduction duplicates");

    /* A wrong "proof" is not checked against the private layout: it is a
     * reveal like any other, and the batch stops once the game ends. Mines
     * are flagged first. */
    uint32_t wrong_safe[2] = {16, 21};
    uint32_t mine_23[1] = {23};
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, 2, wrong_safe, 2, mine_23, 1, &changed),
                 MS_OK);
    CHECK(changed);
    h = take_view(&env, view_buf);
    CHECK_EQ(h->status, MS_GAME_LOST);
    CHECK_EQ(h->revision, 3);
    CHECK_EQ(h->flags, 2);
    check_board(view_buf, "00000" "00000" "11211" "1X#F#" "1##F#", "wrong proof");
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, 3, NULL, 0, NULL, 0, &changed),
                 MS_ERR_GAME_OVER);
    env_close(&env);
}

/* Actions, placement and batches never allocate after creation. */
static void test_no_allocation_after_create(void) {
    test_case("actions and batches never allocate (failure injection)");
    const GameScript *s = &GAME_SCRIPTS[GAME_SCRIPT_REAL_SOLVER_CORRECTS_WRONG_FLAG_AND_WINS];
    game_env env;
    env_open(&env, 5, 5, 2, s->layout, s->layout_count, 0.0, 0);
    rt_mem_set_failure(&env.mem, 0, RT_MEM_FOREVER);
    uint64_t requests = env.mem.requests;
    reveal_first(&env, 0, 0);
    bool changed = false;
    CHECK_STATUS(ms_game_act(env.game, 1, 1, MS_ACTION_FLAG, 3, 0, &changed), MS_OK);
    uint32_t safe[8] = {15, 17, 19, 20, 21, 22, 23, 24};
    uint32_t mines[2] = {16, 18};
    CHECK_STATUS(ms_game_apply_deductions(env.game, 1, 2, safe, 8, mines, 2, &changed), MS_OK);
    CHECK_EQ(take_view(&env, view_buf)->status, MS_GAME_WON);
    CHECK_EQ(env.mem.requests, requests);
    CHECK_EQ(env.mem.failures, 0);
    rt_mem_set_failure(&env.mem, 0, 0);
    env_close(&env);

    /* Placement on the largest board too. */
    env_open(&env, 80, 80, 1280, NULL, 0, 0.0, 19);
    rt_mem_set_failure(&env.mem, 0, RT_MEM_FOREVER);
    reveal_first(&env, 40, 40);
    check_first_reveal(&env, 40, 40);
    CHECK_EQ(env.mem.failures, 0);
    rt_mem_set_failure(&env.mem, 0, 0);
    env_close(&env);
}

void test_game(void) {
    test_config_cases();
    test_create_arguments();
    test_initial_view();
    test_game_scripts();
    test_row_major();
    test_first_reveal_property();
    test_corner_density();
    test_uniform_placement();
    test_placement_determinism();
    test_no_leak_property();
    test_revision_checks();
    test_timer_rounding();
    test_observation();
    test_view_buffers();
    test_deduction_details();
    test_no_allocation_after_create();
}
