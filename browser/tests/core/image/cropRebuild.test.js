// The crop view is drawn in ONE pass: the quarter turn and the crop offset are one transform
// onto the crop-sized canvas. Every source pixel must land where rotating the whole original
// and then cutting the crop out would put it, on whole pixels, so the picture is unchanged.
import { test } from 'node:test';
import assert from 'node:assert/strict';

let canvases = [];
const recorder = (canvas) => {
  let m = [1, 0, 0, 1, 0, 0];   // a b c d e f
  const mul = ([a, b, c, d, e, f]) => {
    const [A, B, C, D, E, F] = m;
    m = [A * a + C * b, B * a + D * b, A * c + C * d, B * c + D * d, A * e + C * f + E, B * e + D * f + F];
  };
  return {
    canvas, draws: [],
    translate(x, y) { mul([1, 0, 0, 1, x, y]); },
    rotate(t) { const c = Math.cos(t), s = Math.sin(t); mul([c, s, -s, c, 0, 0]); },
    drawImage(...args) { this.draws.push({ m: [...m], args }); },
  };
};
globalThis.document = {
  getElementById: () => null,
  createElement: () => {
    const c = { width: 0, height: 0 };
    c.ctx = recorder(c);
    c.getContext = () => c.ctx;
    canvases.push(c);
    return c;
  },
};
const { ImageModel } = await import('../../../js/core/image/model.js');

const IW = 5, IH = 3;
// Clockwise quarter turns of pixel (x, y) in an IW×IH image, on whole pixels.
const turn = (x, y, q) => {
  let [px, py, w, h] = [x, y, IW, IH];
  for (let i = 0; i < q; i++) [px, py, w, h] = [h - 1 - py, px, h, w];
  return [px, py];
};

for (const q of [0, 1, 2, 3]) {
  test(`quarter turn ${q}: one canvas, every pixel where rotate-then-crop puts it`, () => {
    canvases = [];
    const swap = q % 2 === 1;
    const cropRect = swap ? { x: 1, y: 2, width: 2, height: 3 } : { x: 2, y: 1, width: 3, height: 2 };
    const app = { originalImage: { width: IW, height: IH }, rotationQuarters: q, cropRect, canvas: {} };
    new ImageModel(app).rebuildCroppedImage();
    assert.equal(canvases.length, 1, 'no full-size rotated intermediate');
    assert.equal(app.image, canvases[0]);
    assert.deepEqual([app.image.width, app.image.height], [cropRect.width, cropRect.height]);
    const [{ m, args }] = app.image.ctx.draws;
    if (q === 0) {
      assert.deepEqual(args.slice(1), [2, 1, 3, 2, 0, 0, 3, 2]);
      return;
    }
    const [, dx, dy] = args;
    for (let y = 0; y < IH; y++) for (let x = 0; x < IW; x++) {
      const sx = dx + x + 0.5, sy = dy + y + 0.5;
      const devX = m[0] * sx + m[2] * sy + m[4], devY = m[1] * sx + m[3] * sy + m[5];
      const [rx, ry] = turn(x, y, q);
      assert.ok(Math.abs(devX - (rx - cropRect.x + 0.5)) < 1e-9 && Math.abs(devY - (ry - cropRect.y + 0.5)) < 1e-9,
        `pixel ${x},${y} → ${devX},${devY}`);
    }
  });
}
