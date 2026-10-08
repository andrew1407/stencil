// The zoom buttons' drags (js/ui/drag/zoomDrag.js) over the real ZoomPan: − / + follow the
// pointer's distance as z0·e^(±k·d) about the viewport centre, undoing the press's own step, and a
// release over the button restores z0; fit lights both and steps through them, its cancel restoring.
// The desktop twin is pinned to the same curve and held rate.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installViewport, seenAt } from '../../helpers/zoomViewportRig.js';
import { createStubElement } from '../../helpers/dom.js';
import { desktopSource } from '../../helpers/desktopSource.js';
import { HOLD_STEP, HOLD_REPEAT_MS } from '../../../js/ui/bindings/viewport/holdZoom.js';
import { createIconDrag, TARGET_CLASS, TARGET_OVER_CLASS } from '../../../js/ui/drag/iconDrag.js';
import {
  ZOOM_DRAG_K, zoomAtDistance, zoomDragHooks, createFitStepper, fitDragHooks,
} from '../../../js/ui/drag/zoomDrag.js';

const near = (a, b, msg) => assert.ok(Math.abs(a - b) < 1e-9, `${msg}: ${a} vs ${b}`);
const ICON = { left: 100, top: 10, right: 128, bottom: 38, width: 28, height: 28 };
const CX = 114, CY = 24;

test('zoomAtDistance: 300 px spans ×8 in and ÷8 out; no distance is z0; the clamp has the last word', () => {
  near(zoomAtDistance(0.5, 300, +1), 4, '×8');
  near(zoomAtDistance(0.5, 300, -1), 0.0625, '÷8');
  near(zoomAtDistance(0.5, 0, +1), 0.5, 'z0');
  near(zoomAtDistance(0.5, 150, +1), 0.5 * Math.sqrt(8), 'halfway is the square root');
  near(Math.exp(ZOOM_DRAG_K * 300), 8, 'k');
  assert.equal(zoomAtDistance(4, 600, +1, (s) => Math.min(s, 32)), 32);
});

test('the desktop twin follows the same curve and steps at the same held rate', () => {
  const cpp = desktopSource('app/drag/zoomFollow');
  const num = (name) => Number(new RegExp(`${name} = ([\\d.e+-]+);`).exec(cpp)?.[1]);
  near(num('ZOOM_DRAG_K'), ZOOM_DRAG_K, 'k');
  assert.equal(num('HOLD_ZOOM_STEP'), HOLD_STEP);
  assert.equal(num('HOLD_ZOOM_TICK_MS'), HOLD_REPEAT_MS);
});

const zoomRig = (sign) => {
  const view = installViewport({ vpW: 1000, vpH: 700, imgW: 2000, imgH: 1400, scale: 1 });
  view.vp.scrollLeft = 300;
  view.vp.scrollTop = 200;
  const anchor = view.zp.viewAnchor();
  let stopped = 0;
  const hold = { stop: () => { stopped++; }, pressAnchor: () => anchor };
  view.zp.setZoom(1.1);   // the step the press took (setupHoldZoom)
  const btn = createStubElement('button', { getBoundingClientRect: () => ICON });
  const m = createIconDrag({ ...zoomDragHooks({ btn, sign, zp: view.zp, hold }), originRect: () => ICON });
  return { ...view, anchor, m, stopped: () => stopped };
};

test('zoom +: the press step is undone and the hold stops as the drag starts, then the distance drives', () => {
  const { app, vp, anchor, m, stopped } = zoomRig(+1);
  m.press(CX, CY);
  m.move(CX, CY + 100);
  assert.equal(stopped(), 1);
  near(app.scale, zoomAtDistance(1, 100, +1), 'z0·e^(k·100)');
  m.move(CX + 180, CY + 240);
  near(app.scale, 8, 'three hundred pixels out');
  const c = seenAt(app, vp, anchor.x, anchor.y);
  assert.ok(Math.abs(c.x - 500) < 1 && Math.abs(c.y - 350) < 1, 'about the viewport centre');
  m.move(CX + 3, CY + 4);
  near(app.scale, zoomAtDistance(1, 5, +1), 'back near the button, back near z0');
});

test('zoom +: released over the button the zoom and the centred point are z0\'s again', () => {
  const { app, vp, m } = zoomRig(+1);
  m.press(CX, CY);
  m.move(CX, CY + 250);
  m.release(CX + 2, CY + 2);
  assert.equal(app.scale, 1);
  assert.deepEqual([vp.scrollLeft, vp.scrollTop], [300, 200]);
});

test('zoom −: away it zooms out, and a release there keeps the zoom', () => {
  const { app, m } = zoomRig(-1);
  m.press(CX, CY);
  m.move(CX, CY + 300);
  m.release(CX, CY + 300);
  near(app.scale, 1 / 8, 'z0 ÷ 8 kept');
});

test('createFitStepper: steps at once on entering, repeats while over, one sign at a time', () => {
  const steps = [];
  const timers = new Map();
  let seq = 0;
  const s = createFitStepper({ step: (sign) => steps.push(sign), repeatMs: 90,
    setTimer: (fn) => { timers.set(++seq, fn); return seq; }, clearTimer: (id) => timers.delete(id) });
  s.over(1);
  s.over(1);
  for (const fn of timers.values()) fn();
  assert.deepEqual(steps, [1, 1], 'once now, once per repeat, not restarted');
  s.over(-1);
  assert.equal(timers.size, 1, 'the + repeat was dropped');
  s.stop();
  assert.equal(timers.size, 0);
  assert.deepEqual(steps, [1, 1, -1]);
  assert.equal(s.sign, 0);
});

const fitRig = () => {
  const view = installViewport({ vpW: 1000, vpH: 700, imgW: 2000, imgH: 1400, scale: 1 });
  view.zp.fitToWindow();
  const zoomIn = createStubElement('button');
  const zoomOut = createStubElement('button');
  const page = createStubElement('div');
  const over = [];
  const stepper = { over: (s) => over.push(s), stop: () => over.push('stop') };
  const m = createIconDrag({
    ...fitDragHooks({ zp: view.zp, zoomIn, zoomOut, stepper }),
    originRect: () => ICON,
    targetAt: (x) => (x >= 300 && x < 330 ? zoomIn : x >= 260 && x < 290 ? zoomOut : page),
  });
  const glow = (el) => [el.classList.contains(TARGET_CLASS), el.classList.contains(TARGET_OVER_CLASS)];
  return { ...view, m, over, zoomIn, zoomOut, glow };
};

test('fit: − and + glow as targets, the one under the pointer brighter, and step while it is there', () => {
  const { m, over, zoomIn, zoomOut, glow } = fitRig();
  m.press(CX, CY);
  m.move(CX, CY + 40);
  assert.deepEqual([glow(zoomIn), glow(zoomOut)], [[true, false], [true, false]]);
  m.move(310, 24);
  assert.deepEqual([glow(zoomIn), glow(zoomOut)], [[true, true], [true, false]]);
  m.move(270, 24);
  assert.deepEqual([glow(zoomIn), glow(zoomOut)], [[true, false], [true, true]]);
  m.release(270, 24);
  assert.deepEqual(over, [0, 1, -1, 'stop']);
  assert.deepEqual([glow(zoomIn), glow(zoomOut)], [[false, false], [false, false]]);
});

test('fit: released over fit, the zoom returns to where the drag began', () => {
  const { app, zp, m } = fitRig();
  const fitted = app.scale;
  m.press(CX, CY);
  m.move(310, 24);
  zp.setZoom(fitted * 3);   // what the steps over + did
  m.release(CX + 1, CY + 1);
  assert.equal(app.scale, fitted);
});
