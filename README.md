# Minesweeper with mine odds

A browser Minesweeper game with an optional **mine-odds overlay**. Turn it on
and every hidden cell shows its chance of holding a mine. The odds are exact
when the position can be counted within the time budget. Otherwise they are
marked as estimates, or not shown at all. The server is a single Python
program that uses only the standard library. The front end is plain
HTML/CSS/JavaScript with no build step and no external assets.

## Quick start

Requirements: **Python 3.9 or newer**. There are no runtime dependencies,
nothing to `pip install` and no npm or build step.

```sh
python3 server.py
```

Then open <http://127.0.0.1:8000/> in a browser.

Options:

| Option | Default | Meaning |
| --- | --- | --- |
| `--host HOST` | `127.0.0.1` | Address to listen on. The default is reachable from this computer only. |
| `--port PORT` | `8000` | Port to listen on. `0` picks a free port; the startup log prints the URL. |

For example, `python3 server.py --port 8080` serves the game at
<http://127.0.0.1:8080/>. `--host 0.0.0.0` makes the game reachable from other
devices on your network. The server has **no authentication**, so only do this
on a network you trust. Stop the server with `Ctrl+C`.

### Games live in memory

Games are stored in the server process only:

- Restarting `server.py` discards every game.
- At most 256 games are kept. Creating one more removes the least recently
  used game.
- A game that has not been used for 24 hours expires.

The browser remembers the id of your current game (in `localStorage`) and
restores it when you reload the page. If the server no longer knows that game,
the page starts a new one and tells you why. If the server stops mid-game, the
page shows that it cannot reach the server, keeps your board on screen, and
reconnects automatically once the server is back. After a restart the old
game is gone, so the page offers a new one.

A lost answer does not prove that a move failed: the server may have applied
it before the connection dropped. When a move ends in a network error, a
timeout, an unreadable reply or a server error, the page therefore says the
move may or may not have been applied. It then reloads the game from the
server before it accepts another move or shows new odds.

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
  the board instead of sending such moves.

The board is an ARIA grid with a single tab stop. Each cell has a label with
its row and column, its content (number, flag, hidden) and, when the overlay
is on, its odds and whether they are exact. Game events are announced through
a polite live region; the timer is not announced.

## The mine-odds overlay

Switch on **Mine odds** above the board. The page then asks the server for
the odds of the exact position on screen, identified by game id and revision.
It works like this:

- **Moves never wait for the solver.** Starting a move, starting a new game or
  turning the overlay off hides the old odds immediately and cancels the
  pending request. Answers that arrive for an older position are discarded,
  and the page never draws odds for a position other than the one shown.
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
  are shown (except proven cells), the server's explanation stays visible, and
  **Retry odds** asks the server to calculate again. Unavailable results are
  not cached, so a retry can succeed after a temporary budget shortfall.
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
certain move. Make the first reveal yourself. The server then flags proven
mines, removes any flags on proven-safe cells, and opens those safe cells,
including their normal zero-cell flood fill. It recalculates and repeats until
no more certain moves remain, or the game ends.

The remaining uncertain cells keep their percentages. Choose one yourself;
if you survive, autosolve resumes automatically. It **never guesses** and
never treats a sampled 0% or 100% estimate as a proof. Even when full odds are
unavailable, logically proven moves can still be played.

Switch Autosolve off to pause; an already-sent safe-move batch may finish, but
no next batch is started. Turning Mine odds off also turns Autosolve off.
Autosolve is remembered across reloads and new games, which still wait for
your first reveal. A failed automatic move pauses the mode rather than retrying
blindly; the usual authoritative reload handles an uncertain request outcome.

Each automatic pass is one server-verified batch and increments the revision
once if anything changes. This avoids sending one full-board response for
every automatic flag or reveal on a large grid. Proofs computed for an old
revision are rejected, and the browser never applies old odds to a new board.

## How the odds are computed

The solver (`minesweeper/probability.py`) sees only public information: the
board size, the total number of mines, and the numbers on revealed cells. It
never receives the hidden layout, and it never receives flags.

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

The solver propagates these deductions and removes the fixed variables from
the remaining problem. Pairwise overlap reasoning has a time budget; stopping
it early only leaves more work for the counting phase. Flags do not reduce
the remaining mine count unless the solver independently proves those mines.

### 2. Count connected regions separately

The **frontier** consists of undetermined hidden cells involved in revealed
clues. Cells connected through shared clues form a component. Each component
can be counted separately, although its probabilities remain coupled to other
components by the total mine count. Hidden cells involved in no remaining
clue form one interchangeable, unconstrained pool.

For each component `c`, the solver computes a histogram:

```text
H_c[k] = number of valid assignments containing exactly k mines
```

`_Component.count` uses a layered, memoized dynamic program, not whole-board
enumeration. Cells participating in exactly the same clues are grouped.
Choosing `m` mines among `g` interchangeable cells contributes `C(g, m)`
assignments, where `C` is the binomial coefficient.

The groups are ordered to keep as few clues open simultaneously as practical.
At each layer, a state records the remaining mine requirements of the partly
processed clues and holds a histogram of how many mines have been placed.
Assignments leading to the same remaining requirements are merged by adding
their histogram counts. Choices that leave too many mines, too few available
cells to satisfy a clue, or exceed the remaining mine budget are pruned.
This reuses common subproblems instead of visiting every valid assignment
individually.

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

`_Solver._combine` convolves the component histograms rather than explicitly
trying every combination of component mine counts. If `Q[t]` counts frontier
assignments with `t` mines, the total number of consistent complete layouts is:

```text
Q = H_1 * H_2 * ...                 (* denotes polynomial convolution)
Z = sum over t of Q[t] * C(U, R - t)
```

For each component, exact polynomial division of `Q` by its histogram recovers
the counts for all the other components. These supply the outside weight for
each possible mine count in that component. A weighted backward pass through
the counting states then obtains each cell's numerator: the number of complete
layouts containing a mine in that cell. Within a group of `g` interchangeable
cells with `m` mines, requiring a particular cell to be mined contributes
`C(g - 1, m - 1)` rather than `C(g, m)`. Dividing the resulting numerator by
`Z` gives that cell's probability.

Unconstrained cells all receive the same probability: the weighted expected
number of leftover mines divided by `U`, or `E[R - t] / U`. Their odds are
therefore not simply the total mine count divided by all hidden cells.
Counts and weights remain arbitrary-precision Python integers until the final
division; a huge binomial coefficient does not overflow floating-point
arithmetic.

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

`_Sampler.draw` assigns cells in a fixed order. It tentatively tries both
values and propagates forced assignments to detect contradictions. If only
one value survives, it takes that value. If both survive, it flips a fair coin.
A path that eventually reaches a dead end has weight zero. A draw interrupted
by the deadline is discarded entirely.

A completed assignment reached through `b` fair binary choices has proposal
probability `2**(-b)`, so it receives inverse-proposal weight `2**b`. This
correction matters: the sampler does not generate every valid assignment
equally often. Weighted samples replace that component's exact histogram and
cell counts, then undergo the same global conditioning, including the
unconstrained-pool binomial factor.

The result is labeled `approximate`. After global re-weighting, each sampled
component must have at least 50 effective samples
(`MIN_EFFECTIVE_SAMPLE_SIZE` in `minesweeper/probability.py`), calculated as:

```text
effective sample size = (sum of sample weights)**2 / sum of squared weights
```

This is a quality guard, **not an error bound or a proof of certainty**.
Too few effective samples, no globally compatible sample combinations, or a
budget exhausted before the result is complete produce `unavailable` rather
than fabricated percentages. Independently proven cells can still be reported.

### Budgets, certainty and scaling

The defaults are the `SOLVER_*` constants in `server.py`:

| Budget | Default |
| --- | ---: |
| Time per calculation | 1.5 seconds |
| Exact forward-counting search nodes | 100,000 |
| Sampling proposals, shared across hard components | 2,000 |

Time is reserved for sampling and for combining results after counting. A
separate guard limits stored dynamic-programming coefficients and edges.

For an exact calculation, an integer numerator of zero proves a cell safe,
and a numerator equal to `Z` proves it mined. Display rounding is never used
to establish certainty. When some components are sampled, only independent
logical deductions or proofs from exactly counted components are marked
certain: a sampled 0% or 100% alone proves nothing. **Autosolve acts only on
these proofs, never on rounded percentages or sampled endpoints.**

The server copies the public clues under the game lock and releases that lock
before calculating odds. Exact and approximate results are cached per game
revision, avoiding recomputation for the same position.
Unavailable results are not cached: **Retry odds** recomputes them, which can
succeed if the budget ran short only temporarily. The server rejects requests
for a revision that is no longer current.

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

## Project layout

| Path | Contents |
| --- | --- |
| `server.py` | Standard-library HTTP server (`ThreadingHTTPServer`). Serves the static files and the JSON API, validates requests, runs the solver with budgets and a per-revision cache, and handles the `--host`/`--port` command line. |
| `minesweeper/game.py` | Game rules and the in-memory store: first-reveal-safe mine placement, flood fill, chording, win/loss, revisions, idle expiry and LRU eviction. The public state never contains the hidden layout before the game ends. |
| `minesweeper/probability.py` | The mine-probability solver described above. |
| `static/index.html`, `static/styles.css`, `static/app.js` | The browser UI: vanilla JavaScript, inline SVG icons, no external requests. |
| `tests/` | `unittest` suites for the game rules, the solver and the HTTP server. |

## API

All endpoints are same-origin JSON. Request bodies must be sent with
`Content-Type: application/json`. Rows and columns are zero-based, and cells
are listed in row-major order: index = `row x width + col`.

| Method and path | Body or query | Success |
| --- | --- | --- |
| `POST /api/games` | `{"width", "height", "mines"}` | `201` with a GameState |
| `GET /api/games/{id}` | | `200` with a GameState |
| `POST /api/games/{id}/actions` | `{"action": "reveal" or "flag" or "chord", "row", "col", "revision"}` | `200` with the new GameState |
| `POST /api/games/{id}/autosolve` | `{"revision": N}` | `200` with GameState after one batch of server-proven flags and safe reveals; no guesses |
| `GET /api/games/{id}/probabilities?revision=N` | | `200` with a probability object |

**GameState** is
`{id, width, height, mines, status, revision, flags, elapsed_seconds, cells}`:

- `status` is `ready` (no reveal yet), `playing`, `won` or `lost`.
- `cells` holds one entry per cell:
  `{revealed, flagged, adjacent, mine, exploded}`. `adjacent` is `null` until
  the cell is revealed. `mine` is `null` for every cell until the game ends,
  when the layout is disclosed.
- Every state-changing action increments `revision`. An action must send the
  revision it was based on.

**Probability object** is
`{game_id, revision, status, probabilities, proven_safe, proven_mines, message, meta}`:

- `status` is `exact`, `approximate`, `unavailable`, `not-started` or
  `finished`.
- `probabilities` has one entry per cell: a number from 0 to 1, or `null` for
  revealed cells and for cells without a trustworthy value.
- `proven_safe` and `proven_mines` list flat cell indices.
- `meta` includes `frontier_cells`, `components`, `unconstrained_cells`,
  `samples`, `elapsed_ms` and `reason`. It may also include the optional
  diagnostics `effective_sample_size`, `sample_attempts`, `exact_components`
  and `sampled_components`.

**Errors** use the shape `{"error": {"code", "message"}}`:

- `404`: unknown or expired game.
- `409`: stale revision, or the game is already over. The body also includes
  `state`, the latest GameState. The page shows that state and tells you that
  your move was not applied, instead of replaying the move on a position you
  have not seen.
- `400` and `415`: invalid input.
- `503`: the solver is busy. The page retries automatically.
- `500`: the odds calculation failed. The game itself is not affected.

## Tests

```sh
python3 -m unittest discover -s tests -v
```

The tests use only the standard library. Run them from the repository root.
