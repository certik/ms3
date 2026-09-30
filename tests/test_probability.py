"""Tests for minesweeper.probability.

Expected values come from an independent oracle: brute-force enumeration of
every placement of the mines on small boards, using only the public
observation (clues of revealed cells and the mine total).
"""

import inspect
import itertools
import math
import random
import sys
import time
import unittest
from fractions import Fraction
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from minesweeper.probability import (  # noqa: E402
    MIN_EFFECTIVE_SAMPLE_SIZE,
    InconsistentBoardError,
    calculate_probabilities,
)

STATUSES = {"exact", "approximate", "unavailable"}
REQUIRED_META = {
    "frontier_cells", "components", "unconstrained_cells", "samples", "elapsed_ms", "reason",
}
# Generous wall-clock budget so budget-dependent tests are driven by nodes/samples.
SLOW = 60.0


def neighbours(index, width, height):
    row, col = divmod(index, width)
    return [
        r * width + c
        for r in range(max(row - 1, 0), min(row + 2, height))
        for c in range(max(col - 1, 0), min(col + 2, width))
        if (r, c) != (row, col)
    ]


def clues_for(width, height, mines, cells):
    return {i: sum(j in mines for j in neighbours(i, width, height)) for i in cells}


def brute_force(width, height, total, revealed):
    """Exact odds by enumerating every placement of ``total`` mines, or None if impossible."""
    n = width * height
    hidden = [i for i in range(n) if i not in revealed]
    checks = []
    for index, clue in revealed.items():
        mask = 0
        for j in neighbours(index, width, height):
            if j not in revealed:
                mask |= 1 << j
        checks.append((mask, clue))
    counts = [0] * n
    layouts = 0
    for combo in itertools.combinations(hidden, total):
        bits = 0
        for i in combo:
            bits |= 1 << i
        if all(bin(bits & mask).count("1") == clue for mask, clue in checks):
            layouts += 1
            for i in combo:
                counts[i] += 1
    if layouts == 0:
        return None
    return [None if i in revealed else Fraction(counts[i], layouts) for i in range(n)]


def random_observation(seed, sizes=((3, 3), (4, 3), (4, 4), (5, 3), (1, 9), (2, 6), (6, 2), (5, 4))):
    """A consistent observation generated from a random layout (the layout is then discarded)."""
    rng = random.Random(seed)
    width, height = rng.choice(sizes)
    n = width * height
    total = rng.randint(1, max(1, n // 3))
    mines = set(rng.sample(range(n), total))
    safe = [i for i in range(n) if i not in mines]
    minimum = max(0, n - 14)  # keep at most 14 hidden cells for the oracle
    shown = rng.sample(safe, rng.randint(min(minimum, len(safe)), len(safe)))
    return width, height, total, clues_for(width, height, mines, shown)


def chain_board(blocks, pad_between=True):
    """1-row board of blocks ``? 1 ? 1 ? [?]``.

    Each block is an isolated component with solutions {middle mine} (1 mine)
    or {both ends mined} (2 mines); the optional trailing ``?`` touches no clue.
    """
    block = 6 if pad_between else 5
    width = blocks * block
    revealed = {}
    for b in range(blocks):
        revealed[b * block + 1] = 1
        revealed[b * block + 3] = 1
    return width, revealed


class ResultChecks(unittest.TestCase):
    def check_shape(self, result, width, height, revealed):
        n = width * height
        self.assertEqual(
            set(result), {"status", "probabilities", "proven_safe", "proven_mines", "message", "meta"}
        )
        self.assertIn(result["status"], STATUSES)
        self.assertIsInstance(result["message"], str)
        self.assertTrue(result["message"])
        probabilities = result["probabilities"]
        self.assertEqual(len(probabilities), n)
        for i, p in enumerate(probabilities):
            if i in revealed:
                self.assertIsNone(p, f"revealed cell {i} must have no probability")
            elif p is not None:
                self.assertIsInstance(p, float)
                self.assertTrue(0.0 <= p <= 1.0 and not math.isnan(p))
        safe, mines = result["proven_safe"], result["proven_mines"]
        self.assertEqual(safe, sorted(set(safe)))
        self.assertEqual(mines, sorted(set(mines)))
        self.assertFalse(set(safe) & set(mines))
        self.assertFalse((set(safe) | set(mines)) & set(revealed))
        for i in safe:
            self.assertEqual(probabilities[i], 0.0)
        for i in mines:
            self.assertEqual(probabilities[i], 1.0)
        if result["status"] == "exact":
            # Exact endpoints only ever come from integer-proven certainty.
            for i, p in enumerate(probabilities):
                if p == 0.0:
                    self.assertIn(i, safe)
                elif p == 1.0:
                    self.assertIn(i, mines)
        meta = result["meta"]
        self.assertTrue(REQUIRED_META <= set(meta))
        for key in ("frontier_cells", "components", "unconstrained_cells", "samples"):
            self.assertIsInstance(meta[key], int)
            self.assertGreaterEqual(meta[key], 0)
        self.assertGreaterEqual(meta["elapsed_ms"], 0)
        if result["status"] == "exact":
            self.assertIsNone(meta["reason"])
        else:
            self.assertIsInstance(meta["reason"], str)
        if result["status"] == "unavailable":
            proven = set(safe) | set(mines)
            for i, p in enumerate(probabilities):
                if i not in proven:
                    self.assertIsNone(p, "unavailable results must not invent odds")

    def check_truthful_proofs(self, result, expected):
        for i in result["proven_safe"]:
            self.assertEqual(expected[i], 0, f"cell {i} listed safe but can be a mine")
        for i in result["proven_mines"]:
            self.assertEqual(expected[i], 1, f"cell {i} listed as mine but can be safe")

    def check_mine_sum(self, result, total, places=7):
        values = [p for p in result["probabilities"] if p is not None]
        self.assertAlmostEqual(sum(values), total, places=places)


class ExactAgreementTests(ResultChecks):
    def test_random_small_boards_match_brute_force(self):
        exact_cases = 0
        for seed in range(150):
            width, height, total, revealed = random_observation(seed)
            expected = brute_force(width, height, total, revealed)
            result = calculate_probabilities(width, height, total, revealed, time_budget=SLOW)
            with self.subTest(seed=seed):
                self.check_shape(result, width, height, revealed)
                self.assertEqual(result["status"], "exact")
                for i, want in enumerate(expected):
                    if want is None:
                        continue
                    self.assertAlmostEqual(result["probabilities"][i], float(want), places=12)
                    # 0.0 and 1.0 appear exactly when the cell is certain.
                    self.assertEqual(result["probabilities"][i] == 0.0, want == 0)
                    self.assertEqual(result["probabilities"][i] == 1.0, want == 1)
                certain_safe = [i for i, p in enumerate(expected) if p == 0]
                certain_mines = [i for i, p in enumerate(expected) if p == 1]
                self.assertEqual(result["proven_safe"], certain_safe)
                self.assertEqual(result["proven_mines"], certain_mines)
                self.check_mine_sum(result, total)
                exact_cases += 1
        self.assertEqual(exact_cases, 150)

    def test_isolated_components_are_coupled_by_the_mine_total(self):
        width, revealed = chain_board(2)  # components {0,2,4}, {6,8,10}; free cells 5, 11
        odds = {}
        for total in range(2, 7):
            expected = brute_force(width, 1, total, revealed)
            result = calculate_probabilities(width, 1, total, revealed, time_budget=SLOW)
            self.check_shape(result, width, 1, revealed)
            self.assertEqual(result["status"], "exact")
            self.assertEqual(result["meta"]["components"], 2)
            self.assertEqual(result["meta"]["unconstrained_cells"], 2)
            for i, want in enumerate(expected):
                if want is not None:
                    self.assertAlmostEqual(result["probabilities"][i], float(want), places=12)
            odds[total] = result
        # The same local clues give different odds for different totals.
        self.assertNotAlmostEqual(odds[3]["probabilities"][2], odds[5]["probabilities"][2])
        # With 6 mines both components must hold 2 and both free cells are mines.
        self.assertEqual(odds[6]["proven_mines"], [0, 4, 5, 6, 10, 11])
        self.assertEqual(odds[6]["proven_safe"], [2, 8])
        # With 2 mines each component holds exactly one (its middle cell).
        self.assertEqual(odds[2]["proven_mines"], [2, 8])
        self.assertEqual(odds[2]["proven_safe"], [0, 4, 5, 6, 10, 11])

    def test_unconstrained_region_weights_component_mine_counts(self):
        # "? 1 ? 1 ?" plus six cells touching no clue, three mines in total:
        # middle mine leaves C(6,2)=15 layouts, both ends leave C(6,1)=6.
        width = 11
        revealed = {1: 1, 3: 1}
        expected = brute_force(width, 1, 3, revealed)
        self.assertEqual(expected[2], Fraction(15, 21))
        result = calculate_probabilities(width, 1, 3, revealed, time_budget=SLOW)
        self.assertEqual(result["status"], "exact")
        self.assertAlmostEqual(result["probabilities"][2], 15 / 21, places=12)
        self.assertAlmostEqual(result["probabilities"][0], 6 / 21, places=12)
        # Free cells share the conditioned remainder, not the naive prior 3/9.
        free = (15 * Fraction(2, 6) + 6 * Fraction(1, 6)) / 21
        self.assertEqual(expected[7], free)
        for i in range(5, 11):
            self.assertAlmostEqual(result["probabilities"][i], float(free), places=12)
        self.assertNotAlmostEqual(result["probabilities"][7], 3 / 9, places=3)
        self.check_mine_sum(result, 3)

    def test_frontier_and_pool_on_2d_board(self):
        width, height = 6, 3
        revealed = {0: 1}  # exactly one mine among cells 1, 6, 7
        expected = brute_force(width, height, 5, revealed)
        result = calculate_probabilities(width, height, 5, revealed, time_budget=SLOW)
        self.assertEqual(result["status"], "exact")
        for i, want in enumerate(expected):
            if want is not None:
                self.assertAlmostEqual(result["probabilities"][i], float(want), places=12)
        self.assertAlmostEqual(result["probabilities"][1], 1 / 3, places=12)
        self.assertAlmostEqual(result["probabilities"][17], 4 / 14, places=12)
        self.assertEqual(result["meta"]["frontier_cells"], 3)
        self.assertEqual(result["meta"]["unconstrained_cells"], 14)

    def test_forced_cells_are_proven(self):
        result = calculate_probabilities(4, 1, 1, {0: 1, 3: 0}, time_budget=SLOW)
        self.assertEqual(result["status"], "exact")
        self.assertEqual(result["probabilities"], [None, 1.0, 0.0, None])
        self.assertEqual(result["proven_mines"], [1])
        self.assertEqual(result["proven_safe"], [2])

    def test_overlapping_clue_deductions_match_brute_force(self):
        # A classic 1-2-1 wall: row 1 revealed, row 0 hidden, row 2 revealed zeros-free.
        width, height = 5, 3
        mines = {1, 3}
        shown = [5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
        revealed = clues_for(width, height, mines, shown)
        expected = brute_force(width, height, 2, revealed)
        result = calculate_probabilities(width, height, 2, revealed, time_budget=SLOW)
        self.check_shape(result, width, height, revealed)
        self.check_truthful_proofs(result, expected)
        self.assertEqual(result["proven_mines"], [i for i, p in enumerate(expected) if p == 1])
        self.assertEqual(result["proven_safe"], [i for i, p in enumerate(expected) if p == 0])

    def test_no_frontier_uses_uniform_prior(self):
        result = calculate_probabilities(5, 4, 7, {}, time_budget=SLOW)
        self.assertEqual(result["status"], "exact")
        self.assertEqual(result["probabilities"], [7 / 20] * 20)
        self.assertEqual((result["proven_safe"], result["proven_mines"]), ([], []))
        self.assertEqual(result["meta"]["unconstrained_cells"], 20)
        self.assertEqual(result["meta"]["frontier_cells"], 0)
        none = calculate_probabilities(3, 3, 0, {}, time_budget=SLOW)
        self.assertEqual(none["proven_safe"], list(range(9)))
        full = calculate_probabilities(3, 3, 9, {}, time_budget=SLOW)
        self.assertEqual(full["proven_mines"], list(range(9)))

    def test_all_cells_revealed(self):
        result = calculate_probabilities(2, 2, 0, {0: 0, 1: 0, 2: 0, 3: 0}, time_budget=SLOW)
        self.check_shape(result, 2, 2, {0, 1, 2, 3})
        self.assertEqual(result["status"], "exact")
        self.assertEqual(result["probabilities"], [None] * 4)

    def test_long_chain_component_is_exact_without_recursion(self):
        # "? 1 ? 1 ? ... ?" with 1501 hidden cells: x_i + x_(i+1) = 1 along the
        # chain, so 751 mines force the alternating layout starting with a mine.
        width = 3001
        revealed = {i: 1 for i in range(1, width, 2)}
        result = calculate_probabilities(width, 1, 751, revealed)
        self.check_shape(result, width, 1, revealed)
        self.assertEqual(result["status"], "exact")
        self.assertEqual(result["meta"]["components"], 1)
        self.assertEqual(result["proven_mines"], list(range(0, width, 4)))
        self.assertEqual(result["proven_safe"], list(range(2, width, 4)))


class InvalidInputTests(unittest.TestCase):
    def test_contradictory_observations_raise(self):
        cases = [
            ("clue larger than unrevealed neighbours", (3, 1, 1, {0: 2})),
            ("clue on a board corner exceeding neighbours", (2, 2, 3, {0: 4})),
            ("adjacent clues disagree", (3, 1, 1, {0: 1, 2: 0})),
            ("too many mines for unrevealed cells", (3, 1, 3, {0: 1})),
            ("forced mines exceed the total", (5, 1, 1, {0: 1, 4: 1})),
            ("mine total unreachable by any combination", (6, 1, 3, {1: 1, 4: 1})),
            ("two clues over the same cells demand different counts", (3, 2, 2, {1: 1, 4: 2})),
        ]
        for label, (width, height, total, revealed) in cases:
            with self.subTest(label):
                self.assertIsNone(brute_force(width, height, total, revealed))
                with self.assertRaises(InconsistentBoardError):
                    calculate_probabilities(width, height, total, revealed, time_budget=SLOW)

    def test_inconsistency_is_a_value_error(self):
        self.assertTrue(issubclass(InconsistentBoardError, ValueError))

    def test_malformed_arguments_raise_value_error(self):
        cases = [
            (0, 3, 1, {}),
            (3, -1, 1, {}),
            (3, 3, -1, {}),
            (3, 3, 10, {}),
            (3, 3, 1, {9: 1}),
            (3, 3, 1, {-1: 1}),
            (3, 3, 1, {0: 9}),
            (3, 3, 1, {0: -1}),
            (3, 3, 1, {0: True}),
            (3, 3, 1, {"0": 1}),
            (3, 3, 1.0, {}),
            (True, 3, 1, {}),
            (3, 3, 1, [(0, 1)]),
        ]
        for args in cases:
            with self.subTest(args=args):
                with self.assertRaises(ValueError):
                    calculate_probabilities(*args)
        for kwargs in ({"time_budget": -1}, {"time_budget": float("nan")}, {"node_budget": -1},
                       {"sample_budget": -5}, {"rng": object()}):
            with self.subTest(kwargs=kwargs):
                with self.assertRaises(ValueError):
                    calculate_probabilities(3, 3, 1, {}, **kwargs)


class PublicInformationOnlyTests(unittest.TestCase):
    def test_signature_has_no_secret_or_flag_inputs(self):
        parameters = inspect.signature(calculate_probabilities).parameters
        self.assertEqual(
            list(parameters),
            ["width", "height", "total_mines", "revealed", "time_budget", "node_budget",
             "sample_budget", "rng"],
        )
        for name in ("time_budget", "node_budget", "sample_budget", "rng"):
            self.assertIs(parameters[name].kind, inspect.Parameter.KEYWORD_ONLY)
        for secret in ({"flags": [1]}, {"mines": [1]}, {"layout": [1]}, {"board": [1]}):
            with self.assertRaises(TypeError):
                calculate_probabilities(3, 3, 1, {}, **secret)

    def test_layouts_with_the_same_observation_get_the_same_answer(self):
        width, height = 4, 4
        shown = [0, 1, 4, 5, 10, 15]
        first = {2, 8, 12}
        observation = clues_for(width, height, first, shown)
        # Find a different layout with identical public clues.
        hidden = [i for i in range(16) if i not in shown]
        other = next(
            set(c) for c in itertools.combinations(hidden, 3)
            if set(c) != first and clues_for(width, height, set(c), shown) == observation
        )
        self.assertEqual(clues_for(width, height, other, shown), observation)
        a = calculate_probabilities(width, height, 3, observation)
        b = calculate_probabilities(width, height, 3, dict(observation))
        self.assertEqual(a["probabilities"], b["probabilities"])
        self.assertEqual((a["proven_safe"], a["proven_mines"]), (b["proven_safe"], b["proven_mines"]))


class LargeBoardTests(ResultChecks):
    @staticmethod
    def played_board(width, height, density, seed, openings):
        rng = random.Random(seed)
        n = width * height
        mines = set(rng.sample(range(n), int(n * density)))
        clue = clues_for(width, height, mines, range(n))
        revealed = {}
        zeros = [i for i in range(n) if i not in mines and clue[i] == 0]
        for start in rng.sample(zeros, min(openings, len(zeros))):
            stack = [start]
            while stack:
                i = stack.pop()
                if i in revealed or i in mines:
                    continue
                revealed[i] = clue[i]
                if clue[i] == 0:
                    stack.extend(neighbours(i, width, height))
        return mines, revealed

    def test_uniform_prior_on_largest_board_does_not_overflow(self):
        result = calculate_probabilities(80, 80, 3200, {})
        self.assertEqual(result["status"], "exact")
        self.assertEqual(set(result["probabilities"]), {0.5})

    def test_large_played_board_is_exact_bounded_and_consistent(self):
        for seed in range(2):
            mines, revealed = self.played_board(80, 80, 0.2, seed, 40)
            started = time.perf_counter()
            result = calculate_probabilities(80, 80, len(mines), revealed, time_budget=1.5)
            elapsed = time.perf_counter() - started
            with self.subTest(seed=seed):
                self.check_shape(result, 80, 80, revealed)
                self.assertEqual(result["status"], "exact")
                self.assertLess(elapsed, 3.0)
                self.check_mine_sum(result, len(mines), places=6)
                # Proofs must agree with the (hidden) layout that produced the clues.
                self.assertFalse(set(result["proven_safe"]) & mines)
                self.assertTrue(set(result["proven_mines"]) <= mines)
                self.assertGreater(result["meta"]["unconstrained_cells"], 0)

    def test_huge_binomial_weights_keep_frontier_odds_exact(self):
        # One "1" clue in a corner of an 80x80 board with 2000 mines: the
        # frontier holds exactly one mine among 3 cells, so the pool holds
        # 1999 mines in the other 6396 cells.  Huge C(6396, 1999) weights
        # must cancel exactly.
        result = calculate_probabilities(80, 80, 2000, {0: 1})
        self.assertEqual(result["status"], "exact")
        for cell in (1, 80, 81):
            self.assertEqual(result["probabilities"][cell], 1 / 3)
        self.assertEqual(result["probabilities"][6399], 1999 / 6396)
        self.check_mine_sum(result, 2000, places=6)

    def test_near_certain_cells_are_not_proven_and_impossible_cells_are(self):
        # Cell y is surrounded by eight revealed 1-clues on an 80x80 board with
        # 4 mines.  Either y is the mine (the 6375-cell pool keeps 3 mines) or
        # one of two "pinwheels" of 4 ring cells holds the mines (pool empty).
        # The pinwheels weigh 1 against C(6375, 3), so their cells have odds
        # near 2e-11 and y near 1 - 5e-11: extreme, yet not certain.  The other
        # ring cells are impossible and must be proven from integer zeros.
        width = 80
        y = 40 * width + 40
        clue_cells = [c for c in neighbours(y, width, width)]
        revealed = {c: 1 for c in clue_cells}
        frontier = sorted({j for c in clue_cells for j in neighbours(c, width, width)} - set(clue_cells))
        self.assertEqual(len(frontier), 17)
        pool = width * width - len(revealed) - len(frontier)
        # Independent oracle: every frontier assignment, pool weighted by C(U, 4 - k).
        checks = [[frontier.index(j) for j in neighbours(c, width, width) if j in frontier]
                  for c in clue_cells]
        weight_total = 0
        weight_mine = [0] * len(frontier)
        for bits in itertools.product((0, 1), repeat=len(frontier)):
            if all(sum(bits[j] for j in members) == 1 for members in checks):
                k = sum(bits)
                weight = math.comb(pool, 4 - k)
                weight_total += weight
                for j, bit in enumerate(bits):
                    if bit:
                        weight_mine[j] += weight
        expected = {cell: Fraction(weight_mine[j], weight_total) for j, cell in enumerate(frontier)}

        result = calculate_probabilities(width, width, 4, revealed)
        self.check_shape(result, width, width, revealed)
        self.assertEqual(result["status"], "exact")
        self.assertEqual(result["proven_safe"], sorted(c for c, p in expected.items() if p == 0))
        self.assertEqual(result["proven_mines"], [])
        tiny = [c for c, p in expected.items() if 0 < p < Fraction(1, 10**10)]
        self.assertEqual(len(tiny), 8)
        for cell in tiny:
            got = result["probabilities"][cell]
            self.assertGreater(got, 0.0)
            self.assertAlmostEqual(got / float(expected[cell]), 1.0, places=12)
        self.assertLess(1 - expected[y], Fraction(1, 10**10))
        self.assertLess(result["probabilities"][y], 1.0)
        self.assertAlmostEqual(result["probabilities"][y], float(expected[y]), places=15)
        self.check_mine_sum(result, 4, places=6)

    def test_exact_ratios_reserve_endpoints_for_certainty(self):
        # White-box check of the final integer -> float step: a non-certain
        # exact ratio that rounds to an endpoint must not be shown as certain,
        # while sampled (approximate) estimates are reported unmodified.
        from minesweeper.probability import _ratio

        huge = 10 ** 400
        self.assertEqual(_ratio(0, huge, True), 0.0)
        self.assertEqual(_ratio(huge, huge, True), 1.0)
        self.assertGreater(_ratio(1, huge, True), 0.0)
        self.assertLess(_ratio(huge - 1, huge, True), 1.0)
        self.assertEqual(_ratio(1, huge, False), 0.0)
        self.assertEqual(_ratio(huge - 1, huge, False), 1.0)
        self.assertAlmostEqual(_ratio(huge // 3, huge, True), 1 / 3, places=15)


class SamplingTests(ResultChecks):
    def sampling_run(self, width, height, total, revealed, seed, **kwargs):
        options = dict(time_budget=SLOW, node_budget=0, sample_budget=4000, rng=random.Random(seed))
        options.update(kwargs)
        return calculate_probabilities(width, height, total, revealed, **options)

    def test_sampling_estimates_agree_with_brute_force(self):
        checked = 0
        for seed in range(40):
            width, height, total, revealed = random_observation(seed)
            expected = brute_force(width, height, total, revealed)
            result = self.sampling_run(width, height, total, revealed, seed)
            with self.subTest(seed=seed):
                self.check_shape(result, width, height, revealed)
                self.check_truthful_proofs(result, expected)
                if result["meta"]["components"] == 0:
                    self.assertEqual(result["status"], "exact")
                    continue
                self.assertEqual(result["status"], "approximate")
                self.assertEqual(result["meta"]["reason"], "counting_budget_exceeded")
                ess = result["meta"]["effective_sample_size"]
                self.assertGreaterEqual(ess, MIN_EFFECTIVE_SAMPLE_SIZE)
                # Each estimate is a weighted mean with standard error at most
                # 0.5 / sqrt(ESS); allow five standard errors.
                tolerance = 5 * 0.5 / math.sqrt(ess)
                for i, want in enumerate(expected):
                    if want is not None:
                        self.assertLessEqual(abs(result["probabilities"][i] - float(want)), tolerance)
                self.check_mine_sum(result, total)
                checked += 1
        self.assertGreater(checked, 10)

    def test_sampled_certainties_are_not_reported_as_proofs(self):
        # Alternating chain whose two solutions hold 750 and 751 mines; with 751
        # mines only one survives global conditioning, so every sampled
        # estimate is 0 or 1 -- yet nothing may be listed as proven.
        width = 3001
        revealed = {i: 1 for i in range(1, width, 2)}
        result = self.sampling_run(width, 1, 751, revealed, seed=3, sample_budget=300)
        self.check_shape(result, width, 1, revealed)
        self.assertEqual(result["status"], "approximate")
        self.assertEqual(result["proven_safe"], [])
        self.assertEqual(result["proven_mines"], [])
        for i in range(0, width, 2):
            self.assertEqual(result["probabilities"][i], 1.0 if i % 4 == 0 else 0.0)
        self.check_mine_sum(result, 751)

    def test_injected_rng_makes_sampling_deterministic(self):
        width, height, total, revealed = random_observation(11)
        first = self.sampling_run(width, height, total, revealed, seed=99)
        second = self.sampling_run(width, height, total, revealed, seed=99)
        for result in (first, second):
            result["meta"].pop("elapsed_ms")
        self.assertEqual(first, second)

    def test_default_rng_is_reproducible(self):
        width, height, total, revealed = random_observation(11)
        runs = [
            calculate_probabilities(width, height, total, revealed, node_budget=0, time_budget=SLOW)
            for _ in range(2)
        ]
        self.assertEqual(runs[0]["probabilities"], runs[1]["probabilities"])

    def test_low_node_budget_mixes_exact_and_sampled_components(self):
        # Component A: one clue over two interchangeable cells (a single search
        # node).  Component B: a longer chain that needs more nodes.
        width = 18
        revealed = {1: 1, 5: 1, 7: 1, 9: 1, 11: 1, 13: 1}
        total = 5
        expected = brute_force(width, 1, total, revealed)
        result = calculate_probabilities(width, 1, total, revealed, time_budget=SLOW,
                                         node_budget=2, sample_budget=4000, rng=random.Random(5))
        self.check_shape(result, width, 1, revealed)
        self.check_truthful_proofs(result, expected)
        self.assertEqual(result["status"], "approximate")
        self.assertGreaterEqual(result["meta"]["exact_components"], 1)
        self.assertGreaterEqual(result["meta"]["sampled_components"], 1)
        tolerance = 5 * 0.5 / math.sqrt(result["meta"]["effective_sample_size"])
        for i, want in enumerate(expected):
            if want is not None:
                self.assertLessEqual(abs(result["probabilities"][i] - float(want)), tolerance)
        self.check_mine_sum(result, total)

    def test_no_sampling_budget_is_unavailable(self):
        width, revealed = chain_board(2)
        result = calculate_probabilities(width, 1, 4, revealed, time_budget=SLOW,
                                         node_budget=0, sample_budget=0)
        self.check_shape(result, width, 1, revealed)
        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["meta"]["reason"], "sampling_budget_exhausted")
        self.assertEqual(result["meta"]["samples"], 0)
        hidden = [i for i in range(width) if i not in revealed]
        self.assertTrue(all(result["probabilities"][i] is None for i in hidden))

    def test_unavailable_still_reports_logical_proofs(self):
        # Cell 12 is forced by the lone clue at 13; the chain needs sampling.
        width, revealed = chain_board(2)
        width += 2
        revealed[width - 1] = 1  # hidden neighbour: only cell width-2
        result = calculate_probabilities(width, 1, 5, revealed, time_budget=SLOW,
                                         node_budget=0, sample_budget=0)
        self.check_shape(result, width, 1, revealed)
        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["proven_mines"], [width - 2])
        self.assertEqual(result["probabilities"][width - 2], 1.0)

    def test_too_few_effective_samples_is_unavailable(self):
        width, revealed = chain_board(1)
        result = calculate_probabilities(width, 1, 2, revealed, time_budget=SLOW,
                                         node_budget=0, sample_budget=5, rng=random.Random(1))
        self.check_shape(result, width, 1, revealed)
        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["meta"]["reason"], "insufficient_effective_samples")
        self.assertLess(result["meta"]["effective_sample_size"], MIN_EFFECTIVE_SAMPLE_SIZE)

    def test_globally_incompatible_samples_are_unavailable(self):
        # Twelve components that each hold 1 or 2 mines; with 36 mines on 48
        # hidden cells every component must hold 2 and every free cell a mine.
        # One proposal per component finds that combination with chance 2**-12.
        width, revealed = chain_board(12)
        exact = calculate_probabilities(width, 1, 36, revealed, time_budget=SLOW)
        self.assertEqual(exact["status"], "exact")
        self.assertEqual(len(exact["proven_mines"]) + len(exact["proven_safe"]), 48)
        result = calculate_probabilities(width, 1, 36, revealed, time_budget=SLOW,
                                         node_budget=0, sample_budget=12, rng=random.Random(2))
        self.check_shape(result, width, 1, revealed)
        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["meta"]["reason"], "no_globally_compatible_samples")
        self.assertEqual(result["proven_safe"], [])
        self.assertEqual(result["proven_mines"], [])

    def test_large_connected_component_is_time_bounded(self):
        # Clues on a 2x2 lattice of an 80x80 board form one ~3000-cell component
        # that is far beyond exact counting.
        rng = random.Random(4)
        mines = set(rng.sample(range(6400), 1280))
        shown = [i for i in range(6400)
                 if i not in mines and (i // 80) % 2 == 0 and (i % 80) % 2 == 0]
        revealed = clues_for(80, 80, mines, shown)
        for budget in (0.3, 1.0):
            started = time.perf_counter()
            result = calculate_probabilities(80, 80, 1280, revealed, time_budget=budget)
            elapsed = time.perf_counter() - started
            with self.subTest(budget=budget):
                self.check_shape(result, 80, 80, revealed)
                self.assertIn(result["status"], {"approximate", "unavailable"})
                self.assertLess(elapsed, budget + 0.5)
                self.assertGreater(result["meta"]["frontier_cells"], 1000)
                self.assertFalse(set(result["proven_safe"]) & mines)
                self.assertTrue(set(result["proven_mines"]) <= mines)


if __name__ == "__main__":
    unittest.main()
