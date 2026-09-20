import { test } from 'node:test';
import assert from 'node:assert';
import { createLlmClient, LlmError } from '../js/llm/llmClient.js';

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
