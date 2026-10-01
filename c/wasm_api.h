#pragma once

/*
 * wasm_api.h - the WebAssembly reactor ABI of the Minesweeper engine.
 *
 * This header is the complete contract between the compiled module and the
 * JavaScript adapter: export names, parameter/return types, buffer rules,
 * memory budgets and the intended call sequences. Buffer layouts, status
 * codes and enums are the ones in engine.h (offsets are restated below and
 * checked at compile time). The same .wasm file is instantiated twice per
 * tab: the GAME instance on the main thread (ms_new_game ... ms_apply_
 * autosolve) and the SOLVER instance in a dedicated worker
 * (ms_solve_observation only). The engine never runs the solver itself.
 *
 * Instantiation
 * -------------
 *   imports: corec's WASI host (platform/js/wasi.js: makeWasi(io).imports,
 *            then wasi.setMemory(instance.exports.memory)) and
 *            ms_host: { now_ms: () => performance.now() }   (f64, monotonic)
 *   1. ms_abi_version() === MS_ABI_VERSION (1), else refuse the module;
 *   2. ms_init() === MS_OK exactly once per instance.
 *   A trap or corec ProcExit escaping any export leaves the instance
 *   unusable: discard it (solver: recreate the worker; game: fatal error).
 *
 * Value passing
 * -------------
 * - Every parameter is an i32 holding a uint32 value. WebAssembly converts JS
 *   numbers with ToInt32 (fractions truncate, NaN -> 0, true -> 1), so JS
 *   must reject non-integers, booleans, NaN and values outside 0..2^32-1
 *   before calling. C re-checks every range. 64-bit seeds travel as two
 *   uint32 halves (seed_lo, seed_hi).
 * - Exports returning int32_t return an ms_status (0 = MS_OK). Exports
 *   returning uint32_t return a pointer or size; JS receives i32 results as
 *   signed numbers, so read them as `x >>> 0`.
 * - Revisions and generations stay below 2^31 (MS_REVISION_MAX,
 *   MS_GENERATION_MAX). A JS safe integer above MS_REVISION_MAX can never be
 *   the current revision: report stale_revision without calling C (Python
 *   semantics); negative values are invalid_revision.
 *
 * Buffers and pointers
 * --------------------
 * - Every pointer argument must lie inside ONE live buffer obtained from
 *   ms_alloc: [ptr, ptr + len) within it (interior pointers are allowed, so
 *   one scratch buffer can hold several small out-params), aligned to 8 for
 *   ABI buffers (views, observations, results, limits) and to 4 for uint32
 *   out-params. All buffers of one call - inputs and outputs - must be
 *   pairwise disjoint (adjacent is fine): aliasing is rejected, never given
 *   an in-place meaning. Anything else - 0, a wild pointer (even beyond linear
 *   memory), a freed or foreign pointer, a range crossing the end of the
 *   bytes requested from ms_alloc, a pointer into engine/static memory - is
 *   MS_ERR_INVALID_BUFFER, checked before every other condition and without
 *   reading caller memory, so a bad pointer can neither trap nor reach
 *   engine state, and a rejected call leaves every buffer unchanged. (Host
 *   buffers come from their own allocation context, separate from the
 *   engine's games and the solver workspace.)
 * - ms_alloc returns zero-filled, 16-byte aligned memory. Budgets:
 *   MS_WASM_MAX_BUFFERS live buffers, MS_WASM_MAX_BUFFER_BYTES each,
 *   MS_WASM_BUFFER_BUDGET bytes in total, where a buffer costs its size
 *   plus a small header rounded up to a power of two, at least 4 KiB (a
 *   4 MiB buffer costs 8 MiB); beyond them ms_alloc returns 0.
 * - Outputs are written only on MS_OK; on any error output buffers are
 *   unspecified and every mutating call has changed nothing (all pointers
 *   are validated before any state changes).
 * - Calls that may grow linear memory (ms_alloc, ms_new_game,
 *   ms_solve_observation) detach existing ArrayBuffer views. Simplest rule:
 *   re-read `memory.buffer` and rebuild typed arrays/DataViews after every
 *   call, and copy outputs into JS-owned objects before the next call.
 * - JS owns its buffers and frees them with ms_free; the engine never keeps
 *   a pointer to a caller buffer after returning.
 *
 * Status names
 * ------------
 * ms_status_text(status) / ms_reason_text(reason) return pointers to static
 * NUL-terminated ASCII strings (engine.h names: "stale_revision",
 * "time_budget_exhausted", ...). Statuses 1..31 are input errors, 32..47
 * conflicts (stale_revision, game_over, game_not_started: refresh the view
 * with ms_get_view and show the latest state), 48 inconsistent_observation
 * (never expected for a real game: treat as an engine failure), 64..
 * resource exhaustion / internal errors.
 *
 * Game instance call sequences
 * ----------------------------
 *   new game:   ms_new_game(w, h, mines, lo, hi, out) -> generation;
 *               (re)allocate view/obs/result buffers for w x h;
 *               ms_get_view(generation, view, ms_view_bytes(w, h)).
 *   move:       ms_act(generation, revision, action, row, col, out) ->
 *               changed; then ms_get_view. stale_revision/game_over: no
 *               change, refresh with ms_get_view.
 *   odds for the view's (generation, revision):
 *               ms_get_cached_result(...) -> available. 1: show the buffer
 *               (placeholder or reusable exact/approximate result). 0:
 *               ms_get_observation -> copy the bytes -> post to the solver
 *               worker with {generation, revision, request id} -> worker
 *               returns result bytes -> drop it unless generation, revision
 *               and request id are still current -> copy into a result
 *               buffer -> ms_accept_result(generation, revision, ...). MS_OK:
 *               show the JS copy (UNAVAILABLE results are not served by
 *               ms_get_cached_result again: a retry solves anew).
 *   autosolve:  ms_apply_autosolve(generation, revision, avail, changed).
 *               available 0: obtain and accept a result for this revision
 *               first, then call again. changed 1: refresh the view and
 *               continue with the new revision; changed 0: pause (nothing
 *               certain left; the odds stay visible).
 *
 * Solver instance
 * ---------------
 *   ms_init_default_limits(limits, MS_WASM_LIMITS_BYTES) once, then per
 *   request ms_solve_observation(obs, obs_len, limits, MS_WASM_LIMITS_BYTES,
 *   result, result_len). Its workspace (at most MS_WASM_SOLVER_BUDGET) is
 *   fully released after every call; ms_live_bytes(MS_WASM_POOL_SOLVER) is
 *   0 between calls. To cancel a running solve, terminate the worker.
 */

#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * Budgets of each instance (fixed in the module)
 * ------------------------------------------------------------------------ */
#define MS_WASM_MAX_BUFFERS 64u                      /* live ms_alloc buffers */
#define MS_WASM_MAX_BUFFER_BYTES ((uint32_t)4 << 20) /* one ms_alloc request */
#define MS_WASM_BUFFER_BUDGET ((size_t)32 << 20)     /* all ms_alloc buffers */
#define MS_WASM_ENGINE_BUDGET ((size_t)8 << 20)      /* games + stored result */
/* Outer cap of the solver workspace; ms_infer_limits.memory_budget_bytes
 * above it is effectively clamped to it. */
#define MS_WASM_SOLVER_BUDGET ((size_t)MS_DEFAULT_MEMORY_BUDGET)

/* ms_live_bytes pools */
#define MS_WASM_POOL_BUFFERS 0u /* ms_alloc buffers (budget accounting) */
#define MS_WASM_POOL_ENGINE 1u  /* current game, stored result, scratch */
#define MS_WASM_POOL_SOLVER 2u  /* solver workspace; 0 between calls */

#define MS_WASM_LIMITS_BYTES 56u /* sizeof(ms_infer_limits) */

/* ------------------------------------------------------------------------
 * Layout reference for the adapter (little-endian, byte offsets)
 *
 * view (ms_view_header, then uint8 cells[w*h] at 48):
 *   0 magic 0x4D530001, 4 version, 8 generation, 12 revision, 16 status
 *   (1 ready, 2 playing, 3 won, 4 lost), 20 width, 24 height, 28 mines,
 *   32 flags, 36 revealed, 40 elapsed_ms (f64, whole ms).
 *   cell byte: bits 0-3 clue (0x0F = none), 0x10 revealed, 0x20 flagged,
 *   0x40 mine (won/lost only), 0x80 exploded.
 * observation (ms_obs_header, then uint8 clues[w*h] at 32; 0xFF hidden):
 *   0 magic 0x4D530002, 4 version, 8 width, 12 height, 16 total_mines,
 *   20 revealed, 24/28 reserved (0).
 * limits (ms_infer_limits, 56 bytes):
 *   0 magic 0x4D530003, 4 version, 8 node_budget, 12 sample_budget,
 *   16 max_stored_entries, 20 flags, 24 time_budget_ms (f64),
 *   32 min_effective_samples (f64), 40 memory_budget_bytes (u64), 48 seed.
 * result (ms_result_header, then f64 probabilities[w*h] at 120, then
 * uint8 flags[w*h] at 120 + 8*w*h):
 *   0 magic 0x4D530004, 4 version, 8 status (1 exact, 2 approximate,
 *   3 unavailable, 4 not-started, 5 finished), 12 reason, 16 width,
 *   20 height, 24 total_mines, 28 revealed, 32 observation_hash (u64),
 *   40 frontier_cells, 44 components, 48 unconstrained_cells, 52 samples,
 *   56 exact_components, 60 sampled_components, 64 sample_attempts,
 *   68 nodes, 72 hidden_cells, 76 remaining_mines, 80 propagated_cells,
 *   84 pair_reasoning_complete, 88 proven_safe, 92 proven_mines,
 *   96 has_effective_sample_size, 100 reserved, 104 elapsed_ms (f64),
 *   112 effective_sample_size (f64).
 *   flag byte: 0x01 value present, 0x02 proven safe, 0x04 proven mine.
 * Buffer sizes: ms_view_bytes / ms_obs_bytes / ms_result_bytes below.
 * ------------------------------------------------------------------------ */
_Static_assert(MS_OFFSETOF(ms_view_header, generation) == 8, "view layout");
_Static_assert(MS_OFFSETOF(ms_view_header, status) == 16, "view layout");
_Static_assert(MS_OFFSETOF(ms_view_header, flags) == 32, "view layout");
_Static_assert(MS_OFFSETOF(ms_obs_header, revealed) == 20, "observation layout");
_Static_assert(MS_OFFSETOF(ms_infer_limits, memory_budget_bytes) == 40, "limits layout");
_Static_assert(sizeof(ms_infer_limits) == MS_WASM_LIMITS_BYTES, "limits layout");
_Static_assert(MS_OFFSETOF(ms_result_header, frontier_cells) == 40, "result layout");
_Static_assert(MS_OFFSETOF(ms_result_header, nodes) == 68, "result layout");
_Static_assert(MS_OFFSETOF(ms_result_header, proven_safe) == 88, "result layout");
_Static_assert(MS_OFFSETOF(ms_result_header, has_effective_sample_size) == 96, "result layout");
_Static_assert(MS_OFFSETOF(ms_result_header, effective_sample_size) == 112, "result layout");

#if defined(__wasm__)
#define MS_WASM_EXPORT(name) __attribute__((export_name(#name))) name
#else
#define MS_WASM_EXPORT(name) name
#endif

/* ------------------------------------------------------------------------
 * Lifecycle, buffers and names (both instances)
 * ------------------------------------------------------------------------ */

/* MS_ABI_VERSION. Callable before ms_init. */
uint32_t MS_WASM_EXPORT(ms_abi_version)(void);

/* Initializes the instance: platform_init, the three budgeted pools, the
 * host clock and an engine without a game. MS_OK once; MS_ERR_INTERNAL for
 * every later call (nothing changes). Before it, every other export except
 * ms_abi_version, the *_bytes size helpers and the *_text names returns
 * MS_ERR_INTERNAL, or 0 for uint32_t results. */
int32_t MS_WASM_EXPORT(ms_init)(void);

/* A zero-filled, 16-byte aligned buffer of `size` (1..MS_WASM_MAX_BUFFER_BYTES)
 * bytes, or 0 (before ms_init, size out of range, MS_WASM_MAX_BUFFERS live,
 * or MS_WASM_BUFFER_BUDGET exhausted). */
uint32_t MS_WASM_EXPORT(ms_alloc)(uint32_t size);

/* Frees a buffer from ms_alloc (its start address). 0 is a no-op (MS_OK);
 * any other pointer that is not the start of a live buffer (double free,
 * interior or foreign pointer) is MS_ERR_INVALID_BUFFER and changes
 * nothing. */
int32_t MS_WASM_EXPORT(ms_free)(uint32_t ptr);

/* Buffer sizes in bytes for a width x height board: ms_view_size,
 * ms_obs_size and ms_result_size of engine.h; 0 for invalid dimensions
 * (width, height >= 1, width * height <= MS_SOLVER_MAX_CELLS). Pure;
 * callable before ms_init. */
uint32_t MS_WASM_EXPORT(ms_view_bytes)(uint32_t width, uint32_t height);
uint32_t MS_WASM_EXPORT(ms_obs_bytes)(uint32_t width, uint32_t height);
uint32_t MS_WASM_EXPORT(ms_result_bytes)(uint32_t width, uint32_t height);

/* Static NUL-terminated names: ms_status_name() ("unknown_status" outside
 * the enum) and ms_prob_reason_name() ("" for MS_REASON_NONE, 0 outside the
 * enum). Pure; callable before ms_init. */
uint32_t MS_WASM_EXPORT(ms_status_text)(int32_t status);
uint32_t MS_WASM_EXPORT(ms_reason_text)(uint32_t reason);

/* Diagnostics: bytes currently held by an MS_WASM_POOL_* pool (0 for an
 * unknown pool or before ms_init). JS tests use it to prove that buffers
 * are freed, that new games release the old one and that every solve
 * releases its workspace. */
uint32_t MS_WASM_EXPORT(ms_live_bytes)(uint32_t pool);

/* ------------------------------------------------------------------------
 * Game instance: one current game per instance (ms_engine_* of engine.h)
 * ------------------------------------------------------------------------ */

/* Starts a new READY game with the next generation (1, 2, ...) and drops
 * the previous game and its stored result. seed = seed_lo | seed_hi << 32
 * (host entropy: crypto.getRandomValues). Writes the new generation to the
 * uint32 at generation_out. Errors: MS_ERR_INVALID_BUFFER (generation_out),
 * MS_ERR_INVALID_WIDTH / _HEIGHT / _MINES (5..80, 1..w*h-9, in that order),
 * MS_ERR_RESOURCE_EXHAUSTED (generations or engine memory exhausted); on
 * any error the current game is untouched. */
int32_t MS_WASM_EXPORT(ms_new_game)(uint32_t width, uint32_t height, uint32_t mines,
                                    uint32_t seed_lo, uint32_t seed_hi,
                                    uint32_t generation_out);

/* One player action (ms_action: 1 reveal, 2 flag toggle, 3 chord) on
 * (row, col) of the current game, based on (generation, revision). Writes
 * changed (uint32 0/1) to changed_out; changed 1 means the revision
 * advanced by exactly one. Validation order: changed_out ->
 * MS_ERR_INVALID_BUFFER; action -> MS_ERR_INVALID_ACTION; row/col ->
 * MS_ERR_OUT_OF_BOUNDS; generation/revision range -> MS_ERR_INVALID_REVISION;
 * not current -> MS_ERR_STALE_REVISION; won/lost -> MS_ERR_GAME_OVER;
 * revision at MS_REVISION_MAX -> MS_ERR_RESOURCE_EXHAUSTED. Errors change
 * nothing. */
int32_t MS_WASM_EXPORT(ms_act)(uint32_t generation, uint32_t revision, uint32_t action,
                               uint32_t row, uint32_t col, uint32_t changed_out);

/* Writes the public view of game `generation` (any status, any revision;
 * the header carries the current revision). view_len must equal
 * ms_view_bytes(width, height). Errors: MS_ERR_INVALID_BUFFER,
 * MS_ERR_INVALID_REVISION (generation 0 or above MS_GENERATION_MAX),
 * MS_ERR_STALE_REVISION (not the current game). No side effects. */
int32_t MS_WASM_EXPORT(ms_get_view)(uint32_t generation, uint32_t view, uint32_t view_len);

/* Writes the public observation (the solver's only input: revealed clues,
 * total mines; no flags, no layout) of a PLAYING game at exactly
 * (generation, revision). obs_len must equal ms_obs_bytes(width, height).
 * Errors: MS_ERR_INVALID_BUFFER, MS_ERR_INVALID_REVISION,
 * MS_ERR_STALE_REVISION, MS_ERR_GAME_NOT_STARTED (ready),
 * MS_ERR_GAME_OVER (won/lost). */
int32_t MS_WASM_EXPORT(ms_get_observation)(uint32_t generation, uint32_t revision,
                                           uint32_t obs, uint32_t obs_len);

/* The odds JS can show for (generation, revision) without solving. Writes
 * available (uint32 0/1) to available_out on MS_OK:
 *   1: `result` now holds the answer: the NOT_STARTED placeholder (ready),
 *      the FINISHED placeholder (won/lost; reason game_over), or a copy of
 *      the latest result accepted for this revision when it is EXACT or
 *      APPROXIMATE;
 *   0: nothing reusable (no accepted result, or the latest accepted one is
 *      UNAVAILABLE): request a solve; `result` is left unchanged.
 * result_len must equal ms_result_bytes(width, height). Errors:
 * MS_ERR_INVALID_BUFFER, MS_ERR_INVALID_REVISION, MS_ERR_STALE_REVISION. */
int32_t MS_WASM_EXPORT(ms_get_cached_result)(uint32_t generation, uint32_t revision,
                                             uint32_t result, uint32_t result_len,
                                             uint32_t available_out);

/* Hands a solver result (bytes from the worker's ms_solve_observation) to
 * the engine for (generation, revision). It is accepted only for the
 * current revision of a PLAYING game and only if it passes
 * ms_result_validate against a freshly built observation of that game
 * (same observation_hash) plus the engine's count checks (engine.h,
 * "Result acceptance"). The latest accepted result replaces any earlier one
 * for the revision, whatever the statuses (no result is assumed to contain
 * another's proofs): EXACT/APPROXIMATE results are then reusable through
 * ms_get_cached_result; an UNAVAILABLE one is never served again (a retry
 * solves anew) but its proofs feed ms_apply_autosolve. Errors (nothing
 * stored): MS_ERR_INVALID_BUFFER, MS_ERR_INVALID_REVISION,
 * MS_ERR_STALE_REVISION (a late result), MS_ERR_GAME_NOT_STARTED,
 * MS_ERR_GAME_OVER, MS_ERR_INVALID_RESULT. */
int32_t MS_WASM_EXPORT(ms_accept_result)(uint32_t generation, uint32_t revision,
                                         uint32_t result, uint32_t result_len);

/* One certainty-only autosolve batch for (generation, revision), using only
 * the proof flags of the latest result accepted for that revision (any
 * status; sampled 0/1 values are never proofs): flags proven mines, clears
 * flags on proven-safe cells, then reveals them (floods included), all as
 * one revision. Writes available (uint32 0/1) and changed (uint32 0/1) on
 * MS_OK: available 0 means no result is accepted for the revision yet
 * (nothing changed; solve, accept, retry); changed 0 with available 1 is a
 * pause. Errors: MS_ERR_INVALID_BUFFER, MS_ERR_INVALID_REVISION,
 * MS_ERR_STALE_REVISION (e.g. a repeated batch), MS_ERR_GAME_NOT_STARTED,
 * MS_ERR_GAME_OVER, MS_ERR_RESOURCE_EXHAUSTED (MS_REVISION_MAX). */
int32_t MS_WASM_EXPORT(ms_apply_autosolve)(uint32_t generation, uint32_t revision,
                                           uint32_t available_out, uint32_t changed_out);

/* ------------------------------------------------------------------------
 * Solver instance (worker)
 * ------------------------------------------------------------------------ */

/* Writes the default inference limits (ms_limits_default: the server
 * policy) into `limits`; limits_len must be MS_WASM_LIMITS_BYTES. */
int32_t MS_WASM_EXPORT(ms_init_default_limits)(uint32_t limits, uint32_t limits_len);

/* ms_solve on a public observation with the instance's workspace and the
 * ms_host clock. `obs`, `limits` and `result` must be pairwise disjoint
 * (checked, with ownership and alignment, before the solver initializes
 * the result; MS_ERR_INVALID_BUFFER leaves all three unchanged);
 * limits_len must be MS_WASM_LIMITS_BYTES; result_len must equal
 * ms_result_bytes(width, height) of the observation. Returns ms_solve's
 * status: MS_OK with an EXACT, APPROXIMATE or UNAVAILABLE result (budget
 * exhaustion is not an error), or MS_ERR_INVALID_BUFFER /
 * MS_ERR_INVALID_OBSERVATION / MS_ERR_INVALID_LIMITS /
 * MS_ERR_INCONSISTENT / MS_ERR_INTERNAL (no result; never show it as odds).
 * The workspace is released before returning. Never called by the game
 * instance's engine. */
int32_t MS_WASM_EXPORT(ms_solve_observation)(uint32_t obs, uint32_t obs_len, uint32_t limits,
                                             uint32_t limits_len, uint32_t result,
                                             uint32_t result_len);

#ifdef __cplusplus
}
#endif
