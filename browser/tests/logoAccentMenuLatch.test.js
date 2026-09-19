// Viewport-fit sizing and the .logo-hover latch (js/ui/toolbar.js): the latch holds through an
// accent swap's view transition, which force-drops :hover, and the two CSS motion hooks.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { rig } from './helpers/logoAccentMenuRig.js';

// ── Viewport-fit sizing ─────────────────────────────────────────────────
test('the menu sizes to the space under the logo — scrolling only in a too-short window', () => {
  const { wrap, menu } = rig();                   // innerHeight 800, logo bottom 64
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.equal(menu.style.maxHeight, '718px', 'tall window: cap = viewport − logo bottom − margin (≥ all rows)');

  globalThis.window.innerHeight = 150;            // shrink, reopen: the cap follows
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.equal(menu.style.maxHeight, '90px', 'short window: floored cap ⇒ overflow-y takes over');
});

// ── The hover latch ─────────────────────────────────────────────────────
test('pointerenter latches .logo-hover; a real pointerleave drops it at once', () => {
  const { wrap, hover } = rig();
  hover(true);
  assert.ok(wrap.classList.contains('logo-hover'));
  hover(false);
  assert.ok(!wrap.classList.contains('logo-hover'));
});

test('a pointerleave during the accent swap (html.theme-instant) holds the latch', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, htmlEl, hover } = rig();
  hover(true);
  htmlEl.classList.add('theme-instant');          // swap in flight — the leave is synthetic
  wrap.dispatch('pointerleave');
  assert.ok(wrap.classList.contains('logo-hover'), 'latch held through the swap');
  t.mock.timers.tick(500);
  assert.ok(wrap.classList.contains('logo-hover'), 'still held while the swap runs');

  // swap over, pointer still on the logo (hover() keeps wrap.matches true)
  htmlEl.classList.remove('theme-instant');
  t.mock.timers.tick(500);
  t.mock.timers.tick(500);   // mock ticks don't cascade into timers a timer schedules
  assert.ok(wrap.classList.contains('logo-hover'), 'pointer stayed ⇒ the loop never restarts');
});

test('…but drops once the swap ends if the pointer really left mid-swap', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, htmlEl, hover } = rig();
  hover(true);
  htmlEl.classList.add('theme-instant');
  wrap.matches = () => false;                     // the pointer is really gone…
  wrap.dispatch('pointerleave');                  // …and leaves while the wipe runs
  htmlEl.classList.remove('theme-instant');
  t.mock.timers.tick(500);                        // settle sees the swap over…
  t.mock.timers.tick(500);                        // …then its :hover re-check runs (no cascade in mock ticks)
  assert.ok(!wrap.classList.contains('logo-hover'), 'no pointer, no latch');
});

// ── The two CSS hooks the motion hangs off ──────────────────────────────
test('open flips [hidden] (the entrance hook) and close raises .dd-closing (the exit hook)', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, menu, doc } = rig();
  assert.equal(menu.hidden, true, 'at rest the entrance selector does not match');
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.equal(menu.hidden, false, ':not([hidden]) now matches ⇒ menuFromAnchor runs');
  assert.ok(!menu.classList.contains('dd-closing'), 'and no exit state while it is open');

  doc.dispatch('keydown', { key: 'Escape' });
  assert.ok(menu.classList.contains('dd-closing'), '.dd-closing ⇒ menuToAnchor runs');
  assert.equal(menu.hidden, false, 'still displayed, or there would be nothing to shrink');
  t.mock.timers.tick(300);   // no animationend at all — the fallback still cleans up
  assert.equal(menu.hidden, true);
  assert.ok(!menu.classList.contains('dd-closing'), 'exit state cleared for the next open');
});
