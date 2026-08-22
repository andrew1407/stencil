import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { quadrantAt, mountDropZones } from '../src/lib/dropZones.js';

// The 4-quadrant map the on-page drop overlay uses (see lib/dropZones.js / popup drag).
test('quadrantAt maps each corner to its action', () => {
  const W = 1000, H = 800;
  assert.equal(quadrantAt(10, 10, W, H), 'here');          // top-left
  assert.equal(quadrantAt(990, 10, W, H), 'incognito');    // top-right
  assert.equal(quadrantAt(10, 790, W, H), 'newtab');       // bottom-left
  assert.equal(quadrantAt(990, 790, W, H), 'crop');        // bottom-right
});

test('quadrantAt splits on the exact midpoint (< is top/left)', () => {
  const W = 1000, H = 800;
  // Exactly on the divide counts as the far (right/bottom) half, since the test is `< half`.
  assert.equal(quadrantAt(500, 400, W, H), 'crop');
  assert.equal(quadrantAt(499, 399, W, H), 'here');
  assert.equal(quadrantAt(500, 399, W, H), 'incognito');
  assert.equal(quadrantAt(499, 400, W, H), 'newtab');
});


// ── The zones wear the EXTENSION's theme, not the page's ────────────────────
// They used to key their light palette off `@media (prefers-color-scheme: light)`, so a
// dark-set extension still dropped white panels onto a light desktop. The Appearance
// choice now travels in (unresolved — only the target page can answer 'system').
// NB: mounting arms the overlay's own 12s self-teardown timer. Left running it fires
// after the stub document is gone and takes the whole FILE down with it, so every caller
// mounts under mocked timers.
const stylesFor = (mode, prefersDark) => {
  const styles = [];
  const el = () => ({ style: {}, classList: { add() {}, remove() {} }, set textContent(v) { this._t = v; },
                      get textContent() { return this._t; }, append() {}, appendChild() {}, addEventListener() {},
                      set innerHTML(v) { this._h = v; }, remove() {}, attachShadow: () => ({ append: (...n) => styles.push(...n) }) });
  const priorDoc = globalThis.document, priorWin = globalThis.window;
  globalThis.document = {
    createElement: () => el(),
    getElementById: () => null,
    body: { appendChild() {} },
    documentElement: { appendChild() {} },
    addEventListener() {},
    removeEventListener() {},
  };
  globalThis.window = {
    matchMedia: () => ({ matches: prefersDark }),
    innerWidth: 1000, innerHeight: 800,
    addEventListener() {}, removeEventListener() {},
  };
  try { mountDropZones('#7c3aed', false, mode); } catch { /* only the stylesheet matters */ }
  finally { globalThis.document = priorDoc; globalThis.window = priorWin; }
  return styles.map((n) => n.textContent || '').join('\n');
};

test('the zone palette follows the Appearance choice, not the OS', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  // Dark extension on a LIGHT desktop: dark panels (the reported bug — white before).
  const darkOnLightOs = stylesFor('dark', false);
  assert.match(darkOnLightOs, /background:rgba\(33,36,45/, 'dark panels');
  assert.ok(!/prefers-color-scheme/.test(darkOnLightOs),
    'no OS media query decides the palette any more');

  // Light extension on a DARK desktop: light panels.
  assert.match(stylesFor('light', true), /background:rgba\(244,245,247/, 'light panels');

  // 'system' is the only mode that asks the page.
  assert.match(stylesFor('system', true), /background:rgba\(33,36,45/);
  assert.match(stylesFor('system', false), /background:rgba\(244,245,247/);
});

test('the zone panels let a fifth more of the page through', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  // .86/.90 → .69/.72: a 20% cut, so the page under the zones stays readable.
  assert.match(stylesFor('dark', false), /rgba\(33,36,45,\.69\)/);
  assert.match(stylesFor('light', true), /rgba\(244,245,247,\.72\)/);
});

test('the service worker hands the Appearance mode to the injected zones', () => {
  const sw = readFileSync(new URL('../src/background/background.js', import.meta.url), 'utf8');
  assert.match(sw, /THEME_STORAGE_KEY/, 'it reads the mirrored Appearance mode');
  assert.match(sw, /func: mountDropZones, args: \[accent, !!probe\.ok, mode\]/,
    'and passes it through executeScript');
});
