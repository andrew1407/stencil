// Tests for the extension LLM client (src/llm/llmClient.js): the three §6 wire
// mappings driven with a mock fetch (mirrors browser/tests/llmClient.test.js so
// the two implementations stay behaviourally identical), plus the extension's
// stencil-server token resolution from the stored connection list.
import { test } from 'node:test';
import assert from 'node:assert';
import { createLlmClient, fetchLlmInfo, LlmError, sanitizeProviderText } from '../src/llm/llmClient.js';
import { serverTokenFor, turnFailureText } from '../src/llm/llmSurface.js';

// ── A mock fetch that records every request and replies from a queue ──
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
test('stencil-server: POST {serverUrl}/llm/chat with the resolved bearer token', async () => {
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

test('stencil-server: the default token resolver falls back to settings.serverToken (no chrome)', async () => {
  delete globalThis.chrome;   // no stored connections in Node
  const { calls, fetchImpl } = makeMockFetch({ body: { model: 'm', text: 'x', stopReason: 'end_turn' } });
  const client = createLlmClient({
    settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090', serverToken: 'explicit-tok' },
    fetchImpl,
  });
  await client.chat({ system: 'S', messages: [] });
  assert.strictEqual(calls[0].init.headers.Authorization, 'Bearer explicit-tok');
});

test('serverTokenFor prefers the stored connection token over the explicit one', () => {
  const connections = [{ url: 'http://a:1', token: 'conn-a' }, { url: 'http://srv:8090', token: 'conn-srv' }];
  assert.strictEqual(serverTokenFor('http://srv:8090', { connections, settings: { serverToken: 'explicit' } }), 'conn-srv');
  assert.strictEqual(serverTokenFor('http://other:9', { connections, settings: { serverToken: 'explicit' } }), 'explicit');
  assert.strictEqual(serverTokenFor('http://other:9', { connections }), '');
  assert.strictEqual(serverTokenFor('http://x', {}), '');
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

// Browser sanitizeProviderText parity: an untrusted body is bounded and stripped.
test('provider error prose is bounded, control-free and never echoes a key or URL', async () => {
  assert.strictEqual(sanitizeProviderText('model\nnot\tfound'), 'model not found');
  assert.strictEqual(
    sanitizeProviderText('Incorrect API key provided: sk-abcdef1234567890'),
    'Incorrect API key provided: [redacted]');
  assert.strictEqual(
    sanitizeProviderText('failed to reach http://10.0.0.5:11434/api/chat now'),
    'failed to reach [redacted] now');
  const { fetchImpl } = makeMockFetch({ status: 500, body: { error: 'boom '.repeat(200) } });
  const client = createLlmClient({ settings: { provider: 'ollama', baseUrl: 'http://localhost:11434' }, fetchImpl });
  await assert.rejects(() => client.chat({ system: 'S', messages: [] }), (err) => err.message.length <= 200);
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

test('turnFailureText: same error voice as the browser on every surface', () => {
  const server = { provider: 'stencil-server', serverUrl: 'http://localhost:8090' };
  const answered = (msg) => Object.assign(new LlmError(msg, 'http'), { answered: true, status: 502 });
  // The reason is said ONCE (contract §6.3): an endpoint that ANSWERED is quoted
  // in its own words, the host labels it, nothing restates it.
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

test('fetch failures are wrapped as typed network errors; HTTP errors carry answered+status', async () => {
  const boom = async () => { throw new TypeError('Failed to fetch'); };
  const client = createLlmClient({ settings: { provider: 'ollama', baseUrl: 'http://x:1' }, fetchImpl: boom });
  await assert.rejects(() => client.chat({ system: 'S', messages: [] }), (err) => {
    assert.ok(err instanceof LlmError);
    assert.strictEqual(err.kind, 'network');
    assert.strictEqual(err.message, 'Failed to fetch');
    return true;
  });
  const { fetchImpl } = makeMockFetch({ status: 502, body: { message: 'upstream broke' } });
  const c2 = createLlmClient({ settings: { provider: 'stencil-server', serverUrl: 'http://srv:8090' }, fetchImpl, getToken: () => 't' });
  await assert.rejects(() => c2.chat({ system: 'S', messages: [] }), (err) => {
    assert.strictEqual(err.answered, true);
    assert.strictEqual(err.status, 502);
    return true;
  });
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
});
