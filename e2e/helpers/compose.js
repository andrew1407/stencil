// Playwright globalSetup/globalTeardown — brings the backing stack (Postgres + Redis + the Go
// collaboration server) up via the repo-root docker-compose.yml, ONLY when E2E_STACK=1.
// `docker compose up -d` is idempotent, so re-runs reuse a running stack; teardown is opt-in
// via E2E_STACK_DOWN=1.
import { execFileSync } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const HELPERS_DIR = path.dirname(fileURLToPath(import.meta.url));
const COMPOSE_FILE = path.resolve(HELPERS_DIR, '../../docker-compose.yml');
// The override points the server's Anthropic proxy at the stub LLM server; with
// E2E_SKIP_COMPOSE=1 it is not applied and the llm-proxy spec self-skips off /llm/info.
const LLM_OVERRIDE = path.resolve(HELPERS_DIR, 'compose.llm.yml');
const HEALTH_URL = process.env.SERVER_URL ? `${process.env.SERVER_URL}/healthz` : 'http://localhost:8090/healthz';
const SERVICES = ['db', 'redis', 'server'];

// Issuance is always admin-gated now, so hand compose a known dev admin token
// (the root compose interpolates ${ADMIN_TOKEN-}); serverApi.js sends the same one.
const compose = (...args) =>
  execFileSync('docker', ['compose', '-f', COMPOSE_FILE, '-f', LLM_OVERRIDE, ...args], {
    stdio: 'inherit',
    env: {
      ...process.env,
      ADMIN_TOKEN: process.env.ADMIN_TOKEN || 'e2e-admin',
      // Every spec issues tokens from one IP; the 10/min production default 429s
      // the suite on repeat runs. The limiter itself is covered by Go unit tests.
      AUTH_RATE_PER_MINUTE: process.env.AUTH_RATE_PER_MINUTE || '0',
    },
  });


async function waitForHealthz(timeoutMs = 120_000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const res = await fetch(HEALTH_URL);
      if (res.ok && (await res.text()).trim() === 'ok') return;
    } catch { /* not up yet */ }
    await sleep(1000);
  }
  throw new Error(`server /healthz not ready within ${timeoutMs}ms at ${HEALTH_URL}`);
}

export default async function globalSetup() {
  if (process.env.E2E_STACK !== '1') {
    console.log('[e2e stack] E2E_STACK != 1 — skipping compose; stack-dependent projects will skip.');
    return;
  }
  // Point the stack suites at a server that's ALREADY running (e.g. a local dev server,
  // or a stack started out-of-band) instead of starting compose here.
  if (process.env.E2E_SKIP_COMPOSE === '1') {
    console.log('[e2e stack] E2E_SKIP_COMPOSE=1 — using the already-running server; waiting for health…');
    await waitForHealthz();
    console.log('[e2e stack] server healthy at', HEALTH_URL);
    return;
  }
  console.log('[e2e stack] docker compose up -d db redis server …');
  compose('up', '-d', '--wait', ...SERVICES);
  await waitForHealthz();
  console.log('[e2e stack] server healthy at', HEALTH_URL);
}
