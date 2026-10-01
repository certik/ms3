#pragma once

#include "engine.h"

/* Additive advisor API. The existing game/probability ABI is unchanged.
 * The planner sees public clues only and never submits moves to the game.
 * Exact means a complete belief-state search over every compatible layout.
 * Estimated means guided full-game rollouts, not a proof or optimal policy.
 */
#define MS_PLANNER_VERSION 1u
#define MS_MAGIC_PLAN_LIMITS 0x4D530005u
#define MS_MAGIC_PLAN_RESULT 0x4D530006u
#define MS_PLAN_NO_CELL UINT32_MAX
#define MS_PLAN_EXPLICIT_SEED 1u

#define MS_PLAN_DEFAULT_EXACT_LAYOUTS 256u
#define MS_PLAN_DEFAULT_EXACT_NODES 100000u
#define MS_PLAN_DEFAULT_SAMPLES 96u
#define MS_PLAN_DEFAULT_CANDIDATES 8u
#define MS_PLAN_DEFAULT_MIN_ROLLOUTS 16u
#define MS_PLAN_DEFAULT_TIME_MS 3000.0
#define MS_PLAN_DEFAULT_MEMORY ((uint64_t)256u << 20)
/* Reveals per simulated game. Every reveal opens at least one safe cell, so
 * this (the largest board) never cuts a game short. */
#define MS_PLAN_DEFAULT_ROLLOUT_STEPS MS_SOLVER_MAX_CELLS

typedef enum ms_plan_status {
    MS_PLAN_NONE = 0,
    MS_PLAN_EXACT = 1,
    MS_PLAN_ESTIMATED = 2,
    MS_PLAN_UNAVAILABLE = 3
} ms_plan_status;

typedef enum ms_plan_reason {
    MS_PLAN_REASON_NONE = 0,
    MS_PLAN_REASON_CERTAIN_MOVES = 1,
    MS_PLAN_REASON_FINISHED = 2,
    MS_PLAN_REASON_NO_SAMPLES = 3,
    MS_PLAN_REASON_BUDGET = 4,
    MS_PLAN_REASON_INSUFFICIENT_ROLLOUTS = 5,
    MS_PLAN_REASON_POSTERIOR_UNAVAILABLE = 6,
    MS_PLAN_REASON_NOT_STARTED = 7
} ms_plan_reason;

/* Every budget may be 0: the answer then degrades (EXACT -> ESTIMATED ->
 * UNAVAILABLE with a reason), never an error. Validation (in this order):
 * NULL/misaligned, magic or version -> MS_ERR_INVALID_BUFFER; unknown
 * flags, NaN or negative time, nonzero reserved -> MS_ERR_INVALID_LIMITS. */
typedef struct ms_plan_limits {
    uint32_t magic;              /* MS_MAGIC_PLAN_LIMITS */
    uint32_t version;            /* MS_PLANNER_VERSION */
    uint32_t exact_layout_limit; /* exact search only when every compatible
                                    layout fits (clamped so the layout rows
                                    take at most 1/4 of memory_budget_bytes) */
    uint32_t exact_node_limit;   /* positions the exact search may expand */
    uint32_t sample_count;       /* posterior draws = paired rollout rounds
                                    (same clamp as exact_layout_limit); a
                                    complete listing is instead played once
                                    per layout, in a seeded random order */
    uint32_t candidate_limit;    /* first reveals compared by rollouts */
    uint32_t rollout_step_limit; /* reveals per simulated game; a game cut
                                    short ends the rollouts with
                                    UNAVAILABLE/BUDGET (the rounds that
                                    complete would be length-selected) */
    uint32_t flags;              /* MS_PLAN_EXPLICIT_SEED */
    double time_budget_ms;       /* whole call on the injected clock, >= 0,
                                    +infinity allowed; 0 -> UNAVAILABLE */
    uint64_t memory_budget_bytes; /* cap on the planner's workspace bytes,
                                     nested inference included */
    uint64_t seed;               /* mixed into every seed iff
                                    MS_PLAN_EXPLICIT_SEED, else ms_obs_hash */
    uint32_t min_rollouts;       /* completed rounds needed (0 means 1),
                                    except after a complete pass over a
                                    listing (no sampling error) */
    uint32_t reserved;           /* 0 */
} ms_plan_limits;

/* Outcomes, in the order ms_plan decides them (status/reason):
 *   NONE/NOT_STARTED     nothing revealed (decided first, whatever the limits)
 *   NONE/FINISHED        only mines hidden (every clue is checked: one that
 *                        disagrees is MS_ERR_INCONSISTENT)
 *   NONE/CERTAIN_MOVES   a hidden cell is proven safe by the root inference,
 *                        or safe in every layout of a complete listing:
 *                        reveal those first, no guess is advised
 *   UNAVAILABLE/BUDGET   time_budget_ms 0; candidate_limit 0 (rollouts
 *                        needed); posterior generation or any later phase
 *                        stopped by the deadline or the byte budget; the
 *                        byte budget left room for no layout row; or a
 *                        simulated game hit rollout_step_limit
 *   UNAVAILABLE/NO_SAMPLES  no complete listing and sample_count 0
 *   UNAVAILABLE/POSTERIOR_UNAVAILABLE  the root inference was UNAVAILABLE
 *                        (or produced no layout) for another reason, e.g.
 *                        too few effective samples
 *   EXACT                a complete listing of >= 2 layouts and the exact
 *                        search finished (exact_node_limit, half the time
 *                        left, the byte budget)
 *   ESTIMATED            rollouts completed max(min_rollouts, 1) paired
 *                        rounds, or a complete pass over a listing
 *   UNAVAILABLE/INSUFFICIENT_ROLLOUTS  the rollouts give no usable
 *                        evidence: fewer completed rounds than that (fewer
 *                        draws than min_rollouts, or the deadline), or the
 *                        advised move won none of them (zero observed wins
 *                        is not a zero chance)
 * EXACT: the advised cell's policy wins exactly exact_wins of the
 * exact_total equally likely layouts; win_probability is exact_wins /
 * exact_total and survival_probability the cell's exact safe fraction. Ties
 * prefer the safer cell, then more distinct clues, then the lower index.
 * ESTIMATED: win_probability = wins / (trials - incomplete) of the advised
 * cell under the continuation policy (not an optimal policy, not a proof),
 * with wins >= 1; survival_probability the best available marginal estimate,
 * strictly below 1 (it may be below win_probability, both being estimates);
 * standard_error the standard deviation of the Jeffreys posterior
 * Beta(wins + 1/2, losses + 1/2) - close to the binomial standard error, but
 * positive even when every round was won - times the finite-population
 * correction when the rounds sample a complete listing without replacement
 * (0 only after a complete pass, whose fraction is the policy's exact win
 * rate and never 0 or 1). Estimates are never certainties: a UI must not
 * show an ESTIMATED value as 0% or 100%. The advice is the safest
 * shortlisted cell unless another one won clearly more of the same paired
 * rounds (or, after a complete pass, simply the most wins). Rounds stop at
 * the first unfinished one, so incomplete is 0 or 1: a round the deadline
 * cut is discarded for every candidate and counted against any switch away
 * from the safest cell. The completed rounds then lean slightly toward
 * short games (by about one round at most, the same rounds for every
 * candidate): show "wins / completed rounds" with the discarded round.
 *
 * Canonical results (ms_plan_result_validate; every MS_OK result passes):
 * - buffer: 112 bytes, 8-byte aligned, magic and version, else
 *   MS_ERR_INVALID_BUFFER (an invalid observation reports its own error
 *   first); every rule below is MS_ERR_INVALID_RESULT;
 * - every status: width, height, total_mines, revealed, observation_hash
 *   equal the observation's; reserved 0; status 0..3; all doubles finite;
 *   elapsed_ms >= 0; posterior_exact 0 or 1, and 0 when layouts is 0;
 *   incomplete <= 1 and incomplete <= trials <= layouts; candidates <=
 *   hidden cells, >= 1 when trials > 0 and 0 when layouts is 0;
 *   search_nodes 0 unless layouts >= 2 and posterior_exact is 1 (the exact
 *   search runs only on a complete listing);
 * - NONE: reason NOT_STARTED iff revealed is 0, FINISHED iff revealed > 0
 *   and hidden cells == total_mines, else CERTAIN_MOVES; cell NO_CELL; every
 *   other field 0 (identity fields and elapsed_ms aside);
 * - EXACT: reason NONE; a guess position (revealed > 0, hidden cells >
 *   total_mines); cell hidden; 2 <= exact_total == layouts; 1 <= exact_wins <
 *   exact_total; win_probability == exact_wins / exact_total and
 *   survival_probability == s / exact_total (IEEE division) for an integer
 *   s, the cell's safe layouts; win_probability <= survival_probability,
 *   0 < survival < 1; standard_error 0; trials 0; search_nodes >= 1;
 *   candidates > hidden cells - total_mines, hence >= 2 (the hidden cells
 *   safe in some layout, all searched: fewer than total_mines cells are
 *   mines in every one of >= 2 distinct layouts); posterior_exact 1;
 * - ESTIMATED: reason NONE; a guess position; cell hidden; trials -
 *   incomplete >= 1 and win_probability == k / (trials - incomplete) for an
 *   integer k >= 1; 0 < survival_probability < 1; 0 <= standard_error <=
 *   0.5, and > 0 when win_probability is 1; exact_wins and exact_total 0;
 *   candidates >= 1;
 * - UNAVAILABLE: reason BUDGET, NO_SAMPLES, INSUFFICIENT_ROLLOUTS or
 *   POSTERIOR_UNAVAILABLE; a guess position; cell NO_CELL; both
 *   probabilities, standard_error, exact_wins and exact_total 0. The other
 *   counts record how far the planner got: NO_SAMPLES and
 *   POSTERIOR_UNAVAILABLE have layouts, candidates and search_nodes 0;
 *   INSUFFICIENT_ROLLOUTS has layouts >= 1.
 * ms_plan also keeps every count within the limits of its call, which a
 * result does not carry: layouts <= max(exact_layout_limit, sample_count),
 * exact_total <= exact_layout_limit, search_nodes <= exact_node_limit, and
 * candidates <= candidate_limit unless EXACT.
 * A valid result binds to its observation only (observation_hash): flags
 * and anything else outside the observation do not invalidate it.
 */
typedef struct ms_plan_result {
    uint32_t magic;            /* MS_MAGIC_PLAN_RESULT */
    uint32_t version;          /* MS_PLANNER_VERSION */
    uint32_t status;           /* ms_plan_status */
    uint32_t reason;           /* ms_plan_reason; NONE for EXACT/ESTIMATED */
    uint32_t width;            /* copied from the observation */
    uint32_t height;
    uint32_t total_mines;
    uint32_t revealed;
    uint64_t observation_hash; /* ms_obs_hash() of the observation */
    uint32_t cell; /* MS_PLAN_NO_CELL unless exact/estimated */
    uint32_t candidates;       /* EXACT: hidden cells safe in some layout (all
                                  covered); else first reveals simulated */
    uint32_t layouts;          /* posterior layouts generated */
    uint32_t trials; /* paired rollout rounds started per candidate */
    uint32_t incomplete; /* unfinished rounds; never silently counted as wins
                            (0 or 1: rounds stop at the first one) */
    uint32_t search_nodes;     /* exact-search positions expanded */
    uint32_t posterior_exact;  /* layouts drawn from (or listing) the exact
                                  posterior, not an importance-weighted one */
    uint32_t reserved;         /* 0 */
    double survival_probability;
    double win_probability; /* exact, or fraction of completed rollouts won */
    double standard_error; /* sampling diagnostic, not a calibrated guarantee */
    double elapsed_ms;         /* clock span of the call, finite >= 0 */
    uint32_t exact_wins; /* integer successful-layout count for the best policy */
    uint32_t exact_total; /* exhaustive posterior size, 0 unless EXACT */
} ms_plan_result;

_Static_assert(sizeof(ms_plan_limits) == 64, "ms_plan_limits layout");
_Static_assert(MS_OFFSETOF(ms_plan_limits, time_budget_ms) == 32, "ms_plan_limits layout");
_Static_assert(sizeof(ms_plan_result) == 112, "ms_plan_result layout");
_Static_assert(MS_OFFSETOF(ms_plan_result, observation_hash) == 32, "ms_plan_result layout");
_Static_assert(MS_OFFSETOF(ms_plan_result, survival_probability) == 72, "ms_plan_result layout");

void ms_plan_limits_default(ms_plan_limits *limits);
ms_status ms_plan_limits_validate(const ms_plan_limits *limits);
ms_status ms_plan_result_validate(const void *obs, size_t obs_len,
                                   const ms_plan_result *result, size_t result_len);

/* Same address/range/alignment/non-overlap and workspace lifetime invariants
 * as ms_solve, plus *workspace and *clock disjoint from each other (all
 * seven ranges pairwise disjoint, checked on addresses before anything is
 * read or written). Defaults share a single wall-clock and allocation budget
 * across posterior generation, exact search, and every rollout. Interrupted
 * exact search is never labeled exact. Rollout decisions depend only on
 * their simulated public observations, not the evaluator's hidden board,
 * and never on time: nested inference and tail searches run on a virtual
 * clock that stands still until the deadline and are cancelled by it from
 * inside; a round whose nested work the deadline reached is discarded.
 * MS_OK always provides a canonical result, including explicit NONE and
 * UNAVAILABLE outcomes. Inputs and rejected calls remain untouched.
 * Errors: MS_ERR_INVALID_BUFFER / _OBSERVATION / _LIMITS for rejected
 * arguments (no write, allocation or clock reading happens);
 * MS_ERR_INCONSISTENT when the clues and total admit no layout (as
 * ms_solve); MS_ERR_INTERNAL for a broken invariant, never absorbed into
 * advice, also inside nested work the deadline reached (never mistaken for
 * a cancellation). Budget, memory and sampling shortfalls are never errors.
 */
ms_status ms_plan(const void *obs, size_t obs_len, const ms_plan_limits *limits,
                   rt_mem *workspace, rt_clock *clock,
                   ms_plan_result *result, size_t result_len);

#if MS_TEST_SUITE_PLANNER
/* TEST HOOKS: compiled only into the planner test build (scripts/build.mjs
 * defines MS_TEST_SUITE_PLANNER there); the production reactor contains no
 * such function, so nothing in the browser can hand the planner a layout. */
#define MS_PLAN_TEST_MAX_CANDIDATES 64u

typedef struct ms_plan_test_stats {
    uint32_t candidates;  /* shortlisted first reveals */
    uint32_t rounds;      /* completed paired rounds (equal for all) */
    uint32_t cell[MS_PLAN_TEST_MAX_CANDIDATES];
    uint32_t wins[MS_PLAN_TEST_MAX_CANDIDATES];   /* in completed rounds */
    uint32_t played[MS_PLAN_TEST_MAX_CANDIDATES]; /* finished games, discarded
                                                     rounds included */
    uint32_t gain[MS_PLAN_TEST_MAX_CANDIDATES];   /* rounds won where cell[0] lost */
    uint32_t loss[MS_PLAN_TEST_MAX_CANDIDATES];   /* rounds lost where cell[0] won */
    double survival[MS_PLAN_TEST_MAX_CANDIDATES];
    uint32_t exhaustive;  /* rounds visit a complete listing */
    uint32_t order[MS_PLAN_TEST_MAX_CANDIDATES]; /* layout row of the first
                                                    started rounds */
    uint64_t round_wins[MS_PLAN_TEST_MAX_CANDIDATES]; /* bit r: won completed
                                                         round r (< 64) */
    uint32_t nested_aborts_inference; /* rounds discarded because the
                                         deadline passed inside nested
                                         inference */
    uint32_t nested_aborts_tail;      /* ... inside an exact tail search */
    uint32_t nested_inference_calls;  /* nested inferences started */
    uint32_t nested_tail_calls;       /* exact tail searches started */
} ms_plan_test_stats;

/* ms_plan, also reporting the per-candidate rollout figures. */
ms_status ms_plan_test_run(const void *obs, size_t obs_len, const ms_plan_limits *limits,
                           rt_mem *workspace, rt_clock *clock, ms_plan_result *result,
                           size_t result_len, ms_plan_test_stats *stats);

/* Plays one rollout game exactly as ms_plan's rounds do: `layout` (one 0/1
 * byte per cell, completing obs) is the simulated board, `first` the first
 * reveal, then the continuation policy. *outcome: 0 lost, 1 won, 2 cut by
 * rollout_step_limit, 3 deadline. trace receives the reveals in order. */
ms_status ms_plan_test_playout(const void *obs, size_t obs_len, const ms_plan_limits *limits,
                               rt_mem *workspace, rt_clock *clock, const uint8_t *layout,
                               uint32_t first, uint32_t *outcome, uint32_t *trace,
                               uint32_t trace_cap, uint32_t *trace_len);

/* Fault injection into the nested work of rollouts, for every later planner
 * call until reset (NULL): the at-th nested inference or exact tail search
 * of a call (1-based; 0 never) has the deadline pass at its first clock
 * reading (trip), and/or breaks (fail 1: an error status; fail 2: an MS_OK
 * inference result that is malformed). */
typedef struct ms_plan_test_fault {
    uint32_t inference_at;
    uint32_t tail_at;
    uint32_t trip;
    uint32_t fail;
} ms_plan_test_fault;

void ms_plan_test_set_fault(const ms_plan_test_fault *fault);
#endif
