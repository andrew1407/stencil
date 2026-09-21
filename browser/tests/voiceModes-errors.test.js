// Voice chat's turn chain and failure paths: serialized utterances, a synchronous refusal, a
// fatal engine error, a live language swap and the level subscribers. From voiceModes.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { VOICE_SETTINGS_EVENT } from '../js/llm/voice/settings.js';
import { make } from './helpers/voiceModesRig.js';

test('voice chat serializes utterances behind a slow turn; a failed turn breaks neither chain nor mode', async () => {
  const m = make({ settings: { silenceMs: 500 } });
  const { voice, engine, clock, sent } = m;
  voice.voiceChat = true;
  engine.hear('first one send');
  assert.deepStrictEqual(sent, ['first one']);
  engine.hear('second one execute it');
  await Promise.resolve();
  assert.deepStrictEqual(sent, ['first one'], 'the second waits for the first turn');
  m.pending[0].reject(new Error('provider down'));
  await Promise.resolve(); await Promise.resolve(); await Promise.resolve();
  assert.deepStrictEqual(sent, ['first one', 'second one']);
  assert.strictEqual(voice.voiceChat, true);
  assert.strictEqual(m.pending.at(-1).meta.reason, 'phrase');
  engine.hear('third');
  clock.advance(500);
  await m.settle();
  await Promise.resolve();
  assert.deepStrictEqual(sent, ['first one', 'second one', 'third']);
  assert.strictEqual(m.pending.at(-1).meta.reason, 'silence');
});

test('a platform that refuses synchronously during start leaves startComposer false and the mode off', () => {
  const { voice, engine, notes, target } = make();
  engine.start = (s) => { engine.calls.push(['start', s.lang]); engine.session = s; s.onState?.('idle', { error: 'not-allowed' }); s.onError?.({ code: 'not-allowed', fatal: true, text: 'denied' }); return true; };
  const t = target();
  assert.strictEqual(voice.startComposer(t), false);
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(t.stops, ['error']);
  assert.deepStrictEqual(notes, [['denied', 'fail']]);
  voice.voiceChat = true;
  assert.strictEqual(voice.voiceChat, false, 'the setter cannot hold a mode the platform refused');
});

test('a fatal engine error turns the mode off, toasts, and reports the error in the state event', () => {
  const { voice, engine, win, notes, target } = make();
  const t = target();
  voice.startComposer(t);
  engine.fail('not-allowed');
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(t.stops, ['error']);
  assert.deepStrictEqual(notes, [['boom not-allowed', 'fail']]);
  const last = win.events.filter((e) => typeof e === 'object').at(-1);
  assert.deepStrictEqual(last, { mode: 'off', listening: false, supported: true, reason: 'error', error: 'not-allowed' });
});

test('a language change while active hot-swaps once; silence changes apply on the next arm', () => {
  const { voice, engine, cfg, win, clock, sent } = make();
  voice.voiceChat = true;
  assert.deepStrictEqual(engine.calls, [['start', 'en-US']]);
  win.fire(VOICE_SETTINGS_EVENT);   // nothing changed
  assert.strictEqual(engine.calls.length, 1);
  cfg.language = 'de-DE';
  win.fire(VOICE_SETTINGS_EVENT);
  assert.deepStrictEqual(engine.calls, [['start', 'en-US'], ['start', 'de-DE']]);
  cfg.silenceMs = 3000;
  engine.hear('warten');
  assert.deepStrictEqual(clock.pendingDelays, [3000]);
  clock.advance(3000);
  assert.deepStrictEqual(sent, ['warten']);
  voice.dispose();
  assert.strictEqual(voice.mode, 'off');
  cfg.language = 'fr-FR';
  win.fire(VOICE_SETTINGS_EVENT);   // unsubscribed
  assert.strictEqual(engine.calls.length, 3);
});

test('level subscribers hear every frame and can unsubscribe', () => {
  const { voice, engine } = make();
  const seen = [];
  const off = voice.onLevel((l) => seen.push(l));
  voice.voiceChat = true;
  engine.level(0.3);
  assert.strictEqual(voice.level, 0.3);
  off();
  engine.level(0.6);
  assert.deepStrictEqual(seen, [0.3]);
  assert.strictEqual(voice.level, 0.6);
});

// The mic's ring lives OUTSIDE its tile, but layout.css clips every button and a z-index:-1 pseudo on
// a non-isolated one paints behind the toolbar: the listening states lift both and drop the sweep.
