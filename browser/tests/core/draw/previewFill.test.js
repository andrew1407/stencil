// A blank recolour on trial (Renderer.previewFill): the picture paints as a solid fill, filtered as
// the base would paint that fill, over the image it was tried on only; a hover frame leaves it be,
// and ending the trial or replacing the image puts the base back. No reload, no history.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { recordingCtx } from '../../helpers/recordingCtx.js';
import { PixelCanvas } from '../../helpers/pixelCanvas.js';

globalThis.OffscreenCanvas = PixelCanvas;
const { Renderer } = await import('../../../js/core/draw/renderer.js');
const { filteredFill, ImageFilterCanvas } = await import('../../../js/core/image/filterCanvas.js');

const solid = (w, h, hex) => {
  const c = new PixelCanvas(w, h);
  const ctx = c.getContext();
  ctx.fillStyle = hex;
  ctx.fillRect();
  return c;
};

test('the trial colour is what each filter paints over a whole fill of it', () => {
  assert.equal(filteredFill('#336699', 'none', null), '#336699');
  for (const filter of ['bw', 'sepia', 'invert', 'contour', 'custom']) {
    const tint = filter === 'custom' ? '#10b981' : null;
    const [r, g, b] = new ImageFilterCanvas().canvasFor(solid(5, 5, '#336699'), filter, tint).getContext().getImageData(2, 2, 1, 1).data;
    assert.equal(filteredFill('#336699', filter, tint), `rgb(${r}, ${g}, ${b})`, filter);
  }
});

const stage = () => {
  const app = { image: solid(6, 4, '#ffffff'), imageFilter: 'none', canvas: { width: 6, height: 4 }, lines: [],
    showLines: true, compareMode: 'none', strokeFx: { pointsOf: (l) => l.points } };
  const base = recordingCtx(app.canvas);
  app.ctx = base.ctx;
  const overlay = { width: 0, height: 0, getContext: () => recordingCtx(overlay).ctx };
  const r = new Renderer(app);
  const image = app.image;
  app.image = null;
  r.useOverlay(overlay);
  app.image = image;
  const take = () => base.calls.splice(0).map(([k, v]) => (k === 'set:fillStyle' ? `fill ${v}` : k));
  return { app, r, take };
};

test('a trial paints the fill once, a hover frame leaves it, and its end or a new image restores the base', () => {
  const { app, r, take } = stage();
  r.redraw();
  assert.deepEqual(take(), ['clearRect', 'drawImage']);
  r.previewFill('#ff0000');   // without rAF the frame it asks for paints straight through
  assert.deepEqual(take(), ['clearRect', 'set:filter', 'fill #ff0000', 'fillRect']);
  r.redraw();
  assert.deepEqual(take(), [], 'an overlay-only frame keeps the trial on #canvas');
  r.previewFill(null);
  assert.deepEqual(take(), ['clearRect', 'drawImage'], 'the trial ended: the blank again');
  r.previewFill('#00ff00');
  take();
  app.image = solid(6, 4, '#00ff00');
  r.redraw();
  assert.deepEqual(take(), ['clearRect', 'drawImage'], 'the recoloured image replaces the trial');
});

test('under a filter the trial paints the filtered fill', () => {
  const { app, r, take } = stage();
  app.imageFilter = 'bw';
  r.previewFill('#336699');
  assert.ok(take().includes(`fill ${filteredFill('#336699', 'bw', null)}`));
});
