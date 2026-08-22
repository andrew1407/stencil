// Handler-level tests for the logo's accent affordances (js/ui/toolbar.js
// wireLogoColorPicker): the right-click / Alt-peek preset menu (shared rows from
// accentPicker.js, LIFETIME from the shared popover gesture machine — ui/popover.js
// glide registry, same as every toolbar icon's mini window), the plain-click cycle
// it must not disturb, and the .logo-hover latch that keeps the hover animation's
// clock running through an accent swap's view transition (which force-drops :hover
// and used to reset the pulse loop).
// Minimal element stubs, same style as accentController.test.js.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { ACCENTS } from '../js/core/accents.js';
import { createModalOpenGesture, LINGER_CLOSE_MS } from '../js/ui/popover.js';
import { wireLogoColorPicker } from '../js/ui/toolbar.js';
import { createStubElement } from './helpers/dom.js';

// ── Stubs ── the shared element factory (dedup'd listeners + dispatch-with-target are
// its defaults); contains() must always hit, so containment guards see dispatched
// targets as "inside".
const makeEl = () => createStubElement('div', { contains: () => true });

// A wired logo + wrap + menu + stub document; returns every handle a test needs.
const rig = ({ accent = 'violet', customAccent = null } = {}) => {
  const logo = makeEl();
  const wrap = makeEl();
  const menu = makeEl();
  menu.hidden = true;
  logo.closest = () => wrap;
  wrap.querySelector = (sel) => (sel === '.logo-accent-menu' ? menu : null);
  wrap.getBoundingClientRect = () => ({ bottom: 64 });

  const htmlEl = makeEl();
  const doc = makeEl();       // reuse the stub for add/removeEventListener bookkeeping
  doc.documentElement = htmlEl;
  doc.createElement = () => makeEl();
  // Handlers read the global `document`/`window` at dispatch time, so the stubs stay
  // installed for the whole file (each rig() replaces them; node isolates test files).
  globalThis.document = doc;
  const win = makeEl();
  win.innerHeight = 800;
  globalThis.window = win;

  const calls = [];
  const app = {
    accent, customAccent,
    setAccent: (k, origin) => calls.push(['setAccent', k, origin]),
    setCustomAccent: (h, origin) => calls.push(['setCustomAccent', h, origin]),
  };
  wireLogoColorPicker(logo, app);
  // Live :hover is what gates the Alt routes (like every icon); helpers flip it and
  // keep the animation latch in step.
  const hover = (on) => {
    wrap.matches = (sel) => on && sel === ':hover';
    wrap.dispatch(on ? 'pointerenter' : 'pointerleave');
  };
  const pressAlt = () => doc.dispatch('keydown', { key: 'Alt', preventDefault: () => {} });
  const releaseAlt = () => doc.dispatch('keyup', { key: 'Alt' });
  return { logo, wrap, menu, doc, htmlEl, win, app, calls, hover, pressAlt, releaseAlt };
};

// Rows carrying the ✓ — aria-selected drives it (accentPicker.test.js pins the CSS).
const marked = (menu) => menu.children
  .filter((li) => li.getAttribute('aria-selected') === 'true').map((li) => li.dataset.key);

// ── The right-click menu ────────────────────────────────────────────────
test('right-click opens the preset menu — the shared rows, no Custom entry', () => {
  const { wrap, menu } = rig();
  let prevented = false;
  wrap.dispatch('contextmenu', { preventDefault: () => { prevented = true; } });
  assert.ok(prevented, 'the native context menu is suppressed');
  assert.equal(menu.hidden, false, 'the menu is open');
  assert.equal(menu.children.length, ACCENTS.length, 'one row per preset, nothing else');
  const keys = menu.children.map((li) => li.dataset.key);
  assert.deepEqual(keys, ACCENTS.map((a) => a.key), 'rows are exactly the ACCENTS presets');
  // Selection reflects the active preset — aria-selected, which is what paints the ✓ in
  // the row's chip (accentPicker.test.js pins the shared rows + the CSS).
  const selected = menu.children.filter((li) => li.getAttribute('aria-selected') === 'true');
  assert.equal(selected.length, 1);
  assert.equal(selected[0].dataset.key, 'violet');
  assert.match(selected[0].innerHTML, /accent-check/, 'the marked row carries the ✓ glyph');
});

test('a custom accent selects no row, and picking one routes through setAccent with the logo origin', () => {
  const { logo, wrap, menu, calls } = rig({ customAccent: '#123456' });
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.ok(menu.children.every((li) => li.getAttribute('aria-selected') === 'false'),
    'custom colour ⇒ no preset row selected');
  menu.children.find((li) => li.dataset.key === 'pink').dispatch('click');
  assert.deepEqual(calls, [['setAccent', 'pink', logo]], 'same path and origin as the click-cycle');
  assert.deepEqual(marked(menu), ['pink'], 'the pick clears the custom state and takes the mark');
});

// ── Picking KEEPS the menu open ─────────────────────────────────────────
test('clicking three rows applies each accent and leaves the menu up, mark following the last', () => {
  const { logo, wrap, menu, calls } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  for (const key of ['pink', 'aqua', 'brown']) {
    menu.children.find((li) => li.dataset.key === key).dispatch('click');
    assert.equal(menu.hidden, false, `still open after picking ${key}`);
    assert.ok(!menu.classList.contains('dd-closing'), 'and never starts its exit');
    assert.deepEqual(marked(menu), [key], 'the ✓ follows the pick');
  }
  assert.deepEqual(calls, [['setAccent', 'pink', logo], ['setAccent', 'aqua', logo], ['setAccent', 'brown', logo]],
    'every pick applied immediately, through the same path and origin');
  // The rows are built once — a pick must not re-render the list out from under the user.
  assert.equal(menu.children.length, ACCENTS.length);
});

test('the mark tracks the picked key, not app.accent — the swap writes it on a LATER beat', () => {
  const { wrap, menu } = rig();     // app.accent stays 'violet': the rig's setAccent is a spy,
  wrap.dispatch('contextmenu', { preventDefault: () => {} });   // like startViewTransition's
  menu.children.find((li) => li.dataset.key === 'grey').dispatch('click');   // deferred callback
  assert.deepEqual(marked(menu), ['grey'],
    'reading app.accent back here would re-mark the OLD preset');
});

test("a swap's synthetic mouseleave does not dismiss the LINGERING menu being recoloured", (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { menu, htmlEl, hover, pressAlt, releaseAlt } = rig();
  hover(true);
  pressAlt();
  menu.matches = (sel) => sel === ':hover';       // pointer rests inside the menu
  releaseAlt();                                   // → linger: rows stay clickable
  assert.equal(menu.hidden, false);

  htmlEl.classList.add('theme-instant');          // a pick's accent swap in flight
  menu.dispatch('mouseleave');                    // the transition's synthetic leave
  t.mock.timers.tick(LINGER_CLOSE_MS + 300);
  assert.equal(menu.hidden, false, 'the menu is still there when the wipe finishes');
  assert.ok(!menu.classList.contains('dd-closing'), 'and was never sent into its exit');

  // …but a REAL leave during the swap still closes it, once the swap is over.
  menu.matches = () => false;
  menu.dispatch('mouseleave');
  htmlEl.classList.remove('theme-instant');
  t.mock.timers.tick(500);                        // settle sees the swap over…
  t.mock.timers.tick(500);                        // …then its :hover re-check runs
  t.mock.timers.tick(LINGER_CLOSE_MS + 10);       // …then the linger grace
  assert.ok(menu.classList.contains('dd-closing') || menu.hidden, 'no pointer, no linger');
});

test('the menu dismisses on Escape and on an outside press — via its exit animation', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, menu, doc } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  doc.dispatch('keydown', { key: 'Escape' });
  assert.ok(menu.classList.contains('dd-closing'));
  t.mock.timers.tick(260);   // no animationend (e.g. reduced motion) — the fallback timer hides it
  assert.equal(menu.hidden, true);

  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.equal(menu.hidden, false);
  wrap.contains = () => false;                    // press lands outside the wrap
  doc.dispatch('pointerdown', { target: {} });
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
});

test('reopening mid-close aborts the exit — the stale animationend cannot hide the fresh menu', () => {
  const { wrap, menu, doc } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  doc.dispatch('keydown', { key: 'Escape' });     // closing…
  wrap.dispatch('contextmenu', { preventDefault: () => {} });   // …reopened before it finished
  assert.ok(!menu.classList.contains('dd-closing'), 'exit state cleared');
  menu.dispatch('animationend');                  // the entrance animation finishing
  assert.equal(menu.hidden, false, 'the reopened menu stays open');
});

test("a ROW's animation cannot end the exit — only the menu's own pop-out does", (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, menu, doc } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  doc.dispatch('keydown', { key: 'Escape' });
  assert.ok(menu.classList.contains('dd-closing'));
  // Every row runs the shared hover shimmer on its ::after (layout.css) and animationend
  // BUBBLES to the <ul>. The pointer is over a row whenever the menu closes, so this
  // lands within a frame of the exit starting — unfiltered, it blinked the menu off.
  menu.dispatch('animationend', { target: menu.children[2] });
  assert.equal(menu.hidden, false, 'the pop-out is still playing');
  assert.ok(menu.classList.contains('dd-closing'));
  menu.dispatch('animationend');            // the menu's own exit finishing
  assert.equal(menu.hidden, true);
  t.mock.timers.tick(300);
  assert.equal(menu.hidden, true, 'and the fallback timer was cleared with it');
});

test('reduced motion: the menu shows and hides outright, with no exit to sit through', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const prev = globalThis.matchMedia;
  globalThis.matchMedia = () => ({ matches: true });
  try {
    const { wrap, menu, doc } = rig();
    wrap.dispatch('contextmenu', { preventDefault: () => {} });
    assert.equal(menu.hidden, false, 'it still opens — animations.css just drops the rise');
    doc.dispatch('keydown', { key: 'Escape' });
    assert.equal(menu.hidden, true, 'hidden at once, not after the fallback timer');
    assert.ok(!menu.classList.contains('dd-closing'), 'and no exit state is left behind');
  } finally { globalThis.matchMedia = prev; }
});

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

// ── CSS contract pins (same style as motion.test.js) ────────────────────
test('animations.css keys the logo loop on the latch, not :hover', () => {
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  assert.match(css, /\.app-logo-wrap\.logo-hover \.app-logo \{ animation: logoPulse/,
    'pulse keyed on .logo-hover (a :hover-gated animation is cancelled by the accent view transition)');
  assert.match(css, /\.app-logo-wrap\.logo-hover::before/,
    'rays ride the same latch');
  assert.ok(!/\.app-logo:hover \{ animation/.test(css),
    'no :hover-gated animation on the logo remains');
});

test('the menu grows OUT OF the logo and shrinks back INTO it — anchored, not a generic rise', () => {
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  // A shared, named pair: any menu hanging off a control can use it by pointing
  // transform-origin at the edge it is anchored to.
  const from = /@keyframes menuFromAnchor \{[^}]*\}[^}]*\}[^}]*\}/.exec(css)?.[0] || '';
  const to = /@keyframes menuToAnchor \{[^}]*\}[^}]*\}[^}]*\}/.exec(css)?.[0] || '';
  assert.match(from, /scale\(0\.66\)/, 'it starts small enough to read as coming from the mark');
  assert.match(to, /scale\(0\.66\)/, 'and the exit is the reverse — back into it');
  assert.ok(/0%\s*\{ opacity: 0;/.test(from) && /22%\s*\{ opacity: 1; \}/.test(from),
    'full opacity while still small (modalFromIcon\'s trick) — you watch it grow, not fade');
  assert.ok(/60%\s*\{ opacity: 0\.85; \}/.test(to),
    'the shrink stays visible before the fade takes over');
  // Origin = the corner nearest the control. The logo copy hangs LEFT-aligned under the
  // logo; the Visuals copy hangs right-aligned under its trigger.
  assert.match(css, /\.accent-dd-menu \{ transform-origin: right top; \}/,
    'the shared copy scales out of its trigger\'s corner');
  assert.match(css, /\.logo-accent-menu, \.cs-dd \.accent-dd-menu \{ transform-origin: left top; \}/,
    'the left-aligned copies scale out of theirs (a centred origin is what made it read as no motion)');
  assert.match(css, /\.accent-dd-menu:not\(\[hidden\]\) \{ animation: menuFromAnchor 0\.22s cubic-bezier\(0\.16, 1, 0\.3, 1\)/,
    'entrance: the app\'s existing menu easing, in its usual duration range');
  assert.match(css, /\.logo-accent-menu\.dd-closing \{ animation: menuToAnchor 0\.18s ease/,
    'exit: the reverse, on the same easing the other pop-outs use');
  // Reduced motion drops BOTH — toolbar.js then hides it outright (test above), so a
  // neutralised exit can't leave the menu sitting on screen for the fallback timer.
  const reduced = /@media \(prefers-reduced-motion: reduce\) \{[^}]*\.accent-dd-menu[^}]*\}/.exec(css)?.[0] || '';
  assert.match(reduced, /\.accent-dd-menu:not\(\[hidden\]\), \.logo-accent-menu\.dd-closing \{ animation: none; \}/,
    'entrance and exit are both neutralised under prefers-reduced-motion');
});

test('components.css lifts the shared 280px cap for the logo menu — toolbar.js re-caps per open', () => {
  const css = readFileSync(new URL('../css/components.css', import.meta.url), 'utf8');
  const block = /\.logo-accent-menu \{[^}]*\}/.exec(css)?.[0] || '';
  assert.match(block, /max-height: none/,
    'without this the full preset list scrolls even in a tall window (the shared cap is sized for the Visuals dialog)');
});

test('motion.js raises theme-instant BEFORE startViewTransition — the latch depends on it', () => {
  const src = readFileSync(new URL('../js/ui/motion.js', import.meta.url), 'utf8');
  const add = src.indexOf('root.classList.add(THEME_INSTANT_CLASS)');
  const svt = src.indexOf('document.startViewTransition(');
  assert.ok(add !== -1 && svt !== -1 && add < svt,
    'the class must already be up when the swap\'s synthetic pointerleave lands');
});
