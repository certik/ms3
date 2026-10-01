/*
 * Game rule fixtures (scripts, validation, RNG properties).
 *
 * GENERATED reference data - do not edit by hand. Frozen from the Python
 * reference at baseline commit 153b78ac025858f11c124e6ae17953fdecd0aec3 by a one-off
 * generator kept outside the repository (see fixture_types.h for the
 * method, oracles, conventions and schema). Provenance strings name the
 * baseline unittest each case ports (module.Class.method).
 *
 * Deterministic game scripts: fixed layouts, every step's arguments, the
 * expected outcome (changed / no-op / error code) and the public state after
 * the step (status, revision, flags, elapsed ms on the injected clock,
 * revealed count, exploded cells, board render). Errors and no-ops leave the
 * state unchanged. Also configuration validation tables and the parameters of
 * the RNG property tests, which the C port runs with its own generator.
 * Port destinations: tests/c/test_game.c and tests/c/test_autosolve.c.
 */
#pragma once

#include "fixture_types.h"

/* Fixed layouts from tests/test_game.py:
 *   TWO_MINES 5x5, mines (3,1),(3,3) = cells 16, 18; after revealing (0,0):
 *     00000 / 00000 / 11211 / ##### / #####
 *   COLUMN 7x5, mines (0,3),(2,3),(4,3) = cells 3, 17, 31; the right half is
 *     only reachable through chords. */
static const uint32_t game_flood_reveals_zero_region_layout[2] = {
    16u, 18u,
};
static const GameStep game_flood_reveals_zero_region_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
};

static const uint32_t game_flood_stops_at_numbers_layout[3] = {
    3u, 17u, 31u,
};
static const GameStep game_flood_stops_at_numbers_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "001####" "002####" "001####" "002####" "001####"},
};

static const uint32_t game_reveal_number_only_that_cell_layout[2] = {
    16u, 18u,
};
static const GameStep game_reveal_number_only_that_cell_steps[2] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "1####" "#####"},
};

static const uint32_t game_flood_skips_flagged_cells_layout[3] = {
    3u, 17u, 31u,
};
static const GameStep game_flood_skips_flagged_cells_steps[3] = {
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "F######" "#######" "#######" "#######" "#######"},
    {.op = FIXTURE_OP_FLAG, .row = 4, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 2, .flags_after = 2,
     .board = "F######" "#######" "#######" "#######" "#F#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 2, .revealed_count = 13u,
     .board = "F01####" "002####" "001####" "002####" "0F1####"},
};

static const uint32_t game_huge_flood_is_iterative_layout[1] = {
    6399u,
};
static const GameStep game_huge_flood_is_iterative_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 1, .flags_after = 1, .revealed_count = 6399u},
};

static const GameStep game_toggle_flag_counts_and_revisions_steps[2] = {
    {.op = FIXTURE_OP_FLAG, .row = 2, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "#########" "#########" "###F#####" "#########" "#########" "#########" "#########" "#########" "#########"},
    {.op = FIXTURE_OP_FLAG, .row = 2, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 2,
     .board = "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
};

static const GameStep game_flags_may_exceed_mine_count_steps[18] = {
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "F########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 2, .flags_after = 2,
     .board = "F########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "F########"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 3, .flags_after = 3,
     .board = "FF#######" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "F########"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 4, .flags_after = 4,
     .board = "FF#######" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FF#######"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 5, .flags_after = 5,
     .board = "FFF######" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FF#######"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 6, .flags_after = 6,
     .board = "FFF######" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFF######"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 7, .flags_after = 7,
     .board = "FFFF#####" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFF######"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 8, .flags_after = 8,
     .board = "FFFF#####" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFF#####"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 9, .flags_after = 9,
     .board = "FFFFF####" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFF#####"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 10, .flags_after = 10,
     .board = "FFFFF####" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFF####"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 5, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 11, .flags_after = 11,
     .board = "FFFFFF###" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFF####"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 5, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 12, .flags_after = 12,
     .board = "FFFFFF###" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFFF###"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 6, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 13, .flags_after = 13,
     .board = "FFFFFFF##" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFFF###"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 6, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 14, .flags_after = 14,
     .board = "FFFFFFF##" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFFFF##"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 7, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 15, .flags_after = 15,
     .board = "FFFFFFFF#" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFFFF##"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 7, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 16, .flags_after = 16,
     .board = "FFFFFFFF#" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFFFFF#"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 8, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 17, .flags_after = 17,
     .board = "FFFFFFFFF" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFFFFF#"},
    {.op = FIXTURE_OP_FLAG, .row = 8, .col = 8, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 18, .flags_after = 18,
     .board = "FFFFFFFFF" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "FFFFFFFFF"},
};

static const uint32_t game_flagging_revealed_cell_is_noop_layout[2] = {
    16u, 18u,
};
static const GameStep game_flagging_revealed_cell_is_noop_steps[2] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 1, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
};

static const uint32_t game_flagged_cell_revealed_after_unflag_layout[2] = {
    16u, 18u,
};
static const GameStep game_flagged_cell_revealed_after_unflag_steps[5] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "F####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "F####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "1####" "#####"},
};

static const GameStep game_flags_before_start_no_timer_steps[2] = {
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "F########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
    {.op = FIXTURE_OP_ADVANCE, .advance_ms = 30000,
     .expect = FIXTURE_EXPECT_NONE, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "F########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
};

static const uint32_t game_play_to_victory_layout[2] = {
    16u, 18u,
};
static const GameStep game_play_to_victory_steps[10] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 1, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "1F###" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 1, .revealed_count = 17u,
     .board = "00000" "00000" "11211" "1F2##" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 5, .flags_after = 1, .revealed_count = 18u,
     .board = "00000" "00000" "11211" "1F2#1" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 6, .flags_after = 1, .revealed_count = 19u,
     .board = "00000" "00000" "11211" "1F2#1" "1####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 7, .flags_after = 1, .revealed_count = 20u,
     .board = "00000" "00000" "11211" "1F2#1" "11###"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 8, .flags_after = 1, .revealed_count = 21u,
     .board = "00000" "00000" "11211" "1F2#1" "112##"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 9, .flags_after = 1, .revealed_count = 22u,
     .board = "00000" "00000" "11211" "1F2#1" "1121#"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 10, .flags_after = 2, .revealed_count = 23u,
     .board = "00000" "00000" "11211" "1F2F1" "11211"},
};

static const uint32_t game_chord_reveals_unflagged_neighbors_layout[2] = {
    16u, 18u,
};
static const GameStep game_chord_reveals_unflagged_neighbors_steps[3] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 1, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "1F###" "#####"},
};

static const uint32_t game_chord_noop_when_flag_count_differs_layout[2] = {
    16u, 18u,
};
static const GameStep game_chord_noop_when_flag_count_differs_steps[6] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 2, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F#F#" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 3, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#FFF#" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 3, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#FFF#" "#####"},
};

static const uint32_t game_chord_on_hidden_or_unstarted_is_noop_layout[2] = {
    16u, 18u,
};
static const GameStep game_chord_on_hidden_or_unstarted_is_noop_steps[4] = {
    {.op = FIXTURE_OP_CHORD, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#####" "#####" "#####" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 1, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
};

static const uint32_t game_chords_can_win_layout[2] = {
    16u, 18u,
};
static const GameStep game_chords_can_win_steps[9] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 2, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F#F#" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 2, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "1F#F#" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 5, .flags_after = 2, .revealed_count = 17u,
     .board = "00000" "00000" "11211" "1F2F#" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 6, .flags_after = 2, .revealed_count = 18u,
     .board = "00000" "00000" "11211" "1F2F1" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 7, .flags_after = 2, .revealed_count = 20u,
     .board = "00000" "00000" "11211" "1F2F1" "11###"},
    {.op = FIXTURE_OP_CHORD, .row = 3, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 8, .flags_after = 2, .revealed_count = 22u,
     .board = "00000" "00000" "11211" "1F2F1" "1121#"},
    {.op = FIXTURE_OP_CHORD, .row = 3, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 9, .flags_after = 2, .revealed_count = 23u,
     .board = "00000" "00000" "11211" "1F2F1" "11211"},
};

static const uint32_t game_chord_opening_zero_floods_layout[3] = {
    3u, 17u, 31u,
};
static const GameStep game_chord_opening_zero_floods_steps[8] = {
    {.op = FIXTURE_OP_REVEAL, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "001####" "002####" "001####" "002####" "001####"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "001F###" "002####" "001####" "002####" "001####"},
    {.op = FIXTURE_OP_FLAG, .row = 2, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 2, .revealed_count = 15u,
     .board = "001F###" "002####" "001F###" "002####" "001####"},
    {.op = FIXTURE_OP_CHORD, .row = 1, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 2, .revealed_count = 16u,
     .board = "001F###" "0022###" "001F###" "002####" "001####"},
    {.op = FIXTURE_OP_CHORD, .row = 1, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 5, .flags_after = 2, .revealed_count = 19u,
     .board = "001F1##" "00222##" "001F1##" "002####" "001####"},
    {.op = FIXTURE_OP_CHORD, .row = 1, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 6, .flags_after = 2, .revealed_count = 31u,
     .board = "001F100" "0022200" "001F100" "002#200" "001#100"},
    {.op = FIXTURE_OP_FLAG, .row = 4, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 7, .flags_after = 3, .revealed_count = 31u,
     .board = "001F100" "0022200" "001F100" "002#200" "001F100"},
    {.op = FIXTURE_OP_CHORD, .row = 3, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 8, .flags_after = 3, .revealed_count = 32u,
     .board = "001F100" "0022200" "001F100" "0022200" "001F100"},
};

static const uint32_t game_chord_with_wrong_flag_loses_layout[2] = {
    16u, 18u,
};
static const uint32_t game_chord_with_wrong_flag_loses_s2_exploded[1] = {
    16u,
};
static const GameStep game_chord_with_wrong_flag_loses_steps[3] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "F####" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_LOST, .revision_after = 3, .flags_after = 1, .revealed_count = 16u, .exploded = game_chord_with_wrong_flag_loses_s2_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "FX###" "#####"},
};

static const uint32_t game_chord_detonating_several_mines_layout[2] = {
    16u, 18u,
};
static const uint32_t game_chord_detonating_several_mines_s8_exploded[2] = {
    16u, 18u,
};
static const GameStep game_chord_detonating_several_mines_steps[9] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 2, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F#F#" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 2, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "#F2F#" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 5, .flags_after = 1, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "##2F#" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 6, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "##2##" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 4, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 7, .flags_after = 1, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "##2##" "#F###"},
    {.op = FIXTURE_OP_FLAG, .row = 4, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 8, .flags_after = 2, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "##2##" "#FF##"},
    {.op = FIXTURE_OP_CHORD, .row = 3, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_LOST, .revision_after = 9, .flags_after = 2, .revealed_count = 19u, .exploded = game_chord_detonating_several_mines_s8_exploded, .exploded_count = 2u,
     .board = "00000" "00000" "11211" "#X2X#" "#FF1#"},
};

static const uint32_t game_revealing_a_mine_loses_layout[2] = {
    16u, 18u,
};
static const uint32_t game_revealing_a_mine_loses_s2_exploded[1] = {
    16u,
};
static const GameStep game_revealing_a_mine_loses_steps[3] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_ADVANCE, .advance_ms = 4000,
     .expect = FIXTURE_EXPECT_NONE, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .elapsed_ms = 4000, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_LOST, .revision_after = 2, .elapsed_ms = 4000, .revealed_count = 16u, .exploded = game_revealing_a_mine_loses_s2_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
};

static const uint32_t game_actions_after_loss_rejected_layout[2] = {
    16u, 18u,
};
static const uint32_t game_actions_after_loss_rejected_s1_exploded[1] = {
    16u,
};
static const uint32_t game_actions_after_loss_rejected_s2_exploded[1] = {
    16u,
};
static const uint32_t game_actions_after_loss_rejected_s3_exploded[1] = {
    16u,
};
static const uint32_t game_actions_after_loss_rejected_s4_exploded[1] = {
    16u,
};
static const GameStep game_actions_after_loss_rejected_steps[5] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_LOST, .revision_after = 2, .revealed_count = 16u, .exploded = game_actions_after_loss_rejected_s1_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_OVER, .status = FIXTURE_GAME_LOST, .revision_after = 2, .revealed_count = 16u, .exploded = game_actions_after_loss_rejected_s2_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_OVER, .status = FIXTURE_GAME_LOST, .revision_after = 2, .revealed_count = 16u, .exploded = game_actions_after_loss_rejected_s3_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_OVER, .status = FIXTURE_GAME_LOST, .revision_after = 2, .revealed_count = 16u, .exploded = game_actions_after_loss_rejected_s4_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
};

static const uint32_t game_actions_after_win_rejected_layout[2] = {
    16u, 18u,
};
static const GameStep game_actions_after_win_rejected_steps[12] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "1####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .revealed_count = 17u,
     .board = "00000" "00000" "11211" "1#2##" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .revealed_count = 18u,
     .board = "00000" "00000" "11211" "1#2#1" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 5, .revealed_count = 19u,
     .board = "00000" "00000" "11211" "1#2#1" "1####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 6, .revealed_count = 20u,
     .board = "00000" "00000" "11211" "1#2#1" "11###"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 7, .revealed_count = 21u,
     .board = "00000" "00000" "11211" "1#2#1" "112##"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 3, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 8, .revealed_count = 22u,
     .board = "00000" "00000" "11211" "1#2#1" "1121#"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 9, .flags_after = 2, .revealed_count = 23u,
     .board = "00000" "00000" "11211" "1F2F1" "11211"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_OVER, .status = FIXTURE_GAME_WON, .revision_after = 9, .flags_after = 2, .revealed_count = 23u,
     .board = "00000" "00000" "11211" "1F2F1" "11211"},
    {.op = FIXTURE_OP_FLAG, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_OVER, .status = FIXTURE_GAME_WON, .revision_after = 9, .flags_after = 2, .revealed_count = 23u,
     .board = "00000" "00000" "11211" "1F2F1" "11211"},
    {.op = FIXTURE_OP_CHORD, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_OVER, .status = FIXTURE_GAME_WON, .revision_after = 9, .flags_after = 2, .revealed_count = 23u,
     .board = "00000" "00000" "11211" "1F2F1" "11211"},
};

static const uint32_t game_timer_starts_and_freezes_on_loss_layout[2] = {
    16u, 18u,
};
static const uint32_t game_timer_starts_and_freezes_on_loss_s4_exploded[1] = {
    16u,
};
static const uint32_t game_timer_starts_and_freezes_on_loss_s5_exploded[1] = {
    16u,
};
static const GameStep game_timer_starts_and_freezes_on_loss_steps[6] = {
    {.op = FIXTURE_OP_ADVANCE, .advance_ms = 10000,
     .expect = FIXTURE_EXPECT_NONE, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#####" "#####" "#####" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_ADVANCE, .advance_ms = 2250,
     .expect = FIXTURE_EXPECT_NONE, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .elapsed_ms = 2250, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_ADVANCE, .advance_ms = 1500,
     .expect = FIXTURE_EXPECT_NONE, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .elapsed_ms = 3750, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_LOST, .revision_after = 2, .elapsed_ms = 3750, .revealed_count = 16u, .exploded = game_timer_starts_and_freezes_on_loss_s4_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
    {.op = FIXTURE_OP_ADVANCE, .advance_ms = 100000,
     .expect = FIXTURE_EXPECT_NONE, .status = FIXTURE_GAME_LOST, .revision_after = 2, .elapsed_ms = 3750, .revealed_count = 16u, .exploded = game_timer_starts_and_freezes_on_loss_s5_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
};

static const GameStep game_timer_freezes_on_win_steps[2] = {
    {.op = FIXTURE_OP_REVEAL, .row = 2, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 1, .flags_after = 16, .revealed_count = 9u,
     .board = "FFFFF" "F535F" "F303F" "F535F" "FFFFF"},
    {.op = FIXTURE_OP_ADVANCE, .advance_ms = 60000,
     .expect = FIXTURE_EXPECT_NONE, .status = FIXTURE_GAME_WON, .revision_after = 1, .flags_after = 16, .revealed_count = 9u,
     .board = "FFFFF" "F535F" "F303F" "F535F" "FFFFF"},
};

static const uint32_t game_each_change_increments_revision_layout[2] = {
    16u, 18u,
};
static const GameStep game_each_change_increments_revision_steps[3] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 1, .revealed_count = 16u,
     .board = "00000" "00000" "11211" "1F###" "#####"},
};

static const uint32_t game_noops_keep_revision_layout[2] = {
    16u, 18u,
};
static const GameStep game_noops_keep_revision_steps[8] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 1, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 2, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
};

static const uint32_t game_stale_revision_rejected_layout[2] = {
    16u, 18u,
};
static const GameStep game_stale_revision_rejected_steps[5] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = 0,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_STALE_REVISION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = 2,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_STALE_REVISION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = 99,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_STALE_REVISION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = INT64_C(4294967297),
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_STALE_REVISION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
};

static const uint32_t game_invalid_input_rejected_layout[2] = {
    16u, 18u,
};
static const GameStep game_invalid_input_rejected_steps[12] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_RAW_ACTION, .raw_action = 3, .row = 1, .col = 1, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_ACTION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_RAW_ACTION, .raw_action = 255, .row = 1, .col = 1, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_ACTION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_RAW_ACTION, .raw_action = -1, .row = 1, .col = 1, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_ACTION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = -1, .col = 1, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_OUT_OF_BOUNDS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 5, .col = 1, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_OUT_OF_BOUNDS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 1, .col = 5, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_OUT_OF_BOUNDS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_CHORD, .row = 1, .col = -1, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_OUT_OF_BOUNDS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = -1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_REVISION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = INT64_C(4294967296), .col = 0, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_OUT_OF_BOUNDS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = INT64_C(4294967297), .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_OUT_OF_BOUNDS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = INT64_C(1000000000000000000), .col = 0, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_OUT_OF_BOUNDS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
};

static const GameStep game_invalid_input_before_start_steps[2] = {
    {.op = FIXTURE_OP_REVEAL, .row = 9, .col = 0, .revision = 0,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_OUT_OF_BOUNDS, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = 1,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_STALE_REVISION, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
};

static const uint32_t game_no_mines_until_first_reveal_layout[10] = {
    6u, 8u, 15u, 17u, 26u, 35u, 57u, 66u, 69u, 72u,
};
static const GameStep game_no_mines_until_first_reveal_steps[3] = {
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "F########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
    {.op = FIXTURE_OP_CHORD, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "F########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 60u,
     .board = "F00002###" "000002###" "00000114#" "00000002#" "000000011" "001110000" "002#21110" "112####10" "#######10"},
};

static const GameStep game_revealing_flagged_cell_does_not_start_steps[2] = {
    {.op = FIXTURE_OP_FLAG, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "#########" "#########" "#########" "#########" "####F####" "#########" "#########" "#########" "#########"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "#########" "#########" "#########" "#########" "####F####" "#########" "#########" "#########" "#########"},
};

static const uint32_t game_flagged_cells_in_start_area_stay_hidden_layout[2] = {
    16u, 18u,
};
static const GameStep game_flagged_cells_in_start_area_stay_hidden_steps[2] = {
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_READY, .revision_after = 1, .flags_after = 1,
     .board = "#F###" "#####" "#####" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 14u,
     .board = "0F000" "00000" "11211" "#####" "#####"},
};

static const uint32_t game_invalid_layout_duplicates_layout[10] = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
};
static const GameStep game_invalid_layout_duplicates_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_LAYOUT, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
};

static const uint32_t game_invalid_layout_in_start_block_layout[10] = {
    10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 79u, 78u,
};
static const GameStep game_invalid_layout_in_start_block_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_LAYOUT, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
};

static const uint32_t game_invalid_layout_wrong_count_layout[9] = {
    20u, 30u, 40u, 50u, 60u, 70u, 80u, 79u, 78u,
};
static const GameStep game_invalid_layout_wrong_count_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_LAYOUT, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
};

static const uint32_t game_invalid_layout_out_of_range_layout[10] = {
    20u, 30u, 40u, 50u, 60u, 70u, 80u, 79u, 78u, 81u,
};
static const GameStep game_invalid_layout_out_of_range_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_LAYOUT, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########" "#########"},
};

static const GameStep game_max_density_center_5x5_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 2, .col = 2, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 1, .flags_after = 16, .revealed_count = 9u,
     .board = "FFFFF" "F535F" "F303F" "F535F" "FFFFF"},
};

static const GameStep game_max_density_center_9x9_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 1, .flags_after = 72, .revealed_count = 9u,
     .board = "FFFFFFFFF" "FFFFFFFFF" "FFFFFFFFF" "FFF535FFF" "FFF303FFF" "FFF535FFF" "FFFFFFFFF" "FFFFFFFFF" "FFFFFFFFF"},
};

static const GameStep game_max_density_center_80x80_steps[1] = {
    {.op = FIXTURE_OP_REVEAL, .row = 40, .col = 40, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 1, .flags_after = 6391, .revealed_count = 9u},
};

static const uint32_t game_revealed_clues_public_only_layout[2] = {
    16u, 18u,
};
static const GameStep game_revealed_clues_public_only_steps[2] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
};

static const uint32_t game_conflict_errors_carry_latest_state_layout[2] = {
    16u, 18u,
};
static const uint32_t game_conflict_errors_carry_latest_state_s2_exploded[1] = {
    16u,
};
static const uint32_t game_conflict_errors_carry_latest_state_s3_exploded[1] = {
    16u,
};
static const GameStep game_conflict_errors_carry_latest_state_steps[4] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = 0,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_STALE_REVISION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 1, .revision = 1,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_LOST, .revision_after = 2, .revealed_count = 16u, .exploded = game_conflict_errors_carry_latest_state_s2_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 4, .col = 4, .revision = 2,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_OVER, .status = FIXTURE_GAME_LOST, .revision_after = 2, .revealed_count = 16u, .exploded = game_conflict_errors_carry_latest_state_s3_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
};

static const uint32_t game_deduce_flags_and_corrects_safe_flags_layout[2] = {
    16u, 18u,
};
static const int64_t game_deduce_flags_and_corrects_safe_flags_s3_safe[2] = {
    INT64_C(15), INT64_C(17),
};
static const int64_t game_deduce_flags_and_corrects_safe_flags_s3_mines[2] = {
    INT64_C(16), INT64_C(18),
};
static const GameStep game_deduce_flags_and_corrects_safe_flags_steps[4] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "F####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 4, .col = 4, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 2, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "F####" "####F"},
    {.op = FIXTURE_OP_DEDUCE, .revision = FIXTURE_REV_CURRENT, .safe = game_deduce_flags_and_corrects_safe_flags_s3_safe, .safe_count = 2u, .mines = game_deduce_flags_and_corrects_safe_flags_s3_mines, .mine_count = 2u,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .flags_after = 3, .revealed_count = 17u,
     .board = "00000" "00000" "11211" "1F2F#" "####F"},
};

static const uint32_t game_deduce_clears_safe_flags_before_flood_layout[3] = {
    3u, 17u, 31u,
};
static const int64_t game_deduce_clears_safe_flags_before_flood_s3_safe[4] = {
    INT64_C(5), INT64_C(6), INT64_C(12), INT64_C(13),
};
static const GameStep game_deduce_clears_safe_flags_before_flood_steps[4] = {
    {.op = FIXTURE_OP_REVEAL, .row = 2, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "001####" "002####" "001####" "002####" "001####"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 5, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "001##F#" "002####" "001####" "002####" "001####"},
    {.op = FIXTURE_OP_FLAG, .row = 0, .col = 6, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 3, .flags_after = 2, .revealed_count = 15u,
     .board = "001##FF" "002####" "001####" "002####" "001####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = FIXTURE_REV_CURRENT, .safe = game_deduce_clears_safe_flags_before_flood_s3_safe, .safe_count = 4u, .mines = NULL, .mine_count = 0u,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 4, .revealed_count = 30u,
     .board = "001#100" "002#200" "001#100" "002#200" "001#100"},
};

static const uint32_t game_deduce_empty_or_already_flagged_noop_layout[2] = {
    16u, 18u,
};
static const int64_t game_deduce_empty_or_already_flagged_noop_s3_mines[2] = {
    INT64_C(16), INT64_C(16),
};
static const GameStep game_deduce_empty_or_already_flagged_noop_steps[4] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = FIXTURE_REV_CURRENT, .safe = NULL, .safe_count = 0u, .mines = NULL, .mine_count = 0u,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = FIXTURE_REV_CURRENT, .safe = NULL, .safe_count = 0u, .mines = game_deduce_empty_or_already_flagged_noop_s3_mines, .mine_count = 2u,
     .expect = FIXTURE_EXPECT_NOOP, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#F###" "#####"},
};

static const uint32_t game_deduce_batch_wins_layout[2] = {
    16u, 18u,
};
static const int64_t game_deduce_batch_wins_s1_safe[8] = {
    INT64_C(15), INT64_C(17), INT64_C(19), INT64_C(20), INT64_C(21), INT64_C(22), INT64_C(23), INT64_C(24),
};
static const int64_t game_deduce_batch_wins_s1_mines[2] = {
    INT64_C(16), INT64_C(18),
};
static const GameStep game_deduce_batch_wins_steps[2] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = 1, .safe = game_deduce_batch_wins_s1_safe, .safe_count = 8u, .mines = game_deduce_batch_wins_s1_mines, .mine_count = 2u,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 2, .flags_after = 2, .revealed_count = 23u,
     .board = "00000" "00000" "11211" "1F2F1" "11211"},
};

static const uint32_t game_deduce_stale_and_invalid_layout[2] = {
    16u, 18u,
};
static const int64_t game_deduce_stale_and_invalid_s1_safe[1] = {
    INT64_C(15),
};
static const int64_t game_deduce_stale_and_invalid_s1_mines[1] = {
    INT64_C(16),
};
static const int64_t game_deduce_stale_and_invalid_s2_safe[2] = {
    INT64_C(15), INT64_C(-1),
};
static const int64_t game_deduce_stale_and_invalid_s2_mines[1] = {
    INT64_C(16),
};
static const int64_t game_deduce_stale_and_invalid_s3_safe[1] = {
    INT64_C(15),
};
static const int64_t game_deduce_stale_and_invalid_s3_mines[1] = {
    INT64_C(25),
};
static const int64_t game_deduce_stale_and_invalid_s4_safe[1] = {
    INT64_C(15),
};
static const int64_t game_deduce_stale_and_invalid_s4_mines[1] = {
    INT64_C(15),
};
static const int64_t game_deduce_stale_and_invalid_s5_safe[1] = {
    INT64_C(0),
};
static const int64_t game_deduce_stale_and_invalid_s5_mines[1] = {
    INT64_C(16),
};
static const int64_t game_deduce_stale_and_invalid_s6_safe[1] = {
    INT64_C(15),
};
static const int64_t game_deduce_stale_and_invalid_s6_mines[1] = {
    INT64_C(16),
};
static const int64_t game_deduce_stale_and_invalid_s7_safe[1] = {
    INT64_C(15),
};
static const int64_t game_deduce_stale_and_invalid_s7_mines[1] = {
    INT64_C(4294967312),
};
static const GameStep game_deduce_stale_and_invalid_steps[8] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = 0, .safe = game_deduce_stale_and_invalid_s1_safe, .safe_count = 1u, .mines = game_deduce_stale_and_invalid_s1_mines, .mine_count = 1u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_STALE_REVISION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = 1, .safe = game_deduce_stale_and_invalid_s2_safe, .safe_count = 2u, .mines = game_deduce_stale_and_invalid_s2_mines, .mine_count = 1u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_DEDUCTIONS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = 1, .safe = game_deduce_stale_and_invalid_s3_safe, .safe_count = 1u, .mines = game_deduce_stale_and_invalid_s3_mines, .mine_count = 1u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_DEDUCTIONS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = 1, .safe = game_deduce_stale_and_invalid_s4_safe, .safe_count = 1u, .mines = game_deduce_stale_and_invalid_s4_mines, .mine_count = 1u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_DEDUCTIONS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = 1, .safe = game_deduce_stale_and_invalid_s5_safe, .safe_count = 1u, .mines = game_deduce_stale_and_invalid_s5_mines, .mine_count = 1u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_DEDUCTIONS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = -1, .safe = game_deduce_stale_and_invalid_s6_safe, .safe_count = 1u, .mines = game_deduce_stale_and_invalid_s6_mines, .mine_count = 1u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_REVISION, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = 1, .safe = game_deduce_stale_and_invalid_s7_safe, .safe_count = 1u, .mines = game_deduce_stale_and_invalid_s7_mines, .mine_count = 1u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_INVALID_DEDUCTIONS, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
};

static const uint32_t game_deduce_first_move_and_terminal_layout[2] = {
    16u, 18u,
};
static const uint32_t game_deduce_first_move_and_terminal_s2_exploded[1] = {
    16u,
};
static const uint32_t game_deduce_first_move_and_terminal_s3_exploded[1] = {
    16u,
};
static const GameStep game_deduce_first_move_and_terminal_steps[4] = {
    {.op = FIXTURE_OP_DEDUCE, .revision = 0, .safe = NULL, .safe_count = 0u, .mines = NULL, .mine_count = 0u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_NOT_STARTED, .status = FIXTURE_GAME_READY, .revision_after = 0,
     .board = "#####" "#####" "#####" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_REVEAL, .row = 3, .col = 1, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_LOST, .revision_after = 2, .revealed_count = 16u, .exploded = game_deduce_first_move_and_terminal_s2_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = FIXTURE_REV_CURRENT, .safe = NULL, .safe_count = 0u, .mines = NULL, .mine_count = 0u,
     .expect = FIXTURE_EXPECT_ERROR, .error = FIXTURE_ERR_GAME_OVER, .status = FIXTURE_GAME_LOST, .revision_after = 2, .revealed_count = 16u, .exploded = game_deduce_first_move_and_terminal_s3_exploded, .exploded_count = 1u,
     .board = "00000" "00000" "11211" "#X###" "#####"},
};

static const uint32_t game_real_solver_corrects_wrong_flag_and_wins_layout[2] = {
    16u, 18u,
};
static const int64_t game_real_solver_corrects_wrong_flag_and_wins_s2_safe[8] = {
    INT64_C(15), INT64_C(17), INT64_C(19), INT64_C(20), INT64_C(21), INT64_C(22), INT64_C(23), INT64_C(24),
};
static const int64_t game_real_solver_corrects_wrong_flag_and_wins_s2_mines[2] = {
    INT64_C(16), INT64_C(18),
};
static const GameStep game_real_solver_corrects_wrong_flag_and_wins_steps[3] = {
    {.op = FIXTURE_OP_REVEAL, .row = 0, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "#####" "#####"},
    {.op = FIXTURE_OP_FLAG, .row = 3, .col = 0, .revision = FIXTURE_REV_CURRENT,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_PLAYING, .revision_after = 2, .flags_after = 1, .revealed_count = 15u,
     .board = "00000" "00000" "11211" "F####" "#####"},
    {.op = FIXTURE_OP_DEDUCE, .revision = FIXTURE_REV_CURRENT, .safe = game_real_solver_corrects_wrong_flag_and_wins_s2_safe, .safe_count = 8u, .mines = game_real_solver_corrects_wrong_flag_and_wins_s2_mines, .mine_count = 2u,
     .expect = FIXTURE_EXPECT_CHANGED, .status = FIXTURE_GAME_WON, .revision_after = 3, .flags_after = 2, .revealed_count = 23u,
     .board = "00000" "00000" "11211" "1F2F1" "11211"},
};

static const GameScript GAME_SCRIPTS[] = {
    {"flood_reveals_zero_region", "tests.test_game.FloodTests.test_flood_reveals_zero_region_and_its_border", 5u, 5u, 2u, game_flood_reveals_zero_region_layout, 2u, 1000000, game_flood_reveals_zero_region_steps, 1u},
    {"flood_stops_at_numbers", "tests.test_game.FloodTests.test_flood_stops_at_numbers_on_other_side_of_wall; tests.test_game.SerializationTests.test_cells_are_row_major", 7u, 5u, 3u, game_flood_stops_at_numbers_layout, 3u, 1000000, game_flood_stops_at_numbers_steps, 1u},
    {"reveal_number_only_that_cell", "tests.test_game.FloodTests.test_revealing_a_number_reveals_only_that_cell", 5u, 5u, 2u, game_reveal_number_only_that_cell_layout, 2u, 1000000, game_reveal_number_only_that_cell_steps, 2u},
    {"flood_skips_flagged_cells", "tests.test_game.FloodTests.test_flood_skips_flagged_cells", 7u, 5u, 3u, game_flood_skips_flagged_cells_layout, 3u, 1000000, game_flood_skips_flagged_cells_steps, 3u},
    /* One mine at (79,79): the first reveal floods 6399 cells and wins.
     * Must be iterative (no recursion proportional to the region). */
    {"huge_flood_is_iterative", "tests.test_game.FloodTests.test_huge_flood_is_iterative", 80u, 80u, 1u, game_huge_flood_is_iterative_layout, 1u, 1000000, game_huge_flood_is_iterative_steps, 1u},
    {"toggle_flag_counts_and_revisions", "tests.test_game.FlagTests.test_toggle_flag_counts_and_revisions", 9u, 9u, 10u, NULL, 0u, 1000000, game_toggle_flag_counts_and_revisions_steps, 2u},
    {"flags_may_exceed_mine_count", "tests.test_game.FlagTests.test_flags_may_exceed_mine_count", 9u, 9u, 10u, NULL, 0u, 1000000, game_flags_may_exceed_mine_count_steps, 18u},
    {"flagging_revealed_cell_is_noop", "tests.test_game.FlagTests.test_flagging_revealed_cell_is_a_noop", 5u, 5u, 2u, game_flagging_revealed_cell_is_noop_layout, 2u, 1000000, game_flagging_revealed_cell_is_noop_steps, 2u},
    {"flagged_cell_revealed_after_unflag", "tests.test_game.FlagTests.test_flagged_cell_can_be_revealed_after_unflagging", 5u, 5u, 2u, game_flagged_cell_revealed_after_unflag_layout, 2u, 1000000, game_flagged_cell_revealed_after_unflag_steps, 5u},
    {"flags_before_start_no_timer", "tests.test_game.FlagTests.test_flags_before_start_do_not_start_timer", 9u, 9u, 10u, NULL, 0u, 1000000, game_flags_before_start_no_timer_steps, 2u},
    /* Reveal, flag, chord, then single reveals: won at revision 10 with both
     * mines flagged (00000/00000/11211/1F2F1/11211). */
    {"play_to_victory", "tests.test_server.GameActionTests.test_play_to_victory", 5u, 5u, 2u, game_play_to_victory_layout, 2u, 1000000, game_play_to_victory_steps, 10u},
    {"chord_reveals_unflagged_neighbors", "tests.test_game.ChordTests.test_chord_reveals_unflagged_neighbors_when_flags_match", 5u, 5u, 2u, game_chord_reveals_unflagged_neighbors_layout, 2u, 1000000, game_chord_reveals_unflagged_neighbors_steps, 3u},
    {"chord_noop_when_flag_count_differs", "tests.test_game.ChordTests.test_chord_is_noop_when_flag_count_differs", 5u, 5u, 2u, game_chord_noop_when_flag_count_differs_layout, 2u, 1000000, game_chord_noop_when_flag_count_differs_steps, 6u},
    {"chord_on_hidden_or_unstarted_is_noop", "tests.test_game.ChordTests.test_chord_on_hidden_or_unstarted_cells_is_a_noop", 5u, 5u, 2u, game_chord_on_hidden_or_unstarted_is_noop_layout, 2u, 1000000, game_chord_on_hidden_or_unstarted_is_noop_steps, 4u},
    {"chords_can_win", "tests.test_game.ChordTests.test_chords_can_win_the_game", 5u, 5u, 2u, game_chords_can_win_layout, 2u, 1000000, game_chords_can_win_steps, 9u},
    {"chord_opening_zero_floods", "tests.test_game.ChordTests.test_chord_opening_a_zero_floods", 7u, 5u, 3u, game_chord_opening_zero_floods_layout, 3u, 1000000, game_chord_opening_zero_floods_steps, 8u},
    /* Wrong flag on safe cell 15: the chord detonates mine 16. After the loss
     * cell 15 stays flagged and is publicly not a mine (a wrong flag). */
    {"chord_with_wrong_flag_loses", "tests.test_game.ChordTests.test_chord_with_wrong_flag_loses", 5u, 5u, 2u, game_chord_with_wrong_flag_loses_layout, 2u, 1000000, game_chord_with_wrong_flag_loses_steps, 3u},
    {"chord_detonating_several_mines", "tests.test_game.ChordTests.test_chord_detonating_several_mines_marks_each", 5u, 5u, 2u, game_chord_detonating_several_mines_layout, 2u, 1000000, game_chord_detonating_several_mines_steps, 9u},
    /* Public cell 16 after the loss: revealed, mine, exploded, no clue;
     * hidden cell 20 keeps a null clue; the full layout {16,18} is public. */
    {"revealing_a_mine_loses", "tests.test_game.TerminalStateTests.test_revealing_a_mine_loses", 5u, 5u, 2u, game_revealing_a_mine_loses_layout, 2u, 1000000, game_revealing_a_mine_loses_steps, 3u},
    {"actions_after_loss_rejected", "tests.test_game.TerminalStateTests.test_actions_after_game_over_are_rejected_without_change (lose)", 5u, 5u, 2u, game_actions_after_loss_rejected_layout, 2u, 1000000, game_actions_after_loss_rejected_steps, 5u},
    {"actions_after_win_rejected", "tests.test_game.TerminalStateTests.test_actions_after_game_over_are_rejected_without_change (win); tests.test_game.TerminalStateTests.test_winning_requires_only_safe_reveals_and_flags_every_mine", 5u, 5u, 2u, game_actions_after_win_rejected_layout, 2u, 1000000, game_actions_after_win_rejected_steps, 12u},
    {"timer_starts_and_freezes_on_loss", "tests.test_game.TimerTests.test_timer_starts_on_first_reveal_and_freezes_at_the_end", 5u, 5u, 2u, game_timer_starts_and_freezes_on_loss_layout, 2u, 500000, game_timer_starts_and_freezes_on_loss_steps, 6u},
    /* Maximum density: the layout is forced (every cell outside the 3x3
     * start block), so any placement source gives the same game. */
    {"timer_freezes_on_win", "tests.test_game.TimerTests.test_timer_freezes_on_win", 5u, 5u, 16u, NULL, 0u, 1000000, game_timer_freezes_on_win_steps, 2u},
    {"each_change_increments_revision", "tests.test_game.RevisionAndValidationTests.test_each_state_change_increments_revision_once", 5u, 5u, 2u, game_each_change_increments_revision_layout, 2u, 1000000, game_each_change_increments_revision_steps, 3u},
    {"noops_keep_revision", "tests.test_game.RevisionAndValidationTests.test_noops_do_not_change_revision", 5u, 5u, 2u, game_noops_keep_revision_layout, 2u, 1000000, game_noops_keep_revision_steps, 8u},
    /* Expected revisions 0, 2, 99 (baseline) and 2^32+1 (C addition: must not
     * truncate to the current revision 1). */
    {"stale_revision_rejected", "tests.test_game.RevisionAndValidationTests.test_stale_revision_is_rejected_without_mutation", 5u, 5u, 2u, game_stale_revision_rejected_layout, 2u, 1000000, game_stale_revision_rejected_steps, 5u},
    /* Raw action values 3/255/-1 are not reveal/flag/chord (baseline: 'explode',
     * 'REVEAL', None, ['reveal']). Coordinates 2^32, 2^32+1 and 10^18 are C
     * additions (baseline 10**40): they must not truncate onto a valid cell.
     * Non-integer inputs (True, 1.0, '1', None) are JS adapter cases. */
    {"invalid_input_rejected", "tests.test_game.RevisionAndValidationTests.test_invalid_input_is_rejected_without_mutation", 5u, 5u, 2u, game_invalid_input_rejected_layout, 2u, 1000000, game_invalid_input_rejected_steps, 12u},
    {"invalid_input_before_start", "tests.test_game.RevisionAndValidationTests.test_invalid_input_before_start_does_not_place_mines", 9u, 9u, 10u, NULL, 0u, 1000000, game_invalid_input_before_start_steps, 2u},
    /* Layout frozen from the baseline random.Random(1) placement; before the
     * first reveal no mine exists and flag/chord do not start the game. */
    {"no_mines_until_first_reveal", "tests.test_game.FirstRevealTests.test_no_mines_exist_until_first_reveal", 9u, 9u, 10u, game_no_mines_until_first_reveal_layout, 10u, 1000000, game_no_mines_until_first_reveal_steps, 3u},
    {"revealing_flagged_cell_does_not_start", "tests.test_game.FirstRevealTests.test_revealing_a_flagged_cell_does_not_start_the_game", 9u, 9u, 10u, NULL, 0u, 1000000, game_revealing_flagged_cell_does_not_start_steps, 2u},
    {"flagged_cells_in_start_area_stay_hidden", "tests.test_game.FirstRevealTests.test_flagged_cells_in_start_area_stay_hidden", 5u, 5u, 2u, game_flagged_cells_in_start_area_stay_hidden_layout, 2u, 1000000, game_flagged_cells_in_start_area_stay_hidden_steps, 2u},
    /* Placement source returns cell 0 ten times (duplicates inside the start
     * block): rejected before any mutation. In C this is the fixed-layout
     * test hook's validation; the real generator draws distinct cells. */
    {"invalid_layout_duplicates", "tests.test_game.FirstRevealTests.test_invalid_random_sample_is_rejected_before_mutation", 9u, 9u, 10u, game_invalid_layout_duplicates_layout, 10u, 1000000, game_invalid_layout_duplicates_steps, 1u},
    {"invalid_layout_in_start_block", "C addition (placement validation): mine inside the protected 3x3 block", 9u, 9u, 10u, game_invalid_layout_in_start_block_layout, 10u, 1000000, game_invalid_layout_in_start_block_steps, 1u},
    {"invalid_layout_wrong_count", "C addition (placement validation): fewer cells than mines", 9u, 9u, 10u, game_invalid_layout_wrong_count_layout, 9u, 1000000, game_invalid_layout_wrong_count_steps, 1u},
    {"invalid_layout_out_of_range", "C addition (placement validation): cell index past the board", 9u, 9u, 10u, game_invalid_layout_out_of_range_layout, 10u, 1000000, game_invalid_layout_out_of_range_steps, 1u},
    /* Forced layout (every cell outside the start block): any placement
     * source must win at once with every mine flagged. */
    {"max_density_center_5x5", "tests.test_game.FirstRevealTests.test_maximum_density_interior_start_wins_immediately", 5u, 5u, 16u, NULL, 0u, 1000000, game_max_density_center_5x5_steps, 1u},
    /* Forced layout (every cell outside the start block): any placement
     * source must win at once with every mine flagged. */
    {"max_density_center_9x9", "tests.test_game.FirstRevealTests.test_maximum_density_interior_start_wins_immediately", 9u, 9u, 72u, NULL, 0u, 1000000, game_max_density_center_9x9_steps, 1u},
    /* Forced layout (every cell outside the start block): any placement
     * source must win at once with every mine flagged. */
    {"max_density_center_80x80", "tests.test_game.FirstRevealTests.test_maximum_density_interior_start_wins_immediately", 80u, 80u, 6391u, NULL, 0u, 1000000, game_max_density_center_80x80_steps, 1u},
    /* The solver observation is exactly the 15 revealed digits of the final
     * board (flags excluded); see PROB_CASE_TWO_MINES_OPENED. */
    {"revealed_clues_public_only", "tests.test_game.SerializationTests.test_revealed_clues_contains_only_public_safe_cells", 5u, 5u, 2u, game_revealed_clues_public_only_layout, 2u, 1000000, game_revealed_clues_public_only_steps, 2u},
    {"conflict_errors_carry_latest_state", "tests.test_game.GameStoreTests.test_conflict_errors_carry_latest_state", 5u, 5u, 2u, game_conflict_errors_carry_latest_state_layout, 2u, 1000000, game_conflict_errors_carry_latest_state_steps, 4u},
    /* Flag 24 is not among the proofs and stays; safe flag 15 is removed and
     * the cell revealed; one revision for the whole batch. */
    {"deduce_flags_and_corrects_safe_flags", "tests.test_autosolve.DeductionBatchTests.test_flags_mines_and_corrects_safe_flags_in_one_revision", 5u, 5u, 2u, game_deduce_flags_and_corrects_safe_flags_layout, 2u, 1000000, game_deduce_flags_and_corrects_safe_flags_steps, 4u},
    {"deduce_clears_safe_flags_before_flood", "tests.test_autosolve.DeductionBatchTests.test_all_safe_flags_are_removed_before_zero_flood", 7u, 5u, 3u, game_deduce_clears_safe_flags_before_flood_layout, 3u, 1000000, game_deduce_clears_safe_flags_before_flood_steps, 4u},
    {"deduce_empty_or_already_flagged_noop", "tests.test_autosolve.DeductionBatchTests.test_empty_or_already_flagged_deductions_are_noops", 5u, 5u, 2u, game_deduce_empty_or_already_flagged_noop_layout, 2u, 1000000, game_deduce_empty_or_already_flagged_noop_steps, 4u},
    {"deduce_batch_wins", "tests.test_autosolve.DeductionBatchTests.test_batch_can_win_without_revealing_mines", 5u, 5u, 2u, game_deduce_batch_wins_layout, 2u, 1000000, game_deduce_batch_wins_steps, 2u},
    /* Stale revision 0; invalid sets: index -1, index 25, a cell in both lists,
     * a revealed cell; revision -1. Index 2^32+16 is a C addition (must not
     * truncate to mine 16). Boolean/float/string inputs are JS adapter cases. */
    {"deduce_stale_and_invalid", "tests.test_autosolve.DeductionBatchTests.test_stale_and_invalid_deductions_never_partially_apply", 5u, 5u, 2u, game_deduce_stale_and_invalid_layout, 2u, 1000000, game_deduce_stale_and_invalid_steps, 8u},
    {"deduce_first_move_and_terminal", "tests.test_autosolve.DeductionBatchTests.test_first_move_and_terminal_state_stay_player_controlled", 5u, 5u, 2u, game_deduce_first_move_and_terminal_layout, 2u, 1000000, game_deduce_first_move_and_terminal_steps, 4u},
    /* The deduction batch is the exact proof set of PROB_CASE_TWO_MINES_OPENED
     * (flags are not evidence): wrong flag 15 is cleared and revealed, 16 and
     * 18 are flagged, and the game is won in one revision. */
    {"real_solver_corrects_wrong_flag_and_wins", "tests.test_autosolve.RealAutosolveApiTests.test_real_solver_corrects_wrong_flag_and_wins", 5u, 5u, 2u, game_real_solver_corrects_wrong_flag_and_wins_layout, 2u, 1000000, game_real_solver_corrects_wrong_flag_and_wins_steps, 3u}
};
#define GAME_SCRIPT_COUNT (sizeof(GAME_SCRIPTS) / sizeof(GAME_SCRIPTS[0]))

#define GAME_SCRIPT_FLOOD_REVEALS_ZERO_REGION 0u
#define GAME_SCRIPT_FLOOD_STOPS_AT_NUMBERS 1u
#define GAME_SCRIPT_REVEAL_NUMBER_ONLY_THAT_CELL 2u
#define GAME_SCRIPT_FLOOD_SKIPS_FLAGGED_CELLS 3u
#define GAME_SCRIPT_HUGE_FLOOD_IS_ITERATIVE 4u
#define GAME_SCRIPT_TOGGLE_FLAG_COUNTS_AND_REVISIONS 5u
#define GAME_SCRIPT_FLAGS_MAY_EXCEED_MINE_COUNT 6u
#define GAME_SCRIPT_FLAGGING_REVEALED_CELL_IS_NOOP 7u
#define GAME_SCRIPT_FLAGGED_CELL_REVEALED_AFTER_UNFLAG 8u
#define GAME_SCRIPT_FLAGS_BEFORE_START_NO_TIMER 9u
#define GAME_SCRIPT_PLAY_TO_VICTORY 10u
#define GAME_SCRIPT_CHORD_REVEALS_UNFLAGGED_NEIGHBORS 11u
#define GAME_SCRIPT_CHORD_NOOP_WHEN_FLAG_COUNT_DIFFERS 12u
#define GAME_SCRIPT_CHORD_ON_HIDDEN_OR_UNSTARTED_IS_NOOP 13u
#define GAME_SCRIPT_CHORDS_CAN_WIN 14u
#define GAME_SCRIPT_CHORD_OPENING_ZERO_FLOODS 15u
#define GAME_SCRIPT_CHORD_WITH_WRONG_FLAG_LOSES 16u
#define GAME_SCRIPT_CHORD_DETONATING_SEVERAL_MINES 17u
#define GAME_SCRIPT_REVEALING_A_MINE_LOSES 18u
#define GAME_SCRIPT_ACTIONS_AFTER_LOSS_REJECTED 19u
#define GAME_SCRIPT_ACTIONS_AFTER_WIN_REJECTED 20u
#define GAME_SCRIPT_TIMER_STARTS_AND_FREEZES_ON_LOSS 21u
#define GAME_SCRIPT_TIMER_FREEZES_ON_WIN 22u
#define GAME_SCRIPT_EACH_CHANGE_INCREMENTS_REVISION 23u
#define GAME_SCRIPT_NOOPS_KEEP_REVISION 24u
#define GAME_SCRIPT_STALE_REVISION_REJECTED 25u
#define GAME_SCRIPT_INVALID_INPUT_REJECTED 26u
#define GAME_SCRIPT_INVALID_INPUT_BEFORE_START 27u
#define GAME_SCRIPT_NO_MINES_UNTIL_FIRST_REVEAL 28u
#define GAME_SCRIPT_REVEALING_FLAGGED_CELL_DOES_NOT_START 29u
#define GAME_SCRIPT_FLAGGED_CELLS_IN_START_AREA_STAY_HIDDEN 30u
#define GAME_SCRIPT_INVALID_LAYOUT_DUPLICATES 31u
#define GAME_SCRIPT_INVALID_LAYOUT_IN_START_BLOCK 32u
#define GAME_SCRIPT_INVALID_LAYOUT_WRONG_COUNT 33u
#define GAME_SCRIPT_INVALID_LAYOUT_OUT_OF_RANGE 34u
#define GAME_SCRIPT_MAX_DENSITY_CENTER_5X5 35u
#define GAME_SCRIPT_MAX_DENSITY_CENTER_9X9 36u
#define GAME_SCRIPT_MAX_DENSITY_CENTER_80X80 37u
#define GAME_SCRIPT_REVEALED_CLUES_PUBLIC_ONLY 38u
#define GAME_SCRIPT_CONFLICT_ERRORS_CARRY_LATEST_STATE 39u
#define GAME_SCRIPT_DEDUCE_FLAGS_AND_CORRECTS_SAFE_FLAGS 40u
#define GAME_SCRIPT_DEDUCE_CLEARS_SAFE_FLAGS_BEFORE_FLOOD 41u
#define GAME_SCRIPT_DEDUCE_EMPTY_OR_ALREADY_FLAGGED_NOOP 42u
#define GAME_SCRIPT_DEDUCE_BATCH_WINS 43u
#define GAME_SCRIPT_DEDUCE_STALE_AND_INVALID 44u
#define GAME_SCRIPT_DEDUCE_FIRST_MOVE_AND_TERMINAL 45u
#define GAME_SCRIPT_REAL_SOLVER_CORRECTS_WRONG_FLAG_AND_WINS 46u

/* Configuration validation (validate_config order: width, height, mines).
 * Values above 2^31 are C additions guarding against truncation; non-integer
 * inputs (True, 9.0, "9", None, [9], 10**20/10**30) are JS adapter cases. */
static const GameConfigCase GAME_CONFIG_CASES[] = {
    {INT64_C(5), INT64_C(5), INT64_C(1), FIXTURE_ERR_NONE, INT64_C(16)}, /* tests.test_game.ConfigValidationTests.test_accepts_boundary_configurations */
    {INT64_C(5), INT64_C(5), INT64_C(16), FIXTURE_ERR_NONE, INT64_C(16)}, /* tests.test_game.ConfigValidationTests.test_accepts_boundary_configurations */
    {INT64_C(80), INT64_C(80), INT64_C(6391), FIXTURE_ERR_NONE, INT64_C(6391)}, /* tests.test_game.ConfigValidationTests.test_accepts_boundary_configurations */
    {INT64_C(5), INT64_C(80), INT64_C(391), FIXTURE_ERR_NONE, INT64_C(391)}, /* tests.test_game.ConfigValidationTests.test_accepts_boundary_configurations */
    {INT64_C(80), INT64_C(5), INT64_C(391), FIXTURE_ERR_NONE, INT64_C(391)}, /* tests.test_game.ConfigValidationTests.test_accepts_boundary_configurations */
    {INT64_C(9), INT64_C(9), INT64_C(10), FIXTURE_ERR_NONE, INT64_C(72)}, /* tests.test_game.ConfigValidationTests.test_accepts_boundary_configurations */
    {INT64_C(9), INT64_C(9), INT64_C(1), FIXTURE_ERR_NONE, INT64_C(72)}, /* tests.test_game.ConfigValidationTests.test_max_mines_leaves_a_safe_three_by_three */
    {INT64_C(5), INT64_C(5), INT64_C(1), FIXTURE_ERR_NONE, INT64_C(16)}, /* tests.test_game.ConfigValidationTests.test_max_mines_leaves_a_safe_three_by_three */
    {INT64_C(80), INT64_C(80), INT64_C(1), FIXTURE_ERR_NONE, INT64_C(6391)}, /* tests.test_game.ConfigValidationTests.test_max_mines_leaves_a_safe_three_by_three */
    {INT64_C(4), INT64_C(9), INT64_C(10), FIXTURE_ERR_INVALID_WIDTH, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions */
    {INT64_C(9), INT64_C(4), INT64_C(10), FIXTURE_ERR_INVALID_HEIGHT, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions */
    {INT64_C(81), INT64_C(9), INT64_C(10), FIXTURE_ERR_INVALID_WIDTH, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions */
    {INT64_C(9), INT64_C(81), INT64_C(10), FIXTURE_ERR_INVALID_HEIGHT, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions */
    {INT64_C(0), INT64_C(9), INT64_C(10), FIXTURE_ERR_INVALID_WIDTH, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions */
    {INT64_C(9), INT64_C(0), INT64_C(10), FIXTURE_ERR_INVALID_HEIGHT, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions */
    {INT64_C(-9), INT64_C(9), INT64_C(10), FIXTURE_ERR_INVALID_WIDTH, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions */
    {INT64_C(9), INT64_C(-9), INT64_C(10), FIXTURE_ERR_INVALID_HEIGHT, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions */
    {INT64_C(4294967305), INT64_C(9), INT64_C(10), FIXTURE_ERR_INVALID_WIDTH, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions (C addition) */
    {INT64_C(9), INT64_C(4294967305), INT64_C(10), FIXTURE_ERR_INVALID_HEIGHT, INT64_C(-1)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_dimensions (C addition) */
    {INT64_C(9), INT64_C(9), INT64_C(0), FIXTURE_ERR_INVALID_MINES, INT64_C(72)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_mine_counts */
    {INT64_C(9), INT64_C(9), INT64_C(-1), FIXTURE_ERR_INVALID_MINES, INT64_C(72)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_mine_counts */
    {INT64_C(9), INT64_C(9), INT64_C(73), FIXTURE_ERR_INVALID_MINES, INT64_C(72)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_mine_counts */
    {INT64_C(9), INT64_C(9), INT64_C(81), FIXTURE_ERR_INVALID_MINES, INT64_C(72)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_mine_counts */
    {INT64_C(9), INT64_C(9), INT64_C(4294967306), FIXTURE_ERR_INVALID_MINES, INT64_C(72)}, /* tests.test_game.ConfigValidationTests.test_rejects_invalid_mine_counts (C addition) */
    {INT64_C(4), INT64_C(81), INT64_C(0), FIXTURE_ERR_INVALID_WIDTH, INT64_C(-1)}, /* Baseline validation order (validate_config) */
    {INT64_C(9), INT64_C(81), INT64_C(0), FIXTURE_ERR_INVALID_HEIGHT, INT64_C(-1)}, /* Baseline validation order (validate_config) */
};
#define GAME_CONFIG_CASE_COUNT (sizeof(GAME_CONFIG_CASES) / sizeof(GAME_CONFIG_CASES[0]))

/* RNG property-test parameters (the C generator replaces random.Random). */

/* tests.test_game.FirstRevealTests.test_first_reveal_and_neighbors_are_safe_everywhere:
 * for every config, first reveal at (0,0), (0,w-1), (h-1,0), (h-1,w-1), (0,w/2),
 * (h/2,0), (h/2,w/2) and (1,1), each with GAME_FIRST_REVEAL_SEEDS seeds: exactly
 * `mines` placed, none in the clipped 3x3 start block, status playing or won,
 * and the first cell shows 0. */
static const uint32_t GAME_FIRST_REVEAL_CONFIGS[5][3] = {
    {9u, 9u, 72u}, {9u, 9u, 60u}, {16u, 16u, 40u}, {30u, 16u, 99u}, {5u, 5u, 16u},
};
#define GAME_FIRST_REVEAL_SEEDS 15u

/* tests.test_game.FirstRevealTests.test_maximum_density_corner_start_places_exact_count:
 * 5x5 with 16 mines, first reveal (4,0), 25 seeds: 16 mines outside the block. */
#define GAME_CORNER_DENSITY_SEEDS 25u

/* tests.test_game.FirstRevealTests.test_mine_placement_is_uniform_outside_the_start_area:
 * 5x5 with 1 mine, first reveal (0,0), 4200 games; the 4 start-block cells never
 * get the mine; Pearson chi-square over the other 21 cells (20 degrees of
 * freedom) must stay below 45.3, the 99.9th percentile (45.315). */
#define GAME_UNIFORMITY_TRIALS 4200u
#define GAME_UNIFORMITY_CHI2_LIMIT 45.3

/* tests.test_game.SerializationTests.test_no_hidden_information_before_game_ends:
 * 12x10 with 25 mines, 10 seeds, reveal (5,5) then flag (0,0); while playing,
 * no public cell exposes a mine flag, an exploded flag or an unrevealed clue. */
static const uint32_t GAME_NO_LEAK_CONFIG[3] = {12u, 10u, 25u};
#define GAME_NO_LEAK_SEEDS 10u
