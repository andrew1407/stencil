// Stencil E2E — one Node/Playwright harness for every surface.
//
// Projects:
//   browser-app       Chromium against the served browser/ app, driven via window.stencil
//   extension         Chromium persistent context loading the unpacked MV3 extension/
//   fullstack         browser app + real Go server (compose) — multi-client collaboration
//   server-protocol   black-box REST/WS/TCP against the running server binary (no browser)
//
// `webServer` serves browser/ (+ e2e fixtures) on APP_URL (127.0.0.1:8188) for every project. `globalSetup`
// brings up db+redis+server via docker-compose ONLY when E2E_STACK=1; the stack-dependent
// projects self-skip otherwise (helpers/serverApi.js `stackEnabled`). Run everything with
// `E2E_STACK=1 npm test`, or just the UI projects with `npm run test:ui`.
import { defineConfig, devices } from '@playwright/test';
import { APP_URL } from './helpers/config.js';

export default defineConfig({
  testDir: './tests',
  fullyParallel: false,        // shared backing stack + shared server state
  workers: 1,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  reporter: [['list'], ['html', { open: 'never' }]],
  timeout: 60_000,
  globalSetup: './helpers/compose.js',
  globalTeardown: './helpers/compose-teardown.js',

  use: {
    baseURL: APP_URL,
    trace: 'retain-on-failure',
    serviceWorkers: 'block',   // keep browser/sw.js from caching state across tests
  },

  webServer: {
    command: 'node helpers/static-server.js',
    // A dedicated port on 127.0.0.1 (see helpers/config.js) so nothing else is ever
    // reused in its place; `url` (not `port`) pins the readiness probe to that host.
    url: APP_URL,
    reuseExistingServer: !process.env.CI,
    stdout: 'ignore',
    stderr: 'pipe',
  },

  projects: [
    {
      name: 'browser-app',
      testMatch: /tests\/browser\/.*\.spec\.js$/,
      use: { ...devices['Desktop Chrome'] },
    },
    {
      // Persistent context + unpacked extension is created inside the test (Playwright's
      // default `page` fixture can't load extensions), so no `use.channel` here.
      name: 'extension',
      testMatch: /tests\/extension\/.*\.spec\.js$/,
    },
    {
      name: 'fullstack',
      testMatch: /tests\/fullstack\/.*\.spec\.js$/,
      use: { ...devices['Desktop Chrome'] },
    },
    {
      name: 'server-protocol',
      testMatch: /tests\/server\/.*\.spec\.js$/,
    },
    {
      // Black-box the Zig CLI binary (no browser, no stack). Self-skips if the binary
      // isn't built; the URL-input case uses the shared static server on :8188.
      name: 'cli',
      testMatch: /tests\/cli\/.*\.spec\.js$/,
    },
  ],
});
