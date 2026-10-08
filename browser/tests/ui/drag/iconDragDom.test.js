// A control's drag on the page (js/ui/drag/iconDrag.js wireIconDrag): every tooltip is held off from
// its start to its end, Escape included; the click its release makes is swallowed, and so is the
// dblclick it makes after a click just before the drag; a plain press holds nothing and keeps its click.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from '../../helpers/dom.js';

const doc = installDom();
const win = createStubElement('window');
globalThis.window = win;
doc.elementFromPoint = () => null;
const { wireIconDrag } = await import('../../../js/ui/drag/iconDrag.js');
const { tipsHeld } = await import('../../../js/ui/tip/tipHold.js');

const ICON = { left: 100, top: 10, right: 128, bottom: 38, width: 28, height: 28 };
const pointer = (x, y) => ({ pointerId: 1, isPrimary: true, button: 0, pointerType: 'mouse', clientX: x, clientY: y });
const after = (type) => {
  const seen = { type, stopped: false, prevented: false };
  win.dispatch(type, { stopImmediatePropagation: () => { seen.stopped = true; }, preventDefault: () => { seen.prevented = true; } });
  return seen;
};
const tick = () => new Promise((r) => setTimeout(r, 0));

const rig = () => {
  const drops = [];
  const el = createStubElement('button', { getBoundingClientRect: () => ICON, setPointerCapture() {} });
  wireIconDrag(el, { ghost: false, drop: (p) => drops.push([p.x, p.y]) });
  return { el, drops };
};

test('a drag swallows the click and the dblclick its release makes, and only those', async () => {
  const { el, drops } = rig();
  el.dispatch('pointerdown', pointer(110, 20));
  win.dispatch('pointermove', pointer(300, 300));
  win.dispatch('pointerup', pointer(320, 310));
  assert.deepEqual(drops, [[320, 310]]);
  for (const type of ['click', 'dblclick']) {
    const seen = after(type);
    assert.ok(seen.stopped && seen.prevented, `${type} swallowed`);
  }
  await tick();
  assert.equal(win.listeners.click?.length ?? 0, 0, 'nothing is left listening');
  assert.equal(after('click').stopped, false, 'the next click is the page\'s');
});

test('a press that never left the slop keeps its click', async () => {
  const { el, drops } = rig();
  el.dispatch('pointerdown', pointer(110, 20));
  win.dispatch('pointermove', pointer(113, 22));
  win.dispatch('pointerup', pointer(113, 22));
  assert.deepEqual(drops, []);
  assert.equal(after('click').stopped, false);
  await tick();
});

test('tooltips are held from the drag\'s start to its end, Escape ending it too', async () => {
  const { el } = rig();
  el.dispatch('pointerdown', pointer(110, 20));
  win.dispatch('pointermove', pointer(113, 22));
  assert.equal(tipsHeld(), false, 'inside the slop it is still a press');
  win.dispatch('pointermove', pointer(300, 300));
  assert.equal(tipsHeld(), true);
  win.dispatch('pointerup', pointer(320, 310));
  assert.equal(tipsHeld(), false);
  el.dispatch('pointerdown', pointer(110, 20));
  win.dispatch('pointermove', pointer(300, 300));
  win.dispatch('keydown', { key: 'Escape', preventDefault() {}, stopImmediatePropagation() {} });
  assert.equal(tipsHeld(), false);
  win.dispatch('pointerup', pointer(300, 300));
  await tick();
});

// The theme lens rides its switch in the circle's middle, wherever the press took hold of it.
const ghostCopy = () => {
  const attrs = new Map();
  const classes = new Set();
  classes.remove = (...c) => c.forEach((x) => classes.delete(x));
  return { style: {}, classList: classes, querySelectorAll: () => [], remove() {},
    setAttribute: (k, v) => attrs.set(k, v), removeAttribute: (k) => attrs.delete(k), hasAttribute: (k) => attrs.has(k) };
};
test('a centred ghost sits centred on the pointer; a plain one keeps the grab point', async () => {
  const ghostAt = (centred) => {
    const el = createStubElement('button', { getBoundingClientRect: () => ICON, setPointerCapture() {},
      cloneNode: () => ghostCopy() });
    wireIconDrag(el, { ghostCentred: centred });
    el.dispatch('pointerdown', pointer(104, 14));
    win.dispatch('pointermove', pointer(300, 300));
    const ghost = [...(doc.body.children ?? doc.body.childNodes ?? [])].findLast((n) => n.hasAttribute?.('data-drag-ghost'));
    const at = { x: parseFloat(ghost.style.left), y: parseFloat(ghost.style.top) };
    win.dispatch('pointerup', pointer(300, 300));
    return at;
  };
  assert.deepEqual(ghostAt(true), { x: 300 - ICON.width / 2, y: 300 - ICON.height / 2 });
  assert.deepEqual(ghostAt(false), { x: 300 - 4, y: 300 - 4 });
  await tick();
});

test('the tips are already held when the owner starts (a lens copies no tip), and freed if it refuses', async () => {
  const seen = [];
  for (const refuse of [false, true]) {
    const el = createStubElement('button', { getBoundingClientRect: () => ICON, setPointerCapture() {} });
    wireIconDrag(el, { ghost: false, start: () => { seen.push(tipsHeld()); return refuse ? false : undefined; } });
    el.dispatch('pointerdown', pointer(110, 20));
    win.dispatch('pointermove', pointer(300, 300));
    assert.equal(tipsHeld(), !refuse, refuse ? 'a refused start holds nothing' : 'held through the drag');
    win.dispatch('pointerup', pointer(300, 300));
    await tick();
  }
  assert.deepEqual(seen, [true, true]);
});
