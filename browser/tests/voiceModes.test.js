// js/llm/voiceModes.js: the send-phrase split, the unsupported-platform guard and the
// composer/chat exclusivity. The rig lives in helpers/voiceModesRig.js.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  splitSendPhrase, SEND_PHRASES, UNSUPPORTED_TEXT,
} from '../js/llm/voiceModes.js';
import { make } from './helpers/voiceModesRig.js';

test('splitSendPhrase: the four phrases only as the whole tail, case and punctuation tolerant', () => {
  assert.deepStrictEqual(SEND_PHRASES, ['send it', 'execute it', 'send', 'execute']);
  for (const [input, want] of [
    ['crop the image send', { text: 'crop the image', send: true, phrase: 'send' }],
    ['crop the image, send it.', { text: 'crop the image', send: true, phrase: 'send it' }],
    ['Rotate left. Execute!', { text: 'Rotate left', send: true, phrase: 'execute' }],
    ['rotate left EXECUTE IT', { text: 'rotate left', send: true, phrase: 'execute it' }],
    ['send', { text: '', send: true, phrase: 'send' }],
    ['Send it.', { text: '', send: true, phrase: 'send it' }],
    ['resend', { text: 'resend', send: false, phrase: null }],
    ['send me the file', { text: 'send me the file', send: false, phrase: null }],
    ['  many   spaces  ', { text: 'many spaces', send: false, phrase: null }],
    ['', { text: '', send: false, phrase: null }],
    [null, { text: '', send: false, phrase: null }],
  ]) assert.deepStrictEqual(splitSendPhrase(input), want, String(input));
});

test('unsupported: voiceChat=true throws, startComposer is false, nothing starts or fires', () => {
  const { voice, engine, win, target } = make({ supported: false });
  assert.strictEqual(voice.supported, false);
  assert.throws(() => { voice.voiceChat = true; }, new RegExp(UNSUPPORTED_TEXT));
  assert.strictEqual(voice.startComposer(target()), false);
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(engine.calls, []);
  assert.deepStrictEqual(win.events, []);
});

test('voice chat on/off: one start in the settings language, a stop, state events each way', () => {
  const { voice, engine, win } = make({ settings: { language: 'uk-UA' } });
  voice.voiceChat = true;
  assert.strictEqual(voice.voiceChat, true);
  assert.strictEqual(voice.mode, 'chat');
  assert.strictEqual(voice.listening, true);
  assert.deepStrictEqual(engine.calls, [['start', 'uk-UA']]);
  voice.voiceChat = true;   // idempotent
  assert.strictEqual(engine.calls.length, 1);
  voice.voiceChat = false;
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(engine.calls.at(-1), ['stop']);
  assert.deepStrictEqual(win.events.filter((e) => typeof e === 'object').map((e) => [e.mode, e.listening]),
    [['chat', true], ['chat', true], ['off', false], ['off', false]]);
  voice.voiceChat = false;   // already off — silent
  assert.strictEqual(engine.calls.length, 2);
});

test('exclusivity: composer → chat hot-swaps (no stop) and tells the composer; chat → composer likewise', () => {
  const { voice, engine, target } = make();
  const t = target();
  assert.strictEqual(voice.startComposer(t), true);
  assert.strictEqual(voice.mode, 'composer');
  assert.strictEqual(voice.target, t);
  engine.hear('half a sentence');
  voice.voiceChat = true;
  assert.strictEqual(voice.mode, 'chat');
  assert.deepStrictEqual(t.stops, ['switch']);
  assert.deepStrictEqual(engine.calls.map((c) => c[0]), ['start', 'start'], 'a hot swap, never a stop');
  assert.ok(engine.committed >= 1, 'the half sentence is discarded');
  assert.deepStrictEqual(t.submits, []);
  const t2 = target();
  voice.startComposer(t2);
  assert.strictEqual(voice.voiceChat, false);
  assert.strictEqual(voice.target, t2);
  // Another composer taking over stops the first one's face.
  const t3 = target();
  voice.startComposer(t3);
  assert.deepStrictEqual(t2.stops, ['switch']);
  // stopComposer for a stale target is ignored; for the live one it leaves.
  voice.stopComposer(t2);
  assert.strictEqual(voice.mode, 'composer');
  voice.stopComposer(t3);
  assert.strictEqual(voice.mode, 'off');
  assert.deepStrictEqual(t3.stops, ['stop']);
  // toggleComposer flips.
  assert.strictEqual(voice.toggleComposer(t3), true);
  assert.strictEqual(voice.toggleComposer(t3), false);
  assert.strictEqual(voice.mode, 'off');
});
