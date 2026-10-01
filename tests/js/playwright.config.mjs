// Playwright configuration for the real-browser tests of the static site.
//
//   npx playwright test --config tests/js/playwright.config.mjs
//
// The site under test is dist/ (node scripts/build.mjs dist), or $MS_DIST,
// served read-only by scripts/serve.mjs twice: at the root and under a
// nested project path. Nothing else is served: no API, no other origin.
// Chromium runs everything (desktop, plus a touch phone profile for the
// mobile and layout specs); Firefox and WebKit run smoke and layout tests.
import { defineConfig, devices } from '@playwright/test';
import { fileURLToPath } from 'node:url';
import { NESTED_BASE, ROOT_PORT, NESTED_PORT, SITE } from './browser/site.mjs';

const ROOT = fileURLToPath(new URL('../..', import.meta.url));

export default defineConfig({
  testDir: './browser',
  testMatch: /\.spec\.mjs$/,
  globalSetup: './browser/global-setup.mjs',
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: 0,
  timeout: 60000,
  expect: { timeout: 15000 },
  outputDir: '../../build/playwright/results',
  reporter: process.env.CI ? [['list'], ['html', { open: 'never', outputFolder: '../../build/playwright/report' }]]
    : [['list']],
  use: {
    baseURL: `http://127.0.0.1:${ROOT_PORT}/`,
    trace: 'retain-on-failure'
  },
  webServer: [
    {
      command: `node scripts/serve.mjs "${SITE}" --port ${ROOT_PORT}`,
      cwd: ROOT,
      url: `http://127.0.0.1:${ROOT_PORT}/`,
      reuseExistingServer: false,
      stdout: 'ignore'
    },
    {
      command: `node scripts/serve.mjs "${SITE}" --port ${NESTED_PORT} --base ${NESTED_BASE}`,
      cwd: ROOT,
      url: `http://127.0.0.1:${NESTED_PORT}${NESTED_BASE}`,
      reuseExistingServer: false,
      stdout: 'ignore'
    }
  ],
  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
      testIgnore: /mobile\.spec\.mjs$/
    },
    {
      name: 'mobile',
      use: { ...devices['Pixel 7'] },
      testMatch: /(?:mobile|layout)\.spec\.mjs$/
    },
    {
      name: 'firefox',
      use: { ...devices['Desktop Firefox'] },
      testMatch: /(?:smoke|layout)\.spec\.mjs$/
    },
    {
      name: 'webkit',
      use: { ...devices['Desktop Safari'] },
      testMatch: /(?:smoke|layout)\.spec\.mjs$/
    }
  ]
});
