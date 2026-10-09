// The Visuals "Animation speed" (js/ui/motion/motionPrefs.js, surface/cssSpeed.js): the default plays
// every flight exactly as tuned, any other speed stretches the JS timers and the CSS rates alike.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';
import MOTION from '../../../../common/config/motion.json' with { type: 'json' };

const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};
const listeners = new Map();
installDom({}, {
  matchMedia: () => ({ matches: false }),
  window: {
    dispatchEvent: (e) => { for (const fn of listeners.get(e.type) || []) fn(e); },
    addEventListener: (t, fn) => listeners.set(t, [...(listeners.get(t) || []), fn]),
    removeEventListener: (t, fn) => listeners.set(t, (listeners.get(t) || []).filter((f) => f !== fn)),
  },
  CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init?.detail; } },
});

const {
  MOTION_SPEED, DEFAULT_MOTION_SPEED, clampMotionSpeed, motionSpeed, motionScale, motionMs,
  setMotionPrefs, reloadMotionPrefs, MOTION_STORAGE_KEY,
} = await import('../../../js/ui/motion/motionPrefs.js');
const { rateCssAnimations, installCssSpeed } = await import('../../../js/ui/motion/surface/cssSpeed.js');

test('the range and the default are the shared table, and the default is the tuned pace', () => {
  assert.deepEqual({ ...MOTION_SPEED }, MOTION.speed);
  assert.deepEqual([MOTION_SPEED.min, MOTION_SPEED.max, DEFAULT_MOTION_SPEED], [0.1, 1, 0.37]);
  reloadMotionPrefs();
  assert.equal(motionSpeed(), 0.37);
  assert.equal(motionScale(), 1);
  for (const ms of [0, 1, 22, 220, 470, 1650, 123.5]) assert.equal(motionMs(ms), ms, `${ms} untouched`);
});

test('a speed outside the range is clamped, and junk reads as the default', () => {
  assert.equal(clampMotionSpeed(0), 0.1);
  assert.equal(clampMotionSpeed(-3), 0.1);
  assert.equal(clampMotionSpeed(7), 1);
  assert.equal(clampMotionSpeed('0.5'), 0.5);
  assert.equal(clampMotionSpeed(0.456), 0.46);
  for (const junk of [NaN, Infinity, null, undefined, '', 'fast', {}]) assert.equal(clampMotionSpeed(junk), 0.37);
});

test('faster and slower stretch every duration by default / speed, and it persists', () => {
  setMotionPrefs({ speed: 1 });
  assert.equal(motionMs(1000), 370);
  setMotionPrefs({ speed: 0.1 });
  assert.equal(motionMs(1000), 3700);
  assert.equal(JSON.parse(store.get(MOTION_STORAGE_KEY)).speed, 0.1);
  store.set(MOTION_STORAGE_KEY, JSON.stringify({ speed: 9 }));
  assert.equal(reloadMotionPrefs().speed, 1, 'a hand-edited store is clamped on read');
  setMotionPrefs({ speed: DEFAULT_MOTION_SPEED });
});

const fakeAnimation = (kind, iterations = 1) => ({
  [kind]: 'x', playbackRate: 1, effect: { getTiming: () => ({ iterations }) },
});

test('CSS animations are rated once at birth; loops, view transitions and WAAPI flights are left alone', () => {
  const css = fakeAnimation('transitionProperty');
  const keyframes = fakeAnimation('animationName');
  const loop = fakeAnimation('animationName', Infinity);
  const wipe = { ...fakeAnimation('animationName'), effect: { pseudoElement: '::view-transition-new(root)', getTiming: () => ({ iterations: 1 }) } };
  const scripted = { playbackRate: 1, effect: { getTiming: () => ({ iterations: 1 }) } };
  const doc = { getAnimations: () => [css, keyframes, loop, wipe, scripted] };
  const seen = new WeakSet();
  rateCssAnimations(doc, 2.5, seen);
  assert.deepEqual([css, keyframes, loop, wipe, scripted].map((a) => a.playbackRate), [2.5, 2.5, 1, 1, 1]);
  css.playbackRate = 1;
  rateCssAnimations(doc, 2.5, seen);
  assert.equal(css.playbackRate, 1, 'never re-rated');
});

test('the hook listens only while the speed is off the default', () => {
  const added = [];
  const doc = {
    documentElement: {},
    getAnimations: () => [],
    addEventListener: (t) => added.push(t),
    removeEventListener: (t) => added.splice(added.indexOf(t), 1),
  };
  const off = installCssSpeed(doc);
  assert.deepEqual(added, [], 'nothing at the default');
  setMotionPrefs({ speed: 0.8 });
  assert.deepEqual(added.sort(), ['animationstart', 'transitionrun']);
  setMotionPrefs({ speed: DEFAULT_MOTION_SPEED });
  assert.deepEqual(added, [], 'back to the default, the listeners go');
  off();
});

test('a burst of mutations and events costs one getAnimations pass per frame, and still rates', () => {
  const frames = [];
  const observers = [];
  const saved = [globalThis.requestAnimationFrame, globalThis.cancelAnimationFrame, globalThis.MutationObserver];
  globalThis.requestAnimationFrame = (fn) => frames.push(fn);
  globalThis.cancelAnimationFrame = (id) => { frames[id - 1] = null; };
  globalThis.MutationObserver = class {
    constructor(fn) { this.fn = fn; observers.push(this); }
    observe(_, opts) { this.opts = opts; }
    disconnect() {}
  };
  const handlers = {};
  const born = fakeAnimation('animationName');
  let passes = 0;
  const doc = {
    documentElement: {},
    getAnimations: () => { passes++; return [born]; },
    addEventListener: (t, fn) => { handlers[t] = fn; },
    removeEventListener: (t) => { delete handlers[t]; },
  };
  try {
    const off = installCssSpeed(doc);
    setMotionPrefs({ speed: 0.74 });
    const watch = observers.at(-1);
    assert.deepEqual(watch.opts.attributeFilter, ['class', 'open', 'hidden'], 'an inline style write never wakes it');
    for (let i = 0; i < 50; i++) watch.fn([]);
    handlers.transitionrun();
    handlers.animationstart();
    assert.equal(passes, 0, 'nothing runs before the frame');
    assert.equal(frames.length, 1, 'one frame queued for the whole burst');
    frames.shift()();
    assert.equal(passes, 1);
    assert.equal(born.playbackRate, 2);
    watch.fn([]);
    assert.equal(frames.length, 1, 'the next mutation queues the next frame');
    setMotionPrefs({ speed: DEFAULT_MOTION_SPEED });
    assert.equal(frames[0], null, 'back to the default, the queued pass is cancelled');
    off();
  } finally {
    [globalThis.requestAnimationFrame, globalThis.cancelAnimationFrame, globalThis.MutationObserver] = saved;
  }
});
