// The settings surface (js/console/stencilApi.js): the incognito toggle guard, shortcut
// rebinding against the real hotkeys singleton, and every flattened setting's app setter.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, hotkeys, makeApp, called, lastCall } from './helpers/stencilApiRig.js';

// ── Incognito toggle guard ────────────────────────────────────────────────────────
test('incognito setter only enables on a blank editor', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  app.image = { width: 1, height: 1 };
  assert.throws(() => { stencil.incognito = true; }, /blank editor/);

  app.image = null;
  stencil.incognito = true;
  assert.equal(app.storage.incognito, true);
  assert.equal(called(app, 'updateIncognitoUI').length, 1);
});

// ── Shortcuts (real hotkeys singleton) ─────────────────────────────────────────────
test('changeShortcut rebinds, rejects unknown refs, and rejects conflicts', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  const entries = hotkeys.entries();
  assert.ok(entries.length >= 2, 'need at least two shortcuts to test conflicts');
  const [id0, combo0] = entries[0];
  const [, combo1] = entries[1];
  const FREE = 'ctrl+shift+f13';

  try {
    assert.throws(() => stencil.changeShortcut('no-such-action', FREE), /No shortcut matches/);
    assert.throws(() => stencil.changeShortcut(id0, combo1), /already bound/);

    assert.equal(stencil.changeShortcut(id0, FREE), stencil);
    assert.equal(stencil.shortcuts[id0], FREE);
    // oldRef may be the current combo string too.
    stencil.changeShortcut(FREE, combo0);
    assert.equal(stencil.shortcuts[id0], combo0);
  } finally {
    hotkeys.set(id0, combo0);   // restore the singleton for other test files
  }
});

// ── Full settings sweep ───────────────────────────────────────────────────────────
test('every documented flattened setting routes to its app setter with the expected arg', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  // [facade key, app setter, value to assign, expected setter arg]. Hex values dodge the
  // canvas-less toHexColor (named colors would pass through unchanged anyway).
  const DIRECT = [
    ['unit', 'setUnit', 'in', 'in'],
    ['lineColor', 'setColor', '#abcdef', '#abcdef'],
    ['thickness', 'setThickness', 3, 3],
    ['pointSize', 'setPointSize', 9, 9],
    ['pointSize', 'setPointSize', 9, 9],
    ['lineStyle', 'setLineStyle', 'dashed', 'dashed'],
    ['pointStyle', 'setShowPoints', true, true],
    ['showPoints', 'setShowPoints', true, true],
    ['showLines', 'setShowLines', false, false],
    ['filter', 'setImageFilter', 'sepia', 'sepia'],
    ['filterColor', 'setFilterColor', '#7c3aed', '#7c3aed'],
    ['pageSize', 'setPageSize', 'a3', 'a3'],
    ['pageWidth', 'setCustomPageWidth', 30, 30],
    ['pageHeight', 'setCustomPageHeight', 40, 40],
    ['darkTheme', 'setTheme', true, 'dark'],
    ['allowFormulas', 'setAllowFormulas', true, true],
  ];
  for (const [key, setter, value, expected] of DIRECT) {
    stencil[key] = value;
    assert.deepEqual(lastCall(app, setter), [setter, expected], `${key} → ${setter}`);
  }

  // Special routing: drawMode normalizes, formulas pick an axis, visual colors pick a channel.
  stencil.drawMode = 'rect';
  assert.deepEqual(lastCall(app, 'setDrawMode'), ['setDrawMode', 'rect']);
  stencil.holdDrawDelay = 750;
  assert.deepEqual(lastCall(app, 'setHoldDrawDelay'), ['setHoldDrawDelay', 750]);
  stencil.formulaX = 'x*2';
  assert.deepEqual(lastCall(app, 'setFormula'), ['setFormula', 'x', 'x*2']);
  stencil.formulaY = 'y+1';
  assert.deepEqual(lastCall(app, 'setFormula'), ['setFormula', 'y', 'y+1']);
  for (const [key, channel] of [['fillColor', 'fill'], ['selectionGlow', 'selGlow'], ['hoverRing', 'hoverRing'], ['focusRing', 'focusRing']]) {
    stencil[key] = '#123456';
    assert.deepEqual(lastCall(app, 'setVisualColor'), ['setVisualColor', channel, '#123456'], `${key} → setVisualColor`);
  }
});

// mainTheme: preset keys persist+sync via setAccent; a hex applies a temp page-local accent
// via setCustomAccent; the getter prefers an active custom hex; junk throws.
test('mainTheme accepts preset keys and custom hex colours', () => {
  const app = makeApp();
  const stencil = createStencil(app);

  assert.equal(stencil.mainTheme, 'violet');         // preset key from app.accent

  stencil.mainTheme = 'GREEN';                        // case-insensitive preset
  assert.deepEqual(lastCall(app, 'setAccent'), ['setAccent', 'green']);
  assert.equal(stencil.mainTheme, 'green');

  stencil.mainTheme = '#FF5623';                      // custom hex → normalized, page-local
  assert.deepEqual(lastCall(app, 'setCustomAccent'), ['setCustomAccent', '#ff5623']);
  assert.equal(stencil.mainTheme, '#ff5623');         // getter prefers the custom hex

  stencil.mainTheme = 'f50';                          // short hex, no '#'
  assert.deepEqual(lastCall(app, 'setCustomAccent'), ['setCustomAccent', '#ff5500']);

  assert.throws(() => { stencil.mainTheme = 'notacolour'; }, /Unknown theme/);
});
