// The Visuals dialog's one voice knob (js/ui/visualsVoiceRow.js): Send-after-pause. The row
// is a wire between the number field and the shared store, so what is pinned is that it
// CLAMPS what was typed before it saves, and that reset goes back to the documented default.
import test from 'node:test';
import assert from 'node:assert';

import { installMemoryStorage } from './helpers/memoryStorage.js';
import { createStubElement, installDom } from './helpers/dom.js';

installMemoryStorage();
const field = createStubElement('input');
installDom().register('vs-voice-silence', field);

const { loadVoiceSettings, saveVoiceSettings, SILENCE_MS_DEFAULT, SILENCE_MS_MIN, SILENCE_MS_MAX } =
  await import('../js/llm/voiceSettings.js');
const { wireVoiceSilenceRow } = await import('../js/ui/visuals/visualsVoiceRow.js');

const row = wireVoiceSilenceRow();
const type = (v) => { field.value = v; field.dispatch('change', { target: field }); };

test('populate shows what is stored', () => {
  saveVoiceSettings({ silenceMs: 2500, language: 'default' });
  row.populate();
  assert.strictEqual(Number(field.value), 2500);
});

test('a typed pause is clamped in the FIELD as well as in the store', () => {
  type(String(SILENCE_MS_MAX + 5000));
  assert.strictEqual(Number(field.value), SILENCE_MS_MAX, 'the reader sees the value that was kept');
  assert.strictEqual(loadVoiceSettings().silenceMs, SILENCE_MS_MAX);

  type('10');
  assert.strictEqual(Number(field.value), SILENCE_MS_MIN);
  assert.strictEqual(loadVoiceSettings().silenceMs, SILENCE_MS_MIN);
});

test('junk falls back to the default rather than poisoning the store', () => {
  type('soon');
  assert.strictEqual(loadVoiceSettings().silenceMs, SILENCE_MS_DEFAULT);
});

test('saving the pause leaves the language alone — this row owns one knob', () => {
  saveVoiceSettings({ silenceMs: 1000, language: 'uk-UA' });
  type('3000');
  assert.deepStrictEqual(loadVoiceSettings(), { silenceMs: 3000, language: 'uk-UA' });
});

test('reset restores the default pause, and populate then shows it', () => {
  type('4000');
  row.reset();
  assert.strictEqual(loadVoiceSettings().silenceMs, SILENCE_MS_DEFAULT);
  row.populate();
  assert.strictEqual(Number(field.value), SILENCE_MS_DEFAULT);
});
