// Move-advisor tests against the real engine (static/engine-client.js,
// static/wasm-host.js, static/probability-worker.js and the production
// reactor): the planner's WASM exports and their buffer rules, the game
// instance's ms_check_plan, and EngineClient.recommendation() through the
// real worker module: routing, reuse after flag-only revisions, odds-first
// scheduling, cancellation, worker failures and refused plans. Which cell
// the planner picks is tested in C (tests/c/test_planner.c); here only what
// the WASM boundary and the JavaScript adapter add. Like the other real-WASM
// tests these need the production reactor and fail (never skip) without it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import {
  ROOT, engineModule, fakeWorkerFactory, flush, importSite, nodeWorkerFactory, requireWasm, waitFor
} from './support/site.mjs';
import { forgePlan, wrongAbiModule } from './support/forge.mjs';

requireWasm();
const { EngineClient } = await importSite('engine-client.js');
const host = await importSite('wasm-host.js');
const { EngineError, PLAN_LIMITS_SIZE, PLAN_NO_CELL, PLAN_RESULT_SIZE, POOL, REQUIRED_EXPORTS, STATUS } = host;

const SEED = () => ({ lo: 0x9e3779b9, hi: 0x7f4a7c15 });
const PLAN_EXPORTS = ['ms_init_plan_limits', 'ms_plan_observation', 'ms_check_plan'];

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

async function solverInstance() {
  return host.instantiateSolverEngine(await engineModule(), { log: () => {} });
}

function throwsCode(fn, code) {
  assert.throws(fn, (error) => {
    assert.ok(error instanceof EngineError, `expected EngineError, got ${error && error.stack}`);
    assert.equal(error.code, code);
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
  assert.ok(error instanceof EngineError, `expected an EngineError rejection, got ${error && error.stack}`);
  assert.equal(error.code, code);
  if (check) check(error);
  return error;
}

function routing(state) {
  return { generation: state.generation, revision: state.revision };
}

function reveal(client, state, row, col) {
  return client.act(Object.assign({ action: 'reveal', row: row, col: col }, routing(state))).state;
}

function flag(client, state, index) {
  return client.act(Object.assign({ action: 'flag', row: Math.floor(index / state.width), col: index % state.width },
    routing(state))).state;
}

// A playing 16x16 position (the first reveal of this seeded game at (8, 8)
// opens an area and does not end it).
function playing(client) {
  const state = reveal(client, client.newGame({ width: 16, height: 16, mines: 40 }), 8, 8);
  assert.equal(state.status, 'playing');
  return state;
}

function hiddenCells(state) {
  return state.cells.flatMap((cell, i) => (!cell.revealed && !cell.flagged ? [i] : []));
}

// A different game for every new game of the client (randomSeed option).
function countingSeed() {
  let next = 0;
  return () => ({ lo: (0x9e3779b9 + next++) >>> 0, hi: 0x7f4a7c15 });
}

// A 16x16 position where certainty runs out: new games are opened at
// (8, 8) and played with accepted odds and autosolve batches until a batch
// changes nothing while the game is still on (a genuine guess is needed).
async function pausedGame(client) {
  for (let attempt = 0; attempt < 20; attempt++) {
    let state = reveal(client, client.newGame({ width: 16, height: 16, mines: 40 }), 8, 8);
    while (state.status === 'playing') {
      await client.odds(routing(state));
      const batch = client.autosolve(routing(state));
      if (!batch.changed) return state;
      state = batch.state;
    }
  }
  throw new Error('no paused 16x16 position in 20 games');
}

// Records every export the client's game instance calls.
function spyExports(client) {
  const calls = [];
  const engine = client.engine;
  const call = engine.call.bind(engine);
  engine.call = (name, ...args) => {
    calls.push(name);
    return call(name, ...args);
  };
  return calls;
}

function checkPayload(plan, state) {
  assert.deepEqual(Object.keys(plan), ['game_id', 'generation', 'revision', 'status', 'reason', 'cell', 'row', 'col',
    'survival_probability', 'win_probability', 'standard_error', 'candidates', 'layouts', 'trials', 'incomplete',
    'rollout_wins', 'search_nodes', 'posterior_exact', 'elapsed_ms', 'exact_wins', 'exact_total', 'observation_hash']);
  assert.equal(plan.game_id, state.id);
  assert.deepEqual(routing(plan), routing(state));
  assert.ok(['exact', 'estimated', 'unavailable', 'none'].includes(plan.status), plan.status);
  if (plan.status === 'exact' || plan.status === 'estimated') {
    assert.ok(Number.isInteger(plan.cell) && !state.cells[plan.cell].revealed, 'a hidden cell');
    assert.deepEqual([plan.row, plan.col], [Math.floor(plan.cell / state.width), plan.cell % state.width]);
  } else {
    assert.equal(plan.cell, null);
  }
  assert.deepEqual(JSON.parse(JSON.stringify(plan)), plan);
}

// ------------------------------------------------------- exports and limits

test('the production module exports exactly the documented ABI, the move advisor included', async () => {
  const module = await engineModule();
  const functions = WebAssembly.Module.exports(module).filter((entry) => entry.kind === 'function')
    .map((entry) => entry.name).filter((name) => !name.startsWith('wasm_buddy_'));
  assert.deepEqual(functions.sort(), [...REQUIRED_EXPORTS].sort(), 'every ms_* export is one the page knows');
  for (const name of PLAN_EXPORTS) assert.ok(REQUIRED_EXPORTS.includes(name), name);
  // A build without one of the advisor exports is refused before it runs.
  for (const name of PLAN_EXPORTS) {
    const stale = new WebAssembly.Module(wrongAbiModule(REQUIRED_EXPORTS.filter((other) => other !== name), 1));
    assert.throws(() => host.auditEngineModule(stale), (error) => error instanceof EngineError &&
      error.code === 'abi_mismatch' && error.message.includes('missing ' + name), name);
  }
});

test('the default plan limits come from C, match planner.h and are fixed-size', async () => {
  const solver = await solverInstance();
  const limits = solver.defaultPlanLimits();
  const header = readFileSync(join(ROOT, 'c', 'planner.h'), 'utf8');
  const define = (name) => {
    const match = new RegExp(`#define\\s+${name}\\s+([0-9.]+)u?\\b`).exec(header);
    assert.ok(match, name);
    return Number(match[1]);
  };
  assert.equal(limits.exactLayoutLimit, define('MS_PLAN_DEFAULT_EXACT_LAYOUTS'));
  assert.equal(limits.exactNodeLimit, define('MS_PLAN_DEFAULT_EXACT_NODES'));
  assert.equal(limits.sampleCount, define('MS_PLAN_DEFAULT_SAMPLES'));
  assert.equal(limits.candidateLimit, define('MS_PLAN_DEFAULT_CANDIDATES'));
  assert.equal(limits.minRollouts, define('MS_PLAN_DEFAULT_MIN_ROLLOUTS'));
  assert.equal(limits.timeBudgetMs, define('MS_PLAN_DEFAULT_TIME_MS'));
  assert.equal(limits.memoryBudgetBytes, 256n << 20n);
  assert.equal(limits.rolloutStepLimit, host.SOLVER_MAX_CELLS, 'MS_PLAN_DEFAULT_ROLLOUT_STEPS');
  assert.equal(limits.flags, 0, 'the default seed is the observation hash');
  const x = solver.exports;
  const scratch = solver.alloc(PLAN_LIMITS_SIZE + 8);
  assert.equal(x.ms_init_plan_limits(scratch, 56), STATUS.INVALID_BUFFER, 'solver-sized limits are refused');
  assert.equal(x.ms_init_plan_limits(scratch, PLAN_LIMITS_SIZE + 8), STATUS.INVALID_BUFFER);
  assert.equal(x.ms_init_plan_limits(scratch + 4, PLAN_LIMITS_SIZE), STATUS.INVALID_BUFFER, 'misaligned');
  assert.equal(x.ms_init_plan_limits(0xFFFFFFF0, PLAN_LIMITS_SIZE), STATUS.INVALID_BUFFER, 'wild');
  solver.free(scratch);
});

// -------------------------------------------------- the planner's buffers

test('ms_plan_observation refuses bad buffers before touching them and always releases its workspace', async () => {
  const solver = await solverInstance();
  const x = solver.exports;
  await withClient({}, async (client) => {
    const state = playing(client);
    const observation = client.engine.observe(state.generation, state.revision);
    const obsLen = observation.length;
    const baseline = solver.liveBytes(POOL.BUFFERS);
    const obs = solver.alloc(obsLen);
    const result = solver.alloc(PLAN_RESULT_SIZE);
    const big = solver.alloc(obsLen + PLAN_RESULT_SIZE + PLAN_LIMITS_SIZE); // one buffer for aliasing cases
    const freed = solver.alloc(PLAN_RESULT_SIZE);
    solver.free(freed);
    const limits = solver.planLimits;
    const bytes = (ptr, len) => Array.from(solver.copyOut(ptr, len));
    const fill = () => {
      solver.copyIn(obs, observation);
      solver.bytes(result, PLAN_RESULT_SIZE).fill(0xA5);
      solver.copyIn(big, observation);
      solver.bytes(big + obsLen, PLAN_RESULT_SIZE + PLAN_LIMITS_SIZE).fill(0x5A);
    };
    fill();
    const snapshot = () => JSON.stringify([bytes(obs, obsLen), bytes(limits, PLAN_LIMITS_SIZE),
      bytes(result, PLAN_RESULT_SIZE), bytes(big, obsLen + PLAN_RESULT_SIZE + PLAN_LIMITS_SIZE)]);
    const before = snapshot();
    const refused = {
      'the plan over its observation': [obs, obsLen, limits, PLAN_LIMITS_SIZE, obs, PLAN_RESULT_SIZE],
      'the plan inside its observation': [big, obsLen, limits, PLAN_LIMITS_SIZE, big + 8, PLAN_RESULT_SIZE],
      'the plan over its limits': [obs, obsLen, limits, PLAN_LIMITS_SIZE, limits, PLAN_RESULT_SIZE],
      'limits over the observation': [big, obsLen, big, PLAN_LIMITS_SIZE, result, PLAN_RESULT_SIZE],
      'limits overlapping the plan': [obs, obsLen, big + obsLen + 8, PLAN_LIMITS_SIZE, big + obsLen,
        PLAN_RESULT_SIZE],
      'solver-sized limits': [obs, obsLen, limits, 56, result, PLAN_RESULT_SIZE],
      'an odds-sized result': [obs, obsLen, limits, PLAN_LIMITS_SIZE, result, 120],
      'a short plan': [obs, obsLen, limits, PLAN_LIMITS_SIZE, result, 104],
      'an observation past its buffer': [obs, obsLen + 8, limits, PLAN_LIMITS_SIZE, result, PLAN_RESULT_SIZE],
      'a misaligned plan': [obs, obsLen, limits, PLAN_LIMITS_SIZE, big + obsLen + 4, PLAN_RESULT_SIZE],
      'a freed plan buffer': [obs, obsLen, limits, PLAN_LIMITS_SIZE, freed, PLAN_RESULT_SIZE],
      'a null plan': [obs, obsLen, limits, PLAN_LIMITS_SIZE, 0, PLAN_RESULT_SIZE],
      'a wild plan': [obs, obsLen, limits, PLAN_LIMITS_SIZE, 0xFFFFFFF0, PLAN_RESULT_SIZE],
      'a plan beyond memory': [obs, obsLen, limits, PLAN_LIMITS_SIZE, solver.memory.buffer.byteLength + 16,
        PLAN_RESULT_SIZE],
      'a wild observation': [8, obsLen, limits, PLAN_LIMITS_SIZE, result, PLAN_RESULT_SIZE],
      'the engine\'s own memory as limits': [obs, obsLen, 1024, PLAN_LIMITS_SIZE, result, PLAN_RESULT_SIZE]
    };
    for (const [name, args] of Object.entries(refused)) {
      assert.equal(x.ms_plan_observation(...args), STATUS.INVALID_BUFFER, name);
      assert.equal(snapshot(), before, `${name}: no buffer changed`);
      assert.equal(solver.liveBytes(POOL.SOLVER), 0, `${name}: no workspace held`);
    }

    // Malformed contents behind valid buffers: refused by C, plan untouched.
    const corrupt = (mutate, expected, name) => {
      fill();
      const dv = new DataView(solver.memory.buffer);
      mutate(dv);
      const plan = bytes(result, PLAN_RESULT_SIZE);
      assert.equal(x.ms_plan_observation(obs, obsLen, limits, PLAN_LIMITS_SIZE, result, PLAN_RESULT_SIZE), expected,
        name);
      assert.deepEqual(bytes(result, PLAN_RESULT_SIZE), plan, `${name}: the plan buffer is untouched`);
      assert.equal(solver.liveBytes(POOL.SOLVER), 0, `${name}: the workspace is released`);
    };
    corrupt((dv) => dv.setUint8(obs + 32 + hiddenCells(state)[0], 9), STATUS.INVALID_OBSERVATION, 'clue 9');
    corrupt((dv) => dv.setUint32(obs + 20, 0, true), STATUS.INVALID_OBSERVATION, 'revealed count');
    const savedLimits = bytes(limits, PLAN_LIMITS_SIZE);
    corrupt((dv) => dv.setFloat64(limits + 32, NaN, true), STATUS.INVALID_LIMITS, 'NaN time budget');
    solver.copyIn(limits, Uint8Array.from(savedLimits));
    corrupt((dv) => dv.setUint32(limits, 0x4D530003, true), STATUS.INVALID_BUFFER, 'solver limits magic');
    solver.copyIn(limits, Uint8Array.from(savedLimits));

    // A valid request answers with a canonical plan for that observation.
    fill();
    assert.equal(x.ms_plan_observation(obs, obsLen, limits, PLAN_LIMITS_SIZE, result, PLAN_RESULT_SIZE), STATUS.OK);
    assert.equal(solver.liveBytes(POOL.SOLVER), 0, 'the workspace is released after a plan');
    const plan = host.decodePlan(solver.copyOut(result, PLAN_RESULT_SIZE), observation, routing(state));
    checkPayload(Object.assign(plan, { game_id: state.id }), state);
    assert.deepEqual(bytes(obs, obsLen), Array.from(observation), 'the observation is read only');
    for (const ptr of [obs, result, big]) solver.free(ptr);
    assert.equal(solver.liveBytes(POOL.BUFFERS), baseline, 'every test buffer was freed');

    // The adapter's own calls leak nothing either.
    for (let i = 0; i < 3; i++) {
      const bytesOut = solver.plan(observation);
      assert.equal(bytesOut.length, PLAN_RESULT_SIZE);
      assert.equal(solver.liveBytes(POOL.BUFFERS), baseline, 'plan() frees its buffers');
      assert.equal(solver.liveBytes(POOL.SOLVER), 0);
    }
  });
});

// ------------------------------------------------------- ms_check_plan

test('ms_check_plan binds a plan to the current position and stores nothing', async () => {
  const solver = await solverInstance();
  await withClient({}, async (client) => {
    let state = playing(client);
    const observation = client.engine.observe(state.generation, state.revision);
    const plan = solver.plan(observation);
    const engineBytes = client.engine.liveBytes(POOL.ENGINE);
    client.engine.checkPlan(state.generation, state.revision, plan);
    assert.equal(client.engine.cachedResult(state.generation, state.revision), null, 'no odds were stored');
    assert.equal(client.autosolve(routing(state)).available, false, 'autosolve has no proofs from a plan');
    assert.equal(client.engine.liveBytes(POOL.ENGINE), engineBytes);

    const edited = (change) => {
      const bytes = plan.slice();
      change(new DataView(bytes.buffer));
      return bytes;
    };
    const refused = {
      'another observation hash': [edited((dv) => dv.setBigUint64(32, dv.getBigUint64(32, true) ^ 1n, true)),
        'invalid_result'],
      'another width': [edited((dv) => dv.setUint32(16, 15, true)), 'invalid_result'],
      'another revealed count': [edited((dv) => dv.setUint32(28, dv.getUint32(28, true) + 1, true)),
        'invalid_result'],
      'a revealed cell': [edited((dv) => {
        dv.setUint32(8, 2, true);
        dv.setUint32(12, 0, true);
        dv.setUint32(40, state.cells.findIndex((c) => c.revealed), true);
      }), 'invalid_result'],
      'an unknown status': [edited((dv) => dv.setUint32(8, 4, true)), 'invalid_result']
    };
    for (const [name, [bytes, code]] of Object.entries(refused)) {
      throwsCode(() => client.engine.checkPlan(state.generation, state.revision, bytes), code);
      assert.equal(client.engine.cachedResult(state.generation, state.revision), null, name);
    }
    for (const bytes of [edited((dv) => dv.setUint32(0, 0x4D530004, true)), edited((dv) => dv.setUint32(4, 2, true))]) {
      assert.throws(() => client.engine.checkPlan(state.generation, state.revision, bytes),
        (e) => e instanceof EngineError && ['invalid_buffer', 'invalid_result'].includes(e.code));
    }
    throwsCode(() => client.engine.checkPlan(state.generation, state.revision, plan.slice(0, 104)), 'invalid_result');

    // Raw export: pointers and lengths first, without reading the plan.
    const x = client.engine.exports;
    const buffer = client.engine.alloc(PLAN_RESULT_SIZE + 8);
    client.engine.copyIn(buffer, plan);
    assert.equal(x.ms_check_plan(state.generation, state.revision, buffer, PLAN_RESULT_SIZE), STATUS.OK);
    for (const [ptr, len] of [[buffer, 104], [buffer, 120], [buffer + 4, PLAN_RESULT_SIZE], [0, PLAN_RESULT_SIZE],
      [0xFFFFFFF0, PLAN_RESULT_SIZE], [buffer + 16, PLAN_RESULT_SIZE]]) {
      assert.equal(x.ms_check_plan(state.generation, state.revision, ptr, len), STATUS.INVALID_BUFFER, `${ptr}+${len}`);
    }
    assert.equal(x.ms_check_plan(state.generation, state.revision + 1, buffer, PLAN_RESULT_SIZE),
      STATUS.STALE_REVISION);
    assert.equal(x.ms_check_plan(0, state.revision, buffer, PLAN_RESULT_SIZE), STATUS.INVALID_REVISION);
    client.engine.free(buffer);

    // Flags are not part of the observation: a flag-only revision keeps the
    // plan valid, the old revision is stale, a reveal invalidates the plan.
    const hidden = hiddenCells(state);
    const flagged = flag(client, state, hidden[hidden.length - 1]);
    assert.equal(flagged.revision, state.revision + 1);
    client.engine.checkPlan(flagged.generation, flagged.revision, plan);
    throwsCode(() => client.engine.checkPlan(state.generation, state.revision, plan), 'stale_revision');
    state = flagged;
    const odds = await client.odds(routing(state));
    const safe = odds.proven_safe.find((i) => !state.cells[i].revealed && !state.cells[i].flagged);
    const target = safe !== undefined ? safe : hiddenCells(state)[0];
    const after = reveal(client, state, Math.floor(target / 16), target % 16);
    if (after.status === 'playing') {
      throwsCode(() => client.engine.checkPlan(after.generation, after.revision, plan), 'invalid_result');
    } else {
      throwsCode(() => client.engine.checkPlan(after.generation, after.revision, plan), 'game_over');
    }
    const ready = client.newGame({ width: 16, height: 16, mines: 40 });
    throwsCode(() => client.engine.checkPlan(ready.generation, ready.revision, plan), 'game_not_started');
  });
});

// ------------------------------------------- exact advice is not safest

// gold_a of tests/c/test_planner.c: 5x4, 4 mines, 10 compatible layouts.
// The top row is the safest (safe in 8 of 10 layouts) but wins only 4; the
// optimum wins 5 of 10 from a cell that is safe in only 5.
const GOLD_A = { width: 5, height: 4, mines: 4,
  clues: { 7: 2, 10: 1, 11: 1, 12: 2, 14: 2, 15: 0, 16: 0, 17: 1, 19: 1 } };

function observationOf(spec) {
  const bytes = new Uint8Array(host.observationSize(spec.width, spec.height));
  const dv = new DataView(bytes.buffer);
  bytes.fill(0xFF, 32, 32 + spec.width * spec.height);
  for (const [index, clue] of Object.entries(spec.clues)) bytes[32 + Number(index)] = clue;
  [0x4D530002, 1, spec.width, spec.height, spec.mines, Object.keys(spec.clues).length, 0, 0]
    .forEach((v, i) => dv.setUint32(4 * i, v, true));
  return bytes;
}

test('the real exact planner prefers the whole-game optimum over the safest reveal (gold_a)', async () => {
  const solver = await solverInstance();
  const obs = observationOf(GOLD_A);
  const odds = host.decodeResult(solver.solve(obs));
  assert.equal(odds.status, 'exact');
  for (const cell of [0, 1, 2, 3, 4]) assert.equal(odds.probabilities[cell], 0.2, `cell ${cell}: 8 of 10 safe`);
  const plan = host.decodePlan(solver.plan(obs), obs);
  assert.equal(plan.status, 'exact');
  assert.deepEqual([plan.exact_wins, plan.exact_total, plan.layouts], [5, 10, 10]);
  assert.equal(plan.win_probability, 0.5);
  assert.ok([5, 6, 8, 9].includes(plan.cell), `an optimal first reveal, not ${plan.cell}`);
  assert.equal(plan.survival_probability, 0.5);
  assert.equal(odds.probabilities[plan.cell], 0.5, 'riskier than the safest cells');
  assert.equal(solver.liveBytes(POOL.SOLVER), 0);
});

test('in a real game the exact suggestion can be riskier than the lowest-risk cell (browser seed 2026)', async () => {
  // The browser's fixEntropy(page, 2026) gives the page's first game this seed.
  await withClient({ randomSeed: () => ({ lo: 2026, hi: 0x5eed }) }, async (client) => {
    let state = reveal(client, client.newGame({ width: 9, height: 9, mines: 10 }), 4, 4);
    let odds = null;
    for (;;) {
      odds = await client.odds(routing(state));
      const batch = client.autosolve(routing(state));
      if (!batch.changed) break;
      state = batch.state;
    }
    assert.equal(state.status, 'playing');
    assert.equal(odds.status, 'exact');
    const plan = await client.recommendation(routing(state));
    checkPayload(plan, state);
    assert.equal(plan.status, 'exact');
    assert.deepEqual([plan.exact_wins, plan.exact_total], [7, 15]);
    const risks = state.cells.flatMap((c, i) => (!c.revealed && !c.flagged && odds.probabilities[i] > 0 &&
      odds.probabilities[i] < 1 ? [odds.probabilities[i]] : []));
    const risk = odds.probabilities[plan.cell];
    assert.ok(risk > Math.min(...risks), `risk ${risk} is above the lowest ${Math.min(...risks)}`);
    assert.ok(Math.abs(plan.survival_probability - (1 - risk)) < 1e-12, 'the exact survival matches the odds');
    const batch = client.autosolve(routing(state));
    assert.deepEqual([batch.available, batch.changed], [true, false], 'the suggestion is never played');
  });
});

// Plays seeded games, asking for advice at every position: certain batches
// are played by autosolve, guesses follow the advice (else the lowest-risk
// cell). Any real plan the JS decoder or ms_check_plan refused would reject
// as invalid_plan, so this keeps the decoder no stricter than C.
test('every plan the real planner returns passes the JS decoder and ms_check_plan', async () => {
  const seen = { exact: 0, estimated: 0, unavailable: 0, none: 0 };
  await withClient({ randomSeed: countingSeed() }, async (client) => {
    for (const [width, height, mines, games] of [[9, 9, 10, 20], [9, 9, 30, 20], [16, 16, 40, 4]]) {
      for (let g = 0; g < games; g++) {
        let state = reveal(client, client.newGame({ width, height, mines }), Math.floor(height / 2),
          Math.floor(width / 2));
        for (let step = 0; step < 40 && state.status === 'playing'; step++) {
          const odds = await client.odds(routing(state));
          const plan = await client.recommendation(routing(state));
          checkPayload(plan, state);
          seen[plan.status]++;
          const batch = client.autosolve(routing(state));
          if (batch.changed) {
            state = batch.state;
            continue;
          }
          let target = plan.cell === null ? -1 : plan.cell;
          state.cells.forEach((c, i) => {
            if (plan.cell === null && !c.revealed && !c.flagged && odds.probabilities[i] !== null &&
                (target < 0 || odds.probabilities[i] < odds.probabilities[target])) target = i;
          });
          if (target < 0) break;
          state = reveal(client, state, Math.floor(target / width), target % width);
        }
      }
    }
  });
  assert.ok(seen.exact > 0 && seen.estimated > 0 && seen.none > 0, JSON.stringify(seen));
});

// ------------------------------------------------ EngineClient advice

/*
 * A worker double answering every solve with the real solver and every plan
 * with the real planner, or with forgePlan(real plan, planSpec()) when
 * planSpec() returns a spec: C's checks still apply in full.
 */
function answeringWorkers(solver, planSpec) {
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
        const observation = new Uint8Array(message.observation);
        const reply = { id: message.id, generation: message.generation, revision: message.revision };
        if (message.type === 'solve') {
          worker.reply(Object.assign({ type: 'result', result: solver.solve(observation).buffer }, reply));
          return;
        }
        const real = solver.plan(observation);
        const spec = planSpec();
        const result = spec ? forgePlan(real.buffer, message.observation, spec) : real.buffer;
        worker.reply(Object.assign({ type: 'plan-result', result: result }, reply));
      }, 0);
    };
    return worker;
  };
  create.created = factory.created;
  return create;
}

test('unavailable advice is never reused: asking again plans anew, suggestions are reused', async () => {
  const solver = await solverInstance();
  let spec = { status: 'unavailable', reason: 'budget' };
  await withClient({ createWorker: answeringWorkers(solver, () => spec), randomSeed: countingSeed() },
    async (client) => {
      const state = await pausedGame(client);
      const plans = () => client.solverStats.plansSent;
      const first = await client.recommendation(routing(state));
      assert.deepEqual([first.status, first.reason, first.cell], ['unavailable', 'budget', null]);
      assert.equal(plans(), 1);
      assert.equal(client.lastPlan, null, 'nothing kept for reuse');
      await client.recommendation(routing(state));
      assert.equal(plans(), 2, 'the same position is planned anew');
      const flagged = flag(client, state, hiddenCells(state).at(-1));
      await client.recommendation(routing(flagged));
      assert.equal(plans(), 3, 'a flag-only revision is planned anew too');
      spec = null; // the real planner from now on
      const real = await client.recommendation(routing(flagged));
      assert.ok(['exact', 'estimated'].includes(real.status), real.status);
      assert.equal(plans(), 4);
      assert.deepEqual(await client.recommendation(routing(flagged)), real);
      assert.equal(plans(), 4, 'a suggestion is reused');
    });
});

test('recalculateOdds solves a playing position anew; validation, placeholders and odds priority as odds()',
  async () => {
    await withClient({}, async (client) => {
      const ready = client.newGame({ width: 16, height: 16, mines: 40 });
      assert.equal((await client.recalculateOdds(routing(ready))).status, 'not-started');
      assert.equal(client.solverStats, null, 'a placeholder needs no worker');
      const state = reveal(client, ready, 8, 8);
      const first = await client.odds(routing(state));
      await client.odds(routing(state));
      const solves = () => client.solverStats.solvesSent;
      assert.equal(solves(), first.status === 'unavailable' ? 2 : 1, 'plain odds reuse what C holds');
      const sent = solves();
      const fresh = await client.recalculateOdds(routing(state));
      assert.equal(solves(), sent + 1, 'solved anew although C held a reusable answer');
      assert.deepEqual(routing(fresh), routing(state));
      await client.odds(routing(state));
      assert.equal(solves(), sent + 1 + (fresh.status === 'unavailable' ? 1 : 0), 'C stored the new answer');
      await rejectsCode(client.recalculateOdds(Object.assign({ extra: 1 }, routing(state))), 'invalid_request');
      await rejectsCode(client.recalculateOdds(routing(ready)), 'stale_revision');
      await rejectsCode(client.recalculateOdds({ generation: state.generation, revision: -1 }), 'invalid_revision');
      // Odds first: a plan in flight is abandoned for the recalculation.
      const plan = client.recommendation(routing(state));
      const odds = client.recalculateOdds(routing(state));
      await rejectsCode(plan, 'aborted');
      assert.equal((await odds).revision, state.revision);
      assert.equal(client.state().revision, state.revision, 'nothing was played');
      const lost = reveal(client, reveal(client, client.newGame({ width: 9, height: 9, mines: 72 }), 0, 0), 8, 8);
      assert.equal((await client.recalculateOdds(routing(lost))).status, 'finished');
    });
  });

test('recommendation plans a snapshot of the public observation and reuses it while flags change', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory, randomSeed: countingSeed() }, async (client) => {
    let state = await pausedGame(client);
    const solves = client.solverStats.solvesSent;
    const calls = spyExports(client);
    const [first, shared] = await Promise.all([client.recommendation(routing(state)),
      client.recommendation(routing(state))]);
    checkPayload(first, state);
    assert.ok(['exact', 'estimated'].includes(first.status), 'a paused position gets a suggestion');
    assert.ok(first.cell !== null);
    assert.deepEqual(shared, first, 'concurrent requests share one plan');
    assert.equal(client.solverStats.plansSent, 1);
    assert.equal(client.solverStats.solvesSent, solves, 'advice needs no odds solve');
    assert.equal(calls.filter((name) => name === 'ms_check_plan').length, 1, 'C checked the plan once');
    const worker = factory.created.at(-1);
    const index = worker.posted.findIndex((m) => m.type === 'plan');
    assert.deepEqual(Object.keys(worker.posted[index]), ['type', 'id', 'generation', 'revision', 'observation']);
    assert.equal(worker.postedBytes[index], host.observationSize(16, 16), 'a copy of the observation only');
    assert.equal(worker.posted[index].observation.byteLength, 0, 'the copy was transferred');
    assert.ok(client.engine.memory.buffer.byteLength > 0, 'the game memory itself was never transferred');

    const again = await client.recommendation(routing(state));
    assert.deepEqual(again, first, 'the same position reuses the checked plan');
    assert.equal(client.solverStats.plansSent, 1);

    const target = first.cell;
    state = flag(client, state, target);
    const rebound = await client.recommendation(routing(state));
    assert.deepEqual(rebound, Object.assign({}, first, { revision: state.revision }),
      'a flag-only revision keeps the plan, even on its own cell');
    assert.equal(client.solverStats.plansSent, 1, 'nothing was planned again');
    state = flag(client, state, target);
    assert.equal((await client.recommendation(routing(state))).revision, state.revision);
    assert.equal(client.solverStats.plansSent, 1);
    assert.ok(calls.filter((name) => name === 'ms_check_plan').length >= 4, 'every reuse passed C first');

    const moved = reveal(client, state, Math.floor(target / 16), target % 16);
    if (moved.status === 'playing') {
      const fresh = await client.recommendation(routing(moved));
      checkPayload(fresh, moved);
      assert.equal(client.solverStats.plansSent, 2, 'a new position is planned anew');
    } else {
      await rejectsCode(client.recommendation(routing(moved)), 'game_over');
      assert.equal(client.solverStats.plansSent, 1);
    }
  });
});

test('stale or malformed advice requests never reach the worker', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory }, async (client) => {
    const ready = client.newGame({ width: 9, height: 9, mines: 10 });
    await rejectsCode(client.recommendation(routing(ready)), 'game_not_started', (e) => assert.equal(e.kind, 'conflict'));
    const state = reveal(client, ready, 4, 4);
    await rejectsCode(client.recommendation(routing(ready)), 'stale_revision',
      (e) => assert.equal(e.state.revision, state.revision));
    await rejectsCode(client.recommendation({ generation: state.generation, revision: 2 ** 31 }), 'stale_revision');
    for (const revision of [-1, 1.5, NaN, '0', true, 2 ** 53]) {
      await rejectsCode(client.recommendation({ generation: state.generation, revision }), 'invalid_revision');
    }
    await rejectsCode(client.recommendation({ generation: state.generation, revision: state.revision, cell: 4 }),
      'invalid_request');
    await rejectsCode(client.recommendation(null), 'invalid_request');
    assert.equal(factory.created.length, 0);
  });
});

test('odds come first: advice waits for a solve in flight, and an odds solve abandons a running plan', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory }, async (client) => {
    const state = playing(client);
    const odds = client.odds(routing(state));
    const plan = client.recommendation(routing(state));
    const [answer, advice] = await Promise.all([odds, plan]);
    assert.equal(answer.revision, state.revision);
    checkPayload(advice, state);
    const types = factory.created[0].posted.map((m) => m.type);
    assert.deepEqual(types, ['init', 'solve', 'plan'], 'the plan was sent only after the odds answer');

    // A new game whose odds need a solve: the plan in flight is dropped.
    const next = playing(client);
    const pending = client.recommendation(routing(next));
    const newOdds = client.odds(routing(next));
    await rejectsCode(pending, 'aborted', (e) => assert.equal(e.kind, 'aborted'));
    assert.equal((await newOdds).revision, next.revision);
    assert.equal(client.status, 'ready');
  });
});

test('moves, new games, explicit cancels and dispose abandon the advice in flight', async () => {
  const factory = nodeWorkerFactory();
  await withClient({ createWorker: factory }, async (client) => {
    const state = playing(client);
    const pending = client.recommendation(routing(state));
    const moved = flag(client, state, hiddenCells(state)[0]);
    await rejectsCode(pending, 'aborted');
    assert.equal(factory.created[0].terminated, true, 'the running plan was stopped with its worker');
    assert.equal(moved.revision, state.revision + 1, 'the move did not wait for the planner');

    const second = client.recommendation(routing(moved));
    client.cancelRecommendation();
    await rejectsCode(second, 'aborted');

    const third = client.recommendation(routing(moved));
    client.newGame({ width: 9, height: 9, mines: 10 });
    await rejectsCode(third, 'aborted');
    assert.ok(factory.created.every((worker) => worker.terminated));
  });
  const client = await makeClient();
  const state = playing(client);
  const pending = client.recommendation(routing(state));
  client.dispose();
  await rejectsCode(pending, 'aborted');
  await rejectsCode(client.recommendation(routing(state)), 'engine_disposed');
});

test('a planner trap fails only the advice; the game goes on and a retry recreates the worker', async () => {
  let faults = { planTraps: true };
  const created = [];
  const createWorker = (url) => {
    const worker = nodeWorkerFactory(faults)(url);
    created.push(worker);
    return worker;
  };
  await withClient({ createWorker }, async (client) => {
    const state = playing(client);
    await rejectsCode(client.recommendation(routing(state)), 'trap', (e) => assert.equal(e.kind, 'worker'));
    assert.equal(client.status, 'ready');
    await waitFor(() => created[0].terminated);
    faults = {};
    const odds = await client.odds(routing(state));
    assert.equal(odds.revision, state.revision, 'odds work after a planner trap');
    assert.equal(created.length, 2, 'the odds started a fresh worker');
    const fresh = await withTimeout(client.recommendation(routing(state)));
    checkPayload(fresh, state);
    assert.equal(created.length, 2, 'the retry used it');
  });
});

test('a worker that cannot start fails the advice without touching the game or its odds', async () => {
  const createWorker = fakeWorkerFactory();
  await withClient({ createWorker }, async (client) => {
    const state = playing(client);
    const pending = client.recommendation(routing(state));
    await flush();
    createWorker.created[0].crash('Failed to fetch module script');
    await rejectsCode(pending, 'solver_unavailable', (e) => assert.equal(e.kind, 'worker'));
    assert.equal(client.status, 'ready');
    assert.deepEqual(client.state(), state);
    assert.equal(createWorker.created[0].terminated, true);
    // The next request (odds here) starts a fresh worker.
    const odds = client.odds(routing(state));
    assert.equal(createWorker.created.length, 2);
    client.cancelOdds();
    await rejectsCode(odds, 'aborted');
  });
});

function withTimeout(promise, ms = 30000) {
  let timer = 0;
  return Promise.race([promise, new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error('timed out')), ms);
  })]).finally(() => clearTimeout(timer));
}

test('worker plans are checked before the page sees them; refused ones never touch the game or the odds',
  async () => {
    const createWorker = fakeWorkerFactory();
    const solver = await solverInstance();
    await withClient({ createWorker }, async (client) => {
      const state = playing(client);
      const calls = spyExports(client);
      const checks = () => calls.filter((name) => name === 'ms_check_plan').length;
      // The fake worker answers solves with the real solver's result and
      // plans with forge(real plan) for the posted observation.
      const answer = async (type, forge) => {
        const before = createWorker.created.length;
        const pending = type === 'solve' ? client.odds(routing(state)) : client.recommendation(routing(state));
        await flush();
        const worker = createWorker.created.at(-1);
        if (createWorker.created.length > before) worker.reply({ type: 'ready' });
        await flush();
        await flush();
        const request = worker.last(type);
        const observation = new Uint8Array(request.observation);
        const real = type === 'solve' ? solver.solve(observation) : solver.plan(observation);
        const bytes = forge ? forge(real) : real;
        worker.reply({ type: type === 'solve' ? 'result' : 'plan-result', id: request.id,
          generation: request.generation, revision: request.revision, result: bytes.buffer });
        return { pending, worker };
      };
      const odds = await (await answer('solve')).pending;
      assert.equal(odds.revision, state.revision);
      const solves = client.solverStats.solvesSent;
      const edit = (change) => (real) => {
        const forged = real.slice();
        change(new DataView(forged.buffer));
        return forged;
      };
      const revealed = state.cells.findIndex((c) => c.revealed);
      const cases = [
        // Refused by the adapter before C sees them:
        ['not a plan', edit((dv) => dv.setUint32(0, 0x4D530004, true)), false],
        ['NaN win chance', edit((dv) => dv.setFloat64(80, NaN, true)), false],
        ['unknown status', edit((dv) => dv.setUint32(8, 9, true)), false],
        ['revealed cell', edit((dv) => {
          dv.setUint32(8, 2, true);
          dv.setUint32(40, revealed, true);
        }), false],
        ['cell without a suggestion', edit((dv) => {
          dv.setUint32(8, 3, true);
          dv.setUint32(40, hiddenCells(state)[0], true);
        }), false],
        // Decodable, refused by C's ms_check_plan:
        ['another observation', edit((dv) => dv.setBigUint64(32, dv.getBigUint64(32, true) ^ 1n, true)), true]
      ];
      for (const [name, forge, reachesC] of cases) {
        const before = checks();
        const { pending, worker } = await answer('plan', forge);
        await rejectsCode(pending, 'invalid_plan', (e) => {
          assert.equal(e.kind, 'worker', name);
          assert.match(e.message, /move advisor returned a suggestion that does not fit this board/);
        });
        assert.equal(checks() - before, reachesC ? 1 : 0, `${name}: C saw it only after JS decoded it`);
        assert.equal(worker.terminated, true, `${name}: the worker is recycled`);
        assert.equal(client.status, 'ready', `${name}: the game instance is healthy`);
        assert.equal(client.state().revision, state.revision, `${name}: the game is unchanged`);
        assert.equal(client.lastPlan, null, `${name}: nothing was kept for reuse`);
      }
      // The odds cache is untouched: the accepted odds are still served by C.
      const cached = await client.odds(routing(state));
      assert.deepEqual(cached, odds);
      assert.equal(client.solverStats.solvesSent, solves);
      const batch = client.autosolve(routing(state));
      assert.equal(batch.available, true, 'autosolve still has the accepted odds proofs');

      const after = batch.changed ? batch.state : state;
      if (after.status === 'playing') {
        const valid = await (async () => {
          const s = after;
          const pending = client.recommendation(routing(s));
          await flush();
          const worker = createWorker.created.at(-1);
          if (!worker.posted.some((m) => m.type === 'plan')) worker.reply({ type: 'ready' });
          await flush();
          await flush();
          const request = worker.last('plan');
          const real = solver.plan(new Uint8Array(request.observation));
          worker.reply({ type: 'plan-result', id: request.id, generation: request.generation,
            revision: request.revision, result: real.buffer });
          return pending;
        })();
        checkPayload(valid, after);
      }
    });
  });

test('a plan is never a proof: advice changes nothing autosolve can play', async () => {
  await withClient({ randomSeed: countingSeed() }, async (client) => {
    const state = await pausedGame(client);
    const plan = await client.recommendation(routing(state));
    checkPayload(plan, state);
    assert.ok(plan.cell !== null, 'a suggested cell');
    const odds = await client.odds(routing(state));
    assert.equal(odds.proven_safe.every((i) => state.cells[i].revealed), true, 'nothing certain is left');
    assert.ok(!odds.proven_mines.includes(plan.cell), 'never a proven mine');
    const batch = client.autosolve(routing(state));
    assert.deepEqual([batch.available, batch.changed], [true, false], 'autosolve pauses: the suggestion is no proof');
    assert.deepEqual(batch.state, client.state());
    assert.equal(client.state().cells[plan.cell].revealed, false, 'the suggested cell was not played');
    assert.equal(client.state().revision, state.revision);
    assert.equal(PLAN_NO_CELL, 0xFFFFFFFF);
  });
});
