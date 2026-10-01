/*
 * Shared schema for the static compatibility fixtures in tests/c/fixtures.
 *
 * Provenance: every *_cases.h / *_scripts.h / large_boards.h header was
 * generated once from the Python reference at baseline commit
 * 153b78ac025858f11c124e6ae17953fdecd0aec3 (149 passing unittest tests:
 * 53 game, 29 probability, 17 autosolve, 50 server). The generator is not
 * part of the repository; the headers are frozen reference data, so no
 * build or test needs Python. Each case names the baseline test it ports;
 * tests/coverage-map.json maps every baseline test to its destination.
 *
 * Probability values come from the baseline solver
 * (minesweeper/probability.py, unlimited time budget) AND from independent
 * exact oracles written separately for the fixtures:
 *   - whole-board enumeration of every mine placement (small boards), and
 *   - raw-component backtracking combined by explicit convolution with
 *     exact binomial weights C(U, R - t) for the cells touching no clue
 *     (80x80 and chain cases; the 80x80 autosolve rounds first apply
 *     single-clue unit propagation, still independent of the solver's DP).
 * A case is only emitted when every applicable oracle and the baseline agree
 * exactly; reference doubles were compared bit for bit.
 *
 * The data does not assume the engine ABI. Enumerations here are fixture
 * enumerations: test code maps them onto the real engine/solver types.
 * Everything is header-only static data built on corec's base/types.h; no
 * system header, libc call, JSON parser or decimal parser is required.
 *
 * Conventions
 * - Cells are row-major flat indices: index = row * width + col.
 * - FixtureRun is the progression start + k * step for 0 <= k < count,
 *   with step >= 1; it lists cells compactly (e.g. a 3001-cell chain).
 * - Exact probabilities are reduced fractions num/den. The reference double
 *   is num/den correctly rounded to nearest, except that an exact value
 *   that is neither 0 nor 1 never becomes 0.0 or 1.0: it is replaced by the
 *   smallest subnormal (bits 0x0000000000000001) or by 1 - 2^-53 (bits
 *   0x3fefffffffffffff), the baseline guard reserving endpoints for proofs.
 *   Reference doubles are stored as binary64 bit patterns (fixture_f64).
 *   When den == 0 only the reference double is recorded (the exact fraction
 *   has thousands of bits); compare such values with a relative tolerance of
 *   a few ulps (e.g. 1e-12) unless the port guarantees correct rounding.
 * - Big integers (FixtureBig) are little-endian base-2^32 limbs with no
 *   leading zero limb; zero has len 0.
 * - Board renders (game/autosolve): one char per cell, rows concatenated:
 *     '#' hidden, 'F' hidden and flagged, '0'..'8' revealed safe clue,
 *     'X' exploded mine (revealed by the losing move).
 *   In a won/lost state the full layout is public; compare it with the
 *   script layout separately (renders do not show unexploded mines).
 * - Board maps (80x80 observations with a generating layout) are arrays of
 *   `height` row strings of `width` chars (each literal stays below the
 *   4095-char minimum ISO C guarantees):
 *     '0'..'8' revealed clue (public), '.' hidden safe, '*' hidden mine.
 *   Only the digits are public; '*' lets a test check that proofs agree
 *   with the layout that produced the clues. Large renders use the same
 *   row-array form with the render alphabet.
 * - Generated script/event rows use designated initializers and omit
 *   zero-valued fields, so an absent field means 0/NULL (e.g. elapsed_ms 0
 *   and flags_after 0 are real expectations; error is FIXTURE_ERR_NONE).
 */
#pragma once

#include <base/types.h>

#define FIXTURE_BASELINE_COMMIT "153b78ac025858f11c124e6ae17953fdecd0aec3"

/* Baseline policy constants (server.py SOLVER_*, probability.py). */
#define FIXTURE_DEFAULT_TIME_BUDGET_MS 1500u
#define FIXTURE_DEFAULT_NODE_BUDGET 100000u
#define FIXTURE_DEFAULT_SAMPLE_BUDGET 2000u
#define FIXTURE_MIN_EFFECTIVE_SAMPLE_SIZE 50.0
#define FIXTURE_MAX_STORED_ENTRIES 1500000u

/* Game limits (minesweeper/game.py). */
#define FIXTURE_MIN_DIMENSION 5u
#define FIXTURE_MAX_DIMENSION 80u
#define FIXTURE_MIN_MINES 1u
#define FIXTURE_SAFE_START_CELLS 9u

/* Sentinels for int64 step fields. */
#define FIXTURE_REV_CURRENT (-INT64_MAX - 1)  /* pass the game's current revision */
#define FIXTURE_UNCHECKED (-INT64_MAX - 1)    /* expectation intentionally not checked */
#define FIXTURE_HIDDEN_CELL (-1)               /* dense clue arrays: hidden cell */

/* ------------------------------------------------------------ shared types */

typedef struct FixtureRun {
    uint32_t start;
    uint32_t step;
    uint32_t count;
} FixtureRun;

/* Every cell of the progression is revealed and shows `clue` (0..8). */
typedef struct FixtureClueRun {
    uint32_t start;
    uint32_t step;
    uint32_t count;
    uint8_t clue;
} FixtureClueRun;

typedef struct FixtureBig {
    const uint32_t *limbs;
    uint32_t len;
} FixtureBig;

typedef struct FixtureRatio {
    uint64_t num;
    uint64_t den;        /* 0: exact fraction not recorded, use the reference double */
    uint64_t value_bits; /* reference double as IEEE-754 binary64 bits (fixture_f64) */
} FixtureRatio;

/* All listed cells share one probability. */
typedef struct FixtureOddsClass {
    FixtureRatio odds;
    const FixtureRun *runs;
    uint32_t run_count;
} FixtureOddsClass;

/* Probability statuses. Names match the baseline JSON strings. */
typedef enum FixtureStatus {
    FIXTURE_STATUS_EXACT = 0,        /* "exact" */
    FIXTURE_STATUS_APPROXIMATE = 1,  /* "approximate" */
    FIXTURE_STATUS_UNAVAILABLE = 2,  /* "unavailable" */
    FIXTURE_STATUS_NOT_STARTED = 3,  /* "not-started": engine placeholder, solver not run */
    FIXTURE_STATUS_FINISHED = 4,     /* "finished": engine placeholder, solver not run */
    FIXTURE_STATUS_INCONSISTENT = 5, /* solver error InconsistentBoardError */
    FIXTURE_STATUS_MALFORMED = 6,    /* solver error ValueError (malformed arguments) */
    FIXTURE_STATUS_COUNT = 7
} FixtureStatus;

#define FIXTURE_STATUS_BIT(status) (1u << (status))

static const char *const FIXTURE_STATUS_NAMES[FIXTURE_STATUS_COUNT] = {
    "exact", "approximate", "unavailable", "not-started", "finished",
    "inconsistent", "malformed",
};

/* meta.reason values. NONE is JSON null (exact results). */
typedef enum FixtureReason {
    FIXTURE_REASON_NONE = 0,
    FIXTURE_REASON_COUNTING_BUDGET_EXCEEDED = 1,
    FIXTURE_REASON_SAMPLING_BUDGET_EXHAUSTED = 2,
    FIXTURE_REASON_NO_CONSISTENT_SAMPLES = 3,
    FIXTURE_REASON_NO_GLOBALLY_COMPATIBLE_SAMPLES = 4,
    FIXTURE_REASON_INSUFFICIENT_EFFECTIVE_SAMPLES = 5,
    FIXTURE_REASON_TIME_BUDGET_EXHAUSTED = 6,
    FIXTURE_REASON_NOT_STARTED = 7,  /* placeholder reason */
    FIXTURE_REASON_GAME_OVER = 8,    /* placeholder reason */
    FIXTURE_REASON_COUNT = 9
} FixtureReason;

#define FIXTURE_REASON_BIT(reason) (1u << (reason))

static const char *const FIXTURE_REASON_NAMES[FIXTURE_REASON_COUNT] = {
    "", "counting_budget_exceeded", "sampling_budget_exhausted",
    "no_consistent_samples", "no_globally_compatible_samples",
    "insufficient_effective_samples", "time_budget_exhausted",
    "not_started", "game_over",
};

typedef enum FixtureTruth {
    FIXTURE_TRUTH_ODDS = 0,         /* consistent; exact odds and proofs recorded */
    FIXTURE_TRUTH_INCONSISTENT = 1, /* no layout satisfies clues and mine total */
    FIXTURE_TRUTH_UNRECORDED = 2    /* consistent (generating layout in board_rows); odds unknown */
} FixtureTruth;

/* Independent checks that agreed with the recorded values (bit mask). */
#define FIXTURE_ORACLE_BASELINE 1u    /* baseline Python solver (always) */
#define FIXTURE_ORACLE_WHOLE_BOARD 2u /* enumeration of every mine placement */
#define FIXTURE_ORACLE_FRONTIER 4u    /* raw-component backtracking + explicit convolution */
#define FIXTURE_ORACLE_CLOSED_FORM 8u /* formula stated in the case comment */

/* Baseline meta counts (after the documented propagation stage). These are
 * algorithm-defined: they match when the port keeps stages 1-3 unchanged. */
typedef struct FixtureMeta {
    uint32_t frontier_cells;
    uint32_t components;
    uint32_t unconstrained_cells;
} FixtureMeta;

/* ------------------------------------------------------------ probability */

typedef struct ProbabilityCase {
    const char *name;
    const char *provenance;
    uint32_t width;
    uint32_t height;
    uint32_t total_mines;
    const FixtureClueRun *clues;   /* sparse observation (NULL when board_rows is set) */
    uint32_t clue_run_count;
    const char *const *board_rows; /* NULL or `height` map rows, see conventions */
    uint32_t truth;                /* FixtureTruth */
    uint32_t baseline_status;      /* FixtureStatus with default budgets */
    uint32_t accepted_statuses;    /* FIXTURE_STATUS_BIT mask allowed for the port, default budgets */
    const FixtureOddsClass *classes;
    uint32_t class_count;
    int32_t rest_class;            /* class of hidden cells not listed by any class, or -1 */
    const FixtureRun *proven_safe; /* exact proofs: cells with probability exactly 0 */
    uint32_t proven_safe_runs;
    const FixtureRun *proven_mines; /* exact proofs: cells with probability exactly 1 */
    uint32_t proven_mine_runs;
    FixtureBig layouts;            /* consistent complete layouts Z (len 0: not recorded) */
    FixtureMeta meta;
    uint32_t verified_by;          /* FIXTURE_ORACLE_* mask */
} ProbabilityCase;

typedef enum FixtureValueKind {
    FIXTURE_VALUE_NULL = 0,        /* JSON null / no value */
    FIXTURE_VALUE_FINITE = 1,
    FIXTURE_VALUE_NAN = 2,
    FIXTURE_VALUE_POS_INF = 3,
    FIXTURE_VALUE_NEG_INF = 4
} FixtureValueKind;

/* Raw solver arguments outside the domain; only C-representable values are
 * listed (bool/string/float/non-mapping inputs are JS adapter cases). */
typedef struct MalformedObservationCase {
    const char *name;
    const char *provenance;
    int64_t width;
    int64_t height;
    int64_t total_mines;
    const int64_t *clue_cells;     /* raw clue indices (parallel to clue_values) */
    const int64_t *clue_values;
    uint32_t clue_count;
    int64_t node_budget;           /* negative values are themselves malformed */
    int64_t sample_budget;
    uint32_t time_budget_kind;     /* FixtureValueKind; FINITE uses time_budget_s */
    double time_budget_s;
    uint32_t expected_status;      /* FIXTURE_STATUS_MALFORMED or _INCONSISTENT */
    uint32_t baseline_case;        /* 0: C-specific addition, not in the baseline tests */
} MalformedObservationCase;

typedef enum FixtureProofsRule {
    FIXTURE_PROOFS_EXACT_MATCH = 0, /* lists equal proven_safe / proven_mines */
    FIXTURE_PROOFS_TRUTHFUL = 1,    /* every listed cell is certain in the truth/layout */
    FIXTURE_PROOFS_EMPTY = 2        /* nothing may be listed */
} FixtureProofsRule;

/* Budgeted solve of a recorded observation. Expected values are ground
 * truth, never a Python random stream: the C generator may differ.
 * Per-cell tolerance for approximate results:
 *   |p - truth| <= tolerance_sigmas * 0.5 / sqrt(ESS)
 * (the self-normalised importance estimate of a Bernoulli mean has standard
 * error about sqrt(p(1-p)/ESS) <= 0.5/sqrt(ESS); five sigmas is the baseline
 * rule). The expected mine total is exact for every consistent weighted
 * sample mixture, so sum(p) == total_mines up to rounding. */
typedef struct SamplingCase {
    const char *name;
    const char *provenance;
    const ProbabilityCase *observation;
    uint32_t node_budget;
    uint32_t sample_budget;
    uint32_t time_budget_ms;       /* 0: generous; baseline used 60 s so nodes/samples bind */
    uint32_t baseline_status;
    uint32_t accepted_statuses;    /* FIXTURE_STATUS_BIT mask */
    uint32_t baseline_reason;
    uint32_t accepted_reasons;     /* FIXTURE_REASON_BIT mask */
    uint32_t rng_independent;      /* 1: status/reason/proofs hold for any generator stream */
    uint32_t proofs_rule;          /* FixtureProofsRule */
    const FixtureRun *proven_safe;
    uint32_t proven_safe_runs;
    const FixtureRun *proven_mines;
    uint32_t proven_mine_runs;
    double min_effective_samples;  /* approximate results: ESS >= this */
    double exact_effective_samples; /* >= 0: ESS equals this (equal weights); -1 unchecked */
    double tolerance_sigmas;       /* 0: no per-cell estimate check */
    double mine_sum_tolerance;     /* applies when probabilities are reported */
    uint32_t min_exact_components;
    uint32_t min_sampled_components;
    /* Evidence from the baseline sampler over `evidence_seeds` independent
     * seeds (informational, not normative). */
    uint32_t evidence_seeds;
    double evidence_min_ess;
    double evidence_max_sigma;
} SamplingCase;

/* ------------------------------------------------------------------- game */

typedef enum FixtureGameStatus {
    FIXTURE_GAME_READY = 0,
    FIXTURE_GAME_PLAYING = 1,
    FIXTURE_GAME_WON = 2,
    FIXTURE_GAME_LOST = 3
} FixtureGameStatus;

typedef enum FixtureGameOp {
    FIXTURE_OP_REVEAL = 0,
    FIXTURE_OP_FLAG = 1,           /* toggle */
    FIXTURE_OP_CHORD = 2,
    FIXTURE_OP_RAW_ACTION = 3,     /* pass raw_action as the action value */
    FIXTURE_OP_DEDUCE = 4,         /* apply one proven batch: safe[], mines[] */
    FIXTURE_OP_ADVANCE = 5         /* advance the injected monotonic clock */
} FixtureGameOp;

typedef enum FixtureExpect {
    FIXTURE_EXPECT_CHANGED = 0,    /* applied; revision + 1 */
    FIXTURE_EXPECT_NOOP = 1,       /* accepted no-op; state and revision unchanged */
    FIXTURE_EXPECT_ERROR = 2,      /* rejected; state unchanged */
    FIXTURE_EXPECT_NONE = 3        /* clock step */
} FixtureExpect;

typedef enum FixtureGameError {
    FIXTURE_ERR_NONE = 0,
    FIXTURE_ERR_INVALID_WIDTH = 1,
    FIXTURE_ERR_INVALID_HEIGHT = 2,
    FIXTURE_ERR_INVALID_MINES = 3,
    FIXTURE_ERR_INVALID_ACTION = 4,
    FIXTURE_ERR_INVALID_COORDINATES = 5, /* non-integer coordinates: JS adapter only */
    FIXTURE_ERR_OUT_OF_BOUNDS = 6,
    FIXTURE_ERR_INVALID_REVISION = 7,
    FIXTURE_ERR_STALE_REVISION = 8,      /* latest public state accompanies it */
    FIXTURE_ERR_GAME_OVER = 9,           /* latest public state accompanies it */
    FIXTURE_ERR_GAME_NOT_STARTED = 10,
    FIXTURE_ERR_INVALID_DEDUCTIONS = 11,
    FIXTURE_ERR_INVALID_LAYOUT = 12,     /* rejected placement source (baseline RuntimeError) */
    FIXTURE_ERR_COUNT = 13
} FixtureGameError;

static const char *const FIXTURE_GAME_ERROR_NAMES[FIXTURE_ERR_COUNT] = {
    "", "invalid_width", "invalid_height", "invalid_mines", "invalid_action",
    "invalid_coordinates", "out_of_bounds", "invalid_revision",
    "stale_revision", "game_over", "game_not_started", "invalid_deductions",
    "invalid_layout",
};

typedef struct GameStep {
    uint32_t op;                   /* FixtureGameOp */
    int64_t raw_action;            /* FIXTURE_OP_RAW_ACTION only */
    int64_t row;
    int64_t col;
    int64_t revision;              /* argument, or FIXTURE_REV_CURRENT */
    int64_t advance_ms;            /* FIXTURE_OP_ADVANCE only */
    const int64_t *safe;           /* FIXTURE_OP_DEDUCE inputs (raw indices) */
    uint32_t safe_count;
    const int64_t *mines;
    uint32_t mine_count;
    uint32_t expect;               /* FixtureExpect */
    uint32_t error;                /* FixtureGameError when expect == ERROR */
    /* State after the step (the unchanged state for no-ops and errors). */
    uint32_t status;               /* FixtureGameStatus */
    int64_t revision_after;
    int64_t flags_after;
    int64_t elapsed_ms;            /* FIXTURE_UNCHECKED: not checked */
    uint32_t revealed_count;
    const char *board;             /* render, or NULL for large boards */
    const uint32_t *exploded;
    uint32_t exploded_count;
} GameStep;

typedef struct GameScript {
    const char *name;
    const char *provenance;
    uint32_t width;
    uint32_t height;
    uint32_t mines;
    const uint32_t *layout;        /* fixed mine cells placed at the first reveal;
                                      NULL when the script never places mines */
    uint32_t layout_count;
    int64_t clock_start_ms;        /* injected monotonic clock at creation */
    const GameStep *steps;
    uint32_t step_count;
} GameScript;

typedef struct GameConfigCase {
    int64_t width;
    int64_t height;
    int64_t mines;
    uint32_t error;                /* FIXTURE_ERR_NONE: accepted */
    int64_t max_mines;             /* width*height - 9 when dimensions are valid, else -1 */
} GameConfigCase;

/* ------------------------------------------------------------- autosolve */

typedef enum FixtureAutosolveEventKind {
    FIXTURE_AUTOSOLVE_BATCH = 0,          /* solve current revision, apply its proofs atomically */
    FIXTURE_AUTOSOLVE_MANUAL_REVEAL = 1   /* the player's surviving reveal of `cell` */
} FixtureAutosolveEventKind;

typedef struct AutosolveEvent {
    uint32_t kind;
    uint32_t cell;                 /* MANUAL_REVEAL only */
    uint32_t solver_status;        /* BATCH: baseline status of the solve */
    uint32_t proven_safe_count;    /* BATCH: proofs of the solve (counts always set) */
    uint32_t proven_mine_count;
    const FixtureRun *proven_safe; /* BATCH: full lists, or NULL for large boards */
    uint32_t proven_safe_runs;
    const FixtureRun *proven_mines;
    uint32_t proven_mine_runs;
    uint32_t changed;              /* 0 on a pause: nothing certain is left to play */
    uint32_t status;               /* FixtureGameStatus after the event */
    int64_t revision_after;
    int64_t flags_after;
    uint32_t revealed_count;
    const char *board;             /* render after the event, or NULL */
    const ProbabilityCase *pause_odds; /* pause: exact odds of the paused observation */
} AutosolveEvent;

typedef struct AutosolveScript {
    const char *name;
    const char *provenance;
    uint32_t width;
    uint32_t height;
    uint32_t mines;
    const uint32_t *layout;        /* fixed layout placed at the first reveal, or NULL */
    uint32_t layout_count;
    const char *const *layout_rows; /* alternative to `layout` for 80x80: map rows,
                                       '*' mine, '.' safe */
    uint32_t first_cell;           /* the player's first reveal */
    const char *board_after_first;
    const AutosolveEvent *events;
    uint32_t event_count;
    uint32_t normative;            /* 1: every solve was exact, so the whole trajectory
                                      is implementation independent; 0: informational */
} AutosolveScript;

/* ---------------------------------------------------- engine result checks */

typedef enum FixtureMutationKind {
    FIXTURE_MUT_STATUS = 0,             /* raw: status value (FixtureStatus or out of range) */
    FIXTURE_MUT_PROBABILITY_COUNT = 1,  /* raw: number of per-cell entries */
    FIXTURE_MUT_ALL_PROBABILITIES = 2,  /* every cell, revealed included := value */
    FIXTURE_MUT_HIDDEN_PROBABILITIES = 3, /* every hidden cell := value */
    FIXTURE_MUT_PROBABILITY = 4,        /* target cell := value */
    FIXTURE_MUT_ADD_SAFE = 5,           /* raw: append to proven_safe */
    FIXTURE_MUT_ADD_MINE = 6,           /* raw: append to proven_mines */
    FIXTURE_MUT_META_COUNT = 7,         /* target FixtureMetaField := raw */
    FIXTURE_MUT_ELAPSED_MS = 8,         /* value */
    FIXTURE_MUT_REASON = 9,             /* raw: reason value (out of range: invalid) */
    FIXTURE_MUT_DIAGNOSTIC = 10         /* target FixtureDiagnostic := raw or value */
} FixtureMutationKind;

typedef enum FixtureMetaField {
    FIXTURE_META_FRONTIER_CELLS = 0,
    FIXTURE_META_COMPONENTS = 1,
    FIXTURE_META_UNCONSTRAINED_CELLS = 2,
    FIXTURE_META_SAMPLES = 3
} FixtureMetaField;

typedef enum FixtureDiagnostic {
    FIXTURE_DIAG_EXACT_COMPONENTS = 0,
    FIXTURE_DIAG_SAMPLED_COMPONENTS = 1,
    FIXTURE_DIAG_SAMPLE_ATTEMPTS = 2,
    FIXTURE_DIAG_EFFECTIVE_SAMPLE_SIZE = 3 /* value_kind NULL: present but null */
} FixtureDiagnostic;

typedef struct FixtureMutation {
    uint32_t kind;                 /* FixtureMutationKind */
    uint32_t target;               /* cell index or field selector */
    int64_t raw;
    uint32_t value_kind;           /* FixtureValueKind for value-carrying mutations */
    double value;
} FixtureMutation;

typedef struct EngineResultCase {
    const char *name;
    const char *provenance;
    const FixtureMutation *mutations; /* applied in order to the engine base result */
    uint32_t mutation_count;
    uint32_t accepted;             /* 0: rejected as a solver failure, nothing applied */
    const uint32_t *normalized_safe; /* accepted: proofs after normalisation */
    uint32_t normalized_safe_count;
    const uint32_t *normalized_mines;
    uint32_t normalized_mine_count;
    uint32_t batch_changed;        /* accepted: autosolve batch at revision 1 changes the game */
    int64_t revision_after_batch;
    const char *board_after_batch; /* NULL when rejected */
} EngineResultCase;

/* ----------------------------------------------------------------- bigint */

/* C(n, k) known answers (C(n, k) == 0 when k > n). */
typedef struct BigintBinomialCase {
    uint32_t n;
    uint32_t k;
    FixtureBig value;
    uint32_t bit_length;
} BigintBinomialCase;

/* Exact ratio to double, as in the solver's final step. `exact` selects the
 * endpoint guard (exact results never round a non-certain value to 0 or 1;
 * approximate results are converted unmodified). tolerance 0: the result
 * must equal `expected` bit for bit; otherwise |result - expected| <= it. */
typedef struct BigintRatioCase {
    const char *name;
    const char *provenance;
    FixtureBig num;
    FixtureBig den;
    uint32_t exact;
    uint64_t expected_bits;        /* binary64 bits (fixture_f64) */
    double tolerance;
} BigintRatioCase;

/* Exact low-order polynomial division (histogram cavities): coefficient
 * arrays are ascending powers. When `exact` is 0 some step leaves a nonzero
 * remainder and the division must fail rather than truncate. */
typedef struct BigintPolyDivisionCase {
    const char *name;
    const FixtureBig *dividend;
    uint32_t dividend_len;
    const FixtureBig *divisor;
    uint32_t divisor_len;
    const FixtureBig *quotient;    /* first dividend_len coefficients (exact cases) */
    uint32_t quotient_len;
    uint32_t exact;
} BigintPolyDivisionCase;

/* ---------------------------------------------------------------- helpers */

/* Reference doubles are stored as IEEE-754 binary64 bit patterns so that no
 * compiler needs C99 hexadecimal floating constants (MSVC's C mode lacks
 * them) and no decimal rounding is involved. */
static inline double fixture_f64(uint64_t bits) {
    union {
        uint64_t u;
        double d;
    } pun;
    pun.u = bits;
    return pun.d;
}

static inline uint32_t fixture_run_cell(const FixtureRun *run, uint32_t k) {
    return run->start + k * run->step;
}

static inline uint32_t fixture_runs_size(const FixtureRun *runs, uint32_t run_count) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < run_count; i++) {
        total += runs[i].count;
    }
    return total;
}

static inline bool fixture_runs_contain(const FixtureRun *runs, uint32_t run_count, uint32_t cell) {
    for (uint32_t i = 0; i < run_count; i++) {
        const FixtureRun *run = &runs[i];
        if (cell < run->start || run->step == 0) {
            continue;
        }
        uint32_t offset = cell - run->start;
        if (offset % run->step == 0 && offset / run->step < run->count) {
            return true;
        }
    }
    return false;
}

/* Character of `cell` in a row-array map/render, or '\0' when out of range. */
static inline char fixture_rows_at(const char *const *rows, uint32_t width, uint32_t height, uint32_t cell) {
    if (rows == NULL || width == 0 || cell / width >= height) {
        return '\0';
    }
    return rows[cell / width][cell % width];
}

/* Fills out[cell] with the clue (0..8) or FIXTURE_HIDDEN_CELL. Returns false
 * if the case is malformed (index outside the board, duplicate clue, bad map
 * character or row length) or cell_count != width * height. */
static inline bool fixture_case_dense_clues(const ProbabilityCase *c, int8_t *out, uint32_t cell_count) {
    if (cell_count != c->width * c->height) {
        return false;
    }
    for (uint32_t i = 0; i < cell_count; i++) {
        out[i] = FIXTURE_HIDDEN_CELL;
    }
    if (c->board_rows != NULL) {
        for (uint32_t r = 0; r < c->height; r++) {
            const char *row = c->board_rows[r];
            for (uint32_t col = 0; col < c->width; col++) {
                char ch = row[col];
                if (ch >= '0' && ch <= '8') {
                    out[r * c->width + col] = (int8_t)(ch - '0');
                } else if (ch != '.' && ch != '*') {
                    return false;
                }
            }
            if (row[c->width] != '\0') {
                return false;
            }
        }
        return true;
    }
    for (uint32_t r = 0; r < c->clue_run_count; r++) {
        const FixtureClueRun *run = &c->clues[r];
        for (uint32_t k = 0; k < run->count; k++) {
            uint32_t cell = run->start + k * run->step;
            if (cell >= cell_count || out[cell] != FIXTURE_HIDDEN_CELL || run->clue > 8) {
                return false;
            }
            out[cell] = (int8_t)run->clue;
        }
    }
    return true;
}

/* Odds class of a hidden cell, or -1 when the case records none for it. */
static inline int32_t fixture_case_class(const ProbabilityCase *c, uint32_t cell) {
    for (uint32_t i = 0; i < c->class_count; i++) {
        if (fixture_runs_contain(c->classes[i].runs, c->classes[i].run_count, cell)) {
            return (int32_t)i;
        }
    }
    return c->rest_class;
}

/* Board-map truth: whether the generating layout put a mine on `cell`. */
static inline bool fixture_case_layout_mine(const ProbabilityCase *c, uint32_t cell) {
    return fixture_rows_at(c->board_rows, c->width, c->height, cell) == '*';
}
