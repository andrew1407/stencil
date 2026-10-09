// The deadline every extension fetch carries (urlGuard.js withDeadline): a stalled host rejects
// at NETWORK.fetchTimeoutMs on the guarded road and the server REST client, and a caller abort wins.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { guardedFetch, FETCH_TIMEOUT_MS } from '../../../src/lib/connection/urlGuard.js';
import { checkSession } from '../../../src/lib/connection/rest.js';

const constants = JSON.parse(readFileSync(new URL('../../../../common/config/constants.json', import.meta.url), 'utf8'));

// A host that never answers: the request settles only when its signal aborts.
const stalled = (seen) => (url, init) => new Promise((_, reject) => {
  seen.push(init);
  if (!init.signal) return;
  init.signal.addEventListener('abort', () => reject(init.signal.reason), { once: true });
});

// AbortSignal.timeout(ms) answers in 20 ms, recording the ms it was asked for.
const fastDeadline = async (fn) => {
  const orig = AbortSignal.timeout;
  const asked = [];
  AbortSignal.timeout = (ms) => { asked.push(ms); return orig.call(AbortSignal, 20); };
  try { await fn(); } finally { AbortSignal.timeout = orig; }
  return asked;
};
const withFetch = async (impl, fn) => {
  const orig = globalThis.fetch;
  globalThis.fetch = impl;
  try { return await fn(); } finally { globalThis.fetch = orig; }
};

test('the deadline is the shared network constant', () => {
  assert.equal(FETCH_TIMEOUT_MS, constants.NETWORK.fetchTimeoutMs);
});

test('a stalled guarded fetch rejects at the deadline', async () => {
  const seen = [];
  const asked = await fastDeadline(() => withFetch(stalled(seen),
    () => assert.rejects(() => guardedFetch('https://cdn.example/a.png'), { name: 'TimeoutError' })));
  assert.deepEqual(asked, [FETCH_TIMEOUT_MS]);
  assert.ok(seen[0].signal, 'the request carries a signal');
});

test('a caller abort still wins over the deadline', async () => {
  const seen = [];
  const ctl = new AbortController();
  await withFetch(stalled(seen), async () => {
    const pending = guardedFetch('https://cdn.example/a.png', {}, { signal: ctl.signal });
    ctl.abort(new Error('user cancelled'));
    await assert.rejects(pending, /user cancelled/);
  });
});

test('a stalled server request rejects at the deadline', async () => {
  const seen = [];
  const asked = await fastDeadline(() =>
    assert.rejects(() => checkSession({ url: 'http://srv.example', token: 't' }, stalled(seen)), { name: 'TimeoutError' }));
  assert.deepEqual(asked, [FETCH_TIMEOUT_MS]);
  assert.equal(seen[0].redirect, 'manual');
});
