// src/llm/sessionKey.js and its callers — the anthropic session key (llm-providers.md §5) in
// chrome.storage.session: the providers.json TTL, dropped on expiry and on forget, gone with a new
// browser session, never in storage.local, trusted contexts only, and the missing key's wording.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import PROVIDERS_ASSET from '../../src/config/providers.json' with { type: 'json' };
import {
  SESSION_KEY_ITEM, SESSION_KEY_TTL_MS, lockSessionKeyArea, readSessionKey, sessionKey, setSessionKey, forgetSessionKey,
} from '../../src/llm/sessionKey.js';
import { saveLlmSettings, loadLlmSettings, withSessionKey, LLM_SETTINGS_KEY } from '../../src/llm/settings.js';
import { createLlmClient, LlmError } from '../../src/llm/client.js';
import { turnFailureText, isUnreachableError } from '../../src/llm/surface.js';
import { providerOptions } from '../../src/options/providerOptions.js';
import { installChromeStub } from '../helpers/chromeStub.js';

const KEY = 'sk-ant-test-0123456789abcdef';
const T0 = Date.UTC(2026, 8, 27, 9, 0);
const anthropic = { provider: 'anthropic', baseUrl: 'https://api.anthropic.test', model: '', apiKey: '', serverUrl: '', serverToken: '', shareTabs: false };
const src = (rel) => readFileSync(new URL(`../../src/${rel}`, import.meta.url), 'utf8');

test('held in chrome.storage.session for providers.json ttlMinutes, never in storage.local', async () => {
  const stub = installChromeStub();
  try {
    assert.equal(SESSION_KEY_TTL_MS, PROVIDERS_ASSET.providers.anthropic.sessionKey.ttlMinutes * 60_000);
    assert.equal(await setSessionKey(` ${KEY} `, T0), T0 + SESSION_KEY_TTL_MS);
    assert.deepEqual(stub.peekSession()[SESSION_KEY_ITEM], { key: KEY, expiresAt: T0 + SESSION_KEY_TTL_MS });
    assert.equal(await sessionKey(T0 + 1), KEY);
    assert.deepEqual(stub.peek(), {}, 'storage.local untouched');
  } finally { stub.restore(); }
});

test('expired or malformed records are dropped on read; forget and an empty key drop it now', async () => {
  const stub = installChromeStub();
  try {
    await setSessionKey(KEY, T0);
    assert.equal(await readSessionKey(T0 + SESSION_KEY_TTL_MS), null);
    assert.equal(stub.peekSession()[SESSION_KEY_ITEM], undefined);
    await setSessionKey(KEY, T0);
    await forgetSessionKey();
    assert.equal(await sessionKey(T0), '');
    await setSessionKey(KEY, T0);
    assert.equal(await setSessionKey('', T0), 0);
    assert.equal(await sessionKey(T0), '');
    for (const bad of ['sk', { key: '' , expiresAt: T0 + 1 }, { key: KEY }]) {
      stub.peekSession()[SESSION_KEY_ITEM] = bad;
      assert.equal(await readSessionKey(T0), null, JSON.stringify(bad));
      assert.equal(stub.peekSession()[SESSION_KEY_ITEM], undefined);
    }
  } finally { stub.restore(); }
});

test('a new browser session (or an extension reload) starts without it', async () => {
  let stub = installChromeStub();
  await setSessionKey(KEY, T0);
  stub.restore();
  stub = installChromeStub();
  try { assert.equal(await sessionKey(T0), ''); } finally { stub.restore(); }
});

test('inert without chrome and when storage throws; the worker pins trusted contexts', async () => {
  assert.equal(await setSessionKey(KEY, T0), 0);
  assert.equal(await readSessionKey(T0), null);
  await assert.doesNotReject(forgetSessionKey());
  await assert.doesNotReject(lockSessionKeyArea());
  const stub = installChromeStub({ storageThrows: true });
  try {
    assert.equal(await setSessionKey(KEY, T0), 0);
    assert.equal(await sessionKey(T0), '');
  } finally { stub.restore(); }
  const ok = installChromeStub();
  try {
    await lockSessionKeyArea();
    assert.deepEqual(ok.sessionAccess, [{ accessLevel: 'TRUSTED_CONTEXTS' }]);
  } finally { ok.restore(); }
  assert.match(src('background/background.js'), /^lockSessionKeyArea\(\);/m);
});

test('settings never store the anthropic key; withSessionKey adds it to one request', async () => {
  const stub = installChromeStub();
  try {
    await setSessionKey(KEY);
    await saveLlmSettings({ ...anthropic, apiKey: KEY, model: 'claude-haiku-4-5' });
    assert.equal(stub.peek()[LLM_SETTINGS_KEY].apiKey, '');
    assert.equal(stub.peek()[LLM_SETTINGS_KEY].model, 'claude-haiku-4-5');
    await saveLlmSettings({ ...anthropic, provider: 'openai-compat', apiKey: KEY });
    assert.equal(stub.peek()[LLM_SETTINGS_KEY].apiKey, '', 'the held key is never saved under any provider');
    stub.peek()[LLM_SETTINGS_KEY] = { ...anthropic, apiKey: 'left-over' };
    assert.equal((await loadLlmSettings({ connections: [] })).apiKey, '');
    assert.equal((await withSessionKey(anthropic)).apiKey, KEY);
    assert.equal(anthropic.apiKey, '', 'a copy — the settings object never holds it');
    const openai = { provider: 'openai-compat', apiKey: 'sk-openai' };
    assert.equal(await withSessionKey(openai), openai);

    const seen = [];
    const fetchImpl = async (url, init) => {
      seen.push(init);
      return { ok: true, json: async () => ({ stop_reason: 'end_turn', content: [{ type: 'text', text: 'ok' }] }) };
    };
    const client = createLlmClient({ settings: await withSessionKey(await loadLlmSettings({ connections: [] })), fetchImpl });
    assert.equal(await client.chat({ system: 's', messages: [{ role: 'user', text: 'hi' }] }), 'ok');
    assert.equal(seen[0].headers['x-api-key'], KEY);
    assert.ok(!JSON.stringify(stub.peek()).includes(KEY), 'storage.local never saw the key');
  } finally { stub.restore(); }
});

test('no session key reads as the options asking for it, with the configure CTA', () => {
  const err = LlmError.disabled('no API key for this session');
  assert.equal(turnFailureText(anthropic, err),
    'Anthropic API (Claude): no API key for this session — enter your key in the extension options.');
  assert.equal(isUnreachableError(err, anthropic), true);
  assert.equal(isUnreachableError(LlmError.disabled('off'), { provider: 'stencil-server' }), false);
});

test('a plain-http refusal names the fix itself, without asking for the key', () => {
  const why = "refusing to send the API key to 'lan.test' over plain http — use https";
  assert.equal(turnFailureText(anthropic, LlmError.disabled(why)), `Anthropic API (Claude): ${why}.`);
});

test('the options page carries the provider, a password field, the expiry line and Forget', () => {
  assert.ok(providerOptions().some(([id, label]) => id === 'anthropic' && label === 'Anthropic API (Claude)'));
  const rows = src('options/llm.js');
  assert.match(rows, /<div id="llm-session-rows" hidden>/);
  assert.match(rows, /<input id="llm-sessionkey" type="password" autocomplete="off"/);
  for (const id of ['llm-sessionkey-status', 'llm-sessionkey-forget']) assert.ok(rows.includes(`id="${id}"`), id);
  assert.match(rows, /straight from the extension to Anthropic[\s\S]*never saved/);
  assert.match(rows, /getElementById\('llm-base-rows'\)\.insertAdjacentHTML\('beforeend', SESSION_ROWS_HTML\)/, 'last in the endpoint rows');
});
