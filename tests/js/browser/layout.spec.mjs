import { test, expect } from '@playwright/test';
import {
  boardInfo, cell, enableAutosolve, expectReady, fixEntropy, learnLayout, openApp, startCustom
} from './helpers.mjs';

async function boardGeometry(page) {
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

async function revealAndMeasure(page, index) {
  const target = cell(page, index);
  // Complete navigation before measuring: only the move itself must not scroll.
  await target.scrollIntoViewIfNeeded();
  await target.focus();
  const before = await boardGeometry(page);
  await page.keyboard.press('Enter');
  return before;
}

async function expectStationaryResult(page, before, outcome) {
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', outcome);
  const result = page.locator('#result');
  await expect(result).toBeVisible();
  expect(await boardGeometry(page)).toEqual(before);
  await result.evaluate((node) => Promise.all(node.getAnimations().map((animation) => animation.finished)));
  expect(await boardGeometry(page)).toEqual(before);
  const banner = await result.boundingBox();
  expect(banner.y).toBeGreaterThanOrEqual(before.frame.y + before.frame.height);
  await expect(page.locator('#result-title')).toHaveText(
    outcome === 'won' ? 'You cleared the board!' : 'Boom! That was a mine.'
  );
}

for (const autosolve of [false, true]) {
  const mode = autosolve ? 'on' : 'off';

  test(`a win with Autosolve ${mode} keeps the board fixed`, async ({ page }) => {
    await openApp(page);
    await startCustom(page, 9, 9, 72);
    if (autosolve) await enableAutosolve(page);
    const before = await revealAndMeasure(page, 40);
    await expectStationaryResult(page, before, 'won');
    await page.locator('#result-again').click();
    await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
    await expect(page.locator('#result')).toBeHidden();
  });

  test(`a loss with Autosolve ${mode} keeps the board fixed`, async ({ page }) => {
    await fixEntropy(page, 8);
    await openApp(page);
    const mines = await learnLayout(page, 40);
    await page.reload();
    await expectReady(page);
    if (autosolve) await enableAutosolve(page);
    await cell(page, 40).click();
    if (autosolve) await expect(page.locator('#autosolve-status')).toHaveText(/^Your move:/);
    const info = await boardInfo(page);
    const mine = mines.find((index) => info[index].hidden && !info[index].flagged);
    expect(mine).toBeDefined();
    const before = await revealAndMeasure(page, mine);
    await expectStationaryResult(page, before, 'lost');
  });
}

for (const font of ['default', 'monospace']) {
  test(`wrapping autosolve feedback does not move the board (${font})`, async ({ page }) => {
    await page.setViewportSize({ width: 900, height: 800 });
    await fixEntropy(page, 8);
    await openApp(page);
    await enableAutosolve(page);
    const status = page.locator('#autosolve-status');
    await status.evaluate((node, font) => {
      if (font === 'monospace') node.style.fontFamily = font;
      // Freeze the first message's intrinsic width, independent of system fonts.
      node.style.width = 'max-content';
      node.style.width = `${Math.ceil(node.getBoundingClientRect().width)}px`;
    }, font);
    const ready = await status.boundingBox();
    const before = await revealAndMeasure(page, 40);
    await expect(status).toHaveText(/^Your move:/);
    expect((await status.boundingBox()).height).toBeGreaterThan(ready.height);
    expect(await boardGeometry(page)).toEqual(before);
  });
}

// The real move advisor after autosolve pauses (seed 8): neither the marker
// nor the advice text below the board may move the board, in any font.
for (const font of ['default', 'monospace']) {
  test(`move advice and its wrapping text do not move the board (${font})`, async ({ page }) => {
    await page.setViewportSize({ width: 900, height: 800 });
    await fixEntropy(page, 8);
    await openApp(page);
    await enableAutosolve(page);
    const box = page.locator('#advice');
    await box.evaluate((node, font) => {
      if (font === 'monospace') node.style.fontFamily = font;
    }, font);
    const before = await revealAndMeasure(page, 40);
    const cellBox = await cell(page, 40).boundingBox();
    await expect(page.locator('#autosolve-status')).toHaveText(/^Your move:/, { timeout: 20000 });
    await expect(box).toHaveAttribute('data-state', /^(exact|estimated)$/, { timeout: 20000 });
    await expect(page.locator('#board .cell.is-advised')).toHaveCount(1);
    expect(await boardGeometry(page)).toEqual(before);
    expect(await cell(page, 40).boundingBox(), 'cells keep their place and size').toEqual(cellBox);
    const marked = await page.locator('#board .cell.is-advised').boundingBox();
    expect([marked.width, marked.height], 'the marker adds no size').toEqual([cellBox.width, cellBox.height]);
    // Narrow the advice to half its rendered width (derived from this
    // browser's own text layout, no font-specific threshold): it wraps onto
    // more lines below the board, which must stay where it is.
    const [open, narrow] = await box.evaluate((node) => {
      const height = node.getBoundingClientRect().height;
      node.style.width = `${Math.ceil(node.getBoundingClientRect().width / 2)}px`;
      return [height, node.getBoundingClientRect().height];
    });
    expect(narrow).toBeGreaterThan(open);
    expect(await boardGeometry(page)).toEqual(before);
    const banner = await box.boundingBox();
    expect(banner.y).toBeGreaterThanOrEqual(before.frame.y + before.frame.height);
  });
}

test('a win preserves the position and scroll offsets of a large board', async ({ page }) => {
  await openApp(page);
  await startCustom(page, 80, 80, 6391);
  await enableAutosolve(page);
  const before = await revealAndMeasure(page, 3240);
  expect(before.boardScroll[0]).toBeGreaterThan(0);
  expect(before.boardScroll[1]).toBeGreaterThan(0);
  await expectStationaryResult(page, before, 'won');
});
