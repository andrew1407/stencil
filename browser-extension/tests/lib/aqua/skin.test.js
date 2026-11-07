// The aqua skin (lib/aqua/ under lib/skin/install.js, the StencilSkin.aqua half in lib/prefs/shellPrefs.js):
// its palette pinned to the browser's, the gel, mark and glyphs in the page's accent, the hold that
// resolves blue + slide + light, and one skin at a time.
import test, { mock } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { loadAccent } from '../../helpers/accentSandbox.js';
import { heldSkin, wireSkinHold } from '../../../src/lib/skin/hold.js';
import { installSkins } from '../../../src/lib/skin/install.js';
import { HOLD_MS } from '../../../src/lib/logo/stageRules.js';
import { AQUA, gelTokens } from '../../../src/lib/aqua/rules.js';
import { HEX, fakePage } from '../../helpers/aquaPage.js';

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');

const pickGel = (tokens) => Object.fromEntries(Object.keys(gelTokens(null)).map((k) => [k, tokens[k]]));
const aqTokens = (css, selector) => {
  const at = css.indexOf(`${selector} {`);
  const body = css.slice(at, css.indexOf('\n}', at));
  // --aq-tick is the sheet's own (as in the browser's aqua.test.js), never a table colour.
  return Object.fromEntries([...body.matchAll(/(--aq-[a-z-]+):\s*(#[0-9a-f]{6});/g)]
    .filter((m) => m[1] !== '--aq-tick').map((m) => [m[1], m[2]]));
};

test('the palette is the browser skin\'s, value for value, on both faces', () => {
  const css = read('../../../src/lib/aqua/tokens.css');
  const { tokens } = JSON.parse(read('../../../../common/config/aqua.json'));
  assert.deepEqual(aqTokens(css, ':root[data-skin="aqua"]'), tokens.light);
  assert.deepEqual(aqTokens(css, ':root[data-skin="aqua"][data-theme="dark"]'), tokens.dark);
});

test('the gel follows the accent: the table\'s own at its base, the ink that reads on select', () => {
  assert.deepEqual(gelTokens(AQUA.tint.base), pickGel(AQUA.tokens.light));
  assert.deepEqual(gelTokens(AQUA.tint.base, true), pickGel(AQUA.tokens.dark));
  assert.equal(gelTokens(null)['--aq-select'], AQUA.tint.base, 'no accent is the base');
  const red = gelTokens('#dc2626');
  assert.equal(red['--aq-gel-bottom'], '#dc2626');
  assert.notEqual(red['--aq-gel-hi'], gelTokens(AQUA.tint.base)['--aq-gel-hi']);
  assert.equal(gelTokens('#facc15')['--aq-on-select'], '#1a1a1a', 'a light accent takes the dark ink');
});

test('installSkins: the gel, the mark and the favicon wear the accent, and follow it', () => {
  const p = fakePage();
  try {
    installSkins(p.doc, p.win);
    assert.deepEqual(p.root.style.props, gelTokens(HEX.blue));
    assert.match(p.logo.innerHTML, new RegExp(gelTokens(HEX.blue)['--aq-gel-top']));
    assert.doesNotMatch(p.logo.innerHTML, /%[1-4]/);
    assert.match(decodeURIComponent(p.link.href), new RegExp(gelTokens(HEX.blue)['--aq-gel-edge']));
    p.root.setAttribute('data-accent', 'red');
    p.fire();
    assert.deepEqual(p.root.style.props, gelTokens(HEX.red), 'an accent change repaints the gel');
    assert.match(p.logo.innerHTML, new RegExp(gelTokens(HEX.red)['--aq-gel-top']));
    assert.match(decodeURIComponent(p.link.href), new RegExp(gelTokens(HEX.red)['--aq-gel-edge']));
    p.root.setAttribute('data-theme', 'dark');
    p.fire();
    assert.deepEqual(p.root.style.props, gelTokens(HEX.red, true), 'and so does the face');
  } finally { p.restore(); }
});

// A hover preview writes only data-accent (lib/accent/accent.js previewAccent); its end writes it back.
test('installSkins: a previewed accent repaints the gel and the mark live, and its end puts them back', () => {
  const p = fakePage();
  try {
    installSkins(p.doc, p.win);
    const worn = { gel: { ...p.root.style.props }, mark: p.logo.innerHTML, icon: p.link.href };
    p.root.setAttribute('data-accent', 'green');
    p.fire();
    assert.deepEqual(p.root.style.props, gelTokens(HEX.green), 'the gel wears the preview');
    assert.match(p.logo.innerHTML, new RegExp(gelTokens(HEX.green)['--aq-gel-top']));
    p.root.setAttribute('data-accent', 'blue');
    p.fire();
    assert.deepEqual({ gel: p.root.style.props, mark: p.logo.innerHTML, icon: p.link.href }, worn);
  } finally { p.restore(); }
});

test('installSkins: the glossy glyphs go on with their own ids, and come off as they were', () => {
  const p = fakePage();
  try {
    installSkins(p.doc, p.win);
    const [a, b] = p.gears;
    assert.equal(a.getAttribute('viewBox'), '0 0 32 32');
    assert.equal(a.getAttribute('stroke'), null, 'the line-art paint is set aside');
    const ids = (el) => [...el.innerHTML.matchAll(/id="([^"]+)"/g)].map((m) => m[1]);
    assert.ok(ids(a).length && !ids(a).some((id) => ids(b).includes(id)), 'two copies never share an id');
    assert.doesNotMatch(a.innerHTML, /%u/);
    assert.equal(p.plain.innerHTML, '<rect/>', 'a glyph the table lacks keeps its line art');
    p.root.removeAttribute('data-skin');
    p.fire();
    assert.deepEqual(p.root.style.props, {}, 'every gel token is gone');
    assert.equal(a.innerHTML, '<circle/>');
    assert.equal(a.getAttribute('viewBox'), '0 0 24 24');
    assert.equal(a.getAttribute('stroke'), 'currentColor');
    assert.equal(a.getAttribute('data-aq'), null);
    assert.equal(p.logo.innerHTML, '<rect id="own"/>');
    assert.equal(p.link.href, `data:image/svg+xml,${encodeURIComponent('<svg plain="#2563eb"/>')}`);
  } finally { p.restore(); }
});

test('installSkins: straight to webcore, the pixel art lands on the page\'s own art, and back', () => {
  const p = fakePage();
  try {
    installSkins(p.doc, p.win);
    p.root.setAttribute('data-skin', 'webcore');
    p.fire();
    const [a] = p.gears;
    assert.equal(a.getAttribute('data-aq'), null, 'the gloss is shed first');
    assert.equal(a.getAttribute('data-wc'), '', 'webcore wears it');
    assert.equal(a.getAttribute('viewBox'), '0 0 16 16');
    assert.deepEqual(Object.keys(p.root.style.props).filter((k) => k.startsWith('--aq-')), [], 'and the gel is gone');
    p.root.removeAttribute('data-skin');
    p.fire();
    assert.equal(a.innerHTML, '<circle/>', 'the line art is back, not the gloss');
    assert.equal(a.getAttribute('viewBox'), '0 0 24 24');
    assert.equal(a.getAttribute('data-wc'), null);
  } finally { p.restore(); }
});

test('installSkins: a second install on the same page disposes the first', () => {
  const p = fakePage();
  try {
    let disconnected = 0;
    p.win.MutationObserver = class { constructor(cb) { this.cb = cb; } observe() {} disconnect() { disconnected += 1; } };
    installSkins(p.doc, p.win);
    const dispose = installSkins(p.doc, p.win);
    assert.equal(disconnected, 2, 'the first install\'s two watches are gone');
    dispose();
    assert.equal(disconnected, 4);
  } finally { p.restore(); }
});

const fakeWin = (accent, motion, theme = 'light') => {
  const skin = { name: '' };
  return {
    skin,
    StencilAccent: { get: () => accent }, StencilMotion: { get: () => motion },
    StencilSkin: {
      get: () => skin.name === 'webcore', set: (v) => { skin.name = v ? 'webcore' : ''; return v; },
      aqua: { get: () => skin.name === 'aqua', set: (v) => { skin.name = v ? 'aqua' : ''; return v; } },
    },
    document: { documentElement: { style: { getPropertyValue: () => '' }, getAttribute: () => theme } },
  };
};

test('the hold resolves aqua only for blue, sliding, on the light face', () => {
  assert.equal(heldSkin(fakeWin('blue', 'slide')), 'aqua');
  assert.equal(heldSkin(fakeWin('blue', 'slide', 'dark')), null);
  assert.equal(heldSkin(fakeWin('blue', 'water')), null, 'the water show is a stage, not a skin');
  assert.equal(heldSkin(fakeWin('sky', 'slide')), null);
  assert.equal(heldSkin(fakeWin('grey', 'none')), 'webcore');
});

test('a held press on blue + slide flips the aqua skin, never webcore', () => {
  mock.timers.enable({ apis: ['setTimeout'] });
  try {
    const win = fakeWin('blue', 'slide');
    const on = new Map();
    const wrap = {
      addEventListener: (t, fn) => { if (!on.has(t)) on.set(t, new Set()); on.get(t).add(fn); },
      removeEventListener: (t, fn) => on.get(t)?.delete(fn),
    };
    wireSkinHold(wrap, { win });
    for (const fn of on.get('pointerdown')) fn({ button: 0, clientX: 5, clientY: 5 });
    mock.timers.tick(HOLD_MS);
    assert.equal(win.skin.name, 'aqua');
  } finally {
    mock.timers.reset();
  }
});

test('StencilSkin.aqua: stored, stamped, slides, and swaps with webcore', () => {
  const page = loadAccent({ stored: { stencil_motion: 'water' } });
  assert.equal(page.skin.aqua.set(true), true);
  assert.equal(page.dataSkin(), 'aqua');
  assert.equal(page.store.get('stencil_skin'), 'aqua');
  assert.equal(page.motion.get(), 'slide', 'aqua slides');
  assert.equal(page.motion.stored(), 'water', 'the choice is held, not lost');
  assert.equal(page.skin.set(true), true, 'webcore takes the page');
  assert.equal(page.dataSkin(), 'webcore');
  assert.equal(page.skin.aqua.get(), false);
  assert.equal(page.motion.get(), 'none');
  page.skin.aqua.set(true);
  assert.equal(page.dataSkin(), 'aqua');
  assert.equal(page.motion.get(), 'slide', 'aqua slides again');
  page.skin.set(false);
  assert.equal(page.dataSkin(), 'aqua', 'turning off webcore leaves aqua alone');
  page.skin.aqua.set(false);
  assert.equal(page.dataSkin(), null);
  assert.equal(page.motion.get(), 'water', 'the user\'s mode is back');
  assert.equal(loadAccent({ stored: { stencil_skin: 'aqua' } }).dataSkin(), 'aqua', 'stamped before paint');
});
