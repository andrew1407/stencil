// The modal box's height ease (js/ui/motion/easeBoxHeight.js): a box sized by its content
// snaps when that content changes, and this eases it there instead. Node has neither
// ResizeObserver nor WAAPI, so both are stubs — what is pinned is WHICH element is watched,
// when a flight starts, and what it starts FROM.
import test from 'node:test';
import assert from 'node:assert';

import { setMotionPrefs } from '../../../js/ui/motion/motionPrefs.js';

// A WAAPI-shaped flight that never resolves on its own; `settle()` finishes it.
class StubAnimation {
  constructor(frames, opts) {
    this.frames = frames;
    this.opts = opts;
    this.cancelled = false;
    this.finished = new Promise((res, rej) => { this.settle = res; this.reject = rej; });
    this.finished.catch(() => {});
  }
  cancel() { this.cancelled = true; this.reject(new Error('cancelled')); }
}
globalThis.Animation = StubAnimation;

// One observer at a time is all the module makes; the rig fires its callback by hand.
let observer = null;
globalThis.ResizeObserver = class {
  constructor(cb) { this.cb = cb; this.watched = []; this.disconnected = false; observer = this; }
  observe(el) { this.watched.push(el); }
  disconnect() { this.disconnected = true; }
};

const { easeBoxHeight, modalBoxEase, BOX_RESIZE_MS } = await import('../../../js/ui/motion/easeBoxHeight.js');

const makeRow = (flights = []) => ({ getAnimations: () => flights });

// The box reports `natural` whenever no inline height is pinned on it.
const makeBox = (natural) => {
  const flights = [];
  const style = {
    height: '',
    removeProperty(k) { style[k] = ''; },
  };
  const box = {
    style, flights,
    natural,
    get offsetHeight() { return style.height ? parseFloat(style.height) : box.natural; },
    animate(frames, opts) { const a = new StubAnimation(frames, opts); flights.push(a); return a; },
  };
  return box;
};

const rig = (natural, rows) => {
  const box = makeBox(natural);
  const scroller = { children: rows };
  const stop = easeBoxHeight(box, scroller);
  return { box, scroller, stop };
};

test('it watches the ROWS — never the box or the scroller it would measure back from', () => {
  const rows = [makeRow(), makeRow(), makeRow()];
  const { box, scroller, stop } = rig(300, rows);
  assert.deepStrictEqual(observer.watched, rows);
  assert.ok(!observer.watched.includes(box) && !observer.watched.includes(scroller));
  stop();
});

test('a box with no ResizeObserver or no WAAPI is left alone, and still hands back a stop', () => {
  const saved = globalThis.ResizeObserver;
  delete globalThis.ResizeObserver;
  const noRo = easeBoxHeight(makeBox(300), { children: [makeRow()] });
  assert.strictEqual(typeof noRo, 'function');
  noRo();
  globalThis.ResizeObserver = saved;

  const plain = makeBox(300);
  delete plain.animate;
  const noWaapi = easeBoxHeight(plain, { children: [makeRow()] });
  assert.strictEqual(typeof noWaapi, 'function');
  noWaapi();
});

test('the FIRST settle only records the height — there is nothing yet to fly from', () => {
  const { box, stop } = rig(300, [makeRow()]);
  observer.cb();
  assert.strictEqual(box.flights.length, 0, 'the box arriving is not a change of height');
  assert.strictEqual(box.style.height, '', 'and it keeps no inline height');
  stop();
});

test('a later change eases from the height last shown to the box\'s own natural one', () => {
  const { box, stop } = rig(300, [makeRow()]);
  observer.cb();            // 300 is what is on screen
  box.natural = 460;        // a crop stage opened
  observer.cb();

  assert.strictEqual(box.flights.length, 1);
  const f = box.flights[0];
  assert.deepStrictEqual(f.frames, [{ height: '300px' }, { height: '460px' }]);
  assert.strictEqual(f.opts.duration, BOX_RESIZE_MS);
  // The layout is ALREADY at the target, so the start has to be pinned or the box paints
  // one frame there and the flight rewinds — a snap followed by a slide.
  assert.strictEqual(box.style.height, '300px');

  f.settle();
  stop();
});

test('the inline height is handed back once the flight lands', async () => {
  const { box, stop } = rig(300, [makeRow()]);
  observer.cb();
  box.natural = 460;
  observer.cb();
  box.flights[0].settle();
  await box.flights[0].finished;
  await null;
  assert.strictEqual(box.style.height, '', 'the layout owns the height again');
  stop();
});

test('a change arriving mid-flight is CHASED: the old flight is cancelled, not queued', () => {
  const { box, stop } = rig(300, [makeRow()]);
  observer.cb();
  box.natural = 460;
  observer.cb();
  const first = box.flights[0];

  box.natural = 520;
  observer.cb();
  assert.strictEqual(first.cancelled, true);
  assert.strictEqual(box.flights.length, 2);
  assert.strictEqual(box.flights[1].frames[1].height, '520px');
  box.flights[1].settle();
  stop();
});

test('a row still mid-flight holds the ease off — its own motion owns the height', () => {
  const row = makeRow([new StubAnimation([], {})]);
  const { box, stop } = rig(300, [row]);
  observer.cb();
  box.natural = 460;
  observer.cb();
  assert.strictEqual(box.flights.length, 0, 'the row is still growing; its end is the target');
  stop();
});

test('a control\'s own CSS transition does NOT hold it off, or every tab switch would', () => {
  // A CSSTransition is not an Animation — only a scripted flight counts.
  const row = makeRow([{ constructor: class CSSTransition {} }]);
  const { box, stop } = rig(300, [row]);
  observer.cb();
  box.natural = 460;
  observer.cb();
  assert.strictEqual(box.flights.length, 1);
  box.flights[0].settle();
  stop();
});

test('reduced motion records the height and moves nothing', () => {
  setMotionPrefs({ mode: 'none' });
  try {
    const { box, stop } = rig(300, [makeRow()]);
    observer.cb();
    box.natural = 460;
    observer.cb();
    assert.strictEqual(box.flights.length, 0);
    assert.strictEqual(box.style.height, '', 'and no inline height is left pinned on it');
    stop();
  } finally { setMotionPrefs({ mode: 'particles' }); }
});

test('the stop disconnects, cancels the flight and releases the height', () => {
  const { box, stop } = rig(300, [makeRow()]);
  observer.cb();
  box.natural = 460;
  observer.cb();
  const f = box.flights[0];
  stop();
  assert.strictEqual(observer.disconnected, true);
  assert.strictEqual(f.cancelled, true);
  assert.strictEqual(box.style.height, '');
});

test('modalBoxEase reaches for the modal and its body, and stopping twice is safe', () => {
  const asked = [];
  const overlay = { querySelector: (sel) => { asked.push(sel); return null; } };
  const saved = globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame = (fn) => { fn(); return 1; };
  try {
    const ease = modalBoxEase(overlay);
    ease.start();
    assert.deepStrictEqual(asked, ['.app-modal', '.settings-body']);
    ease.stop();
    ease.stop();
  } finally {
    if (saved) globalThis.requestAnimationFrame = saved;
    else delete globalThis.requestAnimationFrame;
  }
});
