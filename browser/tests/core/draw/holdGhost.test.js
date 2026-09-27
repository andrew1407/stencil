// The hold-to-draw ghost paints from constants.json HOLD_DRAW: its dash in px and the two
// alphas, the values the renderer always drew and the desktop reads through the qrc. A
// recording context stands in for the overlay canvas.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import constants from '../../../js/config/constants.json' with { type: 'json' };
import { Renderer } from '../../../js/core/draw/renderer.js';

const recordingCtx = () => {
  const seen = { dash: [], strokeAlpha: null, fillAlpha: null };
  let alpha = 1;
  return {
    seen,
    save() {}, restore() {}, beginPath() {}, moveTo() {}, lineTo() {}, arc() {},
    set globalAlpha(v) { alpha = v; }, get globalAlpha() { return alpha; },
    setLineDash(d) { seen.dash.push([...d]); },
    stroke() { seen.strokeAlpha = alpha; },
    fill() { seen.fillAlpha = alpha; },
  };
};

test('the ghost segment and point read HOLD_DRAW, unchanged from the literals they replaced', () => {
  const { ghostDashPx, ghostLineAlpha, ghostPointAlpha } = constants.HOLD_DRAW;
  assert.deepEqual([ghostDashPx, ghostLineAlpha, ghostPointAlpha], [[6, 4], 0.45, 0.6]);
  const ctx = recordingCtx();
  const app = {
    ctx, holdPreview: { x: 20, y: 10 }, color: '#ff0000', thickness: 3, pointSize: 4,
    input: { holdAnchorPoint: () => ({ x: 0, y: 0 }) },
  };
  new Renderer(app).drawHoldPreview();
  assert.deepEqual(ctx.seen.dash, [ghostDashPx, []], 'dashed for the segment, solid again after');
  assert.equal(ctx.seen.strokeAlpha, ghostLineAlpha);
  assert.equal(ctx.seen.fillAlpha, ghostPointAlpha);
});

test('with no anchor only the point paints', () => {
  const ctx = recordingCtx();
  new Renderer({ ctx, holdPreview: { x: 1, y: 1 }, color: '#000', pointSize: 2, input: {} }).drawHoldPreview();
  assert.deepEqual(ctx.seen.dash, []);
  assert.equal(ctx.seen.strokeAlpha, null);
  assert.equal(ctx.seen.fillAlpha, constants.HOLD_DRAW.ghostPointAlpha);
});
