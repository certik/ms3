// Worker lifecycle tests for SolverHost (static/engine-client.js) with an
// in-memory worker double: request ids, cancellation by termination, late and
// malformed messages, startup failures, traps and timeouts. The real worker
// and engine are covered by engine-client.test.mjs and the browser tests.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fakeWorkerFactory, flush, importSite } from './support/site.mjs';

const { SolverHost } = await importSite('engine-client.js');
const { EngineError } = await importSite('wasm-host.js');

const MODULE = { compiled: 'module stand-in' };

function observation() {
  return new Uint8Array(40);
}

function setup(options = {}) {
  const factory = fakeWorkerFactory();
  const host = new SolverHost(Object.assign({ module: MODULE, workerUrl: 'probability-worker.js', createWorker: factory },
    options));
  return { host, factory };
}

function resultFor(message, length = 160) {
  return { type: 'result', id: message.id, generation: message.generation, revision: message.revision,
    result: new ArrayBuffer(length) };
}

async function rejection(promise) {
  try {
    await promise;
  } catch (error) {
    return error;
  }
  assert.fail('expected the promise to reject');
}

async function started(host, factory, request = { generation: 1, revision: 0 }) {
  const promise = host.solve(Object.assign({ observation: observation() }, request));
  const worker = factory.created[factory.created.length - 1];
  worker.reply({ type: 'ready' });
  await flush();
  return { promise, worker, message: worker.last('solve') };
}

test('starts a worker with the cached module, then sends one copied public observation', async () => {
  const { host, factory } = setup();
  const obs = observation();
  const promise = host.solve({ generation: 3, revision: 7, observation: obs });
  assert.equal(factory.created.length, 1);
  const worker = factory.created[0];
  assert.equal(worker.url, 'probability-worker.js');
  assert.deepEqual(worker.posted, [{ type: 'init', module: MODULE }]);
  worker.reply({ type: 'ready' });
  await flush();
  const message = worker.last('solve');
  assert.deepEqual(Object.keys(message), ['type', 'id', 'generation', 'revision', 'observation']);
  assert.equal(message.generation, 3);
  assert.equal(message.revision, 7);
  assert.ok(message.observation instanceof ArrayBuffer, 'an ArrayBuffer copy, not a view of WASM memory');
  assert.equal(message.observation, obs.buffer);
  assert.deepEqual(worker.transfers[1], [obs.buffer]);
  worker.reply(resultFor(message));
  const bytes = await promise;
  assert.ok(bytes instanceof Uint8Array);
  assert.equal(bytes.length, 160);
  assert.equal(host.busy, false);
  assert.deepEqual(host.stats, { workersCreated: 1, workersTerminated: 0, solvesSent: 1, lateMessages: 0 });
});

test('a sub-view observation is copied before it is transferred', async () => {
  const { host, factory } = setup();
  const backing = new Uint8Array(64);
  const view = backing.subarray(8, 48);
  const pending = host.solve({ generation: 1, revision: 0, observation: view });
  factory.created[0].reply({ type: 'ready' });
  await flush();
  const sent = factory.created[0].last('solve').observation;
  assert.notEqual(sent, backing.buffer);
  assert.equal(sent.byteLength, 40);
  host.dispose();
  assert.equal((await rejection(pending)).kind, 'aborted');
});

test('a ready worker is reused and request ids increase', async () => {
  const { host, factory } = setup();
  const first = await started(host, factory);
  first.worker.reply(resultFor(first.message));
  await first.promise;
  const second = host.solve({ generation: 1, revision: 1, observation: observation() });
  await flush();
  assert.equal(factory.created.length, 1);
  const message = first.worker.last('solve');
  assert.equal(message.id, first.message.id + 1);
  first.worker.reply(resultFor(message));
  await second;
  assert.throws(() => {
    host.solve({ generation: 1, revision: 2, observation: observation() }).catch(() => {});
    host.solve({ generation: 1, revision: 2, observation: observation() });
  }, /already running/);
  host.dispose();
});

test('cancel terminates a busy worker; its late answer is ignored', async () => {
  const { host, factory } = setup();
  const { promise, worker, message } = await started(host, factory);
  assert.equal(host.cancel(), true);
  const error = await rejection(promise);
  assert.ok(error instanceof EngineError);
  assert.equal(error.kind, 'aborted');
  assert.equal(worker.terminated, true);
  worker.reply(resultFor(message)); // the host no longer listens to it
  worker.replyStale(resultFor(message)); // already queued before termination
  worker.crashStale('late crash');
  assert.equal(host.stats.lateMessages, 2);
  assert.equal(host.busy, false);
  const next = host.solve({ generation: 1, revision: 1, observation: observation() });
  assert.equal(factory.created.length, 2, 'a fresh worker replaces the terminated one');
  factory.created[1].reply({ type: 'ready' });
  await flush();
  factory.created[1].reply(resultFor(factory.created[1].last('solve')));
  assert.equal((await next).length, 160);
  assert.equal(host.cancel(), false, 'nothing to cancel');
  assert.equal(factory.created[1].terminated, false, 'an idle worker is kept');
});

test('cancel during startup terminates the starting worker', async () => {
  const { host, factory } = setup();
  const promise = host.solve({ generation: 1, revision: 0, observation: observation() });
  host.cancel();
  assert.equal((await rejection(promise)).kind, 'aborted');
  factory.created[0].reply({ type: 'ready' });
  assert.equal(factory.created[0].terminated, true);
  assert.equal(factory.created[0].posted.length, 1, 'no solve was sent');
});

test('startup failures are explicit and the next request starts a new worker', async () => {
  const { host, factory } = setup();
  const failed = host.solve({ generation: 1, revision: 0, observation: observation() });
  factory.created[0].reply({ type: 'init-failed', error: { code: 'engine_load_failed', status: null,
    message: 'no memory', fatal: true } });
  const error = await rejection(failed);
  assert.equal(error.code, 'solver_unavailable');
  assert.equal(error.kind, 'worker');
  assert.match(error.message, /could not start: no memory/);
  assert.equal(factory.created[0].terminated, true);

  const crashed = host.solve({ generation: 1, revision: 0, observation: observation() });
  factory.created[1].crash('SyntaxError: Unexpected token');
  const loadError = await rejection(crashed);
  assert.equal(loadError.code, 'solver_unavailable');
  assert.match(loadError.message, /Unexpected token/);
  assert.equal(factory.created.length, 2);
});

test('a worker that cannot be created or sent the module fails explicitly', async () => {
  const throwing = new SolverHost({ module: MODULE, createWorker: () => {
    throw new Error('blocked by policy');
  } });
  const error = await rejection(throwing.solve({ generation: 1, revision: 0, observation: observation() }));
  assert.equal(error.code, 'solver_unavailable');
  assert.match(error.message, /blocked by policy/);

  const factory = fakeWorkerFactory();
  const host = new SolverHost({
    module: MODULE,
    createWorker: (url) => {
      const worker = factory(url);
      worker.postMessage = () => {
        throw new DOMException('could not clone', 'DataCloneError');
      };
      return worker;
    }
  });
  const cloneError = await rejection(host.solve({ generation: 1, revision: 0, observation: observation() }));
  assert.equal(cloneError.code, 'solver_unavailable');
  assert.match(cloneError.message, /could not clone/);
  assert.equal(factory.created[0].terminated, true);
});

// Settles with the promise, or rejects once `ms` elapse without that.
function within(promise, ms) {
  let timer = 0;
  const late = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(`still pending after ${ms} ms`)), ms);
  });
  return Promise.race([promise, late]).finally(() => clearTimeout(timer));
}

test('a solve request that cannot be posted fails at once; a retry starts a fresh worker', async () => {
  const unhandled = [];
  const onUnhandled = (reason) => unhandled.push(reason);
  process.on('unhandledRejection', onUnhandled);
  try {
    const factory = fakeWorkerFactory();
    let breakSolves = true;
    const host = new SolverHost({
      module: MODULE,
      createWorker: (url) => {
        const worker = factory(url);
        if (breakSolves) {
          const post = worker.postMessage.bind(worker);
          worker.postMessage = (message, transfer) => {
            if (message.type === 'solve') throw new DOMException('could not clone the observation', 'DataCloneError');
            return post(message, transfer);
          };
        }
        return worker;
      }
    });
    const first = host.solve({ generation: 1, revision: 0, observation: observation() });
    factory.created[0].reply({ type: 'ready' }); // startup itself succeeds
    const error = await within(rejection(first), 1000); // not after the 60 s watchdog
    assert.ok(error instanceof EngineError);
    assert.equal(error.code, 'solver_unavailable');
    assert.equal(error.kind, 'worker');
    assert.match(error.message, /could not receive the position: DataCloneError: could not clone the observation/);
    assert.equal(factory.created[0].terminated, true, 'the worker is discarded');
    assert.equal(host.busy, false);
    assert.equal(host.stats.solvesSent, 0);

    breakSolves = false;
    const retry = host.solve({ generation: 1, revision: 0, observation: observation() });
    assert.equal(factory.created.length, 2, 'the retry recreates the worker');
    factory.created[1].reply({ type: 'ready' });
    await flush();
    factory.created[1].reply(resultFor(factory.created[1].last('solve')));
    assert.equal((await retry).length, 160);
    assert.equal(host.stats.solvesSent, 1);
    await flush();
    assert.deepEqual(unhandled, [], 'no rejection escapes unhandled');
  } finally {
    process.off('unhandledRejection', onUnhandled);
  }
});

test('a crash during a solve fails the job and discards the worker', async () => {
  const { host, factory } = setup();
  const { promise, worker } = await started(host, factory);
  worker.crash('RuntimeError: unreachable');
  const error = await rejection(promise);
  assert.equal(error.code, 'solver_crashed');
  assert.equal(error.kind, 'worker');
  assert.equal(worker.terminated, true);
});

test('fatal error replies discard the worker, non-fatal ones keep it', async () => {
  const { host, factory } = setup();
  const first = await started(host, factory);
  first.worker.reply({ type: 'error', id: first.message.id, generation: 1, revision: 0,
    error: { code: 'inconsistent_observation', status: 48, message: 'no layout', fatal: false } });
  const soft = await rejection(first.promise);
  assert.equal(soft.code, 'inconsistent_observation');
  assert.equal(soft.status, 48);
  assert.equal(soft.kind, 'worker');
  assert.equal(first.worker.terminated, false);

  const second = host.solve({ generation: 1, revision: 0, observation: observation() });
  await flush();
  const message = first.worker.last('solve');
  first.worker.reply({ type: 'error', id: message.id, generation: 1, revision: 0,
    error: { code: 'trap', status: null, message: 'The odds calculator stopped unexpectedly', fatal: true } });
  const hard = await rejection(second);
  assert.equal(hard.code, 'trap');
  assert.equal(hard.kind, 'worker', 'a solver trap never fails the game instance');
  assert.equal(first.worker.terminated, true);
  host.solve({ generation: 1, revision: 0, observation: observation() }).catch(() => {});
  assert.equal(factory.created.length, 2);
  host.dispose();
});

test('answers for other requests or positions are protocol violations', async () => {
  for (const change of [{ id: 99 }, { generation: 2 }, { revision: 5 }]) {
    const { host, factory } = setup();
    const { promise, worker, message } = await started(host, factory, { generation: 1, revision: 4 });
    worker.reply(Object.assign(resultFor(message), change));
    const error = await rejection(promise);
    assert.equal(error.code, 'invalid_message', JSON.stringify(change));
    assert.equal(worker.terminated, true);
  }
});

test('malformed or unexpected worker messages fail the job', async () => {
  const malformed = [
    null, 'result', {}, { type: 'result' }, { type: 'ready' },
    { type: 'result', id: 1, generation: 1, revision: 0, result: new Uint8Array(160) },
    { type: 'result', id: 1, generation: 1, revision: 0, result: new ArrayBuffer(160), meta: {} },
    { type: 'error', id: 1, generation: 1, revision: 0, error: { code: 'x', message: 'm' } }
  ];
  for (const data of malformed) {
    const { host, factory } = setup();
    const { promise, worker } = await started(host, factory);
    worker.reply(data);
    const error = await rejection(promise);
    assert.equal(error.code, 'invalid_message', JSON.stringify(data));
    assert.equal(error.kind, 'worker');
    assert.equal(worker.terminated, true);
  }
  const { host, factory } = setup();
  const { promise, worker } = await started(host, factory);
  worker.onmessageerror({});
  assert.equal((await rejection(promise)).code, 'invalid_message');
});

test('a hung solver times out and is terminated', async () => {
  const { host, factory } = setup({ timeoutMs: 30 });
  const { promise, worker, message } = await started(host, factory);
  const error = await rejection(promise);
  assert.equal(error.code, 'solver_timeout');
  assert.equal(error.kind, 'worker');
  assert.equal(worker.terminated, true);
  worker.replyStale(resultFor(message));
  assert.equal(host.stats.lateMessages, 1);
});

test('a worker that never starts also times out', async () => {
  const { host, factory } = setup({ timeoutMs: 30 });
  const error = await rejection(host.solve({ generation: 1, revision: 0, observation: observation() }));
  assert.equal(error.code, 'solver_timeout');
  assert.equal(factory.created[0].terminated, true);
});
