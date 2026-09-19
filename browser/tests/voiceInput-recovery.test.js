// js/llm/voiceInput.js recovery: no-speech and aborted restart at once, network backs off and
// turns fatal, each fatal code reports once, and a hot language swap. From voiceInput.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { createVoiceInput, voiceErrorText, FATAL_ERRORS, NETWORK_BACKOFF_MS } from '../js/llm/voiceInput.js';
import { createFakeSpeechRecognition, stubClock } from './helpers/speech.js';

// A level meter double: records start/stop, lets the test push a level.
const fakeMeter = () => {
  const m = { started: 0, stopped: 0, onLevel: null, fail: false };
  m.create = () => ({
    async start(cb) { m.started++; if (m.fail) throw new Error('mic refused'); m.onLevel = cb; },
    stop() { m.stopped++; m.onLevel = null; },
  });
  return m;
};

const makeEngine = (over = {}) => {
  const sr = createFakeSpeechRecognition(over.recognition || {});
  const clock = stubClock();
  const meter = fakeMeter();
  const engine = createVoiceInput({
    SpeechRecognition: over.unsupported ? undefined : sr.ctor,
    createLevelMeter: meter.create,
    setTimer: clock.setTimer,
    clearTimer: clock.clearTimer,
  });
  const log = { transcripts: [], states: [], errors: [], levels: [] };
  const session = (lang = 'en-US') => ({
    lang,
    onTranscript: (t) => log.transcripts.push(t),
    onState: (s, extra) => log.states.push(extra ? [s, extra] : s),
    onError: (e) => log.errors.push(e),
    onLevel: (l) => log.levels.push(l),
  });
  return { sr, clock, meter, engine, log, session };
};

test('no-speech and aborted restart immediately; network backs off and turns fatal on the third drop', () => {
  const { sr, clock, engine, log, session } = makeEngine();
  engine.start(session());
  sr.last.fire.start();
  sr.last.fire.error('no-speech');
  sr.last.fire.end();
  assert.deepStrictEqual(clock.pendingDelays, [0]);
  clock.advance(0);
  sr.last.fire.start();
  sr.last.fire.error('aborted');
  sr.last.fire.end();
  assert.deepStrictEqual(clock.pendingDelays, [0]);
  clock.advance(0);
  // network ×2 → backoffs; a result in between resets the counter
  sr.last.fire.start();
  sr.last.fire.error('network');
  sr.last.fire.end();
  assert.deepStrictEqual(clock.pendingDelays, [NETWORK_BACKOFF_MS[0]]);
  clock.advance(NETWORK_BACKOFF_MS[0]);
  sr.last.fire.start();
  sr.last.fire.error('network');
  sr.last.fire.end();
  assert.deepStrictEqual(clock.pendingDelays, [NETWORK_BACKOFF_MS[1]]);
  clock.advance(NETWORK_BACKOFF_MS[1]);
  sr.last.fire.start();
  sr.last.fire.result([{ text: 'ok', isFinal: false }]);   // reset
  sr.last.fire.error('network');
  sr.last.fire.end();
  assert.deepStrictEqual(clock.pendingDelays, [NETWORK_BACKOFF_MS[0]], 'a result between drops resets the backoff');
  clock.advance(NETWORK_BACKOFF_MS[0]);
  sr.last.fire.start();
  sr.last.fire.error('network');
  sr.last.fire.end();
  clock.advance(NETWORK_BACKOFF_MS[1]);
  sr.last.fire.start();
  sr.last.fire.error('network');   // third in a row → fatal, before onend
  assert.strictEqual(engine.active, false);
  assert.strictEqual(engine.state, 'idle');
  assert.deepStrictEqual(log.errors, [{ code: 'network', fatal: true, text: voiceErrorText('network') }]);
  assert.deepStrictEqual(log.states.at(-1), ['idle', { error: 'network' }]);
  assert.strictEqual(sr.last.calls.at(-1), 'abort');
  sr.last.fire.end();   // ignored
  assert.strictEqual(clock.pending, 0);
});

test('each fatal code aborts, ends the intent and reports once', () => {
  for (const code of ['not-allowed', 'service-not-allowed', 'audio-capture', 'language-not-supported', 'bad-grammar']) {
    assert.ok(FATAL_ERRORS.has(code));
    const { sr, engine, log, session, meter } = makeEngine();
    engine.start(session());
    sr.last.fire.start();
    sr.last.fire.result([{ text: 'partial', isFinal: true }]);
    sr.last.fire.error(code);
    assert.strictEqual(engine.active, false, code);
    assert.strictEqual(engine.state, 'idle', code);
    assert.strictEqual(sr.last.calls.at(-1), 'abort', code);
    assert.strictEqual(log.errors.length, 1, code);
    assert.strictEqual(log.errors[0].code, code);
    assert.strictEqual(log.errors[0].fatal, true);
    assert.strictEqual(meter.stopped, 1, code);
    assert.deepStrictEqual(engine.transcript, { final: '', interim: '', text: '' }, 'the transcript is dropped with it');
  }
  assert.match(voiceErrorText('not-allowed'), /Microphone access was denied/);
  assert.match(voiceErrorText('weird'), /weird/);
});

test('start() throwing once recovers with a fresh instance; twice in a row is fatal', () => {
  const once = makeEngine({ recognition: { throwOnStart: 1 } });
  once.engine.start(once.session());
  assert.strictEqual(once.sr.instances[0].calls.at(-1), 'abort');
  assert.strictEqual(once.clock.pending, 1);
  once.clock.advance(0);
  assert.strictEqual(once.sr.instances.length, 2);
  once.sr.last.fire.start();
  assert.strictEqual(once.engine.listening, true);
  assert.deepStrictEqual(once.log.errors, []);

  const twice = makeEngine({ recognition: { throwOnStart: 2 } });
  twice.engine.start(twice.session());
  twice.clock.advance(0);
  assert.strictEqual(twice.engine.active, false);
  assert.deepStrictEqual(twice.log.errors.map((e) => e.code), ['start-failed']);
});

test('hot swap: same language keeps the instance and swaps callbacks; a new language recreates it', () => {
  const { sr, engine, log, session, meter } = makeEngine();
  engine.start(session());
  sr.last.fire.start();
  sr.last.fire.result([{ text: 'first words', isFinal: false }]);
  const other = { transcripts: [], states: [] };
  engine.start({ lang: 'en-US', onTranscript: (t) => other.transcripts.push(t), onState: (s) => other.states.push(s) });
  assert.strictEqual(sr.instances.length, 1, 'no new recognizer');
  assert.deepStrictEqual(other.states, ['listening'], 'the new session learns the current state');
  assert.deepStrictEqual(engine.transcript, { final: '', interim: '', text: '' }, 'the swap commits what was heard');
  sr.last.fire.result([{ text: 'first words', isFinal: true }, { text: 'more', isFinal: false }]);
  assert.strictEqual(other.transcripts.at(-1).text, 'more');
  assert.strictEqual(log.transcripts.length, 1, 'the old session hears nothing further');
  engine.start({ lang: 'fr-FR', onTranscript: (t) => other.transcripts.push(t), onState: (s) => other.states.push(s) });
  assert.strictEqual(sr.instances.length, 2);
  assert.strictEqual(sr.instances[0].calls.at(-1), 'abort');
  assert.strictEqual(sr.instances[1].lang, 'fr-FR');
  assert.strictEqual(meter.started, 1, 'the mic stays open across the swap');
  sr.instances[0].fire.end();   // the dead instance's end is ignored — no restart
  assert.strictEqual(sr.instances.length, 2);
});

test('a refused level meter does not stop recognition', () => {
  const { sr, engine, meter, session, log } = makeEngine();
  meter.fail = true;
  engine.start(session());
  sr.last.fire.start();
  assert.strictEqual(engine.listening, true);
  assert.deepStrictEqual(log.errors, []);
});
