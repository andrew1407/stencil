// BR-2: the picture on #canvas repaints only when the picture changes — image, filter, tint, crop,
// turn, compare mode or split — while every frame repaints the lines overlay alone. A layered
// frame paints exactly what the one-canvas frame painted, split across the two layers.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx, argsOf } from '../../helpers/recordingCtx.js';
import { PixelCanvas, gradientImage } from '../../helpers/pixelCanvas.js';

globalThis.OffscreenCanvas = PixelCanvas;
const { Renderer } = await import('../../../js/core/draw/renderer.js');
const { StageLayers } = await import('../../../js/core/draw/stageLayers.js');

const LINES = [
  { points: [{ x: 1, y: 1 }, { x: 4, y: 2 }, { x: 2, y: 3 }], color: '#c81e1e', thickness: 2, style: 'solid' },
  { points: [{ x: 5, y: 0 }, { x: 5, y: 3 }], color: '#1e63c8', thickness: 1, style: 'dashed' },
];
const RESTING = { pointsOf: (l) => l.points, scaleAt: () => 1, paintUnder() {}, paintOver() {} };

const makeApp = (over = {}) => ({
  image: gradientImage(6, 4), imageFilter: 'none', filterColor: '#7c3aed', canvas: { width: 6, height: 4 },
  lines: LINES.map((l) => ({ ...l, points: l.points.map((p) => ({ ...p })) })),
  showLines: true, showPoints: true, pointSize: 2, strokeFx: RESTING, selectedLines: [], selectedLineIdx: -1,
  coordLineIdx: -1, compareMode: 'none', compareSplit: 0.5, scale: 1, selGlowColor: '#00ff00',
  hoverRingColor: '#0000ff', focusRingColor: '#ff0000', ...over,
});

// The base ctx and the overlay each record into their own list; `take()` empties both.
const layered = (over) => {
  const app = makeApp(over);
  const base = recordingCtx(app.canvas);
  app.ctx = base.ctx;
  const overlay = { width: 0, height: 0 };
  const top = recordingCtx(overlay);
  overlay.getContext = () => top.ctx;
  const r = new Renderer(app);
  // The view attaches its overlay at boot, before any picture is loaded.
  const image = app.image;
  app.image = null;
  r.useOverlay(overlay);
  app.image = image;
  const take = () => {
    const out = { base: [...base.calls], top: [...top.calls] };
    base.calls.length = 0;
    top.calls.length = 0;
    return out;
  };
  take();
  return { app, r, overlay, take };
};

const flat = (over) => {
  const app = makeApp(over);
  const rec = recordingCtx(app.canvas);
  app.ctx = rec.ctx;
  new Renderer(app).redraw();
  return rec.calls;
};

test('a hover-only frame repaints the lines layer and leaves the picture alone', () => {
  const { app, r, take } = layered();
  r.redraw();
  const first = take();
  assert.deepEqual(first.base.map(([k]) => k), ['clearRect', 'drawImage'], 'the picture, once');
  assert.ok(argsOf(first.top, 'stroke').length > 0, 'the lines on the overlay');
  for (let i = 0; i < 3; i++) {
    app.hoverPt = { lineIdx: 0, ptIdx: i };
    r.redraw();
    const frame = take();
    assert.deepEqual(frame.base, [], 'nothing on #canvas');
    assert.equal(frame.top[0][0], 'clearRect', 'the overlay starts empty');
    assert.ok(argsOf(frame.top, 'arc').some((a) => a[2] === app.pointSize + 4), 'the hover ring is drawn');
  }
});

test('the overlay takes #canvas backing size, and a resize empties it', () => {
  const { app, r, overlay, take } = layered();
  r.redraw();
  assert.deepEqual([overlay.width, overlay.height], [6, 4]);
  take();
  r.redraw();
  assert.deepEqual(take().top[0], ['clearRect', 0, 0, 6, 4], 'same size: cleared');
  app.image = gradientImage(3, 2);
  app.canvas.width = 3;
  app.canvas.height = 2;
  r.redraw();
  assert.deepEqual([overlay.width, overlay.height], [3, 2], 'a crop resizes it');
});

const CHANGES = [
  ['a filter', (app) => { app.imageFilter = 'bw'; }],
  ['a duotone tint', (app) => { app.imageFilter = 'custom'; }, (app) => { app.filterColor = '#10b981'; }],
  ['a crop or turn (a new image)', (app) => { app.image = gradientImage(6, 4); }],
  ['a compare mode', (app) => { app.compareMode = 'vertical'; }],
  ['the split moving', (app) => { app.compareMode = 'horizontal'; }, (app) => { app.compareSplit = 0.25; }],
  ['the original hold', (app) => { app.compareHoldOriginal = true; }],
];
for (const [what, ...steps] of CHANGES) {
  test(`${what} repaints the picture once, and the next frame does not`, () => {
    const { app, r, take } = layered();
    r.redraw();
    for (const step of steps) {
      take();
      step(app);
      r.redraw();
      assert.ok(take().base.some(([k]) => k === 'drawImage'), `${what}: repainted`);
    }
    r.redraw();
    assert.deepEqual(take().base, [], `${what}: once`);
  });
}

test('zoom repaints only the overlay: the divider keeps its on-screen size', () => {
  const { app, r, take } = layered({ compareMode: 'vertical' });
  r.redraw();
  take();
  app.scale = 2;
  r.redraw();
  const frame = take();
  assert.deepEqual(frame.base, [], 'zoom is CSS — the picture is not repainted');
  assert.ok(frame.top.some(([k, v]) => k === 'set:lineWidth' && v === 1), 'the 2px bar at 2x is 1 image px');
});

test('a split: the original side on #canvas, the lines erased from it on the overlay, the divider over them', () => {
  const { app, r, take } = layered({ compareMode: 'vertical', compareSplit: 0.5 });
  r.redraw();
  const { base, top } = take();
  assert.deepEqual(argsOf(base, 'rect'), [[0, 0, 3, 4]], 'the original side');
  assert.deepEqual(argsOf(base, 'drawImage').map((a) => a[0]), [app.image, app.image]);
  assert.ok(!top.some(([k]) => k === 'clip'), 'the lines are never clipped: a clip rasterises strokes differently');
  const erase = top.findIndex(([k, v]) => k === 'set:globalCompositeOperation' && v === 'destination-out');
  const keys = top.map(([k]) => k);
  assert.ok(erase > keys.indexOf('stroke'), 'erased after the lines are down');
  assert.deepEqual(top.slice(erase + 1, erase + 5), [['set:fillStyle', '#000'], ['beginPath'], ['rect', 0, 0, 3, 4], ['fill']]);
  assert.ok(keys.lastIndexOf('stroke') > erase, 'the divider paints over the erased side');
  assert.ok(!top.some(([k, v]) => k === 'set:shadowBlur' && v === 12), 'no focus ring on the read-only side');
});

test('the original compare view: the picture is the original and the overlay stays empty', () => {
  const { app, r, take } = layered({ compareMode: 'original', imageFilter: 'bw' });
  r.redraw();
  const { base, top } = take();
  assert.deepEqual(argsOf(base, 'drawImage').map((a) => a[0]), [app.image], 'unfiltered');
  assert.deepEqual(top.map(([k]) => k), [], 'the first frame sized the overlay: nothing drawn');
});

test('an emptied editor empties the overlay and forgets the picture it held', () => {
  const { app, r, overlay, take } = layered();
  r.redraw();
  const image = app.image;
  app.image = null;
  app.canvas.width = 0;
  app.canvas.height = 0;
  r.redraw();
  assert.deepEqual([overlay.width, overlay.height], [0, 0]);
  take();
  app.image = image;
  app.canvas.width = 6;
  app.canvas.height = 4;
  r.redraw();
  assert.ok(take().base.some(([k]) => k === 'drawImage'), 'the same picture is painted again');
});

test('attaching the overlay over a picture repaints it without its lines at once', () => {
  const app = makeApp();
  const base = recordingCtx(app.canvas);
  app.ctx = base.ctx;
  const r = new Renderer(app);
  r.redraw();
  base.calls.length = 0;
  const top = recordingCtx();
  r.useOverlay({ width: 0, height: 0, getContext: () => top.ctx });
  assert.deepEqual(base.calls.map(([k]) => k), ['clearRect', 'drawImage'], 'the picture alone');
  assert.ok(argsOf(top.calls, 'stroke').length > 0, 'the lines on the overlay');
});

test('layers(): the picture, then the overlay — and #canvas alone before one is attached', () => {
  const app = makeApp();
  const r = new Renderer(app);
  assert.deepEqual(r.layers(), [app.canvas]);
  const overlay = { width: 0, height: 0, getContext: () => recordingCtx().ctx };
  app.image = null;
  r.useOverlay(overlay);
  assert.deepEqual(r.layers(), [app.canvas, overlay]);
  r.useOverlay({ getContext: () => null });
  assert.deepEqual(r.layers(), [app.canvas], 'a canvas with no 2D context leaves the stage flat');
});

// The first frame sizes the overlay (no clearRect), so its calls start with the lines.
for (const compareMode of ['none', 'vertical', 'horizontal']) {
  test(`${compareMode}: the layered frame paints exactly the one-canvas frame, split across two layers`, () => {
    const over = { compareMode, selectedLineIdx: 0, selectedLines: [0], coordLineIdx: 0, focusedPtIdx: 1 };
    const one = flat(over);
    const { r, take } = layered(over);
    r.redraw();
    const { base, top } = take();
    if (compareMode === 'none') {
      assert.deepEqual([...base, ...top], one);
      return;
    }
    // The overlay: annotations, then the six-call erase (save … restore), then the divider.
    const erase = top.findIndex(([k, v]) => k === 'set:globalCompositeOperation' && v === 'destination-out') - 1;
    const annotations = top.slice(0, erase);
    const divider = top.slice(erase + 7);
    const original = base.slice(2);
    assert.deepEqual([...base.slice(0, 2), ...annotations, ...original, ...divider], one,
      'picture, annotations, original side, divider — the old order, on two canvases');
  });
}

test('StageLayers: unlayered, every key is stale; layered, only a new one', () => {
  const s = new StageLayers();
  assert.equal(s.stale([1]), true);
  assert.equal(s.stale([1]), true, 'no overlay: #canvas repaints every frame');
  assert.equal(s.clear({ width: 1, height: 1 }), false);
  s.attach({ width: 0, height: 0, getContext: () => recordingCtx().ctx });
  assert.equal(s.stale([1, 'a']), true);
  assert.equal(s.stale([1, 'a']), false);
  assert.equal(s.stale([1, 'b']), true);
  s.invalidate();
  assert.equal(s.stale([1, 'b']), true, 'invalidate forgets the picture');
});
