// The chat icon's drag (js/ui/drag/chatDrag.js) through the real drag machine: the dock zones
// while it is live, docked on the side a release lands in, floating with its top-left corner on
// the release point anywhere else away from the icon (formed out of the cursor), and nothing at
// all for a cancel.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createIconDrag, dropAnchor } from '../../../js/ui/drag/iconDrag.js';
import { chatDragHooks, chatSpotAt } from '../../../js/ui/drag/chatDrag.js';
import { DOCK_ZONE_BAND } from '../../../js/ui/chat/geometry.js';

const VIEW = { w: 1280, h: 800 };
// Inside the top edge band, as the toolbar's chat icon is.
const ICON = { left: 600, top: 20, right: 628, bottom: 48 };

const rig = ({ chat = true, phone = false } = {}) => {
  const log = [];
  const zones = { show: () => log.push('show'), highlight: (s) => log.push(['lit', s]), hide: () => log.push('hide') };
  const surface = chat ? { openAt: (spot, opts) => log.push(['openAt', spot, opts]) } : null;
  const m = createIconDrag({
    ...chatDragHooks({ chat: () => surface, zones, view: () => VIEW, phone: () => phone }),
    originRect: () => ICON,
  });
  return { m, log };
};

test('chatSpotAt: an edge band names its side, anywhere else is the point itself', () => {
  assert.equal(chatSpotAt(10, 400, VIEW.w, VIEW.h), 'left');
  assert.equal(chatSpotAt(VIEW.w - 5, 400, VIEW.w, VIEW.h), 'right');
  assert.equal(chatSpotAt(640, 4, VIEW.w, VIEW.h), 'top');
  assert.equal(chatSpotAt(640, VIEW.h - 4, VIEW.w, VIEW.h), 'bottom');
  assert.deepEqual(chatSpotAt(640, 400, VIEW.w, VIEW.h), { x: 640, y: 400 });
  assert.deepEqual(chatSpotAt(DOCK_ZONE_BAND + 1, 400, VIEW.w, VIEW.h), { x: DOCK_ZONE_BAND + 1, y: 400 });
});

test('the zones come up as the drag starts and light the side under the pointer', () => {
  const { m, log } = rig();
  m.press(610, 30);
  m.move(20, 400);
  m.move(640, 400);
  assert.deepEqual(log, ['show', ['lit', 'left'], ['lit', null]]);
});

test('over the icon no zone lights, even where the icon sits inside an edge band', () => {
  const { m, log } = rig();
  m.press(610, 30);
  m.move(40, 400);
  m.move(612, 32);
  assert.deepEqual(log.at(-1), ['lit', null]);
});

test('released in a zone the chat opens docked on that side', () => {
  const { m, log } = rig();
  m.press(610, 30);
  m.move(VIEW.w - 20, 400);
  m.release(VIEW.w - 20, 400);
  assert.deepEqual(log.slice(-2), ['hide', ['openAt', 'right', { from: dropAnchor(VIEW.w - 20, 400) }]]);
});

test('released anywhere else it opens floating at the release point, out of the cursor', () => {
  const { m, log } = rig();
  m.press(610, 30);
  m.move(700, 500);
  m.release(710, 520);
  assert.deepEqual(log.slice(-2), ['hide', ['openAt', { x: 710, y: 520 }, { from: dropAnchor(710, 520) }]]);
});

test('back over the icon, or Escape, the zones go and the chat stays as it was', () => {
  const { m, log } = rig();
  m.press(610, 30);
  m.move(20, 400);
  m.release(612, 32);
  m.press(610, 30);
  m.move(20, 400);
  m.abort();
  assert.equal(log.filter((e) => e[0] === 'openAt').length, 0);
  assert.equal(log.at(-1), 'hide');
});

test('no chat panel yet, or a phone-width page, keeps the press a press', () => {
  for (const opts of [{ chat: false }, { phone: true }]) {
    const { m, log } = rig(opts);
    m.press(610, 30);
    assert.equal(m.move(20, 400), false);
    assert.deepEqual(log, [], JSON.stringify(opts));
  }
});
