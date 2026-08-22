import { test } from 'node:test';
import assert from 'node:assert';

// Install localStorage BEFORE importing (the store reads it lazily, but the
// stencil-server default reads the saved server set).
import { installMemoryStorage } from './helpers/memoryStorage.js';

const mem = installMemoryStorage()._map;

const { defaultSettings, loadLlmSettings, saveLlmSettings, serverBearerToken, PROVIDERS, PROVIDER_BASE_URLS } =
  await import('../js/llm/llmSettings.js');

test('defaults match the contract §5 table', () => {
  mem.clear();
  assert.deepStrictEqual(defaultSettings(), {
    provider: 'ollama',
    baseUrl: 'http://localhost:11434',
    model: '',
    apiKey: '',
    serverUrl: '',
    saveChats: false,   // §12: chat persistence ships OFF
  });
  // 'none' is the local-only off switch (contract §5) ahead of the three wire providers.
  assert.deepStrictEqual(PROVIDERS, ['none', 'ollama', 'openai-compat', 'stencil-server']);
  assert.strictEqual(PROVIDER_BASE_URLS.ollama, 'http://localhost:11434');
  assert.strictEqual(PROVIDER_BASE_URLS['openai-compat'], 'http://localhost:1234/v1');
});

test('stencil-server default serverUrl is the FIRST saved server connection', () => {
  mem.clear();
  mem.set('drawingApp_servers', JSON.stringify([
    { url: 'http://first:8090', token: 't1' },
    { url: 'http://second:8090', token: 't2' },
  ]));
  assert.strictEqual(defaultSettings().serverUrl, 'http://first:8090');
  assert.strictEqual(loadLlmSettings().serverUrl, 'http://first:8090');
});

test('loadLlmSettings returns pure defaults when nothing is saved', () => {
  mem.clear();
  assert.deepStrictEqual(loadLlmSettings(), defaultSettings());
});

test('saveLlmSettings round-trips overrides (slimmed to the contract shape)', () => {
  mem.clear();
  saveLlmSettings({
    provider: 'openai-compat', baseUrl: 'http://box:9999/v1', model: 'qwen-vl',
    apiKey: 'sk-1', serverUrl: 'http://srv:8090', extra: 'dropped',
  });
  const raw = JSON.parse(mem.get('drawingApp_llmSettings'));
  assert.deepStrictEqual(raw, {
    provider: 'openai-compat', baseUrl: 'http://box:9999/v1', model: 'qwen-vl',
    apiKey: 'sk-1', serverUrl: 'http://srv:8090', saveChats: false,
  });
  assert.deepStrictEqual(loadLlmSettings(), raw);
});

test('saveChats opt-in (§12) round-trips as a real boolean and defaults off', () => {
  mem.clear();
  saveLlmSettings({ ...defaultSettings(), saveChats: true });
  assert.strictEqual(loadLlmSettings().saveChats, true);
  // Only a literal boolean is honored — truthy junk saves (and loads) as off.
  saveLlmSettings({ ...defaultSettings(), saveChats: 'yes' });
  assert.strictEqual(loadLlmSettings().saveChats, false);
  mem.set('drawingApp_llmSettings', JSON.stringify({ saveChats: 'yes' }));
  assert.strictEqual(loadLlmSettings().saveChats, false);
});

test('saved overrides win over the first-server default', () => {
  mem.clear();
  mem.set('drawingApp_servers', JSON.stringify([{ url: 'http://first:8090', token: 't' }]));
  saveLlmSettings({ provider: 'stencil-server', serverUrl: 'http://other:8090' });
  assert.strictEqual(loadLlmSettings().serverUrl, 'http://other:8090');
});

test('serverBearerToken: live connection token first, saved token fallback, else empty', () => {
  mem.clear();
  mem.set('drawingApp_servers', JSON.stringify([{ url: 'http://srv:8090', token: 'saved-tkn' }]));
  const live = { connections: { get: (url) => (url === 'http://srv:8090' ? { token: 'live-tkn' } : null) } };
  assert.strictEqual(serverBearerToken(live, 'http://srv:8090'), 'live-tkn');
  assert.strictEqual(serverBearerToken({ connections: { get: () => null } }, 'http://srv:8090'), 'saved-tkn');
  assert.strictEqual(serverBearerToken(null, 'http://srv:8090'), 'saved-tkn');   // panel not wired yet
  assert.strictEqual(serverBearerToken(null, 'http://other:8090'), '');
});

test('corrupt or partial saved data degrades to defaults', () => {
  mem.clear();
  mem.set('drawingApp_llmSettings', '{not json');
  assert.deepStrictEqual(loadLlmSettings(), defaultSettings());

  mem.set('drawingApp_llmSettings', JSON.stringify({ model: 'only-model', provider: 42 }));
  const s = loadLlmSettings();
  assert.strictEqual(s.model, 'only-model');            // valid field kept
  assert.strictEqual(s.provider, 'ollama');             // non-string provider ignored
  assert.strictEqual(s.baseUrl, 'http://localhost:11434');

  mem.set('drawingApp_llmSettings', JSON.stringify({ provider: 'not-a-provider' }));
  assert.strictEqual(loadLlmSettings().provider, 'ollama');
});
