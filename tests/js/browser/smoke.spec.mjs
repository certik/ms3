// Basic module + worker gameplay smoke test, run in Chromium, Firefox and
// WebKit: the engine module loads, a first reveal works and the solver
// worker (a module worker receiving the compiled WebAssembly.Module) answers.
import { test, expect } from '@playwright/test';
import { NESTED_URL, cell, enableOdds, fixEntropy, oddsSettled, openApp, track } from './helpers.mjs';

for (const [where, url] of [['root', '/'], ['nested path', NESTED_URL]]) {
  test(`plays a first move and gets odds from the worker (${where})`, async ({ page }) => {
    const { problems } = track(page);
    await fixEntropy(page, 3);
    await openApp(page, url);
    await cell(page, 40).click();
    await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
    await expect(page.locator('#board .cell.is-revealed').first()).toBeVisible();
    await enableOdds(page);
    expect(['result', 'ended']).toContain(await oddsSettled(page));
    await expect(page.locator('#odds-badge')).not.toHaveText('Error');
    await cell(page, 0).click({ button: 'right' });
    await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'ready');
    expect(problems.filter((p) => !p.startsWith('warning:'))).toEqual([]);
  });
}
