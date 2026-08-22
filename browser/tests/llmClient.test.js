import { test } from 'node:test';
import assert from 'node:assert';
import { createLlmClient, fetchLlmInfo, probeProvider, listModels, LlmError, sanitizeProviderText } from '../js/llm/llmClient.js';

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

// ── ollama (contract §6.1) ──
test('ollama: POST {baseUrl}/api/chat with images as bare base64', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { message: { role: 'assistant', content: 'the reply' } } });
  const client = createLlmClient({
    settings: { provider: 'ollama', baseUrl: 'http://localhost:11434', model: 'llama3.2-vision', apiKey: '', serverUrl: '' },
    fetchImpl,
  });
  const text = await client.chat({ system: 'SYS', messages: MSGS });
  assert.strictEqual(text, 'the reply');
  assert.strictEqual(calls.length, 1);
  assert.strictEqual(calls[0].url, 'http://localhost:11434/api/chat');
  assert.strictEqual(calls[0].init.method, 'POST');
  assert.deepStrictEqual(calls[0].init.headers, { 'Content-Type': 'application/json' });
  assert.deepStrictEqual(JSON.parse(calls[0].init.body), {
    model: 'llama3.2-vision',
    stream: false,
    messages: [
      { role: 'system', content: 'SYS' },
      { role: 'user', content: 'hello', images: ['AAAA', 'BBBB'] },
      { role: 'assistant', content: 'prior reply' },
      { role: 'user', content: 'again' },
    ],
  });
});

// ── openai-compat (contract §6.2) ──
test('openai-compat: POST {baseUrl}/chat/completions with image_url data URLs + Bearer key', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { choices: [{ message: { content: 'oa reply' } }] } });
  const client = createLlmClient({
    settings: { provider: 'openai-compat', baseUrl: 'http://localhost:1234/v1', model: 'local-model', apiKey: 'sk-test', serverUrl: '' },
    fetchImpl,
  });
  const text = await client.chat({ system: 'SYS', messages: MSGS });
  assert.strictEqual(text, 'oa reply');
  assert.strictEqual(calls[0].url, 'http://localhost:1234/v1/chat/completions');
  assert.deepStrictEqual(calls[0].init.headers, {
    'Content-Type': 'application/json',
    Authorization: 'Bearer sk-test',
  });
  assert.deepStrictEqual(JSON.parse(calls[0].init.body), {
    model: 'local-model',
    stream: false,
    messages: [
      { role: 'system', content: 'SYS' },
      {
        role: 'user',
        content: [
          { type: 'text', text: 'hello' },
          { type: 'image_url', image_url: { url: 'data:image/png;base64,AAAA' } },
          { type: 'image_url', image_url: { url: 'data:image/jpeg;base64,BBBB' } },
        ],
      },
      { role: 'assistant', content: 'prior reply' },
      { role: 'user', content: 'again' },
    ],
  });
});

test('openai-compat: apiKey optional — no Authorization header without one (LM Studio)', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { choices: [{ message: { content: 'x' } }] } });
  const client = createLlmClient({
    settings: { provider: 'openai-compat', baseUrl: 'http://localhost:1234/v1', model: '', apiKey: '', serverUrl: '' },
    fetchImpl,
  });
  await client.chat({ system: 'S', messages: [] });
  assert.deepStrictEqual(calls[0].init.headers, { 'Content-Type': 'application/json' });
});

// ── stencil-server (contract §6.3) ──
test('stencil-server: POST {serverUrl}/llm/chat with the existing bearer token', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { model: 'claude-opus-5', text: 'srv reply', stopReason: 'end_turn' } });
  const tokens = [];
  const client = createLlmClient({
    settings: { provider: 'stencil-server', baseUrl: '', model: '', apiKey: '', serverUrl: 'http://srv:8090' },
    fetchImpl,
    getToken: (url) => { tokens.push(url); return 'tkn-123'; },
  });
  const text = await client.chat({ system: 'SYS', messages: MSGS });
  assert.strictEqual(text, 'srv reply');
  assert.deepStrictEqual(tokens, ['http://srv:8090']);
  assert.strictEqual(calls[0].url, 'http://srv:8090/llm/chat');
  assert.deepStrictEqual(calls[0].init.headers, {
    'Content-Type': 'application/json',
    Authorization: 'Bearer tkn-123',
  });
  // Exact protocol.LlmChatRequest shape: images ride as {mediaType, data}; empty
  // model omitted; text-only messages carry no images key.
  assert.deepStrictEqual(JSON.parse(calls[0].init.body), {
    system: 'SYS',
    messages: [
      { role: 'user', text: 'hello', images: [{ mediaType: 'image/png', data: 'AAAA' }, { mediaType: 'image/jpeg', data: 'BBBB' }] },
      { role: 'assistant', text: 'prior reply' },
      { role: 'user', text: 'again' },
    ],
  });
});

test('stencil-server: a configured model is passed through', async () => {
  const { calls, fetchImpl } = makeMockFetch({ body: { model: 'm', text: 'x', stopReason: 'end_turn' } });
  const client = createLlmClient({
    settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090', model: 'claude-opus-5' },
    fetchImpl,
    getToken: () => 't',
  });
  await client.chat({ system: 'S', messages: [] });
  assert.strictEqual(JSON.parse(calls[0].init.body).model, 'claude-opus-5');
});

test('stencil-server: stopReason max_tokens → typed "truncated" error, never a plan', async () => {
  const { fetchImpl } = makeMockFetch({ body: { model: 'm', text: '{"version":1,"reply":"partial…', stopReason: 'max_tokens' } });
  const client = createLlmClient({
    settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090' },
    fetchImpl, getToken: () => 't',
  });
  await assert.rejects(() => client.chat({ system: 'S', messages: [] }), (err) => {
    assert.ok(err instanceof LlmError);
    assert.strictEqual(err.kind, 'truncated');
    assert.match(err.message, /truncated/i);
    return true;
  });
});

test('stencil-server: stopReason refusal → typed "refusal" error carrying the text', async () => {
  const { fetchImpl } = makeMockFetch({ body: { model: 'm', text: 'I cannot help with that.', stopReason: 'refusal' } });
  const client = createLlmClient({
    settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090' },
    fetchImpl, getToken: () => 't',
  });
  await assert.rejects(() => client.chat({ system: 'S', messages: [] }), (err) => {
    assert.strictEqual(err.kind, 'refusal');
    assert.strictEqual(err.message, 'I cannot help with that.');
    return true;
  });
});

test('stencil-server: 503 llmDisabled → typed "disabled" error', async () => {
  const { fetchImpl } = makeMockFetch({ status: 503, body: { code: 'llmDisabled', message: 'LLM support is not enabled on this server' } });
  const client = createLlmClient({
    settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090' },
    fetchImpl, getToken: () => 't',
  });
  await assert.rejects(() => client.chat({ system: 'S', messages: [] }), (err) => {
    assert.strictEqual(err.kind, 'disabled');
    assert.match(err.message, /not enabled/);
    return true;
  });
});

test('HTTP errors surface the server message with kind "http"', async () => {
  const { fetchImpl } = makeMockFetch({ status: 401, body: { code: 'unauthorized', message: 'bad token' } });
  const client = createLlmClient({
    settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090' },
    fetchImpl, getToken: () => 't',
  });
  await assert.rejects(() => client.chat({ system: 'S', messages: [] }), (err) => err.kind === 'http' && err.message === 'bad token');
});

test('a malformed 200 body is a typed badReply error, never an empty reply', async () => {
  const cases = [
    [{ provider: 'ollama', baseUrl: 'http://x:1' }, { error: 'model failed to load' }, /message\.content/],
    [{ provider: 'openai-compat', baseUrl: 'http://x:1' }, { choices: [] }, /choices\[0\]/],
    [{ provider: 'stencil-server', serverUrl: 'http://srv:8090' }, { model: 'm', stopReason: 'end_turn' }, /no text/],
  ];
  for (const [settings, body, re] of cases) {
    const { fetchImpl } = makeMockFetch({ body });
    const client = createLlmClient({ settings, fetchImpl, getToken: () => 't' });
    await assert.rejects(() => client.chat({ system: 'S', messages: [] }), (err) => {
      assert.strictEqual(err.kind, 'badReply');
      assert.match(err.message, re);
      return true;
    });
  }
  // An EMPTY string reply is still a reply (a blank completion), not a badReply.
  const { fetchImpl } = makeMockFetch({ body: { message: { role: 'assistant', content: '' } } });
  const client = createLlmClient({ settings: { provider: 'ollama', baseUrl: 'http://x:1' }, fetchImpl });
  assert.strictEqual(await client.chat({ system: 'S', messages: [] }), '');
});

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
