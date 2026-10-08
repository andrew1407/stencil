// The clean preview (Renderer.previewClean): while on, the picture paints unfiltered with nothing
// over it — no lines, points or compare split — and no field of the app moves; off, the frame the
// app's own settings make comes back. Pure view: an export still reads the applied filter.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx, argsOf } from '../../helpers/recordingCtx.js';
import { PixelCanvas, gradientImage } from '../../helpers/pixelCanvas.js';

globalThis.OffscreenCanvas = PixelCanvas;
const { Renderer } = await import('../../../js/core/draw/renderer.js');

const RESTING = { pointsOf: (l) => l.points, scaleAt: () => 1, paintUnder() {}, paintOver() {} };
const LINE = { points: [{ x: 1, y: 1 }, { x: 4, y: 2 }, { x: 2, y: 3 }], color: '#c81e1e', thickness: 2, style: 'solid' };

const stage = (over = {}) => {
  const app = {
    image: gradientImage(6, 4), imageFilter: 'bw', filterColor: '#7c3aed', canvas: { width: 6, height: 4 },
    lines: [structuredClone(LINE)], showLines: true, showPoints: true, pointSize: 2, strokeFx: RESTING,
    selectedLines: [], selectedLineIdx: -1, coordLineIdx: -1, compareMode: 'vertical', compareSplit: 0.5,
    scale: 1, selGlowColor: '#00ff00', hoverRingColor: '#0000ff', focusRingColor: '#ff0000', ...over,
  };
  const base = recordingCtx(app.canvas);
  app.ctx = base.ctx;
  const overlay = { width: 0, height: 0 };
  const top = recordingCtx(overlay);
  overlay.getContext = () => top.ctx;
  const r = new Renderer(app);
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
  r.redraw();
  take();
  return { app, r, take };
};

const SETTINGS = ['imageFilter', 'filterColor', 'showLines', 'showPoints', 'compareMode', 'compareSplit'];
const snapshot = (app) => Object.fromEntries(SETTINGS.map((k) => [k, app[k]]));

test('on: the unfiltered picture alone — no split, lines, points or divider — and no setting moves', () => {
  const { app, r, take } = stage();
  const before = snapshot(app);
  r.previewClean(true);
  const { base, top } = take();
  assert.deepEqual(argsOf(base, 'drawImage').map((a) => a[0]), [app.image], 'the image itself, once: no filtered copy, no original side');
  assert.ok(base.some(([k, v]) => k === 'set:filter' && v === 'none'));
  assert.ok(!base.some(([k]) => k === 'rect'), 'no split rectangle');
  assert.deepEqual(top.map(([k]) => k), ['clearRect'], 'the overlay is emptied and stays empty');
  assert.deepEqual(snapshot(app), before, 'the app holds what it held');
});

test('every frame while on stays clean, and a second on asks for nothing', () => {
  const { app, r, take } = stage({ compareMode: 'none' });
  r.previewClean(true);
  take();
  r.previewClean(true);
  assert.deepEqual(take(), { base: [], top: [] }, 'already on: no frame');
  app.hoverPt = { lineIdx: 0, ptIdx: 1 };
  r.redraw();
  const frame = take();
  assert.deepEqual(frame.base, [], 'the picture is still the clean one');
  assert.ok(!frame.top.some(([k]) => k === 'stroke' || k === 'arc'), 'no line, point or hover ring');
});

test('off: the applied filter, the split, the lines and the points come back', () => {
  const { app, r, take } = stage();
  r.previewClean(true);
  take();
  r.previewClean(false);
  const { base, top } = take();
  const sources = argsOf(base, 'drawImage').map((a) => a[0]);
  assert.equal(sources.length, 2, 'the filtered base, then the original side');
  assert.notEqual(sources[0], app.image, 'the base is the filtered copy');
  assert.equal(sources[1], app.image);
  assert.ok(argsOf(top, 'stroke').length > 0, 'the lines and the divider');
  assert.ok(argsOf(top, 'arc').length > 0, 'the points and the knob');
});

test('an export reads the applied filter while the preview is on', () => {
  const { app, r } = stage();
  r.previewClean(true);
  assert.notEqual(r.restingBase(), app.image, 'the resting base is still the filtered one');
});
