// Hosting, network, lifecycle and preference behavior of the static site.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test, expect } from '@playwright/test';
import {
  NESTED_BASE, NESTED_URL, REACTOR_FILE, ROOT_URL, VENDOR_PATHS, boardInfo, cell, enableOdds, expectReady,
  fixEntropy, oddsSettled, openApp, startPreset, track
} from './helpers.mjs';
import { SITE } from './site.mjs';
import { wrongAbiModule } from '../support/forge.mjs';

const REACTOR_ROUTE = '**/' + REACTOR_FILE;

for (const [where, url] of [['the root', ROOT_URL], ['a nested project path', NESTED_URL]]) {
  test(`runs from ${where} with only same-site, relative requests and no API`, async ({ page }) => {
    const { problems, requests } = track(page);
    await fixEntropy(page, 1);
    await openApp(page, url);
    await expect(page.locator('#engine-status-text')).toHaveText('Runs in your browser');
    await cell(page, 40).click();
    await expect(page.locator('#board .cell.is-revealed').first()).toBeVisible();
    await enableOdds(page);
    expect(['result', 'ended']).toContain(await oddsSettled(page));
    const base = new URL(url);
    for (const request of requests) {
      const target = new URL(request);
      expect(target.origin, request).toBe(base.origin);
      expect(target.pathname.startsWith(base.pathname), request).toBe(true);
      expect(target.pathname, request).not.toMatch(/\/api(\/|$)/);
    }
    const paths = requests.map((request) => new URL(request).pathname.slice(base.pathname.length));
    for (const file of ['', 'app.js', 'styles.css', 'engine-client.js', 'wasm-host.js', VENDOR_PATHS[0], REACTOR_FILE,
      'probability-worker.js']) {
      expect(paths, file).toContain(file);
    }
    expect(problems).toEqual([]);
  });
}

test('the nested site redirects its bare path and keeps working', async ({ page }) => {
  const bare = NESTED_URL.slice(0, -1);
  await page.goto(bare);
  expect(page.url()).toBe(NESTED_URL);
  await expectReady(page);
  expect(NESTED_BASE.endsWith('/')).toBe(true);
});

test('reload always starts a new game while preferences persist', async ({ page }) => {
  await openApp(page);
  await page.evaluate(() => localStorage.setItem('minesweeper.gameId', 'left-over-server-id'));
  await startPreset(page, 'intermediate');
  await page.locator('#zoom-in').click();
  await enableOdds(page);
  await cell(page, 100).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won|lost/);
  const zoomed = await page.locator('#board').evaluate((node) => node.style.getPropertyValue('--cell'));

  await page.reload();
  await expectReady(page);
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
  await expect(page.locator('#board .cell.is-revealed')).toHaveCount(0);
  await expect(page.locator('#board .cell')).toHaveCount(256);
  await expect(page.locator('.preset[data-preset="intermediate"]')).toHaveAttribute('aria-pressed', 'true');
  await expect(page.locator('#odds-toggle')).toHaveAttribute('aria-checked', 'true');
  await expect(page.locator('#zoom-fit')).toHaveAttribute('aria-pressed', 'false');
  expect(await page.locator('#board').evaluate((node) => node.style.getPropertyValue('--cell'))).toBe(zoomed);
  const stored = await page.evaluate(() => Object.keys(localStorage).sort());
  expect(stored).toEqual(['minesweeper.prefs', 'minesweeper.settings']);
  await expect(page.locator('#odds-panel')).toHaveAttribute('data-phase', 'ready');
  await expect(page.locator('.app-footer')).toContainText('Reloading the page starts a new game');
});

test('each tab plays its own game', async ({ context }) => {
  const first = await context.newPage();
  const second = await context.newPage();
  await fixEntropy(first, 10);
  await fixEntropy(second, 10);
  await openApp(first);
  await openApp(second);
  await cell(first, 40).click();
  await cell(first, 0).click({ button: 'right' });
  await expect(first.locator('#board-frame')).not.toHaveAttribute('data-status', 'ready');
  await second.waitForTimeout(300);
  await expect(second.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
  await expect(second.locator('#board .cell.is-revealed')).toHaveCount(0);
  await expect(second.locator('#board .cell.is-flagged')).toHaveCount(0);
  // The second tab's moves do not reach the first one either.
  const revealedInFirst = await first.locator('#board .cell.is-revealed').count();
  await cell(second, 80).click();
  await expect(second.locator('#board .cell.is-revealed').first()).toBeVisible();
  await first.waitForTimeout(300);
  await expect(first.locator('#board .cell.is-revealed')).toHaveCount(revealedInFirst);
  await expect(cell(first, 0)).toHaveClass(/is-flagged/);
});

test('an engine that cannot load is reported and can be retried', async ({ page }) => {
  const { problems } = track(page);
  await page.route(REACTOR_ROUTE, (route) => route.fulfill({ status: 404, body: 'gone' }));
  await page.goto('/');
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'error');
  await expect(page.locator('#engine-status-text')).toHaveText('Engine error');
  await expect(page.locator('#board-empty-title')).toHaveText('Could not load the game engine');
  await expect(page.locator('#board-empty-text')).toContainText('HTTP 404');
  await expect(page.locator('#board-empty-retry')).toBeVisible();
  await expect(page.locator('#board')).toBeHidden();
  await page.unroute(REACTOR_ROUTE);
  await page.locator('#board-empty-retry').click();
  await expectReady(page);
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
  expect(problems.filter((p) => !/404|Failed to load resource/.test(p))).toEqual([]);
});

test('a module that is not the engine is refused before it runs', async ({ page }) => {
  await page.route(REACTOR_ROUTE, (route) => route.fulfill({
    status: 200,
    contentType: 'application/wasm',
    body: Buffer.from([0, 97, 115, 109, 1, 0, 0, 0])
  }));
  await page.goto('/');
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'error');
  await expect(page.locator('#board-empty-text')).toContainText('does not match this page');
});

test('an engine build that cannot start is fetched again by Try again', async ({ page }) => {
  const { problems } = track(page);
  const exported = WebAssembly.Module.exports(new WebAssembly.Module(readFileSync(join(SITE, REACTOR_FILE))));
  const stale = wrongAbiModule(exported.filter((entry) => entry.kind === 'function').map((entry) => entry.name));
  let fetches = 0;
  await page.route(REACTOR_ROUTE, (route) => {
    // The first request gets a build from another release, later ones the
    // fixed file.
    if (++fetches > 1) return route.fallback();
    return route.fulfill({ status: 200, contentType: 'application/wasm', body: Buffer.from(stale) });
  });
  await page.goto('/');
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'error');
  await expect(page.locator('#board-empty-text')).toContainText('speaks ABI version 2');
  await page.locator('#board-empty-retry').click();
  await expectReady(page);
  expect(fetches).toBe(2);
  await cell(page, 40).click();
  await expect(page.locator('#board .cell.is-revealed').first()).toBeVisible();
  expect(problems).toEqual([]);
});

test('a trap in the game engine stops the game visibly; a new game restarts it', async ({ page }) => {
  // Only the main-thread instance's ms_host.now_ms import is made to throw.
  await page.addInitScript(() => {
    const instantiate = WebAssembly.instantiate;
    WebAssembly.instantiate = function (module, imports) {
      if (imports && imports.ms_host) {
        const now = imports.ms_host.now_ms;
        imports = Object.assign({}, imports, { ms_host: { now_ms: () => {
          if (window.__msBreakClock) throw new Error('injected clock fault');
          return now();
        } } });
      }
      return instantiate.call(this, module, imports);
    };
  });
  const { requests } = track(page);
  await fixEntropy(page, 5);
  await openApp(page);
  await cell(page, 40).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'playing');
  const hidden = (await boardInfo(page)).flatMap((c, i) => (c.hidden ? [i] : []));
  await page.evaluate(() => { window.__msBreakClock = true; });
  await cell(page, hidden[0]).click({ button: 'right' });
  await expect(page.locator('.notice-title')).toHaveText('The game engine stopped');
  await expect(page.locator('.notice-message')).toContainText('injected clock fault');
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'error');
  await expect(page.locator('#board')).toHaveAttribute('data-interactive', 'false');
  const frozen = await page.locator('#timer').textContent();
  await cell(page, hidden[1]).click();
  await expect(page.locator('#inspector')).toContainText('The game engine stopped');
  await page.waitForTimeout(1100);
  await expect(page.locator('#timer')).toHaveText(frozen);
  await page.evaluate(() => { window.__msBreakClock = false; });
  await page.locator('.notice .btn', { hasText: 'Start new game' }).click();
  await expect(page.locator('#engine-status')).toHaveAttribute('data-state', 'ready');
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', 'ready');
  await expect(page.locator('.notice')).toHaveCount(0);
  await cell(page, 40).click();
  await expect(page.locator('#board-frame')).toHaveAttribute('data-status', /playing|won/);
  // The new instance started from the module compiled at load.
  expect(requests.filter((url) => new URL(url).pathname.endsWith('/' + REACTOR_FILE))).toHaveLength(1);
});
