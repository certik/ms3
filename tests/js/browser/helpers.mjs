// Shared helpers for the browser specs. Everything here is test-side
// instrumentation: the site itself has no test hooks.
import { expect } from '@playwright/test';
import { forgeResult } from '../support/forge.mjs';

export { ROOT_URL, NESTED_URL, NESTED_BASE, REACTOR_FILE, VENDOR_PATHS } from './site.mjs';

// Deterministic game entropy: the app seeds each new game from
// crypto.getRandomValues(new Uint32Array(2)); here game k of a page load gets
// (seed + k, 0x5eed), so a reload replays the same layouts for the same
// first clicks. Other uses of getRandomValues are untouched.
export async function fixEntropy(page, seed = 1) {
  await page.addInitScript((first) => {
    let next = first;
    const original = crypto.getRandomValues.bind(crypto);
    crypto.getRandomValues = (array) => {
      if (array instanceof Uint32Array && array.length === 2) {
        array[0] = next++;
        array[1] = 0x5eed;
        return array;
      }
      return original(array);
    };
  }, seed);
}

// Counts solver workers, solve requests and terminations without changing
// what they do.
export async function instrumentWorkers(page) {
  await page.addInitScript(() => {
    const Native = window.Worker;
    const stats = { created: 0, terminated: 0, solves: 0 };
    window.__msWorkerStats = stats;
    window.Worker = class extends Native {
      constructor(url, options) {
        super(url, options);
        stats.created++;
      }

      postMessage(message, transfer) {
        if (message && message.type === 'solve') stats.solves++;
        return transfer === undefined ? super.postMessage(message) : super.postMessage(message, transfer);
      }

      terminate() {
        stats.terminated++;
        return super.terminate();
      }
    };
  });
}

export function workerStats(page) {
  return page.evaluate(() => Object.assign({}, window.__msWorkerStats));
}

// Console errors/warnings, page errors and every request URL (dedicated
// worker requests included).
export function track(page) {
  const problems = [];
  const requests = [];
  page.on('console', (message) => {
    if (message.type() === 'error' || message.type() === 'warning') problems.push(message.type() + ': ' + message.text());
  });
  page.on('pageerror', (error) => problems.push('pageerror: ' + error.message));
  page.on('request', (request) => requests.push(request.url()));
  return { problems, requests };
}

/*
 * Replaces the solver worker script for one test:
 *   { status: 404 }   the worker script cannot be loaded
 *   { delayMs }       every solve first blocks the worker thread (a slow,
 *                     uninterruptible WASM call)
 *   { trap: true }    ms_solve_observation throws like a trap
 *   { inconsistent: true }  ms_solve_observation reports
 *                     MS_ERR_INCONSISTENT (48), as for clues no layout fits
 *   { forge: spec }   the worker's result is replaced by forgeResult(spec)
 *                     (support/forge.mjs): C still validates it before
 *                     anything is shown
 *   { corrupt: 'hash' }  the result names another observation (C refuses it)
 * The real probability-worker.js still runs; a patch module is imported
 * before it.
 */
export async function patchWorker(page, options) {
  await page.route('**/probability-worker.js', (route) => {
    if (options.status) {
      return route.fulfill({ status: options.status, contentType: 'text/plain', body: 'blocked by the test' });
    }
    return route.fulfill({
      status: 200,
      contentType: 'text/javascript',
      body: "import './ms-test-worker-patch.js';\nimport './probability-worker.js?real';\n"
    });
  });
  await page.route('**/ms-test-worker-patch.js', (route) => route.fulfill({
    status: 200,
    contentType: 'text/javascript',
    body: patchSource(options)
  }));
}

export async function unpatchWorker(page) {
  await page.unroute('**/probability-worker.js');
  await page.unroute('**/ms-test-worker-patch.js');
}

function patchSource(options) {
  return `const OPTIONS = ${JSON.stringify(options)};
let observation = null;
self.addEventListener('message', (event) => {
  const data = event.data;
  if (!data || data.type !== 'solve') return;
  observation = data.observation;
  if (OPTIONS.delayMs) {
    const end = Date.now() + OPTIONS.delayMs;
    while (Date.now() < end) { /* an uninterruptible solve */ }
  }
});
if (OPTIONS.trap || OPTIONS.inconsistent) {
  const instantiate = WebAssembly.instantiate;
  WebAssembly.instantiate = async (...args) => {
    const instance = await instantiate(...args);
    return { exports: Object.assign({}, instance.exports, {
      ms_solve_observation: () => {
        if (OPTIONS.inconsistent) return 48;
        throw new WebAssembly.RuntimeError('unreachable');
      }
    }) };
  };
}
if (OPTIONS.forge || OPTIONS.corrupt) {
  const post = self.postMessage.bind(self);
  self.postMessage = (message, transfer) => {
    if (message && message.type === 'result') {
      const result = OPTIONS.forge ? forgeResult(message.result, observation, OPTIONS.forge) : message.result;
      if (OPTIONS.corrupt === 'hash') {
        const dv = new DataView(result);
        dv.setUint32(32, dv.getUint32(32, true) ^ 1, true);
      }
      return post(Object.assign({}, message, { result: result }), [result]);
    }
    return post(message, transfer);
  };
}
${forgeResult.toString()}
`;
}

// ------------------------------------------------------------------ board

export async function openApp(page, url = '/') {
  await page.goto(url);
  await expectReady(page);
}

export async function expectReady(page) {
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'ready');
  await expect(page.locator('#board .cell').first()).toBeVisible();
}

export function cell(page, index) {
  return page.locator(`#board .cell[data-index="${index}"]`);
}

export function boardStatus(page) {
  return page.locator('#board-frame').getAttribute('data-status');
}

export function boardInfo(page) {
  return page.evaluate(() => Array.from(document.querySelectorAll('#board .cell'), (node) => {
    const c = node.classList;
    const odds = node.querySelector('.odds');
    return {
      revealed: c.contains('is-revealed'),
      hidden: c.contains('is-hidden'),
      flagged: c.contains('is-flagged'),
      mine: c.contains('is-mine') || c.contains('is-auto-flag'),
      exploded: c.contains('is-exploded'),
      wrongFlag: c.contains('is-wrong-flag'),
      provenSafe: c.contains('is-proven-safe'),
      provenMine: c.contains('is-proven-mine'),
      estimate: c.contains('is-estimate'),
      odds: odds ? odds.textContent : null,
      text: node.textContent,
      label: node.getAttribute('aria-label')
    };
  }));
}

export async function startPreset(page, preset) {
  await page.locator(`.preset[data-preset="${preset}"]`).click();
  await expectReady(page);
}

export async function startCustom(page, width, height, mines) {
  if ((await page.locator('#custom-form').isHidden())) await page.locator('#custom-toggle').click();
  await page.locator('#custom-width').fill(String(width));
  await page.locator('#custom-height').fill(String(height));
  await page.locator('#custom-mines').fill(String(mines));
  await page.locator('#custom-start').click();
  await expect(page.locator('#game-meta')).toContainText(`${width} x ${height}`);
}

// Plays the current game from `first` by revealing the lowest hidden cell
// until it ends, and returns the mine layout the end screen discloses. With
// fixEntropy() a reload followed by the same first click recreates it.
export async function learnLayout(page, first) {
  await cell(page, first).click();
  for (let step = 0; step < 6400; step++) {
    const status = await boardStatus(page);
    if (status === 'won' || status === 'lost') break;
    const info = await boardInfo(page);
    const next = info.findIndex((c) => c.hidden && !c.flagged);
    await cell(page, next).click();
  }
  // Lost: mines are drawn as mines; won: the engine flagged every mine (no
  // flags are placed while learning).
  const info = await boardInfo(page);
  return info.flatMap((c, i) => (c.mine || (c.flagged && !c.wrongFlag) ? [i] : []));
}

export async function oddsSettled(page) {
  await expect(page.locator('#odds-panel')).not.toHaveAttribute('data-phase', /^(loading|waiting)$/);
  return page.locator('#odds-panel').getAttribute('data-phase');
}

export async function enableOdds(page) {
  if ((await page.locator('#odds-toggle').getAttribute('aria-checked')) !== 'true') {
    await page.locator('#odds-toggle').click();
  }
}

export async function enableAutosolve(page) {
  if ((await page.locator('#autosolve-toggle').getAttribute('aria-checked')) !== 'true') {
    await page.locator('#autosolve-toggle').click();
  }
}

export function neighbors(index, width, height) {
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
