// The glide registry and the click routes (js/ui/popover.js): one mini window at a time in
// both directions, the sticky right-click path, and Alt+click against the plain-click cycle.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ACCENTS } from '../js/core/settings/accents.js';
import { createModalOpenGesture } from '../js/ui/tip/popover.js';
import { rig } from './helpers/logoAccentMenuRig.js';

// ── The glide registry: one mini window at a time, both directions ──────
test("Alt-hovering ANOTHER icon closes the logo's peek through its animated close", () => {
  const { menu, hover, pressAlt } = rig();
  hover(true);
  pressAlt();
  assert.equal(menu.hidden, false);
  // A stand-in for any other toolbar icon's machine (same registry, same contract).
  let otherOpen = false;
  const other = createModalOpenGesture({
    openFull: () => {}, openPopover: () => { otherOpen = true; },
    closePopover: () => { otherOpen = false; }, isPopoverOpen: () => otherOpen,
  });
  other.altHover();                               // the glide lands on the other icon
  assert.ok(menu.classList.contains('dd-closing'), "the logo's menu is glide-closed");
  assert.ok(otherOpen, "and the other icon's peek shows");
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
  other.notifyClosed();
});

test("gliding back onto the logo closes the other icon's window and reopens the accent menu", () => {
  const { wrap, menu, hover } = rig();
  let otherOpen = false;
  const other = createModalOpenGesture({
    openFull: () => {}, openPopover: () => { otherOpen = true; },
    closePopover: () => { otherOpen = false; }, isPopoverOpen: () => otherOpen,
  });
  other.altHover();
  assert.ok(otherOpen);
  hover(true);
  wrap.dispatch('mouseenter', { altKey: true });  // the glide comes home
  assert.equal(menu.hidden, false, 'accent menu swapped in');
  assert.ok(!otherOpen, "other icon's window swapped out");
});

test('a glide closes a STICKY accent menu too — like any deliberately opened mini window', () => {
  const { wrap, menu } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });   // sticky open
  const other = createModalOpenGesture({
    openFull: () => {}, openPopover: () => {}, closePopover: () => {}, isPopoverOpen: () => false,
  });
  other.altHover();
  assert.ok(menu.classList.contains('dd-closing'), 'sticky minis are glide-closed (system rule)');
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
  other.notifyClosed();
});

// ── Sticky right-click path ─────────────────────────────────────────────
test('the right-click (sticky) menu ignores Alt-up — and right-click promotes an open peek to sticky', () => {
  const { wrap, menu, doc, hover, pressAlt, releaseAlt } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  releaseAlt();
  assert.equal(menu.hidden, false, 'sticky menu keeps its dismissal rules');
  doc.dispatch('keydown', { key: 'Escape' });
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);

  hover(true);
  pressAlt();                                     // peek…
  wrap.dispatch('contextmenu', { preventDefault: () => {} });   // …promoted
  releaseAlt();
  assert.equal(menu.hidden, false, 'after a right-click the menu no longer follows Alt');
});

// ── Alt+click vs the plain-click cycle ──────────────────────────────────
test('Alt+click opens the menu INSTEAD of cycling; a plain click still cycles', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { logo, menu, calls, hover, releaseAlt } = rig();
  hover(true);
  logo.dispatch('click', { altKey: true });
  assert.equal(menu.hidden, false, 'Alt+click opens the menu');
  t.mock.timers.tick(400);
  assert.deepEqual(calls, [], 'and never schedules the deferred accent cycle');
  releaseAlt();
  menu.dispatch('animationend');

  logo.dispatch('click', { altKey: false });
  t.mock.timers.tick(220);
  const keys = ACCENTS.map((a) => a.key);
  const next = keys[(keys.indexOf('violet') + 1) % keys.length];
  assert.deepEqual(calls, [['setAccent', next, logo]], 'plain click keeps the cycle');
});

test('Alt+click cancels a pending cycle; on an Alt-opened menu it is a no-op (part of the hold gesture)', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { logo, menu, calls, hover, releaseAlt } = rig();
  hover(true);
  logo.dispatch('click', { altKey: false });      // cycle armed (220ms)
  logo.dispatch('click', { altKey: true });       // Alt arrives first — peek instead
  t.mock.timers.tick(400);
  assert.deepEqual(calls, [], 'the armed cycle was cancelled');
  assert.equal(menu.hidden, false);
  logo.dispatch('click', { altKey: true });       // clicking the logo mid-peek: no cycle, no close
  assert.equal(menu.hidden, false);
  assert.ok(!menu.classList.contains('dd-closing'), 'the peek is governed by the hold gesture, not the click');
  t.mock.timers.tick(400);
  assert.deepEqual(calls, [], 'and still no cycle fired');
  releaseAlt();                                   // the gesture ends the peek
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
});

test('Alt+click on an open STICKY menu keeps its toggle-shut behaviour', () => {
  const { logo, wrap, menu } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });   // sticky open
  logo.dispatch('click', { altKey: true });
  assert.ok(menu.classList.contains('dd-closing'), 'sticky toggles closed');
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
});
