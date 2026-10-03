// The marks overlay is backed at the zoom × devicePixelRatio, so a 16×11 picture at 3200% paints
// its points at screen resolution (desktop drawLineScaled), drawing in image px through a scale.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx } from '../../helpers/recordingCtx.js';
import { PixelCanvas, gradientImage } from '../../helpers/pixelCanvas.js';

globalThis.OffscreenCanvas = PixelCanvas;
const { Renderer } = await import('../../../js/core/draw/renderer.js');

const RESTING = { pointsOf: (l) => l.points, scaleAt: () => 1, paintUnder() {}, paintOver() {} };
const stage = (scale) => {
  const app = {
    image: gradientImage(16, 11), imageFilter: 'none', canvas: { width: 16, height: 11 },
    lines: [{ points: [{ x: 4, y: 4 }], color: '#ff0', thickness: 2, style: 'solid' }],
    showLines: true, showPoints: true, pointSize: 4, strokeFx: RESTING, selectedLines: [], selectedLineIdx: -1,
    coordLineIdx: -1, compareMode: 'none', scale,
  };
  app.ctx = recordingCtx(app.canvas).ctx;
  const overlay = { width: 0, height: 0 };
  const top = recordingCtx(overlay);
  overlay.getContext = () => top.ctx;
  const r = new Renderer(app);
  r.useOverlay(overlay);
  return { app, r, overlay, calls: top.calls };
};

test('zoomed in, the overlay is backed at the zoom and draws through the matching scale', () => {
  const { r, overlay, calls } = stage(32);
  r.redraw();
  assert.deepEqual([overlay.width, overlay.height], [512, 352]);
  assert.ok(calls.some((c) => c[0] === 'setTransform' && c[1] === 32 && c[4] === 32));
});

test('devicePixelRatio multiplies the density, and a zoom change rebacks it', () => {
  globalThis.devicePixelRatio = 2;
  try {
    const { app, r, overlay } = stage(4);
    r.redraw();
    assert.deepEqual([overlay.width, overlay.height], [128, 88]);
    app.scale = 1;
    r.redraw();
    assert.deepEqual([overlay.width, overlay.height], [32, 22]);
  } finally {
    delete globalThis.devicePixelRatio;
  }
});

test('a huge picture zoomed in is capped at 4096² backing pixels', () => {
  const { app, r, overlay } = stage(8);
  app.canvas.width = 4000;
  app.canvas.height = 3000;
  r.redraw();
  assert.ok(overlay.width * overlay.height <= 4096 * 4096 * 1.01);
});
