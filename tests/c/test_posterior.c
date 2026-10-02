/* Tests for ms_posterior_generate (c/posterior.h; c/probability.c stage 7):
 * complete posterior layouts. An independent whole-board enumerator checks
 * exhaustive output (exactly the consistent layouts, each once) and the
 * frequencies of exact uniform draws; importance-weighted draws are
 * compared with the exact posterior; huge boards, determinism, deadlines,
 * memory, failure injection and argument validation follow the ms_solve
 * tests. Every call is wrapped by generate(), which independently checks
 * each row against the clues, the total and the result's proofs. Clocks are
 * fake (deterministic): with step 0 time never passes. */

#include "test_support.h"

#include "posterior.h"
#include "fixtures/fixture_types.h"
#include "fixtures/large_boards.h"
#include "fixtures/probability_cases.h"
#include "fixtures/sampling_cases.h"

#define MAX_CELLS MS_SOLVER_MAX_CELLS
#define OBS_WORDS ((sizeof(ms_obs_header) + MAX_CELLS + 7u) / 8u)
#define RESULT_WORDS ((sizeof(ms_result_header) + (size_t)MAX_CELLS * 9u + 7u) / 8u)
#define ROW_BYTES ((size_t)1 << 20) /* layout rows of one call */
#define GUARD_BYTES 64u             /* past the rows: must stay untouched */

static uint64_t obs_buf[OBS_WORDS];
static uint64_t obs_copy[OBS_WORDS];
static uint64_t result_buf[RESULT_WORDS];
static uint64_t result_ref[RESULT_WORDS];
static uint64_t rows_words[ROW_BYTES / 8u];
static uint64_t rows_ref_words[ROW_BYTES / 8u];
static int8_t dense[MAX_CELLS];

#define ROWS ((uint8_t *)rows_words)
#define ROWS_REF ((uint8_t *)rows_ref_words)

/* ---------------------------------------------------------------- helpers */

typedef struct post_env {
    rt_mem mem;
    fake_clock fake;
    rt_clock clock;
    ms_infer_limits limits;
} post_env;

static void env_open(post_env *env, double step) {
    rt_mem_init(&env->mem, RT_MEM_UNLIMITED);
    fake_clock_init(&env->clock, &env->fake, 1000.0, step);
    ms_limits_default(&env->limits);
}

static void env_close(post_env *env) {
    CHECK_EQ(env->mem.live, 0);
    CHECK_EQ(env->mem.live_blocks, 0);
    CHECK_EQ(env->mem.misuses, 0);
    rt_mem_dispose(&env->mem);
}

static void env_seed(post_env *env, uint64_t seed) {
    env->limits.flags = MS_LIMIT_EXPLICIT_SEED;
    env->limits.seed = seed;
}

static uint32_t cells_of(const void *obs) {
    const ms_obs_header *h = (const ms_obs_header *)obs;
    return h->width * h->height;
}

static size_t result_len_of(const void *obs) {
    const ms_obs_header *h = (const ms_obs_header *)obs;
    return ms_result_size(h->width, h->height);
}

static const ms_result_header *header(const void *result) {
    return (const ms_result_header *)result;
}

static double prob_at(const void *result, uint32_t cell) {
    return ms_result_probabilities(result)[cell];
}

static uint64_t f64_bits(double value) {
    union {
        double d;
        uint64_t u;
    } pun;
    pun.d = value;
    return pun.u;
}

static uint32_t max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
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

/* Independent of the implementation: 0/1 bytes, revealed cells safe, every
 * clue equal to its mined neighbors and the mine total exact. */
static bool layout_ok(const void *obs, const uint8_t *row) {
    const ms_obs_header *h = (const ms_obs_header *)obs;
    const uint8_t *clues = ms_obs_clues(obs);
    int32_t width = (int32_t)h->width, height = (int32_t)h->height;
    uint32_t mines = 0;
    for (int32_t r = 0; r < height; r++) {
        for (int32_t c = 0; c < width; c++) {
            uint32_t i = (uint32_t)(r * width + c);
            if (row[i] > 1u) return false;
            mines += row[i];
            if (clues[i] == MS_CLUE_HIDDEN) continue;
            if (row[i] != 0) return false;
            uint32_t near = 0;
            for (int32_t dr = -1; dr <= 1; dr++) {
                for (int32_t dc = -1; dc <= 1; dc++) {
                    int32_t rr = r + dr, cc = c + dc;
                    if ((dr || dc) && rr >= 0 && cc >= 0 && rr < height && cc < width) {
                        near += row[rr * width + cc];
                    }
                }
            }
            if (near != clues[i]) return false;
        }
    }
    return mines == h->total_mines;
}

static size_t rows_len(const void *obs, uint32_t exact_limit, uint32_t sample_count) {
    size_t len = (size_t)max_u32(exact_limit, sample_count) * cells_of(obs);
    CHECK(len + GUARD_BYTES <= ROW_BYTES);
    return len;
}

/* The info contract against the result just written. */
static void check_info(const ms_posterior_info *info, uint32_t exact_limit,
                       uint32_t sample_count) {
    const ms_result_header *r = header(result_buf);
    CHECK(info->count <= max_u32(exact_limit, sample_count));
    CHECK(info->exhaustive <= 1u && info->exact_distribution <= 1u);
    double ess = r->has_effective_sample_size ? r->effective_sample_size : 0.0;
    CHECK_EQ(f64_bits(info->effective_samples), f64_bits(ess));
    if (r->status == MS_PROB_UNAVAILABLE) {
        CHECK_EQ(info->count, 0);
        CHECK_EQ(info->exhaustive, 0);
        CHECK_EQ(info->exact_distribution, 0);
        CHECK_EQ(info->total_layouts, 0);
        CHECK_EQ(info->reason, r->reason);
        return;
    }
    CHECK_EQ(info->exact_distribution, r->status == MS_PROB_EXACT);
    if (!info->exact_distribution) {
        CHECK_EQ(info->exhaustive, 0);
        CHECK_EQ(info->total_layouts, 0);
        CHECK(info->effective_samples > 0.0);
    }
    if (info->exhaustive) {
        CHECK(info->total_layouts > 0 && info->total_layouts <= exact_limit);
        CHECK_EQ(info->count, info->total_layouts);
        CHECK_EQ(info->reason, MS_REASON_NONE);
        return;
    }
    switch (info->reason) {
    case MS_REASON_NONE:
        CHECK(info->exact_distribution);
        CHECK_EQ(info->count, sample_count);
        CHECK(info->total_layouts == 0 || info->total_layouts > exact_limit);
        break;
    case MS_REASON_COUNTING_BUDGET_EXCEEDED:
        CHECK(!info->exact_distribution);
        CHECK_EQ(info->count, sample_count);
        break;
    case MS_REASON_SAMPLING_BUDGET_EXHAUSTED:
        CHECK_EQ(sample_count, 0);
        CHECK_EQ(info->count, 0);
        break;
    case MS_REASON_TIME_BUDGET_EXHAUSTED:
    case MS_REASON_MEMORY_BUDGET_EXHAUSTED:
        CHECK(info->count <= sample_count);
        break;
    default:
        CHECK(false);
    }
}

/* Every row a complete layout agreeing with every proof of the result;
 * rows past the count zero. */
static void check_rows(const void *obs, uint32_t count, size_t len) {
    uint32_t n = cells_of(obs);
    const uint8_t *flags = ms_result_flags(result_buf);
    for (uint32_t k = 0; k < count; k++) {
        const uint8_t *row = ROWS + (size_t)k * n;
        if (!layout_ok(obs, row)) {
            test_print("    row ");
            test_print_u64(k);
            test_print(" is not a complete layout\n");
        }
        CHECK(layout_ok(obs, row));
        for (uint32_t v = 0; v < n; v++) {
            if (flags[v] & MS_PCELL_PROVEN_MINE) CHECK_EQ(row[v], 1);
            if (flags[v] & MS_PCELL_PROVEN_SAFE) CHECK_EQ(row[v], 0);
        }
    }
    for (size_t i = (size_t)count * n; i < len; i++) CHECK_EQ(ROWS[i], 0);
}

/* ms_posterior_generate plus the invariants of every call: the workspace
 * returns to its live bytes, blocks and budget without misuse, the
 * observation is unchanged, no byte past the rows is written, and on MS_OK
 * the result validates, info keeps its contract and every row is a
 * complete layout consistent with the proofs. */
static ms_status generate(post_env *env, const void *obs, size_t obs_len, uint32_t exact_limit,
                          uint32_t sample_count, ms_posterior_info *info) {
    size_t live = env->mem.live, blocks = env->mem.live_blocks, budget = env->mem.budget;
    uint64_t misuses = env->mem.misuses;
    base_memcpy(obs_copy, obs, obs_len);
    size_t rlen = result_len_of(obs);
    size_t len = rows_len(obs, exact_limit, sample_count);
    base_memset(ROWS, 0xA5, len + GUARD_BYTES);
    ms_status status = ms_posterior_generate(obs, obs_len, &env->limits, exact_limit,
                                             sample_count, &env->mem, &env->clock, result_buf,
                                             rlen, len ? ROWS : NULL, len, info);
    CHECK_EQ(env->mem.live, live);
    CHECK_EQ(env->mem.live_blocks, blocks);
    CHECK_EQ(env->mem.budget, budget);
    CHECK_EQ(env->mem.misuses, misuses);
    CHECK(base_memcmp(obs_copy, obs, obs_len) == 0);
    for (size_t i = len; i < len + GUARD_BYTES; i++) CHECK_EQ(ROWS[i], 0xA5);
    if (status == MS_OK) {
        CHECK_STATUS(ms_result_validate_solver(obs, obs_len, result_buf, rlen), MS_OK);
        check_info(info, exact_limit, sample_count);
        check_rows(obs, info->count, len);
    }
    return status;
}

/* Bytes of the rows of one call (and its info). */
static uint64_t output_digest(const void *obs, const ms_posterior_info *info) {
    uint64_t h = rt_hash64(ROWS, (size_t)info->count * cells_of(obs), 0x706F737465726Full);
    return rt_hash64(info, sizeof(*info), h);
}

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

/* Rows [0, count) pairwise different. */
static void check_distinct_rows(uint32_t count, uint32_t n) {
    static uint64_t hashes[1024];
    CHECK(count <= 1024u);
    for (uint32_t k = 0; k < count; k++) {
        hashes[k] = rt_hash64(ROWS + (size_t)k * n, n, 0);
        for (uint32_t j = 0; j < k; j++) {
            CHECK(hashes[j] != hashes[k] ||
                  base_memcmp(ROWS + (size_t)j * n, ROWS + (size_t)k * n, n) != 0);
        }
    }
}

/* ------------------------------------- independent whole-board oracle */

#define ORACLE_MAX_HIDDEN 22u
#define ORACLE_MAX_LAYOUTS 8192u

/* Every consistent layout as a mask over the hidden cells (bit k: hidden[k]
 * holds a mine), ascending. */
typedef struct oracle {
    uint32_t nh;
    uint32_t hidden[ORACLE_MAX_HIDDEN];
    uint64_t layouts;  /* all consistent layouts */
    uint32_t stored;   /* min(layouts, ORACLE_MAX_LAYOUTS) masks below */
    uint64_t mask[ORACLE_MAX_LAYOUTS];
} oracle;

static oracle the_oracle;

static uint32_t popcount64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return (uint32_t)((x * 0x0101010101010101ull) >> 56);
}

/* Enumerates every placement of `total` mines on the hidden cells (Gosper's
 * hack: same popcount, ascending) and keeps those matching every clue;
 * false if there are too many hidden cells. */
static bool oracle_run(uint32_t width, uint32_t height, uint32_t total, const int8_t *clues,
                       oracle *o) {
    static uint32_t index_of[MAX_CELLS];
    static uint64_t clue_mask[MAX_CELLS];
    static uint32_t clue_value[MAX_CELLS];
    uint32_t n = width * height;
    o->nh = 0;
    o->layouts = 0;
    o->stored = 0;
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
        clue_value[nclues++] = (uint32_t)clues[i];
    }
    if (total > o->nh) return true;
    uint64_t limit = 1ull << o->nh;
    uint64_t mask = total == 0 ? 0 : (1ull << total) - 1u;
    while (mask < limit) {
        bool ok = true;
        for (uint32_t k = 0; k < nclues && ok; k++) {
            ok = popcount64(mask & clue_mask[k]) == clue_value[k];
        }
        if (ok) {
            if (o->stored < ORACLE_MAX_LAYOUTS) o->mask[o->stored++] = mask;
            o->layouts++;
        }
        if (total == 0) break;
        uint64_t low = mask & (0u - mask);
        uint64_t ripple = mask + low;
        mask = (((ripple ^ mask) >> 2) / low) | ripple;
    }
    return true;
}

static uint64_t row_mask(const oracle *o, const uint8_t *row) {
    uint64_t mask = 0;
    for (uint32_t k = 0; k < o->nh; k++) mask |= (uint64_t)row[o->hidden[k]] << k;
    return mask;
}

/* Index of a layout among the stored masks, or UINT32_MAX. */
static uint32_t oracle_find(const oracle *o, uint64_t mask) {
    uint32_t lo = 0, hi = o->stored;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (o->mask[mid] < mask) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo < o->stored && o->mask[lo] == mask ? lo : UINT32_MAX;
}

/* In-place heapsort of masks. */
static void sort_masks(uint64_t *a, uint32_t count) {
    for (uint32_t start = count / 2u; start-- > 0;) {
        for (uint32_t root = start;;) {
            uint32_t child = 2u * root + 1u;
            if (child >= count) break;
            if (child + 1u < count && a[child] < a[child + 1u]) child++;
            if (a[root] >= a[child]) break;
            uint64_t t = a[root];
            a[root] = a[child];
            a[child] = t;
            root = child;
        }
    }
    for (uint32_t end = count; end-- > 1u;) {
        uint64_t t = a[0];
        a[0] = a[end];
        a[end] = t;
        for (uint32_t root = 0;;) {
            uint32_t child = 2u * root + 1u;
            if (child >= end) break;
            if (child + 1u < end && a[child] < a[child + 1u]) child++;
            if (a[root] >= a[child]) break;
            t = a[root];
            a[root] = a[child];
            a[child] = t;
            root = child;
        }
    }
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

/* A random layout, then a random subset of its safe cells revealed, with at
 * most max_hidden hidden cells (thin and square shapes). */
static void random_observation(rt_rng *rng, uint32_t *width, uint32_t *height, uint32_t *total,
                               int8_t *clues, uint32_t max_hidden) {
    static const uint32_t sizes[][2] = {{3, 3}, {4, 3}, {4, 4}, {5, 3}, {1, 9}, {9, 1},
                                        {2, 6}, {6, 2}, {5, 4}, {1, 16}, {12, 1}, {6, 3},
                                        {7, 3}, {5, 5}, {11, 2}, {3, 7}, {2, 2}, {1, 3}};
    uint32_t pick = rt_rng_below(rng, (uint32_t)array_size(sizes));
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

/* ------------------------------------------------ exhaustive enumeration */

static void print_label(const char *what, const char *label) {
    test_print("    ");
    test_print(what);
    test_print(": ");
    test_print(label);
    test_print("\n");
}

/* The posterior of (width, height, total, clues) with exact_limit >= Z is
 * exactly the oracle's layout set, each layout once, with marginals equal
 * to the exact probabilities; with Z > exact_limit, uniform-posterior
 * draws of consistent layouts. Inconsistent observations are errors. */
static void check_exhaustive(post_env *env, uint32_t width, uint32_t height, uint32_t total,
                             const int8_t *clues, uint32_t exact_limit, const char *label) {
    oracle *o = &the_oracle;
    CHECK(oracle_run(width, height, total, clues, o));
    size_t len = make_obs(obs_buf, width, height, total, clues);
    ms_posterior_info info;
    ms_status status = generate(env, obs_buf, len, exact_limit, 3, &info);
    if (o->layouts == 0) {
        if (status != MS_ERR_INCONSISTENT) print_label("expected inconsistent", label);
        CHECK_STATUS(status, MS_ERR_INCONSISTENT);
        return;
    }
    CHECK_STATUS(status, MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_EXACT);
    CHECK_EQ(info.exact_distribution, 1);
    CHECK_EQ(info.total_layouts, o->layouts);
    uint32_t n = width * height;
    if (o->layouts > exact_limit) {
        CHECK_EQ(info.exhaustive, 0);
        CHECK_EQ(info.count, 3);
        CHECK_EQ(info.reason, MS_REASON_NONE);
        if (o->stored == o->layouts) {
            for (uint32_t k = 0; k < info.count; k++) {
                CHECK(oracle_find(o, row_mask(o, ROWS + (size_t)k * n)) != UINT32_MAX);
            }
        }
        return;
    }
    if (info.exhaustive != 1 || info.count != o->layouts) print_label("not exhaustive", label);
    CHECK_EQ(info.exhaustive, 1);
    CHECK_EQ(info.count, o->layouts);
    CHECK_EQ(info.reason, MS_REASON_NONE);
    CHECK(o->stored == o->layouts);
    static uint64_t got[ORACLE_MAX_LAYOUTS];
    for (uint32_t k = 0; k < info.count; k++) got[k] = row_mask(o, ROWS + (size_t)k * n);
    sort_masks(got, info.count);
    for (uint32_t k = 0; k < info.count; k++) {
        if (got[k] != o->mask[k]) print_label("layout set differs", label);
        CHECK_EQ(got[k], o->mask[k]); /* the same set; ascending => each once */
    }
    /* Exhaustive rows give the exact marginals (Z < 2^53: correctly rounded). */
    for (uint32_t k = 0; k < o->nh; k++) {
        uint64_t mined = 0;
        for (uint32_t j = 0; j < info.count; j++) mined += (got[j] >> k) & 1u;
        CHECK(prob_at(result_buf, o->hidden[k]) == (double)mined / (double)info.count);
    }
}

static void test_exhaustive_random_boards(void) {
    test_case("exhaustive: exactly the enumerated layout set, each once (C PRNG boards)");
    post_env env;
    env_open(&env, 0.0);
    rt_rng rng;
    rt_rng_seed(&rng, 0x9057E1A10Bull);
    for (uint32_t round = 0; round < 360u; round++) {
        uint32_t width, height, total;
        random_observation(&rng, &width, &height, &total, dense, round < 240u ? 14u : 18u);
        check_exhaustive(&env, width, height, total, dense, 4096, "random board");
        /* Every other total over the same clues: global coupling with the
         * pool and contradictions of the total. */
        uint32_t hidden = 0;
        for (uint32_t i = 0; i < width * height; i++) hidden += dense[i] < 0;
        if (round % 9u == 0 && hidden <= 14u) {
            for (uint32_t t = 0; t <= hidden; t++) {
                check_exhaustive(&env, width, height, t, dense, 4096, "random board, other total");
            }
        }
    }
    env_close(&env);
}

static void test_exhaustive_fixtures_and_thin_boards(void) {
    test_case("exhaustive: fixture observations, thin and degenerate boards");
    post_env env;
    env_open(&env, 0.0);
    for (uint32_t k = 0; k < PROBABILITY_CASE_COUNT; k++) {
        const ProbabilityCase *c = &PROBABILITY_CASES[k];
        uint32_t n = c->width * c->height;
        if (n > 64u) continue;
        CHECK(fixture_case_dense_clues(c, dense, n));
        uint32_t hidden = 0;
        for (uint32_t i = 0; i < n; i++) hidden += dense[i] < 0;
        if (hidden > ORACLE_MAX_HIDDEN) continue;
        check_exhaustive(&env, c->width, c->height, c->total_mines, dense, 4096, c->name);
        if (c->layouts.len == 1u) CHECK_EQ(the_oracle.layouts, c->layouts.limbs[0]);
    }
    /* README example ?1?1??????? (Z = 21): the limit is inclusive. */
    const ProbabilityCase *readme = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    CHECK(fixture_case_dense_clues(readme, dense, 11));
    check_exhaustive(&env, 11, 1, 3, dense, 21, "readme, limit 21");
    check_exhaustive(&env, 11, 1, 3, dense, 20, "readme, limit 20");
    /* 1x1, 1x2, 2x1 and fully revealed boards. */
    static const int8_t one_hidden[1] = {-1};
    check_exhaustive(&env, 1, 1, 0, one_hidden, 1, "1x1 empty");
    check_exhaustive(&env, 1, 1, 1, one_hidden, 1, "1x1 mine");
    static const int8_t pair[2] = {-1, 1};
    check_exhaustive(&env, 2, 1, 1, pair, 1, "2x1 forced");
    check_exhaustive(&env, 1, 2, 1, pair, 1, "1x2 forced");
    check_exhaustive(&env, 1, 2, 0, pair, 1, "1x2 contradiction");
    static const int8_t revealed[4] = {0, 0, 0, 0};
    check_exhaustive(&env, 2, 2, 0, revealed, 1, "2x2 all revealed");
    /* Thin boards: alternating 1D chains and pools. */
    for (uint32_t w = 3; w <= 21; w += 2) {
        for (uint32_t i = 0; i < w; i++) dense[i] = i % 2u ? 1 : -1;
        uint32_t hidden = (w + 1u) / 2u;
        for (uint32_t t = 0; t <= hidden; t++) {
            check_exhaustive(&env, w, 1, t, dense, 4096, "1D chain");
            check_exhaustive(&env, 1, w, t, dense, 4096, "vertical chain");
        }
    }
    env_close(&env);
}

/* The 3001-cell chain with 751 mines has exactly one layout (Z = 1); a
 * 6400x1 board without clues holds C(6400, 3) layouts. */
static void test_exhaustive_long_boards(void) {
    test_case("exhaustive: 3001-cell chain (Z = 1); 6400x1 pool (Z = C(6400, 3))");
    post_env env;
    env_open(&env, 0.0);
    size_t len = case_obs(&PROBABILITY_CASES[PROB_CASE_CHAIN_3001_EXACT], obs_buf);
    ms_posterior_info info;
    CHECK_STATUS(generate(&env, obs_buf, len, 2, 0, &info), MS_OK);
    CHECK_EQ(info.exhaustive, 1);
    CHECK_EQ(info.count, 1);
    CHECK_EQ(info.total_layouts, 1);
    for (uint32_t i = 0; i < 3001u; i++) CHECK_EQ(ROWS[i], i % 4u == 0 ? 1 : 0);
    /* The same with draws requested: still the one layout, once. */
    CHECK_STATUS(generate(&env, obs_buf, len, 1, 5, &info), MS_OK);
    CHECK_EQ(info.exhaustive, 1);
    CHECK_EQ(info.count, 1);

    for (uint32_t i = 0; i < 6400u; i++) dense[i] = -1;
    len = make_obs(obs_buf, 6400, 1, 3, dense);
    CHECK_STATUS(generate(&env, obs_buf, len, 100, 40, &info), MS_OK);
    uint64_t z = (uint64_t)6400u * 6399u * 6398u / 6u;
    CHECK_EQ(info.total_layouts, z);
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.count, 40);
    CHECK_EQ(info.reason, MS_REASON_NONE);
    check_distinct_rows(info.count, 6400);
    /* Too many layouts and no draws requested: nothing, with a reason. */
    CHECK_STATUS(generate(&env, obs_buf, len, 100, 0, &info), MS_OK);
    CHECK_EQ(info.count, 0);
    CHECK_EQ(info.exact_distribution, 1);
    CHECK_EQ(info.total_layouts, z);
    CHECK_EQ(info.reason, MS_REASON_SAMPLING_BUDGET_EXHAUSTED);
    env_close(&env);
}

/* ------------------------------------------------------- draw frequencies */

static uint32_t freq[ORACLE_MAX_LAYOUTS];

/* Counts the rows of each oracle layout (every row must be one) and returns
 * Pearson's chi-square against the uniform posterior. */
static double tally_rows(const oracle *o, uint32_t count, uint32_t n) {
    CHECK(o->stored == o->layouts && o->layouts > 0);
    for (uint32_t k = 0; k < o->stored; k++) freq[k] = 0;
    for (uint32_t r = 0; r < count; r++) {
        uint32_t at = oracle_find(o, row_mask(o, ROWS + (size_t)r * n));
        CHECK(at != UINT32_MAX);
        freq[at]++;
    }
    double expected = (double)count / (double)o->layouts;
    double chi2 = 0.0;
    for (uint32_t k = 0; k < o->stored; k++) {
        double d = (double)freq[k] - expected;
        chi2 += d * d / expected;
    }
    return chi2;
}

/* About 8 standard deviations above the mean of a chi-square with
 * categories - 1 degrees of freedom (seeds are fixed: no flakiness, but
 * per-cell or unweighted sampling lands far above). */
static double chi2_bound(uint64_t categories, double slack) {
    double dof = (double)(categories - 1u);
    double root = 1.0 + dof; /* Newton's method for sqrt(2 dof), from above */
    for (uint32_t k = 0; k < 64u; k++) root = 0.5 * (root + 2.0 * dof / root);
    return dof + 8.0 * root + 10.0 + slack;
}

/* Exact posterior draws (exact_limit 0 forces draws) are uniform over the
 * oracle's layouts. */
static void check_uniform_draws(post_env *env, uint32_t width, uint32_t height, uint32_t total,
                                const int8_t *clues, uint32_t per_layout, const char *label) {
    oracle *o = &the_oracle;
    CHECK(oracle_run(width, height, total, clues, o));
    CHECK(o->layouts >= 2u && o->layouts == o->stored);
    uint32_t draws = (uint32_t)o->layouts * per_layout;
    size_t len = make_obs(obs_buf, width, height, total, clues);
    ms_posterior_info info;
    CHECK_STATUS(generate(env, obs_buf, len, 0, draws, &info), MS_OK);
    CHECK_EQ(info.exact_distribution, 1);
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.count, draws);
    CHECK_EQ(info.total_layouts, o->layouts);
    CHECK_EQ(info.reason, MS_REASON_NONE);
    double chi2 = tally_rows(o, draws, width * height);
    if (!(chi2 < chi2_bound(o->layouts, 0.0))) print_label("non-uniform exact draws", label);
    CHECK(chi2 < chi2_bound(o->layouts, 0.0));
    for (uint32_t k = 0; k < o->stored; k++) CHECK(freq[k] > 0);
}

static void test_exact_draw_frequencies(void) {
    test_case("exact draws: uniform over complete layouts (components, coupling, pool)");
    post_env env;
    env_open(&env, 0.0);
    env_seed(&env, 0xD1CE5EEDull);
    /* README ?1?1??????? with 3 mines: b alone has C(6,2) = 15 completions,
     * a and c have C(6,1) = 6, so P(b) = 15/21 - not 1/2 as an unweighted
     * choice of the component layout, nor independent cells. */
    const ProbabilityCase *readme = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    CHECK(fixture_case_dense_clues(readme, dense, 11));
    check_uniform_draws(&env, 11, 1, 3, dense, 1000, "readme");
    uint32_t with_b = 0;
    for (uint32_t r = 0; r < 21000u; r++) with_b += ROWS[(size_t)r * 11u + 2u];
    CHECK(with_b > 14400u && with_b < 15600u); /* 15000 expected, sd ~65 */
    /* Two disconnected components and a pool, coupled only by the total. */
    static const uint32_t coupled[] = {PROB_CASE_COMPONENTS_TOTAL_2, PROB_CASE_COMPONENTS_TOTAL_3,
                                       PROB_CASE_COMPONENTS_TOTAL_4, PROB_CASE_COMPONENTS_TOTAL_5,
                                       PROB_CASE_POOL_2D_6X3, PROB_CASE_OVERLAP_121_WALL,
                                       PROB_CASE_PUBLIC_EQUIVALENCE_4X4};
    for (uint32_t k = 0; k < array_size(coupled); k++) {
        const ProbabilityCase *c = &PROBABILITY_CASES[coupled[k]];
        CHECK(fixture_case_dense_clues(c, dense, c->width * c->height));
        oracle *o = &the_oracle;
        CHECK(oracle_run(c->width, c->height, c->total_mines, dense, o));
        if (o->layouts < 2u || o->layouts > 400u) continue;
        check_uniform_draws(&env, c->width, c->height, c->total_mines, dense,
                            o->layouts > 100u ? 40u : 200u, c->name);
    }
    /* A corner clue over three cells, one mine, plus a pool of five. */
    for (uint32_t i = 0; i < 9u; i++) dense[i] = -1;
    dense[0] = 1;
    check_uniform_draws(&env, 3, 3, 3, dense, 100, "corner 3x3");
    /* Random 2D boards with moderate posteriors. */
    rt_rng rng;
    rt_rng_seed(&rng, 0xF2E9u);
    uint32_t tested = 0;
    for (uint32_t round = 0; round < 400u && tested < 12u; round++) {
        uint32_t width, height, total;
        random_observation(&rng, &width, &height, &total, dense, 14u);
        oracle *o = &the_oracle;
        CHECK(oracle_run(width, height, total, dense, o));
        if (o->layouts < 3u || o->layouts > 150u || width == 1u || height == 1u) continue;
        check_uniform_draws(&env, width, height, total, dense, 60, "random 2D board");
        tested++;
    }
    CHECK(tested == 12u);
    env_close(&env);
}

/* Draws from an approximate result: every component sampled (node budget
 * 0) or one sampled beside an exact one. The rows follow the importance-
 * weighted empirical posterior, which with many proposals is close to the
 * exact one; ignoring 2^choices or the global coupling is far off. */
static bool check_weighted_draws(post_env *env, uint32_t width, uint32_t height, uint32_t total,
                                 const int8_t *clues, uint32_t node_budget,
                                 uint32_t sample_budget, uint32_t per_layout, const char *label) {
    oracle *o = &the_oracle;
    CHECK(oracle_run(width, height, total, clues, o));
    CHECK(o->layouts >= 2u && o->layouts == o->stored);
    uint32_t draws = (uint32_t)o->layouts * per_layout;
    size_t len = make_obs(obs_buf, width, height, total, clues);
    env->limits.node_budget = node_budget;
    env->limits.sample_budget = sample_budget;
    ms_posterior_info info;
    /* exact_limit far above Z: approximate rows are never exhaustive. */
    CHECK_STATUS(generate(env, obs_buf, len, draws + 4096u, draws, &info), MS_OK);
    const ms_result_header *h = header(result_buf);
    if (h->status == MS_PROB_EXACT && h->components == h->exact_components) {
        ms_limits_default(&env->limits); /* nothing left to sample */
        return false;
    }
    CHECK_EQ(h->status, MS_PROB_APPROXIMATE);
    CHECK(h->sampled_components > 0);
    CHECK_EQ(info.exact_distribution, 0);
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.total_layouts, 0);
    CHECK_EQ(info.count, draws);
    CHECK_EQ(info.reason, MS_REASON_COUNTING_BUDGET_EXCEEDED);
    CHECK(info.effective_samples > 0.0 && info.effective_samples <= (double)sample_budget);
    double chi2 = tally_rows(o, draws, width * height);
    if (!(chi2 < chi2_bound(o->layouts, 0.002 * draws))) {
        print_label("weighted draws far from the posterior", label);
    }
    CHECK(chi2 < chi2_bound(o->layouts, 0.002 * draws));
    ms_limits_default(&env->limits);
    return true;
}

static void test_weighted_draw_frequencies(void) {
    test_case("approximate draws: occurrences * 2^choices weights and global coupling");
    post_env env;
    env_open(&env, 0.0);
    /* 2x2 board, corner clue 1, one mine: the proposal visits cell 1 first
     * (coin: 1/2), then cell 2 (coin), so it proposes {1}, {2}, {3} with
     * 1/2, 1/4, 1/4; the weights 2, 4, 4 restore 1/3 each. */
    static const int8_t corner[4] = {1, -1, -1, -1};
    env_seed(&env, 31);
    CHECK(check_weighted_draws(&env, 2, 2, 1, corner, 0, 30000, 10000, "2x2 corner"));
    for (uint32_t k = 0; k < 3u; k++) CHECK(freq[k] > 9300u && freq[k] < 10700u);
    CHECK_EQ(header(result_buf)->samples, 30000); /* no proposal dead-ends here */
    CHECK_EQ(header(result_buf)->sample_attempts, 30000);
    /* The README chain sampled: proposals are fair between {b} and {a, c};
     * only the pool weights C(6, 2) : C(6, 1) give P(b) = 15/21. */
    const ProbabilityCase *readme = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    CHECK(fixture_case_dense_clues(readme, dense, 11));
    env_seed(&env, 32);
    CHECK(check_weighted_draws(&env, 11, 1, 3, dense, 0, 20000, 500, "readme sampled"));
    uint32_t with_b = 0;
    for (uint32_t r = 0; r < 10500u; r++) with_b += ROWS[(size_t)r * 11u + 2u];
    CHECK(with_b > 7100u && with_b < 7900u); /* 7500 expected */
    /* Random consistent 2D boards, every component sampled: corners and
     * edges give unequal proposals, varying mine counts couple the pool. */
    rt_rng rng;
    rt_rng_seed(&rng, 0x5A3D1Eu);
    oracle *o = &the_oracle;
    uint32_t tested = 0;
    for (uint32_t round = 0; round < 600u && tested < 8u; round++) {
        uint32_t width, height, total;
        random_observation(&rng, &width, &height, &total, dense, 14u);
        CHECK(oracle_run(width, height, total, dense, o));
        if (o->layouts < 3u || o->layouts > 120u || width == 1u || height == 1u) continue;
        env_seed(&env, 40u + round);
        if (!check_weighted_draws(&env, width, height, total, dense, 0, 40000, 150,
                                  "random 2D board")) {
            continue;
        }
        tested++;
    }
    CHECK(tested == 8u);
    /* One exact component beside a sampled one (node budget for the first):
     * ?1?1???1?1?? with every total. */
    for (uint32_t total = 2; total <= 6u; total++) {
        const ProbabilityCase *c = &PROBABILITY_CASES[PROB_CASE_COMPONENTS_TOTAL_2 + (total - 2u)];
        CHECK_EQ(c->total_mines, total);
        CHECK(fixture_case_dense_clues(c, dense, 12));
        size_t len = make_obs(obs_buf, 12, 1, total, dense);
        /* The least node budget that counts one component and samples the
         * other (component order and kcap make it depend on the total). */
        uint32_t split = 0;
        for (uint32_t nodes = 1; nodes < 64u && split == 0; nodes++) {
            ms_limits_default(&env.limits);
            env.limits.node_budget = nodes;
            CHECK_STATUS(ms_solve(obs_buf, len, &env.limits, &env.mem, &env.clock, result_buf,
                                  result_len_of(obs_buf)),
                         MS_OK);
            const ms_result_header *h = header(result_buf);
            if (h->exact_components == 1u && h->sampled_components == 1u) split = nodes;
        }
        CHECK(split > 0);
        CHECK(oracle_run(12, 1, total, dense, o));
        if (o->layouts < 2u) continue;
        env_seed(&env, 50u + total);
        CHECK(check_weighted_draws(&env, 12, 1, total, dense, split, 20000, 1000, c->name));
        CHECK_EQ(header(result_buf)->exact_components, 1);
        CHECK_EQ(header(result_buf)->sampled_components, 1);
    }
    env_close(&env);
}

/* ------------------------------------------- complete belief fixtures */

/* Complete posteriors from the parent's independent belief oracle (5x4).
 * TEN, 4 mines ('?' hidden):
 *   ?????   x13 + x18 = 1, x5 + x6 = x6 + x8 = x8 + x9 = 1 and
 *   ??2??   x1 + x2 + x3 = 1 - x13; the pool {0, 4} holds x13 mines:
 *   112?2   3 * 2 layouts without a mine on 13, 2 * 2 with one, Z = 10.
 *   001?1
 * ROOT, 3 mines: a 1 on cells 1 and 10, overlapping on {5, 6}. A mine
 * there leaves two for the 10-cell pool (2 * C(10, 2) = 90 layouts);
 * otherwise one of {0, 2, 7} and one of {11, 15, 16} leave one
 * (9 * C(10, 1) = 90): Z = 180, and half of the layouts come from 2 of
 * the 11 component layouts (pool weights, never per component layout). */
#define BELIEF_CELLS 20u
static const int8_t TEN_CLUES[BELIEF_CELLS] = {-1, -1, -1, -1, -1, -1, -1, 2,  -1, -1,
                                               1,  1,  2,  -1, 2,  0,  0,  1,  -1, 1};
static const uint32_t TEN_MINED[BELIEF_CELLS] = {2, 2, 2, 2, 2, 5, 5, 0, 5, 5,
                                                 0, 0, 0, 4, 0, 0, 0, 0, 6, 0};
static const int8_t ROOT_CLUES[BELIEF_CELLS] = {-1, 1,  -1, -1, -1, -1, -1, -1, -1, -1,
                                                1,  -1, -1, -1, -1, -1, -1, -1, -1, -1};
static const uint32_t ROOT_MINED[BELIEF_CELLS] = {30, 0,  30, 27, 27, 45, 45, 30, 27, 27,
                                                  0,  30, 27, 27, 27, 30, 30, 27, 27, 27};

/* Mines per cell over rows [0, count) of a 5x4 board. */
static void count_belief_mines(uint32_t count, uint32_t *mined) {
    for (uint32_t v = 0; v < BELIEF_CELLS; v++) mined[v] = 0;
    for (uint32_t r = 0; r < count; r++) {
        const uint8_t *row = ROWS + (size_t)r * BELIEF_CELLS;
        for (uint32_t v = 0; v < BELIEF_CELLS; v++) mined[v] += row[v];
    }
}

/* Rows of the ROOT board with a mine on the overlap {5, 6}. */
static uint32_t root_overlap_rows(uint32_t count) {
    uint32_t rows = 0;
    for (uint32_t r = 0; r < count; r++) {
        const uint8_t *row = ROWS + (size_t)r * BELIEF_CELLS;
        rows += (row[5] | row[6]) != 0;
    }
    return rows;
}

/* Exhaustive output: Z distinct complete layouts (hence every layout once)
 * with the oracle's mine count on every cell and the exact probabilities. */
static void check_belief_set(post_env *env, const int8_t *clues, uint32_t total, uint32_t z,
                             const uint32_t *mined, uint32_t exact_limit, uint32_t sample_count) {
    size_t len = make_obs(obs_buf, 5, 4, total, clues);
    ms_posterior_info info;
    CHECK_STATUS(generate(env, obs_buf, len, exact_limit, sample_count, &info), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_EXACT);
    CHECK_EQ(info.exhaustive, 1);
    CHECK_EQ(info.exact_distribution, 1);
    CHECK_EQ(info.count, z);
    CHECK_EQ(info.total_layouts, z);
    CHECK_EQ(info.reason, MS_REASON_NONE);
    CHECK_EQ(f64_bits(info.effective_samples), f64_bits(0.0));
    check_distinct_rows(z, BELIEF_CELLS);
    uint32_t got[BELIEF_CELLS];
    count_belief_mines(z, got);
    for (uint32_t v = 0; v < BELIEF_CELLS; v++) {
        CHECK_EQ(got[v], mined[v]);
        if (clues[v] < 0) CHECK(prob_at(result_buf, v) == (double)mined[v] / (double)z);
    }
}

/* Draw marginals within `slack` rows of mined[v] * count / Z. */
static void check_belief_marginals(uint32_t count, uint32_t z, const uint32_t *mined,
                                   double slack) {
    uint32_t got[BELIEF_CELLS];
    count_belief_mines(count, got);
    for (uint32_t v = 0; v < BELIEF_CELLS; v++) {
        CHECK_NEAR((double)got[v], (double)mined[v] * (double)count / (double)z, slack);
    }
}

static void test_complete_belief_fixtures(void) {
    test_case("complete belief fixtures: 10 and 180 equally likely layouts, pool weights");
    post_env env;
    env_open(&env, 0.0);
    /* The limit is inclusive; 256 and 96 are the planner's defaults. */
    check_belief_set(&env, TEN_CLUES, 4, 10, TEN_MINED, 10, 0);
    check_belief_set(&env, TEN_CLUES, 4, 10, TEN_MINED, 256, 96);
    check_belief_set(&env, ROOT_CLUES, 3, 180, ROOT_MINED, 180, 0);
    check_belief_set(&env, ROOT_CLUES, 3, 180, ROOT_MINED, 256, 96);
    CHECK_EQ(root_overlap_rows(180), 90);
    /* One below Z: draws, never a partial enumeration. */
    ms_posterior_info info;
    size_t len = make_obs(obs_buf, 5, 4, 3, ROOT_CLUES);
    CHECK_STATUS(generate(&env, obs_buf, len, 179, 96, &info), MS_OK);
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.count, 96);
    CHECK_EQ(info.total_layouts, 180);
    CHECK_EQ(info.reason, MS_REASON_NONE);
    len = make_obs(obs_buf, 5, 4, 4, TEN_CLUES);
    CHECK_STATUS(generate(&env, obs_buf, len, 9, 0, &info), MS_OK);
    CHECK_EQ(info.count, 0);
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.total_layouts, 10);
    CHECK_EQ(info.reason, MS_REASON_SAMPLING_BUDGET_EXHAUSTED);

    /* Exact draws: uniform over the layouts (chi-square, every layout seen)
     * and marginals within about 8 standard deviations. A uniform choice of
     * the component layout would mine {5, 6} in 2/11 of the ROOT draws. */
    env_seed(&env, 0xB0A2Dull);
    check_uniform_draws(&env, 5, 4, 4, TEN_CLUES, 2000, "ten-layout fixture");
    check_belief_marginals(20000, 10, TEN_MINED, 600.0);
    check_uniform_draws(&env, 5, 4, 3, ROOT_CLUES, 200, "180-layout fixture");
    check_belief_marginals(36000, 180, ROOT_MINED, 700.0);
    CHECK_NEAR((double)root_overlap_rows(36000), 18000.0, 800.0);

    /* Every component sampled: occurrences * 2^choices and the pool weights
     * restore the same posterior (ignoring 2^choices gives the overlap
     * about 0.39, ignoring the pool 2/11). */
    env_seed(&env, 0x5A3Dull);
    CHECK(check_weighted_draws(&env, 5, 4, 4, TEN_CLUES, 0, 40000, 400, "ten-layout, sampled"));
    check_belief_marginals(4000, 10, TEN_MINED, 4000.0 * 0.08);
    env_seed(&env, 0x5A3Eull);
    CHECK(check_weighted_draws(&env, 5, 4, 3, ROOT_CLUES, 0, 40000, 50, "180-layout, sampled"));
    check_belief_marginals(9000, 180, ROOT_MINED, 9000.0 * 0.05);
    CHECK_NEAR((double)root_overlap_rows(9000), 4500.0, 9000.0 * 0.05);
    env_close(&env);
}

/* ----------------------------------------------------------- huge boards */

static void test_huge_boards(void) {
    test_case("80x80 and 6000x1: exact draws when Z is far beyond 64 bits");
    post_env env;
    env_open(&env, 0.0);
    ms_posterior_info info;
    /* One corner clue 1, 2000 mines: Z = 3 C(6396, 1999). */
    size_t len = case_obs(&PROBABILITY_CASES[PROB_CASE_CORNER_80X80_2000], obs_buf);
    env_seed(&env, 2000);
    CHECK_STATUS(generate(&env, obs_buf, len, 16, 96, &info), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_EXACT);
    CHECK_EQ(info.exact_distribution, 1);
    CHECK_EQ(info.total_layouts, 0); /* not representable */
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.count, 96);
    CHECK_EQ(info.reason, MS_REASON_NONE);
    check_distinct_rows(96, 6400);
    uint32_t corner[3] = {0, 0, 0}, first_pool = 0;
    for (uint32_t r = 0; r < 96u; r++) {
        const uint8_t *row = ROWS + (size_t)r * 6400u;
        corner[0] += row[1];
        corner[1] += row[80];
        corner[2] += row[81];
        for (uint32_t i = 2; i < 80u; i++) first_pool += row[i]; /* 78 pool cells */
    }
    for (uint32_t k = 0; k < 3u; k++) CHECK(corner[k] >= 14u && corner[k] <= 50u); /* 32 */
    /* Hypergeometric: 96 * 78 * 1999/6396 = 2340 mines expected, sd ~ 42. */
    CHECK(first_pool > 2100u && first_pool < 2580u);

    /* Eight 1s around cell 3240, 4 mines: Z = C(6375, 3) + 2 < 2^64. */
    len = case_obs(&PROBABILITY_CASES[PROB_CASE_NEAR_CERTAIN_80X80], obs_buf);
    CHECK_STATUS(generate(&env, obs_buf, len, 64, 48, &info), MS_OK);
    CHECK_EQ(info.total_layouts, (uint64_t)6375u * 6374u * 6373u / 6u + 2u);
    CHECK_EQ(info.count, 48);
    for (uint32_t r = 0; r < 48u; r++) CHECK_EQ(ROWS[(size_t)r * 6400u + 3240u], 1);

    /* The empty 80x80 prior with 3200 mines: no component at all. */
    len = case_obs(&PROBABILITY_CASES[PROB_CASE_PRIOR_80X80_HALF], obs_buf);
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 32, &info), MS_OK);
    CHECK_EQ(info.count, 32);
    CHECK_EQ(info.total_layouts, 0);
    check_distinct_rows(32, 6400);
    uint32_t top_half = 0;
    for (uint32_t r = 0; r < 32u; r++) {
        for (uint32_t i = 0; i < 3200u; i++) top_half += ROWS[(size_t)r * 6400u + i];
    }
    CHECK(top_half > 32u * 1560u && top_half < 32u * 1640u); /* 1600 per row, sd ~20 */

    /* 1000 components '?1?1??' coupled only by the total (1700 mines): the
     * product tree over 1000 histograms. Ends of a block are mines exactly
     * when it holds two mines, with the exact probability of the result. */
    for (uint32_t i = 0; i < 6000u; i++) dense[i] = (i % 6u == 1u || i % 6u == 3u) ? 1 : -1;
    len = make_obs(obs_buf, 6000, 1, 1700, dense);
    env_seed(&env, 1000);
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 40, &info), MS_OK);
    CHECK_EQ(header(result_buf)->components, 1000);
    CHECK_EQ(info.count, 40);
    double p_two = prob_at(result_buf, 0);
    uint32_t two = 0;
    for (uint32_t r = 0; r < 40u; r++) {
        const uint8_t *row = ROWS + (size_t)r * 6000u;
        for (uint32_t b = 0; b < 1000u; b++) {
            CHECK_EQ(row[6u * b], row[6u * b + 4u]);
            CHECK(row[6u * b] + row[6u * b + 2u] == 1u);
            two += row[6u * b];
        }
    }
    double observed = (double)two / 40000.0;
    CHECK(observed > p_two - 0.03 && observed < p_two + 0.03);
    env_close(&env);
}

/* Played 80x80 boards (many components, exact) and the ~3000-cell lattice
 * (sampled within the default budgets): rows agree with every clue, the
 * total and every proof, and the generating layout is one of the layouts
 * they are drawn from. */
static void test_played_boards(void) {
    test_case("played 80x80 boards and the 80x80 lattice with default limits");
    for (uint32_t k = 0; k < LARGE_BOARD_CASE_COUNT; k++) {
        const ProbabilityCase *c = &LARGE_BOARD_CASES[k];
        post_env env;
        env_open(&env, 0.0);
        size_t len = case_obs(c, obs_buf);
        for (uint32_t i = 0; i < 6400u; i++) {
            ROWS_REF[i] = fixture_case_layout_mine(c, i) ? 1u : 0u;
        }
        CHECK(layout_ok(obs_buf, ROWS_REF));
        ms_posterior_info info;
        CHECK_STATUS(generate(&env, obs_buf, len, 128, 96, &info), MS_OK);
        const ms_result_header *h = header(result_buf);
        if (h->status == MS_PROB_UNAVAILABLE) {
            CHECK_EQ(info.count, 0);
        } else {
            CHECK_EQ(info.count, info.exhaustive ? info.total_layouts : 96u);
            CHECK(info.exhaustive || info.count == 96u);
        }
        env_close(&env);
    }
    /* A played board with node budgets too small for every component: draws
     * combine exact and sampled components (37 of them) and the pool. */
    uint32_t approximate = 0;
    static const uint32_t node_budgets[] = {30, 100, 300};
    for (uint32_t b = 0; b < array_size(node_budgets); b++) {
        post_env env;
        env_open(&env, 0.0);
        size_t len = case_obs(&LARGE_BOARD_CASES[LARGE_CASE_PLAYED_80X80_SEED1], obs_buf);
        env.limits.node_budget = node_budgets[b];
        env.limits.sample_budget = 40000;
        env_seed(&env, 100u + b);
        ms_posterior_info info;
        CHECK_STATUS(generate(&env, obs_buf, len, 128, 96, &info), MS_OK);
        const ms_result_header *h = header(result_buf);
        if (h->status == MS_PROB_APPROXIMATE && h->exact_components > 0) {
            approximate++;
            CHECK_EQ(info.count, 96);
            CHECK_EQ(info.exact_distribution, 0);
            CHECK_EQ(info.reason, MS_REASON_COUNTING_BUDGET_EXCEEDED);
        }
        env_close(&env);
    }
    CHECK(approximate >= 2u);
}

/* --------------------------------------------------- determinism, seeds */

#define DIGEST_README_DRAWS 0xb0aadcef279c8ec3ull
#define DIGEST_CORNER_80X80 0x3db8bcbd760e691dull
#define DIGEST_SAMPLED_CORNER 0xf06bd3fd44057d4bull
#define DIGEST_MIXED_FIXTURE 0xa36ee5db579ed1ebull
#define DIGEST_ENUMERATION 0x2689821d8478d38aull

static void test_determinism_and_digests(void) {
    test_case("seeded rows are reproducible and bit-identical on every runtime");
    post_env env;
    env_open(&env, 0.0);
    ms_posterior_info info, again;
    const ProbabilityCase *readme = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    size_t len = case_obs(readme, obs_buf);
    size_t rlen = result_len_of(obs_buf);
    env_seed(&env, 7);
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 64, &info), MS_OK);
    check_digest("readme draws", output_digest(obs_buf, &info), DIGEST_README_DRAWS);
    base_memcpy(ROWS_REF, ROWS, 64u * 11u);
    base_memcpy(result_ref, result_buf, rlen);
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 64, &again), MS_OK);
    CHECK(base_memcmp(ROWS_REF, ROWS, 64u * 11u) == 0);
    CHECK(base_memcmp(result_ref, result_buf, rlen) == 0);
    CHECK(base_memcmp(&info, &again, sizeof(info)) == 0);
    env_seed(&env, 8);
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 64, &again), MS_OK);
    CHECK(base_memcmp(ROWS_REF, ROWS, 64u * 11u) != 0);
    /* The default seed is the observation's hash. */
    env.limits.flags = 0;
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 64, &info), MS_OK);
    base_memcpy(ROWS_REF, ROWS, 64u * 11u);
    env_seed(&env, ms_obs_hash(obs_buf));
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 64, &again), MS_OK);
    CHECK(base_memcmp(ROWS_REF, ROWS, 64u * 11u) == 0);
    /* Enumeration does not depend on the seed: rank order. */
    env_seed(&env, 1);
    CHECK_STATUS(generate(&env, obs_buf, len, 21, 3, &info), MS_OK);
    CHECK_EQ(info.exhaustive, 1);
    check_digest("readme enumeration", output_digest(obs_buf, &info), DIGEST_ENUMERATION);
    base_memcpy(ROWS_REF, ROWS, 21u * 11u);
    env_seed(&env, 2);
    CHECK_STATUS(generate(&env, obs_buf, len, 21, 3, &info), MS_OK);
    CHECK(base_memcmp(ROWS_REF, ROWS, 21u * 11u) == 0);

    len = case_obs(&PROBABILITY_CASES[PROB_CASE_CORNER_80X80_2000], obs_buf);
    env_seed(&env, 11);
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 16, &info), MS_OK);
    check_digest("corner 80x80 draws", output_digest(obs_buf, &info), DIGEST_CORNER_80X80);

    static const int8_t corner[4] = {1, -1, -1, -1};
    len = make_obs(obs_buf, 2, 2, 1, corner);
    env_seed(&env, 5);
    env.limits.node_budget = 0;
    env.limits.sample_budget = 4000;
    CHECK_STATUS(generate(&env, obs_buf, len, 0, 64, &info), MS_OK);
    CHECK_EQ(info.exact_distribution, 0);
    check_digest("sampled corner draws", output_digest(obs_buf, &info), DIGEST_SAMPLED_CORNER);

    const SamplingCase *sc = &SAMPLING_CASES[SAMPLING_CASE_MIXED_EXACT_AND_SAMPLED];
    ms_limits_default(&env.limits);
    env.limits.node_budget = sc->node_budget;
    env.limits.sample_budget = sc->sample_budget;
    len = case_obs(sc->observation, obs_buf);
    CHECK_STATUS(generate(&env, obs_buf, len, 8, 64, &info), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_APPROXIMATE);
    CHECK_EQ(info.count, 64);
    check_digest("mixed fixture draws", output_digest(obs_buf, &info), DIGEST_MIXED_FIXTURE);
    env_close(&env);
}

/* ------------------------------------------ statuses and approximations */

static void test_unavailable_and_approximate(void) {
    test_case("unavailable results give no rows; sampled rows are never exhaustive");
    post_env env;
    env_open(&env, 0.0);
    ms_posterior_info info;
    const ProbabilityCase *readme = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    size_t len = case_obs(readme, obs_buf);
    /* No counting and no sampling budget. */
    env.limits.node_budget = 0;
    env.limits.sample_budget = 0;
    CHECK_STATUS(generate(&env, obs_buf, len, 64, 64, &info), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_UNAVAILABLE);
    CHECK_EQ(info.reason, MS_REASON_SAMPLING_BUDGET_EXHAUSTED);
    CHECK_EQ(info.count, 0);
    /* Too few effective samples. */
    env.limits.sample_budget = 2000;
    env.limits.min_effective_samples = 1e9;
    CHECK_STATUS(generate(&env, obs_buf, len, 64, 64, &info), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_UNAVAILABLE);
    CHECK_EQ(info.reason, MS_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES);
    CHECK_EQ(info.count, 0);
    CHECK(info.effective_samples > 0.0);
    /* Sampled, with exact_limit far above the 21 layouts: draws only. */
    env.limits.min_effective_samples = MS_DEFAULT_MIN_EFFECTIVE_SAMPLES;
    CHECK_STATUS(generate(&env, obs_buf, len, 1000, 50, &info), MS_OK);
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.exact_distribution, 0);
    CHECK_EQ(info.count, 50);
    /* No draws requested: nothing, though every layout was proposed. */
    CHECK_STATUS(generate(&env, obs_buf, len, 1000, 0, &info), MS_OK);
    CHECK_EQ(info.count, 0);
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.reason, MS_REASON_SAMPLING_BUDGET_EXHAUSTED);

    /* chain_3001_sampled: one globally compatible layout. Every row is the
     * same layout, yet nothing is exhaustive or exact. */
    const SamplingCase *sc = &SAMPLING_CASES[SAMPLING_CASE_CHAIN_3001_SAMPLED];
    ms_limits_default(&env.limits);
    env.limits.node_budget = sc->node_budget;
    env.limits.sample_budget = sc->sample_budget;
    len = case_obs(sc->observation, obs_buf);
    CHECK_STATUS(generate(&env, obs_buf, len, 100, 6, &info), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_APPROXIMATE);
    CHECK_EQ(info.exhaustive, 0);
    CHECK_EQ(info.exact_distribution, 0);
    CHECK_EQ(info.total_layouts, 0);
    CHECK_EQ(info.count, 6);
    for (uint32_t r = 0; r < 6u; r++) {
        for (uint32_t i = 0; i < 3001u; i++) CHECK_EQ(ROWS[(size_t)r * 3001u + i], i % 4u == 0);
    }

    /* Globally incompatible samples: unavailable, never inconsistent. */
    sc = &SAMPLING_CASES[SAMPLING_CASE_GLOBALLY_INCOMPATIBLE];
    ms_limits_default(&env.limits);
    env.limits.node_budget = sc->node_budget;
    env.limits.sample_budget = sc->sample_budget;
    len = case_obs(sc->observation, obs_buf);
    uint32_t incompatible = 0;
    for (uint64_t seed = 1; seed <= 8u; seed++) {
        env_seed(&env, seed);
        CHECK_STATUS(generate(&env, obs_buf, len, 4, 4, &info), MS_OK);
        CHECK_EQ(header(result_buf)->status, MS_PROB_UNAVAILABLE);
        CHECK_EQ(info.count, 0);
        incompatible += info.reason == MS_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES;
    }
    CHECK(incompatible >= 7u);

    /* A proven contradiction stays an error. */
    ms_limits_default(&env.limits);
    len = case_obs(&PROBABILITY_CASES[PROB_CASE_INCONSISTENT_TOTAL_UNREACHABLE], obs_buf);
    CHECK_STATUS(generate(&env, obs_buf, len, 4, 4, &info), MS_ERR_INCONSISTENT);
    len = case_obs(&PROBABILITY_CASES[PROB_CASE_INCONSISTENT_TOO_MANY_MINES], obs_buf);
    CHECK_STATUS(generate(&env, obs_buf, len, 4, 4, &info), MS_ERR_INCONSISTENT);
    env_close(&env);
}

/* prob_result is ms_solve's result (fake clocks: budgets never bind). */
static void test_result_matches_solve(void) {
    test_case("prob_result is byte-identical to ms_solve's result");
    static const uint32_t cases[] = {PROB_CASE_README_WEIGHTING, PROB_CASE_COMPONENTS_TOTAL_4,
                                     PROB_CASE_CHAIN_3001_EXACT, PROB_CASE_CORNER_80X80_2000,
                                     PROB_CASE_NEAR_CERTAIN_80X80, PROB_CASE_RANDOM_SEED_21,
                                     PROB_CASE_FORCED_CELLS_4X1, PROB_CASE_PRIOR_3X3_ALL};
    for (uint32_t pass = 0; pass < 2u; pass++) {
        for (uint32_t k = 0; k < array_size(cases); k++) {
            post_env env;
            env_open(&env, 0.0);
            if (pass == 1u) {
                env.limits.node_budget = 3; /* mostly sampled */
                env_seed(&env, 77);
            }
            size_t len = case_obs(&PROBABILITY_CASES[cases[k]], obs_buf);
            size_t rlen = result_len_of(obs_buf);
            CHECK_STATUS(ms_solve(obs_buf, len, &env.limits, &env.mem, &env.clock, result_ref,
                                  rlen),
                         MS_OK);
            ms_posterior_info info;
            CHECK_STATUS(generate(&env, obs_buf, len, 4, 4, &info), MS_OK);
            CHECK(base_memcmp(result_ref, result_buf, rlen) == 0);
            env_close(&env);
        }
    }
}

/* ------------------------------------------------ deadlines and memory */

/* A clock reading 0 until `at` readings were taken, then `later`. */
typedef struct jump_clock {
    uint32_t reads;
    uint32_t at;
    double later;
} jump_clock;

static double jump_read(void *ctx) {
    jump_clock *jump = (jump_clock *)ctx;
    return jump->reads++ < jump->at ? 0.0 : jump->later;
}

/* How one request is set up (observation and limits). */
typedef struct post_case {
    const char *name;
    uint32_t width;
    uint32_t height;
    uint32_t total;
    const int8_t *clues;
    uint32_t node_budget;
    uint32_t sample_budget;
    uint32_t exact_limit;
    uint32_t sample_count;
    double time_budget_ms;
} post_case;

static int8_t pairs_board[3200];

/* 3200x1 with eight '?1?' pairs and 8 mines: one mine per pair, none in
 * the 3176-cell pool, Z = 256 long rows (a slow enumeration). */
static const int8_t *make_pairs_board(void) {
    for (uint32_t i = 0; i < 3200u; i++) pairs_board[i] = -1;
    for (uint32_t p = 0; p < 8u; p++) pairs_board[400u * p + 201u] = 1;
    return pairs_board;
}

static size_t post_case_open(post_env *env, const post_case *pc) {
    env->limits.node_budget = pc->node_budget;
    env->limits.sample_budget = pc->sample_budget;
    env->limits.time_budget_ms = pc->time_budget_ms;
    env_seed(env, 0xC0FFEEull);
    return make_obs(obs_buf, pc->width, pc->height, pc->total, pc->clues);
}

/* The reference answer of a case with time standing still. */
static void post_case_reference(const post_case *pc, ms_posterior_info *info) {
    post_env env;
    env_open(&env, 0.0);
    size_t len = post_case_open(&env, pc);
    CHECK_STATUS(generate(&env, obs_buf, len, pc->exact_limit, pc->sample_count, info), MS_OK);
    base_memcpy(ROWS_REF, ROWS, rows_len(obs_buf, pc->exact_limit, pc->sample_count));
    base_memcpy(result_ref, result_buf, result_len_of(obs_buf));
    env_close(&env);
}

static bool same_as_reference(const ms_posterior_info *info, const ms_posterior_info *ref,
                              const post_case *pc) {
    return base_memcmp(info, ref, sizeof(*info)) == 0 &&
           base_memcmp(ROWS, ROWS_REF, rows_len(obs_buf, pc->exact_limit, pc->sample_count)) == 0 &&
           base_memcmp(result_buf, result_ref, result_len_of(obs_buf)) == 0;
}

#define DRAW_REF_BYTES ((size_t)1 << 18)
static uint64_t draw_ref_words[DRAW_REF_BYTES / 8u];
#define DRAW_REF ((uint8_t *)draw_ref_words)

/* The draws of the same request with exact_limit 0 and time standing still.
 * An enumeration consumes no random numbers, so the draws replacing an
 * unfinished one must be exactly these. */
static void post_case_draws(const post_case *pc) {
    post_env env;
    env_open(&env, 0.0);
    size_t len = post_case_open(&env, pc);
    ms_posterior_info info;
    CHECK_STATUS(generate(&env, obs_buf, len, 0, pc->sample_count, &info), MS_OK);
    CHECK_EQ(header(result_buf)->status, MS_PROB_EXACT);
    CHECK_EQ(info.count, pc->sample_count);
    size_t bytes = (size_t)info.count * cells_of(obs_buf);
    CHECK(bytes <= DRAW_REF_BYTES);
    base_memcpy(DRAW_REF, ROWS, bytes);
    env_close(&env);
}

/* Deadline injection: the clock jumps to `later` at each reading in turn
 * (every `stride`-th). Each answer keeps every invariant, and:
 * - exact rows other than an enumeration are always a prefix of the
 *   uninterrupted draws: the reference's own or, when the reference
 *   enumerated, those exact_limit 0 makes. An unfinished enumeration is
 *   discarded, never kept as rows or relabelled as draws;
 * - a jump past the whole budget (after the entry reading) ends the call
 *   within a few readings, before every requested row is complete;
 * - once the jump comes after the last reading the answer is the reference.
 * Returns a mask of outcomes: 1 cut draws, 2 discarded enumeration
 * replaced by complete draws, 4 unavailable inference, 8 exhaustive,
 * 16 draws cut after some completed rows (the clock is read within). */
static uint32_t deadline_sweep(const post_case *pc, double later, uint32_t stride) {
    ms_posterior_info ref;
    post_case_reference(pc, &ref);
    if (ref.exhaustive && pc->sample_count > 0) post_case_draws(pc);
    bool past_budget = later > pc->time_budget_ms;
    uint32_t seen = 0;
    for (uint32_t at = 0;; at += stride) {
        post_env env;
        env_open(&env, 0.0);
        jump_clock jump = {0, at, later};
        rt_clock_init(&env.clock, jump_read, &jump);
        size_t len = post_case_open(&env, pc);
        size_t n = cells_of(obs_buf);
        ms_posterior_info info;
        CHECK_STATUS(generate(&env, obs_buf, len, pc->exact_limit, pc->sample_count, &info), MS_OK);
        const ms_result_header *h = header(result_buf);
        bool finished = jump.reads <= at;
        if (h->status == MS_PROB_UNAVAILABLE) seen |= 4u;
        if (info.exhaustive) seen |= 8u;
        if (h->status != MS_PROB_UNAVAILABLE && !info.exhaustive) {
            if (info.count < pc->sample_count) seen |= 1u;
            if (info.count > 0 && info.count < pc->sample_count) seen |= 16u;
            if (ref.exhaustive && h->status == MS_PROB_EXACT) {
                /* Enumeration due but not delivered: replaced, never partial. */
                CHECK_EQ(info.reason, MS_REASON_TIME_BUDGET_EXHAUSTED);
                if (pc->sample_count > 0 && info.count == pc->sample_count) seen |= 2u;
            }
        }
        if (h->status == MS_PROB_EXACT && !info.exhaustive) {
            const uint8_t *want = ref.exhaustive ? DRAW_REF : ROWS_REF;
            CHECK(base_memcmp(ROWS, want, info.count * n) == 0);
        }
        if (!finished && at > 0 && past_budget) {
            CHECK(jump.reads - at <= 16u);
            CHECK(!info.exhaustive && info.count < pc->sample_count);
        }
        env_close(&env);
        if (finished) {
            CHECK(same_as_reference(&info, &ref, pc));
            break;
        }
    }
    return seen;
}

static void test_deadlines(void) {
    test_case("deadline injection: completed rows only, enumeration never partial");
    const ProbabilityCase *readme = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    CHECK(fixture_case_dense_clues(readme, dense, 11));
    static int8_t readme_clues[11];
    base_memcpy(readme_clues, dense, sizeof(readme_clues));
    static const post_case enumerate = {"readme enumeration", 11, 1, 3, readme_clues,
                                        MS_DEFAULT_NODE_BUDGET, MS_DEFAULT_SAMPLE_BUDGET, 64, 5,
                                        1000.0};
    CHECK(deadline_sweep(&enumerate, 1e9, 1) & 4u);
    /* Long rows: the clock is read within the draws. */
    make_pairs_board();
    static const post_case draws = {"pairs draws", 3200, 1, 8, pairs_board, MS_DEFAULT_NODE_BUDGET,
                                    MS_DEFAULT_SAMPLE_BUDGET, 0, 40, 1000.0};
    CHECK(deadline_sweep(&draws, 1e9, 1) & 16u);
    /* A clock jumping to 60% of the budget during the enumeration passes its
     * midpoint deadline but not the end: the rows enumerated so far are
     * discarded and complete exact draws replace them. */
    static const post_case fallback = {"pairs enumeration", 3200, 1, 8, pairs_board,
                                       MS_DEFAULT_NODE_BUDGET, MS_DEFAULT_SAMPLE_BUDGET, 256, 24,
                                       1000.0};
    uint32_t seen = deadline_sweep(&fallback, 600.0, 1);
    CHECK(seen & 2u);
    CHECK(seen & 8u);
    /* Approximate draws (sampled components) cut by the deadline. */
    static const int8_t corner[4] = {1, -1, -1, -1};
    static const post_case sampled = {"sampled corner", 2, 2, 1, corner, 0, 200, 0, 2000, 1000.0};
    CHECK(deadline_sweep(&sampled, 1e9, 1) & 16u);
    /* Huge Z = 3 C(6396, 1999) (~5700-bit ranks): rank generation, the
     * component walk and every row are metered, so a jump past the budget
     * ends the draws at the next reading with completed rows only. */
    static int8_t corner_clues[6400];
    CHECK(fixture_case_dense_clues(&PROBABILITY_CASES[PROB_CASE_CORNER_80X80_2000], corner_clues,
                                   6400));
    static const post_case huge = {"corner 80x80 draws", 80, 80, 2000, corner_clues,
                                   MS_DEFAULT_NODE_BUDGET, MS_DEFAULT_SAMPLE_BUDGET, 0, 24, 1000.0};
    CHECK(deadline_sweep(&huge, 1e9, 1) & 16u);
    /* A real step clock: every call ends within its budget plus one meter
     * interval of work, and only completed rows count. */
    for (uint32_t b = 0; b < 12u; b++) {
        post_env env;
        env_open(&env, 0.05);
        size_t len = post_case_open(&env, &draws);
        env.limits.time_budget_ms = (double)b * 2.0;
        double start = env.fake.now;
        ms_posterior_info info;
        CHECK_STATUS(generate(&env, obs_buf, len, 0, 40, &info), MS_OK);
        CHECK(env.fake.now - start <= (double)b * 2.0 + 5.0);
        env_close(&env);
    }
}

static void test_memory_budgets(void) {
    test_case("memory budgets: shortfalls are reasons, never errors or partial enumerations");
    static const uint64_t budgets[] = {0, 1, 4096, 16384, 65536, 131072, 262144, 524288,
                                       1048576, 4194304, 16777216};
    make_pairs_board();
    for (uint32_t i = 0; i < 6000u; i++) dense[i] = (i % 6u == 1u || i % 6u == 3u) ? 1 : -1;
    static const post_case cases[] = {
        {"pairs enumeration", 3200, 1, 8, pairs_board, MS_DEFAULT_NODE_BUDGET, 2000, 256, 8, 1e9},
        {"1000 components", 6000, 1, 1700, dense, MS_DEFAULT_NODE_BUDGET, 2000, 0, 16, 1e9},
        {"pairs sampled", 3200, 1, 8, pairs_board, 0, 2000, 0, 16, 1e9},
    };
    uint32_t short_rows = 0;
    for (uint32_t k = 0; k < array_size(cases); k++) {
        for (uint32_t b = 0; b < array_size(budgets); b++) {
            post_env env;
            env_open(&env, 0.0);
            size_t len = post_case_open(&env, &cases[k]);
            env.limits.memory_budget_bytes = budgets[b];
            ms_posterior_info info;
            CHECK_STATUS(generate(&env, obs_buf, len, cases[k].exact_limit, cases[k].sample_count,
                                  &info),
                         MS_OK);
            CHECK(env.mem.peak <= budgets[b] + RT_MEM_CHUNK_BLOCK || budgets[b] == 0);
            if (header(result_buf)->status != MS_PROB_UNAVAILABLE && !info.exhaustive &&
                info.count < cases[k].sample_count) {
                CHECK_EQ(info.reason, MS_REASON_MEMORY_BUDGET_EXHAUSTED);
                short_rows++;
            }
            env_close(&env);
        }
    }
    CHECK(short_rows > 0);
}

/* Allocation failure injection at every request (or every `stride`-th) with
 * unrelated caller allocations live in the workspace: always MS_OK with
 * every invariant, the caller's bytes intact, and once nothing fails the
 * reference answer. Returns whether generation itself ran out of memory
 * after an available result. */
static bool injection_sweep(const post_case *pc, uint32_t stride, uint64_t burst) {
    ms_posterior_info ref;
    post_case_reference(pc, &ref);
    bool generation_hit = false;
    for (uint64_t after = 0;; after += stride) {
        post_env env;
        env_open(&env, 0.0);
        uint8_t *keep = (uint8_t *)rt_alloc(&env.mem, 1000);
        uint8_t *small = (uint8_t *)rt_bump(&env.mem, 48);
        CHECK(keep != NULL && small != NULL);
        for (uint32_t i = 0; i < 1000u; i++) keep[i] = (uint8_t)(i * 7u);
        for (uint32_t i = 0; i < 48u; i++) small[i] = (uint8_t)(i + 1u);
        size_t len = post_case_open(&env, pc);
        rt_mem_set_failure(&env.mem, after, burst);
        uint64_t failures = env.mem.failures;
        ms_posterior_info info;
        CHECK_STATUS(generate(&env, obs_buf, len, pc->exact_limit, pc->sample_count, &info), MS_OK);
        bool hit = env.mem.failures != failures;
        rt_mem_set_failure(&env.mem, 0, 0);
        for (uint32_t i = 0; i < 1000u; i++) CHECK_EQ(keep[i], (uint8_t)(i * 7u));
        for (uint32_t i = 0; i < 48u; i++) CHECK_EQ(small[i], (uint8_t)(i + 1u));
        if (header(result_buf)->status != MS_PROB_UNAVAILABLE &&
            info.reason == MS_REASON_MEMORY_BUDGET_EXHAUSTED) {
            generation_hit = true;
        }
        rt_free(&env.mem, keep);
        rt_mem_reset(&env.mem);
        env_close(&env);
        if (!hit) {
            CHECK(same_as_reference(&info, &ref, pc));
            break;
        }
    }
    return generation_hit;
}

static void test_allocation_failures(void) {
    test_case("allocation failure injection: valid answers and complete cleanup");
    const ProbabilityCase *readme = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    static int8_t readme_clues[11];
    CHECK(fixture_case_dense_clues(readme, readme_clues, 11));
    static const int8_t corner[4] = {1, -1, -1, -1};
    make_pairs_board();
    static const post_case cases[] = {
        {"readme enumeration", 11, 1, 3, readme_clues, MS_DEFAULT_NODE_BUDGET, 2000, 64, 5, 1e9},
        {"readme draws", 11, 1, 3, readme_clues, MS_DEFAULT_NODE_BUDGET, 2000, 0, 30, 1e9},
        {"readme sampled", 11, 1, 3, readme_clues, 0, 500, 0, 30, 1e9},
        {"sampled corner", 2, 2, 1, corner, 0, 500, 4, 30, 1e9},
        {"pairs enumeration", 3200, 1, 8, pairs_board, MS_DEFAULT_NODE_BUDGET, 2000, 256, 4, 1e9},
    };
    bool generation_hit = false;
    for (uint32_t k = 0; k < array_size(cases); k++) {
        uint32_t stride = cases[k].width > 100u ? 5u : 1u;
        generation_hit |= injection_sweep(&cases[k], stride, RT_MEM_FOREVER);
        generation_hit |= injection_sweep(&cases[k], stride, 1);
    }
    CHECK(generation_hit);
}

/* ----------------------------------------------------- argument checks */

#define ARENA_BYTES 8192u
static uint64_t arena[ARENA_BYTES / 8u];
static uint64_t arena_before[ARENA_BYTES / 8u];
static uint64_t rows_before[4096u / 8u];

static uint8_t *arena_at(size_t offset) {
    return (uint8_t *)arena + offset;
}

/* A rejected call: `expected` with no byte of the arena, the observation,
 * result, rows or info changed, no clock reading and no workspace request. */
static void check_rejected(post_env *env, ms_status expected, const void *obs, size_t obs_len,
                           const ms_infer_limits *limits, uint32_t exact_limit,
                           uint32_t sample_count, rt_mem *workspace, rt_clock *clock, void *result,
                           size_t result_len, uint8_t *layouts, size_t layouts_len,
                           ms_posterior_info *info) {
    base_memcpy(arena_before, arena, ARENA_BYTES);
    base_memcpy(obs_copy, obs_buf, sizeof(obs_buf));
    base_memcpy(result_ref, result_buf, sizeof(result_buf));
    base_memcpy(rows_before, rows_words, sizeof(rows_before));
    uint32_t reads = env->fake.reads;
    uint64_t requests = env->mem.requests;
    size_t live = env->mem.live, budget = env->mem.budget;
    CHECK_STATUS(ms_posterior_generate(obs, obs_len, limits, exact_limit, sample_count, workspace,
                                       clock, result, result_len, layouts, layouts_len, info),
                 expected);
    CHECK(base_memcmp(arena_before, arena, ARENA_BYTES) == 0);
    CHECK(base_memcmp(obs_copy, obs_buf, sizeof(obs_buf)) == 0);
    CHECK(base_memcmp(result_ref, result_buf, sizeof(result_buf)) == 0);
    CHECK(base_memcmp(rows_before, rows_words, sizeof(rows_before)) == 0);
    CHECK_EQ(env->fake.reads, reads);
    CHECK_EQ(env->mem.requests, requests);
    CHECK_EQ(env->mem.live, live);
    CHECK_EQ(env->mem.budget, budget);
}

static void test_invalid_arguments(void) {
    test_case("invalid, misaligned, aliased or wrapping arguments are refused untouched");
    post_env env;
    env_open(&env, 1.0);
    const ProbabilityCase *c = &PROBABILITY_CASES[PROB_CASE_README_WEIGHTING];
    CHECK(fixture_case_dense_clues(c, dense, 11));
    size_t olen = make_obs(obs_buf, 11, 1, 3, dense);
    size_t rlen = ms_result_size(11, 1);
    size_t llen = 4u * 11u; /* exact_limit 4, sample_count 2 */
    size_t ilen = sizeof(ms_posterior_info);
    ms_infer_limits *limits = &env.limits;
    rt_mem *mem = &env.mem;
    rt_clock *clk = &env.clock;
    void *res = result_buf;
    ms_posterior_info info;
    base_memset(&info, 0x3C, sizeof(info));
    for (uint32_t i = 0; i < ARENA_BYTES / 8u; i++) arena[i] = 0x5A5A5A5A5A5A5A5Aull;
    const ms_status BAD = MS_ERR_INVALID_BUFFER;
#define REJECT(status, o, ol, l, el, sc, w, ck, r, rl, y, yl, in)                                 \
    check_rejected(&env, (status), (o), (ol), (l), (el), (sc), (w), (ck), (r), (rl), (y), (yl),  \
                   (in))

    /* NULL and misaligned pointers. */
    REJECT(BAD, NULL, olen, limits, 4, 2, mem, clk, res, rlen, ROWS, llen, &info);
    REJECT(BAD, obs_buf, olen, NULL, 4, 2, mem, clk, res, rlen, ROWS, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, NULL, &env.clock, result_buf, rlen, ROWS, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, &env.mem, NULL, result_buf, rlen, ROWS, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, NULL, rlen, ROWS, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, NULL, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS, llen, NULL);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS, llen,
           (ms_posterior_info *)arena_at(4));
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, arena_at(4), rlen, ROWS, llen,
           &info);
    REJECT(BAD, obs_buf, olen, (const ms_infer_limits *)arena_at(12), 4, 2, mem, clk, res, rlen,
           ROWS, llen, &info);
    REJECT(BAD, arena_at(4), olen, limits, 4, 2, mem, clk, res, rlen, ROWS, llen,
           &info);

    /* Lengths: observation, result and rows (exactly capacity * cells). */
    REJECT(BAD, obs_buf, olen + 8u, limits, 4, 2, mem, clk, res, rlen, ROWS,
           llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen - 8u, ROWS,
           llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS,
           llen - 1u, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS,
           llen + 11u, &info);
    REJECT(BAD, obs_buf, olen, limits, 2, 4, mem, clk, res, rlen, ROWS,
           2u * 11u, &info);
    REJECT(BAD, obs_buf, olen, limits, UINT32_MAX, 2, mem, clk, res, rlen, ROWS,
           llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 0, 0, mem, clk, res, rlen, ROWS, 11u,
           &info);
    REJECT(BAD, obs_buf, 0, limits, 4, 2, mem, clk, res, rlen, ROWS, llen, &info);

    /* Content: limits header, observation and limit values. */
    ms_infer_limits bad_limits;
    ms_limits_default(&bad_limits);
    bad_limits.magic++;
    REJECT(BAD, obs_buf, olen, &bad_limits, 4, 2, mem, clk, res, rlen, ROWS,
           llen, &info);
    ms_limits_default(&bad_limits);
    bad_limits.time_budget_ms = -1.0;
    REJECT(MS_ERR_INVALID_LIMITS, obs_buf, olen, &bad_limits, 4, 2, mem, clk, res, rlen, ROWS,
           llen, &info);
    ms_limits_default(&bad_limits);
    bad_limits.flags = 0x80u;
    REJECT(MS_ERR_INVALID_LIMITS, obs_buf, olen, &bad_limits, 4, 2, mem, clk, res, rlen, ROWS,
           llen, &info);
    ms_obs_clues(obs_buf)[0] = 9;
    REJECT(MS_ERR_INVALID_OBSERVATION, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS,
           llen, &info);
    ms_obs_clues(obs_buf)[0] = MS_CLUE_HIDDEN;

    /* Rows overlapping anything else. */
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen,
           (uint8_t *)obs_buf + olen - 1u, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen,
           (uint8_t *)result_buf + 8u, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen,
           (uint8_t *)limits + 3u, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, arena_at(512 - 5), llen,
           (ms_posterior_info *)arena_at(512));
    rt_mem *inner_mem = (rt_mem *)arena_at(1024 + 64);
    rt_mem_init(inner_mem, RT_MEM_UNLIMITED);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, inner_mem, clk, res, rlen,
           arena_at(1024 + 64 - 16), llen, &info);
    CHECK_EQ(inner_mem->requests, 0);
    fake_clock inner_fake;
    rt_clock *inner_clock = (rt_clock *)arena_at(2048);
    fake_clock_init(inner_clock, &inner_fake, 0.0, 1.0);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, inner_clock, res, rlen,
           arena_at(2048 + 8), llen, &info);
    CHECK_EQ(inner_fake.reads, 0);
    /* Info overlapping the result, the observation, the limits or a struct. */
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS, llen,
           (ms_posterior_info *)((uint8_t *)result_buf + rlen - 8u));
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS, llen,
           (ms_posterior_info *)obs_buf);
    ms_limits_default((ms_infer_limits *)arena_at(3072));
    REJECT(BAD, obs_buf, olen, (const ms_infer_limits *)arena_at(3072), 4, 2, mem, clk, res, rlen,
           ROWS, llen, (ms_posterior_info *)arena_at(3072 + 48));
    REJECT(BAD, obs_buf, olen, limits, 4, 2, inner_mem, clk, res, rlen, ROWS, llen,
           (ms_posterior_info *)((uint8_t *)inner_mem + 8u));
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, inner_clock, res, rlen, ROWS, llen,
           (ms_posterior_info *)arena_at(2048 - 8));
    /* The result over the clock, the workspace over the clock. */
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, inner_clock, arena_at(2048 - rlen + 8u), rlen,
           ROWS, llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, (rt_mem *)arena_at(2048 - 8), inner_clock, res, rlen,
           ROWS, llen, &info);
    rt_mem_dispose(inner_mem);

    /* Wrapping ranges are refused without being read. */
    uintptr_t top = MS_UINTPTR_MAX - 15u;
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, (uint8_t *)top,
           llen, &info);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS, llen,
           (ms_posterior_info *)top);
    REJECT(BAD, obs_buf, olen, limits, 4, 2, mem, clk, res, rlen, ROWS, SIZE_MAX,
           &info);
#undef REJECT

    /* Adjacent ranges in one arena are fine; rows need no alignment. */
    for (uint32_t i = 0; i < ARENA_BYTES / 8u; i++) arena[i] = 0;
    uint8_t *adj_obs = arena_at(0);
    ms_infer_limits *adj_limits = (ms_infer_limits *)arena_at(olen);
    uint8_t *adj_result = arena_at(olen + sizeof(ms_infer_limits));
    ms_posterior_info *adj_info = (ms_posterior_info *)(adj_result + rlen);
    uint8_t *adj_rows = (uint8_t *)adj_info + ilen;
    CHECK((size_t)(adj_rows + llen + 1u - arena_at(0)) <= ARENA_BYTES);
    make_obs(adj_obs, 11, 1, 3, dense);
    ms_limits_default(adj_limits);
    CHECK_STATUS(ms_posterior_generate(adj_obs, olen, adj_limits, 4, 2, &env.mem, &env.clock,
                                       adj_result, rlen, adj_rows, llen, adj_info),
                 MS_OK);
    CHECK_EQ(adj_info->count, 2);
    CHECK_EQ(adj_rows[llen], 0);
    CHECK_STATUS(ms_posterior_generate(adj_obs, olen, adj_limits, 4, 2, &env.mem, &env.clock,
                                       adj_result, rlen, adj_rows + 1, llen, adj_info),
                 MS_OK);
    CHECK(layout_ok(adj_obs, adj_rows + 1));
    /* No rows at all (both limits 0): NULL or any pointer with length 0. */
    CHECK_STATUS(ms_posterior_generate(obs_buf, olen, limits, 0, 0, &env.mem, &env.clock,
                                       result_buf, rlen, NULL, 0, &info),
                 MS_OK);
    CHECK_EQ(info.count, 0);
    CHECK_EQ(info.total_layouts, 21);
    CHECK_EQ(info.reason, MS_REASON_SAMPLING_BUDGET_EXHAUSTED);
    CHECK_STATUS(ms_posterior_generate(obs_buf, olen, limits, 0, 0, &env.mem, &env.clock,
                                       result_buf, rlen, (uint8_t *)result_buf, 0, &info),
                 MS_OK);
    env_close(&env);
}

void test_posterior(void) {
    test_exhaustive_random_boards();
    test_exhaustive_fixtures_and_thin_boards();
    test_exhaustive_long_boards();
    test_exact_draw_frequencies();
    test_weighted_draw_frequencies();
    test_complete_belief_fixtures();
    test_huge_boards();
    test_played_boards();
    test_determinism_and_digests();
    test_unavailable_and_approximate();
    test_result_matches_solve();
    test_deadlines();
    test_memory_budgets();
    test_allocation_failures();
    test_invalid_arguments();
}
