// The aqua skin's session Blue (src/lib/aqua/skin.js, browser js/ui/aqua/toggle.js wearBlue /
// giveBack): the page wears the preset while the skin is on, never through the store, and gets
// its own accent back after unless the user picked one meanwhile.
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

test('session Blue: a preset page wears Blue under the skin and gets its own back after', () => {
  const p = fakePage('aqua', { accent: 'red' });
  try {
    installSkins(p.doc, p.win);
    assert.equal(AQUA_ACCENT, 'blue');
    assert.equal(p.root.getAttribute('data-accent'), 'blue');
    assert.equal(p.root.getAttribute('data-accent-light'), null, 'Blue takes the light ink');
    assert.deepEqual(p.root.style.props, gelTokens(HEX.blue), 'the gel is Blue\'s');
    assert.match(p.logo.innerHTML, new RegExp(gelTokens(HEX.blue)['--aq-gel-top']));
    assert.match(decodeURIComponent(p.link.href), new RegExp(gelTokens(HEX.blue)['--aq-gel-edge']));
    skinOff(p);
    assert.equal(p.root.getAttribute('data-accent'), 'red', 'the page\'s own preset is back');
    assert.equal(p.link.href, `data:image/svg+xml,${encodeURIComponent('<svg plain="#dc2626"/>')}`);
  } finally { p.restore(); }
});

test('session Blue: a page-only custom accent is set aside and put back', () => {
  const p = fakePage('aqua', { accent: 'red', custom: '#ff8800' });
  try {
    installSkins(p.doc, p.win);
    assert.equal(p.root.style.getPropertyValue('--accent'), '', 'Blue paints over no custom hex');
    assert.equal(p.root.getAttribute('data-accent'), 'blue');
    skinOff(p);
    assert.equal(p.root.style.getPropertyValue('--accent'), '#ff8800');
    assert.equal(p.root.getAttribute('data-accent'), 'red');
    assert.equal(p.root.getAttribute('data-accent-light'), '', 'the orange keeps its dark ink');
  } finally { p.restore(); }
});

test('session Blue: the user\'s own pick while the skin is on stands when it goes', () => {
  const p = fakePage('aqua', { accent: 'red' });
  try {
    installSkins(p.doc, p.win);
    picked(p, 'green');
    assert.deepEqual(p.root.style.props, gelTokens(HEX.green), 'the gel follows the pick');
    skinOff(p);
    assert.equal(p.root.getAttribute('data-accent'), 'green');
  } finally { p.restore(); }
});

test('session Blue: a pick in another page (the storage event) stands too; a preview does not', () => {
  const p = fakePage('aqua', { accent: 'red' });
  try {
    installSkins(p.doc, p.win);
    p.root.setAttribute('data-accent', 'green');   // a hover preview: no announce, no store
    p.fire();
    p.root.setAttribute('data-accent', 'blue');
    p.fire();
    skinOff(p);
    assert.equal(p.root.getAttribute('data-accent'), 'red', 'a preview drops nothing');
    p.root.setAttribute('data-skin', 'aqua');
    p.fire();
    p.win.fire('storage', { key: 'stencil_accent' });
    p.root.setAttribute('data-accent', 'green');
    p.fire();
    skinOff(p);
    assert.equal(p.root.getAttribute('data-accent'), 'green');
  } finally { p.restore(); }
});

test('session Blue: a page already in Blue is left alone both ways', () => {
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

test('the hold reads the session Blue on screen while aqua is on, so it turns the skin off', () => {
  const root = { style: { getPropertyValue: () => '' },
    getAttribute: (k) => ({ 'data-accent': 'blue', 'data-theme': 'light' })[k] ?? null };
  const win = { StencilAccent: { get: () => 'red' }, StencilMotion: { get: () => 'slide' }, document: { documentElement: root },
    StencilSkin: { get: () => false, aqua: { get: () => true } } };
  assert.equal(heldSkin(win), 'aqua');
  win.StencilSkin.aqua.get = () => false;
  assert.equal(heldSkin(win), null, 'off, the stored accent rules');
});
