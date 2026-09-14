// The extension's half of the shared LLM client. src/llm/llmClient.js is a byte-pinned
// PORT of browser/js/llm/llmClient.js (portParity.test.js), the §6 wire cases are the
// browser suite's (browser/tests/llmClient.test.js), and the shared providerWire +
// sanitizer fixture corpus is walked against THIS client by fixtureWalkers.test.js — so
// none of that is duplicated here.
//
// What remains is the one module the client is allowed to differ through:
// src/llm/llmSurface.js — the extension's stencil-server token resolution off its stored
// connection list, and the per-surface failure wording.
import { test } from 'node:test';
import assert from 'node:assert';
import { createLlmClient, LlmError } from '../src/llm/llmClient.js';
import {
  ASSISTANT_OFF_TEXT, serverTokenFor, turnFailureText, isUnreachableError,
} from '../src/llm/llmSurface.js';
import { installChromeStub } from './helpers/chromeStub.js';
import { CONNECTIONS_KEY } from '../src/lib/connections.js';

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
