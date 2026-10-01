// Phone-sized touch device (Chromium, Pixel 7 profile): taps, long-press
// flags, and layout without page-level horizontal scrolling.
import { test, expect } from '@playwright/test';
import { boardInfo, cell, fixEntropy, openApp, startCustom, track } from './helpers.mjs';

async function longPress(page, index) {
  const target = cell(page, index);
  const box = await target.boundingBox();
  const at = { clientX: box.x + box.width / 2, clientY: box.y + box.height / 2 };
  const init = Object.assign({ pointerType: 'touch', isPrimary: true, pointerId: 7, bubbles: true, button: 0 }, at);
  await target.dispatchEvent('pointerdown', init);
  await page.waitForTimeout(650);
  await target.dispatchEvent('pointerup', init);
  // Browsers may follow a long press with a click; it must not reveal.
  await target.dispatchEvent('click', at);
}

test('a long press flags, a tap reveals, flag mode flags on tap', async ({ page }) => {
  const { problems } = track(page);
  await fixEntropy(page, 5);
  await openApp(page);
  await expect(page.locator('#inspector')).toHaveText('Click any cell to start. The first reveal is always safe.');
  await longPress(page, 0);
  await expect(cell(page, 0)).toHaveClass(/is-flagged/);
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
  await longPress(page, 0);
  await expect(cell(page, 0)).not.toHaveClass(/is-flagged/);
  await cell(page, 40).tap();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
  await page.locator('#mode-flag').tap();
  const hidden = (await boardInfo(page)).findIndex((c) => c.hidden && !c.flagged);
  await cell(page, hidden).tap();
  await expect(cell(page, hidden)).toHaveClass(/is-flagged/);
  await expect(page.locator('#inspector')).toHaveText(/Tap to reveal|Row|flag/i);
  expect(problems).toEqual([]);
});

test('the page fits the phone; large boards scroll inside their frame', async ({ page }) => {
  await openApp(page);
  const overflow = () => page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
  expect(await overflow()).toBeLessThanOrEqual(0);
  await expect(page.locator('#engine-status')).toBeVisible();
  const frame = await page.locator('#board-frame').boundingBox();
  expect(frame.x + frame.width).toBeLessThanOrEqual(page.viewportSize().width + 1);
  await startCustom(page, 80, 80, 640);
  const scroller = page.locator('#board-scroller');
  const dims = await scroller.evaluate((node) => ({ sw: node.scrollWidth, cw: node.clientWidth }));
  expect(dims.sw).toBeGreaterThan(dims.cw);
  expect(await overflow()).toBeLessThanOrEqual(0);
  await scroller.evaluate((node) => { node.scrollLeft = node.scrollWidth; });
  await cell(page, 79).tap();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
});
