"""Certainty-only autosolve batches, API races and real solver progression."""

import random
import threading
import unittest

from minesweeper.game import Game, GameOverError, InvalidActionError, StaleRevisionError
from minesweeper.probability import calculate_probabilities
from server import ProbabilityService
from tests.test_game import COLUMN, TWO_MINES, layout_game
from tests.test_server import OPENED, ServerTestCase, fake_odds, untimed


class DeductionBatchTests(unittest.TestCase):
    def opened(self):
        game = layout_game(*TWO_MINES)
        game.reveal(0, 0)
        return game

    def test_flags_mines_and_corrects_safe_flags_in_one_revision(self):
        game = self.opened()
        game.toggle_flag(3, 0)
        game.toggle_flag(4, 4)  # Not among this batch's proofs; leave it alone.
        revision = game.revision
        self.assertTrue(game.apply_deductions([15, 17], [16, 18], revision))
        state = game.public_state()
        self.assertEqual(state["revision"], revision + 1)
        self.assertEqual(state["status"], "playing")
        for index in (15, 17):
            self.assertTrue(state["cells"][index]["revealed"])
            self.assertFalse(state["cells"][index]["flagged"])
        for index in (16, 18, 24):
            self.assertTrue(state["cells"][index]["flagged"])
        self.assertTrue(all(cell["mine"] is None for cell in state["cells"]))

    def test_all_safe_flags_are_removed_before_zero_flood(self):
        game = layout_game(*COLUMN)
        game.reveal(2, 0)
        game.toggle_flag(0, 5)
        game.toggle_flag(0, 6)
        revision = game.revision
        self.assertTrue(game.apply_deductions([5, 6, 12, 13], [], revision))
        state = game.public_state()
        self.assertEqual(state["revision"], revision + 1)
        self.assertEqual(state["flags"], 0)
        self.assertTrue(all(state["cells"][i]["revealed"] for i in (5, 6, 12, 13)))
        self.assertEqual(state["status"], "playing")

    def test_empty_or_already_flagged_deductions_are_noops(self):
        game = self.opened()
        game.toggle_flag(3, 1)
        revision = game.revision
        self.assertFalse(game.apply_deductions([], [], revision))
        self.assertFalse(game.apply_deductions([], [16, 16], revision))
        self.assertEqual(game.revision, revision)
        self.assertEqual(game.flags, 1)

    def test_batch_can_win_without_revealing_mines(self):
        game = self.opened()
        safe = [i for i in range(15, 25) if i not in (16, 18)]
        self.assertTrue(game.apply_deductions(safe, [16, 18], 1))
        state = game.public_state()
        self.assertEqual((state["status"], state["revision"], state["flags"]), ("won", 2, 2))
        self.assertEqual(sum(cell["revealed"] for cell in state["cells"]), 23)
        self.assertFalse(any(cell["exploded"] for cell in state["cells"]))

    def test_stale_and_invalid_deductions_never_partially_apply(self):
        game = self.opened()
        before = untimed(game.public_state())
        with self.assertRaises(StaleRevisionError):
            game.apply_deductions([15], [16], 0)
        for safe, mines in [([15, -1], [16]), ([15], [25]), ([True], [16]),
                            ([15], [15]), ([0], [16])]:
            with self.subTest(safe=safe, mines=mines):
                with self.assertRaises(InvalidActionError):
                    game.apply_deductions(safe, mines, 1)
                self.assertEqual(untimed(game.public_state()), before)
        for revision in (True, 1.0, "1", -1):
            with self.assertRaises(InvalidActionError):
                game.apply_deductions([15], [16], revision)
        self.assertEqual(untimed(game.public_state()), before)

    def test_first_move_and_terminal_state_stay_player_controlled(self):
        game = layout_game(*TWO_MINES)
        with self.assertRaises(InvalidActionError) as error:
            game.apply_deductions([], [], 0)
        self.assertEqual(error.exception.code, "game_not_started")
        self.assertEqual((game.status, game.revision), ("ready", 0))
        game.reveal(0, 0)
        game.reveal(3, 1)
        with self.assertRaises(GameOverError):
            game.apply_deductions([], [], game.revision)


class AutosolveApiTests(ServerTestCase):
    layout = [16, 18]

    def opened(self):
        state = self.create()
        return self.play(state["id"], "reveal", 0, 0, 0)

    def step(self, state):
        return self.request("POST", f"/api/games/{state['id']}/autosolve",
                            {"revision": state["revision"]})

    def proof_result(self, status="exact"):
        result = fake_odds(5, 5, 2, OPENED)
        result["status"] = status
        if status == "unavailable":
            result["probabilities"] = [None] * 25
        result["probabilities"][15] = 0.0
        result["probabilities"][16] = 1.0
        result["proven_safe"] = [15]
        result["proven_mines"] = [16]
        return result

    def test_applies_only_proven_moves_for_every_solver_status(self):
        for status in ("exact", "approximate", "unavailable"):
            with self.subTest(status=status):
                before = self.opened()
                self.solver.result = self.proof_result(status)
                response = self.step(before)
                self.assertEqual(response.status, 200, response.body)
                self.assert_json_headers(response)
                after = response.json()
                self.assert_state_shape(after)
                self.assertEqual(after["revision"], before["revision"] + 1)
                self.assertTrue(after["cells"][15]["revealed"])
                self.assertTrue(after["cells"][16]["flagged"])
                self.assertFalse(after["cells"][17]["revealed"])
                self.assertFalse(after["cells"][18]["flagged"])
                self.assertEqual(after["status"], "playing")

    def test_sampled_endpoints_are_not_automatic_moves(self):
        before = self.opened()
        result = fake_odds(5, 5, 2, OPENED)
        result["status"] = "approximate"
        result["probabilities"][16] = 0.0  # A sampled zero on an actual mine.
        result["probabilities"][15] = 1.0  # A sampled one on a safe cell.
        self.solver.result = result
        response = self.step(before)
        self.assertEqual(response.status, 200)
        self.assertEqual(untimed(response.json()), untimed(before))

    def test_reuses_odds_and_invalidates_cache_after_the_batch(self):
        before = self.opened()
        self.solver.result = self.proof_result()
        self.assertEqual(self.odds(before["id"], before["revision"]).status, 200)
        after = self.step(before).json()
        self.assertEqual(len(self.solver.calls), 1)
        clues = {i: cell["adjacent"] for i, cell in enumerate(after["cells"]) if cell["revealed"]}
        self.solver.result = fake_odds(5, 5, 2, clues)
        self.assertEqual(self.odds(after["id"], after["revision"]).status, 200)
        self.assertEqual(len(self.solver.calls), 2)
        self.assertEqual(self.solver.calls[-1]["args"], (5, 5, 2, clues))

    def test_duplicate_or_stale_batches_do_not_toggle_flags_back(self):
        before = self.opened()
        self.solver.result = self.proof_result()
        after = self.step(before).json()
        error = self.assert_error(self.step(before), 409, "stale_revision")
        self.assertEqual(untimed(error["state"]), untimed(after))
        self.assertEqual(error["state"]["flags"], 1)
        self.assertEqual(len(self.solver.calls), 1)

    def test_move_during_calculation_invalidates_the_whole_batch(self):
        before = self.opened()
        self.solver.result = self.proof_result()
        self.solver.gate = threading.Event()
        responses = []
        worker = threading.Thread(target=lambda: responses.append(self.step(before)))
        worker.start()
        try:
            self.assertTrue(self.solver.started.wait(5))
            latest = self.play(before["id"], "flag", 4, 4, before["revision"])
        finally:
            self.solver.gate.set()
            worker.join(10)
        self.assertFalse(worker.is_alive())
        error = self.assert_error(responses[0], 409, "stale_revision")
        self.assertEqual(untimed(error["state"]), untimed(latest))
        self.assertFalse(error["state"]["cells"][15]["revealed"])
        self.assertFalse(error["state"]["cells"][16]["flagged"])

    def test_revision_only_body_rejects_client_supplied_proofs(self):
        before = self.opened()
        path = f"/api/games/{before['id']}/autosolve"
        for revision in (True, None, -1, 1.5, "1"):
            self.assert_error(self.request("POST", path, {"revision": revision}), 400, "invalid_revision")
        self.assert_error(self.request("POST", path, {}), 400, "missing_fields")
        self.assert_error(self.request("POST", path, {"revision": 1, "proven_safe": [16]}),
                          400, "unknown_fields")
        self.assert_error(self.request("GET", path), 405, "method_not_allowed")
        self.assert_error(self.request("POST", path + "?revision=1", {"revision": 1}),
                          400, "unexpected_query")
        self.assertEqual(self.solver.calls, [])
        self.assertEqual(untimed(self.store.get(before["id"]).state()), untimed(before))

    def test_ready_and_finished_games_do_not_run_the_solver(self):
        state = self.create()
        self.assert_error(self.step(state), 400, "game_not_started")
        state = self.play(state["id"], "reveal", 0, 0, 0)
        state = self.play(state["id"], "reveal", 3, 1, state["revision"])
        error = self.assert_error(self.step(state), 409, "game_over")
        self.assertEqual(untimed(error["state"]), untimed(state))
        self.assertEqual(self.solver.calls, [])

    def test_solver_failure_is_reported_without_playing(self):
        before = self.opened()
        self.solver.error = RuntimeError("test failure")
        with self.assertLogs("minesweeper.server", "ERROR"):
            self.assert_error(self.step(before), 500, "probability_failed")
        self.assertEqual(untimed(self.store.get(before["id"]).state()), untimed(before))


class RealAutosolveApiTests(ServerTestCase):
    layout = [16, 18]

    def make_probability_service(self):
        return ProbabilityService()

    def test_real_solver_corrects_wrong_flag_and_wins(self):
        state = self.create()
        state = self.play(state["id"], "reveal", 0, 0, 0)
        state = self.play(state["id"], "flag", 3, 0, state["revision"])
        response = self.request("POST", f"/api/games/{state['id']}/autosolve",
                                {"revision": state["revision"]})
        self.assertEqual(response.status, 200, response.body)
        after = response.json()
        self.assertEqual(after["status"], "won")
        self.assertEqual(after["revision"], state["revision"] + 1)
        self.assertTrue(after["cells"][15]["revealed"])
        self.assertFalse(after["cells"][15]["flagged"])
        self.assertEqual([i for i, cell in enumerate(after["cells"]) if cell["flagged"]], [16, 18])


class RealAutosolveProgressionTests(unittest.TestCase):
    def exhaust_certain_moves(self, game):
        for _ in range(100):
            if game.is_over:
                self.assertEqual(game.status, "won")
                return None
            odds = calculate_probabilities(game.width, game.height, game.mines, game.revealed_clues())
            safe = odds["proven_safe"]
            mines = odds["proven_mines"]
            self.assertFalse(any(game._is_mine[i] for i in safe))
            self.assertTrue(all(game._is_mine[i] for i in mines))
            if not game.apply_deductions(safe, mines, game.revision):
                return odds
        self.fail("The fixture did not reach a guessing position within 100 batches")

    def test_repeated_batches_pause_and_resume_after_surviving_manual_move(self):
        resumed = 0
        for seed in range(12):
            game = Game(9, 9, 10, rng=random.Random(seed))
            game.reveal(4, 4)
            odds = self.exhaust_certain_moves(game)
            if odds is None:
                continue
            self.assertEqual(odds["status"], "exact")
            hidden = [i for i in range(game.size) if not game._revealed[i] and not game._flagged[i]]
            self.assertTrue(hidden)
            self.assertTrue(all(0 < odds["probabilities"][i] < 1 for i in hidden))
            # Choose a surviving guess for this regression only; autosolve never
            # receives or inspects this private layout.
            chosen = next(i for i in hidden if not game._is_mine[i])
            game.reveal(*divmod(chosen, game.width))
            self.exhaust_certain_moves(game)
            resumed += 1
        self.assertGreater(resumed, 0)

    def test_largest_board_batches_never_guess(self):
        game = Game(80, 80, 1280, rng=random.Random(19))
        game.reveal(40, 40)
        self.exhaust_certain_moves(game)
        self.assertNotEqual(game.status, "lost")


if __name__ == "__main__":
    unittest.main()
