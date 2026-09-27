// stencil.llm and the anthropic session key (llm-providers.md §5): write-only on the facade — set
// for this tab's session through apiKey, read back redacted, its expiry and forget exposed — and
// never saved to localStorage whichever way it is set.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createMemoryStorage, installMemoryStorage } from '../helpers/memoryStorage.js';

const local = installMemoryStorage();
const tab = createMemoryStorage();
Object.defineProperty(globalThis, 'sessionStorage', { value: tab, configurable: true, writable: true });

const { createStencil, makeApp } = await import('../helpers/stencilApiRig.js');
const { readSessionKey, SESSION_KEY_TTL_MS } = await import('../../js/llm/sessionKey.js');

const KEY = 'sk-ant-test-0123456789abcdef';
const leaked = () => [...local._map.values()].some((v) => v.includes(KEY));

test('setup({ provider: anthropic, apiKey }) holds the key for the session and saves the rest', () => {
  local.clear(); tab.clear();
  const stencil = createStencil(makeApp());
  const before = Date.now();
  assert.equal(stencil.llm.setup({ provider: 'anthropic', model: 'claude-haiku-4-5', apiKey: KEY }), stencil);
  assert.equal(stencil.llm.provider, 'anthropic');
  assert.equal(stencil.llm.baseUrl, 'https://api.anthropic.com');
  assert.equal(stencil.llm.model, 'claude-haiku-4-5');
  assert.equal(readSessionKey()?.key, KEY);
  assert.ok(stencil.llm.keyExpiresAt >= before + SESSION_KEY_TTL_MS);
  assert.equal(JSON.parse(local.getItem('drawingApp_llmSettings')).apiKey, '');
  assert.equal(leaked(), false);
});

test('the key reads back redacted, never as itself, and forgetKey drops it', () => {
  local.clear(); tab.clear();
  const stencil = createStencil(makeApp());
  stencil.llm.provider = 'anthropic';
  assert.equal(stencil.llm.apiKey, '', 'none held yet');
  assert.equal(stencil.llm.keyExpiresAt, 0);
  stencil.llm.apiKey = KEY;
  assert.equal(stencil.llm.apiKey, '[redacted]');
  assert.ok(!JSON.stringify({ ...stencil.llm }).includes(KEY), 'no enumerable member leaks it');
  assert.equal(stencil.llm.forgetKey(), stencil);
  assert.equal(stencil.llm.apiKey, '');
  assert.equal(readSessionKey(), null);
  stencil.llm.apiKey = KEY;
  stencil.llm.apiKey = '';
  assert.equal(readSessionKey(), null, 'an empty key forgets');
  assert.equal(leaked(), false);
});

test('other providers keep their stored key; switching away never carries the session key', () => {
  local.clear(); tab.clear();
  const stencil = createStencil(makeApp());
  stencil.llm.setup({ provider: 'anthropic', apiKey: KEY });
  stencil.llm.setup({ provider: 'openai-compat', apiKey: 'sk-openai' });
  assert.equal(stencil.llm.apiKey, 'sk-openai', 'the openai-compat key reads back as before');
  assert.equal(readSessionKey()?.key, KEY, 'the anthropic key stays held, apart');
  stencil.llm.apiKey = KEY;
  assert.equal(JSON.parse(local.getItem('drawingApp_llmSettings')).apiKey, '', 'the session key is never saved under any provider');
  assert.equal(leaked(), false);
});

test('a browser that refuses session storage says so instead of dropping the key silently', () => {
  local.clear();
  const refusing = { getItem: () => null, setItem() { throw new Error('SecurityError'); }, removeItem() {} };
  Object.defineProperty(globalThis, 'sessionStorage', { value: refusing, configurable: true, writable: true });
  const stencil = createStencil(makeApp());
  assert.throws(() => stencil.llm.setup({ provider: 'anthropic', apiKey: KEY }), /refuses session storage/);
  assert.equal(leaked(), false);
  Object.defineProperty(globalThis, 'sessionStorage', { value: tab, configurable: true, writable: true });
});
