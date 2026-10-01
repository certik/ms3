#pragma once

/*
 * engine.h - the shared, versioned contract of the Minesweeper C engine.
 *
 * Everything that crosses a module boundary (game rules, probability solver,
 * engine service, WebAssembly exports, JavaScript adapter) is defined here:
 * status codes, limits, game/observation/result buffer layouts, inference
 * limits and the C entry points each module implements.
 *
 *   c/runtime.c      ABI helpers declared below (validation, layout builders)
 *   c/game.c         ms_game_*      (game rules, public view, observation)
 *   c/probability.c  ms_solve       (exact counting and sampling)
 *   c/engine.c       ms_engine_*    (per-tab service: current game, result
 *                                    cache, atomic autosolve)
 *   c/wasm_api.c     reactor exports wrapping the functions above
 *
 * Layout rules shared by every buffer below
 * -----------------------------------------
 * - A buffer is one contiguous block: a fixed-width header (only uint32_t,
 *   uint64_t and double fields, no pointers) followed by arrays.
 * - Byte order is little-endian (WebAssembly and every native target).
 * - Buffers must be 8-byte aligned. rt_alloc()/rt_bump() and the reactor's
 *   buffer exports return 16-byte aligned memory. (corec's wasm_buddy_alloc
 *   export is only 4-byte aligned on wasm32 and must not be used for them.)
 * - A buffer's length is exactly the ms_*_size() of its dimensions, always a
 *   multiple of 8; trailing padding bytes are zero.
 * - The caller owns every buffer. Functions never keep a pointer to a caller
 *   buffer after returning. On error an output buffer's contents are
 *   unspecified and must be ignored; mutating functions change nothing.
 * - Pointer/length/alignment problems (MS_ERR_INVALID_BUFFER) are checked
 *   before every other condition.
 * - Cells are indexed row-major: index = row * width + col.
 *
 * Every header starts with a magic number identifying the buffer kind and
 * MS_ABI_VERSION. Any change to a layout, enum value or documented meaning
 * below requires a new MS_ABI_VERSION; the JS adapter refuses mismatches.
 */

#include <base/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime types (defined in runtime.h); only pointers appear here. */
typedef struct rt_mem rt_mem;
typedef struct rt_clock rt_clock;

#define MS_ABI_VERSION 1u

#define MS_MAGIC_VIEW        0x4D530001u /* ms_view_header */
#define MS_MAGIC_OBSERVATION 0x4D530002u /* ms_obs_header */
#define MS_MAGIC_LIMITS      0x4D530003u /* ms_infer_limits */
#define MS_MAGIC_RESULT      0x4D530004u /* ms_result_header */

#if defined(_MSC_VER)
#define MS_OFFSETOF(type, member) ((size_t)&(((type *)0)->member))
#else
#define MS_OFFSETOF(type, member) __builtin_offsetof(type, member)
#endif

/* ------------------------------------------------------------------------
 * Status codes
 *
 * Every fallible function returns one of these (WASM: i32). Numbers and
 * names are stable; ms_status_name() gives the snake_case code, which equals
 * the Python server's error code wherever one existed.
 *
 *   1..31   invalid input: rejected before any change.
 *   32..47  conflict: well-formed, but not applicable to the current game.
 *   48      the observation admits no mine layout.
 *   64..    resource exhaustion or internal failure.
 *
 * Budget exhaustion inside the solver is NOT an error: it yields an
 * MS_PROB_UNAVAILABLE result (MS_OK) that still carries sound proofs.
 * ------------------------------------------------------------------------ */
typedef enum ms_status {
    MS_OK = 0,

    MS_ERR_INVALID_WIDTH = 1,       /* "invalid_width": not 5..80 */
    MS_ERR_INVALID_HEIGHT = 2,      /* "invalid_height": not 5..80 */
    MS_ERR_INVALID_MINES = 3,       /* "invalid_mines": not 1..width*height-9 */
    MS_ERR_INVALID_ACTION = 4,      /* "invalid_action": unknown ms_action */
    MS_ERR_OUT_OF_BOUNDS = 5,       /* "out_of_bounds": row/col off the board */
    MS_ERR_INVALID_REVISION = 6,    /* "invalid_revision": generation not
                                       1..MS_GENERATION_MAX or revision above
                                       MS_REVISION_MAX */
    MS_ERR_INVALID_DEDUCTIONS = 7,  /* "invalid_deductions": bad proof mask */
    MS_ERR_INVALID_OBSERVATION = 8, /* "invalid_observation" */
    MS_ERR_INVALID_LIMITS = 9,      /* "invalid_limits" */
    MS_ERR_INVALID_RESULT = 10,     /* "invalid_result": a probability result
                                       contradicts its observation/contract */
    MS_ERR_INVALID_BUFFER = 11,     /* "invalid_buffer": NULL, misaligned,
                                       wrong length, magic or ABI version */

    MS_ERR_STALE_REVISION = 32,     /* "stale_revision": generation or
                                       revision is not the current one */
    MS_ERR_GAME_OVER = 33,          /* "game_over": game already won/lost */
    MS_ERR_GAME_NOT_STARTED = 34,   /* "game_not_started": no reveal yet */

    MS_ERR_INCONSISTENT = 48,       /* "inconsistent_observation": the clues
                                       and mine total admit no layout */

    MS_ERR_RESOURCE_EXHAUSTED = 64, /* "resource_exhausted": allocation
                                       failed, a byte budget or counter limit
                                       was reached; nothing was changed */
    MS_ERR_INTERNAL = 65            /* "internal_error": a broken invariant
                                       (engine/solver bug, or host misuse such
                                       as calling before initialization) */
} ms_status;

/* Stable snake_case name of a status ("ok", "invalid_width", ...);
 * "unknown_status" for values outside the enum. */
const char *ms_status_name(ms_status status);

static inline bool ms_status_is_input_error(ms_status status) {
    return status >= 1 && status < 32;
}

static inline bool ms_status_is_conflict(ms_status status) {
    return status >= 32 && status < 48;
}

/* ------------------------------------------------------------------------
 * Limits
 * ------------------------------------------------------------------------ */
#define MS_GAME_MIN_SIDE 5u
#define MS_GAME_MAX_SIDE 80u
#define MS_GAME_MIN_MINES 1u
/* The first revealed cell and its (up to eight) neighbors never hold a mine,
 * so mines <= width * height - MS_GAME_SAFE_START_CELLS. */
#define MS_GAME_SAFE_START_CELLS 9u
#define MS_GAME_MAX_CELLS (MS_GAME_MAX_SIDE * MS_GAME_MAX_SIDE)

/* The solver accepts any width, height >= 1 with width * height <= this
 * (thin fixtures such as 3001x1 included) and any 0..cells mine total. */
#define MS_SOLVER_MAX_CELLS 6400u

/* Generations number the games of one engine instance: 1, 2, ...
 * Revisions count the state-changing operations of one game: 0, 1, ...
 * Both stay below 2^31, so signed and unsigned i32 views agree in JS. */
#define MS_GENERATION_MAX 0x7FFFFFFFu
#define MS_REVISION_MAX 0x7FFFFFFFu

#define MS_MAX_CLUE 8u
#define MS_CLUE_HIDDEN 0xFFu /* observation byte of an unrevealed cell */

/* Python's validate_config order: width, then height, then mines. */
ms_status ms_game_check_config(uint32_t width, uint32_t height, uint32_t mines);

/* Number of cells, or 0 unless 1 <= width, height and
 * width * height <= MS_SOLVER_MAX_CELLS (which covers every game). */
static inline uint32_t ms_cell_count(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || width > MS_SOLVER_MAX_CELLS ||
        height > MS_SOLVER_MAX_CELLS / width) {
        return 0;
    }
    return width * height;
}

static inline size_t ms_pad8(size_t size) {
    return (size + 7u) & ~(size_t)7u;
}

/* ------------------------------------------------------------------------
 * Game status, actions and the public view
 *
 * The view is the only game state that leaves the game module. It mirrors
 * Python's Game.public_state(); JS rebuilds its GameState by marshalling:
 *
 *   id               JS-local label derived from generation (not a secret)
 *   width, height, mines, revision, flags   header fields
 *   status           MS_GAME_* -> 'ready' | 'playing' | 'won' | 'lost'
 *   elapsed          elapsed_ms / 1000 (Python's elapsed_seconds)
 *   revealedCount    header revealed
 *   cells[i].revealed  (b & MS_CELL_REVEALED) != 0
 *   cells[i].flagged   (b & MS_CELL_FLAGGED) != 0
 *   cells[i].exploded  (b & MS_CELL_EXPLODED) != 0
 *   cells[i].mine      won/lost: (b & MS_CELL_MINE) != 0; otherwise null
 *   cells[i].adjacent  (b & 0x0F) == MS_CELL_NO_ADJACENT ? null : b & 0x0F
 * ------------------------------------------------------------------------ */
typedef enum ms_game_status {
    MS_GAME_READY = 1,   /* "ready": no reveal yet, mines not placed */
    MS_GAME_PLAYING = 2, /* "playing" */
    MS_GAME_WON = 3,     /* "won" */
    MS_GAME_LOST = 4     /* "lost" */
} ms_game_status;

typedef enum ms_action {
    MS_ACTION_REVEAL = 1, /* "reveal" */
    MS_ACTION_FLAG = 2,   /* "flag": toggle */
    MS_ACTION_CHORD = 3   /* "chord" */
} ms_action;

/* One byte per cell:
 *   bits 0-3  adjacent mine count 0..8 of a revealed safe cell, otherwise
 *             MS_CELL_NO_ADJACENT (hidden cells and revealed mines);
 *   bit 4     revealed;  bit 5 flagged (never together with revealed);
 *   bit 6     mine: set only in won/lost games, for every mine;
 *   bit 7     exploded: the mine(s) revealed by the losing move.
 * While a game is ready/playing no byte reveals anything about hidden cells
 * beyond the player's own flags. */
#define MS_CELL_ADJACENT_MASK 0x0Fu
#define MS_CELL_NO_ADJACENT   0x0Fu
#define MS_CELL_REVEALED      0x10u
#define MS_CELL_FLAGGED       0x20u
#define MS_CELL_MINE          0x40u
#define MS_CELL_EXPLODED      0x80u

typedef struct ms_view_header {
    uint32_t magic;      /* MS_MAGIC_VIEW */
    uint32_t version;    /* MS_ABI_VERSION */
    uint32_t generation; /* 1..MS_GENERATION_MAX */
    uint32_t revision;   /* 0..MS_REVISION_MAX */
    uint32_t status;     /* ms_game_status */
    uint32_t width;      /* 5..80 */
    uint32_t height;     /* 5..80 */
    uint32_t mines;      /* 1..width*height-9 */
    uint32_t flags;      /* number of flagged cells */
    uint32_t revealed;   /* number of revealed cells (incl. exploded mines) */
    double elapsed_ms;   /* whole milliseconds since the first reveal; 0 while
                            ready, frozen once won/lost */
} ms_view_header;
/* followed by: uint8_t cells[width * height] at offset 48 */

_Static_assert(sizeof(ms_view_header) == 48, "ms_view_header layout");
_Static_assert(MS_OFFSETOF(ms_view_header, revealed) == 36, "ms_view_header layout");
_Static_assert(MS_OFFSETOF(ms_view_header, elapsed_ms) == 40, "ms_view_header layout");

/* Buffer size for a width x height view; 0 for invalid dimensions. */
static inline size_t ms_view_size(uint32_t width, uint32_t height) {
    uint32_t cells = ms_cell_count(width, height);
    return cells ? ms_pad8(sizeof(ms_view_header) + cells) : 0;
}

static inline uint8_t *ms_view_cells(const void *view) {
    return (uint8_t *)view + sizeof(ms_view_header);
}

/* Structural self-check of a view, for tests and adapters: header fields,
 * per-cell bit rules above, flag/revealed counts, status-specific invariants
 * (ready: nothing revealed, elapsed 0; playing: 1 <= revealed < safe cells,
 * no mine bits; won: every safe cell revealed and every mine flagged; lost:
 * at least one exploded mine) and, once won/lost, every revealed clue equal
 * to its mine-neighbor count. MS_ERR_INVALID_BUFFER for pointer/length/
 * magic/version problems, MS_ERR_INTERNAL for any broken invariant. */
ms_status ms_view_validate(const void *view, size_t view_len);

/* ------------------------------------------------------------------------
 * Public observation: the solver's only input
 *
 * Exactly Python's (width, height, total_mines, revealed_clues()): the clue
 * of every revealed safe cell and nothing else - no flags, no generation or
 * revision, nothing derived from the hidden layout. Request routing
 * (generation, revision, request id) travels beside it, never inside.
 * ------------------------------------------------------------------------ */
typedef struct ms_obs_header {
    uint32_t magic;       /* MS_MAGIC_OBSERVATION */
    uint32_t version;     /* MS_ABI_VERSION */
    uint32_t width;       /* >= 1 */
    uint32_t height;      /* >= 1, width * height <= MS_SOLVER_MAX_CELLS */
    uint32_t total_mines; /* 0..width*height */
    uint32_t revealed;    /* number of clue bytes != MS_CLUE_HIDDEN */
    uint32_t reserved0;   /* 0 */
    uint32_t reserved1;   /* 0 */
} ms_obs_header;
/* followed by: uint8_t clues[width * height] at offset 32, each 0..8 (a
 * revealed safe cell's clue) or MS_CLUE_HIDDEN. */

_Static_assert(sizeof(ms_obs_header) == 32, "ms_obs_header layout");

static inline size_t ms_obs_size(uint32_t width, uint32_t height) {
    uint32_t cells = ms_cell_count(width, height);
    return cells ? ms_pad8(sizeof(ms_obs_header) + cells) : 0;
}

static inline uint8_t *ms_obs_clues(const void *obs) {
    return (uint8_t *)obs + sizeof(ms_obs_header);
}

/* Writes an observation with every cell hidden. MS_ERR_INVALID_BUFFER if
 * obs_len != ms_obs_size(width, height) (or NULL/misaligned);
 * MS_ERR_INVALID_OBSERVATION for bad dimensions or total_mines > cells. */
ms_status ms_obs_init(void *obs, size_t obs_len, uint32_t width, uint32_t height,
                      uint32_t total_mines);

/* Sets one cell's clue (0..8, or MS_CLUE_HIDDEN to hide it again) and keeps
 * the header's revealed count in sync. obs must be a valid observation;
 * MS_ERR_INVALID_OBSERVATION for a bad index or clue value. */
ms_status ms_obs_set_clue(void *obs, uint32_t index, uint8_t clue);

/* Structural validation: buffer, magic/version, dimensions, total_mines <=
 * cells, clue values, revealed count, zero reserved/padding bytes.
 * Returns MS_OK, MS_ERR_INVALID_BUFFER or MS_ERR_INVALID_OBSERVATION.
 * Consistency (e.g. more mines than hidden cells, a clue larger than its
 * hidden neighbor count) is the solver's job: MS_ERR_INCONSISTENT. */
ms_status ms_obs_validate(const void *obs, size_t obs_len);

/* 64-bit fingerprint of a valid observation (all ms_obs_size() bytes,
 * rt_hash64 with a fixed seed). Identical on every platform. It binds a
 * probability result to the exact observation it answers, and is the
 * solver's default sampling seed. */
uint64_t ms_obs_hash(const void *obs);

/* ------------------------------------------------------------------------
 * Inference limits (Python calculate_probabilities budgets + C accounting)
 * ------------------------------------------------------------------------ */
#define MS_DEFAULT_TIME_BUDGET_MS 1500.0
#define MS_DEFAULT_NODE_BUDGET 100000u
#define MS_DEFAULT_SAMPLE_BUDGET 2000u
#define MS_DEFAULT_MIN_EFFECTIVE_SAMPLES 50.0
#define MS_DEFAULT_MAX_STORED_ENTRIES 1500000u
#define MS_DEFAULT_MEMORY_BUDGET ((uint64_t)256u << 20)

#define MS_LIMIT_EXPLICIT_SEED 0x1u /* use `seed` instead of ms_obs_hash() */
#define MS_LIMIT_KNOWN_FLAGS MS_LIMIT_EXPLICIT_SEED

typedef struct ms_infer_limits {
    uint32_t magic;                /* MS_MAGIC_LIMITS */
    uint32_t version;              /* MS_ABI_VERSION */
    uint32_t node_budget;          /* memoised forward-counting node expansions,
                                      all components together */
    uint32_t sample_budget;        /* importance-sampling proposals shared by
                                      all hard components */
    uint32_t max_stored_entries;   /* retained DP coefficients + edges guard */
    uint32_t flags;                /* MS_LIMIT_* */
    double time_budget_ms;         /* whole call, measured on the injected
                                      clock; >= 0, +infinity allowed */
    double min_effective_samples;  /* ESS every sampled component must reach
                                      for an approximate result; finite >= 0 */
    uint64_t memory_budget_bytes;  /* cap on the solver's own workspace bytes
                                      (rt_mem charge; clamped to SIZE_MAX) */
    uint64_t seed;                 /* sampling seed iff MS_LIMIT_EXPLICIT_SEED */
} ms_infer_limits;

_Static_assert(sizeof(ms_infer_limits) == 56, "ms_infer_limits layout");
_Static_assert(MS_OFFSETOF(ms_infer_limits, time_budget_ms) == 24, "ms_infer_limits layout");
_Static_assert(MS_OFFSETOF(ms_infer_limits, seed) == 48, "ms_infer_limits layout");

/* Fills the defaults above (the current server policy). */
void ms_limits_default(ms_infer_limits *limits);

/* MS_OK, MS_ERR_INVALID_BUFFER (NULL/misaligned/magic/version) or
 * MS_ERR_INVALID_LIMITS (NaN or negative time, non-finite or negative ESS,
 * unknown flags). Zero budgets are valid and force unavailable results. */
ms_status ms_limits_validate(const ms_infer_limits *limits);

/* ------------------------------------------------------------------------
 * Probability result
 *
 * Mirrors Python's calculate_probabilities() dict and the server payload.
 * JS rebuilds its odds object by marshalling:
 *
 *   status               MS_PROB_* -> 'exact' | 'approximate' |
 *                        'unavailable' | 'not-started' | 'finished'
 *   probabilities[i]     (flags[i] & MS_PCELL_VALUE) ? probabilities[i] : null
 *   proven_safe/mines    ascending indices with MS_PCELL_PROVEN_SAFE/MINE
 *   meta.frontier_cells, components, unconstrained_cells, samples,
 *        exact_components, sampled_components, sample_attempts, elapsed_ms
 *                        header fields of the same name
 *   meta.effective_sample_size  has_effective_sample_size ? value : null
 *   meta.reason          ms_prob_reason name, MS_REASON_NONE -> null
 *   message              presentation text JS derives from status, reason
 *                        and meta (Python's _message wording); C has no text
 * Placeholders (not-started/finished) carry only the four meta counts (all
 * zero), elapsed_ms 0 and the reason, like Python's _placeholder_response.
 * Extra header counts (nodes, hidden_cells, remaining_mines,
 * propagated_cells, pair_reasoning_complete) are Python's extra meta keys.
 * ------------------------------------------------------------------------ */
typedef enum ms_prob_status {
    MS_PROB_EXACT = 1,       /* every component counted exactly; a value is
                                exactly 0.0/1.0 only for proven cells */
    MS_PROB_APPROXIMATE = 2, /* some component sampled: values are estimates
                                and may be 0.0/1.0 without proof */
    MS_PROB_UNAVAILABLE = 3, /* no trustworthy estimate within budget: only
                                proven cells carry a value */
    MS_PROB_NOT_STARTED = 4, /* engine placeholder: game ready (no solve) */
    MS_PROB_FINISHED = 5     /* engine placeholder: game won/lost (no solve) */
} ms_prob_status;

/* meta.reason. Names are the Python strings. */
typedef enum ms_prob_reason {
    MS_REASON_NONE = 0,                           /* null: exact results */
    MS_REASON_COUNTING_BUDGET_EXCEEDED = 1,       /* approximate results */
    MS_REASON_SAMPLING_BUDGET_EXHAUSTED = 2,
    MS_REASON_NO_CONSISTENT_SAMPLES = 3,
    MS_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES = 4,
    MS_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES = 5,
    MS_REASON_TIME_BUDGET_EXHAUSTED = 6,
    MS_REASON_MEMORY_BUDGET_EXHAUSTED = 7,        /* new in C: workspace bytes
                                                     or retained entries ran
                                                     out outside counting */
    MS_REASON_NOT_STARTED = 8,                    /* placeholder */
    MS_REASON_GAME_OVER = 9                       /* placeholder */
} ms_prob_reason;

/* Python reason string ("counting_budget_exceeded", ...); "" for NONE and
 * NULL for values outside the enum. */
const char *ms_prob_reason_name(uint32_t reason);

/* Per-cell flag byte. Revealed cells: 0. Hidden cells: any of */
#define MS_PCELL_VALUE       0x01u /* probabilities[i] is meaningful */
#define MS_PCELL_PROVEN_SAFE 0x02u /* logically/exactly proven safe: value 0.0 */
#define MS_PCELL_PROVEN_MINE 0x04u /* proven mine: value 1.0 */
#define MS_PCELL_KNOWN_BITS  0x07u

typedef struct ms_result_header {
    uint32_t magic;                     /* MS_MAGIC_RESULT */
    uint32_t version;                   /* MS_ABI_VERSION */
    uint32_t status;                    /* ms_prob_status */
    uint32_t reason;                    /* ms_prob_reason */
    uint32_t width;                     /* copied from the observation */
    uint32_t height;
    uint32_t total_mines;
    uint32_t revealed;
    uint64_t observation_hash;          /* ms_obs_hash() of the observation */
    uint32_t frontier_cells;            /* hidden cells in solved components */
    uint32_t components;                /* exact_components + sampled_components */
    uint32_t unconstrained_cells;       /* hidden cells next to no clue */
    uint32_t samples;                   /* successful sampled layouts */
    uint32_t exact_components;
    uint32_t sampled_components;        /* components exact counting gave up */
    uint32_t sample_attempts;           /* proposals incl. zero-weight dead ends */
    uint32_t nodes;                     /* forward-counting nodes used */
    uint32_t hidden_cells;              /* cells - revealed */
    uint32_t remaining_mines;           /* total - mines fixed by propagation */
    uint32_t propagated_cells;          /* hidden cells fixed by propagation */
    uint32_t pair_reasoning_complete;   /* 0 or 1 */
    uint32_t proven_safe;               /* number of MS_PCELL_PROVEN_SAFE */
    uint32_t proven_mines;              /* number of MS_PCELL_PROVEN_MINE */
    uint32_t has_effective_sample_size; /* 0: meta.effective_sample_size null */
    uint32_t reserved;                  /* 0 */
    double elapsed_ms;                  /* finite >= 0 (0.001 resolution) */
    double effective_sample_size;       /* min ESS of sampled components;
                                           0.0 unless has_effective_sample_size */
} ms_result_header;
/* followed by: double probabilities[width * height] at offset 120 (finite,
 * 0..1 where MS_PCELL_VALUE, else 0.0), then uint8_t flags[width * height],
 * then zero padding to a multiple of 8. */

_Static_assert(sizeof(ms_result_header) == 120, "ms_result_header layout");
_Static_assert(MS_OFFSETOF(ms_result_header, observation_hash) == 32, "ms_result_header layout");
_Static_assert(MS_OFFSETOF(ms_result_header, elapsed_ms) == 104, "ms_result_header layout");

static inline size_t ms_result_size(uint32_t width, uint32_t height) {
    uint32_t cells = ms_cell_count(width, height);
    return cells ? ms_pad8(sizeof(ms_result_header) + (size_t)cells * 9u) : 0;
}

static inline double *ms_result_probabilities(const void *result) {
    return (double *)((uint8_t *)result + sizeof(ms_result_header));
}

static inline uint8_t *ms_result_flags(const void *result) {
    const ms_result_header *h = (const ms_result_header *)result;
    return (uint8_t *)result + sizeof(ms_result_header) +
           (size_t)h->width * h->height * sizeof(double);
}

/* Starts a solver result for a valid observation: magic/version, the
 * observation's dimensions, total, revealed count, hash and hidden_cells;
 * status 0 (invalid until the solver sets it), everything else zero.
 * MS_ERR_INVALID_BUFFER / MS_ERR_INVALID_OBSERVATION as for the inputs. */
ms_status ms_result_init(void *result, size_t result_len, const void *obs, size_t obs_len);

/* Writes the canonical placeholder for a game that is not being solved:
 * status MS_PROB_NOT_STARTED (reason NOT_STARTED) or MS_PROB_FINISHED
 * (reason GAME_OVER), all counts, values and flags zero. */
ms_status ms_result_placeholder(void *result, size_t result_len, uint32_t width,
                                uint32_t height, uint32_t total_mines, uint32_t status);

/* The engine's acceptance gate for a solver result, checked against the
 * observation it claims to answer: Python server _normalize_solver_result
 * semantics plus canonical encoding. Returns the observation's own error,
 * MS_ERR_INVALID_BUFFER, or MS_ERR_INVALID_RESULT unless all of these hold:
 * - dimensions, total, revealed count and observation_hash match;
 *   hidden_cells = cells - revealed; remaining_mines <= total;
 * - status is EXACT, APPROXIMATE or UNAVAILABLE (placeholders are never
 *   solver output); reason is MS_REASON_NONE..MEMORY_BUDGET_EXHAUSTED
 *   (any pairing with the status, as the server accepted);
 * - revealed cells: flags 0, value 0.0. Hidden cells: only known bits; not
 *   both proofs; PROVEN_SAFE => VALUE and 0.0; PROVEN_MINE => VALUE and 1.0;
 *   VALUE => finite 0..1; no VALUE => 0.0;
 * - EXACT/APPROXIMATE: every hidden cell has VALUE; EXACT: value 0.0 iff
 *   PROVEN_SAFE and 1.0 iff PROVEN_MINE (the UI shows exact endpoints as
 *   certain); UNAVAILABLE: VALUE iff proven;
 * - proven counts match the flags; 0/1 booleans; ESS 0.0 when absent, else
 *   finite >= 0; finite elapsed >= 0; zero reserved and padding bytes.
 * Proofs are trusted only after this check AND a matching generation/revision
 * (engine) - never from rounded values. */
ms_status ms_result_validate(const void *obs, size_t obs_len, const void *result,
                             size_t result_len);

/* ms_result_validate plus Python's pairing, which every ms_solve result
 * keeps (solver tests check each result with it): EXACT has reason NONE and
 * no sampling counts or ESS; APPROXIMATE has COUNTING_BUDGET_EXCEEDED, at
 * least one sampled component and sample, and an ESS; UNAVAILABLE has one
 * of reasons SAMPLING_BUDGET_EXHAUSTED..MEMORY_BUDGET_EXHAUSTED;
 * components = exact + sampled; samples <= sample_attempts; frontier +
 * unconstrained + propagated = hidden_cells (at most, when UNAVAILABLE). */
ms_status ms_result_validate_solver(const void *obs, size_t obs_len, const void *result,
                                    size_t result_len);

/* ------------------------------------------------------------------------
 * Game rules: c/game.c
 *
 * Port of minesweeper/game.py's Game for one game. All validation happens
 * before any mutation; a state change increments the revision by exactly
 * one, no-ops leave it unchanged. Once a game reaches MS_REVISION_MAX every
 * further ms_game_act/ms_game_apply_deductions fails with
 * MS_ERR_RESOURCE_EXHAUSTED after its other checks, so revisions never wrap.
 * Storage comes from the rt_mem given at creation (fallible);
 * ms_game_destroy returns all of it. The clock must outlive the game.
 * Not thread-safe (nothing here is).
 * ------------------------------------------------------------------------ */
typedef struct ms_game ms_game; /* opaque; never crosses the WASM boundary */

/* Creates a READY game: revision 0, no flags, elapsed 0.
 * Order: out/mem/clock NULL -> MS_ERR_INVALID_BUFFER; config (width, height,
 * mines as ms_game_check_config); generation outside 1..MS_GENERATION_MAX ->
 * MS_ERR_INVALID_REVISION; allocation -> MS_ERR_RESOURCE_EXHAUSTED.
 * `seed` drives mine placement only (host entropy in production, fixed in
 * tests): rt_rng_seed(seed), then on the first reveal candidates = every
 * index outside the clicked cell's clipped 3x3 block, ascending;
 * rt_rng_choose(rng, candidates, count, mines); the first `mines`
 * candidates become mines. Same seed + same moves => same game everywhere. */
ms_status ms_game_create(ms_game **out, rt_mem *mem, rt_clock *clock, uint32_t width,
                         uint32_t height, uint32_t mines, uint32_t generation,
                         uint64_t seed);

/* Test hook replacing Python's injected placement RNG: as ms_game_create,
 * but the first reveal places exactly the given cells (copied here; layout
 * may be NULL only when layout_count is 0; layout_count > width * height ->
 * MS_ERR_INVALID_BUFFER). The layout is checked at that first reveal: it
 * must list `mines` distinct cells < width * height, none in the clicked
 * cell's clipped 3x3 block. Otherwise the reveal fails with MS_ERR_INTERNAL
 * (Python's "random source returned an invalid mine sample") and changes
 * nothing: the game stays READY at its revision. Never used in production. */
ms_status ms_game_create_with_layout(ms_game **out, rt_mem *mem, rt_clock *clock,
                                     uint32_t width, uint32_t height, uint32_t mines,
                                     uint32_t generation, const uint32_t *layout,
                                     uint32_t layout_count);

/* Frees everything the game owns (NULL is a no-op). */
void ms_game_destroy(ms_game *game);

/* Applies one player action against the expected generation/revision.
 * Order (Python apply_action): action -> MS_ERR_INVALID_ACTION;
 * row >= height or col >= width -> MS_ERR_OUT_OF_BOUNDS; generation or
 * revision out of range -> MS_ERR_INVALID_REVISION; not the current
 * generation/revision -> MS_ERR_STALE_REVISION; won/lost -> MS_ERR_GAME_OVER.
 * REVEAL: no-op on revealed/flagged cells. The first reveal places the
 * mines (above), starts the timer and makes the game PLAYING. A mine is
 * exploded (revealed + exploded) and the game LOST; otherwise an iterative
 * flood opens the region around zero clues (flagged cells stay hidden)
 * and the game is WON once every safe cell is revealed, which also flags
 * every mine. FLAG: toggles a hidden cell's flag (allowed while READY);
 * no-op on revealed cells. CHORD: on a revealed safe cell whose flagged
 * neighbor count equals its clue, reveals every hidden unflagged neighbor:
 * safe ones flood first, then any mines among them (wrong flags) all
 * explode; otherwise a no-op. Won/lost stop the timer.
 * *changed (optional) reports whether the revision advanced. */
ms_status ms_game_act(ms_game *game, uint32_t generation, uint32_t revision, uint32_t action,
                      uint32_t row, uint32_t col, bool *changed);

/* Applies one atomic batch of proven deductions (Python apply_deductions):
 * `safe` and `mines` list cell indices (each may be NULL when its count is
 * 0; duplicates within a list are allowed, as with Python's sets).
 * Order: a NULL list with a nonzero count -> MS_ERR_INVALID_BUFFER;
 * revision checks as ms_game_act; READY -> MS_ERR_GAME_NOT_STARTED; an
 * index >= width * height, a cell in both lists, or a revealed cell ->
 * MS_ERR_INVALID_DEDUCTIONS (nothing applied).
 * Then, in ascending cell order: flag every proven mine not yet flagged;
 * clear every flag on a proven-safe cell; reveal proven-safe cells with
 * REVEAL semantics, stopping if the game ends. One revision for the whole
 * batch iff anything changed. The engine passes only proofs of a validated
 * result for the current revision (from its MS_PCELL_PROVEN_* flags). */
ms_status ms_game_apply_deductions(ms_game *game, uint32_t generation, uint32_t revision,
                                   const uint32_t *safe, uint32_t safe_count,
                                   const uint32_t *mines, uint32_t mine_count, bool *changed);

/* Writes the public view (always allowed; reads the clock while PLAYING).
 * MS_ERR_INVALID_BUFFER unless view_len == ms_view_size(width, height). */
ms_status ms_game_view(ms_game *game, void *view, size_t view_len);

/* Writes the public observation of a PLAYING game for the expected
 * generation/revision (Python revealed_clues()).
 * Order: buffer -> MS_ERR_INVALID_BUFFER; revision checks as ms_game_act;
 * READY -> MS_ERR_GAME_NOT_STARTED; won/lost -> MS_ERR_GAME_OVER. */
ms_status ms_game_observe(const ms_game *game, uint32_t generation, uint32_t revision,
                          void *obs, size_t obs_len);

/* ------------------------------------------------------------------------
 * Probability solver: c/probability.c
 *
 * Port of minesweeper/probability.py (all seven documented stages, exact
 * multiprecision integer counting, same proof rules and statuses).
 * ------------------------------------------------------------------------ */

/* Solves one observation into a result buffer of
 * ms_result_size(width, height) bytes.
 *
 * Arguments. First, on addresses alone (no caller byte is read or written
 * yet): obs, limits and result non-NULL and 8-byte aligned; workspace and
 * clock non-NULL; the byte ranges of obs (obs_len), limits
 * (sizeof(ms_infer_limits)), result (result_len), *workspace and *clock
 * non-empty and not wrapping the address space (checked uintptr_t
 * arithmetic); obs, limits and result pairwise disjoint and overlapping
 * neither struct (adjacent ranges are fine). The solver zeroes and fills the
 * result while it still reads the observation, so an aliased request is
 * refused, never given an in-place meaning. Then: limits magic/version,
 * ms_obs_validate, result_len equal to the observation's ms_result_size,
 * ms_limits_validate. A rejected call (MS_ERR_INVALID_BUFFER,
 * MS_ERR_INVALID_OBSERVATION or MS_ERR_INVALID_LIMITS) changes nothing: the
 * inputs, result, workspace and clock are left untouched.
 *
 * Returns MS_OK with an EXACT, APPROXIMATE or UNAVAILABLE result that passes
 * ms_result_validate_solver(). MS_OK writes every byte of the result
 * (header, values, flags and zero padding), so the buffer needs no prior
 * initialization (ms_result_init() is not required). Budget exhaustion
 * (time, nodes, samples, ESS, memory_budget_bytes, max_stored_entries) is
 * reported this way with the Python reason, never as an error, and keeps
 * every sound proof found. Proofs come from exact integers only:
 * propagation, the structural proofs of exhaustively counted components
 * and, in an EXACT result, numerators equal to 0 or to their full
 * denominator; never doubles or samples. If the final integer-ratio-to-double
 * conversions run out of workspace memory, the result degrades to
 * UNAVAILABLE (MEMORY_BUDGET_EXHAUSTED) without estimates but keeps those
 * integer proofs, including the numerator proofs of a count that had
 * completed exactly. Diagnostics: nodes saturates at UINT32_MAX; elapsed_ms
 * and effective_sample_size are rounded to 0.001 like Python's meta
 * (round(x * 1000) / 1000, halves up), which never affects values or proofs.
 *
 * Other errors: MS_ERR_INCONSISTENT when propagation or exact counting
 * proves that the clues and total admit no layout (e.g. more mines than
 * hidden cells or a clue above its hidden neighbors: Python's
 * InconsistentBoardError); an inconsistency not proven within the budgets
 * yields an UNAVAILABLE result instead (sampling never proves one).
 * MS_ERR_INTERNAL: a broken invariant (e.g. inexact histogram division, a
 * weight mismatch or workspace misuse during the call), never turned into
 * probabilities. After either, the result's contents are unspecified.
 *
 * Workspace: every temporary allocation comes from `workspace`, which may
 * hold unrelated live allocations. The solver caps its own usage at
 * limits->memory_budget_bytes (rt_mem_limit, restored before returning)
 * and releases everything it allocated before returning, on every path.
 * Clock: time_budget_ms is measured on `clock` from entry; elapsed_ms is the
 * clock span from entry to exit, clamped to a finite value >= 0. Sampling
 * uses an rt_rng seeded with limits->seed (MS_LIMIT_EXPLICIT_SEED) or
 * ms_obs_hash(obs), so results are reproducible on every platform whenever
 * budgets do not cut work short (tests use fake clocks). */
ms_status ms_solve(const void *obs, size_t obs_len, const ms_infer_limits *limits,
                   rt_mem *workspace, rt_clock *clock, void *result, size_t result_len);

/* ------------------------------------------------------------------------
 * Engine service (c/engine.c) and WebAssembly reactor (c/wasm_api.c)
 *
 * These are designed in the wasm-bindings milestone under the `ms_engine_`
 * prefix; the rules below are already fixed.
 *
 * Engine service invariants (replaces server.py's ProbabilityService and
 * GameEntry for one browser tab):
 * - One current game per instance; ms_engine_* creation of a new game uses
 *   the next generation (1, 2, ...) and drops the old game and its results.
 * - Every operation from JS names (generation, revision). A mismatch is
 *   MS_ERR_STALE_REVISION before anything else changes.
 * - A solver result is accepted only for the current generation/revision of
 *   a PLAYING game and only after ms_result_validate() against an
 *   observation freshly built from that game.
 * - EXACT/APPROXIMATE results are reusable for their revision. UNAVAILABLE
 *   results are never reused for display (a retry recomputes), but their
 *   independent proofs may feed autosolve at that revision.
 * - Autosolve applies the stored proofs through ms_game_apply_deductions
 *   (atomic, one revision); sampled 0.0/1.0 values are never proofs.
 * - READY/won/lost games answer with ms_result_placeholder().
 *
 * Reactor ABI rules:
 * - Built with PLATFORM_SKIP_ENTRY and --no-entry: no _start. Exports are
 *   memory, __heap_base, the ms_* functions, and corec's own
 *   wasm_buddy_alloc/wasm_buddy_free (not used by JS).
 * - uint32_t ms_abi_version(void) returns MS_ABI_VERSION and may be called
 *   first; JS refuses a mismatch.
 * - ms_status ms_init(void) must be called once per instance before anything
 *   else; it runs platform_init(0, NULL, NULL). A repeated call returns
 *   MS_ERR_INTERNAL and changes nothing; every other export returns
 *   MS_ERR_INTERNAL (or a 0 pointer) before initialization.
 * - JS obtains buffers from 16-byte aligned, budgeted allocation exports
 *   and frees them after copying results into JS-owned objects.
 * - Parameters are i32 (uint32 values; JS rejects booleans, fractions, NaN
 *   and out-of-range numbers before the call, C re-checks every range) or
 *   f64; 64-bit seeds travel as two uint32 halves (lo, hi).
 * - Any export that may allocate can grow memory and detach existing
 *   ArrayBuffer views: JS recreates typed-array/DataView views after calls.
 * - A trap or corec ProcExit escaping an export leaves the instance unusable;
 *   the host discards it (solver: recreate the worker; game: fatal error).
 * - Imports are limited to the wasi_snapshot_preview1 functions corec links
 *   and ms_host.now_ms() -> f64 (monotonic milliseconds, performance.now).
 *   Linking never uses --allow-undefined; scripts/check-wasm.mjs audits it.
 * - No export exposes pointers into engine state. Only public views,
 *   observations and results cross the boundary; the solver instance never
 *   sees the game, its flags, its RNG or its seed.
 * ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------
 * Engine service API: c/engine.c (exported to JS by c/wasm_api.h)
 *
 * The service never runs the solver: results arrive from the solver worker
 * (another instance, ms_solve) and are accepted here only after the checks
 * below. Storage for the current game, its stored result and scratch comes
 * from `mem` and is allocated by ms_engine_new_game, so no other operation
 * allocates or can fail half-way.
 *
 * Common rules for the functions below:
 * - Engine pointer misuse (NULL, never initialized or disposed engine) is
 *   MS_ERR_INTERNAL. Buffer arguments: NULL, misaligned, overlapping each
 *   other or the engine's own storage (the stored result and observation
 *   scratch, which must never be a caller buffer) first, then the
 *   generation/revision checks, then exact lengths (MS_ERR_INVALID_BUFFER);
 *   a rejected call leaves every buffer unchanged.
 * - `generation` outside 1..MS_GENERATION_MAX or `revision` above
 *   MS_REVISION_MAX: MS_ERR_INVALID_REVISION; not the current game (or no
 *   game yet) or not its current revision: MS_ERR_STALE_REVISION. Mutating
 *   operations change nothing on any error.
 * - The stored result belongs to one (generation, revision). Any state
 *   change (an action or batch that advances the revision) and every new
 *   game drop it; no-ops keep it.
 *
 * Result acceptance (ms_engine_accept_result), in order: the result must
 * name the current generation/revision of a PLAYING game (READY:
 * MS_ERR_GAME_NOT_STARTED, won/lost: MS_ERR_GAME_OVER), pass
 * ms_result_validate() against an observation freshly built from the game
 * (observation_hash binds it to exactly that public observation), and have
 * every count field frontier_cells, components, unconstrained_cells,
 * samples, exact_components, sampled_components, sample_attempts, nodes and
 * propagated_cells <= INT32_MAX (larger values are negative numbers in a
 * signed view, Python's "meta counts non-negative"); otherwise
 * MS_ERR_INVALID_RESULT (MS_ERR_INVALID_BUFFER for length/magic/version)
 * and nothing is stored. Proof lists cannot hold duplicates: proofs are
 * per-cell flag bits, so no cell is ever applied twice.
 * Storage: every accepted result replaces the stored one - the latest
 * validated result for the revision wins, whatever its status. No status
 * is assumed to contain another's proofs (a sampled APPROXIMATE result may
 * leave 0/1 endpoints unproven that a later budget-limited UNAVAILABLE run
 * proves exactly). ms_engine_cached_result serves the stored result only
 * while it is EXACT/APPROXIMATE, so accepting an UNAVAILABLE one makes a
 * retry solve anew; ms_engine_autosolve applies the proofs of the latest.
 * ------------------------------------------------------------------------ */

/* Largest uintptr_t (corec's base/types.h defines no UINTPTR_MAX); used by
 * the overflow-checked caller-range arithmetic of the engine and reactor. */
#define MS_UINTPTR_MAX ((uintptr_t)-1)

/* One instance per WebAssembly instance (per browser tab). Fields
 * are public for inspection, like rt_mem; mutate only through the
 * functions (tests may preset `generation` before a new game to exercise
 * generation exhaustion). */
typedef struct ms_engine {
    rt_mem *mem;              /* games, stored results and scratch */
    rt_clock *clock;          /* game timers; must outlive the engine */
    ms_game *game;            /* current game, NULL before the first */
    uint32_t generation;      /* current game's generation, 0 before the first */
    uint32_t width;           /* current game's configuration */
    uint32_t height;
    uint32_t mines;
    uint32_t stored;          /* 1: result holds an accepted result for
                                 (generation, stored_revision) */
    uint32_t stored_revision;
    uint32_t stored_status;   /* its ms_prob_status */
    uint32_t state;           /* private: initialized/disposed marker */
    void *storage;            /* private: one block per game */
    size_t storage_len;
    void *result;             /* private: stored result, result_len bytes */
    size_t result_len;
    void *obs;                /* private: fresh-observation scratch */
    size_t obs_len;
    uint32_t *proof_safe;     /* private: autosolve index lists, cells each */
    uint32_t *proof_mines;
} ms_engine;

/* Starts an engine without a game. Never allocates. NULL engine, mem or
 * clock: MS_ERR_INVALID_BUFFER (the engine stays unusable). */
ms_status ms_engine_init(ms_engine *engine, rt_mem *mem, rt_clock *clock);

/* Frees the current game and everything stored; afterwards every function
 * returns MS_ERR_INTERNAL until ms_engine_init. Safe to call twice. */
void ms_engine_dispose(ms_engine *engine);

/* Creates a new READY game (revision 0) with the next generation and, only
 * once everything is allocated, drops the old game and its stored result.
 * Order: config (as ms_game_check_config); generation already at
 * MS_GENERATION_MAX -> MS_ERR_RESOURCE_EXHAUSTED; allocation ->
 * MS_ERR_RESOURCE_EXHAUSTED. On any error the current game, its generation
 * and its stored result are untouched. `seed` drives mine placement
 * (ms_game_create). *generation (optional) receives the new generation. */
ms_status ms_engine_new_game(ms_engine *engine, uint32_t width, uint32_t height,
                             uint32_t mines, uint64_t seed, uint32_t *generation);

/* TEST HOOK (native tests only; never exported by the reactor): as
 * ms_engine_new_game, but the game uses ms_game_create_with_layout. */
ms_status ms_engine_new_game_with_layout(ms_engine *engine, uint32_t width, uint32_t height,
                                         uint32_t mines, const uint32_t *layout,
                                         uint32_t layout_count, uint32_t *generation);

/* ms_game_act on the current game (same validation order; with no game:
 * action, then generation/revision checks). Drops the stored result when
 * the revision advances. *changed (optional) as ms_game_act. */
ms_status ms_engine_act(ms_engine *engine, uint32_t generation, uint32_t revision,
                        uint32_t action, uint32_t row, uint32_t col, bool *changed);

/* The current game's public view (any status; reading has no side effects
 * beyond the clock). view_len must be ms_view_size(width, height). */
ms_status ms_engine_view(ms_engine *engine, uint32_t generation, void *view, size_t view_len);

/* The public observation the solver worker needs (ms_game_observe):
 * READY -> MS_ERR_GAME_NOT_STARTED, won/lost -> MS_ERR_GAME_OVER. */
ms_status ms_engine_observe(ms_engine *engine, uint32_t generation, uint32_t revision,
                            void *obs, size_t obs_len);

/* What JS can show for (generation, revision) without a new solve.
 * *available (required) is set on MS_OK: 1 when `result` now holds the
 * answer - the NOT_STARTED placeholder (READY), the FINISHED placeholder
 * (won/lost) or a copy of the stored EXACT/APPROXIMATE result; 0 when there
 * is nothing reusable (no result yet, or the latest accepted result is
 * UNAVAILABLE): JS must request a fresh solve, and `result` is left
 * unchanged. result_len must be ms_result_size(width, height). Outputs are
 * written only on MS_OK. */
ms_status ms_engine_cached_result(ms_engine *engine, uint32_t generation, uint32_t revision,
                                  void *result, size_t result_len, bool *available);

/* Accepts a solver result for (generation, revision) as described above;
 * it replaces whatever result was stored for the revision. The caller
 * keeps its buffer; the engine stores its own copy. */
ms_status ms_engine_accept_result(ms_engine *engine, uint32_t generation, uint32_t revision,
                                  const void *result, size_t result_len);

/* One autosolve batch: applies the proven safe/mine flags of the latest
 * result accepted for (generation, revision) - any status - through
 * ms_game_apply_deductions (atomic, one revision). Order: aliased outputs
 * -> MS_ERR_INVALID_BUFFER; generation/revision checks, READY ->
 * MS_ERR_GAME_NOT_STARTED, won/lost -> MS_ERR_GAME_OVER, then MS_OK with
 * *available = 0 and nothing changed if no result is stored for the
 * revision (JS solves first and retries); otherwise *available = 1 and
 * ms_game_apply_deductions decides (e.g. MS_ERR_RESOURCE_EXHAUSTED at
 * MS_REVISION_MAX). *changed reports whether the batch changed the game (a
 * pause leaves the stored result in place). There is deliberately no way to
 * apply caller-supplied proof lists. Both outputs are optional and written
 * only on MS_OK. */
ms_status ms_engine_autosolve(ms_engine *engine, uint32_t generation, uint32_t revision,
                              bool *available, bool *changed);

#ifdef __cplusplus
}
#endif
