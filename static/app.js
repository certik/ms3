/*
 * Minesweeper front end.
 *
 * Vanilla JavaScript modules with no build step and no dependencies. The game
 * runs locally: engine-client.js drives the C engine compiled to WebAssembly,
 * with the authoritative game in this page's main thread and the mine-odds
 * solver in a separate worker. Each tab plays its own game; reloading starts
 * a new one. Only the difficulty, zoom, mine-odds and autosolve preferences
 * are stored.
 *
 * Moves and odds are independent: a move never waits for the solver. Odds are
 * only drawn when they belong to the game AND revision on screen, and every
 * odds request carries a generation number so late answers are ignored even
 * if cancelling the solver came too late.
 */
import { EngineClient, EngineError } from './engine-client.js';

(function () {
  'use strict';

  // ----------------------------------------------------------------- constants

  const PRESETS = {
    beginner: { label: 'Beginner', width: 9, height: 9, mines: 10 },
    intermediate: { label: 'Intermediate', width: 16, height: 16, mines: 40 },
    expert: { label: 'Expert', width: 30, height: 16, mines: 99 },
    large: { label: 'Large', width: 50, height: 30, mines: 225 }
  };
  const DEFAULT_PRESET = 'beginner';
  const MIN_SIDE = 5;
  const MAX_SIDE = 80;
  const SAFE_AREA = 9; // the first reveal and its neighbors never hold mines
  const ZOOM_STEPS = [22, 26, 30, 34, 40, 46];
  const CELL_GAP = 2;
  const PAGE_ROWS = 5;
  const LONG_PRESS_MS = 450;
  const TOUCH_SLOP_PX = 10;
  const HINT_MS = 4000;
  const STORAGE_KEYS = {
    settings: 'minesweeper.settings',
    prefs: 'minesweeper.prefs'
  };
  // The former server version remembered its game here; games are no longer
  // saved, so the stale id is removed.
  const LEGACY_GAME_KEY = 'minesweeper.gameId';
  const GAME_STATUSES = ['ready', 'playing', 'won', 'lost'];
  const ODDS_STATUSES = ['exact', 'approximate', 'unavailable', 'not-started', 'finished'];
  const READY_TEXT = 'Odds are not defined yet: the mines are placed when you make your first reveal. ' +
    'That cell and its neighbors are always safe, so any first click is fine.';

  const ICON = {
    flag: svgIcon('i-flag'),
    wrongFlag: svgIcon('i-flag-wrong'),
    mine: svgIcon('i-mine'),
    cornerFlag: svgIcon('i-flag', 'corner-flag'),
    provenSafe: svgIcon('i-check', 'odds-icon'),
    provenMine: svgIcon('i-mine', 'odds-icon')
  };

  // --------------------------------------------------------------------- state

  const app = {
    game: null, // latest adopted GameState (normalized)
    settings: { preset: DEFAULT_PRESET, width: 9, height: 9, mines: 10 },
    customOpen: false,
    flagMode: false,
    zoom: { mode: 'auto', index: 2 },
    cellSize: 0,
    focusIndex: 0, // roving tabindex position
    hoverIndex: -1,
    pressing: false,
    // Local engine: loading -> ready, or error. `fatal` marks a game engine
    // that stopped while a game was on screen (the board stays visible but
    // frozen until a new game starts a fresh engine instance).
    engine: { state: 'loading', error: null, fatal: false },
    pendingStart: null, // { config, opts } requested while the engine loads
    loadError: null, // shown when there is no game at all
    notice: null,
    hintTimer: 0,
    hintActive: false,
    timerHandle: 0,
    timerText: '',
    fitFrame: 0,
    autosolve: { enabled: false, timer: 0, attemptedKey: null },
    odds: {
      enabled: false,
      gen: 0, // bumped whenever an in-flight request becomes obsolete
      requestKey: null,
      startedAt: 0,
      ticker: 0,
      data: null, // parsed odds valid for dataKey only
      dataKey: null,
      cache: null, // last good answer, reused while the position is unchanged
      error: null,
      discardedKey: null,
      announceNext: false
    }
  };

  const engine = new EngineClient();
  const boardView = { width: 0, height: 0, cells: [], sigs: [] };
  const touch = { timer: 0, index: -1, x: 0, y: 0, pointerId: null, fired: false };
  let lastPointerType = 'mouse';
  const el = {};

  // ----------------------------------------------------------------- utilities

  function svgIcon(id, extraClass) {
    return '<svg class="icon' + (extraClass ? ' ' + extraClass : '') +
      '" aria-hidden="true" focusable="false"><use href="#' + id + '"></use></svg>';
  }

  function isInt(value) {
    return typeof value === 'number' && Number.isInteger(value);
  }

  function isNum(value) {
    return typeof value === 'number' && isFinite(value);
  }

  function clamp(value, low, high) {
    return Math.min(high, Math.max(low, value));
  }

  function plural(count, one, many) {
    return formatCount(count) + ' ' + (count === 1 ? one : (many || one + 's'));
  }

  function formatCount(value) {
    return Number(value).toLocaleString('en-US');
  }

  function pad2(value) {
    return value < 10 ? '0' + value : String(value);
  }

  function formatClock(seconds) {
    const total = Math.max(0, Math.floor(seconds));
    const hours = Math.floor(total / 3600);
    const minutes = Math.floor((total % 3600) / 60);
    const secs = total % 60;
    return hours ? hours + ':' + pad2(minutes) + ':' + pad2(secs) : minutes + ':' + pad2(secs);
  }

  function formatDurationWords(seconds) {
    const total = Math.max(0, Math.floor(seconds));
    const minutes = Math.floor(total / 60);
    const secs = total % 60;
    if (!minutes) return plural(secs, 'second');
    return plural(minutes, 'minute') + (secs ? ' ' + plural(secs, 'second') : '');
  }

  function formatMs(ms) {
    if (ms < 1) return '<1 ms';
    if (ms < 1000) return Math.round(ms) + ' ms';
    return (ms / 1000).toFixed(ms < 10000 ? 1 : 0) + ' s';
  }

  function presetKeyFor(width, height, mines) {
    const keys = Object.keys(PRESETS);
    for (let k = 0; k < keys.length; k++) {
      const p = PRESETS[keys[k]];
      if (p.width === width && p.height === height && p.mines === mines) return keys[k];
    }
    return 'custom';
  }

  function configName(config) {
    const key = presetKeyFor(config.width, config.height, config.mines);
    return key === 'custom' ? 'Custom' : PRESETS[key].label;
  }

  function describeConfig(config) {
    return config.width + ' x ' + config.height + ', ' + plural(config.mines, 'mine');
  }

  function maxMines(width, height) {
    return width * height - SAFE_AREA;
  }

  function oddsKey(game) {
    return game.id + '#' + game.revision;
  }

  function neighborsOf(index, width, height) {
    const row = Math.floor(index / width);
    const col = index % width;
    const result = [];
    for (let dr = -1; dr <= 1; dr++) {
      for (let dc = -1; dc <= 1; dc++) {
        if (!dr && !dc) continue;
        const r = row + dr;
        const c = col + dc;
        if (r >= 0 && r < height && c >= 0 && c < width) result.push(r * width + c);
      }
    }
    return result;
  }

  function positionText(index, width) {
    return 'row ' + (Math.floor(index / width) + 1) + ', column ' + ((index % width) + 1);
  }

  function isTerminal(game) {
    return !!game && (game.status === 'won' || game.status === 'lost');
  }

  // ------------------------------------------------------------------- storage

  const store = {
    get: function (key) {
      try { return window.localStorage.getItem(key); } catch (e) { return null; }
    },
    set: function (key, value) {
      try { window.localStorage.setItem(key, value); } catch (e) { /* storage unavailable */ }
    },
    remove: function (key) {
      try { window.localStorage.removeItem(key); } catch (e) { /* storage unavailable */ }
    },
    getJSON: function (key) {
      const raw = this.get(key);
      if (!raw) return null;
      try { return JSON.parse(raw); } catch (e) { return null; }
    },
    setJSON: function (key, value) {
      this.set(key, JSON.stringify(value));
    }
  };

  function validateConfig(width, height, mines) {
    const errors = {};
    const sideText = 'Enter a whole number from ' + MIN_SIDE + ' to ' + MAX_SIDE + '.';
    if (!isInt(width) || width < MIN_SIDE || width > MAX_SIDE) errors.width = sideText;
    if (!isInt(height) || height < MIN_SIDE || height > MAX_SIDE) errors.height = sideText;
    if (!isInt(mines) || mines < 1) {
      errors.mines = 'Enter a whole number of at least 1.';
    } else if (!errors.width && !errors.height && mines > maxMines(width, height)) {
      errors.mines = 'At most ' + formatCount(maxMines(width, height)) + ' mines fit on a ' + width + ' x ' +
        height + ' board, because the first reveal keeps 9 cells free.';
    }
    return errors;
  }

  function parseWholeNumber(text) {
    const trimmed = String(text).trim();
    return /^\d{1,6}$/.test(trimmed) ? Number(trimmed) : NaN;
  }

  function loadSettings() {
    const saved = store.getJSON(STORAGE_KEYS.settings);
    if (saved && typeof saved === 'object') {
      const errors = validateConfig(saved.width, saved.height, saved.mines);
      if (!Object.keys(errors).length) {
        app.settings = {
          preset: presetKeyFor(saved.width, saved.height, saved.mines),
          width: saved.width,
          height: saved.height,
          mines: saved.mines
        };
      }
    }
  }

  function saveSettings() {
    store.setJSON(STORAGE_KEYS.settings, {
      width: app.settings.width,
      height: app.settings.height,
      mines: app.settings.mines
    });
  }

  function loadPrefs() {
    const prefs = store.getJSON(STORAGE_KEYS.prefs) || {};
    if (isInt(prefs.zoom) && prefs.zoom >= 0 && prefs.zoom < ZOOM_STEPS.length) {
      app.zoom = { mode: 'manual', index: prefs.zoom };
    }
    app.autosolve.enabled = prefs.autosolve === true;
    app.odds.enabled = prefs.odds === true || app.autosolve.enabled;
  }

  function savePrefs() {
    store.setJSON(STORAGE_KEYS.prefs, {
      zoom: app.zoom.mode === 'auto' ? 'auto' : app.zoom.index,
      odds: app.odds.enabled,
      autosolve: app.autosolve.enabled
    });
  }

  // -------------------------------------------------------------------- engine

  // Engine failures are EngineError objects (wasm-host.js) with a stable
  // `code`, a player-facing message and a `kind`: input | conflict |
  // inconsistent | resource | internal | trap | load | worker | protocol |
  // timeout | aborted.
  function isEngineError(err) {
    return !!err && err.name === 'EngineError' && typeof err.code === 'string';
  }

  function describeError(err) {
    if (err && typeof err.message === 'string' && err.message.trim()) return err.message.trim();
    return 'The game engine reported an unexpected error.';
  }

  // The game instance itself stopped (a trap) or broke its contract: the game
  // on screen cannot continue in it.
  function isFatal(err) {
    return !isEngineError(err) || err.kind === 'trap' || engine.status === 'failed';
  }

  // ------------------------------------------------------------------- parsing

  // Strict validation against the documented contract: anything malformed is
  // rejected (null) so the caller reports an error. Nothing is patched with
  // plausible defaults. The engine adapter rebuilds the former server's
  // GameState from the C engine's public view; hidden cells keep mine and
  // adjacent as null until the engine discloses the layout at the end.
  function isBool(value) {
    return value === true || value === false;
  }

  function isPlainObject(value) {
    return !!value && typeof value === 'object' && !Array.isArray(value);
  }

  function parseGame(raw) {
    if (!isPlainObject(raw)) return null;
    const width = raw.width;
    const height = raw.height;
    if (typeof raw.id !== 'string' || !raw.id) return null;
    if (!isInt(raw.generation) || raw.generation < 1) return null;
    if (!isInt(width) || width < MIN_SIDE || width > MAX_SIDE) return null;
    if (!isInt(height) || height < MIN_SIDE || height > MAX_SIDE) return null;
    const total = width * height;
    if (!isInt(raw.mines) || raw.mines < 1 || raw.mines > total - SAFE_AREA) return null;
    if (GAME_STATUSES.indexOf(raw.status) < 0) return null;
    if (!isInt(raw.revision) || raw.revision < 0) return null;
    if (!isInt(raw.flags) || raw.flags < 0) return null;
    if (!isNum(raw.elapsed_seconds) || raw.elapsed_seconds < 0) return null;
    if (!Array.isArray(raw.cells) || raw.cells.length !== total) return null;
    const cells = new Array(total);
    let flagged = 0;
    let revealed = 0;
    for (let i = 0; i < total; i++) {
      const c = raw.cells[i];
      if (!isPlainObject(c)) return null;
      if (!isBool(c.revealed) || !isBool(c.flagged) || !isBool(c.exploded)) return null;
      if (c.adjacent !== null && !(isInt(c.adjacent) && c.adjacent >= 0 && c.adjacent <= 8)) return null;
      if (c.mine !== null && !isBool(c.mine)) return null;
      if (c.revealed && c.flagged) return null;
      if (c.flagged) flagged++;
      if (c.revealed) revealed++;
      cells[i] = { revealed: c.revealed, flagged: c.flagged, adjacent: c.adjacent, mine: c.mine, exploded: c.exploded };
    }
    if (flagged !== raw.flags) return null;
    return {
      id: raw.id,
      generation: raw.generation,
      width: width,
      height: height,
      mines: raw.mines,
      status: raw.status,
      revision: raw.revision,
      flags: raw.flags,
      elapsed: raw.elapsed_seconds,
      cells: cells,
      revealedCount: revealed,
      receivedAt: performance.now()
    };
  }

  function parseIndexList(list) {
    if (!Array.isArray(list)) return null;
    const result = new Set();
    for (let k = 0; k < list.length; k++) {
      if (!isInt(list[k]) || list[k] < 0) return null;
      result.add(list[k]);
    }
    return result;
  }

  function parseMeta(raw) {
    if (!isPlainObject(raw)) return null;
    const meta = {};
    const counts = ['frontier_cells', 'components', 'unconstrained_cells', 'samples'];
    for (let k = 0; k < counts.length; k++) {
      const value = raw[counts[k]];
      if (!isInt(value) || value < 0) return null;
      meta[counts[k]] = value;
    }
    if (!isNum(raw.elapsed_ms) || raw.elapsed_ms < 0) return null;
    if (raw.reason !== null && typeof raw.reason !== 'string') return null;
    meta.elapsed_ms = raw.elapsed_ms;
    meta.reason = raw.reason;
    // Optional diagnostics are kept only when well formed; they are never required.
    ['sample_attempts', 'exact_components', 'sampled_components'].forEach(function (key) {
      if (isInt(raw[key]) && raw[key] >= 0) meta[key] = raw[key];
    });
    if (isNum(raw.effective_sample_size) && raw.effective_sample_size >= 0) {
      meta.effective_sample_size = raw.effective_sample_size;
    }
    return meta;
  }

  // Structural validation only; oddsFitBoard() checks the answer against the
  // board once its game id and revision are known to match.
  function parseOdds(raw) {
    if (!isPlainObject(raw)) return null;
    if (ODDS_STATUSES.indexOf(raw.status) < 0) return null;
    if (typeof raw.game_id !== 'string' || !raw.game_id) return null;
    if (!isInt(raw.revision) || raw.revision < 0) return null;
    if (!Array.isArray(raw.probabilities)) return null;
    const probabilities = new Array(raw.probabilities.length);
    for (let i = 0; i < raw.probabilities.length; i++) {
      const value = raw.probabilities[i];
      if (value === null) probabilities[i] = null;
      else if (isNum(value) && value >= 0 && value <= 1) probabilities[i] = value;
      else return null;
    }
    const safe = parseIndexList(raw.proven_safe);
    const mines = parseIndexList(raw.proven_mines);
    if (!safe || !mines) return null;
    for (const index of safe) {
      if (mines.has(index)) return null;
    }
    if (typeof raw.message !== 'string') return null;
    const meta = parseMeta(raw.meta);
    if (!meta) return null;
    return {
      gameId: raw.game_id,
      revision: raw.revision,
      status: raw.status,
      probabilities: probabilities,
      safe: safe,
      mines: mines,
      message: raw.message.trim(),
      meta: meta
    };
  }

  // Mirrors the engine's own validation: exact and approximate answers are
  // complete (a number for every hidden cell), revealed cells are always null,
  // and proven cells carry exactly 0 or 1. Unavailable answers may leave
  // hidden cells null.
  function oddsFitBoard(data, game) {
    const total = game.width * game.height;
    if (data.probabilities.length !== total) return false;
    const complete = data.status === 'exact' || data.status === 'approximate';
    for (let i = 0; i < total; i++) {
      const value = data.probabilities[i];
      if (game.cells[i].revealed ? value !== null : complete && value === null) return false;
    }
    for (const index of data.safe) {
      if (index >= total || game.cells[index].revealed || data.probabilities[index] !== 0) return false;
    }
    for (const index of data.mines) {
      if (index >= total || game.cells[index].revealed || data.probabilities[index] !== 1) return false;
    }
    return true;
  }

  // -------------------------------------------------------------- engine state

  function setEngineState(state, error) {
    app.engine.state = state;
    app.engine.error = error || null;
    renderEngineStatus();
  }

  // The game instance stopped while a game was on screen. The board stays
  // visible but frozen; a new game starts a fresh engine instance.
  function engineFailed(err) {
    const game = app.game;
    if (game && timerRunning()) {
      game.elapsed = currentElapsed();
      game.receivedAt = performance.now();
    }
    app.engine.fatal = true;
    setEngineState('error', err);
    cancelOddsRequest();
    updateTimer();
    if (!game) {
      app.loadError = {
        offline: true,
        title: 'The game engine stopped',
        text: describeError(err) + ' Try again to restart it.'
      };
      render();
      return;
    }
    showNotice({
      id: 'engine',
      tone: 'error',
      icon: 'i-offline',
      title: 'The game engine stopped',
      message: describeError(err) + ' This game cannot continue. Start a new game to keep playing.',
      actions: [{
        label: 'Start new game',
        primary: true,
        run: function () { startNewGame(app.settings, { focusBoard: true }); }
      }]
    });
    render();
  }

  // Loads the engine (once, or again after a failure) and then starts the
  // game requested meanwhile, or one with the saved settings.
  async function loadEngine() {
    if (app.engine.state === 'loading' && engine.status === 'loading') return;
    setEngineState('loading');
    app.loadError = null;
    render();
    try {
      if (app.engine.fatal) await engine.restart();
      else await engine.load();
    } catch (err) {
      setEngineState('error', err);
      app.loadError = {
        offline: true,
        title: 'Could not load the game engine',
        text: describeError(err) + (isEngineError(err) && err.code === 'abi_mismatch'
          ? ' Reload the page; if this persists, the site files are out of date.'
          : ' Check your connection and try again.')
      };
      const pending = app.pendingStart;
      app.pendingStart = null;
      if (app.game) {
        showNotice({
          id: 'engine',
          tone: 'error',
          icon: 'i-offline',
          title: 'Could not restart the game engine',
          message: describeError(err),
          actions: [{
            label: 'Try again',
            primary: true,
            run: function () { startNewGame(pending ? pending.config : app.settings, pending ? pending.opts : {}); }
          }]
        });
      }
      render();
      if (!isEngineError(err)) throw err;
      return;
    }
    app.engine.fatal = false;
    setEngineState('ready');
    const pending = app.pendingStart || { config: app.settings, opts: {} };
    app.pendingStart = null;
    startNewGame(pending.config, pending.opts);
  }

  // ----------------------------------------------------- notices and messages

  function showNotice(notice) {
    const hadFocus = el.noticeRegion.contains(document.activeElement);
    app.notice = notice;
    renderNotice();
    if (hadFocus) restoreFocus();
  }

  function clearNotice(id) {
    if (!app.notice) return;
    if (id && app.notice.id !== id) return;
    const hadFocus = el.noticeRegion.contains(document.activeElement);
    app.notice = null;
    renderNotice();
    if (hadFocus) restoreFocus();
  }

  function restoreFocus() {
    if (app.game && boardView.cells[app.focusIndex]) focusCell(app.focusIndex);
    else el.newGame.focus();
  }

  function renderNotice() {
    const region = el.noticeRegion;
    while (region.firstChild) region.removeChild(region.firstChild);
    const notice = app.notice;
    if (!notice) return;
    const box = document.createElement('div');
    box.className = 'notice';
    box.dataset.tone = notice.tone || 'info';
    box.insertAdjacentHTML('beforeend',
      svgIcon(notice.icon || (notice.tone === 'info' ? 'i-info' : 'i-alert'), 'notice-icon'));
    const body = document.createElement('div');
    body.className = 'notice-body';
    const title = document.createElement('strong');
    title.className = 'notice-title';
    title.textContent = notice.title;
    body.appendChild(title);
    if (notice.message) {
      const message = document.createElement('span');
      message.className = 'notice-message';
      message.textContent = notice.message;
      body.appendChild(message);
    }
    if (notice.actions && notice.actions.length) {
      const actions = document.createElement('div');
      actions.className = 'notice-actions';
      notice.actions.forEach(function (action) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'btn btn-small' + (action.primary ? ' btn-primary' : '');
        button.textContent = action.label;
        button.addEventListener('click', function () { action.run(); });
        actions.appendChild(button);
      });
      body.appendChild(actions);
    }
    box.appendChild(body);
    const close = document.createElement('button');
    close.type = 'button';
    close.className = 'icon-btn notice-close';
    close.setAttribute('aria-label', 'Dismiss message');
    close.innerHTML = svgIcon('i-close');
    close.addEventListener('click', function () { clearNotice(); });
    box.appendChild(close);
    region.appendChild(box);
  }

  let liveQueue = [];
  let liveTimer = 0;

  // Polite, batched announcements for state changes (never for timer ticks).
  function announce(text) {
    if (!text) return;
    liveQueue.push(text);
    if (liveTimer) return;
    el.live.textContent = '';
    liveTimer = setTimeout(function () {
      liveTimer = 0;
      el.live.textContent = liveQueue.join(' ');
      liveQueue = [];
    }, 120);
  }

  function hint(text) {
    app.hintActive = true;
    el.inspector.textContent = text;
    el.inspector.dataset.tone = 'hint';
    clearTimeout(app.hintTimer);
    app.hintTimer = setTimeout(function () {
      app.hintActive = false;
      renderInspector();
    }, HINT_MS);
    announce(text);
  }

  function clearHint() {
    if (!app.hintActive) return;
    clearTimeout(app.hintTimer);
    app.hintActive = false;
    renderInspector();
  }

  // --------------------------------------------------------------------- timer

  function timerRunning() {
    return !!app.game && app.game.status === 'playing' && !app.engine.fatal;
  }

  function currentElapsed() {
    const game = app.game;
    if (!game) return 0;
    let seconds = game.elapsed;
    if (timerRunning()) seconds += (performance.now() - game.receivedAt) / 1000;
    return seconds;
  }

  function updateTimer() {
    const running = timerRunning();
    if (running && !app.timerHandle) app.timerHandle = setInterval(renderTimer, 250);
    if (!running && app.timerHandle) {
      clearInterval(app.timerHandle);
      app.timerHandle = 0;
    }
    renderTimer();
  }

  function renderTimer() {
    const text = app.game ? formatClock(currentElapsed()) : '0:00';
    if (text !== app.timerText) {
      app.timerText = text;
      el.timer.textContent = text;
    }
  }

  // ----------------------------------------------------------------- game flow

  function malformedState() {
    return new EngineError('malformed_state', 'The game engine sent game data this page cannot read.', {
      kind: 'internal'
    });
  }

  function startNewGame(config, options) {
    const opts = options || {};
    const request = { width: config.width, height: config.height, mines: config.mines };
    app.settings = {
      preset: presetKeyFor(request.width, request.height, request.mines),
      width: request.width,
      height: request.height,
      mines: request.mines
    };
    saveSettings();
    clearHint();
    if (engine.status !== 'ready' || app.engine.fatal) {
      // The engine starts (or restarts after a failure) first; this game
      // follows as soon as it runs.
      app.pendingStart = { config: request, opts: opts };
      clearOddsView();
      if (engine.status !== 'loading') loadEngine();
      else render();
      return;
    }
    let next = null;
    let error = null;
    try {
      next = parseGame(engine.newGame(request));
      if (!next) error = malformedState();
    } catch (err) {
      error = err;
    }
    if (error) {
      handleCreateError(error, request, opts);
      render();
      syncOdds();
      if (!isEngineError(error)) throw error;
      return;
    }
    clearNotice();
    if (opts.fromCustom) closeCustom(false);
    adoptGame(next);
    announce('New ' + configName(next) + ' game: ' + next.width + ' by ' + next.height + ' with ' +
      plural(next.mines, 'mine') + '. Your first reveal is always safe.');
    if (opts.focusBoard) focusCell(app.focusIndex);
  }

  function handleCreateError(err, request, opts) {
    if (isFatal(err) || err.code === 'malformed_state') {
      engineFailed(err);
      return;
    }
    if (!app.game) {
      app.loadError = { title: 'Could not start a game', text: describeError(err) };
      return;
    }
    if (opts.fromCustom && err.kind === 'input') {
      el.customFormError.textContent = 'The engine rejected these settings: ' + describeError(err);
      return;
    }
    showNotice({
      id: 'create',
      tone: 'error',
      title: 'Could not start a new game',
      message: describeError(err) + ' Your current game is unchanged.',
      actions: [{ label: 'Try again', run: function () { startNewGame(request, opts); } }]
    });
  }

  function adoptGame(next) {
    const prev = app.game;
    const sameGame = !!prev && prev.id === next.id;
    if (sameGame && next.revision < prev.revision) return false;
    const moved = !sameGame || next.revision !== prev.revision || next.status !== prev.status;
    app.game = next;
    app.loadError = null;
    if (!sameGame) {
      resetOdds();
      app.hoverIndex = -1;
      app.settings = {
        preset: presetKeyFor(next.width, next.height, next.mines),
        width: next.width,
        height: next.height,
        mines: next.mines
      };
      saveSettings();
    } else if (moved) {
      clearOddsView();
    }
    updateTimer();
    render();
    syncOdds();
    return true;
  }

  // Moves run synchronously in the local engine: a move is either applied
  // (one new revision) or rejected without any change, so there is never an
  // outcome to guess or a move to retry.
  function sendAction(action, index) {
    const game = app.game;
    const automatic = action === 'autosolve';
    clearHint();
    let result = null;
    let error = null;
    try {
      result = automatic
        ? engine.autosolve({ generation: game.generation, revision: game.revision })
        : engine.act({
          action: action,
          row: Math.floor(index / game.width),
          col: index % game.width,
          generation: game.generation,
          revision: game.revision
        });
    } catch (err) {
      error = err;
    }
    const next = error ? null : parseGame(result.state);
    if (!error && (!next || next.id !== game.id)) error = malformedState();
    if (!error && automatic && !result.available) {
      error = new EngineError('odds_not_accepted', 'The engine holds no accepted odds for this position, so no ' +
        'moves were played. Retry odds to continue.', { kind: 'conflict' });
    }
    if (error) {
      handleActionError(error, action);
      if (automatic) setAutosolveEnabled(false);
      render();
      syncOdds();
      if (!isEngineError(error)) throw error;
      return;
    }
    if (app.notice && app.notice.id !== 'engine') clearNotice();
    adoptGame(next);
    announceMove(action, index, game, next);
  }

  function actionNoun(action) {
    if (action === 'autosolve') return 'automatic move batch';
    if (action === 'flag') return 'flag change';
    if (action === 'chord') return 'chord';
    return 'reveal';
  }

  function handleActionError(err, action) {
    if (isFatal(err) || err.code === 'malformed_state') {
      engineFailed(err);
      return;
    }
    const current = app.game;
    const latest = err.state ? parseGame(err.state) : null;
    const sameGame = !!latest && !!current && latest.id === current.id;
    if (sameGame) adoptGame(latest);
    if (err.code === 'game_over') {
      showNotice({ id: 'action', tone: 'warning', title: 'This game is already over', message: describeError(err) });
      return;
    }
    if (err.code === 'stale_revision') {
      showNotice({
        id: 'stale',
        tone: 'warning',
        title: 'Your move was not applied',
        message: 'The game changed before your ' + actionNoun(action) + ' was applied, so it was not applied ' +
          'to a position you had not seen. The latest board is shown now; check it and try again.'
      });
      return;
    }
    showNotice({
      id: 'action',
      tone: err.kind === 'conflict' ? 'warning' : 'error',
      title: action === 'autosolve' ? 'Autosolve paused' : 'Move rejected',
      message: describeError(err)
    });
  }

  function announceMove(action, index, before, after) {
    if (after.status === 'won' && before.status !== 'won') {
      announce('You won! Board cleared in ' + formatDurationWords(after.elapsed) + '.');
      return;
    }
    if (after.status === 'lost' && before.status !== 'lost') {
      announce('Boom, that was a mine. Game over; the full layout is now shown.');
      return;
    }
    if (action === 'flag') {
      const left = after.mines - after.flags;
      announce((after.cells[index].flagged ? 'Flag placed. ' : 'Flag removed. ') + plural(left, 'mine') + ' left.');
      return;
    }
    const opened = after.revealedCount - before.revealedCount;
    if (action === 'autosolve') {
      if (opened > 0) announce('Autosolve opened ' + plural(opened, 'cell') + ' and updated the flags.');
      else if (after.flags > before.flags) announce('Autosolve marked ' + plural(after.flags - before.flags, 'proven mine') + '.');
      return;
    }
    if (opened <= 0) {
      announce('No cells were opened.');
    } else if (opened === 1 && action === 'reveal') {
      const cell = after.cells[index];
      announce(cell.adjacent ? 'Revealed ' + cell.adjacent + '.' : 'Revealed an empty cell.');
    } else {
      announce('Opened ' + plural(opened, 'cell') + '.');
    }
  }

  // ------------------------------------------------------------------- moves

  function canAct() {
    const game = app.game;
    if (!game) return false;
    if (app.pendingStart) {
      hint('A new game is starting...');
      return false;
    }
    if (app.engine.fatal) {
      hint('The game engine stopped, so this game cannot continue. Start a new game to keep playing.');
      return false;
    }
    if (isTerminal(game)) {
      hint('This game is over. Press New game or the face to play again.');
      return false;
    }
    return true;
  }

  function validIndex(index) {
    return !!app.game && isInt(index) && index >= 0 && index < app.game.cells.length;
  }

  // Click, tap, Enter or Space. fromPointer lets Flag mode apply to clicks and taps only.
  function primaryAction(index, fromPointer) {
    if (!validIndex(index) || !canAct()) return;
    const cell = app.game.cells[index];
    if (cell.revealed) {
      if (cell.adjacent) tryChord(index);
      return;
    }
    if (fromPointer && app.flagMode) {
      sendAction('flag', index);
      return;
    }
    if (cell.flagged) {
      hint('This cell is flagged. Remove the flag first (right-click, F or Flag mode).');
      return;
    }
    sendAction('reveal', index);
  }

  function tryChord(index) {
    const game = app.game;
    const cell = game.cells[index];
    const around = neighborsOf(index, game.width, game.height);
    let flags = 0;
    let closed = 0;
    around.forEach(function (j) {
      const other = game.cells[j];
      if (other.revealed) return;
      if (other.flagged) flags++;
      else closed++;
    });
    if (!closed) {
      hint('Every neighbor of this ' + cell.adjacent + ' is already open or flagged.');
      return;
    }
    if (flags !== cell.adjacent) {
      flashNeighbors(around);
      hint('This ' + cell.adjacent + ' has ' + plural(flags, 'flag') + ' around it. Place exactly ' +
        cell.adjacent + ' to open the rest in one go.');
      return;
    }
    sendAction('chord', index);
  }

  function toggleFlag(index) {
    if (!validIndex(index) || !canAct()) return;
    if (app.game.cells[index].revealed) {
      hint('Only hidden cells can be flagged.');
      return;
    }
    sendAction('flag', index);
  }

  function flashNeighbors(indices) {
    const game = app.game;
    const nodes = [];
    indices.forEach(function (j) {
      const cell = game.cells[j];
      if (!cell.revealed && !cell.flagged && boardView.cells[j]) nodes.push(boardView.cells[j]);
    });
    nodes.forEach(function (node) { node.classList.add('is-peek'); });
    setTimeout(function () {
      nodes.forEach(function (node) { node.classList.remove('is-peek'); });
    }, 220);
  }

  // ---------------------------------------------------------------------- odds

  function cancelAutosolve() {
    clearTimeout(app.autosolve.timer);
    app.autosolve.timer = 0;
  }

  function hasAutomaticMoves(game, data) {
    for (const index of data.safe) {
      if (!game.cells[index].revealed) return true;
    }
    for (const index of data.mines) {
      if (!game.cells[index].revealed && !game.cells[index].flagged) return true;
    }
    return false;
  }

  function syncAutosolve() {
    renderAutosolve();
    const auto = app.autosolve;
    const game = app.game;
    const data = currentOdds();
    if (!auto.enabled || auto.timer || !data || app.engine.fatal || !hasAutomaticMoves(game, data)) return;
    const key = oddsKey(game);
    if (auto.attemptedKey === key) return;
    auto.timer = setTimeout(function () {
      auto.timer = 0;
      const latest = currentOdds();
      if (!auto.enabled || !latest || oddsKey(app.game) !== key || app.engine.fatal ||
          !hasAutomaticMoves(app.game, latest)) return;
      // A no-op or failed batch must not become a same-revision request loop.
      auto.attemptedKey = key;
      sendAction('autosolve');
    }, 0);
  }

  function setAutosolveEnabled(on) {
    if (app.autosolve.enabled === on) return;
    app.autosolve.enabled = on;
    app.autosolve.attemptedKey = null;
    cancelAutosolve();
    if (on) {
      app.odds.enabled = true;
      app.odds.error = null;
      app.odds.discardedKey = null;
    }
    savePrefs();
    render();
    syncOdds();
    announce(on ? 'Autosolve on. Only proven moves are played; you choose every uncertain cell.'
      : 'Autosolve paused. Moves already played stay on the board.');
  }

  function renderAutosolve() {
    const auto = app.autosolve;
    const game = app.game;
    el.autosolveToggle.setAttribute('aria-checked', auto.enabled ? 'true' : 'false');
    el.autosolveStatus.hidden = !auto.enabled;
    let text = '';
    if (auto.enabled) {
      const data = currentOdds();
      if (!game || app.pendingStart) text = 'Autosolve is waiting for the board.';
      else if (app.engine.fatal) text = 'Autosolve stopped with the game engine. Start a new game to continue.';
      else if (game.status === 'ready') text = 'Choose your first cell. Autosolve will then play only certain moves.';
      else if (isTerminal(game)) text = 'Game over. Autosolve never chooses uncertain cells.';
      else if (!data) {
        text = oddsPhase() === 'error' || oddsPhase() === 'discarded'
          ? 'Autosolve is waiting for valid odds. Retry odds or keep playing.'
          : 'Autosolve is calculating the next certain moves...';
      } else if (hasAutomaticMoves(game, data)) {
        text = auto.attemptedKey === oddsKey(game)
          ? 'No automatic moves were applied. Toggle Autosolve off and on to retry.'
          : 'Autosolve is opening proven-safe cells and flagging proven mines...';
      } else {
        text = data.status === 'unavailable'
          ? 'No more proven moves. Some odds are unavailable; retry odds or choose your next cell.'
          : 'Your move: choose a cell using its mine odds. If you survive, autosolve continues.';
      }
    }
    setText(el.autosolveStatus, text);
  }

  const PROVEN_SAFE = { kind: 'safe', label: 'proven safe', text: 'proven safe' };
  const PROVEN_MINE = { kind: 'mine', label: 'proven mine', text: 'proven mine' };
  const UNKNOWN_ODDS = { kind: 'unknown', label: 'mine chance unavailable', text: 'odds unavailable' };

  function formatPercent(p, estimated) {
    const pct = p * 100;
    if (estimated) return '~' + Math.round(pct) + '%';
    if (pct > 0 && pct < 1) return '<1%';
    if (pct > 99 && pct < 100) return '>99%';
    return Math.round(pct) + '%';
  }

  function percentWords(p, estimated) {
    const pct = p * 100;
    if (estimated) return 'about ' + Math.round(pct) + ' percent';
    if (pct > 0 && pct < 1) return 'less than 1 percent';
    if (pct > 99 && pct < 100) return 'more than 99 percent';
    return Math.round(pct) + ' percent';
  }

  // Odds to draw on one hidden cell, or null for no overlay. Sampled 0 or 1
  // values are never promoted to "proven"; only the proven lists (or an
  // exact result) give certainty styling.
  function cellOddsInfo(data, index) {
    const status = data.status;
    if (status !== 'exact' && status !== 'approximate' && status !== 'unavailable') return null;
    if (data.safe.has(index)) return PROVEN_SAFE;
    if (data.mines.has(index)) return PROVEN_MINE;
    const p = data.probabilities[index];
    if (status === 'unavailable' || p === null) return UNKNOWN_ODDS;
    if (status === 'exact' && p === 0) return PROVEN_SAFE;
    if (status === 'exact' && p === 1) return PROVEN_MINE;
    const estimated = status === 'approximate';
    return {
      kind: 'value',
      p: p,
      bucket: clamp(Math.round(p * 10), 0, 10),
      estimated: estimated,
      text: formatPercent(p, estimated),
      label: estimated
        ? 'estimated mine chance ' + percentWords(p, true)
        : 'mine chance ' + percentWords(p, false) + ', exact'
    };
  }

  function currentOdds() {
    const o = app.odds;
    const game = app.game;
    if (!o.enabled || !game || !o.data || app.engine.fatal || game.status !== 'playing') return null;
    if (o.dataKey !== oddsKey(game)) return null;
    if (o.data.gameId !== game.id || o.data.revision !== game.revision) return null;
    return o.data;
  }

  function oddsPhase() {
    const o = app.odds;
    const game = app.game;
    if (!o.enabled) return 'off';
    if (app.pendingStart) return 'waiting';
    if (!game) return 'nogame';
    if (app.engine.fatal) return 'stopped';
    if (game.status === 'ready') return 'ready';
    if (isTerminal(game)) return 'ended';
    const key = oddsKey(game);
    if (currentOdds()) return 'result';
    if (o.error && o.error.key === key) return 'error';
    if (o.discardedKey === key) return 'discarded';
    return 'loading';
  }

  // Obsolete inference is cancelled by terminating the solver worker; the
  // game itself never waits for it.
  function cancelOddsRequest() {
    cancelAutosolve();
    const o = app.odds;
    o.gen++; // late answers from the old request are ignored even if cancelling came too late
    engine.cancelOdds();
    o.requestKey = null;
    stopOddsTicker();
  }

  function clearOddsView() {
    cancelOddsRequest();
    app.odds.data = null;
    app.odds.dataKey = null;
  }

  function resetOdds() {
    clearOddsView();
    app.autosolve.attemptedKey = null;
    app.odds.cache = null;
    app.odds.error = null;
    app.odds.discardedKey = null;
  }

  // Make the odds match the position on screen: reuse, request or cancel.
  function syncOdds() {
    const o = app.odds;
    const game = app.game;
    if (!o.enabled || !game || app.engine.fatal || app.pendingStart || game.status !== 'playing') {
      cancelOddsRequest();
      renderOddsPanel();
      return;
    }
    const key = oddsKey(game);
    if (o.data && o.dataKey === key) {
      syncAutosolve();
      return;
    }
    if (o.cache && o.cache.key === key) {
      o.data = o.cache.data;
      o.dataKey = key;
      renderBoard();
      renderOddsPanel();
      renderInspector();
      announceOddsIfNeeded();
      syncAutosolve();
      return;
    }
    if (o.requestKey === key) return;
    if ((o.error && o.error.key === key) || o.discardedKey === key) {
      renderOddsPanel();
      return;
    }
    requestOdds(game, key);
  }

  async function requestOdds(game, key) {
    const o = app.odds;
    cancelOddsRequest();
    const gen = ++o.gen;
    o.requestKey = key;
    o.startedAt = performance.now();
    startOddsTicker();
    renderOddsPanel();
    let data = null;
    let error = null;
    try {
      data = await engine.odds({ generation: game.generation, revision: game.revision });
    } catch (err) {
      error = err;
    }
    if (gen !== o.gen) return; // a move, new game or toggle made this answer obsolete
    o.requestKey = null;
    stopOddsTicker();
    const current = app.game;
    if (!o.enabled || !current || oddsKey(current) !== key) {
      renderOddsPanel();
      return;
    }
    if (error) {
      if (isFatal(error)) {
        engineFailed(error);
        if (!isEngineError(error)) throw error;
        return;
      }
      if (error.kind === 'conflict') {
        handleOddsConflict(error, key);
        return;
      }
      o.error = { key: key, kind: error.kind, message: oddsErrorText(error) };
      renderOddsPanel();
      return;
    }
    const parsed = parseOdds(data);
    if (!parsed) {
      o.error = { key: key, kind: 'parse', message: 'The engine sent odds this page cannot read, so none are shown.' };
      renderOddsPanel();
      return;
    }
    if (parsed.gameId !== current.id || parsed.revision !== current.revision) {
      // An answer computed for another position is never drawn.
      o.discardedKey = key;
      renderOddsPanel();
      return;
    }
    if (!oddsFitBoard(parsed, current)) {
      o.error = { key: key, kind: 'parse', message: 'The engine sent odds that do not match this board, so none are shown.' };
      renderOddsPanel();
      return;
    }
    o.data = parsed;
    o.dataKey = key;
    o.cache = { key: key, data: parsed };
    renderBoard();
    renderOddsPanel();
    renderInspector();
    announceOddsIfNeeded();
    syncAutosolve();
  }

  // Solver failures never affect the game: they are shown with Retry odds.
  function oddsErrorText(err) {
    if (err.kind === 'aborted') return 'The odds calculation was cancelled. Retry odds, or keep playing.';
    return describeError(err) + ' Your game is not affected; retry odds, or keep playing (odds are ' +
      'calculated again after your next move).';
  }

  function handleOddsConflict(error, key) {
    const current = app.game;
    const latest = error.state ? parseGame(error.state) : null;
    if (latest && current && latest.id === current.id &&
        (latest.revision !== current.revision || latest.status !== current.status)) {
      adoptGame(latest);
      return;
    }
    app.odds.error = { key: key, kind: 'conflict', message: describeError(error) };
    renderOddsPanel();
  }

  function announceOddsIfNeeded() {
    const o = app.odds;
    if (!o.announceNext || !o.data) return;
    o.announceNext = false;
    if (o.data.status === 'exact') announce('Exact mine odds shown.');
    else if (o.data.status === 'approximate') announce('Estimated mine odds shown. Numbers with a tilde are approximate.');
    else announce('Mine odds are unavailable for this position.');
  }

  function setOddsEnabled(on) {
    const o = app.odds;
    if (o.enabled === on) return;
    o.enabled = on;
    o.error = null;
    o.discardedKey = null;
    if (!on) app.autosolve.enabled = false;
    savePrefs();
    if (!on) {
      clearOddsView();
      o.announceNext = false;
      render();
      announce('Mine odds hidden. Autosolve is off.');
      return;
    }
    o.announceNext = true;
    render();
    syncOdds();
    const phase = oddsPhase();
    if (phase === 'ready') {
      o.announceNext = false;
      announce('Mine odds on. ' + READY_TEXT);
    } else if (phase === 'ended') {
      o.announceNext = false;
      announce('Mine odds on. The game is over, so the board shows the real layout.');
    } else if (phase === 'loading') {
      announce('Mine odds on. Calculating.');
    }
  }

  function retryOdds() {
    const o = app.odds;
    const game = app.game;
    cancelAutosolve();
    app.autosolve.attemptedKey = null;
    o.error = null;
    o.discardedKey = null;
    if (game && o.data && o.data.status === 'unavailable') {
      o.data = null;
      o.dataKey = null;
    }
    if (o.cache && o.cache.data.status === 'unavailable') o.cache = null;
    o.announceNext = true;
    syncOdds();
    renderOddsPanel();
    renderBoard();
  }

  function startOddsTicker() {
    stopOddsTicker();
    app.odds.ticker = setInterval(renderOddsPanel, 1000);
  }

  function stopOddsTicker() {
    if (app.odds.ticker) {
      clearInterval(app.odds.ticker);
      app.odds.ticker = 0;
    }
  }

  // ----------------------------------------------------------------- rendering

  function setText(node, text) {
    if (node.textContent !== text) node.textContent = text;
  }

  function capitalize(text) {
    return text ? text.charAt(0).toUpperCase() + text.slice(1) : text;
  }

  function escapeHtml(text) {
    return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function coarsePointer() {
    return !!(window.matchMedia && window.matchMedia('(pointer: coarse)').matches);
  }

  function render() {
    renderEngineStatus();
    renderPresets();
    renderHud();
    renderResult();
    renderBoard();
    renderOddsPanel();
    renderInspector();
    renderGameMeta();
  }

  function renderEngineStatus() {
    const state = app.engine.state;
    el.engineStatus.dataset.state = state;
    setText(el.engineStatusText, state === 'ready' ? 'Runs in your browser'
      : state === 'error' ? 'Engine error' : 'Loading engine...');
    el.engineStatus.title = state === 'error' && app.engine.error ? describeError(app.engine.error)
      : state === 'ready' ? 'The game and its odds are computed locally in this tab.' : '';
  }

  function renderPresets() {
    const current = app.settings.preset;
    el.presetButtons.forEach(function (button) {
      button.setAttribute('aria-pressed', button.dataset.preset === current ? 'true' : 'false');
    });
    el.customToggle.setAttribute('aria-expanded', app.customOpen ? 'true' : 'false');
    el.customForm.hidden = !app.customOpen;
    setText(el.customDims, current === 'custom'
      ? app.settings.width + 'x' + app.settings.height + ', ' + plural(app.settings.mines, 'mine')
      : 'Your size');
  }

  function renderHud() {
    const game = app.game;
    if (game) {
      const left = game.mines - game.flags;
      const over = left < 0;
      setText(el.minesLeft, String(left));
      setText(el.minesLabel, over ? 'Too many flags' : 'Mines left');
      el.minesCounter.classList.toggle('is-over', over);
      el.minesCounter.title = plural(game.flags, 'flag') + ' placed for ' + plural(game.mines, 'mine');
    } else {
      setText(el.minesLeft, '--');
      setText(el.minesLabel, 'Mines left');
      el.minesCounter.classList.remove('is-over');
    }
    renderTimer();
    renderFace();
    el.modeReveal.setAttribute('aria-pressed', app.flagMode ? 'false' : 'true');
    el.modeFlag.setAttribute('aria-pressed', app.flagMode ? 'true' : 'false');
    el.oddsToggle.setAttribute('aria-checked', app.odds.enabled ? 'true' : 'false');
    renderZoomButtons();
  }

  function renderFace() {
    const game = app.game;
    let face = 'face-smile';
    if (game && game.status === 'won') face = 'face-cool';
    else if (game && game.status === 'lost') face = 'face-dead';
    else if (app.pressing) face = 'face-wow';
    if (el.faceUse.getAttribute('href') !== '#' + face) el.faceUse.setAttribute('href', '#' + face);
  }

  function renderResult() {
    const game = app.game;
    const show = isTerminal(game);
    el.result.hidden = !show;
    if (!show) return;
    const won = game.status === 'won';
    el.result.dataset.outcome = game.status;
    el.resultIcon.setAttribute('href', won ? '#i-trophy' : '#i-mine');
    setText(el.resultTitle, won ? 'You cleared the board!' : 'Boom! That was a mine.');
    let detail;
    if (won) {
      detail = configName(game) + ', ' + describeConfig(game) + ', cleared in ' + formatClock(game.elapsed) + '.';
    } else {
      let wrongFlags = 0;
      game.cells.forEach(function (cell) {
        if (cell.flagged && cell.mine === false) wrongFlags++;
      });
      detail = 'Every mine is shown now' + (wrongFlags ? ' and crossed-out flags were wrong' : '') +
        '. Time ' + formatClock(game.elapsed) + '.';
    }
    setText(el.resultDetail, detail);
    setText(el.resultAgainLabel, won ? 'Play again' : 'Try again');
  }

  function renderGameMeta() {
    const game = app.game;
    setText(el.gameMeta, game ? configName(game) + ': ' + describeConfig(game) : '');
  }

  function renderEmptyState() {
    const busy = app.engine.state === 'loading' || (!!app.pendingStart && !app.loadError);
    el.boardEmptySpinner.hidden = !busy;
    el.boardEmptyIcon.hidden = busy || !(app.loadError && app.loadError.offline);
    el.boardEmptyRetry.hidden = busy;
    if (busy) {
      setText(el.boardEmptyTitle, app.engine.state === 'loading' ? 'Loading the game engine...' : 'Starting a new game...');
      setText(el.boardEmptyText, 'The game and its odds run entirely in your browser.');
    } else if (app.loadError) {
      setText(el.boardEmptyTitle, app.loadError.title);
      setText(el.boardEmptyText, app.loadError.text);
      setText(el.boardEmptyRetry, 'Try again');
    } else {
      setText(el.boardEmptyTitle, 'No game yet');
      setText(el.boardEmptyText, 'Pick a difficulty or press New game to start.');
      setText(el.boardEmptyRetry, 'New game');
    }
  }

  // Builds the cell elements once per board size; later renders only patch
  // cells whose content changed, so focus and scroll position survive.
  function ensureBoardDom(width, height) {
    if (boardView.width === width && boardView.height === height) return false;
    const hadFocus = el.board.contains(document.activeElement);
    const oldWidth = boardView.width;
    const oldRow = oldWidth ? Math.floor(app.focusIndex / oldWidth) : 0;
    const oldCol = oldWidth ? app.focusIndex % oldWidth : 0;
    const parts = [];
    for (let r = 0; r < height; r++) {
      parts.push('<div class="board-row" role="row">');
      for (let c = 0; c < width; c++) {
        parts.push('<div class="cell" role="gridcell" tabindex="-1" data-index="' + (r * width + c) + '"></div>');
      }
      parts.push('</div>');
    }
    el.board.innerHTML = parts.join('');
    boardView.width = width;
    boardView.height = height;
    boardView.cells = Array.prototype.slice.call(el.board.querySelectorAll('.cell'));
    boardView.sigs = new Array(width * height).fill('');
    app.focusIndex = Math.min(oldRow, height - 1) * width + Math.min(oldCol, width - 1);
    boardView.cells[app.focusIndex].tabIndex = 0;
    app.cellSize = 0;
    return hadFocus;
  }

  function cellView(game, index, odds) {
    const cell = game.cells[index];
    let cls = 'cell';
    let html = '';
    let base;
    let info = null;
    if (cell.revealed) {
      if (cell.mine === true || cell.exploded) {
        cls += ' is-revealed is-mine' + (cell.exploded ? ' is-exploded' : '');
        html = ICON.mine;
        base = cell.exploded ? 'Exploded mine' : 'Mine';
      } else if (cell.adjacent) {
        cls += ' is-revealed is-number n' + cell.adjacent;
        html = String(cell.adjacent);
        base = plural(cell.adjacent, 'adjacent mine');
      } else {
        cls += ' is-revealed is-empty';
        base = 'Empty, no adjacent mines';
      }
    } else if (isTerminal(game) && cell.mine !== null) {
      // The engine disclosed the layout because the game is over.
      if (cell.mine && cell.exploded) {
        cls += ' is-revealed is-mine is-exploded';
        html = ICON.mine;
        base = 'Exploded mine';
      } else if (cell.mine && cell.flagged) {
        cls += ' is-hidden is-flagged';
        html = ICON.flag;
        base = game.status === 'won' ? 'Mine, flagged' : 'Flagged mine, correct flag';
      } else if (cell.mine && game.status === 'won') {
        cls += ' is-hidden is-flagged is-auto-flag';
        html = ICON.flag;
        base = 'Mine';
      } else if (cell.mine) {
        cls += ' is-revealed is-mine';
        html = ICON.mine;
        base = 'Mine';
      } else if (cell.flagged) {
        cls += ' is-hidden is-flagged is-wrong-flag';
        html = ICON.wrongFlag;
        base = 'Wrong flag, no mine here';
      } else {
        cls += ' is-hidden';
        base = 'Hidden, no mine';
      }
    } else {
      cls += ' is-hidden' + (cell.flagged ? ' is-flagged' : '');
      base = cell.flagged ? 'Flagged' : 'Hidden';
      info = odds ? cellOddsInfo(odds, index) : null;
      if (info && info.kind !== 'unknown') {
        let content;
        cls += ' has-odds';
        if (info.kind === 'safe') {
          cls += ' is-proven-safe';
          content = ICON.provenSafe;
        } else if (info.kind === 'mine') {
          cls += ' is-proven-mine';
          content = ICON.provenMine;
        } else {
          cls += ' heat-' + info.bucket + (info.estimated ? ' is-estimate' : '');
          content = '<span class="odds' + (info.text.length > 4 ? ' odds-long' : '') + '">' + escapeHtml(info.text) + '</span>';
        }
        html = (cell.flagged ? ICON.cornerFlag : '') + content;
      } else if (cell.flagged) {
        html = ICON.flag;
      }
    }
    const where = capitalize(positionText(index, game.width));
    return {
      cls: cls,
      html: html,
      base: base,
      info: info,
      label: base + (info ? ', ' + info.label : '') + '. ' + where + '.'
    };
  }

  function renderBoard() {
    const game = app.game;
    if (!game) {
      el.board.hidden = true;
      el.boardEmpty.hidden = false;
      el.boardFrame.dataset.status = 'none';
      renderEmptyState();
      return;
    }
    el.board.hidden = false;
    el.boardEmpty.hidden = true;
    const refocus = ensureBoardDom(game.width, game.height);
    applyCellSize(false);
    const interactive = !isTerminal(game) && !app.engine.fatal;
    el.boardFrame.dataset.status = game.status;
    el.board.dataset.status = game.status;
    el.board.dataset.interactive = interactive ? 'true' : 'false';
    el.board.setAttribute('aria-label', 'Minefield, ' + game.width + ' columns by ' + game.height + ' rows, ' +
      plural(game.mines, 'mine'));
    const odds = currentOdds();
    const total = game.cells.length;
    for (let i = 0; i < total; i++) {
      const view = cellView(game, i, odds);
      const sig = view.cls + '|' + view.html + '|' + view.label;
      if (boardView.sigs[i] !== sig) {
        const node = boardView.cells[i];
        node.className = view.cls;
        node.innerHTML = view.html;
        node.setAttribute('aria-label', view.label);
        boardView.sigs[i] = sig;
      }
    }
    if (refocus) focusCell(app.focusIndex);
  }

  function inspectText(view) {
    const info = view.info;
    if (!info) return view.base;
    if (info.kind === 'value') {
      return view.base + ', ' + info.text + ' mine chance (' + (info.estimated ? 'estimated' : 'exact') + ')';
    }
    return view.base + ', ' + info.text;
  }

  function renderInspector() {
    if (app.hintActive) return;
    el.inspector.dataset.tone = '';
    const game = app.game;
    let text = '';
    if (game) {
      let index = app.hoverIndex;
      if (index < 0 && el.board.contains(document.activeElement)) index = app.focusIndex;
      if (index >= 0 && index < game.cells.length) {
        const view = cellView(game, index, currentOdds());
        text = capitalize(positionText(index, game.width)) + ': ' + inspectText(view);
      } else if (game.status === 'ready') {
        text = 'Click any cell to start. The first reveal is always safe.';
      } else if (isTerminal(game)) {
        text = 'Game over. Press New game or the face to play again.';
      } else if (coarsePointer()) {
        text = 'Tap to reveal. Use Flag mode or a long press to flag.';
      } else {
        text = 'Hover over or focus a cell to see its details.';
      }
    }
    setText(el.inspector, text);
  }

  function humanizeReason(reason) {
    let text = String(reason).trim();
    if (!text) return '';
    if (/^[a-z0-9_-]+$/i.test(text)) text = text.replace(/[_-]+/g, ' ');
    text = text.replace(/[.\s]+$/, '');
    if (text.length > 200) text = text.slice(0, 197) + '...';
    return capitalize(text) + '.';
  }

  // Short, player-oriented insight shown under the solver's own message. The
  // solver message already lists certain cells and reasons, so they are only
  // repeated here when the result carries no message.
  function oddsSummary(data) {
    const game = app.game;
    const verbose = !data.message;
    let safe = 0;
    let mines = 0;
    let wrongFlags = 0;
    let best = null;
    for (let i = 0; i < game.cells.length; i++) {
      const cell = game.cells[i];
      if (cell.revealed) continue;
      const info = cellOddsInfo(data, i);
      if (!info) continue;
      if (info.kind === 'safe') {
        safe++;
        if (cell.flagged) wrongFlags++;
      } else if (info.kind === 'mine') {
        mines++;
      } else if (info.kind === 'value' && !cell.flagged && (!best || info.p < best.p)) {
        best = info;
      }
    }
    const sentences = [];
    if (verbose && (safe || mines)) {
      const parts = [];
      if (safe) parts.push(plural(safe, 'cell') + ' proven safe');
      if (mines) parts.push(plural(mines, 'proven mine'));
      sentences.push(capitalize(parts.join(', ')) + '.');
    }
    if (best) sentences.push('Lowest risk among unproven cells: ' + best.text + '.');
    if (wrongFlags) {
      sentences.push(wrongFlags === 1
        ? '1 of your flags is on a proven safe cell.'
        : formatCount(wrongFlags) + ' of your flags are on proven safe cells.');
    }
    if (verbose && data.status === 'approximate') sentences.push('Only check and mine markers are certain.');
    if (verbose && data.status !== 'exact' && typeof data.meta.reason === 'string') {
      const reason = humanizeReason(data.meta.reason);
      if (reason) sentences.push('Reason: ' + reason);
    }
    return sentences.join(' ');
  }

  function metaChips(data) {
    const meta = data.meta || {};
    const chips = [];
    if (isNum(meta.frontier_cells)) {
      chips.push([formatCount(meta.frontier_cells), meta.frontier_cells === 1 ? 'frontier cell' : 'frontier cells',
        'Hidden cells next to at least one revealed number']);
    }
    if (isNum(meta.components)) {
      let label = meta.components === 1 ? 'component' : 'components';
      if (isNum(meta.sampled_components) && meta.sampled_components > 0) {
        label += ' (' + formatCount(meta.sampled_components) + ' sampled)';
      }
      chips.push([formatCount(meta.components), label, 'Independent groups of frontier cells that share clues']);
    }
    if (isNum(meta.unconstrained_cells)) {
      chips.push([formatCount(meta.unconstrained_cells), 'unconstrained', 'Hidden cells next to no clue; they share one probability']);
    }
    if (isNum(meta.samples) && (data.status === 'approximate' || meta.samples > 0)) {
      chips.push([formatCount(meta.samples), meta.samples === 1 ? 'sample' : 'samples', 'Weighted layouts drawn for hard components']);
    }
    if (data.status !== 'exact' && isNum(meta.effective_sample_size)) {
      chips.push([formatCount(Math.round(meta.effective_sample_size)), 'effective samples',
        'How many independent samples the weighted samples are worth; too few means no estimate']);
    }
    if (isNum(meta.elapsed_ms)) chips.push([formatMs(meta.elapsed_ms), 'solve time', 'Time the solver spent on this position']);
    return chips;
  }

  function renderMetaChips(chips) {
    const sig = chips.map(function (chip) { return chip.join(' '); }).join('|');
    if (el.oddsMeta.dataset.sig !== sig) {
      el.oddsMeta.dataset.sig = sig;
      while (el.oddsMeta.firstChild) el.oddsMeta.removeChild(el.oddsMeta.firstChild);
      chips.forEach(function (chip) {
        const item = document.createElement('li');
        const value = document.createElement('b');
        value.textContent = chip[0];
        item.appendChild(value);
        if (chip[1]) item.appendChild(document.createTextNode(' ' + chip[1]));
        if (chip[2]) item.title = chip[2];
        el.oddsMeta.appendChild(item);
      });
    }
    el.oddsMeta.hidden = !chips.length;
  }

  function renderOddsPanel() {
    const phase = oddsPhase();
    const o = app.odds;
    const game = app.game;
    let badge = 'Off';
    let tone = 'muted';
    let message = '';
    let detail = '';
    let chips = [];
    let retry = false;
    switch (phase) {
      case 'off':
        message = 'Turn on Mine odds to shade every hidden cell by its chance of hiding a mine.';
        break;
      case 'nogame':
        badge = 'Waiting';
        message = 'Odds appear once a game is loaded.';
        break;
      case 'stopped':
        badge = 'Unavailable';
        message = 'The game engine stopped, so there are no odds for this game. Start a new game to keep playing.';
        break;
      case 'waiting':
        badge = 'Updating';
        tone = 'busy';
        message = 'Starting a new game...';
        break;
      case 'ready':
        badge = 'Not started';
        message = READY_TEXT;
        break;
      case 'ended':
        badge = 'Game over';
        message = game.status === 'won'
          ? 'Board cleared. The real layout is shown, so there is nothing left to estimate.'
          : 'The game is over and the real layout is shown, so no odds are needed.';
        break;
      case 'loading': {
        badge = 'Calculating';
        tone = 'busy';
        message = 'Calculating odds for this position...';
        const seconds = o.requestKey ? Math.floor((performance.now() - o.startedAt) / 1000) : 0;
        if (seconds >= 2) detail = 'Still working (' + seconds + ' s). You can keep playing; odds appear when ready.';
        break;
      }
      case 'error':
        badge = 'Error';
        tone = 'error';
        message = o.error.message;
        retry = true;
        break;
      case 'discarded':
        badge = 'Outdated';
        message = 'The odds calculator answered for a different position, so that answer was ignored.';
        retry = true;
        break;
      case 'result': {
        const data = currentOdds();
        if (data.status === 'exact') {
          badge = 'Exact';
          tone = 'exact';
          message = data.message || 'Exact odds: every layout that fits the numbers and the mine total was counted.';
        } else if (data.status === 'approximate') {
          badge = 'Estimated';
          tone = 'estimate';
          message = data.message || 'Estimated odds: this frontier is too complex to count exactly in time, ' +
            'so layouts that fit the clues were sampled. Numbers marked ~ are approximate.';
        } else if (data.status === 'unavailable') {
          badge = 'Unavailable';
          message = data.message || 'The solver could not produce odds for this position, so no numbers are shown.';
          retry = true;
        } else if (data.status === 'not-started') {
          badge = 'Not started';
          message = data.message || READY_TEXT;
        } else {
          badge = 'Game over';
          message = data.message || 'The game is over.';
        }
        if (data.status === 'exact' || data.status === 'approximate' || data.status === 'unavailable') {
          detail = oddsSummary(data);
          chips = metaChips(data);
        }
        break;
      }
    }
    el.oddsPanel.dataset.phase = phase;
    setText(el.oddsBadge, badge);
    el.oddsBadge.dataset.tone = tone;
    setText(el.oddsMessage, message);
    setText(el.oddsDetail, detail);
    el.oddsDetail.hidden = !detail;
    renderMetaChips(chips);
    el.oddsRetry.hidden = !retry;
    renderAutosolve();
  }

  // ---------------------------------------------------------- focus and zoom

  function setRovingIndex(index) {
    const previous = boardView.cells[app.focusIndex];
    if (previous && app.focusIndex !== index) previous.tabIndex = -1;
    app.focusIndex = index;
    const next = boardView.cells[index];
    if (next) next.tabIndex = 0;
  }

  function focusCell(index) {
    const node = boardView.cells[index];
    if (!node) return;
    setRovingIndex(index);
    node.focus({ preventScroll: true });
    node.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }

  function keepFocusedCellVisible() {
    if (!el.board.contains(document.activeElement)) return;
    const node = boardView.cells[app.focusIndex];
    if (node) node.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }

  function px(value) {
    const number = parseFloat(value);
    return isFinite(number) ? number : 0;
  }

  // "Fit" picks the largest comfortable cell size that shows the whole board,
  // but never goes below a legible minimum: big boards scroll instead.
  function computeAutoCell(game) {
    const coarse = coarsePointer();
    const minCell = coarse ? 28 : 26;
    const maxCell = coarse ? 46 : 42;
    const scrollerStyle = window.getComputedStyle(el.boardScroller);
    const boardStyle = window.getComputedStyle(el.board);
    const padX = px(scrollerStyle.paddingLeft) + px(scrollerStyle.paddingRight) +
      px(boardStyle.paddingLeft) + px(boardStyle.paddingRight);
    const padY = px(scrollerStyle.paddingTop) + px(scrollerStyle.paddingBottom) +
      px(boardStyle.paddingTop) + px(boardStyle.paddingBottom);
    const maxHeight = px(scrollerStyle.maxHeight) || window.innerHeight * 0.7;
    const availWidth = el.boardFrame.clientWidth - padX - 4;
    const availHeight = maxHeight - padY - 4;
    const fitWidth = Math.floor((availWidth - (game.width - 1) * CELL_GAP) / game.width);
    const fitHeight = Math.floor((availHeight - (game.height - 1) * CELL_GAP) / game.height);
    return clamp(Math.min(fitWidth, fitHeight), minCell, maxCell);
  }

  function applyCellSize(preserveView) {
    const game = app.game;
    if (!game) return;
    const size = app.zoom.mode === 'auto' ? computeAutoCell(game) : ZOOM_STEPS[app.zoom.index];
    if (size === app.cellSize) return;
    const scroller = el.boardScroller;
    let ratioX = 0.5;
    let ratioY = 0.5;
    if (preserveView && scroller.scrollWidth && scroller.scrollHeight) {
      ratioX = (scroller.scrollLeft + scroller.clientWidth / 2) / scroller.scrollWidth;
      ratioY = (scroller.scrollTop + scroller.clientHeight / 2) / scroller.scrollHeight;
    }
    app.cellSize = size;
    el.board.style.setProperty('--cell', size + 'px');
    if (preserveView) {
      scroller.scrollLeft = ratioX * scroller.scrollWidth - scroller.clientWidth / 2;
      scroller.scrollTop = ratioY * scroller.scrollHeight - scroller.clientHeight / 2;
    }
    renderZoomButtons();
  }

  function renderZoomButtons() {
    const size = app.cellSize || ZOOM_STEPS[app.zoom.index];
    el.zoomOut.setAttribute('aria-disabled', size <= ZOOM_STEPS[0] ? 'true' : 'false');
    el.zoomIn.setAttribute('aria-disabled', size >= ZOOM_STEPS[ZOOM_STEPS.length - 1] ? 'true' : 'false');
    el.zoomFit.setAttribute('aria-pressed', app.zoom.mode === 'auto' ? 'true' : 'false');
  }

  function zoomBy(direction) {
    const current = app.cellSize || ZOOM_STEPS[app.zoom.index];
    let index = -1;
    if (direction > 0) {
      for (let k = 0; k < ZOOM_STEPS.length; k++) {
        if (ZOOM_STEPS[k] > current) { index = k; break; }
      }
    } else {
      for (let k = ZOOM_STEPS.length - 1; k >= 0; k--) {
        if (ZOOM_STEPS[k] < current) { index = k; break; }
      }
    }
    if (index < 0) return;
    app.zoom = { mode: 'manual', index: index };
    savePrefs();
    applyCellSize(true);
    renderZoomButtons();
    keepFocusedCellVisible();
  }

  function zoomFit() {
    app.zoom = { mode: 'auto', index: app.zoom.index };
    savePrefs();
    applyCellSize(true);
    renderZoomButtons();
    keepFocusedCellVisible();
  }

  function scheduleAutoFit() {
    if (app.zoom.mode !== 'auto' || app.fitFrame) return;
    app.fitFrame = window.requestAnimationFrame(function () {
      app.fitFrame = 0;
      applyCellSize(true);
    });
  }

  // ---------------------------------------------------------------- board input

  function cellIndexFromEvent(event) {
    const target = event.target;
    const node = target && target.closest ? target.closest('.cell') : null;
    if (!node || !el.board.contains(node)) return -1;
    const index = Number(node.dataset.index);
    return validIndex(index) ? index : -1;
  }

  function cancelLongPress() {
    if (touch.timer) {
      clearTimeout(touch.timer);
      touch.timer = 0;
    }
    touch.index = -1;
  }

  function fireLongPress() {
    const index = touch.index;
    touch.index = -1;
    if (!validIndex(index)) return;
    touch.fired = true; // swallow the click that may follow this touch
    const activation = navigator.userActivation;
    if (navigator.vibrate && (!activation || activation.hasBeenActive)) {
      try { navigator.vibrate(12); } catch (e) { /* not supported */ }
    }
    toggleFlag(index);
  }

  function onBoardPointerDown(event) {
    lastPointerType = event.pointerType || 'mouse';
    touch.fired = false;
    cancelLongPress();
    const index = cellIndexFromEvent(event);
    if (index < 0) return;
    if (event.pointerType === 'touch') {
      if (!event.isPrimary) return; // a second finger (pinch) cancels the long press
      touch.index = index;
      touch.x = event.clientX;
      touch.y = event.clientY;
      touch.pointerId = event.pointerId;
      touch.timer = setTimeout(function () {
        touch.timer = 0;
        fireLongPress();
      }, LONG_PRESS_MS);
      return;
    }
    const game = app.game;
    if (event.button === 0 && !app.flagMode && game && !isTerminal(game) && !app.engine.fatal && !app.pendingStart) {
      const cell = game.cells[index];
      if (!cell.revealed && !cell.flagged) {
        app.pressing = true;
        renderFace();
      }
    }
  }

  function onBoardPointerMove(event) {
    if (event.pointerType === 'mouse') {
      const index = cellIndexFromEvent(event);
      if (index !== app.hoverIndex) {
        app.hoverIndex = index;
        renderInspector();
      }
      return;
    }
    if (touch.timer && event.pointerId === touch.pointerId) {
      const dx = event.clientX - touch.x;
      const dy = event.clientY - touch.y;
      if (dx * dx + dy * dy > TOUCH_SLOP_PX * TOUCH_SLOP_PX) cancelLongPress();
    }
  }

  function onBoardPointerLeave() {
    if (app.hoverIndex !== -1) {
      app.hoverIndex = -1;
      renderInspector();
    }
  }

  function onPointerEnd(event) {
    if (touch.timer && (event.type === 'pointercancel' || event.pointerId === touch.pointerId)) cancelLongPress();
    if (app.pressing) {
      app.pressing = false;
      renderFace();
    }
  }

  function onBoardClick(event) {
    const index = cellIndexFromEvent(event);
    if (index < 0) return;
    if (touch.fired) {
      touch.fired = false;
      event.preventDefault();
      return;
    }
    primaryAction(index, true);
  }

  function onBoardContextMenu(event) {
    const index = cellIndexFromEvent(event);
    if (index < 0) return;
    event.preventDefault();
    if (lastPointerType === 'touch') {
      // Some mobile browsers report a long press as contextmenu: treat it as
      // the same long press, exactly once.
      if (touch.timer && touch.index === index) {
        clearTimeout(touch.timer);
        touch.timer = 0;
        fireLongPress();
      }
      return;
    }
    toggleFlag(index);
  }

  function onBoardMouseDown(event) {
    // Stop middle-button autoscroll so a middle click can chord.
    if (event.button === 1 && cellIndexFromEvent(event) >= 0) event.preventDefault();
  }

  function onBoardAuxClick(event) {
    if (event.button !== 1) return;
    const index = cellIndexFromEvent(event);
    if (index < 0) return;
    event.preventDefault();
    const cell = app.game.cells[index];
    if (cell.revealed && cell.adjacent && canAct()) tryChord(index);
  }

  function onBoardKeyDown(event) {
    const index = cellIndexFromEvent(event);
    if (index < 0 || event.altKey) return;
    const game = app.game;
    const width = game.width;
    const height = game.height;
    const row = Math.floor(index / width);
    const col = index % width;
    const ctrl = event.ctrlKey || event.metaKey;
    let target;
    switch (event.key) {
      case 'ArrowUp':
        if (ctrl) return;
        target = row > 0 ? index - width : index;
        break;
      case 'ArrowDown':
        if (ctrl) return;
        target = row < height - 1 ? index + width : index;
        break;
      case 'ArrowLeft':
        if (ctrl) return;
        target = col > 0 ? index - 1 : index;
        break;
      case 'ArrowRight':
        if (ctrl) return;
        target = col < width - 1 ? index + 1 : index;
        break;
      case 'Home':
        target = ctrl ? 0 : row * width;
        break;
      case 'End':
        target = ctrl ? width * height - 1 : row * width + width - 1;
        break;
      case 'PageUp':
        target = Math.max(0, row - PAGE_ROWS) * width + col;
        break;
      case 'PageDown':
        target = Math.min(height - 1, row + PAGE_ROWS) * width + col;
        break;
      case 'Enter':
      case ' ':
      case 'Spacebar':
        if (ctrl) return;
        event.preventDefault();
        if (!event.repeat) primaryAction(index, false);
        return;
      case 'f':
      case 'F':
        if (ctrl) return;
        event.preventDefault();
        if (!event.repeat) toggleFlag(index);
        return;
      default:
        return;
    }
    event.preventDefault();
    app.hoverIndex = -1;
    clearHint();
    if (target !== index) focusCell(target);
    renderInspector();
  }

  function onBoardFocusIn(event) {
    const index = cellIndexFromEvent(event);
    if (index < 0) return;
    if (index !== app.focusIndex) setRovingIndex(index);
    renderInspector();
  }

  function onBoardFocusOut() {
    window.requestAnimationFrame(renderInspector);
  }

  // --------------------------------------------------------------- custom form

  function openCustom() {
    app.customOpen = true;
    fillCustomForm();
    clearCustomErrors();
    renderPresets();
    el.customWidth.focus();
  }

  function closeCustom(returnFocus) {
    const hadFocus = el.customForm.contains(document.activeElement);
    app.customOpen = false;
    renderPresets();
    if (!hadFocus) return;
    if (returnFocus) el.customToggle.focus();
    else restoreFocus();
  }

  function fillCustomForm() {
    el.customWidth.value = String(app.settings.width);
    el.customHeight.value = String(app.settings.height);
    el.customMines.value = String(app.settings.mines);
    updateMinesHint();
  }

  function updateMinesHint() {
    const width = parseWholeNumber(el.customWidth.value);
    const height = parseWholeNumber(el.customHeight.value);
    if (width >= MIN_SIDE && width <= MAX_SIDE && height >= MIN_SIDE && height <= MAX_SIDE) {
      const most = maxMines(width, height);
      el.customMines.max = String(most);
      setText(el.customMinesHint, '1 to ' + formatCount(most) + ' on ' + width + ' x ' + height);
    } else {
      el.customMines.removeAttribute('max');
      setText(el.customMinesHint, '1 to area minus 9');
    }
  }

  function showFieldError(input, errorNode, message) {
    if (message) input.setAttribute('aria-invalid', 'true');
    else input.removeAttribute('aria-invalid');
    setText(errorNode, message || '');
  }

  function clearCustomErrors() {
    showFieldError(el.customWidth, el.customWidthError, '');
    showFieldError(el.customHeight, el.customHeightError, '');
    showFieldError(el.customMines, el.customMinesError, '');
    setText(el.customFormError, '');
  }

  function onCustomSubmit(event) {
    event.preventDefault();
    const width = parseWholeNumber(el.customWidth.value);
    const height = parseWholeNumber(el.customHeight.value);
    const mines = parseWholeNumber(el.customMines.value);
    const errors = validateConfig(width, height, mines);
    showFieldError(el.customWidth, el.customWidthError, errors.width);
    showFieldError(el.customHeight, el.customHeightError, errors.height);
    showFieldError(el.customMines, el.customMinesError, errors.mines);
    setText(el.customFormError, '');
    const firstInvalid = errors.width ? el.customWidth : errors.height ? el.customHeight : errors.mines ? el.customMines : null;
    if (firstInvalid) {
      setText(el.customFormError, 'Please fix the highlighted field' + (Object.keys(errors).length > 1 ? 's.' : '.'));
      firstInvalid.focus();
      return;
    }
    startNewGame({ width: width, height: height, mines: mines }, { fromCustom: true, focusBoard: true });
  }

  function onCustomInput(event) {
    const input = event.target;
    if (input === el.customWidth) showFieldError(el.customWidth, el.customWidthError, '');
    if (input === el.customHeight) showFieldError(el.customHeight, el.customHeightError, '');
    if (input === el.customMines) showFieldError(el.customMines, el.customMinesError, '');
    setText(el.customFormError, '');
    updateMinesHint();
  }

  // ---------------------------------------------------------------------- init

  function setFlagMode(on) {
    if (app.flagMode === on) return;
    app.flagMode = on;
    renderHud();
    renderInspector();
    announce(on ? 'Flag mode: clicks and taps place or remove flags.' : 'Reveal mode: clicks and taps reveal cells.');
  }

  async function retryFromEmpty() {
    const hadFocus = document.activeElement === el.boardEmptyRetry;
    if (engine.status === 'ready' && !app.engine.fatal) startNewGame(app.settings);
    else {
      app.pendingStart = app.pendingStart || { config: app.settings, opts: {} };
      await loadEngine();
    }
    const active = document.activeElement;
    if (hadFocus && app.game && (!active || active === document.body)) restoreFocus();
  }

  function initElements() {
    [
      'board', 'board-frame', 'board-scroller', 'board-empty', 'board-empty-spinner', 'board-empty-icon',
      'board-empty-title', 'board-empty-text', 'board-empty-retry', 'inspector', 'game-meta',
      'notice-region', 'result', 'result-icon', 'result-title', 'result-detail', 'result-again', 'mines-left',
      'autosolve-toggle', 'autosolve-status',
      'mines-counter', 'mines-label', 'timer', 'face', 'face-use', 'mode-reveal', 'mode-flag', 'odds-toggle',
      'zoom-out', 'zoom-in', 'zoom-fit', 'new-game', 'custom-toggle', 'custom-form', 'custom-width',
      'custom-height', 'custom-mines', 'custom-width-error', 'custom-height-error', 'custom-mines-error',
      'custom-mines-hint', 'custom-form-error', 'custom-dims', 'engine-status', 'engine-status-text', 'odds-panel',
      'odds-badge', 'odds-message', 'odds-detail', 'odds-meta', 'odds-retry', 'live', 'skip-link'
    ].forEach(function (id) {
      const node = document.getElementById(id);
      if (!node) throw new Error('Missing element #' + id);
      el[id.replace(/-([a-z])/g, function (_, ch) { return ch.toUpperCase(); })] = node;
    });
    el.resultAgainLabel = el.resultAgain.querySelector('span');
    el.presetButtons = Array.prototype.slice.call(document.querySelectorAll('.preset[data-preset]'));
  }

  function bindEvents() {
    el.presetButtons.forEach(function (button) {
      button.addEventListener('click', function () {
        const key = button.dataset.preset;
        if (key === 'custom') {
          if (app.customOpen) closeCustom(true);
          else openCustom();
          return;
        }
        if (app.customOpen) closeCustom(false);
        startNewGame(PRESETS[key]);
      });
    });
    el.newGame.addEventListener('click', function () { startNewGame(app.settings); });
    el.face.addEventListener('click', function () { startNewGame(app.settings); });
    el.resultAgain.addEventListener('click', function () {
      const game = app.game;
      const config = game ? { width: game.width, height: game.height, mines: game.mines } : app.settings;
      startNewGame(config, { focusBoard: true });
    });
    el.modeReveal.addEventListener('click', function () { setFlagMode(false); });
    el.modeFlag.addEventListener('click', function () { setFlagMode(true); });
    el.oddsToggle.addEventListener('click', function () { setOddsEnabled(!app.odds.enabled); });
    el.autosolveToggle.addEventListener('click', function () { setAutosolveEnabled(!app.autosolve.enabled); });
    el.oddsRetry.addEventListener('click', function () {
      retryOdds();
      if (el.oddsRetry.hidden) el.oddsToggle.focus();
    });
    el.zoomOut.addEventListener('click', function () {
      if (el.zoomOut.getAttribute('aria-disabled') !== 'true') zoomBy(-1);
    });
    el.zoomIn.addEventListener('click', function () {
      if (el.zoomIn.getAttribute('aria-disabled') !== 'true') zoomBy(1);
    });
    el.zoomFit.addEventListener('click', zoomFit);
    el.boardEmptyRetry.addEventListener('click', retryFromEmpty);
    el.skipLink.addEventListener('click', function (event) {
      if (app.game && boardView.cells[app.focusIndex]) {
        event.preventDefault();
        focusCell(app.focusIndex);
      }
    });

    el.customForm.addEventListener('submit', onCustomSubmit);
    el.customForm.addEventListener('input', onCustomInput);
    el.customForm.addEventListener('keydown', function (event) {
      if (event.key === 'Escape') {
        event.preventDefault();
        closeCustom(true);
      }
    });

    const board = el.board;
    board.addEventListener('click', onBoardClick);
    board.addEventListener('contextmenu', onBoardContextMenu);
    board.addEventListener('mousedown', onBoardMouseDown);
    board.addEventListener('auxclick', onBoardAuxClick);
    board.addEventListener('pointerdown', onBoardPointerDown);
    board.addEventListener('pointermove', onBoardPointerMove);
    board.addEventListener('pointerleave', onBoardPointerLeave);
    board.addEventListener('keydown', onBoardKeyDown);
    board.addEventListener('focusin', onBoardFocusIn);
    board.addEventListener('focusout', onBoardFocusOut);
    board.addEventListener('dragstart', function (event) { event.preventDefault(); });
    window.addEventListener('pointerup', onPointerEnd);
    window.addEventListener('pointercancel', onPointerEnd);

    if (window.ResizeObserver) new ResizeObserver(scheduleAutoFit).observe(el.boardFrame);
    window.addEventListener('resize', scheduleAutoFit);
  }

  // Every load starts a new game in this tab; only preferences carry over.
  function init() {
    initElements();
    store.remove(LEGACY_GAME_KEY);
    loadSettings();
    loadPrefs();
    bindEvents();
    fillCustomForm();
    render();
    loadEngine();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
