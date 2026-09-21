// The crop rect's flight between two shapes (js/ui/motion/rectTween.js) — the Album/Portrait
// press on both crop surfaces. Node has no rAF, so the clock is driven by hand: every frame
// the tween hands out is recorded, which is the only thing its callers see of it.
import test from 'node:test';
import assert from 'node:assert';

import { tweenRect, easeOutCubic, RECT_TWEEN_MS } from '../js/ui/motion/rectTween.js';
import { setMotionPrefs, reloadMotionPrefs } from '../js/ui/motion/motionPrefs.js';

// A hand-cranked rAF: `step(ms)` runs whatever the last frame asked for, at that timestamp.
const installRaf = () => {
  const queued = new Map();
  let id = 0, now = 0;
  globalThis.requestAnimationFrame = (fn) => { queued.set(++id, fn); return id; };
  globalThis.cancelAnimationFrame = (n) => queued.delete(n);
  const saved = globalThis.performance;
  globalThis.performance = { now: () => now };
  return {
    step(ms) {
      now += ms;
      const due = [...queued.entries()];
      queued.clear();
      for (const [, fn] of due) fn(now);
    },
    get pending() { return queued.size; },
    restore() {
      delete globalThis.requestAnimationFrame;
      delete globalThis.cancelAnimationFrame;
      globalThis.performance = saved;
    },
  };
};

const RECT = (x, y, width, height) => ({ x, y, width, height });
const FROM = RECT(0, 0, 100, 200);
const TO = RECT(50, 20, 200, 100);

test('easeOutCubic spans 0→1 and is the OutCubic the desktop plays', () => {
  assert.strictEqual(easeOutCubic(0), 0);
  assert.strictEqual(easeOutCubic(1), 1);
  assert.ok(Math.abs(easeOutCubic(0.5) - 0.875) < 1e-12);   // 1 - 0.5^3
  // Front-loaded: half the time is already most of the way there.
  assert.ok(easeOutCubic(0.5) > 0.5);
});

test('tweenRect hands out `from` first and exactly `to` last', () => {
  const raf = installRaf();
  try {
    const seen = [];
    tweenRect(FROM, TO, (r) => seen.push({ ...r }));
    assert.deepStrictEqual(seen[0], FROM, 'the first frame is the shape it starts from');
    raf.step(RECT_TWEEN_MS / 2);
    assert.strictEqual(seen.length, 2);
    for (const k of ['x', 'y', 'width', 'height']) {
      const mid = seen[1][k], a = FROM[k], b = TO[k];
      assert.ok(mid > Math.min(a, b) && mid < Math.max(a, b), `${k} is between the two`);
    }
    raf.step(RECT_TWEEN_MS);
    assert.deepStrictEqual(seen.at(-1), TO, 'it lands ON the target, never near it');
    assert.strictEqual(raf.pending, 0, 'and asks for no frame after that');
  } finally { raf.restore(); }
});

test('tweenRect interpolates every side on the eased clock', () => {
  const raf = installRaf();
  try {
    const seen = [];
    tweenRect(FROM, TO, (r) => seen.push({ ...r }));
    raf.step(RECT_TWEEN_MS / 4);
    const k = easeOutCubic(0.25);
    const want = { x: 50 * k, y: 20 * k, width: 100 + 100 * k, height: 200 - 100 * k };
    for (const side of ['x', 'y', 'width', 'height'])
      assert.ok(Math.abs(seen[1][side] - want[side]) < 1e-9, `${side}: ${seen[1][side]} ≈ ${want[side]}`);
  } finally { raf.restore(); }
});

test('the cancel stops the flight: no frame follows it', () => {
  const raf = installRaf();
  try {
    const seen = [];
    const cancel = tweenRect(FROM, TO, (r) => seen.push({ ...r }));
    raf.step(RECT_TWEEN_MS / 4);
    const after = seen.length;
    cancel();
    raf.step(RECT_TWEEN_MS / 4);
    assert.strictEqual(seen.length, after, 'a cancelled tween hands out nothing more');
    assert.strictEqual(raf.pending, 0);
  } finally { raf.restore(); }
});

test('no `from` lands on `to` at once — a first fit has no old shape to fly from', () => {
  const raf = installRaf();
  try {
    const seen = [];
    const cancel = tweenRect(null, TO, (r) => seen.push({ ...r }));
    assert.deepStrictEqual(seen, [TO]);
    assert.strictEqual(raf.pending, 0);
    cancel();   // still safe to call
  } finally { raf.restore(); }
});

test('reduced motion lands on `to` at once, and no rAF at all does too', () => {
  const raf = installRaf();
  try {
    setMotionPrefs({ mode: 'none' });
    const seen = [];
    tweenRect(FROM, TO, (r) => seen.push({ ...r }));
    assert.deepStrictEqual(seen, [TO], 'nothing may move when motion is off');
    assert.strictEqual(raf.pending, 0);
  } finally {
    setMotionPrefs({ mode: 'particles' });
    reloadMotionPrefs();
    raf.restore();
  }

  // Node itself: no requestAnimationFrame in scope.
  const seen = [];
  tweenRect(FROM, TO, (r) => seen.push({ ...r }));
  assert.deepStrictEqual(seen, [TO]);
});
