// ABI marshalling tests for static/wasm-host.js: buffer decoding against the
// frozen layouts of c/engine.h and c/wasm_api.h, argument validation, status
// names and wording, and the worker message protocol. No WASM needed: the
// buffers are built here byte by byte at the documented offsets.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { importSite } from './support/site.mjs';

const host = await importSite('wasm-host.js');
const {
  EngineError, ProcExit, STATUS, cellCount, checkSolveRequest, checkWorkerMessage, decodeLimits, decodeResult,
  decodeView, describeThrown, errorFromPayload, errorFromStatus, errorPayload, isU32, kindOfStatus,
  observationSize, probabilityMessage, readObservationHeader, resultSize, statusMessage, statusName, viewSize
} = host;

const pad8 = (n) => Math.ceil(n / 8) * 8;

// ------------------------------------------------------------------ builders

function viewBuffer(spec) {
  const width = spec.width ?? 5;
  const height = spec.height ?? 5;
  const total = width * height;
  const cells = spec.cells || new Array(total).fill(0x0F);
  const bytes = new Uint8Array(spec.length ?? pad8(48 + total));
  const dv = new DataView(bytes.buffer);
  let flags = 0;
  let revealed = 0;
  cells.forEach((b, i) => {
    bytes[48 + i] = b;
    if (b & 0x20) flags++;
    if (b & 0x10) revealed++;
  });
  const header = [spec.magic ?? 0x4D530001, spec.version ?? 1, spec.generation ?? 1, spec.revision ?? 0,
    spec.status ?? 1, width, height, spec.mines ?? 3, spec.flags ?? flags, spec.revealed ?? revealed];
  header.forEach((v, i) => dv.setUint32(4 * i, v, true));
  dv.setFloat64(40, spec.elapsedMs ?? 0, true);
  return bytes;
}

// Observation of Python's (width, height, total, clues).
function obsBuffer(width, height, total, clues = {}, overrides = {}) {
  const cells = width * height;
  const bytes = new Uint8Array(overrides.length ?? pad8(32 + cells));
  const dv = new DataView(bytes.buffer);
  bytes.fill(0xFF, 32, 32 + cells);
  for (const [index, clue] of Object.entries(clues)) bytes[32 + Number(index)] = clue;
  [overrides.magic ?? 0x4D530002, overrides.version ?? 1, width, height, total, Object.keys(clues).length, 0, 0]
    .forEach((v, i) => dv.setUint32(4 * i, v, true));
  return bytes;
}

const RESULT_FIELDS = {
  frontier_cells: 40, components: 44, unconstrained_cells: 48, samples: 52, exact_components: 56,
  sampled_components: 60, sample_attempts: 64, nodes: 68, hidden_cells: 72, remaining_mines: 76,
  propagated_cells: 80, pair_reasoning_complete: 84
};

function resultBuffer(spec) {
  const cells = spec.width * spec.height;
  const bytes = new Uint8Array(spec.length ?? pad8(120 + 9 * cells));
  const dv = new DataView(bytes.buffer);
  let safe = 0;
  let mines = 0;
  (spec.cells || []).forEach((cell, i) => {
    if (!cell) return;
    dv.setFloat64(120 + 8 * i, cell.value ?? 0, true);
    bytes[120 + 8 * cells + i] = cell.flags;
    if (cell.flags & 2) safe++;
    if (cell.flags & 4) mines++;
  });
  [0x4D530004, 1, spec.status, spec.reason ?? 0, spec.width, spec.height, spec.total ?? 1, spec.revealed ?? 0]
    .forEach((v, i) => dv.setUint32(4 * i, v, true));
  dv.setBigUint64(32, 0x1234n, true);
  for (const [name, offset] of Object.entries(RESULT_FIELDS)) dv.setUint32(offset, spec[name] ?? 0, true);
  dv.setUint32(88, spec.provenSafe ?? safe, true);
  dv.setUint32(92, spec.provenMines ?? mines, true);
  dv.setUint32(96, spec.hasEss ?? 0, true);
  dv.setUint32(100, spec.reserved ?? 0, true);
  dv.setFloat64(104, spec.elapsedMs ?? 0, true);
  dv.setFloat64(112, spec.ess ?? 0, true);
  return bytes;
}

const VALUE = 1;
const SAFE = 2;
const MINE = 4;

// The 4x1 example of scripts/check-wasm.mjs: clues {0: 1, 3: 0}, one mine.
function exact4x1() {
  return resultBuffer({
    status: 1, width: 4, height: 1, total: 1, revealed: 2, hidden_cells: 2, propagated_cells: 2,
    pair_reasoning_complete: 1, elapsedMs: 0.5,
    cells: [null, { value: 1, flags: VALUE | MINE }, { value: 0, flags: VALUE | SAFE }, null]
  });
}

function throwsCode(fn, code) {
  assert.throws(fn, (error) => {
    assert.ok(error instanceof EngineError, `expected EngineError, got ${error}`);
    assert.equal(error.code, code);
    assert.ok(error.message.length > 0);
    return true;
  });
}

// ------------------------------------------------------------- sizes, names

test('buffer sizes follow engine.h and stay multiples of 8', () => {
  assert.equal(viewSize(5, 5), 80);
  assert.equal(viewSize(80, 80), 48 + 6400);
  assert.equal(observationSize(9, 9), 32 + 88);
  assert.equal(resultSize(4, 1), 160);
  assert.equal(resultSize(80, 80), pad8(120 + 9 * 6400));
  assert.equal(cellCount(3001, 1), 3001);
  for (const [w, h] of [[0, 5], [5, 0], [6401, 1], [81, 80], [-1, 5], [1.5, 2], [true, 5], ['5', 5]]) {
    assert.equal(cellCount(w, h), 0, `${w}x${h}`);
    assert.equal(viewSize(w, h), 0);
    assert.equal(observationSize(w, h), 0);
    assert.equal(resultSize(w, h), 0);
  }
  for (let w = 1; w <= 80; w += 7) {
    for (let h = 1; h <= 80; h += 9) {
      assert.equal(viewSize(w, h) % 8 + observationSize(w, h) % 8 + resultSize(w, h) % 8, 0);
    }
  }
});

test('status names, kinds and messages', () => {
  const names = {
    0: 'ok', 1: 'invalid_width', 2: 'invalid_height', 3: 'invalid_mines', 4: 'invalid_action', 5: 'out_of_bounds',
    6: 'invalid_revision', 7: 'invalid_deductions', 8: 'invalid_observation', 9: 'invalid_limits',
    10: 'invalid_result', 11: 'invalid_buffer', 32: 'stale_revision', 33: 'game_over', 34: 'game_not_started',
    48: 'inconsistent_observation', 64: 'resource_exhausted', 65: 'internal_error'
  };
  for (const [status, name] of Object.entries(names)) assert.equal(statusName(Number(status)), name);
  assert.equal(statusName(12), 'unknown_status');
  assert.equal(statusName(-1), 'unknown_status');
  assert.equal(kindOfStatus(STATUS.INVALID_WIDTH), 'input');
  assert.equal(kindOfStatus(STATUS.STALE_REVISION), 'conflict');
  assert.equal(kindOfStatus(STATUS.GAME_NOT_STARTED), 'conflict');
  assert.equal(kindOfStatus(STATUS.INCONSISTENT), 'inconsistent');
  assert.equal(kindOfStatus(STATUS.RESOURCE_EXHAUSTED), 'resource');
  assert.equal(kindOfStatus(STATUS.INTERNAL), 'internal');
  for (const status of Object.keys(names).map(Number).filter((s) => s !== 0)) {
    const error = errorFromStatus(status);
    assert.equal(error.status, status);
    assert.equal(error.code, names[status]);
    assert.ok(error.message.length > 10, names[status]);
  }
  assert.equal(statusMessage('invalid_mines', { width: 9, height: 9 }),
    'mines must be a whole number from 1 to 72 for a 9x9 board (the first revealed cell and its neighbors are ' +
    'always safe).');
  assert.equal(statusMessage('invalid_width'), 'width must be a whole number from 5 to 80.');
  assert.equal(statusMessage('out_of_bounds', { row: 9, col: 0, width: 9, height: 9 }),
    'Cell (row 9, col 0) is off the board: row must be 0-8 and col must be 0-8.');
  assert.equal(statusMessage('stale_revision', { revision: 3, state: { revision: 5 } }),
    'The board changed since revision 3; it is now at revision 5. The latest state is included.');
  assert.equal(statusMessage('game_over', { state: { status: 'lost' } }),
    'This game is already over (you lost). Start a new game to keep playing.');
});

test('isU32 rejects everything that would truncate into an i32 parameter', () => {
  for (const bad of [true, false, '9', '', null, undefined, [9], {}, 9.5, NaN, Infinity, -Infinity, -1, 2 ** 32,
    1e20, 10n, Number.MAX_SAFE_INTEGER]) {
    assert.equal(isU32(bad), false, String(bad));
  }
  for (const good of [0, 1, 80, 2 ** 31, 2 ** 32 - 1, -0]) assert.equal(isU32(good), true, String(good));
});

// ---------------------------------------------------------------- the view

test('a ready view becomes a hidden GameState with null adjacency and mines', () => {
  const state = decodeView(viewBuffer({ width: 9, height: 8, mines: 10, generation: 3 }), { idPrefix: 'local-1-' });
  assert.deepEqual(Object.keys(state), ['id', 'generation', 'width', 'height', 'mines', 'status', 'revision',
    'flags', 'elapsed_seconds', 'cells']);
  assert.equal(state.id, 'local-1-3');
  assert.equal(state.status, 'ready');
  assert.equal(state.cells.length, 72);
  for (const cell of state.cells) {
    assert.deepEqual(cell, { revealed: false, flagged: false, adjacent: null, mine: null, exploded: false });
  }
  assert.deepEqual(JSON.parse(JSON.stringify(state)), state);
});

test('a playing view marshals clues, flags and milliseconds without leaking the layout', () => {
  const cells = new Array(25).fill(0x0F);
  cells[0] = 0x10; // revealed 0
  cells[1] = 0x12; // revealed 2
  cells[2] = 0x2F; // flagged
  const state = decodeView(viewBuffer({ status: 2, revision: 7, cells: cells, elapsedMs: 2500 }));
  assert.equal(state.status, 'playing');
  assert.equal(state.revision, 7);
  assert.equal(state.flags, 1);
  assert.equal(state.elapsed_seconds, 2.5);
  assert.deepEqual(state.cells[0], { revealed: true, flagged: false, adjacent: 0, mine: null, exploded: false });
  assert.deepEqual(state.cells[1], { revealed: true, flagged: false, adjacent: 2, mine: null, exploded: false });
  assert.deepEqual(state.cells[2], { revealed: false, flagged: true, adjacent: null, mine: null, exploded: false });
  assert.deepEqual(state.cells[3], { revealed: false, flagged: false, adjacent: null, mine: null, exploded: false });
});

test('a lost view discloses every mine, the explosion and wrong flags', () => {
  const cells = new Array(25).fill(0x0F);
  cells[0] = 0x10 | 0x40 | 0x80 | 0x0F; // exploded mine
  cells[1] = 0x40 | 0x20 | 0x0F; // flagged mine
  cells[2] = 0x20 | 0x0F; // wrong flag
  cells[3] = 0x40 | 0x0F; // hidden mine
  cells[4] = 0x11; // revealed 1
  const state = decodeView(viewBuffer({ status: 4, cells: cells, elapsedMs: 61000 }));
  assert.equal(state.status, 'lost');
  assert.deepEqual(state.cells[0], { revealed: true, flagged: false, adjacent: null, mine: true, exploded: true });
  assert.deepEqual(state.cells[1], { revealed: false, flagged: true, adjacent: null, mine: true, exploded: false });
  assert.deepEqual(state.cells[2], { revealed: false, flagged: true, adjacent: null, mine: false, exploded: false });
  assert.deepEqual(state.cells[3], { revealed: false, flagged: false, adjacent: null, mine: true, exploded: false });
  assert.deepEqual(state.cells[4], { revealed: true, flagged: false, adjacent: 1, mine: false, exploded: false });
  assert.equal(state.cells[5].mine, false);
});

test('malformed views are refused, never patched', () => {
  const playing = (cells, extra = {}) => viewBuffer(Object.assign({ status: 2, cells: cells }, extra));
  const hidden = () => new Array(25).fill(0x0F);
  const withCell = (index, value) => {
    const cells = hidden();
    cells[index] = value;
    return cells;
  };
  const cases = {
    'wrong magic': viewBuffer({ magic: 0x4D530002 }),
    'wrong version': viewBuffer({ version: 2 }),
    'short buffer': viewBuffer({}).slice(0, 40),
    'wrong length': viewBuffer({ length: 88 }),
    'generation 0': viewBuffer({ generation: 0 }),
    'revision above max': viewBuffer({ revision: 2 ** 31 }),
    'status 0': viewBuffer({ status: 0 }),
    'status 5': viewBuffer({ status: 5 }),
    'width 4': viewBuffer({ width: 4, cells: new Array(20).fill(0x0F) }),
    'too many mines': viewBuffer({ mines: 17 }),
    'negative elapsed': viewBuffer({ elapsedMs: -1 }),
    'NaN elapsed': viewBuffer({ elapsedMs: NaN }),
    'mine bit mid-game': playing(withCell(3, 0x4F)),
    'revealed and flagged': playing(withCell(3, 0x31)),
    'clue on a hidden cell': playing(withCell(3, 0x02)),
    'clue 9': playing(withCell(3, 0x19)),
    'revealed without a clue': playing(withCell(3, 0x1F)),
    'explosion mid-game': playing(withCell(3, 0x9F)),
    'flag count mismatch': playing(hidden(), { flags: 1 }),
    'revealed count mismatch': playing(hidden(), { revealed: 2 })
  };
  const padded = viewBuffer({});
  padded[padded.length - 1] = 1;
  cases['non-zero padding'] = padded;
  for (const [name, bytes] of Object.entries(cases)) {
    assert.throws(() => decodeView(bytes), (error) => error instanceof EngineError &&
      error.code === 'malformed_buffer' && error.kind === 'internal', name);
  }
  assert.throws(() => decodeView([1, 2, 3]), EngineError);
});

// -------------------------------------------------------------- the result

test('an exact result rebuilds the former odds payload', () => {
  const payload = decodeResult(exact4x1(), { gameId: 'local-1-1', generation: 1, revision: 4, gameStatus: 'playing' });
  assert.deepEqual(Object.keys(payload), ['game_id', 'generation', 'revision', 'status', 'probabilities',
    'proven_safe', 'proven_mines', 'message', 'meta']);
  assert.equal(payload.game_id, 'local-1-1');
  assert.equal(payload.generation, 1);
  assert.equal(payload.revision, 4);
  assert.equal(payload.status, 'exact');
  assert.deepEqual(payload.probabilities, [null, 1, 0, null]);
  assert.deepEqual(payload.proven_safe, [2]);
  assert.deepEqual(payload.proven_mines, [1]);
  assert.equal(payload.message, 'Exact probabilities for 2 unrevealed cells (0 next to clues in 0 group(s), ' +
    '0 unconstrained). Certain: 1 safe cell(s), 1 mine(s).');
  // Documented keys only: nodes, hidden_cells, remaining_mines,
  // propagated_cells and pair_reasoning_complete are not exposed, and the
  // effective sample size is null while its validity flag is clear, as the
  // former solver sent it.
  assert.deepEqual(payload.meta, {
    frontier_cells: 0, components: 0, unconstrained_cells: 0, samples: 0, elapsed_ms: 0.5, reason: null,
    exact_components: 0, sampled_components: 0, sample_attempts: 0, effective_sample_size: null
  });
  assert.deepEqual(JSON.parse(JSON.stringify(payload)), payload);
});

test('solver results always carry the diagnostics; ESS is null unless C marks it present', () => {
  const keys = ['frontier_cells', 'components', 'unconstrained_cells', 'samples', 'elapsed_ms', 'reason',
    'exact_components', 'sampled_components', 'sample_attempts', 'effective_sample_size'];
  const base = { status: 2, reason: 1, width: 1, height: 1, total: 1, hidden_cells: 1, components: 1,
    sampled_components: 1, samples: 3, sample_attempts: 340, cells: [{ value: 0.5, flags: VALUE }] };
  const present = decodeResult(resultBuffer(Object.assign({ hasEss: 1, ess: 57.25 }, base)));
  assert.deepEqual(Object.keys(present.meta), keys);
  assert.equal(present.meta.effective_sample_size, 57.25);
  assert.equal(present.meta.sample_attempts, 340);
  const zero = decodeResult(resultBuffer(Object.assign({ hasEss: 1, ess: 0 }, base)));
  assert.equal(zero.meta.effective_sample_size, 0, 'a present zero is forwarded, not nulled');
  const absent = decodeResult(resultBuffer(base));
  assert.deepEqual(Object.keys(absent.meta), keys);
  assert.equal(absent.meta.effective_sample_size, null);
  // Unavailable results forward the flag the same way (e.g. too few
  // effective samples: measured but below the minimum).
  const unavailable = { status: 3, reason: 5, width: 1, height: 1, total: 1, hidden_cells: 1, components: 1,
    sampled_components: 1, samples: 3, sample_attempts: 9, cells: [{ value: 0, flags: 0 }] };
  const measured = decodeResult(resultBuffer(Object.assign({ hasEss: 1, ess: 12.5 }, unavailable)));
  assert.deepEqual(Object.keys(measured.meta), keys);
  assert.equal(measured.meta.effective_sample_size, 12.5);
  const unmeasured = decodeResult(resultBuffer(unavailable));
  assert.deepEqual(Object.keys(unmeasured.meta), keys);
  assert.equal(unmeasured.meta.effective_sample_size, null);
  assert.deepEqual(JSON.parse(JSON.stringify(unmeasured.meta)), unmeasured.meta, 'null survives JSON');
  for (const name of ['nodes', 'hidden_cells', 'remaining_mines', 'propagated_cells', 'pair_reasoning_complete']) {
    assert.equal(name in present.meta, false, name);
  }
});

test('approximate results keep sampled endpoints as plain values, never proofs', () => {
  const bytes = resultBuffer({
    status: 2, reason: 1, width: 3, height: 1, total: 1, revealed: 0, hidden_cells: 3, frontier_cells: 3,
    components: 2, exact_components: 1, sampled_components: 1, samples: 900, sample_attempts: 2000,
    hasEss: 1, ess: 52.5, cells: [{ value: 0, flags: VALUE }, { value: 1, flags: VALUE }, { value: 0.25, flags: VALUE }]
  });
  const payload = decodeResult(bytes, {});
  assert.equal(payload.status, 'approximate');
  assert.deepEqual(payload.probabilities, [0, 1, 0.25]);
  assert.deepEqual(payload.proven_safe, []);
  assert.deepEqual(payload.proven_mines, []);
  assert.equal(payload.meta.reason, 'counting_budget_exceeded');
  assert.equal(payload.meta.effective_sample_size, 52.5);
  // Python's '{:.0f}': half to even.
  assert.equal(payload.message, 'Approximate probabilities: exact counting exceeded the budget for 1 of 2 group(s), ' +
    'estimated from 900 weighted samples (effective sample size 52). Only logically proven cells are marked certain.');
  const odd = decodeResult(resultBuffer({
    status: 2, reason: 1, width: 1, height: 1, total: 1, hidden_cells: 1, hasEss: 1, ess: 53.5,
    cells: [{ value: 0.5, flags: VALUE }]
  }));
  assert.match(odd.message, /effective sample size 54\)/);
});

test('unavailable results carry only proven values and the reason wording', () => {
  const reasons = {
    2: 'no sampling budget was left for the hard component(s)',
    3: 'sampling found no consistent layout for a component',
    4: 'no sampled layouts could be combined to match the mine total',
    5: 'too few effective samples for a reliable estimate',
    6: 'the time budget ran out before the result was complete',
    7: 'the memory budget ran out before the result was complete'
  };
  for (const [reason, text] of Object.entries(reasons)) {
    const payload = decodeResult(resultBuffer({
      status: 3, reason: Number(reason), width: 3, height: 1, total: 1, hidden_cells: 3,
      cells: [{ value: 0, flags: VALUE | SAFE }, null, null]
    }));
    assert.equal(payload.status, 'unavailable');
    assert.deepEqual(payload.probabilities, [0, null, null]);
    assert.deepEqual(payload.proven_safe, [0]);
    assert.equal(payload.message, 'Probabilities unavailable: ' + text + '. Certain: 1 safe cell(s), 0 mine(s).');
  }
});

test('placeholders carry the former server wording and only the required meta keys', () => {
  const ready = decodeResult(resultBuffer({ status: 4, reason: 8, width: 5, height: 5, total: 3 }));
  assert.equal(ready.status, 'not-started');
  assert.deepEqual(ready.meta, {
    frontier_cells: 0, components: 0, unconstrained_cells: 0, samples: 0, elapsed_ms: 0, reason: 'not_started'
  });
  assert.equal(ready.message, 'No mines have been placed yet. They are placed on your first reveal, and that ' +
    'cell and all of its neighbors are guaranteed to be safe.');
  assert.ok(ready.probabilities.every((p) => p === null));
  const over = decodeResult(resultBuffer({ status: 5, reason: 9, width: 5, height: 5, total: 3 }), { gameStatus: 'won' });
  assert.equal(over.status, 'finished');
  assert.deepEqual(over.meta, {
    frontier_cells: 0, components: 0, unconstrained_cells: 0, samples: 0, elapsed_ms: 0, reason: 'game_over'
  });
  assert.equal(over.message, 'This game is over (you won), so there are no odds to show.');
});

test('malformed results are refused', () => {
  const base = { status: 1, width: 2, height: 1, total: 1, hidden_cells: 2 };
  const make = (extra) => resultBuffer(Object.assign({}, base, extra));
  const cells = (a, b) => ({ cells: [a, b] });
  const ok = cells({ value: 0.5, flags: VALUE }, { value: 0.5, flags: VALUE });
  decodeResult(make(ok));
  const cases = {
    'status 0': make(Object.assign({ status: 0 }, ok)),
    'status 6': make(Object.assign({ status: 6 }, ok)),
    'reason 10': make(Object.assign({ reason: 10 }, ok)),
    'unknown flag bit': make(cells({ value: 0.5, flags: VALUE | 8 }, { value: 0.5, flags: VALUE })),
    'value without flag': make(cells({ value: 0.5, flags: 0 }, { value: 0.5, flags: VALUE })),
    'tiny value without flag': make(cells({ value: Number.MIN_VALUE, flags: 0 }, { value: 0.5, flags: VALUE })),
    'proof without value': make(cells({ value: 0, flags: SAFE }, { value: 0.5, flags: VALUE })),
    'both proofs': make(cells({ value: 0, flags: VALUE | SAFE | MINE }, { value: 0.5, flags: VALUE })),
    'proven safe at 0.5': make(cells({ value: 0.5, flags: VALUE | SAFE }, { value: 0.5, flags: VALUE })),
    'proven mine at 0.99': make(cells({ value: 0.99, flags: VALUE | MINE }, { value: 0.5, flags: VALUE })),
    'value above one': make(cells({ value: 1.5, flags: VALUE }, { value: 0.5, flags: VALUE })),
    'NaN value': make(cells({ value: NaN, flags: VALUE }, { value: 0.5, flags: VALUE })),
    'proof count mismatch': make(Object.assign({ provenSafe: 1 }, ok)),
    'reserved word': make(Object.assign({ reserved: 1 }, ok)),
    'ESS flag 2': make(Object.assign({ hasEss: 2 }, ok)),
    'ESS without flag': make(Object.assign({ ess: 3 }, ok)),
    'NaN ESS': make(Object.assign({ hasEss: 1, ess: NaN }, ok)),
    'negative elapsed': make(Object.assign({ elapsedMs: -1 }, ok)),
    'wrong length': make(Object.assign({ length: 160 }, ok))
  };
  const padded = make(ok);
  padded[padded.length - 1] = 1;
  cases['non-zero padding'] = padded;
  for (const [name, bytes] of Object.entries(cases)) {
    assert.throws(() => decodeResult(bytes), (error) => error instanceof EngineError &&
      error.code === 'malformed_buffer', name);
  }
});

test('zero is compared as C compares it: -0.0 is a valid 0.0', () => {
  // ms_result_validate checks p != 0.0, which -0.0 passes, for valueless and
  // revealed cells and for proven-safe cells; the decoder must not be stricter.
  const payload = decodeResult(resultBuffer({
    status: 3, reason: 6, width: 3, height: 1, total: 1, revealed: 1, hidden_cells: 2,
    cells: [{ value: -0, flags: 0 }, { value: -0, flags: 0 }, { value: -0, flags: VALUE | SAFE }]
  }));
  assert.deepEqual(payload.probabilities, [null, null, 0]);
  assert.deepEqual(payload.proven_safe, [2]);
  assert.ok(Object.is(payload.probabilities[2], 0), 'rebuilt objects use +0');
  const sampled = decodeResult(resultBuffer({
    status: 2, reason: 1, width: 1, height: 1, total: 1, hidden_cells: 1, hasEss: 1, ess: -0, elapsedMs: -0,
    cells: [{ value: -0, flags: VALUE }]
  }));
  assert.ok(Object.is(sampled.probabilities[0], 0));
  assert.ok(Object.is(sampled.meta.effective_sample_size, 0));
  assert.ok(Object.is(sampled.meta.elapsed_ms, 0));
  assert.deepEqual(sampled.proven_safe, [], 'a sampled zero is still no proof');
  assert.ok(Object.is(decodeView(viewBuffer({ elapsedMs: -0 })).elapsed_seconds, 0));
});

test('probabilityMessage omits a missing ESS instead of inventing one', () => {
  for (const meta of [{ sampled_components: 1, components: 1, samples: 3, effective_sample_size: null },
    { sampled_components: 1, components: 1, samples: 3 }]) {
    const text = probabilityMessage({ status: 'approximate', meta: meta, proven_safe: [], proven_mines: [],
      hiddenCells: 4 });
    assert.equal(text, 'Approximate probabilities: exact counting exceeded the budget for 1 of 1 group(s), ' +
      'estimated from 3 weighted samples. Only logically proven cells are marked certain.');
  }
});

// ------------------------------------------------- observations and limits

test('observation headers are checked before sizing a result', () => {
  const header = readObservationHeader(obsBuffer(4, 1, 1, { 0: 1, 3: 0 }));
  assert.deepEqual(header, { magic: 0x4D530002, version: 1, width: 4, height: 1, totalMines: 1, revealed: 2 });
  assert.equal(readObservationHeader(obsBuffer(3001, 1, 5)).width, 3001);
  for (const bytes of [obsBuffer(4, 1, 1, {}, { magic: 0x4D530001 }), obsBuffer(4, 1, 1, {}, { version: 0 }),
    obsBuffer(4, 1, 1, {}, { length: 48 }), obsBuffer(4, 1, 1).slice(0, 16), obsBuffer(6401, 1, 1)]) {
    assert.throws(() => readObservationHeader(bytes), EngineError);
  }
});

test('limits blocks decode at the documented offsets, i64 fields as BigInt', () => {
  const bytes = new Uint8Array(56);
  const dv = new DataView(bytes.buffer);
  [0x4D530003, 1, 100000, 2000, 1500000, 0].forEach((v, i) => dv.setUint32(4 * i, v, true));
  dv.setFloat64(24, 1500, true);
  dv.setFloat64(32, 50, true);
  dv.setBigUint64(40, 256n << 20n, true);
  assert.deepEqual(decodeLimits(bytes), {
    nodeBudget: 100000, sampleBudget: 2000, maxStoredEntries: 1500000, flags: 0, timeBudgetMs: 1500,
    minEffectiveSamples: 50, memoryBudgetBytes: 268435456n, seed: 0n
  });
  assert.throws(() => decodeLimits(bytes.slice(0, 48)), EngineError);
});

// --------------------------------------------------------- worker protocol

function solveRequest(extra = {}) {
  return Object.assign({
    type: 'solve', id: 1, generation: 1, revision: 0, observation: obsBuffer(4, 1, 1, { 0: 1 }).buffer
  }, extra);
}

test('solve requests accept only public fields and routing numbers', () => {
  const request = checkSolveRequest(solveRequest());
  assert.equal(request.id, 1);
  assert.ok(request.observation instanceof Uint8Array);
  assert.equal(request.header.width, 4);
  const bad = {
    'not an object': 'solve',
    'array': [solveRequest()],
    'null': null,
    'missing observation': (() => {
      const r = solveRequest();
      delete r.observation;
      return r;
    })(),
    'flags field': solveRequest({ flags: [1] }),
    'mines field': solveRequest({ mines: [2] }),
    'layout field': solveRequest({ layout: [2] }),
    'board field': solveRequest({ board: {} }),
    'seed field': solveRequest({ seed: 1 }),
    'memory field': solveRequest({ memory: new ArrayBuffer(8) }),
    'wrong type': solveRequest({ type: 'init' }),
    'id 0': solveRequest({ id: 0 }),
    'id 1.5': solveRequest({ id: 1.5 }),
    'id string': solveRequest({ id: '1' }),
    'id boolean': solveRequest({ id: true }),
    'generation 0': solveRequest({ generation: 0 }),
    'generation 2^31': solveRequest({ generation: 2 ** 31 }),
    'revision -1': solveRequest({ revision: -1 }),
    'revision string': solveRequest({ revision: '0' }),
    'revision NaN': solveRequest({ revision: NaN }),
    'typed array instead of a copy': solveRequest({ observation: obsBuffer(4, 1, 1) }),
    'array of clues': solveRequest({ observation: [1, 255, 255, 255] }),
    'shared memory': solveRequest({ observation: new SharedArrayBuffer(40) }),
    'short observation': solveRequest({ observation: new ArrayBuffer(24) }),
    'odd length': solveRequest({ observation: new ArrayBuffer(41) }),
    'wrong magic': solveRequest({ observation: obsBuffer(4, 1, 1, {}, { magic: 1 }).buffer }),
    'length mismatch': solveRequest({ observation: obsBuffer(4, 1, 1, {}, { length: 48 }).buffer }),
    'oversized': solveRequest({ observation: new ArrayBuffer(8 * 1024) })
  };
  for (const [name, message] of Object.entries(bad)) {
    assert.throws(() => checkSolveRequest(message), (error) => error instanceof EngineError &&
      error.code === 'invalid_message' && error.kind === 'protocol', name);
  }
});

test('worker messages are validated before any use', () => {
  const result = new ArrayBuffer(160);
  const error = { code: 'trap', status: null, message: 'boom', fatal: true };
  const good = [
    { type: 'ready' },
    { type: 'init-failed', error: error },
    { type: 'result', id: 1, generation: 1, revision: 0, result: result },
    { type: 'error', id: 1, generation: 1, revision: 0, error: { code: 'inconsistent_observation', status: 48,
      message: 'no layout', fatal: false } }
  ];
  for (const message of good) assert.equal(checkWorkerMessage(message), message);
  const bad = [
    null, 'ready', [], {}, { type: 'ready', extra: 1 }, { type: 'unknown' },
    { type: 'result', id: '1', generation: 1, revision: 0, result: result },
    { type: 'result', id: 1, generation: true, revision: 0, result: result },
    { type: 'result', id: 1, generation: 1, revision: 0.5, result: result },
    { type: 'result', id: 1, generation: 1, revision: 0, result: new Uint8Array(160) },
    { type: 'result', id: 1, generation: 1, revision: 0, result: new ArrayBuffer(64) },
    { type: 'result', id: 1, generation: 1, revision: 0, result: result, probabilities: [] },
    { type: 'error', id: 1, generation: 1, revision: 0, error: { code: 'x', status: null, message: 5, fatal: true } },
    { type: 'error', id: 1, generation: 1, revision: 0, error: { code: 'BAD CODE', status: null, message: 'm', fatal: true } },
    { type: 'error', id: 1, generation: 1, revision: 0, error: { code: 'x', status: 0, message: 'm', fatal: true } },
    { type: 'error', id: 1, generation: 1, revision: 0, error: { code: 'x', status: 12, message: 'm', fatal: true } },
    { type: 'error', id: 1, generation: 1, revision: 0, error: { code: 'x', status: null, message: '', fatal: true } },
    { type: 'error', id: 1, generation: 1, revision: 0, error: { code: 'x', status: null, message: 'm', fatal: 1 } },
    { type: 'init-failed', error: { code: 'x', status: null, message: 'm' } }
  ];
  for (const message of bad) {
    assert.throws(() => checkWorkerMessage(message), (e) => e instanceof EngineError && e.kind === 'protocol',
      JSON.stringify(message));
  }
});

test('worker error payloads round-trip; traps are always fatal', () => {
  const status = errorPayload(errorFromStatus(STATUS.INCONSISTENT), false);
  assert.deepEqual(status, {
    code: 'inconsistent_observation', status: 48,
    message: 'The revealed numbers and the mine total admit no mine layout.', fatal: false
  });
  const again = errorFromPayload(status, 'worker');
  assert.equal(again.code, 'inconsistent_observation');
  assert.equal(again.kind, 'worker');
  const trap = errorPayload(new WebAssembly.RuntimeError('unreachable'), false);
  assert.equal(trap.code, 'trap');
  assert.equal(trap.fatal, true);
  assert.match(trap.message, /RuntimeError: unreachable/);
  assert.doesNotThrow(() => checkWorkerMessage({ type: 'error', id: 1, generation: 1, revision: 0, error: trap }));
  assert.equal(describeThrown(new ProcExit(3)), 'the engine exited with status 3');
});
