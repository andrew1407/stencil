// Press, drag, release on a selector (js/ui/control/dropdownMenu.js createDragPick / wireDragPick): a
// drag past the slop opens the list, the row under the pointer is marked, the release picks it through
// its own click or, off the list, closes it with no change; a plain click stays the trigger's.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createDragPick, wireDragPick, DRAG_PICK_ROW_CLASS } from '../../../js/ui/control/dropdownMenu.js';
import { PRESS_SLOP_PX } from '../../../js/ui/tip/popover.js';

// The list spans x 0..100, y 40..100; its one row is y 40..60.
const ROW = { id: 'row' };
const machineRig = ({ open = false } = {}) => {
  const log = [];
  let shown = open;
  const m = createDragPick({
    open: () => { log.push('open'); shown = true; },
    close: () => { log.push('close'); shown = false; },
    isOpen: () => shown,
    inList: (x, y) => shown && x >= 0 && x <= 100 && y >= 40 && y <= 100,
    rowAt: (x, y) => (shown && x >= 0 && x <= 100 && y >= 40 && y <= 60 ? ROW : null),
    pick: (row) => log.push(`pick:${row.id}`),
  });
  return { m, log };
};

test('a press that stays inside the slop is the trigger\'s own click', () => {
  const { m, log } = machineRig();
  m.press(10, 10);
  assert.equal(m.move(10 + PRESS_SLOP_PX, 10), null);
  assert.equal(m.release(10 + PRESS_SLOP_PX, 10), false);
  assert.deepEqual(log, []);
});

test('past the slop the list opens once and the row under the pointer is reported', () => {
  const { m, log } = machineRig();
  m.press(10, 10);
  assert.equal(m.move(10, 10 + PRESS_SLOP_PX + 1), null);
  assert.equal(m.active, true);
  assert.equal(m.move(20, 50), ROW);
  assert.deepEqual(log, ['open']);
});

test('released on a row it picks it; released off the list it closes with no pick', () => {
  const picked = machineRig();
  picked.m.press(10, 10);
  picked.m.move(20, 50);
  assert.equal(picked.m.release(20, 50), true);
  assert.deepEqual(picked.log, ['open', 'pick:row']);
  const away = machineRig();
  away.m.press(10, 10);
  away.m.move(20, 50);
  away.m.move(300, 300);
  assert.equal(away.m.release(300, 300), true, 'the click it would make is the drag\'s');
  assert.deepEqual(away.log, ['open', 'close']);
});

test('released inside the list but on no row, it stays open; an open list is not reopened', () => {
  const { m, log } = machineRig({ open: true });
  m.press(10, 10);
  m.move(20, 90);
  assert.equal(m.release(20, 90), true);
  assert.deepEqual(log, []);
  m.press(10, 10);
  m.move(20, 90);
  m.abort();
  assert.equal(m.release(20, 90), false, 'an aborted drag leaves its release alone');
});

// The DOM half: window listeners from the press, the row's hover mark, its own click, the swallowed click.
const box = (left, top, right, bottom) => ({ left, top, right, bottom, width: right - left, height: bottom - top });
const domRig = (t, { disabled = false } = {}) => {
  const on = new Map();
  if (!t.timed) t.mock.timers.enable({ apis: ['setTimeout'] });
  t.timed = true;
  globalThis.window = {
    addEventListener: (type, fn) => on.set(type, [...(on.get(type) ?? []), fn]),
    removeEventListener: (type, fn) => on.set(type, (on.get(type) ?? []).filter((f) => f !== fn)),
  };
  t.after(() => { delete globalThis.window; });
  const fire = (type, e) => {
    const ev = { pointerId: 1, type, stopImmediatePropagation() { ev.stopped = true; }, preventDefault() {}, ...e };
    for (const fn of on.get(type) ?? []) fn(ev);
    return ev;
  };
  const classes = new Set();
  const row = { clicks: 0, click() { row.clicks++; }, getBoundingClientRect: () => box(0, 40, 100, 60),
    classList: { add: (c) => classes.add(c), remove: (c) => classes.delete(c) } };
  const menu = { hidden: true, getBoundingClientRect: () => box(0, 40, 100, 100), querySelectorAll: () => [row] };
  let down = null;
  const trigger = { addEventListener: (type, fn) => { if (type === 'pointerdown') down = fn; } };
  const log = [];
  wireDragPick(trigger, menu, {
    open: () => { log.push('open'); menu.hidden = false; },
    close: () => { log.push('close'); menu.hidden = true; },
    enabled: () => !disabled,
  });
  const press = (e = {}) => down({ button: 0, isPrimary: true, pointerType: 'mouse', pointerId: 1, clientX: 10, clientY: 10, ...e });
  return { fire, press, row, menu, classes, log, on };
};

test('dragged onto a row it wears the hover mark; the release clicks it and swallows the real click', (t) => {
  const { fire, press, row, classes, log } = domRig(t);
  press();
  fire('pointermove', { clientX: 20, clientY: 50 });
  assert.deepEqual(log, ['open']);
  assert.ok(classes.has(DRAG_PICK_ROW_CLASS));
  fire('pointerup', { clientX: 20, clientY: 50 });
  assert.equal(row.clicks, 1);
  assert.ok(!classes.has(DRAG_PICK_ROW_CLASS), 'the mark comes off with the gesture');
  assert.equal(fire('click', {}).stopped, true, 'the click the release makes is the drag\'s');
  assert.equal(fire('pointermove', { clientX: 30, clientY: 50 }).stopped, undefined);
});

test('a touch press or a disabled selector starts nothing', (t) => {
  const touch = domRig(t);
  touch.press({ pointerType: 'touch' });
  touch.fire('pointermove', { clientX: 20, clientY: 50 });
  assert.deepEqual(touch.log, []);
  const off = domRig(t, { disabled: true });
  off.press();
  off.fire('pointermove', { clientX: 20, clientY: 50 });
  assert.deepEqual(off.log, []);
});
