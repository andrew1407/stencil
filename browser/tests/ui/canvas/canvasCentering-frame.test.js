// The canvas frame under a zoom: the Ctrl+wheel loop keeps the cursor's pixel put through the
// centring margins and never resizes the frame, only syncViewportHeight sizes it, and leaving
// fullscreen hands the box back through it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installViewport } from '../../helpers/zoomViewportRig.js';
import { installDom } from '../../helpers/dom.js';

// Every scroll offset the code writes, before the clamp, with the scale on screen at the time.
const recordScrollWrites = (vp, app) => {
  const writes = [];
  for (const axis of ['scrollLeft', 'scrollTop']) {
    const d = Object.getOwnPropertyDescriptor(vp, axis);
    Object.defineProperty(vp, axis, {
      get: d.get, set(v) { writes.push({ axis, v, s: app.scale }); d.set.call(this, v); },
    });
  }
  return writes;
};

// controlsBinder's Ctrl+wheel zoom writes the scroll offset itself, frame by frame, so it has to
// include the centring margin: in a full-height frame it is real once a zoom crosses into overflow.
test('the wheel zoom pins the cursor through the centring margins, at every frame', async () => {
  const { wireSmoothZoom } = await import('../../../js/ui/bindings/viewport/smoothZoom.js');
  const { app, vp, zp } = installViewport({ vpH: 700, imgW: 600, imgH: 200 });
  const frames = [];
  globalThis.requestAnimationFrame = (cb) => frames.push(cb);
  const writes = recordScrollWrites(vp, app);
  let wheel = null;
  globalThis.document.addEventListener = (type, fn) => { if (type === 'wheel') wheel = fn; };
  Object.assign(vp, { contains: () => true, getBoundingClientRect: () => ({ left: 0, top: 0 }) });
  Object.assign(app, { zoomPan: zp, compareReadOnly: () => false, selectedIndices: () => [] });
  wireSmoothZoom(app);
  // Image pixel (300, 100) sits under the cursor at (500, 350): origin (200, 250) at scale 1.
  const pixel = { scrollLeft: 300, scrollTop: 100 };
  const cursor = { scrollLeft: 500, scrollTop: 350 };
  for (let i = 0; i < 3; i++) {
    wheel({ ctrlKey: true, deltaY: -80, deltaMode: 0, clientX: 500, clientY: 350, target: vp, preventDefault() {} });
  }
  while (frames.length) frames.shift()(0);
  // Each notch caps at 0.32, then scales with the zoom above 100% (core zoomStepAt).
  const want = [0, 0, 0].reduce((s) => s + 0.32 * Math.max(1, s), 1);
  assert.ok(Math.abs(app.scale - want) < 1e-9, `three capped notches land at ${want}, got ${app.scale}`);
  assert.ok(writes.length >= 6, 'the loop wrote the scroll on its frames and at the snap');
  for (const { axis, v, s } of writes) {
    const org = zp.originAt(s)[axis === 'scrollLeft' ? 'x' : 'y'];
    assert.ok(Math.abs(org + pixel[axis] * s - v - cursor[axis]) < 1e-6,
      `${axis} at ${s.toFixed(3)} keeps the pixel under the cursor through the margin ${org}`);
  }
  assert.ok(!('maxHeight' in vp.style) && !('height' in vp.style), 'and it never resizes the frame per frame');
});

// Sizing is syncViewportHeight's job alone (utils/viewportMetrics.js): a zoom that sized the
// frame to the picture would then read clientHeight out of the box it just changed.
test('only syncViewportHeight sizes the frame, to the room and never to the picture', () => {
  const { vp, zp } = installViewport({ imgW: 600, imgH: 200 });
  globalThis.document.body.classList.contains = () => false;
  Object.assign(vp, { getBoundingClientRect: () => ({ top: 100 }), closest: () => null });
  const sized = [];
  for (const prop of ['maxHeight', 'minHeight', 'height']) {
    Object.defineProperty(vp.style, prop, { set(v) { sized.push(`${prop}=${v}`); }, get: () => '' });
  }
  for (const s of [0.25, 1, 3]) {
    zp.setZoom(s, false);
    zp.zoomAroundCenter(s * 1.5);
    zp.zoomToImagePoint(s, 10, 10);
  }
  assert.ok(sized.length >= 3, 'every zoom re-measures the room');
  // 700 tall window, the frame 100px down, 96px of fallback chrome below it.
  assert.deepEqual([...new Set(sized)], ['maxHeight=504px'], 'one height, whatever the zoom');
});

// Fullscreen owns the box while it is on (components.css pins it to the window) and hands
// it back on the way out — through the same rule, not a height of its own.
test('leaving fullscreen restores the frame through syncViewportHeight, then again once landed', async () => {
  const { StencilFullscreenLayer } = await import('../../../js/ui/fullscreen/layer.js');
  const { FLIP_MS } = await import('../../../js/ui/motion.js');
  const doc = installDom({}, {
    window: { innerWidth: 1000, innerHeight: 700, addEventListener() {} },
    requestAnimationFrame: () => 1,
  });
  for (const id of ['fs-controls-panel', 'fs-points-panel', 'fs-top-trigger', 'fs-right-trigger',
    'fullscreen-toggle', 'canvas-viewport']) doc.register(id, doc.createElement('div'));
  const timers = [];
  const realSetTimeout = globalThis.setTimeout;
  globalThis.setTimeout = (fn, ms) => timers.push({ fn, ms });
  try {
    let syncs = 0;
    const app = { image: null, zoomPan: { syncViewportHeight: () => { syncs++; } } };
    new StencilFullscreenLayer().wire(app);
    app.toggleFullscreen();
    assert.equal(syncs, 0, 'entering hands the box to the fullscreen rule');
    assert.equal(doc.getElementById('canvas-viewport').style.maxHeight, '', 'and drops the in-flow height');
    app.toggleFullscreen();
    assert.equal(syncs, 1, 'the exit path re-measures');
    // The exit flight is animated: measured as it starts, the toolbar rows are still coming
    // back and the frame lands ~60px too tall (a permanent page scrollbar).
    const landed = timers.filter((t) => t.ms > FLIP_MS);
    assert.equal(landed.length, 1, 'one re-measure is armed past the flight');
    landed[0].fn();
    assert.equal(syncs, 2, '…and again once the flight has landed');
  } finally {
    globalThis.setTimeout = realSetTimeout;
    doc.restore();
  }
});

// Stacked (animations/responsive.css) the page scrolls and the panel sits below the frame: sized to what the window
// had left under a phone's toolbar, the frame was a 50px strip hiding the "+ Blank image" card (user report).
test('stacked, the frame and the panel each hold at least half the window, measured from the document', () => {
  const { vp, zp } = installViewport({ vpW: 400, vpH: 800 });
  const priorMM = globalThis.matchMedia;
  globalThis.matchMedia = (q) => ({ matches: q === '(max-width: 960px)' });
  globalThis.document.body.classList.contains = () => false;
  const panel = { style: { setProperty(k, v) { this[k] = v; } }, getBoundingClientRect: () => ({ top: 900 }), parentElement: null };
  const byId = globalThis.document.getElementById;
  globalThis.document.getElementById = (id) => (id === 'coord-panel' ? panel : byId(id));
  try {
    // Scrolled 300px down with the frame's top 100px into the window: the room is measured from 400px of document.
    globalThis.window = { innerWidth: 400, innerHeight: 800, scrollY: 300 };
    Object.assign(vp, { getBoundingClientRect: () => ({ top: 100 }), closest: () => null });
    zp.syncViewportHeight();
    assert.equal(vp.style.maxHeight, '400px', 'under a tall toolbar (304px left) the floor is half the window');
    assert.equal(vp.style.minHeight, '400px', 'held as a floor too: the column no longer fills the page');
    zp.syncCoordPanelHeight();
    assert.equal(panel.style.maxHeight, '400px', 'the panel below the fold takes its half as well, scrolling inside');
    // A short toolbar at the top of the page: the room to the window's bottom, as the wide layout measures it.
    globalThis.window = { innerWidth: 400, innerHeight: 800, scrollY: 0 };
    vp.getBoundingClientRect = () => ({ top: 50 });
    zp.syncViewportHeight();
    assert.equal(vp.style.maxHeight, '654px');
    assert.equal(vp.style.minHeight, '654px');
    // Widened again: the floor goes, the cap stays.
    globalThis.matchMedia = () => ({ matches: false });
    zp.syncViewportHeight();
    assert.equal(vp.style.maxHeight, '654px');
    assert.equal(vp.style.minHeight, '');
  } finally { globalThis.matchMedia = priorMM; globalThis.document.getElementById = byId; }
});
