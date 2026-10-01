/*
 * probability-worker.js - the isolated odds calculator (a dedicated module
 * worker created by engine-client.js).
 *
 * It owns a second, independent instance of the engine module and runs only
 * C ms_solve_observation on public observations: the revealed clues and the
 * mine total, copied out of the game instance. It never sees flags, the
 * hidden layout, the game's seed or the game instance's memory, and it keeps
 * no state between requests besides its WebAssembly instance.
 *
 * Protocol (wasm-host.js, "worker protocol"):
 *   <- { type: 'init', module }   the compiled WebAssembly.Module, once
 *   -> { type: 'ready' } | { type: 'init-failed', error }
 *   <- { type: 'solve', id, generation, revision, observation: ArrayBuffer }
 *   -> { type: 'result', id, generation, revision, result: ArrayBuffer }
 *    | { type: 'error', id, generation, revision, error }
 * Requests are handled one at a time and synchronously; the page cancels an
 * obsolete solve by terminating this worker. After a trap the instance is
 * unusable: the error is reported as fatal and the worker closes itself.
 */

import { EngineError, checkSolveRequest, errorPayload, instantiateSolverEngine } from './wasm-host.js';

let phase = 'new'; // new | starting | ready | failed
let solver = null;

function isInitMessage(message) {
  return !!message && typeof message === 'object' && !Array.isArray(message) &&
    Object.keys(message).length === 2 && message.type === 'init' && message.module instanceof WebAssembly.Module;
}

function fail(error) {
  phase = 'failed';
  self.postMessage({ type: 'init-failed', error: errorPayload(error, true) });
  self.close();
}

function start(module) {
  phase = 'starting';
  instantiateSolverEngine(module, { label: 'minesweeper-odds' }).then((engine) => {
    solver = engine;
    phase = 'ready';
    self.postMessage({ type: 'ready' });
  }, fail);
}

// Routing numbers of a message, when they are well formed enough to answer.
function routingOf(message) {
  if (!message || typeof message !== 'object') return null;
  const { id, generation, revision } = message;
  const ok = [id, generation, revision].every((value) => Number.isInteger(value) && value >= 0 && value <= 0x7FFFFFFF);
  return ok && id >= 1 && generation >= 1 ? { id: id, generation: generation, revision: revision } : null;
}

function solve(message) {
  let request;
  try {
    request = checkSolveRequest(message);
  } catch (error) {
    if (!(error instanceof EngineError)) throw error;
    const routing = routingOf(message);
    if (!routing) throw error; // unanswerable: surfaces as a worker error event
    self.postMessage({
      type: 'error',
      id: routing.id,
      generation: routing.generation,
      revision: routing.revision,
      error: errorPayload(error, false)
    });
    return;
  }
  const reply = { id: request.id, generation: request.generation, revision: request.revision };
  let result;
  try {
    result = solver.solve(request.observation);
  } catch (error) {
    const fatal = !(error instanceof EngineError) || error.kind === 'trap' || error.kind === 'internal';
    self.postMessage(Object.assign({ type: 'error' }, reply, { error: errorPayload(error, fatal) }));
    if (fatal) {
      phase = 'failed';
      self.close();
    }
    return;
  }
  const buffer = result.buffer;
  self.postMessage(Object.assign({ type: 'result' }, reply, { result: buffer }), [buffer]);
}

self.addEventListener('message', (event) => {
  const message = event.data;
  if (phase === 'new') {
    if (!isInitMessage(message)) {
      fail(new EngineError('invalid_message', 'The odds calculator expected its engine module first.', {
        kind: 'protocol'
      }));
      return;
    }
    start(message.module);
    return;
  }
  if (phase !== 'ready') {
    throw new Error('probability-worker: message received while ' + phase);
  }
  solve(message);
});

self.addEventListener('messageerror', () => {
  if (phase === 'new') {
    fail(new EngineError('invalid_message', 'The odds calculator could not read its engine module.', {
      kind: 'protocol'
    }));
    return;
  }
  throw new Error('probability-worker: a message could not be deserialized');
});
