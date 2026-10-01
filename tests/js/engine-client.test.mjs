// EngineClient tests against the real engine (static/engine-client.js,
// static/wasm-host.js, static/probability-worker.js and the production
// reactor): argument validation before marshalling, GameState
// reconstruction, generation/revision guards, conflicts, memory growth and
// buffer ownership, traps and recovery, and the odds pipeline through the
// real worker module (run on node:worker_threads by support/worker-thread.mjs).
//
// Game rules and probabilities themselves are tested in C (tests/c); these
// tests only check what the JavaScript adapter adds. They need the
// production reactor and fail (never skip) with build instructions without
// it; `npm run test:js:unit` runs the WASM-free tests alone.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createServer } from 'node:http';
import {
  engineModule, fakeWorkerFactory, flush, importSite, nodeWorkerFactory, requireWasm, siteUrl, waitFor
} from './support/site.mjs';
import { forgeResult, wrongAbiModule } from './support/forge.mjs';

requireWasm();
const { EngineClient } = await importSite('engine-client.js');
const host = await importSite('wasm-host.js');
const { EngineError, POOL, STATUS } = host;

const SEED = () => ({ lo: 0x9e3779b9, hi: 0x7f4a7c15 });

async function makeClient(options = {}) {
  const client = new EngineClient(Object.assign({
    module: await engineModule(),
    createWorker: nodeWorkerFactory(),
    randomSeed: SEED,
    log: () => {}
  }, options));
  await client.load();
  return client;
}

async function withClient(options, body) {
  const client = await makeClient(options);
  try {
    await body(client);
  } finally {
    client.dispose();
  }
}

function throwsCode(fn, code, check) {
  assert.throws(fn, (error) => {
    assert.ok(error instanceof EngineError, `expected EngineError, got ${error && error.stack}`);
    assert.equal(error.code, code);
    assert.equal(typeof error.message, 'string');
    assert.ok(error.message.length > 0);
    if (check) check(error);
    return true;
  });
}

async function rejectsCode(promise, code, check) {
  let error = null;
  try {
    await promise;
  } catch (caught) {
    error = caught;
  }
  assert.ok(error instanceof EngineError, `expected an EngineError rejection, got ${error}`);
  assert.equal(error.code, code);
  if (check) check(error);
  return error;
}

function routing(state) {
  return { generation: state.generation, revision: state.revision };
}

// Records every export the client's game instance calls, with arguments.
function spyExports(client) {
  const calls = [];
  const engine = client.engine;
  const call = engine.call.bind(engine);
  engine.call = (name, ...args) => {
    calls.push({ name: name, args: args });
    return call(name, ...args);
  };
  return calls;
}

// Exports that act on a game or a result (views and sizes excluded).
const GAME_EXPORTS = new Set(['ms_new_game', 'ms_act', 'ms_apply_autosolve', 'ms_get_observation',
  'ms_get_cached_result', 'ms_accept_result']);

function reachedGame(calls) {
  return calls.filter((call) => GAME_EXPORTS.has(call.name)).map((call) => call.name);
}

function neighbors(index, width, height) {
  const row = Math.floor(index / width);
  const col = index % width;
  const out = [];
  for (let dr = -1; dr <= 1; dr++) {
    for (let dc = -1; dc <= 1; dc++) {
      const r = row + dr;
      const c = col + dc;
      if ((dr || dc) && r >= 0 && r < height && c >= 0 && c < width) out.push(r * width + c);
    }
  }
  return out;
}

// Plays a game from `first` by revealing the lowest hidden cell until it
// ends and returns the disclosed mine layout. With a fixed randomSeed, the
// next game with the same first reveal has the same layout.
function learnLayout(client, config, first) {
  let state = client.newGame(config);
  state = client.act(Object.assign({ action: 'reveal', row: Math.floor(first / config.width),
    col: first % config.width }, routing(state))).state;
  while (state.status === 'playing') {
    const next = state.cells.findIndex((c) => !c.revealed && !c.flagged);
    state = client.act(Object.assign({ action: 'reveal', row: Math.floor(next / config.width),
      col: next % config.width }, routing(state))).state;
  }
  return new Set(state.cells.flatMap((c, i) => (c.mine ? [i] : [])));
}

/*
 * A solver worker double answering each solve with forgeResult(spec()) built
 * on the real solver's result for the posted observation (same header and
 * observation hash), so the engine's acceptance gate applies in full.
 */
function oracleWorkers(solver, spec) {
  const factory = fakeWorkerFactory();
  const create = (url) => {
    const worker = factory(url);
    const post = worker.postMessage.bind(worker);
    worker.postMessage = (message, transfer) => {
      post(message, transfer);
      setTimeout(() => {
        if (worker.terminated) return;
        if (message.type === 'init') {
          worker.reply({ type: 'ready' });
          return;
        }
        const real = solver.solve(new Uint8Array(message.observation));
        const forged = forgeResult(real.buffer, message.observation, spec());
        worker.reply({ type: 'result', id: message.id, generation: message.generation, revision: message.revision,
          result: forged });
      }, 0);
    };
    return worker;
  };
  create.created = factory.created;
  return create;
}

function reveal(client, state, row, col) {
  return client.act(Object.assign({ action: 'reveal', row: row, col: col }, routing(state)));
}

// Serves the bytes `body()` returns as /minesweeper.wasm and counts requests.
async function serveWasm(body, delayMs = 0) {
  let requests = 0;
  const server = createServer((request, response) => {
    requests++;
    setTimeout(() => {
      const bytes = body();
      response.writeHead(200, { 'Content-Type': 'application/wasm', 'Content-Length': bytes.length });
      response.end(bytes);
    }, delayMs);
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  return {
    url: new URL(`http://127.0.0.1:${server.address().port}/minesweeper.wasm`),
    requests: () => requests,
    close: () => new Promise((resolve) => server.close(resolve))
  };
}

// A 9x9 game with 72 mines: the first reveal opens exactly the 3x3 block
// around (4, 4) and wins; any other first reveal leaves only mines around.
function denseGame(client) {
  return client.newGame({ width: 9, height: 9, mines: 72 });
}

// ------------------------------------------------------------- lifecycle

test('load audits the module, initializes once and owns an independent instance', async () => {
  await withClient({}, async (client) => {
    assert.equal(client.status, 'ready');
    assert.equal(client.engine.status('ms_init'), STATUS.INTERNAL, 'ms_init runs exactly once per instance');
    await withClient({}, async (other) => {
      assert.ok(other.engine.memory !== client.engine.memory, 'each instance has its own memory');
      const a = client.newGame({ width: 9, height: 9, mines: 10 });
      const b = other.newGame({ width: 16, height: 16, mines: 40 });
      assert.equal(a.generation, 1);
      assert.equal(b.generation, 1);
      reveal(client, a, 4, 4);
      assert.equal(other.state().revision, 0, 'instances share nothing');
    });
  });
});

test('calls before load or after a failed load are explicit errors', async () => {
  const idle = new EngineClient({ module: await engineModule() });
  throwsCode(() => idle.state(), 'engine_not_ready', (e) => assert.equal(e.kind, 'load'));
  const broken = new EngineClient({ module: new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0])) });
  await rejectsCode(broken.load(), 'abi_mismatch', (e) => assert.equal(e.kind, 'load'));
  assert.equal(broken.status, 'failed');
  throwsCode(() => broken.newGame({ width: 9, height: 9, mines: 10 }), 'abi_mismatch');
});

test('a start failure drops a fetched module: a retry refetches the fixed artifact, restarts do not', async () => {
  const good = readFileSync(requireWasm());
  let artifact = wrongAbiModule(host.REQUIRED_EXPORTS);
  const server = await serveWasm(() => artifact);
  const client = new EngineClient({ wasmUrl: server.url, createWorker: nodeWorkerFactory(), randomSeed: SEED,
    log: () => {} });
  try {
    await rejectsCode(client.load(), 'abi_mismatch', (e) => assert.equal(e.kind, 'load'));
    assert.equal(client.status, 'failed');
    assert.ok(client.module === null, 'the module that failed to start is not kept');
    await rejectsCode(client.restart(), 'abi_mismatch');
    assert.equal(server.requests(), 2, 'an explicit retry refetched and recompiled');
    artifact = good; // the static file is fixed
    await client.load();
    assert.equal(client.status, 'ready');
    assert.equal(server.requests(), 3);
    const working = client.module;
    const state = reveal(client, client.newGame({ width: 9, height: 9, mines: 10 }), 4, 4).state;
    const odds = await client.odds(routing(state));
    assert.equal(odds.revision, state.revision, 'the worker started from the working module');
    await client.restart();
    reveal(client, client.newGame({ width: 9, height: 9, mines: 10 }), 4, 4);
    assert.ok(client.module === working, 'a restart reuses the module that started');
    assert.equal(server.requests(), 3, 'restarts and workers never refetch');
  } finally {
    client.dispose();
    await server.close();
  }
});

test('an injected module is used as given on every attempt', async () => {
  const bad = new WebAssembly.Module(wrongAbiModule(host.REQUIRED_EXPORTS));
  const failing = new EngineClient({ module: bad, log: () => {} });
  await rejectsCode(failing.load(), 'abi_mismatch');
  await rejectsCode(failing.load(), 'abi_mismatch');
  assert.ok(failing.module === bad, 'a test module is never dropped or replaced');
  failing.dispose();
  const module = await engineModule();
  await withClient({ module }, async (client) => {
    await client.restart();
    assert.ok(client.module === module, 'the injected module survives a restart');
    assert.equal(client.status, 'ready');
  });
});

test('disposing during a load never resurrects the client', async () => {
  const server = await serveWasm(() => readFileSync(requireWasm()), 50);
  try {
    const fetching = new EngineClient({ wasmUrl: server.url, log: () => {} });
    const loading = fetching.load();
    fetching.dispose();
    await rejectsCode(loading, 'engine_disposed');
    assert.equal(fetching.status, 'disposed');
    assert.ok(fetching.engine === null, 'no engine was installed');
    assert.ok(fetching.module === null, 'nothing was cached for a disposed client');
    throwsCode(() => fetching.newGame({ width: 9, height: 9, mines: 10 }), 'engine_disposed');
    await rejectsCode(fetching.load(), 'engine_disposed');
    await rejectsCode(fetching.restart(), 'engine_disposed');

    const injected = new EngineClient({ module: await engineModule(), log: () => {} });
    const starting = injected.load();
    injected.dispose();
    await rejectsCode(starting, 'engine_disposed');
    assert.ok(injected.engine === null, 'no engine was installed');
    assert.equal(injected.status, 'disposed');
  } finally {
    await server.close();
  }
});

// ----------------------------------------------------------- new games

test('createGame accepts boundary configurations and returns a hidden ready GameState', async () => {
  await withClient({}, async (client) => {
    const configs = [[5, 5, 1], [5, 5, 16], [80, 80, 6391], [5, 80, 391], [80, 5, 391], [9, 9, 10], [9, 8, 10]];
    configs.forEach(([width, height, mines], i) => {
      const state = client.newGame({ width, height, mines });
      assert.equal(state.generation, i + 1);
      assert.equal(state.id, 'local-1-' + (i + 1));
      assert.deepEqual([state.width, state.height, state.mines], [width, height, mines]);
      assert.equal(state.status, 'ready');
      assert.equal(state.revision, 0);
      assert.equal(state.flags, 0);
      assert.equal(state.elapsed_seconds, 0);
      assert.equal(state.cells.length, width * height);
      assert.ok(state.cells.every((c) => !c.revealed && !c.flagged && c.adjacent === null && c.mine === null &&
        !c.exploded));
    });
    const state = client.state();
    assert.deepEqual(Object.keys(state), ['id', 'generation', 'width', 'height', 'mines', 'status', 'revision',
      'flags', 'elapsed_seconds', 'cells']);
    assert.deepEqual(JSON.parse(JSON.stringify(state)), state);
  });
});

test('createGame rejects values that would truncate, before marshalling', async () => {
  await withClient({}, async (client) => {
    const calls = spyExports(client);
    // 2^32 + 9 and 2^32 + 10 would wrap to the valid 9 and 10 in an i32.
    const bad = [true, false, '9', null, undefined, [9], {}, 9.5, 10.5, NaN, Infinity, -Infinity, -1, -9, 1e20, 1e30,
      2 ** 32, 2 ** 32 + 9, 2 ** 32 + 10, 9n];
    for (const value of bad) {
      throwsCode(() => client.newGame({ width: value, height: 9, mines: 10 }), 'invalid_width',
        (e) => assert.equal(e.status, null));
      throwsCode(() => client.newGame({ width: 9, height: value, mines: 10 }), 'invalid_height');
      throwsCode(() => client.newGame({ width: 9, height: 9, mines: value }), 'invalid_mines',
        (e) => assert.match(e.message, /from 1 to 72 for a 9x9 board/));
    }
    throwsCode(() => client.newGame({ width: 9, height: 9, mines: 10, preset: 'beginner' }), 'invalid_request');
    throwsCode(() => client.newGame(null), 'invalid_request');
    throwsCode(() => client.newGame([9, 9, 10]), 'invalid_request');
    assert.deepEqual(reachedGame(calls), [], 'no rejected value reached WASM');
    assert.throws(() => client.state(), /No game/, 'nothing was created');
  });
});

test('createGame passes valid safe integers through unchanged', async () => {
  await withClient({}, async (client) => {
    const calls = spyExports(client);
    for (const [width, height, mines] of [[5, 5, 1], [80, 80, 6391], [5, 80, 391], [80, 5, 391], [9, 9, 10]]) {
      client.newGame({ width, height, mines });
      const call = calls.filter((c) => c.name === 'ms_new_game').at(-1);
      assert.deepEqual(call.args.slice(0, 5), [width, height, mines, SEED().lo, SEED().hi]);
    }
  });
});

test('createGame domain errors come from C in Python order', async () => {
  await withClient({}, async (client) => {
    const cases = [
      [{ width: 0, height: 9, mines: 10 }, 'invalid_width', 1],
      [{ width: 9, height: 0, mines: 10 }, 'invalid_height', 2],
      [{ width: 4, height: 9, mines: 10 }, 'invalid_width', 1],
      [{ width: 81, height: 9, mines: 10 }, 'invalid_width', 1],
      [{ width: 9, height: 4, mines: 10 }, 'invalid_height', 2],
      [{ width: 9, height: 81, mines: 10 }, 'invalid_height', 2],
      [{ width: 9, height: 9, mines: 0 }, 'invalid_mines', 3],
      [{ width: 9, height: 9, mines: 73 }, 'invalid_mines', 3],
      [{ width: 9, height: 9, mines: 81 }, 'invalid_mines', 3],
      [{ width: 4, height: 81, mines: 0 }, 'invalid_width', 1],
      [{ width: 9, height: 81, mines: 0 }, 'invalid_height', 2]
    ];
    for (const [config, code, status] of cases) {
      throwsCode(() => client.newGame(config), code, (e) => assert.equal(e.status, status, JSON.stringify(config)));
    }
    throwsCode(() => client.newGame({ width: 9, height: 9, mines: 73 }), 'invalid_mines',
      (e) => assert.match(e.message, /from 1 to 72 for a 9x9 board/));
    // A later field's type error never hides an earlier field's domain error.
    throwsCode(() => client.newGame({ width: 4, height: 'x', mines: 10 }), 'invalid_width');
    throwsCode(() => client.newGame({ width: 9, height: 81, mines: true }), 'invalid_height');
    throwsCode(() => client.newGame({ width: -9, height: 9, mines: 10 }), 'invalid_width');
    throwsCode(() => client.newGame({ width: 9, height: -9, mines: 10 }), 'invalid_height');
    throwsCode(() => client.newGame({ width: 9, height: 9, mines: -1 }), 'invalid_mines');
    const game = client.newGame({ width: 9, height: 9, mines: 10 });
    throwsCode(() => client.newGame({ width: 9, height: 9, mines: 80 }), 'invalid_mines');
    assert.equal(client.state().generation, game.generation, 'a rejected new game leaves the current one');
  });
});

// --------------------------------------------------------------- actions

test('actions reject non-integer arguments before marshalling', async () => {
  await withClient({}, async (client) => {
    const state = client.newGame({ width: 9, height: 9, mines: 10 });
    const base = { action: 'reveal', row: 0, col: 0, generation: state.generation, revision: 0 };
    const act = (change) => client.act(Object.assign({}, base, change));
    const calls = spyExports(client);
    // Raw action numbers 1..3 are never accepted in place of the names.
    for (const action of ['explode', 'REVEAL', '', null, ['reveal'], 1, 3, 255, -1, true, false, { reveal: 1 }]) {
      throwsCode(() => act({ action }), 'invalid_action');
    }
    // 1e18 and 1e40 are not safe integers: rejected, never rounded or wrapped.
    for (const value of [true, false, 1.5, '1', null, undefined, [0], {}, 1e18, 1e40, NaN, Infinity, -Infinity, 1n]) {
      throwsCode(() => act({ row: value }), 'invalid_coordinates');
      throwsCode(() => act({ col: value }), 'invalid_coordinates');
    }
    for (const value of [true, '1', null, 1.5, -1, NaN, Infinity, -Infinity, [0], 2 ** 53]) {
      throwsCode(() => act({ revision: value }), 'invalid_revision');
    }
    for (const value of [0, -1, true, '1', 1.5, 2 ** 31, 2 ** 40]) {
      throwsCode(() => act({ generation: value }), 'invalid_revision');
    }
    throwsCode(() => client.act(Object.assign({ extra: 1 }, base)), 'invalid_request');
    assert.deepEqual(reachedGame(calls), [], 'no rejected value reached WASM');
    assert.equal(client.state().revision, 0, 'nothing was applied');
  });
});

test('out-of-bounds cells name the board ranges, checked before routing', async () => {
  await withClient({}, async (client) => {
    const state = client.newGame({ width: 9, height: 8, mines: 10 });
    const at = (row, col, extra = {}) => client.act(Object.assign({ action: 'reveal', row, col }, routing(state), extra));
    throwsCode(() => at(8, 0), 'out_of_bounds', (e) => {
      assert.equal(e.status, STATUS.OUT_OF_BOUNDS);
      assert.equal(e.message, 'Cell (row 8, col 0) is off the board: row must be 0-7 and col must be 0-8.');
    });
    throwsCode(() => at(0, 9), 'out_of_bounds');
    throwsCode(() => at(-1, 0), 'out_of_bounds', (e) => assert.match(e.message, /row -1/));
    const calls = spyExports(client);
    // 2^32 and 2^32 + 1 would wrap onto cells (0, 0) and (0, 1).
    throwsCode(() => at(2 ** 32, 0), 'out_of_bounds', (e) => assert.equal(e.status, null));
    throwsCode(() => at(0, 2 ** 32 + 1), 'out_of_bounds');
    assert.deepEqual(reachedGame(calls), [], 'decided without calling C');
    throwsCode(() => at(-1, 0, { revision: 2 ** 40 }), 'out_of_bounds');
    throwsCode(() => at(-1, 0, { generation: 0 }), 'out_of_bounds');
  });
});

test('revisions above the maximum are stale, never wrapped', async () => {
  await withClient({}, async (client) => {
    const state = client.newGame({ width: 9, height: 9, mines: 10 });
    for (const revision of [2 ** 31, 2 ** 32, 2 ** 32 + 1, 2 ** 40, Number.MAX_SAFE_INTEGER]) {
      throwsCode(() => client.act({ action: 'flag', row: 0, col: 0, generation: state.generation, revision }),
        'stale_revision', (e) => {
          assert.equal(e.kind, 'conflict');
          assert.equal(e.state.revision, 0);
        });
      throwsCode(() => client.autosolve({ generation: state.generation, revision }), 'stale_revision');
    }
    throwsCode(() => client.act({ action: 'flag', row: 0, col: 0, generation: 7, revision: 0 }), 'stale_revision',
      (e) => assert.equal(e.status, STATUS.STALE_REVISION));
    assert.equal(client.state().flags, 0);
  });
});

test('conflicts return the latest state and never replay the move', async () => {
  await withClient({}, async (client) => {
    const start = client.newGame({ width: 9, height: 9, mines: 10 });
    const flag = (row, col, base) => client.act({ action: 'flag', row, col, generation: base.generation,
      revision: base.revision });
    const first = flag(0, 0, start);
    assert.equal(first.changed, true);
    assert.equal(first.state.revision, 1);
    // A second action based on the same revision (queued input) is refused.
    throwsCode(() => flag(1, 1, start), 'stale_revision', (e) => {
      assert.equal(e.status, STATUS.STALE_REVISION);
      assert.equal(e.state.revision, 1);
      assert.equal(e.state.cells[0].flagged, true);
      assert.equal(e.state.cells[10].flagged, false);
      assert.equal(e.message, 'The board changed since revision 0; it is now at revision 1. ' +
        'The latest state is included.');
    });
    assert.equal(client.state().revision, 1);
    // A flagged cell ignores a reveal: a no-op keeps the revision.
    const noop = reveal(client, first.state, 0, 0);
    assert.equal(noop.changed, false);
    assert.equal(noop.state.revision, 1);
  });
});

test('won and lost games refuse moves and disclose the layout', async () => {
  await withClient({}, async (client) => {
    const won = reveal(client, denseGame(client), 4, 4).state;
    assert.equal(won.status, 'won');
    assert.equal(won.cells.filter((c) => c.mine === true).length, 72);
    assert.ok(won.cells.every((c) => c.mine === !c.revealed));
    assert.equal(won.flags, 72, 'winning flags every mine');
    throwsCode(() => reveal(client, won, 0, 0), 'game_over', (e) => {
      assert.equal(e.kind, 'conflict');
      assert.equal(e.state.status, 'won');
      assert.match(e.message, /\(you won\)/);
    });

    const ready = denseGame(client);
    const started = reveal(client, ready, 0, 0).state; // the 2x2 corner block is safe
    assert.equal(started.status, 'playing');
    assert.ok(started.cells.every((c) => c.mine === null), 'no layout while playing');
    const lost = reveal(client, started, 8, 8).state;
    assert.equal(lost.status, 'lost');
    assert.deepEqual(lost.cells[80], { revealed: true, flagged: false, adjacent: null, mine: true, exploded: true });
    assert.equal(lost.cells.filter((c) => c.exploded).length, 1);
    assert.equal(lost.cells.filter((c) => c.mine).length, 72);
    throwsCode(() => client.autosolve(routing(lost)), 'game_over');
  });
});

test('elapsed_seconds is the engine milliseconds on the injected host clock', async () => {
  let now = 1000;
  await withClient({ now: () => now }, async (client) => {
    const ready = client.newGame({ width: 9, height: 9, mines: 72 });
    now = 1500;
    assert.equal(client.state().elapsed_seconds, 0);
    const started = reveal(client, ready, 0, 0).state;
    assert.equal(started.elapsed_seconds, 0);
    now = 4000;
    assert.equal(client.state().elapsed_seconds, 2.5);
    const lost = reveal(client, client.state(), 8, 8).state;
    now = 90000;
    assert.equal(client.state().elapsed_seconds, lost.elapsed_seconds, 'frozen once the game ends');
    assert.equal(lost.elapsed_seconds, 2.5);
  });
});

test('public state never carries the layout before the end', async () => {
  await withClient({}, async (client) => {
    const check = (state) => state.cells.forEach((cell, i) => {
      assert.equal(cell.mine, null, `cell ${i}: no mine information while ${state.status}`);
      assert.equal(cell.exploded, false);
      if (cell.revealed) assert.ok(Number.isInteger(cell.adjacent) && cell.adjacent >= 0 && cell.adjacent <= 8);
      else assert.equal(cell.adjacent, null, `cell ${i}: a hidden cell has no clue`);
    });
    const ready = client.newGame({ width: 16, height: 16, mines: 40 });
    check(ready);
    const flagged = client.act({ action: 'flag', row: 0, col: 0, generation: ready.generation, revision: 0 }).state;
    check(flagged);
    const playing = reveal(client, flagged, 8, 8).state;
    assert.equal(playing.status, 'playing');
    check(playing);
    check(client.state());
  });
});

test('the default host clock is performance.now', async () => {
  const original = performance.now;
  let now = 100;
  performance.now = () => now;
  try {
    await withClient({ randomSeed: () => ({ lo: 3, hi: 0 }) }, async (client) => {
      const ready = client.newGame({ width: 9, height: 9, mines: 72 });
      now = 1000;
      const started = reveal(client, ready, 0, 0).state;
      assert.equal(started.status, 'playing');
      now = 3250;
      assert.equal(client.state().elapsed_seconds, 2.25);
    });
  } finally {
    performance.now = original;
  }
});

test('play to victory through the adapter', async () => {
  const W = 5;
  const config = { width: W, height: W, mines: 2 };
  // A seed whose game (first reveal at 0) offers a flag followed by a chord
  // that opens cells, like the baseline script; the layout is learned by
  // losing once, then the same seed and first reveal replay it.
  for (let seed = 1; seed < 100; seed++) {
    let played = false;
    await withClient({ randomSeed: () => ({ lo: seed, hi: 0 }) }, async (client) => {
      const mines = learnLayout(client, config, 0);
      let state = reveal(client, client.newGame(config), 0, 0).state;
      const opened = state.cells.findIndex((c, i) => c.revealed && c.adjacent === 1 &&
        neighbors(i, W, W).some((j) => mines.has(j) && !state.cells[j].revealed) &&
        neighbors(i, W, W).some((j) => !mines.has(j) && !state.cells[j].revealed));
      if (state.status !== 'playing' || opened < 0) return;
      played = true;
      let revision = state.revision;
      const step = (request) => {
        const result = client.act(Object.assign(request, routing(state)));
        assert.equal(result.changed, true);
        assert.equal(result.state.revision, ++revision, 'each change is one revision');
        state = result.state;
      };
      const mine = neighbors(opened, W, W).find((j) => mines.has(j));
      step({ action: 'flag', row: Math.floor(mine / W), col: mine % W });
      assert.equal(state.flags, 1);
      const hiddenBefore = state.cells.filter((c) => !c.revealed).length;
      step({ action: 'chord', row: Math.floor(opened / W), col: opened % W });
      assert.ok(state.cells.filter((c) => !c.revealed).length < hiddenBefore, 'the chord opened cells');
      for (let i = 0; i < W * W; i++) {
        if (state.status !== 'playing' || mines.has(i) || state.cells[i].revealed) continue;
        step({ action: 'reveal', row: Math.floor(i / W), col: i % W });
      }
      assert.equal(state.status, 'won');
      assert.equal(state.flags, 2, 'winning flags every mine');
      assert.deepEqual(state.cells.flatMap((c, i) => (c.mine ? [i] : [])), [...mines].sort((a, b) => a - b));
      assert.ok(state.cells.every((c) => !c.exploded));
      assert.equal(typeof state.elapsed_seconds, 'number');
      assert.deepEqual(client.state(), state, 'the final state reads back unchanged');
    });
    if (played) return;
  }
  assert.fail('no seed below 100 offers a flag-and-chord opening');
});

// ------------------------------------------------- memory and ownership

test('memory growth detaches old views; every read rebuilds them', async () => {
  await withClient({}, async (client) => {
    const state = reveal(client, client.newGame({ width: 80, height: 80, mines: 600 }), 40, 40).state;
    const engine = client.engine;
    const before = engine.memory.buffer;
    const big = [];
    while (before.byteLength !== 0) {
      assert.ok(big.length < 24, 'memory never grew');
      big.push(engine.alloc(1 << 20));
    }
    assert.equal(before.byteLength, 0, 'memory.grow detached the previous ArrayBuffer');
    const again = client.state();
    assert.deepEqual(again, Object.assign({}, state, { elapsed_seconds: again.elapsed_seconds }));
    const flagged = client.act({ action: 'flag', row: 0, col: 0, generation: state.generation,
      revision: state.revision });
    assert.equal(flagged.state.cells[0].flagged !== state.cells[0].flagged || state.cells[0].revealed, true);
    big.forEach((ptr) => engine.free(ptr));
  });
});

test('game buffers are owned once per board size and released on resize', async () => {
  await withClient({}, async (client) => {
    const live = () => client.engine.liveBytes(POOL.BUFFERS);
    client.newGame({ width: 9, height: 9, mines: 10 });
    const small = live();
    for (let i = 0; i < 20; i++) client.state();
    client.newGame({ width: 9, height: 9, mines: 10 });
    assert.equal(live(), small, 'same size: buffers are reused');
    client.newGame({ width: 80, height: 80, mines: 10 });
    const large = live();
    assert.ok(large > small);
    client.newGame({ width: 9, height: 9, mines: 10 });
    assert.equal(live(), small, 'the 80x80 buffers were freed');
    throwsCode(() => client.newGame({ width: 80, height: 80, mines: 6400 }), 'invalid_mines');
    assert.equal(live(), small, 'a rejected game frees its new buffers');
  });
});

test('a trap poisons the game instance; restart starts a fresh one', async () => {
  let broken = false;
  const now = () => {
    if (broken) throw new Error('host clock failed');
    return 5;
  };
  await withClient({ now }, async (client) => {
    const ready = client.newGame({ width: 9, height: 9, mines: 10 });
    broken = true;
    throwsCode(() => reveal(client, ready, 4, 4), 'trap', (e) => {
      assert.equal(e.kind, 'trap');
      assert.match(e.message, /stopped unexpectedly .*host clock failed/);
      assert.ok(Array.isArray(e.log));
    });
    assert.equal(client.status, 'failed');
    throwsCode(() => client.state(), 'trap');
    throwsCode(() => client.newGame({ width: 9, height: 9, mines: 10 }), 'trap');
    await rejectsCode(client.odds(routing(ready)), 'trap');
    broken = false;
    await client.restart();
    assert.equal(client.status, 'ready');
    const fresh = client.newGame({ width: 9, height: 9, mines: 10 });
    assert.equal(fresh.id, 'local-2-1', 'a new instance never reuses the failed game id');
    assert.equal(reveal(client, fresh, 4, 4).state.status === 'ready', false);
  });
});

// ------------------------------------------------------------------ odds

test('ready and finished games answer with placeholders, without a worker', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory }, async (client) => {
    const ready = denseGame(client);
    const placeholder = await client.odds(routing(ready));
    assert.equal(placeholder.status, 'not-started');
    assert.equal(placeholder.game_id, ready.id);
    assert.ok(placeholder.probabilities.every((p) => p === null));
    // Only the required meta keys, without solver diagnostics (no ESS key).
    const required = (reason) => ({
      frontier_cells: 0, components: 0, unconstrained_cells: 0, samples: 0, elapsed_ms: 0, reason: reason
    });
    assert.deepEqual(placeholder.meta, required('not_started'));
    const lost = reveal(client, reveal(client, ready, 0, 0).state, 8, 8).state;
    const finished = await client.odds(routing(lost));
    assert.equal(finished.status, 'finished');
    assert.equal(finished.message, 'This game is over (you lost), so there are no odds to show.');
    assert.deepEqual(finished.meta, required('game_over'));
    assert.equal(factory.created.length, 0);
  });
});

test('a playing position is solved by the worker and accepted by C', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory }, async (client) => {
    const state = reveal(client, client.newGame({ width: 16, height: 16, mines: 40 }), 8, 8).state;
    assert.equal(state.status, 'playing');
    const odds = await client.odds(routing(state));
    assert.ok(['exact', 'approximate', 'unavailable'].includes(odds.status), odds.status);
    assert.deepEqual(Object.keys(odds), ['game_id', 'generation', 'revision', 'status', 'probabilities',
      'proven_safe', 'proven_mines', 'message', 'meta']);
    assert.equal(odds.game_id, state.id);
    assert.equal(odds.generation, state.generation);
    assert.equal(odds.revision, state.revision);
    // The former solver's meta keys, with ESS null when C did not measure it.
    assert.deepEqual(Object.keys(odds.meta), ['frontier_cells', 'components', 'unconstrained_cells', 'samples',
      'elapsed_ms', 'reason', 'exact_components', 'sampled_components', 'sample_attempts', 'effective_sample_size']);
    if (odds.status === 'exact') assert.equal(odds.meta.effective_sample_size, null);
    if (odds.status === 'approximate') assert.equal(typeof odds.meta.effective_sample_size, 'number');
    assert.equal(odds.probabilities.length, 256);
    state.cells.forEach((cell, i) => {
      if (cell.revealed) assert.equal(odds.probabilities[i], null);
    });
    // Only an explicit copy of the public observation was posted.
    const worker = factory.created[0];
    const index = worker.posted.findIndex((m) => m.type === 'solve');
    const solve = worker.posted[index];
    assert.deepEqual(Object.keys(solve), ['type', 'id', 'generation', 'revision', 'observation']);
    assert.ok(solve.observation instanceof ArrayBuffer);
    assert.equal(worker.postedBytes[index], host.observationSize(16, 16), 'a copy of the observation only');
    assert.equal(solve.observation.byteLength, 0, 'the copy was transferred to the worker');
    assert.notEqual(solve.observation, client.engine.memory.buffer);
    assert.ok(client.engine.memory.buffer.byteLength > 0, 'the game memory itself was never transferred');
    assert.equal(client.solverStats.solvesSent, 1);
    assert.equal(client.engine.liveBytes(POOL.ENGINE) > 0, true);
  });
});

test('current-revision odds are reused unless unavailable; concurrent requests share a solve', async () => {
  await withClient({}, async (client) => {
    const state = reveal(client, client.newGame({ width: 16, height: 16, mines: 40 }), 8, 8).state;
    const [a, b] = await Promise.all([client.odds(routing(state)), client.odds(routing(state))]);
    assert.deepEqual(a, b);
    assert.equal(client.solverStats.solvesSent, 1, 'one solve for two concurrent requests');
    const again = await client.odds(routing(state));
    const expected = a.status === 'unavailable' ? 2 : 1;
    assert.equal(client.solverStats.solvesSent, expected,
      a.status === 'unavailable' ? 'an unavailable answer is recomputed' : 'an exact/approximate answer is reused');
    assert.equal(again.status === 'unavailable' || again.status === a.status, true);
  });
});

test('stale odds requests never reach the worker', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory }, async (client) => {
    const ready = client.newGame({ width: 9, height: 9, mines: 10 });
    const state = reveal(client, ready, 4, 4).state;
    await rejectsCode(client.odds(routing(ready)), 'stale_revision', (e) => assert.equal(e.state.revision,
      state.revision));
    await rejectsCode(client.odds({ generation: state.generation, revision: 2 ** 31 }), 'stale_revision');
    for (const revision of [-1, 1.5, NaN, Infinity, '0', true, 2 ** 53]) {
      await rejectsCode(client.odds({ generation: state.generation, revision }), 'invalid_revision');
    }
    await rejectsCode(client.odds({ generation: state.generation, revision: state.revision, extra: 1 }),
      'invalid_request');
    assert.equal(factory.created.length, 0);
  });
});

test('a move cancels the solve in flight by terminating its worker', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory }, async (client) => {
    const state = reveal(client, client.newGame({ width: 30, height: 16, mines: 99 }), 8, 15).state;
    const pending = client.odds(routing(state));
    const moved = client.act({ action: 'flag', row: 0, col: 0, generation: state.generation,
      revision: state.revision });
    assert.equal(moved.changed, true, 'the move did not wait for the solver');
    await rejectsCode(pending, 'aborted', (e) => assert.equal(e.kind, 'aborted'));
    assert.equal(factory.created[0].terminated, true);
    const fresh = await client.odds(routing(moved.state));
    assert.equal(fresh.revision, moved.state.revision);
    assert.equal(factory.created.length, 2, 'a new worker solved the new revision');
  });
});

test('a new game or an explicit cancel also abandons the old request', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory }, async (client) => {
    const state = reveal(client, client.newGame({ width: 16, height: 16, mines: 40 }), 8, 8).state;
    const first = client.odds(routing(state));
    client.newGame({ width: 9, height: 9, mines: 10 });
    await rejectsCode(first, 'aborted');
    const next = reveal(client, client.state(), 4, 4).state;
    const second = client.odds(routing(next));
    client.cancelOdds();
    await rejectsCode(second, 'aborted');
    assert.ok(factory.created.every((w) => w.terminated));
  });
});

test('a solver trap is an explicit failure; the game survives and a retry recreates the worker',
  async () => {
    let faults = { solveTraps: true };
    const created = [];
    const createWorker = (url) => {
      const worker = nodeWorkerFactory(faults)(url);
      created.push(worker);
      return worker;
    };
    await withClient({ createWorker }, async (client) => {
      const state = reveal(client, client.newGame({ width: 16, height: 16, mines: 40 }), 8, 8).state;
      await rejectsCode(client.odds(routing(state)), 'trap', (e) => {
        assert.equal(e.kind, 'worker', 'never a game-instance failure');
        assert.match(e.message, /stopped unexpectedly/);
      });
      assert.equal(client.status, 'ready');
      const nothing = client.autosolve(routing(state));
      assert.deepEqual([nothing.available, nothing.changed], [false, false], 'a failed solve plays no batch');
      assert.deepEqual(nothing.state, client.state());
      await waitFor(() => created[0].terminated);
      faults = {};
      const odds = await client.odds(routing(state));
      assert.equal(odds.revision, state.revision);
      assert.equal(created.length, 2);
      const moved = client.act({ action: 'flag', row: 0, col: 0, generation: state.generation,
        revision: state.revision });
      assert.equal(moved.changed, true);
    });
  });

test('a worker that cannot start fails the request without touching the game', async () => {
  const createWorker = fakeWorkerFactory();
  await withClient({ createWorker }, async (client) => {
    const state = reveal(client, client.newGame({ width: 9, height: 9, mines: 10 }), 4, 4).state;
    const pending = client.odds(routing(state));
    createWorker.created[0].crash('Failed to fetch module script');
    await rejectsCode(pending, 'solver_unavailable', (e) => assert.equal(e.kind, 'worker'));
    assert.equal(client.state().revision, state.revision);
  });
});

test('the real worker answers only well-formed public observation requests', async () => {
  const worker = nodeWorkerFactory()(new URL('probability-worker.js', siteUrl()));
  const replies = [];
  worker.onmessage = (event) => replies.push(event.data);
  try {
    worker.postMessage({ type: 'init', module: await engineModule() });
    await waitFor(() => replies.length === 1);
    assert.deepEqual(replies[0], { type: 'ready' });
    await withClient({}, async (client) => {
      const state = reveal(client, client.newGame({ width: 9, height: 9, mines: 10 }), 4, 4).state;
      const observation = () => client.engine.observe(state.generation, state.revision).buffer;
      const base = (id) => ({ type: 'solve', id: id, generation: state.generation, revision: state.revision });
      const forbidden = [{ flags: [0] }, { mines: [1] }, { layout: [2] }, { board: {} }, { seed: 1 },
        { memory: client.engine.memory.buffer }];
      for (const [i, extra] of forbidden.entries()) {
        worker.postMessage(Object.assign(base(i + 1), { observation: observation() }, extra));
        await waitFor(() => replies.length === i + 2);
        const reply = replies[i + 1];
        assert.equal(reply.type, 'error', JSON.stringify(Object.keys(extra)));
        assert.equal(reply.id, i + 1);
        assert.equal(reply.error.code, 'invalid_message');
        assert.equal(reply.error.fatal, false);
      }
      worker.postMessage(Object.assign(base(99), { observation: new Uint8Array(observation()) }));
      await waitFor(() => replies.length === forbidden.length + 2);
      assert.equal(replies.at(-1).error.code, 'invalid_message', 'a typed-array view is not a copy');
      // Malformed observation contents: JS refuses what it cannot size, C
      // (ms_obs_validate inside the solve) refuses the rest, without a trap.
      const corrupt = [
        [(dv) => dv.setUint32(8, 0, true), 'invalid_message'],
        [(dv) => dv.setUint32(8, 10, true), 'invalid_message'],
        [(dv) => dv.setUint8(32 + 40, 9), 'invalid_observation'],
        [(dv) => dv.setUint32(16, 82, true), 'invalid_observation'],
        [(dv) => dv.setUint32(20, 1, true), 'invalid_observation'],
        [(dv) => dv.setUint32(24, 1, true), 'invalid_observation']
      ];
      for (const [i, [mutate, code]] of corrupt.entries()) {
        const buffer = observation();
        mutate(new DataView(buffer));
        const count = replies.length;
        worker.postMessage(Object.assign(base(200 + i), { observation: buffer }), [buffer]);
        await waitFor(() => replies.length === count + 1);
        assert.equal(replies.at(-1).type, 'error');
        assert.equal(replies.at(-1).error.code, code, `case ${i}`);
        assert.equal(replies.at(-1).error.fatal, false);
      }
      const buffer = observation();
      const before = replies.length;
      worker.postMessage(Object.assign(base(100), { observation: buffer }), [buffer]);
      await waitFor(() => replies.length === before + 1);
      const answer = replies.at(-1);
      assert.equal(answer.type, 'result');
      assert.deepEqual([answer.id, answer.generation, answer.revision], [100, state.generation, state.revision]);
      assert.ok(answer.result instanceof ArrayBuffer);
      assert.equal(answer.result.byteLength, host.resultSize(9, 9));
      client.engine.acceptResult(state.generation, state.revision, new Uint8Array(answer.result));
    });
  } finally {
    worker.terminate();
  }
});

test('worker results are checked before C accepts them; rejected ones never touch the game', async () => {
  const createWorker = fakeWorkerFactory();
  const solver = await host.instantiateSolverEngine(await engineModule(), { log: () => {} });
  await withClient({ createWorker }, async (client) => {
    const state = reveal(client, client.newGame({ width: 9, height: 9, mines: 10 }), 4, 4).state;
    const calls = spyExports(client);
    const acceptCalls = () => calls.filter((call) => call.name === 'ms_accept_result').length;
    const revealedCell = state.cells.findIndex((c) => c.revealed);
    const hiddenCell = state.cells.findIndex((c) => !c.revealed);
    // Answers the next odds request with the real solver's result for the
    // posted observation, as transformed by `forge`.
    const answer = async (forge) => {
      const before = createWorker.created.length;
      const pending = client.odds(routing(state));
      const worker = createWorker.created.at(-1);
      if (createWorker.created.length > before) worker.reply({ type: 'ready' });
      await flush();
      const request = worker.last('solve');
      const real = solver.solve(new Uint8Array(request.observation));
      worker.reply({ type: 'result', id: request.id, generation: request.generation, revision: request.revision,
        result: forge(real).buffer });
      return { pending, worker };
    };
    const edit = (change) => (real) => {
      const forged = real.slice();
      change(new DataView(forged.buffer), forged);
      return forged;
    };
    const cases = [
      // Refused by the adapter before C sees them:
      ['truncated', (real) => real.slice(0, real.length - 8), 'invalid_result', false],
      ['all zero', (real) => new Uint8Array(real.length), 'invalid_result', false],
      ['not a result', edit((dv) => dv.setUint32(0, 0x4D530001, true)), 'invalid_result', false],
      ['NaN probability', edit((dv) => {
        dv.setFloat64(120 + 8 * hiddenCell, NaN, true);
      }), 'invalid_result', false],
      ['NaN effective sample size', edit((dv) => {
        dv.setUint32(96, 1, true);
        dv.setFloat64(112, NaN, true);
      }), 'invalid_result', false],
      ['unknown reason', edit((dv) => dv.setUint32(12, 10, true)), 'invalid_result', false],
      // Decodable, refused by C's acceptance gate:
      ['another observation', edit((dv) => dv.setBigUint64(32, dv.getBigUint64(32, true) ^ 1n, true)),
        'invalid_result', true],
      ['placeholder status', edit((dv) => dv.setUint32(8, 4, true)), 'invalid_result', true],
      ['negative count', edit((dv) => dv.setUint32(64, 0x80000000, true)), 'invalid_result', true]
    ];
    for (const [name, forge, code, reachesC] of cases) {
      const before = acceptCalls();
      const { pending, worker } = await answer(forge);
      await rejectsCode(pending, code, (e) => assert.equal(e.kind, 'worker', name));
      assert.equal(acceptCalls() - before, reachesC ? 1 : 0, `${name}: C saw it only after JS decoded it`);
      assert.equal(client.status, 'ready', `${name}: the game instance is healthy`);
      assert.equal(client.state().revision, state.revision);
      assert.equal(client.autosolve(routing(state)).available, false, `${name}: nothing was accepted or cached`);
      assert.equal(worker.terminated, true, `${name}: the worker is recycled`);
    }
    assert.equal(createWorker.created.length, cases.length, 'every retry started a fresh worker');

    // -0.0 is a valid zero for C (p != 0.0), so the decoder accepts it too.
    const { pending } = await answer(edit((dv) => dv.setFloat64(120 + 8 * revealedCell, -0, true)));
    const odds = await pending;
    assert.equal(odds.probabilities[revealedCell], null);
    assert.equal(client.status, 'ready');
    assert.equal(client.autosolve(routing(state)).available, true, 'the valid answer was accepted');
    const now = client.state();
    if (now.status === 'playing') {
      const target = now.cells.findIndex((c) => !c.revealed && !c.flagged);
      const moved = client.act({ action: 'flag', row: Math.floor(target / 9), col: target % 9,
        generation: now.generation, revision: now.revision });
      assert.equal(moved.changed, true, 'the game is playable throughout');
    } else {
      assert.equal(now.status, 'won', 'an accepted batch may finish the game, never lose it');
    }
  });
});

// ------------------------------------------------------------- autosolve

test('autosolve takes only generation and revision and needs an accepted result', async () => {
  await withClient({}, async (client) => {
    const ready = client.newGame({ width: 16, height: 16, mines: 40 });
    throwsCode(() => client.autosolve(routing(ready)), 'game_not_started', (e) => assert.equal(e.kind, 'conflict'));
    const state = reveal(client, ready, 8, 8).state;
    // There is no way to hand proofs to the engine: index lists of any kind
    // ([true], 2^32 + 16 that would wrap to 16, ...) are refused outright.
    for (const extra of [{ proven_safe: [0] }, { proven_mines: [] }, { safe: [] }, { proven_safe: [true] },
      { proven_mines: [2 ** 32 + 16] }]) {
      throwsCode(() => client.autosolve(Object.assign({}, routing(state), extra)), 'invalid_request');
    }
    for (const revision of [true, null, -1, 1.5, '1']) {
      throwsCode(() => client.autosolve({ generation: state.generation, revision }), 'invalid_revision');
    }
    const none = client.autosolve(routing(state));
    assert.deepEqual([none.available, none.changed], [false, false], 'no accepted result: nothing changes');
    assert.equal(none.state.revision, state.revision);
    const odds = await client.odds(routing(state));
    const batch = client.autosolve(routing(state));
    assert.equal(batch.available, true);
    const actionable = odds.proven_safe.some((i) => !state.cells[i].revealed) ||
      odds.proven_mines.some((i) => !state.cells[i].flagged);
    assert.equal(batch.changed, actionable, 'a batch changes the game iff the proofs require a move');
    assert.equal(batch.state.revision, state.revision + (batch.changed ? 1 : 0), 'one revision per batch');
    if (batch.changed) {
      throwsCode(() => client.autosolve(routing(state)), 'stale_revision', (e) => assert.equal(e.state.revision,
        state.revision + 1));
    }
  });
});

test('autosolve: one atomic batch per revision, duplicates refused, pause, resume after a manual move',
  async () => {
    const solver = await host.instantiateSolverEngine(await engineModule(), { log: () => {} });
    const config = { width: 9, height: 9, mines: 10 };
    let spec = null;
    const createWorker = oracleWorkers(solver, () => spec);
    await withClient({ randomSeed: () => ({ lo: 5, hi: 0x5eed }), createWorker }, async (client) => {
      const mines = learnLayout(client, config, 40);
      const clue = (i) => neighbors(i, 9, 9).filter((j) => mines.has(j)).length;
      // Safe cells no flood can reach stay hidden until someone opens them.
      const isolated = [...Array(81).keys()].filter((i) => i !== 40 && !mines.has(i) &&
        !neighbors(40, 9, 9).includes(i) && neighbors(i, 9, 9).every((j) => mines.has(j) || clue(j) > 0));
      assert.ok(isolated.length >= 2, 'seed 5 leaves two isolated safe cells');
      const [s1, s2] = isolated;
      const mine = [...mines][0];
      spec = { layout: [...mines], ambiguous: [mine, s1, s2] };
      const solves = () => client.solverStats.solvesSent;

      const state = reveal(client, client.newGame(config), 4, 4).state;
      assert.equal(state.status, 'playing');
      const none = client.autosolve(routing(state));
      assert.deepEqual([none.available, none.changed], [false, false], 'nothing accepted yet');
      await client.odds(routing(state));
      assert.equal(solves(), 1);

      const batch = client.autosolve(routing(state));
      assert.deepEqual([batch.available, batch.changed], [true, true]);
      assert.equal(batch.state.revision, state.revision + 1, 'the whole batch is one revision');
      batch.state.cells.forEach((cell, i) => {
        if (i === mine || i === s1 || i === s2) assert.deepEqual([cell.revealed, cell.flagged], [false, false], `${i}`);
        else if (mines.has(i)) assert.equal(cell.flagged, true, `proven mine ${i} flagged`);
        else assert.equal(cell.revealed, true, `proven safe ${i} revealed`);
      });
      throwsCode(() => client.autosolve(routing(state)), 'stale_revision', (e) => {
        assert.equal(e.state.revision, batch.state.revision);
      });
      assert.equal(client.state().flags, 9, 'a duplicate batch toggles nothing back');

      const odds = await client.odds(routing(batch.state));
      assert.equal(solves(), 2, 'one solve per revision');
      assert.deepEqual([odds.probabilities[mine], odds.probabilities[s1], odds.probabilities[s2]], [0.5, 0.5, 0.5]);
      const pause = client.autosolve(routing(batch.state));
      assert.deepEqual([pause.available, pause.changed], [true, false], 'nothing certain left: a pause');
      assert.equal(pause.state.revision, batch.state.revision);
      const again = client.autosolve(routing(batch.state));
      assert.deepEqual([again.available, again.changed, again.state.revision], [true, false, batch.state.revision]);
      await client.odds(routing(batch.state));
      assert.equal(solves(), 2, 'the exact answer is reused for its revision');

      spec = { layout: [...mines] };
      const moved = reveal(client, batch.state, Math.floor(s1 / 9), s1 % 9).state;
      assert.equal(moved.status, 'playing', 'the manual move survived');
      await client.odds(routing(moved));
      assert.equal(solves(), 3);
      const finish = client.autosolve(routing(moved));
      assert.equal(finish.changed, true);
      assert.equal(finish.state.revision, moved.revision + 1);
      assert.equal(finish.state.status, 'won');
      assert.equal(finish.state.cells[s2].revealed, true);
    });
  });
