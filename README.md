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

**Model.** Every hidden cell is a Boolean variable: mine or no mine. Each
revealed number gives one equality: the number of mines among its hidden
neighbors equals the clue. The total mine count gives one global equality.
Mines are placed uniformly at random, apart from the first-reveal rule. The
first revealed cell is always a 0, and that clue already says its neighbors
are safe, so the rule adds no information beyond the revealed numbers. Every
layout that satisfies the equalities is therefore equally likely. A cell's
probability is:

```
P(mine at cell) = (layouts with a mine at the cell) / (all consistent layouts)
```

This is **not** the same as reading clues locally. A `1` with three hidden
neighbors does not make each of them 33% likely, because overlapping clues and
the global mine count both change the weights.

**Algorithm.** The solver works in five steps:

1. **Propagate forced assignments.** Apply sound deductions until nothing
   changes. A clue whose mines are all accounted for makes its other neighbors
   safe. A clue that needs all its hidden neighbors makes them all mines.
   Overlapping clues are compared pairwise, and the global mine count gives
   the extremes. The resulting cells are proven.
2. **Split the frontier into components.** Frontier cells are hidden cells
   next to at least one clue. Two frontier cells are connected when they share
   a clue. Each connected component of this cell-clue incidence graph can be
   solved on its own. Hidden cells next to no clue are *unconstrained*; they
   are interchangeable and are handled as one pool.
3. **Count each component exactly when tractable.** A memoized depth-first
   search counts the satisfying assignments of a component, grouped by how
   many mines they use. Cells that touch exactly the same clues are grouped
   and counted together with binomial multiplicities, which keeps the search
   small. The result is a histogram `H_c[k]`: the number of solutions of
   component `c` with `k` mines. After the global conditioning in step 4, a
   weighted backward pass over the same search gives each cell's exact
   numerator.
4. **Combine globally.** Components interact only through the mine total. With
   `R` mines left to place and `U` unconstrained cells, one choice of mine
   counts `k_1, k_2, ...` for the components has weight
   `H_1[k_1] x H_2[k_2] x ... x C(U, R - sum k)`. The binomial `C(U, R - sum k)`
   counts the ways to put the remaining mines into the unconstrained pool. The
   solver convolves the histograms (a dynamic program over components) in
   exact integer arithmetic. This conditions every probability on the global
   mine count. The unconstrained cells share the expected number of leftover
   mines: `E[R - sum k] / U` each.
5. **Estimate only the hard parts.** A component can be too large and tangled
   to count within the node or time budget. In that case the solver uses
   bounded, constraint-aware sequential importance sampling:
   - It assigns the component's cells one at a time. After each tentative
     choice it checks feasibility with unit propagation. It flips a fair coin
     only when both values are still possible.
   - Each completed layout is weighted by the inverse of its proposal
     probability. A dead end gets weight zero.
   - The weighted samples replace that component's exact histogram and are
     combined through the same global conditioning, including the binomial
     term.

   The result is labeled `approximate`, and only cells proven by logic or by
   exactly counted components are marked certain. There is an
   effective-sample-size guard: after global re-weighting, every sampled
   component must reach an effective sample size of at least 50
   (`MIN_EFFECTIVE_SAMPLE_SIZE` in `minesweeper/probability.py`). If it does
   not, the answer is `unavailable` rather than a guess. The same happens if no
   sampled layout fits the mine total, or if the budget runs out.

The server handles every request on its own thread, so a running solve never
blocks moves. Each solve has a time budget, a counting-node budget and a
sample budget. These are the `SOLVER_*` constants in `server.py`. Exact and
approximate results are cached per game revision, so asking again for the same
position is instant. Unavailable results are not cached: **Retry odds**
recomputes them, which can succeed if the budget ran short only temporarily.
The server rejects requests for a revision that is no longer current.

**Why the odds cannot always be exact and fast.** Deciding whether a
Minesweeper position is consistent at all is NP-complete (Kaye, 2000).
Counting its consistent layouts, which is what exact probabilities require, is
#P-hard. No known algorithm gives exact answers quickly on every large board;
the worst case grows exponentially. The cost depends on the frontier, not on
the board area: on the size and tangledness of the largest connected
component. An 80 x 80 board whose frontier is short and simple is cheap to
count exactly. A mid-game Expert board with one long, interlocking frontier
can exceed the budget. That is why the solver counts exactly where it can,
estimates only where it must, and says which one it did.

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
