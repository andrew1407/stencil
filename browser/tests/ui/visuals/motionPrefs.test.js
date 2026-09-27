// The two motion switches (js/ui/motion/motionPrefs.js): the canvas stroke animation, and the
// five interface modes — particles (dust) / water / fire / slide / none. Everything that
// moves asks one of the two gates below, so these pin what each mode means; who asks which
// gate, and where the switches live, is motionPrefs-wiring.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';

// A localStorage stand-in, installed BEFORE the module reads it at import time.
const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};
let reduced = false;   // what the OS preference answers
const events = [];
const doc = installDom({}, {
  matchMedia: (q) => ({ matches: q.includes('reduced-motion') ? reduced : false }),
  window: { dispatchEvent: (e) => events.push(e), addEventListener: () => {} },
  CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init?.detail; } },
});

const prefs = await import('../../../js/ui/motion/motionPrefs.js');
const {
  MOTION_MODES, MOTION_STORAGE_KEY, MOTION_EVENT, DEFAULT_MOTION_MODE,
  motionPrefs, setMotionPrefs, reloadMotionPrefs, normalizeMotionMode,
  motionReduced, dustEnabled, drawMotionEnabled, motionMode, drawingAnimations,
  PARTICLE_MODES, particleStyle, MOTION_MODE_LABELS,
} = prefs;

test('the defaults are "everything moves, made of particles"', () => {
  assert.deepEqual(MOTION_MODES, ['particles', 'water', 'fire', 'slide', 'none']);
  assert.deepEqual(PARTICLE_MODES, ['particles', 'water', 'fire']);
  assert.equal(DEFAULT_MOTION_MODE, 'particles');
  assert.deepEqual(motionPrefs(), { mode: 'particles', drawing: true, backdrop: true });
  // The dropdown offers every mode, in this order, and nothing else.
  assert.deepEqual(MOTION_MODE_LABELS.map(([k]) => k), MOTION_MODES);
});

// Water and fire are particles too: the same gate, a different style on the grains.
test('water and fire fly particles like dust, each wearing its own style', () => {
  setMotionPrefs({ mode: 'particles', drawing: true, backdrop: true });
  assert.equal(particleStyle(), 'dust');
  setMotionPrefs({ mode: 'water' });
  assert.equal(dustEnabled(), true, 'water: the clouds still fly');
  assert.equal(motionReduced(), false);
  assert.equal(particleStyle(), 'water');
  assert.equal(doc.documentElement.attrs.get('data-motion'), 'water');
  setMotionPrefs({ mode: 'fire' });
  assert.equal(dustEnabled(), true);
  assert.equal(particleStyle(), 'fire');
  assert.deepEqual(reloadMotionPrefs(), { mode: 'fire', drawing: true, backdrop: true }, 'persists like any mode');
  setMotionPrefs({ mode: 'slide' });
  assert.equal(particleStyle(), null, 'no particles, no style');
  setMotionPrefs({ mode: 'particles' });
  reduced = true;
  assert.equal(particleStyle(), null, 'the OS preference silences the style with the cloud');
  reduced = false;
});

test('an unknown, missing or junk mode reads as the default — never as "off"', () => {
  assert.equal(normalizeMotionMode('slide'), 'slide');
  assert.equal(normalizeMotionMode(' NONE '), 'none');
  assert.equal(normalizeMotionMode('sparkles'), 'particles');
  assert.equal(normalizeMotionMode(undefined), 'particles');
  store.set(MOTION_STORAGE_KEY, '{ not json');
  assert.deepEqual(reloadMotionPrefs(), { mode: 'particles', drawing: true, backdrop: true });
  store.set(MOTION_STORAGE_KEY, JSON.stringify({ mode: 'nope', drawing: 0 }));
  assert.deepEqual(reloadMotionPrefs(), { mode: 'particles', drawing: false, backdrop: true });
  store.delete(MOTION_STORAGE_KEY);
  reloadMotionPrefs();
});

test('a change persists, restamps <html data-motion> and announces itself', () => {
  events.length = 0;
  setMotionPrefs({ mode: 'slide' });
  assert.equal(motionMode(), 'slide');
  assert.deepEqual(JSON.parse(store.get(MOTION_STORAGE_KEY)), { mode: 'slide', drawing: true, backdrop: true });
  assert.equal(doc.documentElement.attrs.get('data-motion'), 'slide');
  assert.equal(events.at(-1).type, MOTION_EVENT);
  assert.deepEqual(events.at(-1).detail, { mode: 'slide', drawing: true, backdrop: true });
  // A patch touches only what it names.
  setMotionPrefs({ drawing: false });
  assert.deepEqual(motionPrefs(), { mode: 'slide', drawing: false, backdrop: true });
  // …and a fresh load reads back exactly what was written.
  assert.deepEqual(reloadMotionPrefs(), { mode: 'slide', drawing: false, backdrop: true });
});

// The truth table every helper in the app leans on.
test('particles / slide / none each answer the two gates differently', () => {
  setMotionPrefs({ mode: 'particles', drawing: true, backdrop: true });
  assert.equal(motionReduced(), false);
  assert.equal(dustEnabled(), true, 'particles: dust flies');
  assert.equal(drawMotionEnabled(), true);

  setMotionPrefs({ mode: 'slide' });
  assert.equal(motionReduced(), false, 'slide still moves — each surface plays its own entrance');
  assert.equal(dustEnabled(), false, '…just never out of dust');
  assert.equal(drawMotionEnabled(), true);

  setMotionPrefs({ mode: 'none' });
  assert.equal(motionReduced(), true);
  assert.equal(dustEnabled(), false);
  assert.equal(drawMotionEnabled(), false, 'nothing moves means the stroke does not either');
});

test('the drawing switch is independent of the interface mode', () => {
  setMotionPrefs({ mode: 'particles', drawing: false, backdrop: true });
  assert.equal(drawingAnimations(), false);
  assert.equal(drawMotionEnabled(), false, 'the canvas is still');
  assert.equal(dustEnabled(), true, 'while the windows still form out of dust');
});

// The OS preference is never overridden by ours: reduce means reduce, whatever is stored.
test('prefers-reduced-motion wins over any stored mode', () => {
  setMotionPrefs({ mode: 'particles', drawing: true, backdrop: true });
  reduced = true;
  assert.equal(motionReduced(), true);
  assert.equal(dustEnabled(), false);
  assert.equal(drawMotionEnabled(), false);
  reduced = false;
  assert.equal(motionReduced(), false);
});

// A session override (the webcore skin) sits over the stored prefs: every reader sees it, the
// store never does, and the user's next choice lifts it whole.
test('a session override is read everywhere, written nowhere, and lifted by the next choice', () => {
  const { setMotionOverride, motionOverridden, modalBackdrop } = prefs;
  setMotionPrefs({ mode: 'water', drawing: true, backdrop: true });
  const stored = store.get(MOTION_STORAGE_KEY);
  setMotionOverride({ mode: 'none', drawing: false, backdrop: false });
  assert.equal(motionOverridden(), true);
  assert.equal(motionReduced(), true);
  assert.equal(drawMotionEnabled(), false);
  assert.equal(modalBackdrop(), false);
  assert.equal(doc.documentElement.attrs.get('data-motion'), 'none');
  assert.equal(store.get(MOTION_STORAGE_KEY), stored, 'the store still says water');
  setMotionOverride(null);
  assert.equal(motionMode(), 'water');
  assert.equal(motionOverridden(), false);
  setMotionOverride({ mode: 'none' });
  setMotionPrefs({ drawing: false });
  assert.equal(motionMode(), 'water', 'a choice of any switch lifts the whole override');
  assert.deepEqual(JSON.parse(store.get(MOTION_STORAGE_KEY)), { mode: 'water', drawing: false, backdrop: true });
  setMotionPrefs({ mode: 'particles', drawing: true, backdrop: true });
});
