// The webcore skin (lib/webcore/, the StencilSkin pre-paint half in lib/prefs/shellPrefs.js):
// its palette and hold rules pinned to the browser's, the gate, the pixel art and the hold.
import test, { mock } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { loadAccent } from '../../helpers/accentSandbox.js';
import { holdAllowed, wireWebcoreHold } from '../../../src/lib/webcore/skin.js';
import { HOLD_MS } from '../../../src/lib/logo/stageRules.js';
import { pixelArt, pixelIconSvg, hasPixelIcon } from '../../../src/lib/webcore/icons.js';

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');
const json = (rel) => JSON.parse(read(rel));

// The --wc-* declarations of one `selector { … }` block.
const wcTokens = (css, selector) => {
  const at = css.indexOf(`${selector} {`);
  const body = css.slice(at, css.indexOf('\n}', at));
  return Object.fromEntries([...body.matchAll(/(--wc-[a-z-]+):\s*(#[0-9a-f]{6});/g)].map((m) => [m[1], m[2]]));
};

test('the palette is the browser skin\'s, value for value, on both faces', () => {
  const css = read('../../../src/lib/webcore/tokens.css');
  const { tokens } = json('../../../../browser/js/config/webcore.json');
  assert.deepEqual(wcTokens(css, ':root[data-skin="webcore"]'), tokens.light);
  assert.deepEqual(wcTokens(css, ':root[data-skin="webcore"][data-theme="dark"]'), tokens.dark);
});

const fakeWin = (accent, motion, custom = '') => ({
  StencilAccent: { get: () => accent }, StencilMotion: { get: () => motion },
  StencilSkin: { on: false, get() { return this.on; }, set(v) { this.on = v; return v; } },
  document: { documentElement: { style: { getPropertyValue: () => custom } } },
});

// [accent, stored motion, custom hex, allowed]: the browser table's webcore row and its neighbours.
const HOLD_CASES = Object.freeze([
  ['grey', 'none', '', true],
  ['violet', 'none', '', false],
  ['grey', 'particles', '', false],
  ['grey', 'slide', '', false],
  ['grey', 'none', '#64748b', false],
]);

test('the hold toggles only where the browser\'s opens its webcore row', () => {
  for (const [accent, motion, custom, allowed] of HOLD_CASES)
    assert.equal(holdAllowed(fakeWin(accent, motion, custom)), allowed, `${accent} + ${motion} + '${custom}'`);
  assert.equal(holdAllowed({}), false, 'no pre-paint facades, no skin');
});

test('the hold reads the user\'s own mode, not the skin\'s stillness', () => {
  const win = fakeWin('grey', 'none');
  win.StencilMotion.stored = () => 'water';
  assert.equal(holdAllowed(win), false);
});

test('pixel art: the mark\'s ring wears the accent, the dark face re-inks the ink', () => {
  assert.ok(hasPixelIcon('logo') && hasPixelIcon('gear'));
  assert.equal(pixelArt('sidebar'), '', 'an extension-only glyph has no pixel twin');
  assert.match(pixelArt('logo', { accent: '#64748b' }), /fill="#64748b"/);
  assert.notEqual(pixelArt('plus', { dark: true }), pixelArt('plus'));
  assert.match(pixelIconSvg('logo', {}, 16), /^<svg [^>]*viewBox="0 0 16 16" width="16"/);
});

test('StencilSkin: stamped before paint, stored, and followed across pages', () => {
  const off = loadAccent();
  assert.equal(off.dataSkin(), null);
  assert.equal(off.skin.get(), false);
  off.skin.set(true);
  assert.equal(off.dataSkin(), 'webcore');
  assert.equal(off.store.get('stencil_skin'), 'webcore');
  const on = loadAccent({ stored: { stencil_skin: 'webcore' } });
  assert.equal(on.dataSkin(), 'webcore');
  on.store.set('stencil_skin', '');
  on.fireStorage('stencil_skin');
  assert.equal(on.dataSkin(), null, 'another page turned it off');
});

// Just enough of an element for the hold: listeners by type, fired by hand.
const fakeWrap = () => {
  const on = new Map();
  return {
    addEventListener: (t, fn) => { if (!on.has(t)) on.set(t, new Set()); on.get(t).add(fn); },
    removeEventListener: (t, fn) => on.get(t)?.delete(fn),
    fire: (t, e = {}) => { for (const fn of [...(on.get(t) || [])]) fn({ button: 0, clientX: 5, clientY: 5,
      preventDefault() { this.prevented = true; }, stopImmediatePropagation() {}, ...e }); },
    count: (t) => on.get(t)?.size ?? 0,
  };
};

test('a still press held for HOLD_MS flips the skin and eats the release\'s click', () => {
  mock.timers.enable({ apis: ['setTimeout'] });
  try {
    const win = fakeWin('grey', 'none'), wrap = fakeWrap();
    wireWebcoreHold(wrap, { win });
    wrap.fire('pointerdown');
    mock.timers.tick(HOLD_MS - 1);
    assert.equal(win.StencilSkin.on, false);
    mock.timers.tick(1);
    assert.equal(win.StencilSkin.on, true);
    wrap.fire('pointerup');
    assert.equal(wrap.count('click'), 1, 'the swallow waits for the click');

    const short = fakeWrap();
    wireWebcoreHold(short, { win });
    short.fire('pointerdown');
    mock.timers.tick(HOLD_MS / 2);
    short.fire('pointerup');
    mock.timers.tick(HOLD_MS);
    assert.equal(win.StencilSkin.on, true, 'a short press is a click, not a hold');

    short.fire('pointerdown');
    short.fire('pointermove', { clientX: 40 });
    mock.timers.tick(HOLD_MS);
    assert.equal(win.StencilSkin.on, true, 'a press that drifts is a drag');

    const barred = fakeWin('violet', 'none'), other = fakeWrap();
    wireWebcoreHold(other, { win: barred });
    other.fire('pointerdown');
    mock.timers.tick(HOLD_MS);
    assert.equal(barred.StencilSkin.on, false, 'outside grey + None the hold does nothing');
  } finally {
    mock.timers.reset();
  }
});

test('the skin stills the interface and hands the user\'s mode back when it goes', () => {
  const page = loadAccent({ stored: { stencil_motion: 'water' } });
  page.skin.set(true);
  assert.equal(page.motion.get(), 'none');
  assert.equal(page.dataMotion(), 'none');
  assert.equal(page.motion.stored(), 'water', 'the choice is held, not lost');
  page.skin.set(false);
  assert.equal(page.motion.get(), 'water');
  assert.equal(page.dataMotion(), 'water');
  assert.equal(page.store.get('stencil_motion_held'), '');
});

test('a mode already None stays None, and a pick under the skin is kept', () => {
  const still = loadAccent({ stored: { stencil_motion: 'none' } });
  still.skin.set(true);
  still.skin.set(false);
  assert.equal(still.motion.get(), 'none');

  const picked = loadAccent({ stored: { stencil_motion: 'fire' } });
  picked.skin.set(true);
  picked.motion.set('slide');
  assert.equal(picked.motion.stored(), 'slide');
  picked.skin.set(false);
  assert.equal(picked.motion.get(), 'slide', 'the user\'s own pick ends the hold');

  const backToNone = loadAccent({ stored: { stencil_motion: 'fire' } });
  backToNone.skin.set(true);
  backToNone.motion.set('none');
  backToNone.skin.set(false);
  assert.equal(backToNone.motion.get(), 'none', 'None picked on purpose stays None');
});
