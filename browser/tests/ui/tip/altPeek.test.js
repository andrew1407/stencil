// A dropdown's Alt peek (js/ui/tip/altPeek.js): Alt+hover opens the list like a mini window, and
// Alt released closes it only when the pointer is outside the list.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { wireAltPeek, wirePeekBox, wireReleasePick } from '../../../js/ui/tip/altPeek.js';
import { LINGER_CLOSE_MS, createModalOpenGesture } from '../../../js/ui/tip/popover.js';

const target = () => {
  const on = {};
  return {
    on, hovered: false,
    addEventListener: (t, f) => { (on[t] ||= []).push(f); },
    removeEventListener: (t, f) => { on[t] = (on[t] || []).filter((x) => x !== f); },
    dispatch: (t, e = {}) => { for (const f of on[t] || []) f({ preventDefault() {}, ...e }); },
    matches(q) { return q === ':hover' && this.hovered; },
    contains(el) { return el === this || (!!el && el.parent === this); },
  };
};

const rig = ({ enabled = () => true, isTyping = () => false } = {}) => {
  globalThis.document = { ...target(), querySelectorAll: () => [] };
  globalThis.window = target();
  const hover = target();
  const menu = { ...target(), hidden: true };
  const peek = wireAltPeek(hover, menu, {
    open: () => { menu.hidden = false; },
    close: () => { peek.notifyClosed(); menu.hidden = true; },
    enabled, isTyping,
  });
  const pressAlt = (e = {}) => document.dispatch('keydown', { key: 'Alt', ...e });
  const releaseAlt = () => document.dispatch('keyup', { key: 'Alt' });
  return { hover, menu, peek, pressAlt, releaseAlt };
};

test('gliding onto the trigger with Alt held peeks the list, and so does Alt pressed over it', () => {
  const a = rig();
  a.hover.dispatch('mouseenter', { altKey: true });
  assert.equal(a.menu.hidden, false);
  const b = rig();
  b.hover.hovered = true;
  b.pressAlt();
  assert.equal(b.menu.hidden, false);
});

test('no Alt, Alt away from the trigger or a disabled trigger opens nothing', () => {
  const a = rig();
  a.hover.dispatch('mouseenter', {});
  a.pressAlt();
  assert.equal(a.menu.hidden, true);
  const b = rig({ enabled: () => false });
  b.hover.dispatch('mouseenter', { altKey: true });
  assert.equal(b.menu.hidden, true);
});

test('a focused text field still peeks, but keeps its own Alt', () => {
  const { hover, menu, pressAlt } = rig({ isTyping: () => true });
  hover.hovered = true;
  let prevented = false;
  pressAlt({ preventDefault: () => { prevented = true; } });
  assert.equal(menu.hidden, false, 'the pointer on the trigger is the intent (a modal auto-focuses its search)');
  assert.equal(prevented, false, 'the field keeps the key');
});

// A mini window (modal/shell.js) around the dropdown: `holds` + wirePeekBox.
const windowAround = (hover) => {
  const box = target();
  hover.parent = box;
  const state = { open: true };
  const g = createModalOpenGesture({
    openFull() {}, openPopover() { state.open = true; }, closePopover() { state.open = false; },
    isPopoverOpen: () => state.open, holds: (el) => box.contains(el),
    isPeekEngaged: () => box.hovered,
  });
  g.dblclick();   // sticky, so a glide from anything it does not hold would close it
  wirePeekBox(box, g);
  return { box, state };
};

test('a dropdown peeking inside a mini window leaves the window open', () => {
  const { hover, menu } = rig();
  const { state } = windowAround(hover);
  hover.dispatch('mouseenter', { altKey: true });
  assert.equal(menu.hidden, false);
  assert.equal(state.open, true, 'the window holds its own dropdown');
  const other = rig();
  other.hover.dispatch('mouseenter', { altKey: true });
  assert.equal(state.open, false, 'a dropdown outside it still glides it shut');
});

test('stepping from a window into its own dropdown list is not a leave', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  globalThis.document = { ...target(), querySelectorAll: () => [] };
  const hover = target();
  const box = target();
  hover.parent = box;
  let leaves = 0;
  wirePeekBox(box, { boxEnter() {}, boxLeave() { leaves++; } });
  const list = { ...target(), __ddTrigger: hover };
  list.closest = () => list;
  box.dispatch('mouseleave', { relatedTarget: list });
  assert.equal(leaves, 0, 'the list on <body> counts as inside');
  list.dispatch('mouseleave', { relatedTarget: null });
  assert.equal(leaves, 1, 'leaving the list outward is the leave');
});

test('Alt released outside the list closes the peek', () => {
  const { hover, menu, releaseAlt } = rig();
  hover.dispatch('mouseenter', { altKey: true });
  releaseAlt();
  assert.equal(menu.hidden, true);
});

test('Alt released inside the list keeps it open until the pointer leaves it', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { hover, menu, releaseAlt } = rig();
  hover.dispatch('mouseenter', { altKey: true });
  menu.hovered = true;
  releaseAlt();
  assert.equal(menu.hidden, false, 'released over the list ⇒ it lingers');
  menu.hovered = false;
  menu.dispatch('mouseleave');
  menu.hovered = true;
  menu.dispatch('mouseenter');
  t.mock.timers.tick(LINGER_CLOSE_MS);
  assert.equal(menu.hidden, false, 'coming back before the linger runs out keeps it');
  menu.hovered = false;
  menu.dispatch('mouseleave');
  t.mock.timers.tick(LINGER_CLOSE_MS);
  assert.equal(menu.hidden, true);
});

test('a leave the pointer contradicts waits for the next move before it counts', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { hover, menu, releaseAlt } = rig();
  hover.dispatch('mouseenter', { altKey: true });
  menu.hovered = true;
  releaseAlt();
  menu.dispatch('mouseleave');   // Chrome's boundary event, a frame ahead of the pointer
  t.mock.timers.tick(LINGER_CLOSE_MS);
  assert.equal(menu.hidden, false, 'still over the list: not a leave yet');
  menu.hovered = false;
  document.dispatch('mousemove');
  t.mock.timers.tick(LINGER_CLOSE_MS);
  assert.equal(menu.hidden, true, 'the next move that is really outside closes it');
});

test('a window blur counts as the release (Alt+Tab eats the keyup)', () => {
  const { hover, menu } = rig();
  hover.dispatch('mouseenter', { altKey: true });
  window.dispatch('blur');
  assert.equal(menu.hidden, true);
});

test('a click-opened list ignores Alt entirely', () => {
  const { hover, menu, releaseAlt } = rig();
  menu.hidden = false;
  hover.dispatch('mouseenter', { altKey: true });
  releaseAlt();
  menu.dispatch('mouseleave');
  assert.equal(menu.hidden, false);
});

test('Alt-gliding onto another dropdown closes the first peek', () => {
  const a = rig();
  a.hover.dispatch('mouseenter', { altKey: true });
  const b = rig();
  b.hover.dispatch('mouseenter', { altKey: true });
  assert.equal(a.menu.hidden, true);
  assert.equal(b.menu.hidden, false);
});

// One shared document key/blur registry: however many dropdowns wire a peek, Alt is heard by
// one keydown, one keyup and one blur listener, and each trigger still answers for itself.
test('every peek trigger shares one keydown, keyup and blur listener', () => {
  const a = rig();
  const more = Array.from({ length: 4 }, () => {
    const hover = target();
    const menu = { ...target(), hidden: true };
    const peek = wireAltPeek(hover, menu, {
      open: () => { menu.hidden = false; }, close: () => { peek.notifyClosed(); menu.hidden = true; },
    });
    return { hover, menu };
  });
  assert.equal(document.on.keydown.length, 1);
  assert.equal(document.on.keyup.length, 1);
  assert.equal(window.on.blur.length, 1);
  more[2].hover.hovered = true;
  a.pressAlt();
  assert.deepEqual(more.map((m) => m.menu.hidden), [true, true, false, true], 'only the hovered trigger peeks');
  assert.equal(a.menu.hidden, true);
});

// The release-to-pick menus (the logo's accent list) ride the same registry: a keyup picks the row
// under the pointer, a blur only releases.
test('a release pick shares the registry; a blur releases without picking', () => {
  rig();
  const menu = target();
  const row = { clicks: 0, click() { this.clicks++; }, matches: (q) => q === ':hover' };
  menu.querySelectorAll = () => [row];
  let releases = 0;
  const g = { altRelease: () => { releases++; } };
  wireReleasePick(menu, g, '.row', { isPeek: () => true, isShowing: () => true });
  assert.equal(document.on.keyup.length, 1);
  assert.equal(window.on.blur.length, 1);
  window.dispatch('blur');
  assert.deepEqual([releases, row.clicks], [1, 0], 'Alt+Tab away picks nothing');
  document.dispatch('keyup', { key: 'Alt' });
  assert.deepEqual([releases, row.clicks], [2, 1], 'the release over a row picks it');
});
