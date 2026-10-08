// A window opened at a point (js/ui/modal/{drag,shell,registry}.js): its top-left corner on the
// point inside the viewport through the header drag's own offset; opened from a client rect it flies
// out of that rect and its close goes home to the control named as `backTo`; one already up at full
// size only moves; the registry finds a shell by its overlay id.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

installDom({}, {
  window: { matchMedia: () => ({ matches: false }), innerWidth: 1000, innerHeight: 800, addEventListener: () => {} },
  matchMedia: () => ({ matches: false }),
});
const { placeOffset, wireModalDrag } = await import('../../../js/ui/modal/drag.js');
const { wireModalShell } = await import('../../../js/ui/base.js');
const { shellFor } = await import('../../../js/ui/modal/registry.js');
const { dropAnchor } = await import('../../../js/ui/drag/iconDrag.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
setMotionPrefs({ mode: 'slide' });

const VIEW = { width: 1000, height: 800 };
const BOX = { left: 220, top: 200, width: 560, height: 400 };

test('placeOffset puts the box\'s top-left corner on the point', () => {
  assert.deepEqual(placeOffset(BOX, { x: 300, y: 250 }, VIEW), { dx: 80, dy: 50 });
  assert.deepEqual(placeOffset(BOX, { x: 220, y: 200 }, VIEW), { dx: 0, dy: 0 });
});

test('placeOffset shifts left or up near the right or bottom edge, a margin inside', () => {
  assert.deepEqual(placeOffset(BOX, { x: 900, y: 300 }, VIEW), { dx: 1000 - 8 - 560 - 220, dy: 100 });
  assert.deepEqual(placeOffset(BOX, { x: 300, y: 700 }, VIEW), { dx: 80, dy: 800 - 8 - 400 - 200 });
  assert.deepEqual(placeOffset(BOX, { x: 2, y: 3 }, VIEW), { dx: 8 - 220, dy: 8 - 200 }, 'and right or down near the top-left');
  assert.deepEqual(placeOffset(BOX, { x: 990, y: 790 }, VIEW, 0), { dx: 1000 - 560 - 220, dy: 800 - 400 - 200 });
});

test('placeOffset: a box bigger than the room keeps its top-left edge in view', () => {
  const big = { left: -100, top: -50, width: 1200, height: 900 };
  assert.deepEqual(placeOffset(big, { x: 500, y: 400 }, VIEW), { dx: 108, dy: 58 });
});

// A box at rest where BOX says, moved by whatever `translate` its style carries.
const movableBox = () => {
  const box = createStubElement('div');
  box.getBoundingClientRect = () => {
    const [x = 0, y = 0] = String(box.style.translate || '').split(/\s+/).map(parseFloat);
    return { ...BOX, left: BOX.left + (x || 0), top: BOX.top + (y || 0), bottom: BOX.top + (y || 0) + BOX.height };
  };
  return box;
};

test('placeAt measures from the box\'s rest place, so a moved window lands on the point too', () => {
  const box = movableBox();
  const overlay = createStubElement('div');
  const drag = wireModalDrag(overlay, () => box);
  drag.placeAt({ x: 300, y: 250 }, true);
  assert.equal(box.style.translate, '80px 50px');
  assert.ok(!overlay.classList.contains('modal-measuring'), 'the measuring beat is over');
  drag.placeAt({ x: 100, y: 120 });
  assert.equal(box.style.translate, '-120px -80px');
  drag.reset();
  assert.equal(box.style.translate, '');
});

const ICON = { left: 20, top: 20, width: 28, height: 28, bottom: 48 };
const shellRig = (id) => {
  const box = movableBox();
  const overlay = createStubElement('div', { id, querySelector: (sel) => (sel === '.app-modal' ? box : null) });
  const openBtn = createStubElement('button', { getBoundingClientRect: () => ICON });
  let opens = 0;
  const shell = wireModalShell(overlay, openBtn, null, { onOpen: () => { opens++; } });
  return { box, overlay, openBtn, shell, opens: () => opens };
};
const flightFrom = (box) => [box.style.getPropertyValue('--modal-dx'), box.style.getPropertyValue('--modal-dy')];

test('a drop opens the full window at the point, flying out of the cursor and home to the icon', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { box, overlay, openBtn, shell } = shellRig('at-overlay');
  shell.open(dropAnchor(300, 250), openBtn, { at: { x: 300, y: 250 } });
  assert.equal(shell.isOpen(), true);
  assert.ok(!overlay.classList.contains('modal-popover'));
  assert.equal(box.style.translate, '80px 50px');
  // The placed box is 300..860 × 250..650, so its centre is (580, 450); the cursor is (300, 250).
  assert.deepEqual(flightFrom(box), [`${300 - 580}px`, `${250 - 450}px`], 'out of the cursor');
  // flight.js floors the starting scale, so a 24 px origin on a 560 px box starts at that floor.
  assert.equal(box.style.getPropertyValue('--modal-sx'), String(Math.max(24 / 560, 0.05)), 'from a speck');
  shell.close();
  assert.deepEqual(flightFrom(box), [`${34 - 580}px`, `${34 - 450}px`], 'home to the icon');
  t.mock.timers.tick(1000);
  shell.open();
  assert.equal(box.style.translate, '', 'a plain open is where its flight puts it');
  assert.deepEqual(flightFrom(box), [`${34 - 500}px`, `${34 - 400}px`], '…out of the icon');
  shell.close();
});

test('open({ at }) on a window already up only moves it; a popover becomes the full window there', () => {
  const { box, overlay, openBtn, shell, opens } = shellRig('moving-overlay');
  shell.open(openBtn);
  shell.open(dropAnchor(100, 120), openBtn, { at: { x: 100, y: 120 } });
  assert.equal(opens(), 1, 'not opened again: what it holds stays');
  assert.equal(box.style.translate, '-120px -80px');
  shell.close();
  shell.openPopover(openBtn);
  assert.ok(overlay.classList.contains('modal-popover'));
  shell.open(dropAnchor(300, 250), openBtn, { at: { x: 300, y: 250 } });
  assert.ok(!overlay.classList.contains('modal-popover'));
  assert.equal(box.style.translate, '80px 50px');
  shell.close();
});

test('shellFor finds a wired shell by its overlay id', () => {
  const { shell } = shellRig('found-overlay');
  assert.equal(shellFor('found-overlay'), shell);
  assert.equal(shellFor('missing-overlay'), null);
  assert.equal(shellFor(null), null);
});
