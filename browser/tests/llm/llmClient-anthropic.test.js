// The anthropic wire beyond its fixture corpus (llm-providers.md §5, §6.4, §6.5): the session key
// reaches a request only through withSessionKey and never localStorage, the models list and probe
// carry the same headers, plain http off loopback sends nothing, the classifier's status-only rows,
// and the card a missing key raises.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createMemoryStorage, installMemoryStorage } from '../helpers/memoryStorage.js';
import { makeMockFetch as recorder } from '../helpers/llmClientRig.js';

const local = installMemoryStorage();
const tab = createMemoryStorage();
Object.defineProperty(globalThis, 'sessionStorage', { value: tab, configurable: true, writable: true });

const { createLlmClient, listModels, probeProvider, LlmError } = await import('../../js/llm/client.js');
const { classifyUpstream, upstreamErrorText } = await import('../../js/llm/http.js');
const { loadLlmSettings, saveLlmSettings, withSessionKey } = await import('../../js/llm/settings.js');
const { setSessionKey, forgetSessionKey } = await import('../../js/llm/sessionKey.js');
const { describeChatError } = await import('../../js/llm/chat/reply.js');

const KEY = 'sk-ant-test-0123456789abcdef';
const BASE = 'https://api.anthropic.test';
const anthropic = (over = {}) => ({ provider: 'anthropic', baseUrl: BASE, model: '', apiKey: '', serverUrl: '', saveChats: false, ...over });
const everyStoredValue = () => [...local._map.values()].join('\n');

test('withSessionKey: the held key rides anthropic settings only, as a copy', () => {
  forgetSessionKey();
  const s = anthropic();
  assert.equal(withSessionKey(s).apiKey, '', 'no key held — the client will send nothing');
  setSessionKey(KEY);
  const req = withSessionKey(s);
  assert.equal(req.apiKey, KEY);
  assert.equal(s.apiKey, '', 'the settings object itself never holds it');
  const openai = { provider: 'openai-compat', apiKey: 'sk-openai' };
  assert.equal(withSessionKey(openai), openai);
  forgetSessionKey();
});

test('saveLlmSettings cannot write the anthropic key, nor anything equal to the session key', () => {
  local.clear();
  setSessionKey(KEY);
  saveLlmSettings(anthropic({ apiKey: KEY, model: 'claude-haiku-4-5' }));
  const saved = JSON.parse(local.getItem('drawingApp_llmSettings'));
  assert.deepEqual(saved, { provider: 'anthropic', baseUrl: BASE, model: 'claude-haiku-4-5', apiKey: '', serverUrl: '', saveChats: false });
  saveLlmSettings({ provider: 'openai-compat', baseUrl: 'http://box/v1', apiKey: KEY });
  assert.equal(JSON.parse(local.getItem('drawingApp_llmSettings')).apiKey, '', 'switching providers cannot carry it over');
  local.setItem('drawingApp_llmSettings', JSON.stringify(anthropic({ apiKey: 'left-over' })));
  assert.equal(loadLlmSettings().apiKey, '', 'a stored anthropic apiKey is never loaded');
  forgetSessionKey();
});

test('a chat turn sends the session key as x-api-key and leaves no trace of it in localStorage', async () => {
  local.clear();
  saveLlmSettings(anthropic());
  setSessionKey(KEY);
  const { calls, fetchImpl } = recorder({ body: { stop_reason: 'end_turn', content: [{ type: 'text', text: 'hi' }] } });
  const reply = await createLlmClient({ settings: withSessionKey(loadLlmSettings()), fetchImpl }).chat({ system: 's', messages: [{ role: 'user', text: 'x' }] });
  assert.equal(reply, 'hi');
  assert.equal(calls[0].url, `${BASE}/v1/messages`);
  assert.equal(calls[0].init.headers['x-api-key'], KEY);
  assert.equal(calls[0].init.headers['anthropic-dangerous-direct-browser-access'], 'true');
  assert.equal(calls[0].init.headers.Authorization, undefined);
  assert.ok(!calls[0].init.body.includes(KEY), 'the key is a header, never the body');
  assert.ok(!everyStoredValue().includes(KEY), 'localStorage never saw the key');
  assert.equal(JSON.parse(tab.getItem('stencil_llm_session_key')).key, KEY);
  forgetSessionKey();
});

test('models and probe: no key sends nothing; with one they GET /v1/models with the §6.5 headers', async () => {
  const { calls, fetchImpl } = recorder({ body: { data: [{ id: 'claude-opus-5' }, { id: 'claude-haiku-4-5' }] } });
  assert.deepEqual(await listModels(anthropic(), { fetchImpl }), []);
  const none = await probeProvider(anthropic(), { fetchImpl });
  assert.deepEqual({ ok: none.ok, detail: none.detail }, { ok: false, detail: 'no API key for this session' });
  assert.equal(calls.length, 0);

  assert.deepEqual(await listModels(anthropic({ apiKey: KEY }), { fetchImpl }), ['claude-opus-5', 'claude-haiku-4-5']);
  const ok = await probeProvider(anthropic({ apiKey: KEY }), { fetchImpl });
  assert.deepEqual({ ok: ok.ok, detail: ok.detail, url: ok.url }, { ok: true, detail: 'claude-opus-5', url: BASE });
  for (const c of calls) {
    assert.equal(c.url, `${BASE}/v1/models`);
    assert.deepEqual(c.init.headers, { 'x-api-key': KEY, 'anthropic-version': '2023-06-01', 'anthropic-dangerous-direct-browser-access': 'true' });
  }
  const refused = recorder({ status: 401, body: {} });
  const bad = await probeProvider(anthropic({ apiKey: KEY }), { fetchImpl: refused.fetchImpl });
  assert.deepEqual({ ok: bad.ok, detail: bad.detail }, { ok: false, detail: 'HTTP 401' });
  assert.ok(!JSON.stringify(bad).includes(KEY), 'a probe result never carries the key');
});

test('plain http: an IPv6 loopback carries the key without following redirects; a LAN host gets nothing', async () => {
  const { calls, fetchImpl } = recorder({ body: { stop_reason: 'end_turn', content: [{ type: 'text', text: 'ok' }], data: [{ id: 'm' }] } });
  const local6 = anthropic({ baseUrl: 'http://[::1]:8787', apiKey: KEY });
  assert.equal(await createLlmClient({ settings: local6, fetchImpl }).chat({ system: 's', messages: [] }), 'ok');
  assert.deepEqual(await listModels(local6, { fetchImpl }), ['m']);
  assert.deepEqual(calls.map((c) => [c.url, c.init.redirect, c.init.headers['x-api-key']]), [
    ['http://[::1]:8787/v1/messages', 'error', KEY], ['http://[::1]:8787/v1/models', 'error', KEY]]);

  calls.length = 0;
  const lan = anthropic({ baseUrl: 'http://192.168.1.5', apiKey: KEY });
  const refusal = "refusing to send the API key to '192.168.1.5' over plain http — use https";
  await assert.rejects(createLlmClient({ settings: lan, fetchImpl }).chat({ system: 's', messages: [] }),
    (e) => e instanceof LlmError && e.kind === 'disabled' && e.message === refusal);
  assert.deepEqual(await listModels(lan, { fetchImpl }), []);
  assert.deepEqual((await probeProvider(lan, { fetchImpl })).detail, refusal);
  assert.equal(calls.length, 0, 'nothing reached the network');
});

test('the classifier: status alone decides when the envelope is missing', async () => {
  assert.equal(classifyUpstream(408), 'timeout');
  assert.equal(classifyUpstream(504), 'timeout');
  assert.equal(classifyUpstream(402), 'credits');
  assert.equal(classifyUpstream(500), 'overloaded');
  assert.equal(classifyUpstream(429, 'invalid_request_error', 'Your credit balance is too low'), 'credits', 'billing wins over the status');
  assert.equal(classifyUpstream(400, 'api_error'), 'overloaded');
  assert.equal(upstreamErrorText(400, '', '', KEY), 'the LLM provider returned an error (HTTP 400)');
  const fetchImpl = async () => ({ ok: false, status: 400, json: async () => { throw new SyntaxError('html'); } });
  await assert.rejects(
    createLlmClient({ settings: anthropic({ apiKey: KEY }), fetchImpl }).chat({ system: 's', messages: [] }),
    (e) => e instanceof LlmError && e.kind === 'http' && e.status === 400 && e.answered && e.message === 'the LLM provider returned an error (HTTP 400)',
  );
});

test('a turn without a session key is a card that asks for it, not a notice', () => {
  const err = LlmError.disabled('no API key for this session');
  const out = describeChatError(err, anthropic());
  assert.equal(out.kind, 'unreachable');
  assert.equal(out.text, 'Anthropic API (Claude): no API key for this session — enter your key in the assistant settings.');
  assert.equal(describeChatError(LlmError.disabled('LLM disabled'), { provider: 'stencil-server' }).kind, 'notice');
});

test('a plain-http refusal names the fix itself, without asking for the key', () => {
  const why = "refusing to send the API key to 'lan.test' over plain http — use https";
  const out = describeChatError(LlmError.disabled(why), anthropic());
  assert.equal(out.kind, 'unreachable');
  assert.equal(out.text, `Anthropic API (Claude): ${why}.`);
});
