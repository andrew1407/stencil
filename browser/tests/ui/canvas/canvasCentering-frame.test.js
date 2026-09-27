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
  assert.ok(Math.abs(app.scale - 1.96) < 1e-9, `three capped notches land at 1.96, got ${app.scale}`);
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
