import { test } from 'node:test';
import assert from 'node:assert';
import { installMemoryStorage } from './helpers/memoryStorage.js';

const mem = installMemoryStorage()._map;

const {
  defaultVoiceSettings, loadVoiceSettings, saveVoiceSettings,
  clampSilenceMs, normalizeLanguage, recognitionLang, isLanguageTag,
  SILENCE_MS_DEFAULT, SILENCE_MS_MIN, SILENCE_MS_MAX, VOICE_SETTINGS_EVENT, VOICE_LANGUAGES,
} = await import('../js/llm/voiceSettings.js');

test('defaults: 1000 ms of silence, the default (English) language', () => {
  mem.clear();
  assert.deepStrictEqual(defaultVoiceSettings(), { silenceMs: 1000, language: 'default' });
  assert.strictEqual(SILENCE_MS_DEFAULT, 1000);
  assert.deepStrictEqual(loadVoiceSettings(), defaultVoiceSettings());
  // The dialog's menu starts with the default row and only lists real tags.
  assert.strictEqual(VOICE_LANGUAGES[0][0], 'default');
  for (const [tag] of VOICE_LANGUAGES.slice(1)) assert.ok(isLanguageTag(tag), tag);
});

test('clampSilenceMs: range 500..10000, garbage → the default, rounding', () => {
  assert.strictEqual(clampSilenceMs('abc'), 1000);
  assert.strictEqual(clampSilenceMs(undefined), 1000);
  assert.strictEqual(clampSilenceMs(100), SILENCE_MS_MIN);
  assert.strictEqual(clampSilenceMs(99999), SILENCE_MS_MAX);
  assert.strictEqual(clampSilenceMs(2500.6), 2501);
  assert.strictEqual(clampSilenceMs('3000'), 3000);
});

test('normalizeLanguage: default / BCP-47 tags / anything else falls back', () => {
  for (const [input, want] of [
    ['', 'default'], ['default', 'default'], ['DEFAULT', 'default'], [undefined, 'default'], [42, 'default'],
    ['en-US', 'en-US'], ['pt-BR', 'pt-BR'], ['zh-Hans-CN', 'zh-Hans-CN'], ['  uk-UA ', 'uk-UA'], ['en', 'en'],
    ['en US', 'default'], ['english', 'default'], ['e', 'default'],
  ]) assert.strictEqual(normalizeLanguage(input), want, String(input));
  assert.strictEqual(recognitionLang('default'), 'en-US');
  assert.strictEqual(recognitionLang(''), 'en-US');
  assert.strictEqual(recognitionLang('de-DE'), 'de-DE');
});

test('save round-trips a slim, clamped object and drops extras', () => {
  mem.clear();
  saveVoiceSettings({ silenceMs: 250, language: 'fr-FR', extra: 'dropped' });
  assert.deepStrictEqual(JSON.parse(mem.get('drawingApp_voiceSettings')), { silenceMs: 500, language: 'fr-FR' });
  assert.deepStrictEqual(loadVoiceSettings(), { silenceMs: 500, language: 'fr-FR' });
});

test('load: corrupt JSON or bad types degrade to defaults', () => {
  mem.clear();
  mem.set('drawingApp_voiceSettings', '{not json');
  assert.deepStrictEqual(loadVoiceSettings(), defaultVoiceSettings());
  mem.set('drawingApp_voiceSettings', JSON.stringify({ silenceMs: 'soon', language: 7 }));
  assert.deepStrictEqual(loadVoiceSettings(), defaultVoiceSettings());
  mem.set('drawingApp_voiceSettings', JSON.stringify({ silenceMs: 50000, language: 'bad tag' }));
  assert.deepStrictEqual(loadVoiceSettings(), { silenceMs: 10000, language: 'default' });
});

test('save dispatches the settings event when a window exists, and never throws', () => {
  mem.clear();
  const seen = [];
  const prev = globalThis.window;
  globalThis.window = { dispatchEvent: (e) => { seen.push(e.type); return true; } };
  try {
    saveVoiceSettings({ silenceMs: 2000, language: 'default' });
    assert.deepStrictEqual(seen, [VOICE_SETTINGS_EVENT]);
    // A blocked store keeps the session alive: no throw, the event still fires.
    globalThis.localStorage.throwOnSet = true;
    assert.doesNotThrow(() => saveVoiceSettings({ silenceMs: 3000, language: 'default' }));
    assert.strictEqual(seen.length, 2);
    globalThis.localStorage.throwOnSet = false;
  } finally {
    if (prev === undefined) delete globalThis.window; else globalThis.window = prev;
  }
  // Without a window the dispatch is skipped silently.
  assert.doesNotThrow(() => saveVoiceSettings({ silenceMs: 2000, language: 'default' }));
});
