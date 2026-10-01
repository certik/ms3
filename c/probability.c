/*
 * probability.c - ms_solve: mine probabilities from a public observation.
 *
 * A stage-for-stage port of minesweeper/probability.py. The solver sees only
 * the observation (dimensions, mine total, clues of revealed safe cells) and
 * writes one canonical result buffer (engine.h).
 *
 *  1. Validation, one Boolean equation per clue and sound propagation:
 *     single-clue rules, bounded pairwise overlap reasoning (until 10% of
 *     the time budget) and the global mine-count extremes.
 *  2. The residual clue/cell incidence graph splits into connected
 *     components; hidden cells next to no clue form one exchangeable pool.
 *  3. Each component's cells are grouped by identical clue sets and the
 *     groups ordered (Cuthill-McKee from a pseudo-peripheral group) so few
 *     clues are open at a time. An iterative layered DP keyed by the residual
 *     demands of the open clues stores, per state, a polynomial over the
 *     number of mines placed so far (exact multiprecision coefficients,
 *     C(size, x) multiplicities), pruned by residual feasibility and the
 *     global mine cap. Every layer and edge is kept for the weighted backward
 *     pass and for structural proofs.
 *  4. Components that exceed the node/time/entry/byte budgets are estimated
 *     by sequential importance sampling: both values of each visited cell are
 *     tested with unit propagation, a fair coin decides only when both
 *     survive, a completed layout weighs 2^choices, dead ends weigh zero and
 *     interrupted draws are discarded.
 *  5. Components interact only through the mine total: histograms are
 *     convolved exactly, the pool contributes C(U, R - t), and each
 *     component's outside weights come from exact low-order division of the
 *     product by its own histogram. Per-cell numerators come from weighted
 *     backward passes (exact components) or weighted samples, and every
 *     component's total weight must equal the common Z.
 *  6. Proofs come from exact integers only: in an exact result a numerator
 *     equal to 0 or Z; otherwise propagation and the structural proofs of
 *     exhaustively counted components. Doubles appear only in the final
 *     ratio conversions and the effective sample size.
 *
 * Memory: every allocation comes from the caller's workspace, capped at
 * memory_budget_bytes (rt_mem_limit) and released before returning. Frozen
 * and small data are rt_bump'ed after an entry mark (rewound at the end);
 * growable scratch arrays are rt_alloc'ed (freed at the end). A failed
 * counting attempt rewinds its own mark and pool, then falls back to
 * sampling. Large work is iterative; nothing recurses.
 *
 * Aliasing: the observation, limits and result ranges (and the workspace
 * and clock structs) must not wrap and must be pairwise disjoint; this is
 * checked on addresses alone before any caller byte is read or written, and
 * a rejected call (MS_ERR_INVALID_BUFFER) changes nothing.
 */

#include "engine.h"
#include "runtime.h"
#include "bigint.h"

#include <base/math.h>
#include <base/mem.h>

/* Fractions of the time budget at which phases stop (probability.py). */
#define PB_PAIR_FRACTION 0.10
#define PB_COUNT_FRACTION 0.35
#define PB_SAMPLE_FRACTION 0.80
#define PB_RESERVE_FRACTION 0.10
/* The exact marginal pass costs up to about twice the forward pass. */
#define PB_BACKWARD_TIME_FACTOR 2.0
/* Pair-reasoning steps between clock reads. */
#define PB_PAIR_CLOCK_STEPS 64u
/* Cell assignments/undos between clock reads inside one sampling draw. */
#define PB_DRAW_CLOCK_OPS 4096u
/* Proposals per hard component and round-robin turn. */
#define PB_SAMPLE_BATCH 8u

#define PB_NONE UINT32_MAX
/* A group's cells all touch its first clue: at most eight. */
#define PB_MAX_GROUP 8u
/* Unit-propagation pushes per assigned cell: <= 8 clues x 7 other cells. */
#define PB_PUSHES_PER_CELL 56u

/* Structural verdict of a group: fixed in every counted component layout. */
#define PB_VERDICT_FREE 0u
#define PB_VERDICT_SAFE 1u
#define PB_VERDICT_MINE 2u

/* Component modes. */
#define PB_PENDING 0u
#define PB_EXACT 1u
#define PB_SAMPLED 2u

/* Internal outcomes; only PB_UNSAT and PB_BUG ever become ms_solve errors. */
typedef enum pb_rc {
    PB_OK = 0,
    PB_BUDGET, /* nodes, time or retained entries ran out */
    PB_NOMEM,  /* an allocation failed: byte budget, injected or host */
    PB_UNSAT,  /* the observation admits no layout */
    PB_BUG     /* a broken invariant or workspace misuse */
} pb_rc;

#define PB_TRY(expr)                                                                       \
    do {                                                                                   \
        pb_rc pb_try_ = (expr);                                                            \
        if (pb_try_ != PB_OK) return pb_try_;                                              \
    } while (0)
#define PB_TRYBI(expr) PB_TRY(pb_bi(expr))

_Static_assert(sizeof(bigint) == 16, "bigint is a 16-byte value");
_Static_assert(sizeof(size_t) == sizeof(uintptr_t), "caller ranges use one width");

/* C(n, k) for group sizes n <= 8. */
static const uint8_t PB_BINOM[PB_MAX_GROUP + 1][PB_MAX_GROUP + 1] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0, 0, 0, 0},
    {1, 3, 3, 1, 0, 0, 0, 0, 0},
    {1, 4, 6, 4, 1, 0, 0, 0, 0},
    {1, 5, 10, 10, 5, 1, 0, 0, 0},
    {1, 6, 15, 20, 15, 6, 1, 0, 0},
    {1, 7, 21, 35, 35, 21, 7, 1, 0},
    {1, 8, 28, 56, 70, 56, 28, 8, 1},
};

/* ======================================================================
 * Types
 * ====================================================================== */

/* A growable scratch array owned by the solver (rt_grow; freed at the end). */
typedef struct pb_vec {
    void *data;
    size_t cap; /* elements */
} pb_vec;

enum {
    PB_V_KEYS,    /* uint8: residual demands of the current layer's states */
    PB_V_NKEYS,   /* uint8: the same for the layer being built */
    PB_V_HASHES,  /* uint32: key hash per state of the layer being built */
    PB_V_SLOTS,   /* uint32: open-addressing table of that layer */
    PB_V_ESRC,    /* uint32: edges of the layer being built */
    PB_V_EDST,    /* uint32 */
    PB_V_EX,      /* uint8 */
    PB_V_RLO,     /* uint32: polynomial range per new state */
    PB_V_RHI,     /* uint32 */
    PB_V_LIVE,    /* uint8: structural proof flags */
    PB_V_ALIVE,   /* uint8 */
    PB_V_SUF,     /* bigint: backward suffix weights of layer i */
    PB_V_NSUF,    /* bigint: ... of layer i + 1 */
    PB_V_PRES,    /* uint8: state of layer i reaches a live suffix */
    PB_V_NPRES,   /* uint8 */
    PB_V_QA,      /* bigint: histogram product (ping-pong) */
    PB_V_QB,      /* bigint */
    PB_V_D,       /* bigint: product of the other histograms (cavity) */
    PB_V_SAMPLES, /* pb_sample: distinct sampled layouts, all components */
    PB_V_WORDS,   /* uint32: their layout bitsets */
    PB_V_SSLOTS,  /* uint32: hash table over the samples */
    PB_V_COUNT
};

/* One DP layer: the states after the first i groups of a component. A
 * state's polynomial counts partial layouts by mines placed so far: its
 * coefficient j belongs to lo + j mines. */
typedef struct pb_layer {
    uint32_t count;  /* states */
    uint32_t total;  /* coefficients of all states */
    uint32_t *lo;
    uint32_t *len;
    uint32_t *start; /* first coefficient in coef */
    bigint *coef;
} pb_layer;

/* The expansions from layer i to layer i + 1: state src places x mines in
 * group order[i] and reaches state dst. */
typedef struct pb_edges {
    uint32_t count;
    uint32_t *src;
    uint32_t *dst;
    uint8_t *x;
} pb_edges;

typedef struct pb_comp {
    /* structure: local variables are positions in cells */
    uint32_t ncells;
    uint32_t *cells;       /* hidden cells, ascending */
    uint32_t ncons;
    uint8_t *need;         /* residual clue value per local constraint */
    uint32_t *cons_start;  /* CSR: variables of each constraint, ascending */
    uint32_t *cons_vars;
    uint32_t *var_start;   /* CSR: constraints of each variable, ascending */
    uint32_t *var_cons;
    uint32_t ngroups;
    uint32_t *group_start; /* CSR: variables of each group, ascending */
    uint32_t *group_vars;
    uint32_t *gcons_start; /* CSR: each group's clue signature */
    uint32_t *gcons;
    uint32_t *order;       /* groups in counting order */
    uint32_t width;        /* most clues open at once along the order */
    uint32_t mode;         /* PB_PENDING, PB_EXACT or PB_SAMPLED */
    /* exact counting (mode PB_EXACT) */
    bi_pool pool;          /* coefficients of the layers below */
    pb_layer *layers;      /* ngroups + 1 */
    pb_edges *edges;       /* ngroups */
    uint8_t *verdict;      /* PB_VERDICT_* per group */
    /* mine-count histogram: hist[k - hlo] layouts (or sample weight) with k
     * mines; hist[0] and hist[hlen - 1] are nonzero */
    uint32_t hlo;
    uint32_t hlen;
    const bigint *hist;
    /* sampling (mode PB_SAMPLED) */
    bool sampler;          /* a sampler was set up */
    uint32_t kcap;
    uint32_t quota;
    uint32_t attempts;
    uint32_t successes;
    uint32_t *visit;       /* variables in proposal order */
    /* combination */
    const bigint *weights; /* outside weight W[k - hlo] of k mines here */
    bigint *numerators;    /* exact: per group (one cell); sampled: per variable */
    bool has_ess;
    double ess;
} pb_comp;

/* One distinct sampled layout of a hard component. */
typedef struct pb_sample {
    uint32_t comp;        /* index into pb_solver.comp */
    uint32_t occurrences;
    uint32_t choices;     /* fair coins: proposal probability 2^-choices */
    uint32_t mines;
    uint32_t hash;
    uint32_t reserved;
    size_t words;         /* offset of the layout bitset in PB_V_WORDS */
} pb_sample;

typedef struct pb_solver {
    rt_mem *mem;
    rt_clock *clock;
    uint64_t misuses0;

    /* limits */
    uint64_t node_budget;
    uint64_t store_budget;
    uint32_t sample_budget;
    double min_ess;
    double budget_ms;
    double start_ms;
    double deadline_ms;
    bool infinite;
    rt_rng rng;

    /* observation */
    uint32_t width;
    uint32_t height;
    uint32_t n;
    uint32_t total;
    uint32_t revealed;
    const uint8_t *clue;

    /* stage 1: constraints and propagation */
    bool propagated;      /* val holds sound proofs */
    bool pairs_complete;
    int8_t *val;          /* -1 unknown, 0 safe (incl. revealed), 1 mine */
    uint32_t unknown;
    uint32_t fixed_mines;
    uint32_t ncons;
    uint32_t *cons_cell;
    int32_t *cons_need;   /* residual demand */
    uint8_t *cons_count;  /* unknown members, the first of cons_list[cons_start[c]..] */
    uint32_t *cons_start; /* CSR: hidden neighbors of each clue, ascending */
    uint32_t *cons_list;
    uint32_t *var_start;  /* CSR: clues of each cell, ascending */
    uint32_t *var_list;
    uint32_t *units;      /* unit-rule stack */
    size_t units_len;
    size_t units_cap;
    uint32_t *touched;
    size_t touched_len;
    size_t touched_cap;
    uint32_t *ring;       /* pair-reasoning FIFO */
    uint32_t ring_head;
    uint32_t ring_len;
    uint8_t *queued;

    /* stage 2: components and pool */
    bool split;
    uint32_t ncomp;
    pb_comp **comp;       /* sorted by (width, cells, first cell) */
    uint32_t pool_cells;
    uint32_t *bulk;
    uint32_t remaining;   /* mines not fixed by propagation */

    /* stages 3 and 4 */
    uint64_t nodes_used;
    uint32_t exact_count;
    uint32_t hard_count;
    int8_t *d_val;        /* shared draw workspace */
    int32_t *d_need;
    int32_t *d_cap;
    uint32_t *d_trail;
    uint32_t d_trail_len;
    uint32_t *d_stack;
    size_t d_stack_cap;
    uint64_t d_ops;
    uint32_t nsamples;
    size_t nwords;
    uint32_t sslot_cap;

    /* stage 5 */
    bool combined;        /* z, bulk_num and every numerator are valid */
    bigint z;
    bigint bulk_num;      /* pool cell numerator (denominator pool * z) */
    bigint bulk_den;
    size_t suf_used;      /* live bigints in PB_V_SUF, PB_V_NSUF, ... */
    size_t nsuf_used;
    size_t q_used[2];     /* PB_V_QA, PB_V_QB */
    size_t d_used;

    /* outcome */
    uint32_t status;
    uint32_t reason;

    bi_pool pool;         /* every bigint outside exact counting */
    pb_vec vec[PB_V_COUNT];
} pb_solver;

/* ======================================================================
 * Helpers
 * ====================================================================== */

static pb_rc pb_bi(ms_status status) {
    if (status == MS_OK) return PB_OK;
    return status == MS_ERR_RESOURCE_EXHAUSTED ? PB_NOMEM : PB_BUG;
}

/* Why the last allocation returned NULL: misuse of the workspace is a bug,
 * anything else (budget, injection, host) is exhaustion. */
static pb_rc pb_fail_rc(const pb_solver *s) {
    return s->mem->misuses != s->misuses0 ? PB_BUG : PB_NOMEM;
}

/* Zeroed bump storage, released by the final rewind (or a failed counting
 * attempt's rewind). NULL on failure: see pb_fail_rc. */
static void *pb_new(pb_solver *s, size_t count, size_t size) {
    return rt_bump_array(s->mem, count ? count : 1u, size);
}

#define PB_NEW(s, ptr, count) (((ptr) = pb_new((s), (count), sizeof(*(ptr)))) != NULL)

static pb_rc pb_grow_vec(pb_solver *s, uint32_t id, size_t needed, size_t elem) {
    pb_vec *v = &s->vec[id];
    void *grown = rt_grow(s->mem, v->data, &v->cap, needed ? needed : 1u, elem);
    if (grown == NULL) return pb_fail_rc(s);
    v->data = grown;
    return PB_OK;
}

/* Capacity for `needed` elements; contents are kept, new ones undefined. */
static inline pb_rc pb_reserve(pb_solver *s, uint32_t id, size_t needed, size_t elem) {
    const pb_vec *v = &s->vec[id];
    if (v->data != NULL && needed <= v->cap) return PB_OK;
    return pb_grow_vec(s, id, needed, elem);
}

static void pb_swap_vec(pb_solver *s, uint32_t a, uint32_t b) {
    pb_vec t = s->vec[a];
    s->vec[a] = s->vec[b];
    s->vec[b] = t;
}

static inline uint32_t pb_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static inline int64_t pb_min_i64(int64_t a, int64_t b) {
    return a < b ? a : b;
}

static inline int64_t pb_max_i64(int64_t a, int64_t b) {
    return a > b ? a : b;
}

static inline uint32_t pb_group_size(const pb_comp *c, uint32_t g) {
    return c->group_start[g + 1] - c->group_start[g];
}

/* acc += a * m, exactly. Values below 2^64 stay inline: the checked 64-bit
 * fast path falls back to multiprecision on any overflow. */
static ms_status pb_addmul_u32(bi_pool *pool, bigint *acc, const bigint *a, uint32_t m) {
    uint64_t av, cv, product, sum;
    if (bi_get_u64(a, &av) && bi_get_u64(acc, &cv) && rt_mul_u64(av, m, &product) &&
        rt_add_u64(cv, product, &sum)) {
        bi_set_u64(acc, sum);
        return MS_OK;
    }
    return bi_addmul_u32(pool, acc, a, m);
}

/* acc += a * b, exactly, with the same fast path (acc distinct from a, b). */
static ms_status pb_addmul(bi_pool *pool, bigint *acc, const bigint *a, const bigint *b) {
    uint64_t av, bv, cv, product, sum;
    if (bi_get_u64(a, &av) && bi_get_u64(b, &bv) && bi_get_u64(acc, &cv) &&
        rt_mul_u64(av, bv, &product) && rt_add_u64(cv, product, &sum)) {
        bi_set_u64(acc, sum);
        return MS_OK;
    }
    return bi_addmul(pool, acc, a, b);
}

/* Hash of a state key (residual demands, one byte each). */
static uint32_t pb_hash_key(const uint8_t *key, uint32_t len) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (uint64_t)len;
    uint32_t i = 0;
    for (; i + 8u <= len; i += 8u) {
        uint64_t word = 0;
        for (uint32_t b = 0; b < 8u; b++) word |= (uint64_t)key[i + b] << (8u * b);
        h = rt_mix64(h ^ word);
    }
    uint64_t tail = 0;
    for (uint32_t b = 0; i + b < len; b++) tail |= (uint64_t)key[i + b] << (8u * b);
    h = rt_mix64(h ^ tail ^ 0xD6E8FEB86659FD93ull);
    return (uint32_t)(h ^ (h >> 32));
}

/* Index (0..31) of the lowest set bit of a nonzero word (de Bruijn). */
static uint32_t pb_lowest_bit(uint32_t word) {
    static const uint8_t table[32] = {
        0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9,
    };
    uint32_t low = word & (0u - word);
    return table[(uint32_t)(low * 0x077CB531u) >> 27];
}

/* ======================================================================
 * Stage 1: clue constraints and sound propagation
 * ====================================================================== */

/* Contradictions visible without any allocation (Python raises them before
 * or while building constraints): more mines than hidden cells, or a clue
 * above its hidden neighbor count. */
static ms_status pb_quick_contradictions(const ms_obs_header *h, const uint8_t *clue) {
    uint32_t n = h->width * h->height;
    if (h->total_mines > n - h->revealed) return MS_ERR_INCONSISTENT;
    for (uint32_t i = 0; i < n; i++) {
        if (clue[i] == MS_CLUE_HIDDEN) continue;
        uint32_t around[8];
        uint32_t count = rt_grid_neighbors(h->width, h->height, i, around);
        uint32_t hidden = 0;
        for (uint32_t k = 0; k < count; k++) hidden += clue[around[k]] == MS_CLUE_HIDDEN;
        if (clue[i] > hidden) return MS_ERR_INCONSISTENT;
    }
    return MS_OK;
}

/* One constraint per revealed clue with hidden neighbors, in ascending clue
 * order (Python's _build_constraints), stored as CSR in both directions with
 * exact sizes. */
static pb_rc pb_build(pb_solver *s) {
    uint32_t n = s->n;
    if (!PB_NEW(s, s->val, n) || !PB_NEW(s, s->var_start, (size_t)n + 1u)) return pb_fail_rc(s);
    uint32_t ncons = 0;
    uint32_t links = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s->clue[i] == MS_CLUE_HIDDEN) {
            s->val[i] = -1;
            continue;
        }
        s->val[i] = 0;
        uint32_t around[8];
        uint32_t count = rt_grid_neighbors(s->width, s->height, i, around);
        uint32_t members = 0;
        for (uint32_t k = 0; k < count; k++) {
            if (s->clue[around[k]] != MS_CLUE_HIDDEN) continue;
            members++;
            s->var_start[around[k] + 1u]++;
        }
        if (s->clue[i] > members) return PB_UNSAT;
        if (members == 0) continue;
        ncons++;
        links += members;
    }
    for (uint32_t v = 0; v < n; v++) s->var_start[v + 1u] += s->var_start[v];
    if (s->var_start[n] != links) return PB_BUG;
    s->unknown = n - s->revealed;
    s->ncons = ncons;
    /* Each assignment queues/touches at most one entry per clue of the cell,
     * so the clue-cell links bound both lists over the whole propagation. */
    s->units_cap = (size_t)ncons + links + 1u;
    s->touched_cap = (size_t)links + 1u;
    uint32_t *fill;
    if (!PB_NEW(s, s->var_list, links) || !PB_NEW(s, fill, n) ||
        !PB_NEW(s, s->cons_cell, ncons) || !PB_NEW(s, s->cons_need, ncons) ||
        !PB_NEW(s, s->cons_count, ncons) || !PB_NEW(s, s->cons_start, (size_t)ncons + 1u) ||
        !PB_NEW(s, s->cons_list, links) || !PB_NEW(s, s->units, s->units_cap) ||
        !PB_NEW(s, s->touched, s->touched_cap) || !PB_NEW(s, s->ring, ncons) ||
        !PB_NEW(s, s->queued, ncons)) {
        return pb_fail_rc(s);
    }
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s->clue[i] == MS_CLUE_HIDDEN) continue;
        uint32_t around[8];
        uint32_t count = rt_grid_neighbors(s->width, s->height, i, around);
        uint32_t start = s->cons_start[c];
        uint32_t members = 0;
        for (uint32_t k = 0; k < count; k++) {
            uint32_t v = around[k];
            if (s->clue[v] != MS_CLUE_HIDDEN) continue;
            if (start + members >= links) return PB_BUG;
            s->cons_list[start + members++] = v;
        }
        if (members == 0) continue;
        if (c >= ncons) return PB_BUG;
        s->cons_cell[c] = i;
        s->cons_need[c] = s->clue[i];
        s->cons_count[c] = (uint8_t)members;
        s->cons_start[c + 1u] = start + members;
        for (uint32_t k = 0; k < members; k++) {
            uint32_t v = s->cons_list[start + k];
            s->var_list[s->var_start[v] + fill[v]++] = c;
        }
        c++;
    }
    return c == ncons ? PB_OK : PB_BUG;
}

static inline uint32_t pb_var_degree(const pb_solver *s, uint32_t v) {
    return s->var_start[v + 1u] - s->var_start[v];
}

/* Python's _assign: fixes v, updates its clues, queues unit clues. */
static pb_rc pb_assign(pb_solver *s, uint32_t v, int8_t x) {
    s->val[v] = x;
    s->unknown--;
    if (x) s->fixed_mines++;
    const uint32_t *vc = s->var_list + s->var_start[v];
    uint32_t degree = pb_var_degree(s, v);
    for (uint32_t k = 0; k < degree; k++) {
        uint32_t c = vc[k];
        uint32_t *members = s->cons_list + s->cons_start[c];
        uint32_t count = s->cons_count[c];
        uint32_t at = 0;
        while (at < count && members[at] != v) at++;
        if (at == count) return PB_BUG;
        for (; at + 1 < count; at++) members[at] = members[at + 1];
        count--;
        s->cons_count[c] = (uint8_t)count;
        if (x) s->cons_need[c]--;
        int32_t r = s->cons_need[c];
        if (r < 0 || r > (int32_t)count) return PB_UNSAT;
        if (count && (r == 0 || r == (int32_t)count)) {
            if (s->units_len >= s->units_cap) return PB_BUG;
            s->units[s->units_len++] = c;
        }
        if (s->touched_len >= s->touched_cap) return PB_BUG;
        s->touched[s->touched_len++] = c;
    }
    return PB_OK;
}

/* Python's _units: a clue needing none or all of its unknown cells. */
static pb_rc pb_units(pb_solver *s) {
    while (s->units_len > 0) {
        uint32_t c = s->units[--s->units_len];
        uint32_t count = s->cons_count[c];
        if (count == 0) continue;
        int32_t r = s->cons_need[c];
        int8_t x;
        if (r == 0) {
            x = 0;
        } else if (r == (int32_t)count) {
            x = 1;
        } else {
            continue;
        }
        uint32_t cells[8];
        const uint32_t *members = s->cons_list + s->cons_start[c];
        for (uint32_t k = 0; k < count; k++) cells[k] = members[k];
        for (uint32_t k = 0; k < count; k++) PB_TRY(pb_assign(s, cells[k], x));
    }
    return PB_OK;
}

/* Python's _global: the mine total against the unknown cells. */
static pb_rc pb_global(pb_solver *s, bool *assigned) {
    *assigned = false;
    int64_t remaining = (int64_t)s->total - (int64_t)s->fixed_mines;
    if (remaining < 0 || remaining > (int64_t)s->unknown) return PB_UNSAT;
    if (s->unknown && (remaining == 0 || remaining == (int64_t)s->unknown)) {
        int8_t x = remaining == 0 ? 0 : 1;
        for (uint32_t v = 0; v < s->n; v++) {
            if (s->val[v] < 0) PB_TRY(pb_assign(s, v, x));
        }
        *assigned = true;
    }
    return PB_OK;
}

/* Sorted intersection/difference of small ascending lists. */
static uint32_t pb_intersect(const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb,
                             uint32_t *out) {
    uint32_t i = 0, j = 0, k = 0;
    while (i < na && j < nb) {
        if (a[i] < b[j]) {
            i++;
        } else if (b[j] < a[i]) {
            j++;
        } else {
            out[k++] = a[i];
            i++;
            j++;
        }
    }
    return k;
}

static uint32_t pb_minus(const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb,
                         uint32_t *out) {
    uint32_t i = 0, j = 0, k = 0;
    while (i < na) {
        while (j < nb && b[j] < a[i]) j++;
        if (j < nb && b[j] == a[i]) {
            i++;
            continue;
        }
        out[k++] = a[i++];
    }
    return k;
}

/* Python's _pair_check: overlap reasoning between clue a and every clue
 * sharing one of its unknown cells, in ascending clue order; stops at the
 * first pair that forces something. */
static pb_rc pb_pair_check(pb_solver *s, uint32_t a, bool *progressed) {
    *progressed = false;
    uint32_t sa[8];
    uint32_t na_all = s->cons_count[a];
    const uint32_t *ma = s->cons_list + s->cons_start[a];
    for (uint32_t k = 0; k < na_all; k++) sa[k] = ma[k];
    uint32_t others[64];
    uint32_t nothers = 0;
    for (uint32_t k = 0; k < na_all; k++) {
        uint32_t v = sa[k];
        const uint32_t *vc = s->var_list + s->var_start[v];
        for (uint32_t t = 0; t < pb_var_degree(s, v); t++) {
            uint32_t b = vc[t];
            if (b == a) continue;
            uint32_t at = 0;
            while (at < nothers && others[at] < b) at++;
            if (at < nothers && others[at] == b) continue;
            for (uint32_t m = nothers; m > at; m--) others[m] = others[m - 1];
            others[at] = b;
            nothers++;
        }
    }
    for (uint32_t o = 0; o < nothers; o++) {
        uint32_t b = others[o];
        uint32_t sb[8];
        uint32_t nb_all = s->cons_count[b];
        const uint32_t *mb = s->cons_list + s->cons_start[b];
        for (uint32_t k = 0; k < nb_all; k++) sb[k] = mb[k];
        uint32_t inter[8];
        uint32_t ni = pb_intersect(sa, na_all, sb, nb_all, inter);
        if (ni == 0) continue;
        int32_t na = (int32_t)(na_all - ni);
        int32_t nb = (int32_t)(nb_all - ni);
        int32_t ra = s->cons_need[a];
        int32_t rb = s->cons_need[b];
        int32_t lo = 0;
        if (ra - na > lo) lo = ra - na;
        if (rb - nb > lo) lo = rb - nb;
        int32_t hi = (int32_t)ni;
        if (ra < hi) hi = ra;
        if (rb < hi) hi = rb;
        if (lo > hi) return PB_UNSAT;
        int fa = -1, fb = -1, fi = -1;
        if (na) {
            if (ra - hi == na) {
                fa = 1;
            } else if (ra == lo) {
                fa = 0;
            }
        }
        if (nb) {
            if (rb - hi == nb) {
                fb = 1;
            } else if (rb == lo) {
                fb = 0;
            }
        }
        if (lo == (int32_t)ni) {
            fi = 1;
        } else if (hi == 0) {
            fi = 0;
        }
        if (fa < 0 && fb < 0 && fi < 0) continue;
        /* Snapshots first: assignments edit the live member lists. */
        uint32_t only_a[8], only_b[8];
        uint32_t noa = pb_minus(sa, na_all, inter, ni, only_a);
        uint32_t nob = pb_minus(sb, nb_all, inter, ni, only_b);
        const uint32_t *sets[3] = {only_a, only_b, inter};
        uint32_t sizes[3] = {noa, nob, ni};
        int values[3] = {fa, fb, fi};
        for (uint32_t f = 0; f < 3; f++) {
            if (values[f] < 0) continue;
            int8_t x = (int8_t)values[f];
            for (uint32_t k = 0; k < sizes[f]; k++) {
                uint32_t v = sets[f][k];
                if (s->val[v] < 0) {
                    PB_TRY(pb_assign(s, v, x));
                } else if (s->val[v] != x) {
                    return PB_UNSAT;
                }
            }
        }
        *progressed = true;
        return PB_OK;
    }
    return PB_OK;
}

/* Python's _propagate: unit and global rules to a fixpoint, then pair
 * reasoning one queued clue at a time until a deduction (which restarts the
 * cheap rules) or until 10% of the time budget has passed. */
static pb_rc pb_propagate(pb_solver *s) {
    uint32_t ncons = s->ncons;
    for (uint32_t c = 0; c < ncons; c++) {
        s->units[c] = c;
        s->ring[c] = c;
        s->queued[c] = 1;
    }
    s->units_len = ncons;
    s->ring_head = 0;
    s->ring_len = ncons;
    double deadline = rt_deadline(s->start_ms, s->budget_ms, PB_PAIR_FRACTION);
    bool complete = true;
    uint64_t steps = 0;
    for (;;) {
        PB_TRY(pb_units(s));
        bool assigned;
        PB_TRY(pb_global(s, &assigned));
        if (assigned) continue;
        for (size_t t = 0; t < s->touched_len; t++) {
            uint32_t c = s->touched[t];
            if (!s->queued[c] && s->cons_count[c]) {
                if (s->ring_len >= ncons) return PB_BUG;
                s->queued[c] = 1;
                s->ring[(s->ring_head + s->ring_len) % ncons] = c;
                s->ring_len++;
            }
        }
        s->touched_len = 0;
        bool progressed = false;
        while (s->ring_len > 0 && complete) {
            steps++;
            if (steps % PB_PAIR_CLOCK_STEPS == 0 && rt_clock_now(s->clock) > deadline) {
                complete = false;
                break;
            }
            uint32_t a = s->ring[s->ring_head];
            s->ring_head = (s->ring_head + 1) % ncons;
            s->ring_len--;
            s->queued[a] = 0;
            if (s->cons_count[a]) {
                PB_TRY(pb_pair_check(s, a, &progressed));
                if (progressed) break;
            }
        }
        if (!progressed) {
            s->pairs_complete = complete;
            return PB_OK;
        }
    }
}

/* ======================================================================
 * Stage 2: components, the unconstrained pool, groups and their order
 * ====================================================================== */

static uint32_t pb_find(uint32_t *parent, uint32_t c) {
    while (parent[c] != c) {
        parent[c] = parent[parent[c]];
        c = parent[c];
    }
    return c;
}

/* Breadth-first order from `source`; returns the number of groups reached. */
static uint32_t pb_bfs(uint32_t source, uint32_t ng, const uint32_t *adj_start,
                       const uint32_t *adj, int32_t *dist, uint32_t *order) {
    for (uint32_t g = 0; g < ng; g++) dist[g] = -1;
    dist[source] = 0;
    order[0] = source;
    uint32_t len = 1;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t g = order[i];
        int32_t step = dist[g] + 1;
        for (uint32_t t = adj_start[g]; t < adj_start[g + 1]; t++) {
            uint32_t h = adj[t];
            if (dist[h] < 0) {
                dist[h] = step;
                order[len++] = h;
            }
        }
    }
    return len;
}

/* Python's _Component._group_and_order: groups of variables with identical
 * clue sets (numbered by first variable), a Cuthill-McKee order from a
 * pseudo-peripheral group (neighbors by ascending degree, then index; at
 * most three eccentricity improvements) and the order's width. */
static pb_rc pb_group_order(pb_solver *s, pb_comp *c) {
    uint32_t nv = c->ncells;
    uint32_t nc = c->ncons;
    uint32_t *group_of, *gfirst, *first_count, *first_list;
    if (!PB_NEW(s, group_of, nv) || !PB_NEW(s, gfirst, nv) || !PB_NEW(s, first_count, nc) ||
        !PB_NEW(s, first_list, (size_t)nc * 8u)) {
        return pb_fail_rc(s);
    }
    uint32_t ng = 0;
    for (uint32_t v = 0; v < nv; v++) {
        const uint32_t *sig = c->var_cons + c->var_start[v];
        uint32_t slen = c->var_start[v + 1] - c->var_start[v];
        if (slen == 0) return PB_BUG;
        uint32_t c0 = sig[0];
        uint32_t found = PB_NONE;
        for (uint32_t t = 0; t < first_count[c0] && found == PB_NONE; t++) {
            uint32_t g = first_list[(size_t)c0 * 8u + t];
            uint32_t u = gfirst[g];
            uint32_t ulen = c->var_start[u + 1] - c->var_start[u];
            if (ulen == slen &&
                base_memcmp(c->var_cons + c->var_start[u], sig, slen * sizeof(uint32_t)) == 0) {
                found = g;
            }
        }
        if (found == PB_NONE) {
            if (first_count[c0] >= 8u) return PB_BUG;
            found = ng++;
            gfirst[found] = v;
            first_list[(size_t)c0 * 8u + first_count[c0]++] = found;
        }
        group_of[v] = found;
    }
    c->ngroups = ng;

    uint32_t *cursor;
    if (!PB_NEW(s, c->group_start, (size_t)ng + 1u) || !PB_NEW(s, c->group_vars, nv) ||
        !PB_NEW(s, c->gcons_start, (size_t)ng + 1u) || !PB_NEW(s, cursor, ng)) {
        return pb_fail_rc(s);
    }
    for (uint32_t v = 0; v < nv; v++) c->group_start[group_of[v] + 1]++;
    for (uint32_t g = 0; g < ng; g++) {
        c->group_start[g + 1] += c->group_start[g];
        cursor[g] = c->group_start[g];
        uint32_t u = gfirst[g];
        c->gcons_start[g + 1] = c->gcons_start[g] + (c->var_start[u + 1] - c->var_start[u]);
    }
    for (uint32_t v = 0; v < nv; v++) c->group_vars[cursor[group_of[v]]++] = v;
    for (uint32_t g = 0; g < ng; g++) {
        if (pb_group_size(c, g) > PB_MAX_GROUP) return PB_BUG;
    }
    if (!PB_NEW(s, c->gcons, c->gcons_start[ng])) return pb_fail_rc(s);
    for (uint32_t g = 0; g < ng; g++) {
        uint32_t u = gfirst[g];
        base_memcpy(c->gcons + c->gcons_start[g], c->var_cons + c->var_start[u],
                    (c->gcons_start[g + 1] - c->gcons_start[g]) * sizeof(uint32_t));
    }

    /* Groups of each clue, in group order (at most eight). */
    uint32_t *cg_count, *cg_list;
    if (!PB_NEW(s, cg_count, nc) || !PB_NEW(s, cg_list, (size_t)nc * 8u)) return pb_fail_rc(s);
    for (uint32_t g = 0; g < ng; g++) {
        for (uint32_t t = c->gcons_start[g]; t < c->gcons_start[g + 1]; t++) {
            uint32_t k = c->gcons[t];
            if (cg_count[k] >= 8u) return PB_BUG;
            cg_list[(size_t)k * 8u + cg_count[k]++] = g;
        }
    }

    /* Group adjacency (shared clue), neighbors sorted by (degree, index). */
    uint32_t *stamp, *adj_start, *degree, *adj;
    if (!PB_NEW(s, stamp, ng) || !PB_NEW(s, adj_start, (size_t)ng + 1u) ||
        !PB_NEW(s, degree, ng)) {
        return pb_fail_rc(s);
    }
    for (uint32_t g = 0; g < ng; g++) stamp[g] = PB_NONE;
    for (uint32_t g = 0; g < ng; g++) {
        stamp[g] = g;
        uint32_t count = 0;
        for (uint32_t t = c->gcons_start[g]; t < c->gcons_start[g + 1]; t++) {
            uint32_t k = c->gcons[t];
            for (uint32_t m = 0; m < cg_count[k]; m++) {
                uint32_t h = cg_list[(size_t)k * 8u + m];
                if (stamp[h] != g) {
                    stamp[h] = g;
                    count++;
                }
            }
        }
        degree[g] = count;
        adj_start[g + 1] = adj_start[g] + count;
    }
    if (!PB_NEW(s, adj, adj_start[ng])) return pb_fail_rc(s);
    for (uint32_t g = 0; g < ng; g++) stamp[g] = PB_NONE;
    for (uint32_t g = 0; g < ng; g++) {
        stamp[g] = g;
        uint32_t *list = adj + adj_start[g];
        uint32_t count = 0;
        for (uint32_t t = c->gcons_start[g]; t < c->gcons_start[g + 1]; t++) {
            uint32_t k = c->gcons[t];
            for (uint32_t m = 0; m < cg_count[k]; m++) {
                uint32_t h = cg_list[(size_t)k * 8u + m];
                if (stamp[h] == g) continue;
                stamp[h] = g;
                /* insertion by (degree, index) */
                uint32_t at = count++;
                while (at > 0 && (degree[list[at - 1]] > degree[h] ||
                                  (degree[list[at - 1]] == degree[h] && list[at - 1] > h))) {
                    list[at] = list[at - 1];
                    at--;
                }
                list[at] = h;
            }
        }
    }

    uint32_t *order_a, *order_b;
    int32_t *dist_a, *dist_b;
    if (!PB_NEW(s, order_a, ng) || !PB_NEW(s, order_b, ng) || !PB_NEW(s, dist_a, ng) ||
        !PB_NEW(s, dist_b, ng)) {
        return pb_fail_rc(s);
    }
    uint32_t start = 0;
    for (uint32_t g = 1; g < ng; g++) {
        if (degree[g] < degree[start]) start = g;
    }
    if (pb_bfs(start, ng, adj_start, adj, dist_a, order_a) != ng) return PB_BUG;
    for (uint32_t round = 0; round < 3u; round++) {
        int32_t ecc = dist_a[order_a[ng - 1]];
        uint32_t far = PB_NONE;
        for (uint32_t t = 0; t < ng; t++) {
            uint32_t g = order_a[t];
            if (dist_a[g] != ecc) continue;
            if (far == PB_NONE || degree[g] < degree[far] ||
                (degree[g] == degree[far] && g < far)) {
                far = g;
            }
        }
        pb_bfs(far, ng, adj_start, adj, dist_b, order_b);
        int32_t next_ecc = dist_b[order_b[ng - 1]];
        if (next_ecc < ecc) break;
        uint32_t *swap_order = order_a;
        order_a = order_b;
        order_b = swap_order;
        int32_t *swap_dist = dist_a;
        dist_a = dist_b;
        dist_b = swap_dist;
        if (next_ecc == ecc) break;
    }
    c->order = order_a;

    /* Width: most clues open at once (from first to last group in order). */
    int32_t *delta;
    uint32_t *position = order_b; /* no longer needed as an order */
    if (!PB_NEW(s, delta, (size_t)ng + 1u)) return pb_fail_rc(s);
    for (uint32_t i = 0; i < ng; i++) position[order_a[i]] = i;
    for (uint32_t k = 0; k < nc; k++) {
        if (cg_count[k] == 0) return PB_BUG;
        uint32_t lo = UINT32_MAX, hi = 0;
        for (uint32_t m = 0; m < cg_count[k]; m++) {
            uint32_t p = position[cg_list[(size_t)k * 8u + m]];
            if (p < lo) lo = p;
            if (p > hi) hi = p;
        }
        delta[lo]++;
        delta[hi + 1]--;
    }
    int32_t open = 0;
    c->width = 0;
    for (uint32_t i = 0; i < ng; i++) {
        open += delta[i];
        if (open > (int32_t)c->width) c->width = (uint32_t)open;
    }
    return PB_OK;
}

/* Python's _Component.__init__: local CSR structure of one component. */
static pb_rc pb_comp_build(pb_solver *s, pb_comp *c, const uint32_t *gcid, const uint32_t *local) {
    uint32_t nc = c->ncons;
    uint32_t nv = c->ncells;
    if (!PB_NEW(s, c->cons_start, (size_t)nc + 1u) || !PB_NEW(s, c->var_start, (size_t)nv + 1u)) {
        return pb_fail_rc(s);
    }
    for (uint32_t k = 0; k < nc; k++) {
        c->cons_start[k + 1] = c->cons_start[k] + s->cons_count[gcid[k]];
    }
    uint32_t links = c->cons_start[nc];
    uint32_t *cursor;
    if (!PB_NEW(s, c->cons_vars, links) || !PB_NEW(s, c->var_cons, links) ||
        !PB_NEW(s, cursor, nv)) {
        return pb_fail_rc(s);
    }
    for (uint32_t k = 0; k < nc; k++) {
        const uint32_t *members = s->cons_list + s->cons_start[gcid[k]];
        for (uint32_t m = 0; m < s->cons_count[gcid[k]]; m++) {
            uint32_t lv = local[members[m]];
            c->cons_vars[c->cons_start[k] + m] = lv;
            c->var_start[lv + 1]++;
        }
    }
    for (uint32_t v = 0; v < nv; v++) {
        c->var_start[v + 1] += c->var_start[v];
        cursor[v] = c->var_start[v];
    }
    for (uint32_t k = 0; k < nc; k++) {
        for (uint32_t t = c->cons_start[k]; t < c->cons_start[k + 1]; t++) {
            uint32_t lv = c->cons_vars[t];
            c->var_cons[cursor[lv]++] = k;
        }
    }
    return pb_group_order(s, c);
}

static bool pb_comp_before(const pb_comp *a, const pb_comp *b) {
    if (a->width != b->width) return a->width < b->width;
    if (a->ncells != b->ncells) return a->ncells < b->ncells;
    return a->cells[0] < b->cells[0];
}

/* Stable bottom-up merge sort of the component list. */
static void pb_sort_comps(pb_comp **items, pb_comp **tmp, uint32_t count) {
    pb_comp **from = items;
    pb_comp **to = tmp;
    for (uint32_t width = 1; width < count; width *= 2u) {
        for (uint32_t lo = 0; lo < count; lo += 2u * width) {
            uint32_t mid = pb_min_u32(lo + width, count);
            uint32_t hi = pb_min_u32(lo + 2u * width, count);
            uint32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) to[k++] = pb_comp_before(from[j], from[i]) ? from[j++] : from[i++];
            while (i < mid) to[k++] = from[i++];
            while (j < hi) to[k++] = from[j++];
        }
        pb_comp **swap = from;
        from = to;
        to = swap;
    }
    if (from != items) {
        for (uint32_t k = 0; k < count; k++) items[k] = from[k];
    }
}

/* Python's _components: union-find over the clues of every unknown cell,
 * components in order of their first clue, then sorted by
 * (width, cells, first cell) so easy components are counted first. */
static pb_rc pb_split(pb_solver *s) {
    uint32_t n = s->n;
    uint32_t ncons = s->ncons;
    uint32_t *parent, *root_comp, *local;
    if (!PB_NEW(s, parent, ncons) || !PB_NEW(s, root_comp, ncons) || !PB_NEW(s, local, n)) {
        return pb_fail_rc(s);
    }
    for (uint32_t c = 0; c < ncons; c++) {
        parent[c] = c;
        root_comp[c] = PB_NONE;
    }
    uint32_t pool = 0;
    for (uint32_t v = 0; v < n; v++) {
        if (s->val[v] >= 0) continue;
        uint32_t degree = pb_var_degree(s, v);
        if (degree == 0) {
            pool++;
            continue;
        }
        const uint32_t *vc = s->var_list + s->var_start[v];
        uint32_t root = pb_find(parent, vc[0]);
        for (uint32_t k = 1; k < degree; k++) {
            uint32_t other = pb_find(parent, vc[k]);
            if (other != root) parent[other] = root;
        }
    }
    if (!PB_NEW(s, s->bulk, pool)) return pb_fail_rc(s);
    for (uint32_t v = 0; v < n; v++) {
        if (s->val[v] < 0 && pb_var_degree(s, v) == 0) s->bulk[s->pool_cells++] = v;
    }
    uint32_t ncomp = 0;
    for (uint32_t c = 0; c < ncons; c++) {
        if (s->cons_count[c] == 0) continue;
        uint32_t root = pb_find(parent, c);
        if (root_comp[root] == PB_NONE) root_comp[root] = ncomp++;
    }
    pb_comp *comps;
    pb_comp **sorted, **scratch;
    uint32_t **gcid;
    uint32_t *fill_cells, *fill_cons;
    if (!PB_NEW(s, comps, ncomp) || !PB_NEW(s, sorted, ncomp) || !PB_NEW(s, scratch, ncomp) ||
        !PB_NEW(s, gcid, ncomp) || !PB_NEW(s, fill_cells, ncomp) || !PB_NEW(s, fill_cons, ncomp)) {
        return pb_fail_rc(s);
    }
    for (uint32_t c = 0; c < ncons; c++) {
        if (s->cons_count[c]) comps[root_comp[pb_find(parent, c)]].ncons++;
    }
    for (uint32_t v = 0; v < n; v++) {
        if (s->val[v] < 0 && pb_var_degree(s, v)) {
            comps[root_comp[pb_find(parent, s->var_list[s->var_start[v]])]].ncells++;
        }
    }
    for (uint32_t k = 0; k < ncomp; k++) {
        pb_comp *c = &comps[k];
        if (c->ncells == 0 || c->ncons == 0) return PB_BUG;
        if (!PB_NEW(s, c->cells, c->ncells) || !PB_NEW(s, c->need, c->ncons) ||
            !PB_NEW(s, gcid[k], c->ncons)) {
            return pb_fail_rc(s);
        }
    }
    for (uint32_t v = 0; v < n; v++) {
        if (s->val[v] < 0 && pb_var_degree(s, v)) {
            uint32_t k = root_comp[pb_find(parent, s->var_list[s->var_start[v]])];
            local[v] = fill_cells[k];
            comps[k].cells[fill_cells[k]++] = v;
        }
    }
    for (uint32_t c = 0; c < ncons; c++) {
        if (s->cons_count[c] == 0) continue;
        uint32_t k = root_comp[pb_find(parent, c)];
        uint32_t lc = fill_cons[k]++;
        if (s->cons_need[c] < 0 || s->cons_need[c] > 8) return PB_BUG;
        comps[k].need[lc] = (uint8_t)s->cons_need[c];
        gcid[k][lc] = c;
    }
    for (uint32_t k = 0; k < ncomp; k++) {
        PB_TRY(pb_comp_build(s, &comps[k], gcid[k], local));
        sorted[k] = &comps[k];
    }
    pb_sort_comps(sorted, scratch, ncomp);
    s->comp = sorted;
    s->ncomp = ncomp;
    return PB_OK;
}

/* ======================================================================
 * Stage 3: exact counting (Python's _Component.count) and structural proofs
 * ====================================================================== */

typedef struct pb_bound {
    int32_t src;  /* position of the clue in the state key, -1 if not open */
    int32_t init; /* its demand when not open */
    int32_t cap;  /* its cells after this group */
} pb_bound;

/* Finds `key` in the next layer's table; on a miss *slot is the free slot. */
static uint32_t pb_table_find(const pb_solver *s, uint32_t mask, uint32_t w, const uint8_t *key,
                              uint32_t hash, uint32_t *slot) {
    const uint32_t *slots = (const uint32_t *)s->vec[PB_V_SLOTS].data;
    const uint32_t *hashes = (const uint32_t *)s->vec[PB_V_HASHES].data;
    const uint8_t *keys = (const uint8_t *)s->vec[PB_V_NKEYS].data;
    uint32_t p = hash & mask;
    for (;;) {
        uint32_t index = slots[p];
        if (index == PB_NONE) {
            *slot = p;
            return PB_NONE;
        }
        if (hashes[index] == hash && base_memcmp(keys + (size_t)index * w, key, w) == 0) {
            return index;
        }
        p = (p + 1u) & mask;
    }
}

/* (Re)builds an empty table of `cap` slots holding states 0..count-1. */
static pb_rc pb_table_rebuild(pb_solver *s, uint32_t cap, uint32_t count) {
    PB_TRY(pb_reserve(s, PB_V_SLOTS, cap, sizeof(uint32_t)));
    uint32_t *slots = (uint32_t *)s->vec[PB_V_SLOTS].data;
    const uint32_t *hashes = (const uint32_t *)s->vec[PB_V_HASHES].data;
    for (uint32_t p = 0; p < cap; p++) slots[p] = PB_NONE;
    for (uint32_t d = 0; d < count; d++) {
        uint32_t p = hashes[d] & (cap - 1u);
        while (slots[p] != PB_NONE) p = (p + 1u) & (cap - 1u);
        slots[p] = d;
    }
    return PB_OK;
}

/* Forward pass. Layer i maps the residual demands of the open clues after
 * the first i groups to a polynomial over the mines placed so far. Each
 * (state, group mine count) expansion is one search node (Python's node
 * accounting, so node budgets mean the same); stored coefficients and edges
 * count against the retained-entry guard. Pass 1 of a layer enumerates the
 * expansions (states, edges, coefficient ranges); pass 2 accumulates
 * C(size, x) * polynomial into freshly allocated, zeroed coefficients, so
 * no next-layer accumulator ever aliases a frozen previous layer.
 * Returns PB_UNSAT when the component has no layout within kcap mines. */
static pb_rc pb_count(pb_solver *s, pb_comp *c, int64_t kcap, uint64_t node_limit,
                      uint64_t store_limit, rt_meter *meter, uint64_t *nodes_out,
                      uint64_t *stored_out) {
    uint32_t G = c->ngroups;
    uint32_t nc = c->ncons;
    uint64_t nodes = 0;
    uint64_t stored = 0;
    *nodes_out = 0;
    *stored_out = 0;
    if (G == 0 || kcap < 0) return PB_BUG;

    int32_t *last, *remcap, *pos, *b_src, *b_init;
    uint32_t *active, *next_active;
    uint8_t *b_dec, *key_tmp;
    if (!PB_NEW(s, last, nc) || !PB_NEW(s, remcap, nc) || !PB_NEW(s, pos, nc) ||
        !PB_NEW(s, b_src, nc) || !PB_NEW(s, b_init, nc) || !PB_NEW(s, active, nc) ||
        !PB_NEW(s, next_active, nc) || !PB_NEW(s, b_dec, nc) || !PB_NEW(s, key_tmp, nc) ||
        !PB_NEW(s, c->layers, (size_t)G + 1u) || !PB_NEW(s, c->edges, G) ||
        !PB_NEW(s, c->verdict, G)) {
        return pb_fail_rc(s);
    }
    for (uint32_t k = 0; k < nc; k++) pos[k] = -1;
    for (uint32_t i = 0; i < G; i++) {
        uint32_t g = c->order[i];
        uint32_t size = pb_group_size(c, g);
        for (uint32_t t = c->gcons_start[g]; t < c->gcons_start[g + 1]; t++) {
            last[c->gcons[t]] = (int32_t)i;
            remcap[c->gcons[t]] += (int32_t)size;
        }
    }
    pb_layer *first = &c->layers[0];
    if (!PB_NEW(s, first->lo, 1) || !PB_NEW(s, first->len, 1) || !PB_NEW(s, first->start, 1) ||
        !PB_NEW(s, first->coef, 1)) {
        return pb_fail_rc(s);
    }
    first->count = 1;
    first->total = 1;
    first->len[0] = 1;
    bi_set_u64(&first->coef[0], 1);
    PB_TRY(pb_reserve(s, PB_V_KEYS, 1, 1));
    PB_TRY(pb_reserve(s, PB_V_LIVE, 1, 1));
    PB_TRY(pb_reserve(s, PB_V_ALIVE, 1, 1));

    uint32_t w = 0; /* open clues (key length) of the current layer */
    for (uint32_t i = 0; i < G; i++) {
        uint32_t g = c->order[i];
        uint32_t size = pb_group_size(c, g);
        if (size == 0 || size > PB_MAX_GROUP) return PB_BUG;
        const uint32_t *cg = c->gcons + c->gcons_start[g];
        uint32_t ncg = c->gcons_start[g + 1] - c->gcons_start[g];
        if (ncg > 8u) return PB_BUG;
        for (uint32_t k = 0; k < ncg; k++) remcap[cg[k]] -= (int32_t)size;
        for (uint32_t j = 0; j < w; j++) pos[active[j]] = (int32_t)j;
        pb_bound bounds[8], exact[8];
        uint32_t nb = 0, ne = 0;
        for (uint32_t k = 0; k < ncg; k++) {
            uint32_t q = cg[k];
            pb_bound b = {pos[q], c->need[q], remcap[q]};
            if (last[q] == (int32_t)i) {
                exact[ne++] = b;
            } else {
                bounds[nb++] = b;
            }
        }
        /* next_active = sorted((active | cg) - ended) */
        uint32_t a = 0, b = 0, nw = 0;
        while (a < w || b < ncg) {
            uint32_t q;
            bool in_group;
            if (b >= ncg || (a < w && active[a] < cg[b])) {
                q = active[a++];
                in_group = false;
            } else if (a >= w || cg[b] < active[a]) {
                q = cg[b++];
                in_group = true;
            } else {
                q = active[a++];
                b++;
                in_group = true;
            }
            if (last[q] == (int32_t)i) continue;
            next_active[nw] = q;
            b_src[nw] = pos[q];
            b_init[nw] = c->need[q];
            b_dec[nw] = in_group ? 1u : 0u;
            nw++;
        }
        for (uint32_t j = 0; j < w; j++) pos[active[j]] = -1;

        /* Pass 1: expansions, next states and their coefficient ranges. */
        const pb_layer *L = &c->layers[i];
        uint32_t tcap = 16;
        while (tcap < 2u * L->count + 2u && tcap < (1u << 30)) tcap <<= 1;
        PB_TRY(pb_reserve(s, PB_V_NKEYS, 1, 1));
        PB_TRY(pb_reserve(s, PB_V_HASHES, 1, sizeof(uint32_t)));
        PB_TRY(pb_table_rebuild(s, tcap, 0));
        uint32_t next_count = 0;
        uint32_t nedges = 0;
        uint64_t pending = 0;
        for (uint32_t st = 0; st < L->count; st++) {
            const uint8_t *key = (const uint8_t *)s->vec[PB_V_KEYS].data + (size_t)st * w;
            int64_t lo = L->lo[st];
            uint32_t len = L->len[st];
            int32_t xlo = 0, xhi = (int32_t)size;
            for (uint32_t k = 0; k < nb; k++) {
                int32_t r0 = bounds[k].src >= 0 ? key[bounds[k].src] : bounds[k].init;
                if (r0 < xhi) xhi = r0;
                if (r0 - bounds[k].cap > xlo) xlo = r0 - bounds[k].cap;
            }
            for (uint32_t k = 0; k < ne; k++) {
                int32_t r0 = exact[k].src >= 0 ? key[exact[k].src] : exact[k].init;
                if (r0 > xlo) xlo = r0;
                if (r0 < xhi) xhi = r0;
            }
            if (kcap - lo < xhi) xhi = (int32_t)(kcap - lo);
            for (int32_t x = xlo; x <= xhi; x++) {
                for (uint32_t k = 0; k < nw; k++) {
                    int32_t r = b_src[k] >= 0 ? key[b_src[k]] : b_init[k];
                    if (b_dec[k]) r -= x;
                    key_tmp[k] = (uint8_t)r;
                }
                int64_t limit = kcap - lo - x + 1;
                uint32_t plen = limit >= (int64_t)len ? len : (uint32_t)limit;
                nodes++;
                *nodes_out = nodes;
                if (nodes > node_limit) return PB_BUDGET;
                if (rt_meter_work(meter, plen + 1u)) return PB_BUDGET;
                pending += (uint64_t)plen + 1u;
                if (stored + pending > store_limit) return PB_BUDGET;
                uint32_t hash = pb_hash_key(key_tmp, nw);
                uint32_t slot;
                uint32_t d = pb_table_find(s, tcap - 1u, nw, key_tmp, hash, &slot);
                uint32_t nlo = (uint32_t)lo + (uint32_t)x;
                uint32_t nhi = nlo + plen - 1u;
                if (d == PB_NONE) {
                    d = next_count;
                    PB_TRY(pb_reserve(s, PB_V_NKEYS, ((size_t)d + 1u) * (nw ? nw : 1u), 1));
                    PB_TRY(pb_reserve(s, PB_V_HASHES, (size_t)d + 1u, sizeof(uint32_t)));
                    PB_TRY(pb_reserve(s, PB_V_RLO, (size_t)d + 1u, sizeof(uint32_t)));
                    PB_TRY(pb_reserve(s, PB_V_RHI, (size_t)d + 1u, sizeof(uint32_t)));
                    uint8_t *nkeys = (uint8_t *)s->vec[PB_V_NKEYS].data;
                    for (uint32_t k = 0; k < nw; k++) nkeys[(size_t)d * nw + k] = key_tmp[k];
                    ((uint32_t *)s->vec[PB_V_HASHES].data)[d] = hash;
                    ((uint32_t *)s->vec[PB_V_RLO].data)[d] = nlo;
                    ((uint32_t *)s->vec[PB_V_RHI].data)[d] = nhi;
                    ((uint32_t *)s->vec[PB_V_SLOTS].data)[slot] = d;
                    next_count++;
                    if (2u * next_count > tcap) {
                        if (tcap >= (1u << 30)) return PB_NOMEM;
                        tcap <<= 1;
                        PB_TRY(pb_table_rebuild(s, tcap, next_count));
                    }
                } else {
                    uint32_t *rlo = (uint32_t *)s->vec[PB_V_RLO].data;
                    uint32_t *rhi = (uint32_t *)s->vec[PB_V_RHI].data;
                    if (nlo < rlo[d]) rlo[d] = nlo;
                    if (nhi > rhi[d]) rhi[d] = nhi;
                }
                if (nedges == UINT32_MAX) return PB_BUDGET;
                PB_TRY(pb_reserve(s, PB_V_ESRC, (size_t)nedges + 1u, sizeof(uint32_t)));
                PB_TRY(pb_reserve(s, PB_V_EDST, (size_t)nedges + 1u, sizeof(uint32_t)));
                PB_TRY(pb_reserve(s, PB_V_EX, (size_t)nedges + 1u, 1));
                ((uint32_t *)s->vec[PB_V_ESRC].data)[nedges] = st;
                ((uint32_t *)s->vec[PB_V_EDST].data)[nedges] = d;
                ((uint8_t *)s->vec[PB_V_EX].data)[nedges] = (uint8_t)x;
                nedges++;
            }
        }
        if (next_count == 0) return PB_UNSAT;

        /* Freeze layer i + 1 and the edges into it. The running estimate
         * above sums the incoming parts, but a merged state's span can also
         * hold zero-filled mine counts no single part covered (e.g. layouts
         * with 1 or 4 mines only), so the actual retained entries are
         * checked against the guard before anything is allocated. */
        const uint32_t *rlo = (const uint32_t *)s->vec[PB_V_RLO].data;
        const uint32_t *rhi = (const uint32_t *)s->vec[PB_V_RHI].data;
        uint64_t total = 0;
        for (uint32_t d = 0; d < next_count; d++) total += (uint64_t)(rhi[d] - rlo[d]) + 1u;
        uint64_t retained = (uint64_t)nedges + total;
        if (stored + retained > store_limit) return PB_BUDGET;
        stored += retained;
        *stored_out = stored;
        if (total > UINT32_MAX) return PB_BUDGET;
        pb_layer *N = &c->layers[i + 1];
        pb_edges *E = &c->edges[i];
        if (!PB_NEW(s, N->lo, next_count) || !PB_NEW(s, N->len, next_count) ||
            !PB_NEW(s, N->start, next_count) || !PB_NEW(s, N->coef, (size_t)total) ||
            !PB_NEW(s, E->src, nedges) || !PB_NEW(s, E->dst, nedges) || !PB_NEW(s, E->x, nedges)) {
            return pb_fail_rc(s);
        }
        N->count = next_count;
        N->total = (uint32_t)total;
        uint32_t at = 0;
        for (uint32_t d = 0; d < next_count; d++) {
            N->lo[d] = rlo[d];
            N->len[d] = rhi[d] - rlo[d] + 1u;
            N->start[d] = at;
            at += N->len[d];
        }
        E->count = nedges;
        base_memcpy(E->src, s->vec[PB_V_ESRC].data, (size_t)nedges * sizeof(uint32_t));
        base_memcpy(E->dst, s->vec[PB_V_EDST].data, (size_t)nedges * sizeof(uint32_t));
        base_memcpy(E->x, s->vec[PB_V_EX].data, nedges);

        /* Pass 2: next[dst] += C(size, x) * x^(lo + x) * part. */
        for (uint32_t e = 0; e < nedges; e++) {
            uint32_t st = E->src[e];
            uint32_t d = E->dst[e];
            uint32_t x = E->x[e];
            uint32_t lo = L->lo[st];
            uint32_t len = L->len[st];
            int64_t limit = kcap - (int64_t)lo - (int64_t)x + 1;
            uint32_t plen = limit >= (int64_t)len ? len : (uint32_t)limit;
            const bigint *from = L->coef + L->start[st];
            bigint *to = N->coef + N->start[d] + (lo + x - N->lo[d]);
            uint32_t mult = PB_BINOM[size][x];
            for (uint32_t j = 0; j < plen; j++) {
                if (bi_is_zero(&from[j])) continue;
                PB_TRYBI(pb_addmul_u32(&c->pool, &to[j], &from[j], mult));
            }
            if (rt_meter_work(meter, plen + 1u)) return PB_BUDGET;
        }

        PB_TRY(pb_reserve(s, PB_V_LIVE, next_count, 1));
        PB_TRY(pb_reserve(s, PB_V_ALIVE, next_count, 1));
        pb_swap_vec(s, PB_V_KEYS, PB_V_NKEYS);
        uint32_t *swap = active;
        active = next_active;
        next_active = swap;
        w = nw;
    }

    const pb_layer *F = &c->layers[G];
    if (F->count != 1 || w != 0) return PB_BUG;
    const bigint *coef = F->coef + F->start[0];
    uint32_t a = 0, b = F->len[0];
    while (a < b && bi_is_zero(&coef[a])) a++;
    while (b > a && bi_is_zero(&coef[b - 1])) b--;
    if (a == b) return PB_UNSAT;
    c->hlo = F->lo[0] + a;
    c->hlen = b - a;
    c->hist = coef + a;

    /* Python's structural_proofs: a group is fixed when every edge on a
     * path to the final state places the same 0 or `size` mines. */
    uint8_t *live = (uint8_t *)s->vec[PB_V_LIVE].data;
    uint8_t *alive = (uint8_t *)s->vec[PB_V_ALIVE].data;
    live[0] = 1;
    for (uint32_t i = G; i-- > 0;) {
        const pb_layer *Li = &c->layers[i];
        const pb_edges *E = &c->edges[i];
        base_memset(alive, 0, Li->count);
        uint32_t used = 0;
        for (uint32_t e = 0; e < E->count; e++) {
            if (live[E->dst[e]]) {
                used |= 1u << E->x[e];
                alive[E->src[e]] = 1;
            }
        }
        uint32_t g = c->order[i];
        uint32_t size = pb_group_size(c, g);
        c->verdict[g] = used == 1u ? PB_VERDICT_SAFE
                        : used == (1u << size) ? PB_VERDICT_MINE
                                               : PB_VERDICT_FREE;
        uint8_t *swap = live;
        live = alive;
        alive = swap;
    }
    return PB_OK;
}

/* Forgets a failed counting attempt and releases its memory. */
static void pb_discard(pb_solver *s, pb_comp *c, rt_mark mark) {
    bi_pool_reset(&c->pool);
    rt_mem_rewind(s->mem, mark);
    c->layers = NULL;
    c->edges = NULL;
    c->verdict = NULL;
    c->hist = NULL;
    c->hlo = 0;
    c->hlen = 0;
}

/* Python's _count: easiest components first, all sharing one node budget
 * and the retained-entry guard, until 35% of the time budget. A component
 * that runs out of nodes, time, entries or memory is left for sampling.
 * Returns the time spent on successful forward passes in *forward_ms. */
static pb_rc pb_count_all(pb_solver *s, double *forward_ms, uint64_t *known_min_out) {
    int64_t left = (int64_t)s->node_budget;
    uint64_t store_left = s->store_budget;
    double deadline = rt_deadline(s->start_ms, s->budget_ms, PB_COUNT_FRACTION);
    uint64_t known_min = 0;
    double spent = 0.0;
    for (uint32_t k = 0; k < s->ncomp; k++) {
        pb_comp *c = s->comp[k];
        c->mode = PB_PENDING;
        if (left <= 0 || rt_clock_now(s->clock) > deadline) continue;
        rt_meter meter;
        rt_meter_init(&meter, s->clock, deadline, RT_METER_INTERVAL);
        double began = rt_clock_now(s->clock);
        rt_mark mark = rt_mem_mark(s->mem);
        bi_pool_init(&c->pool, s->mem);
        uint64_t nodes = 0, stored = 0;
        int64_t kcap = (int64_t)s->remaining - (int64_t)known_min;
        pb_rc rc = pb_count(s, c, kcap, (uint64_t)left, store_left, &meter, &nodes, &stored);
        if (rc == PB_BUDGET || rc == PB_NOMEM) {
            pb_discard(s, c, mark);
            left -= (int64_t)nodes;
            s->nodes_used += nodes;
            continue;
        }
        if (rc != PB_OK) return rc; /* PB_UNSAT: no layout within the total */
        spent += rt_clock_now(s->clock) - began;
        left -= (int64_t)nodes;
        store_left = stored < store_left ? store_left - stored : 0;
        s->nodes_used += nodes;
        c->mode = PB_EXACT;
        known_min += c->hlo;
    }
    *forward_ms = spent;
    *known_min_out = known_min;
    return PB_OK;
}

/* ======================================================================
 * Stage 4: sequential importance sampling (Python's _Sampler, _sample)
 * ====================================================================== */

/* Assigns v0 := x0 in the draw workspace and unit-propagates. Returns the
 * new mine count, or -1 on a conflict (the trail keeps what was assigned;
 * the caller undoes it). */
static int64_t pb_draw_assign(pb_solver *s, const pb_comp *c, uint32_t v0, uint32_t x0,
                              int64_t mines) {
    int8_t *val = s->d_val;
    int32_t *need = s->d_need;
    int32_t *cap = s->d_cap;
    uint32_t *stack = s->d_stack;
    size_t top = 0;
    stack[top++] = (v0 << 1) | x0;
    while (top > 0) {
        uint32_t item = stack[--top];
        uint32_t v = item >> 1;
        int8_t x = (int8_t)(item & 1u);
        if (val[v] >= 0) {
            if (val[v] != x) return -1;
            continue;
        }
        s->d_ops++;
        val[v] = x;
        s->d_trail[s->d_trail_len++] = v;
        bool bad = false;
        if (x) {
            mines++;
            bad = mines > (int64_t)c->kcap;
        }
        for (uint32_t t = c->var_start[v]; t < c->var_start[v + 1]; t++) {
            uint32_t k = c->var_cons[t];
            int32_t room = cap[k] - 1;
            cap[k] = room;
            int32_t r = need[k] - x;
            need[k] = r;
            if (r < 0 || r > room) {
                bad = true;
            } else if (room && !bad && (r == 0 || r == room)) {
                uint32_t forced = r == 0 ? 0u : 1u;
                for (uint32_t m = c->cons_start[k]; m < c->cons_start[k + 1]; m++) {
                    uint32_t u = c->cons_vars[m];
                    if (val[u] < 0) {
                        if (top >= s->d_stack_cap) return -2; /* cannot happen: see bound */
                        stack[top++] = (u << 1) | forced;
                    }
                }
            }
        }
        if (bad) return -1;
    }
    return mines;
}

static void pb_draw_undo(pb_solver *s, const pb_comp *c, uint32_t mark) {
    s->d_ops += s->d_trail_len - mark;
    while (s->d_trail_len > mark) {
        uint32_t v = s->d_trail[--s->d_trail_len];
        int32_t x = s->d_val[v];
        for (uint32_t t = c->var_start[v]; t < c->var_start[v + 1]; t++) {
            uint32_t k = c->var_cons[t];
            s->d_cap[k]++;
            s->d_need[k] += x;
        }
        s->d_val[v] = -1;
    }
}

/* Records one completed layout (draw workspace) of component `ci`. */
static pb_rc pb_sample_store(pb_solver *s, uint32_t ci, const pb_comp *c, uint32_t choices,
                             uint32_t mines) {
    size_t nw = ((size_t)c->ncells + 31u) / 32u;
    PB_TRY(pb_reserve(s, PB_V_WORDS, s->nwords + nw, sizeof(uint32_t)));
    uint32_t *bits = (uint32_t *)s->vec[PB_V_WORDS].data + s->nwords;
    for (size_t k = 0; k < nw; k++) bits[k] = 0;
    for (uint32_t v = 0; v < c->ncells; v++) {
        if (s->d_val[v] == 1) bits[v >> 5] |= 1u << (v & 31u);
    }
    uint32_t hash = (uint32_t)rt_hash64(bits, nw * sizeof(uint32_t), ci);
    if (s->sslot_cap == 0) {
        PB_TRY(pb_reserve(s, PB_V_SSLOTS, 64, sizeof(uint32_t)));
        s->sslot_cap = 64;
        uint32_t *slots = (uint32_t *)s->vec[PB_V_SSLOTS].data;
        for (uint32_t p = 0; p < s->sslot_cap; p++) slots[p] = PB_NONE;
    }
    uint32_t *slots = (uint32_t *)s->vec[PB_V_SSLOTS].data;
    pb_sample *samples = (pb_sample *)s->vec[PB_V_SAMPLES].data;
    const uint32_t *words = (const uint32_t *)s->vec[PB_V_WORDS].data;
    uint32_t mask = s->sslot_cap - 1u;
    uint32_t p = hash & mask;
    for (;;) {
        uint32_t index = slots[p];
        if (index == PB_NONE) break;
        pb_sample *old = &samples[index];
        if (old->comp == ci && old->hash == hash &&
            base_memcmp(words + old->words, bits, nw * sizeof(uint32_t)) == 0) {
            old->occurrences++;
            return PB_OK;
        }
        p = (p + 1u) & mask;
    }
    if (s->nsamples == UINT32_MAX) return PB_NOMEM;
    PB_TRY(pb_reserve(s, PB_V_SAMPLES, (size_t)s->nsamples + 1u, sizeof(pb_sample)));
    samples = (pb_sample *)s->vec[PB_V_SAMPLES].data;
    pb_sample *fresh = &samples[s->nsamples];
    fresh->comp = ci;
    fresh->occurrences = 1;
    fresh->choices = choices;
    fresh->mines = mines;
    fresh->hash = hash;
    fresh->reserved = 0;
    fresh->words = s->nwords;
    slots[p] = s->nsamples++;
    s->nwords += nw;
    if (2u * s->nsamples > s->sslot_cap) {
        if (s->sslot_cap >= (1u << 30)) return PB_NOMEM;
        uint32_t cap = s->sslot_cap << 1;
        PB_TRY(pb_reserve(s, PB_V_SSLOTS, cap, sizeof(uint32_t)));
        slots = (uint32_t *)s->vec[PB_V_SSLOTS].data;
        for (uint32_t q = 0; q < cap; q++) slots[q] = PB_NONE;
        for (uint32_t index = 0; index < s->nsamples; index++) {
            uint32_t q = samples[index].hash & (cap - 1u);
            while (slots[q] != PB_NONE) q = (q + 1u) & (cap - 1u);
            slots[q] = index;
        }
        s->sslot_cap = cap;
    }
    return PB_OK;
}

/* One proposal (Python's _Sampler.draw). Visits the variables in group
 * order; tests both values with unit propagation; flips a fair coin only
 * when both survive. A dead end is a completed proposal of weight zero. A
 * proposal interrupted by the deadline is discarded (*interrupted). */
static pb_rc pb_draw(pb_solver *s, uint32_t ci, pb_comp *c, double deadline, bool *interrupted) {
    *interrupted = false;
    for (uint32_t v = 0; v < c->ncells; v++) s->d_val[v] = -1;
    for (uint32_t k = 0; k < c->ncons; k++) {
        s->d_need[k] = c->need[k];
        s->d_cap[k] = (int32_t)(c->cons_start[k + 1] - c->cons_start[k]);
    }
    s->d_trail_len = 0;
    s->d_ops = 0;
    int64_t mines = 0;
    uint32_t choices = 0;
    uint64_t next_check = PB_DRAW_CLOCK_OPS;
    for (uint32_t idx = 0; idx < c->ncells; idx++) {
        uint32_t v = c->visit[idx];
        if (s->d_val[v] >= 0) continue;
        if (s->d_ops >= next_check) {
            next_check = s->d_ops + PB_DRAW_CLOCK_OPS;
            if (rt_clock_now(s->clock) > deadline) {
                *interrupted = true;
                return PB_OK;
            }
        }
        uint32_t mark = s->d_trail_len;
        int64_t with_mine = pb_draw_assign(s, c, v, 1, mines);
        pb_draw_undo(s, c, mark);
        int64_t without = pb_draw_assign(s, c, v, 0, mines);
        if (with_mine == -2 || without == -2) return PB_BUG;
        if (without >= 0 && with_mine >= 0) {
            choices++;
            if (rt_rng_bit(&s->rng)) {
                pb_draw_undo(s, c, mark);
                mines = pb_draw_assign(s, c, v, 1, mines);
            } else {
                mines = without;
            }
        } else if (without >= 0) {
            mines = without;
        } else if (with_mine >= 0) {
            pb_draw_undo(s, c, mark);
            mines = pb_draw_assign(s, c, v, 1, mines);
        } else {
            c->attempts++; /* dead end: weight zero */
            return PB_OK;
        }
        if (mines < 0) return PB_BUG; /* a surviving branch cannot conflict on replay */
    }
    c->attempts++;
    PB_TRY(pb_sample_store(s, ci, c, choices, (uint32_t)mines));
    c->successes++;
    return PB_OK;
}

/* Python's _sample: proposals shared round-robin by the hard components in
 * batches, until the quotas are used or the deadline passes. Sets *failure
 * to the Python reason when no usable estimate can follow. */
static pb_rc pb_sample_all(pb_solver *s, double deadline, uint64_t known_min, uint32_t *failure) {
    uint32_t nh = s->hard_count;
    uint32_t base = s->sample_budget / nh;
    uint32_t extra = s->sample_budget % nh;
    uint32_t max_cells = 0, max_cons = 0, rank = 0;
    for (uint32_t k = 0; k < s->ncomp; k++) {
        pb_comp *c = s->comp[k];
        if (c->mode == PB_EXACT) continue;
        c->mode = PB_SAMPLED;
        c->sampler = true;
        c->kcap = (uint32_t)((int64_t)s->remaining - (int64_t)known_min);
        c->quota = base + (rank < extra ? 1u : 0u);
        rank++;
        if (c->ncells > max_cells) max_cells = c->ncells;
        if (c->ncons > max_cons) max_cons = c->ncons;
    }
    for (uint32_t k = 0; k < s->ncomp; k++) {
        pb_comp *c = s->comp[k];
        if (c->mode != PB_SAMPLED) continue;
        if (!PB_NEW(s, c->visit, c->ncells)) return pb_fail_rc(s);
        uint32_t at = 0;
        for (uint32_t i = 0; i < c->ngroups; i++) {
            uint32_t g = c->order[i];
            for (uint32_t t = c->group_start[g]; t < c->group_start[g + 1]; t++) {
                c->visit[at++] = c->group_vars[t];
            }
        }
    }
    s->d_stack_cap = (size_t)PB_PUSHES_PER_CELL * max_cells + 8u;
    if (!PB_NEW(s, s->d_val, max_cells) || !PB_NEW(s, s->d_need, max_cons) ||
        !PB_NEW(s, s->d_cap, max_cons) || !PB_NEW(s, s->d_trail, max_cells) ||
        !PB_NEW(s, s->d_stack, s->d_stack_cap)) {
        return pb_fail_rc(s);
    }

    uint32_t *pending;
    if (!PB_NEW(s, pending, s->ncomp)) return pb_fail_rc(s);
    uint32_t npending = 0;
    for (uint32_t k = 0; k < s->ncomp; k++) {
        if (s->comp[k]->mode == PB_SAMPLED && s->comp[k]->quota > 0) pending[npending++] = k;
    }
    bool out_of_time = false;
    while (npending > 0 && !out_of_time) {
        uint32_t still = 0;
        for (uint32_t t = 0; t < npending; t++) {
            uint32_t ci = pending[t];
            pb_comp *c = s->comp[ci];
            uint32_t batch = pb_min_u32(PB_SAMPLE_BATCH, c->quota - c->attempts);
            for (uint32_t b = 0; b < batch; b++) {
                bool interrupted;
                PB_TRY(pb_draw(s, ci, c, deadline, &interrupted));
                if (interrupted || rt_clock_now(s->clock) > deadline) {
                    out_of_time = true;
                    break;
                }
            }
            if (c->attempts < c->quota) pending[still++] = ci;
            if (out_of_time) break;
        }
        npending = still;
    }
    for (uint32_t k = 0; k < s->ncomp; k++) {
        const pb_comp *c = s->comp[k];
        if (c->mode == PB_SAMPLED && c->attempts == 0) {
            *failure = c->quota ? MS_REASON_TIME_BUDGET_EXHAUSTED
                                : MS_REASON_SAMPLING_BUDGET_EXHAUSTED;
            return PB_OK;
        }
    }
    for (uint32_t k = 0; k < s->ncomp; k++) {
        const pb_comp *c = s->comp[k];
        if (c->mode == PB_SAMPLED && c->successes == 0) {
            *failure = MS_REASON_NO_CONSISTENT_SAMPLES;
            return PB_OK;
        }
    }
    /* Histograms: weight occurrences * 2^choices per mine count. */
    const pb_sample *samples = (const pb_sample *)s->vec[PB_V_SAMPLES].data;
    bigint term = BI_ZERO_INIT;
    for (uint32_t k = 0; k < s->ncomp; k++) {
        pb_comp *c = s->comp[k];
        if (c->mode != PB_SAMPLED) continue;
        uint32_t klo = UINT32_MAX, khi = 0;
        for (uint32_t i = 0; i < s->nsamples; i++) {
            if (samples[i].comp != k) continue;
            if (samples[i].mines < klo) klo = samples[i].mines;
            if (samples[i].mines > khi) khi = samples[i].mines;
        }
        if (klo > khi) return PB_BUG;
        bigint *hist;
        if (!PB_NEW(s, hist, (size_t)(khi - klo) + 1u)) return pb_fail_rc(s);
        for (uint32_t i = 0; i < s->nsamples; i++) {
            if (samples[i].comp != k) continue;
            bi_set_u64(&term, samples[i].occurrences);
            PB_TRYBI(bi_shl(&s->pool, &term, &term, samples[i].choices));
            bigint *slot = &hist[samples[i].mines - klo];
            PB_TRYBI(bi_add(&s->pool, slot, slot, &term));
        }
        c->hlo = klo;
        c->hlen = khi - klo + 1u;
        c->hist = hist;
    }
    bi_free(&s->pool, &term);
    return PB_OK;
}

/* ======================================================================
 * Stage 5: global conditioning and per-cell numerators
 * ====================================================================== */

/* `count` zero bigints in vector `id`, after releasing the `*used` it held
 * (their limbs return to the pool for reuse). */
static pb_rc pb_bigvec_prepare(pb_solver *s, uint32_t id, size_t *used, size_t count) {
    bigint *items = (bigint *)s->vec[id].data;
    for (size_t k = 0; k < *used; k++) bi_free(&s->pool, &items[k]);
    *used = 0;
    PB_TRY(pb_reserve(s, id, count, sizeof(bigint)));
    base_memset(s->vec[id].data, 0, count * sizeof(bigint));
    *used = count;
    return PB_OK;
}

/* Python's _cavity: d = Q / H by exact low-order division (the product of
 * every other histogram), then W[k] = sum_t d[t] * T[k + t] for each k of H. */
static pb_rc pb_cavity(pb_solver *s, rt_meter *meter, const bigint *q, uint32_t qlo,
                       uint32_t qlen, const pb_comp *c, const bigint *T, int64_t tlo,
                       uint32_t tlen, const bigint **out) {
    bi_pool *pool = &s->pool;
    bigint *weights;
    if (!PB_NEW(s, weights, c->hlen)) return pb_fail_rc(s);
    PB_TRY(pb_bigvec_prepare(s, PB_V_D, &s->d_used, qlen));
    bigint *d = (bigint *)s->vec[PB_V_D].data;
    for (uint32_t t = 0; t < qlen; t++) {
        uint32_t top = t < c->hlen - 1u ? t : c->hlen - 1u;
        if (rt_meter_work(meter, top + 1u)) return PB_BUDGET;
        /* MS_ERR_INTERNAL here is an inexact or negative division. */
        PB_TRYBI(bi_poly_divexact_term(pool, d, t, &q[t], c->hist, c->hlen));
    }
    int64_t dlo = (int64_t)qlo - (int64_t)c->hlo;
    if (dlo < 0) return PB_BUG;
    for (uint32_t kk = 0; kk < c->hlen; kk++) {
        if (bi_is_zero(&c->hist[kk])) continue;
        int64_t base = (int64_t)c->hlo + kk + dlo - tlo;
        int64_t rlo = base < 0 ? -base : 0;
        int64_t rhi = pb_min_i64((int64_t)qlen, (int64_t)tlen - base);
        if (rhi <= rlo) continue;
        if (rt_meter_work(meter, (uint32_t)(rhi - rlo))) return PB_BUDGET;
        for (int64_t r = rlo; r < rhi; r++) {
            if (bi_is_zero(&d[r])) continue;
            PB_TRYBI(bi_addmul(pool, &weights[kk], &d[r], &T[base + r]));
        }
    }
    PB_TRY(pb_bigvec_prepare(s, PB_V_D, &s->d_used, 0));
    *out = weights;
    return PB_OK;
}

static bool pb_same_hist(const pb_comp *a, const pb_comp *b) {
    if (a->hlo != b->hlo || a->hlen != b->hlen) return false;
    for (uint32_t k = 0; k < a->hlen; k++) {
        if (!bi_eq(&a->hist[k], &b->hist[k])) return false;
    }
    return true;
}

/* Hash of a histogram's offset, length and exact coefficients. */
static uint64_t pb_hist_hash(const pb_comp *c) {
    uint64_t h = rt_mix64(((uint64_t)c->hlo << 32) ^ (uint64_t)c->hlen);
    for (uint32_t k = 0; k < c->hlen; k++) {
        const bigint *x = &c->hist[k];
        h = rt_hash64(bi_limbs(x), (size_t)x->len * sizeof(uint32_t), h ^ x->len);
    }
    return h;
}

/* Outside weights per distinct histogram, computed once and shared by every
 * component with an identical histogram (Python's cavity cache keyed by
 * (hlo, tuple(h))): a hash table over the components seen so far, with
 * exact comparison on every hash match. */
static pb_rc pb_all_cavities(pb_solver *s, rt_meter *meter, const bigint *q, uint32_t qlo,
                             uint32_t qlen, const bigint *T, int64_t tlo, uint32_t tlen) {
    uint32_t cap = 16;
    while (cap < 2u * s->ncomp) cap <<= 1;
    uint32_t *slots;
    uint64_t *hashes;
    if (!PB_NEW(s, slots, cap) || !PB_NEW(s, hashes, s->ncomp)) return pb_fail_rc(s);
    for (uint32_t p = 0; p < cap; p++) slots[p] = PB_NONE;
    for (uint32_t k = 0; k < s->ncomp; k++) {
        pb_comp *c = s->comp[k];
        hashes[k] = pb_hist_hash(c);
        c->weights = NULL;
        for (uint32_t p = (uint32_t)hashes[k] & (cap - 1u);; p = (p + 1u) & (cap - 1u)) {
            uint32_t j = slots[p];
            if (j == PB_NONE) {
                PB_TRY(pb_cavity(s, meter, q, qlo, qlen, c, T, tlo, tlen, &c->weights));
                slots[p] = k;
                break;
            }
            if (hashes[j] == hashes[k] && pb_same_hist(s->comp[j], c)) {
                c->weights = s->comp[j]->weights;
                break;
            }
        }
    }
    return PB_OK;
}

/* Python's _combine: Q = product of all histograms (exponents <= R), the
 * pool row T[s] = C(U, R - s), Z = sum Q[s] T[s], the pool numerator
 * sum Q[s] T[s] (R - s) (over denominator U Z) and, when Z > 0, each
 * distinct histogram's outside weights. The running product alternates
 * between two reusable arrays, so memory stays bounded by two products. */
static pb_rc pb_combine(pb_solver *s, rt_meter *meter) {
    bi_pool *pool = &s->pool;
    int64_t R = s->remaining;
    uint32_t U = s->pool_cells;
    static const uint32_t ids[2] = {PB_V_QA, PB_V_QB};
    uint32_t cur = 0;
    PB_TRY(pb_bigvec_prepare(s, ids[cur], &s->q_used[cur], 1));
    bigint *q = (bigint *)s->vec[ids[cur]].data;
    bi_set_u64(&q[0], 1);
    uint32_t qlo = 0, qlen = 1;
    for (uint32_t k = 0; k < s->ncomp && qlen > 0; k++) {
        const pb_comp *c = s->comp[k];
        int64_t lo = (int64_t)qlo + c->hlo;
        if (c->hlen == 0 || lo > R) {
            qlen = 0;
            break;
        }
        uint32_t length = (uint32_t)pb_min_i64((int64_t)qlen + c->hlen - 1, R - lo + 1);
        uint32_t next = 1u - cur;
        PB_TRY(pb_bigvec_prepare(s, ids[next], &s->q_used[next], length));
        bigint *out = (bigint *)s->vec[ids[next]].data;
        q = (bigint *)s->vec[ids[cur]].data;
        for (uint32_t i = 0; i < qlen && i < length; i++) {
            if (bi_is_zero(&q[i])) continue;
            uint32_t m = pb_min_u32(c->hlen, length - i);
            if (rt_meter_work(meter, m)) return PB_BUDGET;
            for (uint32_t j = 0; j < m; j++) {
                if (bi_is_zero(&c->hist[j])) continue;
                PB_TRYBI(pb_addmul(pool, &out[i + j], &q[i], &c->hist[j]));
            }
        }
        cur = next;
        q = out;
        qlen = length;
        qlo = (uint32_t)lo;
    }
    bi_set_zero(&s->z);
    bi_set_zero(&s->bulk_num);
    if (qlen == 0) return PB_OK;
    int64_t tlo = pb_max_i64((int64_t)qlo, R - (int64_t)U);
    int64_t thi = pb_min_i64((int64_t)qlo + qlen - 1, R);
    if (thi < tlo) return PB_OK;
    uint32_t tlen = (uint32_t)(thi - tlo + 1);
    if (rt_meter_work(meter, 2u * tlen)) return PB_BUDGET;
    bigint *T;
    if (!PB_NEW(s, T, tlen)) return pb_fail_rc(s);
    PB_TRYBI(bi_binomial(pool, &T[tlen - 1u], U, (uint32_t)(R - thi)));
    for (int64_t at = thi; at > tlo; at--) {
        uint32_t idx = (uint32_t)(at - tlo);
        PB_TRYBI(bi_copy(pool, &T[idx - 1u], &T[idx]));
        PB_TRYBI(bi_binomial_next(pool, &T[idx - 1u], U, (uint32_t)(R - at)));
    }
    bigint w = BI_ZERO_INIT;
    for (int64_t at = tlo; at <= thi; at++) {
        if (rt_meter_work(meter, 2u)) return PB_BUDGET;
        PB_TRYBI(bi_mul(pool, &w, &q[at - qlo], &T[at - tlo]));
        if (bi_is_zero(&w)) continue;
        PB_TRYBI(bi_add(pool, &s->z, &s->z, &w));
        PB_TRYBI(bi_addmul_u32(pool, &s->bulk_num, &w, (uint32_t)(R - at)));
    }
    bi_free(pool, &w);
    if (bi_is_zero(&s->z)) return PB_OK;
    PB_TRYBI(bi_mul_u32(pool, &s->bulk_den, &s->z, U));
    return pb_all_cavities(s, meter, q, qlo, qlen, T, tlo, tlen);
}

/* Python's exact_numerators: weighted backward pass over the stored DP.
 * The suffix of a state is, per coefficient (mines placed so far), the
 * outside-weighted number of completions; a group's numerator sums, over
 * its edges, C(size - 1, x - 1) * forward . suffix. The root total must be
 * the common Z. */
static pb_rc pb_backward(pb_solver *s, pb_comp *c, rt_meter *meter) {
    bi_pool *pool = &s->pool;
    uint32_t G = c->ngroups;
    if (!PB_NEW(s, c->numerators, G)) return pb_fail_rc(s);
    const pb_layer *F = &c->layers[G];
    PB_TRY(pb_bigvec_prepare(s, PB_V_NSUF, &s->nsuf_used, F->total));
    PB_TRY(pb_reserve(s, PB_V_NPRES, F->count, 1));
    ((uint8_t *)s->vec[PB_V_NPRES].data)[0] = 1;
    bigint *final = (bigint *)s->vec[PB_V_NSUF].data;
    for (uint32_t j = 0; j < F->len[0]; j++) {
        int64_t k = (int64_t)F->lo[0] + j - c->hlo;
        if (k >= 0 && k < (int64_t)c->hlen) PB_TRYBI(bi_copy(pool, &final[j], &c->weights[k]));
    }
    bigint dot = BI_ZERO_INIT;
    bigint numerator = BI_ZERO_INIT;
    for (uint32_t i = G; i-- > 0;) {
        const pb_layer *L = &c->layers[i];
        const pb_layer *N = &c->layers[i + 1];
        const pb_edges *E = &c->edges[i];
        PB_TRY(pb_bigvec_prepare(s, PB_V_SUF, &s->suf_used, L->total));
        PB_TRY(pb_reserve(s, PB_V_PRES, L->count, 1));
        bigint *suf = (bigint *)s->vec[PB_V_SUF].data;
        const bigint *nsuf = (const bigint *)s->vec[PB_V_NSUF].data;
        uint8_t *pres = (uint8_t *)s->vec[PB_V_PRES].data;
        const uint8_t *npres = (const uint8_t *)s->vec[PB_V_NPRES].data;
        base_memset(pres, 0, L->count);
        uint32_t g = c->order[i];
        uint32_t size = pb_group_size(c, g);
        bi_set_zero(&numerator);
        for (uint32_t e = 0; e < E->count; e++) {
            uint32_t d = E->dst[e];
            if (!npres[d]) continue;
            uint32_t st = E->src[e];
            uint32_t x = E->x[e];
            pres[st] = 1;
            const bigint *arr = L->coef + L->start[st];
            const bigint *arr2 = nsuf + N->start[d];
            bigint *acc = suf + L->start[st];
            int64_t base = (int64_t)L->lo[st] + x - (int64_t)N->lo[d];
            int64_t jlo = base < 0 ? -base : 0;
            int64_t jhi = pb_min_i64((int64_t)L->len[st], (int64_t)N->len[d] - base);
            uint32_t mult = PB_BINOM[size][x];
            if (rt_meter_work(meter, (uint32_t)(jhi > jlo ? jhi - jlo : 0) + 1u)) return PB_BUDGET;
            bi_set_zero(&dot);
            for (int64_t j = jlo; j < jhi; j++) {
                const bigint *gv = &arr2[base + j];
                if (bi_is_zero(gv)) continue;
                PB_TRYBI(pb_addmul_u32(pool, &acc[j], gv, mult));
                PB_TRYBI(pb_addmul(pool, &dot, &arr[j], gv));
            }
            if (x > 0 && !bi_is_zero(&dot)) {
                PB_TRYBI(bi_addmul_u32(pool, &numerator, &dot, PB_BINOM[size - 1u][x - 1u]));
            }
        }
        PB_TRYBI(bi_copy(pool, &c->numerators[g], &numerator));
        pb_swap_vec(s, PB_V_SUF, PB_V_NSUF);
        pb_swap_vec(s, PB_V_PRES, PB_V_NPRES);
        size_t used = s->suf_used;
        s->suf_used = s->nsuf_used;
        s->nsuf_used = used;
    }
    bi_free(pool, &dot);
    bi_free(pool, &numerator);
    const bigint *root = (const bigint *)s->vec[PB_V_NSUF].data;
    bool present = ((const uint8_t *)s->vec[PB_V_NPRES].data)[0] != 0;
    bool total_ok = present ? bi_eq(&root[0], &s->z) : bi_is_zero(&s->z);
    /* Release both suffix layers for the next component. */
    PB_TRY(pb_bigvec_prepare(s, PB_V_SUF, &s->suf_used, 0));
    PB_TRY(pb_bigvec_prepare(s, PB_V_NSUF, &s->nsuf_used, 0));
    return total_ok ? PB_OK : PB_BUG; /* component weight mismatch */
}

/* Python's _Sampler.numerators: each distinct layout weighs
 * occurrences * W[k] * 2^choices; the weights also give the effective
 * sample size (sum w)^2 / sum w^2, and their sum must be Z. */
static pb_rc pb_sample_numerators(pb_solver *s, uint32_t ci, pb_comp *c, rt_meter *meter) {
    bi_pool *pool = &s->pool;
    if (!PB_NEW(s, c->numerators, c->ncells)) return pb_fail_rc(s);
    const pb_sample *samples = (const pb_sample *)s->vec[PB_V_SAMPLES].data;
    const uint32_t *words = (const uint32_t *)s->vec[PB_V_WORDS].data;
    size_t nw = ((size_t)c->ncells + 31u) / 32u;
    bigint s1 = BI_ZERO_INIT, s2 = BI_ZERO_INIT, w = BI_ZERO_INIT, t = BI_ZERO_INIT;
    for (uint32_t i = 0; i < s->nsamples; i++) {
        const pb_sample *sm = &samples[i];
        if (sm->comp != ci) continue;
        if (rt_meter_work(meter, sm->mines + 1u)) return PB_BUDGET;
        int64_t j = (int64_t)sm->mines - (int64_t)c->hlo;
        if (j < 0 || j >= (int64_t)c->hlen || bi_is_zero(&c->weights[j])) continue;
        PB_TRYBI(bi_shl(pool, &w, &c->weights[j], sm->choices));
        PB_TRYBI(bi_mul_u32(pool, &t, &w, sm->occurrences));
        PB_TRYBI(bi_add(pool, &s1, &s1, &t));
        PB_TRYBI(bi_addmul(pool, &s2, &t, &w));
        const uint32_t *bits = words + sm->words;
        for (size_t k = 0; k < nw; k++) {
            uint32_t word = bits[k];
            while (word) {
                uint32_t v = (uint32_t)(k * 32u) + pb_lowest_bit(word);
                word &= word - 1u;
                PB_TRYBI(bi_add(pool, &c->numerators[v], &c->numerators[v], &t));
            }
        }
    }
    double ess = 0.0;
    if (!bi_is_zero(&s2)) {
        PB_TRYBI(bi_mul(pool, &w, &s1, &s1));
        PB_TRYBI(bi_ratio(pool, &w, &s2, &ess));
    }
    c->ess = ess;
    c->has_ess = true;
    bool total_ok = bi_eq(&s1, &s->z);
    bi_free(pool, &s1);
    bi_free(pool, &s2);
    bi_free(pool, &w);
    bi_free(pool, &t);
    return total_ok ? PB_OK : PB_BUG; /* sample weight mismatch */
}

/* Combination and numerators under the full deadline. Out of time or
 * memory is a failure reason; Z = 0 without sampled components means the
 * observation is inconsistent. */
static pb_rc pb_combine_all(pb_solver *s, uint32_t *failure) {
    rt_meter meter;
    rt_meter_init(&meter, s->clock, s->deadline_ms, RT_METER_INTERVAL);
    pb_rc rc = pb_combine(s, &meter);
    if (rc == PB_OK && bi_is_zero(&s->z)) {
        if (s->hard_count == 0) return PB_UNSAT;
        *failure = MS_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES;
        return PB_OK;
    }
    for (uint32_t k = 0; k < s->ncomp && rc == PB_OK; k++) {
        pb_comp *c = s->comp[k];
        rc = c->mode == PB_EXACT ? pb_backward(s, c, &meter)
                                 : pb_sample_numerators(s, k, c, &meter);
    }
    if (rc == PB_BUDGET) {
        *failure = MS_REASON_TIME_BUDGET_EXHAUSTED;
        return PB_OK;
    }
    if (rc == PB_NOMEM) {
        *failure = MS_REASON_MEMORY_BUDGET_EXHAUSTED;
        return PB_OK;
    }
    if (rc != PB_OK) return rc;
    s->combined = true;
    return PB_OK;
}

/* ======================================================================
 * Stage 6: statuses, proofs and the result buffer
 * ====================================================================== */

static void pb_prove(double *p, uint8_t *f, uint32_t v, bool mine) {
    p[v] = mine ? 1.0 : 0.0;
    f[v] = (uint8_t)(MS_PCELL_VALUE | (mine ? MS_PCELL_PROVEN_MINE : MS_PCELL_PROVEN_SAFE));
}

static void pb_estimate(double *p, uint8_t *f, uint32_t v, double value) {
    p[v] = value;
    f[v] = MS_PCELL_VALUE;
}

/* Python reports meta values with 0.001 resolution. */
static double pb_round3(double x) {
    return x >= 0.0 && x < 1e12 ? base_round(x * 1000.0) / 1000.0 : x;
}

static double pb_elapsed(double start_ms, double end_ms) {
    double elapsed = end_ms - start_ms;
    if (!(elapsed >= 0.0)) return 0.0;
    if (elapsed - elapsed != 0.0) return 1e300; /* infinite clock span: keep it finite */
    return pb_round3(elapsed);
}

/* Writes the result. `values`: every hidden cell gets its estimate (EXACT,
 * APPROXIMATE); otherwise only proven cells carry a value (UNAVAILABLE).
 * `exact_proofs`: an unavailable result whose exact computation completed
 * still lists the integer-proven cells. Fails only while converting
 * estimates (PB_NOMEM / PB_BUG), never in the proofs-only mode. */
static pb_rc pb_write(pb_solver *s, void *result, uint64_t hash, bool values, bool exact_proofs) {
    base_memset(result, 0, ms_result_size(s->width, s->height));
    ms_result_header *r = (ms_result_header *)result;
    r->magic = MS_MAGIC_RESULT;
    r->version = MS_ABI_VERSION;
    r->status = s->status;
    r->reason = s->reason;
    r->width = s->width;
    r->height = s->height;
    r->total_mines = s->total;
    r->revealed = s->revealed;
    r->observation_hash = hash;
    r->hidden_cells = s->n - s->revealed;
    r->remaining_mines = s->propagated ? s->remaining : s->total;
    r->pair_reasoning_complete = s->propagated && s->pairs_complete ? 1u : 0u;
    r->nodes = s->nodes_used > UINT32_MAX ? UINT32_MAX : (uint32_t)s->nodes_used;
    double *p = ms_result_probabilities(result);
    uint8_t *f = ms_result_flags(result);
    bool exact = s->status == MS_PROB_EXACT;

    uint32_t propagated = 0;
    if (s->propagated) {
        for (uint32_t v = 0; v < s->n; v++) {
            if (s->clue[v] != MS_CLUE_HIDDEN || s->val[v] < 0) continue;
            propagated++;
            pb_prove(p, f, v, s->val[v] == 1);
        }
    }
    r->propagated_cells = propagated;

    if (s->split) {
        uint32_t frontier = 0, samples = 0, attempts = 0;
        bool has_ess = false;
        double ess = 0.0;
        for (uint32_t k = 0; k < s->ncomp; k++) {
            const pb_comp *c = s->comp[k];
            frontier += c->ncells;
            if (c->sampler) {
                samples += c->successes;
                attempts += c->attempts;
            }
            if (c->has_ess && (!has_ess || c->ess < ess)) {
                ess = c->ess;
                has_ess = true;
            }
        }
        r->frontier_cells = frontier;
        r->components = s->ncomp;
        r->unconstrained_cells = s->pool_cells;
        r->samples = samples;
        r->exact_components = s->exact_count;
        r->sampled_components = s->hard_count;
        r->sample_attempts = attempts;
        r->has_effective_sample_size = has_ess ? 1u : 0u;
        r->effective_sample_size = has_ess ? pb_round3(ess) : 0.0;

        if (values || exact_proofs) {
            if (!s->combined) return PB_BUG;
            for (uint32_t k = 0; k < s->ncomp; k++) {
                const pb_comp *c = s->comp[k];
                if (c->mode == PB_EXACT) {
                    for (uint32_t g = 0; g < c->ngroups; g++) {
                        const bigint *num = &c->numerators[g];
                        bool proven = exact || exact_proofs;
                        int certain = !proven ? -1 : bi_is_zero(num) ? 0 : bi_eq(num, &s->z) ? 1 : -1;
                        double value = 0.0;
                        if (certain < 0 && values) {
                            PB_TRYBI(bi_probability(&s->pool, num, &s->z, exact, &value));
                        }
                        for (uint32_t t = c->group_start[g]; t < c->group_start[g + 1]; t++) {
                            uint32_t v = c->cells[c->group_vars[t]];
                            if (certain >= 0) {
                                pb_prove(p, f, v, certain == 1);
                            } else if (values) {
                                pb_estimate(p, f, v, value);
                            }
                        }
                    }
                } else if (values) {
                    for (uint32_t lv = 0; lv < c->ncells; lv++) {
                        double value;
                        PB_TRYBI(bi_probability(&s->pool, &c->numerators[lv], &s->z, false, &value));
                        pb_estimate(p, f, c->cells[lv], value);
                    }
                }
            }
            if (s->pool_cells) {
                bool proven = exact || exact_proofs;
                int certain = !proven ? -1
                              : bi_is_zero(&s->bulk_num) ? 0
                              : bi_eq(&s->bulk_num, &s->bulk_den) ? 1
                                                                   : -1;
                double value = 0.0;
                if (certain < 0 && values) {
                    PB_TRYBI(bi_probability(&s->pool, &s->bulk_num, &s->bulk_den, exact, &value));
                }
                for (uint32_t t = 0; t < s->pool_cells; t++) {
                    if (certain >= 0) {
                        pb_prove(p, f, s->bulk[t], certain == 1);
                    } else if (values) {
                        pb_estimate(p, f, s->bulk[t], value);
                    }
                }
            }
        }
        if (!exact) {
            /* Only exhaustively counted components prove cells here. */
            for (uint32_t k = 0; k < s->ncomp; k++) {
                const pb_comp *c = s->comp[k];
                if (c->mode != PB_EXACT) continue;
                for (uint32_t g = 0; g < c->ngroups; g++) {
                    if (c->verdict[g] == PB_VERDICT_FREE) continue;
                    for (uint32_t t = c->group_start[g]; t < c->group_start[g + 1]; t++) {
                        pb_prove(p, f, c->cells[c->group_vars[t]], c->verdict[g] == PB_VERDICT_MINE);
                    }
                }
            }
        }
    }

    uint32_t safe = 0, mines = 0;
    for (uint32_t v = 0; v < s->n; v++) {
        safe += (f[v] & MS_PCELL_PROVEN_SAFE) != 0;
        mines += (f[v] & MS_PCELL_PROVEN_MINE) != 0;
    }
    r->proven_safe = safe;
    r->proven_mines = mines;
    return PB_OK;
}

static pb_rc pb_give_up(pb_solver *s, uint32_t reason) {
    s->status = MS_PROB_UNAVAILABLE;
    s->reason = reason;
    return PB_OK;
}

/* Python's _Solver.run: stages 1-5 and the status/reason decision. */
static pb_rc pb_run(pb_solver *s) {
    pb_rc rc = pb_build(s);
    if (rc == PB_NOMEM) return pb_give_up(s, MS_REASON_MEMORY_BUDGET_EXHAUSTED);
    if (rc != PB_OK) return rc;
    PB_TRY(pb_propagate(s));
    s->propagated = true;
    s->remaining = s->total - s->fixed_mines;
    rc = pb_split(s);
    if (rc == PB_NOMEM) return pb_give_up(s, MS_REASON_MEMORY_BUDGET_EXHAUSTED);
    if (rc != PB_OK) return rc;
    s->split = true;

    double forward_ms = 0.0;
    uint64_t known_min = 0;
    PB_TRY(pb_count_all(s, &forward_ms, &known_min));
    for (uint32_t k = 0; k < s->ncomp; k++) {
        if (s->comp[k]->mode == PB_EXACT) {
            s->exact_count++;
        } else {
            s->hard_count++;
        }
    }

    uint32_t failure = MS_REASON_NONE;
    if (s->hard_count) {
        double sample_deadline = HUGE_VAL;
        if (!s->infinite) {
            double share = rt_deadline(s->start_ms, s->budget_ms, PB_SAMPLE_FRACTION);
            double reserve = s->deadline_ms - PB_BACKWARD_TIME_FACTOR * forward_ms -
                             PB_RESERVE_FRACTION * s->budget_ms;
            sample_deadline = share < reserve ? share : reserve;
        }
        if (s->sample_budget == 0) {
            failure = MS_REASON_SAMPLING_BUDGET_EXHAUSTED;
        } else if (rt_clock_now(s->clock) >= sample_deadline) {
            failure = MS_REASON_TIME_BUDGET_EXHAUSTED;
        } else {
            rc = pb_sample_all(s, sample_deadline, known_min, &failure);
            if (rc == PB_NOMEM) {
                failure = MS_REASON_MEMORY_BUDGET_EXHAUSTED;
            } else if (rc != PB_OK) {
                return rc;
            }
        }
    }
    if (failure == MS_REASON_NONE) PB_TRY(pb_combine_all(s, &failure));
    if (failure == MS_REASON_NONE && s->hard_count) {
        double min_ess = HUGE_VAL;
        for (uint32_t k = 0; k < s->ncomp; k++) {
            const pb_comp *c = s->comp[k];
            if (c->mode == PB_SAMPLED && c->ess < min_ess) min_ess = c->ess;
        }
        if (min_ess < s->min_ess) failure = MS_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES;
    }
    if (failure != MS_REASON_NONE) {
        s->status = MS_PROB_UNAVAILABLE;
        s->reason = failure;
    } else if (s->hard_count) {
        s->status = MS_PROB_APPROXIMATE;
        s->reason = MS_REASON_COUNTING_BUDGET_EXCEEDED;
    } else {
        s->status = MS_PROB_EXACT;
        s->reason = MS_REASON_NONE;
    }
    return PB_OK;
}

/* Writes the result; if converting the estimates runs out of memory, the
 * answer becomes UNAVAILABLE (memory) with every sound proof kept. */
static pb_rc pb_finish(pb_solver *s, void *result, uint64_t hash) {
    pb_rc rc = pb_write(s, result, hash, s->status != MS_PROB_UNAVAILABLE, false);
    if (rc == PB_NOMEM) {
        bool exact_proofs = s->status == MS_PROB_EXACT;
        s->status = MS_PROB_UNAVAILABLE;
        s->reason = MS_REASON_MEMORY_BUDGET_EXHAUSTED;
        rc = pb_write(s, result, hash, false, exact_proofs);
    }
    if (rc != PB_OK) return rc;
    ((ms_result_header *)result)->elapsed_ms = pb_elapsed(s->start_ms, rt_clock_now(s->clock));
    return PB_OK;
}

static void pb_cleanup(pb_solver *s, rt_mark mark) {
    for (uint32_t id = 0; id < PB_V_COUNT; id++) {
        if (s->vec[id].data != NULL) rt_free(s->mem, s->vec[id].data);
        s->vec[id].data = NULL;
        s->vec[id].cap = 0;
    }
    for (uint32_t k = 0; k < s->ncomp; k++) bi_pool_reset(&s->comp[k]->pool);
    bi_pool_reset(&s->pool);
    rt_mem_rewind(s->mem, mark);
}

/* [ptr, ptr + len) as integers; false when the range is empty or wraps
 * the address space (the reactor's ms_host_range_ok rule; NULL pointers are
 * refused before). Addresses only: nothing is read. */
static bool pb_range(const void *ptr, size_t len, uintptr_t *start, uintptr_t *end) {
    uintptr_t at = (uintptr_t)ptr;
    if (len == 0 || (uintptr_t)len > MS_UINTPTR_MAX - at) return false;
    *start = at;
    *end = at + (uintptr_t)len;
    return true;
}

/* Whether two valid ranges share a byte (ms_host_disjoint negated:
 * adjacent ranges are disjoint). */
static bool pb_overlap(uintptr_t a0, uintptr_t a1, uintptr_t b0, uintptr_t b1) {
    return !(a1 <= b0 || b1 <= a0);
}

/* Buffer/pointer problems first (engine.h), then the observation, the result
 * length and the limits. Before any caller byte is read or written, the
 * observation, limits and result ranges must not wrap and must be pairwise
 * disjoint, and none may cover the workspace or clock structs the solve
 * mutates: an aliased request would corrupt its own input (the result is
 * zeroed and written while clues are still read), so it is refused rather
 * than given an in-place meaning. A rejected call changes nothing. */
static ms_status pb_check_args(const void *obs, size_t obs_len, const ms_infer_limits *limits,
                               const rt_mem *workspace, const rt_clock *clock,
                               const void *result, size_t result_len) {
    if (obs == NULL || limits == NULL || result == NULL || workspace == NULL || clock == NULL ||
        ((uintptr_t)obs & 7u) != 0 || ((uintptr_t)limits & 7u) != 0 ||
        ((uintptr_t)result & 7u) != 0) {
        return MS_ERR_INVALID_BUFFER;
    }
    uintptr_t o0, o1, l0, l1, r0, r1, w0, w1, c0, c1;
    if (!pb_range(obs, obs_len, &o0, &o1) || !pb_range(limits, sizeof(*limits), &l0, &l1) ||
        !pb_range(result, result_len, &r0, &r1) ||
        !pb_range(workspace, sizeof(*workspace), &w0, &w1) ||
        !pb_range(clock, sizeof(*clock), &c0, &c1)) {
        return MS_ERR_INVALID_BUFFER;
    }
    if (pb_overlap(o0, o1, l0, l1) || pb_overlap(r0, r1, o0, o1) || pb_overlap(r0, r1, l0, l1) ||
        pb_overlap(o0, o1, w0, w1) || pb_overlap(o0, o1, c0, c1) || pb_overlap(l0, l1, w0, w1) ||
        pb_overlap(l0, l1, c0, c1) || pb_overlap(r0, r1, w0, w1) || pb_overlap(r0, r1, c0, c1)) {
        return MS_ERR_INVALID_BUFFER;
    }
    if (limits->magic != MS_MAGIC_LIMITS || limits->version != MS_ABI_VERSION) {
        return MS_ERR_INVALID_BUFFER;
    }
    ms_status status = ms_obs_validate(obs, obs_len);
    if (status != MS_OK) return status;
    const ms_obs_header *h = (const ms_obs_header *)obs;
    if (result_len != ms_result_size(h->width, h->height)) return MS_ERR_INVALID_BUFFER;
    return ms_limits_validate(limits);
}

ms_status ms_solve(const void *obs, size_t obs_len, const ms_infer_limits *limits,
                   rt_mem *workspace, rt_clock *clock, void *result, size_t result_len) {
    ms_status status = pb_check_args(obs, obs_len, limits, workspace, clock, result, result_len);
    if (status != MS_OK) return status;
    const ms_obs_header *h = (const ms_obs_header *)obs;
    status = pb_quick_contradictions(h, ms_obs_clues(obs));
    if (status != MS_OK) return status;

    pb_solver solver;
    pb_solver *s = &solver;
    base_memset(s, 0, sizeof(*s));
    s->mem = workspace;
    s->clock = clock;
    s->width = h->width;
    s->height = h->height;
    s->n = h->width * h->height;
    s->total = h->total_mines;
    s->revealed = h->revealed;
    s->clue = ms_obs_clues(obs);
    s->node_budget = limits->node_budget;
    s->sample_budget = limits->sample_budget;
    s->store_budget = limits->max_stored_entries;
    s->min_ess = limits->min_effective_samples;
    s->budget_ms = limits->time_budget_ms;
    s->infinite = !(limits->time_budget_ms < HUGE_VAL);
    uint64_t hash = ms_obs_hash(obs);
    rt_rng_seed(&s->rng, (limits->flags & MS_LIMIT_EXPLICIT_SEED) ? limits->seed : hash);
    bi_init(&s->z);
    bi_init(&s->bulk_num);
    bi_init(&s->bulk_den);

    s->start_ms = rt_clock_now(clock);
    s->deadline_ms = rt_deadline(s->start_ms, s->budget_ms, 1.0);
    size_t bytes = limits->memory_budget_bytes > (uint64_t)SIZE_MAX
                       ? SIZE_MAX
                       : (size_t)limits->memory_budget_bytes;
    size_t saved_budget = rt_mem_limit(workspace, bytes);
    rt_mark mark = rt_mem_mark(workspace);
    s->misuses0 = workspace->misuses;
    bi_pool_init(&s->pool, workspace);

    pb_rc rc = pb_run(s);
    if (rc == PB_OK) rc = pb_finish(s, result, hash);
    status = rc == PB_OK ? MS_OK : rc == PB_UNSAT ? MS_ERR_INCONSISTENT : MS_ERR_INTERNAL;

    pb_cleanup(s, mark);
    rt_mem_set_budget(workspace, saved_budget);
    if (workspace->misuses != s->misuses0) status = MS_ERR_INTERNAL;
    return status;
}
