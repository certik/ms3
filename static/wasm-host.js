/*
 * wasm-host.js - WebAssembly host for the Minesweeper C engine.
 *
 * Shared by the main thread (the game instance behind engine-client.js) and
 * the solver worker (probability-worker.js). Both load the same compiled
 * module into independent, single-threaded instances.
 *
 * This file only marshals data. The C engine owns every game rule and every
 * probability computation (c/engine.h, c/wasm_api.h); JavaScript validates
 * argument types before they become integer WASM parameters, copies typed
 * buffers in and out of linear memory, and rebuilds the plain objects the
 * page already understands (the former server's GameState and odds shapes).
 *
 * Memory rules: every export that may allocate can grow linear memory, which
 * detaches existing ArrayBuffer views. Views are therefore created right
 * before each access and results are copied into JS-owned arrays before the
 * WASM buffers are freed. An exception escaping an export (a trap, corec's
 * ProcExit, a failing host import) leaves the instance unusable: it is
 * poisoned and every later call fails with an explicit `trap` error.
 */

import { makeWasi, ProcExit } from './vendor/corec/wasi.js';

// ------------------------------------------------------------ ABI (engine.h)

export const ABI_VERSION = 1;

export const MAGIC = Object.freeze({
  VIEW: 0x4D530001,
  OBSERVATION: 0x4D530002,
  LIMITS: 0x4D530003,
  RESULT: 0x4D530004,
  PLAN_LIMITS: 0x4D530005,
  PLAN_RESULT: 0x4D530006
});

// The move advisor's own buffers (c/planner.h) carry MS_PLANNER_VERSION
// instead of the ABI version; the ABI itself is unchanged (additive).
export const PLANNER_VERSION = 1;

export const STATUS = Object.freeze({
  OK: 0,
  INVALID_WIDTH: 1,
  INVALID_HEIGHT: 2,
  INVALID_MINES: 3,
  INVALID_ACTION: 4,
  OUT_OF_BOUNDS: 5,
  INVALID_REVISION: 6,
  INVALID_DEDUCTIONS: 7,
  INVALID_OBSERVATION: 8,
  INVALID_LIMITS: 9,
  INVALID_RESULT: 10,
  INVALID_BUFFER: 11,
  STALE_REVISION: 32,
  GAME_OVER: 33,
  GAME_NOT_STARTED: 34,
  INCONSISTENT: 48,
  RESOURCE_EXHAUSTED: 64,
  INTERNAL: 65
});

const STATUS_NAMES = new Map([
  [0, 'ok'],
  [1, 'invalid_width'],
  [2, 'invalid_height'],
  [3, 'invalid_mines'],
  [4, 'invalid_action'],
  [5, 'out_of_bounds'],
  [6, 'invalid_revision'],
  [7, 'invalid_deductions'],
  [8, 'invalid_observation'],
  [9, 'invalid_limits'],
  [10, 'invalid_result'],
  [11, 'invalid_buffer'],
  [32, 'stale_revision'],
  [33, 'game_over'],
  [34, 'game_not_started'],
  [48, 'inconsistent_observation'],
  [64, 'resource_exhausted'],
  [65, 'internal_error']
]);

export const GAME_MIN_SIDE = 5;
export const GAME_MAX_SIDE = 80;
export const GAME_SAFE_START_CELLS = 9;
export const SOLVER_MAX_CELLS = 6400;
export const GENERATION_MAX = 0x7FFFFFFF;
export const REVISION_MAX = 0x7FFFFFFF;
export const CLUE_HIDDEN = 0xFF;

export const ACTIONS = Object.freeze({ reveal: 1, flag: 2, chord: 3 });
const GAME_STATUS_NAMES = [null, 'ready', 'playing', 'won', 'lost'];
const PROB_STATUS_NAMES = [null, 'exact', 'approximate', 'unavailable', 'not-started', 'finished'];
const REASON_NAMES = [
  null,
  'counting_budget_exceeded',
  'sampling_budget_exhausted',
  'no_consistent_samples',
  'no_globally_compatible_samples',
  'insufficient_effective_samples',
  'time_budget_exhausted',
  'memory_budget_exhausted',
  'not_started',
  'game_over'
];
// minesweeper/probability.py _REASON_TEXT, plus the C-only memory reason.
const REASON_TEXT = {
  counting_budget_exceeded: 'exact counting exceeded the budget',
  sampling_budget_exhausted: 'no sampling budget was left for the hard component(s)',
  no_consistent_samples: 'sampling found no consistent layout for a component',
  no_globally_compatible_samples: 'no sampled layouts could be combined to match the mine total',
  insufficient_effective_samples: 'too few effective samples for a reliable estimate',
  time_budget_exhausted: 'the time budget ran out before the result was complete',
  memory_budget_exhausted: 'the memory budget ran out before the result was complete'
};

export const CELL = Object.freeze({
  ADJACENT_MASK: 0x0F,
  NO_ADJACENT: 0x0F,
  REVEALED: 0x10,
  FLAGGED: 0x20,
  MINE: 0x40,
  EXPLODED: 0x80
});

export const PCELL = Object.freeze({ VALUE: 0x01, PROVEN_SAFE: 0x02, PROVEN_MINE: 0x04, KNOWN_BITS: 0x07 });

export const VIEW_HEADER_SIZE = 48;
export const OBS_HEADER_SIZE = 32;
export const RESULT_HEADER_SIZE = 120;
export const LIMITS_SIZE = 56; // MS_WASM_LIMITS_BYTES
export const PLAN_LIMITS_SIZE = 64; // MS_WASM_PLAN_LIMITS_BYTES
export const PLAN_RESULT_SIZE = 112; // MS_WASM_PLAN_RESULT_BYTES
export const PLAN_NO_CELL = 0xFFFFFFFF; // MS_PLAN_NO_CELL

// ms_plan_status and ms_plan_reason (planner.h), by number.
export const PLAN_STATUS_NAMES = Object.freeze(['none', 'exact', 'estimated', 'unavailable']);
export const PLAN_REASON_NAMES = Object.freeze([null, 'certain_moves', 'finished', 'no_samples', 'budget',
  'insufficient_rollouts', 'posterior_unavailable', 'not_started']);

// ms_live_bytes pools (wasm_api.h).
export const POOL = Object.freeze({ BUFFERS: 0, ENGINE: 1, SOLVER: 2 });

// ------------------------------------------------------------------ errors

/*
 * Every failure crossing this module is an EngineError:
 *   code    stable snake_case code (the C status name, or a host code such
 *           as 'invalid_coordinates', 'engine_load_failed', 'trap')
 *   status  the C ms_status number, or null when JavaScript rejected first
 *   kind    'input' | 'conflict' | 'inconsistent' | 'resource' | 'internal'
 *           | 'trap' | 'load' | 'worker' | 'protocol' | 'timeout' | 'aborted'
 *   state   the latest GameState for conflicts, otherwise null
 */
export class EngineError extends Error {
  constructor(code, message, options = {}) {
    super(message, options.cause === undefined ? undefined : { cause: options.cause });
    this.name = 'EngineError';
    this.code = code;
    this.status = options.status === undefined ? null : options.status;
    this.kind = options.kind || kindOfStatus(this.status);
    this.state = options.state || null;
  }
}

export function statusName(status) {
  return STATUS_NAMES.get(status) || 'unknown_status';
}

export function kindOfStatus(status) {
  if (status === null || status === undefined) return 'internal';
  if (status >= 1 && status < 32) return 'input';
  if (status >= 32 && status < 48) return 'conflict';
  if (status === STATUS.INCONSISTENT) return 'inconsistent';
  if (status === STATUS.RESOURCE_EXHAUSTED) return 'resource';
  return 'internal';
}

// Player-facing wording, following the former server's messages.
export function statusMessage(code, context = {}) {
  const c = context;
  switch (code) {
    case 'invalid_width':
    case 'invalid_height':
      return (code === 'invalid_width' ? 'width' : 'height') + ' must be a whole number from ' +
        GAME_MIN_SIDE + ' to ' + GAME_MAX_SIDE + '.';
    case 'invalid_mines':
      if (isValidSide(c.width) && isValidSide(c.height)) {
        return 'mines must be a whole number from 1 to ' + maxMines(c.width, c.height) + ' for a ' +
          c.width + 'x' + c.height + ' board (the first revealed cell and its neighbors are always safe).';
      }
      return 'mines must be a whole number from 1 to the board area minus ' + GAME_SAFE_START_CELLS + '.';
    case 'invalid_action':
      return 'action must be one of: reveal, flag, chord.';
    case 'invalid_coordinates':
      return 'row and col must be whole numbers.';
    case 'out_of_bounds':
      if (Number.isInteger(c.row) && Number.isInteger(c.col) && isValidSide(c.width) && isValidSide(c.height)) {
        return 'Cell (row ' + c.row + ', col ' + c.col + ') is off the board: row must be 0-' + (c.height - 1) +
          ' and col must be 0-' + (c.width - 1) + '.';
      }
      return 'The cell is off the board.';
    case 'invalid_revision':
      return 'revision must be a non-negative whole number.';
    case 'invalid_generation':
      return 'generation must be a positive whole number.';
    case 'stale_revision':
      if (c.state && Number.isInteger(c.revision)) {
        return 'The board changed since revision ' + c.revision + '; it is now at revision ' +
          c.state.revision + '. The latest state is included.';
      }
      return 'The board changed since this request was made. The latest state is included.';
    case 'game_over':
      if (c.state && (c.state.status === 'won' || c.state.status === 'lost')) {
        return 'This game is already over (you ' + c.state.status + '). Start a new game to keep playing.';
      }
      return 'This game is already over. Start a new game to keep playing.';
    case 'game_not_started':
      return 'Reveal your first cell before using autosolve.';
    case 'invalid_deductions':
      return 'Deductions must describe distinct hidden cells.';
    case 'inconsistent_observation':
      return 'The revealed numbers and the mine total admit no mine layout.';
    case 'invalid_result':
      return 'The odds calculator returned a result that does not fit this board, so it was not used.';
    case 'resource_exhausted':
      return 'The engine ran out of memory for this request.';
    case 'invalid_buffer':
    case 'invalid_observation':
    case 'invalid_limits':
      return 'The engine rejected malformed data from this page (' + code + ').';
    default:
      return 'The engine reported an internal error (' + code + ').';
  }
}

export function errorFromStatus(status, context = {}) {
  const code = statusName(status);
  return new EngineError(code, statusMessage(code, context), {
    status: status,
    kind: kindOfStatus(status),
    state: context.state || null
  });
}

// --------------------------------------------------------- argument checks

// A value that can become a WASM i32 parameter holding a uint32: rejects
// booleans, strings, null, arrays, BigInt, fractions, NaN, infinities and
// numbers outside 0..2^32-1 before the call (C re-checks every domain).
export function isU32(value) {
  return typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 0xFFFFFFFF;
}

export function requireU32(value, code, context) {
  if (!isU32(value)) throw new EngineError(code, statusMessage(code, context), { kind: 'input' });
  return value >>> 0;
}

function isValidSide(value) {
  return Number.isInteger(value) && value >= GAME_MIN_SIDE && value <= GAME_MAX_SIDE;
}

export function maxMines(width, height) {
  return width * height - GAME_SAFE_START_CELLS;
}

// ------------------------------------------------------------ buffer sizes

export function pad8(size) {
  return Math.ceil(size / 8) * 8;
}

// ms_cell_count: 0 unless 1 <= width, height and width * height <= 6400.
export function cellCount(width, height) {
  if (!isU32(width) || !isU32(height) || width === 0 || height === 0) return 0;
  if (width > SOLVER_MAX_CELLS || height > Math.floor(SOLVER_MAX_CELLS / width)) return 0;
  return width * height;
}

export function viewSize(width, height) {
  const cells = cellCount(width, height);
  return cells ? pad8(VIEW_HEADER_SIZE + cells) : 0;
}

export function observationSize(width, height) {
  const cells = cellCount(width, height);
  return cells ? pad8(OBS_HEADER_SIZE + cells) : 0;
}

export function resultSize(width, height) {
  const cells = cellCount(width, height);
  return cells ? pad8(RESULT_HEADER_SIZE + cells * 9) : 0;
}

export const MAX_OBSERVATION_SIZE = pad8(OBS_HEADER_SIZE + SOLVER_MAX_CELLS);
export const MAX_RESULT_SIZE = pad8(RESULT_HEADER_SIZE + SOLVER_MAX_CELLS * 9);

// ---------------------------------------------------------------- decoding

function malformed(what, detail) {
  return new EngineError('malformed_buffer', 'The engine produced a malformed ' + what + ' (' + detail + ').', {
    kind: 'internal'
  });
}

function asBytes(buffer, what) {
  if (buffer instanceof Uint8Array) return buffer;
  if (buffer instanceof ArrayBuffer) return new Uint8Array(buffer);
  throw malformed(what, 'not a byte buffer');
}

function dataView(bytes) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
}

// -0.0 passes every C check that 0.0 passes (p != 0.0); rebuilt objects use
// a single representation of zero.
function plusZero(value) {
  return value === 0 ? 0 : value;
}

function zeroFrom(bytes, start) {
  for (let i = start; i < bytes.length; i++) {
    if (bytes[i] !== 0) return false;
  }
  return true;
}

/*
 * Rebuilds the former server's GameState from an ms_view buffer (engine.h,
 * "Game status, actions and the public view"):
 *   { id, generation, width, height, mines, status, revision, flags,
 *     elapsed_seconds, cells: [{ revealed, flagged, adjacent, mine, exploded }] }
 * `adjacent` is null unless the cell is a revealed safe cell; `mine` is null
 * until the game is won or lost. Structural problems throw.
 */
export function decodeView(buffer, options = {}) {
  const bytes = asBytes(buffer, 'game view');
  if (bytes.length < VIEW_HEADER_SIZE) throw malformed('game view', 'short header');
  const dv = dataView(bytes);
  const u32 = (offset) => dv.getUint32(offset, true);
  if (u32(0) !== MAGIC.VIEW) throw malformed('game view', 'magic');
  if (u32(4) !== ABI_VERSION) throw malformed('game view', 'ABI version');
  const generation = u32(8);
  const revision = u32(12);
  const statusCode = u32(16);
  const width = u32(20);
  const height = u32(24);
  const mines = u32(28);
  const flags = u32(32);
  const revealedCount = u32(36);
  const elapsedMs = dv.getFloat64(40, true);
  if (!isValidSide(width) || !isValidSide(height)) throw malformed('game view', 'dimensions');
  const total = width * height;
  if (bytes.length !== viewSize(width, height)) throw malformed('game view', 'length');
  if (!zeroFrom(bytes, VIEW_HEADER_SIZE + total)) throw malformed('game view', 'padding');
  if (generation < 1 || generation > GENERATION_MAX) throw malformed('game view', 'generation');
  if (revision > REVISION_MAX) throw malformed('game view', 'revision');
  const status = GAME_STATUS_NAMES[statusCode];
  if (!status) throw malformed('game view', 'status');
  if (mines < 1 || mines > maxMines(width, height)) throw malformed('game view', 'mine count');
  if (!Number.isFinite(elapsedMs) || elapsedMs < 0) throw malformed('game view', 'elapsed time');
  const terminal = status === 'won' || status === 'lost';
  const cells = new Array(total);
  let flagged = 0;
  let revealed = 0;
  for (let i = 0; i < total; i++) {
    const b = bytes[VIEW_HEADER_SIZE + i];
    const isRevealed = (b & CELL.REVEALED) !== 0;
    const isFlagged = (b & CELL.FLAGGED) !== 0;
    const isMine = (b & CELL.MINE) !== 0;
    const exploded = (b & CELL.EXPLODED) !== 0;
    const nibble = b & CELL.ADJACENT_MASK;
    // Revealed safe cells carry a clue 0..8; the only revealed mines are the
    // exploded ones of a lost game; hidden cells carry nothing but a flag.
    let consistent;
    if (!isRevealed) consistent = nibble === CELL.NO_ADJACENT && !exploded;
    else if (exploded) consistent = status === 'lost' && isMine && !isFlagged && nibble === CELL.NO_ADJACENT;
    else consistent = !isMine && !isFlagged && nibble <= 8;
    if (!consistent || (isMine && !terminal)) throw malformed('game view', 'cell ' + i + ' bits');
    if (isFlagged) flagged++;
    if (isRevealed) revealed++;
    cells[i] = {
      revealed: isRevealed,
      flagged: isFlagged,
      adjacent: nibble === CELL.NO_ADJACENT ? null : nibble,
      mine: terminal ? isMine : null,
      exploded: exploded
    };
  }
  if (flagged !== flags) throw malformed('game view', 'flag count');
  if (revealed !== revealedCount) throw malformed('game view', 'revealed count');
  const prefix = typeof options.idPrefix === 'string' ? options.idPrefix : 'game-';
  return {
    id: prefix + generation,
    generation: generation,
    width: width,
    height: height,
    mines: mines,
    status: status,
    revision: revision,
    flags: flags,
    elapsed_seconds: plusZero(elapsedMs) / 1000,
    cells: cells
  };
}

// Header of an ms_obs buffer, with the structural checks needed to size the
// matching result buffer. C validates clue values and consistency.
export function readObservationHeader(buffer) {
  const bytes = asBytes(buffer, 'observation');
  if (bytes.length < OBS_HEADER_SIZE) throw malformed('observation', 'short header');
  const dv = dataView(bytes);
  const header = {
    magic: dv.getUint32(0, true),
    version: dv.getUint32(4, true),
    width: dv.getUint32(8, true),
    height: dv.getUint32(12, true),
    totalMines: dv.getUint32(16, true),
    revealed: dv.getUint32(20, true)
  };
  if (header.magic !== MAGIC.OBSERVATION) throw malformed('observation', 'magic');
  if (header.version !== ABI_VERSION) throw malformed('observation', 'ABI version');
  if (!cellCount(header.width, header.height)) throw malformed('observation', 'dimensions');
  if (bytes.length !== observationSize(header.width, header.height)) throw malformed('observation', 'length');
  return header;
}

function formatInt(value) {
  return String(value);
}

// Python's format(x, '.0f'): round half to even on the exact binary value.
function formatFixed0(value) {
  const floor = Math.floor(value);
  const diff = value - floor;
  let rounded;
  if (diff > 0.5) rounded = floor + 1;
  else if (diff < 0.5) rounded = floor;
  else rounded = floor % 2 === 0 ? floor : floor + 1;
  return String(rounded);
}

/*
 * The odds message the former solver produced (probability.py _message and
 * server.py _placeholder_response), derived from status, reason and counts.
 * `gameStatus` ('won' | 'lost') words the finished placeholder.
 */
export function probabilityMessage(result, gameStatus) {
  const m = result.meta;
  let text;
  switch (result.status) {
    case 'exact':
      text = 'Exact probabilities for ' + formatInt(result.hiddenCells) + ' unrevealed cells (' +
        formatInt(m.frontier_cells) + ' next to clues in ' + formatInt(m.components) + ' group(s), ' +
        formatInt(m.unconstrained_cells) + ' unconstrained).';
      break;
    case 'approximate':
      text = 'Approximate probabilities: exact counting exceeded the budget for ' +
        formatInt(m.sampled_components) + ' of ' + formatInt(m.components) + ' group(s), estimated from ' +
        formatInt(m.samples) + ' weighted samples' +
        (typeof m.effective_sample_size === 'number'
          ? ' (effective sample size ' + formatFixed0(m.effective_sample_size) + ')' : '') +
        '. Only logically proven cells are marked certain.';
      break;
    case 'unavailable':
      text = 'Probabilities unavailable: ' + (REASON_TEXT[m.reason] || String(m.reason)) + '.';
      break;
    case 'not-started':
      return 'No mines have been placed yet. They are placed on your first reveal, and that cell and all of ' +
        'its neighbors are guaranteed to be safe.';
    default:
      return gameStatus === 'won' || gameStatus === 'lost'
        ? 'This game is over (you ' + gameStatus + '), so there are no odds to show.'
        : 'This game is over, so there are no odds to show.';
  }
  const safe = result.proven_safe.length;
  const mines = result.proven_mines.length;
  if (safe || mines) text += ' Certain: ' + formatInt(safe) + ' safe cell(s), ' + formatInt(mines) + ' mine(s).';
  return text;
}

/*
 * Rebuilds the former server's odds payload from an ms_result buffer
 * (engine.h, "Probability result"), with the game's generation beside its id:
 *   { game_id, generation, revision, status, probabilities, proven_safe,
 *     proven_mines, message, meta }
 * meta has the documented keys only: frontier_cells, components,
 * unconstrained_cells, samples, elapsed_ms and reason, plus, for solver
 * results (exact, approximate, unavailable), exact_components,
 * sampled_components, sample_attempts and effective_sample_size, which is
 * null unless the result's validity flag (has_effective_sample_size) is set,
 * as the former solver sent it. Placeholders (not-started, finished) carry
 * only the required keys, as the former server sent them. These checks only
 * refuse to rebuild an object from a malformed buffer and are never stricter
 * than C's ms_result_validate, which stays authoritative (observation hash,
 * completeness, exact-endpoint rules).
 */
export function decodeResult(buffer, routing = {}) {
  const bytes = asBytes(buffer, 'probability result');
  if (bytes.length < RESULT_HEADER_SIZE) throw malformed('probability result', 'short header');
  const dv = dataView(bytes);
  const u32 = (offset) => dv.getUint32(offset, true);
  if (u32(0) !== MAGIC.RESULT) throw malformed('probability result', 'magic');
  if (u32(4) !== ABI_VERSION) throw malformed('probability result', 'ABI version');
  const status = PROB_STATUS_NAMES[u32(8)];
  if (!status) throw malformed('probability result', 'status');
  const reasonCode = u32(12);
  if (reasonCode >= REASON_NAMES.length) throw malformed('probability result', 'reason');
  const width = u32(16);
  const height = u32(20);
  const cells = cellCount(width, height);
  if (!cells) throw malformed('probability result', 'dimensions');
  if (bytes.length !== resultSize(width, height)) throw malformed('probability result', 'length');
  const flagsOffset = RESULT_HEADER_SIZE + cells * 8;
  if (!zeroFrom(bytes, flagsOffset + cells) || u32(100) !== 0) throw malformed('probability result', 'padding');
  const hasEss = u32(96);
  if (hasEss > 1) throw malformed('probability result', 'ESS flag');
  const elapsedMs = dv.getFloat64(104, true);
  const ess = dv.getFloat64(112, true);
  if (!Number.isFinite(elapsedMs) || elapsedMs < 0) throw malformed('probability result', 'elapsed time');
  if (hasEss ? !(Number.isFinite(ess) && ess >= 0) : ess !== 0) throw malformed('probability result', 'ESS');

  const probabilities = new Array(cells);
  const provenSafe = [];
  const provenMines = [];
  for (let i = 0; i < cells; i++) {
    const flag = bytes[flagsOffset + i];
    const value = dv.getFloat64(RESULT_HEADER_SIZE + i * 8, true);
    if (flag & ~PCELL.KNOWN_BITS) throw malformed('probability result', 'cell ' + i + ' flags');
    if (!(flag & PCELL.VALUE)) {
      // C's contract compares with p != 0.0, so -0.0 is a valid zero here.
      if (flag !== 0 || value !== 0) throw malformed('probability result', 'cell ' + i + ' value');
      probabilities[i] = null;
      continue;
    }
    if (!(Number.isFinite(value) && value >= 0 && value <= 1)) {
      throw malformed('probability result', 'cell ' + i + ' value');
    }
    if (flag & PCELL.PROVEN_SAFE) {
      if ((flag & PCELL.PROVEN_MINE) || value !== 0) throw malformed('probability result', 'cell ' + i + ' proof');
      provenSafe.push(i);
    } else if (flag & PCELL.PROVEN_MINE) {
      if (value !== 1) throw malformed('probability result', 'cell ' + i + ' proof');
      provenMines.push(i);
    }
    probabilities[i] = plusZero(value);
  }
  if (provenSafe.length !== u32(88) || provenMines.length !== u32(92)) {
    throw malformed('probability result', 'proof counts');
  }

  const reason = REASON_NAMES[reasonCode];
  const meta = {
    frontier_cells: u32(40),
    components: u32(44),
    unconstrained_cells: u32(48),
    samples: u32(52),
    elapsed_ms: plusZero(elapsedMs),
    reason: reason
  };
  const placeholder = status === 'not-started' || status === 'finished';
  if (!placeholder) {
    meta.exact_components = u32(56);
    meta.sampled_components = u32(60);
    meta.sample_attempts = u32(64);
    meta.effective_sample_size = hasEss ? plusZero(ess) : null;
  }
  const result = {
    game_id: routing.gameId === undefined ? null : routing.gameId,
    generation: routing.generation === undefined ? null : routing.generation,
    revision: routing.revision === undefined ? null : routing.revision,
    status: status,
    probabilities: probabilities,
    proven_safe: provenSafe,
    proven_mines: provenMines,
    message: '',
    meta: meta
  };
  result.message = probabilityMessage({
    status: status,
    meta: meta,
    proven_safe: provenSafe,
    proven_mines: provenMines,
    hiddenCells: u32(72)
  }, routing.gameStatus);
  return result;
}

// Reads the routing-free facts of a result header the client cross-checks
// against the observation it asked about.
export function readResultHeader(buffer) {
  const bytes = asBytes(buffer, 'probability result');
  if (bytes.length < RESULT_HEADER_SIZE) throw malformed('probability result', 'short header');
  const dv = dataView(bytes);
  return {
    magic: dv.getUint32(0, true),
    version: dv.getUint32(4, true),
    status: dv.getUint32(8, true),
    width: dv.getUint32(16, true),
    height: dv.getUint32(20, true),
    totalMines: dv.getUint32(24, true),
    revealed: dv.getUint32(28, true)
  };
}

// Reads an ms_infer_limits block (diagnostics and tests; C writes it).
export function decodeLimits(buffer) {
  const bytes = asBytes(buffer, 'limits');
  if (bytes.length !== LIMITS_SIZE) throw malformed('limits', 'length');
  const dv = dataView(bytes);
  if (dv.getUint32(0, true) !== MAGIC.LIMITS) throw malformed('limits', 'magic');
  if (dv.getUint32(4, true) !== ABI_VERSION) throw malformed('limits', 'ABI version');
  return {
    nodeBudget: dv.getUint32(8, true),
    sampleBudget: dv.getUint32(12, true),
    maxStoredEntries: dv.getUint32(16, true),
    flags: dv.getUint32(20, true),
    timeBudgetMs: dv.getFloat64(24, true),
    minEffectiveSamples: dv.getFloat64(32, true),
    memoryBudgetBytes: dv.getBigUint64(40, true),
    seed: dv.getBigUint64(48, true)
  };
}

// Reads an ms_plan_limits block (planner.h; diagnostics and tests, C writes
// it). Refuses another magic or planner version.
export function decodePlanLimits(buffer) {
  const bytes = asBytes(buffer, 'plan limits');
  if (bytes.length !== PLAN_LIMITS_SIZE) throw malformed('plan limits', 'length');
  const dv = dataView(bytes);
  if (dv.getUint32(0, true) !== MAGIC.PLAN_LIMITS) throw malformed('plan limits', 'magic');
  if (dv.getUint32(4, true) !== PLANNER_VERSION) throw malformed('plan limits', 'planner version');
  return {
    exactLayoutLimit: dv.getUint32(8, true),
    exactNodeLimit: dv.getUint32(12, true),
    sampleCount: dv.getUint32(16, true),
    candidateLimit: dv.getUint32(20, true),
    rolloutStepLimit: dv.getUint32(24, true),
    flags: dv.getUint32(28, true),
    timeBudgetMs: dv.getFloat64(32, true),
    memoryBudgetBytes: dv.getBigUint64(40, true),
    seed: dv.getBigUint64(48, true),
    minRollouts: dv.getUint32(56, true)
  };
}

/*
 * Rebuilds the move advisor's answer from an ms_plan_result buffer
 * (c/planner.h) for the observation bytes it was computed from:
 *   { game_id, generation, revision, status, reason, cell, row, col,
 *     survival_probability, win_probability, standard_error, candidates,
 *     layouts, trials, incomplete, rollout_wins, search_nodes,
 *     posterior_exact, elapsed_ms, exact_wins, exact_total, observation_hash }
 * status: 'exact' (a completed search over every layout that fits the
 * clues), 'estimated' (guided full-game rollouts), 'unavailable' or 'none'
 * (nothing to suggest); reason: a PLAN_REASON_NAMES name, null for exact and
 * estimated answers. Only those two name a cell (row/col beside its index)
 * and carry probabilities; otherwise those fields are null. exact_wins and
 * exact_total are integers only for exact answers, whose win probability is
 * exactly their ratio; an estimate's is the share of its trials -
 * incomplete finished rollout rounds that were won, and rollout_wins (null
 * otherwise) is that integer number of rounds won. observation_hash is the
 * plan's 64-bit fingerprint as 16 hex digits.
 *
 * The checks mirror C's ms_plan_result_validate on everything this side can
 * see - header, planner version, dimensions/total/revealed against the
 * observation, status and reason codes and their pairing with the position
 * (no clue yet, nothing left to open, a guess), a hidden recommended cell
 * (NO_CELL otherwise), finite probabilities in range, zero fields a status
 * leaves unused, 0/1 flags, counts (rounds within layouts, candidates
 * within hidden cells, per-reason counts of unavailable answers), reserved
 * bytes - and are never stricter. C stays authoritative (ms_check_plan in
 * the game instance), in particular for the observation hash against the
 * current position.
 */
export function decodePlan(buffer, observation, routing = {}) {
  const bytes = asBytes(buffer, 'move advice');
  if (bytes.length !== PLAN_RESULT_SIZE) throw malformed('move advice', 'length');
  const obs = asBytes(observation, 'observation');
  const header = readObservationHeader(obs);
  const dv = dataView(bytes);
  const u32 = (offset) => dv.getUint32(offset, true);
  const f64 = (offset) => dv.getFloat64(offset, true);
  const bad = (detail) => malformed('move advice', detail);
  if (u32(0) !== MAGIC.PLAN_RESULT) throw bad('magic');
  if (u32(4) !== PLANNER_VERSION) throw bad('planner version');
  const status = PLAN_STATUS_NAMES[u32(8)];
  if (!status) throw bad('status');
  const reasonCode = u32(12);
  const reason = reasonCode < PLAN_REASON_NAMES.length ? PLAN_REASON_NAMES[reasonCode] : undefined;
  if (reason === undefined) throw bad('reason');
  if (u32(16) !== header.width || u32(20) !== header.height || u32(24) !== header.totalMines ||
      u32(28) !== header.revealed) {
    throw bad('another observation');
  }
  if (u32(68) !== 0) throw bad('reserved word');
  const cell = u32(40);
  const candidates = u32(44);
  const layouts = u32(48);
  const trials = u32(52);
  const incomplete = u32(56);
  const searchNodes = u32(60);
  const posteriorExact = u32(64);
  const exactWins = u32(104);
  const exactTotal = u32(108);
  const survival = f64(72);
  const win = f64(80);
  const standardError = f64(88);
  const elapsedMs = f64(96);
  const cells = header.width * header.height;
  const hidden = cells - header.revealed;
  if (![survival, win, standardError, elapsedMs].every(Number.isFinite) || elapsedMs < 0) throw bad('number');
  if (posteriorExact > 1 || (layouts === 0 && posteriorExact !== 0)) throw bad('posterior flag');
  // Rounds stop at the first unfinished one: incomplete is 0 or 1.
  if (incomplete > 1 || incomplete > trials || trials > layouts) throw bad('rollout rounds');
  if (candidates > hidden || (trials > 0 && candidates === 0) || (candidates > 0 && layouts === 0)) {
    throw bad('candidates');
  }
  // The exact search runs only on a complete listing of at least 2 layouts.
  if (searchNodes > 0 && (layouts < 2 || posteriorExact !== 1)) throw bad('search nodes');
  // A guess is needed (and possible) once something is revealed and not every
  // hidden cell is a mine.
  const guess = header.revealed > 0 && hidden > header.totalMines;
  const hiddenCell = cell < cells && obs[OBS_HEADER_SIZE + cell] === CLUE_HIDDEN;
  const noEstimates = cell === PLAN_NO_CELL && survival === 0 && win === 0 && standardError === 0 &&
    exactWins === 0 && exactTotal === 0;
  switch (status) {
    case 'none':
      if (!((reason === 'not_started' && header.revealed === 0) ||
            (reason === 'finished' && header.revealed > 0 && hidden === header.totalMines) ||
            (reason === 'certain_moves' && guess))) {
        throw bad('reason ' + reason + ' for this position');
      }
      if (!noEstimates || candidates || layouts || trials || searchNodes || posteriorExact) throw bad('empty answer');
      break;
    case 'exact':
      if (reason !== null || !guess) throw bad('reason or position');
      if (!hiddenCell) throw bad('cell');
      // Every hidden cell safe in some layout is a candidate (fewer than
      // total_mines cells are mines in all of them), and the survival is the
      // advised cell's safe-layout count over the total.
      if (candidates <= hidden - header.totalMines ||
          Math.trunc(survival * exactTotal + 0.5) / exactTotal !== survival) {
        throw bad('exact search figures');
      }
      if (exactTotal < 2 || layouts !== exactTotal || exactWins === 0 ||
          exactWins >= exactTotal || trials !== 0 || searchNodes === 0 || posteriorExact !== 1 ||
          standardError !== 0 || win !== exactWins / exactTotal || !(survival > 0 && survival < 1) ||
          win > survival) {
        throw bad('exact search figures');
      }
      break;
    case 'estimated': {
      if (reason !== null || !guess) throw bad('reason or position');
      if (!hiddenCell) throw bad('cell');
      // Never certain: survival below 1, at least one round won, and every
      // round won only with a positive standard error.
      const completed = trials - incomplete;
      if (candidates === 0 || completed === 0 || exactWins !== 0 || exactTotal !== 0 ||
          !(survival > 0 && survival < 1) || !(win > 0) || win > 1 || standardError < 0 || standardError > 0.5 ||
          (win === 1 && !(standardError > 0)) || Math.trunc(win * completed + 0.5) / completed !== win) {
        throw bad('rollout figures');
      }
      break;
    }
    default: { // unavailable: how far the planner got depends on why it stopped
      let countsOk;
      switch (reason) {
        case 'no_samples':
        case 'posterior_unavailable':
          countsOk = layouts === 0 && candidates === 0 && searchNodes === 0;
          break;
        case 'insufficient_rollouts':
          countsOk = layouts > 0;
          break;
        case 'budget':
          countsOk = true;
          break;
        default:
          throw bad('reason ' + reason + ' for this position');
      }
      if (!guess) throw bad('reason ' + reason + ' for this position');
      if (!countsOk || !noEstimates) throw bad('empty answer');
      break;
    }
  }
  const recommends = status === 'exact' || status === 'estimated';
  // Validated above: the win probability is exactly k / finished rounds.
  const rolloutWins = status === 'estimated' ? Math.trunc(win * (trials - incomplete) + 0.5) : null;
  return {
    game_id: routing.gameId === undefined ? null : routing.gameId,
    generation: routing.generation === undefined ? null : routing.generation,
    revision: routing.revision === undefined ? null : routing.revision,
    status: status,
    reason: reason,
    cell: recommends ? cell : null,
    row: recommends ? Math.floor(cell / header.width) : null,
    col: recommends ? cell % header.width : null,
    survival_probability: recommends ? plusZero(survival) : null,
    win_probability: recommends ? plusZero(win) : null,
    standard_error: recommends ? plusZero(standardError) : null,
    candidates: candidates,
    layouts: layouts,
    trials: trials,
    incomplete: incomplete,
    rollout_wins: rolloutWins,
    search_nodes: searchNodes,
    posterior_exact: posteriorExact === 1,
    elapsed_ms: plusZero(elapsedMs),
    exact_wins: status === 'exact' ? exactWins : null,
    exact_total: status === 'exact' ? exactTotal : null,
    observation_hash: dv.getBigUint64(32, true).toString(16).padStart(16, '0')
  };
}

// ------------------------------------------------------- worker protocol
//
// main -> worker   { type: 'init', module }                      once
//                  { type: 'solve', id, generation, revision, observation }
//                  { type: 'plan', id, generation, revision, observation }
// worker -> main   { type: 'ready' }
//                  { type: 'init-failed', error }
//                  { type: 'result', id, generation, revision, result }
//                  { type: 'plan-result', id, generation, revision, result }
//                  { type: 'error', id, generation, revision, error }
// observation/result are ArrayBuffer copies (transferred), never views of a
// WASM memory; a plan result is exactly PLAN_RESULT_SIZE bytes. A solve is
// answered by 'result' or 'error', a plan (the move advisor, on the same
// worker and instance, one request at a time) by 'plan-result' or 'error'.
// error = { code, status, message, fatal }. Messages carrying any other
// field are protocol violations: only public observations and routing
// numbers ever reach the solver.

function hasExactKeys(message, keys) {
  if (!message || typeof message !== 'object' || Array.isArray(message)) return false;
  const own = Object.keys(message);
  return own.length === keys.length && keys.every((key) => Object.prototype.hasOwnProperty.call(message, key));
}

function isRequestId(value) {
  return isU32(value) && value >= 1;
}

function isGeneration(value) {
  return isU32(value) && value >= 1 && value <= GENERATION_MAX;
}

function isRevision(value) {
  return isU32(value) && value <= REVISION_MAX;
}

function protocolError(message) {
  return new EngineError('invalid_message', message, { kind: 'protocol' });
}

export function checkSolveRequest(message) {
  return checkObservationRequest(message, 'solve');
}

// A move-advice request: the same public fields as a solve request.
export function checkPlanRequest(message) {
  return checkObservationRequest(message, 'plan');
}

function checkObservationRequest(message, type) {
  if (!hasExactKeys(message, ['type', 'id', 'generation', 'revision', 'observation']) || message.type !== type) {
    throw protocolError('A ' + type + ' request must have exactly type, id, generation, revision and observation.');
  }
  if (!isRequestId(message.id)) throw protocolError('The ' + type + ' request id must be a positive whole number.');
  if (!isGeneration(message.generation) || !isRevision(message.revision)) {
    throw protocolError('The ' + type + ' request generation/revision is out of range.');
  }
  const observation = message.observation;
  if (!(observation instanceof ArrayBuffer) || observation.byteLength < OBS_HEADER_SIZE ||
      observation.byteLength > MAX_OBSERVATION_SIZE || observation.byteLength % 8 !== 0) {
    throw protocolError('The observation must be an ArrayBuffer copy of an engine observation.');
  }
  let header;
  try {
    header = readObservationHeader(observation);
  } catch (error) {
    if (!(error instanceof EngineError)) throw error;
    throw protocolError('The observation is malformed: ' + error.message);
  }
  return {
    id: message.id,
    generation: message.generation,
    revision: message.revision,
    observation: new Uint8Array(observation),
    header: header
  };
}

function checkErrorPayload(error) {
  return hasExactKeys(error, ['code', 'status', 'message', 'fatal']) &&
    typeof error.code === 'string' && /^[a-z0-9_]{1,64}$/.test(error.code) &&
    (error.status === null || (isU32(error.status) && STATUS_NAMES.has(error.status) && error.status !== 0)) &&
    typeof error.message === 'string' && error.message.length > 0 && error.message.length <= 2000 &&
    typeof error.fatal === 'boolean';
}

// Validates a worker -> main message. Returns it unchanged or throws.
export function checkWorkerMessage(message) {
  const type = message && typeof message === 'object' ? message.type : undefined;
  switch (type) {
    case 'ready':
      if (hasExactKeys(message, ['type'])) return message;
      break;
    case 'init-failed':
      if (hasExactKeys(message, ['type', 'error']) && checkErrorPayload(message.error)) return message;
      break;
    case 'result':
      if (hasExactKeys(message, ['type', 'id', 'generation', 'revision', 'result']) &&
          isRequestId(message.id) && isGeneration(message.generation) && isRevision(message.revision) &&
          message.result instanceof ArrayBuffer && message.result.byteLength >= RESULT_HEADER_SIZE &&
          message.result.byteLength <= MAX_RESULT_SIZE && message.result.byteLength % 8 === 0) {
        return message;
      }
      break;
    case 'plan-result':
      if (hasExactKeys(message, ['type', 'id', 'generation', 'revision', 'result']) &&
          isRequestId(message.id) && isGeneration(message.generation) && isRevision(message.revision) &&
          message.result instanceof ArrayBuffer && message.result.byteLength === PLAN_RESULT_SIZE) {
        return message;
      }
      break;
    case 'error':
      if (hasExactKeys(message, ['type', 'id', 'generation', 'revision', 'error']) &&
          isRequestId(message.id) && isGeneration(message.generation) && isRevision(message.revision) &&
          checkErrorPayload(message.error)) {
        return message;
      }
      break;
    default:
      break;
  }
  throw protocolError('The odds worker sent a malformed message.');
}

export function errorPayload(error, fatal) {
  if (error instanceof EngineError) {
    return {
      code: /^[a-z0-9_]{1,64}$/.test(error.code) ? error.code : 'internal_error',
      status: error.status,
      message: String(error.message || error.code).slice(0, 2000) || error.code,
      fatal: fatal
    };
  }
  return {
    code: 'trap',
    status: null,
    message: ('The odds calculator stopped unexpectedly: ' + describeThrown(error)).slice(0, 2000),
    fatal: true
  };
}

export function errorFromPayload(payload, kind) {
  return new EngineError(payload.code, payload.message, { status: payload.status, kind: kind });
}

export function describeThrown(error) {
  if (error instanceof ProcExit) return 'the engine exited with status ' + error.status;
  if (error && typeof error === 'object' && typeof error.message === 'string' && error.message) {
    return (error.name ? error.name + ': ' : '') + error.message;
  }
  return String(error);
}

// --------------------------------------------------------------- WASI host

// The wasi_snapshot_preview1 functions corec's platform_wasm.c may import and
// the application host boundary (engine.h, "Reactor ABI rules").
export const COREC_WASI_IMPORTS = Object.freeze(['args_get', 'args_sizes_get', 'environ_get',
  'environ_sizes_get', 'fd_close', 'fd_read', 'fd_seek', 'fd_tell', 'fd_write', 'path_open', 'proc_exit']);
export const HOST_IMPORTS = Object.freeze(['ms_host.now_ms']);

const LOG_LINES = 40;

// Line-buffered sink for the engine's stdout/stderr: lines go to `log` and
// the most recent ones are kept for error reports.
function makeLogSink(stream, label, log, recent) {
  const decoder = new TextDecoder();
  let pending = '';
  const emit = (line) => {
    recent.push(stream + ': ' + line);
    if (recent.length > LOG_LINES) recent.shift();
    log(stream, '[' + label + '] ' + line);
  };
  return {
    write(bytes) {
      pending += decoder.decode(bytes, { stream: true });
      let newline = pending.indexOf('\n');
      while (newline >= 0) {
        emit(pending.slice(0, newline));
        pending = pending.slice(newline + 1);
        newline = pending.indexOf('\n');
      }
    },
    flush() {
      if (pending) {
        emit(pending);
        pending = '';
      }
    }
  };
}

function defaultLog(stream, text) {
  if (stream === 'stderr') console.error(text);
  else console.info(text);
}

// The WASI `io` object for corec's makeWasi: no arguments or environment,
// empty stdin, logs for stdout/stderr and a filesystem that denies every
// operation (the engine never opens files).
export function makeEngineIo(label, log = defaultLog) {
  const recent = [];
  const stdout = makeLogSink('stdout', label, log, recent);
  const stderr = makeLogSink('stderr', label, log, recent);
  return {
    io: {
      argv: [label],
      environ: [],
      stdin: { read: () => new Uint8Array(0) },
      stdout: stdout,
      stderr: stderr,
      fs: {
        open: () => -1,
        close: () => 8,
        read: () => null,
        write: () => -1,
        seek: () => null,
        tell: () => null
      }
    },
    recent: recent,
    flush() {
      stdout.flush();
      stderr.flush();
    }
  };
}

function hostNow() {
  return performance.now();
}

// ------------------------------------------------------- module and exports

// The reactor exports this host calls (c/wasm_api.h). Extra exports
// (__heap_base, corec's wasm_buddy_alloc/free) are allowed and unused.
export const REQUIRED_EXPORTS = Object.freeze([
  'ms_abi_version', 'ms_init', 'ms_alloc', 'ms_free', 'ms_view_bytes', 'ms_obs_bytes', 'ms_result_bytes',
  'ms_status_text', 'ms_reason_text', 'ms_live_bytes', 'ms_new_game', 'ms_act', 'ms_get_view',
  'ms_get_observation', 'ms_get_cached_result', 'ms_accept_result', 'ms_apply_autosolve',
  'ms_init_default_limits', 'ms_solve_observation', 'ms_init_plan_limits', 'ms_plan_observation', 'ms_check_plan'
]);

function loadError(code, message, cause) {
  return new EngineError(code, message, { kind: 'load', cause: cause });
}

// Refuses modules whose imports or exports differ from the documented ABI
// before instantiating them: a mismatched build fails loudly, not later.
export function auditEngineModule(module) {
  if (!(module instanceof WebAssembly.Module)) {
    throw loadError('engine_load_failed', 'The game engine is not a compiled WebAssembly module.');
  }
  for (const entry of WebAssembly.Module.imports(module)) {
    const name = entry.module + '.' + entry.name;
    const allowed = entry.kind === 'function' && (
      (entry.module === 'wasi_snapshot_preview1' && COREC_WASI_IMPORTS.includes(entry.name)) ||
      HOST_IMPORTS.includes(name));
    if (!allowed) {
      throw loadError('abi_mismatch', 'The game engine build does not match this page (it needs the unsupported ' +
        'import ' + name + ').');
    }
  }
  const exported = new Map(WebAssembly.Module.exports(module).map((entry) => [entry.name, entry.kind]));
  if (exported.get('memory') !== 'memory') {
    throw loadError('abi_mismatch', 'The game engine build does not match this page (it exports no memory).');
  }
  const missing = REQUIRED_EXPORTS.filter((name) => exported.get(name) !== 'function');
  if (missing.length) {
    throw loadError('abi_mismatch', 'The game engine build does not match this page (missing ' +
      missing.join(', ') + ').');
  }
}

/*
 * Fetches and compiles the engine once; the Module is reused for every
 * instance (the game instance and each solver worker). Uses streaming
 * compilation when the server labels the file application/wasm.
 */
export async function compileEngineModule(url) {
  let response;
  try {
    response = await fetch(url, { credentials: 'same-origin' });
  } catch (error) {
    throw loadError('engine_load_failed', 'Could not download the game engine: ' + describeThrown(error) + '.', error);
  }
  if (!response.ok) {
    throw loadError('engine_load_failed', 'Could not download the game engine (HTTP ' + response.status + ').');
  }
  let module;
  try {
    const type = response.headers.get('Content-Type') || '';
    module = /^application\/wasm\s*(;|$)/i.test(type) && typeof WebAssembly.compileStreaming === 'function'
      ? await WebAssembly.compileStreaming(response)
      : await WebAssembly.compile(await response.arrayBuffer());
  } catch (error) {
    throw loadError('engine_load_failed', 'The game engine could not be compiled: ' + describeThrown(error) + '.', error);
  }
  auditEngineModule(module);
  return module;
}

// --------------------------------------------------------------- instances

// Out-parameter slots in each instance's scratch buffer (uint32, 4-aligned,
// never overlapping).
const OUT_A = 0;
const OUT_B = 8;
const SCRATCH_BYTES = 16;

/*
 * One instantiated engine. Every export call goes through call(): an
 * exception escaping WebAssembly poisons the instance and becomes an
 * EngineError of kind 'trap' (the engine's recent stdout/stderr lines are
 * attached as `log`). Memory views are rebuilt for every access.
 */
class WasmInstance {
  constructor(instance, ioState, role) {
    this.exports = instance.exports;
    this.memory = instance.exports.memory;
    this.ioState = ioState;
    this.role = role;
    this.failure = null;
    this.scratch = 0;
  }

  call(name, ...args) {
    if (this.failure) throw this.failure;
    try {
      return this.exports[name](...args);
    } catch (error) {
      this.ioState.flush();
      const failure = new EngineError('trap', 'The ' + this.role + ' stopped unexpectedly (' +
        describeThrown(error) + ').', { kind: 'trap', cause: error });
      failure.log = this.ioState.recent.slice();
      this.failure = failure;
      throw failure;
    }
  }

  status(name, ...args) {
    return this.call(name, ...args) | 0;
  }

  u32(name, ...args) {
    return this.call(name, ...args) >>> 0;
  }

  check(status) {
    if (status !== STATUS.OK) throw errorFromStatus(status);
  }

  alloc(size) {
    const ptr = this.u32('ms_alloc', size);
    if (!ptr) {
      throw new EngineError('resource_exhausted', 'The engine has no buffer space left for this request.', {
        status: STATUS.RESOURCE_EXHAUSTED,
        kind: 'resource'
      });
    }
    return ptr;
  }

  free(ptr) {
    if (!ptr || this.failure) return;
    const status = this.status('ms_free', ptr);
    if (status !== STATUS.OK) throw errorFromStatus(status);
  }

  bytes(ptr, length) {
    return new Uint8Array(this.memory.buffer, ptr, length);
  }

  copyOut(ptr, length) {
    return this.bytes(ptr, length).slice();
  }

  copyIn(ptr, data) {
    this.bytes(ptr, data.length).set(data);
  }

  readU32(ptr) {
    return new DataView(this.memory.buffer).getUint32(ptr, true);
  }

  readFlag(ptr) {
    const value = this.readU32(ptr);
    if (value > 1) throw malformed('flag', 'value ' + value);
    return value === 1;
  }

  cString(ptr) {
    const all = new Uint8Array(this.memory.buffer);
    let end = ptr;
    while (end < all.length && all[end] !== 0 && end - ptr < 256) end++;
    if (end >= all.length || all[end] !== 0) throw malformed('name', 'unterminated string');
    let text = '';
    for (let i = ptr; i < end; i++) text += String.fromCharCode(all[i]);
    return text;
  }

  liveBytes(pool) {
    return this.u32('ms_live_bytes', pool);
  }

  // The module's own status/reason names and buffer sizes must agree with
  // this file's tables (a cheap guard against ABI drift).
  verifyTables() {
    for (const [status, name] of STATUS_NAMES) {
      if (this.cString(this.u32('ms_status_text', status)) !== name) {
        throw loadError('abi_mismatch', 'The game engine names status ' + status + ' differently.');
      }
    }
    for (let reason = 0; reason < REASON_NAMES.length; reason++) {
      const ptr = this.u32('ms_reason_text', reason);
      const name = ptr ? this.cString(ptr) : null;
      if (name !== (REASON_NAMES[reason] || '')) {
        throw loadError('abi_mismatch', 'The game engine names reason ' + reason + ' differently.');
      }
    }
    if (this.u32('ms_reason_text', REASON_NAMES.length) !== 0) {
      throw loadError('abi_mismatch', 'The game engine knows more odds reasons than this page.');
    }
    for (const [width, height] of [[5, 5], [9, 9], [80, 80], [3001, 1], [0, 5], [6401, 1]]) {
      if (this.u32('ms_view_bytes', width, height) !== viewSize(width, height) ||
          this.u32('ms_obs_bytes', width, height) !== observationSize(width, height) ||
          this.u32('ms_result_bytes', width, height) !== resultSize(width, height)) {
        throw loadError('abi_mismatch', 'The game engine sizes its buffers differently.');
      }
    }
  }
}

/*
 * The main-thread game instance (wasm_api.h, "Game instance call
 * sequences"). Keeps one view/observation/result buffer for the current
 * board size and a scratch buffer for out-parameters; every output is
 * copied into a JS-owned Uint8Array before the next call.
 */
export class WasmGameEngine extends WasmInstance {
  constructor(instance, ioState) {
    super(instance, ioState, 'game engine');
    this.generation = 0;
    this.width = 0;
    this.height = 0;
    this.buffers = null; // { view, viewLen, obs, obsLen, result, resultLen }
  }

  setup() {
    this.scratch = this.alloc(SCRATCH_BYTES);
    this.plan = this.alloc(PLAN_RESULT_SIZE);
  }

  // Buffers for width x height, allocated before the game changes so a
  // failed allocation leaves the current game and its buffers intact.
  allocateBuffers(width, height) {
    const viewLen = this.u32('ms_view_bytes', width, height);
    const obsLen = this.u32('ms_obs_bytes', width, height);
    const resultLen = this.u32('ms_result_bytes', width, height);
    if (!viewLen || !obsLen || !resultLen) return null;
    const next = { view: 0, viewLen: viewLen, obs: 0, obsLen: obsLen, result: 0, resultLen: resultLen };
    try {
      next.view = this.alloc(viewLen);
      next.obs = this.alloc(obsLen);
      next.result = this.alloc(resultLen);
    } catch (error) {
      this.releaseBuffers(next);
      throw error;
    }
    return next;
  }

  releaseBuffers(buffers) {
    if (!buffers) return;
    this.free(buffers.view);
    this.free(buffers.obs);
    this.free(buffers.result);
  }

  newGame(width, height, mines, seedLo, seedHi) {
    const sameSize = this.buffers && width === this.width && height === this.height;
    const fresh = sameSize ? null : this.allocateBuffers(width, height);
    let status;
    try {
      status = this.status('ms_new_game', width, height, mines, seedLo, seedHi, this.scratch + OUT_A);
    } catch (error) {
      if (!this.failure) this.releaseBuffers(fresh);
      throw error;
    }
    if (status !== STATUS.OK) {
      this.releaseBuffers(fresh);
      throw errorFromStatus(status);
    }
    const generation = this.readU32(this.scratch + OUT_A);
    if (generation < 1 || generation > GENERATION_MAX || generation <= this.generation) {
      throw malformed('new game', 'generation ' + generation);
    }
    if (fresh) {
      this.releaseBuffers(this.buffers);
      this.buffers = fresh;
      this.width = width;
      this.height = height;
    }
    this.generation = generation;
    return generation;
  }

  requireGame() {
    if (!this.buffers || !this.generation) {
      throw new EngineError('no_game', 'No game has been started yet.', { kind: 'internal' });
    }
    return this.buffers;
  }

  act(generation, revision, action, row, col) {
    this.requireGame();
    this.check(this.status('ms_act', generation, revision, action, row, col, this.scratch + OUT_A));
    return this.readFlag(this.scratch + OUT_A);
  }

  view() {
    const b = this.requireGame();
    this.check(this.status('ms_get_view', this.generation, b.view, b.viewLen));
    return this.copyOut(b.view, b.viewLen);
  }

  observe(generation, revision) {
    const b = this.requireGame();
    this.check(this.status('ms_get_observation', generation, revision, b.obs, b.obsLen));
    return this.copyOut(b.obs, b.obsLen);
  }

  // The reusable answer for (generation, revision), or null when a solve is
  // needed.
  cachedResult(generation, revision) {
    const b = this.requireGame();
    this.check(this.status('ms_get_cached_result', generation, revision, b.result, b.resultLen,
      this.scratch + OUT_A));
    return this.readFlag(this.scratch + OUT_A) ? this.copyOut(b.result, b.resultLen) : null;
  }

  acceptResult(generation, revision, bytes) {
    const b = this.requireGame();
    if (!(bytes instanceof Uint8Array) || bytes.length !== b.resultLen) {
      throw errorFromStatus(STATUS.INVALID_RESULT);
    }
    this.copyIn(b.result, bytes);
    this.check(this.status('ms_accept_result', generation, revision, b.result, b.resultLen));
  }

  autosolve(generation, revision) {
    this.requireGame();
    this.check(this.status('ms_apply_autosolve', generation, revision, this.scratch + OUT_A, this.scratch + OUT_B));
    return { available: this.readFlag(this.scratch + OUT_A), changed: this.readFlag(this.scratch + OUT_B) };
  }

  // Whether worker plan bytes answer the current observation of
  // (generation, revision) (ms_check_plan). Throws the C status otherwise;
  // nothing is stored either way.
  checkPlan(generation, revision, bytes) {
    this.requireGame();
    if (!(bytes instanceof Uint8Array) || bytes.length !== PLAN_RESULT_SIZE) {
      throw errorFromStatus(STATUS.INVALID_RESULT);
    }
    this.copyIn(this.plan, bytes);
    this.check(this.status('ms_check_plan', generation, revision, this.plan, PLAN_RESULT_SIZE));
  }
}

/*
 * The worker's solver instance: default solver and planner limits from C
 * once, then one ms_solve_observation or ms_plan_observation per request
 * with buffers that are freed afterwards.
 */
export class WasmSolverEngine extends WasmInstance {
  constructor(instance, ioState) {
    super(instance, ioState, 'odds calculator');
    this.limits = 0;
    this.planLimits = 0;
  }

  setup() {
    this.limits = this.alloc(LIMITS_SIZE);
    this.check(this.status('ms_init_default_limits', this.limits, LIMITS_SIZE));
    this.planLimits = this.alloc(PLAN_LIMITS_SIZE);
    this.check(this.status('ms_init_plan_limits', this.planLimits, PLAN_LIMITS_SIZE));
    this.defaultPlanLimits(); // a module from another planner version is refused here
  }

  defaultLimits() {
    return decodeLimits(this.copyOut(this.limits, LIMITS_SIZE));
  }

  defaultPlanLimits() {
    return decodePlanLimits(this.copyOut(this.planLimits, PLAN_LIMITS_SIZE));
  }

  // Plans the next move for a copied public observation; returns a
  // JS-owned copy of the PLAN_RESULT_SIZE plan bytes.
  plan(observation) {
    const header = readObservationHeader(observation);
    const obsLen = this.u32('ms_obs_bytes', header.width, header.height);
    if (obsLen !== observation.length) throw errorFromStatus(STATUS.INVALID_OBSERVATION);
    const obs = this.alloc(obsLen);
    let result = 0;
    try {
      result = this.alloc(PLAN_RESULT_SIZE);
      this.copyIn(obs, observation);
      this.check(this.status('ms_plan_observation', obs, obsLen, this.planLimits, PLAN_LIMITS_SIZE, result,
        PLAN_RESULT_SIZE));
      return this.copyOut(result, PLAN_RESULT_SIZE);
    } finally {
      if (!this.failure) {
        this.free(result);
        this.free(obs);
      }
    }
  }

  // Solves a copied public observation; returns a JS-owned copy of the result.
  solve(observation) {
    const header = readObservationHeader(observation);
    const obsLen = this.u32('ms_obs_bytes', header.width, header.height);
    const resultLen = this.u32('ms_result_bytes', header.width, header.height);
    if (obsLen !== observation.length || resultLen !== resultSize(header.width, header.height)) {
      throw errorFromStatus(STATUS.INVALID_OBSERVATION);
    }
    const obs = this.alloc(obsLen);
    let result = 0;
    try {
      result = this.alloc(resultLen);
      this.copyIn(obs, observation);
      this.check(this.status('ms_solve_observation', obs, obsLen, this.limits, LIMITS_SIZE, result, resultLen));
      return this.copyOut(result, resultLen);
    } finally {
      if (!this.failure) {
        this.free(result);
        this.free(obs);
      }
    }
  }
}

/*
 * Instantiates the compiled module with corec's WASI host (no arguments,
 * environment or files; logs only) and the ms_host clock, checks the ABI
 * version, runs ms_init exactly once and allocates the instance's own
 * buffers. Options: label, now (clock, default performance.now), log.
 */
async function instantiate(Kind, module, options = {}) {
  auditEngineModule(module);
  const label = options.label || 'minesweeper';
  const ioState = makeEngineIo(label, options.log || defaultLog);
  const wasi = makeWasi(ioState.io);
  const now = options.now || hostNow;
  let instance;
  try {
    instance = await WebAssembly.instantiate(module, {
      wasi_snapshot_preview1: wasi.imports.wasi_snapshot_preview1,
      ms_host: { now_ms: () => now() }
    });
  } catch (error) {
    throw loadError('engine_load_failed', 'The game engine could not be started: ' + describeThrown(error) + '.', error);
  }
  wasi.setMemory(instance.exports.memory);
  const host = new Kind(instance, ioState);
  try {
    const version = host.u32('ms_abi_version');
    if (version !== ABI_VERSION) {
      throw loadError('abi_mismatch', 'The game engine speaks ABI version ' + version + ', this page needs ' +
        ABI_VERSION + '.');
    }
    const status = host.status('ms_init');
    if (status !== STATUS.OK) {
      throw loadError('engine_load_failed', 'The game engine failed to initialize (' + statusName(status) + ').');
    }
    host.verifyTables();
    host.setup();
  } catch (error) {
    if (error instanceof EngineError && error.kind === 'load') throw error;
    throw loadError('engine_load_failed', 'The game engine failed to initialize: ' + describeThrown(error) + '.', error);
  }
  return host;
}

export function instantiateGameEngine(module, options) {
  return instantiate(WasmGameEngine, module, options);
}

export function instantiateSolverEngine(module, options) {
  return instantiate(WasmSolverEngine, module, options);
}

export { makeWasi, ProcExit };
