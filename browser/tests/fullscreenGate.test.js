// Fullscreen is available on an EMPTY editor (js/core/drawingApp.js updateButtons,
// js/ui/fullscreenLayer.js toggleFullscreen).
//
// It used to be gated on `app.image` in two places at once — the toolbar button was
// disabled and the toggle itself returned early — so the imageless editor could not go
// fullscreen from the button, Alt+F, the context menu, or `stencil.fullscreen = true`.
// There is nothing to protect: the empty canvas carries the "＋ Blank image" card and the
// whole toolbar, which is exactly when the extra room is most useful. The one rule that
// stays is that the other image-dependent controls (zoom, undo/redo, crop…) are still off.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// Every id hands back a fresh stub element, so updateButtons can set .disabled /
// .style.display on anything it reaches for and we can read it back.
import { installDom } from './helpers/dom.js';

const makeDom = () => {
  const doc = installDom({ autoCreateById: true });
  return { el: doc.getElementById, bodyClasses: doc.body.classes };
};

const makeApp = (over = {}) => ({
  image: null,
  lines: [],
  isDrawing: false,
  currentLine: null,
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

test('the fullscreen button stays enabled with no image loaded', async () => {
  const { el, bodyClasses } = makeDom();
  const { DrawingApp } = await import('../js/core/drawingApp.js');
  const app = makeApp();
  DrawingApp.prototype.updateButtons.call(app);
  assert.equal(el('fullscreen-toggle').disabled, false, 'fullscreen is available on an empty editor');
  // …and the controls that genuinely need an image are still off, so this is not a
  // blanket "enable everything" regression.
  for (const id of ['zoom-in', 'zoom-out', 'zoom-fit', 'crop-image', 'save-image', 'undo'])
    assert.equal(el(id).disabled, true, `${id} stays disabled without an image`);
  // …and the empty state is published to the stylesheet, which uses it to keep the
  // fullscreen viewport on the page ground instead of the image-viewing black.
  assert.ok(bodyClasses.has('canvas-empty'), 'body.canvas-empty marks the imageless editor');
});

test('an image loaded keeps fullscreen enabled too', async () => {
  const { el, bodyClasses } = makeDom();
  const { DrawingApp } = await import('../js/core/drawingApp.js');
  DrawingApp.prototype.updateButtons.call(makeApp({ image: { width: 10, height: 10 } }));
  assert.equal(el('fullscreen-toggle').disabled, false);
  assert.equal(el('crop-image').disabled, false, 'the image controls come back on');
  assert.ok(!bodyClasses.has('canvas-empty'), 'the cinema-black fullscreen ground comes back with an image');
});

test('the button carries no "load an image" reason to show in its tooltip', async () => {
  const { layout } = await import('../js/ui/layout.js');
  const btn = layout().match(/<button id="fullscreen-toggle"[^>]*>/)?.[0] || '';
  assert.ok(btn, 'the fullscreen button is in the toolbar markup');
  assert.ok(!btn.includes('data-disabled-reason'),
            'a disabled-reason would print a stale "Load an image" line in the tooltip');
});

test('fullscreen on an empty editor keeps the page ground, not the image-viewing black', async () => {
  const { readFileSync } = await import('node:fs');
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  // The empty-editor override must come AFTER the plain fullscreen rule it beats, and
  // after the leave-flight rule too — both match with the same specificity.
  const fs = css.indexOf('body.fullscreen-mode .canvas-viewport {');
  const fsEmpty = css.indexOf('body.fullscreen-mode.canvas-empty .canvas-viewport {');
  const flight = css.indexOf('body:not(.fullscreen-mode) .canvas-viewport.flip-active {');
  const flightEmpty = css.indexOf('body:not(.fullscreen-mode).canvas-empty .canvas-viewport.flip-active {');
  assert.ok(fs >= 0 && fsEmpty > fs, 'the empty-editor fullscreen ground is declared after the black one');
  assert.ok(flight >= 0 && flightEmpty > flight, 'the leave-flight override comes after the black flight rule');
  const bodyOf = (at) => css.slice(at, css.indexOf('}', at));
  assert.match(bodyOf(fsEmpty), /background:\s*var\(--bg-page\)/);
  assert.match(bodyOf(flightEmpty), /background:\s*var\(--bg-page\)/);
  assert.match(bodyOf(fs), /background:\s*#000/, 'an image still gets the black viewing ground');
});
