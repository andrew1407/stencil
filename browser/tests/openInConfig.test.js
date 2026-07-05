// Tests for the "Open in…" operator-config loader (js/config/openInConfig.js).
// loadOpenInConfig() fetches a LOCAL, gitignored openInConfig.json at runtime (the static-site
// equivalent of a .env) and MUST degrade to OPEN_IN_DEFAULTS on every failure mode so a fresh
// clone with no local file still boots: a missing file / network error, a non-ok response, bad
// JSON, and per-field type coercion (a non-string field falls back to its default). The result
// is process-cached (fetched at most once), so each scenario re-imports the module with a unique
// ?case= query to get a fresh cache — the same ESM-cache-busting pattern the other suites use.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// Import OPEN_IN_DEFAULTS once (a stable constant) for comparison.
const { OPEN_IN_DEFAULTS } = await import('../js/config/openInConfig.js');

let caseId = 0;
// Install a fake global fetch, then load a fresh copy of the module (its `cached` promise resets).
const loadWith = async (fetchImpl) => {
  globalThis.fetch = fetchImpl;
  const mod = await import(`../js/config/openInConfig.js?case=${++caseId}`);
  return mod.loadOpenInConfig();
};

test('OPEN_IN_DEFAULTS is desktop-enabled, Telegram-hidden', () => {
  assert.deepEqual(OPEN_IN_DEFAULTS, { desktopScheme: 'stencil', telegramBotUsername: '' });
});

test('a valid config is loaded and both fields are honoured', async () => {
  const cfg = await loadWith(async () => ({ ok: true, json: async () => ({ desktopScheme: 'myscheme', telegramBotUsername: 'stencilbot' }) }));
  assert.deepEqual(cfg, { desktopScheme: 'myscheme', telegramBotUsername: 'stencilbot' });
});

test('a missing file / network error → defaults (fetch rejects)', async () => {
  const cfg = await loadWith(async () => { throw new Error('Failed to fetch'); });
  assert.deepEqual(cfg, OPEN_IN_DEFAULTS);
  assert.notEqual(cfg, OPEN_IN_DEFAULTS, 'returns a fresh copy, not the shared constant');
});

test('a non-ok response (404) → defaults', async () => {
  const cfg = await loadWith(async () => ({ ok: false, status: 404, json: async () => ({ desktopScheme: 'nope' }) }));
  assert.deepEqual(cfg, OPEN_IN_DEFAULTS);
});

test('malformed JSON (json() throws) → defaults', async () => {
  const cfg = await loadWith(async () => ({ ok: true, json: async () => { throw new SyntaxError('Unexpected token'); } }));
  assert.deepEqual(cfg, OPEN_IN_DEFAULTS);
});

test('non-string fields are ignored and fall back to their defaults', async () => {
  const cfg = await loadWith(async () => ({ ok: true, json: async () => ({ desktopScheme: 42, telegramBotUsername: { evil: true } }) }));
  assert.deepEqual(cfg, OPEN_IN_DEFAULTS);
});

test('a valid desktopScheme with a non-string telegramBotUsername keeps the scheme, drops the username', async () => {
  const cfg = await loadWith(async () => ({ ok: true, json: async () => ({ desktopScheme: 'custom', telegramBotUsername: 123 }) }));
  assert.deepEqual(cfg, { desktopScheme: 'custom', telegramBotUsername: '' });
});

test('the result is cached — fetch runs at most once across callers', async () => {
  let calls = 0;
  globalThis.fetch = async () => { calls++; return { ok: true, json: async () => ({ desktopScheme: 'once', telegramBotUsername: '' }) }; };
  const mod = await import(`../js/config/openInConfig.js?case=${++caseId}`);
  const [a, b] = await Promise.all([mod.loadOpenInConfig(), mod.loadOpenInConfig()]);
  await mod.loadOpenInConfig();
  assert.equal(calls, 1, 'fetch invoked exactly once');
  assert.equal(a, b, 'same cached promise result shared by callers');
});
