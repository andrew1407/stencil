// Tests for the LLM assistant settings (src/llm/llmSettings.js): §5 defaults,
// the stencil-server default following the first stored connection, and the
// chrome.storage round-trip — driven with the storage mock idiom from
// connections.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  LLM_SETTINGS_KEY, PROVIDERS, PROVIDER_BASE_URLS,
  defaultSettings, loadLlmSettings, saveLlmSettings, assistantEnabled, isHttpUrl,
} from '../src/llm/llmSettings.js';
import { applyAssistantVisibility } from '../src/popup/assistant.js';

import { installChromeStub } from './helpers/chromeStub.js';

const installStorageMock = () => {
  const stub = installChromeStub();
  // reset also removes chrome, so the no-chrome default-path tests stay honest.
  return { peek: stub.peek, reset: () => { stub.reset(); stub.restore(); } };
};

test('provider table matches contract §5', () => {
  // 'none' is the local-only off switch (contract §5) ahead of the three wire providers.
  assert.deepEqual(PROVIDERS, ['none', 'ollama', 'openai-compat', 'stencil-server']);
  assert.equal(PROVIDER_BASE_URLS.ollama, 'http://localhost:11434');
  assert.equal(PROVIDER_BASE_URLS['openai-compat'], 'http://localhost:1234/v1');
  assert.equal(PROVIDER_BASE_URLS['stencil-server'], '');
});

test('defaults: ollama on its standard port, empty model/key, no server, tabs unshared', () => {
  assert.deepEqual(defaultSettings(), {
    provider: 'ollama',
    baseUrl: 'http://localhost:11434',
    model: '',
    apiKey: '',
    serverUrl: '',
    serverToken: '',
    shareTabs: false,
  });
});

test('default serverUrl follows the FIRST stored server connection', async () => {
  const conns = [{ url: 'http://srv:8090', token: 't1' }, { url: 'http://other:1', token: 't2' }];
  assert.equal(defaultSettings(conns).serverUrl, 'http://srv:8090');
  // loadLlmSettings without chrome degrades to the defaults but still applies them.
  const s = await loadLlmSettings({ connections: conns });
  assert.equal(s.serverUrl, 'http://srv:8090');
  assert.equal(s.provider, 'ollama');
});

test('loadLlmSettings without chrome (Node) returns plain defaults', async () => {
  delete globalThis.chrome;
  const s = await loadLlmSettings({ connections: [] });
  assert.deepEqual(s, defaultSettings());
});

test('save → load round-trips through the chrome.storage stub under key llmSettings', async () => {
  const mock = installStorageMock();
  const saved = await saveLlmSettings({
    provider: 'openai-compat', baseUrl: 'http://box:9000/v1', model: 'qwen-vl',
    apiKey: 'sk-x', serverUrl: '', serverToken: '',
  });
  assert.equal(mock.peek()[LLM_SETTINGS_KEY].baseUrl, 'http://box:9000/v1');
  const s = await loadLlmSettings({ connections: [] });
  assert.deepEqual(s, saved);
  assert.equal(s.provider, 'openai-compat');
  assert.equal(s.model, 'qwen-vl');
  assert.equal(s.apiKey, 'sk-x');
  mock.reset();
});

// §8 opt-in: default OFF, and only an explicit true turns it on — a truthy leftover
// from an older stored shape must not silently enable it.
test('shareTabs is off by default and only an explicit true enables it', async () => {
  assert.equal(defaultSettings().shareTabs, false);

  const mock = installStorageMock();
  // Never saved → off.
  assert.equal((await loadLlmSettings({ connections: [] })).shareTabs, false);

  await saveLlmSettings({ provider: 'ollama', shareTabs: true });
  assert.equal((await loadLlmSettings({ connections: [] })).shareTabs, true);

  await saveLlmSettings({ provider: 'ollama', shareTabs: false });
  assert.equal((await loadLlmSettings({ connections: [] })).shareTabs, false);

  // Truthy-but-not-true (a string from an older/hand-edited store) stays off.
  await saveLlmSettings({ provider: 'ollama', shareTabs: 'yes' });
  assert.equal(mock.peek()[LLM_SETTINGS_KEY].shareTabs, false);
  assert.equal((await loadLlmSettings({ connections: [] })).shareTabs, false);

  // A non-boolean already in storage is ignored by the merge, not coerced.
  await globalThis.chrome.storage.local.set({ [LLM_SETTINGS_KEY]: { provider: 'ollama', shareTabs: 1 } });
  assert.equal((await loadLlmSettings({ connections: [] })).shareTabs, false);
  mock.reset();
});

test('serverToken (extension extra) persists too', async () => {
  const mock = installStorageMock();
  await saveLlmSettings({ provider: 'stencil-server', serverUrl: 'http://srv:8090', serverToken: 'tok-x' });
  const s = await loadLlmSettings({ connections: [] });
  assert.equal(s.provider, 'stencil-server');
  assert.equal(s.serverUrl, 'http://srv:8090');
  assert.equal(s.serverToken, 'tok-x');
  mock.reset();
});

test('a bad saved provider degrades to ollama; empty serverUrl re-follows connections', async () => {
  const mock = installStorageMock();
  await globalThis.chrome.storage.local.set({ [LLM_SETTINGS_KEY]: { provider: 'weird', serverUrl: '' } });
  const s = await loadLlmSettings({ connections: [{ url: 'http://srv:1', token: 't' }] });
  assert.equal(s.provider, 'ollama');
  assert.equal(s.serverUrl, 'http://srv:1');
  mock.reset();
});

test('non-string saved values are ignored (defaults win)', async () => {
  const mock = installStorageMock();
  await globalThis.chrome.storage.local.set({ [LLM_SETTINGS_KEY]: { provider: 42, baseUrl: null, model: ['x'] } });
  const s = await loadLlmSettings({ connections: [] });
  assert.equal(s.provider, 'ollama');
  assert.equal(s.baseUrl, 'http://localhost:11434');
  assert.equal(s.model, '');
  mock.reset();
});

// Contract §5 provider 'none': switched off, the surfaces must not offer the assistant AT ALL — no
// section header, no ✦ button. The same rule the browser and desktop menus gate on.

test('assistantEnabled: only the local-only "none" provider switches it off', () => {
  assert.equal(assistantEnabled({ provider: 'none' }), false);
  for (const provider of PROVIDERS.filter((p) => p !== 'none'))
    assert.equal(assistantEnabled({ provider }), true, provider);
  // Unrecognised values count as ON — loadLlmSettings has already coerced them to the
  // default provider, so this can only be a hand-built object.
  assert.equal(assistantEnabled({ provider: 'weird' }), true);
  assert.equal(assistantEnabled({}), true);
  assert.equal(assistantEnabled(null), false);
  assert.equal(assistantEnabled(undefined), false);
});

test('assistantEnabled reads a stored "none" back off chrome.storage', async () => {
  const mock = installStorageMock();
  await globalThis.chrome.storage.local.set({ [LLM_SETTINGS_KEY]: { provider: 'none' } });
  assert.equal(assistantEnabled(await loadLlmSettings({ connections: [] })), false);
  await globalThis.chrome.storage.local.set({ [LLM_SETTINGS_KEY]: { provider: 'ollama' } });
  assert.equal(assistantEnabled(await loadLlmSettings({ connections: [] })), true);
  mock.reset();
});

test('applyAssistantVisibility hides BOTH the section and the ✦ button, reversibly', () => {
  const section = { hidden: false };
  const button = { hidden: false };
  assert.equal(applyAssistantVisibility(false, { section, button }), false);
  assert.equal(section.hidden, true, 'no collapsed ASSISTANT header is left behind');
  assert.equal(button.hidden, true, 'and no button that would reveal nothing');

  // Picking a provider in Options brings both back — no reload.
  assert.equal(applyAssistantVisibility(true, { section, button }), true);
  assert.equal(section.hidden, false);
  assert.equal(button.hidden, false);

  // Missing elements (a surface without one) are simply skipped.
  assert.doesNotThrow(() => applyAssistantVisibility(false, {}));
  assert.doesNotThrow(() => applyAssistantVisibility(true));
});

// NEGATIVE: chrome.storage is writable by anything running in the extension, so a poisoned entry
// must not aim the client at another scheme (browser/js/llm/llmSettings.js checks the same).
test('an endpoint saved on a non-http(s) scheme is dropped for the default', async () => {
  const mock = installStorageMock();
  for (const bad of ['javascript:fetch(1)', 'file:///etc/passwd', 'chrome-extension://abc/x',
    'data:text/html,x', '//evil.example']) {
    assert.equal(isHttpUrl(bad), false, bad);
    await saveLlmSettings({ provider: 'ollama', baseUrl: bad, serverUrl: bad });
    const s = await loadLlmSettings({ connections: [] });
    assert.equal(s.baseUrl, PROVIDER_BASE_URLS.ollama, bad);
    assert.equal(s.serverUrl, '', bad);
  }
  await saveLlmSettings({ provider: 'ollama', baseUrl: 'HTTPS://box:9000/v1', serverUrl: 'http://srv:8090' });
  const ok = await loadLlmSettings({ connections: [] });
  assert.equal(ok.baseUrl, 'HTTPS://box:9000/v1');
  assert.equal(ok.serverUrl, 'http://srv:8090');
  mock.reset();
});
