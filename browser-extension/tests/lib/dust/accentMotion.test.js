// StencilMotion inside src/lib/accent.js — the browser js/ui/prefs.js twin: the five modes,
// the gates they open, and the wake each style paints from the accent palette.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { loadAccent } from '../../helpers/accentSandbox.js';

// ── Interface motion (StencilMotion — browser js/ui/prefs.js twin) ──

test('the motion mode defaults to particles, is stamped before first paint, and mirrors the browser list', async () => {
  const page = loadAccent();
  assert.equal(page.motion.get(), 'particles');
  assert.equal(page.dataMotion(), 'particles', 'data-motion is on <html> at load');
  assert.equal(page.motion.storageKey, 'stencil_motion');
  const browser = await import('../../../../browser/js/ui/motion/motionPrefs.js');
  // Through JSON: values built inside the vm context carry their own Array prototype.
  const plain = (v) => JSON.parse(JSON.stringify(v));
  assert.deepEqual(plain(page.motion.modes), browser.MOTION_MODES, 'the same five modes, in the same order');
  assert.deepEqual(plain(page.motion.labels), browser.MOTION_MODE_LABELS, 'and the same dropdown labels');
});

test('setting the mode persists it, restamps <html>, and an unknown one reads as particles', () => {
  const page = loadAccent();
  assert.equal(page.motion.set('fire'), 'fire');
  assert.equal(page.store.get('stencil_motion'), 'fire');
  assert.equal(page.dataMotion(), 'fire');
  assert.equal(page.motion.set('sparkles'), 'particles', 'junk falls back');
  assert.equal(page.dataMotion(), 'particles');
  // A stored junk value never silences the page either.
  assert.equal(loadAccent({ stored: { stencil_motion: 'nope' } }).motion.get(), 'particles');
  assert.equal(loadAccent({ stored: { stencil_motion: 'water' } }).dataMotion(), 'water');
  // Private mode: the default stands, nothing throws.
  assert.equal(loadAccent({ storageThrows: true }).motion.set('slide'), 'slide');
});

test('the gates: particles / water / fire fly, slide moves without particles, none is still', () => {
  const page = loadAccent();
  const expect = (mode, reduced, particles, style) => {
    page.motion.set(mode);
    assert.equal(page.motion.reduced(), reduced, `${mode}: reduced`);
    assert.equal(page.motion.particles(), particles, `${mode}: particles`);
    assert.equal(page.motion.style(), style, `${mode}: style`);
  };
  expect('particles', false, true, 'dust');
  expect('water', false, true, 'water');
  expect('fire', false, true, 'fire');
  expect('slide', false, false, null);
  expect('none', true, false, null);
  // The OS preference wins over any stored mode.
  page.motion.set('fire');
  page.setPrefersReduced(true);
  assert.equal(page.motion.reduced(), true);
  assert.equal(page.motion.particles(), false);
  assert.equal(page.motion.style(), null);
});

test('a mode changed on another page lands here through the storage event', () => {
  const page = loadAccent();
  let seen = null;
  page.motion.onChange((m) => { seen = m; });
  page.store.set('stencil_motion', 'water');
  page.fireStorage('stencil_motion');
  assert.equal(page.dataMotion(), 'water', 'the CSS half is restamped');
  assert.equal(seen, 'water', 'and the listener told');
});

test('the classic-script styleFrame is cloud.js styleFrame, frame for frame', async () => {
  const { styleFrame } = await import('../../../src/lib/dust/cloud.js');
  const page = loadAccent();
  for (const style of [0, 1, 2]) {
    for (const p of [0, 0.13, 0.5, 0.87, 1]) {
      for (const w of [0, 0.37, 0.91]) {
        for (const t of [0, 133, 777]) {
          // Plain numbers (no -0, no vm prototypes) on both sides.
          const norm = (o) => Object.fromEntries(Object.entries(o).map(([k, v]) => [k, +(+v).toFixed(12) + 0]));
          assert.deepEqual(norm(page.motion.styleFrame(style, p, 1 - p, w, 90, t)),
                           norm(styleFrame(style, p, 1 - p, w, 90, t)), `style ${style} p ${p} w ${w} t ${t}`);
        }
      }
    }
  }
});

test('a water or fire wake is painted from the accent palette; slide keeps the wipe and drops the grain', async () => {
  const styled = loadAccent({ withViewTransitions: true, stored: { stencil_motion: 'fire' } });
  styled.accent.set('sky', 'theme-toggle');
  await Promise.resolve(); await Promise.resolve();
  assert.equal(styled.bodyChildren.length, 1, 'one dust layer');
  const stage = styled.bodyChildren[0];
  styled.frame(0); styled.frame(200);
  const lit = stage.fills.filter((f) => f.arcs > 0);
  assert.ok(lit.length > 0, 'grains were painted');
  // Every fill is a palette colour — a stop of the accent ramp or one of its tints
  // (lib/cloud.js paletteCss) — never a pixel of the page.
  const ramp = /^color-mix\(in srgb, var\(--accent\) \d+%, var\(--accent-2\)\)$/;
  const tint = /^(#b4b4b4|#6e6e6e|color-mix\(in srgb, var\(--accent\) 55%, #ffffff\)|var\(--dust-(ink|accent-alt), #\w{6}\))$/;
  for (const f of lit) assert.ok(ramp.test(f.colour) || tint.test(f.colour), `palette fill, got ${f.colour}`);
  assert.ok(lit.some((f) => ramp.test(f.colour)), 'most of it the accent');
  assert.ok(lit.some((f) => tint.test(f.colour)), '…and a tinted minority');
  assert.ok(new Set(lit.map((f) => f.colour)).size >= 2, 'more than one stop of it');

  const sliding = loadAccent({ withViewTransitions: true, stored: { stencil_motion: 'slide' } });
  sliding.accent.set('sky', 'theme-toggle');
  await Promise.resolve(); await Promise.resolve();
  assert.equal(sliding.dataAccent(), 'sky', 'the palette still swaps');
  assert.ok(sliding.swapOrigin(), 'the wipe still plays');
  assert.equal(sliding.bodyChildren.length, 0, 'no wake');

  const still = loadAccent({ withViewTransitions: true, stored: { stencil_motion: 'none' } });
  still.accent.set('sky', 'theme-toggle');
  assert.equal(still.dataAccent(), 'sky');
  assert.equal(still.swapOrigin(), null, 'none: the palette applies outright, no wipe');
});
