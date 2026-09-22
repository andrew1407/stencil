// A window's edges resize it (js/ui/modal/resize.js): the band along each edge, the rect the
// drag produces with its far edge anchored, and the wiring that holds the size until the next open.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { edgeAt, resizeRect, wireModalResize, MIN_W, MIN_H, CURSORS } from '../../../js/ui/modal/resize.js';

const BOX = { left: 100, top: 100, width: 400, height: 300 };
const VIEW = { width: 1000, height: 800 };

test('edgeAt names the edge under the pointer, corners included, nothing inside or away', () => {
  assert.equal(edgeAt(BOX, 300, 102), 'n');
  assert.equal(edgeAt(BOX, 300, 398), 's');
  assert.equal(edgeAt(BOX, 498, 250), 'e');
  assert.equal(edgeAt(BOX, 102, 250), 'w');
  assert.equal(edgeAt(BOX, 498, 398), 'se');
  assert.equal(edgeAt(BOX, 102, 102), 'nw');
  assert.equal(edgeAt(BOX, 300, 250), '', 'the middle is not an edge');
  assert.equal(edgeAt(BOX, 300, 50), '', 'nor is the page beside the box');
  assert.equal(edgeAt(null, 0, 0), '');
  for (const dir of ['n', 's', 'e', 'w', 'ne', 'nw', 'se', 'sw']) assert.ok(CURSORS[dir].endsWith('-resize'));
});

test('resizeRect grows from the dragged edge and keeps the opposite one where it was', () => {
  assert.deepEqual(resizeRect(BOX, 'se', 50, 40, VIEW), { left: 100, top: 100, width: 450, height: 340 });
  assert.deepEqual(resizeRect(BOX, 'nw', -50, -40, VIEW), { left: 50, top: 60, width: 450, height: 340 });
  assert.deepEqual(resizeRect(BOX, 'e', -1000, 0, VIEW).width, MIN_W, 'never under the minimum width');
  assert.deepEqual(resizeRect(BOX, 'n', 1000, 0, VIEW), BOX, 'a horizontal pull on a horizontal edge does nothing');
  assert.equal(resizeRect(BOX, 's', 0, 1000, VIEW).height, VIEW.height - 20 - BOX.top, 'stops at the viewport margin');
  assert.equal(resizeRect(BOX, 'w', 1000, 0, VIEW).width, MIN_W);
  assert.equal(resizeRect(BOX, 'n', 0, 1000, VIEW).height, MIN_H);
});

const rig = () => {
  globalThis.window = globalThis.window || {};
  globalThis.window.innerWidth = VIEW.width;
  globalThis.window.innerHeight = VIEW.height;
  let rect = { ...BOX };
  const box = createStubElement('div', { getBoundingClientRect: () => ({ ...rect, right: rect.left + rect.width, bottom: rect.top + rect.height }) });
  const overlay = createStubElement('div');
  overlay.appendChild(box);
  const wired = wireModalResize(overlay, () => box);
  return { box, overlay, wired, setRect: (r) => { rect = { ...rect, ...r }; } };
};
const pointer = (type, x, y, id = 1) => ({ type, pointerId: id, clientX: x, clientY: y, preventDefault() {}, stopPropagation() {} });

test('a press on the edge band drags the size, held on the box until reset', () => {
  const { box, overlay, wired } = rig();
  overlay.dispatch('pointermove', pointer('pointermove', 498, 250));
  assert.equal(box.dataset.edge, 'e', 'hovering the edge names it for the cursor');
  overlay.dispatch('pointerdown', pointer('pointerdown', 498, 250));
  assert.ok(box.classList.contains('modal-resizing'));
  overlay.dispatch('pointermove', pointer('pointermove', 548, 250));
  assert.equal(box.style.width, '450px');
  assert.equal(box.style.height, '300px');
  assert.equal(box.style.maxHeight, 'none', 'the shell ceiling yields to the held size');
  overlay.dispatch('pointerup', pointer('pointerup', 548, 250));
  assert.ok(!box.classList.contains('modal-resizing'));
  assert.equal(box.style.width, '450px', 'the size stays after the release');
  wired.reset();
  assert.equal(box.style.width, '', 'the next open starts at the window\'s own size');
  assert.equal(box.dataset.edge, undefined);
});

test('the middle of the box, a popover and a closing window are left alone', () => {
  const { box, overlay } = rig();
  overlay.dispatch('pointerdown', pointer('pointerdown', 300, 250));
  assert.ok(!box.classList.contains('modal-resizing'), 'the middle is the window\'s own');
  overlay.classList.add('modal-popover');
  overlay.dispatch('pointerdown', pointer('pointerdown', 498, 250));
  assert.ok(!box.classList.contains('modal-resizing'), 'a popover keeps its opener\'s shape');
  overlay.classList.remove('modal-popover');
  overlay.classList.add('modal-closing');
  overlay.dispatch('pointerdown', pointer('pointerdown', 498, 250));
  assert.ok(!box.classList.contains('modal-resizing'), 'a closing window is the flight\'s');
});
