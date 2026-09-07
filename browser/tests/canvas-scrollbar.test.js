import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { scrollbarHit } from '../js/utils.js';
import { thumbMetrics, SB_MIN_THUMB_PX } from '../js/ui/canvasScrollbars.js';

// The canvas viewport draws its own overlay scrollbars (js/ui/canvasScrollbars.js, the
// desktop's overlayScrollArea.hpp): the native bars cannot colour ONE thumb on hover —
// scrollbar-color is a single colour for both — and the webkit pseudo-elements are
// ignored once scrollbar-width is set (user reports).

test('thumbMetrics: proportional length with a floor, offset scaled over the free track', () => {
  assert.strictEqual(thumbMetrics(400, 400, 0, 400), null, 'no overflow ⇒ no thumb');
  assert.strictEqual(thumbMetrics(400, 800, 0, 0), null, 'no track ⇒ no thumb');
  assert.deepStrictEqual(thumbMetrics(400, 800, 0, 400), { len: 200, pos: 0 });
  assert.deepStrictEqual(thumbMetrics(400, 800, 400, 400), { len: 200, pos: 200 }, 'scrolled to the end');
  assert.deepStrictEqual(thumbMetrics(400, 800, 200, 400), { len: 200, pos: 100 });
  assert.strictEqual(thumbMetrics(100, 100000, 0, 400).len, SB_MIN_THUMB_PX, 'never thinner than the floor');
  assert.deepStrictEqual(thumbMetrics(400, 800, 5000, 400), { len: 200, pos: 200 }, 'offset is clamped');
});

test('the canvas hides its native bars and styles its own, accent only on the hovered bar', () => {
  const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
  assert.match(css, /\.canvas-viewport \{\s*overflow: auto;\s*scrollbar-width: none;/, 'native bars off (standard)');
  assert.ok(css.includes('.canvas-viewport::-webkit-scrollbar { display: none; width: 0; height: 0; }'), 'native bars off (webkit)');
  assert.ok(!/\.canvas-viewport[:.][^{]*scrollbar-color/.test(css), 'no native colouring left on the canvas');
  // Per-bar hover: the vertical rule keys on the vertical bar's own :hover, the horizontal on its own.
  assert.ok(css.includes('.canvas-sb-y:hover .canvas-sb-thumb, .canvas-sb-y.canvas-sb-drag .canvas-sb-thumb {\n    width: 9px; right: 1.5px; background: var(--sb-thumb-hover);'));
  assert.ok(css.includes('.canvas-sb-x:hover .canvas-sb-thumb, .canvas-sb-x.canvas-sb-drag .canvas-sb-thumb {\n    height: 9px; bottom: 1.5px; background: var(--sb-thumb-hover);'));
  // Thin (6px) pills at rest, the thumb grey.
  assert.ok(css.includes('.canvas-sb-y .canvas-sb-thumb { width: 6px; right: 3px; }'));
  assert.ok(css.includes('.canvas-sb-thumb {\n    position: absolute;\n    background: var(--sb-thumb);\n    border-radius: 999px;'));
  // Faded bars let the pointer through (desktop WA_TransparentForMouseEvents parity).
  assert.ok(css.includes('.canvas-sb.canvas-sb-on { opacity: 1; pointer-events: auto; }'));
  // The bars live on the body: zoomPan.js's belowInColumn sums the viewport's following
  // siblings' heights, and a bar beside it collapsed the viewport on every zoom.
  const mod = readFileSync(new URL('../js/ui/canvasScrollbars.js', import.meta.url), 'utf8');
  assert.ok(mod.includes('const host = document.body;'), 'the bars are appended to the body');
  const app = readFileSync(new URL('../js/core/drawingApp.js', import.meta.url), 'utf8');
  assert.ok(app.includes("wireCanvasScrollbars(document.getElementById('canvas-viewport'));"), 'the canvas is wired');
  const theme = readFileSync(new URL('../css/theme.css', import.meta.url), 'utf8');
  assert.strictEqual((theme.match(/--sb-thumb-hover:\s*var\(--accent\);/g) || []).length, 2, 'hover IS the accent, both themes');
});

test('the other panels keep the native thin bars, accent only with the pointer on the bar strip', () => {
  const box = { left: 100, top: 50, right: 700, bottom: 450 };
  assert.strictEqual(scrollbarHit(box, 690, 200, { canY: true }), true);
  assert.strictEqual(scrollbarHit(box, 690, 200, { canY: false }), false);
  assert.strictEqual(scrollbarHit(box, 400, 200, { canY: true }), false, 'the middle of the panel is not the bar');
  assert.strictEqual(scrollbarHit(box, 400, 440, { canX: true }), true);
  assert.strictEqual(scrollbarHit(box, 800, 440, { canX: true }), false, 'outside the box is never a hit');
  const css = readFileSync(new URL('../css/layout.css', import.meta.url), 'utf8');
  // App-wide: every scrollable gets the thin grey bar, and the accent only via .sb-hover.
  assert.match(css, /\n\* \{\s*scrollbar-width: thin;\s*scrollbar-color: var\(--sb-thumb\) transparent;\s*\}/);
  assert.match(css, /\.sb-hover \{ scrollbar-color: var\(--sb-thumb-hover\) transparent; \}/);
  const app = readFileSync(new URL('../js/core/drawingApp.js', import.meta.url), 'utf8');
  assert.ok(app.includes('wireScrollbarHover();'), 'one document-level wiring, no per-panel list');
});
