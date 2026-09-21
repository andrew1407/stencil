import { test } from 'node:test';
import assert from 'node:assert';
import { createLevelMeter, rmsOfTimeDomain, levelFromRms, LEVEL_DECAY_MS, LEVEL_FFT_SIZE } from '../js/llm/voice/level.js';
import { createFakeAudio } from './helpers/speech.js';

const makeMeter = (audio, over = {}) => {
  let t = 0;
  const meter = createLevelMeter({
    getUserMedia: audio.getUserMedia,
    AudioContext: audio.AudioContext,
    requestAnimationFrame: audio.requestAnimationFrame,
    cancelAnimationFrame: audio.cancelAnimationFrame,
    now: () => t,
    ...over,
  });
  return { meter, setTime: (v) => { t = v; } };
};
const square = (amp) => new Uint8Array(LEVEL_FFT_SIZE).map((_, i) => (i % 2 ? 128 + amp : 128 - amp));

test('rms + level: silence is 0, a full-scale square wave is 1, clamped', () => {
  assert.strictEqual(rmsOfTimeDomain(new Uint8Array(0)), 0);
  assert.strictEqual(rmsOfTimeDomain(new Uint8Array(256).fill(128)), 0);
  assert.ok(Math.abs(rmsOfTimeDomain(square(128)) - 1) < 1e-9);
  assert.strictEqual(levelFromRms(0), 0);
  assert.strictEqual(levelFromRms(1), 1);
  assert.strictEqual(levelFromRms(0.05), 0.4);
  assert.strictEqual(levelFromRms(-1), 0);
});

test('start opens the mic, wires a 256-point analyser, resumes a suspended context and samples per frame', async () => {
  const audio = createFakeAudio({ suspended: true });
  const { meter } = makeMeter(audio);
  const levels = [];
  await meter.start((l) => levels.push(l));
  assert.strictEqual(meter.running, true);
  assert.strictEqual(audio.streams.length, 1);
  const ctx = audio.contexts[0];
  assert.strictEqual(ctx.resumed, 1);
  assert.strictEqual(ctx.analyser.fftSize, LEVEL_FFT_SIZE);
  assert.strictEqual(ctx.sources[0].connected, ctx.analyser);
  assert.strictEqual(audio.pendingFrames, 1);
  audio.bytes = square(64);
  audio.frame();
  assert.strictEqual(ctx.samples, 1);
  assert.strictEqual(levels.length, 1);
  assert.ok(Math.abs(levels[0] - 1) < 1e-9, 'half-scale square → rms 0.5 → ×8 clamps to 1');
  assert.strictEqual(audio.pendingFrames, 1, 'the loop re-requests a frame');
  // A second start is a no-op.
  await meter.start(() => {});
  assert.strictEqual(audio.streams.length, 1);
});

test('instant attack, exponential decay against the real dt; idle frames emit nothing', async () => {
  const audio = createFakeAudio();
  const { meter, setTime } = makeMeter(audio);
  const levels = [];
  await meter.start((l) => levels.push(l));
  setTime(0);
  audio.bytes = square(8);            // rms 1/16 → level 0.5
  audio.frame();
  assert.ok(Math.abs(levels.at(-1) - 0.5) < 1e-9);
  audio.bytes = square(16);           // louder: jumps straight to 1
  setTime(16);
  audio.frame();
  assert.ok(Math.abs(levels.at(-1) - 1) < 1e-9);
  audio.bytes = new Uint8Array(LEVEL_FFT_SIZE).fill(128);   // silence: decays
  setTime(16 + LEVEL_DECAY_MS);
  audio.frame();
  assert.ok(Math.abs(levels.at(-1) - Math.exp(-1)) < 1e-9, 'one decay constant later ≈ e⁻¹');
  setTime(16 + 2 * LEVEL_DECAY_MS);
  audio.frame();
  assert.ok(Math.abs(levels.at(-1) - Math.exp(-2)) < 1e-9);
  // Let it die out, then confirm fully idle frames stay silent.
  const n = levels.length;
  setTime(16 + 20 * LEVEL_DECAY_MS);
  audio.frame();                       // emits once more (prev still above the floor)
  audio.frame();
  audio.frame();
  assert.strictEqual(levels.length, n + 1);
});

test('stop releases tracks, closes the context, cancels the frame and emits a final 0', async () => {
  const audio = createFakeAudio();
  const { meter } = makeMeter(audio);
  const levels = [];
  await meter.start((l) => levels.push(l));
  audio.bytes = square(32);
  audio.frame();
  meter.stop();
  assert.strictEqual(meter.running, false);
  assert.strictEqual(audio.streams[0].tracks[0].stopped, true);
  assert.strictEqual(audio.contexts[0].closed, true);
  assert.strictEqual(audio.pendingFrames, 0);
  assert.strictEqual(levels.at(-1), 0);
  assert.strictEqual(meter.level, 0);
  meter.stop();   // idempotent
  assert.strictEqual(levels.filter((l) => l === 0).length, 1);
});

test('stop during the pending getUserMedia hands the late stream straight back', async () => {
  const audio = createFakeAudio();
  let release;
  const gated = new Promise((r) => { release = r; });
  const { meter } = makeMeter(audio, { getUserMedia: async () => { await gated; return audio.getUserMedia(); } });
  const p = meter.start(() => {});
  meter.stop();
  release();
  await p;
  assert.strictEqual(meter.running, false);
  assert.strictEqual(audio.streams.length, 1);
  assert.strictEqual(audio.streams[0].tracks[0].stopped, true, 'the mic light goes out');
  assert.strictEqual(audio.contexts.length, 0, 'no context was ever built');
});

test('a refused mic rejects start and leaves the meter idle; missing capabilities reject too', async () => {
  const audio = createFakeAudio({ reject: new Error('NotAllowedError') });
  const { meter } = makeMeter(audio);
  await assert.rejects(meter.start(() => {}), /NotAllowedError/);
  assert.strictEqual(meter.running, false);
  const bare = createLevelMeter({ getUserMedia: null, AudioContext: null, requestAnimationFrame: null });
  await assert.rejects(bare.start(() => {}), /unavailable/);
});
