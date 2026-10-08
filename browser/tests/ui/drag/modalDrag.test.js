// The window icons' drag (js/ui/drag/modalDrag.js): every opener config/uiStrings.json names,
// refused until its window is wired, and a release away from the icon opening the full window with
// its top-left corner on the release point, flying out of the cursor and home to the icon; back
// over the icon nothing opens.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { createIconDrag, dropAnchor, DROP_ANCHOR_PX } from '../../../js/ui/drag/iconDrag.js';
import { windowOpeners, modalDragHooks, wireModalDrags } from '../../../js/ui/drag/modalDrag.js';
import UI_STRINGS from '../../../../common/config/uiStrings.json' with { type: 'json' };

const ICON = { left: 100, top: 10, right: 128, bottom: 38 };

test('windowOpeners lists one row per opener, a window with two openers twice', () => {
  const rows = windowOpeners();
  const ids = rows.map(([id]) => id);
  assert.equal(new Set(ids).size, ids.length, 'an opener opens one window');
  assert.deepEqual(rows.filter(([, o]) => o === 'open-image-modal-overlay').map(([id]) => id).sort(),
    ['load-image-btn', 'open-image-btn']);
  for (const w of UI_STRINGS.windows) {
    for (const id of [].concat(w.opener)) assert.ok(rows.some(([i, o]) => i === id && o === w.overlay), id);
  }
  assert.deepEqual(windowOpeners([{ opener: 'a', overlay: 'x' }, { opener: ['b', 'c'], overlay: 'y' }]),
    [['a', 'x'], ['b', 'y'], ['c', 'y']]);
});

const rig = (wired = true) => {
  const opened = [];
  const shell = { open: (...args) => opened.push(args) };
  const btn = createStubElement('button');
  const m = createIconDrag({
    ...modalDragHooks(btn, 'info-modal-overlay', (id) => (wired && id === 'info-modal-overlay' ? shell : null)),
    originRect: () => ICON,
  });
  return { m, btn, opened };
};

test('a release away from the icon opens its window there, out of the cursor and home to the icon', () => {
  const { m, btn, opened } = rig();
  m.press(110, 20);
  m.move(400, 300);
  m.release(420, 310);
  assert.deepEqual(opened, [[dropAnchor(420, 310), btn, { at: { x: 420, y: 310 } }]]);
  const half = DROP_ANCHOR_PX / 2;
  assert.deepEqual(opened[0][0], { left: 420 - half, top: 310 - half, right: 420 + half, bottom: 310 + half,
    width: DROP_ANCHOR_PX, height: DROP_ANCHOR_PX }, 'a cursor-sized square on the release point');
  assert.equal(DROP_ANCHOR_PX, 24, 'the size the desktop flies from too');
});

test('back over the icon, or Escape, nothing opens', () => {
  const { m, opened } = rig();
  m.press(110, 20);
  m.move(400, 300);
  m.release(112, 22);
  m.press(110, 20);
  m.move(400, 300);
  m.abort();
  assert.deepEqual(opened, []);
});

test('a window not wired yet keeps the press a press', () => {
  const { m, opened } = rig(false);
  m.press(110, 20);
  assert.equal(m.move(400, 300), false);
  assert.equal(m.release(400, 300), false);
  assert.deepEqual(opened, []);
});

test('wireModalDrags wires only the openers the root holds', () => {
  const held = new Map(['projects-btn', 'crop-image', 'open-image-btn', 'load-image-btn']
    .map((id) => [id, createStubElement('button', { id })]));
  const wired = wireModalDrags({ $: (id) => held.get(id) ?? null });
  assert.deepEqual(wired.sort(), [...held.keys()].sort());
  for (const el of held.values()) assert.equal(el.listeners.pointerdown?.length, 1, el.id);
  assert.deepEqual(wireModalDrags(null), []);
});
