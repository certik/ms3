/*
 * Probability fixtures (exact odds, contradictions, malformed inputs).
 *
 * GENERATED reference data - do not edit by hand. Frozen from the Python
 * reference at baseline commit 153b78ac025858f11c124e6ae17953fdecd0aec3 by a one-off
 * generator kept outside the repository (see fixture_types.h for the
 * method, oracles, conventions and schema). Provenance strings name the
 * baseline unittest each case ports (module.Class.method).
 *
 * Exact probability observations (sparse clue runs) with ground-truth odds,
 * proofs and layout counts; inconsistent observations; malformed arguments;
 * and public-information equivalence pairs. Default budgets for every case:
 * 1.5 s, 100000 counting nodes, 2000 samples (all cases are exact at baseline).
 * Port destinations: tests/c/test_probability.c (solver) and
 * tests/c/test_bigint.c (layout counts); see tests/coverage-map.json.
 */
#pragma once

#include "fixture_types.h"

/* readme_weighting: 11x1, 3 mine(s).
 * Clues give a+b=1 (cell 1) and b+c=1 (cell 3) over a=0, b=2, c=4; six
 * unconstrained cells 5..10 and three mines. b alone leaves C(6,2)=15
 * completions, a and c leave C(6,1)=6: Z=21, P(b)=15/21, P(a)=P(c)=6/21,
 * each unconstrained cell (15*2+6*1)/(21*6)=2/7 (not the naive 3/9).
 * Baseline internals: component histogram H={k=1:1, k=2:1}, binomial
 * row T[t=1..2]=[C(6,2), C(6,1)]=[15, 6].
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1??????? */
static const FixtureClueRun readme_weighting_clues[2] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u},
};
static const FixtureRun readme_weighting_class0[2] = {
    {0u, 1u, 1u}, {4u, 1u, 7u},
};
static const FixtureRun readme_weighting_class1[1] = {
    {2u, 1u, 1u},
};
static const FixtureOddsClass readme_weighting_classes[2] = {
    {{UINT64_C(2), UINT64_C(7), UINT64_C(0x3fd2492492492492)}, readme_weighting_class0, 2u}, /* 8 cell(s), p = 2/7 ~ 0.2857142857142857 */
    {{UINT64_C(5), UINT64_C(7), UINT64_C(0x3fe6db6db6db6db7)}, readme_weighting_class1, 1u}, /* 1 cell(s), p = 5/7 ~ 0.7142857142857143 */
};
static const uint32_t readme_weighting_layouts_limbs[1] = {
    0x00000015u,
};

/* components_total_2: 12x1, 2 mine(s).
 * Two disconnected components {0,2,4} and {6,8,10} (middle mine or both
 * ends) plus unconstrained cells 5 and 11, coupled only by the total:
 * Z = sum over kA,kB in {1,2} of C(2, total - kA - kB).
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1???1?1?? */
static const FixtureClueRun components_total_2_clues[4] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {9u, 1u, 1u, 1u},
};
static const FixtureRun components_total_2_class0[3] = {
    {0u, 1u, 1u}, {4u, 1u, 3u}, {10u, 1u, 2u},
};
static const FixtureRun components_total_2_class1[2] = {
    {2u, 1u, 1u}, {8u, 1u, 1u},
};
static const FixtureOddsClass components_total_2_classes[2] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, components_total_2_class0, 3u}, /* 6 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, components_total_2_class1, 2u}, /* 2 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t components_total_2_layouts_limbs[1] = {
    0x00000001u,
};

/* components_total_3: 12x1, 3 mine(s).
 * Two disconnected components {0,2,4} and {6,8,10} (middle mine or both
 * ends) plus unconstrained cells 5 and 11, coupled only by the total:
 * Z = sum over kA,kB in {1,2} of C(2, total - kA - kB).
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1???1?1?? */
static const FixtureClueRun components_total_3_clues[4] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {9u, 1u, 1u, 1u},
};
static const FixtureRun components_total_3_class0[3] = {
    {0u, 1u, 1u}, {4u, 1u, 3u}, {10u, 1u, 2u},
};
static const FixtureRun components_total_3_class1[2] = {
    {2u, 1u, 1u}, {8u, 1u, 1u},
};
static const FixtureOddsClass components_total_3_classes[2] = {
    {{UINT64_C(1), UINT64_C(4), UINT64_C(0x3fd0000000000000)}, components_total_3_class0, 3u}, /* 6 cell(s), p = 1/4 ~ 0.25 */
    {{UINT64_C(3), UINT64_C(4), UINT64_C(0x3fe8000000000000)}, components_total_3_class1, 2u}, /* 2 cell(s), p = 3/4 ~ 0.75 */
};
static const uint32_t components_total_3_layouts_limbs[1] = {
    0x00000004u,
};

/* components_total_4: 12x1, 4 mine(s).
 * Two disconnected components {0,2,4} and {6,8,10} (middle mine or both
 * ends) plus unconstrained cells 5 and 11, coupled only by the total:
 * Z = sum over kA,kB in {1,2} of C(2, total - kA - kB).
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1???1?1?? */
static const FixtureClueRun components_total_4_clues[4] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {9u, 1u, 1u, 1u},
};
static const FixtureRun components_total_4_class0[4] = {
    {0u, 2u, 3u}, {5u, 1u, 2u}, {8u, 1u, 1u}, {10u, 1u, 2u},
};
static const FixtureOddsClass components_total_4_classes[1] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, components_total_4_class0, 4u}, /* 8 cell(s), p = 1/2 ~ 0.5 */
};
static const uint32_t components_total_4_layouts_limbs[1] = {
    0x00000006u,
};

/* components_total_5: 12x1, 5 mine(s).
 * Two disconnected components {0,2,4} and {6,8,10} (middle mine or both
 * ends) plus unconstrained cells 5 and 11, coupled only by the total:
 * Z = sum over kA,kB in {1,2} of C(2, total - kA - kB).
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1???1?1?? */
static const FixtureClueRun components_total_5_clues[4] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {9u, 1u, 1u, 1u},
};
static const FixtureRun components_total_5_class0[2] = {
    {2u, 1u, 1u}, {8u, 1u, 1u},
};
static const FixtureRun components_total_5_class1[3] = {
    {0u, 1u, 1u}, {4u, 1u, 3u}, {10u, 1u, 2u},
};
static const FixtureOddsClass components_total_5_classes[2] = {
    {{UINT64_C(1), UINT64_C(4), UINT64_C(0x3fd0000000000000)}, components_total_5_class0, 2u}, /* 2 cell(s), p = 1/4 ~ 0.25 */
    {{UINT64_C(3), UINT64_C(4), UINT64_C(0x3fe8000000000000)}, components_total_5_class1, 3u}, /* 6 cell(s), p = 3/4 ~ 0.75 */
};
static const uint32_t components_total_5_layouts_limbs[1] = {
    0x00000004u,
};

/* components_total_6: 12x1, 6 mine(s).
 * Two disconnected components {0,2,4} and {6,8,10} (middle mine or both
 * ends) plus unconstrained cells 5 and 11, coupled only by the total:
 * Z = sum over kA,kB in {1,2} of C(2, total - kA - kB).
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1???1?1?? */
static const FixtureClueRun components_total_6_clues[4] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {9u, 1u, 1u, 1u},
};
static const FixtureRun components_total_6_class0[2] = {
    {2u, 1u, 1u}, {8u, 1u, 1u},
};
static const FixtureRun components_total_6_class1[3] = {
    {0u, 1u, 1u}, {4u, 1u, 3u}, {10u, 1u, 2u},
};
static const FixtureOddsClass components_total_6_classes[2] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, components_total_6_class0, 2u}, /* 2 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, components_total_6_class1, 3u}, /* 6 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t components_total_6_layouts_limbs[1] = {
    0x00000001u,
};

/* pool_2d_6x3: 6x3, 5 mine(s).
 * One clue: exactly one mine among 1, 6, 7 (1/3 each); the 14-cell pool
 * holds the other 4 mines (4/14 = 2/7). Z = 3*C(14,4) = 3003.
 * Observation ('?' hidden, digits revealed clues):
 *   1?????
 *   ??????
 *   ?????? */
static const FixtureClueRun pool_2d_6x3_clues[1] = {
    {0u, 1u, 1u, 1u},
};
static const FixtureRun pool_2d_6x3_class0[2] = {
    {2u, 1u, 4u}, {8u, 1u, 10u},
};
static const FixtureRun pool_2d_6x3_class1[2] = {
    {1u, 1u, 1u}, {6u, 1u, 2u},
};
static const FixtureOddsClass pool_2d_6x3_classes[2] = {
    {{UINT64_C(2), UINT64_C(7), UINT64_C(0x3fd2492492492492)}, pool_2d_6x3_class0, 2u}, /* 14 cell(s), p = 2/7 ~ 0.2857142857142857 */
    {{UINT64_C(1), UINT64_C(3), UINT64_C(0x3fd5555555555555)}, pool_2d_6x3_class1, 2u}, /* 3 cell(s), p = 1/3 ~ 0.33333333333333331 */
};
static const uint32_t pool_2d_6x3_layouts_limbs[1] = {
    0x00000bbbu,
};

/* forced_cells_4x1: 4x1, 1 mine(s).
 * Cell 1 is forced mined by the 1 at cell 0, cell 2 forced safe by the 0.
 * Observation ('?' hidden, digits revealed clues):
 *   1??0 */
static const FixtureClueRun forced_cells_4x1_clues[2] = {
    {0u, 1u, 1u, 1u}, {3u, 1u, 1u, 0u},
};
static const FixtureRun forced_cells_4x1_class0[1] = {
    {2u, 1u, 1u},
};
static const FixtureRun forced_cells_4x1_class1[1] = {
    {1u, 1u, 1u},
};
static const FixtureOddsClass forced_cells_4x1_classes[2] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, forced_cells_4x1_class0, 1u}, /* 1 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, forced_cells_4x1_class1, 1u}, /* 1 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t forced_cells_4x1_layouts_limbs[1] = {
    0x00000001u,
};

/* overlap_121_wall: 5x3, 2 mine(s).
 * A 1-2-1 style wall: row 0 hidden, rows 1-2 revealed; layout {1,3}.
 * Observation ('?' hidden, digits revealed clues):
 *   ?????
 *   11211
 *   00000 */
static const FixtureClueRun overlap_121_wall_clues[4] = {
    {5u, 1u, 2u, 1u}, {7u, 1u, 1u, 2u}, {8u, 1u, 2u, 1u}, {10u, 1u, 5u, 0u},
};
static const FixtureRun overlap_121_wall_class0[1] = {
    {0u, 2u, 3u},
};
static const FixtureRun overlap_121_wall_class1[2] = {
    {1u, 1u, 1u}, {3u, 1u, 1u},
};
static const FixtureOddsClass overlap_121_wall_classes[2] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, overlap_121_wall_class0, 1u}, /* 3 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, overlap_121_wall_class1, 2u}, /* 2 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t overlap_121_wall_layouts_limbs[1] = {
    0x00000001u,
};

/* prior_5x4_7: 5x4, 7 mine(s).
 * No clues: uniform prior 7/20, Z = C(20,7).
 * Observation ('?' hidden, digits revealed clues):
 *   ?????
 *   ?????
 *   ?????
 *   ????? */
static const FixtureRun prior_5x4_7_class0[1] = {
    {0u, 1u, 20u},
};
static const FixtureOddsClass prior_5x4_7_classes[1] = {
    {{UINT64_C(7), UINT64_C(20), UINT64_C(0x3fd6666666666666)}, prior_5x4_7_class0, 1u}, /* 20 cell(s), p = 7/20 ~ 0.34999999999999998 */
};
static const uint32_t prior_5x4_7_layouts_limbs[1] = {
    0x00012ed0u,
};

/* prior_3x3_none: 3x3, 0 mine(s).
 * No mines: every cell proven safe.
 * Observation ('?' hidden, digits revealed clues):
 *   ???
 *   ???
 *   ??? */
static const FixtureRun prior_3x3_none_class0[1] = {
    {0u, 1u, 9u},
};
static const FixtureOddsClass prior_3x3_none_classes[1] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, prior_3x3_none_class0, 1u}, /* 9 cell(s), p = 0/1 ~ 0 */
};
static const uint32_t prior_3x3_none_layouts_limbs[1] = {
    0x00000001u,
};

/* prior_3x3_all: 3x3, 9 mine(s).
 * Every cell a mine: every cell proven mined.
 * Observation ('?' hidden, digits revealed clues):
 *   ???
 *   ???
 *   ??? */
static const FixtureRun prior_3x3_all_class0[1] = {
    {0u, 1u, 9u},
};
static const FixtureOddsClass prior_3x3_all_classes[1] = {
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, prior_3x3_all_class0, 1u}, /* 9 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t prior_3x3_all_layouts_limbs[1] = {
    0x00000001u,
};

/* all_revealed_2x2: 2x2, 0 mine(s).
 * No hidden cells: every probability is null, no proofs, Z = 1.
 * Observation ('?' hidden, digits revealed clues):
 *   00
 *   00 */
static const FixtureClueRun all_revealed_2x2_clues[1] = {
    {0u, 1u, 4u, 0u},
};
static const uint32_t all_revealed_2x2_layouts_limbs[1] = {
    0x00000001u,
};

/* chain_3001_exact: 3001x1, 751 mine(s).
 * Parametric 1-row chain: clue 1 on every odd cell, 1501 hidden even cells
 * with x_i + x_(i+2) = 1. The two alternating solutions hold 751 and 750
 * mines; with 751 mines only 0,4,...,3000 mined survives (Z = 1). Must be
 * exact without recursion (one component, 1500 clues). */
static const FixtureClueRun chain_3001_exact_clues[1] = {
    {1u, 2u, 1500u, 1u},
};
static const FixtureRun chain_3001_exact_class0[1] = {
    {2u, 4u, 750u},
};
static const FixtureRun chain_3001_exact_class1[1] = {
    {0u, 4u, 751u},
};
static const FixtureOddsClass chain_3001_exact_classes[2] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, chain_3001_exact_class0, 1u}, /* 750 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, chain_3001_exact_class1, 1u}, /* 751 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t chain_3001_exact_layouts_limbs[1] = {
    0x00000001u,
};

/* prior_80x80_half: 80x80, 3200 mine(s).
 * Empty 80x80 prior: Z = C(6400,3200) (6394 bits); every cell exactly 1/2. */
static const FixtureRun prior_80x80_half_class0[1] = {
    {0u, 1u, 6400u},
};
static const FixtureOddsClass prior_80x80_half_classes[1] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, prior_80x80_half_class0, 1u}, /* 6400 cell(s), p = 1/2 ~ 0.5 */
};
static const uint32_t prior_80x80_half_layouts_limbs[200] = {
    0x5a209468u, 0x0e697006u, 0xd9a47633u, 0x7bf25d7du, 0x7e007419u, 0xf3899541u,
    0xbb39c42eu, 0xa8e1e85cu, 0xa197ccebu, 0xdea2aafeu, 0x808b66dbu, 0x7dd30214u,
    0xc05f9a91u, 0x194515d6u, 0xea58b26fu, 0x244827dfu, 0x9bf391aau, 0x751757efu,
    0x3d0a9ab5u, 0x73432618u, 0xf2f15cbbu, 0xda90a630u, 0x8d544f72u, 0x6f840f83u,
    0xcf5c9325u, 0x990b388cu, 0x5d16ff90u, 0x2b64b61cu, 0xb62b31c8u, 0xb48ce61au,
    0x2f6b0ec9u, 0x9ce581e5u, 0xfb52c539u, 0xaa631db9u, 0x6bbe0181u, 0xda91536au,
    0x8ff10d5fu, 0x4ce55edau, 0xc3f7550du, 0xa0fb59b8u, 0xe8a412b4u, 0xe3af5ad8u,
    0x5f76d89fu, 0x8c732c37u, 0xba9acda3u, 0xdec0ae9bu, 0x68f28fdcu, 0xe5a14defu,
    0x0d33ed0fu, 0xb5ff4489u, 0xf5b8d914u, 0x923c8350u, 0x2cf32c50u, 0x1bc9f990u,
    0xb27db169u, 0x7b6cdfe0u, 0x633d50b4u, 0xa1ac60a2u, 0x43ced45au, 0xa03f0518u,
    0x958c72e6u, 0x7c55b03du, 0x26da5c2bu, 0x87758acbu, 0xce7fd960u, 0x1255cf6du,
    0x9e1ed77eu, 0x184ad264u, 0xcc50cddau, 0xd529b204u, 0x1b7bfc44u, 0x72c967fdu,
    0x3640c7ebu, 0x871bb513u, 0x273fa82eu, 0x07b3a9f1u, 0x9c20291bu, 0xb096c604u,
    0x20738bcau, 0xf4825618u, 0xf8dab5d3u, 0x5a6cbcf3u, 0x868d7b92u, 0x5a76fa1du,
    0xfd44bf24u, 0xf5b8dd3du, 0xd847ec69u, 0x4bb7d58bu, 0xe5deddf8u, 0x3ac46a68u,
    0x6e043b41u, 0xe924a22eu, 0x1202cadbu, 0x4210c802u, 0xfa406abcu, 0xd806903eu,
    0xbacc1fc8u, 0xbd5e597du, 0x340cc0f6u, 0x1635de38u, 0xe328f86du, 0x55c12e78u,
    0xc4690feau, 0xa32dcc3au, 0xf12348dfu, 0x624afba8u, 0xc14c30dbu, 0xf8136513u,
    0xd52d2f5bu, 0xf8bfa011u, 0xa8725c73u, 0xd8ae8d5bu, 0xd252f7e5u, 0x5f82273fu,
    0xec6f4ac7u, 0x2d78196du, 0xdaa65931u, 0x7ca02911u, 0x148ecd3au, 0x13168758u,
    0xdf830e92u, 0xd1afbbecu, 0x41ca6716u, 0xd0f60c6bu, 0xbbdc1969u, 0x15c9b5a9u,
    0x0859f3e4u, 0x56f8356du, 0xd3fe5c2bu, 0xe405aca2u, 0xf1465087u, 0xddef678eu,
    0xad7ac0dbu, 0xb445bdf9u, 0x10c9d417u, 0x0b5d755bu, 0x4084ed10u, 0xd24a8c93u,
    0xcca4f729u, 0x03115690u, 0x9bb89073u, 0xaf5ea018u, 0xdb5c55e5u, 0x80a2bd72u,
    0xfac32ef8u, 0x58be6d66u, 0x33fb4c22u, 0xceb0146fu, 0x366b4dc8u, 0xc4711f29u,
    0xa032e6f3u, 0xed190e01u, 0xaeccef57u, 0x2cc30560u, 0x524b307du, 0x85da7a45u,
    0x81982889u, 0x40b56c98u, 0xcae7c989u, 0x62b1d031u, 0x5f1d8128u, 0x733ec3d7u,
    0xee2ecb7au, 0x3c5c6e01u, 0xe89dc979u, 0x15e5111du, 0xf868de5fu, 0xd6f02adeu,
    0xb7c1e038u, 0x6892bffbu, 0x7fce987bu, 0x6fe90751u, 0xe95675ecu, 0x85a1febcu,
    0x2c784491u, 0x815dd689u, 0x94ab5877u, 0x8fd5b539u, 0xcf54160au, 0xc87858dcu,
    0x6fb4985fu, 0xf213bbbfu, 0xcef9a0ddu, 0xc0fcc031u, 0xbe857515u, 0x4e71255bu,
    0x413ef65cu, 0xe92f4ab8u, 0x203fc824u, 0xf665b9bfu, 0xbfcc0590u, 0x7f440671u,
    0xf533bd95u, 0x8b813b38u, 0x55e6a3b9u, 0x784807fbu, 0x90962a28u, 0xbaad2c75u,
    0xee2d6d36u, 0x028d99fbu,
};

/* corner_80x80_2000: 80x80, 2000 mine(s).
 * One corner clue 1: frontier 1, 80, 81 hold one mine (1/3 each); the
 * 6396-cell pool holds 1999 (1999/6396). Z = 3*C(6396,1999); the huge
 * binomials must cancel exactly. */
static const FixtureClueRun corner_80x80_2000_clues[1] = {
    {0u, 1u, 1u, 1u},
};
static const FixtureRun corner_80x80_2000_class0[2] = {
    {2u, 1u, 78u}, {82u, 1u, 6318u},
};
static const FixtureRun corner_80x80_2000_class1[2] = {
    {1u, 1u, 1u}, {80u, 1u, 2u},
};
static const FixtureOddsClass corner_80x80_2000_classes[2] = {
    {{UINT64_C(1999), UINT64_C(6396), UINT64_C(0x3fd400a3f14552c6)}, corner_80x80_2000_class0, 2u}, /* 6396 cell(s), p = 1999/6396 ~ 0.31253908692933086 */
    {{UINT64_C(1), UINT64_C(3), UINT64_C(0x3fd5555555555555)}, corner_80x80_2000_class1, 2u}, /* 3 cell(s), p = 1/3 ~ 0.33333333333333331 */
};
static const uint32_t corner_80x80_2000_layouts_limbs[179] = {
    0x9545f980u, 0xd115a134u, 0x7e0d51ddu, 0x8580e5bau, 0xb82c1435u, 0xec2726c3u,
    0xf7e3dd06u, 0xc4d8977bu, 0x1a93d696u, 0x4b14a104u, 0xd8841850u, 0x04187fb4u,
    0x52c6baa7u, 0xd00a6495u, 0xf55acd7cu, 0xb52b51dau, 0xe08d12c0u, 0x049b41e3u,
    0xbbd3ad39u, 0x97f13e0au, 0x74abace1u, 0x56595e87u, 0xd7d5848cu, 0xb933da23u,
    0xda06c2cfu, 0xba3ee528u, 0xa0eeaf5au, 0xbad92ff9u, 0x07a61c2eu, 0x61991754u,
    0x85a3802cu, 0x26fd1c39u, 0xafefa8f1u, 0xa9f96a20u, 0xb19c478cu, 0xdd34ad65u,
    0xed484a56u, 0xcb4d8313u, 0x49622f65u, 0xd0d44639u, 0x1b007a69u, 0x906ffa4au,
    0xa02d1783u, 0xb521e541u, 0xb3bb02ecu, 0x97c3090au, 0x210e58ccu, 0x0861f9fbu,
    0xfa0f676cu, 0x3cb762b4u, 0xedb5ec04u, 0x6aa50d56u, 0x3ef5a132u, 0x7775eb5fu,
    0x5607d7d6u, 0xd6be3a00u, 0x0538be03u, 0xe5a09414u, 0x52349572u, 0x4225c25eu,
    0x9e2adaa4u, 0xc1f26847u, 0xb9a36a88u, 0x1e1e521au, 0x11f53c0au, 0xceec0810u,
    0x7a1f8a9du, 0xce080389u, 0xf501ca36u, 0xd85bedceu, 0xa247264au, 0x4e08a046u,
    0x542f19c5u, 0xecc40a5au, 0x59fe82eeu, 0x9c195185u, 0x44bfe88eu, 0x701b74e4u,
    0x5a49d061u, 0xe0a8cf74u, 0xec080b34u, 0x335702efu, 0xf0d157dcu, 0xd17e0ea3u,
    0x4db2e973u, 0x320619eeu, 0x5fb7adc3u, 0xbe4307e2u, 0xe6cb6e42u, 0x2cafc939u,
    0x9d4d63e4u, 0xc5562f8bu, 0x721a482cu, 0x409ab11eu, 0xccc1412eu, 0x0cbd60beu,
    0x99fb8964u, 0xcc003219u, 0xa002f511u, 0x531b1433u, 0x23cf7273u, 0x8084f676u,
    0x37f87b97u, 0x0e93aa4bu, 0xb07dfb46u, 0xb428e03fu, 0xca0014f4u, 0x17d1b106u,
    0xcc8ced51u, 0x1c73dad4u, 0x8b9a23d8u, 0x195b68e0u, 0x7b914134u, 0x0e9d7162u,
    0x211cc2d2u, 0xfa0037d2u, 0xf314f4cdu, 0x91a36a42u, 0x9efa6ac1u, 0xbc4509d0u,
    0x935ef628u, 0xb57c7610u, 0x0ba71212u, 0x69d42899u, 0xaefaf2c4u, 0xf8dfd790u,
    0x73b09f0cu, 0x0ddd8cc7u, 0xc49fe5e5u, 0xe97c151fu, 0x1faed4a5u, 0x86f8ffcbu,
    0x8e850510u, 0xe5a49645u, 0x9975ddebu, 0x3d83b61du, 0x6c9cdba7u, 0x478f8ce9u,
    0xabe88b97u, 0xcaa38125u, 0x2025131cu, 0xafc76018u, 0x77e18142u, 0x3ae9b4f3u,
    0x5670bd98u, 0xa854b53bu, 0x128723fdu, 0x6abd8c15u, 0xf30ccebfu, 0x6b79cedbu,
    0x9f368fb0u, 0x1c518e79u, 0x4dc13a2bu, 0x35415587u, 0xc7555580u, 0x564cf2e6u,
    0xe18cbacbu, 0x1d5d1fb4u, 0x49bf3aabu, 0x1d69047bu, 0x83f9e1cfu, 0xe16dd8edu,
    0x6bf6c018u, 0x47558c60u, 0xf8e5aa5fu, 0x19887cddu, 0x999e47d1u, 0x24bec7a4u,
    0x17cbcd3au, 0x2e647e0au, 0x87c9ffb0u, 0xeac3b312u, 0xb09dca5eu, 0x055f4451u,
    0x303ea976u, 0x0cda2c01u, 0xa2c7d426u, 0xe9bfa3d2u, 0x53f88e64u,
};

/* near_certain_80x80: 80x80, 4 mine(s).
 * Cell y = 3240 (row 40, col 40) is ringed by eight revealed 1s; 4 mines.
 * Either y is the mine (the 6375-cell pool keeps 3: C(6375,3) layouts) or
 * one of two 4-cell pinwheels holds all four (pool empty: 1 layout each).
 * Z = C(6375,3) + 2. Pinwheel cells have p = 1/Z ~ 2.3e-11 and y has
 * p = 1 - 2/Z: extreme yet NOT proven. The 8 other frontier cells are
 * impossible and must be proven safe from integer zeros. */
static const FixtureClueRun near_certain_80x80_clues[4] = {
    {3159u, 1u, 3u, 1u}, {3239u, 1u, 1u, 1u}, {3241u, 1u, 1u, 1u}, {3319u, 1u, 3u, 1u},
};
static const FixtureRun near_certain_80x80_class0[4] = {
    {3078u, 2u, 3u}, {3238u, 1u, 1u}, {3242u, 1u, 1u}, {3398u, 2u, 3u},
};
static const FixtureRun near_certain_80x80_class1[8] = {
    {3079u, 1u, 1u}, {3081u, 1u, 1u}, {3158u, 1u, 1u}, {3162u, 1u, 1u}, {3318u, 1u, 1u}, {3322u, 1u, 1u},
    {3399u, 1u, 1u}, {3401u, 1u, 1u},
};
static const FixtureRun near_certain_80x80_class2[6] = {
    {0u, 1u, 3078u}, {3083u, 1u, 75u}, {3163u, 1u, 75u}, {3243u, 1u, 75u}, {3323u, 1u, 75u}, {3403u, 1u, 2997u},
};
static const FixtureRun near_certain_80x80_class3[1] = {
    {3240u, 1u, 1u},
};
static const FixtureOddsClass near_certain_80x80_classes[4] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, near_certain_80x80_class0, 4u}, /* 8 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(43160345877), UINT64_C(0x3db9799c5bbad969)}, near_certain_80x80_class1, 8u}, /* 8 cell(s), p = 1/43160345877 ~ 2.316941580704284e-11 */
    {{UINT64_C(20310751), UINT64_C(43160345877), UINT64_C(0x3f3ed7291493951e)}, near_certain_80x80_class2, 6u}, /* 6375 cell(s), p = 20310751/43160345877 ~ 0.00047058823527231116 */
    {{UINT64_C(43160345875), UINT64_C(43160345877), UINT64_C(0x3feffffffff9a199)}, near_certain_80x80_class3, 1u}, /* 1 cell(s), p = 43160345875/43160345877 ~ 0.99999999995366118 */
};
static const uint32_t near_certain_80x80_layouts_limbs[2] = {
    0x0c8e9d15u, 0x0000000au,
};

/* random_seed_0: 6x2, 4 mine(s).
 * Frozen tests/test_probability.py random_observation(0); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ??22?1
 *   2??2?1 */
static const FixtureClueRun random_seed_0_clues[5] = {
    {2u, 1u, 2u, 2u}, {5u, 1u, 1u, 1u}, {6u, 1u, 1u, 2u}, {9u, 1u, 1u, 2u}, {11u, 1u, 1u, 1u},
};
static const FixtureRun random_seed_0_class0[1] = {
    {1u, 3u, 4u},
};
static const FixtureRun random_seed_0_class1[2] = {
    {0u, 1u, 1u}, {8u, 1u, 1u},
};
static const FixtureOddsClass random_seed_0_classes[2] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, random_seed_0_class0, 1u}, /* 4 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, random_seed_0_class1, 2u}, /* 2 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t random_seed_0_layouts_limbs[1] = {
    0x00000004u,
};

/* random_seed_5: 1x9, 3 mine(s).
 * Frozen tests/test_probability.py random_observation(5); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ?
 *   1
 *   0
 *   ?
 *   ?
 *   ?
 *   ?
 *   ?
 *   0 */
static const FixtureClueRun random_seed_5_clues[3] = {
    {1u, 1u, 1u, 1u}, {2u, 1u, 1u, 0u}, {8u, 1u, 1u, 0u},
};
static const FixtureRun random_seed_5_class0[2] = {
    {3u, 1u, 1u}, {7u, 1u, 1u},
};
static const FixtureRun random_seed_5_class1[1] = {
    {4u, 1u, 3u},
};
static const FixtureRun random_seed_5_class2[1] = {
    {0u, 1u, 1u},
};
static const FixtureOddsClass random_seed_5_classes[3] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_5_class0, 2u}, /* 2 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(2), UINT64_C(3), UINT64_C(0x3fe5555555555555)}, random_seed_5_class1, 1u}, /* 3 cell(s), p = 2/3 ~ 0.66666666666666663 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, random_seed_5_class2, 1u}, /* 1 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t random_seed_5_layouts_limbs[1] = {
    0x00000003u,
};

/* random_seed_11: 5x4, 5 mine(s).
 * Frozen tests/test_probability.py random_observation(11); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   22100
 *   ??111
 *   3322?
 *   ??12? */
static const FixtureClueRun random_seed_11_clues[8] = {
    {0u, 1u, 2u, 2u}, {2u, 1u, 1u, 1u}, {3u, 1u, 2u, 0u}, {7u, 1u, 3u, 1u}, {10u, 1u, 2u, 3u},
    {12u, 1u, 2u, 2u}, {17u, 1u, 1u, 1u}, {18u, 1u, 1u, 2u},
};
static const FixtureRun random_seed_11_class0[1] = {
    {15u, 1u, 1u},
};
static const FixtureRun random_seed_11_class1[4] = {
    {5u, 1u, 2u}, {14u, 1u, 1u}, {16u, 1u, 1u}, {19u, 1u, 1u},
};
static const FixtureOddsClass random_seed_11_classes[2] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_11_class0, 1u}, /* 1 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, random_seed_11_class1, 4u}, /* 5 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t random_seed_11_layouts_limbs[1] = {
    0x00000001u,
};

/* random_seed_21: 4x4, 4 mine(s).
 * Frozen tests/test_probability.py random_observation(21); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ??11
 *   ????
 *   ?3?2
 *   ??2? */
static const FixtureClueRun random_seed_21_clues[4] = {
    {2u, 1u, 2u, 1u}, {9u, 1u, 1u, 3u}, {11u, 1u, 1u, 2u}, {14u, 1u, 1u, 2u},
};
static const FixtureRun random_seed_21_class0[2] = {
    {1u, 1u, 1u}, {5u, 1u, 1u},
};
static const FixtureRun random_seed_21_class1[1] = {
    {0u, 1u, 1u},
};
static const FixtureRun random_seed_21_class2[1] = {
    {4u, 4u, 3u},
};
static const FixtureRun random_seed_21_class3[2] = {
    {7u, 1u, 1u}, {15u, 1u, 1u},
};
static const FixtureRun random_seed_21_class4[2] = {
    {6u, 1u, 1u}, {10u, 1u, 1u},
};
static const FixtureRun random_seed_21_class5[1] = {
    {13u, 1u, 1u},
};
static const FixtureOddsClass random_seed_21_classes[6] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_21_class0, 2u}, /* 2 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(7), UINT64_C(0x3fc2492492492492)}, random_seed_21_class1, 1u}, /* 1 cell(s), p = 1/7 ~ 0.14285714285714285 */
    {{UINT64_C(2), UINT64_C(7), UINT64_C(0x3fd2492492492492)}, random_seed_21_class2, 1u}, /* 3 cell(s), p = 2/7 ~ 0.2857142857142857 */
    {{UINT64_C(3), UINT64_C(7), UINT64_C(0x3fdb6db6db6db6db)}, random_seed_21_class3, 2u}, /* 2 cell(s), p = 3/7 ~ 0.42857142857142855 */
    {{UINT64_C(4), UINT64_C(7), UINT64_C(0x3fe2492492492492)}, random_seed_21_class4, 2u}, /* 2 cell(s), p = 4/7 ~ 0.5714285714285714 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, random_seed_21_class5, 1u}, /* 1 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t random_seed_21_layouts_limbs[1] = {
    0x00000007u,
};

/* random_seed_39: 5x3, 3 mine(s).
 * Frozen tests/test_probability.py random_observation(39); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ?2???
 *   ??2?1
 *   ??1?? */
static const FixtureClueRun random_seed_39_clues[4] = {
    {1u, 1u, 1u, 2u}, {7u, 1u, 1u, 2u}, {9u, 1u, 1u, 1u}, {12u, 1u, 1u, 1u},
};
static const FixtureRun random_seed_39_class0[1] = {
    {10u, 1u, 2u},
};
static const FixtureRun random_seed_39_class1[2] = {
    {4u, 1u, 1u}, {14u, 1u, 1u},
};
static const FixtureRun random_seed_39_class2[1] = {
    {3u, 5u, 3u},
};
static const FixtureRun random_seed_39_class3[2] = {
    {0u, 1u, 1u}, {5u, 1u, 1u},
};
static const FixtureRun random_seed_39_class4[1] = {
    {6u, 1u, 1u},
};
static const FixtureRun random_seed_39_class5[1] = {
    {2u, 1u, 1u},
};
static const FixtureOddsClass random_seed_39_classes[6] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_39_class0, 1u}, /* 2 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(8), UINT64_C(0x3fc0000000000000)}, random_seed_39_class1, 2u}, /* 2 cell(s), p = 1/8 ~ 0.125 */
    {{UINT64_C(1), UINT64_C(4), UINT64_C(0x3fd0000000000000)}, random_seed_39_class2, 1u}, /* 3 cell(s), p = 1/4 ~ 0.25 */
    {{UINT64_C(3), UINT64_C(8), UINT64_C(0x3fd8000000000000)}, random_seed_39_class3, 2u}, /* 2 cell(s), p = 3/8 ~ 0.375 */
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, random_seed_39_class4, 1u}, /* 1 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(3), UINT64_C(4), UINT64_C(0x3fe8000000000000)}, random_seed_39_class5, 1u}, /* 1 cell(s), p = 3/4 ~ 0.75 */
};
static const uint32_t random_seed_39_layouts_limbs[1] = {
    0x00000008u,
};

/* random_seed_54: 4x4, 4 mine(s).
 * Frozen tests/test_probability.py random_observation(54); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ???2
 *   ?2??
 *   1?4?
 *   1??? */
static const FixtureClueRun random_seed_54_clues[5] = {
    {3u, 1u, 1u, 2u}, {5u, 1u, 1u, 2u}, {8u, 1u, 1u, 1u}, {10u, 1u, 1u, 4u}, {12u, 1u, 1u, 1u},
};
static const FixtureRun random_seed_54_class0[3] = {
    {0u, 1u, 3u}, {4u, 1u, 1u}, {13u, 1u, 1u},
};
static const FixtureRun random_seed_54_class1[2] = {
    {11u, 1u, 1u}, {14u, 1u, 2u},
};
static const FixtureRun random_seed_54_class2[2] = {
    {6u, 1u, 2u}, {9u, 1u, 1u},
};
static const FixtureOddsClass random_seed_54_classes[3] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_54_class0, 3u}, /* 5 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(3), UINT64_C(0x3fd5555555555555)}, random_seed_54_class1, 2u}, /* 3 cell(s), p = 1/3 ~ 0.33333333333333331 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, random_seed_54_class2, 2u}, /* 3 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t random_seed_54_layouts_limbs[1] = {
    0x00000003u,
};

/* random_seed_74: 4x3, 3 mine(s).
 * Frozen tests/test_probability.py random_observation(74); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ???1
 *   ??2?
 *   1??? */
static const FixtureClueRun random_seed_74_clues[3] = {
    {3u, 1u, 1u, 1u}, {6u, 1u, 1u, 2u}, {8u, 1u, 1u, 1u},
};
static const FixtureRun random_seed_74_class0[2] = {
    {1u, 4u, 3u}, {10u, 1u, 2u},
};
static const FixtureRun random_seed_74_class1[1] = {
    {0u, 1u, 1u},
};
static const FixtureRun random_seed_74_class2[2] = {
    {2u, 1u, 1u}, {7u, 1u, 1u},
};
static const FixtureRun random_seed_74_class3[1] = {
    {4u, 1u, 1u},
};
static const FixtureOddsClass random_seed_74_classes[4] = {
    {{UINT64_C(1), UINT64_C(5), UINT64_C(0x3fc999999999999a)}, random_seed_74_class0, 2u}, /* 5 cell(s), p = 1/5 ~ 0.20000000000000001 */
    {{UINT64_C(2), UINT64_C(5), UINT64_C(0x3fd999999999999a)}, random_seed_74_class1, 1u}, /* 1 cell(s), p = 2/5 ~ 0.40000000000000002 */
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, random_seed_74_class2, 2u}, /* 2 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(3), UINT64_C(5), UINT64_C(0x3fe3333333333333)}, random_seed_74_class3, 1u}, /* 1 cell(s), p = 3/5 ~ 0.59999999999999998 */
};
static const uint32_t random_seed_74_layouts_limbs[1] = {
    0x0000000au,
};

/* random_seed_82: 4x4, 4 mine(s).
 * Frozen tests/test_probability.py random_observation(82); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   0??1
 *   ?2??
 *   ????
 *   ??2? */
static const FixtureClueRun random_seed_82_clues[4] = {
    {0u, 1u, 1u, 0u}, {3u, 1u, 1u, 1u}, {5u, 1u, 1u, 2u}, {14u, 1u, 1u, 2u},
};
static const FixtureRun random_seed_82_class0[2] = {
    {1u, 1u, 1u}, {4u, 1u, 1u},
};
static const FixtureRun random_seed_82_class1[1] = {
    {7u, 1u, 1u},
};
static const FixtureRun random_seed_82_class2[2] = {
    {2u, 1u, 1u}, {6u, 1u, 1u},
};
static const FixtureRun random_seed_82_class3[3] = {
    {9u, 1u, 3u}, {13u, 1u, 1u}, {15u, 1u, 1u},
};
static const FixtureRun random_seed_82_class4[1] = {
    {8u, 1u, 1u},
};
static const FixtureRun random_seed_82_class5[1] = {
    {12u, 1u, 1u},
};
static const FixtureOddsClass random_seed_82_classes[6] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_82_class0, 2u}, /* 2 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(7), UINT64_C(25), UINT64_C(0x3fd1eb851eb851ec)}, random_seed_82_class1, 1u}, /* 1 cell(s), p = 7/25 ~ 0.28000000000000003 */
    {{UINT64_C(9), UINT64_C(25), UINT64_C(0x3fd70a3d70a3d70a)}, random_seed_82_class2, 2u}, /* 2 cell(s), p = 9/25 ~ 0.35999999999999999 */
    {{UINT64_C(2), UINT64_C(5), UINT64_C(0x3fd999999999999a)}, random_seed_82_class3, 3u}, /* 5 cell(s), p = 2/5 ~ 0.40000000000000002 */
    {{UINT64_C(12), UINT64_C(25), UINT64_C(0x3fdeb851eb851eb8)}, random_seed_82_class4, 1u}, /* 1 cell(s), p = 12/25 ~ 0.47999999999999998 */
    {{UINT64_C(13), UINT64_C(25), UINT64_C(0x3fe0a3d70a3d70a4)}, random_seed_82_class5, 1u}, /* 1 cell(s), p = 13/25 ~ 0.52000000000000002 */
};
static const uint32_t random_seed_82_layouts_limbs[1] = {
    0x00000019u,
};

/* random_seed_93: 5x4, 5 mine(s).
 * Frozen tests/test_probability.py random_observation(93); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   01???
 *   1???2
 *   ??11?
 *   ?11?? */
static const FixtureClueRun random_seed_93_clues[6] = {
    {0u, 1u, 1u, 0u}, {1u, 1u, 1u, 1u}, {5u, 1u, 1u, 1u}, {9u, 1u, 1u, 2u}, {12u, 1u, 2u, 1u},
    {16u, 1u, 2u, 1u},
};
static const FixtureRun random_seed_93_class0[2] = {
    {6u, 1u, 3u}, {14u, 1u, 2u},
};
static const FixtureRun random_seed_93_class1[2] = {
    {10u, 1u, 2u}, {18u, 1u, 2u},
};
static const FixtureRun random_seed_93_class2[1] = {
    {2u, 1u, 3u},
};
static const FixtureOddsClass random_seed_93_classes[3] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_93_class0, 2u}, /* 5 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, random_seed_93_class1, 2u}, /* 4 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, random_seed_93_class2, 1u}, /* 3 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t random_seed_93_layouts_limbs[1] = {
    0x00000002u,
};

/* random_seed_126: 3x3, 3 mine(s).
 * Frozen tests/test_probability.py random_observation(126); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ???
 *   ?3?
 *   ??? */
static const FixtureClueRun random_seed_126_clues[1] = {
    {4u, 1u, 1u, 3u},
};
static const FixtureRun random_seed_126_class0[2] = {
    {0u, 1u, 4u}, {5u, 1u, 4u},
};
static const FixtureOddsClass random_seed_126_classes[1] = {
    {{UINT64_C(3), UINT64_C(8), UINT64_C(0x3fd8000000000000)}, random_seed_126_class0, 2u}, /* 8 cell(s), p = 3/8 ~ 0.375 */
};
static const uint32_t random_seed_126_layouts_limbs[1] = {
    0x00000038u,
};

/* random_seed_128: 5x3, 4 mine(s).
 * Frozen tests/test_probability.py random_observation(128); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ???3?
 *   ?????
 *   ????? */
static const FixtureClueRun random_seed_128_clues[1] = {
    {3u, 1u, 1u, 3u},
};
static const FixtureRun random_seed_128_class0[3] = {
    {0u, 1u, 2u}, {5u, 1u, 2u}, {10u, 1u, 5u},
};
static const FixtureRun random_seed_128_class1[3] = {
    {2u, 1u, 1u}, {4u, 1u, 1u}, {7u, 1u, 3u},
};
static const FixtureOddsClass random_seed_128_classes[2] = {
    {{UINT64_C(1), UINT64_C(9), UINT64_C(0x3fbc71c71c71c71c)}, random_seed_128_class0, 3u}, /* 9 cell(s), p = 1/9 ~ 0.1111111111111111 */
    {{UINT64_C(3), UINT64_C(5), UINT64_C(0x3fe3333333333333)}, random_seed_128_class1, 3u}, /* 5 cell(s), p = 3/5 ~ 0.59999999999999998 */
};
static const uint32_t random_seed_128_layouts_limbs[1] = {
    0x0000005au,
};

/* random_seed_131: 2x6, 3 mine(s).
 * Frozen tests/test_probability.py random_observation(131); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   1?
 *   ??
 *   ??
 *   ??
 *   ?2
 *   11 */
static const FixtureClueRun random_seed_131_clues[3] = {
    {0u, 1u, 1u, 1u}, {9u, 1u, 1u, 2u}, {10u, 1u, 2u, 1u},
};
static const FixtureRun random_seed_131_class0[1] = {
    {4u, 1u, 2u},
};
static const FixtureRun random_seed_131_class1[1] = {
    {1u, 1u, 3u},
};
static const FixtureRun random_seed_131_class2[1] = {
    {6u, 1u, 2u},
};
static const FixtureRun random_seed_131_class3[1] = {
    {8u, 1u, 1u},
};
static const FixtureOddsClass random_seed_131_classes[4] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_131_class0, 1u}, /* 2 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(3), UINT64_C(0x3fd5555555555555)}, random_seed_131_class1, 1u}, /* 3 cell(s), p = 1/3 ~ 0.33333333333333331 */
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, random_seed_131_class2, 1u}, /* 2 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, random_seed_131_class3, 1u}, /* 1 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t random_seed_131_layouts_limbs[1] = {
    0x00000006u,
};

/* random_seed_134: 5x4, 5 mine(s).
 * Frozen tests/test_probability.py random_observation(134); the generating
 * layout was discarded by the baseline. Exact values: whole-board enumeration.
 * Observation ('?' hidden, digits revealed clues):
 *   ?????
 *   2???1
 *   232??
 *   ?2?1? */
static const FixtureClueRun random_seed_134_clues[7] = {
    {5u, 1u, 1u, 2u}, {9u, 1u, 1u, 1u}, {10u, 1u, 1u, 2u}, {11u, 1u, 1u, 3u}, {12u, 1u, 1u, 2u},
    {16u, 1u, 1u, 2u}, {18u, 1u, 1u, 1u},
};
static const FixtureRun random_seed_134_class0[4] = {
    {2u, 1u, 1u}, {7u, 1u, 2u}, {13u, 1u, 2u}, {19u, 1u, 1u},
};
static const FixtureRun random_seed_134_class1[2] = {
    {0u, 1u, 2u}, {3u, 1u, 2u},
};
static const FixtureRun random_seed_134_class2[3] = {
    {6u, 1u, 1u}, {15u, 1u, 1u}, {17u, 1u, 1u},
};
static const FixtureOddsClass random_seed_134_classes[3] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, random_seed_134_class0, 4u}, /* 6 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, random_seed_134_class1, 2u}, /* 4 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, random_seed_134_class2, 3u}, /* 3 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t random_seed_134_layouts_limbs[1] = {
    0x00000004u,
};

/* chain_board_12_total_36: 72x1, 36 mine(s).
 * Twelve 6-cell blocks '?1?1??': each component holds 1 or 2 mines; 36
 * mines on 48 hidden cells force both ends of every block and every free
 * cell (Z = 1): all 48 hidden cells are proven. */
static const FixtureClueRun chain_board_12_total_36_clues[24] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {9u, 1u, 1u, 1u}, {13u, 1u, 1u, 1u},
    {15u, 1u, 1u, 1u}, {19u, 1u, 1u, 1u}, {21u, 1u, 1u, 1u}, {25u, 1u, 1u, 1u}, {27u, 1u, 1u, 1u},
    {31u, 1u, 1u, 1u}, {33u, 1u, 1u, 1u}, {37u, 1u, 1u, 1u}, {39u, 1u, 1u, 1u}, {43u, 1u, 1u, 1u},
    {45u, 1u, 1u, 1u}, {49u, 1u, 1u, 1u}, {51u, 1u, 1u, 1u}, {55u, 1u, 1u, 1u}, {57u, 1u, 1u, 1u},
    {61u, 1u, 1u, 1u}, {63u, 1u, 1u, 1u}, {67u, 1u, 1u, 1u}, {69u, 1u, 1u, 1u},
};
static const FixtureRun chain_board_12_total_36_class0[1] = {
    {2u, 6u, 12u},
};
static const FixtureRun chain_board_12_total_36_class1[13] = {
    {0u, 1u, 1u}, {4u, 1u, 3u}, {10u, 1u, 3u}, {16u, 1u, 3u}, {22u, 1u, 3u}, {28u, 1u, 3u},
    {34u, 1u, 3u}, {40u, 1u, 3u}, {46u, 1u, 3u}, {52u, 1u, 3u}, {58u, 1u, 3u}, {64u, 1u, 3u},
    {70u, 1u, 2u},
};
static const FixtureOddsClass chain_board_12_total_36_classes[2] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, chain_board_12_total_36_class0, 1u}, /* 12 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, chain_board_12_total_36_class1, 13u}, /* 36 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t chain_board_12_total_36_layouts_limbs[1] = {
    0x00000001u,
};

/* mixed_components_18x1: 18x1, 5 mine(s).
 * Component A: one clue over two cells (cheap). Component B: chain 4..14
 * (needs more search nodes). Ground truth for the mixed exact/sampled case.
 * Observation ('?' hidden, digits revealed clues):
 *   ?1???1?1?1?1?1???? */
static const FixtureClueRun mixed_components_18x1_clues[2] = {
    {1u, 1u, 1u, 1u}, {5u, 2u, 5u, 1u},
};
static const FixtureRun mixed_components_18x1_class0[2] = {
    {3u, 1u, 1u}, {15u, 1u, 3u},
};
static const FixtureRun mixed_components_18x1_class1[1] = {
    {0u, 2u, 8u},
};
static const FixtureOddsClass mixed_components_18x1_classes[2] = {
    {{UINT64_C(1), UINT64_C(4), UINT64_C(0x3fd0000000000000)}, mixed_components_18x1_class0, 2u}, /* 4 cell(s), p = 1/4 ~ 0.25 */
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, mixed_components_18x1_class1, 1u}, /* 8 cell(s), p = 1/2 ~ 0.5 */
};
static const uint32_t mixed_components_18x1_layouts_limbs[1] = {
    0x00000010u,
};

/* chain_board_1_total_2: 6x1, 2 mine(s).
 * '?1?1??': {2,5} or {0,4}, both weighted 1 (Z = 2).
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1?? */
static const FixtureClueRun chain_board_1_total_2_clues[2] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u},
};
static const FixtureRun chain_board_1_total_2_class0[2] = {
    {0u, 2u, 3u}, {5u, 1u, 1u},
};
static const FixtureOddsClass chain_board_1_total_2_classes[1] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, chain_board_1_total_2_class0, 2u}, /* 4 cell(s), p = 1/2 ~ 0.5 */
};
static const uint32_t chain_board_1_total_2_layouts_limbs[1] = {
    0x00000002u,
};

/* chain_board_2_total_4: 12x1, 4 mine(s).
 * Same observation as components_total_4 (ground truth for sampling cases).
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1???1?1?? */
static const FixtureClueRun chain_board_2_total_4_clues[4] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {9u, 1u, 1u, 1u},
};
static const FixtureRun chain_board_2_total_4_class0[4] = {
    {0u, 2u, 3u}, {5u, 1u, 2u}, {8u, 1u, 1u}, {10u, 1u, 2u},
};
static const FixtureOddsClass chain_board_2_total_4_classes[1] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, chain_board_2_total_4_class0, 4u}, /* 8 cell(s), p = 1/2 ~ 0.5 */
};
static const uint32_t chain_board_2_total_4_layouts_limbs[1] = {
    0x00000006u,
};

/* chain_board_2_forced_tail: 14x1, 5 mine(s).
 * chain_board(2) plus two cells; the lone clue at 13 forces cell 12 mined.
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?1???1?1???1 */
static const FixtureClueRun chain_board_2_forced_tail_clues[5] = {
    {1u, 1u, 1u, 1u}, {3u, 1u, 1u, 1u}, {7u, 1u, 1u, 1u}, {9u, 1u, 1u, 1u}, {13u, 1u, 1u, 1u},
};
static const FixtureRun chain_board_2_forced_tail_class0[4] = {
    {0u, 2u, 3u}, {5u, 1u, 2u}, {8u, 1u, 1u}, {10u, 1u, 2u},
};
static const FixtureRun chain_board_2_forced_tail_class1[1] = {
    {12u, 1u, 1u},
};
static const FixtureOddsClass chain_board_2_forced_tail_classes[2] = {
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, chain_board_2_forced_tail_class0, 4u}, /* 8 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, chain_board_2_forced_tail_class1, 1u}, /* 1 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t chain_board_2_forced_tail_layouts_limbs[1] = {
    0x00000006u,
};

/* two_mines_opened: 5x5, 2 mine(s).
 * TWO_MINES (mines 16, 18) after revealing (0,0): rows 0-2 revealed
 * (00000/00000/11211). Row 3 is forced (16, 18 mined; 15, 17, 19 safe) and
 * row 4 holds no mine: every hidden cell is proven.
 * Observation ('?' hidden, digits revealed clues):
 *   00000
 *   00000
 *   11211
 *   ?????
 *   ????? */
static const FixtureClueRun two_mines_opened_clues[4] = {
    {0u, 1u, 10u, 0u}, {10u, 1u, 2u, 1u}, {12u, 1u, 1u, 2u}, {13u, 1u, 2u, 1u},
};
static const FixtureRun two_mines_opened_class0[2] = {
    {15u, 2u, 3u}, {20u, 1u, 5u},
};
static const FixtureRun two_mines_opened_class1[2] = {
    {16u, 1u, 1u}, {18u, 1u, 1u},
};
static const FixtureOddsClass two_mines_opened_classes[2] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, two_mines_opened_class0, 2u}, /* 8 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, two_mines_opened_class1, 2u}, /* 2 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t two_mines_opened_layouts_limbs[1] = {
    0x00000001u,
};

/* public_equivalence_4x4: 4x4, 3 mine(s).
 * Layouts [2, 8, 12] and [2, 3, 8] give identical clues on cells [0, 1, 4, 5, 10, 15].
 * Observation ('?' hidden, digits revealed clues):
 *   01??
 *   12??
 *   ??0?
 *   ???0 */
static const FixtureClueRun public_equivalence_4x4_clues[6] = {
    {0u, 1u, 1u, 0u}, {1u, 1u, 1u, 1u}, {4u, 1u, 1u, 1u}, {5u, 1u, 1u, 2u}, {10u, 1u, 1u, 0u},
    {15u, 1u, 1u, 0u},
};
static const FixtureRun public_equivalence_4x4_class0[3] = {
    {6u, 1u, 2u}, {9u, 2u, 3u}, {14u, 1u, 1u},
};
static const FixtureRun public_equivalence_4x4_class1[2] = {
    {3u, 1u, 1u}, {12u, 1u, 1u},
};
static const FixtureRun public_equivalence_4x4_class2[2] = {
    {2u, 1u, 1u}, {8u, 1u, 1u},
};
static const FixtureOddsClass public_equivalence_4x4_classes[3] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, public_equivalence_4x4_class0, 3u}, /* 6 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(2), UINT64_C(0x3fe0000000000000)}, public_equivalence_4x4_class1, 2u}, /* 2 cell(s), p = 1/2 ~ 0.5 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, public_equivalence_4x4_class2, 2u}, /* 2 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t public_equivalence_4x4_layouts_limbs[1] = {
    0x00000002u,
};

/* public_equivalence_game_5x5: 5x5, 3 mine(s).
 * Both layouts give the same public state after revealing (0,0): row 3
 * forced, the third mine anywhere in row 4 (1/5 each).
 * Observation ('?' hidden, digits revealed clues):
 *   00000
 *   00000
 *   11211
 *   ?????
 *   ????? */
static const FixtureClueRun public_equivalence_game_5x5_clues[4] = {
    {0u, 1u, 10u, 0u}, {10u, 1u, 2u, 1u}, {12u, 1u, 1u, 2u}, {13u, 1u, 2u, 1u},
};
static const FixtureRun public_equivalence_game_5x5_class0[1] = {
    {15u, 2u, 3u},
};
static const FixtureRun public_equivalence_game_5x5_class1[1] = {
    {20u, 1u, 5u},
};
static const FixtureRun public_equivalence_game_5x5_class2[2] = {
    {16u, 1u, 1u}, {18u, 1u, 1u},
};
static const FixtureOddsClass public_equivalence_game_5x5_classes[3] = {
    {{UINT64_C(0), UINT64_C(1), UINT64_C(0x0000000000000000)}, public_equivalence_game_5x5_class0, 1u}, /* 3 cell(s), p = 0/1 ~ 0 */
    {{UINT64_C(1), UINT64_C(5), UINT64_C(0x3fc999999999999a)}, public_equivalence_game_5x5_class1, 1u}, /* 5 cell(s), p = 1/5 ~ 0.20000000000000001 */
    {{UINT64_C(1), UINT64_C(1), UINT64_C(0x3ff0000000000000)}, public_equivalence_game_5x5_class2, 2u}, /* 2 cell(s), p = 1/1 ~ 1 */
};
static const uint32_t public_equivalence_game_5x5_layouts_limbs[1] = {
    0x00000005u,
};

/* inconsistent_clue_exceeds_neighbours: 3x1, 1 mine(s).
 * clue larger than unrevealed neighbours
 * Observation ('?' hidden, digits revealed clues):
 *   2?? */
static const FixtureClueRun inconsistent_clue_exceeds_neighbours_clues[1] = {
    {0u, 1u, 1u, 2u},
};

/* inconsistent_corner_clue: 2x2, 3 mine(s).
 * clue on a board corner exceeding neighbours
 * Observation ('?' hidden, digits revealed clues):
 *   4?
 *   ?? */
static const FixtureClueRun inconsistent_corner_clue_clues[1] = {
    {0u, 1u, 1u, 4u},
};

/* inconsistent_adjacent_clues: 3x1, 1 mine(s).
 * adjacent clues disagree
 * Observation ('?' hidden, digits revealed clues):
 *   1?0 */
static const FixtureClueRun inconsistent_adjacent_clues_clues[2] = {
    {0u, 1u, 1u, 1u}, {2u, 1u, 1u, 0u},
};

/* inconsistent_too_many_mines: 3x1, 3 mine(s).
 * too many mines for unrevealed cells
 * Observation ('?' hidden, digits revealed clues):
 *   1?? */
static const FixtureClueRun inconsistent_too_many_mines_clues[1] = {
    {0u, 1u, 1u, 1u},
};

/* inconsistent_forced_mines_exceed_total: 5x1, 1 mine(s).
 * forced mines exceed the total
 * Observation ('?' hidden, digits revealed clues):
 *   1???1 */
static const FixtureClueRun inconsistent_forced_mines_exceed_total_clues[2] = {
    {0u, 1u, 1u, 1u}, {4u, 1u, 1u, 1u},
};

/* inconsistent_total_unreachable: 6x1, 3 mine(s).
 * mine total unreachable by any combination
 * Observation ('?' hidden, digits revealed clues):
 *   ?1??1? */
static const FixtureClueRun inconsistent_total_unreachable_clues[2] = {
    {1u, 1u, 1u, 1u}, {4u, 1u, 1u, 1u},
};

/* inconsistent_overlapping_counts: 3x2, 2 mine(s).
 * two clues over the same cells demand different counts
 * Observation ('?' hidden, digits revealed clues):
 *   ?1?
 *   ?2? */
static const FixtureClueRun inconsistent_overlapping_counts_clues[2] = {
    {1u, 1u, 1u, 1u}, {4u, 1u, 1u, 2u},
};

static const ProbabilityCase PROBABILITY_CASES[] = {
    {
        .name = "readme_weighting",
        .provenance = "README worked example; tests.test_probability.ExactAgreementTests.test_unconstrained_region_weights_component_mine_counts",
        .width = 11u, .height = 1u, .total_mines = 3u,
        .clues = readme_weighting_clues, .clue_run_count = 2u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = readme_weighting_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {readme_weighting_layouts_limbs, 1u},
        .meta = {3u, 1u, 6u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "components_total_2",
        .provenance = "tests.test_probability.ExactAgreementTests.test_isolated_components_are_coupled_by_the_mine_total",
        .width = 12u, .height = 1u, .total_mines = 2u,
        .clues = components_total_2_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = components_total_2_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = components_total_2_class0, .proven_safe_runs = 3u,
        .proven_mines = components_total_2_class1, .proven_mine_runs = 2u,
        .layouts = {components_total_2_layouts_limbs, 1u},
        .meta = {6u, 2u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "components_total_3",
        .provenance = "tests.test_probability.ExactAgreementTests.test_isolated_components_are_coupled_by_the_mine_total",
        .width = 12u, .height = 1u, .total_mines = 3u,
        .clues = components_total_3_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = components_total_3_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {components_total_3_layouts_limbs, 1u},
        .meta = {6u, 2u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "components_total_4",
        .provenance = "tests.test_probability.ExactAgreementTests.test_isolated_components_are_coupled_by_the_mine_total",
        .width = 12u, .height = 1u, .total_mines = 4u,
        .clues = components_total_4_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = components_total_4_classes, .class_count = 1u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {components_total_4_layouts_limbs, 1u},
        .meta = {6u, 2u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "components_total_5",
        .provenance = "tests.test_probability.ExactAgreementTests.test_isolated_components_are_coupled_by_the_mine_total",
        .width = 12u, .height = 1u, .total_mines = 5u,
        .clues = components_total_5_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = components_total_5_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {components_total_5_layouts_limbs, 1u},
        .meta = {6u, 2u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "components_total_6",
        .provenance = "tests.test_probability.ExactAgreementTests.test_isolated_components_are_coupled_by_the_mine_total",
        .width = 12u, .height = 1u, .total_mines = 6u,
        .clues = components_total_6_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = components_total_6_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = components_total_6_class0, .proven_safe_runs = 2u,
        .proven_mines = components_total_6_class1, .proven_mine_runs = 3u,
        .layouts = {components_total_6_layouts_limbs, 1u},
        .meta = {6u, 2u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "pool_2d_6x3",
        .provenance = "tests.test_probability.ExactAgreementTests.test_frontier_and_pool_on_2d_board",
        .width = 6u, .height = 3u, .total_mines = 5u,
        .clues = pool_2d_6x3_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = pool_2d_6x3_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {pool_2d_6x3_layouts_limbs, 1u},
        .meta = {3u, 1u, 14u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "forced_cells_4x1",
        .provenance = "tests.test_probability.ExactAgreementTests.test_forced_cells_are_proven",
        .width = 4u, .height = 1u, .total_mines = 1u,
        .clues = forced_cells_4x1_clues, .clue_run_count = 2u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = forced_cells_4x1_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = forced_cells_4x1_class0, .proven_safe_runs = 1u,
        .proven_mines = forced_cells_4x1_class1, .proven_mine_runs = 1u,
        .layouts = {forced_cells_4x1_layouts_limbs, 1u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "overlap_121_wall",
        .provenance = "tests.test_probability.ExactAgreementTests.test_overlapping_clue_deductions_match_brute_force",
        .width = 5u, .height = 3u, .total_mines = 2u,
        .clues = overlap_121_wall_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = overlap_121_wall_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = overlap_121_wall_class0, .proven_safe_runs = 1u,
        .proven_mines = overlap_121_wall_class1, .proven_mine_runs = 2u,
        .layouts = {overlap_121_wall_layouts_limbs, 1u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "prior_5x4_7",
        .provenance = "tests.test_probability.ExactAgreementTests.test_no_frontier_uses_uniform_prior",
        .width = 5u, .height = 4u, .total_mines = 7u,
        .clues = NULL, .clue_run_count = 0u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = prior_5x4_7_classes, .class_count = 1u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {prior_5x4_7_layouts_limbs, 1u},
        .meta = {0u, 0u, 20u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "prior_3x3_none",
        .provenance = "tests.test_probability.ExactAgreementTests.test_no_frontier_uses_uniform_prior",
        .width = 3u, .height = 3u, .total_mines = 0u,
        .clues = NULL, .clue_run_count = 0u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = prior_3x3_none_classes, .class_count = 1u, .rest_class = -1,
        .proven_safe = prior_3x3_none_class0, .proven_safe_runs = 1u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {prior_3x3_none_layouts_limbs, 1u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "prior_3x3_all",
        .provenance = "tests.test_probability.ExactAgreementTests.test_no_frontier_uses_uniform_prior",
        .width = 3u, .height = 3u, .total_mines = 9u,
        .clues = NULL, .clue_run_count = 0u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = prior_3x3_all_classes, .class_count = 1u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = prior_3x3_all_class0, .proven_mine_runs = 1u,
        .layouts = {prior_3x3_all_layouts_limbs, 1u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "all_revealed_2x2",
        .provenance = "tests.test_probability.ExactAgreementTests.test_all_cells_revealed",
        .width = 2u, .height = 2u, .total_mines = 0u,
        .clues = all_revealed_2x2_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = NULL, .class_count = 0u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {all_revealed_2x2_layouts_limbs, 1u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "chain_3001_exact",
        .provenance = "tests.test_probability.ExactAgreementTests.test_long_chain_component_is_exact_without_recursion",
        .width = 3001u, .height = 1u, .total_mines = 751u,
        .clues = chain_3001_exact_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = chain_3001_exact_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = chain_3001_exact_class0, .proven_safe_runs = 1u,
        .proven_mines = chain_3001_exact_class1, .proven_mine_runs = 1u,
        .layouts = {chain_3001_exact_layouts_limbs, 1u},
        .meta = {1501u, 1u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "prior_80x80_half",
        .provenance = "tests.test_probability.LargeBoardTests.test_uniform_prior_on_largest_board_does_not_overflow",
        .width = 80u, .height = 80u, .total_mines = 3200u,
        .clues = NULL, .clue_run_count = 0u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = prior_80x80_half_classes, .class_count = 1u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {prior_80x80_half_layouts_limbs, 200u},
        .meta = {0u, 0u, 6400u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "corner_80x80_2000",
        .provenance = "tests.test_probability.LargeBoardTests.test_huge_binomial_weights_keep_frontier_odds_exact",
        .width = 80u, .height = 80u, .total_mines = 2000u,
        .clues = corner_80x80_2000_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = corner_80x80_2000_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {corner_80x80_2000_layouts_limbs, 179u},
        .meta = {3u, 1u, 6396u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "near_certain_80x80",
        .provenance = "tests.test_probability.LargeBoardTests.test_near_certain_cells_are_not_proven_and_impossible_cells_are",
        .width = 80u, .height = 80u, .total_mines = 4u,
        .clues = near_certain_80x80_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = near_certain_80x80_classes, .class_count = 4u, .rest_class = -1,
        .proven_safe = near_certain_80x80_class0, .proven_safe_runs = 4u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {near_certain_80x80_layouts_limbs, 2u},
        .meta = {17u, 1u, 6375u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "random_seed_0",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(0))",
        .width = 6u, .height = 2u, .total_mines = 4u,
        .clues = random_seed_0_clues, .clue_run_count = 5u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_0_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = random_seed_0_class1, .proven_mine_runs = 2u,
        .layouts = {random_seed_0_layouts_limbs, 1u},
        .meta = {4u, 2u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_5",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(5))",
        .width = 1u, .height = 9u, .total_mines = 3u,
        .clues = random_seed_5_clues, .clue_run_count = 3u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_5_classes, .class_count = 3u, .rest_class = -1,
        .proven_safe = random_seed_5_class0, .proven_safe_runs = 2u,
        .proven_mines = random_seed_5_class2, .proven_mine_runs = 1u,
        .layouts = {random_seed_5_layouts_limbs, 1u},
        .meta = {0u, 0u, 3u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_11",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(11))",
        .width = 5u, .height = 4u, .total_mines = 5u,
        .clues = random_seed_11_clues, .clue_run_count = 8u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_11_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = random_seed_11_class0, .proven_safe_runs = 1u,
        .proven_mines = random_seed_11_class1, .proven_mine_runs = 4u,
        .layouts = {random_seed_11_layouts_limbs, 1u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_21",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(21))",
        .width = 4u, .height = 4u, .total_mines = 4u,
        .clues = random_seed_21_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_21_classes, .class_count = 6u, .rest_class = -1,
        .proven_safe = random_seed_21_class0, .proven_safe_runs = 2u,
        .proven_mines = random_seed_21_class5, .proven_mine_runs = 1u,
        .layouts = {random_seed_21_layouts_limbs, 1u},
        .meta = {8u, 1u, 1u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_39",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(39))",
        .width = 5u, .height = 3u, .total_mines = 3u,
        .clues = random_seed_39_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_39_classes, .class_count = 6u, .rest_class = -1,
        .proven_safe = random_seed_39_class0, .proven_safe_runs = 1u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {random_seed_39_layouts_limbs, 1u},
        .meta = {10u, 1u, 1u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_54",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(54))",
        .width = 4u, .height = 4u, .total_mines = 4u,
        .clues = random_seed_54_clues, .clue_run_count = 5u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_54_classes, .class_count = 3u, .rest_class = -1,
        .proven_safe = random_seed_54_class0, .proven_safe_runs = 3u,
        .proven_mines = random_seed_54_class2, .proven_mine_runs = 2u,
        .layouts = {random_seed_54_layouts_limbs, 1u},
        .meta = {10u, 1u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_74",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(74))",
        .width = 4u, .height = 3u, .total_mines = 3u,
        .clues = random_seed_74_clues, .clue_run_count = 3u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_74_classes, .class_count = 4u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {random_seed_74_layouts_limbs, 1u},
        .meta = {8u, 1u, 1u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_82",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(82))",
        .width = 4u, .height = 4u, .total_mines = 4u,
        .clues = random_seed_82_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_82_classes, .class_count = 6u, .rest_class = -1,
        .proven_safe = random_seed_82_class0, .proven_safe_runs = 2u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {random_seed_82_layouts_limbs, 1u},
        .meta = {9u, 1u, 1u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_93",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(93))",
        .width = 5u, .height = 4u, .total_mines = 5u,
        .clues = random_seed_93_clues, .clue_run_count = 6u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_93_classes, .class_count = 3u, .rest_class = -1,
        .proven_safe = random_seed_93_class0, .proven_safe_runs = 2u,
        .proven_mines = random_seed_93_class2, .proven_mine_runs = 1u,
        .layouts = {random_seed_93_layouts_limbs, 1u},
        .meta = {7u, 1u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_126",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(126))",
        .width = 3u, .height = 3u, .total_mines = 3u,
        .clues = random_seed_126_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_126_classes, .class_count = 1u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {random_seed_126_layouts_limbs, 1u},
        .meta = {8u, 1u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_128",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(128))",
        .width = 5u, .height = 3u, .total_mines = 4u,
        .clues = random_seed_128_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_128_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {random_seed_128_layouts_limbs, 1u},
        .meta = {5u, 1u, 9u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_131",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(131))",
        .width = 2u, .height = 6u, .total_mines = 3u,
        .clues = random_seed_131_clues, .clue_run_count = 3u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_131_classes, .class_count = 4u, .rest_class = -1,
        .proven_safe = random_seed_131_class0, .proven_safe_runs = 1u,
        .proven_mines = random_seed_131_class3, .proven_mine_runs = 1u,
        .layouts = {random_seed_131_layouts_limbs, 1u},
        .meta = {5u, 2u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "random_seed_134",
        .provenance = "tests.test_probability.ExactAgreementTests.test_random_small_boards_match_brute_force (random_observation(134))",
        .width = 5u, .height = 4u, .total_mines = 5u,
        .clues = random_seed_134_clues, .clue_run_count = 7u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = random_seed_134_classes, .class_count = 3u, .rest_class = -1,
        .proven_safe = random_seed_134_class0, .proven_safe_runs = 4u,
        .proven_mines = random_seed_134_class2, .proven_mine_runs = 3u,
        .layouts = {random_seed_134_layouts_limbs, 1u},
        .meta = {4u, 2u, 1u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "chain_board_12_total_36",
        .provenance = "tests.test_probability.SamplingTests.test_globally_incompatible_samples_are_unavailable",
        .width = 72u, .height = 1u, .total_mines = 36u,
        .clues = chain_board_12_total_36_clues, .clue_run_count = 24u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = chain_board_12_total_36_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = chain_board_12_total_36_class0, .proven_safe_runs = 1u,
        .proven_mines = chain_board_12_total_36_class1, .proven_mine_runs = 13u,
        .layouts = {chain_board_12_total_36_layouts_limbs, 1u},
        .meta = {36u, 12u, 12u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "mixed_components_18x1",
        .provenance = "tests.test_probability.SamplingTests.test_low_node_budget_mixes_exact_and_sampled_components",
        .width = 18u, .height = 1u, .total_mines = 5u,
        .clues = mixed_components_18x1_clues, .clue_run_count = 2u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = mixed_components_18x1_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {mixed_components_18x1_layouts_limbs, 1u},
        .meta = {8u, 2u, 4u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "chain_board_1_total_2",
        .provenance = "tests.test_probability.SamplingTests.test_too_few_effective_samples_is_unavailable",
        .width = 6u, .height = 1u, .total_mines = 2u,
        .clues = chain_board_1_total_2_clues, .clue_run_count = 2u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = chain_board_1_total_2_classes, .class_count = 1u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {chain_board_1_total_2_layouts_limbs, 1u},
        .meta = {3u, 1u, 1u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "chain_board_2_total_4",
        .provenance = "tests.test_probability.SamplingTests.test_no_sampling_budget_is_unavailable",
        .width = 12u, .height = 1u, .total_mines = 4u,
        .clues = chain_board_2_total_4_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = chain_board_2_total_4_classes, .class_count = 1u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {chain_board_2_total_4_layouts_limbs, 1u},
        .meta = {6u, 2u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "chain_board_2_forced_tail",
        .provenance = "tests.test_probability.SamplingTests.test_unavailable_still_reports_logical_proofs",
        .width = 14u, .height = 1u, .total_mines = 5u,
        .clues = chain_board_2_forced_tail_clues, .clue_run_count = 5u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = chain_board_2_forced_tail_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = chain_board_2_forced_tail_class1, .proven_mine_runs = 1u,
        .layouts = {chain_board_2_forced_tail_layouts_limbs, 1u},
        .meta = {6u, 2u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "two_mines_opened",
        .provenance = "tests.test_server.RealSolverIntegrationTests.test_odds_for_a_forced_position; tests.test_autosolve.RealAutosolveApiTests.test_real_solver_corrects_wrong_flag_and_wins",
        .width = 5u, .height = 5u, .total_mines = 2u,
        .clues = two_mines_opened_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = two_mines_opened_classes, .class_count = 2u, .rest_class = -1,
        .proven_safe = two_mines_opened_class0, .proven_safe_runs = 2u,
        .proven_mines = two_mines_opened_class1, .proven_mine_runs = 2u,
        .layouts = {two_mines_opened_layouts_limbs, 1u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_CLOSED_FORM,
    },
    {
        .name = "public_equivalence_4x4",
        .provenance = "tests.test_probability.PublicInformationOnlyTests.test_layouts_with_the_same_observation_get_the_same_answer",
        .width = 4u, .height = 4u, .total_mines = 3u,
        .clues = public_equivalence_4x4_clues, .clue_run_count = 6u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = public_equivalence_4x4_classes, .class_count = 3u, .rest_class = -1,
        .proven_safe = public_equivalence_4x4_class0, .proven_safe_runs = 3u,
        .proven_mines = public_equivalence_4x4_class2, .proven_mine_runs = 2u,
        .layouts = {public_equivalence_4x4_layouts_limbs, 1u},
        .meta = {0u, 0u, 2u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "public_equivalence_game_5x5",
        .provenance = "C addition (public-data-only): game layouts {16,18,20} and {16,18,24}",
        .width = 5u, .height = 5u, .total_mines = 3u,
        .clues = public_equivalence_game_5x5_clues, .clue_run_count = 4u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_ODDS,
        .baseline_status = FIXTURE_STATUS_EXACT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_EXACT),
        .classes = public_equivalence_game_5x5_classes, .class_count = 3u, .rest_class = -1,
        .proven_safe = public_equivalence_game_5x5_class0, .proven_safe_runs = 1u,
        .proven_mines = public_equivalence_game_5x5_class2, .proven_mine_runs = 2u,
        .layouts = {public_equivalence_game_5x5_layouts_limbs, 1u},
        .meta = {0u, 0u, 5u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_FRONTIER | FIXTURE_ORACLE_WHOLE_BOARD,
    },
    {
        .name = "inconsistent_clue_exceeds_neighbours",
        .provenance = "tests.test_probability.InvalidInputTests.test_contradictory_observations_raise (clue larger than unrevealed neighbours)",
        .width = 3u, .height = 1u, .total_mines = 1u,
        .clues = inconsistent_clue_exceeds_neighbours_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_INCONSISTENT,
        .baseline_status = FIXTURE_STATUS_INCONSISTENT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_INCONSISTENT),
        .classes = NULL, .class_count = 0u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {NULL, 0u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_FRONTIER,
    },
    {
        .name = "inconsistent_corner_clue",
        .provenance = "tests.test_probability.InvalidInputTests.test_contradictory_observations_raise (clue on a board corner exceeding neighbours)",
        .width = 2u, .height = 2u, .total_mines = 3u,
        .clues = inconsistent_corner_clue_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_INCONSISTENT,
        .baseline_status = FIXTURE_STATUS_INCONSISTENT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_INCONSISTENT),
        .classes = NULL, .class_count = 0u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {NULL, 0u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_FRONTIER,
    },
    {
        .name = "inconsistent_adjacent_clues",
        .provenance = "tests.test_probability.InvalidInputTests.test_contradictory_observations_raise (adjacent clues disagree)",
        .width = 3u, .height = 1u, .total_mines = 1u,
        .clues = inconsistent_adjacent_clues_clues, .clue_run_count = 2u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_INCONSISTENT,
        .baseline_status = FIXTURE_STATUS_INCONSISTENT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_INCONSISTENT),
        .classes = NULL, .class_count = 0u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {NULL, 0u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_FRONTIER,
    },
    {
        .name = "inconsistent_too_many_mines",
        .provenance = "tests.test_probability.InvalidInputTests.test_contradictory_observations_raise (too many mines for unrevealed cells)",
        .width = 3u, .height = 1u, .total_mines = 3u,
        .clues = inconsistent_too_many_mines_clues, .clue_run_count = 1u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_INCONSISTENT,
        .baseline_status = FIXTURE_STATUS_INCONSISTENT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_INCONSISTENT),
        .classes = NULL, .class_count = 0u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {NULL, 0u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_FRONTIER,
    },
    {
        .name = "inconsistent_forced_mines_exceed_total",
        .provenance = "tests.test_probability.InvalidInputTests.test_contradictory_observations_raise (forced mines exceed the total)",
        .width = 5u, .height = 1u, .total_mines = 1u,
        .clues = inconsistent_forced_mines_exceed_total_clues, .clue_run_count = 2u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_INCONSISTENT,
        .baseline_status = FIXTURE_STATUS_INCONSISTENT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_INCONSISTENT),
        .classes = NULL, .class_count = 0u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {NULL, 0u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_FRONTIER,
    },
    {
        .name = "inconsistent_total_unreachable",
        .provenance = "tests.test_probability.InvalidInputTests.test_contradictory_observations_raise (mine total unreachable by any combination)",
        .width = 6u, .height = 1u, .total_mines = 3u,
        .clues = inconsistent_total_unreachable_clues, .clue_run_count = 2u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_INCONSISTENT,
        .baseline_status = FIXTURE_STATUS_INCONSISTENT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_INCONSISTENT),
        .classes = NULL, .class_count = 0u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {NULL, 0u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_FRONTIER,
    },
    {
        .name = "inconsistent_overlapping_counts",
        .provenance = "tests.test_probability.InvalidInputTests.test_contradictory_observations_raise (two clues over the same cells demand different counts)",
        .width = 3u, .height = 2u, .total_mines = 2u,
        .clues = inconsistent_overlapping_counts_clues, .clue_run_count = 2u,
        .board_rows = NULL,
        .truth = FIXTURE_TRUTH_INCONSISTENT,
        .baseline_status = FIXTURE_STATUS_INCONSISTENT,
        .accepted_statuses = FIXTURE_STATUS_BIT(FIXTURE_STATUS_INCONSISTENT),
        .classes = NULL, .class_count = 0u, .rest_class = -1,
        .proven_safe = NULL, .proven_safe_runs = 0u,
        .proven_mines = NULL, .proven_mine_runs = 0u,
        .layouts = {NULL, 0u},
        .meta = {0u, 0u, 0u},
        .verified_by = FIXTURE_ORACLE_BASELINE | FIXTURE_ORACLE_WHOLE_BOARD | FIXTURE_ORACLE_FRONTIER,
    }
};
#define PROBABILITY_CASE_COUNT (sizeof(PROBABILITY_CASES) / sizeof(PROBABILITY_CASES[0]))

/* Indices into PROBABILITY_CASES. */
#define PROB_CASE_README_WEIGHTING 0u
#define PROB_CASE_COMPONENTS_TOTAL_2 1u
#define PROB_CASE_COMPONENTS_TOTAL_3 2u
#define PROB_CASE_COMPONENTS_TOTAL_4 3u
#define PROB_CASE_COMPONENTS_TOTAL_5 4u
#define PROB_CASE_COMPONENTS_TOTAL_6 5u
#define PROB_CASE_POOL_2D_6X3 6u
#define PROB_CASE_FORCED_CELLS_4X1 7u
#define PROB_CASE_OVERLAP_121_WALL 8u
#define PROB_CASE_PRIOR_5X4_7 9u
#define PROB_CASE_PRIOR_3X3_NONE 10u
#define PROB_CASE_PRIOR_3X3_ALL 11u
#define PROB_CASE_ALL_REVEALED_2X2 12u
#define PROB_CASE_CHAIN_3001_EXACT 13u
#define PROB_CASE_PRIOR_80X80_HALF 14u
#define PROB_CASE_CORNER_80X80_2000 15u
#define PROB_CASE_NEAR_CERTAIN_80X80 16u
#define PROB_CASE_RANDOM_SEED_0 17u
#define PROB_CASE_RANDOM_SEED_5 18u
#define PROB_CASE_RANDOM_SEED_11 19u
#define PROB_CASE_RANDOM_SEED_21 20u
#define PROB_CASE_RANDOM_SEED_39 21u
#define PROB_CASE_RANDOM_SEED_54 22u
#define PROB_CASE_RANDOM_SEED_74 23u
#define PROB_CASE_RANDOM_SEED_82 24u
#define PROB_CASE_RANDOM_SEED_93 25u
#define PROB_CASE_RANDOM_SEED_126 26u
#define PROB_CASE_RANDOM_SEED_128 27u
#define PROB_CASE_RANDOM_SEED_131 28u
#define PROB_CASE_RANDOM_SEED_134 29u
#define PROB_CASE_CHAIN_BOARD_12_TOTAL_36 30u
#define PROB_CASE_MIXED_COMPONENTS_18X1 31u
#define PROB_CASE_CHAIN_BOARD_1_TOTAL_2 32u
#define PROB_CASE_CHAIN_BOARD_2_TOTAL_4 33u
#define PROB_CASE_CHAIN_BOARD_2_FORCED_TAIL 34u
#define PROB_CASE_TWO_MINES_OPENED 35u
#define PROB_CASE_PUBLIC_EQUIVALENCE_4X4 36u
#define PROB_CASE_PUBLIC_EQUIVALENCE_GAME_5X5 37u
#define PROB_CASE_INCONSISTENT_CLUE_EXCEEDS_NEIGHBOURS 38u
#define PROB_CASE_INCONSISTENT_CORNER_CLUE 39u
#define PROB_CASE_INCONSISTENT_ADJACENT_CLUES 40u
#define PROB_CASE_INCONSISTENT_TOO_MANY_MINES 41u
#define PROB_CASE_INCONSISTENT_FORCED_MINES_EXCEED_TOTAL 42u
#define PROB_CASE_INCONSISTENT_TOTAL_UNREACHABLE 43u
#define PROB_CASE_INCONSISTENT_OVERLAPPING_COUNTS 44u

/* Malformed solver arguments (baseline ValueError). baseline_case 0 marks a
 * C-specific addition: a sparse observation can repeat a cell, a mapping
 * cannot. Defaults: node 100000, sample 2000, time 1.5 s. */
static const int64_t malformed_clue_index_past_end_cells[1] = {
    INT64_C(9),
};
static const int64_t malformed_clue_index_past_end_values[1] = {
    INT64_C(1),
};
static const int64_t malformed_clue_index_negative_cells[1] = {
    INT64_C(-1),
};
static const int64_t malformed_clue_index_negative_values[1] = {
    INT64_C(1),
};
static const int64_t malformed_clue_value_nine_cells[1] = {
    INT64_C(0),
};
static const int64_t malformed_clue_value_nine_values[1] = {
    INT64_C(9),
};
static const int64_t malformed_clue_value_negative_cells[1] = {
    INT64_C(0),
};
static const int64_t malformed_clue_value_negative_values[1] = {
    INT64_C(-1),
};
static const int64_t malformed_duplicate_clue_index_cells[2] = {
    INT64_C(0), INT64_C(0),
};
static const int64_t malformed_duplicate_clue_index_values[2] = {
    INT64_C(1), INT64_C(1),
};
static const MalformedObservationCase MALFORMED_OBSERVATIONS[] = {
    {"width_zero", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(0), INT64_C(3), INT64_C(1), NULL, NULL, 0u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"height_negative", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(-1), INT64_C(1), NULL, NULL, 0u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"total_negative", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(-1), NULL, NULL, 0u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"total_exceeds_cells", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(10), NULL, NULL, 0u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"clue_index_past_end", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(1), malformed_clue_index_past_end_cells, malformed_clue_index_past_end_values, 1u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"clue_index_negative", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(1), malformed_clue_index_negative_cells, malformed_clue_index_negative_values, 1u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"clue_value_nine", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(1), malformed_clue_value_nine_cells, malformed_clue_value_nine_values, 1u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"clue_value_negative", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(1), malformed_clue_value_negative_cells, malformed_clue_value_negative_values, 1u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"time_budget_negative", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(1), NULL, NULL, 0u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, -1.0, FIXTURE_STATUS_MALFORMED, 1u},
    {"time_budget_nan", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(1), NULL, NULL, 0u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_NAN, 0.0, FIXTURE_STATUS_MALFORMED, 1u},
    {"node_budget_negative", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(1), NULL, NULL, 0u, INT64_C(-1), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"sample_budget_negative", "tests.test_probability.InvalidInputTests.test_malformed_arguments_raise_value_error", INT64_C(3), INT64_C(3), INT64_C(1), NULL, NULL, 0u, INT64_C(100000), INT64_C(-5), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 1u},
    {"duplicate_clue_index", "C addition: duplicate sparse clue index", INT64_C(3), INT64_C(3), INT64_C(1), malformed_duplicate_clue_index_cells, malformed_duplicate_clue_index_values, 2u, INT64_C(100000), INT64_C(2000), FIXTURE_VALUE_FINITE, 1.5, FIXTURE_STATUS_MALFORMED, 0u}
};
#define MALFORMED_OBSERVATION_COUNT (sizeof(MALFORMED_OBSERVATIONS) / sizeof(MALFORMED_OBSERVATIONS[0]))

/* Public-information equivalence (tests.test_probability.PublicInformationOnlyTests
 * .test_layouts_with_the_same_observation_get_the_same_answer): both layouts
 * produce identical clues on `shown`, so the solver input and output must be
 * identical; the observation and its odds are PROB_CASE_PUBLIC_EQUIVALENCE_4X4. */
static const uint32_t PUBLIC_EQUIVALENCE_4X4_SHOWN[6] = {
    0u, 1u, 4u, 5u, 10u, 15u,
};
static const uint32_t PUBLIC_EQUIVALENCE_4X4_LAYOUT_A[3] = {
    2u, 8u, 12u,
};
static const uint32_t PUBLIC_EQUIVALENCE_4X4_LAYOUT_B[3] = {
    2u, 3u, 8u,
};

/* Game-level variant (C addition): 5x5, 3 mines, first reveal (0,0). Both
 * layouts must yield byte-identical public state and solver observation;
 * odds in PROB_CASE_PUBLIC_EQUIVALENCE_GAME_5X5. */
static const uint32_t PUBLIC_EQUIVALENCE_GAME_LAYOUT_A[3] = {
    16u, 18u, 20u,
};
static const uint32_t PUBLIC_EQUIVALENCE_GAME_LAYOUT_B[3] = {
    16u, 18u, 24u,
};
