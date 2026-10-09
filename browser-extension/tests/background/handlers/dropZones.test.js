import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { mountDropZones } from '../../../src/lib/drop/zones.js';
import { installDom, stubDoc, stubEl, stubWin } from '../../helpers/domStub.js';

// Which action a drop at each corner sends: mounted on the stub page, a drop is fired at the
// point and the PAGE_DROP message read back. `<` is top/left, so the exact middle is the far cell.
const dropAt = (x, y) => {
  const sent = [];
  const win = stubWin({ innerWidth: 1000, innerHeight: 800 });
  const el = () => stubEl('div', { attachShadow: () => ({ append() {} }) });
  const restore = installDom({
    document: stubDoc({ createElement: el }), window: win,
    chrome: { runtime: { sendMessage: (m) => { sent.push(m); } } },
  });
  try {
    mountDropZones('#7c3aed', false, 'light');
    const dataTransfer = { types: ['text/uri-list'], getData: (k) => (k === 'text/uri-list' ? 'https://cdn.example/a.png' : '') };
    win.fire('drop', { clientX: x, clientY: y, dataTransfer, preventDefault() {}, stopPropagation() {} });
  } finally { restore(); }
  return sent.map((m) => m.action);
};

test('a drop in each quadrant sends that quadrant\'s action', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  assert.deepEqual(dropAt(10, 10), ['here']);
  assert.deepEqual(dropAt(990, 10), ['incognito']);
  assert.deepEqual(dropAt(10, 790), ['newtab']);
  assert.deepEqual(dropAt(990, 790), ['crop']);
});

test('the split is on the exact midpoint, which counts as the far half', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  assert.deepEqual(dropAt(500, 400), ['crop']);
  assert.deepEqual(dropAt(499, 399), ['here']);
  assert.deepEqual(dropAt(500, 399), ['incognito']);
  assert.deepEqual(dropAt(499, 400), ['newtab']);
});

// The zones wear the EXTENSION's Appearance choice, which travels in unresolved. NB: mounting
// arms the overlay's 12s self-teardown — left running it takes the whole FILE down, so mock timers.
const stylesFor = (mode, prefersDark) => {
  const styles = [];
  const el = () => stubEl('div', { attachShadow: () => ({ append: (...n) => styles.push(...n) }) });
  const restore = installDom({
    document: stubDoc({ createElement: el }),
    window: stubWin({ innerWidth: 1000, innerHeight: 800, matchMedia: () => ({ matches: prefersDark }) }),
  });
  try { mountDropZones('#7c3aed', false, mode); } catch { /* only the stylesheet matters */ }
  finally { restore(); }
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

test('the zone panels carry the editor\'s own weight', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  // .88 = the editor's own zone fill (browser controls.css #global-drop-overlay .drop-zone),
  // so a target drawn on a page and one drawn on the canvas read alike.
  assert.match(stylesFor('dark', false), /rgba\(33,36,45,\.88\)/);
  assert.match(stylesFor('light', true), /rgba\(244,245,247,\.88\)/);
  // …and the aimed cell mixes into the SOLID panel colour, as the editor's aimed half does.
  assert.match(stylesFor('dark', false), /\.cell\.over\{background:color-mix\(in srgb, \S+ 30%, #21242d\)/);
  assert.match(stylesFor('light', true), /\.cell\.over\{background:color-mix\(in srgb, \S+ 22%, #f4f5f7\)/);
});

test('the service worker hands the Appearance mode to the injected zones', () => {
  const sw = readFileSync(new URL('../../../src/background/handlers/dropZones.js', import.meta.url), 'utf8');
  assert.match(sw, /THEME_STORAGE_KEY/, 'it reads the mirrored Appearance mode');
  assert.match(sw, /func: mountDropZones, args: \[accent, !!probe\.ok, mode\]/,
    'and passes it through executeScript');
});
