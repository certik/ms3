#pragma once

/*
 * game.h - internal additions to the ms_game_* API of engine.h.
 *
 * ms_game is opaque everywhere else. The engine service needs to read a
 * game's status and revision without rendering a whole view, and the C
 * tests need to inspect the private layout and to reach MS_REVISION_MAX
 * without 2^31 moves. None of this is exported by the WebAssembly reactor;
 * nothing here reveals the layout to JS.
 */

#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Engine service accessors (public facts the view header also carries). */
uint32_t ms_game_get_status(const ms_game *game); /* an ms_game_status value */
uint32_t ms_game_get_revision(const ms_game *game);
uint32_t ms_game_get_generation(const ms_game *game);

/* ------------------------------------------------------------------------
 * TEST-ONLY hooks (tests/c only; never called by engine.c or wasm_api.c)
 * ------------------------------------------------------------------------ */

/* Whether the mines have been placed (the first reveal happened). */
bool ms_game_test_placed(const ms_game *game);

/* Whether `index` holds a mine (false before placement or out of range). */
bool ms_game_test_is_mine(const ms_game *game, uint32_t index);

/* Number of cells holding a mine (0 before placement). */
uint32_t ms_game_test_mine_count(const ms_game *game);

/* Overwrites the revision (<= MS_REVISION_MAX) to exercise the
 * MS_REVISION_MAX guard. */
void ms_game_test_set_revision(ms_game *game, uint32_t revision);

#ifdef __cplusplus
}
#endif
