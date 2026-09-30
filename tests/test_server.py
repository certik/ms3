"""HTTP tests for server.py against a real server bound to an ephemeral port."""

import http.client
import json
import math
import socket
import threading
import time
import unittest
from pathlib import Path

import server
from minesweeper.game import GameStore
from server import MAX_BODY_BYTES, ProbabilityService, create_server
from tests.test_game import FixedLayout, render

# 5x5 board with mines at (3, 1) and (3, 3); see tests.test_game.TWO_MINES.
TWO_MINES = [16, 18]
# Cells revealed after the opening move (0, 0) on the TWO_MINES board: rows 0-2.
OPENED = frozenset(range(15))
STATE_KEYS = {"id", "width", "height", "mines", "status", "revision", "flags", "elapsed_seconds", "cells"}
CELL_KEYS = {"revealed", "flagged", "adjacent", "mine", "exploded"}
ODDS_KEYS = {"game_id", "revision", "status", "probabilities", "proven_safe", "proven_mines", "message", "meta"}
META_KEYS = {"frontier_cells", "components", "unconstrained_cells", "samples", "elapsed_ms", "reason"}
OPTIONAL_META_KEYS = {"exact_components", "sampled_components", "sample_attempts", "effective_sample_size"}
HIDDEN_CELL = {"revealed": False, "flagged": False, "adjacent": None, "mine": None, "exploded": False}
STATIC_FILES = {
    "index.html": b"<!doctype html><title>Minesweeper</title>",
    "app.js": b"console.log('app');",
    "styles.css": b"body { margin: 0; }",
}


class FakeInconsistentBoardError(Exception):
    pass


_DEFAULT_RESULT = object()


def untimed(state):
    """State without the live timer, which ticks between two reads of a game in play."""
    return {key: value for key, value in state.items() if key != "elapsed_seconds"}


def fake_odds(width, height, total_mines, revealed):
    """Well-formed solver output: uniform odds on hidden cells and no proofs."""
    hidden = width * height - len(revealed)
    return {
        "status": "exact",
        "probabilities": [None if i in revealed else total_mines / hidden for i in range(width * height)],
        "proven_safe": [],
        "proven_mines": [],
        "message": "Fake odds.",
        "meta": {"frontier_cells": 3, "components": 1, "unconstrained_cells": hidden - 3,
                 "samples": 0, "elapsed_ms": 0.25, "reason": None},
    }


class FakeSolver:
    """Records calls; can block on ``gate``, raise ``error`` or return ``result``."""

    def __init__(self):
        self.calls = []
        self.lock = threading.Lock()
        self.started = threading.Event()
        self.gate = None
        self.error = None
        self.result = _DEFAULT_RESULT

    def __call__(self, width, height, total_mines, revealed, **kwargs):
        with self.lock:
            self.calls.append({"args": (width, height, total_mines, dict(revealed)), "kwargs": kwargs})
        self.started.set()
        if self.gate is not None:
            self.gate.wait(10)
        if self.error is not None:
            raise self.error
        if self.result is not _DEFAULT_RESULT:
            return self.result
        return fake_odds(width, height, total_mines, revealed)


class MemoryStaticDir:
    """In-memory stand-in for the static directory (the server only uses ``/`` and ``read_bytes``)."""

    def __init__(self, files):
        self.files = dict(files)
        self.requested = []

    def __truediv__(self, name):
        self.requested.append(name)
        return _MemoryFile(self.files, name)


class _MemoryFile:
    def __init__(self, files, name):
        self.files = files
        self.name = name

    def read_bytes(self):
        if self.name not in self.files:
            raise FileNotFoundError(self.name)
        return self.files[self.name]


class ExplodingStore(GameStore):
    def get(self, game_id):
        raise RuntimeError("secret detail in /Users/someone/internal/path.py")


class Response:
    def __init__(self, status, headers, body):
        self.status = status
        self.headers = headers
        self.body = body

    def json(self):
        return json.loads(self.body.decode("utf-8"))


class ServerTestCase(unittest.TestCase):
    layout = None  # fixed mine indices for every new game, or None for random

    def setUp(self):
        self.solver = FakeSolver()
        layout = self.layout
        self.store = GameStore(rng_factory=(lambda: FixedLayout(layout)) if layout else None)
        self.static = MemoryStaticDir(STATIC_FILES)
        self.start_server(store=self.store, static_dir=self.static)
        self.addCleanup(self.release_solver)

    def make_probability_service(self):
        return ProbabilityService(self.solver, FakeInconsistentBoardError, wait_timeout=5)

    def start_server(self, **kwargs):
        kwargs.setdefault("probability_service", self.make_probability_service())
        httpd = create_server("127.0.0.1", 0, **kwargs)
        thread = threading.Thread(target=httpd.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        thread.start()

        def stop():
            httpd.shutdown()
            httpd.server_close()
            thread.join(5)

        self.addCleanup(stop)
        self.port = httpd.server_address[1]
        return httpd

    def release_solver(self):
        if self.solver.gate is not None:
            self.solver.gate.set()

    # ---------------------------------------------------------------- helpers

    def request(self, method, path, body=None, *, raw=None, headers=None, content_type="application/json"):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=15)
        try:
            data = json.dumps(body).encode("utf-8") if body is not None else raw
            all_headers = {}
            if data is not None and content_type is not None:
                all_headers["Content-Type"] = content_type
            all_headers.update(headers or {})
            conn.request(method, path, body=data, headers=all_headers)
            response = conn.getresponse()
            return Response(response.status, {k.lower(): v for k, v in response.getheaders()}, response.read())
        finally:
            conn.close()

    def raw_request(self, data, *, shutdown_write=True):
        with socket.create_connection(("127.0.0.1", self.port), timeout=15) as sock:
            sock.sendall(data)
            if shutdown_write:
                sock.shutdown(socket.SHUT_WR)
            chunks = []
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk)
        head, _, body = b"".join(chunks).partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        headers = {}
        for line in lines[1:]:
            name, _, value = line.partition(":")
            headers[name.strip().lower()] = value.strip()
        return Response(int(lines[0].split()[1]), headers, body)

    def create(self, width=5, height=5, mines=2):
        response = self.request("POST", "/api/games", {"width": width, "height": height, "mines": mines})
        self.assertEqual(response.status, 201, response.body)
        return response.json()

    def act(self, game_id, action, row, col, revision):
        body = {"action": action, "row": row, "col": col, "revision": revision}
        return self.request("POST", f"/api/games/{game_id}/actions", body)

    def play(self, game_id, action, row, col, revision):
        response = self.act(game_id, action, row, col, revision)
        self.assertEqual(response.status, 200, response.body)
        return response.json()

    def odds(self, game_id, revision):
        return self.request("GET", f"/api/games/{game_id}/probabilities?revision={revision}")

    def assert_json_headers(self, response):
        self.assertEqual(response.headers.get("content-type"), "application/json; charset=utf-8")
        self.assertEqual(response.headers.get("cache-control"), "no-store")
        self.assertEqual(response.headers.get("x-content-type-options"), "nosniff")

    def assert_error(self, response, status, code):
        self.assertEqual(response.status, status, response.body)
        self.assert_json_headers(response)
        payload = response.json()
        self.assertLessEqual(set(payload), {"error", "state"})
        self.assertEqual(set(payload["error"]), {"code", "message"})
        self.assertEqual(payload["error"]["code"], code)
        self.assertRegex(payload["error"]["code"], r"^[a-z]+(_[a-z]+)*$")
        self.assertIsInstance(payload["error"]["message"], str)
        self.assertTrue(payload["error"]["message"].strip())
        self.assertNotIn("Traceback", response.body.decode("utf-8"))
        return payload

    def assert_state_shape(self, state):
        self.assertEqual(set(state), STATE_KEYS)
        self.assertEqual(len(state["cells"]), state["width"] * state["height"])
        for cell in state["cells"]:
            self.assertEqual(set(cell), CELL_KEYS)
        terminal = state["status"] in ("won", "lost")
        for cell in state["cells"]:
            if terminal:
                self.assertIsInstance(cell["mine"], bool)
            else:
                self.assertIsNone(cell["mine"])
                self.assertIs(cell["exploded"], False)
            if not cell["revealed"] or cell["mine"]:
                self.assertIsNone(cell["adjacent"])
            else:
                self.assertIsInstance(cell["adjacent"], int)


class StaticAndHealthTests(ServerTestCase):
    def test_healthz(self):
        response = self.request("GET", "/healthz")
        self.assertEqual(response.status, 200)
        self.assertEqual(response.json(), {"status": "ok"})
        self.assert_json_headers(response)

    def test_allowlisted_static_files(self):
        cases = {
            "/": ("index.html", "text/html; charset=utf-8"),
            "/index.html": ("index.html", "text/html; charset=utf-8"),
            "/app.js": ("app.js", "text/javascript; charset=utf-8"),
            "/styles.css": ("styles.css", "text/css; charset=utf-8"),
            "/app.js?v=2": ("app.js", "text/javascript; charset=utf-8"),
        }
        for path, (name, content_type) in cases.items():
            with self.subTest(path=path):
                response = self.request("GET", path)
                self.assertEqual(response.status, 200)
                self.assertEqual(response.body, STATIC_FILES[name])
                self.assertEqual(response.headers["content-type"], content_type)
                self.assertEqual(response.headers["content-length"], str(len(STATIC_FILES[name])))
                self.assertEqual(response.headers["x-content-type-options"], "nosniff")
                self.assertEqual(response.headers["cache-control"], "no-cache")

    def test_head_sends_headers_only(self):
        response = self.request("HEAD", "/styles.css")
        self.assertEqual(response.status, 200)
        self.assertEqual(response.body, b"")
        self.assertEqual(response.headers["content-length"], str(len(STATIC_FILES["styles.css"])))
        response = self.request("HEAD", "/healthz")
        self.assertEqual((response.status, response.body), (200, b""))

    def test_only_allowlisted_paths_are_served(self):
        paths = [
            "/server.py", "/static/app.js", "/../server.py", "/%2e%2e/server.py", "/index.html/",
            "/app.js/..", "//etc/passwd", "/favicon.ico", "/README.md", "/minesweeper/game.py",
            "/.git/config", "/static/../server.py", "/app.js%00", "/APP.JS", "/api",
        ]
        for path in paths:
            with self.subTest(path=path):
                self.assert_error(self.request("GET", path), 404, "not_found")
        self.assertEqual(self.static.requested, [])

    def test_missing_static_file_is_a_json_404(self):
        del self.static.files["app.js"]
        self.assert_error(self.request("GET", "/app.js"), 404, "not_found")

    def test_static_routes_only_allow_get_and_head(self):
        response = self.request("POST", "/", {"x": 1})
        self.assert_error(response, 405, "method_not_allowed")
        self.assertEqual(response.headers["allow"], "GET, HEAD")

    def test_static_directory_is_located_next_to_server_module(self):
        self.assertEqual(server.STATIC_DIR, Path(server.__file__).resolve().parent / "static")
        httpd = create_server("127.0.0.1", 0, store=GameStore(), probability_service=ProbabilityService(FakeSolver()))
        try:
            self.assertEqual(httpd.static_dir, server.STATIC_DIR)
        finally:
            httpd.server_close()

    @unittest.skipUnless(
        all((server.STATIC_DIR / name).is_file() for name in STATIC_FILES), "frontend assets not present"
    )
    def test_serves_real_frontend_assets(self):
        self.start_server(store=GameStore())
        for path, name in [("/", "index.html"), ("/app.js", "app.js"), ("/styles.css", "styles.css")]:
            with self.subTest(path=path):
                response = self.request("GET", path)
                self.assertEqual(response.status, 200)
                self.assertEqual(response.body, (server.STATIC_DIR / name).read_bytes())


class CreateGameTests(ServerTestCase):
    def test_create_returns_hidden_ready_state(self):
        response = self.request("POST", "/api/games", {"width": 9, "height": 8, "mines": 10})
        self.assertEqual(response.status, 201)
        self.assert_json_headers(response)
        state = response.json()
        self.assert_state_shape(state)
        self.assertEqual(
            {k: state[k] for k in STATE_KEYS - {"cells", "id"}},
            {"width": 9, "height": 8, "mines": 10, "status": "ready", "revision": 0, "flags": 0, "elapsed_seconds": 0.0},
        )
        self.assertTrue(all(cell == HIDDEN_CELL for cell in state["cells"]))
        self.assertRegex(state["id"], r"^[A-Za-z0-9_-]{20,64}$")
        self.assertEqual(response.headers["location"], f"/api/games/{state['id']}")
        fetched = self.request("GET", response.headers["location"])
        self.assertEqual(fetched.status, 200)
        self.assertEqual(fetched.json(), state)

    def test_games_are_independent(self):
        first, second = self.create(9, 9, 10), self.create(9, 9, 10)
        self.assertNotEqual(first["id"], second["id"])
        self.play(first["id"], "flag", 0, 0, 0)
        self.assertEqual(self.request("GET", f"/api/games/{second['id']}").json()["revision"], 0)

    def test_rejects_invalid_settings(self):
        valid = {"width": 9, "height": 9, "mines": 10}
        cases = [
            ({"width": 4}, "invalid_width"), ({"width": 81}, "invalid_width"), ({"width": True}, "invalid_width"),
            ({"width": 9.0}, "invalid_width"), ({"width": "9"}, "invalid_width"), ({"width": None}, "invalid_width"),
            ({"height": 0}, "invalid_height"), ({"height": [9]}, "invalid_height"), ({"height": 10 ** 30}, "invalid_height"),
            ({"mines": 0}, "invalid_mines"), ({"mines": 73}, "invalid_mines"), ({"mines": False}, "invalid_mines"),
            ({"mines": 10.5}, "invalid_mines"), ({"mines": {}}, "invalid_mines"),
            ({"mines": None}, "invalid_mines"), ({"extra": 1}, "unknown_fields"),
        ]
        for change, code in cases:
            with self.subTest(change=change):
                self.assert_error(self.request("POST", "/api/games", {**valid, **change}), 400, code)
        self.assert_error(self.request("POST", "/api/games", {"width": 9, "height": 9}), 400, "missing_fields")
        self.assertEqual(len(self.store), 0)

    def test_rejects_malformed_bodies(self):
        cases = [
            (b"", "invalid_json"), (b"{", "invalid_json"), (b"{'width': 9}", "invalid_json"),
            (b'{"width": NaN, "height": 9, "mines": 10}', "invalid_json"),
            (b'{"width": 9, "height": 9, "mines": Infinity}', "invalid_json"),
            (b'{"width": 9, "width": 9, "height": 9, "mines": 10}', "invalid_json"),
            (b"\xff\xfe\x00{", "invalid_json"), (b"[" * 5000, "invalid_json"),
            (b"[9, 9, 10]", "invalid_body"), (b'"9x9"', "invalid_body"), (b"null", "invalid_body"), (b"42", "invalid_body"),
        ]
        for body, code in cases:
            with self.subTest(body=body[:40]):
                self.assert_error(self.request("POST", "/api/games", raw=body), 400, code)
        self.assertEqual(len(self.store), 0)

    def test_requires_json_content_type(self):
        body = {"width": 9, "height": 9, "mines": 10}
        for content_type in [None, "text/plain", "application/x-www-form-urlencoded",
                             "application/json; charset=latin-1", "application/jsonp", "multipart/form-data"]:
            with self.subTest(content_type=content_type):
                response = self.request("POST", "/api/games", body, content_type=content_type)
                self.assert_error(response, 415, "unsupported_media_type")
        for content_type in ["application/json; charset=UTF-8", "Application/JSON", 'application/json; charset="utf-8"']:
            with self.subTest(content_type=content_type):
                self.assertEqual(self.request("POST", "/api/games", body, content_type=content_type).status, 201)

    def test_body_size_limit(self):
        body = json.dumps({"width": 9, "height": 9, "mines": 10}).encode()
        at_limit = body + b" " * (MAX_BODY_BYTES - len(body))
        self.assertEqual(self.request("POST", "/api/games", raw=at_limit).status, 201)
        self.assert_error(self.request("POST", "/api/games", raw=at_limit + b" "), 413, "payload_too_large")
        self.assert_error(self.request("POST", "/api/games", raw=b" " * 300_000), 413, "payload_too_large")

    def test_content_length_rules(self):
        body = b'{"width": 9, "height": 9, "mines": 10}'
        prefix = b"POST /api/games HTTP/1.1\r\nHost: test\r\nContent-Type: application/json\r\n"
        cases = [
            (prefix + b"\r\n" + body, 411, "length_required"),
            (prefix + b"Transfer-Encoding: chunked\r\n\r\n" + b"%x\r\n" % len(body) + body + b"\r\n0\r\n\r\n",
             411, "length_required"),
            (prefix + b"Content-Length: abc\r\n\r\n" + body, 400, "invalid_content_length"),
            (prefix + b"Content-Length: -5\r\n\r\n" + body, 400, "invalid_content_length"),
            (prefix + b"Content-Length: 38\r\nContent-Length: 38\r\n\r\n" + body, 400, "invalid_content_length"),
            (prefix + b"Content-Length: 80\r\n\r\n" + body, 400, "incomplete_body"),
        ]
        for data, status, code in cases:
            with self.subTest(code=code, data=data[len(prefix):len(prefix) + 40]):
                self.assert_error(self.raw_request(data), status, code)
        self.assertEqual(len(self.store), 0)

    def test_rejects_query_parameters(self):
        response = self.request("POST", "/api/games?width=9", {"width": 9, "height": 9, "mines": 10})
        self.assert_error(response, 400, "unexpected_query")


class GameActionTests(ServerTestCase):
    layout = TWO_MINES

    def test_get_and_head_game(self):
        created = self.create()
        response = self.request("GET", f"/api/games/{created['id']}")
        self.assertEqual(response.status, 200)
        self.assert_json_headers(response)
        self.assertEqual(response.json(), created)
        head = self.request("HEAD", f"/api/games/{created['id']}")
        self.assertEqual((head.status, head.body), (200, b""))
        self.assertEqual(head.headers["content-length"], response.headers["content-length"])

    def test_unknown_and_malformed_game_ids(self):
        game_id = self.create()["id"]
        for game_path in ["/api/games/nope", "/api/games/" + "a" * 65, "/api/games/abc.def",
                          "/api/games/%2e%2e", f"/api/games/{game_id}x", f"/api/games/{game_id[:-1]}"]:
            with self.subTest(path=game_path):
                self.assert_error(self.request("GET", game_path), 404, "game_not_found")
                self.assert_error(self.request("GET", game_path + "/probabilities?revision=0"), 404, "game_not_found")
                body = {"action": "reveal", "row": 0, "col": 0, "revision": 0}
                self.assert_error(self.request("POST", game_path + "/actions", body), 404, "game_not_found")
        for path in ["/api/games/", f"/api/games/{game_id}/", f"/api/games/{game_id}/moves", f"/api/games/{game_id}/actions/x"]:
            with self.subTest(path=path):
                self.assert_error(self.request("GET", path), 404, "not_found")

    def test_wrong_methods(self):
        game_id = self.create()["id"]
        cases = [
            ("GET", "/api/games", "POST"), ("DELETE", "/api/games", "POST"),
            ("DELETE", f"/api/games/{game_id}", "GET, HEAD"), ("PUT", f"/api/games/{game_id}", "GET, HEAD"),
            ("GET", f"/api/games/{game_id}/actions", "POST"), ("PATCH", f"/api/games/{game_id}/actions", "POST"),
            ("POST", f"/api/games/{game_id}/probabilities", "GET, HEAD"), ("OPTIONS", "/healthz", "GET, HEAD"),
        ]
        for method, path, allow in cases:
            with self.subTest(method=method, path=path):
                response = self.request(method, path)
                self.assertEqual(response.headers.get("allow"), allow)
                if method != "HEAD":
                    self.assert_error(response, 405, "method_not_allowed")

    def test_play_to_victory(self):
        game_id = self.create()["id"]
        state = self.play(game_id, "reveal", 0, 0, 0)
        self.assertEqual((state["status"], state["revision"]), ("playing", 1))
        self.assertEqual(render(state), ["00000", "00000", "11211", "#####", "#####"])
        state = self.play(game_id, "flag", 3, 1, 1)
        self.assertEqual((state["revision"], state["flags"]), (2, 1))
        state = self.play(game_id, "chord", 2, 0, 2)
        self.assertEqual(render(state)[3:], ["1F###", "#####"])
        moves = [(3, 2), (3, 4), (4, 0), (4, 1), (4, 2), (4, 3)]
        for row, col in moves:
            state = self.play(game_id, "reveal", row, col, state["revision"])
            self.assertEqual(state["status"], "playing")
            self.assert_state_shape(state)
        state = self.play(game_id, "reveal", 4, 4, state["revision"])
        self.assert_state_shape(state)
        self.assertEqual((state["status"], state["revision"], state["flags"]), ("won", 10, 2))
        self.assertEqual(render(state), ["00000", "00000", "11211", "1F2F1", "11211"])
        self.assertEqual([i for i, cell in enumerate(state["cells"]) if cell["mine"]], TWO_MINES)
        self.assertFalse(any(cell["exploded"] for cell in state["cells"]))
        self.assertIsInstance(state["elapsed_seconds"], (int, float))
        self.assertEqual(self.request("GET", f"/api/games/{game_id}").json(), state)

    def test_losing_reports_explosion_and_layout(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        self.play(game_id, "flag", 3, 0, 1)  # wrong flag
        state = self.play(game_id, "chord", 2, 0, 2)
        self.assert_state_shape(state)
        self.assertEqual(state["status"], "lost")
        self.assertEqual([i for i, cell in enumerate(state["cells"]) if cell["exploded"]], [16])
        self.assertEqual([i for i, cell in enumerate(state["cells"]) if cell["mine"]], TWO_MINES)
        self.assertEqual(state["cells"][15], {"revealed": False, "flagged": True, "adjacent": None, "mine": False, "exploded": False})

    def test_stale_revision_is_a_conflict_with_latest_state(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        latest = untimed(self.request("GET", f"/api/games/{game_id}").json())
        for revision in (0, 2, 50):
            with self.subTest(revision=revision):
                payload = self.assert_error(self.act(game_id, "reveal", 4, 4, revision), 409, "stale_revision")
                self.assertEqual(untimed(payload["state"]), latest)
        self.assertEqual(untimed(self.request("GET", f"/api/games/{game_id}").json()), latest)

    def test_actions_after_game_over_are_conflicts(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        final = self.play(game_id, "reveal", 3, 1, 1)
        for action in ("reveal", "flag", "chord"):
            with self.subTest(action=action):
                payload = self.assert_error(self.act(game_id, action, 4, 4, 2), 409, "game_over")
                self.assertEqual(payload["state"], final)

    def test_rejects_invalid_action_payloads(self):
        game_id = self.create()["id"]
        valid = {"action": "reveal", "row": 0, "col": 0, "revision": 0}
        cases = [
            ({"action": "explode"}, "invalid_action"), ({"action": 1}, "invalid_action"), ({"action": None}, "invalid_action"),
            ({"row": True}, "invalid_coordinates"), ({"col": False}, "invalid_coordinates"), ({"row": 1.0}, "invalid_coordinates"),
            ({"row": "1"}, "invalid_coordinates"), ({"col": None}, "invalid_coordinates"), ({"row": [0]}, "invalid_coordinates"),
            ({"row": -1}, "out_of_bounds"), ({"row": 5}, "out_of_bounds"), ({"col": 5}, "out_of_bounds"), ({"col": 10 ** 40}, "out_of_bounds"),
            ({"revision": True}, "invalid_revision"), ({"revision": -1}, "invalid_revision"), ({"revision": "0"}, "invalid_revision"),
            ({"revision": None}, "invalid_revision"), ({"revision": 0.0}, "invalid_revision"),
            ({"cheat": True}, "unknown_fields"),
        ]
        for change, code in cases:
            with self.subTest(change=change):
                self.assert_error(self.request("POST", f"/api/games/{game_id}/actions", {**valid, **change}), 400, code)
        for missing in valid:
            body = {k: v for k, v in valid.items() if k != missing}
            self.assert_error(self.request("POST", f"/api/games/{game_id}/actions", body), 400, "missing_fields")
        self.assert_error(self.request("POST", f"/api/games/{game_id}/actions", raw=b"[]"), 400, "invalid_body")
        self.assert_error(self.request("POST", f"/api/games/{game_id}/actions", valid, content_type="text/plain"),
                          415, "unsupported_media_type")
        self.assert_error(self.request("POST", f"/api/games/{game_id}/actions?x=1", valid), 400, "unexpected_query")
        state = self.request("GET", f"/api/games/{game_id}").json()
        self.assertEqual((state["status"], state["revision"]), ("ready", 0))

    def test_noop_actions_keep_revision(self):
        game_id = self.create()["id"]
        state = untimed(self.play(game_id, "reveal", 0, 0, 0))
        for action, row, col in [("flag", 1, 1), ("reveal", 1, 1), ("chord", 4, 4)]:
            with self.subTest(action=action):
                self.assertEqual(untimed(self.play(game_id, action, row, col, 1)), state)

    def test_concurrent_moves_on_one_revision_apply_once(self):
        game_id = self.create()["id"]
        barrier = threading.Barrier(8)
        statuses = []

        def flag(col):
            barrier.wait()
            statuses.append(self.act(game_id, "flag", col % 5, col // 5, 0).status)

        threads = [threading.Thread(target=flag, args=(i,)) for i in range(8)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(15)
        self.assertEqual(sorted(statuses), [200] + [409] * 7)
        state = self.request("GET", f"/api/games/{game_id}").json()
        self.assertEqual((state["revision"], state["flags"]), (1, 1))


class ProbabilityTests(ServerTestCase):
    layout = TWO_MINES

    def assert_odds_shape(self, payload, game_id, revision):
        self.assertEqual(set(payload), ODDS_KEYS)
        self.assertEqual(set(payload["meta"]), META_KEYS)
        self.assertEqual((payload["game_id"], payload["revision"]), (game_id, revision))
        self.assertEqual(len(payload["probabilities"]), 25)
        self.assertIsInstance(payload["message"], str)

    def test_not_started_game(self):
        game_id = self.create()["id"]
        response = self.odds(game_id, 0)
        self.assertEqual(response.status, 200)
        self.assert_json_headers(response)
        payload = response.json()
        self.assert_odds_shape(payload, game_id, 0)
        self.assertEqual(payload["status"], "not-started")
        self.assertEqual(payload["probabilities"], [None] * 25)
        self.assertEqual((payload["proven_safe"], payload["proven_mines"]), ([], []))
        self.assertIn("first", payload["message"])
        self.assertIn("neighbors", payload["message"])
        self.assertEqual(self.solver.calls, [])

    def test_playing_game_uses_only_public_observation(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        state = self.play(game_id, "flag", 3, 1, 1)
        response = self.odds(game_id, 2)
        self.assertEqual(response.status, 200)
        payload = response.json()
        self.assert_odds_shape(payload, game_id, 2)
        self.assertEqual(payload["status"], "exact")
        self.assertEqual(payload["message"], "Fake odds.")
        self.assertEqual(payload["meta"]["elapsed_ms"], 0.25)
        public = {i: cell["adjacent"] for i, cell in enumerate(state["cells"]) if cell["revealed"]}
        self.assertEqual(len(self.solver.calls), 1)
        call = self.solver.calls[0]
        self.assertEqual(call["args"], (5, 5, 2, public))
        self.assertEqual(call["kwargs"], {"time_budget": 1.5, "node_budget": 100_000, "sample_budget": 2_000})

    def test_results_are_cached_per_revision(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        first = self.odds(game_id, 1).json()
        self.assertEqual(self.odds(game_id, 1).json(), first)
        self.assertEqual(len(self.solver.calls), 1)
        self.play(game_id, "flag", 1, 1, 1)  # no-op keeps the cached result
        self.odds(game_id, 1)
        self.assertEqual(len(self.solver.calls), 1)
        self.play(game_id, "flag", 4, 4, 1)
        self.assertEqual(self.odds(game_id, 2).json()["revision"], 2)
        self.assertEqual(len(self.solver.calls), 2)

    def test_stale_revision_is_a_conflict(self):
        game_id = self.create()["id"]
        state = self.play(game_id, "reveal", 0, 0, 0)
        for revision in (0, 2):
            with self.subTest(revision=revision):
                payload = self.assert_error(self.odds(game_id, revision), 409, "stale_revision")
                self.assertEqual(untimed(payload["state"]), untimed(state))
        self.assertEqual(self.solver.calls, [])

    def test_revision_query_is_strict(self):
        game_id = self.create()["id"]
        base = f"/api/games/{game_id}/probabilities"
        cases = [
            ("", "missing_revision"), ("?", "missing_revision"),
            ("?revision=", "invalid_revision"), ("?revision=abc", "invalid_revision"), ("?revision=-1", "invalid_revision"),
            ("?revision=1.0", "invalid_revision"), ("?revision=01", "invalid_revision"), ("?revision=+0", "invalid_revision"),
            ("?revision=%2B0", "invalid_revision"), ("?revision=1e2", "invalid_revision"), ("?revision=true", "invalid_revision"),
            ("?revision=%EF%BC%90", "invalid_revision"), ("?revision=" + "9" * 30, "invalid_revision"),
            ("?revision=0&revision=0", "invalid_revision"),
            ("?revision=0&extra=1", "unknown_query_parameters"), ("?Revision=0", "unknown_query_parameters"),
            ("?revision", "invalid_query"), ("?revision=0&&", "invalid_query"), ("?" + "&".join(["a=1"] * 20), "invalid_query"),
        ]
        for suffix, code in cases:
            with self.subTest(query=suffix[:40]):
                self.assert_error(self.request("GET", base + suffix), 400, code)
        self.assertEqual(self.request("GET", base + "?revision=0").status, 200)

    def test_finished_game(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        self.play(game_id, "reveal", 3, 1, 1)
        payload = self.odds(game_id, 2).json()
        self.assert_odds_shape(payload, game_id, 2)
        self.assertEqual(payload["status"], "finished")
        self.assertEqual(payload["probabilities"], [None] * 25)
        self.assertEqual(self.solver.calls, [])

    def test_solver_inconsistency_is_a_logged_internal_error(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        self.solver.error = FakeInconsistentBoardError("no layout fits /secret/path")
        with self.assertLogs("minesweeper.server", "ERROR") as logs:
            response = self.odds(game_id, 1)
        self.assert_error(response, 500, "probability_failed")
        self.assertNotIn(b"secret", response.body)
        self.assertIn("inconsistent", "\n".join(logs.output))
        self.solver.error = None
        self.assertEqual(self.odds(game_id, 1).status, 200)  # failures are not cached

    def test_solver_crash_is_a_logged_internal_error(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        self.solver.error = ZeroDivisionError("boom")
        with self.assertLogs("minesweeper.server", "ERROR"):
            response = self.odds(game_id, 1)
        self.assert_error(response, 500, "probability_failed")
        self.assertNotIn(b"boom", response.body)

    def opened_game_odds(self, result):
        """Odds for a fresh TWO_MINES game after revealing (0, 0), with the solver returning ``result``."""
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        self.solver.result = result
        return self.odds(game_id, 1)

    def assert_rejected(self, cases, reason=None):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)  # failures are not cached, so one game serves every case
        for label, result in cases:
            with self.subTest(label):
                self.solver.result = result
                with self.assertLogs("minesweeper.server", "ERROR") as logs:
                    self.assert_error(self.odds(game_id, 1), 500, "probability_failed")
                if reason is not None:
                    self.assertIn(reason, "\n".join(logs.output))

    def test_malformed_solver_output_is_rejected(self):
        good = fake_odds(5, 5, 2, OPENED)
        meta = good["meta"]
        self.assert_rejected([
            ("not a dict", None), ("empty", {}), ("placeholder status", {**good, "status": "finished"}),
            ("short list", {**good, "probabilities": [0.5] * 24}), ("above one", {**good, "probabilities": [1.5] * 25}),
            ("nan", {**good, "probabilities": [math.nan] * 25}), ("bool", {**good, "probabilities": [True] * 25}),
            ("index out of range", {**good, "proven_safe": [25]}), ("string index", {**good, "proven_safe": ["1"]}),
            ("both lists", {**good, "proven_safe": [20], "proven_mines": [20]}),
            ("message", {**good, "message": None}), ("negative count", {**good, "meta": {**meta, "samples": -1}}),
            ("missing count", {**good, "meta": {k: v for k, v in meta.items() if k != "components"}}),
            ("elapsed", {**good, "meta": {**meta, "elapsed_ms": "fast"}}), ("reason", {**good, "meta": {**meta, "reason": 3}}),
        ])

    def test_proofs_must_match_odds_and_skip_revealed_cells(self):
        good = fake_odds(5, 5, 2, OPENED)
        self.assert_rejected([
            ("safe proof on revealed cell", {**good, "proven_safe": [3]}),
            ("mine proof on revealed cell", {**good, "proven_mines": [4]}),
        ], reason="lists revealed cell")
        self.assert_rejected([
            ("safe proof with nonzero odds", {**good, "proven_safe": [17]}),
            ("mine proof with odds below one", {**good, "proven_mines": [16]}),
            ("proof without odds", {**good, "status": "unavailable", "probabilities": [None] * 25, "proven_safe": [17]}),
        ], reason="whose probability is")
        odds = list(good["probabilities"])
        odds[16], odds[17], odds[20] = 1, 0.0, 0.0
        payload = self.opened_game_odds({
            **good, "probabilities": odds, "proven_safe": [20, 20, 17], "proven_mines": [16], "extra": "dropped",
        }).json()
        self.assertEqual((payload["proven_safe"], payload["proven_mines"]), ([17, 20], [16]))
        self.assertEqual(payload["probabilities"][15:18], [0.2, 1.0, 0.0])
        self.assertNotIn("extra", payload)

    def test_only_unavailable_results_may_leave_hidden_cells_unknown(self):
        good = fake_odds(5, 5, 2, OPENED)
        missing_hidden = [*good["probabilities"][:24], None]
        self.assert_rejected([
            ("exact", {**good, "probabilities": missing_hidden}),
            ("approximate", {**good, "status": "approximate", "probabilities": missing_hidden}),
        ])
        partial = [None] * 25
        partial[17] = 0.0
        unavailable = {**good, "status": "unavailable", "probabilities": partial, "proven_safe": [17]}
        payload = self.opened_game_odds(unavailable).json()
        self.assertEqual((payload["status"], payload["probabilities"], payload["proven_safe"]), ("unavailable", partial, [17]))
        payload = self.opened_game_odds({**good, "status": "approximate"}).json()
        self.assertEqual(payload["probabilities"], good["probabilities"])

    def test_revealed_cells_must_have_null_odds(self):
        good = fake_odds(5, 5, 2, OPENED)
        odds = list(good["probabilities"])
        odds[7] = 0.0  # a revealed safe cell must not carry a number, even zero
        self.assert_rejected(
            [(status, {**good, "status": status, "probabilities": odds}) for status in ("exact", "approximate", "unavailable")],
            reason="revealed cell 7 must have a null probability",
        )

    def test_unavailable_results_are_recomputed_on_retry(self):
        good = fake_odds(5, 5, 2, OPENED)
        unavailable = {**good, "status": "unavailable", "probabilities": [None] * 25,
                       "meta": {**good["meta"], "reason": "time_budget_exhausted"}}
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        self.solver.result = unavailable
        for attempt in (1, 2):
            self.assertEqual(self.odds(game_id, 1).json()["status"], "unavailable")
            self.assertEqual(len(self.solver.calls), attempt)
        self.solver.result = good  # a retry that succeeds is cached as usual
        self.assertEqual(self.odds(game_id, 1).json()["status"], "exact")
        self.assertEqual(self.odds(game_id, 1).json()["status"], "exact")
        self.assertEqual(len(self.solver.calls), 3)

    def test_complete_results_are_reused_for_the_same_revision(self):
        good = fake_odds(5, 5, 2, OPENED)
        for status in ("exact", "approximate"):
            with self.subTest(status=status):
                self.solver.calls.clear()
                game_id = self.create()["id"]
                self.play(game_id, "reveal", 0, 0, 0)
                self.solver.result = {**good, "status": status}
                first = self.odds(game_id, 1).json()
                self.assertEqual(first["status"], status)
                self.assertEqual(self.odds(game_id, 1).json(), first)
                self.assertEqual(len(self.solver.calls), 1)

    def test_optional_solver_diagnostics_are_forwarded(self):
        good = fake_odds(5, 5, 2, OPENED)
        diagnostics = {"exact_components": 1, "sampled_components": 2, "sample_attempts": 340, "effective_sample_size": 57.25}
        unknown = {"nodes": 99, "hidden_cells": 10, "internal": {"x": 1}}
        payload = self.opened_game_odds({**good, "meta": {**good["meta"], **diagnostics, **unknown}}).json()
        self.assertEqual(payload["meta"], {**good["meta"], **diagnostics})
        for extra in ({"effective_sample_size": None}, {"effective_sample_size": 0}, {"sample_attempts": 0}):
            with self.subTest(extra=extra):
                payload = self.opened_game_odds({**good, "meta": {**good["meta"], **extra}}).json()
                self.assertEqual(payload["meta"], {**good["meta"], **extra})

    def test_absent_optional_diagnostics_stay_absent(self):
        good = fake_odds(5, 5, 2, OPENED)
        payload = self.opened_game_odds(good).json()
        self.assertEqual(payload["meta"], good["meta"])
        self.assertFalse(OPTIONAL_META_KEYS & set(payload["meta"]))

    def test_invalid_optional_diagnostics_are_rejected(self):
        good = fake_odds(5, 5, 2, OPENED)
        cases = [("effective_sample_size", value) for value in (-1, -0.5, math.nan, math.inf, "12", True, [3])]
        cases += [
            (key, value)
            for key in ("exact_components", "sampled_components", "sample_attempts")
            for value in (-1, 1.5, 2.0, True, None, "3")
        ]
        self.assert_rejected([(f"{key}={value!r}", {**good, "meta": {**good["meta"], key: value}}) for key, value in cases])

    def test_duplicate_requests_share_one_solver_run(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        self.solver.gate = threading.Event()
        responses = []

        def fetch():
            responses.append(self.odds(game_id, 1))

        threads = [threading.Thread(target=fetch) for _ in range(4)]
        for thread in threads:
            thread.start()
        self.assertTrue(self.solver.started.wait(5))
        time.sleep(0.2)
        self.solver.gate.set()
        for thread in threads:
            thread.join(15)
        self.assertEqual([r.status for r in responses], [200] * 4)
        self.assertEqual(len({r.body for r in responses}), 1)
        self.assertEqual(len(self.solver.calls), 1)

    def test_game_stays_playable_while_solving(self):
        game_id = self.create()["id"]
        self.play(game_id, "reveal", 0, 0, 0)
        self.solver.gate = threading.Event()
        slow = []
        thread = threading.Thread(target=lambda: slow.append(self.odds(game_id, 1)))
        thread.start()
        self.assertTrue(self.solver.started.wait(5))
        started = time.monotonic()
        self.assertEqual(self.play(game_id, "flag", 4, 4, 1)["revision"], 2)
        self.assertLess(time.monotonic() - started, 2.0)
        self.solver.gate.set()
        thread.join(15)
        self.assertEqual(slow[0].status, 200)
        self.assertEqual(slow[0].json()["revision"], 1)  # snapshot revision; the client discards it
        self.assert_error(self.odds(game_id, 1), 409, "stale_revision")
        self.assertEqual(self.odds(game_id, 2).json()["revision"], 2)
        self.assertEqual(len(self.solver.calls), 2)


class ErrorHandlingTests(ServerTestCase):
    def test_unknown_paths(self):
        for path in ["/nope", "/api/game", "/api/games/x/y/z", "/healthz/"]:
            with self.subTest(path=path):
                self.assert_error(self.request("GET", path), 404, "not_found")

    def test_protocol_errors_use_json(self):
        cases = [
            (b"BREW /healthz HTTP/1.1\r\nHost: test\r\n\r\n", 501, "unsupported_method"),
            (b"GET /" + b"a" * (65537 - 5), 414, "uri_too_long"),
            (b"GET /healthz HTTP/1.1\r\n" + b"".join(b"X-%d: y\r\n" % i for i in range(120)) + b"\r\n",
             431, "headers_too_large"),
        ]
        for data, status, code in cases:
            with self.subTest(code=code):
                with self.assertLogs("minesweeper.server", "WARNING"):
                    response = self.raw_request(data)
                self.assertEqual(response.status, status)
                self.assertEqual(response.json()["error"]["code"], code)
                self.assertEqual(response.headers["content-type"], "application/json; charset=utf-8")

    def test_unexpected_errors_are_logged_and_hidden(self):
        self.start_server(store=ExplodingStore(), static_dir=self.static)
        with self.assertLogs("minesweeper.server", "ERROR") as logs:
            response = self.request("GET", "/api/games/abc")
        self.assert_error(response, 500, "internal_error")
        self.assertNotIn(b"secret", response.body)
        self.assertNotIn(b"/Users", response.body)
        self.assertIn("RuntimeError", "\n".join(logs.output))

    def test_server_header_hides_versions(self):
        response = self.request("GET", "/healthz")
        self.assertEqual(response.headers["server"], "Minesweeper")


class RealSolverIntegrationTests(ServerTestCase):
    layout = TWO_MINES

    def make_probability_service(self):
        try:
            return ProbabilityService()
        except ModuleNotFoundError as exc:
            if exc.name != "minesweeper.probability":
                raise
            self.skipTest("minesweeper.probability is not available")

    def test_odds_for_a_forced_position(self):
        game_id = self.create()["id"]
        state = self.play(game_id, "reveal", 0, 0, 0)
        response = self.odds(game_id, 1)
        self.assertEqual(response.status, 200, response.body)
        payload = response.json()
        self.assertEqual(set(payload), ODDS_KEYS)
        self.assertEqual(set(payload["meta"]), META_KEYS | OPTIONAL_META_KEYS)  # solver internals are dropped
        self.assertEqual(payload["status"], "exact")
        revealed = {i for i, cell in enumerate(state["cells"]) if cell["revealed"]}
        hidden = [p for i, p in enumerate(payload["probabilities"]) if i not in revealed]
        self.assertTrue(all(isinstance(p, (int, float)) for p in hidden))
        self.assertAlmostEqual(sum(hidden), 2.0, places=6)  # expected mines equals the mine count
        self.assertLessEqual({16, 18}, set(payload["proven_mines"]))
        self.assertLessEqual({15, 17, 19}, set(payload["proven_safe"]))
        self.assertFalse(revealed & (set(payload["proven_mines"]) | set(payload["proven_safe"])))


if __name__ == "__main__":
    unittest.main()
