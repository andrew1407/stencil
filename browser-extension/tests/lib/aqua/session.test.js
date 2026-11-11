// The aqua skin's accent (src/lib/aqua/skin.js): the skin paints its gel in the accent on screen and
// never changes it; Blue is a stored setting its word writes (lib/prefs/shellPrefs.js, browser adopt.js),
// so a page re-dressed from the stored skin wears whatever the user has.
import test from 'node:test';
import assert from 'node:assert/strict';
import { gelTokens } from '../../../src/lib/aqua/rules.js';
import { AQUA_ACCENT } from '../../../src/lib/aqua/skin.js';
import { installSkins } from '../../../src/lib/skin/install.js';
import { heldSkin } from '../../../src/lib/skin/hold.js';
import { HEX, fakePage } from '../../helpers/aquaPage.js';

const skinOff = (p) => { p.root.removeAttribute('data-skin'); p.fire(); };
const picked = (p, key) => {
  p.win.fire('stencil:accent-changed', { detail: key });
  p.root.setAttribute('data-accent', key);
  p.fire();
};

test('the skin never changes the accent: a page re-dressed from the stored skin wears the user\'s', () => {
  const p = fakePage('aqua', { accent: 'red' });
  try {
    installSkins(p.doc, p.win);
    assert.equal(AQUA_ACCENT, 'blue');
    assert.equal(p.root.getAttribute('data-accent'), 'red');
    assert.deepEqual(p.root.style.props, gelTokens(HEX.red), 'the gel wears the accent on screen');
    skinOff(p);
    assert.equal(p.root.getAttribute('data-accent'), 'red');
  } finally { p.restore(); }
});

test('a pick while the skin is on repaints the gel and stands when it goes', () => {
  const p = fakePage('aqua', { accent: 'red' });
  try {
    installSkins(p.doc, p.win);
    picked(p, 'green');
    assert.deepEqual(p.root.style.props, gelTokens(HEX.green), 'the gel follows the pick');
    skinOff(p);
    assert.equal(p.root.getAttribute('data-accent'), 'green');
  } finally { p.restore(); }
});

test('a page in Blue is left alone both ways', () => {
  const p = fakePage('aqua');
  try {
    let writes = 0;
    const set = p.root.setAttribute;
    p.root.setAttribute = (k, v) => { if (k === 'data-accent') writes += 1; set(k, v); };
    installSkins(p.doc, p.win);
    skinOff(p);
    assert.equal(writes, 0);
    assert.equal(p.root.getAttribute('data-accent'), 'blue');
  } finally { p.restore(); }
});

test('the hold reads the Blue on screen while aqua is on, so it turns the skin off', () => {
  const root = { style: { getPropertyValue: () => '' },
    getAttribute: (k) => ({ 'data-accent': 'blue', 'data-theme': 'light' })[k] ?? null };
  const win = { StencilAccent: { get: () => 'red' }, StencilMotion: { get: () => 'slide' }, document: { documentElement: root },
    StencilSkin: { get: () => false, aqua: { get: () => true } } };
  assert.equal(heldSkin(win), 'aqua');
  win.StencilSkin.aqua.get = () => false;
  assert.equal(heldSkin(win), null, 'off, the stored accent rules');
});
