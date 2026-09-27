// The extension's half of the shared LLM client. src/llm/client.js is a byte-pinned PORT of
// browser/js/llm/client.js (portParity.test.js), its §6 wire cases are the browser suite's, and the
// shared providerWire + sanitizer corpus is walked by fixtureWalkers.test.js. What remains is
// src/llm/surface.js: stencil-server token resolution, the loopback classifier the keyed wire
// judges plain http by, and the failure wording.
import { test } from 'node:test';
import assert from 'node:assert';
import { createLlmClient, LlmError } from '../../src/llm/client.js';
import {
  ASSISTANT_OFF_TEXT, serverTokenFor, turnFailureText, isUnreachableError,
} from '../../src/llm/surface.js';
import { installChromeStub } from '../helpers/chromeStub.js';
import { CONNECTIONS_KEY } from '../../src/lib/connection/connections.js';

const mockFetch = (body = {}, status = 200) => {
  const calls = [];
  return {
    calls,
    fetchImpl: async (url, init = {}) => {
      calls.push({ url, init });
      return { ok: status >= 200 && status < 300, status, json: async () => body };
    },
  };
};
const OK = { model: 'm', text: 'x', stopReason: 'end_turn' };

// ── serverTokenFor: the stored connection wins ──

test('serverTokenFor prefers the stored connection token over the explicit one', () => {
  const connections = [{ url: 'http://a:1', token: 'conn-a' }, { url: 'http://srv:8090', token: 'conn-srv' }];
  assert.strictEqual(serverTokenFor('http://srv:8090', { connections, settings: { serverToken: 'explicit' } }), 'conn-srv');
  assert.strictEqual(serverTokenFor('http://other:9', { connections, settings: { serverToken: 'explicit' } }), 'explicit');
  assert.strictEqual(serverTokenFor('http://other:9', { connections }), '');
  assert.strictEqual(serverTokenFor('http://x', {}), '');
});

test('the default token resolver reads the stored connection list', async () => {
  const chrome = installChromeStub({
    local: { [CONNECTIONS_KEY]: [{ url: 'http://srv:8090', token: 'conn-srv', name: 'srv' }] },
  });
  try {
    const { calls, fetchImpl } = mockFetch(OK);
    const client = createLlmClient({
      settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090', serverToken: 'explicit-tok' },
      fetchImpl,
    });
    await client.chat({ system: 'S', messages: [] });
    assert.strictEqual(calls[0].init.headers.Authorization, 'Bearer conn-srv');
  } finally { chrome.restore(); }
});

test('…and falls back to settings.serverToken with no chrome at all (Node)', async () => {
  delete globalThis.chrome;
  const { calls, fetchImpl } = mockFetch(OK);
  const client = createLlmClient({
    settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090', serverToken: 'explicit-tok' },
    fetchImpl,
  });
  await client.chat({ system: 'S', messages: [] });
  assert.strictEqual(calls[0].init.headers.Authorization, 'Bearer explicit-tok');
});

// ── the keyed wire rides this surface's loopback classifier ──

test('anthropic over plain http: [::1] carries the key with redirects off, a LAN host gets nothing', async () => {
  const { calls, fetchImpl } = mockFetch({ stop_reason: 'end_turn', content: [{ type: 'text', text: 'ok' }] });
  const settings = (baseUrl) => ({ provider: 'anthropic', baseUrl, apiKey: 'sk-ant-test-0123456789abcdef' });
  assert.strictEqual(await createLlmClient({ settings: settings('http://[::1]:8787'), fetchImpl }).chat({ system: 'S', messages: [] }), 'ok');
  assert.deepStrictEqual([calls[0].url, calls[0].init.redirect], ['http://[::1]:8787/v1/messages', 'error']);
  await assert.rejects(createLlmClient({ settings: settings('http://192.168.1.5'), fetchImpl }).chat({ system: 'S', messages: [] }),
    { kind: 'disabled', message: "refusing to send the API key to '192.168.1.5' over plain http — use https" });
  assert.strictEqual(calls.length, 1, 'the refused turn sent nothing');
});

// ── turnFailureText: the same voice as every other surface ──

test('turnFailureText: an endpoint that ANSWERED is quoted, and the reason said once', () => {
  const server = { provider: 'stencil-server', serverUrl: 'http://localhost:8090' };
  const answered = (msg) => Object.assign(new LlmError(msg, 'http'), { answered: true, status: 502 });
  assert.strictEqual(
    turnFailureText(server, answered('LLM request failed')),
    'Stencil server at localhost:8090: LLM request failed');
  assert.strictEqual(
    turnFailureText(server, answered('the LLM provider is out of credits or has no active billing')),
    'Stencil server at localhost:8090: the LLM provider is out of credits or has no active billing');
  // fetch failures are tagged kind 'network' by the client itself.
  assert.strictEqual(
    turnFailureText({ provider: 'ollama', baseUrl: 'http://localhost:11434' }, new LlmError('Failed to fetch', 'network')),
    "Couldn't reach Ollama at localhost:11434 (Failed to fetch)");
  // A bare TypeError is NOT read as unreachable (plan execution can throw those).
  assert.strictEqual(turnFailureText(server, new TypeError('x is not a function')), 'Failed: x is not a function');
  assert.strictEqual(turnFailureText(server, new Error('bad plan')), 'Failed: bad plan');
});

test('isUnreachableError: only the kinds a different endpoint would fix', () => {
  for (const kind of ['http', 'network', 'config']) assert.ok(isUnreachableError(new LlmError('x', kind)));
  for (const kind of ['truncated', 'refusal', 'badReply', 'disabled']) {
    assert.strictEqual(isUnreachableError(new LlmError('x', kind)), false);
  }
  assert.strictEqual(isUnreachableError(new TypeError('x')), false);
});

test('the off-switch text names where the choice lives', () => {
  assert.match(ASSISTANT_OFF_TEXT, /turned off/);
  assert.match(ASSISTANT_OFF_TEXT, /extension options/);
});
