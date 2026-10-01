// Test-only construction of solver results and move-advice plans with a
// chosen content, used to make odds, autosolve and advice scenarios
// deterministic, and of engine modules that load but cannot start.
// forgeResult and forgePlan are self-contained (no imports or closures): the
// browser tests inline their source into a patched worker with
// Function.prototype.toString.

/*
 * Builds a solver result for the observation the worker was asked about,
 * keeping the real header's dimensions, counts and observation hash so the
 * engine's acceptance gate (ms_accept_result) still applies in full.
 *   { status: 'exact' | 'approximate', values: { index: p }, default: p }
 *   { status: 'unavailable', values: { index: 0 | 1 } }   only those proven
 *   { layout: [mine indices], ambiguous: [indices], until: index }   an
 *     exact "oracle" proving every hidden cell except the ambiguous ones
 *     (shown as 50%) while cell `until` (if given) is still hidden
 * Exact/unavailable values 0 and 1 carry the matching proof flag;
 * approximate ones never carry proofs, whatever their value.
 */
export function forgeResult(resultBuffer, observationBuffer, spec) {
  const source = new DataView(resultBuffer);
  const width = source.getUint32(16, true);
  const height = source.getUint32(20, true);
  const cells = width * height;
  const obs = new Uint8Array(observationBuffer);
  const out = new ArrayBuffer(resultBuffer.byteLength);
  const bytes = new Uint8Array(out);
  const dv = new DataView(out);
  bytes.set(new Uint8Array(resultBuffer, 0, 40));
  const unavailable = spec.status === 'unavailable';
  const exact = spec.status !== 'approximate' && !unavailable;
  dv.setUint32(8, exact ? 1 : unavailable ? 3 : 2, true);
  dv.setUint32(12, exact ? 0 : unavailable ? 6 : 1, true);
  const layout = new Set(spec.layout || []);
  const unresolved = spec.until === undefined || obs[32 + spec.until] === 0xFF;
  const ambiguous = new Set(unresolved ? spec.ambiguous || [] : []);
  const values = spec.values || {};
  let hidden = 0;
  let safe = 0;
  let mines = 0;
  for (let i = 0; i < cells; i++) {
    if (obs[32 + i] !== 0xFF) continue;
    hidden++;
    let value;
    if (spec.layout) value = ambiguous.has(i) ? 0.5 : layout.has(i) ? 1 : 0;
    else value = Object.prototype.hasOwnProperty.call(values, i) ? values[i] : spec.default;
    if (value === undefined) continue;
    let flags = 1;
    if (!spec.status || spec.status !== 'approximate') {
      if (value === 0) {
        flags |= 2;
        safe++;
      }
      if (value === 1) {
        flags |= 4;
        mines++;
      }
    }
    dv.setFloat64(120 + 8 * i, value, true);
    bytes[120 + 8 * cells + i] = flags;
  }
  dv.setUint32(40, unavailable ? 0 : hidden, true);
  dv.setUint32(44, unavailable ? 0 : 1, true);
  dv.setUint32(56, exact ? 1 : 0, true);
  dv.setUint32(60, exact || unavailable ? 0 : 1, true);
  if (!exact && !unavailable) {
    dv.setUint32(52, 400, true);
    dv.setUint32(64, 2000, true);
    dv.setUint32(96, 1, true);
    dv.setFloat64(112, 61.5, true);
  }
  dv.setUint32(72, hidden, true);
  dv.setUint32(88, safe, true);
  dv.setUint32(92, mines, true);
  dv.setFloat64(104, 1.25, true);
  return out;
}

/*
 * Builds a move-advice plan (ms_plan_result, 112 bytes) for the observation
 * the worker was asked about, keeping the real plan's header (magic,
 * version, dimensions, mine total, revealed count and observation hash) so
 * the game instance's ms_check_plan still applies in full:
 *   { status: 'exact', wins, total, survival, cell, candidates }  complete
 *                     search; candidates default to the fewest the planner
 *                     can report, hidden cells minus mines plus one
 *   { status: 'estimated', win, se, trials, incomplete, survival, cell }
 *   { status: 'none' | 'unavailable', reason }             no suggestion
 * cell defaults to the lowest hidden cell not listed in `avoid`;
 * { corrupt: 'hash' } names another observation (C refuses it).
 */
export function forgePlan(planBuffer, observationBuffer, spec) {
  const PLAN_SIZE = 112;
  const NO_CELL = 0xFFFFFFFF;
  const out = new ArrayBuffer(PLAN_SIZE);
  const bytes = new Uint8Array(out);
  bytes.set(new Uint8Array(planBuffer, 0, 40));
  const dv = new DataView(out);
  const obs = new Uint8Array(observationBuffer);
  const width = new DataView(observationBuffer).getUint32(8, true);
  const height = new DataView(observationBuffer).getUint32(12, true);
  const mines = new DataView(observationBuffer).getUint32(16, true);
  let hidden = 0;
  for (let i = 0; i < width * height; i++) hidden += obs[32 + i] === 0xFF ? 1 : 0;
  const avoid = new Set(spec.avoid || []);
  let cell = spec.cell;
  if (cell === undefined) {
    cell = NO_CELL;
    for (let i = 0; i < width * height; i++) {
      if (obs[32 + i] === 0xFF && !avoid.has(i)) {
        cell = i;
        break;
      }
    }
  }
  const status = { none: 0, exact: 1, estimated: 2, unavailable: 3 }[spec.status];
  const reasons = { certain_moves: 1, finished: 2, no_samples: 3, budget: 4, insufficient_rollouts: 5,
    posterior_unavailable: 6, not_started: 7 };
  const set = (offset, value) => dv.setUint32(offset, value, true);
  set(8, status);
  set(12, spec.reason ? reasons[spec.reason] : 0);
  if (status === 1) {
    const total = spec.total ?? 4;
    const wins = spec.wins ?? 3;
    set(40, cell);
    set(44, spec.candidates ?? hidden - mines + 1);
    set(48, total);
    set(60, spec.nodes ?? 57);
    set(64, 1);
    dv.setFloat64(72, spec.survival ?? 0.75, true);
    dv.setFloat64(80, wins / total, true);
    dv.setFloat64(96, 3.5, true);
    set(104, wins);
    set(108, total);
  } else if (status === 2) {
    set(40, cell);
    set(44, spec.candidates ?? 4);
    set(48, spec.layouts ?? 96);
    set(52, spec.trials ?? 32);
    set(56, spec.incomplete ?? 0);
    dv.setFloat64(72, spec.survival ?? 0.7, true);
    dv.setFloat64(80, spec.win ?? 0.5, true);
    dv.setFloat64(88, spec.se ?? 0.05, true);
    dv.setFloat64(96, 900, true);
  } else {
    set(40, NO_CELL);
    dv.setFloat64(96, 2, true);
  }
  if (spec.corrupt === 'hash') dv.setUint32(32, dv.getUint32(32, true) ^ 1, true);
  return out;
}

/*
 * The bytes of a WebAssembly module that exports one page of memory and a
 * function () -> i32 for each name in `exportNames`: ms_abi_version
 * answers `abiVersion`, every other one 0. It compiles and passes the
 * page's export audit, then fails to start with abi_mismatch, like an
 * engine build from another release.
 */
export function wrongAbiModule(exportNames, abiVersion = 2) {
  const leb = (n) => {
    const out = [];
    do {
      let byte = n & 0x7f;
      n >>>= 7;
      if (n) byte |= 0x80;
      out.push(byte);
    } while (n);
    return out;
  };
  const sleb = (n) => {
    const out = [];
    for (;;) {
      const byte = n & 0x7f;
      n >>= 7;
      if ((n === 0 && !(byte & 0x40)) || (n === -1 && (byte & 0x40))) return [...out, byte];
      out.push(byte | 0x80);
    }
  };
  const name = (text) => [...leb(text.length), ...new TextEncoder().encode(text)];
  const section = (id, body) => [id, ...leb(body.length), ...body];
  const vec = (items) => [...leb(items.length), ...items.flat()];
  const bodies = exportNames.map((exportName) => {
    const body = [0x00, 0x41, ...sleb(exportName === 'ms_abi_version' ? abiVersion : 0), 0x0b]; // i32.const, end
    return [...leb(body.length), ...body];
  });
  return new Uint8Array([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    ...section(1, vec([[0x60, 0x00, 0x01, 0x7f]])), // type 0: () -> i32
    ...section(3, vec(exportNames.map(() => [0x00]))),
    ...section(5, vec([[0x00, 0x01]])), // memory: min 1 page
    ...section(7, vec([[...name('memory'), 0x02, 0x00],
      ...exportNames.map((exportName, index) => [...name(exportName), 0x00, ...leb(index)])])),
    ...section(10, vec(bodies))]);
}
