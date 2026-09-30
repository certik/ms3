"""Authoritative Minesweeper rules engine and a thread-safe in-memory game store.

The engine is the only component that knows where the mines are. Callers only
ever receive :meth:`Game.public_state` (for clients) or
:meth:`Game.revealed_clues` (for the probability solver); neither exposes mine
positions or unrevealed clue values while a game is in progress.
"""

from __future__ import annotations

import logging
import random
import re
import secrets
import threading
import time
from collections import OrderedDict, deque
from typing import Any, Callable, Dict, List, NamedTuple, Optional, Tuple

logger = logging.getLogger(__name__)

MIN_DIMENSION = 5
MAX_DIMENSION = 80
MIN_MINES = 1
# The first revealed cell and its (up to eight) neighbours never hold a mine,
# so at least this many cells must stay mine-free.
SAFE_START_CELLS = 9

STATUS_READY = "ready"
STATUS_PLAYING = "playing"
STATUS_WON = "won"
STATUS_LOST = "lost"
TERMINAL_STATUSES = frozenset({STATUS_WON, STATUS_LOST})

ACTION_REVEAL = "reveal"
ACTION_FLAG = "flag"
ACTION_CHORD = "chord"
ACTIONS = (ACTION_REVEAL, ACTION_FLAG, ACTION_CHORD)

DEFAULT_MAX_GAMES = 256
DEFAULT_IDLE_TTL_SECONDS = 24 * 60 * 60

GAME_ID_PATTERN = re.compile(r"[A-Za-z0-9_-]{1,64}")


class GameError(Exception):
    """A rejected request. ``code`` is a stable snake_case identifier."""

    code = "game_error"

    def __init__(self, message: str, *, code: Optional[str] = None) -> None:
        super().__init__(message)
        self.message = message
        if code is not None:
            self.code = code
        # Latest public state; attached by GameEntry to conflict errors.
        self.state: Optional[Dict[str, Any]] = None


class InvalidConfigError(GameError):
    code = "invalid_config"


class InvalidActionError(GameError):
    code = "invalid_action"


class StaleRevisionError(GameError):
    code = "stale_revision"

    def __init__(self, expected: int, current: int) -> None:
        super().__init__(
            f"The board changed since revision {expected}; it is now at revision "
            f"{current}. The latest state is included."
        )
        self.expected = expected
        self.current = current


class GameOverError(GameError):
    code = "game_over"


class GameNotFoundError(GameError):
    code = "game_not_found"

    def __init__(self) -> None:
        super().__init__(
            "Game not found. It may have expired or been removed; start a new game."
        )


def _is_int(value: Any) -> bool:
    # bool is a subclass of int; JSON true/false must never pass as a number.
    return isinstance(value, int) and not isinstance(value, bool)


def max_mines(width: int, height: int) -> int:
    """Largest mine count that still leaves room for a fully safe 3x3 start."""
    return width * height - SAFE_START_CELLS


def validate_config(width: Any, height: Any, mines: Any) -> None:
    """Raise :class:`InvalidConfigError` unless the board settings are valid."""
    for name, value in (("width", width), ("height", height)):
        if not _is_int(value) or not MIN_DIMENSION <= value <= MAX_DIMENSION:
            raise InvalidConfigError(
                f"{name} must be a whole number from {MIN_DIMENSION} to {MAX_DIMENSION}.",
                code=f"invalid_{name}",
            )
    limit = max_mines(width, height)
    if not _is_int(mines) or not MIN_MINES <= mines <= limit:
        raise InvalidConfigError(
            f"mines must be a whole number from {MIN_MINES} to {limit} for a "
            f"{width}x{height} board (the first revealed cell and its neighbors "
            "are always safe).",
            code="invalid_mines",
        )


def new_game_id() -> str:
    """Opaque, unguessable identifier (128 bits of randomness)."""
    return secrets.token_urlsafe(16)


class Game:
    """A single game of Minesweeper.

    Not thread-safe: callers must serialize access (``GameStore`` does this).
    Mines are placed lazily on the first successful reveal, uniformly among
    all cells outside the clicked cell's clipped 3x3 neighborhood.
    """

    def __init__(
        self,
        width: Any,
        height: Any,
        mines: Any,
        *,
        game_id: Optional[str] = None,
        rng: Optional[random.Random] = None,
        clock: Optional[Callable[[], float]] = None,
    ) -> None:
        validate_config(width, height, mines)
        self.id = game_id if game_id is not None else new_game_id()
        self.width: int = width
        self.height: int = height
        self.mines: int = mines
        self.status = STATUS_READY
        self.revision = 0
        self.flags = 0
        self._rng = rng if rng is not None else random.SystemRandom()
        self._clock = clock if clock is not None else time.monotonic
        size = width * height
        self._is_mine = bytearray(size)
        self._adjacent = bytearray(size)
        self._revealed = bytearray(size)
        self._flagged = bytearray(size)
        self._mines_placed = False
        self._exploded: set = set()
        self._hidden_safe = size - mines
        self._started_at: Optional[float] = None
        self._finished_at: Optional[float] = None

    def __repr__(self) -> str:
        # Deliberately excludes the layout so games are safe to log.
        return (
            f"<Game {self.id} {self.width}x{self.height} mines={self.mines} "
            f"status={self.status} revision={self.revision}>"
        )

    @property
    def size(self) -> int:
        return self.width * self.height

    @property
    def is_over(self) -> bool:
        return self.status in TERMINAL_STATUSES

    @property
    def elapsed_seconds(self) -> float:
        """Seconds since the first reveal; frozen once the game is won or lost."""
        if self._started_at is None:
            return 0.0
        end = self._finished_at if self._finished_at is not None else self._clock()
        return round(max(0.0, end - self._started_at), 3)

    # ----------------------------------------------------------------- actions

    def apply_action(self, action: Any, row: Any, col: Any, expected_revision: Any) -> bool:
        """Validate and apply one action; return whether the game changed.

        All validation happens before any mutation, so a rejected request never
        alters the game. The revision increases by exactly one for every
        state-changing action; no-ops (for example revealing a flagged cell)
        leave it unchanged.
        """
        handler = self._HANDLERS.get(action) if isinstance(action, str) else None
        if handler is None:
            raise InvalidActionError(
                "action must be one of: " + ", ".join(ACTIONS) + ".",
                code="invalid_action",
            )
        index = self._checked_index(row, col)
        if not _is_int(expected_revision) or expected_revision < 0:
            raise InvalidActionError(
                "revision must be a non-negative whole number.", code="invalid_revision"
            )
        if expected_revision != self.revision:
            raise StaleRevisionError(expected_revision, self.revision)
        if self.is_over:
            outcome = "won" if self.status == STATUS_WON else "lost"
            raise GameOverError(
                f"This game is already over (you {outcome}). Start a new game to keep playing."
            )
        changed = handler(self, index)
        if changed:
            self.revision += 1
        return changed

    def reveal(self, row: int, col: int) -> bool:
        return self.apply_action(ACTION_REVEAL, row, col, self.revision)

    def toggle_flag(self, row: int, col: int) -> bool:
        return self.apply_action(ACTION_FLAG, row, col, self.revision)

    def chord(self, row: int, col: int) -> bool:
        return self.apply_action(ACTION_CHORD, row, col, self.revision)

    def _checked_index(self, row: Any, col: Any) -> int:
        if not _is_int(row) or not _is_int(col):
            raise InvalidActionError(
                "row and col must be whole numbers.", code="invalid_coordinates"
            )
        if not (0 <= row < self.height and 0 <= col < self.width):
            raise InvalidActionError(
                f"Cell (row {row}, col {col}) is off the board: row must be 0-"
                f"{self.height - 1} and col must be 0-{self.width - 1}.",
                code="out_of_bounds",
            )
        return row * self.width + col

    def _reveal(self, index: int) -> bool:
        if self._revealed[index] or self._flagged[index]:
            return False
        if not self._mines_placed:
            self._place_mines(index)
        if self._is_mine[index]:
            self._explode([index])
        else:
            self._flood(index)
            self._check_win()
        return True

    def _toggle_flag(self, index: int) -> bool:
        if self._revealed[index]:
            return False
        if self._flagged[index]:
            self._flagged[index] = 0
            self.flags -= 1
        else:
            self._flagged[index] = 1
            self.flags += 1
        return True

    def _chord(self, index: int) -> bool:
        if not self._revealed[index] or self._is_mine[index]:
            return False
        neighbors = self._neighbors(index)
        if sum(self._flagged[n] for n in neighbors) != self._adjacent[index]:
            return False
        targets = [n for n in neighbors if not self._revealed[n] and not self._flagged[n]]
        if not targets:
            return False
        # Wrong flags mean some targets are mines: they detonate like any reveal.
        hit = [n for n in targets if self._is_mine[n]]
        for n in targets:
            if not self._is_mine[n]:
                self._flood(n)
        if hit:
            self._explode(hit)
        else:
            self._check_win()
        return True

    _HANDLERS = {
        ACTION_REVEAL: _reveal,
        ACTION_FLAG: _toggle_flag,
        ACTION_CHORD: _chord,
    }

    # ------------------------------------------------------------------ rules

    def _neighbors(self, index: int) -> List[int]:
        row, col = divmod(index, self.width)
        result = []
        for r in range(max(row - 1, 0), min(row + 2, self.height)):
            base = r * self.width
            for c in range(max(col - 1, 0), min(col + 2, self.width)):
                if r != row or c != col:
                    result.append(base + c)
        return result

    def _place_mines(self, first: int) -> None:
        excluded = set(self._neighbors(first))
        excluded.add(first)
        candidates = [i for i in range(self.size) if i not in excluded]
        chosen = list(self._rng.sample(candidates, self.mines))
        # Guard against a misbehaving injected RNG before mutating anything.
        if len(chosen) != self.mines or len(set(chosen)) != self.mines or any(
            not _is_int(i) or i in excluded or not 0 <= i < self.size for i in chosen
        ):
            raise RuntimeError("random source returned an invalid mine sample")
        for i in chosen:
            self._is_mine[i] = 1
            for n in self._neighbors(i):
                self._adjacent[n] += 1
        self._mines_placed = True
        self.status = STATUS_PLAYING
        self._started_at = self._clock()

    def _flood(self, start: int) -> None:
        """Reveal ``start`` and, iteratively, the region around zero clues."""
        if self._revealed[start] or self._flagged[start] or self._is_mine[start]:
            return
        self._revealed[start] = 1
        self._hidden_safe -= 1
        queue = deque([start])
        while queue:
            index = queue.popleft()
            if self._adjacent[index]:
                continue
            # Neighbors of a zero are never mines; flagged cells stay hidden.
            for n in self._neighbors(index):
                if not self._revealed[n] and not self._flagged[n]:
                    self._revealed[n] = 1
                    self._hidden_safe -= 1
                    queue.append(n)

    def _explode(self, indices: List[int]) -> None:
        for i in indices:
            self._revealed[i] = 1
            self._exploded.add(i)
        self.status = STATUS_LOST
        self._finished_at = self._clock()

    def _check_win(self) -> None:
        if self._hidden_safe:
            return
        self.status = STATUS_WON
        self._finished_at = self._clock()
        # Classic behavior: flag every mine on a win so the mine counter reads 0.
        # All existing flags are necessarily on mines here, because flagged
        # cells can never be revealed and every safe cell has been revealed.
        for i in range(self.size):
            if self._is_mine[i] and not self._flagged[i]:
                self._flagged[i] = 1
                self.flags += 1

    # ---------------------------------------------------------- serialization

    def public_state(self) -> Dict[str, Any]:
        """JSON-ready view that never leaks hidden information mid-game.

        ``adjacent`` is only set for revealed safe cells and ``mine`` stays
        ``None`` for every cell until the game is won or lost.
        """
        terminal = self.is_over
        exploded = self._exploded
        cells = []
        append = cells.append
        for i in range(self.size):
            revealed = self._revealed[i] == 1
            mine = self._is_mine[i] == 1
            append({
                "revealed": revealed,
                "flagged": self._flagged[i] == 1,
                "adjacent": self._adjacent[i] if revealed and not mine else None,
                "mine": mine if terminal else None,
                "exploded": i in exploded,
            })
        return {
            "id": self.id,
            "width": self.width,
            "height": self.height,
            "mines": self.mines,
            "status": self.status,
            "revision": self.revision,
            "flags": self.flags,
            "elapsed_seconds": self.elapsed_seconds,
            "cells": cells,
        }

    def revealed_clues(self) -> Dict[int, int]:
        """Flat index -> clue for every publicly revealed safe cell.

        This is the only evidence the probability solver may use: it contains
        no flags and nothing derived from the hidden layout beyond what the
        player can already see.
        """
        return {
            i: self._adjacent[i]
            for i in range(self.size)
            if self._revealed[i] and not self._is_mine[i]
        }


class Snapshot(NamedTuple):
    """Public, immutable view of a game taken under its lock for solving."""

    game_id: str
    revision: int
    status: str
    width: int
    height: int
    mines: int
    revealed: Optional[Dict[int, int]]
    cached_probabilities: Optional[Dict[str, Any]]


class GameEntry:
    """A stored game plus the locks and probability cache that belong to it."""

    __slots__ = ("_game", "_lock", "solver_lock", "_probability_cache", "last_access")

    def __init__(self, game: Game, now: float) -> None:
        self._game = game
        self._lock = threading.Lock()
        # Serializes solver runs for this game so identical requests coalesce.
        self.solver_lock = threading.Lock()
        self._probability_cache: Optional[Tuple[int, Dict[str, Any]]] = None
        self.last_access = now

    @property
    def game_id(self) -> str:
        return self._game.id

    def state(self) -> Dict[str, Any]:
        with self._lock:
            return self._game.public_state()

    def act(self, action: Any, row: Any, col: Any, expected_revision: Any) -> Dict[str, Any]:
        """Apply an action atomically and return the resulting public state.

        Stale-revision and game-over errors carry the latest state in ``state``.
        """
        with self._lock:
            try:
                changed = self._game.apply_action(action, row, col, expected_revision)
            except (StaleRevisionError, GameOverError) as exc:
                exc.state = self._game.public_state()
                raise
            if changed:
                self._probability_cache = None
            return self._game.public_state()

    def snapshot(self, *, include_observation: bool = False) -> Snapshot:
        with self._lock:
            game = self._game
            cached = None
            if self._probability_cache is not None and self._probability_cache[0] == game.revision:
                cached = self._probability_cache[1]
            revealed = None
            if include_observation and game.status == STATUS_PLAYING:
                revealed = game.revealed_clues()
            return Snapshot(
                game_id=game.id,
                revision=game.revision,
                status=game.status,
                width=game.width,
                height=game.height,
                mines=game.mines,
                revealed=revealed,
                cached_probabilities=cached,
            )

    def cache_probabilities(self, revision: int, payload: Dict[str, Any]) -> None:
        """Remember a result, but only while it still matches the live revision."""
        with self._lock:
            if self._game.revision == revision:
                self._probability_cache = (revision, payload)


class GameStore:
    """Thread-safe, bounded, in-memory collection of games.

    Games are kept in least-recently-used order. A game expires after
    ``idle_ttl`` seconds without being accessed, and creating a game while
    ``max_games`` are stored evicts the least recently used one.
    """

    def __init__(
        self,
        *,
        max_games: int = DEFAULT_MAX_GAMES,
        idle_ttl: float = DEFAULT_IDLE_TTL_SECONDS,
        clock: Callable[[], float] = time.monotonic,
        rng_factory: Optional[Callable[[], random.Random]] = None,
    ) -> None:
        if not _is_int(max_games) or max_games < 1:
            raise ValueError("max_games must be a positive integer")
        if not idle_ttl > 0:
            raise ValueError("idle_ttl must be positive")
        self._max_games = max_games
        self._idle_ttl = idle_ttl
        self._clock = clock
        self._rng_factory = rng_factory
        self._entries: "OrderedDict[str, GameEntry]" = OrderedDict()
        self._lock = threading.Lock()

    def __len__(self) -> int:
        with self._lock:
            self._expire(self._clock())
            return len(self._entries)

    def create(self, width: Any, height: Any, mines: Any) -> GameEntry:
        validate_config(width, height, mines)
        rng = self._rng_factory() if self._rng_factory is not None else None
        with self._lock:
            now = self._clock()
            self._expire(now)
            game_id = new_game_id()
            while game_id in self._entries:
                game_id = new_game_id()
            game = Game(width, height, mines, game_id=game_id, rng=rng, clock=self._clock)
            while len(self._entries) >= self._max_games:
                evicted, _ = self._entries.popitem(last=False)
                logger.info("Evicted least recently used game %s (store full)", evicted)
            entry = GameEntry(game, now)
            self._entries[game_id] = entry
            return entry

    def get(self, game_id: Any) -> GameEntry:
        if not isinstance(game_id, str) or not GAME_ID_PATTERN.fullmatch(game_id):
            raise GameNotFoundError()
        with self._lock:
            now = self._clock()
            self._expire(now)
            entry = self._entries.get(game_id)
            if entry is None:
                raise GameNotFoundError()
            entry.last_access = now
            self._entries.move_to_end(game_id)
            return entry

    def _expire(self, now: float) -> None:
        # Entries are ordered by last access, so expired ones are at the front.
        while self._entries:
            game_id, entry = next(iter(self._entries.items()))
            if now - entry.last_access < self._idle_ttl:
                break
            del self._entries[game_id]
            logger.info("Expired idle game %s", game_id)
