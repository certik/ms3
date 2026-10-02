/*
 * engine-client.js - the page's local game engine (replaces the HTTP API).
 *
 * One compiled WebAssembly module, two independent single-threaded instances:
 *
 *   - the game instance runs here, on the main thread. It owns the
 *     authoritative C game, input validation, public observations, the
 *     revision-bound odds cache and atomic autosolve batches. Its calls are
 *     small and synchronous.
 *   - the solver instance runs C ms_solve, and the move advisor's ms_plan,
 *     in a dedicated worker (probability-worker.js), one request at a time.
 *     It only ever receives a copied public observation plus routing
 *     numbers: never flags, the hidden board, the game's seed or its memory.
 *
 * A synchronous WASM call cannot be interrupted by a message, so obsolete
 * inference is cancelled by terminating the worker; the next request starts
 * a new one from the cached WebAssembly.Module. Every request carries the
 * game generation, revision and a request id; answers that do not match the
 * current request are ignored, and C validates each result again against a
 * freshly built observation before accepting its proofs (odds) or letting
 * the page show it (move advice, which is never played or cached as odds).
 * Odds always come first: a plan starts only once no solve is in flight,
 * and an odds request abandons a plan that is still running.
 *
 * All methods return the former server's JSON shapes (GameState and odds
 * payloads), so the page's strict parsers stay unchanged. Failures are
 * EngineError instances (wasm-host.js) with a stable `code` and `kind`.
 */

import {
  ACTIONS,
  EngineError,
  GAME_MAX_SIDE,
  GAME_MIN_SIDE,
  GENERATION_MAX,
  REVISION_MAX,
  checkWorkerMessage,
  compileEngineModule,
  decodePlan,
  decodeResult,
  decodeView,
  describeThrown,
  errorFromPayload,
  instantiateGameEngine,
  isU32,
  readObservationHeader,
  readResultHeader,
  requireU32,
  resultSize,
  statusMessage
} from './wasm-host.js';

// The dist/ layout (scripts/build.mjs REACTOR_FILE, VENDOR_FILES) puts the
// engine beside this module; the build's reference audit checks this URL.
export const ENGINE_WASM_URL = new URL('./minesweeper.wasm', import.meta.url);
export const SOLVER_WORKER_URL = new URL('./probability-worker.js', import.meta.url);

// A hang detector only: the solver's own time budget is 1.5 seconds, the
// planner's 3 seconds.
export const SOLVER_TIMEOUT_MS = 60000;

const RESULT_STATUS_UNAVAILABLE = 3;
const RESULT_STATUS_NOT_STARTED = 4; // and 5, finished: placeholders without odds

// Worker request kinds: message type, answer type and what the page calls
// the job in messages.
const JOBS = Object.freeze({
  solve: { reply: 'result', name: 'odds calculator', stat: 'solvesSent' },
  plan: { reply: 'plan-result', name: 'move advisor', stat: 'plansSent' }
});

function aborted() {
  return new EngineError('aborted', 'The odds request was cancelled because the board changed.', { kind: 'aborted' });
}

function planAborted() {
  return new EngineError('aborted', 'The move advice was cancelled because the board or the odds changed.', {
    kind: 'aborted'
  });
}

function disposed() {
  return new EngineError('engine_disposed', 'The game engine was shut down.', { kind: 'load' });
}

function workerFailure(code, message, cause) {
  return new EngineError(code, message, { kind: 'worker', cause: cause });
}

function inputError(code, context) {
  return new EngineError(code, statusMessage(code, context), { kind: 'input' });
}

function hasExactKeys(value, keys) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const own = Object.keys(value);
  return own.length === keys.length && keys.every((key) => Object.prototype.hasOwnProperty.call(value, key));
}

function requireRequest(value, keys, what) {
  if (!hasExactKeys(value, keys)) {
    throw new EngineError('invalid_request', what + ' takes exactly: ' + keys.join(', ') + '.', { kind: 'input' });
  }
  return value;
}

/*
 * Routing numbers (wasm_api.h, "Value passing"): anything but a safe
 * integer, a negative revision or a generation outside 1..GENERATION_MAX is
 * invalid_revision; a safe-integer revision above REVISION_MAX can never be
 * current, so it is stale_revision without calling C.
 */
function routingOf(request) {
  const generation = request.generation;
  const revision = request.revision;
  if (typeof generation !== 'number' || !Number.isSafeInteger(generation) || generation < 1 ||
      generation > GENERATION_MAX || typeof revision !== 'number' || !Number.isSafeInteger(revision) ||
      revision < 0) {
    return { invalid: true };
  }
  if (revision > REVISION_MAX) return { stale: true, revision: revision };
  return { generation: generation, revision: revision };
}

function isSafeInt(value) {
  return typeof value === 'number' && Number.isSafeInteger(value);
}

function defaultCreateWorker(url) {
  return new Worker(url, { type: 'module', name: 'minesweeper-odds' });
}

function defaultRandomSeed() {
  const words = new Uint32Array(2);
  crypto.getRandomValues(words);
  return { lo: words[0], hi: words[1] };
}

// ------------------------------------------------------------ solver worker

/*
 * Owns at most one solver worker and at most one job at a time.
 *
 *   solve(job)  starts the worker if needed (posting the cached module),
 *               sends one request and resolves with the answer bytes (a
 *               transferred ArrayBuffer wrapped in a Uint8Array). job.kind
 *               is 'solve' (default: odds, answered by 'result') or 'plan'
 *               (move advice, answered by 'plan-result'); an answer of the
 *               other kind is a protocol violation.
 *   cancel(kind) rejects the job with an `aborted` error and terminates the
 *               worker if it is starting or working; an idle ready worker is
 *               kept for the next request. With a kind, only a job of that
 *               kind is cancelled.
 *   recycle()   drops an idle worker whose answer was rejected.
 *
 * Any worker failure (script/module load error, uncaught exception, trap
 * reported by the worker, malformed or unexpected message, timeout) rejects
 * the job with kind 'worker' and discards the worker; the next solve()
 * creates a fresh one. Messages from discarded workers are ignored.
 */
export class SolverHost {
  constructor(options) {
    this.module = options.module;
    this.workerUrl = options.workerUrl || SOLVER_WORKER_URL;
    this.createWorker = options.createWorker || defaultCreateWorker;
    this.timeoutMs = options.timeoutMs || SOLVER_TIMEOUT_MS;
    this.worker = null;
    this.ready = null; // promise resolved once the current worker is initialized
    this.job = null;
    this.nextId = 1;
    this.stats = { workersCreated: 0, workersTerminated: 0, solvesSent: 0, plansSent: 0, lateMessages: 0 };
  }

  get busy() {
    return this.job !== null;
  }

  // 'solve', 'plan' or null when idle.
  get jobKind() {
    return this.job ? this.job.kind : null;
  }

  solve(request) {
    if (this.job) throw new Error('SolverHost.solve: a job is already running');
    const kind = request.kind === undefined ? 'solve' : request.kind;
    if (!Object.prototype.hasOwnProperty.call(JOBS, kind)) {
      throw new Error('SolverHost.solve: unknown request kind ' + String(kind));
    }
    const job = {
      kind: kind,
      id: this.nextId++,
      generation: request.generation,
      revision: request.revision,
      observation: request.observation,
      timer: 0,
      resolve: null,
      reject: null,
      promise: null
    };
    job.promise = new Promise((resolve, reject) => {
      job.resolve = resolve;
      job.reject = reject;
    });
    this.job = job;
    const name = JOBS[kind].name;
    job.timer = setTimeout(() => {
      if (this.job !== job) return;
      this.fail(job, workerFailure('solver_timeout',
        'The ' + name + ' did not finish within ' + Math.round(this.timeoutMs / 1000) + ' seconds.'));
    }, this.timeoutMs);
    this.ensureWorker().then(() => {
      if (this.job !== job) return;
      const buffer = job.observation.buffer.byteLength === job.observation.byteLength
        ? job.observation.buffer
        : job.observation.slice().buffer;
      job.observation = null;
      // postMessage throws synchronously (e.g. DataCloneError, or a worker
      // that can no longer receive): fail this job now and drop the worker
      // instead of leaving it pending until the watchdog.
      try {
        this.worker.postMessage({
          type: kind,
          id: job.id,
          generation: job.generation,
          revision: job.revision,
          observation: buffer
        }, [buffer]);
      } catch (error) {
        this.fail(job, workerFailure('solver_unavailable',
          'The ' + name + ' could not receive the position: ' + describeThrown(error) + '.', error));
        return;
      }
      this.stats[JOBS[kind].stat]++;
    }, (error) => {
      if (this.job === job) this.fail(job, error);
    });
    return job.promise;
  }

  cancel(kind) {
    const job = this.job;
    if (!job || (kind !== undefined && job.kind !== kind)) return false;
    this.finish(job);
    this.discardWorker();
    job.reject(job.kind === 'plan' ? planAborted() : aborted());
    return true;
  }

  dispose() {
    this.cancel();
    this.discardWorker();
  }

  // Drops an idle worker whose last answer was rejected, so the next solve
  // starts a fresh instance. A worker already busy with a newer job is left
  // to that job.
  recycle() {
    if (!this.job) this.discardWorker();
  }

  // ---------------------------------------------------------------- private

  ensureWorker() {
    if (this.worker && this.ready) return this.ready;
    let worker;
    try {
      worker = this.createWorker(this.workerUrl);
    } catch (error) {
      return Promise.reject(workerFailure('solver_unavailable',
        'The odds calculator could not start: ' + describeThrown(error), error));
    }
    this.worker = worker;
    this.stats.workersCreated++;
    let settle = null;
    this.ready = new Promise((resolve, reject) => {
      settle = { resolve: resolve, reject: reject };
    });
    // A rejected ready promise is always observed through solve(); avoid an
    // unhandled-rejection report when the worker dies between jobs.
    this.ready.catch(() => {});
    worker.onmessage = (event) => this.onMessage(worker, settle, event.data);
    worker.onmessageerror = () => this.onWorkerError(worker, settle, workerFailure('invalid_message',
      'The odds calculator sent a message that could not be read.'));
    worker.onerror = (event) => {
      const detail = event && event.message ? ': ' + event.message : '';
      this.onWorkerError(worker, settle, workerFailure(settle.done ? 'solver_crashed' : 'solver_unavailable',
        (settle.done ? 'The odds calculator stopped unexpectedly' : 'The odds calculator could not start') + detail + '.'));
    };
    try {
      worker.postMessage({ type: 'init', module: this.module });
    } catch (error) {
      this.discardWorker();
      return Promise.reject(workerFailure('solver_unavailable',
        'The odds calculator could not start: ' + describeThrown(error), error));
    }
    return this.ready;
  }

  onMessage(worker, settle, data) {
    if (worker !== this.worker) {
      this.stats.lateMessages++;
      return;
    }
    let message;
    try {
      message = checkWorkerMessage(data);
    } catch (error) {
      if (!(error instanceof EngineError)) throw error;
      this.onWorkerError(worker, settle, workerFailure('invalid_message', error.message, error));
      return;
    }
    if (message.type === 'ready' || message.type === 'init-failed') {
      if (settle.done) {
        this.onWorkerError(worker, settle, workerFailure('invalid_message',
          'The odds calculator reported its startup twice.'));
        return;
      }
      settle.done = true;
      if (message.type === 'ready') {
        settle.resolve();
      } else {
        const error = workerFailure('solver_unavailable', 'The odds calculator could not start: ' +
          message.error.message);
        this.discardWorker();
        settle.reject(error);
      }
      return;
    }
    const job = this.job;
    if (!settle.done || !job || message.id !== job.id ||
        message.generation !== job.generation || message.revision !== job.revision ||
        (message.type !== 'error' && message.type !== JOBS[job.kind].reply)) {
      // A live worker only ever answers the job it was given, in its kind.
      this.onWorkerError(worker, settle, workerFailure('invalid_message',
        'The odds calculator answered a request it was not given.'));
      return;
    }
    this.finish(job);
    if (message.type !== 'error') {
      job.resolve(new Uint8Array(message.result));
      return;
    }
    if (message.error.fatal) this.discardWorker();
    job.reject(errorFromPayload(message.error, 'worker'));
  }

  onWorkerError(worker, settle, error) {
    if (worker !== this.worker) {
      this.stats.lateMessages++;
      return;
    }
    this.discardWorker();
    if (!settle.done) {
      settle.done = true;
      settle.reject(error);
    }
    const job = this.job;
    if (job) this.fail(job, error);
  }

  fail(job, error) {
    this.finish(job);
    this.discardWorker();
    job.reject(error);
  }

  finish(job) {
    clearTimeout(job.timer);
    if (this.job === job) this.job = null;
  }

  discardWorker() {
    const worker = this.worker;
    if (!worker) return;
    this.worker = null;
    this.ready = null;
    worker.onmessage = null;
    worker.onmessageerror = null;
    worker.onerror = null;
    worker.terminate();
    this.stats.workersTerminated++;
  }
}

// ------------------------------------------------------------ engine client

/*
 * new EngineClient(options)
 *   wasmUrl      engine WASM (default minesweeper.wasm beside this module)
 *   module       an already compiled WebAssembly.Module (tests)
 *   workerUrl, createWorker, solverTimeoutMs   solver worker (tests)
 *   now          host clock in ms (default performance.now)
 *   randomSeed   () => { lo, hi } game entropy (default crypto.getRandomValues)
 *   log          (stream, text) engine stdout/stderr sink (default console)
 *
 * load()          starts the game instance; the module is fetched and
 *                 compiled the first time and again after a failed start.
 * newGame(cfg)    { width, height, mines } -> GameState of a new READY game.
 * act(req)        { action, row, col, generation, revision } -> { state, changed }
 * autosolve(req)  { generation, revision } -> { state, changed, available }:
 *                 applies the proofs C accepted for this revision as one
 *                 atomic batch (available false: no accepted result yet).
 * state()         current GameState.
 * odds(req)       { generation, revision } -> Promise of the odds payload.
 * recalculateOdds(req)  as odds(), but a playing position is solved anew even
 *                 when C holds a reusable answer for it (a recovery the
 *                 player asks for; never automatic).
 * cancelOdds()    abandons the odds request in flight (terminates the worker).
 * recommendation(req)  { generation, revision } -> Promise of the move
 *                 advice for that position (wasm-host.js decodePlan shape):
 *                 planned in the solver worker from a snapshot of the public
 *                 observation, after any odds solve in flight, and shown only
 *                 once C's ms_check_plan binds it to the current position.
 *                 A plan already checked for this game is reused, after the
 *                 same check, while the observation is unchanged (flags are
 *                 not part of it), unless it is unavailable: those are
 *                 planned anew. Advice is never played or cached as odds.
 * cancelRecommendation()  abandons the advice request in flight.
 * restart()       replaces a failed game instance (the old game is lost),
 *                 reusing the module that started before; after a failed
 *                 start, load()/restart() fetch and compile the file again.
 * dispose()       shuts the client down for good, including loads in flight.
 *
 * A trap in the game instance is fatal for that instance: the call throws
 * kind 'trap', status becomes 'failed' and every later call throws the same
 * error until restart(). Solver failures never touch the game instance.
 */
export class EngineClient {
  constructor(options = {}) {
    this.options = options;
    this.status = 'idle'; // idle | loading | ready | failed | disposed
    this.failure = null;
    // The module instances start from. An injected one (tests) is always
    // used as given; one compiled from the URL is kept only once an instance
    // started from it, so a retry after a failed start refetches the file
    // while game restarts and solver workers reuse a module that works.
    this.module = options.module || null;
    this.injected = !!options.module;
    this.engine = null;
    this.instances = 0;
    this.idPrefix = '';
    this.loading = null;
    this.epoch = 0; // bumped by dispose(): older loads never complete into it
    this.solver = null;
    this.job = null; // the odds request in flight: { generation, revision, promise }
    this.planJob = null; // the advice request in flight: { generation, revision, promise }
    // The latest plan C accepted for the current game: { generation, bytes,
    // observation }, reused while ms_check_plan still accepts it.
    this.lastPlan = null;
  }

  load() {
    if (this.status === 'disposed') return Promise.reject(disposed());
    if (this.status === 'ready') return Promise.resolve();
    if (this.loading) return this.loading;
    const epoch = this.epoch;
    this.status = 'loading';
    this.failure = null;
    this.loading = this.startInstance(epoch).then((engine) => {
      if (epoch !== this.epoch) throw disposed();
      this.loading = null;
      this.engine = engine;
      this.status = 'ready';
    }, (error) => {
      if (epoch !== this.epoch) throw disposed();
      this.loading = null;
      this.status = 'failed';
      this.failure = error;
      throw error;
    });
    return this.loading;
  }

  restart() {
    if (this.status === 'disposed') return Promise.reject(disposed());
    this.cancelOdds();
    this.cancelRecommendation();
    this.lastPlan = null;
    this.engine = null;
    if (!this.loading) {
      this.status = 'idle';
      this.failure = null;
    }
    return this.load();
  }

  dispose() {
    this.epoch++;
    this.cancelOdds();
    this.cancelRecommendation();
    this.lastPlan = null;
    if (this.solver) this.solver.dispose();
    this.solver = null;
    this.engine = null;
    this.loading = null;
    this.failure = null;
    this.status = 'disposed';
  }

  newGame(config) {
    const engine = this.requireEngine();
    requireRequest(config, ['width', 'height', 'mines'], 'newGame');
    // Python's validate_config order: width, height, then mines. JS rejects
    // values C cannot receive; C checks every domain, so a later field's
    // type error must not hide an earlier field's domain error.
    const width = requireU32(config.width, 'invalid_width');
    const widthOk = width >= GAME_MIN_SIDE && width <= GAME_MAX_SIDE;
    if (!isU32(config.height)) throw inputError(widthOk ? 'invalid_height' : 'invalid_width');
    const height = config.height;
    const heightOk = height >= GAME_MIN_SIDE && height <= GAME_MAX_SIDE;
    if (!isU32(config.mines)) {
      if (!widthOk) throw inputError('invalid_width');
      if (!heightOk) throw inputError('invalid_height');
      throw inputError('invalid_mines', { width: width, height: height });
    }
    const mines = config.mines;
    const seed = (this.options.randomSeed || defaultRandomSeed)();
    if (!seed || !isU32(seed.lo) || !isU32(seed.hi)) throw new Error('randomSeed must return two uint32 words');
    this.guard(() => engine.newGame(width, height, mines, seed.lo, seed.hi), { width: width, height: height, mines: mines });
    // The previous game and any odds or advice request for it are gone.
    this.cancelOdds();
    this.cancelRecommendation();
    this.lastPlan = null;
    return this.state();
  }

  act(request) {
    const engine = this.requireEngine();
    requireRequest(request, ['action', 'row', 'col', 'generation', 'revision'], 'act');
    const action = typeof request.action === 'string' && Object.prototype.hasOwnProperty.call(ACTIONS, request.action)
      ? ACTIONS[request.action] : 0;
    if (!action) throw inputError('invalid_action');
    const row = request.row;
    const col = request.col;
    if (!isSafeInt(row) || !isSafeInt(col)) throw inputError('invalid_coordinates');
    const routing = routingOf(request);
    if (!isU32(row) || !isU32(col) || !Number.isInteger(routing.generation)) {
      // Values C cannot receive, checked in C's order: bounds, then routing.
      const state = this.state();
      if (row < 0 || col < 0 || row >= state.height || col >= state.width) {
        throw inputError('out_of_bounds', { row: row, col: col, width: state.width, height: state.height });
      }
      this.rejectRouting(routing, request);
    }
    const changed = this.guard(() => engine.act(routing.generation, routing.revision, action, row, col),
      { row: row, col: col, revision: routing.revision });
    return this.afterMutation(changed);
  }

  /*
   * One certainty-only batch from the proofs C accepted for this revision.
   * Returns { state, changed, available }: available false means no result
   * has been accepted for the revision yet (nothing changed); changed false
   * with available true is a pause.
   */
  autosolve(request) {
    const engine = this.requireEngine();
    requireRequest(request, ['generation', 'revision'], 'autosolve');
    const routing = routingOf(request);
    if (!Number.isInteger(routing.generation)) this.rejectRouting(routing, request);
    const outcome = this.guard(() => engine.autosolve(routing.generation, routing.revision),
      { revision: routing.revision });
    const after = this.afterMutation(outcome.changed);
    return { state: after.state, changed: outcome.changed, available: outcome.available };
  }

  state() {
    const engine = this.requireEngine();
    const bytes = this.guard(() => engine.view(), null);
    return this.decode(() => decodeView(bytes, { idPrefix: this.idPrefix }));
  }

  /*
   * Odds for the current game at (generation, revision): the cached
   * exact/approximate answer or the placeholder of a ready/finished game
   * comes straight from C; otherwise the public observation is solved in the
   * worker and the answer is accepted by C before it is returned. Concurrent
   * requests for the same revision share one solve. Rejects with kind
   * 'aborted' when the board changes or cancelOdds() is called first.
   */
  async odds(request) {
    return this.requestOdds(request, 'odds', false);
  }

  /*
   * odds() for a position the player wants solved again: a playing position
   * goes to the worker even when C holds a reusable exact or approximate
   * answer for it, and the new answer, once C accepts it, replaces the stored
   * one whatever its status (C's rule for every accepted result). Ready and
   * finished games still get their placeholders, and a solve already in
   * flight for the position is shared. Same validation and errors as odds().
   * For explicit recovery only, e.g. when the move advisor found a certainty
   * the cached sampled odds could not prove; nothing calls it automatically.
   */
  async recalculateOdds(request) {
    return this.requestOdds(request, 'recalculateOdds', true);
  }

  async requestOdds(request, name, fresh) {
    const engine = this.requireEngine();
    requireRequest(request, ['generation', 'revision'], name);
    const routing = routingOf(request);
    if (!Number.isInteger(routing.generation)) this.rejectRouting(routing, request);
    const job = this.job;
    if (job && job.generation === routing.generation && job.revision === routing.revision) return job.promise;
    this.cancelOdds();
    const cached = this.guard(() => engine.cachedResult(routing.generation, routing.revision), routing);
    const status = cached ? readResultHeader(cached).status : 0;
    // Unavailable answers are never reused for display: a retry recomputes.
    // A fresh request takes only the placeholders of games without odds.
    if (cached && status !== RESULT_STATUS_UNAVAILABLE && (!fresh || status >= RESULT_STATUS_NOT_STARTED)) {
      return this.servedOdds(cached, routing);
    }
    const observation = this.guard(() => engine.observe(routing.generation, routing.revision), routing);
    const header = this.decode(() => readObservationHeader(observation));
    // Odds come first: advice still being planned is abandoned (the page asks
    // again once these odds are shown).
    this.cancelRecommendation();
    const next = { generation: routing.generation, revision: routing.revision, promise: null };
    next.promise = this.solverHost().solve({
      generation: routing.generation,
      revision: routing.revision,
      observation: observation
    }).then((bytes) => {
      if (this.job !== next) throw aborted();
      this.job = null;
      return this.acceptOdds(bytes, header, routing);
    }, (error) => {
      if (this.job === next) this.job = null;
      throw error;
    });
    // Callers may drop an obsolete request; its cancellation is not an error.
    next.promise.catch(() => {});
    this.job = next;
    return next.promise;
  }

  cancelOdds() {
    this.job = null;
    if (this.solver) this.solver.cancel('solve');
  }

  /*
   * Move advice for the current game at (generation, revision): the
   * planner's suggestion for the next reveal, its estimated or exact chances,
   * or why there is none (status 'none' / 'unavailable'). Waits for an odds
   * solve in flight, snapshots the public observation (C refuses stale,
   * not-started and finished positions first), plans it in the worker and
   * returns the answer only if it is still the current position, decodes
   * strictly and passes ms_check_plan. Concurrent requests for the same
   * position share one plan. Rejects with kind 'aborted' when the board
   * changes, odds are requested or cancelRecommendation() is called first,
   * and with kind 'worker' for worker failures and refused plans (the game
   * and its odds are never touched).
   */
  async recommendation(request) {
    this.requireEngine();
    requireRequest(request, ['generation', 'revision'], 'recommendation');
    const routing = routingOf(request);
    if (!Number.isInteger(routing.generation)) this.rejectRouting(routing, request);
    const job = this.planJob;
    if (job && job.generation === routing.generation && job.revision === routing.revision) return job.promise;
    this.cancelRecommendation();
    const reused = this.reusePlan(routing);
    if (reused) return reused;
    const next = { generation: routing.generation, revision: routing.revision, promise: null };
    this.planJob = next;
    next.promise = this.plan(next, routing).then((payload) => {
      if (this.planJob === next) this.planJob = null;
      return payload;
    }, (error) => {
      if (this.planJob === next) this.planJob = null;
      throw error;
    });
    // Callers may drop an obsolete request; its cancellation is not an error.
    next.promise.catch(() => {});
    return next.promise;
  }

  cancelRecommendation() {
    const job = this.planJob;
    this.planJob = null;
    if (job && this.solver) this.solver.cancel('plan');
  }

  get solverStats() {
    return this.solver ? this.solver.stats : null;
  }

  // ---------------------------------------------------------------- private

  // Starts a game instance for load(); `epoch` detects a dispose() meanwhile.
  async startInstance(epoch) {
    const module = this.module || await compileEngineModule(this.options.wasmUrl || ENGINE_WASM_URL);
    if (epoch !== this.epoch) throw disposed();
    let engine;
    try {
      engine = await instantiateGameEngine(module, {
        label: 'minesweeper',
        now: this.options.now,
        log: this.options.log
      });
    } catch (error) {
      // A fetched module that cannot start (ABI or name-table mismatch,
      // failed ms_init or instantiation) is dropped: the next explicit load
      // refetches and recompiles the artifact.
      if (!this.injected && this.module === module) this.module = null;
      throw error;
    }
    if (epoch !== this.epoch) throw disposed();
    if (module !== this.module) {
      // A newly fetched module: solver workers must start from it as well.
      if (this.solver) this.solver.dispose();
      this.solver = null;
      this.module = module;
    }
    this.instances++;
    this.idPrefix = 'local-' + this.instances + '-';
    return engine;
  }

  requireEngine() {
    if (this.status === 'ready' && this.engine) return this.engine;
    if (this.status === 'failed' && this.failure) throw this.failure;
    if (this.status === 'disposed') throw disposed();
    throw new EngineError('engine_not_ready', 'The game engine is still loading.', { kind: 'load' });
  }

  // Throws the error C would report for routing numbers it cannot receive.
  rejectRouting(routing, request) {
    if (routing.invalid) throw inputError('invalid_revision');
    const state = this.state();
    throw new EngineError('stale_revision', statusMessage('stale_revision', {
      state: state,
      revision: request.revision
    }), { status: null, kind: 'conflict', state: state });
  }

  // Runs one engine call; C statuses come back as EngineError (with the
  // latest state attached to conflicts), a trap fails the instance.
  guard(call, context) {
    try {
      return call();
    } catch (error) {
      if (!(error instanceof EngineError)) throw error;
      if (error.kind === 'trap') {
        this.status = 'failed';
        this.failure = error;
        this.cancelOdds();
        this.cancelRecommendation();
        throw error;
      }
      if (error.kind === 'conflict' && context) {
        const state = this.state();
        throw new EngineError(error.code, statusMessage(error.code, Object.assign({}, context, { state: state })), {
          status: error.status,
          kind: 'conflict',
          state: state
        });
      }
      if (error.status !== null && context) {
        if (error.code === 'out_of_bounds') {
          const state = this.state();
          context = Object.assign({}, context, { width: state.width, height: state.height });
        }
        throw new EngineError(error.code, statusMessage(error.code, context), {
          status: error.status,
          kind: error.kind
        });
      }
      throw error;
    }
  }

  // A decoding failure means the instance broke its own contract: fatal.
  decode(read) {
    try {
      return read();
    } catch (error) {
      if (!(error instanceof EngineError)) throw error;
      this.status = 'failed';
      this.failure = error;
      this.cancelOdds();
      this.cancelRecommendation();
      throw error;
    }
  }

  afterMutation(changed) {
    const state = this.state();
    if (changed) {
      const job = this.job;
      if (job && (job.generation !== state.generation || job.revision !== state.revision)) this.cancelOdds();
      const plan = this.planJob;
      if (plan && (plan.generation !== state.generation || plan.revision !== state.revision)) {
        this.cancelRecommendation();
      }
    }
    return { state: state, changed: changed };
  }

  // The advice job: after any odds solve in flight, a snapshot of the
  // public observation goes to the worker as a 'plan' request.
  async plan(next, routing) {
    while (this.job) {
      await this.job.promise.catch(() => {});
      if (this.planJob !== next) throw planAborted();
    }
    if (this.planJob !== next) throw planAborted();
    const engine = this.requireEngine();
    const observation = this.guard(() => engine.observe(routing.generation, routing.revision), routing);
    this.decode(() => readObservationHeader(observation));
    const snapshot = observation.slice(); // the worker receives (and detaches) its own copy
    const bytes = await this.solverHost().solve({
      kind: 'plan',
      generation: routing.generation,
      revision: routing.revision,
      observation: observation
    });
    if (this.planJob !== next) throw planAborted();
    return this.acceptPlan(bytes, snapshot, routing);
  }

  /*
   * Worker plan bytes are decoded strictly against the observation they
   * answer, then C's ms_check_plan binds them to the current position
   * (observation hash, hidden cell); only then may the page show them. A
   * refusal fails this advice request only (kind 'worker', the worker is
   * recycled); nothing reaches the game, its odds cache or autosolve.
   */
  acceptPlan(bytes, observation, routing) {
    const engine = this.requireEngine();
    const state = this.currentPlanState(routing);
    let payload;
    try {
      payload = this.planPayload(bytes, observation, state);
    } catch (error) {
      if (!(error instanceof EngineError)) throw error;
      throw this.rejectWorkerPlan(error);
    }
    try {
      this.guard(() => engine.checkPlan(routing.generation, routing.revision, bytes), routing);
    } catch (error) {
      if (error instanceof EngineError && (error.code === 'invalid_result' || error.code === 'invalid_buffer')) {
        throw this.rejectWorkerPlan(error);
      }
      throw error;
    }
    // Unavailable plans are budget or evidence shortfalls: never reused, so
    // asking again plans anew (as unavailable odds are solved anew).
    this.lastPlan = payload.status === 'unavailable' ? null
      : { generation: routing.generation, bytes: bytes, observation: observation };
    return payload;
  }

  /*
   * The last accepted plan of this game, when C still accepts it for
   * (generation, revision): the public observation is unchanged (the same
   * position, or flag-only revisions since). Null when there is none or C
   * refuses it (invalid_result: the position changed). Conflicts (stale,
   * finished) are thrown like any request's.
   */
  reusePlan(routing) {
    const last = this.lastPlan;
    if (!last || last.generation !== routing.generation) return null;
    const engine = this.requireEngine();
    try {
      this.guard(() => engine.checkPlan(routing.generation, routing.revision, last.bytes), routing);
    } catch (error) {
      if (error instanceof EngineError && error.code === 'invalid_result') {
        this.lastPlan = null;
        return null;
      }
      throw error;
    }
    const state = this.currentPlanState(routing);
    try {
      return this.planPayload(last.bytes, last.observation, state);
    } catch (error) {
      if (!(error instanceof EngineError)) throw error;
      this.lastPlan = null;
      return null;
    }
  }

  rejectWorkerPlan(cause) {
    if (this.solver) this.solver.recycle();
    return new EngineError('invalid_plan', 'The move advisor returned a suggestion that does not fit this board, ' +
      'so it was not used.', { status: cause.status, kind: 'worker', cause: cause });
  }

  // The current state, which must still be the position being advised.
  currentPlanState(routing) {
    const state = this.state();
    if (state.generation !== routing.generation || state.revision !== routing.revision) throw planAborted();
    return state;
  }

  planPayload(bytes, observation, state) {
    return decodePlan(bytes, observation, {
      gameId: state.id,
      generation: state.generation,
      revision: state.revision
    });
  }

  solverHost() {
    if (!this.solver) {
      this.solver = new SolverHost({
        module: this.module,
        workerUrl: this.options.workerUrl,
        createWorker: this.options.createWorker,
        timeoutMs: this.options.solverTimeoutMs
      });
    }
    return this.solver;
  }

  /*
   * Worker bytes are checked and decoded before C sees them, then C's
   * acceptance (observation hash, revision, result contract) decides; only
   * accepted bytes reach C's cache and the page. Every failure here is the
   * worker's: the odds request fails with kind 'worker', the worker is
   * recycled for the next request and the game instance is untouched.
   */
  acceptOdds(bytes, observation, routing) {
    const engine = this.requireEngine();
    const state = this.currentState(routing);
    let payload;
    try {
      const header = readResultHeader(bytes);
      if (bytes.length !== resultSize(observation.width, observation.height) ||
          header.width !== observation.width || header.height !== observation.height ||
          header.totalMines !== observation.totalMines || header.revealed !== observation.revealed) {
        throw new EngineError('invalid_result', statusMessage('invalid_result'), { kind: 'worker' });
      }
      payload = this.oddsPayload(bytes, state);
    } catch (error) {
      if (!(error instanceof EngineError)) throw error;
      throw this.rejectWorkerResult(error, 'invalid_result');
    }
    try {
      this.guard(() => engine.acceptResult(routing.generation, routing.revision, bytes), routing);
    } catch (error) {
      if (error instanceof EngineError && (error.code === 'invalid_result' || error.code === 'invalid_buffer')) {
        throw this.rejectWorkerResult(error, error.code);
      }
      throw error;
    }
    return payload;
  }

  rejectWorkerResult(cause, code) {
    if (this.solver) this.solver.recycle();
    return new EngineError(code, statusMessage('invalid_result'), {
      status: cause.status,
      kind: 'worker',
      cause: cause
    });
  }

  // A result C serves itself (a cached answer or a placeholder). A decoding
  // failure fails this odds request only; the game stays playable.
  servedOdds(bytes, routing) {
    const state = this.currentState(routing);
    try {
      return this.oddsPayload(bytes, state);
    } catch (error) {
      if (!(error instanceof EngineError)) throw error;
      throw new EngineError('invalid_result', 'The engine served odds this page cannot read, so none are shown.', {
        kind: 'internal',
        cause: error
      });
    }
  }

  // The current state, which must still be the requested position.
  currentState(routing) {
    const state = this.state();
    if (state.generation !== routing.generation || state.revision !== routing.revision) throw aborted();
    return state;
  }

  oddsPayload(bytes, state) {
    return decodeResult(bytes, {
      gameId: state.id,
      generation: state.generation,
      revision: state.revision,
      gameStatus: state.status
    });
  }
}

export { EngineError, GENERATION_MAX, REVISION_MAX };
