// Playwright globalSetup/globalTeardown — bring up the backing stack (Postgres +
// Redis + the Go collaboration server) via the repo-root docker-compose.yml, but
// ONLY when E2E_STACK=1. The pure-browser and extension projects need no stack, so
// they run without Docker; fullstack/server-protocol tests self-skip when the flag
// is unset (see helpers/serverApi.js `stackEnabled`).
//
// `docker compose up -d` is idempotent, so re-runs reuse a running stack (fast local
// iteration). Teardown is opt-in via E2E_STACK_DOWN=1 so local runs keep the DB warm;
// CI discards the whole VM, so it never needs an explicit down.
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const COMPOSE_FILE = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../docker-compose.yml');
const HEALTH_URL = process.env.SERVER_URL ? `${process.env.SERVER_URL}/healthz` : 'http://localhost:8090/healthz';
const SERVICES = ['db', 'redis', 'server'];

const compose = (...args) =>
  execFileSync('docker', ['compose', '-f', COMPOSE_FILE, ...args], { stdio: 'inherit' });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

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
