// Tests for src/lib/sectionPeek.js — the Alt+hover peek that shows a collapsed
// section's body in a floating mini window without unfolding the accordion.
// Placement and the open/close machine are pure/injected (the dragSections
// pattern), so the whole matrix runs under plain `node --test`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { peekPosition, createSectionPeek, isTypingTarget, PEEK_CLOSE_GRACE_MS } from '../src/lib/sectionPeek.js';

// ── Placement (the popoverPosition rules, kept case-for-case) ──
test('peekPosition: below the anchor, left-aligned, with the gap', () => {
  const p = peekPosition({
    anchor: { left: 12, top: 100, bottom: 120 },
    box: { width: 340, height: 300 },
    viewport: { width: 400, height: 600 },
  });
  assert.deepEqual(p, { left: 12, top: 126 });
});

test('peekPosition: flips above when the bottom would overflow', () => {
  const p = peekPosition({
    anchor: { left: 12, top: 500, bottom: 520 },
    box: { width: 340, height: 300 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(p.top, 500 - 6 - 300);
});

// The same case the browser suite carries: a short box low in the panel still "fits" below by the
// viewport's own measure, but above is where it stops covering the rows under it.
test('peekPosition: flips above when above has more room, even without overflowing below', () => {
  const p = peekPosition({
    anchor: { left: 12, top: 400, bottom: 420 },
    box: { width: 340, height: 120 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(p.top, 400 - 6 - 120);
});

test('peekPosition: clamps into the viewport when neither side fits, and at the right edge', () => {
  const tall = peekPosition({
    anchor: { left: 12, top: 300, bottom: 320 },
    box: { width: 340, height: 700 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(tall.top, 8, 'taller than either side pins inside the top margin');
  const right = peekPosition({
    anchor: { left: 380, top: 100, bottom: 120 },
    box: { width: 340, height: 300 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(right.left, 400 - 8 - 340);
});

// ── The machine. Sections are opaque tokens; timers are held by the test. ──
// `flags.engaged` mimics the owner's pointer/focus-inside check.
const stubPeek = (collapsed = {}) => {
  const state = { ...collapsed };
  const flags = { engaged: false };
  const calls = [];
  const timers = [];
  const peek = createSectionPeek({
    isCollapsed: (s) => !!state[s],
    open: (s) => calls.push(['open', s]),
    close: (s) => calls.push(['close', s]),
    isEngaged: () => flags.engaged,
    setTimer: (fn, ms) => { timers.push({ fn, ms }); return timers.length; },
    clearTimer: (h) => { if (timers[h - 1]) timers[h - 1].cancelled = true; },
  });
  const fire = () => { for (const t of timers.splice(0)) if (!t.cancelled) t.fn(); };
  return { state, flags, calls, peek, fire, timers };
};

test('Alt+hover on a collapsed header opens the peek; without Alt nothing happens', () => {
  const { calls, peek } = stubPeek({ a: true });
  peek.enterHead('a', false);
  assert.deepEqual(calls, []);
  peek.enterHead('a', true);
  assert.deepEqual(calls, [['open', 'a']]);
  assert.equal(peek.openSection(), 'a');
});

test('an EXPANDED section never peeks — it already shows itself', () => {
  const { calls, peek } = stubPeek({ a: false });
  peek.enterHead('a', true);
  peek.altPressed('a');
  assert.deepEqual(calls, []);
});

test('re-hovering the peeked section is idempotent; a second section swaps the peek', () => {
  const { calls, peek } = stubPeek({ a: true, b: true });
  peek.enterHead('a', true);
  peek.enterHead('a', true);
  assert.deepEqual(calls, [['open', 'a']], 'no reopen while already peeking it');
  peek.enterHead('b', true);
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a'], ['open', 'b']]);
});

test('leaving closes after the grace — unless the pointer lands in the panel first', () => {
  const { calls, peek, fire } = stubPeek({ a: true });
  peek.enterHead('a', true);
  peek.leave();
  peek.enterPeek();          // made it into the panel — the close is cancelled
  fire();
  assert.deepEqual(calls, [['open', 'a']]);
  peek.leave();              // left the panel for good
  fire();
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a']]);
  assert.equal(peek.isOpen(), false);
});

test('the close grace uses PEEK_CLOSE_GRACE_MS', () => {
  const { peek, timers } = stubPeek({ a: true });
  peek.enterHead('a', true);
  peek.leave();
  assert.equal(timers[0].ms, PEEK_CLOSE_GRACE_MS);
});

test('HOLD-to-peek: releasing Alt closes immediately, no grace', () => {
  const { calls, peek, fire, timers } = stubPeek({ a: true });
  peek.enterHead('a', true);
  peek.altReleased();
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a']]);
  assert.equal(timers.length, 0, 'no timer involved — the release is instant');
  fire();
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a']]);
  // Holding Alt again re-peeks normally afterwards.
  peek.enterHead('a', true);
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a'], ['open', 'a']]);
});

test('dismiss closes immediately; a stale scheduled close then no-ops', () => {
  const { calls, peek, fire } = stubPeek({ a: true });
  peek.enterHead('a', true);
  peek.leave();
  peek.dismiss();
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a']]);
  fire();                    // the cancelled leave-timer must not close twice
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a']]);
});

test('sectionToggled sends the body home only for the peeked section', () => {
  const { calls, peek } = stubPeek({ a: true, b: true });
  peek.enterHead('a', true);
  peek.sectionToggled('b');
  assert.deepEqual(calls, [['open', 'a']], 'toggling another section leaves the peek alone');
  peek.sectionToggled('a');
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a']]);
});

test('an ENGAGED panel (pointer/typed content inside) survives the Alt release', () => {
  const hover = stubPeek({ a: true });
  hover.peek.enterHead('a', true);
  hover.flags.engaged = true;    // pointer inside, or a field with typed content
  hover.peek.altReleased();
  assert.deepEqual(hover.calls, [['open', 'a']], 'release keeps an engaged panel');
  assert.equal(hover.peek.isOpen(), true);
  // The normal close paths still work on the lingering panel.
  hover.peek.dismiss();
  assert.deepEqual(hover.calls, [['open', 'a'], ['close', 'a']]);
  // Un-engaged (pointer away, nothing typed — e.g. a checkbox was clicked and
  // the pointer moved on): the release closes it, no stranded panel.
  const away = stubPeek({ a: true });
  away.peek.enterHead('a', true);
  away.peek.altReleased();
  assert.deepEqual(away.calls, [['open', 'a'], ['close', 'a']]);
});

test('the hover-out grace re-checks engagement — never closes mid-typing', () => {
  const { flags, calls, peek, fire } = stubPeek({ a: true });
  peek.enterHead('a', true);
  peek.leave();
  flags.engaged = true;          // focus landed in the panel's composer meanwhile
  fire();
  assert.deepEqual(calls, [['open', 'a']], 'the grace landing on an engaged panel is a no-op');
  flags.engaged = false;
  peek.leave();
  fire();
  assert.deepEqual(calls, [['open', 'a'], ['close', 'a']]);
});

test('isTypingTarget: text-entry controls yes; buttons/checkboxes/plain elements no', () => {
  assert.equal(isTypingTarget({ tagName: 'TEXTAREA' }), true);
  assert.equal(isTypingTarget({ tagName: 'SELECT' }), true);
  assert.equal(isTypingTarget({ tagName: 'INPUT', type: 'text' }), true);
  assert.equal(isTypingTarget({ tagName: 'INPUT', type: 'search' }), true);
  assert.equal(isTypingTarget({ tagName: 'INPUT', type: 'number' }), true);
  assert.equal(isTypingTarget({ tagName: 'INPUT' }), true, 'a typeless input defaults to text');
  for (const type of ['checkbox', 'radio', 'file', 'color', 'button'])
    assert.equal(isTypingTarget({ tagName: 'INPUT', type }), false, `input[${type}] is not typing`);
  assert.equal(isTypingTarget({ tagName: 'DIV' }), false);
  assert.equal(isTypingTarget({ tagName: 'DIV', isContentEditable: true }), true);
  assert.equal(isTypingTarget(null), false);
});

test('leave/enterPeek while nothing is open are safe no-ops', () => {
  const { calls, peek, fire } = stubPeek({ a: true });
  peek.leave();
  peek.enterPeek();
  peek.dismiss();
  fire();
  assert.deepEqual(calls, []);
});
