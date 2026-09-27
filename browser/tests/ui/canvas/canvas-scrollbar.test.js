import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { scrollbarHit } from '../../../js/utils.js';
import { thumbMetrics, SB_MIN_THUMB_PX, wireCanvasScrollbars } from '../../../js/ui/canvas/scrollbars.js';
import { LAYOUT_CSS } from '../../helpers/css.js';
import { createStubElement, installDom } from '../../helpers/dom.js';
import { installMemoryStorage } from '../../helpers/memoryStorage.js';

installMemoryStorage();
const stub = (id = '') => createStubElement('div', { id, dataset: { csEnhanced: '1' }, querySelector: () => stub() });
const doc = installDom({}, {
  window: { addEventListener() {}, dispatchEvent() {}, matchMedia: () => ({ matches: false, addEventListener() {} }) },
  location: { hash: '', search: '', pathname: '/' }, history: { replaceState() {} },
});
doc.getElementById = (id) => doc.els.get(id) ?? doc.els.set(id, stub(id)).get(id);
doc.querySelectorAll = () => [];
// A viewport 400×300 at (100, 50), holding `content` px of picture.
const viewport = (content = { w: 400, h: 300 }) => createStubElement('div', {
  clientWidth: 400, clientHeight: 300, clientLeft: 0, clientTop: 0, scrollTop: 0, scrollLeft: 0,
  get scrollWidth() { return content.w; }, get scrollHeight() { return content.h; },
  getBoundingClientRect: () => ({ left: 100, top: 50, right: 500, bottom: 350, width: 400, height: 300 }),
});
// wireControls runs every group against an app that answers anything, so only the DOM wiring is observed.
const anyApp = () => new Proxy(() => {}, {
  get: (t, k) => (k === 'then' ? undefined : k === Symbol.toPrimitive ? () => 0 : anyApp()), apply: () => anyApp(),
});

// The canvas viewport draws its own overlay scrollbars (js/ui/canvas/scrollbars.js, the
// desktop's OverlayScrollArea.hpp): the native bars cannot colour ONE thumb on hover —
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

test('the canvas hides its native bars and styles its own, accent only on the hovered bar', async () => {
  const css = LAYOUT_CSS;
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
  // The bars live on the body: pan.js's belowInColumn sums the viewport's following
  // siblings' heights, and a bar beside it collapsed the viewport on every zoom.
  const vp = viewport();
  const parent = createStubElement('div');
  parent.appendChild(vp);
  const { bars } = wireCanvasScrollbars(vp);
  for (const { bar } of Object.values(bars)) {
    assert.equal(bar.parentNode, doc.body, 'the bars are appended to the body');
    assert.ok(!parent.children.includes(bar), 'never beside the viewport');
  }
  const { wireControls } = await import('../../../js/ui/bindings/index.js');
  wireControls(anyApp());
  assert.ok(doc.getElementById('canvas-viewport').__canvasSb, 'the canvas is wired');
  const theme = readFileSync(new URL('../../../css/theme.css', import.meta.url), 'utf8');
  assert.strictEqual((theme.match(/--sb-thumb-hover:\s*var\(--accent\);/g) || []).length, 2, 'hover IS the accent, both themes');
});

test('the other panels keep the native thin bars, accent only with the pointer on the bar strip', async () => {
  const box = { left: 100, top: 50, right: 700, bottom: 450 };
  assert.strictEqual(scrollbarHit(box, 690, 200, { canY: true }), true);
  assert.strictEqual(scrollbarHit(box, 690, 200, { canY: false }), false);
  assert.strictEqual(scrollbarHit(box, 400, 200, { canY: true }), false, 'the middle of the panel is not the bar');
  assert.strictEqual(scrollbarHit(box, 400, 440, { canX: true }), true);
  assert.strictEqual(scrollbarHit(box, 800, 440, { canX: true }), false, 'outside the box is never a hit');
  const css = LAYOUT_CSS;
  // App-wide: every scrollable gets the thin grey bar, and the accent only via .sb-hover.
  assert.match(css, /\n\* \{\s*scrollbar-width: thin;\s*scrollbar-color: var\(--sb-thumb\) transparent;\s*\}/);
  assert.match(css, /\.sb-hover \{ scrollbar-color: var\(--sb-thumb-hover\) transparent; \}/);
  // One document-level wiring, no per-panel list: a panel nobody registered still lights up.
  const { wireControls } = await import('../../../js/ui/bindings/index.js');
  wireControls(anyApp());
  globalThis.getComputedStyle = () => ({ scrollbarWidth: 'thin', overflowY: 'auto', overflowX: 'hidden' });
  const panel = createStubElement('div', {
    nodeType: 1, scrollHeight: 900, clientHeight: 400, scrollWidth: 600, clientWidth: 600, getBoundingClientRect: () => box,
  });
  const move = (x, y) => (doc.listeners.mousemove || []).forEach((fn) => {
    try { fn({ target: panel, clientX: x, clientY: y }); } catch { /* another group's handler, fed a bare event */ }
  });
  move(690, 200);
  assert.ok(panel.classList.contains('sb-hover'), 'the pointer on its bar strip accents the thumb');
  move(400, 200);
  assert.ok(!panel.classList.contains('sb-hover'), 'and off it, the thumb is grey again');
  delete globalThis.getComputedStyle;
});

// A panel drag shrinks the viewport; on that very frame its children still measure at the old
// width, so a bar decided there showed over an empty canvas and stayed. The observer's verdict
// is taken once the layout has settled instead.
test('an empty canvas never offers a bar, whatever size the element still carries', () => {
  const vp = viewport({ w: 900, h: 700 });
  const sb = wireCanvasScrollbars(vp);
  assert.deepEqual(sb.layout(), { canY: true, canX: true }, 'a picture larger than the frame offers both');
  doc.body.classList.add('canvas-empty');
  try {
    assert.deepEqual(sb.layout(), { canY: false, canX: false }, 'no picture: the stale canvas size is not content');
    assert.ok(sb.bars.x.bar.hidden && sb.bars.y.bar.hidden);
  } finally {
    doc.body.classList.remove('canvas-empty');
  }
});

// The viewport's own box only re-places the bars: a fold overflows for a few frames and popped them up
// mid-flight, then left them over the rows below (user report). The picture's box — a zoom — shows them.
const rig = (content, canvas = createStubElement('canvas')) => {
  const frames = [];
  const observers = new Map();
  globalThis.requestAnimationFrame = (fn) => frames.push(fn) && frames.length;
  globalThis.cancelAnimationFrame = () => {};
  globalThis.ResizeObserver = class { constructor(cb) { this.cb = cb; } observe(el) { observers.set(el, this.cb); } };
  const vp = Object.assign(viewport(content), { querySelector: (sel) => (sel === '#canvas' ? canvas : null) });
  const api = wireCanvasScrollbars(vp);
  const run = () => { const due = frames.splice(0); due.forEach((fn) => fn()); };
  return { api, vp, canvas, frames, run, resized: (el) => observers.get(el)(),
           on: () => api.bars.y.bar.classList.contains('canvas-sb-on') };
};
const unrig = () => {
  for (const k of ['requestAnimationFrame', 'cancelAnimationFrame', 'ResizeObserver']) delete globalThis[k];
};

test('a viewport resize re-places the bars but never pops them up', () => {
  try {
    const r = rig({ w: 400, h: 900 });
    r.resized(r.vp);
    r.run();
    r.run();
    assert.equal(r.api.bars.y.bar.hidden, false, 'laid out against the overflow');
    assert.equal(r.on(), false, 'yet not shown: a fold\'s passing overflow is not a scroll');
  } finally { unrig(); }
});

test('a zoom (the picture resized) shows the bars once the layout has settled', () => {
  try {
    const r = rig({ w: 400, h: 900 });
    r.resized(r.canvas);
    assert.equal(r.on(), false, 'the resize frame itself decides nothing');
    r.run();
    assert.equal(r.on(), false, 'nor does the next one');
    r.run();
    assert.equal(r.on(), true, 'the second frame, with the layout settled, reveals the bar');
  } finally { unrig(); }
});

test('shown bars follow the viewport every frame and go the moment nothing overflows', () => {
  try {
    const r = rig({ w: 400, h: 900 });
    r.api.reveal();
    assert.equal(r.on(), true);
    assert.equal(r.frames.length, 1, 'a frame re-measures them while shown');
    r.run();
    assert.equal(r.on(), true, 'still overflowing: still shown, measured again next frame');
    r.vp.scrollHeight = 300;
    r.run();
    assert.equal(r.on(), false, 'the overflow is gone, so are the bars');
    assert.equal(r.api.bars.y.bar.hidden, true);
  } finally { unrig(); }
});
