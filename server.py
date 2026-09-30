#!/usr/bin/env python3
"""Minesweeper web server (Python standard library only).

Run ``python3 server.py`` and open http://127.0.0.1:8000/. The server listens
on this computer only unless ``--host`` is given explicitly.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import logging
import math
import os
import re
import socket
import socketserver
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence, Tuple, Type
from urllib.parse import parse_qsl

from minesweeper.game import (
    STATUS_PLAYING,
    STATUS_READY,
    STATUS_WON,
    GameEntry,
    GameError,
    GameNotFoundError,
    GameOverError,
    GameStore,
    Snapshot,
    StaleRevisionError,
)

logger = logging.getLogger("minesweeper.server")

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8000
STATIC_DIR = Path(__file__).resolve().parent / "static"
# Fixed allowlist: nothing else on disk is ever served.
STATIC_ROUTES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/styles.css": ("styles.css", "text/css; charset=utf-8"),
}

MAX_BODY_BYTES = 8 * 1024
# Unread request bodies up to this size are drained before replying so the
# client reliably receives the error instead of a connection reset.
MAX_DRAIN_BYTES = 1024 * 1024
JSON_CONTENT_TYPE = "application/json; charset=utf-8"
API_CACHE_CONTROL = "no-store"
STATIC_CACHE_CONTROL = "no-cache"

CREATE_FIELDS = ("width", "height", "mines")
ACTION_FIELDS = ("action", "row", "col", "revision")
AUTOSOLVE_FIELDS = ("revision",)

SOLVER_TIME_BUDGET = 1.5
SOLVER_NODE_BUDGET = 100_000
SOLVER_SAMPLE_BUDGET = 2_000
MAX_CONCURRENT_SOLVES = 2
SOLVER_WAIT_TIMEOUT = 10.0

_GAME_ROUTE = re.compile(r"/api/games/([^/]+)(?:/(actions|probabilities|autosolve))?")
_REVISION_TEXT = re.compile(r"0|[1-9][0-9]{0,15}")
_CONTENT_LENGTH_TEXT = re.compile(r"[0-9]{1,18}")
_SOLVER_STATUSES = frozenset({"exact", "approximate", "unavailable"})
# Only complete results are cached; "unavailable" is recomputed on retry.
_CACHEABLE_STATUSES = frozenset({"exact", "approximate"})
_META_COUNTS = ("frontier_cells", "components", "unconstrained_cells", "samples")
# Optional solver diagnostics forwarded when present; other extra keys are dropped.
_OPTIONAL_META_COUNTS = ("exact_components", "sampled_components", "sample_attempts")


class ApiError(Exception):
    """An error that maps directly onto a JSON error response."""

    def __init__(
        self,
        status: int,
        code: str,
        message: str,
        *,
        state: Optional[Dict[str, Any]] = None,
        headers: Iterable[Tuple[str, str]] = (),
    ) -> None:
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message
        self.state = state
        self.headers = tuple(headers)

    def payload(self) -> Dict[str, Any]:
        body: Dict[str, Any] = {"error": {"code": self.code, "message": self.message}}
        if self.state is not None:
            body["state"] = self.state
        return body


def _api_error_from_game_error(exc: GameError) -> ApiError:
    if isinstance(exc, GameNotFoundError):
        status = 404
    elif isinstance(exc, (StaleRevisionError, GameOverError)):
        status = 409
    else:
        status = 400
    return ApiError(status, exc.code, exc.message, state=exc.state)


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_number(value: Any) -> bool:
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )


# ---------------------------------------------------------------- probabilities


def load_default_solver() -> Tuple[Callable[..., Dict[str, Any]], Type[Exception]]:
    from minesweeper.probability import calculate_probabilities, InconsistentBoardError

    return calculate_probabilities, InconsistentBoardError


def _solver_failed() -> ApiError:
    return ApiError(
        500,
        "probability_failed",
        "Mine odds could not be calculated for this position. The game itself is unaffected.",
    )


def _solver_busy() -> ApiError:
    return ApiError(
        503,
        "solver_busy",
        "The odds calculator is busy. Please try again in a moment.",
        headers=[("Retry-After", "1")],
    )


def _index_list(value: Any, size: int, name: str) -> List[int]:
    if not isinstance(value, list) or not all(_is_int(i) and 0 <= i < size for i in value):
        raise ValueError(f"{name} must be a list of cell indices")
    return sorted(set(value))


def _normalize_solver_result(result: Any, snapshot: Snapshot) -> Dict[str, Any]:
    """Validate solver output against the API contract; raise ValueError if broken."""
    size = snapshot.width * snapshot.height
    if not isinstance(result, dict):
        raise ValueError("result is not a dict")
    status = result.get("status")
    if status not in _SOLVER_STATUSES:
        raise ValueError(f"unexpected status {status!r}")
    probabilities = result.get("probabilities")
    if not isinstance(probabilities, list) or len(probabilities) != size:
        raise ValueError("probabilities must have one entry per cell")
    revealed = snapshot.revealed or {}
    # Revealed cells never have odds; exact and approximate results must give
    # odds for every hidden cell, while unavailable ones may leave them null.
    complete = status != "unavailable"
    odds: List[Optional[float]] = []
    for index, value in enumerate(probabilities):
        if index in revealed:
            if value is not None:
                raise ValueError(f"revealed cell {index} must have a null probability")
            odds.append(None)
        elif value is None:
            if complete:
                raise ValueError(f"{status} result has no probability for hidden cell {index}")
            odds.append(None)
        elif _is_number(value) and 0 <= value <= 1:
            odds.append(float(value))
        else:
            raise ValueError("probabilities must be null or numbers in [0, 1]")
    proven_safe = _index_list(result.get("proven_safe"), size, "proven_safe")
    proven_mines = _index_list(result.get("proven_mines"), size, "proven_mines")
    if set(proven_safe) & set(proven_mines):
        raise ValueError("a cell is listed as both proven safe and a proven mine")
    for name, proven, certain in (("proven_safe", proven_safe, 0.0), ("proven_mines", proven_mines, 1.0)):
        for index in proven:
            if index in revealed:
                raise ValueError(f"{name} lists revealed cell {index}")
            if odds[index] != certain:
                raise ValueError(f"{name} lists cell {index} whose probability is {odds[index]!r}")
    message = result.get("message")
    if not isinstance(message, str):
        raise ValueError("message must be a string")
    meta = result.get("meta")
    if not isinstance(meta, dict):
        raise ValueError("meta must be an object")
    clean_meta: Dict[str, Any] = {}
    for key in _META_COUNTS:
        if not _is_int(meta.get(key)) or meta[key] < 0:
            raise ValueError(f"meta.{key} must be a non-negative integer")
        clean_meta[key] = meta[key]
    elapsed_ms = meta.get("elapsed_ms")
    if not _is_number(elapsed_ms) or elapsed_ms < 0:
        raise ValueError("meta.elapsed_ms must be a non-negative number")
    clean_meta["elapsed_ms"] = elapsed_ms
    reason = meta.get("reason")
    if reason is not None and not isinstance(reason, str):
        raise ValueError("meta.reason must be a string or null")
    clean_meta["reason"] = reason
    for key in _OPTIONAL_META_COUNTS:
        if key in meta:
            if not _is_int(meta[key]) or meta[key] < 0:
                raise ValueError(f"meta.{key} must be a non-negative integer")
            clean_meta[key] = meta[key]
    if "effective_sample_size" in meta:
        ess = meta["effective_sample_size"]
        if ess is not None and (not _is_number(ess) or ess < 0):
            raise ValueError("meta.effective_sample_size must be a non-negative number or null")
        clean_meta["effective_sample_size"] = ess
    return {
        "game_id": snapshot.game_id,
        "revision": snapshot.revision,
        "status": status,
        "probabilities": odds,
        "proven_safe": proven_safe,
        "proven_mines": proven_mines,
        "message": message,
        "meta": clean_meta,
    }


def _placeholder_response(snapshot: Snapshot) -> Dict[str, Any]:
    """Response for games that are not in progress; the solver is not run."""
    if snapshot.status == STATUS_READY:
        status, reason = "not-started", "not_started"
        message = (
            "No mines have been placed yet. They are placed on your first reveal, and "
            "that cell and all of its neighbors are guaranteed to be safe."
        )
    else:
        status, reason = "finished", "game_over"
        outcome = "won" if snapshot.status == STATUS_WON else "lost"
        message = f"This game is over (you {outcome}), so there are no odds to show."
    return {
        "game_id": snapshot.game_id,
        "revision": snapshot.revision,
        "status": status,
        "probabilities": [None] * (snapshot.width * snapshot.height),
        "proven_safe": [],
        "proven_mines": [],
        "message": message,
        "meta": {
            "frontier_cells": 0,
            "components": 0,
            "unconstrained_cells": 0,
            "samples": 0,
            "elapsed_ms": 0.0,
            "reason": reason,
        },
    }


class ProbabilityService:
    """Runs the solver on public observations only, with per-revision caching.

    The game lock is held only while snapshotting. Solver runs for one game are
    serialized so duplicate requests for the same revision reuse one result, and
    a global semaphore bounds how many games are solved at once.
    """

    def __init__(
        self,
        calculator: Optional[Callable[..., Dict[str, Any]]] = None,
        inconsistent_error: Optional[Type[Exception]] = None,
        *,
        time_budget: float = SOLVER_TIME_BUDGET,
        node_budget: int = SOLVER_NODE_BUDGET,
        sample_budget: int = SOLVER_SAMPLE_BUDGET,
        max_concurrent: int = MAX_CONCURRENT_SOLVES,
        wait_timeout: float = SOLVER_WAIT_TIMEOUT,
    ) -> None:
        if calculator is None:
            calculator, inconsistent_error = load_default_solver()
        self._calculator = calculator
        self._inconsistent_error: Any = inconsistent_error or ()
        self._time_budget = time_budget
        self._node_budget = node_budget
        self._sample_budget = sample_budget
        self._wait_timeout = wait_timeout
        self._slots = threading.BoundedSemaphore(max_concurrent)

    def probabilities(self, entry: GameEntry, revision: int) -> Dict[str, Any]:
        snapshot = entry.snapshot()
        self._require_current(entry, snapshot, revision)
        if snapshot.status != STATUS_PLAYING:
            return _placeholder_response(snapshot)
        if snapshot.cached_probabilities is not None:
            return snapshot.cached_probabilities
        if not entry.solver_lock.acquire(timeout=self._wait_timeout):
            raise _solver_busy()
        try:
            # While waiting, another request may have solved this revision or
            # the game may have moved on; never solve a stale position.
            snapshot = entry.snapshot(include_observation=True)
            self._require_current(entry, snapshot, revision)
            if snapshot.status != STATUS_PLAYING:
                return _placeholder_response(snapshot)
            if snapshot.cached_probabilities is not None:
                return snapshot.cached_probabilities
            payload = self._solve(snapshot)
            if payload["status"] in _CACHEABLE_STATUSES:
                entry.cache_probabilities(snapshot.revision, payload)
            return payload
        finally:
            entry.solver_lock.release()

    @staticmethod
    def _require_current(entry: GameEntry, snapshot: Snapshot, revision: int) -> None:
        if snapshot.revision != revision:
            error = StaleRevisionError(revision, snapshot.revision)
            error.state = entry.state()
            raise error

    def _solve(self, snapshot: Snapshot) -> Dict[str, Any]:
        if not self._slots.acquire(timeout=self._wait_timeout):
            raise _solver_busy()
        try:
            result = self._calculator(
                snapshot.width,
                snapshot.height,
                snapshot.mines,
                snapshot.revealed,
                time_budget=self._time_budget,
                node_budget=self._node_budget,
                sample_budget=self._sample_budget,
            )
        except self._inconsistent_error:
            # A real game always has a consistent layout, so this is a bug.
            logger.error(
                "Solver reported an inconsistent board for game %s revision %d "
                "(%d revealed cells); this indicates an engine or solver bug",
                snapshot.game_id, snapshot.revision, len(snapshot.revealed or ()),
                exc_info=True,
            )
            raise _solver_failed() from None
        except Exception:
            logger.exception(
                "Solver crashed for game %s revision %d", snapshot.game_id, snapshot.revision
            )
            raise _solver_failed() from None
        finally:
            self._slots.release()
        try:
            return _normalize_solver_result(result, snapshot)
        except ValueError as exc:
            logger.error(
                "Solver returned malformed output for game %s revision %d: %s",
                snapshot.game_id, snapshot.revision, exc,
            )
            raise _solver_failed() from None


# ------------------------------------------------------------------ requests


class _DuplicateKeyError(ValueError):
    pass


def _unique_object(pairs: List[Tuple[str, Any]]) -> Dict[str, Any]:
    result: Dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateKeyError(key)
        result[key] = value
    return result


def _reject_constant(name: str) -> Any:
    raise ValueError(f"{name} is not valid JSON")


def _decode_json_object(body: bytes) -> Dict[str, Any]:
    try:
        value = json.loads(
            body.decode("utf-8"),
            object_pairs_hook=_unique_object,
            parse_constant=_reject_constant,
        )
    except _DuplicateKeyError:
        raise ApiError(
            400, "invalid_json", "The request body repeats a field; each field may appear only once."
        ) from None
    except (UnicodeDecodeError, ValueError, RecursionError):
        raise ApiError(400, "invalid_json", "The request body is not valid JSON.") from None
    if not isinstance(value, dict):
        raise ApiError(400, "invalid_body", "The request body must be a JSON object.")
    return value


def _describe(names: Sequence[str]) -> str:
    shown = ", ".join(json.dumps(name[:40]) for name in names[:5])
    return shown + (f" and {len(names) - 5} more" if len(names) > 5 else "")


def _require_fields(payload: Dict[str, Any], fields: Sequence[str]) -> None:
    unknown = [key for key in payload if key not in fields]
    if unknown:
        raise ApiError(
            400,
            "unknown_fields",
            f"Unknown field(s): {_describe(unknown)}. Expected exactly: {', '.join(fields)}.",
        )
    missing = [name for name in fields if name not in payload]
    if missing:
        raise ApiError(400, "missing_fields", f"Missing required field(s): {', '.join(missing)}.")


def _require_json_content_type(header: Optional[str]) -> None:
    if header is not None:
        media_type, *params = header.split(";")
        if media_type.strip().lower() == "application/json":
            charset = None
            for param in params:
                name, _, value = param.partition("=")
                if name.strip().lower() == "charset":
                    charset = value.strip().strip('"').lower()
            if charset in (None, "utf-8", "utf8"):
                return
    raise ApiError(
        415,
        "unsupported_media_type",
        "Send the request body as UTF-8 JSON with the header Content-Type: application/json.",
    )


def _reject_query(query: str) -> None:
    if query:
        raise ApiError(400, "unexpected_query", "This endpoint does not accept query parameters.")


def _parse_revision_query(query: str) -> int:
    if not query:
        raise ApiError(
            400, "missing_revision", "Include the board revision in the URL, for example ?revision=3."
        )
    try:
        pairs = parse_qsl(query, keep_blank_values=True, strict_parsing=True, max_num_fields=8)
    except ValueError:
        raise ApiError(400, "invalid_query", "The query string is malformed.") from None
    unknown = sorted({name for name, _ in pairs if name != "revision"})
    if unknown:
        raise ApiError(
            400,
            "unknown_query_parameters",
            f"Unknown query parameter(s): {_describe(unknown)}. Only revision is supported.",
        )
    values = [value for _, value in pairs]
    if len(values) != 1 or not _REVISION_TEXT.fullmatch(values[0]):
        raise ApiError(
            400, "invalid_revision", "revision must be given once, as a non-negative whole number."
        )
    return int(values[0])


_PROTOCOL_ERRORS = {
    400: ("bad_request", "The HTTP request is malformed."),
    408: ("request_timeout", "The request took too long to arrive."),
    414: ("uri_too_long", "The request URL is too long."),
    431: ("headers_too_large", "The request headers are too large."),
    501: ("unsupported_method", "This HTTP method is not supported."),
    505: ("http_version_not_supported", "This HTTP version is not supported."),
}


class MinesweeperRequestHandler(BaseHTTPRequestHandler):
    server: "MinesweeperServer"
    server_version = "Minesweeper"
    protocol_version = "HTTP/1.0"
    # Seconds of socket inactivity before an idle or slow connection is dropped.
    timeout = 30

    _body_consumed = True
    _response_started = False

    def do_GET(self) -> None:
        self._handle()

    do_HEAD = do_POST = do_PUT = do_PATCH = do_DELETE = do_OPTIONS = do_GET

    # -------------------------------------------------------------- dispatch

    def _handle(self) -> None:
        self._body_consumed = False
        self._response_started = False
        path, _, query = self.path.partition("?")
        try:
            self._route(self.command, path, query)
        except GameError as exc:
            self._send_error(_api_error_from_game_error(exc))
        except ApiError as exc:
            self._send_error(exc)
        except (ConnectionError, socket.timeout) as exc:
            self.close_connection = True
            logger.info("Connection from %s ended early (%s)", self.address_string(), type(exc).__name__)
        except Exception:
            self.close_connection = True
            logger.exception("Unhandled error while serving %s %s", self.command, path)
            if not self._response_started:
                try:
                    self._send_error(ApiError(
                        500, "internal_error", "Something went wrong on the server. Please try again."
                    ))
                except OSError:
                    pass

    def _route(self, method: str, path: str, query: str) -> None:
        static = STATIC_ROUTES.get(path)
        if static is not None:
            self._allow(method, "GET", "HEAD")
            self._serve_static(*static)
            return
        if path == "/healthz":
            self._allow(method, "GET", "HEAD")
            self._send_json(200, {"status": "ok"})
            return
        if path == "/api/games":
            self._allow(method, "POST")
            _reject_query(query)
            payload = self._read_json_object()
            _require_fields(payload, CREATE_FIELDS)
            entry = self.server.store.create(payload["width"], payload["height"], payload["mines"])
            self._send_json(201, entry.state(), headers=[("Location", f"/api/games/{entry.game_id}")])
            return
        match = _GAME_ROUTE.fullmatch(path)
        if match is None:
            raise ApiError(404, "not_found", "There is nothing at this address.")
        game_id, resource = match.groups()
        if resource is None:
            self._allow(method, "GET", "HEAD")
            _reject_query(query)
            self._send_json(200, self.server.store.get(game_id).state())
        elif resource == "actions":
            self._allow(method, "POST")
            _reject_query(query)
            entry = self.server.store.get(game_id)
            payload = self._read_json_object()
            _require_fields(payload, ACTION_FIELDS)
            state = entry.act(payload["action"], payload["row"], payload["col"], payload["revision"])
            self._send_json(200, state)
        elif resource == "autosolve":
            self._allow(method, "POST")
            _reject_query(query)
            entry = self.server.store.get(game_id)
            payload = self._read_json_object()
            _require_fields(payload, AUTOSOLVE_FIELDS)
            revision = payload["revision"]
            if not _is_int(revision) or revision < 0:
                raise ApiError(400, "invalid_revision", "revision must be a non-negative whole number.")
            odds = self.server.probabilities.probabilities(entry, revision)
            # Recheck the revision under the game lock: a move may have happened
            # while solving. Only proofs are actionable, never sampled endpoints.
            state = entry.apply_deductions(odds["proven_safe"], odds["proven_mines"], revision)
            self._send_json(200, state)
        else:
            self._allow(method, "GET", "HEAD")
            entry = self.server.store.get(game_id)
            revision = _parse_revision_query(query)
            self._send_json(200, self.server.probabilities.probabilities(entry, revision))

    @staticmethod
    def _allow(method: str, *allowed: str) -> None:
        if method not in allowed:
            raise ApiError(
                405,
                "method_not_allowed",
                f"{method} is not supported here; use {' or '.join(allowed)}.",
                headers=[("Allow", ", ".join(allowed))],
            )

    def _serve_static(self, filename: str, content_type: str) -> None:
        try:
            body = (self.server.static_dir / filename).read_bytes()
        except (FileNotFoundError, NotADirectoryError):
            raise ApiError(404, "not_found", "This file is not available.") from None
        self._send(200, body, content_type, STATIC_CACHE_CONTROL)

    # ---------------------------------------------------------- request body

    def _declared_length(self) -> Optional[int]:
        values = self.headers.get_all("Content-Length") or []
        if not values:
            return None
        if len(values) != 1 or not _CONTENT_LENGTH_TEXT.fullmatch(values[0].strip()):
            self.close_connection = True
            raise ApiError(400, "invalid_content_length", "Content-Length must be a single whole number.")
        return int(values[0].strip())

    def _read_json_object(self) -> Dict[str, Any]:
        if self.headers.get("Transfer-Encoding") is not None:
            self.close_connection = True
            raise ApiError(
                411, "length_required", "Send the body with a Content-Length header; chunked uploads are not supported."
            )
        length = self._declared_length()
        if length is None:
            raise ApiError(411, "length_required", "A Content-Length header is required.")
        if length > MAX_BODY_BYTES:
            raise ApiError(413, "payload_too_large", f"The request body must be at most {MAX_BODY_BYTES} bytes.")
        _require_json_content_type(self.headers.get("Content-Type"))
        self._body_consumed = True
        body = self.rfile.read(length) if length else b""
        if len(body) != length:
            self.close_connection = True
            raise ApiError(400, "incomplete_body", "The request body is shorter than its Content-Length.")
        return _decode_json_object(body)

    def _drain_body(self) -> None:
        """Consume an unread request body so an early reply is not lost to a reset."""
        if self._body_consumed:
            return
        self._body_consumed = True
        if self.headers.get("Transfer-Encoding") is not None:
            self.close_connection = True
            return
        values = self.headers.get_all("Content-Length") or []
        if not values:
            return
        if len(values) != 1 or not _CONTENT_LENGTH_TEXT.fullmatch(values[0].strip()):
            self.close_connection = True
            return
        remaining = int(values[0].strip())
        if remaining > MAX_DRAIN_BYTES:
            self.close_connection = True
            return
        while remaining > 0:
            chunk = self.rfile.read(min(remaining, 64 * 1024))
            if not chunk:
                break
            remaining -= len(chunk)

    # -------------------------------------------------------------- responses

    def _send_json(self, status: int, payload: Any, *, headers: Iterable[Tuple[str, str]] = ()) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
        self._send(status, body.encode("utf-8"), JSON_CONTENT_TYPE, API_CACHE_CONTROL, headers)

    def _send_error(self, error: ApiError) -> None:
        self._send_json(error.status, error.payload(), headers=error.headers)

    def _send(
        self,
        status: int,
        body: bytes,
        content_type: str,
        cache_control: str,
        headers: Iterable[Tuple[str, str]] = (),
    ) -> None:
        self._drain_body()
        self._response_started = True
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache_control)
        self.send_header("X-Content-Type-Options", "nosniff")
        for name, value in headers:
            self.send_header(name, value)
        if self.close_connection:
            self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_error(self, code: int, message: Optional[str] = None, explain: Optional[str] = None) -> None:
        """JSON replacement for the stdlib's HTML errors (malformed requests etc.)."""
        self.log_error("code %d, message %s", code, message)
        self.close_connection = True
        error_code, text = _PROTOCOL_ERRORS.get(code, ("http_error", "The request could not be processed."))
        try:
            self._send_json(code, {"error": {"code": error_code, "message": text}})
        except OSError:
            pass

    def version_string(self) -> str:
        return self.server_version

    def log_message(self, format: str, *args: Any) -> None:
        logger.info("%s %s", self.address_string(), format % args)

    def log_error(self, format: str, *args: Any) -> None:
        logger.warning("%s %s", self.address_string(), format % args)


class MinesweeperServer(ThreadingHTTPServer):
    """Threaded HTTP server that owns one game store and probability service."""

    daemon_threads = True

    def __init__(
        self,
        server_address: Tuple[str, int],
        *,
        store: GameStore,
        probabilities: ProbabilityService,
        static_dir: Any = STATIC_DIR,
    ) -> None:
        self.store = store
        self.probabilities = probabilities
        self.static_dir = static_dir
        if ":" in server_address[0]:
            self.address_family = socket.AF_INET6
        super().__init__(server_address, MinesweeperRequestHandler)

    def server_bind(self) -> None:
        # Skip HTTPServer's reverse DNS lookup (socket.getfqdn), which can stall.
        socketserver.TCPServer.server_bind(self)
        host, port = self.server_address[:2]
        self.server_name = host
        self.server_port = port

    def handle_error(self, request: Any, client_address: Any) -> None:
        logger.exception("Unhandled error while serving a request from %s", client_address)


def create_server(
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    *,
    store: Optional[GameStore] = None,
    probability_service: Optional[ProbabilityService] = None,
    static_dir: Any = None,
) -> MinesweeperServer:
    """Create (but do not start) a server; ``port=0`` picks a free port.

    Call ``serve_forever()`` to run it and ``shutdown()`` plus
    ``server_close()`` to stop it.
    """
    if isinstance(static_dir, (str, os.PathLike)):
        static_dir = Path(static_dir)
    return MinesweeperServer(
        (host, port),
        store=store if store is not None else GameStore(),
        probabilities=probability_service if probability_service is not None else ProbabilityService(),
        static_dir=static_dir if static_dir is not None else STATIC_DIR,
    )


# ----------------------------------------------------------------------- CLI


def _port(text: str) -> int:
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError("port must be a whole number") from None
    if not 0 <= value <= 65535:
        raise argparse.ArgumentTypeError("port must be between 0 and 65535")
    return value


def _is_loopback(host: str) -> bool:
    if host.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def _display_url(host: str, port: int) -> str:
    if host in ("", "0.0.0.0", "::"):
        host = "localhost"
    elif ":" in host:
        host = f"[{host}]"
    return f"http://{host}:{port}/"


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Play Minesweeper in your browser.")
    parser.add_argument(
        "--host",
        default=DEFAULT_HOST,
        help="address to listen on (default: %(default)s, this computer only). "
        "Other addresses such as 0.0.0.0 expose the unauthenticated game to your network.",
    )
    parser.add_argument(
        "--port", type=_port, default=DEFAULT_PORT, help="port to listen on (default: %(default)s)"
    )
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    try:
        server = create_server(args.host, args.port)
    except OSError as exc:
        logger.error("Could not listen on %s port %d: %s", args.host, args.port, exc.strerror or exc)
        return 1
    with server:
        if not _is_loopback(args.host):
            logger.warning(
                "Listening on %s: other devices on your network may reach this server, "
                "which has no authentication. Only do this on a trusted network.",
                args.host,
            )
        logger.info(
            "Minesweeper is running at %s (press Ctrl+C to stop)",
            _display_url(args.host, server.server_address[1]),
        )
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            logger.info("Shutting down")
    return 0


if __name__ == "__main__":
    sys.exit(main())
