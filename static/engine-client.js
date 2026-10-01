/*
 * engine-client.js - the page's local game engine (replaces the HTTP API).
 *
 * One compiled WebAssembly module, two independent single-threaded instances:
 *
 *   - the game instance runs here, on the main thread. It owns the
 *     authoritative C game, input validation, public observations, the
 *     revision-bound odds cache and atomic autosolve batches. Its calls are
 *     small and synchronous.
 *   - the solver instance runs C ms_solve in a dedicated worker
 *     (probability-worker.js). It only ever receives a copied public
 *     observation plus routing numbers: never flags, the hidden board, the
 *     game's seed or its memory.
 *
 * A synchronous WASM call cannot be interrupted by a message, so obsolete
 * inference is cancelled by terminating the worker; the next request starts
 * a new one from the cached WebAssembly.Module. Every request carries the
 * game generation, revision and a request id; answers that do not match the
 * current request are ignored, and C validates each result again against a
 * freshly built observation before accepting its proofs.
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

// A hang detector only: the solver's own time budget is 1.5 seconds.
export const SOLVER_TIMEOUT_MS = 60000;

const RESULT_STATUS_UNAVAILABLE = 3;

function aborted() {
  return new EngineError('aborted', 'The odds request was cancelled because the board changed.', { kind: 'aborted' });
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
 *               sends one solve request and resolves with the result bytes
 *               (a transferred ArrayBuffer wrapped in a Uint8Array).
 *   cancel()    rejects the job with an `aborted` error and terminates the
 *               worker if it is starting or solving; an idle ready worker is
 *               kept for the next request.
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
    this.stats = { workersCreated: 0, workersTerminated: 0, solvesSent: 0, lateMessages: 0 };
  }

  get busy() {
    return this.job !== null;
  }

  solve(request) {
    if (this.job) throw new Error('SolverHost.solve: a job is already running');
    const job = {
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
    job.timer = setTimeout(() => {
      if (this.job !== job) return;
      this.fail(job, workerFailure('solver_timeout',
        'The odds calculator did not finish within ' + Math.round(this.timeoutMs / 1000) + ' seconds.'));
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
          type: 'solve',
          id: job.id,
          generation: job.generation,
          revision: job.revision,
          observation: buffer
        }, [buffer]);
      } catch (error) {
        this.fail(job, workerFailure('solver_unavailable',
          'The odds calculator could not receive the position: ' + describeThrown(error) + '.', error));
        return;
      }
      this.stats.solvesSent++;
    }, (error) => {
      if (this.job === job) this.fail(job, error);
    });
    return job.promise;
  }

  cancel() {
    const job = this.job;
    if (!job) return false;
    this.finish(job);
    this.discardWorker();
    job.reject(aborted());
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
        message.generation !== job.generation || message.revision !== job.revision) {
      // A live worker only ever answers the job it was given.
      this.onWorkerError(worker, settle, workerFailure('invalid_message',
        'The odds calculator answered a request it was not given.'));
      return;
    }
    this.finish(job);
    if (message.type === 'result') {
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
 * cancelOdds()    abandons the odds request in flight (terminates the worker).
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
    // The previous game and any odds request for it are gone.
    this.cancelOdds();
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
    const engine = this.requireEngine();
    requireRequest(request, ['generation', 'revision'], 'odds');
    const routing = routingOf(request);
    if (!Number.isInteger(routing.generation)) this.rejectRouting(routing, request);
    const job = this.job;
    if (job && job.generation === routing.generation && job.revision === routing.revision) return job.promise;
    this.cancelOdds();
    const cached = this.guard(() => engine.cachedResult(routing.generation, routing.revision), routing);
    // Unavailable answers are never reused for display: a retry recomputes.
    if (cached && readResultHeader(cached).status !== RESULT_STATUS_UNAVAILABLE) {
      return this.servedOdds(cached, routing);
    }
    const observation = this.guard(() => engine.observe(routing.generation, routing.revision), routing);
    const header = this.decode(() => readObservationHeader(observation));
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
    if (this.solver) this.solver.cancel();
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
      throw error;
    }
  }

  afterMutation(changed) {
    const state = this.state();
    if (changed) {
      const job = this.job;
      if (job && (job.generation !== state.generation || job.revision !== state.revision)) this.cancelOdds();
    }
    return { state: state, changed: changed };
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
