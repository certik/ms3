/*
 * Sampling fixtures (importance sampling, budgets, unavailable).
 *
 * GENERATED reference data - do not edit by hand. Frozen from the Python
 * reference at baseline commit 153b78ac025858f11c124e6ae17953fdecd0aec3 by a one-off
 * generator kept outside the repository (see fixture_types.h for the
 * method, oracles, conventions and schema). Provenance strings name the
 * baseline unittest each case ports (module.Class.method).
 *
 * Budget-bound solves of recorded observations. Expected values are ground
 * truth (exact odds of the referenced ProbabilityCase), never a Python random
 * stream: statuses/reasons marked rng_independent hold for any generator; the
 * others list every acceptable outcome. The evidence fields summarise the
 * baseline sampler over independent seeds and are informational.
 * Port destination: tests/c/test_probability.c (sampling section).
 */
#pragma once

#include "fixture_types.h"

#include "probability_cases.h"
#include "large_boards.h"

static const FixtureRun sampling_unavailable_keeps_logical_proofs_mines[1] = {
    {12u, 1u, 1u},
};
static const SamplingCase SAMPLING_CASES[] = {
    /* random_seed_0_sampled:
     * Baseline (rng seed 0): approximate, reason counting_budget_exceeded, ESS 2000.000. */
    {
        .name = "random_seed_0_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(0))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_0],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 2000.0,
        .evidence_max_sigma = 3.354,
    },
    /* random_seed_5_sampled:
     * Propagation fixes every hidden cell (0 components): the sampler never
     * runs and the result is exact even with node_budget 0.
     * Baseline (rng seed 5): exact. */
    {
        .name = "random_seed_5_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(5))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_5],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .baseline_reason = FIXTURE_REASON_NONE,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_NONE),
        .rng_independent = 1u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 0.0,
        .evidence_max_sigma = 0.0,
    },
    /* random_seed_11_sampled:
     * Propagation fixes every hidden cell (0 components): the sampler never
     * runs and the result is exact even with node_budget 0.
     * Baseline determinism tests (test_injected_rng_makes_sampling_deterministic,
     * test_default_rng_is_reproducible) used this observation, so they never
     * exercised the sampler; port them with random_seed_134_sampled instead.
     * Baseline (rng seed 11): exact. */
    {
        .name = "random_seed_11_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(11))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_11],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .baseline_reason = FIXTURE_REASON_NONE,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_NONE),
        .rng_independent = 1u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 0.0,
        .evidence_max_sigma = 0.0,
    },
    /* random_seed_21_sampled:
     * Baseline (rng seed 21): approximate, reason counting_budget_exceeded, ESS 2328.013. */
    {
        .name = "random_seed_21_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(21))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_21],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 2290.667,
        .evidence_max_sigma = 2.818,
    },
    /* random_seed_39_sampled:
     * Baseline (rng seed 39): approximate, reason counting_budget_exceeded, ESS 1507.333. */
    {
        .name = "random_seed_39_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(39))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_39],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 1444.432,
        .evidence_max_sigma = 2.792,
    },
    /* random_seed_54_sampled:
     * Baseline (rng seed 54): approximate, reason counting_budget_exceeded, ESS 897.293. */
    {
        .name = "random_seed_54_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(54))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_54],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 864.043,
        .evidence_max_sigma = 2.727,
    },
    /* random_seed_74_sampled:
     * Baseline (rng seed 74): approximate, reason counting_budget_exceeded, ESS 2197.818. */
    {
        .name = "random_seed_74_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(74))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_74],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 2149.824,
        .evidence_max_sigma = 2.491,
    },
    /* random_seed_82_sampled:
     * Baseline (rng seed 82): approximate, reason counting_budget_exceeded, ESS 2391.969. */
    {
        .name = "random_seed_82_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(82))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_82],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 2359.803,
        .evidence_max_sigma = 3.205,
    },
    /* random_seed_93_sampled:
     * Baseline (rng seed 93): approximate, reason counting_budget_exceeded, ESS 2701.045. */
    {
        .name = "random_seed_93_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(93))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_93],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 2623.171,
        .evidence_max_sigma = 2.115,
    },
    /* random_seed_126_sampled:
     * Baseline (rng seed 126): approximate, reason counting_budget_exceeded, ESS 2493.451.
     * Unequal proposal weights: every layout holds 3 mines (truth 3/8 per cell),
     * but the sampler's proposal mines cell 0 (first in its order) with
     * probability 1/2; only the 2^choices weights recover 3/8. */
    {
        .name = "random_seed_126_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(126))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_126],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 2429.942,
        .evidence_max_sigma = 3.262,
    },
    /* random_seed_128_sampled:
     * Baseline (rng seed 128): approximate, reason counting_budget_exceeded, ESS 3235.507. */
    {
        .name = "random_seed_128_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(128))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_128],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 3203.373,
        .evidence_max_sigma = 2.793,
    },
    /* random_seed_131_sampled:
     * Baseline (rng seed 131): approximate, reason counting_budget_exceeded, ESS 1800.241. */
    {
        .name = "random_seed_131_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(131))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_131],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 1794.223,
        .evidence_max_sigma = 3.262,
    },
    /* random_seed_134_sampled:
     * Use for C determinism: the same seed twice (and the default
     * observation-derived seed twice) must give identical results.
     * Baseline (rng seed 134): approximate, reason counting_budget_exceeded, ESS 2000.000. */
    {
        .name = "random_seed_134_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampling_estimates_agree_with_brute_force (method on frozen random_observation(134))",
        .observation = &PROBABILITY_CASES[PROB_CASE_RANDOM_SEED_134],
        .node_budget = 0u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 2000.0,
        .evidence_max_sigma = 3.354,
    },
    /* mixed_exact_and_sampled:
     * node_budget 2 counts the one-node component exactly and samples the
     * chain (node accounting as in the baseline: one node per state/group
     * count expansion).
     * Baseline (rng seed 5): approximate, reason counting_budget_exceeded, ESS 4000.000. */
    {
        .name = "mixed_exact_and_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_low_node_budget_mixes_exact_and_sampled_components",
        .observation = &PROBABILITY_CASES[PROB_CASE_MIXED_COMPONENTS_18X1],
        .node_budget = 2u, .sample_budget = 4000u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 1u, .min_sampled_components = 1u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 4000.0,
        .evidence_max_sigma = 3.067,
    },
    /* no_sampling_budget:
     * No counting and no sampling budget: unavailable, 0 samples, every
     * hidden probability null.
     * Baseline (rng seed None): unavailable, reason sampling_budget_exhausted. */
    {
        .name = "no_sampling_budget",
        .provenance = "tests.test_probability.SamplingTests.test_no_sampling_budget_is_unavailable",
        .observation = &PROBABILITY_CASES[PROB_CASE_CHAIN_BOARD_2_TOTAL_4],
        .node_budget = 0u, .sample_budget = 0u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_UNAVAILABLE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_UNAVAILABLE),
        .baseline_reason = FIXTURE_REASON_SAMPLING_BUDGET_EXHAUSTED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_SAMPLING_BUDGET_EXHAUSTED),
        .rng_independent = 1u,
        .proofs_rule = FIXTURE_PROOFS_EXACT_MATCH,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 0.0,
        .evidence_max_sigma = 0.0,
    },
    /* unavailable_keeps_logical_proofs:
     * Unavailable, yet the logically forced mine 12 is still proven (p = 1).
     * Baseline (rng seed None): unavailable, reason sampling_budget_exhausted. */
    {
        .name = "unavailable_keeps_logical_proofs",
        .provenance = "tests.test_probability.SamplingTests.test_unavailable_still_reports_logical_proofs",
        .observation = &PROBABILITY_CASES[PROB_CASE_CHAIN_BOARD_2_FORCED_TAIL],
        .node_budget = 0u, .sample_budget = 0u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_UNAVAILABLE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_UNAVAILABLE),
        .baseline_reason = FIXTURE_REASON_SAMPLING_BUDGET_EXHAUSTED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_SAMPLING_BUDGET_EXHAUSTED),
        .rng_independent = 1u,
        .proofs_rule = FIXTURE_PROOFS_EXACT_MATCH,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = sampling_unavailable_keeps_logical_proofs_mines, .proven_mine_runs = 1u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 0.0,
        .evidence_max_sigma = 0.0,
    },
    /* too_few_effective_samples:
     * Every proposal makes exactly one fair choice and both solutions have
     * global weight 1, so all 5 weights are equal and ESS is exactly 5.
     * Baseline (rng seed 1): unavailable, reason insufficient_effective_samples, ESS 5.000. */
    {
        .name = "too_few_effective_samples",
        .provenance = "tests.test_probability.SamplingTests.test_too_few_effective_samples_is_unavailable",
        .observation = &PROBABILITY_CASES[PROB_CASE_CHAIN_BOARD_1_TOTAL_2],
        .node_budget = 0u, .sample_budget = 5u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_UNAVAILABLE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_UNAVAILABLE),
        .baseline_reason = FIXTURE_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES),
        .rng_independent = 1u,
        .proofs_rule = FIXTURE_PROOFS_EXACT_MATCH,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = 5.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 0.0,
        .evidence_max_sigma = 0.0,
    },
    /* globally_incompatible:
     * One proposal per component; only the all-two-mine combination fits the
     * total, which one draw each finds with probability 2^-12. Status is
     * always unavailable; the reason is no_globally_compatible_samples except
     * for a 1/4096 stream that then fails on ESS (1 < 50). Choose a C seed
     * that exercises the incompatible path.
     * Baseline (rng seed 2): unavailable, reason no_globally_compatible_samples. */
    {
        .name = "globally_incompatible",
        .provenance = "tests.test_probability.SamplingTests.test_globally_incompatible_samples_are_unavailable",
        .observation = &PROBABILITY_CASES[PROB_CASE_CHAIN_BOARD_12_TOTAL_36],
        .node_budget = 0u, .sample_budget = 12u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_UNAVAILABLE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_UNAVAILABLE),
        .baseline_reason = FIXTURE_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES) | FIXTURE_REASON_BIT(FIXTURE_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_EMPTY,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 0.0,
        .evidence_max_sigma = 0.0,
    },
    /* chain_3001_sampled:
     * Sampling only: both chain solutions are proposed, the 750-mine one
     * gets global weight 0, so every estimate is exactly 0.0 or 1.0 (i%4)
     * yet nothing may be listed as proven. ESS < 50 needs fewer than 50 of
     * 300 fair draws on one side (probability ~1e-34).
     * Baseline (rng seed 3): approximate, reason counting_budget_exceeded, ESS 159.000. */
    {
        .name = "chain_3001_sampled",
        .provenance = "tests.test_probability.SamplingTests.test_sampled_certainties_are_not_reported_as_proofs",
        .observation = &PROBABILITY_CASES[PROB_CASE_CHAIN_3001_EXACT],
        .node_budget = 0u, .sample_budget = 300u, .time_budget_ms = 0u,
        .baseline_status = FIXTURE_STATUS_APPROXIMATE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE),
        .baseline_reason = FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_EMPTY,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 5.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 50u,
        .evidence_min_ess = 129.0,
        .evidence_max_sigma = 0.0,
    },
    /* lattice_300ms:
     * Default node/sample budgets, 300 ms: must return within the
     * budget plus 0.5 s with frontier_cells > 1000 and truthful proofs.
     * Baseline (rng seed None): unavailable, reason no_consistent_samples. */
    {
        .name = "lattice_300ms",
        .provenance = "tests.test_probability.SamplingTests.test_large_connected_component_is_time_bounded",
        .observation = &LARGE_BOARD_CASES[LARGE_CASE_LATTICE_80X80_SEED4],
        .node_budget = 100000u, .sample_budget = 2000u, .time_budget_ms = 300u,
        .baseline_status = FIXTURE_STATUS_UNAVAILABLE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE) | FIXTURE_STATUS_BIT(FIXTURE_STATUS_UNAVAILABLE),
        .baseline_reason = FIXTURE_REASON_NO_CONSISTENT_SAMPLES,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED) | FIXTURE_REASON_BIT(FIXTURE_REASON_NO_CONSISTENT_SAMPLES) | FIXTURE_REASON_BIT(FIXTURE_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES) | FIXTURE_REASON_BIT(FIXTURE_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES) | FIXTURE_REASON_BIT(FIXTURE_REASON_TIME_BUDGET_EXHAUSTED) | FIXTURE_REASON_BIT(FIXTURE_REASON_SAMPLING_BUDGET_EXHAUSTED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 0.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 5u,
        .evidence_min_ess = 0.0,
        .evidence_max_sigma = 0.0,
    },
    /* lattice_1000ms:
     * Default node/sample budgets, 1000 ms: must return within the
     * budget plus 0.5 s with frontier_cells > 1000 and truthful proofs.
     * Baseline (rng seed None): unavailable, reason no_consistent_samples. */
    {
        .name = "lattice_1000ms",
        .provenance = "tests.test_probability.SamplingTests.test_large_connected_component_is_time_bounded",
        .observation = &LARGE_BOARD_CASES[LARGE_CASE_LATTICE_80X80_SEED4],
        .node_budget = 100000u, .sample_budget = 2000u, .time_budget_ms = 1000u,
        .baseline_status = FIXTURE_STATUS_UNAVAILABLE,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_APPROXIMATE) | FIXTURE_STATUS_BIT(FIXTURE_STATUS_UNAVAILABLE),
        .baseline_reason = FIXTURE_REASON_NO_CONSISTENT_SAMPLES,
        .accepted_reasons = FIXTURE_REASON_BIT(FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED) | FIXTURE_REASON_BIT(FIXTURE_REASON_NO_CONSISTENT_SAMPLES) | FIXTURE_REASON_BIT(FIXTURE_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES) | FIXTURE_REASON_BIT(FIXTURE_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES) | FIXTURE_REASON_BIT(FIXTURE_REASON_TIME_BUDGET_EXHAUSTED) | FIXTURE_REASON_BIT(FIXTURE_REASON_SAMPLING_BUDGET_EXHAUSTED),
        .rng_independent = 0u,
        .proofs_rule = FIXTURE_PROOFS_TRUTHFUL,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .min_effective_samples = 50.0,
        .exact_effective_samples = -1.0,
        .tolerance_sigmas = 0.0, .mine_sum_tolerance = 1e-07,
        .min_exact_components = 0u, .min_sampled_components = 0u,
        .evidence_seeds = 5u,
        .evidence_min_ess = 0.0,
        .evidence_max_sigma = 0.0,
    }
};
#define SAMPLING_CASE_COUNT (sizeof(SAMPLING_CASES) / sizeof(SAMPLING_CASES[0]))
#define SAMPLING_CASE_RANDOM_SEED_0_SAMPLED 0u
#define SAMPLING_CASE_RANDOM_SEED_5_SAMPLED 1u
#define SAMPLING_CASE_RANDOM_SEED_11_SAMPLED 2u
#define SAMPLING_CASE_RANDOM_SEED_21_SAMPLED 3u
#define SAMPLING_CASE_RANDOM_SEED_39_SAMPLED 4u
#define SAMPLING_CASE_RANDOM_SEED_54_SAMPLED 5u
#define SAMPLING_CASE_RANDOM_SEED_74_SAMPLED 6u
#define SAMPLING_CASE_RANDOM_SEED_82_SAMPLED 7u
#define SAMPLING_CASE_RANDOM_SEED_93_SAMPLED 8u
#define SAMPLING_CASE_RANDOM_SEED_126_SAMPLED 9u
#define SAMPLING_CASE_RANDOM_SEED_128_SAMPLED 10u
#define SAMPLING_CASE_RANDOM_SEED_131_SAMPLED 11u
#define SAMPLING_CASE_RANDOM_SEED_134_SAMPLED 12u
#define SAMPLING_CASE_MIXED_EXACT_AND_SAMPLED 13u
#define SAMPLING_CASE_NO_SAMPLING_BUDGET 14u
#define SAMPLING_CASE_UNAVAILABLE_KEEPS_LOGICAL_PROOFS 15u
#define SAMPLING_CASE_TOO_FEW_EFFECTIVE_SAMPLES 16u
#define SAMPLING_CASE_GLOBALLY_INCOMPATIBLE 17u
#define SAMPLING_CASE_CHAIN_3001_SAMPLED 18u
#define SAMPLING_CASE_LATTICE_300MS 19u
#define SAMPLING_CASE_LATTICE_1000MS 20u
