"""Tests for the authoritative Minesweeper engine and the in-memory game store."""

import json
import random
import threading
import unittest

from minesweeper.game import (
    ACTIONS,
    Game,
    GameNotFoundError,
    GameOverError,
    GameStore,
    InvalidActionError,
    InvalidConfigError,
    StaleRevisionError,
    max_mines,
)

STATE_KEYS = {"id", "width", "height", "mines", "status", "revision", "flags", "elapsed_seconds", "cells"}
CELL_KEYS = {"revealed", "flagged", "adjacent", "mine", "exploded"}


class FixedLayout:
    """Stand-in RNG whose ``sample`` returns a predetermined mine layout.

    It refuses layouts that intrude on the protected first-reveal area, so a
    test can never accidentally bypass the first-click guarantee.
    """

    def __init__(self, mine_indices):
        self.mine_indices = list(mine_indices)
        self.calls = 0

    def sample(self, population, k):
        self.calls += 1
        if k != len(self.mine_indices):
            raise AssertionError(f"engine asked for {k} mines, layout has {len(self.mine_indices)}")
        blocked = set(self.mine_indices) - set(population)
        if blocked:
            raise AssertionError(f"layout puts mines in the protected start area: {sorted(blocked)}")
        return list(self.mine_indices)


class FakeClock:
    def __init__(self, now=1000.0):
        self.now = now

    def __call__(self):
        return self.now

    def advance(self, seconds):
        self.now += seconds


def layout_game(width, height, mine_cells, **kwargs):
    """Game whose mines land exactly on ``mine_cells`` (row, col) at the first reveal."""
    indices = [row * width + col for row, col in mine_cells]
    return Game(width, height, len(indices), rng=FixedLayout(indices), **kwargs)


def render(state):
    """Player's view: '#' hidden, 'F' flagged, 'X' exploded, digits for revealed clues."""
    rows = []
    width = state["width"]
    for r in range(state["height"]):
        line = ""
        for cell in state["cells"][r * width:(r + 1) * width]:
            if cell["exploded"]:
                line += "X"
            elif cell["revealed"]:
                line += str(cell["adjacent"])
            elif cell["flagged"]:
                line += "F"
            else:
                line += "#"
        rows.append(line)
    return rows


def mine_indices(game):
    # White-box access to the hidden layout, for test assertions only.
    return {i for i, flag in enumerate(game._is_mine) if flag}


def zone(game, row, col):
    return {
        r * game.width + c
        for r in range(max(row - 1, 0), min(row + 2, game.height))
        for c in range(max(col - 1, 0), min(col + 2, game.width))
    }


# Layout "two mines": after revealing (0, 0) the bottom two rows stay hidden.
#   00000
#   00000
#   11211
#   1*2*1
#   11211
TWO_MINES = (5, 5, [(3, 1), (3, 3)])

# Layout "column": the right half is only reachable through chords.
#   001*100
#   0022200
#   001*100
#   0022200
#   001*100
COLUMN = (7, 5, [(0, 3), (2, 3), (4, 3)])


class ConfigValidationTests(unittest.TestCase):
    def test_accepts_boundary_configurations(self):
        for width, height, mines in [(5, 5, 1), (5, 5, 16), (80, 80, 6391), (5, 80, 391), (80, 5, 391), (9, 9, 10)]:
            game = Game(width, height, mines)
            self.assertEqual((game.width, game.height, game.mines), (width, height, mines))
            self.assertEqual(game.status, "ready")

    def test_max_mines_leaves_a_safe_three_by_three(self):
        self.assertEqual(max_mines(9, 9), 72)
        self.assertEqual(max_mines(5, 5), 16)
        self.assertEqual(max_mines(80, 80), 6391)

    def test_rejects_invalid_dimensions(self):
        for bad in [4, 81, 0, -9, True, False, 9.0, "9", None, [9], 10 ** 20]:
            with self.subTest(width=bad):
                with self.assertRaises(InvalidConfigError) as ctx:
                    Game(bad, 9, 10)
                self.assertEqual(ctx.exception.code, "invalid_width")
            with self.subTest(height=bad):
                with self.assertRaises(InvalidConfigError) as ctx:
                    Game(9, bad, 10)
                self.assertEqual(ctx.exception.code, "invalid_height")

    def test_rejects_invalid_mine_counts(self):
        for bad in [0, -1, 73, 81, True, False, 10.0, "10", None]:
            with self.subTest(mines=bad):
                with self.assertRaises(InvalidConfigError) as ctx:
                    Game(9, 9, bad)
                self.assertEqual(ctx.exception.code, "invalid_mines")
                self.assertIn("72", ctx.exception.message)


class FirstRevealTests(unittest.TestCase):
    def test_no_mines_exist_until_first_reveal(self):
        game = Game(9, 9, 10, rng=random.Random(1))
        game.toggle_flag(0, 0)
        game.chord(4, 4)
        self.assertEqual(game.status, "ready")
        self.assertEqual(mine_indices(game), set())
        game.reveal(4, 4)
        self.assertEqual(game.status, "playing")
        self.assertEqual(len(mine_indices(game)), 10)

    def test_first_reveal_and_neighbors_are_safe_everywhere(self):
        configs = [(9, 9, 72), (9, 9, 60), (16, 16, 40), (30, 16, 99), (5, 5, 16)]
        for width, height, mines in configs:
            spots = {
                (0, 0), (0, width - 1), (height - 1, 0), (height - 1, width - 1),
                (0, width // 2), (height // 2, 0), (height // 2, width // 2), (1, 1),
            }
            for row, col in sorted(spots):
                for seed in range(15):
                    with self.subTest(board=(width, height, mines), cell=(row, col), seed=seed):
                        game = Game(width, height, mines, rng=random.Random(seed))
                        game.reveal(row, col)
                        placed = mine_indices(game)
                        self.assertEqual(len(placed), mines)
                        self.assertFalse(placed & zone(game, row, col))
                        self.assertIn(game.status, ("playing", "won"))
                        first = game.public_state()["cells"][row * width + col]
                        self.assertEqual(first["adjacent"], 0)

    def test_maximum_density_interior_start_wins_immediately(self):
        for width, height in [(5, 5), (9, 9), (80, 80)]:
            with self.subTest(size=(width, height)):
                mines = max_mines(width, height)
                game = Game(width, height, mines, rng=random.Random(7))
                game.reveal(height // 2, width // 2)
                self.assertEqual(game.status, "won")
                self.assertEqual(mine_indices(game), set(range(width * height)) - zone(game, height // 2, width // 2))
                self.assertEqual(game.flags, mines)

    def test_maximum_density_corner_start_places_exact_count(self):
        for seed in range(25):
            game = Game(5, 5, 16, rng=random.Random(seed))
            game.reveal(4, 0)
            self.assertEqual(len(mine_indices(game)), 16)
            self.assertFalse(mine_indices(game) & zone(game, 4, 0))

    def test_mine_placement_is_uniform_outside_the_start_area(self):
        rng = random.Random(20240601)
        counts = [0] * 25
        trials = 4200
        for _ in range(trials):
            game = Game(5, 5, 1, rng=rng)
            game.reveal(0, 0)
            (index,) = mine_indices(game)
            counts[index] += 1
        excluded = zone(Game(5, 5, 1), 0, 0)
        self.assertTrue(all(counts[i] == 0 for i in excluded))
        expected = trials / (25 - len(excluded))
        chi_squared = sum((counts[i] - expected) ** 2 / expected for i in range(25) if i not in excluded)
        # 20 degrees of freedom: the 99.9th percentile is about 45.3.
        self.assertLess(chi_squared, 45.3)

    def test_revealing_a_flagged_cell_does_not_start_the_game(self):
        game = Game(9, 9, 10, rng=random.Random(3))
        game.toggle_flag(4, 4)
        self.assertFalse(game.reveal(4, 4))
        self.assertEqual(game.status, "ready")
        self.assertEqual(game.revision, 1)
        self.assertEqual(mine_indices(game), set())

    def test_flagged_cells_in_start_area_stay_hidden(self):
        game = layout_game(*TWO_MINES)
        game.toggle_flag(0, 1)
        game.reveal(0, 0)
        self.assertEqual(render(game.public_state())[:3], ["0F000", "00000", "11211"])

    def test_invalid_random_sample_is_rejected_before_mutation(self):
        class Broken:
            def sample(self, population, k):
                return [0] * k

        game = Game(9, 9, 10, rng=Broken())
        before = game.public_state()
        with self.assertRaises(RuntimeError):
            game.reveal(0, 0)
        self.assertEqual(game.public_state(), before)
        self.assertEqual(mine_indices(game), set())


class FloodTests(unittest.TestCase):
    def test_flood_reveals_zero_region_and_its_border(self):
        game = layout_game(*TWO_MINES)
        self.assertTrue(game.reveal(0, 0))
        self.assertEqual(render(game.public_state()), ["00000", "00000", "11211", "#####", "#####"])
        self.assertEqual(game.status, "playing")

    def test_flood_stops_at_numbers_on_other_side_of_wall(self):
        game = layout_game(*COLUMN)
        game.reveal(2, 0)
        self.assertEqual(
            render(game.public_state()),
            ["001####", "002####", "001####", "002####", "001####"],
        )

    def test_revealing_a_number_reveals_only_that_cell(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        game.reveal(3, 0)
        self.assertEqual(render(game.public_state())[3:], ["1####", "#####"])

    def test_flood_skips_flagged_cells(self):
        game = layout_game(*COLUMN)
        game.toggle_flag(0, 0)
        game.toggle_flag(4, 1)
        game.reveal(2, 0)
        self.assertEqual(
            render(game.public_state()),
            ["F01####", "002####", "001####", "002####", "0F1####"],
        )
        self.assertEqual(game.flags, 2)

    def test_huge_flood_is_iterative(self):
        game = layout_game(80, 80, [(79, 79)])
        game.reveal(0, 0)
        self.assertEqual(game.status, "won")
        state = game.public_state()
        self.assertEqual(sum(cell["revealed"] for cell in state["cells"]), 80 * 80 - 1)


class FlagTests(unittest.TestCase):
    def test_toggle_flag_counts_and_revisions(self):
        game = Game(9, 9, 10, rng=random.Random(5))
        self.assertTrue(game.toggle_flag(2, 3))
        self.assertEqual((game.flags, game.revision), (1, 1))
        self.assertTrue(game.public_state()["cells"][2 * 9 + 3]["flagged"])
        self.assertTrue(game.toggle_flag(2, 3))
        self.assertEqual((game.flags, game.revision), (0, 2))
        self.assertFalse(game.public_state()["cells"][2 * 9 + 3]["flagged"])

    def test_flags_may_exceed_mine_count(self):
        game = Game(9, 9, 10, rng=random.Random(5))
        for col in range(9):
            game.toggle_flag(0, col)
            game.toggle_flag(8, col)
        self.assertEqual(game.flags, 18)
        self.assertEqual(game.status, "ready")

    def test_flagging_revealed_cell_is_a_noop(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        revision = game.revision
        self.assertFalse(game.toggle_flag(1, 1))
        self.assertEqual((game.flags, game.revision), (0, revision))

    def test_flagged_cell_can_be_revealed_after_unflagging(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        game.toggle_flag(3, 0)
        self.assertFalse(game.reveal(3, 0))
        game.toggle_flag(3, 0)
        self.assertTrue(game.reveal(3, 0))
        self.assertEqual(render(game.public_state())[3], "1####")

    def test_flags_before_start_do_not_start_timer(self):
        clock = FakeClock()
        game = Game(9, 9, 10, rng=random.Random(5), clock=clock)
        game.toggle_flag(0, 0)
        clock.advance(30)
        self.assertEqual(game.elapsed_seconds, 0.0)


class ChordTests(unittest.TestCase):
    def test_chord_reveals_unflagged_neighbors_when_flags_match(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        game.toggle_flag(3, 1)
        self.assertTrue(game.chord(2, 0))
        self.assertEqual(render(game.public_state())[3:], ["1F###", "#####"])

    def test_chord_is_noop_when_flag_count_differs(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        game.toggle_flag(3, 1)
        for extra_flags in ([], [(3, 3), (3, 2)]):  # one flag, then three, around a 2
            for row, col in extra_flags:
                game.toggle_flag(row, col)
            before = game.public_state()
            self.assertFalse(game.chord(2, 2))
            self.assertEqual(game.public_state(), before)

    def test_chord_on_hidden_or_unstarted_cells_is_a_noop(self):
        game = layout_game(*TWO_MINES)
        self.assertFalse(game.chord(0, 0))
        self.assertEqual((game.status, game.revision), ("ready", 0))
        game.reveal(0, 0)
        self.assertFalse(game.chord(4, 4))
        self.assertFalse(game.chord(1, 1))  # zero with nothing left to reveal
        self.assertEqual(game.revision, 1)

    def test_chords_can_win_the_game(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        game.toggle_flag(3, 1)
        game.toggle_flag(3, 3)
        for row, col in [(2, 0), (2, 2), (2, 4), (3, 0), (3, 2)]:
            self.assertTrue(game.chord(row, col))
            self.assertEqual(game.status, "playing")
        self.assertTrue(game.chord(3, 4))
        self.assertEqual(game.status, "won")
        self.assertEqual(render(game.public_state()), ["00000", "00000", "11211", "1F2F1", "11211"])

    def test_chord_opening_a_zero_floods(self):
        game = layout_game(*COLUMN)
        game.reveal(2, 0)
        game.toggle_flag(0, 3)
        game.toggle_flag(2, 3)
        self.assertTrue(game.chord(1, 2))
        self.assertTrue(game.chord(1, 3))
        self.assertTrue(game.chord(1, 4))
        self.assertEqual(
            render(game.public_state()),
            ["001F100", "0022200", "001F100", "002#200", "001#100"],
        )
        game.toggle_flag(4, 3)
        self.assertTrue(game.chord(3, 2))
        self.assertEqual(game.status, "won")

    def test_chord_with_wrong_flag_loses(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        game.toggle_flag(3, 0)  # wrong: (3, 0) is safe, the mine is at (3, 1)
        self.assertTrue(game.chord(2, 0))
        self.assertEqual(game.status, "lost")
        state = game.public_state()
        self.assertEqual([i for i, c in enumerate(state["cells"]) if c["exploded"]], [3 * 5 + 1])
        wrong_flag = state["cells"][3 * 5 + 0]
        self.assertTrue(wrong_flag["flagged"])
        self.assertIs(wrong_flag["mine"], False)

    def test_chord_detonating_several_mines_marks_each(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        game.toggle_flag(3, 1)
        game.toggle_flag(3, 3)
        game.chord(2, 2)  # reveals (3, 2), a 2
        game.toggle_flag(3, 1)
        game.toggle_flag(3, 3)
        game.toggle_flag(4, 1)
        game.toggle_flag(4, 2)
        self.assertTrue(game.chord(3, 2))
        self.assertEqual(game.status, "lost")
        state = game.public_state()
        exploded = {i for i, c in enumerate(state["cells"]) if c["exploded"]}
        self.assertEqual(exploded, {3 * 5 + 1, 3 * 5 + 3})


class TerminalStateTests(unittest.TestCase):
    def test_revealing_a_mine_loses(self):
        clock = FakeClock()
        game = layout_game(*TWO_MINES, clock=clock)
        game.reveal(0, 0)
        clock.advance(4)
        self.assertTrue(game.reveal(3, 1))
        self.assertEqual((game.status, game.revision), ("lost", 2))
        state = game.public_state()
        cell = state["cells"][3 * 5 + 1]
        self.assertEqual(cell, {"revealed": True, "flagged": False, "adjacent": None, "mine": True, "exploded": True})
        self.assertEqual(sum(c["exploded"] for c in state["cells"]), 1)
        self.assertTrue(all(isinstance(c["mine"], bool) for c in state["cells"]))
        self.assertEqual(sum(c["mine"] for c in state["cells"]), 2)
        self.assertIsNone(state["cells"][4 * 5]["adjacent"])  # unrevealed clues stay private

    def test_actions_after_game_over_are_rejected_without_change(self):
        for finish in ("lose", "win"):
            with self.subTest(finish=finish):
                game = layout_game(*TWO_MINES)
                game.reveal(0, 0)
                if finish == "lose":
                    game.reveal(3, 1)
                else:
                    for row, col in [(3, 0), (3, 2), (3, 4), (4, 0), (4, 1), (4, 2), (4, 3), (4, 4)]:
                        game.reveal(row, col)
                self.assertTrue(game.is_over)
                before = game.public_state()
                for action in ACTIONS:
                    with self.assertRaises(GameOverError) as ctx:
                        game.apply_action(action, 4, 4, game.revision)
                    self.assertEqual(ctx.exception.code, "game_over")
                self.assertEqual(game.public_state(), before)

    def test_winning_requires_only_safe_reveals_and_flags_every_mine(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        for row, col in [(3, 0), (3, 2), (3, 4), (4, 0), (4, 1), (4, 2), (4, 3)]:
            game.reveal(row, col)
            self.assertEqual(game.status, "playing")
        game.reveal(4, 4)
        self.assertEqual(game.status, "won")
        self.assertEqual(game.flags, 2)
        state = game.public_state()
        self.assertEqual(render(state)[3], "1F2F1")
        self.assertFalse(any(c["exploded"] for c in state["cells"]))
        self.assertEqual({i for i, c in enumerate(state["cells"]) if c["mine"]}, {16, 18})


class TimerTests(unittest.TestCase):
    def test_timer_starts_on_first_reveal_and_freezes_at_the_end(self):
        clock = FakeClock(500.0)
        game = layout_game(*TWO_MINES, clock=clock)
        clock.advance(10)
        self.assertEqual(game.elapsed_seconds, 0.0)
        game.reveal(0, 0)
        clock.advance(2.25)
        self.assertEqual(game.elapsed_seconds, 2.25)
        self.assertEqual(game.public_state()["elapsed_seconds"], 2.25)
        clock.advance(1.5)
        game.reveal(3, 1)
        clock.advance(100)
        self.assertEqual(game.elapsed_seconds, 3.75)

    def test_timer_freezes_on_win(self):
        clock = FakeClock()
        game = Game(5, 5, 16, rng=random.Random(1), clock=clock)
        game.reveal(2, 2)
        clock.advance(60)
        self.assertEqual((game.status, game.elapsed_seconds), ("won", 0.0))


class RevisionAndValidationTests(unittest.TestCase):
    def test_each_state_change_increments_revision_once(self):
        game = layout_game(*TWO_MINES)
        self.assertEqual(game.revision, 0)
        game.reveal(0, 0)
        self.assertEqual(game.revision, 1)
        game.toggle_flag(3, 1)
        self.assertEqual(game.revision, 2)
        game.chord(2, 0)
        self.assertEqual(game.revision, 3)

    def test_noops_do_not_change_revision(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        game.toggle_flag(3, 1)
        before = game.public_state()
        noops = [("reveal", 1, 1), ("reveal", 3, 1), ("flag", 0, 0), ("chord", 4, 4), ("chord", 2, 2), ("chord", 0, 0)]
        for action, row, col in noops:
            with self.subTest(action=action, cell=(row, col)):
                self.assertFalse(game.apply_action(action, row, col, before["revision"]))
                self.assertEqual(game.public_state(), before)

    def test_stale_revision_is_rejected_without_mutation(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        before = game.public_state()
        for expected in (0, 2, 99):
            with self.subTest(expected=expected):
                with self.assertRaises(StaleRevisionError) as ctx:
                    game.apply_action("reveal", 4, 4, expected)
                self.assertEqual((ctx.exception.expected, ctx.exception.current), (expected, 1))
                self.assertEqual(ctx.exception.code, "stale_revision")
                self.assertEqual(game.public_state(), before)

    def test_invalid_input_is_rejected_without_mutation(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        before = game.public_state()
        cases = [
            (("explode", 1, 1, 1), "invalid_action"),
            (("REVEAL", 1, 1, 1), "invalid_action"),
            ((None, 1, 1, 1), "invalid_action"),
            ((["reveal"], 1, 1, 1), "invalid_action"),
            (("reveal", True, 1, 1), "invalid_coordinates"),
            (("reveal", 1, False, 1), "invalid_coordinates"),
            (("reveal", 1.0, 1, 1), "invalid_coordinates"),
            (("reveal", "1", 1, 1), "invalid_coordinates"),
            (("reveal", None, 1, 1), "invalid_coordinates"),
            (("reveal", -1, 1, 1), "out_of_bounds"),
            (("reveal", 5, 1, 1), "out_of_bounds"),
            (("flag", 1, 5, 1), "out_of_bounds"),
            (("chord", 1, -1, 1), "out_of_bounds"),
            (("reveal", 4, 4, True), "invalid_revision"),
            (("reveal", 4, 4, -1), "invalid_revision"),
            (("reveal", 4, 4, "1"), "invalid_revision"),
            (("reveal", 4, 4, None), "invalid_revision"),
            (("reveal", 4, 4, 1.0), "invalid_revision"),
        ]
        for args, code in cases:
            with self.subTest(args=args):
                with self.assertRaises(InvalidActionError) as ctx:
                    game.apply_action(*args)
                self.assertEqual(ctx.exception.code, code)
                self.assertTrue(ctx.exception.message)
                self.assertEqual(game.public_state(), before)

    def test_invalid_input_before_start_does_not_place_mines(self):
        game = Game(9, 9, 10, rng=random.Random(2))
        with self.assertRaises(InvalidActionError):
            game.apply_action("reveal", 9, 0, 0)
        with self.assertRaises(StaleRevisionError):
            game.apply_action("reveal", 0, 0, 1)
        self.assertEqual((game.status, game.revision, mine_indices(game)), ("ready", 0, set()))


class SerializationTests(unittest.TestCase):
    def assert_no_hidden_information(self, state):
        for cell in state["cells"]:
            self.assertEqual(set(cell), CELL_KEYS)
            self.assertIsNone(cell["mine"])
            self.assertIs(cell["exploded"], False)
            if cell["revealed"]:
                self.assertIsInstance(cell["adjacent"], int)
                self.assertFalse(cell["flagged"])
            else:
                self.assertIsNone(cell["adjacent"])

    def test_state_schema(self):
        game = Game(7, 5, 6, game_id="abc", rng=random.Random(4))
        state = game.public_state()
        self.assertEqual(set(state), STATE_KEYS)
        self.assertEqual(
            {k: state[k] for k in STATE_KEYS - {"cells"}},
            {"id": "abc", "width": 7, "height": 5, "mines": 6, "status": "ready",
             "revision": 0, "flags": 0, "elapsed_seconds": 0.0},
        )
        self.assertEqual(len(state["cells"]), 35)
        self.assertEqual(state["cells"][0], {"revealed": False, "flagged": False, "adjacent": None, "mine": None, "exploded": False})
        json.dumps(state, allow_nan=False)

    def test_no_hidden_information_before_game_ends(self):
        for seed in range(10):
            game = Game(12, 10, 25, rng=random.Random(seed))
            self.assert_no_hidden_information(game.public_state())
            game.reveal(5, 5)
            game.toggle_flag(0, 0)
            if game.status == "playing":
                self.assert_no_hidden_information(game.public_state())

    def test_cells_are_row_major(self):
        game = layout_game(*COLUMN)
        game.reveal(2, 0)
        cells = game.public_state()["cells"]
        self.assertEqual(cells[1 * 7 + 2]["adjacent"], 2)  # row 1, col 2
        self.assertIsNone(cells[2 * 7 + 3]["adjacent"])  # the hidden mine at row 2, col 3

    def test_revealed_clues_contains_only_public_safe_cells(self):
        game = layout_game(*TWO_MINES)
        self.assertEqual(game.revealed_clues(), {})
        game.reveal(0, 0)
        game.toggle_flag(3, 1)
        clues = game.revealed_clues()
        expected = {i: c["adjacent"] for i, c in enumerate(game.public_state()["cells"]) if c["revealed"]}
        self.assertEqual(clues, expected)
        self.assertEqual(len(clues), 15)
        clues.clear()
        self.assertEqual(len(game.revealed_clues()), 15)

    def test_repr_does_not_leak_layout(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        self.assertEqual(repr(game), f"<Game {game.id} 5x5 mines=2 status=playing revision=1>")


class GameStoreTests(unittest.TestCase):
    def test_create_and_get(self):
        store = GameStore(rng_factory=lambda: random.Random(9))
        entry = store.create(9, 9, 10)
        self.assertIs(store.get(entry.game_id), entry)
        self.assertEqual(entry.state()["status"], "ready")
        self.assertRegex(entry.game_id, r"^[A-Za-z0-9_-]{20,}$")
        self.assertNotEqual(store.create(9, 9, 10).game_id, entry.game_id)
        self.assertEqual(len(store), 2)

    def test_unknown_or_malformed_ids_are_not_found(self):
        store = GameStore()
        store.create(9, 9, 10)
        for bad in ["missing", "", "a" * 65, "../etc", "abc.def", None, 42]:
            with self.subTest(game_id=bad):
                with self.assertRaises(GameNotFoundError) as ctx:
                    store.get(bad)
                self.assertEqual(ctx.exception.code, "game_not_found")

    def test_invalid_config_is_not_stored(self):
        store = GameStore()
        with self.assertRaises(InvalidConfigError):
            store.create(9, 9, 0)
        self.assertEqual(len(store), 0)

    def test_full_store_evicts_least_recently_used(self):
        store = GameStore(max_games=2)
        first = store.create(9, 9, 10)
        second = store.create(9, 9, 10)
        store.get(first.game_id)
        third = store.create(9, 9, 10)
        self.assertEqual(len(store), 2)
        store.get(first.game_id)
        store.get(third.game_id)
        with self.assertRaises(GameNotFoundError):
            store.get(second.game_id)

    def test_idle_games_expire(self):
        clock = FakeClock(0.0)
        store = GameStore(idle_ttl=10, clock=clock)
        entry = store.create(9, 9, 10)
        clock.advance(5)
        store.get(entry.game_id)
        clock.advance(9.5)
        store.get(entry.game_id)
        clock.advance(10)
        with self.assertRaises(GameNotFoundError):
            store.get(entry.game_id)
        self.assertEqual(len(store), 0)

    def test_rejects_nonsensical_limits(self):
        for kwargs in ({"max_games": 0}, {"max_games": True}, {"idle_ttl": 0}):
            with self.subTest(**kwargs):
                with self.assertRaises(ValueError):
                    GameStore(**kwargs)

    def test_conflict_errors_carry_latest_state(self):
        store = GameStore(rng_factory=lambda: FixedLayout([16, 18]))
        entry = store.create(5, 5, 2)
        entry.act("reveal", 0, 0, 0)
        with self.assertRaises(StaleRevisionError) as ctx:
            entry.act("reveal", 4, 4, 0)
        self.assertEqual(ctx.exception.state, entry.state())
        entry.act("reveal", 3, 1, 1)
        with self.assertRaises(GameOverError) as ctx:
            entry.act("reveal", 4, 4, 2)
        self.assertEqual(ctx.exception.state["status"], "lost")

    def test_concurrent_actions_on_one_revision_apply_exactly_once(self):
        store = GameStore()
        entry = store.create(9, 9, 10)
        barrier = threading.Barrier(16)
        outcomes = []

        def flag(col):
            barrier.wait()
            try:
                entry.act("flag", col // 9, col % 9, 0)
                outcomes.append("ok")
            except StaleRevisionError:
                outcomes.append("stale")

        threads = [threading.Thread(target=flag, args=(i,)) for i in range(16)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(5)
        self.assertEqual(sorted(outcomes), ["ok"] + ["stale"] * 15)
        state = entry.state()
        self.assertEqual((state["revision"], state["flags"]), (1, 1))

    def test_snapshot_and_probability_cache_follow_revisions(self):
        store = GameStore(rng_factory=lambda: FixedLayout([16, 18]))
        entry = store.create(5, 5, 2)
        snapshot = entry.snapshot(include_observation=True)
        self.assertEqual((snapshot.status, snapshot.revision, snapshot.revealed), ("ready", 0, None))
        entry.act("reveal", 0, 0, 0)
        snapshot = entry.snapshot(include_observation=True)
        self.assertEqual(len(snapshot.revealed), 15)
        self.assertIsNone(entry.snapshot().revealed)
        entry.cache_probabilities(0, {"stale": True})
        self.assertIsNone(entry.snapshot().cached_probabilities)
        entry.cache_probabilities(1, {"fresh": True})
        self.assertEqual(entry.snapshot().cached_probabilities, {"fresh": True})
        entry.act("flag", 1, 1, 1)  # no-op on a revealed cell keeps the cache
        self.assertEqual(entry.snapshot().cached_probabilities, {"fresh": True})
        entry.act("flag", 4, 4, 1)
        self.assertIsNone(entry.snapshot().cached_probabilities)


if __name__ == "__main__":
    unittest.main()
