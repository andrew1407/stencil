// The voice surface (js/console/stencilApi.js): voiceChat and chat.voiceInput route to the
// app's coordinators, and the language / silence settings validate and persist.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp } from './helpers/stencilApiRig.js';

// ── Voice input ──────────────────────────────────────────────────────────────────
test('voiceChat get/set routes to app.voice, and throws before the coordinator exists', () => {
  const app = makeApp();
  const stencil = createStencil(app);
  assert.equal(stencil.voiceChat, false);
  assert.throws(() => { stencil.voiceChat = true; }, /not ready/);
  let on = false;
  app.voice = { supported: true, get voiceChat() { return on; }, set voiceChat(v) { on = v; } };
  stencil.voiceChat = true;
  assert.equal(on, true);
  assert.equal(stencil.voiceChat, true);
  stencil.voiceChat = 0;
  assert.equal(on, false);
  // An unsupported browser's refusal comes through as the coordinator's own error.
  app.voice = { supported: false, get voiceChat() { return false; }, set voiceChat(_v) { throw new Error('Voice input is not supported in this browser'); } };
  assert.throws(() => { stencil.voiceChat = true; }, /not supported/);
});

test('chat.voiceInput drives the panel composer\'s mic face through app.chat', () => {
  const app = makeApp();
  const stencil = createStencil(app);
  assert.equal(stencil.chat.voiceInput, false);
  assert.throws(() => { stencil.chat.voiceInput = true; }, /not ready/);
  const calls = [];
  app.chat = { voiceInput: false, setVoiceInput: (v) => { calls.push(v); app.chat.voiceInput = v; } };
  stencil.chat.voiceInput = true;
  assert.deepEqual(calls, [true]);
  assert.equal(stencil.chat.voiceInput, true);
  stencil.chat.voiceInput = false;
  assert.deepEqual(calls, [true, false]);
});

test('voiceInputLanguage / voiceSilenceMs validate, clamp and persist through the voice store', async () => {
  const { installMemoryStorage } = await import('./helpers/memoryStorage.js');
  const mem = installMemoryStorage();
  try {
    const stencil = createStencil(makeApp());
    assert.equal(stencil.voiceInputLanguage, 'default');
    assert.equal(stencil.voiceSilenceMs, 1000);
    stencil.voiceInputLanguage = 'de-DE';
    assert.equal(stencil.voiceInputLanguage, 'de-DE');
    assert.equal(JSON.parse(mem._map.get('drawingApp_voiceSettings')).language, 'de-DE');
    stencil.settings.voiceInputLanguage = '';          // '' and 'default' both mean the default
    assert.equal(stencil.voiceInputLanguage, 'default');
    assert.throws(() => { stencil.voiceInputLanguage = 'english'; }, /BCP-47/);
    stencil.voiceSilenceMs = 250;                      // clamped to the range
    assert.equal(stencil.voiceSilenceMs, 500);
    stencil.settings.voiceSilenceMs = '2500';
    assert.equal(stencil.voiceSilenceMs, 2500);
    assert.throws(() => { stencil.voiceSilenceMs = 'soon'; }, /milliseconds/);
    assert.equal(stencil.voiceSilenceMs, 2500, 'a rejected write changes nothing');
  } finally {
    mem.restore();
  }
});
