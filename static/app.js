/*
 * Minesweeper front end.
 *
 * Vanilla JavaScript with no build step and no dependencies. Everything shown
 * comes from the same-origin JSON API served by server.py:
 *
 *   POST /api/games                               create a game
 *   GET  /api/games/{id}                          current game state
 *   POST /api/games/{id}/actions                  reveal / flag / chord
 *   GET  /api/games/{id}/probabilities?revision=N mine odds for one revision
 *
 * Moves and odds are independent: a move never waits for the solver. Odds are
 * only drawn when they belong to the game id AND revision on screen, and every
 * odds request carries a generation number so late answers are ignored even
 * if aborting the fetch came too late.
 */
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
  const BUSY_DELAY_MS = 300;
  const HINT_MS = 4000;
  const TIMEOUTS = { load: 15000, create: 20000, action: 20000, probe: 6000, odds: 90000 };
  const STORAGE_KEYS = {
    game: 'minesweeper.gameId',
    settings: 'minesweeper.settings',
    prefs: 'minesweeper.prefs'
  };
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
    connection: 'connecting',
    loading: false, // restoring a saved game
    loadSeq: 0,
    loadError: null, // shown when there is no game at all
    pendingCreate: null,
    pendingAction: null,
    busyTimer: 0,
    busyVisible: false,
    gameGone: false,
    syncing: false,
    needsResync: false, // a move's outcome is unknown; reload before the next move
    uncertainSeq: 0, // bumped whenever a move's outcome becomes unknown
    notice: null,
    hintTimer: 0,
    hintActive: false,
    timerHandle: 0,
    timerText: '',
    reconnectTimer: 0,
    reconnectDelay: 2000,
    fitFrame: 0,
    autosolve: { enabled: false, timer: 0, attemptedKey: null },
    odds: {
      enabled: false,
      gen: 0, // bumped whenever an in-flight request becomes obsolete
      controller: null,
      requestKey: null,
      startedAt: 0,
      ticker: 0,
      data: null, // parsed odds valid for dataKey only
      dataKey: null,
      cache: null, // last good answer, reused after a failed or rejected move
      error: null,
      discardedKey: null,
      retryTimer: 0,
      announceNext: false
    }
  };

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

  function gameUrl(id) {
    return '/api/games/' + encodeURIComponent(id);
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

  // ------------------------------------------------------------------- network

  class ApiError extends Error {
    constructor(message, info) {
      super(message);
      this.name = 'ApiError';
      this.kind = (info && info.kind) || 'http'; // http | network | timeout | aborted | parse
      this.status = (info && info.status) || 0;
      this.code = (info && info.code) || '';
      this.state = (info && info.state) || null;
    }
  }

  async function api(method, path, options) {
    const opts = options || {};
    const controller = new AbortController();
    const outer = opts.signal || null;
    let timedOut = false;
    const relayAbort = function () { controller.abort(); };
    if (outer) {
      if (outer.aborted) controller.abort();
      else outer.addEventListener('abort', relayAbort);
    }
    const timer = setTimeout(function () {
      timedOut = true;
      controller.abort();
    }, opts.timeout || 15000);
    const headers = { Accept: 'application/json' };
    const init = {
      method: method,
      headers: headers,
      signal: controller.signal,
      cache: 'no-store',
      credentials: 'same-origin'
    };
    if (opts.body !== undefined) {
      headers['Content-Type'] = 'application/json';
      init.body = JSON.stringify(opts.body);
    }
    let response;
    let text;
    try {
      response = await fetch(path, init);
      text = await response.text();
    } catch (err) {
      if (outer && outer.aborted) throw new ApiError('Request cancelled.', { kind: 'aborted' });
      if (timedOut) throw new ApiError('The server did not answer in time.', { kind: 'timeout' });
      if (err && err.name === 'AbortError') throw new ApiError('Request cancelled.', { kind: 'aborted' });
      throw new ApiError('Could not reach the game server.', { kind: 'network' });
    } finally {
      clearTimeout(timer);
      if (outer) outer.removeEventListener('abort', relayAbort);
    }
    setConnection('online');
    let data = null;
    let parsed = false;
    if (text) {
      try {
        data = JSON.parse(text);
        parsed = true;
      } catch (e) {
        parsed = false;
      }
    }
    if (!response.ok) {
      const body = parsed && data && typeof data === 'object' ? data : null;
      const info = body && body.error && typeof body.error === 'object' ? body.error : null;
      const message = info && typeof info.message === 'string' && info.message.trim()
        ? info.message.trim()
        : 'The server answered with an error (HTTP ' + response.status + ').';
      throw new ApiError(message, {
        kind: 'http',
        status: response.status,
        code: info && typeof info.code === 'string' ? info.code : '',
        state: body ? body.state : null
      });
    }
    if (!parsed) {
      throw new ApiError('The server sent a reply that is not JSON (HTTP ' + response.status + ').', {
        kind: 'parse',
        status: response.status
      });
    }
    return data;
  }

  function describeError(err) {
    if (err.kind === 'network') return 'Could not reach the game server.';
    if (err.kind === 'timeout') return 'The server did not answer in time.';
    return err.message;
  }

  // ------------------------------------------------------------------- parsing

  // Strict validation against the documented contract: anything malformed is
  // rejected (null) so the caller reports a parse error. Nothing is patched
  // with plausible defaults. Only the public fields are read; hidden cells keep
  // mine and adjacent as null until the server discloses the layout at the end.
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

  // Mirrors the server's own validation: exact and approximate answers are
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

  // ---------------------------------------------------------------- connection

  function setConnection(state) {
    if (app.connection === state) return;
    const previous = app.connection;
    app.connection = state;
    renderConnection();
    if (state === 'offline') {
      scheduleReconnect(true);
      return;
    }
    stopReconnect();
    if (state === 'online' && previous === 'offline') {
      clearNotice('offline');
      announce('Connected to the game server again.');
      if (app.odds.error && app.odds.error.kind === 'network') {
        app.odds.error = null;
        syncOdds();
        renderOddsPanel();
      }
    }
    // Being reachable again never settles an uncertain move by itself; only an
    // authoritative reload of the game does.
    if (state === 'online' && app.needsResync) resync({ quiet: true });
  }

  function scheduleReconnect(reset) {
    if (reset) app.reconnectDelay = 2000;
    if (app.reconnectTimer) return;
    app.reconnectTimer = setTimeout(reconnectTick, app.reconnectDelay);
  }

  function stopReconnect() {
    if (app.reconnectTimer) {
      clearTimeout(app.reconnectTimer);
      app.reconnectTimer = 0;
    }
  }

  async function reconnectTick() {
    app.reconnectTimer = 0;
    if (app.connection !== 'offline') return;
    if (!document.hidden) await resync({ quiet: true });
    if (app.connection === 'offline') {
      app.reconnectDelay = Math.min(app.reconnectDelay * 2, 30000);
      scheduleReconnect(false);
    }
  }

  function showOfflineNotice(prefix) {
    if (app.notice && app.notice.id === 'uncertain') {
      // Keep the more important "move outcome unknown" notice on screen.
      hint('Still cannot reach the game server. This page keeps retrying.');
      return;
    }
    showNotice({
      id: 'offline',
      tone: 'error',
      icon: 'i-offline',
      title: 'Cannot reach the game server',
      message: (prefix ? prefix + ' ' : '') +
        'Check that python3 server.py is still running. This page keeps retrying on its own.',
      actions: [{ label: 'Retry now', primary: true, run: function () { resync(); } }]
    });
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

  // --------------------------------------------------------- busy and timer

  function isBusy() {
    return !!(app.pendingAction || app.pendingCreate);
  }

  function refreshBusy() {
    const busy = isBusy();
    if (busy && !app.busyVisible && !app.busyTimer) {
      app.busyTimer = setTimeout(function () {
        app.busyTimer = 0;
        if (isBusy()) {
          app.busyVisible = true;
          renderBusy();
        }
      }, BUSY_DELAY_MS);
    }
    if (!busy) {
      clearTimeout(app.busyTimer);
      app.busyTimer = 0;
      app.busyVisible = false;
    }
    renderBusy();
  }

  function renderBusy() {
    const busy = isBusy();
    el.boardFrame.classList.toggle('is-busy', busy);
    el.busyPill.hidden = !(busy && app.busyVisible && app.game);
    el.board.setAttribute('aria-busy', busy ? 'true' : 'false');
  }

  function timerRunning() {
    return !!app.game && app.game.status === 'playing' && !app.gameGone;
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

  const OFFLINE_EMPTY = {
    offline: true,
    title: 'Cannot reach the game server',
    text: 'Make sure python3 server.py is running, then try again. This page also keeps retrying on its own.'
  };

  async function initialLoad() {
    if (app.loading || app.pendingCreate) return;
    const savedId = store.get(STORAGE_KEYS.game);
    if (!savedId) {
      await startNewGame(app.settings);
      return;
    }
    const seq = ++app.loadSeq;
    app.loading = true;
    app.loadError = null;
    render();
    let data = null;
    let error = null;
    try {
      data = await api('GET', gameUrl(savedId), { timeout: TIMEOUTS.load });
    } catch (err) {
      error = err;
    }
    if (seq !== app.loadSeq) return; // a new game was requested meanwhile
    app.loading = false;
    const restored = error ? null : parseGame(data);
    if (restored) {
      adoptGame(restored);
      if (restored.status === 'playing') announce('Restored your game in progress.');
      return;
    }
    if (error && (error.kind === 'network' || error.kind === 'timeout')) {
      if (error.kind === 'network') setConnection('offline');
      app.loadError = error.kind === 'network' ? OFFLINE_EMPTY : {
        title: 'The server is not responding',
        text: 'It did not answer in time. Try again in a moment.'
      };
      render();
      return;
    }
    const missing = !!error && error.kind === 'http' && error.status === 404;
    const reason = error ? describeError(error) : 'The saved game could not be read.';
    store.remove(STORAGE_KEYS.game);
    await startNewGame(app.settings);
    if (app.game && !app.notice) {
      showNotice({
        id: 'restore',
        tone: 'info',
        title: 'Started a new game',
        message: missing
          ? 'Your previous game was not found. The server keeps games in memory only, so they are lost when it restarts.'
          : 'Your previous game could not be restored (' + reason + ').'
      });
    }
  }

  async function startNewGame(config, options) {
    const opts = options || {};
    const request = { width: config.width, height: config.height, mines: config.mines };
    app.settings = {
      preset: presetKeyFor(request.width, request.height, request.mines),
      width: request.width,
      height: request.height,
      mines: request.mines
    };
    saveSettings();
    if (app.pendingCreate) app.pendingCreate.controller.abort();
    const pending = { controller: new AbortController() };
    app.pendingCreate = pending;
    app.loadSeq++; // a restore still in flight is now obsolete
    app.loading = false;
    clearHint();
    clearOddsView();
    refreshBusy();
    render();
    let data = null;
    let error = null;
    try {
      data = await api('POST', '/api/games', {
        body: request,
        signal: pending.controller.signal,
        timeout: TIMEOUTS.create
      });
    } catch (err) {
      error = err;
    }
    if (app.pendingCreate !== pending) return; // superseded by a newer request
    app.pendingCreate = null;
    const next = error ? null : parseGame(data);
    if (!error && !next) error = new ApiError('The server sent game data this page cannot read.', { kind: 'parse' });
    if (error) {
      refreshBusy();
      if (error.kind !== 'aborted') handleCreateError(error, request, opts);
      render();
      syncOdds();
      return;
    }
    // A move still in flight belongs to the previous game; drop it.
    if (app.pendingAction) {
      app.pendingAction.controller.abort();
      app.pendingAction = null;
    }
    refreshBusy();
    clearNotice();
    if (opts.fromCustom) closeCustom(false);
    adoptGame(next);
    announce('New ' + configName(next) + ' game: ' + next.width + ' by ' + next.height + ' with ' +
      plural(next.mines, 'mine') + '. Your first reveal is always safe.');
    if (opts.focusBoard) focusCell(app.focusIndex);
  }

  function handleCreateError(err, request, opts) {
    if (err.kind === 'network') setConnection('offline');
    if (!app.game) {
      app.loadError = err.kind === 'network' ? OFFLINE_EMPTY : {
        title: 'Could not start a game',
        text: describeError(err)
      };
      return;
    }
    if (opts.fromCustom && err.kind === 'http' && err.status >= 400 && err.status < 500) {
      el.customFormError.textContent = 'The server rejected these settings: ' + err.message;
      return;
    }
    if (err.kind === 'network') {
      showOfflineNotice('No new game was started; your current game is unchanged.');
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
    app.gameGone = false;
    app.loadError = null;
    store.set(STORAGE_KEYS.game, next.id);
    if (!sameGame) {
      resetOdds();
      app.needsResync = false; // uncertainty about the previous game no longer matters
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

  async function sendAction(action, index) {
    const game = app.game;
    const automatic = action === 'autosolve';
    const pending = { controller: new AbortController(), gameId: game.id, action: action };
    const body = automatic ? { revision: game.revision } : {
      action: action,
      row: Math.floor(index / game.width),
      col: index % game.width,
      revision: game.revision
    };
    app.pendingAction = pending;
    clearHint();
    clearOddsView(); // stale odds disappear the moment a move starts
    refreshBusy();
    render();
    let data = null;
    let error = null;
    try {
      data = await api('POST', gameUrl(game.id) + (automatic ? '/autosolve' : '/actions'), {
        body: body,
        signal: pending.controller.signal,
        timeout: TIMEOUTS.action
      });
    } catch (err) {
      error = err;
    }
    if (app.pendingAction !== pending) return; // dropped because a new game started
    app.pendingAction = null;
    refreshBusy();
    const current = app.game;
    if (!current || current.id !== pending.gameId) {
      render();
      syncOdds();
      return;
    }
    const next = error ? null : parseGame(data);
    if (!error && !next) error = new ApiError('The server sent game data this page cannot read.', { kind: 'parse' });
    if (!error && next.id !== current.id) {
      error = new ApiError('The server answered for a different game.', { kind: 'parse' });
    }
    if (error) {
      handleActionError(error, action);
      if (automatic) setAutosolveEnabled(false);
      render();
      syncOdds();
      return;
    }
    if (app.notice && app.notice.id !== 'gone') clearNotice();
    adoptGame(next);
    announceMove(action, index, current, next);
  }

  function actionNoun(action) {
    if (action === 'autosolve') return 'automatic move batch';
    if (action === 'flag') return 'flag change';
    if (action === 'chord') return 'chord';
    return 'reveal';
  }

  // A failed POST does not prove the move was not applied: the server may have
  // applied it and the answer been lost. Moves and odds pause until a reload of
  // the game that started after this point succeeds (see resync).
  function markMoveUncertain(action, err) {
    app.needsResync = true;
    app.uncertainSeq++;
    cancelOddsRequest();
    let cause = 'The server reported an error';
    if (err.kind === 'network') cause = 'The connection to the game server failed before its answer arrived';
    else if (err.kind === 'timeout') cause = 'The server did not answer in time';
    else if (err.kind === 'parse') cause = 'The server sent an answer this page cannot read';
    showNotice({
      id: 'uncertain',
      tone: err.kind === 'network' ? 'error' : 'warning',
      icon: err.kind === 'network' ? 'i-offline' : 'i-alert',
      title: 'Your ' + actionNoun(action) + ' may or may not have been applied',
      message: cause + ', so it is not known whether the server applied it. The board will be reloaded ' +
        'from the server before your next move.' +
        (err.kind === 'network' ? ' Check that python3 server.py is still running; this page keeps retrying.' : ''),
      actions: [{ label: 'Reload board', primary: true, run: function () { resync(); } }]
    });
    render();
    if (err.kind !== 'network') resync({ quiet: true }); // offline: the reconnect loop reloads
  }

  function handleActionError(err, action) {
    if (err.kind === 'aborted') return;
    if (err.kind === 'network' || err.kind === 'timeout' || err.kind === 'parse' ||
        (err.kind === 'http' && err.status >= 500)) {
      if (err.kind === 'network') setConnection('offline');
      markMoveUncertain(action, err);
      return;
    }
    if (err.status === 404) {
      markGameGone();
      return;
    }
    const current = app.game;
    const latest = parseGame(err.state);
    const sameGame = !!latest && !!current && latest.id === current.id;
    if (err.status === 409 && err.code === 'game_over') {
      if (sameGame) adoptGame(latest);
      showNotice({ id: 'action', tone: 'warning', title: 'This game is already over', message: err.message });
      return;
    }
    if (err.status === 409) {
      if (sameGame) adoptGame(latest);
      showNotice({
        id: 'stale',
        tone: 'warning',
        title: 'Your move was not applied',
        message: 'The game changed before your ' + actionNoun(action) + ' arrived (for example in another tab), ' +
          'so it was not applied to a position you had not seen. ' +
          (sameGame ? 'The latest board is shown now; check it and try again.' : err.message),
        actions: sameGame ? [] : [{ label: 'Reload game', run: function () { resync(); } }]
      });
      return;
    }
    if (sameGame) adoptGame(latest);
    showNotice({ id: 'action', tone: 'error', title: 'Move rejected', message: err.message });
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

  function markGameGone() {
    const game = app.game;
    if (game && timerRunning()) {
      game.elapsed = currentElapsed();
      game.receivedAt = performance.now();
    }
    app.gameGone = true;
    app.needsResync = false;
    cancelOddsRequest();
    store.remove(STORAGE_KEYS.game);
    updateTimer();
    showNotice({
      id: 'gone',
      tone: 'warning',
      title: 'This game is no longer on the server',
      message: 'The server keeps games in memory only, so they vanish when it restarts. Start a new game to keep playing.',
      actions: [{
        label: 'Start new game',
        primary: true,
        run: function () { startNewGame(app.settings, { focusBoard: true }); }
      }]
    });
    render();
  }

  // Re-read the current game from the server (reconnects, timeouts, retries).
  // This authoritative reload is the only thing that settles an uncertain move,
  // and only if it started after the move became uncertain.
  async function resync(options) {
    const opts = options || {};
    if (!app.game) {
      if (!app.loading && !app.pendingCreate) await initialLoad();
      return;
    }
    if (app.syncing) return;
    app.syncing = true;
    const id = app.game.id;
    const seq = app.uncertainSeq;
    try {
      const data = await api('GET', gameUrl(id), { timeout: TIMEOUTS.probe });
      const next = parseGame(data);
      if (!next || next.id !== id) throw new ApiError('The server sent game data this page cannot read.', { kind: 'parse' });
      if (app.game && app.game.id === id) {
        const changed = next.revision !== app.game.revision;
        const settled = app.needsResync && seq === app.uncertainSeq;
        if (settled) app.needsResync = false;
        adoptGame(next);
        clearNotice('offline');
        if (settled) {
          showNotice({
            id: 'resynced',
            tone: 'info',
            title: 'Board reloaded from the server',
            message: changed
              ? 'The board changed, so your last move was probably applied. Check it before your next move.'
              : 'Nothing changed, so your last move was not applied.'
          });
        } else if (changed && !opts.quiet) {
          announce('Loaded the latest board from the server.');
        }
      }
    } catch (err) {
      if (err.kind === 'http' && err.status === 404) {
        if (app.game && app.game.id === id && !app.gameGone) markGameGone();
      } else if (err.kind === 'network') {
        setConnection('offline');
        if (!opts.quiet) showOfflineNotice('');
      } else if (!opts.quiet) {
        showNotice({ id: 'action', tone: 'error', title: 'Could not refresh the game', message: describeError(err) });
      }
    } finally {
      app.syncing = false;
      // A move became uncertain while this reload was in flight: reload again.
      if (app.needsResync && seq !== app.uncertainSeq && app.connection !== 'offline') resync({ quiet: true });
      else syncAutosolve();
    }
  }

  // ------------------------------------------------------------------- moves

  function canAct() {
    const game = app.game;
    if (!game) return false;
    if (app.pendingCreate) {
      hint('A new game is starting...');
      return false;
    }
    if (app.pendingAction) return false; // one move at a time; the busy pill shows progress
    if (app.gameGone) {
      hint('This game is no longer on the server. Start a new game to keep playing.');
      return false;
    }
    if (app.needsResync || app.syncing) {
      hint(app.needsResync
        ? 'Your last move may or may not have been applied. Reloading the board from the server first...'
        : 'Reloading the board from the server...');
      if (app.needsResync) resync({ quiet: true });
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
    if (!auto.enabled || auto.timer || !data || isBusy() || app.syncing ||
        app.needsResync || !hasAutomaticMoves(game, data)) return;
    const key = oddsKey(game);
    if (auto.attemptedKey === key) return;
    auto.timer = setTimeout(function () {
      auto.timer = 0;
      const latest = currentOdds();
      if (!auto.enabled || !latest || oddsKey(app.game) !== key || isBusy() ||
          app.syncing || app.needsResync || !hasAutomaticMoves(app.game, latest)) return;
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
      : 'Autosolve paused. Any safe-move batch already sent will finish.');
  }

  function renderAutosolve() {
    const auto = app.autosolve;
    const game = app.game;
    const running = app.pendingAction && app.pendingAction.action === 'autosolve';
    el.autosolveToggle.setAttribute('aria-checked', auto.enabled ? 'true' : 'false');
    el.autosolveStatus.hidden = !auto.enabled && !running;
    let text = '';
    if (!auto.enabled && running) {
      text = 'Pausing autosolve; the safe-move batch already sent is finishing.';
    } else if (auto.enabled) {
      const data = currentOdds();
      if (!game || app.pendingCreate) text = 'Autosolve is waiting for the board.';
      else if (game.status === 'ready') text = 'Choose your first cell. Autosolve will then play only certain moves.';
      else if (isTerminal(game)) text = 'Game over. Autosolve never chooses uncertain cells.';
      else if (app.gameGone || app.needsResync || app.syncing) text = 'Autosolve is waiting for the board to be reloaded.';
      else if (isBusy()) text = 'Autosolve is opening proven-safe cells and updating flags...';
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
    if (!o.enabled || !game || !o.data || app.gameGone || game.status !== 'playing') return null;
    if (o.dataKey !== oddsKey(game)) return null;
    if (o.data.gameId !== game.id || o.data.revision !== game.revision) return null;
    return o.data;
  }

  function oddsPhase() {
    const o = app.odds;
    const game = app.game;
    if (!o.enabled) return 'off';
    if (app.pendingCreate) return 'waiting';
    if (!game) return 'nogame';
    if (app.gameGone) return 'gone';
    if (game.status === 'ready') return 'ready';
    if (isTerminal(game)) return 'ended';
    if (app.pendingAction || app.needsResync) return 'waiting';
    const key = oddsKey(game);
    if (currentOdds()) return 'result';
    if (o.error && o.error.key === key) return 'error';
    if (o.discardedKey === key) return 'discarded';
    return 'loading';
  }

  function cancelOddsRequest() {
    cancelAutosolve();
    const o = app.odds;
    o.gen++; // late answers from the old request are ignored even if abort was too late
    if (o.controller) {
      o.controller.abort();
      o.controller = null;
    }
    if (o.retryTimer) {
      clearTimeout(o.retryTimer);
      o.retryTimer = 0;
    }
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
    if (!o.enabled || !game || app.gameGone || app.pendingCreate || app.pendingAction || app.needsResync ||
        game.status !== 'playing') {
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

  async function requestOdds(game, key, attempt) {
    const o = app.odds;
    const tries = attempt || 1;
    cancelOddsRequest();
    const gen = ++o.gen;
    const controller = new AbortController();
    o.controller = controller;
    o.requestKey = key;
    if (tries === 1) o.startedAt = performance.now();
    startOddsTicker();
    renderOddsPanel();
    let data = null;
    let error = null;
    try {
      data = await api('GET', gameUrl(game.id) + '/probabilities?revision=' + encodeURIComponent(String(game.revision)), {
        signal: controller.signal,
        timeout: TIMEOUTS.odds
      });
    } catch (err) {
      error = err;
    }
    if (gen !== o.gen) return; // a move, new game or toggle made this answer obsolete
    o.controller = null;
    o.requestKey = null;
    stopOddsTicker();
    const current = app.game;
    if (!o.enabled || !current || oddsKey(current) !== key) {
      renderOddsPanel();
      return;
    }
    if (error) {
      if (error.kind === 'aborted') {
        renderOddsPanel();
        return;
      }
      if (error.kind === 'http' && error.status === 409) {
        handleOddsConflict(error, key);
        return;
      }
      if (error.kind === 'http' && error.status === 404) {
        markGameGone();
        return;
      }
      if (error.kind === 'http' && error.status === 503 && tries < 4) {
        // The solver is busy with other positions: wait briefly and ask again,
        // keeping the "calculating" state (same key, fresh generation).
        o.requestKey = key;
        startOddsTicker();
        o.retryTimer = setTimeout(function () {
          o.retryTimer = 0;
          const now = app.game;
          if (!o.enabled || !now || oddsKey(now) !== key || app.pendingAction || app.pendingCreate) {
            o.requestKey = null;
            syncOdds();
            return;
          }
          requestOdds(now, key, tries + 1);
        }, 1200 * tries);
        renderOddsPanel();
        return;
      }
      if (error.kind === 'network') setConnection('offline');
      o.error = { key: key, kind: error.kind, message: oddsErrorText(error) };
      renderOddsPanel();
      return;
    }
    const parsed = parseOdds(data);
    if (!parsed) {
      o.error = { key: key, kind: 'parse', message: 'The server sent odds this page cannot read, so none are shown.' };
      renderOddsPanel();
      return;
    }
    if (parsed.gameId !== current.id || parsed.revision !== current.revision) {
      // An answer computed for another snapshot is never drawn.
      o.discardedKey = key;
      renderOddsPanel();
      if (parsed.gameId === current.id && parsed.revision > current.revision) resync({ quiet: true });
      return;
    }
    if (!oddsFitBoard(parsed, current)) {
      o.error = { key: key, kind: 'parse', message: 'The server sent odds that do not match this board, so none are shown.' };
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

  function oddsErrorText(err) {
    if (err.kind === 'network') return 'Could not reach the server to calculate odds.';
    if (err.kind === 'timeout') {
      return 'The solver did not answer within ' + Math.round(TIMEOUTS.odds / 1000) +
        ' seconds. Try again, or keep playing; odds are requested again after your next move.';
    }
    if (err.kind === 'http') return 'The server could not calculate odds: ' + err.message;
    return err.message;
  }

  function handleOddsConflict(error, key) {
    const current = app.game;
    const latest = parseGame(error.state);
    if (latest && current && latest.id === current.id && latest.revision > current.revision) {
      adoptGame(latest);
      showNotice({
        id: 'stale',
        tone: 'info',
        title: 'Board updated',
        message: 'This game changed elsewhere (for example in another tab), so the latest board is shown.'
      });
      return;
    }
    app.odds.error = {
      key: key,
      kind: 'http',
      message: error.message || 'The odds request did not match the board on the server.'
    };
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
    renderConnection();
    renderPresets();
    renderHud();
    renderResult();
    renderBoard();
    renderOddsPanel();
    renderInspector();
    renderGameMeta();
  }

  function renderConnection() {
    el.connection.dataset.state = app.connection;
    setText(el.connectionText, app.connection === 'online' ? 'Server connected'
      : app.connection === 'offline' ? 'Server unreachable' : 'Connecting...');
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
    const busy = app.loading || !!app.pendingCreate;
    el.boardEmptySpinner.hidden = !busy;
    el.boardEmptyIcon.hidden = busy || !(app.loadError && app.loadError.offline);
    el.boardEmptyRetry.hidden = busy;
    if (busy) {
      setText(el.boardEmptyTitle, app.loading ? 'Restoring your game...' : 'Starting a new game...');
      setText(el.boardEmptyText, 'Contacting the local game server.');
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
      // The server disclosed the layout because the game is over.
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
    renderBusy();
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
    const interactive = !isTerminal(game) && !app.gameGone && !app.needsResync;
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
  // repeated here when the server did not send a message.
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
    if (isNum(meta.elapsed_ms)) chips.push([formatMs(meta.elapsed_ms), 'solve time', 'Time the server spent on this position']);
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
      case 'gone':
        badge = 'Unavailable';
        message = 'This game is no longer on the server, so there are no odds to show.';
        break;
      case 'waiting':
        badge = 'Updating';
        tone = 'busy';
        message = app.pendingCreate ? 'Starting a new game...'
          : app.needsResync ? 'Waiting until the board is reloaded from the server...'
          : 'Updating after your move...';
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
        message = 'The server answered for a different position, so that answer was ignored.';
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
    if (event.button === 0 && !app.flagMode && game && !isTerminal(game) && !isBusy() && !app.gameGone) {
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
    await initialLoad();
    const active = document.activeElement;
    if (hadFocus && app.game && (!active || active === document.body)) restoreFocus();
  }

  function initElements() {
    [
      'board', 'board-frame', 'board-scroller', 'board-empty', 'board-empty-spinner', 'board-empty-icon',
      'board-empty-title', 'board-empty-text', 'board-empty-retry', 'busy-pill', 'inspector', 'game-meta',
      'notice-region', 'result', 'result-icon', 'result-title', 'result-detail', 'result-again', 'mines-left',
      'autosolve-toggle', 'autosolve-status',
      'mines-counter', 'mines-label', 'timer', 'face', 'face-use', 'mode-reveal', 'mode-flag', 'odds-toggle',
      'zoom-out', 'zoom-in', 'zoom-fit', 'new-game', 'custom-toggle', 'custom-form', 'custom-width',
      'custom-height', 'custom-mines', 'custom-width-error', 'custom-height-error', 'custom-mines-error',
      'custom-mines-hint', 'custom-form-error', 'custom-dims', 'connection', 'connection-text', 'odds-panel',
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
    document.addEventListener('visibilitychange', function () {
      if (!document.hidden && app.connection === 'offline') resync({ quiet: true });
    });
    window.addEventListener('online', function () {
      if (app.connection === 'offline') resync({ quiet: true });
    });
    window.addEventListener('pageshow', function (event) {
      if (event.persisted) resync({ quiet: true });
    });
  }

  function init() {
    initElements();
    loadSettings();
    loadPrefs();
    bindEvents();
    fillCustomForm();
    render();
    initialLoad();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
