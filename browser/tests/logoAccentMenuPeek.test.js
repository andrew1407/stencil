// The Alt peek (js/ui/toolbar.js): it opens like every other icon's mini window, and Alt owns
// its lifetime — release, linger, blur and the swap's synthetic events.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { LINGER_CLOSE_MS } from '../js/ui/popover.js';
import { rig, marked } from './helpers/logoAccentMenuRig.js';

// ── Alt peek: opens like every other icon's mini window ─────────────────
test('pressing Alt while the pointer is over the logo peeks the menu — no click needed', () => {
  const { menu, hover, pressAlt } = rig();
  hover(true);
  pressAlt();
  assert.equal(menu.hidden, false, 'the bare modifier press opens the menu');
});

test('gliding onto the logo with Alt already held peeks too (mouseenter route)', () => {
  const { wrap, menu, hover } = rig();
  hover(true);
  wrap.dispatch('mouseenter', { altKey: true });
  assert.equal(menu.hidden, false);
});

test('Alt does nothing away from the logo or on other keys; typing focus defers the KEY route only', () => {
  const { wrap, menu, doc, hover, pressAlt } = rig();
  pressAlt();
  assert.equal(menu.hidden, true, 'pointer not over the logo ⇒ ignored (covered-by-modal physics too)');

  hover(true);
  doc.dispatch('keydown', { key: 'a', altKey: true, preventDefault: () => {} });
  assert.equal(menu.hidden, true, 'Alt+<letter> hotkeys are not the bare modifier press');

  doc.activeElement = { tagName: 'INPUT', type: 'text' };   // focus parked in a text field
  pressAlt();
  assert.equal(menu.hidden, true, 'pressing Alt mid-typing is typing — deferred, like the other icons');
  wrap.dispatch('mouseenter', { altKey: true });
  assert.equal(menu.hidden, false, 'the deliberate glide still peeks');
  doc.activeElement = null;
});

test('a repeat Alt press leaves the open menu alone (no flicker)', () => {
  const { menu, hover, pressAlt } = rig();
  hover(true);
  pressAlt();
  pressAlt();
  assert.equal(menu.hidden, false);
  assert.ok(!menu.classList.contains('dd-closing'));
});

// ── Peek lifetime: the machine's rules ──────────────────────────────────
test('releasing Alt away from the menu closes the peek through the animated close', () => {
  const { menu, hover, pressAlt, releaseAlt } = rig();
  hover(true);
  pressAlt();
  releaseAlt();
  assert.ok(menu.classList.contains('dd-closing'), 'Alt up ⇒ the peek leaves on its exit animation');
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
});

test('releasing Alt with the pointer INSIDE the menu lingers — it closes when the pointer leaves the box', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { menu, hover, pressAlt, releaseAlt } = rig();
  hover(true);
  pressAlt();
  menu.matches = (sel) => sel === ':hover';       // pointer rests inside the menu
  releaseAlt();
  assert.equal(menu.hidden, false, 'engaged release ⇒ linger, rows stay clickable');
  assert.ok(!menu.classList.contains('dd-closing'));

  menu.dispatch('mouseleave');                    // leaving the lingering box…
  t.mock.timers.tick(LINGER_CLOSE_MS + 10);       // …closes after the grace
  assert.ok(menu.classList.contains('dd-closing') || menu.hidden);
  t.mock.timers.tick(260);
  assert.equal(menu.hidden, true);
});

test('re-entering a lingering menu within the grace cancels the close', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { menu, hover, pressAlt, releaseAlt } = rig();
  hover(true);
  pressAlt();
  menu.matches = (sel) => sel === ':hover';
  releaseAlt();                                   // linger
  menu.dispatch('mouseleave');
  t.mock.timers.tick(LINGER_CLOSE_MS / 2);
  menu.dispatch('mouseenter');                    // back on the box before the grace ran out
  t.mock.timers.tick(1000);
  assert.equal(menu.hidden, false, 'boxEnter cancels the linger close');
});

test('window blur counts as Alt-up (⌘Tab eats the keyup)', () => {
  const { menu, hover, pressAlt, win } = rig();
  hover(true);
  pressAlt();
  assert.equal(menu.hidden, false);
  win.dispatch('blur');
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
});

test('pointer leaving the logo while Alt is still held does NOT close the peek (system semantics)', () => {
  const { menu, hover, pressAlt } = rig();
  hover(true);
  pressAlt();
  hover(false);                                   // glide off into neutral space, Alt down
  assert.equal(menu.hidden, false, 'peeks are swapped by glides / closed by Alt-up, not by hover-out');
  assert.ok(!menu.classList.contains('dd-closing'));
});

test("a swap's synthetic pointerleave touches nothing mid-apply", () => {
  const { wrap, menu, htmlEl, hover, pressAlt } = rig();
  hover(true);
  pressAlt();
  htmlEl.classList.add('theme-instant');          // accent swap in flight
  wrap.dispatch('pointerleave');
  assert.equal(menu.hidden, false);
  assert.ok(wrap.classList.contains('logo-hover'), 'the animation latch holds too');
  htmlEl.classList.remove('theme-instant');
});

test('picking a row while peeking applies the accent and leaves the peek up — Alt still owns its lifetime', () => {
  const { logo, menu, calls, hover, pressAlt, releaseAlt } = rig();
  hover(true);
  pressAlt();
  menu.children.find((li) => li.dataset.key === 'pink').dispatch('click');
  assert.deepEqual(calls, [['setAccent', 'pink', logo]]);
  assert.equal(menu.hidden, false, 'a pick is not a dismissal, even mid-peek');
  assert.deepEqual(marked(menu), ['pink']);
  releaseAlt();                                   // the gesture still ends it
  assert.ok(menu.classList.contains('dd-closing'));
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
});
