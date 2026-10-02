/* Tests for c/planner.c (ms_plan). The exact mode is checked against an
 * independent reference written here: a brute-force game tree over full
 * public observations (explicit flood fill, every hidden cell tried, no
 * closure, no equivalence or pruning), on frozen fixtures whose values were
 * also computed by a separate Python enumeration, and on random small
 * positions from the C PRNG. Rollouts are checked for honest bookkeeping
 * (paired rounds, incomplete rounds, terminal outcomes), statistical
 * agreement with the continuation policy's exact win rate, and the absence
 * of clairvoyance. Clocks are fake: a step of 0 means time never passes,
 * so only work caps bind and every result is reproducible. */

#include "test_support.h"

#include "planner.h"
#include "posterior.h"

#define MAX_CELLS MS_SOLVER_MAX_CELLS
#define OBS_WORDS ((sizeof(ms_obs_header) + MAX_CELLS + 7u) / 8u)
#define RESULT_WORDS ((sizeof(ms_result_header) + (size_t)MAX_CELLS * 9u + 7u) / 8u)

static uint64_t obs_buf[OBS_WORDS];
static uint64_t obs_copy[OBS_WORDS];
static uint64_t solve_buf[RESULT_WORDS];

/* ---------------------------------------------------------------- helpers */

typedef struct plan_env {
    rt_mem mem;
    fake_clock fake;
    rt_clock clock;
    ms_plan_limits limits;
} plan_env;

static void env_open(plan_env *env, double step) {
    rt_mem_init(&env->mem, RT_MEM_UNLIMITED);
    fake_clock_init(&env->clock, &env->fake, 1000.0, step);
    ms_plan_limits_default(&env->limits);
}

static void env_close(plan_env *env) {
    CHECK_EQ(env->mem.live, 0);
    CHECK_EQ(env->mem.live_blocks, 0);
    CHECK_EQ(env->mem.misuses, 0);
    rt_mem_dispose(&env->mem);
}

static uint64_t f64_bits(double value) {
    union {
        double d;
        uint64_t u;
    } pun;
    pun.d = value;
    return pun.u;
}

/* corec's fast_sqrt is approximate on Windows. Bisection gives an independent
 * reference for these bounded test values, unlike the planner's Newton method. */
static double ref_sqrt(double value) {
    CHECK(value >= 0.0 && value - value == 0.0);
    if (value == 0.0) return 0.0;
    double lo = 0.0, hi = value > 1.0 ? value : 1.0;
    for (uint32_t i = 0; i < 100u; i++) {
        double mid = lo + (hi - lo) * 0.5;
        if (mid * mid < value) lo = mid;
        else hi = mid;
    }
    return lo + (hi - lo) * 0.5;
}

/* The documented ESTIMATED standard error for `wins` of `done` sampled
 * rounds: the Jeffreys posterior standard deviation, times the finite-
 * population factor `fpc` (1 for draws). */
static double jeffreys_se(uint32_t wins, uint32_t done, double fpc) {
    double a = (double)wins + 0.5, b = (double)(done - wins) + 0.5, sum = a + b;
    return ref_sqrt(a * b / (sum * sum * (sum + 1.0)) * fpc);
}

/* The counts of a result stay within the limits of its call, which the
 * result does not carry (so ms_plan_result_validate cannot check them). */
static void check_counts(const ms_plan_limits *limits, const ms_plan_result *r) {
    uint32_t rows = limits->exact_layout_limit > limits->sample_count
                        ? limits->exact_layout_limit
                        : limits->sample_count;
    CHECK(r->layouts <= rows);
    CHECK(r->exact_total <= limits->exact_layout_limit);
    CHECK(r->search_nodes <= limits->exact_node_limit);
    if (r->status != MS_PLAN_EXACT) CHECK(r->candidates <= limits->candidate_limit);
}

/* ms_plan plus the invariants every call keeps: inputs untouched, the
 * workspace back to its live bytes, blocks and budget without misuse, and
 * a valid canonical result within the limits on MS_OK. */
static ms_status plan(plan_env *env, const void *obs, size_t obs_len, ms_plan_result *result) {
    size_t live = env->mem.live;
    size_t blocks = env->mem.live_blocks;
    size_t budget = env->mem.budget;
    uint64_t misuses = env->mem.misuses;
    ms_plan_limits limits_copy;
    base_memcpy(&limits_copy, &env->limits, sizeof(limits_copy));
    base_memcpy(obs_copy, obs, obs_len);
    ms_status status = ms_plan(obs, obs_len, &env->limits, &env->mem, &env->clock, result,
                               sizeof(*result));
    CHECK_EQ(env->mem.live, live);
    CHECK_EQ(env->mem.live_blocks, blocks);
    CHECK_EQ(env->mem.budget, budget);
    CHECK_EQ(env->mem.misuses, misuses);
    CHECK(base_memcmp(obs_copy, obs, obs_len) == 0);
    CHECK(base_memcmp(&limits_copy, &env->limits, sizeof(limits_copy)) == 0);
    if (status == MS_OK) {
        CHECK_STATUS(ms_plan_result_validate(obs, obs_len, result, sizeof(*result)), MS_OK);
        check_counts(&env->limits, result);
    }
    return status;
}

static ms_status plan_stats(plan_env *env, const void *obs, size_t obs_len,
                            ms_plan_result *result, ms_plan_test_stats *stats) {
    size_t live = env->mem.live;
    ms_status status = ms_plan_test_run(obs, obs_len, &env->limits, &env->mem, &env->clock,
                                        result, sizeof(*result), stats);
    CHECK_EQ(env->mem.live, live);
    CHECK_EQ(env->mem.misuses, 0);
    if (status == MS_OK) {
        CHECK_STATUS(ms_plan_result_validate(obs, obs_len, result, sizeof(*result)), MS_OK);
        check_counts(&env->limits, result);
    }
    return status;
}

static void print_u32(const char *label, uint64_t value) {
    test_print(label);
    test_print_u64(value);
}

/* ---------------------------------------------------------------- boards */

/* A position: the generating layout and which cells are revealed. Rows use
 * '*' for a mine, 'o' for a revealed safe cell, '.' for a hidden safe cell.
 * The planner only ever sees the derived observation. */
typedef struct board {
    uint32_t width;
    uint32_t height;
    uint32_t cells;
    uint32_t mines;
    uint8_t mine[MAX_CELLS];
    uint8_t revealed[MAX_CELLS];
} board;

static board board_a;
static board board_b;

static uint32_t near_mines(uint32_t width, uint32_t height, const uint8_t *mine, uint32_t cell) {
    uint32_t around[8];
    uint32_t n = rt_grid_neighbors(width, height, cell, around), count = 0;
    for (uint32_t i = 0; i < n; i++) count += mine[around[i]];
    return count;
}

static void board_rows(board *b, uint32_t width, uint32_t height, const char *rows) {
    b->width = width;
    b->height = height;
    b->cells = width * height;
    b->mines = 0;
    CHECK_EQ(base_strlen(rows), b->cells);
    for (uint32_t i = 0; i < b->cells; i++) {
        char ch = rows[i];
        CHECK(ch == '*' || ch == 'o' || ch == '.');
        b->mine[i] = ch == '*';
        b->revealed[i] = ch == 'o';
        b->mines += b->mine[i];
    }
}

static size_t board_obs(const board *b, void *obs) {
    size_t len = ms_obs_size(b->width, b->height);
    CHECK(len > 0);
    CHECK_STATUS(ms_obs_init(obs, len, b->width, b->height, b->mines), MS_OK);
    for (uint32_t i = 0; i < b->cells; i++) {
        if (!b->revealed[i]) continue;
        CHECK(!b->mine[i]);
        uint8_t clue = (uint8_t)near_mines(b->width, b->height, b->mine, i);
        CHECK_STATUS(ms_obs_set_clue(obs, i, clue), MS_OK);
    }
    CHECK_STATUS(ms_obs_validate(obs, len), MS_OK);
    return len;
}

/* Reveals `cell` of the board with the game's flood fill. */
static void board_reveal(board *b, uint32_t cell) {
    uint32_t stack[MAX_CELLS];
    uint32_t n = 0, around[8];
    if (b->revealed[cell]) return;
    CHECK(!b->mine[cell]);
    b->revealed[cell] = 1;
    stack[n++] = cell;
    while (n > 0) {
        uint32_t x = stack[--n];
        if (near_mines(b->width, b->height, b->mine, x) != 0) continue;
        uint32_t k = rt_grid_neighbors(b->width, b->height, x, around);
        for (uint32_t i = 0; i < k; i++) {
            if (b->revealed[around[i]]) continue;
            b->revealed[around[i]] = 1;
            stack[n++] = around[i];
        }
    }
}

/* A random game position: mines outside the first click's 3x3 block, the
 * first click flooded, then `extra` random safe reveals. */
static void board_random(board *b, rt_rng *rng, uint32_t width, uint32_t height,
                         uint32_t mines, uint32_t extra) {
    uint32_t cand[MAX_CELLS], around[8];
    b->width = width;
    b->height = height;
    b->cells = width * height;
    b->mines = mines;
    base_memset(b->mine, 0, b->cells);
    base_memset(b->revealed, 0, b->cells);
    uint32_t first = rt_rng_below(rng, b->cells), count = 0;
    uint32_t k = rt_grid_neighbors(width, height, first, around);
    for (uint32_t c = 0; c < b->cells; c++) {
        bool near = c == first;
        for (uint32_t i = 0; i < k; i++) near = near || around[i] == c;
        if (!near) cand[count++] = c;
    }
    CHECK(mines <= count);
    rt_rng_choose(rng, cand, count, mines);
    for (uint32_t i = 0; i < mines; i++) b->mine[cand[i]] = 1;
    board_reveal(b, first);
    for (uint32_t e = 0; e < extra; e++) {
        uint32_t safe = 0;
        for (uint32_t c = 0; c < b->cells; c++) {
            if (!b->mine[c] && !b->revealed[c]) cand[safe++] = c;
        }
        if (safe == 0) break;
        board_reveal(b, cand[rt_rng_below(rng, safe)]);
    }
}

static uint32_t board_hidden(const board *b) {
    uint32_t hidden = 0;
    for (uint32_t i = 0; i < b->cells; i++) hidden += !b->revealed[i];
    return hidden;
}

/* Reveals every cell ms_solve proves safe until none is left: the position
 * a player reaches before having to guess. */
static void board_close(board *b, double time_budget) {
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    fake_clock fake;
    rt_clock clock;
    fake_clock_init(&clock, &fake, 0.0, 0.0);
    ms_infer_limits limits;
    ms_limits_default(&limits);
    limits.time_budget_ms = time_budget;
    for (;;) {
        size_t len = board_obs(b, obs_buf);
        size_t rlen = ms_result_size(b->width, b->height);
        CHECK_STATUS(ms_solve(obs_buf, len, &limits, &mem, &clock, solve_buf, rlen), MS_OK);
        const uint8_t *flags = ms_result_flags(solve_buf);
        bool any = false;
        for (uint32_t c = 0; c < b->cells; c++) {
            if (!b->revealed[c] && (flags[c] & MS_PCELL_PROVEN_SAFE)) {
                CHECK(!b->mine[c]);
                board_reveal(b, c);
                any = true;
            }
        }
        if (!any) break;
    }
    CHECK_EQ(mem.live, 0);
    rt_mem_dispose(&mem);
}

/* ------------------------------------------------- independent reference
 *
 * Positions of at most 64 cells. Layouts are enumerated from the clues (up
 * to REF_LAYOUTS); the value search takes at most REF_SEARCH_LAYOUTS, so
 * a set of layouts is one 64-bit word. V(R, S): R the revealed cells, S the
 * compatible layouts. Won when only mines are hidden; otherwise the best
 * over EVERY hidden cell (certain or not) of the sum over the observations
 * its reveal can produce - the exact flood region plus every clue in it -
 * of V(R', S'). */

#define REF_CELLS 64u
#define REF_LAYOUTS 256u
#define REF_SEARCH_LAYOUTS 64u
#define REF_MEMO ((uint32_t)1 << 17)

typedef struct ref_pos {
    uint32_t width;
    uint32_t height;
    uint32_t cells;
    uint32_t mines;
    uint64_t revealed;
    uint64_t nbr[REF_CELLS];
    uint32_t count;
    uint64_t layout[REF_LAYOUTS];
    uint8_t near[REF_LAYOUTS][REF_CELLS];
    bool overflow;
} ref_pos;

typedef struct ref_entry {
    uint64_t r;
    uint64_t s;
    uint32_t value; /* 0: empty */
    uint32_t reserved;
} ref_entry;

static ref_pos ref;
static ref_entry ref_memo[REF_MEMO];
static uint32_t ref_slots[REF_MEMO]; /* occupied slots, for a cheap clear */
static uint32_t ref_used;

static uint32_t popcount64(uint64_t x) {
    uint32_t n = 0;
    while (x) {
        x &= x - 1u;
        n++;
    }
    return n;
}

static uint32_t lowest(uint64_t x) {
    uint32_t i = 0;
    while (!(x & 1u)) {
        x >>= 1;
        i++;
    }
    return i;
}

static uint64_t bit(uint32_t i) {
    return (uint64_t)1 << i;
}

/* Enumerates every layout compatible with the observation (at most
 * REF_CELLS cells); false if there are more than max_layouts. */
static bool ref_load_obs(const void *obs, uint32_t max_layouts) {
    const ms_obs_header *h = (const ms_obs_header *)obs;
    const uint8_t *clue = ms_obs_clues(obs);
    uint32_t cells = h->width * h->height;
    CHECK(cells <= REF_CELLS && max_layouts <= REF_LAYOUTS);
    base_memset(&ref, 0, sizeof(ref));
    ref.width = h->width;
    ref.height = h->height;
    ref.cells = cells;
    ref.mines = h->total_mines;
    uint32_t hidden[REF_CELLS], nh = 0, around[8];
    for (uint32_t c = 0; c < cells; c++) {
        uint32_t k = rt_grid_neighbors(h->width, h->height, c, around);
        for (uint32_t i = 0; i < k; i++) ref.nbr[c] |= bit(around[i]);
        if (clue[c] != MS_CLUE_HIDDEN) {
            ref.revealed |= bit(c);
        } else {
            hidden[nh++] = c;
        }
    }
    if (ref.mines > nh) return false;
    CHECK(nh < 64u);
    /* Gosper's hack over m-subsets of the hidden cells. */
    uint64_t x = ref.mines == 0 ? 0 : (bit(ref.mines) - 1u);
    uint64_t end = bit(nh);
    for (;;) {
        uint64_t mask = 0;
        for (uint32_t i = 0; i < nh; i++) {
            if (x & bit(i)) mask |= bit(hidden[i]);
        }
        bool ok = true;
        for (uint32_t c = 0; c < cells && ok; c++) {
            if (clue[c] != MS_CLUE_HIDDEN) ok = popcount64(mask & ref.nbr[c]) == clue[c];
        }
        if (ok) {
            if (ref.count == max_layouts) return false;
            uint32_t l = ref.count++;
            ref.layout[l] = mask;
            for (uint32_t c = 0; c < cells; c++) {
                ref.near[l][c] = (uint8_t)popcount64(mask & ref.nbr[c]);
            }
        }
        if (x == 0) break;
        uint64_t low = x & (0 - x), r = x + low;
        x = (((r ^ x) >> 2) / low) | r;
        if (x >= end) break;
    }
    return true;
}

static uint64_t ref_obs[(sizeof(ms_obs_header) + REF_CELLS + 7u) / 8u];

/* The board's observation, for the value search. */
static bool ref_load(const board *b) {
    CHECK(b->cells <= REF_CELLS);
    board_obs(b, ref_obs);
    return ref_load_obs(ref_obs, REF_SEARCH_LAYOUTS);
}

static uint64_t ref_flood(uint64_t r, uint32_t cell, uint32_t l) {
    uint64_t out = r | bit(cell);
    uint32_t stack[REF_CELLS], n = 0;
    stack[n++] = cell;
    while (n > 0) {
        uint32_t x = stack[--n];
        if (ref.near[l][x] != 0) continue;
        uint64_t open = ref.nbr[x] & ~out;
        out |= open;
        while (open) {
            stack[n++] = lowest(open);
            open &= open - 1u;
        }
    }
    return out;
}

static uint32_t ref_value(uint64_t r, uint64_t s);

/* Layouts of s (cell safe) grouped by the full observation of the reveal. */
static uint32_t ref_action(uint64_t r, uint64_t s, uint32_t cell) {
    uint64_t group_r[REF_SEARCH_LAYOUTS], group_s[REF_SEARCH_LAYOUTS];
    uint32_t group_rep[REF_SEARCH_LAYOUTS], groups = 0;
    for (uint64_t rest = s; rest; rest &= rest - 1u) {
        uint32_t l = lowest(rest);
        if (ref.layout[l] & bit(cell)) continue;
        uint64_t rn = ref_flood(r, cell, l);
        uint32_t g = 0;
        for (; g < groups; g++) {
            if (group_r[g] != rn) continue;
            bool same = true;
            for (uint64_t fresh = rn & ~r; fresh && same; fresh &= fresh - 1u) {
                uint32_t x = lowest(fresh);
                same = ref.near[l][x] == ref.near[group_rep[g]][x];
            }
            if (same) break;
        }
        if (g == groups) {
            group_r[g] = rn;
            group_s[g] = 0;
            group_rep[g] = l;
            groups++;
        }
        group_s[g] |= bit(l);
    }
    uint32_t total = 0;
    for (uint32_t g = 0; g < groups; g++) total += ref_value(group_r[g], group_s[g]);
    return total;
}

static uint32_t ref_value(uint64_t r, uint64_t s) {
    if (ref.cells - popcount64(r) == ref.mines) return popcount64(s);
    if (ref.overflow) return 0;
    uint64_t h = rt_mix64(r * 0x9E3779B97F4A7C15ull ^ rt_mix64(s));
    uint32_t at = (uint32_t)h & (REF_MEMO - 1u);
    for (; ref_memo[at].value != 0; at = (at + 1u) & (REF_MEMO - 1u)) {
        if (ref_memo[at].r == r && ref_memo[at].s == s) return ref_memo[at].value;
    }
    uint32_t best = 0;
    for (uint32_t c = 0; c < ref.cells; c++) {
        if (r & bit(c)) continue;
        uint32_t v = ref_action(r, s, c);
        if (v > best) best = v;
    }
    if (ref_used * 4u >= REF_MEMO * 3u) {
        ref.overflow = true;
        return 0;
    }
    /* The slot may have been taken by recursive inserts: probe again. */
    at = (uint32_t)h & (REF_MEMO - 1u);
    while (ref_memo[at].value != 0) at = (at + 1u) & (REF_MEMO - 1u);
    ref_memo[at].r = r;
    ref_memo[at].s = s;
    ref_memo[at].value = best;
    ref_slots[ref_used++] = at;
    return best;
}

static void ref_clear(void) {
    for (uint32_t i = 0; i < ref_used; i++) ref_memo[ref_slots[i]].value = 0;
    ref_used = 0;
    ref.overflow = false;
}

/* The root: value of every hidden cell, the optimum and safe counts. */
typedef struct ref_root {
    uint32_t value[REF_CELLS];
    uint32_t safe[REF_CELLS];
    uint32_t best;
    uint32_t candidates; /* hidden cells safe in some layout */
    bool certain;        /* some hidden cell is safe in every layout */
} ref_root;

/* Safe counts of every hidden cell (any number of loaded layouts). */
static void ref_counts(ref_root *root) {
    base_memset(root, 0, sizeof(*root));
    for (uint32_t c = 0; c < ref.cells; c++) {
        if (ref.revealed & bit(c)) continue;
        for (uint32_t l = 0; l < ref.count; l++) root->safe[c] += !(ref.layout[l] & bit(c));
        if (root->safe[c] > 0) root->candidates++;
        if (root->safe[c] == ref.count) root->certain = true;
    }
}

/* ref_counts plus the value of every hidden cell; false when the layouts
 * do not fit one word or the memo overflows. */
static bool ref_solve(ref_root *root) {
    ref_counts(root);
    if (ref.count > REF_SEARCH_LAYOUTS) return false;
    ref_clear();
    uint64_t all = ref.count == 64u ? ~(uint64_t)0 : bit(ref.count) - 1u;
    for (uint32_t c = 0; c < ref.cells; c++) {
        if (ref.revealed & bit(c)) continue;
        root->value[c] = ref_action(ref.revealed, all, c);
        if (root->value[c] > root->best) root->best = root->value[c];
    }
    return !ref.overflow;
}

/* ---------------------------------------------------------------- fixtures */

typedef struct fixture {
    const char *name;
    uint32_t width;
    uint32_t height;
    const char *rows;
    uint32_t layouts;   /* compatible layouts (Python reference) */
    uint32_t best;      /* optimal number of layouts won */
    uint32_t top_safe;  /* largest safe count of any hidden cell */
} fixture;

/* Values from an independent Python enumeration (written alongside these
 * tests, not shipped), re-derived below by the C reference. "notsafest": every optimal reveal
 * is strictly less safe than the safest cell. "geometry": cells with equal
 * odds but different values. "flood": zeros open regions. */
static const fixture FIXTURES[] = {
    {"notsafest_a", 5, 4, ".*.*." "..o*." "**ooo" "..ooo", 19, 15, 18},
    {"notsafest_b", 5, 5, "*.*o*" ".*ooo" ".oooo" "*.ooo" "**ooo", 6, 3, 4},
    {"notsafest_c", 6, 3, "..oooo" ".**ooo" "***o.*", 20, 5, 12},
    {"notsafest_d", 5, 5, "*...*" "*..*." "o*o*o" "ooooo" "ooooo", 25, 10, 20},
    {"geometry_a", 6, 3, "*.oo.*" "oo.*.." "oo*.*.", 40, 21, 30},
    {"geometry_b", 5, 4, "oo..*" "oo*.." "ooo.." ".*...", 9, 7, 8},
    {"geometry_c", 5, 4, "*...." "*.*.." "oo*.." "oo...", 44, 36, 40},
    {"flood_a", 4, 4, "..oo" ".*oo" "*o.*" "*..*", 55, 24, 32},
    {"flood_b", 7, 3, "oooo*.." "oooo.*o" "oo***..", 10, 7, 8},
    {"flood_c", 7, 3, "oooo..*" "oooo*.*" "*.o*...", 60, 30, 40},
    {"fifty", 2, 3, "oo" "oo" "*.", 2, 1, 1},
    {"double_fifty", 2, 6, "*." "oo" "oo" "oo" "oo" ".*", 4, 1, 2},
};

#define FIXTURE_COUNT (sizeof(FIXTURES) / sizeof(FIXTURES[0]))

static const fixture *find_fixture(const char *name) {
    for (uint32_t i = 0; i < FIXTURE_COUNT; i++) {
        if (base_strcmp(FIXTURES[i].name, name) == 0) return &FIXTURES[i];
    }
    CHECK(false);
    return NULL;
}

static size_t fixture_obs(const fixture *f, board *b, void *obs) {
    board_rows(b, f->width, f->height, f->rows);
    return board_obs(b, obs);
}

/* Gold fixtures, given as raw clue arrays, with the optimal number of
 * layouts won after revealing each hidden cell first and that cell's safe
 * count. Both tables were computed by two independent brute-force
 * enumerations over full flood observations and every action (the parent
 * review's oracle and this suite's Python reference). The C reference below
 * re-derives every layout and safe count, and every value where its
 * one-word layout sets allow (gold_a).
 *   gold_a: the safest cells 0..4 (8/10 safe) win only 4/10; the optimum
 *           5/10 starts at 5, 6, 8 or 9 (5/10 safe).
 *   gold_b: 180 layouts; cell 15 (150/180 safe) uniquely wins 140/180,
 *           while the safest cells (153/180) win at most 139/180. */
typedef struct gold_fixture {
    const char *name;
    uint32_t width;
    uint32_t height;
    uint32_t mines;
    uint8_t clue[20];
    uint32_t layouts;
    uint32_t best;           /* optimal layouts won */
    uint32_t optima;         /* cells achieving it */
    uint32_t top_safe;       /* largest safe count */
    uint32_t best_of_safest; /* best value among the safest cells */
    uint32_t value[20];      /* per first reveal; 0 for revealed cells */
    uint32_t safe[20];
} gold_fixture;

#define GH MS_CLUE_HIDDEN

static const gold_fixture GOLD[] = {
    {"gold_a", 5, 4, 4,
     {GH, GH, GH, GH, GH, GH, GH, 2, GH, GH, 1, 1, 2, GH, 2, 0, 0, 1, GH, 1},
     10, 5, 4, 8, 4,
     {4, 4, 4, 4, 4, 5, 5, 0, 5, 5, 0, 0, 0, 3, 0, 0, 0, 0, 2, 0},
     {8, 8, 8, 8, 8, 5, 5, 0, 5, 5, 0, 0, 0, 6, 0, 0, 0, 0, 4, 0}},
    {"gold_b", 5, 4, 3,
     {GH, 1, GH, GH, GH, GH, GH, GH, GH, GH, 1, GH, GH, GH, GH, GH, GH, GH, GH, GH},
     180, 140, 1, 153, 139,
     {138, 0, 132, 133, 137, 121, 128, 130, 139, 136,
      0, 133, 135, 134, 131, 140, 133, 136, 136, 139},
     {150, 0, 150, 153, 153, 135, 135, 150, 153, 153,
      0, 150, 153, 153, 153, 150, 150, 153, 153, 153}},
};

#undef GH

#define GOLD_COUNT (sizeof(GOLD) / sizeof(GOLD[0]))

static size_t gold_obs(const gold_fixture *g, void *obs) {
    size_t len = ms_obs_size(g->width, g->height);
    CHECK(len > 0 && g->width * g->height <= 20u);
    CHECK_STATUS(ms_obs_init(obs, len, g->width, g->height, g->mines), MS_OK);
    for (uint32_t c = 0; c < g->width * g->height; c++) {
        if (g->clue[c] != MS_CLUE_HIDDEN) CHECK_STATUS(ms_obs_set_clue(obs, c, g->clue[c]), MS_OK);
    }
    CHECK_STATUS(ms_obs_validate(obs, len), MS_OK);
    return len;
}

/* An EXACT answer equals the reference: optimal value, an optimal cell, the
 * exact survival and every count. */
static void check_exact(const ms_plan_result *r, const ref_root *root, const char *name) {
    if (r->status != MS_PLAN_EXACT || r->exact_wins != root->best) {
        test_print("    ");
        test_print(name);
        print_u32(": status ", r->status);
        print_u32(" reason ", r->reason);
        print_u32(" wins ", r->exact_wins);
        print_u32(" expected ", root->best);
        print_u32(" cell ", r->cell);
        test_print("\n");
    }
    CHECK_EQ(r->status, MS_PLAN_EXACT);
    CHECK_EQ(r->reason, MS_PLAN_REASON_NONE);
    CHECK_EQ(r->exact_total, ref.count);
    CHECK_EQ(r->layouts, ref.count);
    CHECK_EQ(r->exact_wins, root->best);
    CHECK(r->cell < ref.cells);
    CHECK(!(ref.revealed & bit(r->cell)));
    CHECK_EQ(root->value[r->cell], root->best);
    CHECK_EQ(r->candidates, root->candidates);
    CHECK_EQ(f64_bits(r->survival_probability),
             f64_bits((double)root->safe[r->cell] / (double)ref.count));
    CHECK_EQ(f64_bits(r->win_probability), f64_bits((double)root->best / (double)ref.count));
    CHECK_EQ(r->trials, 0);
    CHECK_EQ(r->incomplete, 0);
    CHECK_EQ(r->posterior_exact, 1);
    CHECK(r->search_nodes >= 1);
    CHECK(r->standard_error == 0.0);
    /* Ties go to the safest optimal cell. */
    for (uint32_t c = 0; c < ref.cells; c++) {
        if (!(ref.revealed & bit(c)) && root->value[c] == root->best) {
            CHECK(root->safe[c] <= root->safe[r->cell]);
        }
    }
}

/* ---------------------------------------------------------------- tests */

static void test_limits(void) {
    ms_plan_limits limits;
    base_memset(&limits, 0xA5, sizeof(limits));
    ms_plan_limits_default(&limits);
    CHECK_EQ(limits.magic, MS_MAGIC_PLAN_LIMITS);
    CHECK_EQ(limits.version, MS_PLANNER_VERSION);
    CHECK_EQ(limits.exact_layout_limit, 256);
    CHECK_EQ(limits.exact_node_limit, 100000);
    CHECK_EQ(limits.sample_count, 96);
    CHECK_EQ(limits.candidate_limit, 8);
    CHECK_EQ(limits.min_rollouts, 16);
    CHECK_EQ(limits.rollout_step_limit, MS_SOLVER_MAX_CELLS);
    CHECK_EQ(limits.flags, 0);
    CHECK(limits.time_budget_ms == 3000.0);
    CHECK_EQ(limits.memory_budget_bytes, (uint64_t)256u << 20);
    CHECK_EQ(limits.seed, 0);
    CHECK_EQ(limits.reserved, 0);
    CHECK_STATUS(ms_plan_limits_validate(&limits), MS_OK);
    ms_plan_limits_default(NULL); /* no-op */

    CHECK_STATUS(ms_plan_limits_validate(NULL), MS_ERR_INVALID_BUFFER);
    uint64_t raw[16];
    base_memcpy((uint8_t *)raw + 4, &limits, sizeof(limits));
    CHECK_STATUS(ms_plan_limits_validate((const ms_plan_limits *)((uint8_t *)raw + 4)),
                 MS_ERR_INVALID_BUFFER);
    ms_plan_limits bad;
    base_memcpy(&bad, &limits, sizeof(bad));
    bad.magic = MS_MAGIC_LIMITS;
    CHECK_STATUS(ms_plan_limits_validate(&bad), MS_ERR_INVALID_BUFFER);
    base_memcpy(&bad, &limits, sizeof(bad));
    bad.version = 2;
    CHECK_STATUS(ms_plan_limits_validate(&bad), MS_ERR_INVALID_BUFFER);
    base_memcpy(&bad, &limits, sizeof(bad));
    bad.flags = 2;
    CHECK_STATUS(ms_plan_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    base_memcpy(&bad, &limits, sizeof(bad));
    bad.time_budget_ms = -1.0;
    CHECK_STATUS(ms_plan_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    bad.time_budget_ms = (double)NAN;
    CHECK_STATUS(ms_plan_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    base_memcpy(&bad, &limits, sizeof(bad));
    bad.reserved = 1;
    CHECK_STATUS(ms_plan_limits_validate(&bad), MS_ERR_INVALID_LIMITS);
    /* Zero and extreme budgets are valid. */
    base_memcpy(&bad, &limits, sizeof(bad));
    bad.exact_layout_limit = bad.exact_node_limit = bad.sample_count = 0;
    bad.candidate_limit = bad.rollout_step_limit = bad.min_rollouts = 0;
    bad.time_budget_ms = 0.0;
    bad.memory_budget_bytes = 0;
    CHECK_STATUS(ms_plan_limits_validate(&bad), MS_OK);
    bad.time_budget_ms = HUGE_VAL;
    bad.flags = MS_PLAN_EXPLICIT_SEED;
    bad.sample_count = UINT32_MAX;
    bad.memory_budget_bytes = UINT64_MAX;
    CHECK_STATUS(ms_plan_limits_validate(&bad), MS_OK);
}

static void test_arguments(void) {
    plan_env env;
    env_open(&env, 1.0);
    const fixture *f = find_fixture("fifty");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    static uint64_t result_words[32];
    ms_plan_result *result = (ms_plan_result *)result_words;
    base_memset(result_words, 0x5A, sizeof(result_words));
    uint64_t sentinel[32];
    base_memcpy(sentinel, result_words, sizeof(sentinel));
    const size_t rlen = sizeof(ms_plan_result);
#define REJECT(call, expected)                                                             \
    do {                                                                                   \
        uint32_t reads = env.fake.reads;                                                   \
        uint64_t requests = env.mem.requests;                                              \
        CHECK_STATUS((call), (expected));                                                  \
        CHECK_EQ(env.fake.reads, reads);                                                   \
        CHECK_EQ(env.mem.requests, requests);                                              \
        CHECK(base_memcmp(result_words, sentinel, sizeof(sentinel)) == 0);                 \
    } while (0)
    REJECT(ms_plan(NULL, len, &env.limits, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, NULL, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, NULL, &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, NULL, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock, NULL, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock,
                   (ms_plan_result *)((uint8_t *)result + 4), rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan((uint8_t *)obs_buf + 4, len, &env.limits, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock, result, rlen - 8),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock, result, rlen + 8),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, 0, &env.limits, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len + 8, &env.limits, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    /* Wrapping ranges are refused on addresses alone. */
    REJECT(ms_plan((const void *)(MS_UINTPTR_MAX - 15u), 64, &env.limits, &env.mem, &env.clock,
                   result, rlen),
           MS_ERR_INVALID_BUFFER);
    /* Aliasing: the result may not overlap any input or the workspace and
     * clock structs; inputs may not overlap each other either. */
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock, (ms_plan_result *)obs_buf,
                   rlen),
           MS_ERR_INVALID_BUFFER);
    static uint64_t shared[64];
    base_memcpy(shared, obs_buf, len);
    REJECT(ms_plan(shared, len, (const ms_plan_limits *)((uint8_t *)shared + 8), &env.mem,
                   &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock,
                   (ms_plan_result *)(void *)&env.limits, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock,
                   (ms_plan_result *)(void *)&env.mem, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock,
                   (ms_plan_result *)(void *)&env.clock, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan((const void *)&env.mem, sizeof(env.mem), &env.limits, &env.mem, &env.clock,
                   result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, (const ms_plan_limits *)(void *)&env.clock, &env.mem,
                   &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan((const void *)&env.clock, sizeof(env.clock), &env.limits, &env.mem,
                   &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    REJECT(ms_plan(obs_buf, len, (const ms_plan_limits *)(void *)&env.mem, &env.mem, &env.clock,
                   result, rlen),
           MS_ERR_INVALID_BUFFER);
    /* Workspace and clock structs overlapping each other: refused on
     * addresses, neither read nor written. */
    static uint64_t blob[(sizeof(rt_mem) + sizeof(rt_clock)) / 8u + 2u];
    static uint64_t blob_copy[sizeof(blob) / 8u];
    base_memset(blob, 0x3C, sizeof(blob));
    base_memcpy(blob_copy, blob, sizeof(blob));
    CHECK_STATUS(ms_plan(obs_buf, len, &env.limits, (rt_mem *)(void *)blob,
                         (rt_clock *)(void *)((uint8_t *)blob + 8u), result, rlen),
                 MS_ERR_INVALID_BUFFER);
    CHECK(base_memcmp(blob, blob_copy, sizeof(blob)) == 0);
    CHECK(base_memcmp(result_words, sentinel, sizeof(sentinel)) == 0);
    /* Limits header, then the observation, then the limits values. */
    ms_plan_limits saved;
    base_memcpy(&saved, &env.limits, sizeof(saved));
    env.limits.magic = 0;
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    base_memcpy(&env.limits, &saved, sizeof(saved));
    env.limits.time_budget_ms = -5.0;
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_LIMITS);
    base_memcpy(&env.limits, &saved, sizeof(saved));
    ms_obs_header *h = (ms_obs_header *)obs_buf;
    h->revealed++;
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_OBSERVATION);
    h->revealed--;
    h->magic = MS_MAGIC_VIEW;
    REJECT(ms_plan(obs_buf, len, &env.limits, &env.mem, &env.clock, result, rlen),
           MS_ERR_INVALID_BUFFER);
    h->magic = MS_MAGIC_OBSERVATION;
#undef REJECT
    /* Inconsistent observations are errors, like ms_solve's. */
    CHECK_STATUS(ms_obs_set_clue(obs_buf, 0, 8), MS_OK);
    CHECK_STATUS(plan(&env, obs_buf, len, result), MS_ERR_INCONSISTENT);
    board_rows(&board_a, 3, 3, "ooo" "o.o" "ooo");
    board_a.mines = 2; /* more mines than hidden cells */
    len = board_obs(&board_a, obs_buf);
    CHECK_STATUS(plan(&env, obs_buf, len, result), MS_ERR_INCONSISTENT);
    env_close(&env);
}

static void test_result_validate(void) {
    plan_env env;
    env_open(&env, 0.0);
    const fixture *f = find_fixture("flood_b");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    ms_plan_result good, r;
    CHECK_STATUS(plan(&env, obs_buf, len, &good), MS_OK);
    CHECK_EQ(good.status, MS_PLAN_EXACT);
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &good, sizeof(good)), MS_OK);
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, NULL, sizeof(good)), MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &good, sizeof(good) - 8),
                 MS_ERR_INVALID_BUFFER);
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len - 8, &good, sizeof(good)),
                 MS_ERR_INVALID_BUFFER);
#define BREAKS(field, value, expected)                                                     \
    do {                                                                                   \
        base_memcpy(&r, &good, sizeof(r));                                                 \
        r.field = value;                                                                   \
        CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), expected);      \
    } while (0)
    BREAKS(magic, MS_MAGIC_RESULT, MS_ERR_INVALID_BUFFER);
    BREAKS(version, 2, MS_ERR_INVALID_BUFFER);
    BREAKS(status, 4, MS_ERR_INVALID_RESULT);
    BREAKS(reason, MS_PLAN_REASON_BUDGET, MS_ERR_INVALID_RESULT);
    BREAKS(width, 8, MS_ERR_INVALID_RESULT);
    BREAKS(total_mines, 4, MS_ERR_INVALID_RESULT);
    BREAKS(revealed, 3, MS_ERR_INVALID_RESULT);
    BREAKS(observation_hash, good.observation_hash ^ 1u, MS_ERR_INVALID_RESULT);
    BREAKS(reserved, 1, MS_ERR_INVALID_RESULT);
    BREAKS(cell, 0, MS_ERR_INVALID_RESULT); /* a revealed cell */
    BREAKS(cell, MS_PLAN_NO_CELL, MS_ERR_INVALID_RESULT);
    BREAKS(exact_wins, good.exact_total, MS_ERR_INVALID_RESULT);
    BREAKS(exact_wins, 0, MS_ERR_INVALID_RESULT);
    BREAKS(exact_total, good.exact_total + 1u, MS_ERR_INVALID_RESULT);
    BREAKS(win_probability, 0.75, MS_ERR_INVALID_RESULT);
    BREAKS(survival_probability, 1.0, MS_ERR_INVALID_RESULT);
    BREAKS(survival_probability, 0.5, MS_ERR_INVALID_RESULT); /* below the win rate */
    BREAKS(standard_error, 0.01, MS_ERR_INVALID_RESULT);
    BREAKS(trials, 1, MS_ERR_INVALID_RESULT);
    BREAKS(posterior_exact, 0, MS_ERR_INVALID_RESULT);
    BREAKS(search_nodes, 0, MS_ERR_INVALID_RESULT);
    BREAKS(elapsed_ms, -1.0, MS_ERR_INVALID_RESULT);
    BREAKS(elapsed_ms, (double)NAN, MS_ERR_INVALID_RESULT);
    BREAKS(candidates, 0, MS_ERR_INVALID_RESULT);
    BREAKS(candidates, good.width * good.height - good.revealed + 1u, MS_ERR_INVALID_RESULT);
    BREAKS(incomplete, 1, MS_ERR_INVALID_RESULT); /* > trials */
    BREAKS(posterior_exact, 2, MS_ERR_INVALID_RESULT);
    /* Every hidden cell safe in some layout is an EXACT candidate: fewer
     * than total_mines cells are mines in all of >= 2 distinct layouts
     * (here 10 hidden cells, 5 mines: at least 6 candidates). */
    uint32_t hidden = good.width * good.height - good.revealed;
    CHECK_EQ(hidden, 10);
    CHECK_EQ(good.total_mines, 5);
    CHECK(good.candidates >= 6u && good.candidates <= hidden);
    BREAKS(candidates, hidden, MS_OK);
    BREAKS(candidates, 6, MS_OK);
    BREAKS(candidates, 5, MS_ERR_INVALID_RESULT);
    BREAKS(candidates, 1, MS_ERR_INVALID_RESULT);
    /* Exact survival is a count of safe layouts over the total. */
    double total = (double)good.exact_total;
    double safe = base_round(good.survival_probability * total);
    CHECK(safe / total == good.survival_probability);
    CHECK(safe + 1.0 < total);
    BREAKS(survival_probability, (safe + 1.0) / total, MS_OK);
    BREAKS(survival_probability, (safe + 0.5) / total, MS_ERR_INVALID_RESULT);
    BREAKS(survival_probability, good.survival_probability + 1e-12, MS_ERR_INVALID_RESULT);
    /* An ESTIMATED answer: wins / completed exactly, never exact counts. */
    env.limits.exact_layout_limit = 0;
    env.limits.sample_count = 40;
    CHECK_STATUS(plan(&env, obs_buf, len, &good), MS_OK);
    CHECK_EQ(good.status, MS_PLAN_ESTIMATED);
    BREAKS(exact_total, 10, MS_ERR_INVALID_RESULT);
    BREAKS(exact_wins, 1, MS_ERR_INVALID_RESULT);
    BREAKS(win_probability, good.win_probability + 0.001, MS_ERR_INVALID_RESULT);
    BREAKS(win_probability, 1.5, MS_ERR_INVALID_RESULT);
    BREAKS(incomplete, good.trials, MS_ERR_INVALID_RESULT);
    BREAKS(incomplete, 2, MS_ERR_INVALID_RESULT); /* rounds stop at the first */
    BREAKS(trials, good.layouts + 1u, MS_ERR_INVALID_RESULT);
    BREAKS(survival_probability, 0.0, MS_ERR_INVALID_RESULT);
    BREAKS(standard_error, 0.75, MS_ERR_INVALID_RESULT);
    BREAKS(reason, MS_PLAN_REASON_NO_SAMPLES, MS_ERR_INVALID_RESULT);
    BREAKS(candidates, 0, MS_ERR_INVALID_RESULT); /* rounds ran */
    /* Estimates are never certain. */
    BREAKS(win_probability, 0.0, MS_ERR_INVALID_RESULT);
    BREAKS(survival_probability, 1.0, MS_ERR_INVALID_RESULT);
    CHECK(good.standard_error > 0.0);
    BREAKS(win_probability, 1.0, MS_OK); /* every round won, with an error */
    base_memcpy(&r, &good, sizeof(r));
    r.win_probability = 1.0;
    r.standard_error = 0.0;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    BREAKS(posterior_exact, 2, MS_ERR_INVALID_RESULT);
    BREAKS(candidates, hidden + 1u, MS_ERR_INVALID_RESULT);
    BREAKS(candidates, hidden, MS_OK);
    /* Search nodes only beside a complete listing from the exact posterior
     * (rollouts may follow an interrupted search over it). */
    CHECK_EQ(good.search_nodes, 0);
    CHECK_EQ(good.posterior_exact, 1); /* draws from the exact posterior */
    BREAKS(search_nodes, 5, MS_OK);
    base_memcpy(&r, &good, sizeof(r));
    r.posterior_exact = 0; /* importance-weighted draws */
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_OK);
    r.search_nodes = 5;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    /* NONE and UNAVAILABLE carry no estimate. */
    base_memcpy(&r, &good, sizeof(r));
    r.status = MS_PLAN_UNAVAILABLE;
    r.reason = MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    r.cell = MS_PLAN_NO_CELL;
    r.survival_probability = r.win_probability = r.standard_error = 0.0;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_OK);
    r.candidates = hidden + 1u;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    r.candidates = hidden;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_OK);
    r.candidates = good.candidates;
    /* Reason-specific counts: no layouts behind NO_SAMPLES and
     * POSTERIOR_UNAVAILABLE, some behind INSUFFICIENT_ROLLOUTS. */
    ms_plan_result u;
    base_memcpy(&u, &r, sizeof(u));
    u.reason = MS_PLAN_REASON_NO_SAMPLES;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.layouts = u.trials = u.incomplete = u.candidates = u.search_nodes = 0;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.posterior_exact = 0; /* no layouts, so not "from the exact posterior" */
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_OK);
    u.reason = MS_PLAN_REASON_POSTERIOR_UNAVAILABLE;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_OK);
    u.search_nodes = 1;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.search_nodes = 0;
    u.reason = MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.reason = MS_PLAN_REASON_BUDGET;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_OK);
    /* Candidates come from a posterior, search nodes from a complete
     * listing of >= 2 layouts from the exact posterior. */
    u.candidates = 1;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.candidates = 0;
    u.search_nodes = 1;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.layouts = 1;
    u.posterior_exact = 1;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.layouts = 2;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_OK);
    u.posterior_exact = 0;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.search_nodes = 0;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_OK);
    u.candidates = hidden + 1u;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.candidates = hidden;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_OK);
    u.candidates = u.layouts = 0;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_OK);
    u.reason = MS_PLAN_REASON_CERTAIN_MOVES;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    u.reason = 8;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &u, sizeof(u)), MS_ERR_INVALID_RESULT);
    r.reason = MS_PLAN_REASON_NOT_STARTED;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    r.status = MS_PLAN_NONE;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    r.reason = MS_PLAN_REASON_CERTAIN_MOVES;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    r.candidates = r.layouts = r.trials = r.incomplete = r.search_nodes = 0;
    r.posterior_exact = 0;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_OK);
    r.reason = MS_PLAN_REASON_FINISHED; /* hidden cells are not all mines here */
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    r.reason = MS_PLAN_REASON_CERTAIN_MOVES;
    r.elapsed_ms = 12.5; /* any finite elapsed time */
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_OK);
    r.cell = 4;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
    r.cell = MS_PLAN_NO_CELL;
    r.survival_probability = 0.5;
    CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_ERR_INVALID_RESULT);
#undef BREAKS
    env_close(&env);
}

static void test_placeholders(void) {
    plan_env env;
    env_open(&env, 1.0);
    ms_plan_result r;
    /* Nothing revealed yet. */
    size_t len = ms_obs_size(9, 9);
    CHECK_STATUS(ms_obs_init(obs_buf, len, 9, 9, 10), MS_OK);
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_NONE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_NOT_STARTED);
    CHECK_EQ(r.cell, MS_PLAN_NO_CELL);
    CHECK_EQ(r.width, 9);
    CHECK_EQ(r.total_mines, 10);
    CHECK_EQ(r.observation_hash, ms_obs_hash(obs_buf));
    /* Only mines are hidden: finished. */
    board_rows(&board_a, 4, 3, "o*oo" "oooo" "oo*o");
    len = board_obs(&board_a, obs_buf);
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_NONE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_FINISHED);
    /* ... with a wrong clue it is inconsistent instead. */
    CHECK_STATUS(ms_obs_set_clue(obs_buf, 0, 0), MS_OK);
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_ERR_INCONSISTENT);
    /* A proven-safe cell waits: no guess is needed, none is advised. */
    board_rows(&board_a, 4, 3, "o.oo" "oooo" "oo*o");
    len = board_obs(&board_a, obs_buf);
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_NONE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_CERTAIN_MOVES);
    CHECK_EQ(r.cell, MS_PLAN_NO_CELL);
    CHECK(r.win_probability == 0.0);
    /* Even with every planning budget at zero. */
    env.limits.time_budget_ms = 0.0;
    env.limits.exact_layout_limit = env.limits.sample_count = 0;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.reason, MS_PLAN_REASON_CERTAIN_MOVES);
    /* Hidden cells next to revealed zeros are proven safe as well. */
    ms_plan_limits_default(&env.limits);
    board_rows(&board_a, 5, 5, "ooooo" "ooooo" "ooooo" "ooo.." "ooo.*");
    len = board_obs(&board_a, obs_buf);
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_NONE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_CERTAIN_MOVES);
    env_close(&env);
}

static void test_exact_fixtures(void) {
    plan_env env;
    env_open(&env, 0.0);
    ms_plan_result r;
    for (uint32_t i = 0; i < FIXTURE_COUNT; i++) {
        const fixture *f = &FIXTURES[i];
        size_t len = fixture_obs(f, &board_a, obs_buf);
        CHECK(ref_load(&board_a));
        CHECK_EQ(ref.count, f->layouts);
        ref_root root;
        CHECK(ref_solve(&root));
        CHECK(!root.certain);
        CHECK_EQ(root.best, f->best);
        uint32_t top = 0;
        for (uint32_t c = 0; c < ref.cells; c++) top = root.safe[c] > top ? root.safe[c] : top;
        CHECK_EQ(top, f->top_safe);
        CHECK(root.best < ref.count); /* clairvoyance would win every layout */
        CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
        check_exact(&r, &root, f->name);
        if (base_strncmp(f->name, "notsafest", 9) == 0) {
            /* The advice is not the safest cell, and no safest cell is optimal. */
            CHECK(root.safe[r.cell] < f->top_safe);
            for (uint32_t c = 0; c < ref.cells; c++) {
                if (root.safe[c] == f->top_safe) CHECK(root.value[c] < root.best);
            }
        }
        /* Deterministic: the same call gives the same bytes. */
        ms_plan_result again;
        CHECK_STATUS(plan(&env, obs_buf, len, &again), MS_OK);
        CHECK(base_memcmp(&r, &again, sizeof(r)) == 0);
    }
    /* Equal odds, different geometry: the planner tells them apart. */
    const fixture *g = find_fixture("geometry_a");
    size_t len = fixture_obs(g, &board_a, obs_buf);
    CHECK(ref_load(&board_a));
    ref_root root;
    CHECK(ref_solve(&root));
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.cell, 4);
    uint32_t peers = 0;
    for (uint32_t c = 0; c < ref.cells; c++) {
        if (c != r.cell && root.safe[c] == root.safe[r.cell] && !(ref.revealed & bit(c))) {
            CHECK(root.value[c] < root.best);
            peers++;
        }
    }
    CHECK(peers >= 3);
    env_close(&env);
}

/* Random small positions: the planner's EXACT answers equal the reference. */
static void test_exact_random(void) {
    plan_env env;
    env_open(&env, 0.0);
    rt_rng rng;
    rt_rng_seed(&rng, 0x706C616E6E6572ull);
    static const uint32_t dims[][2] = {{4, 4}, {5, 4}, {4, 5}, {5, 5}, {6, 3}, {7, 3}, {8, 2},
                                       {6, 4}, {3, 7}, {9, 2}};
    uint32_t checked = 0, exact = 0, certain = 0, attempts = 0;
    while (checked < 120u && attempts < 20000u) {
        attempts++;
        uint32_t d = rt_rng_below(&rng, (uint32_t)(sizeof(dims) / sizeof(dims[0])));
        uint32_t w = dims[d][0], h = dims[d][1];
        uint32_t mines = 2u + rt_rng_below(&rng, 6u);
        if (mines + 9u > w * h) continue;
        board_random(&board_a, &rng, w, h, mines, rt_rng_below(&rng, 3u));
        if (attempts % 4u != 0) board_close(&board_a, HUGE_VAL); /* mostly guesses */
        uint32_t hidden = board_hidden(&board_a);
        if (hidden == mines || hidden > 20u) continue;
        if (!ref_load(&board_a) || ref.count < 2u) continue;
        ref_root root;
        if (!ref_solve(&root)) continue;
        size_t len = board_obs(&board_a, obs_buf);
        ms_plan_result r;
        CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
        if (root.certain) {
            /* Safe in every layout: advice is to take the certain moves. */
            CHECK_EQ(r.status, MS_PLAN_NONE);
            CHECK_EQ(r.reason, MS_PLAN_REASON_CERTAIN_MOVES);
            certain++;
        } else {
            check_exact(&r, &root, "random");
            exact++;
        }
        checked++;
    }
    print_u32("    positions ", checked);
    print_u32(" exact ", exact);
    print_u32(" certain ", certain);
    test_print("\n");
    CHECK_EQ(checked, 120);
    CHECK(exact >= 60u);
    CHECK(certain >= 10u);
    env_close(&env);
}

static void test_gold_fixtures(void) {
    plan_env env;
    env_open(&env, 0.0);
    for (uint32_t i = 0; i < GOLD_COUNT; i++) {
        const gold_fixture *g = &GOLD[i];
        uint32_t cells = g->width * g->height;
        size_t len = gold_obs(g, obs_buf);
        /* The tables against themselves and the C reference. */
        CHECK(ref_load_obs(obs_buf, REF_LAYOUTS));
        CHECK_EQ(ref.count, g->layouts);
        ref_root root;
        bool searched = ref_solve(&root);
        CHECK_EQ(searched ? 1u : 0u, g->layouts <= REF_SEARCH_LAYOUTS ? 1u : 0u);
        CHECK(!root.certain); /* no forced safe cell: a real guess */
        uint32_t best = 0, optima = 0, top_safe = 0, best_of_safest = 0;
        for (uint32_t c = 0; c < cells; c++) {
            if (g->clue[c] != MS_CLUE_HIDDEN) {
                CHECK_EQ(g->value[c], 0);
                CHECK_EQ(g->safe[c], 0);
                continue;
            }
            CHECK_EQ(root.safe[c], g->safe[c]);
            if (searched) CHECK_EQ(root.value[c], g->value[c]);
            CHECK(g->value[c] >= 1u && g->value[c] <= g->safe[c]);
            if (g->value[c] > best) best = g->value[c];
            if (g->safe[c] > top_safe) top_safe = g->safe[c];
        }
        for (uint32_t c = 0; c < cells; c++) {
            if (g->clue[c] != MS_CLUE_HIDDEN) continue;
            optima += g->value[c] == best;
            if (g->safe[c] == top_safe && g->value[c] > best_of_safest) {
                best_of_safest = g->value[c];
            }
        }
        CHECK_EQ(best, g->best);
        CHECK_EQ(optima, g->optima);
        CHECK_EQ(top_safe, g->top_safe);
        CHECK_EQ(best_of_safest, g->best_of_safest);
        CHECK(best_of_safest < best); /* the safest cells are not optimal */
        if (searched) CHECK_EQ(root.best, best);

        /* The planner finds the optimum exactly. */
        ms_plan_result r;
        CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
        CHECK_EQ(r.status, MS_PLAN_EXACT);
        CHECK_EQ(r.exact_total, g->layouts);
        CHECK_EQ(r.layouts, g->layouts);
        CHECK_EQ(r.exact_wins, g->best);
        CHECK(r.cell < cells && g->clue[r.cell] == MS_CLUE_HIDDEN);
        CHECK_EQ(g->value[r.cell], g->best);
        CHECK(g->safe[r.cell] < g->top_safe);
        CHECK_EQ(f64_bits(r.survival_probability),
                 f64_bits((double)g->safe[r.cell] / (double)g->layouts));
        CHECK_EQ(f64_bits(r.win_probability), f64_bits((double)g->best / (double)g->layouts));
        test_print("    ");
        test_print(g->name);
        print_u32(": cell ", r.cell);
        print_u32(" wins ", r.exact_wins);
        print_u32("/", r.exact_total);
        print_u32(" nodes ", r.search_nodes);
        test_print("\n");

        /* Rollouts over the complete listing: each candidate's continuation
         * policy sees public clues only, so it wins at most the optimum. */
        env.limits.exact_node_limit = 0;
        ms_plan_test_stats st;
        CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
        CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
        CHECK_EQ(st.rounds, g->layouts);
        CHECK(r.standard_error == 0.0);
        for (uint32_t k = 0; k < st.candidates; k++) {
            CHECK(st.wins[k] <= g->value[st.cell[k]]);
        }
        ms_plan_limits_default(&env.limits);
    }
    /* The named optima. */
    const gold_fixture *a = &GOLD[0];
    CHECK(a->value[5] == 5u && a->value[6] == 5u && a->value[8] == 5u && a->value[9] == 5u);
    for (uint32_t c = 0; c < 5u; c++) CHECK(a->safe[c] == 8u && a->value[c] == 4u);
    CHECK(a->value[13] == 3u && a->safe[13] == 6u && a->value[18] == 2u && a->safe[18] == 4u);
    const gold_fixture *b = &GOLD[1];
    CHECK(b->value[15] == 140u && b->safe[15] == 150u);
    size_t len = gold_obs(b, obs_buf);
    ms_plan_result r;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.cell, 15); /* uniquely optimal */
    env_close(&env);
}

/* ---------------------------------------------------------------- rollouts */

/* A layout of the reference as one 0/1 byte per cell. */
static void ref_layout_bytes(uint32_t l, uint8_t *out) {
    for (uint32_t c = 0; c < ref.cells; c++) out[c] = (ref.layout[l] & bit(c)) ? 1u : 0u;
}

static uint8_t layout_bytes[MAX_CELLS];

/* The continuation policy's exact number of wins over every compatible
 * layout after first revealing `cell` (the test hook plays each game). */
static uint32_t policy_wins(plan_env *env, const void *obs, size_t len, uint32_t cell) {
    uint32_t wins = 0, trace_len;
    for (uint32_t l = 0; l < ref.count; l++) {
        ref_layout_bytes(l, layout_bytes);
        uint32_t outcome = 9;
        CHECK_STATUS(ms_plan_test_playout(obs, len, &env->limits, &env->mem, &env->clock,
                                          layout_bytes, cell, &outcome, NULL, 0, &trace_len),
                     MS_OK);
        CHECK(outcome == 0 || outcome == 1);
        wins += outcome;
    }
    return wins;
}

/* Exact mode off but every layout listed: one paired round per layout, so
 * each candidate's simulated wins must equal its policy's exact win count. */
static void test_rollout_exhaustive_rounds(void) {
    plan_env env;
    env_open(&env, 0.0);
    static const char *const names[] = {"fifty", "flood_b", "notsafest_b", "geometry_b"};
    for (uint32_t i = 0; i < 4u; i++) {
        const fixture *f = find_fixture(names[i]);
        size_t len = fixture_obs(f, &board_a, obs_buf);
        CHECK(ref_load(&board_a));
        ref_root root;
        CHECK(ref_solve(&root));
        env.limits.exact_node_limit = 0;
        env.limits.min_rollouts = 2;
        ms_plan_result r;
        ms_plan_test_stats st;
        CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
        CHECK_EQ(r.status, MS_PLAN_ESTIMATED); /* never EXACT without a completed search */
        CHECK_EQ(r.exact_total, 0);
        CHECK_EQ(r.exact_wins, 0);
        CHECK_EQ(r.search_nodes, 0);
        CHECK_EQ(r.layouts, ref.count);
        CHECK_EQ(r.trials, ref.count);
        CHECK_EQ(r.incomplete, 0);
        CHECK_EQ(r.posterior_exact, 1);
        CHECK_EQ(st.rounds, ref.count);
        CHECK_EQ(r.candidates, st.candidates);
        CHECK(st.candidates >= 1u && st.candidates <= env.limits.candidate_limit);
        uint32_t best = 0;
        for (uint32_t k = 0; k < st.candidates; k++) {
            CHECK_EQ(st.played[k], ref.count);
            CHECK_EQ(st.wins[k], policy_wins(&env, obs_buf, len, st.cell[k]));
            CHECK(st.wins[k] <= root.value[st.cell[k]]); /* public clues only */
            /* Every shortlisted cell is hidden and safe in some layout. */
            uint32_t safe = 0;
            for (uint32_t l = 0; l < ref.count; l++) safe += !(ref.layout[l] & bit(st.cell[k]));
            CHECK(safe > 0);
            CHECK(st.wins[k] <= safe);
            CHECK_EQ(f64_bits(st.survival[k]), f64_bits((double)safe / (double)ref.count));
            if (st.wins[k] > best) best = st.wins[k];
        }
        CHECK_EQ(f64_bits(r.win_probability), f64_bits((double)best / (double)ref.count));
        CHECK(r.standard_error == 0.0);
        bool advised = false;
        for (uint32_t k = 0; k < st.candidates; k++) {
            advised = advised || (st.cell[k] == r.cell && st.wins[k] == best);
        }
        CHECK(advised);
        ms_plan_limits_default(&env.limits);
    }
    /* A pure 50-50: whichever cell is safe wins at once, so the two
     * candidates' wins split the rounds exactly. */
    const fixture *f = find_fixture("fifty");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    env.limits.exact_layout_limit = 0; /* draws, not a listing */
    env.limits.sample_count = 64;
    ms_plan_result r;
    ms_plan_test_stats st;
    CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
    CHECK_EQ(r.layouts, 64);
    CHECK_EQ(r.trials, 64);
    CHECK_EQ(st.candidates, 2);
    CHECK_EQ(st.wins[0] + st.wins[1], 64);
    CHECK(st.wins[0] > 0 && st.wins[1] > 0);
    CHECK(r.survival_probability == 0.5);
    env_close(&env);
}

/* Sampled rounds estimate the policy's win rate within sampling error. */
static void test_rollout_statistics(void) {
    plan_env env;
    env_open(&env, 0.0);
    static const char *const names[] = {"flood_b", "notsafest_b", "notsafest_c"};
    for (uint32_t i = 0; i < 3u; i++) {
        const fixture *f = find_fixture(names[i]);
        size_t len = fixture_obs(f, &board_a, obs_buf);
        CHECK(ref_load(&board_a));
        env.limits.exact_layout_limit = 0;
        env.limits.sample_count = 400;
        env.limits.flags = MS_PLAN_EXPLICIT_SEED;
        env.limits.seed = 0x5EED0000u + i;
        ms_plan_result r;
        ms_plan_test_stats st;
        CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
        CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
        CHECK_EQ(st.rounds, 400);
        CHECK_EQ(r.posterior_exact, 1);
        for (uint32_t k = 0; k < st.candidates; k++) {
            double truth = (double)policy_wins(&env, obs_buf, len, st.cell[k]) / ref.count;
            double est = (double)st.wins[k] / (double)st.rounds;
            double sd = truth * (1.0 - truth) / (double)st.rounds;
            double tol = 4.5 * (sd > 0.0 ? ref_sqrt(sd) : 0.0) + 1e-12;
            if (!(est - truth <= tol && truth - est <= tol)) {
                test_print("    ");
                test_print(f->name);
                print_u32(" cell ", st.cell[k]);
                test_print(" estimate ");
                test_print_double(est);
                test_print(" truth ");
                test_print_double(truth);
                test_print("\n");
            }
            CHECK_NEAR(est, truth, tol);
        }
        /* Paired bookkeeping and the decision rule: the safest candidate
         * (first) unless another's paired advantage clears one standard
         * deviation; the most clearly better one wins. */
        uint32_t advised = 0;
        double top = 0.0;
        for (uint32_t k = 0; k < st.candidates; k++) {
            CHECK_EQ((int64_t)st.wins[k] - (int64_t)st.wins[0],
                     (int64_t)st.gain[k] - (int64_t)st.loss[k]);
            if (k == 0) {
                CHECK_EQ(st.gain[0] + st.loss[0], 0);
                continue;
            }
            double margin = (double)st.gain[k] - (double)st.loss[k] -
                            ref_sqrt((double)st.gain[k] + (double)st.loss[k]);
            if (margin > top + 1e-9) {
                top = margin;
                advised = k;
            }
        }
        CHECK_EQ(r.cell, st.cell[advised]);
        CHECK_EQ(f64_bits(r.win_probability),
                 f64_bits((double)st.wins[advised] / (double)st.rounds));
        CHECK_EQ(f64_bits(r.survival_probability), f64_bits(st.survival[advised]));
        CHECK_NEAR(r.standard_error, jeffreys_se(st.wins[advised], st.rounds, 1.0), 1e-12);
        /* The same seed reproduces the same answer. */
        ms_plan_result again;
        CHECK_STATUS(plan(&env, obs_buf, len, &again), MS_OK);
        CHECK(base_memcmp(&r, &again, sizeof(r)) == 0);
        ms_plan_limits_default(&env.limits);
    }
    env_close(&env);
}

/* Replays `trace` on layout l of the reference; returns the first step after
 * which the public observation differs from layout m's (or the length). */
static uint32_t divergence(const uint32_t *trace, uint32_t len, uint32_t l, uint32_t m) {
    uint64_t r = ref.revealed;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t c = trace[i];
        bool bl = (ref.layout[l] & bit(c)) != 0, bm = (ref.layout[m] & bit(c)) != 0;
        if (bl != bm) return i;
        if (bl) return len;
        if (r & bit(c)) return i; /* revealing a revealed cell would be a bug */
        uint64_t rl = ref_flood(r, c, l), rm = ref_flood(r, c, m);
        if (rl != rm) return i;
        for (uint64_t fresh = rl & ~r; fresh; fresh &= fresh - 1u) {
            uint32_t x = lowest(fresh);
            if (ref.near[l][x] != ref.near[m][x]) return i;
        }
        r = rl;
    }
    return len;
}

/* Rollout decisions see only public observations: games on two boards are
 * identical until the boards first show something different. */
static void test_no_clairvoyance(void) {
    plan_env env;
    env_open(&env, 0.0);
    static uint32_t traces[REF_SEARCH_LAYOUTS][REF_CELLS];
    uint32_t lens[REF_SEARCH_LAYOUTS], outcomes[REF_SEARCH_LAYOUTS];
    static const char *const names[] = {"flood_c", "geometry_a", "notsafest_a"};
    uint32_t compared = 0, shared_steps = 0, decided = 0;
    for (uint32_t i = 0; i < 3u; i++) {
        const fixture *f = find_fixture(names[i]);
        size_t len = fixture_obs(f, &board_a, obs_buf);
        CHECK(ref_load(&board_a));
        for (uint32_t first = 0; first < ref.cells; first++) {
            if (ref.revealed & bit(first)) continue;
            uint32_t safe = 0;
            for (uint32_t l = 0; l < ref.count; l++) safe += !(ref.layout[l] & bit(first));
            if (safe < 2u) continue;
            for (uint32_t l = 0; l < ref.count; l++) {
                ref_layout_bytes(l, layout_bytes);
                CHECK_STATUS(ms_plan_test_playout(obs_buf, len, &env.limits, &env.mem,
                                                  &env.clock, layout_bytes, first, &outcomes[l],
                                                  traces[l], REF_CELLS, &lens[l]),
                             MS_OK);
                CHECK(outcomes[l] <= 1u);
                CHECK(lens[l] >= 1u && traces[l][0] == first);
                /* A loss ends on a mine; a win reveals every safe cell. */
                bool boom = (ref.layout[l] & bit(traces[l][lens[l] - 1u])) != 0;
                CHECK_EQ(boom ? 0u : 1u, outcomes[l]);
            }
            for (uint32_t l = 0; l < ref.count; l++) {
                for (uint32_t m = l + 1u; m < ref.count; m++) {
                    uint32_t d = divergence(traces[l], lens[l], l, m);
                    uint32_t shared = d < lens[l] ? d + 1u : lens[l];
                    CHECK(lens[m] >= shared);
                    for (uint32_t s = 0; s < shared; s++) CHECK_EQ(traces[m][s], traces[l][s]);
                    if (d == lens[l]) CHECK_EQ(lens[m], lens[l]);
                    compared++;
                    shared_steps += shared;
                    if (shared >= 2u) decided++;
                }
            }
        }
    }
    print_u32("    pairs ", compared);
    print_u32(" shared steps ", shared_steps);
    print_u32(" pairs with a shared decision ", decided);
    test_print("\n");
    CHECK(compared > 10000u);
    /* Thousands of pairs share policy decisions made on identical
     * observations of different boards. */
    CHECK(decided > 2000u);
    env_close(&env);
}

/* Rounds over a complete listing (exact search off): a seeded random
 * permutation, never rank order; a complete pass is exact for the policy
 * whatever the order and needs no minimum number of rounds; a truncated
 * pass is a uniform sample without replacement (finite-population SE). */
static void test_listing_rounds(void) {
    plan_env env;
    env_open(&env, 0.0);
    const fixture *f = find_fixture("flood_b");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    CHECK(ref_load(&board_a));
    uint32_t n = ref.count;
    CHECK_EQ(n, 10);
    uint32_t first[REF_SEARCH_LAYOUTS];
    for (uint32_t i = 0; i < n; i++) first[i] = 0;
    uint32_t identity = 0, runs = 300;
    ms_plan_test_stats base;
    ms_plan_result base_r;
    for (uint32_t seed = 0; seed <= runs; seed++) {
        ms_plan_limits_default(&env.limits); /* min_rollouts 16 > 10 layouts */
        env.limits.exact_node_limit = 0;
        if (seed > 0) {
            env.limits.flags = MS_PLAN_EXPLICIT_SEED;
            env.limits.seed = 0xC0FFEE00u + seed;
        }
        ms_plan_result r;
        ms_plan_test_stats st;
        CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
        CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
        CHECK_EQ(st.exhaustive, 1);
        CHECK_EQ(r.trials, n);
        CHECK_EQ(r.incomplete, 0);
        CHECK_EQ(st.rounds, n);
        CHECK(r.standard_error == 0.0); /* the policy's exact win rate */
        uint32_t seen = 0;
        bool sorted = true;
        for (uint32_t i = 0; i < n; i++) {
            CHECK(st.order[i] < n);
            CHECK(!(seen & (1u << st.order[i])));
            seen |= 1u << st.order[i];
            sorted = sorted && st.order[i] == i;
        }
        identity += sorted;
        if (seed == 0) {
            base_memcpy(&base, &st, sizeof(st));
            base_memcpy(&base_r, &r, sizeof(r));
            CHECK(!sorted);
            continue;
        }
        first[st.order[0]]++;
        /* The order changes nothing in a complete pass. */
        CHECK_EQ(st.candidates, base.candidates);
        for (uint32_t k = 0; k < st.candidates; k++) {
            CHECK_EQ(st.cell[k], base.cell[k]);
            CHECK_EQ(st.wins[k], base.wins[k]);
        }
        CHECK_EQ(r.cell, base_r.cell);
        CHECK_EQ(f64_bits(r.win_probability), f64_bits(base_r.win_probability));
    }
    CHECK_EQ(identity, 0);
    /* The first round is uniform over the listing: each of 10 layouts about
     * 30 times in 300 runs (sd 5.2). */
    for (uint32_t i = 0; i < n; i++) CHECK(first[i] >= 10u && first[i] <= 55u);

    /* A truncated pass over 60 layouts: the completed rounds are a random
     * prefix of the permutation, with the finite-population SE. */
    f = find_fixture("flood_c");
    len = fixture_obs(f, &board_a, obs_buf);
    CHECK(ref_load(&board_a));
    n = ref.count;
    CHECK_EQ(n, 60);
    bool truncated = false;
    for (double step = 0.002; step < 2.0 && !truncated; step *= 1.5) {
        env_close(&env);
        env_open(&env, step);
        env.limits.exact_node_limit = 0;
        env.limits.min_rollouts = 4;
        ms_plan_result r;
        ms_plan_test_stats st;
        CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
        if (r.status != MS_PLAN_ESTIMATED || st.rounds >= n) continue;
        truncated = true;
        uint32_t done = st.rounds;
        CHECK_EQ(r.trials - r.incomplete, done);
        CHECK(r.incomplete <= 1u);
        bool prefix = true;
        uint64_t seen = 0;
        for (uint32_t i = 0; i < r.trials && i < 64u; i++) {
            CHECK(st.order[i] < n && !(seen & bit(st.order[i])));
            seen |= bit(st.order[i]);
            prefix = prefix && st.order[i] == i;
        }
        CHECK(!prefix); /* not the first layouts in rank order */
        uint32_t won = (uint32_t)base_round(r.win_probability * (double)done);
        double fpc = (double)(n - done) / (double)(n - 1u);
        CHECK_NEAR(r.standard_error, jeffreys_se(won, done, fpc), 1e-12);
        print_u32("    truncated listing pass: rounds ", done);
        print_u32(" of ", n);
        test_print("\n");
    }
    CHECK(truncated);
    env_close(&env);
}

/* Default limits on genuine first-guess positions (after every proven-safe
 * cell is revealed) of the standard board sizes: usable ESTIMATED advice,
 * every round complete. The clock is frozen, so only work caps bind here;
 * a scan of advancing fake clocks then shows deadline-cut runs keep the
 * rounds they completed. */
static void test_default_usable(void) {
    static const uint32_t sizes[][4] = {{9, 9, 10, 2}, {16, 16, 40, 2}, {30, 16, 99, 1}};
    plan_env env;
    rt_rng rng;
    rt_rng_seed(&rng, 0xDEFA017u);
    for (uint32_t z = 0; z < 3u; z++) {
        uint32_t w = sizes[z][0], h = sizes[z][1], m = sizes[z][2];
        for (uint32_t found = 0; found < sizes[z][3];) {
            board_random(&board_a, &rng, w, h, m, 0);
            board_close(&board_a, HUGE_VAL);
            if (board_hidden(&board_a) <= m + (w * h) / 10u) continue; /* not an endgame */
            found++;
            size_t len = board_obs(&board_a, obs_buf);
            env_open(&env, 0.0);
            ms_plan_result r;
            ms_plan_test_stats st;
            CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
            CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
            CHECK_EQ(r.reason, MS_PLAN_REASON_NONE);
            CHECK_EQ(r.layouts, MS_PLAN_DEFAULT_SAMPLES);
            CHECK_EQ(r.posterior_exact, 1);
            CHECK_EQ(r.search_nodes, 0);
            CHECK_EQ(r.trials, MS_PLAN_DEFAULT_SAMPLES);
            CHECK_EQ(r.incomplete, 0);
            CHECK_EQ(r.candidates, MS_PLAN_DEFAULT_CANDIDATES);
            CHECK_EQ(st.exhaustive, 0);
            CHECK_EQ(st.rounds, MS_PLAN_DEFAULT_SAMPLES);
            for (uint32_t k = 0; k < st.candidates; k++) {
                CHECK_EQ(st.played[k], MS_PLAN_DEFAULT_SAMPLES);
            }
            CHECK(!board_a.revealed[r.cell]);
            CHECK(r.survival_probability > 0.0 && r.survival_probability <= 1.0);
            CHECK(r.standard_error <= 0.06);
            print_u32("    ", w);
            print_u32("x", h);
            print_u32("/", m);
            print_u32(": hidden ", board_hidden(&board_a));
            print_u32(" cell ", r.cell);
            test_print(" win ");
            test_print_double(r.win_probability);
            test_print("\n");
            env_close(&env);
        }
    }

    /* Deadlines: from complete runs to cut runs that still give estimates
     * from their completed rounds (one discarded round at most), down to
     * explicit UNAVAILABLE when the budget is spent too early. Steps grow
     * 4-fold, less than the factor 6 between 16 and 96 completed rounds. */
    board_random(&board_a, &rng, 16, 16, 40, 0);
    board_close(&board_a, HUGE_VAL);
    while (board_hidden(&board_a) <= 40u + 25u) {
        board_random(&board_a, &rng, 16, 16, 40, 0);
        board_close(&board_a, HUGE_VAL);
    }
    size_t len = board_obs(&board_a, obs_buf);
    bool full = false, cut = false, unavailable = false;
    for (double step = 0.0005; step < 200.0; step *= 4.0) {
        env_open(&env, step);
        ms_plan_result r;
        CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
        CHECK(r.incomplete <= 1u);
        if (r.status == MS_PLAN_ESTIMATED) {
            uint32_t done = r.trials - r.incomplete;
            CHECK(done >= MS_PLAN_DEFAULT_MIN_ROLLOUTS);
            if (done == MS_PLAN_DEFAULT_SAMPLES) {
                full = true;
            } else {
                cut = true;
            }
        } else {
            CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
            unavailable = true;
        }
        env_close(&env);
    }
    CHECK(full && cut && unavailable);
}

/* Finite samples never become certainty. A strip of 16 independent forced
 * 50/50s (win chance 2^-16 for every move) loses every rollout: that is
 * UNAVAILABLE, never an estimated 0% or a ranking of moves. A sampled run
 * whose advised move won every round reports a positive standard error. */
static void test_finite_samples(void) {
    plan_env env;
    env_open(&env, 0.0);
    char rows[2u * 48u + 1u];
    for (uint32_t i = 0; i < 16u; i++) {
        const char *block = (i & 1u) ? ".*" "oo" "oo" : "*." "oo" "oo";
        for (uint32_t j = 0; j < 6u; j++) rows[6u * i + j] = block[j];
    }
    rows[96] = '\0';
    board_rows(&board_a, 2, 48, rows);
    size_t len = board_obs(&board_a, obs_buf);
    ms_plan_result r;
    ms_plan_test_stats st;
    CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS);
    CHECK_EQ(r.layouts, MS_PLAN_DEFAULT_SAMPLES); /* 2^16 layouts: draws */
    CHECK_EQ(r.trials, MS_PLAN_DEFAULT_SAMPLES);  /* every round completed... */
    CHECK_EQ(r.incomplete, 0);
    CHECK_EQ(st.rounds, MS_PLAN_DEFAULT_SAMPLES);
    for (uint32_t k = 0; k < st.candidates; k++) CHECK_EQ(st.wins[k], 0); /* ...and lost */
    CHECK_EQ(r.cell, MS_PLAN_NO_CELL);
    CHECK(r.win_probability == 0.0 && r.standard_error == 0.0);

    /* Every round won: a fraction of 1, never certainty. */
    const fixture *f = find_fixture("flood_b");
    len = fixture_obs(f, &board_a, obs_buf);
    bool all_won = false;
    for (uint32_t seed = 1; seed <= 200u && !all_won; seed++) {
        ms_plan_limits_default(&env.limits);
        env.limits.exact_layout_limit = 0;
        env.limits.sample_count = 8;
        env.limits.min_rollouts = 8;
        env.limits.flags = MS_PLAN_EXPLICIT_SEED;
        env.limits.seed = seed;
        CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
        if (r.status != MS_PLAN_ESTIMATED || r.win_probability != 1.0) continue;
        all_won = true;
        CHECK_EQ(r.trials - r.incomplete, 8);
        CHECK_NEAR(r.standard_error, jeffreys_se(8, 8, 1.0), 1e-12);
        CHECK(r.standard_error > 0.05);
        CHECK(r.survival_probability < 1.0);
    }
    CHECK(all_won);
    env_close(&env);
}

/* An adversarial clock: it stands still, then jumps far past any deadline
 * at reading number jump_at. */
typedef struct jump_clock {
    double now;
    uint64_t reads;
    uint64_t jump_at;
} jump_clock;

static double jump_clock_read(void *ctx) {
    jump_clock *j = (jump_clock *)ctx;
    j->reads++;
    if (j->reads == j->jump_at) j->now += 1e9;
    return j->now;
}

static ms_status jump_run(jump_clock *j, rt_clock *clock, uint64_t jump_at, rt_mem *mem,
                          const ms_plan_limits *limits, size_t len, ms_plan_result *r,
                          ms_plan_test_stats *st) {
    j->now = 1000.0;
    j->reads = 0;
    j->jump_at = jump_at;
    rt_clock_init(clock, jump_clock_read, j);
    return ms_plan_test_run(obs_buf, len, limits, mem, clock, r, sizeof(*r), st);
}

/* The deadline passing at any clock reading - inside nested inference or
 * an exact tail search, not only between steps - never lets a time-cut
 * answer into a rollout: the rounds completed before it are exactly the
 * undisturbed run's, the round in progress is discarded, and the planner
 * stops after a few more readings. */
static void test_nested_deadline(void) {
    const fixture *f = find_fixture("flood_c");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    ms_plan_limits limits;
    ms_plan_limits_default(&limits);
    limits.exact_layout_limit = 0; /* draws: games infer and search tails */
    limits.sample_count = 12;
    limits.candidate_limit = 4;
    limits.min_rollouts = 2;
    jump_clock j;
    rt_clock clock;
    ms_plan_result base_r, r;
    static ms_plan_test_stats base, st;
    CHECK_STATUS(jump_run(&j, &clock, UINT64_MAX, &mem, &limits, len, &base_r, &base), MS_OK);
    CHECK_EQ(base_r.status, MS_PLAN_ESTIMATED);
    CHECK_EQ(base.rounds, 12);
    CHECK_EQ(base.nested_aborts_inference + base.nested_aborts_tail, 0);
    uint64_t total = j.reads;
    print_u32("    clock readings in an undisturbed run: ", total);
    test_print("\n");
    uint64_t stride = total > 1500u ? total / 1500u : 1u;
    uint32_t in_inference = 0, in_tail = 0, with_rounds = 0;
    uint64_t worst_after = 0;
    for (uint64_t at = 1; at <= total; at += stride) {
        CHECK_STATUS(jump_run(&j, &clock, at, &mem, &limits, len, &r, &st), MS_OK);
        CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_OK);
        CHECK_EQ(mem.live, 0);
        CHECK_EQ(mem.misuses, 0);
        CHECK(j.reads >= at);
        if (at == 1u) {
            /* The entry reading itself: the deadline is measured from the
             * jumped time, so nothing passes it - the undisturbed run. */
            CHECK(base_memcmp(&st, &base, sizeof(st)) == 0);
            CHECK_EQ(r.cell, base_r.cell);
            continue;
        }
        if (j.reads - at > worst_after) worst_after = j.reads - at;
        CHECK(r.incomplete <= 1u);
        if (st.nested_aborts_inference + st.nested_aborts_tail > 0) {
            CHECK_EQ(r.incomplete, 1);
            CHECK_EQ(st.nested_aborts_inference + st.nested_aborts_tail, 1);
        }
        in_inference += st.nested_aborts_inference;
        in_tail += st.nested_aborts_tail;
        uint32_t done = r.trials - r.incomplete;
        if (done == 0) continue;
        with_rounds++;
        CHECK_EQ(st.candidates, base.candidates);
        uint64_t mask = done >= 64u ? ~(uint64_t)0 : (((uint64_t)1 << done) - 1u);
        for (uint32_t k = 0; k < st.candidates; k++) {
            CHECK_EQ(st.cell[k], base.cell[k]);
            CHECK_EQ(st.round_wins[k] & mask, base.round_wins[k] & mask);
        }
    }
    print_u32("    jumps inside inference ", in_inference);
    print_u32(", inside tail searches ", in_tail);
    print_u32(", runs keeping completed rounds ", with_rounds);
    print_u32(", most readings after a jump ", worst_after);
    test_print("\n");
    CHECK(in_inference > 0u && in_tail > 0u && with_rounds > 0u);
    CHECK(worst_after <= 64u);
    rt_mem_dispose(&mem);
}

/* One nested call of the rollouts sees the deadline pass inside it and/or
 * breaks (ms_plan_test_set_fault), at the first, a middle and the last
 * call of each kind: the deadline alone discards only the round in
 * progress (the completed rounds stay exactly the undisturbed run's), and a
 * failure is MS_ERR_INTERNAL even when the deadline passed in the same
 * call - never mistaken for a cancellation - with the result untouched. */
static void test_nested_faults(void) {
    const fixture *f = find_fixture("flood_c");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    rt_mem mem;
    rt_mem_init(&mem, RT_MEM_UNLIMITED);
    ms_plan_limits limits;
    ms_plan_limits_default(&limits);
    limits.exact_layout_limit = 0; /* draws: games infer and search tails */
    limits.sample_count = 12;
    limits.candidate_limit = 4;
    limits.min_rollouts = 2;
    jump_clock j; /* never jumps here: the real deadline never passes */
    rt_clock clock;
    ms_plan_result base_r, r;
    static ms_plan_test_stats base, st;
    ms_plan_test_fault fault;
    ms_plan_test_set_fault(NULL);
    CHECK_STATUS(jump_run(&j, &clock, UINT64_MAX, &mem, &limits, len, &base_r, &base), MS_OK);
    CHECK_EQ(base_r.status, MS_PLAN_ESTIMATED);
    uint32_t calls[2] = {base.nested_inference_calls, base.nested_tail_calls};
    CHECK(calls[0] >= 2u && calls[1] >= 2u);
    print_u32("    nested inferences ", calls[0]);
    print_u32(", tail searches ", calls[1]);
    test_print("\n");
    for (uint32_t kind = 0; kind < 2u; kind++) {
        uint32_t ats[3] = {1u, (calls[kind] + 1u) / 2u, calls[kind]};
        for (uint32_t i = 0; i < 3u; i++) {
            base_memset(&fault, 0, sizeof(fault));
            if (kind == 0) {
                fault.inference_at = ats[i];
            } else {
                fault.tail_at = ats[i];
            }
            fault.trip = 1;
            ms_plan_test_set_fault(&fault);
            CHECK_STATUS(jump_run(&j, &clock, UINT64_MAX, &mem, &limits, len, &r, &st), MS_OK);
            CHECK_STATUS(ms_plan_result_validate(obs_buf, len, &r, sizeof(r)), MS_OK);
            CHECK_EQ(st.nested_aborts_inference, kind == 0 ? 1u : 0u);
            CHECK_EQ(st.nested_aborts_tail, kind == 0 ? 0u : 1u);
            /* Nothing of that kind ran after it. */
            CHECK_EQ(kind == 0 ? st.nested_inference_calls : st.nested_tail_calls, ats[i]);
            CHECK_EQ(r.incomplete, 1);
            uint32_t done = r.trials - 1u;
            CHECK_EQ(st.rounds, done);
            CHECK_EQ(st.candidates, base.candidates);
            uint64_t mask = done >= 64u ? ~(uint64_t)0 : (((uint64_t)1 << done) - 1u);
            for (uint32_t k = 0; k < st.candidates; k++) {
                CHECK_EQ(st.cell[k], base.cell[k]);
                CHECK_EQ(st.round_wins[k] & mask, base.round_wins[k] & mask);
            }
            /* Failures: an error status, or (inference) an MS_OK result
             * that is malformed; with and without the deadline. */
            for (uint32_t fail = 1; fail <= (kind == 0 ? 2u : 1u); fail++) {
                for (uint32_t trip = 0; trip < 2u; trip++) {
                    fault.trip = trip;
                    fault.fail = fail;
                    ms_plan_test_set_fault(&fault);
                    base_memset(&r, 0xA5, sizeof(r));
                    CHECK_STATUS(jump_run(&j, &clock, UINT64_MAX, &mem, &limits, len, &r, &st),
                                 MS_ERR_INTERNAL);
                    const uint8_t *bytes = (const uint8_t *)&r;
                    bool untouched = true;
                    for (size_t b = 0; b < sizeof(r); b++) untouched &= bytes[b] == 0xA5u;
                    CHECK(untouched);
                    CHECK_EQ(mem.live, 0);
                    CHECK_EQ(mem.misuses, 0);
                }
            }
        }
    }
    /* Past the last call of each kind: the undisturbed run. */
    fault.inference_at = calls[0] + 1u;
    fault.tail_at = calls[1] + 1u;
    fault.trip = 1;
    fault.fail = 1;
    ms_plan_test_set_fault(&fault);
    CHECK_STATUS(jump_run(&j, &clock, UINT64_MAX, &mem, &limits, len, &r, &st), MS_OK);
    CHECK(base_memcmp(&st, &base, sizeof(st)) == 0);
    CHECK_EQ(r.cell, base_r.cell);
    ms_plan_test_set_fault(NULL);
    rt_mem_dispose(&mem);
}

/* ---------------------------------------------------------------- budgets */

static void test_budget_transitions(void) {
    plan_env env;
    env_open(&env, 0.0);
    const fixture *f = find_fixture("geometry_c");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    CHECK(ref_load(&board_a));
    ref_root root;
    CHECK(ref_solve(&root));
    ms_plan_result r;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    check_exact(&r, &root, f->name);
    uint32_t nodes = r.search_nodes;
    CHECK(nodes > 2u);
    /* One node short of completion: the search proves nothing and the
     * rollouts answer instead (over every listed layout here). */
    env.limits.exact_node_limit = nodes - 1u;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
    CHECK_EQ(r.search_nodes, nodes - 1u);
    CHECK_EQ(r.layouts, ref.count); /* the nodes came from this exact listing */
    CHECK_EQ(r.posterior_exact, 1);
    CHECK_EQ(r.exact_total, 0);
    CHECK_EQ(r.trials, ref.count);
    env.limits.exact_node_limit = nodes;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    check_exact(&r, &root, f->name);
    /* Too few layouts allowed: not exhaustive, so sampled rollouts. */
    env.limits.exact_layout_limit = ref.count - 1u;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
    CHECK_EQ(r.layouts, env.limits.sample_count);
    CHECK_EQ(r.posterior_exact, 1);
    /* Fewer draws than min_rollouts: explicitly unavailable, at once. */
    env.limits.sample_count = 8;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS);
    CHECK_EQ(r.layouts, 8);
    CHECK_EQ(r.trials, 0);
    CHECK_EQ(r.cell, MS_PLAN_NO_CELL);
    env.limits.min_rollouts = 8;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
    env.limits.min_rollouts = 0; /* means 1 */
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
    env_close(&env);

    /* A clock that advances: every exact-search answer is complete, and a
     * deadline only ever downgrades the answer. */
    bool saw_exact = false, saw_estimate = false, saw_unavailable = false;
    for (double budget = 5.0; budget <= 20000.0; budget *= 1.6) {
        env_open(&env, 1.0);
        env.limits.time_budget_ms = budget;
        CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
        if (r.status == MS_PLAN_EXACT) {
            check_exact(&r, &root, f->name);
            saw_exact = true;
        } else if (r.status == MS_PLAN_ESTIMATED) {
            saw_estimate = true;
        } else {
            CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
            saw_unavailable = true;
        }
        CHECK(r.elapsed_ms > 0.0);
        env_close(&env);
    }
    CHECK(saw_exact && saw_unavailable);
    (void)saw_estimate;
}

static void test_rollout_limits(void) {
    plan_env env;
    env_open(&env, 0.0);
    const fixture *f = find_fixture("flood_c");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    ms_plan_result r;
    ms_plan_test_stats st;
    /* A step limit that cuts a game: the rounds that would complete are
     * length-selected, so the rollouts stop at once with no estimate. */
    env.limits.exact_layout_limit = 0;
    for (uint32_t limit = 0; limit <= 4u; limit++) {
        env.limits.rollout_step_limit = limit;
        CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
        CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
        CHECK_EQ(r.reason, MS_PLAN_REASON_BUDGET);
        CHECK_EQ(r.incomplete, 1); /* the first cut round ends the rollouts */
        CHECK(r.trials >= 1u);
        CHECK_EQ(r.trials - r.incomplete, st.rounds);
        for (uint32_t k = 0; k < st.candidates; k++) {
            CHECK(st.played[k] == st.rounds || st.played[k] == st.rounds + 1u);
        }
    }
    /* A limit no game reaches (21 cells: every reveal opens one). */
    env.limits.rollout_step_limit = 21;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
    CHECK_EQ(r.incomplete, 0);
    CHECK_EQ(r.trials, env.limits.sample_count);
    env_close(&env);

    /* The deadline mid-round: the partial round is discarded for every
     * candidate, so all candidates keep equal denominators. */
    bool partial = false;
    uint32_t cut_estimates = 0;
    for (double budget = 20.0; budget <= 4000.0; budget *= 1.25) {
        env_open(&env, 0.5);
        env.limits.exact_layout_limit = 0;
        env.limits.time_budget_ms = budget;
        env.limits.min_rollouts = 4;
        CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
        CHECK(r.incomplete <= 1u);
        if (r.status == MS_PLAN_ESTIMATED || r.reason == MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS) {
            CHECK_EQ(r.trials - r.incomplete, st.rounds);
            uint32_t extra = 0;
            for (uint32_t k = 0; k < st.candidates; k++) {
                CHECK(st.played[k] == st.rounds || st.played[k] == st.rounds + 1u);
                extra += st.played[k] - st.rounds;
            }
            CHECK(r.incomplete == 1u || extra == 0u);
            if (extra > 0u) partial = true;
        }
        if (r.status == MS_PLAN_ESTIMATED) {
            CHECK(st.rounds >= 4u);
            /* The decision holds whatever the discarded round's outcome:
             * it counts against every switch from the safest candidate. */
            uint32_t advised = 0;
            double top = 0.0, unknown = (double)r.incomplete;
            for (uint32_t k = 1; k < st.candidates; k++) {
                double gain = (double)st.gain[k], loss = (double)st.loss[k];
                double margin = gain - loss - unknown - ref_sqrt(gain + loss + unknown);
                if (margin > top + 1e-9) {
                    top = margin;
                    advised = k;
                }
            }
            CHECK_EQ(r.cell, st.cell[advised]);
            if (r.incomplete == 1u) cut_estimates++;
        }
        env_close(&env);
    }
    CHECK(partial);
    CHECK(cut_estimates > 0u);
}

static void test_zero_budgets(void) {
    plan_env env;
    env_open(&env, 0.0);
    const fixture *f = find_fixture("flood_b");
    size_t len = fixture_obs(f, &board_a, obs_buf);
    ms_plan_result r;
    env.limits.time_budget_ms = 0.0;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_BUDGET);
    ms_plan_limits_default(&env.limits);
    env.limits.memory_budget_bytes = 0;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_BUDGET);
    ms_plan_limits_default(&env.limits);
    env.limits.exact_layout_limit = 0;
    env.limits.sample_count = 0;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_NO_SAMPLES);
    CHECK_EQ(r.layouts, 0);
    CHECK_EQ(r.posterior_exact, 0); /* the exact posterior, but no layout of it */
    CHECK_EQ(r.candidates, 0);
    CHECK_EQ(r.trials, 0);
    ms_plan_limits_default(&env.limits);
    env.limits.exact_layout_limit = 0;
    env.limits.candidate_limit = 0;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
    CHECK_EQ(r.reason, MS_PLAN_REASON_BUDGET);
    /* Zero search nodes: never exact, the rollouts decide. */
    ms_plan_limits_default(&env.limits);
    env.limits.exact_node_limit = 0;
    env.limits.min_rollouts = 1;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_ESTIMATED);
    CHECK_EQ(r.search_nodes, 0);
    /* A byte budget too small for the root layouts but not for inference. */
    ms_plan_limits_default(&env.limits);
    env.limits.memory_budget_bytes = 96u << 10;
    rt_mem_reset_peak(&env.mem);
    size_t before = env.mem.live;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK(env.mem.peak - before <= (size_t)env.limits.memory_budget_bytes);
    env_close(&env);
}

static void test_failure_injection(void) {
    plan_env env;
    env_open(&env, 0.0);
    static const char *const names[] = {"flood_b", "notsafest_c"};
    for (uint32_t i = 0; i < 2u; i++) {
        const fixture *f = find_fixture(names[i]);
        size_t len = fixture_obs(f, &board_a, obs_buf);
        for (uint32_t mode = 0; mode < 2u; mode++) {
            ms_plan_limits_default(&env.limits);
            if (mode == 1u) {
                env.limits.exact_layout_limit = 0; /* rollouts */
                env.limits.sample_count = 20;
                env.limits.min_rollouts = 4;
            }
            ms_plan_result base;
            uint64_t requests = env.mem.requests;
            CHECK_STATUS(plan(&env, obs_buf, len, &base), MS_OK);
            uint64_t total = env.mem.requests - requests;
            CHECK(total > 0u);
            uint64_t stride = total > 120u ? total / 120u : 1u;
            for (uint64_t after = 0; after <= total; after += stride) {
                for (uint32_t forever = 0; forever < 2u; forever++) {
                    rt_mem_set_failure(&env.mem, after, forever ? RT_MEM_FOREVER : 1u);
                    uint64_t failures = env.mem.failures;
                    ms_plan_result r;
                    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
                    rt_mem_set_failure(&env.mem, 0, 0);
                    if (env.mem.failures == failures) {
                        CHECK(base_memcmp(&r, &base, sizeof(r)) == 0);
                    } else if (r.status == MS_PLAN_EXACT) {
                        CHECK_EQ(r.exact_wins, base.exact_wins);
                    }
                }
            }
        }
    }
    env_close(&env);
}

/* ---------------------------------------------------------------- big boards */

static void test_huge_boards(void) {
    /* An 80x80 board whose only uncertainty is a small embedded position:
     * the exact search must give the small board's exact value. */
    const char *rows = "*...oooo" "*..*oooo" "*oo**ooo" "oooooooo";
    board_rows(&board_b, 8, 4, rows);
    CHECK(ref_load(&board_b));
    CHECK_EQ(ref.count, 9);
    ref_root root;
    CHECK(ref_solve(&root));
    CHECK(!root.certain);
    CHECK_EQ(root.best, 5);
    board *b = &board_a;
    b->width = b->height = 80;
    b->cells = 6400;
    b->mines = board_b.mines;
    for (uint32_t c = 0; c < b->cells; c++) {
        uint32_t row = c / 80u, col = c % 80u;
        bool inside = row < 4u && col < 8u;
        b->mine[c] = inside ? board_b.mine[row * 8u + col] : 0;
        b->revealed[c] = inside ? board_b.revealed[row * 8u + col] : 1;
    }
    size_t len = board_obs(b, obs_buf);
    plan_env env;
    env_open(&env, 0.0);
    ms_plan_result r;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK_EQ(r.status, MS_PLAN_EXACT);
    CHECK_EQ(r.exact_total, 9);
    CHECK_EQ(r.exact_wins, 5);
    uint32_t small = (r.cell / 80u) * 8u + r.cell % 80u;
    CHECK(r.cell % 80u < 8u && r.cell / 80u < 4u);
    CHECK_EQ(root.value[small], 5);
    env_close(&env);

    /* A real 80x80 midgame: bounded by the injected clock and the byte
     * budget, whatever the answer. */
    rt_rng rng;
    rt_rng_seed(&rng, 0x8080u);
    for (uint32_t tries = 0;; tries++) {
        CHECK(tries < 20u);
        board_random(b, &rng, 80, 80, 1300, 0);
        board_close(b, HUGE_VAL);
        if (board_hidden(b) > b->mines + 200u) break; /* a guess, far from the end */
    }
    len = board_obs(b, obs_buf);
    env_open(&env, 2.0);
    rt_mem_reset_peak(&env.mem);
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK(r.status == MS_PLAN_ESTIMATED || r.status == MS_PLAN_UNAVAILABLE);
    CHECK(r.elapsed_ms <= env.limits.time_budget_ms + 1000.0);
    CHECK(env.mem.peak <= (size_t)env.limits.memory_budget_bytes);
    test_print("    80x80, fake clock at 2 ms per reading: status ");
    test_print_u64(r.status);
    print_u32(" reason ", r.reason);
    print_u32(" layouts ", r.layouts);
    print_u32(" candidates ", r.candidates);
    print_u32(" trials ", r.trials);
    print_u32(" incomplete ", r.incomplete);
    test_print(" elapsed ");
    test_print_double(r.elapsed_ms);
    test_print("\n");
    /* A tight budget stays tight. */
    env.limits.time_budget_ms = 50.0;
    CHECK_STATUS(plan(&env, obs_buf, len, &r), MS_OK);
    CHECK(r.status == MS_PLAN_UNAVAILABLE || r.status == MS_PLAN_ESTIMATED);
    CHECK(r.elapsed_ms <= 50.0 + 1000.0);
    env_close(&env);

    /* Whole paired rounds of complete 80x80 games (time frozen). */
    env_open(&env, 0.0);
    env.limits.sample_count = 4;
    env.limits.min_rollouts = 4;
    ms_plan_test_stats st;
    CHECK_STATUS(plan_stats(&env, obs_buf, len, &r, &st), MS_OK);
    CHECK_EQ(r.trials, 4);
    CHECK_EQ(r.incomplete, 0);
    CHECK_EQ(st.rounds, 4);
    CHECK_EQ(st.candidates, 8);
    uint32_t most = 0;
    for (uint32_t k = 0; k < st.candidates; k++) {
        CHECK_EQ(st.played[k], 4);
        if (st.wins[k] > most) most = st.wins[k];
    }
    if (r.status == MS_PLAN_ESTIMATED) {
        CHECK(r.win_probability > 0.0 && r.standard_error > 0.0);
    } else {
        /* The advised move won no round: no evidence, not a 0% chance. */
        CHECK_EQ(r.status, MS_PLAN_UNAVAILABLE);
        CHECK_EQ(r.reason, MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS);
        CHECK(most <= 1u); /* two wins against none would switch the advice */
    }
    print_u32("    80x80 rounds: status ", r.status);
    print_u32(" most wins of 4: ", most);
    test_print("\n");
    env_close(&env);
}

void test_planner(void) {
    test_case("independent square-root reference");
    CHECK(ref_sqrt(0.0) == 0.0);
    CHECK_NEAR(ref_sqrt(0.25), 0.5, 1e-15);
    CHECK_NEAR(ref_sqrt(4.0), 2.0, 1e-15);
    CHECK_NEAR(ref_sqrt(2.0), 1.4142135623730951, 1e-15);
    test_case("limits defaults and validation");
    test_limits();
    test_case("argument, aliasing and consistency errors");
    test_arguments();
    test_case("result validation");
    test_result_validate();
    test_case("not started, finished and certain moves");
    test_placeholders();
    test_case("exact fixtures against independent enumeration");
    test_exact_fixtures();
    test_case("random small positions against independent enumeration");
    test_exact_random();
    test_case("gold fixtures: optimal guesses that are not the safest");
    test_gold_fixtures();
    test_case("exhaustive paired rollouts equal the policy's terminal outcomes");
    test_rollout_exhaustive_rounds();
    test_case("sampled rollouts estimate the policy's win rate");
    test_rollout_statistics();
    test_case("no clairvoyance across indistinguishable boards");
    test_no_clairvoyance();
    test_case("complete listings: random round order and exact passes");
    test_listing_rounds();
    test_case("default limits give estimated advice on first-guess positions");
    test_default_usable();
    test_case("finite samples: no wins is unavailable, all wins is uncertain");
    test_finite_samples();
    test_case("deadline jumps inside nested inference and tail searches");
    test_nested_deadline();
    test_case("nested failures are errors, also when the deadline passes in them");
    test_nested_faults();
    test_case("budget transitions between exact, estimated and unavailable");
    test_budget_transitions();
    test_case("rollout step limits and mid-round deadlines");
    test_rollout_limits();
    test_case("zero budgets");
    test_zero_budgets();
    test_case("allocation failure injection");
    test_failure_injection();
    test_case("80x80 boards");
    test_huge_boards();
}
