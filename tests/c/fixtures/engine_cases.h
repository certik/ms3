/*
 * Engine result-validation and autosolve injection fixtures.
 *
 * GENERATED reference data - do not edit by hand. Frozen from the Python
 * reference at baseline commit 153b78ac025858f11c124e6ae17953fdecd0aec3 by a one-off
 * generator kept outside the repository (see fixture_types.h for the
 * method, oracles, conventions and schema). Provenance strings name the
 * baseline unittest each case ports (module.Class.method).
 *
 * Engine-side validation of a solver result before its proofs are accepted
 * or cached, and certainty-only autosolve batches built from injected results.
 * The base is the TWO_MINES game (mines 16, 18) after revealing (0,0) at
 * revision 1: revealed cells 0..14 (00000/00000/11211), hidden cells 15..24.
 * The well-formed base result (tests/test_server.py fake_odds) is: status
 * exact; every hidden cell ENGINE_BASE_HIDDEN_PROBABILITY (2/10); revealed
 * cells null; no proofs; meta frontier 3, components 1, unconstrained 7,
 * samples 0, elapsed 0.25 ms, reason null. Each case applies its mutations in
 * order; rejected results become a solver failure that changes nothing and is
 * not cached. Type-level cases (non-dict, bool/str values, missing keys,
 * non-string message) are JS adapter cases listed in tests/coverage-map.json.
 * Port destinations: tests/c/test_api.c / test_autosolve.c (engine service).
 */
#pragma once

#include "fixture_types.h"

#define ENGINE_BASE_REVISION 1
#define ENGINE_BASE_HIDDEN_PROBABILITY 0.2 /* 2 mines / 10 hidden cells, as fake_odds */
#define ENGINE_BASE_FRONTIER_CELLS 3u
#define ENGINE_BASE_COMPONENTS 1u
#define ENGINE_BASE_UNCONSTRAINED_CELLS 7u
#define ENGINE_BASE_SAMPLES 0u
#define ENGINE_BASE_ELAPSED_MS 0.25
static const uint32_t ENGINE_BASE_LAYOUT[2] = {16u, 18u};

static const FixtureMutation engine_placeholder_status_finished_mutations[1] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(4), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_placeholder_status_not_started_mutations[1] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(3), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_status_out_of_range_mutations[1] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(99), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_short_probability_list_mutations[1] = {
    {FIXTURE_MUT_PROBABILITY_COUNT, 0u, INT64_C(24), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_probability_above_one_mutations[1] = {
    {FIXTURE_MUT_ALL_PROBABILITIES, 0u, INT64_C(0), FIXTURE_VALUE_FINITE, 1.5},
};
static const FixtureMutation engine_probability_nan_mutations[1] = {
    {FIXTURE_MUT_ALL_PROBABILITIES, 0u, INT64_C(0), FIXTURE_VALUE_NAN, 0.0},
};
static const FixtureMutation engine_hidden_probability_negative_mutations[1] = {
    {FIXTURE_MUT_PROBABILITY, 17u, INT64_C(0), FIXTURE_VALUE_FINITE, -0.25},
};
static const FixtureMutation engine_hidden_probability_infinite_mutations[1] = {
    {FIXTURE_MUT_PROBABILITY, 17u, INT64_C(0), FIXTURE_VALUE_POS_INF, 0.0},
};
static const FixtureMutation engine_proof_index_out_of_range_mutations[1] = {
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(25), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_proof_index_negative_mutations[1] = {
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(-1), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_proof_in_both_lists_mutations[2] = {
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(20), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ADD_MINE, 0u, INT64_C(20), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_negative_meta_count_mutations[1] = {
    {FIXTURE_MUT_META_COUNT, 3u, INT64_C(-1), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_negative_elapsed_mutations[1] = {
    {FIXTURE_MUT_ELAPSED_MS, 0u, INT64_C(0), FIXTURE_VALUE_FINITE, -1.0},
};
static const FixtureMutation engine_nan_elapsed_mutations[1] = {
    {FIXTURE_MUT_ELAPSED_MS, 0u, INT64_C(0), FIXTURE_VALUE_NAN, 0.0},
};
static const FixtureMutation engine_reason_out_of_range_mutations[1] = {
    {FIXTURE_MUT_REASON, 0u, INT64_C(99), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_safe_proof_on_revealed_cell_mutations[1] = {
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(3), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_mine_proof_on_revealed_cell_mutations[1] = {
    {FIXTURE_MUT_ADD_MINE, 0u, INT64_C(4), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_safe_proof_with_nonzero_odds_mutations[1] = {
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(17), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_mine_proof_with_odds_below_one_mutations[1] = {
    {FIXTURE_MUT_ADD_MINE, 0u, INT64_C(16), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_proof_without_odds_mutations[3] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(2), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ALL_PROBABILITIES, 0u, INT64_C(0), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(17), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_proofs_normalized_mutations[7] = {
    {FIXTURE_MUT_PROBABILITY, 16u, INT64_C(0), FIXTURE_VALUE_FINITE, 1.0},
    {FIXTURE_MUT_PROBABILITY, 17u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
    {FIXTURE_MUT_PROBABILITY, 20u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(20), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(20), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(17), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ADD_MINE, 0u, INT64_C(16), FIXTURE_VALUE_NULL, 0.0},
};
static const uint32_t engine_proofs_normalized_safe[2] = {
    17u, 20u,
};
static const uint32_t engine_proofs_normalized_mines[1] = {
    16u,
};
static const FixtureMutation engine_exact_missing_hidden_probability_mutations[1] = {
    {FIXTURE_MUT_PROBABILITY, 24u, INT64_C(0), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_approximate_missing_hidden_probability_mutations[2] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(1), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_PROBABILITY, 24u, INT64_C(0), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_unavailable_partial_with_proof_mutations[4] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(2), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ALL_PROBABILITIES, 0u, INT64_C(0), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_PROBABILITY, 17u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(17), FIXTURE_VALUE_NULL, 0.0},
};
static const uint32_t engine_unavailable_partial_with_proof_safe[1] = {
    17u,
};
static const FixtureMutation engine_approximate_complete_mutations[1] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(1), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_revealed_cell_with_odds_exact_mutations[1] = {
    {FIXTURE_MUT_PROBABILITY, 7u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
};
static const FixtureMutation engine_revealed_cell_with_odds_approximate_mutations[2] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(1), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_PROBABILITY, 7u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
};
static const FixtureMutation engine_revealed_cell_with_odds_unavailable_mutations[2] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(2), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_PROBABILITY, 7u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
};
static const FixtureMutation engine_diagnostics_forwarded_mutations[4] = {
    {FIXTURE_MUT_DIAGNOSTIC, 0u, INT64_C(1), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_DIAGNOSTIC, 1u, INT64_C(2), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_DIAGNOSTIC, 2u, INT64_C(340), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_DIAGNOSTIC, 3u, INT64_C(0), FIXTURE_VALUE_FINITE, 57.25},
};
static const FixtureMutation engine_diagnostic_ess_null_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 3u, INT64_C(0), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_diagnostic_ess_zero_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 3u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
};
static const FixtureMutation engine_diagnostic_attempts_zero_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 2u, INT64_C(0), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_diagnostic_ess_minus_one_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 3u, INT64_C(0), FIXTURE_VALUE_FINITE, -1.0},
};
static const FixtureMutation engine_diagnostic_ess_minus_half_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 3u, INT64_C(0), FIXTURE_VALUE_FINITE, -0.5},
};
static const FixtureMutation engine_diagnostic_ess_nan_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 3u, INT64_C(0), FIXTURE_VALUE_NAN, 0.0},
};
static const FixtureMutation engine_diagnostic_ess_inf_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 3u, INT64_C(0), FIXTURE_VALUE_POS_INF, 0.0},
};
static const FixtureMutation engine_diagnostic_exact_components_negative_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 0u, INT64_C(-1), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_diagnostic_sampled_components_negative_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 1u, INT64_C(-1), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_diagnostic_sample_attempts_negative_mutations[1] = {
    {FIXTURE_MUT_DIAGNOSTIC, 2u, INT64_C(-1), FIXTURE_VALUE_NULL, 0.0},
};
static const FixtureMutation engine_autosolve_proofs_exact_mutations[4] = {
    {FIXTURE_MUT_PROBABILITY, 15u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
    {FIXTURE_MUT_PROBABILITY, 16u, INT64_C(0), FIXTURE_VALUE_FINITE, 1.0},
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(15), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ADD_MINE, 0u, INT64_C(16), FIXTURE_VALUE_NULL, 0.0},
};
static const uint32_t engine_autosolve_proofs_exact_safe[1] = {
    15u,
};
static const uint32_t engine_autosolve_proofs_exact_mines[1] = {
    16u,
};
static const FixtureMutation engine_autosolve_proofs_approximate_mutations[5] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(1), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_PROBABILITY, 15u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
    {FIXTURE_MUT_PROBABILITY, 16u, INT64_C(0), FIXTURE_VALUE_FINITE, 1.0},
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(15), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ADD_MINE, 0u, INT64_C(16), FIXTURE_VALUE_NULL, 0.0},
};
static const uint32_t engine_autosolve_proofs_approximate_safe[1] = {
    15u,
};
static const uint32_t engine_autosolve_proofs_approximate_mines[1] = {
    16u,
};
static const FixtureMutation engine_autosolve_proofs_unavailable_mutations[6] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(2), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ALL_PROBABILITIES, 0u, INT64_C(0), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_PROBABILITY, 15u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
    {FIXTURE_MUT_PROBABILITY, 16u, INT64_C(0), FIXTURE_VALUE_FINITE, 1.0},
    {FIXTURE_MUT_ADD_SAFE, 0u, INT64_C(15), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_ADD_MINE, 0u, INT64_C(16), FIXTURE_VALUE_NULL, 0.0},
};
static const uint32_t engine_autosolve_proofs_unavailable_safe[1] = {
    15u,
};
static const uint32_t engine_autosolve_proofs_unavailable_mines[1] = {
    16u,
};
static const FixtureMutation engine_autosolve_sampled_endpoints_mutations[3] = {
    {FIXTURE_MUT_STATUS, 0u, INT64_C(1), FIXTURE_VALUE_NULL, 0.0},
    {FIXTURE_MUT_PROBABILITY, 16u, INT64_C(0), FIXTURE_VALUE_FINITE, 0.0},
    {FIXTURE_MUT_PROBABILITY, 15u, INT64_C(0), FIXTURE_VALUE_FINITE, 1.0},
};

static const EngineResultCase ENGINE_RESULT_CASES[] = {
    {"base_exact", "tests.test_server.ProbabilityTests.test_playing_game_uses_only_public_observation",
        NULL, 0u, 1u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(1),
        "00000" "00000" "11211" "#####" "#####"},
    {"placeholder_status_finished", "tests.test_server.ProbabilityTests.test_malformed_solver_output_is_rejected (placeholder status)",
        engine_placeholder_status_finished_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"placeholder_status_not_started", "C addition: solver results never carry placeholder statuses",
        engine_placeholder_status_not_started_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"status_out_of_range", "C addition: status outside the enumeration",
        engine_status_out_of_range_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"short_probability_list", "tests.test_server.ProbabilityTests.test_malformed_solver_output_is_rejected (short list)",
        engine_short_probability_list_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"probability_above_one", "tests.test_server.ProbabilityTests.test_malformed_solver_output_is_rejected (above one)",
        engine_probability_above_one_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"probability_nan", "tests.test_server.ProbabilityTests.test_malformed_solver_output_is_rejected (nan)",
        engine_probability_nan_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"hidden_probability_negative", "C addition: probability below zero",
        engine_hidden_probability_negative_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"hidden_probability_infinite", "C addition: infinite probability",
        engine_hidden_probability_infinite_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"proof_index_out_of_range", "tests.test_server.ProbabilityTests.test_malformed_solver_output_is_rejected (index out of range)",
        engine_proof_index_out_of_range_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"proof_index_negative", "C addition: negative proof index",
        engine_proof_index_negative_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"proof_in_both_lists", "tests.test_server.ProbabilityTests.test_malformed_solver_output_is_rejected (both lists)",
        engine_proof_in_both_lists_mutations, 2u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"negative_meta_count", "tests.test_server.ProbabilityTests.test_malformed_solver_output_is_rejected (negative count)",
        engine_negative_meta_count_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"negative_elapsed", "C addition: meta.elapsed_ms must be a non-negative number",
        engine_negative_elapsed_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"nan_elapsed", "C addition: meta.elapsed_ms must be finite",
        engine_nan_elapsed_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"reason_out_of_range", "tests.test_server.ProbabilityTests.test_malformed_solver_output_is_rejected (reason)",
        engine_reason_out_of_range_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"safe_proof_on_revealed_cell", "tests.test_server.ProbabilityTests.test_proofs_must_match_odds_and_skip_revealed_cells",
        engine_safe_proof_on_revealed_cell_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"mine_proof_on_revealed_cell", "tests.test_server.ProbabilityTests.test_proofs_must_match_odds_and_skip_revealed_cells",
        engine_mine_proof_on_revealed_cell_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"safe_proof_with_nonzero_odds", "tests.test_server.ProbabilityTests.test_proofs_must_match_odds_and_skip_revealed_cells",
        engine_safe_proof_with_nonzero_odds_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"mine_proof_with_odds_below_one", "tests.test_server.ProbabilityTests.test_proofs_must_match_odds_and_skip_revealed_cells",
        engine_mine_proof_with_odds_below_one_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"proof_without_odds", "tests.test_server.ProbabilityTests.test_proofs_must_match_odds_and_skip_revealed_cells",
        engine_proof_without_odds_mutations, 3u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    /* Duplicate proofs are merged and sorted (baseline); a port may instead reject
     * duplicates, but must never apply a cell twice. */
    {"proofs_normalized", "tests.test_server.ProbabilityTests.test_proofs_must_match_odds_and_skip_revealed_cells",
        engine_proofs_normalized_mutations, 7u, 1u,
        engine_proofs_normalized_safe, 2u, engine_proofs_normalized_mines, 1u, 1u, INT64_C(2),
        "00000" "00000" "11211" "#F2##" "1####"},
    {"exact_missing_hidden_probability", "tests.test_server.ProbabilityTests.test_only_unavailable_results_may_leave_hidden_cells_unknown",
        engine_exact_missing_hidden_probability_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"approximate_missing_hidden_probability", "tests.test_server.ProbabilityTests.test_only_unavailable_results_may_leave_hidden_cells_unknown",
        engine_approximate_missing_hidden_probability_mutations, 2u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"unavailable_partial_with_proof", "tests.test_server.ProbabilityTests.test_only_unavailable_results_may_leave_hidden_cells_unknown",
        engine_unavailable_partial_with_proof_mutations, 4u, 1u,
        engine_unavailable_partial_with_proof_safe, 1u, NULL, 0u, 1u, INT64_C(2),
        "00000" "00000" "11211" "##2##" "#####"},
    {"approximate_complete", "tests.test_server.ProbabilityTests.test_only_unavailable_results_may_leave_hidden_cells_unknown",
        engine_approximate_complete_mutations, 1u, 1u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(1),
        "00000" "00000" "11211" "#####" "#####"},
    {"revealed_cell_with_odds_exact", "tests.test_server.ProbabilityTests.test_revealed_cells_must_have_null_odds",
        engine_revealed_cell_with_odds_exact_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"revealed_cell_with_odds_approximate", "tests.test_server.ProbabilityTests.test_revealed_cells_must_have_null_odds",
        engine_revealed_cell_with_odds_approximate_mutations, 2u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"revealed_cell_with_odds_unavailable", "tests.test_server.ProbabilityTests.test_revealed_cells_must_have_null_odds",
        engine_revealed_cell_with_odds_unavailable_mutations, 2u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"diagnostics_forwarded", "tests.test_server.ProbabilityTests.test_optional_solver_diagnostics_are_forwarded",
        engine_diagnostics_forwarded_mutations, 4u, 1u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(1),
        "00000" "00000" "11211" "#####" "#####"},
    {"diagnostic_ess_null", "tests.test_server.ProbabilityTests.test_optional_solver_diagnostics_are_forwarded",
        engine_diagnostic_ess_null_mutations, 1u, 1u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(1),
        "00000" "00000" "11211" "#####" "#####"},
    {"diagnostic_ess_zero", "tests.test_server.ProbabilityTests.test_optional_solver_diagnostics_are_forwarded",
        engine_diagnostic_ess_zero_mutations, 1u, 1u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(1),
        "00000" "00000" "11211" "#####" "#####"},
    {"diagnostic_attempts_zero", "tests.test_server.ProbabilityTests.test_optional_solver_diagnostics_are_forwarded",
        engine_diagnostic_attempts_zero_mutations, 1u, 1u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(1),
        "00000" "00000" "11211" "#####" "#####"},
    {"diagnostic_ess_minus_one", "tests.test_server.ProbabilityTests.test_invalid_optional_diagnostics_are_rejected",
        engine_diagnostic_ess_minus_one_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"diagnostic_ess_minus_half", "tests.test_server.ProbabilityTests.test_invalid_optional_diagnostics_are_rejected",
        engine_diagnostic_ess_minus_half_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"diagnostic_ess_nan", "tests.test_server.ProbabilityTests.test_invalid_optional_diagnostics_are_rejected",
        engine_diagnostic_ess_nan_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"diagnostic_ess_inf", "tests.test_server.ProbabilityTests.test_invalid_optional_diagnostics_are_rejected",
        engine_diagnostic_ess_inf_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"diagnostic_exact_components_negative", "tests.test_server.ProbabilityTests.test_invalid_optional_diagnostics_are_rejected",
        engine_diagnostic_exact_components_negative_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"diagnostic_sampled_components_negative", "tests.test_server.ProbabilityTests.test_invalid_optional_diagnostics_are_rejected",
        engine_diagnostic_sampled_components_negative_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    {"diagnostic_sample_attempts_negative", "tests.test_server.ProbabilityTests.test_invalid_optional_diagnostics_are_rejected",
        engine_diagnostic_sample_attempts_negative_mutations, 1u, 0u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(-1),
        NULL},
    /* Only the listed proofs are played: 15 revealed, 16 flagged, nothing else. */
    {"autosolve_proofs_exact", "tests.test_autosolve.AutosolveApiTests.test_applies_only_proven_moves_for_every_solver_status",
        engine_autosolve_proofs_exact_mutations, 4u, 1u,
        engine_autosolve_proofs_exact_safe, 1u, engine_autosolve_proofs_exact_mines, 1u, 1u, INT64_C(2),
        "00000" "00000" "11211" "1F###" "#####"},
    /* Only the listed proofs are played: 15 revealed, 16 flagged, nothing else. */
    {"autosolve_proofs_approximate", "tests.test_autosolve.AutosolveApiTests.test_applies_only_proven_moves_for_every_solver_status",
        engine_autosolve_proofs_approximate_mutations, 5u, 1u,
        engine_autosolve_proofs_approximate_safe, 1u, engine_autosolve_proofs_approximate_mines, 1u, 1u, INT64_C(2),
        "00000" "00000" "11211" "1F###" "#####"},
    /* Only the listed proofs are played: 15 revealed, 16 flagged, nothing else. */
    {"autosolve_proofs_unavailable", "tests.test_autosolve.AutosolveApiTests.test_applies_only_proven_moves_for_every_solver_status",
        engine_autosolve_proofs_unavailable_mutations, 6u, 1u,
        engine_autosolve_proofs_unavailable_safe, 1u, engine_autosolve_proofs_unavailable_mines, 1u, 1u, INT64_C(2),
        "00000" "00000" "11211" "1F###" "#####"},
    /* Sampled 0 on a real mine and 1 on a safe cell, no proofs: the batch is a
     * no-op (revision stays 1). */
    {"autosolve_sampled_endpoints", "tests.test_autosolve.AutosolveApiTests.test_sampled_endpoints_are_not_automatic_moves",
        engine_autosolve_sampled_endpoints_mutations, 3u, 1u,
        NULL, 0u, NULL, 0u, 0u, INT64_C(1),
        "00000" "00000" "11211" "#####" "#####"}
};
#define ENGINE_RESULT_CASE_COUNT (sizeof(ENGINE_RESULT_CASES) / sizeof(ENGINE_RESULT_CASES[0]))
