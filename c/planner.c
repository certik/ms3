/*
 * planner.c - ms_plan: advice for the next reveal, chosen to maximize the
 * probability of winning the whole game, from the public observation alone.
 *
 * The planner reads one observation (dimensions, mine total and the clues of
 * revealed safe cells: never flags, the real layout or the game's RNG) and
 * writes one fixed 112-byte ms_plan_result. It never touches a game, never
 * moves and never produces proofs; autosolve keeps using ms_solve's.
 *
 *  1. Placeholders. No clue yet: NONE/NOT_STARTED. Every hidden cell a mine
 *     (the clues are checked to agree): NONE/FINISHED.
 *  2. ms_posterior_generate supplies the solver result and complete layouts:
 *     every compatible layout once when there are at most exact_layout_limit
 *     of them (exhaustive), otherwise equal-weight draws. A proven-safe hidden
 *     cell, or a hidden cell safe in every layout of an exhaustive posterior,
 *     gives NONE/CERTAIN_MOVES: reveal those, no guess is needed.
 *  3. Exact endgame search (exhaustive posteriors; ex_* below): a belief-
 *     state search over sets of equally likely layouts that maximizes the
 *     integer number of layouts won. Only a completed search is EXACT.
 *  4. Otherwise guided rollouts (pl_* below): a diversity shortlist of
 *     candidate first reveals, each played to the end of the game on the
 *     same sampled boards in paired rounds by a continuation policy that
 *     sees only the simulated public observation. ESTIMATED reports the
 *     fraction of completed rounds won; too few completed rounds are
 *     UNAVAILABLE. Sampled certainty is never reported as proof.
 *
 * Budgets: one wall-clock deadline (time_budget_ms from entry) and one byte
 * budget (rt_mem_limit, restored before returning) cover posterior
 * generation, the exact search and every rollout, including the inference
 * the continuation policy runs on simulated observations. That nested work
 * sees the deadline through a virtual clock and is cancelled by it inside,
 * not only between steps; the round it belonged to is then discarded, so
 * no policy decision depends on time. Node, candidate, sample and step
 * limits are work caps. Every exhausted budget degrades the answer (exact
 * -> estimated -> unavailable); none is an error.
 *
 * Memory: everything comes from the caller's workspace. Fixed arrays are
 * rt_bump'ed after an entry mark and rewound at the end; the exact search's
 * growable memo and frame stack are rt_alloc'ed and freed. Large work is
 * iterative (the exact search keeps an explicit frame stack), so no input
 * can exhaust the C stack.
 */

#include "planner.h"
#include "posterior.h"
#include "runtime.h"

#include <base/math.h>
#include <base/mem.h>

#define PL_NONE UINT32_MAX
/* A cell's outcome when revealed: clue 0..8, or this for a mine. */
#define PL_MINE 9u
#define PL_VIEWS 10u

/* Time shares: the root posterior gets this part of the budget; an exact
 * search gets this part of what is left (the rest stays for rollouts). */
#define PL_POSTERIOR_SHARE 0.4
#define PL_EXACT_SHARE 0.5
/* The root layouts may use at most 1/PL_LAYOUT_SHARE of the byte budget. */
#define PL_LAYOUT_SHARE 4u
/* Rollout steps between amortized clock reads (inference reads it too). */
#define PL_METER_INTERVAL 256u

/* Continuation-policy inference: deterministic work caps. Nested work runs
 * on a virtual clock that reads 0 until the shared deadline passes, so its
 * phases never time out early; a deadline inside it discards the round. */
#define PL_ROLL_NODES 20000u
#define PL_ROLL_SAMPLES 256u
#define PL_ROLL_MIN_ESS 16.0
/* Exact endgame tail of the continuation policy: at most this many layouts
 * and search nodes. */
#define PL_TAIL_LAYOUTS 32u
#define PL_TAIL_NODES 1024u
/* The virtual clock (pl_vclock): nested budgets end at PL_NESTED_MS of
 * virtual time, never reached while it reads 0; after the deadline its
 * readings start at PL_LATE_MS and grow by more than PL_NESTED_MS each, so
 * every nested deadline passes at the next check, whenever it began. */
#define PL_NESTED_MS 1e12
#define PL_LATE_MS 1e15
#define PL_LATE_STEP_MS 4e12
/* Separates the round-order stream from every other seed use. */
#define PL_ROUND_STREAM 0x726F756E64730000ull /* "rounds" */
/* Guess probabilities this close are ties (rounding, not sampling noise). */
#define PL_TIE 1e-12

/* Shortlist rankings (pl_cand_before). */
#define PL_RANK_SAFE 0u
#define PL_RANK_OPEN 1u
#define PL_RANK_INFO 2u
#define PL_RANK_INTERIOR 3u
#define PL_RANK_FRONTIER 4u
#define PL_RANKS 5u
/* Non-safety picks need at least this fraction of the best survival. */
#define PL_SHORTLIST_FLOOR 0.5
/* Largest double below 1: estimated survival never claims certainty. */
#define PL_SURVIVAL_MAX (1.0 - 1.0 / 9007199254740992.0)
/* With sampled rounds, a candidate replaces the safest one only when its
 * paired advantage (rounds it won and the safest lost, minus the reverse)
 * exceeds this many standard deviations of that difference (McNemar). */
#ifndef PL_SWITCH_Z
#define PL_SWITCH_Z 1.0
#endif

/* Policy knowledge per cell, derived from public clues only. */
#define PL_K_UNKNOWN 0u
#define PL_K_SAFE 1u
#define PL_K_MINE 2u

/* Simulated game outcomes. */
#define PL_LOST 0u
#define PL_WON 1u
#define PL_CUT 2u   /* rollout_step_limit reached: unknown ending */
#define PL_TIMED 3u /* the shared deadline passed: unknown ending */

/* Internal outcomes; only PL_FAIL becomes an ms_plan error. */
typedef enum pl_rc {
    PL_OK = 0,
    PL_STOP,  /* the deadline or a work cap was reached */
    PL_NOMEM, /* an allocation failed: byte budget, injection or host */
    PL_FAIL   /* an error status to return (pl_ctx.fail) */
} pl_rc;

#define PL_TRY(expr)                                                                       \
    do {                                                                                   \
        pl_rc pl_try_ = (expr);                                                            \
        if (pl_try_ != PL_OK) return pl_try_;                                              \
    } while (0)

_Static_assert(sizeof(size_t) == sizeof(uintptr_t), "caller ranges use one width");

/* ======================================================================
 * Call context
 * ====================================================================== */

/* Clock of nested work in rollouts: 0 until the caller's clock passes the
 * deadline, then late and increasing; `tripped` records the passing. */
typedef struct pl_vclock {
    rt_clock *real;
    double deadline_ms;
    double late_ms;
    bool tripped;
} pl_vclock;

typedef struct pl_ctx {
    rt_mem *mem;
    rt_clock *clock;
    pl_vclock vclock;
    rt_clock nested;        /* reads vclock */
    uint32_t aborts_inference; /* rounds discarded inside nested work */
    uint32_t aborts_tail;
    const ms_plan_limits *limits;
    uint64_t misuses0;
    size_t saved_budget;
    rt_mark mark;
    ms_status fail;
    size_t memory;          /* the planner's byte budget */
    double start_ms;
    double deadline_ms;
    rt_meter meter;         /* the shared deadline */
    /* the observation */
    const void *obs;
    size_t obs_len;
    const uint8_t *clue;
    uint32_t width;
    uint32_t height;
    uint32_t cells;
    uint32_t total;
    uint32_t revealed;
    uint32_t hidden;
    uint64_t hash;
    /* grid neighbors: nbr[8 * cell + i], i < nbr_n[cell], ascending */
    uint32_t *nbr;
    uint8_t *nbr_n;
} pl_ctx;

/* Per-candidate rollout figures (test statistics only). */
typedef struct pl_stats {
    uint32_t candidates;
    uint32_t rounds;
    uint32_t cell[64];
    uint32_t wins[64];
    uint32_t played[64];
    uint32_t gain[64];
    uint32_t loss[64];
    double survival[64];
    uint32_t exhaustive;
    uint32_t order[64];
    uint64_t round_wins[64];
    uint32_t aborts_inference;
    uint32_t aborts_tail;
} pl_stats;

static double pl_vclock_read(void *data) {
    pl_vclock *v = (pl_vclock *)data;
    if (!v->tripped) {
        if (!(rt_clock_now(v->real) > v->deadline_ms)) return 0.0;
        v->tripped = true;
        return v->late_ms;
    }
    v->late_ms += PL_LATE_STEP_MS;
    return v->late_ms;
}

static pl_rc pl_fail(pl_ctx *ctx, ms_status status) {
    ctx->fail = status;
    return PL_FAIL;
}

/* Test-only fault injection into nested rollout work (ms_plan_test_set_fault;
 * the production build has none of it). */
#define PL_FAULT_INFER 0u
#define PL_FAULT_TAIL 1u
#if MS_TEST_SUITE_PLANNER
static ms_plan_test_fault pl_fault;
static uint32_t pl_fault_calls[2]; /* nested calls in this ms_plan so far */

/* Whether this nested call is the one to break; when asked, the deadline
 * passes inside it first (at its first clock reading). */
static bool pl_fault_enter(pl_ctx *ctx, uint32_t kind) {
    uint32_t at = kind == PL_FAULT_INFER ? pl_fault.inference_at : pl_fault.tail_at;
    if (++pl_fault_calls[kind] != at) return false;
    if (pl_fault.trip) ctx->vclock.deadline_ms = -HUGE_VAL;
    return true;
}
#define PL_FAULT_ENTER(kind) bool fault = pl_fault_enter(ctx, kind)
#define PL_FAULT_STATUS(status, res)                                               \
    do {                                                                           \
        if (fault && pl_fault.fail == 1u) status = MS_ERR_INTERNAL;                \
        if (fault && pl_fault.fail == 2u) ((ms_result_header *)(res))->magic ^= 1u; \
    } while (0)
#define PL_FAULT_RC(rc)                                                            \
    do {                                                                           \
        if (fault && pl_fault.fail != 0 && (rc) == PL_OK) {                        \
            rc = pl_fail(ctx, MS_ERR_INTERNAL);                                    \
        }                                                                          \
    } while (0)
#else
#define PL_FAULT_ENTER(kind) ((void)0)
#define PL_FAULT_STATUS(status, res) ((void)0)
#define PL_FAULT_RC(rc) ((void)0)
#endif

/* Why the last allocation returned NULL: misuse of the workspace is a bug,
 * anything else (budget, injection, host) is exhaustion. */
static pl_rc pl_nomem(pl_ctx *ctx) {
    return ctx->mem->misuses != ctx->misuses0 ? pl_fail(ctx, MS_ERR_INTERNAL) : PL_NOMEM;
}

/* Zeroed bump storage, released by the caller's rewind. */
static void *pl_bump(pl_ctx *ctx, size_t count, size_t size) {
    return rt_bump_array(ctx->mem, count ? count : 1u, size);
}

#define PL_NEW(ctx, ptr, count) (((ptr) = pl_bump((ctx), (count), sizeof(*(ptr)))) != NULL)

static inline uint32_t pl_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static inline bool pl_finite(double x) {
    return x - x == 0.0; /* false for NaN and infinities */
}

static uint32_t pl_popcount(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return (uint32_t)((x * 0x0101010101010101ull) >> 56);
}

/* Index of the lowest set bit of a nonzero word (de Bruijn). */
static uint32_t pl_lowest_bit(uint64_t x) {
    static const uint8_t table[64] = {
        0,  1,  48, 2,  57, 49, 28, 3,  61, 58, 50, 42, 38, 29, 17, 4,
        62, 55, 59, 36, 53, 51, 43, 22, 45, 39, 33, 30, 24, 18, 12, 5,
        63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21, 44, 32, 23, 11,
        46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9,  13, 8,  7,  6,
    };
    return table[((x & (0 - x)) * 0x03F79D71B4CB0A89ull) >> 58];
}

/* A deterministic square root for the standard-error diagnostic (corec's
 * fast_sqrt is approximate on some platforms): Newton's iteration after
 * scaling by powers of four, identical on every target. */
static double pl_sqrt(double x) {
    if (!(x > 0.0) || !pl_finite(x)) return 0.0;
    double scale = 1.0;
    while (x > 4.0) {
        x *= 0.25;
        scale *= 2.0;
    }
    while (x < 0.25) {
        x *= 4.0;
        scale *= 0.5;
    }
    double r = 1.0;
    for (uint32_t i = 0; i < 8u; i++) r = 0.5 * (r + x / r);
    return r * scale;
}

/* Stable bottom-up merge sort of uint32 items; tmp holds count items. */
typedef bool (*pl_before_fn)(const void *data, uint32_t a, uint32_t b, uint32_t key);

static void pl_sort(uint32_t *items, uint32_t *tmp, uint32_t count, pl_before_fn before,
                    const void *data, uint32_t key) {
    uint32_t *src = items;
    uint32_t *dst = tmp;
    for (uint32_t width = 1; width < count; width = width > count / 2u ? count : width * 2u) {
        for (uint32_t lo = 0; lo < count;) {
            uint32_t mid = count - lo > width ? lo + width : count;
            uint32_t hi = count - mid > width ? mid + width : count;
            uint32_t a = lo, b = mid, out = lo;
            while (a < mid && b < hi) {
                dst[out++] = before(data, src[b], src[a], key) ? src[b++] : src[a++];
            }
            while (a < mid) dst[out++] = src[a++];
            while (b < hi) dst[out++] = src[b++];
            lo = hi;
        }
        uint32_t *swap = src;
        src = dst;
        dst = swap;
    }
    if (src != items) base_memcpy(items, src, (size_t)count * sizeof(uint32_t));
}

/* ======================================================================
 * Exact endgame search
 *
 * Positions are sets S of the exhaustive posterior's layouts, all equally
 * likely. Revealing a cell that is safe in every layout of S never loses
 * and only refines what is known, so it never lowers the chance to win:
 * the search reveals all such cells at once ("closure"; the order does not
 * matter, the fixpoint is unique). A closed position is therefore
 * identified by S alone: its revealed cells are exactly the cells safe in
 * every layout of S, and S is every root layout agreeing with the clues
 * seen. The public history matters only through S; the layout the game
 * would really have is never consulted. The game's flood fill is subsumed:
 * the neighbors of a revealed 0 are safe in every layout that shows that 0,
 * so the closure reveals them, and their clues, as the flood would - each
 * observable outcome keeps its own branch.
 *
 * With V(S) the number of layouts of S an optimal policy wins:
 *   |S| = 1: V = 1 (everything safe gets revealed: won);
 *   otherwise every hidden cell holds a mine in some layout of S and
 *   V(S) = max over hidden cells c of sum over children T of V(T),
 * where the layouts of S in which c is safe split into children by the
 * clue c shows and then by everything the closure reveals. So
 * V(S) <= |S| - 1, and V(S) = 1 when |S| = 2.
 *
 * Exactness of the shortcuts: two cells that split S identically (same
 * mine layouts, same partition of the others) reach the same children, so
 * one represents both. Cells are tried by safe count; a cell whose safe
 * count, or whose children's bounds V(T) <= |T| - 1, cannot beat the best
 * value found is skipped (it could at most tie, and ties keep the earlier
 * cell: more layouts safe, then more distinct clues, then lower index).
 * Every completed value is exact and memoized by S. The search is
 * iterative (explicit frames on the workspace) and bounded by nodes, time
 * and bytes; an interrupted search proves nothing and is discarded.
 * ====================================================================== */

#define EX_EMPTY 0u /* memo value of an empty slot: values are >= 1 */
#define EX_MEMO_MIN 1024u
#define EX_MEMO_MAX ((uint32_t)1 << 30)

typedef struct ex_cand {
    uint64_t hash;  /* signature of the split it makes */
    uint32_t var;   /* varying-cell index */
    uint32_t safe;  /* layouts of the position in which it is safe */
    uint32_t parts; /* distinct clues it can show there */
    uint32_t reserved;
} ex_cand;

typedef struct ex_frame {
    const uint32_t *items; /* the position's layouts */
    uint32_t n;
    uint32_t ncand;
    uint64_t *uni;         /* varying cells holding a mine in some layout */
    ex_cand *cand;         /* distinct splits, best first */
    uint32_t ci;           /* candidates started */
    uint32_t cur;          /* the candidate being evaluated */
    uint32_t *kid_items;   /* its children's layouts, segment by segment */
    uint32_t *kid_start;
    uint32_t *kid_len;
    uint32_t *kid_val;     /* exact value, or the bound |T| - 1 */
    uint8_t *kid_exact;
    uint32_t nkids;
    uint32_t ki;           /* next child */
    uint32_t acc;          /* exact values so far */
    uint32_t rem;          /* bounds of the children still open */
    uint32_t best;
    uint32_t best_var;
    uint32_t best_safe;
    bool active;           /* evaluating cand[cur] */
    rt_mark mark;          /* before the frame's storage */
    rt_mark cand_mark;     /* before the current candidate's storage */
} ex_frame;

typedef struct ex_search {
    pl_ctx *ctx;
    uint32_t n;            /* root layouts */
    uint32_t nw;           /* words of a layout set */
    uint32_t k;            /* varying cells: hidden, a mine in some layouts only */
    uint32_t kw;           /* words of a varying-cell set */
    uint32_t *var_cell;    /* varying cell -> board cell, ascending */
    uint8_t *view;         /* [layout * k + var]: clue 0..8 or PL_MINE */
    uint64_t *mbits;       /* [layout * kw + word]: varying cells holding a mine */
    uint32_t *all;         /* 0 .. n - 1 */
    uint64_t *memo_key;    /* memo_cap * nw words (rt_alloc) */
    uint32_t *memo_val;    /* memo_cap values, EX_EMPTY when free (rt_alloc) */
    uint32_t memo_cap;
    uint32_t memo_count;
    ex_frame *frame;       /* explicit DFS stack (rt_grow) */
    size_t frame_cap;
    uint32_t depth;
    uint64_t *key;         /* scratch: one layout set */
    uint64_t *band;        /* scratch: AND of mine bits */
    uint64_t *upool;       /* closure unions, kw words each */
    uint32_t upool_cap;
    uint32_t upool_used;
    uint32_t *pend_s;      /* closure stack: segments and their parent union */
    uint32_t *pend_l;
    uint32_t *pend_u;
    uint32_t *seg_s;       /* refinement of one segment */
    uint32_t *seg_l;
    uint32_t *nseg_s;
    uint32_t *nseg_l;
    uint32_t *tmp;
    uint32_t *newly;
    uint32_t *dedup;       /* candidate dedupe slots */
    uint32_t dedup_cap;
    ex_cand *sort_tmp;
    uint64_t nodes;
    uint64_t node_limit;
    bool timed;
    rt_meter meter;
    uint32_t root_value;
    uint32_t root_var;
    uint32_t root_safe;
} ex_search;

/* Outcome of ex_run. */
typedef struct ex_answer {
    bool complete;         /* values below are exact */
    bool certain;          /* a hidden cell is safe in every layout: cell */
    uint32_t cell;         /* best reveal */
    uint32_t wins;         /* layouts the best policy wins */
    uint32_t safe;         /* layouts in which `cell` is safe */
    uint32_t total;        /* layouts */
    uint32_t candidates;   /* hidden cells safe in some layout */
    uint64_t nodes;        /* positions expanded */
} ex_answer;

static inline uint8_t ex_view(const ex_search *s, uint32_t layout, uint32_t var) {
    return s->view[(size_t)layout * s->k + var];
}

static inline const uint64_t *ex_mines(const ex_search *s, uint32_t layout) {
    return s->mbits + (size_t)layout * s->kw;
}

static uint64_t ex_hash_words(const uint64_t *w, uint32_t count) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ count;
    for (uint32_t i = 0; i < count; i++) h = rt_mix64(h ^ w[i]);
    return h;
}

static uint32_t ex_pow2_at_least(uint32_t x) {
    uint32_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

/* s->key = the set of `n` layouts listed in items. */
static void ex_set_key(ex_search *s, const uint32_t *items, uint32_t n) {
    base_memset(s->key, 0, (size_t)s->nw * sizeof(uint64_t));
    for (uint32_t i = 0; i < n; i++) s->key[items[i] >> 6] |= (uint64_t)1 << (items[i] & 63u);
}

/* Memoized exact value of the set in s->key (hash h), or EX_EMPTY. */
static uint32_t ex_memo_get(const ex_search *s, uint64_t h) {
    if (s->memo_cap == 0) return EX_EMPTY;
    uint32_t mask = s->memo_cap - 1u;
    size_t bytes = (size_t)s->nw * sizeof(uint64_t);
    for (uint32_t i = (uint32_t)h & mask;; i = (i + 1u) & mask) {
        uint32_t value = s->memo_val[i];
        if (value == EX_EMPTY) return EX_EMPTY;
        if (base_memcmp(s->memo_key + (size_t)i * s->nw, s->key, bytes) == 0) return value;
    }
}

static uint32_t ex_memo_slot(const uint64_t *keys, const uint32_t *vals, uint32_t cap,
                             uint32_t nw, const uint64_t *key, uint64_t h) {
    uint32_t mask = cap - 1u;
    size_t bytes = (size_t)nw * sizeof(uint64_t);
    for (uint32_t i = (uint32_t)h & mask;; i = (i + 1u) & mask) {
        if (vals[i] == EX_EMPTY || base_memcmp(keys + (size_t)i * nw, key, bytes) == 0) return i;
    }
}

static pl_rc ex_memo_grow(ex_search *s) {
    pl_ctx *ctx = s->ctx;
    if (s->memo_cap >= EX_MEMO_MAX) return PL_NOMEM;
    uint32_t cap = s->memo_cap ? s->memo_cap * 2u : EX_MEMO_MIN;
    size_t key_bytes;
    if (!rt_mul_size((size_t)cap * s->nw, sizeof(uint64_t), &key_bytes)) return PL_NOMEM;
    uint64_t *keys = (uint64_t *)rt_alloc(ctx->mem, key_bytes);
    if (keys == NULL) return pl_nomem(ctx);
    uint32_t *vals = (uint32_t *)rt_calloc(ctx->mem, cap, sizeof(uint32_t));
    if (vals == NULL) {
        rt_free(ctx->mem, keys);
        return pl_nomem(ctx);
    }
    size_t bytes = (size_t)s->nw * sizeof(uint64_t);
    for (uint32_t i = 0; i < s->memo_cap; i++) {
        if (s->memo_val[i] == EX_EMPTY) continue;
        const uint64_t *key = s->memo_key + (size_t)i * s->nw;
        uint32_t at = ex_memo_slot(keys, vals, cap, s->nw, key, ex_hash_words(key, s->nw));
        base_memcpy(keys + (size_t)at * s->nw, key, bytes);
        vals[at] = s->memo_val[i];
    }
    if (s->memo_key != NULL) rt_free(ctx->mem, s->memo_key);
    if (s->memo_val != NULL) rt_free(ctx->mem, s->memo_val);
    s->memo_key = keys;
    s->memo_val = vals;
    s->memo_cap = cap;
    return PL_OK;
}

/* Stores the value of the set in s->key (hash h). */
static pl_rc ex_memo_put(ex_search *s, uint64_t h, uint32_t value) {
    if ((uint64_t)(s->memo_count + 1u) * 2u > s->memo_cap) PL_TRY(ex_memo_grow(s));
    uint32_t at = ex_memo_slot(s->memo_key, s->memo_val, s->memo_cap, s->nw, s->key, h);
    if (s->memo_val[at] == EX_EMPTY) {
        base_memcpy(s->memo_key + (size_t)at * s->nw, s->key, (size_t)s->nw * sizeof(uint64_t));
        s->memo_count++;
    } else if (s->memo_val[at] != value) {
        return pl_fail(s->ctx, MS_ERR_INTERNAL); /* one position, two exact values */
    }
    s->memo_val[at] = value;
    return PL_OK;
}

/* The split `var` makes of the position: a hash of its outcomes relabelled
 * in first-occurrence order (mines kept apart), its mine count and the
 * number of distinct clues. */
static uint64_t ex_signature(const ex_search *s, const uint32_t *items, uint32_t n, uint32_t var,
                             uint32_t *mines, uint32_t *parts) {
    uint8_t label[PL_VIEWS];
    for (uint32_t x = 0; x < PL_VIEWS; x++) label[x] = 0xFFu;
    uint32_t next = 0, m = 0;
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t x = ex_view(s, items[i], var);
        uint8_t lab;
        if (x == PL_MINE) {
            m++;
            lab = 0xFEu;
        } else {
            if (label[x] == 0xFFu) label[x] = (uint8_t)next++;
            lab = label[x];
        }
        h = (h ^ lab) * 0x100000001B3ull;
    }
    *mines = m;
    *parts = next;
    return h;
}

/* Whether cells a and b split the position identically. */
static bool ex_same_split(const ex_search *s, const uint32_t *items, uint32_t n, uint32_t a,
                          uint32_t b) {
    uint8_t la[PL_VIEWS], lb[PL_VIEWS];
    for (uint32_t x = 0; x < PL_VIEWS; x++) la[x] = lb[x] = 0xFFu;
    uint8_t na = 0, nb = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t x = ex_view(s, items[i], a), y = ex_view(s, items[i], b);
        if ((x == PL_MINE) != (y == PL_MINE)) return false;
        if (x == PL_MINE) continue;
        if (la[x] == 0xFFu) la[x] = na++;
        if (lb[y] == 0xFFu) lb[y] = nb++;
        if (la[x] != lb[y]) return false;
    }
    return true;
}

/* Safe count desc, then distinct clues desc, then cell index asc. */
static bool ex_cand_before(const ex_cand *a, const ex_cand *b) {
    if (a->safe != b->safe) return a->safe > b->safe;
    if (a->parts != b->parts) return a->parts > b->parts;
    return a->var < b->var;
}

static void ex_sort(ex_cand *items, ex_cand *tmp, uint32_t count) {
    ex_cand *src = items;
    ex_cand *dst = tmp;
    for (uint32_t width = 1; width < count; width = width > count / 2u ? count : width * 2u) {
        for (uint32_t lo = 0; lo < count;) {
            uint32_t mid = count - lo > width ? lo + width : count;
            uint32_t hi = count - mid > width ? mid + width : count;
            uint32_t a = lo, b = mid, out = lo;
            while (a < mid && b < hi) {
                dst[out++] = ex_cand_before(&src[b], &src[a]) ? src[b++] : src[a++];
            }
            while (a < mid) dst[out++] = src[a++];
            while (b < hi) dst[out++] = src[b++];
            lo = hi;
        }
        ex_cand *swap = src;
        src = dst;
        dst = swap;
    }
    if (src != items) base_memcpy(items, src, (size_t)count * sizeof(ex_cand));
}

/* The distinct splits of the position on top of the stack, best first. */
static void ex_candidates(ex_search *s, ex_frame *f) {
    uint32_t mask = ex_pow2_at_least(2u * f->ncand + 1u) - 1u;
    for (uint32_t i = 0; i <= mask; i++) s->dedup[i] = PL_NONE;
    uint32_t count = 0;
    for (uint32_t w = 0; w < s->kw; w++) {
        uint64_t bits = f->uni[w] & ~s->band[w];
        while (bits) {
            uint32_t var = w * 64u + pl_lowest_bit(bits);
            bits &= bits - 1u;
            uint32_t mines, parts;
            uint64_t h = ex_signature(s, f->items, f->n, var, &mines, &parts);
            uint32_t at = (uint32_t)h & mask;
            bool dup = false;
            for (; s->dedup[at] != PL_NONE; at = (at + 1u) & mask) {
                const ex_cand *o = &f->cand[s->dedup[at]];
                if (o->hash == h && ex_same_split(s, f->items, f->n, o->var, var)) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            s->dedup[at] = count;
            ex_cand *c = &f->cand[count++];
            c->hash = h;
            c->var = var;
            c->safe = f->n - mines;
            c->parts = parts;
            c->reserved = 0;
        }
    }
    f->ncand = count;
    ex_sort(f->cand, s->sort_tmp, count);
}

/* Pushes the closed position `items` (n >= 3, not memoized). */
static pl_rc ex_push(ex_search *s, const uint32_t *items, uint32_t n) {
    pl_ctx *ctx = s->ctx;
    if (s->nodes >= s->node_limit) return PL_STOP;
    s->nodes++;
    uint64_t work = ((uint64_t)n * (s->kw + 1u)) >> 4;
    if (s->timed && rt_meter_work(&s->meter, work > 0xFFFFu ? 0xFFFFu : (uint32_t)work + 1u)) {
        return PL_STOP;
    }
    if (s->depth == s->frame_cap) {
        ex_frame *grown = (ex_frame *)rt_grow(ctx->mem, s->frame, &s->frame_cap,
                                              (size_t)s->depth + 1u, sizeof(ex_frame));
        if (grown == NULL) return pl_nomem(ctx);
        s->frame = grown;
    }
    ex_frame *f = &s->frame[s->depth];
    base_memset(f, 0, sizeof(*f));
    f->mark = rt_mem_mark(ctx->mem);
    f->items = items;
    f->n = n;
    f->best_var = PL_NONE;
    if (!PL_NEW(ctx, f->uni, s->kw)) return pl_nomem(ctx);
    const uint64_t *m0 = ex_mines(s, items[0]);
    for (uint32_t w = 0; w < s->kw; w++) f->uni[w] = s->band[w] = m0[w];
    for (uint32_t i = 1; i < n; i++) {
        const uint64_t *m = ex_mines(s, items[i]);
        for (uint32_t w = 0; w < s->kw; w++) {
            f->uni[w] |= m[w];
            s->band[w] &= m[w];
        }
    }
    uint32_t count = 0;
    for (uint32_t w = 0; w < s->kw; w++) count += pl_popcount(f->uni[w] & ~s->band[w]);
    if (!PL_NEW(ctx, f->cand, count)) return pl_nomem(ctx);
    f->ncand = count;
    ex_candidates(s, f);
    f->cand_mark = rt_mem_mark(ctx->mem);
    s->depth++;
    return PL_OK;
}

static void ex_emit(ex_frame *f, uint32_t start, uint32_t len) {
    f->kid_start[f->nkids] = start;
    f->kid_len[f->nkids] = len;
    f->nkids++;
}

/* Closure of the segment [start, start + len) of f->kid_items: the layouts
 * in which the candidate showed one clue. Each final segment is a closed
 * child position. Parent unions live in s->upool (slot 0: the position's
 * union without the revealed candidate). */
static pl_rc ex_close(ex_search *s, ex_frame *f, uint32_t start, uint32_t len) {
    uint32_t *items = f->kid_items;
    uint32_t np = 0;
    s->pend_s[np] = start;
    s->pend_l[np] = len;
    s->pend_u[np] = 0;
    np++;
    while (np > 0) {
        np--;
        uint32_t ps = s->pend_s[np], pl = s->pend_l[np], pu = s->pend_u[np];
        if (pl == 1u) {
            ex_emit(f, ps, 1u);
            continue;
        }
        if (s->upool_used >= s->upool_cap) return pl_fail(s->ctx, MS_ERR_INTERNAL);
        uint32_t slot = s->upool_used++;
        uint64_t *u = s->upool + (size_t)slot * s->kw;
        const uint64_t *up = s->upool + (size_t)pu * s->kw;
        const uint64_t *m0 = ex_mines(s, items[ps]);
        for (uint32_t w = 0; w < s->kw; w++) u[w] = m0[w];
        for (uint32_t i = 1; i < pl; i++) {
            const uint64_t *m = ex_mines(s, items[ps + i]);
            for (uint32_t w = 0; w < s->kw; w++) u[w] |= m[w];
        }
        uint32_t nn = 0;
        for (uint32_t w = 0; w < s->kw; w++) {
            uint64_t bits = up[w] & ~u[w];
            while (bits) {
                s->newly[nn++] = w * 64u + pl_lowest_bit(bits);
                bits &= bits - 1u;
            }
        }
        if (nn == 0) {
            s->upool_used--; /* closed: nothing new is safe */
            ex_emit(f, ps, pl);
            continue;
        }
        /* Reveal the newly safe cells: split by the clue each one shows. */
        uint32_t nseg = 1, unsplit = pl > 1u ? 1u : 0u;
        s->seg_s[0] = ps;
        s->seg_l[0] = pl;
        for (uint32_t j = 0; j < nn && unsplit > 0; j++) {
            uint32_t var = s->newly[j], next = 0;
            unsplit = 0;
            for (uint32_t g = 0; g < nseg; g++) {
                uint32_t gs = s->seg_s[g], gl = s->seg_l[g];
                if (gl == 1u) {
                    s->nseg_s[next] = gs;
                    s->nseg_l[next++] = 1u;
                    continue;
                }
                uint32_t count[PL_VIEWS], at[PL_VIEWS];
                for (uint32_t x = 0; x < PL_VIEWS; x++) count[x] = 0;
                for (uint32_t i = 0; i < gl; i++) count[ex_view(s, items[gs + i], var)]++;
                if (count[PL_MINE] != 0) return pl_fail(s->ctx, MS_ERR_INTERNAL);
                uint32_t sum = 0;
                for (uint32_t x = 0; x < PL_MINE; x++) {
                    at[x] = sum;
                    sum += count[x];
                }
                for (uint32_t i = 0; i < gl; i++) {
                    uint32_t item = items[gs + i];
                    s->tmp[at[ex_view(s, item, var)]++] = item;
                }
                base_memcpy(items + gs, s->tmp, (size_t)gl * sizeof(uint32_t));
                uint32_t off = 0;
                for (uint32_t x = 0; x < PL_MINE; x++) {
                    if (count[x] == 0) continue;
                    s->nseg_s[next] = gs + off;
                    s->nseg_l[next++] = count[x];
                    if (count[x] > 1u) unsplit++;
                    off += count[x];
                }
            }
            uint32_t *t = s->seg_s;
            s->seg_s = s->nseg_s;
            s->nseg_s = t;
            t = s->seg_l;
            s->seg_l = s->nseg_l;
            s->nseg_l = t;
            nseg = next;
        }
        if (s->timed) {
            uint64_t work = ((uint64_t)nn * pl) >> 4;
            if (rt_meter_work(&s->meter, work > 0xFFFFu ? 0xFFFFu : (uint32_t)work + 1u)) {
                return PL_STOP;
            }
        }
        for (uint32_t g = 0; g < nseg; g++) {
            if (s->seg_l[g] == 1u) {
                ex_emit(f, s->seg_s[g], 1u);
            } else {
                s->pend_s[np] = s->seg_s[g];
                s->pend_l[np] = s->seg_l[g];
                s->pend_u[np] = slot;
                np++;
            }
        }
    }
    return PL_OK;
}

/* Children of candidate c of the top frame, with exact values where known
 * (|T| <= 2 or memoized) and bounds |T| - 1 otherwise. */
static pl_rc ex_children(ex_search *s, ex_frame *f, const ex_cand *c) {
    pl_ctx *ctx = s->ctx;
    uint32_t safe = c->safe, var = c->var;
    if (!PL_NEW(ctx, f->kid_items, safe) || !PL_NEW(ctx, f->kid_start, safe) ||
        !PL_NEW(ctx, f->kid_len, safe) || !PL_NEW(ctx, f->kid_val, safe) ||
        !PL_NEW(ctx, f->kid_exact, safe)) {
        return pl_nomem(ctx);
    }
    uint32_t count[PL_VIEWS], at[PL_VIEWS];
    for (uint32_t x = 0; x < PL_VIEWS; x++) count[x] = 0;
    for (uint32_t i = 0; i < f->n; i++) count[ex_view(s, f->items[i], var)]++;
    uint32_t sum = 0;
    for (uint32_t x = 0; x < PL_MINE; x++) {
        at[x] = sum;
        sum += count[x];
    }
    if (sum != safe) return pl_fail(ctx, MS_ERR_INTERNAL);
    for (uint32_t i = 0; i < f->n; i++) {
        uint8_t x = ex_view(s, f->items[i], var);
        if (x != PL_MINE) f->kid_items[at[x]++] = f->items[i];
    }
    uint64_t *slot0 = s->upool;
    for (uint32_t w = 0; w < s->kw; w++) slot0[w] = f->uni[w];
    slot0[var >> 6] &= ~((uint64_t)1 << (var & 63u));
    s->upool_used = 1;
    f->nkids = 0;
    uint32_t start = 0;
    for (uint32_t x = 0; x < PL_MINE; x++) {
        if (count[x] == 0) continue;
        PL_TRY(ex_close(s, f, start, count[x]));
        start += count[x];
    }
    f->acc = 0;
    f->rem = 0;
    for (uint32_t j = 0; j < f->nkids; j++) {
        uint32_t len = f->kid_len[j];
        uint32_t value = 1u;
        bool exact = len <= 2u;
        if (!exact) {
            ex_set_key(s, f->kid_items + f->kid_start[j], len);
            value = ex_memo_get(s, ex_hash_words(s->key, s->nw));
            exact = value != EX_EMPTY;
            if (!exact) value = len - 1u;
        }
        f->kid_val[j] = value;
        f->kid_exact[j] = exact ? 1u : 0u;
        if (exact) {
            f->acc += value;
        } else {
            f->rem += value;
        }
    }
    f->ki = 0;
    return PL_OK;
}

/* Depth-first evaluation of the frame stack down to the root. */
static pl_rc ex_loop(ex_search *s) {
    pl_ctx *ctx = s->ctx;
    while (s->depth > 0) {
        ex_frame *f = &s->frame[s->depth - 1u];
        if (f->active) {
            while (f->ki < f->nkids && f->kid_exact[f->ki]) f->ki++;
            if (f->ki < f->nkids) {
                PL_TRY(ex_push(s, f->kid_items + f->kid_start[f->ki], f->kid_len[f->ki]));
                continue;
            }
            if (f->acc > f->best) {
                f->best = f->acc;
                f->best_var = f->cand[f->cur].var;
                f->best_safe = f->cand[f->cur].safe;
            }
            f->active = false;
            rt_mem_rewind(ctx->mem, f->cand_mark);
            continue;
        }
        if (f->ci < f->ncand && f->cand[f->ci].safe > f->best) {
            f->cur = f->ci++;
            PL_TRY(ex_children(s, f, &f->cand[f->cur]));
            if (f->acc + f->rem <= f->best) {
                rt_mem_rewind(ctx->mem, f->cand_mark);
            } else {
                f->active = true;
            }
            continue;
        }
        /* Every candidate tried or bounded: the exact value. No candidate
         * means every hidden cell is a mine throughout: won. */
        uint32_t value = f->ncand == 0 ? f->n : f->best;
        if (s->depth == 1u) {
            s->root_value = value;
            s->root_var = f->best_var;
            s->root_safe = f->best_safe;
            s->depth = 0;
            break;
        }
        ex_set_key(s, f->items, f->n);
        PL_TRY(ex_memo_put(s, ex_hash_words(s->key, s->nw), value));
        rt_mem_rewind(ctx->mem, f->mark);
        s->depth--;
        ex_frame *p = &s->frame[s->depth - 1u];
        p->acc += value;
        p->rem -= p->kid_val[p->ki];
        p->ki++;
        if (p->acc + p->rem <= p->best) {
            p->active = false;
            rt_mem_rewind(ctx->mem, p->cand_mark);
        }
    }
    return PL_OK;
}

/* Checks the layouts against the observation (defence in depth: the
 * generator checks them too), finds the varying cells and builds the
 * search tables. Sets ans->certain when a hidden cell is safe throughout. */
static pl_rc ex_setup(ex_search *s, const uint8_t *clue, const uint8_t *layouts,
                      ex_answer *ans) {
    pl_ctx *ctx = s->ctx;
    uint32_t cells = ctx->cells, n = s->n;
    uint32_t *mines;
    if (!PL_NEW(ctx, mines, cells)) return pl_nomem(ctx);
    for (uint32_t l = 0; l < n; l++) {
        const uint8_t *row = layouts + (size_t)l * cells;
        uint32_t total = 0;
        for (uint32_t c = 0; c < cells; c++) {
            if (row[c] > 1u || (row[c] && clue[c] != MS_CLUE_HIDDEN)) {
                return pl_fail(ctx, MS_ERR_INTERNAL);
            }
            if (row[c]) {
                mines[c]++;
                total++;
            } else if (clue[c] != MS_CLUE_HIDDEN) {
                uint32_t near = 0;
                for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) near += row[ctx->nbr[8u * c + i]];
                if (near != clue[c]) return pl_fail(ctx, MS_ERR_INTERNAL);
            }
        }
        if (total != ctx->total) return pl_fail(ctx, MS_ERR_INTERNAL);
    }
    uint32_t k = 0;
    for (uint32_t c = 0; c < cells; c++) {
        if (clue[c] != MS_CLUE_HIDDEN) continue;
        if (mines[c] == 0) {
            ans->certain = true;
            ans->cell = c;
            return PL_OK;
        }
        if (mines[c] < n) k++;
    }
    ans->candidates = k;
    if (k == 0) return pl_fail(ctx, MS_ERR_INTERNAL); /* nothing left to guess */
    s->k = k;
    s->kw = (k + 63u) / 64u;
    if (!PL_NEW(ctx, s->var_cell, k)) return pl_nomem(ctx);
    for (uint32_t c = 0, v = 0; c < cells; c++) {
        if (clue[c] == MS_CLUE_HIDDEN && mines[c] < n) s->var_cell[v++] = c;
    }
    size_t view_len, bit_len;
    if (!rt_mul_size(n, k, &view_len) || !rt_mul_size(n, s->kw, &bit_len)) return PL_NOMEM;
    if (!PL_NEW(ctx, s->view, view_len) || !PL_NEW(ctx, s->mbits, bit_len)) return pl_nomem(ctx);
    for (uint32_t l = 0; l < n; l++) {
        const uint8_t *row = layouts + (size_t)l * cells;
        uint8_t *view = s->view + (size_t)l * k;
        uint64_t *bits = s->mbits + (size_t)l * s->kw;
        for (uint32_t v = 0; v < k; v++) {
            uint32_t c = s->var_cell[v];
            if (row[c]) {
                view[v] = PL_MINE;
                bits[v >> 6] |= (uint64_t)1 << (v & 63u);
            } else {
                uint32_t near = 0;
                for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) near += row[ctx->nbr[8u * c + i]];
                view[v] = (uint8_t)near;
            }
        }
    }
    /* Distinct layouts (an exhaustive posterior lists each once): they agree
     * outside the varying cells, so their mine bits must differ. */
    uint32_t dcap = ex_pow2_at_least(2u * n);
    uint32_t *slots;
    if (!PL_NEW(ctx, slots, dcap)) return pl_nomem(ctx);
    for (uint32_t i = 0; i < dcap; i++) slots[i] = PL_NONE;
    size_t row_bytes = (size_t)s->kw * sizeof(uint64_t);
    for (uint32_t l = 0; l < n; l++) {
        uint32_t at = (uint32_t)ex_hash_words(ex_mines(s, l), s->kw) & (dcap - 1u);
        for (; slots[at] != PL_NONE; at = (at + 1u) & (dcap - 1u)) {
            if (base_memcmp(ex_mines(s, slots[at]), ex_mines(s, l), row_bytes) == 0) {
                return pl_fail(ctx, MS_ERR_INTERNAL);
            }
        }
        slots[at] = l;
    }
    s->nw = (n + 63u) / 64u;
    s->upool_cap = 2u * n + 2u;
    s->dedup_cap = ex_pow2_at_least(2u * k + 1u);
    size_t upool_len;
    if (!rt_mul_size(s->upool_cap, s->kw, &upool_len)) return PL_NOMEM;
    if (!PL_NEW(ctx, s->all, n) || !PL_NEW(ctx, s->key, s->nw) || !PL_NEW(ctx, s->band, s->kw) ||
        !PL_NEW(ctx, s->upool, upool_len) || !PL_NEW(ctx, s->pend_s, n) ||
        !PL_NEW(ctx, s->pend_l, n) || !PL_NEW(ctx, s->pend_u, n) || !PL_NEW(ctx, s->seg_s, n) ||
        !PL_NEW(ctx, s->seg_l, n) || !PL_NEW(ctx, s->nseg_s, n) || !PL_NEW(ctx, s->nseg_l, n) ||
        !PL_NEW(ctx, s->tmp, n) || !PL_NEW(ctx, s->newly, k) ||
        !PL_NEW(ctx, s->dedup, s->dedup_cap) || !PL_NEW(ctx, s->sort_tmp, k)) {
        return pl_nomem(ctx);
    }
    for (uint32_t l = 0; l < n; l++) s->all[l] = l;
    return PL_OK;
}

/* Exact search over the n distinct layouts (rows of `cells` 0/1 bytes) that
 * complete the observation with clues `clue`, timed on `clock` up to
 * deadline_ms. Interruption (nodes, the deadline, bytes) leaves
 * ans->complete false; only errors fail. Every byte it allocated is
 * released before returning. */
static pl_rc ex_run(pl_ctx *ctx, rt_clock *clock, const uint8_t *clue, const uint8_t *layouts,
                    uint32_t n, uint64_t node_limit, double deadline_ms, ex_answer *ans) {
    base_memset(ans, 0, sizeof(*ans));
    ans->cell = PL_NONE;
    ans->total = n;
    if (n == 0) return pl_fail(ctx, MS_ERR_INTERNAL);
    rt_mark mark = rt_mem_mark(ctx->mem);
    ex_search search;
    ex_search *s = &search;
    base_memset(s, 0, sizeof(*s));
    s->ctx = ctx;
    s->n = n;
    s->node_limit = node_limit;
    s->timed = deadline_ms < HUGE_VAL;
    rt_meter_init(&s->meter, clock, deadline_ms, RT_METER_INTERVAL);
    pl_rc rc = ex_setup(s, clue, layouts, ans);
    if (rc == PL_OK && ans->certain) {
        ans->complete = true;
    } else if (rc == PL_OK) {
        rc = s->timed && rt_meter_check(&s->meter) ? PL_STOP : ex_push(s, s->all, n);
        if (rc == PL_OK) rc = ex_loop(s);
        if (rc == PL_OK) {
            if (s->root_var == PL_NONE || s->root_var >= s->k) {
                rc = pl_fail(ctx, MS_ERR_INTERNAL);
            } else {
                ans->complete = true;
                ans->cell = s->var_cell[s->root_var];
                ans->wins = s->root_value;
                ans->safe = s->root_safe;
            }
        }
    }
    ans->nodes = s->nodes;
    if (s->memo_key != NULL) rt_free(ctx->mem, s->memo_key);
    if (s->memo_val != NULL) rt_free(ctx->mem, s->memo_val);
    if (s->frame != NULL) rt_free(ctx->mem, s->frame);
    rt_mem_rewind(ctx->mem, mark);
    if (rc == PL_STOP || rc == PL_NOMEM) {
        ans->complete = false;
        ans->cell = PL_NONE;
        rc = PL_OK;
    }
    return rc;
}

/* ======================================================================
 * Limits and results
 * ====================================================================== */

void ms_plan_limits_default(ms_plan_limits *limits) {
    if (limits == NULL) return;
    base_memset(limits, 0, sizeof(*limits));
    limits->magic = MS_MAGIC_PLAN_LIMITS;
    limits->version = MS_PLANNER_VERSION;
    limits->exact_layout_limit = MS_PLAN_DEFAULT_EXACT_LAYOUTS;
    limits->exact_node_limit = MS_PLAN_DEFAULT_EXACT_NODES;
    limits->sample_count = MS_PLAN_DEFAULT_SAMPLES;
    limits->candidate_limit = MS_PLAN_DEFAULT_CANDIDATES;
    limits->rollout_step_limit = MS_PLAN_DEFAULT_ROLLOUT_STEPS;
    limits->flags = 0;
    limits->time_budget_ms = MS_PLAN_DEFAULT_TIME_MS;
    limits->memory_budget_bytes = MS_PLAN_DEFAULT_MEMORY;
    limits->seed = 0;
    limits->min_rollouts = MS_PLAN_DEFAULT_MIN_ROLLOUTS;
    limits->reserved = 0;
}

ms_status ms_plan_limits_validate(const ms_plan_limits *limits) {
    if (limits == NULL || ((uintptr_t)limits & 7u) != 0) return MS_ERR_INVALID_BUFFER;
    if (limits->magic != MS_MAGIC_PLAN_LIMITS || limits->version != MS_PLANNER_VERSION) {
        return MS_ERR_INVALID_BUFFER;
    }
    if (limits->flags & ~(uint32_t)MS_PLAN_EXPLICIT_SEED) return MS_ERR_INVALID_LIMITS;
    if (!(limits->time_budget_ms >= 0.0)) return MS_ERR_INVALID_LIMITS; /* NaN too */
    if (limits->reserved != 0) return MS_ERR_INVALID_LIMITS;
    return MS_OK;
}

/* Fields NONE and UNAVAILABLE leave unused: no cell, no estimate. */
static bool pl_result_zero_estimates(const ms_plan_result *r) {
    return r->cell == MS_PLAN_NO_CELL && r->survival_probability == 0.0 &&
           r->win_probability == 0.0 && r->standard_error == 0.0 && r->exact_wins == 0 &&
           r->exact_total == 0;
}

/* The canonical encoding documented in planner.h ("Canonical results"). */
ms_status ms_plan_result_validate(const void *obs, size_t obs_len, const ms_plan_result *result,
                                  size_t result_len) {
    ms_status status = ms_obs_validate(obs, obs_len);
    if (status != MS_OK) return status;
    if (result == NULL || ((uintptr_t)result & 7u) != 0 || result_len != sizeof(ms_plan_result)) {
        return MS_ERR_INVALID_BUFFER;
    }
    const ms_plan_result *r = result;
    if (r->magic != MS_MAGIC_PLAN_RESULT || r->version != MS_PLANNER_VERSION) {
        return MS_ERR_INVALID_BUFFER;
    }
    const ms_obs_header *h = (const ms_obs_header *)obs;
    uint32_t cells = h->width * h->height;
    uint32_t hidden = cells - h->revealed;
    if (r->width != h->width || r->height != h->height || r->total_mines != h->total_mines ||
        r->revealed != h->revealed || r->observation_hash != ms_obs_hash(obs) ||
        r->reserved != 0) {
        return MS_ERR_INVALID_RESULT;
    }
    /* Every status. */
    if (!pl_finite(r->survival_probability) || !pl_finite(r->win_probability) ||
        !pl_finite(r->standard_error) || !pl_finite(r->elapsed_ms) || r->elapsed_ms < 0.0 ||
        r->posterior_exact > 1u || (r->layouts == 0 && r->posterior_exact != 0) ||
        r->incomplete > 1u || r->incomplete > r->trials || r->trials > r->layouts ||
        r->candidates > hidden || (r->trials > 0 && r->candidates == 0) ||
        (r->candidates > 0 && r->layouts == 0) ||
        /* The exact search runs only on a complete listing of >= 2 layouts. */
        (r->search_nodes > 0 && (r->layouts < 2u || r->posterior_exact != 1u))) {
        return MS_ERR_INVALID_RESULT;
    }
    bool guess = h->revealed > 0 && hidden > h->total_mines;
    bool cell_ok = r->cell < cells && ms_obs_clues(obs)[r->cell] == MS_CLUE_HIDDEN;
    switch (r->status) {
    case MS_PLAN_NONE: {
        bool reason_ok = (r->reason == MS_PLAN_REASON_NOT_STARTED && h->revealed == 0) ||
                         (r->reason == MS_PLAN_REASON_FINISHED && h->revealed > 0 &&
                          hidden == h->total_mines) ||
                         (r->reason == MS_PLAN_REASON_CERTAIN_MOVES && guess);
        if (!reason_ok || !pl_result_zero_estimates(r) || r->candidates != 0 || r->layouts != 0 ||
            r->search_nodes != 0) {
            return MS_ERR_INVALID_RESULT;
        }
        return MS_OK;
    }
    case MS_PLAN_EXACT: {
        /* A completed search over >= 2 layouts with no certain cell: the best
         * reveal is safe in some layouts and not all, wins >= 1 layout (a
         * policy can follow any one layout) and never all of them. Every
         * hidden cell safe in some layout is a candidate: fewer than
         * total_mines cells are mines in every layout (else all layouts
         * would be one), so candidates >= hidden - total_mines + 1 >= 2. */
        if (r->reason != MS_PLAN_REASON_NONE || !guess || !cell_ok ||
            r->candidates <= hidden - h->total_mines ||
            r->exact_total < 2u || r->layouts != r->exact_total || r->exact_wins == 0 ||
            r->exact_wins >= r->exact_total || r->trials != 0 || r->search_nodes == 0 ||
            r->posterior_exact != 1u || r->standard_error != 0.0 ||
            r->win_probability != (double)r->exact_wins / (double)r->exact_total ||
            !(r->survival_probability > 0.0 && r->survival_probability < 1.0) ||
            r->win_probability > r->survival_probability) {
            return MS_ERR_INVALID_RESULT;
        }
        /* Survival is the advised cell's safe-layout count over the total:
         * exact_wins <= that count < exact_total (checked above). */
        double total = (double)r->exact_total;
        if (base_round(r->survival_probability * total) / total != r->survival_probability) {
            return MS_ERR_INVALID_RESULT;
        }
        return MS_OK;
    }
    case MS_PLAN_ESTIMATED: {
        uint32_t completed = r->trials - r->incomplete;
        /* Never certain: survival below 1, at least one round won, and
         * every round won only with a positive standard error. */
        if (r->reason != MS_PLAN_REASON_NONE || !guess || !cell_ok || r->candidates == 0 ||
            completed == 0 || r->exact_wins != 0 || r->exact_total != 0 ||
            !(r->survival_probability > 0.0 && r->survival_probability < 1.0) ||
            !(r->win_probability > 0.0) || r->win_probability > 1.0 ||
            r->standard_error < 0.0 || r->standard_error > 0.5 ||
            (r->win_probability == 1.0 && !(r->standard_error > 0.0))) {
            return MS_ERR_INVALID_RESULT;
        }
        /* The fraction of completed rounds won: wins / completed exactly. */
        double wins = base_round(r->win_probability * (double)completed);
        if (wins < 0.0 || wins > (double)completed ||
            wins / (double)completed != r->win_probability) {
            return MS_ERR_INVALID_RESULT;
        }
        return MS_OK;
    }
    case MS_PLAN_UNAVAILABLE: {
        bool counts_ok;
        switch (r->reason) {
        case MS_PLAN_REASON_NO_SAMPLES:
        case MS_PLAN_REASON_POSTERIOR_UNAVAILABLE:
            counts_ok = r->layouts == 0 && r->candidates == 0 && r->search_nodes == 0;
            break;
        case MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS:
            counts_ok = r->layouts > 0;
            break;
        case MS_PLAN_REASON_BUDGET:
            counts_ok = true;
            break;
        default:
            counts_ok = false;
            break;
        }
        if (!counts_ok || !guess || !pl_result_zero_estimates(r)) return MS_ERR_INVALID_RESULT;
        return MS_OK;
    }
    default:
        return MS_ERR_INVALID_RESULT;
    }
}

/* ======================================================================
 * Arguments and the call context
 * ====================================================================== */

/* [ptr, ptr + len) as integers; false when empty or wrapping. */
static bool pl_range(const void *ptr, size_t len, uintptr_t *start, uintptr_t *end) {
    uintptr_t at = (uintptr_t)ptr;
    if (len == 0 || (uintptr_t)len > MS_UINTPTR_MAX - at) return false;
    *start = at;
    *end = at + (uintptr_t)len;
    return true;
}

static bool pl_overlap(uintptr_t a0, uintptr_t a1, uintptr_t b0, uintptr_t b1) {
    return !(a1 <= b0 || b1 <= a0);
}

/* ms_solve's argument rules (pb_check_args): addresses first, before any
 * caller byte is read or written, then the limits header, the observation,
 * the result length and the limits. A rejected call changes nothing. */
static ms_status pl_check_args(const void *obs, size_t obs_len, const ms_plan_limits *limits,
                               const rt_mem *workspace, const rt_clock *clock,
                               const ms_plan_result *result, size_t result_len) {
    if (obs == NULL || limits == NULL || result == NULL || workspace == NULL || clock == NULL ||
        ((uintptr_t)obs & 7u) != 0 || ((uintptr_t)limits & 7u) != 0 ||
        ((uintptr_t)result & 7u) != 0) {
        return MS_ERR_INVALID_BUFFER;
    }
    uintptr_t o0, o1, l0, l1, r0, r1, w0, w1, c0, c1;
    if (!pl_range(obs, obs_len, &o0, &o1) || !pl_range(limits, sizeof(*limits), &l0, &l1) ||
        !pl_range(result, result_len, &r0, &r1) ||
        !pl_range(workspace, sizeof(*workspace), &w0, &w1) ||
        !pl_range(clock, sizeof(*clock), &c0, &c1)) {
        return MS_ERR_INVALID_BUFFER;
    }
    if (pl_overlap(o0, o1, l0, l1) || pl_overlap(r0, r1, o0, o1) || pl_overlap(r0, r1, l0, l1) ||
        pl_overlap(o0, o1, w0, w1) || pl_overlap(o0, o1, c0, c1) || pl_overlap(l0, l1, w0, w1) ||
        pl_overlap(l0, l1, c0, c1) || pl_overlap(r0, r1, w0, w1) || pl_overlap(r0, r1, c0, c1) ||
        pl_overlap(w0, w1, c0, c1)) {
        return MS_ERR_INVALID_BUFFER;
    }
    if (limits->magic != MS_MAGIC_PLAN_LIMITS || limits->version != MS_PLANNER_VERSION) {
        return MS_ERR_INVALID_BUFFER;
    }
    ms_status status = ms_obs_validate(obs, obs_len);
    if (status != MS_OK) return status;
    if (result_len != sizeof(ms_plan_result)) return MS_ERR_INVALID_BUFFER;
    return ms_plan_limits_validate(limits);
}

static void pl_open(pl_ctx *ctx, const void *obs, size_t obs_len, const ms_plan_limits *limits,
                    rt_mem *workspace, rt_clock *clock) {
    const ms_obs_header *h = (const ms_obs_header *)obs;
    base_memset(ctx, 0, sizeof(*ctx));
#if MS_TEST_SUITE_PLANNER
    pl_fault_calls[PL_FAULT_INFER] = pl_fault_calls[PL_FAULT_TAIL] = 0;
#endif
    ctx->mem = workspace;
    ctx->clock = clock;
    ctx->limits = limits;
    ctx->obs = obs;
    ctx->obs_len = obs_len;
    ctx->clue = ms_obs_clues(obs);
    ctx->width = h->width;
    ctx->height = h->height;
    ctx->cells = h->width * h->height;
    ctx->total = h->total_mines;
    ctx->revealed = h->revealed;
    ctx->hidden = ctx->cells - h->revealed;
    ctx->hash = ms_obs_hash(obs);
    ctx->start_ms = rt_clock_now(clock);
    ctx->deadline_ms = rt_deadline(ctx->start_ms, limits->time_budget_ms, 1.0);
    rt_meter_init(&ctx->meter, clock, ctx->deadline_ms, PL_METER_INTERVAL);
    ctx->vclock.real = clock;
    ctx->vclock.deadline_ms = ctx->deadline_ms;
    ctx->vclock.late_ms = PL_LATE_MS;
    ctx->vclock.tripped = false;
    rt_clock_init(&ctx->nested, pl_vclock_read, &ctx->vclock);
    ctx->memory = limits->memory_budget_bytes > (uint64_t)SIZE_MAX
                      ? SIZE_MAX
                      : (size_t)limits->memory_budget_bytes;
    ctx->saved_budget = rt_mem_limit(workspace, ctx->memory);
    ctx->mark = rt_mem_mark(workspace);
    ctx->misuses0 = workspace->misuses;
}

/* Releases everything the call allocated and restores the caller's budget;
 * workspace misuse during the call is internal. */
static ms_status pl_close(pl_ctx *ctx, pl_rc rc) {
    ms_status status = MS_OK;
    if (rc == PL_FAIL) {
        status = ctx->fail != MS_OK ? ctx->fail : MS_ERR_INTERNAL;
    } else if (rc != PL_OK) {
        status = MS_ERR_INTERNAL;
    }
    rt_mem_rewind(ctx->mem, ctx->mark);
    rt_mem_set_budget(ctx->mem, ctx->saved_budget);
    if (ctx->mem->misuses != ctx->misuses0) status = MS_ERR_INTERNAL;
    return status;
}

static double pl_elapsed(const pl_ctx *ctx) {
    double elapsed = rt_clock_now(ctx->clock) - ctx->start_ms;
    return pl_finite(elapsed) && elapsed > 0.0 ? elapsed : 0.0;
}

static void pl_result_start(const pl_ctx *ctx, ms_plan_result *r) {
    base_memset(r, 0, sizeof(*r));
    r->magic = MS_MAGIC_PLAN_RESULT;
    r->version = MS_PLANNER_VERSION;
    r->width = ctx->width;
    r->height = ctx->height;
    r->total_mines = ctx->total;
    r->revealed = ctx->revealed;
    r->observation_hash = ctx->hash;
    r->cell = MS_PLAN_NO_CELL;
}

/* An explicit NONE answer: no estimate, every count zero. */
static void pl_result_none(ms_plan_result *r, uint32_t reason) {
    r->status = MS_PLAN_NONE;
    r->reason = reason;
    r->cell = MS_PLAN_NO_CELL;
    r->candidates = 0;
    r->layouts = 0;
    r->trials = 0;
    r->incomplete = 0;
    r->search_nodes = 0;
    r->posterior_exact = 0;
    r->survival_probability = 0.0;
    r->win_probability = 0.0;
    r->standard_error = 0.0;
    r->exact_wins = 0;
    r->exact_total = 0;
}

/* An explicit UNAVAILABLE answer; the diagnostic counts already set stay. */
static void pl_result_unavailable(ms_plan_result *r, uint32_t reason) {
    r->status = MS_PLAN_UNAVAILABLE;
    r->reason = reason;
    r->cell = MS_PLAN_NO_CELL;
    r->survival_probability = 0.0;
    r->win_probability = 0.0;
    r->standard_error = 0.0;
    r->exact_wins = 0;
    r->exact_total = 0;
}

static uint32_t pl_saturate(uint64_t x) {
    return x > UINT32_MAX ? UINT32_MAX : (uint32_t)x;
}

/* Answers that need no inference: nothing revealed yet, or every hidden
 * cell a mine (after checking that the clues agree). *done when answered. */
static pl_rc pl_placeholders(pl_ctx *ctx, ms_plan_result *out, bool *done) {
    *done = false;
    if (ctx->revealed == 0) {
        pl_result_none(out, MS_PLAN_REASON_NOT_STARTED);
        *done = true;
        return PL_OK;
    }
    if (ctx->hidden < ctx->total) return pl_fail(ctx, MS_ERR_INCONSISTENT);
    if (ctx->hidden > ctx->total) return PL_OK;
    uint32_t around[8];
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (ctx->clue[c] == MS_CLUE_HIDDEN) continue;
        uint32_t n = rt_grid_neighbors(ctx->width, ctx->height, c, around), mines = 0;
        for (uint32_t i = 0; i < n; i++) mines += ctx->clue[around[i]] == MS_CLUE_HIDDEN;
        if (mines != ctx->clue[c]) return pl_fail(ctx, MS_ERR_INCONSISTENT);
    }
    pl_result_none(out, MS_PLAN_REASON_FINISHED);
    *done = true;
    return PL_OK;
}

static pl_rc pl_prepare(pl_ctx *ctx) {
    size_t slots;
    if (!rt_mul_size(ctx->cells, 8u, &slots)) return PL_NOMEM;
    if (!PL_NEW(ctx, ctx->nbr, slots) || !PL_NEW(ctx, ctx->nbr_n, ctx->cells)) {
        return pl_nomem(ctx);
    }
    for (uint32_t c = 0; c < ctx->cells; c++) {
        ctx->nbr_n[c] = (uint8_t)rt_grid_neighbors(ctx->width, ctx->height, c, ctx->nbr + 8u * c);
    }
    return PL_OK;
}

/* ======================================================================
 * Root posterior
 * ====================================================================== */

typedef struct pl_root {
    void *prob;              /* ms_solve result of the observation */
    size_t prob_len;
    uint8_t *layouts;        /* capacity rows of cells bytes */
    size_t layouts_len;
    uint32_t exact_limit;    /* after the byte-budget clamp */
    uint32_t sample_count;
    ms_posterior_info info;
    uint8_t *know;           /* proven mines (PL_K_MINE): the policy's start */
    uint32_t known_mines;
} pl_root;

static pl_rc pl_root_infer(pl_ctx *ctx, pl_root *root) {
    const ms_plan_limits *limits = ctx->limits;
    base_memset(root, 0, sizeof(*root));
    root->prob_len = ms_result_size(ctx->width, ctx->height);
    /* Layout rows may take at most a share of the byte budget. */
    size_t room = ctx->memory / PL_LAYOUT_SHARE / ctx->cells;
    uint32_t cap = room > UINT32_MAX ? UINT32_MAX : (uint32_t)room;
    root->exact_limit = pl_min_u32(limits->exact_layout_limit, cap);
    root->sample_count = pl_min_u32(limits->sample_count, cap);
    uint32_t rows = root->exact_limit > root->sample_count ? root->exact_limit : root->sample_count;
    root->layouts_len = (size_t)rows * ctx->cells;
    root->prob = pl_bump(ctx, root->prob_len, 1u);
    if (root->prob == NULL || !PL_NEW(ctx, root->know, ctx->cells)) return pl_nomem(ctx);
    if (root->layouts_len > 0 && !PL_NEW(ctx, root->layouts, root->layouts_len)) {
        return pl_nomem(ctx);
    }
    ms_infer_limits il;
    ms_limits_default(&il);
    il.time_budget_ms = limits->time_budget_ms < HUGE_VAL
                            ? limits->time_budget_ms * PL_POSTERIOR_SHARE
                            : HUGE_VAL;
    il.memory_budget_bytes = ctx->memory;
    if (limits->flags & MS_PLAN_EXPLICIT_SEED) {
        il.flags |= MS_LIMIT_EXPLICIT_SEED;
        il.seed = limits->seed;
    }
    ms_posterior_info info;
    ms_status status = ms_posterior_generate(ctx->obs, ctx->obs_len, &il, root->exact_limit,
                                             root->sample_count, ctx->mem, ctx->clock,
                                             root->prob, root->prob_len, root->layouts,
                                             root->layouts_len, &info);
    if (status != MS_OK) return pl_fail(ctx, status);
    if (ms_result_validate_solver(ctx->obs, ctx->obs_len, root->prob, root->prob_len) != MS_OK ||
        info.count > rows || info.exhaustive > 1u || info.exact_distribution > 1u ||
        (info.exhaustive && (!info.exact_distribution || info.total_layouts != info.count))) {
        return pl_fail(ctx, MS_ERR_INTERNAL);
    }
    base_memcpy(&root->info, &info, sizeof(info));
    const uint8_t *flags = ms_result_flags(root->prob);
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (ctx->clue[c] == MS_CLUE_HIDDEN && (flags[c] & MS_PCELL_PROVEN_MINE)) {
            root->know[c] = PL_K_MINE;
            root->known_mines++;
        }
    }
    return PL_OK;
}

/* Whether a guess is unnecessary: a proven-safe hidden cell, or (complete
 * enumeration) a hidden cell safe in every compatible layout. */
static bool pl_certain_moves(const pl_ctx *ctx, const pl_root *root) {
    const uint8_t *flags = ms_result_flags(root->prob);
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (ctx->clue[c] == MS_CLUE_HIDDEN && (flags[c] & MS_PCELL_PROVEN_SAFE)) return true;
    }
    if (!root->info.exhaustive || root->info.count == 0) return false;
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (ctx->clue[c] != MS_CLUE_HIDDEN) continue;
        bool safe = true;
        for (uint32_t l = 0; l < root->info.count && safe; l++) {
            safe = root->layouts[(size_t)l * ctx->cells + c] == 0;
        }
        if (safe) return true;
    }
    return false;
}

/* Whether the layouts there are come from the exact posterior (0 when
 * there are none). */
static uint32_t pl_posterior_exact(const pl_root *root) {
    return root->info.count > 0 && root->info.exact_distribution ? 1u : 0u;
}

/* Why no layouts came back. */
static uint32_t pl_posterior_reason(const pl_ctx *ctx, const pl_root *root) {
    const ms_result_header *h = (const ms_result_header *)root->prob;
    uint32_t reason = root->info.reason;
    if (reason == MS_REASON_TIME_BUDGET_EXHAUSTED || reason == MS_REASON_MEMORY_BUDGET_EXHAUSTED) {
        return MS_PLAN_REASON_BUDGET;
    }
    if (h->status == MS_PROB_UNAVAILABLE) return MS_PLAN_REASON_POSTERIOR_UNAVAILABLE;
    if (root->sample_count == 0) {
        return ctx->limits->sample_count == 0 ? MS_PLAN_REASON_NO_SAMPLES : MS_PLAN_REASON_BUDGET;
    }
    return MS_PLAN_REASON_POSTERIOR_UNAVAILABLE;
}

/* ======================================================================
 * Simulated games
 *
 * Information boundary. A rollout plays one whole game on one sampled
 * board. The board is read only by the environment: env_load (checks it
 * against the real observation and counts its neighbors) and env_reveal
 * (the transition that answers a reveal with what the game would show: an
 * explosion, or the clues the flood opens), plus the won/lost test on the
 * simulated observation. The continuation policy (pol_*) never receives the
 * board: its inputs are the simulated public observation (pl_sim: clues,
 * the cells the last reveal opened, the hidden count), the root's proven
 * mines (from the real public observation) and what it derives from those -
 * local deductions, ms_posterior_generate on the simulated observation and
 * the exact tail search over that posterior. Boards that agree on
 * everything revealed so far therefore get identical decisions, and its
 * seeds come from public data only (the seed and the simulated
 * observation's hash). The first move comes from the shortlist, which is
 * the same for every board.
 * ====================================================================== */

typedef struct pl_sim {
    void *obs;       /* the simulated public observation (ms_obs layout) */
    size_t obs_len;
    uint8_t *clue;
    uint32_t hidden;
    uint32_t *fresh; /* cells the last reveal opened */
    uint32_t nfresh;
} pl_sim;

typedef struct pl_env {
    const uint8_t *mine; /* the sampled board: 1 = mine */
    uint8_t *count;      /* its adjacent-mine counts */
    uint32_t *queue;     /* flood queue */
} pl_env;

static void sim_reset(pl_sim *sim, const pl_ctx *ctx) {
    base_memcpy(sim->obs, ctx->obs, ctx->obs_len);
    sim->hidden = ctx->hidden;
    sim->nfresh = 0;
}

/* Loads a board, checking that it completes the observation. */
static pl_rc env_load(pl_env *env, pl_ctx *ctx, const uint8_t *layout) {
    env->mine = layout;
    base_memset(env->count, 0, ctx->cells);
    uint32_t mines = 0;
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (layout[c] > 1u) return pl_fail(ctx, MS_ERR_INTERNAL);
        if (!layout[c]) continue;
        mines++;
        for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) env->count[ctx->nbr[8u * c + i]]++;
    }
    if (mines != ctx->total) return pl_fail(ctx, MS_ERR_INTERNAL);
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (ctx->clue[c] != MS_CLUE_HIDDEN && (layout[c] || env->count[c] != ctx->clue[c])) {
            return pl_fail(ctx, MS_ERR_INTERNAL);
        }
    }
    return PL_OK;
}

/* The environment transition (the game's reveal rule and flood). */
static pl_rc env_reveal(const pl_env *env, pl_ctx *ctx, pl_sim *sim, uint32_t cell, bool *boom) {
    sim->nfresh = 0;
    *boom = false;
    if (cell >= ctx->cells || sim->clue[cell] != MS_CLUE_HIDDEN) {
        return pl_fail(ctx, MS_ERR_INTERNAL);
    }
    if (env->mine[cell]) {
        *boom = true;
        return PL_OK;
    }
    ms_obs_header *h = (ms_obs_header *)sim->obs;
    uint32_t head = 0, tail = 0;
    sim->clue[cell] = env->count[cell];
    h->revealed++;
    sim->hidden--;
    sim->fresh[sim->nfresh++] = cell;
    env->queue[tail++] = cell;
    while (head < tail) {
        uint32_t x = env->queue[head++];
        if (env->count[x] != 0) continue;
        for (uint32_t i = 0; i < ctx->nbr_n[x]; i++) {
            uint32_t y = ctx->nbr[8u * x + i];
            if (sim->clue[y] != MS_CLUE_HIDDEN) continue;
            sim->clue[y] = env->count[y]; /* a 0's neighbors are never mines */
            h->revealed++;
            sim->hidden--;
            sim->fresh[sim->nfresh++] = y;
            env->queue[tail++] = y;
        }
    }
    return PL_OK;
}

/* ----------------------------------------------------------------------
 * Continuation policy: forced-safe closure, then a min-risk guess.
 *
 * 1. Reveal every cell known safe. Knowledge comes from sound public
 *    deductions only: single clues, pairs of nearby clues whose unknown
 *    neighbors nest (subset rule), the global mine count, and the proofs
 *    of the inference below.
 * 2. When stuck, run ms_posterior_generate on the simulated observation
 *    with fixed work caps (PL_ROLL_*). Its proofs join the knowledge; when
 *    the complete posterior has at most PL_TAIL_LAYOUTS layouts, the exact
 *    search (PL_TAIL_NODES) picks the guess - the exact endgame tail.
 *    Both run on the virtual clock (pl_vclock): it reads 0 until the
 *    caller's clock passes the shared deadline, so their phases never time
 *    out early and a completed answer depends on the work caps only, never
 *    on the time left. Once the deadline passes it reads late (and later at
 *    each reading), every nested meter expires at its next check, and the
 *    round is discarded: an answer the deadline may have cut short is never
 *    used.
 * 3. Otherwise guess the lowest mine probability; ties go to the cell most
 *    likely to open a region (neighbors treated as independent), then the
 *    lowest index. If inference has no estimates (UNAVAILABLE), a documented
 *    heuristic stands in for probabilities: the highest residual density
 *    among the cell's revealed neighbors, else the global density of unknown
 *    cells. Internal errors are never absorbed by the heuristic.
 * ---------------------------------------------------------------------- */

typedef struct pl_policy {
    pl_ctx *ctx;
    const pl_root *root;
    uint8_t *know;        /* PL_K_* per cell */
    uint32_t *safe;       /* known-safe hidden cells to reveal */
    uint32_t nsafe;
    uint32_t *dirty;      /* revealed cells whose surroundings changed */
    uint32_t ndirty;
    uint8_t *queued;
    uint32_t mines;       /* cells known to be mines */
    uint32_t unknown;     /* hidden cells known neither safe nor mine */
    bool solved;          /* res answers the current observation */
    uint32_t tail_count;  /* complete posterior rows in tail (0: none) */
    void *res;
    size_t res_len;
    uint8_t *tail;
    size_t tail_len;
    ms_infer_limits il;
} pl_policy;

static void pol_dirty(pl_policy *pol, uint32_t x) {
    if (pol->queued[x]) return;
    pol->queued[x] = 1u;
    pol->dirty[pol->ndirty++] = x;
}

/* Revealed neighbors of c see a changed neighborhood. */
static void pol_touch(pl_policy *pol, const pl_sim *sim, uint32_t c) {
    const pl_ctx *ctx = pol->ctx;
    for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) {
        uint32_t y = ctx->nbr[8u * c + i];
        if (sim->clue[y] != MS_CLUE_HIDDEN) pol_dirty(pol, y);
    }
}

static void pol_start(pl_policy *pol, const pl_sim *sim) {
    for (uint32_t i = 0; i < pol->ndirty; i++) pol->queued[pol->dirty[i]] = 0;
    pol->ndirty = 0;
    pol->nsafe = 0;
    base_memcpy(pol->know, pol->root->know, pol->ctx->cells);
    pol->mines = pol->root->known_mines;
    pol->unknown = sim->hidden - pol->mines;
    pol->solved = false;
    pol->tail_count = 0;
}

/* Records a sound public deduction about hidden cell c. */
static pl_rc pol_mark(pl_policy *pol, const pl_sim *sim, uint32_t c, uint8_t what) {
    if (sim->clue[c] != MS_CLUE_HIDDEN) {
        return what == PL_K_SAFE ? PL_OK : pl_fail(pol->ctx, MS_ERR_INTERNAL);
    }
    uint8_t known = pol->know[c];
    if (known == what) return PL_OK;
    if (known != PL_K_UNKNOWN) return pl_fail(pol->ctx, MS_ERR_INTERNAL); /* contradiction */
    pol->know[c] = what;
    pol->unknown--;
    if (what == PL_K_MINE) {
        pol->mines++;
    } else {
        pol->safe[pol->nsafe++] = c;
    }
    pol_touch(pol, sim, c);
    return PL_OK;
}

/* Takes in the cells the last reveal opened. */
static pl_rc pol_observe(pl_policy *pol, const pl_sim *sim) {
    for (uint32_t i = 0; i < sim->nfresh; i++) {
        uint32_t c = sim->fresh[i];
        if (pol->know[c] == PL_K_MINE) return pl_fail(pol->ctx, MS_ERR_INTERNAL);
        if (pol->know[c] == PL_K_UNKNOWN) pol->unknown--;
        pol->know[c] = PL_K_SAFE;
        pol_dirty(pol, c);
        pol_touch(pol, sim, c);
    }
    pol->solved = false;
    return PL_OK;
}

/* Unknown hidden neighbors of revealed x (ascending) and how many mines
 * they still hold. */
static uint32_t pol_frontier(const pl_policy *pol, const pl_sim *sim, uint32_t x, uint32_t *u,
                             int32_t *need) {
    const pl_ctx *ctx = pol->ctx;
    uint32_t nu = 0;
    int32_t mines = 0;
    for (uint32_t i = 0; i < ctx->nbr_n[x]; i++) {
        uint32_t y = ctx->nbr[8u * x + i];
        if (sim->clue[y] != MS_CLUE_HIDDEN) continue;
        if (pol->know[y] == PL_K_MINE) {
            mines++;
        } else if (pol->know[y] == PL_K_UNKNOWN) {
            u[nu++] = y;
        }
    }
    *need = (int32_t)sim->clue[x] - mines;
    return nu;
}

static bool pl_subset(const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb) {
    uint32_t j = 0;
    for (uint32_t i = 0; i < na; i++) {
        while (j < nb && b[j] < a[i]) j++;
        if (j == nb || b[j] != a[i]) return false;
        j++;
    }
    return true;
}

/* Subset rule between x and the revealed cells within two rows/columns. */
static pl_rc pol_pairs(pl_policy *pol, const pl_sim *sim, uint32_t x, const uint32_t *ux,
                       uint32_t nx, int32_t rx) {
    const pl_ctx *ctx = pol->ctx;
    uint32_t row = x / ctx->width, col = x % ctx->width;
    uint32_t r0 = row >= 2u ? row - 2u : 0;
    uint32_t r1 = row + 2u < ctx->height ? row + 2u : ctx->height - 1u;
    uint32_t c0 = col >= 2u ? col - 2u : 0;
    uint32_t c1 = col + 2u < ctx->width ? col + 2u : ctx->width - 1u;
    for (uint32_t r = r0; r <= r1; r++) {
        for (uint32_t cc = c0; cc <= c1; cc++) {
            uint32_t y = r * ctx->width + cc;
            if (y == x || sim->clue[y] == MS_CLUE_HIDDEN) continue;
            uint32_t uy[8];
            int32_t ry;
            uint32_t ny = pol_frontier(pol, sim, y, uy, &ry);
            const uint32_t *small, *big;
            uint32_t ns, nbig;
            int32_t rs, rb;
            if (ny > nx) {
                small = ux;
                ns = nx;
                rs = rx;
                big = uy;
                nbig = ny;
                rb = ry;
            } else if (ny < nx && ny > 0) {
                small = uy;
                ns = ny;
                rs = ry;
                big = ux;
                nbig = nx;
                rb = rx;
            } else {
                continue;
            }
            if (!pl_subset(small, ns, big, nbig)) continue;
            uint32_t d[8], nd = 0;
            for (uint32_t i = 0, j = 0; i < nbig; i++) {
                if (j < ns && small[j] == big[i]) {
                    j++;
                } else {
                    d[nd++] = big[i];
                }
            }
            int32_t rd = rb - rs;
            if (rd < 0 || rd > (int32_t)nd) return pl_fail(pol->ctx, MS_ERR_INTERNAL);
            if (rd != 0 && rd != (int32_t)nd) continue;
            uint8_t what = rd == 0 ? PL_K_SAFE : PL_K_MINE;
            for (uint32_t i = 0; i < nd; i++) PL_TRY(pol_mark(pol, sim, d[i], what));
            pol_dirty(pol, x); /* x's neighborhood may have changed */
            return PL_OK;
        }
    }
    return PL_OK;
}

/* Global mine count: no mines left, or as many as unknown cells. */
static pl_rc pol_global(pl_policy *pol, const pl_sim *sim) {
    const pl_ctx *ctx = pol->ctx;
    if (pol->mines > ctx->total || ctx->total - pol->mines > pol->unknown) {
        return pl_fail(pol->ctx, MS_ERR_INTERNAL);
    }
    uint32_t remaining = ctx->total - pol->mines;
    if (pol->unknown == 0 || (remaining != 0 && remaining != pol->unknown)) return PL_OK;
    uint8_t what = remaining == 0 ? PL_K_SAFE : PL_K_MINE;
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (sim->clue[c] == MS_CLUE_HIDDEN && pol->know[c] == PL_K_UNKNOWN) {
            PL_TRY(pol_mark(pol, sim, c, what));
        }
    }
    return PL_OK;
}

/* Local deductions to a fixpoint. */
static pl_rc pol_deduce(pl_policy *pol, const pl_sim *sim) {
    for (;;) {
        while (pol->ndirty > 0) {
            uint32_t x = pol->dirty[--pol->ndirty];
            pol->queued[x] = 0;
            if (sim->clue[x] == MS_CLUE_HIDDEN) continue;
            uint32_t ux[8];
            int32_t rx;
            uint32_t nx = pol_frontier(pol, sim, x, ux, &rx);
            if (rx < 0 || rx > (int32_t)nx) return pl_fail(pol->ctx, MS_ERR_INTERNAL);
            if (nx == 0) continue;
            if (rx == 0 || rx == (int32_t)nx) {
                uint8_t what = rx == 0 ? PL_K_SAFE : PL_K_MINE;
                for (uint32_t i = 0; i < nx; i++) PL_TRY(pol_mark(pol, sim, ux[i], what));
                continue;
            }
            PL_TRY(pol_pairs(pol, sim, x, ux, nx, rx));
        }
        PL_TRY(pol_global(pol, sim));
        if (pol->ndirty == 0) return PL_OK;
    }
}

/* Inference on the simulated observation, on the virtual clock; PL_STOP
 * once the deadline passed, also when it passed inside (the answer might
 * then depend on time, so the round is discarded instead of using it).
 * Failures come first: a deadline never hides a bug. */
static pl_rc pol_infer(pl_policy *pol, const pl_sim *sim) {
    pl_ctx *ctx = pol->ctx;
    if (rt_meter_check(&ctx->meter)) return PL_STOP;
    if (ctx->limits->flags & MS_PLAN_EXPLICIT_SEED) {
        pol->il.seed = rt_mix64(ctx->limits->seed ^ ms_obs_hash(sim->obs));
    }
    PL_FAULT_ENTER(PL_FAULT_INFER);
    ms_posterior_info info;
    ms_status status = ms_posterior_generate(sim->obs, sim->obs_len, &pol->il, PL_TAIL_LAYOUTS, 0,
                                             ctx->mem, &ctx->nested, pol->res, pol->res_len,
                                             pol->tail, pol->tail_len, &info);
    PL_FAULT_STATUS(status, pol->res);
    /* Any failure is a bug, reported even when the deadline passed inside:
     * the arguments are the planner's own, and a simulated observation
     * comes from a complete layout, so it is always consistent (shortages
     * are reasons inside a valid MS_OK result). */
    if (status != MS_OK ||
        ms_result_validate_solver(sim->obs, sim->obs_len, pol->res, pol->res_len) != MS_OK ||
        info.count > PL_TAIL_LAYOUTS || (info.exhaustive && !info.exact_distribution)) {
        return pl_fail(ctx, MS_ERR_INTERNAL);
    }
    if (ctx->vclock.tripped) {
        ctx->aborts_inference++;
        return PL_STOP;
    }
    pol->solved = true;
    pol->tail_count = info.exhaustive ? info.count : 0;
    const uint8_t *flags = ms_result_flags(pol->res);
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (sim->clue[c] != MS_CLUE_HIDDEN) continue;
        if (flags[c] & MS_PCELL_PROVEN_SAFE) {
            PL_TRY(pol_mark(pol, sim, c, PL_K_SAFE));
        } else if (flags[c] & MS_PCELL_PROVEN_MINE) {
            PL_TRY(pol_mark(pol, sim, c, PL_K_MINE));
        }
    }
    return PL_OK;
}

/* Heuristic stand-in for a probability (inference UNAVAILABLE). */
static double pol_heuristic(const pl_policy *pol, const pl_sim *sim, uint32_t c) {
    const pl_ctx *ctx = pol->ctx;
    double best = -1.0;
    for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) {
        uint32_t x = ctx->nbr[8u * c + i];
        if (sim->clue[x] == MS_CLUE_HIDDEN) continue;
        uint32_t u[8];
        int32_t need;
        uint32_t nu = pol_frontier(pol, sim, x, u, &need);
        if (nu > 0) {
            double q = (double)need / (double)nu;
            if (q > best) best = q;
        }
    }
    if (best >= 0.0) return best;
    return pol->unknown > 0 ? (double)(ctx->total - pol->mines) / (double)pol->unknown : 1.0;
}

static double pol_probability(const pl_policy *pol, const pl_sim *sim, uint32_t c, bool values) {
    if (pol->know[c] == PL_K_MINE) return 1.0;
    if (pol->know[c] == PL_K_SAFE) return 0.0;
    return values ? ms_result_probabilities(pol->res)[c] : pol_heuristic(pol, sim, c);
}

/* Chance that revealing c shows a 0 (a tie-breaking progress proxy). */
static double pol_open_chance(const pl_policy *pol, const pl_sim *sim, uint32_t c, bool values) {
    const pl_ctx *ctx = pol->ctx;
    double chance = 1.0;
    for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) {
        uint32_t y = ctx->nbr[8u * c + i];
        if (sim->clue[y] == MS_CLUE_HIDDEN) chance *= 1.0 - pol_probability(pol, sim, y, values);
    }
    return chance;
}

static pl_rc pol_guess(pl_policy *pol, const pl_sim *sim, uint32_t *cell) {
    pl_ctx *ctx = pol->ctx;
    if (pol->tail_count >= 2u) {
        ex_answer ans;
        PL_FAULT_ENTER(PL_FAULT_TAIL);
        pl_rc rc = ex_run(ctx, &ctx->nested, sim->clue, pol->tail, pol->tail_count, PL_TAIL_NODES,
                          PL_NESTED_MS, &ans);
        PL_FAULT_RC(rc);
        PL_TRY(rc); /* failures first: a deadline never hides them */
        if (ctx->vclock.tripped) {
            ctx->aborts_tail++; /* cut short by the deadline: discard the round */
            return PL_STOP;
        }
        /* Not tripped: complete, or stopped by its node or memory cap,
         * never by the clock (then the guess below). */
        pol->tail_count = 0;
        if (ans.complete && ans.cell < ctx->cells && sim->clue[ans.cell] == MS_CLUE_HIDDEN &&
            pol->know[ans.cell] != PL_K_MINE) {
            *cell = ans.cell;
            return PL_OK;
        }
    }
    const ms_result_header *h = (const ms_result_header *)pol->res;
    bool values = h->status == MS_PROB_EXACT || h->status == MS_PROB_APPROXIMATE;
    const uint8_t *flags = ms_result_flags(pol->res);
    uint32_t best = PL_NONE;
    double bp = 2.0, bz = -1.0;
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (sim->clue[c] != MS_CLUE_HIDDEN || pol->know[c] != PL_K_UNKNOWN ||
            (flags[c] & MS_PCELL_PROVEN_MINE)) {
            continue;
        }
        double p = pol_probability(pol, sim, c, values);
        if (best == PL_NONE || p < bp - PL_TIE) {
            best = c;
            bp = p;
            bz = -1.0;
            continue;
        }
        if (p > bp + PL_TIE) continue;
        if (bz < 0.0) bz = pol_open_chance(pol, sim, best, values);
        double z = pol_open_chance(pol, sim, c, values);
        if (z > bz) {
            best = c;
            bp = p;
            bz = z;
        }
    }
    if (best == PL_NONE) return pl_fail(ctx, MS_ERR_INTERNAL); /* not won, nothing to try */
    *cell = best;
    return PL_OK;
}

/* The policy's next reveal; PL_STOP once the deadline passed. */
static pl_rc pol_choose(pl_policy *pol, const pl_sim *sim, uint32_t *cell) {
    for (;;) {
        while (pol->nsafe > 0) {
            uint32_t c = pol->safe[--pol->nsafe];
            if (sim->clue[c] == MS_CLUE_HIDDEN) {
                *cell = c;
                return PL_OK;
            }
        }
        PL_TRY(pol_deduce(pol, sim));
        if (pol->nsafe > 0) continue;
        if (!pol->solved) {
            PL_TRY(pol_infer(pol, sim));
            continue;
        }
        return pol_guess(pol, sim, cell);
    }
}

/* ----------------------------------------------------------------------
 * Playing a game
 * ---------------------------------------------------------------------- */

typedef struct pl_roll {
    pl_ctx *ctx;
    pl_sim sim;
    pl_env env;
    pl_policy pol;
    uint32_t step_limit;
} pl_roll;

typedef struct pl_trace {
    uint32_t *cells;
    uint32_t cap;
    uint32_t len;
} pl_trace;

static pl_rc pl_roll_open(pl_roll *ro, pl_ctx *ctx, const pl_root *root) {
    base_memset(ro, 0, sizeof(*ro));
    ro->ctx = ctx;
    ro->step_limit = ctx->limits->rollout_step_limit;
    pl_sim *sim = &ro->sim;
    pl_policy *pol = &ro->pol;
    sim->obs_len = ctx->obs_len;
    sim->obs = pl_bump(ctx, ctx->obs_len, 1u);
    pol->res_len = ms_result_size(ctx->width, ctx->height);
    pol->res = pl_bump(ctx, pol->res_len, 1u);
    pol->tail_len = (size_t)PL_TAIL_LAYOUTS * ctx->cells;
    pol->tail = (uint8_t *)pl_bump(ctx, pol->tail_len, 1u);
    if (sim->obs == NULL || pol->res == NULL || pol->tail == NULL ||
        !PL_NEW(ctx, sim->fresh, ctx->cells) || !PL_NEW(ctx, ro->env.count, ctx->cells) ||
        !PL_NEW(ctx, ro->env.queue, ctx->cells) || !PL_NEW(ctx, pol->know, ctx->cells) ||
        !PL_NEW(ctx, pol->safe, ctx->cells) || !PL_NEW(ctx, pol->dirty, ctx->cells) ||
        !PL_NEW(ctx, pol->queued, ctx->cells)) {
        return pl_nomem(ctx);
    }
    sim->clue = ms_obs_clues(sim->obs);
    pol->ctx = ctx;
    pol->root = root;
    ms_limits_default(&pol->il);
    pol->il.node_budget = PL_ROLL_NODES;
    pol->il.sample_budget = PL_ROLL_SAMPLES;
    pol->il.min_effective_samples = PL_ROLL_MIN_ESS;
    pol->il.time_budget_ms = PL_NESTED_MS; /* on the virtual clock */
    pol->il.memory_budget_bytes = ctx->memory;
    if (ctx->limits->flags & MS_PLAN_EXPLICIT_SEED) pol->il.flags |= MS_LIMIT_EXPLICIT_SEED;
    return PL_OK;
}

/* One game on the loaded board: reveal `first`, then follow the policy
 * until the game ends, the step limit cuts it (PL_CUT) or the deadline
 * passes (PL_TIMED). Unknown endings are never wins. */
static pl_rc pl_play(pl_roll *ro, uint32_t first, uint32_t *outcome, pl_trace *trace) {
    pl_ctx *ctx = ro->ctx;
    pl_sim *sim = &ro->sim;
    sim_reset(sim, ctx);
    pol_start(&ro->pol, sim);
    uint32_t cell = first;
    if (ro->step_limit == 0) {
        *outcome = PL_CUT;
        return PL_OK;
    }
    for (uint32_t steps = 1;; steps++) {
        if (trace != NULL && trace->len < trace->cap) trace->cells[trace->len++] = cell;
        bool boom;
        PL_TRY(env_reveal(&ro->env, ctx, sim, cell, &boom));
        if (boom) {
            *outcome = PL_LOST;
            return PL_OK;
        }
        if (sim->hidden == ctx->total) {
            *outcome = PL_WON;
            return PL_OK;
        }
        if (steps >= ro->step_limit) {
            *outcome = PL_CUT;
            return PL_OK;
        }
        if (rt_meter_work(&ctx->meter, 1u)) {
            *outcome = PL_TIMED;
            return PL_OK;
        }
        PL_TRY(pol_observe(&ro->pol, sim));
        pl_rc rc = pol_choose(&ro->pol, sim, &cell);
        if (rc == PL_STOP) {
            *outcome = PL_TIMED;
            return PL_OK;
        }
        if (rc != PL_OK) return rc;
    }
}

/* ======================================================================
 * Rollout estimates
 *
 * Shortlist: every hidden cell that is safe in some layout gets cheap
 * figures - its survival, its chance to show a 0 (an opening), the
 * diversity of the clue it shows (Gini-Simpson) and whether it touches a
 * revealed clue. A complete listing gives them exactly. Posterior draws are
 * the very boards the rollouts are scored on, so cells are never ranked by
 * their outcomes in them (lucky cells would be favored and then scored on
 * the same luck): survival is the root marginal, and the clue proxies treat
 * the neighbors' marginals as independent. Five rankings (safest; most
 * likely to open; safety times clue diversity; safest off the frontier;
 * safest on it) are merged round-robin up to candidate_limit, skipping
 * cells whose outcomes match an already chosen cell's on every layout.
 * Picks other than the safety ranking need PL_SHORTLIST_FLOOR of the best
 * survival, so the shortlist is never only the minimum-risk cells and never
 * mostly hopeless ones. These figures only choose what to simulate; the
 * decision is by simulated wins.
 *
 * Paired rounds: round r plays every candidate on the same layout. Draws
 * are used in the order generated (independent draws). A complete listing
 * (exact search unavailable or interrupted) is visited once, in a seeded
 * random permutation, so any prefix the deadline leaves is a uniform sample
 * without replacement, never the first layouts in rank order. A round
 * counts only when every candidate's game ended in a win or a loss
 * (unknown endings are never wins), and the rounds stop at the first one
 * that does not: incomplete is 0 or 1.
 *  - The deadline: the round in progress is discarded for all candidates
 *    alike, as is any outcome it would have had. Which round that is
 *    leans toward long rounds, so the completed rounds lean slightly
 *    toward short ones - a bias of at most about one round in the counts,
 *    shared by all candidates since they play the same rounds; the decision
 *    below holds whatever the discarded round's outcomes.
 *  - rollout_step_limit: a game cut by it ends the rollouts with no
 *    estimate (UNAVAILABLE/BUDGET). Long games are often wins and their
 *    length depends on the first move, so the rounds that do complete
 *    would be a length-selected subset, biased differently per candidate.
 *    The default limit (the largest board) never cuts a game: every reveal
 *    opens a cell.
 * At least max(min_rollouts, 1) completed rounds are needed, except after
 * a complete pass over a listing, which has no sampling error.
 *
 * Decision. When the rounds covered a complete listing once each, the
 * counts are the policy's exact win counts: the most wins (then survival,
 * then shortlist order) is advised. With sampled rounds the safest
 * candidate (first on the shortlist) is advised unless another one's
 * paired advantage over it - rounds it won and the safest lost, minus the
 * reverse - exceeds PL_SWITCH_Z standard deviations of that difference
 * (McNemar); then the clearest such candidate. This guards against the
 * winner's curse of maximizing over noisy estimates. A round the deadline
 * discarded counts against every switch (its unknown outcome is assumed to
 * favor the safest candidate), so it cannot have changed the advice.
 *
 * Finite samples. The result reports the advised cell's fraction of
 * completed rounds won. If it won none, the answer is UNAVAILABLE
 * (insufficient rollouts): zero observed wins is not a zero chance, and
 * with (almost) no wins anywhere nothing ranks the moves. All rounds won is
 * not certainty either: the standard error is the standard deviation of the
 * Jeffreys posterior Beta(wins + 1/2, losses + 1/2), which is close to the
 * binomial one inside but never 0, times the finite-population correction
 * for rounds over a listing; only a complete pass over a listing (the
 * policy's exact win rate, never 0 or 1) reports 0. Estimated survival is
 * kept below 1 as well.
 * ====================================================================== */

typedef struct pl_cand {
    uint64_t column;  /* hash of the cell's outcomes over the layouts */
    double survival;
    double gini;      /* diversity of the clue it shows */
    double open;      /* chance that it shows a 0 */
    uint32_t cell;
    uint32_t frontier;
} pl_cand;

static uint8_t pl_outcome(const pl_ctx *ctx, const uint8_t *row, uint32_t c) {
    if (row[c]) return PL_MINE;
    uint32_t near = 0;
    for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) near += row[ctx->nbr[8u * c + i]];
    return (uint8_t)near;
}

static bool pl_same_column(const pl_ctx *ctx, const pl_root *root, uint32_t a, uint32_t b) {
    for (uint32_t l = 0; l < root->info.count; l++) {
        const uint8_t *row = root->layouts + (size_t)l * ctx->cells;
        if (pl_outcome(ctx, row, a) != pl_outcome(ctx, row, b)) return false;
    }
    return true;
}

static bool pl_cand_before(const void *data, uint32_t a, uint32_t b, uint32_t key) {
    const pl_cand *x = (const pl_cand *)data + a;
    const pl_cand *y = (const pl_cand *)data + b;
    if (key == PL_RANK_OPEN) {
        if (x->open != y->open) return x->open > y->open;
        if (x->survival != y->survival) return x->survival > y->survival;
    } else if (key == PL_RANK_INFO) {
        double ix = x->survival * (1.0 + x->gini), iy = y->survival * (1.0 + y->gini);
        if (ix != iy) return ix > iy;
        if (x->survival != y->survival) return x->survival > y->survival;
    } else if (key == PL_RANK_INTERIOR) {
        if (x->survival != y->survival) return x->survival > y->survival;
        if (x->open != y->open) return x->open > y->open;
    } else {
        if (x->survival != y->survival) return x->survival > y->survival;
        if (x->gini != y->gini) return x->gini > y->gini;
        if (x->open != y->open) return x->open > y->open;
    }
    return x->cell < y->cell;
}

/* The clue cell c would show, from the root marginals with its hidden
 * neighbors taken as independent: dist[k] = P(k mines next to it). A cheap
 * proxy that never looks at the draws the rollouts are scored on. */
static void pl_clue_proxy(const pl_ctx *ctx, const uint8_t *flags, const double *probs,
                          uint32_t c, double dist[PL_MINE]) {
    for (uint32_t k = 0; k < PL_MINE; k++) dist[k] = 0.0;
    dist[0] = 1.0;
    uint32_t m = 0;
    for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) {
        uint32_t y = ctx->nbr[8u * c + i];
        if (ctx->clue[y] != MS_CLUE_HIDDEN) continue;
        double q = (flags[y] & MS_PCELL_VALUE) ? probs[y] : 0.5;
        for (uint32_t k = m + 1u; k > 0; k--) dist[k] = dist[k] * (1.0 - q) + dist[k - 1u] * q;
        dist[0] *= 1.0 - q;
        m++;
    }
}

static pl_rc pl_shortlist(pl_ctx *ctx, const pl_root *root, pl_cand **out, uint32_t *count) {
    uint32_t n = root->info.count;
    const uint8_t *flags = ms_result_flags(root->prob);
    const double *probs = ms_result_probabilities(root->prob);
    pl_cand *all;
    if (!PL_NEW(ctx, all, ctx->hidden)) return pl_nomem(ctx);
    uint32_t ne = 0;
    double top = 0.0;
    for (uint32_t c = 0; c < ctx->cells; c++) {
        if (ctx->clue[c] != MS_CLUE_HIDDEN || (flags[c] & MS_PCELL_PROVEN_MINE)) continue;
        uint32_t hist[PL_MINE];
        for (uint32_t x = 0; x < PL_MINE; x++) hist[x] = 0;
        uint32_t safe = 0;
        uint64_t column = 0x84222325CBF29CE4ull;
        for (uint32_t l = 0; l < n; l++) {
            uint8_t o = pl_outcome(ctx, root->layouts + (size_t)l * ctx->cells, c);
            column = rt_mix64(column ^ o);
            if (o == PL_MINE) continue;
            safe++;
            hist[o]++;
        }
        if (safe == 0) continue;
        if (rt_meter_work(&ctx->meter, (n >> 4) + 1u)) return PL_STOP;
        /* A listing gives exact figures; draws are scored by the rollouts,
         * so ranking cells by their outcomes in those same draws would
         * favor lucky cells (winner's curse): the root marginals decide. */
        double survival, open, dist[PL_MINE];
        if (root->info.exhaustive) {
            survival = (double)safe / (double)n;
            open = (double)hist[0] / (double)n;
            for (uint32_t x = 0; x < PL_MINE; x++) dist[x] = (double)hist[x] / (double)safe;
        } else {
            survival = (flags[c] & MS_PCELL_VALUE) ? 1.0 - probs[c] : (double)safe / (double)n;
            if (!(survival > 0.0)) survival = (double)safe / (double)n;
            pl_clue_proxy(ctx, flags, probs, c, dist);
            open = survival * dist[0];
        }
        /* Unproven cells: a sampled or rounded 1 is not certainty. */
        if (survival > PL_SURVIVAL_MAX) survival = PL_SURVIVAL_MAX;
        double gini = 1.0;
        for (uint32_t x = 0; x < PL_MINE; x++) gini -= dist[x] * dist[x];
        uint32_t frontier = 0;
        for (uint32_t i = 0; i < ctx->nbr_n[c]; i++) {
            if (ctx->clue[ctx->nbr[8u * c + i]] != MS_CLUE_HIDDEN) frontier = 1;
        }
        pl_cand *e = &all[ne++];
        e->column = column;
        e->survival = survival;
        e->gini = gini > 0.0 ? gini : 0.0;
        e->open = open;
        e->cell = c;
        e->frontier = frontier;
        if (survival > top) top = survival;
    }
    if (ne == 0) return pl_fail(ctx, MS_ERR_INTERNAL); /* every layout has a safe hidden cell */

    uint32_t *order, *tmp, len[PL_RANKS];
    size_t slots;
    if (!rt_mul_size(ne, PL_RANKS, &slots)) return PL_NOMEM;
    if (!PL_NEW(ctx, order, slots) || !PL_NEW(ctx, tmp, ne)) return pl_nomem(ctx);
    for (uint32_t r = 0; r < PL_RANKS; r++) {
        uint32_t *list = order + (size_t)r * ne;
        len[r] = 0;
        for (uint32_t i = 0; i < ne; i++) {
            if (r == PL_RANK_INTERIOR && all[i].frontier) continue;
            if (r == PL_RANK_FRONTIER && !all[i].frontier) continue;
            list[len[r]++] = i;
        }
        pl_sort(list, tmp, len[r], pl_cand_before, all, r);
    }
    uint32_t limit = pl_min_u32(ctx->limits->candidate_limit, ne);
    pl_cand *chosen;
    uint8_t *taken;
    if (!PL_NEW(ctx, chosen, limit) || !PL_NEW(ctx, taken, ne)) return pl_nomem(ctx);
    uint32_t pos[PL_RANKS], nc = 0;
    for (uint32_t r = 0; r < PL_RANKS; r++) pos[r] = 0;
    double floor = PL_SHORTLIST_FLOOR * top;
    bool progress = true;
    while (nc < limit && progress) {
        progress = false;
        for (uint32_t r = 0; r < PL_RANKS && nc < limit; r++) {
            const uint32_t *list = order + (size_t)r * ne;
            while (pos[r] < len[r]) {
                uint32_t i = list[pos[r]++];
                if (taken[i] || (r != PL_RANK_SAFE && all[i].survival < floor)) continue;
                taken[i] = 1u;
                bool dup = false;
                for (uint32_t j = 0; j < nc && !dup; j++) {
                    dup = chosen[j].column == all[i].column &&
                          pl_same_column(ctx, root, chosen[j].cell, all[i].cell);
                }
                if (dup) continue;
                base_memcpy(&chosen[nc++], &all[i], sizeof(pl_cand));
                progress = true;
                break;
            }
        }
    }
    *out = chosen;
    *count = nc;
    return PL_OK;
}

typedef struct pl_tally {
    uint32_t trials;
    uint32_t incomplete;
    uint32_t completed;
    uint32_t *wins;   /* per candidate, completed rounds only */
    uint32_t *gain;   /* completed rounds it won and candidate 0 lost */
    uint32_t *loss;   /* completed rounds candidate 0 won and it lost */
    uint32_t *played; /* per candidate, finished games incl. discarded rounds */
    uint8_t *won;     /* per candidate, the current round */
    const uint32_t *order; /* layout row of each round */
    uint64_t *bits;   /* per candidate: completed rounds < 64 it won */
    bool cut;         /* a game hit rollout_step_limit */
} pl_tally;

static pl_rc pl_rounds(pl_ctx *ctx, const pl_root *root, const pl_cand *cand, uint32_t ncand,
                       pl_tally *t) {
    base_memset(t, 0, sizeof(*t));
    if (!PL_NEW(ctx, t->wins, ncand) || !PL_NEW(ctx, t->gain, ncand) ||
        !PL_NEW(ctx, t->loss, ncand) || !PL_NEW(ctx, t->played, ncand) ||
        !PL_NEW(ctx, t->won, ncand) || !PL_NEW(ctx, t->bits, ncand)) {
        return pl_nomem(ctx);
    }
    pl_roll ro;
    PL_TRY(pl_roll_open(&ro, ctx, root));
    /* Draws arrive in random order already; a complete listing comes in
     * rank order, so visit it in a random permutation (public seed): any
     * prefix the deadline leaves is then a uniform sample without
     * replacement, never the first layouts in rank order. */
    uint32_t n = root->info.count, *order;
    if (!PL_NEW(ctx, order, n)) return pl_nomem(ctx);
    for (uint32_t r = 0; r < n; r++) order[r] = r;
    t->order = order;
    if (root->info.exhaustive) {
        rt_rng rng;
        bool explicit_seed = (ctx->limits->flags & MS_PLAN_EXPLICIT_SEED) != 0;
        uint64_t seed = explicit_seed ? ctx->limits->seed : ctx->hash;
        rt_rng_seed(&rng, rt_mix64(seed ^ PL_ROUND_STREAM));
        rt_rng_choose(&rng, order, n, n);
    }
    for (uint32_t r = 0; r < n; r++) {
        if (rt_meter_check(&ctx->meter)) break; /* no round starts after the deadline */
        t->trials++;
        PL_TRY(env_load(&ro.env, ctx, root->layouts + (size_t)order[r] * ctx->cells));
        uint32_t ended = PL_WON;
        for (uint32_t k = 0; k < ncand; k++) {
            uint32_t outcome;
            PL_TRY(pl_play(&ro, cand[k].cell, &outcome, NULL));
            if (outcome == PL_CUT || outcome == PL_TIMED) {
                ended = outcome;
                break;
            }
            t->played[k]++;
            t->won[k] = outcome == PL_WON ? 1u : 0u;
        }
        if (ended == PL_WON) {
            for (uint32_t k = 0; k < ncand && t->completed < 64u; k++) {
                if (t->won[k]) t->bits[k] |= (uint64_t)1 << t->completed;
            }
            t->completed++;
            for (uint32_t k = 0; k < ncand; k++) {
                t->wins[k] += t->won[k];
                t->gain[k] += t->won[k] && !t->won[0];
                t->loss[k] += !t->won[k] && t->won[0];
            }
        } else {
            /* Stop at the first unfinished round: its partial games are
             * discarded for every candidate alike. */
            t->incomplete++;
            t->cut = ended == PL_CUT;
            break;
        }
    }
    return PL_OK;
}

static pl_rc pl_estimate(pl_ctx *ctx, const pl_root *root, ms_plan_result *out, pl_stats *stats) {
    const ms_plan_limits *limits = ctx->limits;
    out->layouts = root->info.count;
    out->posterior_exact = pl_posterior_exact(root);
    if (root->info.count == 0) {
        pl_result_unavailable(out, pl_posterior_reason(ctx, root));
        return PL_OK;
    }
    if (limits->candidate_limit == 0) {
        pl_result_unavailable(out, MS_PLAN_REASON_BUDGET);
        return PL_OK;
    }
    uint32_t need = limits->min_rollouts > 0 ? limits->min_rollouts : 1u;
    bool listing = root->info.exhaustive != 0;
    if (!listing && root->info.count < need) {
        /* One round per draw: the minimum is out of reach. */
        pl_result_unavailable(out, MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS);
        return PL_OK;
    }
    pl_cand *cand;
    uint32_t ncand;
    PL_TRY(pl_shortlist(ctx, root, &cand, &ncand));
    out->candidates = ncand;
    pl_tally tally;
    PL_TRY(pl_rounds(ctx, root, cand, ncand, &tally));
    out->trials = tally.trials;
    out->incomplete = tally.incomplete;
    if (stats != NULL) {
        stats->candidates = ncand;
        stats->rounds = tally.completed;
        for (uint32_t k = 0; k < ncand && k < 64u; k++) {
            stats->cell[k] = cand[k].cell;
            stats->wins[k] = tally.wins[k];
            stats->played[k] = tally.played[k];
            stats->gain[k] = tally.gain[k];
            stats->loss[k] = tally.loss[k];
            stats->survival[k] = cand[k].survival;
            stats->round_wins[k] = tally.bits[k];
        }
        stats->exhaustive = listing ? 1u : 0u;
        for (uint32_t r = 0; r < tally.trials && r < 64u; r++) stats->order[r] = tally.order[r];
    }
    if (tally.cut) {
        /* A game hit rollout_step_limit. Long games are often wins, and how
         * long they last depends on the first move: the completed rounds
         * would be a length-selected, biased subset. No estimate. */
        pl_result_unavailable(out, MS_PLAN_REASON_BUDGET);
        return PL_OK;
    }
    /* A complete pass over a listing has no sampling error: it needs no
     * minimum number of rounds. */
    bool complete = listing && tally.completed == root->info.count;
    if (tally.completed < need && !complete) {
        pl_result_unavailable(out, MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS);
        return PL_OK;
    }
    uint32_t best = 0;
    if (complete) {
        /* Every layout once: the counts are the policy's exact win counts. */
        for (uint32_t k = 1; k < ncand; k++) {
            if (tally.wins[k] > tally.wins[best] ||
                (tally.wins[k] == tally.wins[best] && cand[k].survival > cand[best].survival)) {
                best = k;
            }
        }
    } else {
        /* Sampled rounds: the safest candidate (0) unless another one's
         * paired advantage clears the noise, then the clearest such one.
         * The round the deadline discarded (if any) counts against the
         * switch, so no outcome of it could have reversed the advice. */
        double top = 0.0, unknown = (double)tally.incomplete;
        for (uint32_t k = 1; k < ncand; k++) {
            double gain = (double)tally.gain[k], loss = (double)tally.loss[k];
            double margin = gain - loss - unknown - PL_SWITCH_Z * pl_sqrt(gain + loss + unknown);
            if (margin > top) {
                top = margin;
                best = k;
            }
        }
    }
    uint32_t done = tally.completed, wins = tally.wins[best];
    if (wins == 0) {
        /* The advised move won no completed round, so nothing ranks the
         * moves, and zero observed wins is not a zero chance. */
        pl_result_unavailable(out, MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS);
        return PL_OK;
    }
    double p = (double)wins / (double)done;
    double variance = 0.0;
    if (!complete) {
        /* Variance of the Jeffreys posterior Beta(wins + 1/2, losses + 1/2):
         * close to the binomial p (1 - p) / n inside, never 0 when every
         * round was won. Rounds over a listing sample it without
         * replacement: times the finite-population correction. */
        double a = (double)wins + 0.5, b = (double)(done - wins) + 0.5, sum = a + b;
        variance = a * b / (sum * sum * (sum + 1.0));
        if (listing && root->info.count > 1u) {
            uint32_t population = root->info.count;
            variance *= (double)(population - done) / (double)(population - 1u);
        }
    }
    double se = pl_sqrt(variance);
    out->status = MS_PLAN_ESTIMATED;
    out->reason = MS_PLAN_REASON_NONE;
    out->cell = cand[best].cell;
    out->survival_probability = cand[best].survival;
    out->win_probability = p;
    out->standard_error = se < 0.5 ? se : 0.5;
    return PL_OK;
}

static pl_rc pl_plan(pl_ctx *ctx, ms_plan_result *out, pl_stats *stats) {
    const ms_plan_limits *limits = ctx->limits;
    PL_TRY(pl_prepare(ctx));
    pl_root root;
    PL_TRY(pl_root_infer(ctx, &root));
    if (pl_certain_moves(ctx, &root)) {
        pl_result_none(out, MS_PLAN_REASON_CERTAIN_MOVES);
        return PL_OK;
    }
    out->layouts = root.info.count;
    out->posterior_exact = pl_posterior_exact(&root);
    if (!(limits->time_budget_ms > 0.0)) {
        pl_result_unavailable(out, MS_PLAN_REASON_BUDGET); /* a zero budget is explicit */
        return PL_OK;
    }
    uint64_t nodes = 0;
    if (root.info.exhaustive && root.info.count >= 2u && limits->exact_node_limit > 0 &&
        !rt_meter_check(&ctx->meter)) {
        double now = rt_clock_now(ctx->clock);
        double until = ctx->deadline_ms < HUGE_VAL
                           ? now + PL_EXACT_SHARE * (ctx->deadline_ms - now)
                           : HUGE_VAL;
        ex_answer ans;
        PL_TRY(ex_run(ctx, ctx->clock, ctx->clue, root.layouts, root.info.count,
                      limits->exact_node_limit, until, &ans));
        nodes = ans.nodes;
        if (ans.complete && ans.certain) return pl_fail(ctx, MS_ERR_INTERNAL);
        if (ans.complete) {
            uint32_t n = root.info.count;
            out->status = MS_PLAN_EXACT;
            out->reason = MS_PLAN_REASON_NONE;
            out->cell = ans.cell;
            out->candidates = ans.candidates;
            out->layouts = n;
            out->search_nodes = pl_saturate(nodes);
            out->posterior_exact = 1u;
            out->survival_probability = (double)ans.safe / (double)n;
            out->win_probability = (double)ans.wins / (double)n;
            out->standard_error = 0.0;
            out->exact_wins = ans.wins;
            out->exact_total = n;
            return PL_OK;
        }
    }
    out->search_nodes = pl_saturate(nodes);
    return pl_estimate(ctx, &root, out, stats);
}

static ms_status pl_entry(const void *obs, size_t obs_len, const ms_plan_limits *limits,
                          rt_mem *workspace, rt_clock *clock, ms_plan_result *result,
                          size_t result_len, pl_stats *stats) {
    ms_status status = pl_check_args(obs, obs_len, limits, workspace, clock, result, result_len);
    if (status != MS_OK) return status;
    pl_ctx ctx;
    pl_open(&ctx, obs, obs_len, limits, workspace, clock);
    ms_plan_result out;
    pl_result_start(&ctx, &out);
    bool done = false;
    pl_rc rc = pl_placeholders(&ctx, &out, &done);
    if (rc == PL_OK && !done) rc = pl_plan(&ctx, &out, stats);
    if (stats != NULL) {
        stats->aborts_inference = ctx.aborts_inference;
        stats->aborts_tail = ctx.aborts_tail;
    }
    if (rc == PL_NOMEM || rc == PL_STOP) {
        pl_result_unavailable(&out, MS_PLAN_REASON_BUDGET);
        rc = PL_OK;
    }
    out.elapsed_ms = pl_elapsed(&ctx);
    status = pl_close(&ctx, rc);
    if (status == MS_OK && ms_plan_result_validate(obs, obs_len, &out, sizeof(out)) != MS_OK) {
        status = MS_ERR_INTERNAL;
    }
    if (status == MS_OK) base_memcpy(result, &out, sizeof(out));
    return status;
}

ms_status ms_plan(const void *obs, size_t obs_len, const ms_plan_limits *limits,
                  rt_mem *workspace, rt_clock *clock, ms_plan_result *result, size_t result_len) {
    return pl_entry(obs, obs_len, limits, workspace, clock, result, result_len, NULL);
}

#if MS_TEST_SUITE_PLANNER
/* ======================================================================
 * Test hooks (compiled only into the planner test build; the production
 * reactor never contains them)
 * ====================================================================== */

void ms_plan_test_set_fault(const ms_plan_test_fault *fault) {
    if (fault != NULL) {
        base_memcpy(&pl_fault, fault, sizeof(pl_fault));
    } else {
        base_memset(&pl_fault, 0, sizeof(pl_fault));
    }
}

ms_status ms_plan_test_run(const void *obs, size_t obs_len, const ms_plan_limits *limits,
                           rt_mem *workspace, rt_clock *clock, ms_plan_result *result,
                           size_t result_len, ms_plan_test_stats *stats) {
    if (stats == NULL) return MS_ERR_INVALID_BUFFER;
    pl_stats internal;
    base_memset(&internal, 0, sizeof(internal));
    ms_status status = pl_entry(obs, obs_len, limits, workspace, clock, result, result_len,
                                &internal);
    base_memset(stats, 0, sizeof(*stats));
    stats->candidates = internal.candidates;
    stats->rounds = internal.rounds;
    for (uint32_t k = 0; k < MS_PLAN_TEST_MAX_CANDIDATES && k < 64u; k++) {
        stats->cell[k] = internal.cell[k];
        stats->wins[k] = internal.wins[k];
        stats->played[k] = internal.played[k];
        stats->gain[k] = internal.gain[k];
        stats->loss[k] = internal.loss[k];
        stats->survival[k] = internal.survival[k];
        stats->order[k] = internal.order[k];
        stats->round_wins[k] = internal.round_wins[k];
    }
    stats->exhaustive = internal.exhaustive;
    stats->nested_aborts_inference = internal.aborts_inference;
    stats->nested_aborts_tail = internal.aborts_tail;
    stats->nested_inference_calls = pl_fault_calls[PL_FAULT_INFER];
    stats->nested_tail_calls = pl_fault_calls[PL_FAULT_TAIL];
    return status;
}

ms_status ms_plan_test_playout(const void *obs, size_t obs_len, const ms_plan_limits *limits,
                               rt_mem *workspace, rt_clock *clock, const uint8_t *layout,
                               uint32_t first, uint32_t *outcome, uint32_t *trace,
                               uint32_t trace_cap, uint32_t *trace_len) {
    ms_plan_result scratch;
    if (layout == NULL || outcome == NULL || trace_len == NULL ||
        (trace == NULL && trace_cap != 0)) {
        return MS_ERR_INVALID_BUFFER;
    }
    ms_status status = pl_check_args(obs, obs_len, limits, workspace, clock, &scratch,
                                     sizeof(scratch));
    if (status != MS_OK) return status;
    const ms_obs_header *h = (const ms_obs_header *)obs;
    uint32_t cells = h->width * h->height, mines = 0;
    const uint8_t *clue = ms_obs_clues(obs);
    if (first >= cells || clue[first] != MS_CLUE_HIDDEN) return MS_ERR_INVALID_BUFFER;
    for (uint32_t c = 0; c < cells; c++) {
        if (layout[c] > 1u || (layout[c] && clue[c] != MS_CLUE_HIDDEN)) {
            return MS_ERR_INVALID_BUFFER;
        }
        mines += layout[c];
        if (clue[c] == MS_CLUE_HIDDEN) continue;
        uint32_t around[8], near = 0;
        uint32_t n = rt_grid_neighbors(h->width, h->height, c, around);
        for (uint32_t i = 0; i < n; i++) near += layout[around[i]];
        if (near != clue[c]) return MS_ERR_INVALID_BUFFER;
    }
    if (mines != h->total_mines) return MS_ERR_INVALID_BUFFER;
    pl_ctx ctx;
    pl_open(&ctx, obs, obs_len, limits, workspace, clock);
    ms_plan_result out;
    pl_result_start(&ctx, &out);
    bool done = false;
    pl_trace tr;
    tr.cells = trace;
    tr.cap = trace_cap;
    tr.len = 0;
    pl_rc rc = pl_placeholders(&ctx, &out, &done);
    if (rc == PL_OK && done) rc = pl_fail(&ctx, MS_ERR_INVALID_OBSERVATION);
    pl_root root;
    pl_roll ro;
    if (rc == PL_OK) rc = pl_prepare(&ctx);
    if (rc == PL_OK) rc = pl_root_infer(&ctx, &root);
    if (rc == PL_OK) rc = pl_roll_open(&ro, &ctx, &root);
    if (rc == PL_OK) rc = env_load(&ro.env, &ctx, layout);
    if (rc == PL_OK) rc = pl_play(&ro, first, outcome, &tr);
    if (rc == PL_NOMEM) rc = pl_fail(&ctx, MS_ERR_RESOURCE_EXHAUSTED);
    *trace_len = tr.len;
    return pl_close(&ctx, rc);
}
#endif
