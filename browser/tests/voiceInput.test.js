import { test } from 'node:test';
import assert from 'node:assert';
import { createVoiceInput, isVoiceSupported } from '../js/llm/voice/voiceInput.js';
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

test('unsupported: supported=false, start() is false and nothing throws', () => {
  const { engine, log } = makeEngine({ unsupported: true });
  assert.strictEqual(engine.supported, false);
  assert.strictEqual(engine.start({ lang: 'en-US' }), false);
  assert.strictEqual(engine.state, 'idle');
  assert.deepStrictEqual(log.states, []);
  assert.strictEqual(isVoiceSupported({}), false);
  assert.strictEqual(isVoiceSupported({ webkitSpeechRecognition: class {} }), true);
  assert.strictEqual(isVoiceSupported({ SpeechRecognition: class {} }), true);
});

test('start configures a continuous, interim, single-alternative recognizer in the session language', () => {
  const { sr, engine, log, session, meter } = makeEngine();
  assert.strictEqual(engine.start(session('de-DE')), true);
  const r = sr.last;
  assert.strictEqual(r.continuous, true);
  assert.strictEqual(r.interimResults, true);
  assert.strictEqual(r.maxAlternatives, 1);
  assert.strictEqual(r.lang, 'de-DE');
  assert.deepStrictEqual(r.calls, ['start']);
  assert.strictEqual(engine.state, 'starting');
  r.fire.start();
  assert.strictEqual(engine.listening, true);
  assert.deepStrictEqual(log.states, ['starting', 'listening']);
  assert.strictEqual(meter.started, 1);
  // The meter's levels reach the session.
  meter.onLevel(0.5);
  assert.deepStrictEqual(log.levels, [0.5]);
});

test('transcripts: finals accumulate, interims trail, whitespace is normalized', () => {
  const { sr, engine, log, session } = makeEngine();
  engine.start(session());
  sr.last.fire.start();
  sr.last.fire.result([{ text: ' crop the ', isFinal: false }]);
  assert.deepStrictEqual(log.transcripts.at(-1), { final: '', interim: 'crop the', text: 'crop the' });
  sr.last.fire.result([{ text: ' crop the image', isFinal: true }, { text: ' and ', isFinal: false }]);
  assert.deepStrictEqual(log.transcripts.at(-1), { final: 'crop the image', interim: 'and', text: 'crop the image and' });
  assert.deepStrictEqual(engine.transcript, log.transcripts.at(-1));
});

test('commit on an interim also drops its later finalization at the same index', () => {
  const { sr, engine, log, session } = makeEngine();
  engine.start(session());
  sr.last.fire.start();
  sr.last.fire.result([{ text: 'send it', isFinal: false }]);
  engine.commit();
  assert.deepStrictEqual(engine.transcript, { final: '', interim: '', text: '' });
  sr.last.fire.result([{ text: 'send it', isFinal: true }]);
  assert.strictEqual(log.transcripts.at(-1).text, '', 'the finalized copy is consumed');
  sr.last.fire.result([{ text: 'send it', isFinal: true }, { text: 'next thing', isFinal: false }]);
  assert.strictEqual(log.transcripts.at(-1).text, 'next thing');
});

test('onend while active restarts with a new instance; uncommitted finals carry over, indices restart', () => {
  const { sr, clock, engine, log, session } = makeEngine();
  engine.start(session());
  sr.instances[0].fire.start();
  sr.instances[0].fire.result([{ text: 'draw a line', isFinal: true }]);
  sr.instances[0].fire.end();
  assert.strictEqual(engine.state, 'starting');
  assert.strictEqual(clock.pending, 1);
  clock.advance(0);
  assert.strictEqual(sr.instances.length, 2);
  assert.deepStrictEqual(sr.instances[1].calls, ['start']);
  sr.instances[1].fire.start();
  sr.instances[1].fire.result([{ text: 'across', isFinal: false }]);
  assert.deepStrictEqual(log.transcripts.at(-1), { final: 'draw a line', interim: 'across', text: 'draw a line across' });
  // A late event from the dead instance is ignored.
  sr.instances[0].fire.result([{ text: 'ghost', isFinal: true }]);
  assert.strictEqual(log.transcripts.at(-1).text, 'draw a line across');
  assert.deepStrictEqual(log.states, ['starting', 'listening', 'starting', 'listening']);
});

test('stop is graceful: the recognizer is stopped, its final onend settles to idle, no restart', () => {
  const { sr, clock, engine, log, session, meter } = makeEngine();
  engine.start(session());
  sr.last.fire.start();
  engine.stop();
  assert.strictEqual(engine.active, false);
  assert.strictEqual(engine.state, 'stopping');
  assert.deepStrictEqual(sr.last.calls, ['start', 'stop']);
  sr.last.fire.result([{ text: 'late final', isFinal: true }]);   // pending finals still arrive
  assert.strictEqual(log.transcripts.at(-1).text, 'late final');
  sr.last.fire.end();
  assert.strictEqual(engine.state, 'idle');
  assert.strictEqual(clock.pending, 0);
  assert.strictEqual(sr.instances.length, 1);
  assert.strictEqual(meter.stopped, 1);
  engine.stop();   // idempotent
  assert.strictEqual(log.states.at(-1), 'idle');
});
