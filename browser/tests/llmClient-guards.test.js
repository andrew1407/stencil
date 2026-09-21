// js/llm/client.js guards: the off switch, the AbortSignal, missing configuration and
// fetchLlmInfo's bearer token. Split from llmClient.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { createLlmClient, fetchLlmInfo, probeProvider, listModels, LlmError } from '../js/llm/client.js';

// ── A mock fetch that records every request and replies from a queue ──
// (the mock-fetch idiom from connections.test.js).
const makeMockFetch = (responses) => {
  const calls = [];
  const queue = Array.isArray(responses) ? responses.slice() : [responses];
  const fetchImpl = async (url, init = {}) => {
    calls.push({ url, init });
    const next = queue.length > 1 ? queue.shift() : queue[0];
    const { status = 200, body = {} } = next || {};
    return { ok: status >= 200 && status < 300, status, json: async () => body };
  };
  return { calls, fetchImpl };
};

const MSGS = [
  { role: 'user', text: 'hello', images: [{ mediaType: 'image/png', data: 'AAAA' }, { mediaType: 'image/jpeg', data: 'BBBB' }] },
  { role: 'assistant', text: 'prior reply' },
  { role: 'user', text: 'again' },
];

test('provider "none": chat throws a typed config error, probe fails offline, no fetch ever fires', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: {} });
  const client = createLlmClient({ settings: { provider: 'none' }, fetchImpl });
  await assert.rejects(client.chat({ system: 's', messages: MSGS }), (e) => e instanceof LlmError && e.kind === 'config' && /turned off/.test(e.message));
  const probe = await probeProvider({ provider: 'none' }, { fetchImpl });
  assert.deepStrictEqual({ ok: probe.ok, detail: probe.detail }, { ok: false, detail: 'assistant turned off' });
  assert.deepStrictEqual(await listModels({ provider: 'none' }, { fetchImpl }), []);
  assert.strictEqual(calls.length, 0, 'none must never touch the network');
});

test('chat threads an AbortSignal into the fetch; aborting rejects with AbortError', async () => {
  const seen = [];
  const fetchImpl = (url, init) => new Promise((res, reject) => {
    seen.push(init.signal);
    init.signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')));
  });
  const ac = new AbortController();
  const client = createLlmClient({ settings: { provider: 'ollama', baseUrl: 'http://x' }, fetchImpl });
  const p = client.chat({ system: 's', messages: [], signal: ac.signal });
  ac.abort();
  await assert.rejects(p, (e) => e.name === 'AbortError');
  assert.ok(seen[0] instanceof AbortSignal, 'signal reached the fetch init');
});

test('missing configuration and unknown providers throw clearly', async () => {
  const { fetchImpl } = makeMockFetch({});
  await assert.rejects(() => createLlmClient({ settings: { provider: 'ollama', baseUrl: '' }, fetchImpl }).chat({ system: 'S', messages: [] }), /base URL/i);
  await assert.rejects(() => createLlmClient({ settings: { provider: 'stencil-server', serverUrl: '' }, fetchImpl }).chat({ system: 'S', messages: [] }), /server/i);
  await assert.rejects(() => createLlmClient({ settings: { provider: 'weird' }, fetchImpl }).chat({ system: 'S', messages: [] }), /Unknown LLM provider/);
});

test('fetchLlmInfo GETs /llm/info with the bearer token', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { enabled: true, model: 'claude-opus-5' } });
  const info = await fetchLlmInfo('http://srv:8090', { token: 'tkn', fetchImpl });
  assert.deepStrictEqual(info, { enabled: true, model: 'claude-opus-5' });
  assert.strictEqual(calls[0].url, 'http://srv:8090/llm/info');
  assert.deepStrictEqual(calls[0].init.headers, { Authorization: 'Bearer tkn' });
});
