#pragma once

#include "engine.h"

/* Complete hypothetical mine layouts from the public observation. Exact
 * components reuse the counting DP; hard components retain their importance
 * weights and global mine-count conditioning. Never independent cell draws.
 *
 * layouts holds capacity * cells bytes, one 0/1 mine byte per cell. The
 * caller owns every output. At most max(exact_limit, sample_count) layouts
 * are produced. If the exact complete posterior has <= exact_limit layouts,
 * each occurs once and exhaustive is 1. Otherwise these are draws (with
 * replacement); exact_distribution distinguishes the true posterior from
 * the importance-weighted empirical posterior. Exhaustive is never inferred
 * from samples, even if every sampled layout is identical.
 *
 * prob_result is the normal ms_solve result for this observation. Unavailable
 * inference or exhausted generation yields count 0 or a completed prefix of
 * draws, with info.reason; malformed input and internal errors remain errors.
 * All scratch memory is released on every return, preserving caller memory.
 *
 * Precise contract (c/probability.c implements it next to ms_solve)
 * ------------------------------------------------------------------
 * Arguments, checked like ms_solve's before any caller byte is read or
 * written: obs, limits, prob_result and info non-NULL and 8-byte aligned;
 * workspace and clock non-NULL; capacity = max(exact_limit, sample_count)
 * and layouts_len must equal capacity * width * height exactly (checked
 * multiplication; layout bytes need no alignment; layouts may be NULL only
 * when that length is 0). The ranges of obs, limits (sizeof), prob_result,
 * layouts, info (sizeof), *workspace and *clock must be non-empty (layouts:
 * unless its length is 0), must not wrap the address space and must be
 * pairwise disjoint (adjacent is fine). Then limits magic/version,
 * ms_obs_validate, prob_result_len == ms_result_size, the layouts length,
 * ms_limits_validate. A rejected call (MS_ERR_INVALID_BUFFER,
 * MS_ERR_INVALID_OBSERVATION, MS_ERR_INVALID_LIMITS) changes nothing: no
 * output byte, clock reading or workspace request.
 *
 * MS_OK writes every byte of prob_result, info and layouts: rows [0, count)
 * are complete layouts (revealed cells 0, every clue and the mine total
 * satisfied, checked for each row before it counts), rows [count, capacity)
 * are zero. MS_ERR_INCONSISTENT (as ms_solve) and MS_ERR_INTERNAL (a broken
 * invariant, never turned into layouts) leave the outputs unspecified.
 * Budget, memory or sampling shortfalls are never errors.
 *
 * Time: the inference that produces prob_result gets
 * MS_POSTERIOR_INFER_FRACTION of limits->time_budget_ms (its own phase
 * fractions apply within that share); generation then runs until entry +
 * the whole budget, checking the injected clock within draws, and keeps
 * only completed rows. memory_budget_bytes caps inference and generation
 * together. Draws use an rt_rng stream derived from limits->seed
 * (MS_LIMIT_EXPLICIT_SEED) or ms_obs_hash(obs), so outputs are reproducible
 * on every platform whenever budgets do not cut work short.
 *
 * Which rows (Z: the posterior's total layout weight):
 * - prob_result UNAVAILABLE: count 0.
 * - EXACT (every component counted): Z is the exact number of complete
 *   layouts. If Z <= exact_limit, rank order lists every layout once
 *   (exhaustive 1, count == Z). An enumeration that cannot finish by the
 *   midpoint of the remaining time (when sample_count > 0) or the deadline
 *   is discarded entirely - never reported as exhaustive and never reused
 *   as draws - and replaced by fresh draws. Otherwise sample_count draws,
 *   each uniform over all Z layouts (exact integer ranks; never per-cell).
 * - APPROXIMATE (some component sampled): sample_count draws from the
 *   importance-weighted empirical posterior, resampled into equal-weight
 *   rows: a layout combining sampled component layouts L_j has weight
 *   prod_j occurrences(L_j) * 2^choices(L_j) (one per exact component
 *   layout and pool subset), conditioned exactly on the global mine total.
 *   exhaustive and exact_distribution are 0 even if every row is identical
 *   or exact_limit exceeds the number of distinct layouts.
 *
 * info: count rows; exhaustive as above; exact_distribution 1 iff the rows
 * come from the exact uniform posterior (EXACT inference); total_layouts Z
 * when EXACT and Z < 2^64, else 0; effective_samples prob_result's
 * effective sample size when it has one (sampled components), else 0.
 * reason: MS_REASON_NONE for complete exact output,
 * MS_REASON_COUNTING_BUDGET_EXCEEDED for all sample_count approximate draws,
 * otherwise why fewer rows than intended were produced: the UNAVAILABLE
 * result's reason, MS_REASON_TIME_BUDGET_EXHAUSTED (the deadline; also when
 * an exhaustive enumeration had to be replaced by draws),
 * MS_REASON_MEMORY_BUDGET_EXHAUSTED, or MS_REASON_SAMPLING_BUDGET_EXHAUSTED
 * (draws were needed but sample_count is 0, or the bounded random-rank
 * rejection loop failed, probability below 2^-64 per draw).
 */
typedef struct ms_posterior_info {
    uint32_t count;
    uint32_t exhaustive;
    uint32_t exact_distribution;
    uint32_t reason; /* ms_prob_reason */
    uint64_t total_layouts; /* exact count when representable in uint64_t, else 0 */
    double effective_samples; /* underlying importance sample ESS, 0 if exact */
} ms_posterior_info;

_Static_assert(sizeof(ms_posterior_info) == 32, "ms_posterior_info layout");
_Static_assert(MS_OFFSETOF(ms_posterior_info, total_layouts) == 16, "ms_posterior_info layout");

/* Share of limits->time_budget_ms given to the inference stage. */
#define MS_POSTERIOR_INFER_FRACTION 0.75

ms_status ms_posterior_generate(const void *obs, size_t obs_len,
                                const ms_infer_limits *limits,
                                uint32_t exact_limit, uint32_t sample_count,
                                rt_mem *workspace, rt_clock *clock,
                                void *prob_result, size_t prob_result_len,
                                uint8_t *layouts, size_t layouts_len,
                                ms_posterior_info *info);
