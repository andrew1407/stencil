// The two motion switches (js/ui/motionPrefs.js): the canvas stroke animation, and the
// three interface modes — particles / slide / none. Everything that moves in the app asks
// one of the two gates below, so these are the tests that pin what each mode means.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom } from './helpers/dom.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');

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

const prefs = await import('../js/ui/motionPrefs.js');
const {
  MOTION_MODES, MOTION_STORAGE_KEY, MOTION_EVENT, DEFAULT_MOTION_MODE,
  motionPrefs, setMotionPrefs, reloadMotionPrefs, normalizeMotionMode,
  motionReduced, dustEnabled, drawMotionEnabled, motionMode, drawingAnimations,
} = prefs;

test('the defaults are "everything moves, made of particles"', () => {
  assert.deepEqual(MOTION_MODES, ['particles', 'slide', 'none']);
  assert.equal(DEFAULT_MOTION_MODE, 'particles');
  assert.deepEqual(motionPrefs(), { mode: 'particles', drawing: true });
});

test('an unknown, missing or junk mode reads as the default — never as "off"', () => {
  assert.equal(normalizeMotionMode('slide'), 'slide');
  assert.equal(normalizeMotionMode(' NONE '), 'none');
  assert.equal(normalizeMotionMode('sparkles'), 'particles');
  assert.equal(normalizeMotionMode(undefined), 'particles');
  store.set(MOTION_STORAGE_KEY, '{ not json');
  assert.deepEqual(reloadMotionPrefs(), { mode: 'particles', drawing: true });
  store.set(MOTION_STORAGE_KEY, JSON.stringify({ mode: 'nope', drawing: 0 }));
  assert.deepEqual(reloadMotionPrefs(), { mode: 'particles', drawing: false });
  store.delete(MOTION_STORAGE_KEY);
  reloadMotionPrefs();
});

test('a change persists, restamps <html data-motion> and announces itself', () => {
  events.length = 0;
  setMotionPrefs({ mode: 'slide' });
  assert.equal(motionMode(), 'slide');
  assert.deepEqual(JSON.parse(store.get(MOTION_STORAGE_KEY)), { mode: 'slide', drawing: true });
  assert.equal(doc.documentElement.attrs.get('data-motion'), 'slide');
  assert.equal(events.at(-1).type, MOTION_EVENT);
  assert.deepEqual(events.at(-1).detail, { mode: 'slide', drawing: true });
  // A patch touches only what it names.
  setMotionPrefs({ drawing: false });
  assert.deepEqual(motionPrefs(), { mode: 'slide', drawing: false });
  // …and a fresh load reads back exactly what was written.
  assert.deepEqual(reloadMotionPrefs(), { mode: 'slide', drawing: false });
});

// The truth table every helper in the app leans on.
test('particles / slide / none each answer the two gates differently', () => {
  setMotionPrefs({ mode: 'particles', drawing: true });
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
  setMotionPrefs({ mode: 'particles', drawing: false });
  assert.equal(drawingAnimations(), false);
  assert.equal(drawMotionEnabled(), false, 'the canvas is still');
  assert.equal(dustEnabled(), true, 'while the windows still form out of dust');
});

// The OS preference is never overridden by ours: reduce means reduce, whatever is stored.
test('prefers-reduced-motion wins over any stored mode', () => {
  setMotionPrefs({ mode: 'particles', drawing: true });
  reduced = true;
  assert.equal(motionReduced(), true);
  assert.equal(dustEnabled(), false);
  assert.equal(drawMotionEnabled(), false);
  reduced = false;
  assert.equal(motionReduced(), false);
});

// ── The wiring: who asks which gate ─────────────────────────────────────────
test('every cloud in the app is built behind the dust gate, and the strokes behind the drawing one', () => {
  const motion = read('../js/ui/motion.js');
  // disintegrate() is the one door every element-sized cloud goes through.
  const body = motion.slice(motion.indexOf('export function disintegrate('),
                            motion.indexOf('export const reintegrate'));
  assert.ok(body.includes('if (!dustEnabled()) return false;'), 'the cloud is built behind the gate');
  // …and the three clouds that are NOT built there: the theme wipe's grain and the
  // canvas ghost in both directions.
  assert.match(motion, /typeof requestAnimationFrame !== 'function' \|\| !dustEnabled\(\)/);
  assert.equal(motion.match(/if \(!dustEnabled\(\)\) return false;/g).length, 3, 'disintegrate + ghostIn + ghostOut');
  // The voice mic's motes and ray ring are particles too.
  assert.match(read('../js/ui/voiceDust.js'), /isOn\(\) && dustEnabled\(\)/);
  // The canvas stroke flight answers the drawing switch instead.
  assert.match(read('../js/core/strokeFx.js'), /if \(!this\.#schedule \|\| !drawMotionEnabled\(\)\) return null;/);
  // One gate, asked in one place: no component still reads the media query by hand.
  for (const f of ['../js/ui/base.js', '../js/ui/toolbar.js', '../js/ui/motion.js'])
    assert.ok(!read(f).includes("matchMedia('(prefers-reduced-motion: reduce)')"), f);
});

test('the mode reaches the CSS before first paint, and stops what CSS alone drives', () => {
  const prePaint = read('../js/prePaintTheme.js');
  assert.ok(prePaint.includes("localStorage.getItem('drawingApp_motion')"), 'same key as motionPrefs.js');
  assert.match(prePaint, /root\.setAttribute\('data-motion',/);
  const css = read('../css/animations.css');
  assert.match(css, /:root\[data-motion="none"\] \*,/);
  assert.match(css, /animation-duration: 0\.01ms !important;/);
  // 'slide' needs no rules of its own — each surface keeps its own entrance — except the
  // chat entry, whose only entrance ever WAS its dust.
  assert.match(css, /\.chat-slide-in \{ animation: chatRiseIn/);
});

test('both switches are in the Visuals modal and on the console facade', () => {
  const modal = read('../js/ui/visualsModal.js');
  assert.match(modal, /<div class="vs-section">Motion<\/div>/);
  assert.match(modal, /id="vs-draw-anim"/);
  assert.match(modal, /id="vs-motion-mode"/);
  assert.match(modal, /app\.settings\.setMotion\('drawing', drawAnim\.checked\)/);
  assert.match(modal, /app\.settings\.setMotion\('mode', motionMode\.value\)/);
  // Reset All restores them along with the colours.
  assert.match(modal, /setMotion\('mode', DEFAULT_MOTION_MODE\)/);
  const api = read('../js/console/stencilApi.js');
  assert.match(api, /get drawingAnimations\(\) \{ return motionPrefs\(\)\.drawing; \}/);
  assert.match(api, /set motionMode\(v\) \{ app\.settings\.setMotion\('mode', v\); \}/);
  // Both surfaces come through the ONE setter, which is also what rejects a bad mode.
  const controller = read('../js/core/settingsController.js');
  assert.match(controller, /if \(!MOTION_MODES\.includes\(m\)\)\s*\n?\s*throw new Error\(`Unknown motion mode/);
  assert.match(controller, /setVal\('vs-motion-mode', m\)/);
});
