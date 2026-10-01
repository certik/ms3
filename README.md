# Minesweeper with mine odds

A browser Minesweeper game with an optional **mine-odds overlay**. Turn it on
and every hidden cell shows its chance of holding a mine. The odds are exact
when the position can be counted within the time budget. Otherwise they are
marked as estimates, or not shown at all.

Every game rule and every probability calculation is freestanding C on
[corec](https://github.com/certik/corec), with no C library. It is compiled
once into a single WebAssembly module, `minesweeper.wasm`, which runs
entirely in the browser. The page creates two single-threaded instances of
that module: one plays the game on the main thread, the other calculates odds
in a dedicated worker. JavaScript runs no authoritative game or probability
algorithm: it draws the page, validates input, shows hints and schedules
odds requests and autosolve passes, while the C engine decides every move and
every odds result. The built site is a folder of static files with no server
code, no runtime dependencies and no external requests.

## Quick start

Requirements: `git` and [pixi](https://pixi.sh) 0.68 or newer. Pixi installs
everything else (Clang/lld 20, Wasmtime and Node.js) at the versions locked
in `pixi.lock`.

```sh
git clone --recurse-submodules https://github.com/certik/ms3.git minesweeper
cd minesweeper
pixi run --locked -e js start
```

In a clone made without `--recurse-submodules`, run
`git submodule update --init` first: corec is the Git submodule
`third_party/corec`, pinned to one commit.

`start` builds the site into `dist/` and serves it with Node's built-in HTTP
server. Then open <http://127.0.0.1:8000/> in a browser. Options go after the
task name, as in `pixi run --locked -e js start --port 8080`:

| Option | Default | Meaning |
| --- | --- | --- |
| `--port N` | `8000` | Port to listen on. |
| `--host HOST` | `127.0.0.1` | Address to listen on. The default is reachable from this computer only; `0.0.0.0` makes the game reachable from other devices on your network. |
| `--base /PREFIX/` | `/` | Serve the site under a subpath, as a static host would. |

The server only serves the files in `dist/`, read-only. Stop it with
`Ctrl+C`. `pixi run --locked -e js serve` serves an existing `dist/` without
rebuilding it. Always open the game through a web server: browsers block
ES modules, module workers and the engine download for a page opened directly
from disk. The browser tests cover Chromium, Firefox and WebKit (Safari's
engine).

### Games live in the tab

There is no game server and no saved game. Each browser tab runs its own
engine.

- Reloading or closing the tab ends the game. After a reload the page starts
  a fresh game with your board settings.
- The page remembers, in `localStorage`, the board size and mine count, the
  zoom, and whether **Mine odds** and **Autosolve** are on. It never stores
  a game. Both modes are on by default for fresh pages; saved off settings
  are respected. Autosolve still waits for your first reveal.
- Moves are applied locally by the game instance, synchronously, so a move
  cannot be lost on the way or applied twice.
- If the engine cannot be loaded, for example because `minesweeper.wasm` is
  missing or was built for a different engine ABI version, the page says so
  and offers **Try again**. If the game engine stops, the board stays visible
  but frozen, and a new game starts a fresh engine instance. Failures of the
  odds calculation never affect the game.

## How to play

Open every cell that does not hide a mine. A number tells you how many of the
8 surrounding cells hold mines.

| Action | Mouse | Keyboard | Touch |
| --- | --- | --- | --- |
| Reveal a cell | Left-click | `Enter` or `Space` | Tap |
| Chord: open the other neighbors of a number whose flags are all placed | Click the number (middle-click also works) | `Enter` or `Space` on the number | Tap the number |
| Flag or unflag | Right-click | `F` | Switch to **Flag** mode and tap, or long-press |
| Move between cells | | Arrow keys, `Home`/`End` (row ends), `Ctrl+Home`/`Ctrl+End` (corners), `PageUp`/`PageDown` (5 rows) | |

- **Difficulty presets:** Beginner 9 x 9 with 10 mines, Intermediate 16 x 16
  with 40, Expert 30 x 16 (width 30, height 16) with 99, and Large 50 x 30 with 225.
- **Custom boards:** width and height from 5 to 80, and from 1 to
  `width x height - 9` mines.
- **The first reveal is always safe.** Mines are placed only after your first
  reveal, and never in that cell or its neighbors (up to 9 cells in a 3 x 3
  block). The first cell is therefore always a 0 and opens an area. This is
  why a custom board holds at most `width x height - 9` mines.
- **Flags are your notes.** The counter shows mines minus flags placed, and it
  goes negative if you place more flags than there are mines. A flagged cell is
  protected from an accidental reveal. Chording relies on your flags, so a
  wrong flag can make a chord hit a mine.
- **Board size:** **Fit** sizes the cells to your screen. The zoom buttons pick
  a fixed size. Large boards never shrink below a legible size; they scroll
  inside the board frame instead.
- The timer starts on your first reveal and stops when you win or lose.
  Press **New game**, the face button, or **Play again** after a game ends.
- **Winning** flags every remaining mine automatically, so the counter ends
  at 0.
- A move that changes nothing is ignored and leaves the game's revision
  unchanged. Examples are revealing a flagged cell, or chording when the flag
  count does not match the number. The page explains why in the line under
  the board.

The board is an ARIA grid with a single tab stop. Each cell has a label with
its row and column, its content (number, flag, hidden) and, when the overlay
is on, its odds and whether they are exact. Game events are announced through
a polite live region; the timer is not announced.

## The mine-odds overlay

Switch on **Mine odds** above the board. The page then asks the solver for
the odds of the exact position on screen, identified by game generation,
revision and a request id. It works like this:

- **Moves never wait for the solver.** The solver runs in its own worker, so
  the page stays responsive while it calculates. Starting a move, starting a
  new game or turning the overlay off hides the old odds immediately and
  cancels the pending calculation. A running WebAssembly call cannot be
  interrupted, so the page terminates the worker and later starts a fresh one
  from the already compiled module. Answers for an older position are
  discarded, the game engine checks every result against the current position
  before accepting it, and the page never draws odds for a position other
  than the one shown.
- **Exact odds** appear as plain percentages such as `24%`. An exact value
  below 1% or above 99% that is not certain is shown as `<1%` or `>99%`, never
  rounded to `0%` or `100%`.
- **Estimated odds** carry a tilde, such as `~24%`, and the panel shows
  **Estimated**. A sampled value of 0 or 1 is shown as `~0%` or `~100%`. It is
  never presented as certain.
- **Proven cells** are marked with a check (safe) or a mine icon (mine). They
  come from logical deduction or exact counting, so they stay certain even
  when the rest of the board is estimated.
- **Unavailable:** if no trustworthy estimate fits in the budget, no numbers
  are shown (except proven cells), the explanation stays visible, and
  **Retry odds** runs the calculation again. Unavailable results are not
  reused, so a retry can succeed after a temporary budget shortfall. If the
  calculation fails, or does not finish within 60 seconds (a hang guard; the
  solver's own budget is 1.5 seconds), the page shows the error with
  **Retry odds**. The game is not affected.
- **Before the first reveal** there are no odds, because the mines have not
  been placed yet. The panel reminds you that the first reveal is safe.
- **After the game ends** the real layout is on the board, so no odds are
  calculated.
- The panel also lists a few solver details: frontier cells, independent
  components (and how many were sampled), unconstrained cells and solve time.
  For estimates it adds the sample count and the effective sample size.

Colors run from teal (low risk) through amber to red (high risk). The number
is always printed on the cell as well, so color is never the only cue.

**Flags are not evidence.** The solver ignores flags completely: a flagged
cell is just another hidden cell and gets its own odds. If you have flagged a
cell that is proven safe, the panel points this out.

## Autosolve

Turn on **Autosolve** to keep the odds visible and automatically play every
certain move. Make the first reveal yourself. The engine then flags proven
mines, removes any flags on proven-safe cells, and opens those safe cells,
including their normal zero-cell flood fill. It recalculates and repeats until
no more certain moves remain, or the game ends.

The remaining uncertain cells keep their percentages. Choose one yourself;
if you survive, autosolve resumes automatically. It **never guesses** and
never treats a sampled 0% or 100% estimate as a proof. Even when full odds are
unavailable, logically proven moves can still be played.

Switch Autosolve off to pause: no further batch is started, and moves already
played stay on the board. Turning Mine odds off also turns Autosolve off.
Autosolve is remembered across reloads and new games, which still wait for
your first reveal. A batch that fails or changes nothing is not retried for
the same position; toggle Autosolve off and on to try again.

Each automatic pass is one atomic batch in the C engine and increments the
revision once if anything changes. The batch applies only the proofs of the
latest result the engine validated for the current revision. Proofs computed
for an old revision are rejected, and the page has no way to submit its own
list of cells to play.

## How the odds are computed

The solver (`ms_solve` in `c/probability.c`) sees only public information: a
copied *observation* holding the board size, the total number of mines, and
the numbers on revealed cells. It never receives the hidden layout, the game's
random seed or flags, and it runs in a separate WebAssembly instance that
shares no memory with the game.

**Model.** Every hidden cell is a Boolean variable: 0 for safe, 1 for a mine.
Each revealed number gives one equality: the number of mines among its
hidden neighbors equals the clue. For example, a revealed 2 touching three
hidden cells gives `a + b + c = 2`. The total mine count gives one global
equality covering the whole board.

Mines are placed uniformly at random, apart from the first-reveal rule. The
first revealed cell is always a 0, and that clue already says its neighbors
are safe, so the rule adds no information beyond the revealed numbers. Every
complete board layout that satisfies the equalities is therefore equally
likely. A cell's probability is:

```text
P(cell i is a mine) =
    consistent complete layouts with a mine in cell i
    ------------------------------------------------
            all consistent complete layouts
```

This is **not** the same as reading clues locally. A `1` with three hidden
neighbors does not make each of them 33% likely, because overlapping clues and
the global mine count both change the weights.

### 1. Propagate forced assignments

A clue needing zero more mines makes its remaining neighbors safe. A clue
needing as many mines as it has remaining neighbors makes them all mines.
Comparing overlapping clues can reveal further certainties: `a + b = 1`
together with `a + b + c = 1` proves `c = 0`. The global mine count also forces
assignments when no mines remain, or every remaining cell must be a mine.

The solver (`pb_propagate`) applies these deductions and removes the fixed
variables from the remaining problem. Pairwise overlap reasoning stops at 10%
of the time budget; stopping it early only leaves more work for the counting
phase. Flags do not reduce the remaining mine count unless the solver
independently proves those mines.

### 2. Count connected regions separately

The **frontier** consists of undetermined hidden cells involved in revealed
clues. Cells connected through shared clues form a component (`pb_split`).
Each component can be counted separately, although its probabilities remain
coupled to other components by the total mine count. Hidden cells involved in
no remaining clue form one interchangeable, unconstrained pool.

For each component `c`, the solver computes a histogram:

```text
H_c[k] = number of valid assignments containing exactly k mines
```

`pb_count` uses a layered, memoized dynamic program, not whole-board
enumeration. Cells participating in exactly the same clues are grouped.
Choosing `m` mines among `g` interchangeable cells contributes `C(g, m)`
assignments, where `C` is the binomial coefficient.

The groups are ordered to keep as few clues open simultaneously as practical
(a Cuthill-McKee ordering from a pseudo-peripheral group). At each layer, a
state records the remaining mine requirements of the partly processed clues
and holds a histogram of how many mines have been placed.
Assignments leading to the same remaining requirements are merged by adding
their histogram counts. Choices that leave too many mines, too few available
cells to satisfy a clue, or exceed the remaining mine budget are pruned.
This reuses common subproblems instead of visiting every valid assignment
individually. The program is iterative and keeps its layers for the backward
pass described below; nothing recurses.

### 3. Weight each component by the rest of the board

Let `R` be the mines left after removing mines fixed by propagation, and `U`
the number of unconstrained cells. One combination of component mine counts
has weight:

```text
weight(k_1, k_2, ...) =
    H_1[k_1] * H_2[k_2] * ... * C(U, R - k_1 - k_2 - ...)
```

The binomial factor counts how many ways the leftover mines can occupy the
unconstrained cells. It is zero when the leftover count is negative or exceeds
`U`. Component assignments with more possible completions elsewhere on the
board must receive more weight.

`pb_combine_all` convolves the component histograms rather than explicitly
trying every combination of component mine counts. If `Q[t]` counts frontier
assignments with `t` mines, the total number of consistent complete layouts is:

```text
Q = H_1 * H_2 * ...                 (* denotes polynomial convolution)
Z = sum over t of Q[t] * C(U, R - t)
```

For each component, exact polynomial division of `Q` by its histogram
(`pb_cavity`, one term at a time with `bi_poly_divexact_term`) recovers the
counts for all the other components. These supply the outside weight for each
possible mine count in that component; a division that is not exact is an
internal error, never turned into odds. A weighted backward pass through the
counting states (`pb_backward`) then obtains each cell's numerator: the
number of complete layouts containing a mine in that cell. Within a group of
`g` interchangeable cells with `m` mines, requiring a particular cell to be
mined contributes `C(g - 1, m - 1)` rather than `C(g, m)`. Dividing the
resulting numerator by `Z` gives that cell's probability.

Unconstrained cells all receive the same probability: the weighted expected
number of leftover mines divided by `U`, or `E[R - t] / U`. Their odds are
therefore not simply the total mine count divided by all hidden cells.
Counts and weights stay exact multiprecision integers (`c/bigint.c`) until
the final division, so a huge binomial coefficient never overflows.
`bi_probability` rounds each exact ratio to the nearest double without first
converting the huge operands, and reserves exactly 0 and 1 for proven cells:
a tiny but nonzero probability is never shown as certain.

#### Worked example: why local possibilities are not equally likely

Suppose two revealed clues imply:

```text
a + b = 1
b + c = 1
```

There are two possible local assignments. Suppose there are also six
unconstrained cells and three mines to place overall:

| Local assignment | Mines left for the six other cells | Complete-board possibilities |
| --- | ---: | ---: |
| Only `b` is mined: `(a, b, c) = (0, 1, 0)` | 2 | `C(6, 2) = 15` |
| `a` and `c` are mined: `(a, b, c) = (1, 0, 1)` | 1 | `C(6, 1) = 6` |

Thus there are `15 + 6 = 21` consistent complete layouts:

```text
P(b is mined) = 15 / 21 = 5 / 7       (about 71.4%)
P(a is mined) = P(c is mined) = 6 / 21 (about 28.6%)
```

Each unconstrained cell has probability
`(15 * 2 + 6 * 1) / (21 * 6) = 2 / 7`, also about 28.6%.
The two local possibilities are **not 50/50**: one can be completed elsewhere
on the board in more ways than the other.

### 4. Estimate components that are too expensive to count

For a component that exceeds the exact-counting budget, the solver uses
**sequential importance sampling**. A partially explored counting search is
not treated as an exact result or as a uniform sample.

`pb_draw` assigns cells in a fixed order. It tentatively tries both
values and propagates forced assignments to detect contradictions. If only
one value survives, it takes that value. If both survive, it flips a fair coin.
A path that eventually reaches a dead end has weight zero. A draw interrupted
by the deadline is discarded entirely.

A completed assignment reached through `b` fair binary choices has proposal
probability `2**(-b)`, so it receives inverse-proposal weight `2**b`. This
correction matters: the sampler does not generate every valid assignment
equally often. Weighted samples replace that component's exact histogram and
cell counts, then undergo the same global conditioning, including the
unconstrained-pool binomial factor. The proposals are shared across all hard
components (`pb_sample_all`), and the coin flips come from a seeded
xoshiro256\*\* generator. Its seed is a hash of the observation, so the same
position gives the same estimate on every platform unless the time budget cuts
the sampling short.

The result is labeled `approximate`. After global re-weighting, each sampled
component must have at least 50 effective samples
(`MS_DEFAULT_MIN_EFFECTIVE_SAMPLES` in `c/engine.h`), calculated as:

```text
effective sample size = (sum of sample weights)**2 / sum of squared weights
```

This is a quality guard, **not an error bound or a proof of certainty**.
Too few effective samples, no globally compatible sample combinations, or a
budget exhausted before the result is complete produce `unavailable` rather
than fabricated percentages. Independently proven cells can still be reported.

### Budgets, certainty and scaling

The defaults are the `MS_DEFAULT_*` constants in `c/engine.h`, which the page
passes to the solver as an `ms_infer_limits` record:

| Budget | Default |
| --- | ---: |
| Time per calculation | 1.5 seconds |
| Exact forward-counting search nodes | 100,000 |
| Sampling proposals, shared across hard components | 2,000 |
| Effective samples required per sampled component | 50 |
| Stored dynamic-programming coefficients and edges | 1,500,000 |
| Solver workspace memory | 256 MiB |

Time is measured on the browser's monotonic clock. Counting stops at 35% of
the time budget and sampling at 80%, leaving time to combine the results.
Running out of any budget, memory included, is not an error: it produces an
estimate or an `unavailable` result with a reason, and every proof found so
far is kept.

For an exact calculation, an integer numerator of zero proves a cell safe,
and a numerator equal to `Z` proves it mined. Display rounding is never used
to establish certainty. When some components are sampled, only independent
logical deductions or proofs from exactly counted components are marked
certain: a sampled 0% or 100% alone proves nothing. **Autosolve acts only on
these proofs, never on rounded percentages or sampled endpoints.**

The game engine copies the public observation out of the game, and the worker
solves it. Before a result is shown, the game engine validates it against a
freshly built observation of the current game and revision; the result also
carries a hash of the observation it answers. Exact and approximate results
are then reused for that revision, avoiding recomputation for the same
position. Unavailable results are not reused: **Retry odds** recomputes them,
which can succeed if the budget ran short only temporarily, while their proofs
can still drive autosolve. Any move that changes the revision, and every new
game, drops the stored result, and answers for an older revision are
rejected.

**Why the odds cannot always be exact and fast.** Deciding whether a
Minesweeper position is consistent at all is NP-complete (Kaye, 2000).
Counting its consistent layouts, which is what exact probabilities require, is
#P-hard. No known algorithm gives exact answers quickly on every large board;
the worst case grows exponentially. A huge unconstrained region needs only
combinatorial counts, and separate components do not require whole-board
enumeration. The difficult work depends mainly on the frontier's structure,
especially how many clue requirements must be tracked simultaneously. Even a
long, narrow component can be cheap when the dynamic program reuses enough
states. An 80 x 80 board with a simple frontier can be cheap to count exactly,
while a smaller board with one tangled, interlocking frontier can exceed the
budget.

## How the engine runs

All game and solver algorithms are C code in `c/`, compiled with Clang for
`wasm32-wasi` into one module, `minesweeper.wasm`. JavaScript holds no
authoritative game rules or probability algorithms. Besides drawing the page,
it validates input, explains moves that would change nothing, schedules odds
requests and autosolve passes, checks argument types, copies buffers in and
out of WebAssembly memory, and rebuilds the plain objects the page draws.
The page downloads and compiles the module once and creates two independent,
single-threaded instances of it:

| Instance | Where it runs | What it does |
| --- | --- | --- |
| Game | The main thread (`static/engine-client.js`), in small synchronous calls | Holds the only copy of the game: rules, mine placement, flood fill, chording, flags, the timer, the public view, observations for the solver, the per-revision odds cache and atomic autosolve batches. |
| Solver | A dedicated module worker (`static/probability-worker.js`) | Runs `ms_solve` on a copied public observation. It never sees flags, the hidden layout, the game's seed or the game's memory, and keeps no state between requests. |

- **Responsiveness:** the main thread never runs the solver, so the page keeps
  responding while odds are calculated.
- **Cancellation:** a running WebAssembly call cannot be interrupted, so an
  obsolete calculation is cancelled by terminating the worker. The next
  request starts a new worker from the cached compiled module. A 60-second
  watchdog reports a hung worker as an error.
- **Routing:** every solve request carries the game's generation, revision
  and a request id. Answers that do not match the current request are
  ignored.
- **Randomness and time:** each new game gets a 64-bit seed from
  `crypto.getRandomValues`. The C engine places the mines with its own
  xoshiro256\*\* generator, so the same seed and moves give the same game on
  every platform. Besides corec's system interface, the module imports one
  function from the page: a monotonic millisecond clock (`performance.now`)
  for the game timer and the solver's time budget.
- **Failures:** an instance that traps is discarded. A trap in the solver
  fails only that odds request, and the worker is replaced. A trap in the game
  instance ends the current game; a new game starts a fresh instance.

**Freestanding C.** The C sources include only corec headers, never system
headers. They use no C library, no compiler runtime library and no 128-bit
integers, and are compiled with `-nostdlib -nostdinc -fno-builtin`. corec
provides memory, formatting and I/O routines, a buddy allocator and a small
platform layer: a WebAssembly system interface host (`wasi.js`) in the
browser, and direct OS calls for the native test programs (libSystem on macOS,
raw system calls on Linux, kernel32 and shell32 on Windows). The same sources
therefore build for the browser and as native test programs. The builds check
these rules: WebAssembly links never allow undefined symbols, and the module
may import only corec's system interface functions and the clock. On Windows,
MSVC-compatible compilers call `memset` and `memcpy` to initialize and copy
some local structures and arrays; `c/compiler_mem.c` forwards exactly those
two to corec's `base_memset` and `base_memcpy`, in the Windows test build
only.

## Engine ABI

JavaScript reaches the engine only through a small, versioned set of
WebAssembly exports declared in `c/wasm_api.h`. The buffer layouts and their
rules are in `c/engine.h`. The application functions work on caller-owned
buffers and return only public views, observations and results, never
handles to private game state. The module's linear memory is itself exported,
as WebAssembly requires, so anyone with the page open can inspect it,
including the hidden mine layout. This is not an anti-cheat boundary. The
separation the odds rely on is the solver's: its instance receives only the
public observation, never the hidden layout.

- **Version.** `ms_abi_version()` returns `MS_ABI_VERSION` (currently 1), and
  every buffer starts with a magic number and that version. The page refuses
  a module with another version. Any change to a layout, enum value or
  documented meaning requires a new version.
- **Setup.** `ms_init()` runs once per instance; a second call fails.
- **Buffers.** The page allocates every buffer with `ms_alloc` (zero-filled,
  16-byte aligned) and releases it with `ms_free`. A buffer is a fixed-width,
  little-endian header followed by arrays, and has an exact size
  (`ms_view_bytes`, `ms_obs_bytes`, `ms_result_bytes`). A pointer argument
  must lie inside one live buffer from `ms_alloc`; anything else is rejected
  as `invalid_buffer` before any memory is touched. Calls that can grow memory
  detach old `ArrayBuffer` views, so the page re-reads memory after each call.
  Each instance has fixed budgets: 32 MiB of buffers, 8 MiB for the game and
  its stored result, and at most 256 MiB of solver workspace, all of which is
  released after every solve.
- **Game instance:** `ms_new_game` (returns the new game's generation),
  `ms_act` (reveal, flag or chord), `ms_get_view`, `ms_get_observation`,
  `ms_get_cached_result`, `ms_accept_result` and `ms_apply_autosolve`.
- **Solver instance:** `ms_init_default_limits` and `ms_solve_observation`.
- **Both instances:** `ms_abi_version`, `ms_init`, `ms_alloc`, `ms_free`, the
  size functions, `ms_status_text`, `ms_reason_text` and `ms_live_bytes`.

**Generations and revisions.** Each new game in a tab gets the next
*generation*: 1, 2, and so on. Its *revision* starts at 0 and increases by
exactly one with each state change; a move that changes nothing leaves it
unchanged. Every call names the generation and revision it is based on, and a
mismatch is rejected as `stale_revision` before anything changes.

**GameState**, the object the page draws, is rebuilt from the view buffer:
`{id, generation, width, height, mines, status, revision, flags,
elapsed_seconds, cells}`.

- `status` is `ready` (no reveal yet), `playing`, `won` or `lost`.
- `cells` holds one entry per cell, in row-major order (index =
  `row x width + col`): `{revealed, flagged, adjacent, mine, exploded}`.
  `adjacent` is `null` until the cell is revealed. `mine` is `null` for every
  cell until the game ends, when the layout is disclosed; until then the view
  carries no information about hidden mines.

**Odds** are rebuilt from the result buffer:
`{game_id, generation, revision, status, probabilities, proven_safe,
proven_mines, message, meta}`.

- `status` is `exact`, `approximate`, `unavailable`, `not-started` or
  `finished`.
- `probabilities` has one entry per cell: a number from 0 to 1, or `null` for
  revealed cells and for cells without a trustworthy value.
- `proven_safe` and `proven_mines` list flat cell indices.
- `meta` includes `frontier_cells`, `components`, `unconstrained_cells`,
  `samples`, `elapsed_ms` and `reason`. Calculated results add
  `effective_sample_size` (or `null`), `sample_attempts`, `exact_components`
  and `sampled_components`.

**Odds cache and autosolve.** For the position on screen,
`ms_get_cached_result` answers without a new calculation when it can: with the
`not-started` placeholder before the first reveal, `finished` after the game
ends, or a stored `exact` or `approximate` result for that revision.
Otherwise the page copies the observation (`ms_get_observation`) to the
worker, which runs `ms_solve_observation`, and passes the returned result to
`ms_accept_result`, which validates it against a fresh observation before
storing it. Any revision change or new game drops the stored result. An
`unavailable` result is never served from the cache, but its proofs remain
usable: `ms_apply_autosolve` plays the proven safe cells and mines of the
latest accepted result for the current revision as one atomic batch, and
reports when no result is stored yet so that the page calculates odds first.

**Status codes.** Operations that return a status use these codes, each with
a stable snake_case name (`ms_status_text`). Exports that return a pointer or
a size do not: `ms_alloc` returns a buffer pointer, or 0 when it cannot
allocate, and the size functions return 0 for invalid dimensions.

| Code | Name | Meaning |
| ---: | --- | --- |
| 0 | `ok` | Success. |
| 1-11 | `invalid_width`, `invalid_height`, `invalid_mines`, `invalid_action`, `out_of_bounds`, `invalid_revision`, `invalid_deductions`, `invalid_observation`, `invalid_limits`, `invalid_result`, `invalid_buffer` | Invalid input, rejected before any change. Width and height must be 5 to 80, and mines 1 to `width x height - 9`. |
| 32 | `stale_revision` | Not the current game or revision. |
| 33 | `game_over` | The game is already won or lost. |
| 34 | `game_not_started` | No reveal yet, so there are no odds or proven moves. |
| 48 | `inconsistent_observation` | The clues and the mine total admit no layout. A real game never produces this. |
| 64 | `resource_exhausted` | An allocation, byte budget or counter limit was reached; nothing changed. |
| 65 | `internal_error` | A broken invariant, or a call before `ms_init`. |

Conflicts (32 to 34) change nothing: the page shows the latest state and
tells you that your move was not applied, instead of replaying it on a
position you have not seen. Solver budget exhaustion is not an error. It
produces an `approximate` result (reason `counting_budget_exceeded`) or an
`unavailable` one (reason `sampling_budget_exhausted`,
`no_consistent_samples`, `no_globally_compatible_samples`,
`insufficient_effective_samples`, `time_budget_exhausted` or
`memory_budget_exhausted`), still carrying every proof found.

## Building and testing

Every command runs through pixi with the tools locked in `pixi.lock`;
`--locked` makes pixi stop if the lock file does not match `pixi.toml`.
The `js` environment has the WebAssembly toolchain, Wasmtime, Node.js and
the browser test tooling; `wasm` has the WebAssembly toolchain and Wasmtime;
`macos`, `linux` and `windows` build the native C test programs.

| Purpose | Command |
| --- | --- |
| Build `dist/` and serve it | `pixi run --locked -e js start` |
| Build and verify `dist/` | `pixi run --locked -e js build-dist` |
| Serve an existing `dist/` | `pixi run --locked -e js serve` |
| Build and audit only `minesweeper.wasm` | `pixi run --locked -e wasm build-reactor` |
| C tests, native macOS (Apple silicon) | `pixi run --locked -e macos test-native` |
| C tests, native Linux (x86-64) | `pixi run --locked -e linux test-native` |
| C tests, native Windows (x64, MSVC) | `pixi run --locked -e windows test-native`, in a shell where `vcvars64.bat` has run |
| C tests as WebAssembly under Wasmtime | `pixi run --locked -e wasm test-wasm` |
| C tests as WebAssembly under Node.js | `pixi run --locked -e js test-node` |
| WebAssembly import/export audits and module checks | `pixi run --locked -e js check-wasm` |
| Install the JavaScript test tooling (once) | `pixi run --locked -e js install-js` |
| JavaScript adapter tests (builds `minesweeper.wasm` first) | `pixi run --locked -e js test-js` |
| Browser tests (builds `dist/` first) | `pixi run --locked -e js test-browser` |

The browser tests need Playwright's browsers, installed once (after
`install-js`) with
`pixi run --locked -e js npx --no playwright install chromium firefox webkit`
(on Linux, add `--with-deps` to install their system libraries). They run
Chromium (desktop and a Pixel 7 phone profile), with Firefox and WebKit smoke
tests, against `dist/` served both at the site root and under a nested path.

The C tests are grouped into the suites `runtime`, `bigint`, `game`,
`probability`, `autosolve` and `api`. Without arguments every suite runs; a
subset compiles only the sources it needs, for example
`pixi run --locked -e js test-node --suite probability`. A full run needs
every suite and never skips one. `check-wasm` audits the imports and exports
of the WebAssembly modules, proves that its gates reject undefined symbols,
compiler runtime helpers, system headers and C library calls, and
instantiates the module with corec's `wasi.js`.

Build output goes to `build/` (test programs and `build/wasm/minesweeper.wasm`)
and `dist/`. These, `.pixi/` and `node_modules/` are generated and ignored by
Git. Commit only sources, `pixi.lock` and `package-lock.json`.

### Deploying

`pixi run --locked -e js build-dist` writes the complete site to `dist/`:

- `index.html`, `styles.css`, `app.js`, `engine-client.js`, `wasm-host.js` and
  `probability-worker.js`, copied from `static/`;
- `minesweeper.wasm`, the engine;
- `vendor/corec/wasi.js`, corec's WebAssembly system interface host, and
  `vendor/corec/LICENSE`, corec's MIT license.

The build checks that `dist/` holds exactly these files, so a stray file
such as a test or a source file can never ship; a new site asset must first
be registered in `STATIC_FILES` in `scripts/build.mjs`. It also checks that
every reference in the site is a relative URL to a file in `dist/`. Upload the
folder to any static web host. Nothing else is needed
at runtime: there is no server code, nothing to install and no request to
another site. Because every URL is relative, the site also works under a
nested path, such as `https://example.org/projects/minesweeper/`. To try
that locally, run
`pixi run --locked -e js serve dist --base /projects/minesweeper/` and open
<http://127.0.0.1:8000/projects/minesweeper/>. Serving `.wasm` files as
`application/wasm` lets browsers compile the engine while downloading it;
other content types work too.

### Continuous integration

`.github/workflows/ci.yml` uses the same locked environments (pixi 0.76.1):

- **C tests** on Linux (`ubuntu-24.04`), macOS (`macos-15`, Apple silicon)
  and Windows (`windows-2025`, MSVC): the native C tests, the same tests as
  WebAssembly under Wasmtime and Node.js, `check-wasm` and `build-dist`.
- **Browser checks** on Linux: the JavaScript tests, then the browser tests
  against the built `dist/`, which is uploaded as the
  `minesweeper-static-site` artifact.
- **GitHub Pages** after all checks pass on `main`: the exact tested `dist/`
  is uploaded and deployed to <https://certik.github.io/ms3/>.

Every push to `main`, including a merged pull request, runs this pipeline and
updates the website on success. Pull requests run the checks but do not
publish. You can also start the pipeline from **Actions > CI > Run workflow**
with branch `main`. Failed checks leave the last successfully deployed site
unchanged, and a running main deployment is not cancelled by a newer push.

### GitHub Pages settings

The one-time publishing setting is **Settings > Pages > Build and deployment
> Source > GitHub Actions**. Do not select "Deploy from a branch" or create a
`gh-pages` branch: the workflow builds and uploads the generated files.
You can skip GitHub's suggested workflow templates because this repository
already has its publishing workflow.

Under **Settings > Actions > General**, Actions must be enabled and the
policy must allow GitHub's `actions/*` actions and
`prefix-dev/setup-pixi@v0.10.2`. The default read-only workflow permissions
can stay in place: only the deployment job requests `pages: write` and
`id-token: write`. No personal access token, deployment secret, or permission
to create pull requests is needed.

The deployment uses the **github-pages** environment. Restrict its deployment
branch rule to `main`. For unattended publishing, do not add required
reviewers or a wait timer to that environment. If an existing protection rule
requires approval, the deployment will wait for it.

## Project layout

| Path | Contents |
| --- | --- |
| `c/runtime.h`, `c/runtime.c` | Shared C runtime: fallible, budgeted allocation over corec's buddy allocator, the seeded random generator, hashing, the injected clock, grid neighbors and buffer helpers. |
| `c/bigint.h`, `c/bigint.c` | Exact multiprecision integers: counting, polynomial division and correctly rounded ratios. |
| `c/game.h`, `c/game.c` | Game rules: first-reveal-safe mine placement, flood fill, chording, flags, win/loss, revisions, the timer, the public view and observations. |
| `c/probability.c` | The mine-probability solver described above (`ms_solve`). |
| `c/engine.h`, `c/engine.c` | The shared contract (status codes, limits, buffer layouts) and the per-tab engine: current game, validated odds cache, atomic autosolve. |
| `c/wasm_api.h`, `c/wasm_api.c`, `c/wasm_buffers.h` | The WebAssembly exports and the checks on page-supplied buffers. |
| `c/compiler_mem.c` | Windows-only `memset`/`memcpy` forwarding to corec (see above). |
| `static/` | The site: `index.html`, `styles.css`, `app.js` (the interface), `engine-client.js` (game instance and solver worker), `wasm-host.js` (marshalling; loads corec's `wasi.js`), `probability-worker.js` (solver instance). Plain JavaScript modules and inline SVG icons; no bundler. |
| `scripts/` | `build.mjs` builds and audits the C test programs, the WebAssembly module and `dist/`; `check-wasm.mjs` runs the WebAssembly checks; `serve.mjs` is the static file server. |
| `tests/c/` | The C test runner (`main.c`), one file per suite, reference fixtures in `fixtures/` (frozen from the former Python implementation), and a test-only WebAssembly module. |
| `tests/js/` | Node.js tests of the JavaScript adapter and the real engine, and the Playwright browser tests. |
| `tests/coverage-map.json` | The historical migration trace: every test of the former Python implementation (now removed), mapped to the C or JavaScript tests that replaced it. |
| `third_party/corec/` | corec, a Git submodule pinned to one commit (MIT License). |
| `pixi.toml`, `pixi.lock` | Toolchains, tasks and their locked versions. |
| `package.json`, `package-lock.json` | Development-only JavaScript test tooling (Playwright). The site itself has no npm dependencies. |
| `.github/workflows/ci.yml` | Continuous integration. |

## Third-party code

[corec](https://github.com/certik/corec) is used under the MIT License,
Copyright (c) 2026 Ondřej Čertík. The license is in
`third_party/corec/LICENSE`. The WebAssembly module contains corec's C code
and the site ships corec's `platform/js/wasi.js`, so `dist/` includes the
license as `vendor/corec/LICENSE`.
