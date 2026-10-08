// The zoom buttons' press-and-hold (js/ui/bindings/viewport/holdZoom.js) over the real ZoomPan: the
// view a press zoomed from is kept for a drag to return to, stop() ends the hold before its repeat,
// and holdStep is the held rate a fit drag steps at too.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installViewport } from '../../../helpers/zoomViewportRig.js';
import { createStubElement } from '../../../helpers/dom.js';
import { setupHoldZoom, holdStep, HOLD_STEP, HOLD_REPEAT_MS } from '../../../../js/ui/bindings/viewport/holdZoom.js';

const rig = () => {
  const view = installViewport({ vpW: 1000, vpH: 700, imgW: 2000, imgH: 1400, scale: 1 });
  globalThis.window.addEventListener = () => {};
  let clock = 1000;
  globalThis.performance = { now: () => (clock += 1000) };
  view.vp.scrollLeft = 300;
  view.vp.scrollTop = 200;
  const btn = createStubElement('button');
  return { ...view, btn };
};
const press = (btn) => btn.dispatch('mousedown', { button: 0, preventDefault() {} });

test('a press keeps the view it zoomed from', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'setInterval'] });
  const { app, zp, btn } = rig();
  const hold = setupHoldZoom(zp, btn, +1);
  assert.equal(hold.pressAnchor(), null, 'nothing before a press');
  const before = zp.viewAnchor();
  press(btn);
  assert.ok(app.scale > 1, 'the press took its step');
  assert.deepEqual(hold.pressAnchor(), before);
  hold.stop();
});

test('stop() ends a press before its hold repeats', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'setInterval'] });
  const { app, zp, btn } = rig();
  const hold = setupHoldZoom(zp, btn, +1);
  press(btn);
  const stepped = app.scale;
  hold.stop();
  for (let i = 0; i < 20; i++) t.mock.timers.tick(100);
  assert.equal(app.scale, stepped, 'no held steps after stop');
  press(btn);
  const again = app.scale;
  t.mock.timers.tick(480);
  for (let i = 0; i < 3; i++) t.mock.timers.tick(HOLD_REPEAT_MS);
  assert.ok(Math.abs(app.scale - (again + HOLD_STEP * 3)) < 1e-9, 'an unstopped hold repeats at the held rate');
  hold.stop();
});

test('holdStep moves the zoom one held step either way, inside the limits', () => {
  const { app, zp } = rig();
  holdStep(zp, +1);
  assert.ok(Math.abs(app.scale - (1 + HOLD_STEP)) < 1e-9);
  holdStep(zp, -1);
  holdStep(zp, -1);
  assert.ok(Math.abs(app.scale - (1 - HOLD_STEP)) < 1e-9);
  zp.setZoom(zp.clampScale(1e9));
  holdStep(zp, +1);
  assert.equal(app.scale, zp.clampScale(1e9), 'the top of the range holds');
});
