// js/llm/client.js reachability probe (the panel's status dot) and the provider error bodies
// that surface as themselves, bounded and key-free. Split from llmClient.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { createLlmClient, probeProvider, providerUrl, listModels, LlmError, sanitizeProviderText } from '../../js/llm/client.js';
import { makeMockFetch, MSGS } from '../helpers/llmClientRig.js';


// ── Reachability probe (the panel's status dot / intro card; never throws) ──
test('probe ollama: GET /api/version → ok with the version', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { version: '0.5.7' } });
  const p = await probeProvider({ provider: 'ollama', baseUrl: 'http://localhost:11434', model: 'llava' }, { fetchImpl });
  assert.strictEqual(calls[0].url, 'http://localhost:11434/api/version');
  assert.deepStrictEqual(p, { provider: 'ollama', url: 'http://localhost:11434', model: 'llava', ok: true, detail: 'v0.5.7' });
});

test('probe openai-compat: GET /models with optional Bearer key → ok with the first model', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { data: [{ id: 'qwen-vl' }, { id: 'other' }] } });
  const p = await probeProvider({ provider: 'openai-compat', baseUrl: 'http://localhost:1234/v1', apiKey: 'sk-x' }, { fetchImpl });
  assert.strictEqual(calls[0].url, 'http://localhost:1234/v1/models');
  assert.deepStrictEqual(calls[0].init.headers, { Authorization: 'Bearer sk-x' });
  assert.strictEqual(p.ok, true);
  assert.strictEqual(p.detail, 'qwen-vl');
});

test('probe stencil-server: GET /llm/info with the connection token; disabled = not ok', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { enabled: true, model: 'claude-opus-5' } });
  const p = await probeProvider({ provider: 'stencil-server', serverUrl: 'http://srv:8090' }, { fetchImpl, getToken: () => 'tkn' });
  assert.strictEqual(calls[0].url, 'http://srv:8090/llm/info');
  assert.strictEqual(calls[0].init.headers.Authorization, 'Bearer tkn');
  assert.deepStrictEqual({ ok: p.ok, detail: p.detail }, { ok: true, detail: 'claude-opus-5' });

  const off = makeMockFetch({ body: { enabled: false, model: '' } });
  const pOff = await probeProvider({ provider: 'stencil-server', serverUrl: 'http://srv:8090' }, { fetchImpl: off.fetchImpl, getToken: () => 't' });
  assert.strictEqual(pOff.ok, false);
  assert.match(pOff.detail, /disabled/i);
});

test('probe failures resolve { ok: false } — HTTP errors, thrown fetches, no config', async () => {
  const http = makeMockFetch({ status: 404, body: {} });
  const p404 = await probeProvider({ provider: 'ollama', baseUrl: 'http://localhost:11434' }, { fetchImpl: http.fetchImpl });
  assert.deepStrictEqual({ ok: p404.ok, detail: p404.detail }, { ok: false, detail: 'HTTP 404' });

  const down = await probeProvider({ provider: 'ollama', baseUrl: 'http://localhost:11434' }, { fetchImpl: async () => { throw new TypeError('fetch failed'); } });
  assert.deepStrictEqual({ ok: down.ok, detail: down.detail }, { ok: false, detail: 'fetch failed' });

  const none = await probeProvider({ provider: 'ollama', baseUrl: '' }, { fetchImpl: async () => ({}) });
  assert.deepStrictEqual({ ok: none.ok, detail: none.detail }, { ok: false, detail: 'not configured' });
});


// ── Provider error bodies surface as themselves (desktop llmClient parity) ──
test('an HTTP error carries the provider\'s own message and marks the endpoint as answered', async () => {
  // ollama: {"error":"model 'x' not found"} — a missing model is not a down server.
  const ollama = makeMockFetch({ status: 404, body: { error: "model 'qwen2.5:0.5b' not found, try pulling it first" } });
  const c1 = createLlmClient({ settings: { provider: 'ollama', baseUrl: 'http://localhost:11434', model: 'qwen2.5:0.5b' }, fetchImpl: ollama.fetchImpl });
  await assert.rejects(() => c1.chat({ system: 's', messages: [{ role: 'user', text: 'hi' }] }), (err) => {
    assert.ok(err instanceof LlmError);
    assert.match(err.message, /not found, try pulling it first/);
    assert.strictEqual(err.answered, true);
    return true;
  });
  // openai-compat: {"error":{"message":"…"}}.
  const oa = makeMockFetch({ status: 400, body: { error: { message: 'invalid model' } } });
  const c2 = createLlmClient({ settings: { provider: 'openai-compat', baseUrl: 'http://localhost:1234/v1' }, fetchImpl: oa.fetchImpl });
  await assert.rejects(() => c2.chat({ system: 's', messages: [{ role: 'user', text: 'hi' }] }), /invalid model/);
});

// A provider's prose is untrusted and unbounded — the chat quotes it briefly or not at all.
test('provider error prose is bounded, control-free and never echoes a key or URL', () => {
  assert.strictEqual(sanitizeProviderText('model\nnot\tfound'), 'model not found');
  assert.strictEqual(
    sanitizeProviderText('Incorrect API key provided: sk-abcdef1234567890'),
    'Incorrect API key provided: [redacted]');
  assert.strictEqual(
    sanitizeProviderText('failed to reach http://10.0.0.5:11434/api/chat now'),
    'failed to reach [redacted] now');
  const long = sanitizeProviderText('the model is very busy right now. '.repeat(30));
  assert.ok(long.length <= 200 && long.length > 190, String(long.length));
  assert.ok(long.endsWith('…'));
});

test('an unparseable / oversized error body never reaches the chat as itself', async () => {
  const junk = makeMockFetch({ status: 500, body: { error: 'boom '.repeat(200) } });
  const c = createLlmClient({ settings: { provider: 'ollama', baseUrl: 'http://localhost:11434' }, fetchImpl: junk.fetchImpl });
  await assert.rejects(() => c.chat({ system: 's', messages: [{ role: 'user', text: 'hi' }] }), (err) => {
    assert.ok(err.message.length <= 200, err.message);
    return true;
  });
});

test('providerUrl names the endpoint the settings talk to', () => {
  const s = { baseUrl: 'http://127.0.0.1:11434', serverUrl: 'https://stencil.example' };
  assert.equal(providerUrl({ ...s, provider: 'ollama' }), 'http://127.0.0.1:11434');
  assert.equal(providerUrl({ ...s, provider: 'stencil-server' }), 'https://stencil.example');
  assert.equal(providerUrl({ ...s, provider: 'mystery' }), 'http://127.0.0.1:11434', 'an unknown provider keeps its base');
  assert.equal(providerUrl({ provider: 'stencil-server' }), '');
  assert.equal(providerUrl(null), '');
});

// A 30x must not carry a key or bearer token to a second host, on any provider's GET either.
test('the probe and the model list never follow a redirect', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { data: [{ id: 'm' }], version: '1', enabled: true } });
  await probeProvider({ provider: 'openai-compat', baseUrl: 'https://llm.example/v1', apiKey: 'sk-x' }, { fetchImpl });
  await listModels({ provider: 'ollama', baseUrl: 'http://localhost:11434' }, { fetchImpl });
  assert.deepStrictEqual(calls.map((c) => c.init.redirect), ['error', 'error']);
});
