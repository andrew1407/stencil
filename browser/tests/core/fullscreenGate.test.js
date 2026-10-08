// Fullscreen is available on an EMPTY editor (js/ui/control/state.js updateButtons, js/ui/fullscreen/layer.js
// toggleFullscreen): the empty canvas carries the "＋ Blank image" card and the whole toolbar, which is when
// the extra room is most useful, so no route — the button, Alt+F, the context menu, `stencil.fullscreen` — is
// gated on `app.image`. The other image-dependent controls (zoom, undo/redo, crop…) stay off.

import { test } from 'node:test';
import assert from 'node:assert/strict';

// Every id hands back a fresh stub element, so updateButtons can set .disabled /
// .style.display on anything it reaches for and we can read it back.
import { installDom } from '../helpers/dom.js';
import { COMPONENTS_CSS } from '../helpers/css.js';
import { makeControlApp } from '../helpers/controlStateRig.js';

const makeDom = () => {
  const doc = installDom({ autoCreateById: true });
  return { el: doc.getElementById, bodyClasses: doc.body.classes };
};

const makeApp = makeControlApp;

test('the fullscreen button stays enabled with no image loaded', async () => {
  const { el, bodyClasses } = makeDom();
  const { DrawingApp } = await import('../../js/core/drawingApp.js');
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
  const { DrawingApp } = await import('../../js/core/drawingApp.js');
  DrawingApp.prototype.updateButtons.call(makeApp({ image: { width: 10, height: 10 } }));
  assert.equal(el('fullscreen-toggle').disabled, false);
  assert.equal(el('crop-image').disabled, false, 'the image controls come back on');
  assert.ok(!bodyClasses.has('canvas-empty'), 'the cinema-black fullscreen ground comes back with an image');
});

test('the button carries no "load an image" reason to show in its tooltip', async () => {
  const { layout } = await import('../../js/ui/layout.js');
  const btn = layout().match(/<button id="fullscreen-toggle"[^>]*>/)?.[0] || '';
  assert.ok(btn, 'the fullscreen button is in the toolbar markup');
  assert.ok(!btn.includes('data-disabled-reason'),
            'a disabled-reason would print a stale "Load an image" line in the tooltip');
});

test('fullscreen keeps the themed page ground and a hairline frame, image or not (desktop [fsView])', async () => {
  const css = COMPONENTS_CSS;
  const bodyOf = (at) => css.slice(at, css.indexOf('}', at));
  const fs = css.indexOf('body.fullscreen-mode .canvas-viewport {');
  const flight = css.indexOf('body:not(.fullscreen-mode) .canvas-viewport.flip-active {');
  assert.ok(fs >= 0 && flight >= 0);
  assert.match(bodyOf(fs), /background:\s*var\(--bg-page\)/);
  assert.match(bodyOf(fs), /border:\s*1px solid var\(--border-main\)/);
  assert.match(bodyOf(fs), /border-radius:\s*8px/);
  assert.match(bodyOf(flight), /background:\s*var\(--bg-page\)/, 'the leave-flight shrinks out of that same ground');
  assert.equal(/fullscreen-mode[^{]*\{[^}]*#000/.test(css), false, 'no black ground is left anywhere in fullscreen');
});
