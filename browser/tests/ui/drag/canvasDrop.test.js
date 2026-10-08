// A toolbar action dropped on the canvas (js/ui/drag/canvasDrop.js), driven through the real drag
// machine: the frame glows while live and brighter under the pointer, a release on it acts once,
// anywhere else or back over the icon nothing happens and the glow goes.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { createIconDrag, TARGET_CLASS, TARGET_OVER_CLASS } from '../../../js/ui/drag/iconDrag.js';
import { canvasDropHooks } from '../../../js/ui/drag/canvasDrop.js';

const ICON = { left: 100, top: 10, right: 128, bottom: 38 };
const rig = ({ frame = true } = {}) => {
  const picture = createStubElement('canvas');
  const vp = createStubElement('div', { contains: (n) => n === vp || n === picture });
  const page = createStubElement('div');
  const acts = [];
  // The canvas fills x ≥ 400; the page is everything else.
  const m = createIconDrag({
    ...canvasDropHooks({ frame: () => (frame ? vp : null), act: () => acts.push('act') }),
    originRect: () => ICON,
    targetAt: (x) => (x >= 400 ? picture : page),
  });
  const glow = () => [vp.classList.contains(TARGET_CLASS), vp.classList.contains(TARGET_OVER_CLASS)];
  return { m, acts, glow };
};

test('the frame glows from the start, brighter only while the pointer is over it', () => {
  const { m, glow } = rig();
  m.press(110, 20);
  m.move(110, 60);
  assert.deepEqual(glow(), [true, false]);
  m.move(500, 300);
  assert.deepEqual(glow(), [true, true]);
  m.move(300, 300);
  assert.deepEqual(glow(), [true, false]);
});

test('a release on the canvas acts once and drops the glow', () => {
  const { m, acts, glow } = rig();
  m.press(110, 20);
  m.move(500, 300);
  m.release(520, 310);
  assert.deepEqual(acts, ['act']);
  assert.deepEqual(glow(), [false, false]);
});

test('a release anywhere else does nothing', () => {
  const { m, acts, glow } = rig();
  m.press(110, 20);
  m.move(500, 300);
  m.release(300, 300);
  assert.deepEqual(acts, []);
  assert.deepEqual(glow(), [false, false]);
});

test('back over the icon, or Escape, cancels: nothing happens', () => {
  const { m, acts, glow } = rig();
  m.press(110, 20);
  m.move(500, 300);
  m.release(112, 22);
  m.press(110, 20);
  m.move(500, 300);
  m.abort();
  assert.deepEqual(acts, []);
  assert.deepEqual(glow(), [false, false]);
});

test('with no canvas frame the press stays a press', () => {
  const { m, acts } = rig({ frame: false });
  m.press(110, 20);
  assert.equal(m.move(500, 300), false);
  assert.equal(m.release(500, 300), false, 'the click stays the button\'s');
  assert.deepEqual(acts, []);
});
