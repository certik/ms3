/*
 * Autosolve fixtures (multi-round batches, pause, resume).
 *
 * GENERATED reference data - do not edit by hand. Frozen from the Python
 * reference at baseline commit 153b78ac025858f11c124e6ae17953fdecd0aec3 by a one-off
 * generator kept outside the repository (see fixture_types.h for the
 * method, oracles, conventions and schema). Provenance strings name the
 * baseline unittest each case ports (module.Class.method).
 *
 * Certainty-only autosolve trajectories on frozen layouts. Each BATCH solves
 * the current revision with default budgets (exact at baseline), applies every
 * proven safe/mine cell as one atomic deduction batch (one revision), and
 * records the state; a batch that changes nothing is a pause, where every
 * unflagged hidden cell has exact odds strictly between 0 and 1 (pause_odds).
 * MANUAL_REVEAL is the player's surviving guess (lowest-index hidden safe
 * cell, chosen with the private layout for the regression only); autosolve
 * resumes after it. Every proof set was re-derived by the independent frontier
 * oracle, so these trajectories are implementation independent. The extra
 * 9x9 layout is random.Random(185).
 * Port destinations: tests/c/test_autosolve.c and the JS autosolve scheduling
 * tests (pause/resume, first-reveal wait).
 */
#pragma once

#include "fixture_types.h"

#include "large_boards.h"

/* beginner_seed11_pause0: 9x9, 10 mine(s).
 * Observation at pause 0 of autosolve script beginner_seed11; every
 * unflagged hidden cell has 0 < p < 1, so autosolve must stop.
 * Observation ('?' hidden, digits revealed clues):
 *   000001?10
 *   000013320
 *   00001??10
 *   000012210
 *   000000000
 *   000000000
 *   001122210
 *   013?4??21
 *   01????3?? */
static const FixtureClueRun beginner_seed11_pause0_clues[27] = {
    {0u, 1u, 5u, 0u}, {5u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {8u, 1u, 5u, 0u}, {13u, 1u, 1u, 1u},
    {14u, 1u, 2u, 3u}, {16u, 1u, 1u, 2u}, {17u, 1u, 5u, 0u}, {22u, 1u, 1u, 1u}, {25u, 1u, 1u, 1u},
    {26u, 1u, 5u, 0u}, {31u, 1u, 1u, 1u}, {32u, 1u, 2u, 2u}, {34u, 1u, 1u, 1u}, {35u, 1u, 21u, 0u},
    {56u, 1u, 2u, 1u}, {58u, 1u, 3u, 2u}, {61u, 1u, 1u, 1u}, {62u, 1u, 2u, 0u}, {64u, 1u, 1u, 1u},
    {65u, 1u, 1u, 3u}, {67u, 1u, 1u, 4u}, {70u, 1u, 1u, 2u}, {71u, 1u, 1u, 1u}, {72u, 1u, 1u, 0u},
    {73u, 1u, 1u, 1u}, {78u, 1u, 1u, 3u},
};
static const FixtureRun beginner_seed11_pause0_class0[2] = {
    {76u, 1u, 2u}, {79u, 1u, 2u},
};
static const FixtureRun beginner_seed11_pause0_class1[5] = {
    {6u, 1u, 1u}, {23u, 1u, 2u}, {66u, 1u, 1u}, {68u, 1u, 2u}, {74u, 1u, 2u},
};
static const FixtureOddsClass beginner_seed11_pause0_classes[2] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, beginner_seed11_pause0_class0, 2u}, /* 4 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, beginner_seed11_pause0_class1, 5u}, /* 8 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t beginner_seed11_pause0_layouts_limbs[1] = {
    0x00000002u,
};

/* beginner_seed1_pause0: 9x9, 10 mine(s).
 * Observation at pause 0 of autosolve script beginner_seed1; every
 * unflagged hidden cell has 0 < p < 1, so autosolve must stop.
 * Observation ('?' hidden, digits revealed clues):
 *   000002???
 *   000002?5?
 *   00000114?
 *   00000002?
 *   000000011
 *   001110000
 *   002?21110
 *   112?21?10
 *   ?11111110 */
static const FixtureClueRun beginner_seed1_pause0_clues[25] = {
    {0u, 1u, 5u, 0u}, {5u, 1u, 1u, 2u}, {9u, 1u, 5u, 0u}, {14u, 1u, 1u, 2u}, {16u, 1u, 1u, 5u},
    {18u, 1u, 5u, 0u}, {23u, 1u, 2u, 1u}, {25u, 1u, 1u, 4u}, {27u, 1u, 7u, 0u}, {34u, 1u, 1u, 2u},
    {36u, 1u, 7u, 0u}, {43u, 1u, 2u, 1u}, {45u, 1u, 2u, 0u}, {47u, 1u, 3u, 1u}, {50u, 1u, 6u, 0u},
    {56u, 1u, 1u, 2u}, {58u, 1u, 1u, 2u}, {59u, 1u, 3u, 1u}, {62u, 9u, 3u, 0u}, {63u, 1u, 2u, 1u},
    {65u, 1u, 1u, 2u}, {67u, 1u, 1u, 2u}, {68u, 1u, 1u, 1u}, {70u, 1u, 1u, 1u}, {73u, 1u, 7u, 1u},
};
static const FixtureRun beginner_seed1_pause0_class0[1] = {
    {7u, 1u, 2u},
};
static const FixtureRun beginner_seed1_pause0_class1[5] = {
    {6u, 1u, 1u}, {15u, 1u, 1u}, {17u, 9u, 3u}, {57u, 1u, 1u}, {66u, 3u, 3u},
};
static const FixtureOddsClass beginner_seed1_pause0_classes[2] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, beginner_seed1_pause0_class0, 1u}, /* 2 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, beginner_seed1_pause0_class1, 5u}, /* 9 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t beginner_seed1_pause0_layouts_limbs[1] = {
    0x00000002u,
};

/* beginner_seed185_pause0: 9x9, 10 mine(s).
 * Observation at pause 0 of autosolve script beginner_seed185; every
 * unflagged hidden cell has 0 < p < 1, so autosolve must stop.
 * Observation ('?' hidden, digits revealed clues):
 *   ?1001????
 *   ?2112????
 *   ??1??2???
 *   1111111??
 *   0000001??
 *   0000012??
 *   000002???
 *   000002???
 *   0000012?? */
static const FixtureClueRun beginner_seed185_pause0_clues[21] = {
    {1u, 1u, 1u, 1u}, {2u, 1u, 2u, 0u}, {4u, 1u, 1u, 1u}, {10u, 1u, 1u, 2u}, {11u, 1u, 2u, 1u},
    {13u, 1u, 1u, 2u}, {20u, 1u, 1u, 1u}, {23u, 1u, 1u, 2u}, {27u, 1u, 7u, 1u}, {36u, 1u, 6u, 0u},
    {42u, 1u, 1u, 1u}, {45u, 1u, 5u, 0u}, {50u, 1u, 1u, 1u}, {51u, 1u, 1u, 2u}, {54u, 1u, 5u, 0u},
    {59u, 1u, 1u, 2u}, {63u, 1u, 5u, 0u}, {68u, 1u, 1u, 2u}, {72u, 1u, 5u, 0u}, {77u, 1u, 1u, 1u},
    {78u, 1u, 1u, 2u},
};
static const FixtureRun beginner_seed185_pause0_class0[4] = {
    {18u, 3u, 3u}, {25u, 1u, 1u}, {34u, 1u, 1u}, {61u, 1u, 1u},
};
static const FixtureRun beginner_seed185_pause0_class1[3] = {
    {5u, 1u, 4u}, {15u, 1u, 3u}, {26u, 9u, 7u},
};
static const FixtureRun beginner_seed185_pause0_class2[1] = {
    {52u, 1u, 1u},
};
static const FixtureRun beginner_seed185_pause0_class3[4] = {
    {0u, 1u, 1u}, {9u, 1u, 1u}, {70u, 1u, 1u}, {79u, 1u, 1u},
};
static const FixtureRun beginner_seed185_pause0_class4[1] = {
    {43u, 1u, 1u},
};
static const FixtureRun beginner_seed185_pause0_class5[1] = {
    {14u, 1u, 1u},
};
static const FixtureRun beginner_seed185_pause0_class6[2] = {
    {19u, 1u, 1u}, {22u, 1u, 1u},
};
static const FixtureRun beginner_seed185_pause0_class7[2] = {
    {60u, 1u, 1u}, {69u, 1u, 1u},
};
static const FixtureOddsClass beginner_seed185_pause0_classes[8] = {
    {{UINT64_C(1), UINT64_C(9), UINT64_C(0x3fbc71c71c71c71c)}, beginner_seed185_pause0_class0, 4u}, /* 6 cell(s), p = 1/9 ~ 0.1111111111111111 */
    {{UINT64_C(5), UINT64_C(39), UINT64_C(0x3fc0690690690690)}, beginner_seed185_pause0_class1, 3u}, /* 14 cell(s), p = 5/39 ~ 0.12820512820512819 */
    {{UINT64_C(2), UINT64_C(9), UINT64_C(0x3fcc71c71c71c71c)}, beginner_seed185_pause0_class2, 1u}, /* 1 cell(s), p = 2/9 ~ 0.22222222222222221 */
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, beginner_seed185_pause0_class3, 4u}, /* 4 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(2), UINT64_C(3), UINT64_C(0x3fe5555555555555)}, beginner_seed185_pause0_class4, 1u}, /* 1 cell(s), p = 2/3 ~ 0.66666666666666663 */
    {{UINT64_C(34), UINT64_C(39), UINT64_C(0x3febe5be5be5be5c)}, beginner_seed185_pause0_class5, 1u}, /* 1 cell(s), p = 34/39 ~ 0.87179487179487181 */
    {{UINT64_C(8), UINT64_C(9), UINT64_C(0x3fec71c71c71c71c)}, beginner_seed185_pause0_class6, 2u}, /* 2 cell(s), p = 8/9 ~ 0.88888888888888884 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, beginner_seed185_pause0_class7, 2u}, /* 2 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t beginner_seed185_pause0_layouts_limbs[1] = {
    0x000001d4u,
};

/* beginner_seed185_pause1: 9x9, 10 mine(s).
 * Observation at pause 1 of autosolve script beginner_seed185; every
 * unflagged hidden cell has 0 < p < 1, so autosolve must stop.
 * Observation ('?' hidden, digits revealed clues):
 *   ?10011100
 *   22112?100
 *   1?11?2100
 *   111111122
 *   0000001??
 *   000001232
 *   000002?31
 *   000002???
 *   0000012?? */
static const FixtureClueRun beginner_seed185_pause1_clues[32] = {
    {1u, 1u, 1u, 1u}, {2u, 1u, 2u, 0u}, {4u, 1u, 3u, 1u}, {7u, 1u, 2u, 0u}, {9u, 1u, 2u, 2u},
    {11u, 1u, 2u, 1u}, {13u, 1u, 1u, 2u}, {15u, 1u, 1u, 1u}, {16u, 1u, 2u, 0u}, {18u, 1u, 1u, 1u},
    {20u, 1u, 2u, 1u}, {23u, 1u, 1u, 2u}, {24u, 1u, 1u, 1u}, {25u, 1u, 2u, 0u}, {27u, 1u, 7u, 1u},
    {34u, 1u, 2u, 2u}, {36u, 1u, 6u, 0u}, {42u, 1u, 1u, 1u}, {45u, 1u, 5u, 0u}, {50u, 1u, 1u, 1u},
    {51u, 1u, 1u, 2u}, {52u, 1u, 1u, 3u}, {53u, 1u, 1u, 2u}, {54u, 1u, 5u, 0u}, {59u, 1u, 1u, 2u},
    {61u, 1u, 1u, 3u}, {62u, 1u, 1u, 1u}, {63u, 1u, 5u, 0u}, {68u, 1u, 1u, 2u}, {72u, 1u, 5u, 0u},
    {77u, 1u, 1u, 1u}, {78u, 1u, 1u, 2u},
};
static const FixtureRun beginner_seed185_pause1_class0[2] = {
    {70u, 1u, 2u}, {79u, 1u, 2u},
};
static const FixtureRun beginner_seed185_pause1_class1[7] = {
    {0u, 1u, 1u}, {14u, 1u, 1u}, {19u, 1u, 1u}, {22u, 1u, 1u}, {43u, 1u, 2u}, {60u, 1u, 1u},
    {69u, 1u, 1u},
};
static const FixtureOddsClass beginner_seed185_pause1_classes[2] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, beginner_seed185_pause1_class0, 2u}, /* 4 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, beginner_seed185_pause1_class1, 7u}, /* 8 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t beginner_seed185_pause1_layouts_limbs[1] = {
    0x00000002u,
};

static const ProbabilityCase AUTOSOLVE_PAUSE_CASES[] = {
    {
        .name = "beginner_seed11_pause0",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests pause of beginner_seed11",
        .width = 9u, .height = 9u, .total_mines = 10u,
        .clues = beginner_seed11_pause0_clues, .clue_run_count = 27u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = beginner_seed11_pause0_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = beginner_seed11_pause0_class1, .proven_mine_runs = 5u,
        .layouts = {beginner_seed11_pause0_layouts_limbs, 1u},
        .meta = {4u, 1u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "beginner_seed1_pause0",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests pause of beginner_seed1",
        .width = 9u, .height = 9u, .total_mines = 10u,
        .clues = beginner_seed1_pause0_clues, .clue_run_count = 25u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = beginner_seed1_pause0_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = beginner_seed1_pause0_class1, .proven_mine_runs = 5u,
        .layouts = {beginner_seed1_pause0_layouts_limbs, 1u},
        .meta = {2u, 1u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "beginner_seed185_pause0",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests pause of beginner_seed185",
        .width = 9u, .height = 9u, .total_mines = 10u,
        .clues = beginner_seed185_pause0_clues, .clue_run_count = 21u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = beginner_seed185_pause0_classes, .class_count = 8u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = beginner_seed185_pause0_class7, .proven_mine_runs = 2u,
        .layouts = {beginner_seed185_pause0_layouts_limbs, 1u},
        .meta = {17u, 2u, 12u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER,
    },
    {
        .name = "beginner_seed185_pause1",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests pause of beginner_seed185",
        .width = 9u, .height = 9u, .total_mines = 10u,
        .clues = beginner_seed185_pause1_clues, .clue_run_count = 32u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = beginner_seed185_pause1_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = beginner_seed185_pause1_class1, .proven_mine_runs = 7u,
        .layouts = {beginner_seed185_pause1_layouts_limbs, 1u},
        .meta = {3u, 1u, 1u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    }
};
#define AUTOSOLVE_PAUSE_CASE_COUNT (sizeof(AUTOSOLVE_PAUSE_CASES) / sizeof(AUTOSOLVE_PAUSE_CASES[0]))

static const uint32_t autosolve_beginner_seed11_layout[10] = {
    6u, 23u, 24u, 66u, 68u, 69u, 74u, 75u, 77u, 80u,
};
static const FixtureRun autosolve_beginner_seed11_e0_safe[3] = {
    {15u, 1u, 1u}, {67u, 1u, 1u}, {78u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed11_e0_mines[5] = {
    {6u, 1u, 1u}, {23u, 1u, 2u}, {66u, 1u, 1u}, {68u, 1u, 2u}, {74u, 1u, 2u},
};
static const FixtureRun autosolve_beginner_seed11_e1_mines[5] = {
    {6u, 1u, 1u}, {23u, 1u, 2u}, {66u, 1u, 1u}, {68u, 1u, 2u}, {74u, 1u, 2u},
};
static const FixtureRun autosolve_beginner_seed11_e3_safe[1] = {
    {79u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed11_e3_mines[7] = {
    {6u, 1u, 1u}, {23u, 1u, 2u}, {66u, 1u, 1u}, {68u, 1u, 2u}, {74u, 1u, 2u}, {77u, 1u, 1u},
    {80u, 1u, 1u},
};
static const AutosolveEvent autosolve_beginner_seed11_events[4] = {
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 3u, .proven_mine_count = 8u, .proven_safe = autosolve_beginner_seed11_e0_safe, .proven_safe_runs = 3u, .proven_mines = autosolve_beginner_seed11_e0_mines, .proven_mine_runs = 5u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 8, .revealed_count = 69u,
     .board = "000001F10" "000013320" "00001FF10" "000012210" "000000000" "000000000" "001122210" "013F4FF21" "01FF##3##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 0u, .proven_mine_count = 8u, .proven_mines = autosolve_beginner_seed11_e1_mines, .proven_mine_runs = 5u,
     .changed = 0u, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 8, .revealed_count = 69u, .pause_odds = &AUTOSOLVE_PAUSE_CASES[0],
     .board = "000001F10" "000013320" "00001FF10" "000012210" "000000000" "000000000" "001122210" "013F4FF21" "01FF##3##"},
    {.kind = FIXTURE_AUTOSOLVE_MANUAL_REVEAL, .cell = 76u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 8, .revealed_count = 70u,
     .board = "000001F10" "000013320" "00001FF10" "000012210" "000000000" "000000000" "001122210" "013F4FF21" "01FF4#3##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 1u, .proven_mine_count = 10u, .proven_safe = autosolve_beginner_seed11_e3_safe, .proven_safe_runs = 1u, .proven_mines = autosolve_beginner_seed11_e3_mines, .proven_mine_runs = 7u,
     .changed = 1u, .status = FIXTURE_GAME_WON, .revision_after = 4, .flags_after = 10, .revealed_count = 71u,
     .board = "000001F10" "000013320" "00001FF10" "000012210" "000000000" "000000000" "001122210" "013F4FF21" "01FF4F32F"},
};

static const uint32_t autosolve_beginner_seed1_layout[10] = {
    6u, 8u, 15u, 17u, 26u, 35u, 57u, 66u, 69u, 72u,
};
static const FixtureRun autosolve_beginner_seed1_e0_safe[4] = {
    {16u, 1u, 1u}, {67u, 1u, 2u}, {73u, 1u, 3u}, {78u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed1_e0_mines[5] = {
    {6u, 1u, 1u}, {15u, 1u, 1u}, {17u, 9u, 3u}, {57u, 1u, 1u}, {66u, 3u, 3u},
};
static const FixtureRun autosolve_beginner_seed1_e1_safe[1] = {
    {76u, 1u, 2u},
};
static const FixtureRun autosolve_beginner_seed1_e1_mines[5] = {
    {6u, 1u, 1u}, {15u, 1u, 1u}, {17u, 9u, 3u}, {57u, 1u, 1u}, {66u, 3u, 3u},
};
static const FixtureRun autosolve_beginner_seed1_e2_mines[5] = {
    {6u, 1u, 1u}, {15u, 1u, 1u}, {17u, 9u, 3u}, {57u, 1u, 1u}, {66u, 3u, 3u},
};
static const AutosolveEvent autosolve_beginner_seed1_events[4] = {
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 7u, .proven_mine_count = 9u, .proven_safe = autosolve_beginner_seed1_e0_safe, .proven_safe_runs = 4u, .proven_mines = autosolve_beginner_seed1_e0_mines, .proven_mine_runs = 5u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 9, .revealed_count = 68u,
     .board = "000002F##" "000002F5F" "00000114F" "00000002F" "000000011" "001110000" "002F21110" "112F21F10" "F111##110"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 2u, .proven_mine_count = 9u, .proven_safe = autosolve_beginner_seed1_e1_safe, .proven_safe_runs = 1u, .proven_mines = autosolve_beginner_seed1_e1_mines, .proven_mine_runs = 5u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 9, .revealed_count = 70u,
     .board = "000002F##" "000002F5F" "00000114F" "00000002F" "000000011" "001110000" "002F21110" "112F21F10" "F11111110"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 0u, .proven_mine_count = 9u, .proven_mines = autosolve_beginner_seed1_e2_mines, .proven_mine_runs = 5u,
     .changed = 0u, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 9, .revealed_count = 70u, .pause_odds = &AUTOSOLVE_PAUSE_CASES[1],
     .board = "000002F##" "000002F5F" "00000114F" "00000002F" "000000011" "001110000" "002F21110" "112F21F10" "F11111110"},
    {.kind = FIXTURE_AUTOSOLVE_MANUAL_REVEAL, .cell = 7u,
     .changed = 1u, .status = FIXTURE_GAME_WON, .revision_after = 4, .flags_after = 10, .revealed_count = 71u,
     .board = "000002F4F" "000002F5F" "00000114F" "00000002F" "000000011" "001110000" "002F21110" "112F21F10" "F11111110"},
};

static const uint32_t autosolve_beginner_seed4_layout[10] = {
    2u, 8u, 11u, 13u, 19u, 25u, 33u, 44u, 59u, 70u,
};
static const FixtureRun autosolve_beginner_seed4_e0_safe[6] = {
    {10u, 2u, 3u}, {15u, 1u, 1u}, {18u, 1u, 1u}, {24u, 1u, 1u}, {42u, 9u, 3u}, {61u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed4_e0_mines[5] = {
    {11u, 1u, 1u}, {13u, 1u, 1u}, {19u, 1u, 1u}, {33u, 1u, 1u}, {59u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed4_e1_safe[5] = {
    {4u, 1u, 4u}, {9u, 1u, 1u}, {34u, 9u, 3u}, {53u, 9u, 3u}, {79u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed4_e1_mines[6] = {
    {11u, 1u, 1u}, {13u, 1u, 1u}, {19u, 1u, 1u}, {33u, 1u, 1u}, {59u, 1u, 1u}, {70u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed4_e2_safe[5] = {
    {0u, 1u, 2u}, {3u, 1u, 1u}, {26u, 1u, 1u}, {35u, 1u, 1u}, {80u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed4_e2_mines[7] = {
    {2u, 1u, 1u}, {11u, 1u, 1u}, {13u, 6u, 3u}, {33u, 1u, 1u}, {44u, 1u, 1u}, {59u, 1u, 1u},
    {70u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed4_e3_safe[1] = {
    {17u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed4_e3_mines[8] = {
    {2u, 1u, 1u}, {8u, 1u, 1u}, {11u, 1u, 1u}, {13u, 6u, 3u}, {33u, 1u, 1u}, {44u, 1u, 1u},
    {59u, 1u, 1u}, {70u, 1u, 1u},
};
static const AutosolveEvent autosolve_beginner_seed4_events[4] = {
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 10u, .proven_mine_count = 5u, .proven_safe = autosolve_beginner_seed4_e0_safe, .proven_safe_runs = 6u, .proven_mines = autosolve_beginner_seed4_e0_mines, .proven_mine_runs = 5u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 5, .revealed_count = 51u,
     .board = "#########" "#3F3F11##" "1F22122##" "111001F##" "0000011##" "0000111##" "00001F21#" "0000112##" "0000001##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 12u, .proven_mine_count = 6u, .proven_safe = autosolve_beginner_seed4_e1_safe, .proven_safe_runs = 5u, .proven_mines = autosolve_beginner_seed4_e1_mines, .proven_mine_runs = 6u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 6, .revealed_count = 64u,
     .board = "####1101#" "13F3F112#" "1F22122##" "111001F3#" "00000112#" "000011111" "00001F211" "0000112F1" "00000011#"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 6u, .proven_mine_count = 9u, .proven_safe = autosolve_beginner_seed4_e2_safe, .proven_safe_runs = 5u, .proven_mines = autosolve_beginner_seed4_e2_mines, .proven_mine_runs = 7u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 9, .revealed_count = 70u,
     .board = "02F31101#" "13F3F112#" "1F22122F1" "111001F32" "00000112F" "000011111" "00001F211" "0000112F1" "000000111"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 1u, .proven_mine_count = 10u, .proven_safe = autosolve_beginner_seed4_e3_safe, .proven_safe_runs = 1u, .proven_mines = autosolve_beginner_seed4_e3_mines, .proven_mine_runs = 8u,
     .changed = 1u, .status = FIXTURE_GAME_WON, .revision_after = 5, .flags_after = 10, .revealed_count = 71u,
     .board = "02F31101F" "13F3F1122" "1F22122F1" "111001F32" "00000112F" "000011111" "00001F211" "0000112F1" "000000111"},
};

static const uint32_t autosolve_beginner_seed185_layout[10] = {
    0u, 14u, 19u, 22u, 43u, 44u, 60u, 69u, 71u, 79u,
};
static const FixtureRun autosolve_beginner_seed185_e0_safe[3] = {
    {20u, 1u, 1u}, {23u, 1u, 1u}, {78u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e0_mines[2] = {
    {60u, 1u, 1u}, {69u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e1_safe[1] = {
    {10u, 1u, 3u},
};
static const FixtureRun autosolve_beginner_seed185_e1_mines[2] = {
    {60u, 1u, 1u}, {69u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e2_safe[2] = {
    {1u, 1u, 4u}, {13u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e2_mines[2] = {
    {60u, 1u, 1u}, {69u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e3_mines[2] = {
    {60u, 1u, 1u}, {69u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e5_safe[2] = {
    {6u, 1u, 1u}, {15u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e5_mines[3] = {
    {14u, 1u, 1u}, {60u, 1u, 1u}, {69u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e6_safe[5] = {
    {7u, 1u, 1u}, {16u, 1u, 1u}, {18u, 3u, 3u}, {25u, 1u, 1u}, {52u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e6_mines[5] = {
    {14u, 1u, 1u}, {19u, 1u, 1u}, {22u, 1u, 1u}, {60u, 1u, 1u}, {69u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e7_safe[3] = {
    {9u, 1u, 1u}, {53u, 1u, 1u}, {61u, 1u, 2u},
};
static const FixtureRun autosolve_beginner_seed185_e7_mines[7] = {
    {0u, 1u, 1u}, {14u, 1u, 1u}, {19u, 1u, 1u}, {22u, 1u, 1u}, {43u, 1u, 2u}, {60u, 1u, 1u},
    {69u, 1u, 1u},
};
static const FixtureRun autosolve_beginner_seed185_e8_mines[7] = {
    {0u, 1u, 1u}, {14u, 1u, 1u}, {19u, 1u, 1u}, {22u, 1u, 1u}, {43u, 1u, 2u}, {60u, 1u, 1u},
    {69u, 1u, 1u},
};
static const AutosolveEvent autosolve_beginner_seed185_events[9] = {
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 3u, .proven_mine_count = 2u, .proven_safe = autosolve_beginner_seed185_e0_safe, .proven_safe_runs = 3u, .proven_mines = autosolve_beginner_seed185_e0_mines, .proven_mine_runs = 2u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 2, .revealed_count = 42u,
     .board = "#########" "#########" "##1##2###" "1111111##" "0000001##" "0000012##" "000002F##" "000002F##" "0000012##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 3u, .proven_mine_count = 2u, .proven_safe = autosolve_beginner_seed185_e1_safe, .proven_safe_runs = 1u, .proven_mines = autosolve_beginner_seed185_e1_mines, .proven_mine_runs = 2u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 2, .revealed_count = 45u,
     .board = "#########" "#211#####" "##1##2###" "1111111##" "0000001##" "0000012##" "000002F##" "000002F##" "0000012##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 5u, .proven_mine_count = 2u, .proven_safe = autosolve_beginner_seed185_e2_safe, .proven_safe_runs = 2u, .proven_mines = autosolve_beginner_seed185_e2_mines, .proven_mine_runs = 2u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 2, .revealed_count = 50u,
     .board = "#1001####" "#2112####" "##1##2###" "1111111##" "0000001##" "0000012##" "000002F##" "000002F##" "0000012##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 0u, .proven_mine_count = 2u, .proven_mines = autosolve_beginner_seed185_e3_mines, .proven_mine_runs = 2u,
     .changed = 0u, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 2, .revealed_count = 50u, .pause_odds = &AUTOSOLVE_PAUSE_CASES[2],
     .board = "#1001####" "#2112####" "##1##2###" "1111111##" "0000001##" "0000012##" "000002F##" "000002F##" "0000012##"},
    {.kind = FIXTURE_AUTOSOLVE_MANUAL_REVEAL, .cell = 5u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 5, .flags_after = 2, .revealed_count = 51u,
     .board = "#10011###" "#2112####" "##1##2###" "1111111##" "0000001##" "0000012##" "000002F##" "000002F##" "0000012##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 2u, .proven_mine_count = 3u, .proven_safe = autosolve_beginner_seed185_e5_safe, .proven_safe_runs = 2u, .proven_mines = autosolve_beginner_seed185_e5_mines, .proven_mine_runs = 3u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 6, .flags_after = 3, .revealed_count = 53u,
     .board = "#100111##" "#2112F1##" "##1##2###" "1111111##" "0000001##" "0000012##" "000002F##" "000002F##" "0000012##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 7u, .proven_mine_count = 5u, .proven_safe = autosolve_beginner_seed185_e6_safe, .proven_safe_runs = 5u, .proven_mines = autosolve_beginner_seed185_e6_mines, .proven_mine_runs = 5u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 7, .flags_after = 5, .revealed_count = 65u,
     .board = "#10011100" "#2112F100" "1F11F2100" "111111122" "0000001##" "00000123#" "000002F##" "000002F##" "0000012##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 4u, .proven_mine_count = 8u, .proven_safe = autosolve_beginner_seed185_e7_safe, .proven_safe_runs = 3u, .proven_mines = autosolve_beginner_seed185_e7_mines, .proven_mine_runs = 7u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 8, .flags_after = 8, .revealed_count = 69u,
     .board = "F10011100" "22112F100" "1F11F2100" "111111122" "0000001FF" "000001232" "000002F31" "000002F##" "0000012##"},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 0u, .proven_mine_count = 8u, .proven_mines = autosolve_beginner_seed185_e8_mines, .proven_mine_runs = 7u,
     .changed = 0u, .status = FIXTURE_GAME_PLAYING, .revision_after = 8, .flags_after = 8, .revealed_count = 69u, .pause_odds = &AUTOSOLVE_PAUSE_CASES[3],
     .board = "F10011100" "22112F100" "1F11F2100" "111111122" "0000001FF" "000001232" "000002F31" "000002F##" "0000012##"},
};

static const AutosolveEvent autosolve_largest_board_seed19_events[43] = {
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 14u, .proven_mine_count = 11u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 11, .revealed_count = 57u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 21u, .proven_mine_count = 17u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 17, .revealed_count = 253u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 63u, .proven_mine_count = 60u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 60, .revealed_count = 319u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 75u, .proven_mine_count = 75u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 5, .flags_after = 75, .revealed_count = 446u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 75u, .proven_mine_count = 107u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 6, .flags_after = 107, .revealed_count = 544u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 89u, .proven_mine_count = 137u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 7, .flags_after = 137, .revealed_count = 694u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 91u, .proven_mine_count = 181u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 8, .flags_after = 181, .revealed_count = 864u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 84u, .proven_mine_count = 229u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 9, .flags_after = 229, .revealed_count = 996u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 105u, .proven_mine_count = 262u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 10, .flags_after = 262, .revealed_count = 1206u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 98u, .proven_mine_count = 313u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 11, .flags_after = 313, .revealed_count = 1333u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 100u, .proven_mine_count = 365u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 12, .flags_after = 365, .revealed_count = 1536u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 104u, .proven_mine_count = 403u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 13, .flags_after = 403, .revealed_count = 1672u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 121u, .proven_mine_count = 449u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 14, .flags_after = 449, .revealed_count = 1896u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 125u, .proven_mine_count = 502u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 15, .flags_after = 502, .revealed_count = 2099u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 134u, .proven_mine_count = 562u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 16, .flags_after = 562, .revealed_count = 2303u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 130u, .proven_mine_count = 607u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 17, .flags_after = 607, .revealed_count = 2502u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 131u, .proven_mine_count = 685u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 18, .flags_after = 685, .revealed_count = 2656u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 112u, .proven_mine_count = 734u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 19, .flags_after = 734, .revealed_count = 2880u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 100u, .proven_mine_count = 780u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 20, .flags_after = 780, .revealed_count = 3045u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 106u, .proven_mine_count = 827u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 21, .flags_after = 827, .revealed_count = 3230u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 128u, .proven_mine_count = 880u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 22, .flags_after = 880, .revealed_count = 3399u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 97u, .proven_mine_count = 906u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 23, .flags_after = 906, .revealed_count = 3531u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 85u, .proven_mine_count = 941u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 24, .flags_after = 941, .revealed_count = 3698u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 80u, .proven_mine_count = 981u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 25, .flags_after = 981, .revealed_count = 3900u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 103u, .proven_mine_count = 1024u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 26, .flags_after = 1024, .revealed_count = 4150u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 104u, .proven_mine_count = 1073u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 27, .flags_after = 1073, .revealed_count = 4283u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 83u, .proven_mine_count = 1102u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 28, .flags_after = 1102, .revealed_count = 4409u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 77u, .proven_mine_count = 1131u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 29, .flags_after = 1131, .revealed_count = 4608u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 100u, .proven_mine_count = 1170u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 30, .flags_after = 1170, .revealed_count = 4822u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 72u, .proven_mine_count = 1202u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 31, .flags_after = 1202, .revealed_count = 4917u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 38u, .proven_mine_count = 1222u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 32, .flags_after = 1222, .revealed_count = 4961u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 24u, .proven_mine_count = 1233u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 33, .flags_after = 1233, .revealed_count = 4985u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 17u, .proven_mine_count = 1243u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 34, .flags_after = 1243, .revealed_count = 5002u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 16u, .proven_mine_count = 1250u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 35, .flags_after = 1250, .revealed_count = 5024u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 16u, .proven_mine_count = 1254u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 36, .flags_after = 1254, .revealed_count = 5041u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 8u, .proven_mine_count = 1258u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 37, .flags_after = 1258, .revealed_count = 5049u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 7u, .proven_mine_count = 1258u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 38, .flags_after = 1258, .revealed_count = 5056u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 7u, .proven_mine_count = 1260u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 39, .flags_after = 1260, .revealed_count = 5085u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 8u, .proven_mine_count = 1269u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 40, .flags_after = 1269, .revealed_count = 5093u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 12u, .proven_mine_count = 1275u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 41, .flags_after = 1275, .revealed_count = 5105u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 10u, .proven_mine_count = 1275u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 42, .flags_after = 1275, .revealed_count = 5115u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 3u, .proven_mine_count = 1278u,
     .changed = 1u, .status = FIXTURE_GAME_PLAYING, .revision_after = 43, .flags_after = 1278, .revealed_count = 5118u},
    {.kind = FIXTURE_AUTOSOLVE_BATCH, .solver_status = FIXTURE_STATUS_EXACT, .proven_safe_count = 0u, .proven_mine_count = 1278u,
     .changed = 0u, .status = FIXTURE_GAME_PLAYING, .revision_after = 43, .flags_after = 1278, .revealed_count = 5118u},
};
static const AutosolveScript AUTOSOLVE_SCRIPTS[] = {
    /* beginner_seed11:
     * Baseline seed 11: one batch, pause, surviving reveal, one more batch wins.
     * Proof sets of 3 of 3 solves re-derived by the frontier oracle.
     * Trajectory: 3 batch(es), 1 pause(s), a surviving manual reveal, final status won. */
    {
        .name = "beginner_seed11",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests.test_repeated_batches_pause_and_resume_after_surviving_manual_move (Game(9,9,10, random.Random(11)), first reveal (4,4))",
        .width = 9u, .height = 9u, .mines = 10u,
        .layout = autosolve_beginner_seed11_layout, .layout_count = 10u,
        .layout_rows = NULL,
        .first_cell = 40u,
        .board_after_first = "000001#10" "000013#20" "00001##10" "000012210" "000000000" "000000000" "001122210" "013####21" "01#######",
        .events = autosolve_beginner_seed11_events, .event_count = 4u,
        .normative = 1u,
    },
    /* beginner_seed1:
     * Baseline seed 1: two batches, pause with one uncertain mine left; the
     * surviving reveal wins outright.
     * Proof sets of 3 of 3 solves re-derived by the frontier oracle.
     * Trajectory: 3 batch(es), 1 pause(s), a surviving manual reveal, final status won. */
    {
        .name = "beginner_seed1",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests.test_repeated_batches_pause_and_resume_after_surviving_manual_move (Game(9,9,10, random.Random(1)), first reveal (4,4))",
        .width = 9u, .height = 9u, .mines = 10u,
        .layout = autosolve_beginner_seed1_layout, .layout_count = 10u,
        .layout_rows = NULL,
        .first_cell = 40u,
        .board_after_first = "000002###" "000002###" "00000114#" "00000002#" "000000011" "001110000" "002#21110" "112####10" "#######10",
        .events = autosolve_beginner_seed1_events, .event_count = 4u,
        .normative = 1u,
    },
    /* beginner_seed4:
     * Baseline seed 4: four consecutive batches win without any guess.
     * Proof sets of 4 of 4 solves re-derived by the frontier oracle.
     * Trajectory: 4 batch(es), 0 pause(s), final status won. */
    {
        .name = "beginner_seed4",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests.test_repeated_batches_pause_and_resume_after_surviving_manual_move (Game(9,9,10, random.Random(4)), first reveal (4,4))",
        .width = 9u, .height = 9u, .mines = 10u,
        .layout = autosolve_beginner_seed4_layout, .layout_count = 10u,
        .layout_rows = NULL,
        .first_cell = 40u,
        .board_after_first = "#########" "#########" "##2212###" "111001###" "000001###" "000011###" "00001####" "0000112##" "0000001##",
        .events = autosolve_beginner_seed4_events, .event_count = 4u,
        .normative = 1u,
    },
    /* beginner_seed185:
     * Extra layout (first seed after the baseline's 0..11 with >= 2 changing
     * batches both before the pause and after the surviving reveal).
     * Proof sets of 8 of 8 solves re-derived by the frontier oracle.
     * Trajectory: 8 batch(es), 2 pause(s), a surviving manual reveal, final status playing. */
    {
        .name = "beginner_seed185",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests.test_repeated_batches_pause_and_resume_after_surviving_manual_move (extra layout: random.Random(185), first reveal (4,4))",
        .width = 9u, .height = 9u, .mines = 10u,
        .layout = autosolve_beginner_seed185_layout, .layout_count = 10u,
        .layout_rows = NULL,
        .first_cell = 40u,
        .board_after_first = "#########" "#########" "#########" "1111111##" "0000001##" "0000012##" "000002###" "000002###" "000001###",
        .events = autosolve_beginner_seed185_events, .event_count = 9u,
        .normative = 1u,
    },
    /* largest_board_seed19:
     * 80x80 with 1280 mines. Every solve along the way is exact, so the
     * trajectory is ground truth; it must never reveal a mine. Per-batch
     * proof counts are recorded; the paused final board is
     * LARGE_AUTOSOLVE_SEED19_FINAL_ROWS.
     * Proof sets of 43 of 43 solves re-derived by the frontier oracle.
     * Trajectory: 43 batch(es), 1 pause(s), final status playing. */
    {
        .name = "largest_board_seed19",
        .provenance = "tests.test_autosolve.RealAutosolveProgressionTests.test_largest_board_batches_never_guess (Game(80,80,1280, random.Random(19)), first reveal (40,40))",
        .width = 80u, .height = 80u, .mines = 1280u,
        .layout = NULL, .layout_count = 0u,
        .layout_rows = LARGE_AUTOSOLVE_SEED19_LAYOUT_ROWS,
        .first_cell = 3240u,
        .board_after_first = NULL,
        .events = autosolve_largest_board_seed19_events, .event_count = 43u,
        .normative = 1u,
    }
};
#define AUTOSOLVE_SCRIPT_COUNT (sizeof(AUTOSOLVE_SCRIPTS) / sizeof(AUTOSOLVE_SCRIPTS[0]))
#define AUTOSOLVE_SCRIPT_BEGINNER_SEED11 0u
#define AUTOSOLVE_SCRIPT_BEGINNER_SEED1 1u
#define AUTOSOLVE_SCRIPT_BEGINNER_SEED4 2u
#define AUTOSOLVE_SCRIPT_BEGINNER_SEED185 3u
#define AUTOSOLVE_SCRIPT_LARGEST_BOARD_SEED19 4u
