// The canvas cursor is STATE-DRIVEN (js/core/drawingApp.js updateButtons + css/layout.css).
//
// The crosshair says "click here to place a point". It used to be a constant on `canvas`
// and `.canvas-viewport`, so an EMPTY editor — whose canvas is the drop hint and the
// "＋ Blank image" card, with nothing to draw on — aimed at nothing, and so did a COMPARE
// view, which is read-only. Both states were already published/known (body.canvas-empty,
// compareReadOnly()); the cursor just did not follow them.
//
// Element stubs + the updateButtons rig from fullscreenGate.test.js.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom } from './helpers/dom.js';
import { COMPONENTS_CSS } from './helpers/css.js';

const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
const blockAt = (at) => css.slice(at, css.indexOf('}', at));
const cursorIn = (at) => /cursor:\s*([a-z-]+)/.exec(blockAt(at))?.[1];

// Resolve the cursor the way the cascade does, from the real stylesheet: the base
// declaration, replaced by the state-gated rule when the body classes let it match (it is
// both more specific and declared later, which the pin below enforces).
const cursorFor = (bodyClasses, base) => {
  const baseAt = css.indexOf(`${base} {`);
  const gate = `body:not(.canvas-empty):not(.canvas-readonly) ${base}`;
  const gateAt = css.indexOf(gate);
  assert.ok(baseAt >= 0, `no base rule for ${base}`);
  assert.ok(gateAt > baseAt, `the state-gated cursor for ${base} must be declared after its base rule`);
  const blockedBy = [...gate.matchAll(/:not\(\.([a-z-]+)\)/g)].map((m) => m[1]);
  return blockedBy.some((c) => bodyClasses.has(c)) ? cursorIn(baseAt) : cursorIn(gateAt);
};

// ── The updateButtons rig (same shape as fullscreenGate.test.js) ────────
const makeDom = () => {
  const doc = installDom({ autoCreateById: true });
  return { el: doc.getElementById, bodyClasses: doc.body.classes };
};

const makeApp = (over = {}) => ({
  image: null,
  lines: [],
  isDrawing: false,
  currentLine: null,
  canvas: { style: { cursor: '' } },
  history: { canUndo: () => false, canRedo: () => false },
  remoteLink: null,
  compareReadOnly: () => false,
  openInAvailable: () => false,
  syncDrawToggleUI() {},
  syncDrawModeUI() {},
  updateStencilSyncUI() {},
  updateIncognitoUI() {},
  updateProjectTitle() {},
  renderLinesList() {},
  ...over,
});

// Run updateButtons for a state and report what the canvas and its viewport would show.
const cursorsFor = async (over) => {
  const { bodyClasses } = makeDom();
  const { DrawingApp } = await import('../js/core/drawingApp.js');
  const app = makeApp(over);
  DrawingApp.prototype.updateButtons.call(app);
  return {
    canvas: cursorFor(bodyClasses, 'canvas'),
    viewport: cursorFor(bodyClasses, '.canvas-viewport'),
    classes: bodyClasses,
    app,
  };
};

const IMAGE = { width: 40, height: 30 };

// ── The four states ─────────────────────────────────────────────────────
test('empty editor: the ordinary pointer — there is nothing to draw on', async () => {
  const { canvas, viewport, classes } = await cursorsFor({});
  assert.equal(canvas, 'default');
  assert.equal(viewport, 'default', 'the surrounding void is not an aim target either');
  assert.ok(classes.has('canvas-empty'));
});

test('image loaded: the aim comes back', async () => {
  const { canvas, viewport, classes } = await cursorsFor({ image: IMAGE });
  assert.equal(canvas, 'crosshair');
  assert.equal(viewport, 'crosshair');
  assert.ok(!classes.has('canvas-empty') && !classes.has('canvas-readonly'));
});

test('compare on: an image, but read-only — the aim goes away again', async () => {
  const { canvas, viewport, classes } = await cursorsFor({ image: IMAGE, compareReadOnly: () => true });
  assert.equal(canvas, 'default', 'a comparison cannot be edited, so it must not invite a click');
  assert.equal(viewport, 'default');
  assert.ok(classes.has('canvas-readonly'), 'published off compareReadOnly() — the undo/redo gate\'s own state');
  assert.equal(document.getElementById('undo').disabled, true, 'the same state still greys undo/redo');
});

test('drawing on: still the aim (that is exactly when a click places a point)', async () => {
  const { canvas } = await cursorsFor({ image: IMAGE, isDrawing: true, currentLine: { points: [] } });
  assert.equal(canvas, 'crosshair');
});

// ── The transitions ─────────────────────────────────────────────────────
test('every transition moves the cursor — load, clear, compare on/off', async () => {
  const { bodyClasses } = makeDom();
  const { DrawingApp } = await import('../js/core/drawingApp.js');
  const app = makeApp();
  const run = () => DrawingApp.prototype.updateButtons.call(app);
  const now = () => cursorFor(bodyClasses, 'canvas');

  run();
  assert.equal(now(), 'default', 'starts empty');
  app.image = IMAGE;            run(); assert.equal(now(), 'crosshair', 'image loaded');
  app.compareReadOnly = () => true;  run(); assert.equal(now(), 'default', 'compare on');
  app.compareReadOnly = () => false; run(); assert.equal(now(), 'crosshair', 'compare off');
  app.image = null;             run(); assert.equal(now(), 'default', 'image cleared (clear op / removeProject fallback)');
});

test('a transition drops the inline cursor a hover left behind, so the stylesheet wins', async () => {
  const { DrawingApp } = await import('../js/core/drawingApp.js');
  makeDom();
  const app = makeApp({ image: IMAGE });
  DrawingApp.prototype.updateButtons.call(app);
  app.canvas.style.cursor = 'crosshair';     // what canvasMouseMove leaves on an editable image
  app.image = null;                          // …and now the image goes away
  DrawingApp.prototype.updateButtons.call(app);
  assert.equal(app.canvas.style.cursor, '',
    'a stale inline crosshair would outlive the change until the pointer moved again');

  // …but a call that changes nothing must not wipe a live drag cursor (grabbing/move).
  app.canvas.style.cursor = 'grabbing';
  DrawingApp.prototype.updateButtons.call(app);
  assert.equal(app.canvas.style.cursor, 'grabbing', 'only a real transition resets it');
});

// ── The empty-editor placeholder ────────────────────────────────────────
test('the "＋ Blank image" card owns only its own box — no click/cursor overreach', async () => {
  const comp = COMPONENTS_CSS;
  const at = comp.indexOf('.idle-create {');
  const btnAt = comp.indexOf('.idle-create-btn {');
  assert.ok(at >= 0 && btnAt >= 0);
  const overlay = comp.slice(at, comp.indexOf('}', at));
  const btn = comp.slice(btnAt, comp.indexOf('}', btnAt));
  // The overlay stretches across the whole empty canvas (inset: 0) — that is the LAYOUT,
  // not the hit target. Without pointer-events:none it would be the desktop's bug: the
  // whole void clickable, and the card's cursor over all of it.
  assert.match(overlay, /inset: 0/);
  assert.match(overlay, /pointer-events: none/, 'the stretched overlay must not take clicks');
  assert.match(btn, /pointer-events: auto/, 'only the card itself does');
  assert.match(btn, /cursor: pointer/, 'and it reads as the control it is');
});
