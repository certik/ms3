/*
 * game.c - Minesweeper rules for one game (port of minesweeper/game.py's
 * Game). See engine.h ("Game rules") for the contract and game.h for the
 * engine accessors and test hooks.
 *
 * Every game is one rt_alloc block holding the struct, the per-cell state,
 * deduction marks, the flood/placement queue and the optional test layout,
 * so creation is the only operation that allocates: actions, placement and
 * deduction batches validate everything first and then cannot fail.
 */

#include "game.h"

#include <base/mem.h>

#include "runtime.h"

#define GAME_LIVE 0x454D4147u /* "GAME" */

/* Private cell byte. Bits 0-3 hold the true mine-neighbor count of every
 * cell (mines included); ms_game_view shows only what the player may see.
 * The flag bits coincide with the public view's. */
#define CELL_ADJACENT 0x0Fu
#define CELL_REVEALED 0x10u
#define CELL_FLAGGED 0x20u
#define CELL_MINE 0x40u
#define CELL_EXPLODED 0x80u

_Static_assert(CELL_REVEALED == MS_CELL_REVEALED && CELL_FLAGGED == MS_CELL_FLAGGED &&
                   CELL_MINE == MS_CELL_MINE && CELL_EXPLODED == MS_CELL_EXPLODED,
               "private cell bits match the public view");

/* Deduction-batch and layout-validation marks (zero between calls). */
#define MARK_SAFE 0x01u
#define MARK_MINE 0x02u
#define MARK_SEEN 0x04u

struct ms_game {
    uint32_t state;
    rt_mem *mem;
    rt_clock *clock;
    uint32_t width;
    uint32_t height;
    uint32_t mines;
    uint32_t cells;
    uint32_t generation;
    uint32_t revision;
    uint32_t status;
    uint32_t flags;
    uint32_t revealed;    /* revealed cells, exploded mines included */
    uint32_t hidden_safe; /* safe cells still hidden */
    bool placed;
    bool has_layout;
    uint32_t layout_count;
    double started_ms;
    double finished_ms;
    rt_rng rng;
    uint8_t *cell;
    uint8_t *mark;
    uint32_t *queue; /* cells entries: flood queue, placement candidates */
    uint32_t *layout;
};

static bool game_ok(const ms_game *game) {
    return game != NULL && game->state == GAME_LIVE;
}

static bool aligned8(const void *ptr) {
    return ptr != NULL && ((uintptr_t)ptr & 7u) == 0;
}

static bool game_over(const ms_game *game) {
    return game->status == MS_GAME_WON || game->status == MS_GAME_LOST;
}

static size_t align_up(size_t size, size_t to) {
    return (size + to - 1u) & ~(to - 1u);
}

/* ======================================================================
 * Creation
 * ====================================================================== */

static ms_status game_create(ms_game **out, rt_mem *mem, rt_clock *clock, uint32_t width,
                             uint32_t height, uint32_t mines, uint32_t generation,
                             uint64_t seed, bool has_layout, const uint32_t *layout,
                             uint32_t layout_count) {
    if (out == NULL || mem == NULL || clock == NULL) return MS_ERR_INVALID_BUFFER;
    if (has_layout && layout == NULL && layout_count != 0) return MS_ERR_INVALID_BUFFER;
    ms_status status = ms_game_check_config(width, height, mines);
    if (status != MS_OK) return status;
    uint32_t cells = width * height;
    if (layout_count > cells) return MS_ERR_INVALID_BUFFER;
    if (generation == 0 || generation > MS_GENERATION_MAX) return MS_ERR_INVALID_REVISION;

    /* cells <= 6400: none of these sums can overflow. */
    size_t cell_at = align_up(sizeof(ms_game), 16u);
    size_t mark_at = cell_at + cells;
    size_t queue_at = align_up(mark_at + cells, 4u);
    size_t layout_at = queue_at + (size_t)cells * sizeof(uint32_t);
    size_t total = layout_at + (size_t)layout_count * sizeof(uint32_t);
    uint8_t *block = (uint8_t *)rt_alloc(mem, total);
    if (block == NULL) return MS_ERR_RESOURCE_EXHAUSTED;

    ms_game *game = (ms_game *)block;
    base_memset(game, 0, sizeof(*game));
    game->mem = mem;
    game->clock = clock;
    game->width = width;
    game->height = height;
    game->mines = mines;
    game->cells = cells;
    game->generation = generation;
    game->revision = 0;
    game->status = MS_GAME_READY;
    game->hidden_safe = cells - mines;
    game->has_layout = has_layout;
    game->layout_count = layout_count;
    game->cell = block + cell_at;
    game->mark = block + mark_at;
    game->queue = (uint32_t *)(block + queue_at);
    game->layout = (uint32_t *)(block + layout_at);
    base_memset(game->cell, 0, (size_t)cells * 2u);
    if (layout_count != 0) {
        base_memcpy(game->layout, layout, (size_t)layout_count * sizeof(uint32_t));
    }
    rt_rng_seed(&game->rng, seed);
    game->state = GAME_LIVE;
    *out = game;
    return MS_OK;
}

ms_status ms_game_create(ms_game **out, rt_mem *mem, rt_clock *clock, uint32_t width,
                         uint32_t height, uint32_t mines, uint32_t generation,
                         uint64_t seed) {
    return game_create(out, mem, clock, width, height, mines, generation, seed, false, NULL, 0);
}

ms_status ms_game_create_with_layout(ms_game **out, rt_mem *mem, rt_clock *clock,
                                     uint32_t width, uint32_t height, uint32_t mines,
                                     uint32_t generation, const uint32_t *layout,
                                     uint32_t layout_count) {
    return game_create(out, mem, clock, width, height, mines, generation, 0, true, layout,
                       layout_count);
}

void ms_game_destroy(ms_game *game) {
    if (!game_ok(game)) return;
    game->state = 0;
    rt_free(game->mem, game);
}

/* ======================================================================
 * Rules
 * ====================================================================== */

/* Whether `index` is in the clipped 3x3 block around `center`. */
static bool in_start_block(const ms_game *game, uint32_t center, uint32_t index) {
    uint32_t r0 = center / game->width, c0 = center % game->width;
    uint32_t r1 = index / game->width, c1 = index % game->width;
    uint32_t dr = r0 > r1 ? r0 - r1 : r1 - r0;
    uint32_t dc = c0 > c1 ? c0 - c1 : c1 - c0;
    return dr <= 1 && dc <= 1;
}

/* The fixed test layout must hold `mines` distinct cells on the board and
 * outside the start block. Uses (and clears) MARK_SEEN. */
static bool layout_valid(ms_game *game, uint32_t first) {
    if (game->layout_count != game->mines) return false;
    bool ok = true;
    for (uint32_t k = 0; k < game->layout_count && ok; k++) {
        uint32_t index = game->layout[k];
        if (index >= game->cells || in_start_block(game, first, index) ||
            (game->mark[index] & MARK_SEEN)) {
            ok = false;
        } else {
            game->mark[index] |= MARK_SEEN;
        }
    }
    for (uint32_t k = 0; k < game->layout_count; k++) {
        if (game->layout[k] < game->cells) game->mark[game->layout[k]] &= (uint8_t)~MARK_SEEN;
    }
    return ok;
}

/* Lazy placement on the first reveal of `first`: uniform over the cells
 * outside its clipped 3x3 block (or the validated test layout). */
static ms_status place_mines(ms_game *game, uint32_t first) {
    if (game->has_layout) {
        if (!layout_valid(game, first)) return MS_ERR_INTERNAL;
        for (uint32_t k = 0; k < game->mines; k++) game->cell[game->layout[k]] |= CELL_MINE;
    } else {
        uint32_t count = 0;
        for (uint32_t i = 0; i < game->cells; i++) {
            if (!in_start_block(game, first, i)) game->queue[count++] = i;
        }
        rt_rng_choose(&game->rng, game->queue, count, game->mines);
        for (uint32_t k = 0; k < game->mines; k++) game->cell[game->queue[k]] |= CELL_MINE;
    }
    uint32_t around[8];
    for (uint32_t i = 0; i < game->cells; i++) {
        if (!(game->cell[i] & CELL_MINE)) continue;
        uint32_t n = rt_grid_neighbors(game->width, game->height, i, around);
        for (uint32_t k = 0; k < n; k++) game->cell[around[k]]++; /* <= 8: stays in bits 0-3 */
    }
    game->placed = true;
    game->status = MS_GAME_PLAYING;
    game->started_ms = rt_clock_now(game->clock);
    return MS_OK;
}

/* Reveals `start` and, iteratively, the region around zero clues; flagged
 * cells stay hidden. Neighbors of a zero are never mines. */
static void flood(ms_game *game, uint32_t start) {
    if (game->cell[start] & (CELL_REVEALED | CELL_FLAGGED | CELL_MINE)) return;
    game->cell[start] |= CELL_REVEALED;
    game->revealed++;
    game->hidden_safe--;
    uint32_t head = 0, tail = 0;
    game->queue[tail++] = start;
    uint32_t around[8];
    while (head < tail) {
        uint32_t index = game->queue[head++];
        if (game->cell[index] & CELL_ADJACENT) continue;
        uint32_t n = rt_grid_neighbors(game->width, game->height, index, around);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t next = around[k];
            if (game->cell[next] & (CELL_REVEALED | CELL_FLAGGED)) continue;
            game->cell[next] |= CELL_REVEALED;
            game->revealed++;
            game->hidden_safe--;
            game->queue[tail++] = next; /* each cell is queued at most once */
        }
    }
}

static void finish(ms_game *game, uint32_t status) {
    game->status = status;
    game->finished_ms = rt_clock_now(game->clock);
}

static void detonate(ms_game *game, uint32_t index) {
    game->cell[index] |= CELL_REVEALED | CELL_EXPLODED;
    game->revealed++;
}

/* Won once every safe cell is revealed; then every mine is flagged (all
 * existing flags are necessarily on mines). */
static void check_win(ms_game *game) {
    if (game->hidden_safe != 0) return;
    finish(game, MS_GAME_WON);
    for (uint32_t i = 0; i < game->cells; i++) {
        if ((game->cell[i] & CELL_MINE) && !(game->cell[i] & CELL_FLAGGED)) {
            game->cell[i] |= CELL_FLAGGED;
            game->flags++;
        }
    }
}

static ms_status reveal(ms_game *game, uint32_t index, bool *changed) {
    *changed = false;
    if (game->cell[index] & (CELL_REVEALED | CELL_FLAGGED)) return MS_OK;
    if (!game->placed) {
        ms_status status = place_mines(game, index);
        if (status != MS_OK) return status;
    }
    if (game->cell[index] & CELL_MINE) {
        detonate(game, index);
        finish(game, MS_GAME_LOST);
    } else {
        flood(game, index);
        check_win(game);
    }
    *changed = true;
    return MS_OK;
}

static bool toggle_flag(ms_game *game, uint32_t index) {
    if (game->cell[index] & CELL_REVEALED) return false;
    game->cell[index] ^= CELL_FLAGGED;
    if (game->cell[index] & CELL_FLAGGED) {
        game->flags++;
    } else {
        game->flags--;
    }
    return true;
}

/* Opens every hidden unflagged neighbor of a revealed number whose flagged
 * neighbor count matches: safe ones flood first, then every mine among
 * them (wrong flags) explodes. */
static bool chord(ms_game *game, uint32_t index) {
    uint8_t c = game->cell[index];
    if (!(c & CELL_REVEALED) || (c & CELL_MINE)) return false;
    uint32_t around[8];
    uint32_t n = rt_grid_neighbors(game->width, game->height, index, around);
    uint32_t flagged = 0, targets = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint8_t b = game->cell[around[k]];
        if (b & CELL_FLAGGED) flagged++;
        if (!(b & (CELL_REVEALED | CELL_FLAGGED))) targets++;
    }
    if (flagged != (c & CELL_ADJACENT) || targets == 0) return false;
    bool hit = false;
    for (uint32_t k = 0; k < n; k++) {
        uint8_t b = game->cell[around[k]];
        if (!(b & (CELL_REVEALED | CELL_FLAGGED | CELL_MINE))) flood(game, around[k]);
    }
    for (uint32_t k = 0; k < n; k++) {
        uint8_t b = game->cell[around[k]];
        if ((b & CELL_MINE) && !(b & (CELL_REVEALED | CELL_FLAGGED))) {
            detonate(game, around[k]);
            hit = true;
        }
    }
    if (hit) {
        finish(game, MS_GAME_LOST);
    } else {
        check_win(game);
    }
    return true;
}

/* ======================================================================
 * Actions and deduction batches
 *
 * *changed is written only on MS_OK, after the batch has been read and
 * applied, so an output that aliases caller memory can never alter or
 * corrupt a rejected request.
 * ====================================================================== */

static ms_status check_revision(const ms_game *game, uint32_t generation, uint32_t revision) {
    if (generation == 0 || generation > MS_GENERATION_MAX || revision > MS_REVISION_MAX) {
        return MS_ERR_INVALID_REVISION;
    }
    if (generation != game->generation || revision != game->revision) {
        return MS_ERR_STALE_REVISION;
    }
    return MS_OK;
}

ms_status ms_game_act(ms_game *game, uint32_t generation, uint32_t revision, uint32_t action,
                      uint32_t row, uint32_t col, bool *changed) {
    if (!game_ok(game)) return MS_ERR_INVALID_BUFFER;
    if (action < MS_ACTION_REVEAL || action > MS_ACTION_CHORD) return MS_ERR_INVALID_ACTION;
    if (row >= game->height || col >= game->width) return MS_ERR_OUT_OF_BOUNDS;
    ms_status status = check_revision(game, generation, revision);
    if (status != MS_OK) return status;
    if (game_over(game)) return MS_ERR_GAME_OVER;
    if (game->revision >= MS_REVISION_MAX) return MS_ERR_RESOURCE_EXHAUSTED;

    uint32_t index = row * game->width + col;
    bool did = false;
    switch (action) {
    case MS_ACTION_REVEAL:
        status = reveal(game, index, &did);
        if (status != MS_OK) return status;
        break;
    case MS_ACTION_FLAG:
        did = toggle_flag(game, index);
        break;
    default:
        did = chord(game, index);
        break;
    }
    if (did) game->revision++;
    if (changed) *changed = did;
    return MS_OK;
}

ms_status ms_game_apply_deductions(ms_game *game, uint32_t generation, uint32_t revision,
                                   const uint32_t *safe, uint32_t safe_count,
                                   const uint32_t *mines, uint32_t mine_count, bool *changed) {
    if (!game_ok(game)) return MS_ERR_INVALID_BUFFER;
    if ((safe == NULL && safe_count != 0) || (mines == NULL && mine_count != 0)) {
        return MS_ERR_INVALID_BUFFER;
    }
    ms_status status = check_revision(game, generation, revision);
    if (status != MS_OK) return status;
    if (game_over(game)) return MS_ERR_GAME_OVER;
    if (game->status == MS_GAME_READY) return MS_ERR_GAME_NOT_STARTED;

    /* All-or-nothing validation; duplicates within a list are fine. */
    status = MS_OK;
    for (uint32_t k = 0; k < safe_count && status == MS_OK; k++) {
        uint32_t index = safe[k];
        if (index >= game->cells || (game->cell[index] & CELL_REVEALED)) {
            status = MS_ERR_INVALID_DEDUCTIONS;
        } else {
            game->mark[index] |= MARK_SAFE;
        }
    }
    for (uint32_t k = 0; k < mine_count && status == MS_OK; k++) {
        uint32_t index = mines[k];
        if (index >= game->cells || (game->cell[index] & CELL_REVEALED) ||
            (game->mark[index] & MARK_SAFE)) {
            status = MS_ERR_INVALID_DEDUCTIONS;
        } else {
            game->mark[index] |= MARK_MINE;
        }
    }
    if (status == MS_OK && game->revision >= MS_REVISION_MAX) status = MS_ERR_RESOURCE_EXHAUSTED;
    if (status != MS_OK) {
        base_memset(game->mark, 0, game->cells);
        return status;
    }

    bool any = false;
    for (uint32_t i = 0; i < game->cells; i++) {
        if ((game->mark[i] & MARK_MINE) && !(game->cell[i] & CELL_FLAGGED)) {
            any = toggle_flag(game, i) || any;
        }
    }
    /* Clear every proven-safe flag before flooding so no safe region stays
     * blocked behind a wrong flag. */
    for (uint32_t i = 0; i < game->cells; i++) {
        if ((game->mark[i] & MARK_SAFE) && (game->cell[i] & CELL_FLAGGED)) {
            any = toggle_flag(game, i) || any;
        }
    }
    for (uint32_t i = 0; i < game->cells && !game_over(game); i++) {
        if (!(game->mark[i] & MARK_SAFE)) continue;
        bool did = false;
        reveal(game, i, &did); /* the game is placed: cannot fail */
        any = did || any;
    }
    base_memset(game->mark, 0, game->cells);
    if (any) game->revision++;
    if (changed) *changed = any;
    return MS_OK;
}

/* ======================================================================
 * Public view and observation
 * ====================================================================== */

/* Whole milliseconds, ties to even (Python's round(seconds, 3)). */
static double whole_ms(double ms) {
    if (!(ms > 0.0)) return 0.0;
    if (ms >= 9007199254740992.0) return ms; /* 2^53: already integral */
    int64_t whole = (int64_t)ms;
    double frac = ms - (double)whole;
    if (frac > 0.5 || (frac == 0.5 && (whole & 1))) whole++;
    return (double)whole;
}

ms_status ms_game_view(ms_game *game, void *view, size_t view_len) {
    if (!game_ok(game)) return MS_ERR_INVALID_BUFFER;
    if (!aligned8(view) || view_len != ms_view_size(game->width, game->height)) {
        return MS_ERR_INVALID_BUFFER;
    }
    double elapsed = 0.0;
    if (game->status == MS_GAME_PLAYING) {
        elapsed = whole_ms(rt_clock_now(game->clock) - game->started_ms);
    } else if (game_over(game)) {
        elapsed = whole_ms(game->finished_ms - game->started_ms);
    }
    base_memset(view, 0, view_len);
    ms_view_header *h = (ms_view_header *)view;
    h->magic = MS_MAGIC_VIEW;
    h->version = MS_ABI_VERSION;
    h->generation = game->generation;
    h->revision = game->revision;
    h->status = game->status;
    h->width = game->width;
    h->height = game->height;
    h->mines = game->mines;
    h->flags = game->flags;
    h->revealed = game->revealed;
    h->elapsed_ms = elapsed;

    bool terminal = game_over(game);
    uint8_t *out = ms_view_cells(view);
    for (uint32_t i = 0; i < game->cells; i++) {
        uint8_t c = game->cell[i];
        uint8_t b;
        if ((c & CELL_REVEALED) && !(c & CELL_MINE)) {
            b = (uint8_t)(CELL_REVEALED | (c & CELL_ADJACENT));
        } else {
            b = (uint8_t)(MS_CELL_NO_ADJACENT | (c & (CELL_REVEALED | CELL_FLAGGED | CELL_EXPLODED)));
            if (terminal && (c & CELL_MINE)) b |= MS_CELL_MINE;
        }
        out[i] = b;
    }
    return MS_OK;
}

ms_status ms_game_observe(const ms_game *game, uint32_t generation, uint32_t revision,
                          void *obs, size_t obs_len) {
    if (!game_ok(game)) return MS_ERR_INVALID_BUFFER;
    if (!aligned8(obs) || obs_len != ms_obs_size(game->width, game->height)) {
        return MS_ERR_INVALID_BUFFER;
    }
    ms_status status = check_revision(game, generation, revision);
    if (status != MS_OK) return status;
    if (game->status == MS_GAME_READY) return MS_ERR_GAME_NOT_STARTED;
    if (game_over(game)) return MS_ERR_GAME_OVER;
    status = ms_obs_init(obs, obs_len, game->width, game->height, game->mines);
    if (status != MS_OK) return status;
    uint8_t *clues = ms_obs_clues(obs);
    uint32_t count = 0;
    for (uint32_t i = 0; i < game->cells; i++) {
        uint8_t c = game->cell[i];
        if ((c & CELL_REVEALED) && !(c & CELL_MINE)) {
            clues[i] = (uint8_t)(c & CELL_ADJACENT);
            count++;
        }
    }
    ((ms_obs_header *)obs)->revealed = count;
    return MS_OK;
}

/* ======================================================================
 * game.h: engine accessors and test hooks
 * ====================================================================== */

uint32_t ms_game_get_status(const ms_game *game) {
    return game_ok(game) ? game->status : 0;
}

uint32_t ms_game_get_revision(const ms_game *game) {
    return game_ok(game) ? game->revision : 0;
}

uint32_t ms_game_get_generation(const ms_game *game) {
    return game_ok(game) ? game->generation : 0;
}

bool ms_game_test_placed(const ms_game *game) {
    return game_ok(game) && game->placed;
}

bool ms_game_test_is_mine(const ms_game *game, uint32_t index) {
    return game_ok(game) && index < game->cells && (game->cell[index] & CELL_MINE) != 0;
}

uint32_t ms_game_test_mine_count(const ms_game *game) {
    if (!game_ok(game)) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < game->cells; i++) count += (game->cell[i] & CELL_MINE) != 0;
    return count;
}

void ms_game_test_set_revision(ms_game *game, uint32_t revision) {
    if (game_ok(game) && revision <= MS_REVISION_MAX) game->revision = revision;
}
