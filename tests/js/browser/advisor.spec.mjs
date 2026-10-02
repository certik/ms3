// Move advice in the browser: when nothing certain is left to play, the
// worker plans the next reveal and the page marks one hidden cell and
// explains it below the board. Tests tagged @solver use the real production
// planner; the others control the plan through a patched worker
// (helpers.mjs patchWorker, support/forge.mjs forgePlan), which the C
// engine still checks (ms_check_plan) before anything is shown.
import { test, expect } from '@playwright/test';
import {
  advice, adviceSettled, boardInfo, cell, enableOdds, expectReady, fixEntropy, instrumentWorkers, openApp,
  patchWorker, startCustom, track, unpatchWorker, workerStats
} from './helpers.mjs';

const W = 9;

function rowCol(index) {
  return `row ${Math.floor(index / W) + 1}, column ${(index % W) + 1}`;
}

// The single marked cell, or -1.
async function advised(page) {
  const marked = await page.evaluate(() => Array.from(document.querySelectorAll('#board .cell.is-advised'),
    (node) => Number(node.dataset.index)));
  expect(marked.length).toBeLessThanOrEqual(1);
  return marked.length ? marked[0] : -1;
}

async function geometry(page) {
  return page.evaluate(() => {
    const box = (id) => {
      const { x, y, width, height } = document.getElementById(id).getBoundingClientRect();
      return { x, y, width, height };
    };
    const scroller = document.getElementById('board-scroller');
    return {
      frame: box('board-frame'),
      board: box('board'),
      pageScroll: [window.scrollX, window.scrollY],
      boardScroll: [scroller.scrollLeft, scroller.scrollTop]
    };
  });
}

// A playing beginner game (seed 7, first reveal 40) with assistance off.
async function opened(page, seed = 7) {
  await fixEntropy(page, seed);
  await instrumentWorkers(page);
  await openApp(page);
  await cell(page, 40).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'playing');
  return (await boardInfo(page)).flatMap((c, i) => (c.hidden ? [i] : []));
}

// Exact odds of 30% for every hidden cell: nothing is certain, so advice
// follows the odds.
const NO_PROOFS = { status: 'exact', default: 0.3 };

// Replaces the worker patch and replays the page's first game (fixEntropy):
// a reload is the only way to get a worker script the new patch applies to.
async function replay(page, options) {
  await unpatchWorker(page);
  await patchWorker(page, options);
  await page.reload();
  await expectReady(page);
  await cell(page, 40).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'playing');
}

test('@solver after autosolve pauses, the real advisor marks one hidden cell and leaves its odds alone',
  async ({ page }) => {
    const { problems } = track(page);
    await fixEntropy(page, 8);
    await instrumentWorkers(page);
    // The real planner, held back briefly so the odds can be read first.
    await patchWorker(page, { planDelayMs: 1200 });
    await page.goto('/');
    await expectReady(page);
    await cell(page, 40).click();
    await expect(page.locator('#autosolve-status')).toHaveText(/^Your move:/, { timeout: 20000 });
    await expect(advice(page)).toHaveAttribute('data-state', 'loading');
    await expect(page.locator('#advice-title')).toHaveText('Looking for a good next move...');
    await expect(page.locator('#odds-badge')).toHaveText('Exact');
    const before = await boardInfo(page);
    const layout = await geometry(page);
    expect(before.some((c) => c.hidden && !c.flagged && /%$/.test(c.odds || '')), 'odds stay visible').toBe(true);

    const state = await adviceSettled(page);
    expect(['exact', 'estimated']).toContain(state);
    const index = await advised(page);
    expect(index).toBeGreaterThanOrEqual(0);
    const after = await boardInfo(page);
    expect(after[index].hidden).toBe(true);
    expect(after.map((c) => [c.revealed, c.flagged, c.odds, c.provenSafe, c.provenMine]),
      'the marker changes no odds, proofs or cells').toEqual(before.map((c) => [c.revealed, c.flagged, c.odds,
      c.provenSafe, c.provenMine]));
    expect(await geometry(page), 'the board did not move').toEqual(layout);
    const label = state === 'exact' ? 'Best next move (exact)' : 'Suggested next move (estimate)';
    await expect(page.locator('#advice-title')).toHaveText(`${label}: ${rowCol(index)}.`);
    await expect(cell(page, index)).toHaveAttribute('aria-label', state === 'exact'
      ? /, best next move \(exact\)\. Row \d+, column \d+\.$/ : /, suggested next move \(estimate\)\. Row/);
    await expect(cell(page, index)).toHaveAttribute('title', /next move/);
    await expect(cell(page, index).locator('.odds')).toHaveText(before[index].odds);
    await expect(page.locator('#advice-detail')).toContainText(`Mine risk ${before[index].odds} (exact odds).`);
    if (state === 'exact') {
      await expect(page.locator('#advice-detail')).toContainText(/Chance to win with best play from here: .*\(exact: /);
    } else {
      await expect(page.locator('#advice-detail')).toContainText(/an estimate, not a guarantee/);
      await expect(page.locator('#advice-detail')).not.toContainText(/optimal|best play/i);
    }

    // Autosolve stays certainty-only: it never plays the suggestion.
    await page.waitForTimeout(800);
    const still = await boardInfo(page);
    expect(still.map((c) => [c.revealed, c.flagged])).toEqual(after.map((c) => [c.revealed, c.flagged]));
    expect((await workerStats(page)).plans).toBe(1);
    expect(problems).toEqual([]);
  });

test('exact advice is labeled exact and counts layouts; estimates never claim to be the best move',
  async ({ page }) => {
    const hidden = await opened(page);
    await patchWorker(page, { forge: NO_PROOFS, planForge: { status: 'exact', wins: 3, total: 4 } });
    await enableOdds(page);
    expect(await adviceSettled(page)).toBe('exact');
    const index = hidden[0];
    expect(await advised(page)).toBe(index);
    await expect(page.locator('#advice-title')).toHaveText(`Best next move (exact): ${rowCol(index)}.`);
    await expect(page.locator('#advice-detail')).toHaveText('Mine risk 30% (exact odds). Chance to win with best ' +
      'play from here: 75% (exact: wins in 3 of 4 possible layouts). Found by searching every layout that fits the ' +
      'clues; the best move is not always the lowest-risk cell.');
    await expect(cell(page, index)).toHaveAttribute('aria-label', /mine chance 30 percent, exact, best next move \(exact\)/);

    for (const [spec, text] of [
      [{ status: 'estimated', win: 0.5, se: 0.05, trials: 32 },
        'Estimated chance to win: about 50%, from 16 wins in 32 simulated games (standard error 5 points: a ' +
        'sampling diagnostic, not a guaranteed bound).'],
      // A round cut off by time: only the raw finished outcomes, no chance.
      [{ status: 'estimated', win: 0.25, se: 0.06, trials: 33, incomplete: 1 },
        'Simulated games: 8 of the 32 that finished were won, and 1 more ran out of time, so no overall chance ' +
        'to win is given.'],
      // Sampled wins above the cell's 70% chance of being safe are shown,
      // and explained, not refused.
      [{ status: 'estimated', win: 0.75, se: 0.08, trials: 32, survival: 0.55 },
        'Estimated chance to win: about 75%, from 24 wins in 32 simulated games (standard error 8 points: a ' +
        'sampling diagnostic, not a guaranteed bound). It is above this cell\'s chance of being safe only ' +
        'because of sampling noise.'],
      // One game on each listed layout: no sampling error, still no proof.
      [{ status: 'estimated', win: 0.5, se: 0, trials: 32, layouts: 32 },
        'Chance to win with the advisor\'s own play: 50%, from 16 wins in 32 simulated games, one on each ' +
        'possible layout (other play could win more often).'],
      // Every game won: the count, never a certainty.
      [{ status: 'estimated', win: 1, se: 0.02, trials: 32 },
        'All 32 simulated games were won. That does not make winning certain, so no chance to win is given.']
    ]) {
      await page.locator('#odds-toggle').click();
      await replay(page, { forge: NO_PROOFS, planForge: Object.assign({ avoid: [index] }, spec) });
      await enableOdds(page);
      expect(await adviceSettled(page)).toBe('estimated');
      const marked = await advised(page);
      await expect(page.locator('#advice-title')).toHaveText(`Suggested next move (estimate): ${rowCol(marked)}.`);
      await expect(page.locator('#advice-detail')).toHaveText('Mine risk 30% (exact odds). ' + text + ' Picked by ' +
        'simulating whole games on layouts sampled to fit the clues: an estimate, not a guarantee.');
      await expect(advice(page)).not.toContainText(/optimal|best/i);
      await expect(cell(page, marked)).toHaveAttribute('aria-label', /suggested next move \(estimate\)/);
    }
  });

test('@solver a real exact suggestion can be riskier than the lowest-risk cell', async ({ page }) => {
  const { problems } = track(page);
  // Seed 2026: when autosolve pauses, the best reveal wins 7 of 15 layouts
  // but is not the cell least likely to hide a mine.
  await fixEntropy(page, 2026);
  await page.goto('/');
  await expectReady(page);
  await cell(page, 40).click();
  await expect(page.locator('#autosolve-status')).toHaveText(/^Your move:/, { timeout: 20000 });
  expect(await adviceSettled(page)).toBe('exact');
  const index = await advised(page);
  const info = await boardInfo(page);
  const percent = (text) => Number(/^(\d+)%$/.exec(text || '')?.[1] ?? NaN);
  const shown = info.flatMap((c) => (c.hidden && !c.flagged && Number.isFinite(percent(c.odds)) ? [percent(c.odds)] : []));
  const lowest = Math.min(...shown);
  const risk = percent(info[index].odds);
  expect(risk, 'the suggestion is riskier than the lowest-risk cell').toBeGreaterThan(lowest);
  await expect(page.locator('#odds-detail')).toContainText(`Lowest risk among unproven cells: ${lowest}%.`);
  await expect(page.locator('#advice-title')).toHaveText(`Best next move (exact): ${rowCol(index)}.`);
  await expect(page.locator('#advice-detail')).toHaveText(`Mine risk ${risk}% (exact odds). Chance to win with ` +
    'best play from here: 47% (exact: wins in 7 of 15 possible layouts). Found by searching every layout that ' +
    'fits the clues; the best move is not always the lowest-risk cell.');
  expect(problems).toEqual([]);
});

test('there is no advice before the first reveal, while proofs remain, without odds or after the game',
  async ({ page }) => {
    await fixEntropy(page, 7);
    await instrumentWorkers(page);
    await page.goto('/');
    await expectReady(page);
    await expect(page.locator('#odds-toggle')).toHaveAttribute('aria-checked', 'true');
    await page.waitForTimeout(300);
    await expect(advice(page)).toBeHidden();
    await openApp(page, '/');
    await cell(page, 40).click();
    const hidden = (await boardInfo(page)).flatMap((c, i) => (c.hidden ? [i] : []));
    await replay(page, { forge: { status: 'exact', default: 0.3, values: { [hidden[0]]: 0 } },
      planForge: { status: 'exact' } });
    await enableOdds(page);
    await expect(page.locator('#odds-badge')).toHaveText('Exact');
    await expect(cell(page, hidden[0])).toHaveClass(/is-proven-safe/);
    await page.waitForTimeout(500);
    await expect(advice(page)).toBeHidden();
    expect((await workerStats(page)).plans, 'a proven-safe cell comes first').toBe(0);

    await page.locator('#odds-toggle').click();
    await replay(page, { forge: { status: 'unavailable', values: {} }, planForge: { status: 'exact' } });
    await enableOdds(page);
    await expect(page.locator('#odds-badge')).toHaveText('Unavailable');
    await page.waitForTimeout(500);
    await expect(advice(page)).toBeHidden();
    expect((await workerStats(page)).plans, 'no advice without odds').toBe(0);
    await page.locator('#odds-toggle').click();
    await expect(advice(page)).toBeHidden();

    // A dense board whose first reveal wins: nothing is left to advise.
    await enableOdds(page);
    await startCustom(page, 9, 9, 72);
    await cell(page, 40).click();
    await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'won');
    await page.waitForTimeout(300);
    await expect(advice(page)).toBeHidden();
    expect((await workerStats(page)).plans).toBe(0);
  });

test('a move, a new game or turning odds off drops the marker at once; flags keep the plan', async ({ page }) => {
  const hidden = await opened(page);
  await patchWorker(page, { forge: NO_PROOFS, planForge: { status: 'exact' } });
  await enableOdds(page);
  expect(await adviceSettled(page)).toBe('exact');
  const index = hidden[0];
  expect(await advised(page)).toBe(index);
  expect((await workerStats(page)).plans).toBe(1);

  // Odds off and on: gone at once, back without planning again.
  const offCount = await page.evaluate(() => {
    document.getElementById('odds-toggle').click();
    return document.querySelectorAll('#board .cell.is-advised').length;
  });
  expect(offCount).toBe(0);
  await expect(advice(page)).toBeHidden();
  await page.locator('#odds-toggle').click();
  expect(await adviceSettled(page)).toBe('exact');
  expect(await advised(page)).toBe(index);
  expect((await workerStats(page)).plans, 'the checked plan was reused').toBe(1);

  // A flag elsewhere is a new revision: the marker goes at once, then comes
  // back for the unchanged position without planning again.
  const other = hidden[hidden.length - 1];
  const afterFlag = await page.evaluate((target) => {
    const node = document.querySelector(`#board .cell[data-index="${target}"]`);
    node.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, button: 2 }));
    return document.querySelectorAll('#board .cell.is-advised').length;
  }, other);
  expect(afterFlag).toBe(0);
  await expect(cell(page, other)).toHaveClass(/is-flagged/);
  expect(await adviceSettled(page)).toBe('exact');
  expect(await advised(page)).toBe(index);
  expect((await workerStats(page)).plans).toBe(1);

  // Flagging the suggested cell keeps the flag and says to remove it first.
  await cell(page, index).click({ button: 'right' });
  await expect(cell(page, index)).toHaveClass(/is-flagged/);
  expect(await adviceSettled(page)).toBe('exact');
  expect(await advised(page)).toBe(index);
  await expect(page.locator('#advice-detail')).toContainText('It is flagged: remove your flag first to reveal it.');
  await expect(cell(page, index)).toHaveAttribute('aria-label',
    /^Flagged, mine chance 30 percent, exact, best next move \(exact\): remove the flag to reveal it\. Row/);
  await expect(cell(page, index)).toHaveClass(/is-flagged/);
  await cell(page, index).click({ button: 'right' });
  await expect(cell(page, index)).not.toHaveClass(/is-flagged/);
  expect(await adviceSettled(page)).toBe('exact');
  await expect(page.locator('#advice-detail')).not.toContainText('flagged');
  expect((await workerStats(page)).plans, 'flags never needed a new plan').toBe(1);

  // A reveal changes the position: the marker goes at once.
  const target = hidden[1];
  const afterReveal = await page.evaluate((t) => {
    document.querySelector(`#board .cell[data-index="${t}"]`).click();
    return document.querySelectorAll('#board .cell.is-advised').length;
  }, target);
  expect(afterReveal).toBe(0);

  // A new game: gone at once, and no advice before its first reveal.
  const afterNew = await page.evaluate(() => {
    document.getElementById('new-game').click();
    return document.querySelectorAll('#board .cell.is-advised').length;
  });
  expect(afterNew).toBe(0);
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
  await expect(advice(page)).toBeHidden();
});

test('moves stay instant while advice is planned; odds stay visible and the stale plan is dropped',
  async ({ page }) => {
    const hidden = await opened(page);
    await patchWorker(page, { forge: NO_PROOFS, planDelayMs: 4000, planForge: { status: 'exact' } });
    await enableOdds(page);
    await expect(advice(page)).toHaveAttribute('data-state', 'loading');
    await expect.poll(async () => (await workerStats(page)).plans).toBe(1);
    await expect(page.locator('#odds-badge')).toHaveText('Exact');
    await expect(cell(page, hidden[2]).locator('.odds')).toHaveText('30%');
    const target = hidden[hidden.length - 1];
    const started = Date.now();
    await cell(page, target).click({ button: 'right' });
    await expect(cell(page, target)).toHaveClass(/is-flagged/);
    expect(Date.now() - started, 'the flag did not wait for the 4 s plan').toBeLessThan(1500);
    await expect.poll(async () => (await workerStats(page)).terminated).toBeGreaterThanOrEqual(1);
    await expect(page.locator('#odds-badge')).toHaveText('Exact');
    await expect(cell(page, hidden[2]).locator('.odds')).toHaveText('30%');
    await expect(advice(page)).toHaveAttribute('data-state', 'loading');
    expect(await advised(page)).toBe(-1);
    expect(await adviceSettled(page)).toBe('exact');
    expect(await advised(page)).toBe(hidden[0]);
  });

test('advice failures are explicit and retryable without touching the game or its odds', async ({ page }) => {
  const hidden = await opened(page);
  await patchWorker(page, { forge: NO_PROOFS, planTrap: true });
  await enableOdds(page);
  expect(await adviceSettled(page)).toBe('error');
  await expect(page.locator('#advice-title')).toHaveText('No move advice');
  await expect(page.locator('#advice-detail')).toHaveText('The move advisor stopped unexpectedly. Your game and ' +
    'its odds are not affected.');
  await expect(page.locator('#advice-retry')).toBeVisible();
  await expect(page.locator('#odds-badge')).toHaveText('Exact');
  await expect(cell(page, hidden[0]).locator('.odds')).toHaveText('30%');
  await page.waitForTimeout(800);
  expect((await workerStats(page)).plans, 'a failure is never retried by itself').toBe(1);
  expect(await advised(page)).toBe(-1);

  await unpatchWorker(page);
  await patchWorker(page, { forge: NO_PROOFS, planForge: { status: 'exact', corrupt: 'hash' } });
  await page.locator('#advice-retry').click();
  expect(await adviceSettled(page)).toBe('error');
  await expect(page.locator('#advice-detail')).toHaveText('The move advisor returned a suggestion that does not fit ' +
    'this board, so it was not used. Your game and its odds are not affected.');
  expect(await advised(page)).toBe(-1);
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'ready');
  await expect(page.locator('#board')).toHaveAttribute('data-interactive', 'true');

  await unpatchWorker(page);
  await patchWorker(page, { forge: NO_PROOFS, planForge: { status: 'unavailable', reason: 'budget' } });
  await page.locator('#advice-retry').click();
  expect(await adviceSettled(page)).toBe('unavailable');
  await expect(page.locator('#advice-title')).toHaveText('No suggestion for this position');
  await expect(page.locator('#advice-detail')).toHaveText('The search ran out of its time or memory budget before ' +
    'it could compare cells. Retry advice, or choose a cell using its mine odds.');
  await expect(page.locator('#advice-retry')).toHaveText('Retry advice');
  expect(await advised(page)).toBe(-1);
  await page.waitForTimeout(500);
  expect((await workerStats(page)).plans, 'an unavailable answer is not retried by itself').toBe(3);
  // Retry advice plans anew (an unavailable answer is never reused), once.
  await page.locator('#advice-retry').click();
  await expect.poll(async () => (await workerStats(page)).plans).toBe(4);
  expect(await adviceSettled(page)).toBe('unavailable');
  await page.waitForTimeout(500);
  expect((await workerStats(page)).plans, 'one plan per click, no loop').toBe(4);
  expect((await workerStats(page)).solves, 'the odds were never solved again').toBe(1);
});

test('certainty the sampled odds missed: Recalculate odds solves again; nothing plays without autosolve',
  async ({ page }) => {
    const hidden = await opened(page);
    const safe = hidden[0];
    // The first solve is sampled (no proofs), a full solve then proves `safe`.
    await patchWorker(page, {
      forge: [{ status: 'approximate', default: 0.3 }, { status: 'exact', default: 0.3, values: { [safe]: 0 } }],
      planForge: { status: 'none', reason: 'certain_moves' }
    });
    await enableOdds(page);
    await expect(page.locator('#odds-badge')).toHaveText('Estimated');
    expect(await adviceSettled(page)).toBe('none');
    await expect(page.locator('#advice-title')).toHaveText('No suggestion for this position');
    await expect(page.locator('#advice-detail')).toHaveText('Some cell is safe in every layout that fits the ' +
      'clues, so no guess should be needed, but the odds shown did not prove which one in time. Recalculate the ' +
      'odds to look for it.');
    await expect(page.locator('#advice-retry')).toHaveText('Recalculate odds');
    await expect(page.locator('#odds-retry')).toBeHidden();
    await page.waitForTimeout(500);
    expect((await workerStats(page)).solves, 'nothing is recalculated by itself').toBe(1);
    await page.locator('#advice-retry').click();
    await expect(page.locator('#odds-badge')).toHaveText('Exact');
    expect((await workerStats(page)).solves, 'the cached sampled odds were solved again').toBe(2);
    await expect(cell(page, safe)).toHaveClass(/is-proven-safe/);
    await expect(advice(page)).toBeHidden(); // a proven move comes first: no advice
    await page.waitForTimeout(500);
    await expect(cell(page, safe)).toHaveClass(/is-hidden/); // Autosolve is off: nothing is played
    expect((await workerStats(page)).plans).toBe(1);
  });

test('keyboard focus and touch flags work on the suggested cell', async ({ page }) => {
  const hidden = await opened(page);
  await patchWorker(page, { forge: NO_PROOFS, planDelayMs: 600, planForge: { status: 'exact' } });
  const index = hidden[0];
  await cell(page, index).focus();
  await enableOdds(page);
  await cell(page, index).focus();
  expect(await adviceSettled(page)).toBe('exact');
  expect(await advised(page)).toBe(index);
  expect(await page.evaluate(() => Number(document.activeElement.dataset.index)), 'focus stayed put').toBe(index);
  await expect(page.locator('#inspector')).toContainText('best next move (exact)');
  await page.keyboard.press('f');
  await expect(cell(page, index)).toHaveClass(/is-flagged/);
  expect(await page.evaluate(() => Number(document.activeElement.dataset.index))).toBe(index);
  expect(await adviceSettled(page)).toBe('exact');

  // A touch long press on the marked cell removes the flag again.
  const box = await cell(page, index).boundingBox();
  const at = { clientX: box.x + box.width / 2, clientY: box.y + box.height / 2 };
  const init = Object.assign({ pointerType: 'touch', isPrimary: true, pointerId: 7, bubbles: true, button: 0 }, at);
  await cell(page, index).dispatchEvent('pointerdown', init);
  await page.waitForTimeout(650);
  await cell(page, index).dispatchEvent('pointerup', init);
  await cell(page, index).dispatchEvent('click', at);
  await expect(cell(page, index)).not.toHaveClass(/is-flagged/);
  await expect(cell(page, index)).toHaveClass(/is-hidden/);
});
