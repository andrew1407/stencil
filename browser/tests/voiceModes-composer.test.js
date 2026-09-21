// Voice composer dictation: the sent-balloon echo, transcripts driving setText, the send
// phrases and the silence window that closes a turn. Split from voiceModes.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { VOICE_ACTIVE_LEVEL } from '../js/llm/voice/modes.js';
import { make } from './helpers/voiceModesRig.js';

// Hands-free, the only surface is the toast — and a dictated prompt is a paragraph, not
// a line. The "Sent" balloon echoes just enough of it to prove the mic heard right.
test('the sent balloon echoes a stub of the spoken prompt, never the whole of it', async () => {
  const { spokenEcho, SPOKEN_ECHO_CHARS, CHAT_TOAST_CHARS } = await import('../js/llm/chat/session.js');
  assert.ok(SPOKEN_ECHO_CHARS < CHAT_TOAST_CHARS, 'shorter than a reply balloon');
  assert.strictEqual(spokenEcho('crop it'), 'crop it', 'a short prompt is shown whole');
  const long = spokenEcho('crop ten percent off every edge and then rotate it right twice please');
  assert.strictEqual(long.length, SPOKEN_ECHO_CHARS);
  assert.ok(long.endsWith('…'));
  // One line: a composer's typed prefix can carry newlines into the dictated text.
  assert.strictEqual(spokenEcho('  make it \n  sepia  '), 'make it sepia');
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(new URL('../js/llm/voice/modes.js', import.meta.url), 'utf8');
  assert.ok(src.includes('`Sent: "${spokenEcho(text)}"`'),
    'the voice-chat toast quotes the stub — it is repeating what the mic heard');
});

test('composer: transcripts drive setText, a spoken phrase sends the stripped text and stops', () => {
  const { voice, engine, target } = make();
  const t = target();
  voice.startComposer(t);
  engine.hear('crop the');
  engine.hear('crop the image');
  assert.deepStrictEqual(t.texts, ['crop the', 'crop the image']);
  engine.hear('crop the image send it');
  assert.deepStrictEqual(t.texts.at(-1), 'crop the image');
  assert.deepStrictEqual(t.submits, ['phrase']);
  // One utterance, one message: the spoken send ENDS the dictation (the face stays a
  // paused mic — view.js). Hands-free chaining is voice CHAT mode's job.
  assert.strictEqual(voice.mode, 'off', 'the spoken send stops listening');
  assert.deepStrictEqual(t.stops, ['phrase'], 'and says why it stopped');
  // Once for the utterance itself, once as the teardown lets the engine go.
  assert.strictEqual(engine.committed, 2);
  // A bare "send" over an EMPTY box sends nothing — and leaves the mic alone, so a stray
  // "send" heard across a quiet room cannot close a dictation in progress.
  const t2 = target();
  voice.startComposer(t2);
  engine.hear('send');
  assert.deepStrictEqual(t2.submits, []);
  assert.strictEqual(t2.texts.at(-1), '');
  assert.strictEqual(voice.mode, 'composer', 'nothing said, nothing ended');
});

// A box that ALREADY holds something is exactly what "send it" is about — words typed before the mic
// went on, or an earlier utterance a pause left standing in the textarea (user report).
test('composer: a bare "send it" sends what is already in the box, without rewriting it', () => {
  const { voice, engine, target } = make();
  const t = target();
  t.hasText = () => true;            // the composer's own textarea is not empty
  voice.startComposer(t);
  engine.hear('send it');
  assert.deepStrictEqual(t.submits, ['phrase'], 'the standing text is sent');
  // The live transcript still streams in, but a bare send never writes WORDS over what stands there:
  // the composer restores whatever it was holding (view.js keeps that prefix).
  assert.ok(t.texts.every((x) => x === ''), 'the box is never given the send phrase');
  assert.strictEqual(voice.mode, 'off', 'one utterance, one message — the dictation ends');
  assert.deepStrictEqual(t.stops, ['phrase']);
});

test('silence: arms on results, is held open by recent loud audio only up to the cap, then sends', async () => {
  const m = make({ settings: { silenceMs: 1000 } });
  const { voice, engine, clock, sent } = m;
  voice.voiceChat = true;
  engine.hear('draw a line');
  assert.deepStrictEqual(clock.pendingDelays, [1000]);
  clock.advance(999);
  assert.deepStrictEqual(sent, []);
  clock.advance(1);
  assert.deepStrictEqual(sent, ['draw a line']);
  assert.strictEqual(voice.voiceChat, true, 'voice chat stays on');
  await m.settle();
  // Loud audio (above the threshold) mid-word extends the wait…
  engine.hear('and then');
  clock.advance(800);
  engine.level(VOICE_ACTIVE_LEVEL + 0.1);
  clock.advance(200);   // the original deadline: still loud 200 ms ago → extended
  assert.deepStrictEqual(sent, ['draw a line']);
  clock.advance(800);   // 1000 ms after the last loud frame
  assert.deepStrictEqual(sent, ['draw a line', 'and then']);
  await m.settle();
  // …but continuous noise cannot hold it past twice the setting.
  engine.hear('fan noise');
  for (let i = 0; i < 30; i++) { clock.advance(100); engine.level(0.9); }
  assert.deepStrictEqual(sent.at(-1), 'fan noise');
  assert.strictEqual(clock.pending, 0);
  await m.settle();
  // Quiet audio never counts as speech.
  engine.hear('quiet');
  clock.advance(900);
  engine.level(VOICE_ACTIVE_LEVEL - 0.05);
  clock.advance(100);
  assert.deepStrictEqual(sent.at(-1), 'quiet');
  await m.settle();
  // Stopping clears an armed timer — and sends the words the timer was waiting on.
  engine.hear('unfinished');
  assert.strictEqual(clock.pending, 1);
  voice.stopAll();
  assert.strictEqual(clock.pending, 0);
  assert.deepStrictEqual(sent.at(-1), 'unfinished');
  clock.advance(5000);
  assert.strictEqual(sent.length, 5, 'nothing fires after the stop');
});

test('turning voice chat off sends what was already said; a fatal stop and a composer stop do not', () => {
  const m = make();
  const { voice, engine, sent, target } = m;
  voice.voiceChat = true;
  engine.hear('rotate it a little');
  voice.voiceChat = false;
  assert.deepStrictEqual(sent, ['rotate it a little'], 'the pending words go out on the way off');
  assert.strictEqual(voice.mode, 'off');
  voice.voiceChat = true;
  engine.hear('send');                    // a bare phrase: nothing to send
  voice.voiceChat = false;
  assert.deepStrictEqual(sent, ['rotate it a little']);
  voice.voiceChat = true;
  engine.hear('half a');
  engine.fail('network');                 // a platform failure never fires a half-heard turn
  assert.deepStrictEqual(sent, ['rotate it a little']);
  const t = target();
  voice.startComposer(t);
  engine.hear('keep me in the box');
  voice.stopComposer(t);
  assert.deepStrictEqual(t.submits, [], 'a composer stop leaves the words in the textarea');
  assert.deepStrictEqual(t.texts.at(-1), 'keep me in the box');
});

