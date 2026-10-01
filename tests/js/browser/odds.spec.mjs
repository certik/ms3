// Mine-odds behavior in the browser: presentation of exact, estimated and
// unavailable answers, caching and Retry, solver failures, and moves racing a
// slow solver. Tests tagged @solver need the real production solver; the
// others control the solver's answer through a patched worker (helpers.mjs),
// which the C engine still validates before anything is shown.
import { test, expect } from '@playwright/test';
import {
  boardInfo, cell, enableOdds, expectReady, fixEntropy, instrumentWorkers, learnLayout, neighbors, oddsSettled,
  openApp, patchWorker, startCustom, track, unpatchWorker, workerStats
} from './helpers.mjs';

async function opened(page, seed = 7) {
  await fixEntropy(page, seed);
  await instrumentWorkers(page);
  await openApp(page);
  await cell(page, 40).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'playing');
  const info = await boardInfo(page);
  return info.flatMap((c, i) => (c.hidden ? [i] : []));
}

test('before the first reveal the panel explains why there are no odds yet', async ({ page }) => {
  await instrumentWorkers(page);
  await openApp(page);
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Not started');
  await expect(page.locator('#odds-message')).toHaveText(/the mines are placed when you make your first reveal/);
  expect((await workerStats(page)).solves).toBe(0);
});

test('@solver exact odds appear after the first reveal', async ({ page }) => {
  const { problems } = track(page);
  await opened(page);
  await enableOdds(page);
  expect(await oddsSettled(page)).toBe('result');
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  await expect(page.locator('#odds-message')).toHaveText(/^Exact probabilities for \d+ unrevealed cells/);
  await expect(page.locator('#board .cell.has-odds').first()).toBeVisible();
  await expect(page.locator('#odds-meta')).toContainText('solve time');
  expect(problems).toEqual([]);
});

test('near-certain exact odds read <1% and >99%; only proofs get certainty icons', async ({ page }) => {
  const hidden = await opened(page);
  const [low, high, safe, mine] = hidden;
  await patchWorker(page, { forge: { status: 'exact', default: 0.3,
    values: { [low]: 0.004, [high]: 0.996, [safe]: 0, [mine]: 1 } } });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  await expect(cell(page, low).locator('.odds')).toHaveText('<1%');
  await expect(cell(page, high).locator('.odds')).toHaveText('>99%');
  await expect(cell(page, low)).not.toHaveClass(/is-proven/);
  await expect(cell(page, high)).not.toHaveClass(/is-proven/);
  await expect(cell(page, low)).toHaveAttribute('aria-label', /mine chance less than 1 percent, exact/);
  await expect(cell(page, high)).toHaveAttribute('aria-label', /mine chance more than 99 percent, exact/);
  await expect(cell(page, safe)).toHaveClass(/is-proven-safe/);
  await expect(cell(page, mine)).toHaveClass(/is-proven-mine/);
  await expect(cell(page, hidden[4]).locator('.odds')).toHaveText('30%');
  await expect(page.locator('#odds-message')).toContainText('Certain: 1 safe cell(s), 1 mine(s).');
});

test('sampled endpoints read ~0% and ~100% and never look proven', async ({ page }) => {
  const hidden = await opened(page);
  const [zero, one] = hidden;
  await patchWorker(page, { forge: { status: 'approximate', default: 0.4, values: { [zero]: 0, [one]: 1 } } });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Estimated');
  await expect(cell(page, zero).locator('.odds')).toHaveText('~0%');
  await expect(cell(page, one).locator('.odds')).toHaveText('~100%');
  for (const index of [zero, one]) {
    await expect(cell(page, index)).toHaveClass(/is-estimate/);
    await expect(cell(page, index)).not.toHaveClass(/is-proven/);
    await expect(cell(page, index).locator('.odds-icon')).toHaveCount(0);
  }
  await expect(cell(page, zero)).toHaveAttribute('aria-label', /estimated mine chance about 0 percent/);
  await expect(page.locator('#odds-message')).toHaveText(new RegExp('^Approximate probabilities: exact counting ' +
    'exceeded the budget for 1 of 1 group\\(s\\), estimated from 400 weighted samples \\(effective sample size 62\\)'));
  await expect(page.locator('#odds-meta')).toContainText('62 effective samples');
});

test('unavailable answers are recomputed on Retry, keeping their proofs visible', async ({ page }) => {
  const hidden = await opened(page);
  await patchWorker(page, { forge: { status: 'unavailable', values: { [hidden[0]]: 0 } } });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Unavailable');
  await expect(page.locator('#odds-message')).toHaveText('Probabilities unavailable: the time budget ran out before ' +
    'the result was complete. Certain: 1 safe cell(s), 0 mine(s).');
  await expect(cell(page, hidden[0])).toHaveClass(/is-proven-safe/);
  await expect(cell(page, hidden[1])).not.toHaveClass(/has-odds/);
  await expect(cell(page, hidden[1])).toHaveAttribute('aria-label', /mine chance unavailable/);
  await expect(page.locator('#odds-retry')).toBeVisible();
  expect((await workerStats(page)).solves).toBe(1);
  await page.locator('#odds-retry').click();
  await expect.poll(async () => (await workerStats(page)).solves).toBe(2);
  await expect(page.locator('#odds-badge')).toHaveText('Unavailable');
  expect((await workerStats(page)).created, 'the idle worker is reused for the fresh solve').toBe(1);
});

test('exact answers are reused for the position on screen', async ({ page }) => {
  const hidden = await opened(page);
  await patchWorker(page, { forge: { status: 'exact', default: 0.25 } });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  await expect(page.locator('#odds-retry')).toBeHidden();
  await page.locator('#odds-toggle').click();
  await expect(page.locator('#odds-badge')).toHaveText('Off');
  await expect(page.locator('#board .cell.has-odds')).toHaveCount(0);
  await page.locator('#odds-toggle').click();
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  await expect(cell(page, hidden[1]).locator('.odds')).toHaveText('25%');
  expect((await workerStats(page)).solves).toBe(1);
});

test('a solver that cannot load is reported, the game goes on, and Retry recovers', async ({ page }) => {
  await opened(page);
  await patchWorker(page, { status: 404 });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Error');
  await expect(page.locator('#odds-message')).toContainText('could not start');
  await expect(page.locator('#odds-message')).toContainText('Your game is not affected');
  await expect(page.locator('#odds-retry')).toBeVisible();
  const before = (await boardInfo(page)).filter((c) => c.flagged).length;
  const target = (await boardInfo(page)).findIndex((c) => c.hidden && !c.flagged);
  await cell(page, target).click({ button: 'right' });
  await expect(cell(page, target)).toHaveClass(/is-flagged/);
  expect((await boardInfo(page)).filter((c) => c.flagged).length).toBe(before + 1);
  await expect(page.locator('#odds-badge')).toHaveText('Error');
  await unpatchWorker(page);
  await patchWorker(page, { forge: { status: 'exact', default: 0.2 } });
  await page.locator('#odds-retry').click();
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'ready');
});

test('a solver trap is an explicit error and Retry starts a fresh worker', async ({ page }) => {
  await opened(page);
  await patchWorker(page, { trap: true });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Error');
  await expect(page.locator('#odds-message')).toContainText('stopped unexpectedly');
  await expect(page.locator('#board .cell.has-odds')).toHaveCount(0);
  const stats = await workerStats(page);
  expect(stats.terminated).toBeGreaterThanOrEqual(1);
  await unpatchWorker(page);
  await patchWorker(page, { forge: { status: 'exact', default: 0.2 } });
  await page.locator('#odds-retry').click();
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  expect((await workerStats(page)).created).toBe(stats.created + 1);
});

test('a worker answer that fails validation is only an odds error; Retry starts a fresh worker', async ({ page }) => {
  const hidden = await opened(page);
  await patchWorker(page, { corrupt: 'hash' });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Error');
  await expect(page.locator('#odds-message')).toContainText('does not fit this board, so it was not used');
  await expect(page.locator('#odds-message')).toContainText('Your game is not affected');
  await expect(page.locator('#board .cell.has-odds')).toHaveCount(0);
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'ready');
  await expect(page.locator('.notice')).toHaveCount(0);
  await expect(page.locator('#board')).toHaveAttribute('data-interactive', 'true');
  await expect.poll(async () => (await workerStats(page)).terminated).toBe(1);
  await cell(page, hidden[0]).click({ button: 'right' });
  await expect(cell(page, hidden[0])).toHaveClass(/is-flagged/);
  await expect(page.locator('#odds-badge')).toHaveText('Error');
  await expect.poll(async () => (await workerStats(page)).created).toBe(2);
  await unpatchWorker(page);
  await patchWorker(page, { forge: { status: 'exact', default: 0.3 } });
  await page.locator('#odds-retry').click();
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  expect((await workerStats(page)).created, 'the rejected worker was replaced').toBe(3);
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'ready');
});

test('a solver that reports an inconsistent position is a visible, retryable error', async ({ page }) => {
  await opened(page);
  await patchWorker(page, { inconsistent: true });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Error');
  await expect(page.locator('#odds-message')).toHaveText('The revealed numbers and the mine total admit no mine ' +
    'layout. Your game is not affected; retry odds, or keep playing (odds are calculated again after your next move).');
  await expect(page.locator('#board .cell.has-odds')).toHaveCount(0);
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'ready');
  expect((await workerStats(page)).terminated, 'a reported error keeps the worker').toBe(0);
  await unpatchWorker(page);
  await page.locator('#odds-retry').click();
  await expect.poll(async () => (await workerStats(page)).solves).toBe(2);
  await expect(page.locator('#odds-badge')).toHaveText('Error');
  const target = (await boardInfo(page)).findIndex((c) => c.hidden && !c.flagged);
  await cell(page, target).click({ button: 'right' });
  await expect(cell(page, target)).toHaveClass(/is-flagged/);
});

test('a move completes while a slow solve runs; the obsolete solve is abandoned', async ({ page }) => {
  const hidden = await opened(page);
  await patchWorker(page, { delayMs: 4000, forge: { status: 'exact', default: 0.35 } });
  await enableOdds(page);
  await expect(page.locator('#odds-badge')).toHaveText('Calculating');
  await expect.poll(async () => (await workerStats(page)).solves).toBe(1);
  const target = hidden[hidden.length - 1];
  const started = Date.now();
  await cell(page, target).click({ button: 'right' });
  await expect(cell(page, target)).toHaveClass(/is-flagged/);
  expect(Date.now() - started, 'the move did not wait for the 4 s solve').toBeLessThan(1500);
  await expect.poll(async () => (await workerStats(page)).terminated).toBeGreaterThanOrEqual(1);
  await expect(page.locator('#odds-badge')).toHaveText('Calculating');
  await expect(page.locator('#odds-badge')).toHaveText('Exact', { timeout: 15000 });
  expect((await workerStats(page)).solves).toBe(2);
  await expect(cell(page, target)).toHaveClass(/is-flagged/);
  await expect(cell(page, hidden[0]).locator('.odds')).toHaveText('35%');
});

test('turning odds off or starting a new game drops a slow solve for good', async ({ page }) => {
  await opened(page);
  await patchWorker(page, { delayMs: 2500, forge: { status: 'exact', default: 0.45 } });
  await enableOdds(page);
  await expect.poll(async () => (await workerStats(page)).solves).toBe(1);
  await page.locator('#odds-toggle').click();
  await expect(page.locator('#odds-badge')).toHaveText('Off');
  await expect.poll(async () => (await workerStats(page)).terminated).toBe(1);
  await page.waitForTimeout(3000);
  await expect(page.locator('#board .cell.has-odds')).toHaveCount(0);
  await expect(page.locator('#odds-badge')).toHaveText('Off');

  await page.locator('#odds-toggle').click();
  await expect.poll(async () => (await workerStats(page)).solves).toBe(2);
  await page.locator('#new-game').click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
  await expect.poll(async () => (await workerStats(page)).terminated).toBe(2);
  await page.waitForTimeout(3000);
  await expect(page.locator('#board .cell.has-odds')).toHaveCount(0);
  await expect(page.locator('#odds-badge')).toHaveText('Not started');
});

test('@solver an 80x80 position is solved off the main thread', async ({ page }) => {
  const { problems } = track(page);
  await fixEntropy(page, 3);
  await instrumentWorkers(page);
  await openApp(page);
  await startCustom(page, 80, 80, 1200);
  await cell(page, 3240).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
  await enableOdds(page);
  // The main thread keeps answering while the worker solves.
  const gaps = await page.evaluate(() => new Promise((resolve) => {
    const result = [];
    let last = performance.now();
    const end = last + 1500;
    const tick = () => {
      const now = performance.now();
      result.push(now - last);
      last = now;
      if (now < end) setTimeout(tick, 10);
      else resolve(result);
    };
    setTimeout(tick, 10);
  }));
  // Rendering 6400 cells may take a frame or two; a solve on this thread
  // would block for up to its 1.5 s budget.
  expect(Math.max(...gaps)).toBeLessThan(500);
  expect(['result', 'ended']).toContain(await oddsSettled(page));
  expect(problems).toEqual([]);
  await expectReady(page);
});

// The baseline's hard case: clues on a 2x2 lattice form one huge component,
// far beyond exact counting, so the real solver stops at its budgets. Only
// lattice clues that flood nothing are opened, so the game shows the same
// kind of observation as the baseline built directly.
test('@solver a hard 80x80 lattice position is budget-limited and never blocks input', async ({ page }) => {
  test.setTimeout(180000);
  const { problems } = track(page);
  await fixEntropy(page, 4);
  await instrumentWorkers(page);
  await openApp(page);
  await startCustom(page, 80, 80, 1280);
  await page.reload(); // the saved 80x80 settings now start the page's first (seeded) game
  await expectReady(page);
  const mines = new Set(await learnLayout(page, 3240));
  await page.reload();
  await expectReady(page);
  await cell(page, 3240).click();
  const clue = (i) => neighbors(i, 80, 80).filter((j) => mines.has(j)).length;
  const lattice = [];
  for (let r = 0; r < 80; r += 2) {
    for (let c = 0; c < 80; c += 2) {
      const i = r * 80 + c;
      if (!mines.has(i) && clue(i) > 0) lattice.push(i);
    }
  }
  await page.evaluate((cells) => {
    for (const index of cells) {
      const node = document.querySelector(`#board .cell[data-index="${index}"]`);
      if (node.classList.contains('is-hidden') && !node.classList.contains('is-flagged')) node.click();
    }
  }, lattice);
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'playing');
  // Record main-thread scheduling gaps from before the first solve until
  // the answer is shown.
  await page.evaluate(() => {
    const gaps = [];
    let last = performance.now();
    window.__msGaps = gaps;
    window.__msGapTimer = setInterval(() => {
      const now = performance.now();
      gaps.push(now - last);
      last = now;
    }, 10);
  });
  await enableOdds(page);
  const target = (await boardInfo(page)).findIndex((c, i) => c.hidden && !c.flagged && i >= 80 * 70);
  const started = Date.now();
  await cell(page, target).click({ button: 'right' });
  await expect(cell(page, target)).toHaveClass(/is-flagged/);
  expect(Date.now() - started, 'a move never waits for the solver').toBeLessThan(1000);
  await expect(page.locator('#odds-panel')).toHaveAttribute('data-phase', 'result', { timeout: 30000 });
  const maxGap = await page.evaluate(() => {
    clearInterval(window.__msGapTimer);
    return Math.max(...window.__msGaps);
  });
  expect(maxGap, 'no solve ran on the main thread').toBeLessThan(1000);
  await expect(page.locator('#odds-badge')).toHaveText(/^(Estimated|Unavailable)$/);
  const frontier = await page.locator('#odds-meta li', { hasText: 'frontier' }).textContent();
  expect(Number(frontier.replace(/[^0-9]/g, ''))).toBeGreaterThan(1000);
  expect((await workerStats(page)).solves).toBeGreaterThanOrEqual(1);
  const info = await boardInfo(page);
  expect(info.every((c, i) => !c.provenSafe || !mines.has(i)), 'no proven-safe mine').toBe(true);
  expect(info.every((c, i) => !c.provenMine || mines.has(i)), 'no proven mine that is safe').toBe(true);
  expect(problems).toEqual([]);
});
