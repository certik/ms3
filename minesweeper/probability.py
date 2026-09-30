"""Mine probability engine for Minesweeper, using public information only.

Inputs are the board size, the total number of mines and the clue values of
revealed cells, which are known to be safe.  The engine never accepts the
secret mine layout or player flags.

Model
-----
Every unrevealed cell is a Boolean variable.  A revealed clue is the linear
equality "mined unrevealed neighbours == clue", and the mine total is one
global equality.  All layouts satisfying these equalities are equally likely,
so a cell's probability is ``#layouts with a mine there / #layouts``.

Algorithm
---------
1. Sound propagation: single-clue rules, bounded pairwise overlap reasoning
   between clues that share cells, and the global mine-count extremes.
2. The remaining clue/cell incidence graph is split into connected
   components.  Unrevealed cells next to no clue form one exchangeable pool.
3. Each component is counted exactly by an iterative, memoised depth-first
   search over a bandwidth-reducing ordering of cell groups (cells with the
   same clue set, counted with binomial multiplicities).  The memo key is the
   residual demand of partially assigned clues; branches are pruned by
   residual-capacity feasibility and the mine budget.  This yields ``H[k]``,
   the number of component solutions with ``k`` mines, and, after global
   conditioning, exact per-cell numerators from a weighted backward pass.
4. Components whose counting exceeds the node/time budget are estimated by
   sequential importance sampling: cells are visited in a fixed order, both
   values are tested with unit propagation, a fair coin is used only when
   both are feasible, a completed layout is weighted by ``2 ** choices``
   (its inverse proposal probability) and dead ends get weight zero.
5. Components interact only through the mine total.  Histograms are
   convolved with exact integers, the unconstrained pool contributes
   ``C(U, R - t)``, and each component's cavity weight comes from exact
   polynomial division.  Probabilities are ratios of Python integers, so no
   intermediate weight overflows or is rounded before the final ratio.

Statuses
--------
``exact``        every component was counted exactly.  A probability is
                 exactly 0.0 or 1.0 only for proven cells.
``approximate``  some component was sampled; values are consistent
                 estimates and may be 0.0/1.0 without proof.  Proven lists
                 contain logical deductions only.
``unavailable``  no trustworthy estimate within budget; only proven cells
                 carry a probability, all others are ``None``.
"""

from __future__ import annotations

import hashlib
import math
import numbers
import operator
import random
import time
from collections import deque
from typing import Any, Dict, List, Mapping, Optional, Tuple

__all__ = [
    "InconsistentBoardError",
    "MIN_EFFECTIVE_SAMPLE_SIZE",
    "calculate_probabilities",
]

#: Every sampled component must reach this effective sample size (computed
#: from its globally re-weighted importance weights) for an approximate result.
MIN_EFFECTIVE_SAMPLE_SIZE = 50.0

EXACT = "exact"
APPROXIMATE = "approximate"
UNAVAILABLE = "unavailable"

# Fractions of ``time_budget`` at which phases must stop.
_PAIR_FRACTION = 0.10
_COUNT_FRACTION = 0.35
_SAMPLE_FRACTION = 0.80
_RESERVE_FRACTION = 0.10
# Measured cost of the exact marginal pass is 0.7-1.7x the forward pass.
_BACKWARD_TIME_FACTOR = 2.0
# Memory guard: stored DP polynomial coefficients plus edges, all components.
_MAX_STORED_ENTRIES = 1_500_000  # roughly 60 MB peak
# Work units (big-integer operations) between clock reads.
_CLOCK_WORK = 2_048
# Cell assignments/undos between clock reads inside one sampling draw.
_DRAW_CLOCK_OPS = 4_096
_SAMPLE_BATCH = 8

_TINY = math.nextafter(0.0, 1.0)
_BELOW_ONE = math.nextafter(1.0, 0.0)

_REASON_TEXT = {
    "counting_budget_exceeded": "exact counting exceeded the budget",
    "sampling_budget_exhausted": "no sampling budget was left for the hard component(s)",
    "no_consistent_samples": "sampling found no consistent layout for a component",
    "no_globally_compatible_samples": (
        "no sampled layouts could be combined to match the mine total"
    ),
    "insufficient_effective_samples": "too few effective samples for a reliable estimate",
    "time_budget_exhausted": "the time budget ran out before the result was complete",
}


class InconsistentBoardError(ValueError):
    """The public observations (clues and mine total) admit no mine layout."""


class _Contradiction(Exception):
    pass


class _OutOfBudget(Exception):
    pass


# --------------------------------------------------------------------- public


def calculate_probabilities(
    width: int,
    height: int,
    total_mines: int,
    revealed: Mapping[int, int],
    *,
    time_budget: float = 1.5,
    node_budget: int = 100_000,
    sample_budget: int = 2_000,
    rng: Optional[random.Random] = None,
) -> dict:
    """Return mine probabilities for every cell of a ``width`` x ``height`` board.

    ``revealed`` maps row-major cell indices (``row * width + col``) of revealed
    safe cells to their clue values (0..8).  All other cells are unknown.

    Returns a JSON-compatible dict with keys ``status`` (``"exact"``,
    ``"approximate"`` or ``"unavailable"``), ``probabilities`` (one float or
    ``None`` per cell; ``None`` for revealed cells and, when unavailable, for
    unproven cells), ``proven_safe`` / ``proven_mines`` (sorted indices proven
    by logic or exact counting, never by sampling), ``message`` and ``meta``.

    Budgets: ``node_budget`` bounds the memoised search-node expansions of
    the exact forward counting passes, ``sample_budget`` bounds
    importance-sampling proposals and ``time_budget`` (seconds) bounds
    wall-clock time of the whole call, including the combination and
    marginal passes.  When ``rng`` is omitted a generator seeded from the
    observation is used, so the proposal sequence is reproducible; results
    can still differ between runs when a deadline changes how much counting
    or sampling completes.

    Raises ``ValueError`` for malformed arguments and
    ``InconsistentBoardError`` (a ``ValueError``) when the clues and mine
    total are contradictory.
    """
    started = time.perf_counter()
    width = _as_int("width", width)
    height = _as_int("height", height)
    if width < 1 or height < 1:
        raise ValueError("width and height must be positive")
    n_cells = width * height
    total_mines = _as_int("total_mines", total_mines)
    if not 0 <= total_mines <= n_cells:
        raise ValueError(f"total_mines must be between 0 and {n_cells}")
    if not isinstance(revealed, Mapping):
        raise ValueError("revealed must be a mapping from cell index to clue value")
    if isinstance(time_budget, bool) or not isinstance(time_budget, numbers.Real):
        raise ValueError("time_budget must be a non-negative number of seconds")
    budget = float(time_budget)
    if math.isnan(budget) or budget < 0:
        raise ValueError("time_budget must be a non-negative number of seconds")
    node_budget = _as_int("node_budget", node_budget)
    sample_budget = _as_int("sample_budget", sample_budget)
    if node_budget < 0 or sample_budget < 0:
        raise ValueError("node_budget and sample_budget must be non-negative")
    if rng is not None and not callable(getattr(rng, "getrandbits", None)):
        raise ValueError("rng must provide getrandbits(), like random.Random")

    clues: Dict[int, int] = {}
    for key, value in revealed.items():
        index = _as_int("revealed cell index", key)
        if not 0 <= index < n_cells:
            raise ValueError(f"revealed cell index {index} is outside the board")
        clue = _as_int(f"clue at cell {index}", value)
        if not 0 <= clue <= 8:
            raise ValueError(f"clue at cell {index} must be between 0 and 8, got {clue}")
        clues[index] = clue
    if total_mines > n_cells - len(clues):
        raise InconsistentBoardError(
            f"{total_mines} mines cannot fit in {n_cells - len(clues)} unrevealed cells"
        )
    solver = _Solver(
        width, height, total_mines, clues, _Clock(started, budget),
        node_budget, sample_budget, rng,
    )
    return solver.run()


# -------------------------------------------------------------------- helpers


def _as_int(name: str, value: Any) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{name} must be an integer, not a bool")
    try:
        return operator.index(value)
    except TypeError:
        raise ValueError(f"{name} must be an integer, got {type(value).__name__}") from None


class _Clock:
    def __init__(self, started: float, budget: float) -> None:
        self.started = started
        self.budget = budget
        self.infinite = math.isinf(budget)
        self.deadline = math.inf if self.infinite else started + budget

    def at(self, fraction: float) -> float:
        return math.inf if self.infinite else self.started + self.budget * fraction


class _Meter:
    """Budget guard: search nodes, stored entries and a wall-clock deadline."""

    __slots__ = ("nodes", "node_limit", "stored", "store_limit", "deadline", "_work")

    def __init__(self, deadline: float, node_limit: float = math.inf,
                 store_limit: float = math.inf) -> None:
        self.nodes = 0
        self.node_limit = node_limit
        self.stored = 0
        self.store_limit = store_limit
        self.deadline = deadline
        self._work = 0

    def node(self, work: int) -> None:
        """Account one search-node expansion costing ``work`` coefficient operations."""
        self.nodes += 1
        if self.nodes > self.node_limit:
            raise _OutOfBudget("nodes")
        self.work(work)

    def work(self, units: int) -> None:
        self._work += units
        if self._work >= _CLOCK_WORK:
            self._work = 0
            if time.perf_counter() > self.deadline:
                raise _OutOfBudget("time")

    def check_store(self, extra: int) -> None:
        if self.stored + extra > self.store_limit:
            raise _OutOfBudget("memory")


def _default_rng(width: int, height: int, total: int, clues: Mapping[int, int]) -> random.Random:
    digest = hashlib.sha256(f"{width}x{height}/{total}".encode("ascii"))
    for index in sorted(clues):
        digest.update(f";{index}={clues[index]}".encode("ascii"))
    return random.Random(int.from_bytes(digest.digest()[:16], "big"))


def _normalize(lo: int, arr: List[int]) -> Tuple[int, List[int]]:
    start, end = 0, len(arr)
    while start < end and not arr[start]:
        start += 1
    while end > start and not arr[end - 1]:
        end -= 1
    return lo + start, arr[start:end]


def _add_scaled(table: dict, key: tuple, lo: int, arr: List[int], mult: int) -> None:
    """``table[key] += mult * x**lo * arr`` for offset polynomials ``[lo, coeffs]``."""
    current = table.get(key)
    if current is None:
        table[key] = [lo, [v * mult for v in arr] if mult != 1 else list(arr)]
        return
    clo, carr = current
    if lo < clo:
        carr[0:0] = [0] * (clo - lo)
        current[0] = clo = lo
    offset = lo - clo
    end = offset + len(arr)
    if end > len(carr):
        carr.extend([0] * (end - len(carr)))
    if mult == 1:
        for j, v in enumerate(arr, offset):
            carr[j] += v
    else:
        for j, v in enumerate(arr, offset):
            carr[j] += v * mult


def _poly_mul(alo: int, a: List[int], blo: int, b: List[int], tmax: int, meter: _Meter):
    """Product of two offset polynomials, keeping only exponents <= ``tmax``."""
    lo = alo + blo
    if not a or not b or lo > tmax:
        return lo, []
    length = min(len(a) + len(b) - 1, tmax - lo + 1)
    out = [0] * length
    lb = len(b)
    for i, av in enumerate(a):
        if i >= length:
            break
        if not av:
            continue
        m = min(lb, length - i)
        meter.work(m)
        for j in range(m):
            bv = b[j]
            if bv:
                out[i + j] += av * bv
    return lo, out


def _binomial_row(pool: int, remaining: int, tlo: int, thi: int) -> List[int]:
    """``T[s - tlo] = C(pool, remaining - s)`` for ``tlo <= s <= thi <= remaining``."""
    if thi < tlo:
        return []
    row = [0] * (thi - tlo + 1)
    j = remaining - thi
    value = math.comb(pool, j)
    for s in range(thi, tlo - 1, -1):
        j = remaining - s
        row[s - tlo] = value
        value = value * (pool - j) // (j + 1)
    return row


def _cavity(qlo, q, hlo, h, tlo, T, meter: _Meter) -> List[int]:
    """Weights ``W[k] = sum_t (Q / H)[t] * T[k + t]`` for ``k`` in ``H``'s range.

    ``Q`` is the product of all component histograms (truncated above the
    remaining mine count), so ``Q / H`` is the product of all *other*
    histograms; the low-order exact division recovers it without storing
    prefix/suffix products.
    """
    h0 = h[0]
    lh = len(h)
    d: List[int] = []
    for t in range(len(q)):
        acc = q[t]
        top = t if t < lh - 1 else lh - 1
        meter.work(top + 1)
        for i in range(1, top + 1):
            dv = d[t - i]
            if dv:
                hv = h[i]
                if hv:
                    acc -= hv * dv
        quotient, remainder = divmod(acc, h0)
        if remainder or quotient < 0:
            raise ArithmeticError("inexact histogram division")
        d.append(quotient)
    dlo = qlo - hlo
    weights = [0] * lh
    n_t = len(T)
    for kk in range(lh):
        if not h[kk]:
            continue
        base = hlo + kk + dlo - tlo
        rlo = -base if base < 0 else 0
        rhi = min(len(d), n_t - base)
        if rhi <= rlo:
            continue
        meter.work(rhi - rlo)
        acc = 0
        for r in range(rlo, rhi):
            dv = d[r]
            if dv:
                acc += dv * T[base + r]
        weights[kk] = acc
    return weights


def _ratio(numerator: int, denominator: int, exact: bool) -> float:
    value = numerator / denominator
    if exact:
        # Keep 0.0 and 1.0 reserved for cells that are actually certain.
        if value == 0.0 and numerator:
            value = _TINY
        elif value == 1.0 and numerator != denominator:
            value = _BELOW_ONE
    return value


# ----------------------------------------------------------------- component


class _Component:
    """A connected set of constrained unknown cells and their clues."""

    def __init__(self, cells: List[int], constraints: List[Tuple[int, List[int]]]) -> None:
        self.cells = cells
        index = {cell: i for i, cell in enumerate(cells)}
        self.cons_need = [need for need, _ in constraints]
        self.cons_vars = [[index[v] for v in members] for _, members in constraints]
        self.var_cons: List[List[int]] = [[] for _ in cells]
        for ci, members in enumerate(self.cons_vars):
            for v in members:
                self.var_cons[v].append(ci)
        self.mode: Optional[str] = None
        self.hist: Optional[Tuple[int, List[int]]] = None
        self.layers: Optional[List[dict]] = None
        self.edges: Optional[List[list]] = None
        self.sampler: Optional[_Sampler] = None
        self.ess: Optional[float] = None
        self._group_and_order()

    def _group_and_order(self) -> None:
        signature: Dict[tuple, int] = {}
        groups: List[List[int]] = []
        gcons: List[tuple] = []
        for v, cons in enumerate(self.var_cons):
            key = tuple(cons)
            g = signature.get(key)
            if g is None:
                g = signature[key] = len(groups)
                groups.append([])
                gcons.append(key)
            groups[g].append(v)
        count = len(groups)
        cons_groups: List[List[int]] = [[] for _ in self.cons_need]
        for g, key in enumerate(gcons):
            for c in key:
                cons_groups[c].append(g)
        adjacency = [set() for _ in range(count)]
        for members in cons_groups:
            for g in members:
                adjacency[g].update(members)
        degree = [len(a) - 1 for a in adjacency]
        neighbours = [
            sorted((h for h in adjacency[g] if h != g), key=lambda h: (degree[h], h))
            for g in range(count)
        ]

        def bfs(source: int):
            dist = [-1] * count
            dist[source] = 0
            order = [source]
            for g in order:
                step = dist[g] + 1
                for h in neighbours[g]:
                    if dist[h] < 0:
                        dist[h] = step
                        order.append(h)
            return order, dist

        # Cuthill-McKee order from a pseudo-peripheral group keeps the set of
        # partially assigned clues (the DP state) small along chains/loops.
        order, dist = bfs(min(range(count), key=lambda g: (degree[g], g)))
        for _ in range(3):
            ecc = dist[order[-1]]
            far = min((g for g in order if dist[g] == ecc), key=lambda g: (degree[g], g))
            new_order, new_dist = bfs(far)
            if new_dist[new_order[-1]] < ecc:
                break
            order, dist = new_order, new_dist
            if new_dist[new_order[-1]] == ecc:
                break
        self.groups = groups
        self.gcons = gcons
        self.order = order
        # Largest number of simultaneously open clues along the order: the
        # dimension of the DP state, used to count easy components first.
        position = {g: i for i, g in enumerate(order)}
        delta = [0] * (count + 1)
        for members in cons_groups:
            spots = [position[g] for g in members]
            delta[min(spots)] += 1
            delta[max(spots) + 1] -= 1
        open_now = 0
        self.width = 0
        for i in range(count):
            open_now += delta[i]
            self.width = max(self.width, open_now)

    # -- exact counting ---------------------------------------------------

    def count(self, kcap: int, meter: _Meter) -> bool:
        """Forward pass of the memoised search.  Returns False if there is no solution.

        Layer ``i`` maps the residual demands of the open clues after the
        first ``i`` groups (the memo key) to a polynomial over the number of
        mines placed so far.  Each (state, group count) expansion is one
        search node; every stored layer and edge is kept for the backward
        pass and counted against the memory guard.
        """
        sizes = [len(g) for g in self.groups]
        need = self.cons_need
        ncons = len(need)
        last = [-1] * ncons
        remaining = [0] * ncons
        for i, g in enumerate(self.order):
            for c in self.gcons[g]:
                last[c] = i
                remaining[c] += sizes[g]
        active: List[int] = []
        layer: Dict[tuple, list] = {(): [0, [1]]}
        layers = [layer]
        edges: List[list] = []
        for i, g in enumerate(self.order):
            size = sizes[g]
            cg = self.gcons[g]
            for c in cg:
                remaining[c] -= size
            position = {c: j for j, c in enumerate(active)}
            ended = {c for c in cg if last[c] == i}
            cgset = set(cg)
            new_active = sorted((set(active) | cgset) - ended)
            bounds = [(position.get(c, -1), need[c], remaining[c]) for c in cg if c not in ended]
            exact = [(position.get(c, -1), need[c]) for c in cg if c in ended]
            build = [(position.get(c, -1), need[c], c in cgset) for c in new_active]
            binom = [math.comb(size, x) for x in range(size + 1)]
            nxt: Dict[tuple, list] = {}
            layer_edges = []
            pending = 0
            for state, (lo, arr) in layer.items():
                xlo, xhi = 0, size
                for src, init, cap in bounds:
                    r0 = state[src] if src >= 0 else init
                    if r0 < xhi:
                        xhi = r0
                    if r0 - cap > xlo:
                        xlo = r0 - cap
                for src, init in exact:
                    r0 = state[src] if src >= 0 else init
                    if r0 > xlo:
                        xlo = r0
                    if r0 < xhi:
                        xhi = r0
                if kcap - lo < xhi:
                    xhi = kcap - lo
                for x in range(xlo, xhi + 1):
                    new_state = tuple([
                        (state[src] if src >= 0 else init) - (x if dec else 0)
                        for src, init, dec in build
                    ])
                    limit = kcap - lo - x + 1
                    part = arr if limit >= len(arr) else arr[:limit]
                    meter.node(len(part) + 1)
                    pending += len(part) + 1
                    meter.check_store(pending)
                    _add_scaled(nxt, new_state, lo + x, part, binom[x])
                    layer_edges.append((state, x, new_state))
            meter.stored += len(layer_edges) + sum(len(entry[1]) for entry in nxt.values())
            layers.append(nxt)
            edges.append(layer_edges)
            layer = nxt
            active = new_active
            if not layer:
                return False
        final = layer.get(())
        if final is None:
            return False
        lo, arr = _normalize(final[0], final[1])
        if not arr:
            return False
        self.hist = (lo, arr)
        self.layers = layers
        self.edges = edges
        return True

    def exact_numerators(self, wlo: int, weights: List[int], meter: _Meter):
        """Weighted backward pass: per-cell numerators and the total weight.

        ``weights[k - wlo]`` is the global weight of everything outside this
        component given that it holds ``k`` mines.
        """
        layers, edges, order = self.layers, self.edges, self.order
        n = len(order)
        flo, farr = layers[n][()]
        span = len(weights)

        def weight_at(k: int) -> int:
            j = k - wlo
            return weights[j] if 0 <= j < span else 0

        suffix: Dict[tuple, tuple] = {(): (flo, [weight_at(flo + j) for j in range(len(farr))])}
        group_numerators = [0] * len(self.groups)
        for i in range(n - 1, -1, -1):
            g = order[i]
            size = len(self.groups[g])
            binom = [math.comb(size, x) for x in range(size + 1)]
            per_cell = [0] + [math.comb(size - 1, x - 1) for x in range(1, size + 1)]
            forward = layers[i]
            current: Dict[tuple, tuple] = {}
            numerator = 0
            for state, x, next_state in edges[i]:
                target = suffix.get(next_state)
                if target is None:
                    continue
                lo2, arr2 = target
                lo, arr = forward[state]
                entry = current.get(state)
                if entry is None:
                    acc = [0] * len(arr)
                    current[state] = (lo, acc)
                else:
                    acc = entry[1]
                mult = binom[x]
                base = lo + x - lo2
                jlo = -base if base < 0 else 0
                jhi = min(len(arr), len(arr2) - base)
                meter.node((jhi - jlo if jhi > jlo else 0) + 1)
                dot = 0
                for j in range(jlo, jhi):
                    gv = arr2[base + j]
                    if gv:
                        acc[j] += mult * gv
                        dot += arr[j] * gv
                if dot and per_cell[x]:
                    numerator += per_cell[x] * dot
            group_numerators[g] = numerator
            suffix = current
        root = suffix.get(())
        total = root[1][0] if root is not None else 0
        numerators = [0] * len(self.cells)
        for g, members in enumerate(self.groups):
            for v in members:
                numerators[v] = group_numerators[g]
        return numerators, total

    def structural_proofs(self):
        """Cells fixed in every component solution (from the exhaustive DP graph)."""
        safe: List[int] = []
        mines: List[int] = []
        live = {()}
        for i in range(len(self.order) - 1, -1, -1):
            used = set()
            alive = set()
            for state, x, next_state in self.edges[i]:
                if next_state in live:
                    used.add(x)
                    alive.add(state)
            g = self.order[i]
            members = self.groups[g]
            if used == {0}:
                safe.extend(self.cells[v] for v in members)
            elif used == {len(members)}:
                mines.extend(self.cells[v] for v in members)
            live = alive
        return safe, mines


class _Sampler:
    """Sequential importance sampler for one component."""

    def __init__(self, component: _Component, kcap: int) -> None:
        self.n = len(component.cells)
        self.var_cons = component.var_cons
        self.cons_vars = component.cons_vars
        self.need0 = list(component.cons_need)
        self.cap0 = [len(vs) for vs in component.cons_vars]
        self.order = [v for g in component.order for v in component.groups[g]]
        self.kcap = kcap
        self.attempts = 0
        self.successes = 0
        # mine tuple -> [occurrences, number of binary choices]
        self.samples: Dict[tuple, list] = {}

    def draw(self, getrandbits, deadline: float) -> bool:
        """Run one proposal; False if the deadline interrupted it (then it is discarded)."""
        val = [-1] * self.n
        need = self.need0[:]
        cap = self.cap0[:]
        var_cons = self.var_cons
        cons_vars = self.cons_vars
        kcap = self.kcap
        trail: List[int] = []
        ops = 0

        def assign(v0: int, x0: int, mines: int) -> int:
            """Assign and unit-propagate; new mine count, or -1 on conflict."""
            nonlocal ops
            stack = [(v0, x0)]
            while stack:
                v, x = stack.pop()
                current = val[v]
                if current >= 0:
                    if current != x:
                        return -1
                    continue
                ops += 1
                val[v] = x
                trail.append(v)
                bad = False
                if x:
                    mines += 1
                    bad = mines > kcap
                for c in var_cons[v]:
                    k = cap[c] - 1
                    cap[c] = k
                    r = need[c] - x
                    need[c] = r
                    if r < 0 or r > k:
                        bad = True
                    elif k and not bad and (r == 0 or r == k):
                        forced = 0 if r == 0 else 1
                        for u in cons_vars[c]:
                            if val[u] < 0:
                                stack.append((u, forced))
                if bad:
                    return -1
            return mines

        def undo(mark: int) -> None:
            nonlocal ops
            ops += len(trail) - mark
            while len(trail) > mark:
                v = trail.pop()
                x = val[v]
                for c in var_cons[v]:
                    cap[c] += 1
                    need[c] += x
                val[v] = -1

        mines = 0
        choices = 0
        next_check = _DRAW_CLOCK_OPS
        for v in self.order:
            if val[v] >= 0:
                continue
            if ops >= next_check:
                next_check = ops + _DRAW_CLOCK_OPS
                if time.perf_counter() > deadline:
                    return False
            mark = len(trail)
            with_mine = assign(v, 1, mines)
            undo(mark)
            without = assign(v, 0, mines)
            if without >= 0 and with_mine >= 0:
                choices += 1
                if getrandbits(1):
                    undo(mark)
                    mines = assign(v, 1, mines)
                else:
                    mines = without
            elif without >= 0:
                mines = without
            elif with_mine >= 0:
                undo(mark)
                mines = assign(v, 1, mines)
            else:
                self.attempts += 1  # dead end: a completed proposal of weight zero
                return True
        self.attempts += 1
        key = tuple(i for i in range(self.n) if val[i] == 1)
        entry = self.samples.get(key)
        if entry is None:
            self.samples[key] = [1, choices]
        else:
            entry[0] += 1
        self.successes += 1
        return True

    def histogram(self) -> Tuple[int, List[int]]:
        weights: Dict[int, int] = {}
        for mines, (occurrences, choices) in self.samples.items():
            k = len(mines)
            weights[k] = weights.get(k, 0) + (occurrences << choices)
        lo = min(weights)
        arr = [weights.get(k, 0) for k in range(lo, max(weights) + 1)]
        return lo, arr

    def numerators(self, wlo: int, weights: List[int], meter: _Meter):
        numerators = [0] * self.n
        s1 = 0
        s2 = 0
        for mines, (occurrences, choices) in self.samples.items():
            meter.work(len(mines) + 1)
            j = len(mines) - wlo
            w = weights[j] if 0 <= j < len(weights) else 0
            if not w:
                continue
            w <<= choices
            s1 += occurrences * w
            s2 += occurrences * w * w
            total = occurrences * w
            for v in mines:
                numerators[v] += total
        ess = (s1 * s1) / s2 if s2 else 0.0
        return numerators, s1, ess


# --------------------------------------------------------------------- solver


class _Solver:
    def __init__(self, width, height, total, clues, clock, node_budget, sample_budget, rng):
        self.width = width
        self.height = height
        self.n = width * height
        self.total = total
        self.clues = clues
        self.clock = clock
        self.node_budget = node_budget
        self.sample_budget = sample_budget
        self.rng = rng
        self.val = [-1] * self.n
        for index in clues:
            self.val[index] = 0
        self.unknown = self.n - len(clues)
        self.fixed_mines = 0
        self.cons_cell: List[int] = []
        self.cons_need: List[int] = []
        self.cons_unk: List[set] = []
        self.var_cons: List[List[int]] = [[] for _ in range(self.n)]
        self.touched: List[int] = []
        self.nodes_used = 0

    # -- propagation --------------------------------------------------------

    def _build_constraints(self) -> None:
        w, h = self.width, self.height
        clues = self.clues
        for index in sorted(clues):
            clue = clues[index]
            row, col = divmod(index, w)
            hidden = []
            for r in range(max(row - 1, 0), min(row + 2, h)):
                for c in range(max(col - 1, 0), min(col + 2, w)):
                    j = r * w + c
                    if j != index and j not in clues:
                        hidden.append(j)
            if clue > len(hidden):
                raise InconsistentBoardError(
                    f"clue {clue} at cell {index} exceeds its {len(hidden)} unrevealed neighbours"
                )
            if not hidden:
                continue
            cid = len(self.cons_need)
            self.cons_cell.append(index)
            self.cons_need.append(clue)
            self.cons_unk.append(set(hidden))
            for j in hidden:
                self.var_cons[j].append(cid)

    def _assign(self, v: int, x: int, queue: List[int]) -> None:
        self.val[v] = x
        self.unknown -= 1
        if x:
            self.fixed_mines += 1
        need = self.cons_need
        for c in self.var_cons[v]:
            members = self.cons_unk[c]
            members.discard(v)
            if x:
                need[c] -= 1
            r = need[c]
            k = len(members)
            if r < 0 or r > k:
                raise _Contradiction(f"the clue at cell {self.cons_cell[c]} cannot be satisfied")
            if k and (r == 0 or r == k):
                queue.append(c)
            self.touched.append(c)

    def _units(self, queue: List[int]) -> None:
        need = self.cons_need
        unk = self.cons_unk
        while queue:
            c = queue.pop()
            members = unk[c]
            if not members:
                continue
            r = need[c]
            if r == 0:
                x = 0
            elif r == len(members):
                x = 1
            else:
                continue
            for v in sorted(members):
                self._assign(v, x, queue)

    def _global(self, queue: List[int]) -> bool:
        remaining = self.total - self.fixed_mines
        if remaining < 0 or remaining > self.unknown:
            raise _Contradiction("the clues cannot be matched with the total number of mines")
        if self.unknown and (remaining == 0 or remaining == self.unknown):
            x = 0 if remaining == 0 else 1
            val = self.val
            for v in range(self.n):
                if val[v] < 0:
                    self._assign(v, x, queue)
            return True
        return False

    def _pair_check(self, a: int, queue: List[int]) -> bool:
        """Overlap reasoning between clue ``a`` and every clue sharing a cell."""
        unk = self.cons_unk
        need = self.cons_need
        sa = unk[a]
        others = set()
        for v in sa:
            others.update(self.var_cons[v])
        others.discard(a)
        for b in sorted(others):
            sb = unk[b]
            inter = sa & sb
            if not inter:
                continue
            ni = len(inter)
            na = len(sa) - ni
            nb = len(sb) - ni
            ra = need[a]
            rb = need[b]
            lo = max(0, ra - na, rb - nb)
            hi = min(ni, ra, rb)
            if lo > hi:
                raise _Contradiction(
                    f"the clues at cells {self.cons_cell[a]} and {self.cons_cell[b]} conflict"
                )
            forced = []
            if na:
                if ra - hi == na:
                    forced.append((sa - inter, 1))
                elif ra == lo:
                    forced.append((sa - inter, 0))
            if nb:
                if rb - hi == nb:
                    forced.append((sb - inter, 1))
                elif rb == lo:
                    forced.append((sb - inter, 0))
            if lo == ni:
                forced.append((inter, 1))
            elif hi == 0:
                forced.append((inter, 0))
            if forced:
                for cells, x in forced:
                    for v in sorted(cells):
                        if self.val[v] < 0:
                            self._assign(v, x, queue)
                        elif self.val[v] != x:
                            raise _Contradiction("overlapping clues conflict")
                return True
        return False

    def _propagate(self) -> bool:
        """Run all sound deductions; returns False if pair reasoning was cut short."""
        ncons = len(self.cons_need)
        unk = self.cons_unk
        queue = list(range(ncons))
        work = deque(range(ncons))
        queued = bytearray(b"\x01") * ncons
        deadline = self.clock.at(_PAIR_FRACTION)
        complete = True
        steps = 0
        while True:
            self._units(queue)
            if self._global(queue):
                continue
            for c in self.touched:
                if not queued[c] and unk[c]:
                    queued[c] = 1
                    work.append(c)
            self.touched = []
            progressed = False
            while work and complete:
                steps += 1
                if not steps % 64 and time.perf_counter() > deadline:
                    complete = False
                    break
                a = work.popleft()
                queued[a] = 0
                if unk[a] and self._pair_check(a, queue):
                    progressed = True
                    break
            if not progressed:
                return complete

    def _components(self):
        unk = self.cons_unk
        parent = list(range(len(unk)))

        def find(c: int) -> int:
            while parent[c] != c:
                parent[c] = parent[parent[c]]
                c = parent[c]
            return c

        bulk = []
        for v in range(self.n):
            if self.val[v] >= 0:
                continue
            cons = self.var_cons[v]
            if not cons:
                bulk.append(v)
                continue
            root = find(cons[0])
            for c in cons[1:]:
                other = find(c)
                if other != root:
                    parent[other] = root
        grouped: Dict[int, List[int]] = {}
        for c in range(len(unk)):
            if unk[c]:
                grouped.setdefault(find(c), []).append(c)
        components = []
        for cons in grouped.values():
            cells = sorted({v for c in cons for v in unk[c]})
            members = [(self.cons_need[c], sorted(unk[c])) for c in cons]
            components.append(_Component(cells, members))
        components.sort(key=lambda comp: (comp.width, len(comp.cells), comp.cells[0]))
        return components, bulk

    # -- counting and sampling ----------------------------------------------

    def _count(self, components: List[_Component], remaining: int) -> float:
        """Exact forward counting, easiest components first; returns time spent.

        ``node_budget`` bounds the memoised search-node expansions of all
        forward passes together; the marginal pass later re-traverses the same
        stored graph once, so total counting work stays proportional to it.
        """
        left = self.node_budget
        store_left = _MAX_STORED_ENTRIES
        deadline = self.clock.at(_COUNT_FRACTION)
        known_min = 0
        spent = 0.0
        for comp in components:
            comp.mode = "pending"
            if left <= 0 or time.perf_counter() > deadline:
                continue
            meter = _Meter(deadline, node_limit=left, store_limit=store_left)
            began = time.perf_counter()
            try:
                solvable = comp.count(remaining - known_min, meter)
            except _OutOfBudget:
                comp.layers = comp.edges = None
                left -= meter.nodes
                self.nodes_used += meter.nodes
                continue
            spent += time.perf_counter() - began
            left -= meter.nodes
            store_left -= meter.stored
            self.nodes_used += meter.nodes
            if not solvable:
                raise InconsistentBoardError(
                    "no mine layout satisfies the clues around cells "
                    f"{comp.cells[0]}..{comp.cells[-1]} within the mine total"
                )
            comp.mode = EXACT
            known_min += comp.hist[0]
        return spent

    def _sample(self, hard: List[_Component], known_min: int, remaining: int,
                deadline: float) -> Optional[str]:
        if self.rng is None:
            self.rng = _default_rng(self.width, self.height, self.total, self.clues)
        getrandbits = self.rng.getrandbits
        quotas = []
        base, extra = divmod(self.sample_budget, len(hard))
        for i, comp in enumerate(hard):
            comp.mode = "sampled"
            comp.sampler = _Sampler(comp, remaining - known_min)
            quotas.append(base + (1 if i < extra else 0))
        # Round-robin batches so every hard component gets its share of time.
        pending = [i for i in range(len(hard)) if quotas[i] > 0]
        out_of_time = False
        while pending and not out_of_time:
            still = []
            for i in pending:
                sampler = hard[i].sampler
                for _ in range(min(_SAMPLE_BATCH, quotas[i] - sampler.attempts)):
                    if not sampler.draw(getrandbits, deadline) or time.perf_counter() > deadline:
                        out_of_time = True
                        break
                if sampler.attempts < quotas[i]:
                    still.append(i)
                if out_of_time:
                    break
            pending = still
        for comp, quota in zip(hard, quotas):
            if comp.sampler.attempts == 0:
                return "time_budget_exhausted" if quota else "sampling_budget_exhausted"
        for comp in hard:
            if comp.sampler.successes == 0:
                return "no_consistent_samples"
        for comp in hard:
            comp.hist = comp.sampler.histogram()
        return None

    # -- combination ------------------------------------------------------------

    def _combine(self, components, pool: int, remaining: int, meter: _Meter):
        qlo, q = 0, [1]
        for comp in components:
            hlo, h = comp.hist
            qlo, q = _poly_mul(qlo, q, hlo, h, remaining, meter)
            if not q:
                return 0, 0, {}
        tlo = max(qlo, remaining - pool)
        thi = min(qlo + len(q) - 1, remaining)
        meter.work(2 * max(0, thi - tlo + 1))
        T = _binomial_row(pool, remaining, tlo, thi)
        # Pool cell: sum_t Q[t] * C(U-1, R-t-1) / Z, computed as
        # sum_t Q[t] * C(U, R-t) * (R-t) / (U * Z) since C(U-1, R-t-1) =
        # C(U, R-t) * (R-t) / U; numerator and denominator stay integers.
        z = 0
        bulk = 0
        for s in range(tlo, thi + 1):
            w = q[s - qlo] * T[s - tlo]
            if w:
                z += w
                bulk += w * (remaining - s)
        cavities: Dict[tuple, List[int]] = {}
        if z:
            for comp in components:
                hlo, h = comp.hist
                key = (hlo, tuple(h))
                if key not in cavities:
                    cavities[key] = _cavity(qlo, q, hlo, h, tlo, T, meter)
        return z, bulk, cavities

    # -- driver -------------------------------------------------------------------

    def run(self) -> dict:
        clock = self.clock
        try:
            self._build_constraints()
            pairs_complete = self._propagate()
        except _Contradiction as exc:
            raise InconsistentBoardError(str(exc)) from None
        components, bulk_cells = self._components()
        pool = len(bulk_cells)
        remaining = self.total - self.fixed_mines

        forward_time = self._count(components, remaining)
        exact_comps = [c for c in components if c.mode == EXACT]
        hard = [c for c in components if c.mode != EXACT]
        known_min = sum(c.hist[0] for c in exact_comps)

        reason: Optional[str] = "counting_budget_exceeded" if hard else None
        failure: Optional[str] = None
        if hard:
            if clock.infinite:
                sample_deadline = math.inf
            else:
                sample_deadline = min(
                    clock.at(_SAMPLE_FRACTION),
                    clock.deadline - _BACKWARD_TIME_FACTOR * forward_time
                    - _RESERVE_FRACTION * clock.budget,
                )
            if self.sample_budget <= 0:
                failure = "sampling_budget_exhausted"
            elif time.perf_counter() >= sample_deadline:
                failure = "time_budget_exhausted"
            else:
                failure = self._sample(hard, known_min, remaining, sample_deadline)

        z = 0
        bulk_numerator = 0
        numerators: Dict[int, List[int]] = {}
        if failure is None:
            meter = _Meter(clock.deadline)
            try:
                z, bulk_numerator, cavities = self._combine(components, pool, remaining, meter)
                if z == 0:
                    if not hard:
                        raise InconsistentBoardError(
                            "no mine layout satisfies the clues and the total number of mines"
                        )
                    failure = "no_globally_compatible_samples"
                else:
                    for ci, comp in enumerate(components):
                        hlo, h = comp.hist
                        weights = cavities[(hlo, tuple(h))]
                        if comp.mode == EXACT:
                            nums, check = comp.exact_numerators(hlo, weights, meter)
                            if check != z:
                                raise ArithmeticError("component weight mismatch")
                        else:
                            nums, check, comp.ess = comp.sampler.numerators(hlo, weights, meter)
                            if check != z:
                                raise ArithmeticError("sample weight mismatch")
                        numerators[ci] = nums
            except _OutOfBudget:
                failure = "time_budget_exhausted"
        if failure is None and hard:
            if min(c.ess for c in hard) < MIN_EFFECTIVE_SAMPLE_SIZE:
                failure = "insufficient_effective_samples"

        if failure is not None:
            status = UNAVAILABLE
            reason = failure
        elif hard:
            status = APPROXIMATE
        else:
            status = EXACT

        n = self.n
        val = self.val
        probabilities: List[Optional[float]] = [None] * n
        proven_safe = []
        proven_mines = []
        for v in range(n):
            if v in self.clues or val[v] < 0:
                continue
            probabilities[v] = float(val[v])
            (proven_mines if val[v] else proven_safe).append(v)

        exact = status == EXACT
        if status != UNAVAILABLE:
            for ci, comp in enumerate(components):
                for v, num in zip(comp.cells, numerators[ci]):
                    probabilities[v] = _ratio(num, z, exact)
                    if exact and num == 0:
                        proven_safe.append(v)
                    elif exact and num == z:
                        proven_mines.append(v)
            if pool:
                denominator = pool * z
                p_bulk = _ratio(bulk_numerator, denominator, exact)
                for v in bulk_cells:
                    probabilities[v] = p_bulk
                if exact and bulk_numerator == 0:
                    proven_safe.extend(bulk_cells)
                elif exact and bulk_numerator == denominator:
                    proven_mines.extend(bulk_cells)
        if not exact:
            # Only exhaustive (exactly counted) components can prove cells here.
            for comp in exact_comps:
                safe, mines = comp.structural_proofs()
                for v in safe:
                    probabilities[v] = 0.0
                proven_safe.extend(safe)
                for v in mines:
                    probabilities[v] = 1.0
                proven_mines.extend(mines)

        sampled = [c for c in hard if c.sampler is not None]
        samples = sum(c.sampler.successes for c in sampled)
        attempts = sum(c.sampler.attempts for c in sampled)
        computed = [c.ess for c in sampled if c.ess is not None]
        ess = min(computed) if computed else None
        frontier = sum(len(c.cells) for c in components)
        proven_safe.sort()
        proven_mines.sort()
        message = self._message(status, reason, frontier, len(components), pool,
                                len(hard), samples, ess, len(proven_safe), len(proven_mines))
        meta = {
            "frontier_cells": frontier,
            "components": len(components),
            "unconstrained_cells": pool,
            "samples": samples,
            "elapsed_ms": round((time.perf_counter() - clock.started) * 1000.0, 3),
            "reason": reason,
            "exact_components": len(exact_comps),
            "sampled_components": len(hard),
            "sample_attempts": attempts,
            "effective_sample_size": None if ess is None else round(ess, 3),
            "nodes": self.nodes_used,
            "hidden_cells": self.n - len(self.clues),
            "remaining_mines": remaining,
            "propagated_cells": self.n - len(self.clues) - frontier - pool,
            "pair_reasoning_complete": pairs_complete,
        }
        return {
            "status": status,
            "probabilities": probabilities,
            "proven_safe": proven_safe,
            "proven_mines": proven_mines,
            "message": message,
            "meta": meta,
        }

    def _message(self, status, reason, frontier, n_components, pool, n_hard,
                 samples, ess, n_safe, n_mines) -> str:
        hidden = self.n - len(self.clues)
        if status == EXACT:
            text = (
                f"Exact probabilities for {hidden} unrevealed cells "
                f"({frontier} next to clues in {n_components} group(s), {pool} unconstrained)."
            )
        elif status == APPROXIMATE:
            text = (
                f"Approximate probabilities: exact counting exceeded the budget for {n_hard} of "
                f"{n_components} group(s), estimated from {samples} weighted samples "
                f"(effective sample size {ess:.0f}). Only logically proven cells are marked certain."
            )
        else:
            text = f"Probabilities unavailable: {_REASON_TEXT.get(reason, reason)}."
        if n_safe or n_mines:
            text += f" Certain: {n_safe} safe cell(s), {n_mines} mine(s)."
        return text
