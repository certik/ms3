// Board input and rendering: mouse, keyboard, flags, chords, custom boards,
// counters, terminal states, timer, zoom and large-board scrolling. Games
// are deterministic through fixEntropy(); learnLayout() reads a layout from a
// lost game so a replay can act on known cells.
import { test, expect } from '@playwright/test';
import {
  boardInfo, cell, expectReady, fixEntropy, learnLayout, neighbors, openApp, startCustom, startPreset, track
} from './helpers.mjs';

const WIDTH = 9;

async function replay(page, first) {
  await page.reload();
  await expectReady(page);
  await cell(page, first).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'playing');
}

// A revealed number with at least one hidden safe neighbor, given the layout.
function chordTarget(info, mines) {
  for (let i = 0; i < info.length; i++) {
    if (!info[i].revealed || !/^[1-8]$/.test(info[i].text)) continue;
    const around = neighbors(i, WIDTH, WIDTH);
    const safeHidden = around.filter((j) => info[j].hidden && !mines.has(j));
    if (safeHidden.length) return { index: i, mines: around.filter((j) => mines.has(j)), safeHidden };
  }
  return null;
}

test('right-click flags and unflags; the counter may go negative', async ({ page }) => {
  await openApp(page);
  const counter = page.locator('#mines-left');
  await expect(counter).toHaveText('10');
  for (let i = 0; i < 11; i++) await cell(page, i).click({ button: 'right' });
  await expect(counter).toHaveText('-1');
  await expect(page.locator('#mines-label')).toHaveText('Too many flags');
  await expect(page.locator('#mines-counter')).toHaveClass(/is-over/);
  await cell(page, 0).click({ button: 'right' });
  await expect(counter).toHaveText('0');
  await expect(cell(page, 0)).not.toHaveClass(/is-flagged/);
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
});

test('a flagged cell is explained instead of revealed; flag mode flags on click', async ({ page }) => {
  await openApp(page);
  await cell(page, 3).click({ button: 'right' });
  await cell(page, 3).click();
  await expect(page.locator('#inspector')).toHaveText(/This cell is flagged\. Remove the flag first/);
  await expect(cell(page, 3)).toHaveClass(/is-flagged/);
  await page.locator('#mode-flag').click();
  await expect(page.locator('#mode-flag')).toHaveAttribute('aria-pressed', 'true');
  await cell(page, 5).click();
  await expect(cell(page, 5)).toHaveClass(/is-flagged/);
  await page.locator('#mode-reveal').click();
  await cell(page, 40).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
});

test('keyboard: arrows move a roving focus, F flags, Enter reveals', async ({ page }) => {
  await openApp(page);
  await cell(page, 0).focus();
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('ArrowDown');
  const focused = () => page.evaluate(() => document.activeElement.dataset.index);
  expect(await focused()).toBe('10');
  await expect(cell(page, 10)).toHaveAttribute('tabindex', '0');
  await expect(cell(page, 0)).toHaveAttribute('tabindex', '-1');
  await page.keyboard.press('f');
  await expect(cell(page, 10)).toHaveClass(/is-flagged/);
  await page.keyboard.press('F');
  await expect(cell(page, 10)).not.toHaveClass(/is-flagged/);
  await page.keyboard.press('End');
  expect(await focused()).toBe('17');
  await page.keyboard.press('Control+End');
  expect(await focused()).toBe('80');
  await page.keyboard.press('Control+Home');
  await page.keyboard.press('PageDown');
  expect(await focused()).toBe('45');
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('Enter');
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
  await expect(cell(page, 36)).toHaveClass(/is-revealed/);
  expect(await focused(), 'focus survives the re-render').toBe('36');
  await expect(page.locator('#inspector')).toContainText('Row 5, column 1');
});

test('chords open the neighbors of a satisfied number (middle click and Space)', async ({ page }) => {
  await fixEntropy(page, 5);
  await openApp(page);
  const mines = new Set(await learnLayout(page, 40));
  expect(mines.size).toBe(10);
  for (const key of ['middle', 'Space']) {
    await replay(page, 40);
    const target = chordTarget(await boardInfo(page), mines);
    expect(target, 'the opening has a number next to hidden safe cells').not.toBeNull();
    for (const mine of target.mines) await cell(page, mine).click({ button: 'right' });
    if (key === 'middle') {
      await cell(page, target.index).click({ button: 'middle' });
    } else {
      await cell(page, target.index).focus();
      await page.keyboard.press('Space');
    }
    for (const safe of target.safeHidden) await expect(cell(page, safe)).toHaveClass(/is-revealed/);
    await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
  }
});

test('a chord with the wrong flag count is explained, not played', async ({ page }) => {
  await fixEntropy(page, 5);
  await openApp(page);
  const mines = new Set(await learnLayout(page, 40));
  await replay(page, 40);
  const target = chordTarget(await boardInfo(page), mines);
  await cell(page, target.index).click();
  await expect(page.locator('#inspector')).toContainText(/has 0 flags around it/);
  for (const safe of target.safeHidden) await expect(cell(page, safe)).toHaveClass(/is-hidden/);
});

test('a loss shows the exploded mine, every mine and crossed-out wrong flags', async ({ page }) => {
  await fixEntropy(page, 5);
  await openApp(page);
  const mines = await learnLayout(page, 40);
  await replay(page, 40);
  const info = await boardInfo(page);
  const wrong = info.findIndex((c, i) => c.hidden && !mines.includes(i));
  const right = mines[1];
  await cell(page, wrong).click({ button: 'right' });
  await cell(page, right).click({ button: 'right' });
  await cell(page, mines[0]).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'lost');
  await expect(cell(page, mines[0])).toHaveClass(/is-exploded/);
  await expect(cell(page, wrong)).toHaveClass(/is-wrong-flag/);
  await expect(cell(page, right)).toHaveClass(/is-flagged/);
  await expect(cell(page, right)).not.toHaveClass(/is-wrong-flag/);
  for (const mine of mines.slice(2)) await expect(cell(page, mine)).toHaveClass(/is-mine/);
  await expect(page.locator('#result')).toBeVisible();
  await expect(page.locator('#result-title')).toHaveText('Boom! That was a mine.');
  await expect(page.locator('#result-detail')).toContainText('crossed-out flags were wrong');
  await expect(page.locator('#face-use')).toHaveAttribute('href', '#face-dead');
  const frozen = await page.locator('#timer').textContent();
  await page.waitForTimeout(1200);
  await expect(page.locator('#timer')).toHaveText(frozen);
  await cell(page, wrong).click();
  await expect(page.locator('#inspector')).toContainText('This game is over');
});

test('a win shows the result, flags every mine and stops the timer', async ({ page }) => {
  await openApp(page);
  await startCustom(page, 9, 9, 72);
  await cell(page, 40).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'won');
  await expect(page.locator('#result-title')).toHaveText('You cleared the board!');
  await expect(page.locator('#result-detail')).toContainText('Custom, 9 x 9, 72 mines, cleared in');
  await expect(page.locator('#board .cell.is-flagged')).toHaveCount(72);
  await expect(page.locator('#mines-left')).toHaveText('0');
  await expect(page.locator('#face-use')).toHaveAttribute('href', '#face-cool');
  await page.locator('#result-again').click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
  await expect(page.locator('#game-meta')).toHaveText('Custom: 9 x 9, 72 mines');
});

test('the timer runs while playing', async ({ page }) => {
  await fixEntropy(page, 5);
  await openApp(page);
  await expect(page.locator('#timer')).toHaveText('0:00');
  await cell(page, 40).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'playing');
  await expect(page.locator('#timer')).toHaveText('0:01', { timeout: 3000 });
});

test('custom boards validate 5..80 and at most area minus 9 mines', async ({ page }) => {
  await openApp(page);
  await page.locator('#custom-toggle').click();
  await expect(page.locator('#custom-form')).toBeVisible();
  await page.locator('#custom-width').fill('4');
  await page.locator('#custom-height').fill('81');
  await page.locator('#custom-mines').fill('10');
  await page.locator('#custom-start').click();
  await expect(page.locator('#custom-width-error')).toHaveText('Enter a whole number from 5 to 80.');
  await expect(page.locator('#custom-height-error')).toHaveText('Enter a whole number from 5 to 80.');
  await expect(page.locator('#custom-form-error')).toHaveText('Please fix the highlighted fields.');
  await page.locator('#custom-width').fill('9');
  await page.locator('#custom-height').fill('9');
  await expect(page.locator('#custom-mines-hint')).toHaveText('1 to 72 on 9 x 9');
  await page.locator('#custom-mines').fill('73');
  await page.locator('#custom-start').click();
  await expect(page.locator('#custom-mines-error')).toHaveText(
    'At most 72 mines fit on a 9 x 9 board, because the first reveal keeps 9 cells free.');
  await page.locator('#custom-mines').fill('72');
  await page.locator('#custom-start').click();
  await expect(page.locator('#custom-form')).toBeHidden();
  await expect(page.locator('#board .cell')).toHaveCount(81);
  await expect(page.locator('#custom-dims')).toHaveText('9x9, 72 mines');
  await expect(page.locator('.preset[data-preset="custom"]')).toHaveAttribute('aria-pressed', 'true');
});

test('zoom buttons resize cells and Fit restores automatic sizing', async ({ page }) => {
  await openApp(page);
  const size = () => page.locator('#board').evaluate((node) => parseFloat(node.style.getPropertyValue('--cell')));
  const fit = await size();
  await page.locator('#zoom-in').click();
  expect(await size()).toBeGreaterThan(fit);
  await expect(page.locator('#zoom-fit')).toHaveAttribute('aria-pressed', 'false');
  await page.locator('#zoom-out').click();
  await page.locator('#zoom-out').click();
  expect(await size()).toBeLessThan(fit);
  await page.locator('#zoom-fit').click();
  await expect(page.locator('#zoom-fit')).toHaveAttribute('aria-pressed', 'true');
  expect(await size()).toBe(fit);
});

test('an 80x80 board scrolls inside its frame and keeps the focused cell visible', async ({ page }) => {
  const { problems } = track(page);
  await openApp(page);
  await startCustom(page, 80, 80, 1000);
  await expect(page.locator('#board .cell')).toHaveCount(6400);
  const scroller = page.locator('#board-scroller');
  const dims = await scroller.evaluate((node) => ({ sw: node.scrollWidth, cw: node.clientWidth, sh: node.scrollHeight,
    ch: node.clientHeight }));
  expect(dims.sw).toBeGreaterThan(dims.cw);
  expect(dims.sh).toBeGreaterThan(dims.ch);
  const pageWidth = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
  expect(pageWidth).toBeLessThanOrEqual(0);
  await cell(page, 0).focus();
  await page.keyboard.press('End');
  await page.keyboard.press('PageDown');
  expect(await page.evaluate(() => document.activeElement.dataset.index)).toBe(String(5 * 80 + 79));
  expect(await scroller.evaluate((node) => node.scrollLeft)).toBeGreaterThan(0);
  const started = Date.now();
  await page.keyboard.press('Enter');
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
  expect(Date.now() - started).toBeLessThan(3000);
  expect(problems).toEqual([]);
});

test('presets start the documented boards', async ({ page }) => {
  await openApp(page);
  for (const [preset, cells, mines] of [['intermediate', 256, '40'], ['expert', 480, '99'], ['large', 1500, '225'],
    ['beginner', 81, '10']]) {
    await startPreset(page, preset);
    await expect(page.locator('#board .cell')).toHaveCount(cells);
    await expect(page.locator('#mines-left')).toHaveText(mines);
    await expect(page.locator(`.preset[data-preset="${preset}"]`)).toHaveAttribute('aria-pressed', 'true');
  }
});
