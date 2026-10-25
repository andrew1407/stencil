// applyLaunchQuery's assistant keys (js/console/launchQuery.js): a link that re-points the provider
// or an endpoint, or brings another key, asks first and goes through stencil.llm, a declined one
// changes nothing, and a moved endpoint never inherits the old one's key (llm-providers.md §5).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { boot, notices, texts, called, lastCall, applyLaunchQuery } from '../helpers/launchQueryRig.js';

const SETTINGS = 'drawingApp_llmSettings';
const asking = (answer) => ({
  confirm(msg, opts) { this.calls.push(['confirm', msg, opts.title]); return Promise.resolve(answer); },
});
const fresh = (saved = null) => {
  if (saved) localStorage.setItem(SETTINGS, JSON.stringify(saved));
  else localStorage.removeItem(SETTINGS);
};

test('a link that names a provider and endpoint asks, naming both, then goes through stencil.llm.setup', async () => {
  fresh();
  const { app, stencil } = boot('?llm-provider=ollama&llm-model=llava&llm-url=http://localhost:11434', '', asking(true));
  await applyLaunchQuery({ app, stencil });
  const [, msg, title] = lastCall(app, 'confirm');
  assert.equal(title, 'Change the assistant');
  assert.match(msg, /provider ollama, baseUrl http:\/\/localhost:11434/);
  assert.equal(stencil.llm.provider, 'ollama');
  assert.equal(stencil.llm.model, 'llava');
  assert.equal(stencil.llm.baseUrl, 'http://localhost:11434');
  assert.deepEqual(notices, []);
});

test('a fragment key becomes this tab\'s session key once the switch is accepted, and is never saved', async () => {
  fresh();
  const keyed = boot('', '#llm-provider=anthropic&llm-api-key=sk-test-1234', asking(true));
  await applyLaunchQuery(keyed);
  assert.equal(keyed.stencil.llm.provider, 'anthropic');
  assert.equal(keyed.stencil.llm.apiKey, '[redacted]');
  assert.ok(keyed.stencil.llm.keyExpiresAt > Date.now());
  assert.equal(localStorage.getItem(SETTINGS).includes('sk-test'), false, 'never persisted');
  keyed.stencil.llm.forgetKey();
});

test('a declined switch leaves every assistant setting as it was and says so', async () => {
  const saved = { provider: 'openai-compat', baseUrl: 'https://llm.example/v1', model: 'm', apiKey: 'sk-mine', serverUrl: '' };
  fresh(saved);
  const { app, stencil } = boot('', '#llm-url=https://evil.example&llm-provider=openai-compat', asking(false));
  await applyLaunchQuery({ app, stencil });
  assert.equal(called(app, 'confirm').length, 1);
  assert.equal(stencil.llm.baseUrl, 'https://llm.example/v1');
  assert.equal(stencil.llm.apiKey, 'sk-mine');
  assert.deepEqual(texts(), ['Launch link: assistant — left unchanged — the change was declined']);
});

test('an accepted endpoint move drops the stored key unless the link brings its own', async () => {
  fresh({ provider: 'openai-compat', baseUrl: 'https://llm.example/v1', model: 'm', apiKey: 'sk-mine', serverUrl: '' });
  const moved = boot('', '#llm-url=https://other.example/v1', asking(true));
  await applyLaunchQuery(moved);
  assert.equal(moved.stencil.llm.baseUrl, 'https://other.example/v1');
  assert.equal(moved.stencil.llm.apiKey, '', 'the old endpoint\'s key stays behind');
  assert.equal(localStorage.getItem(SETTINGS).includes('sk-mine'), false);

  fresh({ provider: 'openai-compat', baseUrl: 'https://llm.example/v1', model: 'm', apiKey: 'sk-mine', serverUrl: '' });
  const own = boot('', '#llm-url=https://other.example/v1&llm-api-key=sk-theirs', asking(true));
  await applyLaunchQuery(own);
  assert.equal(own.stencil.llm.apiKey, 'sk-theirs');
});

test('a link that changes only the model, or names the endpoint already set, asks nothing', async () => {
  fresh({ provider: 'ollama', baseUrl: 'http://localhost:11434', model: 'a', apiKey: '', serverUrl: '' });
  const { app, stencil } = boot('?llm-model=llava&llm-provider=ollama', '', asking(false));
  await applyLaunchQuery({ app, stencil });
  assert.equal(called(app, 'confirm').length, 0);
  assert.equal(stencil.llm.model, 'llava');
});

test('a link that brings only a different key asks without showing it, and a decline keeps the saved key', async () => {
  fresh({ provider: 'openai-compat', baseUrl: 'https://llm.example/v1', model: 'm', apiKey: 'sk-mine', serverUrl: '' });
  const { app, stencil } = boot('', '#llm-api-key=sk-attacker', asking(false));
  await applyLaunchQuery({ app, stencil });
  const [, msg] = lastCall(app, 'confirm');
  assert.match(msg, /a different API key/);
  assert.equal(msg.includes('sk-attacker'), false);
  assert.equal(stencil.llm.apiKey, 'sk-mine');

  fresh({ provider: 'openai-compat', baseUrl: 'https://llm.example/v1', model: 'm', apiKey: 'sk-mine', serverUrl: '' });
  const same = boot('', '#llm-api-key=sk-mine', asking(false));
  await applyLaunchQuery(same);
  assert.equal(called(same.app, 'confirm').length, 0, 'the key already set asks nothing');
});
