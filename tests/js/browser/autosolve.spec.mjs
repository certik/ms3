// Autosolve in the browser. The C engine applies only the proofs of the
// result it accepted for the current revision, one atomic batch at a time.
// Most tests control the solver's answer: learnLayout() reads a game's
// layout, a reload replays that game, and the patched worker answers with an
// exact "oracle" result for it (helpers.mjs, forgeResult), which C validates
// like any other. Tests tagged @solver use the real production solver.
import { test, expect } from '@playwright/test';
import {
  boardInfo, cell, enableAutosolve, expectReady, fixEntropy, instrumentWorkers, learnLayout, neighbors, openApp,
  patchWorker, startCustom, track, workerStats
} from './helpers.mjs';

const W = 9;
const FIRST = 40;

function clue(index, mines) {
  return neighbors(index, W, W).filter((j) => mines.has(j)).length;
}

// Safe cells no flood can reach: none of their neighbors is a safe zero.
function isolatedSafe(mines) {
  const out = [];
  for (let i = 0; i < W * W; i++) {
    if (i === FIRST || mines.has(i) || neighbors(FIRST, W, W).includes(i)) continue;
    if (neighbors(i, W, W).every((j) => mines.has(j) || clue(j, mines) > 0)) out.push(i);
  }
  return out;
}

// A replayable beginner game (seed 5) with two isolated safe cells to leave
// open and one mine to leave unflagged.
async function knownGame(page) {
  await fixEntropy(page, 5);
  await openApp(page);
  const mines = new Set(await learnLayout(page, FIRST));
  expect(mines.size).toBe(10);
  const isolated = isolatedSafe(mines);
  expect(isolated.length, 'seed 5 leaves two isolated safe cells').toBeGreaterThanOrEqual(2);
  return { mines, layout: [...mines], s1: isolated[0], s2: isolated[1], m: [...mines][0] };
}

async function replay(page) {
  await page.reload();
  await expectReady(page);
}

const status = (page) => page.locator('#autosolve-status');

test('autosolve waits for the first reveal', async ({ page }) => {
  await instrumentWorkers(page);
  await openApp(page);
  await enableAutosolve(page);
  await expect(page.locator('#odds-toggle')).toHaveAttribute('aria-checked', 'true');
  await expect(status(page)).toHaveText('Choose your first cell. Autosolve will then play only certain moves.');
  await page.waitForTimeout(700);
  await expect(page.locator('#board .cell.is-revealed')).toHaveCount(0);
  await expect(page.locator('#board .cell.is-flagged')).toHaveCount(0);
  expect((await workerStats(page)).solves).toBe(0);
  await page.reload();
  await expectReady(page);
  await expect(page.locator('#autosolve-toggle')).toHaveAttribute('aria-checked', 'true');
  await expect(status(page)).toHaveText(/Choose your first cell/);
});

test('plays proven batches, pauses at ambiguity with odds visible, resumes after a surviving move',
  async ({ page }) => {
    const { problems } = track(page);
    const game = await knownGame(page);
    await instrumentWorkers(page);
    await patchWorker(page, { forge: { layout: game.layout, ambiguous: [game.m, game.s1, game.s2], until: game.s1 } });
    await replay(page);
    await enableAutosolve(page);
    await cell(page, FIRST).click();
    await expect(status(page)).toHaveText('Your move: choose a cell using its mine odds. If you survive, ' +
      'autosolve continues.');
    for (const index of [game.m, game.s1, game.s2]) {
      await expect(cell(page, index).locator('.odds')).toHaveText('50%');
      await expect(cell(page, index)).not.toHaveClass(/is-flagged/);
    }
    const paused = await boardInfo(page);
    expect(paused.filter((c) => c.flagged).length).toBe(9);
    expect(paused.filter((c) => c.hidden).length).toBe(12);
    await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'playing');
    await cell(page, game.s1).click();
    await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'won');
    await expect(cell(page, game.s2)).toHaveClass(/is-revealed/);
    await expect(status(page)).toHaveText('Game over. Autosolve never chooses uncertain cells.');
    expect(problems).toEqual([]);
  });

test('a proven-safe cell under a wrong flag is unflagged and opened', async ({ page }) => {
  const game = await knownGame(page);
  await patchWorker(page, { forge: { layout: game.layout } });
  await replay(page);
  await cell(page, FIRST).click();
  await cell(page, game.s2).click({ button: 'right' });
  await expect(cell(page, game.s2)).toHaveClass(/is-flagged/);
  await enableAutosolve(page);
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'won');
  await expect(cell(page, game.s2)).toHaveClass(/is-revealed/);
});

test('sampled 0% and 100% estimates are never played', async ({ page }) => {
  await fixEntropy(page, 7);
  await openApp(page);
  await cell(page, FIRST).click();
  const hidden = (await boardInfo(page)).flatMap((c, i) => (c.hidden ? [i] : []));
  const [zero, one] = hidden;
  await patchWorker(page, { forge: { status: 'approximate', default: 0.5, values: { [zero]: 0, [one]: 1 } } });
  await enableAutosolve(page);
  await expect(page.locator('#odds-badge')).toHaveText('Estimated');
  await expect(status(page)).toHaveText(/^Your move: choose a cell/);
  await page.waitForTimeout(500);
  await expect(cell(page, zero)).toHaveClass(/is-hidden/);
  await expect(cell(page, zero).locator('.odds')).toHaveText('~0%');
  await expect(cell(page, one)).not.toHaveClass(/is-flagged/);
  expect((await boardInfo(page)).filter((c) => c.hidden).length).toBe(hidden.length);
});

test('autosolve pauses visibly when the solver fails, without moves', async ({ page }) => {
  await fixEntropy(page, 7);
  await patchWorker(page, { status: 404 });
  await openApp(page);
  await enableAutosolve(page);
  await cell(page, FIRST).click();
  await expect(page.locator('#odds-badge')).toHaveText('Error');
  await expect(status(page)).toHaveText('Autosolve is waiting for valid odds. Retry odds or keep playing.');
  const revealed = (await boardInfo(page)).filter((c) => c.revealed).length;
  await page.waitForTimeout(500);
  expect((await boardInfo(page)).filter((c) => c.revealed).length).toBe(revealed);
  await expect(page.locator('#board .cell.is-flagged')).toHaveCount(0);
});

test('a move during a slow solve wins; the abandoned solve never plays', async ({ page }) => {
  const game = await knownGame(page);
  await instrumentWorkers(page);
  await patchWorker(page, { delayMs: 2500, forge: { layout: game.layout, ambiguous: [game.m, game.s1, game.s2] } });
  await replay(page);
  await enableAutosolve(page);
  await cell(page, FIRST).click();
  await expect.poll(async () => (await workerStats(page)).solves).toBe(1);
  const started = Date.now();
  await cell(page, game.s1).click(); // a surviving manual move while the first solve blocks its worker
  await expect(cell(page, game.s1)).toHaveClass(/is-revealed/);
  expect(Date.now() - started).toBeLessThan(1500);
  await expect.poll(async () => (await workerStats(page)).terminated).toBeGreaterThanOrEqual(1);
  await expect(status(page)).toHaveText(/^Your move: choose a cell/, { timeout: 20000 });
  // The abandoned solve, the solve for the manual move, and the one after
  // the batch it allowed.
  expect((await workerStats(page)).solves).toBe(3);
  await expect(cell(page, game.s2).locator('.odds')).toHaveText('50%');
});

test('turning autosolve off before the answer arrives plays nothing', async ({ page }) => {
  const game = await knownGame(page);
  await patchWorker(page, { delayMs: 1500, forge: { layout: game.layout } });
  await replay(page);
  await enableAutosolve(page);
  await cell(page, FIRST).click();
  await expect(page.locator('#odds-badge')).toHaveText('Calculating');
  const before = await boardInfo(page);
  await page.locator('#autosolve-toggle').click();
  await expect(page.locator('#autosolve-toggle')).toHaveAttribute('aria-checked', 'false');
  await expect(status(page)).toBeHidden();
  await expect(page.locator('#odds-badge')).toHaveText('Exact', { timeout: 15000 });
  await expect(page.locator('#board .cell.is-proven-safe').first()).toBeVisible();
  await page.waitForTimeout(300);
  const after = await boardInfo(page);
  expect(after.map((c) => [c.revealed, c.flagged])).toEqual(before.map((c) => [c.revealed, c.flagged]));
});

// Seed 8 needs four proven batches and then a genuine guess; exact proofs
// are determined by the position, so any correct solver pauses there.
test('@solver the real solver drives autosolve until a guess is needed', async ({ page }) => {
  const { problems } = track(page);
  await fixEntropy(page, 8);
  await instrumentWorkers(page);
  await openApp(page);
  const mines = new Set(await learnLayout(page, FIRST));
  await replay(page);
  await enableAutosolve(page);
  await cell(page, FIRST).click();
  const done = page.locator('#board-frame[data-status="won"]');
  const pause = status(page).filter({ hasText: /^(Your move|No more proven moves)/ });
  await expect(pause).toBeVisible({ timeout: 20000 });
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  const info = await boardInfo(page);
  expect(info.filter((c) => c.flagged).length, 'proven mines were flagged by batches').toBeGreaterThan(0);
  const fractional = info.flatMap((c, i) => (c.hidden && !c.flagged && /%$/.test(c.odds || '') ? [i] : []));
  expect(fractional.length, 'the pause leaves fractional odds visible').toBeGreaterThan(0);
  expect(info.every((c, i) => !c.flagged || mines.has(i)), 'only proven mines are flagged').toBe(true);
  const safe = fractional.find((i) => !mines.has(i));
  const revealed = info.filter((c) => c.revealed).length;
  const solves = (await workerStats(page)).solves;
  await cell(page, safe).click();
  await expect.poll(async () => (await boardInfo(page)).filter((c) => c.revealed).length).toBeGreaterThan(revealed);
  // Autosolve continues from the new position: it is solved again and then
  // either played further or paused again, never guessed.
  await expect.poll(async () => (await workerStats(page)).solves).toBeGreaterThan(solves);
  await expect(page.locator('#odds-panel')).toHaveAttribute('data-phase', /result|ended/, { timeout: 20000 });
  await expect(done.or(pause)).toBeVisible({ timeout: 20000 });
  const after = await boardInfo(page);
  expect(after.every((c, i) => !c.exploded && (!c.flagged || mines.has(i)))).toBe(true);
  expect(problems).toEqual([]);
});

test('@solver autosolve on an 80x80 board keeps the page responsive', async ({ page }) => {
  await fixEntropy(page, 4);
  await openApp(page);
  await startCustom(page, 80, 80, 800);
  await enableAutosolve(page);
  await cell(page, 3240).click();
  const gaps = await page.evaluate(() => new Promise((resolve) => {
    const result = [];
    let last = performance.now();
    const end = last + 4000;
    const tick = () => {
      const now = performance.now();
      result.push(now - last);
      last = now;
      if (now < end) setTimeout(tick, 10);
      else resolve(result);
    };
    setTimeout(tick, 10);
  }));
  expect(Math.max(...gaps)).toBeLessThan(1000);
  await expect(page.locator('#board .cell.is-revealed').first()).toBeVisible();
});
