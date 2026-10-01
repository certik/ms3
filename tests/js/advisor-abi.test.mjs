// Move-advice marshalling tests for static/wasm-host.js: the ms_plan_result
// and ms_plan_limits layouts of c/planner.h and c/wasm_api.h, the strict
// plan decoder and the worker protocol's plan messages. No WASM needed: the
// buffers are built here byte by byte at the documented offsets (the real
// exports are covered by advisor.test.mjs).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { importSite } from './support/site.mjs';

const host = await importSite('wasm-host.js');
const {
  EngineError, MAGIC, PLAN_LIMITS_SIZE, PLAN_NO_CELL, PLAN_REASON_NAMES, PLAN_RESULT_SIZE, PLAN_STATUS_NAMES,
  PLANNER_VERSION, checkPlanRequest, checkSolveRequest, checkWorkerMessage, decodePlan, decodePlanLimits
} = host;

const pad8 = (n) => Math.ceil(n / 8) * 8;

// Observation of Python's (width, height, total, clues).
function obsBuffer(width, height, total, clues = {}) {
  const cells = width * height;
  const bytes = new Uint8Array(pad8(32 + cells));
  const dv = new DataView(bytes.buffer);
  bytes.fill(0xFF, 32, 32 + cells);
  for (const [index, clue] of Object.entries(clues)) bytes[32 + Number(index)] = clue;
  [0x4D530002, 1, width, height, total, Object.keys(clues).length, 0, 0].forEach((v, i) => dv.setUint32(4 * i, v, true));
  return bytes;
}

// A 3x3 board, 2 mines, the corner (0, 0) revealed as 1 and (0, 1) as 2:
// seven hidden cells for two mines, so a guess is needed.
const OBS = obsBuffer(3, 3, 2, { 0: 1, 1: 2 });
// Nothing revealed yet, and nothing left to open (every hidden cell a mine).
const OBS_READY = obsBuffer(3, 3, 2);
const OBS_FINISHED = obsBuffer(3, 3, 7, { 0: 3, 1: 5 });

const STATUS = { none: 0, exact: 1, estimated: 2, unavailable: 3 };

// Canonical fields of each status (c/planner.c ms_plan_result_validate).
const DEFAULTS = {
  [STATUS.exact]: { cell: 4, candidates: 7, layouts: 12, search_nodes: 321, posterior_exact: 1, exact_wins: 9,
    exact_total: 12, survival: 0.75, win: 0.75 },
  [STATUS.estimated]: { cell: 4, candidates: 7, layouts: 96, trials: 48, survival: 0.75, win: 0.5, se: 0.05 },
  [STATUS.none]: { reason: 1 },
  [STATUS.unavailable]: { reason: 4, candidates: 7, layouts: 96, trials: 48, incomplete: 1 }
};

// An ms_plan_result for `obs`; spec overrides any field by its planner.h name.
function planBuffer(spec = {}, obs = OBS) {
  const bytes = new Uint8Array(spec.length ?? PLAN_RESULT_SIZE);
  const dv = new DataView(bytes.buffer);
  const src = new DataView(obs.buffer, obs.byteOffset, obs.byteLength);
  const status = spec.status ?? STATUS.exact;
  const fields = Object.assign({
    magic: 0x4D530006, version: 1, status: status, reason: 0, width: src.getUint32(8, true),
    height: src.getUint32(12, true), total_mines: src.getUint32(16, true), revealed: src.getUint32(20, true),
    cell: PLAN_NO_CELL, candidates: 0, layouts: 0, trials: 0, incomplete: 0, search_nodes: 0, posterior_exact: 0,
    reserved: 0, exact_wins: 0, exact_total: 0, survival: 0, win: 0, se: 0, elapsed: 12.5,
    hash: 0x0123456789abcdefn
  }, DEFAULTS[status] || {}, spec);
  const u32 = { magic: 0, version: 4, status: 8, reason: 12, width: 16, height: 20, total_mines: 24, revealed: 28,
    cell: 40, candidates: 44, layouts: 48, trials: 52, incomplete: 56, search_nodes: 60, posterior_exact: 64,
    reserved: 68, exact_wins: 104, exact_total: 108 };
  for (const [name, offset] of Object.entries(u32)) {
    if (offset + 4 <= bytes.length) dv.setUint32(offset, fields[name], true);
  }
  if (bytes.length >= 112) {
    dv.setBigUint64(32, fields.hash, true);
    dv.setFloat64(72, fields.survival, true);
    dv.setFloat64(80, fields.win, true);
    dv.setFloat64(88, fields.se, true);
    dv.setFloat64(96, fields.elapsed, true);
  }
  return bytes;
}

function malformed(fn, name) {
  assert.throws(fn, (error) => error instanceof EngineError && error.code === 'malformed_buffer' &&
    error.kind === 'internal', name);
}

test('plan constants follow planner.h and wasm_api.h', () => {
  assert.equal(MAGIC.PLAN_LIMITS, 0x4D530005);
  assert.equal(MAGIC.PLAN_RESULT, 0x4D530006);
  assert.equal(PLANNER_VERSION, 1);
  assert.equal(PLAN_LIMITS_SIZE, 64);
  assert.equal(PLAN_RESULT_SIZE, 112);
  assert.equal(PLAN_NO_CELL, 0xFFFFFFFF);
  assert.deepEqual([...PLAN_STATUS_NAMES], ['none', 'exact', 'estimated', 'unavailable']);
  assert.deepEqual([...PLAN_REASON_NAMES], [null, 'certain_moves', 'finished', 'no_samples', 'budget',
    'insufficient_rollouts', 'posterior_unavailable', 'not_started']);
});

test('an exact plan names a hidden cell with exact integer counts', () => {
  const plan = decodePlan(planBuffer(), OBS, { gameId: 'local-1-2', generation: 2, revision: 5 });
  assert.deepEqual(Object.keys(plan), ['game_id', 'generation', 'revision', 'status', 'reason', 'cell', 'row', 'col',
    'survival_probability', 'win_probability', 'standard_error', 'candidates', 'layouts', 'trials', 'incomplete',
    'rollout_wins', 'search_nodes', 'posterior_exact', 'elapsed_ms', 'exact_wins', 'exact_total',
    'observation_hash']);
  assert.deepEqual(plan, {
    game_id: 'local-1-2', generation: 2, revision: 5, status: 'exact', reason: null, cell: 4, row: 1, col: 1,
    survival_probability: 0.75, win_probability: 0.75, standard_error: 0, candidates: 7, layouts: 12, trials: 0,
    incomplete: 0, rollout_wins: null, search_nodes: 321, posterior_exact: true, elapsed_ms: 12.5, exact_wins: 9,
    exact_total: 12, observation_hash: '0123456789abcdef'
  });
  assert.deepEqual(JSON.parse(JSON.stringify(plan)), plan, 'plain JSON, no BigInt');
});

test('an estimated plan carries rollout statistics and no exact counts', () => {
  // 28 of the 64 finished rounds won; one more was cut short.
  const plan = decodePlan(planBuffer({ status: STATUS.estimated, cell: 8, trials: 65, incomplete: 1, win: 0.4375,
    se: 0.0625, survival: 0.5 }), OBS);
  assert.equal(plan.status, 'estimated');
  assert.equal(plan.reason, null);
  assert.deepEqual([plan.cell, plan.row, plan.col], [8, 2, 2]);
  assert.deepEqual([plan.trials, plan.incomplete, plan.layouts], [65, 1, 96]);
  assert.equal(plan.rollout_wins, 28, 'the integer count behind the share');
  assert.deepEqual([plan.win_probability, plan.standard_error, plan.survival_probability], [0.4375, 0.0625, 0.5]);
  assert.equal(plan.posterior_exact, false);
  assert.equal(plan.exact_wins, null);
  assert.equal(plan.exact_total, null);
  assert.equal(plan.game_id, null, 'routing is the caller\'s');
});

test('an estimate may win more often than its survival estimate; only exact plans are held to it', () => {
  // 36 of 48 finished rounds won, while the planner's marginal survival
  // estimate is 0.5: finite samples, not a malformed plan.
  const plan = decodePlan(planBuffer({ status: STATUS.estimated, trials: 48, win: 0.75, survival: 0.5 }), OBS);
  assert.equal(plan.status, 'estimated');
  assert.deepEqual([plan.win_probability, plan.survival_probability], [0.75, 0.5]);
  // Every round won is an estimate too, with a positive standard error.
  const allWon = decodePlan(planBuffer({ status: STATUS.estimated, win: 1, se: 0.02 }), OBS);
  assert.deepEqual([allWon.win_probability, allWon.rollout_wins, allWon.trials], [1, 48, 48]);
  // Exact counts cannot: every layout won survives the first reveal.
  malformed(() => decodePlan(planBuffer({ status: STATUS.exact, survival: 8 / 12 }), OBS), 'exact win above survival');
});

// The counts an unavailable answer may carry depend on why it stopped.
const NOTHING_GENERATED = { candidates: 0, layouts: 0, trials: 0, incomplete: 0, search_nodes: 0 };

test('plans without a suggestion carry no cell and no chances', () => {
  for (const [status, reason, obs, counts] of [[STATUS.none, 1, OBS], [STATUS.none, 2, OBS_FINISHED],
    [STATUS.none, 7, OBS_READY], [STATUS.unavailable, 3, OBS, NOTHING_GENERATED], [STATUS.unavailable, 4, OBS],
    [STATUS.unavailable, 4, OBS, NOTHING_GENERATED], [STATUS.unavailable, 5, OBS],
    [STATUS.unavailable, 6, OBS, NOTHING_GENERATED],
    // An exact search that ran out of budget over a complete listing.
    [STATUS.unavailable, 4, OBS, { candidates: 5, layouts: 40, trials: 0, incomplete: 0, search_nodes: 100,
      posterior_exact: 1 }]]) {
    const plan = decodePlan(planBuffer(Object.assign({ status, reason }, counts), obs), obs);
    assert.equal(plan.status, PLAN_STATUS_NAMES[status]);
    assert.equal(plan.reason, PLAN_REASON_NAMES[reason]);
    for (const key of ['cell', 'row', 'col', 'survival_probability', 'win_probability', 'standard_error',
      'rollout_wins', 'exact_wins', 'exact_total']) {
      assert.equal(plan[key], null, `${PLAN_STATUS_NAMES[status]}: ${key}`);
    }
  }
});

test('malformed or inconsistent plans are refused, never patched', () => {
  decodePlan(planBuffer(), OBS);
  decodePlan(planBuffer({ status: STATUS.estimated }), OBS);
  const exact = (extra) => planBuffer(Object.assign({ status: STATUS.exact }, extra));
  const estimated = (extra) => planBuffer(Object.assign({ status: STATUS.estimated }, extra));
  const none = (extra, obs = OBS) => planBuffer(Object.assign({ status: STATUS.none }, extra), obs);
  const unavailable = (extra) => planBuffer(Object.assign({ status: STATUS.unavailable }, extra));
  const cases = {
    'short buffer': planBuffer({ length: 104 }),
    'result-sized buffer': planBuffer({ length: 120 }),
    'odds result magic': planBuffer({ magic: 0x4D530004 }),
    'limits magic': planBuffer({ magic: 0x4D530005 }),
    'planner version 2': planBuffer({ version: 2 }),
    'status 4': planBuffer({ status: 4 }),
    'reason 8': planBuffer({ reason: 8 }),
    'another width': planBuffer({ width: 4 }),
    'another height': planBuffer({ height: 2 }),
    'another mine total': planBuffer({ total_mines: 3 }),
    'another revealed count': planBuffer({ revealed: 3 }),
    'reserved word': planBuffer({ reserved: 1 }),
    'posterior flag 2': planBuffer({ posterior_exact: 2 }),
    'more unfinished than started': planBuffer({ status: STATUS.estimated, trials: 4, incomplete: 5 }),
    'NaN survival': planBuffer({ survival: NaN }),
    'survival above one': planBuffer({ survival: 1.5 }),
    'negative win': planBuffer({ win: -0.25 }),
    'infinite win': planBuffer({ win: Infinity }),
    'NaN standard error': planBuffer({ status: STATUS.estimated, se: NaN }),
    'negative standard error': planBuffer({ status: STATUS.estimated, se: -0.01 }),
    'negative elapsed': planBuffer({ elapsed: -1 }),
    'infinite elapsed': planBuffer({ elapsed: Infinity }),
    'revealed cell': planBuffer({ cell: 1 }),
    'cell off the board': planBuffer({ cell: 9 }),
    'exact plan without a cell': planBuffer({ cell: PLAN_NO_CELL }),
    'estimated plan without a cell': planBuffer({ status: STATUS.estimated, cell: PLAN_NO_CELL }),
    'a cell without a suggestion': planBuffer({ status: STATUS.none, reason: 1, cell: 4 }),
    'unavailable plan with a cell': planBuffer({ status: STATUS.unavailable, reason: 4, cell: 8 }),
    'exact plan over no layouts': planBuffer({ exact_wins: 0, exact_total: 0 }),
    'more wins than layouts': planBuffer({ exact_wins: 13, exact_total: 12 }),
    'estimated plan with exact counts': planBuffer({ status: STATUS.estimated, exact_wins: 1, exact_total: 2 }),
    'unavailable plan with exact counts': planBuffer({ status: STATUS.unavailable, reason: 4, cell: PLAN_NO_CELL,
      exact_total: 1 }),
    // The pairing and figure rules of ms_plan_result_validate:
    'exact plan winning every layout': exact({ exact_wins: 12, win: 1, survival: 1 }),
    'exact plan winning no layout': exact({ exact_wins: 0, win: 0 }),
    'exact plan over one layout': exact({ exact_wins: 1, exact_total: 1, layouts: 1, win: 1, survival: 1 }),
    'exact layouts other than its total': exact({ layouts: 13 }),
    'exact plan with rollouts': exact({ trials: 4 }),
    'exact plan without search nodes': exact({ search_nodes: 0 }),
    'exact plan over sampled layouts': exact({ posterior_exact: 0 }),
    'exact plan with a standard error': exact({ se: 0.01 }),
    'exact win chance not its ratio': exact({ win: 0.7 }),
    'exact survival of one': exact({ survival: 1 }),
    'exact win above survival': exact({ survival: 8 / 12 }),
    'exact survival not a share of its layouts': exact({ survival: 0.8 }),
    'exact plan with one candidate': exact({ candidates: 1 }),
    'exact plan missing candidates': exact({ candidates: 4 }),
    'exact candidates no more than hidden minus mines': exact({ candidates: 5 }),
    'candidates without layouts': unavailable(Object.assign({}, NOTHING_GENERATED, { candidates: 1 })),
    'search nodes over sampled layouts': unavailable({ search_nodes: 5 }),
    'search nodes over one layout': unavailable(Object.assign({}, NOTHING_GENERATED, { layouts: 1,
      posterior_exact: 1, search_nodes: 3 })),
    'exact plan with a reason': exact({ reason: 1 }),
    'estimated plan with a reason': estimated({ reason: 4 }),
    'estimated plan without a finished round': estimated({ trials: 1, incomplete: 1 }),
    'two unfinished rounds': estimated({ trials: 48, incomplete: 2 }),
    'estimated rounds beyond its layouts': estimated({ trials: 97, win: 0 }),
    'estimated survival of zero': estimated({ survival: 0 }),
    'estimated survival of one': estimated({ survival: 1 }),
    'estimated plan winning no round': estimated({ win: 0 }),
    'estimated plan winning every round without a standard error': estimated({ win: 1, se: 0 }),
    'estimated standard error above one half': estimated({ se: 0.6 }),
    'estimated win not a share of finished rounds': estimated({ win: 0.4 }),
    'estimated plan without candidates': estimated({ candidates: 0 }),
    'none with a rollout reason': none({ reason: 4 }),
    'none without a reason': none({ reason: 0 }),
    'none with counts': none({ layouts: 5 }),
    'finished while cells can still be opened': none({ reason: 2 }),
    'not started after a reveal': none({ reason: 7 }),
    'certain moves before the first reveal': none({ reason: 1 }, OBS_READY),
    'unavailable with a none reason': unavailable({ reason: 1 }),
    'unavailable without a reason': unavailable({ reason: 0 }),
    'unavailable rounds beyond its layouts': unavailable({ trials: 97 }),
    // Counts, for every status:
    'a posterior flag without layouts': unavailable(Object.assign({}, NOTHING_GENERATED, { posterior_exact: 1 })),
    'more candidates than hidden cells': estimated({ candidates: 8 }),
    'rounds without candidates': unavailable({ candidates: 0 }),
    'no samples, yet layouts': unavailable(Object.assign({}, NOTHING_GENERATED, { reason: 3, layouts: 5 })),
    'no posterior, yet candidates': unavailable(Object.assign({}, NOTHING_GENERATED, { reason: 6, candidates: 1 })),
    'no samples, yet search nodes': unavailable(Object.assign({}, NOTHING_GENERATED, { reason: 3,
      search_nodes: 1 })),
    'too few rollouts without layouts': unavailable(Object.assign({}, NOTHING_GENERATED, { reason: 5 }))
  };
  for (const [name, bytes] of Object.entries(cases)) {
    const obs = name === 'certain moves before the first reveal' ? OBS_READY : OBS;
    malformed(() => decodePlan(bytes, obs), name);
  }
  for (const status of [STATUS.exact, STATUS.estimated, STATUS.unavailable]) {
    malformed(() => decodePlan(planBuffer({ status, cell: status === STATUS.unavailable ? PLAN_NO_CELL : 0 },
      OBS_FINISHED), OBS_FINISHED), `${PLAN_STATUS_NAMES[status]} with nothing left to guess`);
    malformed(() => decodePlan(planBuffer({ status, cell: status === STATUS.unavailable ? PLAN_NO_CELL : 0 },
      OBS_READY), OBS_READY), `${PLAN_STATUS_NAMES[status]} before the first reveal`);
  }
  assert.throws(() => decodePlan([1, 2, 3], OBS), EngineError);
  assert.throws(() => decodePlan(planBuffer(), OBS.slice(0, 16)), EngineError, 'the observation is checked too');
  assert.throws(() => decodePlan(planBuffer(), obsBuffer(3, 3, 2, { 0: 1, 1: 2, 4: 3 })), EngineError,
    'a plan for the position before a reveal no longer fits');
});

test('-0.0 decodes as a plain zero, as C compares it', () => {
  const plan = decodePlan(planBuffer({ status: STATUS.estimated, se: -0, elapsed: -0 }), OBS);
  for (const key of ['standard_error', 'elapsed_ms']) assert.ok(Object.is(plan[key], 0), key);
  const empty = decodePlan(planBuffer({ status: STATUS.none, win: -0, se: -0, survival: -0 }), OBS);
  assert.equal(empty.win_probability, null);
});

test('plan limits decode at the documented offsets, u64 fields as BigInt', () => {
  const bytes = new Uint8Array(PLAN_LIMITS_SIZE);
  const dv = new DataView(bytes.buffer);
  [0x4D530005, 1, 256, 100000, 96, 8, 4000, 0].forEach((v, i) => dv.setUint32(4 * i, v, true));
  dv.setFloat64(32, 3000, true);
  dv.setBigUint64(40, 256n << 20n, true);
  dv.setBigUint64(48, 7n, true);
  dv.setUint32(56, 16, true);
  assert.deepEqual(decodePlanLimits(bytes), {
    exactLayoutLimit: 256, exactNodeLimit: 100000, sampleCount: 96, candidateLimit: 8, rolloutStepLimit: 4000,
    flags: 0, timeBudgetMs: 3000, memoryBudgetBytes: 268435456n, seed: 7n, minRollouts: 16
  });
  malformed(() => decodePlanLimits(bytes.slice(0, 56)), 'solver-sized limits');
  const other = bytes.slice();
  new DataView(other.buffer).setUint32(0, 0x4D530003, true);
  malformed(() => decodePlanLimits(other), 'solver limits magic');
  const newer = bytes.slice();
  new DataView(newer.buffer).setUint32(4, 2, true);
  malformed(() => decodePlanLimits(newer), 'planner version 2');
});

function planRequest(extra = {}) {
  return Object.assign({ type: 'plan', id: 3, generation: 1, revision: 4, observation: OBS.slice().buffer }, extra);
}

test('plan requests accept only public fields and routing numbers, like solve requests', () => {
  const request = checkPlanRequest(planRequest());
  assert.deepEqual([request.id, request.generation, request.revision], [3, 1, 4]);
  assert.ok(request.observation instanceof Uint8Array);
  assert.equal(request.header.width, 3);
  const bad = {
    'a solve request': planRequest({ type: 'solve' }),
    'flags field': planRequest({ flags: [0] }),
    'layout field': planRequest({ layout: [4] }),
    'seed field': planRequest({ seed: 1 }),
    'limits field': planRequest({ limits: { time: 1 } }),
    'id 0': planRequest({ id: 0 }),
    'generation 0': planRequest({ generation: 0 }),
    'revision 2^31': planRequest({ revision: 2 ** 31 }),
    'typed array instead of a copy': planRequest({ observation: OBS.slice() }),
    'short observation': planRequest({ observation: new ArrayBuffer(24) })
  };
  for (const [name, message] of Object.entries(bad)) {
    assert.throws(() => checkPlanRequest(message), (error) => error instanceof EngineError &&
      error.code === 'invalid_message' && error.kind === 'protocol', name);
  }
  assert.throws(() => checkSolveRequest(planRequest()), EngineError, 'a plan request is not a solve request');
});

test('plan-result messages are exactly a plan buffer with routing numbers', () => {
  const good = { type: 'plan-result', id: 1, generation: 1, revision: 0, result: new ArrayBuffer(112) };
  assert.equal(checkWorkerMessage(good), good);
  const bad = [
    Object.assign({}, good, { result: new ArrayBuffer(120) }),
    Object.assign({}, good, { result: new ArrayBuffer(104) }),
    Object.assign({}, good, { result: new Uint8Array(112) }),
    Object.assign({}, good, { cell: 4 }),
    Object.assign({}, good, { id: 0 }),
    Object.assign({}, good, { revision: -1 }),
    { type: 'plan', id: 1, generation: 1, revision: 0, observation: new ArrayBuffer(40) }
  ];
  for (const message of bad) {
    assert.throws(() => checkWorkerMessage(message), (e) => e instanceof EngineError && e.kind === 'protocol',
      JSON.stringify(Object.keys(message)));
  }
});
